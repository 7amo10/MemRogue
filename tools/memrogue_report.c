/**
 * @file memrogue_report.c
 * @brief Main entry point for the memrogue-report CLI tool.
 *
 * This is the standalone binary for analyzing memory logs and generating
 * leak reports. It wraps the CLI library functions.
 *
 * Usage:
 *   memrogue-report [OPTIONS] [INPUT_FILE...]
 *   memrogue-report --help
 *   memrogue-report --version
 *
 * MEMRO-19: CLI Tool Foundation
 */

#include "memrogue_cli.h"

/**
 * Main entry point.
 *
 * Simply delegates to cli_main() which handles all CLI functionality.
 */
int main(int argc, char* argv[]) {
    return cli_main(argc, argv);
}
