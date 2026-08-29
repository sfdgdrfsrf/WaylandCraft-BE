// ============================================================================
//  WaylandCraft-BE — util/Log.h
//  Pure logging sink. The LeviLamina glue (Mod.cpp) installs the real
//  ll::io::Logger backend; the standalone core (and the smoke tests) install
//  a stdout backend. This keeps the entire compositor stack free of
//  LeviLamina headers so it can be unit-tested and cross-compiled anywhere.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace wlc {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

using LogSink = std::function<void(LogLevel, std::string_view msg)>;

class Log {
public:
    static void setSink(LogSink sink);
    /// Default sink: stdout/stderr (warn+ -> stderr).
    static void installDefaultSink();

    static void debug(std::string_view msg);
    static void info(std::string_view msg);
    static void warn(std::string_view msg);
    static void error(std::string_view msg);

    /// fmt-lite helpers: only "%s"-style single-argument concatenation is
    /// provided to keep the core dependency-free. The glue layer uses
    /// ll::io::Logger formatting for richer messages.
    static void infof(const std::string& msg) { info(msg); }
};

std::string strf(const char* fmt, ...); // tiny snprintf wrapper

} // namespace wlc
