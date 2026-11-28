# API de Metadata y API de Streaming

Guía práctica de los dos APIs. El contrato oficial está en `SPEC.md` (secciones 6 a 10); este documento explica cómo usarlos, compilarlos y probarlos.

## Estructura

```
tecflix-project/
├── common/              biblioteca compartida: ServidorHttp (CORS, errores, /salud, telemetría por petición),
│                        ClienteHttpInterno (token + traceparent), Telemetria (OpenTelemetry), configuración
├── metadata-api/        VideoMetadata, IRepositorioMetadata, RepositorioMongo, ServicioMetadata, ControladorMetadata
├── streaming-api/       ITranscodificador, EjecutorProcesos, GestorAlmacenamiento, PoolTranscodificacion,
│                        IClienteUsuarios, IClienteMetadata, ServicioStreaming, ControladorStreaming
├── observabilidad/      configuración del OpenTelemetry Collector y de Prometheus
└── docker-compose.yml   mongo, otel-collector, prometheus, aspire-dashboard y los dos APIs
```

Dependencias de C++ (por `FetchContent`, en versiones fijas): cpp-httplib 0.18.7, nlohmann/json 3.11.3, opentelemetry-cpp 1.22.0, mongo-cxx-driver 3.10.2 (solo metadata) y GoogleTest 1.15.2.
Del sistema (Ubuntu 24.04): `libprotobuf-dev protobuf-compiler libcurl4-openssl-dev`, más `libmongoc-dev` para metadata y `ffmpeg` para streaming.

## Uso desde la aplicación web

Todas las peticiones llevan `X-Usuario-Id` con el id que devuelve el API de usuarios.

**Subir un video** (al API de streaming). Los campos de texto van **antes** del archivo:
```ts
const formulario = new FormData();
formulario.append("titulo", titulo);
formulario.append("descripcion", descripcion);
formulario.append("tema", tema);
formulario.append("etiquetas", JSON.stringify(etiquetas));   // por ejemplo ["btree","disco"]
formulario.append("visibilidad", "publico");                 // o "privado"
formulario.append("archivo", archivo);                       // mp4, mov, mkv, webm o avi; va al final

const respuesta = await fetch(`${URL_API_STREAMING}/videos`, {
  method: "POST",
  headers: { "X-Usuario-Id": String(usuarioId) },
  body: formulario,
});
// 202 -> { id_video, estado: "procesando" }
// 413 -> CUOTA_EXCEDIDA o ARCHIVO_DEMASIADO_GRANDE
```
Mientras el estado sea `"procesando"`, la web puede consultar `GET ${URL_API_METADATA}/videos/{id}` cada pocos segundos.

**Listar** (al API de metadata):
`GET /videos?ordenar=titulo|autor|fecha&orden=asc|desc&pagina=1&limite=20` →
`{ "videos": [...], "pagina", "limite", "total", "total_paginas" }`.

**Editar**: `PUT ${URL_API_METADATA}/videos/{id}` con cualquiera de `titulo`, `descripcion`, `tema`, `etiquetas` y `visibilidad` (solo el autor).

**Reproducir con Shaka Player.** Todas las peticiones del reproductor necesitan el encabezado:
```ts
const reproductor = new shaka.Player();
await reproductor.attach(elementoVideo);
reproductor.getNetworkingEngine()!.registerRequestFilter((_tipo, peticion) => {
  peticion.headers["X-Usuario-Id"] = String(usuarioId);
});
await reproductor.load(`${URL_API_STREAMING}/videos/${id}/manifest.mpd`);
```

**Eliminar**: `DELETE ${URL_API_STREAMING}/videos/{id}` (solo el autor). Streaming borra la metadata, los archivos y libera la cuota.

## Errores

Siempre `{"error": {"codigo": "...", "mensaje": "..."}}` con los códigos de la sección 6.4 del SPEC.

## Telemetría

- Cada petición genera un span (`GET /videos/:id`) y las métricas `http_peticiones_total`, `http_duracion_ms` y `http_errores_total` (respuestas 5xx).
- Las llamadas entre APIs envían `traceparent`, así que una subida aparece en Aspire como un solo trace: streaming → usuarios → metadata, con el span `transcodificar` como hijo.
- Métricas propias:
  - metadata: `mongo_consulta_duracion_ms` (etiqueta `operacion`);
  - streaming: `transcodificacion_duracion_seg`, `transcodificaciones_en_cola`, `segmentos_servidos_total`, `bytes_servidos_total` y `compensaciones_fallidas_total`.
- Con `docker compose up`: el Aspire Dashboard queda en http://localhost:18888 y Prometheus en http://localhost:9090.

## Compilar y probar

```bash
cd tecflix-project
cmake -S streaming-api -B build-streaming -DTECFLIX_COBERTURA=ON
cmake --build build-streaming -j
ctest --test-dir build-streaming --output-on-failure
gcovr -r . --filter 'streaming-api/src/' --filter 'common/src/' --exclude '.*main\.cpp' build-streaming
```
Para metadata es igual, cambiando `streaming-api` por `metadata-api`. Las pruebas de `RepositorioMongo` contra una base real necesitan `TECFLIX_URI_MONGO_PRUEBAS=mongodb://localhost:27017`; sin esa variable se omiten.
La primera compilación tarda unos minutos porque descarga y compila OpenTelemetry y el driver de MongoDB.

## Ejecutar sin Docker

```bash
URI_MONGO="mongodb://localhost:27017/?serverSelectionTimeoutMS=5000" TOKEN_INTERNO=secreto \
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 ./build-metadata/metadata_api

URL_API_USUARIOS=http://localhost:8081 URL_API_METADATA=http://localhost:8082 TOKEN_INTERNO=secreto \
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 ./build-streaming/streaming_api
```
Si falta una variable obligatoria, el API termina al arrancar e indica cuál falta.
