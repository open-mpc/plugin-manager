// modeslot: LD_PRELOAD shim for MPC OS (Gen2, aarch64) that turns an unused ModeID (the first free of
// 0x2090, 0x2092, 0x2099) into a mode you can enter, leave and see ("Hello slot"; mpc-vst-plugins docs/MPC_MODES.md; plugin-manager modeslot/).
//
// Hooks (each one's entry checked against the profile before anything is changed):
//  1. getModeName(ModeID) -> juce::String (x8): the slot gets a name, so MPC can address it.
//  2. registerMode(manager, ModeID, Mode *): Main Mode's controller object is registered for the slot
//     too. Without a controller nothing handles the slot's buttons and it cannot be left
//     (device, 2026-11-22). MPC already shares that object between eight IDs.
//  3. The touch UI's mode-change handler (this, ModeID): records the current screen mode.
//  4. juce::ImageCache::getFromMemory(data, size) -> Image (x8): while the slot is the current mode,
//     MPC's "page coming soon" picture is swapped for our own PNG file, which the placeholder page
//     then draws the way it draws its own.
//  5. The MENU grid's icon lookup (ModeID) -> Image (x8): the slot gets our icon PNG. With the slot
//     named in MPC.settings (ModeMenuPage<p>Slot<s> = "Plugin Manager") it becomes a MENU tile.
//  6. ModeMenuOrder setup (order, const std::vector<ModeID> &, settings): the MENU grid is built from
//     that list of allowed modes, and saved names of other modes are dropped (MENU tile missing,
//     device 2026-11-22). The hook passes a copy with the slot appended.
//
// Stands aside when the slot turns out to be taken: if MPC's own getModeName has a name for it, or
// anything but the shim registers a controller for it, omm_yield is set and every hook passes
// through from then on (no name, controller, page, icon or tile of ours; one log line).
//
// Fails closed. Hooks are installed only when all of these hold:
//   - the process executable is called "MPC" and OMM_DISABLE is not set;
//   - the executable's GNU build-id is in the profile table below;
//   - every hooked function starts with exactly the expected four instructions, the String
//     constructor's first instruction matches, and every address lies in an executable segment.
// Otherwise it logs one line to stderr (MPC's journal) and changes nothing.
//
// Mechanism: per hook, the four entry instructions go to a trampoline (an adrp among them is
// rewritten as a load of its absolute value; other PC-relative instructions are refused), followed
// by a jump back to entry+16; the entry becomes "ldr x16, #8; br x16; .quad hook". This runs in a
// library constructor, before MPC starts any thread. The MPC binary has no BTI property note.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "modeslot.h"

// page.png, icon.png and an app's start script sit next to this library (/data/mpc-addins/<id>/).
#define APP_START "pm-app-start.sh"

// The folder this library was loaded from ("" if unknown).
static void so_dir(char *out, size_t n)
{
    Dl_info di;
    out[0] = 0;
    if (!dladdr((void *)so_dir, &di) || !di.dli_fname || di.dli_fname[0] != '/') return;
    snprintf(out, n, "%s", di.dli_fname);
    char *sl = strrchr(out, '/');
    if (sl && sl != out) *sl = 0;
    else out[0] = 0;
}

// An app shipped with the slot (the Plugin Manager app): run its start script, which starts the program that draws the
// page. Fully detached (a double fork, so MPC never waits on it), and without LD_PRELOAD, so nothing it runs loads the
// addins. Called once, from MPC's start, after the slot is installed.
static void start_app(const char *dir)
{
    char script[600];
    if (getenv("OMM_NO_APP") || !dir[0]) return;
    snprintf(script, sizeof script, "%s/" APP_START, dir);
    if (access(script, R_OK) != 0) return;
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (fork() == 0) {
            setsid();
            unsetenv("LD_PRELOAD");
            int fd = open("/dev/null", O_RDWR);
            if (fd >= 0) { dup2(fd, 0); if (fd > 2) close(fd); }
            execl("/bin/sh", "sh", script, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    (void)waitpid(pid, NULL, 0);
    fprintf(stderr, "modeslot: started %s\n", script);
}
#define MAX_PAGE_PNG (16u << 20)

// Read by hook.S.
uintptr_t omm_tramp_addr, omm_reg_tramp_addr, omm_img_tramp_addr, omm_icon_tramp_addr, omm_order_tramp_addr;
uintptr_t omm_ctor;
uint32_t omm_slot_id;
const char *omm_slot_name;
uint32_t omm_alias_from;
uint32_t omm_cur_mode;          // last screen mode seen by hook 3
const void *omm_dummy_data;     // MPC's placeholder PNG
uint32_t omm_dummy_size;
const void *omm_png_data;       // ours
uint32_t omm_png_size;          // 0 = no picture: the placeholder stays
const void *omm_icon_data;      // our MENU icon
uint32_t omm_icon_size;         // 0 = no icon (MPC's default: none)
uint32_t omm_yield;             // 1 = the slot is someone else's: every hook passes through
uint32_t omm_img_refcount;      // offset of the reference count in an Image's pixel data (the 32-bit image hook)

// The launcher tile (Gen2; read by hook.S): a second free slot with a name, an icon and a picture of its own
// and no live page. Entering it runs tile.conf's RUN, as a systemd service of its own: the command may stop MPC
// (a launcher does), and a child of MPC would be stopped with it.
uint32_t omm_slot2_id;
const char *omm_slot2_name;
uint32_t omm_yield2;            // 1 = the second slot is someone else's: its hooks pass through
const void *omm_png2_data;
uint32_t omm_png2_size;
const void *omm_icon2_data;
uint32_t omm_icon2_size;
char omm_tile2_run[512];
static char tile2_name[64];

#if defined(__aarch64__)
#include "profiles_a64.h"
#elif defined(__arm__)
#include "profiles_a32.h"
#else
#error "modeslot: no profiles for this architecture"
#endif

static void say(const char *fmt, const char *a, const char *b)
{
    fprintf(stderr, "modeslot: ");
    fprintf(stderr, fmt, a, b);
    fputc('\n', stderr);
}

struct main_info {
    int found;
    uintptr_t base;
    const ElfW(Phdr) *phdr;
    int phnum;
};

static int first_object(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    struct main_info *m = data;
    m->found = 1;
    m->base = info->dlpi_addr;
    m->phdr = info->dlpi_phdr;
    m->phnum = info->dlpi_phnum;
    return 1; // the first object is the main program
}

int omm_main_build_id(uintptr_t *base, uint8_t *id, size_t cap, size_t *len)
{
    struct main_info m = {0};
    dl_iterate_phdr(first_object, &m);
    if (!m.found) return -1;
    *base = m.base;
    for (int i = 0; i < m.phnum; i++) {
        if (m.phdr[i].p_type != PT_NOTE) continue;
        const uint8_t *p = (const uint8_t *)(m.base + m.phdr[i].p_vaddr);
        const uint8_t *end = p + m.phdr[i].p_memsz;
        while (p + 12 <= end) {
            const ElfW(Nhdr) *n = (const ElfW(Nhdr) *)p;
            const uint8_t *name = p + 12;
            const uint8_t *desc = name + ((n->n_namesz + 3u) & ~3u);
            const uint8_t *next = desc + ((n->n_descsz + 3u) & ~3u);
            if (next > end) break;
            if (n->n_type == NT_GNU_BUILD_ID && n->n_namesz == 4 && !memcmp(name, "GNU", 4)) {
                if (n->n_descsz > cap) return -1;
                memcpy(id, desc, n->n_descsz);
                *len = n->n_descsz;
                return 0;
            }
            p = next;
        }
    }
    return -1;
}

static int in_segment(uintptr_t base, uintptr_t addr, size_t len, int exec)
{
    struct main_info m = {0};
    dl_iterate_phdr(first_object, &m);
    if (!m.found || m.base != base) return 0;
    for (int i = 0; i < m.phnum; i++) {
        const ElfW(Phdr) *ph = &m.phdr[i];
        if (ph->p_type != PT_LOAD || (exec && !(ph->p_flags & PF_X))) continue;
        uintptr_t lo = base + ph->p_vaddr, hi = lo + ph->p_memsz;
        if (addr >= lo && addr + len <= hi) return 1;
    }
    return 0;
}

static const char *check_site(uintptr_t base, const struct omm_site *s, int check_segments)
{
    const uint32_t *entry = (const uint32_t *)(base + s->addr);
    if (check_segments && !in_segment(base, (uintptr_t)entry, 4 * OMM_ENTRY_WORDS, 1)) return "hook address outside the executable segment";
    if (((uintptr_t)entry & 3) != 0) return "hook address not aligned";
    if (memcmp(entry, s->prologue, 4 * OMM_ENTRY_WORDS) != 0) return "entry instructions differ";
    return omm_arch_check_entry(s->prologue);
}

uint32_t omm_pick_slot(uintptr_t base, const struct omm_profile *p) { return omm_pick_slot_except(base, p, 0); }

uint32_t omm_pick_slot_except(uintptr_t base, const struct omm_profile *p, uint32_t except)
{
    for (int i = 0; i < 4 && p->candidates[i]; i++) {
        uint32_t id = p->candidates[i];
        if (id == except) continue;
        if (omm_arch_table_default(base, p->name_table, p->name_table_base, p->name_default, p->table_len, id) &&
            omm_arch_table_default(base, p->page_table, p->page_table_base, p->page_default, p->table_len, id))
            return id;
    }
    return 0;
}

const char *omm_install(uintptr_t base, const struct omm_profile *p, int check_segments)
{
    enum { N = 7 };
    const struct omm_site *sites[N] = {&p->get_mode_name, &p->register_mode, &p->mode_changed,
                                       &p->image_from_memory, &p->mode_icon, &p->menu_order, &p->page_paint};
    void (*hooks[N])(void) = {(void (*)(void))omm_hook, (void (*)(void))omm_reg_hook, (void (*)(void))omm_mode_hook_c,
                              (void (*)(void))omm_img_hook, (void (*)(void))omm_icon_hook,
                              (void (*)(void))omm_order_hook, (void (*)(void))omm_paint_hook};
    uintptr_t *tramps[N] = {&omm_tramp_addr, &omm_reg_tramp_addr, &omm_mode_tramp_addr, &omm_img_tramp_addr,
                            &omm_icon_tramp_addr, &omm_order_tramp_addr, &omm_paint_tramp_addr};
    const uint32_t *ctor = (const uint32_t *)(base + p->string_ctor);
    const char *err;

    // Every check first, so a refusal changes nothing. Hook 1 is required; 2..6 are optional.
    if (!p->get_mode_name.addr) return "no getModeName in the profile";
    if (p->mode_icon.addr && !p->image_from_memory.addr) return "icon hook needs the image hook";
    for (int i = 0; i < N; i++)
        if (sites[i]->addr && (err = check_site(base, sites[i], check_segments))) return err;
    if (check_segments && !in_segment(base, (uintptr_t)ctor, 4, 1)) return "hook address outside the executable segment";
    if (*ctor != p->string_ctor_first) return "String constructor differs";
    if (check_segments && (!in_segment(base, base + p->name_table, 2 * p->table_len, 0) ||
                           !in_segment(base, base + p->page_table, 2 * p->table_len, 0)))
        return "mode tables outside the program";
    // 7. the live page host: every MPC function it calls, and the mouse slots it replaces
    if (p->page_paint.addr) {
        if (!p->mode_changed.addr || !p->image_from_memory.addr) return "page host needs the mode and image hooks";
        const struct omm_fn *f[6] = {&p->repaint, &p->repaint_area, &p->memstream_ctor, &p->memstream_dtor, &p->find_format,
                                     p->set_bounds.addr ? &p->set_bounds : &p->repaint};
        for (int i = 0; i < 6; i++) {
            if (check_segments && !in_segment(base, base + f[i]->addr, 4, 1)) return "page host function outside the program";
            if (*(const uint32_t *)(base + f[i]->addr) != f[i]->first) return "page host function differs";
        }
        for (int i = 0; i < 3; i++) {
            if (check_segments && !in_segment(base, base + p->mouse_slots[i], 8, 0)) return "mouse slot outside the program";
            if (*(const uintptr_t *)(base + p->mouse_slots[i]) != base + p->mouse_default) return "mouse slot differs";
        }
    }
    uint32_t slot = omm_pick_slot(base, p);
    if (!slot) return "no free mode slot (all candidates are in use)";
    if ((p->image_from_memory.addr != 0) != (p->mode_changed.addr != 0)) return "image hook needs the mode hook";
    const void *dummy = NULL;
    if (p->image_from_memory.addr) {
        const void *const *var = (const void *const *)(base + p->placeholder_ptr_var);
        if (check_segments && !in_segment(base, (uintptr_t)var, 8, 0)) return "placeholder pointer outside the program";
        dummy = *var;
        if (check_segments && !in_segment(base, (uintptr_t)dummy, p->placeholder_size, 0))
            return "placeholder picture outside the program";
        if (memcmp(dummy, "\x89PNG", 4) != 0) return "placeholder is not a PNG";
    }

    long pg = sysconf(_SC_PAGESIZE);
    if (pg <= 0) return "no page size";
    uint32_t *t = mmap(NULL, (size_t)pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (t == MAP_FAILED) return "mmap failed";
    for (int i = 0; i < N; i++)
        if (sites[i]->addr) {
            uintptr_t a = base + sites[i]->addr;
            omm_fill_tramp(t + OMM_TRAMP_WORDS * i, (const uint32_t *)a, a);
            *tramps[i] = (uintptr_t)(t + OMM_TRAMP_WORDS * i);
        }
    if (mprotect(t, (size_t)pg, PROT_READ | PROT_EXEC) != 0) {
        munmap(t, (size_t)pg);
        return "mprotect trampoline failed";
    }
    __builtin___clear_cache((char *)t, (char *)(t + OMM_TRAMP_WORDS * N));

    omm_ctor = (uintptr_t)ctor;
#if defined(__aarch64__) // hook.S knows the second slot; the 32-bit hooks do not (yet)
    if (tile2_name[0] && (omm_slot2_id = omm_pick_slot_except(base, p, slot)) != 0) omm_slot2_name = tile2_name;
#endif
    omm_slot_id = slot;
    omm_slot_name = p->slot_name;
    omm_alias_from = p->alias_from;
    omm_dummy_data = dummy;
    omm_dummy_size = p->placeholder_size;
    omm_img_refcount = p->img_refcount;

    if (p->page_paint.addr) {
        struct omm_live_fns lf = {
            .repaint = (void (*)(void *))(base + p->repaint.addr),
            .repaint_area = omm_arch_repaint_rect(base + p->repaint_area.addr),
            .memstream_ctor = (void (*)(void *, const void *, size_t, int))(base + p->memstream_ctor.addr),
            .memstream_dtor = (void (*)(void *))(base + p->memstream_dtor.addr),
            .find_format = (void *(*)(void *))(base + p->find_format.addr),
            .decode_vslot = p->decode_vslot,
            .page_component_offset = p->page_component_offset,
            .set_bounds = p->set_bounds.addr && !(getenv("OMM_FULL_HEIGHT") && !strcmp(getenv("OMM_FULL_HEIGHT"), "0"))
                              ? (void (*)(void *, int, int, int, int))(base + p->set_bounds.addr) : NULL,
            .dummy_page_offset = p->dummy_page_offset,
            .page_holder_field = p->page_holder_field,
            .page_vtable = p->page_vtable ? base + p->page_vtable : 0,
            .wrapper_vtable = p->wrapper_vtable ? base + p->wrapper_vtable : 0,
            .comp_parent = p->comp_parent, .comp_bounds = p->comp_bounds,
            .comp_children = p->comp_children, .comp_children_used = p->comp_children_used, .img_refcount = p->img_refcount,
            .whole_screen = !(getenv("OMM_FULL_HEIGHT") && !strcmp(getenv("OMM_FULL_HEIGHT"), "1")),
        };
        omm_live_setup(&lf);
        // The mouse slots live in RELRO: writable just for the write.
        uintptr_t lo = (base + p->mouse_slots[0]) & ~((uintptr_t)pg - 1);
        uintptr_t hi = (base + p->mouse_slots[2] + 8 + (uintptr_t)pg - 1) & ~((uintptr_t)pg - 1);
        if (mprotect((void *)lo, hi - lo, PROT_READ | PROT_WRITE) != 0) return "mprotect vtable (write) failed";
        void (*m[3])(void *, const float *) = {omm_mouse_down, omm_mouse_drag, omm_mouse_up};
        for (int i = 0; i < 3; i++) *(uintptr_t *)(base + p->mouse_slots[i]) = (uintptr_t)m[i];
        if (mprotect((void *)lo, hi - lo, PROT_READ) != 0) return "mprotect vtable (read) failed";
    }

    // The name hook last: until it is in, nothing can ask for the slot.
    for (int i = N - 1; i >= 0; i--)
        if (sites[i]->addr && (err = omm_arch_patch((uint32_t *)(base + sites[i]->addr), hooks[i], pg))) return err;
    return NULL;
}

void omm_order_hook(void *order, const struct omm_mode_vec *modes, void *settings)
{
    void (*orig)(void *, const struct omm_mode_vec *, void *) =
        (void (*)(void *, const struct omm_mode_vec *, void *))omm_order_tramp_addr;
    size_t n = (size_t)(modes->end - modes->begin);
    static int said;
    if (omm_yield) {
        if (!said++) fprintf(stderr, "modeslot: slot %#x is used by something else; standing aside\n", (unsigned)omm_slot_id);
        orig(order, modes, settings);
        return;
    }
    int add1 = 1, add2 = omm_slot2_id && !omm_yield2;
    for (size_t i = 0; i < n; i++) { // already in MPC's list: not ours to add
        if (modes->begin[i] == omm_slot_id) add1 = 0;
        if (modes->begin[i] == omm_slot2_id) add2 = 0;
    }
    if (!add1 && !add2) {
        orig(order, modes, settings);
        return;
    }
    // MPC copies the list (ModeMenuOrderImpl), so a temporary is enough.
    uint32_t *buf = malloc((n + 2) * sizeof *buf);
    if (!buf) {
        orig(order, modes, settings);
        return;
    }
    if (n) memcpy(buf, modes->begin, n * sizeof *buf);
    size_t m = n;
    if (add1) buf[m++] = omm_slot_id;
    if (add2) buf[m++] = omm_slot2_id;
    struct omm_mode_vec v = {buf, buf + m, buf + m};
    orig(order, &v, settings);
    free(buf);
}

static size_t load_png(const char *path, const void **data, uint32_t *size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    void *buf = NULL;
    size_t n = 0;
    if (fstat(fd, &st) == 0 && st.st_size > 8 && (size_t)st.st_size <= MAX_PAGE_PNG &&
        (buf = malloc((size_t)st.st_size)) != NULL) {
        while (n < (size_t)st.st_size) {
            ssize_t r = read(fd, (char *)buf + n, (size_t)st.st_size - n);
            if (r <= 0) break;
            n += (size_t)r;
        }
    }
    close(fd);
    if (!buf || n != (size_t)st.st_size || memcmp(buf, "\x89PNG\r\n\x1a\n", 8) != 0) {
        free(buf);
        return 0;
    }
    *data = buf; // kept for the life of the process: ImageCache keys on this pointer
    *size = (uint32_t)n;
    return n;
}

size_t omm_load_page(const char *path) { return load_png(path, &omm_png_data, &omm_png_size); }

// The launcher tile's command: a transient systemd service, fully detached from MPC and without LD_PRELOAD.
static void run_detached(const char *cmd)
{
    if (!cmd || !*cmd) return;
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        if (fork() == 0) {
            setsid();
            unsetenv("LD_PRELOAD");
            int fd = open("/dev/null", O_RDWR);
            if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
            char unit[64];
            snprintf(unit, sizeof unit, "--unit=modeslot-tile-%ld", (long)time(NULL));
            execl("/usr/bin/systemd-run", "systemd-run", "--collect", "--quiet", unit, "/bin/sh", "-c", cmd, (char *)NULL);
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); // no systemd-run: at least run it
            _exit(127);
        }
        _exit(0);
    }
    (void)waitpid(pid, NULL, 0);
    fprintf(stderr, "modeslot: tile \"%s\": ran %s\n", omm_slot2_name ? omm_slot2_name : "", cmd);
}
void (*omm_tile2_runner)(const char *cmd) = run_detached;

// <dir>/tile.conf: NAME=<tile name> and RUN=<command>, one per line (# comments); icon.png and page.png beside
// it. Returns 1 when a tile is configured.
int omm_tile2_config(const char *dir)
{
    char path[600], line[600];
    snprintf(path, sizeof path, "%s/tile.conf", dir);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    tile2_name[0] = 0;
    omm_tile2_run[0] = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v;
        if (!strncmp(line, "NAME=", 5)) v = line + 5, snprintf(tile2_name, sizeof tile2_name, "%.63s", v);
        else if (!strncmp(line, "RUN=", 4)) v = line + 4, snprintf(omm_tile2_run, sizeof omm_tile2_run, "%.511s", v);
    }
    fclose(f);
    if (!tile2_name[0]) return 0;
    snprintf(path, sizeof path, "%s/page.png", dir);
    load_png(path, &omm_png2_data, &omm_png2_size);
    snprintf(path, sizeof path, "%s/icon.png", dir);
    load_png(path, &omm_icon2_data, &omm_icon2_size);
    return 1;
}
size_t omm_load_icon(const char *path) { return load_png(path, &omm_icon_data, &omm_icon_size); }

static int exe_is_mpc(void)
{
    char buf[512];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
    const char *b = strrchr(buf, '/');
    b = b ? b + 1 : buf;
    return strcmp(b, "MPC") == 0;
}

__attribute__((constructor)) static void omm_init(void)
{
    if (getenv("OMM_DISABLE")) return;
    if (!exe_is_mpc()) return;

    // An app beside the library (the Plugin Manager app) starts whether or not the slot can be made on this firmware:
    // without the slot it still serves its web page.
    char dir[512];
    so_dir(dir, sizeof dir);
    uintptr_t base;
    uint8_t id[64];
    size_t len = 0;
    if (omm_main_build_id(&base, id, sizeof id, &len) != 0) {
        say("refused: no build-id in the MPC binary%s%s", "", "");
        start_app(dir);
        return;
    }
    char hex[2 * sizeof id + 1];
    for (size_t i = 0; i < len; i++) snprintf(hex + 2 * i, 3, "%02x", id[i]);

    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; i++) {
        const struct omm_profile *p = &profiles[i];
        if (p->build_id_len != len || memcmp(p->build_id, id, len) != 0) continue;
        char dpage[600], dicon[600];
        snprintf(dpage, sizeof dpage, "%s/page.png", dir[0] ? dir : "/data/mpc-addins/modeslot");
        snprintf(dicon, sizeof dicon, "%s/icon.png", dir[0] ? dir : "/data/mpc-addins/modeslot");
        const char *path = getenv("OMM_PAGE_PNG");
        if (!path || !*path) path = dpage;
        size_t png = p->image_from_memory.addr ? omm_load_page(path) : 0;
        const char *ipath = getenv("OMM_ICON_PNG");
        if (!ipath || !*ipath) ipath = dicon;
        size_t icon = p->mode_icon.addr ? omm_load_icon(ipath) : 0;
        const char *tdir = getenv("OMM_TILE2_DIR");
        int tile2 = omm_tile2_config(tdir && *tdir ? tdir : "/data/mpc-addins/launcher-tile");
        const char *err = omm_install(base, p, 1);
        if (err) {
            say("refused (%s): %s", hex, err);
            start_app(dir);
            return;
        }
        fprintf(stderr, "modeslot: installed: %s, slot %#x named \"%s\"%s%s, page %s (%zu bytes), icon %s (%zu bytes)\n",
                p->label, (unsigned)omm_slot_id, p->slot_name, p->register_mode.addr ? ", controller of " : "",
                p->register_mode.addr ? "Main Mode" : "", png ? path : "none (placeholder)", png,
                icon ? ipath : "none", icon);
        if (p->menu_order.addr) fprintf(stderr, "modeslot: MENU grid may hold the slot (ModeMenuPage<p>Slot<s> = \"%s\")\n", p->slot_name);
        if (tile2)
        {
            char where[32];
            if (omm_slot2_id) snprintf(where, sizeof where, "slot %#x", (unsigned)omm_slot2_id);
            else snprintf(where, sizeof where, "no second free slot");
            fprintf(stderr, "modeslot: launcher tile \"%s\": %s, page %u bytes, icon %u bytes, runs: %s\n", tile2_name, where,
                    omm_png2_size, omm_icon2_size, omm_tile2_run[0] ? omm_tile2_run : "(nothing)");
        }
        start_app(dir);
        return;
    }
    say("refused: unknown MPC build-id %s%s", hex, "");
    start_app(dir);
}
