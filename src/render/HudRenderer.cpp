// ============================================================================
//  WaylandCraft-BE — render/HudRenderer.cpp
//
//  Client render glue. Drawing goes through MinecraftUIRenderContext
//  (ScreenView& from ll::event::render::AfterUIRenderEvent) — the only
//  officially exposed client render hook in LeviLamina's client headers
//  (src-client/ll/api/event/render/UIRenderEvent.h in 26.20).
//
//  NOTE FOR PORTERS: MUIRC texture draws use resource-pack-relative texture
//  paths (drawImage / flushImages). Dynamic app frames use TextureBridge's
//  texture pool: the bridge maintains a per-surface "virtual texture path"
//  backed by runtime-uploaded pixels (RenderDragon texture via
//  BedrockTexture::uploadFromRGBA-equivalent hook, isolated in TextureBridge
//  so per-version symbol drift only touches one file).
//
//  CLIENT-ONLY TU: the whole body is guarded out on server targets — the
//  render/UI event headers and mc/client/* types ship with src-client only.
// ============================================================================
#include "render/HudRenderer.h"

#if defined(WLC_CLIENT) && !defined(WLC_SERVER)

#include "Mod.h"
#include "config/Config.h"
#include "core/WindowRegistry.h"
#include "render/TextureBridge.h"
#include "util/Log.h"

#include "ll/api/event/EventBus.h"
// 26.20: Before/AfterUIRenderEvent both live in UIRenderEvent.h.
#include "ll/api/event/render/UIRenderEvent.h"

// 26.20 layout: gui/* split into mc/client/gui + mc/client/renderer/screen.
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>

namespace wlc {

namespace {

std::vector<ll::event::ListenerPtr> gRenderListeners;
std::mutex                          gFrameMutex;

void drawHud(MinecraftUIRenderContext& ctx, ScreenView&) {
    const auto& cfg = Config::get();
    auto& reg = Mod::registry();

    // 1) time-date (top-right)
    if (cfg.hudClock) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &lt);
        // ctx.drawText at top-right (metrics from ScreenView camera)
        TextureBridge::drawTextTopRight(ctx, buf);
    }

    // 2) app-list (right side): icon tile + title, capture status lines
    if (cfg.hudAppList) {
        std::string lines;
        for (WindowDisplay* d : reg.displays()) {
            lines += d->title.empty() ? std::string("<unknown app>") : d->title;
            lines += "\n";
        }
        TextureBridge::drawAppList(ctx, lines);
    }

    // 3) pinned toplevel — THE video pin (top-left, 0.5x gui scale)
    if (cfg.hudVideoPin && reg.pinned()) {
        WindowDisplay* d = reg.pinned();
        auto tex = TextureBridge::textureFor(d->root);
        if (tex) TextureBridge::drawTextureTopLeft(ctx, *tex, 0.5f);
    }

    // 4) dnd-icon — drag ghost at HUD center while a drag is active
    if (auto icon = reg.dndIcon(); icon.valid()) {
        auto tex = TextureBridge::textureFor(icon);
        if (tex) TextureBridge::drawTextureCenter(ctx, *tex);
    }
}

} // namespace

bool HudRenderer::install() {
    auto& bus = ll::event::EventBus::getInstance();
    gRenderListeners.push_back(
        bus.emplaceListener<ll::event::AfterUIRenderEvent>([](auto& ev) {
            // The frame pump ALSO rides this event: upstream called
            // bridge.update() from MinecraftMixin.runTick "Post render".
            Mod::registry().update(Config::get().pixelsPerBlock);
            drawHud(ev.uiRenderContext(), ev.screenView());
        }));
    Log::info("HUD renderer installed (client render event)");
    return true;
}

void HudRenderer::remove() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto& l : gRenderListeners) bus.removeListener(l);
    gRenderListeners.clear();
}

void HudRenderer::onFrameUpdated(uint64_t surfaceId, int w, int h, const void* rgba,
                                 int stride) {
    std::lock_guard<std::mutex> lock(gFrameMutex);
    TextureBridge::uploadRgba(surfaceId, w, h, rgba, stride);
}

} // namespace wlc

#endif // WLC_CLIENT && !WLC_SERVER
