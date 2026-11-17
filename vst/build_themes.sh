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

# Background-server payload (Gen2 only): the daemon binary + the launcher .so + the SIGNED device script, in vst/build/bgserver-aarch64/.
# A local aarch64 build also copies it into each plugin folder (bgserver/), so the SETTINGS "Background server" toggle can install it; a
# release (arch "all") does not: the skin folder is shared by the armv7 and aarch64 zips, so the release workflow adds the folder to the
# aarch64 zip only (extra_aarch64 in plugin-manager.yml). The .ini's checksum covers the .lua only (daemon/README.md), is committed, and
# needs no re-signing when the binaries are rebuilt. Without a checksum the payload is skipped (the SETTINGS toggle says "Gen2 only").
if [ "$arch" = aarch64 ] || [ "$arch" = all ]; then
    ds="$here/../daemon/device-script"
    bgout="$here/build/bgserver-aarch64"
    if grep -q '^checksum=[0-9a-f]\{64\}$' "$ds/openmpc_pm_launch.ini" 2>/dev/null; then
        "$here/build_daemon.sh" aarch64
        U="$(id -u):$(id -g)"
        docker run --rm --platform linux/arm64 -u "$U" -v "$here/..":/b -w /b arm64v8/gcc:12-bookworm bash -euc '
            gcc -shared -fPIC -O2 -Wall -o daemon/device-script/openmpc_pm_launch.so daemon/device-script/openmpc_pm_launch.c
            strip daemon/device-script/openmpc_pm_launch.so'
        rm -rf "$bgout"; mkdir -p "$bgout"
        cp "$here/build/daemon/openmpc-pm-daemon" "$ds/openmpc_pm_launch.so" "$ds/openmpc_pm_launch.lua" "$ds/openmpc_pm_launch.ini" "$bgout/"
        echo "bgserver payload in $bgout: $(ls "$bgout" | tr '\n' ' ')"
        if [ "$arch" = aarch64 ]; then
            for d in "$here"/build/skin/*/; do rm -rf "$d/bgserver"; cp -r "$bgout" "$d/bgserver"; done
        fi
    else
        echo "NOTE: the device script is not signed (no checksum in daemon/device-script/openmpc_pm_launch.ini): no bgserver payload"
    fi
fi

# Background-server payload (Gen1): the armv7 daemon alone, run from a systemd unit the plugin writes (daemon/install.sh install_force).
# Built into vst/build/bgserver-armv7/; the release workflow adds it to the armv7 zip as bgserver/ (extra_armv7).
if [ "$arch" = armv7 ] || [ "$arch" = all ]; then
    "$here/build_daemon.sh" armv7
    bgout="$here/build/bgserver-armv7"
    rm -rf "$bgout"; mkdir -p "$bgout"
    cp "$here/build/daemon/openmpc-pm-daemon-armv7" "$bgout/openmpc-pm-daemon"
    echo "bgserver payload in $bgout: $(ls "$bgout" | tr '\n' ' ')"
    if [ "$arch" = armv7 ]; then
        for d in "$here"/build/skin/*/; do rm -rf "$d/bgserver"; cp -r "$bgout" "$d/bgserver"; done
    fi
fi
