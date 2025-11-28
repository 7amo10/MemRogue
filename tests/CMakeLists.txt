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

# Integration test for interception layer
add_test(NAME InterceptionTest 
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_interception.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
