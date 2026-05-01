#pragma once

#include "PhpBuildController.h"

#include <QString>
#include <QStringList>

struct ModuleDefinition {
    QString label;
    QString flag;
    QString peclPackage;
    QString localPackageName;
    bool defaultChecked = true;
};

struct BuildProfileDefinition {
    QString key;
    QString label;
    QStringList moduleLabels;
};

QList<ModuleDefinition> allModuleDefinitions();
QList<BuildProfileDefinition> allBuildProfiles();
QStringList moduleLabelsForProfile(const QString &profileKey);
QStringList defaultModuleLabels();
QStringList selectedModuleLabelsFromArguments(const QStringList &arguments);
PhpBuildRequest createBuildRequest(const QString &version, const QString &installBasePath, const QStringList &selectedLabels);
