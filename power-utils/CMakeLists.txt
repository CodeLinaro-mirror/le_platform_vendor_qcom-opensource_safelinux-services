cmake_minimum_required(VERSION 3.16)

project(power-utils)

# Allows us to use CMAKE_INSTALL_<dir> for installation destinations.
# See available variables at https://cmake.org/cmake/power-utils/lapower-utils/module/GNUInstallDirs.html
include(GNUInstallDirs)

find_package(PkgConfig)
# Get systemd unitdir from systemd's pkg config file
pkg_search_module(SYSTEMD REQUIRED "systemd")
pkg_search_module(LIBSYSTEMD REQUIRED "libsystemd")
pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd "systemdsystemunitdir")

set(SOURCE_DIR src)
set(CONF_DIR conf)

set(CHK_INHIB_EXEC check-inhibitors)
set(CHK_INHIB_SOURCES ${SOURCE_DIR}/check-inhibitors.c)
set(CHK_INHIB_SYSTEMD_UNIT ${CONF_DIR}/check-inhibitors.service)

add_executable(${CHK_INHIB_EXEC} ${CHK_INHIB_SOURCES})
target_link_libraries(${CHK_INHIB_EXEC} PRIVATE ${LIBSYSTEMD_LDFLAGS})
target_compile_options(${CHK_INHIB_EXEC} PRIVATE ${LIBSYSTEMD_CFLAGS})

install(FILES ${CHK_INHIB_SYSTEMD_UNIT} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})
install(TARGETS ${CHK_INHIB_EXEC} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
