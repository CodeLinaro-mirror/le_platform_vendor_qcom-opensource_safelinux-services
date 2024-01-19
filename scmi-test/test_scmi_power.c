/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <uapi/misc/qcom_uscmi.h>

void print_usage()
{
	printf("Usage:\n");
	printf("  ./test_scmi_power <on/off>\n");
	printf("  e.g. ./test_scmi_power on\n");
}

int request_power_operation(int fd, scmi_oper_ioctl_t *req, scmi_pwr_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_POWER;
	req->oper = op;

	return ioctl(fd, SCMI_IOCTL_PWR, req);
}

int main(int argc, char *argv[])
{
	char *node = "/dev/test_scmi_power";
	scmi_oper_ioctl_t request;
	int ret = 0;
	char oper[3];
	int fd;

	if (argc <2) {
		print_usage();
		return 0;
	}

	fd = open(node, O_RDWR);
	if (fd < 0) {
		printf("Failed to open the node %s\n", node);
		return -ENODEV;
	}

	strlcpy(oper, argv[1], 3);
	oper[0] = tolower(oper[0]);
	oper[1] = tolower(oper[1]);
	oper[2] = '\0';

	if (!strncmp(oper, "on", 2)) {
		if (request_power_operation(fd, &request, SCMI_PWR_ON)) {
			printf("Failed to power on\n");
			ret = -EINVAL;
			goto failure;
		}
	} else if (!strncmp(oper, "of", 2)) {
		if (request_power_operation(fd, &request, SCMI_PWR_OFF)) {
			printf("Failed to power off\n");
			ret = -EINVAL;
			goto failure;
		}
	} else {
		printf("Invalid power operation(%s) requested\n", oper);
		ret = -EINVAL;
		goto failure;
	}

	printf("power is turned %s successfully\n", oper);

	close(fd);

	return 0;

failure:
	close(fd);
	return ret;
}
