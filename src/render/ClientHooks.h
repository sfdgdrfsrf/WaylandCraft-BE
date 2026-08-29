// ============================================================================
//  WaylandCraft-BE — render/ClientHooks.h
//  Install/remove client hooks: HUD renderer + keybinds (KeyRegistry) +
//  pointer/key forwarding. Server builds get the server hooks instead.
// ============================================================================
#pragma once

namespace wlc {

bool installClientHooks();  // client target (LL_PLAT_C)
void removeClientHooks();
bool installServerHooks();  // server target (LL_PLAT_S)
void removeServerHooks();

} // namespace wlc
