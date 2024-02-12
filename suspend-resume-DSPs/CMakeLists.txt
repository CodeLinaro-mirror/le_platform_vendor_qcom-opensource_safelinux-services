# Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

cmake_minimum_required(VERSION 3.16)

project(SuspendResumeDSPs)

include(GNUInstallDirs)

find_package(PkgConfig)
# Get systemd unitdir from systemd's pkg config file
pkg_check_modules(SYSTEMD REQUIRED "systemd")
pkg_get_variable(SYSTEMD_SYSTEM_UNIT_DIR systemd "systemd_system_unit_dir")


set(SUSPEND_DSPS_SYSTEMD_UNIT suspend-DSPs.service)
set(RESUME_DSPS_SYSTEMD_UNIT resume-DSPs.service)


install(FILES ${SUSPEND_DSPS_SYSTEMD_UNIT} ${RESUME_DSPS_SYSTEMD_UNIT} DESTINATION ${SYSTEMD_SYSTEM_UNIT_DIR})
