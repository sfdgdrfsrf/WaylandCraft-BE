-- ============================================================================
--  WaylandCraft-BE — Wayland compositor in Minecraft Bedrock (LeviLamina port)
--  Original Java mod: WaylandCraft v2.0.3 by EVV1E (GPL-3.0)
--  This port targets LeviLamina 26.20.x (server) and LeviLauncher/LeviLaunchroid
--  client builds, with an experimental Android arm64 cross build.
-- ============================================================================

add_rules("mode.release", "mode.debug")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

-- target_type is injected by CI / `xmake f --target_type=server|client`
local target_type = is_config("target_type", "client") and "client" or "server"

add_requires("levilamina 26.20", { configs = { target_type = target_type } })
add_requires("levibuildscript")

if is_plat("windows") then
    add_cxxflags("/EHsc", "/utf-8", { tools = "clang_cl" })
    add_cxxflags("/Zc:__cplusplus", { tools = "clang_cl" })
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_description("Build for BDS server or Bedrock client (LeviLauncher / LeviLaunchroid)")
option_end()

option("android")
    set_default(false)
    set_showmenu(true)
    set_description("Enable the Android platform bridge (LeviLaunchroid client builds)")
option_end()

target("WaylandCraftBE")
    add_rules("@levibuildscript/linkrule", "@levibuildscript/modpacker")
    add_packages("levilamina")
    set_kind("shared")
    set_languages("c++20")
    set_basename("WaylandCraftBE")
    set_values("mod.name", "WaylandCraftBE")
    set_values("mod.version", "1.0.0")

    add_includedirs("src")
    add_files("src/**.cpp")

    -- Android client builds get the AndroidBridge / CaptureService + intent socket;
    -- Linux desktop builds get the real XDG / Wayland-satellite-style bridge.
    if is_config("android", true) then
        add_defines("WLC_ANDROID=1")
    end
    if is_plat("linux") and not is_config("android", true) then
        add_defines("WLC_LINUX_DESKTOP=1")
    end
    if target_type == "client" then
        add_defines("LL_PLAT_C=1", "WLC_CLIENT=1")
    else
        add_defines("LL_PLAT_S=1", "WLC_SERVER=1")
    end
    add_defines('WLC_VERSION="1.0.0"')


-- ---------------------------------------------------------------------------
-- Standalone compositor-core test: a REAL Wayland client handshakes with our
-- server over the socket (registry → bind → shm → xdg-shell → frame callbacks).
-- Pure POSIX, no LeviLamina headers — runs in CI on Linux.
-- ---------------------------------------------------------------------------
target("wlc-smoke-test")
    set_kind("binary")
    set_default(false)
    set_languages("c++20")
    set_exceptions("cxx")
    add_includedirs("src")
    add_files("tests/smoke_client.cpp")
    add_files("src/compositor/*.cpp")
    add_files("src/util/Log.cpp", "src/util/MemFd.cpp", "src/util/XkbKeymap.cpp")
    if is_plat("linux", "android", "macosx", "bsd") then
        add_syslinks("pthread")
    elseif is_plat("windows") then
        add_syslinks("ws2_32")
    end
    on_run(function (target)
        import("core.base.os")
        os.execv(target:targetfile())
    end)

