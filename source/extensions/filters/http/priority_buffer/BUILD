load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "priority_buffer_filter_lib",
    srcs = [
        "buffer_queue.cc",
        "priority_buffer_filter.cc",
    ],
    hdrs = [
        "buffer_queue.h",
        "priority_buffer_filter.h",
    ],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/priority_buffer/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/priority_buffer:priority_buffer_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/priority_buffer/v3:pkg_cc_proto",
    ],
)
