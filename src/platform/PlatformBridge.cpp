// ============================================================================
//  WaylandCraft-BE — platform/PlatformBridge.cpp
//  Platform dispatch (compile-time selected, mirroring upstream's
//  Platform.get() == LINUX gate in WaylandCraft.java).
//
//  scoreEntry lives HERE (once) rather than in the per-platform bridge files:
//  search ranking is identical on every platform, and all three bridges are
//  linked into the mod simultaneously — a definition per bridge file would be
//  an ODR violation.
// ============================================================================
#include "platform/PlatformBridge.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace wlc {

namespace {

int scoreQuery(const std::string& text, const std::string& q) {
    if (text.empty()) return 0;
    if (text == q) return 3;
    if (text.rfind(q, 0) == 0) return 2;
    if (text.find(q) != std::string::npos) return 1;
    return 0;
}

} // namespace

/// Search ranking — port of upstream AppListWidget scoring
/// (exact=3 / prefix=2 / contains=1; name x3, appId+keywords x2).
int scoreEntry(const DesktopEntry& entry, const std::string& query) {
    if (query.empty()) return 1;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    int score = 0;
    score += scoreQuery(lower(entry.name), q) * 3;          // name x3 (upstream)
    score += scoreQuery(lower(entry.genericName), q);
    score += scoreQuery(lower(entry.comment), q);
    score += scoreQuery(lower(entry.appId), q) * 2;
    for (const auto& k : entry.keywords) score += scoreQuery(lower(k), q) * 2;
    return score;
}

std::unique_ptr<PlatformBridge> makePlatformBridge() {
#if defined(WLC_ANDROID) && defined(__ANDROID__)
    std::unique_ptr<PlatformBridge> a = makePlatformBridgeForAndroid();
    return a;
#elif defined(WLC_LINUX_DESKTOP) && defined(__linux__)
    std::unique_ptr<PlatformBridge> l = makePlatformBridgeForLinux();
    return l;
#else
    std::unique_ptr<PlatformBridge> n = makePlatformBridgeForNull();
    return n;
#endif
}

} // namespace wlc
