// 32-bit ARM stand-ins' C parts and the entry/trampoline checks for the modeslot test.
#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "modeslot.h"
#include "test_arch.h"

struct jstring { const char *text; void *pad[2]; };

const char *fake_slot_name;
static const char browser[] = "Browser", empty[] = "";

// getModeName's body: names 0x2091 "Browser", leaves everything else empty (another mod's name for the slot if
// fake_slot_name is set), through fake_ctor like MPC's juce::String(const char *).
void *fake_name_body(struct jstring *out, uint32_t id)
{
    const char *s = empty;
    if (id == 0x2091) s = browser;
    else if (id == 0x2090 && fake_slot_name) s = fake_slot_name;
    fake_ctor(out, s);
    return out;
}

void fake_default_case(void) {}
void fake_other_case(void) {}

__attribute__((noinline)) uint32_t a32_branch(const void *at, const void *target)
{
    int32_t off = (int32_t)((const char *)target - ((const char *)at + 8)) >> 2;
    return 0xea000000u | ((uint32_t)off & 0xffffffu);
}

static unsigned last_lit[2];
void fake_lits_record(unsigned a, unsigned b) { last_lit[0] = a; last_lit[1] = b; }

extern void fake_neg_lit(void);

// A trampoline in an executable page of its own (the one the shim allocates also holds only trampolines).
static uint32_t *tramp_page(void)
{
    uint32_t *t = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(t != MAP_FAILED, "mmap for the trampoline");
    return t;
}

void arch_tramp_tests(void)
{
    uint32_t t[OMM_TRAMP_WORDS] __attribute__((aligned(8)));
    CHECK(OMM_ENTRY_WORDS == 2 && OMM_TRAMP_WORDS == 8, "arch constants");

    // Plain instructions are copied; the jump back resumes after the two displaced instructions.
    uint32_t plain[2] = {0xe3033112, 0xe52d4008}; // movw r3, #0x3112; str r4, [sp, #-8]!
    omm_fill_tramp(t, plain, 0x166e39c);
    CHECK(t[0] == plain[0] && t[1] == plain[1] && t[2] == 0xe51ff004u && t[3] == 0x166e3a4u,
          "plain entry: %08x %08x %08x %08x", t[0], t[1], t[2], t[3]);

    // A literal load (forward) is re-created to read a copy of its literal: ldr rX, [pc, #8] reaches t[4 + i] from t[i].
    uint32_t lits[0x500 / 4 + 4] = {0};
    lits[0] = 0xe59f24fc; // ldr r2, [pc, #0x4fc]  -> the word at +8+0x4fc
    lits[1] = 0xe59f34fc; // ldr r3, [pc, #0x4fc]  -> the word at +4+8+0x4fc
    lits[(8 + 0x4fc) / 4] = 0xa1b2c3d4;
    lits[(12 + 0x4fc) / 4] = 0x0d4c3b2a;
    omm_fill_tramp(t, lits, (uintptr_t)lits);
    CHECK(t[0] == 0xe59f2008u && t[1] == 0xe59f3008u && t[4] == 0xa1b2c3d4u && t[5] == 0x0d4c3b2au && t[3] == (uintptr_t)lits + 8,
          "literal entry: %08x %08x lits %08x %08x resume %08x", t[0], t[1], t[4], t[5], t[3]);
    // ...a conditional literal load keeps its condition and register; a conditional non-literal is copied as it is
    uint32_t clp[4] = {0x159fc004u, 0xe1a00000u, 0, 0x12345678u}; // ldrne ip, [pc, #4] -> the word at +12
    omm_fill_tramp(t, clp, (uintptr_t)clp);
    CHECK((t[0] >> 28) == 0x1 && ((t[0] >> 12) & 15) == 12 && (t[0] & 0xfffu) == 8 && t[4] == 0x12345678u,
          "conditional literal: %08x lit %08x", t[0], t[4]);
    uint32_t cond[2] = {0x11a00001u, 0xe1a00000u}; // movne r0, r1
    omm_fill_tramp(t, cond, 0x1000);
    CHECK(t[0] == cond[0], "non-literal conditional copied");

    // Executing trampolines: a displaced PC-relative load must give the same register value as the original.
    uint32_t *tp = tramp_page();
    if (tp != MAP_FAILED) {
        extern void fake_mode_changed(void);
        int obj_dummy;
        omm_fill_tramp(tp, (const uint32_t *)fake_mode_changed, (uintptr_t)fake_mode_changed);
        last_lit[0] = last_lit[1] = 0;
        __builtin___clear_cache((char *)tp, (char *)(tp + 8));
        CHECK(call_a32((void *)tp, &obj_dummy, (void *)0x2090, NULL) == 0, "trampoline run: registers clobbered");
        CHECK(last_lit[0] == 0xa1b2c3d4u && last_lit[1] == 0x0d4c3b2au,
              "trampoline: relocated literals read %08x %08x", last_lit[0], last_lit[1]);
        // a backward literal
        omm_fill_tramp(tp, (const uint32_t *)fake_neg_lit, (uintptr_t)fake_neg_lit);
        __builtin___clear_cache((char *)tp, (char *)(tp + 8));
        CHECK(call_a32((void *)tp, NULL, NULL, NULL) == 0 && call_a32_ret == (void *)0x77665544,
              "trampoline: backward literal gave %p", call_a32_ret);
        munmap(tp, 4096);
    }

    // What can be displaced.
    CHECK(omm_arch_check_entry((uint32_t[]){0xe3033112, 0xe52d4008}) == NULL, "movw/str refused");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe59f24fc, 0xe59f34fc}) == NULL, "two literal loads refused");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe16d42f4, 0xe1cd81f0}) == NULL, "strd pair refused");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe1a05002, 0xe16d41fc}) == NULL, "mov/strd refused");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe92d4030, 0xe1a04000}) == NULL, "push/mov refused");
    CHECK(omm_arch_check_entry((uint32_t[]){0xeb000010, 0xe1a00000}) != NULL, "bl accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xea000010, 0xe1a00000}) != NULL, "b accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe1a00000, 0x0a000001}) != NULL, "beq accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe28f0010, 0xe1a00000}) != NULL, "add r0, pc, #16 (adr) accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe08f0001, 0xe1a00000}) != NULL, "add r0, pc, r1 accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe1a0000f, 0xe1a00000}) != NULL, "mov r0, pc accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe59ff010, 0xe1a00000}) != NULL, "ldr pc, [pc, #16] accepted");
    CHECK(omm_arch_check_entry((uint32_t[]){0xe1a00000, 0xe590f000}) != NULL, "ldr pc, [r0] accepted");

    // The jump tables: `b` entries.
    static uint32_t tab[4];
    tab[0] = a32_branch(&tab[0], fake_default_case);
    tab[1] = a32_branch(&tab[1], fake_other_case);
    tab[2] = 0xe1a00000; // not a branch
    tab[3] = a32_branch(&tab[3], fake_default_case) & 0x0fffffffu; // cond 0
    CHECK(omm_arch_table_default(0, (uintptr_t)tab, 0, (uintptr_t)fake_default_case, 4, 0x2080) == 1, "default entry");
    CHECK(omm_arch_table_default(0, (uintptr_t)tab, 0, (uintptr_t)fake_default_case, 4, 0x2081) == 0, "other entry");
    CHECK(omm_arch_table_default(0, (uintptr_t)tab, 0, (uintptr_t)fake_default_case, 4, 0x2082) == 0, "non-branch entry");
    CHECK(omm_arch_table_default(0, (uintptr_t)tab, 0, (uintptr_t)fake_default_case, 4, 0x2083) == 0, "conditional branch entry");
    CHECK(omm_arch_table_default(0, (uintptr_t)tab, 0, (uintptr_t)fake_default_case, 4, 0x2084) == 0, "id past the table");
    CHECK(omm_arch_table_default(0, (uintptr_t)tab, 0, (uintptr_t)fake_default_case, 4, 0x207f) == 0, "id before the table");
}
