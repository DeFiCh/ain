ARG TARGET=x86_64-pc-linux-gnu

FROM defichain-builder-base-${TARGET}
ARG TARGET
LABEL org.defichain.name="defichain-dev"
LABEL org.defichain.arch=${TARGET}

WORKDIR /data
COPY . .

VOLUME [ "/data" ]
CMD [ "bash", "-c", "./make.sh release" ]