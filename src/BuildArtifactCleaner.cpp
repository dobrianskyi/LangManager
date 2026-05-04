#include "BuildArtifactCleaner.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>

QStringList BuildArtifactCleaner::cleanupInstalledRuntimeArtifacts(const QString &installPath)
{
    if (installPath.isEmpty()) {
        return {};
    }

    qint64 removedEntries = 0;
    QStringList warnings;

    auto removePath = [&](const QString &path) {
        QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) {
            return;
        }

        bool removed = false;
        if (info.isDir() && !info.isSymLink()) {
            removed = QDir(path).removeRecursively();
        } else {
            removed = QFile::remove(path);
        }

        if (removed) {
            ++removedEntries;
        } else {
            warnings << path;
        }
    };

    auto removeInstallPath = [&](const QString &relativePath) {
        removePath(QDir(installPath).filePath(relativePath));
    };

    auto removeDirectChildrenExcept = [&](const QString &relativePath, const QStringList &keepNames) {
        QDir directory(QDir(installPath).filePath(relativePath));
        if (!directory.exists()) {
            return;
        }

        const QFileInfoList entries = directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            if (keepNames.contains(entry.fileName())) {
                continue;
            }
            removePath(entry.absoluteFilePath());
        }
    };

    auto removeNamedDirectoriesBelow = [&](const QString &rootPath, const QStringList &names) {
        QDir root(rootPath);
        if (!root.exists()) {
            return;
        }

        QStringList paths;
        QDirIterator iterator(rootPath, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            iterator.next();
            if (names.contains(iterator.fileName())) {
                paths << iterator.filePath();
            }
        }

        std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right) {
            return left.size() > right.size();
        });

        for (const QString &path : paths) {
            removePath(path);
        }
    };

    auto removeFilesBySuffixBelow = [&](const QString &rootPath, const QStringList &suffixes) {
        QDir root(rootPath);
        if (!root.exists()) {
            return;
        }

        QDirIterator iterator(rootPath, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            for (const QString &suffix : suffixes) {
                if (path.endsWith(suffix)) {
                    removePath(path);
                    break;
                }
            }
        }
    };

    removeDirectChildrenExcept(QStringLiteral("bin"), {QStringLiteral("php")});
    removeDirectChildrenExcept(QStringLiteral("lib/php"), {QStringLiteral("extensions")});
    removeInstallPath(QStringLiteral("include"));
    removeInstallPath(QStringLiteral("php"));
    removeInstallPath(QStringLiteral("etc/pear.conf"));

    const QString depsPath = QDir(installPath).filePath(QStringLiteral("deps"));
    removeNamedDirectoriesBelow(depsPath, {
        QStringLiteral("include"),
        QStringLiteral("bin"),
        QStringLiteral("pkgconfig"),
        QStringLiteral("cmake"),
        QStringLiteral("share"),
        QStringLiteral("man"),
        QStringLiteral("doc"),
        QStringLiteral("docs"),
        QStringLiteral("test"),
        QStringLiteral("tests"),
        QStringLiteral("__pycache__"),
    });
    removeFilesBySuffixBelow(depsPath, {
        QStringLiteral(".a"),
        QStringLiteral(".la"),
        QStringLiteral(".pc"),
        QStringLiteral(".cmake"),
    });

    QStringList logLines;
    logLines << QStringLiteral("Cleaned installed PHP runtime artifacts: %1 entries removed.").arg(removedEntries);
    for (const QString &path : warnings) {
        logLines << QStringLiteral("Warning: cannot remove install artifact: %1").arg(path);
    }
    return logLines;
}

QStringList BuildArtifactCleaner::cleanupSuccessfulBuildArtifacts(const QString &workRoot)
{
    if (workRoot.isEmpty()) {
        return {};
    }

    QDir workDirectory(workRoot);
    if (!workDirectory.exists()) {
        return {};
    }

    if (workDirectory.removeRecursively()) {
        return {QStringLiteral("Removed build workspace: %1").arg(workRoot)};
    }
    return {QStringLiteral("Warning: cannot remove build workspace: %1").arg(workRoot)};
}
