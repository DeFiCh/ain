FROM defichain-x86_64-pc-linux-gnu:dockerhub-latest as dh-build

FROM debian:10-slim
ENV PATH=/app/bin:$PATH
WORKDIR /app

COPY --from=dh-build /app/. ./

VOLUME ["/root/.defi"]

EXPOSE 8555 8554 18555 18554 19555 19554

CMD [ "/app/bin/defid" ]
