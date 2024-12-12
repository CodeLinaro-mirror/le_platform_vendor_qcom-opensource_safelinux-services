/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <systemd/sd-daemon.h>

#include "pm_client_lib.h"
#include "pm-internal.h"

#define PM_SOCK_DIR "/run/qcom_pm/"
#define PM_SOCK_SUFFIX ".sock"

int get_socket_path(const char *client_name, char *socket_path) {
	strlcpy(socket_path, PM_SOCK_DIR, UNIX_PATH_MAX);
	strlcat(socket_path, client_name, UNIX_PATH_MAX);
	strlcat(socket_path, PM_SOCK_SUFFIX, UNIX_PATH_MAX);
	fprintf(stderr, SD_INFO "socket path is %s\n", socket_path);
	return 0;
}

int get_suspend_mode() {
	int suspend_mode = PM_MODE_INVALID;
	char buffer[UNIX_PATH_MAX];

	FILE *file = fopen("/sys/power/mem_sleep", "r");
	if (file == NULL) {
		perror("Error opening file");
		return -ENODEV;
	}

	if (fgets(buffer, sizeof(buffer), file) != NULL) {
		if (strstr(buffer, "[s2idle]") != NULL) {
			suspend_mode = PM_MODE_S2R;
		} else if (strstr(buffer, "[deep]") != NULL) {
			suspend_mode = PM_MODE_DS;
		} else {
			fprintf(stderr, SD_ERR "Error: unknown suspend mode in /sys/power/mem_sleep\n");
		}
	}

	fclose(file);
	return suspend_mode;
}
