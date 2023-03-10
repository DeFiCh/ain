ARG TARGET=x86_64-w64-mingw32

# -----------
FROM ubuntu:18.04 as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

RUN apt update && apt dist-upgrade -y

# Setup DeFiChain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup

RUN apt install -y software-properties-common build-essential git libtool autotools-dev automake \
pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
libminiupnpc-dev libzmq3-dev libqrencode-dev \
curl cmake \
g++-mingw-w64-x86-64 mingw-w64-x86-64-dev nsis

# install rustlang
RUN curl https://sh.rustup.rs -sSf | \
    sh -s -- --default-toolchain stable -y
ENV PATH=/root/.cargo/bin:$PATH
RUN rustup target add x86_64-pc-windows-gnu
RUN printf '\n\
    [target.x86_64-pc-windows-gnu]\n\
    linker = "x86_64-w64-mingw32-gcc"\n\
    ar = "x86_64-w64-mingw32-ar"\n' > /root/.cargo/config

# Set the default mingw32 g++ compiler option to posix.
RUN update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

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
RUN CONFIG_SITE=`pwd`/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/ ${MAKE_CONF_ARGS}

ARG BUILD_VERSION=

RUN make -j $(nproc)
RUN mkdir /app && make prefix=/ DESTDIR=/app install && cp /work/README.md /app/.

# -----------
### Actual image that contains defi binaries
FROM ubuntu:18.04
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./
