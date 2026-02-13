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
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include <systemd/sd-daemon.h>
#include "bootkpi/logging.h"

#include "pm_client_lib.h"
#include "pm-internal.h"
#include "pm_server_lib.h"

static int trace_fd = -1;

void trace_log_init(void) {
	trace_fd = open("/sys/kernel/debug/tracing/instances/suspend_resume/trace_marker", O_WRONLY);
	if (trace_fd < 0) {
		fprintf(stderr, "trace_marker is not available\n");
	}
}

void trace_log_write(const char *format, ...) {
	if (trace_fd < 0) {
		fprintf(stderr, "trace_log_write: trace_fd not initialized\n");
		return;
	}

	char buffer[1024];
	va_list args;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	write(trace_fd, buffer, strlen(buffer));
}

void trace_log_close(void) {
	if (trace_fd >= 0) {
		close(trace_fd);
		trace_fd = -1;
	}
}

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

static int send_event(const char *name, const struct pm_event *pm_data)
{
	int fd_type = SOCK_STREAM | SOCK_CLOEXEC;
	int pm_notify_fd;
	int ret;
	char buf[MAX_BUF_LEN] = {'\0'};
	char pm_ops[PM_MAX_BUF_LEN] = {'\0'};
	char suspend_resume[PM_MAX_BUF_LEN] = {'\0'};
	static struct sockaddr_un pm_notify;
	struct timespec start, end;
	double elapsed;

	pm_notify_fd = socket(PF_UNIX, fd_type, 0);
	if (pm_notify_fd < 0) {
		fprintf(stderr, SD_ERR "Error opening socket ret=%d errno=%s\n", pm_notify_fd, strerror(errno));
		return -ENODEV;
	}

	memset(&pm_notify, 0, sizeof(pm_notify));
	get_socket_path(name, pm_notify.sun_path);
	fprintf(stderr, SD_INFO "got socket path %s\n", pm_notify.sun_path);
	pm_notify.sun_family = AF_UNIX;
	pm_log_init();

	ret = connect(pm_notify_fd, (struct sockaddr *)&pm_notify,sizeof(pm_notify));
	if ((ret < 0) && (errno == ENOENT)) {
		fprintf(stderr, SD_INFO "%s socket is missing!\n",pm_notify.sun_path);
		fprintf(stderr, SD_INFO "Returning SUCCESS to sleep-notify@%s.service\n",name);
		close(pm_notify_fd);
		return 0;
	}
	else if (ret != 0) {
		fprintf(stderr, SD_ERR "Error connecting to fd =%d errno=%s socket=%s len=%ld\n", pm_notify_fd, strerror(errno), pm_notify.sun_path, strlen(pm_notify.sun_path));
		close(pm_notify_fd);
		return -ENODEV;
	}

	if (pm_data->mode == PM_MODE_DS)
		strlcpy(pm_ops,"deepsleep" , PM_MAX_BUF_LEN);
	else if (pm_data->mode == PM_MODE_S2R)
		strlcpy(pm_ops,"str" , PM_MAX_BUF_LEN);

	if (!strcmp(pm_data->cmd, PM_ENTER_CMD))
		strlcpy(suspend_resume, "suspend" , PM_MAX_BUF_LEN);
	else if (!strcmp(pm_data->cmd, PM_EXIT_CMD))
		strlcpy(suspend_resume, "resume" , PM_MAX_BUF_LEN);

	fprintf(stderr, SD_INFO "%s: sending pm_data.cmd %s, pm_data.mode %s, pm_data.lpm_mode %d\n", name, suspend_resume, pm_ops, pm_data->lpm_mode);
	trace_log_init();

	clock_gettime(CLOCK_BOOTTIME, &start);
	send(pm_notify_fd, (const unsigned char *)pm_data, sizeof(*pm_data), 0);
	fprintf(stderr, SD_INFO "%s: sent pm_data.cmd %s, pm_data.mode %s, pm_data.lpm_mode %d\n", name, suspend_resume, pm_ops, pm_data->lpm_mode);
	recv(pm_notify_fd, (unsigned char *)buf, MAX_BUF_LEN, 0);
	close(pm_notify_fd);

	fprintf(stderr, SD_INFO "Received %s from client-%s\n", buf, name);

	clock_gettime(CLOCK_BOOTTIME, &end);
	elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	if (!strcmp(ACK_RESPONSE, buf)) {
		fprintf(stderr, SD_INFO "Received ACK from client %s\n", name);

		if  (!strcmp(pm_data->cmd, PM_ENTER_CMD) || !strcmp(pm_data->cmd, PM_EXIT_CMD)) {
			pm_log_line("total_time %.6f seconds:%s :%s :%s", elapsed, name, pm_ops , suspend_resume);
			trace_log_write("%s :%s :%s :%.6f seconds", name, pm_ops , suspend_resume, elapsed);
		}
		trace_log_close();
		return 0;
	}

	if (!strcmp(NACK_RESPONSE, buf)) {
		fprintf(stderr, SD_CRIT "Received NACK from client %s\n", name);
		trace_log_close();
		return -1;
	}

	fprintf(stderr, SD_CRIT "Received unknown response from client %s\n", name);
	trace_log_close();

	return -EAGAIN;
}

int pm_send_notif(char *name, char *pm_cmd, int mode)
{
	struct pm_event pm_data = {0};

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

	return send_event(name, &pm_data);
}

int pm_send_notif_v2(char *name, char *pm_cmd, int mode, int lpm_mode)
{
	struct pm_event pm_data = {0};

	if (!strcmp(pm_cmd, IMPOSE_CMD) || !strcmp(pm_cmd, IMPOSE_V2_CMD)) {
		pm_data.mode = mode; // mode is used as the impose level
		if (!strcmp(pm_cmd, IMPOSE_V2_CMD)) {
			pm_data.lpm_mode = lpm_mode;
		}
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

	return send_event(name, &pm_data);
}
