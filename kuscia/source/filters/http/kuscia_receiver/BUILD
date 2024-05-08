load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

package(default_visibility = ["//visibility:public"])

envoy_cc_library(
    name = "receiver_filter",
    srcs = [
        "receiver_filter.cc",
    ],
    hdrs = [
        "receiver_filter.h",
        "event_loop.h",
        "conn.h",
        "svc_register.h",
        "event.h",
    ],
    repository = "@envoy",
    deps = [
        "//kuscia/api/filters/http/kuscia_receiver/v3:pkg_cc_proto",
        "//kuscia/source/filters/http/kuscia_common",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:base64_lib",
        "@envoy//source/common/http:header_utility_lib",
        "@envoy//source/common/http:headers_lib",
        "@envoy//source/common/http:message_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy//source/common/http:codes_lib",
        "@envoy//source/common/http:utility_lib",
        "@com_github_nlohmann_json//:json",
        "@envoy//source/common/api:os_sys_calls_lib",
        "@envoy//envoy/network:connection_interface",
        "@envoy//source/common/network:connection_lib",
        "@envoy//source/common/common:random_generator_lib",
        "@envoy//source/common/upstream:cluster_manager_lib",
        "@envoy//envoy/http:async_client_interface",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
        "@envoy//source/common/common:hex_lib",
        # "@envoy//source/extensions/tracers/zipkin:zipkin_lib",
    ],
    external_deps = [
        "curl",
        "http_parser",
    ],
)

envoy_cc_library(
    name = "kuscia_receiver_config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":receiver_filter",
        "@envoy//envoy/registry",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
