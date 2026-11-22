#!/usr/bin/env bash
# Proton-Scarlett — Clean Idempotent Drop-In Overlay for vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/
#
# Installs scarlett/rdna2_lifter/ sources into:
#   <vkd3d-proton>/subprojects/dxil-spirv/rdna2-lifter/
# and idempotently registers subdir('rdna2-lifter') inside dxil-spirv/meson.build
# (branch proton-scarlett-3.1).
#
# Usage:
#   ./apply_to_dxil_spirv.sh [--vkd3d-dir <path>] [--dxil-spirv-dir <path>] [--dry-run] [--check]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${SRC_DIR}/../.." && pwd)"
VKD3D_DIR="${REPO_ROOT}/vkd3d-proton"
DXIL_SPIRV_DIR=""
DRY_RUN=0
CHECK_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --vkd3d-dir)
            if [[ $# -lt 2 ]]; then
                echo "[rdna2-lifter-dropin] ERROR: --vkd3d-dir requires a path argument" >&2
                exit 2
            fi
            VKD3D_DIR="$2"
            shift 2
            ;;
        --dxil-spirv-dir)
            if [[ $# -lt 2 ]]; then
                echo "[rdna2-lifter-dropin] ERROR: --dxil-spirv-dir requires a path argument" >&2
                exit 2
            fi
            DXIL_SPIRV_DIR="$2"
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
            echo "Usage: $0 [--vkd3d-dir <path>] [--dxil-spirv-dir <path>] [--dry-run] [--check]"
            exit 0
            ;;
        *)
            echo "[rdna2-lifter-dropin] ERROR: unknown option '$1'" >&2
            exit 2
            ;;
    esac
done

if [[ -z "${DXIL_SPIRV_DIR}" ]]; then
    DXIL_SPIRV_DIR="${VKD3D_DIR}/subprojects/dxil-spirv"
fi

REQUIRED_FILES=(
    "rdna2_isa.hpp"
    "rdna2_lifter.hpp"
    "rdna2_ir.hpp"
    "rdna2_spirv_emitter.hpp"
    "rdna2_containers.cpp"
    "rdna2_decoder.cpp"
    "rdna2_ir.cpp"
    "rdna2_spirv_emitter.cpp"
    "rdna2_microprobes.cpp"
    "rdna2_spv_eval.cpp"
    "rdna2_lifter.cpp"
)

for f in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "${SRC_DIR}/${f}" ]]; then
        echo "[rdna2-lifter-dropin] ERROR: missing source file ${SRC_DIR}/${f}" >&2
        exit 1
    fi
done

if [[ ! -f "${SCRIPT_DIR}/meson.build" ]]; then
    echo "[rdna2-lifter-dropin] ERROR: missing drop-in meson.build" >&2
    exit 1
fi

if [[ "${CHECK_ONLY}" -eq 1 ]]; then
    echo "[rdna2-lifter-dropin] Source payload check PASSED (${#REQUIRED_FILES[@]} files + meson.build)."
    if [[ -f "${DXIL_SPIRV_DIR}/meson.build" ]]; then
        echo "[rdna2-lifter-dropin] Target dxil-spirv checkout detected at: ${DXIL_SPIRV_DIR}"
    else
        echo "[rdna2-lifter-dropin] Target dxil-spirv submodule not yet initialized at: ${DXIL_SPIRV_DIR}"
    fi
    exit 0
fi

if [[ ! -f "${DXIL_SPIRV_DIR}/meson.build" ]]; then
    if [[ "${DRY_RUN}" -eq 1 ]]; then
        echo "[rdna2-lifter-dropin] [DRY-RUN] Target dxil-spirv submodule not initialized at ${DXIL_SPIRV_DIR}"
        echo "[rdna2-lifter-dropin] [DRY-RUN] Would install ${#REQUIRED_FILES[@]} files + meson.build -> ${DXIL_SPIRV_DIR}/rdna2-lifter"
        echo "[rdna2-lifter-dropin] [DRY-RUN] Would register subdir('rdna2-lifter') in ${DXIL_SPIRV_DIR}/meson.build"
        exit 0
    fi
    echo "[rdna2-lifter-dropin] ERROR: dxil-spirv not found at '${DXIL_SPIRV_DIR}' (meson.build missing)." >&2
    echo "[rdna2-lifter-dropin] Run: git submodule update --init --recursive vkd3d-proton (branch proton-scarlett-3.1)" >&2
    exit 1
fi

DST_DIR="${DXIL_SPIRV_DIR}/rdna2-lifter"
HOOK_MARKER="# PROTON-SCARLETT-RDNA2-LIFTER-DROPIN"

if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "[rdna2-lifter-dropin] [DRY-RUN] Would create ${DST_DIR}"
    for f in "${REQUIRED_FILES[@]}"; do
        echo "[rdna2-lifter-dropin] [DRY-RUN] Would copy ${SRC_DIR}/${f} -> ${DST_DIR}/${f}"
    done
    echo "[rdna2-lifter-dropin] [DRY-RUN] Would copy ${SCRIPT_DIR}/meson.build -> ${DST_DIR}/meson.build"
    echo "[rdna2-lifter-dropin] [DRY-RUN] Would ensure subdir('rdna2-lifter') in ${DXIL_SPIRV_DIR}/meson.build"
    exit 0
fi

mkdir -p "${DST_DIR}"
for f in "${REQUIRED_FILES[@]}"; do
    cp -f "${SRC_DIR}/${f}" "${DST_DIR}/${f}"
done
cp -f "${SCRIPT_DIR}/meson.build" "${DST_DIR}/meson.build"

if ! grep -Fq "${HOOK_MARKER}" "${DXIL_SPIRV_DIR}/meson.build"; then
    {
        echo ""
        echo "${HOOK_MARKER}"
        echo "subdir('rdna2-lifter')"
    } >> "${DXIL_SPIRV_DIR}/meson.build"
fi

echo "[rdna2-lifter-dropin] Successfully applied rdna2-lifter overlay to ${DST_DIR}"
