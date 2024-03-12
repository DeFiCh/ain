# This is required to be passed in for compilation. 
# This is the dockerfile to use for adding support to new arch or or arch
# without end docker images, like darwin x84_64 and darwin aarch64 platforms 
ARG TARGET=unknown

# -----------
FROM --platform=linux/amd64 ubuntu:latest as builder
ARG TARGET
ARG MAKE_DEBUG
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

ENV PATH=/root/.cargo/bin:$PATH
RUN ./make.sh ci-setup-deps
RUN ./make.sh ci-setup-user-deps

COPY . .
RUN ./make.sh build-deps
RUN ./make.sh build-conf
RUN ./make.sh build-make

RUN mkdir /app && cd build/ && \
    make -s prefix=/ DESTDIR=/app install

# NOTE: These may or may not be runnable binaries on the platform. 
# So we do not add into a scratch base image. Extract and use as needed.
