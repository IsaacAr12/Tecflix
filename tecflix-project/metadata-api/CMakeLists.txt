cmake_minimum_required(VERSION 3.24)
project(TecflixMetadataAPI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TECFLIX_PRUEBAS "Compilar las pruebas unitarias" ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR}/common)

# Driver oficial de MongoDB para C++ (SPEC 6.3). La versión 3.10 usa la libmongoc del
# sistema (paquete libmongoc-dev, 1.25 o superior).
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_VERSION 3.10.2 CACHE STRING "" FORCE)
set(MONGOCXX_OVERRIDE_DEFAULT_INSTALL_PREFIX OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mongo-cxx-driver
    GIT_REPOSITORY https://github.com/mongodb/mongo-cxx-driver.git
    GIT_TAG r3.10.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(mongo-cxx-driver)

add_library(metadata_core STATIC
    src/video_metadata.cpp
    src/servicio_metadata.cpp
    src/repositorio_mongo.cpp
    src/controlador_metadata.cpp
)
target_include_directories(metadata_core PUBLIC include)
target_link_libraries(metadata_core PUBLIC tecflix_common mongocxx_static)
target_compile_options(metadata_core PRIVATE -Wall -Wextra)
tecflix_cobertura(metadata_core)

add_executable(metadata_api src/main.cpp)
target_link_libraries(metadata_api PRIVATE metadata_core)

if(TECFLIX_PRUEBAS)
    enable_testing()
    add_executable(metadata_tests
        tests/test_video_metadata.cpp
        tests/test_servicio_metadata.cpp
        tests/test_controlador_metadata.cpp
        tests/test_repositorio_mongo.cpp
        ${TECFLIX_PRUEBAS_COMMON}
    )
    target_include_directories(metadata_tests PRIVATE tests ${TECFLIX_DIR_PRUEBAS_COMMON})
    target_link_libraries(metadata_tests PRIVATE metadata_core ${TECFLIX_LIBS_PRUEBAS_COMMON} GTest::gmock_main)
    tecflix_cobertura(metadata_tests)
    include(GoogleTest)
    gtest_discover_tests(metadata_tests DISCOVERY_TIMEOUT 60)
endif()
