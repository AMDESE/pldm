oem_amd_pldm_sources += files('platform-mc/platform_manager.cpp')

oem_amd_dep = declare_dependency(
    sources: files(
        'rde/cache_manager_dbus.cpp',
        'rde/rde_cache_manager.cpp',
    ),
    include_directories: include_directories('.', 'rde')
)

