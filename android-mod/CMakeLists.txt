# ============================================================================
#  WaylandCraft-BE — android-mod/CMakeLists.txt
#
#  LeviLaunchroid native mod (.so) for arm64-v8a, built with the NDK
#  toolchain directly — NOT the xmake lane. LeviLamina has no Android
#  target, so this lane compiles the pure-POSIX core (compositor, Android
#  bridge, WLCF capture service) and wires it to the launcher's
#  preloader-android SDK via PL_REGISTER_MOD. See src/AndroidMod.cpp.
#
#  Build:
#    cmake -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#          -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
#          -DCMAKE_BUILD_TYPE=Release
#    cmake --build build
#
#  Output: build/libwaylandcraft.so  → zip with manifest.json (see package.sh)
# ============================================================================

cmake_minimum_required(VERSION 3.18)
project(WaylandCraftBE LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(waylandcraft SHARED
    # pl-SDK entry + config glue (this lane's replacement for src/Mod.cpp)
    src/AndroidMod.cpp
    src/AndroidConfig.cpp

    # Wayland compositor core — real wire protocol, zero LL dependencies
    ../src/compositor/Client.cpp
    ../src/compositor/DataDevice.cpp
    ../src/compositor/Globals.cpp
    ../src/compositor/Seat.cpp
    ../src/compositor/Server.cpp
    ../src/compositor/Surface.cpp
    ../src/compositor/XdgShell.cpp

    # POSIX utilities
    ../src/util/Log.cpp
    ../src/util/MemFd.cpp
    ../src/util/XkbKeymap.cpp

    # Android platform bridge (`pm` scan / `am start` / intent socket)
    ../src/platform/PlatformBridge.cpp
    ../src/platform/AndroidBridge.cpp

    # WLCF capture protocol server (companion APK frame ingest)
    ../src/android/CaptureService.cpp
)

target_include_directories(waylandcraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pl-include
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(waylandcraft PRIVATE
    WLC_ANDROID=1
    WLC_CLIENT=1
    WLC_VERSION="1.0.0-android"
    # pl/Logger.hpp calls non-template fmt::vformat; header-only keeps the
    # .so free of any fmt link dependency.
    FMT_HEADER_ONLY=1
    FMT_STATIC_THOUSANDS_SEPARATOR=0
)

# liblog for __android_log_print; c++_shared to match the launcher runtime
find_library(log-lib log)
target_link_libraries(waylandcraft PRIVATE ${log-lib})

# Keep the exported surface tight: only PLGetModRegistration (+ JNI bits the
# runtime needs) should leave the module.
target_link_options(waylandcraft PRIVATE
    -Wl,--exclude-libs,ALL
    -Wl,-z,max-page-size=16384
)
