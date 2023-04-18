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
ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup target add x86_64-unknown-linux-gnu

COPY . .
RUN ./make.sh clean-depends && \
    export MAKE_DEPS_ARGS="\
        x86_64_linux_CC=clang-${CLANG_VERSION} \
        x86_64_linux_CXX=clang++-${CLANG_VERSION}" && \
    ./make.sh build-deps
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

EXPOSE 8555 8554 18555 18554 19555 19554 20555 20554