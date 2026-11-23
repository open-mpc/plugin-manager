#!/bin/sh
# Build src/modeslot.so for Gen2 (aarch64) and src/modeslot-armv7.so for Gen1 MPC and Force (32-bit ARM), and run the
# offline tests under qemu.
#   ./build.sh          Gen2 library
#   ./build.sh arm      32-bit library (src/modeslot-armv7.so)
#   ./build.sh test     aarch64 test of the hooks against stand-in functions (Docker + qemu binfmt)
#   ./build.sh test32   the same on 32-bit ARM
#   ./build.sh check MPC-BINARY   check the profile against an MPC binary on disk (python3 -I)
#   ./build.sh demo   demo/pagedemo, a test app for the live page host
set -eu
cd "$(dirname "$0")"
IMG=arm64v8/gcc:12-bookworm
IMG32=arm32v7/gcc:12-bookworm
FLAGS='-O2 -Wall -Wextra -std=gnu11 -fPIC -fvisibility=hidden -mbranch-protection=standard'
FLAGS32='-O2 -Wall -Wextra -std=gnu11 -fPIC -fvisibility=hidden -marm'

case "${1:-}" in
test)
    docker run --rm --user "$(id -u):$(id -g)" --platform linux/arm64 -v "$PWD/src":/b -w /b $IMG bash -euc \
        "gcc $FLAGS -Wl,--build-id test_modeslot.c test_arch_a64.c modeslot.c arch_a64.c page_host.c hook.S test_fake.S -o /tmp/t && /tmp/t &&
         gcc $FLAGS -shared modeslot.c arch_a64.c page_host.c hook.S -o /tmp/modeslot.so &&
         mkdir -p /tmp/x && echo 'int main(void){return 0;}' > /tmp/x/m.c && gcc -Wl,--build-id=none /tmp/x/m.c -o /tmp/x/MPC &&
         LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1 | grep -q '^modeslot: refused: no build-id' &&
         gcc -Wl,--build-id /tmp/x/m.c -o /tmp/x/MPC &&
         LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1 | grep -q '^modeslot: refused: unknown MPC build-id' &&
         cp /tmp/x/MPC /tmp/x/other && test -z \"\$(LD_PRELOAD=/tmp/modeslot.so /tmp/x/other 2>&1)\" &&
         test -z \"\$(OMM_DISABLE=1 LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1)\" &&
         echo 'constructor: refuses an unknown MPC, silent for other programs and with OMM_DISABLE'"
    ;;
test32)
    docker run --rm --user "$(id -u):$(id -g)" --platform linux/arm/v7 -v "$PWD/src":/b -w /b $IMG32 bash -euc \
        "gcc $FLAGS32 -Wl,--build-id test_modeslot.c test_arch_a32.c modeslot.c arch_a32.c hooks_a32.c page_host.c test_fake_a32.S -o /tmp/t && stdbuf -o0 /tmp/t &&
         gcc $FLAGS32 -shared modeslot.c arch_a32.c hooks_a32.c page_host.c -o /tmp/modeslot.so &&
         mkdir -p /tmp/x && echo 'int main(void){return 0;}' > /tmp/x/m.c && gcc -Wl,--build-id=none /tmp/x/m.c -o /tmp/x/MPC &&
         LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1 | grep -q '^modeslot: refused: no build-id' &&
         gcc -Wl,--build-id /tmp/x/m.c -o /tmp/x/MPC &&
         LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1 | grep -q '^modeslot: refused: unknown MPC build-id' &&
         cp /tmp/x/MPC /tmp/x/other && test -z \"\$(LD_PRELOAD=/tmp/modeslot.so /tmp/x/other 2>&1)\" &&
         test -z \"\$(OMM_DISABLE=1 LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1)\" &&
         echo 'constructor: refuses an unknown MPC, silent for other programs and with OMM_DISABLE'"
    ;;
arm)
    docker run --rm --user "$(id -u):$(id -g)" --platform linux/arm/v7 -v "$PWD/src":/b -w /b $IMG32 bash -euc \
        "gcc $FLAGS32 -shared -Wl,-z,now,-z,relro modeslot.c arch_a32.c hooks_a32.c page_host.c -o modeslot-armv7.so && strip modeslot-armv7.so"
    ;;
check)
    python3 -I check_profile.py "$2"
    ;;
demo)   # demo/pagedemo, the page host's test app (aarch64)
    docker run --rm --user "$(id -u):$(id -g)" --platform linux/arm64 -v "$PWD/demo":/b -w /b $IMG bash -euc \
        "gcc -O2 -Wall -Wextra -std=gnu11 pagedemo.c -o pagedemo && strip pagedemo"
    ;;
*)
    docker run --rm --user "$(id -u):$(id -g)" --platform linux/arm64 -v "$PWD/src":/b -w /b $IMG bash -euc \
        "gcc $FLAGS -shared -Wl,-z,now,-z,relro modeslot.c arch_a64.c page_host.c hook.S -o modeslot.so && strip modeslot.so"
    ;;
esac
