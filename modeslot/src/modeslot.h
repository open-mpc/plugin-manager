#ifndef MODESLOT_H
#define MODESLOT_H
#include <stddef.h>
#include <stdint.h>

// One hooked function: its link-time address and the entry instructions it must have (aarch64: 4 words, 32-bit ARM: 2;
// the rest zero).
struct omm_site {
    uintptr_t addr;      // 0 = this hook is off
    uint32_t prologue[4];
};

// MPC functions the live page host calls (resolved at install).
struct omm_live_fns {
    void (*repaint)(void *component);                                  // juce::Component::repaint()
    void (*repaint_area)(void *component, int x, int y, int w, int h); // repaint of a Rectangle<int>
    void (*memstream_ctor)(void *stream, const void *data, size_t size, int keep_copy);
    void (*memstream_dtor)(void *stream);
    void *(*find_format)(void *stream);                               // ImageFileFormat::findImageFormatForStream
    uint32_t decode_vslot;                                             // ImageFileFormat::decodeImage, vtable offset
    uintptr_t page_component_offset;                                   // TouchDummyPage -> its juce::Component
    void (*set_bounds)(void *component, int x, int y, int w, int h);   // juce::Component::setBounds; NULL = no full height
    uintptr_t dummy_page_offset;                                       // touch UI (the mode handler's this) -> page holder
    uintptr_t page_holder_field;                                       // page holder -> TouchDummyPage * at this offset
    uintptr_t page_vtable;                                             // TouchDummyPage's vtable (absolute): checked first
    uintptr_t wrapper_vtable;                                          // a mode's wrapper (the page's parent), absolute; 0 = keep the strip
    int whole_screen;                                                  // also cover MPC's bottom strip (OMM_FULL_HEIGHT not 1)
    // juce::Component and ReferenceCountedObject layout (bytes), which differs with the pointer size
    uint32_t comp_parent, comp_bounds;                                 // parent pointer; x, y, w, h (4 ints)
    uint32_t comp_children, comp_children_used;                        // child array (elements pointer); its numUsed
    uint32_t img_refcount;                                             // an Image's pixel data: the reference count
};

// A function MPC calls or the shim calls, with its expected first instruction.
struct omm_fn {
    uintptr_t addr;
    uint32_t first;
};

struct omm_profile {
    const char *label;
    uint8_t build_id[20];
    size_t build_id_len;
    // The slot: the first candidate that is still free, i.e. MPC's name switch and its touch page table
    // both send it to their default case (no name, placeholder page). Halfword jump tables indexed by
    // id - 0x2080 (table_len entries); target = table_base + 4 * (int16)entry.
    uint32_t candidates[4];       // 0-terminated
    const char *slot_name;
    uintptr_t name_table, name_table_base, name_default;
    uintptr_t page_table, page_table_base, page_default;
    uint32_t table_len;

    // 1. getModeName(ModeID) -> juce::String in x8: names the slot.
    struct omm_site get_mode_name;
    uintptr_t string_ctor;        // juce::String::String(const char *)
    uint32_t string_ctor_first;   // its first instruction, as expected

    // 2. registerMode(manager, ModeID, Mode *): when alias_from is registered, register the same
    //    controller object for the slot, so its buttons work (and can leave it).
    struct omm_site register_mode;
    uint32_t alias_from;

    // 3. The touch UI's mode-change handler (this, ModeID): records the current screen mode.
    struct omm_site mode_changed;

    // 4. juce::ImageCache::getFromMemory(data, size) -> Image in x8: while the slot is the current
    //    mode, MPC's placeholder picture (*placeholder_ptr_var, placeholder_size bytes) is swapped
    //    for the PNG file page_png. The placeholder page draws it, centred, as it draws its own.
    struct omm_site image_from_memory;
    uintptr_t placeholder_ptr_var; // address of the const char * that points at the placeholder PNG
    uint32_t placeholder_size;

    // 5. The MENU grid's icon lookup (ModeID) -> Image in x8: for the slot, if icon_png is loaded,
    //    return it (through getFromMemory). Stock icons are 70 x 70 PNGs.
    struct omm_site mode_icon;

    // 6. ModeMenuOrder setup (order, const std::vector<ModeID> &menu_modes, settings): MPC builds the
    //    MENU grid's pages from menu_modes, then moves each mode to the slot MPC.settings names
    //    (ModeMenuPage<p>Slot<s>); a saved name whose mode is not in menu_modes is dropped. The hook
    //    passes a copy with the slot appended, so a settings entry "Plugin Manager" becomes a tile.
    struct omm_site menu_order;

    // 7. Live page host (page_host.c): TouchDummyPage::paint(page, Graphics &), the page's
    //    juce::Component vtable slots for mouseDown, mouseDrag, mouseUp (each must hold mouse_default,
    //    JUCE's empty callback), and the MPC functions it calls.
    struct omm_site page_paint;
    uintptr_t mouse_slots[3];
    uintptr_t mouse_default;
    struct omm_fn repaint, repaint_area, memstream_ctor, memstream_dtor, find_format;
    uint32_t decode_vslot;
    uintptr_t page_component_offset;
    // Full height (like Developer mode): while the slot is shown the page also covers the band above it,
    // where other modes have their header; the bottom strip is left. set_bounds = juce::Component::setBounds
    // (this, x, y, w, h); dummy_page_offset: where the touch UI keeps the TouchDummyPage (the handler's
    // default case: the holder of the placeholder page); the holder keeps the TouchDummyPage * at
    // page_holder_field (page_holder_get, its getter, is "ldr x0, [x0, #page_holder_field]"); setBounds is only
    // called on an object whose vtable is page_vtable (page_dtor sets it). set_bounds.addr 0 = off.
    struct omm_fn set_bounds;
    uintptr_t dummy_page_offset, page_holder_field, page_holder_get, page_vtable, page_dtor;
    // The whole screen: each mode's page sits in a wrapper Component (wrapper_vtable) that stops above MPC's bottom
    // strip (1280 x 738 on a Live III); the shim stretches it to its parent's bottom while the slot is shown.
    uintptr_t wrapper_vtable;
    // juce::Component layout in this build: parentComponent, boundsRelativeToParent (x, y, w, h), the child array's
    // elements pointer and its numUsed; ReferenceCountedObject's count (the Image's pixel data).
    uint32_t comp_parent, comp_bounds, comp_children, comp_children_used, img_refcount;
};

// 1 once the slot turned out to be someone else's (every hook then passes through); for the tests.
extern uint32_t omm_yield;
// page_host.c
void omm_live_setup(const struct omm_live_fns *f);
void omm_paint_hook(void *page, void *g);
void omm_mode_hook_c(void *self, uint32_t id);
void omm_mouse_down(void *comp, const float *ev);
void omm_mouse_drag(void *comp, const float *ev);
void omm_mouse_up(void *comp, const float *ev);
void *omm_decode(const void *data, size_t size);
void omm_send(const char *fmt, ...);
extern void *omm_live_img;
extern uintptr_t omm_paint_tramp_addr, omm_mode_tramp_addr;
extern uint32_t omm_cur_mode;

// The ModeMenuOrder hook body (C, standard calling convention); exposed for the tests.
struct omm_mode_vec { uint32_t *begin, *end, *cap; };
void omm_order_hook(void *order, const struct omm_mode_vec *modes, void *settings);

// Install the hooks for a program loaded at base. Returns NULL on success, else why it refused
// (nothing is changed when it refuses). check_segments: require the hook addresses to be in an
// executable PT_LOAD of the main program.
const char *omm_install(uintptr_t base, const struct omm_profile *p, int check_segments);
int omm_main_build_id(uintptr_t *base, uint8_t *id, size_t cap, size_t *len);
#if defined(__aarch64__)
int omm_pc_relative(uint32_t insn);   // arch_a64.c
#endif
// The first free candidate for a program loaded at base, or 0 if none is free.
uint32_t omm_pick_slot(uintptr_t base, const struct omm_profile *p);
// Load the slot's page picture / icon (PNG bytes); returns the size, 0 if there is none.
size_t omm_load_page(const char *path);
size_t omm_load_icon(const char *path);
// ---- per architecture (arch_a64.c: Gen2 aarch64; arch_a32.c: Gen1 MPC and Force, 32-bit ARM) ----
// Entry instructions a hook displaces, and the words one trampoline takes.
#if defined(__aarch64__)
#define OMM_ENTRY_WORDS 4
#define OMM_TRAMP_WORDS 16
#elif defined(__arm__)
#define OMM_ENTRY_WORDS 2
#define OMM_TRAMP_WORDS 8
#endif
// The trampoline for one hooked entry: the displaced instructions (PC-relative loads re-created), then a jump back to
// the rest of the function. t holds OMM_TRAMP_WORDS words and must be 8-byte aligned. Exposed for the tests.
void omm_fill_tramp(uint32_t *t, const uint32_t *entry_code, uintptr_t entry_addr);
// NULL if the entry instructions in code can be displaced by a trampoline, else why not.
const char *omm_arch_check_entry(const uint32_t *code);
// Overwrite the entry with a jump to hook (entry's page made writable for the write).
const char *omm_arch_patch(uint32_t *entry, void (*hook)(void), long pg);
// 1 if a jump table entry for id (the switch at table, default case at def) goes to the default case.
int omm_arch_table_default(uintptr_t base, uintptr_t table, uintptr_t table_base, uintptr_t def, uint32_t len, uint32_t id);
// The function that repaints a Rectangle<int> as (comp, x, y, w, h), from MPC's own (address real).
void (*omm_arch_repaint_rect(uintptr_t real))(void *, int, int, int, int);
// omm_call_sret(fn, a0, a1, out): fn(a0, a1) with its result object at out (hook.S; hooks_a32.c).
void omm_call_sret(void *fn, void *a0, void *a1, void *out);
// The hooks 1, 2, 4, 5 (assembly on aarch64, C on 32-bit ARM, where the result pointer is an ordinary argument).
#if defined(__aarch64__)
void omm_hook(void);
void omm_reg_hook(void);
void omm_img_hook(void);
void omm_icon_hook(void);
#else
void *omm_hook(void *out, uint32_t id);                                  // getModeName
void omm_reg_hook(void *mgr, uint32_t id, void *mode);                   // registerMode
void *omm_img_hook(void *out, const void *data, uint32_t size);          // ImageCache::getFromMemory
void *omm_icon_hook(void *out, uint32_t id);                             // the MENU icon lookup
#endif

#endif
