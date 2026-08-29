// ============================================================================
//  WaylandCraft-BE — world/WindowDisplay.h
//  Port of upstream displays/AbstractWindowDisplay + WindowDisplay: one
//  displayed window anchored in the world (or to the camera), with raycast
//  picking, surface-tree hit testing, wall attach and window snapping.
//
//  Render-side: the renderer draws this display's composited framebuffer at
//  `origin` spanning localX*bufWidth × localY*bufHeight.
// ============================================================================
#pragma once

#include <cstdint>

#include "compositor/Types.h"
#include "world/WorldPlane.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wlc {

class Compositor;
class WindowRegistry;

struct SurfaceHit {
    SurfaceRef surface;       // deepest surface under the ray
    SurfaceRef rootSurface;   // the toplevel's root surface
    Vec3 worldPos;
    double surfaceX = 0, surfaceY = 0; // surface-local px (incl. subsurface offsets)
    double geometryX = 0, geometryY = 0;
    double dist = 0;
};

/// One window placed in the world.
class WindowDisplay {
public:
    SurfaceRef root;             // wl_surface of the xdg_toplevel
    std::string title;
    std::string appId;

    WorldPlane plane;            // current placement
    double anchorDistance = 2.0; // meters from camera (0.5..20, scroll adjusts)
    int32_t windowW = 0, windowH = 0;
    int32_t geometryX = 0, geometryY = 0; // xdg window geometry offset
    bool visible = true;
    bool isPopup = false;

    // ---- placement -------------------------------------------------------
    void anchorToCamera(const Vec3& camPos, const Vec3& camLook, const Vec3& camUp);
    void anchorToParent(const WindowDisplay& parent, int32_t popupX, int32_t popupY);
    bool tryAttachWalls(const Vec3& camPos, const Vec3& camDir, double range);
    bool trySnapToOtherWindows(std::vector<WindowDisplay*>& others);

    // ---- picking ----------------------------------------------------------
    /// Raycast this display's surface tree (back-to-front, subsurfaces on
    /// top — mirrors upstream intersect()).
    std::optional<SurfaceHit> intersect(Compositor& comp, const Vec3& pos,
                                        const Vec3& dir) const;

    bool containsPoint(const Vec3& p) const;

    // ---- geometry (from compositor state each update) ---------------------
    void syncGeometry(Compositor& comp, double pixelsPerBlock);
};

// ---------------------------------------------------------------------------
// Pointer grabs — port of upstream grabs/ (PointerGrabMap, MoveGrab,
// ResizeGrab, DNDGrab, WindowGrab). The serial choreography matches
// Wayland implicit grabs exactly.
// ---------------------------------------------------------------------------
struct PointerGrab {
    virtual ~PointerGrab() = default;
    uint32_t button = 0; // evdev button that started the grab (0 = none)
    virtual const char* kind() const = 0;
    virtual void onMove(WindowDisplay& d, const Vec3& camPos, const Vec3& camLook) {}
    virtual void onScroll(WindowDisplay& d, double dy) {}
};

class MoveGrab : public PointerGrab {
public:
    const char* kind() const override { return "move"; }
    void onMove(WindowDisplay& d, const Vec3& camPos, const Vec3& camLook) override;
};

class ResizeGrab : public PointerGrab {
public:
    uint32_t edges = 0; // xdg_toplevel.resize_edge bitmask
    Vec3 startPos;
    double startW = 0, startH = 0;
    const char* kind() const override { return "resize"; }
    void onMove(WindowDisplay& d, const Vec3& camPos, const Vec3& camLook) override;
};

class DNDGrab : public PointerGrab {
public:
    const char* kind() const override { return "dnd"; }
    // hover/drop logic lives in InputRouter (needs the display list).
};

class WindowGrab : public PointerGrab {
public:
    const char* kind() const override { return "grab"; }
    void onScroll(WindowDisplay& d, double dy) override {
        d.anchorDistance = std::clamp(d.anchorDistance - dy * 0.5, 0.5, 20.0);
        d.plane.pixelScale = 1.0 / 500.0;
    }
};

/// One exclusive grab + implicit-grab tracking (upstream PointerGrabMap).
class PointerGrabMap {
public:
    std::unique_ptr<PointerGrab> exclusive;
    WindowDisplay* grabbedDisplay = nullptr;

    // Implicit grab: the pressed button + the serial the app saw, so we can
    // match xdg_toplevel.move/resize/start_drag serials (upstream
    // ImplicitGrab + dropImplicitMatching).
    struct Implicit {
        SurfaceRef surface;
        uint32_t button = 0;
        uint32_t serial = 0;
        Vec3 startWorldPos;
        double startSurfaceX = 0, startSurfaceY = 0;
        bool active = false;
    } implicit;

    void startImplicit(const SurfaceRef& surf, uint32_t button, uint32_t serial,
                       const Vec3& worldPos, double sx, double sy);
    void endImplicit();
    /// If an app requests move/resize/dnd with this serial, promote the
    /// implicit grab into `grab` (upstream dropImplicitMatching).
    bool matchSerial(uint32_t serial, PointerGrabMap& map);
};

} // namespace wlc
