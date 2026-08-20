function(hft_add_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE hft_common)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
        target_compile_options(${name} PRIVATE
            $<$<CONFIG:Release>:-O3;-march=native>
            $<$<CONFIG:RelWithDebInfo>:-O3;-march=native>)
    endif()
endfunction()

function(hft_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE hft_common)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()
