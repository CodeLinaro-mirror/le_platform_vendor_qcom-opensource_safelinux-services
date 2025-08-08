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
	fprintf(stderr, SD_INFO "Usage: pm-notify name...\n"
			"\t\t%s\n"
			"\t\t%s\n"
			"\t\t%s <impose level>\n"
			"\t\t%s <impose level> <lpm mode>\n",
			PM_ENTER_CMD,
			PM_EXIT_CMD,
			IMPOSE_CMD,
			IMPOSE_V2_CMD
		);
	exit(0 - EINVAL);
}

int main(int argc, char* argv[])
{
	int ret;
	if (argc < 3) {
		print_usage();
	}

	char client_name[UNIX_PATH_MAX];
	char pm_cmd[PM_CMD_LEN];
	int impose_level = -1;
	int lpm_mode = -1;

	strlcpy(client_name, argv[1], UNIX_PATH_MAX);
	strlcpy(pm_cmd, argv[2], PM_CMD_LEN);

	if (!strcmp(pm_cmd, IMPOSE_CMD)) {
		if (argc < 4) {
			fprintf(stderr, SD_ERR "impose command requires <impose level> argument\n\n");
			print_usage();
		}
		impose_level = atoi(argv[3]); // TODO change to strtol so we can detect errors
	}
	else if (!strcmp(pm_cmd, IMPOSE_V2_CMD)) {
		if (argc < 5) {
			fprintf(stderr, SD_ERR "impose command requires <impose level> argument\n\n");
			print_usage();
		}
		impose_level = atoi(argv[3]); // TODO change to strtol so we can detect errors
		lpm_mode = atoi(argv[4]);
		return pm_send_notif_v2(client_name, pm_cmd, impose_level, lpm_mode);
	}

	return pm_send_notif(client_name, pm_cmd, impose_level);
}
