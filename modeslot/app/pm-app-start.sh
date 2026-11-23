#!/bin/sh
# Plugin Manager app: start the Manager's daemon, which draws the "Plugin Manager" mode slot's page and serves the web
# page on port 8790. modeslot.so runs this, detached, each time MPC starts (it sits next to the library in
# /data/mpc-addins/<id>/). Steps:
#   1. unpack the skin (pm-skin.tar) into /run/openmpc-pm-app, once per boot or when the addin was updated
#   2. if this daemon already runs (unit openmpc-pm-app), nothing more to do
#   3. otherwise stop the Plugin Manager plugin's own background server, if it runs (unit openmpc-pm-daemon, a transient
#      unit on Gen2, a persistent one on Gen1 and Force: the app wins, one daemon only, as both use port 8790 and the
#      same work folder) and start this one in its own unit
D=$(cd "$(dirname "$0")" && pwd)
R=/run/openmpc-pm-app
U=openmpc-pm-app
PLUGIN_UNIT=openmpc-pm-daemon
BIN="$D/openmpc-pm-daemon"
log() { echo "pm-app: $*" >&2; }
[ -x "$BIN" ] || { log "no $BIN"; exit 1; }

mkdir -p "$R"
if [ ! -f "$R/skin.ok" ] || [ "$D/pm-skin.tar" -nt "$R/skin.ok" ]; then
    rm -rf "$R/Plugin Skins" "$R/skin.ok"
    tar xf "$D/pm-skin.tar" -C "$R" && touch "$R/skin.ok" || { log "could not unpack the skin"; exit 1; }
fi

pid=$(systemctl show -p MainPID --value "$U" 2>/dev/null)
if [ -n "$pid" ] && [ "$pid" != 0 ] && [ "$(readlink "/proc/$pid/exe")" = "$BIN" ]; then
    exit 0
fi
other=$(systemctl show -p MainPID --value "$PLUGIN_UNIT" 2>/dev/null)
if [ -n "$other" ] && [ "$other" != 0 ]; then
    log "stopping the plugin's background server in $PLUGIN_UNIT ($(readlink "/proc/$other/exe"))"
    systemctl stop "$PLUGIN_UNIT" 2>/dev/null
fi
systemctl stop "$U" 2>/dev/null
systemctl reset-failed "$U" 2>/dev/null
if command -v systemd-run >/dev/null 2>&1; then
    systemd-run --unit="$U" --collect --quiet --setenv=PM_PAGE_SKIN="$R/Plugin Skins" "$BIN" && log "started $BIN"
else
    PM_PAGE_SKIN="$R/Plugin Skins" nohup "$BIN" >/dev/null 2>&1 &
    log "started $BIN (no systemd-run)"
fi
