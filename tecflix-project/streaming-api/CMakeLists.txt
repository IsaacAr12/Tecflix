cmake_minimum_required(VERSION 3.24)
project(TecflixStreamingAPI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TECFLIX_PRUEBAS "Compilar las pruebas unitarias" ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR}/common)

add_library(streaming_core STATIC
    src/ejecutor_procesos.cpp
    src/transcodificador.cpp
    src/gestor_almacenamiento.cpp
    src/pool_transcodificacion.cpp
    src/cliente_metadata.cpp
    src/cliente_usuarios.cpp
    src/servicio_streaming.cpp
    src/controlador_streaming.cpp
)
target_include_directories(streaming_core PUBLIC include)
target_link_libraries(streaming_core PUBLIC tecflix_common)
target_compile_options(streaming_core PRIVATE -Wall -Wextra)
tecflix_cobertura(streaming_core)

add_executable(streaming_api src/main.cpp)
target_link_libraries(streaming_api PRIVATE streaming_core)

if(TECFLIX_PRUEBAS)
    enable_testing()
    add_executable(streaming_tests
        tests/test_gestor_almacenamiento.cpp
        tests/test_pool_transcodificacion.cpp
        tests/test_transcodificador.cpp
        tests/test_clientes_http.cpp
        tests/test_servicio_streaming.cpp
        tests/test_controlador_streaming.cpp
        ${TECFLIX_PRUEBAS_COMMON}
    )
    target_include_directories(streaming_tests PRIVATE tests ${TECFLIX_DIR_PRUEBAS_COMMON})
    target_link_libraries(streaming_tests PRIVATE streaming_core ${TECFLIX_LIBS_PRUEBAS_COMMON} GTest::gmock_main)
    tecflix_cobertura(streaming_tests)
    include(GoogleTest)
    # Las pruebas de FFMPEG generan videos reales; se les da más tiempo.
    gtest_discover_tests(streaming_tests DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 300)
endif()
