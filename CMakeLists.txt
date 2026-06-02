# ___________________________________________________________________________________
# h5cpp-comparison-benchmarks
# Cross-library HDF5 C++ benchmark suite — Linux only.
# ___________________________________________________________________________________

cmake_minimum_required(VERSION 3.22.0)

project(h5cpp-comparison-benchmarks
    VERSION 0.1.0
    LANGUAGES CXX C
    DESCRIPTION "Cross-library HDF5 C++ benchmark suite"
)

# ─── Linux-only guard ─────────────────────────────────────────────────────────────
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project is Linux-only.")
endif()

# ─── Standard Settings ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(WARNING "In-source builds are discouraged.")
endif()

# ─── System Dependencies ──────────────────────────────────────────────────────────
# Use HDF5 2.1.1 from the HDF_Group installation (not the system package).
set(HDF5_ROOT "/usr/local/HDF_Group/HDF5/2.1.1")
list(APPEND CMAKE_PREFIX_PATH ${HDF5_ROOT}/cmake)
find_package(HDF5 REQUIRED COMPONENTS C)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# ─── Third-party Include Directories ──────────────────────────────────────────────
set(BENCHMARK_THIRDPARTY_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/doctest
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nanobench
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libdeflate
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zstd
)

# ─── Wrapper Library Includes ─────────────────────────────────────────────────────
# Each wrapper is added as a header-only include path.
# Only headers + licences are kept in-tree; build artefacts are excluded.
set(WRAPPER_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/highfive/include
    ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/dmsc-h5cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/h5pp/include
)

# ─── Common Benchmark Compile Settings ────────────────────────────────────────────
function(add_benchmark target_name source_file)
    set(options)
    set(one_value_args)
    set(multi_value_args EXTRA_INCLUDES EXTRA_LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    add_executable(${target_name} ${source_file})
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${BENCHMARK_THIRDPARTY_INCLUDES}
        ${WRAPPER_INCLUDES}
        ${HDF5_C_INCLUDE_DIRS}
        ${ARG_EXTRA_INCLUDES}
    )
    target_link_libraries(${target_name} PRIVATE
        ${HDF5_LIBRARIES}
        ZLIB::ZLIB
        Threads::Threads
        ${ARG_EXTRA_LIBRARIES}
    )
    set_target_properties(${target_name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endfunction()

# ─── C API Reference Implementation ─────────────────────────────────────────────────
# Standalone C program: copy dataset with gzip level 6, 5-minute hard cap
add_executable(copy-gzip6 bench/capi/copy_gzip6.c)
target_include_directories(copy-gzip6 PRIVATE ${HDF5_C_INCLUDE_DIRS})
target_link_libraries(copy-gzip6 PRIVATE ${HDF5_LIBRARIES})
set_target_properties(copy-gzip6 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# ─── Wrapper Throughput Benchmarks ──────────────────────────────────────────────────
# Each does the same job: read IEX tick data, write with gzip-6, report Mticks/s.

# HighFive
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/highfive/include)
    add_benchmark(copy-gzip6-highfive bench/highfive/copy_gzip6.cpp)
endif()

# ESS-DMSC h5cpp
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/dmsc-h5cpp/include)
    add_benchmark(copy-gzip6-dmsc-h5cpp bench/dmsc-h5cpp/copy_gzip6.cpp)
endif()

# h5pp
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/h5pp/include)
    add_benchmark(copy-gzip6-h5pp bench/h5pp/copy_gzip6.cpp)
endif()

# vargalabs h5cpp (threaded packet-table pipeline)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/vargalabs/include/h5cpp)
    set(H5CPP_COMPILER "/home/steven/projects/h5cpp-compiler/build/h5cpp")
    set(TICK_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bench/h5cpp)
    set(TICK_GEN_H   ${TICK_GEN_DIR}/generated.h)

    add_custom_command(
        OUTPUT  ${TICK_GEN_H}
        COMMAND ${H5CPP_COMPILER}
                -o ${TICK_GEN_H}
                --extra-arg=-std=c++20
                --extra-arg=-I${CMAKE_CURRENT_SOURCE_DIR}/wrappers/vargalabs/include
                --extra-arg=-I${HDF5_ROOT}/include
                ${TICK_GEN_DIR}/copy_gzip6.cpp
        DEPENDS ${TICK_GEN_DIR}/copy_gzip6.cpp ${TICK_GEN_DIR}/tick.h
        COMMENT "Generating h5cpp type descriptor for tick_t"
        VERBATIM
    )
    add_custom_target(copy-gzip6-h5cpp-generated DEPENDS ${TICK_GEN_H})

    add_benchmark(copy-gzip6-h5cpp bench/h5cpp/copy_gzip6.cpp
        EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/vargalabs/include)
    add_dependencies(copy-gzip6-h5cpp copy-gzip6-h5cpp-generated)
    target_compile_definitions(copy-gzip6-h5cpp PRIVATE H5CPP_HAS_LIBDEFLATE=1)
    target_link_libraries(copy-gzip6-h5cpp PRIVATE deflate)
endif()

# ─── Micro-benchmark Targets (nanobench) ────────────────────────────────────────────
# Raw HDF5 C API baseline
add_benchmark(bench-hdf5-c src/baseline_c.cpp)

# C++ wrappers (enabled when their headers are present)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/highfive/include)
    add_benchmark(bench-highfive src/highfive.cpp)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/dmsc-h5cpp/include)
    add_benchmark(bench-dmsc-h5cpp src/dmsc_h5cpp.cpp)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/h5pp/include)
    add_benchmark(bench-h5pp src/h5pp.cpp)
endif()

# h5cpp (added manually by Steven under vargalabs/)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/vargalabs/include/h5cpp)
    add_benchmark(bench-h5cpp src/h5cpp.cpp
        EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/wrappers/vargalabs/include)
endif()

# ─── Aggregate Target ─────────────────────────────────────────────────────────────
add_custom_target(bench-all DEPENDS
    bench-hdf5-c
    $<TARGET_NAME_IF_EXISTS:bench-highfive>
    $<TARGET_NAME_IF_EXISTS:bench-dmsc-h5cpp>
    $<TARGET_NAME_IF_EXISTS:bench-h5pp>
    $<TARGET_NAME_IF_EXISTS:bench-h5cpp>
)
