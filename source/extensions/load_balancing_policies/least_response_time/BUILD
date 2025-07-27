load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    extra_visibility = [
        # previously considered core code.
        "//test:__subpackages__",
    ],
    deps = [
        ":least_response_time_lb_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/load_balancing_policies/common:factory_base",
        "@envoy_api//envoy/extensions/load_balancing_policies/least_response_time/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "least_response_time_lb_lib",
    srcs = ["least_response_time_lb.cc"],
    hdrs = ["least_response_time_lb.h"],
    deps = ["//source/extensions/load_balancing_policies/common:load_balancer_lib"],
)
