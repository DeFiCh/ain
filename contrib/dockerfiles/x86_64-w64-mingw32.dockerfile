ARG TARGET=x86_64-w64-mingw32

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
RUN CONFIG_SITE=`pwd`/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/

RUN make

### Actual image that contains defi binaries
FROM ubuntu:18.04

WORKDIR /app

# XREF: #defi-package-bins
COPY --from=builder \
    ./work/src/defid.exe \
    ./work/src/defi-cli.exe \
    ./work/src/defi-wallet.exe \
    ./work/src/defi-tx.exe \
    ./
