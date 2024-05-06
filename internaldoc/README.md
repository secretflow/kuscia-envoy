## 说明
本说明文档仅限内部使用，不会带到开源仓库。

## 开发环境搭建
安装 golang

```shell
# mac
wget https://go.dev/dl/go1.19.4.darwin-amd64.tar.gz
sudo tar -C /usr/local -zxvf go1.19.4.darwin-amd64.tar.gz

# linux
wget https://go.dev/dl/go1.19.4.linux-amd64.tar.gz
sudo tar -C /usr/local -zxvf go1.19.4.linux-amd64.tar.gz
```

添加环境变量
```shell
export GOPROXY="https://goproxy.cn,direct"
export GO111MODULE=on
export GOPATH="$HOME/gopath"
export PATH="$PATH:$GOPATH/bin:/usr/local/go/bin"
export GOPRIVATE="gitlab.alipay-inc.com"
```

bazel_tools的java依赖
```shell
sudo apt install default-jdk
sudo update-alternatives --config javac
```

gazelle的安装与使用
```shell
# 安装
go install github.com/bazelbuild/bazel-gazelle/cmd/gazelle@latest
# 生成BUILD(在根目录下执行）
gazelle -exclude envoy -exclude bazel -exclude internaldoc -build_file_name BUILD -repo_root=.
# 生成依赖
gazelle  update-repos -from_file=go.mod -to_macro=deps.bzl%go_dependencie
# 部分依赖需要手动禁止编译proto
sed  -i '/.*name = "\(io_k8s_api\|io_k8s_apimachinery\|com_github_google_gnostic\)",/a\        build_file_proto_mode = "disable_global",' deps.bzl
```

git 配置
```shell
git config --global user.name "xxx"
git config --global user.email "xxx@antgroup.com"

# http/https 免密码登录（首次需要密码验证）
git config --global credential.helper store

# 通过 ssh 访问 gitlab，需要加这行配置
git config --global url."git@gitlab.alipay-inc.com:".insteadOf "http://gitlab.alipay-inc.com/"
```
ssh 配置
```shell
# 生成 ssh 公私钥
ssh-keygen -t rsa -b 4096 -f ~/.ssh/id_rsa -N ''

# 将公钥内容填至 AntCode->设置->SSH密钥
cat ~/.ssh/id_rsa.pub
```
