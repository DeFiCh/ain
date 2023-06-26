# -----------
### Actual image that contains defi binaries
FROM --platform=linux/amd64 ubuntu:latest as defi
ARG TARGET
ARG PACKAGE
ENV PATH=/app/bin:$PATH
LABEL org.defichain.name="defichain"
LABEL org.defichain.arch=${TARGET}

WORKDIR /app
COPY ${PACKAGE} ./
RUN tar -xvzf ${PKG_NAME} --strip-components 1
COPY --from=builder /app/. ./

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8554 8550 8551 18554 18550 18551 19554 19550 19551 20554 20550 20551