<img src="https://r2cdn.perplexity.ai/pplx-full-logo-primary-dark%402x.png" style="height:64px;margin-right:32px"/>

# **Custom Memory Debugger - Project Plan for Plane**


***

## **PROJECT DETAILS**

### **Project Name**

Custom Memory Debugger

### **Project Identifier**

`MEMDEBUG`

### **Project Description**

A lightweight, production-ready memory debugging library for C/C++ applications that intercepts memory allocation/deallocation calls, tracks allocations, and provides real-time leak detection capabilities.

**Problem Statement:**
Memory leaks and improper memory management remain critical issues in systems programming, leading to application crashes, performance degradation, resource exhaustion, and difficult-to-debug production issues.

**Solution:**
Build a POSIX-compliant memory debugger that uses library preloading (`LD_PRELOAD`) to intercept standard memory functions, capture stack traces, and detect memory leaks with minimal performance overhead (<5%).

**Target Users:**

- Systems programmers working with C/C++ applications
- DevOps teams monitoring production services
- QA engineers testing memory-critical applications
- Open-source contributors needing debugging tools

**Success Metrics:**

- ✅ <5% performance overhead in production mode
- ✅ Zero false positives in leak detection
- ✅ 100% test coverage for core functions
- ✅ Used in at least 3 real-world projects
- ✅ Clear, actionable leak reports

**Technology Stack:**

- Language: C11/C++17
- Build System: CMake
- Testing: CUnit + Custom Test Suite
- CI/CD: GitHub Actions
- Platform: Linux (primary), macOS (secondary)

**Repository Structure:**

```
memdebug/
├── src/          # Core implementation
├── include/      # Public headers
├── tools/        # CLI utilities
├── tests/        # Test suite
├── examples/     # Usage examples
├── docs/         # Documentation
└── diagrams/     # Architecture diagrams
```


***

## **SPRINTS / CYCLES**


***

### **Sprint 1: Foundation \& Interception**

**Duration:** 2 weeks
**Goal:** Set up project infrastructure and implement basic memory interception

**Description:**
Establish the foundation of the memory debugger by creating the build system, implementing the interception layer using `LD_PRELOAD`, and setting up core data structures. By the end of this sprint, the library should be able to intercept malloc/free calls and log them to stdout.

**Key Deliverables:**

- Project repository with CMake build system
- malloc/free/calloc/realloc wrappers functional
- Basic hash table for allocation tracking
- Thread-safe lock mechanism
- Initial test suite (10+ unit tests)

***

### **Sprint 2: Stack Traces \& Tracking Core**

**Duration:** 2 weeks
**Goal:** Implement complete allocation tracking with stack trace capture

**Description:**
Enhance the tracking system to capture full call stacks for each allocation using `backtrace()` and resolve symbols using `backtrace_symbols()`. Implement the core allocation record storage and retrieval system with proper memory management.

**Key Deliverables:**

- Backtrace capture integration
- Symbol resolution working
- Complete allocation record structure
- Hash table insert/lookup/remove operations
- Frame filtering (skip debugger frames)
- 20+ unit tests covering edge cases

***

### **Sprint 3: Leak Detection \& Reporting**

**Duration:** 1.5 weeks
**Goal:** Implement leak detection engine and basic reporting

**Description:**
Build the leak detection algorithm that scans for unfreed allocations at program exit. Implement grouping of leaks by allocation site (stack trace signature) and create formatted text reports with statistics and leak details.

**Key Deliverables:**

- Exit hook for leak detection
- Double-free detection
- Invalid free detection
- Leak grouping by allocation site
- Text report formatter
- Statistics calculator (total leaks, bytes, etc.)

***

### **Sprint 4: CLI Tool \& Configuration**

**Duration:** 1.5 weeks
**Goal:** Create user-facing tools and configuration system

**Description:**
Develop the `memdebug-report` CLI tool for analyzing logs and generating reports. Implement the configuration system using environment variables to control tracking behavior, sampling rates, and output options.

**Key Deliverables:**

- CLI tool for report generation
- Environment variable parser
- Configuration manager
- Sampling mode implementation
- Multiple output format support (text/JSON/CSV)
- User documentation

***

### **Sprint 5: Testing, Documentation \& Release**

**Duration:** 1 week
**Goal:** Comprehensive testing, documentation, and v1.0 release

**Description:**
Complete the test suite with integration tests, stress tests, and real-world scenarios. Write comprehensive documentation including API docs, usage guides, and troubleshooting. Prepare for first release with examples and packaging.

**Key Deliverables:**

- 100+ test cases with full coverage
- API documentation
- User guide and examples
- Performance benchmarks
- GitHub release v1.0.0
- Installation scripts

***

## **WORK ITEMS**


***

## **Sprint 1: Foundation \& Interception**

### **MEMDEBUG-1: Project Setup \& Build System**

**Title:** Initialize project repository and CMake build system

**Description:**
Create the GitHub repository with proper structure, initialize CMakeLists.txt with appropriate compiler flags, and set up basic directory structure. Configure build options for debug/release modes and establish coding standards.

**Acceptance Criteria:**

- Repository created with LICENSE (MIT/Apache 2.0)
- CMakeLists.txt builds static and shared libraries
- Directory structure matches design document
- README.md with basic project info
- .gitignore configured for C/C++ projects

**Labels:** `setup`, `infrastructure`, `sprint-1`
**Cycle:** Sprint 1: Foundation \& Interception
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-2: Hash Table Implementation**

**Title:** Implement core hash table for allocation tracking

**Description:**
Build a thread-safe hash table to store allocation records with open chaining for collision resolution. Support insert, lookup, and remove operations with O(1) average complexity. Implement resize functionality when load factor exceeds threshold.

**Acceptance Criteria:**

- hash_table_create() and hash_table_destroy()
- hash_table_insert() with collision handling
- hash_table_lookup() with O(1) average time
- hash_table_remove() with proper memory cleanup
- Unit tests for 1000+ entries
- Thread-safe operations

**Labels:** `core`, `data-structures`, `sprint-1`
**Cycle:** Sprint 1: Foundation \& Interception
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-3: Memory Hook Interception Layer**

**Title:** Implement malloc/free/calloc/realloc wrappers

**Description:**
Create wrapper functions for all standard memory allocation functions that intercept calls, perform tracking, and delegate to real implementations. Use `dlsym()` with RTLD_NEXT to obtain original function pointers.

**Acceptance Criteria:**

- malloc_wrapper() intercepts and tracks allocations
- free_wrapper() intercepts and tracks deallocations
- calloc_wrapper() and realloc_wrapper() implemented
- Original functions called correctly via dlsym()
- LD_PRELOAD mechanism working
- Test with simple applications

**Labels:** `core`, `interception`, `sprint-1`
**Cycle:** Sprint 1: Foundation \& Interception
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-4: Thread Safety Layer**

**Title:** Implement thread-safe locking mechanism

**Description:**
Add pthread mutex locks to protect shared data structures from race conditions in multi-threaded applications. Implement lock acquisition/release with proper error handling and deadlock prevention.

**Acceptance Criteria:**

- pthread_mutex_t initialized in tracker
- lock_acquire() and lock_release() helpers
- All critical sections protected
- Multi-threaded test passes (10 threads, 10k allocations)
- No deadlocks or race conditions detected

**Labels:** `core`, `threading`, `sprint-1`
**Cycle:** Sprint 1: Foundation \& Interception
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-5: Allocation Record Structure**

**Title:** Define and implement allocation_info_t structure

**Description:**
Create the allocation record structure that stores all metadata for tracked allocations including address, size, timestamp, and backtrace. Implement creation and destruction functions.

**Acceptance Criteria:**

- allocation_info_t struct defined in header
- allocation_info_create() allocates and initializes
- allocation_info_destroy() frees resources
- Proper memory management (no leaks in debugger itself)
- Unit tests for structure lifecycle

**Labels:** `core`, `data-structures`, `sprint-1`
**Cycle:** Sprint 1: Foundation \& Interception
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-6: Basic Test Suite**

**Title:** Create initial unit test framework and tests

**Description:**
Set up CUnit testing framework or custom test harness. Write unit tests for hash table operations, allocation tracking, and interception layer. Integrate with CMake for automated testing.

**Acceptance Criteria:**

- Test framework integrated in CMake
- 10+ unit tests covering core functions
- Tests run with `make test`
- All tests passing
- Code coverage report generated

**Labels:** `testing`, `quality`, `sprint-1`
**Cycle:** Sprint 1: Foundation \& Interception
**Priority:** P1 (High)
**Status:** Todo

***

## **Sprint 2: Stack Traces \& Tracking Core**

### **MEMDEBUG-7: Backtrace Capture Integration**

**Title:** Implement stack trace capture using backtrace()

**Description:**
Integrate POSIX backtrace() function to capture call stacks at allocation time. Store up to 16 frames per allocation. Handle edge cases where backtrace fails or returns incomplete stacks.

**Acceptance Criteria:**

- backtrace_capture() function implemented
- Captures up to 16 stack frames
- Handles backtrace() failures gracefully
- Frames stored in allocation_info_t
- Unit tests verify frame capture

**Labels:** `core`, `debugging`, `sprint-2`
**Cycle:** Sprint 2: Stack Traces \& Tracking Core
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-8: Symbol Resolution**

**Title:** Resolve stack frame addresses to function names

**Description:**
Use backtrace_symbols() or libdw to convert frame addresses to function names, file names, and line numbers. Parse symbol information and store human-readable strings.

**Acceptance Criteria:**

- symbol_resolve() converts addresses to names
- Function names extracted from symbols
- File:line information when available
- Handles stripped binaries gracefully
- Memory-efficient symbol storage

**Labels:** `core`, `debugging`, `sprint-2`
**Cycle:** Sprint 2: Stack Traces \& Tracking Core
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-9: Frame Filtering**

**Title:** Filter internal debugger frames from backtraces

**Description:**
Implement logic to skip debugger's own frames (malloc_wrapper, tracker functions, etc.) from reported backtraces so users only see their application's call stack.

**Acceptance Criteria:**

- Identifies and skips debugger frames
- Configurable filter depth
- User frames start from actual allocation site
- Tested with various call stack depths

**Labels:** `core`, `debugging`, `sprint-2`
**Cycle:** Sprint 2: Stack Traces \& Tracking Core
**Priority:** P2 (Medium)
**Status:** Todo

***

### **MEMDEBUG-10: Complete Allocation Tracker**

**Title:** Finalize allocation tracking with full metadata storage

**Description:**
Complete the memory_tracker_t implementation with all operations: track_allocation(), track_deallocation(), lookup_allocation(). Ensure proper integration with hash table and statistics updates.

**Acceptance Criteria:**

- memory_tracker_t fully functional
- track_allocation() stores complete records
- track_deallocation() removes records properly
- lookup_allocation() works correctly
- Statistics updated on each operation
- No memory leaks in tracker itself

**Labels:** `core`, `tracking`, `sprint-2`
**Cycle:** Sprint 2: Stack Traces \& Tracking Core
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-11: Statistics Calculator**

**Title:** Implement real-time memory usage statistics

**Description:**
Build statistics_t structure that tracks total allocations, deallocations, current memory usage, peak memory, and allocation counts. Update statistics on every allocation/deallocation event.

**Acceptance Criteria:**

- statistics_t structure defined
- Real-time updates on alloc/free
- Peak memory tracking works correctly
- Average allocation size calculated
- statistics_format() produces readable output

**Labels:** `core`, `statistics`, `sprint-2`
**Cycle:** Sprint 2: Stack Traces \& Tracking Core
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-12: Comprehensive Unit Tests**

**Title:** Expand test suite to 20+ tests covering edge cases

**Description:**
Add tests for multithreading, large allocations, realloc scenarios, and error conditions. Test backtrace capture and symbol resolution with various scenarios.

**Acceptance Criteria:**

- 20+ unit tests total
- Thread safety tests (race conditions)
- Large allocation tests (1GB+)
- Realloc edge cases covered
- Backtrace tests verify frame capture
- All tests passing

**Labels:** `testing`, `quality`, `sprint-2`
**Cycle:** Sprint 2: Stack Traces \& Tracking Core
**Priority:** P1 (High)
**Status:** Todo

***

## **Sprint 3: Leak Detection \& Reporting**

### **MEMDEBUG-13: Exit Hook Implementation**

**Title:** Register exit handler for leak detection

**Description:**
Use atexit() or __attribute__((destructor)) to register cleanup function that runs at program termination. Trigger leak detection and report generation in this handler.

**Acceptance Criteria:**

- Exit handler registers successfully
- Runs at program termination
- Gracefully handles abnormal exits
- Calls leak detection engine
- Generates final report

**Labels:** `core`, `leak-detection`, `sprint-3`
**Cycle:** Sprint 3: Leak Detection \& Reporting
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-14: Leak Detection Engine**

**Title:** Implement algorithm to detect unfreed allocations

**Description:**
Scan hash table at exit to identify all allocations that were never freed. Filter out false positives (static allocations, known globals). Group leaks by allocation site using backtrace signatures.

**Acceptance Criteria:**

- Iterates all hash table entries
- Identifies truly leaked allocations
- Groups leaks by stack trace signature
- Calculates total leaked bytes
- Generates leak_report_t structure

**Labels:** `core`, `leak-detection`, `sprint-3`
**Cycle:** Sprint 3: Leak Detection \& Reporting
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-15: Double-Free Detection**

**Title:** Detect and report double-free errors

**Description:**
Check if a pointer being freed was already freed previously. Report violation with backtrace of both free calls. Optionally abort() based on configuration.

**Acceptance Criteria:**

- Detects double-free attempts
- Captures backtrace of both frees
- Reports violation clearly
- Configurable abort on error
- Test suite verifies detection

**Labels:** `core`, `error-detection`, `sprint-3`
**Cycle:** Sprint 3: Leak Detection \& Reporting
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-16: Invalid Free Detection**

**Title:** Detect attempts to free untracked pointers

**Description:**
Identify when free() is called with a pointer that was never allocated (or not tracked). Report the violation with current backtrace and pointer address.

**Acceptance Criteria:**

- Detects invalid free attempts
- Reports pointer and backtrace
- Distinguishes from double-free
- Configurable severity level
- Test suite includes invalid free cases

**Labels:** `core`, `error-detection`, `sprint-3`
**Cycle:** Sprint 3: Leak Detection \& Reporting
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-17: Text Report Formatter**

**Title:** Create human-readable text report generator

**Description:**
Format leak reports into readable text with sections for summary statistics, grouped leaks with backtraces, and totals. Include percentages and rankings by size/count.

**Acceptance Criteria:**

- Summary section with totals
- Grouped leak entries with backtraces
- Sorted by size (largest first)
- Readable formatting with indentation
- File:line information included
- Percentage of total calculated

**Labels:** `reporting`, `formatting`, `sprint-3`
**Cycle:** Sprint 3: Leak Detection \& Reporting
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-18: Leak Report Structure**

**Title:** Define leak_report_t and leak_group_t structures

**Description:**
Create data structures to hold leak detection results including grouped allocations, statistics, and metadata. Implement creation and destruction functions.

**Acceptance Criteria:**

- leak_report_t structure defined
- leak_group_t for grouped leaks
- Creation and destruction functions
- Proper memory management
- Unit tests for structure lifecycle

**Labels:** `core`, `data-structures`, `sprint-3`
**Cycle:** Sprint 3: Leak Detection \& Reporting
**Priority:** P1 (High)
**Status:** Todo

***

## **Sprint 4: CLI Tool \& Configuration**

### **MEMDEBUG-19: CLI Tool Foundation**

**Title:** Create memdebug-report command-line tool

**Description:**
Build the CLI tool that reads log files or live data and generates reports. Implement argument parsing, help text, and basic report generation workflow.

**Acceptance Criteria:**

- Standalone binary compiled
- Argument parsing with getopt()
- --help and --version flags
- Reads input files or stdin
- Generates report to stdout or file
- Error handling for invalid inputs

**Labels:** `tooling`, `cli`, `sprint-4`
**Cycle:** Sprint 4: CLI Tool \& Configuration
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-20: Environment Variable Configuration**

**Title:** Implement configuration via environment variables

**Description:**
Parse environment variables (MEMDEBUG_ENABLED, MEMDEBUG_OUTPUT, etc.) to control debugger behavior at runtime. Create config_t structure to hold all settings.

**Acceptance Criteria:**

- MEMDEBUG_ENABLED toggles tracking
- MEMDEBUG_OUTPUT sets output file
- MEMDEBUG_SAMPLE_RATE for sampling
- MEMDEBUG_BACKTRACE toggles stack traces
- MEMDEBUG_VERBOSITY for log levels
- Documentation of all variables

**Labels:** `configuration`, `usability`, `sprint-4`
**Cycle:** Sprint 4: CLI Tool \& Configuration
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-21: Sampling Mode**

**Title:** Implement allocation sampling to reduce overhead

**Description:**
Add sampling mode that tracks only every Nth allocation (configurable). Reduces overhead for production monitoring while still detecting leaks.

**Acceptance Criteria:**

- Sampling rate configurable via env var
- Random or deterministic sampling
- Statistics adjusted for sampling rate
- Overhead <1% with 100:1 sampling
- Documentation explains trade-offs

**Labels:** `optimization`, `configuration`, `sprint-4`
**Cycle:** Sprint 4: CLI Tool \& Configuration
**Priority:** P2 (Medium)
**Status:** Todo

***

### **MEMDEBUG-22: JSON Export Format**

**Title:** Add JSON output format for machine parsing

**Description:**
Implement JSON formatter for leak reports to enable integration with CI/CD tools, log aggregators, and monitoring systems.

**Acceptance Criteria:**

- report_to_json() function implemented
- Valid JSON schema
- Includes all leak data and statistics
- Pretty-printed or compact mode
- CLI tool supports --format=json

**Labels:** `reporting`, `formatting`, `sprint-4`
**Cycle:** Sprint 4: CLI Tool \& Configuration
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-23: CSV Export Format**

**Title:** Add CSV output format for spreadsheet analysis

**Description:**
Implement CSV formatter for exporting leak data in tabular format, suitable for importing into Excel or data analysis tools.

**Acceptance Criteria:**

- report_to_csv() function implemented
- Headers: address, size, timestamp, function, file, line
- Proper CSV escaping
- CLI tool supports --format=csv
- Example analysis scripts

**Labels:** `reporting`, `formatting`, `sprint-4`
**Cycle:** Sprint 4: CLI Tool \& Configuration
**Priority:** P2 (Medium)
**Status:** Todo

***

### **MEMDEBUG-24: User Documentation**

**Title:** Write comprehensive user guide and API documentation

**Description:**
Create documentation covering installation, usage, configuration options, troubleshooting, and examples. Include API reference for library integration.

**Acceptance Criteria:**

- README.md with quick start
- USAGE.md with detailed guide
- API.md with function reference
- TROUBLESHOOTING.md for common issues
- Example code snippets
- Configuration reference

**Labels:** `documentation`, `usability`, `sprint-4`
**Cycle:** Sprint 4: CLI Tool \& Configuration
**Priority:** P1 (High)
**Status:** Todo

***

## **Sprint 5: Testing, Documentation \& Release**

### **MEMDEBUG-25: Integration Test Suite**

**Title:** Create integration tests with real-world scenarios

**Description:**
Build integration tests using actual C programs with known leaks, multithreading, and edge cases. Verify end-to-end functionality from interception to reporting.

**Acceptance Criteria:**

- 20+ integration test programs
- Tests cover: simple leaks, complex patterns, multithreading
- Automated verification of reports
- Tests for all error conditions
- CI/CD integration (GitHub Actions)

**Labels:** `testing`, `quality`, `sprint-5`
**Cycle:** Sprint 5: Testing, Documentation \& Release
**Priority:** P0 (Critical)
**Status:** Todo

***

### **MEMDEBUG-26: Performance Benchmarks**

**Title:** Measure and document performance overhead

**Description:**
Create benchmark suite to measure overhead in various scenarios. Document results and compare with Valgrind. Ensure <5% overhead target is met.

**Acceptance Criteria:**

- Benchmark suite with 5+ scenarios
- Measures allocation/deallocation latency
- Tests throughput (allocs/second)
- Compares with baseline (no tracking)
- Results documented in PERFORMANCE.md
- Overhead <5% in all benchmarks

**Labels:** `testing`, `performance`, `sprint-5`
**Cycle:** Sprint 5: Testing, Documentation \& Release
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-27: Example Applications**

**Title:** Create example programs demonstrating usage

**Description:**
Build 3-5 example programs showing common use cases: simple leak, multithreaded app, real-world simulation. Include Makefiles and documentation.

**Acceptance Criteria:**

- basic_leak.c - simple memory leak example
- multithreaded.c - concurrent allocations
- webserver_sim.c - realistic server scenario
- custom_allocator.c - tracking custom allocators
- All examples build and run
- README explains each example

**Labels:** `documentation`, `examples`, `sprint-5`
**Cycle:** Sprint 5: Testing, Documentation \& Release
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-28: Stress Testing**

**Title:** Stress test with millions of allocations

**Description:**
Test debugger with extreme scenarios: millions of allocations, large memory sizes, long-running processes. Verify stability and memory usage.

**Acceptance Criteria:**

- Test with 10M+ allocations
- Test with allocations up to 1GB
- 24-hour stability test
- Verify no memory leaks in debugger
- Document maximum capacity

**Labels:** `testing`, `quality`, `sprint-5`
**Cycle:** Sprint 5: Testing, Documentation \& Release
**Priority:** P1 (High)
**Status:** Todo

***

### **MEMDEBUG-29: Installation \& Packaging**

**Title:** Create installation scripts and packages

**Description:**
Write install scripts, create Debian/RPM packages, and provide installation instructions for various platforms. Support system-wide and local installation.

**Acceptance Criteria:**

- `make install` works correctly
- Installation to /usr/local/lib
- Package scripts for deb/rpm
- Uninstall script provided
- Multi-platform support documented

**Labels:** `infrastructure`, `packaging`, `sprint-5`
**Cycle:** Sprint 5: Testing, Documentation \& Release
**Priority:** P2 (Medium)
**Status:** Todo

***

### **MEMDEBUG-30: Release v1.0.0**

**Title:** Prepare and publish first stable release

**Description:**
Finalize all features, ensure all tests pass, complete documentation, create GitHub release with binaries and source tarball. Write release notes.

**Acceptance Criteria:**

- All tests passing (100+ tests)
- Documentation complete
- CHANGELOG.md written
- GitHub release created
- Tagged as v1.0.0
- Release announcement prepared
- Binaries for Linux x86-64 provided

**Labels:** `release`, `milestone`, `sprint-5`
**Cycle:** Sprint 5: Testing, Documentation \& Release
**Priority:** P0 (Critical)
**Status:** Todo

***

## **SUMMARY**

**Total Sprints:** 5 (8 weeks total)
**Total Work Items:** 30

**Priority Breakdown:**

- P0 (Critical): 13 items
- P1 (High): 13 items
- P2 (Medium): 4 items

**Label Categories:**

- `core`: 11 items
- `testing`: 6 items
- `sprint-1` through `sprint-5`: Organized by cycle
- `documentation`: 4 items
- `reporting`: 5 items
- `configuration`: 4 items

**Work Distribution:**

- Sprint 1: 6 items (foundation)
- Sprint 2: 6 items (tracking)
- Sprint 3: 6 items (leak detection)
- Sprint 4: 6 items (tooling)
- Sprint 5: 6 items (release)

***

This structure can be directly imported into Plane. Each work item is self-contained with clear acceptance criteria, making it easy to track progress and ensure quality throughout development. Good luck with your implementation! 🚀

