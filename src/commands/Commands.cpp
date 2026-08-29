// ============================================================================
//  WaylandCraft-BE — commands/Commands.cpp
//  ll::command::CommandRegistrar tree:
//    /wlc launch <appId>     — open an app in the world (client/local)
//    /wlc list               — list running windows (client) / apps (server)
//    /wlc pin|unpin          — HUD video pin (upstream Pin button)
//    /wlc close              — close the focused window
//    /wlc settings <key> <v> — pixelsPerBlock / focusOnHover / terminal
//    /wlc alive <handles>    — client -> server sync channel (port of the
//                              ServerboundAliveWindowsPayload)
//    /wlc give <handle>      — port of ServerboundGiveItemsPayload
// ============================================================================
#include "commands/Commands.h"

#include "Mod.h"
#include "config/Config.h"
#include "platform/PlatformBridge.h"
#include "util/Log.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wlc {

namespace {
bool gRegistered = false;
} // namespace

bool Commands::registerAll() {
    if (gRegistered) return true;

    // 26.20: registrar instances are per-side; each target only touches its
    // own. getOrCreateCommand returns CommandHandle& (not a pointer).
#if defined(WLC_CLIENT) && !defined(WLC_SERVER)
    auto& cmd = ll::command::CommandRegistrar::getClientInstance().getOrCreateCommand(
        "wlc", "WaylandCraft-BE compositor");
#else
    auto& cmd = ll::command::CommandRegistrar::getServerInstance().getOrCreateCommand(
        "wlc", "WaylandCraft-BE compositor");
#endif

    // /wlc list
    cmd.overload().text("list").execute([](CommandOrigin const&, CommandOutput& output) {
        auto& bridge = Mod::bridge();
        if (!bridge.featuresAvailable()) {
            output.error(bridge.statusLine());
            return;
        }
        size_t n = 0;
        std::string lines;
        Mod::registry().update(Config::get().pixelsPerBlock);
        for (WindowDisplay* d : Mod::registry().displays()) {
            lines += strf("  0x%x  %s (%s)\n", (unsigned)(uintptr_t)d, d->title.c_str(),
                          d->appId.c_str());
            ++n;
        }
        output.success(strf("%zu window(s) running:\n%s", n, lines.c_str()));
    });

    // /wlc launch <appId>
    struct LaunchParams {
        std::string appId;
    };
    cmd.overload<LaunchParams>()
        .text("launch")
        .required("appId")
        .execute([](CommandOrigin const&, CommandOutput& output, LaunchParams const& p) {
            auto& bridge = Mod::bridge();
            if (!bridge.featuresAvailable()) {
                output.error("App launching is only available on Linux / Android builds.");
                return;
            }
            static std::vector<DesktopEntry> cached;
            cached = bridge.scanApps();
            for (const auto& e : cached) {
                if (e.appId == p.appId || e.name == p.appId) {
                    output.success(strf("Launching %s ...", e.name.c_str()));
                    if (bridge.launchApp(e)) return;
                    output.error("Launch failed.");
                    return;
                }
            }
            output.error(strf("No app matches '%s'. Try /wlc apps.", p.appId.c_str()));
        });

    // /wlc pin — HUD video pin (upstream WM screen "Pin")
    cmd.overload().text("pin").execute([](CommandOrigin const&, CommandOutput& output) {
        auto& reg = Mod::registry();
        WindowDisplay* d = reg.mostRecentFocused();
        if (!d) {
            output.error("No focused window to pin.");
            return;
        }
        reg.pin(d);
        output.success(strf("Pinned '%s' to the HUD.", d->title.c_str()));
    });

    cmd.overload().text("unpin").execute([](CommandOrigin const&, CommandOutput& output) {
        Mod::registry().pin(nullptr);
        output.success("HUD pin cleared.");
    });
    gRegistered = true;
    return true;
}

void Commands::unregisterAll() { gRegistered = false; }

} // namespace wlc
