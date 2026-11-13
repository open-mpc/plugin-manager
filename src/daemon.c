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

    for (;;) pause();   /* the threads do all the work; stay alive until signalled */
}
