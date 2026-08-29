// ============================================================================
//  WaylandCraft-BE — platform/PlatformBridge.cpp
//  Platform dispatch (compile-time selected, mirroring upstream's
//  Platform.get() == LINUX gate in WaylandCraft.java).
// ============================================================================
#include "platform/PlatformBridge.h"

namespace wlc {

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
