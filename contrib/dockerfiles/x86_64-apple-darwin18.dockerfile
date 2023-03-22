ARG TARGET=x86_64-apple-darwin18

# -----------
FROM ubuntu:20.04 as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_base

# Setup DeFiChain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup

RUN apt install -y software-properties-common build-essential git libtool autotools-dev automake \
pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
libminiupnpc-dev libzmq3-dev libqrencode-dev \
curl cmake unzip \
python3-dev python3-pip libcap-dev libbz2-dev libz-dev fonts-tuffy librsvg2-bin libtiff-tools imagemagick

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg-install-deps-x86_64
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_mac_sdk_deps

# install protobuf
RUN curl -OL https://github.com/protocolbuffers/protobuf/releases/download/v3.20.0/protoc-3.20.0-linux-x86_64.zip
RUN unzip -o protoc-3.20.0-linux-x86_64.zip -d ./proto
RUN chmod 755 -R ./proto/bin
RUN cp ./proto/bin/protoc /usr/local/bin/
RUN cp -R ./proto/include/* /usr/local/include/

# install rustlang
RUN curl https://sh.rustup.rs -sSf | \
    sh -s -- --default-toolchain stable -y
ENV PATH=/root/.cargo/bin:$PATH
RUN rustup target add x86_64-apple-darwin

# For Berkeley DB - but we don't need as we do a depends build.
# RUN apt install -y libdb-dev

# -----------
FROM builder-base as depends-builder
ARG TARGET
LABEL org.defichain.name="defichain-depends-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work/depends
COPY ./depends .
# XREF: #make-deps
RUN make HOST=${TARGET} -j $(nproc)

# -----------
FROM builder-base as builder
ARG TARGET
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work

COPY --from=depends-builder /work/depends ./depends
COPY . .

RUN ./autogen.sh

# XREF: #make-configure
RUN ./configure --prefix=`pwd`/depends/${TARGET} ${MAKE_CONF_ARGS}

ARG BUILD_VERSION=

RUN make -j $(nproc)
RUN mkdir /app && make prefix=/ DESTDIR=/app install && cp /work/README.md /app/.

# -----------
### Actual image that contains defi binaries
FROM ubuntu:20.04
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./
