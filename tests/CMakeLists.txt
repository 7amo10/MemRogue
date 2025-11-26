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

# Integration test for interception layer
add_test(NAME InterceptionTest 
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_interception.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
