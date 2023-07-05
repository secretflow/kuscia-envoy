# Kuscia-Envoy

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/secretflow/kuscia-envoy/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/secretflow/kuscia-envoy/tree/main)

<p align="center">
<a href="./README.zh-CN.md">简体中文</a>｜<a href="./README.md">English</a>
</p>

Kuscia Envoy implements several custom http filters based on open source [Envoy](https://github.com/envoyproxy/envoy).
The extension filters are:
- kuscia_gress: rewrite host header to choose a new route.
- kuscia_token_auth: authorize requests by checking token in header.
- kuscia_crypt: enable envoy to encrypt egress data and decrypt ingress data.
- kuscia_header_decorator: add specified headers before forwarding requests to upstream service. For example,
you can assign a token for each source, then the upstream service can use the token to authorize requests.

## build executable files
```shell
make build-envoy

## build image
```shell
make image
```

```

## test
```shell
make test-envoy
```

## Contributing

Please check [CONTRIBUTING.md](./CONTRIBUTING.md)
