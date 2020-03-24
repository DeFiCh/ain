ARG TARGET=x86_64-pc-linux-gnu

FROM defichain-builder-${TARGET}

WORKDIR /data
COPY . .

VOLUME [ "/data" ]
CMD [ "bash", "-c", "./make.sh dev-release" ]