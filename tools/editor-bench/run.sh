#!/bin/bash
# Runs tekst-editor-bench on 1, 10 and 100 MB of code with both editors and
# prints a Markdown table (paste it into docs/editor-bench.md).
#
# Usage: tools/editor-bench/run.sh [BUILD_DIR]
# The files are made from Tekst's and rask's sources, repeated, in
# $TMPDIR/tekst-bench. Each case runs in its own process and window.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build/Qt_6_12_0_for_macOS_Debug}
BENCH=$BUILD/tools/editor-bench/tekst-editor-bench
[[ -x $BENCH ]] || { echo "not built: $BENCH" >&2; exit 1; }
DIR=${TMPDIR:-/tmp}/tekst-bench
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

echo "| Editor | Size (MB) | Open to first frame (ms) | Keystroke work avg (ms) | Keystroke work max (ms) | Keystroke to screen max (ms) | Scroll work avg (ms) | Scroll work max (ms) | Memory (MB) |"
echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|"
for mb in 1 10 100; do
    for mode in textedit editorview; do
        read -r m size open kavg kmax kframe savg smax rss <<<"$("$BENCH" "$mode" "$DIR/code-${mb}mb.txt")"
        echo "| $m | $size | $open | $kavg | $kmax | $kframe | $savg | $smax | $rss |"
    done
done
