cmake_minimum_required(VERSION 3.15)
project(MemRogue VERSION 1.0.0 LANGUAGES C CXX)

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
option(MEMROGUE_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(MEMROGUE_ENABLE_COVERAGE "Enable code coverage reporting" OFF)

# Code Coverage Configuration
if(MEMROGUE_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Code coverage enabled")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()

# Output Directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Core library (data structures, utilities)
set(MEMROGUE_CORE_SOURCES
    src/memrogue_hash_table.c
    src/memrogue_allocation_record.c
    src/memrogue_backtrace.c
    src/memrogue_tracker.c
    src/memrogue_exit_handler.c
    src/memrogue_leak_detector.c
    src/memrogue_double_free.c
    src/memrogue_invalid_free.c
    src/memrogue_report.c
    src/memrogue_cli.c
    src/memrogue_config.c
    src/memrogue_json.c
    src/memrogue_csv.c
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

# CLI tool - standalone binary for report generation
add_executable(memrogue-report tools/memrogue_report.c)
target_link_libraries(memrogue-report PRIVATE memrogue_core)

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

# Benchmarks
if(MEMROGUE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Coverage Report Target
if(MEMROGUE_ENABLE_COVERAGE)
    find_program(LCOV lcov)
    find_program(GENHTML genhtml)
    
    if(LCOV AND GENHTML)
        add_custom_target(coverage
            COMMAND ${LCOV} --capture --directory . --output-file coverage.info --ignore-errors mismatch
            COMMAND ${LCOV} --remove coverage.info '/usr/*' --output-file coverage.info
            COMMAND ${GENHTML} coverage.info --output-directory coverage_report
            COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated in coverage_report/index.html"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report..."
        )
    else()
        message(WARNING "lcov/genhtml not found. Coverage reports will not be available.")
    endif()
endif()

# ============================================================================
# Installation
# ============================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Set version for pkg-config and cmake config
set(MEMROGUE_VERSION_MAJOR 1)
set(MEMROGUE_VERSION_MINOR 0)
set(MEMROGUE_VERSION_PATCH 0)
set(MEMROGUE_VERSION "${MEMROGUE_VERSION_MAJOR}.${MEMROGUE_VERSION_MINOR}.${MEMROGUE_VERSION_PATCH}")

# Set library version properties
set_target_properties(memrogue_intercept PROPERTIES
    VERSION ${MEMROGUE_VERSION}
    SOVERSION ${MEMROGUE_VERSION_MAJOR}
)

# Install targets
install(TARGETS memrogue_core memrogue_intercept memrogue-report
    EXPORT MemRogueTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT development
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT runtime
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install headers to memrogue subdirectory
install(DIRECTORY include/ 
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/memrogue
    COMPONENT development
    FILES_MATCHING PATTERN "*.h"
)

# Install the memrogue wrapper script
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/memrogue
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime
)

# Install documentation
install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT documentation
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    COMPONENT documentation
    FILES_MATCHING PATTERN "*.md"
)

# Install examples
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/memrogue/examples
    COMPONENT examples
    FILES_MATCHING 
    PATTERN "*.c"
    PATTERN "*.py"
    PATTERN "*.md"
    PATTERN "CMakeLists.txt" EXCLUDE
)

# ============================================================================
# pkg-config file generation
# ============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/memrogue.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/memrogue.pc"
    @ONLY
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/memrogue.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
    COMPONENT development
)

# ============================================================================
# CMake package config files
# ============================================================================
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/MemRogueConfigVersion.cmake"
    VERSION ${MEMROGUE_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MemRogueConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MemRogueConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MemRogue
)

install(EXPORT MemRogueTargets
    FILE MemRogueTargets.cmake
    NAMESPACE MemRogue::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MemRogue
    COMPONENT development
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/MemRogueConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/MemRogueConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MemRogue
    COMPONENT development
)

# ============================================================================
# CPack configuration for package generation
# ============================================================================
set(CPACK_PACKAGE_NAME "memrogue")
set(CPACK_PACKAGE_VENDOR "MemRogue Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A lightweight memory debugging tool for C/C++ applications")
set(CPACK_PACKAGE_DESCRIPTION "MemRogue is a powerful memory debugging tool that helps detect memory leaks, double frees, and invalid frees in C/C++ applications with minimal overhead.")
set(CPACK_PACKAGE_VERSION_MAJOR ${MEMROGUE_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${MEMROGUE_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${MEMROGUE_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${MEMROGUE_VERSION})
set(CPACK_PACKAGE_CONTACT "memrogue@example.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/7amo10/MemRogue")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Debian package settings
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "MemRogue Team <memrogue@example.com>")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# RPM package settings
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.17")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

# Archive settings (for tar.gz, zip)
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/.git/"
    "/.vscode/"
    "/.idea/"
    "\\\\.swp$"
    "\\\\.o$"
)

include(CPack)
