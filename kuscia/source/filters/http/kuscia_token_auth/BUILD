load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

package(default_visibility = ["//visibility:public"])

envoy_cc_library(
    name = "token_auth_filter",
    srcs = ["token_auth_filter.cc"],
    hdrs = ["token_auth_filter.h"],
    repository = "@envoy",
    deps = [
        "//kuscia/api/filters/http/kuscia_token_auth/v3:pkg_cc_proto",
        "//kuscia/source/filters/http/kuscia_common",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/http:header_utility_lib",
	"@envoy//source/common/http:utility_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "kuscia_token_auth_config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":token_auth_filter",
        "@envoy//envoy/registry",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
