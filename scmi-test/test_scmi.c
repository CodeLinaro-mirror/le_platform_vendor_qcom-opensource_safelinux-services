/* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Refactor notes:
 * 1) Device node path is passed via argv (no hard-coded /dev/test_scmi)
 * 2) Domain/name strings for perf/power are passed via argv (no hard-coded "perf"/"power")
 * 3) main() provides three sub-commands: perf | power | reset
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <uapi/misc/qcom_uscmi.h>

/* ---- helper: usage ---- */
static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s <device_node> perf  <domain_name> set <level>\n"
        "  %s <device_node> power <domain_name> on|off\n"
        "  %s <device_node> reset <reset_id>    reset|assert|deassert\n"
        "\n"
        "Examples:\n"
        "  %s /dev/scmi_video  perf perf_video_bw set 2\n"
        "  %s /dev/scmi_usb2 power usb_transfer on\n"
        "  %s /dev/test_scmi reset core reset\n"
        "\n"
        "Notes:\n"
        "  - <domain_name> must match DT power-domain-names (e.g., perf_video_clk, perf_video_bw)\n"
        "  - <reset_id> corresponds to DT reset-names (e.g., core)\n",
        prog, prog, prog, prog, prog, prog);
}

/* ---- Validate and copy a domain / id name ------------------------------------------------ */
static int validate_and_copy_name(const char *src, char *dest,
                                  size_t dest_size, const char *type)
{
    if (!dest) {
        fprintf(stderr, "Error: destination buffer for %s name is NULL.\n", type);
        return -EINVAL;
    }

    if (dest_size == 0) {
        fprintf(stderr,
                "Error: destination size for %s name is zero.\n", type);
        return -EINVAL;
    }

    if (!src || src[0] == '\0') {
        fprintf(stderr, "Error: empty %s name.\n", type);
        return -EINVAL;
    }

    size_t len = strlcpy(dest, src, dest_size);
    if (len >= dest_size) {
        fprintf(stderr,
                "Error: %s name \"%s\" too long (%zu bytes, maximum is %zu).\n",
                type, src, len, dest_size - 1);
        return -ENAMETOOLONG;
    }

    return 0;
}

/* ---- request_perf_operation ------------------------------------------------ */
int request_perf_operation(int fd, scmi_oper_ioctl_t *req,
                           const char *domain, unsigned int level,
                           scmi_prf_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_PERFORMANCE;
    req->oper  = op;
    req->level = level;

    int ret = validate_and_copy_name(domain, req->name,
                                     sizeof(req->name), "performance");
    if (ret)
        return ret;


    ret = ioctl(fd, SCMI_IOCTL_PRF, req);
    if (ret == -1) {
        int err = errno;
        return -err;
    }

    return 0;
}

/* ---- request_power_operation ------------------------------------------------ */
int request_power_operation(int fd, scmi_oper_ioctl_t *req,
                            const char *domain, scmi_pwr_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_POWER;
    req->oper  = op;

    int ret = validate_and_copy_name(domain, req->name,
                                     sizeof(req->name), "power");
    if (ret)
        return ret;

    ret = ioctl(fd, SCMI_IOCTL_PWR, req);
    if (ret == -1) {
        int err = errno;
        return -err;
    }

    return 0;
}

/* ---- request_reset_operation ------------------------------------------------ */
int request_reset_operation(int fd, scmi_oper_ioctl_t *req,
                            const char *id, scmi_rst_oper_t op)
{
    memset(req, 0, sizeof(*req));
    req->proto = SCMI_PROTO_RESET;
    req->oper  = op;

    int ret = validate_and_copy_name(id, req->name,
                                     sizeof(req->name), "reset");
    if (ret)
        return ret;

    ret = ioctl(fd, SCMI_IOCTL_RST, req);
    if (ret == -1) {
        int err = errno;
        return -err;
    }

    return 0;
}

/* ---- main: three cases: perf / power / reset ---- */
int main(int argc, char *argv[])
{
    int ret = 0;
    int fd  = -1;
    scmi_oper_ioctl_t request;

    if (argc < 3) {
        print_usage(argv[0]);
        ret = -EINVAL;
        goto out;
    }

    const char *node = argv[1];
    const char *op   = argv[2];


    if (!node || node[0] == '\0' || !op || op[0] == '\0') {
        fprintf(stderr, "Error: empty device node or operation.\n");
        print_usage(argv[0]);
        ret = -EINVAL;
        goto out;
    }

    if (strncmp(node, "/dev/", 5) != 0) {
        fprintf(stderr,
                "Error: device node path must start with \"/dev/\".\n");
        print_usage(argv[0]);
        ret = -EINVAL;
        goto out;
    }

    fd = open(node, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open node '%s': %s\n",
                node, strerror(errno));
        ret = -errno;
        goto out;
    }

    /* ---- perf: <node> perf <domain> set <level> ---- */
    if (strcmp(op, "perf") == 0) {
        if (argc != 6) {
            print_usage(argv[0]);
            ret = -EINVAL;
            goto out;
        }
        const char *domain = argv[3];
        const char *action = argv[4];
        const char *level_str = argv[5];

        if (strcmp(action, "set") != 0) {
            fprintf(stderr, "Unsupported perf action '%s'. Only 'set' is supported.\n", action);
            ret = -EINVAL;
            goto out;
        }
        char *endp = NULL;
        errno = 0;
        unsigned long ul = strtoul(level_str, &endp, 0);

        if (*level_str == '\0' || (endp && *endp != '\0')) {
            fprintf(stderr,
                    "Invalid perf level '%s': not a valid integer.\n",
                    level_str);
            ret = -EINVAL;
            goto out;
        }

        if (errno == ERANGE) {
            fprintf(stderr,
                    "Invalid perf level '%s': value out of range.\n",
                    level_str);
            ret = -ERANGE;
            goto out;
        }

        #if ULONG_MAX > UINT_MAX
        if (ul > UINT_MAX) {
            fprintf(stderr,
                    "Invalid perf level '%s': must be ≤ %u.\n",
                    level_str, UINT_MAX);
            ret = -EOVERFLOW;
            goto out;
        }
        #endif
        unsigned int ulevel = (unsigned int)ul;


        ret = request_perf_operation(fd, &request, domain, ulevel, SCMI_PRF_LVL_SET);
        if (ret < 0) {
            fprintf(stderr, "Failed to set perf level %u for domain '%s': %s\n",
                    ulevel, domain, strerror(-ret));
            goto out;
        }

        printf("Perf level set to %u successfully on domain '%s'.\n", ulevel, domain);
    }
    /* ---- power: <node> power <domain> on|off ---- */
    else if (strcmp(op, "power") == 0) {
        if (argc != 5) {
            print_usage(argv[0]);
            ret = -EINVAL;
            goto out;
        }
        const char *domain = argv[3];
        const char *action = argv[4];

        scmi_pwr_oper_t pwr_op;
        if (strcmp(action, "on") == 0) {
            pwr_op = SCMI_PWR_ON;
        } else if (strcmp(action, "off") == 0) {
            pwr_op = SCMI_PWR_OFF;
        } else {
            fprintf(stderr, "Unsupported power action '%s'. Use 'on' or 'off'.\n", action);
            ret = -EINVAL;
            goto out;
        }

        ret = request_power_operation(fd, &request, domain, pwr_op);
        if (ret < 0) {
            fprintf(stderr, "Failed to power %s domain '%s': %s\n",
                    action, domain, strerror(-ret));
            goto out;
        }

        printf("Power %s successful on domain '%s'.\n", action, domain);
    }
    /* ---- reset: <node> reset <id> reset|assert|deassert ---- */
    else if (strcmp(op, "reset") == 0) {
        if (argc != 5) {
            print_usage(argv[0]);
            ret = -EINVAL;
            goto out;
        }
        const char *id = argv[3];
        const char *action = argv[4];

        scmi_rst_oper_t rst_op;
        if (strcmp(action, "reset") == 0) {
            rst_op = SCMI_RST_RESET;
        } else if (strcmp(action, "assert") == 0) {
            rst_op = SCMI_RST_ASSERT;
        } else if (strcmp(action, "deassert") == 0) {
            rst_op = SCMI_RST_DEASSERT;
        } else {
            fprintf(stderr, "Unsupported reset action '%s'. Use 'reset'|'assert'|'deassert'.\n", action);
            ret = -EINVAL;
            goto out;
        }

        ret = request_reset_operation(fd, &request, id, rst_op);
        if (ret < 0) {
            fprintf(stderr, "Failed to perform reset action '%s' on id '%s': %s\n",
                    action, id, strerror(-ret));
            goto out;
        }

        printf("Reset action '%s' successful on id '%s'.\n", action, id);
    }
    else {
        fprintf(stderr, "Unknown operation '%s'.\n", op);
        print_usage(argv[0]);
        ret = -EINVAL;
        goto out;
    }

out:
    if (fd >= 0)
        close(fd);
    return ret;
}
