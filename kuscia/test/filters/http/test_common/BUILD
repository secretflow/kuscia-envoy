load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

package(default_visibility = ["//visibility:public"])

envoy_cc_library(
    name = "test_common",
    hdrs = [
        "header_checker.h",
    ],
    repository = "@envoy",
    deps = [
        "//kuscia/source/filters/http/kuscia_common",
        "@envoy//source/common/http:header_utility_lib",
    ],
)
