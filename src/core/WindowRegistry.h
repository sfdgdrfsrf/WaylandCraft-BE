// ============================================================================
//  WaylandCraft-BE — core/WindowRegistry.h
//
//  Game-side window state — the port of upstream's WaylandCraftBridge.java
//  Java-side lists (toplevels/popups/surfaces/focusOrder) plus the update()
//  choreography from WaylandCraft.java:
//    - drain compositor request queues (move/resize/minimize/.../dnd)
//    - walk surface trees & sync geometry into WindowDisplays
//    - present frame callbacks (per rendered frame)
//    - maintain the focus MRU list (getMostToLeastRecentFocus)
//  Pure C++ (no MC headers) so the smoke test and the mod share one truth.
// ============================================================================
#pragma once

#include <cstdint>

#include "compositor/Server.h"
#include "compositor/XdgShell.h"
#include "world/WindowDisplay.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wlc {

class WindowRegistry {
public:
    explicit WindowRegistry(Compositor& comp) : comp_(comp) {}

    /// Per-frame pump (game client thread). `pixelsPerBlock` from settings.
    void update(double pixelsPerBlock);

    /// All currently displayed windows (toplevels; popups hang off parents).
    std::vector<WindowDisplay*>& displays() { return displays_; }

    /// Focus MRU (index 0 = most recent keyboard focus).
    std::vector<SurfaceRef>& focusOrder() { return focusOrder_; }

    /// Focus a window (upstream focusSurface); moves it to MRU front.
    bool focusSurface(const SurfaceRef& root);

    /// Asks an app to resize (configure loop; upstream resizeToplevel /
    /// resizeToplevelInteractive with interactive=true live behavior).
    bool resizeToplevel(const SurfaceRef& root, int32_t w, int32_t h);
    bool maximizeToplevel(const SurfaceRef& root);
    bool fullscreenToplevel(const SurfaceRef& root, bool fullscreen);
    bool closeToplevel(const SurfaceRef& root);

    /// The window currently pinned to the HUD (upstream pinnedToplevel).
    WindowDisplay* pinned() { return pinned_; }
    void pin(WindowDisplay* d) { pinned_ = d; }

    /// DnD icon surface (rendered at HUD center while a drag is active).
    SurfaceRef dndIcon();

    /// Most recent focused mapped toplevel (upstream getMostToLeastRecentFocus).
    WindowDisplay* mostRecentFocused();

    /// Implicit/exclusive grab state (used by the client input hooks).
    PointerGrabMap& grabMap() { return grabMap_; }

    /// Called by the glue when the game window resizes (virtual output).
    void updateOutputSize(int32_t w, int32_t h);

    /// Find a display by its root surface.
    WindowDisplay* displayFor(const SurfaceRef& root);

    /// Callbacks the glue installs (render/input hooks).
    std::function<void(const ToplevelRequest&)> onRequestUnhandled; // e.g. ShowMenu

private:
    void syncDisplays(double pixelsPerBlock);
    void handleRequests();

    Compositor& comp_;
    std::vector<WindowDisplay*> displays_;
    std::vector<std::unique_ptr<WindowDisplay>> owned_;
    std::vector<SurfaceRef> focusOrder_;
    PointerGrabMap grabMap_;
    WindowDisplay* pinned_ = nullptr;
};

} // namespace wlc
