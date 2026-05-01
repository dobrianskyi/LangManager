#include "MainWindow.h"

#include "ArchiveExtractor.h"
#include "BuildCatalog.h"
#include "PhpDefaultSwitcher.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QBrush>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStandardPaths>
#include <QSizePolicy>
#include <QSysInfo>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

constexpr int ManifestRole = Qt::UserRole + 1;
constexpr int BuildVersionRole = Qt::UserRole + 2;
constexpr int ChannelRole = Qt::UserRole + 3;
const QColor InstalledGreen(31, 122, 63);

struct PhpChannel {
    QString channel;
    QString buildVersion;
};

QList<PhpChannel> availablePhpChannels()
{
    return {
        {QStringLiteral("8.5"), QStringLiteral("8.5.4")},
        {QStringLiteral("8.4"), QStringLiteral("8.4.20")},
        {QStringLiteral("8.3"), QStringLiteral("8.3.29")},
        {QStringLiteral("8.2"), QStringLiteral("8.2.30")},
        {QStringLiteral("8.1"), QStringLiteral("8.1.34")},
        {QStringLiteral("8.0"), QStringLiteral("8.0.30")},
        {QStringLiteral("7.4"), QStringLiteral("7.4.33")},
    };
}

QString phpChannelFromVersion(const QString &version)
{
    const QStringList parts = version.split('.');
    if (parts.size() < 2) {
        return version;
    }
    return QStringLiteral("%1.%2").arg(parts.at(0), parts.at(1));
}

QString phpSymlinkPath(const QString &installBasePath, const QString &defaultBinPath)
{
    const QString binPath = defaultBinPath.isEmpty()
        ? QDir(installBasePath).filePath(QStringLiteral("bin"))
        : defaultBinPath;
    return QDir(binPath).filePath(QStringLiteral("php"));
}

bool pathContainsDirectory(const QString &directory)
{
    return qEnvironmentVariable("PATH")
        .split(':', Qt::SkipEmptyParts)
        .contains(QDir(directory).absolutePath());
}

QString shellPathExpression(const QString &directory)
{
    const QString homePath = QDir::homePath();
    const QString absoluteDirectory = QDir(directory).absolutePath();
    if (absoluteDirectory == homePath) {
        return QStringLiteral("$HOME");
    }
    if (absoluteDirectory.startsWith(homePath + QLatin1Char('/'))) {
        return QStringLiteral("$HOME/%1").arg(absoluteDirectory.mid(homePath.size() + 1));
    }
    return absoluteDirectory;
}

bool shellConfigAlreadyContainsPath(const QString &content, const QString &directory)
{
    const QString absoluteDirectory = QDir(directory).absolutePath();
    const QString expression = shellPathExpression(absoluteDirectory);
    QString tildeExpression;
    const QString homePath = QDir::homePath();
    if (absoluteDirectory.startsWith(homePath + QLatin1Char('/'))) {
        tildeExpression = QStringLiteral("~/%1").arg(absoluteDirectory.mid(homePath.size() + 1));
    }
    return content.contains(absoluteDirectory)
        || content.contains(expression)
        || (!tildeExpression.isEmpty() && content.contains(tildeExpression));
}

QString preferredShellConfigPath()
{
    const QString shell = qEnvironmentVariable("SHELL");
    if (shell.endsWith(QStringLiteral("zsh"))) {
        return QDir::home().filePath(QStringLiteral(".zshrc"));
    }
    if (shell.endsWith(QStringLiteral("bash"))) {
        return QDir::home().filePath(QStringLiteral(".bashrc"));
    }
    return QDir::home().filePath(QStringLiteral(".profile"));
}

QString defaultSummaryText(const QString &installBasePath, const QString &defaultVersion, const QString &defaultBinPath)
{
    if (defaultVersion.isEmpty()) {
        return QStringLiteral("Default: not selected");
    }

    const QString linkPath = phpSymlinkPath(installBasePath, defaultBinPath);
    const QString binPath = QFileInfo(linkPath).absolutePath();
    const QFileInfo linkInfo(linkPath);
    const QString targetPath = linkInfo.isSymLink() ? linkInfo.symLinkTarget() : QString();

    QString summary;
    if (targetPath.isEmpty()) {
        summary = QStringLiteral("Default: PHP %1 via %2").arg(defaultVersion, linkPath);
    } else {
        summary = QStringLiteral("Default: PHP %1 via %2 -> %3").arg(defaultVersion, linkPath, targetPath);
    }
    if (!pathContainsDirectory(binPath)) {
        summary.append(QStringLiteral("\nPATH missing: add %1 to use `php` in a terminal.").arg(binPath));
    }
    return summary;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();

    connect(&m_controller, &PhpBuildController::statusChanged, m_statusLabel, &QLabel::setText);
    connect(&m_controller, &PhpBuildController::progressChanged, m_progressBar, &QProgressBar::setValue);
    connect(&m_controller, &PhpBuildController::logLine, this, &MainWindow::appendLogLine);
    connect(&m_controller, &PhpBuildController::finished, this, &MainWindow::onBuildFinished);
}

void MainWindow::updateInstallBaseFromTarget(int index)
{
    const QString value = m_installTargetCombo->itemData(index).toString();
    const bool custom = value == QStringLiteral("custom");
    m_installBaseEdit->setEnabled(custom);
    m_browseButton->setEnabled(custom);

    if (value == QStringLiteral("local")) {
        m_installBaseEdit->setText(QDir::home().filePath(QStringLiteral(".local")));
    } else if (value == QStringLiteral("opt")) {
        m_installBaseEdit->setText(QStringLiteral("/opt"));
    }
    refreshInstalledVersions();
    refreshToolStatus();
}

void MainWindow::chooseInstallBase()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Select install base"), m_installBaseEdit->text());
    if (!path.isEmpty()) {
        m_installBaseEdit->setText(path);
        refreshInstalledVersions();
        refreshToolStatus();
    }
}

void MainWindow::onVersionSelectionChanged()
{
    updateSelectedVersionDetails();
}

void MainWindow::installSelectedVersion()
{
    startBuild();
}

void MainWindow::startBuild()
{
    PhpBuildRequest request;
    request = createBuildRequest(selectedVersion(), m_installBaseEdit->text(), selectedModuleLabels());

    QStringList localPackages;
    bool needsCmake = false;
    bool needsPerl = false;
    for (const LocalSourcePackage &package : request.localPackages) {
        localPackages << package.name;
        if (package.buildSystem == QStringLiteral("cmake")) {
            needsCmake = true;
        }
        if (package.name == QStringLiteral("openssl")) {
            needsPerl = true;
        }
    }
    QStringList missingTools;
    auto missingExecutable = [](const QStringList &candidates) {
        for (const QString &candidate : candidates) {
            if (!QStandardPaths::findExecutable(candidate).isEmpty()) {
                return false;
            }
        }
        return true;
    };

    if (QStandardPaths::findExecutable(QStringLiteral("make")).isEmpty()) {
        missingTools << QStringLiteral("make");
    }
    if (missingExecutable({QStringLiteral("cc"), QStringLiteral("gcc"), QStringLiteral("clang")})) {
        missingTools << QStringLiteral("C compiler");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("pkg-config")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("pkgconf")).isEmpty()) {
        missingTools << QStringLiteral("pkg-config");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("autoconf")).isEmpty()) {
        missingTools << QStringLiteral("autoconf");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("automake")).isEmpty()) {
        missingTools << QStringLiteral("automake");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("libtool")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("libtoolize")).isEmpty()) {
        missingTools << QStringLiteral("libtool");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("m4")).isEmpty()) {
        missingTools << QStringLiteral("m4");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("tar")).isEmpty()) {
        missingTools << QStringLiteral("tar");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("gzip")).isEmpty()) {
        missingTools << QStringLiteral("gzip");
    }
    if (QStandardPaths::findExecutable(QStringLiteral("xz")).isEmpty()) {
        missingTools << QStringLiteral("xz");
    }
    if (needsCmake && QStandardPaths::findExecutable(QStringLiteral("cmake")).isEmpty()) {
        missingTools << QStringLiteral("cmake");
    }
    if (needsPerl && QStandardPaths::findExecutable(QStringLiteral("perl")).isEmpty()) {
        missingTools << QStringLiteral("perl (required by OpenSSL)");
    }

    QStringList preflight;
    preflight << QStringLiteral("PHP version: %1").arg(request.version);
    preflight << QStringLiteral("Install base: %1").arg(request.installBasePath);
    preflight << QStringLiteral("Modules: %1").arg(request.selectedModules.join(QStringLiteral(", ")));
    preflight << QStringLiteral("Local dependencies: %1").arg(localPackages.isEmpty() ? QStringLiteral("-") : localPackages.join(QStringLiteral(", ")));
    preflight << QStringLiteral("PECL: %1").arg(request.peclExtensions.isEmpty() ? QStringLiteral("-") : request.peclExtensions.join(QStringLiteral(", ")));
    if (!missingTools.isEmpty()) {
        const QStringList installHints = {
            QStringLiteral("Debian/Ubuntu: sudo apt install build-essential cmake pkg-config autoconf automake libtool m4 perl tar gzip xz-utils"),
            QStringLiteral("Arch Linux:    sudo pacman -S base-devel cmake pkgconf perl tar gzip xz"),
            QStringLiteral("Fedora:        sudo dnf groupinstall \"Development Tools\" && sudo dnf install cmake pkgconf-pkg-config autoconf automake libtool m4 perl tar gzip xz"),
        };
        QMessageBox::warning(
            this,
            QStringLiteral("Build tools missing"),
            QStringLiteral("Cannot start build.\n\nMissing tools:\n%1\n\nInstall examples:\n%2")
                .arg(missingTools.join(QStringLiteral("\n")), installHints.join(QStringLiteral("\n"))));
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Start PHP build"),
        QStringLiteral("Preflight check passed.\n\n%1\n\nStart build?").arg(preflight.join(QStringLiteral("\n"))));
    if (answer != QMessageBox::Yes) {
        return;
    }

    m_logEdit->clear();
    m_readyLabel->setText(QStringLiteral("Building PHP %1").arg(request.version));
    m_readyLabel->setStyleSheet(QStringLiteral("color: #9a6b00; font-weight: 600;"));
    setRunning(true);
    m_controller.start(request);
}

void MainWindow::onBuildFinished(bool success, const QString &message, const QString &installPath)
{
    setRunning(false);
    if (success) {
        m_readyLabel->setText(QStringLiteral("Ready: %1/bin/php").arg(installPath));
        m_readyLabel->setStyleSheet(QStringLiteral("color: #1f7a3f; font-weight: 600;"));
        refreshInstalledVersions();
    } else {
        m_readyLabel->setText(QStringLiteral("Failed"));
        m_readyLabel->setStyleSheet(QStringLiteral("color: #9b1c1c; font-weight: 600;"));
    }
    appendLogLine(message);
}

void MainWindow::setSelectedVersionAsDefault()
{
    const QJsonObject manifest = selectedVersionManifest();
    if (manifest.isEmpty()) {
        return;
    }

    setManifestAsDefault(manifest);
}

void MainWindow::removeSelectedVersion()
{
    const QJsonObject manifest = selectedVersionManifest();
    if (manifest.isEmpty()) {
        return;
    }

    const QString version = manifest.value(QStringLiteral("version")).toString();
    const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Remove PHP %1").arg(version),
        QStringLiteral("Remove %1 and its registry entry?").arg(installPath));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QFile registryFile(installedRegistryPath());
    QJsonObject registry;
    if (registryFile.open(QIODevice::ReadOnly)) {
        registry = QJsonDocument::fromJson(registryFile.readAll()).object();
    }

    QJsonArray versions;
    for (const QJsonValue &value : registry.value(QStringLiteral("versions")).toArray()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("installPath")).toString() != installPath) {
            versions.append(object);
        }
    }
    registry.insert(QStringLiteral("versions"), versions);

    if (registry.value(QStringLiteral("defaultInstallPath")).toString() == installPath) {
        const QStringList binaryNames = {
            QStringLiteral("php"),
            QStringLiteral("phpize"),
            QStringLiteral("php-config"),
            QStringLiteral("pecl"),
            QStringLiteral("pear"),
        };
        const QString binPath = registry.value(QStringLiteral("defaultBinPath")).toString(
            QDir(m_installBaseEdit->text()).filePath(QStringLiteral("bin")));
        for (const QString &binaryName : binaryNames) {
            const QString linkPath = QDir(binPath).filePath(binaryName);
            const QFileInfo linkInfo(linkPath);
            if (linkInfo.isSymLink() && linkInfo.symLinkTarget().startsWith(installPath)) {
                QFile::remove(linkPath);
            }
        }
        registry.remove(QStringLiteral("defaultVersion"));
        registry.remove(QStringLiteral("defaultInstallPath"));
        registry.remove(QStringLiteral("defaultPhpBinary"));
        registry.remove(QStringLiteral("defaultBinPath"));
        registry.remove(QStringLiteral("defaultSetAtUtc"));
    }

    QSaveFile output(installedRegistryPath());
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot update registry"), output.errorString());
        return;
    }
    output.write(QJsonDocument(registry).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        QMessageBox::warning(this, QStringLiteral("Cannot update registry"), output.errorString());
        return;
    }

    QDir installDirectory(installPath);
    if (installDirectory.exists() && !installDirectory.removeRecursively()) {
        QMessageBox::warning(this, QStringLiteral("Cannot remove PHP"), QStringLiteral("Cannot remove %1").arg(installPath));
        return;
    }

    appendLogLine(QStringLiteral("Removed PHP %1: %2").arg(version, installPath));
    refreshInstalledVersions();
}

void MainWindow::fixPathForCurrentDefault()
{
    if (m_currentDefaultBinPath.isEmpty()) {
        return;
    }

    const QString configPath = preferredShellConfigPath();
    QFile existingConfig(configPath);
    QString content;
    if (existingConfig.open(QIODevice::ReadOnly)) {
        content = QString::fromUtf8(existingConfig.readAll());
    }

    const QString pathExpression = shellPathExpression(m_currentDefaultBinPath);
    if (!shellConfigAlreadyContainsPath(content, m_currentDefaultBinPath)) {
        QFile configFile(configPath);
        if (!configFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QMessageBox::warning(
                this,
                QStringLiteral("Cannot update PATH"),
                QStringLiteral("Cannot write %1: %2").arg(configPath, configFile.errorString()));
            return;
        }

        QTextStream stream(&configFile);
        if (!content.endsWith(QLatin1Char('\n')) && !content.isEmpty()) {
            stream << '\n';
        }
        stream << '\n'
               << "# PHPManager PATH\n"
               << "case \":$PATH:\" in\n"
               << "    *\":" << pathExpression << ":\"*) ;;\n"
               << "    *) export PATH=\"" << pathExpression << ":$PATH\" ;;\n"
               << "esac\n";
    }

    const QString currentPath = qEnvironmentVariable("PATH");
    if (!pathContainsDirectory(m_currentDefaultBinPath)) {
        qputenv("PATH", QStringLiteral("%1:%2")
            .arg(QDir(m_currentDefaultBinPath).absolutePath(), currentPath)
            .toUtf8());
    }

    appendLogLine(QStringLiteral("Added PHPManager bin directory to PATH config: %1").arg(configPath));
    QMessageBox::information(
        this,
        QStringLiteral("PATH updated"),
        QStringLiteral("Added %1 to %2. Open a new terminal, then run php -v.")
            .arg(m_currentDefaultBinPath, configPath));
    refreshInstalledVersions();
}

void MainWindow::applyBuildProfile(int index)
{
    if (index < 0 || !m_buildProfileCombo) {
        return;
    }

    const QString profileKey = m_buildProfileCombo->itemData(index).toString();
    const QStringList labels = moduleLabelsForProfile(profileKey);
    const QSet<QString> selected = QSet<QString>(labels.begin(), labels.end());
    for (ModuleOption &module : m_modules) {
        if (module.checkBox) {
            module.checkBox->setChecked(selected.contains(module.label));
        }
    }
}

void MainWindow::installComposerCli()
{
    const QString phpBinary = defaultPhpBinary();
    if (phpBinary.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Composer needs PHP"), QStringLiteral("Set an installed PHP version as default first."));
        return;
    }

    const QString composerPath = QDir(m_installBaseEdit->text()).filePath(QStringLiteral("tools/composer/composer.phar"));
    QString error;
    if (!downloadToFile(QUrl(QStringLiteral("https://getcomposer.org/download/latest-stable/composer.phar")), composerPath, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Composer"), error);
        return;
    }

    QDir().mkpath(installBaseBinPath());
    const QString wrapperPath = QDir(installBaseBinPath()).filePath(QStringLiteral("composer"));
    QSaveFile wrapper(wrapperPath);
    if (!wrapper.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Composer"), wrapper.errorString());
        return;
    }
    wrapper.write(QStringLiteral("#!/usr/bin/env sh\nexec \"%1\" \"%2\" \"$@\"\n")
        .arg(QDir(installBaseBinPath()).filePath(QStringLiteral("php")), composerPath)
        .toUtf8());
    if (!wrapper.commit()) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Composer"), wrapper.errorString());
        return;
    }
    QFile::setPermissions(wrapperPath, QFile::permissions(wrapperPath)
        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    appendLogLine(QStringLiteral("Installed Composer locally: %1").arg(wrapperPath));
    refreshToolStatus();
}

void MainWindow::installSymfonyCli()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), QStringLiteral("Cannot create temporary directory."));
        return;
    }

    const QString archivePath = QDir(tempDir.path()).filePath(QStringLiteral("symfony-cli.tar.gz"));
    QString error;
    if (!downloadToFile(QUrl(symfonyCliDownloadUrl()), archivePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), error);
        return;
    }

    const QString extractPath = QDir(tempDir.path()).filePath(QStringLiteral("extract"));
    if (!ArchiveExtractor::extractArchive(archivePath, extractPath, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), error);
        return;
    }

    const QString extractedBinary = QDir(extractPath).filePath(QStringLiteral("symfony"));
    if (!QFileInfo::exists(extractedBinary)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), QStringLiteral("Symfony archive did not contain the expected binary."));
        return;
    }

    const QString toolsPath = QDir(m_installBaseEdit->text()).filePath(QStringLiteral("tools/symfony/symfony"));
    QDir().mkpath(QFileInfo(toolsPath).absolutePath());
    QFile::remove(toolsPath);
    if (!QFile::copy(extractedBinary, toolsPath)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), QStringLiteral("Cannot copy Symfony binary to %1").arg(toolsPath));
        return;
    }
    QFile::setPermissions(toolsPath, QFile::permissions(toolsPath)
        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    QDir().mkpath(installBaseBinPath());
    const QString linkPath = QDir(installBaseBinPath()).filePath(QStringLiteral("symfony"));
    const QFileInfo linkInfo(linkPath);
    if ((linkInfo.exists() || linkInfo.isSymLink()) && !linkInfo.isSymLink()) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), QStringLiteral("Refusing to overwrite non-symlink file: %1").arg(linkPath));
        return;
    }
    if (linkInfo.isSymLink()) {
        QFile::remove(linkPath);
    }
    if (!QFile::link(toolsPath, linkPath)) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), QStringLiteral("Cannot create symlink %1").arg(linkPath));
        return;
    }

    appendLogLine(QStringLiteral("Installed Symfony CLI locally: %1").arg(linkPath));
    refreshToolStatus();
}

void MainWindow::refreshToolStatus()
{
    const QString composerPath = QDir(installBaseBinPath()).filePath(QStringLiteral("composer"));
    const QString symfonyPath = QDir(installBaseBinPath()).filePath(QStringLiteral("symfony"));

    if (m_composerStatusLabel) {
        if (QFileInfo::exists(composerPath)) {
            m_composerStatusLabel->setText(runToolCommand(composerPath, {QStringLiteral("--version")}));
        } else {
            m_composerStatusLabel->setText(QStringLiteral("Composer is not installed in this install base."));
        }
    }
    if (m_symfonyStatusLabel) {
        if (QFileInfo::exists(symfonyPath)) {
            m_symfonyStatusLabel->setText(runToolCommand(symfonyPath, {QStringLiteral("--version")}));
        } else {
            m_symfonyStatusLabel->setText(QStringLiteral("Symfony CLI is not installed in this install base."));
        }
    }
}

bool MainWindow::setManifestAsDefault(const QJsonObject &manifest)
{
    QString error;
    if (!PhpDefaultSwitcher::setDefault(m_installBaseEdit->text(), manifest, &error)) {
        if (m_currentPhpLabel) {
            m_currentPhpLabel->setText(QStringLiteral("Default switch failed"));
        }
        QMessageBox::warning(this, QStringLiteral("Cannot set default PHP"), error);
        return false;
    }

    const QString version = manifest.value(QStringLiteral("version")).toString();
    const QString binPath = QDir(m_installBaseEdit->text()).filePath(QStringLiteral("bin"));
    const QString summary = defaultSummaryText(m_installBaseEdit->text(), version, binPath);
    if (m_currentPhpLabel) {
        m_currentPhpLabel->setText(summary);
    }
    refreshInstalledVersions();
    return true;
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("PHPManager"));
    resize(1180, 680);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(0);

    central->setStyleSheet(QStringLiteral(
        "QGroupBox { font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
        "QLineEdit, QComboBox, QTextEdit { font-weight: 400; }"
        "QPushButton { min-height: 28px; padding: 2px 12px; }"
        "QProgressBar { min-height: 16px; }"
    ));

    auto *tabs = new QTabWidget(central);
    tabs->setDocumentMode(true);
    rootLayout->addWidget(tabs, 1);

    auto *versionsPage = new QWidget(tabs);
    auto *versionsLayout = new QHBoxLayout(versionsPage);
    versionsLayout->setContentsMargins(10, 10, 10, 10);
    versionsLayout->setSpacing(10);

    auto *leftColumn = new QWidget(versionsPage);
    leftColumn->setMaximumWidth(430);
    leftColumn->setMinimumWidth(360);
    auto *leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    auto *formBox = new QGroupBox(QStringLiteral("Install location"), leftColumn);
    auto *formLayout = new QFormLayout(formBox);
    formLayout->setContentsMargins(8, 12, 8, 8);
    formLayout->setHorizontalSpacing(8);
    formLayout->setVerticalSpacing(6);

    m_installTargetCombo = new QComboBox(formBox);
    m_installTargetCombo->addItem(QStringLiteral("User (~/.local)"), QStringLiteral("local"));
    m_installTargetCombo->addItem(QStringLiteral("System (/opt)"), QStringLiteral("opt"));
    m_installTargetCombo->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    formLayout->addRow(QStringLiteral("Install target"), m_installTargetCombo);

    auto *pathRow = new QWidget(formBox);
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_installBaseEdit = new QLineEdit(pathRow);
    m_installBaseEdit->setText(QDir::home().filePath(QStringLiteral(".local")));
    m_browseButton = new QPushButton(QStringLiteral("Browse"), pathRow);
    pathLayout->addWidget(m_installBaseEdit, 1);
    pathLayout->addWidget(m_browseButton);
    formLayout->addRow(QStringLiteral("Install base"), pathRow);

    auto *versionsBox = new QGroupBox(QStringLiteral("PHP versions"), leftColumn);
    auto *versionsBoxLayout = new QVBoxLayout(versionsBox);
    versionsBoxLayout->setContentsMargins(8, 12, 8, 8);
    versionsBoxLayout->setSpacing(6);
    m_currentPhpLabel = new QLabel(QStringLiteral("Default: not selected"), versionsBox);
    m_currentPhpLabel->setWordWrap(true);
    m_fixPathButton = new QPushButton(QStringLiteral("Fix PATH"), versionsBox);
    m_fixPathButton->setVisible(false);
    m_versionsList = new QListWidget(versionsBox);
    m_versionsList->setUniformItemSizes(true);
    m_versionsList->setSelectionMode(QAbstractItemView::SingleSelection);
    versionsBoxLayout->addWidget(m_currentPhpLabel);
    versionsBoxLayout->addWidget(m_fixPathButton);
    versionsBoxLayout->addWidget(m_versionsList, 1);

    auto *buttonRow = new QHBoxLayout();
    m_installVersionButton = new QPushButton(QStringLiteral("Install"), leftColumn);
    m_setVersionDefaultButton = new QPushButton(QStringLiteral("Set default"), leftColumn);
    m_removeVersionButton = new QPushButton(QStringLiteral("Remove"), leftColumn);
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), leftColumn);
    m_cancelButton->setEnabled(false);
    buttonRow->addWidget(m_installVersionButton, 1);
    buttonRow->addWidget(m_setVersionDefaultButton);
    buttonRow->addWidget(m_removeVersionButton);
    buttonRow->addWidget(m_cancelButton);

    auto *stateBox = new QGroupBox(QStringLiteral("Current action"), leftColumn);
    auto *stateLayout = new QVBoxLayout(stateBox);
    stateLayout->setContentsMargins(8, 12, 8, 8);
    stateLayout->setSpacing(6);
    m_statusLabel = new QLabel(QStringLiteral("Idle"), stateBox);
    m_readyLabel = new QLabel(QStringLiteral("No build running"), stateBox);
    m_readyLabel->setWordWrap(true);
    m_progressBar = new QProgressBar(stateBox);
    m_progressBar->setRange(0, 100);
    stateLayout->addWidget(m_statusLabel);
    stateLayout->addWidget(m_readyLabel);
    stateLayout->addWidget(m_progressBar);

    leftLayout->addWidget(formBox);
    leftLayout->addWidget(versionsBox, 1);
    leftLayout->addLayout(buttonRow);
    leftLayout->addWidget(stateBox);

    auto *rightColumn = new QWidget(versionsPage);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *detailsBox = new QGroupBox(QStringLiteral("Selected version"), rightColumn);
    auto *detailsLayout = new QVBoxLayout(detailsBox);
    detailsLayout->setContentsMargins(8, 12, 8, 8);
    detailsLayout->setSpacing(6);
    m_selectedVersionTitleLabel = new QLabel(QStringLiteral("PHP"), detailsBox);
    m_selectedVersionTitleLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    m_selectedVersionStatusLabel = new QLabel(detailsBox);
    m_selectedVersionStatusLabel->setWordWrap(true);
    m_selectedVersionDetailsEdit = new QTextEdit(detailsBox);
    m_selectedVersionDetailsEdit->setReadOnly(true);
    m_selectedVersionDetailsEdit->setMinimumHeight(160);
    detailsLayout->addWidget(m_selectedVersionTitleLabel);
    detailsLayout->addWidget(m_selectedVersionStatusLabel);
    detailsLayout->addWidget(m_selectedVersionDetailsEdit);

    auto *outputBox = new QGroupBox(QStringLiteral("Build output"), rightColumn);
    auto *outputLayout = new QVBoxLayout(outputBox);
    outputLayout->setContentsMargins(8, 12, 8, 8);
    outputLayout->setSpacing(4);
    m_logEdit = new QTextEdit(outputBox);
    m_logEdit->setReadOnly(true);
    m_logEdit->setLineWrapMode(QTextEdit::NoWrap);
    outputLayout->addWidget(m_logEdit, 1);

    rightLayout->addWidget(detailsBox);
    rightLayout->addWidget(outputBox, 1);

    versionsLayout->addWidget(leftColumn);
    versionsLayout->addWidget(rightColumn, 1);

    auto *optionsPage = new QWidget(tabs);
    auto *optionsLayout = new QVBoxLayout(optionsPage);
    optionsLayout->setContentsMargins(10, 10, 10, 10);
    optionsLayout->setSpacing(8);

    auto *profileBox = new QGroupBox(QStringLiteral("Build profile"), optionsPage);
    auto *profileLayout = new QFormLayout(profileBox);
    profileLayout->setContentsMargins(8, 12, 8, 8);
    m_buildProfileCombo = new QComboBox(profileBox);
    for (const BuildProfileDefinition &profile : allBuildProfiles()) {
        m_buildProfileCombo->addItem(profile.label, profile.key);
    }
    const int defaultProfileIndex = m_buildProfileCombo->findData(QStringLiteral("symfony_full"));
    if (defaultProfileIndex >= 0) {
        m_buildProfileCombo->setCurrentIndex(defaultProfileIndex);
    }
    profileLayout->addRow(QStringLiteral("Preset"), m_buildProfileCombo);
    optionsLayout->addWidget(profileBox);

    auto *moduleBox = new QGroupBox(QStringLiteral("Modules for next install or rebuild"), optionsPage);
    auto *moduleBoxLayout = new QVBoxLayout(moduleBox);
    moduleBoxLayout->setContentsMargins(8, 12, 8, 8);
    moduleBoxLayout->setSpacing(0);

    for (const ModuleDefinition &module : allModuleDefinitions()) {
        m_modules << ModuleOption{
            module.label,
            module.flag,
            module.peclPackage,
            module.localPackageName,
            module.defaultChecked,
        };
    }

    auto *moduleGrid = new QWidget(moduleBox);
    auto *moduleGridLayout = new QGridLayout(moduleGrid);
    moduleGridLayout->setContentsMargins(4, 4, 4, 4);
    moduleGridLayout->setHorizontalSpacing(18);
    moduleGridLayout->setVerticalSpacing(0);

    constexpr int maxRowsPerColumn = 9;
    for (int i = 0; i < m_modules.size(); ++i) {
        auto *checkBox = new QCheckBox(m_modules[i].label, moduleGrid);
        checkBox->setChecked(m_modules[i].defaultChecked);
        checkBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        checkBox->setStyleSheet(QStringLiteral("QCheckBox { margin: 0; padding: 0; min-height: 22px; }"));
        m_modules[i].checkBox = checkBox;
        moduleGridLayout->addWidget(checkBox, i % maxRowsPerColumn, i / maxRowsPerColumn);
    }

    auto *moduleScrollArea = new QScrollArea(moduleBox);
    moduleScrollArea->setWidget(moduleGrid);
    moduleScrollArea->setWidgetResizable(false);
    moduleScrollArea->setFrameShape(QFrame::NoFrame);
    moduleScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    moduleScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    moduleBoxLayout->addWidget(moduleScrollArea, 1);
    optionsLayout->addWidget(moduleBox, 1);

    auto *composerPage = new QWidget(tabs);
    auto *composerLayout = new QVBoxLayout(composerPage);
    composerLayout->setContentsMargins(10, 10, 10, 10);
    composerLayout->setSpacing(8);

    auto *composerBox = new QGroupBox(QStringLiteral("Composer CLI"), composerPage);
    auto *composerBoxLayout = new QVBoxLayout(composerBox);
    composerBoxLayout->setContentsMargins(8, 12, 8, 8);
    composerBoxLayout->setSpacing(6);
    m_composerStatusLabel = new QLabel(QStringLiteral("Composer is not installed in this install base."), composerBox);
    m_composerStatusLabel->setWordWrap(true);
    m_installComposerButton = new QPushButton(QStringLiteral("Install or Update Composer"), composerBox);
    composerBoxLayout->addWidget(m_composerStatusLabel);
    composerBoxLayout->addWidget(m_installComposerButton);

    auto *symfonyBox = new QGroupBox(QStringLiteral("Symfony CLI"), composerPage);
    auto *symfonyBoxLayout = new QVBoxLayout(symfonyBox);
    symfonyBoxLayout->setContentsMargins(8, 12, 8, 8);
    symfonyBoxLayout->setSpacing(6);
    m_symfonyStatusLabel = new QLabel(QStringLiteral("Symfony CLI is not installed in this install base."), symfonyBox);
    m_symfonyStatusLabel->setWordWrap(true);
    m_installSymfonyButton = new QPushButton(QStringLiteral("Install or Update Symfony CLI"), symfonyBox);
    symfonyBoxLayout->addWidget(m_symfonyStatusLabel);
    symfonyBoxLayout->addWidget(m_installSymfonyButton);

    auto *toolsInfo = new QLabel(
        QStringLiteral("Tools are installed under the selected install base and exposed through its bin directory. Composer runs through the managed PHP command from the same install base."),
        composerPage);
    toolsInfo->setWordWrap(true);
    composerLayout->addWidget(composerBox);
    composerLayout->addWidget(symfonyBox);
    composerLayout->addWidget(toolsInfo);
    composerLayout->addStretch(1);

    tabs->addTab(versionsPage, QStringLiteral("Versions"));
    tabs->addTab(optionsPage, QStringLiteral("Build options"));
    tabs->addTab(composerPage, QStringLiteral("Composer"));

    setCentralWidget(central);

    connect(m_installTargetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateInstallBaseFromTarget);
    connect(m_installBaseEdit, &QLineEdit::editingFinished, this, &MainWindow::refreshInstalledVersions);
    connect(m_versionsList, &QListWidget::currentRowChanged, this, &MainWindow::onVersionSelectionChanged);
    connect(m_installVersionButton, &QPushButton::clicked, this, &MainWindow::installSelectedVersion);
    connect(m_setVersionDefaultButton, &QPushButton::clicked, this, &MainWindow::setSelectedVersionAsDefault);
    connect(m_removeVersionButton, &QPushButton::clicked, this, &MainWindow::removeSelectedVersion);
    connect(m_fixPathButton, &QPushButton::clicked, this, &MainWindow::fixPathForCurrentDefault);
    connect(m_buildProfileCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::applyBuildProfile);
    connect(m_installComposerButton, &QPushButton::clicked, this, &MainWindow::installComposerCli);
    connect(m_installSymfonyButton, &QPushButton::clicked, this, &MainWindow::installSymfonyCli);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::chooseInstallBase);
    connect(m_cancelButton, &QPushButton::clicked, &m_controller, &PhpBuildController::cancel);
    applyBuildProfile(m_buildProfileCombo->currentIndex());
    updateInstallBaseFromTarget(m_installTargetCombo->currentIndex());
}

void MainWindow::appendLogLine(const QString &line)
{
    m_logEdit->append(line.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
    m_logEdit->verticalScrollBar()->setValue(m_logEdit->verticalScrollBar()->maximum());
}

QString MainWindow::selectedVersion() const
{
    if (!m_versionsList || !m_versionsList->currentItem()) {
        const QList<PhpChannel> channels = availablePhpChannels();
        return channels.isEmpty() ? QString() : channels.first().buildVersion;
    }
    return m_versionsList->currentItem()->data(BuildVersionRole).toString();
}

QStringList MainWindow::selectedModuleLabels() const
{
    QStringList labels;
    for (const ModuleOption &module : m_modules) {
        if (module.checkBox && module.checkBox->isChecked()) {
            labels << module.label;
        }
    }
    return labels;
}

QStringList MainWindow::selectedConfigureFlags() const
{
    QStringList flags;
    for (const ModuleOption &module : m_modules) {
        if (module.checkBox && module.checkBox->isChecked() && !module.flag.isEmpty()) {
            flags << module.flag;
            if (module.localPackageName == QStringLiteral("gd")) {
                flags << QStringLiteral("--with-jpeg");
            }
        }
    }
    return flags;
}

QStringList MainWindow::selectedPeclExtensions() const
{
    QStringList extensions;
    for (const ModuleOption &module : m_modules) {
        if (module.checkBox && module.checkBox->isChecked() && !module.peclPackage.isEmpty()) {
            extensions << module.peclPackage;
        }
    }
    return extensions;
}

QList<LocalSourcePackage> MainWindow::selectedLocalPackages() const
{
    bool needsUnixOdbc = false;
    bool needsPostgreSql = false;
    bool needsGd = false;
    for (const ModuleOption &module : m_modules) {
        if (module.checkBox && module.checkBox->isChecked() && module.localPackageName == QStringLiteral("unixODBC")) {
            needsUnixOdbc = true;
        } else if (module.checkBox && module.checkBox->isChecked() && module.localPackageName == QStringLiteral("postgresql")) {
            needsPostgreSql = true;
        } else if (module.checkBox && module.checkBox->isChecked() && module.localPackageName == QStringLiteral("gd")) {
            needsGd = true;
        }
    }

    QList<LocalSourcePackage> packages;
    if (needsGd) {
        packages << LocalSourcePackage{
            QStringLiteral("zlib"),
            QUrl(QStringLiteral("https://zlib.net/fossils/zlib-1.3.2.tar.gz")),
            QStringLiteral("zlib-1.3.2"),
            {},
        };
        packages << LocalSourcePackage{
            QStringLiteral("jpeg"),
            QUrl(QStringLiteral("https://www.ijg.org/files/jpegsrc.v10.tar.gz")),
            QStringLiteral("jpeg-10"),
            {},
        };
        packages << LocalSourcePackage{
            QStringLiteral("libpng"),
            QUrl(QStringLiteral("https://download.sourceforge.net/libpng/libpng-1.6.57.tar.gz")),
            QStringLiteral("libpng-1.6.57"),
            {},
        };
    }

    if (needsPostgreSql) {
        packages << LocalSourcePackage{
            QStringLiteral("postgresql"),
            QUrl(QStringLiteral("https://ftp.postgresql.org/pub/source/v17.9/postgresql-17.9.tar.gz")),
            QStringLiteral("postgresql-17.9"),
            {
                QStringLiteral("--without-readline"),
                QStringLiteral("--without-zlib"),
            },
        };
    }

    if (needsUnixOdbc) {
        packages << LocalSourcePackage{
            QStringLiteral("unixODBC"),
            QUrl(QStringLiteral("https://www.unixodbc.org/unixODBC-2.3.12.tar.gz")),
            QStringLiteral("unixODBC-2.3.12"),
            {
                QStringLiteral("--disable-gui"),
                QStringLiteral("--enable-static"),
                QStringLiteral("--enable-shared"),
            },
        };
    }

    return packages;
}

QString MainWindow::installedRegistryPath() const
{
    return QDir(m_installBaseEdit->text()).filePath(QStringLiteral("php/installed.json"));
}

QJsonObject MainWindow::selectedVersionManifest() const
{
    if (!m_versionsList || !m_versionsList->currentItem()) {
        return {};
    }

    const QString payload = m_versionsList->currentItem()->data(ManifestRole).toString();
    if (payload.isEmpty()) {
        return {};
    }

    return QJsonDocument::fromJson(payload.toUtf8()).object();
}

QString MainWindow::runPhpCommand(const QString &phpBinary, const QStringList &arguments) const
{
    return runToolCommand(phpBinary, arguments);
}

QString MainWindow::runToolCommand(const QString &program, const QStringList &arguments) const
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(3000)) {
        return QStringLiteral("Cannot start %1: %2").arg(program, process.errorString());
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return QStringLiteral("Command timed out: %1 %2").arg(program, arguments.join(' '));
    }

    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
    if (!errorOutput.trimmed().isEmpty()) {
        if (!output.endsWith(QLatin1Char('\n')) && !output.isEmpty()) {
            output.append(QLatin1Char('\n'));
        }
        output.append(errorOutput);
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (!output.endsWith(QLatin1Char('\n')) && !output.isEmpty()) {
            output.append(QLatin1Char('\n'));
        }
        output.append(QStringLiteral("[exit code %1]").arg(process.exitCode()));
    }
    return output.trimmed();
}

bool MainWindow::downloadToFile(const QUrl &url, const QString &path, QString *errorMessage)
{
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PHPManager/0.1 QtNetwork"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = reply->errorString();
        }
        reply->deleteLater();
        return false;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = output.errorString();
        }
        reply->deleteLater();
        return false;
    }
    output.write(reply->readAll());
    reply->deleteLater();
    if (!output.commit()) {
        if (errorMessage) {
            *errorMessage = output.errorString();
        }
        return false;
    }
    return true;
}

QString MainWindow::installBaseBinPath() const
{
    return QDir(m_installBaseEdit->text()).filePath(QStringLiteral("bin"));
}

QString MainWindow::defaultPhpBinary() const
{
    const QString managedPhp = QDir(installBaseBinPath()).filePath(QStringLiteral("php"));
    if (QFileInfo::exists(managedPhp)) {
        return managedPhp;
    }

    QFile registryFile(installedRegistryPath());
    if (registryFile.open(QIODevice::ReadOnly)) {
        const QJsonObject registry = QJsonDocument::fromJson(registryFile.readAll()).object();
        const QString phpBinary = registry.value(QStringLiteral("defaultPhpBinary")).toString();
        if (QFileInfo::exists(phpBinary)) {
            return phpBinary;
        }
    }
    return {};
}

QString MainWindow::symfonyCliDownloadUrl() const
{
    const QString architecture = QSysInfo::currentCpuArchitecture();
    QString assetArchitecture = QStringLiteral("amd64");
    if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64")) {
        assetArchitecture = QStringLiteral("arm64");
    } else if (architecture == QStringLiteral("i386") || architecture == QStringLiteral("i686")) {
        assetArchitecture = QStringLiteral("386");
    }
    return QStringLiteral("https://github.com/symfony-cli/symfony-cli/releases/latest/download/symfony-cli_linux_%1.tar.gz")
        .arg(assetArchitecture);
}

QString MainWindow::installedExtensionPath(const QString &installPath, const QString &fileName) const
{
    const QString extensionRoot = QDir(installPath).filePath(QStringLiteral("lib/php/extensions"));
    QDirIterator iterator(extensionRoot, {fileName}, QDir::Files, QDirIterator::Subdirectories);
    if (iterator.hasNext()) {
        return iterator.next();
    }
    return {};
}

bool MainWindow::ensurePhpIniForManifest(const QJsonObject &manifest)
{
    const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
    if (installPath.isEmpty()) {
        return false;
    }

    const QString phpIniPath = manifest.value(QStringLiteral("phpIniPath")).toString(
        QDir(installPath).filePath(QStringLiteral("lib/php.ini")));
    if (QFileInfo::exists(phpIniPath)) {
        return true;
    }

    QStringList lines;
    lines << QStringLiteral("; Generated by PHPManager.")
          << QStringLiteral("memory_limit=512M")
          << QStringLiteral("date.timezone=UTC");

    const QJsonArray modules = manifest.value(QStringLiteral("selectedModules")).toArray();
    bool needsOpcache = false;
    for (const QJsonValue &value : modules) {
        if (value.toString() == QStringLiteral("OPcache")) {
            needsOpcache = true;
            break;
        }
    }

    if (needsOpcache) {
        const QString opcachePath = installedExtensionPath(installPath, QStringLiteral("opcache.so"));
        if (opcachePath.isEmpty()) {
            appendLogLine(QStringLiteral("Cannot repair php.ini: opcache.so is missing for %1").arg(installPath));
            return false;
        }
        lines << QString()
              << QStringLiteral("[opcache]")
              << QStringLiteral("zend_extension=%1").arg(opcachePath)
              << QStringLiteral("opcache.enable=1")
              << QStringLiteral("opcache.enable_cli=1")
              << QStringLiteral("opcache.memory_consumption=128")
              << QStringLiteral("opcache.interned_strings_buffer=16")
              << QStringLiteral("opcache.max_accelerated_files=20000")
              << QStringLiteral("opcache.validate_timestamps=1")
              << QStringLiteral("opcache.revalidate_freq=0");
    }

    const QJsonArray peclExtensions = manifest.value(QStringLiteral("peclExtensions")).toArray();
    if (!peclExtensions.isEmpty()) {
        lines << QString() << QStringLiteral("[phpmanager-pecl]");
        for (const QJsonValue &value : peclExtensions) {
            const QString extension = value.toString();
            const QString extensionPath = installedExtensionPath(installPath, QStringLiteral("%1.so").arg(extension));
            if (extensionPath.isEmpty()) {
                appendLogLine(QStringLiteral("Cannot repair php.ini: %1.so is missing for %2").arg(extension, installPath));
                return false;
            }
            if (extension == QStringLiteral("xdebug")) {
                lines << QStringLiteral("zend_extension=%1").arg(extensionPath);
            } else {
                lines << QStringLiteral("extension=%1").arg(extensionPath);
            }
        }
    }

    QDir().mkpath(QFileInfo(phpIniPath).absolutePath());
    QSaveFile phpIniFile(phpIniPath);
    if (!phpIniFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLogLine(QStringLiteral("Cannot repair php.ini %1: %2").arg(phpIniPath, phpIniFile.errorString()));
        return false;
    }
    phpIniFile.write(lines.join(QStringLiteral("\n")).toUtf8());
    phpIniFile.write("\n");
    if (!phpIniFile.commit()) {
        appendLogLine(QStringLiteral("Cannot commit repaired php.ini %1: %2").arg(phpIniPath, phpIniFile.errorString()));
        return false;
    }

    appendLogLine(QStringLiteral("Repaired missing php.ini: %1").arg(phpIniPath));
    return true;
}

void MainWindow::refreshInstalledVersions()
{
    if (!m_versionsList) {
        return;
    }

    const QString previouslySelectedChannel = m_versionsList->currentItem()
        ? m_versionsList->currentItem()->data(ChannelRole).toString()
        : QString();
    m_versionsList->blockSignals(true);
    m_versionsList->clear();

    QJsonObject registry;
    QFile registryFile(installedRegistryPath());
    if (registryFile.open(QIODevice::ReadOnly)) {
        registry = QJsonDocument::fromJson(registryFile.readAll()).object();
    }

    const QString defaultInstallPath = registry.value(QStringLiteral("defaultInstallPath")).toString();
    QString defaultVersion = registry.value(QStringLiteral("defaultVersion")).toString();
    QString defaultBinPath = registry.value(QStringLiteral("defaultBinPath")).toString();
    if (defaultBinPath.isEmpty()) {
        defaultBinPath = QDir(m_installBaseEdit->text()).filePath(QStringLiteral("bin"));
    }
    const QString defaultLinkTarget = QFileInfo(phpSymlinkPath(m_installBaseEdit->text(), defaultBinPath)).symLinkTarget();
    const bool hasDefaultSymlink = !defaultLinkTarget.isEmpty();
    QHash<QString, QJsonObject> manifestsByChannel;
    for (const QJsonValue &value : registry.value(QStringLiteral("versions")).toArray()) {
        QJsonObject manifest = value.toObject();
        if (manifest.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) {
            continue;
        }
        ensurePhpIniForManifest(manifest);
        const QString version = manifest.value(QStringLiteral("version")).toString();
        const QString channel = phpChannelFromVersion(version);
        manifestsByChannel.insert(channel, manifest);
    }

    int selectedRow = 0;
    const QList<PhpChannel> channels = availablePhpChannels();
    for (int i = 0; i < channels.size(); ++i) {
        const PhpChannel channel = channels.at(i);
        QJsonObject manifest = manifestsByChannel.value(channel.channel);
        const bool installed = !manifest.isEmpty();
        const QString version = installed ? manifest.value(QStringLiteral("version")).toString() : channel.buildVersion;
        const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
        const QString phpBinary = manifest.value(QStringLiteral("phpBinary")).toString();
        const bool registryDefault = !defaultInstallPath.isEmpty() && installPath == defaultInstallPath;
        const bool symlinkDefault = !defaultLinkTarget.isEmpty() && QFileInfo(defaultLinkTarget).absoluteFilePath() == QFileInfo(phpBinary).absoluteFilePath();
        const bool isDefault = installed && (hasDefaultSymlink ? symlinkDefault : registryDefault);
        if (isDefault) {
            defaultVersion = version;
        }

        manifest.insert(QStringLiteral("isDefault"), isDefault);
        QString label;
        if (installed) {
            label = QStringLiteral("PHP %1  (%2 installed)").arg(channel.channel, version);
        } else {
            label = QStringLiteral("PHP %1  (not installed)").arg(channel.channel);
        }
        if (isDefault) {
            label.append(QStringLiteral("  default"));
        }
        const QString payload = QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact));

        auto *item = new QListWidgetItem(label);
        item->setData(ChannelRole, channel.channel);
        item->setData(BuildVersionRole, channel.buildVersion);
        item->setData(ManifestRole, installed ? payload : QString());
        if (installed) {
            item->setForeground(QBrush(InstalledGreen));
        }
        QFont font = item->font();
        font.setBold(isDefault);
        item->setFont(font);
        m_versionsList->addItem(item);

        if (!previouslySelectedChannel.isEmpty() && previouslySelectedChannel == channel.channel) {
            selectedRow = i;
        } else if (previouslySelectedChannel.isEmpty() && isDefault) {
            selectedRow = i;
        }
    }

    const QString summary = defaultSummaryText(m_installBaseEdit->text(), defaultVersion, defaultBinPath);
    if (m_currentPhpLabel) {
        m_currentPhpLabel->setText(summary);
    }
    m_currentDefaultBinPath = defaultVersion.isEmpty() ? QString() : QDir(defaultBinPath).absolutePath();
    if (m_fixPathButton) {
        const bool showFixPath = !m_currentDefaultBinPath.isEmpty() && !pathContainsDirectory(m_currentDefaultBinPath);
        m_fixPathButton->setVisible(showFixPath);
        m_fixPathButton->setEnabled(showFixPath && !m_controller.isRunning());
        m_fixPathButton->setText(QStringLiteral("Fix PATH (%1)").arg(m_currentDefaultBinPath));
    }

    if (m_versionsList->count() > 0) {
        m_versionsList->setCurrentRow(qBound(0, selectedRow, m_versionsList->count() - 1));
    }
    m_versionsList->blockSignals(false);
    updateSelectedVersionDetails();
}

void MainWindow::updateSelectedVersionDetails()
{
    if (!m_versionsList || !m_versionsList->currentItem()) {
        return;
    }

    QListWidgetItem *item = m_versionsList->currentItem();
    const QString channel = item->data(ChannelRole).toString();
    const QString buildVersion = item->data(BuildVersionRole).toString();
    const QJsonObject manifest = selectedVersionManifest();
    const bool installed = !manifest.isEmpty();
    const bool isDefault = manifest.value(QStringLiteral("isDefault")).toBool();

    m_selectedVersionTitleLabel->setText(QStringLiteral("PHP %1").arg(channel));

    QStringList details;
    if (installed) {
        const QString phpBinary = manifest.value(QStringLiteral("phpBinary")).toString();
        m_selectedVersionStatusLabel->setText(isDefault
            ? QStringLiteral("Installed and selected as default PHP")
            : QStringLiteral("Installed"));
        m_selectedVersionStatusLabel->setStyleSheet(QStringLiteral("color: #1f7a3f; font-weight: 600;"));

        details << QStringLiteral("Installed version: %1").arg(manifest.value(QStringLiteral("version")).toString());
        details << QStringLiteral("Install path: %1").arg(manifest.value(QStringLiteral("installPath")).toString());
        details << QStringLiteral("PHP binary: %1").arg(phpBinary);
        details << QStringLiteral("Installed at: %1").arg(manifest.value(QStringLiteral("installedAtUtc")).toString());
        details << QString();
        details << QStringLiteral("$ %1 -v").arg(phpBinary);
        details << runPhpCommand(phpBinary, {QStringLiteral("-v")});
        details << QString();
        details << QStringLiteral("$ %1 --ini").arg(phpBinary);
        details << runPhpCommand(phpBinary, {QStringLiteral("--ini")});
        details << QString();
        details << QStringLiteral("$ %1 -m").arg(phpBinary);
        details << runPhpCommand(phpBinary, {QStringLiteral("-m")});
    } else {
        m_selectedVersionStatusLabel->setText(QStringLiteral("Not installed"));
        m_selectedVersionStatusLabel->setStyleSheet(QStringLiteral("color: #9b1c1c; font-weight: 600;"));
        details << QStringLiteral("Build version: %1").arg(buildVersion);
        details << QStringLiteral("Install path: %1").arg(QDir(m_installBaseEdit->text()).filePath(QStringLiteral("php/%1").arg(buildVersion)));
    }

    m_selectedVersionDetailsEdit->setPlainText(details.join('\n'));
    m_installVersionButton->setText(installed
        ? QStringLiteral("Rebuild PHP %1").arg(channel)
        : QStringLiteral("Install PHP %1").arg(channel));
    m_setVersionDefaultButton->setEnabled(installed && !isDefault && !m_controller.isRunning());
    m_removeVersionButton->setEnabled(installed && !m_controller.isRunning());
}

void MainWindow::setRunning(bool running)
{
    m_installTargetCombo->setEnabled(!running);
    const bool custom = m_installTargetCombo->currentData().toString() == QStringLiteral("custom");
    m_installBaseEdit->setEnabled(!running && custom);
    m_browseButton->setEnabled(!running && custom);
    m_versionsList->setEnabled(!running);
    m_installVersionButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    if (m_fixPathButton) {
        m_fixPathButton->setEnabled(!running && !m_currentDefaultBinPath.isEmpty() && !pathContainsDirectory(m_currentDefaultBinPath));
    }
    updateSelectedVersionDetails();
    for (const ModuleOption &module : m_modules) {
        if (module.checkBox) {
            module.checkBox->setEnabled(!running);
        }
    }
}
