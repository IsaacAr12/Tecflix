# Se construye desde la carpeta tecflix-project (necesita common/):
#   docker build -f metadata-api/Dockerfile -t tecflix/metadata-api .
FROM ubuntu:24.04 AS compilacion
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make git ca-certificates pkg-config \
        libmongoc-dev libprotobuf-dev protobuf-compiler libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY common common
COPY metadata-api metadata-api
RUN cmake -S metadata-api -B build -DTECFLIX_PRUEBAS=OFF -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)" --target metadata_api

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libmongoc-1.0-0t64 libprotobuf32t64 libcurl4t64 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=compilacion /src/build/metadata_api /usr/local/bin/metadata_api
USER nobody
EXPOSE 8082
CMD ["metadata_api"]
