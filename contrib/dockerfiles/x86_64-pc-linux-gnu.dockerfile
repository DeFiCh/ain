ARG TARGET=x86_64-pc-linux-gnu

# -----------
FROM ubuntu:latest as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_update_base
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps

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

LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work

COPY . .
COPY --from=depends-builder /work/build/depends ./build/depends

RUN export MAKE_COMPILER="CC=gcc CXX=g++" && \
    ./make.sh clean-conf && ./make.sh build-conf 

RUN ./make.sh build-make

RUN mkdir /app && cd build && \
    make prefix=/ DESTDIR=/app install

# -----------
### Actual image that contains defi binaries
FROM ubuntu:latest
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./
