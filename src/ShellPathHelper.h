#pragma once

#include <QString>

QString phpSymlinkPath(const QString &installBasePath, const QString &defaultBinPath = {});
bool pathContainsDirectory(const QString &directory);
QString shellPathExpression(const QString &directory);
bool shellConfigAlreadyContainsPath(const QString &content, const QString &directory);
QString preferredShellConfigPath();
QString defaultSummaryText(const QString &installBasePath, const QString &defaultVersion, const QString &defaultBinPath);
