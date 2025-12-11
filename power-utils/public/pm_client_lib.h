/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __PM_CLIENT_H
#define __PM_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

struct _pm_client_s;
typedef struct _pm_client_s* pm_client_t;

enum PM_MODE {
	PM_MODE_DS = 1,     // Deep Sleep
	PM_MODE_S2R,        // Suspend 2 RAM
	PM_MODE_INVALID = 0xFFFFFFFF,    // Invalid
};

/* pm_ops_s
*
* callbacks for power mode notifications and impose level notifications
*
* pm_enter / pm_exit: power mode enter / exit callbacks are called upon receiving
*                     notifications to enter / exit a power mode
*   mode arg: PM_MODE enum value representing the power mode being entered / exited
*       pm_enter with mode of PM_MODE_DS means entering Deep Sleep mode
*       pm_exit with mode of PM_MODE_S2R means exiting from Suspend 2 RAM mode
* impose: set new "level" to be interpreted by the client
*
* callback return int value: client must return 0 if the callback is successful
*                            client must return negative error code if callback fails
*
*       Eg. if pm_enter fails because the client is busy with servicing a user, pm_enter could return EBUSY
*/
struct pm_ops_s {
	int (*pm_enter)(void *ctxt, enum PM_MODE mode);
	int (*pm_exit)(void *ctxt, enum PM_MODE mode);
	int (*impose)(void *ctxt, int level);
	int (*pm_cancel)(void *ctxt, enum PM_MODE mode);
};

/* pm_register
*
* Register with power management system to receive notifications & trigger callbacks.
*
* name: mandatory unique string representing the calling program
* ops: callbacks for the various notifications. Eg. when the calling program is notified to enter
*      the deep sleep power mode, pm_enter() callback will be called with mode PM_MODE_DS
* ctxt: optional context passed into each callback. If no context is needed, can pass NULL into ctxt arg
* hdl: pm_register creates this output handle associated with the power manager client.
*      pm_register modifies the handle for later teardown using the pm_deregister function.
*/
int pm_register(const char *name,
		struct pm_ops_s *ops,
		void *ctxt,
		pm_client_t *hdl);

/* pm_deregister
*
* Deregister with power management system. No longer receive notifications after deregistering.
* This function tears down internally allocated resources / memory from pm_register.
*
* hdl: power management client handle from a previous invocation of pm_register.
*/
int pm_deregister(pm_client_t hdl);

#ifdef __cplusplus
}
#endif

#endif
