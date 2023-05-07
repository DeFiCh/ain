ARG TARGET=x86_64-pc-linux-gnu

# -----------
FROM --platform=linux/amd64 debian:10 as builder
ARG TARGET
ARG CLANG_VERSION=15
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_update_base
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_rust
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_llvm

COPY . .
RUN ./make.sh clean-depends && ./make.sh build-deps
RUN export MAKE_CONF_ARGS="\
    CC=clang-${CLANG_VERSION} \
    CXX=clang++-${CLANG_VERSION}" && \
    ./make.sh clean-conf && ./make.sh build-conf 
RUN ./make.sh build-make

RUN mkdir /app && cd build/${TARGET} && \
    make -s prefix=/ DESTDIR=/app install

# -----------
### Actual image that contains defi binaries
FROM --platform=linux/amd64 debian:10
ARG TARGET
ENV PATH=/app/bin:$PATH
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app
COPY --from=builder /app/. ./

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8554 8550 8551 18554 18550 18551 19554 19550 19551 20554 20550 20551