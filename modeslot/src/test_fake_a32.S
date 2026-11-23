@ Stand-ins for the MPC functions modeslot hooks on 32-bit ARM, with entry instructions of the kinds MPC's start with
@ (PC-relative literal loads, movw, push) and the same calling convention (the result object of a class returned by value
@ is the first argument, r0). They live in a 64 KiB region of their own: patching makes a page non-executable for a moment,
@ so the test's own code must not be in it (in MPC the hooked pages hold only MPC code).
    .syntax unified
    .arm
    .section .text.fake, "ax", %progbits
    .balign 65536

@ getModeName(out, id) -> out. entry: movw r3, #0x3112; str r4, [sp, #-8]!  (both position independent)
    .globl fake_get_mode_name
    .type fake_get_mode_name, %function
fake_get_mode_name:
    movw    r3, #0x3112
    str     r4, [sp, #-8]!
    str     lr, [sp, #4]
    bl      fake_name_body
    ldr     r4, [sp]
    ldr     lr, [sp, #4]
    add     sp, sp, #8
    bx      lr
    .size fake_get_mode_name, . - fake_get_mode_name

@ registerMode(manager, ModeID, Mode *): records through fake_record(r0, r1, r2). entry: push {r4, r5, lr}; mov r4, r0
    .globl fake_register
    .type fake_register, %function
fake_register:
    push    {r4, r5, lr}
    mov     r4, r0
    sub     sp, sp, #4
    bl      fake_record
    add     sp, sp, #4
    pop     {r4, r5, pc}
    .size fake_register, . - fake_register

@ The touch UI's mode change (this, ModeID). Its entry is two PC-relative literal loads, 0x4fc apart from their words, like
@ MPC's: ldr r2, [pc, #0x4fc]; ldr r3, [pc, #0x4fc]. fake_lits_record(r2, r3) shows what they loaded, then fake_mode_record.
    .globl fake_mode_changed
    .type fake_mode_changed, %function
fake_mode_changed:
    ldr     r2, [pc, #0x4fc]
    ldr     r3, [pc, #0x4fc]
    push    {r4, r5, r6, lr}
    mov     r4, r0
    mov     r5, r1
    mov     r0, r2
    mov     r1, r3
    bl      fake_lits_record
    mov     r0, r4
    mov     r1, r5
    bl      fake_mode_record
    pop     {r4, r5, r6, pc}
    .space  0x504 - (. - fake_mode_changed)
    .word   0xa1b2c3d4                  @ fake_mode_changed + 8 + 0x4fc
    .word   0x0d4c3b2a                  @ fake_mode_changed + 12 + 0x4fc
    .size fake_mode_changed, . - fake_mode_changed

@ ImageCache::getFromMemory(out, data, size) -> out. entry: ldr ip, [pc, #...] (a literal); push {r4, lr}.
@ fake_img_record(data, size, ip, out) lets the test check the relocated literal.
    .globl fake_image
    .type fake_image, %function
fake_image:
    ldr     ip, fake_image_lit
    push    {r4, lr}
    mov     r4, r0
    mov     r3, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, ip
    bl      fake_img_record
    mov     r0, r4
    pop     {r4, pc}
fake_image_lit:
    .word   0x5a5a0001
    .size fake_image, . - fake_image

@ The MENU icon lookup (out, ModeID) -> out. entry: movw r3, #0x20a4; ldr r2, [pc, #...] (the second is a literal load).
@ fake_icon_record(id, out, r2).
    .globl fake_icon
    .type fake_icon, %function
fake_icon:
    movw    r3, #0x20a4
    ldr     r2, fake_icon_lit
    push    {r4, lr}
    mov     r4, r0
    mov     r3, r0
    mov     r0, r1
    mov     r1, r3
    bl      fake_icon_record
    mov     r0, r4
    pop     {r4, pc}
fake_icon_lit:
    .word   0x5a5a0001
    .size fake_icon, . - fake_icon

@ ModeMenuOrder setup (order, const vector<ModeID> *, settings): records through fake_order_record(r0, r1, r2).
@ entry: push {r4, lr}; mov r4, r1
    .globl fake_order
    .type fake_order, %function
fake_order:
    push    {r4, lr}
    mov     r4, r1
    bl      fake_order_record
    pop     {r4, pc}
    .size fake_order, . - fake_order

@ TouchDummyPage::paint(page, Graphics &). entry: two literal loads (ip, r2); fake_paint_record(r0, r1, r2).
    .globl fake_paint
    .type fake_paint, %function
fake_paint:
    ldr     ip, fake_paint_lit0
    ldr     r2, fake_paint_lit1
    push    {r4, lr}
    bl      fake_paint_record
    pop     {r4, pc}
fake_paint_lit0:
    .word   0xcafe0001
fake_paint_lit1:
    .word   0x5a5a0001
    .size fake_paint, . - fake_paint

@ A function whose first instruction is a literal load with a NEGATIVE offset (the literal is before the function):
@ fake_neg_lit(void) -> 0x77665544.  entry: ldr r2, [pc, #-12]; mov r0, r2
    .word   0x77665544
    .globl fake_neg_lit
    .type fake_neg_lit, %function
fake_neg_lit:
    ldr     r2, [pc, #-12]
    mov     r0, r2
    bx      lr
    .size fake_neg_lit, . - fake_neg_lit

    .balign 65536

@ call_a32(fn, a0, a1, a2): fn(a0, a1, a2) with r4-r11 set to markers; returns 0, or 1 if any of r4-r11 or sp changed.
@ The result of fn (r0) is left in call_a32_ret.
    .text
    .globl call_a32
    .type call_a32, %function
call_a32:
    push    {r4-r11, lr}
    sub     sp, sp, #4                  @ 8-byte alignment at the call; the slot holds sp's value
    mov     ip, r0
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    str     sp, [sp]
    ldr     r4,  =0x11110004
    ldr     r5,  =0x11110005
    ldr     r6,  =0x11110006
    ldr     r7,  =0x11110007
    ldr     r8,  =0x11110008
    ldr     r9,  =0x11110009
    ldr     r10, =0x1111000a
    ldr     r11, =0x1111000b
    blx     ip
    ldr     r3, =call_a32_ret
    str     r0, [r3]
    mov     ip, #0
    ldr     r3, =0x11110004
    cmp     r4, r3
    orrne   ip, ip, #1
    ldr     r3, =0x11110005
    cmp     r5, r3
    orrne   ip, ip, #1
    ldr     r3, =0x11110006
    cmp     r6, r3
    orrne   ip, ip, #1
    ldr     r3, =0x11110007
    cmp     r7, r3
    orrne   ip, ip, #1
    ldr     r3, =0x11110008
    cmp     r8, r3
    orrne   ip, ip, #1
    ldr     r3, =0x11110009
    cmp     r9, r3
    orrne   ip, ip, #1
    ldr     r3, =0x1111000a
    cmp     r10, r3
    orrne   ip, ip, #1
    ldr     r3, =0x1111000b
    cmp     r11, r3
    orrne   ip, ip, #1
    ldr     r3, [sp]
    cmp     r3, sp
    orrne   ip, ip, #1
    mov     r0, ip
    add     sp, sp, #4
    pop     {r4-r11, pc}
    .ltorg
    .size call_a32, . - call_a32

    .data
    .globl call_a32_ret
call_a32_ret: .word 0
    .section .note.GNU-stack, "", %progbits
