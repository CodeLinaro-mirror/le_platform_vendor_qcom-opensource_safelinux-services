cmake_minimum_required(VERSION 3.16)

project(power-utils)

# Allows us to use CMAKE_INSTALL_<dir> for installation destinations.
# See available variables at https://cmake.org/cmake/power-utils/lapower-utils/module/GNUInstallDirs.html
include(GNUInstallDirs)

include_directories(public)

find_package(PkgConfig)
# Get systemd unitdir from systemd's pkg config file
pkg_search_module(SYSTEMD REQUIRED "systemd")
pkg_search_module(LIBSYSTEMD REQUIRED "libsystemd")
pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd "systemdsystemunitdir")
pkg_get_variable(TMPFILES_DIR systemd "tmpfilesdir")

set(PM_MAJOR_VERSION 0)
set(PM_MINOR_VERSION 0)
set(PM_PATCH_VERSION 1)
set(PM_FULL_VERSION "${PM_MAJOR_VERSION}.${PM_MINOR_VERSION}.${PM_PATCH_VERSION}")
configure_file(pm-client.pc.in ${CMAKE_CURRENT_BINARY_DIR}/pm-client.pc)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pm-client.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

add_subdirectory(public)
add_subdirectory(src)
add_subdirectory(conf)
