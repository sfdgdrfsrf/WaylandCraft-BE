// ============================================================================
//  WaylandCraft-BE — compositor/Server.h
//
//  The Compositor: listens on a Wayland socket (Unix domain on Linux/Android,
//  optional TCP loopback for the Android companion / Termux bridge), accepts
//  clients, and runs the event loop on a dedicated thread. The game pumps it
//  once per frame with update() — the same choreography as upstream's
//  bridge.update() call from MinecraftMixin.runTick.
// ============================================================================
#pragma once

#include "compositor/Client.h"
#include "compositor/Types.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wlc {

class Connection;

/// A request an app made of the window manager (drained by WindowRegistry).
struct ToplevelRequest {
    enum class Kind {
        Minimize,
        Maximize,
        Unmaximize,
        Fullscreen,
        Unfullscreen,
        Move,       // also fill serial + edges invalid
        Resize,     // + edges
        ShowMenu,   // + x/y (surface-local)
        Close,
        DndStart,   // + serial (implicit grab), source/icon refs
    };
    Kind kind = Kind::Close;
    SurfaceRef surface;   // toplevel root surface
    SurfaceRef seatRef;   // seat object that carried the request (Move/Resize)
    uint32_t serial = 0;  // implicit-grab serial (Move/Resize/DndStart)
    uint32_t edges = 0;   // Resize
    int32_t x = 0, y = 0; // ShowMenu
    SurfaceRef source;    // DndStart data source
    SurfaceRef icon;      // DndStart icon surface
};

struct DndState {
    // DnD in flight (originated by an app's start_drag; choreographed by the
    // game's DNDGrab exactly like upstream's native `ddm` module).
    bool active = false;
    SurfaceRef source;      // wl_data_source object
    SurfaceRef origin;      // drag origin surface
    SurfaceRef icon;        // dnd icon surface (may be invalid)
    uint32_t serial = 0;
    std::vector<std::string> mimeTypes;
    uint32_t sourceActions = 0;
};

class Compositor {
public:
    Compositor();
    ~Compositor();

    /// Process-wide anchor for the input-injection API (set by start()).
    static Compositor* active();

    /// Starts listening. `socketName` is used as $WAYLAND_DISPLAY (Unix) and
    /// `tcpPort` > 0 additionally opens a loopback listener (Android relay /
    /// Termux bridge). Returns false on failure.
    bool start(const std::string& runtimeDir, const std::string& socketName, int tcpPort);
    void shutdown();

    bool running() const { return running_.load(); }

    /// Absolute path of the listening socket (for $WAYLAND_DISPLAY +
    /// logging, mirroring upstream's "Wayland compositor running on <socket>").
    const std::string& socketPath() const { return socketPath_; }
    int tcpPort() const { return tcpPort_; }

    // ---- game-side pump (called from the client render thread) -----------
    void update();

    // ---- game window size (virtual output) --------------------------------
    void setOutputSize(int32_t width, int32_t height);
    int32_t outputWidth() const { return outW_; }
    int32_t outputHeight() const { return outH_; }
    int32_t outputBoundsWidth() const { return outBoundsW_; }
    int32_t outputBoundsHeight() const { return outBoundsH_; }
    void setOutputBounds(int32_t w, int32_t h) { outBoundsW_ = w; outBoundsH_ = h; }

    // ---- window-management request queue ---------------------------------
    std::vector<ToplevelRequest> drainRequests();
    void pushRequest(ToplevelRequest req);
    DndState& dnd() { return dnd_; }

    // ---- commit notifications (surface -> game layer) ----------------------
    void onSurfaceCommitted(const SurfaceRef& surface);
    std::vector<SurfaceRef> drainCommittedSurfaces();

    // ---- connection access (iterate under lock) ---------------------------
    template <typename F>
    void forEachConnection(F&& f) {
        std::lock_guard<std::mutex> lock(connMutex_);
        for (auto& c : connections_) {
            if (c && c->alive()) f(*c);
        }
    }

    // ---- input injection (Seat.cpp hooks these up to bound objects) -------
    // Implemented in Seat.cpp; game side calls these from InputRouter.
    // All no-ops when no client has bound a pointer/keyboard yet.
    static bool pointerMotion(const SurfaceRef& surface, double sx, double sy);
    static bool pointerLeave();
    static uint32_t pointerButton(uint32_t evdevButton, bool pressed); // returns serial
    static bool pointerScroll(double dx, double dy); // positive dy = scroll up
    static bool keyboardKey(uint32_t evdevScancode, bool pressed);
    static bool keyboardFocus(const SurfaceRef& surface); // focusSurface() port
    static void keyboardModifiers(uint32_t depressed, uint32_t latched, uint32_t locked,
                                  uint32_t group);
    static int cursorShape(); // wp_cursor_shape value of current pointer focus, -1 = unknown

    // ---- internal (protocol modules / event loop) -------------------------
    std::mutex& mutex() { return updateMutex_; }
    void markForClose(int fd);
    Connection* connectionForFd(int fd);

    // Registered by Seat.cpp: live pointer/keyboard objects (may be absent).
    struct SeatObjects {
        SurfaceRef seat, pointer, keyboard;
        Connection* conn = nullptr;
        bool hasPointer = false, hasKeyboard = false;
    };
    SeatObjects& seat() { return seat_; }

private:
    SeatObjects seat_;
public:

    /// Sends `done` for every committed frame callback and releases buffers
    /// whose contents were consumed. Called from update().
    void presentFrameCallbacks();

private:
    void eventLoop();
    bool setupUnixListener(const std::string& path);
    bool setupTcpListener(int port);
    void acceptNewClients(int listenFd, bool isTcp);
    bool handleClientIo(Connection& c, bool readable);
    void flushClient(Connection& c);
    void reapClosed();

    std::atomic<bool> running_{false};
    int unixListenFd_ = -1;
    int tcpListenFd_ = -1;
    std::string socketPath_;
    std::string runtimeDir_;
    std::string socketName_;
    int tcpPort_ = 0;

    std::thread loopThread_;
    std::mutex updateMutex_;   // guards requests_, surfaces during dispatch
    std::mutex connMutex_;     // guards connections_ container
    std::vector<std::shared_ptr<Connection>> connections_;

    std::mutex reqMutex_;
    std::vector<ToplevelRequest> requests_;
    std::vector<SurfaceRef> committed_;
    DndState dnd_;

    int32_t outW_ = 1280, outH_ = 720;
    int32_t outBoundsW_ = 0, outBoundsH_ = 0;
};

} // namespace wlc
