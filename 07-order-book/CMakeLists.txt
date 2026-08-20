include(${CMAKE_SOURCE_DIR}/cmake/hft.cmake)

set(LOB_INC ${CMAKE_CURRENT_SOURCE_DIR}/include)

hft_add_example(book_demo apps/book_demo.cpp)
hft_add_example(book_bench apps/book_bench.cpp)
hft_add_test(test_book tests/test_book.cpp)

target_include_directories(book_demo PRIVATE ${LOB_INC})
target_include_directories(book_bench PRIVATE ${LOB_INC})
target_include_directories(test_book PRIVATE ${LOB_INC})
