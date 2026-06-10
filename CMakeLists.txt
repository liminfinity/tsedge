cmake_minimum_required(VERSION 3.16)

project(tsedge
    VERSION 0.1.0
    LANGUAGES C
)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(TSEDGE_WAL_FSYNC "Flush and fsync WAL entries before append returns" OFF)
set(TSEDGE_SEGMENT_MAX_BYTES "" CACHE STRING "Override segment rotation limit in bytes")

set(TSEDGE_PUBLIC_HEADERS
    include/tsedge.h
)

set(TSEDGE_PRIVATE_HEADERS
    src/compression/bitstream.h
    src/compression/compress.h
    src/core/db.h
    src/core/series.h
    src/core/series_index.h
    src/core/series_list.h
    src/core/series_query.h
    src/core/series_retention.h
    src/core/series_stats.h
    src/core/verify.h
    src/export/csv.h
    src/storage/block.h
    src/storage/segment.h
    src/storage/segment_files.h
    src/storage/segment_reader.h
    src/storage/segment_rotation.h
    src/storage/segment_writer.h
    src/storage/wal.h
)

set(TSEDGE_SOURCES
    src/api/tsedge.c
    src/compression/bitstream.c
    src/compression/compress.c
    src/core/db.c
    src/core/series.c
    src/core/series_index.c
    src/core/series_list.c
    src/core/series_query.c
    src/core/series_retention.c
    src/core/series_stats.c
    src/core/verify.c
    src/export/csv.c
    src/storage/block.c
    src/storage/segment_files.c
    src/storage/segment_reader.c
    src/storage/segment_rotation.c
    src/storage/segment_writer.c
    src/storage/wal.c
)

set(TSEDGE_TEST_SOURCES
    tests/test_main.c
    tests/test_block.c
    tests/test_compress.c
    tests/test_db.c
    tests/test_flush.c
    tests/test_list_series.c
    tests/test_retention.c
    tests/test_recovery.c
    tests/test_segments.c
    tests/test_stats.c
    tests/test_verify.c
)

set(TSEDGE_DEMO_SOURCES
    examples/tsedge_demo.c
)

set(TSEDGE_ECOPOST_AGENT_SOURCES
    examples/ecopost/ecopost_agent.c
    examples/ecopost/ecopost_commands.c
    examples/ecopost/ecopost_config.c
    examples/ecopost/ecopost_fs.c
    examples/ecopost/ecopost_ops.c
    examples/ecopost/ecopost_output.c
    examples/ecopost/ecopost_sensors.c
    examples/ecopost/ecopost_state.c
)

set(TSEDGE_CRASH_WRITER_SOURCES
    examples/crash_recovery/tsedge_crash_writer.c
)

set(TSEDGE_RECOVER_CHECK_SOURCES
    examples/crash_recovery/tsedge_recover_check.c
)

set(TSEDGE_BENCH_SOURCES
    bench/tsedge_bench.c
)

set(TSEDGE_FILE_BENCH_SOURCES
    bench/file_bench.c
)

set(TSEDGE_SQLITE_BENCH_SOURCES
    bench/sqlite_bench.c
)

find_library(TSEDGE_MATH_LIBRARY m)
find_package(SQLite3 QUIET)

set(TSEDGE_INTERNAL_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/api
    ${CMAKE_CURRENT_SOURCE_DIR}/src/compression
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/export
    ${CMAKE_CURRENT_SOURCE_DIR}/src/storage
)

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

add_library(tsedge_testlib STATIC
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

set_target_properties(tsedge_testlib PROPERTIES
    OUTPUT_NAME tsedge_test
)

target_include_directories(tsedge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${TSEDGE_INTERNAL_INCLUDE_DIRS}
)

target_include_directories(tsedge_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${TSEDGE_INTERNAL_INCLUDE_DIRS}
)

target_include_directories(tsedge_testlib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${TSEDGE_INTERNAL_INCLUDE_DIRS}
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tsedge PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(tsedge_static PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(tsedge_testlib PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TSEDGE_WAL_FSYNC)
    target_compile_definitions(tsedge PRIVATE TSEDGE_WAL_FSYNC)
    target_compile_definitions(tsedge_static PRIVATE TSEDGE_WAL_FSYNC)
    target_compile_definitions(tsedge_testlib PRIVATE TSEDGE_WAL_FSYNC)
endif()

if(TSEDGE_SEGMENT_MAX_BYTES)
    target_compile_definitions(tsedge PRIVATE TSEDGE_SEGMENT_MAX_BYTES=${TSEDGE_SEGMENT_MAX_BYTES})
    target_compile_definitions(tsedge_static PRIVATE TSEDGE_SEGMENT_MAX_BYTES=${TSEDGE_SEGMENT_MAX_BYTES})
endif()

target_compile_definitions(tsedge_testlib PRIVATE TSEDGE_SEGMENT_MAX_BYTES=8192)

add_executable(tsedge_demo ${TSEDGE_DEMO_SOURCES})
target_link_libraries(tsedge_demo PRIVATE tsedge)
tsedge_link_math(tsedge_demo)

add_executable(tsedge_ecopost_agent ${TSEDGE_ECOPOST_AGENT_SOURCES})
target_link_libraries(tsedge_ecopost_agent PRIVATE tsedge)
target_include_directories(tsedge_ecopost_agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/ecopost
)
tsedge_link_math(tsedge_ecopost_agent)

if(UNIX)
    add_executable(tsedge_crash_writer ${TSEDGE_CRASH_WRITER_SOURCES})
    target_link_libraries(tsedge_crash_writer PRIVATE tsedge)
    tsedge_link_math(tsedge_crash_writer)

    add_executable(tsedge_recover_check ${TSEDGE_RECOVER_CHECK_SOURCES})
    target_link_libraries(tsedge_recover_check PRIVATE tsedge)
    tsedge_link_math(tsedge_recover_check)
endif()

add_executable(tsedge_bench ${TSEDGE_BENCH_SOURCES})
target_link_libraries(tsedge_bench PRIVATE tsedge)
tsedge_link_math(tsedge_bench)

add_executable(file_bench ${TSEDGE_FILE_BENCH_SOURCES})
tsedge_link_math(file_bench)

if(SQLite3_FOUND)
    add_executable(sqlite_bench ${TSEDGE_SQLITE_BENCH_SOURCES})
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(sqlite_bench PRIVATE SQLite3::SQLite3)
    else()
        target_include_directories(sqlite_bench PRIVATE ${SQLite3_INCLUDE_DIRS})
        target_link_libraries(sqlite_bench PRIVATE ${SQLite3_LIBRARIES})
    endif()
    tsedge_link_math(sqlite_bench)
endif()

enable_testing()

add_executable(tsedge_tests ${TSEDGE_TEST_SOURCES})
target_link_libraries(tsedge_tests PRIVATE tsedge_testlib)
target_include_directories(tsedge_tests PRIVATE ${TSEDGE_INTERNAL_INCLUDE_DIRS})
tsedge_link_math(tsedge_tests)

add_test(NAME tsedge_tests COMMAND $<TARGET_FILE:tsedge_tests>)

if(UNIX)
    add_test(NAME tsedge_crash_recovery_demo
        COMMAND sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_crash_recovery_demo.sh
            $<TARGET_FILE:tsedge_crash_writer>
            $<TARGET_FILE:tsedge_recover_check>
    )
endif()

install(TARGETS tsedge tsedge_static
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS tsedge_demo tsedge_ecopost_agent tsedge_bench file_bench
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TARGET tsedge_crash_writer)
    install(TARGETS tsedge_crash_writer tsedge_recover_check
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(TARGET sqlite_bench)
    install(TARGETS sqlite_bench
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/tsedge.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    ${CMAKE_CURRENT_SOURCE_DIR}/INSTALL.md
    DESTINATION .
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
        DESTINATION .
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/docs)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs
        DESTINATION .
        FILES_MATCHING PATTERN "*.md"
    )
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" TSEDGE_PACKAGE_SYSTEM)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TSEDGE_PACKAGE_SYSTEM "macos")
endif()

if(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" TSEDGE_PACKAGE_ARCH)
else()
    set(TSEDGE_PACKAGE_ARCH "unknown")
endif()

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "tsedge")
set(CPACK_PACKAGE_VENDOR "TSEdge")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${TSEDGE_PACKAGE_SYSTEM}-${TSEDGE_PACKAGE_ARCH}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

include(CPack)
