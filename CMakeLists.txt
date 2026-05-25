cmake_minimum_required(VERSION 3.16)

project(tsedge
    VERSION 0.1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(TSEDGE_PUBLIC_HEADERS
    include/tsedge.h
)

set(TSEDGE_PRIVATE_HEADERS
    src/bitstream.h
    src/block.h
    src/compress.h
    src/csv.h
    src/db.h
    src/segment.h
    src/series.h
    src/wal.h
)

set(TSEDGE_SOURCES
    src/tsedge.c
    src/bitstream.c
    src/block.c
    src/compress.c
    src/csv.c
    src/db.c
    src/segment.c
    src/series.c
    src/wal.c
)

set(TSEDGE_TEST_SOURCES
    tests/test_main.c
    tests/test_compress.c
    tests/test_db.c
    tests/test_recovery.c
)

set(TSEDGE_DEMO_SOURCES
    examples/tsedge_demo.c
)

set(TSEDGE_BENCH_SOURCES
    bench/tsedge_bench.c
)

find_library(TSEDGE_MATH_LIBRARY m)

function(tsedge_link_math target_name)
    if(TSEDGE_MATH_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${TSEDGE_MATH_LIBRARY})
    endif()
endfunction()

add_library(tsedge SHARED
    ${TSEDGE_SOURCES}
    ${TSEDGE_PUBLIC_HEADERS}
    ${TSEDGE_PRIVATE_HEADERS}
)

add_library(tsedge_static STATIC
    ${TSEDGE_SOURCES}
    ${TSEDGE_PUBLIC_HEADERS}
    ${TSEDGE_PRIVATE_HEADERS}
)

set_target_properties(tsedge PROPERTIES
    OUTPUT_NAME tsedge
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

set_target_properties(tsedge_static PROPERTIES
    OUTPUT_NAME tsedge
)

target_include_directories(tsedge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(tsedge_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tsedge PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(tsedge_static PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tsedge_demo ${TSEDGE_DEMO_SOURCES})
target_link_libraries(tsedge_demo PRIVATE tsedge)
tsedge_link_math(tsedge_demo)

add_executable(tsedge_bench ${TSEDGE_BENCH_SOURCES})
target_link_libraries(tsedge_bench PRIVATE tsedge)
tsedge_link_math(tsedge_bench)

enable_testing()

add_executable(tsedge_tests ${TSEDGE_TEST_SOURCES})
target_link_libraries(tsedge_tests PRIVATE tsedge)
target_include_directories(tsedge_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
tsedge_link_math(tsedge_tests)

add_test(NAME tsedge_tests COMMAND $<TARGET_FILE:tsedge_tests>)
