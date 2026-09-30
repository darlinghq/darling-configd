#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCNetworkConfiguration.h>
#include <SystemConfiguration/SCNetworkConnection.h>
#include <CoreFoundation/CFRuntime.h>
#include <dispatch/dispatch.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <mach/mach_error.h>
#include <servers/bootstrap.h>
#include "SCErrorMessages.h"

#define STUB() printf("STUB %s\n", __func__)

const CFStringRef kSCDynamicStoreUseSessionKeys = CFSTR("UseSessionKeys");

CFTypeID SCNetworkConnectionGetTypeID(void)
{
    STUB();
	return 0;
}

Boolean SCNetworkConnectionScheduleWithRunLoop(SCNetworkConnectionRef ref, CFRunLoopRef rl, CFStringRef rlMode)
{
    STUB();
	return 0;
}

Boolean SCNetworkConnectionUnscheduleFromRunLoop(SCNetworkConnectionRef ref, CFRunLoopRef rl, CFStringRef rlMode)
{
    STUB();
	return 0;
}

Boolean SCNetworkConnectionStop(SCNetworkConnectionRef ref, Boolean force)
{
    STUB();
	return 0;
}

Boolean SCNetworkConnectionStart(SCNetworkConnectionRef ref, CFDictionaryRef userOptions, Boolean linger)
{
    STUB();
	return 0;
}

Boolean
SCNetworkConnectionCopyUserPreferences		(
						CFDictionaryRef				  __nullable	selectionOptions,
						CFStringRef		__nonnull	* __nullable	serviceID,
						CFDictionaryRef		__nonnull	* __nullable	userOptions
						)			__OSX_AVAILABLE_STARTING(__MAC_10_3,__IPHONE_NA) {
    STUB();
	return 0;
}

SCNetworkConnectionRef SCNetworkConnectionCreateWithServiceID(CFAllocatorRef allocator, CFStringRef serviceID, SCNetworkConnectionCallBack cb, SCNetworkConnectionContext* ctxt)
{
    STUB();
	return NULL;
}

/*
 * SCNetworkReachability
 *
 * Darling has no configd to track network state, so a target's flags are
 * worked out when they are asked for: a UDP socket is connected towards
 * the target (connect() only selects a route, nothing is sent), and the
 * target is compared with the addresses and subnets of the interfaces.
 * A target given by name is not resolved; the route to a documentation
 * address (RFC 5737, RFC 3849) stands in for it.
 *
 * Reachability change notifications are not implemented. Setting a
 * callback, scheduling on a run loop and setting a dispatch queue succeed,
 * but the callback is never called.
 */
struct __SCNetworkReachability {
	CFRuntimeBase base;
	CFStringRef name;
	struct sockaddr_storage local;
	struct sockaddr_storage remote;
	Boolean hasLocal;
	Boolean hasRemote;
	SCNetworkReachabilityCallBack callout;
	SCNetworkReachabilityContext context;
};

static void reachabilityReleaseContext(struct __SCNetworkReachability* target)
{
	if (target->context.release != NULL && target->context.info != NULL)
		target->context.release(target->context.info);
	memset(&target->context, 0, sizeof(target->context));
	target->callout = NULL;
}

static void reachabilityFinalize(CFTypeRef cf)
{
	struct __SCNetworkReachability* target = (struct __SCNetworkReachability*) cf;

	if (target->name != NULL)
		CFRelease(target->name);
	reachabilityReleaseContext(target);
}

static CFStringRef reachabilityCopyDescription(CFTypeRef cf)
{
	struct __SCNetworkReachability* target = (struct __SCNetworkReachability*) cf;

	if (target->name != NULL)
		return CFStringCreateWithFormat(NULL, NULL,
			CFSTR("<SCNetworkReachability %p> {name = %@}"), cf, target->name);
	return CFStringCreateWithFormat(NULL, NULL,
		CFSTR("<SCNetworkReachability %p>"), cf);
}

static const CFRuntimeClass reachabilityClass = {
	.version = 0,
	.className = "SCNetworkReachability",
	.finalize = reachabilityFinalize,
	.copyDebugDesc = reachabilityCopyDescription,
};

static CFTypeID reachabilityTypeID = _kCFRuntimeNotATypeID;

static void reachabilityRegisterClass(void* unused)
{
	reachabilityTypeID = _CFRuntimeRegisterClass(&reachabilityClass);
}

CFTypeID SCNetworkReachabilityGetTypeID(void)
{
	static dispatch_once_t once;

	dispatch_once_f(&once, NULL, reachabilityRegisterClass);
	return reachabilityTypeID;
}

static socklen_t sockaddrLength(int family)
{
	switch (family) {
		case AF_INET:
			return sizeof(struct sockaddr_in);
		case AF_INET6:
			return sizeof(struct sockaddr_in6);
		default:
			return 0;
	}
}

// Copies an IPv4 or IPv6 address; anything else, or an address shorter
// than its family's sockaddr, is rejected.
static Boolean copySockaddr(struct sockaddr_storage* dst, const struct sockaddr* src)
{
	socklen_t len;

	if (src == NULL)
		return FALSE;
	len = sockaddrLength(src->sa_family);
	if (len == 0 || src->sa_len < len)
		return FALSE;

	memset(dst, 0, sizeof(*dst));
	memcpy(dst, src, len);
	dst->ss_len = len;
	return TRUE;
}

static struct __SCNetworkReachability* reachabilityCreate(CFAllocatorRef allocator)
{
	return (struct __SCNetworkReachability*) _CFRuntimeCreateInstance(allocator,
		SCNetworkReachabilityGetTypeID(),
		sizeof(struct __SCNetworkReachability) - sizeof(CFRuntimeBase), NULL);
}

static Boolean isTarget(SCNetworkReachabilityRef target)
{
	return target != NULL && CFGetTypeID(target) == SCNetworkReachabilityGetTypeID();
}

SCNetworkReachabilityRef SCNetworkReachabilityCreateWithAddress(CFAllocatorRef allocator, const struct sockaddr* address)
{
	struct sockaddr_storage remote;
	struct __SCNetworkReachability* target;

	if (!copySockaddr(&remote, address))
		return NULL;

	target = reachabilityCreate(allocator);
	if (target == NULL)
		return NULL;
	target->remote = remote;
	target->hasRemote = TRUE;
	return (SCNetworkReachabilityRef) target;
}

SCNetworkReachabilityRef SCNetworkReachabilityCreateWithAddressPair(CFAllocatorRef allocator, const struct sockaddr* localAddress, const struct sockaddr* remoteAddress)
{
	struct sockaddr_storage local, remote;
	struct __SCNetworkReachability* target;

	if (localAddress == NULL && remoteAddress == NULL)
		return NULL;
	if (localAddress != NULL && !copySockaddr(&local, localAddress))
		return NULL;
	if (remoteAddress != NULL && !copySockaddr(&remote, remoteAddress))
		return NULL;

	target = reachabilityCreate(allocator);
	if (target == NULL)
		return NULL;
	if (localAddress != NULL) {
		target->local = local;
		target->hasLocal = TRUE;
	}
	if (remoteAddress != NULL) {
		target->remote = remote;
		target->hasRemote = TRUE;
	}
	return (SCNetworkReachabilityRef) target;
}

SCNetworkReachabilityRef SCNetworkReachabilityCreateWithName(CFAllocatorRef allocator, const char* nodeName)
{
	struct __SCNetworkReachability* target;

	if (nodeName == NULL)
		return NULL;

	target = reachabilityCreate(allocator);
	if (target == NULL)
		return NULL;
	target->name = CFStringCreateWithCString(allocator, nodeName, kCFStringEncodingUTF8);

	// A numeric address needs no name resolution.
	struct sockaddr_in* sin = (struct sockaddr_in*) &target->remote;
	struct sockaddr_in6* sin6 = (struct sockaddr_in6*) &target->remote;
	if (inet_pton(AF_INET, nodeName, &sin->sin_addr) == 1) {
		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		target->hasRemote = TRUE;
	} else if (inet_pton(AF_INET6, nodeName, &sin6->sin6_addr) == 1) {
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		target->hasRemote = TRUE;
	}
	return (SCNetworkReachabilityRef) target;
}

static Boolean sameAddress(const struct sockaddr* a, const struct sockaddr* b)
{
	if (a->sa_family != b->sa_family)
		return FALSE;
	if (a->sa_family == AF_INET)
		return ((const struct sockaddr_in*) a)->sin_addr.s_addr
			== ((const struct sockaddr_in*) b)->sin_addr.s_addr;
	return memcmp(&((const struct sockaddr_in6*) a)->sin6_addr,
		&((const struct sockaddr_in6*) b)->sin6_addr, sizeof(struct in6_addr)) == 0;
}

static Boolean sameSubnet(const struct sockaddr* a, const struct sockaddr* b, const struct sockaddr* mask)
{
	const uint8_t *pa, *pb, *pm;
	size_t i, len;

	if (a->sa_family != b->sa_family || mask->sa_family != a->sa_family)
		return FALSE;
	if (a->sa_family == AF_INET) {
		pa = (const uint8_t*) &((const struct sockaddr_in*) a)->sin_addr;
		pb = (const uint8_t*) &((const struct sockaddr_in*) b)->sin_addr;
		pm = (const uint8_t*) &((const struct sockaddr_in*) mask)->sin_addr;
		len = sizeof(struct in_addr);
	} else {
		pa = (const uint8_t*) &((const struct sockaddr_in6*) a)->sin6_addr;
		pb = (const uint8_t*) &((const struct sockaddr_in6*) b)->sin6_addr;
		pm = (const uint8_t*) &((const struct sockaddr_in6*) mask)->sin6_addr;
		len = sizeof(struct in6_addr);
	}
	for (i = 0; i < len; i++) {
		if ((pa[i] & pm[i]) != (pb[i] & pm[i]))
			return FALSE;
	}
	return TRUE;
}

// kSCNetworkReachabilityFlagsIsLocalAddress for an address of one of the
// interfaces, kSCNetworkReachabilityFlagsIsDirect for that and for an
// address in the subnet of a broadcast or loopback interface.
static SCNetworkReachabilityFlags interfaceFlags(const struct sockaddr* address)
{
	struct ifaddrs *interfaces, *ifa;
	SCNetworkReachabilityFlags flags = 0;

	if (getifaddrs(&interfaces) != 0)
		return 0;
	for (ifa = interfaces; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || !(ifa->ifa_flags & IFF_UP)
			|| ifa->ifa_addr->sa_family != address->sa_family)
			continue;
		if (sameAddress(ifa->ifa_addr, address)) {
			flags = kSCNetworkReachabilityFlagsIsLocalAddress
				| kSCNetworkReachabilityFlagsIsDirect;
			break;
		}
		if (!(ifa->ifa_flags & IFF_POINTOPOINT) && ifa->ifa_netmask != NULL
			&& sameSubnet(ifa->ifa_addr, address, ifa->ifa_netmask))
			flags = kSCNetworkReachabilityFlagsIsDirect;
	}
	freeifaddrs(interfaces);
	return flags;
}

// Asks the kernel for a route from local (if given) to remote (if given).
// A local address must be one the socket can bind to.
static SCNetworkReachabilityFlags routeFlags(const struct sockaddr_storage* local, const struct sockaddr_storage* remote)
{
	int family = (remote != NULL) ? remote->ss_family : local->ss_family;
	SCNetworkReachabilityFlags flags = 0;
	int fd;

	fd = socket(family, SOCK_DGRAM, 0);
	if (fd < 0)
		return 0;

	if (local != NULL) {
		struct sockaddr_storage bound = *local;

		// Only the address matters, not whether the port is free.
		if (family == AF_INET)
			((struct sockaddr_in*) &bound)->sin_port = 0;
		else
			((struct sockaddr_in6*) &bound)->sin6_port = 0;
		if (bind(fd, (struct sockaddr*) &bound, sockaddrLength(family)) != 0)
			goto out;
	}

	if (remote != NULL) {
		if (connect(fd, (const struct sockaddr*) remote, sockaddrLength(family)) != 0)
			goto out;
		flags = kSCNetworkReachabilityFlagsReachable
			| interfaceFlags((const struct sockaddr*) remote);
	} else {
		flags = kSCNetworkReachabilityFlagsReachable;
	}

out:
	close(fd);
	return flags;
}

// RFC 5737 (TEST-NET-1) and RFC 3849 documentation addresses: never used
// on a real network, so connecting towards them follows the default route.
static void documentationAddress(int family, struct sockaddr_storage* address)
{
	memset(address, 0, sizeof(*address));
	if (family == AF_INET) {
		struct sockaddr_in* sin = (struct sockaddr_in*) address;
		sin->sin_len = sizeof(*sin);
		sin->sin_family = AF_INET;
		inet_pton(AF_INET, "192.0.2.1", &sin->sin_addr);
	} else {
		struct sockaddr_in6* sin6 = (struct sockaddr_in6*) address;
		sin6->sin6_len = sizeof(*sin6);
		sin6->sin6_family = AF_INET6;
		inet_pton(AF_INET6, "2001:db8::1", &sin6->sin6_addr);
	}
}

Boolean SCNetworkReachabilityGetFlags(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags* flags)
{
	struct __SCNetworkReachability* ref = (struct __SCNetworkReachability*) target;
	struct sockaddr_storage address;

	if (!isTarget(target) || flags == NULL)
		return FALSE;

	if (ref->hasRemote || ref->hasLocal) {
		*flags = routeFlags(ref->hasLocal ? &ref->local : NULL,
			ref->hasRemote ? &ref->remote : NULL);
		return TRUE;
	}

	// A name: report whether there is a route to the Internet at all.
	documentationAddress(AF_INET, &address);
	*flags = routeFlags(NULL, &address) & kSCNetworkReachabilityFlagsReachable;
	if (*flags == 0) {
		documentationAddress(AF_INET6, &address);
		*flags = routeFlags(NULL, &address) & kSCNetworkReachabilityFlagsReachable;
	}
	return TRUE;
}

Boolean SCNetworkReachabilitySetCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityCallBack callout, SCNetworkReachabilityContext* context)
{
	struct __SCNetworkReachability* ref = (struct __SCNetworkReachability*) target;
	SCNetworkReachabilityContext newContext;

	if (!isTarget(target))
		return FALSE;

	// Retain the new info before releasing the old one, they may be the same.
	memset(&newContext, 0, sizeof(newContext));
	if (callout != NULL && context != NULL) {
		newContext = *context;
		if (newContext.retain != NULL && newContext.info != NULL)
			newContext.info = (void*) newContext.retain(newContext.info);
	}
	reachabilityReleaseContext(ref);
	ref->callout = callout;
	ref->context = newContext;
	return TRUE;
}

Boolean SCNetworkReachabilityScheduleWithRunLoop(SCNetworkReachabilityRef target, CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	return isTarget(target) && runLoop != NULL && runLoopMode != NULL;
}

Boolean SCNetworkReachabilityUnscheduleFromRunLoop(SCNetworkReachabilityRef target, CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	return isTarget(target) && runLoop != NULL && runLoopMode != NULL;
}

Boolean SCNetworkReachabilitySetDispatchQueue(SCNetworkReachabilityRef target, dispatch_queue_t queue)
{
	return isTarget(target);
}

CFStringRef SCDynamicStoreKeyCreateProxies(CFAllocatorRef allocator)
{
	return SCDynamicStoreKeyCreateNetworkGlobalEntity(allocator,
							  kSCDynamicStoreDomainState,
							  kSCEntNetProxies);
}

const char *SCErrorString(int status)
{
	size_t i;

	for (i = 0; i < nSC_ERRMSGS; i++) {
		if (sc_errmsgs[i].status == status) {
			return sc_errmsgs[i].message;
		}
	}
	if ((status > 0) && (status <= ELAST)) {
		return strerror(status);
	}
	if ((status >= BOOTSTRAP_SUCCESS) && (status <= BOOTSTRAP_NO_MEMORY)) {
		return bootstrap_strerror(status);
	}
	return mach_error_string(status);
}

/*
 * SCDynamicStore
 *
 * There is no configd store in Darling. A store session is a real object
 * that accepts notification keys and can be scheduled, but no change
 * notifications are delivered: the callout is never called and the run
 * loop source never fires. SCDynamicStoreCopyValue answers the global IPv4
 * and IPv6 state keys with the interface the kernel routes Internet
 * traffic through, and finds no value for any other key.
 */
struct __SCDynamicStore {
	CFRuntimeBase base;
	CFStringRef name;
	CFArrayRef notificationKeys;
	CFArrayRef notificationPatterns;
	SCDynamicStoreCallBack callout;
	SCDynamicStoreContext context;
};

static void storeFinalize(CFTypeRef cf)
{
	struct __SCDynamicStore* store = (struct __SCDynamicStore*) cf;

	if (store->name != NULL)
		CFRelease(store->name);
	if (store->notificationKeys != NULL)
		CFRelease(store->notificationKeys);
	if (store->notificationPatterns != NULL)
		CFRelease(store->notificationPatterns);
	if (store->context.release != NULL && store->context.info != NULL)
		store->context.release(store->context.info);
}

static CFStringRef storeCopyDescription(CFTypeRef cf)
{
	struct __SCDynamicStore* store = (struct __SCDynamicStore*) cf;

	return CFStringCreateWithFormat(NULL, NULL,
		CFSTR("<SCDynamicStore %p> {name = %@}"), cf,
		(store->name != NULL) ? store->name : CFSTR(""));
}

static const CFRuntimeClass storeClass = {
	.version = 0,
	.className = "SCDynamicStore",
	.finalize = storeFinalize,
	.copyDebugDesc = storeCopyDescription,
};

static CFTypeID storeTypeID = _kCFRuntimeNotATypeID;

static void storeRegisterClass(void* unused)
{
	storeTypeID = _CFRuntimeRegisterClass(&storeClass);
}

CFTypeID SCDynamicStoreGetTypeID(void)
{
	static dispatch_once_t once;

	dispatch_once_f(&once, NULL, storeRegisterClass);
	return storeTypeID;
}

static Boolean isStore(SCDynamicStoreRef store)
{
	return store != NULL && CFGetTypeID(store) == SCDynamicStoreGetTypeID();
}

SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef allocator, CFStringRef name, SCDynamicStoreCallBack callout, SCDynamicStoreContext *context)
{
	struct __SCDynamicStore* store;

	store = (struct __SCDynamicStore*) _CFRuntimeCreateInstance(allocator,
		SCDynamicStoreGetTypeID(),
		sizeof(struct __SCDynamicStore) - sizeof(CFRuntimeBase), NULL);
	if (store == NULL)
		return NULL;

	if (name != NULL)
		store->name = CFStringCreateCopy(allocator, name);
	store->callout = callout;
	if (callout != NULL && context != NULL) {
		store->context = *context;
		if (store->context.retain != NULL && store->context.info != NULL)
			store->context.info = (void*) store->context.retain(store->context.info);
	}
	return (SCDynamicStoreRef) store;
}

SCDynamicStoreRef __nullable SCDynamicStoreCreateWithOptions(CFAllocatorRef __nullable allocator, CFStringRef name, CFDictionaryRef __nullable storeOptions, SCDynamicStoreCallBack __nullable callout, SCDynamicStoreContext* __nullable context)
{
	return SCDynamicStoreCreate(allocator, name, callout, context);
};

Boolean SCDynamicStoreSetDispatchQueue(SCDynamicStoreRef store, dispatch_queue_t queue)
{
	return isStore(store);
}

Boolean SCDynamicStoreSetNotificationKeys(SCDynamicStoreRef store, CFArrayRef __nullable keys, CFArrayRef __nullable patterns) {
	struct __SCDynamicStore* s = (struct __SCDynamicStore*) store;

	if (!isStore(store))
		return FALSE;

	if (keys != NULL)
		CFRetain(keys);
	if (patterns != NULL)
		CFRetain(patterns);
	if (s->notificationKeys != NULL)
		CFRelease(s->notificationKeys);
	if (s->notificationPatterns != NULL)
		CFRelease(s->notificationPatterns);
	s->notificationKeys = keys;
	s->notificationPatterns = patterns;
	return TRUE;
}

static void storeSourcePerform(void* info)
{
}

CFRunLoopSourceRef SCDynamicStoreCreateRunLoopSource(CFAllocatorRef allocator, SCDynamicStoreRef store, CFIndex order)
{
	CFRunLoopSourceContext context;

	if (!isStore(store))
		return NULL;

	// The source keeps the session alive while it exists.
	memset(&context, 0, sizeof(context));
	context.info = (void*) store;
	context.retain = CFRetain;
	context.release = CFRelease;
	context.copyDescription = CFCopyDescription;
	context.perform = storeSourcePerform;
	return CFRunLoopSourceCreate(allocator, order, &context);
}

// The interface whose address the kernel picks for a connection towards a
// documentation address (see documentationAddress()), i.e. the interface of
// the default route. Nothing is sent.
static CFStringRef copyPrimaryInterface(int family)
{
	struct sockaddr_storage remote, local;
	socklen_t localLen = sizeof(local);
	struct ifaddrs *interfaces, *ifa;
	CFStringRef name = NULL;
	int fd, err;

	documentationAddress(family, &remote);
	fd = socket(family, SOCK_DGRAM, 0);
	if (fd < 0)
		return NULL;
	err = connect(fd, (struct sockaddr*) &remote, sockaddrLength(family));
	if (err == 0)
		err = getsockname(fd, (struct sockaddr*) &local, &localLen);
	close(fd);
	if (err != 0)
		return NULL;

	if (getifaddrs(&interfaces) != 0)
		return NULL;
	for (ifa = interfaces; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL || !(ifa->ifa_flags & IFF_UP)
			|| (ifa->ifa_flags & IFF_LOOPBACK)
			|| ifa->ifa_addr->sa_family != family)
			continue;
		if (sameAddress(ifa->ifa_addr, (struct sockaddr*) &local)) {
			name = CFStringCreateWithCString(NULL, ifa->ifa_name, kCFStringEncodingUTF8);
			break;
		}
	}
	freeifaddrs(interfaces);
	return name;
}

static Boolean isGlobalStateKey(CFStringRef key, CFStringRef entity)
{
	CFStringRef globalKey;
	Boolean equal;

	globalKey = SCDynamicStoreKeyCreateNetworkGlobalEntity(NULL,
		kSCDynamicStoreDomainState, entity);
	if (globalKey == NULL)
		return FALSE;
	equal = CFEqual(key, globalKey);
	CFRelease(globalKey);
	return equal;
}

CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef store, CFStringRef key)
{
	CFStringRef primary;
	CFDictionaryRef value;
	int family;

	if (key == NULL || CFGetTypeID(key) != CFStringGetTypeID())
		return NULL;
	if (store != NULL && !isStore(store))
		return NULL;

	if (isGlobalStateKey(key, kSCEntNetIPv4))
		family = AF_INET;
	else if (isGlobalStateKey(key, kSCEntNetIPv6))
		family = AF_INET6;
	else
		return NULL;

	primary = copyPrimaryInterface(family);
	if (primary == NULL)
		return NULL;

	const void* keys[] = { kSCDynamicStorePropNetPrimaryInterface };
	const void* values[] = { primary };
	value = CFDictionaryCreate(NULL, keys, values, 1,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFRelease(primary);
	return value;
}

CFStringRef SCDynamicStoreKeyCreateComputerName(CFAllocatorRef allocator) {
	return SCDynamicStoreKeyCreate(allocator, CFSTR("%@/%@"),
				       kSCDynamicStoreDomainSetup, kSCCompSystem);
};

Boolean SCPreferencesSetComputerName(SCPreferencesRef	prefs, CFStringRef __nullable	name, CFStringEncoding	nameEncoding) {
	STUB();
	return FALSE;
};

Boolean SCPreferencesSetLocalHostName(SCPreferencesRef	prefs, CFStringRef __nullable	name) {
	STUB();
	return FALSE;
};

CFStringRef SCDynamicStoreKeyCreateHostNames(CFAllocatorRef allocator) {
	return SCDynamicStoreKeyCreate(allocator, CFSTR("%@/%@/%@"),
				       kSCDynamicStoreDomainSetup, kSCCompNetwork,
				       kSCCompHostNames);
};

CFArrayRef SCNetworkInterfaceCopyAll(void)
{
	STUB();
	return CFArrayCreate(kCFAllocatorDefault, NULL, 0, &kCFTypeArrayCallBacks);
}

SCNetworkConnectionRef SCNetworkConnectionCreateWithService(CFAllocatorRef allocator, CFStringRef serviceID, SCNetworkConnectionCallBack callout, SCNetworkConnectionContext* context)
{
	STUB();
	return NULL;
}

CFArrayRef SCNetworkConnectionCopyAvailableServices(void)
{
	STUB();
	return CFArrayCreate(kCFAllocatorDefault, NULL, 0, &kCFTypeArrayCallBacks);
}

CFDictionaryRef SCNetworkConnectionCopyExtendedStatus(SCNetworkConnectionRef connection)
{
	STUB();
	return CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

CFDictionaryRef SCNetworkConnectionCopyStatistics(SCNetworkConnectionRef connection)
{
	STUB();
	return CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

SCNetworkServiceRef SCNetworkConnectionGetService(SCNetworkConnectionRef connection)
{
	STUB();
	return NULL;
}

SCNetworkConnectionStatus SCNetworkConnectionGetStatus(SCNetworkConnectionRef connection)
{
	STUB();
	return 0; // kSCNetworkConnectionInvalid
}
