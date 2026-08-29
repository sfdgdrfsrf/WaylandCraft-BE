// ============================================================================
//  WaylandCraft-BE — commands/Commands.h
//  The /wlc command tree — both targets register it; subcommands adapt.
//  (Port of upstream's keybind-driven screens, exposed as commands since
//  Bedrock has no client keybind registry on the server target.)
// ============================================================================
#pragma once

namespace wlc {

class Commands {
public:
    static bool registerAll();
    static void unregisterAll();
};

} // namespace wlc
