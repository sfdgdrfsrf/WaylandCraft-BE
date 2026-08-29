// ============================================================================
//  WaylandCraft-BE — net/Sync.h
//  Client <-> server window-item channel. Upstream used two custom
//  serverbound payloads (alive_windows / give_items); Bedrock has no custom
//  payload channel between a client mod and a server mod, so the port rides
//  the vanilla scriptevent pipeline:
//
//    client:  /scriptevent wlc:alive 0x1a2b,0x3c4d     (CommandRequestPacket)
//    server:  /scriptevent wlc:give 0x1a2b
//
//  which arrive server-side as script events and are parsed back into the
//  same handle lists upstream's StreamCodec payloads carried.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wlc {

class ServerSync {
public:
    /// Installs the server-side scriptevent listeners + /wlc alive|give.
    static bool install();

    /// Handles list parser: "0x1a2b,0x3c4d" -> [6699, 15437].
    static std::vector<uint64_t> parseHandles(const std::string& csv);

    // ---- upstream ServerItemManager state ---------------------------------
    static void setAliveWindows(uint64_t playerRuntimeId, std::vector<uint64_t> handles);
    static std::vector<uint64_t> aliveWindows(uint64_t playerRuntimeId);
    /// Give cooldown per player (upstream itemGiveCooldown duck field).
    static bool tryConsumeGiveCooldown(uint64_t playerRuntimeId, int cooldownTicks);
};

class ClientSync {
public:
    /// Client target: publishes our alive-window set to the server every
    /// change (upstream WindowItemManager.syncToplevels).
    static void publishAliveWindows(const std::vector<uint64_t>& handles);
};

} // namespace wlc
