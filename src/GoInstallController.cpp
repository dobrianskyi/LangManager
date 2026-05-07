#include "GoInstallController.h"

#include "ArchiveExtractor.h"
#include "GoVersionCatalog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

GoInstallController::GoInstallController(QObject *parent)
    : QObject(parent)
{
}

bool GoInstallController::isRunning() const
{
    return m_step != Step::Idle;
}

void GoInstallController::start(const GoInstallRequest &request)
{
    if (isRunning()) {
        emit logLine(QStringLiteral("Go install is already running."));
        return;
    }

    m_cancelRequested = false;
    m_request = request;
    preparePaths();
    beginDownload();
}

void GoInstallController::cancel()
{
    if (!isRunning()) {
        return;
    }

    m_cancelRequested = true;
    if (m_reply) {
        m_reply->abort();
    }
    fail(QStringLiteral("Cancelled by user."));
}

void GoInstallController::onDownloadReadyRead()
{
    if (m_reply && m_downloadFile.isOpen()) {
        m_downloadFile.write(m_reply->readAll());
    }
}

void GoInstallController::onDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        emit progressChanged(5);
        return;
    }

    const int value = 5 + static_cast<int>((received * 65) / total);
    emit progressChanged(qBound(5, value, 70));
}

void GoInstallController::onDownloadFinished()
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

    emit logLine(QStringLiteral("Download complete: %1").arg(m_downloadPath));
    beginExtraction();
}

void GoInstallController::fail(const QString &message)
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

void GoInstallController::preparePaths()
{
    QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cacheRoot.isEmpty()) {
        cacheRoot = QDir::home().filePath(QStringLiteral(".cache"));
    }
    cacheRoot = QDir(cacheRoot).filePath(QStringLiteral("langmanager"));
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss"));

    m_downloadPath = QDir(cacheRoot).filePath(QStringLiteral("downloads/%1").arg(goArchiveFileName(m_request.version)));
    m_stagingRoot = QDir(m_request.installBasePath).filePath(QStringLiteral("go/.installing-%1-%2").arg(m_request.version, stamp));
    m_extractedGoPath = QDir(m_stagingRoot).filePath(QStringLiteral("go"));
    m_installPath = QDir(m_request.installBasePath).filePath(QStringLiteral("go/%1").arg(m_request.version));

    QDir().mkpath(QFileInfo(m_downloadPath).absolutePath());
    QDir().mkpath(m_stagingRoot);
    QDir().mkpath(QFileInfo(m_installPath).absolutePath());
}

void GoInstallController::beginDownload()
{
    m_step = Step::Downloading;
    emit statusChanged(QStringLiteral("Downloading Go %1").arg(m_request.version));
    emit progressChanged(0);
    emit logLine(QStringLiteral("Archive: %1").arg(m_request.archiveUrl.toString()));
    emit logLine(QStringLiteral("Install prefix: %1").arg(m_installPath));

    m_downloadFile.setFileName(m_downloadPath);
    if (!m_downloadFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("Cannot write archive: %1").arg(m_downloadFile.errorString()));
        return;
    }

    QNetworkRequest request(m_request.archiveUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LangManager/0.2 QtNetwork"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &GoInstallController::onDownloadReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &GoInstallController::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &GoInstallController::onDownloadFinished);
}

void GoInstallController::beginExtraction()
{
    m_step = Step::Extracting;
    emit statusChanged(QStringLiteral("Extracting Go archive"));
    emit progressChanged(75);

    const QString archivePath = m_downloadPath;
    const QString stagingRoot = m_stagingRoot;
    QThread *thread = QThread::create([this, archivePath, stagingRoot]() {
        QString error;
        const bool ok = ArchiveExtractor::extractArchive(archivePath, stagingRoot, &error);
        QMetaObject::invokeMethod(this, [this, ok, error]() {
            if (m_cancelRequested || m_step == Step::Idle) {
                return;
            }
            if (!ok) {
                fail(QStringLiteral("Extraction failed: %1").arg(error));
                return;
            }
            emit logLine(QStringLiteral("Extraction complete: %1").arg(m_extractedGoPath));
            installExtractedToolchain();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void GoInstallController::installExtractedToolchain()
{
    m_step = Step::Installing;
    emit statusChanged(QStringLiteral("Installing Go"));
    emit progressChanged(90);

    if (!QFileInfo::exists(QDir(m_extractedGoPath).filePath(QStringLiteral("bin/go")))) {
        fail(QStringLiteral("Archive did not contain a Go toolchain at %1").arg(m_extractedGoPath));
        return;
    }

    QDir installDirectory(m_installPath);
    if (installDirectory.exists() && !installDirectory.removeRecursively()) {
        fail(QStringLiteral("Cannot replace existing install directory: %1").arg(m_installPath));
        return;
    }

    if (!QDir().rename(m_extractedGoPath, m_installPath)) {
        fail(QStringLiteral("Cannot move extracted Go toolchain to %1").arg(m_installPath));
        return;
    }

    QDir(m_stagingRoot).removeRecursively();
    markComplete();
}

bool GoInstallController::writeInstallManifest(QString *errorMessage) const
{
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schemaVersion"), 1);
    manifest.insert(QStringLiteral("status"), QStringLiteral("ready"));
    manifest.insert(QStringLiteral("version"), m_request.version);
    manifest.insert(QStringLiteral("channel"), goChannelFromVersion(m_request.version));
    manifest.insert(QStringLiteral("installPath"), m_installPath);
    manifest.insert(QStringLiteral("goBinary"), QDir(m_installPath).filePath(QStringLiteral("bin/go")));
    manifest.insert(QStringLiteral("gofmtBinary"), QDir(m_installPath).filePath(QStringLiteral("bin/gofmt")));
    manifest.insert(QStringLiteral("archiveUrl"), m_request.archiveUrl.toString());
    manifest.insert(QStringLiteral("archiveFileName"), goArchiveFileName(m_request.version));
    manifest.insert(QStringLiteral("installedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    const QString versionManifestPath = QDir(m_installPath).filePath(QStringLiteral(".langmanager-go.json"));
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

    const QString registryPath = QDir(m_request.installBasePath).filePath(QStringLiteral("go/installed.json"));
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

void GoInstallController::markComplete()
{
    QString manifestError;
    if (!writeInstallManifest(&manifestError)) {
        fail(manifestError);
        return;
    }

    m_step = Step::Idle;
    emit progressChanged(100);
    emit statusChanged(QStringLiteral("Ready"));
    emit logLine(QStringLiteral("Go %1 is ready: %2/bin/go").arg(m_request.version, m_installPath));
    emit finished(true, QStringLiteral("Go %1 installed successfully.").arg(m_request.version), m_installPath);
}
