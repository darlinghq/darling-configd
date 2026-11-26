// Copyright (C) 2026 Lubos Dolezel
//
// SystemConfiguration: the kSCNetworkInterfaceType* constants carry the documented string values, and the
// SCNetworkService stubs return NULL (documented for "no such service" / "no interface") without crashing.
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <stdio.h>
#include <string.h>

// Exported by SystemConfiguration but declared only in its private headers.
extern const CFStringRef kSCNetworkInterfaceTypeBridge, kSCNetworkInterfaceTypeLoopback, kSCNetworkInterfaceTypeVPN;

static int failures;

static void expect_string(const char *name, CFStringRef value, const char *expected)
{
	char buf[64] = "";
	if (value == NULL || !CFStringGetCString(value, buf, sizeof(buf), kCFStringEncodingUTF8) || strcmp(buf, expected) != 0) {
		printf("FAIL: %s is \"%s\", expected \"%s\"\n", name, value ? buf : "(null)", expected);
		failures++;
	}
}

#define CHECK(c, v) expect_string(#c, c, v)

int main(void)
{
	CHECK(kSCNetworkInterfaceType6to4, "6to4");
	CHECK(kSCNetworkInterfaceTypeBluetooth, "Bluetooth");
	CHECK(kSCNetworkInterfaceTypeBond, "Bond");
	CHECK(kSCNetworkInterfaceTypeBridge, "Bridge");
	CHECK(kSCNetworkInterfaceTypeEthernet, "Ethernet");
	CHECK(kSCNetworkInterfaceTypeFireWire, "FireWire");
	CHECK(kSCNetworkInterfaceTypeIEEE80211, "IEEE80211");
	CHECK(kSCNetworkInterfaceTypeIPSec, "IPSec");
	CHECK(kSCNetworkInterfaceTypeIrDA, "IrDA");
	CHECK(kSCNetworkInterfaceTypeL2TP, "L2TP");
	CHECK(kSCNetworkInterfaceTypeLoopback, "Loopback");
	CHECK(kSCNetworkInterfaceTypeModem, "Modem");
	CHECK(kSCNetworkInterfaceTypePPP, "PPP");
	CHECK(kSCNetworkInterfaceTypePPTP, "PPTP");
	CHECK(kSCNetworkInterfaceTypeSerial, "Serial");
	CHECK(kSCNetworkInterfaceTypeVLAN, "VLAN");
	CHECK(kSCNetworkInterfaceTypeVPN, "VPN");
	CHECK(kSCNetworkInterfaceTypeWWAN, "WWAN");
	CHECK(kSCNetworkInterfaceTypeIPv4, "IPv4");

	// A preferences session that cannot be opened has no services: the lookups report "none" with NULL.
	SCPreferencesRef prefs = SCPreferencesCreate(NULL, CFSTR("sc-network-interface-types"), NULL);
	SCNetworkServiceRef service = SCNetworkServiceCopy(prefs, CFSTR("00000000-0000-0000-0000-000000000000"));
	if (service != NULL) {
		printf("FAIL: SCNetworkServiceCopy returned a service for an unknown identifier\n");
		failures++;
		CFRelease(service);
	}
	if (SCNetworkServiceGetInterface(NULL) != NULL) {
		printf("FAIL: SCNetworkServiceGetInterface(NULL) is not NULL\n");
		failures++;
	}
	if (prefs != NULL)
		CFRelease(prefs);

	if (failures == 0)
		printf("PASS: SCNetworkInterfaceType constants and SCNetworkService stubs\n");
	return failures != 0;
}
