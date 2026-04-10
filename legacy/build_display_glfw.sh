#!/usr/bin/env bash
# build_display_glfw.sh — Build legacy Display with the GLFW backend.
#
# This script builds bicgl (GLFW backend) and Display in local build
# directories under legacy/bicgl/build_egl and legacy/display/build_glfw.
# No install step is required; you can run the binary directly from
# legacy/display/build_glfw/Display.
#
# The bicgl build directory (build_egl) is shared with build_register_egl.sh,
# so if you have already run that script the bicgl step will be a no-op.
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install cmake build-essential \
#        libglfw3-dev libgl-dev libx11-dev \
#        libhdf5-dev libnetcdf-dev \
#        libz-dev libgifti-dev
# (libglfw3-dev brings EGL support via Mesa; no separate libegl-dev needed)
#
# The MINC toolkit (libminc + bicpl) must be available. Pass its prefix
# via the MINC_TOOLKIT environment variable or the --minc argument.
# Default: /opt/minc/1.9.18.9
#
# Usage:
#   ./build_display_glfw.sh [--minc /path/to/minc] [--jobs N] [--clean]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Defaults ────────────────────────────────────────────────────────────────
MINC_PREFIX="${MINC_TOOLKIT:-/opt/minc/1.9.18.9}"
JOBS="$(nproc 2>/dev/null || echo 4)"
CLEAN=0

# ── Argument parsing ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --minc)   MINC_PREFIX="$2"; shift 2 ;;
        --jobs)   JOBS="$2";        shift 2 ;;
        --clean)  CLEAN=1;          shift   ;;
        -h|--help)
            sed -n '/^# Usage/,/^[^#]/{ s/^# \?//p }' "$0"
            exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

BICGL_SRC="${SCRIPT_DIR}/bicgl"
DISPLAY_SRC="${SCRIPT_DIR}/display"
BICGL_BUILD="${BICGL_SRC}/build_egl"
DISPLAY_BUILD="${DISPLAY_SRC}/build_glfw"

echo "=== Build configuration ==="
echo "  MINC toolkit : ${MINC_PREFIX}"
echo "  bicgl source : ${BICGL_SRC}"
echo "  display src  : ${DISPLAY_SRC}"
echo "  parallel jobs: ${JOBS}"
echo ""

# ── Validate MINC prefix ─────────────────────────────────────────────────────
if [[ ! -f "${MINC_PREFIX}/lib/cmake/LIBMINCConfig.cmake" ]]; then
    echo "ERROR: LIBMINCConfig.cmake not found under ${MINC_PREFIX}/lib/cmake" >&2
    echo "       Set MINC_TOOLKIT or pass --minc /path/to/minc" >&2
    exit 1
fi
if [[ ! -f "${MINC_PREFIX}/lib/BICPLConfig.cmake" ]]; then
    echo "ERROR: BICPLConfig.cmake not found under ${MINC_PREFIX}/lib" >&2
    exit 1
fi

# ── Clean if requested ────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 ]]; then
    echo "--- Cleaning build directories ---"
    rm -rf "${BICGL_BUILD}" "${DISPLAY_BUILD}"
fi

# ── Build bicgl (GLFW backend) ────────────────────────────────────────────────
echo "--- Configuring bicgl (GLFW backend) ---"
mkdir -p "${BICGL_BUILD}"
cmake -S "${BICGL_SRC}" -B "${BICGL_BUILD}" \
    -DBICGL_USE_GLFW=ON \
    -DBICGL_USE_GLUT=OFF \
    -DCMAKE_PREFIX_PATH="${MINC_PREFIX}" \
    -DLIBMINC_DIR="${MINC_PREFIX}/lib/cmake" \
    -DBICPL_DIR="${MINC_PREFIX}/lib" \
    -DCMAKE_BUILD_TYPE=Release \
    -Wno-dev

echo "--- Building bicgl ---"
cmake --build "${BICGL_BUILD}" --parallel "${JOBS}"

# ── Build Display ─────────────────────────────────────────────────────────────
echo "--- Configuring Display ---"
mkdir -p "${DISPLAY_BUILD}"
cmake -S "${DISPLAY_SRC}" -B "${DISPLAY_BUILD}" \
    -DCMAKE_PREFIX_PATH="${MINC_PREFIX}" \
    -DLIBMINC_DIR="${MINC_PREFIX}/lib/cmake" \
    -DBICPL_DIR="${MINC_PREFIX}/lib" \
    -DBICGL_EGL_BUILD_DIR="${BICGL_BUILD}" \
    -DCMAKE_BUILD_TYPE=Release \
    -Wno-dev

echo "--- Building Display ---"
cmake --build "${DISPLAY_BUILD}" --parallel "${JOBS}"

# ── Done ──────────────────────────────────────────────────────────────────────
DISPLAY_BIN="${DISPLAY_BUILD}/Display"
echo ""
echo "=== Build complete ==="
echo "  Binary: ${DISPLAY_BIN}"
echo ""
echo "Run with:"
echo "  ${DISPLAY_BIN} [volume.mnc] [surface.obj]"
