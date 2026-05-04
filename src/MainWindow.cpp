#include "MainWindow.h"

#include "BuildCatalog.h"
#include "CommandRunner.h"
#include "GoDefaultSwitcher.h"
#include "GoVersionCatalog.h"
#include "PhpIniRepair.h"
#include "PhpDefaultSwitcher.h"
#include "PhpToolInstaller.h"
#include "PhpVersionCatalog.h"
#include "ShellPathHelper.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
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
#include <QMenu>
#include <QMessageBox>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStandardPaths>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int ManifestRole = Qt::UserRole + 1;
constexpr int BuildVersionRole = Qt::UserRole + 2;
constexpr int ChannelRole = Qt::UserRole + 3;
const QColor InstalledGreen(31, 122, 63);

QString goSymlinkPath(const QString &installBasePath, const QString &defaultBinPath)
{
    const QString binPath = defaultBinPath.isEmpty()
        ? QDir(installBasePath).filePath(QStringLiteral("bin"))
        : defaultBinPath;
    return QDir(binPath).filePath(QStringLiteral("go"));
}

QString goDefaultSummaryText(const QString &installBasePath, const QString &defaultVersion, const QString &defaultBinPath)
{
    if (defaultVersion.isEmpty()) {
        return QStringLiteral("Default: not selected");
    }

    const QString linkPath = goSymlinkPath(installBasePath, defaultBinPath);
    const QString binPath = QFileInfo(linkPath).absolutePath();
    const QFileInfo linkInfo(linkPath);
    const QString targetPath = linkInfo.isSymLink() ? linkInfo.symLinkTarget() : QString();

    QString summary;
    if (targetPath.isEmpty()) {
        summary = QStringLiteral("Default: Go %1 via %2").arg(defaultVersion, linkPath);
    } else {
        summary = QStringLiteral("Default: Go %1 via %2 -> %3").arg(defaultVersion, linkPath, targetPath);
    }
    if (!pathContainsDirectory(binPath)) {
        summary.append(QStringLiteral("\nPATH missing: add %1 to use `go` in a terminal.").arg(binPath));
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
    connect(&m_goController, &GoInstallController::statusChanged, m_goStatusLabel, &QLabel::setText);
    connect(&m_goController, &GoInstallController::progressChanged, m_goProgressBar, &QProgressBar::setValue);
    connect(&m_goController, &GoInstallController::logLine, this, [this](const QString &line) {
        if (!m_goLogEdit) {
            return;
        }
        m_goLogEdit->append(line.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
        m_goLogEdit->verticalScrollBar()->setValue(m_goLogEdit->verticalScrollBar()->maximum());
    });
    connect(&m_goController, &GoInstallController::finished, this, &MainWindow::onGoInstallFinished);
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

void MainWindow::updateGoInstallBaseFromTarget(int index)
{
    const QString value = m_goInstallTargetCombo->itemData(index).toString();
    const bool custom = value == QStringLiteral("custom");
    m_goInstallBaseEdit->setEnabled(custom);
    m_goBrowseButton->setEnabled(custom);

    if (value == QStringLiteral("local")) {
        m_goInstallBaseEdit->setText(QDir::home().filePath(QStringLiteral(".local")));
    } else if (value == QStringLiteral("opt")) {
        m_goInstallBaseEdit->setText(QStringLiteral("/opt"));
    }
    refreshInstalledGoVersions();
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

void MainWindow::chooseGoInstallBase()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Select install base"), m_goInstallBaseEdit->text());
    if (!path.isEmpty()) {
        m_goInstallBaseEdit->setText(path);
        refreshInstalledGoVersions();
    }
}

void MainWindow::onVersionSelectionChanged()
{
    updateSelectedVersionDetails();
}

void MainWindow::onGoVersionSelectionChanged()
{
    updateSelectedGoVersionDetails();
}

void MainWindow::installSelectedVersion()
{
    startBuild();
}

void MainWindow::installSelectedGoVersion()
{
    startGoInstall();
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

void MainWindow::startGoInstall()
{
    const QString version = selectedGoVersion();
    GoInstallRequest request;
    request.version = version;
    request.archiveUrl = QUrl(goArchiveUrl(version));
    request.installBasePath = m_goInstallBaseEdit->text();

    if (goArchiveArchitecture().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Unsupported architecture"), QStringLiteral("Cannot resolve a Go Linux archive for this CPU architecture."));
        return;
    }

    const QStringList preflight = {
        QStringLiteral("Go version: %1").arg(request.version),
        QStringLiteral("Install base: %1").arg(request.installBasePath),
        QStringLiteral("Archive: %1").arg(request.archiveUrl.toString()),
    };
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Install Go"),
        QStringLiteral("Preflight check passed.\n\n%1\n\nStart install?").arg(preflight.join(QStringLiteral("\n"))));
    if (answer != QMessageBox::Yes) {
        return;
    }

    m_goLogEdit->clear();
    m_goReadyLabel->setText(QStringLiteral("Installing Go %1").arg(request.version));
    m_goReadyLabel->setStyleSheet(QStringLiteral("color: #9a6b00; font-weight: 600;"));
    setGoRunning(true);
    m_goController.start(request);
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

void MainWindow::onGoInstallFinished(bool success, const QString &message, const QString &installPath)
{
    setGoRunning(false);
    if (success) {
        m_goReadyLabel->setText(QStringLiteral("Ready: %1/bin/go").arg(installPath));
        m_goReadyLabel->setStyleSheet(QStringLiteral("color: #1f7a3f; font-weight: 600;"));
        refreshInstalledGoVersions();
    } else {
        m_goReadyLabel->setText(QStringLiteral("Failed"));
        m_goReadyLabel->setStyleSheet(QStringLiteral("color: #9b1c1c; font-weight: 600;"));
    }
    if (m_goLogEdit) {
        m_goLogEdit->append(message.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
        m_goLogEdit->verticalScrollBar()->setValue(m_goLogEdit->verticalScrollBar()->maximum());
    }
}

void MainWindow::setSelectedVersionAsDefault()
{
    const QJsonObject manifest = selectedVersionManifest();
    if (manifest.isEmpty()) {
        return;
    }

    setManifestAsDefault(manifest);
}

void MainWindow::setSelectedGoVersionAsDefault()
{
    const QJsonObject manifest = selectedGoVersionManifest();
    if (manifest.isEmpty()) {
        return;
    }

    setGoManifestAsDefault(manifest);
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

void MainWindow::removeSelectedGoVersion()
{
    const QJsonObject manifest = selectedGoVersionManifest();
    if (manifest.isEmpty()) {
        return;
    }

    const QString version = manifest.value(QStringLiteral("version")).toString();
    const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Remove Go %1").arg(version),
        QStringLiteral("Remove %1 and its registry entry?").arg(installPath));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QFile registryFile(installedGoRegistryPath());
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
            QStringLiteral("go"),
            QStringLiteral("gofmt"),
        };
        const QString binPath = registry.value(QStringLiteral("defaultBinPath")).toString(
            QDir(m_goInstallBaseEdit->text()).filePath(QStringLiteral("bin")));
        for (const QString &binaryName : binaryNames) {
            const QString linkPath = QDir(binPath).filePath(binaryName);
            const QFileInfo linkInfo(linkPath);
            if (linkInfo.isSymLink() && linkInfo.symLinkTarget().startsWith(installPath)) {
                QFile::remove(linkPath);
            }
        }
        registry.remove(QStringLiteral("defaultVersion"));
        registry.remove(QStringLiteral("defaultInstallPath"));
        registry.remove(QStringLiteral("defaultGoBinary"));
        registry.remove(QStringLiteral("defaultBinPath"));
        registry.remove(QStringLiteral("defaultSetAtUtc"));
    }

    QSaveFile output(installedGoRegistryPath());
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
        QMessageBox::warning(this, QStringLiteral("Cannot remove Go"), QStringLiteral("Cannot remove %1").arg(installPath));
        return;
    }

    if (m_goLogEdit) {
        m_goLogEdit->append(QStringLiteral("Removed Go %1: %2").arg(version, installPath).toHtmlEscaped());
    }
    refreshInstalledGoVersions();
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
               << "# LangManager PATH\n"
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

    appendLogLine(QStringLiteral("Added LangManager bin directory to PATH config: %1").arg(configPath));
    QMessageBox::information(
        this,
        QStringLiteral("PATH updated"),
        QStringLiteral("Added %1 to %2. Open a new terminal, then run php -v.")
            .arg(m_currentDefaultBinPath, configPath));
    refreshInstalledVersions();
}

void MainWindow::fixPathForCurrentDefaultGo()
{
    if (m_currentDefaultGoBinPath.isEmpty()) {
        return;
    }

    const QString configPath = preferredShellConfigPath();
    QFile existingConfig(configPath);
    QString content;
    if (existingConfig.open(QIODevice::ReadOnly)) {
        content = QString::fromUtf8(existingConfig.readAll());
    }

    const QString pathExpression = shellPathExpression(m_currentDefaultGoBinPath);
    if (!shellConfigAlreadyContainsPath(content, m_currentDefaultGoBinPath)) {
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
               << "# LangManager Go PATH\n"
               << "case \":$PATH:\" in\n"
               << "    *\":" << pathExpression << ":\"*) ;;\n"
               << "    *) export PATH=\"" << pathExpression << ":$PATH\" ;;\n"
               << "esac\n";
    }

    const QString currentPath = qEnvironmentVariable("PATH");
    if (!pathContainsDirectory(m_currentDefaultGoBinPath)) {
        qputenv("PATH", QStringLiteral("%1:%2")
            .arg(QDir(m_currentDefaultGoBinPath).absolutePath(), currentPath)
            .toUtf8());
    }

    if (m_goLogEdit) {
        m_goLogEdit->append(QStringLiteral("Added LangManager Go bin directory to PATH config: %1").arg(configPath).toHtmlEscaped());
    }
    QMessageBox::information(
        this,
        QStringLiteral("PATH updated"),
        QStringLiteral("Added %1 to %2. Open a new terminal, then run go version.")
            .arg(m_currentDefaultGoBinPath, configPath));
    refreshInstalledGoVersions();
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
    const ToolInstallResult result = PhpToolInstaller::installComposer(m_installBaseEdit->text());
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Composer"), result.message);
        return;
    }

    appendLogLine(result.message);
    refreshToolStatus();
}

void MainWindow::installSymfonyCli()
{
    const ToolInstallResult result = PhpToolInstaller::installSymfonyCli(m_installBaseEdit->text());
    if (!result.success) {
        QMessageBox::warning(this, QStringLiteral("Cannot install Symfony CLI"), result.message);
        return;
    }

    appendLogLine(result.message);
    refreshToolStatus();
}

void MainWindow::refreshToolStatus()
{
    const QString composerPath = QDir(installBaseBinPath()).filePath(QStringLiteral("composer"));
    const QString symfonyPath = QDir(installBaseBinPath()).filePath(QStringLiteral("symfony"));

    if (m_composerStatusLabel) {
        if (QFileInfo::exists(composerPath)) {
            const QString output = runToolCommand(composerPath, {QStringLiteral("--version")});
            m_composerStatusLabel->setText(compactToolStatus(QStringLiteral("Composer"), output));
            m_composerStatusLabel->setToolTip(compactToolTooltip(output));
        } else {
            m_composerStatusLabel->setText(QStringLiteral("Composer is not installed in this install base."));
            m_composerStatusLabel->setToolTip({});
        }
    }
    if (m_symfonyStatusLabel) {
        if (QFileInfo::exists(symfonyPath)) {
            const QString output = runToolCommand(symfonyPath, {QStringLiteral("-V")});
            m_symfonyStatusLabel->setText(compactToolStatus(QStringLiteral("Symfony CLI"), output));
            m_symfonyStatusLabel->setToolTip(compactToolTooltip(output));
        } else {
            m_symfonyStatusLabel->setText(QStringLiteral("Symfony CLI is not installed in this install base."));
            m_symfonyStatusLabel->setToolTip({});
        }
    }
}

void MainWindow::showVersionContextMenu(const QPoint &position)
{
    if (!m_versionsList || m_controller.isRunning()) {
        return;
    }

    QListWidgetItem *item = m_versionsList->itemAt(position);
    if (!item) {
        return;
    }

    const QString payload = item->data(ManifestRole).toString();
    if (payload.isEmpty()) {
        return;
    }

    const QJsonObject manifest = QJsonDocument::fromJson(payload.toUtf8()).object();
    const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
    if (installPath.isEmpty() || !QFileInfo::exists(installPath)) {
        return;
    }

    m_versionsList->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction(QStringLiteral("Open folder"), this, [installPath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(installPath));
    });
    menu.exec(m_versionsList->viewport()->mapToGlobal(position));
}

void MainWindow::showGoVersionContextMenu(const QPoint &position)
{
    if (!m_goVersionsList || m_goController.isRunning()) {
        return;
    }

    QListWidgetItem *item = m_goVersionsList->itemAt(position);
    if (!item) {
        return;
    }

    const QString payload = item->data(ManifestRole).toString();
    if (payload.isEmpty()) {
        return;
    }

    const QJsonObject manifest = QJsonDocument::fromJson(payload.toUtf8()).object();
    const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
    if (installPath.isEmpty() || !QFileInfo::exists(installPath)) {
        return;
    }

    m_goVersionsList->setCurrentItem(item);

    QMenu menu(this);
    menu.addAction(QStringLiteral("Open folder"), this, [installPath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(installPath));
    });
    menu.exec(m_goVersionsList->viewport()->mapToGlobal(position));
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

bool MainWindow::setGoManifestAsDefault(const QJsonObject &manifest)
{
    QString error;
    if (!GoDefaultSwitcher::setDefault(m_goInstallBaseEdit->text(), manifest, &error)) {
        if (m_currentGoLabel) {
            m_currentGoLabel->setText(QStringLiteral("Default switch failed"));
        }
        QMessageBox::warning(this, QStringLiteral("Cannot set default Go"), error);
        return false;
    }

    const QString version = manifest.value(QStringLiteral("version")).toString();
    const QString binPath = QDir(m_goInstallBaseEdit->text()).filePath(QStringLiteral("bin"));
    const QString summary = goDefaultSummaryText(m_goInstallBaseEdit->text(), version, binPath);
    if (m_currentGoLabel) {
        m_currentGoLabel->setText(summary);
    }
    refreshInstalledGoVersions();
    return true;
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("LangManager"));
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

    auto *languageTabs = new QTabWidget(central);
    languageTabs->setDocumentMode(true);
    languageTabs->setTabPosition(QTabWidget::West);
    languageTabs->setMovable(false);
    rootLayout->addWidget(languageTabs, 1);

    auto *phpPage = new QWidget(languageTabs);
    auto *phpPageLayout = new QVBoxLayout(phpPage);
    phpPageLayout->setContentsMargins(0, 0, 0, 0);
    phpPageLayout->setSpacing(0);

    auto *tabs = new QTabWidget(phpPage);
    tabs->setDocumentMode(true);
    phpPageLayout->addWidget(tabs, 1);

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
    m_versionsList->setContextMenuPolicy(Qt::CustomContextMenu);
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
    m_composerStatusLabel->setWordWrap(false);
    m_composerStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_composerStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_installComposerButton = new QPushButton(QStringLiteral("Install or Update Composer"), composerBox);
    composerBoxLayout->addWidget(m_composerStatusLabel);
    composerBoxLayout->addWidget(m_installComposerButton);

    auto *symfonyBox = new QGroupBox(QStringLiteral("Symfony CLI"), composerPage);
    auto *symfonyBoxLayout = new QVBoxLayout(symfonyBox);
    symfonyBoxLayout->setContentsMargins(8, 12, 8, 8);
    symfonyBoxLayout->setSpacing(6);
    m_symfonyStatusLabel = new QLabel(QStringLiteral("Symfony CLI is not installed in this install base."), symfonyBox);
    m_symfonyStatusLabel->setWordWrap(false);
    m_symfonyStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_symfonyStatusLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
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

    auto *goPage = new QWidget(languageTabs);
    auto *goLayout = new QHBoxLayout(goPage);
    goLayout->setContentsMargins(10, 10, 10, 10);
    goLayout->setSpacing(10);

    auto *goLeftColumn = new QWidget(goPage);
    goLeftColumn->setMaximumWidth(430);
    goLeftColumn->setMinimumWidth(360);
    auto *goLeftLayout = new QVBoxLayout(goLeftColumn);
    goLeftLayout->setContentsMargins(0, 0, 0, 0);
    goLeftLayout->setSpacing(8);

    auto *goFormBox = new QGroupBox(QStringLiteral("Install location"), goLeftColumn);
    auto *goFormLayout = new QFormLayout(goFormBox);
    goFormLayout->setContentsMargins(8, 12, 8, 8);
    goFormLayout->setHorizontalSpacing(8);
    goFormLayout->setVerticalSpacing(6);

    m_goInstallTargetCombo = new QComboBox(goFormBox);
    m_goInstallTargetCombo->addItem(QStringLiteral("User (~/.local)"), QStringLiteral("local"));
    m_goInstallTargetCombo->addItem(QStringLiteral("System (/opt)"), QStringLiteral("opt"));
    m_goInstallTargetCombo->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    goFormLayout->addRow(QStringLiteral("Install target"), m_goInstallTargetCombo);

    auto *goPathRow = new QWidget(goFormBox);
    auto *goPathLayout = new QHBoxLayout(goPathRow);
    goPathLayout->setContentsMargins(0, 0, 0, 0);
    m_goInstallBaseEdit = new QLineEdit(goPathRow);
    m_goInstallBaseEdit->setText(QDir::home().filePath(QStringLiteral(".local")));
    m_goBrowseButton = new QPushButton(QStringLiteral("Browse"), goPathRow);
    goPathLayout->addWidget(m_goInstallBaseEdit, 1);
    goPathLayout->addWidget(m_goBrowseButton);
    goFormLayout->addRow(QStringLiteral("Install base"), goPathRow);

    auto *goVersionsBox = new QGroupBox(QStringLiteral("Go versions"), goLeftColumn);
    auto *goVersionsBoxLayout = new QVBoxLayout(goVersionsBox);
    goVersionsBoxLayout->setContentsMargins(8, 12, 8, 8);
    goVersionsBoxLayout->setSpacing(6);
    m_currentGoLabel = new QLabel(QStringLiteral("Default: not selected"), goVersionsBox);
    m_currentGoLabel->setWordWrap(true);
    m_goFixPathButton = new QPushButton(QStringLiteral("Fix PATH"), goVersionsBox);
    m_goFixPathButton->setVisible(false);
    m_goVersionsList = new QListWidget(goVersionsBox);
    m_goVersionsList->setUniformItemSizes(true);
    m_goVersionsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_goVersionsList->setContextMenuPolicy(Qt::CustomContextMenu);
    goVersionsBoxLayout->addWidget(m_currentGoLabel);
    goVersionsBoxLayout->addWidget(m_goFixPathButton);
    goVersionsBoxLayout->addWidget(m_goVersionsList, 1);

    auto *goButtonRow = new QHBoxLayout();
    m_installGoVersionButton = new QPushButton(QStringLiteral("Install"), goLeftColumn);
    m_setGoVersionDefaultButton = new QPushButton(QStringLiteral("Set default"), goLeftColumn);
    m_removeGoVersionButton = new QPushButton(QStringLiteral("Remove"), goLeftColumn);
    m_goCancelButton = new QPushButton(QStringLiteral("Cancel"), goLeftColumn);
    m_goCancelButton->setEnabled(false);
    goButtonRow->addWidget(m_installGoVersionButton, 1);
    goButtonRow->addWidget(m_setGoVersionDefaultButton);
    goButtonRow->addWidget(m_removeGoVersionButton);
    goButtonRow->addWidget(m_goCancelButton);

    auto *goStateBox = new QGroupBox(QStringLiteral("Current action"), goLeftColumn);
    auto *goStateLayout = new QVBoxLayout(goStateBox);
    goStateLayout->setContentsMargins(8, 12, 8, 8);
    goStateLayout->setSpacing(6);
    m_goStatusLabel = new QLabel(QStringLiteral("Idle"), goStateBox);
    m_goReadyLabel = new QLabel(QStringLiteral("No install running"), goStateBox);
    m_goReadyLabel->setWordWrap(true);
    m_goProgressBar = new QProgressBar(goStateBox);
    m_goProgressBar->setRange(0, 100);
    goStateLayout->addWidget(m_goStatusLabel);
    goStateLayout->addWidget(m_goReadyLabel);
    goStateLayout->addWidget(m_goProgressBar);

    goLeftLayout->addWidget(goFormBox);
    goLeftLayout->addWidget(goVersionsBox, 1);
    goLeftLayout->addLayout(goButtonRow);
    goLeftLayout->addWidget(goStateBox);

    auto *goRightColumn = new QWidget(goPage);
    auto *goRightLayout = new QVBoxLayout(goRightColumn);
    goRightLayout->setContentsMargins(0, 0, 0, 0);
    goRightLayout->setSpacing(8);

    auto *goDetailsBox = new QGroupBox(QStringLiteral("Selected version"), goRightColumn);
    auto *goDetailsLayout = new QVBoxLayout(goDetailsBox);
    goDetailsLayout->setContentsMargins(8, 12, 8, 8);
    goDetailsLayout->setSpacing(6);
    m_selectedGoVersionTitleLabel = new QLabel(QStringLiteral("Go"), goDetailsBox);
    m_selectedGoVersionTitleLabel->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    m_selectedGoVersionStatusLabel = new QLabel(goDetailsBox);
    m_selectedGoVersionStatusLabel->setWordWrap(true);
    m_selectedGoVersionDetailsEdit = new QTextEdit(goDetailsBox);
    m_selectedGoVersionDetailsEdit->setReadOnly(true);
    m_selectedGoVersionDetailsEdit->setMinimumHeight(160);
    goDetailsLayout->addWidget(m_selectedGoVersionTitleLabel);
    goDetailsLayout->addWidget(m_selectedGoVersionStatusLabel);
    goDetailsLayout->addWidget(m_selectedGoVersionDetailsEdit);

    auto *goOutputBox = new QGroupBox(QStringLiteral("Install output"), goRightColumn);
    auto *goOutputLayout = new QVBoxLayout(goOutputBox);
    goOutputLayout->setContentsMargins(8, 12, 8, 8);
    goOutputLayout->setSpacing(4);
    m_goLogEdit = new QTextEdit(goOutputBox);
    m_goLogEdit->setReadOnly(true);
    m_goLogEdit->setLineWrapMode(QTextEdit::NoWrap);
    goOutputLayout->addWidget(m_goLogEdit, 1);

    goRightLayout->addWidget(goDetailsBox);
    goRightLayout->addWidget(goOutputBox, 1);

    goLayout->addWidget(goLeftColumn);
    goLayout->addWidget(goRightColumn, 1);

    languageTabs->addTab(phpPage, QStringLiteral("PHP"));
    languageTabs->addTab(goPage, QStringLiteral("Go"));

    setCentralWidget(central);

    connect(m_installTargetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateInstallBaseFromTarget);
    connect(m_installBaseEdit, &QLineEdit::editingFinished, this, &MainWindow::refreshInstalledVersions);
    connect(m_versionsList, &QListWidget::currentRowChanged, this, &MainWindow::onVersionSelectionChanged);
    connect(m_versionsList, &QListWidget::customContextMenuRequested, this, &MainWindow::showVersionContextMenu);
    connect(m_installVersionButton, &QPushButton::clicked, this, &MainWindow::installSelectedVersion);
    connect(m_setVersionDefaultButton, &QPushButton::clicked, this, &MainWindow::setSelectedVersionAsDefault);
    connect(m_removeVersionButton, &QPushButton::clicked, this, &MainWindow::removeSelectedVersion);
    connect(m_fixPathButton, &QPushButton::clicked, this, &MainWindow::fixPathForCurrentDefault);
    connect(m_buildProfileCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::applyBuildProfile);
    connect(m_installComposerButton, &QPushButton::clicked, this, &MainWindow::installComposerCli);
    connect(m_installSymfonyButton, &QPushButton::clicked, this, &MainWindow::installSymfonyCli);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::chooseInstallBase);
    connect(m_cancelButton, &QPushButton::clicked, &m_controller, &PhpBuildController::cancel);
    connect(m_goInstallTargetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::updateGoInstallBaseFromTarget);
    connect(m_goInstallBaseEdit, &QLineEdit::editingFinished, this, &MainWindow::refreshInstalledGoVersions);
    connect(m_goVersionsList, &QListWidget::currentRowChanged, this, &MainWindow::onGoVersionSelectionChanged);
    connect(m_goVersionsList, &QListWidget::customContextMenuRequested, this, &MainWindow::showGoVersionContextMenu);
    connect(m_installGoVersionButton, &QPushButton::clicked, this, &MainWindow::installSelectedGoVersion);
    connect(m_setGoVersionDefaultButton, &QPushButton::clicked, this, &MainWindow::setSelectedGoVersionAsDefault);
    connect(m_removeGoVersionButton, &QPushButton::clicked, this, &MainWindow::removeSelectedGoVersion);
    connect(m_goFixPathButton, &QPushButton::clicked, this, &MainWindow::fixPathForCurrentDefaultGo);
    connect(m_goBrowseButton, &QPushButton::clicked, this, &MainWindow::chooseGoInstallBase);
    connect(m_goCancelButton, &QPushButton::clicked, &m_goController, &GoInstallController::cancel);
    applyBuildProfile(m_buildProfileCombo->currentIndex());
    updateInstallBaseFromTarget(m_installTargetCombo->currentIndex());
    updateGoInstallBaseFromTarget(m_goInstallTargetCombo->currentIndex());
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

QString MainWindow::selectedGoVersion() const
{
    if (!m_goVersionsList || !m_goVersionsList->currentItem()) {
        const QList<GoChannel> channels = availableGoChannels();
        return channels.isEmpty() ? QString() : channels.first().installVersion;
    }
    return m_goVersionsList->currentItem()->data(BuildVersionRole).toString();
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

QString MainWindow::installedRegistryPath() const
{
    return QDir(m_installBaseEdit->text()).filePath(QStringLiteral("php/installed.json"));
}

QString MainWindow::installedGoRegistryPath() const
{
    return QDir(m_goInstallBaseEdit->text()).filePath(QStringLiteral("go/installed.json"));
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

QJsonObject MainWindow::selectedGoVersionManifest() const
{
    if (!m_goVersionsList || !m_goVersionsList->currentItem()) {
        return {};
    }

    const QString payload = m_goVersionsList->currentItem()->data(ManifestRole).toString();
    if (payload.isEmpty()) {
        return {};
    }

    return QJsonDocument::fromJson(payload.toUtf8()).object();
}

QString MainWindow::runPhpCommand(const QString &phpBinary, const QStringList &arguments) const
{
    return ::runToolCommand(phpBinary, arguments);
}

QString MainWindow::runGoCommand(const QString &goBinary, const QStringList &arguments) const
{
    return ::runToolCommand(goBinary, arguments);
}

QString MainWindow::installBaseBinPath() const
{
    return PhpToolInstaller::installBaseBinPath(m_installBaseEdit->text());
}

QString MainWindow::goInstallBaseBinPath() const
{
    return QDir(m_goInstallBaseEdit->text()).filePath(QStringLiteral("bin"));
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
        QStringList repairLogLines;
        PhpIniRepair::ensureForManifest(manifest, &repairLogLines);
        for (const QString &line : repairLogLines) {
            appendLogLine(line);
        }
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

void MainWindow::refreshInstalledGoVersions()
{
    if (!m_goVersionsList) {
        return;
    }

    const QString previouslySelectedChannel = m_goVersionsList->currentItem()
        ? m_goVersionsList->currentItem()->data(ChannelRole).toString()
        : QString();
    m_goVersionsList->blockSignals(true);
    m_goVersionsList->clear();

    QJsonObject registry;
    QFile registryFile(installedGoRegistryPath());
    if (registryFile.open(QIODevice::ReadOnly)) {
        registry = QJsonDocument::fromJson(registryFile.readAll()).object();
    }

    const QString defaultInstallPath = registry.value(QStringLiteral("defaultInstallPath")).toString();
    QString defaultVersion = registry.value(QStringLiteral("defaultVersion")).toString();
    QString defaultBinPath = registry.value(QStringLiteral("defaultBinPath")).toString();
    if (defaultBinPath.isEmpty()) {
        defaultBinPath = QDir(m_goInstallBaseEdit->text()).filePath(QStringLiteral("bin"));
    }
    const QString defaultLinkTarget = QFileInfo(goSymlinkPath(m_goInstallBaseEdit->text(), defaultBinPath)).symLinkTarget();
    const bool hasDefaultSymlink = !defaultLinkTarget.isEmpty();
    QHash<QString, QJsonObject> manifestsByChannel;
    for (const QJsonValue &value : registry.value(QStringLiteral("versions")).toArray()) {
        QJsonObject manifest = value.toObject();
        if (manifest.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) {
            continue;
        }
        const QString version = manifest.value(QStringLiteral("version")).toString();
        const QString channel = goChannelFromVersion(version);
        manifestsByChannel.insert(channel, manifest);
    }

    int selectedRow = 0;
    const QList<GoChannel> channels = availableGoChannels();
    for (int i = 0; i < channels.size(); ++i) {
        const GoChannel channel = channels.at(i);
        QJsonObject manifest = manifestsByChannel.value(channel.channel);
        const bool installed = !manifest.isEmpty();
        const QString version = installed ? manifest.value(QStringLiteral("version")).toString() : channel.installVersion;
        const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
        const QString goBinary = manifest.value(QStringLiteral("goBinary")).toString();
        const bool registryDefault = !defaultInstallPath.isEmpty() && installPath == defaultInstallPath;
        const bool symlinkDefault = !defaultLinkTarget.isEmpty() && QFileInfo(defaultLinkTarget).absoluteFilePath() == QFileInfo(goBinary).absoluteFilePath();
        const bool isDefault = installed && (hasDefaultSymlink ? symlinkDefault : registryDefault);
        if (isDefault) {
            defaultVersion = version;
        }

        manifest.insert(QStringLiteral("isDefault"), isDefault);
        QString label;
        if (installed) {
            label = QStringLiteral("Go %1  (%2 installed)").arg(channel.channel, version);
        } else {
            label = QStringLiteral("Go %1  (not installed)").arg(channel.channel);
        }
        if (isDefault) {
            label.append(QStringLiteral("  default"));
        }
        const QString payload = QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact));

        auto *item = new QListWidgetItem(label);
        item->setData(ChannelRole, channel.channel);
        item->setData(BuildVersionRole, channel.installVersion);
        item->setData(ManifestRole, installed ? payload : QString());
        if (installed) {
            item->setForeground(QBrush(InstalledGreen));
        }
        QFont font = item->font();
        font.setBold(isDefault);
        item->setFont(font);
        m_goVersionsList->addItem(item);

        if (!previouslySelectedChannel.isEmpty() && previouslySelectedChannel == channel.channel) {
            selectedRow = i;
        } else if (previouslySelectedChannel.isEmpty() && isDefault) {
            selectedRow = i;
        }
    }

    const QString summary = goDefaultSummaryText(m_goInstallBaseEdit->text(), defaultVersion, defaultBinPath);
    if (m_currentGoLabel) {
        m_currentGoLabel->setText(summary);
    }
    m_currentDefaultGoBinPath = defaultVersion.isEmpty() ? QString() : QDir(defaultBinPath).absolutePath();
    if (m_goFixPathButton) {
        const bool showFixPath = !m_currentDefaultGoBinPath.isEmpty() && !pathContainsDirectory(m_currentDefaultGoBinPath);
        m_goFixPathButton->setVisible(showFixPath);
        m_goFixPathButton->setEnabled(showFixPath && !m_goController.isRunning());
        m_goFixPathButton->setText(QStringLiteral("Fix PATH (%1)").arg(m_currentDefaultGoBinPath));
    }

    if (m_goVersionsList->count() > 0) {
        m_goVersionsList->setCurrentRow(qBound(0, selectedRow, m_goVersionsList->count() - 1));
    }
    m_goVersionsList->blockSignals(false);
    updateSelectedGoVersionDetails();
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

void MainWindow::updateSelectedGoVersionDetails()
{
    if (!m_goVersionsList || !m_goVersionsList->currentItem()) {
        return;
    }

    QListWidgetItem *item = m_goVersionsList->currentItem();
    const QString channel = item->data(ChannelRole).toString();
    const QString installVersion = item->data(BuildVersionRole).toString();
    const QJsonObject manifest = selectedGoVersionManifest();
    const bool installed = !manifest.isEmpty();
    const bool isDefault = manifest.value(QStringLiteral("isDefault")).toBool();

    m_selectedGoVersionTitleLabel->setText(QStringLiteral("Go %1").arg(channel));

    QStringList details;
    if (installed) {
        const QString goBinary = manifest.value(QStringLiteral("goBinary")).toString();
        m_selectedGoVersionStatusLabel->setText(isDefault
            ? QStringLiteral("Installed and selected as default Go")
            : QStringLiteral("Installed"));
        m_selectedGoVersionStatusLabel->setStyleSheet(QStringLiteral("color: #1f7a3f; font-weight: 600;"));

        details << QStringLiteral("Installed version: %1").arg(manifest.value(QStringLiteral("version")).toString());
        details << QStringLiteral("Install path: %1").arg(manifest.value(QStringLiteral("installPath")).toString());
        details << QStringLiteral("Go binary: %1").arg(goBinary);
        details << QStringLiteral("Installed at: %1").arg(manifest.value(QStringLiteral("installedAtUtc")).toString());
        details << QString();
        details << QStringLiteral("$ %1 version").arg(goBinary);
        details << runGoCommand(goBinary, {QStringLiteral("version")});
        details << QString();
        details << QStringLiteral("$ %1 env GOROOT GOPATH GOTOOLDIR").arg(goBinary);
        details << runGoCommand(goBinary, {QStringLiteral("env"), QStringLiteral("GOROOT"), QStringLiteral("GOPATH"), QStringLiteral("GOTOOLDIR")});
    } else {
        m_selectedGoVersionStatusLabel->setText(QStringLiteral("Not installed"));
        m_selectedGoVersionStatusLabel->setStyleSheet(QStringLiteral("color: #9b1c1c; font-weight: 600;"));
        details << QStringLiteral("Install version: %1").arg(installVersion);
        details << QStringLiteral("Archive: %1").arg(goArchiveUrl(installVersion));
        details << QStringLiteral("Install path: %1").arg(QDir(m_goInstallBaseEdit->text()).filePath(QStringLiteral("go/%1").arg(installVersion)));
    }

    m_selectedGoVersionDetailsEdit->setPlainText(details.join('\n'));
    m_installGoVersionButton->setText(installed
        ? QStringLiteral("Reinstall Go %1").arg(channel)
        : QStringLiteral("Install Go %1").arg(channel));
    m_setGoVersionDefaultButton->setEnabled(installed && !isDefault && !m_goController.isRunning());
    m_removeGoVersionButton->setEnabled(installed && !m_goController.isRunning());
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

void MainWindow::setGoRunning(bool running)
{
    m_goInstallTargetCombo->setEnabled(!running);
    const bool custom = m_goInstallTargetCombo->currentData().toString() == QStringLiteral("custom");
    m_goInstallBaseEdit->setEnabled(!running && custom);
    m_goBrowseButton->setEnabled(!running && custom);
    m_goVersionsList->setEnabled(!running);
    m_installGoVersionButton->setEnabled(!running);
    m_goCancelButton->setEnabled(running);
    if (m_goFixPathButton) {
        m_goFixPathButton->setEnabled(!running && !m_currentDefaultGoBinPath.isEmpty() && !pathContainsDirectory(m_currentDefaultGoBinPath));
    }
    updateSelectedGoVersionDetails();
}
