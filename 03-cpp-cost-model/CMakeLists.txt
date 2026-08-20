include(${CMAKE_SOURCE_DIR}/cmake/hft.cmake)

hft_add_example(virtual_dispatch apps/virtual_dispatch.cpp)
hft_add_example(alloc_vs_pool apps/alloc_vs_pool.cpp)
hft_add_example(hashmap_vs_array apps/hashmap_vs_array.cpp)
