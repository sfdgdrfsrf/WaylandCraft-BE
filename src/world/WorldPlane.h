// ============================================================================
//  WaylandCraft-BE — world/WorldPlane.h
//  Direct C++ port of upstream math/WorldPlane: an oriented plane in world
//  space used to place windows, raycast the crosshair against them, and
//  convert between world and surface-local coordinates.
// ============================================================================
#pragma once

#include <cmath>
#include <optional>

namespace wlc {

struct Vec3 {
    double x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    double length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const {
        double l = length();
        return l > 1e-9 ? Vec3{x / l, y / l, z / l} : Vec3{};
    }
};

/// 3x3 matrix for world<->local transforms (JOML Matrix3d equivalent).
struct Mat3 {
    double m[3][3] = {};

    static Mat3 fromBasis(const Vec3& cx, const Vec3& cy, const Vec3& cz) {
        Mat3 r;
        r.m[0][0] = cx.x; r.m[0][1] = cy.x; r.m[0][2] = cz.x;
        r.m[1][0] = cx.y; r.m[1][1] = cy.y; r.m[1][2] = cz.y;
        r.m[2][0] = cx.z; r.m[2][1] = cy.z; r.m[2][2] = cz.z;
        return r;
    }
    Mat3 inverted() const {
        // Adjugate / determinant.
        double a = m[0][0], b = m[0][1], c = m[0][2];
        double d = m[1][0], e = m[1][1], f = m[1][2];
        double g = m[2][0], h = m[2][1], i = m[2][2];
        double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (std::fabs(det) < 1e-12) return {};
        double id = 1.0 / det;
        Mat3 r;
        r.m[0][0] = (e * i - f * h) * id;
        r.m[0][1] = (c * h - b * i) * id;
        r.m[0][2] = (b * f - c * e) * id;
        r.m[1][0] = (f * g - d * i) * id;
        r.m[1][1] = (a * i - c * g) * id;
        r.m[1][2] = (c * d - a * f) * id;
        r.m[2][0] = (d * h - e * g) * id;
        r.m[2][1] = (b * g - a * h) * id;
        r.m[2][2] = (a * e - b * d) * id;
        return r;
    }
    Vec3 apply(const Vec3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
};

struct PlaneHit {
    Vec3 position;   // world-space intersection
    double dist = 0; // along the ray (negative = behind)
    double localX = 0, localY = 0; // surface-local coords (pixels)
};

/// An oriented rectangle in the world. Upstream: origin + localX/localY/down
/// basis + pixelScale; spans width/height in surface pixels.
class WorldPlane {
public:
    Vec3 origin;    // top-left corner of the window in world space
    Vec3 normal;    // facing direction (out of the screen)
    Vec3 down;      // screen-down direction
    double pixelScale = 1.0 / 500.0; // world units per pixel (pixelsPerBlock=500)

    Vec3 localX() const { return normal.cross(down).normalized() * pixelScale; }
    Vec3 localY() const { return down.normalized() * pixelScale; }

    void rotate(const Vec3& newNormal, const Vec3& newDown) {
        normal = newNormal.normalized();
        down = newDown.normalized();
    }

    /// Ray-plane intersection. `pos` = ray origin (camera eye), `dir` = ray
    /// direction (camera look). Local coords are relative to `origin` minus
    /// the surface geometry offset applied by the caller.
    std::optional<PlaneHit> intersect(const Vec3& pos, const Vec3& dir) const {
        Vec3 n = normal.normalized();
        double denom = dir.dot(n);
        if (std::fabs(denom) < 1e-9) return std::nullopt;
        double t = (origin - pos).dot(n) / denom;
        Vec3 hit = pos + dir * t;

        // Project onto the plane basis.
        Vec3 rel = hit - origin;
        Vec3 bx = localX();
        Vec3 by = localY();
        double lx = rel.dot(bx) / (pixelScale * pixelScale);
        double ly = rel.dot(by) / (pixelScale * pixelScale);
        PlaneHit out;
        out.position = hit;
        out.dist = t;
        out.localX = lx;
        out.localY = ly;
        return out;
    }
};

} // namespace wlc
