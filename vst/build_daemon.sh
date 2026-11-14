#!/usr/bin/env bash
# Build the OpenMPC Plugin Manager background daemon.
#   vst/build_daemon.sh [aarch64|armv7|all]   (default: aarch64)
# Output:
#   vst/build/daemon/openmpc-pm-daemon          aarch64 (Gen2, MPC OS 3.10.x)
#   vst/build/daemon/openmpc-pm-daemon-armv7    armv7  (Gen1/Force, MPC OS 3.9.x)
#
# Reuses the SAME cross containers as the .so in tools/build_port.sh
# (arm64v8/gcc:12-bookworm; arm32v7/gcc:11-bullseye for a low glibc floor). The daemon
# is self-contained: it #includes src/manager.c (which #includes src/web.c); no generated
# params.h, no VST wrapper.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
out="$here/build/daemon"
want="${1:-aarch64}"
mkdir -p "$out"
U="$(id -u):$(id -g)"

build() {   # <docker image> <platform> <output basename>
    local img="$1" plat="$2" name="$3"
    docker run --rm --platform "$plat" -u "$U" -v "$repo":/b -w /b "$img" bash -euc "
        gcc -DPM_DAEMON -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
            -Isrc src/daemon.c -lpthread -ldl -lm -Wl,--no-undefined \
            -o vst/build/daemon/$name
        strip vst/build/daemon/$name
        echo \"built $name: \$(readelf -h vst/build/daemon/$name | awk '/Machine:/{print \$2,\$3}')\"
        echo \"highest glibc: \$(readelf -V vst/build/daemon/$name | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)\"
    "
    md5sum "$out/$name"
}

[ "$want" = aarch64 ] || [ "$want" = all ] && build arm64v8/gcc:12-bookworm  linux/arm64   openmpc-pm-daemon
[ "$want" = armv7 ]   || [ "$want" = all ] && build arm32v7/gcc:11-bullseye linux/arm/v7 openmpc-pm-daemon-armv7
true
