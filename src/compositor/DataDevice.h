// ============================================================================
//  WaylandCraft-BE — compositor/DataDevice.h
//  wl_data_device_manager / wl_data_source / wl_data_device / wl_data_offer.
//
//  Upstream design kept: DnD payload transfer is handled *end-to-end by the
//  compositor* between the two Wayland clients; Minecraft only choreographs
//  the pointer (DNDGrab). Selection/clipboard also ride this module
//  (wl_data_device.set_selection) — the port of upstream's `ddm` module.
// ============================================================================
#pragma once

#include "compositor/Types.h"

namespace wlc {

class Connection;

namespace DataDeviceModule {

bool bindManager(Connection& conn, const char* iface, uint32_t version, ObjectId id);

/// Sends wl_data_offer enter/motion to the drag target's client.
/// Called by the game's DNDGrab (port of upstream sendDndMotion).
/// `surface` invalid => drag is over empty space (send leave semantics).
bool sendDndMotion(const SurfaceRef& surface, double sx, double sy);
/// Client released the button while over a surface (upstream dndDrop).
bool dndDrop();
/// Drag cancelled (window died / screen closed) — upstream dndCancel.
bool dndCancel();

/// Mime types advertised by the active drag source (read by the HUD/game UI).
std::vector<std::string> activeDragMimeTypes();

} // namespace DataDeviceModule

} // namespace wlc
