// ============================================================================
//  WaylandCraft-BE — ui/AppLauncher.h
//  Port of upstream gui/AppLauncherScreen (key V): searchable app list with
//  the 14 freedesktop categories and similarity ranking (exact=3/prefix=2/
//  contains=1, name x3), rendered as a ll::form::SimpleForm on the server
//  target and as an overlay UI on the client target.
// ============================================================================
#pragma once

#include <string>

namespace wlc {

class AppLauncher {
public:
    /// Opens the launcher UI (client overlay) or sends the form (server).
    static void toggle();

    /// Launch + register with the compositor (shared by both paths).
    static bool launch(const std::string& appId);

private:
    static void openServerForm();
    static void openClientOverlay();
};

} // namespace wlc
