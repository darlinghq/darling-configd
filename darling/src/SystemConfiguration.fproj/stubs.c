#include <SystemConfiguration/SystemConfiguration.h>
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

CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef store, CFStringRef key)
{
	STUB();
	return NULL;
}

SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef allocator, CFStringRef name, SCDynamicStoreCallBack callout, SCDynamicStoreContext *context)
{
	STUB();
	return NULL;
}

SCDynamicStoreRef __nullable SCDynamicStoreCreateWithOptions(CFAllocatorRef __nullable allocator, CFStringRef name, CFDictionaryRef __nullable storeOptions, SCDynamicStoreCallBack __nullable callout, SCDynamicStoreContext* __nullable context)
{
    STUB();
	return NULL;
};

Boolean SCDynamicStoreSetDispatchQueue(SCDynamicStoreRef store, dispatch_queue_t queue)
{
	STUB();
	return FALSE;
}

Boolean SCDynamicStoreSetNotificationKeys(SCDynamicStoreRef store, CFArrayRef __nullable keys, CFArrayRef __nullable patterns) {
	STUB();
	return FALSE;
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
