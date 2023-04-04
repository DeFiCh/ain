ARG TARGET=x86_64-w64-mingw32

# -----------
FROM ubuntu:latest as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_update_base
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps_mingw_x86_64

RUN update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
RUN update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

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
RUN ./make.sh purge && rm -rf ./depends
COPY --from=depends-builder /work/depends ./depends

RUN ./make.sh build-conf && ./make.sh build-make

RUN mkdir /app && make prefix=/ DESTDIR=/app install && cp /work/README.md /app/.

# -----------
### Actual image that contains defi binaries
FROM ubuntu:latest
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./
