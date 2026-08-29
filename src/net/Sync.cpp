// ============================================================================
//  WaylandCraft-BE — net/Sync.cpp
//  Server-side: scriptevent handling + per-player handle bookkeeping with
//  the same GC semantics as upstream ServerItemManager (owner-bound handles,
//  10-tick give cooldown, invalid windows "burn up").
// ============================================================================
#include "net/Sync.h"

#include "Mod.h"
#include "config/Config.h"
#include "util/Log.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"  // was PlayerLeftEvent (1.21-era)

#include "mc/world/actor/player/Player.h"  // was mc/player/
#include "mc/server/ServerPlayer.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>

namespace wlc {

// ---------------------------------------------------------------------------
// Handle bookkeeping (upstream IMyServerPlayer.aliveWindows duck interface
// became a side table — Bedrock entities don't take duck interfaces).
// ---------------------------------------------------------------------------
namespace {

struct PlayerState {
    std::vector<uint64_t> aliveWindows;
    int giveCooldownTicksLeft = 0;
};

std::mutex                                     gSyncMutex;
std::map<uint64_t, PlayerState>                gPlayers;
std::vector<ll::event::ListenerPtr>            gListeners;

} // namespace

std::vector<uint64_t> ServerSync::parseHandles(const std::string& csv) {
    std::vector<uint64_t> out;
    size_t i = 0;
    while (i < csv.size()) {
        size_t comma = csv.find(',', i);
        std::string token = csv.substr(i, comma == std::string::npos ? std::string::npos
                                                                     : comma - i);
        if (!token.empty()) {
            try {
                out.push_back(std::stoull(token, nullptr, 0));
            } catch (...) {
                // malformed token — skip (upstream StreamCodec would resync)
            }
        }
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void ServerSync::setAliveWindows(uint64_t runtimeId, std::vector<uint64_t> handles) {
    std::lock_guard<std::mutex> lock(gSyncMutex);
    gPlayers[runtimeId].aliveWindows = std::move(handles);
}

std::vector<uint64_t> ServerSync::aliveWindows(uint64_t runtimeId) {
    std::lock_guard<std::mutex> lock(gSyncMutex);
    auto it = gPlayers.find(runtimeId);
    return it == gPlayers.end() ? std::vector<uint64_t>{} : it->second.aliveWindows;
}

bool ServerSync::tryConsumeGiveCooldown(uint64_t runtimeId, int cooldownTicks) {
    std::lock_guard<std::mutex> lock(gSyncMutex);
    auto& st = gPlayers[runtimeId];
    if (st.giveCooldownTicksLeft > 0) return false;
    st.giveCooldownTicksLeft = cooldownTicks;
    return true;
}

bool ServerSync::install() {
    auto& bus = ll::event::EventBus::getInstance();

    // Welcome players the way upstream prints the compositor banner.
    gListeners.push_back(bus.emplaceListener<ll::event::PlayerJoinEvent>(
        [](ll::event::PlayerJoinEvent& ev) {
            auto& player = ev.self();
            player.sendMessage("§b[WaylandCraft-BE]§r compositor online — use /wlc for help.");
        }));

    gListeners.push_back(bus.emplaceListener<ll::event::PlayerDisconnectEvent>(
        [](ll::event::PlayerDisconnectEvent& ev) {
            std::lock_guard<std::mutex> lock(gSyncMutex);
            gPlayers.erase(ev.self().getOrCreateUniqueID().rawID);
        }));

    // Cooldown tick-down mirrors upstream ServerItemManager.onStartLevelTick.
    static int tickCounter = 0;
    // (Driven by Mod::registry().update cadence in the server loop; the
    // counter runs inside WindowItem::tickAll, see item/WindowItem.cpp.)

    Log::info("server sync channel installed");
    return true;
}

} // namespace wlc
