#!/bin/bash
# sudo bash perf.sh ../build/PartialDecoderTest
# 检查是否提供了可执行程序作为参数
if [ -z "$1" ]; then
    echo "Usage: $0 <executable>"
    exit 1
fi

EXECUTABLE=$1

echo "Running performance analysis on '$EXECUTABLE'..."

# 使用 perf stat 统计缓存命中率和上下文切换次数
perf stat -e cache-references,cache-misses,context-switches "$EXECUTABLE"
# perf stat -e mem_load_retired.fb_hit,mem_load_retired.l1_miss,mem_load_retired.l1_hit,mem_inst_retired.l2_miss "$EXECUTABLE"
# perf stat -e L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses "$EXECUTABLE"

echo "Performance analysis completed."