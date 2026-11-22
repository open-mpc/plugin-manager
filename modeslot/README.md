# modeslot: a mode of your own in one of MPC's unused mode slots (proof of concept)

A small `LD_PRELOAD` library for Gen2 (aarch64) MPC. It turns the unused mode ID `0x2090` into a mode called "Plugin Manager" (the Open MPC Plugin Manager; "Open MPC" before 2026-11-22) that you can enter, see and leave. Background, addresses and the reasoning are in mpc-vst-plugins `docs/MPC_MODES.md` (device results in its `docs/NOTES.md`). The Plugin Manager's daemon draws the slot's page (`src/page.c`, DESIGN.md "The Manager in an MPC mode slot").

**Status:**
- **v1 (name only) was tried on the Live III, 2026-11-22.** It loaded. A Lua `RequestModeChange("Open MPC")` entered the slot and showed MPC's "page coming soon" picture. The mode buttons could not leave it, and only an MPC restart got out (see NOTES).
- **v2 (six hooks) works on the Live III, 2026-11-22.** "Open MPC" is a MENU tile with its icon. Tapping it shows our page, and Main or Menu leaves it. Play/Stop and the other controls work while in it.

## What it does
Six hooks. Each one checks its function's first four instructions before anything is changed.

| # | Function (3.10.1) | What the hook does |
|---|---|---|
| 1 | `getModeName(ModeID)` `0x1bed7c0` | Names `0x2090` "Plugin Manager", using MPC's own `juce::String(const char *)` (`0x108e5e4`), so MPC can address the mode by name. Every other ID runs the original code. |
| 2 | `registerMode(manager, ModeID, Mode *)` `0x1beab20` | When MPC registers Main Mode's controller object (`0x2080`), it registers the same object for `0x2090`, so the slot's pads and buttons work like Main Mode's. MPC already shares this object between eight IDs. |
| 3 | Touch UI mode change `(this, ModeID)` `0x38895cc` | Records the current screen mode (`0x2080..0x20ff`; popups are ignored). |
| 4 | `juce::ImageCache::getFromMemory(data, size)` `0x11bd880` | While the slot is the current mode, swaps MPC's placeholder picture (the 1280×800 PNG at `0x5a3b200`, 208,454 bytes, drawn by `TouchDummyPage::paint` `0x3c59b80`) for `/data/mpc-addins/modeslot/page.png`. |
| 5 | MENU icon lookup `(ModeID)` `0x3a3c4c0` | Returns `/data/mpc-addins/modeslot/icon.png` (70×70 like the stock icons) for the slot. Stock lookup gives an unknown ID an empty image. |
| 6 | `ModeMenuOrder` setup `(order, const std::vector<ModeID> &, settings)` `0x154da40` | The MENU grid is built from this list of allowed modes, and a saved name whose mode is not in it is dropped (the loader `0x154c7a0` converts names with `0x1bee920` and only moves modes already in the list). The hook passes a copy with the slot appended. |

- **Two optional files.** If `page.png` is missing, the placeholder picture stays; if `icon.png` is missing, the tile has no icon. `OMM_PAGE_PNG` and `OMM_ICON_PNG` override the paths. `make_page.py` draws a test card and an icon (`--icon`).
- **MENU tile.** The MENU grid keeps its tiles in `/data/Settings/MPC/MPC.settings` by mode name (`ModeMenuPage<p>Slot<s>`). MPC turns names back into IDs through `getModeName`, so with hook 6 a slot set to "Plugin Manager" becomes a tile (device). Edit the file only with MPC stopped, after a backup.
- **How a hook works.** The four entry instructions are copied to a trampoline page, followed by a jump back. An `adrp` among them is rewritten as a load of its absolute value; any other PC-relative instruction makes the shim refuse. The entry is overwritten with `ldr x16, #8; br x16; .quad <hook>`. This happens in the library constructor, before MPC starts any thread.
- **It takes the first free slot.** The candidates are `0x2090`, `0x2092` and `0x2099`, the three unused screen-mode IDs.
  At install it reads MPC's name switch table (`0x5386fa0`) and touch page table (`0x6712c58`) in memory, and takes the first
  candidate that both still send to their default case: no name, placeholder page. If none is free, it installs nothing.
  The log line names the slot it took. (The examples below say `0x2090`, the slot it takes on a stock 3.10.1.)
- **It stands aside if the slot is taken.** For `0x2090` hook 1 first asks MPC's own `getModeName`: stock MPC has no
  name there, so a name means MPC or another mod owns the slot. Hook 2 notices any registration of a controller for
  `0x2090` other than its own. Either way the shim sets `omm_yield`, logs `slot 0x2090 is used by something else;
  standing aside` (at the next MENU build), and every hook passes through from then on: no name, controller, page,
  icon or tile of ours. A mod that patched the entry of any of the six functions first is caught earlier: the
  instruction checks fail and the shim installs nothing.
- **It refuses (fails closed)** unless all of these hold:
  - the executable is called `MPC` and `OMM_DISABLE` is not set;
  - its GNU build-id is in the profile table;
  - every hooked function and the String constructor start with the expected instructions;
  - every address lies in the program, and the placeholder is a PNG.

  When it refuses, it writes one `modeslot: refused: ...` line to stderr (MPC's journal) and changes nothing. When it installs, it writes `modeslot: installed: ...`, including whether the page and icon loaded.
- **Supported:** one profile, the official Gen2 3.10.1, `MPC 3.10.1.12 (f088e9c8427)`, build-id `8b28179d…b837`, md5 `b24b7955…`. Every other build is refused, including 3.10.1.8.
- **The drum-pad copy** (md5 `b4283ece`) keeps the build-id. It changes only these byte ranges: `0x17b20a9..0x17b417d` (six single bytes), `0x2c549c4..0x2c549cf`, `0x2c54bb0..0x2c54bb3` and `0x7b100f0..0x7b101b2`. None of them touches the five hook sites, the placeholder PNG or its pointer. v1 loaded on it (device).

## Build and test (offline)
```
modeslot/build.sh          # src/modeslot.so (Docker arm64v8/gcc:12-bookworm; needs GLIBC_2.17 only)
modeslot/build.sh test     # aarch64 tests under qemu (Docker binfmt)
modeslot/build.sh check <MPC binary>   # the profile against a firmware binary on disk
python3 modeslot/make_page.py page.png; python3 modeslot/make_page.py --icon icon.png modeslot/addin/openmpc-logo.png
```
- **`test`** hooks stand-ins for all five functions. Each stand-in has the same kind of entry instructions; the image and icon ones include an `adrp`, like MPC's. It checks:
  - **name:** the slot name, the other names and the `x0` return value;
  - **controller:** that a registration of `0x2080` is repeated for `0x2090` with the same object, and no other registration is;
  - **mode and page:** that the current mode is recorded and popups are ignored, and that the picture is swapped only for the placeholder's exact pointer and size while the slot is current;
  - **icon:** that it goes through `getFromMemory`, and only for the slot with an icon loaded;
  - **trampolines:** the relocated `adrp` values, callee-saved registers and `sp`, and 100,000 calls through a trampoline;
  - **refusals:** that every refusal (wrong instruction at any site, a missing dependency, a non-PNG placeholder, an address outside the program) leaves all five functions unchanged;
  - **constructor gating:** an `MPC` with no build-id or an unknown one is refused, and other programs and `OMM_DISABLE=1` are silent.
- **`check`** reads the binary and confirms:
  - the build-id;
  - all five entry sequences, and that each has no PC-relative instruction except `adrp`;
  - the String constructor;
  - that the name jump table sends `0x2090` to the default case;
  - that `0x2080` is registered exactly once and `0x2090` never;
  - that the placeholder pointer leads to a whole 1280×800 PNG;
  - that the binary has no BTI property note.

  It passes on `b24b7955`.
- **Not tested offline:** MPC's real code paths.

## Live page host (v3, offline)
Hook 7 makes the slot a live page for an outside app (`src/page_host.c`; design in mpc-vst-plugins `docs/MPC_MODES.md`).
- **Picture.** While the slot is current, the app's `/run/openmpc-page/frame.png` (drawn at the size the shim reports, written whole and then renamed)
  replaces the placeholder picture. The shim decodes it with MPC's own JUCE decoder (`MemoryInputStream` `0x1053020`,
  `ImageFileFormat::findImageFormatForStream` `0x1172c88`, `decodeImage` at vtable `+0x28`). It doesn't go through
  `ImageCache`, so only the latest frame is held.
- **Touches.** They go to the app as datagrams on `/run/openmpc-page/input.sock`, one line each: `enter`, `size W H`
  (on entering, then every 2 s while shown), `down X Y`, `drag X Y`, `up X Y`, `leave`, in page pixels. **Draw frames at
  W×H** (1280×800 on a Live III: the whole screen; 1280×628 without full height): a picture of another size is scaled to fit, and
  then touches no longer line up with it (device). The shim fills the placeholder page's `mouseDown/Drag/Up`
  slots (`0x7df7140..0x7df7150`), which held JUCE's empty callback. Outside the slot they still do nothing.
- **Repaint.** The page's `paint` (`0x3c59b80`) checks for a new frame and asks for a full repaint
  (`Component::repaint()` `0x1269b80`) when there is one. While the app is active (a frame or a touch in the last 3 s), it
  asks for a 1×1 repaint at the page centre (`0x1269664`), so paint comes round again on the next refresh. All of this runs
  on MPC's message thread.
- **Keeping the page live.** An app that updates on its own, such as a progress bar, should write a frame at least every
  2–3 s while it has something to show.
- **Full height** (like Developer mode, device). Other modes keep a 110 px header band above the page; Developer has none.
  On entering, the shim stretches the page to the top of its parent with `Component::setBounds` (`0x127f5e0`), keeping
  its bottom edge, so the bottom strip stays MPC's (1280×738 on a Live III). It also moves the page to the end of its
  parent's child list, as `toFront` does, so the header band (a sibling drawn after it) no longer covers it. On leaving,
  both are put back. The page is found through the touch UI's page holder (`this + 0x6268`, whose page pointer is at
  `+0x300`), and only if the object there has `TouchDummyPage`'s vtable (`0x7df7080`). The child list is changed only
  if it holds the page exactly once. Nothing is moved from inside `paint`. The page then also covers MPC's bottom strip
  (empty in the slot, which has no tabs): the page's parent, the mode's wrapper (vtable `0x7d7fae0`, 1280×738), is
  stretched to its own parent's bottom, so the page is 1280×800 (device, Live III). `OMM_FULL_HEIGHT=1` keeps the strip
  (1280×738), `OMM_FULL_HEIGHT=0` turns full height off.
- **No app:** `page.png`, or MPC's placeholder picture.
- **`demo/pagedemo`** (`./build.sh demo`) is a test app with no libraries. It shows six buttons that light when touched, a
  cross at the last touch, the last event, and a bar that moves twice a second. Run it on the device as
  `/data/mpc-addins/modeslot/pagedemo &`. It was tried under qemu (events in, a valid PNG out), not yet on a device.

## As an addin
`package.sh 0.1.0 [out]` runs the tests, builds the library and makes `Mode-Slot-0.1.0-mpc-aarch64.zip`. The zip holds
the library, the test page and icon, and the shared addin installer (mpc-vst-plugins `tools/release/addin`, its docs/ADDINS.md; set `MPC_VST` to that checkout), and
`tools/catalog_check.py` passes on it. Its manifest is `addin/addin.manifest` (id `modeslot`).
- **Install:** `sh install.sh` from the unpacked zip copies to `/data/mpc-addins/modeslot`, adds the library to `LD_PRELOAD`
  and restarts MPC. On a Live III the drop-in is on `/run`; Addin Restore puts back every `.so` under `/data/mpc-addins` at boot.
- **Settings:** the installer does not touch `MPC.settings`. With hook 6 the slot is one of the MENU grid's allowed
  modes, so MPC is expected to place the tile itself and save its position (not yet tried on a device).
- **Uninstall:** `sh /data/mpc-addins/modeslot/uninstall.sh`. A saved "Plugin Manager" tile name then refers to a mode MPC
  does not know; the MENU loader drops names it cannot place, but this has not been tried.

## Trying it by hand on a device (ask first)
Use a temporary drop-in on `/run`. A reboot removes it, because Addin Restore does not know about it.
1. Save the project.
2. Copy `modeslot.so`, `page.png` and `icon.png` to `/data/mpc-addins/modeslot/` (each as `x.new`, then `mv`).
3. Read the current `LD_PRELOAD` with `systemctl show acvs -p Environment`. Write `/run/systemd/system/acvs.service.d/99-modeslot.conf` with `Environment="LD_PRELOAD=<current list>:/data/mpc-addins/modeslot/modeslot.so"`.
4. For a MENU tile: stop MPC, back up `MPC.settings`, add `<VALUE name="ModeMenuPage2Slot13" val="Plugin Manager"/>` (the next free slot) next to the other `ModeMenu` entries.
5. Run `systemctl daemon-reload` and start (or restart) `acvs`. Look for the `modeslot:` line with `journalctl -u acvs`.

**Undo:** remove the tile from `MPC.settings` first (MPC stopped; restore the backup). Then delete the drop-in, run `systemctl daemon-reload` and restart MPC. Without the shim, MPC does not know the name "Plugin Manager"; what it does with such a tile has not been tried.

### Device tests, in order
1. **Loads:** the journal says `installed ... page ... icon ...`, and MPC works as before.
2. **Tile:** MENU shows a "Plugin Manager" tile with the Open MPC logo.
3. **Enter:** tapping the tile shows the page.
4. **Leave:** any mode button (Main, Menu, ...) leaves the slot.
5. **Controls:** pads, Q-Links and transport while in the slot behave like Main Mode's.
6. **Other modes:** Fx Mode and the other placeholder pages are unchanged.
7. **Last mode:** save, reload and power-cycle with the slot as the last mode, with and without the shim loaded.
8. **Removed:** the undo steps give stock behaviour.

## Known risks
- **Borrowed controller:** Main Mode's object handles the slot. Anything in it that tests the current mode ID may take a "not one of mine" path.
- **Leftover bitmask test:** `0x2090` is still in a "sequencer modes" bitmask (`0x13331` at `0x16352c4`), so some sequencer behaviour may switch on in the slot.
- **A new name in saved state:** settings and projects that store mode names may keep "Plugin Manager" and then meet a build without the shim.
- **Preload order:** this library exports no symbols, so the order of preloads should not matter. Hakai's `hakai_menu_v3.so` patches menu code too, and no one has tried the two together.
- **Per-build addresses:** they change with every firmware. The build-id and instruction checks make a new build a refusal, not a crash.
