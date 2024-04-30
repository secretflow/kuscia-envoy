FROM openanolis/anolisos:23

ARG TARGETPLATFORM

ENV TZ=Asia/Shanghai

ARG ROOT_DIR="/home/kuscia"

COPY ./output/$TARGETPLATFORM $ROOT_DIR/

WORKDIR ${ROOT_DIR}

ENTRYPOINT ["/bin/bash", "--"]