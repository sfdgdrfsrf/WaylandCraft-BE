// ============================================================================
//  WaylandCraft-BE — android/CaptureService.h
//
//  Server side of the CaptureProtocol: accepts connections from the
//  capture-helper companion APK (or a Termux helper script) and turns each
//  frame / icon payload into a compositor surface, exactly like upstream's
//  shm buffers appearing on a wl_surface after a commit.
//
//  Frame format (tools/capture-helper + docs/PROTOCOL.md):
//    magic      u32  0x574C4346 ('WLCF')
//    version    u32  1
//    msgType    u32  1=FRAME 2=ICON 3=HELLO 4=BYE
//    payloadId  u32  capture session id / package hash
//    width      u32
//    height     u32
//    stride     u32  (bytes per row)
//    format     u32  0=RGBA8888
//    payloadLen u32
//    payload    bytes
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace wlc {

class Compositor;

struct CaptureFrameHeader {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t msgType = 0;
    uint32_t payloadId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t format = 0;
    uint32_t payloadLen = 0;
};

inline constexpr uint32_t kCaptureMagic = 0x574C4346u; // 'WLCF'
inline constexpr uint32_t kCaptureVersion = 1;
inline constexpr uint32_t kMsgHello = 3;
inline constexpr uint32_t kMsgFrame = 1;
inline constexpr uint32_t kMsgIcon = 2;
inline constexpr uint32_t kMsgBye = 4;

class CaptureService {
public:
    using FrameSink = std::function<void(const CaptureFrameHeader&, std::vector<uint8_t>)>;

    explicit CaptureService(Compositor& comp) : comp_(comp) {}
    ~CaptureService() { stop(); }

    /// Listens on 127.0.0.1:`port` (loopback only — the companion runs on the
    /// same device). Returns false when the port can't be bound.
    bool start(int port);
    void stop();

    bool running() const { return running_.load(); }

    /// Receives each decoded frame (glue layer uploads to the texture pool
    /// and pins it to the matching surface).
    void setFrameSink(FrameSink sink);

private:
    void acceptLoop();
    void sessionThread(int fd);

    Compositor& comp_;
    FrameSink frameSink_;
    std::atomic<bool> running_{false};
    int listenFd_ = -1;
    std::thread acceptThread_;
    std::vector<std::thread> sessions_;
};

} // namespace wlc
