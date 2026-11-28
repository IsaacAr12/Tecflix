# Dependencias de C++ compartidas por los APIs (SPEC 6.3). Todas se descargan con
# FetchContent en una versión fija, para que desarrollo, CI y Docker compilen lo mismo.
# Requieren del sistema: libprotobuf-dev, protobuf-compiler y libcurl4-openssl-dev
# (para el exportador OTLP de OpenTelemetry).
include_guard(GLOBAL)
include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# --- nlohmann/json ---
# OVERRIDE_FIND_PACKAGE hace que find_package(nlohmann_json) de OpenTelemetry use esta copia.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    OVERRIDE_FIND_PACKAGE)

# --- cpp-httplib (solo encabezado) ---
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.7
    GIT_SHALLOW TRUE)

# --- opentelemetry-cpp (solo SDK + exportador OTLP por HTTP) ---
set(WITH_OTLP_HTTP ON CACHE BOOL "" FORCE)
set(WITH_OTLP_GRPC OFF CACHE BOOL "" FORCE)
set(WITH_OTLP_FILE OFF CACHE BOOL "" FORCE)
set(WITH_ABSEIL OFF CACHE BOOL "" FORCE)
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WITH_BENCHMARK OFF CACHE BOOL "" FORCE)
set(WITH_FUNC_TESTS OFF CACHE BOOL "" FORCE)
set(OPENTELEMETRY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opentelemetry-cpp
    GIT_REPOSITORY https://github.com/open-telemetry/opentelemetry-cpp.git
    GIT_TAG v1.22.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "third_party/opentelemetry-proto")

# --- GoogleTest + GoogleMock ---
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(nlohmann_json httplib opentelemetry-cpp googletest)
