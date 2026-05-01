#pragma once

#include <QString>

class ArchiveExtractor
{
public:
    static bool extractArchive(const QString &archivePath, const QString &destinationPath, QString *errorMessage);
    static bool extractTarXz(const QString &archivePath, const QString &destinationPath, QString *errorMessage);
};
