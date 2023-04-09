ARG TARGET=x86_64-apple-darwin

# -----------
FROM ubuntu:latest as builder
ARG TARGET
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_update_base
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps_mac_tools
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_local_mac_sdk

COPY . .
RUN ./make.sh clean-depends && ./make.sh build-deps
RUN ./make.sh clean-conf && ./make.sh build-conf 
RUN ./make.sh build-make

RUN mkdir /app && cd build && \
    make prefix=/ DESTDIR=/app install

# NOTE: These are not runnable images. So we do not add into a scratch base image.
