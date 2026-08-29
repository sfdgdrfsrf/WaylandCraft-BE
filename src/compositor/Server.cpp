// ============================================================================
//  WaylandCraft-BE — compositor/Server.cpp
//  Socket listeners + event loop. Unix domain (with SCM_RIGHTS fd passing)
//  on Linux/Android; optional TCP loopback for the Android companion /
//  Termux wayland-tcp bridge. Windows builds use the TCP path (AF_UNIX on
//  Windows cannot pass fds, and shm buffers ride inline there anyway —
//  mirroring upstream's "Linux-only features" stance).
// ============================================================================
#include "compositor/Server.h"

#include "compositor/Client.h"
#include "compositor/Globals.h"
#include "util/Log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace wlc {

namespace {
Compositor* g_activeCompositor = nullptr;
}

Compositor* Compositor::active() { return g_activeCompositor; }

namespace {

constexpr size_t kMaxIncoming = 1 << 22; // 4 MiB per-client input cap

int setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/// Sends as much as possible; returns bytes consumed (0 = would block / error).
/// Fds are attached to the first sendmsg of this chunk (Unix only).
size_t sendChunk(int fd, const uint8_t* data, size_t len, std::deque<int>& fds) {
    struct iovec iov { const_cast<uint8_t*>(data), len };
    struct msghdr msg {};
    char cmsgBuf[CMSG_SPACE(sizeof(int) * 16)];
    if (!fds.empty() && len > 0) {
        size_t n = std::min<size_t>(fds.size(), 16);
        msg.msg_control = cmsgBuf;
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(static_cast<socklen_t>(sizeof(int) * n));
        int* fdPtr = reinterpret_cast<int*>(CMSG_DATA(cmsg));
        size_t i = 0;
        for (const int& f : fds) {
            if (i >= n) break;
            fdPtr[i++] = f;
        }
        msg.msg_controllen = CMSG_LEN(static_cast<socklen_t>(sizeof(int) * n));
    }
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ssize_t n = sendmsg(fd, &msg, MSG_NOSIGNAL);
    if (n < 0) return 0;
    return static_cast<size_t>(n);
}

/// Returns 0 ok, -1 peer closed/errored.
int recvAll(int fd, std::vector<uint8_t>& out, std::deque<int>& fds) {
    uint8_t buf[16384];
    uint8_t cmsgBuf[CMSG_SPACE(sizeof(int) * 16)];
    while (true) {
        struct iovec iov { buf, sizeof(buf) };
        struct msghdr msg {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsgBuf;
        msg.msg_controllen = sizeof(cmsgBuf);
        ssize_t n = recvmsg(fd, &msg, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1; // peer closed
        out.insert(out.end(), buf, buf + n);
        for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                int* fdPtr = reinterpret_cast<int*>(CMSG_DATA(cmsg));
                for (size_t i = 0; i < count; ++i) fds.push_back(fdPtr[i]);
            }
        }
        if (n < static_cast<ssize_t>(sizeof(buf))) return 0;
        if (out.size() > kMaxIncoming) return -1;
    }
}

} // namespace

// ---------------------------------------------------------------------------
Compositor::Compositor() = default;

Compositor::~Compositor() {
    shutdown();
}

bool Compositor::start(const std::string& runtimeDir, const std::string& socketName,
                       int tcpPort) {
    if (running_.load()) return true;

    runtimeDir_ = runtimeDir;
    socketName_ = socketName;
    tcpPort_ = tcpPort;

    // Ensure runtime dir exists (XDG_RUNTIME_DIR equivalent).
    if (!runtimeDir_.empty()) {
        mkdir(runtimeDir_.c_str(), 0700); // best effort
    }

    socketPath_ = runtimeDir_.empty() ? socketName : runtimeDir_ + "/" + socketName;
    ::unlink(socketPath_.c_str());
    if (setupUnixListener(socketPath_)) {
        Log::info(strf("Wayland compositor running on %s", socketName_.c_str()));
    } else {
        Log::warn("unix wayland socket unavailable");
    }

    if (tcpPort_ > 0) {
        if (setupTcpListener(tcpPort_)) {
            Log::info(strf("wayland-tcp bridge listening on 127.0.0.1:%d", tcpPort_));
        } else {
            Log::warn(strf("wayland-tcp bridge failed on port %d", tcpPort_));
        }
    }

    if (unixListenFd_ < 0 && tcpListenFd_ < 0) return false;

    g_activeCompositor = this;
    running_.store(true);
    loopThread_ = std::thread([this] { eventLoop(); });
    return true;
}

void Compositor::shutdown() {
    if (!running_.exchange(false)) return;
    if (g_activeCompositor == this) g_activeCompositor = nullptr;
    if (loopThread_.joinable()) loopThread_.join();

    {
        std::lock_guard<std::mutex> lock(connMutex_);
        connections_.clear();
    }
    if (unixListenFd_ >= 0) ::close(unixListenFd_);
    if (tcpListenFd_ >= 0) ::close(tcpListenFd_);
    unixListenFd_ = tcpListenFd_ = -1;
    if (!socketPath_.empty() && socketPath_.find('/') != std::string::npos) {
        ::unlink(socketPath_.c_str());
    }
}

bool Compositor::setupUnixListener(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::chmod(path.c_str(), 0666) < 0 || // Termux/companion reachability
        ::listen(fd, 16) < 0) {
        ::close(fd);
        return false;
    }
    setNonBlocking(fd);
    unixListenFd_ = fd;
    return true;
}

bool Compositor::setupTcpListener(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only — never expose
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(fd, 16) < 0) {
        ::close(fd);
        return false;
    }
    setNonBlocking(fd);
    tcpListenFd_ = fd;
    return true;
}

void Compositor::eventLoop() {
    while (running_.load()) {
        std::vector<struct pollfd> pfds;
        int unixFd = unixListenFd_;
        int tcpFd = tcpListenFd_;
        if (unixFd >= 0) pfds.push_back({unixFd, POLLIN, 0});
        if (tcpFd >= 0) pfds.push_back({tcpFd, POLLIN, 0});

        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto& c : connections_) {
                if (c->alive()) pfds.push_back({c->fd(), POLLIN, 0});
            }
        }

        int n = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 16 /* ~60fps */);
        if (n < 0 && errno != EINTR) break;

        for (const auto& p : pfds) {
            if (p.revents & POLLIN) {
                if (p.fd == unixFd || p.fd == tcpFd) {
                    acceptNewClients(p.fd, p.fd == tcpFd);
                } else if (auto* c = connectionForFd(p.fd)) {
                    handleClientIo(*c, true);
                }
            }
            if (p.revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL)) {
                if (p.fd != unixFd && p.fd != tcpFd) {
                    if (auto* c = connectionForFd(p.fd)) handleClientIo(*c, false);
                }
            }
        }

        // Flush buffered output (incl. new events queued by dispatch).
        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto& c : connections_) {
                if (c->alive() && !c->outBuf().empty()) flushClient(*c);
            }
        }
        reapClosed();
    }
}

void Compositor::acceptNewClients(int listenFd, bool isTcp) {
    while (true) {
        int fd = ::accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) break;
        setNonBlocking(fd);
        if (isTcp) {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        }
        auto conn = std::make_shared<Connection>(*this, fd, static_cast<int>(connections_.size()));
        installDisplayHandlers(*conn); // wl_display + wl_registry
        std::lock_guard<std::mutex> lock(connMutex_);
        connections_.push_back(std::move(conn));
        Log::info(strf("wayland client connected (%s)", isTcp ? "tcp" : "unix"));
    }
}

bool Compositor::handleClientIo(Connection& c, bool readable) {
    if (readable) {
        int status = recvAll(c.fd(), c.inBuf(), c.inFdQueue());
        if (status != 0) {
            c.kill();
            return false;
        }
        if (c.inBuf().size() > kMaxIncoming) {
            c.sendError(kWlDisplayId, 0, "client flooded the compositor");
            c.kill();
            return false;
        }
        std::lock_guard<std::mutex> lock(updateMutex_);
        c.processIncoming();
    }
    if (c.alive() && !c.outBuf().empty()) flushClient(c);
    return c.alive();
}

void Compositor::flushClient(Connection& c) {
    std::lock_guard<std::mutex> ioLock(c.ioMutex());
    auto& out = c.outBuf();
    if (out.empty()) return;
    // Fds attach to the first sendmsg chunk that goes out.
    if (c.pendingOutFds_.empty() && !c.outFds_.empty()) {
        c.pendingOutFds_.swap(c.outFds_);
    }
    size_t sent = sendChunk(c.fd(), out.data(), out.size(), c.pendingOutFds_);
    if (sent == 0) return; // would block; POLLOUT retries
    if (!c.pendingOutFds_.empty()) {
        // Kernel consumed the ancillary fds together with the first byte.
        for (int fd : c.pendingOutFds_) {
            if (fd >= 0) ::close(fd);
        }
        c.pendingOutFds_.clear();
    }
    out.erase(out.begin(), out.begin() + static_cast<long>(sent));
}

void Compositor::markForClose(int fd) {
    (void)fd; // reapClosed() drops !alive() connections on next tick.
}

Connection* Compositor::connectionForFd(int fd) {
    std::lock_guard<std::mutex> lock(connMutex_);
    for (auto& c : connections_) {
        if (c->fd() == fd) return c.get();
    }
    return nullptr;
}

void Compositor::reapClosed() {
    std::lock_guard<std::mutex> lock(connMutex_);
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [](const std::shared_ptr<Connection>& c) { return !c->alive(); }),
        connections_.end());
}

// ---------------------------------------------------------------------------
// Game-side pump
// ---------------------------------------------------------------------------
void Compositor::update() {
    std::lock_guard<std::mutex> lock(updateMutex_);
    presentFrameCallbacks();
}

void Compositor::setOutputSize(int32_t width, int32_t height) {
    outW_ = width;
    outH_ = height;
}

std::vector<ToplevelRequest> Compositor::drainRequests() {
    std::lock_guard<std::mutex> lock(reqMutex_);
    return std::move(requests_);
}

void Compositor::pushRequest(ToplevelRequest req) {
    std::lock_guard<std::mutex> lock(reqMutex_);
    requests_.push_back(std::move(req));
}

void Compositor::onSurfaceCommitted(const SurfaceRef& surface) {
    std::lock_guard<std::mutex> lock(reqMutex_);
    committed_.push_back(surface);
}

std::vector<SurfaceRef> Compositor::drainCommittedSurfaces() {
    std::lock_guard<std::mutex> lock(reqMutex_);
    return std::move(committed_);
}

} // namespace wlc
