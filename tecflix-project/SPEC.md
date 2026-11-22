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
