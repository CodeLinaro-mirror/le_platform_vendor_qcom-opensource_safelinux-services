/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Unit tests for the scm_user_intf driver (/dev/scmnode).
 * Covers INFO-service calls that read TrustZone state.
 *
 * Pass criteria: every ioctl() returns seconds.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/types.h>

#include <uapi/misc/scm_user_intf.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */
#define SCM_DEV_NODE       "/dev/scmnode"
#define SCM_HAND_SHAKE_IOCTL _IOWR('R', 10, struct scm_hand_shake)

/* ------------------------------------------------------------------ */
/* SVC / CMD IDs                                                       */
/* ------------------------------------------------------------------ */
#define SCM_SVC_INFO                 0x06
#define SCM_INFO_GET_FEAT_VERSION    0x03
#define SCM_INFO_GET_SECURE_STATE    0x04

/* ------------------------------------------------------------------ */
/* arginfo helpers                                                     */
/* ------------------------------------------------------------------ */
#define SCM_TYPE_VAL    0x0

#define ARGINFO_0           0u
#define ARGINFO_1(t0)      (1u | ((t0)<<4))

/* ------------------------------------------------------------------ */
/* Test framework                                                      */
/* ------------------------------------------------------------------ */
static int g_pass;
static int g_fail;

#define TEST_PASS(name) do { \
    printf("  PASS  %s\n", (name)); \
    g_pass++; \
} while (0)

#define TEST_FAIL(name, fmt, ...) do { \
    printf("  FAIL  %s : " fmt "\n", (name), ##__VA_ARGS__); \
    g_fail++; \
} while (0)

#define CHECK_TRUE(name, expr) do { \
    if (expr) TEST_PASS(name); \
    else      TEST_FAIL(name, "expected true: %s", #expr); \
} while (0)

#define IOCTL(name, fd, req) do { \
    int _r = ioctl((fd), SCM_HAND_SHAKE_IOCTL, &(req)); \
    if (!_r) { \
        TEST_PASS(name); \
    } else { \
        TEST_FAIL(name, "ioctl error with ret %d ", req); \
    } \
} while (0)

static int open_scmnode(void)
{
    int fd = open(SCM_DEV_NODE, O_RDWR);
    if (fd < 0)
        printf("  NOTE  Cannot open %s (errno=%d: %s) - "
               "ensure module is loaded and run as root.\n",
               SCM_DEV_NODE, errno, strerror(errno));
    return fd;
}

/* ------------------------------------------------------------------ */
/* Test01 - Test02: device node open / close                               */
/* ------------------------------------------------------------------ */

static void test01_open_valid_path(void)
{
    int fd = open(SCM_DEV_NODE, O_RDWR);
    CHECK_TRUE("Test01_open_valid_path", fd >= 0);
    if (fd >= 0) close(fd);
}

static void test02_double_open_and_close(void)
{
    int fd1 = open(SCM_DEV_NODE, O_RDWR);
    int fd2 = open(SCM_DEV_NODE, O_RDWR);
    CHECK_TRUE("Test02_double_open_both_succeed", fd1 >= 0 && fd2 >= 0);
    if (fd1 >= 0) close(fd1);
    if (fd2 >= 0) close(fd2);
}

/* ------------------------------------------------------------------ */
/* Test03 - Test04: INFO – query-only calls into TrustZone            */
/* ------------------------------------------------------------------ */

/*
 * GET_FEAT_VERSION: returns the TZ feature version.
 *   args[0] = feature_id  (0x0a = SCM_FEAT_LOG_ID)
 *   arginfo = PARAM_ID_1(VAL)
 */
static void test03_info_get_feat_version(void)
{
    int fd = open_scmnode();
    if (fd < 0) { g_fail++; return; }

    struct scm_hand_shake req;
    memset(&req, 0, sizeof(req));
    req.svc            = SCM_SVC_INFO;
    req.cmd            = SCM_INFO_GET_FEAT_VERSION;
    req.arginfo        = ARGINFO_1(SCM_TYPE_VAL);
    req.args_buffer[0] = 0x0a; /* SCM_FEAT_LOG_ID */

    IOCTL("Test03_info_get_feat_version", fd, req);
    printf("        version=0x%llx\n",
           (unsigned long long)req.qcom_scm_res[0]);
    close(fd);
}

/*
 * GET_SECURE_STATE: returns the TZ secure state bitmap.
 *   no args; arginfo = 0
 */
static void test04_info_get_secure_state(void)
{
    int fd = open_scmnode();
    if (fd < 0) { g_fail++; return; }

    struct scm_hand_shake req;
    memset(&req, 0, sizeof(req));
    req.svc     = SCM_SVC_INFO;
    req.cmd     = SCM_INFO_GET_SECURE_STATE;
    req.arginfo = ARGINFO_0;

    IOCTL("Test04_info_get_secure_state", fd, req);
    printf("        secure_state=0x%llx\n",
           (unsigned long long)req.qcom_scm_res[0]);
    close(fd);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("=== scm_user_intf driver unit tests (%s) ===\n", SCM_DEV_NODE);

    printf("-- Test01-Test02: device node open/close --\n");
    test01_open_valid_path();
    test02_double_open_and_close();

    printf("\n-- Test03-Test04: INFO --\n");
    test03_info_get_feat_version();
    test04_info_get_secure_state();

    printf("\n=== Results: %d passed, %d failed ===\n",
           g_pass, g_fail);

    return g_fail ? EXIT_FAILURE : EXIT_SUCCESS;
}
