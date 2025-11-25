# Based on the thread of ideas collection, which is attached in this space, I picked idea "26. Custom Memory Debugger - Tracks allocations and detects leaks."

- Give me a solid Analysis and Design for this project to implement it
- suggest the language and project architecture, and tell me which scale this project should be in the first phase of development, and future enhancements
- This project should be used in real use cases, not an unused implementation!
- Finally, This Project will be built incrementally as I want to add features per feature until I finish the first phase of it.

Based on your project selection and research into modern memory debugging approaches, here's a comprehensive analysis and design for your Custom Memory Debugger:

***

# **Custom Memory Debugger: Analysis \& Design Document**

## **1. Project Overview**

### **1.1 Problem Statement**

Memory leaks and improper memory management remain critical issues in systems programming, leading to:

- Application crashes and undefined behavior
- Performance degradation over time
- Resource exhaustion in long-running services
- Difficult-to-debug production issues


### **1.2 Solution Approach**

Build a **lightweight, production-ready memory debugging library** that intercepts memory allocation/deallocation calls, tracks allocations, and provides real-time leak detection and analysis capabilities.

***

## **2. Language \& Technology Selection**

### **2.1 Primary Implementation: C/C++**

**Why C/C++?**

- Direct control over memory management primitives
- Ability to intercept `malloc/free/new/delete` through various techniques
- Zero-overhead abstractions when needed
- Most real-world memory issues occur in C/C++ codebases
- Performance-critical applications require minimal instrumentation overhead

**Alternative consideration:** Rust (for future versions with memory-safe implementation)

### **2.2 Interception Techniques**

1. **Library Preloading (`LD_PRELOAD`)** - Primary method for Unix/Linux
2. **Compile-time Instrumentation** - Using compiler hooks/attributes
3. **Link-time Wrapping** - Using `--wrap` linker flags

***

## **3. Architecture Design**

### **3.1 Core Components**

```
┌─────────────────────────────────────────────────┐
│          Application Code                        │
│  (malloc, free, new, delete calls)              │
└──────────────┬──────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────┐
│     Interception Layer (Hooks)                   │
│  - malloc_wrapper()                              │
│  - free_wrapper()                                │
│  - new/delete operators                          │
└──────────────┬──────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────┐
│     Allocation Tracker (Core Engine)             │
│  - Hash table for active allocations             │
│  - Stack trace capture                           │
│  - Metadata storage                              │
└──────────────┬──────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────┐
│     Analysis Engine                              │
│  - Leak detection algorithms                     │
│  - Memory usage statistics                       │
│  - Fragmentation analysis                        │
└──────────────┬──────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────┐
│     Reporting Interface                          │
│  - CLI output formatter                          │
│  - JSON/CSV export                               │
│  - Real-time monitoring hooks                    │
└─────────────────────────────────────────────────┘
```


### **3.2 Data Structures**

**Allocation Record:**

```c
typedef struct allocation_info {
    void* address;              // Allocated memory address
    size_t size;               // Allocation size in bytes
    uint64_t timestamp;        // Allocation timestamp
    void* backtrace[^1_16];       // Stack trace at allocation
    int backtrace_size;        // Number of stack frames
    const char* tag;           // Optional user tag
    struct allocation_info* next;  // For hash collision chaining
} allocation_info_t;
```

**Tracker State:**

```c
typedef struct memory_tracker {
    allocation_info_t** table;     // Hash table
    size_t table_size;             // Hash table size
    size_t total_allocations;      // Lifetime allocation count
    size_t current_allocations;    // Active allocations
    size_t peak_memory;            // Peak memory usage
    size_t current_memory;         // Current memory usage
    pthread_mutex_t lock;          // Thread-safety lock
} memory_tracker_t;
```


***

## **4. Phase 1: Minimal Viable Product (MVP)**

### **4.1 Scope \& Timeline: 6-8 weeks**

### **4.2 Core Features**

#### **Feature 1: Basic Allocation Tracking** (Week 1-2)

- Intercept `malloc`, `calloc`, `realloc`, `free`
- Store allocation metadata (address, size, timestamp)
- Implement hash table for O(1) lookup
- Thread-safe operations using mutexes

**Deliverable:** Library that can be preloaded to track allocations

#### **Feature 2: Stack Trace Capture** (Week 2-3)

- Capture call stack at allocation time using `backtrace()`
- Store function names and line numbers
- Implement symbol resolution using `backtrace_symbols()`

**Deliverable:** Allocations with full stack trace information

#### **Feature 3: Leak Detection** (Week 3-4)

- Identify unfreed allocations at program exit
- Detect double-free attempts
- Report invalid free operations

**Deliverable:** Leak report generation at exit

#### **Feature 4: CLI Reporting Tool** (Week 4-5)

- Summary statistics (total leaks, memory usage)
- Grouped leak reports by allocation site
- Human-readable output format

**Deliverable:** `memdebug-report` CLI tool

#### **Feature 5: Configuration System** (Week 5-6)

- Environment variable configuration
- Enable/disable features at runtime
- Sampling mode (track every Nth allocation)
- Output verbosity levels

**Deliverable:** Flexible configuration via `MEMDEBUG_*` env vars

#### **Feature 6: Documentation \& Testing** (Week 6-8)

- User documentation and examples
- Test suite with known leak scenarios
- Performance benchmarks
- Integration examples

**Deliverable:** Complete documentation and test coverage

### **4.3 MVP Success Criteria**

✅ Successfully detects memory leaks with <5% performance overhead
✅ Accurate stack trace reporting
✅ Thread-safe operation in multi-threaded applications
✅ Zero false positives in test suite
✅ Clear, actionable reports

***

## **5. Real-World Use Cases**

### **5.1 Development Phase**

- **Local Testing:** Developers run applications with debugger enabled
- **CI/CD Integration:** Automated leak detection in test pipelines
- **Code Review:** Verify memory management before merging


### **5.2 Production Monitoring**

- **Sampling Mode:** Low-overhead monitoring (1% sampling rate)
- **Periodic Reports:** Daily/weekly leak analysis
- **Alert Integration:** Trigger alerts when leaks detected


### **5.3 Practical Applications**

1. **Web Servers:** Detect leaks in long-running HTTP servers (nginx modules, custom servers)
2. **Database Engines:** Track memory in query executors and storage engines
3. **Game Engines:** Monitor memory during gameplay sessions
4. **Embedded Systems:** Lightweight tracking for resource-constrained devices
5. **System Utilities:** Debug custom allocators in OS components

***

## **6. Implementation Strategy**

### **6.1 Incremental Development Plan**

**Sprint 1 (Weeks 1-2): Foundation**

```
Tasks:
- Set up build system (CMake/Meson)
- Implement basic malloc/free interception
- Create hash table for allocation tracking
- Add thread synchronization primitives
- Write unit tests for core data structures

Deliverable: Can intercept and log allocations
```

**Sprint 2 (Weeks 2-3): Stack Traces**

```
Tasks:
- Integrate backtrace() functionality
- Implement symbol resolution
- Handle edge cases (stripped binaries)
- Add frame filtering (skip debugger frames)
- Test with various optimization levels

Deliverable: Full call stack for each allocation
```

**Sprint 3 (Weeks 3-4): Leak Detection**

```
Tasks:
- Implement exit handler for leak reporting
- Detect double-free scenarios
- Add invalid free detection
- Group leaks by allocation site
- Calculate leak statistics

Deliverable: Comprehensive leak reports
```

**Sprint 4 (Weeks 4-5): Reporting**

```
Tasks:
- Design output format (text/JSON)
- Implement CLI tool for report generation
- Add filtering and sorting options
- Create summary statistics
- Generate per-file leak breakdown

Deliverable: Professional reporting tool
```

**Sprint 5 (Weeks 5-6): Configuration**

```
Tasks:
- Environment variable parsing
- Sampling mode implementation
- Runtime enable/disable toggles
- Output destination configuration
- Performance profiling

Deliverable: Configurable debugger library
```

**Sprint 6 (Weeks 6-8): Polish \& Release**

```
Tasks:
- Write comprehensive documentation
- Create example projects
- Build test suite (100+ test cases)
- Performance optimization
- Package for distribution

Deliverable: Production-ready v1.0
```


### **6.2 Development Workflow**

```
For each feature:
1. Write design doc (30 min)
2. Implement core logic (4-8 hours)
3. Add unit tests (2-4 hours)
4. Integration testing (2 hours)
5. Documentation (1 hour)
6. Code review & refinement (1-2 hours)
```


***

## **7. Future Enhancements (Post-Phase 1)**

### **7.1 Phase 2: Advanced Features** (2-3 months)

- **C++ Support:** New/delete operators, smart pointer tracking
- **Memory Pattern Analysis:** Detect fragmentation patterns
- **Heap Visualization:** Generate memory layout diagrams
- **Custom Allocator Support:** Track pool allocators and arenas
- **Real-time Dashboard:** Web-based monitoring interface


### **7.2 Phase 3: Production Hardening** (2-3 months)

- **Minimal Overhead Mode:** <1% performance impact
- **Distributed Tracing:** Integration with OpenTelemetry
- **Machine Learning:** Anomaly detection for leak patterns
- **Kernel Integration:** Track kernel-level allocations
- **Platform Expansion:** Windows support (Detours API)


### **7.3 Phase 4: Ecosystem** (3-6 months)

- **IDE Plugins:** VSCode, CLion integration
- **Language Bindings:** Python, Ruby, Node.js wrappers
- **Cloud Integration:** AWS Lambda, Kubernetes support
- **Commercial Features:** Enterprise reporting, compliance tools

***

## **8. Technical Specifications**

### **8.1 Performance Targets**

- **Overhead:** <5% slowdown in typical applications
- **Memory:** <10MB additional memory usage
- **Latency:** <100μs per allocation in tracking mode
- **Throughput:** Handle 1M+ allocations/second


### **8.2 Compatibility**

- **OS:** Linux (primary), macOS, *BSD
- **Compilers:** GCC 7+, Clang 10+, ICC
- **Architectures:** x86-64, ARM64, RISC-V
- **Standards:** C11, C++17


### **8.3 Dependencies**

- **Minimal:** Only POSIX APIs and libc
- **Optional:** libunwind (better stack traces), libdw (DWARF debugging)

***

## **9. Project Structure**

```
memdebug/
├── src/
│   ├── core/
│   │   ├── tracker.c          # Core tracking engine
│   │   ├── hashtable.c        # Hash table implementation
│   │   └── allocator.c        # Allocation hooks
│   ├── backtrace/
│   │   ├── capture.c          # Stack capture
│   │   └── resolve.c          # Symbol resolution
│   ├── analysis/
│   │   ├── leaks.c            # Leak detection
│   │   └── stats.c            # Statistics generation
│   └── report/
│       ├── formatter.c        # Output formatting
│       └── exporter.c         # Data export
├── include/
│   └── memdebug.h             # Public API
├── tools/
│   └── memdebug-report.c      # CLI tool
├── tests/
│   ├── unit/                  # Unit tests
│   └── integration/           # Integration tests
├── examples/
│   ├── basic_leak.c           # Simple example
│   └── multithreaded.c        # Threading example
├── docs/
│   ├── API.md                 # API documentation
│   ├── USAGE.md               # Usage guide
│   └── INTERNALS.md           # Implementation details
├── CMakeLists.txt             # Build configuration
└── README.md                  # Project overview
```


***

## **10. Getting Started (Quick Start for Users)**

### **10.1 Installation**

```bash
# Build from source
git clone https://github.com/yourteam/memdebug.git
cd memdebug
mkdir build && cd build
cmake ..
make
sudo make install
```


### **10.2 Basic Usage**

```bash
# Preload the library
LD_PRELOAD=/usr/local/lib/libmemdebug.so ./your_app

# Generate report
memdebug-report /tmp/memdebug-*.log
```


### **10.3 Configuration**

```bash
# Enable full tracking
export MEMDEBUG_ENABLED=1

# Set sampling rate (track 10% of allocations)
export MEMDEBUG_SAMPLE_RATE=10

# Output file
export MEMDEBUG_OUTPUT=/tmp/memdebug.log

# Enable stack traces
export MEMDEBUG_BACKTRACE=1
```


***

## **11. Success Metrics**

### **11.1 Technical Metrics**

- ✅ 100% test coverage for core functions
- ✅ Zero memory leaks in debugger itself
- ✅ <5% performance overhead
- ✅ Support for 1M+ simultaneous tracked allocations


### **11.2 Adoption Metrics**

- 🎯 Used in at least 3 internal projects
- 🎯 10+ GitHub stars within first month
- 🎯 Positive feedback from team members
- 🎯 Integration into CI/CD pipelines

***

## **12. Risk Mitigation**

### **12.1 Technical Risks**

| Risk | Impact | Mitigation |
| :-- | :-- | :-- |
| Performance overhead too high | High | Implement sampling mode, lazy initialization |
| Thread safety issues | Critical | Comprehensive locking strategy, stress testing |
| Platform compatibility | Medium | Focus on Linux first, test on multiple distros |
| False positives | High | Rigorous testing, conservative leak detection |

### **12.2 Project Risks**

| Risk | Impact | Mitigation |
| :-- | :-- | :-- |
| Feature creep | Medium | Strict scope for Phase 1, defer enhancements |
| Time overruns | Medium | Weekly progress reviews, buffer in timeline |
| Lack of adoption | Low | Early demos, clear documentation |


***

## **13. Conclusion**

This Custom Memory Debugger project provides:
✅ **Practical value:** Solves real memory management problems
✅ **Learning opportunity:** Deep dive into systems programming
✅ **Incremental delivery:** Clear weekly milestones
✅ **Production readiness:** Designed for real-world use
✅ **Future potential:** Clear roadmap for enhancements

**Start Date:** February 2025
**Phase 1 Completion:** Mid-March 2025 (6-8 weeks)
**First Real Usage:** Week 9 (dogfooding in team projects)

The project strikes the perfect balance between complexity and achievability, providing immediate value while building toward a comprehensive memory debugging solution.

***


---

# Great, Remake the Architecture Design for Components, Sequence and Class Diagrams in .puml codes.

Here are comprehensive PlantUML diagrams for the Custom Memory Debugger architecture:

***

## **1. Component Diagram**

```plantuml
@startuml Memory_Debugger_Components

!define COMPONENT_COLOR #4A90E2
!define DATABASE_COLOR #50C878
!define INTERFACE_COLOR #F5A623

skinparam component {
    BackgroundColor COMPONENT_COLOR
    BorderColor #2E5C8A
    FontColor white
}

skinparam database {
    BackgroundColor DATABASE_COLOR
    BorderColor #2E8B57
}

skinparam interface {
    BackgroundColor INTERFACE_COLOR
    BorderColor #D68910
}

package "Application Layer" {
    [User Application] as App
    note right of App
        Your C/C++ application
        with malloc/free calls
    end note
}

package "Interception Layer" {
    interface "Memory API" as MemAPI
    [malloc_wrapper] as MallocW
    [free_wrapper] as FreeW
    [realloc_wrapper] as ReallocW
    [calloc_wrapper] as CallocW
    
    note right of MemAPI
        Standard POSIX
        memory functions
    end note
}

package "Core Tracking Engine" {
    [Allocation Tracker] as Tracker
    [Hash Table Manager] as HashMgr
    [Thread Safety Layer] as ThreadSafe
    database "Allocation Records" as AllocDB
    
    note right of Tracker
        Central coordination
        for all tracking ops
    end note
}

package "Stack Trace Module" {
    [Backtrace Capture] as BTrace
    [Symbol Resolver] as SymResolver
    [Frame Filter] as FrameFilter
}

package "Analysis Engine" {
    [Leak Detector] as LeakDet
    [Statistics Calculator] as Stats
    [Memory Profiler] as Profiler
    [Pattern Analyzer] as Pattern
}

package "Reporting System" {
    [Report Generator] as ReportGen
    [CLI Tool] as CLI
    [Export Manager] as Export
    interface "Output Formats" as Formats
    
    Formats -- [Text Formatter]
    Formats -- [JSON Formatter]
    Formats -- [CSV Formatter]
}

package "Configuration System" {
    [Environment Parser] as EnvParser
    [Config Manager] as ConfigMgr
    database "Runtime Config" as ConfigDB
}

package "System Interface" {
    [System Allocator] as SysAlloc
    [Signal Handlers] as Signals
    [Exit Hooks] as ExitHooks
}

' Connections
App --> MemAPI : "calls memory functions"
MemAPI --> MallocW
MemAPI --> FreeW
MemAPI --> ReallocW
MemAPI --> CallocW

MallocW --> Tracker : "record allocation"
FreeW --> Tracker : "record deallocation"
ReallocW --> Tracker : "update allocation"
CallocW --> Tracker : "record allocation"

Tracker --> ThreadSafe : "acquire lock"
Tracker --> HashMgr : "store/retrieve"
HashMgr --> AllocDB : "persist data"

Tracker --> BTrace : "capture stack"
BTrace --> SymResolver : "resolve symbols"
BTrace --> FrameFilter : "filter frames"

Tracker --> SysAlloc : "actual allocation"

ExitHooks --> LeakDet : "trigger analysis"
LeakDet --> AllocDB : "scan unfreed"
LeakDet --> Stats : "calculate metrics"
Stats --> Profiler : "profile data"

ReportGen --> LeakDet : "get leak data"
ReportGen --> Stats : "get statistics"
ReportGen --> Formats : "format output"

CLI --> ReportGen : "request report"
Export --> Formats : "export data"

EnvParser --> ConfigMgr : "parse vars"
ConfigMgr --> ConfigDB : "store config"
Tracker --> ConfigMgr : "read config"

Signals --> ExitHooks : "handle termination"

@enduml
```


***

## **2. Sequence Diagram: Memory Allocation Flow**

```plantuml
@startuml Memory_Allocation_Sequence

skinparam sequenceArrowThickness 2
skinparam roundcorner 10
skinparam maxmessagesize 200
skinparam sequenceParticipant underline

actor "User Application" as App
participant "malloc_wrapper" as Malloc
participant "Config Manager" as Config
participant "Thread Lock" as Lock
participant "Backtrace Capture" as BTrace
participant "Symbol Resolver" as Resolver
participant "Hash Table" as Hash
participant "Allocation Tracker" as Tracker
participant "System malloc" as SysMalloc
participant "Statistics" as Stats

== Allocation Request ==

App -> Malloc: malloc(1024)
activate Malloc

Malloc -> Config: is_enabled()
activate Config
Config --> Malloc: true
deactivate Config

Malloc -> Config: should_track()
activate Config
note right
    Check sampling rate,
    filters, etc.
end note
Config --> Malloc: true
deactivate Config

Malloc -> Lock: acquire_lock()
activate Lock
note right
    Ensure thread-safety
    for tracking operations
end note

Malloc -> SysMalloc: malloc(1024)
activate SysMalloc
SysMalloc --> Malloc: ptr=0x7fff1234
deactivate SysMalloc

alt Allocation Successful
    Malloc -> BTrace: capture_backtrace()
    activate BTrace
    
    BTrace -> BTrace: backtrace(frames, 16)
    BTrace -> Resolver: resolve_symbols(frames)
    activate Resolver
    
    Resolver -> Resolver: backtrace_symbols()
    note right
        Convert addresses to
        function names/files
    end note
    Resolver --> BTrace: symbol_info[]
    deactivate Resolver
    
    BTrace --> Malloc: stack_trace
    deactivate BTrace
    
    Malloc -> Tracker: create_allocation_record()
    activate Tracker
    
    Tracker -> Tracker: allocate_metadata()
    note right
        allocation_info_t {
            address: 0x7fff1234
            size: 1024
            timestamp: 1732558800
            backtrace: [...]
        }
    end note
    
    Tracker -> Hash: insert(ptr, record)
    activate Hash
    
    Hash -> Hash: hash_function(ptr)
    Hash -> Hash: handle_collision()
    Hash --> Tracker: success
    deactivate Hash
    
    Tracker -> Stats: update_stats()
    activate Stats
    Stats -> Stats: total_allocations++
    Stats -> Stats: current_memory += size
    Stats -> Stats: update_peak_memory()
    Stats --> Tracker: updated
    deactivate Stats
    
    Tracker --> Malloc: record_created
    deactivate Tracker
    
else Allocation Failed
    Malloc -> Malloc: log_error()
    note right
        Handle allocation failure
        without tracking
    end note
end

Malloc -> Lock: release_lock()
deactivate Lock

Malloc --> App: ptr=0x7fff1234
deactivate Malloc

== Later: User Uses Memory ==
App -> App: strcpy(ptr, "data")

@enduml
```


***

## **3. Sequence Diagram: Memory Deallocation Flow**

```plantuml
@startuml Memory_Deallocation_Sequence

skinparam sequenceArrowThickness 2
skinparam roundcorner 10
skinparam maxmessagesize 200

actor "User Application" as App
participant "free_wrapper" as Free
participant "Thread Lock" as Lock
participant "Hash Table" as Hash
participant "Allocation Tracker" as Tracker
participant "Leak Detector" as LeakDet
participant "Statistics" as Stats
participant "System free" as SysFree

== Deallocation Request ==

App -> Free: free(0x7fff1234)
activate Free

Free -> Lock: acquire_lock()
activate Lock

Free -> Hash: lookup(0x7fff1234)
activate Hash

Hash -> Hash: compute_hash(0x7fff1234)
Hash -> Hash: search_bucket()

alt Allocation Found
    Hash --> Free: allocation_record
    deactivate Hash
    
    Free -> Tracker: validate_allocation()
    activate Tracker
    
    Tracker -> Tracker: check_double_free()
    Tracker -> Tracker: check_corruption()
    
    alt Valid Deallocation
        Tracker --> Free: valid
        deactivate Tracker
        
        Free -> Hash: remove(0x7fff1234)
        activate Hash
        Hash -> Hash: unlink_from_chain()
        Hash --> Free: removed
        deactivate Hash
        
        Free -> Stats: update_stats()
        activate Stats
        Stats -> Stats: current_allocations--
        Stats -> Stats: current_memory -= size
        Stats -> Stats: total_deallocations++
        Stats --> Free: updated
        deactivate Stats
        
        Free -> SysFree: free(0x7fff1234)
        activate SysFree
        SysFree --> Free: freed
        deactivate SysFree
        
        Free -> Lock: release_lock()
        deactivate Lock
        
        Free --> App: success
        
    else Double Free Detected
        Tracker --> Free: ERROR: double_free
        deactivate Tracker
        
        Free -> LeakDet: report_double_free()
        activate LeakDet
        LeakDet -> LeakDet: log_violation()
        LeakDet -> LeakDet: generate_backtrace()
        LeakDet --> Free: reported
        deactivate LeakDet
        
        Free -> Lock: release_lock()
        deactivate Lock
        
        Free --> App: ERROR
        note right
            Optionally abort()
            based on config
        end note
    end
    
else Allocation Not Found
    Hash --> Free: NULL
    deactivate Hash
    
    Free -> LeakDet: report_invalid_free()
    activate LeakDet
    
    LeakDet -> LeakDet: log_violation()
    note right
        Attempt to free pointer
        that was never allocated
        or already freed
    end note
    
    LeakDet --> Free: reported
    deactivate LeakDet
    
    Free -> Lock: release_lock()
    deactivate Lock
    
    Free --> App: ERROR
end

deactivate Free

@enduml
```


***

## **4. Sequence Diagram: Leak Detection at Exit**

```plantuml
@startuml Leak_Detection_Sequence

skinparam sequenceArrowThickness 2
skinparam roundcorner 10
skinparam maxmessagesize 200

participant "Exit Hook" as Exit
participant "Leak Detector" as LeakDet
participant "Hash Table" as Hash
participant "Allocation Tracker" as Tracker
participant "Statistics" as Stats
participant "Report Generator" as Report
participant "Output Formatter" as Format
participant "File System" as FS

== Program Termination ==

activate Exit
Exit -> LeakDet: detect_leaks()
activate LeakDet

LeakDet -> Hash: get_all_allocations()
activate Hash

Hash -> Hash: iterate_all_buckets()

loop For Each Bucket
    Hash -> Hash: traverse_chain()
end

Hash --> LeakDet: active_allocations[]
deactivate Hash

LeakDet -> LeakDet: filter_false_positives()
note right
    Skip known globals,
    static allocations, etc.
end note

LeakDet -> LeakDet: group_by_allocation_site()
note right
    Group leaks by
    backtrace signature
end note

LeakDet -> Stats: calculate_leak_stats()
activate Stats

Stats -> Stats: total_leaked_bytes = sum(sizes)
Stats -> Stats: total_leaked_blocks = count()
Stats -> Stats: unique_leak_sites = unique(backtraces)

Stats --> LeakDet: leak_statistics
deactivate Stats

LeakDet -> Tracker: get_lifetime_stats()
activate Tracker

Tracker --> LeakDet: lifetime_statistics
deactivate Tracker

LeakDet --> Exit: leak_data
deactivate LeakDet

Exit -> Report: generate_report(leak_data)
activate Report

Report -> Report: create_summary()
Report -> Report: create_detailed_leaks()

loop For Each Leak Group
    Report -> Report: format_leak_entry()
    Report -> Report: format_backtrace()
    Report -> Report: calculate_percentages()
end

Report -> Format: format_text(report)
activate Format

Format -> Format: create_header()
Format -> Format: format_statistics()
Format -> Format: format_leak_list()
Format -> Format: create_footer()

Format --> Report: formatted_text
deactivate Format

Report -> FS: write_to_file("/tmp/memdebug.log")
activate FS
FS --> Report: written
deactivate FS

Report -> Report: print_to_stderr()
note right
    Optional: also print
    summary to console
end note

Report --> Exit: report_generated
deactivate Report

Exit -> Exit: cleanup_debugger()
note right
    Free internal structures,
    release resources
end note

deactivate Exit

@enduml
```


***

## **5. Class Diagram**

```plantuml
@startuml Memory_Debugger_Classes

skinparam classAttributeIconSize 0
skinparam class {
    BackgroundColor #E8F4F8
    BorderColor #2E5C8A
    ArrowColor #4A90E2
}

' Core Data Structures
class allocation_info_t {
    - void* address
    - size_t size
    - uint64_t timestamp
    - void* backtrace[16]
    - int backtrace_size
    - char* tag
    - allocation_info_t* next
    --
    + allocation_info_t* create(address, size)
    + void destroy(allocation_info_t*)
    + void set_backtrace(void** frames, int count)
    + void set_tag(const char* tag)
}

class memory_tracker_t {
    - allocation_info_t** table
    - size_t table_size
    - size_t total_allocations
    - size_t current_allocations
    - size_t peak_memory
    - size_t current_memory
    - size_t total_deallocations
    - pthread_mutex_t lock
    --
    + memory_tracker_t* initialize(size_t table_size)
    + void shutdown(memory_tracker_t*)
    + bool track_allocation(void* ptr, size_t size)
    + bool track_deallocation(void* ptr)
    + allocation_info_t* lookup(void* ptr)
    + void lock()
    + void unlock()
}

class hash_table_t {
    - allocation_info_t** buckets
    - size_t bucket_count
    - size_t item_count
    - float load_factor
    --
    + hash_table_t* create(size_t size)
    + void destroy(hash_table_t*)
    + bool insert(void* key, allocation_info_t* value)
    + allocation_info_t* search(void* key)
    + bool remove(void* key)
    + size_t hash_function(void* key)
    + void resize()
    + allocation_info_t** get_all_entries()
}

class backtrace_info_t {
    - void* frames[32]
    - int frame_count
    - char** symbols
    - bool resolved
    --
    + backtrace_info_t* capture()
    + void resolve_symbols(backtrace_info_t*)
    + char* format_frame(int index)
    + void filter_internal_frames()
    + void destroy(backtrace_info_t*)
}

class statistics_t {
    - size_t total_allocations
    - size_t total_deallocations
    - size_t current_allocations
    - size_t peak_allocations
    - size_t total_bytes_allocated
    - size_t current_bytes
    - size_t peak_bytes
    - double average_allocation_size
    --
    + void update_on_alloc(size_t size)
    + void update_on_free(size_t size)
    + void calculate_statistics()
    + char* format_summary()
    + void reset()
}

class leak_report_t {
    - leak_group_t** groups
    - size_t group_count
    - statistics_t* stats
    - uint64_t timestamp
    --
    + leak_report_t* create()
    + void add_leak_group(leak_group_t*)
    + void sort_by_size()
    + void sort_by_count()
    + char* generate_text_report()
    + char* generate_json_report()
    + void destroy(leak_report_t*)
}

class leak_group_t {
    - allocation_info_t** allocations
    - size_t allocation_count
    - size_t total_bytes
    - backtrace_info_t* representative_trace
    - char* allocation_site
    --
    + leak_group_t* create(backtrace_info_t*)
    + void add_allocation(allocation_info_t*)
    + void calculate_totals()
    + bool matches_trace(backtrace_info_t*)
    + void destroy(leak_group_t*)
}

' Configuration
class config_t {
    - bool enabled
    - bool track_backtraces
    - int sampling_rate
    - size_t min_track_size
    - size_t max_track_size
    - char* output_file
    - int verbosity
    - bool detect_double_free
    --
    + config_t* load_from_env()
    + bool should_track_allocation(size_t size)
    + bool is_sampling_enabled()
    + void set_option(const char* key, const char* value)
    + void print_config()
}

' Interception Layer
class memory_hooks_t {
    - void* (*original_malloc)(size_t)
    - void (*original_free)(void*)
    - void* (*original_realloc)(void*, size_t)
    - void* (*original_calloc)(size_t, size_t)
    --
    + void install_hooks()
    + void remove_hooks()
    + void* hooked_malloc(size_t size)
    + void hooked_free(void* ptr)
    + void* hooked_realloc(void* ptr, size_t size)
    + void* hooked_calloc(size_t num, size_t size)
}

' Analysis Engine
class leak_detector_t {
    - memory_tracker_t* tracker
    - config_t* config
    --
    + leak_detector_t* create(memory_tracker_t*)
    + leak_report_t* detect_leaks()
    + bool is_double_free(void* ptr)
    + bool is_invalid_free(void* ptr)
    + void report_violation(const char* type, void* ptr)
}

class pattern_analyzer_t {
    - allocation_info_t** recent_allocations
    - size_t window_size
    --
    + void analyze_patterns()
    + bool detect_fragmentation()
    + double calculate_fragmentation_ratio()
    + void identify_hotspots()
    + char* generate_pattern_report()
}

' Reporting
class report_formatter_t {
    --
    + {static} char* format_text(leak_report_t*)
    + {static} char* format_json(leak_report_t*)
    + {static} char* format_csv(leak_report_t*)
    + {static} char* format_html(leak_report_t*)
    + {static} void write_to_file(const char* filename, const char* content)
}

class cli_tool_t {
    - config_t* config
    - char** input_files
    - int file_count
    --
    + int main(int argc, char** argv)
    + void parse_arguments(int argc, char** argv)
    + void process_log_file(const char* filename)
    + void print_usage()
}

' Relationships
memory_tracker_t *-- hash_table_t : "uses"
memory_tracker_t *-- statistics_t : "maintains"
memory_tracker_t *-- config_t : "reads"

hash_table_t "1" *-- "*" allocation_info_t : "stores"
allocation_info_t *-- backtrace_info_t : "contains"

leak_detector_t --> memory_tracker_t : "queries"
leak_detector_t --> leak_report_t : "generates"

leak_report_t "1" *-- "*" leak_group_t : "contains"
leak_group_t "1" *-- "*" allocation_info_t : "groups"

memory_hooks_t --> memory_tracker_t : "calls"

report_formatter_t ..> leak_report_t : "formats"
cli_tool_t --> report_formatter_t : "uses"

pattern_analyzer_t --> memory_tracker_t : "analyzes"

@enduml
```


***

## **6. State Diagram: Allocation Lifecycle**

```plantuml
@startuml Allocation_State_Diagram

[*] --> Unallocated

Unallocated --> Allocating : malloc(size)

state Allocating {
    [*] --> RequestingMemory
    RequestingMemory --> CreatingMetadata : allocation succeeded
    RequestingMemory --> [*] : allocation failed
    CreatingMetadata --> CapturingBacktrace
    CapturingBacktrace --> InsertingRecord
    InsertingRecord --> [*]
}

Allocating --> Tracked : record created

state Tracked {
    [*] --> Active
    Active --> Active : pointer in use
    Active --> BeingModified : realloc() called
    BeingModified --> Active : reallocation succeeded
    BeingModified --> [*] : reallocation failed
}

Tracked --> Deallocating : free(ptr)

state Deallocating {
    [*] --> ValidatingPointer
    ValidatingPointer --> RemovingRecord : valid pointer
    ValidatingPointer --> ReportingError : invalid pointer
    RemovingRecord --> ReleasingMemory
    ReleasingMemory --> [*]
    ReportingError --> [*]
}

Deallocating --> Unallocated : freed successfully
Deallocating --> Error : double free / invalid free

Tracked --> Leaked : program exit without free()

state Leaked {
    [*] --> Detected
    Detected --> Reported : leak detection runs
    Reported --> [*]
}

Error --> [*]
Leaked --> [*]

@enduml
```


***

## **7. Activity Diagram: Complete Tracking Workflow**

```plantuml
@startuml Tracking_Workflow

start

:Application calls malloc();

if (Debugger enabled?) then (yes)
    :Intercept allocation;
    
    if (Should track? (sampling)) then (yes)
        :Acquire thread lock;
        
        :Call system malloc();
        
        if (Allocation successful?) then (yes)
            :Capture backtrace;
            
            fork
                :Resolve symbols;
            fork again
                :Create allocation record;
            end fork
            
            :Insert into hash table;
            :Update statistics;
            :Release thread lock;
            :Return pointer to app;
        else (no)
            :Release thread lock;
            :Return NULL;
        endif
    else (no)
        :Call system malloc directly;
        :Return pointer;
    endif
else (no)
    :Call system malloc directly;
    :Return pointer;
endif

:Application uses memory;

:Application calls free();

if (Debugger enabled?) then (yes)
    :Intercept deallocation;
    :Acquire thread lock;
    
    :Lookup pointer in hash table;
    
    if (Pointer found?) then (yes)
        if (Already freed?) then (yes)
            :Report double free error;
            :Release lock;
            stop
        else (no)
            :Remove from hash table;
            :Update statistics;
            :Call system free();
            :Release lock;
        endif
    else (no)
        :Report invalid free;
        :Release lock;
        stop
    endif
else (no)
    :Call system free directly;
endif

stop

@enduml
```


***

These PlantUML diagrams provide a complete visual representation of your Custom Memory Debugger architecture. You can render them using:

1. **Online:** [PlantUML Online Editor](http://www.plantuml.com/plantuml/)
2. **IDE Plugins:** VSCode PlantUML extension, IntelliJ IDEA PlantUML integration
3. **CLI:** `plantuml diagram.puml` (requires Java + PlantUML jar)
4. **Documentation:** Integrate directly into Markdown with rendering tools

Each diagram serves a specific purpose:

- **Component:** System architecture overview
- **Sequence (3 variants):** Detailed runtime behavior
- **Class:** Code structure and relationships
- **State:** Allocation lifecycle
- **Activity:** End-to-end workflow

Ready to start implementing Sprint 1? 🚀

