#include "PhpBuildController.h"

#include "ArchiveExtractor.h"
#include "BuildArtifactCleaner.h"
#include "PhpBuildEnvironment.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

namespace {

int phpMajorVersion(const QString &version)
{
    bool ok = false;
    const int major = version.section(QLatin1Char('.'), 0, 0).toInt(&ok);
    return ok ? major : 0;
}

} // namespace

PhpBuildController::PhpBuildController(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &PhpBuildController::onProcessReadyRead);
    connect(&m_process, &QProcess::readyReadStandardError, this, &PhpBuildController::onProcessReadyRead);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &PhpBuildController::onProcessFinished);
}

bool PhpBuildController::isRunning() const
{
    return m_step != Step::Idle;
}

void PhpBuildController::start(const PhpBuildRequest &request)
{
    if (isRunning()) {
        emit logLine(QStringLiteral("Build is already running."));
        return;
    }

    m_cancelRequested = false;
    m_request = request;
    preparePaths();
    beginDownload();
}

void PhpBuildController::cancel()
{
    if (!isRunning()) {
        return;
    }

    m_cancelRequested = true;
    if (m_reply) {
        m_reply->abort();
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
    }
    fail(QStringLiteral("Cancelled by user."));
}

void PhpBuildController::onDownloadReadyRead()
{
    if (m_reply && m_downloadFile.isOpen()) {
        m_downloadFile.write(m_reply->readAll());
    }
}

void PhpBuildController::onDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        emit progressChanged(m_step == Step::DownloadingLocalPackage ? 47 : 5);
        return;
    }

    if (m_step == Step::DownloadingLocalPackage) {
        const int value = 47 + static_cast<int>((received * 8) / total);
        emit progressChanged(qBound(47, value, 55));
        return;
    }
    if (m_step == Step::DownloadingPecl) {
        const int value = 92 + static_cast<int>((received * 5) / total);
        emit progressChanged(qBound(92, value, 97));
        return;
    }

    const int value = 5 + static_cast<int>((received * 25) / total);
    emit progressChanged(qBound(5, value, 30));
}

void PhpBuildController::onDownloadFinished()
{
    if (!m_reply) {
        return;
    }

    onDownloadReadyRead();
    m_downloadFile.close();

    const QNetworkReply::NetworkError error = m_reply->error();
    const QString errorText = m_reply->errorString();
    m_reply->deleteLater();
    m_reply = nullptr;

    if (m_cancelRequested) {
        return;
    }
    if (error != QNetworkReply::NoError) {
        fail(QStringLiteral("Download failed: %1").arg(errorText));
        return;
    }

    if (m_step == Step::DownloadingLocalPackage) {
        emit logLine(QStringLiteral("Local package download complete: %1").arg(m_localArchivePath));
        beginLocalPackageExtraction();
        return;
    }
    if (m_step == Step::DownloadingPecl) {
        emit logLine(QStringLiteral("PECL package download complete: %1").arg(m_peclArchivePath));
        beginPeclExtraction();
        return;
    }

    emit logLine(QStringLiteral("Download complete: %1").arg(m_archivePath));
    beginExtraction();
}

void PhpBuildController::onProcessReadyRead()
{
    appendLog(QString::fromLocal8Bit(m_process.readAllStandardOutput()));
    appendLog(QString::fromLocal8Bit(m_process.readAllStandardError()));
}

void PhpBuildController::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    onProcessReadyRead();
    if (m_cancelRequested || m_step == Step::Idle) {
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        fail(QStringLiteral("Command failed with exit code %1.").arg(exitCode));
        return;
    }

    switch (m_step) {
    case Step::ConfiguringLocalPackage:
        runLocalPackageMake();
        break;
    case Step::BuildingLocalPackage:
        runLocalPackageInstall();
        break;
    case Step::InstallingLocalPackage:
        ++m_currentLocalPackageIndex;
        runNextLocalPackage();
        break;
    case Step::Configuring:
        emit progressChanged(55);
        runMake();
        break;
    case Step::Building:
        emit progressChanged(85);
        runInstall();
        break;
    case Step::Installing:
        if (m_request.peclExtensions.isEmpty()) {
            markComplete();
        } else {
            m_currentPeclIndex = 0;
            runNextPeclExtension();
        }
        break;
    case Step::InstallingPecl:
        ++m_currentPeclIndex;
        runNextPeclExtension();
        break;
    case Step::PhpizingPecl:
        runPeclConfigure();
        break;
    case Step::ConfiguringPecl:
        runPeclMake();
        break;
    case Step::BuildingPecl:
        runPeclInstall();
        break;
    default:
        break;
    }
}

void PhpBuildController::fail(const QString &message)
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_downloadFile.isOpen()) {
        m_downloadFile.close();
    }

    m_step = Step::Idle;
    emit statusChanged(QStringLiteral("Failed"));
    emit logLine(QStringLiteral("ERROR: %1").arg(message));
    emit finished(false, message, {});
}

void PhpBuildController::appendLog(const QString &text)
{
    const QString normalized = text.trimmed();
    if (!normalized.isEmpty()) {
        emit logLine(normalized);
    }
}

void PhpBuildController::preparePaths()
{
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QDir::home().filePath(QStringLiteral(".cache"));
    }
    cacheRoot = QDir(cacheRoot).filePath(QStringLiteral("langmanager"));
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss"));

    m_workRoot = QDir(cacheRoot).filePath(QStringLiteral("build-%1-%2").arg(m_request.version, stamp));
    m_archivePath = QDir(cacheRoot).filePath(QStringLiteral("downloads/php-%1.tar.xz").arg(m_request.version));
    m_sourcePath = QDir(m_workRoot).filePath(QStringLiteral("php-%1").arg(m_request.version));
    m_installPath = QDir(m_request.installBasePath).filePath(QStringLiteral("php/%1").arg(m_request.version));

    QDir().mkpath(QFileInfo(m_archivePath).absolutePath());
    QDir().mkpath(m_workRoot);
    QDir().mkpath(QFileInfo(m_installPath).absolutePath());
}

void PhpBuildController::beginDownload()
{
    m_step = Step::Downloading;
    emit statusChanged(QStringLiteral("Downloading PHP %1").arg(m_request.version));
    emit progressChanged(0);
    emit logLine(QStringLiteral("Source: %1").arg(m_request.sourceUrl.toString()));
    emit logLine(QStringLiteral("Install prefix: %1").arg(m_installPath));

    m_downloadFile.setFileName(m_archivePath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Cannot write archive: %1").arg(m_downloadFile.errorString()));
        return;
    }

    QNetworkRequest request(m_request.sourceUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LangManager/0.1 QtNetwork"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &PhpBuildController::onDownloadReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &PhpBuildController::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &PhpBuildController::onDownloadFinished);
}

void PhpBuildController::beginExtraction()
{
    m_step = Step::Extracting;
    emit statusChanged(QStringLiteral("Extracting source archive"));
    emit progressChanged(35);

    const QString archivePath = m_archivePath;
    const QString workRoot = m_workRoot;
    QThread *thread = QThread::create([this, archivePath, workRoot]() {
        QString error;
        const bool ok = ArchiveExtractor::extractTarXz(archivePath, workRoot, &error);
        QMetaObject::invokeMethod(this, [this, ok, error]() {
            if (m_cancelRequested || m_step == Step::Idle) {
                return;
            }
            if (!ok) {
                fail(QStringLiteral("Extraction failed: %1").arg(error));
                return;
            }
            emit logLine(QStringLiteral("Extraction complete: %1").arg(m_sourcePath));
            emit progressChanged(45);
            if (m_request.localPackages.isEmpty()) {
                runConfigure();
            } else {
                m_currentLocalPackageIndex = 0;
                runNextLocalPackage();
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void PhpBuildController::runNextLocalPackage()
{
    if (m_currentLocalPackageIndex >= m_request.localPackages.size()) {
        runConfigure();
        return;
    }

    const LocalSourcePackage package = m_request.localPackages.at(m_currentLocalPackageIndex);
    const QString fileName = QFileInfo(package.sourceUrl.path()).fileName();
    m_localArchivePath = QDir(QFileInfo(m_archivePath).absolutePath()).filePath(fileName);
    m_localSourcePath = QDir(m_workRoot).filePath(package.sourceDirectoryName);
    m_localBuildPath = QDir(m_workRoot).filePath(QStringLiteral("%1-build").arg(package.name));
    m_localPrefixPath = QDir(m_installPath).filePath(QStringLiteral("deps/%1").arg(package.name));

    beginLocalPackageDownload();
}

void PhpBuildController::beginLocalPackageDownload()
{
    const LocalSourcePackage package = m_request.localPackages.at(m_currentLocalPackageIndex);
    m_step = Step::DownloadingLocalPackage;
    emit statusChanged(QStringLiteral("Downloading local package %1").arg(package.name));
    emit progressChanged(47);
    emit logLine(QStringLiteral("Local package source: %1").arg(package.sourceUrl.toString()));
    emit logLine(QStringLiteral("Local package prefix: %1").arg(m_localPrefixPath));

    m_downloadFile.setFileName(m_localArchivePath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Cannot write local package archive: %1").arg(m_downloadFile.errorString()));
        return;
    }

    QNetworkRequest request(package.sourceUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LangManager/0.1 QtNetwork"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &PhpBuildController::onDownloadReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &PhpBuildController::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &PhpBuildController::onDownloadFinished);
}

void PhpBuildController::beginLocalPackageExtraction()
{
    const QString archivePath = m_localArchivePath;
    const QString workRoot = m_workRoot;
    m_step = Step::ExtractingLocalPackage;
    emit statusChanged(QStringLiteral("Extracting local package"));
    emit progressChanged(55);

    QThread *thread = QThread::create([this, archivePath, workRoot]() {
        QString error;
        const bool ok = ArchiveExtractor::extractArchive(archivePath, workRoot, &error);
        QMetaObject::invokeMethod(this, [this, ok, error]() {
            if (m_cancelRequested || m_step == Step::Idle) {
                return;
            }
            if (!ok) {
                fail(QStringLiteral("Local package extraction failed: %1").arg(error));
                return;
            }
            emit logLine(QStringLiteral("Local package extraction complete: %1").arg(m_localSourcePath));
            runLocalPackageConfigure();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void PhpBuildController::runLocalPackageConfigure()
{
    const LocalSourcePackage package = m_request.localPackages.at(m_currentLocalPackageIndex);
    const QStringList resolvedArguments = resolveLocalPackagePlaceholders(package.configureArguments, m_request.localPackages, m_installPath);

    if (package.buildSystem == QStringLiteral("cmake")) {
        QDir().mkpath(m_localBuildPath);
        QStringList args;
        args << QStringLiteral("-S") << m_localSourcePath
             << QStringLiteral("-B") << m_localBuildPath
             << QStringLiteral("-DCMAKE_INSTALL_PREFIX=%1").arg(m_localPrefixPath)
             << QStringLiteral("-DCMAKE_BUILD_TYPE=Release")
             << QStringLiteral("-DBUILD_SHARED_LIBS=ON");
        args << resolvedArguments;
        runProcess(Step::ConfiguringLocalPackage, QStringLiteral("cmake"), args, m_localSourcePath);
        return;
    }

    QStringList args;
    args << QStringLiteral("--prefix=%1").arg(m_localPrefixPath);
    args << resolvedArguments;
    runProcess(Step::ConfiguringLocalPackage, QDir(m_localSourcePath).filePath(package.configureProgram), args, m_localSourcePath);
}

void PhpBuildController::runLocalPackageMake()
{
    const LocalSourcePackage package = m_request.localPackages.at(m_currentLocalPackageIndex);
    const int jobs = phpBuildJobs();
    if (package.buildSystem == QStringLiteral("cmake")) {
        runProcess(Step::BuildingLocalPackage, QStringLiteral("cmake"), {QStringLiteral("--build"), m_localBuildPath, QStringLiteral("--parallel"), QString::number(jobs)}, m_localBuildPath);
        return;
    }
    runProcess(Step::BuildingLocalPackage, QStringLiteral("make"), {QStringLiteral("-j%1").arg(jobs)}, m_localSourcePath);
}

void PhpBuildController::runLocalPackageInstall()
{
    const LocalSourcePackage package = m_request.localPackages.at(m_currentLocalPackageIndex);
    if (package.buildSystem == QStringLiteral("cmake")) {
        runProcess(Step::InstallingLocalPackage, QStringLiteral("cmake"), {QStringLiteral("--install"), m_localBuildPath}, m_localBuildPath);
        return;
    }
    runProcess(Step::InstallingLocalPackage, QStringLiteral("make"), package.installArguments, m_localSourcePath);
}

void PhpBuildController::runConfigure()
{
    QStringList args;
    args << QStringLiteral("--prefix=%1").arg(m_installPath)
         << QStringLiteral("--with-config-file-path=%1").arg(QFileInfo(phpIniPath()).absolutePath())
         << QStringLiteral("--with-config-file-scan-dir=%1").arg(phpIniScanDirectory())
         << QStringLiteral("--enable-cli")
         << QStringLiteral("--disable-cgi")
         << QStringLiteral("--disable-phpdbg");
    if (!m_request.peclExtensions.isEmpty()) {
        args << QStringLiteral("--with-pear");
    }
    args << resolveLocalPackagePlaceholders(m_request.configureFlags, m_request.localPackages, m_installPath);
    runProcess(Step::Configuring, QDir(m_sourcePath).filePath(QStringLiteral("configure")), args, m_sourcePath);
}

void PhpBuildController::runMake()
{
    const int jobs = phpBuildJobs();
    runProcess(Step::Building, QStringLiteral("make"), {QStringLiteral("-j%1").arg(jobs)}, m_sourcePath);
}

void PhpBuildController::runInstall()
{
    runProcess(Step::Installing, QStringLiteral("make"), {QStringLiteral("install")}, m_sourcePath);
}

void PhpBuildController::runNextPeclExtension()
{
    if (m_currentPeclIndex >= m_request.peclExtensions.size()) {
        markComplete();
        return;
    }

    const QString extension = m_request.peclExtensions.at(m_currentPeclIndex);
    if (extension == QStringLiteral("igbinary")) {
        beginPeclDownload(extension);
        return;
    } else if (extension == QStringLiteral("redis")) {
        beginPeclDownload(extension);
        return;
    } else if (extension == QStringLiteral("xdebug")) {
        beginPeclDownload(extension);
        return;
    }

    const int progress = 92 + static_cast<int>((m_currentPeclIndex * 7) / qMax(1, m_request.peclExtensions.size()));

    emit progressChanged(qBound(92, progress, 99));
    const QString peclPath = QDir(m_installPath).filePath(QStringLiteral("bin/pecl"));
    runProcess(Step::InstallingPecl, peclPath, {QStringLiteral("install"), QStringLiteral("--force"), extension}, m_installPath);
}

void PhpBuildController::beginPeclDownload(const QString &extension)
{
    QUrl packageUrl;
    QString sourceDirectoryName;
    if (extension == QStringLiteral("igbinary")) {
        packageUrl = QUrl(QStringLiteral("https://pecl.php.net/get/igbinary-3.2.16.tgz"));
        sourceDirectoryName = QStringLiteral("igbinary-3.2.16");
    } else if (extension == QStringLiteral("redis")) {
        packageUrl = QUrl(QStringLiteral("https://pecl.php.net/get/redis-6.3.0.tgz"));
        sourceDirectoryName = QStringLiteral("redis-6.3.0");
    } else if (extension == QStringLiteral("xdebug")) {
        const QString xdebugVersion = phpMajorVersion(m_request.version) == 7
            ? QStringLiteral("3.1.6")
            : QStringLiteral("3.5.1");
        packageUrl = QUrl(QStringLiteral("https://xdebug.org/files/xdebug-%1.tgz").arg(xdebugVersion));
        sourceDirectoryName = QStringLiteral("xdebug-%1").arg(xdebugVersion);
    } else {
        fail(QStringLiteral("Unsupported source PECL extension: %1").arg(extension));
        return;
    }

    m_peclArchivePath = QDir(QFileInfo(m_archivePath).absolutePath()).filePath(QStringLiteral("pecl-%1.tgz").arg(extension));
    m_peclSourcePath = QDir(m_workRoot).filePath(sourceDirectoryName);
    m_step = Step::DownloadingPecl;
    emit statusChanged(QStringLiteral("Downloading PECL extension %1").arg(extension));
    emit logLine(QStringLiteral("PECL package source: %1").arg(packageUrl.toString()));

    m_downloadFile.setFileName(m_peclArchivePath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Cannot write PECL package archive: %1").arg(m_downloadFile.errorString()));
        return;
    }

    QNetworkRequest request(packageUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LangManager/0.1 QtNetwork"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &PhpBuildController::onDownloadReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &PhpBuildController::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &PhpBuildController::onDownloadFinished);
}

void PhpBuildController::beginPeclExtraction()
{
    const QString archivePath = m_peclArchivePath;
    const QString workRoot = m_workRoot;
    m_step = Step::ExtractingPecl;
    emit statusChanged(QStringLiteral("Extracting PECL extension"));

    QThread *thread = QThread::create([this, archivePath, workRoot]() {
        QString error;
        const bool ok = ArchiveExtractor::extractArchive(archivePath, workRoot, &error);
        QMetaObject::invokeMethod(this, [this, ok, error]() {
            if (m_cancelRequested || m_step == Step::Idle) {
                return;
            }
            if (!ok) {
                fail(QStringLiteral("PECL extraction failed: %1").arg(error));
                return;
            }
            emit logLine(QStringLiteral("PECL extraction complete: %1").arg(m_peclSourcePath));
            runPeclPhpize();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void PhpBuildController::runPeclPhpize()
{
    runProcess(Step::PhpizingPecl, QDir(m_installPath).filePath(QStringLiteral("bin/phpize")), {}, m_peclSourcePath);
}

void PhpBuildController::runPeclConfigure()
{
    runProcess(Step::ConfiguringPecl, QDir(m_peclSourcePath).filePath(QStringLiteral("configure")), {
        QStringLiteral("--with-php-config=%1").arg(QDir(m_installPath).filePath(QStringLiteral("bin/php-config"))),
    }, m_peclSourcePath);
}

void PhpBuildController::runPeclMake()
{
    const int jobs = phpBuildJobs();
    runProcess(Step::BuildingPecl, QStringLiteral("make"), {QStringLiteral("-j%1").arg(jobs)}, m_peclSourcePath);
}

void PhpBuildController::runPeclInstall()
{
    runProcess(Step::InstallingPecl, QStringLiteral("make"), {QStringLiteral("install")}, m_peclSourcePath);
}

void PhpBuildController::runProcess(Step step, const QString &program, const QStringList &arguments, const QString &workingDirectory)
{
    m_step = step;

    QString status;
    int progress = 45;
    if (step == Step::Configuring) {
        status = QStringLiteral("Configuring PHP");
        progress = 70;
    } else if (step == Step::ConfiguringLocalPackage) {
        status = QStringLiteral("Configuring local package");
        progress = 58;
    } else if (step == Step::BuildingLocalPackage) {
        status = QStringLiteral("Compiling local package");
        progress = 62;
    } else if (step == Step::InstallingLocalPackage) {
        status = QStringLiteral("Installing local package");
        progress = 66;
    } else if (step == Step::Building) {
        status = QStringLiteral("Compiling PHP");
        progress = 78;
    } else if (step == Step::Installing) {
        status = QStringLiteral("Installing PHP");
        progress = 90;
    } else if (step == Step::PhpizingPecl) {
        status = QStringLiteral("Preparing PECL extension");
        progress = 94;
    } else if (step == Step::ConfiguringPecl) {
        status = QStringLiteral("Configuring PECL extension");
        progress = 95;
    } else if (step == Step::BuildingPecl) {
        status = QStringLiteral("Compiling PECL extension");
        progress = 97;
    } else if (step == Step::InstallingPecl) {
        status = QStringLiteral("Installing PECL extension");
        progress = 98;
    }

    emit statusChanged(status);
    emit progressChanged(progress);
    emit logLine(QStringLiteral("$ %1 %2").arg(program, arguments.join(' ')));

    QString excludedLocalPackage;
    if ((step == Step::ConfiguringLocalPackage
         || step == Step::BuildingLocalPackage
         || step == Step::InstallingLocalPackage)
        && m_currentLocalPackageIndex >= 0
        && m_currentLocalPackageIndex < m_request.localPackages.size()) {
        excludedLocalPackage = m_request.localPackages.at(m_currentLocalPackageIndex).name;
    }

    m_process.setWorkingDirectory(workingDirectory);
    m_process.setProcessEnvironment(phpBuildEnvironment(m_request, m_installPath, excludedLocalPackage));
    m_process.start(program, arguments);
    if (!m_process.waitForStarted(5000)) {
        fail(QStringLiteral("Cannot start %1: %2").arg(program, m_process.errorString()));
        return;
    }

    if (program.endsWith(QStringLiteral("pecl"))) {
        m_process.write("\n\n\n\n\n");
        m_process.closeWriteChannel();
    }
}

QString PhpBuildController::phpIniPath() const
{
    return QDir(m_installPath).filePath(QStringLiteral("lib/php.ini"));
}

QString PhpBuildController::phpIniScanDirectory() const
{
    return QDir(m_installPath).filePath(QStringLiteral("etc/conf.d"));
}

bool PhpBuildController::writePhpIni(QString *errorMessage)
{
    const QString phpIniPath = this->phpIniPath();
    QDir().mkpath(QFileInfo(phpIniPath).absolutePath());
    QDir().mkpath(phpIniScanDirectory());

    auto findInstalledExtension = [this](const QString &fileName) {
        const QString extensionRoot = QDir(m_installPath).filePath(QStringLiteral("lib/php/extensions"));
        QDirIterator iterator(extensionRoot, {fileName}, QDir::Files, QDirIterator::Subdirectories);
        if (iterator.hasNext()) {
            return iterator.next();
        }
        return QString();
    };

    auto phpReportsModule = [this](const QStringList &moduleNames) {
        QProcess process;
        process.setProgram(QDir(m_installPath).filePath(QStringLiteral("bin/php")));
        process.setArguments({QStringLiteral("-n"), QStringLiteral("-m")});
        process.setProcessEnvironment(phpBuildEnvironment(m_request, m_installPath));
        process.start();
        if (!process.waitForStarted(5000) || !process.waitForFinished(10000)) {
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return false;
        }

        const QString output = QString::fromLocal8Bit(process.readAllStandardOutput())
            + QString::fromLocal8Bit(process.readAllStandardError());
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            for (const QString &moduleName : moduleNames) {
                if (trimmed.compare(moduleName, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
        }
        return false;
    };

    QStringList lines;
    lines << QStringLiteral("; Generated by LangManager. Manual changes may be overwritten by the next rebuild.")
          << QStringLiteral("memory_limit=512M")
          << QStringLiteral("date.timezone=UTC");

    if (m_request.selectedModules.contains(QStringLiteral("OPcache"))) {
        const QString opcachePath = findInstalledExtension(QStringLiteral("opcache.so"));
        const bool hasBuiltInOpcache = opcachePath.isEmpty()
            && phpReportsModule({QStringLiteral("Zend OPcache"), QStringLiteral("OPcache")});
        if (opcachePath.isEmpty() && !hasBuiltInOpcache) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("OPcache was selected, but opcache.so was not installed.");
            }
            return false;
        }
        lines << QString()
              << QStringLiteral("[opcache]");
        if (opcachePath.isEmpty()) {
            emit logLine(QStringLiteral("OPcache is built into PHP; php.ini will not add zend_extension=opcache.so."));
        } else {
            lines << QStringLiteral("zend_extension=%1").arg(opcachePath);
        }
        lines << QStringLiteral("opcache.enable=1")
              << QStringLiteral("opcache.enable_cli=1")
              << QStringLiteral("opcache.memory_consumption=128")
              << QStringLiteral("opcache.interned_strings_buffer=16")
              << QStringLiteral("opcache.max_accelerated_files=20000")
              << QStringLiteral("opcache.validate_timestamps=1")
              << QStringLiteral("opcache.revalidate_freq=0");
    }

    if (!m_request.peclExtensions.isEmpty()) {
        lines << QString() << QStringLiteral("[langmanager-pecl]");
        for (const QString &extension : m_request.peclExtensions) {
            const QString extensionPath = findInstalledExtension(QStringLiteral("%1.so").arg(extension));
            if (extensionPath.isEmpty()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("PECL extension %1 was selected, but %1.so was not installed.").arg(extension);
                }
                return false;
            }

            if (extension == QStringLiteral("xdebug")) {
                lines << QStringLiteral("zend_extension=%1").arg(extensionPath);
            } else {
                lines << QStringLiteral("extension=%1").arg(extensionPath);
            }
        }
    }

    QSaveFile phpIniFile(phpIniPath);
    if (!phpIniFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write php.ini %1: %2").arg(phpIniPath, phpIniFile.errorString());
        }
        return false;
    }
    phpIniFile.write(lines.join(QStringLiteral("\n")).toUtf8());
    phpIniFile.write("\n");
    if (!phpIniFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot commit php.ini %1: %2").arg(phpIniPath, phpIniFile.errorString());
        }
        return false;
    }

    emit logLine(QStringLiteral("Generated php.ini: %1").arg(phpIniPath));
    return true;
}

bool PhpBuildController::writeInstallManifest(QString *errorMessage) const
{
    QJsonArray selectedModules;
    for (const QString &module : m_request.selectedModules) {
        selectedModules.append(module);
    }

    QJsonArray configureFlags;
    for (const QString &flag : m_request.configureFlags) {
        QString resolvedFlag = flag;
        for (const LocalSourcePackage &package : m_request.localPackages) {
            const QString prefixPath = QDir(m_installPath).filePath(QStringLiteral("deps/%1").arg(package.name));
            resolvedFlag.replace(QStringLiteral("${%1}").arg(package.name), prefixPath);
        }
        configureFlags.append(resolvedFlag);
    }

    QJsonArray peclExtensions;
    for (const QString &extension : m_request.peclExtensions) {
        peclExtensions.append(extension);
    }

    QJsonArray localPackages;
    for (const LocalSourcePackage &package : m_request.localPackages) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), package.name);
        object.insert(QStringLiteral("sourceUrl"), package.sourceUrl.toString());
        object.insert(QStringLiteral("prefix"), QDir(m_installPath).filePath(QStringLiteral("deps/%1").arg(package.name)));
        localPackages.append(object);
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("status"), QStringLiteral("ready"));
    manifest.insert(QStringLiteral("version"), m_request.version);
    manifest.insert(QStringLiteral("installPath"), m_installPath);
    manifest.insert(QStringLiteral("phpBinary"), QDir(m_installPath).filePath(QStringLiteral("bin/php")));
    manifest.insert(QStringLiteral("phpIniPath"), phpIniPath());
    manifest.insert(QStringLiteral("phpIniScanDirectory"), phpIniScanDirectory());
    manifest.insert(QStringLiteral("installedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    manifest.insert(QStringLiteral("selectedModules"), selectedModules);
    manifest.insert(QStringLiteral("configureFlags"), configureFlags);
    manifest.insert(QStringLiteral("peclExtensions"), peclExtensions);
    manifest.insert(QStringLiteral("localPackages"), localPackages);

    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    const QString versionManifestPath = QDir(m_installPath).filePath(QStringLiteral(".langmanager.json"));
    QSaveFile versionManifest(versionManifestPath);
    if (!versionManifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write version manifest %1: %2").arg(versionManifestPath, versionManifest.errorString());
        }
        return false;
    }
    versionManifest.write(manifestBytes);
    if (!versionManifest.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot commit version manifest %1: %2").arg(versionManifestPath, versionManifest.errorString());
        }
        return false;
    }

    const QString registryPath = QDir(m_request.installBasePath).filePath(QStringLiteral("php/installed.json"));
    QDir().mkpath(QFileInfo(registryPath).absolutePath());

    QJsonObject registry;
    QJsonArray versions;
    QFile existingRegistry(registryPath);
    if (existingRegistry.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(existingRegistry.readAll());
        registry = document.object();
        const QJsonArray existingVersions = registry.value(QStringLiteral("versions")).toArray();
        for (const QJsonValue &value : existingVersions) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("installPath")).toString() != m_installPath) {
                versions.append(object);
            }
        }
    }

    versions.append(manifest);
    registry.insert(QStringLiteral("schemaVersion"), 1);
    registry.insert(QStringLiteral("versions"), versions);

    QSaveFile registryFile(registryPath);
    if (!registryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write registry %1: %2").arg(registryPath, registryFile.errorString());
        }
        return false;
    }
    registryFile.write(QJsonDocument(registry).toJson(QJsonDocument::Indented));
    if (!registryFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot commit registry %1: %2").arg(registryPath, registryFile.errorString());
        }
        return false;
    }

    return true;
}

void PhpBuildController::markComplete()
{
    QString phpIniError;
    if (!writePhpIni(&phpIniError)) {
        fail(phpIniError);
        return;
    }

    QString manifestError;
    if (!writeInstallManifest(&manifestError)) {
        fail(manifestError);
        return;
    }

    m_step = Step::Idle;
    emit progressChanged(100);
    emit statusChanged(QStringLiteral("Ready"));
    emit logLine(QStringLiteral("PHP %1 is ready: %2/bin/php").arg(m_request.version, m_installPath));
    for (const QString &line : BuildArtifactCleaner::cleanupInstalledRuntimeArtifacts(m_installPath)) {
        emit logLine(line);
    }
    for (const QString &line : BuildArtifactCleaner::cleanupSuccessfulBuildArtifacts(m_workRoot)) {
        emit logLine(line);
    }
    emit finished(true, QStringLiteral("PHP %1 built successfully.").arg(m_request.version), m_installPath);
}
