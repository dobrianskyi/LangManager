#pragma once

#include "PhpBuildController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void updateInstallBaseFromTarget(int index);
    void chooseInstallBase();
    void onVersionSelectionChanged();
    void installSelectedVersion();
    void startBuild();
    void onBuildFinished(bool success, const QString &message, const QString &installPath);
    void setSelectedVersionAsDefault();
    void removeSelectedVersion();
    void fixPathForCurrentDefault();
    void applyBuildProfile(int index);
    void installComposerCli();
    void installSymfonyCli();
    void refreshToolStatus();

private:
    struct ModuleOption {
        QString label;
        QString flag;
        QString peclPackage;
        QString localPackageName;
        bool defaultChecked = true;
        QCheckBox *checkBox = nullptr;
    };

    void buildUi();
    void appendLogLine(const QString &line);
    QString selectedVersion() const;
    QStringList selectedModuleLabels() const;
    QStringList selectedConfigureFlags() const;
    QStringList selectedPeclExtensions() const;
    QList<LocalSourcePackage> selectedLocalPackages() const;
    QString installedRegistryPath() const;
    QJsonObject selectedVersionManifest() const;
    QString runPhpCommand(const QString &phpBinary, const QStringList &arguments) const;
    QString runToolCommand(const QString &program, const QStringList &arguments) const;
    bool downloadToFile(const QUrl &url, const QString &path, QString *errorMessage);
    QString defaultPhpBinary() const;
    QString installBaseBinPath() const;
    QString symfonyCliDownloadUrl() const;
    QString installedExtensionPath(const QString &installPath, const QString &fileName) const;
    bool ensurePhpIniForManifest(const QJsonObject &manifest);
    bool setManifestAsDefault(const QJsonObject &manifest);
    void refreshInstalledVersions();
    void updateSelectedVersionDetails();
    void setRunning(bool running);

    QComboBox *m_installTargetCombo = nullptr;
    QLineEdit *m_installBaseEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QListWidget *m_versionsList = nullptr;
    QLabel *m_currentPhpLabel = nullptr;
    QLabel *m_selectedVersionTitleLabel = nullptr;
    QLabel *m_selectedVersionStatusLabel = nullptr;
    QTextEdit *m_selectedVersionDetailsEdit = nullptr;
    QPushButton *m_installVersionButton = nullptr;
    QPushButton *m_setVersionDefaultButton = nullptr;
    QPushButton *m_removeVersionButton = nullptr;
    QPushButton *m_fixPathButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QComboBox *m_buildProfileCombo = nullptr;
    QLabel *m_composerStatusLabel = nullptr;
    QLabel *m_symfonyStatusLabel = nullptr;
    QPushButton *m_installComposerButton = nullptr;
    QPushButton *m_installSymfonyButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_readyLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QTextEdit *m_logEdit = nullptr;
    QList<ModuleOption> m_modules;
    PhpBuildController m_controller;
    QString m_currentDefaultBinPath;
};
