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
    srcs = [
        "kuscia_header.cc",
        "framer.cc",
        "coder.cc",
    ],
    hdrs = [
        "common.h",
        "kuscia_header.h",
        "framer.h",
        "coder.h",
    ],
    repository = "@envoy",
    deps = [
        "@envoy//envoy/http:header_map_interface",
        "@com_googlesource_code_re2//:re2",
        "@envoy//source/common/buffer:buffer_lib",
    ],
)
