// ============================================================================
//  WaylandCraft-BE — compositor/Seat.h
//  wl_seat / wl_pointer / wl_keyboard / wp_cursor_shape.
//
//  The game's crosshair *is* the Wayland cursor (upstream design). These are
//  the injection points used by input/InputRouter.
// ============================================================================
#pragma once

#include "compositor/Types.h"

namespace wlc {

class Connection;

namespace SeatModule {

bool bindSeat(Connection& conn, const char* iface, uint32_t version, ObjectId id);
bool bindCursorShapeMgr(Connection& conn, const char* iface, uint32_t version, ObjectId id);

/// Sends a fresh keymap (XKB v1 over fd) to every bound keyboard.
/// Called after setKeymap / on keyboard bind. `keymapFd` is consumed.
void pushKeymap(Connection& conn, int keymapFd, uint32_t size);

/// Called by Compositor when a client binds wl_pointer/wl_keyboard so the
/// game can immediately deliver focus state to late joiners.
void onPointerBound(Connection& conn, ObjectId pointerId);
void onKeyboardBound(Connection& conn, ObjectId keyboardId);

} // namespace SeatModule

} // namespace wlc
