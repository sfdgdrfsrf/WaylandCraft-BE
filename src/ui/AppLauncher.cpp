// ============================================================================
//  WaylandCraft-BE — ui/AppLauncher.cpp
//  Port of upstream gui/AppLauncherScreen: the app list + 14 freedesktop
//  categories + similarity ranking (exact=3/prefix=2/contains=1, name x3 —
//  see platform/PlatformBridge.h scoreEntry).
//
//  Client target: the overlay rides the same AfterUIRenderEvent draw path as
//  the HUD (V key toggles it). Server target: use /wlc launch <appId> —
//  Bedrock forms can't reach back into a client-side compositor, so the
//  launcher screen is a client feature (like upstream, where it's client-only).
// ============================================================================
#include "ui/AppLauncher.h"

#include "Mod.h"
#include "config/Config.h"
#include "platform/PlatformBridge.h"
#include "util/Log.h"

#include <algorithm>
#include <string>
#include <vector>

namespace wlc {

namespace {
std::vector<DesktopEntry> gCachedEntries;
bool                      gScanned = false;
bool                      gOverlayOpen = false;

void ensureScan() {
    if (gScanned) return;
    gCachedEntries = Mod::bridge().scanApps();
    gScanned = true;
}
} // namespace

void AppLauncher::toggle() {
    if (!Mod::bridge().featuresAvailable()) {
        Log::warn(Mod::bridge().statusLine());
        return;
    }
    ensureScan();
    gOverlayOpen = !gOverlayOpen;
    Log::info(strf("app launcher: %zu apps, overlay %s", gCachedEntries.size(),
                   gOverlayOpen ? "open" : "closed"));
    // Overlay drawing happens in render/ClientHooks.cpp while gOverlayOpen;
    // selecting a row calls AppLauncher::launch(appId).
}

bool AppLauncher::launch(const std::string& appId) {
    ensureScan();
    for (const auto& e : gCachedEntries) {
        if (e.appId == appId || e.name == appId) {
            // Upstream: bridge.execApp(appId) — WAYLAND_DISPLAY is already in
            // our env (Mod::enable), so child apps join our compositor.
            bool ok = Mod::bridge().launchApp(e);
            if (ok) Log::info(strf("launched %s", e.name.c_str()));
            return ok;
        }
    }
    Log::warn(strf("no app matches '%s'", appId.c_str()));
    return false;
}

} // namespace wlc
