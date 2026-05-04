#pragma once

#include "GoInstallController.h"
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
    void updateGoInstallBaseFromTarget(int index);
    void chooseInstallBase();
    void chooseGoInstallBase();
    void onVersionSelectionChanged();
    void onGoVersionSelectionChanged();
    void installSelectedVersion();
    void installSelectedGoVersion();
    void startBuild();
    void startGoInstall();
    void onBuildFinished(bool success, const QString &message, const QString &installPath);
    void onGoInstallFinished(bool success, const QString &message, const QString &installPath);
    void setSelectedVersionAsDefault();
    void setSelectedGoVersionAsDefault();
    void removeSelectedVersion();
    void removeSelectedGoVersion();
    void fixPathForCurrentDefault();
    void fixPathForCurrentDefaultGo();
    void applyBuildProfile(int index);
    void installComposerCli();
    void installSymfonyCli();
    void refreshToolStatus();
    void showVersionContextMenu(const QPoint &position);
    void showGoVersionContextMenu(const QPoint &position);

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
    QString selectedGoVersion() const;
    QStringList selectedModuleLabels() const;
    QString installedRegistryPath() const;
    QString installedGoRegistryPath() const;
    QJsonObject selectedVersionManifest() const;
    QJsonObject selectedGoVersionManifest() const;
    QString runPhpCommand(const QString &phpBinary, const QStringList &arguments) const;
    QString runGoCommand(const QString &goBinary, const QStringList &arguments) const;
    QString installBaseBinPath() const;
    QString goInstallBaseBinPath() const;
    bool setManifestAsDefault(const QJsonObject &manifest);
    bool setGoManifestAsDefault(const QJsonObject &manifest);
    void refreshInstalledVersions();
    void refreshInstalledGoVersions();
    void updateSelectedVersionDetails();
    void updateSelectedGoVersionDetails();
    void setRunning(bool running);
    void setGoRunning(bool running);

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

    QComboBox *m_goInstallTargetCombo = nullptr;
    QLineEdit *m_goInstallBaseEdit = nullptr;
    QPushButton *m_goBrowseButton = nullptr;
    QListWidget *m_goVersionsList = nullptr;
    QLabel *m_currentGoLabel = nullptr;
    QLabel *m_selectedGoVersionTitleLabel = nullptr;
    QLabel *m_selectedGoVersionStatusLabel = nullptr;
    QTextEdit *m_selectedGoVersionDetailsEdit = nullptr;
    QPushButton *m_installGoVersionButton = nullptr;
    QPushButton *m_setGoVersionDefaultButton = nullptr;
    QPushButton *m_removeGoVersionButton = nullptr;
    QPushButton *m_goFixPathButton = nullptr;
    QPushButton *m_goCancelButton = nullptr;
    QLabel *m_goStatusLabel = nullptr;
    QLabel *m_goReadyLabel = nullptr;
    QProgressBar *m_goProgressBar = nullptr;
    QTextEdit *m_goLogEdit = nullptr;
    GoInstallController m_goController;
    QString m_currentDefaultGoBinPath;
};
