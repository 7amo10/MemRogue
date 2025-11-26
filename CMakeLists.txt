cmake_minimum_required(VERSION 3.15)
project(MemRogue VERSION 0.1.0 LANGUAGES C CXX)

# Set C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler Flags
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

# Build Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(MEMROGUE_BUILD_TESTS "Build tests" ON)
option(MEMROGUE_BUILD_EXAMPLES "Build examples" ON)

# Output Directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Core library (data structures, utilities)
set(MEMROGUE_CORE_SOURCES
    src/memrogue_hash_table.c
)

add_library(memrogue_core ${MEMROGUE_CORE_SOURCES})
target_include_directories(memrogue_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(memrogue_core PUBLIC pthread)

# Interception layer is a shared object meant to be LD_PRELOAD'ed
add_library(memrogue_intercept SHARED src/memrogue_intercept.c)
target_include_directories(memrogue_intercept PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(memrogue_intercept PRIVATE memrogue_core dl pthread)

add_library(MemRogue::core ALIAS memrogue_core)
add_library(MemRogue::intercept ALIAS memrogue_intercept)

# Tests
if(MEMROGUE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(MEMROGUE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
include(GNUInstallDirs)
install(TARGETS memrogue_core memrogue_intercept
    EXPORT MemRogueTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
