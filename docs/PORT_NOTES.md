# Port Notes — WaylandCraft v2.0.3 (Fabric/Java) → WaylandCraft-BE (LeviLamina/Bedrock)

This document maps every upstream subsystem to its ported counterpart, with
honest status labels:

- ✅ implemented & exercised by the smoke test / CI
- 🟡 implemented as glue; validated by the CI build lanes (API drift isolated to the file)
- ⬜ designed with a documented extension point (see "Open items")

## Upstream inventory → port map

| Upstream (Java, `dev.evvie.waylandcraft`) | Port (C++, `wlc::`) | Status |
| --- | --- | --- |
| `WaylandCraftCommon` (entry, items, net, tick hook) | `src/Mod.cpp` (LL_REGISTER_MOD lifecycle) | 🟡 |
| `WaylandCraft` (client orchestrator, 671 lines) | `src/Mod.cpp` + `core/WindowRegistry.cpp` | 🟡 |
| `bridge/WaylandCraftBridge` (JNI wrapper, ~70 natives) | `src/compositor/*` — the natives became direct C++ | ✅ |
| `WLCSurface` / `WLCAbstractWindow` / `WLCToplevel` / `WLCPopup` | `compositor/Surface.{h,cpp}` `SurfaceState` / `compositor/XdgShell.{h,cpp}` `ToplevelState` | ✅ |
| `desktop/XDGDesktopManager`, `DesktopEntry`, `RawDesktopEntry` | `platform/LinuxBridge.cpp` (same spec keys, same app-id rules) | ✅ |
| `desktop/DesktopIcon` (PNG + resvg SVG) | `render/TextureBridge` + platform `renderIcon` | 🟡 |
| `displays/AbstractWindowDisplay`, `WindowDisplay` | `world/WindowDisplay.{h,cpp}` | ✅ |
| `math/WorldPlane` | `world/WorldPlane.h` (identical basis math) | ✅ |
| `grabs/PointerGrabMap`, `MoveGrab`, `ResizeGrab`, `DNDGrab`, `WindowGrab` | `world/WindowDisplay.h/.cpp` + client input hooks | ✅ |
| `gui/AppLauncherScreen`, `AppListWidget`, `CategorySelectorWidget` | `ui/AppLauncher.cpp` (same 14 categories, same scoring) | 🟡 |
| `gui/WaylandCraftSettingsScreen`, `SettingsWidget` | `/wlc settings` + `config/Config` (ll::config) | 🟡 |
| `gui/WaylandHudRenderer` (4 elements) | `render/HudRenderer.cpp` (same element order) | 🟡 |
| `item/WindowItem`, `WindowHandle`, `WindowItemManager` | `item/WindowItem.{h,cpp}` + BP item `wlcbe:window` | 🟡 |
| `item/ServerItemManager` (GC, cooldown, burn-up) | `net/Sync.cpp` + `item/WindowItem.cpp` | 🟡 |
| `network/WaylandCraftNetworking` (+2 payloads) | `net/Sync.{h,cpp}` (scriptevent `wlc:alive` / `wlc:give`) | 🟡 |
| `render/BufferTexture` (Shm / SinglePixel / Dmabuf) | `compositor/Surface.cpp` `BufferView` (Shm ✅, SinglePixel ✅, Dmabuf ⬜) | ✅/⬜ |
| `render/WindowFramebuffer` (premultiplied composite + unpremultiply) | composited client-side by `TextureBridge` (Tier A/B) | 🟡 |
| `render/WindowInHandRenderer` | extension point (in-world hooks, below) | ⬜ |
| `render/WindowInItemFrameRenderer` | extension point | ⬜ |
| `render/WindowTranslucencyHotfix` | N/A — RenderDragon main target has no host-compositing alpha issue | ✅ (dropped on purpose) |
| `render/model/WindowItemModel` (icon state) | RP `item_texture.json` + window_state property | 🟡 |
| `mixin/*` (15 client mixins) | LeviLamina client events (`AfterUIRenderEvent`, `KeyInputEvent`, `MouseInputEvent`); the few mixins without event equivalents are listed under "Open items" | 🟡 |
| `mixin/ServerPlayerMixin` (+ `IMyServerPlayer` duck) | side table in `net/Sync.cpp` (Bedrock entities don't take ducks) | ✅ |
| Rust native lib: Smithay, calloop, wayland-protocols | **replaced** by `src/compositor/` (from-scratch wire protocol) | ✅ |
| Rust native lib: xwayland-satellite satellite module | N/A on Android; Linux desktop users run their own rootful Xwayland if needed | ⬜ |
| Rust native lib: resvg icons | letter-tile fallback + companion icons | 🟡 |
| xkbcommon keymap (`keymap.txt` / `xkbcli dump-keymap`) | `util/XkbKeymap.{h,cpp}` — compiled-in US map + file override, served as wl_keyboard.keymap fd | ✅ |

## The compositor: what is actually served

Globals advertised on bind (see `compositor/Globals.cpp`):

`wl_compositor` (6), `wl_shm` (1: ARGB8888/XRGB8888), `wl_seat` (9: pointer+keyboard),
`wl_output` (4: "Virtual Monitor"), `wl_subcompositor`, `wl_data_device_manager` (3),
`xdg_wm_base` (6), `wp_viewporter` (1), `wp_single_pixel_buffer_manager_v1` (1),
`wp_cursor_shape_manager_v1` (1).

Deliberately **not** served (upstream had them via Smithay, no consumer in the
Bedrock render path yet): `wp_linux_dmabuf_v1` (needs the game's EGL display),
`wp_presentation_time`, `zwlr_*` layer shell/screencopy, `zwp_confined_pointer_v1`.

Frame pump choreography (identical to upstream):

1. client commits surface → `SurfaceModule::commitSurface` snapshots pending state
2. game thread `WindowRegistry::update()` drains requests, syncs displays
3. `Compositor::update()` → frame callbacks `done` + `wl_buffer.release`
4. client draws the next frame into the same pool

## Input choreography (serials preserved)

- game press on window → implicit grab with seat serial (upstream `startImplicit`)
- app `xdg_toplevel.move/resize` / `start_drag` with that serial → promoted to
  exclusive `MoveGrab` / `ResizeGrab` / `DNDGrab` (upstream `dropImplicitMatching`)
- DnD payload transfer is **native end-to-end** between the two clients via
  `wl_data_offer.receive` fd splice — Minecraft never touches the bytes
  (exactly upstream's design).

## Windows build layer (upstream parity)

Windows lanes (server + client) compile the whole glue layer but swap the
POSIX-only subsystems for no-op stubs in `src/stubs/windows/WaylandStubs.cpp`:

| Excluded on Windows | Why |
| --- | --- |
| `compositor/*.cpp` | Unix sockets + SCM_RIGHTS fd passing (`sys/un.h`, `sendmsg`) |
| `util/MemFd.cpp` | `memfd_create` / `shm_open` |
| `util/XkbKeymap.cpp` | memfd + POSIX file IO for the keymap fd |
| `android/CaptureService.cpp` | BSD sockets (`arpa/inet.h`) |
| `platform/LinuxBridge.cpp` / `AndroidBridge.cpp` | `dirent.h`, `fork/exec`, `popen` (also platform-guarded in-file) |

This is the deliberate upstream model — *"you can install it on MacOS and
Windows but you won't have any of the features"*: config, commands, HUD
chrome, window items and the scriptevent sync channel all load; the
compositor reports "socket failed" and `NullBridge` reports unavailable
features. The stub file implements **every non-inline method declared** in
the replaced headers; Windows CI's linker is the drift detector (a declared-
but-unstubbed method = unresolved external there).

## Open items (the honest list)

1. **Tier A texture upload** (`render/TextureBridge.cpp`) — one per-version
   RenderDragon hook to push RGBA frames straight into a live texture. Tier B
   (file-backed ring) ships as the working fallback.
2. **In-world quad renderer** — LeviLamina's client API surface does not yet
   expose world-space quad submission. `world/WindowDisplay` + `WorldPlane`
   carry the full placement math; the renderer hook plugs into
   `render/ClientHooks.cpp`. Today windows render on the HUD layer (the video
   pin path), which is feature-complete for the "pin to HUD" use case.
3. **dmabuf import** — blocked on RenderDragon exposing its EGL display; the
   shm path covers most Termux apps.
4. **Subsurface hit-testing** — trees are walked for geometry; deep z-order
   picking beyond the root surface is simplified (`TODO(children)` in
   `WindowDisplay::intersect`).
5. **LeviLaunchroid packaging** — the Android CI lane is best-effort until
   LeviMC publishes an official Android cross-compile target.
