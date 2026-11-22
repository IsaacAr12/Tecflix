cmake_minimum_required(VERSION 3.15)
project(TecflixUserAPI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(user_api_bin
    src/main.cpp
    src/btree_index.cpp
)