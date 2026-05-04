#include "ShellPathHelper.h"

#include <QDir>
#include <QFileInfo>

QString phpSymlinkPath(const QString &installBasePath, const QString &defaultBinPath)
{
    const QString binPath = defaultBinPath.isEmpty()
        ? QDir(installBasePath).filePath(QStringLiteral("bin"))
        : defaultBinPath;
    return QDir(binPath).filePath(QStringLiteral("php"));
}

bool pathContainsDirectory(const QString &directory)
{
    return qEnvironmentVariable("PATH")
        .split(':', Qt::SkipEmptyParts)
        .contains(QDir(directory).absolutePath());
}

QString shellPathExpression(const QString &directory)
{
    const QString homePath = QDir::homePath();
    const QString absoluteDirectory = QDir(directory).absolutePath();
    if (absoluteDirectory == homePath) {
        return QStringLiteral("$HOME");
    }
    if (absoluteDirectory.startsWith(homePath + QLatin1Char('/'))) {
        return QStringLiteral("$HOME/%1").arg(absoluteDirectory.mid(homePath.size() + 1));
    }
    return absoluteDirectory;
}

bool shellConfigAlreadyContainsPath(const QString &content, const QString &directory)
{
    const QString absoluteDirectory = QDir(directory).absolutePath();
    const QString expression = shellPathExpression(absoluteDirectory);
    QString tildeExpression;
    const QString homePath = QDir::homePath();
    if (absoluteDirectory.startsWith(homePath + QLatin1Char('/'))) {
        tildeExpression = QStringLiteral("~/%1").arg(absoluteDirectory.mid(homePath.size() + 1));
    }
    return content.contains(absoluteDirectory)
        || content.contains(expression)
        || (!tildeExpression.isEmpty() && content.contains(tildeExpression));
}

QString preferredShellConfigPath()
{
    const QString shell = qEnvironmentVariable("SHELL");
    if (shell.endsWith(QStringLiteral("zsh"))) {
        return QDir::home().filePath(QStringLiteral(".zshrc"));
    }
    if (shell.endsWith(QStringLiteral("bash"))) {
        return QDir::home().filePath(QStringLiteral(".bashrc"));
    }
    return QDir::home().filePath(QStringLiteral(".profile"));
}

QString defaultSummaryText(const QString &installBasePath, const QString &defaultVersion, const QString &defaultBinPath)
{
    if (defaultVersion.isEmpty()) {
        return QStringLiteral("Default: not selected");
    }

    const QString linkPath = phpSymlinkPath(installBasePath, defaultBinPath);
    const QString binPath = QFileInfo(linkPath).absolutePath();
    const QFileInfo linkInfo(linkPath);
    const QString targetPath = linkInfo.isSymLink() ? linkInfo.symLinkTarget() : QString();

    QString summary;
    if (targetPath.isEmpty()) {
        summary = QStringLiteral("Default: PHP %1 via %2").arg(defaultVersion, linkPath);
    } else {
        summary = QStringLiteral("Default: PHP %1 via %2 -> %3").arg(defaultVersion, linkPath, targetPath);
    }
    if (!pathContainsDirectory(binPath)) {
        summary.append(QStringLiteral("\nPATH missing: add %1 to use `php` in a terminal.").arg(binPath));
    }
    return summary;
}
