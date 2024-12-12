/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include <systemd/sd-daemon.h>

#include "pm_client_lib.h"
#include "pm-internal.h"

/* hdl: pm_client_t type */
static void *monitor_pm_notifications(void *hdl)
{
	pm_client_t pm_hdl = (pm_client_t) hdl;
	struct pm_ops_s *pm_ops = pm_hdl->ops;

	int nread, addrplen, activity, ret;
	int conn_fd;
	struct pm_event pm_data;
	fd_set read_fds, temp_fds;
	struct sockaddr *addrp;
	addrplen = sizeof(addrp);

	FD_ZERO(&read_fds);
	FD_SET(pm_hdl->listen_fd, &read_fds);

	while (1) {
		fprintf(stderr, SD_INFO "waiting for pm notifications on %s\n", pm_hdl->pm_sock->sun_path);
		temp_fds = read_fds;
		activity = select(FD_SETSIZE, &temp_fds , NULL , NULL , NULL);
		if (activity < 0 && errno != EINTR) {
			fprintf(stderr, SD_NOTICE "select failed\n");
			continue;
		}

		if (FD_ISSET(pm_hdl->listen_fd, &temp_fds))
		{
			if (FD_ISSET(conn_fd, &temp_fds)) {
				continue;
			}

			do {
				conn_fd = accept(pm_hdl->listen_fd, (struct sockaddr *)&addrp, (socklen_t*)&addrplen);
			} while (conn_fd < 0 && errno == EINTR);
			if (conn_fd < 0) {
				continue;
			} else {
				FD_SET(conn_fd, &read_fds);
				continue;
			}
		} else if (FD_ISSET(conn_fd, &temp_fds)) {
			ioctl(conn_fd, FIONREAD, &nread);
			if (nread == 0) {
				close(conn_fd);
				FD_CLR(conn_fd, &read_fds);
			} else {
				ret = read(conn_fd, (unsigned char *)&pm_data, sizeof(pm_data));
				if (ret < 0) {
					fprintf(stderr, SD_ERR "Error recieving suspend event\n");
					send(conn_fd, NACK_RESPONSE, strlen(NACK_RESPONSE), 0);
				} else {
					fprintf(stderr, SD_NOTICE "Received message: %s %d\n", pm_data.cmd, pm_data.mode);
					if(!strcmp(pm_data.cmd, PM_ENTER_CMD)) {
						ret = pm_ops->pm_enter(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
					}
					else if(!strcmp(pm_data.cmd, PM_EXIT_CMD)) {
						ret = pm_ops->pm_exit(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
					}
					else if(!strcmp(pm_data.cmd, IMPOSE_CMD)) {
						ret = pm_ops->impose(pm_hdl->ctxt, pm_data.mode);
					}
					else {
						fprintf(stderr, SD_ERR "Received invalid pm cmd %s\n", pm_data.cmd);
						ret = -ENODEV;
					}
				}

				if (ret < 0) {
					fprintf(stderr, SD_ERR "responding with NACK to message: %s %d\n", pm_data.cmd, pm_data.mode);
					send(conn_fd, NACK_RESPONSE, strlen(NACK_RESPONSE), 0);
				}
				else {
					fprintf(stderr, SD_INFO "responding with ACK to message: %s %d\n", pm_data.cmd, pm_data.mode);
					send(conn_fd, ACK_RESPONSE, strlen(ACK_RESPONSE), 0);
				}
			}
		}
	}
}

static int setup_socket(const char *name, pm_client_t *hdl) {
	int fd_type = SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
	int listen_fd;
	int ret = 0;
	int saved_errno;


	(*hdl)->pm_sock = (struct sockaddr_un *) malloc(sizeof(struct sockaddr_un));
	if (!(*hdl)->pm_sock) {
		fprintf(stderr, SD_CRIT "pm_sock malloc failed\n");
		return -1;
	}

	memset((*hdl)->pm_sock, 0, sizeof(struct sockaddr_un));

	get_socket_path(name, (*hdl)->pm_sock->sun_path);
	listen_fd = socket(PF_UNIX, fd_type, 0);
	if (listen_fd < 0) {
		saved_errno = errno;
		fprintf(stderr, SD_ERR "Can't open monitor event socket, error : %s\n", strerror(saved_errno));
		return -saved_errno;
	}

	(*hdl)->listen_fd = listen_fd;

	(*hdl)->pm_sock->sun_family = AF_UNIX;

	ret = unlink((*hdl)->pm_sock->sun_path);
	if (ret != 0 && errno != ENOENT) {
		saved_errno = errno;
		fprintf(stderr, SD_ERR "Can't unlink old socket for the monitor, error : %s\n", strerror(saved_errno));
		goto close_listen_fd_and_ret;
	}

	ret = bind(listen_fd, (*hdl)->pm_sock, (socklen_t) sizeof(*((*hdl)->pm_sock)));
	if (ret) {
		saved_errno = errno;
		fprintf(stderr, SD_ERR "Can't bind the monitor socket fd to socket file, error: %s\n", strerror(saved_errno));
		goto unlink_close_listen_fd_and_ret;
	}

	ret = listen(listen_fd, NUM_LISTEN_QUEUE);
	if (ret < 0) {
		saved_errno = errno;
		fprintf(stderr, SD_ERR "Can't listen on monitor socket, error: %s\n", strerror(saved_errno));
		goto unlink_close_listen_fd_and_ret;
	}

	chmod((*hdl)->pm_sock->sun_path, 0666);

	return 0;

unlink_close_listen_fd_and_ret:
	unlink((*hdl)->pm_sock->sun_path);
close_listen_fd_and_ret:
	close(listen_fd);
	return -saved_errno;
}

int pm_register(const char *name, struct pm_ops_s *ops, void *ctxt, pm_client_t *hdl)
{
	int ret = 0;

	if (!ops) {
		fprintf(stderr, SD_CRIT "Error: pm_ops_s is NULL but should not be NULL\n");
		return -ENODEV;
	}

	if (!name) {
		fprintf(stderr, SD_ERR "Error: name input is NULL");
		return -ENODEV;
	}

	/* Create pm client handle */
	*hdl = (pm_client_t) malloc(sizeof(struct _pm_client_s));
	(*hdl)->ops = ops;
	(*hdl)->ctxt = ctxt;
	if (!(*hdl)) {
		fprintf(stderr, SD_CRIT "Error: pm_client_t hdl malloc failed\n");
		return -ENODEV;
	}

	ret = setup_socket(name, hdl);
	if (ret < 0) {
		fprintf(stderr, SD_ERR "Error setting up socket\n");
		return ret;
	}

	/* TODO try to ensure that hdl is not already used. Below code does not work, and reports hdl is already allocated in test driver if the driver statically allocates hdl
	if (hdl && *hdl) {
		fprintf(stderr, "Error: pm_client_t hdl is already allocated before pm_register(), but should not be\n");
		return -ENODEV;
	}
	*/

	ret = pthread_create(&((*hdl)->monitor_thread), NULL, &monitor_pm_notifications, (void *) *hdl);
	if (ret) {
		fprintf(stderr, SD_CRIT "Error creating monitor thread ret=%d : %s\n", ret, strerror(errno));
		return (0 - ret); // pthread_create returns a positive error number
	}

	fprintf(stderr, SD_INFO "Started thread to monitor notifications on %s\n", (*hdl)->pm_sock->sun_path);

	return 0;
}


int pm_deregister(pm_client_t hdl)
{
	if (!hdl) {
		fprintf(stderr, SD_ERR "Error: hdl is NULL, can not deregister NULL hdl\n");
		return -ENODEV;
	}

	/* free all resources from _pm_client_s struct that hdl points to */
	/* clean up and unlink socket */
	return 0;
}
