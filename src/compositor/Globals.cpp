// ============================================================================
//  WaylandCraft-BE — compositor/Globals.cpp
//  wl_display / wl_registry / wl_output implementation.
// ============================================================================
#include "compositor/Globals.h"

#include "compositor/Client.h"
#include "compositor/DataDevice.h"
#include "compositor/Seat.h"
#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "compositor/XdgShell.h"
#include "util/Log.h"

#include <ctime>

namespace wlc {

namespace {

// wl_display opcodes: 0 sync, 1 get_registry
bool handleDisplay(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    switch (opcode) {
    case 0: { // sync(new_id callback)
        auto cb = args.objectId();
        if (!cb) return false;
        Writer w;
        w.u32(conn.nextSerial());
        conn.sendEvent(*cb, 0, w); // wl_callback.done
        break;
    }
    case 1: { // get_registry(new_id)
        auto reg = args.objectId();
        if (!reg) return false;
        conn.addObject(*reg, ifaces::WlRegistry, 1,
                       [](Connection& c, ObjectId selfId, uint16_t op, Reader& a) -> bool {
                           if (op != 0) return false; // bind
                           auto name = a.u32();
                           auto iface = a.string();
                           auto ver = a.u32();
                           auto newId = a.objectId();
                           if (!name || !iface || !ver || !newId) return false;
                           uint32_t bound = std::min<uint32_t>(*ver, 4);
                           if (!Globals::bindGlobal(c, selfId, *name, iface->c_str(), bound,
                                                    *newId)) {
                               c.sendError(kWlDisplayId, 0,
                                           strf("invalid global bind: %s", iface->c_str()));
                               return false;
                           }
                           return true;
                       });
        advertiseGlobals(conn, *reg);
        break;
    }
    default:
        return false;
    }
    (void)self;
    return true;
}

} // namespace

// Registry bind dispatch — implemented here so every module only exposes a
// small bind function (matches how real compositors wire their globals).
bool Globals::bindGlobal(Connection& conn, ObjectId registryId, uint32_t name,
                         const char* interface, uint32_t version, ObjectId newId) {
    (void)registryId;
    switch (name) {
    case globals::WlCompositor:    return SurfaceModule::bindCompositor(conn, interface, version, newId);
    case globals::WlShm:           return SurfaceModule::bindShm(conn, interface, version, newId);
    case globals::WlSeat:          return SeatModule::bindSeat(conn, interface, version, newId);
    case globals::WlOutput:        return bindOutput(conn, interface, version, newId);
    case globals::WlSubcompositor: return SurfaceModule::bindSubcompositor(conn, interface, version, newId);
    case globals::WpDataDeviceManager: return DataDeviceModule::bindManager(conn, interface, version, newId);
    case globals::XdgWmBase:       return XdgShellModule::bindWmBase(conn, interface, version, newId);
    case globals::WpViewporter:    return SurfaceModule::bindViewporter(conn, interface, version, newId);
    case globals::WpSinglePixel:   return SurfaceModule::bindSinglePixel(conn, interface, version, newId);
    case globals::WpCursorShape:   return SeatModule::bindCursorShapeMgr(conn, interface, version, newId);
    default: return false;
    }
}

void installDisplayHandlers(Connection& conn) {
    conn.addObject(kWlDisplayId, ifaces::WlDisplay, 1, handleDisplay);
}

void advertiseGlobals(Connection& conn, ObjectId registryId) {
    auto advertise = [&](uint32_t name, const char* iface, uint32_t version) {
        Writer w;
        w.u32(name);
        w.string(iface);
        w.u32(version);
        conn.sendEvent(registryId, 0, w); // wl_registry.global
    };
    advertise(globals::WlCompositor,        ifaces::WlCompositor, 6);
    advertise(globals::WlShm,               ifaces::WlShm, 1);
    advertise(globals::WlSeat,              ifaces::WlSeat, 9);
    advertise(globals::WlOutput,            ifaces::WlOutput, 4);
    advertise(globals::WlSubcompositor,     ifaces::WlSubcompositor, 1);
    advertise(globals::WpDataDeviceManager, ifaces::WlDataDeviceManager, 3);
    advertise(globals::XdgWmBase,           ifaces::XdgWmBase, 6);
    advertise(globals::WpViewporter,        ifaces::WpViewporter, 1);
    advertise(globals::WpSinglePixel,       ifaces::WpSinglePixelMgr, 1);
    advertise(globals::WpCursorShape,       ifaces::WpCursorShapeMgr, 1);

    // wl_shm.format(ARGB8888), wl_shm.format(XRGB8888) are sent when the
    // client binds wl_shm (see SurfaceModule::bindShm).
}

void sendOutputState(Connection& conn, ObjectId outputId) {
    Compositor& comp = conn.compositor();

    // wl_output.geometry — make/model identify the virtual monitor, matching
    // upstream's "VirtualMonitor" output.
    {
        Writer w;
        w.i32(0); w.i32(0);                     // x, y
        w.i32(320); w.i32(240);                 // physical size (mm), cosmetic
        w.i32(0);                               // subpixel: unknown
        w.string("WaylandCraftBE");             // make
        w.string("Virtual Monitor");            // model  (upstream: VirtualMonitor)
        w.i32(0);                               // transform: normal
        conn.sendEvent(outputId, 0, w);
    }
    {
        Writer w;
        w.u32(1);                               // flags: CURRENT
        w.i32(comp.outputWidth());
        w.i32(comp.outputHeight());
        w.i32(60000);                           // refresh 60 Hz
        conn.sendEvent(outputId, 1, w);         // wl_output.mode
    }
    {
        Writer w;
        w.u32(static_cast<uint32_t>(::time(nullptr)));
        conn.sendEvent(outputId, 2, w);         // wl_output.done
    }
    {
        Writer w;
        w.i32(1);                               // scale 1
        conn.sendEvent(outputId, 3, w);
        Writer wn;
        wn.string("waylandcraft-0");
        conn.sendEvent(outputId, 4, wn);        // wl_output.name (v4)
        Writer wd;
        wd.string("Minecraft Bedrock virtual output");
        conn.sendEvent(outputId, 5, wd);        // wl_output.description (v4)
    }
}

bool Globals::bindOutput(Connection& conn, const char* interface, uint32_t version,
                         ObjectId newId) {
    (void)interface;
    conn.addObject(newId, ifaces::WlOutput, std::min<uint32_t>(version, 4),
                   [](Connection& c, ObjectId self, uint16_t opcode, Reader&) -> bool {
                       if (opcode == 0) { // release
                           c.destroyObject(self);
                           return true;
                       }
                       return false;
                   });
    sendOutputState(conn, newId);
    return true;
}

} // namespace wlc
