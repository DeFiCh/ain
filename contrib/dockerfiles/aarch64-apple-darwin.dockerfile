ARG TARGET=aarch64-apple-darwin

# -----------
FROM --platform=linux/amd64 ubuntu:latest as builder
ARG TARGET
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

# Temporary workaround until https://github.com/DeFiCh/ain/pull/1946 lands
# with specific ci-* methods
ENV PATH=/root/.cargo/bin:$PATH
RUN ./make.sh ci-setup-deps
RUN ./make.sh ci-setup-deps-target

COPY . .
RUN ./make.sh clean-depends && ./make.sh build-deps
RUN ./make.sh clean-conf && ./make.sh build-conf 
RUN ./make.sh build-make

RUN mkdir /app && cd build/ && \
    make -s prefix=/ DESTDIR=/app install

# NOTE: These are not runnable images. So we do not add into a scratch base image.
