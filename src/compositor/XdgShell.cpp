// ============================================================================
//  WaylandCraft-BE — compositor/XdgShell.cpp
// ============================================================================
#include "compositor/XdgShell.h"

#include "compositor/Client.h"
#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "util/Log.h"

#include <algorithm>
#include <cstring>

namespace wlc {

namespace {

// ---------------------------------------------------------------------------
// xdg_positioner — accept everything, keep the geometry for popup placement.
// ---------------------------------------------------------------------------
struct Positioner {
    int32_t width = 0, height = 0;
    Rect anchorRect;
    uint32_t anchor = 0, gravity = 0, constraintAdjustment = 0;
    int32_t offsetX = 0, offsetY = 0;
    int32_t parentWidth = 0, parentHeight = 0;
};

bool handlePositioner(Connection& c, ObjectId self, uint16_t op, Reader& a) {
    Positioner& p = c.makeUserData<Positioner>(self);
    switch (op) {
    case 0: c.destroyObject(self); break;
    case 1: { auto w = a.i32(); auto h = a.i32(); if (!w || !h) return false; p.width = *w; p.height = *h; break; }
    case 2: { auto x = a.i32(); auto y = a.i32(); auto w = a.i32(); auto h = a.i32();
              if (!x || !y || !w || !h) return false;
              p.anchorRect = {*x, *y, *w, *h}; break; }
    case 3: { auto v = a.u32(); if (!v) return false; p.anchor = *v; break; }
    case 4: { auto v = a.u32(); if (!v) return false; p.gravity = *v; break; }
    case 5: { auto v = a.u32(); if (!v) return false; p.constraintAdjustment = *v; break; }
    case 6: { auto dx = a.i32(); auto dy = a.i32(); if (!dx || !dy) return false;
              p.offsetX = *dx; p.offsetY = *dy; break; }
    case 7: break; // set_reactive
    case 8: { auto w = a.i32(); auto h = a.i32(); if (!w || !h) return false;
              p.parentWidth = *w; p.parentHeight = *h; break; }
    case 9: { auto v = a.u32(); if (!v) return false; break; } // set_parent_configure
    default: return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// xdg_toplevel requests
// ---------------------------------------------------------------------------
bool handleToplevel(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    ToplevelState* ts = conn.userData<ToplevelState>(self);
    if (!ts) return false;
    Compositor& comp = conn.compositor();

    switch (opcode) {
    case 0: // destroy
        ts->dead = true;
        conn.destroyObject(self);
        break;
    case 1: { // set_parent(toplevel|0)
        auto p = args.objectId();
        if (!p) return false;
        ts->parent = *p;
        break;
    }
    case 2: { // set_title(string)
        auto t = args.string();
        if (!t) return false;
        ts->title = *t;
        break;
    }
    case 3: { // set_app_id(string)
        auto t = args.string();
        if (!t) return false;
        ts->appId = *t;
        break;
    }
    case 4: { // show_window_menu(seat, serial, x, y)
        auto seat = args.objectId(); auto serial = args.u32();
        auto x = args.i32(); auto y = args.i32();
        if (!seat || !serial || !x || !y) return false;
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::ShowMenu;
        req.surface = SurfaceRef{&conn, ts->surface};
        req.seatRef = SurfaceRef{&conn, *seat};
        req.serial = *serial;
        req.x = *x; req.y = *y;
        comp.pushRequest(req);
        break;
    }
    case 5: { // move(seat, serial) — upstream moveRequest() queue
        auto seat = args.objectId(); auto serial = args.u32();
        if (!seat || !serial) return false;
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Move;
        req.surface = SurfaceRef{&conn, ts->surface};
        req.seatRef = SurfaceRef{&conn, *seat};
        req.serial = *serial;
        comp.pushRequest(req);
        break;
    }
    case 6: { // resize(seat, serial, edges) — upstream resizeRequest() queue
        auto seat = args.objectId(); auto serial = args.u32(); auto edges = args.u32();
        if (!seat || !serial || !edges) return false;
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Resize;
        req.surface = SurfaceRef{&conn, ts->surface};
        req.seatRef = SurfaceRef{&conn, *seat};
        req.serial = *serial;
        req.edges = *edges;
        comp.pushRequest(req);
        break;
    }
    case 7: case 8: { // set_max_size / set_min_size — informational
        break;
    }
    case 9: { // set_maximized — upstream maximizeReq() queue
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Maximize;
        req.surface = SurfaceRef{&conn, ts->surface};
        comp.pushRequest(req);
        break;
    }
    case 10: { // unset_maximized
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Unmaximize;
        req.surface = SurfaceRef{&conn, ts->surface};
        comp.pushRequest(req);
        break;
    }
    case 11: { // set_fullscreen(output|0)
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Fullscreen;
        req.surface = SurfaceRef{&conn, ts->surface};
        comp.pushRequest(req);
        break;
    }
    case 12: { // unset_fullscreen
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Unfullscreen;
        req.surface = SurfaceRef{&conn, ts->surface};
        comp.pushRequest(req);
        break;
    }
    case 13: { // set_minimized — upstream minimizeReq() queue
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::Minimize;
        req.surface = SurfaceRef{&conn, ts->surface};
        comp.pushRequest(req);
        break;
    }
    default:
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// xdg_surface requests
// ---------------------------------------------------------------------------
bool handleXdgSurface(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    Compositor& comp = conn.compositor();
    ObjectId* surfaceLink = conn.userData<ObjectId>(self);
    ObjectId surface = surfaceLink ? *surfaceLink : 0;
    if (surface == 0) return false;
    SurfaceState& st = conn.makeUserData<SurfaceState>(surface);

    switch (opcode) {
    case 0: // destroy
        conn.destroyObject(self);
        break;
    case 1: { // get_toplevel(new_id)
        auto tl = args.objectId();
        if (!tl) return false;
        if (st.role != SurfaceState::Role::None) {
            conn.sendError(self, 2, "xdg_surface: surface already has role");
            return false;
        }
        st.role = SurfaceState::Role::Toplevel;
        st.xdgSurfaceId = self;

        auto ts = std::make_shared<ToplevelState>();
        ts->surface = surface;
        ts->xdgSurface = self;
        ts->id = *tl;
        conn.addObject(*tl, ifaces::XdgToplevel, 6, handleToplevel);
        conn.setShared(*tl, ts);
        Log::debug("new xdg_toplevel mapped");
        break;
    }
    case 2: { // get_popup(new_id, parent xdg_surface, positioner)
        auto pp = args.objectId();
        auto parentXdg = args.objectId();
        auto pos = args.objectId();
        if (!pp || !parentXdg || !pos) return false;
        if (st.role != SurfaceState::Role::None) {
            conn.sendError(self, 2, "xdg_surface: surface already has role");
            return false;
        }
        st.role = SurfaceState::Role::Popup;
        st.xdgSurfaceId = self;

        // Popup parent: resolve the wl_surface under the parent xdg_surface.
        ObjectId* parentLink = conn.userData<ObjectId>(*parentXdg);
        st.parentId = parentLink ? *parentLink : 0;

        conn.addObject(*pp, ifaces::XdgPopup, 6,
                       [](Connection& c, ObjectId s, uint16_t op, Reader&) -> bool {
                           if (op == 0) { c.destroyObject(s); return true; } // destroy
                           if (op == 1) return true; // grab(seat, serial): game handles
                           if (op == 2) return true; // reposition(positioner, token)
                           return false;
                       });
        break;
    }
    case 3: { // set_window_geometry(x, y, w, h) — upstream surfaceXDGGeometry
        auto x = args.i32(); auto y = args.i32(); auto w = args.i32(); auto h = args.i32();
        if (!x || !y || !w || !h) return false;
        SurfaceState& s2 = conn.makeUserData<SurfaceState>(surface);
        (void)s2; // geometry read directly from state by the game layer
        break;
    }
    case 4: { // ack_configure(serial)
        auto serial = args.u32();
        if (!serial) return false;
        st.lastCommitSerial = *serial;
        break;
    }
    default:
        return false;
    }
    (void)comp;
    return true;
}

} // namespace

namespace XdgShellModule {

bool bindWmBase(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::XdgWmBase, std::min<uint32_t>(version, 6),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       switch (op) {
                       case 0: c.destroyObject(self); break;
                       case 1: { // create_positioner(new_id)
                           auto p = a.objectId();
                           if (!p) return false;
                           c.addObject(*p, ifaces::XdgPositioner, 6, handlePositioner);
                           break;
                       }
                       case 2: { // get_xdg_surface(new_id, wl_surface)
                           auto xdg = a.objectId();
                           auto surf = a.objectId();
                           if (!xdg || !surf) return false;
                           auto link = std::make_shared<ObjectId>(*surf);
                           c.addObject(*xdg, ifaces::XdgSurface, 6, handleXdgSurface);
                           c.setShared(*xdg, link);
                           break;
                       }
                       case 3: break; // pong(serial) — no ping implemented yet
                       default:
                           return false;
                       }
                       return true;
                   });
    return true;
}

bool sendToplevelConfigure(Connection& conn, ObjectId toplevelId, int32_t width,
                           int32_t height, bool maximized, bool fullscreen,
                           bool activated) {
    ToplevelState* ts = conn.userData<ToplevelState>(toplevelId);
    if (!ts) return false;

    // xdg_toplevel.configure(width, height, [states])
    {
        std::vector<uint32_t> states;
        if (maximized) states.push_back(ifaces::TopStateMaximized);
        if (fullscreen) states.push_back(ifaces::TopStateFullscreen);
        if (activated) states.push_back(ifaces::TopStateActivated);
        Writer w;
        w.i32(width);
        w.i32(height);
        w.array(states.data(), states.size() * sizeof(uint32_t));
        conn.sendEvent(toplevelId, 0, w);
        ts->maximized = maximized;
        ts->fullscreen = fullscreen;
    }
    // xdg_surface.configure(serial) — client must ack_configure then commit.
    uint32_t serial = conn.nextSerial();
    {
        Writer w;
        w.u32(serial);
        conn.sendEvent(ts->xdgSurface, 0, w);
    }
    ts->pendingConfigureW = width;
    ts->pendingConfigureH = height;
    ts->pendingConfigureSerial = serial;
    ts->hasPendingConfigure = true;
    return true;
}

bool sendToplevelClose(Connection& conn, ObjectId toplevelId) {
    Writer w;
    conn.sendEvent(toplevelId, 1, w); // xdg_toplevel.close
    return true;
}

std::vector<ToplevelState*> toplevels(Connection& conn) {
    std::vector<ToplevelState*> out;
    // ToplevelStates live as userdata on the xdg_toplevel object ids.
    // We scan the object table for xdg_toplevel objects (small tables, apps
    // rarely exceed a few dozen objects).
    out.reserve(8);
    for (auto& [id, obj] : conn.objectTable()) {
        (void)id;
        if (obj.interface == ifaces::XdgToplevel) {
            if (auto* ts = conn.userData<ToplevelState>(obj.id)) out.push_back(ts);
        }
    }
    return out;
}

ToplevelState* findToplevel(Connection& conn, ObjectId toplevelId) {
    return conn.userData<ToplevelState>(toplevelId);
}

ToplevelState* toplevelForSurface(Connection& conn, ObjectId surfaceId) {
    SurfaceState* st = findSurfaceState(conn, surfaceId);
    if (!st || st->role != SurfaceState::Role::Toplevel || st->xdgSurfaceId == 0) return nullptr;
    // find the xdg_toplevel object whose xdgSurface == st->xdgSurfaceId
    for (auto& [id, obj] : conn.objectTable()) {
        (void)id;
        if (obj.interface == ifaces::XdgToplevel) {
            auto* ts = conn.userData<ToplevelState>(obj.id);
            if (ts && ts->xdgSurface == st->xdgSurfaceId) return ts;
        }
    }
    return nullptr;
}

} // namespace XdgShellModule

} // namespace wlc
