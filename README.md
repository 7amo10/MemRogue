<div align="center">
<img src="./logo.svg" alt="MemRogue Logo"/>
<h1>MemRogue</h1>
<p><strong>A Lightweight, Production-Ready Memory Debugging Library for C/C++</strong></p>

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-11%2F17-green.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

[Quick Start](#quick-start) •
[Documentation](#documentation) •
[Installation](#installation) •
[Examples](#examples) •
[Contributing](#contributing)

</div>

---

## Overview

MemRogue is a high-performance memory debugging and leak detection library designed for C/C++ applications. It provides transparent interception of memory allocation functions, comprehensive leak detection, and detailed reporting with stack traces—all with minimal runtime overhead.

### Key Features

| Feature | Description |
|---------|-------------|
| 🔍 **Allocation Tracking** | Intercepts `malloc`, `free`, `calloc`, `realloc`, `new`, `delete` |
| 🚨 **Leak Detection** | Identifies unfreed memory with detailed allocation context |
| 📍 **Stack Traces** | Captures full backtraces for each allocation point |
| ⚡ **Double-Free Detection** | Catches use-after-free and double-free errors |
| ❌ **Invalid Free Detection** | Detects frees of non-allocated pointers |
| 📊 **Multiple Export Formats** | Text, JSON, CSV output for integration with analysis tools |
| 🎯 **Sampling Mode** | Configurable sampling to reduce overhead in production |
| 🔧 **Zero Code Changes** | Works via `LD_PRELOAD` without recompilation |
| 🧵 **Thread-Safe** | Full support for multi-threaded applications |

---

## Quick Start

### 1. Build MemRogue

```bash
git clone https://github.com/yourusername/memrogue.git
cd memrogue
mkdir build && cd build
cmake ..
make
```

### 2. Run Your Application with MemRogue

```bash
# Basic usage with LD_PRELOAD
LD_PRELOAD=./lib/libmemrogue.so ./your_application

# Enable verbose output
MEMROGUE_VERBOSITY=2 LD_PRELOAD=./lib/libmemrogue.so ./your_application

# Save report to file
MEMROGUE_OUTPUT=leaks.txt LD_PRELOAD=./lib/libmemrogue.so ./your_application
```

### 3. Analyze the Results

```bash
# Generate a detailed report
./bin/memrogue-report --format=text --output=report.txt

# Export to JSON for programmatic analysis
./bin/memrogue-report --format=json --output=report.json

# Export to CSV for spreadsheet analysis
./bin/memrogue-report --format=csv --output=report.csv
```

---

## Installation

### Prerequisites

- **Compiler**: GCC 7+ or Clang 6+ with C11/C17 support
- **Build System**: CMake 3.10+
- **Platform**: Linux (x86_64, ARM64)
- **Optional**: libunwind (for enhanced stack traces)

### Building from Source

```bash
# Clone the repository
git clone https://github.com/yourusername/memrogue.git
cd memrogue

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Run tests
make test

# Install (optional)
sudo make install
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Debug` | Build type: `Debug`, `Release`, `RelWithDebInfo` |
| `BUILD_TESTS` | `ON` | Build unit tests |
| `BUILD_EXAMPLES` | `ON` | Build example applications |
| `ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer/UBSan |

---

## Usage

### Method 1: LD_PRELOAD (Recommended)

The simplest way to use MemRogue is via `LD_PRELOAD`, which requires no changes to your application:

```bash
LD_PRELOAD=/path/to/libmemrogue.so ./your_application
```

### Method 2: Link at Compile Time

For tighter integration, link MemRogue directly:

```c
// your_app.c
#include <memrogue_tracker.h>
#include <memrogue_leak_detector.h>

int main() {
    // Initialize tracker
    MemrogueTracker* tracker = tracker_create();
    
    // Your application code...
    void* ptr = malloc(100);
    // ... use ptr ...
    free(ptr);
    
    // Check for leaks
    LeakReport* report = leak_detector_analyze(tracker);
    leak_report_print(report);
    
    // Cleanup
    leak_report_free(report);
    tracker_destroy(tracker);
    return 0;
}
```

Compile with:
```bash
gcc -o your_app your_app.c -lmemrogue_core -L/path/to/memrogue/lib -I/path/to/memrogue/include
```

---

## Configuration

MemRogue is configured entirely through environment variables, requiring no code changes:

### Core Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `MEMROGUE_ENABLED` | `1` | Enable/disable tracking (`0` or `1`) |
| `MEMROGUE_OUTPUT` | `stderr` | Output file path or `stderr`/`stdout` |
| `MEMROGUE_VERBOSITY` | `1` | Verbosity level (`0`=quiet, `1`=normal, `2`=verbose, `3`=debug) |
| `MEMROGUE_REPORT_ON_EXIT` | `1` | Generate report on program exit |

### Detection Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `MEMROGUE_DETECT_DOUBLE_FREE` | `1` | Enable double-free detection |
| `MEMROGUE_DETECT_INVALID_FREE` | `1` | Enable invalid-free detection |

### Stack Trace Settings

| Variable | Default | Description |
|----------|---------|-------------|
| `MEMROGUE_BACKTRACE` | `1` | Enable stack trace capture |
| `MEMROGUE_MAX_DEPTH` | `16` | Maximum stack trace depth |

### Performance Tuning

| Variable | Default | Description |
|----------|---------|-------------|
| `MEMROGUE_SAMPLE_RATE` | `100` | Sampling rate percentage (1-100) |
| `MEMROGUE_SAMPLING_MODE` | `random` | Sampling mode: `random` or `deterministic` |

### Example Configurations

**Development (Maximum Detail)**
```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=3
export MEMROGUE_BACKTRACE=1
export MEMROGUE_MAX_DEPTH=32
export MEMROGUE_SAMPLE_RATE=100
```

**Production (Minimal Overhead)**
```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=0
export MEMROGUE_BACKTRACE=0
export MEMROGUE_SAMPLE_RATE=10
export MEMROGUE_SAMPLING_MODE=random
```

---

## CLI Tool

MemRogue includes `memrogue-report` for analyzing and exporting memory reports:

```
Usage: memrogue-report [OPTIONS] [INPUT_FILE]

Options:
  -f, --format FORMAT    Output format: text, json, csv, summary (default: text)
  -o, --output FILE      Output file (default: stdout)
  -v, --verbose          Enable verbose output
  -q, --quiet            Suppress non-essential output
  -h, --help             Show this help message
  -V, --version          Show version information

Exit Codes:
  0   Success
  1   General error
  2   Usage/argument error
  3   I/O error
  4   Parse error
  5   Memory allocation error
  10  Leaks found (with --exit-on-leak)

Examples:
  memrogue-report --format=json -o report.json leaks.bin
  memrogue-report --format=csv --output=analysis.csv
  memrogue-report --format=summary --verbose
```

---

## Output Formats

### Text Format

Human-readable format ideal for development:

```
================================================================================
                           MEMROGUE LEAK REPORT
================================================================================

Summary:
  Total allocations tracked: 1,247
  Total deallocations:       1,244
  Memory leaks detected:     3
  Total leaked bytes:        2,048

--------------------------------------------------------------------------------
LEAK #1: 1024 bytes at 0x7f8b4c001000
--------------------------------------------------------------------------------
  Allocated at:
    #0  malloc (memrogue_intercept.c:45)
    #1  create_buffer (buffer.c:23)
    #2  initialize_system (main.c:156)
    #3  main (main.c:42)
```

### JSON Format

Structured format for programmatic analysis:

```json
{
  "version": "1.0",
  "summary": {
    "total_allocations": 1247,
    "total_deallocations": 1244,
    "leak_count": 3,
    "total_leaked_bytes": 2048
  },
  "leaks": [
    {
      "address": "0x7f8b4c001000",
      "size": 1024,
      "backtrace": [
        {"function": "malloc", "file": "memrogue_intercept.c", "line": 45},
        {"function": "create_buffer", "file": "buffer.c", "line": 23}
      ]
    }
  ]
}
```

### CSV Format

Tabular format for spreadsheet analysis (RFC 4180 compliant):

```csv
address,size,allocation_time,source_file,source_line,function_name,backtrace
0x7f8b4c001000,1024,1699876543.123456,buffer.c,23,create_buffer,"malloc;create_buffer;initialize_system;main"
0x7f8b4c002000,512,1699876543.234567,utils.c,89,allocate_string,"malloc;allocate_string;parse_config;main"
```

---

## Examples

### Basic Leak Detection

```c
#include <stdlib.h>

int main() {
    // This allocation will be reported as a leak
    char* leaked = malloc(100);
    
    // This allocation is properly freed
    char* not_leaked = malloc(200);
    free(not_leaked);
    
    return 0;
}
```

Run with:
```bash
LD_PRELOAD=./lib/libmemrogue.so ./basic_example
```

### Python Analysis Script

MemRogue includes a Python script for analyzing CSV exports:

```bash
# Generate CSV report
MEMROGUE_OUTPUT=leaks.csv LD_PRELOAD=./lib/libmemrogue.so ./your_app

# Analyze with Python
python3 examples/analyze_leaks.py leaks.csv
```

See [examples/analyze_leaks.py](examples/analyze_leaks.py) for the full script.

---

## Documentation

| Document | Description |
|----------|-------------|
| [USAGE.md](docs/USAGE.md) | Detailed usage guide and tutorials |
| [API.md](docs/API.md) | Complete API reference |
| [CONFIGURATION.md](docs/CONFIGURATION.md) | Configuration reference |
| [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Common issues and solutions |
| [Examples README](examples/README.md) | Example applications guide and usage |

---

## Project Structure

```
memrogue/
├── include/              # Public header files
│   ├── memrogue_tracker.h
│   ├── memrogue_leak_detector.h
│   ├── memrogue_config.h
│   ├── memrogue_report.h
│   ├── memrogue_json.h
│   ├── memrogue_csv.h
│   └── ...
├── src/                  # Source implementation
├── tests/                # Unit and integration tests
├── examples/             # Example applications
├── tools/                # CLI tools
└── docs/                 # Documentation
```

---

## Testing

MemRogue includes comprehensive tests:

```bash
cd build

# Run all tests
make test

# Run specific test
./bin/test_leak_detector

# Run with verbose output
ctest -V
```

---

## Performance

MemRogue is designed for minimal overhead:

| Mode | Overhead | Use Case |
|------|----------|----------|
| Full tracking | ~5-15% | Development, debugging |
| Sampling (10%) | ~1-3% | Testing, staging |
| Disabled | 0% | Production fallback |

---

## Contributing

Contributions are welcome! Please read our contributing guidelines:

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make your changes
4. Run tests: `make test`
5. Submit a pull request

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

- Inspired by Valgrind, ASan, and other memory debugging tools
- Built with modern C best practices

---

<div align="center">
<b>MemRogue</b> - Making Memory Bugs Visible
</div>
