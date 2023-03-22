ARG TARGET=x86_64-apple-darwin18

# -----------
FROM ubuntu:20.04 as builder-base
ARG TARGET
LABEL org.defichain.name="defichain-builder-base"
LABEL org.defichain.arch=${TARGET}

COPY ./make.sh .

ENV TZ=UTC
RUN echo $TZ > /etc/timezone
RUN apt update && apt dist-upgrade -y

# Setup DeFiChain build dependencies. Refer to depends/README.md and doc/build-unix.md
# from the source root for info on the builder setup

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg-install-deps-x86_64
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps_mac

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
RUN ./configure --prefix=`pwd`/depends/${TARGET} ${MAKE_CONF_ARGS}

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
