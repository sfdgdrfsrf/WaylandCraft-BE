// ============================================================================
//  WaylandCraft-BE — input/KeyCodes.h
//  Bedrock/Android key codes -> evdev scancodes (XKB adds +8).
//  Upstream forwarded GLFW scancodes directly (correctScancode adds 8 on
//  Wayland); the Bedrock port maps from Android keycodes at the input hook.
// ============================================================================
#pragma once

#include <cstdint>

namespace wlc {

// evdev base codes we forward (subset used by desktop apps)
namespace evdev {
inline constexpr uint32_t BtnLeft = 0x110;
inline constexpr uint32_t BtnRight = 0x111;
inline constexpr uint32_t BtnMiddle = 0x112;
inline constexpr uint32_t KeyEsc = 1;
inline constexpr uint32_t Key1 = 2;
inline constexpr uint32_t KeyQ = 16;
inline constexpr uint32_t KeyW = 17;
inline constexpr uint32_t KeyG = 34;
inline constexpr uint32_t KeyB = 48;
inline constexpr uint32_t KeyV = 47;
} // namespace evdev

/// wp_cursor_shape_device_v1 shape values — port of upstream CursorShape.
namespace cursorshape {
inline constexpr int Hide = 0;
inline constexpr int Default = 1;
inline constexpr int Help = 3;
inline constexpr int Pointer = 4;
inline constexpr int Wait = 6;
inline constexpr int Text = 9;
inline constexpr int VerticalText = 10;
inline constexpr int EastResize = 18;
inline constexpr int NorthResize = 19;
inline constexpr int WestResize = 22;
inline constexpr int SouthResize = 23;
inline constexpr int NwResize = 24;
inline constexpr int NeResize = 25;
inline constexpr int EwResize = 26;
inline constexpr int NsResize = 27;
inline constexpr int NeswResize = 28;
inline constexpr int NwseResize = 29;
inline constexpr int AllResize = 36;
inline constexpr int ZoomIn = 33;
inline constexpr int ZoomOut = 34;
} // namespace cursorshape

/// Android KeyEvent keycode -> evdev scancode (0 = unmapped).
/// Covers the printable ASCII band + modifiers; everything else falls back
/// to 0 and is dropped (matches upstream scancode filtering).
inline uint32_t androidKeyToEvdev(int32_t androidKey) {
    // AKEYCODE range 29-54 maps A..Z -> KEY_Q(16)+offset ordering in evdev:
    // Q=16,W=17,E=18,R=19,T=20,Y=21,U=22,I=23,O=24,P=25,
    // A=30,S=31,D=32,F=33,G=34,H=35,J=36,K=37,L=38,Z=44,X=45,C=46,V=47,B=48,N=49,M=50
    if (androidKey >= 29 && androidKey <= 54) { // A..Z
        static const uint32_t letterMap[26] = {
            30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
            49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 53};
        return letterMap[androidKey - 29];
    }
    if (androidKey >= 7 && androidKey <= 16) return uint32_t(androidKey - 7) + 2; // 0-9 -> KEY_1..0
    switch (androidKey) {
    case 111: return 1;  // ESC
    case 66: return 28;  // ENTER -> KEY_ENTER
    case 67: return 14;  // BACKSPACE -> KEY_BACKSPACE
    case 62: return 42;  // SHIFT_LEFT
    case 113: return 29; // CTRL_LEFT
    case 57: return 56;  // ALT_LEFT
    case 62 + 1000: return 42;
    case 61: return 39;  // SEMICOLON
    case 56: return 12;  // PERIOD handled below
    case 52: return 51;  // COMMA -> KEY_COMMA
    case 54: return 52;  // PERIOD -> KEY_DOT
    case 55: return 53;  // SLASH -> KEY_SLASH
    case 69: return 12;  // MINUS -> KEY_MINUS
    case 70: return 13;  // EQUALS
    case 71: return 26;  // LEFT_BRACKET
    case 72: return 27;  // RIGHT_BRACKET
    case 68: return 43;  // BACKSLASH / GRAVE handling upstream
    case 75: return 57;  // SPACE -> KEY_SPACE
    case 123: return 110;// INSERT
    case 122: return 102;// HOME
    case 92: return 104; // PAGE_UP
    case 112: return 111;// FORWARD_DEL
    case 93: return 107; // END
    case 93 + 1: return 109; // PAGE_DOWN
    case 19: return 103; // DPAD_UP
    case 20: return 108; // DPAD_DOWN
    case 21: return 105; // DPAD_LEFT
    case 22: return 106; // DPAD_RIGHT
    case 131: return 59; // F1
    default: return 0;
    }
}

/// GLFW/Win32 scancode passthrough helper (desktop client builds): Wayland
/// scancodes are evdev, XKB keycodes are evdev+8 (upstream correctScancode).
inline uint32_t glfwToEvdev(uint32_t glfwScancode) { return glfwScancode; }

/// Mouse button index (0=left) -> evdev button code (upstream: 272 + glfw).
inline uint32_t mouseButtonToEvdev(uint32_t index) { return 0x110 + index; }

} // namespace wlc
