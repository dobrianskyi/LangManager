#pragma once

#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QUrl>

struct LocalSourcePackage {
    QString name;
    QUrl sourceUrl;
    QString sourceDirectoryName;
    QStringList configureArguments;
    QString buildSystem = QStringLiteral("configure");
    QString configureProgram = QStringLiteral("configure");
};

struct PhpBuildRequest {
    QString version;
    QUrl sourceUrl;
    QString installBasePath;
    QStringList selectedModules;
    QStringList configureFlags;
    QStringList peclExtensions;
    QList<LocalSourcePackage> localPackages;
};

class PhpBuildController : public QObject
{
    Q_OBJECT

public:
    explicit PhpBuildController(QObject *parent = nullptr);

    bool isRunning() const;

public slots:
    void start(const PhpBuildRequest &request);
    void cancel();

signals:
    void statusChanged(const QString &status);
    void logLine(const QString &line);
    void progressChanged(int value);
    void finished(bool success, const QString &message, const QString &installPath);

private slots:
    void onDownloadReadyRead();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    enum class Step {
        Idle,
        Downloading,
        Extracting,
        DownloadingLocalPackage,
        ExtractingLocalPackage,
        ConfiguringLocalPackage,
        BuildingLocalPackage,
        InstallingLocalPackage,
        Configuring,
        Building,
        Installing,
        DownloadingPecl,
        ExtractingPecl,
        PhpizingPecl,
        ConfiguringPecl,
        BuildingPecl,
        InstallingPecl
    };

    void fail(const QString &message);
    void appendLog(const QString &text);
    void preparePaths();
    void beginDownload();
    void beginExtraction();
    void runNextLocalPackage();
    void beginLocalPackageDownload();
    void beginLocalPackageExtraction();
    void runLocalPackageConfigure();
    void runLocalPackageMake();
    void runLocalPackageInstall();
    void runConfigure();
    void runMake();
    void runInstall();
    void runNextPeclExtension();
    void beginPeclDownload(const QString &extension);
    void beginPeclExtraction();
    void runPeclPhpize();
    void runPeclConfigure();
    void runPeclMake();
    void runPeclInstall();
    void runProcess(Step step, const QString &program, const QStringList &arguments, const QString &workingDirectory);
    QProcessEnvironment buildEnvironment() const;
    QString phpIniPath() const;
    QString phpIniScanDirectory() const;
    bool writePhpIni(QString *errorMessage);
    bool writeInstallManifest(QString *errorMessage) const;
    void cleanupSuccessfulBuildArtifacts();
    void markComplete();

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QFile m_downloadFile;
    QProcess m_process;
    PhpBuildRequest m_request;
    Step m_step = Step::Idle;
    QString m_workRoot;
    QString m_archivePath;
    QString m_sourcePath;
    QString m_installPath;
    QString m_localArchivePath;
    QString m_localSourcePath;
    QString m_localBuildPath;
    QString m_localPrefixPath;
    QString m_peclArchivePath;
    QString m_peclSourcePath;
    int m_currentLocalPackageIndex = 0;
    int m_currentPeclIndex = 0;
    bool m_cancelRequested = false;
};
