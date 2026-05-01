cmake_minimum_required(VERSION 3.20)

project(PHPManager VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)

add_executable(PHPManager
    src/main.cpp
    src/BuildCatalog.cpp
    src/BuildCatalog.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/PhpDefaultSwitcher.cpp
    src/PhpDefaultSwitcher.h
    src/PhpBuildController.cpp
    src/PhpBuildController.h
    src/ArchiveExtractor.cpp
    src/ArchiveExtractor.h
    resources.qrc
)

target_link_libraries(PHPManager
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        PkgConfig::LIBARCHIVE
)

add_custom_command(TARGET PHPManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/scripts/phpmanager
        ${CMAKE_BINARY_DIR}/phpmanager
)

install(TARGETS PHPManager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(PROGRAMS scripts/phpmanager
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES packaging/linux/phpmanager.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

foreach(icon_size 16 32 48 64 128 256 512)
    install(FILES assets/icons/phpmanager-${icon_size}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps
        RENAME phpmanager.png
    )
endforeach()

install(FILES assets/phpmanager-icon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME phpmanager.svg
)
