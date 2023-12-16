# This is required to be passed in for compilation. 
# This is the dockerfile to use for adding support to new arch or or arch
# without end docker images, like darwin x84_64 and darwin aarch64 platforms 
ARG TARGET=unknown

FROM ubuntu:latest as defi
ARG TARGET
ARG BINARY_DIR
ENV PATH=/app/bin:$PATH
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app
COPY ${BINARY_DIR} ./

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8554 8550 8551 18554 18550 18551 19554 19550 19551 20554 20550 20551
