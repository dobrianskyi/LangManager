#pragma once

#include "PhpBuildController.h"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

int phpBuildJobs();
QProcessEnvironment phpBuildEnvironment(const PhpBuildRequest &request, const QString &installPath, const QString &excludedLocalPackage = {});
QString resolveLocalPackagePlaceholders(QString argument, const QList<LocalSourcePackage> &packages, const QString &installPath);
QStringList resolveLocalPackagePlaceholders(const QStringList &arguments, const QList<LocalSourcePackage> &packages, const QString &installPath);
