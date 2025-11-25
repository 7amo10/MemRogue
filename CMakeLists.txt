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

# Include Directories
include_directories(include)

# Source Files (Placeholder for now, will populate as we add files)
file(GLOB_RECURSE SOURCES "src/*.c" "src/*.cpp")

# Library Target
add_library(memrogue ${SOURCES})
target_include_directories(memrogue PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Alias for easier usage
add_library(MemRogue::memrogue ALIAS memrogue)

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
install(TARGETS memrogue
    EXPORT MemRogueTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
