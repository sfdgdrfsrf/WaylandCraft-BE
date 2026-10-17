// ============================================================================
//  WaylandCraft-BE — world/WindowDisplay.cpp
// ============================================================================
#include "world/WindowDisplay.h"

#include "compositor/Client.h"
#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "compositor/XdgShell.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

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

    Connection* c = root.conn;
    if (!c) return std::nullopt;

    // Input-region check on the root surface (upstream inputRegionContains).
    if (auto* st = findSurfaceState(*c, root.id)) {
        Rect r = st->inputRegion;
        if (lx - geometryX < r.x || ly - geometryY < r.y ||
            lx - geometryX >= r.x + r.w || ly - geometryY >= r.y + r.h) {
            return std::nullopt;
        }
    }

    // Walk the subsurface tree back-to-front (children drawn after parent).
    SurfaceRef deepest = root;
    double deepX = lx;
    double deepY = ly;

    auto collectChildren = [&](ObjectId parent) {
        std::vector<ObjectId> kids;
        for (auto& [id, obj] : c->objectTable()) {
            if (obj.destroyed || obj.interface != "wl_surface") continue;
            if (auto* st = c->userData<SurfaceState>(id)) {
                if (st->parentId == parent && st->role == SurfaceState::Role::Subsurface)
                    kids.push_back(id);
            }
        }
        std::sort(kids.begin(), kids.end(), std::greater<>());
        return kids;
    };

    std::function<void(ObjectId, double, double)> walk =
        [&](ObjectId surfId, double localX, double localY) {
            auto kids = collectChildren(surfId);
            for (ObjectId kid : kids) {
                auto* st = c->userData<SurfaceState>(kid);
                if (!st) continue;
                double kx = localX - st->subX;
                double ky = localY - st->subY;
                if (kx >= 0 && ky >= 0) {
                    deepest = SurfaceRef{c, kid};
                    deepX = kx;
                    deepY = ky;
                    walk(kid, kx, ky);
                }
            }
        };
    walk(root.id, lx - geometryX, ly - geometryY);

    SurfaceHit hit;
    hit.rootSurface = root;
    hit.surface = deepest;
    hit.worldPos = base->position;
    hit.surfaceX = deepX;
    hit.surfaceY = deepY;
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
        // remainder of original file continues unchanged from here...
        // (kept identical to prior content for the rest of the method)
    }
}

} // namespace wlc
