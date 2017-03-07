#ifndef _NE_SESSION_H
#define _NE_SESSION_H

typedef int ne_session_status_t;
typedef int ne_session_event_t;
typedef int NEPolicyServiceActionType;
typedef void* ne_session_t;

static inline int ne_session_use_as_system_vpn(void) { return 0; }
static inline ne_session_t ne_session_create(uuid_t configId, int sessType) { return NULL; }
static inline void ne_session_release(ne_session_t sess) {}
static inline void ne_session_set_event_handler(ne_session_t sess, void* x, void* x2) {}
static inline void ne_session_retain(ne_session_t sess) {}
static inline void ne_session_get_status(ne_session_t sess, ...) {}
static inline void ne_session_get_info(ne_session_t sess, ...) {}
static inline void ne_session_start_on_behalf_of(ne_session_t sess, ...) {}
static inline void ne_session_start_with_options(ne_session_t sess, ...) {}
static inline void ne_session_send_barrier(ne_session_t sess, ...) {}
static inline void ne_session_stop(ne_session_t sess) {}
static inline void ne_session_cancel(ne_session_t sess) {}
static inline void* ne_session_copy_policy_match(const char* hostname, ...) { return NULL; }
static inline NEPolicyServiceActionType e_session_policy_match_get_service_action(void* p) { return 0; }
static inline int ne_session_policy_match_get_service(void* p, ...) { return 0; }
static inline int ne_session_policy_match_get_service_type(void* p) { return 0; }

static inline CFStringRef VPNAppLayerCopyMatchingService(audit_token_t x, ...) { return NULL; }

enum {
	NESessionInfoTypeConfiguration,
	NESessionInfoTypeExtendedStatus,
	NESessionInfoTypeStatistics
};

enum {
	NESessionTypeVPN
};

enum {
	NESessionEventStatusChanged,
	NESessionEventCanceled
};

enum {
	NESessionPolicyActionTrigger,
	NESessionPolicyActionTriggerIfNeeded
};

enum {
	NESessionStatusInvalid,
	NESessionStatusDisconnected,
	NESessionStatusConnecting,
	NESessionStatusConnected,
	NESessionStatusDisconnecting,
	NESessionStatusReasserting
};

#define NESMSessionLegacyUserConfigurationKey "NESMSessionLegacyUserConfigurationKey"
#define NESessionStartOptionIsOnDemandKey "NESessionStartOptionIsOnDemandKey"
#define NESessionStartOptionMatchHostnameKey "NESessionStartOptionMatchHostnameKey"


#endif

