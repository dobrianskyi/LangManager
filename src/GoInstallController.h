#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

struct GoInstallRequest {
    QString version;
    QUrl archiveUrl;
    QString installBasePath;
};

class GoInstallController : public QObject
{
    Q_OBJECT

public:
    explicit GoInstallController(QObject *parent = nullptr);

    bool isRunning() const;

public slots:
    void start(const GoInstallRequest &request);
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

private:
    enum class Step {
        Idle,
        Downloading,
        Extracting,
        Installing
    };

    void fail(const QString &message);
    void preparePaths();
    void beginDownload();
    void beginExtraction();
    void installExtractedToolchain();
    bool writeInstallManifest(QString *errorMessage) const;
    void markComplete();

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QFile m_downloadFile;
    GoInstallRequest m_request;
    Step m_step = Step::Idle;
    QString m_downloadPath;
    QString m_stagingRoot;
    QString m_extractedGoPath;
    QString m_installPath;
    bool m_cancelRequested = false;
};
