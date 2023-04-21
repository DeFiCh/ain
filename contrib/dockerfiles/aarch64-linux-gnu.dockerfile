ARG TARGET=aarch64-linux-gnu

# -----------
FROM --platform=linux/amd64 ubuntu:latest as builder
ARG TARGET
LABEL org.defichain.name="defichain-builder"
LABEL org.defichain.arch=${TARGET}

WORKDIR /work
COPY ./make.sh .

RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_update_base
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_rust
RUN export DEBIAN_FRONTEND=noninteractive && ./make.sh pkg_install_deps_arm64
ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup target add aarch64-unknown-linux-gnu

COPY . .
RUN ./make.sh clean-depends && ./make.sh build-deps
RUN ./make.sh clean-conf && ./make.sh build-conf 
RUN ./make.sh build-make

RUN mkdir /app && cd build/${TARGET} && \
    make -s prefix=/ DESTDIR=/app install

# -----------
### Actual image that contains defi binaries
FROM --platform=linux/arm64 ubuntu:latest
ARG TARGET
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app
COPY --from=builder /app/. ./

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8555 8554 18555 18554 19555 19554 20555 20554
