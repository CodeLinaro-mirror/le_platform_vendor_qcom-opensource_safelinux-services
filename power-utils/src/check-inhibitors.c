/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

// This file has code copied from https://www.freedesktop.org/software/systemd/man/latest/sd_bus_message_enter_container.html# which is licensed as below:

// /* SPDX-License-Identifier: MIT-0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <systemd/sd-bus.h>

// logind dbus macros
#define LOGIND_DEST "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_INTERFACE "org.freedesktop.login1.Manager"
#define LIST_INHIBITORS_METHOD "ListInhibitors"
#define LIST_INHIBITORS_METHOD_SIGNATURE "(ssssuu)"

#define _cleanup_(x) __attribute__((__cleanup__(x)))

/*
 * is_sleep_delay_inhibitor
 *
 * what: list of what operations the inhibitor lock interacts with. List tokens separated by colons.
 * mode: mode of inhibition. Either block or delay.
 *
 * return 1 if mode is "delay" and what contains "sleep"
 * else return 0
 */
int is_sleep_delay_inhibitor(const char *what, const char *mode) {
    int r;

    if (strcmp(mode, "delay")) {
        // mode is not "delay"
        return 0;
    }

    if (!strstr(what, "sleep")) {
        // "sleep" is not found in what
        return 0;
    }

    return 1;
}

/*
 * check_sleep_delay_inhibitors
 *
 * bus_conn: dbus system connection
 *
 * return negative errno if error
 * else return number of sleep delay inhibitor locks currently held
 */
int32_t check_sleep_delay_inhibitors(sd_bus *bus_conn) {
    _cleanup_(sd_bus_error_free) sd_bus_error ret_error = SD_BUS_ERROR_NULL;
    _cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
    int32_t r;
    uint32_t num_sleep_delay_inhibitors = 0;
    const char *what, *who, *why, *mode;
    uint32_t uid, pid;

    syslog(LOG_DEBUG, "Checking if any processes still hold sleep delay inhibitor locks");

    r = sd_bus_call_method(bus_conn,
        LOGIND_DEST, LOGIND_PATH, LOGIND_INTERFACE, LIST_INHIBITORS_METHOD,
        &ret_error, &reply, NULL);
    if (r < 0) {
        syslog(LOG_ERR, "sd_bus_call_method: %s", ret_error.message);
        return r;
    }

    r = sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, LIST_INHIBITORS_METHOD_SIGNATURE);
    if (r < 0) {
        syslog(LOG_ERR, "sd_bus_message_enter_container: %s", strerror(0 - r));
        return r;
    }

    for (;;) {
        r = sd_bus_message_read(reply, LIST_INHIBITORS_METHOD_SIGNATURE, &what, &who, &why, &mode, &uid, &pid);
        if (r < 0) {
            syslog(LOG_ERR, "sd_bus_message_read: %s", strerror(0 - r));
            return r;
        }

        if (r == 0) {
            break;
        }


        if (!is_sleep_delay_inhibitor(what, mode)) {
            // is not a sleep delay inhibitor
            continue;
        }

        if (num_sleep_delay_inhibitors == UINT32_MAX) {
            r = 0 - EOVERFLOW;
            syslog(LOG_ERR, "num_sleep_delay_inhibitors: %s", strerror(0 - r));
            return r;
        }

        num_sleep_delay_inhibitors++;
        syslog(LOG_EMERG, "who: %s, uid: %u, pid: %u, what: %s, why: %s, mode: %s",
            who, uid, pid, what, why, mode);
    }

    r = sd_bus_message_exit_container(reply);
    if (r < 0 ) {
        syslog(LOG_ERR, "sd_bus_message_exit_container: %s", strerror(0 - r));
        return r;
    }

    return num_sleep_delay_inhibitors;
}

int main(int argc, char **argv) {
    _cleanup_(sd_bus_unrefp) sd_bus *bus_conn = NULL;
    int r;

    r = sd_bus_default_system(&bus_conn);
    if (r < 0) {
        syslog(LOG_ERR, "sd_bus_default_systemd: %s", strerror(0 - r));
        return r;
    }

    r = check_sleep_delay_inhibitors(bus_conn);
    if (r == 0) {
        syslog(LOG_DEBUG, "No active sleep delay inhibitor locks. Sleep continuing.");
    }

    if (r > 0) {
        syslog(LOG_EMERG, "%i active sleep delay inhibitor locks. Will cancel sleep.", r);
    }

    return r;
}
