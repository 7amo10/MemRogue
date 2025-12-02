# MemRogue Example Applications

This directory contains example applications demonstrating how to use MemRogue for memory leak detection and analysis in various scenarios.

## Quick Start

Build all examples:

```bash
cd build
cmake ..
make
```

Run any example with MemRogue:

```bash
LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak
```

Run all examples at once:

```bash
make run_examples
```

## Examples Overview

| Example | Description | Threads | Key Concepts |
|---------|-------------|---------|--------------|
| `basic_leak.c` | Simple memory leak patterns | No | Basic leak types |
| `multithreaded.c` | Concurrent allocations | Yes | Thread safety, producer-consumer |
| `webserver_sim.c` | Realistic server scenario | Yes | Connection pools, sessions |
| `custom_allocator.c` | Custom allocator tracking | No | Pool, arena, slab allocators |

---

## 1. Basic Leak Example (`basic_leak.c`)

**Purpose:** Educational demonstration of common memory leak patterns.

**Leak Types Demonstrated:**

1. **Simple Leak** - Single forgotten `free()` call
2. **Overwritten Pointer (Lost Reference)** - Allocated pointer overwritten before being freed
3. **Error Path Leak** - Memory leaked only in certain code paths
4. **Loop Leak** - Accumulating leaks in iterations

**Usage:**

```bash
LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak
```

**Expected Output:**

```
╔══════════════════════════════════════════════════════════════════╗
║           MemRogue Basic Memory Leak Examples                    ║
╚══════════════════════════════════════════════════════════════════╝

[Leak 1] Demonstrating forgotten free...
  Allocated 64 bytes at 0x...
  Content: "This memory will be leaked!"
  [!] Returning without freeing - LEAK!

[Leak 2] Demonstrating overwritten pointer...
  First allocation: 128 bytes at 0x...
  ...

════════════════════════════════════════════════════════════════════
Summary of intentional leaks:
  - Forgotten free:      64 bytes
  - Overwritten pointer: 128 bytes
  - Error path:          64 bytes
  - Loop accumulation:   160 bytes (5 × 32)
  Total leaked:          416 bytes
════════════════════════════════════════════════════════════════════
```

**Learning Points:**
- Always pair `malloc()` with `free()`
- Consider all code paths (error handling, early returns)
- Be careful with nested allocations in structs
- Watch for accumulating leaks in loops

---

## 2. Multithreaded Example (`multithreaded.c`)

**Purpose:** Demonstrates memory tracking in concurrent environments with proper thread synchronization.

**Features:**
- Producer-consumer pattern with work queue
- Mutex and condition variable synchronization
- Thread-safe memory allocation and deallocation
- Intentional leak demonstration in thread context

**Configuration:**

```c
#define NUM_WORKER_THREADS     4
#define ALLOCATIONS_PER_THREAD 10
#define WORK_ITEM_SIZE         128
#define QUEUE_SIZE             16
```

**Usage:**

```bash
LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_multithreaded
```

**Expected Behavior:**
- 4 worker threads perform thread-local allocations
- Producer-consumer pattern with 2 producers and 2 consumers
- Shared buffer pattern with 4 workers using reference counting
- Intentional leaks in thread-local pattern for demonstration
- Clean shutdown with thread synchronization

**Learning Points:**
- Proper mutex locking around shared data structures
- Condition variables for efficient thread waiting
- Memory ownership transfer between threads
- Graceful shutdown with `shutdown_flag` pattern

---

## 3. Web Server Simulation (`webserver_sim.c`)

**Purpose:** Realistic server scenario demonstrating memory management in production-like environments.

**Components:**
- **Connection Pool** - Fixed pool of reusable connections
- **Session Store** - User session management with timeout
- **Request Queue** - Thread-safe request dispatching
- **Worker Threads** - Concurrent request processing

**Leak Scenarios Demonstrated:**

1. **Error Path Leaks** - Request buffer not freed on parse error
2. **Session Timeout Leaks** - Session data not cleaned on expiry
3. **Connection Pool Leaks** - Incomplete cleanup on connection release
4. **Response Buffer Leaks** - Memory leaked on simulated client disconnect

**Configuration:**

```c
#define MAX_CONNECTIONS     32
#define MAX_SESSIONS        64
#define NUM_WORKER_THREADS  4
#define SIMULATION_REQUESTS 100
```

**Usage:**

```bash
LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_webserver_sim
```

**Simulated Endpoints:**
- `/` and `/index.html` - Static pages
- `/login` - Session creation
- `/api/data` - JSON API (with occasional leak)
- `/heavy` - Large buffer processing
- Other paths - 404 response

**Learning Points:**
- Resource pool management patterns
- Session lifecycle management
- Proper cleanup in error paths
- Thread-safe resource cleanup

---

## 4. Custom Allocator Example (`custom_allocator.c`)

**Purpose:** Demonstrates how MemRogue tracks memory through custom allocators.

**Allocator Types:**

### Pool Allocator
Fixed-size block allocation for predictable memory patterns.

```c
pool_allocator_t pool;
pool_init(&pool, 64, 16);       // 64-byte blocks, 16 blocks
void *ptr = pool_alloc(&pool);  // Get block
pool_free(&pool, ptr);          // Return block
pool_destroy(&pool);            // Free underlying memory
```

### Arena Allocator
Bump allocator with batch deallocation - efficient for phase-based allocation.

```c
arena_allocator_t arena;
arena_init(&arena, 4096);              // 4KB chunks
void *ptr = arena_alloc(&arena, 100);  // Bump allocation
arena_reset(&arena);                    // Reset for reuse
arena_destroy(&arena);                  // Free all chunks
```

### Slab Allocator
Object caching allocator - efficient for same-size object allocation.

```c
slab_cache_t cache;
slab_cache_init(&cache, "my_objects", sizeof(MyObject), 8);
MyObject *obj = slab_alloc(&cache);
slab_cache_free(&cache, obj);
slab_cache_destroy(&cache);
```

**Usage:**

```bash
LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_custom_allocator
```

**Key Insight:**
All custom allocators use `malloc()`/`free()` internally. MemRogue tracks these underlying allocations, providing visibility into:
- Total memory used by each allocator
- Leaks from allocators not properly destroyed
- Logical leaks (objects not returned to allocator before destroy)

---

## Running with Different MemRogue Configurations

### Verbose Output

```bash
MEMROGUE_VERBOSE=1 LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak
```

### JSON Report

```bash
MEMROGUE_OUTPUT_FORMAT=json MEMROGUE_OUTPUT_FILE=leaks.json \
  LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak
```

### CSV Report

```bash
MEMROGUE_OUTPUT_FORMAT=csv MEMROGUE_OUTPUT_FILE=leaks.csv \
  LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak
```

### With Sampling (for high-frequency allocations)

```bash
MEMROGUE_SAMPLE_RATE=10 \
  LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_webserver_sim
```

---

## Analyzing Results

After running an example, MemRogue outputs a summary to stderr. For detailed analysis:

1. **Use the Report Tool:**
   ```bash
   ./bin/memrogue-report leaks.json
   ```

2. **Use the Python Analyzer:**
   ```bash
   python3 examples/analyze_leaks.py leaks.json
   ```

3. **Parse JSON/CSV programmatically** for integration with CI/CD systems.

---

## Troubleshooting

### "Library not found" error
Ensure you're running from the build directory:
```bash
cd build
LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak
```

### No output from MemRogue
Check that the library is being loaded:
```bash
MEMROGUE_VERBOSE=1 LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/example_basic_leak 2>&1 | head
```

### Deadlock in multithreaded example
This should not happen with proper implementation. If it does:
1. Check thread sanitizer: `gcc -fsanitize=thread`
2. Review mutex lock ordering

### Valgrind compatibility
Run with suppression file for clean Valgrind output:
```bash
valgrind --suppressions=../valgrind.supp ./bin/example_basic_leak
```

---

## Creating Your Own Examples

Use these examples as templates for testing your own applications:

1. **Start simple** - Use `basic_leak.c` as a template
2. **Add threading** - Reference `multithreaded.c` for thread-safe patterns
3. **Simulate real scenarios** - Use `webserver_sim.c` as inspiration
4. **Test custom allocators** - Adapt `custom_allocator.c` patterns

---

## License

These examples are part of the MemRogue project and are released under the MIT License.
