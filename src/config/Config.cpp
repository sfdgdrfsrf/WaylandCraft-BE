// ============================================================================
//  WaylandCraft-BE — config/Config.cpp
//  ll::config reflection storage — the equivalent of upstream's
//  WaylandCraftSettingsManager (Gson read+rewrite on init).
// ============================================================================
#include "config/Config.h"

#include "ll/api/Config.h"
#include "ll/api/mod/NativeMod.h"

#include <mutex>

namespace wlc {

namespace {
std::mutex cfgMutex;
} // namespace

Config& Config::get() {
    static Config instance;
    return instance;
}

bool Config::load() {
    std::lock_guard<std::mutex> lock(cfgMutex);
    auto path = ll::mod::NativeMod::current()->getDataDir() / "config.json";

    // ll::config::loadConfig performs reflection-based field mapping and
    // fills missing fields with the current defaults of Config::get().
    bool ok = ll::config::loadConfig(get(), path);
    if (ok) ll::config::saveConfig(get(), path); // write back w/ new defaults
    return ok;
}

bool Config::save() {
    std::lock_guard<std::mutex> lock(cfgMutex);
    auto path = ll::mod::NativeMod::current()->getDataDir() / "config.json";
    return ll::config::saveConfig(get(), path);
}

} // namespace wlc
