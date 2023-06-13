load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
)

envoy_cc_test(
    name = "kuscia_header_decorator_filter_test",
    srcs = ["header_decorator_filter_test.cc"],
    repository = "@envoy",
    deps = [
        "//kuscia/source/filters/http/kuscia_header_decorator:header_decorator_filter",
        "//kuscia/test/filters/http/test_common:test_common",
	"@envoy//test/mocks/http:http_mocks",
    ],
)
