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
#include "d3d12_xs_vk_abi.h"
#include "xmem/xmem_api.h"

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
    void *dev_raw = nullptr;
    CHECK_HR(D3D12XboxCreateDevice(&params, &iid_dev, &dev_raw), SCARLETT_OK);
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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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
    CHECK_HR(XMemVirtualAlloc(0u, kSize, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                              XMEM_PAGE_READWRITE, &gpu_va),
             SCARLETT_OK);
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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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
        D3D12XBOX_PRESENT_PLANE_PARAMETERS planes[2]{};
        planes[0].ColorSpace = SCARLETT_COLOR_SPACE_G2084_P2020;
        planes[0].SourceRectRight = 3840u;
        planes[0].SourceRectBottom = 2160u;
        planes[1].ColorSpace = SCARLETT_COLOR_SPACE_G22_P709;
        planes[1].SourceRectRight = 1920u;
        planes[1].SourceRectBottom = 1080u;

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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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

        D3D12XBOX_PRESENT_PLANE_PARAMETERS plane{};
        plane.pResource = reinterpret_cast<void *>(static_cast<uintptr_t>(res_handles[0]));
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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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
    CHECK_HR(D3D12XboxCreateDevice(nullptr, nullptr, &dev_raw), SCARLETT_OK);
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
