#!/usr/bin/env bash
# Build the OpenMPC Plugin Manager background daemon for Gen2 (aarch64 / MPC OS 3.10.1).
#   vst/build_daemon.sh
# Output: vst/build/daemon/openmpc-pm-daemon  (aarch64 ELF executable)
#
# Reuses the SAME cross container as the aarch64 .so in tools/build_port.sh
# (arm64v8/gcc:12-bookworm, device glibc 2.39). The daemon is self-contained: it
# #includes src/manager.c (which #includes src/web.c); no generated params.h, no
# VST wrapper. Gen2 only — not built for Gen1/armv7 on purpose.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
out="$here/build/daemon"
mkdir -p "$out"
U="$(id -u):$(id -g)"

docker run --rm --platform linux/arm64 -u "$U" -v "$repo":/b -w /b arm64v8/gcc:12-bookworm bash -euc '
  gcc -DPM_DAEMON -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
      -Isrc src/daemon.c -lpthread -ldl -lm -Wl,--no-undefined \
      -o vst/build/daemon/openmpc-pm-daemon
  strip vst/build/daemon/openmpc-pm-daemon
  echo "built: $(file -b vst/build/daemon/openmpc-pm-daemon 2>/dev/null || echo aarch64 ELF)"
  echo "highest glibc: $(readelf -V vst/build/daemon/openmpc-pm-daemon | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1) (device has 2.39)"
'
md5sum "$out/openmpc-pm-daemon"
