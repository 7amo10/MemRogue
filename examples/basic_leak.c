/**
 * @file basic_leak.c
 * @brief Basic memory leak example for MemRogue demonstration
 * 
 * MEMRO-27: Example Applications
 * 
 * This example demonstrates various types of memory leaks that MemRogue
 * can detect. It showcases:
 * - Simple forgotten free
 * - Overwritten pointer (lost reference)
 * - Conditional leak (error path)
 * - Accumulated leaks in loop
 * 
 * Usage:
 *   # Build with CMake
 *   cd build && make basic_leak
 *   
 *   # Run with MemRogue interception
 *   LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/basic_leak
 *   
 *   # Or use the memrogue-report tool
 *   ./bin/memrogue-report --output leak_report.json -- ./bin/basic_leak
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * Leak Type 1: Simple Forgotten Free
 * ============================================================================
 * The most common type of memory leak - allocating memory and simply
 * forgetting to free it before the function returns.
 */
static void demonstrate_forgotten_free(void) {
    printf("\n[Leak 1] Demonstrating forgotten free...\n");
    
    char* buffer = malloc(64);
    if (!buffer) {
        fprintf(stderr, "  malloc failed\n");
        return;
    }
    
    strcpy(buffer, "This memory will be leaked!");
    printf("  Allocated 64 bytes at %p\n", (void*)buffer);
    printf("  Content: \"%s\"\n", buffer);
    
    /* BUG: Forgot to free(buffer) - this is a memory leak! */
    printf("  [!] Returning without freeing - LEAK!\n");
}

/* ============================================================================
 * Leak Type 2: Overwritten Pointer (Lost Reference)
 * ============================================================================
 * When a pointer to allocated memory is overwritten without first
 * freeing the original allocation, the reference is lost.
 */
static void demonstrate_overwritten_pointer(void) {
    printf("\n[Leak 2] Demonstrating overwritten pointer...\n");
    
    char* data = malloc(128);
    if (!data) {
        fprintf(stderr, "  malloc failed\n");
        return;
    }
    
    strcpy(data, "First allocation - will be lost");
    printf("  First allocation: 128 bytes at %p\n", (void*)data);
    
    /* BUG: Overwriting pointer without freeing first allocation */
    data = malloc(256);  /* Previous 128 bytes are now leaked! */
    if (!data) {
        fprintf(stderr, "  second malloc failed\n");
        return;
    }
    
    strcpy(data, "Second allocation - only this will be freed");
    printf("  Second allocation: 256 bytes at %p\n", (void*)data);
    printf("  [!] First allocation is now leaked!\n");
    
    free(data);  /* Only frees the second allocation */
    printf("  Freed second allocation\n");
}

/* ============================================================================
 * Leak Type 3: Conditional Leak (Error Path)
 * ============================================================================
 * Memory allocated before an error condition is often forgotten
 * when error handling doesn't clean up properly.
 */
static bool simulate_operation_that_fails(void) {
    /* Simulate an operation that might fail */
    return false;  /* Always fails for demonstration */
}

static void demonstrate_error_path_leak(void) {
    printf("\n[Leak 3] Demonstrating error path leak...\n");
    
    /* Allocate resources */
    int* numbers = malloc(100 * sizeof(int));
    if (!numbers) {
        fprintf(stderr, "  malloc failed\n");
        return;
    }
    printf("  Allocated array: %zu bytes at %p\n", 
           100 * sizeof(int), (void*)numbers);
    
    char* name = malloc(64);
    if (!name) {
        fprintf(stderr, "  second malloc failed\n");
        free(numbers);  /* Good: cleaning up on error */
        return;
    }
    printf("  Allocated name: 64 bytes at %p\n", (void*)name);
    
    /* Simulate an operation that fails */
    if (!simulate_operation_that_fails()) {
        printf("  Operation failed!\n");
        /* BUG: Only freeing 'numbers', forgot 'name' */
        free(numbers);
        printf("  [!] Forgot to free 'name' in error path - LEAK!\n");
        return;  /* 'name' is leaked! */
    }
    
    /* Normal cleanup (never reached in this example) */
    free(name);
    free(numbers);
}

/* ============================================================================
 * Leak Type 4: Accumulated Leaks in Loop
 * ============================================================================
 * When allocations inside a loop are not properly freed, 
 * leaks accumulate with each iteration.
 */
static void demonstrate_loop_leak(void) {
    printf("\n[Leak 4] Demonstrating accumulated leaks in loop...\n");
    
    const int iterations = 5;
    
    for (int i = 0; i < iterations; i++) {
        /* Allocate in each iteration */
        void* chunk = malloc(32);
        if (!chunk) {
            fprintf(stderr, "  malloc failed at iteration %d\n", i);
            continue;
        }
        
        printf("  Iteration %d: allocated 32 bytes at %p\n", i, chunk);
        
        /* BUG: Not freeing 'chunk' - leaks accumulate! */
    }
    
    printf("  [!] Leaked %d allocations (%d bytes total)\n", 
           iterations, iterations * 32);
}

/* ============================================================================
 * Correct Usage Example (No Leaks)
 * ============================================================================
 * For comparison, this function shows correct memory management.
 */
static void demonstrate_correct_usage(void) {
    printf("\n[OK] Demonstrating correct memory management...\n");
    
    /* Allocate */
    char* buffer = malloc(256);
    if (!buffer) {
        fprintf(stderr, "  malloc failed\n");
        return;
    }
    printf("  Allocated 256 bytes at %p\n", (void*)buffer);
    
    /* Use the memory */
    snprintf(buffer, 256, "This memory will be properly freed!");
    printf("  Content: \"%s\"\n", buffer);
    
    /* Free when done */
    free(buffer);
    printf("  Properly freed - no leak!\n");
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */
int main(void) {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║           MemRogue Basic Memory Leak Examples                    ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\nThis program intentionally creates memory leaks to demonstrate\n");
    printf("MemRogue's leak detection capabilities.\n");
    
    /* Run demonstrations */
    demonstrate_forgotten_free();      /* Leaks 64 bytes */
    demonstrate_overwritten_pointer(); /* Leaks 128 bytes */
    demonstrate_error_path_leak();     /* Leaks 64 bytes */
    demonstrate_loop_leak();           /* Leaks 160 bytes (5 × 32) */
    demonstrate_correct_usage();       /* No leaks */
    
    printf("\n════════════════════════════════════════════════════════════════════\n");
    printf("Summary of intentional leaks:\n");
    printf("  - Forgotten free:      64 bytes\n");
    printf("  - Overwritten pointer: 128 bytes\n");
    printf("  - Error path:          64 bytes\n");
    printf("  - Loop accumulation:   160 bytes (5 × 32)\n");
    printf("  ─────────────────────────────────\n");
    printf("  Total leaked:          416 bytes\n");
    printf("════════════════════════════════════════════════════════════════════\n");
    printf("\nRun with MemRogue to detect these leaks:\n");
    printf("  LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/basic_leak\n\n");
    
    return EXIT_SUCCESS;
}
