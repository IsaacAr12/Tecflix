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
- **Detalle:** ver sección 7.

### C. API de Streaming (C++)
- **Tipo:** REST API en C++ integrado con FFMPEG para transcodificación y segmentación de video en bloques multimedia.
- **Detalle:** ver sección 8.

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
- El archivo de índices empieza con una cabecera de 8 bytes que guarda la posición de la raíz (`uint64_t`). Los nodos se escriben a partir del byte 8, por lo que la posición `0` significa "sin nodo" (hijo nulo o árbol vacío). Si el archivo no existe, se crea con la cabecera y un nodo raíz vacío en el byte 8.

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

- **Salud:** todos los APIs exponen `GET /salud` y responden `{"estado":