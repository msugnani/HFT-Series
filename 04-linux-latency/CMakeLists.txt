include(${CMAKE_SOURCE_DIR}/cmake/hft.cmake)

hft_add_example(clocks apps/clocks.cpp)
hft_add_example(page_faults apps/page_faults.cpp)
hft_add_example(jitter apps/jitter.cpp)
