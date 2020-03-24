ARG TARGET=arm-linux-gnueabihf

### Builder that acts as a base for building Defichain with basics dependencies 
### that are required throughout the process
FROM defichain-builder-${TARGET} as depends-builder
ARG TARGET

WORKDIR /work/depends
COPY ./depends .
# XREF: #depends-make
RUN make HOST=${TARGET} NO_QT=1

### Builder that does the actual Defichain build
FROM defichain-builder-${TARGET} as builder
ARG TARGET

WORKDIR /work

COPY --from=depends-builder /work/depends ./depends
COPY . .

RUN ./autogen.sh

# XREF: #make-configure
RUN ./configure --prefix=`pwd`/depends/${TARGET} \
    --enable-glibc-back-compat \
    --enable-reduce-exports \
    --without-gui --disable-tests \
    LDFLAGS="-static-libstdc++"

RUN make

### Actual image that contains defi binaries
FROM arm32v7/ubuntu:18.04

WORKDIR /app

# XREF: #defi-package-bins
COPY --from=builder \
    ./work/src/defid \
    ./work/src/defi-cli \
    ./work/src/defi-wallet \
    ./work/src/defi-tx \
    ./
