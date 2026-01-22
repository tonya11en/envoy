load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "composite_socket_lib",
    srcs = ["composite.cc"],
    hdrs = ["composite.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":composite_socket_lib",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/composite/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "composite_socket_test",
    srcs = ["composite_test.cc"],
    deps = [
        ":composite_socket_lib",
        ":config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)
