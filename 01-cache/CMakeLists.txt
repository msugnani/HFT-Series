include(${CMAKE_SOURCE_DIR}/cmake/hft.cmake)

hft_add_example(false_sharing apps/false_sharing.cpp)
hft_add_example(aos_vs_soa apps/aos_vs_soa.cpp)
hft_add_example(access_pattern apps/access_pattern.cpp)
hft_add_test(test_layout tests/test_layout.cpp)
