#!/bin/bash

# Check if the executable path is provided
# strace 
# sudo bash flame_graph.sh ./PartialDecoderTest ./threadpool.svg
if [ -z "$1" ]; then
    echo "Usage: $0 <executable>"
    exit 1
fi

# Check if the output path is provided
if [ -z "$2" ]; then
    echo "Usage: $0 <executable> <output_path>"
    exit 1
fi

EXECUTABLE="$1"
OUTPUT_PATH="$2"

# Change directory to build directory
cd ../build || { echo "Failed to change directory to ../build"; exit 1; }

# Record performance data
perf record -F 99 -g -e cpu-clock --call-graph dwarf -- "$EXECUTABLE" -- sleep 300

# Check if perf recording was successful
if [ $? -ne 0 ]; then
    echo "perf record failed."
    exit 1
fi

# Convert perf.data to perf.unfold
perf script -i perf.data &> perf.unfold

# Collapse stack traces
../../FlameGraph/stackcollapse-perf.pl perf.unfold &> perf.folded

# Generate flame graph
../../FlameGraph/flamegraph.pl perf.folded > "$OUTPUT_PATH"

echo "Flame graph generated at $OUTPUT_PATH"