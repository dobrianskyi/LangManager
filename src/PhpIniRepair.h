#pragma once

#include <QJsonObject>
#include <QStringList>

class PhpIniRepair
{
public:
    static bool ensureForManifest(const QJsonObject &manifest, QStringList *logLines = nullptr);
};
