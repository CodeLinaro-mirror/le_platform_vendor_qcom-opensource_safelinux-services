/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

#include <systemd/sd-daemon.h>

#include "pm_client_lib.h"
#include "pm-internal.h"
#include "pm_server_lib.h"

void print_usage() {
	fprintf(stderr, SD_INFO "Usage: pm-notify name <pm command> [impose level]\n"
			"\t\timpose <impose level>");
}

int main(int argc, char* argv[])
{
	if (argc < 2) {
		print_usage();
		return -1;
	}

	char client_name[UNIX_PATH_MAX];
	char pm_cmd[PM_CMD_LEN];
	int impose_level = -1;

	strlcpy(client_name, argv[1], UNIX_PATH_MAX);
	strlcpy(pm_cmd, argv[2], PM_CMD_LEN);

	if (!strcmp(pm_cmd, IMPOSE_CMD)) {
		if (argc != 3) {
			fprintf(stderr, SD_ERR "impose command requires <impose level> argument\n\n");
			print_usage();
		}
		impose_level = atoi(argv[3]); // TODO change to strtol so we can detect errors
	}

	return pm_send_notif(client_name, pm_cmd, impose_level);
}
