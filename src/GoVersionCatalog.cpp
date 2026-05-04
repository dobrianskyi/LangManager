#include "GoVersionCatalog.h"

#include <QSysInfo>

QList<GoChannel> availableGoChannels()
{
    return {
        {QStringLiteral("1.26"), QStringLiteral("1.26.2")},
        {QStringLiteral("1.25"), QStringLiteral("1.25.9")},
        {QStringLiteral("1.24"), QStringLiteral("1.24.13")},
        {QStringLiteral("1.23"), QStringLiteral("1.23.12")},
        {QStringLiteral("1.22"), QStringLiteral("1.22.12")},
        {QStringLiteral("1.21"), QStringLiteral("1.21.13")},
        {QStringLiteral("1.20"), QStringLiteral("1.20.14")},
        {QStringLiteral("1.19"), QStringLiteral("1.19.13")},
        {QStringLiteral("1.18"), QStringLiteral("1.18.10")},
    };
}

QString goChannelFromVersion(const QString &version)
{
    QString normalized = version;
    if (normalized.startsWith(QStringLiteral("go"))) {
        normalized.remove(0, 2);
    }

    const QStringList parts = normalized.split('.');
    if (parts.size() < 2) {
        return normalized;
    }
    return QStringLiteral("%1.%2").arg(parts.at(0), parts.at(1));
}

QString goArchiveArchitecture()
{
    const QString cpu = QSysInfo::currentCpuArchitecture();
    if (cpu == QStringLiteral("x86_64")) {
        return QStringLiteral("amd64");
    }
    if (cpu == QStringLiteral("i386") || cpu == QStringLiteral("i686")) {
        return QStringLiteral("386");
    }
    if (cpu == QStringLiteral("arm64") || cpu == QStringLiteral("aarch64")) {
        return QStringLiteral("arm64");
    }
    if (cpu == QStringLiteral("arm")) {
        return QStringLiteral("armv6l");
    }
    return cpu;
}

QString goArchiveFileName(const QString &version)
{
    return QStringLiteral("go%1.linux-%2.tar.gz").arg(version, goArchiveArchitecture());
}

QString goArchiveUrl(const QString &version)
{
    return QStringLiteral("https://go.dev/dl/%1").arg(goArchiveFileName(version));
}
