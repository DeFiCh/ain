ARG TARGET=x86_64-pc-linux-gnu

FROM --platform=linux/amd64 ubuntu:latest as defi
ARG TARGET
ARG PACKAGE_PATH
ENV PATH=/app/bin:$PATH
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app
COPY ${PACKAGE_PATH} ./

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8554 8550 8551 18554 18550 18551 19554 19550 19551 20554 20550 20551
