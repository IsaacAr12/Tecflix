# Documento de Especificaciones: Sistema Tecflix (V1.0)

## 1. Descripción General del Sistema
Tecflix es una plataforma distribuida de streaming de video compuesta por una aplicación web en ReactJS y tres microservicios independientes escritos en C++. El diseño debe contemplar la orientación a objetos, modularidad, preparación para redundancia en fases futuras, instrumentación con OpenTelemetry y una cobertura mínima de pruebas del 90%.

## 2. Especificaciones de los Componentes (V1.0)

### A. API de Usuarios (C++)
- **Tipo:** REST API en C++.
- **Almacenamiento de Datos:** Archivos en formato binario estructurado en disco (`usuarios.dat` e `indice.idx`).
- **Estructura de Datos Obligatoria:** Implementación de un Árbol B en disco de orden M=5 dentro del espacio de nombres `tecflix` para optimizar el rendimiento de las búsquedas por ID de usuario.

### B. API de Metadata (C++)
- **Tipo:** REST API en C++.
- **Almacenamiento de Datos:** Integración con base de datos MongoDB para gestionar la información de los catálogos.

### C. API de Streaming (C++)
- **Tipo:** REST API en C++ integrado con FFMPEG para transcodificación y segmentación de video en bloques multimedia.

## 3. Diseño Detallado del Índice (Árbol B en Disco)
Para garantizar el rendimiento óptimo exigido por la cátedra, el índice estructurado del API de Usuarios no utilizará punteros tradicionales en memoria RAM, sino desplazamientos de bytes fijos (`uint64_t`) para navegar por las páginas del archivo binario.

### 3.1 Estructura del Nodo (`NodoBTree`)
- **Parámetros de Capacidad:** Orden $ORDEN\_M = 5$. Máximo de claves permitidas por nodo = 4 (`MAX_CLAVES`).
- **Componentes Fijos:**
  - `numClaves`: `uint32_t` que almacena la cantidad actual de claves activas en el nodo.
  - `elementos`: Contenedor estático `std::array<ParClaveValor, MAX_CLAVES>` para los pares compuestos de datos.
  - `hijos`: Contenedor estático `std::array<uint64_t, ORDEN_M>` que almacena las posiciones en bytes (`Offsets`) de los nodos hijos en el archivo.

### 3.2 Estructura de Asociación (`ParClaveValor`)
- `id_usuario`: `uint32_t` (Clave primaria del usuario).
- `byte_offset`: `uint64_t` (Posición física real del registro completo del usuario dentro del archivo secuencial por bloques `usuarios.dat`).

### 3.3 Gestión y Persistencia de Archivos
- El archivo binario de índices maneja la persistencia directa mediante bloques de memoria del mismo tamaño mapeados con `reinterpret_cast<char*>(&nodo)`.
- El puntero de la raíz (`raiz`) se almacena como una posición en bytes (`uint64_t`). Si el archivo está vacío o no existe, se inicializa un nodo raíz en la posición `0`.

## 4. Lógica Algorítmica y Control del Flujo en Disco

### 4.1 Navegación y Búsqueda en Disco
- La función de búsqueda lee recursivamente el nodo desde la posición en bytes actual.
- Realiza un escaneo lineal interno en el arreglo estático de `elementos`. Si halla el ID, retorna éxito devolviendo el `byte_offset` correspondiente; de lo contrario, extrae la posición en bytes del hijo correspondiente para saltar físicamente a ese bloque en el disco mediante `seekg()`.

### 4.2 Restricción Crítica de Inserción y Escritura (Algoritmo Split)
- Cuando un nodo alcanza su capacidad máxima (`MAX_CLAVES = 4`), se activa la función de división (`dividirNodo`).
- **Garantía de No Sobrescritura:** Para evitar que la obtención de posiciones de escritura destruya o pise bloques de datos previos, es un requisito obligatorio desplazar el cabezal de forma explícita al final del archivo mediante `archivoIndice.seekp(0, std::ios::end);` inmediatamente antes de calcular la dirección del nuevo nodo.
- El nodo original se divide a la mitad, las claves restantes se vuelcan en un nodo nuevo al final del archivo, y la clave del medio es promovida al nodo padre actualizando sus posiciones binarias correspondientes utilizando manipulación de bloques de tamaño fijo.
- **Persistencia Exacta de Nodos:** Queda estrictamente prohibido el cálculo relativo o matemático de posiciones de hijos basándose en el offset del padre. Cada operación de escritura sobre páginas existentes (`escribirNodo`) debe realizarse utilizando de forma explícita el desplazamiento absoluto en bytes (`uint64_t`) capturado durante su lectura original en disco.
- **Verificación de Hojas Estáticas:** Para determinar si un nodo en proceso de división debe transferir enlaces de hijos hacia la página nueva, queda estrictamente prohibido el uso del método `.empty()` sobre contenedores `std::array`. El estado estructural se valida inspeccionando si el offset del primer hijo apunta a una dirección no nula (`hijos[0] != 0`).

## 5. Instrucciones de Contexto para el Asistente de IA (Continue)
- Todo el código de C++ generado debe cumplir con el estándar C++17 o superior y compilarse nativamente mediante CMake.
- Se debe respetar el uso de `std::array` estáticos dentro de las estructuras para no alterar la alineación de memoria en el disco.
- Todos los nombres de las funciones principales, métodos de control de archivos y comentarios técnicos deben mantenerse estrictamente en idioma español.

## 6. Arquitectura y Contratos entre Componentes

### 6.1 Puertos y Componentes
| Componente | Puerto | Tecnología |
|---|---|---|
| Aplicación Web | 5173 | ReactJS + TypeScript (Vite) |
| API de Usuarios | 8081 | C++17, Árbol B en disco |
| API de Metadata | 8082 | C++17, MongoDB (puerto 27017) |
| API de Streaming | 8083 | C++17, FFMPEG, MPEG-DASH |
| OpenTelemetry Collector | 4317 (gRPC) / 4318 (HTTP) / 8889 (métricas para Prometheus) | otel-collector |
| Prometheus | 9090 | Prometheus |
| Aspire Dashboard | 18888 (interfaz) / 18889 (OTLP) | Visor de traces y métricas |

### 6.2 Configuración
Toda la configuración se lee de variables de entorno. Queda prohibido escribir URLs, puertos o secretos directamente en el código, porque en la V2.0 cada API tendrá dos instancias.

| Variable | Usada por | Descripción |
|---|---|---|
| `PUERTO` | los tres APIs | Puerto en el que escucha el API |
| `URL_API_USUARIOS`, `URL_API_METADATA`, `URL_API_STREAMING` | APIs y web | URL base de cada API |
| `URI_MONGO`, `BD_MONGO` | Metadata | Conexión a MongoDB (`tecflix` por defecto) |
| `TOKEN_INTERNO` | los tres APIs | Secreto compartido para las llamadas entre APIs (sección 6.5) |
| `ORIGEN_APP_WEB` | los tres APIs | Origen permitido por CORS (por ejemplo `http://localhost:5173`) |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | los tres APIs | Dirección del collector |
| `DIR_VIDEOS`, `HILOS_TRANSCODIFICACION`, `MAX_BYTES_SUBIDA` | Streaming | Carpeta de videos, hilos de FFMPEG, tamaño máximo por archivo |
| `CUOTA_POR_DEFECTO_BYTES` | Usuarios | Cuota asignada al registrarse (1 GB por defecto) |

Si falta una variable obligatoria (`TOKEN_INTERNO` y las URLs), el API termina al arrancar con un mensaje claro.

### 6.3 Decisiones Tecnológicas Comunes (los tres APIs)
- **Servidor HTTP:** `cpp-httplib`, la misma librería y versión fija en los tres APIs.
- **JSON:** `nlohmann/json`.
- **MongoDB:** driver oficial `mongocxx` (solo en el API de Metadata).
- **Telemetría:** `opentelemetry-cpp`, con exportador OTLP hacia el collector (sección 9).
- **Pruebas:** GoogleTest + GoogleMock; cobertura medida con `gcov`/`lcov`, con un mínimo de 90 %.
- **Compilación:** CMake ≥ 3.15, C++17; dependencias por `FetchContent` o `vcpkg`.
- **Código compartido:** CORS, formato de errores, `GET /salud`, lectura de configuración, validación de encabezados y telemetría por petición se implementan una sola vez en la biblioteca `common/`, que usan los tres APIs. Así no se duplica código.
- **CORS:** los tres APIs responden `204` a `OPTIONS` y envían:
  - `Access-Control-Allow-Origin: <ORIGEN_APP_WEB>` (nunca `*`);
  - `Access-Control-Allow-Headers: Content-Type, X-Usuario-Id, Range, traceparent`;
  - `Access-Control-Expose-Headers: Content-Length, Content-Range`.

  `X-Token-Interno` no se permite desde el navegador.

### 6.4 Formato Común de Respuestas
- **Éxito:** el JSON del recurso, con el código HTTP correspondiente (`200`, `201`, `202`, `204`).
- **Error:** `{"error": {"codigo": "CUOTA_EXCEDIDA", "mensaje": "Texto legible"}}`, con estos códigos:

| HTTP | `codigo` | Cuándo |
|---|---|---|
| 400 | `DATOS_INVALIDOS` | Campo inválido, JSON mal formado, parámetro inválido, formato de video no soportado |
| 401 | `NO_AUTENTICADO` | Falta `X-Usuario-Id` o es inválido; falta `X-Token-Interno` o no coincide |
| 403 | `SIN_PERMISO` | El usuario ve el recurso, pero no puede hacer esa acción (por ejemplo, editar un video público ajeno) |
| 404 | `NO_ENCONTRADO` | No existe, **o es privado de otro usuario**: así no se revela que existe |
| 409 | `CONFLICTO` | El recurso no está en el estado esperado (por ejemplo, borrar un video que se está procesando) |
| 413 | `CUOTA_EXCEDIDA` | La subida supera la cuota disponible |
| 413 | `ARCHIVO_DEMASIADO_GRANDE` | La subida supera `MAX_BYTES_SUBIDA` |
| 500 | `ERROR_INTERNO` | Error inesperado; el detalle va al log, nunca a la respuesta |
| 502 | `SERVICIO_NO_DISPONIBLE` | MongoDB u otro API no respondió |

- **Salud:** todos los APIs exponen `GET /salud` y responden `{"estado": "ok", "servicio": "<nombre>", "version": "1.0", "dependencias": {"mongodb": "ok"}}`.
  - Si una dependencia no responde, el campo `estado` vale `"degradado"` y el código HTTP es `503`.
  - El dashboard usa este endpoint.

### 6.5 Identificación y Autenticación
- **Usuarios (V1.0):** la aplicación web envía `X-Usuario-Id: <id_usuario>` en cada petición. Cada API valida que sea un entero entre 1 y 4294967295 (`uint32_t`, el mismo tipo que la clave del árbol B). Si falta o es inválido, responde `401`.
- **Entre APIs:** las llamadas de un API a otro envían `X-Token-Interno: <TOKEN_INTERNO>`. Todos los endpoints marcados como *internos* lo exigen y responden `401` si no coincide.
  - Sin este token, cualquier persona podría marcar un video como `listo`, borrar videos ajenos o cambiarse su propio uso de cuota.
  - La comparación se hace en tiempo constante.
- **Tipo del id de usuario:**
  - En C++ es `uint32_t` en los tres APIs. En `user-api`, `Usuario::id` debe pasar de `uint64_t` a `uint32_t`, para coincidir con `ParClaveValor::id_usuario`.
  - En MongoDB se guarda como `int64`, porque BSON no tiene enteros sin signo y un `uint32_t` no cabe en `int32`.

### 6.6 Flujo de Subida de Video
1. La web envía `POST /videos` al **API de Streaming**, en formato multipart con los campos `titulo`, `descripcion`, `tema`, `etiquetas` y `visibilidad`, y al final `archivo`.
2. Antes de leer el cuerpo, Streaming hace dos verificaciones rápidas:
   - Consulta `GET /usuarios/{id}/cuota` al **API de Usuarios**.
   - Si el `Content-Length` supera `MAX_BYTES_SUBIDA` o `disponible_bytes`, responde `413` de inmediato, sin recibir el archivo.
3. Streaming escribe el archivo en disco **por bloques** mientras lo recibe (nunca entero en memoria), como `original.<ext>`.
   - La extensión debe estar en la lista blanca `mp4`, `mov`, `mkv`, `webm` y `avi`; si no, responde `400`.
   - `ffprobe` verifica después que el contenido sea realmente un video.
4. **Reserva de cuota:** Streaming envía `POST /usuarios/{id}/uso` con `{"delta_bytes": tamano_original}`.
   - El API de Usuarios verifica y suma de forma atómica. Si se supera la cuota, responde `413`; Streaming borra el archivo y le responde `413` a la web.
   - La reserva se hace **antes** de transcodificar, para no gastar CPU en un video que no cabe y para que dos subidas simultáneas no pasen juntas la verificación.
5. Streaming obtiene el nombre del autor con `GET /usuarios/{id}` y crea el registro con `POST /videos` (interno) en el **API de Metadata**, con `estado = "procesando"` y `tamano_original_bytes`. Recibe el `id_video`.
   - Si Metadata rechaza los datos (`400`) o no responde, Streaming **compensa**: libera la reserva con `delta_bytes` negativo, borra el archivo y le devuelve el error a la web.
6. Streaming responde `202 Accepted` a la web con `{"id_video": "...", "estado": "procesando"}` y encola la transcodificación en el `PoolTranscodificacion`.
7. Un hilo del pool transcodifica (sección 8.3). Al terminar bien:
   - borra `original.<ext>`;
   - envía `PATCH /videos/{id}` (interno) a Metadata con `estado = "listo"`, `duracion_seg`, `ruta_manifest` y `tamano_bytes` (tamaño total de los archivos transcodificados).
8. Si la transcodificación falla:
   - Streaming borra todos los archivos del video;
   - libera la reserva con `{"delta_bytes": -tamano_original}`;
   - envía `PATCH` con `estado = "error"` y `mensaje_error`.

**La cuota cuenta el tamaño del archivo que subió el usuario** (`tamano_original_bytes`). Es lo que el usuario entiende como "lo que subí", y permite liberar exactamente lo mismo al borrar. `tamano_bytes` (lo que ocupa en disco después de transcodificar) se guarda para las estadísticas del dashboard.

### 6.7 Flujo de Reproducción
1. La web obtiene la metadata del video desde el API de Metadata.
2. El reproductor (Shaka Player o Dash.js) carga `GET /videos/{id}/manifest.mpd` desde el API de Streaming.
   - La web configura el reproductor para que **todas** sus peticiones lleven `X-Usuario-Id`.
   - En Shaka se hace con `player.getNetworkingEngine().registerRequestFilter(...)`; en Dash.js, con un interceptor de peticiones.
3. **Antes de servir el manifiesto y cada segmento**, Streaming consulta `GET /videos/{id}` a Metadata con el `X-Usuario-Id` del solicitante. Metadata aplica la regla de visibilidad, así la regla vive en un solo lugar:
   - si responde `404` (no existe o es privado ajeno), Streaming responde `404`;
   - si el `estado` no es `"listo"`, Streaming responde `409`, porque nunca se sirve un paquete DASH a medias;
   - si no, sirve el archivo.

   Verificar solo el manifiesto no basta: los nombres de los segmentos son predecibles, y cualquiera con el `id` podría descargar un video privado.
4. El reproductor pide los segmentos `GET /videos/{id}/{segmento}` y Streaming los sirve con soporte de `Range` (`206 Partial Content`).

En V1 la consulta a Metadata se hace en cada petición. Guardarla en caché unos segundos es una de las oportunidades de optimización para la V2.0.

### 6.8 Flujo de Eliminación
1. La web envía `DELETE /videos/{id}` al API de Streaming.
2. Streaming consulta `GET /videos/{id}` a Metadata con el `X-Usuario-Id` del solicitante:
   - `404` si no existe o no es visible;
   - `403` si el solicitante no es el autor;
   - `409` si el video está en `"procesando"`, porque un hilo de FFMPEG todavía escribe en su carpeta.
3. Streaming envía `DELETE /videos/{id}` (interno) a Metadata. Se borra primero el registro, para que el video desaparezca del listado y no se pueda reproducir aunque falle un paso posterior.
4. Streaming borra la carpeta del video en disco.
5. Streaming envía `POST /usuarios/{id}/uso` (interno) con `{"delta_bytes": -tamano_original_bytes}`.

Si falla el paso 4 o el 5, Streaming lo registra en el log y en la métrica `compensaciones_fallidas_total`, y responde `204` igualmente, porque para el usuario el video ya no existe.

### 6.9 Adiciones Requeridas al API de Usuarios
El registro del usuario incluye dos campos nuevos:
- `cuota_bytes` (`uint64_t`): fija, asignada al registrarse con `CUOTA_POR_DEFECTO_BYTES`;
- `usado_bytes` (`uint64_t`).

Endpoints nuevos:
- `GET /usuarios/{id}/cuota` (`X-Usuario-Id` igual a `{id}`, o `X-Token-Interno`). Responde `{"cuota_bytes": ..., "usado_bytes": ..., "disponible_bytes": ...}`.
- `POST /usuarios/{id}/uso` (**interno**, `X-Token-Interno`). Recibe `{"delta_bytes": <int64>}`.
  - La lectura, la verificación y la escritura de `usado_bytes` se hacen de forma **atómica** (bajo un `std::mutex`), para que dos subidas simultáneas no superen la cuota.
  - Si el nuevo uso superaría la cuota, responde `413`.
  - Si quedaría negativo, se deja en `0` y se registra una advertencia en el log.
  - `cuota_bytes` no se puede modificar por ningún endpoint.
- En la V2.0, con dos instancias, esta operación debe seguir siendo atómica entre instancias (ver la sección de redundancia).

---

## 7. API de Metadata (C++ + MongoDB)

### 7.1 Modelo del Documento (colección `videos`, base de datos `tecflix`)
| Campo | Tipo | Notas |
|---|---|---|
| `_id` | ObjectId | Identificador del video |
| `titulo` | string | Obligatorio, de 1 a 200 caracteres, sin ser solo espacios |
| `descripcion` | string | Opcional, máximo 5000 caracteres |
| `autor_id` | int64 | `id_usuario` del API de Usuarios (`uint32_t` en C++) |
| `autor_nombre` | string | Copia del nombre del autor, para ordenar por autor sin consultar Usuarios |
| `duracion_seg` | double | Lo llena Streaming |
| `fecha_carga` | date | Se asigna al crear (UTC) |
| `tema` | string | Opcional, máximo 100 caracteres |
| `etiquetas` | array<string> | Máximo 20, de 1 a 50 caracteres cada una, sin repetidas |
| `visibilidad` | string | `"publico"` o `"privado"`; por defecto `"privado"` |
| `estado` | string | `"procesando"`, `"listo"` o `"error"` |
| `mensaje_error` | string | Solo si `estado = "error"` |
| `ruta_manifest` | string | Ruta relativa del `.mpd`, por ejemplo `videos/{id}/manifest.mpd` |
| `tamano_original_bytes` | int64 | Tamaño del archivo subido; es lo que cuenta para la cuota |
| `tamano_bytes` | int64 | Tamaño total de los archivos transcodificados |

**Índices:**
- `{autor_id: 1}`;
- `{visibilidad: 1, estado: 1}`;
- `{fecha_carga: -1}`;
- `{titulo: 1}` y `{autor_nombre: 1}`, con la collation `{locale: "es", strength: 1}`, para ordenar sin distinguir mayúsculas ni tildes.

**Transiciones de estado válidas:** `procesando → listo` y `procesando → error`. Cualquier otra responde `409`.

### 7.2 Endpoints
| Método | Ruta | Autenticación | Descripción |
|---|---|---|---|
| `GET` | `/videos?ordenar=titulo\|autor\|fecha&orden=asc\|desc&pagina=1&limite=20` | `X-Usuario-Id` | Lista paginada |
| `GET` | `/videos/{id}` | `X-Usuario-Id` | Detalle; `404` si no existe o es privado ajeno |
| `POST` | `/videos` | interno | Crear; lo usa Streaming. El cuerpo incluye `autor_id`, `autor_nombre` y `tamano_original_bytes` |
| `PUT` | `/videos/{id}` | `X-Usuario-Id` | Modificar `titulo`, `descripcion`, `tema`, `etiquetas` y `visibilidad`; solo el autor. Cualquier otro campo responde `400` |
| `PATCH` | `/videos/{id}` | interno | Actualizar `estado`, `duracion_seg`, `ruta_manifest`, `tamano_bytes` y `mensaje_error` |
| `DELETE` | `/videos/{id}` | interno | Eliminar; lo usa Streaming (sección 6.8) |

### 7.3 Reglas de Listado
- **Filtro:** `{ $or: [ {visibilidad: "publico", estado: "listo"}, {autor_id: <X-Usuario-Id>} ] }`. El autor ve todos sus videos en cualquier estado; los demás usuarios solo ven los públicos que ya están listos.
- **Orden y paginación:** se hacen en MongoDB (`sort`, `skip`, `limit`, con la collation de los índices), **nunca** en la aplicación web.
  - Se agrega `_id` como segundo criterio de orden, para que la paginación sea estable cuando dos videos empatan.
  - El `total` se obtiene con `count_documents` usando el mismo filtro.
- **Valores por defecto:** `ordenar=fecha`, `pagina=1`, `limite=20`. `orden` es `desc` para la fecha y `asc` para el título y el autor.
- **Límites:** `limite` va de 1 a 50. Un parámetro inválido responde `400`.
- **Respuesta:** `{"videos": [...], "pagina": 1, "limite": 20, "total": 134, "total_paginas": 7}`.

### 7.4 Diseño de Clases (espacio de nombres `tecflix::metadata`)
- `VideoMetadata`: entidad, con las conversiones `aJson()` y `desdeJson()`. Tiene las reglas `esAutor(usuario)` y `puedeVer(usuario)`, donde `puedeVer` = es el autor, o el video es público y está listo. **No conoce BSON.**
- `IRepositorioMetadata`: interfaz abstracta con `crear`, `obtenerPorId`, `listar`, `actualizarDescriptivos`, `actualizarProcesamiento` y `eliminar`.
  - Las dos actualizaciones están separadas, para que una edición del usuario y un reporte de Streaming que llegan al mismo tiempo no se pisen.
  - Cada una usa `$set` solo con sus campos.
- `RepositorioMongo`: implementación con `mongocxx`. Es la **única** clase que conoce MongoDB.
  - La conversión entre `VideoMetadata` y BSON son funciones privadas suyas.
  - Usa `mongocxx::pool`, porque `httplib` atiende peticiones en varios hilos y un `mongocxx::client` no se puede compartir entre hilos.
  - `mongocxx::instance` se crea una sola vez, en `main`.
- `ServicioMetadata`: reglas de negocio (validaciones, permisos de autor, visibilidad, transiciones de estado). Depende de `IRepositorioMetadata`, que se le inyecta por el constructor.
- `ControladorMetadata`: registra las rutas de `httplib`, traduce entre HTTP y el servicio y no contiene lógica de negocio.
- Las pruebas unitarias de `ServicioMetadata` usan un *mock* de `IRepositorioMetadata`, sin necesitar MongoDB.
- Las pruebas de `RepositorioMongo` corren contra un MongoDB real, levantado como servicio en el CI.

---

## 8. API de Streaming (C++ + FFMPEG + MPEG-DASH)

### 8.1 Formato de Streaming
Se usa **MPEG-DASH**, compatible con Shaka Player y Dash.js. Cada video se almacena así:
```
<DIR_VIDEOS>/{id_video}/
├── original.<ext>        (se borra tras transcodificar correctamente)
├── manifest.mpd
├── init-0.m4s, init-1.m4s, init-2.m4s, init-3.m4s
└── seg-0-00001.m4s, seg-1-00001.m4s, ...
```
Mientras el estado no sea `"listo"`, Streaming no sirve ningún archivo de la carpeta (sección 6.7).

### 8.2 Endpoints
| Método | Ruta | Respuesta / `Content-Type` |
|---|---|---|
| `POST` | `/videos` | `202` con `{"id_video", "estado"}`. Multipart; tamaño máximo `MAX_BYTES_SUBIDA` |
| `GET` | `/videos/{id}/manifest.mpd` | `application/dash+xml` |
| `GET` | `/videos/{id}/{segmento}.m4s` | `video/iso.segment`; soporta el encabezado `Range` |
| `DELETE` | `/videos/{id}` | `204` |

Todos exigen `X-Usuario-Id`.

### 8.3 Transcodificación
1. **Análisis:** se analiza el original con `ffprobe -v error -print_format json -show_format -show_streams original.<ext>`, que da la duración, la altura y si tiene pista de audio. Si no hay pista de video, la transcodificación falla con `estado = "error"`.
2. **Calidades:** se elige la escalera de calidades sin superar la altura original:

| Altura | Bitrate de video |
|---|---|
| 1080 | 5000k |
| 720 | 2800k |
| 360 | 800k |

   Si el original mide menos de 360 px de alto, se genera una sola calidad con su altura redondeada a par, a 500k.
3. **Comando:** para un original de 1080p con audio, el comando queda así:
```
ffmpeg -hide_banner -loglevel error -y -i original.mp4 \
  -filter_complex "[0:v]split=3[s0][s1][s2];[s0]scale=-2:1080[v0];[s1]scale=-2:720[v1];[s2]scale=-2:360[v2]" \
  -map "[v0]" -c:v:0 libx264 -b:v:0 5000k -maxrate:v:0 5350k -bufsize:v:0 10000k \
  -map "[v1]" -c:v:1 libx264 -b:v:1 2800k -maxrate:v:1 2996k -bufsize:v:1 5600k \
  -map "[v2]" -c:v:2 libx264 -b:v:2 800k  -maxrate:v:2 856k  -bufsize:v:2 1600k \
  -preset veryfast -profile:v main -pix_fmt yuv420p \
  -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*4)" \
  -map 0:a:0 -c:a aac -b:a 128k -ac 2 \
  -f dash -seg_duration 4 -use_template 1 -use_timeline 1 \
  -init_seg_name 'init-$RepresentationID$.m4s' \
  -media_seg_name 'seg-$RepresentationID$-$Number%05d$.m4s' \
  -adaptation_sets "id=0,streams=v id=1,streams=a" \
  manifest.mpd
```
   - `scale=-2:<altura>` conserva la proporción original. Un tamaño fijo como `-s 1920x1080` deformaría los videos verticales o 4:3.
   - `-force_key_frames` pone un keyframe exactamente cada 4 segundos, sin importar los fps del original. Así todas las calidades se cortan en los mismos instantes y el reproductor puede cambiar de una a otra. `-g 48` solo funciona si el video es de 24 fps.
   - Si `ffprobe` no encontró audio, se omiten `-map 0:a:0`, las opciones de audio y `streams=a` en `-adaptation_sets`.

### 8.4 Reglas de Seguridad y Buenas Prácticas (obligatorias)
- FFMPEG y `ffprobe` se ejecutan con `fork()` + `execvp()` (o `posix_spawn`), pasando los argumentos como arreglo. **Queda prohibido usar `system()` o `popen()` con cadenas concatenadas**, porque permiten inyección de comandos.
- **Validación de rutas:** el `{id}` se valida como ObjectId de 24 caracteres hexadecimales. El nombre del segmento se valida con la expresión `^(init|seg)-[0-9]+(-[0-9]{5})?\.m4s$`. Así se evita el *path traversal* (`../`).
- **Pool de transcodificación:** la transcodificación corre en un `PoolTranscodificacion` (cola + N hilos de trabajo, con N = `HILOS_TRANSCODIFICACION`). Nunca bloquea el hilo que atiende la petición HTTP.
- **Cuota:** se reserva **antes** de transcodificar (sección 6.6, paso 4) y se libera si algo falla.
- **Subidas:**
  - El archivo subido nunca se carga completo en memoria.
  - Una subida interrumpida o rechazada borra su archivo parcial (RAII).
  - Dos subidas no pueden escribir en la misma carpeta.

### 8.5 Diseño de Clases (espacio de nombres `tecflix::streaming`)
- `ITranscodificador` / `TranscodificadorFfmpeg`: construye los argumentos y ejecuta FFMPEG y `ffprobe`.
- `EjecutorProcesos`: encapsula `posix_spawn` y la lectura de la salida del proceso.
- `GestorAlmacenamiento`: crea, lee y borra carpetas y segmentos, valida ids y nombres y calcula tamaños.
- `PoolTranscodificacion`: cola de trabajos thread-safe (`std::mutex` + `std::condition_variable`).
- `IClienteUsuarios` / `ClienteUsuariosHttp`: consulta la cuota y el nombre, y reserva o libera uso.
- `IClienteMetadata` / `ClienteMetadataHttp`: crea, actualiza y borra registros, y consulta si un usuario puede ver un video.
- `ServicioStreaming`: orquesta los flujos de las secciones 6.6, 6.7 y 6.8, incluidas las compensaciones.
- `ControladorStreaming`: rutas HTTP, sin lógica de negocio.
- Las pruebas usan *mocks* de `ITranscodificador`, `IClienteUsuarios` e `IClienteMetadata`. `TranscodificadorFfmpeg` se prueba con videos sintéticos generados con `ffmpeg -f lavfi`, para no guardar videos en el repositorio.

---

## 9. Telemetría (OpenTelemetry), común a los tres APIs
- **Traces:**
  - Un *span* por petición HTTP, con la ruta normalizada (`/videos/:id`), el método y el código de respuesta.
  - El contexto se propaga con el encabezado `traceparent` (W3C) en las llamadas entre APIs, para que un trace de subida muestre Streaming → Usuarios → Metadata.
  - La transcodificación tiene su propio *span*, hijo del de la subida.
- **Métricas mínimas:**
  - `http_peticiones_total` (etiquetas: `ruta`, `metodo`, `codigo`), con la ruta normalizada para no crear una serie por video
  - `http_duracion_ms` (histograma)
  - `http_errores_total`
- **Métricas específicas:**
  - Metadata: `mongo_consulta_duracion_ms`
  - Streaming: `transcodificacion_duracion_seg`, `transcodificaciones_en_cola`, `segmentos_servidos_total`, `bytes_servidos_total`, `compensaciones_fallidas_total`
  - Usuarios: `btree_busqueda_duracion_ms`, `btree_lecturas_disco_total`
- **Exportación:** los APIs envían todo por OTLP al collector. El collector reenvía los traces y las métricas al Aspire Dashboard, y expone las métricas a Prometheus en el puerto 8889. Prometheus solo guarda métricas: los traces se ven en Aspire.

---

## 10. Estructura del Repositorio
```
tecflix-project/
├── SPEC.md
├── docker-compose.yml        (mongo, otel-collector, prometheus, aspire-dashboard, los tres APIs)
├── common/                   (servidor HTTP base, errores, CORS, /salud, configuración, telemetría)
├── user-api/
├── metadata-api/
│   ├── CMakeLists.txt
│   ├── include/   video_metadata.hpp, repositorio_metadata.hpp, repositorio_mongo.hpp,
│   │              servicio_metadata.hpp, controlador_metadata.hpp
│   ├── src/       (un .cpp por cada .hpp) + main.cpp
│   └── tests/
├── streaming-api/
│   ├── CMakeLists.txt
│   ├── include/   transcodificador.hpp, ejecutor_procesos.hpp, gestor_almacenamiento.hpp,
│   │              pool_transcodificacion.hpp, cliente_usuarios.hpp, cliente_metadata.hpp,
│   │              servicio_streaming.hpp, controlador_streaming.hpp
│   ├── src/
│   └── tests/
├── web-app/
└── .github/workflows/ci.yml