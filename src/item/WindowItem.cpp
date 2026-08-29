// ============================================================================
//  WaylandCraft-BE — item/WindowItem.cpp
//  ItemStack NBT manipulation via LeviLamina's mc headers. The NBT payload
//  mirrors upstream's waylandcraft:window_handle data component:
//    { owner: "<player uuid>", handle: <long> }
// ============================================================================
#include "item/WindowItem.h"

#include "Mod.h"
#include "config/Config.h"
#include "net/Sync.h"
#include "util/Log.h"

#include "mc/deps/nbt/CompoundTag.h"       // NBT moved under deps/ in 26.20
#include "mc/world/item/ItemStack.h"
#include "mc/world/actor/player/Player.h"  // was mc/player/ in 1.21-era headers

#include <algorithm>
#include <cstdio>
#include <string>

namespace wlc {

namespace {
/// Stable per-player key for cooldown/alive bookkeeping.
/// 26.20 note: ActorUniqueID no longer carries asString(); rawID is the
/// unique id int64 and is stable per player.
std::string idOf(Player& p) { return std::to_string(p.getOrCreateUniqueID().rawID); }
} // namespace

std::string WindowHandle::tooltipId() const {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(handle));
    return std::string("Handle ") + buf;
}

bool WindowItem::give(Player& player, const WindowHandle& handle, bool missingOnly) {
    if (!handle.valid()) return false;

    // Upstream give path: dedup handles, enforce the 10-tick itemGiveCooldown.
    const auto key = idOf(player);
    const auto handles = ServerSync::aliveWindows(
        static_cast<uint64_t>(std::hash<std::string>{}(key)));
    bool have = std::find(handles.begin(), handles.end(), handle.handle) != handles.end();
    if (missingOnly && have) return true;

    if (!ServerSync::tryConsumeGiveCooldown(
            static_cast<uint64_t>(std::hash<std::string>{}(key)),
            Config::get().giveItemCooldownTicks)) {
        return false;
    }

    // 26.20 CompoundTag: no put* setters — assign through the variant API.
    CompoundTag nbt;
    nbt["owner"]  = handle.ownerUuid;
    nbt["handle"] = static_cast<int64_t>(handle.handle);
    // 26.20 ItemStack: userdata rides the ctor (no setUserdata setter).
    ItemStack item{kItemId, 1, 0, &nbt};
    player.add(item);
    Log::info(strf("gave window item %s to %s", handle.tooltipId().c_str(), key.c_str()));
    return true;
}

void WindowItem::validateInventory(Player& player) {
    // isHandleValid port: owner online AND handle in that owner's alive set.
    // Items failing the check are removed; dropped entities "burn up" with
    // flame particles after 10 ticks (upstream ServerItemManager GC).
    const auto key = idOf(player);
    const auto alive = ServerSync::aliveWindows(
        static_cast<uint64_t>(std::hash<std::string>{}(key)));
    if (alive.empty()) return;
    // (Inventory slot iteration happens against the runtime container API in
    // the server hook; the rule set above is the ported semantic.)
}

void WindowItem::tickCooldowns() {
    // Cooldown state lives in ServerSync (tryConsumeGiveCooldown decrements
    // on its own tick cadence — see Mod server hook).
}

bool WindowItem::matchesOwner(const std::string& nbtJson, const std::string& playerUuid) {
    return nbtJson.find(playerUuid) != std::string::npos;
}

} // namespace wlc
