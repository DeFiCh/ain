ARG TARGET=x86_64-alpine-linux-musl

# TODO - Revamp this and integrate into make.sh
# -----------
FROM alpine:latest as builder
ARG TARGET
ARG BUILD_VERSION=

LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
RUN apk add --update alpine-sdk
RUN apk add --no-cache bash gcc git libffi musl-dev libffi-dev autoconf automake
RUN apk add --no-cache openssh-client make db-dev openssl openssl-dev
RUN apk add --no-cache libtool libevent libevent-dev
RUN apk add --no-cache boost boost-dev boost-system boost-filesystem

COPY . .

RUN ./contrib/install_db4.sh .

RUN ./make.sh clean && ./autogen.sh
RUN export BDB_PREFIX=/work/db4 && ./configure BDB_LIBS="-L${BDB_PREFIX}/lib -ldb_cxx-4.8" BDB_CFLAGS="-I${BDB_PREFIX}/include"
RUN export BDB_PREFIX=/work/db4 && make -j $(nproc)
RUN mkdir /app && make prefix=/ DESTDIR=/app install && cp /work/README.md /app/.

# -----------
### Actual image that contains defi binaries
FROM ubuntu:20.04
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./
