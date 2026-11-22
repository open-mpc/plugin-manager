#!/bin/sh
# Plugin Manager app: start the Manager's daemon, which draws the "Plugin Manager" mode slot's page and serves the web
# page on port 8790. modeslot.so runs this, detached, each time MPC starts (it sits next to the library in
# /data/mpc-addins/<id>/). Steps:
#   1. unpack the skin (pm-skin.tar) into /run/openmpc-pm-app, once per boot or when the addin was updated
#   2. if the background-server unit already runs this daemon, nothing more to do
#   3. otherwise stop whatever runs in that unit (the Plugin Manager plugin's own background server: the app wins, one
#      daemon only, as both use port 8790 and the same work folder) and start this one in it
D=$(cd "$(dirname "$0")" && pwd)
R=/run/openmpc-pm-app
U=openmpc-pm-daemon
BIN="$D/openmpc-pm-daemon"
log() { echo "pm-app: $*" >&2; }
[ -x "$BIN" ] || { log "no $BIN"; exit 1; }

mkdir -p "$R"
if [ ! -f "$R/skin.ok" ] || [ "$D/pm-skin.tar" -nt "$R/skin.ok" ]; then
    rm -rf "$R/Plugin Skins" "$R/skin.ok"
    tar xf "$D/pm-skin.tar" -C "$R" && touch "$R/skin.ok" || { log "could not unpack the skin"; exit 1; }
fi

pid=$(systemctl show -p MainPID --value "$U" 2>/dev/null)
if [ -n "$pid" ] && [ "$pid" != 0 ]; then
    [ "$(readlink "/proc/$pid/exe")" = "$BIN" ] && exit 0
    log "stopping the other daemon in $U ($(readlink "/proc/$pid/exe"))"
fi
systemctl stop "$U" 2>/dev/null
systemctl reset-failed "$U" 2>/dev/null
systemd-run --unit="$U" --collect --quiet --setenv=PM_PAGE_SKIN="$R/Plugin Skins" "$BIN" && log "started $BIN"
