#include "MainWindow.h"

#include "BuildCatalog.h"
#include "PhpBuildController.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSize>
#include <QTextStream>
#include <QTimer>

namespace {

QIcon createAppIcon()
{
    QIcon icon;
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-16.png"), QSize(16, 16));
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-32.png"), QSize(32, 32));
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-48.png"), QSize(48, 48));
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-64.png"), QSize(64, 64));
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-128.png"), QSize(128, 128));
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-256.png"), QSize(256, 256));
    icon.addFile(QStringLiteral(":/assets/icons/phpmanager-512.png"), QSize(512, 512));
    return icon;
}

int runHeadlessBuild(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PHPManager"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Headless PHPManager build runner"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("headless-build"), QStringLiteral("Run a build without opening the GUI.")});
    parser.addOption({QStringLiteral("php-version"), QStringLiteral("PHP version to build."), QStringLiteral("version"), QStringLiteral("8.4.20")});
    parser.addOption({QStringLiteral("install-base"), QStringLiteral("Install base directory."), QStringLiteral("path"), QDir::current().filePath(QStringLiteral(".phpmanager-headless"))});
    parser.addOption({QStringLiteral("modules"), QStringLiteral("Comma-separated module list. Use 'default' for the default GUI profile."), QStringLiteral("list"), QStringLiteral("default")});
    parser.addOption({QStringLiteral("dry-run"), QStringLiteral("Print the resolved build request and exit.")});
    parser.process(app);

    const QString version = parser.value(QStringLiteral("php-version"));
    const QString installBase = QDir(parser.value(QStringLiteral("install-base"))).absolutePath();
    const QStringList moduleArguments = parser.value(QStringLiteral("modules")).split(',', Qt::SkipEmptyParts);
    const QStringList selectedModules = selectedModuleLabelsFromArguments(moduleArguments);
    const PhpBuildRequest request = createBuildRequest(version, installBase, selectedModules);

    QTextStream out(stdout);
    QTextStream err(stderr);

    auto printRequest = [&]() {
        out << "PHP version: " << request.version << '\n';
        out << "Install base: " << request.installBasePath << '\n';
        out << "Source: " << request.sourceUrl.toString() << '\n';
        out << "Modules: " << request.selectedModules.join(QStringLiteral(", ")) << '\n';
        out << "Configure flags: " << request.configureFlags.join(QStringLiteral(" ")) << '\n';
        QStringList localPackageNames;
        for (const LocalSourcePackage &package : request.localPackages) {
            localPackageNames << package.name;
        }
        out << "Local packages: " << (localPackageNames.isEmpty() ? QStringLiteral("-") : localPackageNames.join(QStringLiteral(", "))) << '\n';
        out << "PECL: " << (request.peclExtensions.isEmpty() ? QStringLiteral("-") : request.peclExtensions.join(QStringLiteral(", "))) << '\n';
        out.flush();
    };

    printRequest();
    if (parser.isSet(QStringLiteral("dry-run"))) {
        return 0;
    }

    PhpBuildController controller;
    QObject::connect(&controller, &PhpBuildController::statusChanged, [&](const QString &status) {
        out << "[status] " << status << '\n';
        out.flush();
    });
    QObject::connect(&controller, &PhpBuildController::progressChanged, [&](int progress) {
        out << "[progress] " << progress << "%\n";
        out.flush();
    });
    QObject::connect(&controller, &PhpBuildController::logLine, [&](const QString &line) {
        out << line << '\n';
        out.flush();
    });
    QObject::connect(&controller, &PhpBuildController::finished, [&](bool success, const QString &message, const QString &) {
        if (success) {
            out << "[done] " << message << '\n';
            out.flush();
            app.exit(0);
        } else {
            err << "[failed] " << message << '\n';
            err.flush();
            app.exit(1);
        }
    });

    QTimer::singleShot(0, [&]() {
        controller.start(request);
    });
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--headless-build")) {
            return runHeadlessBuild(argc, argv);
        }
    }

    qunsetenv("BAMF_DESKTOP_FILE_HINT");
    qunsetenv("CHROME_DESKTOP");
    qunsetenv("DESKTOP_STARTUP_ID");
    qunsetenv("GIO_LAUNCHED_DESKTOP_FILE");
    qunsetenv("GIO_LAUNCHED_DESKTOP_FILE_PID");

    QApplication::setApplicationName(QStringLiteral("PHPManager"));
    QApplication::setOrganizationName(QStringLiteral("PHPManager"));
    QApplication::setDesktopFileName(QStringLiteral("phpmanager"));

    QApplication app(argc, argv);
    const QIcon icon = createAppIcon();
    QApplication::setWindowIcon(icon);

    MainWindow window;
    window.setWindowIcon(icon);
    window.show();

    return app.exec();
}
