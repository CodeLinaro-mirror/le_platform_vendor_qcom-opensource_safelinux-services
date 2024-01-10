cmake_minimum_required(VERSION 3.16)

project(earlyServiceInfra)

include(GNUInstallDirs)

find_package(PkgConfig)
# Get systemd unitdir from systemd's pkg config file
pkg_search_module(SYSTEMD REQUIRED "systemd")
if (SYSTEMD_FOUND)
    pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd "systemd_system_unit_dir")
endif()

pkg_search_module(BOOTKPI_LOGGING REQUIRED "bootkpi-logging")

set(TARGET_NAME early-services.target)


install(FILES ${TARGET_NAME} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})
