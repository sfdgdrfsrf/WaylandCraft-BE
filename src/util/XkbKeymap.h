// ============================================================================
//  WaylandCraft-BE — util/XkbKeymap.h
//  Compiled-in default XKB keymap served to clients over wl_keyboard.keymap.
//  Upstream loads `<gamedir>/waylandcraft/keymap.txt` or dumps the host
//  layout via `xkbcli dump-keymap`; the Bedrock port ships a minimal but
//  valid US-ANSI keymap covering letters/digits/modifiers and supports the
//  same file override at <gamedir>/waylandcraft-be/keymap.txt (loaded by
//  Mod.cpp and pushed via SeatModule::pushKeymap).
// ============================================================================
#pragma once

#include <string>

namespace wlc {

/// Minimal US layout keymap (evdev scancodes, XKB v1 wire format compatible).
const char* defaultXkbKeymap();

/// Serves `keymapText` to all currently bound keyboards (file override path).
void useKeymapText(const std::string& keymapText);

} // namespace wlc
