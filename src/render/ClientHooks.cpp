// ============================================================================
//  WaylandCraft-BE — render/ClientHooks.cpp (client target)
//
//  Keybinds — port of upstream's three Fabric keybindings:
//    App Launcher   V
//    Window Manager B
//    Capture Keyboard G (soft capture)
//    Hard capture: upstream used ALT+Q; Bedrock's KeyInputEvent carries no
//    modifier state, so hard capture is G-while-soft-captured instead (G is
//    the capture key either way, so nothing is lost to the app).
//
//  Mouse/keyboard forwarding: Bedrock's client KeyInputEvent/MouseInputEvent
//  (src-client event headers, 26.20) route into the compositor seat — the
//  crosshair is the Wayland cursor (upstream design preserved).
//
//  CLIENT-ONLY TU: guarded out on server targets (input event headers ship
//  with src-client only).
// ============================================================================
#include "render/ClientHooks.h"

#if defined(WLC_CLIENT) && !defined(WLC_SERVER)

#include "Mod.h"
#include "compositor/Server.h"
#include "config/Config.h"
#include "core/WindowRegistry.h"
#include "input/KeyCodes.h"
#include "render/HudRenderer.h"
#include "ui/AppLauncher.h"
#include "ui/WindowManagerUI.h"
#include "util/Log.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"

#include <algorithm>
#include <atomic>

namespace wlc {

namespace {

// Key codes: desktop clients report VK codes (== ASCII uppercase for
// letters); AKEYCODE equivalents differ (V=50, B=31, G=35, ESC=111) and are
// re-anchored when the LeviLaunchroid client lane is validated.
constexpr int32_t kKeyAppLauncher  = 'V'; // VK_V      (AKEYCODE_V)
constexpr int32_t kKeyWindowMgr    = 'B'; // VK_B      (AKEYCODE_B)
constexpr int32_t kKeyCapture      = 'G'; // VK_G      (AKEYCODE_G)
constexpr int32_t kKeyExitCapture  = 27;  // VK_ESCAPE (AKEYCODE_ESCAPE)

std::atomic<bool> gKeyboardCapture{false};      // soft capture (G)
std::atomic<bool> gHardCapture{false};          // G while soft-captured
ll::event::ListenerPtr gKeyListener;
ll::event::ListenerPtr gMouseListener;

bool handleKey(int32_t key, bool pressed) {
    auto& reg = Mod::registry();

    if (pressed) {
        if (key == kKeyExitCapture) {
            // ESC backs out one capture level (hard -> soft -> none).
            if (gHardCapture.load()) gHardCapture.store(false);
            else gKeyboardCapture.store(false);
            return true;
        }
        if (key == kKeyCapture) {
            if (!gKeyboardCapture.load()) {
                gKeyboardCapture.store(true); // enter soft capture
                return true;
            }
            if (!gHardCapture.load()) {
                gHardCapture.store(true); // escalate to hard capture
                return true;
            }
            return false; // fully captured: G forwards to the app
        }
        if (!gKeyboardCapture.load()) {
            if (key == kKeyAppLauncher) {
                AppLauncher::toggle();
                return true;
            }
            if (key == kKeyWindowMgr) {
                WindowManagerUI::toggle();
                return true;
            }
            return false;
        }
    }

    // --- capture modes -------------------------------------------------------
    if (gHardCapture.load() && key == kKeyExitCapture && !pressed) {
        gHardCapture.store(false); // (release edge; state already handled above)
        return true;
    }

    // Forward to the focused window via the seat (evdev scancodes).
    uint32_t evdev = androidKeyToEvdev(key);
    if (evdev != 0) {
        Compositor::keyboardKey(evdev, pressed);
    }
    return true; // consumed while captured
}

} // namespace

bool installClientHooks() {
    auto& bus = ll::event::EventBus::getInstance();

    if (!HudRenderer::install()) return false;

    gKeyListener = bus.emplaceListener<ll::event::KeyInputEvent>(
        [](ll::event::KeyInputEvent& ev) {
            if (handleKey(ev.keyCode(), ev.isDown())) {
                ev.cancel(); // game never sees forwarded keys (upstream behavior)
            }
        });

    gMouseListener = bus.emplaceListener<ll::event::MouseInputEvent>(
        [](ll::event::MouseInputEvent& ev) {
            auto& reg = Mod::registry();
            // 26.20 mouse surface: actionButtonId = button id (0 = none),
            // buttonData = press state. Left-click press begins an implicit
            // grab with the seat serial — the same choreography as upstream's
            // PointerGrabMap.startImplicit.
            const int  button  = static_cast<int>(ev.actionButtonId());
            const bool pressed = ev.buttonData() != 0;
            if (button > 0 && pressed) {
                uint32_t serial =
                    Compositor::pointerButton(mouseButtonToEvdev(button), true);
                if (auto* d = reg.mostRecentFocused()) {
                    reg.grabMap().startImplicit(d->root, mouseButtonToEvdev(button),
                                                serial, Vec3{}, 0, 0);
                }
            } else if (button > 0) {
                Compositor::pointerButton(mouseButtonToEvdev(button), false);
            }
        });

    Log::info("client hooks installed");
    return true;
}

void removeClientHooks() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gKeyListener) bus.removeListener(gKeyListener);
    if (gMouseListener) bus.removeListener(gMouseListener);
    gKeyListener = gMouseListener = nullptr;
    HudRenderer::remove();
}

} // namespace wlc

#endif // WLC_CLIENT && !WLC_SERVER
