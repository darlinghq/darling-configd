# SystemConfiguration interface types and service lookups

`sc-network-interface-types.c` links against SystemConfiguration and checks that every public
`kSCNetworkInterfaceType*` constant exists with its documented string value, and that
`SCNetworkServiceCopy()` / `SCNetworkServiceGetInterface()` return NULL (no such service, no interface)
and do not crash.

Build with `guest-cc.sh -o t sc-network-interface-types.c -framework CoreFoundation -framework SystemConfiguration`
(x86_64 and `-a i386`) and run in the guest. Without the change the link fails on the missing symbols.
The constant values were also read on macOS 26.7.3 with a probe that prints them
(`vd-upstream/mac-probes/sp7-sc-interface-types.c`).
