find_package(Threads REQUIRED)

add_library(hft_common INTERFACE)
target_include_directories(hft_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hft_common INTERFACE Threads::Threads)
target_compile_features(hft_common INTERFACE cxx_std_20)

if(UNIX)
    target_compile_definitions(hft_common INTERFACE _GNU_SOURCE)
endif()
