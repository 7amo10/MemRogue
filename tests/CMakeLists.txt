add_executable(test_hash_table test_hash_table.c)
target_link_libraries(test_hash_table PRIVATE memrogue_core)
add_test(NAME HashTableTest COMMAND test_hash_table)

add_executable(test_multithreaded test_multithreaded.c)
target_link_libraries(test_multithreaded PRIVATE memrogue_core pthread)
add_test(NAME MultithreadedTest COMMAND test_multithreaded)

add_executable(test_allocation_record test_allocation_record.c)
target_link_libraries(test_allocation_record PRIVATE memrogue_core)
add_test(NAME AllocationRecordTest COMMAND test_allocation_record)

add_executable(test_backtrace test_backtrace.c)
target_link_libraries(test_backtrace PRIVATE memrogue_core)
target_compile_options(test_backtrace PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME BacktraceTest COMMAND test_backtrace)

add_executable(test_tracker test_tracker.c)
target_link_libraries(test_tracker PRIVATE memrogue_core)
target_compile_options(test_tracker PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME TrackerTest COMMAND test_tracker)

add_executable(test_comprehensive test_comprehensive.c)
target_link_libraries(test_comprehensive PRIVATE memrogue_core pthread)
target_compile_options(test_comprehensive PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME ComprehensiveTest COMMAND test_comprehensive)

add_executable(test_exit_handler test_exit_handler.c)
target_link_libraries(test_exit_handler PRIVATE memrogue_core pthread)
target_compile_options(test_exit_handler PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME ExitHandlerTest COMMAND test_exit_handler)

add_executable(test_leak_detector test_leak_detector.c)
target_link_libraries(test_leak_detector PRIVATE memrogue_core pthread)
target_compile_options(test_leak_detector PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME LeakDetectorTest COMMAND test_leak_detector)

add_executable(test_double_free test_double_free.c)
target_link_libraries(test_double_free PRIVATE memrogue_core pthread)
target_compile_options(test_double_free PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME DoubleFreeTest COMMAND test_double_free)

add_executable(test_invalid_free test_invalid_free.c)
target_link_libraries(test_invalid_free PRIVATE memrogue_core pthread)
target_compile_options(test_invalid_free PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME InvalidFreeTest COMMAND test_invalid_free)

add_executable(test_report test_report.c)
target_link_libraries(test_report PRIVATE memrogue_core pthread)
target_compile_options(test_report PRIVATE -rdynamic)  # Enable symbol names in backtrace
add_test(NAME ReportTest COMMAND test_report)

add_executable(test_leak_structures test_leak_structures.c)
target_link_libraries(test_leak_structures PRIVATE memrogue_core)
add_test(NAME LeakStructuresTest COMMAND test_leak_structures)

add_executable(test_cli test_cli.c)
target_link_libraries(test_cli PRIVATE memrogue_core)
add_test(NAME CLITest COMMAND test_cli)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE memrogue_core pthread)
add_test(NAME ConfigTest COMMAND test_config)

add_executable(test_sampling test_sampling.c)
target_link_libraries(test_sampling PRIVATE memrogue_core pthread)
add_test(NAME SamplingTest COMMAND test_sampling)

add_executable(test_json test_json.c)
target_link_libraries(test_json PRIVATE memrogue_core)
add_test(NAME JSONTest COMMAND test_json)

add_executable(test_csv test_csv.c)
target_link_libraries(test_csv PRIVATE memrogue_core)
add_test(NAME CSVTest COMMAND test_csv)

# Integration test for interception layer
add_test(NAME InterceptionTest 
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_interception.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
set_tests_properties(InterceptionTest PROPERTIES
    ENVIRONMENT "BUILD_DIR=${CMAKE_BINARY_DIR}"
)

# ============================================================================
# Integration Tests (MEMRO-25)
# ============================================================================

# Integration test common header
set(INTEGRATION_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/integration)

# Integration test: Simple leak patterns
add_executable(integ_simple_leaks 
    integration/test_simple_leaks.c
)
target_include_directories(integ_simple_leaks PRIVATE ${INTEGRATION_INCLUDE_DIR})
target_link_libraries(integ_simple_leaks PRIVATE memrogue_core pthread)
target_compile_options(integ_simple_leaks PRIVATE -rdynamic)
add_test(NAME IntegrationSimpleLeaks COMMAND integ_simple_leaks)

# Integration test: Complex allocation patterns
add_executable(integ_complex_patterns 
    integration/test_complex_patterns.c
)
target_include_directories(integ_complex_patterns PRIVATE ${INTEGRATION_INCLUDE_DIR})
target_link_libraries(integ_complex_patterns PRIVATE memrogue_core pthread)
target_compile_options(integ_complex_patterns PRIVATE -rdynamic)
add_test(NAME IntegrationComplexPatterns COMMAND integ_complex_patterns)

# Integration test: Multithreaded scenarios
add_executable(integ_multithreaded 
    integration/test_multithreaded_integration.c
)
target_include_directories(integ_multithreaded PRIVATE ${INTEGRATION_INCLUDE_DIR})
target_link_libraries(integ_multithreaded PRIVATE memrogue_core pthread)
target_compile_options(integ_multithreaded PRIVATE -rdynamic)
add_test(NAME IntegrationMultithreaded COMMAND integ_multithreaded)

# Integration test: Error conditions
add_executable(integ_error_conditions 
    integration/test_error_conditions.c
)
target_include_directories(integ_error_conditions PRIVATE ${INTEGRATION_INCLUDE_DIR})
target_link_libraries(integ_error_conditions PRIVATE memrogue_core pthread)
target_compile_options(integ_error_conditions PRIVATE -rdynamic)
add_test(NAME IntegrationErrorConditions COMMAND integ_error_conditions)

# Integration test: Report formats
add_executable(integ_report_formats 
    integration/test_report_formats.c
)
target_include_directories(integ_report_formats PRIVATE ${INTEGRATION_INCLUDE_DIR})
target_link_libraries(integ_report_formats PRIVATE memrogue_core pthread)
target_compile_options(integ_report_formats PRIVATE -rdynamic)
add_test(NAME IntegrationReportFormats COMMAND integ_report_formats)

# ============================================================================
# Integration Test Runner
# ============================================================================

add_custom_target(integration_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_integration_tests.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS 
        integ_simple_leaks 
        integ_complex_patterns 
        integ_multithreaded 
        integ_error_conditions 
        integ_report_formats
    COMMENT "Running integration tests"
)

# Group all integration tests under 'IntegrationTests' label
set_tests_properties(
    IntegrationSimpleLeaks
    IntegrationComplexPatterns
    IntegrationMultithreaded
    IntegrationErrorConditions
    IntegrationReportFormats
    PROPERTIES LABELS "integration"
)
