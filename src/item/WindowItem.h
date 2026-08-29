// ============================================================================
//  WaylandCraft-BE — item/WindowItem.h
//  Port of upstream item/: a "window" item bound to (owner UUID, handle).
//  Bedrock ships custom items via behavior packs, so the item definition
//  lives in packs/waylandcraft-be_BP/items/window.json and this code manages
//  the ItemStack NBT + lifecycle rules:
//    - give: /wlc give (10-tick cooldown, dedup — ServerboundGiveItems)
//    - validate: owner must be online AND handle alive (isHandleValid)
//    - GC: invalid items vanish; dropped entities "burn up" with flames
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace wlc {

struct WindowHandle {
    std::string ownerUuid; // player UUID (upstream: UUID player)
    uint64_t handle = 0;   // compositor toplevel id (upstream: long handle)

    bool valid() const { return handle != 0 && !ownerUuid.empty(); }
    std::string tooltipId() const; // "Handle 0x…" (upstream tooltip)
};

class WindowItem {
public:
    /// Item constants (must match packs/waylandcraft-be_BP).
    static constexpr const char* kItemId = "wlcbe:window";
    static constexpr const char* kHandleNbtKey = "wlcbe:window_handle";

    /// Gives the player a window item bound to (playerUuid, handle).
    /// Returns false on cooldown (upstream itemGiveCooldown).
    static bool give(class Player& player, const WindowHandle& handle, bool missingOnly);

    /// Validates + GCs one player's inventory (upstream per-tick validation).
    static void validateInventory(class Player& player);

    /// Cooldown tick-down (called from the server loop hook).
    static void tickCooldowns();

    /// True when the NBT carries a handle matching the given owner.
    static bool matchesOwner(const std::string& nbtJson, const std::string& playerUuid);
};

} // namespace wlc
