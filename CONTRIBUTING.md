# Contributing to MemRogue

First off, thank you for considering contributing to MemRogue! 🎉

MemRogue is a community-driven project, and we welcome contributions of all kinds: bug reports, feature requests, documentation improvements, and code contributions.

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [How to Contribute](#how-to-contribute)
- [Coding Standards](#coding-standards)
- [Testing Guidelines](#testing-guidelines)
- [Pull Request Process](#pull-request-process)
- [Issue Guidelines](#issue-guidelines)
- [Documentation](#documentation)
- [Community](#community)

---

## Code of Conduct

This project adheres to the Contributor Covenant [Code of Conduct](https://www.contributor-covenant.org/version/2/1/code_of_conduct/). By participating, you are expected to uphold this code. Please report unacceptable behavior to [a8087027@gmail.com](mailto:a8087027@gmail.com).

---

## Getting Started

### Prerequisites

- **Operating System**: Linux (glibc-based distributions)
- **Compiler**: GCC 9+ or Clang 10+
- **Build System**: CMake 3.15+
- **Optional**: Valgrind, lcov (for coverage)

### Fork and Clone

1. Fork the repository on GitHub
2. Clone your fork:
   ```bash
   git clone https://github.com/YOUR_USERNAME/MemRogue.git
   cd MemRogue
   ```
3. Add the upstream remote:
   ```bash
   git remote add upstream https://github.com/7amo10/MemRogue.git
   ```

---

## Development Setup

### Build from Source

```bash
# Create build directory
mkdir build && cd build

# Configure with all options
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DMEMROGUE_BUILD_TESTS=ON \
      -DMEMROGUE_BUILD_EXAMPLES=ON \
      -DMEMROGUE_BUILD_BENCHMARKS=ON \
      ..

# Build
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Release | Build type (Debug/Release/RelWithDebInfo) |
| `MEMROGUE_BUILD_TESTS` | ON | Build test suite |
| `MEMROGUE_BUILD_EXAMPLES` | ON | Build example applications |
| `MEMROGUE_BUILD_BENCHMARKS` | ON | Build performance benchmarks |
| `MEMROGUE_ENABLE_COVERAGE` | OFF | Enable code coverage |

### Running Tests

```bash
# Run all tests
ctest --output-on-failure

# Run specific test
ctest -R HashTableTest -V

# Run integration tests only
ctest -L integration -V

# Run with coverage
cmake -DMEMROGUE_ENABLE_COVERAGE=ON ..
make
ctest
make coverage
```

---

## How to Contribute

### Types of Contributions

1. **Bug Reports** - Found a bug? Open an issue with reproduction steps
2. **Feature Requests** - Have an idea? Open an issue to discuss
3. **Documentation** - Improve docs, fix typos, add examples
4. **Code** - Fix bugs, implement features, improve performance
5. **Tests** - Add test coverage, improve existing tests
6. **Examples** - Add example programs demonstrating features

### Before You Start

1. Check existing issues to avoid duplicates
2. For large changes, open an issue first to discuss
3. Ensure your contribution aligns with project goals

---

## Coding Standards

### C Code Style

We follow a consistent code style based on the Linux kernel style with modifications:

```c
// Function naming: snake_case
void memrogue_track_allocation(void* ptr, size_t size);

// Macros: UPPER_SNAKE_CASE
#define MEMROGUE_MAX_STACK_DEPTH 64

// Types: snake_case with _t suffix
typedef struct allocation_record allocation_record_t;

// Constants: UPPER_SNAKE_CASE
#define DEFAULT_HASH_TABLE_SIZE 1024
```

### File Organization

```
include/          # Public headers
src/              # Implementation files
tests/            # Test files
docs/             # Documentation
examples/         # Example programs
```

### Header Guards

```c
#ifndef MEMROGUE_FEATURE_NAME_H
#define MEMROGUE_FEATURE_NAME_H

// Content

#endif // MEMROGUE_FEATURE_NAME_H
```

### Comments

```c
/* ============================================================================
 * Section Header
 * ============================================================================ */

/**
 * @brief Brief description of function.
 * 
 * Detailed description if needed.
 * 
 * @param param1 Description of first parameter
 * @param param2 Description of second parameter
 * @return Description of return value
 * 
 * @note Any important notes
 * @warning Any warnings about usage
 */
int memrogue_function(int param1, void* param2);
```

### Error Handling

```c
// Use explicit error checking
if (ptr == NULL) {
    fprintf(stderr, "[MemRogue] Error: allocation failed\n");
    return MEMROGUE_ERROR_MEMORY;
}

// Clean up on error paths
if (error_condition) {
    free(allocated_resource);
    return MEMROGUE_ERROR;
}
```

---

## Testing Guidelines

### Test Structure

```c
// Test file: tests/test_feature.c

#include <assert.h>
#include <stdio.h>
#include "memrogue_feature.h"

// Individual test functions
static int test_feature_basic(void) {
    // Setup
    feature_t* f = feature_create();
    
    // Test
    int result = feature_operation(f);
    
    // Verify
    assert(result == EXPECTED_VALUE);
    
    // Cleanup
    feature_destroy(f);
    
    return 0; // Success
}

// Main test runner
int main(void) {
    int failures = 0;
    
    printf("=== Feature Tests ===\n");
    
    failures += test_feature_basic();
    failures += test_feature_edge_cases();
    
    printf("\n=== Results: %d failures ===\n", failures);
    return failures;
}
```

### Test Categories

1. **Unit Tests** - Test individual functions/modules
2. **Integration Tests** - Test component interactions
3. **Stress Tests** - Test under high load
4. **Memory Tests** - Run under Valgrind/AddressSanitizer

### Running with Sanitizers

```bash
# AddressSanitizer
cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" ..
make && ctest

# ThreadSanitizer
cmake -DCMAKE_C_FLAGS="-fsanitize=thread -g" ..
make && ctest

# UndefinedBehaviorSanitizer
cmake -DCMAKE_C_FLAGS="-fsanitize=undefined -g" ..
make && ctest
```

---

## Pull Request Process

### Branch Naming

```
feature/description    # New features
fix/description        # Bug fixes
docs/description       # Documentation changes
refactor/description   # Code refactoring
test/description       # Test additions/improvements
```

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
type(scope): brief description

Longer explanation if needed.

Fixes #123
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Examples:
```
feat(tracker): add memory sampling mode
fix(hash): resolve collision handling edge case
docs(api): update stack trace documentation
test(leak): add integration test for large allocations
```

### PR Checklist

Before submitting a PR, ensure:

- [ ] Code follows project style guidelines
- [ ] All existing tests pass
- [ ] New tests added for new functionality
- [ ] Documentation updated if needed
- [ ] Commit messages follow conventions
- [ ] No merge conflicts with main branch
- [ ] PR description explains changes clearly

### Review Process

1. Submit PR with clear description
2. CI/CD runs automated checks
3. Maintainer reviews code
4. Address any feedback
5. PR is merged once approved

---

## Issue Guidelines

### Bug Reports

Include:
- MemRogue version
- Operating system and version
- Compiler version
- Steps to reproduce
- Expected vs actual behavior
- Minimal reproducible example if possible

### Feature Requests

Include:
- Clear description of the feature
- Use case / motivation
- Potential implementation approach
- Any alternatives considered

---

## Documentation

### Documentation Files

| File | Purpose |
|------|---------|
| `README.md` | Project overview and quick start |
| `docs/API.md` | Complete API reference |
| `docs/USAGE.md` | Usage guide and tutorials |
| `docs/CONFIGURATION.md` | Configuration options |
| `docs/PERFORMANCE.md` | Performance characteristics |
| `docs/TROUBLESHOOTING.md` | Common issues and solutions |
| `CHANGELOG.md` | Version history |
| `CONTRIBUTING.md` | This file |

### Documentation Style

- Use clear, concise language
- Include code examples
- Keep examples working and tested
- Update docs when changing functionality

---

## Community

### Getting Help

- **GitHub Issues**: For bugs and feature requests
- **GitHub Discussions**: For questions and general discussion

### Recognition

Contributors are recognized in:
- `CHANGELOG.md` for releases
- GitHub contributors page
- Release notes

---

## License

By contributing to MemRogue, you agree that your contributions will be licensed under the MIT License.

---

Thank you for contributing to MemRogue! 🚀
