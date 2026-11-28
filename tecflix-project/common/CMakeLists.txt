# Biblioteca común de los APIs (SPEC 6.3): servidor HTTP base, errores, CORS, /salud,
# configuración, telemetría y cliente HTTP entre APIs.
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/dependencias.cmake)

option(TECFLIX_COBERTURA "Instrumentar el código propio para medir cobertura (gcov/lcov)" OFF)

# Aplica las opciones de cobertura solo a los objetivos propios, no a las dependencias.
function(tecflix_cobertura objetivo)
    if(TECFLIX_COBERTURA)
        target_compile_options(${objetivo} PRIVATE --coverage -O0 -g)
        target_link_options(${objetivo} PUBLIC --coverage)
    endif()
endfunction()

add_library(tecflix_common STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/errores.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/configuracion.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/telemetria.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/utilidades_http.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/servidor_http.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/cliente_http_interno.cpp
)
target_include_directories(tecflix_common PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(tecflix_common PUBLIC
    httplib::httplib
    nlohmann_json::nlohmann_json
    opentelemetry_trace
    opentelemetry_metrics
    opentelemetry_resources
    opentelemetry_exporter_otlp_http
    opentelemetry_exporter_otlp_http_metric
)
target_compile_features(tecflix_common PUBLIC cxx_std_17)
target_compile_options(tecflix_common PRIVATE -Wall -Wextra)
tecflix_cobertura(tecflix_common)

# Pruebas de la biblioteca común; cada API las compila junto con las suyas para medir
# la cobertura de common/ en el mismo reporte.
set(TECFLIX_PRUEBAS_COMMON
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_errores_configuracion.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_utilidades_http.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_servidor_http.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_telemetria.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_cliente_http_interno.cpp
)
set(TECFLIX_LIBS_PRUEBAS_COMMON opentelemetry_exporter_in_memory opentelemetry_exporter_in_memory_metric)
# Se publican al CMakeLists.txt del API que agrega esta carpeta con add_subdirectory().
set(TECFLIX_PRUEBAS_COMMON ${TECFLIX_PRUEBAS_COMMON} PARENT_SCOPE)
set(TECFLIX_LIBS_PRUEBAS_COMMON ${TECFLIX_LIBS_PRUEBAS_COMMON} PARENT_SCOPE)
set(TECFLIX_DIR_PRUEBAS_COMMON ${CMAKE_CURRENT_LIST_DIR}/tests PARENT_SCOPE)
