#!/bin/sh
# Copies what the site shows from the repository into site/assets, so the
# screenshots and icons are kept in one place (docs/images, assets/).
# Run by .github/workflows/pages.yml before the build, and by tools/site-serve.sh.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
assets="$root/site/assets"
rm -rf "$assets/screenshots" "$assets/icons"
mkdir -p "$assets/screenshots" "$assets/icons"
cp "$root"/docs/images/* "$assets/screenshots/"
cp "$root"/assets/settings/*.svg "$assets/icons/"
cp "$root/assets/kode.png" "$assets/kode.png"
# Icons only the site uses (features the Settings window has no section for).
cp "$root"/site/_icons/*.svg "$assets/icons/"
