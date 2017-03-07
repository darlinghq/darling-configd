#ifndef _PPP_CONTROLLER_H
#define _PPP_CONTROLLER_H

#define PPPCONTROLLER_SERVER "PPPCONTROLLER_SERVER"
#define PPP_MACH_MAX_INLINE_DATA 64

static inline kern_return_t pppcontroller_attach() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_attach_proxy() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_copystatistics() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_getstatus() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_copyextendedstatus() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_start() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_stop() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_suspend() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_resume() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_ondemand_refresh_state() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_copyuseroptions() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_notification() { return KERN_FAILURE; }
static inline kern_return_t pppcontroller_flow_divert_copy_token_parameters() { return KERN_FAILURE; }

#endif

