ARG TARGET=x86_64-w64-mingw32

# -----------
FROM ubuntu:20.04 as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && apt ./make.sh pkg_install_base

# Setup DeFiChain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg-install-deps-x86_64
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps_mingw_x86_64

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
FROM ubuntu:20.04
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app

COPY --from=builder /app/. ./
