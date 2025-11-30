# MemRogue Troubleshooting Guide

This guide covers common issues and their solutions when using MemRogue.

---

## Table of Contents

1. [Installation Issues](#installation-issues)
2. [Runtime Issues](#runtime-issues)
3. [False Positives](#false-positives)
4. [Performance Issues](#performance-issues)
5. [Report Issues](#report-issues)
6. [Platform-Specific Issues](#platform-specific-issues)
7. [Debugging MemRogue Itself](#debugging-memrogue-itself)
8. [FAQ](#faq)

---

## Installation Issues

### Build Fails: CMake Version Too Old

**Symptom:**
```
CMake Error at CMakeLists.txt:1:
  CMake 3.10 or higher is required.
```

**Solution:**
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install cmake

# Or install from source for newer version
wget https://cmake.org/files/v3.25/cmake-3.25.1.tar.gz
tar xzf cmake-3.25.1.tar.gz
cd cmake-3.25.1
./configure && make && sudo make install
```

---

### Build Fails: Missing pthread

**Symptom:**
```
error: pthread.h: No such file or directory
```

**Solution:**
```bash
# Ubuntu/Debian
sudo apt install libc6-dev

# Fedora/RHEL
sudo dnf install glibc-devel
```

---

### Build Fails: Compiler Not Found

**Symptom:**
```
-- The C compiler identification is unknown
CMake Error: CMAKE_C_COMPILER not set
```

**Solution:**
```bash
# Ubuntu/Debian
sudo apt install build-essential

# Fedora/RHEL
sudo dnf groupinstall "Development Tools"

# Explicitly specify compiler
cmake .. -DCMAKE_C_COMPILER=/usr/bin/gcc
```

---

### Linker Error: Undefined Reference to dlsym

**Symptom:**
```
undefined reference to `dlsym'
undefined reference to `dlopen'
```

**Solution:**

Add `-ldl` to link flags. This is usually handled automatically by CMake, but if building manually:

```bash
gcc -o your_app your_app.c -lmemrogue_core -ldl -lpthread
```

---

## Runtime Issues

### LD_PRELOAD Has No Effect

**Symptom:**
Application runs but no MemRogue output appears.

**Possible Causes & Solutions:**

1. **Wrong library path:**
   ```bash
   # Use absolute path
   LD_PRELOAD=/full/path/to/libmemrogue.so ./your_app
   
   # Verify library exists
   ls -la /path/to/libmemrogue.so
   ```

2. **32/64-bit mismatch:**
   ```bash
   # Check your app architecture
   file ./your_app
   
   # Check library architecture
   file /path/to/libmemrogue.so
   
   # Both should match (e.g., both 64-bit)
   ```

3. **Statically linked application:**
   ```bash
   # Check if dynamically linked
   ldd ./your_app
   
   # If "not a dynamic executable", LD_PRELOAD won't work
   # You must link MemRogue at compile time
   ```

4. **setuid/setgid binary:**
   ```bash
   # LD_PRELOAD is ignored for security reasons
   # Copy to non-setuid location or run as root
   cp /usr/bin/setuid_app /tmp/test_app
   LD_PRELOAD=/path/to/libmemrogue.so /tmp/test_app
   ```

5. **MemRogue disabled:**
   ```bash
   # Ensure enabled
   MEMROGUE_ENABLED=1 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

---

### Crash on Application Startup

**Symptom:**
```
Segmentation fault (core dumped)
```

**Possible Causes & Solutions:**

1. **Incompatible library version:**
   ```bash
   # Rebuild MemRogue for your system
   cd memrogue/build
   rm -rf *
   cmake .. && make
   ```

2. **Memory corruption in application:**
   ```bash
   # Try with reduced features
   MEMROGUE_BACKTRACE=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

3. **Stack overflow during backtrace:**
   ```bash
   # Reduce backtrace depth
   MEMROGUE_MAX_DEPTH=4 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

4. **Conflicting interceptors:**
   ```bash
   # Don't use with other memory tools simultaneously
   # Remove any other LD_PRELOAD libraries
   LD_PRELOAD=/path/to/libmemrogue.so ./your_app  # Only MemRogue
   ```

---

### Application Hangs

**Symptom:**
Application freezes and doesn't respond.

**Possible Causes & Solutions:**

1. **Deadlock in multi-threaded code:**
   ```bash
   # Get backtrace of hung process
   gdb -p $(pgrep your_app) -ex "thread apply all bt" -ex "quit"
   ```

2. **Excessive allocations overwhelming MemRogue:**
   ```bash
   # Use sampling
   MEMROGUE_SAMPLE_RATE=10 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

3. **Slow disk I/O for output:**
   ```bash
   # Use memory/tmpfs for output
   MEMROGUE_OUTPUT=/dev/shm/memrogue.log LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

---

### No Output Generated

**Symptom:**
Application runs and exits, but no leak report appears.

**Solutions:**

1. **Check output destination:**
   ```bash
   # Output goes to stderr by default
   LD_PRELOAD=/path/to/libmemrogue.so ./your_app 2>&1 | head -100
   
   # Or specify file
   MEMROGUE_OUTPUT=/tmp/leaks.txt LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   cat /tmp/leaks.txt
   ```

2. **Ensure exit handler runs:**
   ```bash
   # Make sure app exits normally (not killed)
   MEMROGUE_REPORT_ON_EXIT=1 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   
   # If app crashes, report may not generate
   ```

3. **Check verbosity:**
   ```bash
   MEMROGUE_VERBOSITY=2 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

4. **No leaks to report:**
   ```bash
   # If application has no leaks, report will be minimal
   # This is actually good!
   ```

---

### Error: "double free or corruption"

**Symptom:**
```
*** glibc detected *** free(): double free or corruption
```

**Explanation:**
This is actually MemRogue working correctly! It detected an actual double-free in your code.

**Solution:**

Check the MemRogue output for the location of the double-free:

```bash
MEMROGUE_VERBOSITY=3 \
MEMROGUE_DETECT_DOUBLE_FREE=1 \
LD_PRELOAD=/path/to/libmemrogue.so ./your_app 2>&1 | grep -A 20 "Double free"
```

The backtrace will show you where the double-free occurred.

---

## False Positives

### Leak Reports for Global/Static Allocations

**Symptom:**
MemRogue reports leaks for memory that is intentionally never freed.

**Explanation:**
Some programs allocate global state once at startup and rely on process termination to free it. This is a valid pattern but appears as a "leak" to MemRogue.

**Solutions:**

1. **Filter in post-processing:**
   ```bash
   # Export to CSV and filter
   MEMROGUE_OUTPUT=leaks.csv LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   grep -v "init_global\|setup_once" leaks.csv > real_leaks.csv
   ```

2. **Acknowledge expected leaks:**
   ```bash
   # Document expected "leaks" in your project
   # Compare new reports against baseline
   diff baseline_leaks.txt new_leaks.txt
   ```

3. **Free at exit (better practice):**
   ```c
   // Add cleanup function
   void cleanup_globals(void) {
       free(global_buffer);
   }
   
   int main() {
       atexit(cleanup_globals);
       // ...
   }
   ```

---

### False Double-Free Detection

**Symptom:**
MemRogue reports double-free but code looks correct.

**Possible Causes:**

1. **Custom allocator not tracked:**
   ```c
   // If using a memory pool, MemRogue may not know about internal reuse
   void* ptr = pool_alloc(pool, 100);
   pool_free(pool, ptr);
   // ptr may be reused by pool, but MemRogue thinks it's still freed
   ptr = pool_alloc(pool, 100);  // Same address reused
   pool_free(pool, ptr);  // Looks like double-free to MemRogue
   ```

   **Solution:** Disable double-free detection or integrate custom allocator:
   ```bash
   MEMROGUE_DETECT_DOUBLE_FREE=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

2. **Race condition in detection:**
   Very rare, but possible in highly concurrent code. Report as a bug if reproducible.

---

## Performance Issues

### Application Much Slower with MemRogue

**Symptom:**
Application runs 10x slower or more.

**Solutions:**

1. **Disable backtrace capture (biggest impact):**
   ```bash
   MEMROGUE_BACKTRACE=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

2. **Use sampling:**
   ```bash
   MEMROGUE_SAMPLE_RATE=10 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

3. **Reduce backtrace depth:**
   ```bash
   MEMROGUE_MAX_DEPTH=4 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

4. **Minimal verbosity:**
   ```bash
   MEMROGUE_VERBOSITY=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

5. **Full performance mode:**
   ```bash
   MEMROGUE_BACKTRACE=0 \
   MEMROGUE_SAMPLE_RATE=5 \
   MEMROGUE_VERBOSITY=0 \
   MEMROGUE_DETECT_DOUBLE_FREE=0 \
   MEMROGUE_DETECT_INVALID_FREE=0 \
   LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

---

### High Memory Usage by MemRogue

**Symptom:**
Memory usage increases significantly when running with MemRogue.

**Explanation:**
MemRogue stores metadata for each allocation. With millions of allocations, this adds up.

**Solutions:**

1. **Use sampling:**
   ```bash
   MEMROGUE_SAMPLE_RATE=5 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

2. **Disable backtraces (saves memory per allocation):**
   ```bash
   MEMROGUE_BACKTRACE=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

3. **Run shorter test scenarios:**
   Focus on specific code paths rather than full application runs.

---

## Report Issues

### Truncated Backtraces

**Symptom:**
```
#0  malloc
#1  ???
#2  ???
```

**Solutions:**

1. **Compile with debug symbols:**
   ```bash
   gcc -g -o your_app your_app.c
   ```

2. **Don't strip binaries:**
   ```bash
   # Avoid: strip your_app
   ```

3. **Install debug packages:**
   ```bash
   # Ubuntu/Debian
   sudo apt install libc6-dbg
   ```

4. **Increase backtrace depth:**
   ```bash
   MEMROGUE_MAX_DEPTH=32 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   ```

---

### Missing Source File/Line Information

**Symptom:**
```
#0  malloc at ??:0
#1  create_buffer at ??:0
```

**Solutions:**

1. **Compile with `-g` flag:**
   ```bash
   gcc -g -O0 -o your_app your_app.c
   ```

2. **Use `-rdynamic` for better symbol resolution:**
   ```bash
   gcc -g -rdynamic -o your_app your_app.c
   ```

3. **Ensure source files are accessible:**
   The debugger needs access to source files at their original paths.

---

### JSON/CSV Parse Errors

**Symptom:**
```
Error: Invalid JSON format
```

**Solutions:**

1. **Check for complete output:**
   ```bash
   # Ensure app exited cleanly
   MEMROGUE_OUTPUT=/tmp/report.json LD_PRELOAD=/path/to/libmemrogue.so ./your_app
   echo $?  # Should be 0
   
   # Check file isn't truncated
   tail /tmp/report.json  # Should end with proper closing braces
   ```

2. **Validate JSON:**
   ```bash
   python3 -m json.tool /tmp/report.json
   # or
   jq . /tmp/report.json
   ```

3. **Check for special characters in paths:**
   File paths with unusual characters may need escaping.

---

## Platform-Specific Issues

### macOS: LD_PRELOAD Not Working

**Symptom:**
LD_PRELOAD has no effect on macOS.

**Solution:**
macOS uses `DYLD_INSERT_LIBRARIES` instead:

```bash
DYLD_INSERT_LIBRARIES=/path/to/libmemrogue.dylib \
DYLD_FORCE_FLAT_NAMESPACE=1 \
./your_app
```

Note: System Integrity Protection (SIP) may block this for system binaries.

---

### ARM: Backtrace Issues

**Symptom:**
Incomplete or missing backtraces on ARM64.

**Solution:**
Ensure frame pointers are preserved:

```bash
gcc -g -fno-omit-frame-pointer -o your_app your_app.c
```

---

### Alpine Linux / musl libc

**Symptom:**
MemRogue doesn't work or crashes on Alpine.

**Explanation:**
Alpine uses musl libc instead of glibc, which has different internals.

**Solution:**
Build MemRogue on Alpine:

```bash
apk add build-base cmake
cd memrogue && mkdir build && cd build
cmake .. && make
```

Some features may be limited due to musl differences.

---

## Debugging MemRogue Itself

### Enable Debug Output

```bash
# Build with debug symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug
make

# Run with maximum verbosity
MEMROGUE_VERBOSITY=3 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
```

### Attach Debugger

```bash
# Run under gdb
LD_PRELOAD=/path/to/libmemrogue.so gdb ./your_app

# In gdb
(gdb) break memrogue_intercept.c:malloc_hook
(gdb) run
```

### Reporting Bugs

When reporting a bug, please include:

1. MemRogue version
2. Operating system and version
3. Compiler version
4. Minimal reproducible example
5. Full output with `MEMROGUE_VERBOSITY=3`
6. Steps to reproduce

---

## FAQ

### Q: Can I use MemRogue with Valgrind?

**A:** No, do not use both simultaneously. They both intercept memory functions and will conflict. Use one or the other.

---

### Q: Does MemRogue work with C++?

**A:** Yes! MemRogue intercepts `new`, `delete`, `new[]`, and `delete[]` in addition to C functions. Use the same `LD_PRELOAD` method.

---

### Q: Can I use MemRogue in production?

**A:** Yes, with sampling enabled. Use a low sample rate (5-10%) and disable backtraces for minimal overhead:

```bash
MEMROGUE_SAMPLE_RATE=5 \
MEMROGUE_BACKTRACE=0 \
MEMROGUE_VERBOSITY=0 \
LD_PRELOAD=/path/to/libmemrogue.so ./production_app
```

---

### Q: How do I track leaks in a daemon/service?

**A:** For systemd services, modify the service file:

```ini
[Service]
Environment="LD_PRELOAD=/path/to/libmemrogue.so"
Environment="MEMROGUE_OUTPUT=/var/log/myservice_leaks.log"
Environment="MEMROGUE_SAMPLE_RATE=10"
```

---

### Q: Can MemRogue detect buffer overflows?

**A:** No, MemRogue focuses on memory leaks, double-frees, and invalid-frees. For buffer overflow detection, use AddressSanitizer (ASan) or Valgrind.

---

### Q: Why doesn't MemRogue catch all my leaks?

**A:** Possible reasons:
- Sampling is enabled (not all allocations tracked)
- Custom allocators not using standard malloc/free
- Memory mapped files (mmap) not tracked
- Static/global memory (intentionally unfreed)

---

### Q: How do I integrate with my IDE?

**A:** Most IDEs support custom run configurations. Add the environment variables:

**VS Code (launch.json):**
```json
{
    "configurations": [{
        "name": "Debug with MemRogue",
        "type": "cppdbg",
        "environment": [
            {"name": "LD_PRELOAD", "value": "/path/to/libmemrogue.so"},
            {"name": "MEMROGUE_VERBOSITY", "value": "2"}
        ]
    }]
}
```

**CLion:**
Run → Edit Configurations → Environment Variables

---

*Still having issues? Open an issue on GitHub with detailed information about your problem.*
