#!/bin/bash
# Runs kode-work-bench on 1, 10 and 100 MB of code (the files run.sh makes)
# and prints a Markdown table (for docs/editor-bench.md).
#
# Usage: tools/editor-bench/run-work.sh [BUILD_DIR]
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build/Qt_6_12_0_for_macOS_Debug}
BENCH=$BUILD/tools/editor-bench/kode-work-bench
[[ -x $BENCH ]] || { echo "not built: $BENCH" >&2; exit 1; }
DIR=${TMPDIR:-/tmp}/kode-bench
mkdir -p "$DIR"

seed=$DIR/seed.txt
find "$ROOT/src" "$ROOT/external/rask/src" -name '*.cpp' -o -name '*.h' -o -name '*.mm' |
    sort | xargs cat >"$seed"
for mb in 1 10 100; do
    file=$DIR/code-${mb}mb.txt
    [[ -f $file ]] && continue
    : >"$file"
    while (($(stat -f %z "$file") < mb * 1024 * 1024)); do cat "$seed" >>"$file"; done
done

echo "| Size (MB) | Read (ms) | Decode (ms) | Set text (ms) | Open, blocking (ms) | Open, until shown (ms) | Guess at the end (ms) | Highlight all (ms) | Search (ms) | Search, sliced (ms) | Replace All (ms) | Undo (ms) | Re-indent all (ms) | Save (ms) |"
echo "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
for mb in 1 10 100; do
    read -r size read decode set loadblocking load guess highlight search sliced replace undo reindent save <<<"$(QT_QPA_PLATFORM=offscreen "$BENCH" "$DIR/code-${mb}mb.txt")"
    echo "| $size | $read | $decode | $set | $loadblocking | $load | $guess | $highlight | $search | $sliced | $replace | $undo | $reindent | $save |"
done
