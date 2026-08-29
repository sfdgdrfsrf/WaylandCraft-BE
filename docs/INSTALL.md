# Install Guide

## 1. Server (BDS + LeviLamina)

```bash
# get LeviLamina via lip
lip install github.com/LiteLDev/LeviLamina
```

1. Download the release matching your platform (Windows x64 / Linux x86_64).
2. Unpack into `bedrock-server/plugins/WaylandCraftBE/` — you should see
   `manifest.json` + the mod binary.
3. Copy `packs/waylandcraft-be_BP` into the world's
   `behavior_packs/` and register it in `world_behavior_packs.json`;
   same for the RP.
4. Start the server. Players get a compositor banner on join; `/wlc` lists
   windows.

Config lives at `plugins/WaylandCraftBE/data/config.json`
(pixelsPerBlock, focusOnHover, terminalChoice, socket/tcp ports, item rules).

## 2. Client (Windows — LeviLauncher)

1. Install [LeviLauncher](https://levilauncher.levimc.org).
2. Let it manage a Bedrock release and click "Install LeviLamina".
3. Import `WaylandCraftBE-client-windows-x64.zip` into the `mods/` list.
4. Launch — the HUD clock appears; `V` opens the app launcher.

Without Linux/Android features, this is the upstream "install anywhere,
features on Linux" experience — the compositor runs, remote/capture windows
work, local app launching does not.

## 3. Client (Android — LeviLaunchroid)

1. Install [LeviLaunchroid](https://github.com/LiteLDev/LeviLaunchroid)
   (Android 9+, ARM64).
2. Import its licensed Minecraft APK, then add the
   `WaylandCraftBE-android-arm64-client` build in the mod manager.
3. Optional pieces:
   - **capture-helper** APK (`tools/capture-helper/`) — streams native app
     windows into the game.
   - **Termux bridge** — `python tools/wayland-tcp-bridge.py` inside Termux,
     then `WAYLAND_DISPLAY=wayland-0 foot` for real Linux app windows.

## 4. Linux desktop (full upstream experience)

Build the client target natively, run it under any Bedrock Linux client
wrapping (e.g. `mcpelauncher`) or point the BDS plugin at your compositor.

Real apps connect directly:

```bash
WAYLAND_DISPLAY=waylandcraft-be-0 foot   # a terminal in your Minecraft world
WAYLAND_DISPLAY=waylandcraft-be-0 mpv --vo=wlshm video.mp4
# pin it: /wlc pin → video player on your HUD
```

## 5. Verify

- `xmake run wlc-smoke-test` — the CI smoke test: a real Wayland client
  handshake (registry → bind → shm → xdg-shell → frame callbacks) against
  your build of the compositor core.
- In game: `/wlc list` shows running windows; `/wlc launch <appId>` starts
  an app; `G` captures the keyboard; drag between two windows to move data.
