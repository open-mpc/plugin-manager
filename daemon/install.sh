#!/usr/bin/env bash
# install.sh — build, sign, and install the OpenMPC Plugin Manager background daemon on a
# Gen2 MPC (MPC OS 3.10.x). Host-driven: runs on your machine and operates on the device
# over ssh. The daemon serves the manager's LAN web UI on http://<device>:8790 whenever the
# device is on, with or without the Plugin Manager VST loaded on a track.
#
#   daemon/install.sh build                 cross-build the daemon binary + launcher .so (aarch64, Docker)
#   daemon/install.sh sign                  sign the device script (DEV ONLY; needs the private 3.10.0.49 MPC binary)
#   daemon/install.sh install  <device-ip>  deploy the signed script + binary to the device
#   daemon/install.sh install  <device-ip> --restart   ...and restart MPC so it takes effect now
#   daemon/install.sh status   <device-ip>  report what is installed / running
#   daemon/install.sh verify   <device-ip>  is the daemon serving :8790
#   daemon/install.sh uninstall <device-ip> stop + remove it from the device
#
# How it works on Gen2 (MPC OS 3.10.x), which has a read-only verity rootfs (no systemd unit
# can persist): the daemon is launched by a signed MPC Lua *device script* that MPC runs at
# every start (the Addin Restore pattern). The script's .so starts the daemon in its own
# `systemd-run` scope so it survives the addin-restore acvs restart. Self-contained: it needs
# NO Addin Restore and puts NOTHING in MPC's own process. See daemon/README.md.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
DS="$HERE/device-script"                                   # openmpc_pm_launch.{c,so,lua,ini}
BIN_LOCAL="$REPO/vst/build/daemon/openmpc-pm-daemon"             # aarch64 daemon (Gen2)
BIN_LOCAL_ARMV7="$REPO/vst/build/daemon/openmpc-pm-daemon-armv7"  # armv7 daemon (Gen1/Force)

# Signing (DEV ONLY). The salt is read from a 3.10.0.49 MPC binary and is unchanged in
# 3.10.1.8; only the resulting checksum is shipped, never the salt.
SIGN_TOOL="${SIGN_TOOL:-$HOME/forcelink/tools/sign_script.py}"
SIGN_MPC="${SIGN_MPC:-$HOME/forcelink/local/MPC}"

# Device paths.
DEV_SCRIPT_DIR="/nvme/MPC Documents/Devices/OpenMPC PM Server"
DEV_BIN_DIR="/data/openmpc-pm"
DEV_BIN="$DEV_BIN_DIR/openmpc-pm-daemon"
UNIT="openmpc-pm-daemon"
PORT=8790
SSH="ssh -o BatchMode=yes -o ConnectTimeout=6"
SCP="scp -O -o BatchMode=yes"

die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
info() { printf '%s\n' "$*"; }

need_ip() { [ -n "${1:-}" ] || die "a device IP is required (e.g. $0 $CMD 192.168.2.217)"; }

is_signed() { grep -q '^checksum=[0-9a-f]\{64\}$' "$DS/openmpc_pm_launch.ini" 2>/dev/null; }

cmd_build() {
    command -v docker >/dev/null || die "Docker is required to cross-build aarch64"
    info "Building daemon binary (aarch64)…"
    "$REPO/vst/build_daemon.sh"
    info "Building launcher .so (aarch64)…"
    local U; U="$(id -u):$(id -g)"
    docker run --rm --platform linux/arm64 -u "$U" -v "$REPO":/b -w /b arm64v8/gcc:12-bookworm bash -euc '
        gcc -shared -fPIC -O2 -Wall -o daemon/device-script/openmpc_pm_launch.so daemon/device-script/openmpc_pm_launch.c
        strip daemon/device-script/openmpc_pm_launch.so'
    info "Built: $BIN_LOCAL"
    info "Built: $DS/openmpc_pm_launch.so"
    info "Next: $0 sign    (then: $0 install <device-ip>)"
}

cmd_sign() {
    [ -f "$SIGN_TOOL" ] || die "signer not found: $SIGN_TOOL (set SIGN_TOOL=)"
    [ -r "$SIGN_MPC" ]  || die "3.10.0.49 MPC binary not found: $SIGN_MPC (set SIGN_MPC=)"
    [ -f "$DS/openmpc_pm_launch.lua" ] || die "missing $DS/openmpc_pm_launch.lua"
    # Re-sign cleanly: drop any existing checksum line first.
    sed -i '/^checksum=/d' "$DS/openmpc_pm_launch.ini"
    local sum
    sum="$(python3 -I "$SIGN_TOOL" "$SIGN_MPC" "$DS/openmpc_pm_launch.lua" "$DS/openmpc_pm_launch.ini")"
    is_signed || die "signing failed (no checksum written)"
    info "Signed openmpc_pm_launch.ini  checksum=$sum"
    info "(The .ini checksum covers ONLY the .lua; rebuilding the .so does not require re-signing.)"
}

preflight_install() {
    [ -x "$BIN_LOCAL" ]                       || die "daemon binary missing — run: $0 build"
    [ -f "$DS/openmpc_pm_launch.so" ]         || die "launcher .so missing — run: $0 build"
    [ -f "$DS/openmpc_pm_launch.lua" ]        || die "launcher .lua missing"
    is_signed                                 || die "device script is unsigned — run: $0 sign"
    # the deployed .lua must match the signed checksum, so verify the pair is coherent locally
    local luasha; luasha=$(sha256sum "$DS/openmpc_pm_launch.lua" | cut -d' ' -f1)
    info "local artifacts ok (lua sha ${luasha:0:12}…, ini signed)"
}

cmd_install() {
    local ip="$1"; need_ip "$ip"; shift || true
    local arch
    arch=$($SSH "root@$ip" 'uname -m') || die "cannot ssh root@$ip"
    case "$arch" in
        aarch64) install_gen2  "$ip" "$@" ;;   # Gen2: signed MPC device script + systemd-run
        armv7l|armv7|arm) install_force "$ip" "$@" ;;   # Gen1/Force: plain systemd service (writable /etc)
        *) die "unsupported arch: $arch" ;;
    esac
}

# Gen1/Force (armv7, MPC OS 3.9.x): the rootfs has a writable /etc and systemd, so the daemon
# runs as a normal systemd service — no MPC device script, no signing, no cgroup tricks. systemd
# starts it at boot and restarts it on failure, independent of MPC.
install_force() {
    local ip="$1"; need_ip "$ip"; shift || true
    local restart=0; for a in "$@"; do [ "$a" = --restart ] && restart=1; done
    [ -x "$BIN_LOCAL_ARMV7" ] || die "armv7 daemon missing — run: vst/build_daemon.sh armv7"
    info "Installing on Force ($ip) as a systemd service…"
    $SSH "root@$ip" 'rm -rf /tmp/openmpc-pm-stage && mkdir -p /tmp/openmpc-pm-stage'
    $SCP "$BIN_LOCAL_ARMV7" "root@$ip:/tmp/openmpc-pm-stage/openmpc-pm-daemon" >/dev/null
    $SSH "root@$ip" "DEV_BIN_DIR='$DEV_BIN_DIR' DEV_BIN='$DEV_BIN' UNIT='$UNIT' RESTART='$restart' sh -s" <<'REMOTE'
set -eu
mkdir -p "$DEV_BIN_DIR"
cp /tmp/openmpc-pm-stage/openmpc-pm-daemon "$DEV_BIN"; chmod 755 "$DEV_BIN"; rm -rf /tmp/openmpc-pm-stage
# The unit and its boot (wants) symlink go into the ROOT IMAGE (/usr/lib/systemd/system), not /etc:
# /etc here is an overlay (upperdir on /data) that systemd does not see when it computes the boot
# transaction, so a unit enabled in /etc never starts at boot (acvs works only because its symlink
# is in the baked image). / is plain ro ext4 (not verity), so remount it rw to write, then restore.
RO=0; case ",$(findmnt -rn -M / -o OPTIONS)," in *,ro,*) RO=1; mount -o remount,rw / ;; esac
IMG=/usr/lib/systemd/system
mkdir -p "$IMG/multi-user.target.wants"
cat > "$IMG/$UNIT.service" <<UNIT
[Unit]
Description=OpenMPC Plugin Manager background server (LAN web UI on :8790)
# No network-online.target dependency (inactive on this device; would drop the start job). The
# daemon tolerates late networking: web.c re-reads the LAN IP every few seconds.
After=data.mount
[Service]
Type=simple
ExecStart=$DEV_BIN
Restart=always
RestartSec=5
Nice=10
IOSchedulingClass=idle
[Install]
WantedBy=multi-user.target
UNIT
ln -sf ../$UNIT.service "$IMG/multi-user.target.wants/$UNIT.service"
# drop any stale /etc-overlay enablement from an earlier attempt
rm -f /etc/systemd/system/$UNIT.service /etc/systemd/system/multi-user.target.wants/$UNIT.service
sync
[ "$RO" = 1 ] && mount -o remount,ro / || true
systemctl daemon-reload
# Replace any running instance with the new binary: stop the unit, clear a failed unit, and reap any
# straggler still holding :8790 that is not in the unit (^ anchors pgrep to the daemon's own argv[0],
# so it never matches this shell). Otherwise an old instance keeps the port and the new binary never runs.
systemctl stop $UNIT.service 2>/dev/null || true
systemctl reset-failed $UNIT.service 2>/dev/null || true
for p in $(pgrep -f "^$DEV_BIN" 2>/dev/null); do kill "$p" 2>/dev/null || true; done
sleep 1
for p in $(pgrep -f "^$DEV_BIN" 2>/dev/null); do kill -9 "$p" 2>/dev/null || true; done
systemctl start $UNIT.service
echo "  binary:  $DEV_BIN"
echo "  unit:    $IMG/$UNIT.service (+ boot wants symlink in the root image)"
echo "  active:  $(systemctl is-active $UNIT 2>/dev/null)"
REMOTE
    info "Installed on Force."
    sleep 3; cmd_verify "$ip"
}

install_gen2() {
    local ip="$1"; need_ip "$ip"; shift || true
    local restart=0
    for a in "$@"; do [ "$a" = --restart ] && restart=1; done
    preflight_install
    local ver
    ver=$($SSH "root@$ip" 'strings -n4 /usr/bin/MPC 2>/dev/null | grep -oE "3\.1[0-9]\.[0-9]+(\.[0-9]+)?" | sort -uV | tail -1' || true)
    case "$ver" in 3.10.*) : ;; *) info "WARN: MPC version reads '$ver' (expected 3.10.x); continuing" ;; esac

    info "Staging files on $ip…"
    $SSH "root@$ip" 'rm -rf /tmp/openmpc-pm-stage && mkdir -p /tmp/openmpc-pm-stage'
    $SCP "$BIN_LOCAL" "$DS/openmpc_pm_launch.so" "$DS/openmpc_pm_launch.lua" "$DS/openmpc_pm_launch.ini" \
         "root@$ip:/tmp/openmpc-pm-stage/" >/dev/null

    info "Installing on device…"
    $SSH "root@$ip" "DEV_SCRIPT_DIR='$DEV_SCRIPT_DIR' DEV_BIN_DIR='$DEV_BIN_DIR' DEV_BIN='$DEV_BIN' UNIT='$UNIT' PORT='$PORT' RESTART='$restart' sh -s" <<'REMOTE'
set -eu
S=/tmp/openmpc-pm-stage
mkdir -p "$DEV_BIN_DIR"
cp "$S/openmpc-pm-daemon" "$DEV_BIN"; chmod 755 "$DEV_BIN"
mkdir -p "$DEV_SCRIPT_DIR"
cp "$S/openmpc_pm_launch.so" "$S/openmpc_pm_launch.lua" "$S/openmpc_pm_launch.ini" "$DEV_SCRIPT_DIR/"
rm -rf "$S"
echo "  binary:  $DEV_BIN"
echo "  script:  $DEV_SCRIPT_DIR/ (ini=$(grep -c '^checksum=' "$DEV_SCRIPT_DIR/openmpc_pm_launch.ini") signed)"
if [ "${RESTART:-0}" = 1 ]; then
    echo "  restarting MPC so the daemon starts now…"
    # Stop the old daemon first: the boot launcher skips spawning when :8790 is already served, so
    # without this the stale binary would keep serving after the restart. Reap any straggler too
    # (^ anchors pgrep to the daemon's own argv[0], never this shell).
    systemctl stop "$UNIT" 2>/dev/null || true
    systemctl reset-failed "$UNIT" 2>/dev/null || true
    for p in $(pgrep -f "^$DEV_BIN" 2>/dev/null); do kill "$p" 2>/dev/null || true; done
    sleep 1
    for p in $(pgrep -f "^$DEV_BIN" 2>/dev/null); do kill -9 "$p" 2>/dev/null || true; done
    rm -f /run/openmpc-pm-launch-done
    systemctl restart acvs
else
    echo "  (will start at the next MPC restart; pass --restart to start now)"
fi
REMOTE
    info "Installed."
    [ "$restart" = 1 ] && { sleep 6; cmd_verify "$ip"; } || true
}

cmd_uninstall() {
    local ip="$1"; need_ip "$ip"
    $SSH "root@$ip" "DEV_SCRIPT_DIR='$DEV_SCRIPT_DIR' DEV_BIN_DIR='$DEV_BIN_DIR' DEV_BIN='$DEV_BIN' UNIT='$UNIT' sh -s" <<'REMOTE'
set -u
systemctl stop "$UNIT" 2>/dev/null || true
kill $(pgrep -f "^$DEV_BIN") 2>/dev/null || true
rm -f /run/openmpc-pm-launch-done /tmp/openmpc-pm-launch.log
# Gen1/Force systemd unit lives in the root image (+ /etc overlay from older attempts)
RO=0; case ",$(findmnt -rn -M / -o OPTIONS 2>/dev/null)," in *,ro,*) RO=1; mount -o remount,rw / 2>/dev/null || true ;; esac
rm -f /usr/lib/systemd/system/"$UNIT".service /usr/lib/systemd/system/multi-user.target.wants/"$UNIT".service 2>/dev/null || true
rm -f /etc/systemd/system/"$UNIT".service /etc/systemd/system/multi-user.target.wants/"$UNIT".service 2>/dev/null || true
sync; [ "$RO" = 1 ] && mount -o remount,ro / 2>/dev/null || true
systemctl daemon-reload 2>/dev/null || true
rm -rf "$DEV_SCRIPT_DIR"                                         # Gen2 device script
rm -f "$DEV_BIN"; rmdir "$DEV_BIN_DIR" 2>/dev/null || true
echo "removed daemon (systemd unit and/or device script) and binary."
REMOTE
    info "Uninstalled from $ip."
}

cmd_status() {
    local ip="$1"; need_ip "$ip"
    $SSH "root@$ip" "DEV_SCRIPT_DIR='$DEV_SCRIPT_DIR' DEV_BIN='$DEV_BIN' UNIT='$UNIT' PORT='$PORT' sh -s" <<'REMOTE'
set -u
echo "arch:        $(uname -m)"
echo "MPC OS:      $(strings -n4 /usr/bin/MPC 2>/dev/null | grep -oE '3\.1[0-9]\.[0-9]+(\.[0-9]+)?' | sort -uV | tail -1)"
echo "binary:      $([ -x "$DEV_BIN" ] && echo present || echo missing)"
echo "script .ini: $([ -f "$DEV_SCRIPT_DIR/openmpc_pm_launch.ini" ] && echo "present (signed=$(grep -c '^checksum=' "$DEV_SCRIPT_DIR/openmpc_pm_launch.ini"))" || echo missing)"
echo "unit active: $(systemctl is-active "$UNIT" 2>/dev/null || echo inactive)"
echo "daemon pid:  $(pgrep -f "^$DEV_BIN" || echo none)"
echo "serving:     $(wget -q -O /dev/null "http://127.0.0.1:$PORT/" && echo yes || echo no)"
REMOTE
}

cmd_verify() {
    local ip="$1"; need_ip "$ip"
    $SSH "root@$ip" "UNIT='$UNIT' PORT='$PORT' DEV_BIN='$DEV_BIN' sh -s" <<'REMOTE'
set -u
a=$(systemctl is-active "$UNIT" 2>/dev/null || echo inactive)
echo "unit:    $a"
if wget -q -O /dev/null "http://127.0.0.1:$PORT/" 2>/dev/null; then
    online=$(wget -q -O - "http://127.0.0.1:$PORT/api/state" 2>/dev/null | tr ',' '\n' | grep -m1 '"online"')
    ip4=$(ip -4 addr show 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | grep -v '^127\.' | head -n 1)
    echo "serving: yes  ->  http://${ip4:-<device-ip>}:8790/   ($online)"
    echo "VERIFY result=ok"
else
    echo "serving: NO (:8790 not answering)"
    echo "VERIFY result=not-serving"
fi
REMOTE
}

CMD="${1:-}"; shift || true
case "$CMD" in
    build)     cmd_build ;;
    sign)      cmd_sign ;;
    install)   cmd_install "${1:-}" "${@:2}" ;;
    uninstall) cmd_uninstall "${1:-}" ;;
    status)    cmd_status "${1:-}" ;;
    verify)    cmd_verify "${1:-}" ;;
    ''|help|-h|--help) sed -n '2,20p' "$0" ;;
    *) die "unknown command: $CMD (build|sign|install|uninstall|status|verify)" ;;
esac
