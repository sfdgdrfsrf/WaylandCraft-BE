#!/usr/bin/env bash
# ============================================================================
#  WaylandCraft-BE — android-mod/package.sh
#  Builds the arm64-v8a .so (if NDK available) and packages the
#  LeviLaunchroid-importable zip: exactly one .so + manifest.json.
#
#  Usage:  NDK=/path/to/android-ndk-r26d ./package.sh [out.zip]
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NDK="${NDK:-${ANDROID_NDK_HOME:-/home/z/android-ndk-r26d}}"
OUT="${1:-$HERE/dist/WaylandCraftBE-android-arm64.zip}"
PATH="$HOME/.local/bin:$PATH"

# ---- build -----------------------------------------------------------------
if [ ! -f "$HERE/build/libwaylandcraft.so" ]; then
    cmake -B "$HERE/build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$HERE/build"

# ---- package ---------------------------------------------------------------
mkdir -p "$(dirname "$OUT")"
HERE="$HERE" python3 - "$OUT" << 'EOF'
import os, sys, zipfile
here = os.environ["HERE"]
out = sys.argv[1]
so = os.path.join(here, "build", "libwaylandcraft.so")
manifest = os.path.join(here, "manifest.json")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(manifest, "WaylandCraftBE/manifest.json")
    z.write(so, "WaylandCraftBE/libwaylandcraft.so")
print("packaged:", out)
EOF
ls -lh "$OUT"
