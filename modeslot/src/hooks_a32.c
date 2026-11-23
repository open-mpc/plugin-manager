// hooks_a32: hooks 1, 2, 4 and 5 for 32-bit ARM. On this ABI a function returning a class by value (juce::String,
// juce::Image) gets the result's address as an ordinary first argument (r0) and returns it, so the hooks are plain C.
// Each one runs the original through its trampoline (omm_*_tramp_addr) and only adds to it or changes arguments; the
// aarch64 versions (hook.S) do the same with the result pointer in x8.
#define _GNU_SOURCE
#include <stdint.h>

#include "modeslot.h"

extern uintptr_t omm_tramp_addr, omm_reg_tramp_addr, omm_img_tramp_addr, omm_icon_tramp_addr;
extern uintptr_t omm_ctor;
extern uint32_t omm_slot_id, omm_alias_from, omm_dummy_size, omm_png_size, omm_icon_size, omm_img_refcount;
extern const char *omm_slot_name;
extern const void *omm_dummy_data, *omm_png_data, *omm_icon_data;

typedef void *(*name_fn)(void *out, uint32_t id);
typedef void (*reg_fn)(void *mgr, uint32_t id, void *mode);
typedef void *(*img_fn)(void *out, const void *data, uint32_t size);
typedef void (*string_ctor_fn)(void *out, const char *text);

// 1. getModeName(out, ModeID) -> juce::String. For the slot it runs the original first: stock MPC has no name for it, so
// a name there means something else owns it, and the shim stands aside for good (omm_yield = 1, that name is returned).
// Otherwise, and unless it has already stood aside, it builds juce::String(omm_slot_name) into *out (the empty string the
// original built uses a shared static, so nothing leaks).
void *omm_hook(void *out, uint32_t id)
{
    name_fn orig = (name_fn)omm_tramp_addr;
    if (id != omm_slot_id) return orig(out, id);
    orig(out, id);
    const char *text = *(const char *const *)out; // juce::String: the pointer to its UTF-8 text
    if (text && *text) {
        omm_yield = 1;
        return out;
    }
    if (!omm_yield) ((string_ctor_fn)omm_ctor)(out, omm_slot_name);
    return out;
}

// 2. registerMode(manager, ModeID, Mode *). If MPC or another mod registers the slot itself, the shim stands aside.
// Runs the original; for omm_alias_from (Main Mode) it runs it again for the slot with the same controller object.
void omm_reg_hook(void *mgr, uint32_t id, void *mode)
{
    reg_fn orig = (reg_fn)omm_reg_tramp_addr;
    if (id == omm_slot_id) omm_yield = 1;
    orig(mgr, id, mode);
    if (id == omm_alias_from && !omm_yield) orig(mgr, omm_slot_id, mode);
}

// 4. juce::ImageCache::getFromMemory(out, data, size) -> Image. If the slot is the current mode and (data, size) is MPC's
// placeholder picture: with a live frame from the app (omm_live_img) return that Image (one more reference: *out = img);
// otherwise, with page.png loaded, load ours instead.
void *omm_img_hook(void *out, const void *data, uint32_t size)
{
    img_fn orig = (img_fn)omm_img_tramp_addr;
    if (!omm_yield && omm_cur_mode == omm_slot_id && size == omm_dummy_size && data == omm_dummy_data) {
        if (omm_live_img) {
            *(void **)out = omm_live_img;
            __atomic_fetch_add((int *)((char *)omm_live_img + omm_img_refcount), 1, __ATOMIC_ACQ_REL);
            return out;
        }
        if (omm_png_size) {
            data = omm_png_data;
            size = omm_png_size;
        }
    }
    return orig(out, data, size);
}

// 5. The MENU icon lookup (out, ModeID) -> Image. For the slot, with an icon loaded: getFromMemory(omm_icon_data,
// omm_icon_size) through its original code. Otherwise run the lookup.
void *omm_icon_hook(void *out, uint32_t id)
{
    if (!omm_yield && id == omm_slot_id && omm_icon_size)
        return ((img_fn)omm_img_tramp_addr)(out, omm_icon_data, omm_icon_size);
    return ((name_fn)omm_icon_tramp_addr)(out, id);
}

// omm_call_sret(fn, a0, a1, out): fn(a0, a1) with its result object at out.
void omm_call_sret(void *fn, void *a0, void *a1, void *out)
{
    ((void (*)(void *, void *, void *))fn)(out, a0, a1);
}
