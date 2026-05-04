#include "PhpVersionCatalog.h"

#include <QStringList>

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
