#pragma once

#include <QList>
#include <QString>

struct GoChannel {
    QString channel;
    QString installVersion;
};

QList<GoChannel> availableGoChannels();
QString goChannelFromVersion(const QString &version);
QString goArchiveArchitecture();
QString goArchiveFileName(const QString &version);
QString goArchiveUrl(const QString &version);
