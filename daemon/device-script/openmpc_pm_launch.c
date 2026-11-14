/*
 * openmpc_pm_launch.c - native bridge for the OpenMPC Plugin Manager daemon launcher
 * Lua device script. Called via package.loadlib() from openmpc_pm_launch.lua at MPC boot
 * (MPC runs device scripts under "MPC Documents/Devices/" at every startup). The entry
 * point is a lua_CFunction so package.loadlib can return it as a callable function.
 *
 * It spawns the background daemon (/data/openmpc-pm/openmpc-pm-daemon) once per boot,
 * detached. This is the Gen2-native, verity-safe persistence hook: Gen2 roots are
 * read-only (no writable /etc, no systemd unit can persist), so boot work runs through
 * the MPC Lua device-script mechanism instead — the same pattern as addin_restore.
 *
 * Build (aarch64 / Gen2 / MPC OS 3.10.x):
 *   aarch64-linux-gnu-gcc -shared -fPIC -O2 -o openmpc_pm_launch.so openmpc_pm_launch.c
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define GUARD_FILE "/run/openmpc-pm-launch-done"
#define DAEMON_BIN "/data/openmpc-pm/openmpc-pm-daemon"
#define LOG_FILE   "/tmp/openmpc-pm-launch.log"
#define WEB_PORT   8790

static FILE *logfp;
static void logmsg(const char *m) {
    if (!logfp) logfp = fopen(LOG_FILE, "a");
    if (logfp) { fprintf(logfp, "[openmpc-pm-launch] %s\n", m); fflush(logfp); }
}

/* Is something already serving :8790 (a running daemon, or a loaded VST instance)? */
static int already_serving(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(WEB_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
    close(s);
    return ok;
}

static void spawn_daemon(void) {
    pid_t p = fork();
    if (p != 0) return;        /* parent (MPC's Lua thread) returns immediately */
    /* Launch the daemon in its OWN transient systemd unit, so it lives OUTSIDE
     * acvs.service's cgroup. MPC runs this .so, so a bare fork stays in the acvs
     * cgroup and is killed when addin-restore (or anything) restarts acvs a few
     * seconds after boot. systemd-run --collect gives it an independent scope that
     * survives that restart. (Same mechanism manager.c uses for the apply restart.) */
    execlp("systemd-run", "systemd-run",
           "--unit=openmpc-pm-daemon", "--collect", "--quiet",
           DAEMON_BIN, (char *)NULL);
    /* Fallback if systemd-run is unavailable: fully detached double-fork. */
    setsid();
    if (fork() != 0) _exit(0);
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); if (devnull > 2) close(devnull); }
    execl(DAEMON_BIN, DAEMON_BIN, (char *)NULL);
    _exit(127);                /* exec failed */
}

/* lua_CFunction: package.loadlib returns it as callable. L is opaque; we ignore it. */
int openmpc_pm_launch_check(void *L) {
    (void)L;
    if (access(GUARD_FILE, F_OK) == 0) { logmsg("guard present, already ran this boot"); return 0; }
    if (access(DAEMON_BIN, X_OK) != 0) { logmsg("daemon binary missing: " DAEMON_BIN); return 0; }
    if (already_serving()) {
        logmsg(":8790 already served (daemon or VST); not spawning");
    } else {
        logmsg("spawning daemon: " DAEMON_BIN);
        spawn_daemon();
    }
    FILE *g = fopen(GUARD_FILE, "w");
    if (g) { fprintf(g, "ran\n"); fclose(g); }
    return 0;
}
