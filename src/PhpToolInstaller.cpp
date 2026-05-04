#include "PhpToolInstaller.h"

#include "ArchiveExtractor.h"
#include "FileDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>

QString PhpToolInstaller::installBaseBinPath(const QString &installBasePath)
{
    return QDir(installBasePath).filePath(QStringLiteral("bin"));
}

QString PhpToolInstaller::defaultPhpBinary(const QString &installBasePath)
{
    const QString managedPhp = QDir(installBaseBinPath(installBasePath)).filePath(QStringLiteral("php"));
    if (QFileInfo::exists(managedPhp)) {
        return managedPhp;
    }

    QFile registryFile(QDir(installBasePath).filePath(QStringLiteral("php/installed.json")));
    if (registryFile.open(QIODevice::ReadOnly)) {
        const QJsonObject registry = QJsonDocument::fromJson(registryFile.readAll()).object();
        const QString phpBinary = registry.value(QStringLiteral("defaultPhpBinary")).toString();
        if (QFileInfo::exists(phpBinary)) {
            return phpBinary;
        }
    }
    return {};
}

QString PhpToolInstaller::symfonyCliDownloadUrl()
{
    const QString architecture = QSysInfo::currentCpuArchitecture();
    QString assetArchitecture = QStringLiteral("amd64");
    if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64")) {
        assetArchitecture = QStringLiteral("arm64");
    } else if (architecture == QStringLiteral("i386") || architecture == QStringLiteral("i686")) {
        assetArchitecture = QStringLiteral("386");
    }
    return QStringLiteral("https://github.com/symfony-cli/symfony-cli/releases/latest/download/symfony-cli_linux_%1.tar.gz")
        .arg(assetArchitecture);
}

ToolInstallResult PhpToolInstaller::installComposer(const QString &installBasePath)
{
    if (defaultPhpBinary(installBasePath).isEmpty()) {
        return {false, QStringLiteral("Set an installed PHP version as default first."), {}};
    }

    const QString composerPath = QDir(installBasePath).filePath(QStringLiteral("tools/composer/composer.phar"));
    QString error;
    if (!downloadToFile(QUrl(QStringLiteral("https://getcomposer.org/download/latest-stable/composer.phar")), composerPath, &error)) {
        return {false, error, {}};
    }

    const QString binPath = installBaseBinPath(installBasePath);
    QDir().mkpath(binPath);
    const QString wrapperPath = QDir(binPath).filePath(QStringLiteral("composer"));
    QSaveFile wrapper(wrapperPath);
    if (!wrapper.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, wrapper.errorString(), {}};
    }
    wrapper.write(QStringLiteral("#!/usr/bin/env sh\nexec \"%1\" \"%2\" \"$@\"\n")
        .arg(QDir(binPath).filePath(QStringLiteral("php")), composerPath)
        .toUtf8());
    if (!wrapper.commit()) {
        return {false, wrapper.errorString(), {}};
    }
    QFile::setPermissions(wrapperPath, QFile::permissions(wrapperPath)
        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    return {true, QStringLiteral("Installed Composer locally: %1").arg(wrapperPath), wrapperPath};
}

ToolInstallResult PhpToolInstaller::installSymfonyCli(const QString &installBasePath)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        return {false, QStringLiteral("Cannot create temporary directory."), {}};
    }

    const QString archivePath = QDir(tempDir.path()).filePath(QStringLiteral("symfony-cli.tar.gz"));
    QString error;
    if (!downloadToFile(QUrl(symfonyCliDownloadUrl()), archivePath, &error)) {
        return {false, error, {}};
    }

    const QString extractPath = QDir(tempDir.path()).filePath(QStringLiteral("extract"));
    if (!ArchiveExtractor::extractArchive(archivePath, extractPath, &error)) {
        return {false, error, {}};
    }

    const QString extractedBinary = QDir(extractPath).filePath(QStringLiteral("symfony"));
    if (!QFileInfo::exists(extractedBinary)) {
        return {false, QStringLiteral("Symfony archive did not contain the expected binary."), {}};
    }

    const QString toolsPath = QDir(installBasePath).filePath(QStringLiteral("tools/symfony/symfony"));
    QDir().mkpath(QFileInfo(toolsPath).absolutePath());
    QFile::remove(toolsPath);
    if (!QFile::copy(extractedBinary, toolsPath)) {
        return {false, QStringLiteral("Cannot copy Symfony binary to %1").arg(toolsPath), {}};
    }
    QFile::setPermissions(toolsPath, QFile::permissions(toolsPath)
        | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    const QString binPath = installBaseBinPath(installBasePath);
    QDir().mkpath(binPath);
    const QString linkPath = QDir(binPath).filePath(QStringLiteral("symfony"));
    const QFileInfo linkInfo(linkPath);
    if ((linkInfo.exists() || linkInfo.isSymLink()) && !linkInfo.isSymLink()) {
        return {false, QStringLiteral("Refusing to overwrite non-symlink file: %1").arg(linkPath), {}};
    }
    if (linkInfo.isSymLink()) {
        QFile::remove(linkPath);
    }
    if (!QFile::link(toolsPath, linkPath)) {
        return {false, QStringLiteral("Cannot create symlink %1").arg(linkPath), {}};
    }

    return {true, QStringLiteral("Installed Symfony CLI locally: %1").arg(linkPath), linkPath};
}
