rde_test_src = declare_dependency(
    sources: [
        '../redfish_resource_manager.cpp',
        '../redfish_resource_pdr_view.cpp',
        '../../common/utils.cpp',
    ],
)

tests = ['redfish_resource_manager_test', 'redfish_resource_pdr_view_test']

foreach t : tests
    test(
        t,
        executable(
            t.underscorify(),
            t + '.cpp',
            implicit_include_directories: false,
            include_directories: '../../pldmd',
            dependencies: [
                rde_test_src,
                gmock,
                gtest,
                libpldm_dep,
                libpldmutils,
                nlohmann_json_dep,
                phosphor_dbus_interfaces,
                phosphor_logging_dep,
                sdbusplus,
                sdeventplus,
            ],
        ),
        workdir: meson.current_source_dir(),
    )
endforeach
