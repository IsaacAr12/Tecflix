# Se construye desde la carpeta tecflix-project (necesita common/):
#   docker build -f streaming-api/Dockerfile -t tecflix/streaming-api .
FROM ubuntu:24.04 AS compilacion
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make git ca-certificates libprotobuf-dev protobuf-compiler libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY common common
COPY streaming-api streaming-api
RUN cmake -S streaming-api -B build -DTECFLIX_PRUEBAS=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" --target streaming_api

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        ffmpeg libprotobuf32t64 libcurl4t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /datos/videos && chown nobody /datos/videos
COPY --from=compilacion /src/build/streaming_api /usr/local/bin/streaming_api
USER nobody
ENV DIR_VIDEOS=/datos/videos
VOLUME /datos/videos
EXPOSE 8083
CMD ["streaming_api"]
