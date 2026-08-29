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

### Fixed (LeviLamina 26.20 header/API alignment, second CI pass)
- **`mc/*` header relocations** (LeviLamina 26.20 restructured the Bedrock
  header tree): `mc/nbt/CompoundTag.h` → `mc/deps/nbt/CompoundTag.h`,
  `mc/player/Player.h` → `mc/world/actor/player/Player.h`,
  `mc/gui/ScreenView.h` → `mc/client/gui/screens/ScreenView.h`,
  `mc/gui/MinecraftUIRenderContext.h` → `mc/client/renderer/screen/`
  `MinecraftUIRenderContext.h`.
- **Event renames**: `PlayerLeftEvent` → `PlayerDisconnectEvent`;
  `After/BeforeUIRenderEvent` now ship in `ll/api/event/render/UIRenderEvent.h`
  with renamed accessors (`uiRenderContext()` / `screenView()`).
- **Input events**: `KeyInputEvent` exposes `keyCode()`/`isDown()` and no
  modifier state — the upstream ALT+Q hard-capture combo is now
  **G-while-captured** (G toggles none → soft → hard, ESC backs out);
  `MouseInputEvent` uses `actionButtonId()` + `buttonData()`.
- **Scheduler API**: `ll/api/scheduler/Scheduler.h` is gone in 26.20; the
  server cadence now runs on `ll::coro::keepThis` +
  `ll::thread::ServerThreadExecutor` (which the code already targeted).
- **`ItemStack` userdata** rides the constructor
  (`{name, count, aux, CompoundTag*}`); the old `setUserdata` setter is gone.
- **`MemoryOperators.cpp`** defined `LL_MEMORY_OPERATORS` without including
  `ll/api/memory/MemoryOperators.h`, so the allocator overrides were never
  compiled (heap-mismatch risk on Windows). Fixed.
- **Client-only TUs guarded**: `ClientHooks.cpp` / `HudRenderer.cpp` compile
  to empty translation units on server targets (their event headers ship in
  `src-client` only).
- **CI matrix matches upstream reality**: LeviLamina 26.20 builds on Windows
  only (its own CI is windows-latest; the `symbolprovider` dependency is
  Windows-only). The ubuntu lane became a standalone **Wayland protocol
  smoke test** job — pure POSIX C++, no packages — keeping the compositor
  core exercised on every push.
- **xmake**: adopted the official mod-template Windows configuration
  (`clang-cl` toolchain, `/EHa`, `NOMINMAX`/`UNICODE`, `MD` runtime).

### Fixed (third CI pass — link stage)
- **`Mod` ctor/dtor moved out-of-line**: the inline constructor implicitly
  instantiated `std::unique_ptr<State>`'s deleter while `State` was still
  forward-declared (MSVC `static_assert(sizeof(State))`).
- **`NativeMod::current()` is `shared_ptr` in 26.20** — dereferenced in
  `Mod::instance()` and switched to `->getDataDir()` in `Config.cpp`.
- **Self-contained `<cstdint>`** in all 10 headers using fixed-width ints
  (MSVC compiles some TUs standalone, unlike glibc-header-leaky builds).
- **Command system 26.20 API**: `CommandRegistrar::getInstance(bool)` became
  per-side `getServerInstance()` / `getClientInstance()`;
  `getOrCreateCommand` returns `CommandHandle&`; command parameter structs
  need **external linkage** (namespace scope, not function-local) for
  boost::pfr reflection under clang-cl; `WindowRegistry`/`PlatformBridge`
  includes added where previously leak-dependent.
- **Window item NBT built via `CompoundTag::fromSnbt`** (LL-exported):
  writing through `CompoundTagVariant::operator=` instantiates the recursive
  `std::_Variant_storage_` destructor chain, whose symbols the prelink
  import set does not carry (BDS ships an older STL shape). SNBT parsing
  keeps every variant internal inside LeviLamina.dll — the mod's objects
  carry zero STL-variant surface.

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
