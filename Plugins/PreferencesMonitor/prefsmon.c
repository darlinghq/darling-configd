/*
 * Copyright (c) 2000-2008, 2010, 2012-2016 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

/*
 * Modification History
 *
 * April 2, 2004		Allan Nathanson <ajn@apple.com>
 * - use SCPreference notification APIs
 *
 * June 24, 2001		Allan Nathanson <ajn@apple.com>
 * - update to public SystemConfiguration.framework APIs
 *
 * November 10, 2000		Allan Nathanson <ajn@apple.com>
 * - initial revision
 */


#include <TargetConditionals.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define	SC_LOG_HANDLE	__log_PreferencesMonitor()
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCPrivate.h>
#include <SystemConfiguration/SCValidation.h>
#include "plugin_shared.h"


#include <CommonCrypto/CommonDigest.h>


/* globals */
static SCPreferencesRef		prefs			= NULL;
static SCDynamicStoreRef	store			= NULL;

/* InterfaceNamer[.plugin] monitoring globals */
Boolean				haveConfiguration	= FALSE;
static CFStringRef		namerKey		= NULL;
static CFArrayRef		preconfigured		= NULL;

/* KernelEventMonitor[.plugin] monitoring globals */
static CFStringRef		interfacesKey		= NULL;

/* SCDynamicStore (Setup:) */
static CFMutableDictionaryRef	currentPrefs;		/* current prefs */
static CFMutableDictionaryRef	newPrefs;		/* new prefs */
static CFMutableArrayRef	unchangedPrefsKeys;	/* new prefs keys which match current */
static CFMutableArrayRef	removedPrefsKeys;	/* old prefs keys to be removed */

static Boolean			rofs			= FALSE;
static Boolean			restorePrefs		= FALSE;

#define MY_PLUGIN_NAME		"PreferencesMonitor"
#define	MY_PLUGIN_ID		CFSTR("com.apple.SystemConfiguration." MY_PLUGIN_NAME)


static void
updateConfiguration(SCPreferencesRef		prefs,
		    SCPreferencesNotification   notificationType,
		    void			*info);


static os_log_t
__log_PreferencesMonitor()
{
	static os_log_t	log	= NULL;

	if (log == NULL) {
		log = os_log_create("com.apple.SystemConfiguration", "PreferencesMonitor");
	}

	return log;
}


static Boolean
restorePreferences()
{
	Boolean			ok = FALSE;
	CFStringRef		currentModel = NULL;
	CFMutableStringRef	modelPrefixStr = NULL;
	CFArrayRef		keyList = NULL;
	CFIndex			keyListCount;
	CFIndex			idx;
	Boolean			modified = FALSE;
	int			sc_status = kSCStatusFailed;

	while (TRUE) {
		ok = SCPreferencesLock(prefs, TRUE);
		if (ok) {
			break;
		}

		sc_status = SCError();
		if (sc_status == kSCStatusStale) {
			SCPreferencesSynchronize(prefs);
		} else {
			SC_log(LOG_NOTICE, "Could not acquire network configuration lock: %s",
			       SCErrorString(sc_status));
			return FALSE;
		}
	}

	keyList = SCPreferencesCopyKeyList(prefs);
	if (keyList == NULL) {
		goto error;
	}

	currentModel = _SC_hw_model(FALSE);
	if (currentModel == NULL) {
		goto error;
	}

	/* Create "model:" string for prefix-check */
	modelPrefixStr = CFStringCreateMutableCopy(NULL, 0, currentModel);
	CFStringAppend(modelPrefixStr, CFSTR(":"));

	keyListCount = CFArrayGetCount(keyList);
	for (idx = 0; idx < keyListCount; idx++) {
		CFStringRef existingKey = CFArrayGetValueAtIndex(keyList, idx);
		CFStringRef key;
		CFArrayRef splitKey = NULL;
		CFPropertyListRef value;

		if (isA_CFString(existingKey) == NULL) {
			continue;
		}

		if (!CFStringHasPrefix(existingKey, modelPrefixStr)) {
			    continue;
		}

		splitKey = CFStringCreateArrayBySeparatingStrings(NULL, existingKey, CFSTR(":"));
		key = CFArrayGetValueAtIndex(splitKey, 1);
		value = SCPreferencesGetValue(prefs, existingKey);
		SCPreferencesSetValue(prefs, key, value);
		SCPreferencesRemoveValue(prefs, existingKey);
		modified = TRUE;
		CFRelease(splitKey);
	}

	if (modified) {
		SCPreferencesRef	ni_prefs = NULL;
		ni_prefs = SCPreferencesCreate(NULL, MY_PLUGIN_ID, CFSTR("NetworkInterfaces.plist"));
		if (ni_prefs == NULL) {
			goto error;
		}

		ok = _SCNetworkConfigurationCheckValidityWithPreferences(prefs, ni_prefs, NULL);
		CFRelease(ni_prefs);

		//Commit the changes only if prefs files valid
		if (ok) {
			if (!SCPreferencesCommitChanges(prefs)) {
				if (SCError() != EROFS) {
					SC_log(LOG_NOTICE, "SCPreferencesCommitChanges() failed: %s",
					       SCErrorString(SCError()));
				}
				goto error;

			}

			(void) SCPreferencesApplyChanges(prefs);
		}
	}

error:
	(void) SCPreferencesUnlock(prefs);

	if (keyList != NULL) {
		CFRelease(keyList);
	}
	if (modelPrefixStr != NULL) {
		CFRelease(modelPrefixStr);
	}

	return modified;
}

static Boolean
establishNewPreferences()
{
	SCNetworkSetRef	current		= NULL;
	CFStringRef	new_model;
	Boolean		ok		= FALSE;
	int		sc_status	= kSCStatusFailed;
	SCNetworkSetRef	set		= NULL;
	Boolean		updated		= FALSE;

	while (TRUE) {
		ok = SCPreferencesLock(prefs, TRUE);
		if (ok) {
			break;
		}

		sc_status = SCError();
		if (sc_status == kSCStatusStale) {
			SCPreferencesSynchronize(prefs);
		} else {
			SC_log(LOG_NOTICE, "Could not acquire network configuration lock: %s",
			       SCErrorString(sc_status));
			return FALSE;
		}
	}

	/* Ensure that the preferences has the new model */
	new_model = _SC_hw_model(FALSE);

	/* Need to regenerate the new configuration for new model */
	if (new_model != NULL) {
		CFStringRef	old_model;

		old_model = SCPreferencesGetValue(prefs, MODEL);
		if ((old_model != NULL) && !_SC_CFEqual(old_model, new_model)) {
			CFIndex		count;
			CFIndex		index;
			CFArrayRef	keys;

			keys = SCPreferencesCopyKeyList(prefs);
			count = (keys != NULL) ? CFArrayGetCount(keys) : 0;
			// if new hardware
			for (index = 0; index < count; index++) {
				CFStringRef		existing_key;

				existing_key = CFArrayGetValueAtIndex(keys, index);
				if (isA_CFString(existing_key) != NULL) {
					CFStringRef		new_key;
					CFPropertyListRef	value;

					/* If it already contains a Model
					   or if it already contains a MODEL:KEY key skip it*/
					if (CFEqual(existing_key, MODEL)
					    || CFStringFind(existing_key, CFSTR(":"), 0).location
					    != kCFNotFound) {
						continue;
					}

					value = SCPreferencesGetValue(prefs, existing_key);

					/* Create a new key as OLD_MODEL:OLD_KEY */
					new_key = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@:%@"),
									   old_model, existing_key);
					SCPreferencesSetValue(prefs, new_key, value);
					if (!CFEqual(existing_key, kSCPrefSystem)) {
						/* preserve existing host names */
						SCPreferencesRemoveValue(prefs, existing_key);
					}
					CFRelease(new_key);
				}
			}

			if (keys != NULL) {
				CFRelease(keys);
			}
		}
		/* Set the new model */
		SCPreferencesSetValue(prefs, MODEL, new_model);
	}

	current = SCNetworkSetCopyCurrent(prefs);
	if (current != NULL) {
		set = current;
	}

	if (set == NULL) {
		set = _SCNetworkSetCreateDefault(prefs);
		if (set == NULL) {
			ok = FALSE;
			sc_status = SCError();
			goto done;
		}
	}

	ok = SCNetworkSetEstablishDefaultConfiguration(set);
	if (!ok) {
		sc_status = SCError();
		goto done;
	}

    done :

	if (ok) {
		ok = SCPreferencesCommitChanges(prefs);
		if (ok) {
			SC_log(LOG_NOTICE, "New network configuration saved");
			updated = TRUE;
		} else {
			sc_status = SCError();
			if (sc_status == EROFS) {
				/* a read-only fileysstem is OK */
				ok = TRUE;

				/* ... but we don't want to synchronize */
				rofs = TRUE;
			}
		}

		/* apply (committed or temporary/read-only) changes */
		(void) SCPreferencesApplyChanges(prefs);
	} else if ((current == NULL) && (set != NULL)) {
		(void) SCNetworkSetRemove(set);
	}

	if (!ok) {
		SC_log(LOG_NOTICE, "Could not establish network configuration: %s",
		       SCErrorString(sc_status));
	}

	(void)SCPreferencesUnlock(prefs);
	if (set != NULL) CFRelease(set);
	return updated;
}


static void
watchSCDynamicStore()
{
	CFMutableArrayRef	keys;
	Boolean			ok;
	CFRunLoopSourceRef	rls;

	/*
	 * watch for KernelEventMonitor[.bundle] changes (the list of
	 * active network interfaces)
	 */
	interfacesKey = SCDynamicStoreKeyCreateNetworkInterface(NULL,
								kSCDynamicStoreDomainState);

	/*
	 * watch for InterfaceNamer[.bundle] changes (quiet, timeout,
	 * and the list of pre-configured interfaces)
	 */
	namerKey = SCDynamicStoreKeyCreate(NULL,
					   CFSTR("%@" "InterfaceNamer"),
					   kSCDynamicStoreDomainPlugin);

	rls = SCDynamicStoreCreateRunLoopSource(NULL, store, 0);
	if (rls == NULL) {
		SC_log(LOG_NOTICE, "SCDynamicStoreCreateRunLoopSource() failed: %s", SCErrorString(SCError()));
		haveConfiguration = TRUE;
		return;
	}
	CFRunLoopAddSource(CFRunLoopGetCurrent(), rls, kCFRunLoopDefaultMode);
	CFRelease(rls);

	keys = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	CFArrayAppendValue(keys, interfacesKey);
	CFArrayAppendValue(keys, namerKey);
	ok = SCDynamicStoreSetNotificationKeys(store, keys, NULL);
	CFRelease(keys);
	if (!ok) {
		SC_log(LOG_NOTICE, "SCDynamicStoreSetNotificationKeys() failed: %s", SCErrorString(SCError()));
		haveConfiguration = TRUE;
	}

	return;
}




static Boolean
previousConfigurationAvailable()
{
	CFStringRef		backupKey = NULL;
	CFStringRef		currentModel = NULL;
	CFPropertyListRef	properties = NULL;

	currentModel = _SC_hw_model(FALSE);
	if (currentModel == NULL) {
		goto done;
	}

	/* Currently relying only if a backup of "Sets" is present */
	backupKey = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@:Sets"), currentModel);
	properties = SCPreferencesGetValue(prefs, backupKey);
	CFRelease(backupKey);
done:
	return (properties != NULL);
}


static void
storeCallback(SCDynamicStoreRef store, CFArrayRef changedKeys, void *info)
{
	CFDictionaryRef	dict;
	CFArrayRef	interfaces	= NULL;
	Boolean		quiet		= FALSE;
	Boolean		timeout		= FALSE;
	Boolean		updated		= FALSE;

	/*
	 * Capture/process KernelEventMonitor[.bundle] info
	 * 1. get list of active network interfaces
	 */
	dict = SCDynamicStoreCopyValue(store, interfacesKey);
	if (dict != NULL) {
		if (isA_CFDictionary(dict)) {
			interfaces = CFDictionaryGetValue(dict, kSCPropNetInterfaces);
			interfaces = isA_CFArray(interfaces);
			if (interfaces != NULL) {
				CFRetain(interfaces);
			}
		}

		CFRelease(dict);
	}

	/*
	 * Capture/process InterfaceNamer[.bundle] info
	 * 1. check if IORegistry "quiet", "timeout"
	 * 2. get list of named pre-configured interfaces
	 * 3. merge list of active interfaces (from KEV) with the
	 *    list of preconfigured interfaces.
	 */
	dict = SCDynamicStoreCopyValue(store, namerKey);
	if (dict != NULL) {
		if (isA_CFDictionary(dict)) {
			CFArrayRef		cur_preconfigured;
			CFMutableArrayRef	new_preconfigured	= NULL;

			if (CFDictionaryContainsKey(dict, kInterfaceNamerKey_Quiet)) {
				quiet = TRUE;
			}
			if (CFDictionaryContainsKey(dict, kInterfaceNamerKey_Timeout)) {
				timeout = TRUE;
			}

			cur_preconfigured = CFDictionaryGetValue(dict, kInterfaceNamerKey_PreConfiguredInterfaces);
			cur_preconfigured = isA_CFArray(cur_preconfigured);
			if ((cur_preconfigured != NULL) && (interfaces != NULL)) {
				CFIndex		i;
				CFIndex		n;
				CFRange		r	= CFRangeMake(0, CFArrayGetCount(interfaces));

				n = CFArrayGetCount(cur_preconfigured);
				for (i = 0; i < n; i++) {
					CFStringRef	bsdName;

					bsdName = CFArrayGetValueAtIndex(cur_preconfigured, i);
					if (!CFArrayContainsValue(interfaces, r, bsdName)) {
						// if interface not currently active
						continue;
					}

					if (new_preconfigured == NULL) {
						new_preconfigured = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
					}
					CFArrayAppendValue(new_preconfigured, bsdName);
				}
			}

			if (!_SC_CFEqual(preconfigured, new_preconfigured)) {
				SC_log(LOG_INFO, "pre-configured interface list changed");

				if (preconfigured != NULL) {
					CFRelease(preconfigured);
				}
				if (new_preconfigured != NULL) {
					CFRetain(new_preconfigured);
				}
				preconfigured = new_preconfigured;

				updated = TRUE;
			}

			if (new_preconfigured != NULL) {
				CFRelease(new_preconfigured);
			}
		}

		CFRelease(dict);
	}

	if (interfaces != NULL) {
		CFRelease(interfaces);
	}

	if (!haveConfiguration && (quiet || timeout)) {
		static int	logged	= 0;

		if (quiet
#if	!TARGET_OS_IPHONE
		    || timeout
#endif	/* !TARGET_OS_IPHONE */
		    ) {
			haveConfiguration = TRUE;
		}

		(void) establishNewPreferences();

		if (restorePrefs) {
			(void) restorePreferences();
			restorePrefs = FALSE;
		}

		if (timeout && (logged++ == 0)) {
			SC_log(LOG_ERR, "Network configuration creation timed out waiting for IORegistry");
		}
	}

	if (updated && (changedKeys != NULL)) {
		// if pre-configured interface list changed
		updateConfiguration(prefs, kSCPreferencesNotificationApply, (void *)store);
	}

	return;
}


static void
updateCache(const void *key, const void *value, void *context)
{
	CFStringRef		configKey	= (CFStringRef)key;
	CFPropertyListRef	configData	= (CFPropertyListRef)value;
	CFPropertyListRef	cacheData;
	CFIndex			i;

	cacheData = CFDictionaryGetValue(currentPrefs, configKey);
	if (cacheData) {
		/* key exists */
		if (CFEqual(cacheData, configData)) {
			/*
			 * if the old & new property list values have
			 * not changed then we don't need to update
			 * the preference.
			 */
			CFArrayAppendValue(unchangedPrefsKeys, configKey);
		}
	}

	/* in any case, this key should not be removed */
	i = CFArrayGetFirstIndexOfValue(removedPrefsKeys,
					CFRangeMake(0, CFArrayGetCount(removedPrefsKeys)),
					configKey);
	if (i != kCFNotFound) {
		CFArrayRemoveValueAtIndex(removedPrefsKeys, i);
	}

	return;
}


static void
flatten(SCPreferencesRef	prefs,
	CFStringRef		key,
	CFDictionaryRef		base)
{
	CFDictionaryRef		subset;
	CFStringRef		link;
	CFMutableDictionaryRef	myDict;
	CFStringRef		myKey;
	CFIndex			i;
	CFIndex			nKeys;
	const void		**keys;
	const void		**vals;

	if (!CFDictionaryGetValueIfPresent(base, kSCResvLink, (const void **)&link)) {
		/* if this dictionary is not linked */
		subset = base;
	} else {
		/* if __LINK__ key is present */
		subset = SCPreferencesPathGetValue(prefs, link);
		if (!subset) {
			/* if error with link */
			SC_log(LOG_NOTICE, "SCPreferencesPathGetValue(,%@,) failed: %s",
			       link,
			       SCErrorString(SCError()));
			return;
		}
	}

	if (CFDictionaryContainsKey(subset, kSCResvInactive)) {
		/* if __INACTIVE__ key is present */
		return;
	}

	myKey = CFStringCreateWithFormat(NULL,
					 NULL,
					 CFSTR("%@%@"),
					 kSCDynamicStoreDomainSetup,
					 key);

	myDict = (CFMutableDictionaryRef)CFDictionaryGetValue(newPrefs, myKey);
	if (myDict) {
		myDict = CFDictionaryCreateMutableCopy(NULL,
						       0,
						       (CFDictionaryRef)myDict);
	} else {
		myDict = CFDictionaryCreateMutable(NULL,
						   0,
						   &kCFTypeDictionaryKeyCallBacks,
						   &kCFTypeDictionaryValueCallBacks);
	}

	nKeys = CFDictionaryGetCount(subset);
	if (nKeys > 0) {
		keys  = CFAllocatorAllocate(NULL, nKeys * sizeof(CFStringRef)      , 0);
		vals  = CFAllocatorAllocate(NULL, nKeys * sizeof(CFPropertyListRef), 0);
		CFDictionaryGetKeysAndValues(subset, keys, vals);
		for (i = 0; i < nKeys; i++) {
			if (CFGetTypeID((CFTypeRef)vals[i]) != CFDictionaryGetTypeID()) {
				/* add this key/value to the current dictionary */
				CFDictionarySetValue(myDict, keys[i], vals[i]);
			} else {
				CFStringRef	subKey;

				/* flatten [sub]dictionaries */
				subKey = CFStringCreateWithFormat(NULL,
								  NULL,
								  CFSTR("%@%s%@"),
								  key,
								  CFEqual(key, CFSTR("/")) ? "" : "/",
								  keys[i]);
				flatten(prefs, subKey, vals[i]);
				CFRelease(subKey);
			}
		}
		CFAllocatorDeallocate(NULL, keys);
		CFAllocatorDeallocate(NULL, vals);
	}

	if (CFDictionaryGetCount(myDict) > 0) {
		/* add this dictionary to the new preferences */
		CFDictionarySetValue(newPrefs, myKey, myDict);
	}

	CFRelease(myDict);
	CFRelease(myKey);

	return;
}


static CF_RETURNS_RETAINED SCNetworkServiceRef
copyInterfaceService(SCNetworkSetRef set, CFStringRef matchName)
{
	CFIndex			i;
	CFIndex			n;
	SCNetworkServiceRef	service	= NULL;
	CFArrayRef		services;

	services = SCNetworkSetCopyServices(set);
	assert(services != NULL);

	n = CFArrayGetCount(services);
	for (i = 0; i < n; i++) {
		SCNetworkInterfaceRef	interface;

		service = CFArrayGetValueAtIndex(services, i);
		interface = SCNetworkServiceGetInterface(service);
		if (interface != NULL) {
			CFStringRef		bsdName;

			bsdName = SCNetworkInterfaceGetBSDName(interface);
			if (_SC_CFEqual(bsdName, matchName)) {
				// if match
				CFRetain(service);
				break;
			}
		}

		service = NULL;
	}

	CFRelease(services);
	return service;
}


static CF_RETURNS_RETAINED CFStringRef
copyInterfaceUUID(CFStringRef bsdName)
{
	union {
		unsigned char	sha1_bytes[CC_SHA1_DIGEST_LENGTH];
		CFUUIDBytes	uuid_bytes;
	} bytes;
	CC_SHA1_CTX	ctx;
	char		if_name[IF_NAMESIZE];
	CFUUIDRef	uuid;
	CFStringRef	uuid_str;

	// start with interface name
	bzero(&if_name, sizeof(if_name));
	(void) _SC_cfstring_to_cstring(bsdName,
				       if_name,
				       sizeof(if_name),
				       kCFStringEncodingASCII);

	// create SHA1 hash
	bzero(&bytes, sizeof(bytes));
	CC_SHA1_Init(&ctx);
	CC_SHA1_Update(&ctx,
		       if_name,
		       sizeof(if_name));
	CC_SHA1_Final(bytes.sha1_bytes, &ctx);

	// create UUID string
	uuid = CFUUIDCreateFromUUIDBytes(NULL, bytes.uuid_bytes);
	uuid_str = CFUUIDCreateString(NULL, uuid);
	CFRelease(uuid);

	return uuid_str;
}


static void
updatePreConfiguredConfiguration(SCPreferencesRef prefs)
{
	Boolean		ok;
	CFRange		range;
	SCNetworkSetRef	set;
	Boolean		updated	= FALSE;

	range.length = (preconfigured != NULL) ? CFArrayGetCount(preconfigured) : 0;
	if (range.length == 0) {
		// if no [preconfigured] interfaces
		return;
	}
	range.location = 0;

	set = SCNetworkSetCopyCurrent(prefs);
	if (set != NULL) {
		CFArrayRef	services;

		/*
		 * Check for (and remove) and network services associated with
		 * a pre-configured interface from the prefs.
		 */
		services = SCNetworkSetCopyServices(set);
		if (services != NULL) {
			CFIndex		n;

			n = CFArrayGetCount(services);
			for (CFIndex i = 0; i < n; i++) {
				CFStringRef		bsdName;
				SCNetworkInterfaceRef	interface;
				SCNetworkServiceRef	service;

				service = CFArrayGetValueAtIndex(services, i);

				interface = SCNetworkServiceGetInterface(service);
				if (interface == NULL) {
					// if no interface
					continue;
				}

				bsdName = SCNetworkInterfaceGetBSDName(interface);
				if (bsdName == NULL) {
					// if no interface name
					continue;
				}

				if (!CFArrayContainsValue(preconfigured, range, bsdName)) {
					// if not preconfigured
					continue;
				}

				// remove [preconfigured] network service from the prefs
				SC_log(LOG_NOTICE, "removing network service for %@", bsdName);
				SCNetworkServiceRemove(service);
				updated = TRUE;
			}

			CFRelease(services);
		}

		if (updated) {
			// commit the updated prefs ... but don't apply
			ok = SCPreferencesCommitChanges(prefs);
			if (!ok) {
				if (SCError() != EROFS) {
					SC_log(LOG_NOTICE, "SCPreferencesCommitChanges() failed: %s",
					       SCErrorString(SCError()));
				}
			}
		}

		/*
		 * Now, add a new network service for each pre-configured interface
		 */
		for (CFIndex i = 0; i < range.length; i++) {
			CFStringRef		bsdName;
			SCNetworkInterfaceRef	interface;
			SCNetworkServiceRef	service;

			bsdName = CFArrayGetValueAtIndex(preconfigured, i);
			interface = _SCNetworkInterfaceCreateWithBSDName(NULL, bsdName, kIncludeNoVirtualInterfaces);
			if (interface == NULL) {
				SC_log(LOG_ERR, "could not create network interface for %@", bsdName);
				continue;
			}

			if (_SCNetworkInterfaceGetIOPath(interface) == NULL) {
				// if no [real] interface exists
				CFRelease(interface);
				continue;
			}

			ok = SCNetworkSetEstablishDefaultInterfaceConfiguration(set, interface);
			CFRelease(interface);
			if (!ok) {
				SC_log(LOG_ERR, "could not create network service for %@", bsdName);
				continue;
			}

			service = copyInterfaceService(set, bsdName);
			if (service != NULL) {
				CFStringRef	serviceID;

				serviceID = copyInterfaceUUID(bsdName);
				if (serviceID != NULL) {
					ok = _SCNetworkServiceSetServiceID(service, serviceID);
					CFRelease(serviceID);
					if (!ok) {
						SC_log(LOG_ERR, "_SCNetworkServiceSetServiceID() failed: %s",
						       SCErrorString(SCError()));
						// ... and keep whatever random UUID was created for the service
					}
				} else {
					SC_log(LOG_ERR, "could not create serviceID for %@", bsdName);
					// ... and we'll use whatever random UUID was created for the service
				}

				SC_log(LOG_INFO, "network service %@ added for %@",
				       SCNetworkServiceGetServiceID(service),
				       bsdName);

				CFRelease(service);
			} else {
				SC_log(LOG_ERR, "could not find network service for %@", bsdName);
			}
		}

		CFRelease(set);
	}

	return;
}


static void
updateSCDynamicStore(SCPreferencesRef prefs)
{
	CFStringRef		current		= NULL;
	CFDateRef		date		= NULL;
	CFMutableDictionaryRef	dict		= NULL;
	CFDictionaryRef		global		= NULL;
	CFIndex			i;
	CFArrayRef		keys;
	CFIndex			n;
	CFStringRef		pattern;
	CFMutableArrayRef	patterns;
	CFDictionaryRef		set		= NULL;

	/*
	 * initialize old preferences, new preferences, an array
	 * of keys which have not changed, and an array of keys
	 * to be removed (cleaned up).
	 */

	patterns = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	pattern  = CFStringCreateWithFormat(NULL,
					    NULL,
					    CFSTR("^%@.*"),
					    kSCDynamicStoreDomainSetup);
	CFArrayAppendValue(patterns, pattern);
	dict = (CFMutableDictionaryRef)SCDynamicStoreCopyMultiple(store, NULL, patterns);
	CFRelease(patterns);
	CFRelease(pattern);
	if (dict) {
		currentPrefs = CFDictionaryCreateMutableCopy(NULL, 0, dict);
		CFRelease(dict);
	} else {
		currentPrefs = CFDictionaryCreateMutable(NULL,
							 0,
							 &kCFTypeDictionaryKeyCallBacks,
							 &kCFTypeDictionaryValueCallBacks);
	}

	unchangedPrefsKeys = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

	i = CFDictionaryGetCount(currentPrefs);
	if (i > 0) {
		const void	**currentKeys;
		CFArrayRef	array;

		currentKeys = CFAllocatorAllocate(NULL, i * sizeof(CFStringRef), 0);
		CFDictionaryGetKeysAndValues(currentPrefs, currentKeys, NULL);
		array = CFArrayCreate(NULL, currentKeys, i, &kCFTypeArrayCallBacks);
		removedPrefsKeys = CFArrayCreateMutableCopy(NULL, 0, array);
		CFRelease(array);
		CFAllocatorDeallocate(NULL, currentKeys);
	} else {
		removedPrefsKeys = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	}

	/*
	 * The "newPrefs" dictionary will contain the new / updated
	 * configuration which will be written to the configuration cache.
	 */
	newPrefs = CFDictionaryCreateMutable(NULL,
						 0,
						 &kCFTypeDictionaryKeyCallBacks,
						 &kCFTypeDictionaryValueCallBacks);

	/*
	 * create status dictionary associated with current configuration
	 * information including:
	 *   - current set "name" to cache
	 *   - time stamp indicating when the cache preferences were
	 *     last updated.
	 */
	dict = CFDictionaryCreateMutable(NULL,
					 0,
					 &kCFTypeDictionaryKeyCallBacks,
					 &kCFTypeDictionaryValueCallBacks);
	date = CFDateCreate(NULL, CFAbsoluteTimeGetCurrent());

	/*
	 * load preferences
	 */
	keys = SCPreferencesCopyKeyList(prefs);
	if ((keys == NULL) || (CFArrayGetCount(keys) == 0)) {
		SC_log(LOG_NOTICE, "updateConfiguration(): no preferences");
		goto done;
	}

	/*
	 * get "global" system preferences
	 */
	global = SCPreferencesGetValue(prefs, kSCPrefSystem);
	if (!global) {
		/* if no global preferences are defined */
		goto getSet;
	}

	if (!isA_CFDictionary(global)) {
		SC_log(LOG_NOTICE, "updateConfiguration(): %@ is not a dictionary",
		       kSCPrefSystem);
		goto done;
	}

	/* flatten property list */
	flatten(prefs, CFSTR("/"), global);

    getSet :

	/*
	 * get current set name
	 */
	current = SCPreferencesGetValue(prefs, kSCPrefCurrentSet);
	if (!current) {
		/* if current set not defined */
		goto done;
	}

	if (!isA_CFString(current)) {
		SC_log(LOG_NOTICE, "updateConfiguration(): %@ is not a string",
		       kSCPrefCurrentSet);
		goto done;
	}

	/*
	 * get current set
	 */
	set = SCPreferencesPathGetValue(prefs, current);
	if (!set) {
		/* if error with path */
		SC_log(LOG_NOTICE, "%@ value (%@) not valid",
		       kSCPrefCurrentSet,
		       current);
		goto done;
	}

	if (!isA_CFDictionary(set)) {
		SC_log(LOG_NOTICE, "updateConfiguration(): %@ is not a dictionary",
		       current);
		goto done;
	}

	/* flatten property list */
	flatten(prefs, CFSTR("/"), set);

	CFDictionarySetValue(dict, kSCDynamicStorePropSetupCurrentSet, current);

    done :

	/* add last updated time stamp */
	CFDictionarySetValue(dict, kSCDynamicStorePropSetupLastUpdated, date);

	/* add Setup: key */
	CFDictionarySetValue(newPrefs, kSCDynamicStoreDomainSetup, dict);

	/* compare current and new preferences */
	CFDictionaryApplyFunction(newPrefs, updateCache, NULL);

	/* remove those keys which have not changed from the update */
	n = CFArrayGetCount(unchangedPrefsKeys);
	for (i = 0; i < n; i++) {
		CFStringRef	key;

		key = CFArrayGetValueAtIndex(unchangedPrefsKeys, i);
		CFDictionaryRemoveValue(newPrefs, key);
	}

	/* Update the dynamic store */
#ifndef MAIN
	if (!SCDynamicStoreSetMultiple(store, newPrefs, removedPrefsKeys, NULL)) {
		SC_log(LOG_NOTICE, "SCDynamicStoreSetMultiple() failed: %s", SCErrorString(SCError()));
	}
#else	// !MAIN
	SC_log(LOG_DEBUG, "SCDynamicStore\nset: %@\nremove: %@",
	       newPrefs,
	       removedPrefsKeys);
#endif	// !MAIN

	CFRelease(currentPrefs);
	CFRelease(newPrefs);
	CFRelease(unchangedPrefsKeys);
	CFRelease(removedPrefsKeys);
	if (dict)	CFRelease(dict);
	if (date)	CFRelease(date);
	if (keys)	CFRelease(keys);
	return;
}


static void
updateConfiguration(SCPreferencesRef		prefs,
		    SCPreferencesNotification   notificationType,
		    void			*info)
{
	os_activity_t	activity;

	activity = os_activity_create("processing [SC] preferences.plist changes",
				      OS_ACTIVITY_CURRENT,
				      OS_ACTIVITY_FLAG_DEFAULT);
	os_activity_scope(activity);

#if	!TARGET_OS_IPHONE
	if ((notificationType & kSCPreferencesNotificationCommit) == kSCPreferencesNotificationCommit) {
		SCNetworkSetRef	current;

		current = SCNetworkSetCopyCurrent(prefs);
		if (current != NULL) {
			/* network configuration available, disable template creation */
			haveConfiguration = TRUE;
			CFRelease(current);
		}
	}
#endif	/* !TARGET_OS_IPHONE */

	if ((notificationType & kSCPreferencesNotificationApply) != kSCPreferencesNotificationApply) {
		goto done;
	}

	SC_log(LOG_INFO, "updating configuration");

	/* add any [Apple] pre-configured network services */
	updatePreConfiguredConfiguration(prefs);

	/* update SCDynamicStore (Setup:) */
	updateSCDynamicStore(prefs);

	/* finished with current prefs, wait for changes */
	if (!rofs) {
		SCPreferencesSynchronize(prefs);
	}

    done :

	os_release(activity);

	return;
}


__private_extern__
void
prime_PreferencesMonitor()
{
	SC_log(LOG_DEBUG, "prime() called");

	/* load the initial configuration from the database */
	updateConfiguration(prefs, kSCPreferencesNotificationApply, (void *)store);

	return;
}


__private_extern__
void
load_PreferencesMonitor(CFBundleRef bundle, Boolean bundleVerbose)
{
	SC_log(LOG_DEBUG, "load() called");
	SC_log(LOG_DEBUG, "  bundle ID = %@", CFBundleGetIdentifier(bundle));

	/* open a SCDynamicStore session to allow cache updates */
	store = SCDynamicStoreCreate(NULL,
				     CFSTR("PreferencesMonitor.bundle"),
				     storeCallback,
				     NULL);
	if (store == NULL) {
		SC_log(LOG_NOTICE, "SCDynamicStoreCreate() failed: %s", SCErrorString(SCError()));
		goto error;
	}

	/* open a SCPreferences session */
#ifndef	MAIN
	prefs = SCPreferencesCreate(NULL, CFSTR("PreferencesMonitor.bundle"), NULL);
#else	// !MAIN
	prefs = SCPreferencesCreate(NULL, CFSTR("PreferencesMonitor.bundle"), CFSTR("/tmp/preferences.plist"));
#endif	// !MAIN
	if (prefs != NULL) {
		Boolean		need_update = FALSE;
		CFStringRef	new_model;

		new_model = _SC_hw_model(FALSE);

		/* Need to regenerate the new configuration for new model */
		if (new_model != NULL) {
			CFStringRef	old_model;

			old_model = SCPreferencesGetValue(prefs, MODEL);
			if (old_model != NULL && !_SC_CFEqual(old_model, new_model)) {
				// if new hardware
				need_update = TRUE;
				restorePrefs = previousConfigurationAvailable();
			}
		}

		if (!need_update) {
			SCNetworkSetRef current;

			current = SCNetworkSetCopyCurrent(prefs);
			if (current != NULL) {
				/* network configuration available, disable template creation */
				haveConfiguration = TRUE;
				CFRelease(current);
			}
		}
	} else {
		SC_log(LOG_NOTICE, "SCPreferencesCreate() failed: %s", SCErrorString(SCError()));
		goto error;
	}

	/*
	 * register for change notifications.
	 */
	if (!SCPreferencesSetCallback(prefs, updateConfiguration, NULL)) {
		SC_log(LOG_NOTICE, "SCPreferencesSetCallBack() failed: %s", SCErrorString(SCError()));
		goto error;
	}

	if (!SCPreferencesScheduleWithRunLoop(prefs, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode)) {
		SC_log(LOG_NOTICE, "SCPreferencesScheduleWithRunLoop() failed: %s", SCErrorString(SCError()));
		goto error;
	}

	/*
	 * watch InterfaceNamer and KernelEventMonitor changes to know when
	 * the IORegistry has quiesced (to create the initial configuration
	 * template), to track any pre-configured interfaces, and to ensure
	 * that we create a network service for any active interfaces.
	 */
	watchSCDynamicStore();
	storeCallback(store, NULL, NULL);

	return;

    error :

	if (store != NULL)	CFRelease(store);
	if (prefs != NULL)	CFRelease(prefs);
	haveConfiguration = TRUE;

	return;
}


#ifdef  MAIN
int
main(int argc, char **argv)
{
	_sc_log     = FALSE;
	_sc_verbose = (argc > 1) ? TRUE : FALSE;

	load_PreferencesMonitor(CFBundleGetMainBundle(), (argc > 1) ? TRUE : FALSE);
	prime_PreferencesMonitor();
	CFRunLoopRun();
	/* not reached */
	exit(0);
	return 0;
}
#endif
