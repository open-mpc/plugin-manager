#!/bin/sh
# Build the Plugin Manager app: an addin that is the whole Manager without the plugin. The mode slot (modeslot.so) gives
# it a "Plugin Manager" MENU tile; its own copy of the daemon (the engine, the web page on :8790 and the page the slot
# shows) runs in the background-server unit, started by app/pm-app-start.sh each time MPC starts. Gen2, MPC 3.10.1.
#   modeslot/package_app.sh [OUT_DIR]     (default OUT_DIR: dist; the version is the Manager's, PM_VERSION)
# Needs vst/build_themes.sh aarch64 first (the daemon and the skin) and MPC_VST (mpc-vst-plugins tools), as package.sh.
set -eu
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$(pwd)/../../mpc-vst-plugins}"
[ -f "$MPC_VST/tools/release_addin.py" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
VERSION=$(sed -n 's/^#define PM_VERSION "\(.*\)"/\1/p' ../src/manager.c)
OUT=$(cd "$(dirname "${1:-dist}")" 2>/dev/null && pwd)/$(basename "${1:-dist}")
DAEMON=../vst/build/bgserver-aarch64/openmpc-pm-daemon
SKIN=$(ls -d ../vst/build/skin/*/"Plugin Skins" 2>/dev/null | head -n 1)
[ -x "$DAEMON" ] && [ -n "$SKIN" ] && [ -f "$SKIN/openmpc_page.txt" ] ||
    { echo "build the Manager first: vst/build_themes.sh aarch64 (daemon, skin, page table)" >&2; exit 1; }
./build.sh test
./build.sh
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp src/modeslot.so "$DAEMON" ../vst/build/bgserver-aarch64/openmpc-pm-splash app/pm-app-start.sh ../vst/TitilliumWeb-SemiBold.ttf ../vst/TitilliumWeb-Bold.ttf \
   ../vst/OFL-TitilliumWeb.txt "$STAGE/"
tar cf "$STAGE/pm-skin.tar" -C "$(dirname "$SKIN")" "Plugin Skins"
python3 make_page.py "$STAGE/page.png" >/dev/null
python3 make_page.py --icon "$STAGE/icon.png" addin/openmpc-logo.png >/dev/null
cp app/addin.manifest "$STAGE/"
python3 "$MPC_VST/tools/release_addin.py" --dir "$STAGE" --version "$VERSION" --license MIT --repo open-mpc/plugin-manager \
    --about "The Open MPC Plugin Manager as an app: a Plugin Manager tile in MENU, full screen, and the web page on port 8790, without the plugin (Gen2, MPC 3.10.1)." \
    --requires "Addin Restore (PATCHES in the Plugin Manager, or the addin-restore patch) so the app comes back after a reboot." \
    --conflicts modeslot \
    -o "$OUT"
