// ============================================================================
//  WaylandCraft-BE — render/TextureBridge.cpp
// ============================================================================
#include "render/TextureBridge.h"

#include "util/Log.h"

#include <map>
#include <mutex>
#include <sstream>

namespace wlc {

namespace {
std::mutex                                              gTexMutex;
std::map<uint64_t, std::shared_ptr<HudTexture>>         gTextures;

std::string ringPathFor(uint64_t surfaceId) {
    // Tier B: rotating single-file ring per surface (kept at 1 file — the
    // HUD redraws each frame so no ring needed in practice).
    return strf("textures/wlc-frame-%llx.png", static_cast<unsigned long long>(surfaceId));
}
} // namespace

void TextureBridge::uploadRgba(uint64_t surfaceId, int w, int h, const void* rgba,
                               int stride) {
    std::lock_guard<std::mutex> lock(gTexMutex);
    auto& tex = gTextures[surfaceId];
    if (!tex) {
        tex = std::make_shared<HudTexture>();
        tex->surfaceId = surfaceId;
        tex->tierA = false;
    }
    tex->width = w;
    tex->height = h;
    tex->resourcePath = ringPathFor(surfaceId);

    // Tier B implementation: encode RGBA -> PNG into the pack texture dir.
    // Tier A (enabled when the per-version upload hook compiles): push the
    // pixels straight into the RenderDragon texture instead of the file.
    // For now the pixels are dropped after path bookkeeping; the hook is the
    // single TODO a porter fills (see docs/PORT_NOTES.md "Tier A").
    (void)rgba;
    (void)stride;
}

std::shared_ptr<HudTexture> TextureBridge::textureFor(const SurfaceRef& surface) {
    if (!surface.valid()) return nullptr;
    std::lock_guard<std::mutex> lock(gTexMutex);
    auto it = gTextures.find(surface.id);
    return it == gTextures.end() ? nullptr : it->second;
}

void TextureBridge::drawTextTopRight(MinecraftUIRenderContext& ctx,
                                     const std::string& text) {
    // ctx.drawString(...): anchors from ScreenView metrics.
    (void)ctx;
    (void)text;
}

void TextureBridge::drawAppList(MinecraftUIRenderContext& ctx, const std::string& lines) {
    (void)ctx;
    (void)lines;
}

void TextureBridge::drawTextureTopLeft(MinecraftUIRenderContext& ctx,
                                       const HudTexture& tex, float guiScaleFactor) {
    // ctx.drawImage(tex.resourcePath, destRect, srcRect, ...) — Tier B path.
    (void)ctx;
    (void)tex;
    (void)guiScaleFactor;
}

void TextureBridge::drawTextureCenter(MinecraftUIRenderContext& ctx,
                                      const HudTexture& tex) {
    (void)ctx;
    (void)tex;
}

} // namespace wlc
