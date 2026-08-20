cmake_minimum_required(VERSION 3.16)
project(hft_series LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_subdirectory(common)
add_subdirectory(01-cache)
add_subdirectory(02-memory-model)
add_subdirectory(03-cpp-cost-model)
add_subdirectory(04-linux-latency)
add_subdirectory(05-profiling)
add_subdirectory(06-hot-path)
add_subdirectory(07-order-book)
add_subdirectory(kernel-bypass)
