load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)
load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

package(default_visibility = ["//visibility:public"])

api_proto_package()

#"@envoy//envoy/http:header_map_interface",
envoy_cc_library(
    name = "kuscia_common",
    hdrs = [
        "common.h",
        "kuscia_header.h",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//envoy/http:header_map_interface",
    ],
)
