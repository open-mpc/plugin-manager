# Background daemon (Gen2 / MPC OS 3.10.x)

Runs the Plugin Manager's worker + LAN web server (`src/web.c`, port 8790) as a long-lived
process, so the browser UI (`http://<mpc>:8790`) is available whenever the device is on — not
only while the Plugin Manager VST is loaded on a track.

It reuses the plugin code verbatim: `src/daemon.c` is `main()` + signal handling around
`mgr_create(NULL)`, which already starts both threads (no VST host, no audio, no generated
headers). `web.c`'s bind-retry means the daemon and a loaded VST never fight over the port.

## Scope
**Gen2 / MPC OS 3.10.x only.** On 3.10.1 a newly installed plugin folder is picked up on the
next MPC restart with no `MPC.settings` edit, which the manager relies on. Not for Gen1/armv7.

## How it persists on Gen2 (the important part)
Gen2 has a **read-only verity rootfs** — `/` and `/etc` cannot be made writable, so **no systemd
unit can be installed to survive reboot** (confirmed on a Live III; also noted in mpc-vst-plugins
`addin-lib.sh`). The only persistent boot hook is an **MPC Lua device script** that MPC runs at
every start (the Addin Restore pattern). So the daemon is launched by:

```
/nvme/MPC Documents/Devices/OpenMPC PM Server/
    openmpc_pm_launch.ini   # device-script manifest, SIGNED (checksum gates loading)
    openmpc_pm_launch.lua   # MPC runs this at boot; package.loadlib()s the .so
    openmpc_pm_launch.so    # aarch64 lua_CFunction; spawns the daemon, once per boot
/data/openmpc-pm/openmpc-pm-daemon   # the daemon itself (persistent, exec)
```

Two non-obvious requirements, both handled:
1. **The `.ini` must be signed or MPC silently skips the script.** The checksum is
   `sha256(lua_bytes + salt)`, salt read from a **3.10.0.49** MPC binary (unchanged in 3.10.1.8);
   `forcelink/tools/sign_script.py` does it. `install.sh sign` wraps this. Only the resulting
   checksum ships — never the salt. The checksum covers the `.lua` only, so rebuilding the `.so`
   needs no re-signing.
2. **The daemon must leave MPC's cgroup** or it is killed when `acvs` restarts a few seconds into
   boot (e.g. by Addin Restore). The launcher starts it with
   `systemd-run --unit=openmpc-pm-daemon --collect` — its own transient scope — so it survives.

This is **self-contained**: no Addin Restore, nothing LD_PRELOADed into MPC. (A fallback for
users who already run the addin suite is in `boot-addin/` — an LD_PRELOAD shim restored by Addin
Restore — but it depends on Addin Restore being installed, so it is not the default.)

**Device-verified 2026-11-13 on a Live III (3.10.1.8): survives a full cold reboot, unattended,
including the addin-restore `acvs` restart.**

## Build, sign, install
```
daemon/install.sh build                 # cross-build daemon binary + launcher .so (aarch64, Docker)
daemon/install.sh sign                  # DEV ONLY: sign the .ini (needs the 3.10.0.49 MPC binary)
daemon/install.sh install <device-ip>           # deploy signed script + binary
daemon/install.sh install <device-ip> --restart # ...and restart MPC so it starts now
daemon/install.sh status  <device-ip>
daemon/install.sh verify  <device-ip>
daemon/install.sh uninstall <device-ip>
```
Signing inputs are configurable: `SIGN_MPC` (default `~/forcelink/local/MPC`, a 3.10.0.49 dump)
and `SIGN_TOOL` (default `~/forcelink/tools/sign_script.py`).

## Still to do
- A SETTINGS toggle in the plugin that runs install/uninstall from the device itself.
- Optional: respawn the daemon if its unit dies mid-session (currently launched once per boot).
