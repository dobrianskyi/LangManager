#include "GoDefaultSwitcher.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>

namespace {

bool replaceManagedLink(const QString &targetPath, const QString &linkPath, QString *errorMessage)
{
    const QFileInfo targetInfo(targetPath);
    if (!targetInfo.exists() || !targetInfo.isFile()) {
        return true;
    }

    const QFileInfo linkInfo(linkPath);
    if ((linkInfo.exists() || linkInfo.isSymLink()) && !linkInfo.isSymLink()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Refusing to overwrite non-symlink file: %1").arg(linkPath);
        }
        return false;
    }

    if (linkInfo.isSymLink() && !QFile::remove(linkPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot remove old symlink: %1").arg(linkPath);
        }
        return false;
    }

    if (!QFile::link(targetPath, linkPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create symlink %1 -> %2").arg(linkPath, targetPath);
        }
        return false;
    }

    return true;
}

} // namespace

bool GoDefaultSwitcher::setDefault(const QString &installBasePath, const QJsonObject &manifest, QString *errorMessage)
{
    const QString status = manifest.value(QStringLiteral("status")).toString();
    const QString version = manifest.value(QStringLiteral("version")).toString();
    const QString installPath = manifest.value(QStringLiteral("installPath")).toString();
    const QString goBinary = manifest.value(QStringLiteral("goBinary")).toString();

    if (status != QStringLiteral("ready") || version.isEmpty() || installPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Selected Go version is not ready.");
        }
        return false;
    }

    if (!QFileInfo::exists(goBinary)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Go binary does not exist: %1").arg(goBinary);
        }
        return false;
    }

    const QString binPath = QDir(installBasePath).filePath(QStringLiteral("bin"));
    if (!QDir().mkpath(binPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create default bin directory: %1").arg(binPath);
        }
        return false;
    }

    const QStringList binaryNames = {
        QStringLiteral("go"),
        QStringLiteral("gofmt"),
    };

    for (const QString &binaryName : binaryNames) {
        const QString targetPath = QDir(installPath).filePath(QStringLiteral("bin/%1").arg(binaryName));
        const QString linkPath = QDir(binPath).filePath(binaryName);
        if (!replaceManagedLink(targetPath, linkPath, errorMessage)) {
            return false;
        }
    }

    const QString registryPath = QDir(installBasePath).filePath(QStringLiteral("go/installed.json"));
    QFile registryFile(registryPath);
    QJsonObject registry;
    if (registryFile.open(QIODevice::ReadOnly)) {
        registry = QJsonDocument::fromJson(registryFile.readAll()).object();
    }

    registry.insert(QStringLiteral("schemaVersion"), 1);
    registry.insert(QStringLiteral("defaultVersion"), version);
    registry.insert(QStringLiteral("defaultInstallPath"), installPath);
    registry.insert(QStringLiteral("defaultGoBinary"), goBinary);
    registry.insert(QStringLiteral("defaultBinPath"), binPath);
    registry.insert(QStringLiteral("defaultSetAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile output(registryPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write registry %1: %2").arg(registryPath, output.errorString());
        }
        return false;
    }
    output.write(QJsonDocument(registry).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot commit registry %1: %2").arg(registryPath, output.errorString());
        }
        return false;
    }

    return true;
}
