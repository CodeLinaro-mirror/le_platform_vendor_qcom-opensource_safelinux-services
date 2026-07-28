/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <stdio.h>
#include <stddef.h>
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
#include <poll.h>
#include <stdbool.h>

#include <systemd/sd-daemon.h>

#include "pm_client_lib.h"
#include "pm-internal.h"
#include "plat_vfio.h"

#define SUSPEND_STAT_SUCCESS_PATH "/sys/power/suspend_stats/success"

typedef struct {
	uint64_t entry_time;
	uint64_t exit_time;
	uint32_t success;
	uint32_t fail_cnt;
	uint32_t success_cnt;
} gearvm_ds_stats_struct;

/* Read sysfs "/sys/power/suspend_stats/success" file */
int read_sysfs_suspend_success_cnt(int *ptr_success_cnt)
{
	FILE *file = fopen(SUSPEND_STAT_SUCCESS_PATH, "r");

    if (file == NULL) {
		fprintf(stderr, SD_ERR "Error opening file '%s': %s\n", SUSPEND_STAT_SUCCESS_PATH, strerror(errno));
		return -1;
    }

	if (fscanf(file, "%d", ptr_success_cnt) != 1) {
		fprintf(stderr, SD_ERR "Error reading value from %s\n", SUSPEND_STAT_SUCCESS_PATH);
		fclose(file);
		return -1;
	}

	fclose(file);
	return 0;
}

static inline uint32_t read_reg32(volatile void* base, uint32_t offset)
{
    return *(volatile uint32_t *)((volatile uint8_t*)base + offset);
}

/* hdl: pm_client_t type */
static void *monitor_pm_notifications(void *hdl)
{
	pm_client_t pm_hdl = (pm_client_t) hdl;
	struct pm_ops_s *pm_ops = pm_hdl->ops;
	int activity, ret;
	socklen_t addrplen;
	int conn_fd;
	struct pm_event pm_data;
	struct pollfd temp_fds[MAX_LISTEN_QUEUE];
	struct sockaddr_un addrp;
	addrplen = sizeof(addrp);

	struct plat_vfio pvfio;
	volatile uchar *base_reg = NULL;
	const char* dev_name = "d0057000.umd_qc_pm";
	const char *expected_path = "/run/qcom_pm/ssctl-service.sock";
	volatile uint32_t gearvm_ds_success;

	memset(temp_fds,-1,sizeof(temp_fds));
	temp_fds[0].fd = pm_hdl->listen_fd;
	temp_fds[0].events = POLLIN;

	pthread_setname_np(pthread_self(), "pm_monitor");

	while (!pm_hdl->stop_thread) {
		fprintf(stderr, SD_INFO "waiting for pm notifications on %s\n", pm_hdl->pm_sock->sun_path);

		activity = poll(temp_fds, MAX_LISTEN_QUEUE , -1);
		if (activity < 0) {
			if (errno == EINTR) {
				fprintf(stderr, SD_NOTICE "poll interrupted by signal, retrying\n");
				continue;
			} else {
				fprintf(stderr, SD_ERR "poll failed: %s\n", strerror(errno));
				break;
			}
		}

		if(temp_fds[0].revents & POLLIN) {
			conn_fd = accept(pm_hdl->listen_fd, &addrp, &addrplen);
			if(conn_fd < 0)
				continue;

			int found_slot = -1;
			for(int i = 1; i < MAX_LISTEN_QUEUE; i++) {
				if(temp_fds[i].fd == -1) {
					temp_fds[i].fd = conn_fd;
					temp_fds[i].events = POLLIN;
					found_slot = i;
					break;
				}
			}

			if (found_slot == -1) {
				fprintf(stderr, SD_WARNING "Max connections reached, closing new connection on fd %d\n", conn_fd);
				close(conn_fd); // Close connection if no slot is available
			}
		}

		for(int i = 1; i < MAX_LISTEN_QUEUE; i++)
		{
			if(temp_fds[i].fd == -1)
				continue;
			if(temp_fds[i].revents & POLLIN) {
				ret = read(temp_fds[i].fd, (unsigned char *)&pm_data, sizeof(pm_data));
				if (ret < 0) { //if client get disconnected
					fprintf(stderr, SD_ERR "Error recieving suspend event\n");
					send(temp_fds[i].fd, NACK_RESPONSE, strlen(NACK_RESPONSE), 0);
					close(temp_fds[i].fd);
					temp_fds[i].fd = -1;
					temp_fds[i].revents = 0;
					continue;
				}
				fprintf(stderr, SD_NOTICE "Received message: %s %d %d\n", pm_data.cmd, pm_data.mode, pm_data.lpm_mode);
				if(!strcmp(pm_data.cmd, PM_ENTER_CMD)) {
					/* Update the prev success count here, this will be used to compare with current success count in pm_exit path */
					read_sysfs_suspend_success_cnt(&pm_hdl->prev_suspend_stat_success_cnt);
					ret = pm_ops->pm_enter(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
				}
				else if(!strcmp(pm_data.cmd, PM_EXIT_CMD)) {
					int cur_suspend_stat_success_cnt;
					read_sysfs_suspend_success_cnt(&cur_suspend_stat_success_cnt);

					/* Call pm_cancel if there is callback registered and this is rollback case else call always pm_exit */
					if ((pm_ops->pm_cancel != NULL) && (cur_suspend_stat_success_cnt == pm_hdl->prev_suspend_stat_success_cnt)) {
						ret = pm_ops->pm_cancel(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
					} else {
						/* Only if PM client is ssctl-service, check if rollback is initiated from GearVM */
						if ((pm_ops->pm_cancel != NULL) && (strcmp(pm_hdl->pm_sock->sun_path, expected_path) == 0)) {
							fprintf(stderr, SD_INFO "pm_exit cmd received on %s socket\n", pm_hdl->pm_sock->sun_path);
							/* suspend_stats success cnt incremented. Need to check GearVM suspend status now */
							ret = plat_vfio_device_init(dev_name, &pvfio);
							if (ret < 0) {
								fprintf(stderr, SD_ERR "Failed to initialize umd_firmware_vm device with ret: %d\n", ret);
								/* invoking pm_exit callback in case of failure */
								ret = pm_ops->pm_exit(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
								goto close_temp_fd_and_ret;
							}

							base_reg = (uchar *)plat_vfio_map_reg(&pvfio, 0);
							if ((base_reg == NULL) || (base_reg == MAP_FAILED)) {
								fprintf(stderr, SD_ERR "mmap Failed to address: %p\n", base_reg);
								plat_vfio_device_deinit(&pvfio);
								/* invoking pm_exit callback in case of failure */
								ret = pm_ops->pm_exit(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
								goto close_temp_fd_and_ret;
							}

							gearvm_ds_success = read_reg32(base_reg, offsetof(gearvm_ds_stats_struct, success));
							fprintf(stderr, SD_INFO "gearvm_ds_success: 0x%x!\n", gearvm_ds_success);
							if (plat_vfio_unmap_reg(&pvfio, base_reg, 0) < 0) {
								fprintf(stderr, SD_ERR "Failed to unmap VFIO register\n");
							}
							if (plat_vfio_device_deinit(&pvfio) < 0) {
								fprintf(stderr, SD_ERR "Failed to deinitialize VFIO device\n");
							}
							if (gearvm_ds_success != 0) {
								/* This is rollback from GearVM & hence pm_cancel will be invoked */
								ret = pm_ops->pm_cancel(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
							} else {
								ret = pm_ops->pm_exit(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
							}
						} else {
							ret = pm_ops->pm_exit(pm_hdl->ctxt, (enum PM_MODE) pm_data.mode);
						}
					}
				}

				else if(!strcmp(pm_data.cmd, IMPOSE_CMD)) {
					ret = pm_ops->impose(pm_hdl->ctxt, pm_data.mode);
				}
				else if(!strcmp(pm_data.cmd, IMPOSE_V2_CMD)) {
					ret = pm_ops->impose_v2(pm_hdl->ctxt, pm_data.mode, pm_data.lpm_mode);
				}
				else {
					fprintf(stderr, SD_ERR "Received invalid pm cmd %s\n", pm_data.cmd);
					ret = -ENODEV;
				}

close_temp_fd_and_ret:
				if (ret < 0) {
					fprintf(stderr, SD_ERR "responding with NACK to message: %s %d %d\n", pm_data.cmd, pm_data.mode, pm_data.lpm_mode);
					send(temp_fds[i].fd, NACK_RESPONSE, strlen(NACK_RESPONSE), 0);
				}
				else {
					fprintf(stderr, SD_INFO "responding with ACK to message: %s %d %d\n", pm_data.cmd, pm_data.mode, pm_data.lpm_mode);
					send(temp_fds[i].fd, ACK_RESPONSE, strlen(ACK_RESPONSE), 0);
				}
				close(temp_fds[i].fd);
				temp_fds[i].fd = -1;
				temp_fds[i].revents = 0;
			}
		}
	}
	return NULL;
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
	free((*hdl)->pm_sock);
	(*hdl)->pm_sock = NULL;
	return -saved_errno;
}

static void pm_sock_cleanup(pm_client_t hdl) {
	if(!hdl)
		return;

	if(hdl->listen_fd >= 0) // Check if listen_fd is valid
		close(hdl->listen_fd);

	if(hdl->pm_sock && hdl->pm_sock->sun_path) // Add null check for hdl->pm_sock
		unlink(hdl->pm_sock->sun_path);

	if(hdl->pm_sock)
		free(hdl->pm_sock);

	free(hdl);
}

int pm_register(const char *name, struct pm_ops_s *ops, void *ctxt, pm_client_t *hdl)
{
	int ret = 0;

	if (!ops) {
		fprintf(stderr, SD_CRIT "Error: pm_ops_s is NULL but should not be NULL\n");
		return -EINVAL;
	}

	if (!name) {
		fprintf(stderr, SD_ERR "Error: name input is NULL\n");
		return -EINVAL;
	}

	/* Create pm client handle */
	*hdl = (pm_client_t) malloc(sizeof(struct _pm_client_s));
	if (!(*hdl)) {
		fprintf(stderr, SD_CRIT "Error: pm_client_t hdl malloc failed\n");
		return -EINVAL;
	}
	memset(*hdl, 0, sizeof(struct _pm_client_s)); // Initialize allocated memory

	(*hdl)->ops = ops;
	(*hdl)->ctxt = ctxt;
	// (*hdl)->stop_thread is initialized to false by memset

	ret = setup_socket(name, hdl);
	if (ret < 0) {
		fprintf(stderr, SD_ERR "Error setting up socket\n");
		free(*hdl);
		*hdl = NULL;
		return ret;
	}

	/* TODO try to ensure that hdl is not already used. Below code does not work, and reports hdl is already allocated in test driver if the driver statically allocates hdl
	if (hdl && *hdl) {
		fprintf(stderr, "Error: pm_client_t hdl is already allocated before pm_register(), but should not be\n");
		return -ENODEV;
	}
	*/
	ret = pthread_create(&(*hdl)->monitor_thread, NULL, &monitor_pm_notifications, (void *) *hdl);
	if (ret) {
		fprintf(stderr, SD_CRIT "Error creating monitor thread ret=%d : %s\n", ret, strerror(errno));
		pm_sock_cleanup(*hdl);
		return (0 - ret);
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

	hdl->stop_thread = true;
	//wake poll by shutting down the socket
	shutdown(hdl->listen_fd, SHUT_RD);
	pthread_join(hdl->monitor_thread, NULL);

	pm_sock_cleanup(hdl);

	return 0;
}
