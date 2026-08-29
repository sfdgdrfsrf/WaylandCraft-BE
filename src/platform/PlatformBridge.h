// ============================================================================
//  WaylandCraft-BE — platform/PlatformBridge.h
//
//  The Linux -> Android swap. Upstream WaylandCraft talks to the *Linux
//  desktop*: XDG desktop entries, /usr/share/applications, exec, xwayland-
//  satellite, resvg icons. This port keeps the same contract but swaps the
//  backend per platform:
//
//    LinuxBridge  (WLC_LINUX_DESKTOP) — real XDG parsing + exec, real
//                 Wayland apps connect to the compositor socket.
//    AndroidBridge (WLC_ANDROID)      — Android app list via `pm`, launching
//                 via `am start` / the LeviLaunchroid intent socket, frames
//                 streamed by the capture-helper companion, Termux GUI apps
//                 via the wayland-tcp bridge.
//    NullBridge   (everything else)   — the upstream non-Linux experience:
//                 "You can install it on MacOS and Windows but you won't
//                 have any of the features."
// ============================================================================
#pragma once

#include <cstdint>

#include <memory>
#include <string>
#include <vector>

namespace wlc {

struct DesktopEntry {
    std::string appId;        // Android: package name; Linux: desktop file id
    std::string name;
    std::string genericName;
    std::string comment;
    std::vector<std::string> keywords;
    std::vector<std::string> categories; // freedesktop categories (Android: heuristics)
    std::string exec;         // Linux: Exec line; Android: component or package
    bool terminal = false;    // Linux only
    bool visible = true;
    std::string iconPath;     // resolved icon file (empty = fallback icon)
};

/// Search ranking — port of upstream AppListWidget scoring
/// (exact=3 / prefix=2 / contains=1, name x3, keywords+comment+genericName).
int scoreEntry(const DesktopEntry& entry, const std::string& query);

class PlatformBridge {
public:
    virtual ~PlatformBridge() = default;

    virtual const char* platformName() const = 0;
    /// False when the platform can't run apps at all (NullBridge).
    virtual bool featuresAvailable() const = 0;

    /// Blocking scan of installed applications (call on a background thread,
    /// exactly like upstream XDGDesktopManager).
    virtual std::vector<DesktopEntry> scanApps() = 0;

    /// Launches an app; the compositor's socket env is already prepared for
    /// child processes on Linux. Returns false on failure.
    virtual bool launchApp(const DesktopEntry& entry) = 0;

    /// Renders an icon to RGBA8888 at the requested size (returns empty on
    /// failure). Upstream: PNG natively, SVG via resvg at 128x128.
    virtual std::vector<uint8_t> renderIcon(const DesktopEntry& entry, int size) = 0;

    /// Preferred terminal command for Terminal=true entries (Linux).
    virtual std::string preferredTerminal() const { return ""; }
    virtual void setPreferredTerminal(const std::string&) {}

    /// Human-readable status for the settings screen / logs.
    virtual std::string statusLine() const = 0;
};

std::unique_ptr<PlatformBridge> makePlatformBridge();

// Per-platform factories (defined in each bridge .cpp; all linked into the
// mod so symbol resolution is static and testable).
std::unique_ptr<PlatformBridge> makePlatformBridgeForLinux();
std::unique_ptr<PlatformBridge> makePlatformBridgeForAndroid();
std::unique_ptr<PlatformBridge> makePlatformBridgeForNull();

} // namespace wlc
