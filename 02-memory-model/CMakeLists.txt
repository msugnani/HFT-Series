include(${CMAKE_SOURCE_DIR}/cmake/hft.cmake)

hft_add_example(message_passing apps/message_passing.cpp)
hft_add_example(store_buffer apps/store_buffer.cpp)
hft_add_test(test_spsc_hft tests/test_spsc.cpp)
