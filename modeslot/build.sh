#!/bin/sh
# Build src/modeslot.so for Gen2 (aarch64) and run the offline test under qemu.
#   ./build.sh        library
#   ./build.sh test   aarch64 test of the hook against a stand-in function (Docker + qemu binfmt)
#   ./build.sh check MPC-BINARY   check the profile against an MPC binary on disk (python3 -I)
#   ./build.sh demo   demo/pagedemo, a test app for the live page host
set -eu
cd "$(dirname "$0")"
IMG=arm64v8/gcc:12-bookworm
FLAGS='-O2 -Wall -Wextra -std=gnu11 -fPIC -fvisibility=hidden -mbranch-protection=standard'

case "${1:-}" in
test)
    docker run --rm --user "$(id -u):$(id -g)" --platform linux/arm64 -v "$PWD/src":/b -w /b $IMG bash -euc \
        "gcc $FLAGS -Wl,--build-id test_modeslot.c modeslot.c arch_a64.c page_host.c hook.S test_fake.S -o /tmp/t && /tmp/t &&
         gcc $FLAGS -shared modeslot.c arch_a64.c page_host.c hook.S -o /tmp/modeslot.so &&
         mkdir -p /tmp/x && echo 'int main(void){return 0;}' > /tmp/x/m.c && gcc -Wl,--build-id=none /tmp/x/m.c -o /tmp/x/MPC &&
         LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1 | grep -q '^modeslot: refused: no build-id' &&
         gcc -Wl,--build-id /tmp/x/m.c -o /tmp/x/MPC &&
         LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1 | grep -q '^modeslot: refused: unknown MPC build-id' &&
         cp /tmp/x/MPC /tmp/x/other && test -z \"\$(LD_PRELOAD=/tmp/modeslot.so /tmp/x/other 2>&1)\" &&
         test -z \"\$(OMM_DISABLE=1 LD_PRELOAD=/tmp/modeslot.so /tmp/x/MPC 2>&1)\" &&
         echo 'constructor: refuses an unknown MPC, silent for other programs and with OMM_DISABLE'"
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
