#!/usr/bin/env bash
# Build the update screen (src/splash.c): openmpc-pm-splash (aarch64, Gen2) and openmpc-pm-splash-armv7 (Gen1/Force)
# in vst/build/splash/, in the same cross containers as the daemon (vst/build_daemon.sh). Static, so it has no
# library needs at all while MPC is stopped.   vst/build_splash.sh [aarch64|armv7|all|host]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
want="${1:-all}"
mkdir -p "$here/build/splash"
U="$(id -u):$(id -g)"
CF='-O2 -Wall -Wextra -std=gnu11 -Isrc src/splash.c -static'
build() {   # <docker image> <platform> <output basename>
    docker run --rm --platform "$2" -u "$U" -v "$repo":/b -w /b "$1" bash -euc "
        gcc $CF -o vst/build/splash/$3 && strip vst/build/splash/$3
        echo \"built $3: \$(readelf -h vst/build/splash/$3 | awk '/Machine:/{print \$2,\$3}') \$(stat -c %s vst/build/splash/$3) bytes\""
}
[ "$want" = host ] && { cd "$repo" && gcc $CF -o vst/build/splash/splash-host && echo built splash-host; exit 0; }
[ "$want" = aarch64 ] || [ "$want" = all ] && build arm64v8/gcc:12-bookworm  linux/arm64  openmpc-pm-splash
[ "$want" = armv7 ]   || [ "$want" = all ] && build arm32v7/gcc:11-bullseye linux/arm/v7 openmpc-pm-splash-armv7
true
