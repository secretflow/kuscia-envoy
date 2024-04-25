FROM openanolis/anolisos:8.8

ARG ARCH

ENV TZ=Asia/Shanghai

ARG ROOT_DIR="/home/kuscia"

COPY ./output/linux/$ARCH $ROOT_DIR/

WORKDIR ${ROOT_DIR}

ENTRYPOINT ["/bin/bash", "--"]