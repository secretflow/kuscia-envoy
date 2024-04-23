FROM openanolis/anolisos:8.8

ARG TARGETPLATFORM

ENV TZ=Asia/Shanghai

ARG ROOT_DIR="/home/kuscia"

COPY ./output $ROOT_DIR/

RUN if [ -d "$ROOT_DIR/$TARGETPLATFORM" ]; then \
        mv -f "$ROOT_DIR/$TARGETPLATFORM"/* "$ROOT_DIR"/; \
    fi
WORKDIR ${ROOT_DIR}

ENTRYPOINT ["/bin/bash", "--"]