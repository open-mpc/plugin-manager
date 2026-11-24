// Offline test of the modeslot hooks on aarch64 (run under qemu in Docker by ../build.sh test).
// Hooks stand-ins for MPC's four functions (test_fake.S) and checks what each hook does, the
// relocated adrp, return values and callee-saved registers, and that every refusal leaves the code
// unchanged.
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stddef.h>

#include "modeslot.h"
#include "test_arch.h"

struct jstring { const char *text; void *pad[2]; }; // >16 bytes, so aarch64 passes it through x8

void fake_ctor(struct jstring *self, const char *s) { self->text = s; }
void fake_get_mode_name(void);
void fake_register(void);
void fake_mode_changed(void);
void fake_image(void);
void fake_icon(void);
void fake_order(void);
void fake_paint(void);

// Page host stand-ins.
static struct { void *page, *g; unsigned long x3; int n; } last_paint;
void fake_paint_record(void *page, void *g, unsigned long x3)
{
    last_paint.page = page; last_paint.g = g; last_paint.x3 = x3; last_paint.n++;
}
static struct { int full, pixel; void *comp; uint64_t xy, wh; } rp;
static void fake_repaint(void *c) { rp.full++; rp.comp = c; }
DEFINE_FAKE_REPAINT_AREA
static const void *ms_data;
static size_t ms_size;
static int ms_live;
static void fake_ms_ctor(void *s, const void *d, size_t n, int keep) { (void)s; (void)keep; ms_data = d; ms_size = n; ms_live++; }
static void fake_ms_dtor(void *s) { (void)s; ms_live--; }
static void *fake_fmt_vt[8];
static void *fake_fmt = fake_fmt_vt;
static void *fake_find(void *s) { (void)s; return &fake_fmt; }
int fake_decodes;
static int img_deleted;
static void fake_img_delete(void *p) { (void)p; img_deleted++; }
static void *fake_img_vt[2] = {NULL, (void *)fake_img_delete};
struct fake_img { void **vt; int rc; };
struct fake_img fake_imgdata = {fake_img_vt, 1};
DEFINE_FAKE_DECODE
static void fake_mouse_default(void *c, const float *e) { (void)c; (void)e; }
static struct { void *comp; int x, y, w, h, n; } sb;
static void fake_set_bounds(void *c, int x, int y, int w, int h)
{
    int32_t *b = (int32_t *)((char *)c + T_BOUNDS);
    b[0] = x; b[1] = y; b[2] = w; b[3] = h;
    sb.comp = c; sb.x = x; sb.y = y; sb.w = w; sb.h = h; sb.n++;
}
static char pagebuf[0x100] __attribute__((aligned(16))); // the placeholder page: + 16 its Component, bounds at + 0x20 from that
static char holder[0x400] __attribute__((aligned(16)));  // the touch UI's page holder: + 0x300 the page
static void *fake_page_vt[4];
static char fake_parent[0x100] __attribute__((aligned(16)));  // the page's parent: its child list at + 0x40
static char fake_header[0x100];
static char fake_gp[0x100] __attribute__((aligned(16)));   // the wrapper's parent: the whole screen
static void *fake_wrap_vt[4];
static void *kids[2];
static uintptr_t fake_vtable[2048] __attribute__((aligned(16384))); // a page of its own: the shim makes it read-only again
static struct { void *order, *settings; unsigned ids[64]; int n, calls; const void *vec; } last_order;
void fake_order_record(void *order, const struct omm_mode_vec *v, void *settings)
{
    last_order.order = order; last_order.settings = settings; last_order.vec = v; last_order.calls++;
    last_order.n = (int)(v->end - v->begin);
    for (int i = 0; i < last_order.n && i < 64; i++) last_order.ids[i] = v->begin[i];
}
extern const char *fake_slot_name;

static struct { void *mgr; unsigned id; void *mode; } calls[16];
static int ncalls;
void fake_record(void *mgr, unsigned id, void *mode)
{
    if (ncalls < 16) { calls[ncalls].mgr = mgr; calls[ncalls].id = id; calls[ncalls].mode = mode; }
    ncalls++;
}
static struct { void *self; unsigned id; } last_mode;
void fake_mode_record(void *self, unsigned id) { last_mode.self = self; last_mode.id = id; }
static struct { const void *data; unsigned size; unsigned long x2; void *x8; } last_img;
void fake_img_record(const void *data, unsigned size, unsigned long x2, void *x8)
{
    last_img.data = data; last_img.size = size; last_img.x2 = x2; last_img.x8 = x8;
}

static struct { unsigned id; void *x8; unsigned long x1; int n; } last_icon;
void fake_icon_record(unsigned id, void *x8, unsigned long x1)
{
    last_icon.id = id; last_icon.x8 = x8; last_icon.x1 = x1; last_icon.n++;
}

int fails;

static int runs;          // the launcher tile's command, recorded instead of run
static char ran[64];
static void tile_runner(const char *c) { runs++; snprintf(ran, sizeof ran, "%s", c); }

static const char *name_of(unsigned id)
{
    struct jstring s = {"(unset)", {0, 0}};
    void *r = CALL_NAME(id, &s);
    CHECK(r == &s, "id %#x: result %p, want %p (or a callee-saved register clobbered)", id, r, (void *)&s);
    return s.text;
}

static unsigned char placeholder[100] = "\x89PNG\r\n\x1a\n placeholder";
static const void *placeholder_ptr = placeholder;
static int obj; // the touch UI (the mode handler's this); its placeholder page is pagebuf

static void mode(unsigned id)
{
    CHECK(CALL_MODE(&obj, id) == 0, "mode %#x: registers clobbered", id);
    CHECK(last_mode.self == &obj && last_mode.id == id, "mode %#x: original saw %#x", id, last_mode.id);
}

static const void *image(const void *data, unsigned size)
{
    int out;
    memset(&last_img, 0, sizeof last_img);
    CHECK(CALL_IMAGE(data, size, &out) == 0, "image: registers clobbered");
    CHECK(last_img.x2 == FAKE_VAR, "image: relocated entry loaded %#lx, want %#lx", last_img.x2, (unsigned long)FAKE_VAR);
    CHECK(last_img.x8 == &out, "image: x8 not passed through");
    return last_img.data;
}

// Stand-in mode tables: entry 5 is the default case (free), 7 a named / real page.
static TAB_T name_tab[0x25], page_tab[0x25];
static void tables_reset(void)
{
    for (int i = 0; i < 0x25; i++) TAB_SET(name_tab, i, 1), TAB_SET(page_tab, i, 1);
    TAB_SET(name_tab, 0x10, 0); TAB_SET(name_tab, 0x12, 0); TAB_SET(name_tab, 0x19, 0);
    TAB_SET(page_tab, 0x10, 0); TAB_SET(page_tab, 0x12, 0); TAB_SET(page_tab, 0x19, 0); TAB_SET(page_tab, 0x0b, 0);
}

static struct omm_site site(void (*fn)(void))
{
    struct omm_site s = {(uintptr_t)fn, {0}};
    memcpy(s.prologue, (const void *)fn, 16);
    return s;
}

static struct omm_profile profile(void)
{
    struct omm_profile p = {
        .label = "test",
        .candidates = {0x2090, 0x2092, 0x2099, 0},
        .slot_name = "Plugin Manager",
        .name_table = (uintptr_t)name_tab, .name_table_base = T_NAME_BASE, .name_default = T_NAME_DEFAULT,
        .page_table = (uintptr_t)page_tab, .page_table_base = T_PAGE_BASE, .page_default = T_PAGE_DEFAULT,
        .table_len = 0x25,
        .get_mode_name = {(uintptr_t)fake_get_mode_name, T_PRO_GETNAME},
        .string_ctor = T_CTOR_ADDR,
        .string_ctor_first = T_CTOR_FIRST,
        .register_mode = {(uintptr_t)fake_register, T_PRO_REG},
        .alias_from = 0x2080,
        .mode_changed = {(uintptr_t)fake_mode_changed, T_PRO_MODE},
        .image_from_memory = site(fake_image),
        .placeholder_ptr_var = (uintptr_t)&placeholder_ptr,
        .placeholder_size = sizeof placeholder,
        .mode_icon = site(fake_icon),
        .menu_order = {(uintptr_t)fake_order, T_PRO_ORDER},
        .page_paint = site(fake_paint),
        .mouse_slots = {(uintptr_t)&fake_vtable[0], (uintptr_t)&fake_vtable[1], (uintptr_t)&fake_vtable[2]},
        .mouse_default = (uintptr_t)fake_mouse_default,
        .repaint = {(uintptr_t)fake_repaint, *(const uint32_t *)fake_repaint},
        .repaint_area = {(uintptr_t)fake_repaint_area, *(const uint32_t *)fake_repaint_area},
        .memstream_ctor = {(uintptr_t)fake_ms_ctor, *(const uint32_t *)fake_ms_ctor},
        .memstream_dtor = {(uintptr_t)fake_ms_dtor, *(const uint32_t *)fake_ms_dtor},
        .find_format = {(uintptr_t)fake_find, *(const uint32_t *)fake_find},
        .decode_vslot = T_DECODE,
        .page_component_offset = T_COMP,
        .set_bounds = {(uintptr_t)fake_set_bounds, *(const uint32_t *)fake_set_bounds},
        .dummy_page_offset = (uintptr_t)holder - (uintptr_t)&obj,
        .page_holder_field = T_HOLDER_FIELD,
        .page_vtable = (uintptr_t)fake_page_vt,
        .wrapper_vtable = (uintptr_t)fake_wrap_vt,
        .comp_parent = T_PARENT, .comp_bounds = T_BOUNDS, .comp_children = T_CHILDREN, .comp_children_used = T_USED,
        .img_refcount = offsetof(struct fake_img, rc),
    };
    return p;
}

static int rx = -1;
static char rxdir[] = "/tmp/modeslot-page-XXXXXX";
static int recv_line(char *buf, size_t n)
{
    ssize_t r = recv(rx, buf, n - 1, MSG_DONTWAIT);
    if (r < 0) { buf[0] = 0; return 0; }
    buf[r] = 0;
    return 1;
}
static void drain(void) { char b[64]; while (recv_line(b, sizeof b)) {} }
static void write_frame(const char *body)
{
    char tmp[200], path[200];
    snprintf(tmp, sizeof tmp, "%s/frame.tmp", rxdir);
    snprintf(path, sizeof path, "%s/frame.png", rxdir);
    FILE *f = fopen(tmp, "wb");
    fprintf(f, "\x89PNG\r\n\x1a\n%s", body);
    fclose(f);
    rename(tmp, path);
}

int main(void)
{
    // The page host's socket and frame live in a temporary folder (OMM_PAGE_DIR).
    if (!mkdtemp(rxdir)) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("OMM_PAGE_DIR", rxdir, 1);
    rx = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un ra = {.sun_family = AF_UNIX};
    snprintf(ra.sun_path, sizeof ra.sun_path, "%s/input.sock", rxdir);
    if (rx < 0 || bind(rx, (struct sockaddr *)&ra, sizeof ra) != 0) { printf("FAIL bind\n"); return 1; }
    fake_fmt_vt[T_DECODE / sizeof(void *)] = (void *)fake_decode;
    for (int i = 0; i < 3; i++) fake_vtable[i] = (uintptr_t)fake_mouse_default;
    void (*fns[7])(void) = {fake_get_mode_name, fake_register, fake_mode_changed, fake_image, fake_icon, fake_order, fake_paint};
    unsigned char before[7][16], after[16];
    for (int i = 0; i < 7; i++) memcpy(before[i], (void *)fns[i], 16);
    int mgr, modeA, modeB;

    CHECK(!strcmp(name_of(0x2090), ""), "stock 0x2090 not empty");
    CHECK(!strcmp(name_of(0x2091), "Browser"), "stock 0x2091");
    CHECK(image(placeholder, sizeof placeholder) == placeholder, "stock image");

    arch_tramp_tests();

    // Picking the slot: the first candidate both tables send to their default case.
    tables_reset();
    struct omm_profile p = profile();
    CHECK(omm_pick_slot(0, &p) == 0x2090, "pick: %#x", omm_pick_slot(0, &p));
    TAB_SET(name_tab, 0x10, 1); // MPC (or a mod) named 0x2090
    CHECK(omm_pick_slot(0, &p) == 0x2092, "pick with 0x2090 named: %#x", omm_pick_slot(0, &p));
    TAB_SET(page_tab, 0x12, 1); // 0x2092 got a page of its own
    CHECK(omm_pick_slot(0, &p) == 0x2099, "pick with 0x2092 paged: %#x", omm_pick_slot(0, &p));
    TAB_SET(name_tab, 0x19, 1);
    CHECK(omm_pick_slot(0, &p) == 0, "pick with none free: %#x", omm_pick_slot(0, &p));
    CHECK(omm_install(0, &p, 0) != NULL, "installed with no free slot");
    p.candidates[0] = 0x3090; // outside the tables
    p.candidates[1] = 0;
    CHECK(omm_pick_slot(0, &p) == 0, "picked an ID outside the tables");
    tables_reset();

    // Refusals change nothing.
    p = profile();
    p.get_mode_name.prologue[T_LAST] ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong prologue");
    p = profile();
    p.register_mode.prologue[0] ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong register prologue");
    p = profile();
    p.image_from_memory.prologue[T_MID] ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong image prologue");
    p = profile();
    p.mode_changed.prologue[1] = T_BL_INSN; // a bl: not relocatable
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a PC-relative prologue");
    p = profile();
    p.mode_changed.addr = 0;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted the image hook without the mode hook");
    p = profile();
    placeholder[0] = 0;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a placeholder that is not a PNG");
    placeholder[0] = 0x89;
    p = profile();
    p.string_ctor_first = T_CTOR_FIRST ^ 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong ctor");
    static uint32_t data_copy[4] = T_PRO_GETNAME; // right bytes, not code
    p = profile();
    p.get_mode_name.addr = (uintptr_t)data_copy;
    CHECK(omm_install(0, &p, 1) != NULL, "accepted an address outside the executable segments");
    p = profile();
    p.mode_icon.prologue[T_MID] ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong icon prologue");
    p = profile();
    p.image_from_memory.addr = 0;
    p.mode_changed.addr = 0;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted the icon hook without the image hook");
    p = profile();
    p.menu_order.prologue[T_LAST] ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong menu-order prologue");
    CHECK(fake_vtable[0] == (uintptr_t)fake_mouse_default, "a refusal changed a mouse slot");
    p = profile();
    p.page_paint.prologue[0] ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong paint prologue");
    p = profile();
    p.repaint.first ^= 1;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a wrong repaint function");
    p = profile();
    fake_vtable[1] = 1234;
    CHECK(omm_install(0, &p, 0) != NULL, "accepted a mouse slot that is not JUCE's default");
    fake_vtable[1] = (uintptr_t)fake_mouse_default;
    for (int i = 0; i < 7; i++) {
        memcpy(after, (void *)fns[i], 16);
        CHECK(!memcmp(before[i], after, 16), "a refusal changed function %d", i);
    }

    uintptr_t base = 1;
    unsigned char id[64];
    size_t len = 0;
    CHECK(omm_main_build_id(&base, id, sizeof id, &len) == 0 && len == 20, "build-id not found (len %zu)", len);

    p = profile();
    const char *err = omm_install(0, &p, 0);
    CHECK(err == NULL, "install: %s", err ? err : "");

    // 1. the name
    CHECK(!strcmp(name_of(0x2090), "Plugin Manager"), "hooked 0x2090: \"%s\"", name_of(0x2090));
    CHECK(!strcmp(name_of(0x2091), "Browser"), "hooked 0x2091");
    CHECK(!strcmp(name_of(0x2092), ""), "hooked 0x2092 not empty");
    CHECK(!strcmp(name_of(0), ""), "hooked 0 not empty");
    for (int i = 0; i < 100000; i++) // the trampoline path, many times
        if (strcmp(name_of(i & 1 ? 0x2091 : 0x2090), i & 1 ? "Browser" : "Plugin Manager")) { fails++; break; }

    // 2. the borrowed controller
    ncalls = 0;
    CHECK(CALL_REG(&mgr, 0x2084, &modeA) == 0, "registers clobbered (0x2084)");
    CHECK(ncalls == 1 && calls[0].id == 0x2084 && calls[0].mgr == &mgr && calls[0].mode == &modeA,
          "0x2084: %d calls", ncalls);
    ncalls = 0;
    CHECK(CALL_REG(&mgr, 0x2080, &modeB) == 0, "registers clobbered (0x2080)");
    CHECK(ncalls == 2 && calls[0].id == 0x2080 && calls[1].id == 0x2090 && calls[1].mgr == &mgr &&
          calls[0].mode == &modeB && calls[1].mode == &modeB, "0x2080: %d calls, second id %#x", ncalls, calls[1].id);

    // 3 + 4. the page picture
    unsigned char other[100];
    mode(0x2080);
    CHECK(image(placeholder, sizeof placeholder) == placeholder, "Main Mode: placeholder swapped");
    mode(0x2090);
    CHECK(image(placeholder, sizeof placeholder) == placeholder, "slot, no page loaded: placeholder swapped");
    char path[] = "/tmp/modeslot-page-XXXXXX";
    int fd = mkstemp(path);
    const char png[] = "\x89PNG\r\n\x1a\n our page";
    CHECK(fd >= 0 && write(fd, png, sizeof png) == (ssize_t)sizeof png, "temp page");
    close(fd);
    CHECK(omm_load_page("/nonexistent/page.png") == 0, "loaded a missing page");
    CHECK(omm_load_page(path) == sizeof png, "page not loaded");
    const void *got = image(placeholder, sizeof placeholder);
    CHECK(got != placeholder && got && !memcmp(got, png, sizeof png) && last_img.size == sizeof png,
          "slot: page not swapped in (size %u)", last_img.size);
    CHECK(image(other, sizeof placeholder) == other, "slot: another picture swapped");
    CHECK(image(placeholder, sizeof placeholder - 1) == placeholder, "slot: other size swapped");
    mode(0x3085); // a popup: the screen mode stays the slot
    CHECK(image(placeholder, sizeof placeholder) != placeholder, "popup changed the screen mode");
    mode(0x2081);
    CHECK(image(placeholder, sizeof placeholder) == placeholder, "left the slot: still swapped");
    unlink(path);

    // 5. the MENU icon
    int iout;
    last_icon.n = 0;
    CHECK(CALL_ICON(0x2091, &iout) == 0, "icon: registers clobbered");
    CHECK(last_icon.n == 1 && last_icon.id == 0x2091 && last_icon.x8 == &iout && last_icon.x1 == FAKE_VAR,
          "icon 0x2091: lookup not run as is (n %d, x1 %#lx)", last_icon.n, last_icon.x1);
    CHECK(CALL_ICON(0x2090, &iout) == 0 && last_icon.n == 2 && last_icon.id == 0x2090,
          "icon 0x2090 with no icon loaded: lookup not run");
    char ipath[] = "/tmp/modeslot-icon-XXXXXX";
    fd = mkstemp(ipath);
    const char ipng[] = "\x89PNG\r\n\x1a\n our icon";
    CHECK(fd >= 0 && write(fd, ipng, sizeof ipng) == (ssize_t)sizeof ipng, "temp icon");
    close(fd);
    CHECK(omm_load_icon(ipath) == sizeof ipng, "icon not loaded");
    memset(&last_img, 0, sizeof last_img);
    CHECK(CALL_ICON(0x2090, &iout) == 0, "icon slot: registers clobbered");
    CHECK(last_icon.n == 2 && last_img.data && !memcmp(last_img.data, ipng, sizeof ipng) &&
          last_img.size == sizeof ipng && last_img.x8 == &iout && last_img.x2 == FAKE_VAR,
          "icon 0x2090: our PNG not loaded through getFromMemory (n %d, size %u)", last_icon.n, last_img.size);
    CHECK(CALL_ICON(0x2080, &iout) == 0 && last_icon.n == 3 && last_icon.id == 0x2080,
          "icon 0x2080: lookup not run");
    unlink(ipath);

    // 6. the MENU grid's list of allowed modes
    {
        int order, settings;
        uint32_t ids[3] = {0x2091, 0x2083, 0x2082};
        struct omm_mode_vec v = {ids, ids + 3, ids + 3};
        last_order.calls = 0;
        CHECK(CALL_ORDER(&order, &v, &settings) == 0, "order: registers clobbered");
        CHECK(last_order.calls == 1 && last_order.order == &order && last_order.settings == &settings &&
              last_order.n == 4 && last_order.ids[0] == 0x2091 && last_order.ids[2] == 0x2082 &&
              last_order.ids[3] == 0x2090 && last_order.vec != &v,
              "order: slot not appended (n %d, last %#x)", last_order.n, last_order.ids[3]);
        CHECK(ids[0] == 0x2091 && v.end == ids + 3, "order: caller's list changed");
        uint32_t with[2] = {0x2090, 0x2091};
        struct omm_mode_vec w = {with, with + 2, with + 2};
        CHECK(CALL_ORDER(&order, &w, &settings) == 0 && last_order.calls == 2 &&
              last_order.vec == &w && last_order.n == 2, "order: list that has the slot was not passed as is");
        struct omm_mode_vec e = {NULL, NULL, NULL};
        CHECK(CALL_ORDER(&order, &e, &settings) == 0 && last_order.calls == 3 &&
              last_order.n == 1 && last_order.ids[0] == 0x2090, "order: empty list");
    }

    // 7. The live page host.
    {
        char line[64];
        int32_t *b = (int32_t *)(pagebuf + T_COMP + T_BOUNDS);
        *(void **)pagebuf = fake_page_vt;
        *(void **)(holder + T_HOLDER_FIELD) = pagebuf;
        b[0] = 0; b[1] = 0; b[2] = 1280; b[3] = 800; // already at the top: full height has nothing to do
        drain();
        mode(0x2080);
        mode(0x2090);
        recv_line(line, sizeof line);
        CHECK(!strcmp(line, "enter"), "enter: \"%s\"", line);
        // a paint with no frame: the 1 x 1 poll (a touch or enter counts as activity)
        memset(&rp, 0, sizeof rp);
        int n0 = last_paint.n;
        CHECK(CALL_PAINT(pagebuf, (void *)77) == 0, "paint: registers clobbered");
        CHECK(last_paint.n == n0 + 1 && last_paint.page == pagebuf && last_paint.g == (void *)77 && last_paint.x3 == FAKE_VAR,
              "paint: original not run as is");
        CHECK(rp.pixel == 1 && rp.full == 0 && rp.comp == pagebuf + T_COMP && rp.xy == ((400ull << 32) | 640) && rp.wh == ((1ull << 32) | 1),
              "paint, no frame: pixel %d full %d", rp.pixel, rp.full);
        recv_line(line, sizeof line);
        CHECK(!strcmp(line, "size 1280 800"), "size: \"%s\"", line);
        // a frame: decoded once, full repaint, then the placeholder request gets it
        write_frame("frame one");
        memset(&rp, 0, sizeof rp);
        CALL_PAINT(pagebuf, (void *)77);
        CHECK(fake_decodes == 1 && ms_live == 0 && ms_size == 8 + 9, "frame: decodes %d, stream %d, size %zu",
              fake_decodes, ms_live, ms_size);
        CHECK(rp.full == 1, "frame: no full repaint");
        CHECK(omm_live_img == &fake_imgdata, "frame: live image not set");
        void *out = NULL;
        int rc0 = fake_imgdata.rc;
        CHECK(CALL_IMAGE(placeholder, sizeof placeholder, &out) == 0, "live image: registers");
        CHECK(out == &fake_imgdata && fake_imgdata.rc == rc0 + 1, "live image not returned with a reference (rc %d)",
              fake_imgdata.rc);
        fake_imgdata.rc--; // the caller's Image goes away
        CHECK(image(other, sizeof placeholder) == other, "live image given for another picture");
        // the same frame again: no new decode
        CALL_PAINT(pagebuf, (void *)77);
        CHECK(fake_decodes == 1, "same frame decoded again");
        // touches
        void (*md)(void *, const float *) = (void (*)(void *, const float *))fake_vtable[0];
        void (*mu)(void *, const float *) = (void (*)(void *, const float *))fake_vtable[2];
        float ev[2] = {12.7f, 34.2f};
        drain();
        md(pagebuf + T_COMP, ev);
        recv_line(line, sizeof line);
        CHECK(!strcmp(line, "down 12 34"), "down: \"%s\"", line);
        mu(pagebuf + T_COMP, ev);
        recv_line(line, sizeof line);
        CHECK(!strcmp(line, "up 12 34"), "up: \"%s\"", line);
        // frame removed: the live image is released (our reference was the last)
        char path[200];
        snprintf(path, sizeof path, "%s/frame.png", rxdir);
        unlink(path);
        CALL_PAINT(pagebuf, (void *)77);
        CHECK(omm_live_img == NULL && img_deleted == 1, "frame removed: live %p, deleted %d", omm_live_img, img_deleted);
        // leaving: "leave"; outside the slot touches and paint do nothing
        mode(0x2081);
        recv_line(line, sizeof line);
        CHECK(!strcmp(line, "leave"), "leave: \"%s\"", line);
        md(pagebuf + T_COMP, ev);
        CHECK(!recv_line(line, sizeof line), "touch outside the slot sent \"%s\"", line);
        memset(&rp, 0, sizeof rp);
        CALL_PAINT(pagebuf, (void *)77);
        CHECK(rp.pixel == 0 && rp.full == 0, "paint outside the slot repainted");
        CHECK(sb.n == 0, "full height moved a page that was at the top");

        // Full height: something else at the holder (wrong vtable): left alone.
        b[0] = 0; b[1] = 110; b[2] = 1280; b[3] = 628;
        *(void **)pagebuf = (void *)&fails;
        mode(0x2090);
        CHECK(sb.n == 0, "moved an object that is not the placeholder page");
        mode(0x2081);
        *(void **)pagebuf = fake_page_vt;
        // Full height: the page's parent lists it under MPC's header band (drawn after it).
        kids[0] = pagebuf + T_COMP;
        kids[1] = fake_header;
        *(void **)(pagebuf + T_COMP + T_PARENT) = fake_parent;
        *(void ***)(fake_parent + T_CHILDREN) = kids;
        *(int32_t *)(fake_parent + 0x48) = 2;
        *(int32_t *)(fake_parent + T_USED) = 2;
        // the parent is the mode's wrapper (0,0 1280x738) in the whole screen (1280x800)
        *(void **)fake_parent = fake_wrap_vt;
        *(void **)(fake_parent + T_PARENT) = fake_gp;
        int32_t *pb = (int32_t *)(fake_parent + T_BOUNDS), *gb = (int32_t *)(fake_gp + T_BOUNDS);
        pb[0] = 0; pb[1] = 0; pb[2] = 1280; pb[3] = 738;
        gb[0] = 0; gb[1] = 0; gb[2] = 1280; gb[3] = 800;
        // Full height: MPC lays the page out under a header band; entering stretches it to the top.
        b[0] = 0; b[1] = 110; b[2] = 1280; b[3] = 628;
        drain();
        mode(0x2090);
        CHECK(sb.n == 2 && sb.comp == pagebuf + T_COMP && sb.x == 0 && sb.y == 0 && sb.w == 1280 && sb.h == 800 && pb[3] == 800,
              "enter: setBounds %d (%d %d %d %d), wrapper %d", sb.n, sb.x, sb.y, sb.w, sb.h, pb[3]);
        CHECK(kids[0] == fake_header && kids[1] == pagebuf + T_COMP, "enter: page not brought in front of the header");
        CALL_PAINT(pagebuf, (void *)77);
        CHECK(sb.n == 2, "paint stretched it again");
        recv_line(line, sizeof line);
        recv_line(line, sizeof line);
        CHECK(!strcmp(line, "size 1280 800"), "full height size: \"%s\"", line);
        // leaving puts it back where MPC had it, before the original handler runs
        mode(0x2081);
        CHECK(sb.n == 4 && sb.y == 110 && sb.h == 628 && pb[3] == 738, "leave: setBounds %d (%d %d), wrapper %d", sb.n, sb.y, sb.h, pb[3]);
        CHECK(kids[0] == pagebuf + T_COMP && kids[1] == fake_header, "leave: child order not put back");
        // MPC lays it out again while shown: paint leaves it (no setBounds while drawing); leaving then too
        mode(0x2090);
        CHECK(sb.n == 6, "re-enter: setBounds %d", sb.n);
        b[1] = 50;
        pb[2] = 1000;   // MPC laid the wrapper out again too
        CALL_PAINT(pagebuf, (void *)77);
        CHECK(sb.n == 6, "paint moved the page (setBounds %d)", sb.n);
        mode(0x2081);
        CHECK(sb.n == 6, "leave after MPC moved them: setBounds %d", sb.n);
        pb[2] = 1280; pb[3] = 738;
        // something else as the parent: only the page is stretched (to the strip)
        *(void **)fake_parent = (void *)&fails;
        b[0] = 0; b[1] = 110; b[2] = 1280; b[3] = 628;
        mode(0x2090);
        CHECK(sb.n == 7 && sb.h == 738 && pb[3] == 738, "foreign parent: setBounds %d h %d, parent %d", sb.n, sb.h, pb[3]);
        mode(0x2081);
        *(void **)fake_parent = fake_wrap_vt;
        b[0] = 0; b[1] = 0; b[2] = 1280; b[3] = 800;
        drain();
    }

    // Standing aside. (a) Another controller registered for the slot.
    CHECK(!omm_yield, "stood aside too early");
    ncalls = 0;
    CHECK(CALL_REG(&mgr, 0x2090, &modeA) == 0 && ncalls == 1 && calls[0].mode == &modeA,
          "someone else's 0x2090 registration not passed through");
    CHECK(omm_yield == 1, "did not stand aside after someone else registered the slot");
    ncalls = 0;
    CHECK(CALL_REG(&mgr, 0x2080, &modeB) == 0 && ncalls == 1 && calls[0].id == 0x2080,
          "aside: 0x2080 still aliased (%d calls)", ncalls);
    CHECK(!strcmp(name_of(0x2090), ""), "aside: slot still named \"%s\"", name_of(0x2090));
    mode(0x2090);
    CHECK(omm_load_page("/nonexistent") == 0, "x");
    {
        char path2[] = "/tmp/modeslot-page2-XXXXXX";
        int fd2 = mkstemp(path2);
        const char png2[] = "\x89PNG\r\n\x1a\n page";
        CHECK(fd2 >= 0 && write(fd2, png2, sizeof png2) == (ssize_t)sizeof png2, "temp page 2");
        close(fd2);
        omm_load_page(path2);
        unlink(path2);
    }
    CHECK(image(placeholder, sizeof placeholder) == placeholder, "aside: page still swapped");
    int iout2;
    int before_icon = last_icon.n;
    CHECK(CALL_ICON(0x2090, &iout2) == 0 && last_icon.n == before_icon + 1,
          "aside: icon still ours");
    {
        int order, settings;
        uint32_t ids[2] = {0x2091, 0x2083};
        struct omm_mode_vec v = {ids, ids + 2, ids + 2};
        CHECK(CALL_ORDER(&order, &v, &settings) == 0 && last_order.vec == &v &&
              last_order.n == 2, "aside: slot still added to the MENU list");
    }
    // (b) MPC (or a mod hooking further in) names the slot itself: that name wins and we stand aside.
    omm_yield = 0;
    CHECK(!strcmp(name_of(0x2090), "Plugin Manager"), "reset: \"%s\"", name_of(0x2090));
    fake_slot_name = "Other Mod";
    CHECK(!strcmp(name_of(0x2090), "Other Mod"), "someone else's name replaced: \"%s\"", name_of(0x2090));
    CHECK(omm_yield == 1, "did not stand aside after finding a name for the slot");
    fake_slot_name = NULL;
    CHECK(!strcmp(name_of(0x2090), ""), "aside: our name came back");

#if defined(__aarch64__)
    // 8. The launcher tile: a second slot (0x2092) with its own name, icon and picture; entering it runs its command.
    {
        omm_yield = 0;
        fake_slot_name = NULL;
        char tdir[] = "/tmp/modeslot-tile-XXXXXX";
        CHECK(mkdtemp(tdir) != NULL, "tile dir");
        char tp[600];
        snprintf(tp, sizeof tp, "%s/tile.conf", tdir);
        FILE *tf = fopen(tp, "w");
        fputs("# a launcher tile\nNAME=Engine\nRUN=echo started\n", tf);
        fclose(tf);
        const char tpng[] = "\x89PNG\r\n\x1a\n tile page", ticon[] = "\x89PNG\r\n\x1a\n tile icon";
        snprintf(tp, sizeof tp, "%s/page.png", tdir);
        tf = fopen(tp, "w"); fwrite(tpng, 1, sizeof tpng, tf); fclose(tf);
        snprintf(tp, sizeof tp, "%s/icon.png", tdir);
        tf = fopen(tp, "w"); fwrite(ticon, 1, sizeof ticon, tf); fclose(tf);
        CHECK(omm_tile2_config(tdir) == 1 && !strcmp(omm_tile2_run, "echo started"), "tile.conf not read (run \"%s\")", omm_tile2_run);
        omm_slot2_id = 0x2092; // what omm_install picks next to 0x2090
        omm_slot2_name = "Engine";

        CHECK(!strcmp(name_of(0x2092), "Engine"), "tile name: \"%s\"", name_of(0x2092));
        CHECK(!strcmp(name_of(0x2090), "Plugin Manager"), "tile: slot 1 name \"%s\"", name_of(0x2090));
        CHECK(!strcmp(name_of(0x2091), "Browser"), "tile: 0x2091 \"%s\"", name_of(0x2091));
        for (int i = 0; i < 1000; i++)
            if (strcmp(name_of(0x2090 + (i % 3)), i % 3 == 0 ? "Plugin Manager" : i % 3 == 1 ? "Browser" : "Engine")) { fails++; break; }

        ncalls = 0;
        CHECK(CALL_REG(&mgr, 0x2080, &modeB) == 0, "tile: registers clobbered (0x2080)");
        CHECK(ncalls == 3 && calls[0].id == 0x2080 && calls[1].id == 0x2090 && calls[2].id == 0x2092 &&
              calls[2].mgr == &mgr && calls[2].mode == &modeB, "tile 0x2080: %d calls, third id %#x", ncalls, calls[2].id);
        ncalls = 0;
        CHECK(CALL_REG(&mgr, 0x2084, &modeA) == 0 && ncalls == 1, "tile 0x2084: %d calls", ncalls);

        int order, settings;
        uint32_t ids[1] = {0x2091};
        struct omm_mode_vec v = {ids, ids + 1, ids + 1};
        CHECK(CALL_ORDER(&order, &v, &settings) == 0 && last_order.n == 3 && last_order.ids[1] == 0x2090 &&
              last_order.ids[2] == 0x2092, "tile order: n %d", last_order.n);
        uint32_t has1[2] = {0x2091, 0x2090};
        struct omm_mode_vec w = {has1, has1 + 2, has1 + 2};
        CHECK(CALL_ORDER(&order, &w, &settings) == 0 && last_order.n == 3 && last_order.ids[2] == 0x2092,
              "tile order with slot 1 listed: n %d", last_order.n);
        uint32_t both[2] = {0x2092, 0x2090};
        struct omm_mode_vec x = {both, both + 2, both + 2};
        CHECK(CALL_ORDER(&order, &x, &settings) == 0 && last_order.vec == &x, "tile order: both listed, not passed as is");

        int iout3;
        memset(&last_img, 0, sizeof last_img);
        CHECK(CALL_ICON(0x2092, &iout3) == 0 && last_img.data && !memcmp(last_img.data, ticon, sizeof ticon) &&
              last_img.x8 == &iout3, "tile icon not loaded (size %u)", last_img.size);

        omm_tile2_runner = tile_runner;
        mode(0x2080);
        CHECK(image(placeholder, sizeof placeholder) == placeholder, "tile: Main Mode picture swapped");
        mode(0x2092);
        CHECK(runs == 1 && !strcmp(ran, "echo started"), "tile: entering did not run (%d, \"%s\")", runs, ran);
        const void *got = image(placeholder, sizeof placeholder);
        CHECK(got && !memcmp(got, tpng, sizeof tpng), "tile: page.png not swapped in");
        CHECK(image(other, sizeof placeholder) == other, "tile: another picture swapped");
        mode(0x3085); // a popup
        mode(0x2092);
        CHECK(runs == 1, "tile: ran again without leaving (%d)", runs);
        mode(0x2080);
        mode(0x2092);
        CHECK(runs == 2, "tile: entering again did not run (%d)", runs);
        mode(0x2080);

        // Someone else registers the tile's slot: it stands aside, slot 1 does not.
        CHECK(CALL_REG(&mgr, 0x2092, &modeA) == 0 && omm_yield2 == 1 && omm_yield == 0, "tile: did not stand aside alone");
        CHECK(!strcmp(name_of(0x2092), ""), "tile aside: name \"%s\"", name_of(0x2092));
        CHECK(!strcmp(name_of(0x2090), "Plugin Manager"), "tile aside: slot 1 lost its name");
        ncalls = 0;
        CHECK(CALL_REG(&mgr, 0x2080, &modeB) == 0 && ncalls == 2, "tile aside: still registered (%d)", ncalls);
        mode(0x2092);
        CHECK(runs == 2, "tile aside: still runs");
        mode(0x2080);
        omm_slot2_id = 0;
    }
#endif

    printf(fails ? "%d FAILED\n" : "all modeslot tests passed\n", fails);
    return fails != 0;
}
