ARG TARGET=x86_64-pc-linux-gnu

### Builder that acts as a base for building Defichain with basics dependencies 
### that are required throughout the process
FROM ubuntu:18.04 as builder-base

RUN apt update && apt dist-upgrade -y

# Setup Defichain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup  

RUN apt install -y software-properties-common build-essential libtool autotools-dev automake \
    pkg-config bsdmainutils python3 libssl-dev libevent-dev libboost-system-dev \
    libboost-filesystem-dev libboost-chrono-dev libboost-test-dev libboost-thread-dev \
    libminiupnpc-dev libzmq3-dev libqrencode-dev \
    curl cmake

# For Berkeley DB - but we don't need as we do a depends build.
# RUN apt install -y libdb-dev

### Builder that just builds the `depends` items. 
FROM builder-base as depends-builder
ARG TARGET

WORKDIR /work/depends
COPY ./depends .
# XREF: #depends-make
RUN make HOST=${TARGET} NO_QT=1

### Builder that does the actual Defichain build
FROM builder-base as builder
ARG TARGET

WORKDIR /work

COPY --from=depends-builder /work/depends ./depends
COPY . .

RUN ./autogen.sh

# XREF: #make-configure
RUN ./configure --prefix=`pwd`/depends/${TARGET} --without-gui --disable-tests
RUN make

### Actual image that contains defi binaries
FROM ubuntu:18.04

WORKDIR /app

# XREF: #defi-package-bins
COPY --from=builder \
    ./work/src/defid \
    ./work/src/defi-cli \
    ./work/src/defi-wallet \
    ./work/src/defi-tx \
    ./
