# Changelog

All notable changes to WaylandCraft-BE are documented here.
Format based on Keep a Changelog; versioning is semver.
Upstream lineage: WaylandCraft v2.0.3 by EVV1E (Fabric, GPL-3.0).

## v1.0.0 — 2026-08-30

Initial release of the LeviLamina port.

### Fixed (first CI pass)
- **Windows builds failed on POSIX headers** — `CaptureService.cpp` (and every
  compositor-core TU behind it) includes `arpa/inet.h`/`sys/socket.h`, which
  MSVC does not provide. POSIX-only subsystems are now excluded from Windows
  builds and replaced by a no-op stub layer (`src/stubs/windows/`) covering
  the complete declared API of `Compositor`, `Connection`, the surface/xdg/
  seat/data-device modules, `MemFd` and `XkbKeymap` — the deliberate upstream
  parity model ("install anywhere, features on Linux only"). The stub surface
  is link-verified against the headers.
- **Missing `src/Version.h`** — `Mod.cpp` included it; the file never existed
  (only the protocol smoke test had been compiled locally, which does not
  touch the glue layer). Added with an `WLC_VERSION` fallback define.
- **ODR violation: `scoreEntry` defined in both `LinuxBridge.cpp` and
  `AndroidBridge.cpp`** — moved to `PlatformBridge.cpp` (single definition);
  bridge bodies are now guarded by their platform macros
  (`WLC_LINUX_DESKTOP && __linux__` / `WLC_ANDROID && __ANDROID__`), so
  non-matching platforms compile empty TUs instead of duplicating symbols.
- Smoke-test target restricted to POSIX platforms (removed the meaningless
  `ws2_32` Windows branch).

### Added
- **Custom Wayland compositor core** (`src/compositor/`): wire protocol
  marshaling, object lifecycle, SCM_RIGHTS fd passing; serves
  wl_compositor/wl_surface/wl_shm/wl_seat/wl_output/wl_subcompositor/
  wl_data_device_manager/xdg_wm_base family/wp_viewporter/
  wp_single_pixel_buffer/wp_cursor_shape (v-coordinates per docs/PROTOCOL.md).
- **Wire-protocol smoke test** (`tests/smoke_client.cpp`): a dependency-free
  Wayland client exercising registry → bind → shm pool (fd!) → surface →
  xdg_toplevel configure/ack → frame callbacks → WM request routing. Runs in
  CI on every push.
- **Dual-target build**: server (LL_PLAT_S) and client (LL_PLAT_C) from one
  source tree, via `--target_type=`; xmake + levibuildscript packaging with
  manifest template vars.
- **Linux desktop bridge**: XDG desktop-entry parsing (spec keys, locale-key
  tolerance), icon theme fallbacks, percent-field exec cleanup, detached
  launch with compositor env inherited.
- **Android bridge (the Linux→Android swap)**: package scan via `pm`,
  launch via `am start` + LeviLaunchroid intent socket
  (`@waylandcraft-be`), category heuristics, Termux classification.
- **CaptureService** (`src/android/`): WLCF protocol server (loopback TCP);
  companion frames enter the game as synthetic client surfaces through the
  real commit path.
- **World window math** (`src/world/`): WorldPlane raycast/basis math,
  camera anchoring, wall attach + window snap, grab state machine with
  implicit-grab serial choreography (move/resize/dnd promotion).
- **HUD renderer** (`src/render/HudRenderer.cpp`): clock, app list, HUD
  video pin, DnD icon — the four upstream elements, on AfterUIRenderEvent.
- **Window items** (`src/item/`): behavior-pack item `wlcbe:window`,
  NBT (owner uuid + handle), 10-tick give cooldown, alive-window GC with
  burn-up semantics; scriptevent sync channel `wlc:alive` / `wlc:give`.
- **Commands** `/wlc list|launch|pin|unpin|settings|alive|give`.
- **Keybinds** (client): V launcher / B window manager / G soft capture /
  ALT+Q hard capture — upstream parity.
- **XKB keymap**: compiled-in US layout served as wl_keyboard.keymap fd,
  file override at `<data>/keymap.txt`.
- **CI**: matrix builds (windows/linux × server/client), Android arm64
  experimental lane (NDK), release automation, protocol smoke test job.
- Packs: behavior + resource pack for the window item with generated
  placeholder textures.
- Docs: PORT_NOTES (class-by-class map + honest stub list), ANDROID_PORT
  (the Linux→Android guide), PROTOCOL (wire + WLCF + intent socket),
  INSTALL.

### Credits
- EVV1E — original WaylandCraft (architecture, feature design, GPL-3.0).
- LeviMC / LiteLDev — LeviLamina, LeviLauncher, LeviLaunchroid.
- The Wayland project — the protocol this compositor speaks.
