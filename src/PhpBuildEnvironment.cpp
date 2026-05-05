#include "PhpBuildEnvironment.h"

#include <QDir>
#include <QStandardPaths>
#include <QThread>

namespace {

int phpMajorVersion(const QString &version)
{
    bool ok = false;
    const int major = version.section(QLatin1Char('.'), 0, 0).toInt(&ok);
    return ok ? major : 0;
}

} // namespace

int phpBuildJobs()
{
    bool ok = false;
    const int requestedJobs = qEnvironmentVariableIntValue("LANGMANAGER_BUILD_JOBS", &ok);
    if (ok && requestedJobs > 0) {
        return requestedJobs;
    }

    return qMax(1, QThread::idealThreadCount());
}

QProcessEnvironment phpBuildEnvironment(const PhpBuildRequest &request, const QString &installPath, const QString &excludedLocalPackage)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList pkgConfigPaths;
    QStringList includeFlags;
    QStringList libraryFlags;
    QStringList libraryPaths;
    QStringList binaryPaths;
    QStringList cmakePrefixPaths;
    QStringList cFlags;
    QStringList cxxFlags;

    for (const LocalSourcePackage &package : request.localPackages) {
        if (!excludedLocalPackage.isEmpty() && package.name == excludedLocalPackage) {
            continue;
        }

        const QString prefixPath = QDir(installPath).filePath(QStringLiteral("deps/%1").arg(package.name));
        cmakePrefixPaths << prefixPath;
        pkgConfigPaths << QDir(prefixPath).filePath(QStringLiteral("lib/pkgconfig"));
        pkgConfigPaths << QDir(prefixPath).filePath(QStringLiteral("lib64/pkgconfig"));
        includeFlags << QStringLiteral("-I%1").arg(QDir(prefixPath).filePath(QStringLiteral("include")));
        libraryFlags << QStringLiteral("-L%1").arg(QDir(prefixPath).filePath(QStringLiteral("lib")));
        libraryFlags << QStringLiteral("-L%1").arg(QDir(prefixPath).filePath(QStringLiteral("lib64")));
        libraryFlags << QStringLiteral("-Wl,-rpath,%1").arg(QDir(prefixPath).filePath(QStringLiteral("lib")));
        libraryFlags << QStringLiteral("-Wl,-rpath,%1").arg(QDir(prefixPath).filePath(QStringLiteral("lib64")));
        libraryPaths << QDir(prefixPath).filePath(QStringLiteral("lib"));
        libraryPaths << QDir(prefixPath).filePath(QStringLiteral("lib64"));
        binaryPaths << QDir(prefixPath).filePath(QStringLiteral("bin"));
    }

    if (!excludedLocalPackage.isEmpty()) {
        const QString prefixPath = QDir(installPath).filePath(QStringLiteral("deps/%1").arg(excludedLocalPackage));
        libraryFlags << QStringLiteral("-Wl,-rpath,%1").arg(QDir(prefixPath).filePath(QStringLiteral("lib")));
        libraryFlags << QStringLiteral("-Wl,-rpath,%1").arg(QDir(prefixPath).filePath(QStringLiteral("lib64")));
    }

    if (phpMajorVersion(request.version) == 7) {
        cFlags << QStringLiteral("-std=gnu99");
    }
    cFlags << QStringLiteral("-O0") << QStringLiteral("-g0") << QStringLiteral("-pipe");
    cxxFlags << QStringLiteral("-O0") << QStringLiteral("-g0") << QStringLiteral("-pipe");

    auto prepend = [&environment](const QString &name, const QStringList &values, const QString &separator) {
        QStringList merged = values;
        const QString existing = environment.value(name);
        if (!existing.isEmpty()) {
            merged << existing;
        }
        if (merged.isEmpty()) {
            return;
        }
        environment.insert(name, merged.join(separator));
    };

    prepend(QStringLiteral("PKG_CONFIG_PATH"), pkgConfigPaths, QStringLiteral(":"));
    prepend(QStringLiteral("CPPFLAGS"), includeFlags, QStringLiteral(" "));
    prepend(QStringLiteral("LDFLAGS"), libraryFlags, QStringLiteral(" "));
    prepend(QStringLiteral("LD_LIBRARY_PATH"), libraryPaths, QStringLiteral(":"));
    prepend(QStringLiteral("PATH"), binaryPaths, QStringLiteral(":"));
    prepend(QStringLiteral("CMAKE_PREFIX_PATH"), cmakePrefixPaths, QStringLiteral(":"));

    if (environment.value(QStringLiteral("LANGMANAGER_FAST_BUILD"), QStringLiteral("1")) != QStringLiteral("0")) {
        prepend(QStringLiteral("CFLAGS"), cFlags, QStringLiteral(" "));
        prepend(QStringLiteral("CXXFLAGS"), cxxFlags, QStringLiteral(" "));
    }

    if (environment.value(QStringLiteral("LANGMANAGER_USE_CCACHE"), QStringLiteral("1")) != QStringLiteral("0")) {
        const QString ccachePath = QStandardPaths::findExecutable(QStringLiteral("ccache"));
        if (!ccachePath.isEmpty()) {
            if (environment.value(QStringLiteral("CC")).isEmpty()) {
                environment.insert(QStringLiteral("CC"), QStringLiteral("ccache cc"));
            }
            if (environment.value(QStringLiteral("CXX")).isEmpty()) {
                environment.insert(QStringLiteral("CXX"), QStringLiteral("ccache c++"));
            }
        }
    }

    return environment;
}

QString resolveLocalPackagePlaceholders(QString argument, const QList<LocalSourcePackage> &packages, const QString &installPath)
{
    for (const LocalSourcePackage &package : packages) {
        const QString prefixPath = QDir(installPath).filePath(QStringLiteral("deps/%1").arg(package.name));
        argument.replace(QStringLiteral("${%1}").arg(package.name), prefixPath);
    }
    return argument;
}

QStringList resolveLocalPackagePlaceholders(const QStringList &arguments, const QList<LocalSourcePackage> &packages, const QString &installPath)
{
    QStringList resolved;
    for (const QString &argument : arguments) {
        resolved << resolveLocalPackagePlaceholders(argument, packages, installPath);
    }
    return resolved;
}
