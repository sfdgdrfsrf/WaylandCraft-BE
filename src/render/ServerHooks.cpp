// ============================================================================
//  WaylandCraft-BE — render/ServerHooks.cpp (server target)
//  Server-side lifecycle: sync channel install + periodic item GC + cooldown
//  ticking (upstream ServerTickEvents.START_LEVEL_TICK -> ServerItemManager).
// ============================================================================
#include "render/ClientHooks.h"

#include "config/Config.h"
#include "item/WindowItem.h"
#include "net/Sync.h"
#include "util/Log.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/scheduler/Scheduler.h"

#include <chrono>

namespace wlc {

namespace {
std::vector<ll::event::ListenerPtr> gListeners;
} // namespace

bool installServerHooks() {
    if (!ServerSync::install()) return false;

    // Periodic cadence — upstream ran ServerItemManager on every level tick;
    // we tick the cooldown/GC bookkeeping on the scheduler's server thread.
    ll::coro::keepThis([]() -> ll::coro::CoroTask<> {
        while (true) {
            WindowItem::tickCooldowns();
            co_await std::chrono::milliseconds(50); // one game tick
        }
    }).launch(ll::thread::ServerThreadExecutor::getDefault());

    Log::info("server hooks installed");
    return true;
}

void removeServerHooks() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gListeners) bus.removeListener(l);
    gListeners.clear();
}

} // namespace wlc
