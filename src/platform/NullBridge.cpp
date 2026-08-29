// ============================================================================
//  WaylandCraft-BE — platform/NullBridge.cpp
//  The upstream non-Linux experience, faithfully ported:
//  "You can install it on MacOS and Windows but you won't have any of the
//  features." The compositor still runs (remote/capture windows work);
//  local app launching is simply unavailable.
// ============================================================================
#include "platform/PlatformBridge.h"

namespace wlc {

namespace {
class NullBridge : public PlatformBridge {
public:
    const char* platformName() const override { return "unsupported"; }
    bool featuresAvailable() const override { return false; }
    std::vector<DesktopEntry> scanApps() override { return {}; }
    bool launchApp(const DesktopEntry&) override { return false; }
    std::vector<uint8_t> renderIcon(const DesktopEntry&, int) override { return {}; }
    std::string statusLine() const override {
        return "Local app features need Linux or Android (WLC_ANDROID=1 build). "
               "The compositor socket still works for remote/capture sources.";
    }
};
} // namespace

std::unique_ptr<PlatformBridge> makePlatformBridgeForNull() {
    return std::make_unique<NullBridge>();
}

} // namespace wlc
