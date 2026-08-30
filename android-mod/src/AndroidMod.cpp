// ============================================================================
//  WaylandCraft-BE — android-mod/src/AndroidMod.cpp
//
//  LeviLaunchroid native mod entry (preloader-android SDK, `pl::mod`).
//
//  WHY A SECOND ENTRY? LeviLamina 26.20 has no Android target (client is
//  windows|x64-only, server adds linux|x86_64 — see LeviLamina/xmake.lua),
//  so the full LL event/command/render API cannot run on a phone. What CAN
//  run is the pure-POSIX core this repo was structured around (see
//  util/Log.h: "keeps the entire compositor stack free of LeviLamina
//  headers so it can be cross-compiled anywhere"):
//
//    * compositor/            — real Wayland wire protocol (Unix socket @
//                               $XDG_RUNTIME_DIR + TCP loopback 7231)
//    * platform/AndroidBridge — `pm list packages` scan + `am start` launch
//    * android/CaptureService — WLCF frame ingest on 127.0.0.1:7232
//
//  The .so exports PLGetModRegistration (PL_REGISTER_MOD) which the
//  launcher's preloader resolves via dlsym after System.load(). Bedrock-side
//  integration (HUD, commands, RenderDragon hooks) is phase 2 — tracked in
//  docs/PORT_NOTES; this lane already gives the device a working Wayland
//  compositor you can drive from Termux:
//
//    pkg install python;  python wayland-tcp-bridge.py &
//    WAYLAND_DISPLAY=wayland-0 foot
// ============================================================================

#include "pl/Mod.hpp" // vendored preloader-android SDK (see android-mod/pl-include)

#include "AndroidConfig.h"
#include "android/CaptureService.h"
#include "compositor/Server.h"
#include "config/Config.h"
#include "platform/PlatformBridge.h"
#include "util/Log.h"
#include "Version.h"

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

using wlc::LogLevel;

void installLogcatSink() {
    wlc::Log::setSink([](LogLevel level, std::string_view msg) {
        const android_LogPriority prio = level == LogLevel::Debug  ? ANDROID_LOG_DEBUG
                                         : level == LogLevel::Info ? ANDROID_LOG_INFO
                                         : level == LogLevel::Warn ? ANDROID_LOG_WARN
                                                                   : ANDROID_LOG_ERROR;
        __android_log_print(prio, "WaylandCraftBE", "[wlc] %.*s",
                            static_cast<int>(msg.size()), msg.data());
    });
}

/// Drives Compositor::update() at ~30 Hz. In the LeviLamina builds the
/// game's render thread does this once per frame; on this lane there are no
/// RenderDragon hooks yet, so a timer thread stands in. That keeps frame
/// callbacks flowing for real Wayland clients (foot, weston-terminal, mpv
/// --vo=wlshm through the TCP bridge) even though nothing consumes the
/// window-manager requests yet.
class CompositorPump {
public:
    void start(wlc::Compositor& comp) {
        stop();
        comp_ = &comp;
        running_.store(true);
        thread_ = std::thread([this] {
            using clock = std::chrono::steady_clock;
            auto next = clock::now();
            const auto dt = std::chrono::milliseconds(33); // ~30 Hz
            while (running_.load()) {
                if (comp_ && comp_->running()) comp_->update();
                next += dt;
                std::this_thread::sleep_until(next);
            }
        });
    }

    void stop() {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
        comp_ = nullptr;
    }

private:
    wlc::Compositor* comp_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

/// The pl-SDK lifecycle object. Mirrors src/Mod.cpp enable()/disable()
/// minus every LeviLamina-API surface (commands, HUD renderer, window
/// items, scriptevents) — those stay Windows/Linux-lane only for now.
class WaylandCraftMod {
public:
    bool load(pl::mod::ModContext& context) {
        installLogcatSink();

        // Config lives in the launcher-managed mod root: <modroot>/config/.
        // Defaults come from wlc::Config's in-class initializers, then an
        // optional flat config.json overrides individual keys.
        const fs::path configPath = context.configDir() / "config.json";
        wlc::AndroidConfig::loadFromFile(configPath.string());

        auto& cfg = wlc::Config::get();
        if (cfg.runtimeDir.empty()) {
            // Default runtime dir inside the mod's own data area: the game
            // process owns it, other apps can't path-probe the socket.
            cfg.runtimeDir = (context.dataDir() / "runtime").string();
        }

        bridge_ = wlc::makePlatformBridge();
        wlc::Log::info(wlc::strf("WaylandCraft-BE %s (android arm64) — platform bridge: %s",
                                 WLC_VERSION, bridge_->platformName()));
        return true;
    }

    bool enable(pl::mod::ModContext&) {
        auto& cfg = wlc::Config::get();

        // Publish $WAYLAND_DISPLAY before anything we launch could inherit
        // it (upstream choreography: apps join the compositor because the
        // environment carries the socket name).
        setenv("WAYLAND_DISPLAY", cfg.socketName.c_str(), 1);
        setenv("XDG_RUNTIME_DIR", cfg.runtimeDir.c_str(), 1);

        int tcpPort = cfg.tcpBridgeEnabled ? cfg.tcpBridgePort : 0;
        compositorRunning_ = compositor_.start(cfg.runtimeDir, cfg.socketName, tcpPort);
        if (compositorRunning_) {
            wlc::Log::info(wlc::strf("Wayland compositor running on %s (tcp %s)",
                                     compositor_.socketPath().c_str(),
                                     tcpPort ? std::to_string(compositor_.tcpPort()).c_str()
                                             : "off"));
        } else {
            wlc::Log::error("compositor socket failed — app windows disabled");
        }

        pump_.start(compositor_);

        if (cfg.captureEnabled && compositorRunning_) {
            capture_ = std::make_unique<wlc::CaptureService>(compositor_);
            if (capture_->start(cfg.capturePort)) {
                // No HudRenderer on this lane: frame payloads already become
                // compositor surfaces inside CaptureService itself; the sink
                // would only feed the game texture pool (phase 2).
                wlc::Log::info(wlc::strf("WLCF capture service listening on 127.0.0.1:%d",
                                         cfg.capturePort));
            } else {
                wlc::Log::warn("capture service port bind failed");
            }
        }

        // Background probe of the app list — proves AndroidBridge works on
        // this device and warms the launcher data path. Results only in
        // logcat for now (the in-game launcher UI is phase 2).
        std::thread([this] {
            if (!bridge_) return;
            if (!bridge_->featuresAvailable()) {
                wlc::Log::warn(wlc::strf("bridge: %s", bridge_->statusLine().c_str()));
                return;
            }
            auto apps = bridge_->scanApps();
            wlc::Log::info(wlc::strf("bridge: %zu launchable apps scanned (%s)",
                                     apps.size(), bridge_->statusLine().c_str()));
        }).detach();

        return compositorRunning_; // pump/capture are optional services
    }

    bool disable(pl::mod::ModContext&) {
        pump_.stop();
        if (capture_) capture_->stop();
        capture_.reset();
        if (compositorRunning_) compositor_.shutdown();
        compositorRunning_ = false;
        wlc::Log::info("compositor stopped");
        return true;
    }

    bool unload(pl::mod::ModContext&) {
        bridge_.reset();
        return true;
    }

private:
    wlc::Compositor compositor_;
    CompositorPump pump_;
    std::unique_ptr<wlc::CaptureService> capture_;
    std::unique_ptr<wlc::PlatformBridge> bridge_;
    bool compositorRunning_ = false;
};

WaylandCraftMod& modInstance() {
    static WaylandCraftMod inst;
    return inst;
}

} // namespace

PL_REGISTER_MOD(WaylandCraftMod, modInstance())
