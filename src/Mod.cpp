// ============================================================================
//  WaylandCraft-BE — src/Mod.cpp
//
//  Lifecycle wiring:
//    load()   — read config, create the compositor, pick the platform bridge
//    enable() — register commands (both targets), start listeners, start the
//               compositor socket + capture service; on client builds install
//               the HUD renderer and keybinds
//    disable()— reverse everything cleanly
//
//  Upstream parity (WaylandCraftCommon.java):
//    - WindowItem registration        -> item/WindowItem (BP item + NBT)
//    - WaylandCraftNetworking         -> net/Sync (scriptevent channel)
//    - ServerTickEvents level hook    -> ServerItemManager GC cadence
//  ============================================================================
#include "Mod.h"

#include "commands/Commands.h"
#include "config/Config.h"
#include "core/WindowRegistry.h"
#include "render/ClientHooks.h"
#include "util/Log.h"
#include "Version.h"

#include "ll/api/mod/RegisterHelper.h"

#include "compositor/Server.h"
#include "platform/PlatformBridge.h"
#include "render/HudRenderer.h"

#if defined(WLC_ANDROID) && defined(__ANDROID__)
#include "android/CaptureService.h"
#endif

#include <memory>

namespace wlc {

struct Mod::State {
    Compositor compositor;
    std::unique_ptr<WindowRegistry> registry;
    std::unique_ptr<PlatformBridge> bridge;
    bool compositorRunning = false;
#if defined(WLC_ANDROID) && defined(__ANDROID__)
    std::unique_ptr<CaptureService> capture;
#endif
};

Mod::Mod(ll::mod::NativeMod& self) : self_(self) {}
Mod::~Mod() = default;

Mod& Mod::instance() {
    // 26.20: NativeMod::current() returns shared_ptr<NativeMod>; the pointer
    // is owned by the mod manager for the module's lifetime, so binding a
    // reference here stays valid for as long as this mod is loaded.
    static Mod inst{*ll::mod::NativeMod::current()};
    return inst;
}

bool Mod::load() {
    Log::setSink([this](LogLevel level, std::string_view msg) {
        auto& logger = self_.getLogger();
        switch (level) {
        case LogLevel::Debug: logger.debug("[wlc] {}", msg); break;
        case LogLevel::Info: logger.info("[wlc] {}", msg); break;
        case LogLevel::Warn: logger.warn("[wlc] {}", msg); break;
        case LogLevel::Error: logger.error("[wlc] {}", msg); break;
        }
    });

    if (!Config::load()) {
        self_.getLogger().warn("config invalid, defaults applied");
    }

    state_ = std::make_unique<State>();
    state_->bridge = makePlatformBridge();
    state_->registry = std::make_unique<WindowRegistry>(state_->compositor);

    self_.getLogger().info("WaylandCraft-BE {} — platform bridge: {}", WLC_VERSION,
                           state_->bridge->platformName());
    return true;
}

bool Mod::enable() {
    auto& cfg = Config::get();

    // Publish $WAYLAND_DISPLAY BEFORE any app launch (upstream: apps join our
    // compositor because their environment carries the socket name).
#if !defined(_WIN32)
    setenv("WAYLAND_DISPLAY", cfg.socketName.c_str(), 1);
    setenv("XDG_RUNTIME_DIR", cfg.runtimeDir.c_str(), 1);
#endif

    int tcpPort = cfg.tcpBridgeEnabled ? cfg.tcpBridgePort : 0;
    state_->compositorRunning =
        state_->compositor.start(cfg.runtimeDir, cfg.socketName, tcpPort);
    if (state_->compositorRunning) {
        self_.getLogger().info("Wayland compositor running on {}",
                               state_->compositor.socketPath());
    } else {
        self_.getLogger().error("compositor socket failed — app windows disabled");
    }

#if defined(WLC_ANDROID) && defined(__ANDROID__)
    if (cfg.captureEnabled) {
        state_->capture = std::make_unique<CaptureService>(state_->compositor);
        if (state_->capture->start(cfg.capturePort)) {
            state_->capture->setFrameSink([](const CaptureFrameHeader& h,
                                             std::vector<uint8_t> px) {
                HudRenderer::onFrameUpdated(h.payloadId, static_cast<int>(h.width),
                                            static_cast<int>(h.height), px.data(),
                                            static_cast<int>(h.stride));
            });
        }
    }
#endif

    bool ok = true;
    ok &= Commands::registerAll();
#if defined(WLC_CLIENT) && !defined(WLC_SERVER)
    ok &= installClientHooks();  // HUD + keybinds + input forwarding (client)
#else
    ok &= installServerHooks();  // sync channel + item GC cadence (server)
#endif
    return ok;
}

bool Mod::disable() {
#if defined(WLC_CLIENT) && !defined(WLC_SERVER)
    removeClientHooks();
#else
    removeServerHooks();
#endif
    if (state_) {
#if defined(WLC_ANDROID) && defined(__ANDROID__)
        if (state_->capture) state_->capture->stop();
#endif
        state_->compositor.shutdown();
        state_->compositorRunning = false;
    }
    Commands::unregisterAll();
    return true;
}

bool Mod::unload() {
    state_.reset();
    return true;
}

WindowRegistry& Mod::registry() { return *instance().state_->registry; }
Compositor& Mod::compositor() { return instance().state_->compositor; }
PlatformBridge& Mod::bridge() { return *instance().state_->bridge; }

} // namespace wlc

LL_REGISTER_MOD(wlc::Mod, wlc::Mod::instance());
