// ============================================================================
//  WaylandCraft-BE — src/Mod.h
//  LeviLamina native mod entry — port of WaylandCraftCommon.java (server
//  side) + WaylandCraft.java (client side), dual-target via LL_PLAT_S/C.
// ============================================================================
#pragma once

#include "ll/api/mod/NativeMod.h"

#include <memory>

namespace wlc {

class WindowRegistry;
class Compositor;
class PlatformBridge;

class Mod {
public:
    explicit Mod(ll::mod::NativeMod& self) : self_(self) {}

    static Mod& instance();

    ll::mod::NativeMod& self() const { return self_; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

    /// Shared subsystem accessors (defined in Mod.cpp).
    static WindowRegistry&  registry();
    static Compositor&      compositor();
    static PlatformBridge&  bridge();

private:
    ll::mod::NativeMod& self_;
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace wlc
