#!/usr/bin/env bash
# ============================================================================
#  WaylandCraft-BE — android-mod/package.sh
#  Builds the arm64-v8a .so (if NDK available) and packages the
#  LeviLaunchroid-importable zip: exactly one .so + manifest.json.
#
#  Usage:  NDK=/path/to/android-ndk-r26d ./package.sh [out.zip]
#          (run from anywhere — paths are script-relative)
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$HERE/dist/WaylandCraftBE-android-arm64.zip}"
PATH="$HOME/.local/bin:$PATH"

# ---- locate the NDK --------------------------------------------------------
# Priority: $NDK (explicit) > $ANDROID_NDK_HOME (setup-ndk / Android SDK env)
# > GitHub-hosted runner default (setup-ndk installs into the SDK dir).
NDK="${NDK:-${ANDROID_NDK_HOME:-}}"
if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
    NDK=""
    for d in /usr/local/lib/android/sdk/ndk/* "$HOME"/android-ndk* /opt/android-ndk*; do
        if [ -f "$d/build/cmake/android.toolchain.cmake" ]; then
            NDK="$d"
            break
        fi
    done
fi
if [ -z "$NDK" ]; then
    echo "ERROR: no Android NDK found (set NDK=/path/to/ndk or ANDROID_NDK_HOME)" >&2
    exit 1
fi
echo "Using NDK: $NDK"

# ---- build -----------------------------------------------------------------
if [ ! -f "$HERE/build/libwaylandcraft.so" ]; then
    # -S is explicit so the script works from any CWD (CI checkout root).
    cmake -S "$HERE" -B "$HERE/build" -G Ninja \
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
