#!/bin/bash
#
# Copyright 2023 Ant Group Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

docker_image=envoyproxy/envoy-build-ubuntu:0ca52447572ee105a4730da5e76fe47c9c5a7c64

docker pull $docker_image
bazel_cache_user_root=./.bazel-cache

if [[ x$1 == 'x' ]]; then
  docker exec -it envoyv1.29.4-build-$(whoami)  bash
elif [[ x$1 == 'xinit' ]]; then
  docker run -d -it --name envoyv1.29.4-build-$(whoami) \
    --mount type=bind,source="$(pwd)",target=/home/admin/dev/ -w /home/admin/dev \
    -e HOST_PERMS="$(id -u):$(id -g)" \
    -e GOPROXY='https://goproxy.cn,direct' \
    --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
    --cap-add=NET_ADMIN \
    $docker_image
else
  echo "unknown command"
  exit 1
fi
