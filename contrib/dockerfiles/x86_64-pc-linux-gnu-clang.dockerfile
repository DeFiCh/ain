ARG TARGET=x86_64-pc-linux-gnu

# -----------
FROM ubuntu:18.04 as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN apt update && apt install -y apt-transport-https
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg-install-deps-x86_64
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_llvm_ubuntu_18_04

# -----------
FROM builder-base as depends-builder
ARG TARGET
LABEL org.defichain.name="defichain-depends-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./depends ./depends

RUN ./make.sh clean-depends && ./make.sh build-deps

# -----------
FROM builder-base as builder
ARG TARGET
ARG BUILD_VERSION=

LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work

COPY . .
RUN ./make.sh purge && rm -rf ./depends
COPY --from=depends-builder /work/depends ./depends

RUN ./make.sh clean && ./autogen.sh
RUN export MAKE_COMPILER="CC=clang-14 CCX=clang++-14" && \
    ./make.sh build-conf && ./make.sh build-make

RUN mkdir /app && make prefix=/ DESTDIR=/app install && cp /work/README.md /app/.

# -----------
### Actual image that contains defi binaries
FROM ubuntu:18.04
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./

