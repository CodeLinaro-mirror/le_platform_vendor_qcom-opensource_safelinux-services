set(UNITS
    check-inhibitors.service
    sleep-notify@.service
    sleep-bounds.target
    trigger-resume.service
    failure-resume.service
    sleep-apps.target
    sleep-drivers.target
)
install(FILES ${UNITS} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})

set(OVERRIDE_SUBDIRS
    sleep.target.d
)
install(DIRECTORY ${OVERRIDE_SUBDIRS} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})
