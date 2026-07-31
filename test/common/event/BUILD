load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "drr_post_callback_queue_test",
    srcs = ["drr_post_callback_queue_test.cc"],
    deps = [
        "//source/common/event:drr_post_callback_queue_lib",
    ],
)

envoy_cc_test(
    name = "dispatcher_impl_test",
    srcs = ["dispatcher_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/api:api_lib",
        "//source/common/event:deferred_task",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks/server:watch_dog_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "libevent_scheduler_test",
    srcs = ["libevent_scheduler_test.cc"],
    deps = [
        "//source/common/event:libevent_scheduler_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "evwatch_observer_manager_impl_test",
    srcs = ["evwatch_observer_manager_impl_test.cc"],
    deps = [
        "//source/common/event:evwatch_observer_manager_impl_lib",
        "//source/common/event:libevent_scheduler_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "file_event_impl_test",
    srcs = ["file_event_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/event:file_event_interface",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "scaled_range_timer_manager_impl_test",
    srcs = ["scaled_range_timer_manager_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/event:scaled_range_timer_manager_lib",
        "//test/mocks/event:wrapped_dispatcher",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test_binary(
    name = "drr_goodput_experiment",
    srcs = ["drr_goodput_experiment.cc"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/runtime:runtime_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@tclap",
    ],
)

envoy_cc_benchmark_binary(
    name = "drr_dispatcher_benchmark",
    srcs = ["drr_dispatcher_benchmark.cc"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/common:assert_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/runtime:runtime_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
        "@benchmark",
        "@libcircllhist",
    ],
)
