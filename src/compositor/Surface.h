// ============================================================================
//  WaylandCraft-BE — compositor/Surface.h
//  wl_compositor / wl_surface / wl_shm / wl_shm_pool / wl_buffer /
//  wl_region / wl_subcompositor / wl_subsurface / wp_viewporter /
//  wp_single_pixel_buffer_manager_v1.
//
//  Mirrors upstream WLCSurface (surface tree, subsurface offsets, buffer
//  state, damage, viewport) and BufferTexture (ARGB8888/XRGB8888 shm).
// ============================================================================
#pragma once

#include "compositor/Types.h"

#include <memory>
#include <vector>

namespace wlc {

class Connection;
class Compositor;

/// Per-surface compositor state. Lives as long as the wl_surface object.
struct SurfaceState {
    enum class Role { None, Toplevel, Popup, Subsurface };

    // --- pending state (mutated by requests, committed on commit()) ------
    ObjectId pendingBuffer = 0;
    bool bufferCleared = false; // wl_surface.attach(NULL)
    std::vector<Rect> pendingSurfaceDamage;
    std::vector<Rect> pendingBufferDamage;
    std::vector<ObjectId> pendingFrameCallbacks;
    ObjectId pendingOpaqueRegion = 0;
    ObjectId pendingInputRegion = 0;
    int32_t pendingTransform = 0;
    int32_t pendingScale = 1;
    int32_t pendingOffsetX = 0, pendingOffsetY = 0; // wl_surface.offset (v5+)
    bool pendingViewportActive = false;
    double viewportSrcX = 0, viewportSrcY = 0, viewportSrcW = -1, viewportSrcH = -1;
    int32_t viewportDstW = 0, viewportDstH = 0; // 0 = none

    // --- committed state (read by the game layer) -------------------------
    ObjectId committedBuffer = 0;
    bool committedOpaque = false; // XRGB buffer attached (skip alpha blend)
    std::vector<ObjectId> committedFrameCallbacks;
    Rect inputRegion = {-0x7fffffff / 2, -0x7fffffff / 2, 0x7fffffff, 0x7fffffff};

    // --- role & tree --------------------------------------------------------
    Role role = Role::None;
    ObjectId xdgSurfaceId = 0; // xdg_surface wrapping this wl_surface
    ObjectId parentId = 0;     // subsurface parent / popup parent surface
    ObjectId siblingPrev = 0;  // stacking hint within parent (place_above/below)
    int32_t subX = 0, subY = 0; // subsurface set_position
    uint32_t lastCommitSerial = 0;

    SurfaceRef ref(Connection& c) const;
};

/// Convenience: fetch (or lazily create) a surface's state.
SurfaceState& surfaceState(Connection& conn, ObjectId surfaceId);
SurfaceState* findSurfaceState(Connection& conn, ObjectId surfaceId);

/// Resolves a committed buffer object id to an upload view.
BufferView resolveBuffer(Connection& conn, ObjectId bufferId);

namespace SurfaceModule {

// wl_surface request dispatcher (also installed by wl_compositor).
bool handleSurfaceRequests(Connection& conn, ObjectId self, uint16_t opcode, Reader& args);

// Commit: pending -> committed, notifies the game layer.
void commitSurface(Connection& conn, ObjectId surfaceId, SurfaceState& st);

// Sends frame-callback `done` + wl_buffer.release for one surface.
void presentSurfaceFrame(Connection& conn, ObjectId surfaceId);

// Bind functions (routed by Globals::bindGlobal).
bool bindCompositor(Connection& conn, const char* iface, uint32_t version, ObjectId id);
bool bindShm(Connection& conn, const char* iface, uint32_t version, ObjectId id);
bool bindSubcompositor(Connection& conn, const char* iface, uint32_t version, ObjectId id);
bool bindViewporter(Connection& conn, const char* iface, uint32_t version, ObjectId id);
bool bindSinglePixel(Connection& conn, const char* iface, uint32_t version, ObjectId id);

} // namespace SurfaceModule

} // namespace wlc
