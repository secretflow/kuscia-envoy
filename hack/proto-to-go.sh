#!/usr/bin/env bash
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

set -ex
set -o errexit
set -o nounset
set -o pipefail

PROTOC=protoc

KUSCIA_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
echo "${KUSCIA_ROOT}"

PROTO_ROOT_PATH=${KUSCIA_ROOT}/kuscia/api

function pre_install() {
  # install protoc-gen-go tool if not exist
  if [ "$(which protoc-gen-go)" == "" ]; then
    echo "Start to install protoc-gen-go tool"
    GO111MODULE=on go install -v google.golang.org/protobuf/cmd/protoc-gen-go
  fi
}

# $1: proto_dir
# $2: proto_golang_out
function generate_golang_code() {
  proto_dir=$1
  for path in "${proto_dir}"/*
  do
    [[ -e "${path}" ]] || break
    if [ -d "${path}" ]; then
      generate_golang_code "${path}"
    elif [[ ${path} == *.proto ]]; then
      echo "${PROTOC} --proto_path=${KUSCIA_ROOT} --go_opt=paths=source_relative --go_out=./ ${path}"
      ${PROTOC} --proto_path="${KUSCIA_ROOT}" \
                --go_opt=paths=source_relative --go_out=./ \
                "${path}"
    fi
  done
}

function main() {
  pre_install
  generate_golang_code "${PROTO_ROOT_PATH}"
}

main
