#!/bin/sh
# Run the REAL MPC from an Akai update image under user-mode emulation with the shim preloaded, and check what the shim says.
# The shim patches MPC in its constructor, so this tests, on the real binary: the build-id gate, every entry instruction, the jump
# tables, the trampoline setup and the patching, and that MPC's own startup survives it, up to where it stops for lack of a
# display. It does not run the UI (the compat suite and a device do that).
#   modeslot/real_mpc_test.sh UPDATE.img installed|refused [armv7|aarch64]     (default arch: from the image's MPC)
# Needs python3, debugfs (e2fsprogs) and Docker with user emulation (binfmt) for the CPU. The firmware is Akai's: the extracted
# root stays in a temporary folder (TMPDIR) and is removed at the end; nothing is committed.
set -eu
cd "$(dirname "$0")"
IMG=${1:?usage: real_mpc_test.sh UPDATE.img installed|refused [armv7|aarch64]}
EXPECT=${2:?installed or refused}
W=$(mktemp -d "${TMPDIR:-/var/tmp}/modeslot-real.XXXXXX")
trap 'chmod -R u+rwX "$W" 2>/dev/null; rm -rf "$W"' EXIT
python3 -I - "$IMG" "$W/rootfs.ext4" <<'PY'
import lzma, mmap, subprocess, sys
f = open(sys.argv[1], "rb"); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
pos = 0
while True:
    i = m.find(b"\xfd7zXZ\x00", pos)
    if i < 0: sys.exit("no root filesystem with /usr/bin/MPC in the image")
    pos = i + 6
    d = lzma.LZMADecompressor(); out = bytearray(); j = i
    try:
        while not d.eof and j < len(m):
            out += d.decompress(m[j:j + (1 << 20)]); j += 1 << 20
            if len(out) > 2048 and out[1080:1082] != b"\x53\xef": break    # not an ext2/3/4 filesystem
    except lzma.LZMAError:
        continue
    if not d.eof or out[1080:1082] != b"\x53\xef": continue
    open(sys.argv[2], "wb").write(out)
    r = subprocess.run(["debugfs", "-R", "stat /usr/bin/MPC", sys.argv[2]], capture_output=True, text=True)
    if "Inode:" in r.stdout: break                                           # the root filesystem
PY
R=$W/root; mkdir -p "$R"
for d in usr etc lib bin sbin var; do debugfs -R "rdump /$d $R" "$W/rootfs.ext4" >/dev/null 2>&1; done
mkdir -p "$R/proc" "$R/dev" "$R/tmp" "$R/run" "$R/sys" "$R/root"
case "$(file -b "$R/usr/bin/MPC")" in
  *"ARM aarch64"*) A=aarch64; PLAT=linux/arm64; IMGD=arm64v8/gcc:12-bookworm; SO=src/modeslot.so ;;
  *"ARM,"*)        A=armv7;   PLAT=linux/arm/v7; IMGD=arm32v7/gcc:12-bookworm; SO=src/modeslot-armv7.so ;;
  *) echo "MPC is not an ARM binary" >&2; exit 1 ;;
esac
[ -f "$SO" ] || { echo "build it first: ./build.sh$([ $A = armv7 ] && echo ' arm')" >&2; exit 1; }
cp "$SO" "$R/modeslot.so"
OUT=$(docker run --rm --privileged --platform $PLAT -v "$R":/fw $IMGD sh -c \
  'mount -t proc proc /fw/proc; mount --bind /dev /fw/dev; timeout 40 chroot /fw /usr/bin/env LD_PRELOAD=/modeslot.so /usr/bin/MPC 2>&1 | head -20' 2>&1 || true)
echo "$OUT" | grep -E 'modeslot:' | head -3
case "$EXPECT" in
  installed) echo "$OUT" | grep -q '^modeslot: installed:' && echo "ok: $A, the shim installed on the real MPC" || { echo "FAIL: not installed"; echo "$OUT" | head -8; exit 1; } ;;
  refused)   echo "$OUT" | grep -q '^modeslot: refused' && ! echo "$OUT" | grep -q '^modeslot: installed' && echo "ok: $A, the shim refused the real MPC and changed nothing" || { echo "FAIL: not refused"; echo "$OUT" | head -8; exit 1; } ;;
  *) echo "installed or refused" >&2; exit 2 ;;
esac
# MPC itself still starts: its own banner appears after the shim's line
echo "$OUT" | grep -q 'LibXtract' && echo "ok: MPC's own startup ran after it" || { echo "FAIL: MPC did not get as far as its banner"; echo "$OUT" | head -8; exit 1; }
