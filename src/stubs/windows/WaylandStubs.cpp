// ============================================================================
//  WaylandCraft-BE — src/stubs/windows/WaylandStubs.cpp
//
//  Windows compatibility layer. The compositor core (Unix sockets +
//  SCM_RIGHTS fd passing), the shm memfd allocator and the XDG/Android
//  platform bridges are POSIX-only, so Windows builds swap them for these
//  no-op implementations. This is the deliberate upstream parity model:
//
//      "You can install it on MacOS and Windows but you won't have any of
//       the features."  — waylandcraft README
//
//  The mod still loads: config, commands, HUD chrome, window items, the
//  scriptevent sync channel and the NullBridge all work; remote/capture
//  window *content* is simply unavailable because no compositor is running.
//
//  MAINTENANCE RULE: this file must implement every non-inline method
//  declared in compositor/Server.h, compositor/Client.h, compositor/
//  Surface.h, compositor/XdgShell.h, compositor/DataDevice.h, compositor/
//  Seat.h, compositor/Globals.h, util/MemFd.h and util/XkbKeymap.h.
//  Windows CI (the linker) is the enforcement mechanism: a new method that
//  is declared but not stubbed shows up as an unresolved external here.
// ============================================================================

#include "compositor/Client.h"
#include "compositor/DataDevice.h"
#include "compositor/Globals.h"
#include "compositor/Seat.h"
#include "compositor/Server.h"
#include "compositor/Surface.h"
#include "compositor/XdgShell.h"
#include "util/Log.h"
#include "util/MemFd.h"
#include "util/XkbKeymap.h"

#include <deque>
#include <utility>

namespace wlc {

namespace {

/// Warns once — the stubs are hit on hot paths (update(), input injection),
/// so repeated logging would flood the console.
void warnOnce(const char* what) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    Log::warn(strf("Windows build: %s is unavailable (compositor core is "
                   "POSIX-only). See docs/PORT_NOTES.md — upstream parity: "
                   "features need Linux/Android.", what));
}

} // namespace

// ---------------------------------------------------------------------------
// Compositor (compositor/Server.h — real impl in compositor/Server.cpp)
// ---------------------------------------------------------------------------

Compositor::Compositor() = default;
Compositor::~Compositor() = default;

Compositor* Compositor::active() { return nullptr; }

bool Compositor::start(const std::string& runtimeDir, const std::string& socketName,
                       int tcpPort) {
    (void)runtimeDir;
    (void)socketName;
    (void)tcpPort;
    warnOnce("the Wayland compositor");
    return false;
}

void Compositor::shutdown() {}

void Compositor::update() {}

void Compositor::setOutputSize(int32_t width, int32_t height) {
    (void)width;
    (void)height;
}

std::vector<ToplevelRequest> Compositor::drainRequests() { return {}; }

void Compositor::pushRequest(ToplevelRequest req) { (void)req; }

void Compositor::onSurfaceCommitted(const SurfaceRef& surface) { (void)surface; }

std::vector<SurfaceRef> Compositor::drainCommittedSurfaces() { return {}; }

bool Compositor::pointerMotion(const SurfaceRef& surface, double sx, double sy) {
    (void)surface; (void)sx; (void)sy;
    return false;
}

bool Compositor::pointerLeave() { return false; }

uint32_t Compositor::pointerButton(uint32_t evdevButton, bool pressed) {
    (void)evdevButton; (void)pressed;
    return 0;
}

bool Compositor::pointerScroll(double dx, double dy) {
    (void)dx; (void)dy;
    return false;
}

bool Compositor::keyboardKey(uint32_t evdevScancode, bool pressed) {
    (void)evdevScancode; (void)pressed;
    return false;
}

bool Compositor::keyboardFocus(const SurfaceRef& surface) {
    (void)surface;
    return false;
}

void Compositor::keyboardModifiers(uint32_t depressed, uint32_t latched,
                                   uint32_t locked, uint32_t group) {
    (void)depressed; (void)latched; (void)locked; (void)group;
}

int Compositor::cursorShape() { return -1; }

void Compositor::markForClose(int fd) { (void)fd; }

Connection* Compositor::connectionForFd(int fd) {
    (void)fd;
    return nullptr;
}

void Compositor::presentFrameCallbacks() {}

void Compositor::eventLoop() {}

bool Compositor::setupUnixListener(const std::string& path) {
    (void)path;
    return false;
}

bool Compositor::setupTcpListener(int port) {
    (void)port;
    return false;
}

void Compositor::acceptNewClients(int listenFd, bool isTcp) {
    (void)listenFd; (void)isTcp;
}

bool Compositor::handleClientIo(Connection& c, bool readable) {
    (void)c; (void)readable;
    return false;
}

void Compositor::flushClient(Connection& c) { (void)c; }

void Compositor::reapClosed() {}

// ---------------------------------------------------------------------------
// Connection (compositor/Client.h — real impl in compositor/Client.cpp)
// ---------------------------------------------------------------------------

Connection::Connection(Compositor& compositor, int fd, int id)
    : compositor_(compositor), fd_(fd), id_(id) {}

Connection::~Connection() = default;

void Connection::kill() { alive_ = false; }

ObjectId Connection::nextId() { return 0; }

Object* Connection::object(ObjectId id) {
    (void)id;
    return nullptr;
}

void Connection::addObject(ObjectId id, const char* interface, uint32_t version,
                           RequestHandler handler) {
    (void)id; (void)interface; (void)version; (void)handler;
}

void Connection::destroyObject(ObjectId id) { (void)id; }

bool Connection::processIncoming() { return false; }

void Connection::queueFd(int fd) { (void)fd; }

void Connection::sendEvent(ObjectId target, uint16_t opcode, const Writer& body,
                           const std::vector<int>& fds) {
    (void)target; (void)opcode; (void)body; (void)fds;
}

void Connection::sendError(ObjectId object, uint32_t code, std::string_view message) {
    (void)object; (void)code; (void)message;
}

int Connection::takeFd() { return -1; }

ObjectId Connection::createCallback() { return 0; }

bool Connection::dispatch(MessageHeader header, Reader payload, std::deque<int> fds) {
    (void)header; (void)payload; (void)fds;
    return false;
}

void Connection::postDeleteId(ObjectId id) { (void)id; }

// ---------------------------------------------------------------------------
// Surface module (compositor/Surface.h — real impl in compositor/Surface.cpp)
// ---------------------------------------------------------------------------

SurfaceRef SurfaceState::ref(Connection& c) const {
    SurfaceRef r;
    r.conn = &c;
    r.id = 0; // no live surface behind the stub
    return r;
}

SurfaceState& surfaceState(Connection& conn, ObjectId surfaceId) {
    (void)conn; (void)surfaceId;
    static SurfaceState dummy;
    return dummy;
}

SurfaceState* findSurfaceState(Connection& conn, ObjectId surfaceId) {
    (void)conn; (void)surfaceId;
    return nullptr;
}

BufferView resolveBuffer(Connection& conn, ObjectId bufferId) {
    (void)conn; (void)bufferId;
    return BufferView{};
}

namespace SurfaceModule {

bool handleSurfaceRequests(Connection& conn, ObjectId self, uint16_t opcode, Reader& args) {
    (void)conn; (void)self; (void)opcode; (void)args;
    return false;
}

void commitSurface(Connection& conn, ObjectId surfaceId, SurfaceState& st) {
    (void)conn; (void)surfaceId; (void)st;
}

void presentSurfaceFrame(Connection& conn, ObjectId surfaceId) {
    (void)conn; (void)surfaceId;
}

bool bindCompositor(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool bindShm(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool bindSubcompositor(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool bindViewporter(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool bindSinglePixel(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

} // namespace SurfaceModule

// ---------------------------------------------------------------------------
// XDG shell (compositor/XdgShell.h — real impl in compositor/XdgShell.cpp)
// ---------------------------------------------------------------------------

namespace XdgShellModule {

bool bindWmBase(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool sendToplevelConfigure(Connection& conn, ObjectId toplevelId, int32_t width,
                           int32_t height, bool maximized, bool fullscreen,
                           bool activated) {
    (void)conn; (void)toplevelId; (void)width; (void)height;
    (void)maximized; (void)fullscreen; (void)activated;
    return false;
}

bool sendToplevelClose(Connection& conn, ObjectId toplevelId) {
    (void)conn; (void)toplevelId;
    return false;
}

std::vector<ToplevelState*> toplevels(Connection& conn) {
    (void)conn;
    return {};
}

ToplevelState* findToplevel(Connection& conn, ObjectId toplevelId) {
    (void)conn; (void)toplevelId;
    return nullptr;
}

ToplevelState* toplevelForSurface(Connection& conn, ObjectId surfaceId) {
    (void)conn; (void)surfaceId;
    return nullptr;
}

} // namespace XdgShellModule

// ---------------------------------------------------------------------------
// Data device (compositor/DataDevice.h — real impl in compositor/DataDevice.cpp)
// ---------------------------------------------------------------------------

namespace DataDeviceModule {

bool bindManager(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool sendDndMotion(const SurfaceRef& surface, double sx, double sy) {
    (void)surface; (void)sx; (void)sy;
    return false;
}

bool dndDrop() { return false; }

bool dndCancel() { return false; }

std::vector<std::string> activeDragMimeTypes() { return {}; }

} // namespace DataDeviceModule

// ---------------------------------------------------------------------------
// Seat (compositor/Seat.h — real impl in compositor/Seat.cpp)
// ---------------------------------------------------------------------------

namespace SeatModule {

bool bindSeat(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

bool bindCursorShapeMgr(Connection& conn, const char* iface, uint32_t version, ObjectId id) {
    (void)conn; (void)iface; (void)version; (void)id;
    return false;
}

void pushKeymap(Connection& conn, int keymapFd, uint32_t size) {
    (void)conn; (void)keymapFd; (void)size;
}

void onPointerBound(Connection& conn, ObjectId pointerId) {
    (void)conn; (void)pointerId;
}

void onKeyboardBound(Connection& conn, ObjectId keyboardId) {
    (void)conn; (void)keyboardId;
}

} // namespace SeatModule

// ---------------------------------------------------------------------------
// Globals (compositor/Globals.h — real impl in compositor/Globals.cpp)
// ---------------------------------------------------------------------------

void installDisplayHandlers(Connection& conn) { (void)conn; }

void advertiseGlobals(Connection& conn, ObjectId registryId) {
    (void)conn; (void)registryId;
}

namespace Globals {

bool bindGlobal(Connection& conn, ObjectId registryId, uint32_t name,
                const char* interface, uint32_t version, ObjectId newId) {
    (void)conn; (void)registryId; (void)name; (void)interface; (void)version; (void)newId;
    return false;
}

bool bindOutput(Connection& conn, const char* interface, uint32_t version, ObjectId newId) {
    (void)conn; (void)interface; (void)version; (void)newId;
    return false;
}

} // namespace Globals

void sendOutputState(Connection& conn, ObjectId outputId) {
    (void)conn; (void)outputId;
}

// ---------------------------------------------------------------------------
// MemFd (util/MemFd.h — real impl in util/MemFd.cpp)
// ---------------------------------------------------------------------------

int makeMemFd(size_t size) {
    (void)size;
    return -1;
}

size_t memFdSize(int fd, size_t fallback) {
    (void)fd;
    return fallback;
}

// ---------------------------------------------------------------------------
// XkbKeymap (util/XkbKeymap.h — real impl in util/XkbKeymap.cpp)
// ---------------------------------------------------------------------------

const char* defaultXkbKeymap() { return ""; }

void useKeymapText(const std::string& keymapText) { (void)keymapText; }

} // namespace wlc
