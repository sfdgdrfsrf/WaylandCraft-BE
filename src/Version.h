// ============================================================================
//  WaylandCraft-BE — src/Version.h
//
//  WLC_VERSION is normally injected by the build system (xmake
//  add_defines('WLC_VERSION="..."')). This header only guarantees the symbol
//  exists for translation units that reference it directly, so the source
//  tree stays buildable by plain IDE tooling and non-xmake generators.
// ============================================================================
#pragma once

#ifndef WLC_VERSION
#define WLC_VERSION "1.0.0"
#endif
