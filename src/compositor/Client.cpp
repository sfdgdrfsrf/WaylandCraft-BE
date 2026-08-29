#include "compositor/Client.h"

#include "compositor/Server.h"
#include "util/Log.h"

#include <unistd.h>

namespace wlc {

Connection::Connection(Compositor& compositor, int fd, int id)
    : compositor_(compositor), fd_(fd), id_(id) {
    addObject(kWlDisplayId, ifaces::WlDisplay, 1, nullptr); // filled by Globals
}

Connection::~Connection() {
    // Release all fds still queued for transmission.
    for (int fd : outFds_) {
        if (fd >= 0) ::close(fd);
    }
    for (int fd : pendingOutFds_) {
        if (fd >= 0) ::close(fd);
    }
    for (int fd : msgFds_) {
        if (fd >= 0) ::close(fd);
    }
}

void Connection::kill() {
    if (!alive_) return;
    alive_ = false;
    // Grace: give the compositor loop a chance to flush before close.
    compositor_.markForClose(fd_);
}

ObjectId Connection::nextId() {
    return nextObjectId_++;
}

Object* Connection::object(ObjectId id) {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

void Connection::addObject(ObjectId id, const char* interface, uint32_t version,
                           RequestHandler handler) {
    Object obj;
    obj.id = id;
    obj.interface = interface;
    obj.version = version;
    obj.handler = std::move(handler);
    objects_[id] = std::move(obj);
}

void Connection::destroyObject(ObjectId id) {
    auto it = objects_.find(id);
    if (it == objects_.end()) return;
    objects_.erase(it);
    userData_.erase(id);
    postDeleteId(id);
}

void Connection::postDeleteId(ObjectId id) {
    Writer w;
    w.u32(id);
    sendEvent(kWlDisplayId, 1, w); // opcode 1 = wl_display.delete_id
}

void Connection::sendEvent(ObjectId target, uint16_t opcode, const Writer& body,
                           const std::vector<int>& fds) {
    if (!alive_) return;
    std::lock_guard<std::mutex> ioLock(ioMutex_);
    Writer w;
    writeHeader(w, target, opcode, static_cast<uint16_t>(8 + body.size()));
    w.raw(body.data(), body.size());
    out_.insert(out_.end(), w.data(), w.data() + w.size());
    for (int fd : fds) outFds_.push_back(fd);
}

void Connection::sendError(ObjectId object, uint32_t code, std::string_view message) {
    Writer w;
    w.objectId(object);
    w.u32(code);
    w.string(message);
    sendEvent(kWlDisplayId, 0, w); // opcode 0 = wl_display.error
    Log::warn(strf("client #%d: protocol error (obj %u, code %u): %.*s", id_, object, code,
                   static_cast<int>(message.size()), message.data()));
    kill();
}

ObjectId Connection::createCallback() {
    ObjectId id = nextId();
    addObject(id, ifaces::WlCallback, 1, nullptr);
    return id;
}

int Connection::takeFd() {
    if (msgFds_.empty()) return -1;
    int fd = msgFds_.front();
    msgFds_.pop_front();
    return fd;
}

void Connection::queueFd(int fd) {
    msgFds_.push_back(fd);
}

bool Connection::processIncoming() {
    if (!alive_) return false;

    // Fds received via SCM_RIGHTS ride the byte stream positionally: merge
    // them into the pending queue; dispatch consumes them via takeFd() in
    // wire order. Leftovers persist for later messages (e.g. a message whose
    // bytes split across recvmsg calls).
    for (int fd : inFds_) msgFds_.push_back(fd);
    inFds_.clear();

    size_t consumed = 0;
    while (alive_) {
        if (in_.size() - consumed < 8) break;
        MessageHeader h = parseHeader(in_.data() + consumed);
        if (h.size < 8) {
            sendError(kWlDisplayId, 0, "message size < header");
            return false;
        }
        if (in_.size() - consumed < h.size) break; // incomplete, wait for more

        Reader payload(in_.data() + consumed + 8, h.size - 8);

        // Dispatch may destroy objects or the connection itself.
        if (!dispatch(h, payload, {})) {
            if (alive_) sendError(kWlDisplayId, 0, "unhandled protocol error");
            break;
        }
        consumed += h.size;
    }

    if (consumed > 0) {
        in_.erase(in_.begin(), in_.begin() + static_cast<long>(consumed));
    }
    return alive_;
}

bool Connection::dispatch(MessageHeader header, Reader payload, std::deque<int> fds) {
    Object* obj = object(header.objectId);
    if (!obj) {
        Log::warn(strf("client #%d: request to unknown object %u", id_, header.objectId));
        return false;
    }
    if (!obj->handler) {
        // Still keep fds queued: a later message may own them.
        for (int fd : fds) msgFds_.push_back(fd);
        return true; // event-only object (e.g. wl_callback)
    }

    // Wayland fd passing is positional in the byte stream: fds for this
    // message may have arrived earlier and were left unconsumed by previous
    // dispatches, so APPEND (never close) — handlers consume via takeFd().
    for (int fd : fds) msgFds_.push_back(fd);
    RequestHandler h = obj->handler; // copy: handler may erase itself
    return h(*this, header.objectId, header.opcode, payload);
}

} // namespace wlc
