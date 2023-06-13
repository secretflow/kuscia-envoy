# Kuscia-Envoy

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/secretflow/kuscia-envoy/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/secretflow/kuscia-envoy/tree/main)

<p align="center">
<a href="./README.zh-CN.md">简体中文</a>｜<a href="./README.md">English</a>
</p>

Kuscia Envoy 基于开源[Envoy](https://github.com/envoyproxy/envoy)实现了一些自定义的 http 插件.
这些插件是：
- kuscia_gress: 通过重写 host header 重置请求的路由。
- kuscia_token_auth: 通过检查请求头中的 token 对请求进行鉴权
- kuscia_crypt: 对出口数据进行加密、对入口数据进行解密。
- kuscia_header_decorator: 在将请求转发到上游服务之前添加指定的 header。
比如你可以根据请求来源，在请求头中添加对应的 token ，上游服务可以使用 token 对请求进行鉴权。

## 构建

```shell
make build-envoy
```

## 测试

```shell
make test-envoy
```

## 贡献代码

请查阅 [CONTRIBUTING.md](./CONTRIBUTING.md)