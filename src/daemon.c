#define _GNU_SOURCE   /* must precede every include: manager.c needs dladdr/accept4 */
/* OpenMPC Plugin Manager — standalone background daemon (Gen2 / MPC OS 3.10.1 only).
 *
 * Runs the manager's worker + LAN web server (web.c, port 8790) as a long-lived
 * process, so the browser UI (http://<mpc>:8790) is available whenever the device
 * is on, not only while the Plugin Manager VST is loaded on a track.
 *
 * It reuses the plugin's own code verbatim: manager.c is self-contained (its engine
 * never renders; mpc_engine_t is defined inline, so no VST wrapper is needed), and
 * mgr_create() already starts both threads. We add only main() + signal handling.
 * web.c's bind-retry means the daemon and a loaded plugin instance never fight over
 * the port — whichever binds :8790 first serves, the other waits.
 *
 * SCOPE: aarch64 / MPC OS 3.10.1. On 3.10.1 a newly installed plugin folder is
 * picked up on the next MPC restart with no MPC.settings edit, so the daemon does
 * not need the settings-rewrite path that older firmware required. Not built or
 * supported for Gen1 / 3.9.x here.
 *
 * Build: vst/build_daemon.sh  (cross-compiles in the same aarch64 container as the .so)
 */
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

/* Pull in the whole plugin (web.c is #included at the foot of manager.c). Everything
 * is static; the only exported symbol, mpc_engine(), is harmless/unused in an exe. */
#include "manager.c"

/* The mode-slot page (src/page.c): with the modeslot addin, the Manager has a page of its own in MPC's MENU. */
#include <glob.h>
#include <libgen.h>
#include "page.c"

static int pg_get(void *inst, const char *key, char *buf, int n) { return mgr_get_param(inst, key, buf, n); }
static void pg_set(void *inst, const char *key, const char *val) { mgr_set_param(inst, key, val); }

/* Find the Manager's installed skin, the page table (<skin>/openmpc_page.txt, made by vst/gen_page.py at build time) and
 * the fonts (PM_PAGE_SKIN, PM_PAGE_TABLE, PM_PAGE_FONTS override them), then start the page thread. Nothing happens without
 * a page table. */
static void page_start(void *inst) {
    const char *off = getenv("PM_PAGE");
    if (off && !strcmp(off, "0")) return;
    char exe[512] = "", skin[512] = "", table[600] = "", fonts[512] = "";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) exe[n] = 0;
    char *exedir = dirname(exe);
    const char *e;
    if ((e = getenv("PM_PAGE_SKIN")) && *e) snprintf(skin, sizeof skin, "%s", e);
    else {
        static const char *pats[] = {"/nvme/VST2 Plugins/OpenMPC - VST - Plugin Manager*/Plugin Skins",
                                     "/media/*/VST2 Plugins/OpenMPC - VST - Plugin Manager*/Plugin Skins",
                                     "/data/UserData/VST2 Plugins/OpenMPC - VST - Plugin Manager*/Plugin Skins"};
        for (size_t i = 0; i < sizeof pats / sizeof *pats && !skin[0]; i++) {
            glob_t g;
            if (glob(pats[i], 0, NULL, &g) == 0 && g.gl_pathc) snprintf(skin, sizeof skin, "%s", g.gl_pathv[0]);
            globfree(&g);
        }
    }
    if ((e = getenv("PM_PAGE_TABLE")) && *e) snprintf(table, sizeof table, "%s", e);
    else {
        snprintf(table, sizeof table, "%s/openmpc_page.txt", skin);
        if (access(table, R_OK)) snprintf(table, sizeof table, "%s/openmpc_page.txt", exedir);
    }
    /* the fonts: next to the daemon, else in the plugin folder's bgserver/ (the release ships them there) */
    if ((e = getenv("PM_PAGE_FONTS")) && *e) snprintf(fonts, sizeof fonts, "%s", e);
    else {
        char probe[700];
        snprintf(fonts, sizeof fonts, "%s", exedir);
        snprintf(probe, sizeof probe, "%s/TitilliumWeb-SemiBold.ttf", fonts);
        if (access(probe, R_OK) && skin[0]) snprintf(fonts, sizeof fonts, "%s/../bgserver", skin);
    }
    page_engine_t eng = {inst, pg_get, pg_set};
    if (!skin[0] || access(table, R_OK) || page_load(table, skin, fonts, eng)) {
        fprintf(stderr, "openmpc-pm-daemon: no mode-slot page (skin \"%s\", table %s, fonts %s)\n", skin, table, fonts);
        return;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, page_thread, NULL) == 0) {
        pthread_detach(t);
        fprintf(stderr, "openmpc-pm-daemon: mode-slot page from %s\n", skin);
    }
}

static void *g_inst;

static void on_signal(int sig) {
    (void)sig;
    void *inst = g_inst;   /* mgr_destroy sets quit, joins both threads, frees. Once. */
    g_inst = NULL;
    if (inst) mgr_destroy(inst);
    _exit(0);
}

int main(void) {
    /* The daemon IS the server: always serve, regardless of the saved "web" setting
     * or PM_NO_WEB (that env is for the VST/tests). */
    unsetenv("PM_NO_WEB");

    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);
    signal(SIGPIPE, SIG_IGN);

    g_inst = mgr_create(NULL);   /* starts worker + web_thread */
    if (!g_inst) { fprintf(stderr, "openmpc-pm-daemon: mgr_create failed\n"); return 1; }
    page_start(g_inst);

    for (;;) pause();   /* the threads do all the work; stay alive until signalled */
}
