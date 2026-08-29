// ============================================================================
//  WaylandCraft-BE — compositor/Surface.cpp
//  wl_compositor, wl_shm, wl_shm_pool, wl_buffer, wl_region, wl_surface,
//  wl_subcompositor, wl_subsurface, wp_viewporter, wp_single_pixel_buffer.
// ============================================================================
#include "compositor/Surface.h"

#include "compositor/Client.h"
#include "compositor/Server.h"
#include "util/Log.h"
#include "util/MemFd.h"

#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace wlc {

namespace {

// ---------------------------------------------------------------------------
// Object userdata types
// ---------------------------------------------------------------------------
struct ShmPool {
    int fd = -1;
    size_t size = 0;
    void* map = nullptr;

    void ensureMap() {
        if (map || fd < 0 || size == 0) return;
        void* p = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        map = (p == MAP_FAILED) ? nullptr : p;
    }
    void resizeTo(size_t newSize) {
        if (map) {
            munmap(map, size);
            map = nullptr;
        }
        size = newSize;
        ensureMap();
    }
    ~ShmPool() {
        if (map) munmap(map, size);
        if (fd >= 0) close(fd);
    }
};

struct ShmBuffer {
    ObjectId pool = 0;
    int32_t offset = 0, width = 0, height = 0, stride = 0;
    uint32_t format = ifaces::ShmFormatArgb8888;
    bool singlePixel = false;
    uint32_t r = 0, g = 0, b = 0, a = 255;
    bool released = true; // release() pending?
};

// ---------------------------------------------------------------------------
// wl_compositor: create_surface / create_region
// ---------------------------------------------------------------------------
bool handleCompositor(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    (void)self;
    switch (opcode) {
    case 0: { // create_surface(new_id)
        auto id = args.objectId();
        if (!id) return false;
        conn.addObject(*id, ifaces::WlSurface, 6, SurfaceModule::handleSurfaceRequests);
        conn.makeUserData<SurfaceState>(*id);
        break;
    }
    case 1: { // create_region(new_id)
        auto id = args.objectId();
        if (!id) return false;
        conn.addObject(*id, ifaces::WlRegion, 1,
                       [](Connection& c, ObjectId s, uint16_t op, Reader& a) -> bool {
                           if (op == 0) { c.destroyObject(s); return true; } // destroy
                           if (op == 1 || op == 2) { // add / subtract (ignored: we
                               a.i32(); a.i32(); a.i32(); a.i32(); // accept all input)
                               return true;
                           }
                           return false;
                       });
        break;
    }
    default:
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// wl_shm / wl_shm_pool / wl_buffer
// ---------------------------------------------------------------------------
bool handleShm(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    (void)self;
    if (opcode != 0) return false; // create_pool
    auto id = args.objectId();
    if (!id) return false;
    int fd = conn.takeFd(); // fd argument (order: new_id, fd, size)
    auto size = args.i32();
    if (!id || !size || fd < 0 || *size <= 0) {
        if (fd >= 0) close(fd);
        conn.sendError(kWlDisplayId, 0, "wl_shm.create_pool: bad fd/size");
        return false;
    }
    auto pool = std::make_shared<ShmPool>();
    pool->fd = fd;
    pool->size = static_cast<size_t>(*size);
    pool->ensureMap();
    if (!pool->map) {
        conn.sendError(kWlDisplayId, 0, "wl_shm.create_pool: mmap failed");
        return false;
    }
    conn.setShared(*id, pool);
    conn.addObject(*id, ifaces::WlShmPool, 1,
                   [pool](Connection& c, ObjectId s, uint16_t op, Reader& a) -> bool {
                       switch (op) {
                       case 0: { // create_buffer(new_id, offset, w, h, stride, format)
                           auto bufId = a.objectId();
                           auto offset = a.i32();
                           auto w = a.i32();
                           auto h = a.i32();
                           auto stride = a.i32();
                           auto format = a.u32();
                           if (!bufId || !offset || !w || !h || !stride || !format) return false;
                           auto buf = std::make_shared<ShmBuffer>();
                           buf->pool = s;
                           buf->offset = *offset;
                           buf->width = *w;
                           buf->height = *h;
                           buf->stride = *stride;
                           buf->format = *format;
                           c.addObject(*bufId, ifaces::WlBuffer, 1,
                                       [buf](Connection& c2, ObjectId bs, uint16_t bop,
                                             Reader&) -> bool {
                                           if (bop == 0) { // destroy
                                               c2.destroyObject(bs);
                                               return true;
                                           }
                                           return false;
                                       });
                           c.setShared(*bufId, buf);
                           break;
                       }
                       case 1: // destroy
                           c.destroyObject(s);
                           break;
                       case 2: { // resize
                           auto sz = a.i32();
                           if (!sz || *sz <= 0) return false;
                           pool->resizeTo(static_cast<size_t>(*sz));
                           break;
                       }
                       default:
                           return false;
                       }
                       return true;
                   });
    return true;
}

// ---------------------------------------------------------------------------
// wl_surface request handler — the heart of the surface state machine.
// ---------------------------------------------------------------------------
bool handleSurface(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    SurfaceState& st = conn.makeUserData<SurfaceState>(self);
    switch (opcode) {
    case 0: // destroy
        conn.destroyObject(self);
        break;
    case 1: { // attach(buffer, x, y) — x/y must be 0 since protocol v4
        auto buf = args.objectId();
        auto x = args.i32();
        auto y = args.i32();
        if (!buf || !x || !y) return false;
        st.pendingBuffer = *buf;
        st.bufferCleared = (*buf == 0);
        if (*x != 0 || *y != 0) {
            // Only legal with wl_buffer from wl_shm at version < 4; we serve 6.
            conn.sendError(self, 2, "wl_surface.attach: non-zero offset");
            return false;
        }
        break;
    }
    case 2: { // damage (surface-local)
        auto r = args.i32(); auto g = args.i32(); auto w = args.i32(); auto h = args.i32();
        if (!r || !g || !w || !h) return false;
        st.pendingSurfaceDamage.push_back({*r, *g, *w, *h});
        break;
    }
    case 3: { // frame(new_id callback)
        auto cb = args.objectId();
        if (!cb) return false;
        conn.addObject(*cb, ifaces::WlCallback, 1, nullptr);
        st.pendingFrameCallbacks.push_back(*cb);
        break;
    }
    case 4: { // set_opaque_region(region|0)
        auto r = args.objectId();
        if (!r) return false;
        st.pendingOpaqueRegion = *r; // informational: XRGB buffers are opaque anyway
        break;
    }
    case 5: { // set_input_region(region|0)
        auto r = args.objectId();
        if (!r) return false;
        st.pendingInputRegion = *r;
        break;
    }
    case 6: // commit — snapshot pending -> committed
        SurfaceModule::commitSurface(conn, self, st);
        break;
    case 7: { // set_buffer_transform
        auto t = args.i32();
        if (!t) return false;
        st.pendingTransform = *t;
        break;
    }
    case 8: { // set_buffer_scale
        auto s = args.i32();
        if (!s) return false;
        st.pendingScale = *s;
        break;
    }
    case 9: { // damage_buffer (buffer-local)
        auto r = args.i32(); auto g = args.i32(); auto w = args.i32(); auto h = args.i32();
        if (!r || !g || !w || !h) return false;
        st.pendingBufferDamage.push_back({*r, *g, *w, *h});
        break;
    }
    case 10: { // offset (v5)
        auto x = args.i32();
        auto y = args.i32();
        if (!x || !y) return false;
        st.pendingOffsetX = *x;
        st.pendingOffsetY = *y;
        break;
    }
    default:
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// wp_viewport
// ---------------------------------------------------------------------------
bool handleViewport(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    SurfaceState* st = nullptr;
    ObjectId surface = conn.viewportOwner(self);
    if (surface) st = conn.userData<SurfaceState>(surface);
    if (!st) return false;
    switch (opcode) {
    case 0: // destroy
        conn.destroyObject(self);
        break;
    case 1: { // set_source(fixed x,y,w,h) — -1.0 means "auto"
        auto x = args.fixed(); auto y = args.fixed();
        auto w = args.fixed(); auto h = args.fixed();
        if (!x || !y || !w || !h) return false;
        st->viewportSrcX = *x; st->viewportSrcY = *y;
        st->viewportSrcW = *w; st->viewportSrcH = *h;
        st->pendingViewportActive = true;
        break;
    }
    case 2: { // set_destination(int32 w, h)
        auto w = args.i32(); auto h = args.i32();
        if (!w || !h) return false;
        st->viewportDstW = *w; st->viewportDstH = *h;
        st->pendingViewportActive = true;
        break;
    }
    default:
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// SurfaceState accessors
// ---------------------------------------------------------------------------
SurfaceState& surfaceState(Connection& conn, ObjectId surfaceId) {
    return conn.makeUserData<SurfaceState>(surfaceId);
}

SurfaceState* findSurfaceState(Connection& conn, ObjectId surfaceId) {
    return conn.userData<SurfaceState>(surfaceId);
}

BufferView resolveBuffer(Connection& conn, ObjectId bufferId) {
    BufferView view;
    if (bufferId == 0) return view;
    auto buf = conn.shared<ShmBuffer>(bufferId);
    if (!buf) return view;
    view.singlePixel = buf->singlePixel;
    if (buf->singlePixel) {
        view.width = view.height = 1;
        view.stride = 4;
        view.r = buf->r; view.g = buf->g; view.b = buf->b; view.a = buf->a;
        view.hasData = true;
        return view;
    }
    auto pool = conn.shared<ShmPool>(buf->pool);
    if (!pool || !pool->map) return view;
    size_t need = static_cast<size_t>(buf->offset) +
                  static_cast<size_t>(buf->stride) * static_cast<size_t>(buf->height);
    if (buf->width <= 0 || buf->height <= 0 || buf->stride < buf->width * 4 ||
        need > pool->size) {
        return view; // malformed buffer — treat as missing
    }
    view.data = static_cast<const uint8_t*>(pool->map) + buf->offset;
    view.mapSize = pool->size;
    view.width = buf->width;
    view.height = buf->height;
    view.stride = buf->stride;
    view.format = buf->format;
    view.hasData = true;
    return view;
}

SurfaceRef SurfaceState::ref(Connection& c) const { return SurfaceRef{&c, 0}; }

namespace SurfaceModule {

void commitSurface(Connection& conn, ObjectId surfaceId, SurfaceState& st) {
    // Move pending -> committed.
    st.committedBuffer = st.bufferCleared ? 0 : st.pendingBuffer;
    st.bufferCleared = false;
    st.pendingBuffer = 0;
    st.committedFrameCallbacks = std::move(st.pendingFrameCallbacks);
    st.pendingFrameCallbacks.clear();
    st.pendingSurfaceDamage.clear();
    st.pendingBufferDamage.clear();
    st.lastCommitSerial = conn.nextSerial();

    // Mark the attached buffer "in use" until release; track opacity for the
    // renderer's alphaBlend choice (XRGB -> opaque, mirrors upstream).
    st.committedOpaque = false;
    if (st.committedBuffer != 0) {
        if (auto buf = conn.shared<ShmBuffer>(st.committedBuffer)) {
            buf->released = false;
            st.committedOpaque =
                !buf->singlePixel && buf->format == ifaces::ShmFormatXrgb8888;
        }
    }
    // Notify the game layer that a surface committed (window mapped/resized).
    conn.compositor().onSurfaceCommitted(SurfaceRef{&conn, surfaceId});
}

bool handleSurfaceRequests(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    return handleSurface(conn, self, opcode, args);
}

// --- bind functions (called by Globals::bindGlobal) -------------------------
bool bindCompositor(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WlCompositor, std::min<uint32_t>(version, 6), handleCompositor);
    return true;
}

bool bindShm(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WlShm, std::min<uint32_t>(version, 1), handleShm);
    // Advertise the two formats upstream renders (BufferTexture.FORMAT_*).
    for (uint32_t fmt : {ifaces::ShmFormatArgb8888, ifaces::ShmFormatXrgb8888}) {
        Writer w;
        w.u32(fmt);
        conn.sendEvent(id, 0, w); // wl_shm.format
    }
    return true;
}

bool bindSubcompositor(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WlSubcompositor, std::min<uint32_t>(version, 1),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       switch (op) {
                       case 0: // destroy
                           c.destroyObject(self);
                           break;
                       case 1: { // get_subsurface(new_id, surface, main_surface)
                           auto ss = a.objectId();
                           auto surf = a.objectId();
                           auto main = a.objectId();
                           if (!ss || !surf || !main) return false;
                           SurfaceState& st = c.makeUserData<SurfaceState>(*surf);
                           if (st.role != SurfaceState::Role::None) {
                               c.sendError(self, 1, "surface already has a role");
                               return false;
                           }
                           st.role = SurfaceState::Role::Subsurface;
                           st.parentId = *main;
                           c.addObject(*ss, ifaces::WlSubsurface, 1,
                                       [surf](Connection& c2, ObjectId s2, uint16_t sop,
                                              Reader& sa) -> bool {
                                           SurfaceState& sst =
                                               c2.makeUserData<SurfaceState>(*surf);
                                           switch (sop) {
                                           case 0: // destroy
                                               c2.destroyObject(s2);
                                               sst.role = SurfaceState::Role::None;
                                               break;
                                           case 1: { // set_position
                                               auto x = sa.i32(); auto y = sa.i32();
                                               if (!x || !y) return false;
                                               sst.subX = *x;
                                               sst.subY = *y;
                                               break;
                                           }
                                           case 2: // place_above(sibling)
                                           case 3: { // place_below(sibling)
                                               auto sib = sa.objectId();
                                               if (!sib) return false;
                                               // Stacking list: children are kept in
                                               // SurfaceState.siblingPrev chain (game
                                               // walks back-to-front like upstream).
                                               sst.siblingPrev = *sib;
                                               break;
                                           }
                                           case 4: // set_sync
                                           case 5: // set_desync
                                               break;
                                           default:
                                               return false;
                                           }
                                           return true;
                                       });
                           break;
                       }
                       default:
                           return false;
                       }
                       return true;
                   });
    return true;
}

bool bindViewporter(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WpViewporter, std::min<uint32_t>(version, 1),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       switch (op) {
                       case 0: // destroy
                           c.destroyObject(self);
                           break;
                       case 1: { // get_viewport(new_id, surface)
                           auto vp = a.objectId();
                           auto surf = a.objectId();
                           if (!vp || !surf) return false;
                           c.addObject(*vp, ifaces::WpViewport, 1, handleViewport);
                           c.setViewportOwner(*vp, *surf);
                           break;
                       }
                       default:
                           return false;
                       }
                       return true;
                   });
    return true;
}

bool bindSinglePixel(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WpSinglePixelMgr, std::min<uint32_t>(version, 1),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       if (op == 0) { c.destroyObject(self); return true; } // destroy
                       if (op != 1) return false; // create_u32_rgba_buffer
                       auto bufId = a.objectId();
                       auto r = a.u32(); auto g = a.u32(); auto b = a.u32(); auto al = a.u32();
                       if (!bufId || !r || !g || !b || !al) return false;
                       auto buf = std::make_shared<ShmBuffer>();
                       buf->singlePixel = true;
                       buf->r = *r; buf->g = *g; buf->b = *b; buf->a = *al;
                       buf->released = false;
                       c.addObject(*bufId, ifaces::WpSinglePixelBuf, 1,
                                   [buf](Connection& c2, ObjectId bs, uint16_t bop,
                                         Reader&) -> bool {
                                       if (bop == 0) {
                                           c2.destroyObject(bs);
                                           return true;
                                       }
                                       return false;
                                   });
                       c.setShared(*bufId, buf);
                       return true;
                   });
    return true;
}

// --- frame callbacks (called by Compositor::presentFrameCallbacks) ----------
void presentSurfaceFrame(Connection& conn, ObjectId surfaceId) {
    auto* st = conn.userData<SurfaceState>(surfaceId);
    if (!st) return;
    uint32_t ts = static_cast<uint32_t>(::time(nullptr));
    for (ObjectId cb : st->committedFrameCallbacks) {
        Writer w;
        w.u32(ts);
        conn.sendEvent(cb, 0, w); // wl_callback.done
        conn.destroyObject(cb);
    }
    st->committedFrameCallbacks.clear();

    // Buffer release: upstream keeps one buffer in flight per surface tree;
    // a single-pool client relies on release to reuse its pool.
    if (st->committedBuffer != 0) {
        if (auto buf = conn.shared<ShmBuffer>(st->committedBuffer)) {
            if (!buf->released) {
                buf->released = true;
                Writer w;
                conn.sendEvent(st->committedBuffer, 0, w); // wl_buffer.release
            }
        }
    }
}

} // namespace SurfaceModule

} // namespace wlc

// ---------------------------------------------------------------------------
// Compositor::presentFrameCallbacks — implemented here (in the surface
// module) because it needs the per-connection wl_surface object tables.
// ---------------------------------------------------------------------------
namespace wlc {

void Compositor::presentFrameCallbacks() {
    forEachConnection([](Connection& c) {
        // Collect first: presentSurfaceFrame destroys callback objects,
        // which erases from the very map we'd otherwise be iterating.
        std::vector<ObjectId> surfaces;
        for (auto& [id, obj] : c.objectTable()) {
            (void)obj;
            SurfaceState* st = c.userData<SurfaceState>(id);
            if (st && !st->committedFrameCallbacks.empty()) surfaces.push_back(id);
        }
        for (ObjectId id : surfaces) {
            SurfaceState* st = c.userData<SurfaceState>(id);
            if (st) SurfaceModule::presentSurfaceFrame(c, id);
        }
    });
}

} // namespace wlc
