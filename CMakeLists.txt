# add_subdirectory(SystemConfiguration.fproj)

project(system_configuration)

set(DYLIB_INSTALL_NAME "/usr/lib/system/libsystem_configuration.dylib")
set(DYLIB_COMPAT_VERSION "1.0.0")
set(DYLIB_CURRENT_VERSION "1109.140.1")

add_definitions(-DPRIVATE)

include_directories(
    config-agent-info
    dnsinfo
    libSystemConfiguration
    nwi
    Plugins/IPMonitor
)

add_darling_library(system_configuration    
    # dnsinfo/dnsinfo_copy.c
    nwi/network_information.c
    libSystemConfiguration/libSystemConfiguration_client.c
    config-agent-info/config_agent_info.c

    # Our implementation of dnsinfo only parses the 
    # /etc/resolv.conf file.
    darling/src/dnsinfo/dnsinfo.c
)

make_fat(system_configuration)
target_link_libraries(system_configuration 
    PRIVATE
        libdispatch_shared
        system_asl
        system_blocks
        system_c
        system_malloc
        system_notify
        platform
        system_pthread
        system_trace
        xpc

        system_dyld
)

install(TARGETS system_configuration DESTINATION libexec/darling/usr/lib/system)
