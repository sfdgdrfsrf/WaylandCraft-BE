// ============================================================================
//  WaylandCraft-BE — compositor/Client.h
//  One connected Wayland client (a launched app, a Termux GUI app, or the
//  Android capture-helper relaying an Android app's frames).
// ============================================================================
#pragma once

#include <cstdint>

#include "compositor/WireProtocol.h"

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>

namespace wlc {

class Compositor;      // compositor/Server.h
class Connection;      // socket transport, defined in Server.h

/// Handler for one object's requests.
///   opcode + args (Reader over the payload AFTER the header)
///   Return false => protocol error (connection killed).
using RequestHandler =
    std::function<bool(Connection& conn, ObjectId self, uint16_t opcode, Reader& args)>;

struct Object {
    ObjectId id = 0;
    std::string interface;
    uint32_t version = 0;
    RequestHandler handler;              // request dispatcher (may be null)
    bool destroyed = false;
};

/// fd passing context: fds arrive via SCM_RIGHTS in message order.
/// The connection exposes takeFd() to handlers, which must consume fds in
/// argument order (wl_shm.create_pool, wl_data_offer.receive, ...).

class Connection {
public:
    Connection(Compositor& compositor, int fd, int id);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // ---- transport -----------------------------------------------------
    int fd() const { return fd_; }
    Compositor& compositor() const { return compositor_; }
    bool alive() const { return alive_; }
    void kill(); // clean shutdown; Compositor will reap

    // ---- object table ---------------------------------------------------
    ObjectId nextId();
    Object* object(ObjectId id);
    void addObject(ObjectId id, const char* interface, uint32_t version,
                   RequestHandler handler);
    void destroyObject(ObjectId id); // queues wl_display.delete_id + local removal

    // ---- requests --------------------------------------------------------
    /// Reads all complete messages currently buffered, dispatching each.
    /// Returns false on protocol error.
    bool processIncoming();
    void queueFd(int fd); // fd received in ancillary data, in wire order

    // ---- events -----------------------------------------------------------
    /// Marshals and sends an event; fd args must be queued via queueEventFd
    /// in argument order BEFORE calling sendEvent (they are attached to the
    /// same sendmsg call).
    void sendEvent(ObjectId target, uint16_t opcode, const Writer& body,
                   const std::vector<int>& fds = {});
    void sendError(ObjectId object, uint32_t code, std::string_view message);

    /// Fds received for the message currently being dispatched.
    /// Handlers consume them in argument order.
    int takeFd();

    /// Object helpers shared across protocol modules.
    ObjectId createCallback(); // wl_callback + done(serial) event
    uint32_t nextSerial() { return serial_++; }

    /// Generic per-object userdata (SurfaceState, ToplevelState, shared
    /// buffer descriptors...). Protocol modules own the pointee; freed when
    /// the object is destroyed or the connection dies.
    /// Type-tagged object userdata: prevents accidental cross-type reads
    /// (a ToplevelState slot reinterpreted as SurfaceState reads garbage).
    template <typename T>
    T* userData(ObjectId id) {
        auto it = userData_.find(id);
        if (it == userData_.end() || it->second.second == nullptr ||
            *it->second.second != typeid(T))
            return nullptr;
        return static_cast<T*>(it->second.first.get());
    }
    template <typename T>
    T& makeUserData(ObjectId id) {
        auto& slot = userData_[id];
        if (!slot.first || slot.second == nullptr || *slot.second != typeid(T)) {
            slot = {std::make_shared<T>(), &typeid(T)};
        }
        return *static_cast<T*>(slot.first.get());
    }
    template <typename T>
    std::shared_ptr<T> shared(ObjectId id) {
        auto it = userData_.find(id);
        if (it == userData_.end() || it->second.second == nullptr ||
            *it->second.second != typeid(T))
            return nullptr;
        return std::static_pointer_cast<T>(it->second.first);
    }
    template <typename T>
    void setShared(ObjectId id, std::shared_ptr<T> p) {
        userData_[id] = {std::move(p), &typeid(T)};
    }
    void dropUserData(ObjectId id) { userData_.erase(id); }

    /// wp_viewport -> wl_surface ownership.
    ObjectId viewportOwner(ObjectId viewportId) const {
        auto it = viewportOwners_.find(viewportId);
        return it == viewportOwners_.end() ? 0 : it->second;
    }
    void setViewportOwner(ObjectId viewportId, ObjectId surfaceId) {
        viewportOwners_[viewportId] = surfaceId;
    }

    uint32_t version(ObjectId id) const {
        auto it = objects_.find(id);
        return it == objects_.end() ? 0 : it->second.version;
    }

    /// Protocol modules iterate the object table (e.g. XdgShell collects
    /// xdg_toplevel objects each frame).
    std::map<ObjectId, Object>& objectTable() { return objects_; }

    /// Pending outgoing bytes (flushed by the event loop's poll POLLOUT).
    std::vector<uint8_t>& outBuf() { return out_; }
    std::deque<int>& outFds() { return outFds_; }
    std::vector<uint8_t>& inBuf() { return in_; }
    std::deque<int>& inFdQueue() { return inFds_; }
    std::deque<int> pendingOutFds_; // attach on next successful chunk

private:
    friend class Compositor;
    bool dispatch(MessageHeader header, Reader payload, std::deque<int> fds);
    void postDeleteId(ObjectId id);

    std::deque<int> msgFds_; // fds for the in-flight message

    Compositor& compositor_;
    int fd_;
    int id_; // index for logging
    bool alive_ = true;
    uint32_t serial_ = 1;
    ObjectId nextObjectId_ = 2; // 0 invalid, 1 = wl_display

    std::vector<uint8_t> in_;
    std::vector<uint8_t> out_;
    std::deque<int> inFds_;
    std::deque<int> outFds_;

    std::map<ObjectId, Object> objects_;
    std::map<ObjectId, std::pair<std::shared_ptr<void>, const std::type_info*>> userData_;
    std::map<ObjectId, ObjectId> viewportOwners_;

public:
    /// Guards out_ / outFds_: events are marshaled from BOTH the game thread
    /// (sendEvent during update/input injection) and the event loop thread
    /// (flushClient). The compositor's updateMutex_ does NOT cover both paths.
    std::mutex ioMutex_;
    std::mutex& ioMutex() { return ioMutex_; }
};

} // namespace wlc
