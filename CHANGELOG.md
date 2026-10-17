# Changelog

All notable changes to WaylandCraft-BE are documented here.
Format based on Keep a Changelog; versioning is semver.
Upstream lineage: WaylandCraft v2.0.3 by EVV1E (Fabric, GPL-3.0).

## v1.0.1 — 2026-10-17

### Fixed / Improved
- **Subsurface hit-testing** — `WindowDisplay::intersect` now walks the
  subsurface tree (parentId + subX/subY) back-to-front and returns the
  deepest surface under the ray instead of always the root. Removes the
  previous `TODO(children)`.
- **TextureBridge Tier B** — last RGBA frame is retained in memory (ready
  for a Tier A RenderDragon hook) and a minimal valid PNG is written for
  the resource-pack path. Draw helpers remain version-sensitive stubs
  pending MUIRC verification on LeviLauncher / LeviLaunchroid.
- Null connection guard in `WindowDisplay::intersect`.
- **Android** — `android-mod` version bumped to 1.0.1 / `1.0.1-android`.

### Docs
- PORT_NOTES and README limitations section refreshed for the above.

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

See full history in the repository for the complete v1.0.0 change list.
