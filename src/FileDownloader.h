#pragma once

#include <QString>
#include <QUrl>

bool downloadToFile(const QUrl &url, const QString &path, QString *errorMessage);
