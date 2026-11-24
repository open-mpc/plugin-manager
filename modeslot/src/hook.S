// The four hook entries. Each one ends by running the original function through its trampoline
// (omm_*_tramp_addr), so MPC's own code does the work; they only add to it or change arguments.
    .text

// 1. getModeName(ModeID id) -> juce::String (sret in x8). For id == omm_slot_id it runs the original
// first: stock MPC has no name for the slot, so a name there means something else owns it, and the
// shim stands aside for good (omm_yield = 1, that name is returned). Otherwise, and unless it has
// already stood aside, it builds juce::String(omm_slot_name) into *x8 (the empty string the
// original built uses a shared static, so nothing leaks) and returns x8 in x0, as the original does.
    .p2align 2
    .globl omm_hook
    .hidden omm_hook
    .type omm_hook, %function
omm_hook:
    bti     c
    adrp    x16, omm_slot_id
    ldr     w16, [x16, :lo12:omm_slot_id]
    cmp     w0, w16
    b.eq    10f
    adrp    x16, omm_slot2_id           // the launcher tile's slot (0 = none)
    ldr     w16, [x16, :lo12:omm_slot2_id]
    cbz     w16, 1f
    cmp     w0, w16
    b.ne    1f
    adrp    x9, omm_yield2
    add     x9, x9, :lo12:omm_yield2
    adrp    x10, omm_slot2_name
    add     x10, x10, :lo12:omm_slot2_name
    b       20f
10: adrp    x9, omm_yield
    add     x9, x9, :lo12:omm_yield
    adrp    x10, omm_slot_name
    add     x10, x10, :lo12:omm_slot_name
20: paciasp                             // x9 = &that slot's yield flag, x10 = &its name
    stp     x29, x30, [sp, #-48]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    str     x21, [sp, #32]
    mov     x19, x8
    mov     x20, x9
    mov     x21, x10
    adrp    x16, omm_tramp_addr
    ldr     x16, [x16, :lo12:omm_tramp_addr]
    blr     x16                         // the original, for the slot: x8 = x19 still
    ldr     x0, [x19]                   // juce::String's text pointer
    cbz     x0, 2f
    ldrb    w1, [x0]
    cbz     w1, 2f
    mov     w1, #1                      // a name we did not give: someone else's slot
    str     w1, [x20]
    b       3f
2:  ldr     w16, [x20]
    cbnz    w16, 3f
    mov     x0, x19
    ldr     x1, [x21]
    adrp    x16, omm_ctor
    ldr     x16, [x16, :lo12:omm_ctor]
    blr     x16
3:  mov     x0, x19
    ldr     x21, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #48
    autiasp
    ret
1:  adrp    x16, omm_tramp_addr
    ldr     x16, [x16, :lo12:omm_tramp_addr]
    br      x16
    .size omm_hook, . - omm_hook

// 2. registerMode(manager x0, ModeID w1, Mode *x2). If MPC or another mod registers the slot itself,
// the shim stands aside (omm_yield = 1). Runs the original; when w1 == omm_alias_from and the shim has
// not stood aside, runs it again for (x0, omm_slot_id, x2) (straight to the original: not counted).
    .p2align 2
    .globl omm_reg_hook
    .hidden omm_reg_hook
    .type omm_reg_hook, %function
omm_reg_hook:
    bti     c
    paciasp
    stp     x29, x30, [sp, #-48]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    str     x21, [sp, #32]
    mov     x19, x0
    mov     w20, w1
    mov     x21, x2
    adrp    x16, omm_slot_id
    ldr     w16, [x16, :lo12:omm_slot_id]
    cmp     w1, w16
    b.ne    6f
    adrp    x16, omm_yield
    mov     w17, #1
    str     w17, [x16, :lo12:omm_yield]
6:  adrp    x16, omm_slot2_id           // the launcher tile's slot registered by someone else
    ldr     w16, [x16, :lo12:omm_slot2_id]
    cbz     w16, 7f
    cmp     w20, w16
    b.ne    7f
    adrp    x16, omm_yield2
    mov     w17, #1
    str     w17, [x16, :lo12:omm_yield2]
7:  mov     x0, x19
    mov     w1, w20
    mov     x2, x21
    adrp    x16, omm_reg_tramp_addr
    ldr     x16, [x16, :lo12:omm_reg_tramp_addr]
    blr     x16
    adrp    x16, omm_alias_from
    ldr     w16, [x16, :lo12:omm_alias_from]
    cmp     w20, w16
    b.ne    2f
    adrp    x16, omm_yield
    ldr     w16, [x16, :lo12:omm_yield]
    cbnz    w16, 2f
    mov     x0, x19
    adrp    x16, omm_slot_id
    ldr     w1, [x16, :lo12:omm_slot_id]
    mov     x2, x21
    adrp    x16, omm_reg_tramp_addr
    ldr     x16, [x16, :lo12:omm_reg_tramp_addr]
    blr     x16
2:  adrp    x16, omm_alias_from         // and for the launcher tile's slot
    ldr     w16, [x16, :lo12:omm_alias_from]
    cmp     w20, w16
    b.ne    9f
    adrp    x16, omm_slot2_id
    ldr     w1, [x16, :lo12:omm_slot2_id]
    cbz     w1, 9f
    adrp    x16, omm_yield2
    ldr     w16, [x16, :lo12:omm_yield2]
    cbnz    w16, 9f
    mov     x0, x19
    mov     x2, x21
    adrp    x16, omm_reg_tramp_addr
    ldr     x16, [x16, :lo12:omm_reg_tramp_addr]
    blr     x16
9:  ldr     x21, [sp, #32]
    ldp     x19, x20, [sp, #16]
    ldp     x29, x30, [sp], #48
    autiasp
    ret
    .size omm_reg_hook, . - omm_reg_hook

// 3. Touch UI mode change: omm_mode_hook_c in page_host.c.

// 4. juce::ImageCache::getFromMemory(data x0, size w1) -> Image (x8). If the slot is the current
// mode and (x0, w1) is MPC's placeholder picture: with a live frame from the app (omm_live_img), return
// that Image (one more reference: *x8 = img, refcount at img + 8); otherwise, with page.png loaded,
// load ours instead. Touches only x16/x17 besides the arguments.
    .p2align 2
    .globl omm_img_hook
    .hidden omm_img_hook
    .type omm_img_hook, %function
omm_img_hook:
    bti     c
    adrp    x16, omm_slot2_id           // the launcher tile's slot is current: its page.png, if any
    ldr     w16, [x16, :lo12:omm_slot2_id]
    cbz     w16, 30f
    adrp    x17, omm_cur_mode
    ldr     w17, [x17, :lo12:omm_cur_mode]
    cmp     w16, w17
    b.ne    30f
    adrp    x16, omm_yield2
    ldr     w16, [x16, :lo12:omm_yield2]
    cbnz    w16, 4f
    adrp    x16, omm_dummy_size
    ldr     w16, [x16, :lo12:omm_dummy_size]
    cmp     w1, w16
    b.ne    4f
    adrp    x16, omm_dummy_data
    ldr     x16, [x16, :lo12:omm_dummy_data]
    cmp     x0, x16
    b.ne    4f
    adrp    x17, omm_png2_size
    ldr     w17, [x17, :lo12:omm_png2_size]
    cbz     w17, 4f
    adrp    x16, omm_png2_data
    ldr     x0, [x16, :lo12:omm_png2_data]
    mov     w1, w17
    b       4f
30: adrp    x16, omm_yield
    ldr     w16, [x16, :lo12:omm_yield]
    cbnz    w16, 4f
    adrp    x16, omm_cur_mode
    ldr     w16, [x16, :lo12:omm_cur_mode]
    adrp    x17, omm_slot_id
    ldr     w17, [x17, :lo12:omm_slot_id]
    cmp     w16, w17
    b.ne    4f
    adrp    x16, omm_dummy_size
    ldr     w16, [x16, :lo12:omm_dummy_size]
    cmp     w1, w16
    b.ne    4f
    adrp    x16, omm_dummy_data
    ldr     x16, [x16, :lo12:omm_dummy_data]
    cmp     x0, x16
    b.ne    4f
    adrp    x16, omm_live_img
    ldr     x16, [x16, :lo12:omm_live_img]
    cbz     x16, 7f
    str     x16, [x8]
    add     x16, x16, #8
8:  ldaxr   w17, [x16]                  // refcount += 1 (x9 is free at a function entry)
    add     w17, w17, #1
    stlxr   w9, w17, [x16]
    cbnz    w9, 8b
    mov     x0, x8
    ret
7:  adrp    x17, omm_png_size
    ldr     w17, [x17, :lo12:omm_png_size]
    cbz     w17, 4f
    adrp    x16, omm_png_data
    ldr     x0, [x16, :lo12:omm_png_data]
    mov     w1, w17
4:  adrp    x16, omm_img_tramp_addr
    ldr     x16, [x16, :lo12:omm_img_tramp_addr]
    br      x16
    .size omm_img_hook, . - omm_img_hook

// 5. MENU icon lookup (ModeID w0) -> Image (x8). For the slot, with an icon loaded:
// getFromMemory(omm_icon_data, omm_icon_size) through its original code (it keeps x8 and returns it
// in x0, like the lookup). Otherwise run the lookup. Touches only x16/x17 besides the arguments.
    .p2align 2
    .globl omm_icon_hook
    .hidden omm_icon_hook
    .type omm_icon_hook, %function
omm_icon_hook:
    bti     c
    adrp    x16, omm_slot2_id           // the launcher tile's slot: its icon.png, if any
    ldr     w16, [x16, :lo12:omm_slot2_id]
    cbz     w16, 40f
    cmp     w0, w16
    b.ne    40f
    adrp    x16, omm_yield2
    ldr     w16, [x16, :lo12:omm_yield2]
    cbnz    w16, 5f
    adrp    x17, omm_icon2_size
    ldr     w17, [x17, :lo12:omm_icon2_size]
    cbz     w17, 5f
    adrp    x16, omm_icon2_data
    ldr     x0, [x16, :lo12:omm_icon2_data]
    mov     w1, w17
    adrp    x16, omm_img_tramp_addr
    ldr     x16, [x16, :lo12:omm_img_tramp_addr]
    br      x16
40: adrp    x16, omm_yield
    ldr     w16, [x16, :lo12:omm_yield]
    cbnz    w16, 5f
    adrp    x16, omm_slot_id
    ldr     w16, [x16, :lo12:omm_slot_id]
    cmp     w0, w16
    b.ne    5f
    adrp    x17, omm_icon_size
    ldr     w17, [x17, :lo12:omm_icon_size]
    cbz     w17, 5f
    adrp    x16, omm_icon_data
    ldr     x0, [x16, :lo12:omm_icon_data]
    mov     w1, w17
    adrp    x16, omm_img_tramp_addr
    ldr     x16, [x16, :lo12:omm_img_tramp_addr]
    br      x16
5:  adrp    x16, omm_icon_tramp_addr
    ldr     x16, [x16, :lo12:omm_icon_tramp_addr]
    br      x16
    .size omm_icon_hook, . - omm_icon_hook

// omm_call_sret(fn, a0, a1, out): fn(a0, a1) with its result object at out (x8).
    .p2align 2
    .globl omm_call_sret
    .hidden omm_call_sret
    .type omm_call_sret, %function
omm_call_sret:
    bti     c
    paciasp
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    mov     x16, x0
    mov     x0, x1
    mov     x1, x2
    mov     x8, x3
    blr     x16
    ldp     x29, x30, [sp], #16
    autiasp
    ret
    .size omm_call_sret, . - omm_call_sret
    .section .note.GNU-stack, "", %progbits
