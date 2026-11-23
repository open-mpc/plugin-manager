// Per-architecture pieces of the shared modeslot test (test_modeslot.c): how the stand-in functions are called (the result
// pointer of a class returned by value is x8 on aarch64, the first argument on 32-bit ARM), the juce::Component layout the
// test profile uses, the entry instructions of the stand-ins, and the jump-table format.
#ifndef TEST_ARCH_H
#define TEST_ARCH_H
#include <stdint.h>
#include <stdio.h>

extern int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct jstring;
void arch_tramp_tests(void); // the trampoline and entry checks of this architecture (test_arch_*.c)

#if defined(__aarch64__)
// ---------------------------------------------------------------- aarch64 (Gen2)
long call4(void (*fn)(void), void *a, unsigned long b, void *c, void *x8);
void *call_get(void (*fn)(void), unsigned id, struct jstring *out);
extern unsigned long fake_var;
void fake_ctor_entry(void);
void fake_decode(void);
#define DEFINE_FAKE_DECODE
#define CALL_NAME(id, out)         call_get(fake_get_mode_name, id, out)
#define CALL_MODE(self, id)        call4(fake_mode_changed, self, id, NULL, NULL)
#define CALL_REG(mgr, id, mode)    call4(fake_register, mgr, id, mode, NULL)
#define CALL_IMAGE(data, size, out) call4(fake_image, (void *)(data), size, NULL, out)
#define CALL_ICON(id, out)         call4(fake_icon, (void *)(id), 0, NULL, out)
#define CALL_ORDER(o, v, s)        call4(fake_order, o, (unsigned long)(v), s, NULL)
#define CALL_PAINT(page, g)        call4(fake_paint, page, (unsigned long)(g), NULL, NULL)
#define FAKE_VAR                   fake_var
#define DEFINE_FAKE_REPAINT_AREA \
    static void fake_repaint_area(void *c, uint64_t xy, uint64_t wh) { rp.pixel++; rp.comp = c; rp.xy = xy; rp.wh = wh; }
#define T_COMP 16
#define T_BOUNDS 0x20
#define T_PARENT 0x18
#define T_CHILDREN 0x40
#define T_USED 0x4c
#define T_HOLDER_FIELD 0x300
#define T_DECODE 0x28
#define TAB_T int16_t
#define TAB_SET(t, i, named) ((t)[i] = (named) ? 7 : 5) /* halfword offset: 5 is the default case */
#define T_NAME_BASE 0x10000
#define T_NAME_DEFAULT (0x10000 + 4 * 5)
#define T_PAGE_BASE 0x20000
#define T_PAGE_DEFAULT (0x20000 + 4 * 5)
#define T_PRO_GETNAME {0xd503233f, 0xa9be7bfd, 0x2a0003e1, 0x52862240}
#define T_PRO_REG     {0xd503233f, 0xa9bb7bfd, 0x910003fd, 0xf90023f9}
#define T_PRO_MODE    {0xd503233f, 0xd101c3ff, 0xa9037bfd, 0x9100c3fd}
#define T_PRO_ORDER   {0xd503233f, 0xd101c3ff, 0xa9037bfd, 0x9100c3fd}
#define T_LAST 3
#define T_MID 2
#define T_BL_INSN 0x94000000u
#define T_CTOR_ADDR ((uintptr_t)fake_ctor_entry)
#define T_CTOR_FIRST 0xd503233fu
#define T_FIRST_BYTES 16

#elif defined(__arm__)
// ---------------------------------------------------------------- 32-bit ARM (Gen1 MPC, Force)
long call_a32(void *fn, void *a0, void *a1, void *a2); // 0, or 1 if r4-r11 or sp changed; the result is call_a32_ret
extern void *call_a32_ret;
#define FAKE_VAR_A32 0x5a5a0001ul
void fake_ctor(struct jstring *self, const char *s);
#define DEFINE_FAKE_DECODE \
    void fake_decode(void *out, void *fmt, void *stream) { (void)fmt; (void)stream; fake_decodes++; *(void **)out = &fake_imgdata; }
#define CALL_NAME(id, out)         (call_a32((void *)fake_get_mode_name, out, (void *)(uintptr_t)(id), NULL) ? NULL : call_a32_ret)
#define CALL_MODE(self, id)        call_a32((void *)fake_mode_changed, self, (void *)(uintptr_t)(id), NULL)
#define CALL_REG(mgr, id, mode)    call_a32((void *)fake_register, mgr, (void *)(uintptr_t)(id), mode)
#define CALL_IMAGE(data, size, out) call_a32((void *)fake_image, out, (void *)(data), (void *)(uintptr_t)(size))
#define CALL_ICON(id, out)         call_a32((void *)fake_icon, out, (void *)(uintptr_t)(id), NULL)
#define CALL_ORDER(o, v, s)        call_a32((void *)fake_order, o, v, s)
#define CALL_PAINT(page, g)        call_a32((void *)fake_paint, page, (void *)(g), NULL)
#define FAKE_VAR                   FAKE_VAR_A32
#define DEFINE_FAKE_REPAINT_AREA \
    static void fake_repaint_area(void *c, int x, int y, int w, int h) \
    { rp.pixel++; rp.comp = c; rp.xy = ((uint64_t)(uint32_t)y << 32) | (uint32_t)x; rp.wh = ((uint64_t)(uint32_t)h << 32) | (uint32_t)w; }
#define T_COMP 8
#define T_BOUNDS 0x10
#define T_PARENT 0xc
#define T_CHILDREN 0x28
#define T_USED 0x30
#define T_HOLDER_FIELD 0x1a0
#define T_DECODE 0x14
#define TAB_T uint32_t
// A table entry is a `b` to the default case (fake_default_case) or to another one (fake_other_case).
void fake_default_case(void);
void fake_other_case(void);
uint32_t a32_branch(const void *at, const void *target);
#define TAB_SET(t, i, named) ((t)[i] = a32_branch(&(t)[i], (named) ? (const void *)fake_other_case : (const void *)fake_default_case))
#define T_NAME_BASE 0
#define T_NAME_DEFAULT ((uintptr_t)fake_default_case)
#define T_PAGE_BASE 0
#define T_PAGE_DEFAULT ((uintptr_t)fake_default_case)
#define T_PRO_GETNAME {0xe3033112, 0xe52d4008, 0, 0} /* movw r3, #0x3112; str r4, [sp, #-8]! */
#define T_PRO_REG     {0xe92d4030, 0xe1a04000, 0, 0} /* push {r4, r5, lr}; mov r4, r0 */
#define T_PRO_MODE    {0xe59f24fc, 0xe59f34fc, 0, 0} /* ldr r2, [pc, #0x4fc]; ldr r3, [pc, #0x4fc] (two literals) */
#define T_PRO_ORDER   {0xe92d4010, 0xe1a04001, 0, 0} /* push {r4, lr}; mov r4, r1 */
#define T_LAST 1
#define T_MID 0
#define T_BL_INSN 0xeb000000u
#define T_CTOR_ADDR ((uintptr_t)fake_ctor)
#define T_CTOR_FIRST (*(const uint32_t *)fake_ctor)
#define T_FIRST_BYTES 8
#endif

#endif
