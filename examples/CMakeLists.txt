# =============================================================================
# MemRogue Example Applications
# =============================================================================
# This file builds the example applications demonstrating MemRogue usage.
# Each example can be run with MemRogue using LD_PRELOAD.
# =============================================================================

# Find pthreads for multithreaded examples
find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# Simple Example (existing)
# -----------------------------------------------------------------------------
add_executable(memrogue_example simple_app.c)
add_dependencies(memrogue_example memrogue_intercept)

add_test(
	NAME MemRogueInterceptionExample
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:memrogue_example>
)

# -----------------------------------------------------------------------------
# Basic Leak Example
# Demonstrates various memory leak patterns for educational purposes
# -----------------------------------------------------------------------------
add_executable(example_basic_leak basic_leak.c)
add_dependencies(example_basic_leak memrogue_intercept)

add_test(
	NAME ExampleBasicLeak
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_basic_leak>
)

# -----------------------------------------------------------------------------
# Multithreaded Example
# Demonstrates concurrent memory allocations with proper thread safety
# -----------------------------------------------------------------------------
add_executable(example_multithreaded multithreaded.c)
target_link_libraries(example_multithreaded PRIVATE Threads::Threads)
add_dependencies(example_multithreaded memrogue_intercept)

add_test(
	NAME ExampleMultithreaded
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_multithreaded>
)

# -----------------------------------------------------------------------------
# Web Server Simulation Example
# Demonstrates realistic server scenario with connection pools and sessions
# -----------------------------------------------------------------------------
add_executable(example_webserver_sim webserver_sim.c)
target_link_libraries(example_webserver_sim PRIVATE Threads::Threads)
add_dependencies(example_webserver_sim memrogue_intercept)

add_test(
	NAME ExampleWebserverSim
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_webserver_sim>
)

# -----------------------------------------------------------------------------
# Custom Allocator Example
# Demonstrates tracking custom allocators (pool, arena, slab)
# -----------------------------------------------------------------------------
add_executable(example_custom_allocator custom_allocator.c)
add_dependencies(example_custom_allocator memrogue_intercept)

add_test(
	NAME ExampleCustomAllocator
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_custom_allocator>
)

# -----------------------------------------------------------------------------
# Custom target to run all examples
# -----------------------------------------------------------------------------
add_custom_target(run_examples
	COMMAND ${CMAKE_COMMAND} -E echo "Running all MemRogue examples..."
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "=== Basic Leak Example ==="
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_basic_leak> || true
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "=== Multithreaded Example ==="
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_multithreaded> || true
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "=== Web Server Simulation ==="
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_webserver_sim> || true
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "=== Custom Allocator Example ==="
	COMMAND ${CMAKE_COMMAND} -E env LD_PRELOAD=$<TARGET_FILE:memrogue_intercept> $<TARGET_FILE:example_custom_allocator> || true
	COMMAND ${CMAKE_COMMAND} -E echo ""
	COMMAND ${CMAKE_COMMAND} -E echo "All examples completed!"
	DEPENDS example_basic_leak example_multithreaded example_webserver_sim example_custom_allocator memrogue_intercept
	COMMENT "Running all MemRogue example applications"
)
