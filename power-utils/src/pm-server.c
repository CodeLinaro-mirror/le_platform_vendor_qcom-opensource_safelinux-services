/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <systemd/sd-daemon.h>

#include "pm_client_lib.h"
#include "pm-internal.h"
#include "pm_server_lib.h"

/* pm_cmd is either suspend or resume
 *
 * prepend the appropriate prefix depending on the current Linux suspend mode
 *
 * /sys/power/mem_sleep:
 * [deep] -> ds_suspend / ds_resume
 * [s2idle] -> suspend / resume
 */
static int set_suspend_pm_data(struct pm_event *pm_data) {
	int suspend_mode = get_suspend_mode();

	if (suspend_mode == PM_MODE_INVALID || suspend_mode < 0) {
			fprintf(stderr, SD_ERR "Error: invalid suspend mode\n");
			return -ENODEV;
	}

	pm_data->mode = suspend_mode;

	return 0;
}

int pm_send_notif(char *name, char *pm_cmd, int mode)
{
	int fd_type = SOCK_STREAM | SOCK_CLOEXEC;
	int pm_notify_fd;
	char buf[MAX_BUF_LEN] = {'\0'};
	char buffer[256];
	static struct sockaddr_un pm_notify;
	struct pm_event pm_data;

	if (!strcmp(pm_cmd, IMPOSE_CMD)) {
		pm_data.mode = mode; // mode is used as the impose level
	}
	else if (!strcmp(pm_cmd, PM_ENTER_CMD) || !strcmp(pm_cmd, PM_EXIT_CMD)) {
		set_suspend_pm_data(&pm_data);
	}
	else {
		// pm_cmd is invalid
		fprintf(stderr, SD_ERR "Error: invalid pm_cmd %s\n", pm_cmd);
		return -ENODEV;
	}

	strlcpy(pm_data.cmd, pm_cmd, PM_CMD_LEN);

	pm_notify_fd = socket(PF_UNIX, fd_type, 0);
	if (pm_notify_fd < 0) {
		fprintf(stderr, SD_ERR "Error opening socket ret=%d errno=%s\n", pm_notify_fd, strerror(errno));
		return -ENODEV;
	}

	memset(&pm_notify, 0, sizeof(pm_notify));
	get_socket_path(name, pm_notify.sun_path);
	fprintf(stderr, SD_INFO "got socket path %s\n", pm_notify.sun_path);
	pm_notify.sun_family = AF_UNIX;

	if ((connect(pm_notify_fd, (struct sockaddr *)&pm_notify,
				sizeof(pm_notify))) != 0) {
		fprintf(stderr, SD_ERR "Error connecting to fd =%d errno=%s socket=%s len=%ld\n", pm_notify_fd, strerror(errno), pm_notify.sun_path, strlen(pm_notify.sun_path));
		close(pm_notify_fd);
		return -ENODEV;
	}

	fprintf(stderr, SD_INFO "sending pm_data.cmd %s, pm_data.mode %d\n", pm_data.cmd, pm_data.mode);

	send(pm_notify_fd, (unsigned char *)&pm_data, sizeof(pm_data), 0);
	fprintf(stderr, SD_INFO "sent pm_data.cmd %s, pm_data.mode %d\n", pm_data.cmd, pm_data.mode);
	recv(pm_notify_fd, (unsigned char *)buf, MAX_BUF_LEN, 0);
	close(pm_notify_fd);

	fprintf(stderr, SD_INFO "Received %s from client-%s\n", buf,name);

	if (!strcmp(ACK_RESPONSE, buf)) {
		fprintf(stderr, SD_INFO "Received ACK from client-%s\n",name);
		return 0;
	}

	if (!strcmp(NACK_RESPONSE, buf)) {
		fprintf(stderr, SD_CRIT "Received NACK from client-%s\n",name);
		return -1;
	}

	fprintf(stderr, SD_CRIT "Received unknown response from client\n");

	return -EAGAIN;
}
