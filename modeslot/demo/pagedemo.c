// pagedemo: a test app for the modeslot live page host (mpc-vst-plugins docs/MPC_MODES.md, "Live page host").
// Listens on /run/openmpc-page/input.sock, draws a frame the size the shim reports ("size W H": the
// page is 1280 x 628 on a Live III, between MPC's bars; a picture of another size is scaled to fit,
// and then touches no longer line up) to /run/openmpc-page/frame.png (written whole, then renamed),
// once per batch of events (all pending events are read first) and twice a second while shown:
// six buttons that light when touched, a cross at the last touch, the last event, a moving bar.
// No libraries: the PNG is uncompressed (stored deflate blocks).
//   pagedemo [DIR]     (default /run/openmpc-page)
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAXW 1280
#define MAXH 800
static uint8_t px[MAXH][MAXW][3];
static int W = 1280, H = 800; // the frame size: the page's, once the shim says

static void fill(int x0, int y0, int x1, int y1, uint32_t c)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            px[y][x][0] = (uint8_t)(c >> 16);
            px[y][x][1] = (uint8_t)(c >> 8);
            px[y][x][2] = (uint8_t)c;
        }
}

// 5 x 7 font: space, 0-9, A-Z, ':', '-', '.'
static const char *glyphs = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-.";
static const uint8_t font[][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {31, 2, 4, 2, 1, 17, 14},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 30, 1, 1, 17, 14}, {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12},
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14}, {28, 18, 17, 17, 17, 18, 28},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 2, 18, 12}, {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 17, 25, 21, 19, 17, 17}, {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4}, {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
    {0, 12, 12, 0, 12, 12, 0}, {0, 0, 0, 31, 0, 0, 0}, {0, 0, 0, 0, 0, 12, 12},
};

static void text(int x, int y, int s, const char *t, uint32_t c)
{
    for (; *t; t++, x += 6 * s) {
        const char *g = strchr(glyphs, *t >= 'a' && *t <= 'z' ? *t - 32 : *t);
        if (!g) continue;
        const uint8_t *b = font[g - glyphs];
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (b[r] & (16 >> k)) fill(x + k * s, y + r * s, x + (k + 1) * s, y + (r + 1) * s, c);
    }
}

static uint32_t crc_table[256];
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8];
    be32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc(crc(0, (const uint8_t *)type, 4), d, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

static int write_png(const char *path)
{
    size_t raw_n = (size_t)H * (1 + (size_t)W * 3);
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < H; y++) {
        raw[(size_t)y * (1 + W * 3)] = 0;
        memcpy(raw + (size_t)y * (1 + W * 3) + 1, px[y], W * 3);
    }
    size_t blocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *q++ = off + n == raw_n;
        *q++ = (uint8_t)n; *q++ = (uint8_t)(n >> 8); *q++ = (uint8_t)~n; *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        for (size_t i = 0; i < n;) { // Adler-32, the modulo every 4096 bytes
            size_t k = n - i < 4096 ? n - i : 4096;
            for (size_t j = 0; j < k; j++) { a += raw[off + i + j]; b += a; }
            a %= 65521; b %= 65521;
            i += k;
        }
        q += n;
    }
    be32(q, (b << 16) | a);
    q += 4;
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(raw); free(z); return -1; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, W); be32(ihdr + 4, H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    int ok = fclose(f) == 0;
    free(raw); free(z);
    return ok ? rename(tmp, path) : -1;
}

static int lit = -1, tx = -1, ty = -1, ticks, events;
static char last[64] = "WAITING";

static void draw(void)
{
    fill(0, 0, W, H, 0x121418);
    fill(0, 0, W, 10, 0xe84834);
    text(40, 36, 6, "OPEN MPC PAGE DEMO", 0xf0f0f0);
    for (int i = 0; i < 6; i++) {
        int bh = (H - 300) / 2, x = 40 + (i % 3) * 410, y = 120 + (i / 3) * (bh + 20);
        fill(x, y, x + 380, y + bh, i == lit ? 0xe84834 : 0x2a2e36);
        char n[8];
        snprintf(n, sizeof n, "%d", i + 1);
        text(x + 170, y + bh / 2 - 25, 7, n, 0xf0f0f0);
    }
    char s[96];
    snprintf(s, sizeof s, "LAST: %s   EVENTS: %d   PAGE %dX%d", last, events, W, H);
    text(40, H - 120, 4, s, 0xaab0ba);
    fill(40, H - 60, 40 + (ticks % 40) * 30, H - 30, 0x3c8cff);
    if (tx >= 0) {
        fill(tx - 30, ty - 2, tx + 30, ty + 2, 0xffd400);
        fill(tx - 2, ty - 30, tx + 2, ty + 30, 0xffd400);
    }
}

static int button_at(int x, int y)
{
    for (int i = 0; i < 6; i++) {
        int bh = (H - 300) / 2, bx = 40 + (i % 3) * 410, by = 120 + (i / 3) * (bh + 20);
        if (x >= bx && x < bx + 380 && y >= by && y < by + bh) return i;
    }
    return -1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/run/openmpc-page";
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    signal(SIGPIPE, SIG_IGN);
    mkdir(dir, 0755);
    char sock_path[108], frame[300];
    snprintf(sock_path, sizeof sock_path, "%s/input.sock", dir);
    snprintf(frame, sizeof frame, "%s/frame.png", dir);
    int s = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s", sock_path);
    unlink(sock_path);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) { perror("bind"); return 1; }
    int shown = 0;
    draw();
    write_png(frame);
    for (;;) {
        struct pollfd p = {.fd = s, .events = POLLIN};
        int r = poll(&p, 1, 500);
        int dirty = 0;
        if (r > 0) {
            char buf[64];
            ssize_t n;
            while ((n = recv(s, buf, sizeof buf - 1, MSG_DONTWAIT)) > 0) { // the whole batch, then one frame
                buf[n] = 0;
                events++;
                snprintf(last, sizeof last, "%s", buf);
                int x, y;
                if (!strcmp(buf, "enter")) shown = 1;
                else if (!strcmp(buf, "leave")) shown = 0;
                else if (sscanf(buf, "size %d %d", &x, &y) == 2) {
                    if (x > 0 && y > 0 && x <= MAXW && y <= MAXH) { W = x; H = y; }
                } else if (sscanf(buf, "down %d %d", &x, &y) == 2 || sscanf(buf, "drag %d %d", &x, &y) == 2) {
                    tx = x; ty = y; lit = button_at(x, y);
                } else if (sscanf(buf, "up %d %d", &x, &y) == 2) { tx = x; ty = y; lit = -1; }
                if (strncmp(buf, "drag", 4)) fprintf(stderr, "pagedemo: %s\n", buf);
                dirty = 1;
            }
        } else if (r == 0 && shown) {
            ticks++;
            dirty = 1;
        }
        if (dirty) { draw(); write_png(frame); }
    }
}
