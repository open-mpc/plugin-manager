/*
 * openmpc_pm_boot.c - a minimal OpenMPC "boot addin" that launches the Plugin
 * Manager background daemon once at MPC start, then does nothing.
 *
 * It is an LD_PRELOAD .so placed in /data/mpc-addins/openmpc-pm/ like any other
 * OpenMPC addin, so the already-signed, already-working "Addin Restore" Lua device
 * script re-injects it into MPC's LD_PRELOAD at every boot (verity-safe Gen2
 * persistence) with no new device-script signing required. Its constructor
 * double-forks a detached daemon and returns immediately, so MPC runs normally.
 * The daemon itself is a separate process; nothing of it stays inside MPC.
 *
 * Build (aarch64 / Gen2):
 *   aarch64-linux-gnu-gcc -shared -fPIC -O2 -o openmpc_pm_boot.so openmpc_pm_boot.c
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define GUARD_FILE "/run/openmpc-pm-boot-done"
#define DAEMON_BIN "/data/openmpc-pm/openmpc-pm-daemon"
#define WEB_PORT   8790

/* Only one of MPC's several process instances should spawn it; the guard + the
 * daemon's own single-port bind keep it to one. */
static int already_serving(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(WEB_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
    close(s);
    return ok;
}

__attribute__((constructor))
static void openmpc_pm_boot(void) {
    /* once per boot */
    if (access(GUARD_FILE, F_OK) == 0) return;
    if (access(DAEMON_BIN, X_OK) != 0) return;
    int g = open(GUARD_FILE, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (g < 0) return;            /* another instance won the race */
    close(g);
    if (already_serving()) return;

    pid_t p = fork();
    if (p != 0) return;           /* MPC continues */
    setsid();
    pid_t p2 = fork();
    if (p2 != 0) _exit(0);
    int dn = open("/dev/null", O_RDWR);
    if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); if (dn > 2) close(dn); }
    execl(DAEMON_BIN, DAEMON_BIN, (char *)NULL);
    _exit(127);
}
