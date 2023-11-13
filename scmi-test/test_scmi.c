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
#include <uapi/misc/qcom_uscmi.h>

int request_perf_operation(int fd, scmi_oper_ioctl_t *req, unsigned int level, scmi_prf_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_PERFORMANCE;
	req->oper = op;
	req->level = level;
	strlcpy(req->name, "perf", NAME_LEN); /* domain name; must be same as mentioned in devicetree */

	return ioctl(fd, SCMI_IOCTL_PRF, req);
}

int request_power_operation(int fd, scmi_oper_ioctl_t *req, scmi_pwr_oper_t op)
{
        memset(req, 0, sizeof(*req));
        req->proto = SCMI_PROTO_POWER;
        req->oper = op;
	strlcpy(req->name, "power", NAME_LEN); /* domain name; must be same as mentioned in devicetree */

        return ioctl(fd, SCMI_IOCTL_PWR, req);
}

int request_reset_operation(int fd, scmi_oper_ioctl_t *req, const char *id, scmi_rst_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_RESET;
	req->oper = op;
	if (id)
		strlcpy(req->name, id, NAME_LEN);

	return ioctl(fd, SCMI_IOCTL_RST, req);
}

int main(int argc, char *argv[])
{
	char *node = "/dev/test_scmi";
	scmi_oper_ioctl_t request;
	int ret = 0;
	int level;
	int fd;

	fd = open(node, O_RDWR);
	if (fd < 0) {
		printf("Failed to open the node %s\n", node);
		return -ENODEV;
	}

	if (request_power_operation(fd, &request, SCMI_PWR_ON)) {
		printf("Failed to power on\n");
		ret = -EINVAL;
		goto failure;
	}
	printf("Power on successful\n");

	level = 10;
	if (request_perf_operation(fd, &request, level, SCMI_PRF_LVL_SET)) {
		printf("Failed to set Level %d\n", level);
		ret = -EINVAL;
		goto failure;
	}
	printf("The Level is set to %d successfully\n", level);

	level = 12;
	if (request_perf_operation(fd, &request, level, SCMI_PRF_LVL_SET)) {
		printf("Failed to set Level %d\n", level);
		ret = -EINVAL;
		goto failure;
	}
	printf("The Level is set to %d successfully\n", level);

	if (request_reset_operation(fd, &request, "core", SCMI_RST_RESET)) {
		printf("failed to perform reset\n");
		ret = -EINVAL;
		goto failure;
	}
	printf("Reset successful\n");

	if (request_power_operation(fd, &request, SCMI_PWR_OFF)) {
		printf("Failed to power off\n");
		ret = -EINVAL;
		goto failure;
	}
	printf("Power off successful\n");

	close(fd);

	return 0;

failure:
	close(fd);
	return ret;
}
