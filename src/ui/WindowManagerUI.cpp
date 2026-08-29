// ============================================================================
//  WaylandCraft-BE — ui/WindowManagerUI.cpp
// ============================================================================
#include "ui/WindowManagerUI.h"

#include "Mod.h"
#include "config/Config.h"
#include "core/WindowRegistry.h"
#include "util/Log.h"

#include <string>

namespace wlc {

void WindowManagerUI::toggle() {
    auto& reg = Mod::registry();
    reg.update(Config::get().pixelsPerBlock);

    // Upstream buttons map to these operations on the focused window:
    //   Grab      -> exclusive WindowGrab (camera-anchored; scroll = distance)
    //   Resize    -> interactive resize configure loop
    //   Hide      -> display.visible = false (window keeps running)
    //   Pin       -> HUD video pin toggle
    //   Give Item -> WindowItem::give (server sync channel)
    //   Help      -> capture-mode help text
    //   Capture Mode -> soft/hard keyboard capture toggle (G / ALT+Q)
    WindowDisplay* d = reg.mostRecentFocused();
    if (!d) {
        Log::warn("no window focused — open an app first (/wlc launch)");
        return;
    }
    reg.pin(d); // default action: toggle the HUD pin, like upstream's Pin
    Log::info(strf("pinned '%s' — use /wlc unpin to release", d->title.c_str()));
}

} // namespace wlc
