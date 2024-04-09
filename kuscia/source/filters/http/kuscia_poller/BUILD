load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

package(default_visibility = ["//visibility:public"])

envoy_cc_library(
    name = "poller_filter",
    srcs = [
        "callbacks.cc",
        "poller_filter.cc",
    ],
    hdrs = [
        "common.h",
        "callbacks.h",
        "poller_filter.h",
    ],
    repository = "@envoy",
    deps = [
        "//kuscia/api/filters/http/kuscia_poller/v3:pkg_cc_proto",
        "//kuscia/api/filters/http/kuscia_receiver/v3:pkg_cc_proto",
        "//kuscia/source/filters/http/kuscia_common",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:base64_lib",
        "@envoy//source/common/http:header_utility_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "kuscia_poller_config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "common.h",
        "config.h",
    ],
    repository = "@envoy",
    deps = [
        ":poller_filter",
        "@envoy//envoy/registry",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
