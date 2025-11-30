# MemRogue Configuration Reference

This document provides a complete reference for all MemRogue configuration options.

---

## Table of Contents

1. [Overview](#overview)
2. [Environment Variables](#environment-variables)
3. [Configuration Profiles](#configuration-profiles)
4. [Programmatic Configuration](#programmatic-configuration)
5. [Best Practices](#best-practices)

---

## Overview

MemRogue is configured entirely through environment variables. This approach offers several advantages:

- **No code changes required** - Configure at runtime
- **Easy to enable/disable** - Just change environment
- **Scriptable** - Integrate with shell scripts and CI/CD
- **Flexible** - Different configurations for different runs

### Setting Environment Variables

**Single command:**
```bash
MEMROGUE_ENABLED=1 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
```

**Shell session:**
```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_OUTPUT=/tmp/leaks.txt
export LD_PRELOAD=/path/to/libmemrogue.so
./your_app
```

**Persistent (add to ~/.bashrc or ~/.zshrc):**
```bash
export MEMROGUE_LIB=/path/to/libmemrogue.so
alias memcheck='LD_PRELOAD=$MEMROGUE_LIB'
```

---

## Environment Variables

### Core Settings

#### MEMROGUE_ENABLED

Enables or disables memory tracking entirely.

| Property | Value |
|----------|-------|
| **Type** | Boolean |
| **Default** | `1` (enabled) |
| **Values** | `0` = disabled, `1` = enabled |

**Usage:**
```bash
# Enable (default)
MEMROGUE_ENABLED=1 LD_PRELOAD=... ./your_app

# Disable (no overhead)
MEMROGUE_ENABLED=0 LD_PRELOAD=... ./your_app
```

**When to use:**
- Set to `0` to quickly disable without removing LD_PRELOAD
- Useful for A/B performance comparisons

---

#### MEMROGUE_OUTPUT

Specifies where to write the leak report.

| Property | Value |
|----------|-------|
| **Type** | String (file path or stream name) |
| **Default** | `stderr` |
| **Values** | File path, `stderr`, `stdout` |

**Usage:**
```bash
# Output to stderr (default)
MEMROGUE_OUTPUT=stderr LD_PRELOAD=... ./your_app

# Output to stdout
MEMROGUE_OUTPUT=stdout LD_PRELOAD=... ./your_app

# Output to file
MEMROGUE_OUTPUT=/var/log/memrogue/leaks.txt LD_PRELOAD=... ./your_app

# Dynamic filename with timestamp
MEMROGUE_OUTPUT="/tmp/leaks_$(date +%Y%m%d_%H%M%S).txt" LD_PRELOAD=... ./your_app
```

**Notes:**
- Directories must exist; MemRogue won't create them
- Use absolute paths to avoid confusion
- For JSON/CSV output, use appropriate file extension

---

#### MEMROGUE_VERBOSITY

Controls the amount of output detail.

| Property | Value |
|----------|-------|
| **Type** | Integer |
| **Default** | `1` |
| **Range** | `0` - `3` |

**Levels:**

| Level | Name | Description |
|-------|------|-------------|
| `0` | Quiet | Errors only; minimal output |
| `1` | Normal | Standard leak reports |
| `2` | Verbose | Additional statistics and context |
| `3` | Debug | Full debug information; very detailed |

**Usage:**
```bash
# Quiet mode (CI/CD)
MEMROGUE_VERBOSITY=0 LD_PRELOAD=... ./your_app

# Normal (default)
MEMROGUE_VERBOSITY=1 LD_PRELOAD=... ./your_app

# Verbose (debugging)
MEMROGUE_VERBOSITY=2 LD_PRELOAD=... ./your_app

# Debug (troubleshooting)
MEMROGUE_VERBOSITY=3 LD_PRELOAD=... ./your_app
```

---

#### MEMROGUE_REPORT_ON_EXIT

Controls whether a report is automatically generated when the process exits.

| Property | Value |
|----------|-------|
| **Type** | Boolean |
| **Default** | `1` (enabled) |
| **Values** | `0` = disabled, `1` = enabled |

**Usage:**
```bash
# Enable automatic report (default)
MEMROGUE_REPORT_ON_EXIT=1 LD_PRELOAD=... ./your_app

# Disable automatic report (manual control)
MEMROGUE_REPORT_ON_EXIT=0 LD_PRELOAD=... ./your_app
```

**When to disable:**
- When using programmatic API for custom report generation
- When running with very large numbers of allocations
- When app doesn't exit normally (killed, crash)

---

### Detection Settings

#### MEMROGUE_DETECT_DOUBLE_FREE

Enables detection of double-free errors.

| Property | Value |
|----------|-------|
| **Type** | Boolean |
| **Default** | `1` (enabled) |
| **Values** | `0` = disabled, `1` = enabled |

**What it detects:**
```c
void* ptr = malloc(100);
free(ptr);
free(ptr);  // MemRogue will catch this
```

**Usage:**
```bash
MEMROGUE_DETECT_DOUBLE_FREE=1 LD_PRELOAD=... ./your_app
```

**When to disable:**
- Custom memory pools that reuse addresses
- False positives with certain allocators

---

#### MEMROGUE_DETECT_INVALID_FREE

Enables detection of invalid free() calls.

| Property | Value |
|----------|-------|
| **Type** | Boolean |
| **Default** | `1` (enabled) |
| **Values** | `0` = disabled, `1` = enabled |

**What it detects:**
```c
int stack_var;
free(&stack_var);  // MemRogue will catch this

free((void*)0x12345678);  // MemRogue will catch this
```

**Usage:**
```bash
MEMROGUE_DETECT_INVALID_FREE=1 LD_PRELOAD=... ./your_app
```

---

### Stack Trace Settings

#### MEMROGUE_BACKTRACE

Enables capturing stack traces for each allocation.

| Property | Value |
|----------|-------|
| **Type** | Boolean |
| **Default** | `1` (enabled) |
| **Values** | `0` = disabled, `1` = enabled |

**Usage:**
```bash
# Enable backtraces (default, more detail but slower)
MEMROGUE_BACKTRACE=1 LD_PRELOAD=... ./your_app

# Disable backtraces (faster, less detail)
MEMROGUE_BACKTRACE=0 LD_PRELOAD=... ./your_app
```

**Performance impact:**
- Enabled: ~5-15% overhead
- Disabled: ~1-3% overhead

**When to disable:**
- Production environments
- Very allocation-heavy applications
- When you only need leak counts, not locations

---

#### MEMROGUE_MAX_DEPTH

Maximum stack trace depth to capture.

| Property | Value |
|----------|-------|
| **Type** | Integer |
| **Default** | `16` |
| **Range** | `1` - `64` |

**Usage:**
```bash
# Shallow traces (faster)
MEMROGUE_MAX_DEPTH=4 LD_PRELOAD=... ./your_app

# Default depth
MEMROGUE_MAX_DEPTH=16 LD_PRELOAD=... ./your_app

# Deep traces (for complex call stacks)
MEMROGUE_MAX_DEPTH=32 LD_PRELOAD=... ./your_app
```

**Guidance:**
- `4`: Simple applications, leaf-level debugging
- `16`: Most applications (default)
- `32`: Deep call hierarchies, framework-heavy apps

---

### Performance Tuning

#### MEMROGUE_SAMPLE_RATE

Percentage of allocations to track.

| Property | Value |
|----------|-------|
| **Type** | Integer |
| **Default** | `100` (track all) |
| **Range** | `1` - `100` |

**Usage:**
```bash
# Track all allocations (default, most accurate)
MEMROGUE_SAMPLE_RATE=100 LD_PRELOAD=... ./your_app

# Track 50% (good balance)
MEMROGUE_SAMPLE_RATE=50 LD_PRELOAD=... ./your_app

# Track 10% (production-friendly)
MEMROGUE_SAMPLE_RATE=10 LD_PRELOAD=... ./your_app

# Track 1% (minimal overhead)
MEMROGUE_SAMPLE_RATE=1 LD_PRELOAD=... ./your_app
```

**Statistical accuracy:**
| Sample Rate | Accuracy | Use Case |
|-------------|----------|----------|
| 100% | Complete | Development, CI |
| 50% | Very High | Testing |
| 10% | High | Staging, production profiling |
| 1% | Statistical | High-traffic production |

---

#### MEMROGUE_SAMPLING_MODE

Algorithm used for sampling decisions.

| Property | Value |
|----------|-------|
| **Type** | String |
| **Default** | `random` |
| **Values** | `random`, `deterministic` |

**Modes:**

| Mode | Description | Behavior |
|------|-------------|----------|
| `random` | Probabilistic | Each allocation has N% chance of being tracked |
| `deterministic` | Every Nth | Every Nth allocation is tracked |

**Usage:**
```bash
# Random sampling (default, better distribution)
MEMROGUE_SAMPLING_MODE=random MEMROGUE_SAMPLE_RATE=10 LD_PRELOAD=... ./your_app

# Deterministic sampling (reproducible)
MEMROGUE_SAMPLING_MODE=deterministic MEMROGUE_SAMPLE_RATE=10 LD_PRELOAD=... ./your_app
```

**When to use each:**

| Mode | Best For |
|------|----------|
| Random | Production profiling, statistical analysis |
| Deterministic | Debugging, reproducible test cases |

---

## Configuration Profiles

### Development Profile

Maximum detail for debugging:

```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=3
export MEMROGUE_BACKTRACE=1
export MEMROGUE_MAX_DEPTH=32
export MEMROGUE_SAMPLE_RATE=100
export MEMROGUE_DETECT_DOUBLE_FREE=1
export MEMROGUE_DETECT_INVALID_FREE=1
export MEMROGUE_REPORT_ON_EXIT=1
export MEMROGUE_OUTPUT=stderr
```

**One-liner:**
```bash
MEMROGUE_ENABLED=1 MEMROGUE_VERBOSITY=3 MEMROGUE_BACKTRACE=1 MEMROGUE_MAX_DEPTH=32 MEMROGUE_SAMPLE_RATE=100 MEMROGUE_DETECT_DOUBLE_FREE=1 MEMROGUE_DETECT_INVALID_FREE=1 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
```

---

### CI/CD Profile

Automated testing with clear pass/fail:

```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=1
export MEMROGUE_BACKTRACE=1
export MEMROGUE_MAX_DEPTH=16
export MEMROGUE_SAMPLE_RATE=100
export MEMROGUE_DETECT_DOUBLE_FREE=1
export MEMROGUE_DETECT_INVALID_FREE=1
export MEMROGUE_REPORT_ON_EXIT=1
export MEMROGUE_OUTPUT=/tmp/leak_report.txt
```

---

### Staging Profile

Balance between detail and performance:

```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=1
export MEMROGUE_BACKTRACE=1
export MEMROGUE_MAX_DEPTH=8
export MEMROGUE_SAMPLE_RATE=25
export MEMROGUE_SAMPLING_MODE=random
export MEMROGUE_DETECT_DOUBLE_FREE=1
export MEMROGUE_DETECT_INVALID_FREE=1
export MEMROGUE_REPORT_ON_EXIT=1
export MEMROGUE_OUTPUT=/var/log/memrogue/staging.log
```

---

### Production Profile

Minimal overhead for production monitoring:

```bash
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=0
export MEMROGUE_BACKTRACE=0
export MEMROGUE_SAMPLE_RATE=5
export MEMROGUE_SAMPLING_MODE=random
export MEMROGUE_DETECT_DOUBLE_FREE=0
export MEMROGUE_DETECT_INVALID_FREE=0
export MEMROGUE_REPORT_ON_EXIT=1
export MEMROGUE_OUTPUT=/var/log/memrogue/prod_$(hostname).log
```

**One-liner:**
```bash
MEMROGUE_ENABLED=1 MEMROGUE_VERBOSITY=0 MEMROGUE_BACKTRACE=0 MEMROGUE_SAMPLE_RATE=5 MEMROGUE_DETECT_DOUBLE_FREE=0 MEMROGUE_DETECT_INVALID_FREE=0 LD_PRELOAD=/path/to/libmemrogue.so ./your_app
```

---

### Disabled Profile

Complete disable for baseline comparison:

```bash
export MEMROGUE_ENABLED=0
```

---

## Programmatic Configuration

When linking MemRogue directly, you can configure programmatically:

```c
#include "memrogue_config.h"

int main() {
    // Load configuration from environment
    MemrogueConfig* config = config_load();
    
    // Check configuration
    if (config_is_enabled(config)) {
        printf("MemRogue is enabled\n");
        printf("Sample rate: %d%%\n", config_get_sample_rate(config));
        printf("Verbosity: %d\n", config_get_verbosity(config));
    }
    
    // Use configuration
    if (config_should_sample(config)) {
        // Track this allocation
    }
    
    config_free(config);
    return 0;
}
```

### Configuration API Reference

| Function | Description |
|----------|-------------|
| `config_load()` | Load configuration from environment |
| `config_free(config)` | Free configuration object |
| `config_is_enabled(config)` | Check if tracking is enabled |
| `config_get_output_path(config)` | Get output file path |
| `config_get_sample_rate(config)` | Get sampling rate (1-100) |
| `config_get_sampling_mode(config)` | Get sampling mode |
| `config_should_sample(config)` | Decide if allocation should be tracked |
| `config_is_backtrace_enabled(config)` | Check if backtraces enabled |
| `config_get_verbosity(config)` | Get verbosity level (0-3) |
| `config_get_max_backtrace_depth(config)` | Get max backtrace depth |
| `config_should_report_on_exit(config)` | Check if exit report enabled |
| `config_is_double_free_detection_enabled(config)` | Check double-free detection |
| `config_is_invalid_free_detection_enabled(config)` | Check invalid-free detection |

---

## Best Practices

### 1. Use Shell Aliases

```bash
# Add to ~/.bashrc or ~/.zshrc

# Basic memory check
alias memcheck='LD_PRELOAD=/path/to/libmemrogue.so'

# Verbose memory check
alias memcheck-verbose='MEMROGUE_VERBOSITY=3 LD_PRELOAD=/path/to/libmemrogue.so'

# Quick leak scan (fast)
alias memcheck-quick='MEMROGUE_BACKTRACE=0 MEMROGUE_SAMPLE_RATE=50 LD_PRELOAD=/path/to/libmemrogue.so'
```

### 2. Create Configuration Scripts

```bash
#!/bin/bash
# memrogue-dev.sh - Development configuration

export LD_PRELOAD=/path/to/libmemrogue.so
export MEMROGUE_ENABLED=1
export MEMROGUE_VERBOSITY=2
export MEMROGUE_BACKTRACE=1
export MEMROGUE_MAX_DEPTH=16
export MEMROGUE_SAMPLE_RATE=100
export MEMROGUE_OUTPUT="${1:-/dev/stderr}"

exec "${@:2}"
```

Usage: `./memrogue-dev.sh /tmp/leaks.txt ./your_app args...`

### 3. Log Rotation for Production

```bash
# Logrotate configuration: /etc/logrotate.d/memrogue
/var/log/memrogue/*.log {
    daily
    rotate 7
    compress
    delaycompress
    missingok
    notifempty
    create 644 root root
}
```

### 4. Docker Integration

```dockerfile
# Dockerfile
FROM ubuntu:22.04

# Install MemRogue
COPY --from=memrogue-builder /memrogue/build/lib/libmemrogue.so /usr/local/lib/

# Set environment
ENV LD_PRELOAD=/usr/local/lib/libmemrogue.so
ENV MEMROGUE_ENABLED=1
ENV MEMROGUE_SAMPLE_RATE=10
ENV MEMROGUE_OUTPUT=/var/log/memrogue.log

CMD ["./your_app"]
```

### 5. systemd Service Integration

```ini
# /etc/systemd/system/myapp.service
[Unit]
Description=My Application with MemRogue

[Service]
ExecStart=/usr/local/bin/myapp
Environment="LD_PRELOAD=/usr/local/lib/libmemrogue.so"
Environment="MEMROGUE_ENABLED=1"
Environment="MEMROGUE_SAMPLE_RATE=10"
Environment="MEMROGUE_OUTPUT=/var/log/myapp-leaks.log"

[Install]
WantedBy=multi-user.target
```

---

## Quick Reference Card

| Variable | Default | Description |
|----------|---------|-------------|
| `MEMROGUE_ENABLED` | `1` | Enable tracking |
| `MEMROGUE_OUTPUT` | `stderr` | Output destination |
| `MEMROGUE_VERBOSITY` | `1` | Output detail (0-3) |
| `MEMROGUE_BACKTRACE` | `1` | Capture stack traces |
| `MEMROGUE_MAX_DEPTH` | `16` | Max trace depth |
| `MEMROGUE_SAMPLE_RATE` | `100` | Track percentage |
| `MEMROGUE_SAMPLING_MODE` | `random` | Sampling algorithm |
| `MEMROGUE_REPORT_ON_EXIT` | `1` | Auto-report on exit |
| `MEMROGUE_DETECT_DOUBLE_FREE` | `1` | Detect double-free |
| `MEMROGUE_DETECT_INVALID_FREE` | `1` | Detect invalid-free |

---

*For usage examples, see [USAGE.md](USAGE.md). For troubleshooting, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).*
