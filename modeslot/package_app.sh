#!/bin/sh
# Build the Plugin Manager app: an addin that is the whole Manager without the plugin. The mode slot (modeslot.so) gives
# it a "Plugin Manager" MENU tile; its own copy of the daemon (the engine, the web page on :8790 and the page the slot
# shows) runs in the background-server unit, started by app/pm-app-start.sh each time MPC starts. Gen2, MPC 3.10.1.
#   modeslot/package_app.sh [OUT_DIR] [aarch64|armv7]     (default OUT_DIR: dist; default arch: aarch64 = Gen2; armv7 = Gen1
#   MPC and Force; the version is the Manager's, PM_VERSION)
# Needs vst/build_themes.sh aarch64 first (the skin and page table; the Gen2 daemon), and for armv7 also
# vst/build_daemon.sh armv7 and vst/build_splash.sh armv7; and MPC_VST (mpc-vst-plugins tools), as package.sh.
set -eu
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$(pwd)/../../mpc-vst-plugins}"
[ -f "$MPC_VST/tools/release_addin.py" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
VERSION=$(sed -n 's/^#define PM_VERSION "\(.*\)"/\1/p' ../src/manager.c)
OUT=$(cd "$(dirname "${1:-dist}")" 2>/dev/null && pwd)/$(basename "${1:-dist}")
ARCH=${2:-aarch64}
case "$ARCH" in
aarch64) DAEMON=../vst/build/bgserver-aarch64/openmpc-pm-daemon; SPLASH=../vst/build/bgserver-aarch64/openmpc-pm-splash ;;
armv7)   DAEMON=../vst/build/daemon/openmpc-pm-daemon-armv7;     SPLASH=../vst/build/splash/openmpc-pm-splash-armv7 ;;
*) echo "arch: aarch64 or armv7" >&2; exit 1 ;;
esac
SKIN=$(ls -d ../vst/build/skin/*/"Plugin Skins" 2>/dev/null | head -n 1)
[ -x "$DAEMON" ] && [ -x "$SPLASH" ] && [ -n "$SKIN" ] && [ -f "$SKIN/openmpc_page.txt" ] ||
    { echo "build the Manager first: vst/build_themes.sh aarch64 (daemon, skin, page table)" >&2; exit 1; }
case "$ARCH" in
aarch64) ./build.sh test; ./build.sh; SO=src/modeslot.so ;;
armv7)   ./build.sh test32; ./build.sh arm; SO=src/modeslot-armv7.so ;;
esac
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
cp "$SO" "$STAGE/modeslot.so"
cp "$DAEMON" "$STAGE/openmpc-pm-daemon"
cp "$SPLASH" "$STAGE/openmpc-pm-splash"
cp app/pm-app-start.sh ../vst/TitilliumWeb-SemiBold.ttf ../vst/TitilliumWeb-Bold.ttf \
   ../vst/OFL-TitilliumWeb.txt "$STAGE/"
tar cf "$STAGE/pm-skin.tar" -C "$(dirname "$SKIN")" "Plugin Skins"
python3 make_page.py "$STAGE/page.png" >/dev/null
python3 make_page.py --icon "$STAGE/icon.png" addin/openmpc-logo.png >/dev/null
cp app/addin.manifest "$STAGE/"
python3 "$MPC_VST/tools/release_addin.py" --dir "$STAGE" --version "$VERSION" --license MIT --repo open-mpc/plugin-manager \
    --about "The Open MPC Plugin Manager as an app: a Plugin Manager tile in MENU, full screen, and the web page on port 8790, without the plugin (Gen2, Gen1 MPC and Force: MPC OS 3.10.1)." \
    --requires "MPC OS 3.10.1. Addin Restore (PATCHES in the Plugin Manager, installed with this app by the Manager) so the app comes back after a reboot." \
    --conflicts modeslot \
    -o "$OUT"
