// Stand-ins for the four MPC functions modeslot hooks, with the same entry instructions (the image
// one has an adrp among them, like MPC's) and calling conventions. They live in a 64 KiB page of their
// own: patching makes the page non-executable for a moment, so the test's own code must not be in it
// (in MPC the hooked pages hold only MPC code).
    .section .text.fake, "ax", %progbits
    .balign 65536

// getModeName: id in w0, result object in x8, returns x8 in x0. Names 0x2091 "Browser", leaves
// everything else empty, calling fake_ctor(x8, name) like MPC calls juce::String(const char*).
    .globl fake_get_mode_name
    .type fake_get_mode_name, %function
fake_get_mode_name:
    paciasp
    stp     x29, x30, [sp, #-0x20]!
    mov     w1, w0
    mov     w0, #0x3112
    mov     x29, sp
    str     x19, [sp, #0x10]
    mov     x19, x8
    mov     w2, #0x2091
    cmp     w1, w2
    b.ne    0f
    adrp    x1, browser
    add     x1, x1, :lo12:browser
    b       2f
0:  mov     w2, #0x2090                 // fake_slot_name: another mod's name for the slot, if set
    cmp     w1, w2
    b.ne    1f
    adrp    x1, fake_slot_name
    ldr     x1, [x1, :lo12:fake_slot_name]
    cbnz    x1, 2f
1:  adrp    x1, empty
    add     x1, x1, :lo12:empty
2:  mov     x0, x19
    bl      fake_ctor
    mov     x0, x19
    ldr     x19, [sp, #0x10]
    ldp     x29, x30, [sp], #0x20
    autiasp
    ret
    .size fake_get_mode_name, . - fake_get_mode_name

// registerMode(manager, ModeID, Mode *): records each call through fake_record(x0, w1, x2).
    .globl fake_register
    .type fake_register, %function
fake_register:
    paciasp
    stp     x29, x30, [sp, #-0x50]!
    mov     x29, sp
    str     x25, [sp, #0x40]
    bl      fake_record
    ldr     x25, [sp, #0x40]
    ldp     x29, x30, [sp], #0x50
    autiasp
    ret
    .size fake_register, . - fake_register

// The touch UI's mode change (this, ModeID): records through fake_mode_record(x0, w1).
    .globl fake_mode_changed
    .type fake_mode_changed, %function
fake_mode_changed:
    paciasp
    sub     sp, sp, #0x70
    stp     x29, x30, [sp, #0x30]
    add     x29, sp, #0x30
    bl      fake_mode_record
    ldp     x29, x30, [sp, #0x30]
    add     sp, sp, #0x70
    autiasp
    ret
    .size fake_mode_changed, . - fake_mode_changed

// ImageCache::getFromMemory(data, size) -> x8: its third instruction is an adrp, and x2 is then
// loaded through it; fake_img_record(x0, w1, x2, x8) lets the test check the relocated adrp.
    .globl fake_image
    .type fake_image, %function
fake_image:
    paciasp
    sub     sp, sp, #0xa0
    adrp    x2, fake_var
    ldr     x2, [x2, :lo12:fake_var]
    stp     x29, x30, [sp, #0x40]
    add     x29, sp, #0x40
    mov     x3, x8
    str     x8, [sp, #0x50]
    bl      fake_img_record
    ldr     x0, [sp, #0x50]
    ldp     x29, x30, [sp, #0x40]
    add     sp, sp, #0xa0
    autiasp
    ret
    .size fake_image, . - fake_image

// The MENU icon lookup (ModeID w0) -> x8, with an adrp in its entry like MPC's: records through
// fake_icon_record(w0, x8, x1) and returns x8.
    .globl fake_icon
    .type fake_icon, %function
fake_icon:
    paciasp
    sub     sp, sp, #0x30
    adrp    x1, fake_var
    ldr     x1, [x1, :lo12:fake_var]
    stp     x29, x30, [sp, #0x10]
    add     x29, sp, #0x10
    str     x8, [sp, #0x20]
    mov     x2, x1
    mov     x1, x8
    bl      fake_icon_record
    ldr     x0, [sp, #0x20]
    ldp     x29, x30, [sp, #0x10]
    add     sp, sp, #0x30
    autiasp
    ret
    .size fake_icon, . - fake_icon

// ModeMenuOrder setup (order, const vector<ModeID> *, settings): records through
// fake_order_record(x0, x1, x2).
    .globl fake_order
    .type fake_order, %function
fake_order:
    paciasp
    sub     sp, sp, #0x70
    stp     x29, x30, [sp, #0x30]
    add     x29, sp, #0x30
    bl      fake_order_record
    ldp     x29, x30, [sp, #0x30]
    add     sp, sp, #0x70
    autiasp
    ret
    .size fake_order, . - fake_order

// TouchDummyPage::paint(page, Graphics &) with an adrp in its entry: records through
// fake_paint_record(x0, x1, x3) (x3 = what the relocated adrp + ldr loaded).
    .globl fake_paint
    .type fake_paint, %function
fake_paint:
    paciasp
    sub     sp, sp, #0x70
    adrp    x3, fake_var
    ldr     x3, [x3, :lo12:fake_var]
    stp     x29, x30, [sp, #0x40]
    add     x29, sp, #0x40
    mov     x2, x3
    bl      fake_paint_record
    ldp     x29, x30, [sp, #0x40]
    add     sp, sp, #0x70
    autiasp
    ret
    .size fake_paint, . - fake_paint
    .skip 65536

    .text
// fake_ctor with a paciasp first, like juce::String::String(const char*) in MPC.
    .globl fake_ctor_entry
    .type fake_ctor_entry, %function
fake_ctor_entry:
    paciasp
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    bl      fake_ctor
    ldp     x29, x30, [sp], #16
    autiasp
    ret
    .size fake_ctor_entry, . - fake_ctor_entry

// call4(fn, a, b, c, x8): x0..x2 = a, b, c; x8 as given; returns 0, or 1 if x19, x20, x28 or sp
// changed.
    .globl call4
    .type call4, %function
call4:
    stp     x29, x30, [sp, #-48]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    stp     x21, x28, [sp, #32]
    mov     x16, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x8, x4
    movz    x19, #0x919
    movz    x20, #0x202
    movz    x28, #0x828
    mov     x21, sp
    blr     x16
    mov     x0, #0
    mov     x1, sp
    cmp     x1, x21
    b.ne    9f
    cmp     x19, #0x919
    b.ne    9f
    cmp     x20, #0x202
    b.ne    9f
    cmp     x28, #0x828
    b.eq    8f
9:  mov     x0, #1
8:  ldp     x21, x28, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #48
    ret
    .size call4, . - call4

// call_get(fn, id, out): x8 = out, call fn(id), return what fn left in x0 (1 if x19, x20, x28 or
// sp changed).
    .globl call_get
    .type call_get, %function
call_get:
    stp     x29, x30, [sp, #-48]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    stp     x21, x28, [sp, #32]
    mov     x16, x0
    mov     w0, w1
    mov     x8, x2
    movz    x19, #0x919
    movz    x20, #0x202
    movz    x28, #0x828
    mov     x21, sp
    blr     x16
    mov     x1, sp
    cmp     x1, x21
    b.ne    7f
    cmp     x19, #0x919
    b.ne    7f
    cmp     x20, #0x202
    b.ne    7f
    cmp     x28, #0x828
    b.eq    6f
7:  mov     x0, #1
6:  ldp     x21, x28, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #48
    ret
    .size call_get, . - call_get

// ImageFileFormat::decodeImage(fmt, stream) -> Image (x8): *x8 = &fake_imgdata, as if newly decoded.
    .globl fake_decode
    .type fake_decode, %function
fake_decode:
    bti     c
    adrp    x0, fake_imgdata
    add     x0, x0, :lo12:fake_imgdata
    str     x0, [x8]
    adrp    x1, fake_decodes
    ldr     w2, [x1, :lo12:fake_decodes]
    add     w2, w2, #1
    str     w2, [x1, :lo12:fake_decodes]
    ret
    .size fake_decode, . - fake_decode

    .section .rodata
browser: .asciz "Browser"
empty:   .asciz ""
    .data
    .p2align 3
    .globl fake_var
fake_var: .quad 0x123456789abcdef0
    .globl fake_slot_name
fake_slot_name: .quad 0
    .section .note.GNU-stack, "", %progbits
