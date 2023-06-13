load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
)

envoy_cc_test(
    name = "kuscia_gress_filter_test",
    srcs = ["gress_filter_test.cc"],
    repository = "@envoy",
    deps = [
        "//kuscia/source/filters/http/kuscia_gress:gress_filter",
        "//kuscia/test/filters/http/test_common:test_common",
	"@envoy//test/mocks/http:http_mocks",
    ],
)
