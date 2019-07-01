cmake_minimum_required(VERSION 3.14)
project(cofetcher)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)


add_library(cofetcherbase
        include/clock_offset.h
        src/clock_offset.cpp)
target_include_directories(cofetcherbase PUBLIC include)

add_library(cofetcher
        include/clock_offset_udp_server.h
        src/clock_offset_udp_server.cpp)

target_include_directories(cofetcher PUBLIC include)
target_include_directories(cofetcher PUBLIC external/asio/asio/include)

target_compile_definitions(cofetcher PUBLIC -DASIO_STANDALONE)

find_package (Threads)
target_link_libraries(cofetcher ${CMAKE_THREAD_LIBS_INIT} cofetcherbase)


add_executable(cofetcher_example examples/main.cpp)
target_link_libraries(cofetcher_example cofetcher)



include(cmake/gtest.cmake)

add_executable(tests tests/tests.cpp)
target_link_libraries(tests gtest cofetcher)
add_test(AllTestsInCofetcher tests)


# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
# set(CMAKE_CXX_OUTPUT_EXTENSION_REPLACE 1)
