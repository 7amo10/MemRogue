# Changelog

All notable changes to MemRogue will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2025-12-02

###  First Stable Release

This marks the first production-ready release of MemRogue, a lightweight memory debugging library for C/C++ applications. This release represents the culmination of 5 development sprints and 30 completed issues.

---

###  Features

#### Core Memory Tracking (Sprint 1)
- **Allocation Interception** - Transparent interception of `malloc`, `free`, `calloc`, `realloc` via `LD_PRELOAD`
- **High-Performance Hash Table** - Custom lock-free hash table for O(1) allocation tracking
- **Stack Trace Capture** - Full backtrace support with configurable depth (default: 16 frames)
- **Thread-Safe Design** - Mutex-protected operations for multi-threaded applications
- **Allocation Records** - Comprehensive metadata including size, timestamp, file, line, and function

#### Error Detection (Sprint 2)
- **Memory Leak Detection** - Automatic detection of unfreed allocations at program exit
- **Double-Free Detection** - Catches attempts to free already-freed memory
- **Invalid Free Detection** - Detects frees of non-allocated or corrupted pointers
- **Detailed Error Reporting** - Full context with allocation/deallocation stack traces

#### Reporting System (Sprint 3)
- **Multiple Output Formats** - Text (human-readable), JSON (machine-parseable), CSV (spreadsheet-compatible)
- **Configurable Reports** - Summary-only mode, full details, or custom filtering
- **Report Generator CLI** - `memrogue-report` tool for offline analysis
- **Leak Grouping** - Aggregates similar leaks by allocation site

#### Advanced Features (Sprint 4)
- **Configuration System** - Environment variables and programmatic configuration
- **Sampling Mode** - Configurable sampling rate (0.0-1.0) for reduced overhead
- **Exit Handler** - Automatic report generation on program termination
- **Python Analysis Script** - `analyze_leaks.py` for advanced leak analysis

#### Testing & Release (Sprint 5)
- **Comprehensive Test Suite** - 28+ automated tests covering all features
- **Integration Tests** - Real-world scenario testing
- **Stress Testing** - High-load performance validation
- **Multi-Distribution Support** - Tested on Ubuntu, Debian, Fedora, CentOS, Arch Linux
- **Package Distribution** - Debian (.deb) and RPM (.rpm) packaging support
- **Installation Scripts** - Easy `install.sh` with auto-detection

---

###  Installation Methods

```bash
# Quick install (system-wide)
sudo ./scripts/install.sh --system

# User-local install
./scripts/install.sh --user

# Using CMake
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build

# Using wrapper script (after install)
memrogue ./your_application
```

---

###  Components

| Component | Description |
|-----------|-------------|
| `libmemrogue_intercept.so` | LD_PRELOAD interception library |
| `libmemrogue_core.a` | Core tracking functionality (static library) |
| `memrogue-report` | CLI tool for report generation |
| `memrogue` | Wrapper script for easy usage |

---

###  Performance

| Metric | Value |
|--------|-------|
| Allocation Overhead | < 1μs per allocation |
| Memory Overhead | ~100 bytes per tracked allocation |
| Hash Table Lookup | O(1) average case |
| Thread Contention | Minimal (fine-grained locking) |

---

###  Test Coverage

- **28 Unit Tests** - Core functionality verification
- **5 Integration Tests** - End-to-end scenarios
- **Stress Tests** - High-load performance validation
- **CI/CD Pipeline** - Automated testing with GCC and Clang

---

###  Configuration Options

| Environment Variable | Description | Default |
|---------------------|-------------|---------|
| `MEMROGUE_OUTPUT_FILE` | Output file path | stderr |
| `MEMROGUE_OUTPUT_FORMAT` | Output format (text/json/csv) | text |
| `MEMROGUE_STACK_DEPTH` | Stack trace depth | 16 |
| `MEMROGUE_SAMPLING_RATE` | Sampling rate (0.0-1.0) | 1.0 |
| `MEMROGUE_DETECT_LEAKS` | Enable leak detection | 1 |
| `MEMROGUE_DETECT_DOUBLE_FREE` | Enable double-free detection | 1 |
| `MEMROGUE_DETECT_INVALID_FREE` | Enable invalid-free detection | 1 |

---

###  Known Limitations

- Linux only (glibc required)
- x86-64 architecture optimized
- Some system allocations may not be tracked (before library initialization)
- Static executables not supported (requires dynamic linking)

---

### 👥 Contributors

- **Ahmed Ashour** ([@7amo10](https://github.com/7amo10)) - Project Lead & Developer

---

### 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

---

### 🔗 Links

- **GitHub Repository**: https://github.com/7amo10/MemRogue
- **Documentation**: [docs/](docs/)
- **Issue Tracker**: https://github.com/7amo10/MemRogue/issues
- **Project Management**: [Plane](https://app.plane.so/auraeg/projects/d292351e-b614-4761-a9a7-07af913edab5/)

---

## Development History

### Sprint 5: Testing, Documentation & Release
- MEMRO-25: Integration Test Suite
- MEMRO-26: Documentation Completion
- MEMRO-27: Performance Benchmarks
- MEMRO-28: Stress Testing
- MEMRO-29: Package Distribution & Installation
- MEMRO-30: Release v1.0.0 ← **Current Release**

### Sprint 4: Advanced Features & Optimization
- MEMRO-19: Configuration System
- MEMRO-20: Sampling Mode
- MEMRO-21: Performance Optimization
- MEMRO-22: Memory Overhead Reduction
- MEMRO-23: Exit Handler Integration
- MEMRO-24: Report Formatting Options

### Sprint 3: Reporting & Analysis
- MEMRO-13: Text Report Generator
- MEMRO-14: JSON Export
- MEMRO-15: CSV Export
- MEMRO-16: Leak Grouping
- MEMRO-17: Report Filtering
- MEMRO-18: CLI Tool

### Sprint 2: Error Detection
- MEMRO-7: Memory Leak Detection
- MEMRO-8: Double-Free Detection
- MEMRO-9: Invalid Free Detection
- MEMRO-10: Error Reporting
- MEMRO-11: Thread Safety
- MEMRO-12: Exit Handler

### Sprint 1: Core Infrastructure
- MEMRO-1: Project Setup
- MEMRO-2: Hash Table Implementation
- MEMRO-3: Allocation Interception
- MEMRO-4: Stack Trace Capture
- MEMRO-5: Basic Tracking
- MEMRO-6: Thread Safety Foundation

---

*This changelog was generated for MemRogue v1.0.0 release on December 2, 2025.*
