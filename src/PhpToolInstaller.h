#pragma once

#include <QString>

struct ToolInstallResult {
    bool success = false;
    QString message;
    QString path;
};

class PhpToolInstaller
{
public:
    static QString installBaseBinPath(const QString &installBasePath);
    static QString defaultPhpBinary(const QString &installBasePath);
    static QString symfonyCliDownloadUrl();

    static ToolInstallResult installComposer(const QString &installBasePath);
    static ToolInstallResult installSymfonyCli(const QString &installBasePath);
};
