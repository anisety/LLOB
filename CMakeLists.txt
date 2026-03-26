cmake_minimum_required(VERSION 3.15)
project(LLOB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(lob_test
    lob.cpp
    lob_test.cpp
)

target_compile_options(lob_test PRIVATE -O2)
