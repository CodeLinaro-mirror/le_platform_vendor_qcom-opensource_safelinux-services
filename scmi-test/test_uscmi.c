/* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <uapi/misc/vendor_uscmi.h>

#define MAX_INPUT_SIZE  128

struct uscmi_args {
    char dev_name[MAX_INPUT_SIZE];
    scmi_vendor_msg_t msg;
    char ioctl;
};

int parse_inputs(struct uscmi_args *args)
{
    char dev_name[MAX_INPUT_SIZE];
    char buf[MAX_INPUT_SIZE];
    char input[MAX_INPUT_SIZE];
    unsigned int size;
    printf("Enter device name: ");

    if (fgets(dev_name, MAX_INPUT_SIZE, stdin) == NULL) {
        printf("Failed to read device name\n");
        return -1;
    }

    dev_name[strcspn(dev_name, "\n")] = '\0'; // Remove newline character
    printf("Enter param_id: ");

    if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
        printf("Failed to read param_id\n");
        return -1;
    }

    args->msg.param_id = atoi(input);
    printf("Enter rx_size: ");

    if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
        printf("Failed to read rx_size\n");
        return -1;
    }

    args->msg.rx_size = atoi(input);
    printf("Enter buffer: ");

    if (fgets(buf, MAX_INPUT_SIZE, stdin) == NULL) {
        printf("Failed to read buffer\n");
        return -1;
    }

    buf[strcspn(buf, "\n")] = '\0'; // Remove newline character
    printf("Enter ioctl command: ");

    if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
        printf("Failed to read ioctl command\n");
        return -1;
    }

    args->ioctl = atoi(input);
    args->msg.tx_size = strlen(buf);
    args->msg.msg = strdup(buf); // Allocate memory and copy buffer
    size = strlen(dev_name) + strlen("/dev/") + 1;
    strlcpy(args->dev_name, "/dev/", size);
    strlcat(args->dev_name, dev_name, size);
    printf("dev_name : %s\n", args->dev_name);
    printf("param_id:%d tx_size:%d rx_size:%d buf:%s\n",
           args->msg.param_id, args->msg.tx_size,
           args->msg.rx_size, (char*)args->msg.msg);
    return 0;
}

int do_vendor_operation(struct uscmi_args *args)
{
    int fd, ret;
    fd = open(args->dev_name, O_RDWR);

    if (fd < 0) {
        printf("Failed to open the dev %s\n", args->dev_name);
        return -ENODEV;
    }

    switch (args->ioctl) {
        case 1:
            ret = ioctl(fd, SET_PARAM, &args->msg);
            break;

        case 2:
            ret = ioctl(fd, GET_PARAM, &args->msg);
            break;

        case 3:
            ret = ioctl(fd, START_ACTIVITY, &args->msg);
            break;

        case 4:
            ret = ioctl(fd, STOP_ACTIVITY, &args->msg);
            break;

        default:
            printf("Invalid ioctl command\n");
            ret = -EINVAL;
    }

    printf("ioctl ret:%d\n", ret);
    close(fd);
    return ret;
}

int main(int argc, char *argv[])
{
    struct uscmi_args args = {0};
    int ret;

    while (1) {
        if (parse_inputs(&args))
            return -1;

        ret = do_vendor_operation(&args);

        char cont[MAX_INPUT_SIZE];
        printf("Do you want to continue? (y/n): ");

        if (fgets(cont, sizeof(cont), stdin) == NULL) {
            printf("Failed to read input\n");
            return -1;
        }

        // Remove newline character if present
        cont[strcspn(cont, "\n")] = '\0';

        if (strcmp(cont, "y") != 0)
            break;
    }

    return ret;
}
