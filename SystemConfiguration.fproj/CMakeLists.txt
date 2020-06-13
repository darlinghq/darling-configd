project(SystemConfiguration)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -nostdinc -Wno-gcc-compat -Wno-ignored-attributes")

include(darling_framework)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/helper
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_BINARY_DIR}/helper
	${CMAKE_CURRENT_SOURCE_DIR}/../Plugins/common
)

add_definitions(-DPRIVATE)

mig(config.defs)
mig(helper/helper.defs)

set(sc_sources
	helper/SCHelper_client.c
	#BondConfiguration.c
	#BridgeConfiguration.c
	#CaptiveNetwork.c
	#DHCP.c
	${CMAKE_CURRENT_BINARY_DIR}/configUser.c
	${CMAKE_CURRENT_BINARY_DIR}/helper/helperUser.c
	dy_framework.c
	genSCPreferences.c
	#LinkConfiguration.c
	SCDAdd.c
	SCD.c
	SCDConsoleUser.c
	SCDGet.c
	SCDHostName.c
	SCDKeys.c
	SCDList.c
	SCDNotifierAdd.c
	SCDNotifierCancel.c
	SCDNotifierGetChanges.c
	SCDNotifierInformViaCallback.c
	SCDNotifierInformViaFD.c
	SCDNotifierInformViaSignal.c
	SCDNotifierRemove.c
	SCDNotifierSetKeys.c
	SCDNotifierWait.c
	SCDNotify.c
	SCDOpen.c
	SCDPlugin.c
	SCDPrivate.c
	SCDRemove.c
	SCDSet.c
	SCDSnapshot.c
	SCLocation.c
	SCNetwork.c
	SCNetworkConfigurationInternal.c
	SCNetworkConnection.c
	SCNetworkConnectionPrivate.c
	SCNetworkInterface.c
	SCNetworkMigration.c
	SCNetworkProtocol.c
	SCNetworkReachability.c
	SCNetworkService.c
	SCNetworkSet.c
	SCNetworkSignature.c
	SCPAdd.c
	SCPApply.c
	SCP.c
	SCPCommit.c
	SCPGet.c
	SCPList.c
	SCPLock.c
	SCPOpen.c
	SCPPath.c
	SCPreferencesKeychainPrivate.c
	SCPreferencesPathKey.c
	scprefs_observer.c
	SCPRemove.c
	SCProxies.c
	SCPSet.c
	SCPUnlock.c
	SCSchemaDefinitions.c
	SNHelper.c
	VLANConfiguration.c
	VPNAppLayer.c
	VPNConfiguration.c
	VPNFlow.c
	VPNPrivate.c
	VPNService.c
	VPNTunnel.c
)

add_framework(SystemConfiguration
	FAT
	CURRENT_VERSION
	SOURCES
		${sc_sources}
	VERSION "A"
	DEPENDENCIES
		system
		CoreFoundation
)

