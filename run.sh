#!/usr/bin/env bash
# Runs a scheduler and rebuilds the trace viewers from its trace.
#
#   ./run.sh <a|b|c> [--layout row_major|tile_major] [--dramsys-config <path>] [--print]
#
# Every option after the scheduler is passed to the scheduling binary.

set -euo pipefail
cd "$(dirname "$0")"

if [ $# -lt 1 ]; then
    sed -n '2,6p' "$0" | cut -c3-
    exit 1
fi

scheduler=$1
shift

cmake --build build -j

# The same trace name main.cpp derives, so the deps CSV and the viewers pair up with it.
layout=row_major
for ((i = 1; i <= $#; i++)); do
    case "${!i}" in
        --layout) j=$((i + 1)); layout=${!j} ;;
        --layout=*) layout=${!i#--layout=} ;;
    esac
done
trace=trace_scheduler_${scheduler}
[ "$layout" = tile_major ] && trace=${trace}_tile_major
trace=${trace}.csv

./build/testbench/scheduling/scheduling --scheduler "$scheduler" --trace "$trace" "$@"

python3 testbench/trace_viewer/update_pipeline_timeline.py --trace "$trace"
python3 testbench/trace_viewer/update_controller_viewer.py --trace "$trace"
python3 testbench/trace_viewer/update_memory_arbiter_viewer.py --trace "$trace"
