# Protocols — WaylandCraft-BE

## 1. Wayland wire protocol (served)

The compositor implements the standard Wayland wire format
(`docs/ref: wayland.freedesktop.org/docs/html/ch05.html`):

- header: `u32 object-id`, `u32 (size << 16 | opcode)` — little-endian,
  size includes the 8-byte header
- fixed args: signed 24.8 (`wl_fixed`)
- strings/arrays: `u32 length` + bytes + NUL, padded to 4 bytes
- fd args travel via `SCM_RIGHTS` ancillary data, **positionally aligned with
  the byte stream** (implemented in `compositor/Client.cpp` — the queue
  persists across messages until a handler consumes them)

### Objects served

| Interface | v | Notes |
| --- | --- | --- |
| `wl_display` | 1 | sync, get_registry, error, delete_id |
| `wl_registry` | 1 | 10 globals, bind routing |
| `wl_callback` | 1 | done (frame callbacks, sync) |
| `wl_compositor` | 6 | create_surface/create_region |
| `wl_surface` | 6 | attach, damage(+buffer), frame, commit, transform/scale, offset |
| `wl_shm` / `wl_shm_pool` / `wl_buffer` | 1 | memfd pools, ARGB8888 + XRGB8888 |
| `wl_seat` | 9 | capabilities, name; `wl_pointer` (enter/leave/motion/button/axis/value120/discrete/frame), `wl_keyboard` (keymap fd, enter/leave, key, modifiers, repeat_info) |
| `wl_output` | 4 | "Virtual Monitor" sized to the game window |
| `wl_subcompositor` / `wl_subsurface` | 1 | per-window surface trees |
| `xdg_wm_base`/`xdg_surface`/`xdg_toplevel`/`xdg_popup`/`xdg_positioner` | 6 | full configure/ack cycles, wm_capabilities |
| `wp_viewporter` / `wp_viewport` | 1 | src rect + dst size (upstream's viewport support) |
| `wp_single_pixel_buffer_manager_v1` | 1 | 1×1 buffers |
| `wp_cursor_shape_manager_v1` / `_device_v1` | 1 | shape → game crosshair swap |
| `wl_data_device_manager` / `_device` / `_source` / `_offer` | 3 | clipboard + DnD, payload spliced fd→fd (never touches the game) |

### Serial choreography (implicit grabs)

`wl_pointer.button` returns a serial; when an app issues
`xdg_toplevel.move/resize` or `wl_data_device.start_drag` with that serial,
`WindowRegistry` promotes the implicit grab into an exclusive grab — the
same state machine as upstream's `PointerGrabMap`.

## 2. WLCF capture protocol (companion ⇄ mod)

Transport: TCP `127.0.0.1:7232` (loopback only). All integers **little-endian**
on this transport (companion-side convenience; differs from Wayland wire
endian rules deliberately — see `CaptureService.cpp`).

```
offset  size  field
0       u32   magic = 0x574C4346 ('WLCF')
4       u32   version = 1
8       u32   msgType: 1=FRAME 2=ICON 3=HELLO 4=BYE
12      u32   payloadId (capture session id / package hash)
16      u32   width
20      u32   height
24      u32   stride (bytes per row)
28      u32   format (0 = RGBA8888)
32      u32   payloadLen
36      ...   payload (raw RGBA rows)
```

The mod ACKs each frame by echoing the header with the same `msgType`
(congestion signal for the helper). A `HELLO` opens the session; `BYE` ends it.

On reception, each FRAME becomes a **synthetic client surface**: it enters the
game through the exact same commit/frame-callback path a real Wayland client
uses (`CaptureService::frameSink` → `TextureBridge::uploadRgba`), so the
window manager treats Android app windows and Linux wayland windows
identically.

## 3. Intent socket (mod ⇄ LeviLaunchroid / companion)

Transport: abstract-namespace Unix socket `@waylandcraft-be` (same device).

```
→ "LAUNCH <package>\n"
← "OK\n"  |  "ERR\n"
```

## 4. Client ⇄ server sync (Bedrock side)

Upstream shipped two custom serverbound payloads. Bedrock has no custom
payload channel between client and server mods, so the port uses the vanilla
`scriptevent` pipeline (sent by the client mod as a `CommandRequestPacket`):

| Upstream payload | Port channel | Content |
| --- | --- | --- |
| `waylandcraft:alive_windows` | `/scriptevent wlc:alive 0x1a2b,0x3c4d` | CSV of live toplevel handles |
| `waylandcraft:give_items` | `/scriptevent wlc:give 0x1a2b` + `/wlc give` | handle list + missingOnly flag |

Server-side semantics are unchanged: owner-bound handles, 10-tick give
cooldown, per-tick validation ("invalid windows burn up").
