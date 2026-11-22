/* Offline check of src/page.c: draws the mode-slot page from a built skin with a stand-in engine.
 *
 *   [PAGE_H=738] page_test <skin dir> <font dir> <out.png> [key=value ...] [tap=X,Y ...]     (the table: <skin dir>/openmpc_page.txt)
 *
 * key=value sets what the stand-in engine answers for a parameter (an option's label or index, a text); taps are
 * replayed in order and every parameter they set is printed ("set key=value"), then the page is drawn.
 * With no values every parameter reads "" (options at their first). */
#include "../src/page.c"

static struct kv { char k[48], v[160]; } kvs[512];
static int nkv;

static int fake_get(void *inst, const char *key, char *buf, int n) {
    (void)inst;
    for (int i = 0; i < nkv; i++)
        if (!strcmp(kvs[i].k, key)) { snprintf(buf, (size_t)n, "%s", kvs[i].v); return (int)strlen(buf); }
    buf[0] = 0;
    return 0;
}

static void fake_set(void *inst, const char *key, const char *val) {
    (void)inst;
    printf("set %s=%s\n", key, val);
    if (!strcmp(val, "0") && (key[0] == 'k' || strstr(key, "btn") || strstr(key, "_go"))) return;   /* a momentary release */
    for (int i = 0; i < nkv; i++)
        if (!strcmp(kvs[i].k, key)) { snprintf(kvs[i].v, sizeof kvs[i].v, "%s", val); return; }
    if (nkv < 512) { snprintf(kvs[nkv].k, sizeof kvs[nkv].k, "%s", key); snprintf(kvs[nkv].v, sizeof kvs[nkv].v, "%s", val); nkv++; }
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: page_test <skin dir> <font dir> <out.png> [key=value ...] [tap=X,Y ...]\n"); return 2; }
    page_engine_t eng = {NULL, fake_get, fake_set};
    char table[600];
    snprintf(table, sizeof table, "%s/openmpc_page.txt", argv[1]);
    if (page_load(table, argv[1], argv[2], eng)) { fprintf(stderr, "page_load failed\n"); return 1; }
    printf("page %dx%d: %d components, %d items, %d parameters, %d images\n", P.w, P.h, P.ncomps, P.nitems, P.nparams, P.nimgs);
    if (getenv("PAGE_H") && atoi(getenv("PAGE_H")) > P.h && atoi(getenv("PAGE_H")) <= 800) P.dh = atoi(getenv("PAGE_H")) - P.h;
    for (int i = 4; i < argc; i++) {
        int x, y;
        if (sscanf(argv[i], "tap=%d,%d", &x, &y) == 2) printf("tap %d,%d: %s\n", x, y, page_tap(x, y) ? "acted" : "nothing");
        else {
            char *eq = strchr(argv[i], '=');
            if (!eq || nkv >= 512) continue;
            *eq = 0;
            snprintf(kvs[nkv].k, sizeof kvs[nkv].k, "%s", argv[i]);
            snprintf(kvs[nkv].v, sizeof kvs[nkv].v, "%s", eq + 1);
            nkv++;
        }
    }
    static uint8_t fb[1280 * 800 * 3];
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    int H = getenv("PAGE_H") ? atoi(getenv("PAGE_H")) : P.h;   /* the page the mode slot gives (taller at full height) */
    if (H < P.h || H > 800) H = P.h;
    page_render(fb, P.w, H);
    clock_gettime(CLOCK_MONOTONIC, &b);
    printf("rendered %dx%d in %.1f ms (%zu MB of images decoded)\n", P.w, H, (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6, P.decoded >> 20);
    if (page_write_png(argv[3], fb, P.w, H)) { fprintf(stderr, "write failed\n"); return 1; }
    printf("wrote %s\n", argv[3]);
    return 0;
}
