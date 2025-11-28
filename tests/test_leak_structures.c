/**
 * @file test_leak_structures.c
 * @brief Unit tests for leak report structure lifecycle.
 *
 * Tests cover:
 * - Leak entry creation, cloning, destruction
 * - Leak group creation, cloning, destruction
 * - Leak report creation, cloning, destruction
 * - Add/remove operations
 * - Statistics recalculation
 * - Report merging
 * - Edge cases and NULL handling
 * - Memory management correctness
 *
 * MEMRO-18: Leak Report Structure
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../include/memrogue_leak_detector.h"

/* ============================================================================
 * Test Infrastructure
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("Running %s... ", #test_func); \
    fflush(stdout); \
    if (test_func()) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } else { \
        printf("FAILED\n"); \
    } \
} while(0)

/* ============================================================================
 * Leak Entry Tests
 * ============================================================================ */

/**
 * Test creating a leak entry.
 */
static int test_entry_create(void) {
    void* addr = (void*)0x12345678;
    leak_entry_t* entry = leak_entry_create(addr, 1024, "test.c", 42, false);
    
    if (entry == NULL) {
        fprintf(stderr, "FAIL: %s - entry is NULL\n", __func__);
        return 0;
    }
    
    if (entry->address != addr) {
        fprintf(stderr, "FAIL: %s - wrong address\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    if (entry->size != 1024) {
        fprintf(stderr, "FAIL: %s - wrong size\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    if (strcmp(entry->file, "test.c") != 0) {
        fprintf(stderr, "FAIL: %s - wrong file\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    if (entry->line != 42) {
        fprintf(stderr, "FAIL: %s - wrong line\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    if (entry->next != NULL) {
        fprintf(stderr, "FAIL: %s - next not NULL\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    leak_entry_destroy(entry);
    return 1;
}

/**
 * Test creating entry with string copy.
 */
static int test_entry_create_copy_strings(void) {
    char filename[] = "dynamic.c";
    leak_entry_t* entry = leak_entry_create((void*)0x1000, 64, filename, 10, true);
    
    if (entry == NULL) {
        fprintf(stderr, "FAIL: %s - entry is NULL\n", __func__);
        return 0;
    }
    
    /* Modify original - should not affect entry if copied */
    filename[0] = 'X';
    
    if (strcmp(entry->file, "dynamic.c") != 0) {
        fprintf(stderr, "FAIL: %s - string not copied properly\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    /* When copy_strings=true, the entry owns the string and leak_entry_destroy frees it */
    leak_entry_destroy(entry);
    return 1;
}

/**
 * Test creating entry with NULL file.
 */
static int test_entry_create_null_file(void) {
    leak_entry_t* entry = leak_entry_create((void*)0x2000, 128, NULL, 0, false);
    
    if (entry == NULL) {
        fprintf(stderr, "FAIL: %s - entry is NULL\n", __func__);
        return 0;
    }
    
    if (entry->file != NULL) {
        fprintf(stderr, "FAIL: %s - file should be NULL\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    leak_entry_destroy(entry);
    return 1;
}

/**
 * Test cloning a leak entry.
 */
static int test_entry_clone(void) {
    void* frames[3] = {(void*)0x100, (void*)0x200, (void*)0x300};
    
    leak_entry_t* original = leak_entry_create((void*)0x5000, 512, "clone.c", 100, false);
    if (!original) return 0;
    
    original->timestamp = 123456789ULL;
    leak_entry_set_backtrace(original, frames, 3);
    
    leak_entry_t* clone = leak_entry_clone(original);
    if (clone == NULL) {
        fprintf(stderr, "FAIL: %s - clone is NULL\n", __func__);
        leak_entry_destroy(original);
        return 0;
    }
    
    /* Verify clone has same values */
    if (clone->address != original->address ||
        clone->size != original->size ||
        clone->line != original->line ||
        clone->timestamp != original->timestamp ||
        clone->frame_count != original->frame_count) {
        fprintf(stderr, "FAIL: %s - clone values differ\n", __func__);
        leak_entry_destroy(original);
        leak_entry_destroy(clone);  /* destroy frees owned string */
        return 0;
    }
    
    /* Verify string was copied (different pointer) */
    if (clone->file == original->file) {
        fprintf(stderr, "FAIL: %s - string not copied (same pointer)\n", __func__);
        leak_entry_destroy(original);
        leak_entry_destroy(clone);
        return 0;
    }
    
    /* Verify backtrace copied */
    for (int i = 0; i < 3; i++) {
        if (clone->frames[i] != original->frames[i]) {
            fprintf(stderr, "FAIL: %s - backtrace not copied\n", __func__);
            leak_entry_destroy(original);
            leak_entry_destroy(clone);  /* destroy frees owned string */
            return 0;
        }
    }
    
    /* Clone should not be linked */
    if (clone->next != NULL) {
        fprintf(stderr, "FAIL: %s - clone next not NULL\n", __func__);
        leak_entry_destroy(original);
        leak_entry_destroy(clone);  /* destroy frees owned string */
        return 0;
    }
    
    leak_entry_destroy(original);
    leak_entry_destroy(clone);  /* destroy frees owned string */
    return 1;
}

/**
 * Test cloning NULL entry.
 */
static int test_entry_clone_null(void) {
    leak_entry_t* clone = leak_entry_clone(NULL);
    if (clone != NULL) {
        fprintf(stderr, "FAIL: %s - clone of NULL should be NULL\n", __func__);
        leak_entry_destroy(clone);
        return 0;
    }
    return 1;
}

/**
 * Test setting backtrace on entry.
 */
static int test_entry_set_backtrace(void) {
    leak_entry_t* entry = leak_entry_create((void*)0x1000, 64, NULL, 0, false);
    if (!entry) return 0;
    
    void* frames[5] = {(void*)0x10, (void*)0x20, (void*)0x30, (void*)0x40, (void*)0x50};
    leak_entry_set_backtrace(entry, frames, 5);
    
    if (entry->frame_count != 5) {
        fprintf(stderr, "FAIL: %s - wrong frame count\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    for (int i = 0; i < 5; i++) {
        if (entry->frames[i] != frames[i]) {
            fprintf(stderr, "FAIL: %s - frame %d mismatch\n", __func__, i);
            leak_entry_destroy(entry);
            return 0;
        }
    }
    
    /* Clear backtrace */
    leak_entry_set_backtrace(entry, NULL, 0);
    if (entry->frame_count != 0) {
        fprintf(stderr, "FAIL: %s - frame count not cleared\n", __func__);
        leak_entry_destroy(entry);
        return 0;
    }
    
    leak_entry_destroy(entry);
    return 1;
}

/**
 * Test entry chain count.
 */
static int test_entry_chain_count(void) {
    leak_entry_t* e1 = leak_entry_create((void*)0x1, 10, NULL, 0, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x2, 20, NULL, 0, false);
    leak_entry_t* e3 = leak_entry_create((void*)0x3, 30, NULL, 0, false);
    
    if (!e1 || !e2 || !e3) {
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        leak_entry_destroy(e3);
        return 0;
    }
    
    e1->next = e2;
    e2->next = e3;
    
    if (leak_entry_chain_count(e1) != 3) {
        fprintf(stderr, "FAIL: %s - wrong count for 3 entries\n", __func__);
        leak_entry_destroy_chain(e1);
        return 0;
    }
    
    if (leak_entry_chain_count(e2) != 2) {
        fprintf(stderr, "FAIL: %s - wrong count for 2 entries\n", __func__);
        leak_entry_destroy_chain(e1);
        return 0;
    }
    
    if (leak_entry_chain_count(NULL) != 0) {
        fprintf(stderr, "FAIL: %s - NULL chain count should be 0\n", __func__);
        leak_entry_destroy_chain(e1);
        return 0;
    }
    
    leak_entry_destroy_chain(e1);
    return 1;
}

/**
 * Test destroying NULL entry.
 */
static int test_entry_destroy_null(void) {
    /* Should not crash */
    leak_entry_destroy(NULL);
    leak_entry_destroy_chain(NULL);
    return 1;
}

/* ============================================================================
 * Leak Group Tests
 * ============================================================================ */

/**
 * Test creating a leak group.
 */
static int test_group_create(void) {
    leak_group_t* group = leak_group_create("group.c", 55, false);
    
    if (group == NULL) {
        fprintf(stderr, "FAIL: %s - group is NULL\n", __func__);
        return 0;
    }
    
    if (strcmp(group->file, "group.c") != 0) {
        fprintf(stderr, "FAIL: %s - wrong file\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->line != 55) {
        fprintf(stderr, "FAIL: %s - wrong line\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->leak_count != 0 || group->total_bytes != 0) {
        fprintf(stderr, "FAIL: %s - stats not zero\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->entries != NULL || group->next != NULL) {
        fprintf(stderr, "FAIL: %s - pointers not NULL\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_group_destroy(group);
    return 1;
}

/**
 * Test adding entries to a group.
 */
static int test_group_add_entry(void) {
    leak_group_t* group = leak_group_create("add.c", 10, false);
    if (!group) return 0;
    
    leak_entry_t* e1 = leak_entry_create((void*)0x1000, 100, "add.c", 10, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x2000, 200, "add.c", 10, false);
    
    if (!e1 || !e2) {
        leak_group_destroy(group);
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        return 0;
    }
    
    if (!leak_group_add_entry(group, e1)) {
        fprintf(stderr, "FAIL: %s - add e1 failed\n", __func__);
        leak_group_destroy(group);
        leak_entry_destroy(e2);
        return 0;
    }
    
    if (group->leak_count != 1 || group->total_bytes != 100) {
        fprintf(stderr, "FAIL: %s - stats wrong after add e1\n", __func__);
        leak_group_destroy(group);
        leak_entry_destroy(e2);
        return 0;
    }
    
    if (!leak_group_add_entry(group, e2)) {
        fprintf(stderr, "FAIL: %s - add e2 failed\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->leak_count != 2 || group->total_bytes != 300) {
        fprintf(stderr, "FAIL: %s - stats wrong after add e2\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    /* Entry order: e2 -> e1 (prepended) */
    if (group->entries != e2 || group->entries->next != e1) {
        fprintf(stderr, "FAIL: %s - wrong entry order\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_group_destroy(group);
    return 1;
}

/**
 * Test popping entries from a group.
 */
static int test_group_pop_entry(void) {
    leak_group_t* group = leak_group_create("pop.c", 20, false);
    if (!group) return 0;
    
    leak_entry_t* e1 = leak_entry_create((void*)0x1000, 100, NULL, 0, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x2000, 200, NULL, 0, false);
    
    if (!e1 || !e2) {
        leak_group_destroy(group);
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        return 0;
    }
    
    leak_group_add_entry(group, e1);
    leak_group_add_entry(group, e2);
    
    /* Pop e2 (head) */
    leak_entry_t* popped = leak_group_pop_entry(group);
    if (popped != e2) {
        fprintf(stderr, "FAIL: %s - wrong entry popped\n", __func__);
        leak_entry_destroy(popped);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->leak_count != 1 || group->total_bytes != 100) {
        fprintf(stderr, "FAIL: %s - stats wrong after pop\n", __func__);
        leak_entry_destroy(popped);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_entry_destroy(popped);
    
    /* Pop e1 */
    popped = leak_group_pop_entry(group);
    if (popped != e1) {
        fprintf(stderr, "FAIL: %s - wrong second entry popped\n", __func__);
        leak_entry_destroy(popped);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->leak_count != 0 || group->total_bytes != 0) {
        fprintf(stderr, "FAIL: %s - stats not zero after all pops\n", __func__);
        leak_entry_destroy(popped);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_entry_destroy(popped);
    
    /* Pop from empty group */
    popped = leak_group_pop_entry(group);
    if (popped != NULL) {
        fprintf(stderr, "FAIL: %s - pop from empty not NULL\n", __func__);
        leak_entry_destroy(popped);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_group_destroy(group);
    return 1;
}

/**
 * Test cloning a group.
 */
static int test_group_clone(void) {
    leak_group_t* original = leak_group_create("clone_group.c", 77, false);
    if (!original) return 0;
    
    void* frames[2] = {(void*)0x1000, (void*)0x2000};
    leak_group_set_backtrace(original, frames, 2);
    
    leak_entry_t* e1 = leak_entry_create((void*)0x100, 50, "clone_group.c", 77, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x200, 75, "clone_group.c", 77, false);
    if (!e1 || !e2) {
        leak_group_destroy(original);
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        return 0;
    }
    
    leak_group_add_entry(original, e1);
    leak_group_add_entry(original, e2);
    
    leak_group_t* clone = leak_group_clone(original);
    if (clone == NULL) {
        fprintf(stderr, "FAIL: %s - clone is NULL\n", __func__);
        leak_group_destroy(original);
        return 0;
    }
    
    /* Verify clone values */
    if (clone->line != original->line ||
        clone->leak_count != original->leak_count ||
        clone->total_bytes != original->total_bytes ||
        clone->signature != original->signature ||
        clone->frame_count != original->frame_count) {
        fprintf(stderr, "FAIL: %s - clone values differ\n", __func__);
        leak_group_destroy(original);
        leak_group_destroy(clone);
        return 0;
    }
    
    /* Verify entries are different objects */
    if (clone->entries == original->entries) {
        fprintf(stderr, "FAIL: %s - entries not cloned\n", __func__);
        leak_group_destroy(original);
        leak_group_destroy(clone);
        return 0;
    }
    
    /* Count entries */
    if (leak_entry_chain_count(clone->entries) != 2) {
        fprintf(stderr, "FAIL: %s - wrong entry count in clone\n", __func__);
        leak_group_destroy(original);
        leak_group_destroy(clone);
        return 0;
    }
    
    leak_group_destroy(original);
    /* Clone owns copies of strings - leak_group_destroy handles them */
    leak_group_destroy(clone);
    return 1;
}

/**
 * Test setting backtrace on group.
 */
static int test_group_set_backtrace(void) {
    leak_group_t* group = leak_group_create("bt.c", 1, false);
    if (!group) return 0;
    
    void* frames[3] = {(void*)0x111, (void*)0x222, (void*)0x333};
    leak_group_set_backtrace(group, frames, 3);
    
    if (group->frame_count != 3) {
        fprintf(stderr, "FAIL: %s - wrong frame count\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->signature == 0) {
        fprintf(stderr, "FAIL: %s - signature not computed\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    uint64_t expected_sig = backtrace_compute_signature(frames, 3);
    if (group->signature != expected_sig) {
        fprintf(stderr, "FAIL: %s - signature mismatch\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_group_destroy(group);
    return 1;
}

/**
 * Test recalculating group stats.
 */
static int test_group_recalculate_stats(void) {
    leak_group_t* group = leak_group_create("recalc.c", 1, false);
    if (!group) return 0;
    
    leak_entry_t* e1 = leak_entry_create((void*)0x1, 100, NULL, 0, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x2, 200, NULL, 0, false);
    if (!e1 || !e2) {
        leak_group_destroy(group);
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        return 0;
    }
    
    /* Manually add without using leak_group_add_entry */
    e1->next = e2;
    group->entries = e1;
    group->leak_count = 999;  /* Wrong */
    group->total_bytes = 999; /* Wrong */
    
    leak_group_recalculate_stats(group);
    
    if (group->leak_count != 2) {
        fprintf(stderr, "FAIL: %s - wrong leak_count\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    if (group->total_bytes != 300) {
        fprintf(stderr, "FAIL: %s - wrong total_bytes\n", __func__);
        leak_group_destroy(group);
        return 0;
    }
    
    leak_group_destroy(group);
    return 1;
}

/**
 * Test destroying NULL group.
 */
static int test_group_destroy_null(void) {
    /* Should not crash */
    leak_group_destroy(NULL);
    leak_group_destroy_chain(NULL);
    return 1;
}

/* ============================================================================
 * Leak Report Tests
 * ============================================================================ */

/**
 * Test creating a leak report.
 */
static int test_report_create(void) {
    leak_report_t* report = leak_report_create();
    
    if (report == NULL) {
        fprintf(stderr, "FAIL: %s - report is NULL\n", __func__);
        return 0;
    }
    
    if (report->total_leaks != 0 ||
        report->total_bytes != 0 ||
        report->group_count != 0) {
        fprintf(stderr, "FAIL: %s - stats not zero\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    if (report->severity != LEAK_SEVERITY_NONE) {
        fprintf(stderr, "FAIL: %s - wrong initial severity\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    if (report->groups != NULL) {
        fprintf(stderr, "FAIL: %s - groups not NULL\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test adding groups to a report.
 */
static int test_report_add_group(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    leak_group_t* g1 = leak_group_create("g1.c", 10, false);
    leak_group_t* g2 = leak_group_create("g2.c", 20, false);
    if (!g1 || !g2) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        return 0;
    }
    
    /* Add entries to groups */
    leak_entry_t* e1 = leak_entry_create((void*)0x1, 100, NULL, 0, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x2, 500, NULL, 0, false);
    if (!e1 || !e2) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        return 0;
    }
    
    leak_group_add_entry(g1, e1);
    leak_group_add_entry(g2, e2);
    
    if (!leak_report_add_group(report, g1)) {
        fprintf(stderr, "FAIL: %s - add g1 failed\n", __func__);
        leak_report_destroy(report);
        leak_group_destroy(g2);
        return 0;
    }
    
    if (report->group_count != 1 ||
        report->total_leaks != 1 ||
        report->total_bytes != 100) {
        fprintf(stderr, "FAIL: %s - stats wrong after g1\n", __func__);
        leak_report_destroy(report);
        leak_group_destroy(g2);
        return 0;
    }
    
    if (!leak_report_add_group(report, g2)) {
        fprintf(stderr, "FAIL: %s - add g2 failed\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    if (report->group_count != 2 ||
        report->total_leaks != 2 ||
        report->total_bytes != 600) {
        fprintf(stderr, "FAIL: %s - stats wrong after g2\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    /* Check severity (600 bytes < 1KB = LOW) */
    if (report->severity != LEAK_SEVERITY_LOW) {
        fprintf(stderr, "FAIL: %s - wrong severity\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test popping groups from a report.
 */
static int test_report_pop_group(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    leak_group_t* g1 = leak_group_create("pop1.c", 1, false);
    leak_group_t* g2 = leak_group_create("pop2.c", 2, false);
    if (!g1 || !g2) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        return 0;
    }
    
    g1->leak_count = 1;
    g1->total_bytes = 100;
    g2->leak_count = 2;
    g2->total_bytes = 200;
    
    leak_report_add_group(report, g1);
    leak_report_add_group(report, g2);
    
    /* Pop g2 (head) */
    leak_group_t* popped = leak_report_pop_group(report);
    if (popped != g2) {
        fprintf(stderr, "FAIL: %s - wrong group popped\n", __func__);
        leak_group_destroy(popped);
        leak_report_destroy(report);
        return 0;
    }
    
    if (report->group_count != 1 ||
        report->total_leaks != 1 ||
        report->total_bytes != 100) {
        fprintf(stderr, "FAIL: %s - stats wrong after pop\n", __func__);
        leak_group_destroy(popped);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_group_destroy(popped);
    
    /* Pop from empty */
    popped = leak_report_pop_group(report);
    if (popped != g1) {
        fprintf(stderr, "FAIL: %s - wrong second group popped\n", __func__);
        leak_group_destroy(popped);
        leak_report_destroy(report);
        return 0;
    }
    leak_group_destroy(popped);
    
    popped = leak_report_pop_group(report);
    if (popped != NULL) {
        fprintf(stderr, "FAIL: %s - pop from empty not NULL\n", __func__);
        leak_group_destroy(popped);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test cloning a report.
 */
static int test_report_clone(void) {
    leak_report_t* original = leak_report_create();
    if (!original) return 0;
    
    original->detection_time_us = 12345;
    original->suppression_applied = true;
    
    leak_group_t* g = leak_group_create("clone_report.c", 1, false);
    leak_entry_t* e = leak_entry_create((void*)0x1, 2048, NULL, 0, false);
    if (!g || !e) {
        leak_report_destroy(original);
        leak_group_destroy(g);
        leak_entry_destroy(e);
        return 0;
    }
    
    leak_group_add_entry(g, e);
    leak_report_add_group(original, g);
    
    leak_report_t* clone = leak_report_clone(original);
    if (clone == NULL) {
        fprintf(stderr, "FAIL: %s - clone is NULL\n", __func__);
        leak_report_destroy(original);
        return 0;
    }
    
    /* Verify clone values */
    if (clone->total_leaks != original->total_leaks ||
        clone->total_bytes != original->total_bytes ||
        clone->group_count != original->group_count ||
        clone->severity != original->severity ||
        clone->detection_time_us != original->detection_time_us ||
        clone->suppression_applied != original->suppression_applied) {
        fprintf(stderr, "FAIL: %s - clone values differ\n", __func__);
        leak_report_destroy(original);
        leak_report_destroy(clone);
        return 0;
    }
    
    /* Groups should be different objects */
    if (clone->groups == original->groups) {
        fprintf(stderr, "FAIL: %s - groups not cloned\n", __func__);
        leak_report_destroy(original);
        leak_report_destroy(clone);
        return 0;
    }
    
    leak_report_destroy(original);
    leak_report_destroy(clone);
    return 1;
}

/**
 * Test finding group by signature.
 */
static int test_report_find_group(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    leak_group_t* g1 = leak_group_create("find1.c", 1, false);
    leak_group_t* g2 = leak_group_create("find2.c", 2, false);
    if (!g1 || !g2) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        return 0;
    }
    
    void* frames1[1] = {(void*)0x1111};
    void* frames2[1] = {(void*)0x2222};
    leak_group_set_backtrace(g1, frames1, 1);
    leak_group_set_backtrace(g2, frames2, 1);
    
    leak_report_add_group(report, g1);
    leak_report_add_group(report, g2);
    
    /* Find g1 by signature */
    leak_group_t* found = leak_report_find_group_by_signature(report, g1->signature);
    if (found != g1) {
        fprintf(stderr, "FAIL: %s - g1 not found\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    /* Find g2 by signature */
    found = leak_report_find_group_by_signature(report, g2->signature);
    if (found != g2) {
        fprintf(stderr, "FAIL: %s - g2 not found\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    /* Find non-existent */
    found = leak_report_find_group_by_signature(report, 0xDEADBEEF);
    if (found != NULL) {
        fprintf(stderr, "FAIL: %s - found non-existent\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test recalculating report stats.
 */
static int test_report_recalculate_stats(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    leak_group_t* g1 = leak_group_create("recalc1.c", 1, false);
    leak_group_t* g2 = leak_group_create("recalc2.c", 2, false);
    if (!g1 || !g2) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        return 0;
    }
    
    g1->leak_count = 3;
    g1->total_bytes = 1000;
    g2->leak_count = 2;
    g2->total_bytes = 500;
    
    /* Manually link without using add_group */
    g1->next = g2;
    report->groups = g1;
    report->group_count = 999;  /* Wrong */
    report->total_leaks = 999;  /* Wrong */
    report->total_bytes = 999;  /* Wrong */
    
    leak_report_recalculate_stats(report);
    
    if (report->group_count != 2) {
        fprintf(stderr, "FAIL: %s - wrong group_count\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    if (report->total_leaks != 5) {
        fprintf(stderr, "FAIL: %s - wrong total_leaks\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    if (report->total_bytes != 1500) {
        fprintf(stderr, "FAIL: %s - wrong total_bytes\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    /* 1500 bytes = MEDIUM severity (> 1KB) */
    if (report->severity != LEAK_SEVERITY_MEDIUM) {
        fprintf(stderr, "FAIL: %s - wrong severity\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test merging reports.
 */
static int test_report_merge(void) {
    leak_report_t* dest = leak_report_create();
    leak_report_t* src = leak_report_create();
    if (!dest || !src) {
        leak_report_destroy(dest);
        leak_report_destroy(src);
        return 0;
    }
    
    /* Add group to dest */
    leak_group_t* g_dest = leak_group_create("dest.c", 1, false);
    leak_entry_t* e_dest = leak_entry_create((void*)0x1, 100, NULL, 0, false);
    if (!g_dest || !e_dest) {
        leak_report_destroy(dest);
        leak_report_destroy(src);
        leak_group_destroy(g_dest);
        leak_entry_destroy(e_dest);
        return 0;
    }
    leak_group_add_entry(g_dest, e_dest);
    void* frames_dest[1] = {(void*)0xDDD};
    leak_group_set_backtrace(g_dest, frames_dest, 1);
    leak_report_add_group(dest, g_dest);
    
    /* Add group to src with different signature */
    leak_group_t* g_src = leak_group_create("src.c", 2, false);
    leak_entry_t* e_src = leak_entry_create((void*)0x2, 200, NULL, 0, false);
    if (!g_src || !e_src) {
        leak_report_destroy(dest);
        leak_report_destroy(src);
        leak_group_destroy(g_src);
        leak_entry_destroy(e_src);
        return 0;
    }
    leak_group_add_entry(g_src, e_src);
    void* frames_src[1] = {(void*)0xBBB};
    leak_group_set_backtrace(g_src, frames_src, 1);
    leak_report_add_group(src, g_src);
    
    /* Merge */
    if (!leak_report_merge(dest, src)) {
        fprintf(stderr, "FAIL: %s - merge failed\n", __func__);
        leak_report_destroy(dest);
        leak_report_destroy(src);
        return 0;
    }
    
    /* Dest should have 2 groups now */
    if (dest->group_count != 2) {
        fprintf(stderr, "FAIL: %s - wrong group count after merge: %zu\n", 
                __func__, dest->group_count);
        leak_report_destroy(dest);
        leak_report_destroy(src);
        return 0;
    }
    
    if (dest->total_leaks != 2) {
        fprintf(stderr, "FAIL: %s - wrong total_leaks: %zu\n", 
                __func__, dest->total_leaks);
        leak_report_destroy(dest);
        leak_report_destroy(src);
        return 0;
    }
    
    if (dest->total_bytes != 300) {
        fprintf(stderr, "FAIL: %s - wrong total_bytes: %zu\n", 
                __func__, dest->total_bytes);
        leak_report_destroy(dest);
        leak_report_destroy(src);
        return 0;
    }
    
    /* Source unchanged */
    if (src->group_count != 1 || src->total_leaks != 1) {
        fprintf(stderr, "FAIL: %s - source was modified\n", __func__);
        leak_report_destroy(dest);
        leak_report_destroy(src);
        return 0;
    }
    
    leak_report_destroy(dest);
    leak_report_destroy(src);
    return 1;
}

/**
 * Test clearing a report.
 */
static int test_report_clear(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    leak_group_t* g = leak_group_create("clear.c", 1, false);
    leak_entry_t* e = leak_entry_create((void*)0x1, 500, NULL, 0, false);
    if (!g || !e) {
        leak_report_destroy(report);
        leak_group_destroy(g);
        leak_entry_destroy(e);
        return 0;
    }
    
    leak_group_add_entry(g, e);
    leak_report_add_group(report, g);
    
    leak_report_clear(report);
    
    if (report->groups != NULL ||
        report->group_count != 0 ||
        report->total_leaks != 0 ||
        report->total_bytes != 0 ||
        report->severity != LEAK_SEVERITY_NONE) {
        fprintf(stderr, "FAIL: %s - report not cleared\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test iterating over entries.
 */
static int iteration_count = 0;
static size_t iteration_total_size = 0;

static bool count_callback(const leak_entry_t* entry,
                           const leak_group_t* group,
                           void* user_data) {
    (void)group;
    (void)user_data;
    iteration_count++;
    iteration_total_size += entry->size;
    return true;  /* Continue */
}

static bool stop_callback(const leak_entry_t* entry,
                          const leak_group_t* group,
                          void* user_data) {
    (void)entry;
    (void)group;
    int* count = (int*)user_data;
    (*count)++;
    return (*count) < 2;  /* Stop after 2 */
}

static int test_report_iterate_entries(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    leak_group_t* g1 = leak_group_create("iter1.c", 1, false);
    leak_group_t* g2 = leak_group_create("iter2.c", 2, false);
    if (!g1 || !g2) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        return 0;
    }
    
    leak_entry_t* e1 = leak_entry_create((void*)0x1, 10, NULL, 0, false);
    leak_entry_t* e2 = leak_entry_create((void*)0x2, 20, NULL, 0, false);
    leak_entry_t* e3 = leak_entry_create((void*)0x3, 30, NULL, 0, false);
    if (!e1 || !e2 || !e3) {
        leak_report_destroy(report);
        leak_group_destroy(g1);
        leak_group_destroy(g2);
        leak_entry_destroy(e1);
        leak_entry_destroy(e2);
        leak_entry_destroy(e3);
        return 0;
    }
    
    leak_group_add_entry(g1, e1);
    leak_group_add_entry(g1, e2);
    leak_group_add_entry(g2, e3);
    
    leak_report_add_group(report, g1);
    leak_report_add_group(report, g2);
    
    /* Test full iteration */
    iteration_count = 0;
    iteration_total_size = 0;
    int result = leak_report_iterate_entries(report, count_callback, NULL);
    
    if (result != 3) {
        fprintf(stderr, "FAIL: %s - wrong iteration result: %d\n", __func__, result);
        leak_report_destroy(report);
        return 0;
    }
    
    if (iteration_count != 3) {
        fprintf(stderr, "FAIL: %s - wrong iteration count: %d\n", __func__, iteration_count);
        leak_report_destroy(report);
        return 0;
    }
    
    if (iteration_total_size != 60) {
        fprintf(stderr, "FAIL: %s - wrong total size: %zu\n", __func__, iteration_total_size);
        leak_report_destroy(report);
        return 0;
    }
    
    /* Test early stop */
    int stop_count = 0;
    result = leak_report_iterate_entries(report, stop_callback, &stop_count);
    
    if (result != -1) {
        fprintf(stderr, "FAIL: %s - should return -1 on stop\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    if (stop_count != 2) {
        fprintf(stderr, "FAIL: %s - stop count wrong: %d\n", __func__, stop_count);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/**
 * Test NULL handling for report functions.
 */
static int test_report_null_handling(void) {
    /* Should not crash */
    leak_report_destroy(NULL);
    leak_report_clear(NULL);
    leak_report_recalculate_stats(NULL);
    
    if (leak_report_clone(NULL) != NULL) {
        fprintf(stderr, "FAIL: %s - clone NULL should return NULL\n", __func__);
        return 0;
    }
    
    if (leak_report_add_group(NULL, NULL)) {
        fprintf(stderr, "FAIL: %s - add to NULL should fail\n", __func__);
        return 0;
    }
    
    if (leak_report_pop_group(NULL) != NULL) {
        fprintf(stderr, "FAIL: %s - pop from NULL should return NULL\n", __func__);
        return 0;
    }
    
    if (leak_report_find_group_by_signature(NULL, 0) != NULL) {
        fprintf(stderr, "FAIL: %s - find in NULL should return NULL\n", __func__);
        return 0;
    }
    
    if (leak_report_merge(NULL, NULL)) {
        fprintf(stderr, "FAIL: %s - merge NULL should fail\n", __func__);
        return 0;
    }
    
    if (leak_report_iterate_entries(NULL, NULL, NULL) != 0) {
        fprintf(stderr, "FAIL: %s - iterate NULL should return 0\n", __func__);
        return 0;
    }
    
    return 1;
}

/**
 * Test severity levels.
 */
static int test_severity_levels(void) {
    leak_report_t* report = leak_report_create();
    if (!report) return 0;
    
    /* NONE - no leaks */
    if (report->severity != LEAK_SEVERITY_NONE) {
        fprintf(stderr, "FAIL: %s - empty report not NONE\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    /* LOW - < 1KB */
    leak_group_t* g = leak_group_create("sev.c", 1, false);
    g->total_bytes = 500;
    g->leak_count = 1;
    leak_report_add_group(report, g);
    
    if (report->severity != LEAK_SEVERITY_LOW) {
        fprintf(stderr, "FAIL: %s - 500B not LOW\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    /* MEDIUM - 1KB to 1MB */
    g = leak_group_create("sev.c", 2, false);
    g->total_bytes = 5000;
    g->leak_count = 1;
    leak_report_add_group(report, g);
    
    if (report->severity != LEAK_SEVERITY_MEDIUM) {
        fprintf(stderr, "FAIL: %s - 5.5KB not MEDIUM\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_clear(report);
    
    /* HIGH - 1MB to 100MB */
    g = leak_group_create("sev.c", 3, false);
    g->total_bytes = 10 * 1024 * 1024;  /* 10MB */
    g->leak_count = 1;
    leak_report_add_group(report, g);
    
    if (report->severity != LEAK_SEVERITY_HIGH) {
        fprintf(stderr, "FAIL: %s - 10MB not HIGH\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_clear(report);
    
    /* CRITICAL - > 100MB */
    g = leak_group_create("sev.c", 4, false);
    g->total_bytes = 200UL * 1024 * 1024;  /* 200MB */
    g->leak_count = 1;
    leak_report_add_group(report, g);
    
    if (report->severity != LEAK_SEVERITY_CRITICAL) {
        fprintf(stderr, "FAIL: %s - 200MB not CRITICAL\n", __func__);
        leak_report_destroy(report);
        return 0;
    }
    
    leak_report_destroy(report);
    return 1;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("========================================\n");
    printf("Leak Structure Lifecycle Unit Tests\n");
    printf("MEMRO-18\n");
    printf("========================================\n\n");
    
    /* Entry tests */
    printf("--- Leak Entry Tests ---\n");
    RUN_TEST(test_entry_create);
    RUN_TEST(test_entry_create_copy_strings);
    RUN_TEST(test_entry_create_null_file);
    RUN_TEST(test_entry_clone);
    RUN_TEST(test_entry_clone_null);
    RUN_TEST(test_entry_set_backtrace);
    RUN_TEST(test_entry_chain_count);
    RUN_TEST(test_entry_destroy_null);
    
    /* Group tests */
    printf("\n--- Leak Group Tests ---\n");
    RUN_TEST(test_group_create);
    RUN_TEST(test_group_add_entry);
    RUN_TEST(test_group_pop_entry);
    RUN_TEST(test_group_clone);
    RUN_TEST(test_group_set_backtrace);
    RUN_TEST(test_group_recalculate_stats);
    RUN_TEST(test_group_destroy_null);
    
    /* Report tests */
    printf("\n--- Leak Report Tests ---\n");
    RUN_TEST(test_report_create);
    RUN_TEST(test_report_add_group);
    RUN_TEST(test_report_pop_group);
    RUN_TEST(test_report_clone);
    RUN_TEST(test_report_find_group);
    RUN_TEST(test_report_recalculate_stats);
    RUN_TEST(test_report_merge);
    RUN_TEST(test_report_clear);
    RUN_TEST(test_report_iterate_entries);
    RUN_TEST(test_report_null_handling);
    RUN_TEST(test_severity_levels);
    
    /* Summary */
    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n");
    
    return (tests_passed == tests_run) ? 0 : 1;
}
