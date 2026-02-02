pkg_search_module(GLIB_2 REQUIRED "glib-2.0")

add_executable(check-inhibitors check-inhibitors.c)
target_link_libraries(check-inhibitors PRIVATE ${LIBSYSTEMD_LDFLAGS})
target_compile_options(check-inhibitors PRIVATE ${LIBSYSTEMD_CFLAGS})
install(TARGETS check-inhibitors RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_library(pm-client SHARED pm-monitor.c pm-common.c)
target_link_libraries(pm-client PRIVATE ${GLIB_2_LDFLAGS})
target_link_libraries(pm-client PRIVATE kiumd)
target_compile_options(pm-client PRIVATE ${GLIB_2_CFLAGS})
set_target_properties(pm-client PROPERTIES VERSION ${PM_FULL_VERSION} SOVERSION ${PM_MAJOR_VERSION})
install(TARGETS pm-client LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

add_library(pm-server SHARED pm-server.c pm-common.c)
target_link_libraries(pm-server PRIVATE ${GLIB_2_LDFLAGS})
target_compile_options(pm-server PRIVATE ${GLIB_2_CFLAGS})
set_target_properties(pm-server PROPERTIES VERSION ${PM_FULL_VERSION} SOVERSION ${PM_MAJOR_VERSION})
install(TARGETS pm-server LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

add_executable(pm-notify  pm-notify.c)
target_link_libraries(pm-notify PRIVATE ${GLIB_2_LDFLAGS} pm-server)
target_compile_options(pm-notify PRIVATE ${GLIB_2_CFLAGS})
install(TARGETS pm-notify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
