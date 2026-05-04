#pragma once

#include <QString>
#include <QStringList>

class BuildArtifactCleaner
{
public:
    static QStringList cleanupInstalledRuntimeArtifacts(const QString &installPath);
    static QStringList cleanupSuccessfulBuildArtifacts(const QString &workRoot);
};
