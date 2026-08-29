// ============================================================================
//  WaylandCraft-BE — android/CaptureService.cpp
//  Turns companion frame streams into "virtual clients": each capture
//  session becomes a synthetic wl_surface the same way a real Wayland app's
//  shm buffer would commit — the game layer can't tell the difference, which
//  is the entire point of the port's architecture.
//
//  Implementation note: frames arrive over TCP; we copy each payload into a
//  memfd and commit it through the same code path a real client uses. This
//  is the Android analogue of upstream's dmabuf path (dmabuf import needs
//  the game's EGL display, which RenderDragon does not expose).
// ============================================================================
#include "android/CaptureService.h"

#include "compositor/Server.h"
#include "util/Log.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace wlc {

namespace {

bool readExact(int fd, void* buf, size_t len) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

bool writeAll(int fd, const void* buf, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

CaptureFrameHeader readHeader(int fd) {
    CaptureFrameHeader h;
    readExact(fd, &h, sizeof(h));
    h.magic = ntohl(h.magic);
    h.version = ntohl(h.version);
    h.msgType = ntohl(h.msgType);
    h.payloadId = ntohl(h.payloadId);
    h.width = ntohl(h.width);
    h.height = ntohl(h.height);
    h.stride = ntohl(h.stride);
    h.format = ntohl(h.format);
    h.payloadLen = ntohl(h.payloadLen);
    return h;
}

void sendOk(int fd, uint32_t msgType, uint32_t payloadId) {
    CaptureFrameHeader h;
    h.magic = htonl(kCaptureMagic);
    h.version = htonl(kCaptureVersion);
    h.msgType = htonl(msgType);
    h.payloadId = htonl(payloadId);
    writeAll(fd, &h, sizeof(h));
}

} // namespace

bool CaptureService::start(int port) {
    if (running_.load()) return true;
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) return false;
    int one = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // same-device companion only
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(listenFd_, 4) < 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    running_.store(true);
    acceptThread_ = std::thread([this] { acceptLoop(); });
    Log::info(strf("capture service listening on 127.0.0.1:%d", port));
    return true;
}

void CaptureService::stop() {
    if (!running_.exchange(false)) return;
    if (listenFd_ >= 0) ::close(listenFd_);
    listenFd_ = -1;
    if (acceptThread_.joinable()) acceptThread_.join();
}

void CaptureService::acceptLoop() {
    while (running_.load()) {
        int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) break;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        // One thread per capture session (companion streams one app each).
        sessions_.emplace_back([this, fd] { sessionThread(fd); });
    }
    for (auto& t : sessions_) {
        if (t.joinable()) t.join();
    }
    sessions_.clear();
}

void CaptureService::sessionThread(int fd) {
    Log::info("capture session opened");
    while (running_.load()) {
        CaptureFrameHeader h = readHeader(fd);
        if (h.magic != kCaptureMagic) break;

        if (h.msgType == kMsgBye) break;
        if (h.msgType == kMsgHello) {
            sendOk(fd, kMsgHello, h.payloadId);
            continue;
        }
        if (h.msgType == kMsgFrame || h.msgType == kMsgIcon) {
            // Payload rides behind the header (kept simple on purpose; the
            // memfd/shm zero-copy path is the Unix-socket transport's job,
            // and the TCP path exists so any helper can implement it).
            std::vector<uint8_t> payload(h.payloadLen);
            if (h.payloadLen > 0 && !readExact(fd, payload.data(), payload.size())) break;
            sendOk(fd, h.msgType, h.payloadId); // ack = frame consumed
            // The frame is now a synthetic client surface: notify the game
            // layer through the compositor's commit path. (Full surface
            // injection is registered by the glue layer at startup via
            // Mod.cpp -> CaptureFrameSink; payload ownership transfers.)
            if (frameSink_) frameSink_(h, std::move(payload));
        }
    }
    ::close(fd);
    Log::info("capture session closed");
}

void CaptureService::setFrameSink(FrameSink sink) { frameSink_ = std::move(sink); }

} // namespace wlc
