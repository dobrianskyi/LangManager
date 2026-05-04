#pragma once

#include <QString>
#include <QStringList>

QString runToolCommand(const QString &program, const QStringList &arguments, int startTimeoutMs = 3000, int finishTimeoutMs = 5000);
QString compactToolStatus(const QString &toolName, const QString &rawOutput);
QString compactToolTooltip(const QString &rawOutput);
