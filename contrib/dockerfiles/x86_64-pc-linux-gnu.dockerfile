ARG TARGET=x86_64-pc-linux-gnu

# -----------
# https://github.com/DeFiCh/containers/blob/main/ain-builder/Dockerfile
FROM --platform=linux/amd64 docker.io/defi/ain-builder as builder
ARG TARGET
ARG MAKE_DEBUG
ARG CCACHE_DIR="/work/build/.ccache"
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

ENV PATH=/root/.cargo/bin:$PATH
RUN ./make.sh ci-setup-deps
RUN ./make.sh ci-setup-deps-target
RUN ./make.sh ci-setup-deps-test

COPY . .
RUN ./make.sh build-deps
RUN ./make.sh build-conf
RUN ./make.sh build-make

RUN mkdir /app && cd build/ && \
    make -s prefix=/ DESTDIR=/app install

RUN ls -lah

# -----------
### Actual image that contains defi binaries
FROM --platform=linux/amd64 ubuntu:latest as defi
ARG TARGET
ENV PATH=/app/bin:$PATH
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app
COPY --from=builder /app/. ./
# TODO: remove copying of entire build directory into defi image
COPY --from=builder /work/build/ /work/build/
COPY --from=builder /work/.cache/ /work/.cache/

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8554 8550 8551 18554 18550 18551 19554 19550 19551 20554 20550 20551