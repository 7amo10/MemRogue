/**
 * @file integration_common.h
 * @brief Common utilities for MemRogue integration tests
 * 
 * MEMRO-25: Integration Test Suite
 * 
 * This header provides common macros, utilities, and test framework
 * infrastructure for all integration tests.
 */

#ifndef INTEGRATION_COMMON_H
#define INTEGRATION_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

// ============================================================================
// Test Result Codes
// ============================================================================

#define TEST_PASS        0
#define TEST_FAIL        1
#define TEST_SKIP        2
#define TEST_ERROR       3

// ============================================================================
// Color Output (if terminal supports it)
// ============================================================================

#ifdef NO_COLOR
    #define COLOR_RESET   ""
    #define COLOR_RED     ""
    #define COLOR_GREEN   ""
    #define COLOR_YELLOW  ""
    #define COLOR_BLUE    ""
    #define COLOR_CYAN    ""
#else
    #define COLOR_RESET   "\033[0m"
    #define COLOR_RED     "\033[0;31m"
    #define COLOR_GREEN   "\033[0;32m"
    #define COLOR_YELLOW  "\033[0;33m"
    #define COLOR_BLUE    "\033[0;34m"
    #define COLOR_CYAN    "\033[0;36m"
#endif

// ============================================================================
// Test Macros
// ============================================================================

/**
 * Assert that a condition is true, with message on failure.
 */
#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that a condition is false.
 */
#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

/**
 * Assert that two integers are equal.
 */
#define ASSERT_EQ(expected, actual, msg) do { \
    long long _exp = (long long)(expected); \
    long long _act = (long long)(actual); \
    if (_exp != _act) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s (expected %lld, got %lld)\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg, _exp, _act); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that two integers are not equal.
 */
#define ASSERT_NE(not_expected, actual, msg) do { \
    long long _nexp = (long long)(not_expected); \
    long long _act = (long long)(actual); \
    if (_nexp == _act) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s (got unexpected value %lld)\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg, _act); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that a pointer is not NULL.
 */
#define ASSERT_NOT_NULL(ptr, msg) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s (got NULL)\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that a pointer is NULL.
 */
#define ASSERT_NULL(ptr, msg) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s (expected NULL)\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that two strings are equal.
 */
#define ASSERT_STR_EQ(expected, actual, msg) do { \
    const char* _exp = (expected); \
    const char* _act = (actual); \
    if (_exp == NULL && _act == NULL) break; \
    if (_exp == NULL || _act == NULL || strcmp(_exp, _act) != 0) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s\n  expected: \"%s\"\n  actual:   \"%s\"\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg, \
                _exp ? _exp : "(null)", _act ? _act : "(null)"); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that a string contains a substring.
 */
#define ASSERT_STR_CONTAINS(haystack, needle, msg) do { \
    const char* _h = (haystack); \
    const char* _n = (needle); \
    if (_h == NULL || _n == NULL || strstr(_h, _n) == NULL) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s\n  string: \"%s\"\n  should contain: \"%s\"\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg, \
                _h ? _h : "(null)", _n ? _n : "(null)"); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that a value is greater than another.
 */
#define ASSERT_GT(actual, expected, msg) do { \
    long long _act = (long long)(actual); \
    long long _exp = (long long)(expected); \
    if (_act <= _exp) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s (expected > %lld, got %lld)\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg, _exp, _act); \
        return TEST_FAIL; \
    } \
} while(0)

/**
 * Assert that a value is greater than or equal to another.
 */
#define ASSERT_GE(actual, expected, msg) do { \
    long long _act = (long long)(actual); \
    long long _exp = (long long)(expected); \
    if (_act < _exp) { \
        fprintf(stderr, "%s[ASSERT FAILED]%s %s:%d: %s (expected >= %lld, got %lld)\n", \
                COLOR_RED, COLOR_RESET, __FILE__, __LINE__, msg, _exp, _act); \
        return TEST_FAIL; \
    } \
} while(0)

// ============================================================================
// Test Runner Infrastructure
// ============================================================================

/**
 * Test function type.
 */
typedef int (*test_func_t)(void);

/**
 * Test case structure.
 */
typedef struct {
    const char* name;
    const char* description;
    test_func_t func;
} test_case_t;

/**
 * Test suite structure.
 */
typedef struct {
    const char* name;
    test_case_t* tests;
    size_t test_count;
    size_t passed;
    size_t failed;
    size_t skipped;
} test_suite_t;

/**
 * Run a single test case.
 */
static inline int run_test(const test_case_t* test) {
    printf("  %s%-50s%s ", COLOR_CYAN, test->name, COLOR_RESET);
    fflush(stdout);
    
    int result = test->func();
    
    switch (result) {
        case TEST_PASS:
            printf("%s[PASS]%s\n", COLOR_GREEN, COLOR_RESET);
            break;
        case TEST_FAIL:
            printf("%s[FAIL]%s\n", COLOR_RED, COLOR_RESET);
            break;
        case TEST_SKIP:
            printf("%s[SKIP]%s\n", COLOR_YELLOW, COLOR_RESET);
            break;
        default:
            printf("%s[ERROR]%s\n", COLOR_RED, COLOR_RESET);
            break;
    }
    
    return result;
}

/**
 * Run all tests in a suite.
 */
static inline int run_test_suite(test_suite_t* suite) {
    printf("\n%s=== %s ===%s\n\n", COLOR_BLUE, suite->name, COLOR_RESET);
    
    suite->passed = 0;
    suite->failed = 0;
    suite->skipped = 0;
    
    for (size_t i = 0; i < suite->test_count; i++) {
        int result = run_test(&suite->tests[i]);
        switch (result) {
            case TEST_PASS:
                suite->passed++;
                break;
            case TEST_FAIL:
            case TEST_ERROR:
                suite->failed++;
                break;
            case TEST_SKIP:
                suite->skipped++;
                break;
        }
    }
    
    printf("\n%sResults:%s %zu passed, %zu failed, %zu skipped (total: %zu)\n",
           COLOR_BLUE, COLOR_RESET,
           suite->passed, suite->failed, suite->skipped, suite->test_count);
    
    return (suite->failed == 0) ? TEST_PASS : TEST_FAIL;
}

// ============================================================================
// Memory Helpers
// ============================================================================

/**
 * Allocate memory and intentionally leak it.
 * Returns the allocated pointer (which should NOT be freed for leak testing).
 */
static inline void* leak_memory(size_t size) {
    void* ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0xAB, size);  // Fill with pattern to detect in analysis
    }
    return ptr;
}

/**
 * Allocate an array of memory blocks and leak them all.
 * Returns the number of successful allocations.
 */
static inline size_t leak_multiple(size_t count, size_t size) {
    size_t leaked = 0;
    for (size_t i = 0; i < count; i++) {
        if (leak_memory(size) != NULL) {
            leaked++;
        }
    }
    return leaked;
}

/**
 * Create a linked list of specified length (all nodes leaked).
 */
typedef struct leak_node {
    int value;
    struct leak_node* next;
} leak_node_t;

static inline leak_node_t* create_leaked_list(size_t length) {
    if (length == 0) return NULL;
    
    leak_node_t* head = malloc(sizeof(leak_node_t));
    if (!head) return NULL;
    
    head->value = 0;
    head->next = NULL;
    
    leak_node_t* current = head;
    for (size_t i = 1; i < length; i++) {
        current->next = malloc(sizeof(leak_node_t));
        if (!current->next) break;
        current = current->next;
        current->value = (int)i;
        current->next = NULL;
    }
    
    return head;  // Intentionally leaked
}

// ============================================================================
// File Utilities
// ============================================================================

/**
 * Read entire file into a newly allocated buffer.
 * Caller must free the returned buffer.
 */
static inline char* read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    
    char* buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    
    size_t read_size = fread(buffer, 1, (size_t)size, f);
    buffer[read_size] = '\0';
    
    fclose(f);
    return buffer;
}

/**
 * Count occurrences of a substring in a string.
 */
static inline size_t count_occurrences(const char* str, const char* substr) {
    if (!str || !substr || *substr == '\0') return 0;
    
    size_t count = 0;
    size_t substr_len = strlen(substr);
    const char* pos = str;
    
    while ((pos = strstr(pos, substr)) != NULL) {
        count++;
        pos += substr_len;
    }
    
    return count;
}

/**
 * Create a temporary file with given content.
 * Returns the file path (caller must free).
 */
static inline char* create_temp_file(const char* content) {
    char template[] = "/tmp/memrogue_test_XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) return NULL;
    
    if (content) {
        size_t len = strlen(content);
        ssize_t written = write(fd, content, len);
        if (written < 0 || (size_t)written != len) {
            close(fd);
            unlink(template);
            return NULL;
        }
    }
    
    close(fd);
    return strdup(template);
}

// ============================================================================
// Process Utilities
// ============================================================================

/**
 * Run a command and capture its output.
 * Returns the exit status, output is stored in out_buffer (up to max_size).
 */
static inline int run_command(const char* cmd, char* out_buffer, size_t max_size) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return -1;
    
    if (out_buffer && max_size > 0) {
        out_buffer[0] = '\0';
        size_t total = 0;
        char line[256];
        
        while (fgets(line, sizeof(line), pipe) != NULL) {
            size_t len = strlen(line);
            if (total + len < max_size - 1) {
                memcpy(out_buffer + total, line, len);
                total += len;
                out_buffer[total] = '\0';
            }
        }
    }
    
    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

// ============================================================================
// Time Utilities
// ============================================================================

/**
 * Get current time in milliseconds.
 */
static inline uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/**
 * Sleep for specified milliseconds.
 */
static inline void sleep_ms(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000);
    nanosleep(&ts, NULL);
}

#endif // INTEGRATION_COMMON_H
