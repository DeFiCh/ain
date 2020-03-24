ARG TARGET=x86_64-pc-linux-gnu

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
