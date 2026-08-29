// ============================================================================
//  WaylandCraft-BE — compositor/DataDevice.cpp
// ============================================================================
#include "compositor/DataDevice.h"

#include "compositor/Client.h"
#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "util/Log.h"

#include <algorithm>
#include <cstring>
#include <unistd.h>

namespace wlc {

namespace {

// One wl_data_source (the drag origin's payload description).
struct DataSource {
    Connection* conn = nullptr;
    ObjectId id = 0;
    std::vector<std::string> mimeTypes;
    uint32_t actions = 0;
    bool cancelled = false;
};

// An offer created on the target side for the in-flight drag/selection.
struct DataOffer {
    Connection* conn = nullptr;
    ObjectId id = 0;
    DataSource* source = nullptr; // raw: source outlives offer (same server)
    uint32_t finalAction = 0;
};

struct DeviceState {
    Connection* conn = nullptr;
    ObjectId device = 0;
    DataSource selection; // current clipboard (set_selection)
    bool hasSelection = false;
};

DataSource gDragSource;
DeviceState gDevice;
SurfaceRef gDragTarget; // current offer target (invalid = outside)
ObjectId gDragOfferId = 0;
Connection* gDragOfferConn = nullptr;

Connection* deviceConn(Compositor& comp) {
    return gDevice.conn && gDevice.conn->alive() ? gDevice.conn : nullptr;
}

void killDrag() {
    gDragSource = DataSource{};
    gDragTarget = SurfaceRef{};
    gDragOfferId = 0;
    gDragOfferConn = nullptr;
    if (Compositor* comp = Compositor::active()) {
        comp->dnd() = DndState{}; // active = false
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------
namespace {

bool handleDataSource(Connection& c, ObjectId self, uint16_t op, Reader& a) {
    DataSource* src = c.userData<DataSource>(self);
    if (!src) return false;
    switch (op) {
    case 0: { // offer(mime_type)
        auto mime = a.string();
        if (!mime) return false;
        src->mimeTypes.push_back(*mime);
        break;
    }
    case 1: // destroy
        c.destroyObject(self);
        break;
    case 2: { // set_actions(actions)
        auto act = a.u32();
        if (!act) return false;
        src->actions = *act;
        break;
    }
    default:
        return false;
    }
    return true;
}

bool handleDataOffer(Connection& c, ObjectId self, uint16_t op, Reader& a) {
    DataOffer* offer = c.userData<DataOffer>(self);
    if (!offer) return false;
    switch (op) {
    case 0: { // accept(serial, mime|none) — target accepted a mime type
        auto serial = a.u32();
        auto mime = a.string();
        (void)serial;
        (void)mime; // single-offer protocol: nothing to negotiate server-side
        break;
    }
    case 1: { // receive(mime_type, fd) — THE payload handoff. We splice the
              // fd straight to the source client's send fd: bytes never pass
              // through Minecraft, mirroring upstream's native DnD.
        auto mime = a.string();
        if (!mime) return false;
        int fd = c.takeFd();
        if (fd < 0) {
            c.sendError(self, 1, "wl_data_offer.receive: missing fd");
            return false;
        }
        DataSource* src = offer->source;
        if (src && src->conn && src->conn->alive()) {
            // wl_data_source.send(mime_type, fd)
            Writer w;
            w.string(*mime);
            w.u32(0);
            src->conn->sendEvent(src->id, 1, w, {fd});
        }
        ::close(fd); // our copy; the dup'd fd travels to the source client
        break;
    }
    case 2: // destroy
        c.destroyObject(self);
        break;
    case 3: { // finish — target accepted the drop
        if (Compositor* comp = Compositor::active()) {
            comp->dnd() = DndState{};
        }
        DataSource* src = offer->source;
        if (src && src->conn && src->conn->alive()) {
            Writer w;
            src->conn->sendEvent(src->id, 4, w); // wl_data_source.dnd_finished
        }
        killDrag();
        break;
    }
    case 4: { // set_actions(dnd_actions, preferred_action)
        auto acts = a.u32();
        auto pref = a.u32();
        if (!acts || !pref) return false;
        DataSource* src = offer->source;
        if (src && src->conn && src->conn->alive()) {
            Writer w;
            w.u32(*acts);
            src->conn->sendEvent(src->id, 5, w); // wl_data_source.action
        }
        offer->finalAction = *pref;
        break;
    }
    default:
        return false;
    }
    return true;
}

bool handleDataDevice(Connection& c, ObjectId self, uint16_t op, Reader& a) {
    DeviceState* dev = c.userData<DeviceState>(self);
    if (!dev) return false;
    switch (op) {
    case 0: { // start_drag(source|0, origin, icon|0, serial) — DnD begins!
        auto srcId = a.objectId();
        auto origin = a.objectId();
        auto icon = a.objectId();
        auto serial = a.u32();
        if (!srcId || !origin || !icon || !serial) return false;

        DataSource* src = srcId ? c.userData<DataSource>(*srcId) : nullptr;
        gDragSource = src ? *src : DataSource{&c, 0, {}, 0, false};
        gDragSource.conn = &c;

        DndState dnd;
        dnd.active = true;
        dnd.serial = *serial;
        dnd.source = SurfaceRef{&c, *srcId};
        dnd.origin = SurfaceRef{&c, *origin};
        dnd.icon = SurfaceRef{&c, *icon};
        for (const auto& m : gDragSource.mimeTypes) dnd.mimeTypes.push_back(m);
        c.compositor().dnd() = dnd;

        // Route through the game's grab system (implicit-grab serial match).
        ToplevelRequest req;
        req.kind = ToplevelRequest::Kind::DndStart;
        req.surface = dnd.origin;
        req.source = dnd.source;
        req.icon = dnd.icon;
        req.serial = *serial;
        c.compositor().pushRequest(req);
        Log::info(strf("dnd started: %zu mime types", dnd.mimeTypes.size()));
        break;
    }
    case 1: { // set_selection(source|0, serial) — clipboard
        auto srcId = a.objectId();
        auto serial = a.u32();
        if (!srcId || !serial) return false;
        if (*srcId == 0) {
            dev->hasSelection = false;
            dev->selection = DataSource{};
            return true;
        }
        DataSource* src = c.userData<DataSource>(*srcId);
        if (!src) return false;
        dev->selection = *src;
        dev->hasSelection = true;
        break;
    }
    case 2: // release (v2)
        c.destroyObject(self);
        break;
    case 3: break; // set_primary_selection (v2) — accepted, unused
    default:
        return false;
    }
    return true;
}

} // namespace

bool DataDeviceModule::bindManager(Connection& conn, const char* iface, uint32_t version,
                                   ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WlDataDeviceManager, std::min<uint32_t>(version, 3),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       switch (op) {
                       case 0: { // create_data_source(new_id)
                           auto src = a.objectId();
                           if (!src) return false;
                           c.addObject(*src, ifaces::WlDataSource, 3, handleDataSource);
                           DataSource& dsRef = c.makeUserData<DataSource>(*src);
                           DataSource* ds = &dsRef;
                           ds->conn = &c;
                           ds->id = *src;
                           break;
                       }
                       case 1: { // get_data_device(new_id, seat)
                           auto dev = a.objectId();
                           if (!dev) return false;
                           c.addObject(*dev, ifaces::WlDataDevice, 3, handleDataDevice);
                           DeviceState& dsRef = c.makeUserData<DeviceState>(*dev);
                           dsRef.conn = &c;
                           dsRef.device = *dev;
                           DeviceState* ds = &dsRef;
                           gDevice.conn = &c;
                           gDevice.device = *dev;
                           break;
                       }
                       default:
                           return false;
                       }
                       (void)self;
                       return true;
                   });
    return true;
}

// ---------------------------------------------------------------------------
// Game-side DnD choreography (called from world/Grabs.cpp DNDGrab)
// ---------------------------------------------------------------------------
bool DataDeviceModule::sendDndMotion(const SurfaceRef& surface, double sx, double sy) {
    if (!Compositor::active()) return false;
    Connection* c = deviceConn(*Compositor::active());
    if (!c) return false;

    if (!surface.valid()) {
        // Drag over empty world space: implicit leave.
        gDragTarget = SurfaceRef{};
        return true;
    }
    if (!(gDragTarget == surface)) {
        // New target: create a data_offer on the TARGET client, then
        // wl_data_device.enter on its device.
        Connection* tc = surface.conn;
        if (!tc || !tc->alive()) return false;
        ObjectId offer = tc->nextId();
        tc->addObject(offer, ifaces::WlDataOffer, 3, handleDataOffer);
        DataOffer& oRef = tc->makeUserData<DataOffer>(offer);
        DataOffer* o = &oRef;
        o->conn = tc;
        o->id = offer;
        o->source = &gDragSource;

        // data_offer.offer(mime) for each advertised mime type.
        for (const auto& mime : gDragSource.mimeTypes) {
            Writer w;
            w.string(mime);
            tc->sendEvent(offer, 0, w);
        }
        // source_actions (v3)
        Writer sa;
        sa.u32(gDragSource.actions);
        tc->sendEvent(offer, 1, sa);

        if (gDragOfferConn && gDragOfferConn->alive() && gDragOfferId != 0) {
            gDragOfferConn->destroyObject(gDragOfferId);
        }
        gDragOfferConn = tc;
        gDragOfferId = offer;

        // wl_data_device.enter(serial, surface, x, y, id offer) — sent on the
        // device belonging to the target's connection (same seat).
        auto devId = gDevice.device;
        Writer w;
        w.u32(tc->nextSerial());
        w.objectId(surface.id);
        w.fixed(sx);
        w.fixed(sy);
        w.objectId(offer);
        tc->sendEvent(devId, 1, w);
        gDragTarget = surface;
        return true;
    }
    // Same target: wl_data_device.motion.
    Writer w;
    w.u32(0);
    w.fixed(sx);
    w.fixed(sy);
    c->sendEvent(gDevice.device, 3, w);
    return true;
}

bool DataDeviceModule::dndDrop() {
    Compositor* comp = Compositor::active();
    if (!comp || !comp->dnd().active) return false;
    Connection* c = deviceConn(*comp);
    if (!c) return false;
    Writer w;
    c->sendEvent(gDevice.device, 4, w); // wl_data_device.drop
    Writer dp;
    if (gDragSource.conn && gDragSource.conn->alive()) {
        gDragSource.conn->sendEvent(gDragSource.id, 3, dp); // dnd_drop_performed
    }
    return true;
}

bool DataDeviceModule::dndCancel() {
    Compositor* comp = Compositor::active();
    if (!comp) return false;
    if (gDragSource.conn && gDragSource.conn->alive()) {
        Writer w;
        gDragSource.conn->sendEvent(gDragSource.id, 2, w); // wl_data_source.cancelled
    }
    killDrag();
    return true;
}

std::vector<std::string> DataDeviceModule::activeDragMimeTypes() {
    if (Compositor* comp = Compositor::active()) {
        if (comp->dnd().active) return comp->dnd().mimeTypes;
    }
    return {};
}

} // namespace wlc
