#pragma once

#include <QList>
#include <QString>

struct PhpChannel {
    QString channel;
    QString buildVersion;
};

QList<PhpChannel> availablePhpChannels();
QString phpChannelFromVersion(const QString &version);
