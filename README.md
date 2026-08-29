# WaylandCraft-BE

**A full Wayland compositor living inside Minecraft Bedrock Edition.**
Launch apps, open windows inside your Minecraft world, drag & drop data between
them, and pin a video player to your HUD. The choice is yours.

> This is the **LeviLamina (LeviMC) port** of [EVV1E's WaylandCraft v2.0.3](https://github.com/EVV1E)
> (Fabric, Java Edition). Upstream is GPL-3.0 — so is this port. It is *not*
> affiliated with Mojang Studios, LeviMC, or EVV1E.

![platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Android%20%7C%20Windows-blue)
![license](https://img.shields.io/badge/license-GPL--3.0-green)
![levis](https://img.shields.io/badge/LeviLamina-26.20.x-purple)

---

## What it does

| Feature | Upstream (Java) | This port (Bedrock) |
| --- | --- | --- |
| Real Wayland server (`wl_surface`, `xdg_shell`, …) | Rust/Smithay via JNI | **Custom C++ implementation** (`src/compositor/`, no libwayland) |
| Launch apps from inside the game | XDG desktop entries + `exec` | Linux: same XDG path. **Android: `pm`/`am` + intent bridge** |
| Windows placed in the world | Camera-anchored quads, wall attach, snap | Same math (`world/`), rendered via client hooks |
| Drag & drop data between windows | Native wl_data_device_manager | Same protocol objects, native end-to-end transfer |
| Pin a video player to your HUD | Fabric HUD element | `AfterUIRenderEvent` HUD renderer |
| Window items (item/hand/item-frame) | Custom item + data component | Behavior-pack item + NBT handles + server GC |
| Keyboard capture (soft G / hard ALT+Q) | Fabric input mixins | Client `KeyInputEvent` forwarding, same modes |
| Crosshair = Wayland cursor | `wp_cursor_shape` + sprite swap | `wp_cursor_shape` served, HUD cursor swap |
| **Android** | — (upstream is Linux-only) | **Full adaptation: Android *is* Linux here** |

### The Android story (the "Linux → Android" swap)

Upstream says *"This mod only supports Linux!"*. Android **is** Linux —
different OS, same kernel — so this port maps every Linux concept onto its
Android equivalent:

| Linux (upstream) | Android (this port) |
| --- | --- |
| `/usr/share/applications/*.desktop` | `pm list packages -3` + category heuristics |
| `exec(Exec=…)` | `am start --user 0 -a MAIN -c LAUNCHER` (or the LeviLaunchroid **intent socket**) |
| Wayland apps connect via `$WAYLAND_DISPLAY` | **Termux GUI apps** (foot, mpv…) connect through the `wayland-tcp-bridge` |
| App window content via shm/dmabuf | **capture-helper companion** streams the app's surface (MediaProjection → WLCF protocol) |
| SVG icons via resvg | package icons via the companion, letter-tile fallback |
| Xwayland satellite | not needed — no X11 apps on Android |

Read [`docs/ANDROID_PORT.md`](docs/ANDROID_PORT.md) for the full mapping.

---

## Architecture

```
┌─────────────────────────── Minecraft Bedrock ────────────────────────────┐
│                                                                          │
│  client target (LL_PLAT_C)                server target (LL_PLAT_S)      │
│  ┌──────────────────────────┐             ┌──────────────────────────┐   │
│  │ render/HudRenderer       │             │ item/WindowItem          │   │
│  │  clock / app-list / PIN  │  scriptevent│  NBT handles, 10-tick    │   │
│  │  dnd-icon   (AfterUIRdr) │◄───────────►│  cooldown, burn-up GC    │   │
│  │ input/  crosshair cursor │  wlc:alive  │ commands/Commands        │   │
│  │ render/ClientHooks       │  wlc:give   │ net/Sync                 │   │
│  └──────────┬───────────────┘             └──────────────────────────┘   │
│             │ every frame: WindowRegistry::update()                      │
│  ┌──────────▼──────────────────────────────────────────────────────────┐ │
│  │ core/WindowRegistry   displays, focus MRU, request pump             │ │
│  │ world/                WorldPlane math, grabs, snap, raycast         │ │
│  ├──────────────────────────────────────────────────────────────────────┤ │
│  │ compositor/   ★ the Wayland server ★                                │ │
│  │  WireProtocol  wl_display/registry/callbacks  wl_surface  wl_shm     │ │
│  │  Seat (pointer/keyboard/cursor-shape)  XdgShell (toplevel/popup)     │ │
│  │  DataDevice (DnD end-to-end)  Output (virtual monitor)               │ │
│  ├──────────────────────────────────────────────────────────────────────┤ │
│  │ platform/  LinuxBridge │ AndroidBridge │ NullBridge                  │ │
│  │ android/CaptureService (companion frames)                           │ │
│  └──────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────┘
        ▲                              ▲                          ▲
        │ unix socket                  │ tcp loopback             │ tcp 7232
   real Linux apps               Termux wayland apps          capture-helper
   (foot, mpv, nautilus)      (wayland-tcp-bridge.py)        companion APK
```

The compositor is a **from-scratch implementation of the Wayland wire
protocol** (marshaling, object lifecycle, fd passing via SCM_RIGHTS, the
`wl_*` + `xdg_*` + `wp_*` object set) — the same approach as upstream's
Smithay core, in C++, and verified by a wire-protocol smoke test in CI.

## Feature matrix by platform

| | Linux desktop | **Android (LeviLaunchroid)** | Windows (LeviLauncher) |
| --- | --- | --- | --- |
| Compositor socket | ✅ unix | ✅ unix + tcp | ⚠️ tcp only |
| Local app launcher | ✅ XDG | ✅ Intents | ❌ (upstream parity: "no features") |
| Termux / real wayland apps | ✅ direct | ✅ tcp bridge | ❌ |
| Capture-service windows | ✅ | ✅ companion | ✅ remote only |
| HUD video pin | ✅ | ✅ | ✅ |
| Window items (server sync) | ✅ | ✅ | ✅ |

## Install

### Server (BDS + LeviLamina)

1. Install [LeviLamina](https://lamina.levimc.org) (`lip install github.com/LiteLDev/LeviLamina`).
2. Drop the release folder into `plugins/WaylandCraftBE/`.
3. Copy `packs/waylandcraft-be_BP` + `packs/waylandcraft-be_RP` into the world.
4. `/wlc` in game.

### Client (Windows — LeviLauncher / Android — LeviLaunchroid)

1. [LeviLauncher](https://levilauncher.levimc.org) (Windows GDK) or
   [LeviLaunchroid](https://github.com/LiteLDev/LeviLaunchroid) (Android 9+ ARM64).
2. Import the client build into `mods/`.
3. Android extras: install the capture-helper APK (in
   `tools/capture-helper/`), run `tools/wayland-tcp-bridge.py` inside Termux
   for real Linux apps.

## Build

```bash
git clone <this repo> && cd WaylandCraft-BE
# server (Windows x64 or Linux x86_64)
xmake f -y -m release --target_type=server
xmake
# client
xmake f -y -m release --target_type=client
xmake
# experimental Android arm64 client (LeviLaunchroid)
xmake f -y -p android -a arm64-v8a --ndk=$ANDROID_NDK_HOME --target_type=client --android=y
xmake

# run the Wayland protocol smoke test (Linux)
xmake f -y -p linux && xmake build wlc-smoke-test && xmake run wlc-smoke-test
```

CI builds the matrix on every push: [`.github/workflows/build.yml`](.github/workflows/build.yml).

## Commands & keybinds

| Input | Action | Upstream equivalent |
| --- | --- | --- |
| `V` | App Launcher screen | `waylandcraft.key.appLauncher` |
| `B` | Window Manager | `waylandcraft.key.windowManager` |
| `G` | Capture keyboard (soft) | `waylandcraft.key.captureKeyboard` |
| `ALT+Q` | Hard capture (forwards ESC too) | same as upstream |
| `/wlc launch <app>` | Launch an app | `bridge.execApp` |
| `/wlc list` | Running windows | HUD app-list |
| `/wlc pin` / `/wlc unpin` | HUD video pin | WM screen "Pin" |
| `/wlc settings <k> <v>` | pixelsPerBlock / focusOnHover / terminal | Settings screen |
| `/scriptevent wlc:alive <handles>` | client→server sync | `ServerboundAliveWindowsPayload` |

## Docs

- [`docs/PORT_NOTES.md`](docs/PORT_NOTES.md) — full feature-parity table, class-by-class port map, what's stubbed and why
- [`docs/ANDROID_PORT.md`](docs/ANDROID_PORT.md) — the Linux→Android adaptation guide
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) — Wayland objects served, WLCF capture protocol, intent-socket protocol
- [`docs/INSTALL.md`](docs/INSTALL.md) — step-by-step install + Termux guide

## Contributing

PRs welcome — the two big open items are in [`docs/PORT_NOTES.md`](docs/PORT_NOTES.md):
the RenderDragon **Tier A texture upload hook** (per-version) and the
in-world quad renderer hook. Everything else (protocol, compositor, window
management, server sync, items) is implemented and tested.

## License

GPL-3.0 — see [LICENSE](LICENSE). Portions ported from WaylandCraft v2.0.3 by
EVV1E (GPL-3.0). Not affiliated with Mojang Studios or LeviMC.
