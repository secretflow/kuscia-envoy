load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

package(default_visibility = ["//visibility:public"])

envoy_cc_library(
    name = "gress_filter",
    srcs = ["gress_filter.cc"],
    hdrs = ["gress_filter.h"],
    repository = "@envoy",
    deps = [
        "//kuscia/api/filters/http/kuscia_gress/v3:pkg_cc_proto",
        "//kuscia/source/filters/http/kuscia_common",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/http:header_utility_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "kuscia_gress_config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":gress_filter",
        "@envoy//envoy/registry",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
	    "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
        "@envoy//source/common/matcher:matcher_lib",
    ],
)
