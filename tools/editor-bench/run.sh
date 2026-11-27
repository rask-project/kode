#!/bin/bash
# Runs kode-editor-bench on 1, 10 and 100 MB of code with both editors and
# prints a Markdown table (paste it into docs/editor-bench.md).
#
# Usage: tools/editor-bench/run.sh [BUILD_DIR]
#        MODES="editorview minimap" tools/editor-bench/run.sh …   (some cases only)
# Modes: textedit, editorview, minimap (EditorView with the minimap beside it).
# The files are made from Kode's and rask's sources, repeated, in
# $TMPDIR/kode-bench. Each case runs in its own process and window.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build/Qt_6_12_0_for_macOS_Debug}
BENCH=$BUILD/tools/editor-bench/kode-editor-bench
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

echo "| Editor | Size (MB) | Open to first frame (ms) | Keystroke update avg (ms) | Keystroke update max (ms) | Keystroke to screen max (ms) | Return update avg (ms) | Return update max (ms) | Scroll jump update avg (ms) | Scroll jump update max (ms) | Short scroll update avg (ms) | Short scroll update max (ms) | Memory (MB) |"
echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
for mb in 1 10 100; do
    for mode in ${MODES:-textedit editorview}; do
        read -r m size open kavg kmax kframe ravg rmax savg smax tavg tmax rss <<<"$("$BENCH" "$mode" "$DIR/code-${mb}mb.txt")"
        echo "| $m | $size | $open | $kavg | $kmax | $kframe | $ravg | $rmax | $savg | $smax | $tavg | $tmax | $rss |"
    done
done
