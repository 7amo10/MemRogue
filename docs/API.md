# MemRogue API Reference

This document provides a complete reference for all public APIs in MemRogue.

---

## Table of Contents

1. [Overview](#overview)
2. [Tracker API](#tracker-api)
3. [Configuration API](#configuration-api)
4. [Leak Detector API](#leak-detector-api)
5. [Report Formatter API](#report-formatter-api)
6. [JSON Export API](#json-export-api)
7. [CSV Export API](#csv-export-api)
8. [Backtrace API](#backtrace-api)
9. [Hash Table API](#hash-table-api)
10. [Error Codes](#error-codes)
11. [Data Structures](#data-structures)

---

## Overview

### Header Organization

| Header | Purpose |
|--------|---------|
| `memrogue_tracker.h` | Core allocation tracking |
| `memrogue_config.h` | Configuration management |
| `memrogue_leak_detector.h` | Leak detection engine |
| `memrogue_report.h` | Text report generation |
| `memrogue_json.h` | JSON export |
| `memrogue_csv.h` | CSV export |
| `memrogue_backtrace.h` | Stack trace capture |
| `memrogue_hash_table.h` | Internal hash table |
| `memrogue_allocation_record.h` | Allocation record structures |
| `memrogue_double_free.h` | Double-free detection |
| `memrogue_invalid_free.h` | Invalid-free detection |
| `memrogue_cli.h` | CLI tool support |
| `memrogue_exit_handler.h` | Exit handler registration |

### Thread Safety

All MemRogue APIs are thread-safe unless explicitly noted. The library uses pthread mutexes internally to protect shared state.

### Memory Management

Unless otherwise specified:
- Functions returning pointers to new objects transfer ownership to the caller
- Callers must free returned objects using the appropriate `*_destroy()` or `*_free()` function
- Passing NULL to `*_destroy()` functions is safe (no-op)

---

## Tracker API

**Header:** `memrogue_tracker.h`

The tracker is the core component that records all memory allocations and deallocations.

### Data Types

```c
typedef struct MemrogueTracker MemrogueTracker;

typedef struct {
    size_t total_allocations;     // Total malloc/calloc/realloc calls
    size_t total_deallocations;   // Total free calls
    size_t current_allocations;   // Currently outstanding allocations
    size_t total_bytes_allocated; // Total bytes ever allocated
    size_t current_bytes;         // Currently allocated bytes
    size_t peak_memory;           // Peak memory usage (bytes)
    size_t peak_allocations;      // Peak number of concurrent allocations
} TrackerStats;

// Callback for iterating over allocations
typedef void (*AllocationIterator)(const AllocationRecord* record, void* user_data);
```

### Functions

#### tracker_create

```c
MemrogueTracker* tracker_create(void);
```

Creates a new memory tracker instance.

**Returns:**
- Pointer to new tracker on success
- `NULL` on failure (memory allocation failed)

**Example:**
```c
MemrogueTracker* tracker = tracker_create();
if (!tracker) {
    fprintf(stderr, "Failed to create tracker\n");
    exit(1);
}
```

---

#### tracker_destroy

```c
void tracker_destroy(MemrogueTracker* tracker);
```

Destroys a tracker and frees all associated resources.

**Parameters:**
- `tracker`: Tracker to destroy (may be NULL)

**Example:**
```c
tracker_destroy(tracker);
tracker = NULL;
```

---

#### track_allocation

```c
int track_allocation(
    MemrogueTracker* tracker,
    void* ptr,
    size_t size,
    const char* file,
    int line,
    const char* function
);
```

Records a memory allocation.

**Parameters:**
- `tracker`: The tracker instance
- `ptr`: Pointer returned by allocator
- `size`: Size of allocation in bytes
- `file`: Source file name (use `__FILE__`)
- `line`: Source line number (use `__LINE__`)
- `function`: Function name (use `__func__`)

**Returns:**
- `0` on success
- Non-zero error code on failure

**Example:**
```c
void* ptr = malloc(100);
if (ptr) {
    track_allocation(tracker, ptr, 100, __FILE__, __LINE__, __func__);
}
```

---

#### track_deallocation

```c
int track_deallocation(MemrogueTracker* tracker, void* ptr);
```

Records a memory deallocation.

**Parameters:**
- `tracker`: The tracker instance
- `ptr`: Pointer being freed

**Returns:**
- `0` on success
- `MEMROGUE_ERR_DOUBLE_FREE` if pointer was already freed
- `MEMROGUE_ERR_INVALID_FREE` if pointer was never allocated

**Example:**
```c
int result = track_deallocation(tracker, ptr);
if (result == MEMROGUE_ERR_DOUBLE_FREE) {
    fprintf(stderr, "Double free detected!\n");
}
free(ptr);
```

---

#### tracker_get_stats

```c
int tracker_get_stats(const MemrogueTracker* tracker, TrackerStats* stats);
```

Retrieves current tracking statistics.

**Parameters:**
- `tracker`: The tracker instance
- `stats`: Output structure for statistics

**Returns:**
- `0` on success
- Non-zero on failure

**Example:**
```c
TrackerStats stats;
if (tracker_get_stats(tracker, &stats) == 0) {
    printf("Current allocations: %zu\n", stats.current_allocations);
    printf("Peak memory: %zu bytes\n", stats.peak_memory);
}
```

---

#### tracker_iterate

```c
void tracker_iterate(
    const MemrogueTracker* tracker,
    AllocationIterator callback,
    void* user_data
);
```

Iterates over all current allocations.

**Parameters:**
- `tracker`: The tracker instance
- `callback`: Function called for each allocation
- `user_data`: User data passed to callback

**Example:**
```c
void print_allocation(const AllocationRecord* record, void* user_data) {
    printf("  %p: %zu bytes from %s:%d\n",
           record->ptr, record->size, record->file, record->line);
}

printf("Current allocations:\n");
tracker_iterate(tracker, print_allocation, NULL);
```

---

#### tracker_clear

```c
void tracker_clear(MemrogueTracker* tracker);
```

Clears all tracked allocations (resets to empty state).

**Parameters:**
- `tracker`: The tracker instance

---

#### tracker_get_allocation_count

```c
size_t tracker_get_allocation_count(const MemrogueTracker* tracker);
```

Returns the number of currently tracked allocations.

**Parameters:**
- `tracker`: The tracker instance

**Returns:**
- Number of allocations currently being tracked

---

## Configuration API

**Header:** `memrogue_config.h`

Manages MemRogue configuration through environment variables.

### Data Types

```c
typedef struct MemrogueConfig MemrogueConfig;

// Sampling modes
typedef enum {
    MEMROGUE_SAMPLING_RANDOM,      // Probabilistic sampling
    MEMROGUE_SAMPLING_DETERMINISTIC // Every Nth allocation
} MemrogueSamplingMode;
```

### Environment Variables

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| `MEMROGUE_ENABLED` | bool | `1` | Enable/disable tracking |
| `MEMROGUE_OUTPUT` | string | `stderr` | Output destination |
| `MEMROGUE_SAMPLE_RATE` | int | `100` | Sampling rate (1-100) |
| `MEMROGUE_SAMPLING_MODE` | string | `random` | `random` or `deterministic` |
| `MEMROGUE_BACKTRACE` | bool | `1` | Enable backtrace capture |
| `MEMROGUE_VERBOSITY` | int | `1` | Verbosity level (0-3) |
| `MEMROGUE_MAX_DEPTH` | int | `16` | Max backtrace depth |
| `MEMROGUE_REPORT_ON_EXIT` | bool | `1` | Report on process exit |
| `MEMROGUE_DETECT_DOUBLE_FREE` | bool | `1` | Enable double-free detection |
| `MEMROGUE_DETECT_INVALID_FREE` | bool | `1` | Enable invalid-free detection |

### Functions

#### config_load

```c
MemrogueConfig* config_load(void);
```

Loads configuration from environment variables.

**Returns:**
- Pointer to new config on success
- `NULL` on failure

**Example:**
```c
MemrogueConfig* config = config_load();
if (!config) {
    // Use defaults or handle error
}
```

---

#### config_free

```c
void config_free(MemrogueConfig* config);
```

Frees a configuration object.

---

#### config_is_enabled

```c
bool config_is_enabled(const MemrogueConfig* config);
```

Checks if tracking is enabled.

**Returns:**
- `true` if enabled
- `false` if disabled

---

#### config_get_output_path

```c
const char* config_get_output_path(const MemrogueConfig* config);
```

Gets the output file path.

**Returns:**
- File path string
- `"stderr"` or `"stdout"` for standard streams
- `NULL` if config is NULL

---

#### config_get_sample_rate

```c
int config_get_sample_rate(const MemrogueConfig* config);
```

Gets the sampling rate (1-100).

**Returns:**
- Sampling rate percentage

---

#### config_get_sampling_mode

```c
MemrogueSamplingMode config_get_sampling_mode(const MemrogueConfig* config);
```

Gets the sampling mode.

**Returns:**
- `MEMROGUE_SAMPLING_RANDOM` or `MEMROGUE_SAMPLING_DETERMINISTIC`

---

#### config_should_sample

```c
bool config_should_sample(MemrogueConfig* config);
```

Determines if the current allocation should be sampled.

**Returns:**
- `true` if allocation should be tracked
- `false` if allocation should be skipped

**Example:**
```c
if (config_should_sample(config)) {
    track_allocation(tracker, ptr, size, file, line, func);
}
```

---

#### config_is_backtrace_enabled

```c
bool config_is_backtrace_enabled(const MemrogueConfig* config);
```

Checks if backtrace capture is enabled.

---

#### config_get_verbosity

```c
int config_get_verbosity(const MemrogueConfig* config);
```

Gets verbosity level (0-3).

| Level | Description |
|-------|-------------|
| 0 | Quiet (errors only) |
| 1 | Normal |
| 2 | Verbose |
| 3 | Debug |

---

#### config_get_max_backtrace_depth

```c
int config_get_max_backtrace_depth(const MemrogueConfig* config);
```

Gets maximum backtrace depth.

---

#### config_should_report_on_exit

```c
bool config_should_report_on_exit(const MemrogueConfig* config);
```

Checks if exit report is enabled.

---

#### config_is_double_free_detection_enabled

```c
bool config_is_double_free_detection_enabled(const MemrogueConfig* config);
```

Checks if double-free detection is enabled.

---

#### config_is_invalid_free_detection_enabled

```c
bool config_is_invalid_free_detection_enabled(const MemrogueConfig* config);
```

Checks if invalid-free detection is enabled.

---

## Leak Detector API

**Header:** `memrogue_leak_detector.h`

Analyzes tracked allocations to detect memory leaks.

### Functions

#### leak_detector_create

```c
LeakDetector* leak_detector_create(MemrogueTracker* tracker);
```

Creates a leak detector associated with a tracker.

**Parameters:**
- `tracker`: The tracker to analyze

**Returns:**
- Pointer to new detector on success
- `NULL` on failure

---

#### leak_detector_destroy

```c
void leak_detector_destroy(LeakDetector* detector);
```

Destroys a leak detector.

---

#### leak_detector_analyze

```c
LeakReport* leak_detector_analyze(LeakDetector* detector);
```

Analyzes current allocations and generates a leak report.

**Returns:**
- Pointer to leak report on success
- `NULL` on failure

**Example:**
```c
LeakDetector* detector = leak_detector_create(tracker);
LeakReport* report = leak_detector_analyze(detector);

if (report->leak_count > 0) {
    printf("Found %zu leaks!\n", report->leak_count);
}

leak_report_free(report);
leak_detector_destroy(detector);
```

---

#### leak_report_free

```c
void leak_report_free(LeakReport* report);
```

Frees a leak report.

---

## Report Formatter API

**Header:** `memrogue_report.h`

Generates human-readable text reports.

### Data Types

```c
typedef struct ReportFormatter ReportFormatter;

typedef struct {
    bool include_backtrace;     // Include stack traces
    bool include_timestamp;     // Include timestamps
    bool include_statistics;    // Include summary statistics
    bool color_output;          // Use ANSI colors
    int max_leaks_to_show;      // Limit on leaks shown (-1 for all)
} ReportConfig;
```

### Functions

#### report_formatter_create

```c
ReportFormatter* report_formatter_create(const ReportConfig* config);
```

Creates a report formatter with the given configuration.

**Parameters:**
- `config`: Configuration options (may be NULL for defaults)

**Returns:**
- Pointer to new formatter on success
- `NULL` on failure

---

#### report_formatter_destroy

```c
void report_formatter_destroy(ReportFormatter* formatter);
```

Destroys a report formatter.

---

#### report_format_text

```c
char* report_format_text(
    ReportFormatter* formatter,
    const LeakReport* report
);
```

Formats a leak report as text.

**Parameters:**
- `formatter`: The formatter instance
- `report`: Leak report to format

**Returns:**
- Newly allocated string with formatted report
- Caller must free with `free()`
- `NULL` on failure

**Example:**
```c
ReportConfig config = {
    .include_backtrace = true,
    .include_statistics = true,
    .color_output = isatty(STDOUT_FILENO)
};

ReportFormatter* formatter = report_formatter_create(&config);
char* text = report_format_text(formatter, report);
printf("%s", text);
free(text);
report_formatter_destroy(formatter);
```

---

#### report_write_to_stream

```c
int report_write_to_stream(
    ReportFormatter* formatter,
    const LeakReport* report,
    FILE* stream
);
```

Writes formatted report directly to a stream.

**Parameters:**
- `formatter`: The formatter instance
- `report`: Leak report to format
- `stream`: Output stream (e.g., `stdout`, `stderr`, file)

**Returns:**
- `0` on success
- Non-zero on failure

---

#### report_write_to_file

```c
int report_write_to_file(
    ReportFormatter* formatter,
    const LeakReport* report,
    const char* filepath
);
```

Writes formatted report to a file.

**Parameters:**
- `formatter`: The formatter instance
- `report`: Leak report to format
- `filepath`: Path to output file

**Returns:**
- `0` on success
- Non-zero on failure

---

## JSON Export API

**Header:** `memrogue_json.h`

Exports leak reports in JSON format.

### Functions

#### json_formatter_create

```c
JsonFormatter* json_formatter_create(void);
```

Creates a JSON formatter.

**Returns:**
- Pointer to new formatter on success
- `NULL` on failure

---

#### json_formatter_destroy

```c
void json_formatter_destroy(JsonFormatter* formatter);
```

Destroys a JSON formatter.

---

#### json_format_report

```c
char* json_format_report(JsonFormatter* formatter, const LeakReport* report);
```

Formats a leak report as JSON.

**Returns:**
- Newly allocated JSON string
- Caller must free with `free()`
- `NULL` on failure

**Example:**
```c
JsonFormatter* formatter = json_formatter_create();
char* json = json_format_report(formatter, report);

FILE* f = fopen("report.json", "w");
fprintf(f, "%s", json);
fclose(f);

free(json);
json_formatter_destroy(formatter);
```

### JSON Output Format

```json
{
    "version": "1.0",
    "generator": "MemRogue",
    "timestamp": "2024-01-15T14:30:45Z",
    "summary": {
        "total_allocations": 1247,
        "total_deallocations": 1244,
        "leak_count": 3,
        "total_leaked_bytes": 2048,
        "peak_memory": 15360
    },
    "leaks": [
        {
            "id": 1,
            "address": "0x7f8b4c001000",
            "size": 1024,
            "timestamp": 1699876543.123456,
            "source": {
                "file": "buffer.c",
                "line": 23,
                "function": "create_buffer"
            },
            "backtrace": [
                {
                    "frame": 0,
                    "address": "0x7f8b4c100234",
                    "function": "malloc",
                    "file": "memrogue_intercept.c",
                    "line": 45
                }
            ]
        }
    ]
}
```

---

## CSV Export API

**Header:** `memrogue_csv.h`

Exports leak reports in CSV format (RFC 4180 compliant).

### Data Types

```c
typedef struct CsvFormatter CsvFormatter;

typedef struct {
    char delimiter;           // Field delimiter (default: ',')
    char quote_char;          // Quote character (default: '"')
    bool include_header;      // Include header row (default: true)
    bool include_backtrace;   // Include backtrace column (default: true)
    const char* line_ending;  // Line ending (default: "\r\n")
    const char* null_value;   // Representation of NULL (default: "")
} CsvConfig;
```

### Functions

#### csv_formatter_create

```c
CsvFormatter* csv_formatter_create(const CsvConfig* config);
```

Creates a CSV formatter with the given configuration.

**Parameters:**
- `config`: Configuration options (may be NULL for defaults)

**Returns:**
- Pointer to new formatter on success
- `NULL` on failure

---

#### csv_formatter_destroy

```c
void csv_formatter_destroy(CsvFormatter* formatter);
```

Destroys a CSV formatter.

---

#### csv_format_report

```c
char* csv_format_report(CsvFormatter* formatter, const LeakReport* report);
```

Formats a leak report as CSV.

**Returns:**
- Newly allocated CSV string
- Caller must free with `free()`
- `NULL` on failure

---

#### csv_write_to_stream

```c
int csv_write_to_stream(
    CsvFormatter* formatter,
    const LeakReport* report,
    FILE* stream
);
```

Writes CSV report directly to a stream.

**Returns:**
- `0` on success
- Non-zero on failure

---

#### csv_write_to_file

```c
int csv_write_to_file(
    CsvFormatter* formatter,
    const LeakReport* report,
    const char* filepath
);
```

Writes CSV report to a file.

**Returns:**
- `0` on success
- Non-zero on failure

**Example:**
```c
CsvConfig config = {
    .delimiter = ',',
    .include_header = true,
    .include_backtrace = true
};

CsvFormatter* formatter = csv_formatter_create(&config);
csv_write_to_file(formatter, report, "leaks.csv");
csv_formatter_destroy(formatter);
```

### CSV Output Format

```csv
address,size,allocation_time,source_file,source_line,function_name,backtrace
0x7f8b4c001000,1024,1699876543.123456,buffer.c,23,create_buffer,"malloc;create_buffer;init;main"
0x7f8b4c002000,512,1699876543.234567,utils.c,89,alloc_string,"malloc;alloc_string;parse;main"
```

---

## Backtrace API

**Header:** `memrogue_backtrace.h`

Captures and symbolizes stack traces.

### Data Types

```c
#define MEMROGUE_MAX_BACKTRACE_DEPTH 64

typedef struct {
    void* addresses[MEMROGUE_MAX_BACKTRACE_DEPTH];
    char** symbols;       // Symbolized names (may be NULL)
    int depth;            // Actual depth captured
} Backtrace;
```

### Functions

#### backtrace_capture

```c
Backtrace* backtrace_capture(int max_depth);
```

Captures current stack trace.

**Parameters:**
- `max_depth`: Maximum frames to capture

**Returns:**
- Pointer to new backtrace on success
- `NULL` on failure

---

#### backtrace_free

```c
void backtrace_free(Backtrace* bt);
```

Frees a backtrace.

---

#### backtrace_symbolize

```c
int backtrace_symbolize(Backtrace* bt);
```

Resolves addresses to symbol names.

**Parameters:**
- `bt`: Backtrace to symbolize

**Returns:**
- `0` on success
- Non-zero on failure

---

#### backtrace_to_string

```c
char* backtrace_to_string(const Backtrace* bt);
```

Converts backtrace to human-readable string.

**Returns:**
- Newly allocated string
- Caller must free with `free()`

---

## Hash Table API

**Header:** `memrogue_hash_table.h`

Internal hash table for allocation lookup.

### Functions

#### hash_table_create

```c
HashTable* hash_table_create(size_t initial_capacity);
```

Creates a new hash table.

---

#### hash_table_destroy

```c
void hash_table_destroy(HashTable* table);
```

Destroys a hash table.

---

#### hash_table_insert

```c
int hash_table_insert(HashTable* table, void* key, void* value);
```

Inserts a key-value pair.

---

#### hash_table_lookup

```c
void* hash_table_lookup(HashTable* table, void* key);
```

Looks up a value by key.

---

#### hash_table_remove

```c
void* hash_table_remove(HashTable* table, void* key);
```

Removes and returns value for key.

---

## Error Codes

```c
// Success
#define MEMROGUE_SUCCESS              0

// General errors
#define MEMROGUE_ERR_INVALID_ARG     -1
#define MEMROGUE_ERR_OUT_OF_MEMORY   -2
#define MEMROGUE_ERR_IO              -3

// Detection errors
#define MEMROGUE_ERR_DOUBLE_FREE     -10
#define MEMROGUE_ERR_INVALID_FREE    -11

// CLI exit codes
#define CLI_EXIT_SUCCESS             0
#define CLI_EXIT_ERROR               1
#define CLI_EXIT_USAGE               2
#define CLI_EXIT_IO_ERROR            3
#define CLI_EXIT_PARSE_ERROR         4
#define CLI_EXIT_MEMORY_ERROR        5
#define CLI_EXIT_LEAKS_FOUND         10
```

---

## Data Structures

### AllocationRecord

```c
typedef struct {
    void* ptr;              // Allocated pointer
    size_t size;            // Allocation size
    const char* file;       // Source file
    int line;               // Source line
    const char* function;   // Function name
    double timestamp;       // Allocation timestamp
    Backtrace* backtrace;   // Stack trace (may be NULL)
    uint64_t allocation_id; // Unique ID
} AllocationRecord;
```

### LeakReport

```c
typedef struct {
    size_t leak_count;           // Number of leaks
    size_t total_leaked_bytes;   // Total bytes leaked
    AllocationRecord** leaks;    // Array of leak records
    TrackerStats stats;          // Overall statistics
    double report_timestamp;     // When report was generated
} LeakReport;
```

---

## Thread Safety Notes

1. **Tracker Operations**: All tracker operations (`track_allocation`, `track_deallocation`, `tracker_get_stats`) are thread-safe.

2. **Configuration**: Configuration objects are immutable after creation and safe to read from multiple threads.

3. **Formatters**: Formatters are NOT thread-safe. Create separate instances for concurrent formatting.

4. **Reports**: LeakReport objects should be accessed from a single thread.

---

## Version Information

```c
// From memrogue_cli.h
#define MEMROGUE_VERSION_MAJOR 1
#define MEMROGUE_VERSION_MINOR 0
#define MEMROGUE_VERSION_PATCH 0
#define MEMROGUE_VERSION_STRING "1.0.0"
```

---

*For usage examples and tutorials, see [USAGE.md](USAGE.md).*
