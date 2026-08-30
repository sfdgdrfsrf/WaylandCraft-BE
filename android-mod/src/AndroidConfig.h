// ============================================================================
//  WaylandCraft-BE — android-mod/src/AndroidConfig.h
//  Config glue for the LeviLaunchroid lane. Replaces the LeviLamina
//  reflection storage (src/config/Config.cpp, which links ll::api) with a
//  dependency-free flat-JSON reader so the whole android .so links against
//  system libs only.
// ============================================================================
#pragma once

#include <string>

namespace wlc {

class Config; // config/Config.h

namespace AndroidConfig {

/// Reads <modroot>/config/config.json if present (flat key/value object);
/// missing file or keys keep their defaults from Config's initializers.
/// Returns false when the file was absent or unparsed (defaults in effect).
bool loadFromFile(const std::string& path);

/// Writes the current Config back as flat JSON (used on first run to
/// materialize an editable file).
bool saveToFile(const std::string& path);

} // namespace AndroidConfig

} // namespace wlc
