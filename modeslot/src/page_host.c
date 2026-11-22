// page_host: the slot as a live page for an outside app (mpc-vst-plugins docs/MPC_MODES.md, "Live page host").
//
// While the slot is the current mode:
//  - the app's latest picture, /run/openmpc-page/frame.png (written by rename), replaces the placeholder
//    picture. It is decoded here with MPC's own JUCE decoder, not through ImageCache, so only the
//    latest frame is kept;
//  - touches on the page go to the app as datagrams on /run/openmpc-page/input.sock, one text line
//    each: "enter", "size W H" (on entering, then every 2 s while shown), "down X Y", "drag X Y", "up X Y",
//    "leave" (page pixels; draw frames at W x H, or MPC scales them to fit and touches no longer line up);
//  - the page's paint checks for a new frame. While the app is active (a new frame or a touch in the
//    last ACTIVE_SECONDS) it asks for a 1 x 1 repaint so paint comes round again next refresh; a new
//    frame asks for a full repaint.
// Full height (as Developer mode has it): while the slot is shown the page is stretched up over the band
// above it (other modes' header) with juce::Component::setBounds; the bottom strip stays MPC's. MPC lays the
// page out again whenever it shows it, so this is redone after each mode change (and in paint if MPC moved
// it back), and undone when the slot is left. OMM_FULL_HEIGHT=0 turns it off.
// Everything here runs on MPC's message thread (paint, mouse callbacks, the mode-change handler).
// No app: no frame file, the page shows page.png (or MPC's placeholder), touches go nowhere.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "modeslot.h"

#define ACTIVE_SECONDS 3.0
#define MAX_FRAME (32u << 20)

extern uint32_t omm_slot_id, omm_cur_mode, omm_yield;
uintptr_t omm_paint_tramp_addr; // set by omm_install
void *omm_live_img;             // ImagePixelData * of the latest frame (one reference is ours); read by hook.S

// Functions in MPC, set by omm_install from the profile.
static struct omm_live_fns fns;
static char frame_path[256] = "/run/openmpc-page/frame.png";
static char sock_path[108] = "/run/openmpc-page/input.sock";
static int sock = -1;
static struct timespec frame_mtime;
static off_t frame_size = -1;
static double last_activity;
static void *page_comp;          // the placeholder page's juce::Component, seen in paint
static int sent_size;
static double size_sent_at;      // "size" again every SIZE_SECONDS: an app started while the page is up learns it
#define SIZE_SECONDS 2.0
static void *grown;              // the page component while stretched, and where MPC had put it
static int32_t grown_from[4];
static int grown_h;              // the height grow gave it
static void *wrap;               // its parent (the mode's wrapper) while stretched over the bottom strip, and its bounds
static int32_t wrap_from[4];
static void *seen_comp;          // the placeholder page's Component as paint saw it
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int raised_from = -1;
static int logged_once;          // the full-height success lines are logged the first time only     // where the page was in its parent's child list before it was brought to the front

// juce::Component: parentComponent at 0x18, childComponentList (Array<Component *>: elements, numAllocated, numUsed)
// at 0x40. Checked at run time: the parent's list must hold the page exactly once.
#define COMP_PARENT 0x18
#define COMP_CHILDREN 0x40
static void **child_list(void *comp, int *n, int *at)
{
    void *parent = *(void **)((char *)comp + COMP_PARENT);
    if (!parent) return NULL;
    void **kids = *(void ***)((char *)parent + COMP_CHILDREN);
    int used = *(int32_t *)((char *)parent + COMP_CHILDREN + 12);
    if (!kids || used <= 0 || used > 256) return NULL;
    int found = -1;
    for (int i = 0; i < used; i++)
        if (kids[i] == comp) {
            if (found >= 0) return NULL;
            found = i;
        }
    if (found < 0) return NULL;
    *n = used;
    *at = found;
    return kids;
}

// Bring the page in front of its siblings (MPC's header band, drawn after it), as Component::toFront does: move it
// to the end of the parent's child list. Undone by lower().
static void raise_page(void *comp)
{
    int n, at;
    void **kids = child_list(comp, &n, &at);
    if (!kids) {
        say("full height: page not found in its parent's children; not brought to the front");
        return;
    }
    if (at == n - 1) return;
    memmove(kids + at, kids + at + 1, (size_t)(n - 1 - at) * sizeof *kids);
    kids[n - 1] = comp;
    raised_from = at;
    if (!logged_once) say("full height: page brought to the front (%d of %d)", at, n);
}

static void lower_page(void *comp)
{
    int n, at;
    void **kids = raised_from >= 0 ? child_list(comp, &n, &at) : NULL;
    if (kids && at == n - 1 && raised_from < n) {
        memmove(kids + raised_from + 1, kids + raised_from, (size_t)(n - 1 - raised_from) * sizeof *kids);
        kids[raised_from] = comp;
    }
    raised_from = -1;
}

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("modeslot: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}

// omm_call_sret(fn, a0, a1, out): fn(a0, a1) with x8 = out (hook.S).
extern void omm_call_sret(void *fn, void *a0, void *a1, void *out);

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void omm_live_setup(const struct omm_live_fns *f)
{
    fns = *f;
    const char *d = getenv("OMM_PAGE_DIR");
    if (d && *d && strlen(d) < 80) {
        snprintf(frame_path, sizeof frame_path, "%s/frame.png", d);
        snprintf(sock_path, sizeof sock_path, "%s/input.sock", d);
    }
}

void omm_send(const char *fmt, ...)
{
    char line[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n <= 0 || n >= (int)sizeof line) return;
    if (sock < 0) {
        sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sock < 0) return;
    }
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    memcpy(a.sun_path, sock_path, strlen(sock_path) + 1);
    (void)sendto(sock, line, (size_t)n, MSG_DONTWAIT | MSG_NOSIGNAL, (struct sockaddr *)&a, sizeof a);
}

static void release_img(void *img)
{
    if (!img) return;
    int *rc = (int *)((char *)img + 8);
    if (__atomic_fetch_sub(rc, 1, __ATOMIC_ACQ_REL) == 1) {
        void (**vt)(void *) = *(void (***)(void *))img;
        vt[1](img); // deleting destructor
    }
}

// Decode PNG bytes with MPC's JUCE: MemoryInputStream, ImageFileFormat::findImageFormatForStream,
// format->decodeImage. Returns an ImagePixelData * holding one reference, or NULL.
void *omm_decode(const void *data, size_t size)
{
    _Alignas(16) unsigned char stream[256];
    memset(stream, 0, sizeof stream);
    fns.memstream_ctor(stream, data, size, 0);
    void *fmt = fns.find_format(stream);
    void *img = NULL;
    if (fmt) {
        void *decode = (*(void ***)fmt)[fns.decode_vslot / 8];
        omm_call_sret(decode, fmt, stream, &img);
    }
    fns.memstream_dtor(stream);
    return img;
}

// A changed frame file: load and decode it. Returns 1 if the live picture changed.
static int poll_frame(void)
{
    struct stat st;
    if (stat(frame_path, &st) != 0) {
        if (omm_live_img) {
            release_img(omm_live_img);
            omm_live_img = NULL;
            frame_size = -1;
            return 1;
        }
        return 0;
    }
    if (st.st_size == frame_size && st.st_mtim.tv_sec == frame_mtime.tv_sec && st.st_mtim.tv_nsec == frame_mtime.tv_nsec)
        return 0;
    frame_size = st.st_size;
    frame_mtime = st.st_mtim;
    if (st.st_size <= 8 || (size_t)st.st_size > MAX_FRAME) return 0;
    int fd = open(frame_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char *buf = malloc((size_t)st.st_size);
    size_t n = 0;
    while (buf && n < (size_t)st.st_size) {
        ssize_t r = read(fd, buf + n, (size_t)st.st_size - n);
        if (r <= 0) break;
        n += (size_t)r;
    }
    close(fd);
    void *img = (buf && n == (size_t)st.st_size && !memcmp(buf, "\x89PNG", 4)) ? omm_decode(buf, n) : NULL;
    free(buf);
    if (!img) return 0;
    release_img(omm_live_img);
    omm_live_img = img;
    last_activity = now();
    return 1;
}

static void repaint_full(void) { if (page_comp) fns.repaint(page_comp); }

// The placeholder page's Component, through the touch UI's page holder; NULL unless the object there really is
// a TouchDummyPage (its vtable), so setBounds never gets a stray pointer.
static void *dummy_comp(void *touch_ui)
{
    if (!fns.dummy_page_offset || !fns.page_holder_field || !fns.page_vtable) return NULL;
    void *page = *(void **)((char *)touch_ui + fns.dummy_page_offset + fns.page_holder_field);
    if (!page || *(uintptr_t *)page != fns.page_vtable) {
        say("full height: no placeholder page at the holder (%p); not moved", page);
        return NULL;
    }
    return (char *)page + fns.page_component_offset;
}

// Stretch the page up to its parent's top, keeping its bottom edge. 1 if it moved.
static int grow(void *comp)
{
    if (!fns.set_bounds || !comp) return 0;
    if (seen_comp && comp != seen_comp) {
        say("full height: page %p is not the one paint saw (%p); not moved", comp, seen_comp);
        return 0;
    }
    const int32_t *b = (const int32_t *)((char *)comp + 0x20); // x, y, w, h
    if (b[1] <= 0 || b[3] <= 0 || b[2] <= 0 || b[1] > 800 || b[3] > 800 || b[2] > 2000) return 0;
    memcpy(grown_from, b, sizeof grown_from);
    grown = comp;
    int x = b[0], y = b[1], w = b[2], h = b[3], bottom = y + h;
    // The whole screen: the mode's wrapper (the page's parent, checked by its vtable) stops above MPC's bottom strip;
    // stretch it to its own parent's bottom too. The strip is empty for the slot (the placeholder page has no tabs).
    void *parent = *(void **)((char *)comp + COMP_PARENT);
    void *gp = parent ? *(void **)((char *)parent + COMP_PARENT) : NULL;
    if (fns.whole_screen && fns.wrapper_vtable && gp && *(uintptr_t *)parent == fns.wrapper_vtable) {
        const int32_t *pb = (const int32_t *)((char *)parent + 0x20), *gb = (const int32_t *)((char *)gp + 0x20);
        if (pb[1] >= 0 && pb[1] + pb[3] < gb[3] && gb[3] <= 2000) {
            memcpy(wrap_from, pb, sizeof wrap_from);
            wrap = parent;
            fns.set_bounds(parent, pb[0], pb[1], pb[2], gb[3] - pb[1]);
            bottom = pb[3];                                    // now the wrapper's whole height
        }
    }
    fns.set_bounds(comp, x, 0, w, bottom);
    grown_h = bottom;
    if (!logged_once) say("full height: page from %d,%d %dx%d to %d,%d %dx%d", x, y, w, h, b[0], b[1], b[2], b[3]);
    raise_page(comp);
    logged_once = 1;
    fns.repaint(comp);
    sent_size = 0;                                             // the app learns the new size
    return 1;
}

// Put the page back where MPC had it (if it is still where grow left it).
static void shrink(void)
{
    if (!grown) return;
    lower_page(grown);
    if (wrap) {                                                // the wrapper first: its layout may move the page back itself
        const int32_t *pb = (const int32_t *)((char *)wrap + 0x20);
        if (pb[0] == wrap_from[0] && pb[1] == wrap_from[1] && pb[2] == wrap_from[2])
            fns.set_bounds(wrap, wrap_from[0], wrap_from[1], wrap_from[2], wrap_from[3]);
        wrap = NULL;
    }
    const int32_t *b = (const int32_t *)((char *)grown + 0x20);
    if (b[0] == grown_from[0] && b[1] == 0 && b[2] == grown_from[2] && b[3] == grown_h) {
        fns.set_bounds(grown, grown_from[0], grown_from[1], grown_from[2], grown_from[3]);
    }
    grown = NULL;
}
// One pixel at the page's centre: something opaque on top (a toolbar) could hide a corner and stop the poll.
static void repaint_pixel(void)
{
    if (!page_comp) return;
    const int32_t *b = (const int32_t *)((char *)page_comp + 0x20); // x, y, w, h
    uint32_t x = (uint32_t)(b[2] > 0 ? b[2] / 2 : 0), y = (uint32_t)(b[3] > 0 ? b[3] / 2 : 0);
    fns.repaint_area(page_comp, ((uint64_t)y << 32) | x, ((uint64_t)1 << 32) | 1);
}

// 7. TouchDummyPage::paint(page, Graphics &).
void omm_paint_hook(void *page, void *g)
{
    void (*orig)(void *, void *) = (void (*)(void *, void *))omm_paint_tramp_addr;
    if (omm_cur_mode == omm_slot_id && !omm_yield) {
        page_comp = (char *)page + fns.page_component_offset;
        if (page_comp != seen_comp) {
            const int32_t *pb = (const int32_t *)((char *)page_comp + 0x20);
            if (seen_comp) say("page host: the page is a new Component (%p) at %d,%d %dx%d", page_comp, pb[0], pb[1], pb[2], pb[3]);
            seen_comp = page_comp;
        }
        double t = now();
        if (!sent_size || t - size_sent_at >= SIZE_SECONDS) {
            const int32_t *b = (const int32_t *)((char *)page_comp + 0x20); // x, y, w, h
            omm_send("size %d %d", b[2], b[3]);
            sent_size = 1;
            size_sent_at = t;
        }
        if (poll_frame()) repaint_full();
        else if (now() - last_activity < ACTIVE_SECONDS) repaint_pixel();
    }
    orig(page, g);
}

// 3. The touch UI's mode change (this, ModeID): records the current screen mode and tells the app when
// the slot is entered or left.
uintptr_t omm_mode_tramp_addr;
void omm_mode_hook_c(void *self, uint32_t id)
{
    void (*orig)(void *, uint32_t) = (void (*)(void *, uint32_t))omm_mode_tramp_addr;
    int entered = 0;
    if (id - 0x2080u < 0x80u) {
        uint32_t prev = omm_cur_mode;
        omm_cur_mode = id;
        if (!omm_yield && omm_slot_id) {
            if (id == omm_slot_id) {
                if (prev != omm_slot_id) {
                    omm_send("enter");
                    sent_size = 0;
                    last_activity = now();
                }
                entered = 1;
            } else if (prev == omm_slot_id) {
                shrink();                                       // before MPC shows the next page (it may be this one)
                omm_send("leave");
            }
        }
    }
    orig(self, id);
    // MPC has laid the placeholder page out for the slot: stretch it. (Not from paint: setBounds there would
    // change the page while it is being drawn.)
    if (entered && fns.set_bounds) grow(dummy_comp(self));
}

// Mouse callbacks put in the placeholder page's juce::Component vtable (mouseDown, mouseDrag, mouseUp).
// MPC's are JUCE's empty defaults, so outside the slot they still do nothing.
static void mouse(void *comp, const float *ev, const char *what)
{
    (void)comp;
    if (omm_cur_mode != omm_slot_id || omm_yield) return;
    omm_send("%s %d %d", what, (int)ev[0], (int)ev[1]); // MouseEvent::position, page pixels
    last_activity = now();
    page_comp = comp;
    repaint_pixel();
}
void omm_mouse_down(void *comp, const float *ev) { mouse(comp, ev, "down"); }
void omm_mouse_drag(void *comp, const float *ev) { mouse(comp, ev, "drag"); }
void omm_mouse_up(void *comp, const float *ev) { mouse(comp, ev, "up"); }
