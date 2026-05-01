#include "BuildCatalog.h"

#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {

QString normalizedModuleKey(QString value)
{
    value = value.toLower();
    value.remove(QStringLiteral("(pecl)"));
    value.remove(QStringLiteral("symfony:"));
    value.remove(QRegularExpression(QStringLiteral("[^a-z0-9]+")));
    return value;
}

LocalSourcePackage sourcePackage(const QString &name,
                                 const QString &url,
                                 const QString &sourceDirectoryName,
                                 const QStringList &configureArguments = {},
                                 const QString &buildSystem = QStringLiteral("configure"),
                                 const QString &configureProgram = QStringLiteral("configure"))
{
    return LocalSourcePackage{
        name,
        QUrl(url),
        sourceDirectoryName,
        configureArguments,
        buildSystem,
        configureProgram,
    };
}

} // namespace

QList<ModuleDefinition> allModuleDefinitions()
{
    return {
        {QStringLiteral("Symfony: Ctype"), QStringLiteral("--enable-ctype"), {}, {}, true},
        {QStringLiteral("Symfony: Iconv"), QStringLiteral("--with-iconv"), {}, {}, true},
        {QStringLiteral("Symfony: PCRE JIT"), QStringLiteral("--with-pcre-jit"), {}, {}, true},
        {QStringLiteral("Symfony: Session"), QStringLiteral("--enable-session"), {}, {}, true},
        {QStringLiteral("Symfony: SimpleXML"), QStringLiteral("--enable-simplexml"), {}, {}, true},
        {QStringLiteral("Symfony: Tokenizer"), QStringLiteral("--enable-tokenizer"), {}, {}, true},
        {QStringLiteral("Intl"), QStringLiteral("--enable-intl"), {}, QStringLiteral("icu"), true},
        {QStringLiteral("mbstring"), QStringLiteral("--enable-mbstring"), {}, QStringLiteral("oniguruma"), true},
        {QStringLiteral("OpenSSL"), QStringLiteral("--with-openssl=${openssl}"), {}, QStringLiteral("openssl"), true},
        {QStringLiteral("cURL"), QStringLiteral("--with-curl=${curl}"), {}, QStringLiteral("curl"), true},
        {QStringLiteral("OPcache"), QStringLiteral("--enable-opcache"), {}, {}, true},
        {QStringLiteral("PDO"), QStringLiteral("--enable-pdo"), {}, {}, true},
        {QStringLiteral("PDO MySQL"), QStringLiteral("--with-pdo-mysql"), {}, {}, true},
        {QStringLiteral("MySQLi"), QStringLiteral("--with-mysqli"), {}, {}, true},
        {QStringLiteral("PDO PGSQL"), QStringLiteral("--with-pdo-pgsql=${postgresql}"), {}, QStringLiteral("postgresql"), false},
        {QStringLiteral("PGSQL"), QStringLiteral("--with-pgsql=${postgresql}"), {}, QStringLiteral("postgresql"), false},
        {QStringLiteral("PDO SQLite"), QStringLiteral("--with-pdo-sqlite=${sqlite}"), {}, QStringLiteral("sqlite"), true},
        {QStringLiteral("SQLite3"), QStringLiteral("--with-sqlite3=${sqlite}"), {}, QStringLiteral("sqlite"), true},
        {QStringLiteral("ZIP"), QStringLiteral("--with-zip=${libzip}"), {}, QStringLiteral("libzip"), true},
        {QStringLiteral("Redis (PECL)"), {}, QStringLiteral("redis"), {}, true},
        {QStringLiteral("Xdebug (PECL)"), {}, QStringLiteral("xdebug"), {}, true},
        {QStringLiteral("BCMath"), QStringLiteral("--enable-bcmath"), {}, {}, true},
        {QStringLiteral("Calendar"), QStringLiteral("--enable-calendar"), {}, {}, true},
        {QStringLiteral("Exif"), QStringLiteral("--enable-exif"), {}, {}, true},
        {QStringLiteral("Fileinfo"), QStringLiteral("--enable-fileinfo"), {}, {}, true},
        {QStringLiteral("FTP"), QStringLiteral("--enable-ftp"), {}, {}, false},
        {QStringLiteral("PCNTL"), QStringLiteral("--enable-pcntl"), {}, {}, false},
        {QStringLiteral("Phar"), QStringLiteral("--enable-phar"), {}, {}, true},
        {QStringLiteral("POSIX"), QStringLiteral("--enable-posix"), {}, {}, true},
        {QStringLiteral("Readline"), QStringLiteral("--with-readline"), {}, {}, false},
        {QStringLiteral("Shmop"), QStringLiteral("--enable-shmop"), {}, {}, false},
        {QStringLiteral("Soap"), QStringLiteral("--enable-soap"), {}, {}, false},
        {QStringLiteral("Sockets"), QStringLiteral("--enable-sockets"), {}, {}, true},
        {QStringLiteral("Sodium"), QStringLiteral("--with-sodium"), {}, {}, false},
        {QStringLiteral("SysV Msg"), QStringLiteral("--enable-sysvmsg"), {}, {}, false},
        {QStringLiteral("SysV Sem"), QStringLiteral("--enable-sysvsem"), {}, {}, false},
        {QStringLiteral("SysV Shm"), QStringLiteral("--enable-sysvshm"), {}, {}, false},
        {QStringLiteral("XML"), QStringLiteral("--enable-xml"), {}, QStringLiteral("libxml2"), true},
        {QStringLiteral("XMLReader"), QStringLiteral("--enable-xmlreader"), {}, QStringLiteral("libxml2"), true},
        {QStringLiteral("XMLWriter"), QStringLiteral("--enable-xmlwriter"), {}, QStringLiteral("libxml2"), true},
        {QStringLiteral("APCu (PECL)"), {}, QStringLiteral("apcu"), {}, false},
        {QStringLiteral("BZip2"), QStringLiteral("--with-bz2"), {}, {}, false},
        {QStringLiteral("DBA"), QStringLiteral("--enable-dba"), {}, {}, false},
        {QStringLiteral("Enchant"), QStringLiteral("--with-enchant"), {}, {}, false},
        {QStringLiteral("FFI"), QStringLiteral("--with-ffi"), {}, {}, false},
        {QStringLiteral("GD"), QStringLiteral("--enable-gd"), {}, QStringLiteral("gd"), true},
        {QStringLiteral("Gettext"), QStringLiteral("--with-gettext"), {}, {}, false},
        {QStringLiteral("GMP"), QStringLiteral("--with-gmp"), {}, {}, false},
        {QStringLiteral("Imagick (PECL)"), {}, QStringLiteral("imagick"), {}, false},
        {QStringLiteral("LDAP"), QStringLiteral("--with-ldap"), {}, {}, false},
        {QStringLiteral("Memcached (PECL)"), {}, QStringLiteral("memcached"), {}, false},
        {QStringLiteral("MongoDB (PECL)"), {}, QStringLiteral("mongodb"), {}, false},
        {QStringLiteral("ODBC"), QStringLiteral("--with-unixODBC=${unixODBC}"), {}, QStringLiteral("unixODBC"), false},
        {QStringLiteral("PDO ODBC"), QStringLiteral("--with-pdo-odbc=unixODBC,${unixODBC}"), {}, QStringLiteral("unixODBC"), false},
        {QStringLiteral("SNMP"), QStringLiteral("--with-snmp"), {}, {}, false},
        {QStringLiteral("Swoole (PECL)"), {}, QStringLiteral("swoole"), {}, false},
        {QStringLiteral("Tidy"), QStringLiteral("--with-tidy"), {}, {}, false},
        {QStringLiteral("XSL"), QStringLiteral("--with-xsl"), {}, {}, false},
        {QStringLiteral("YAML (PECL)"), {}, QStringLiteral("yaml"), {}, false},
    };
}

QList<BuildProfileDefinition> allBuildProfiles()
{
    const QStringList symfonyRequired = {
        QStringLiteral("Symfony: Ctype"),
        QStringLiteral("Symfony: Iconv"),
        QStringLiteral("Symfony: PCRE JIT"),
        QStringLiteral("Symfony: Session"),
        QStringLiteral("Symfony: SimpleXML"),
        QStringLiteral("Symfony: Tokenizer"),
    };

    const QStringList practicalSymfony = symfonyRequired + QStringList{
        QStringLiteral("Intl"),
        QStringLiteral("mbstring"),
        QStringLiteral("OpenSSL"),
        QStringLiteral("cURL"),
        QStringLiteral("OPcache"),
        QStringLiteral("PDO"),
        QStringLiteral("PDO MySQL"),
        QStringLiteral("MySQLi"),
        QStringLiteral("PDO SQLite"),
        QStringLiteral("SQLite3"),
        QStringLiteral("ZIP"),
        QStringLiteral("Redis (PECL)"),
        QStringLiteral("Xdebug (PECL)"),
        QStringLiteral("BCMath"),
        QStringLiteral("Calendar"),
        QStringLiteral("Exif"),
        QStringLiteral("Fileinfo"),
        QStringLiteral("Phar"),
        QStringLiteral("POSIX"),
        QStringLiteral("Sockets"),
        QStringLiteral("XML"),
        QStringLiteral("XMLReader"),
        QStringLiteral("XMLWriter"),
        QStringLiteral("GD"),
    };

    QStringList full;
    for (const ModuleDefinition &module : allModuleDefinitions()) {
        full << module.label;
    }

    return {
        {QStringLiteral("minimal"), QStringLiteral("Minimal CLI"), {
            QStringLiteral("Symfony: Ctype"),
            QStringLiteral("Symfony: PCRE JIT"),
            QStringLiteral("Phar"),
            QStringLiteral("POSIX"),
            QStringLiteral("Fileinfo"),
        }},
        {QStringLiteral("symfony"), QStringLiteral("Symfony required"), symfonyRequired},
        {QStringLiteral("symfony_full"), QStringLiteral("Symfony practical"), practicalSymfony},
        {QStringLiteral("full"), QStringLiteral("Full"), full},
    };
}

QStringList moduleLabelsForProfile(const QString &profileKey)
{
    for (const BuildProfileDefinition &profile : allBuildProfiles()) {
        if (profile.key == profileKey) {
            return profile.moduleLabels;
        }
    }
    return defaultModuleLabels();
}

QStringList defaultModuleLabels()
{
    return moduleLabelsForProfile(QStringLiteral("symfony_full"));
}

QStringList selectedModuleLabelsFromArguments(const QStringList &arguments)
{
    QSet<QString> requested;
    bool includeDefaults = arguments.isEmpty();
    for (const QString &argument : arguments) {
        for (const QString &part : argument.split(',', Qt::SkipEmptyParts)) {
            const QString trimmed = part.trimmed();
            if (trimmed.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0) {
                includeDefaults = true;
                continue;
            }
            bool matchedProfile = false;
            for (const BuildProfileDefinition &profile : allBuildProfiles()) {
                if (trimmed.compare(profile.key, Qt::CaseInsensitive) == 0
                    || trimmed.compare(profile.label, Qt::CaseInsensitive) == 0) {
                    for (const QString &label : profile.moduleLabels) {
                        requested.insert(normalizedModuleKey(label));
                    }
                    matchedProfile = true;
                    break;
                }
            }
            if (matchedProfile) {
                continue;
            }
            requested.insert(normalizedModuleKey(trimmed));
        }
    }

    QStringList labels;
    for (const ModuleDefinition &module : allModuleDefinitions()) {
        if (includeDefaults && module.defaultChecked) {
            labels << module.label;
            continue;
        }

        const QString label = normalizedModuleKey(module.label);
        const QString plainLabel = normalizedModuleKey(module.label.section(QStringLiteral(" ("), 0, 0));
        if (requested.contains(label) || requested.contains(plainLabel)) {
            labels << module.label;
        }
    }
    return labels;
}

PhpBuildRequest createBuildRequest(const QString &version, const QString &installBasePath, const QStringList &selectedLabels)
{
    const QSet<QString> selected = QSet<QString>(selectedLabels.begin(), selectedLabels.end());
    QSet<QString> localPackageNames;

    PhpBuildRequest request;
    request.version = version;
    request.sourceUrl = QUrl(QStringLiteral("https://www.php.net/distributions/php-%1.tar.xz").arg(version));
    request.installBasePath = installBasePath;

    for (const ModuleDefinition &module : allModuleDefinitions()) {
        if (!selected.contains(module.label)) {
            continue;
        }

        request.selectedModules << module.label;
        if (!module.flag.isEmpty()) {
            request.configureFlags << module.flag;
            if (module.localPackageName == QStringLiteral("gd")) {
                request.configureFlags << QStringLiteral("--with-jpeg");
            }
        }
        if (!module.peclPackage.isEmpty()) {
            request.peclExtensions << module.peclPackage;
        }
        if (!module.localPackageName.isEmpty()) {
            localPackageNames.insert(module.localPackageName);
        }
    }

    auto addPackageOnce = [&request](const LocalSourcePackage &package) {
        for (const LocalSourcePackage &existing : request.localPackages) {
            if (existing.name == package.name) {
                return;
            }
        }
        request.localPackages << package;
    };

    if (localPackageNames.contains(QStringLiteral("gd"))
        || localPackageNames.contains(QStringLiteral("libzip"))
        || localPackageNames.contains(QStringLiteral("libxml2"))
        || localPackageNames.contains(QStringLiteral("curl"))) {
        addPackageOnce(sourcePackage(QStringLiteral("zlib"), QStringLiteral("https://zlib.net/fossils/zlib-1.3.2.tar.gz"), QStringLiteral("zlib-1.3.2")));
    }
    if (localPackageNames.contains(QStringLiteral("gd"))) {
        addPackageOnce(sourcePackage(QStringLiteral("jpeg"), QStringLiteral("https://www.ijg.org/files/jpegsrc.v10.tar.gz"), QStringLiteral("jpeg-10")));
        addPackageOnce(sourcePackage(QStringLiteral("libpng"), QStringLiteral("https://downloads.sourceforge.net/project/libpng/libpng16/1.6.58/libpng-1.6.58.tar.gz"), QStringLiteral("libpng-1.6.58")));
    }
    if (localPackageNames.contains(QStringLiteral("openssl"))
        || localPackageNames.contains(QStringLiteral("curl"))) {
        addPackageOnce(sourcePackage(QStringLiteral("openssl"), QStringLiteral("https://www.openssl.org/source/openssl-3.5.4.tar.gz"), QStringLiteral("openssl-3.5.4"), {QStringLiteral("no-tests")}, QStringLiteral("configure"), QStringLiteral("config")));
    }
    if (localPackageNames.contains(QStringLiteral("curl"))) {
        addPackageOnce(sourcePackage(QStringLiteral("curl"), QStringLiteral("https://curl.se/download/curl-8.17.0.tar.xz"), QStringLiteral("curl-8.17.0"), {
            QStringLiteral("--with-openssl=${openssl}"),
            QStringLiteral("--with-zlib=${zlib}"),
            QStringLiteral("--disable-ldap"),
            QStringLiteral("--disable-ldaps"),
        }));
    }
    if (localPackageNames.contains(QStringLiteral("libxml2"))) {
        addPackageOnce(sourcePackage(QStringLiteral("libxml2"), QStringLiteral("https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.1.tar.xz"), QStringLiteral("libxml2-2.15.1"), {
            QStringLiteral("--with-zlib=${zlib}"),
            QStringLiteral("--without-python"),
        }));
    }
    if (localPackageNames.contains(QStringLiteral("sqlite"))) {
        addPackageOnce(sourcePackage(QStringLiteral("sqlite"), QStringLiteral("https://www.sqlite.org/2025/sqlite-autoconf-3510000.tar.gz"), QStringLiteral("sqlite-autoconf-3510000")));
    }
    if (localPackageNames.contains(QStringLiteral("oniguruma"))) {
        addPackageOnce(sourcePackage(QStringLiteral("oniguruma"), QStringLiteral("https://github.com/kkos/oniguruma/releases/download/v6.9.10/onig-6.9.10.tar.gz"), QStringLiteral("onig-6.9.10")));
    }
    if (localPackageNames.contains(QStringLiteral("icu"))) {
        addPackageOnce(sourcePackage(QStringLiteral("icu"), QStringLiteral("https://github.com/unicode-org/icu/releases/download/release-77-1/icu4c-77_1-src.tgz"), QStringLiteral("icu/source"), {
            QStringLiteral("--disable-samples"),
            QStringLiteral("--disable-tests"),
        }));
    }
    if (localPackageNames.contains(QStringLiteral("libzip"))) {
        addPackageOnce(sourcePackage(QStringLiteral("libzip"), QStringLiteral("https://libzip.org/download/libzip-1.11.4.tar.xz"), QStringLiteral("libzip-1.11.4"), {
            QStringLiteral("-DENABLE_COMMONCRYPTO=OFF"),
            QStringLiteral("-DENABLE_GNUTLS=OFF"),
            QStringLiteral("-DENABLE_MBEDTLS=OFF"),
            QStringLiteral("-DENABLE_OPENSSL=OFF"),
            QStringLiteral("-DENABLE_BZIP2=OFF"),
            QStringLiteral("-DENABLE_LZMA=OFF"),
            QStringLiteral("-DENABLE_ZSTD=OFF"),
        }, QStringLiteral("cmake")));
    }
    if (localPackageNames.contains(QStringLiteral("postgresql"))) {
        addPackageOnce(sourcePackage(QStringLiteral("postgresql"), QStringLiteral("https://ftp.postgresql.org/pub/source/v17.9/postgresql-17.9.tar.gz"), QStringLiteral("postgresql-17.9"), {
            QStringLiteral("--without-readline"),
            QStringLiteral("--without-zlib"),
        }));
    }
    if (localPackageNames.contains(QStringLiteral("unixODBC"))) {
        addPackageOnce(sourcePackage(QStringLiteral("unixODBC"), QStringLiteral("https://www.unixodbc.org/unixODBC-2.3.12.tar.gz"), QStringLiteral("unixODBC-2.3.12"), {
            QStringLiteral("--disable-gui"),
            QStringLiteral("--enable-static"),
            QStringLiteral("--enable-shared"),
        }));
    }

    return request;
}
