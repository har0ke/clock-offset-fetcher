cmake_minimum_required(VERSION 3.9)
project(cofetcher)

set(CMAKE_CXX_STANDARD 14)

SET(COVERAGE OFF CACHE BOOL "Coverage")

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
target_link_libraries(cofetcher PUBLIC ${CMAKE_THREAD_LIBS_INIT} cofetcherbase)


add_executable(cofetcher_example examples/main.cpp)
target_link_libraries(cofetcher_example PUBLIC cofetcher)



include(cmake/gtest.cmake)

add_executable(run_tests tests/tests.cpp)
target_link_libraries(run_tests PUBLIC gtest cofetcher)
add_test(AllTestsInCofetcher tests)

if (COVERAGE)


    target_compile_options(run_tests PRIVATE --coverage)
    target_link_libraries(run_tests PRIVATE --coverage)

    target_compile_options(cofetcher PRIVATE --coverage)
    target_link_libraries(cofetcher PRIVATE --coverage)

    target_compile_options(cofetcherbase PRIVATE --coverage)
    target_link_libraries(cofetcherbase PRIVATE --coverage)
endif()


# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
# set(CMAKE_CXX_OUTPUT_EXTENSION_REPLACE 1)
