# MemRogue Usage Guide

This guide provides detailed instructions for using MemRogue in your development workflow.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Basic Usage](#basic-usage)
3. [Integration Methods](#integration-methods)
4. [Working with Reports](#working-with-reports)
5. [Advanced Usage](#advanced-usage)
6. [CI/CD Integration](#cicd-integration)
7. [Best Practices](#best-practices)

---

## Getting Started

### System Requirements

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| Operating System | Linux (kernel 3.10+) | Linux (kernel 5.0+) |
| Architecture | x86_64, ARM64 | x86_64 |
| Compiler | GCC 7.0 / Clang 6.0 | GCC 11+ / Clang 14+ |
| CMake | 3.10 | 3.20+ |
| Memory | 256 MB | 1 GB+ |

### Building MemRogue

```bash
# Clone and build
git clone https://github.com/yourusername/memrogue.git
cd memrogue
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Verify installation
./bin/memrogue-report --version
```

### Verifying the Installation

After building, verify MemRogue works correctly:

```bash
# Run the built-in example
LD_PRELOAD=$(pwd)/lib/libmemrogue.so ./bin/memrogue_example

# Run the test suite
make test
```

---

## Basic Usage

### Method 1: LD_PRELOAD (No Code Changes)

The simplest way to use MemRogue is via the `LD_PRELOAD` environment variable. This method requires no recompilation of your application.

```bash
# Basic usage
LD_PRELOAD=/path/to/libmemrogue.so ./your_application

# With output to file
MEMROGUE_OUTPUT=memory_report.txt LD_PRELOAD=/path/to/libmemrogue.so ./your_application

# With verbose output
MEMROGUE_VERBOSITY=2 LD_PRELOAD=/path/to/libmemrogue.so ./your_application
```

**Example session:**

```bash
$ cd /path/to/memrogue/build

$ cat > /tmp/test_leak.c << 'EOF'
#include <stdlib.h>
int main() {
    char* leak1 = malloc(100);   // This will leak
    char* leak2 = malloc(200);   // This will also leak
    char* ok = malloc(50);
    free(ok);                     // This is properly freed
    return 0;
}
EOF

$ gcc -o /tmp/test_leak /tmp/test_leak.c

$ LD_PRELOAD=$(pwd)/lib/libmemrogue.so /tmp/test_leak
================================================================================
                           MEMROGUE LEAK REPORT
================================================================================
Memory leaks detected: 2
Total leaked bytes: 300
...
```

### Method 2: Direct Linking

For applications where you want tighter integration:

```bash
# Compile with MemRogue
gcc -o your_app your_app.c \
    -I/path/to/memrogue/include \
    -L/path/to/memrogue/build/lib \
    -lmemrogue_core \
    -lpthread

# Run (library path may be needed)
LD_LIBRARY_PATH=/path/to/memrogue/build/lib ./your_app
```

---

## Integration Methods

### Runtime Interception (LD_PRELOAD)

**Pros:**
- No code changes required
- Works with any compiled binary
- Easy to enable/disable
- Perfect for debugging production issues

**Cons:**
- Linux-only (requires dynamic linker support)
- May not intercept static allocations
- Slight startup overhead

**Usage:**
```bash
# Create a wrapper script
cat > run_with_memrogue.sh << 'EOF'
#!/bin/bash
export LD_PRELOAD=/path/to/libmemrogue.so
export MEMROGUE_ENABLED=1
export MEMROGUE_OUTPUT="${1%.exe}_leaks.txt"
exec "$@"
EOF

chmod +x run_with_memrogue.sh
./run_with_memrogue.sh ./your_application
```

### Compile-Time Linking

**Pros:**
- Full control over tracking
- Can track custom allocators
- Works on all platforms
- Programmatic access to leak data

**Cons:**
- Requires recompilation
- Code modifications needed

**Example:**
```c
#include <stdio.h>
#include <stdlib.h>
#include "memrogue_tracker.h"
#include "memrogue_leak_detector.h"
#include "memrogue_report.h"

int main() {
    // Initialize the tracker
    MemrogueTracker* tracker = tracker_create();
    if (!tracker) {
        fprintf(stderr, "Failed to initialize MemRogue tracker\n");
        return 1;
    }
    
    // Your application code here
    void* ptr1 = malloc(100);
    void* ptr2 = malloc(200);
    free(ptr1);
    // ptr2 is intentionally leaked for demonstration
    
    // Get leak report
    TrackerStats stats;
    tracker_get_stats(tracker, &stats);
    
    printf("Total allocations: %zu\n", stats.total_allocations);
    printf("Total deallocations: %zu\n", stats.total_deallocations);
    printf("Current allocations: %zu\n", stats.current_allocations);
    printf("Peak memory: %zu bytes\n", stats.peak_memory);
    
    // Cleanup
    tracker_destroy(tracker);
    return 0;
}
```

---

## Working with Reports

### Understanding the Text Report

MemRogue's text reports contain several sections:

```
================================================================================
                           MEMROGUE LEAK REPORT
================================================================================

SUMMARY
-------
Report generated at: 2024-01-15 14:30:45
Application: /path/to/your_application
Process ID: 12345

Statistics:
  Total allocations tracked:  1,247
  Total deallocations:        1,244
  Memory leaks detected:      3
  Total leaked bytes:         2,048
  Peak memory usage:          15,360 bytes

--------------------------------------------------------------------------------
LEAK #1: 1024 bytes at 0x7f8b4c001000
--------------------------------------------------------------------------------
  Allocated at:
    #0  0x7f8b4c100234 in malloc (memrogue_intercept.c:45)
    #1  0x555555555678 in create_buffer (buffer.c:23)
    #2  0x555555555890 in initialize_system (main.c:156)
    #3  0x555555555abc in main (main.c:42)
  
  Allocation timestamp: 1699876543.123456

--------------------------------------------------------------------------------
LEAK #2: 512 bytes at 0x7f8b4c002000
--------------------------------------------------------------------------------
...
```

### Generating Different Report Formats

**Text Report (Human-Readable):**
```bash
./bin/memrogue-report --format=text --output=report.txt input.bin
```

**JSON Report (Machine-Readable):**
```bash
./bin/memrogue-report --format=json --output=report.json input.bin
```

**CSV Report (Spreadsheet-Compatible):**
```bash
./bin/memrogue-report --format=csv --output=report.csv input.bin
```

**Summary Only:**
```bash
./bin/memrogue-report --format=summary input.bin
```

### Analyzing CSV Reports with Python

MemRogue includes a Python analysis script:

```bash
python3 examples/analyze_leaks.py report.csv
```

This provides:
- Top memory consumers by source location
- Allocation size distribution
- Timeline analysis
- Backtrace frequency analysis

Custom analysis example:

```python
import csv
from collections import defaultdict

def analyze_leaks(csv_file):
    leaks_by_function = defaultdict(lambda: {'count': 0, 'bytes': 0})
    
    with open(csv_file, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            func = row.get('function_name', 'unknown')
            size = int(row.get('size', 0))
            leaks_by_function[func]['count'] += 1
            leaks_by_function[func]['bytes'] += size
    
    print("Leaks by Function:")
    print("-" * 60)
    for func, data in sorted(leaks_by_function.items(), 
                             key=lambda x: x[1]['bytes'], 
                             reverse=True):
        print(f"{func}: {data['count']} leaks, {data['bytes']} bytes")

if __name__ == '__main__':
    import sys
    analyze_leaks(sys.argv[1])
```

---

## Advanced Usage

### Sampling Mode for Production

For production environments where full tracking overhead is unacceptable:

```bash
# Track only 10% of allocations randomly
MEMROGUE_SAMPLE_RATE=10 \
MEMROGUE_SAMPLING_MODE=random \
LD_PRELOAD=/path/to/libmemrogue.so ./your_application
```

**Sampling Modes:**

| Mode | Description | Use Case |
|------|-------------|----------|
| `random` | Probabilistic sampling | Production profiling |
| `deterministic` | Every Nth allocation | Reproducible debugging |

### Filtering and Focusing

To reduce noise and focus on specific areas:

```bash
# Minimal output - only critical issues
MEMROGUE_VERBOSITY=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app

# Disable backtrace capture (faster)
MEMROGUE_BACKTRACE=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app

# Shallow backtraces only
MEMROGUE_MAX_DEPTH=4 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
```

### Multi-Threaded Applications

MemRogue is fully thread-safe. No special configuration is needed:

```bash
# Works automatically with pthreads
LD_PRELOAD=/path/to/libmemrogue.so ./multi_threaded_app
```

Thread-specific information is included in reports when available.

### Custom Allocator Integration

If your application uses custom allocators, you can manually track allocations:

```c
#include "memrogue_tracker.h"

// Your custom allocator
void* my_pool_alloc(MemPool* pool, size_t size) {
    void* ptr = internal_pool_alloc(pool, size);
    
    // Track with MemRogue
    track_allocation(global_tracker, ptr, size, __FILE__, __LINE__, __func__);
    
    return ptr;
}

void my_pool_free(MemPool* pool, void* ptr) {
    // Track deallocation
    track_deallocation(global_tracker, ptr);
    
    internal_pool_free(pool, ptr);
}
```

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Memory Leak Check

on: [push, pull_request]

jobs:
  leak-check:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Build MemRogue
      run: |
        git clone https://github.com/yourusername/memrogue.git /tmp/memrogue
        cd /tmp/memrogue
        mkdir build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make -j$(nproc)
    
    - name: Build Application
      run: |
        mkdir build && cd build
        cmake ..
        make
    
    - name: Run with Memory Checking
      run: |
        cd build
        MEMROGUE_OUTPUT=leaks.txt \
        LD_PRELOAD=/tmp/memrogue/build/lib/libmemrogue.so \
        ./your_test_suite
    
    - name: Check for Leaks
      run: |
        if grep -q "Memory leaks detected: [1-9]" build/leaks.txt; then
          echo "Memory leaks found!"
          cat build/leaks.txt
          exit 1
        fi
        echo "No memory leaks detected"
    
    - name: Upload Leak Report
      if: failure()
      uses: actions/upload-artifact@v3
      with:
        name: leak-report
        path: build/leaks.txt
```

### Jenkins Pipeline Example

```groovy
pipeline {
    agent any
    
    stages {
        stage('Build') {
            steps {
                sh 'mkdir -p build && cd build && cmake .. && make'
            }
        }
        
        stage('Memory Check') {
            steps {
                sh '''
                    cd build
                    MEMROGUE_OUTPUT=leaks.json \
                    MEMROGUE_VERBOSITY=1 \
                    LD_PRELOAD=/opt/memrogue/lib/libmemrogue.so \
                    ./run_tests
                '''
            }
        }
        
        stage('Analyze Results') {
            steps {
                script {
                    def leaks = readJSON file: 'build/leaks.json'
                    if (leaks.summary.leak_count > 0) {
                        error "Found ${leaks.summary.leak_count} memory leaks"
                    }
                }
            }
        }
    }
    
    post {
        always {
            archiveArtifacts artifacts: 'build/leaks.*', allowEmptyArchive: true
        }
    }
}
```

### Exit Codes for Automation

The `memrogue-report` tool uses specific exit codes for CI integration:

| Code | Meaning | Action |
|------|---------|--------|
| 0 | Success, no leaks | Continue pipeline |
| 1 | General error | Investigate failure |
| 2 | Usage error | Fix command syntax |
| 3 | I/O error | Check file permissions |
| 4 | Parse error | Check input format |
| 5 | Memory error | Increase resources |
| 10 | Leaks found | Investigate leaks |

---

## Best Practices

### Development Workflow

1. **Always run with MemRogue during development:**
   ```bash
   # Add to your shell profile
   alias memcheck='LD_PRELOAD=/path/to/libmemrogue.so'
   
   # Usage
   memcheck ./your_app
   ```

2. **Use verbose mode when debugging:**
   ```bash
   MEMROGUE_VERBOSITY=3 memcheck ./your_app
   ```

3. **Save reports for comparison:**
   ```bash
   MEMROGUE_OUTPUT="leak_report_$(date +%Y%m%d_%H%M%S).txt" memcheck ./your_app
   ```

### Production Considerations

1. **Use sampling to reduce overhead:**
   ```bash
   MEMROGUE_SAMPLE_RATE=5 MEMROGUE_SAMPLING_MODE=random
   ```

2. **Disable backtraces if not needed:**
   ```bash
   MEMROGUE_BACKTRACE=0
   ```

3. **Log to file, not stderr:**
   ```bash
   MEMROGUE_OUTPUT=/var/log/memrogue/$(hostname)_$(date +%Y%m%d).log
   ```

### Debugging Tips

1. **Start with high verbosity:**
   ```bash
   MEMROGUE_VERBOSITY=3
   ```

2. **Use deep backtraces for elusive bugs:**
   ```bash
   MEMROGUE_MAX_DEPTH=32
   ```

3. **Enable all detection features:**
   ```bash
   MEMROGUE_DETECT_DOUBLE_FREE=1
   MEMROGUE_DETECT_INVALID_FREE=1
   ```

### Memory Leak Hunting Strategy

1. **Identify the leak location** using the backtrace
2. **Understand the allocation pattern** (when/where allocated)
3. **Trace the expected deallocation path**
4. **Find where the free() should have been called**
5. **Verify the fix** by running with MemRogue again

---

## Next Steps

- Read the [API Reference](API.md) for programmatic usage
- Check [Configuration](CONFIGURATION.md) for all options
- See [Troubleshooting](TROUBLESHOOTING.md) for common issues

---

*Need help? Open an issue on GitHub or check the troubleshooting guide.*
