// ============================================================================
//  WaylandCraft-BE — compositor/Globals.h
// ============================================================================
#pragma once

#include "compositor/WireProtocol.h"

namespace wlc {

class Connection;
class Compositor;

/// Installs the wl_display request handlers (sync, get_registry) on a fresh
/// connection. Registry binds are routed to the module-specific handlers.
void installDisplayHandlers(Connection& conn);

/// Sends wl_registry.global events for every interface this compositor serves
/// (called when a client binds wl_registry).
void advertiseGlobals(Connection& conn, ObjectId registryId);

namespace Globals {
/// Routes a wl_registry.bind to the owning protocol module.
bool bindGlobal(Connection& conn, ObjectId registryId, uint32_t name,
                const char* interface, uint32_t version, ObjectId newId);
/// wl_output binding (virtual output sized to the game window).
bool bindOutput(Connection& conn, const char* interface, uint32_t version, ObjectId newId);
} // namespace Globals

/// Sends the wl_output geometry/mode/done/scale/name/description burst on
/// a newly bound output, using the compositor's virtual output size.
void sendOutputState(Connection& conn, ObjectId outputId);

/// Global name constants (stable per connection as required by the spec).
namespace globals {
inline constexpr uint32_t WlCompositor    = 1;
inline constexpr uint32_t WlShm           = 2;
inline constexpr uint32_t WlSeat          = 3;
inline constexpr uint32_t WlOutput        = 4;
inline constexpr uint32_t WlSubcompositor = 5;
inline constexpr uint32_t WpDataDeviceManager = 6;
inline constexpr uint32_t XdgWmBase       = 7;
inline constexpr uint32_t WpViewporter    = 8;
inline constexpr uint32_t WpSinglePixel   = 9;
inline constexpr uint32_t WpCursorShape   = 10;

inline constexpr const char* kServerName = "waylandcraft-bedrock";
} // namespace globals

} // namespace wlc
