// ============================================================================
//  WaylandCraft-BE — core/WindowRegistry.cpp
// ============================================================================
#include "core/WindowRegistry.h"

#include "compositor/Surface.h"
#include "util/Log.h"

#include <algorithm>

namespace wlc {

void WindowRegistry::update(double pixelsPerBlock) {
    handleRequests();
    syncDisplays(pixelsPerBlock);
    comp_.update(); // present frame callbacks (the "present" side of update())
}

void WindowRegistry::syncDisplays(double pixelsPerBlock) {
    // Reconcile the compositor's toplevel set with our displays.
    std::vector<WindowDisplay*> alive;
    comp_.forEachConnection([&](Connection& c) {
        for (ToplevelState* ts : XdgShellModule::toplevels(c)) {
            if (ts->dead) continue;
            SurfaceRef root{&c, ts->surface};
            WindowDisplay* d = displayFor(root);
            if (!d) {
                owned_.push_back(std::make_unique<WindowDisplay>());
                d = owned_.back().get();
                d->root = root;
                d->anchorDistance = 2.0;
                displays_.push_back(d);
                Log::info(strf("window mapped: %s (%s)", ts->title.c_str(),
                               ts->appId.c_str()));
            }
            d->syncGeometry(comp_, pixelsPerBlock);
            alive.push_back(d);
        }
    });

    // GC displays whose windows died (upstream display GC in updateWorld).
    displays_.erase(std::remove_if(displays_.begin(), displays_.end(),
                                   [&](WindowDisplay* d) {
                                       bool dead = std::find(alive.begin(), alive.end(), d) ==
                                                   alive.end();
                                       if (dead) {
                                           if (pinned_ == d) pinned_ = nullptr;
                                           focusOrder_.erase(
                                               std::remove_if(focusOrder_.begin(),
                                                              focusOrder_.end(),
                                                              [&](const SurfaceRef& r) {
                                                                  return r == d->root;
                                                              }),
                                               focusOrder_.end());
                                       }
                                       return dead;
                                   }),
                   displays_.end());
}

void WindowRegistry::handleRequests() {
    for (const ToplevelRequest& req : comp_.drainRequests()) {
        WindowDisplay* d = displayFor(req.surface);
        switch (req.kind) {
        case ToplevelRequest::Kind::Minimize:
            if (d) d->visible = false; // HUD app-list still shows it
            break;
        case ToplevelRequest::Kind::Maximize:
            resizeToplevel(req.surface, comp_.outputWidth(), comp_.outputHeight());
            break;
        case ToplevelRequest::Kind::Unmaximize:
            resizeToplevel(req.surface, 800, 600);
            break;
        case ToplevelRequest::Kind::Fullscreen:
            fullscreenToplevel(req.surface, true);
            break;
        case ToplevelRequest::Kind::Unfullscreen:
            fullscreenToplevel(req.surface, false);
            break;
        case ToplevelRequest::Kind::Close:
            closeToplevel(req.surface);
            break;
        case ToplevelRequest::Kind::Move:
        case ToplevelRequest::Kind::Resize:
        case ToplevelRequest::Kind::DndStart:
            // Converted into exclusive grabs by the InputRouter glue
            // (upstream: implicit grab -> MoveGrab/ResizeGrab/DNDGrab).
            if (onRequestUnhandled) onRequestUnhandled(req);
            break;
        case ToplevelRequest::Kind::ShowMenu:
            if (onRequestUnhandled) onRequestUnhandled(req);
            break;
        }
    }
}

bool WindowRegistry::focusSurface(const SurfaceRef& root) {
    if (!root.valid()) return false;
    focusOrder_.erase(std::remove(focusOrder_.begin(), focusOrder_.end(), root),
                      focusOrder_.end());
    focusOrder_.insert(focusOrder_.begin(), root);
    Compositor::keyboardFocus(root);
    return true;
}

bool WindowRegistry::resizeToplevel(const SurfaceRef& root, int32_t w, int32_t h) {
    if (!root.valid()) return false;
    Connection* c = root.conn;
    ToplevelState* ts = XdgShellModule::toplevelForSurface(*c, root.id);
    if (!ts) return false;
    w = std::clamp(w, 1, 10000); // upstream clamps 1..10000
    h = std::clamp(h, 1, 10000);
    XdgShellModule::sendToplevelConfigure(*c, ts->id, w, h, ts->maximized, ts->fullscreen,
                                          true);
    return true;
}

bool WindowRegistry::maximizeToplevel(const SurfaceRef& root) {
    return resizeToplevel(root, comp_.outputWidth(), comp_.outputHeight());
}

bool WindowRegistry::fullscreenToplevel(const SurfaceRef& root, bool fullscreen) {
    if (!root.valid()) return false;
    Connection* c = root.conn;
    ToplevelState* ts = XdgShellModule::toplevelForSurface(*c, root.id);
    if (!ts) return false;
    ts->fullscreen = fullscreen;
    int32_t w = fullscreen ? comp_.outputWidth() : 800;
    int32_t h = fullscreen ? comp_.outputHeight() : 600;
    XdgShellModule::sendToplevelConfigure(*c, ts->id, w, h, ts->maximized, fullscreen, true);
    return true;
}

bool WindowRegistry::closeToplevel(const SurfaceRef& root) {
    if (!root.valid()) return false;
    Connection* c = root.conn;
    ToplevelState* ts = XdgShellModule::toplevelForSurface(*c, root.id);
    if (!ts) return false;
    return XdgShellModule::sendToplevelClose(*c, ts->id);
}

SurfaceRef WindowRegistry::dndIcon() {
    DndState& dnd = comp_.dnd();
    return dnd.active ? dnd.icon : SurfaceRef{};
}

WindowDisplay* WindowRegistry::mostRecentFocused() {
    for (const SurfaceRef& r : focusOrder_) {
        if (WindowDisplay* d = displayFor(r)) return d;
    }
    return nullptr;
}

void WindowRegistry::updateOutputSize(int32_t w, int32_t h) {
    comp_.setOutputSize(w, h);
}

WindowDisplay* WindowRegistry::displayFor(const SurfaceRef& root) {
    for (WindowDisplay* d : displays_) {
        if (d->root == root) return d;
    }
    return nullptr;
}

} // namespace wlc
