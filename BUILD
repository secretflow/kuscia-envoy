package(default_visibility = ["//visibility:public"])

load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
)

envoy_cc_binary(
    name = "envoy",
    repository = "@envoy",
    deps = [
        "//kuscia/source/filters/http/kuscia_gress:kuscia_gress_config",
        "//kuscia/source/filters/http/kuscia_crypt:kuscia_crypt_config",
        "//kuscia/source/filters/http/kuscia_token_auth:kuscia_token_auth_config",
        "//kuscia/source/filters/http/kuscia_header_decorator:kuscia_header_decorator_config",
        "//kuscia/source/filters/http/kuscia_poller:kuscia_poller_config",
        "//kuscia/source/filters/http/kuscia_receiver:kuscia_receiver_config",
        "@envoy//source/exe:envoy_main_entry_lib",
    ],
)

