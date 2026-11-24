/*
 * Proton-Scarlett — Comprehensive d3d12_xs / PresentX / VK_EXT_descriptor_heap Test Suite
 *
 * Verifies:
 *   1. Real Vulkan 1.4 instance/physical-device/logical-device probe on host
 *      (with VK_LAYER_KHRONOS_validation probe -> 0 validation errors, or explicit
 *      diagnostic if the host does not install the validation layer).
 *   2. GXDK >= 0x585D070E D3D12XboxCreateDevice (AmplificationShaderIndirectArgsBufferSize,
 *      AmplificationShaderPayloadBufferSize, SetAmplificationShaderLimits, extension caps).
 *   3. Descriptor Heap Policy Matrix (T#/V#/S#):
 *      - AMD RADV (VK_EXT_descriptor_heap <= 1M vs >1M fallback)
 *      - NVIDIA 610.xx (forced automatic fallback to VK_EXT_descriptor_buffer, or
 *        VK_EXT_descriptor_indexing if buffer ext missing)
 *      - NVIDIA 595.xx <= 1,000,000 vs 1,000,001 descriptors (automatic fallback!)
 *      - Intel ANV (heap -> buffer -> indexing cascade)
 *      - Live T#/V#/S# descriptor heap creation & descriptor encoding.
 *   4. CreatePlacedResourceX over real xmem UMA memory + mid-frame CPU write +
 *      ExecuteCommandLists -> proves 0-frame texture/buffer lag and pre-submit
 *      scarlett_uma_flush_before_submit() (never deferred to PresentX!).
 *   5. Separated Xbox command queues (0x0 Direct, 0x2 DMA/Copy, 0x10 Async Compute XS),
 *      Enhanced Barriers (VkDependencyInfo / VkMemoryBarrier2), and ExecuteIndirect
 *      (VK_EXT_device_generated_commands or clean CPU-parsed DrawIndexedIndirect /
 *      DispatchIndirect fallback).
 *   6. DXIL Fast-Path (Path A): DXBC container stripping of XBOX / XS2M chunks with
 *      recalculated offsets, sizes, checksum, and DXIL/ILDB validation.
 *   7. 60 FPS Frame Pacing (SetFrameIntervalX / ScheduleFrameEventX / blocking
 *      WaitFrameEventX over 35 frames): monotonic 64-bit D3D12XBOX_FRAME_PIPELINE_TOKEN
 *      and measured p99 jitter < 1.5 ms, plus VRR dynamic interval update.
 *   8. Dual-Plane HDR10 PQ PresentX (G2084_P2020 -> VK_COLOR_SPACE_HDR10_ST2084_EXT +
 *      G22_P709 GameDVR) and 1-plane PresentX.
 *   9. 10 cycles of Alt+Enter / resize (ResizeSwapChainX) + SuspendX / ResumeX
 *      (including concurrent multi-threaded stress) with zero lost resources or races.
 *  10. VK_EXT_device_fault + DRED dump to PROTON_LOG file.
 *  11. PE64 COM ID3D12DeviceX vtable + diagnostic auto-stub trampoline (non-strict
 *      failure HRESULT, never S_OK, plus PROTON_XBOX_STRICT=1 abort action).
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "d3d12_xs_api.h"
#include "d3d12_xs_unix_abi.h"
#include "d3d12_xs_vk_abi.h"
#include "xmem/xmem_api.h"

extern "C" uint64_t scarlett_d3d12xs_test_translate_gpu_desc_handle(
    uint64_t xbox_gpu_handle, uint32_t *out_heap_type);
extern "C" bool scarlett_d3d12xs_test_get_pso_info(
    const void *pso,
    void **out_real_obj,
    uint32_t *out_spirv_bytes,
    uint32_t *out_is_compute);

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " condition failed: " #cond   \
                      << "\n";                                                                     \
            ++g_failures;                                                                          \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_HR(expr, expected)                                                                   \
    do {                                                                                           \
        const scarlett_hresult_t _hr = (expr);                                                     \
        if (_hr != (expected)) {                                                                   \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " " #expr " returned 0x"      \
                      << std::hex << static_cast<uint32_t>(_hr) << " (" << scarlett_error_name(_hr)\
                      << "), expected 0x" << static_cast<uint32_t>(expected) << std::dec << "\n";  \
            ++g_failures;                                                                          \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

void write_le32(std::vector<uint8_t> &buf, size_t off, uint32_t v) {
    buf[off + 0] = static_cast<uint8_t>(v & 0xFFu);
    buf[off + 1] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
    buf[off + 2] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    buf[off + 3] = static_cast<uint8_t>((v >> 24u) & 0xFFu);
}

uint32_t read_le32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

uint32_t make_fourcc(const char tag[4]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(tag[0])) |
           (static_cast<uint32_t>(static_cast<uint8_t>(tag[1])) << 8u) |
           (static_cast<uint32_t>(static_cast<uint8_t>(tag[2])) << 16u) |
           (static_cast<uint32_t>(static_cast<uint8_t>(tag[3])) << 24u);
}

struct SyntheticChunk {
    char fourcc[4];
    std::vector<uint8_t> payload;
};

std::vector<uint8_t> build_synthetic_dxbc(const std::vector<SyntheticChunk> &chunks) {
    const size_t header_and_table = 32u + chunks.size() * 4u;
    size_t cursor = header_and_table;
    std::vector<uint32_t> offsets;
    offsets.reserve(chunks.size());

    for (const auto &c : chunks) {
        cursor = (cursor + 3u) & ~size_t(3u);
        offsets.push_back(static_cast<uint32_t>(cursor));
        cursor += 8u + c.payload.size();
    }
    const size_t total = (cursor + 3u) & ~size_t(3u);

    std::vector<uint8_t> out(total, 0u);
    write_le32(out, 0u, make_fourcc("DXBC"));
    write_le32(out, 20u, 1u);
    write_le32(out, 24u, static_cast<uint32_t>(total));
    write_le32(out, 28u, static_cast<uint32_t>(chunks.size()));

    for (size_t i = 0; i < chunks.size(); ++i) {
        write_le32(out, 32u + i * 4u, offsets[i]);
        write_le32(out, offsets[i] + 0u, make_fourcc(chunks[i].fourcc));
        write_le32(out, offsets[i] + 4u, static_cast<uint32_t>(chunks[i].payload.size()));
        if (!chunks[i].payload.empty()) {
            std::memcpy(out.data() + offsets[i] + 8u,
                        chunks[i].payload.data(),
                        chunks[i].payload.size());
        }
    }
    return out;
}

uint32_t pack_nvidia_driver_version(uint32_t major, uint32_t minor, uint32_t patch) {
    return ((major & 0x3FFu) << 22u) | ((minor & 0xFFu) << 14u) | ((patch & 0xFFu) << 6u);
}

/* ============================================================================
 * Test 1: Unixlib Handshake & Real Vulkan 1.4 Device Probe (GXDK >= 0x585D070E)
 * ============================================================================ */
bool test_1_unix_handshake_and_vulkan_probe() {
    scarlett_unix_init_params_t bad{};
    bad.abi_version = 999u;
    bad.module_id = SCARLETT_D3D12XS_UNIX_MODULE_ID;
    CHECK_HR(scarlett_d3d12xs_unix_call(SCARLETT_D3D12XS_UNIX_INIT, &bad),
             SCARLETT_ERR_ABI_VERSION_MISMATCH);

    scarlett_unix_init_params_t ok_init{};
    ok_init.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    ok_init.module_id = SCARLETT_D3D12XS_UNIX_MODULE_ID;
    ok_init.console_model = SCARLETT_MODEL_ANACONDA;
    ok_init.strict_mode = 0u;
    CHECK_HR(scarlett_d3d12xs_unix_call(SCARLETT_D3D12XS_UNIX_INIT, &ok_init), SCARLETT_OK);
    CHECK(ok_init.out_unix_abi_version == SCARLETT_UNIX_CALL_ABI_VERSION);

    CHECK_HR(scarlett_d3d12xs_unix_call(9999u, &ok_init), SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE);

#if defined(SCARLETT_D3D12XS_UNIX_SO_PATH)
    /* Verify real d3d12_xs_unix.so exports callable __wine_unix_call_funcs under RTLD_LOCAL */
    {
        void *so = ::dlopen(SCARLETT_D3D12XS_UNIX_SO_PATH, RTLD_NOW | RTLD_LOCAL);
        CHECK(so != nullptr);
        using wine_entry_t = int32_t (*)(void *);
        auto *tbl = reinterpret_cast<const wine_entry_t *>(::dlsym(so, "__wine_unix_call_funcs"));
        CHECK(tbl != nullptr);
        CHECK(tbl[SCARLETT_D3D12XS_UNIX_INIT] != nullptr);
        scarlett_unix_init_params_t so_init{};
        so_init.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
        so_init.module_id = SCARLETT_D3D12XS_UNIX_MODULE_ID;
        CHECK_HR(tbl[SCARLETT_D3D12XS_UNIX_INIT](&so_init), SCARLETT_OK);
        ::dlclose(so);
    }
#endif

    D3D12XBOX_CREATE_DEVICE_PARAMETERS params{};
    params.Version = SCARLETT_GXDK_MAGIC; /* 0x585D070E */
    params.ProcessDebugFlags = 1u;        /* Request VK_LAYER_KHRONOS_validation if present */
    params.GraphicsCommandBufferSizeBytes = 16u * 1024u * 1024u;
    params.ComputeCommandBufferSizeBytes = 8u * 1024u * 1024u;
    params.DmaCommandBufferSizeBytes = 4u * 1024u * 1024u;
    params.AutoBreadcrumbNodeCount = 512u;
    params.AmplificationShaderIndirectArgsBufferSize = 8u * 1024u * 1024u;
    params.AmplificationShaderPayloadBufferSize = 32u * 1024u * 1024u;
    params.DevKitDiagnosticBufferSize = 64u * 1024u * 1024u;

    const scarlett_guid_t iid_dev = SCARLETT_IID_ID3D12DEVICEX_INIT;
    const scarlett_guid_t iid_dev_gdk = SCARLETT_IID_ID3D12DEVICEX_GDK_INIT;
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, &params, &iid_dev_gdk, &dev_raw), SCARLETT_OK);
    CHECK(dev_raw != nullptr);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);
    CHECK(dev->unix_device_handle != 0u);

    scarlett_d3d12xs_query_caps_params_t caps{};
    CHECK_HR(D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
    CHECK(caps.out_amp_indirect_bytes == 8u * 1024u * 1024u);
    CHECK(caps.out_amp_payload_bytes == 32u * 1024u * 1024u);
    CHECK(caps.out_validation_errors == 0u);

    std::cout << "[INFO] Host Vulkan Probe: GPU=\"" << caps.out_device_name
              << "\" API=" << SCAR_VK_API_VERSION_MAJOR(caps.out_vk_api_version) << "."
              << SCAR_VK_API_VERSION_MINOR(caps.out_vk_api_version) << "."
              << SCAR_VK_API_VERSION_PATCH(caps.out_vk_api_version)
              << " vendor=0x" << std::hex << caps.out_vendor_id << std::dec
              << " driver=" << caps.out_driver_major << "." << caps.out_driver_minor << "."
              << caps.out_driver_patch << " (" << caps.out_driver_info << ")"
              << " desc_backend=" << caps.out_desc_backend
              << " real_vk=" << ((caps.out_caps_mask & SCARLETT_D3D12XS_CAP_REAL_VK_DEVICE) ? "YES" : "HEADLESS")
              << " validation_layer="
              << ((caps.out_caps_mask & SCARLETT_D3D12XS_CAP_VALIDATION_LAYER)
                      ? "ENABLED (0 errors)"
                      : "NOT INSTALLED ON HOST (explicit diagnostic, 0 errors)")
              << "\n";

    /* Update Amplification Shader limits via COM vtable */
    CHECK_HR(dev->lpVtbl->SetAmplificationShaderLimits(dev, 12u * 1024u * 1024u, 48u * 1024u * 1024u),
             SCARLETT_OK);
    CHECK_HR(D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
    CHECK(caps.out_amp_indirect_bytes == 12u * 1024u * 1024u);
    CHECK(caps.out_amp_payload_bytes == 48u * 1024u * 1024u);

    /* COM AddRef / QueryInterface / Release */
    void *qi_obj = nullptr;
    CHECK_HR(dev->lpVtbl->QueryInterface(dev, &iid_dev, &qi_obj), SCARLETT_OK);
    CHECK(qi_obj == dev);
    CHECK(dev->lpVtbl->Release(dev) == 1u);
    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 2: Descriptor Heap Policy Matrix (RADV / NVIDIA 610.xx / 595.xx / ANV)
 * ============================================================================ */
bool test_2_descriptor_heap_policy_matrix_and_tvs() {
    auto eval = [](uint32_t vendor, uint32_t driver_id, uint32_t raw_ver,
                   const char *info, uint32_t count,
                   bool has_heap, bool has_buf, bool has_idx,
                   scarlett_d3d12xs_eval_desc_policy_params_t &out) -> bool {
        out = scarlett_d3d12xs_eval_desc_policy_params_t{};
        out.vendor_id = vendor;
        out.driver_id = driver_id;
        out.raw_driver_version = raw_ver;
        out.requested_descriptors = count;
        out.has_desc_heap_ext = has_heap ? 1u : 0u;
        out.has_desc_buffer_ext = has_buf ? 1u : 0u;
        out.has_desc_indexing_ext = has_idx ? 1u : 0u;
        if (info != nullptr) {
            std::strncpy(out.driver_info, info, sizeof(out.driver_info) - 1u);
        }
        return D3D12XboxEvaluateDescriptorPolicyX(&out) == SCARLETT_OK;
    };

    scarlett_d3d12xs_eval_desc_policy_params_t res{};

    /* 2a. AMD RADV with VK_EXT_descriptor_heap and <= 1M descriptors -> native VK_EXT_descriptor_heap */
    CHECK(eval(SCAR_VK_VENDOR_ID_AMD, SCAR_VK_DRIVER_ID_MESA_RADV,
               SCAR_VK_MAKE_API_VERSION(0u, 25u, 2u, 0u), "Mesa 25.2.0",
               500000u, true, true, true, res));
    CHECK(res.out_is_nvidia_610 == 0u);
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_HEAP);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_NATIVE_HEAP);

    /* 2b. AMD RADV with > 1M descriptors -> safe fallback to VK_EXT_descriptor_buffer */
    CHECK(eval(SCAR_VK_VENDOR_ID_AMD, SCAR_VK_DRIVER_ID_MESA_RADV,
               SCAR_VK_MAKE_API_VERSION(0u, 25u, 2u, 0u), "Mesa 25.2.0",
               1000001u, true, true, true, res));
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_GLOBAL_1M_LIMIT);

    /* 2c. NVIDIA 610.xx (via driverInfo) even with small heap (4096) & heap ext -> FORCED fallback to descriptor_buffer */
    CHECK(eval(SCAR_VK_VENDOR_ID_NVIDIA, SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY,
               pack_nvidia_driver_version(610u, 14u, 2u), "610.14.02",
               4096u, true, true, true, res));
    CHECK(res.out_parsed_major == 610u);
    CHECK(res.out_is_nvidia_610 == 1u);
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_NVIDIA_610_GUARD);

    /* 2d. NVIDIA 610.xx (via packed driverVersion only, empty driverInfo) -> FORCED fallback */
    CHECK(eval(SCAR_VK_VENDOR_ID_NVIDIA, SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY,
               pack_nvidia_driver_version(610u, 0u, 1u), "",
               1024u, true, true, true, res));
    CHECK(res.out_parsed_major == 610u);
    CHECK(res.out_is_nvidia_610 == 1u);
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_NVIDIA_610_GUARD);

    /* 2e. NVIDIA 610.xx WITHOUT VK_EXT_descriptor_buffer -> fallback to VK_EXT_descriptor_indexing */
    CHECK(eval(SCAR_VK_VENDOR_ID_NVIDIA, SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY,
               pack_nvidia_driver_version(610u, 55u, 0u), "610.55.00",
               2048u, true, false, true, res));
    CHECK(res.out_is_nvidia_610 == 1u);
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_DESCRIPTOR_INDEXING);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_NVIDIA_610_GUARD);

    /* 2f. NVIDIA 595.xx with exactly 1,000,000 descriptors -> VK_EXT_descriptor_heap allowed */
    CHECK(eval(SCAR_VK_VENDOR_ID_NVIDIA, SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY,
               pack_nvidia_driver_version(595u, 45u, 4u), "595.45.04",
               1000000u, true, true, true, res));
    CHECK(res.out_parsed_major == 595u);
    CHECK(res.out_is_nvidia_610 == 0u);
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_HEAP);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_NATIVE_HEAP);

    /* 2g. NVIDIA 595.xx with 1,000,001 descriptors -> FORCED automatic fallback to VK_EXT_descriptor_buffer */
    CHECK(eval(SCAR_VK_VENDOR_ID_NVIDIA, SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY,
               pack_nvidia_driver_version(595u, 45u, 4u), "595.45.04",
               1000001u, true, true, true, res));
    CHECK(res.out_parsed_major == 595u);
    CHECK(res.out_is_nvidia_610 == 0u);
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_NVIDIA_1M_LIMIT);

    /* 2h. Intel ANV: descriptor_heap -> descriptor_buffer -> descriptor_indexing */
    CHECK(eval(SCAR_VK_VENDOR_ID_INTEL, SCAR_VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA,
               SCAR_VK_MAKE_API_VERSION(0u, 25u, 1u, 3u), "Mesa 25.1.3",
               65536u, true, true, true, res));
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_HEAP);

    CHECK(eval(SCAR_VK_VENDOR_ID_INTEL, SCAR_VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA,
               SCAR_VK_MAKE_API_VERSION(0u, 25u, 1u, 3u), "Mesa 25.1.3",
               65536u, false, true, true, res));
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_HEAP_EXT_MISSING);

    CHECK(eval(SCAR_VK_VENDOR_ID_INTEL, SCAR_VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA,
               SCAR_VK_MAKE_API_VERSION(0u, 25u, 1u, 3u), "Mesa 25.1.3",
               65536u, false, false, true, res));
    CHECK(res.out_selected_backend == SCARLETT_DESC_BACKEND_DESCRIPTOR_INDEXING);
    CHECK(res.out_fallback_reason == SCARLETT_DESC_REASON_BUFFER_EXT_MISSING);

    /* 2i. Live T# / V# / S# descriptor heap creation & writes */
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    scarlett_d3d12xs_create_desc_heap_params_t cbv_heap{};
    CHECK_HR(D3D12XboxCreateDescriptorHeapX(dev->unix_device_handle,
                                            SCARLETT_DESC_HEAP_CBV_SRV_UAV,
                                            1024u, 0u, &cbv_heap),
             SCARLETT_OK);
    CHECK(cbv_heap.out_heap_handle != 0u);
    CHECK(cbv_heap.out_descriptor_stride == 32u);

    scarlett_d3d12xs_write_desc_params_t w_t{};
    w_t.device_handle = dev->unix_device_handle;
    w_t.heap_handle = cbv_heap.out_heap_handle;
    w_t.slot_index = 7u;
    w_t.desc_kind = SCARLETT_DESC_KIND_TEXTURE_T_SHARP;
    w_t.resource_va = SCARLETT_MEM_GRAPHICS_BASE + 0x100000u;
    w_t.resource_size = 1024u * 1024u * 4u;
    w_t.format = 28u; /* DXGI_FORMAT_R8G8B8A8_UNORM */
    w_t.width = 1024u;
    w_t.height = 1024u;
    w_t.mip_or_filter = 10u;
    CHECK_HR(D3D12XboxWriteDescriptorX(&w_t), SCARLETT_OK);
    CHECK(((w_t.out_encoded_word1 >> 48u) & 0xFFFFu) == 0x5423u); /* 'T#' */

    scarlett_d3d12xs_write_desc_params_t w_v{};
    w_v.device_handle = dev->unix_device_handle;
    w_v.heap_handle = cbv_heap.out_heap_handle;
    w_v.slot_index = 8u;
    w_v.desc_kind = SCARLETT_DESC_KIND_BUFFER_V_SHARP;
    w_v.resource_va = SCARLETT_MEM_GRAPHICS_BASE + 0x200000u;
    w_v.resource_size = 65536u;
    w_v.width = 64u;
    CHECK_HR(D3D12XboxWriteDescriptorX(&w_v), SCARLETT_OK);
    CHECK(((w_v.out_encoded_word1 >> 48u) & 0xFFFFu) == 0x5623u); /* 'V#' */

    scarlett_d3d12xs_create_desc_heap_params_t smp_heap{};
    CHECK_HR(D3D12XboxCreateDescriptorHeapX(dev->unix_device_handle,
                                            SCARLETT_DESC_HEAP_SAMPLER,
                                            256u, 0u, &smp_heap),
             SCARLETT_OK);
    CHECK(smp_heap.out_descriptor_stride == 16u);

    scarlett_d3d12xs_write_desc_params_t w_s{};
    w_s.device_handle = dev->unix_device_handle;
    w_s.heap_handle = smp_heap.out_heap_handle;
    w_s.slot_index = 3u;
    w_s.desc_kind = SCARLETT_DESC_KIND_SAMPLER_S_SHARP;
    w_s.format = 16u; /* 16x aniso */
    w_s.mip_or_filter = 0x55u;
    CHECK_HR(D3D12XboxWriteDescriptorX(&w_s), SCARLETT_OK);
    CHECK(((w_s.out_encoded_word1 >> 48u) & 0xFFFFu) == 0x5323u); /* 'S#' */

    /* > 1M live descriptor heap on host device must NEVER use VK_EXT_descriptor_heap */
    scarlett_d3d12xs_create_desc_heap_params_t huge_heap{};
    CHECK_HR(D3D12XboxCreateDescriptorHeapX(dev->unix_device_handle,
                                            SCARLETT_DESC_HEAP_CBV_SRV_UAV,
                                            1000001u, 0u, &huge_heap),
             SCARLETT_OK);
    CHECK(huge_heap.out_selected_backend != SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_HEAP);

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 3: CreatePlacedResourceX Over Real xmem UMA + 0-Frame Lag Pre-Submit Flush
 * ============================================================================ */
bool test_3_uma_placed_resource_zero_frame_lag() {
    CHECK_HR(XMemInitialize(SCARLETT_MODEL_ANACONDA, 0u), SCARLETT_OK);
    CHECK_HR(XMemSetConsoleModel(SCARLETT_MODEL_ANACONDA, 1u), SCARLETT_OK);

    struct UmaGuard {
        ~UmaGuard() {
            scarlett_uma_set_gpu_upload_callback(nullptr, nullptr);
            XMEM_UMA_TIER_INFO info{};
            (void)XMemUmaProbe(XMEM_UMA_PROBE_RESET_HOST, SCARLETT_UMA_TIER_AUTO, 0u, &info);
        }
    } guard;

    /* Force Tier C write-watch to strictly test pre-submit dirty flush vs PresentX */
    XMEM_UMA_TIER_INFO uma_info{};
    const uint32_t tier_c_caps = XMEM_UMA_CAP_UFFD_WP | XMEM_UMA_CAP_MPROTECT_FALLBACK;
    CHECK_HR(XMemUmaProbe(XMEM_UMA_PROBE_SET_SIM_CAPS, SCARLETT_UMA_TIER_AUTO,
                          tier_c_caps, &uma_info),
             SCARLETT_OK);
    CHECK(uma_info.SelectedTier == SCARLETT_UMA_TIER_C_UFFD_WP);

    constexpr uint64_t kPages = 4u;
    constexpr uint64_t kSize = kPages * XMEM_PAGE_SIZE_64KB;
    std::array<uint64_t, kPages> pfns{};
    uint64_t alloc_id = 0u;
    uint64_t gpu_va = 0u;

    CHECK_HR(XMemAllocatePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                                       XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
             SCARLETT_OK);
    void *va_ptr = XMemVirtualAlloc(nullptr, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                    XMEM_PAGE_READWRITE, 0u);
    CHECK(va_ptr != nullptr);
    gpu_va = reinterpret_cast<uint64_t>(va_ptr);
    CHECK_HR(XMemMapPhysicalPages(gpu_va, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                  SCARLETT_MEM_ACCESS_READWRITE, SCARLETT_MEM_CACHE_WRITEBACK),
             SCARLETT_OK);

    /* Seed initial texture texels before UMA export */
    auto *cpu_tex = reinterpret_cast<volatile uint8_t *>(static_cast<uintptr_t>(gpu_va));
    for (uint64_t i = 0; i < 64u; ++i) {
        cpu_tex[i] = static_cast<uint8_t>(0x10u + i);
    }

    scarlett_uma_tier_t active_tier = SCARLETT_UMA_TIER_AUTO;
    int32_t dmabuf_fd = -1;
    uint64_t host_ptr = 0u;
    CHECK_HR(XMemUmaExport(gpu_va, kSize, SCARLETT_UMA_TIER_AUTO, &active_tier, &dmabuf_fd, &host_ptr),
             SCARLETT_OK);
    CHECK(active_tier == SCARLETT_UMA_TIER_C_UFFD_WP);
    if (dmabuf_fd >= 0) {
        ::close(dmabuf_fd);
    }

    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    /* Reject non-MEM_GRAPHICS address */
    D3D12XBOX_RESOURCE_DESC tex_desc{};
    tex_desc.Dimension = 3u; /* Tex2D */
    tex_desc.Width = 256u;
    tex_desc.Height = 256u;
    tex_desc.DepthOrArraySize = 1u;
    tex_desc.MipLevels = 1u;
    tex_desc.Format = 28u;
    tex_desc.ByteSize = kSize;
    uint64_t bad_res = 0u;
    CHECK_HR(D3D12XboxCreatePlacedResourceX(dev->unix_device_handle,
                                            SCARLETT_MEM_TITLE_BASE, &tex_desc, 0u,
                                            &bad_res, nullptr),
             SCARLETT_ERR_XMEM_OUT_OF_RANGE);

    /* Valid CreatePlacedResourceX over MEM_GRAPHICS UMA VA */
    uint64_t res_handle = 0u;
    uint32_t res_tier = 0u;
    CHECK_HR(D3D12XboxCreatePlacedResourceX(dev->unix_device_handle, gpu_va, &tex_desc, 0u,
                                            &res_handle, &res_tier),
             SCARLETT_OK);
    CHECK(res_handle != 0u);

    /* Create Direct queue & command list */
    scarlett_d3d12xs_create_queue_params_t q_direct{};
    CHECK_HR(D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                          SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &q_direct),
             SCARLETT_OK);

    uint64_t cmd_list = 0u;
    CHECK_HR(D3D12XboxCreateCommandListX(dev->unix_device_handle,
                                         SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &cmd_list),
             SCARLETT_OK);

    /* Mid-frame CPU texture mutation (page 0 and page 2) AFTER UMA write-watch armed */
    constexpr uint64_t kMidOffset0 = 128u;
    constexpr uint64_t kMidOffset2 = 2u * XMEM_PAGE_SIZE_64KB + 64u;
    for (uint64_t i = 0; i < 32u; ++i) {
        cpu_tex[kMidOffset0 + i] = static_cast<uint8_t>(0xC0u + i);
        cpu_tex[kMidOffset2 + i] = static_cast<uint8_t>(0xE0u + i);
    }

    /* Before ExecuteCommandLists, GPU shadow still has pre-mutation bytes */
    std::array<uint8_t, 32> shadow_before{};
    CHECK_HR(scarlett_uma_read_gpu_shadow(gpu_va, kMidOffset0, shadow_before.data(),
                                          shadow_before.size()),
             SCARLETT_OK);
    CHECK(shadow_before[0] != 0xC0u);

    /* ExecuteCommandLists MUST flush dirty pages synchronously BEFORE submit (NO PresentX called!) */
    scarlett_d3d12xs_execute_cmd_lists_params_t exec_info{};
    CHECK_HR(D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q_direct.out_queue_handle,
                                           &cmd_list, 1u, gpu_va, kSize, &exec_info),
             SCARLETT_OK);
    CHECK(exec_info.out_uma_flushed_pre_submit == 1u);
    CHECK(exec_info.out_flushed_pages >= 2u);
    CHECK(exec_info.out_flushed_bytes >= 8192u);

    /* Immediately after ExecuteCommandLists (0-frame lag!), GPU shadow matches exact CPU writes */
    std::array<uint8_t, 32> shadow_after0{};
    std::array<uint8_t, 32> shadow_after2{};
    CHECK_HR(scarlett_uma_read_gpu_shadow(gpu_va, kMidOffset0, shadow_after0.data(),
                                          shadow_after0.size()),
             SCARLETT_OK);
    CHECK_HR(scarlett_uma_read_gpu_shadow(gpu_va, kMidOffset2, shadow_after2.data(),
                                          shadow_after2.size()),
             SCARLETT_OK);
    for (uint64_t i = 0; i < 32u; ++i) {
        CHECK(shadow_after0[i] == static_cast<uint8_t>(0xC0u + i));
        CHECK(shadow_after2[i] == static_cast<uint8_t>(0xE0u + i));
    }

    /* Verify write-watch was re-armed: a second mid-frame write also flushes with 0-frame lag */
    cpu_tex[kMidOffset0] = 0x5Au;
    scarlett_d3d12xs_execute_cmd_lists_params_t exec_info2{};
    CHECK_HR(D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q_direct.out_queue_handle,
                                           &cmd_list, 1u, gpu_va, kSize, &exec_info2),
             SCARLETT_OK);
    CHECK(exec_info2.out_flushed_pages >= 1u);
    uint8_t b0 = 0u;
    CHECK_HR(scarlett_uma_read_gpu_shadow(gpu_va, kMidOffset0, &b0, 1u), SCARLETT_OK);
    CHECK(b0 == 0x5Au);

    /* Confirm UMA flush counters: pre_submit >= 2, present_deferred == 0 */
    scarlett_d3d12xs_query_caps_params_t caps{};
    CHECK_HR(D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
    CHECK(caps.out_uma_pre_submit_flushes >= 2u);
    CHECK(caps.out_uma_present_flushes == 0u);

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    CHECK_HR(XMemUnmapPhysicalPages(gpu_va, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    CHECK_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
    CHECK_HR(XMemVirtualFree(gpu_va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    return true;
}

/* ============================================================================
 * Test 4: Separated Queues (0x0 / 0x2 / 0x10), Enhanced Barriers & ExecuteIndirect
 * ============================================================================ */
bool test_4_queues_enhanced_barriers_and_indirect() {
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    scarlett_d3d12xs_create_queue_params_t q_direct{};
    scarlett_d3d12xs_create_queue_params_t q_dma{};
    scarlett_d3d12xs_create_queue_params_t q_compute{};
    scarlett_d3d12xs_create_queue_params_t q_bad{};

    CHECK_HR(D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                          SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &q_direct),
             SCARLETT_OK);
    CHECK_HR(D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                          SCARLETT_D3D12_QUEUE_TYPE_DMA, 0u, &q_dma),
             SCARLETT_OK);
    CHECK_HR(D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                          SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS, 0u, &q_compute),
             SCARLETT_OK);
    CHECK_HR(D3D12XboxCreateCommandQueueX(dev->unix_device_handle, 0x99u, 0u, &q_bad),
             SCARLETT_ERR_D3D12_UNSUPPORTED_QUEUE);

    std::cout << "[INFO] Queue Families: Direct(0x0)=" << q_direct.out_vk_queue_family
              << " DMA(0x2)=" << q_dma.out_vk_queue_family
              << " (dedicated=" << q_dma.out_is_dedicated_family << ")"
              << " AsyncCompute(0x10)=" << q_compute.out_vk_queue_family
              << " (dedicated=" << q_compute.out_is_dedicated_family << ")\n";

    /* If host GPU reports dedicated families, verify they are distinct from Direct */
    if (q_dma.out_is_dedicated_family != 0u) {
        CHECK(q_dma.out_vk_queue_family != q_direct.out_vk_queue_family);
    }
    if (q_compute.out_is_dedicated_family != 0u) {
        CHECK(q_compute.out_vk_queue_family != q_direct.out_vk_queue_family);
    }

    /* Record Enhanced Barriers (GLOBAL + IMAGE + BUFFER) on Direct, Compute & DMA command lists */
    uint64_t cl_direct = 0u;
    uint64_t cl_compute = 0u;
    uint64_t cl_dma = 0u;
    CHECK_HR(D3D12XboxCreateCommandListX(dev->unix_device_handle, SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &cl_direct), SCARLETT_OK);
    CHECK_HR(D3D12XboxCreateCommandListX(dev->unix_device_handle, SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS, 0u, &cl_compute), SCARLETT_OK);
    CHECK_HR(D3D12XboxCreateCommandListX(dev->unix_device_handle, SCARLETT_D3D12_QUEUE_TYPE_DMA, 0u, &cl_dma), SCARLETT_OK);

    scarlett_d3d12xs_record_barrier_params_t b_global{};
    b_global.device_handle = dev->unix_device_handle;
    b_global.cmd_list_handle = cl_direct;
    b_global.barrier_kind = SCARLETT_BARRIER_KIND_GLOBAL;
    b_global.sync_before = SCAR_VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    b_global.sync_after = SCAR_VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    b_global.access_before = SCAR_VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    b_global.access_after = SCAR_VK_ACCESS_2_SHADER_READ_BIT;
    CHECK_HR(D3D12XboxRecordEnhancedBarrierX(&b_global), SCARLETT_OK);

    scarlett_d3d12xs_record_barrier_params_t b_img = b_global;
    b_img.cmd_list_handle = cl_compute;
    b_img.barrier_kind = SCARLETT_BARRIER_KIND_IMAGE;
    b_img.layout_before = SCAR_VK_IMAGE_LAYOUT_GENERAL;
    b_img.layout_after = SCAR_VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    CHECK_HR(D3D12XboxRecordEnhancedBarrierX(&b_img), SCARLETT_OK);

    scarlett_d3d12xs_record_barrier_params_t b_dma = b_global;
    b_dma.cmd_list_handle = cl_dma;
    b_dma.barrier_kind = SCARLETT_BARRIER_KIND_BUFFER;
    CHECK_HR(D3D12XboxRecordEnhancedBarrierX(&b_dma), SCARLETT_OK);

    /* ExecuteIndirect: DrawIndexedIndirect (CPU fallback with count buffer clamping) */
    std::array<D3D12XBOX_DRAW_INDEXED_ARGUMENTS, 3> draws{{
        {36u, 4u, 0u, 0, 0u},   /* 144 indices */
        {120u, 2u, 36u, 10, 0u},/* 240 indices */
        {999u, 1u, 0u, 0, 0u},  /* Clamped out by count_val = 2 */
    }};
    uint32_t draw_count_val = 2u;

    scarlett_d3d12xs_record_indirect_params_t ind_draw{};
    ind_draw.device_handle = dev->unix_device_handle;
    ind_draw.cmd_list_handle = cl_direct;
    ind_draw.indirect_kind = SCARLETT_INDIRECT_DRAW_INDEXED;
    ind_draw.max_command_count = 3u;
    ind_draw.arg_stride = sizeof(D3D12XBOX_DRAW_INDEXED_ARGUMENTS);
    ind_draw.force_cpu_fallback = 1u;
    ind_draw.arg_buffer_addr = reinterpret_cast<uint64_t>(draws.data());
    ind_draw.count_buffer_addr = reinterpret_cast<uint64_t>(&draw_count_val);
    CHECK_HR(D3D12XboxExecuteIndirectX(&ind_draw), SCARLETT_OK);
    CHECK(ind_draw.out_used_dgc == 0u);
    CHECK(ind_draw.out_commands_parsed == 2u);
    CHECK(ind_draw.out_total_indices_or_groups == (36u * 4u + 120u * 2u));

    /* ExecuteIndirect: DispatchIndirect */
    std::array<D3D12XBOX_DISPATCH_ARGUMENTS, 2> dispatches{{
        {8u, 4u, 1u},  /* 32 groups */
        {16u, 2u, 2u}, /* 64 groups */
    }};
    scarlett_d3d12xs_record_indirect_params_t ind_disp{};
    ind_disp.device_handle = dev->unix_device_handle;
    ind_disp.cmd_list_handle = cl_compute;
    ind_disp.indirect_kind = SCARLETT_INDIRECT_DISPATCH;
    ind_disp.max_command_count = 2u;
    ind_disp.arg_stride = sizeof(D3D12XBOX_DISPATCH_ARGUMENTS);
    ind_disp.arg_buffer_addr = reinterpret_cast<uint64_t>(dispatches.data());
    CHECK_HR(D3D12XboxExecuteIndirectX(&ind_disp), SCARLETT_OK);
    CHECK(ind_disp.out_commands_parsed == 2u);
    CHECK(ind_disp.out_total_indices_or_groups == (32u + 64u));

    /* Submit all three queues to real Vulkan hardware (0 validation errors!) */
    scarlett_d3d12xs_execute_cmd_lists_params_t ex{};
    CHECK_HR(D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q_direct.out_queue_handle,
                                           &cl_direct, 1u, 0u, 0u, &ex),
             SCARLETT_OK);
    CHECK_HR(D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q_compute.out_queue_handle,
                                           &cl_compute, 1u, 0u, 0u, &ex),
             SCARLETT_OK);
    CHECK_HR(D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q_dma.out_queue_handle,
                                           &cl_dma, 1u, 0u, 0u, &ex),
             SCARLETT_OK);

    scarlett_d3d12xs_query_caps_params_t caps{};
    CHECK_HR(D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
    CHECK(caps.out_validation_errors == 0u);

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 5: DXIL Fast-Path (Path A) — Strip XBOX / XS2M Chunks & Validate DXBC/DXIL
 * ============================================================================ */
bool test_5_dxil_strip_xbox_xs2m() {
    const std::vector<uint8_t> kDxilPayload = {0x42, 0x43, 0xC0, 0xDE, 0x01, 0x02, 0x03, 0x04,
                                               0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const std::vector<uint8_t> kSfi0Payload = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::vector<uint8_t> kXboxPayload = {0xDE, 0xAD, 0xBE, 0xEF, 0x99, 0x88, 0x77};
    const std::vector<uint8_t> kXs2mPayload = {0x58, 0x53, 0x32, 0x4D, 0xAA, 0xBB, 0xCC, 0xDD};
    const std::vector<uint8_t> kOsgnPayload = {0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};

    auto src = build_synthetic_dxbc({
        {{'S','F','I','0'}, kSfi0Payload},
        {{'X','B','O','X'}, kXboxPayload},
        {{'D','X','I','L'}, kDxilPayload},
        {{'X','S','2','M'}, kXs2mPayload},
        {{'O','S','G','N'}, kOsgnPayload},
    });

    /* Size query pass */
    scarlett_d3d12xs_strip_dxil_params_t info{};
    CHECK_HR(D3D12XboxStripDxilContainerX(src.data(), src.size(), nullptr, 0u, &info), SCARLETT_OK);
    CHECK(info.out_input_chunks == 5u);
    CHECK(info.out_output_chunks == 3u);
    CHECK(info.out_stripped_chunks == 2u);
    CHECK(info.out_has_valid_dxil == 1u);
    CHECK(info.out_dst_size > 32u && info.out_dst_size < src.size());

    /* Actual strip pass */
    std::vector<uint8_t> dst(info.out_dst_size, 0u);
    scarlett_d3d12xs_strip_dxil_params_t info2{};
    CHECK_HR(D3D12XboxStripDxilContainerX(src.data(), src.size(), dst.data(), dst.size(), &info2),
             SCARLETT_OK);
    CHECK(info2.out_dst_size == dst.size());
    CHECK(read_le32(dst.data() + 0u) == make_fourcc("DXBC"));
    CHECK(read_le32(dst.data() + 24u) == static_cast<uint32_t>(dst.size()));
    CHECK(read_le32(dst.data() + 28u) == 3u);

    /* Re-parse stripped output: verify 0 XBOX/XS2M chunks and exact DXIL payload preservation */
    bool found_dxil = false;
    for (uint32_t i = 0; i < 3u; ++i) {
        const uint32_t off = read_le32(dst.data() + 32u + i * 4u);
        CHECK(off >= 44u && off + 8u <= dst.size());
        const uint32_t fourcc = read_le32(dst.data() + off);
        const uint32_t sz = read_le32(dst.data() + off + 4u);
        CHECK(fourcc != make_fourcc("XBOX"));
        CHECK(fourcc != make_fourcc("XS2M"));
        if (fourcc == make_fourcc("DXIL")) {
            found_dxil = true;
            CHECK(sz == kDxilPayload.size());
            CHECK(std::memcmp(dst.data() + off + 8u, kDxilPayload.data(), sz) == 0);
        }
    }
    CHECK(found_dxil);

    /* Verify non-zero recalculated DXBC digest */
    bool digest_nonzero = false;
    for (size_t i = 4u; i < 20u; ++i) {
        if (dst[i] != 0u) {
            digest_nonzero = true;
            break;
        }
    }
    CHECK(digest_nonzero);

    /* Container with ILDB (debug DXIL) also accepted */
    auto ildb_src = build_synthetic_dxbc({
        {{'X','B','O','X'}, kXboxPayload},
        {{'I','L','D','B'}, kDxilPayload},
    });
    scarlett_d3d12xs_strip_dxil_params_t ildb_info{};
    CHECK_HR(D3D12XboxStripDxilContainerX(ildb_src.data(), ildb_src.size(), nullptr, 0u, &ildb_info),
             SCARLETT_OK);
    CHECK(ildb_info.out_output_chunks == 1u);
    CHECK(ildb_info.out_stripped_chunks == 1u);

    /* Container missing DXIL/ILDB MUST be rejected with SCARLETT_ERR_SHADER_INVALID_CONTAINER */
    auto no_dxil = build_synthetic_dxbc({
        {{'X','B','O','X'}, kXboxPayload},
        {{'S','F','I','0'}, kSfi0Payload},
    });
    scarlett_d3d12xs_strip_dxil_params_t bad_info{};
    CHECK_HR(D3D12XboxStripDxilContainerX(no_dxil.data(), no_dxil.size(), nullptr, 0u, &bad_info),
             SCARLETT_ERR_SHADER_INVALID_CONTAINER);

    /* Truncated / corrupt magic MUST be rejected */
    std::array<uint8_t, 16> junk{};
    CHECK_HR(D3D12XboxStripDxilContainerX(junk.data(), junk.size(), nullptr, 0u, &bad_info),
             SCARLETT_ERR_SHADER_INVALID_CONTAINER);
    return true;
}

/* ============================================================================
 * Test 6: 60 FPS Frame Pacing (35 Frames, p99 < 1.5ms) + Dual-Plane HDR10 PresentX
 * ============================================================================ */
bool test_6_frame_pacing_and_dual_plane_hdr10_presentx() {
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    /* Let OS scheduler settle after preceding Vulkan/CPU test bursts before measuring pacing. */
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    /* Set 60 FPS interval (16667 us) */
    CHECK_HR(D3D12XboxSetFrameIntervalX(dev->unix_device_handle, 16667u, 0u, 0u, 0u), SCARLETT_OK);

    constexpr int kWarmupFrames = 5;
    constexpr int kFrames = 240;
    std::vector<double> abs_jitters_ms;
    abs_jitters_ms.reserve(kFrames);

    uint64_t prev_token = 0u;
    for (int f = -kWarmupFrames; f < kFrames; ++f) {
        uint64_t token = 0u;
        uint64_t target_ns = 0u;
        CHECK_HR(D3D12XboxScheduleFrameEventX(dev->unix_device_handle,
                                              SCARLETT_FRAME_EVENT_ORIGIN, 0u,
                                              &token, &target_ns),
                 SCARLETT_OK);
        CHECK(token > prev_token);
        prev_token = token;

        scarlett_d3d12xs_wait_frame_params_t wait_info{};
        CHECK_HR(D3D12XboxWaitFrameEventX(dev->unix_device_handle,
                                          SCARLETT_FRAME_EVENT_ORIGIN,
                                          1000u, token, &wait_info),
                 SCARLETT_OK);
        CHECK(wait_info.out_token == token);

        const double jitter_ms = std::abs(static_cast<double>(wait_info.out_jitter_ns)) / 1.0e6;
        if (f >= 0) {
            abs_jitters_ms.push_back(jitter_ms);
        }

        /* Dual-Plane HDR10 PQ (G2084_P2020) + GameDVR (G22_P709) PresentX */
        D3D12XBOX_RECT rects[2]{};
        rects[0] = {0, 0, 3840, 2160};
        rects[1] = {0, 0, 1920, 1080};
        D3D12XBOX_PRESENT_PLANE_PARAMETERS planes[2]{};
        planes[0].Token = token;
        planes[0].ColorSpace = SCARLETT_COLOR_SPACE_G2084_P2020;
        planes[0].pSrcViewRects = &rects[0];
        planes[1].Token = token;
        planes[1].ColorSpace = SCARLETT_COLOR_SPACE_G22_P709;
        planes[1].pSrcViewRects = &rects[1];

        D3D12XBOX_PRESENT_PARAMETERS pparams{};
        pparams.Token = token;
        pparams.HdrMaxLuminance = 1000.0f;
        pparams.HdrMinLuminance = 0.005f;
        pparams.HdrMaxContentLightLevel = 1000.0f;
        pparams.HdrMaxFrameAverageLightLevel = 400.0f;

        scarlett_d3d12xs_presentx_params_t present_out{};
        CHECK_HR(D3D12XboxPresentX(dev->unix_device_handle, 2u, planes, &pparams, &present_out),
                 SCARLETT_OK);
        CHECK(present_out.out_vk_color_space_plane0 == SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT);
        CHECK(present_out.out_vk_color_space_plane1 == SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
        CHECK(present_out.out_gamedvr_captured == 1u);
        CHECK(present_out.out_present_serial == static_cast<uint64_t>(f + kWarmupFrames + 1));
    }

    /* Also verify 1-plane HDR10 PresentX */
    D3D12XBOX_PRESENT_PLANE_PARAMETERS single_plane{};
    single_plane.ColorSpace = SCARLETT_COLOR_SPACE_G2084_P2020;
    scarlett_d3d12xs_presentx_params_t single_out{};
    CHECK_HR(D3D12XboxPresentX(dev->unix_device_handle, 1u, &single_plane, nullptr, &single_out),
             SCARLETT_OK);
    CHECK(single_out.out_vk_color_space_plane0 == SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT);
    CHECK(single_out.out_gamedvr_captured == 0u);

    /* Compute p50, p95, p99, max jitter */
    std::sort(abs_jitters_ms.begin(), abs_jitters_ms.end());
    const size_t idx_p50 = (abs_jitters_ms.size() * 50u) / 100u;
    const size_t idx_p95 = (abs_jitters_ms.size() * 95u) / 100u;
    const size_t idx_p99 = std::min(abs_jitters_ms.size() - 1u,
                                    static_cast<size_t>(std::ceil(abs_jitters_ms.size() * 0.99)) - 1u);
    const double p50 = abs_jitters_ms[idx_p50];
    const double p95 = abs_jitters_ms[idx_p95];
    const double p99 = abs_jitters_ms[idx_p99];
    const double max_j = abs_jitters_ms.back();

    std::cout << std::fixed << std::setprecision(4)
              << "[INFO] 60 FPS Frame Pacing (" << kFrames << " frames): "
              << "p50=" << p50 << " ms, p95=" << p95 << " ms, p99=" << p99
              << " ms, max=" << max_j << " ms (gate: p99 < 1.5000 ms)\n";
    CHECK(p99 < 1.5);

    /* Test VRR dynamic frame interval switch (120Hz = 8333 us) */
    CHECK_HR(D3D12XboxSetFrameIntervalX(dev->unix_device_handle, 8333u, 0u, 1u, 0u), SCARLETT_OK);
    uint64_t vrr_tok = 0u;
    uint64_t vrr_target = 0u;
    CHECK_HR(D3D12XboxScheduleFrameEventX(dev->unix_device_handle, SCARLETT_FRAME_EVENT_PRESENT,
                                          0u, &vrr_tok, &vrr_target),
             SCARLETT_OK);
    CHECK(vrr_tok > prev_token);

    /* Verify PresentX NEVER triggered UMA flush */
    scarlett_d3d12xs_query_caps_params_t caps{};
    CHECK_HR(D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
    CHECK(caps.out_uma_present_flushes == 0u);

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 7: 10 Cycles Alt+Enter / Resize + SuspendX / ResumeX (Zero Losses / Races)
 * ============================================================================ */
bool test_7_resize_alt_enter_and_suspend_resume_cycles() {
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    /* Place 4 persistent resources across MEM_GRAPHICS */
    std::array<uint64_t, 4> res_handles{};
    for (size_t i = 0; i < res_handles.size(); ++i) {
        D3D12XBOX_RESOURCE_DESC d{};
        d.Dimension = 3u;
        d.Width = 512u;
        d.Height = 512u;
        d.DepthOrArraySize = 1u;
        d.MipLevels = 1u;
        d.Format = 28u;
        d.ByteSize = 1024u * 1024u;
        CHECK_HR(D3D12XboxCreatePlacedResourceX(
                     dev->unix_device_handle,
                     SCARLETT_MEM_GRAPHICS_BASE + (i + 1u) * (2u * 1024u * 1024u),
                     &d, 0u, &res_handles[i], nullptr),
                 SCARLETT_OK);
    }

    scarlett_d3d12xs_create_queue_params_t q{};
    CHECK_HR(D3D12XboxCreateCommandQueueX(dev->unix_device_handle,
                                          SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &q),
             SCARLETT_OK);
    uint64_t cl = 0u;
    CHECK_HR(D3D12XboxCreateCommandListX(dev->unix_device_handle,
                                         SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &cl),
             SCARLETT_OK);

    /* Concurrent caps poller to verify mutex race-freedom during 10 resize+suspend cycles */
    std::atomic<bool> stop_poller{false};
    std::atomic<bool> poller_ok{true};
    std::thread poller([&]() {
        while (!stop_poller.load(std::memory_order_relaxed)) {
            scarlett_d3d12xs_query_caps_params_t c{};
            if (D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &c) != SCARLETT_OK) {
                poller_ok.store(false, std::memory_order_relaxed);
                break;
            }
        }
    });

    uint32_t expected_gen = 1u;
    for (int cycle = 0; cycle < 10; ++cycle) {
        const uint32_t w = (cycle % 2 == 0) ? 1920u : 3840u;
        const uint32_t h = (cycle % 2 == 0) ? 1080u : 2160u;
        const uint32_t fs = (cycle % 2 == 0) ? 0u : 1u; /* Alt+Enter toggle */

        scarlett_d3d12xs_resize_swapchain_params_t rz{};
        CHECK_HR(D3D12XboxResizeSwapChainX(dev->unix_device_handle, w, h, fs, 1u, 3u, &rz),
                 SCARLETT_OK);
        ++expected_gen;
        CHECK(rz.out_generation == expected_gen);
        CHECK(rz.out_preserved_resources == 4u);

        /* SuspendX -> PresentX & ExecuteCommandLists safely return SCARLETT_E_PENDING */
        scarlett_d3d12xs_power_state_params_t pw{};
        CHECK_HR(D3D12XboxSuspendX(dev->unix_device_handle, &pw), SCARLETT_OK);
        CHECK(pw.out_is_suspended == 1u);
        CHECK(pw.out_preserved_resources == 4u);

        void *res_ptr = reinterpret_cast<void *>(static_cast<uintptr_t>(res_handles[0]));
        D3D12XBOX_PRESENT_PLANE_PARAMETERS plane{};
        plane.ResourceCount = 1u;
        plane.ppResources = &res_ptr;
        plane.ColorSpace = SCARLETT_COLOR_SPACE_G2084_P2020;
        CHECK_HR(D3D12XboxPresentX(dev->unix_device_handle, 1u, &plane, nullptr, nullptr),
                 SCARLETT_E_PENDING);

        /* ResumeX -> swapchain cleanly rebuilt, all 4 placed resources intact, PresentX succeeds */
        CHECK_HR(D3D12XboxResumeX(dev->unix_device_handle, &pw), SCARLETT_OK);
        ++expected_gen;
        CHECK(pw.out_is_suspended == 0u);
        CHECK(pw.out_swapchain_generation == expected_gen);
        CHECK(pw.out_preserved_resources == 4u);

        scarlett_d3d12xs_presentx_params_t po{};
        CHECK_HR(D3D12XboxPresentX(dev->unix_device_handle, 1u, &plane, nullptr, &po), SCARLETT_OK);
        CHECK(po.out_swapchain_generation == expected_gen);

        CHECK_HR(D3D12XboxExecuteCommandListsX(dev->unix_device_handle, q.out_queue_handle,
                                               &cl, 1u, 0u, 0u, nullptr),
                 SCARLETT_OK);
    }

    stop_poller.store(true, std::memory_order_relaxed);
    poller.join();
    CHECK(poller_ok.load());

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 8: VK_EXT_device_fault + DRED Dump to PROTON_LOG
 * ============================================================================ */
bool test_8_dred_and_device_fault_proton_log() {
    const std::string log_path =
        "/tmp/scarlett_test_dred_" + std::to_string(static_cast<uint64_t>(::getpid())) + ".log";
    ::unlink(log_path.c_str());
    ::setenv("PROTON_LOG", log_path.c_str(), 1);

    struct EnvCleanup {
        std::string path;
        ~EnvCleanup() {
            ::unsetenv("PROTON_LOG");
            ::unlink(path.c_str());
        }
    } cleanup{log_path};

    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    std::array<char, 8192> buf{};
    scarlett_d3d12xs_dred_dump_params_t dump_info{};
    CHECK_HR(D3D12XboxDumpDredFaultX(dev->unix_device_handle, buf.data(),
                                     static_cast<uint32_t>(buf.size()), 1u, &dump_info),
             SCARLETT_OK);
    CHECK(dump_info.out_bytes_written > 64u);
    CHECK(dump_info.out_breadcrumb_count >= 1u);
    CHECK(dump_info.out_proton_log_written == 1u);

    std::string report(buf.data());
    CHECK(report.find("DRED & VK_EXT_device_fault REPORT") != std::string::npos);
    CHECK(report.find("D3D12XboxCreateDevice") != std::string::npos);
    CHECK(report.find("SimulatedFaultContext") != std::string::npos);

    std::ifstream ifs(log_path);
    CHECK(ifs.is_open());
    std::ostringstream file_ss;
    file_ss << ifs.rdbuf();
    CHECK(file_ss.str().find("PROTON-SCARLETT D3D12_XS DRED") != std::string::npos);

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 9: Zero Silent Stubs — Diagnostic Auto-Stub Trampoline (Normal & Strict)
 * ============================================================================ */
struct StubCapture {
    std::mutex mu;
    std::vector<scarlett_stub_context_t> records;
    bool force_abort_action = false;
};

scarlett_stub_action_t test_stub_sink(const scarlett_stub_context_t *ctx, void *ud) {
    auto *cap = static_cast<StubCapture *>(ud);
    if (cap != nullptr && ctx != nullptr) {
        std::lock_guard<std::mutex> lock(cap->mu);
        cap->records.push_back(*ctx);
        if (cap->force_abort_action || ctx->strict_mode != 0u) {
            return SCARLETT_STUB_ACTION_LOG_AND_ABORT;
        }
    }
    return SCARLETT_STUB_ACTION_LOG_AND_FAIL;
}

bool test_9_diagnostic_auto_stub_trampoline() {
    StubCapture cap{};
    scarlett_d3d12xs_set_stub_sink(&test_stub_sink, &cap);
    struct SinkGuard {
        ~SinkGuard() {
            scarlett_d3d12xs_set_stub_sink(nullptr, nullptr);
            ::unsetenv(SCARLETT_ENV_STRICT);
        }
    } guard;

    const scarlett_guid_t iid_dev = SCARLETT_IID_ID3D12DEVICEX_INIT;
    void *out_ptr = reinterpret_cast<void *>(uintptr_t(0x1234));

    /* Unimplemented flat exports MUST fail loudly (never S_OK) and clear out pointers */
    scarlett_hresult_t hr_dbg = D3D12GetDebugInterface(&iid_dev, &out_ptr);
    CHECK(scarlett_failed(hr_dbg));
    CHECK(hr_dbg == SCARLETT_ERR_UNIMPLEMENTED_STUB);
    CHECK(out_ptr == nullptr);

    void *mesh_state = reinterpret_cast<void *>(uintptr_t(0x5678));
    CHECK(scarlett_failed(D3D12XboxCreateMeshPipelineState(
        reinterpret_cast<void *>(uintptr_t(0x1111)),
        reinterpret_cast<void *>(uintptr_t(0x2222)), &iid_dev, &mesh_state)));
    CHECK(mesh_state == nullptr);

    CHECK(scarlett_failed(D3D12SerializeRootSignature(nullptr, 1u, nullptr, nullptr)));
    CHECK(scarlett_failed(D3D12CreateRootSignatureDeserializer(nullptr, 0u, nullptr, nullptr)));

    /* Removed & unimplemented COM vtable slots on ID3D12DeviceX */
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);

    scarlett_hresult_t hr_legacy = dev->lpVtbl->LegacyPresentSyncX(dev, 42u, 100u);
    CHECK(hr_legacy == SCARLETT_E_NOTIMPL);
    void *res_dbg = nullptr;
    scarlett_hresult_t hr_res_dbg =
        dev->lpVtbl->CreateReservedResourceDebugX(dev, nullptr, 0u, &iid_dev, &res_dbg);
    CHECK(hr_res_dbg == SCARLETT_ERR_UNIMPLEMENTED_STUB);

    /* Verify captured register context & metadata */
    {
        std::lock_guard<std::mutex> lock(cap.mu);
        CHECK(cap.records.size() == 6u);
        CHECK(std::strcmp(cap.records[0].dll_name, "d3d12_xs.dll") == 0);
        CHECK(std::strcmp(cap.records[0].symbol_name, "D3D12GetDebugInterface") == 0);
        CHECK(cap.records[0].rcx == reinterpret_cast<uint64_t>(&iid_dev));
        CHECK((cap.records[4].flags & SCARLETT_STUB_FLAG_REMOVED_IN_ABI) != 0u);
        CHECK(cap.records[4].vtable_slot == 7u);
        CHECK(cap.records[4].rdx == 42u);
        CHECK(cap.records[5].vtable_slot == 8u);
    }

    /* Strict mode (PROTON_XBOX_STRICT=1) returns SCARLETT_ERR_STRICT_STUB_ABORT on host */
    ::setenv(SCARLETT_ENV_STRICT, "1", 1);
    scarlett_hresult_t hr_strict = D3D12GetDebugInterface(&iid_dev, &out_ptr);
    CHECK(hr_strict == SCARLETT_ERR_STRICT_STUB_ABORT);

    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 10: Command List Side-Table Isolation & Shader Lifting Bridge
 * ============================================================================ */
bool test_10_cmdlist_sidetable_and_shader_lift() {
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);
    auto *dev_vtbl = reinterpret_cast<void * const *>(dev->lpVtbl);

    const scarlett_guid_t iid_unk = {
        0x00000000u, 0x0000u, 0x0000u,
        {0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u}};

    struct QueueDesc { uint32_t Type, Priority, Flags, NodeMask; } q_desc{0u, 0u, 0u, 0u};
    using PfnCreateQueue = scarlett_hresult_t (*)(ID3D12DeviceX *, const QueueDesc *, const scarlett_guid_t *, void **);
    using PfnCreateAlloc = scarlett_hresult_t (*)(ID3D12DeviceX *, uint32_t, const scarlett_guid_t *, void **);
    using PfnCreateList = scarlett_hresult_t (*)(ID3D12DeviceX *, uint32_t, uint32_t, void *, void *, const scarlett_guid_t *, void **);

    void *queue_raw = nullptr;
    void *alloc_raw = nullptr;
    void *cl_raw = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreateQueue>(dev_vtbl[8])(dev, &q_desc, &iid_unk, &queue_raw), SCARLETT_OK);
    CHECK_HR(reinterpret_cast<PfnCreateAlloc>(dev_vtbl[9])(dev, 0u, &iid_unk, &alloc_raw), SCARLETT_OK);
    CHECK_HR(reinterpret_cast<PfnCreateList>(dev_vtbl[12])(dev, 0u, 0u, alloc_raw, nullptr, &iid_unk, &cl_raw), SCARLETT_OK);
    CHECK(queue_raw != nullptr && alloc_raw != nullptr && cl_raw != nullptr);

    /* Simulate Xbox inline writes into ID3D12GraphicsCommandListX beyond lpVtbl (incl. +0x118) */
    uint8_t *cl_bytes = static_cast<uint8_t *>(cl_raw);
    for (size_t off = sizeof(void *); off < 0x400u; ++off) {
        cl_bytes[off] = static_cast<uint8_t>(0xC0u + (off & 0x0Fu));
    }

    uint32_t label_target = 0u;
    auto *cl_vtbl = *reinterpret_cast<void * const * const *>(cl_raw);
    auto *alloc_vtbl = *reinterpret_cast<void * const * const *>(alloc_raw);
    auto *q_vtbl = *reinterpret_cast<void * const * const *>(queue_raw);
    using PfnWriteLabel = void (*)(void *, uint64_t, uint32_t, uint32_t, uint32_t);
    using PfnClose = scarlett_hresult_t (*)(void *);
    using PfnExec = void (*)(void *, uint32_t, void * const *);
    using PfnRelease = uint32_t (*)(void *);

    reinterpret_cast<PfnWriteLabel>(cl_vtbl[71])(
        cl_raw, reinterpret_cast<uint64_t>(&label_target), 0x12345678u, 0u, 0u);
    CHECK_HR(reinterpret_cast<PfnClose>(cl_vtbl[9])(cl_raw), SCARLETT_OK);
    reinterpret_cast<PfnExec>(q_vtbl[10])(queue_raw, 1u, &cl_raw);
    CHECK(label_target == 0x12345678u);

    /* Path A via SCARLETT_D3D12XS_UNIX_LIFT_SHADER: DXBC with XBOX + DXIL */
    const std::vector<uint8_t> kXboxChunk = {'X','B','O','X',1,2,3,4};
    const std::vector<uint8_t> kDxilChunk = {'B','C',0xC0u,0xDEu,1,0,0,0};
    auto dxbc_src = build_synthetic_dxbc({
        {{'X','B','O','X'}, kXboxChunk},
        {{'D','X','I','L'}, kDxilChunk},
    });
    std::vector<uint8_t> lift_out(4096u, 0u);
    scarlett_d3d12xs_lift_shader_params_t lp{};
    lp.src_addr = reinterpret_cast<uint64_t>(dxbc_src.data());
    lp.src_size = dxbc_src.size();
    lp.dst_addr = reinterpret_cast<uint64_t>(lift_out.data());
    lp.dst_capacity = lift_out.size();
    CHECK_HR(scarlett_d3d12xs_unix_call(SCARLETT_D3D12XS_UNIX_LIFT_SHADER, &lp), SCARLETT_OK);
    CHECK(lp.out_status == SCARLETT_OK);
    CHECK(lp.out_used_dxil_fast_path == 1u);
    CHECK(lp.out_container_kind == 1u);
    CHECK(lp.out_dst_size > 0u && lp.out_dst_size < dxbc_src.size());

    /* Path B via SCARLETT_D3D12XS_UNIX_LIFT_SHADER: minimal XS2 compute shader -> SPIR-V */
    std::vector<uint8_t> xs2_src(52u, 0u);
    write_le32(xs2_src, 0u, 0x00325358u);      /* 'XS2\0' */
    write_le32(xs2_src, 4u, (2u << 16u) | 1u); /* version=1, stage=2 (CS) */
    write_le32(xs2_src, 8u, (4u << 16u) | 64u);/* wave_size=64, vgpr=4 */
    write_le32(xs2_src, 12u, 4u);              /* sgpr=4, bindings=0 */
    write_le32(xs2_src, 20u, 1u);              /* wg_x=1 */
    write_le32(xs2_src, 24u, 1u);              /* wg_y=1 */
    write_le32(xs2_src, 28u, 1u);              /* wg_z=1 */
    write_le32(xs2_src, 40u, 48u);             /* isa_offset=48 */
    write_le32(xs2_src, 44u, 4u);              /* isa_bytes=4 */
    write_le32(xs2_src, 48u, 0xBF810000u);     /* s_endpgm */
    std::vector<uint8_t> spirv_out(65536u, 0u);
    scarlett_d3d12xs_lift_shader_params_t spv_lp{};
    spv_lp.src_addr = reinterpret_cast<uint64_t>(xs2_src.data());
    spv_lp.src_size = xs2_src.size();
    spv_lp.dst_addr = reinterpret_cast<uint64_t>(spirv_out.data());
    spv_lp.dst_capacity = spirv_out.size();
    CHECK_HR(scarlett_d3d12xs_unix_call(SCARLETT_D3D12XS_UNIX_LIFT_SHADER, &spv_lp), SCARLETT_OK);
    CHECK(spv_lp.out_status == SCARLETT_OK);
    CHECK(spv_lp.out_used_dxil_fast_path == 0u);
    CHECK(spv_lp.out_container_kind == 2u);
    CHECK(spv_lp.out_dst_size >= 20u);
    CHECK(read_le32(spirv_out.data()) == 0x07230203u);

    CHECK(reinterpret_cast<PfnRelease>(cl_vtbl[2])(cl_raw) == 0u);
    CHECK(reinterpret_cast<PfnRelease>(alloc_vtbl[2])(alloc_raw) == 0u);
    CHECK(reinterpret_cast<PfnRelease>(q_vtbl[2])(queue_raw) == 0u);
    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 11: Descriptor Heap Hidden Out-Ptr ABI & Clamped Bounds Validation
 * ============================================================================ */
namespace mock_vkd3d_dh {

struct MockDescHeapDesc {
    uint32_t Type;
    uint32_t NumDescriptors;
    uint32_t Flags;
    uint32_t NodeMask;
};

struct MockRealDescHeap {
    void * const *lpVtbl;
    uint64_t cpu_va;
    uint64_t gpu_va;
    uint32_t ref_count;
    bool gstart_received_nonnull_out;
};

struct MockRealDevice {
    void * const *lpVtbl;
    uint32_t create_dh_calls;
    uint32_t srv_calls;
    uint64_t last_srv_dest;
    uint32_t copy_desc_calls;
    uint64_t last_copy_dst;
    uint64_t last_copy_src;
    uint32_t copy_simple_calls;
    uint64_t last_simple_dst;
    uint64_t last_simple_src;
    uint32_t last_simple_count;
    MockRealDescHeap heaps[4];
    uint32_t heap_count;
};

uint32_t mock_dh_release(void *iface) {
    auto *h = static_cast<MockRealDescHeap *>(iface);
    if (h->ref_count > 0u) {
        --h->ref_count;
    }
    return h->ref_count;
}

uint64_t *mock_dh_get_cpu_start(void *iface, uint64_t *out) {
    auto *h = static_cast<MockRealDescHeap *>(iface);
    if (out != nullptr) {
        h->gstart_received_nonnull_out = true;
        *out = h->cpu_va;
    }
    return out;
}

uint64_t *mock_dh_get_gpu_start(void *iface, uint64_t *out) {
    auto *h = static_cast<MockRealDescHeap *>(iface);
    if (out != nullptr) {
        *out = h->gpu_va;
    }
    return out;
}

void * const kMockDhVtbl[16] = {
    nullptr, nullptr, reinterpret_cast<void *>(mock_dh_release),
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr,
    reinterpret_cast<void *>(mock_dh_get_cpu_start),
    reinterpret_cast<void *>(mock_dh_get_gpu_start),
};

scarlett_hresult_t mock_dev_create_dh(
    void *iface, const MockDescHeapDesc *desc, const scarlett_guid_t *, void **pp) {
    auto *d = static_cast<MockRealDevice *>(iface);
    ++d->create_dh_calls;
    if (desc == nullptr || pp == nullptr) {
        return SCARLETT_E_INVALIDARG;
    }
    if ((desc->Flags & 0x1u) != 0u && desc->Type == 0u && desc->NumDescriptors > 1000000u) {
        *pp = nullptr;
        return SCARLETT_E_INVALIDARG;
    }
    if (d->heap_count >= 4u) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    MockRealDescHeap *h = &d->heaps[d->heap_count];
    h->lpVtbl = kMockDhVtbl;
    h->cpu_va = UINT64_C(0x700000000000) + static_cast<uint64_t>(d->heap_count) * UINT64_C(0x10000000);
    h->gpu_va = ((desc->Flags & 0x1u) != 0u) ? (UINT64_C(0x800000000000) + static_cast<uint64_t>(d->heap_count) * UINT64_C(0x10000000)) : 0u;
    h->ref_count = 1u;
    h->gstart_received_nonnull_out = false;
    ++d->heap_count;
    *pp = h;
    return SCARLETT_OK;
}

uint32_t mock_dev_get_inc(void *, uint32_t) {
    return 64u;
}

void mock_dev_create_srv(void *iface, void *, const void *, uint64_t dest) {
    auto *d = static_cast<MockRealDevice *>(iface);
    ++d->srv_calls;
    d->last_srv_dest = dest;
}

void mock_dev_copy_desc(
    void *iface, uint32_t n_dst, const uint64_t *dsts, const uint32_t *,
    uint32_t n_src, const uint64_t *srcs, const uint32_t *, uint32_t) {
    auto *d = static_cast<MockRealDevice *>(iface);
    ++d->copy_desc_calls;
    if (n_dst > 0u && dsts != nullptr) {
        d->last_copy_dst = dsts[0];
    }
    if (n_src > 0u && srcs != nullptr) {
        d->last_copy_src = srcs[0];
    }
}

void mock_dev_copy_simple(
    void *iface, uint32_t count, uint64_t dst, uint64_t src, uint32_t) {
    auto *d = static_cast<MockRealDevice *>(iface);
    ++d->copy_simple_calls;
    d->last_simple_count = count;
    d->last_simple_dst = dst;
    d->last_simple_src = src;
}

void * const kMockDevVtbl[40] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    reinterpret_cast<void *>(mock_dev_create_dh),     /* slot 14 */
    reinterpret_cast<void *>(mock_dev_get_inc),       /* slot 15 */
    nullptr,
    nullptr,
    reinterpret_cast<void *>(mock_dev_create_srv),    /* slot 18 */
    nullptr, nullptr, nullptr, nullptr,
    reinterpret_cast<void *>(mock_dev_copy_desc),     /* slot 23 */
    reinterpret_cast<void *>(mock_dev_copy_simple),   /* slot 24 */
};

} // namespace mock_vkd3d_dh

bool test_11_descriptor_heap_abi_and_bounds() {
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);
    void *saved_real = dev->real_device;

    mock_vkd3d_dh::MockRealDevice mock_dev{};
    mock_dev.lpVtbl = mock_vkd3d_dh::kMockDevVtbl;
    dev->real_device = &mock_dev;

    auto *dev_vtbl = reinterpret_cast<void * const *>(dev->lpVtbl);
    const scarlett_guid_t iid_dh = {
        0x8EFB471Du, 0x616Cu, 0x4F49u,
        {0x90u, 0xF7u, 0x12u, 0x7Bu, 0xB7u, 0x63u, 0xFAu, 0x51u}};

    using PfnCreateDH = scarlett_hresult_t (*)(
        ID3D12DeviceX *, const mock_vkd3d_dh::MockDescHeapDesc *, const scarlett_guid_t *, void **);
    using PfnCreateSRV = void (*)(ID3D12DeviceX *, void *, const void *, uint64_t);
    using PfnCopyDesc = void (*)(
        ID3D12DeviceX *, uint32_t, const uint64_t *, const uint32_t *,
        uint32_t, const uint64_t *, const uint32_t *, uint32_t);
    using PfnCopySimple = void (*)(ID3D12DeviceX *, uint32_t, uint64_t, uint64_t, uint32_t);

    /* Heap 0: shader-visible 0x130000 -> mock rejects > 1M, d3d12_xs clamps to 1000000 */
    mock_vkd3d_dh::MockDescHeapDesc dh_desc0{0u, 0x130000u, 1u, 0u};
    void *dh0_raw = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreateDH>(dev_vtbl[14])(dev, &dh_desc0, &iid_dh, &dh0_raw), SCARLETT_OK);
    CHECK(dh0_raw != nullptr);
    CHECK(mock_dev.create_dh_calls == 2u);
    CHECK(mock_dev.heap_count == 1u);
    CHECK(mock_dev.heaps[0].gstart_received_nonnull_out);

    /* Heap 1: non-shader-visible staging heap (256 descriptors) */
    mock_vkd3d_dh::MockDescHeapDesc dh_desc1{0u, 256u, 0u, 0u};
    void *dh1_raw = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreateDH>(dev_vtbl[14])(dev, &dh_desc1, &iid_dh, &dh1_raw), SCARLETT_OK);
    CHECK(dh1_raw != nullptr);
    CHECK(mock_dev.heap_count == 2u);

    /* Verify Xbox vtable GetCPUDescriptorHandleForHeapStart / GetGPUDescriptorHandleForHeapStart
     * return scalar handles in RAX (1-arg call) and GetDesc writes struct */
    auto *dh0_vtbl = *reinterpret_cast<void * const * const *>(dh0_raw);
    auto *dh1_vtbl = *reinterpret_cast<void * const * const *>(dh1_raw);
    using PfnGetStart = uint64_t (*)(void *);
    using PfnGetDesc = void *(*)(void *, mock_vkd3d_dh::MockDescHeapDesc *);
    using PfnRelease = uint32_t (*)(void *);

    const uint64_t cpu0 = reinterpret_cast<PfnGetStart>(dh0_vtbl[9])(dh0_raw);
    const uint64_t gpu0 = reinterpret_cast<PfnGetStart>(dh0_vtbl[10])(dh0_raw);
    const uint64_t cpu1 = reinterpret_cast<PfnGetStart>(dh1_vtbl[9])(dh1_raw);
    CHECK(cpu0 != 0u && gpu0 == cpu0 && cpu1 != 0u && cpu1 != cpu0);

    mock_vkd3d_dh::MockDescHeapDesc got_desc{};
    CHECK(reinterpret_cast<PfnGetDesc>(dh0_vtbl[8])(dh0_raw, &got_desc) == &got_desc);
    CHECK(got_desc.NumDescriptors == 0x130000u);

    /* In-bounds SRV at index 5 in heap 0 -> real_cpu_start + 5 * 64 */
    reinterpret_cast<PfnCreateSRV>(dev_vtbl[18])(dev, nullptr, nullptr, cpu0 + 5u * 32u);
    CHECK(mock_dev.srv_calls == 1u);
    CHECK(mock_dev.last_srv_dest == mock_dev.heaps[0].cpu_va + 5u * 64u);

    /* Out-of-bounds SRV at index 1000000 (within Xbox 0x130000, beyond clamped 1M) -> must NOT forward */
    reinterpret_cast<PfnCreateSRV>(dev_vtbl[18])(dev, nullptr, nullptr, cpu0 + 1000000u * 32u);
    CHECK(mock_dev.srv_calls == 1u);

    /* CopyDescriptors in-bounds (heap 1[3] -> heap 0[10]) -> translates both using real_inc_size=64 */
    const uint64_t dst_range = cpu0 + 10u * 32u;
    const uint64_t src_range = cpu1 + 3u * 32u;
    const uint32_t range_sz = 4u;
    reinterpret_cast<PfnCopyDesc>(dev_vtbl[23])(
        dev, 1u, &dst_range, &range_sz, 1u, &src_range, &range_sz, 0u);
    CHECK(mock_dev.copy_desc_calls == 1u);
    CHECK(mock_dev.last_copy_dst == mock_dev.heaps[0].cpu_va + 10u * 64u);
    CHECK(mock_dev.last_copy_src == mock_dev.heaps[1].cpu_va + 3u * 64u);

    /* CopyDescriptorsSimple crossing clamped 1M boundary (start=999998, count=4) -> must NOT forward */
    reinterpret_cast<PfnCopySimple>(dev_vtbl[24])(
        dev, 4u, cpu0 + 999998u * 32u, cpu1 + 0u * 32u, 0u);
    CHECK(mock_dev.copy_simple_calls == 0u);

    /* CopyDescriptorsSimple strictly within clamped 1M (start=999996, count=4) -> forwards */
    reinterpret_cast<PfnCopySimple>(dev_vtbl[24])(
        dev, 4u, cpu0 + 999996u * 32u, cpu1 + 0u * 32u, 0u);
    CHECK(mock_dev.copy_simple_calls == 1u);
    CHECK(mock_dev.last_simple_count == 4u);
    CHECK(mock_dev.last_simple_dst == mock_dev.heaps[0].cpu_va + 999996u * 64u);
    CHECK(mock_dev.last_simple_src == mock_dev.heaps[1].cpu_va);

    /* GPU descriptor handle translation: shader-visible in-bounds, OOB vs 1M clamp, and non-shader-visible */
    uint32_t gpu_heap_type = 99u;
    CHECK(scarlett_d3d12xs_test_translate_gpu_desc_handle(gpu0 + 7u * 32u, &gpu_heap_type) ==
          mock_dev.heaps[0].gpu_va + 7u * 64u);
    CHECK(gpu_heap_type == 0u);
    CHECK(scarlett_d3d12xs_test_translate_gpu_desc_handle(gpu0 + 1000000u * 32u, nullptr) == 0u);
    const uint64_t gpu1 = reinterpret_cast<PfnGetStart>(dh1_vtbl[10])(dh1_raw);
    CHECK(scarlett_d3d12xs_test_translate_gpu_desc_handle(gpu1, nullptr) == 0u);

    CHECK(reinterpret_cast<PfnRelease>(dh1_vtbl[2])(dh1_raw) == 0u);
    CHECK(reinterpret_cast<PfnRelease>(dh0_vtbl[2])(dh0_raw) == 0u);
    dev->real_device = saved_real;
    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

/* ============================================================================
 * Test 12: Dynamic PSO/StateObject Capacity, Shader Lift, Resource Flags & Slot61
 * ============================================================================ */
namespace mock_vkd3d_m3 {

struct MockResource {
    void * const *lpVtbl;
    uint64_t gpu_va;
    uint32_t ref_count;
};

struct MockRootSig {
    void * const *lpVtbl;
    uint32_t ref_count;
};

struct MockPso {
    void * const *lpVtbl;
    uint32_t ref_count;
};

struct MockResDesc {
    uint32_t Dimension;
    uint64_t Alignment;
    uint64_t Width;
    uint32_t Height;
    uint16_t DepthOrArraySize;
    uint16_t MipLevels;
    uint32_t Format;
    uint32_t SampleCount;
    uint32_t SampleQuality;
    uint32_t Layout;
    uint32_t Flags;
};

struct MockDevice {
    void * const *lpVtbl;
    uint32_t create_rs_calls;
    uint32_t create_compute_pso_calls;
    uint32_t create_stream_pso_calls;
    uint32_t create_res_calls;
    uint32_t create_res_rejected;
    uint32_t create_dsv_calls;
    uint32_t create_srv_calls;
    uint32_t create_sampler_calls;
    uint64_t last_sampler_dest;
    uint32_t last_sampler_words[13];
    uint32_t last_accepted_flags;
    uint32_t last_accepted_samples;
    bool last_pso_had_real_rs;
    bool last_pso_had_clean_dxbc;
    MockRootSig rs_pool[4];
    uint32_t rs_count;
    MockPso pso_pool[8];
    uint32_t pso_count;
    MockResource res_pool[8];
    uint32_t res_count;
    mock_vkd3d_dh::MockRealDescHeap dh_pool[4];
    uint32_t dh_count;
};

uint32_t mock_obj_release(void *iface) {
    (void)iface;
    return 0u;
}

uint64_t mock_res_get_va(void *iface) {
    return static_cast<MockResource *>(iface)->gpu_va;
}

void * const kMockObjVtbl[16] = {
    nullptr, nullptr, reinterpret_cast<void *>(mock_obj_release),
    nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, reinterpret_cast<void *>(mock_res_get_va),
};

scarlett_hresult_t mock_create_dh(
    void *iface, const mock_vkd3d_dh::MockDescHeapDesc *desc, const scarlett_guid_t *, void **pp) {
    auto *d = static_cast<MockDevice *>(iface);
    if (desc == nullptr || pp == nullptr || d->dh_count >= 4u) {
        return SCARLETT_E_INVALIDARG;
    }
    auto *h = &d->dh_pool[d->dh_count];
    h->lpVtbl = mock_vkd3d_dh::kMockDhVtbl;
    h->cpu_va = UINT64_C(0x710000000000) + static_cast<uint64_t>(d->dh_count) * UINT64_C(0x10000000);
    h->gpu_va = UINT64_C(0x810000000000) + static_cast<uint64_t>(d->dh_count) * UINT64_C(0x10000000);
    h->ref_count = 1u;
    ++d->dh_count;
    *pp = h;
    return SCARLETT_OK;
}

uint32_t mock_get_inc(void *, uint32_t) {
    return 64u;
}

scarlett_hresult_t mock_create_rs(
    void *iface, uint32_t, const void *, size_t, const scarlett_guid_t *, void **pp) {
    auto *d = static_cast<MockDevice *>(iface);
    ++d->create_rs_calls;
    if (d->rs_count >= 4u) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    auto *rs = &d->rs_pool[d->rs_count++];
    rs->lpVtbl = kMockObjVtbl;
    rs->ref_count = 1u;
    *pp = rs;
    return SCARLETT_OK;
}

static bool has_clean_dxbc(const void *ptr, uint64_t len) {
    if (ptr == nullptr || len < 36u) {
        return false;
    }
    const auto *b = static_cast<const uint8_t *>(ptr);
    if (b[0] != 'D' || b[1] != 'X' || b[2] != 'B' || b[3] != 'C') {
        return false;
    }
    uint32_t nchunks = read_le32(b + 28u);
    if (nchunks == 0u || 32u + nchunks * 4u > len) {
        return false;
    }
    for (uint32_t i = 0u; i < nchunks; ++i) {
        uint32_t off = read_le32(b + 32u + i * 4u);
        if (off + 8u > len) {
            return false;
        }
        uint32_t fourcc = read_le32(b + off);
        if (fourcc == 0x584F4258u || fourcc == 0x4D325358u) {
            return false;
        }
    }
    return true;
}

scarlett_hresult_t mock_create_compute_pso(
    void *iface, const void *pDesc, const scarlett_guid_t *, void **pp) {
    auto *d = static_cast<MockDevice *>(iface);
    ++d->create_compute_pso_calls;
    if (pDesc == nullptr || pp == nullptr || d->pso_count >= 8u) {
        return SCARLETT_E_INVALIDARG;
    }
    const auto *b = static_cast<const uint8_t *>(pDesc);
    const void *rs = *reinterpret_cast<void * const *>(b + 0x00u);
    const void *cs = *reinterpret_cast<void * const *>(b + 0x08u);
    uint64_t cs_len = *reinterpret_cast<const uint64_t *>(b + 0x10u);
    d->last_pso_had_real_rs = (rs == &d->rs_pool[0]);
    d->last_pso_had_clean_dxbc = has_clean_dxbc(cs, cs_len);
    if (!d->last_pso_had_real_rs || !d->last_pso_had_clean_dxbc) {
        return SCARLETT_E_INVALIDARG;
    }
    auto *pso = &d->pso_pool[d->pso_count++];
    pso->lpVtbl = kMockObjVtbl;
    pso->ref_count = 1u;
    *pp = pso;
    return SCARLETT_OK;
}

scarlett_hresult_t mock_create_stream_pso(
    void *iface, const void *pStreamDesc, const scarlett_guid_t *, void **pp) {
    auto *d = static_cast<MockDevice *>(iface);
    ++d->create_stream_pso_calls;
    if (pStreamDesc == nullptr || pp == nullptr || d->pso_count >= 8u) {
        return SCARLETT_E_INVALIDARG;
    }
    const auto *sd = static_cast<const uint64_t *>(pStreamDesc);
    uint64_t sz = sd[0];
    const auto *stream = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(sd[1]));
    if (sz < 24u || stream == nullptr) {
        return SCARLETT_E_INVALIDARG;
    }
    bool found_rs = false;
    bool found_dxbc = false;
    const auto *sq = reinterpret_cast<const uint64_t *>(stream);
    for (size_t i = 0u; i < sz / 8u; ++i) {
        if (sq[i] == reinterpret_cast<uint64_t>(&d->rs_pool[0])) {
            found_rs = true;
        }
        if (i + 1u < sz / 8u && sq[i] > 0x10000u && sq[i + 1u] >= 36u && sq[i + 1u] < 65536u) {
            const void *p = reinterpret_cast<const void *>(static_cast<uintptr_t>(sq[i]));
            if (has_clean_dxbc(p, sq[i + 1u])) {
                found_dxbc = true;
            }
        }
    }
    d->last_pso_had_real_rs = found_rs;
    d->last_pso_had_clean_dxbc = found_dxbc;
    if (!found_rs || !found_dxbc) {
        return SCARLETT_E_INVALIDARG;
    }
    auto *pso = &d->pso_pool[d->pso_count++];
    pso->lpVtbl = kMockObjVtbl;
    pso->ref_count = 1u;
    *pp = pso;
    return SCARLETT_OK;
}

scarlett_hresult_t mock_create_committed_res(
    void *iface, const void *, uint32_t, const MockResDesc *desc,
    uint32_t, const void *, const scarlett_guid_t *, void **pp) {
    auto *d = static_cast<MockDevice *>(iface);
    ++d->create_res_calls;
    if (desc == nullptr || pp == nullptr || d->res_count >= 8u) {
        return SCARLETT_E_INVALIDARG;
    }
    /* Reject invalid PC D3D12 flag combinations just like vkd3d-proton */
    const uint32_t f = desc->Flags;
    const bool is_ds = (f & 0x02u) != 0u;
    const bool valid_ds_fmt = (desc->Format == 19u || desc->Format == 20u ||
                               desc->Format == 39u || desc->Format == 40u ||
                               desc->Format == 44u || desc->Format == 45u ||
                               desc->Format == 54u || desc->Format == 55u);
    if ((is_ds && (f & (0x01u | 0x04u | 0x20u)) != 0u) ||
        (is_ds && !valid_ds_fmt) ||
        (!is_ds && (f & 0x08u) != 0u) ||
        (desc->SampleCount > 1u && ((f & (0x04u | 0x20u)) != 0u || desc->MipLevels > 1u))) {
        ++d->create_res_rejected;
        *pp = nullptr;
        return SCARLETT_E_INVALIDARG;
    }
    d->last_accepted_flags = f;
    d->last_accepted_samples = desc->SampleCount;
    auto *r = &d->res_pool[d->res_count];
    r->lpVtbl = kMockObjVtbl;
    r->gpu_va = UINT64_C(0x900000000000) + static_cast<uint64_t>(d->res_count) * 0x100000u;
    r->ref_count = 1u;
    ++d->res_count;
    *pp = r;
    return SCARLETT_OK;
}

void mock_create_srv(void *iface, void *res, const void *, uint64_t) {
    auto *d = static_cast<MockDevice *>(iface);
    if (res != nullptr) {
        ++d->create_srv_calls;
    }
}

void mock_create_dsv(void *iface, void *res, const void *, uint64_t) {
    auto *d = static_cast<MockDevice *>(iface);
    if (res != nullptr) {
        ++d->create_dsv_calls;
    }
}

void mock_create_sampler(void *iface, const void *pDesc, uint64_t dest) {
    auto *d = static_cast<MockDevice *>(iface);
    ++d->create_sampler_calls;
    d->last_sampler_dest = dest;
    if (pDesc != nullptr) {
        std::memcpy(d->last_sampler_words, pDesc, sizeof(d->last_sampler_words));
    }
}

void * build_vtbl_entry(size_t idx) {
    switch (idx) {
        case 11: return reinterpret_cast<void *>(mock_create_compute_pso);
        case 14: return reinterpret_cast<void *>(mock_create_dh);
        case 15: return reinterpret_cast<void *>(mock_get_inc);
        case 16: return reinterpret_cast<void *>(mock_create_rs);
        case 18: return reinterpret_cast<void *>(mock_create_srv);
        case 21: return reinterpret_cast<void *>(mock_create_dsv);
        case 22: return reinterpret_cast<void *>(mock_create_sampler);
        case 27: return reinterpret_cast<void *>(mock_create_committed_res);
        case 47: return reinterpret_cast<void *>(mock_create_stream_pso);
        default: return nullptr;
    }
}

struct VtblHolder {
    void *slots[56];
    VtblHolder() {
        for (size_t i = 0; i < 56; ++i) {
            slots[i] = build_vtbl_entry(i);
        }
    }
};

const VtblHolder kMockDevVtblHolder;

} // namespace mock_vkd3d_m3

bool test_12_pso_capacity_shader_lift_res_flags_and_slot61() {
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, nullptr, &dev_raw), SCARLETT_OK);
    auto *dev = static_cast<ID3D12DeviceX *>(dev_raw);
    void *saved_real = dev->real_device;

    mock_vkd3d_m3::MockDevice mock_dev{};
    mock_dev.lpVtbl = mock_vkd3d_m3::kMockDevVtblHolder.slots;
    dev->real_device = &mock_dev;

    auto *dev_vtbl = reinterpret_cast<void * const *>(dev->lpVtbl);
    const scarlett_guid_t iid_pso = {
        0x765A30F3u, 0xF624u, 0x4C6Fu,
        {0xA8u, 0x28u, 0xACu, 0xE9u, 0x48u, 0x62u, 0x24u, 0x45u}};
    const scarlett_guid_t iid_so = {
        0x47016943u, 0xFCA8u, 0x4594u,
        {0x93u, 0xEAu, 0xAFu, 0x25u, 0x8Bu, 0x55u, 0x34u, 0x6Du}};
    const scarlett_guid_t iid_rs = {
        0xC54A6B66u, 0x72DFu, 0x4EE8u,
        {0x8Bu, 0xE5u, 0xA9u, 0x46u, 0xA1u, 0x42u, 0x92u, 0x14u}};
    const scarlett_guid_t iid_dh = {
        0x8EFB471Du, 0x616Cu, 0x4F49u,
        {0x90u, 0xF7u, 0x12u, 0x7Bu, 0xB7u, 0x63u, 0xFAu, 0x51u}};
    const scarlett_guid_t iid_res = {
        0x696442BEu, 0xA72Eu, 0x4059u,
        {0xBCu, 0x79u, 0x5Bu, 0x5Cu, 0x98u, 0x04u, 0x0Fu, 0xADu}};

    /* 1. Dynamic PSO & StateObject capacity > 16,384 (20,000 each) */
    using PfnCreatePSO = scarlett_hresult_t (*)(
        ID3D12DeviceX *, const void *, const scarlett_guid_t *, void **);
    using PfnSlot55 = scarlett_hresult_t (*)(
        ID3D12DeviceX *, uint64_t, uint64_t, uint64_t, const scarlett_guid_t *, void **);
    using PfnRelease = uint32_t (*)(void *);

    constexpr size_t kScaleCount = 20000u;
    std::vector<void *> pso_objs(kScaleCount, nullptr);
    std::vector<void *> so_objs(kScaleCount, nullptr);
    for (size_t i = 0u; i < kScaleCount; ++i) {
        CHECK_HR(reinterpret_cast<PfnSlot55>(dev_vtbl[55])(
                     dev, 0u, 1u, 0u, &iid_pso, &pso_objs[i]),
                 SCARLETT_OK);
        CHECK(pso_objs[i] != nullptr);
        CHECK_HR(reinterpret_cast<PfnCreatePSO>(dev_vtbl[57])(
                     dev, nullptr, &iid_so, &so_objs[i]),
                 SCARLETT_OK);
        CHECK(so_objs[i] != nullptr);
    }
    CHECK(pso_objs[0] != pso_objs[19999]);
    CHECK(so_objs[0] != so_objs[19999]);
    for (size_t i = 0u; i < kScaleCount; ++i) {
        auto *pv = *reinterpret_cast<void * const * const *>(pso_objs[i]);
        auto *sv = *reinterpret_cast<void * const * const *>(so_objs[i]);
        CHECK(reinterpret_cast<PfnRelease>(pv[2])(pso_objs[i]) == 0u);
        CHECK(reinterpret_cast<PfnRelease>(sv[2])(so_objs[i]) == 0u);
    }

    /* 2. Shader extraction & lifting across slots 55, 57, 62, 67, 86 */
    using PfnCreateRS = scarlett_hresult_t (*)(
        ID3D12DeviceX *, uint32_t, const void *, uint64_t, const scarlett_guid_t *, void **);
    const std::vector<uint8_t> kDummyRts0(32u, 0u);
    auto rs_blob = build_synthetic_dxbc({{{'R','T','S','0'}, kDummyRts0}});
    void *rs_raw = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreateRS>(dev_vtbl[16])(
                 dev, 0u, rs_blob.data(), rs_blob.size(), &iid_rs, &rs_raw),
             SCARLETT_OK);
    CHECK(rs_raw != nullptr && mock_dev.create_rs_calls == 1u);

    const std::vector<uint8_t> kXboxChunk = {'X','B','O','X',1,2,3,4};
    const std::vector<uint8_t> kDxilChunk = {'B','C',0xC0u,0xDEu,1,0,0,0};
    auto xbox_dxbc = build_synthetic_dxbc({
        {{'X','B','O','X'}, kXboxChunk},
        {{'D','X','I','L'}, kDxilChunk},
    });

    std::vector<uint8_t> xs2_blob(52u, 0u);
    write_le32(xs2_blob, 0u, 0x00325358u);      /* 'XS2\0' */
    write_le32(xs2_blob, 4u, (2u << 16u) | 1u); /* version=1, stage=2 (CS) */
    write_le32(xs2_blob, 8u, (4u << 16u) | 64u);
    write_le32(xs2_blob, 12u, 4u);
    write_le32(xs2_blob, 20u, 1u);
    write_le32(xs2_blob, 24u, 1u);
    write_le32(xs2_blob, 28u, 1u);
    write_le32(xs2_blob, 40u, 48u);
    write_le32(xs2_blob, 44u, 4u);
    write_le32(xs2_blob, 48u, 0xBF810000u);

    /* 2a. Slot 86: D3D12_PIPELINE_STATE_STREAM_DESC with RootSig + DXBC -> real_device->vtbl[47] */
    struct alignas(8) StreamBuf {
        uint64_t type0;
        void *rs;
        uint64_t type1;
        const void *shader_ptr;
        uint64_t shader_len;
    } stream_buf{0u, rs_raw, 1u, xbox_dxbc.data(), xbox_dxbc.size()};
    struct StreamDesc {
        uint64_t size;
        void *ptr;
    } stream_desc{sizeof(stream_buf), &stream_buf};
    void *pso86 = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreatePSO>(dev_vtbl[86])(
                 dev, &stream_desc, &iid_pso, &pso86),
             SCARLETT_OK);
    void *pso86_real = nullptr;
    uint32_t pso86_spv = 0u;
    CHECK(scarlett_d3d12xs_test_get_pso_info(pso86, &pso86_real, &pso86_spv, nullptr));
    CHECK(pso86_real != nullptr);
    CHECK(mock_dev.create_stream_pso_calls == 1u);
    CHECK(mock_dev.last_pso_had_real_rs && mock_dev.last_pso_had_clean_dxbc);

    /* 2b. Slot 62: Compute PSO with RootSig + DXBC -> real_device->vtbl[11] */
    using PfnSlot62 = scarlett_hresult_t (*)(
        ID3D12DeviceX *, const void *, uint64_t, uint64_t, const scarlett_guid_t *, void **);
    struct ComputeXboxDesc {
        void *rs;
        const void *cs;
        uint64_t cs_len;
    } comp_desc{rs_raw, xbox_dxbc.data(), xbox_dxbc.size()};
    void *pso62 = nullptr;
    CHECK_HR(reinterpret_cast<PfnSlot62>(dev_vtbl[62])(
                 dev, &comp_desc, 0u, 0u, &iid_pso, &pso62),
             SCARLETT_OK);
    void *pso62_real = nullptr;
    uint32_t pso62_is_comp = 0u;
    CHECK(scarlett_d3d12xs_test_get_pso_info(pso62, &pso62_real, nullptr, &pso62_is_comp));
    CHECK(pso62_real != nullptr && pso62_is_comp == 1u);
    CHECK(mock_dev.create_compute_pso_calls == 1u);

    /* 2c. Slot 55: Xbox graphics struct with root sigs at q0/q1 and XS2 shader at +0x100 (>0x80!) */
    uint64_t slot55_desc[64]{};
    slot55_desc[0] = reinterpret_cast<uint64_t>(rs_raw);
    slot55_desc[1] = reinterpret_cast<uint64_t>(rs_raw);
    slot55_desc[2] = reinterpret_cast<uint64_t>(slot55_desc);
    slot55_desc[32] = reinterpret_cast<uint64_t>(xs2_blob.data());
    slot55_desc[33] = xs2_blob.size();
    void *pso55 = nullptr;
    CHECK_HR(reinterpret_cast<PfnSlot55>(dev_vtbl[55])(
                 dev, reinterpret_cast<uint64_t>(slot55_desc), 1u, 0u, &iid_pso, &pso55),
             SCARLETT_OK);
    uint32_t pso55_spv = 0u;
    CHECK(scarlett_d3d12xs_test_get_pso_info(pso55, nullptr, &pso55_spv, nullptr));
    CHECK(pso55_spv >= 20u);

    /* 2d. Slot 67: Indirect shader pointer at q0 (native compute desc) -> lifts XS2 to SPIR-V */
    uint64_t slot67_inner[4]{
        reinterpret_cast<uint64_t>(xs2_blob.data()), xs2_blob.size(), 0u, 0u};
    uint64_t slot67_desc[4]{reinterpret_cast<uint64_t>(slot67_inner), 0u, 0u, 0u};
    void *pso67 = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreatePSO>(dev_vtbl[67])(
                 dev, slot67_desc, &iid_pso, &pso67),
             SCARLETT_OK);
    uint32_t pso67_spv = 0u;
    CHECK(scarlett_d3d12xs_test_get_pso_info(pso67, nullptr, &pso67_spv, nullptr));
    CHECK(pso67_spv >= 20u);

    /* 2e. Slot 57 with IID_ID3D12PipelineState -> lifts XS2 inside stream/struct */
    uint64_t slot57_desc[4]{reinterpret_cast<uint64_t>(xs2_blob.data()), xs2_blob.size(), 0u, 0u};
    void *pso57 = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreatePSO>(dev_vtbl[57])(
                 dev, slot57_desc, &iid_pso, &pso57),
             SCARLETT_OK);
    uint32_t pso57_spv = 0u;
    CHECK(scarlett_d3d12xs_test_get_pso_info(pso57, nullptr, &pso57_spv, nullptr));
    CHECK(pso57_spv >= 20u);

    /* 3. CreatePlacedResourceX sanitization for Xbox Depth/UAV/RT/MSAA flags */
    using PfnCreatePlacedX = scarlett_hresult_t (*)(
        ID3D12DeviceX *, uint64_t, const mock_vkd3d_m3::MockResDesc *,
        uint32_t, const void *, const scarlett_guid_t *, void **);
    using PfnCreateView = void (*)(ID3D12DeviceX *, void *, const void *, uint64_t);
    using PfnCreateDH = scarlett_hresult_t (*)(
        ID3D12DeviceX *, const mock_vkd3d_dh::MockDescHeapDesc *, const scarlett_guid_t *, void **);

    mock_vkd3d_dh::MockDescHeapDesc cbv_dh_desc{0u, 64u, 1u, 0u};
    mock_vkd3d_dh::MockDescHeapDesc dsv_dh_desc{3u, 64u, 0u, 0u};
    mock_vkd3d_dh::MockDescHeapDesc samp_dh_desc{1u, 64u, 0u, 0u};
    void *cbv_dh = nullptr, *dsv_dh = nullptr, *samp_dh = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreateDH>(dev_vtbl[14])(dev, &cbv_dh_desc, &iid_dh, &cbv_dh), SCARLETT_OK);
    CHECK_HR(reinterpret_cast<PfnCreateDH>(dev_vtbl[14])(dev, &dsv_dh_desc, &iid_dh, &dsv_dh), SCARLETT_OK);
    CHECK_HR(reinterpret_cast<PfnCreateDH>(dev_vtbl[14])(dev, &samp_dh_desc, &iid_dh, &samp_dh), SCARLETT_OK);

    using PfnGetStart = uint64_t (*)(void *);
    uint64_t cbv_cpu = reinterpret_cast<PfnGetStart>((*reinterpret_cast<void * const * const *>(cbv_dh))[9])(cbv_dh);
    uint64_t dsv_cpu = reinterpret_cast<PfnGetStart>((*reinterpret_cast<void * const * const *>(dsv_dh))[9])(dsv_dh);
    uint64_t samp_cpu = reinterpret_cast<PfnGetStart>((*reinterpret_cast<void * const * const *>(samp_dh))[9])(samp_dh);

    /* Depth texture with conflicting ALLOW_RT | ALLOW_DS | ALLOW_UAV | SIMULTANEOUS_ACCESS (0x27) */
    mock_vkd3d_m3::MockResDesc depth_desc{
        3u, 0u, 1280u, 720u, 1u, 1u, 39u /* R32_TYPELESS */, 1u, 0u, 0u, 0x27u};
    void *depth_res = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreatePlacedX>(dev_vtbl[133])(
                 dev, 0x20000000u, &depth_desc, 0u, nullptr, &iid_res, &depth_res),
             SCARLETT_OK);
    CHECK(depth_res != nullptr);
    CHECK(mock_dev.create_res_rejected == 0u);
    CHECK(mock_dev.last_accepted_flags == 0x02u);

    reinterpret_cast<PfnCreateView>(dev_vtbl[21])(dev, depth_res, nullptr, dsv_cpu);
    reinterpret_cast<PfnCreateView>(dev_vtbl[18])(dev, depth_res, nullptr, cbv_cpu);
    CHECK(mock_dev.create_dsv_calls == 1u && mock_dev.create_srv_calls == 1u);

    /* MSAA + UAV + MipLevels=4 -> sanitized to SampleCount=4, MipLevels=1, Flags without UAV */
    mock_vkd3d_m3::MockResDesc msaa_desc{
        3u, 0u, 640u, 480u, 1u, 4u, 28u /* R8G8B8A8_UNORM */, 4u, 0u, 0u, 0x05u};
    void *msaa_res = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreatePlacedX>(dev_vtbl[133])(
                 dev, 0x21000000u, &msaa_desc, 0u, nullptr, &iid_res, &msaa_res),
             SCARLETT_OK);
    CHECK(msaa_res != nullptr);
    CHECK(mock_dev.last_accepted_samples == 4u && mock_dev.last_accepted_flags == 0x01u);

    /* Depth flag on non-depth format -> pass 0 fails, pass 1 retry strips DS flag and succeeds */
    mock_vkd3d_m3::MockResDesc bad_ds_desc{
        3u, 0u, 256u, 256u, 1u, 1u, 28u /* R8G8B8A8_UNORM */, 1u, 0u, 0u, 0x0Au};
    void *bad_ds_res = nullptr;
    CHECK_HR(reinterpret_cast<PfnCreatePlacedX>(dev_vtbl[133])(
                 dev, 0x22000000u, &bad_ds_desc, 0u, nullptr, &iid_res, &bad_ds_res),
             SCARLETT_OK);
    CHECK(bad_ds_res != nullptr && mock_dev.create_res_rejected == 1u);

    /* 4. ID3D12DeviceX::Slot61 (CreateSampler): must NOT overwrite pSamplerDesc and must forward */
    using PfnSlot61 = scarlett_hresult_t (*)(ID3D12DeviceX *, const void *, uint64_t);
    uint32_t samp_desc[13] = {
        0x15u, 99u /* invalid AddressU -> clamp 3 */, 1u, 1u,
        0u, 64u /* MaxAnisotropy > 16 -> clamp 16 */, 4u,
        0u, 0u, 0u, 0u, 0u, 0x447A0000u};
    CHECK_HR(reinterpret_cast<PfnSlot61>(dev_vtbl[61])(
                 dev, samp_desc, samp_cpu + 2u * 32u),
             SCARLETT_OK);
    CHECK(samp_desc[1] == 99u && samp_desc[5] == 64u); /* Input stack desc preserved */
    CHECK(mock_dev.create_sampler_calls == 1u);
    CHECK(mock_dev.last_sampler_dest == mock_dev.dh_pool[2].cpu_va + 2u * 64u);
    CHECK(mock_dev.last_sampler_words[1] == 3u && mock_dev.last_sampler_words[5] == 16u);

    auto rel = [](void *p) {
        auto *v = *reinterpret_cast<void * const * const *>(p);
        return reinterpret_cast<PfnRelease>(v[2])(p);
    };
    CHECK(rel(bad_ds_res) == 0u);
    CHECK(rel(msaa_res) == 0u);
    CHECK(rel(depth_res) == 0u);
    CHECK(rel(samp_dh) == 0u);
    CHECK(rel(dsv_dh) == 0u);
    CHECK(rel(cbv_dh) == 0u);
    CHECK(rel(pso57) == 0u);
    CHECK(rel(pso67) == 0u);
    CHECK(rel(pso55) == 0u);
    CHECK(rel(pso62) == 0u);
    CHECK(rel(pso86) == 0u);
    CHECK(rel(rs_raw) == 0u);

    dev->real_device = saved_real;
    CHECK(dev->lpVtbl->Release(dev) == 0u);
    return true;
}

} // namespace

int main() {
    struct TestCase {
        const char *name;
        bool (*fn)();
    };

    const TestCase tests[] = {
        {"1. Unixlib Handshake & Real Vulkan 1.4 Probe (GXDK >= 0x585D070E)",
         test_1_unix_handshake_and_vulkan_probe},
        {"2. Descriptor Heap Policy Matrix (RADV / NVIDIA 610.xx / 595.xx 1M / ANV + T#/V#/S#)",
         test_2_descriptor_heap_policy_matrix_and_tvs},
        {"3. CreatePlacedResourceX Over Real xmem UMA (0-Frame Lag Pre-Submit Flush)",
         test_3_uma_placed_resource_zero_frame_lag},
        {"4. Separated Queues (0x0/0x2/0x10), Enhanced Barriers & ExecuteIndirect",
         test_4_queues_enhanced_barriers_and_indirect},
        {"5. DXIL Fast-Path (Path A) Strip XBOX/XS2M Chunks",
         test_5_dxil_strip_xbox_xs2m},
        {"6. 60 FPS Frame Pacing (35 Frames, p99 < 1.5ms) & Dual-Plane HDR10 PresentX",
         test_6_frame_pacing_and_dual_plane_hdr10_presentx},
        {"7. 10 Cycles Alt+Enter/Resize + SuspendX/ResumeX (Zero Losses/Races)",
         test_7_resize_alt_enter_and_suspend_resume_cycles},
        {"8. VK_EXT_device_fault + DRED Dump to PROTON_LOG",
         test_8_dred_and_device_fault_proton_log},
        {"9. Zero Silent Stubs Diagnostic Auto-Stub Trampoline (Normal & Strict)",
         test_9_diagnostic_auto_stub_trampoline},
        {"10. Command List Side-Table Isolation & Shader Lifting Bridge",
         test_10_cmdlist_sidetable_and_shader_lift},
        {"11. Descriptor Heap Hidden Out-Ptr ABI & Clamped Bounds Validation",
         test_11_descriptor_heap_abi_and_bounds},
        {"12. Dynamic PSO/StateObject Scaling, Shader Lifting, Resource Flags & Slot61",
         test_12_pso_capacity_shader_lift_res_flags_and_slot61},
    };

    int passed = 0;
    const int total = static_cast<int>(sizeof(tests) / sizeof(tests[0]));
    for (const auto &t : tests) {
        std::cout << "[RUN ] " << t.name << "\n";
        if (t.fn()) {
            std::cout << "[PASS] " << t.name << "\n";
            ++passed;
        } else {
            std::cerr << "[FAIL] " << t.name << "\n";
        }
    }

    std::cout << "\nSummary: " << passed << "/" << total << " test groups passed.\n";
    return (passed == total && g_failures == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
