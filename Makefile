SHELL := /bin/bash
BUILD_IMAGE = envoyproxy/envoy-build-ubuntu:81a93046060dbe5620d5b3aa92632090a9ee4da6

# Image URL to use all building image targets
DATETIME = $(shell date +"%Y%m%d%H%M%S")
KUSCIA_VERSION_TAG = $(shell git describe --abbrev=7 --always)
COMMIT_ID = $(shell git log -1 --pretty="format:%h")
TAG = ${KUSCIA_VERSION_TAG}-${DATETIME}-${COMMIT_ID}
IMG ?= secretflow/kuscia-envoy:${TAG}

# Get current architecture information
UNAME_M_OUTPUT := $(shell uname -m)

# To configure the ARCH variable to either arm64 or amd64 or UNAME_M_OUTPUT
ARCH := $(if $(filter aarch64 arm64,$(UNAME_M_OUTPUT)),arm64,$(if $(filter amd64 x86_64,$(UNAME_M_OUTPUT)),amd64,$(UNAME_M_OUTPUT)))

CONTAINER_NAME ?= "build-envoy"
COMPILE_MODE ?=opt
TARGET ?= "//:envoy"
BUILD_OPTS ?="--strip=always"

TEST_COMPILE_MODE = fastbuild
TEST_TARGET ?= "//kuscia/test/..."
TEST_LOG_LEVEL = debug

define start_docker
	if [ ! -f  "./envoy/BUILD" ]; then\
		git submodule update --init;\
	fi;
	if [[ ! -n $$(docker ps -q -f "name=^$(CONTAINER_NAME)$$") ]]; then\
		docker run -itd --rm -v $(shell pwd):/home/admin/dev -v $(shell pwd)/cache:/root/.cache/bazel -w /home/admin/dev --name $(CONTAINER_NAME) \
		-e GOPROXY='https://goproxy.cn,direct' --cap-add=NET_ADMIN $(BUILD_IMAGE);\
		docker exec -it $(CONTAINER_NAME) /bin/bash -c 'git config --global --add safe.directory /home/admin/dev';\
	fi;

endef

define stop_docker
	if [[ -n $$(docker ps -q -f "name=^$(CONTAINER_NAME)$$") ]]; then\
		docker stop $(CONTAINER_NAME);
	fi;
endef

.PHONY: build-envoy
build-envoy:
	@$(call start_docker)
	docker exec -it ${CONTAINER_NAME} make build-envoy-local
	docker exec -it ${CONTAINER_NAME} strip -s /home/admin/dev/bazel-bin/envoy
	mkdir -p output/linux/${ARCH}/bin
	mkdir -p output/linux/${ARCH}/conf
	docker cp ${CONTAINER_NAME}:/home/admin/dev/bazel-bin/envoy output/linux/${ARCH}/bin
	docker cp ${CONTAINER_NAME}:/home/admin/dev/kuscia/conf/envoy.yaml output/linux/${ARCH}/conf

.PHONY: build-envoy-local
build-envoy-local:
	bazel build -c ${COMPILE_MODE} ${TARGET} --verbose_failures ${BUILD_OPTS} --@envoy//source/extensions/wasm_runtime/v8:enabled=false
	mkdir -p output/linux/${ARCH}/bin
	mkdir -p output/linux/${ARCH}/conf
	cp bazel-bin/envoy output/linux/${ARCH}/bin
	cp kuscia/conf/envoy.yaml output/linux/${ARCH}/conf

.PHONY: test-envoy
test-envoy:
	@$(call start_docker)
	docker exec -it ${CONTAINER_NAME} make test-envoy-local

test-envoy-local:
	bazel test -c ${TEST_COMPILE_MODE} ${TEST_TARGET} --test_arg="-l ${TEST_LOG_LEVEL}" \
	--test_verbose_timeout_warnings  \
	--verbose_failures  \
	${BUILD_OPTS}

.PHONY: clean
clean:
	$(call stop_docker)
	rm -rf output

.PHONY: image
image: build-envoy
	docker build -t ${IMG} -f ./build_image/dockerfile/kuscia-envoy-anolis.Dockerfile .
