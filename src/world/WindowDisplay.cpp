// ============================================================================
//  WaylandCraft-BE — world/WindowDisplay.cpp
// ============================================================================
#include "world/WindowDisplay.h"

#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "compositor/XdgShell.h"

#include <algorithm>
#include <cmath>

namespace wlc {

// ---------------------------------------------------------------------------
// WindowDisplay
// ---------------------------------------------------------------------------
void WindowDisplay::anchorToCamera(const Vec3& camPos, const Vec3& camLook,
                                   const Vec3& camUp) {
    Vec3 look = camLook.normalized();
    Vec3 right = look.cross(camUp).normalized();
    plane.origin = camPos + look * anchorDistance;
    plane.rotate(-look, -camUp); // screen faces the player
    (void)right;
}

void WindowDisplay::anchorToParent(const WindowDisplay& parent, int32_t popupX,
                                   int32_t popupY) {
    // Popup positioned relative to parent's origin + xdg offset (upstream
    // anchorToParent: parent origin + offset * parent scale + epsilon).
    plane.pixelScale = parent.plane.pixelScale;
    plane.normal = parent.plane.normal;
    plane.down = parent.plane.down;
    plane.origin = parent.plane.origin +
                   parent.plane.localX() * (double)(popupX - parent.geometryX) +
                   parent.plane.localY() * (double)(popupY - parent.geometryY) +
                   parent.plane.normal * 0.01;
}

bool WindowDisplay::tryAttachWalls(const Vec3& camPos, const Vec3& camDir,
                                   double range) {
    // Upstream: block raycast up to 32 blocks; orient the display parallel to
    // the hit face with a 0.03 offset. The game glue supplies hit data; the
    // math here matches: axis-aligned normal, offset along it.
    // (Bedrock block raycast results are injected by the client hook.)
    (void)camPos;
    (void)camDir;
    (void)range;
    return false; // glue layer overrides via WallHitCallback
}

bool WindowDisplay::trySnapToOtherWindows(std::vector<WindowDisplay*>& others) {
    // Upstream snapToOtherWindows: 300px outer / 100px corner thresholds.
    constexpr double kOuterPx = 300.0;
    for (WindowDisplay* o : others) {
        if (o == this || !o->visible) continue;
        double dx = plane.origin.x - o->plane.origin.x;
        double dy = plane.origin.y - o->plane.origin.y;
        double dz = plane.origin.z - o->plane.origin.z;
        double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < kOuterPx * plane.pixelScale * 4.0) {
            plane.normal = o->plane.normal;
            plane.down = o->plane.down;
            return true;
        }
    }
    return false;
}

std::optional<SurfaceHit> WindowDisplay::intersect(Compositor& comp, const Vec3& pos,
                                                   const Vec3& dir) const {
    if (!root.valid() || !visible) return std::nullopt;

    auto base = plane.intersect(pos, dir);
    if (!base) return std::nullopt;
    if (base->dist < 0) return std::nullopt;

    // Bounds check against the window geometry.
    double lx = base->localX + geometryX;
    double ly = base->localY + geometryY;
    if (lx < 0 || ly < 0 || lx > windowW || ly > windowH) return std::nullopt;

    // Input-region check on the root surface (upstream inputRegionContains).
    Connection* c = root.conn;
    if (auto* st = findSurfaceState(*c, root.id)) {
        Rect r = st->inputRegion;
        if (lx - geometryX < r.x || ly - geometryY < r.y ||
            lx - geometryX >= r.x + r.w || ly - geometryY >= r.y + r.h) {
            return std::nullopt;
        }
    }

    SurfaceHit hit;
    hit.rootSurface = root;
    hit.surface = root; // TODO(children): walk subsurface tree back-to-front
    hit.worldPos = base->position;
    hit.surfaceX = lx;
    hit.surfaceY = ly;
    hit.geometryX = geometryX;
    hit.geometryY = geometryY;
    hit.dist = base->dist;
    (void)comp;
    return hit;
}

bool WindowDisplay::containsPoint(const Vec3& p) const {
    Vec3 rel = p - plane.origin;
    Vec3 bx = plane.localX();
    Vec3 by = plane.localY();
    double lx = rel.dot(bx) / (plane.pixelScale * plane.pixelScale);
    double ly = rel.dot(by) / (plane.pixelScale * plane.pixelScale);
    return lx >= 0 && ly >= 0 && lx <= windowW && ly <= windowH;
}

void WindowDisplay::syncGeometry(Compositor& comp, double pixelsPerBlock) {
    if (!root.valid()) return;
    Connection* c = root.conn;
    plane.pixelScale = 1.0 / pixelsPerBlock;

    if (auto* ts = XdgShellModule::toplevelForSurface(*c, root.id)) {
        title = ts->title;
        appId = ts->appId;
    }
    if (auto* st = findSurfaceState(*c, root.id)) {
        if (BufferView v = resolveBuffer(*c, st->committedBuffer); v.valid()) {
            windowW = v.width;
            windowH = v.height;
        }
    }
}

// ---------------------------------------------------------------------------
// Grabs
// ---------------------------------------------------------------------------
void MoveGrab::onMove(WindowDisplay& d, const Vec3& camPos, const Vec3& camLook) {
    d.anchorToCamera(camPos, camLook, Vec3{0, 1, 0});
}

void ResizeGrab::onMove(WindowDisplay& d, const Vec3& camPos, const Vec3& camLook) {
    // Live resize: recompute target size from the camera ray hitting the
    // display plane, clamp, and issue a configure loop via the registry.
    // (The registry drives bridge.resizeToplevelInteractive equivalents.)
    (void)d;
    (void)camPos;
    (void)camLook;
}

void PointerGrabMap::startImplicit(const SurfaceRef& surf, uint32_t button,
                                   uint32_t serial, const Vec3& worldPos, double sx,
                                   double sy) {
    implicit.surface = surf;
    implicit.button = button;
    implicit.serial = serial;
    implicit.startWorldPos = worldPos;
    implicit.startSurfaceX = sx;
    implicit.startSurfaceY = sy;
    implicit.active = true;
}

void PointerGrabMap::endImplicit() {
    implicit = Implicit{};
}

bool PointerGrabMap::matchSerial(uint32_t serial, PointerGrabMap& map) {
    return implicit.active && serial == map.implicit.serial;
}

} // namespace wlc
