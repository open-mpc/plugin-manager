// arch_a64: the Gen2 (aarch64) side of the entry patching. Each hook's four entry instructions go to a trampoline (an
// adrp among them is rewritten as a load of its absolute value; other PC-relative instructions are refused), followed by
// a jump back to entry+16; the entry becomes "ldr x16, #8; br x16; .quad hook". The MPC binary has no BTI property note.
#define _GNU_SOURCE
#include <string.h>
#include <sys/mman.h>

#include "modeslot.h"

static int is_adrp(uint32_t insn) { return (insn & 0x9f000000u) == 0x90000000u; }

int omm_pc_relative(uint32_t insn)
{
    if ((insn & 0x1f000000u) == 0x10000000u) return 1; // adr, adrp
    if ((insn & 0x7c000000u) == 0x14000000u) return 1; // b, bl
    if ((insn & 0x7e000000u) == 0x34000000u) return 1; // cbz, cbnz
    if ((insn & 0x7e000000u) == 0x36000000u) return 1; // tbz, tbnz
    if ((insn & 0xff000010u) == 0x54000000u) return 1; // b.cond
    if ((insn & 0x3b000000u) == 0x18000000u) return 1; // ldr/ldrsw/prfm (literal)
    return 0;
}

static uint64_t adrp_value(uint32_t insn, uintptr_t pc)
{
    int64_t imm = (int64_t)((((insn >> 5) & 0x7ffffu) << 2) | ((insn >> 29) & 3u));
    if (imm & (1 << 20)) imm -= (int64_t)1 << 21;
    return (uint64_t)((pc & ~(uintptr_t)0xfff) + (uintptr_t)(imm * 4096));
}

// t[0..3]: the entry instructions (an adrp becomes "ldr xd, <literal t[8+2i]>"); t[4..7]: ldr x16, #8;
// br x16; .quad entry+16; t[8..15]: literals. t must be 8-byte aligned.
void omm_fill_tramp(uint32_t *t, const uint32_t *code, uintptr_t entry_addr)
{
    memset(t, 0, 64);
    for (int i = 0; i < 4; i++) {
        if (is_adrp(code[i])) {
            uint64_t v = adrp_value(code[i], entry_addr + 4u * (unsigned)i);
            memcpy(&t[8 + 2 * i], &v, 8);
            t[i] = 0x58000000u | ((uint32_t)(8 + i) << 5) | (code[i] & 31u); // ldr xd, #4*(8+i)
        } else {
            t[i] = code[i];
        }
    }
    t[4] = 0x58000050u; // ldr x16, #8
    t[5] = 0xd61f0200u; // br x16
    uint64_t resume = (uint64_t)(entry_addr + 16);
    memcpy(&t[6], &resume, 8);
}

const char *omm_arch_patch(uint32_t *entry, void (*hook)(void), long pg)
{
    uintptr_t lo = (uintptr_t)entry & ~((uintptr_t)pg - 1); // the 16 bytes may straddle two pages
    uintptr_t hi = ((uintptr_t)entry + 16 + (uintptr_t)pg - 1) & ~((uintptr_t)pg - 1);
    if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE) != 0) return "mprotect entry (write) failed";
    uint32_t patch[4] = {0x58000050u, 0xd61f0200u, 0, 0}; // ldr x16, #8; br x16; .quad hook
    uint64_t h = (uint64_t)(uintptr_t)hook;
    memcpy(&patch[2], &h, 8);
    memcpy(entry, patch, 16);
    if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC) != 0) return "mprotect entry (exec) failed";
    __builtin___clear_cache((char *)entry, (char *)(entry + 4));
    return NULL;
}

int omm_arch_table_default(uintptr_t base, uintptr_t table, uintptr_t tbase, uintptr_t def, uint32_t len, uint32_t id)
{
    if (id < 0x2080 || id - 0x2080 >= len) return 0;
    int16_t e;
    memcpy(&e, (const void *)(base + table + 2 * (id - 0x2080)), 2);
    return base + tbase + (uintptr_t)((intptr_t)e * 4) == base + def;
}


// Entry instructions that can be displaced: everything but PC-relative ones, except adrp (re-created in the trampoline).
const char *omm_arch_check_entry(const uint32_t *code)
{
    for (int i = 0; i < OMM_ENTRY_WORDS; i++)
        if (omm_pc_relative(code[i]) && !is_adrp(code[i])) return "entry instructions are PC-relative";
    return NULL;
}

// MPC's repaint takes a Rectangle<int> as two packed registers: (x, y) and (w, h).
static void (*a64_repaint)(void *, uint64_t, uint64_t);
static void repaint_rect(void *c, int x, int y, int w, int h)
{
    a64_repaint(c, ((uint64_t)(uint32_t)y << 32) | (uint32_t)x, ((uint64_t)(uint32_t)h << 32) | (uint32_t)w);
}
void (*omm_arch_repaint_rect(uintptr_t real))(void *, int, int, int, int)
{
    a64_repaint = (void (*)(void *, uint64_t, uint64_t))real;
    return repaint_rect;
}
