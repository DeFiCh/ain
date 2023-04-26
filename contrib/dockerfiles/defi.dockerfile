FROM debian:10
ARG PACKAGE
LABEL org.defichain.name="defichain"

WORKDIR /app
COPY ./${PACKAGE} ./
RUN tar -xvzf ${PACKAGE} --strip-components 1

RUN useradd --create-home defi && \
    mkdir -p /data && \
    chown defi:defi /data && \
    ln -s /data /home/defi/.defi

VOLUME ["/data"]

USER defi:defi
CMD [ "/app/bin/defid" ]

EXPOSE 8555 8554 18555 18554 19555 19554 20555 20554