// ============================================================================
//  WaylandCraft-BE — config/Config.h
//  Port of upstream WaylandCraftSettings (settings.json knobs) with the
//  Android/platform additions this port needs.
// ============================================================================
#pragma once

#include <string>

namespace wlc {

struct Config {
    int version = 1;

    // ---- upstream settings (WaylandCraftSettings) ------------------------
    int pixelsPerBlock = 500;        // "Window display pixels per block"
    bool focusOnHover = false;       // "Focus windows when hovered"
    std::string terminalChoice = ""; // "Default terminal" (Linux only)

    // ---- port settings ----------------------------------------------------
    std::string runtimeDir = "";     // default: <gamedir>/waylandcraft-be (XDG_RUNTIME_DIR)
    std::string socketName = "waylandcraft-be-0"; // $WAYLAND_DISPLAY name
    bool tcpBridgeEnabled = true;    // loopback TCP for Termux/companion bridge
    int tcpBridgePort = 7231;
    bool captureEnabled = true;      // Android capture-helper companion
    int capturePort = 7232;

    // ---- window item rules (upstream ServerItemManager constants) ---------
    int giveItemCooldownTicks = 10;  // upstream: 10-tick give cooldown
    bool burnInvalidItems = true;    // flame particles + discard

    // ---- HUD ---------------------------------------------------------------
    bool hudClock = true;            // time-date element
    bool hudAppList = true;          // taskbar app list
    bool hudVideoPin = true;         // pinned toplevel element

    // Accessors implemented in Config.cpp (ll::config reflection storage).
    static Config&       get();
    static bool          load();
    static bool          save();
};

} // namespace wlc
