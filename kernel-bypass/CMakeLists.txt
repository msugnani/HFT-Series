cmake_minimum_required(VERSION 3.16)
project(rxsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(rxsim
    src/virtual_interface.cpp
    src/simulated_nic.cpp
)
target_include_directories(rxsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(rxsim PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rxsim PRIVATE -Wall -Wextra -Werror)
endif()

if(UNIX)
    target_compile_definitions(rxsim PRIVATE _GNU_SOURCE)
endif()

function(rxsim_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE rxsim)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

enable_testing()
rxsim_add_test(test_spsc tests/test_spsc.cpp)
rxsim_add_test(test_ownership tests/test_ownership.cpp)

function(rxsim_add_app name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
    target_link_libraries(${name} PRIVATE rxsim)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -O3 -march=native)
    endif()
endfunction()

rxsim_add_app(sender apps/sender.cpp)
rxsim_add_app(receiver apps/receiver.cpp)
rxsim_add_app(baseline_recvfrom apps/baseline_recvfrom.cpp)
