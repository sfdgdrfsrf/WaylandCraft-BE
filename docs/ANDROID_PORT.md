# Android Port Guide — "Android is Linux"

Upstream WaylandCraft ships two native libraries named
`libwaylandcraft-linux-gnu-arm64.so` and `libwaylandcraft-linux-gnu-x86_64.so`
and says: *"This mod only supports Linux! You can install it on MacOS and
Windows but you won't have any of the features."*

Android runs the **Linux kernel** — memfd, unix sockets, `SCM_RIGHTS` fd
passing, `fork`/`exec` all work — it's just a different userland. So the port
keeps every Linux mechanism and swaps the userland pieces:

## Concept mapping

| Linux concept | Android equivalent in this port |
| --- | --- |
| Desktop environment | Minecraft world (the point!) |
| Application menu | `pm list packages -3` scan (`platform/AndroidBridge.cpp`) |
| App launch (`exec`) | `am start` or the **intent socket** (`@waylandcraft-be` abstract namespace) |
| Wayland compositor socket | Same code! Unix socket at `$XDG_RUNTIME_DIR/waylandcraft-be-0` |
| Real wayland apps | **Termux**: `foot`, `weston-terminal`, `mpv --vo=wlshm` … via `tools/wayland-tcp-bridge.py` |
| App content (shm buffers) | Same wl_shm protocol; **native Android apps** stream via the capture-helper |
| Icon themes | Package icons via companion; letter tiles fallback |
| Terminal apps (`Terminal=true`) | Termux exposed as the "System" category terminal |

## Why three transports?

Android sandboxes every app into its own UID. The game process owns the
compositor socket, which other apps *cannot path-access* directly:

1. **Unix socket (in-process & shell uid)** — used by the mod's own child
   processes and any Termux context with adb-bridged access.
2. **TCP loopback bridge** — the compositor additionally listens on
   `127.0.0.1:7231` (config: `tcpBridgePort`). `tools/wayland-tcp-bridge.py`
   runs inside Termux and proxies a local Unix socket to that port, so
   `$WAYLAND_DISPLAY=wayland-0` works for Termux apps. Wayland over a TCP
   byte stream is wire-identical (it's already a byte-stream protocol); only
   fd passing is unavailable, and the compositor's shm path handles that.
3. **Capture protocol (WLCF)** — native Android apps can't "connect" as
   Wayland clients at all. Instead the capture-helper companion (different
   UID, allowed MediaProjection access) streams their window pixels into the
   mod, which *injects them as a synthetic client surface* through the same
   commit path real clients use. To the window manager they are
   indistinguishable.

## Setup on Android (LeviLaunchroid)

> **Build status (phase 1):** LeviLamina has no Android target, so the
> Android `.so` is a **preloader-android (`pl::mod`) native mod**, not an LL
> client mod. It ships the pure-POSIX core: the real Wayland compositor
> (Unix socket + TCP loopback bridge + WLCF capture service). It does NOT
> yet render windows in-game — the RenderDragon hooks, HUD and `/wlc`
> commands remain Windows/Linux-lane features (see `docs/PORT_NOTES.md`).
> Build it with `android-mod/package.sh` (NDK + CMake) or grab
> `WaylandCraftBE-android-arm64.zip` from a tagged release / Actions run.

1. Install [LeviLaunchroid](https://github.com/LiteLDev/LeviLaunchroid) (Android 9+).
2. Import the `WaylandCraftBE-android-arm64` client build into its mod manager.
3. Optional — Termux apps:
   ```
   pkg install python
   python wayland-tcp-bridge.py &
   # then inside Termux:
   WAYLAND_DISPLAY=wayland-0 foot
   # → a foot window floats in your Minecraft world
   ```
4. Optional — native Android apps: install `tools/capture-helper/`
   (`gradle :app:assembleRelease`), open it, grant MediaProjection, then
   launch any app in-game via `/wlc launch`.

## What works on Android today

- ✅ Compositor core (protocol, windows, configure cycles, focus)
- ✅ App launcher (package scan + `am start` / intent socket)
- ✅ HUD video pin (capture-helper streams → pinned window)
- ✅ Window items + server sync
- ✅ Termux wayland apps as first-class windows
- 🟡 In-world quads (awaits the RenderDragon world-render hook — see PORT_NOTES)
- 🟡 DnD *between* Termux apps (native), not yet *into* Android-native apps

## Security notes

- The TCP bridge binds **loopback only** — never expose it on the LAN
  (Wayland has no auth).
- The intent socket is abstract-namespace, `0777` semantics on the same
  device; it only accepts `LAUNCH <package>` commands.
- The capture-helper runs a foreground service with the standard
  MediaProjection consent flow.
