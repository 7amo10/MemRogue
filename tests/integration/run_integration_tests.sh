#!/bin/bash
# ============================================================================
# MemRogue Integration Test Runner
# MEMRO-25: Integration Test Suite
#
# Runs all integration tests and generates a summary report.
# ============================================================================

# Don't use set -e since we want to capture test failures and continue
# Error handling is done explicitly in the script

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
BIN_DIR="${BUILD_DIR}/bin"

# Test result tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
declare -a FAILED_TEST_NAMES
declare -a PASSED_TEST_NAMES
declare -a SKIPPED_TEST_NAMES

# ============================================================================
# Utility Functions
# ============================================================================

print_header() {
    echo ""
    echo -e "${BLUE}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║${NC}     ${BOLD}MemRogue Integration Test Suite${NC}                            ${BLUE}║${NC}"
    echo -e "${BLUE}║${NC}     MEMRO-25: Comprehensive Integration Testing               ${BLUE}║${NC}"
    echo -e "${BLUE}╚════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

print_section() {
    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

run_test() {
    local test_name="$1"
    local test_binary="$2"
    local test_desc="$3"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    echo -n -e "  ${YELLOW}▶${NC} Running ${BOLD}${test_name}${NC}... "
    
    if [[ ! -x "$test_binary" ]]; then
        echo -e "${YELLOW}SKIPPED${NC} (binary not found)"
        SKIPPED_TESTS=$((SKIPPED_TESTS + 1))
        SKIPPED_TEST_NAMES+=("$test_name")
        return 0
    fi
    
    # Run test with timeout
    local start_time=$(date +%s.%N)
    local output
    local exit_code
    
    output=$("$test_binary" 2>&1) && exit_code=0 || exit_code=$?
    
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc 2>/dev/null || echo "0.00")
    
    if [[ $exit_code -eq 0 ]]; then
        echo -e "${GREEN}PASSED${NC} (${duration}s)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        PASSED_TEST_NAMES+=("$test_name")
        
        # Show test output in verbose mode
        if [[ "${VERBOSE:-0}" == "1" ]]; then
            echo "$output" | sed 's/^/      /'
        fi
    else
        echo -e "${RED}FAILED${NC} (exit code: $exit_code)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        FAILED_TEST_NAMES+=("$test_name")
        
        # Always show output for failed tests
        echo "$output" | sed 's/^/      /' | head -50
        if [[ $(echo "$output" | wc -l) -gt 50 ]]; then
            echo "      ... (output truncated)"
        fi
    fi
    
    # Don't return exit code - we track failures ourselves
    return 0
}

# ============================================================================
# Pre-flight Checks
# ============================================================================

check_prerequisites() {
    print_section "Pre-flight Checks"
    
    # Check build directory
    if [[ ! -d "$BUILD_DIR" ]]; then
        echo -e "  ${RED}✗${NC} Build directory not found at $BUILD_DIR"
        echo -e "    Run: ${CYAN}mkdir build && cd build && cmake .. && make${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}✓${NC} Build directory found"
    
    # Check binary directory
    if [[ ! -d "$BIN_DIR" ]]; then
        echo -e "  ${RED}✗${NC} Binary directory not found at $BIN_DIR"
        echo -e "    Run: ${CYAN}cd build && make${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}✓${NC} Binary directory found"
    
    # Check for integration test binaries
    local integ_tests=("integ_simple_leaks" "integ_complex_patterns" 
                       "integ_multithreaded" "integ_error_conditions"
                       "integ_report_formats")
    
    local missing=0
    for test in "${integ_tests[@]}"; do
        if [[ ! -f "$BIN_DIR/$test" ]]; then
            echo -e "  ${YELLOW}!${NC} Integration test binary not found: $test"
            missing=$((missing + 1))
        fi
    done
    
    if [[ $missing -eq ${#integ_tests[@]} ]]; then
        echo -e "  ${RED}✗${NC} No integration test binaries found"
        echo -e "    Run: ${CYAN}cd build && make${NC}"
        exit 1
    fi
    
    if [[ $missing -eq 0 ]]; then
        echo -e "  ${GREEN}✓${NC} All integration test binaries found"
    else
        echo -e "  ${YELLOW}!${NC} Some integration test binaries missing ($missing)"
    fi
    
    echo ""
}

# ============================================================================
# Test Execution
# ============================================================================

run_integration_tests() {
    print_section "Running Integration Tests"
    
    # Simple Leaks
    run_test "Simple Leaks" \
        "$BIN_DIR/integ_simple_leaks" \
        "Basic memory leak detection patterns"
    
    # Complex Patterns
    run_test "Complex Patterns" \
        "$BIN_DIR/integ_complex_patterns" \
        "Complex data structure leak detection"
    
    # Multithreaded
    run_test "Multithreaded" \
        "$BIN_DIR/integ_multithreaded" \
        "Thread-safe memory tracking"
    
    # Error Conditions
    run_test "Error Conditions" \
        "$BIN_DIR/integ_error_conditions" \
        "Error handling and edge cases"
    
    # Report Formats
    run_test "Report Formats" \
        "$BIN_DIR/integ_report_formats" \
        "Report generation and export formats"
}

run_unit_tests_subset() {
    print_section "Running Unit Tests (Subset)"
    
    # Run a subset of critical unit tests
    local unit_tests=(
        "test_hash_table"
        "test_tracker"
        "test_leak_detector"
        "test_json"
        "test_csv"
    )
    
    for test in "${unit_tests[@]}"; do
        run_test "$test" "$BIN_DIR/$test" "Unit test"
    done
}

# ============================================================================
# Report Generation
# ============================================================================

print_summary() {
    print_section "Test Summary"
    
    echo ""
    echo -e "  ${BOLD}Results:${NC}"
    echo -e "    Total:   ${TOTAL_TESTS}"
    echo -e "    Passed:  ${GREEN}${PASSED_TESTS}${NC}"
    echo -e "    Failed:  ${RED}${FAILED_TESTS}${NC}"
    echo -e "    Skipped: ${YELLOW}${SKIPPED_TESTS}${NC}"
    echo ""
    
    if [[ ${#FAILED_TEST_NAMES[@]} -gt 0 ]]; then
        echo -e "  ${BOLD}Failed Tests:${NC}"
        for name in "${FAILED_TEST_NAMES[@]}"; do
            echo -e "    ${RED}✗${NC} $name"
        done
        echo ""
    fi
    
    # Calculate pass rate
    local rate=0
    if [[ $TOTAL_TESTS -gt 0 ]]; then
        rate=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    fi
    
    if [[ $FAILED_TESTS -eq 0 ]]; then
        echo -e "  ${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "  ${GREEN}  ✓ All tests passed! (${rate}%)${NC}"
        echo -e "  ${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    else
        echo -e "  ${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "  ${RED}  ✗ Some tests failed (${rate}% passed)${NC}"
        echo -e "  ${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    fi
    echo ""
}

generate_junit_report() {
    local report_file="${BUILD_DIR}/integration-test-results.xml"
    
    echo '<?xml version="1.0" encoding="UTF-8"?>' > "$report_file"
    echo "<testsuites tests=\"$TOTAL_TESTS\" failures=\"$FAILED_TESTS\" skipped=\"$SKIPPED_TESTS\">" >> "$report_file"
    echo "  <testsuite name=\"integration\" tests=\"$TOTAL_TESTS\" failures=\"$FAILED_TESTS\" skipped=\"$SKIPPED_TESTS\">" >> "$report_file"
    
    # Add passed tests with actual names
    for name in "${PASSED_TEST_NAMES[@]}"; do
        echo "    <testcase name=\"$name\" classname=\"integration\"/>" >> "$report_file"
    done
    
    # Add skipped tests with actual names
    for name in "${SKIPPED_TEST_NAMES[@]}"; do
        echo "    <testcase name=\"$name\" classname=\"integration\">" >> "$report_file"
        echo "      <skipped message=\"Binary not found\"/>" >> "$report_file"
        echo "    </testcase>" >> "$report_file"
    done
    
    # Add failed tests with actual names
    for name in "${FAILED_TEST_NAMES[@]}"; do
        echo "    <testcase name=\"$name\" classname=\"integration\">" >> "$report_file"
        echo "      <failure message=\"Test failed\"/>" >> "$report_file"
        echo "    </testcase>" >> "$report_file"
    done
    
    echo "  </testsuite>" >> "$report_file"
    echo "</testsuites>" >> "$report_file"
    
    echo -e "  ${CYAN}JUnit report:${NC} $report_file"
}

# ============================================================================
# Main
# ============================================================================

main() {
    print_header
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -v|--verbose)
                VERBOSE=1
                shift
                ;;
            -u|--unit)
                RUN_UNIT=1
                shift
                ;;
            -h|--help)
                echo "Usage: $0 [options]"
                echo ""
                echo "Options:"
                echo "  -v, --verbose   Show detailed test output"
                echo "  -u, --unit      Also run unit tests subset"
                echo "  -h, --help      Show this help message"
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                exit 1
                ;;
        esac
    done
    
    check_prerequisites
    run_integration_tests
    
    if [[ "${RUN_UNIT:-0}" == "1" ]]; then
        run_unit_tests_subset
    fi
    
    print_summary
    generate_junit_report
    
    # Exit with failure if any tests failed
    if [[ $FAILED_TESTS -gt 0 ]]; then
        exit 1
    fi
    
    exit 0
}

main "$@"
