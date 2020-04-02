ARG TARGET=x86_64-w64-mingw32

# -----------
FROM ubuntu:18.04 as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

RUN apt update && apt dist-upgrade -y

# Setup Defichain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup  

RUN apt install -y software-properties-common build-essential libtool autotools-dev automake \
pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
libminiupnpc-dev libzmq3-dev libqrencode-dev \
curl cmake \
g++-mingw-w64-x86-64 mingw-w64-x86-64-dev nsis

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
# XREF: #depends-make
RUN make HOST=${TARGET} NO_QT=1

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
RUN CONFIG_SITE=`pwd`/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/

RUN make

# -----------
### Actual image that contains defi binaries
FROM ubuntu:18.04
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

# XREF: #defi-package-bins
COPY --from=builder \
    ./work/src/defid.exe \
    ./work/src/defi-cli.exe \
    ./work/src/defi-wallet.exe \
    ./work/src/defi-tx.exe \
    ./
