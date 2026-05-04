#pragma once

#include <QJsonObject>
#include <QString>

class GoDefaultSwitcher
{
public:
    static bool setDefault(const QString &installBasePath, const QJsonObject &manifest, QString *errorMessage);
};
