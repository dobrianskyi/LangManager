# LangManager

Qt Widgets application for downloading, unpacking, compiling, and installing PHP CLI versions and Go toolchains on Linux.

## Current scope

- Download PHP source archives through `QtNetwork`.
- Extract `.tar.xz` archives through `libarchive`, not by shelling out to `tar`.
- Run the real PHP build with `QProcess` and show `configure`, `make`, and `make install` output in the GUI.
- Install into a selected base directory, for example `~/.local/php/8.4.20` or `/opt/php/8.4.20`.
- Mark a PHP version as ready when installation succeeds.
- Prioritize Symfony-friendly extensions at the top of the module list.
- Install or refresh selected PECL extensions, such as `redis` and `xdebug`, after PHP itself is installed.
- Build selected native dependency packages locally under the PHP prefix instead of installing them globally.
- Manage PHP branches such as `8.5`, `8.4`, `8.3`, `8.2`, `8.1`, `8.0`, and `7.4` from the first screen, with installed patch versions shown in each row.
- Mark one installed branch as the default PHP for the selected install base through managed symlinks.
- Select build profiles: `Minimal CLI`, `Symfony required`, `Symfony practical`, and `Full`.
- Run a preflight check before each build showing selected modules, local source dependencies, PECL extensions, and missing build tools.
- Install Composer and Symfony CLI locally into the selected install base instead of globally.
- Switch between PHP and Go from the left sidebar.
- Install official Linux Go toolchain archives into the selected install base.
- Mark one installed Go branch as the default through managed `go` and `gofmt` symlinks.

## Linux dependencies

The application itself needs Qt 6, CMake, Ninja, and libarchive development files.

On Arch Linux:

```sh
sudo pacman -S qt6-base cmake ninja libarchive
```

The PHP build step also needs a C compiler, `make`, and development libraries for the selected PHP modules. If a checked module is missing its system dependency, the GUI will show the failing `configure` or `make` output. Rare native dependencies such as ODBC, LDAP, SNMP, Tidy, Enchant, GMP, GD, and Imagick are visible in the module list but are not enabled by default.

Some native dependencies can be built locally by LangManager. For example, selecting `ODBC` or `PDO ODBC` downloads and builds `unixODBC` into:

```text
~/.local/php/8.4.20/deps/unixODBC/
```

LangManager then passes that local dependency to PHP's configure step through `PKG_CONFIG_PATH`, `CPPFLAGS`, `LDFLAGS`, `LD_LIBRARY_PATH`, and explicit configure prefixes. Nothing is installed into `/usr` or the system package database.

The same local dependency mechanism is used for:

- `PGSQL` and `PDO PGSQL`: builds PostgreSQL locally under `deps/postgresql` so PHP can use local `libpq` and `pg_config`.
- `GD`: builds `zlib`, `jpeg`, and `libpng` locally under `deps/` so PHP can compile the GD extension without installing system `*-dev` packages.
- `OpenSSL`, `cURL`, `XML`, `SQLite`, `mbstring`, `Intl`, and `ZIP`: builds local OpenSSL, curl, libxml2, SQLite, oniguruma, ICU, and libzip where those modules are selected.

The build profile menu controls module selection:

- `Minimal CLI`: a small CLI-oriented build with no PECL extensions.
- `Symfony required`: only Symfony's documented required extensions: Ctype, iconv, PCRE, Session, SimpleXML, and Tokenizer.
- `Symfony practical`: Symfony required extensions plus common local-development modules such as Intl, mbstring, OpenSSL, cURL, OPcache, PDO drivers, Redis, Xdebug, XML, ZIP, and GD.
- `Full`: selects every module visible in the UI. Some less-common modules may still need additional local dependency recipes.

Successful installs are registered only after PHP and all selected PECL extensions finish without errors. LangManager writes two metadata files:

```text
~/.local/php/8.4.20/.langmanager.json
~/.local/php/installed.json
```

The per-version manifest stores the PHP version, install path, selected module labels, configure flags, PECL extensions, local source packages, PHP binary path, install timestamp, and `ready` status. The shared registry lets the GUI list installed versions for the currently selected install base and show which modules belong to each version.

During configure, LangManager explicitly sets PHP's configuration locations to the selected prefix:

```text
--with-config-file-path=~/.local/php/<version>/lib
--with-config-file-scan-dir=~/.local/php/<version>/etc/conf.d
```

After PHP and PECL extension installation completes, it writes the generated `php.ini` to that exact config-file path so CLI PHP loads OPcache and selected PECL Zend extensions such as Xdebug immediately.

The Versions tab can mark a ready version as the default for the selected install base. LangManager creates or updates managed symlinks in:

```text
~/.local/bin/php
~/.local/bin/phpize
~/.local/bin/php-config
~/.local/bin/pecl
~/.local/bin/pear
```

Each link points to the selected version under `~/.local/php/<version>/bin/`. The app refuses to overwrite regular files, so an existing non-symlink `php` command must be moved manually. The shell will use this default PHP when the install base `bin` directory is earlier in `PATH` than other PHP installations. For the default user install target that usually means `~/.local/bin` must be in `PATH`.

If LangManager detects a default PHP symlink but the selected install base `bin` directory is missing from `PATH`, it shows a `Fix PATH` button on the Versions tab. The button updates the user's shell startup file (`~/.bashrc`, `~/.zshrc`, or `~/.profile` depending on the current shell) and prepends the bin directory to the current application process environment. A new terminal session will then resolve `php`.

The Versions tab is the main screen. It shows PHP branches such as `PHP 8.4`, and if a branch is installed it shows the concrete installed patch version in parentheses, for example `PHP 8.4 (8.4.20 installed)`. Available branches currently include `8.5`, `8.4`, `8.3`, `8.2`, `8.1`, `8.0`, and `7.4`. Installed branches are highlighted in green, the current default is bold, and the selected branch exposes actions to install, rebuild, set as default, or remove that installation. On startup the UI reads the actual `bin/php` symlink target so it shows which PHP command the shell will resolve.

Module choices are kept behind the Build options tab. They apply to the next install or rebuild of the selected PHP branch.

The Go page uses the same install-base and default-switching model. Go toolchains are installed under:

```text
~/.local/go/<version>/
```

The default Go version creates or updates managed symlinks in:

```text
~/.local/bin/go
~/.local/bin/gofmt
```

Installed Go versions are tracked in:

```text
~/.local/go/<version>/.langmanager-go.json
~/.local/go/installed.json
```

The Composer tab installs tool binaries under the selected install base:

```text
~/.local/tools/composer/composer.phar
~/.local/tools/symfony/symfony
~/.local/bin/composer
~/.local/bin/symfony
```

Composer is exposed through a wrapper that runs the managed PHP command from the same install base. Symfony CLI is exposed through a managed symlink. Both commands use the same `PATH` mechanism as PHP, so `~/.local/bin` must be available in the shell.

## CLI Companion

The repository includes a small Bash companion script that uses the same registry and symlink layout as the GUI. CMake copies it to the build directory as:

```text
./build/langmanager
```

Common commands:

```sh
./build/langmanager list
./build/langmanager current
./build/langmanager use 8.2
./build/langmanager use 8.2.30
eval "$(./build/langmanager env)"
```

By default it manages `~/.local`. Use `--base <path>` or `LANGMANAGER_INSTALL_BASE` for another install base.

Symfony `current` is already Symfony 8.0 and requires PHP 8.4+. Symfony 7.4 is still available as a maintained 7.x documentation branch. The app prioritizes the extensions Symfony lists as technical requirements: Ctype, iconv, PCRE, Session, SimpleXML, and Tokenizer. It also puts common Symfony project extensions near the top. The default checked profile is intentionally conservative: Symfony essentials plus common CLI/Web extensions. Optional database drivers, PECL extensions, and extensions with heavier system dependencies are unchecked until the user explicitly selects them.

## Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/LangManager
```

## Notes

Global `/usr/bin/php` switching is intentionally not implemented. User-level defaults are handled through install-base symlinks; system-level defaults should be managed by the distribution package manager or alternatives system.
