#!/usr/bin/env python3
"""
analyze_leaks.py - Example CSV analysis script for MemRogue leak reports.

This script demonstrates how to analyze MemRogue CSV output using Python
for spreadsheet-style analysis.

Features:
- Load and parse MemRogue CSV reports
- Group leaks by file, function, or size
- Calculate statistics (total bytes, leak counts)
- Generate summary reports
- Filter by various criteria

Usage:
    python3 analyze_leaks.py report.csv
    python3 analyze_leaks.py report.csv --group-by file
    python3 analyze_leaks.py report.csv --min-size 1024
    python3 analyze_leaks.py report.csv --top 10

MEMRO-23: CSV Export Format
"""

import argparse
import csv
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


def parse_args():
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description='Analyze MemRogue CSV leak reports',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s report.csv                 # Basic analysis
  %(prog)s report.csv --group-by file # Group by source file
  %(prog)s report.csv --min-size 1024 # Only leaks >= 1KB
  %(prog)s report.csv --top 10        # Show top 10 leaks
  %(prog)s report.csv --output stats.txt
        '''
    )
    
    parser.add_argument('csvfile', type=Path, help='CSV file to analyze')
    parser.add_argument('--group-by', '-g', 
                        choices=['file', 'function', 'line', 'size'],
                        default=None,
                        help='Group leaks by specified field')
    parser.add_argument('--min-size', '-m', type=int, default=0,
                        help='Minimum leak size to include (bytes)')
    parser.add_argument('--max-size', '-M', type=int, default=None,
                        help='Maximum leak size to include (bytes)')
    parser.add_argument('--top', '-t', type=int, default=None,
                        help='Show only top N leaks by size')
    parser.add_argument('--file-filter', '-f', type=str, default=None,
                        help='Only include leaks from files matching pattern')
    parser.add_argument('--output', '-o', type=Path, default=None,
                        help='Write output to file instead of stdout')
    parser.add_argument('--format', choices=['text', 'csv', 'json'],
                        default='text', help='Output format')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show detailed information')
    
    return parser.parse_args()


def load_csv(filepath):
    """Load and parse CSV file, returning list of leak dictionaries."""
    leaks = []
    
    with open(filepath, 'r', newline='') as f:
        # Try to detect dialect
        sample = f.read(8192)
        f.seek(0)
        
        try:
            dialect = csv.Sniffer().sniff(sample)
        except csv.Error:
            dialect = 'excel'
        
        reader = csv.DictReader(f, dialect=dialect)
        
        for row in reader:
            # Convert numeric fields
            leak = {}
            for key, value in row.items():
                if key == 'size':
                    leak[key] = int(value) if value else 0
                elif key == 'line':
                    leak[key] = int(value) if value else 0
                elif key == 'total_in_group':
                    leak[key] = int(value) if value else 0
                elif key == 'total_bytes':
                    leak[key] = int(value) if value else 0
                else:
                    leak[key] = value
            leaks.append(leak)
    
    return leaks


def filter_leaks(leaks, args):
    """Apply filters to leak list."""
    filtered = leaks
    
    # Filter by minimum size
    if args.min_size > 0:
        filtered = [l for l in filtered if l.get('size', 0) >= args.min_size]
    
    # Filter by maximum size
    if args.max_size is not None:
        filtered = [l for l in filtered if l.get('size', 0) <= args.max_size]
    
    # Filter by file pattern
    if args.file_filter:
        pattern = args.file_filter.lower()
        filtered = [l for l in filtered 
                    if l.get('file', '').lower().find(pattern) != -1]
    
    return filtered


def group_by_field(leaks, field):
    """Group leaks by specified field and calculate statistics."""
    groups = defaultdict(lambda: {'count': 0, 'total_bytes': 0, 'leaks': []})
    
    for leak in leaks:
        key = leak.get(field, 'unknown')
        if not key:
            key = 'unknown'
        
        groups[key]['count'] += 1
        groups[key]['total_bytes'] += leak.get('size', 0)
        groups[key]['leaks'].append(leak)
    
    return dict(groups)


def calculate_statistics(leaks):
    """Calculate overall statistics for leak list."""
    if not leaks:
        return {
            'total_leaks': 0,
            'total_bytes': 0,
            'avg_size': 0,
            'min_size': 0,
            'max_size': 0,
            'unique_files': 0,
            'unique_functions': 0,
        }
    
    sizes = [l.get('size', 0) for l in leaks]
    files = set(l.get('file', '') for l in leaks if l.get('file'))
    functions = set(l.get('function', '') for l in leaks if l.get('function'))
    
    return {
        'total_leaks': len(leaks),
        'total_bytes': sum(sizes),
        'avg_size': sum(sizes) / len(sizes) if sizes else 0,
        'min_size': min(sizes) if sizes else 0,
        'max_size': max(sizes) if sizes else 0,
        'unique_files': len(files),
        'unique_functions': len(functions),
    }


def format_bytes(n):
    """Format byte count as human-readable string."""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if abs(n) < 1024.0:
            return f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n:.1f} TB"


def print_text_report(leaks, stats, groups, args, out=sys.stdout):
    """Print text format report."""
    print("=" * 60, file=out)
    print("MemRogue Leak Analysis Report", file=out)
    print("=" * 60, file=out)
    print(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}", file=out)
    print(f"Input file: {args.csvfile}", file=out)
    print(file=out)
    
    # Summary statistics
    print("SUMMARY", file=out)
    print("-" * 40, file=out)
    print(f"Total leaks:       {stats['total_leaks']:>10}", file=out)
    print(f"Total bytes:       {format_bytes(stats['total_bytes']):>10}", file=out)
    print(f"Average size:      {format_bytes(stats['avg_size']):>10}", file=out)
    print(f"Min size:          {format_bytes(stats['min_size']):>10}", file=out)
    print(f"Max size:          {format_bytes(stats['max_size']):>10}", file=out)
    print(f"Unique files:      {stats['unique_files']:>10}", file=out)
    print(f"Unique functions:  {stats['unique_functions']:>10}", file=out)
    print(file=out)
    
    # Grouped analysis
    if groups:
        print(f"GROUPED BY: {args.group_by.upper()}", file=out)
        print("-" * 40, file=out)
        
        # Sort by total bytes descending
        sorted_groups = sorted(groups.items(), 
                               key=lambda x: x[1]['total_bytes'],
                               reverse=True)
        
        for key, data in sorted_groups[:20]:  # Limit to top 20
            print(f"{key}:", file=out)
            print(f"  Count: {data['count']}, Bytes: {format_bytes(data['total_bytes'])}", 
                  file=out)
        print(file=out)
    
    # Top leaks
    if args.top or not groups:
        print("TOP LEAKS BY SIZE", file=out)
        print("-" * 40, file=out)
        
        sorted_leaks = sorted(leaks, key=lambda x: x.get('size', 0), reverse=True)
        n = args.top or 10
        
        for i, leak in enumerate(sorted_leaks[:n], 1):
            addr = leak.get('address', 'unknown')
            size = format_bytes(leak.get('size', 0))
            file = leak.get('file', 'unknown')
            line = leak.get('line', '?')
            func = leak.get('function', 'unknown')
            
            print(f"{i:3}. {addr} - {size}", file=out)
            if args.verbose:
                print(f"     File: {file}:{line}", file=out)
                print(f"     Function: {func}", file=out)
    
    print(file=out)
    print("=" * 60, file=out)


def print_csv_report(leaks, stats, groups, args, out=sys.stdout):
    """Print CSV format report (summary + top leaks)."""
    writer = csv.writer(out)
    
    # Write summary
    writer.writerow(['Metric', 'Value'])
    writer.writerow(['total_leaks', stats['total_leaks']])
    writer.writerow(['total_bytes', stats['total_bytes']])
    writer.writerow(['avg_size', f"{stats['avg_size']:.1f}"])
    writer.writerow(['min_size', stats['min_size']])
    writer.writerow(['max_size', stats['max_size']])
    writer.writerow(['unique_files', stats['unique_files']])
    writer.writerow(['unique_functions', stats['unique_functions']])
    writer.writerow([])
    
    # Write grouped data if applicable
    if groups:
        writer.writerow([args.group_by, 'count', 'total_bytes'])
        sorted_groups = sorted(groups.items(), 
                               key=lambda x: x[1]['total_bytes'],
                               reverse=True)
        for key, data in sorted_groups:
            writer.writerow([key, data['count'], data['total_bytes']])


def print_json_report(leaks, stats, groups, args, out=sys.stdout):
    """Print JSON format report."""
    import json
    
    report = {
        'generated': datetime.now().isoformat(),
        'input_file': str(args.csvfile),
        'statistics': stats,
    }
    
    if groups:
        report['grouped_by'] = args.group_by
        report['groups'] = {
            k: {'count': v['count'], 'total_bytes': v['total_bytes']}
            for k, v in groups.items()
        }
    
    # Include top leaks
    n = args.top or 10
    sorted_leaks = sorted(leaks, key=lambda x: x.get('size', 0), reverse=True)
    report['top_leaks'] = sorted_leaks[:n]
    
    json.dump(report, out, indent=2)
    print(file=out)  # Trailing newline


def main():
    args = parse_args()
    
    # Check input file exists
    if not args.csvfile.exists():
        print(f"Error: File not found: {args.csvfile}", file=sys.stderr)
        return 1
    
    # Load and process data
    try:
        leaks = load_csv(args.csvfile)
    except Exception as e:
        print(f"Error loading CSV: {e}", file=sys.stderr)
        return 1
    
    if not leaks:
        print("No leaks found in CSV file.", file=sys.stderr)
        return 0
    
    # Apply filters
    leaks = filter_leaks(leaks, args)
    
    if not leaks:
        print("No leaks match the specified filters.", file=sys.stderr)
        return 0
    
    # Calculate statistics
    stats = calculate_statistics(leaks)
    
    # Group if requested
    groups = {}
    if args.group_by:
        groups = group_by_field(leaks, args.group_by)
    
    # Determine output destination
    if args.output:
        out = open(args.output, 'w')
    else:
        out = sys.stdout
    
    try:
        # Generate report
        if args.format == 'text':
            print_text_report(leaks, stats, groups, args, out)
        elif args.format == 'csv':
            print_csv_report(leaks, stats, groups, args, out)
        elif args.format == 'json':
            print_json_report(leaks, stats, groups, args, out)
    finally:
        if args.output:
            out.close()
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
