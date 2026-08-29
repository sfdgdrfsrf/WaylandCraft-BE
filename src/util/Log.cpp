#include "util/Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace wlc {

namespace {
std::mutex gMutex;
LogSink gSink = nullptr;
} // namespace

void Log::setSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(gMutex);
    gSink = std::move(sink);
}

void Log::installDefaultSink() {
    // NOTE: called with gMutex already held by the log methods, so the sink
    // itself must NOT re-lock (that would self-deadlock).
    setSink([](LogLevel level, std::string_view msg) {
        static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        std::FILE* out = (level >= LogLevel::Warn) ? stderr : stdout;
        std::fprintf(out, "[WaylandCraftBE/%s] %.*s\n", names[static_cast<int>(level)],
                     static_cast<int>(msg.size()), msg.data());
    });
}

void Log::debug(std::string_view msg) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gSink) gSink(LogLevel::Debug, msg);
}
void Log::info(std::string_view msg) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gSink) gSink(LogLevel::Info, msg);
}
void Log::warn(std::string_view msg) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gSink) gSink(LogLevel::Warn, msg);
}
void Log::error(std::string_view msg) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gSink) gSink(LogLevel::Error, msg);
}

std::string strf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    if (static_cast<size_t>(n) < sizeof(buf)) return std::string(buf, static_cast<size_t>(n));
    std::string big(static_cast<size_t>(n) + 1, '\0');
    va_start(ap, fmt);
    std::vsnprintf(big.data(), big.size() + 1, fmt, ap);
    va_end(ap);
    big.resize(static_cast<size_t>(n));
    return big;
}

} // namespace wlc
