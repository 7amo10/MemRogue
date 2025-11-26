# MemRogue - Custom Memory Debugger

MemRogue is a lightweight, production-ready memory debugging library for C/C++ applications. It intercepts memory allocation/deallocation calls, tracks allocations, and provides real-time leak detection and analysis capabilities.

## Features

- **Allocation Tracking**: Intercepts `malloc`, `free`, `calloc`, `realloc`, `new`, and `delete`.
- **Leak Detection**: Identifies unfreed memory and reports leaks.
- **Stack Traces**: Captures stack traces for each allocation.
- **Real-time Analysis**: Provides insights into memory usage.
- **Low Overhead**: Designed for minimal performance impact.

## Project Structure

- `src/`: Source code for the library.
- `include/`: Public header files.
- `tests/`: Unit and integration tests.
- `examples/`: Example usage programs.
- `docs/`: Documentation.

## Building

This project uses CMake.

```bash
mkdir build
cd build
cmake ..
make
```

## Using the Interception Layer

MemRogue ships with an LD_PRELOAD‑friendly shared library and an example program. After building:

```bash
cd build
LD_PRELOAD=$(pwd)/lib/libmemrogue.so ./bin/memrogue_example
```

The example performs a few allocations and deallocations, while MemRogue prints a short summary at process exit including any outstanding allocations.

## License

MIT License
