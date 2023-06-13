load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

package(default_visibility = ["//visibility:public"])

envoy_cc_library(
    name = "crypt_filter",
    srcs = [
        "crypt_filter.cc",
        "crypter.cc",
    ],
    hdrs = [
        "crypt_filter.h",
        "crypter.h",
    ],
    repository = "@envoy",
    deps = [
        "//kuscia/api/filters/http/kuscia_crypt/v3:pkg_cc_proto",
        "//kuscia/source/filters/http/kuscia_common",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:base64_lib",
        "@envoy//source/common/http:header_utility_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "kuscia_crypt_config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":crypt_filter",
        "@envoy//envoy/registry",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
