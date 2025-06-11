project(scmi-test)
cmake_minimum_required(VERSION 3.10)

set (CMAKE_INSTALL_PREFIX "/usr/")
set(SOURCE_FILES_PERF test_scmi_perf.c)
set(SOURCE_FILES_POWER test_scmi_power.c)
set(SOURCE_FILES test_scmi.c)
if(DEFINED VENDOR_USCMI)
  set(SOURCE_FILES_USCMI test_uscmi.c)
endif()

add_definitions(-Dstrlcpy=g_strlcpy)

add_executable (test_scmi_perf ${SOURCE_FILES_PERF})
add_executable (test_scmi_power ${SOURCE_FILES_POWER})
add_executable (test_scmi ${SOURCE_FILES})
if(DEFINED VENDOR_USCMI)
  add_executable (test_uscmi ${SOURCE_FILES_USCMI})
endif()

target_link_libraries(test_scmi glib-2.0)
target_link_libraries(test_scmi_power glib-2.0)

install(TARGETS "test_scmi_perf" DESTINATION bin)
install(TARGETS "test_scmi_power" DESTINATION bin)
install(TARGETS "test_scmi" DESTINATION bin)

if(DEFINED VENDOR_USCMI)
  install(TARGETS "test_uscmi" DESTINATION bin)
endif()
