#!/usr/bin/env bash
# Proton-Scarlett — Clean Idempotent Drop-In Overlay for vkd3d-proton (proton-scarlett-3.1)
#
# Installs scarlett/d3d12_xs/ sources into <vkd3d-proton>/libs/d3d12_xs/ and
# registers subdir('libs/d3d12_xs') idempotently once the vkd3d-proton git
# submodule is initialized.
#
# Usage:
#   ./apply_to_vkd3d_proton.sh [--vkd3d-dir <path>] [--dry-run] [--check]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${SRC_DIR}/../.." && pwd)"
VKD3D_DIR="${REPO_ROOT}/vkd3d-proton"
DRY_RUN=0
CHECK_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --vkd3d-dir)
            if [[ $# -lt 2 ]]; then
                echo "[d3d12_xs-dropin] ERROR: --vkd3d-dir requires a path argument" >&2
                exit 2
            fi
            VKD3D_DIR="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --check)
            CHECK_ONLY=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--vkd3d-dir <path>] [--dry-run] [--check]"
            exit 0
            ;;
        *)
            echo "[d3d12_xs-dropin] ERROR: unknown option '$1'" >&2
            exit 2
            ;;
    esac
done

REQUIRED_FILES=(
    "d3d12_xs_vk_abi.h"
    "d3d12_xs_unix_abi.h"
    "d3d12_xs_api.h"
    "d3d12_xs_internal.hpp"
    "d3d12_xs_vulkan_loader.cpp"
    "d3d12_xs_descriptors.cpp"
    "d3d12_xs_dxil_strip.cpp"
    "d3d12_xs_presentx.cpp"
    "d3d12_xs_core.cpp"
    "d3d12_xs_unix_dispatch.cpp"
    "d3d12_xs_pe.c"
    "d3d12_xs.def"
)

for f in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "${SRC_DIR}/${f}" ]]; then
        echo "[d3d12_xs-dropin] ERROR: missing source file ${SRC_DIR}/${f}" >&2
        exit 1
    fi
done

if [[ ! -f "${SCRIPT_DIR}/meson.build" ]]; then
    echo "[d3d12_xs-dropin] ERROR: missing drop-in meson.build" >&2
    exit 1
fi

if [[ "${CHECK_ONLY}" -eq 1 ]]; then
    echo "[d3d12_xs-dropin] Source payload check PASSED (${#REQUIRED_FILES[@]} files + meson.build)."
    if [[ -f "${VKD3D_DIR}/meson.build" ]]; then
        echo "[d3d12_xs-dropin] Target vkd3d-proton checkout detected at: ${VKD3D_DIR}"
    else
        echo "[d3d12_xs-dropin] Target vkd3d-proton submodule not yet initialized at: ${VKD3D_DIR}"
    fi
    exit 0
fi

if [[ ! -f "${VKD3D_DIR}/meson.build" ]]; then
    echo "[d3d12_xs-dropin] ERROR: vkd3d-proton submodule is not initialized at '${VKD3D_DIR}' (meson.build not found)." >&2
    echo "[d3d12_xs-dropin] Run: git submodule update --init vkd3d-proton (branch proton-scarlett-3.1)" >&2
    exit 1
fi

DST_DIR="${VKD3D_DIR}/libs/d3d12_xs"
HOOK_MARKER="# PROTON-SCARLETT-D3D12-XS-DROPIN"

if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "[d3d12_xs-dropin] [DRY-RUN] Would create ${DST_DIR}"
    for f in "${REQUIRED_FILES[@]}"; do
        echo "[d3d12_xs-dropin] [DRY-RUN] Would copy ${SRC_DIR}/${f} -> ${DST_DIR}/${f}"
    done
    echo "[d3d12_xs-dropin] [DRY-RUN] Would copy ${SCRIPT_DIR}/meson.build -> ${DST_DIR}/meson.build"
    echo "[d3d12_xs-dropin] [DRY-RUN] Would ensure subdir('libs/d3d12_xs') in ${VKD3D_DIR}/meson.build"
    exit 0
fi

mkdir -p "${DST_DIR}"
for f in "${REQUIRED_FILES[@]}"; do
    cp -f "${SRC_DIR}/${f}" "${DST_DIR}/${f}"
done
cp -f "${SCRIPT_DIR}/meson.build" "${DST_DIR}/meson.build"

if ! grep -Fq "${HOOK_MARKER}" "${VKD3D_DIR}/meson.build"; then
    {
        echo ""
        echo "${HOOK_MARKER}"
        echo "if get_option('enable_scarlett_d3d12_xs')"
        echo "  subdir('libs/d3d12_xs')"
        echo "endif"
    } >> "${VKD3D_DIR}/meson.build"
fi

if [[ -f "${VKD3D_DIR}/meson_options.txt" ]] && \
   ! grep -Fq "enable_scarlett_d3d12_xs" "${VKD3D_DIR}/meson_options.txt"; then
    echo "option('enable_scarlett_d3d12_xs', type: 'boolean', value: true, description: 'Build Proton-Scarlett d3d12_xs extension')" \
        >> "${VKD3D_DIR}/meson_options.txt"
fi

echo "[d3d12_xs-dropin] Successfully applied d3d12_xs overlay to ${DST_DIR}"
