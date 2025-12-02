# ============================================================================
# MemRogue Performance Benchmarks
# MEMRO-26: Performance Benchmarks
# ============================================================================

cmake_minimum_required(VERSION 3.10)

# Benchmark executable names
set(BENCH_LATENCY "bench_latency")
set(BENCH_THROUGHPUT "bench_throughput")
set(BENCH_REALWORLD "bench_realworld")

# Option to enable native architecture optimizations
# Note: -march=native makes binaries non-portable. Disable for CI/CD or cross-system use.
option(MEMROGUE_BENCH_NATIVE_ARCH "Use -march=native for benchmarks (non-portable)" ON)

# Compiler flags for benchmarks - optimize for performance
set(BENCH_COMPILE_FLAGS
    -O2
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
)

# Add native architecture flag if enabled
if(MEMROGUE_BENCH_NATIVE_ARCH)
    list(APPEND BENCH_COMPILE_FLAGS -march=native)
endif()

# ============================================================================
# Latency Benchmarks
# ============================================================================

add_executable(${BENCH_LATENCY}
    benchmark_latency.c
)

target_include_directories(${BENCH_LATENCY} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${BENCH_LATENCY} PRIVATE
    memrogue_core
    m       # Math library for sqrt
    pthread # For any threading utilities
)

target_compile_options(${BENCH_LATENCY} PRIVATE ${BENCH_COMPILE_FLAGS})

# ============================================================================
# Throughput Benchmarks
# ============================================================================

add_executable(${BENCH_THROUGHPUT}
    benchmark_throughput.c
)

target_include_directories(${BENCH_THROUGHPUT} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${BENCH_THROUGHPUT} PRIVATE
    memrogue_core
    m
    pthread
)

target_compile_options(${BENCH_THROUGHPUT} PRIVATE ${BENCH_COMPILE_FLAGS})

# ============================================================================
# Real-World Scenario Benchmarks
# ============================================================================

add_executable(${BENCH_REALWORLD}
    benchmark_realworld.c
)

target_include_directories(${BENCH_REALWORLD} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${BENCH_REALWORLD} PRIVATE
    memrogue_core
    m
    pthread
)

target_compile_options(${BENCH_REALWORLD} PRIVATE ${BENCH_COMPILE_FLAGS})

# ============================================================================
# Output Directory
# ============================================================================

set_target_properties(${BENCH_LATENCY} ${BENCH_THROUGHPUT} ${BENCH_REALWORLD}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ============================================================================
# Custom Target to Run All Benchmarks
# ============================================================================

add_custom_target(benchmarks
    COMMAND echo "============================================"
    COMMAND echo "Running MemRogue Performance Benchmarks"
    COMMAND echo "============================================"
    COMMAND echo ""
    COMMAND ${CMAKE_BINARY_DIR}/bin/${BENCH_LATENCY}
    COMMAND echo ""
    COMMAND ${CMAKE_BINARY_DIR}/bin/${BENCH_THROUGHPUT}
    COMMAND echo ""
    COMMAND ${CMAKE_BINARY_DIR}/bin/${BENCH_REALWORLD}
    DEPENDS ${BENCH_LATENCY} ${BENCH_THROUGHPUT} ${BENCH_REALWORLD}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all performance benchmarks"
)

# ============================================================================
# Custom Target to Generate CSV Report
# ============================================================================

add_custom_target(benchmark-report
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/benchmark-results
    COMMAND ${CMAKE_BINARY_DIR}/bin/${BENCH_LATENCY} --csv ${CMAKE_BINARY_DIR}/benchmark-results/latency.csv
    COMMAND ${CMAKE_BINARY_DIR}/bin/${BENCH_THROUGHPUT} --csv ${CMAKE_BINARY_DIR}/benchmark-results/throughput.csv
    COMMAND ${CMAKE_BINARY_DIR}/bin/${BENCH_REALWORLD} --csv ${CMAKE_BINARY_DIR}/benchmark-results/realworld.csv
    COMMAND echo "Benchmark CSV reports generated in: ${CMAKE_BINARY_DIR}/benchmark-results/"
    DEPENDS ${BENCH_LATENCY} ${BENCH_THROUGHPUT} ${BENCH_REALWORLD}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating benchmark CSV reports"
)
