// arch_a32: the 32-bit ARM (A32 state) side of the entry patching, for the Gen1 MPC and Force builds.
//
// A hook displaces the first two instructions of the function to a trampoline, followed by a jump back to entry+8, and the
// entry becomes "ldr pc, [pc, #-4]; .word hook" (absolute: no range limit). An `ldr rX, [pc, #imm]` among the displaced
// instructions is re-created to read a copy of its literal (what it loads is a constant, e.g. an offset the function adds
// its own pc to later); every other instruction that touches pc is refused. Hooks are ARM-mode functions (compiled -marm).
//
// Trampoline (OMM_TRAMP_WORDS = 8): [0] [1] the displaced instructions, [2] ldr pc, [pc, #-4], [3] entry + 8,
// [4] [5] the copied literals of instructions 0 and 1.
#define _GNU_SOURCE
#include <string.h>
#include <sys/mman.h>

#include "modeslot.h"

#define LDR_PC_MINUS4 0xe51ff004u // ldr pc, [pc, #-4]

static int is_ldr_literal(uint32_t insn)
{
    // ldr rt, [pc, #+/-imm12]: any condition, word, offset addressing (P=1, W=0)
    return (insn & 0x0f7f0000u) == 0x051f0000u;
}

static int is_movw_movt(uint32_t insn) { return (insn & 0x0fb00000u) == 0x03000000u; }

// Registers named by a data-processing or load/store instruction include pc.
static int uses_pc(uint32_t insn)
{
    unsigned cls = (insn >> 25) & 7u;
    if (cls == 5) return 1; // b, bl
    if (is_movw_movt(insn)) return ((insn >> 12) & 15u) == 15u;
    if (cls <= 3) {
        if (((insn >> 16) & 15u) == 15u || ((insn >> 12) & 15u) == 15u) return 1;
        if ((cls == 0 || cls == 3) && (insn & 15u) == 15u) return 1; // Rm (register forms)
    }
    return 0;
}

const char *omm_arch_check_entry(const uint32_t *code)
{
    for (int i = 0; i < OMM_ENTRY_WORDS; i++) {
        if (is_ldr_literal(code[i])) {
            if (((code[i] >> 12) & 15u) == 15u) return "entry loads pc from a literal";
        } else if (uses_pc(code[i])) {
            return "entry instructions are PC-relative";
        }
    }
    return NULL;
}

void omm_fill_tramp(uint32_t *t, const uint32_t *code, uintptr_t entry_addr)
{
    memset(t, 0, 4 * OMM_TRAMP_WORDS);
    for (int i = 0; i < OMM_ENTRY_WORDS; i++) {
        uint32_t insn = code[i];
        if (is_ldr_literal(insn)) {
            uint32_t imm = insn & 0xfffu;
            const char *lit = (const char *)code + 4 * i + 8 + ((insn & (1u << 23)) ? (long)imm : -(long)imm);
            memcpy(&t[4 + i], lit, 4);
            // the same load from this trampoline's own copy: [pc, #8] reaches t[4 + i] from t[i]
            t[i] = (insn & 0xf0000000u) | 0x059f0008u | (insn & 0x0000f000u);
        } else {
            t[i] = insn;
        }
    }
    t[2] = LDR_PC_MINUS4;
    t[3] = (uint32_t)(entry_addr + 4 * OMM_ENTRY_WORDS);
}

const char *omm_arch_patch(uint32_t *entry, void (*hook)(void), long pg)
{
    uintptr_t lo = (uintptr_t)entry & ~((uintptr_t)pg - 1); // the 8 bytes may straddle two pages
    uintptr_t hi = ((uintptr_t)entry + 8 + (uintptr_t)pg - 1) & ~((uintptr_t)pg - 1);
    if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE) != 0) return "mprotect entry (write) failed";
    entry[0] = LDR_PC_MINUS4;
    entry[1] = (uint32_t)(uintptr_t)hook;
    if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_EXEC) != 0) return "mprotect entry (exec) failed";
    __builtin___clear_cache((char *)entry, (char *)(entry + 2));
    return NULL;
}

// The switch's jump table: one `b target` per id from 0x2080 (cond AL), the default case being what the ids with no
// case branch to.
int omm_arch_table_default(uintptr_t base, uintptr_t table, uintptr_t tbase, uintptr_t def, uint32_t len, uint32_t id)
{
    (void)tbase;
    if (id < 0x2080 || id - 0x2080 >= len) return 0;
    uintptr_t at = base + table + 4 * (id - 0x2080);
    uint32_t w;
    memcpy(&w, (const void *)at, 4);
    if ((w >> 24) != 0xea) return 0; // not an unconditional branch
    int32_t off = (int32_t)(w << 8) >> 8;
    return at + 8 + (uintptr_t)(off * 4) == base + def;
}

void (*omm_arch_repaint_rect(uintptr_t real))(void *, int, int, int, int)
{
    return (void (*)(void *, int, int, int, int))real;
}
