cmake_minimum_required(VERSION 3.20)

project(LangManager VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

add_executable(LangManager
    src/main.cpp
    src/BuildCatalog.cpp
    src/BuildCatalog.h
    src/BuildArtifactCleaner.cpp
    src/BuildArtifactCleaner.h
    src/CommandRunner.cpp
    src/CommandRunner.h
    src/FileDownloader.cpp
    src/FileDownloader.h
    src/GoDefaultSwitcher.cpp
    src/GoDefaultSwitcher.h
    src/GoInstallController.cpp
    src/GoInstallController.h
    src/GoVersionCatalog.cpp
    src/GoVersionCatalog.h
    src/PhpBuildEnvironment.cpp
    src/PhpBuildEnvironment.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/PhpIniRepair.cpp
    src/PhpIniRepair.h
    src/PhpToolInstaller.cpp
    src/PhpToolInstaller.h
    src/PhpVersionCatalog.cpp
    src/PhpVersionCatalog.h
    src/ShellPathHelper.cpp
    src/ShellPathHelper.h
    src/PhpDefaultSwitcher.cpp
    src/PhpDefaultSwitcher.h
    src/PhpBuildController.cpp
    src/PhpBuildController.h
    src/ArchiveExtractor.cpp
    src/ArchiveExtractor.h
    resources.qrc
)

target_link_libraries(LangManager
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        PkgConfig::LIBARCHIVE
)

add_custom_command(TARGET LangManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/scripts/langmanager
        ${CMAKE_BINARY_DIR}/langmanager
)

install(TARGETS LangManager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(PROGRAMS scripts/langmanager
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES packaging/linux/langmanager.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

foreach(icon_size 16 32 48 64 128 256 512)
    install(FILES assets/icons/langmanager-${icon_size}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps
        RENAME langmanager.png
    )
endforeach()

install(FILES assets/langmanager-icon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME langmanager.svg
)
