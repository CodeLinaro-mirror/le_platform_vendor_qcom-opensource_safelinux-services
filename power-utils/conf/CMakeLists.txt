set(UNITS
    check-inhibitors.service
    sleep-notify@.service
    sleep-bounds.target
    trigger-resume.service
    failure-resume.service
    sleep-apps.target
    sleep-drivers.target
    make-pm-dir.service
)
install(FILES ${UNITS} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})

set(OVERRIDE_SUBDIRS
    sleep.target.d
)
install(DIRECTORY ${OVERRIDE_SUBDIRS} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})

install(DIRECTORY tmpfiles-early.d DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
