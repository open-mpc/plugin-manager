/* page.c: the Manager on MPC's mode-slot page (the modeslot addin, mpc-vst-plugins tools/mpc_patch/modeslot).
 *
 * The addin turns an unused MPC mode into a page an outside process draws: it shows the PNG this writes to
 * /run/openmpc-page/frame.png and sends the page's touches as datagrams to /run/openmpc-page/input.sock
 * ("enter", "size W H", "down X Y", "drag X Y", "up X Y", "leave"). This draws the Manager there the way MPC draws
 * its skin: the same components, images and live text, read from the page table vst/gen_page.py compiles from the
 * built skin (Plugin Skins/openmpc_page.txt), and a tap on a button sets its parameter as MPC would (a momentary one
 * to "1", an option to its index). First version: the PLUGINS tab only.
 *
 * The engine is reached through two callbacks (page_engine_t), so the host test can drive it without the Manager.
 * Images are the skin's PNGs, decoded on first use (stb_image); text is Titillium Web (stb_truetype), as on MPC. */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_WRITE
#include "vendor/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "vendor/stb_truetype.h"
#pragma GCC diagnostic pop

typedef struct {
    void *inst;
    int (*get)(void *inst, const char *key, char *buf, int n);
    void (*set)(void *inst, const char *key, const char *val);
} page_engine_t;

enum { PK_MOMENTARY = 'm', PK_OPTIONS = 'o', PK_TEXT = 't', PK_NUMBER = 'n' };
enum { IT_IMAGE, IT_BUTTON, IT_LABEL };

typedef struct {
    int idx, kind, nopts;
    char key[48];
    char **opts;
    /* per frame */
    int opt;
    char text[160];
} pparam_t;

typedef struct {
    int type, x, y, w, h;
    int img, img_on;           /* image table indexes, -1 none */
    int bid, ngroup;           /* buttons */
    float height;              /* labels */
    uint32_t colour;
    int just, bold;
} pitem_t;

typedef struct {
    int x, y, w, h, param, tap;
    int c0, nc, i0, ni;        /* conditions, items */
    int row;                   /* the plugin-card row it belongs to (r<n>_ / card<n>_ parameters), -1 none */
    int band;                  /* on a taller page: BAND_STAY, BAND_FOOT (moves down), BAND_FULL (stretches) */
    int tab;                   /* the skin tab it is on (PLUGINS, LOCATIONS, ...) */
} pcomp_t;
enum { BAND_STAY, BAND_FOOT, BAND_FULL, BAND_PANEL };   /* PANEL: a big panel ending at the footer (the details): grows */

typedef struct {
    char *name;
    int w, h;
    uint8_t *rgba;             /* NULL until drawn */
    unsigned used;             /* the frame it was last drawn in */
} pimage_t;

static struct {
    int ok, w, h;
    char dir[512];
    pparam_t *params; int nparams;
    int *pmap; int pmap_n;     /* parameter index -> params[] slot, -1 none */
    pcomp_t *comps; int ncomps;
    pitem_t *items; int nitems;
    int (*conds)[2]; int nconds;
    pimage_t *imgs; int nimgs;
    size_t decoded;
    unsigned frame;
    int nrows;                 /* card rows in the skin (5) */
    int pitch;                 /* the distance between card rows (84) */
    int nbuilt;                /* card rows the page holds: the skin's, then copies of its last row (r6_..., card6_...) */
    int rows_on;               /* card rows shown (and asked of the engine as "rows"), 0 before the first frame */
    int extra;                 /* rows_on - nrows */
    int foot;                  /* the skin's y where the footer starts (below the last card) */
    int tfoot[8];              /* the same for each tab (tabs with no cards: PLUGINS' value) */
    char tabname[8][24];       /* the skin's tabs (T lines) */
    int ntabs, tab;            /* how many, and the one shown */
    int tabbar;                /* the height of the page's own tab bar along the bottom (0: none, the page too short) */
    int dh;                    /* the page drawn this much taller than the skin (the mode slot at full height) */
    stbtt_fontinfo font[2];    /* SemiBold, Bold */
    unsigned char *fontdata[2];
    page_engine_t eng;
} P;

#define DECODED_MAX ((size_t)96 << 20)   /* decoded images kept: the visible ones always, others while under this */

static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) abort(); return q; }

static int image_index(const char *name) {
    if (!strcmp(name, "-")) return -1;
    for (int i = P.nimgs - 1; i >= 0; i--)
        if (!strcmp(P.imgs[i].name, name)) return i;
    P.imgs = xrealloc(P.imgs, (size_t)(P.nimgs + 1) * sizeof *P.imgs);
    P.imgs[P.nimgs] = (pimage_t){strdup(name), 0, 0, NULL, 0};
    return P.nimgs++;
}

static int load_font(int i, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t)n);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return -1; }
    fclose(f);
    if (!stbtt_InitFont(&P.font[i], d, stbtt_GetFontOffsetForIndex(d, 0))) { free(d); return -1; }
    P.fontdata[i] = d;
    return 0;
}

static pparam_t *param_of(int idx) { return idx >= 0 && idx < P.pmap_n && P.pmap[idx] >= 0 ? &P.params[P.pmap[idx]] : NULL; }

/* The card row (0-based) a parameter key belongs to: r<n>_..., card<n>_..., else -1. */
static int key_row(int idx) {
    pparam_t *p = param_of(idx);
    int n = 0;
    if (!p) return -1;
    if (sscanf(p->key, "r%d_", &n) == 1 || sscanf(p->key, "card%d_", &n) == 1) return n >= 1 && n <= 16 ? n - 1 : -1;
    return -1;
}

/* For a page taller than the skin (the mode slot at full height, like MPC's Developer mode): the plugin cards
 * spread out over the extra height, the footer (everything below the cards) moves to the bottom, full-page
 * pieces (the background, a menu's tap-anywhere-to-close) stretch, and the rest (header, tabs, menus and
 * other overlays) stays where the skin has it. */
static void classify(void) {
    int foot = 0;
    P.nrows = 0;
    memset(P.tfoot, 0, sizeof P.tfoot);
    for (int ci = 0; ci < P.ncomps; ci++) {
        pcomp_t *c = &P.comps[ci];
        c->row = key_row(c->param);
        for (int k = 0; k < c->nc && c->row < 0; k++) c->row = key_row(P.conds[c->c0 + k][0]);
        if (c->row >= 0) {
            if (c->row + 1 > P.nrows) P.nrows = c->row + 1;
            if (c->y + c->h > foot) foot = c->y + c->h;
            if (c->y + c->h > P.tfoot[c->tab & 7]) P.tfoot[c->tab & 7] = c->y + c->h;
        }
    }
    for (int t = 0; t < 8; t++)
        if (!P.tfoot[t]) P.tfoot[t] = foot;
    int top0 = 1 << 30, top1 = 1 << 30;
    for (int ci = 0; ci < P.ncomps; ci++) {
        pcomp_t *c = &P.comps[ci];
        int tf = P.tfoot[c->tab & 7];
        c->band = c->h >= P.h ? BAND_FULL : c->row < 0 && tf > 0 && c->y >= tf ? BAND_FOOT : BAND_STAY;
        if (c->band == BAND_STAY && c->row < 0 && c->nc > 0 && c->w >= 1000 && c->h >= 300 && tf > 0 &&
            c->y + c->h >= tf - 40 && c->y + c->h <= tf + 10)
            c->band = BAND_PANEL;
        if (c->row == 0 && c->y < top0) top0 = c->y;
        if (c->row == 1 && c->y < top1) top1 = c->y;
    }
    P.foot = foot;
    P.pitch = P.nrows >= 2 && top1 > top0 && top1 < (1 << 30) ? top1 - top0 : 0;
    P.nbuilt = P.nrows;
}

/* The parameter of card row dst that matches parameter idx of row src (r<src>_x -> r<dst>_x, card<src>_x -> card<dst>_x),
 * added to the page the first time; idx itself if it is not one of row src's. */
static int row_param(int idx, int src, int dst) {
    if (key_row(idx) != src) return idx;
    pparam_t p = *param_of(idx);
    const char *rest = strchr(p.key, '_');
    char key[48];
    snprintf(key, sizeof key, "%s%d%s", p.key[0] == 'r' ? "r" : "card", dst + 1, rest ? rest : "");
    for (int i = 0; i < P.nparams; i++)
        if (!strcmp(P.params[i].key, key)) return P.params[i].idx;
    snprintf(p.key, sizeof p.key, "%s", key);
    p.idx = P.pmap_n;
    p.text[0] = 0;
    P.params = xrealloc(P.params, (size_t)(P.nparams + 1) * sizeof *P.params);
    P.params[P.nparams++] = p;
    P.pmap = xrealloc(P.pmap, (size_t)(P.pmap_n + 1) * sizeof *P.pmap);
    P.pmap[P.pmap_n++] = P.nparams - 1;
    return p.idx;
}

/* One more card row: a copy of the skin's last one, a pitch further down, on the next row's parameters. Each copy goes
 * right after the component it copies, so it stacks as that one does (under the details, the menus, the keyboard). */
static void add_row(void) {
    int src = P.nrows - 1, dst = P.nbuilt, n0 = P.ncomps, n = 0;
    for (int ci = 0; ci < n0; ci++) n += P.comps[ci].row == src;
    pcomp_t *out = xrealloc(NULL, (size_t)(n0 + n) * sizeof *out);
    int k = 0;
    for (int ci = 0; ci < n0; ci++) {
        pcomp_t c = P.comps[ci];
        out[k++] = c;
        if (c.row != src) continue;
        pcomp_t d = c;
        d.y += (dst - src) * P.pitch;
        d.row = dst;
        d.param = row_param(d.param, src, dst);
        d.c0 = P.nconds;
        for (int j = 0; j < c.nc; j++) {
            int cp = P.conds[c.c0 + j][0], cv = P.conds[c.c0 + j][1];
            P.conds = xrealloc(P.conds, (size_t)(P.nconds + 1) * sizeof *P.conds);
            P.conds[P.nconds][0] = row_param(cp, src, dst);
            P.conds[P.nconds][1] = cv;
            P.nconds++;
        }
        d.i0 = P.nitems;
        P.items = xrealloc(P.items, (size_t)(P.nitems + c.ni) * sizeof *P.items);
        memcpy(P.items + P.nitems, P.items + c.i0, (size_t)c.ni * sizeof *P.items);
        P.nitems += c.ni;
        out[k++] = d;
    }
    free(P.comps);
    P.comps = out;
    P.ncomps = k;
    P.nbuilt++;
}

/* Card rows for a page dh taller than the skin: one more per pitch of extra height (at most 9), built on first use and
 * asked of the engine ("rows"), which then pages by that many. */
static void set_rows(int dh) {
    int extra = P.pitch > 0 && dh > 0 ? dh / P.pitch : 0;
    if (P.nrows + extra > 9) extra = 9 - P.nrows;
    if (extra < 0) extra = 0;
    while (P.nbuilt < P.nrows + extra) add_row();
    P.extra = extra;
    if (P.rows_on != P.nrows + extra) {
        char v[8];
        P.rows_on = P.nrows + extra;
        snprintf(v, sizeof v, "%d", P.rows_on);
        P.eng.set(P.eng.inst, "rows", v);
    }
}

/* Where a component is drawn on the current page: its y shift, and its height. */
static int comp_dy(const pcomp_t *c) {
    if (P.dh <= 0) return 0;
    int rows = P.nrows + P.extra, spread = P.dh - P.extra * P.pitch;   /* what the added rows leave, shared out */
    if (c->row >= 0 && rows > 0) return spread * (2 * c->row + 1) / (2 * rows);
    return c->band == BAND_FOOT ? P.dh : 0;
}
static int stretch(const pcomp_t *c, int h) {   /* a component's height on the page (for taps) */
    return (c->band == BAND_FULL && h >= P.h) || c->band == BAND_PANEL ? h + (P.dh > 0 ? P.dh : 0) : h;
}
static void blit(uint8_t *fb, int W, int H, const pimage_t *im, int x, int y, int w, int h);
/* A full-page picture (the background, with the header drawn in) on a taller page: not stretched but cut at the
 * footer; the top part where it was, the footer part moved down, the gap filled with the row above the cut. */
/* A picture drawn taller by grow px without stretching: rows above cut where they were, the rest moved down by grow,
 * the gap filled with the row above the cut (a flat stretch of background in these skins). */
static void blit_cut(uint8_t *fb, int W, int H, const pimage_t *im, int x, int y, int w, int h, int cut, int grow) {
    if (!im || grow <= 0 || cut <= 0 || cut >= h || im->h != h) { blit(fb, W, H, im, x, y, w, h); return; }
    pimage_t top = *im, fill = *im, bot = *im;
    top.h = cut;
    fill.h = 1;
    fill.rgba = im->rgba + (size_t)(cut - 1) * (size_t)im->w * 4;
    bot.h = im->h - cut;
    bot.rgba = im->rgba + (size_t)cut * (size_t)im->w * 4;
    blit(fb, W, H, &top, x, y, w, top.h);
    blit(fb, W, H, &fill, x, y + cut, w, grow);
    blit(fb, W, H, &bot, x, y + cut + grow, w, bot.h);
}

/* A full-page picture (the background, with the header drawn in) on a taller page: cut at the footer. */
static void blit_page(uint8_t *fb, int W, int H, const pimage_t *im, int x, int y, int w, int h) {
    blit_cut(fb, W, H, im, x, y, w, h, P.tfoot[P.tab & 7], P.dh);
}

/* Where a panel picture can grow: the lowest row that repeats the one above it (inside the bottom padding), so what is
 * drawn in the panel keeps its place; the middle if there is none. */
static int flat_row(const pimage_t *im) {
    if (!im || !im->rgba || im->h < 4) return im ? im->h / 2 : 0;
    size_t stride = (size_t)im->w * 4;
    for (int r = im->h - 2; r > im->h / 2; r--)   /* three alike: a straight run, not a rounded corner */
        if (!memcmp(im->rgba + (size_t)r * stride, im->rgba + (size_t)(r - 1) * stride, stride) &&
            !memcmp(im->rgba + (size_t)r * stride, im->rgba + (size_t)(r - 2) * stride, stride)) return r;
    return im->h / 2;
}

/* A component's picture: the page background and big panels (BAND_PANEL: the details) grow with the page. */
static void blit_item(uint8_t *fb, int W, int H, const pcomp_t *c, const pimage_t *im, int x, int y, int w, int h) {
    if (c->band == BAND_FULL && h >= P.h) blit_page(fb, W, H, im, x, y, w, h);
    else if (c->band == BAND_PANEL && h >= c->h - 4) blit_cut(fb, W, H, im, x, y, w, h, flat_row(im), P.dh);
    else blit(fb, W, H, im, x, y, w, h);
}

/* Load the page table (vst/gen_page.py's openmpc_page.txt), the skin images from img_dir and the fonts from font_dir.
 * 0 on success. */
static int page_load(const char *table, const char *img_dir, const char *font_dir, page_engine_t eng) {
    char path[700];
    snprintf(P.dir, sizeof P.dir, "%s", img_dir);
    P.eng = eng;
    snprintf(path, sizeof path, "%s/TitilliumWeb-SemiBold.ttf", font_dir);
    if (load_font(0, path)) return -1;
    snprintf(path, sizeof path, "%s/TitilliumWeb-Bold.ttf", font_dir);
    if (load_font(1, path)) return -1;
    FILE *f = fopen(table, "r");
    if (!f) return -1;
    char *line = NULL;   /* lines of any length: an option list (r1_shot: every thumbnail) runs to thousands of characters */
    size_t cap = 0;
    pcomp_t *cur = NULL;
    int tab = 0;   /* the tab being read (T lines) */
    while (getline(&line, &cap, f) >= 0) {
        line[strcspn(line, "\r\n")] = 0;
        char a[700], b[700];
        if (line[0] == 'S') sscanf(line, "S %d %d", &P.w, &P.h);
        else if (line[0] == 'T') {   /* a tab: the components up to the next T are its own */
            char name[24] = "";
            if (sscanf(line, "T %d %23s", &tab, name) >= 1 && tab >= 0 && tab < 8) {
                snprintf(P.tabname[tab], sizeof P.tabname[tab], "%s", name);
                if (tab + 1 > P.ntabs) P.ntabs = tab + 1;
            } else tab = 0;
            cur = NULL;
        }
        else if (line[0] == 'P') {
            pparam_t p = {0};
            int at = 0;
            if (sscanf(line, "P %d %c %47s %n", &p.idx, (char *)&p.kind, p.key, &at) < 3) continue;
            p.kind = (unsigned char)p.kind;
            char *opts = at > 0 ? line + at : NULL;   /* the rest of the line, however long */
            if (opts && opts[0]) {
                for (char *s = strtok(opts, "|"); s; s = strtok(NULL, "|")) {
                    p.opts = xrealloc(p.opts, (size_t)(p.nopts + 1) * sizeof *p.opts);
                    p.opts[p.nopts++] = strdup(s);
                }
            }
            P.params = xrealloc(P.params, (size_t)(P.nparams + 1) * sizeof *P.params);
            P.params[P.nparams++] = p;
            if (p.idx >= P.pmap_n) {
                int n = p.idx + 1;
                P.pmap = xrealloc(P.pmap, (size_t)n * sizeof *P.pmap);
                for (int i = P.pmap_n; i < n; i++) P.pmap[i] = -1;
                P.pmap_n = n;
            }
            P.pmap[p.idx] = P.nparams - 1;
        } else if (line[0] == 'C') {
            P.comps = xrealloc(P.comps, (size_t)(P.ncomps + 1) * sizeof *P.comps);
            cur = &P.comps[P.ncomps++];
            *cur = (pcomp_t){0};
            sscanf(line, "C %d %d %d %d %d %d", &cur->x, &cur->y, &cur->w, &cur->h, &cur->param, &cur->tap);
            cur->tab = tab;
            cur->c0 = P.nconds;
            cur->i0 = P.nitems;
        } else if (line[0] == 'W' && cur) {
            P.conds = xrealloc(P.conds, (size_t)(P.nconds + 1) * sizeof *P.conds);
            sscanf(line, "W %d %d", &P.conds[P.nconds][0], &P.conds[P.nconds][1]);
            P.nconds++;
            cur->nc++;
        } else if (line[0] == 'I' || line[0] == 'B' || line[0] == 'L' || line[0] == 'F') {
            pitem_t it = {0};
            if (line[0] == 'F') { cur = NULL; line[0] = 'I'; }   /* a free-standing image */
            it.img = it.img_on = -1;
            if (line[0] == 'I') {
                it.type = IT_IMAGE;
                if (sscanf(line, "I %d %d %d %d %699[^\n]", &it.x, &it.y, &it.w, &it.h, a) != 5) continue;
                it.img = image_index(a);
            } else if (line[0] == 'B') {
                it.type = IT_BUTTON;
                if (sscanf(line, "B %d %d %d %d %d %d %699s %699s", &it.x, &it.y, &it.w, &it.h, &it.bid, &it.ngroup, a, b) != 8) continue;
                it.img = image_index(a);
                it.img_on = image_index(b);
            } else {
                char just[16], style[32];
                unsigned col = 0xffffffffu;
                it.type = IT_LABEL;
                if (sscanf(line, "L %d %d %d %d %f %x %15s %31s", &it.x, &it.y, &it.w, &it.h, &it.height, &col, just, style) != 8) continue;
                it.colour = col;
                it.just = just[0] == 'l' ? 0 : just[0] == 'r' ? 2 : 1;
                it.bold = !strcmp(style, "Bold");
            }
            if (!cur) {   /* a free-standing image: a component of its own with no conditions */
                P.comps = xrealloc(P.comps, (size_t)(P.ncomps + 1) * sizeof *P.comps);
                P.comps[P.ncomps] = (pcomp_t){it.x, it.y, it.w, it.h, -1, 0, P.nconds, 0, P.nitems, 1, -1, BAND_STAY, tab};
                P.ncomps++;
                it.x = it.y = 0;
            } else cur->ni++;
            P.items = xrealloc(P.items, (size_t)(P.nitems + 1) * sizeof *P.items);
            P.items[P.nitems++] = it;
        }
    }
    free(line);
    fclose(f);
    P.ok = P.w > 0 && P.h > 0 && P.ncomps > 0;
    if (P.ok) classify();
    return P.ok ? 0 : -1;
}

/* Read every parameter the page uses: its option index and its text, as the wrapper would show them. */
static uint64_t read_state(void) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < P.nparams; i++) {
        pparam_t *p = &P.params[i];
        char buf[160] = "";
        if (p->kind != PK_MOMENTARY && P.eng.get(P.eng.inst, p->key, buf, sizeof buf) <= 0) buf[0] = 0;
        p->opt = 0;
        if (p->kind == PK_OPTIONS) {
            int k = -1;
            for (int j = 0; j < p->nopts; j++)
                if (!strcasecmp(buf, p->opts[j])) { k = j; break; }
            if (k < 0) {
                char *end = NULL;
                long v = strtol(buf, &end, 10);
                if (end && end != buf && !*end) k = (int)v;
            }
            p->opt = k < 0 ? 0 : k >= p->nopts ? p->nopts - 1 : k;
            snprintf(p->text, sizeof p->text, "%s", p->nopts ? p->opts[p->opt] : "");
        } else snprintf(p->text, sizeof p->text, "%s", buf);
        for (const char *s = p->text; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ull;
        h = (h ^ (uint64_t)p->opt ^ 0xff) * 1099511628211ull;
    }
    return h;
}

static int comp_shown(const pcomp_t *c) {
    if (c->tab != P.tab) return 0;
    if (c->row >= P.nrows + P.extra) return 0;   /* an added card row the page is too short for */
    for (int k = 0; k < c->nc; k++) {
        pparam_t *p = param_of(P.conds[c->c0 + k][0]);
        if ((p ? p->opt : 0) != P.conds[c->c0 + k][1]) return 0;
    }
    return 1;
}

static pimage_t *image(int i) {
    if (i < 0 || i >= P.nimgs) return NULL;
    pimage_t *im = &P.imgs[i];
    if (!im->rgba) {
        char path[1200];
        int n;
        snprintf(path, sizeof path, "%s/%s", P.dir, im->name);
        im->rgba = stbi_load(path, &im->w, &im->h, &n, 4);
        if (!im->rgba) return NULL;
        P.decoded += (size_t)im->w * (size_t)im->h * 4;
    }
    im->used = P.frame;
    return im;
}

static void evict(void) {   /* after a frame: drop images that were not drawn while over the budget */
    for (int i = 0; i < P.nimgs && P.decoded > DECODED_MAX; i++) {
        pimage_t *im = &P.imgs[i];
        if (im->rgba && im->used != P.frame) {
            P.decoded -= (size_t)im->w * (size_t)im->h * 4;
            stbi_image_free(im->rgba);
            im->rgba = NULL;
        }
    }
}

/* Draw an image into the frame at x,y scaled to w x h (nearest; skin images are drawn at their own size). */
static void blit(uint8_t *fb, int W, int H, const pimage_t *im, int x, int y, int w, int h) {
    if (!im || w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int fy = y + j;
        if (fy < 0 || fy >= H) continue;
        const uint8_t *row = im->rgba + (size_t)((j * im->h) / h) * (size_t)im->w * 4;
        uint8_t *d = fb + ((size_t)fy * (size_t)W + (size_t)(x < 0 ? 0 : x)) * 3;
        for (int i = x < 0 ? -x : 0; i < w; i++) {
            int fx = x + i;
            if (fx >= W) break;
            const uint8_t *s = row + (size_t)((i * im->w) / w) * 4;
            unsigned a = s[3];
            if (a == 255) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
            else if (a) {
                d[0] = (uint8_t)((s[0] * a + d[0] * (255 - a)) / 255);
                d[1] = (uint8_t)((s[1] * a + d[1] * (255 - a)) / 255);
                d[2] = (uint8_t)((s[2] * a + d[2] * (255 - a)) / 255);
            }
            d += 3;
        }
    }
}

static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    if (c >= 0xc0) {
        int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        c &= 0x3f >> n;
        while (n-- && (*p & 0xc0) == 0x80) c = (c << 6) | (*p++ & 0x3f);
    }
    *s = (const char *)p;
    return c;
}

/* MPC's skin font sizes are line heights; stb_truetype scales by the em box, so match the height from ascent to descent. */
static float font_scale(const stbtt_fontinfo *f, float height) {
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    return height / (float)(asc - desc);
}

static float text_width(const stbtt_fontinfo *f, float sc, const char *text, const char *stop) {
    float width = 0;
    for (const char *s = text; *s && (!stop || s < stop);) {
        uint32_t c = utf8_next(&s);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(f, (int)c, &adv, &lsb);
        width += adv * sc;
    }
    return width;
}

/* A label's text as a JUCE Label draws it: squeezed sideways down to 70 % if it is too wide, then cut short with an
 * ellipsis; never past its box. */
static void draw_text(uint8_t *fb, int W, int H, const pitem_t *it, int ox, int oy, const char *text) {
    const stbtt_fontinfo *f = &P.font[it->bold ? 1 : 0];
    float sc = font_scale(f, it->height);   /* a JUCE font height: ascent to descent (Titillium: 1.52 x the em size) */
    int asc, desc, gap;
    stbtt_GetFontVMetrics(f, &asc, &desc, &gap);
    float box = (float)it->w, width = text_width(f, sc, text, NULL), hs = 1;
    char cut[200];
    if (width > box && box > 0) {
        hs = box / width;
        if (hs < 0.7f) {   /* still too wide at 70 %: as much as fits, then "..." */
            hs = 0.7f;
            float ell = text_width(f, sc * hs, "...", NULL);
            const char *s = text, *last = text;
            while (*s) {
                utf8_next(&s);
                if (text_width(f, sc * hs, text, s) + ell > box) break;
                last = s;
            }
            size_t n = (size_t)(last - text);
            if (n > sizeof cut - 4) n = sizeof cut - 4;
            while (n > 0 && text[n - 1] == ' ') n--;
            memcpy(cut, text, n);
            memcpy(cut + n, "...", 4);
            text = cut;
        }
        width = text_width(f, sc * hs, text, NULL);
    }
    float x = (float)(ox + it->x);
    if (it->just == 2) x += it->w - width;
    else if (it->just == 1) x += (it->w - width) / 2;
    float base = oy + it->y + (it->h + (asc + desc) * sc) / 2;
    int x0 = ox + it->x, x1 = ox + it->x + it->w;
    uint8_t cr = (uint8_t)(it->colour >> 16), cg = (uint8_t)(it->colour >> 8), cb = (uint8_t)it->colour;
    for (const char *s = text; *s;) {
        uint32_t c = utf8_next(&s);
        int adv, lsb, gw, gh, gx, gy;
        stbtt_GetCodepointHMetrics(f, (int)c, &adv, &lsb);
        unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(f, sc * hs, sc, x - (int)x, 0, (int)c, &gw, &gh, &gx, &gy);
        int px0 = (int)x + gx, py0 = (int)base + gy;
        for (int j = 0; bm && j < gh; j++) {
            int fy = py0 + j;
            if (fy < 0 || fy >= H || fy < oy + it->y || fy >= oy + it->y + it->h + 4) continue;
            for (int i = 0; i < gw; i++) {
                int fx = px0 + i;
                unsigned a = bm[j * gw + i];
                if (!a || fx < 0 || fx >= W || fx < x0 - 1 || fx > x1) continue;
                uint8_t *d = fb + ((size_t)fy * (size_t)W + (size_t)fx) * 3;
                d[0] = (uint8_t)((cr * a + d[0] * (255 - a)) / 255);
                d[1] = (uint8_t)((cg * a + d[1] * (255 - a)) / 255);
                d[2] = (uint8_t)((cb * a + d[2] * (255 - a)) / 255);
            }
        }
        if (bm) stbtt_FreeBitmap(bm, NULL);
        x += adv * sc * hs;
    }
}

/* Draw the page into fb (W x H, RGB); the page is drawn at its own size from the top left, the rest left black. */
#define TABBAR_H 62    /* MPC's tab strip: 62 px along the bottom of the screen */
#define TAB_W 213      /* six slots across 1280 */

static void fill_rect(uint8_t *fb, int W, int H, int x0, int y0, int x1, int y1, uint32_t rgb) {
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
        for (int x = x0 < 0 ? 0 : x0; x < x1 && x < W; x++) {
            uint8_t *d = fb + ((size_t)y * (size_t)W + (size_t)x) * 3;
            d[0] = (uint8_t)(rgb >> 16); d[1] = (uint8_t)(rgb >> 8); d[2] = (uint8_t)rgb;
        }
}

/* The skin's tabs along the bottom, as MPC draws a plugin's: a bar over each (lit over the one shown), its name below. */
static void draw_tabbar(uint8_t *fb, int W, int H, int y0) {
    fill_rect(fb, W, H, 0, y0, W, H, 0x000000);
    for (int t = 0; t < P.ntabs && t < 6; t++) {
        int cx = TAB_W * t + TAB_W / 2;
        fill_rect(fb, W, H, cx - 90, y0 + 1, cx + 90, y0 + 7, t == P.tab ? 0xe8304a : 0x2c2c2c);
        pitem_t it = {0};
        it.type = IT_LABEL;
        it.x = TAB_W * t; it.y = 8; it.w = TAB_W; it.h = TABBAR_H - 8;
        it.height = 29; it.colour = 0xffffffff; it.just = 1;   /* MPC's tab labels: about 15 px capitals */
        char name[24];
        snprintf(name, sizeof name, "%s", P.tabname[t]);
        for (char *q = name; *q; q++) if (*q == '_') *q = ' ';
        draw_text(fb, W, H, &it, 0, y0, name);
    }
}

/* The page is H tall: the skin's tab (stretched as above) and, if there is room, the tab bar under it. */
static int content_h(int H) {
    P.tabbar = P.ntabs > 1 && H - P.h >= TABBAR_H ? TABBAR_H : 0;
    return H - P.tabbar;
}

static void page_render(uint8_t *fb, int W, int H) {
    memset(fb, 0, (size_t)W * (size_t)H * 3);
    if (!P.ok) return;
    P.frame++;
    int Hc = content_h(H);
    P.dh = Hc - P.h;
    set_rows(P.dh);
    read_state();
    for (int ci = 0; ci < P.ncomps; ci++) {
        const pcomp_t *c = &P.comps[ci];
        if (!comp_shown(c)) continue;
        pparam_t *p = param_of(c->param);
        int cy = c->y + comp_dy(c);
        for (int k = 0; k < c->ni; k++) {
            const pitem_t *it = &P.items[c->i0 + k];
            if (it->type == IT_IMAGE) blit_item(fb, W, H, c, image(it->img), c->x + it->x, cy + it->y, it->w, it->h);
            else if (it->type == IT_BUTTON) {
                int on = it->ngroup > 1 && p && p->kind == PK_OPTIONS && p->opt == it->bid;
                int i = on && it->img_on >= 0 ? it->img_on : it->img;
                blit_item(fb, W, H, c, image(i), c->x + it->x, cy + it->y, it->w, it->h);
            } else if (it->type == IT_LABEL && p && p->text[0])
                draw_text(fb, W, Hc, it, c->x, cy, p->text);
        }
    }
    if (P.tabbar) draw_tabbar(fb, W, H, Hc);
    evict();
}

/* A tap at x,y (page pixels): the topmost shown button there sets its parameter as MPC would. 1 if one did. */
static int page_tap(int x, int y) {
    if (!P.ok) return 0;
    if (P.tabbar && y >= P.h + P.dh) {   /* the tab bar */
        int t = x / TAB_W;
        if (t < 0 || t >= P.ntabs || t >= 6 || t == P.tab) return 0;
        P.tab = t;
        return 1;
    }
    read_state();
    for (int ci = P.ncomps - 1; ci >= 0; ci--) {
        const pcomp_t *c = &P.comps[ci];
        int cy = c->y + comp_dy(c);
        if (!c->tap || c->param < 0 || x < c->x || y < cy || x >= c->x + c->w || y >= cy + stretch(c, c->h) || !comp_shown(c)) continue;
        pparam_t *p = param_of(c->param);
        if (!p) return 0;
        char v[16];
        if (p->kind == PK_MOMENTARY) {
            P.eng.set(P.eng.inst, p->key, "1");
            P.eng.set(P.eng.inst, p->key, "0");
        } else if (p->kind == PK_OPTIONS && p->nopts > 0) {
            int bid = -1;
            for (int k = 0; k < c->ni; k++)
                if (P.items[c->i0 + k].type == IT_BUTTON && P.items[c->i0 + k].ngroup > 1) bid = P.items[c->i0 + k].bid;
            int o = bid >= 0 ? bid : (p->opt + 1) % p->nopts;   /* a segment picks its option; a toggle switches */
            snprintf(v, sizeof v, "%d", o);
            P.eng.set(P.eng.inst, p->key, v);
        } else return 0;
        return 1;
    }
    return 0;
}

/* ---- the frame file: an uncompressed PNG (stored deflate blocks), written whole and then renamed ---- */
static uint32_t crc_tab[256];
static uint32_t crc32_(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_tab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t k = i;
            for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
            crc_tab[i] = k;
        }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    be32(h, crc32_(crc32_(0, (const uint8_t *)type, 4), d, n));
    fwrite(h, 1, 4, f);
}
static int page_write_png(const char *path, const uint8_t *fb, int W, int H) {
    size_t row = 1 + (size_t)W * 3, raw_n = (size_t)H * row, blocks = (raw_n + 65534) / 65535;
    uint8_t *z = malloc(2 + raw_n + blocks * 5 + 4), *q = z;
    if (!z) return -1;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    size_t off = 0;
    uint8_t *raw = malloc(raw_n);
    if (!raw) { free(z); return -1; }
    for (int y = 0; y < H; y++) { raw[(size_t)y * row] = 0; memcpy(raw + (size_t)y * row + 1, fb + (size_t)y * (size_t)W * 3, (size_t)W * 3); }
    for (; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *q++ = off + n == raw_n;
        *q++ = (uint8_t)n; *q++ = (uint8_t)(n >> 8); *q++ = (uint8_t)~n; *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n;) {
            size_t k = n - i < 4096 ? n - i : 4096;
            for (size_t j = 0; j < k; j++) { a += raw[off + i + j]; b += a; }
            a %= 65521; b %= 65521;
            i += k;
        }
        q += n;
    }
    be32(q, (b << 16) | a);
    q += 4;
    free(raw);
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(z); return -1; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)(q - z));
    png_chunk(f, "IEND", NULL, 0);
    free(z);
    if (fclose(f)) return -1;
    return rename(tmp, path);
}

/* ---- the page thread: socket events in, frames out ---- */
static volatile int page_quit;

__attribute__((unused)) static void *page_thread(void *arg) {
    (void)arg;
    const char *dir = getenv("OMM_PAGE_DIR");
    if (!dir || !*dir) dir = "/run/openmpc-page";
    char sock_path[108], frame[600];
    snprintf(sock_path, sizeof sock_path, "%s/input.sock", dir);
    snprintf(frame, sizeof frame, "%s/frame.png", dir);
    mkdir(dir, 0755);
    int s = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s", sock_path);
    unlink(sock_path);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) return NULL;
    int W = P.w, H = P.h, shown = 0, dirty = 1;
    uint8_t *fb = malloc((size_t)1280 * 800 * 3);
    if (!fb) return NULL;
    uint64_t last = 0;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (!page_quit) {
        struct pollfd p = {.fd = s, .events = POLLIN};
        int r = poll(&p, 1, shown ? 150 : 1000);
        if (r > 0) {
            char buf[64];
            ssize_t n;
            while ((n = recv(s, buf, sizeof buf - 1, MSG_DONTWAIT)) > 0) {
                buf[n] = 0;
                int x, y;
                if (!strcmp(buf, "enter")) { shown = 1; dirty = 1; }
                else if (!strcmp(buf, "leave")) shown = 0;
                else if (sscanf(buf, "size %d %d", &x, &y) == 2) {
                    if (x > 0 && y > 0 && x <= 1280 && y <= 800 && (x != W || y != H)) { W = x; H = y; dirty = 1; }
                    shown = 1;
                } else if (sscanf(buf, "up %d %d", &x, &y) == 2) { if (page_tap(x, y)) dirty = 1; }
            }
        }
        if (!shown && !dirty) continue;
        /* the engine changes on its own (downloads, the catalog): redraw when what the page shows changed */
        uint64_t h = read_state();
        if (h != last) dirty = 1;
        if (dirty) {
            page_render(fb, W, H);
            last = h;
            page_write_png(frame, fb, W, H);
            dirty = 0;
        }
    }
    free(fb);
    close(s);
    unlink(sock_path);
    return NULL;
}
