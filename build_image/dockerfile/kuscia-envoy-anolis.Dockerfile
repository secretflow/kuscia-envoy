FROM openanolis/anolisos:8.8

ENV TZ=Asia/Shanghai

ARG ROOT_DIR="/home/kuscia"
COPY output $ROOT_DIR/

WORKDIR ${ROOT_DIR}

ENTRYPOINT ["/bin/bash", "--"]