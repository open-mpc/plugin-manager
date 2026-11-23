// aarch64 entry/trampoline checks for the modeslot test.
#include <stdint.h>
#include <string.h>

#include "modeslot.h"
#include "test_arch.h"

void arch_tramp_tests(void)
{
    // The trampoline rewrites an adrp as a literal load of its absolute value.
    uint32_t t[16];
    uint32_t code[4] = {0xd503233f, 0xd10283ff, 0xd0036ca2, 0xf9445c42}; // MPC's, at 0x11bd880
    omm_fill_tramp(t, code, 0x11bd880);
    unsigned long v;
    memcpy(&v, &t[12], 8);
    CHECK(t[2] == (0x58000000u | (10u << 5) | 2u) && v == 0x7f53000, "adrp x2 at 0x11bd888: %08x %#lx", t[2], v);
    CHECK(t[0] == code[0] && t[1] == code[1] && t[3] == code[3], "other instructions copied");

    CHECK(omm_pc_relative(0x90000000u), "adrp not flagged");
    CHECK(omm_pc_relative(0x94000000u), "bl not flagged");
    CHECK(omm_pc_relative(0x54000001u), "b.ne not flagged");
    CHECK(omm_pc_relative(0x58000050u), "ldr literal not flagged");
    CHECK(!omm_pc_relative(0xa9be7bfdu), "stp flagged");
    CHECK(!omm_pc_relative(0xd503233fu), "paciasp flagged");
}
