#include "util/XkbKeymap.h"

#include "compositor/Server.h"
#include "compositor/Client.h"
#include "compositor/Seat.h"
#include "util/Log.h"
#include "util/MemFd.h"

#include <cstring>
#include <string>
#include <unistd.h>

namespace wlc {

namespace {
std::string gKeymapOverride;

// A compact but spec-valid xkb_keymap: code names match evdev numbering so
// clients using xkbcommon key_from_keycode behave exactly like on desktop.
const char* kDefaultKeymap = R"xkb(xkb_keymap {
    xkb_keycodes "waylandcraft" {
        minimum = 8;
        maximum = 255;
        <ESC> = 9; <AE01> = 10; <AE02> = 11; <AE03> = 12; <AE04> = 13;
        <AE05> = 14; <AE06> = 15; <AE07> = 16; <AE08> = 17; <AE09> = 18;
        <AE10> = 19; <AE11> = 20; <AE12> = 21; <BKSP> = 22;
        <TAB> = 23; <AD01> = 24; <AD02> = 25; <AD03> = 26; <AD04> = 27;
        <AD05> = 28; <AD06> = 29; <AD07> = 30; <AD08> = 31; <AD09> = 32;
        <AD10> = 33; <AD11> = 34; <AD12> = 35; <RTRN> = 36; <LCTL> = 37;
        <AC01> = 38; <AC02> = 39; <AC03> = 40; <AC04> = 41; <AC05> = 42;
        <AC06> = 43; <AC07> = 44; <AC08> = 45; <AC09> = 46; <AC10> = 47;
        <AC11> = 48; <TLDE> = 49; <LFSH> = 50; <BKSL> = 51; <AB01> = 52;
        <AB02> = 53; <AB03> = 54; <AB04> = 55; <AB05> = 56; <AB06> = 57;
        <AB07> = 58; <AB08> = 59; <AB09> = 60; <AB10> = 61; <RTSH> = 62;
        <KPMU> = 63; <LALT> = 64; <SPCE> = 65; <CAPS> = 66; <FK01> = 67;
        <FK02> = 68; <FK03> = 69; <FK04> = 70; <FK05> = 71; <FK06> = 72;
        <FK07> = 73; <FK08> = 74; <FK09> = 75; <FK10> = 76; <NMLK> = 77;
        <SCLK> = 78; <KP7> = 79; <KP8> = 80; <KP9> = 81; <KPSU> = 82;
        <KP4> = 83; <KP5> = 84; <KP6> = 85; <KPAD> = 86; <KP1> = 87;
        <KP2> = 88; <KP3> = 89; <KP0> = 90; <KPEN> = 104; <KPDE> = 91;
        <KPEQ> = 125; <INS> = 118; <HOME> = 119; <PGUP> = 120; <DELE> = 121;
        <END> = 122; <PGDN> = 123; <UP> = 111; <LEFT> = 113; <DOWN> = 116;
        <RGHT> = 114; <LWIN> = 133; <RWIN> = 134; <COMP> = 135;
        alias <AC12> = <BKSL>;
    };
    xkb_types "complete";
    xkb_compatibility "complete";
    xkb_symbols "waylandcraft" {
        name[group1] = "English (US - WaylandCraft)";
        key <ESC> { [ Escape ] };
        key <AE01> { [ 1, exclam ] };
        key <AE02> { [ 2, at ] };
        key <AE03> { [ 3, numbersign ] };
        key <AE04> { [ 4, dollar ] };
        key <AE05> { [ 5, percent ] };
        key <AE06> { [ 6, asciicircum ] };
        key <AE07> { [ 7, ampersand ] };
        key <AE08> { [ 8, asterisk ] };
        key <AE09> { [ 9, parenleft ] };
        key <AE10> { [ 0, parenright ] };
        key <AE11> { [ minus, underscore ] };
        key <AE12> { [ equal, plus ] };
        key <BKSP> { [ BackSpace ] };
        key <TAB> { [ Tab, ISO_Left_Tab ] };
        key <RTRN> { [ Return ] };
        key <LCTL> { [ Control_L ] };
        key <LFSH> { [ Shift_L ] };
        key <RTSH> { [ Shift_R ] };
        key <LALT> { [ Alt_L, Meta_L ] };
        key <SPCE> { [ space ] };
        key <CAPS> { [ Caps_Lock ] };
        key <TLDE> { [ grave, asciitilde ] };
        key <AC01> { [ a, A ] };
        key <AC02> { [ s, S ] };
        key <AC03> { [ d, D ] };
        key <AC04> { [ f, F ] };
        key <AC05> { [ g, G ] };
        key <AC06> { [ h, H ] };
        key <AC07> { [ j, J ] };
        key <AC08> { [ k, K ] };
        key <AC09> { [ l, L ] };
        key <AC10> { [ semicolon, colon ] };
        key <AC11> { [ apostrophe, quotedbl ] };
        key <BKSL> { [ backslash, bar ] };
        key <AB01> { [ z, Z ] };
        key <AB02> { [ x, X ] };
        key <AB03> { [ c, C ] };
        key <AB04> { [ v, V ] };
        key <AB05> { [ b, B ] };
        key <AB06> { [ n, N ] };
        key <AB07> { [ m, M ] };
        key <AB08> { [ comma, less ] };
        key <AB09> { [ period, greater ] };
        key <AB10> { [ slash, question ] };
        modifier_map Shift { Shift_L, Shift_R };
        modifier_map Lock { Caps_Lock };
        modifier_map Control { Control_L };
        modifier_map Mod1 { Alt_L, Meta_L };
    };
};
)xkb";
} // namespace

const char* defaultXkbKeymap() {
    if (!gKeymapOverride.empty()) return gKeymapOverride.c_str();
    return kDefaultKeymap;
}

void useKeymapText(const std::string& keymapText) {
    gKeymapOverride = keymapText;
    // Push to any keyboards already bound.
    Compositor* comp = Compositor::active();
    if (!comp) return;
    size_t len = keymapText.size() + 1;
    int fd = makeMemFd(len);
    if (fd < 0) return;
    if (::write(fd, keymapText.c_str(), len) != static_cast<ssize_t>(len)) {
        ::close(fd);
        return;
    }
    comp->forEachConnection([&](Connection& c) {
        // Each keyboard needs its own fd (pushKeymap consumes it into the
        // wl_keyboard.keymap event's ancillary data).
        SeatModule::pushKeymap(c, ::dup(fd), static_cast<uint32_t>(len));
    });
    ::close(fd);
    Log::info("keymap override pushed to bound keyboards");
}

} // namespace wlc
