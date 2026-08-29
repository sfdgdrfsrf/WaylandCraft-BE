// ============================================================================
//  WaylandCraft-BE — compositor/Seat.cpp
//  Pointer/keyboard event injection: enter/leave/motion/button/axis/frame,
//  keymap/enter/modifiers/key/repeat_info, cursor shape requests.
//
//  Scancode convention: evdev (+8 == XKB) — the game layer maps Bedrock key
//  codes to evdev (input/KeyCodes.h), mirroring upstream's correctScancode.
// ============================================================================
#include "compositor/Seat.h"

#include "compositor/Client.h"
#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "util/Log.h"
#include "util/MemFd.h"

#include <algorithm>
#include <cstring>
#include <time.h>
#include <unistd.h>

namespace wlc {

// Declared in XkbKeymap.cpp (compiled-in US layout + file override).
const char* defaultXkbKeymap();

namespace {

uint32_t nowMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

struct PointerFocus {
    SurfaceRef surface;
    double sx = 0, sy = 0;
    uint32_t enterSerial = 0;
    bool inside = false;
    bool buttonDown[3] = {false, false, false};
};

struct KeyboardFocus {
    SurfaceRef surface;
    uint32_t enterSerial = 0;
    bool inside = false;
    uint32_t depressed = 0, latched = 0, locked = 0, group = 0;
};

// Compositor-global focus state (single seat: "waylandcraft-be-seat").
PointerFocus gPointer;
KeyboardFocus gKeyboard;
int gCursorShape = -1; // wp_cursor_shape_device_v1 shape, -1 = app didn't set

SurfaceRef seatObject(Connection& conn) {
    auto& s = conn.compositor().seat();
    if (s.conn == &conn && s.seat.valid()) return s.seat;
    return {};
}

void sendPointerEnter(Connection& conn, const SurfaceRef& surf, double sx, double sy) {
    auto& seat = conn.compositor().seat();
    if (!seat.conn || !seat.hasPointer) return;
    gPointer.surface = surf;
    gPointer.sx = sx;
    gPointer.sy = sy;
    gPointer.enterSerial = conn.nextSerial();
    gPointer.inside = true;

    Writer w;
    w.u32(gPointer.enterSerial);
    w.objectId(surf.id);
    w.fixed(sx);
    w.fixed(sy);
    conn.sendEvent(seat.pointer.id, 0, w); // wl_pointer.enter
}

void sendPointerLeave(Connection& conn) {
    auto& seat = conn.compositor().seat();
    if (!seat.conn || !seat.hasPointer) return;
    if (!gPointer.inside) return;
    uint32_t serial = conn.nextSerial();
    Writer w;
    w.u32(serial);
    w.objectId(gPointer.surface.id);
    conn.sendEvent(seat.pointer.id, 1, w); // wl_pointer.leave
    gPointer.inside = false;
    gPointer.surface = {};
    gCursorShape = -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Static injection API (Compositor::pointerMotion etc. delegate here).
// These resolve the live seat connection from the Compositor singleton-free
// design: the seat module registers its connection in Compositor::seat().
// ---------------------------------------------------------------------------
namespace {

Connection* seatConn(Compositor& comp) {
    auto& s = comp.seat();
    return s.conn && s.conn->alive() ? s.conn : nullptr;
}

Compositor* activeCompositor() { return Compositor::active(); }

} // namespace

bool Compositor::pointerMotion(const SurfaceRef& surface, double sx, double sy) {
    if (!surface.valid()) return false;
    Connection* c = surface.conn;
    if (!c || !c->alive()) return false;
    auto& seat = c->compositor().seat();
    if (!seat.hasPointer) return false;

    std::lock_guard<std::mutex> lock(c->compositor().mutex());
    if (!gPointer.inside || !(gPointer.surface == surface)) {
        if (gPointer.inside) sendPointerLeave(*c);
        sendPointerEnter(*c, surface, sx, sy);
    }
    gPointer.sx = sx;
    gPointer.sy = sy;
    Writer w;
    w.u32(nowMs());
    w.fixed(sx);
    w.fixed(sy);
    c->sendEvent(seat.pointer.id, 2, w); // wl_pointer.motion
    return true;
}

bool Compositor::pointerLeave() {
    // Leave wherever the pointer currently is (upstream sendMotionOutside).
    Compositor* comp = activeCompositor();
    if (!comp) return false;
    Connection* c = seatConn(*comp);
    if (!c) return false;
    std::lock_guard<std::mutex> lock(comp->mutex());
    sendPointerLeave(*c);
    return true;
}

uint32_t Compositor::pointerButton(uint32_t evdevButton, bool pressed) {
    Compositor* comp = activeCompositor();
    if (!comp) return 0;
    Connection* c = seatConn(*comp);
    if (!c) return 0;
    auto& seat = c->compositor().seat();
    if (!seat.hasPointer) return 0;

    std::lock_guard<std::mutex> lock(comp->mutex());
    uint32_t serial = c->nextSerial();
    if (pressed && gPointer.inside) {
        size_t idx = std::min<size_t>(evdevButton - 0x110, 2);
        if (evdevButton >= 0x110 && idx < 3) gPointer.buttonDown[idx] = true;
    }
    Writer w;
    w.u32(serial);
    w.u32(nowMs());
    w.u32(evdevButton);
    w.u32(pressed ? ifaces::ButtonPressed : ifaces::ButtonReleased);
    c->sendEvent(seat.pointer.id, 3, w); // wl_pointer.button

    // frame separator (v5+ clients expect it after input bursts)
    Writer fw;
    c->sendEvent(seat.pointer.id, 5, fw);
    return serial;
}

bool Compositor::pointerScroll(double dx, double dy) {
    Compositor* comp = activeCompositor();
    if (!comp) return false;
    Connection* c = seatConn(*comp);
    if (!c) return false;
    auto& seat = c->compositor().seat();
    if (!seat.hasPointer || !gPointer.inside) return false;

    std::lock_guard<std::mutex> lock(comp->mutex());
    auto sendAxis = [&](uint32_t axis, double value) {
        Writer w;
        w.u32(nowMs());
        w.u32(axis);
        w.fixed(value);
        c->sendEvent(seat.pointer.id, 4, w); // wl_pointer.axis
        // axis_value120 (v8) + axis_discrete for wheel-style clients
        int steps = static_cast<int>(value * 10.0);
        if (c->version(seat.pointer.id) >= 8) {
            Writer v120;
            v120.u32(axis);
            v120.i32(steps);
            c->sendEvent(seat.pointer.id, 9, v120);
        }
        Writer disc;
        disc.u32(axis);
        disc.i32(steps);
        c->sendEvent(seat.pointer.id, 8, disc);
    };
    if (dy != 0) sendAxis(ifaces::AxisVertical, dy);    // positive = scroll up
    if (dx != 0) sendAxis(ifaces::AxisHorizontal, dx);
    Writer fw;
    c->sendEvent(seat.pointer.id, 5, fw); // wl_pointer.frame
    return true;
}

bool Compositor::keyboardKey(uint32_t evdevScancode, bool pressed) {
    Compositor* comp = activeCompositor();
    if (!comp) return false;
    Connection* c = seatConn(*comp);
    if (!c) return false;
    auto& seat = c->compositor().seat();
    if (!seat.hasKeyboard || !gKeyboard.inside) return false;

    std::lock_guard<std::mutex> lock(comp->mutex());
    Writer w;
    w.u32(c->nextSerial());
    w.u32(nowMs());
    w.u32(evdevScancode); // evdev scancode (XKB offset handled by clients)
    w.u32(pressed ? ifaces::ButtonPressed : ifaces::ButtonReleased);
    c->sendEvent(seat.keyboard.id, 3, w); // wl_keyboard.key
    return true;
}

bool Compositor::keyboardFocus(const SurfaceRef& surface) {
    Compositor* comp = activeCompositor();
    if (!comp) return false;
    Connection* c = surface.valid() ? surface.conn : (seatConn(*comp));
    if (!c) return false;
    auto& seat = c->compositor().seat();
    if (!seat.hasKeyboard) return false;

    std::lock_guard<std::mutex> lock(comp->mutex());
    if (!surface.valid()) {
        if (gKeyboard.inside) {
            Writer w;
            w.u32(c->nextSerial());
            w.objectId(gKeyboard.surface.id);
            c->sendEvent(seat.keyboard.id, 2, w); // wl_keyboard.leave
            gKeyboard.inside = false;
            gKeyboard.surface = {};
        }
        return true;
    }

    gKeyboard.surface = surface;
    gKeyboard.enterSerial = c->nextSerial();
    gKeyboard.inside = true;
    Writer w;
    w.u32(gKeyboard.enterSerial);
    w.objectId(surface.id);
    uint32_t none = 0;
    w.array(&none, 0); // empty keys array
    c->sendEvent(seat.keyboard.id, 1, w); // wl_keyboard.enter
    return true;
}

void Compositor::keyboardModifiers(uint32_t depressed, uint32_t latched, uint32_t locked,
                                   uint32_t group) {
    Compositor* comp = activeCompositor();
    if (!comp) return;
    Connection* c = seatConn(*comp);
    if (!c) return;
    auto& seat = c->compositor().seat();
    if (!seat.hasKeyboard || !gKeyboard.inside) return;

    std::lock_guard<std::mutex> lock(comp->mutex());
    gKeyboard.depressed = depressed;
    gKeyboard.latched = latched;
    gKeyboard.locked = locked;
    gKeyboard.group = group;
    Writer w;
    w.u32(c->nextSerial());
    w.u32(depressed);
    w.u32(latched);
    w.u32(locked);
    w.u32(group);
    c->sendEvent(seat.keyboard.id, 4, w); // wl_keyboard.modifiers
}

int Compositor::cursorShape() {
    return gCursorShape;
}

// ---------------------------------------------------------------------------
// Seat binding
// ---------------------------------------------------------------------------
namespace {

bool handlePointer(Connection& c, ObjectId self, uint16_t op, Reader&) {
    if (op == 0) { // set_cursor(serial, surface|0, hx, hy)
        // The game renders the cursor itself (crosshair); we accept and drop.
        return true;
    }
    if (op == 1) { // release
        c.destroyObject(self);
        auto& seat = c.compositor().seat();
        if (seat.pointer.id == self) seat.hasPointer = false;
        return true;
    }
    return false;
}

bool handleKeyboard(Connection& c, ObjectId self, uint16_t op, Reader&) {
    if (op == 0) { // release
        c.destroyObject(self);
        auto& seat = c.compositor().seat();
        if (seat.keyboard.id == self) seat.hasKeyboard = false;
        return true;
    }
    return false;
}

} // namespace

bool SeatModule::bindSeat(Connection& conn, const char* iface, uint32_t version,
                          ObjectId id) {
    (void)iface;
    Compositor& comp = conn.compositor();
    conn.addObject(id, ifaces::WlSeat, std::min<uint32_t>(version, 9),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       switch (op) {
                       case 0: { // get_pointer(new_id)
                           auto p = a.objectId();
                           if (!p) return false;
                           c.addObject(*p, ifaces::WlPointer, 9, handlePointer);
                           SeatModule::onPointerBound(c, *p);
                           break;
                       }
                       case 1: { // get_keyboard(new_id)
                           auto k = a.objectId();
                           if (!k) return false;
                           c.addObject(*k, ifaces::WlKeyboard, 9, handleKeyboard);
                           SeatModule::onKeyboardBound(c, *k);
                           break;
                       }
                       case 2: return false; // get_touch: unsupported
                       case 3: c.destroyObject(self); break; // release
                       default:
                           return false;
                       }
                       return true;
                   });

    // wl_seat.capabilities(pointer|keyboard) + name
    Writer w;
    w.u32(ifaces::SeatCapPointer | ifaces::SeatCapKeyboard);
    conn.sendEvent(id, 0, w);
    Writer n;
    n.string("waylandcraft-be-seat");
    conn.sendEvent(id, 1, n);

    auto& seat = comp.seat();
    seat.conn = &conn;
    seat.seat = SurfaceRef{&conn, id};
    return true;
}

void SeatModule::onPointerBound(Connection& conn, ObjectId pointerId) {
    auto& seat = conn.compositor().seat();
    seat.hasPointer = true;
    seat.pointer = SurfaceRef{&conn, pointerId};
    gPointer = {};
}

void SeatModule::onKeyboardBound(Connection& conn, ObjectId keyboardId) {
    auto& seat = conn.compositor().seat();
    seat.hasKeyboard = true;
    seat.keyboard = SurfaceRef{&conn, keyboardId};

    // wl_keyboard.keymap(xkb_v1, fd, size): dump the compiled-in default
    // keymap through a memfd — identical mechanism to upstream's
    // setKeymapFromStr / exportKeymap path.
    const char* keymap = defaultXkbKeymap();
    size_t len = std::strlen(keymap) + 1;
    int fd = makeMemFd(len);
    if (fd >= 0) {
        if (::write(fd, keymap, len) == static_cast<ssize_t>(len)) {
            SeatModule::pushKeymap(conn, fd, static_cast<uint32_t>(len));
        } else {
            ::close(fd);
        }
    }
}

void SeatModule::pushKeymap(Connection& conn, int keymapFd, uint32_t size) {
    auto& seat = conn.compositor().seat();
    if (!seat.conn || seat.conn != &conn || !seat.hasKeyboard) {
        ::close(keymapFd); // nobody bound — just drop it
        return;
    }
    Writer w;
    w.u32(ifaces::KeymapFormatXkbV1);
    w.u32(size);
    conn.sendEvent(seat.keyboard.id, 0, w, {keymapFd}); // wl_keyboard.keymap
}

bool SeatModule::bindCursorShapeMgr(Connection& conn, const char* iface, uint32_t version,
                                    ObjectId id) {
    (void)iface;
    conn.addObject(id, ifaces::WpCursorShapeMgr, std::min<uint32_t>(version, 1),
                   [](Connection& c, ObjectId self, uint16_t op, Reader& a) -> bool {
                       if (op == 0) { c.destroyObject(self); return true; }
                       if (op != 1) return false; // get_pointer(new_id, seat)
                       auto dev = a.objectId();
                       if (!dev) return false;
                       c.addObject(*dev, ifaces::WpCursorShapeDev, 1,
                                   [](Connection& c2, ObjectId s2, uint16_t op2,
                                      Reader& a2) -> bool {
                                       if (op2 == 0) { c2.destroyObject(s2); return true; }
                                       if (op2 != 1) return false; // set_shape
                                       auto serial = a2.u32();
                                       auto shape = a2.i32();
                                       if (!serial || !shape) return false;
                                       gCursorShape = *shape; // game swaps crosshair
                                       return true;
                                   });
                       return true;
                   });
    return true;
}

} // namespace wlc
