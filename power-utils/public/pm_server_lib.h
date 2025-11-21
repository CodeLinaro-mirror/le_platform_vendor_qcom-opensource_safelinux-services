/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __PM_CLIENT_H
#define __PM_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief Send notifications to the clients which registered using pm_register api.
* @param[in] name: mandatory unique string representing the name of the client that notification is being sent to
* @param[in] pm_cmd: either pm_enter, pm_exit or impose
* @param[in] mode: used to send an impose level or suspend_mode to the client
* @return Returns 0 on success , negative value on failure.
*/
int pm_send_notif(char *name,
                char *pm_cmd,
                int mode);

/**
* @brief Send notifications to the clients which registered using pm_register api.
* @param[in] name: mandatory unique string representing the name of the client that notification is being sent to
* @param[in] pm_cmd: either pm_enter, pm_exit or impose
* @param[in] impose_level: used to send an impose level to the client
* @param[in] lpm_mode: lpm mode used for impose_v2
* @return Returns 0 on success , negative value on failure.
*/
int pm_send_notif_v2(char *name,
                char *pm_cmd,
                int impose_level,
                int lpm_mode);

#ifdef __cplusplus
}
#endif

#endif
