// ============================================================================
//  WaylandCraft-BE — tests/smoke_client.cpp
//
//  A dependency-free Wayland CLIENT that hand-shakes with the compositor the
//  same way a real Linux app would (connect → get_registry → bind → shm pool
//  → create_surface → xdg_toplevel → commit → frame callback). It is the CI
//  proof that the custom Wayland implementation speaks the real protocol.
// ============================================================================
#include "compositor/Server.h"
#include "compositor/XdgShell.h"
#include "util/Log.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace wlc;

struct ClientState {
    int fd = -1;
    std::vector<uint8_t> in;
    ObjectId nextId = 2;

    ObjectId registry = 0;
    ObjectId compositor = 0;
    ObjectId shm = 0;
    ObjectId seat = 0;
    ObjectId wmBase = 0;
    ObjectId surface = 0;
    ObjectId buffer = 0;
    ObjectId frameCb = 0;
    ObjectId xdgSurface = 0;
    ObjectId toplevel = 0;

    bool gotFrameDone = false;
    bool gotToplevelConfigure = false;
    bool gotXdgAck = false;
    std::vector<std::string> globalsSeen;

    ObjectId alloc() { return nextId++; }

    void send(ObjectId obj, uint16_t opcode, const Writer& body,
              const std::vector<int>& fds = {}) {
        Writer w;
        writeHeader(w, obj, opcode, static_cast<uint16_t>(8 + body.size()));
        w.raw(body.data(), body.size());
        struct iovec iov { const_cast<uint8_t*>(w.data()), w.size() };
        struct msghdr msg {};
        char cmsg[CMSG_SPACE(sizeof(int) * 4)];
        if (!fds.empty()) {
            msg.msg_control = cmsg;
            msg.msg_controllen = CMSG_SPACE(sizeof(int) * fds.size());
            struct cmsghdr* cm = CMSG_FIRSTHDR(&msg);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type = SCM_RIGHTS;
            cm->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
            memcpy(CMSG_DATA(cm), fds.data(), sizeof(int) * fds.size());
            msg.msg_controllen = CMSG_LEN(sizeof(int) * fds.size());
        }
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        ssize_t n = sendmsg(fd, &msg, 0);
        assert(n > 0);
        (void)n;
    }

    bool pump(int timeoutMs) {
        struct pollfd p { fd, POLLIN, 0 };
        int n = poll(&p, 1, timeoutMs);
        if (n <= 0) return false;
        uint8_t buf[8192];
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) return false;
        in.insert(in.end(), buf, buf + r);

        size_t off = 0;
        while (in.size() - off >= 8) {
            MessageHeader h = parseHeader(in.data() + off);
            if (in.size() - off < h.size) break;
            Reader payload(in.data() + off + 8, h.size - 8);
            handleEvent(h, payload);
            off += h.size;
        }
        if (off) in.erase(in.begin(), in.begin() + static_cast<long>(off));
        return true;
    }

    void handleEvent(MessageHeader h, Reader& a) {
        if (h.objectId == kWlDisplayId && h.opcode == 1) return; // delete_id
        if (h.objectId == registry) {
            if (h.opcode == 0) { // global(name u32, interface string, version u32)
                auto name = a.u32();
                auto iface = a.string();
                auto ver = a.u32();
                (void)name;
                (void)ver;
                if (!iface) return;
                globalsSeen.push_back(*iface);
                if (*iface == ifaces::WlCompositor) compositor = alloc();
                if (*iface == ifaces::WlShm) shm = alloc();
                if (*iface == ifaces::WlSeat) seat = alloc();
                if (*iface == ifaces::XdgWmBase) wmBase = alloc();
            }
        } else if (h.objectId == frameCb && h.opcode == 0) {
            gotFrameDone = true; // wl_callback.done
        } else if (h.objectId == toplevel && h.opcode == 0) {
            gotToplevelConfigure = true; // xdg_toplevel.configure(w,h,states)
            // Real clients then receive xdg_surface.configure(serial) and ack.
        } else if (h.objectId == xdgSurface && h.opcode == 0) {
            auto serial = a.u32();
            (void)serial;
            Writer w;
            w.u32(*serial);
            send(xdgSurface, 4, w); // xdg_surface.ack_configure
            gotXdgAck = true;
        }
    }
};

int runSmokeTest() {
    Log::installDefaultSink();

    Compositor comp;
    const std::string dir = "/tmp/wlc-smoke-" + std::to_string(getpid());
    const std::string sock = "wayland-0";
    if (!comp.start(dir, sock, 0)) {
        fprintf(stderr, "FAIL: compositor start\n");
        return 1;
    }

    ClientState cs;
    cs.fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", dir.c_str(), sock.c_str());
    assert(connect(cs.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0);

    // 1. wl_display.get_registry(new_id)
    cs.registry = cs.alloc();
    { Writer w; w.u32(cs.registry); cs.send(kWlDisplayId, 1, w); }
    for (int i = 0; i < 30 && cs.globalsSeen.size() < 10; ++i) cs.pump(50);
    assert(cs.compositor && cs.shm && cs.seat && cs.wmBase);
    printf("  registry: %zu globals advertised\n", cs.globalsSeen.size());

    // 2. bind wl_compositor + wl_shm + xdg_wm_base
    {
        Writer w; w.u32(1); w.string(ifaces::WlCompositor); w.u32(6); w.u32(cs.compositor);
        cs.send(cs.registry, 0, w);
    }
    {
        Writer w; w.u32(2); w.string(ifaces::WlShm); w.u32(1); w.u32(cs.shm);
        cs.send(cs.registry, 0, w);
    }
    {
        Writer w; w.u32(7); w.string(ifaces::XdgWmBase); w.u32(6); w.u32(cs.wmBase);
        cs.send(cs.registry, 0, w);
    }
    // 3. create surface
    cs.surface = cs.alloc();
    { Writer w; w.u32(cs.surface); cs.send(cs.compositor, 0, w); }
    // 4. shm pool + buffer (64x64 ARGB)
    constexpr int W = 64, H = 64;
    ObjectId poolId = cs.alloc();
    cs.buffer = cs.alloc();
    {
        int mem = memfd_create("smoke-shm", 0);
        assert(mem >= 0);
        assert(ftruncate(mem, W * H * 4) == 0);
        uint32_t* px =
            static_cast<uint32_t*>(mmap(nullptr, W * H * 4, PROT_WRITE, MAP_SHARED, mem, 0));
        for (int i = 0; i < W * H; ++i) px[i] = 0xFF3366CC;
        munmap(px, W * H * 4);
        Writer w;
        w.u32(poolId);
        w.i32(W * H * 4);
        cs.send(cs.shm, 0, w, {mem}); // wl_shm.create_pool(new_id, fd, size)
        close(mem);
    }
    {
        Writer w;
        w.u32(cs.buffer); w.i32(0); w.i32(W); w.i32(H); w.i32(W * 4); w.u32(0);
        cs.send(poolId, 0, w); // wl_shm_pool.create_buffer(new_id, off, w, h, stride, fmt)
    }
    // 5. xdg_surface + toplevel
    cs.xdgSurface = cs.alloc();
    { Writer w; w.u32(cs.xdgSurface); w.u32(cs.surface); cs.send(cs.wmBase, 2, w); }
    cs.toplevel = cs.alloc();
    { Writer w; w.u32(cs.toplevel); cs.send(cs.xdgSurface, 1, w); }
    { Writer t; t.string("Smoke Window"); cs.send(cs.toplevel, 2, t); }
    { Writer t; t.string("org.waylandcraft.smoke"); cs.send(cs.toplevel, 3, t); }
    // 6. frame callback + attach + commit
    cs.frameCb = cs.alloc();
    { Writer w; w.u32(cs.frameCb); cs.send(cs.surface, 3, w); }
    { Writer w; w.u32(cs.buffer); w.i32(0); w.i32(0); cs.send(cs.surface, 1, w); }
    { Writer w; cs.send(cs.surface, 6, w); } // commit

    for (int i = 0; i < 100 && !(cs.gotFrameDone && cs.gotToplevelConfigure); ++i) {
        // Simulate the game layer (WindowRegistry::update): size new toplevels
        // to fit the virtual output — exactly what the real port does per frame.
        comp.forEachConnection([&](Connection& c) {
            for (auto* ts : XdgShellModule::toplevels(c)) {
                if (!ts->hasPendingConfigure && !ts->mapped) {
                    XdgShellModule::sendToplevelConfigure(c, ts->id, 800, 600, false,
                                                          false, true);
                }
            }
        });
        comp.update();
        cs.pump(20);
    }
    printf("  frame done: %s, toplevel configure: %s, xdg ack sent: %s\n",
           cs.gotFrameDone ? "YES" : "no", cs.gotToplevelConfigure ? "YES" : "no",
           cs.gotXdgAck ? "YES" : "no");
    assert(cs.gotFrameDone);
    assert(cs.gotToplevelConfigure);

    // 7. resize request must reach the game layer with the implicit serial
    {
        Writer w;
        w.u32(cs.seat); w.u32(42); w.u32(ifaces::ResizeEdgeBottomRight);
        cs.send(cs.toplevel, 6, w);
    }
    bool sawResize = false;
    for (int i = 0; i < 20 && !sawResize; ++i) {
        auto reqs = comp.drainRequests();
        for (auto& r : reqs) {
            if (r.kind == ToplevelRequest::Kind::Resize && r.serial == 42) sawResize = true;
        }
        cs.pump(10);
    }
    printf("  resize request routed to game layer: %s\n", sawResize ? "YES" : "no");
    assert(sawResize);

    comp.shutdown();
    close(cs.fd);
    printf("SMOKE TEST PASSED\n");
    return 0;
}

#ifndef WLC_SMOKE_AS_LIB
#include <csignal>
#include <execinfo.h>
#include <cstdlib>
extern "C" void wlcSegvHandler(int sig) {
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n=== SIGNAL %d, backtrace (%d frames) ===\n", sig, n);
    backtrace_symbols_fd(bt, n, 2);
    _exit(139);
}
int main() {
    signal(SIGSEGV, wlcSegvHandler);
    return runSmokeTest();
}
#endif
