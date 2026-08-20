include(${CMAKE_SOURCE_DIR}/cmake/hft.cmake)

hft_add_example(batching apps/batching.cpp)
hft_add_example(spsc_logger apps/spsc_logger.cpp)
hft_add_example(parse apps/parse.cpp)
hft_add_test(test_pool tests/test_pool.cpp)
