// ============================================================================
//  WaylandCraft-BE — compositor/XdgShell.h
//  xdg_wm_base / xdg_positioner / xdg_surface / xdg_toplevel / xdg_popup.
//
//  Port of upstream's WLCToplevel / WLCPopup state (title, appID, request
//  queues for minimize/maximize/fullscreen/move/resize, configure cycles).
// ============================================================================
#pragma once

#include "compositor/Types.h"

#include <string>
#include <vector>

namespace wlc {

class Connection;

/// Game-facing view of one xdg_toplevel (mirrors WLCToplevel fields).
struct ToplevelState {
    ObjectId surface = 0;   // wl_surface carrying this role
    ObjectId xdgSurface = 0;
    ObjectId id = 0;        // xdg_toplevel object
    std::string title;
    std::string appId;
    // pending configure (sent to the client, awaiting ack_configure)
    int32_t pendingConfigureW = 0, pendingConfigureH = 0;
    bool hasPendingConfigure = false;
    uint32_t pendingConfigureSerial = 0;
    // current window state
    bool maximized = false;
    bool fullscreen = false;
    bool mapped = false;
    bool dead = false;
    uint32_t parent = 0; // xdg_toplevel parent object (0 = none)
};

namespace XdgShellModule {

bool bindWmBase(Connection& conn, const char* iface, uint32_t version, ObjectId id);

/// Sends xdg_toplevel.configure + xdg_surface.configure to the client
/// (the game calls this when it wants to resize/maximize/fullscreen a window;
/// mirrors upstream's toplevelResize / maximizeToplevel / fullscreenToplevel).
bool sendToplevelConfigure(Connection& conn, ObjectId toplevelId, int32_t width,
                           int32_t height, bool maximized, bool fullscreen, bool activated);
bool sendToplevelClose(Connection& conn, ObjectId toplevelId);

/// Live toplevel registry — the game layer iterates this every update()
/// (mirrors upstream's bridge.toplevels() handle array).
std::vector<ToplevelState*> toplevels(Connection& conn);
ToplevelState* findToplevel(Connection& conn, ObjectId toplevelId);
ToplevelState* toplevelForSurface(Connection& conn, ObjectId surfaceId);

} // namespace XdgShellModule

} // namespace wlc
