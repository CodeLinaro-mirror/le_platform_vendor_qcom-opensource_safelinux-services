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

void print_usage()
{
	printf("Usage:\n");
	printf("  ./test_scmi_perf <non-zero level>\n");
	printf("  e.g. ./test_scmi_perf 1\n");
}

int request_perf_operation(int fd, scmi_oper_ioctl_t *req, unsigned int level, scmi_prf_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_PERFORMANCE;
	req->oper = op;
	req->level = level;

	return ioctl(fd, SCMI_IOCTL_PRF, req);
}

int main(int argc, char *argv[])
{
	char *node = "/dev/test_scmi_perf";
	scmi_oper_ioctl_t request;
	int ret = 0;
	int level;
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

	level = atoi(argv[1]);
	if (level <= 0) {
		printf("Invalid Level %d\n", level);
		ret = -EINVAL;
		goto failure;
	}

	if (request_perf_operation(fd, &request, level, SCMI_PRF_LVL_SET)) {
		printf("Failed to set Level %d\n", level);
		ret = -EINVAL;
		goto failure;
	}

	printf("The Level is set to %d successfully\n", level);

	close(fd);

	return 0;

failure:
	close(fd);
	return ret;
}
