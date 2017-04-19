cmake_minimum_required(VERSION 2.8.4)

project(tinyexpr)

option(build_tinyexpr_test "Build TinyExpr tests." OFF)
option(build_tinyexpr_test_pr "Build TinyExpr tests PR." OFF)
option(build_tinyexpr_bench "Build TinyExpr benchmark." OFF)
option(build_tinyexpr_example "Build TinyExpr example." OFF)
option(build_tinyexpr_example2 "Build TinyExpr example 2." OFF)
option(build_tinyexpr_example3 "Build TinyExpr example 3." OFF)

find_library(MATH_LIB m)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ansi -Wall -Wshadow -fPIC -O3")

set(SOURCE_FILES
        tinyexpr.c
        tinyexpr.h
        )

add_library(tinyexpr STATIC ${SOURCE_FILES})
target_link_libraries(tinyexpr ${MATH_LIB})

if (build_tinyexpr_test)
    add_executable(tinyexpr_test test.c)
    target_link_libraries(tinyexpr_test tinyexpr)
endif()

if (build_tinyexpr_test_pr)
    add_definitions(-DTE_POW_FROM_RIGHT -DTE_NAT_LOG)
    add_executable(tinyexpr_test_pr test.c)
    target_link_libraries(tinyexpr_test_pr tinyexpr)
endif()

if (build_tinyexpr_bench)
    add_executable(tinyexpr_benchmark benchmark.c)
    target_link_libraries(tinyexpr_benchmark tinyexpr)
endif()

if (build_tinyexpr_example)
    add_executable(tinyexpr_example example.c)
    target_link_libraries(tinyexpr_example tinyexpr)
endif()

if (build_tinyexpr_example2)
    add_executable(tinyexpr_example2 example2.c)
    target_link_libraries(tinyexpr_example2 tinyexpr)
endif()

if (build_tinyexpr_example3)
    add_executable(tinyexpr_example3 example3.c)
    target_link_libraries(tinyexpr_example3 tinyexpr)
endif()
