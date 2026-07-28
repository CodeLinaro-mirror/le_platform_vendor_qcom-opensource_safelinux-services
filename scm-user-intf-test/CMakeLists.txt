project(scm-user-intf-test)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_INSTALL_PREFIX "/usr/")

add_executable(test_scm_user_intf test_scm_user_intf.c)

target_link_libraries(test_scm_user_intf)

install(TARGETS "test_scm_user_intf" DESTINATION bin)
