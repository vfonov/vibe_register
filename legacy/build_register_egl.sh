#!/usr/bin/env bash
# build_register_egl.sh — Build legacy register with the EGL+X11 backend.
#
# This script builds bicgl (EGL backend) and register in local build
# directories under legacy/bicgl/build_egl and legacy/register/build_egl.
# No install step is required; you can run the binary directly from
# legacy/register/build_egl/register.
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install cmake build-essential \
#        libglfw3-dev libgl-dev libx11-dev \
#        libhdf5-dev libnetcdf-dev
# (libglfw3-dev brings EGL support via Mesa; no separate libegl-dev needed)
#
# The MINC toolkit (libminc + bicpl) must be available. Pass its prefix
# via the MINC_TOOLKIT environment variable or the --minc argument.
# Default: /opt/minc/1.9.18.9
#
# Usage:
#   ./build_register_egl.sh [--minc /path/to/minc] [--jobs N] [--clean]

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
REGISTER_SRC="${SCRIPT_DIR}/register"
BICGL_BUILD="${BICGL_SRC}/build_egl"
REGISTER_BUILD="${REGISTER_SRC}/build_egl"

echo "=== Build configuration ==="
echo "  MINC toolkit : ${MINC_PREFIX}"
echo "  bicgl source : ${BICGL_SRC}"
echo "  register src : ${REGISTER_SRC}"
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
    rm -rf "${BICGL_BUILD}" "${REGISTER_BUILD}"
fi

# ── Build bicgl (EGL backend) ─────────────────────────────────────────────────
echo "--- Configuring bicgl (EGL backend) ---"
mkdir -p "${BICGL_BUILD}"
cmake -S "${BICGL_SRC}" -B "${BICGL_BUILD}" \
    -DBICGL_USE_EGL=ON \
    -DBICGL_USE_GLUT=OFF \
    -DCMAKE_PREFIX_PATH="${MINC_PREFIX}" \
    -DLIBMINC_DIR="${MINC_PREFIX}/lib/cmake" \
    -DBICPL_DIR="${MINC_PREFIX}/lib" \
    -DCMAKE_BUILD_TYPE=Release \
    -Wno-dev

echo "--- Building bicgl ---"
cmake --build "${BICGL_BUILD}" --parallel "${JOBS}"

# ── Build register ────────────────────────────────────────────────────────────
echo "--- Configuring register ---"
mkdir -p "${REGISTER_BUILD}"
cmake -S "${REGISTER_SRC}" -B "${REGISTER_BUILD}" \
    -DCMAKE_PREFIX_PATH="${MINC_PREFIX}" \
    -DLIBMINC_DIR="${MINC_PREFIX}/lib/cmake" \
    -DBICPL_DIR="${MINC_PREFIX}/lib" \
    -DBICGL_EGL_BUILD_DIR="${BICGL_BUILD}" \
    -DCMAKE_BUILD_TYPE=Release \
    -Wno-dev

echo "--- Building register ---"
cmake --build "${REGISTER_BUILD}" --parallel "${JOBS}"

# ── Done ──────────────────────────────────────────────────────────────────────
REGISTER_BIN="${REGISTER_BUILD}/register"
echo ""
echo "=== Build complete ==="
echo "  Binary: ${REGISTER_BIN}"
echo ""
echo "Run with:"
echo "  ${REGISTER_BIN} [volume1.mnc] [volume2.mnc]"
