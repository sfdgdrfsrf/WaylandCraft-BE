// ============================================================================
//  WaylandCraft-BE — render/ClientHooks.cpp (client target)
//
//  Keybinds — port of upstream's three Fabric keybindings:
//    App Launcher   V  (waylandcraft.key.appLauncher)
//    Window Manager B  (waylandcraft.key.windowManager)
//    Capture Keyboard G (waylandcraft.key.captureKeyboard) — soft capture;
//    hard capture stays ALT+Q exactly like upstream.
//
//  Mouse/keyboard forwarding: Bedrock's client KeyInputEvent/MouseInputEvent
//  (client event headers) route into the compositor seat — the crosshair is
//  the Wayland cursor (upstream design preserved).
// ============================================================================
#include "render/ClientHooks.h"

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

std::atomic<bool> gKeyboardCapture{false};      // soft capture (G key)
std::atomic<bool> gHardCapture{false};          // ALT+Q (upstream HARD_CAPTURE)
ll::event::ListenerPtr gKeyListener;
ll::event::ListenerPtr gMouseListener;

bool handleKey(int32_t androidKey, bool pressed, int32_t modifiers) {
    auto& reg = Mod::registry();

    // --- UI toggles (only when NOT captured; mirrors upstream keybinds) ---
    if (!gKeyboardCapture.load()) {
        if (pressed) {
            // V = App Launcher, B = Window Manager
            if (androidKey == 47) { // V (AKEYCODE_V)
                AppLauncher::toggle();
                return true;
            }
            if (androidKey == 48) { // B (AKEYCODE_B)
                WindowManagerUI::toggle();
                return true;
            }
            if (androidKey == 34) { // G (AKEYCODE_G)
                gKeyboardCapture.store(true);
                return true;
            }
            if (androidKey == 16 && (modifiers & 0x02)) { // Q with ALT
                gHardCapture.store(true);
                return true;
            }
        }
        return false;
    }

    // --- capture modes -------------------------------------------------------
    if (gHardCapture.load() && androidKey == 16 && (modifiers & 0x02) && !pressed) {
        gHardCapture.store(false); // ALT+Q exits hard capture
        return true;
    }
    if (gKeyboardCapture.load() && !gHardCapture.load() && androidKey == 111 && pressed) {
        gKeyboardCapture.store(false); // ESC exits soft capture
        return true;
    }

    // Forward to the focused window via the seat (evdev scancodes).
    uint32_t evdev = androidKeyToEvdev(androidKey);
    if (evdev != 0) {
        Compositor::keyboardKey(evdev, pressed);
    }
    (void)reg;
    return true; // consumed
}

} // namespace

bool installClientHooks() {
    auto& bus = ll::event::EventBus::getInstance();

    if (!HudRenderer::install()) return false;

    gKeyListener = bus.emplaceListener<ll::event::KeyInputEvent>(
        [](ll::event::KeyInputEvent& ev) {
            if (handleKey(ev.key(), ev.action() != 0, ev.modifiers())) {
                ev.cancel(); // game never sees forwarded keys (upstream behavior)
            }
        });

    gMouseListener = bus.emplaceListener<ll::event::MouseInputEvent>(
        [](ll::event::MouseInputEvent& ev) {
            auto& reg = Mod::registry();
            // Left-click press begins an implicit grab with the seat serial —
            // the same choreography upstream's PointerGrabMap.startImplicit.
            if (ev.button() >= 0 && ev.action() != 0) {
                uint32_t serial =
                    Compositor::pointerButton(mouseButtonToEvdev(ev.button()), true);
                if (auto* d = reg.mostRecentFocused()) {
                    reg.grabMap().startImplicit(d->root, mouseButtonToEvdev(ev.button()),
                                                serial, Vec3{}, 0, 0);
                }
            } else if (ev.button() >= 0) {
                Compositor::pointerButton(mouseButtonToEvdev(ev.button()), false);
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
