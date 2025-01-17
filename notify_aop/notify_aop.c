/* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include<stdio.h>
#include <lrmc.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>


#define PIL_MQ_NAME "/pil_service_mq"
#define MAX_AOP_MSG_LEN 96
#define DCMD_AOP_QMP_SEND_MSG     0x16

struct pil_qmp_msg {
    uint32_t mcmd;
    char     msg[MAX_AOP_MSG_LEN];
    int      nbytes;
};

lrmc_send_msg_t smsg;
lrmc_recv_resp_t rmsg;
lrmc_t md = NULL;

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
	if (argc < 2 ) {
		fprintf(stderr, "should enter two argument\n");
		exit(EXIT_FAILURE);
	}

	md = lrmc_connect(PIL_MQ_NAME, (void *)5, 0);
	if (md ==  NULL) {
		fprintf(stderr, "lrmc_connect failing");
		exit(EXIT_FAILURE);
	}

	struct pil_qmp_msg msg_in;  // Check pil_qmp_msg struct in pil_devctl.h
	memset(msg_in.msg, '\0', MAX_AOP_MSG_LEN);
	msg_in.mcmd = DCMD_AOP_QMP_SEND_MSG;
	if (!strcmp (argv[1], "enter")) {
		strlcpy(msg_in.msg, "{class: deep_sleep, res: 1}", sizeof("{class: deep_sleep, res: 1}"));
		msg_in.nbytes = sizeof("{class: deep_sleep, res: 1}");
	}
	else if (!strcmp (argv[1], "exit")) {
		strlcpy(msg_in.msg, "{class: deep_sleep, res: 0}", sizeof("{class: deep_sleep, res: 0}"));
		msg_in.nbytes = sizeof("{class: deep_sleep, res: 0}");
	}
	else
	{
		lrmc_disconnect(md, 0);
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

	fprintf(stderr, "send mesage successfully\n");
	lrmc_disconnect(md, 0);

	return 0;
}

