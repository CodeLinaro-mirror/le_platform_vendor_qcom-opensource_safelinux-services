add_executable(check-inhibitors check-inhibitors.c)
target_link_libraries(check-inhibitors PRIVATE ${LIBSYSTEMD_LDFLAGS})
target_compile_options(check-inhibitors PRIVATE ${LIBSYSTEMD_CFLAGS})
install(TARGETS check-inhibitors RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(pm-notify  pm-notify.c)
install(TARGETS pm-notify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_library(pm-client SHARED pm-monitor.c)
set_target_properties(pm-client PROPERTIES VERSION ${PMCLIENT_FULL_VERSION} SOVERSION ${PMCLIENT_MAJOR_VERSION})
install(TARGETS pm-client LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
