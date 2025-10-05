/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __PM_INTERNAL_H
#define __PM_INTERNAL_H

#include <pthread.h>
#include "pm_client_lib.h"

#include <glib.h>

#ifndef FD_SETSIZE
#define FD_SETSIZE		1024
#endif

#define strlcpy g_strlcpy
#define strlcat g_strlcat

#define NUM_LISTEN_QUEUE	1024
#define MAX_BUF_LEN		12

#define UNIX_PATH_MAX 108
#define PM_CMD_LEN 50

#define PM_MAX_BUF_LEN		20

#define IMPOSE_CMD "impose"
#define PM_ENTER_CMD "pm-enter"
#define PM_EXIT_CMD "pm-exit"
#define ACK_RESPONSE "success"
#define NACK_RESPONSE "failed"
#define MAX_LISTEN_QUEUE 100

struct _pm_client_s {
	pthread_t monitor_thread;
	struct pm_ops_s *ops;
	struct sockaddr_un *pm_sock;
	int listen_fd;
	void *ctxt; // inserted as arg into callback functions
	volatile int stop_thread;
};

struct pm_event {
	char cmd[PM_CMD_LEN];
	int mode; // can be used for suspend mode or impose level
};

int get_socket_path(const char *client_name, char *socket_path);
int get_suspend_mode();

#endif
