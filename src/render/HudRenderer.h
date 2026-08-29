// ============================================================================
//  WaylandCraft-BE — render/HudRenderer.h
//  Client-side HUD — the port of upstream gui/WaylandHudRenderer's four
//  elements, drawn through the LeviLamina client render event
//  (AfterUIRenderEvent -> MinecraftUIRenderContext):
//    1. time-date   top-right clock (taskbar)
//    2. app-list    running windows + capture status lines
//    3. pinned      THE video player HUD pin (top-left, 0.5x gui scale)
//    4. dnd-icon    drag ghost at screen center
// ============================================================================
#pragma once

#include <cstdint>

namespace wlc {

class HudRenderer {
public:
    /// Installs the AfterUIRenderEvent listener (client builds only).
    static bool install();

    static void remove();

    /// Latest frame upload entry point used by TextureBridge; the HUD draws
    /// the pinned window from this texture pool.
    static void onFrameUpdated(uint64_t surfaceId, int w, int h, const void* rgba,
                               int stride);
};

} // namespace wlc
