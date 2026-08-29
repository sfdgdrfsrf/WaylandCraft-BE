// ============================================================================
//  WaylandCraft-BE — render/TextureBridge.h
//  Texture upload + HUD draw abstraction. ALL RenderDragon-version-specific
//  surface area is isolated here so protocol code never touches it.
//
//  Two implementation tiers:
//    * Tier A (full): runtime RGBA upload into a RenderDragon texture —
//      requires one per-version hook (BedrockTexture / Texture::uploadFrom-
//      RGBA equivalent). Symbol drift localized to TierAUpload below.
//    * Tier B (fallback, ships working): frames written to a rotating ring
//      of files in <data>/textures/ and drawn via resource-pack texture
//      paths — slower, but works on every client build.
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "compositor/Types.h"

// Client-only MC types; forward declared for the glue signatures.
class MinecraftUIRenderContext;

namespace wlc {

struct HudTexture {
    uint64_t surfaceId = 0;
    int width = 0;
    int height = 0;
    std::string resourcePath; // Tier B path (resource-pack relative)
    void* native = nullptr;   // Tier A native texture handle
    bool tierA = false;
};

class TextureBridge {
public:
    // ---- frame ingestion --------------------------------------------------
    static void uploadRgba(uint64_t surfaceId, int w, int h, const void* rgba, int stride);
    static std::shared_ptr<HudTexture> textureFor(const SurfaceRef& surface);

    // ---- HUD draw helpers (thin wrappers over MUIRC calls) -----------------
    static void drawTextTopRight(MinecraftUIRenderContext& ctx, const std::string& text);
    static void drawAppList(MinecraftUIRenderContext& ctx, const std::string& lines);
    static void drawTextureTopLeft(MinecraftUIRenderContext& ctx, const HudTexture& tex,
                                   float guiScaleFactor);
    static void drawTextureCenter(MinecraftUIRenderContext& ctx, const HudTexture& tex);
};

} // namespace wlc
