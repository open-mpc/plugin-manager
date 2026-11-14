#!/usr/bin/env bash
# Build the plugin with both skins (light is the default, the active "Plugin Skins"): the dark one (as vst/build.sh) and the light one (vst/make_light.py on a scratch copy), and put them side by side
# in the plugin folder. The manager copies the wanted one over "Plugin Skins" during a restart (DESIGN.md 6d).
#   vst/build_themes.sh [armv7|aarch64|all]
#   vst/build/skin/<folder>/Plugin Skins{,.dark,.light}/   each with a ".theme" file naming it
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$here/../../..}"
arch="${1:-aarch64}"
[ -x "$MPC_VST/tools/build_port.sh" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
"$MPC_VST/tools/build_port.sh" "$here/vst.json" "$arch"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
tar --exclude=.git --exclude=vst/build -C "$here/.." -cf - . | tar -xf - -C "$tmp"
python3 "$here/make_light.py" "$tmp/vst"
(cd "$tmp" && "$MPC_VST/tools/build_port.sh" vst/vst.json "$arch")
for d in "$here"/build/skin/*/; do
    f="$(basename "$d")"
    rm -rf "$d/Plugin Skins.dark" "$d/Plugin Skins.light"
    cp -a "$d/Plugin Skins" "$d/Plugin Skins.dark"                           # the dark skin as drawn
    echo dark > "$d/Plugin Skins.dark/.theme"
    cp -a "$tmp/vst/build/skin/$f/Plugin Skins" "$d/Plugin Skins.light"      # the light one: the default
    echo light > "$d/Plugin Skins.light/.theme"
    rm -rf "$d/Plugin Skins"; cp -a "$d/Plugin Skins.light" "$d/Plugin Skins"
    echo "themes in $d: $(du -sh "$d/Plugin Skins.dark" | cut -f1) dark, $(du -sh "$d/Plugin Skins.light" | cut -f1) light"
done

# Background-server payload (Gen2 only): ship the daemon binary + the SIGNED device script inside
# each plugin folder under bgserver/, so the SETTINGS "Background server" toggle can install it on
# the device with no SSH. aarch64 only; requires `daemon/install.sh build` + `daemon/install.sh sign`
# to have produced and signed the artifacts first.
if [ "$arch" = aarch64 ]; then
    bin="$here/build/daemon/openmpc-pm-daemon"
    ds="$here/../daemon/device-script"
    if [ -x "$bin" ] && grep -q '^checksum=[0-9a-f]\{64\}$' "$ds/openmpc_pm_launch.ini" 2>/dev/null; then
        for d in "$here"/build/skin/*/; do
            mkdir -p "$d/bgserver"
            cp "$bin" "$ds/openmpc_pm_launch.so" "$ds/openmpc_pm_launch.lua" "$ds/openmpc_pm_launch.ini" "$d/bgserver/"
            echo "bgserver payload in $d: $(ls "$d/bgserver" | tr '\n' ' ')"
        done
    else
        echo "NOTE: no signed bgserver payload (run: daemon/install.sh build && daemon/install.sh sign) — SETTINGS toggle will show 'Gen2 only'"
    fi
fi
