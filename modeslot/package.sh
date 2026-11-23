#!/bin/sh
# Build the Mode Slot addin zip: the library, the fallback page, the Open MPC logo as its icon, and the shared addin installer
# (mpc-vst-plugins tools/release/addin and tools/release_addin.py, docs/ADDINS.md there; MPC_VST points at that checkout,
# as for vst/build.sh).
#   modeslot/package.sh VERSION [OUT_DIR] [aarch64|armv7]     (default OUT_DIR: dist; default arch: aarch64 = Gen2; armv7 =
#   Gen1 MPC and Force, MPC OS 3.10.1: the same zip serves both, the shim picks the profile by build-id)
set -eu
cd "$(dirname "$0")"
ARCH=${3:-aarch64}
MPC_VST="${MPC_VST:-$(pwd)/../../mpc-vst-plugins}"
[ -f "$MPC_VST/tools/release_addin.py" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
VERSION=${1:?usage: package.sh VERSION [OUT_DIR]}
OUT=$(cd "$(dirname "${2:-dist}")" 2>/dev/null && pwd)/$(basename "${2:-dist}")
case "$ARCH" in
aarch64) ./build.sh test; ./build.sh; SO=src/modeslot.so ;;
armv7)   ./build.sh test32; ./build.sh arm; SO=src/modeslot-armv7.so ;;
*) echo "arch: aarch64 or armv7" >&2; exit 1 ;;
esac
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp addin/addin.manifest "$STAGE/"
cp "$SO" "$STAGE/modeslot.so"
python3 make_page.py "$STAGE/page.png" >/dev/null
python3 make_page.py --icon "$STAGE/icon.png" addin/openmpc-logo.png >/dev/null
python3 "$MPC_VST/tools/release_addin.py" --dir "$STAGE" --version "$VERSION" --license MIT --repo open-mpc/plugin-manager \
    --about "Turns MPC's unused mode slot 0x2090 into a \"Plugin Manager\" MENU tile: the Open MPC Plugin Manager, full screen (Gen2; Gen1 MPC and Force: MPC 3.10.1 only)." \
    -o "$OUT"
