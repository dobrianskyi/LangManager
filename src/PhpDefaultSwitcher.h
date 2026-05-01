#pragma once

#include <QJsonObject>
#include <QString>

class PhpDefaultSwitcher
{
public:
    static bool setDefault(const QString &installBasePath, const QJsonObject &manifest, QString *errorMessage);
};
