ARG TARGET=x86_64-pc-linux-gnu

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

# -----------
### Actual image that contains defi binaries
FROM --platform=linux/amd64 ubuntu:latest
ARG TARGET
ENV PATH=/app/bin:$PATH
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

EXPOSE 8554 8550 8551 18554 18550 18551 19554 19550 19551 20554 20550 20551