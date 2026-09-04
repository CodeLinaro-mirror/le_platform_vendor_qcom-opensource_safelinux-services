/* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include<stdio.h>
#include <lrmc.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <pil_devctls.h>

#define LRMC_CONNECTION_TIMEOUT 5
#define SUSPEND_MODE_MAX 108

enum PM_MODE {
	PM_MODE_DS = 1,     // Deep Sleep
	PM_MODE_S2R,        // Suspend 2 RAM
	PM_MODE_INVALID = 0xFFFFFFFF,    // Invalid
};

int get_suspend_mode() {
        int suspend_mode = PM_MODE_INVALID;
        char buffer[SUSPEND_MODE_MAX];

        FILE *file = fopen("/sys/power/mem_sleep", "r");
        if (file == NULL) {
                fprintf(stderr, "Error opening file\n");
                return -EINVAL;
        }

        if (fgets(buffer, sizeof(buffer), file) != NULL) {
                if (strstr(buffer, "[s2idle]") != NULL) {
                        suspend_mode = PM_MODE_S2R;
                } else if (strstr(buffer, "[deep]") != NULL) {
                        suspend_mode = PM_MODE_DS;
                } else {
                        fprintf(stderr, "Error: unknown suspend mode in /sys/power/mem_sleep\n");
                }
        }

        fclose(file);
        return suspend_mode;
}

size_t strlcpy(char *dst, const char *src, size_t size)
{

	size_t src_len = 0;

	while (src[src_len] != '\0') {
		src_len++;
	}

	if (size > 0) {
		size_t i;
		for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
			dst[i] = src[i];
		}
		dst[i] = '\0';
	}

	return src_len;
}

int main(int argc, char *argv[])
{
	lrmc_t md = NULL;
	lrmc_send_msg_t smsg;
	lrmc_recv_resp_t rmsg;
	char deepSleep_msg[] = "{class: deep_sleep, res: 1}";
	char quickboot_msg[] = "{class: deep_sleep, res: 0}";
	struct pil_qmp_msg msg_in;
	int suspend_mode = get_suspend_mode();

	if (suspend_mode != PM_MODE_DS)
		return 0;

	if (argc < 2 ) {
		fprintf(stderr, "should enter two argument\n");
		exit(EXIT_FAILURE);
	}

	memset(msg_in.msg, '\0', MAX_AOP_MSG_LEN);
	msg_in.mcmd = DCMD_AOP_QMP_SEND_MSG;
	if (!strcmp (argv[1], "enter")) {
		strlcpy(msg_in.msg, deepSleep_msg, sizeof(deepSleep_msg));
		msg_in.nbytes = sizeof(deepSleep_msg);
	}
	else if (!strcmp (argv[1], "exit")) {
		strlcpy(msg_in.msg, quickboot_msg, sizeof(quickboot_msg));
		msg_in.nbytes = sizeof(quickboot_msg);
	}
	else{
		fprintf(stderr, "wrong APSS-AOP event\n");
		exit(EXIT_FAILURE);
	}

	md = lrmc_connect(PIL_MQ_NAME, (void *)LRMC_CONNECTION_TIMEOUT, 0);
	if (md ==  NULL) {
		fprintf(stderr, "lrmc_connect failing");
		exit(EXIT_FAILURE);
	}

	smsg.msg = (char *)&msg_in;
	smsg.len = sizeof(struct pil_qmp_msg);
	rmsg.msg = (char *) calloc(1, sizeof(int));
	rmsg.len = sizeof(int);
	if (lrmc_send_sync(md, &smsg, &rmsg, 0)) {
		fprintf(stderr, "Fail to send msg.\n");
		lrmc_disconnect(md, 0);
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "send DeepSleep:%s message successfully to AOP\n", argv[1]);
	lrmc_disconnect(md, 0);

	return 0;
}

