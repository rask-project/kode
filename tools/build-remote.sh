#!/bin/sh
# Builds kode-remote, statically linked against musl: run in an Alpine
# container (CI does, .github/workflows/remote-agents.yml), for the host's
# architecture.
#
# Usage: tools/build-remote.sh VERSION OUTPUT_FOLDER
#   VERSION        the Kode version the agent goes with (its folder and socket on hosts)
#   OUTPUT_FOLDER  where <os>-<arch>/kode-remote is written
set -eu

# A tag ("v1.2.0") or a version ("1.2.0").
VERSION=${1#v}
OUT=$2
cd "$(dirname "$0")/.."

apk add --no-cache build-base cmake file ninja linux-headers python3 >/dev/null
cmake -S remote -B build/remote -G Ninja -DCMAKE_BUILD_TYPE=Release -DKODE_DISPLAY_VERSION="$VERSION"
cmake --build build/remote --target kode-remote

ARCH=$(uname -m)
[ "$ARCH" = aarch64 ] && ARCH=arm64
mkdir -p "$OUT/linux-$ARCH"
cp build/remote/kode-remote "$OUT/linux-$ARCH/kode-remote"
strip "$OUT/linux-$ARCH/kode-remote"

# Static: it runs on any Linux of this architecture.
if file "$OUT/linux-$ARCH/kode-remote" 2>/dev/null | grep -q dynamic; then
    echo "build-remote.sh: kode-remote is not static" >&2
    exit 1
fi
python3 -I tools/remote-smoke.py "$OUT/linux-$ARCH/kode-remote" "$VERSION"
