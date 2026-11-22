#!/bin/sh
# Build the Mode Slot addin zip: the library, the fallback page, the Open MPC logo as its icon, and the shared addin installer
# (mpc-vst-plugins tools/release/addin and tools/release_addin.py, docs/ADDINS.md there; MPC_VST points at that checkout,
# as for vst/build.sh).
#   modeslot/package.sh VERSION [OUT_DIR]     (default OUT_DIR: dist)
set -eu
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$(pwd)/../../mpc-vst-plugins}"
[ -f "$MPC_VST/tools/release_addin.py" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
VERSION=${1:?usage: package.sh VERSION [OUT_DIR]}
OUT=$(cd "$(dirname "${2:-dist}")" 2>/dev/null && pwd)/$(basename "${2:-dist}")
./build.sh test
./build.sh
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp addin/addin.manifest src/modeslot.so "$STAGE/"
python3 make_page.py "$STAGE/page.png" >/dev/null
python3 make_page.py --icon "$STAGE/icon.png" addin/openmpc-logo.png >/dev/null
python3 "$MPC_VST/tools/release_addin.py" --dir "$STAGE" --version "$VERSION" --license MIT --repo open-mpc/plugin-manager \
    --about "Turns MPC's unused mode slot 0x2090 into a \"Plugin Manager\" MENU tile: the Open MPC Plugin Manager, full screen (Gen2, MPC 3.10.1 only)." \
    -o "$OUT"
