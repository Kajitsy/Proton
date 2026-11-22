#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "pixevt_backend.hpp"
#include "xbox_bvh_builder.hpp"
#include "xg_sfs_coalescer.hpp"
#include "xg_xs_tiling.hpp"
#include "xg_xs_unix_abi.h"

extern "C" {
/* PE64 C shim exports linked via scarlett_xg_xs_pe_host */
scarlett_hresult_t XGCreateTexture2DComputer(const XG_TEXTURE2D_DESC* pDesc, void** ppComputer);
scarlett_hresult_t XGCreateTexture1DComputer(const void* pDesc, void** ppComputer);
scarlett_hresult_t XGCreateTexture3DComputer(const void* pDesc, void** ppComputer);
uint32_t XGComputeOptimalSwizzleMode(
    uint32_t resourceType,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t depthOrArray,
    uint32_t sampleCount,
    uint32_t bindFlags);
uint32_t XGComputeOptimalTileMode(uint32_t resourceType, uint32_t format, uint32_t width, uint32_t height);
scarlett_hresult_t XGSelectTransferPath(
    const XG_GPU_CAPS_PROBE* pCaps,
    uint32_t swizzleMode,
    uint32_t format,
    XG_TRANSFER_PATH_DECISION* pOutDecision);
scarlett_hresult_t XGSparseCoalesceBinds(
    const ScarlettSparseTileBindRequest* requests,
    uint32_t requestCount,
    uint32_t aspectMask,
    ScarlettVkSparseImageMemoryBind* outImageBinds,
    uint32_t imageBindsCapacity,
    uint32_t* outImageBindCount,
    ScarlettVkSparseMemoryBind* outMemoryBinds,
    uint32_t memoryBindsCapacity,
    uint32_t* outMemoryBindCount,
    ScarlettSparseCoalesceStats* outStats);

scarlett_hresult_t XboxBvhGetPrebuildInfo(
    const XBVH_BUILD_INPUTS* pInputs,
    XBVH_PREBUILD_INFO* pOutInfo);
scarlett_hresult_t XboxBvhBuildAccelerationStructure(
    const XBVH_BUILD_INPUTS* pInputs,
    void* pDstBvh,
    uint64_t dstCapacityBytes,
    const void* pSrcBvh,
    void* pScratch,
    uint64_t scratchCapacityBytes,
    uint64_t* pOutWrittenBytes);
scarlett_hresult_t XboxBvhIntersectRay(
    const void* pBvhData,
    const XBVH_RAY_DESC* pRay,
    XBVH_RAY_HIT* pOutHit);
scarlett_hresult_t XboxBvhMapToVulkanBuildInfo(
    const XBVH_BUILD_INPUTS* pInputs,
    uint32_t hasVkKhrAccelStruct,
    uint64_t dstAsHandle,
    uint64_t srcAsHandle,
    uint64_t scratchBda,
    ScarlettVkAccelTrianglesDataKHR* pOutGeometries,
    uint32_t geometriesCapacity,
    ScarlettVkAccelBuildRangeInfoKHR* pOutRanges,
    ScarlettVkAccelBuildGeometryInfoKHR* pOutBuildInfo);
scarlett_hresult_t XboxBvhBuildProceduralGeometry(const void* pDesc, void* pDstBvh);

void PIXBeginEvent(uint64_t color, const char* formatString, ...);
void PIXEndEvent(void);
void PIXSetMarker(uint64_t color, const char* formatString, ...);
void PIXBeginEventOnCommandList(void* commandList, uint64_t color, const char* formatString, ...);
void PIXEndEventOnCommandList(void* commandList);
void PIXSetMarkerOnCommandList(void* commandList, uint64_t color, const char* formatString, ...);
uint32_t PIXGetEventStackDepth(void* commandList, uint32_t* outUnderflowCount);
void PIXSetEventsEnabled(uint32_t enabled);
scarlett_hresult_t PIXGpuCaptureNextFrames(const void* fileName, uint32_t numFrames);

void scarlett_xg_xs_set_stub_sink(scarlett_stub_sink_fn_t sink, void* user_data);
void scarlett_xg_xs_set_strict_mode(uint32_t strict_mode);
}

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

void test_unix_handshake() {
    scarlett_unix_init_params_t bad{};
    bad.abi_version = 999u;
    bad.module_id = SCARLETT_XG_XS_UNIX_MODULE_ID;
    check(scarlett_xg_xs_unix_call(SCARLETT_XG_XS_UNIX_INIT, &bad) ==
              SCARLETT_ERR_ABI_VERSION_MISMATCH,
          "unixlib INIT must reject mismatched ABI version");

    bad.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    bad.module_id = 99u;
    check(scarlett_xg_xs_unix_call(SCARLETT_XG_XS_UNIX_INIT, &bad) ==
              SCARLETT_ERR_ABI_VERSION_MISMATCH,
          "unixlib INIT must reject mismatched module_id");

    scarlett_unix_init_params_t ok{};
    ok.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    ok.module_id = SCARLETT_XG_XS_UNIX_MODULE_ID;
    ok.console_model = SCARLETT_MODEL_ANACONDA;
    check(scarlett_xg_xs_unix_call(SCARLETT_XG_XS_UNIX_INIT, &ok) == SCARLETT_OK,
          "unixlib INIT must succeed with valid handshake");
    check(scarlett_xg_xs_unix_call(SCARLETT_XG_XS_UNIX_OP_COUNT, &ok) ==
              SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE,
          "unixlib must reject out-of-range opcode");
}

void test_swizzle_roundtrip_matrix() {
    struct FormatCase {
        uint32_t fmt;
        const char* name;
        uint32_t w;
        uint32_t h;
        uint32_t samples;
    };
    const std::vector<FormatCase> format_cases = {
        {XG_FORMAT_R8_UNORM, "R8_8bpp", 96u, 72u, 1u},
        {XG_FORMAT_R16_FLOAT, "R16_16bpp", 80u, 64u, 1u},
        {XG_FORMAT_R8G8B8A8_UNORM, "RGBA8_32bpp", 129u, 67u, 1u},
        {XG_FORMAT_R16G16B16A16_FLOAT, "RGBA16F_64bpp", 65u, 49u, 1u},
        {XG_FORMAT_R32G32B32A32_FLOAT, "RGBA32F_128bpp", 41u, 37u, 1u},
        {XG_FORMAT_BC1_UNORM, "BC1_4x4_8B", 128u, 96u, 1u},
        {XG_FORMAT_BC2_UNORM, "BC2_4x4_16B", 64u, 64u, 1u},
        {XG_FORMAT_BC3_UNORM, "BC3_4x4_16B", 64u, 48u, 1u},
        {XG_FORMAT_BC4_UNORM, "BC4_4x4_8B", 80u, 64u, 1u},
        {XG_FORMAT_BC5_UNORM, "BC5_4x4_16B", 80u, 64u, 1u},
        {XG_FORMAT_BC6H_UF16, "BC6H_4x4_16B", 64u, 64u, 1u},
        {XG_FORMAT_BC7_UNORM, "BC7_4x4_16B", 72u, 52u, 1u},
        {XG_FORMAT_R8G8B8A8_UNORM, "MSAA2x_32bpp", 64u, 48u, 2u},
        {XG_FORMAT_R16G16B16A16_FLOAT, "MSAA4x_64bpp", 48u, 36u, 4u},
        {XG_FORMAT_R32G32B32A32_FLOAT, "MSAA8x_128bpp", 32u, 24u, 8u},
    };

    const std::vector<uint32_t> modes = {
        XG_SWIZZLE_LINEAR,
        XG_SWIZZLE_4KB_D,
        XG_SWIZZLE_4KB_S,
        XG_SWIZZLE_64KB_S,
        XG_SWIZZLE_64KB_D,
        XG_SWIZZLE_64KB_R_X,
        XG_SWIZZLE_64KB_D_X,
        XG_SWIZZLE_64KB_S_X,
    };

    for (const auto& fc : format_cases) {
        for (uint32_t mode : modes) {
            XG_TEXTURE2D_DESC desc{};
            desc.Width = fc.w;
            desc.Height = fc.h;
            desc.MipLevels = (fc.samples > 1u) ? 1u : 3u;
            desc.ArraySize = 2u;
            desc.Format = fc.fmt;
            desc.SampleCount = fc.samples;
            desc.SwizzleMode = mode;
            desc.PitchAlignment = 256u;

            void* raw_comp = nullptr;
            check(XGCreateTexture2DComputer(&desc, &raw_comp) == SCARLETT_OK && raw_comp != nullptr,
                  std::string("XGCreateTexture2DComputer failed for ") + fc.name);
            auto* comp = static_cast<IXGTextureAddressComputer*>(raw_comp);

            // Check QueryInterface & AddRef
            void* qi_ptr = nullptr;
            const scarlett_guid_t iid = SCARLETT_IID_IXG_TEXTURE_ADDRESS_COMPUTER_INIT;
            check(comp->lpVtbl->QueryInterface(comp, &iid, &qi_ptr) == SCARLETT_OK && qi_ptr == comp,
                  "QueryInterface for IXGTextureAddressComputer must succeed");
            check(comp->lpVtbl->Release(comp) == 1u, "Release after QI must drop refcount to 1");

            XG_RESOURCE_LAYOUT rlayout{};
            check(comp->lpVtbl->GetResourceLayout(comp, &rlayout) == SCARLETT_OK,
                  "GetResourceLayout failed");
            check(rlayout.SizeBytes > 0ULL, "Resource SizeBytes must be > 0");

            std::vector<uint8_t> swizzled(static_cast<size_t>(rlayout.SizeBytes), 0xCDu);

            // Check bijectivity on subresource 0 and round-trip across every mip & slice
            const uint32_t total_subs = desc.MipLevels * desc.ArraySize;
            for (uint32_t sub = 0u; sub < total_subs; ++sub) {
                XG_SUBRESOURCE_LAYOUT sl{};
                check(comp->lpVtbl->GetSubresourceLayout(comp, sub, &sl) == SCARLETT_OK,
                      "GetSubresourceLayout failed");

                const auto fm = scarlett::xg_xs::query_format_metrics(fc.fmt);
                const uint64_t sample_plane = sl.RowPitchBytes * sl.HeightElements;
                const size_t linear_bytes = static_cast<size_t>(sample_plane * fc.samples);

                std::vector<uint8_t> src_linear(linear_bytes, 0u);
                std::vector<uint8_t> dst_linear(linear_bytes, 0u);

                // Populate deterministic pattern
                for (uint32_t s = 0u; s < fc.samples; ++s) {
                    for (uint32_t y = 0u; y < sl.HeightElements; ++y) {
                        for (uint32_t x = 0u; x < sl.WidthElements; ++x) {
                            const size_t off = static_cast<size_t>(
                                s * sample_plane + y * sl.RowPitchBytes + x * fm.bytes_per_element);
                            for (uint32_t b = 0u; b < fm.bytes_per_element; ++b) {
                                src_linear[off + b] = static_cast<uint8_t>(
                                    ((x * 13u + y * 31u + s * 53u + sub * 7u + b * 97u) & 0xFFu));
                            }
                        }
                    }
                }

                if (sub == 0u) {
                    std::unordered_set<uint64_t> seen_offsets;
                    seen_offsets.reserve(static_cast<size_t>(sl.WidthElements) * sl.HeightElements * fc.samples);
                    bool collision = false;
                    for (uint32_t s = 0u; s < fc.samples && !collision; ++s) {
                        for (uint32_t y = 0u; y < sl.HeightElements && !collision; ++y) {
                            for (uint32_t x = 0u; x < sl.WidthElements; ++x) {
                                const uint64_t off = comp->lpVtbl->GetTexelElementOffsetBytes(
                                    comp, 0u, 0u, x, y, 0u, s);
                                if (off >= rlayout.SizeBytes || !seen_offsets.insert(off).second) {
                                    collision = true;
                                    break;
                                }
                            }
                        }
                    }
                    check(!collision, std::string("Offset collision detected for ") + fc.name);
                }

                check(comp->lpVtbl->CopyIntoSubresource(
                          comp, swizzled.data(), static_cast<uint32_t>(sl.RowPitchBytes),
                          sub, src_linear.data()) == SCARLETT_OK,
                      "CopyIntoSubresource failed");
                check(comp->lpVtbl->CopyFromSubresource(
                          comp, dst_linear.data(), static_cast<uint32_t>(sl.RowPitchBytes),
                          sub, swizzled.data()) == SCARLETT_OK,
                      "CopyFromSubresource failed");

                bool match = true;
                for (uint32_t s = 0u; s < fc.samples && match; ++s) {
                    for (uint32_t y = 0u; y < sl.HeightElements && match; ++y) {
                        const size_t row_off = static_cast<size_t>(s * sample_plane + y * sl.RowPitchBytes);
                        const size_t row_data_bytes = static_cast<size_t>(sl.WidthElements) * fm.bytes_per_element;
                        if (std::memcmp(src_linear.data() + row_off,
                                        dst_linear.data() + row_off,
                                        row_data_bytes) != 0) {
                            match = false;
                        }
                    }
                }
                check(match, std::string("Round-trip mismatch for ") + fc.name +
                                 " mode=" + std::to_string(mode) + " sub=" + std::to_string(sub));
            }

            check(comp->lpVtbl->Release(comp) == 0u, "Final Release must return 0");
        }
    }

    // Optimal swizzle mode sanity check
    check(XGComputeOptimalSwizzleMode(2u, XG_FORMAT_R8G8B8A8_UNORM, 2048u, 2048u, 1u, 4u, 0x20u) ==
              XG_SWIZZLE_64KB_S,
          "MSAA texture optimal swizzle must select 64KB_S");
    check(XGComputeOptimalTileMode(2u, XG_FORMAT_BC7_UNORM, 1024u, 1024u) == XG_SWIZZLE_64KB_D,
          "1024x1024 BC7 optimal tile mode must select 64KB_D");
}

void test_40bit_above_4gib_addressing() {
    // 16384 x 16384 x 16 bytes (RGBA32F) = 4 GiB per slice; ArraySize=2 -> 8 GiB total!
    XG_TEXTURE2D_DESC desc{};
    desc.Width = 16384u;
    desc.Height = 16384u;
    desc.MipLevels = 1u;
    desc.ArraySize = 2u;
    desc.Format = XG_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleCount = 1u;
    desc.SwizzleMode = XG_SWIZZLE_64KB_D;
    desc.PitchAlignment = 256u;

    void* raw_comp = nullptr;
    check(XGCreateTexture2DComputer(&desc, &raw_comp) == SCARLETT_OK && raw_comp != nullptr,
          "Creating >4 GiB 40-bit texture computer must succeed");
    auto* comp = static_cast<IXGTextureAddressComputer*>(raw_comp);

    XG_RESOURCE_LAYOUT layout{};
    check(comp->lpVtbl->GetResourceLayout(comp, &layout) == SCARLETT_OK,
          "GetResourceLayout on >4 GiB texture failed");
    const uint64_t four_gib = UINT64_C(4) * UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024);
    const uint64_t eight_gib = UINT64_C(2) * four_gib;
    check(layout.SizeBytes >= eight_gib && layout.SizeBytes < SCARLETT_XG_40BIT_LIMIT_BYTES,
          "8 GiB surface must report exact 64-bit SizeBytes > 4 GiB without 32-bit truncation");

    const uint64_t off_slice0 = comp->lpVtbl->GetTexelElementOffsetBytes(comp, 0u, 0u, 16383u, 16383u, 0u, 0u);
    const uint64_t off_slice1 = comp->lpVtbl->GetTexelElementOffsetBytes(comp, 0u, 0u, 16383u, 16383u, 1u, 0u);
    check(off_slice0 < four_gib, "Slice 0 max offset must be < 4 GiB");
    check(off_slice1 == off_slice0 + four_gib,
          "Slice 1 offset must equal slice 0 offset + 4 GiB (exact 40-bit arithmetic)");
    check(off_slice1 > four_gib && (off_slice1 >> 32u) == 1ULL,
          "High 32 bits of 40-bit offset must be preserved");
    comp->lpVtbl->Release(comp);

    // Single-slice > 4 GiB: 32768 x 16384 x 16 bytes = 8 GiB in one slice
    XG_TEXTURE2D_DESC huge_single = desc;
    huge_single.Width = 32768u;
    huge_single.Height = 16384u;
    huge_single.ArraySize = 1u;
    void* raw_single = nullptr;
    check(XGCreateTexture2DComputer(&huge_single, &raw_single) == SCARLETT_OK && raw_single != nullptr,
          "Creating 8 GiB single-slice surface must succeed");
    auto* single_comp = static_cast<IXGTextureAddressComputer*>(raw_single);
    const uint64_t off_high = single_comp->lpVtbl->GetTexelElementOffsetBytes(
        single_comp, 0u, 0u, 32767u, 16383u, 0u, 0u);
    check(off_high > four_gib && off_high < eight_gib,
          "Single-slice 8 GiB surface high coordinate offset must be > 4 GiB");
    single_comp->lpVtbl->Release(single_comp);

    // Overflow beyond 40-bit (> 1 TiB) must fail with SCARLETT_ERR_XG_SURFACE_OVERFLOW
    XG_TEXTURE2D_DESC overflow_desc = desc;
    overflow_desc.Width = 65536u;
    overflow_desc.Height = 65536u;
    overflow_desc.ArraySize = 32u;
    void* raw_ovf = nullptr;
    check(XGCreateTexture2DComputer(&overflow_desc, &raw_ovf) == SCARLETT_ERR_XG_SURFACE_OVERFLOW,
          "Surface exceeding 40-bit 1 TiB limit must return SCARLETT_ERR_XG_SURFACE_OVERFLOW");
}

void test_spirv_deswizzle_and_transfer_paths() {
    std::vector<uint32_t> spirv;
    std::string diag;
    const bool spv_ok = scarlett::xg_xs::compile_and_validate_deswizzle_spirv(spirv, &diag);
    check(spv_ok, "Deswizzle GLSL -> SPIR-V 1.6 -> spirv-val --target-env vulkan1.4 failed: " + diag);
    check(!spirv.empty() && spirv[0] == 0x07230203U, "Compiled SPIR-V magic word invalid");

    // AMD RADV zero-copy fast path detection
    XG_GPU_CAPS_PROBE radv_caps{};
    radv_caps.vendor_id = XG_GPU_VENDOR_AMD;
    radv_caps.driver_id = XG_VK_DRIVER_MESA_RADV;
    radv_caps.has_drm_format_modifier = 1u;
    radv_caps.has_vulkan_compute = 1u;
    radv_caps.drm_modifier_mask = 0x7ULL;

    XG_TRANSFER_PATH_DECISION dec{};
    check(XGSelectTransferPath(&radv_caps, XG_SWIZZLE_64KB_D, XG_FORMAT_R8G8B8A8_UNORM, &dec) ==
              SCARLETT_OK,
          "XGSelectTransferPath for RADV failed");
    check(dec.path == XG_TRANSFER_PATH_RADV_ZERO_COPY && dec.zero_copy_active == 1u &&
              dec.drm_modifier == SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_D,
          "AMD RADV with 64KB_D must select zero-copy DRM_FORMAT_MOD_AMD_GFX10_64KB_D fast-path");

    // NVIDIA & Intel compute deswizzle path detection
    XG_GPU_CAPS_PROBE nv_caps{};
    nv_caps.vendor_id = XG_GPU_VENDOR_NVIDIA;
    nv_caps.driver_id = XG_VK_DRIVER_NVIDIA_PROPRIETARY;
    nv_caps.has_drm_format_modifier = 1u;
    nv_caps.has_vulkan_compute = 1u;
    check(XGSelectTransferPath(&nv_caps, XG_SWIZZLE_64KB_D, XG_FORMAT_R8G8B8A8_UNORM, &dec) ==
              SCARLETT_OK,
          "XGSelectTransferPath for NVIDIA failed");
    check(dec.path == XG_TRANSFER_PATH_VULKAN_COMPUTE_DESWIZZLE && dec.zero_copy_active == 0u,
          "NVIDIA must select Vulkan compute deswizzle path");

    XG_GPU_CAPS_PROBE intel_caps{};
    intel_caps.vendor_id = XG_GPU_VENDOR_INTEL;
    intel_caps.driver_id = XG_VK_DRIVER_INTEL_OPEN_MESA;
    intel_caps.has_drm_format_modifier = 1u;
    intel_caps.has_vulkan_compute = 1u;
    check(XGSelectTransferPath(&intel_caps, XG_SWIZZLE_64KB_D, XG_FORMAT_R8G8B8A8_UNORM, &dec) ==
              SCARLETT_OK,
          "XGSelectTransferPath for Intel ANV failed");
    check(dec.path == XG_TRANSFER_PATH_VULKAN_COMPUTE_DESWIZZLE && dec.zero_copy_active == 0u,
          "Intel ANV must select Vulkan compute deswizzle path");
}

void test_xbox_bvh4_and_refit() {
    // Build a 32x16 grid mesh = 512 quads = 1024 triangles (>= 1000 triangles)
    constexpr uint32_t kCols = 32u;
    constexpr uint32_t kRows = 16u;
    struct Vec3 { float x, y, z; };
    std::vector<Vec3> verts((kCols + 1u) * (kRows + 1u));
    for (uint32_t r = 0u; r <= kRows; ++r) {
        for (uint32_t c = 0u; c <= kCols; ++c) {
            const float x = static_cast<float>(c) - 16.0f;
            const float y = static_cast<float>(r) - 8.0f;
            const float z = 10.0f + 0.1f * std::sin(x * 0.5f) * std::cos(y * 0.5f);
            verts[r * (kCols + 1u) + c] = {x, y, z};
        }
    }
    std::vector<uint32_t> indices;
    indices.reserve(kCols * kRows * 6u);
    for (uint32_t r = 0u; r < kRows; ++r) {
        for (uint32_t c = 0u; c < kCols; ++c) {
            const uint32_t i0 = r * (kCols + 1u) + c;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + (kCols + 1u);
            const uint32_t i3 = i2 + 1u;
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }
    }
    const uint32_t tri_count = static_cast<uint32_t>(indices.size() / 3u);
    check(tri_count == 1024u, "Mesh must contain 1024 triangles (>= 1000)");

    XBVH_TRIANGLES_DESC tri_desc{};
    tri_desc.vertex_buffer_addr = reinterpret_cast<uint64_t>(verts.data());
    tri_desc.index_buffer_addr = reinterpret_cast<uint64_t>(indices.data());
    tri_desc.vertex_count = static_cast<uint32_t>(verts.size());
    tri_desc.vertex_stride_bytes = sizeof(Vec3);
    tri_desc.index_count = static_cast<uint32_t>(indices.size());
    tri_desc.index_format_bytes = 4u;

    XBVH_BUILD_INPUTS inputs{};
    inputs.type = XBVH_AS_TYPE_BOTTOM_LEVEL;
    inputs.flags = XBVH_BUILD_FLAG_ALLOW_UPDATE | XBVH_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.num_descs = 1u;
    inputs.descs_addr = reinterpret_cast<uint64_t>(&tri_desc);

    XBVH_PREBUILD_INFO prebuild{};
    check(XboxBvhGetPrebuildInfo(&inputs, &prebuild) == SCARLETT_OK,
          "XboxBvhGetPrebuildInfo failed");
    check(prebuild.ResultDataMaxSizeInBytes > 0ULL &&
              prebuild.ScratchDataSizeInBytes > 0ULL &&
              prebuild.UpdateScratchDataSizeInBytes > 0ULL,
          "Prebuild sizes must be non-zero");

    std::vector<uint8_t> blas_buf(static_cast<size_t>(prebuild.ResultDataMaxSizeInBytes), 0u);
    std::vector<uint8_t> scratch_buf(static_cast<size_t>(prebuild.ScratchDataSizeInBytes), 0u);
    uint64_t written = 0ULL;
    check(XboxBvhBuildAccelerationStructure(
              &inputs, blas_buf.data(), blas_buf.size(), nullptr,
              scratch_buf.data(), scratch_buf.size(), &written) == SCARLETT_OK,
          "XboxBvhBuildAccelerationStructure BLAS failed");

    const auto* hdr = reinterpret_cast<const scarlett::xg_xs::ScarlettBvh4Header*>(blas_buf.data());
    check(hdr->magic == SCARLETT_BVH4_MAGIC && hdr->primitive_count == 1024u &&
              hdr->leaf_node_count == 1024u && hdr->box_node_count > 0u,
          "BVH4 header & node counts invalid");

    // Verify hits & misses against brute force
    for (int ry = -6; ry <= 6; ry += 3) {
        for (int rx = -12; rx <= 12; rx += 4) {
            XBVH_RAY_DESC ray{};
            ray.origin[0] = static_cast<float>(rx) + 0.37f;
            ray.origin[1] = static_cast<float>(ry) + 0.41f;
            ray.origin[2] = 0.0f;
            ray.t_min = 0.001f;
            ray.direction[0] = 0.0f;
            ray.direction[1] = 0.0f;
            ray.direction[2] = 1.0f;
            ray.t_max = 100.0f;
            ray.instance_mask = 0xFFu;

            XBVH_RAY_HIT hit{};
            check(XboxBvhIntersectRay(blas_buf.data(), &ray, &hit) == SCARLETT_OK,
                  "XboxBvhIntersectRay failed");
            const float expected_z =
                10.0f + 0.1f * std::sin(ray.origin[0] * 0.5f) * std::cos(ray.origin[1] * 0.5f);
            check(hit.hit == 1u && std::fabs(hit.t - expected_z) < 0.05f,
                  "BVH4 ray hit t mismatch on 1024-triangle grid");
            check(hit.nodes_visited < 64u,
                  "BVH4 traversal must prune logarithmically (visited=" +
                      std::to_string(hit.nodes_visited) + ")");
        }
    }

    // Ray miss test
    XBVH_RAY_DESC miss_ray{};
    miss_ray.origin[0] = 50.0f;
    miss_ray.origin[1] = 50.0f;
    miss_ray.origin[2] = 0.0f;
    miss_ray.t_min = 0.001f;
    miss_ray.direction[2] = 1.0f;
    miss_ray.t_max = 100.0f;
    miss_ray.instance_mask = 0xFFu;
    XBVH_RAY_HIT miss_hit{};
    check(XboxBvhIntersectRay(blas_buf.data(), &miss_ray, &miss_hit) == SCARLETT_OK &&
              miss_hit.hit == 0u,
          "BVH4 miss ray must report hit == 0");

    // Refit / Update test: shift entire mesh by +5.0 in Z
    for (auto& v : verts) {
        v.z += 5.0f;
    }
    inputs.flags = XBVH_BUILD_FLAG_PERFORM_UPDATE | XBVH_BUILD_FLAG_ALLOW_UPDATE;
    uint64_t refit_written = 0ULL;
    check(XboxBvhBuildAccelerationStructure(
              &inputs, blas_buf.data(), blas_buf.size(), blas_buf.data(),
              scratch_buf.data(), scratch_buf.size(), &refit_written) == SCARLETT_OK,
          "BVH4 PERFORM_UPDATE refit failed");

    XBVH_RAY_DESC refit_ray{};
    refit_ray.origin[0] = 0.37f;
    refit_ray.origin[1] = 0.41f;
    refit_ray.origin[2] = 0.0f;
    refit_ray.t_min = 0.001f;
    refit_ray.direction[2] = 1.0f;
    refit_ray.t_max = 100.0f;
    refit_ray.instance_mask = 0xFFu;
    XBVH_RAY_HIT refit_hit{};
    check(XboxBvhIntersectRay(blas_buf.data(), &refit_ray, &refit_hit) == SCARLETT_OK &&
              refit_hit.hit == 1u && std::fabs(refit_hit.t - 15.0f) < 0.08f,
          "Refitted BVH4 must reflect +5.0 Z shift");

    // TLAS build + instance transform + mask test
    XBVH_INSTANCE_DESC inst{};
    inst.transform[0] = 1.0f; inst.transform[3] = 100.0f; /* X + 100 */
    inst.transform[5] = 1.0f;
    inst.transform[10] = 1.0f;
    inst.instance_id = 42u;
    inst.instance_mask = 0x01u;
    inst.blas_header_addr = reinterpret_cast<uint64_t>(blas_buf.data());

    XBVH_BUILD_INPUTS tlas_inputs{};
    tlas_inputs.type = XBVH_AS_TYPE_TOP_LEVEL;
    tlas_inputs.flags = XBVH_BUILD_FLAG_PREFER_FAST_TRACE;
    tlas_inputs.num_descs = 1u;
    tlas_inputs.descs_addr = reinterpret_cast<uint64_t>(&inst);

    XBVH_PREBUILD_INFO tlas_prebuild{};
    check(XboxBvhGetPrebuildInfo(&tlas_inputs, &tlas_prebuild) == SCARLETT_OK,
          "TLAS prebuild info failed");
    std::vector<uint8_t> tlas_buf(static_cast<size_t>(tlas_prebuild.ResultDataMaxSizeInBytes), 0u);
    uint64_t tlas_written = 0ULL;
    check(XboxBvhBuildAccelerationStructure(
              &tlas_inputs, tlas_buf.data(), tlas_buf.size(), nullptr,
              scratch_buf.data(), scratch_buf.size(), &tlas_written) == SCARLETT_OK,
          "TLAS build failed");

    XBVH_RAY_DESC tlas_ray{};
    tlas_ray.origin[0] = 100.37f;
    tlas_ray.origin[1] = 0.41f;
    tlas_ray.origin[2] = 0.0f;
    tlas_ray.t_min = 0.001f;
    tlas_ray.direction[2] = 1.0f;
    tlas_ray.t_max = 100.0f;
    tlas_ray.instance_mask = 0x01u;
    XBVH_RAY_HIT tlas_hit{};
    check(XboxBvhIntersectRay(tlas_buf.data(), &tlas_ray, &tlas_hit) == SCARLETT_OK &&
              tlas_hit.hit == 1u && tlas_hit.instance_index == 42u &&
              std::fabs(tlas_hit.t - 15.0f) < 0.08f,
          "TLAS ray hit must match transformed BLAS instance 42");

    // VK_KHR_acceleration_structure mapping check (native RT vs BDA fallback)
    ScarlettVkAccelTrianglesDataKHR vk_geom{};
    ScarlettVkAccelBuildRangeInfoKHR vk_range{};
    ScarlettVkAccelBuildGeometryInfoKHR vk_info{};
    inputs.flags = XBVH_BUILD_FLAG_ALLOW_UPDATE;
    check(XboxBvhMapToVulkanBuildInfo(
              &inputs, 1u, 0x1111ULL, 0ULL, 0x2222ULL, &vk_geom, 1u, &vk_range, &vk_info) ==
              SCARLETT_OK,
          "XboxBvhMapToVulkanBuildInfo native RT failed");
    check(vk_info.sType == SCARLETT_VK_STYPE_ACCEL_BUILD_GEOMETRY_INFO_KHR &&
              vk_info.backend_selected == XBVH_RT_BACKEND_VK_KHR_ACCEL_STRUCT &&
              vk_range.primitiveCount == 1024u,
          "VK_KHR_acceleration_structure mapping mismatch");

    check(XboxBvhMapToVulkanBuildInfo(
              &inputs, 0u, 0x1111ULL, 0ULL, 0x2222ULL, &vk_geom, 1u, &vk_range, &vk_info) ==
              SCARLETT_OK &&
              vk_info.backend_selected == XBVH_RT_BACKEND_BDA_BVH4_SOFTWARE,
          "Without VK_KHR_acceleration_structure must select BDA_BVH4_SOFTWARE fallback");
}

void test_sfs_atomics_and_16k_vsm_coalescing() {
    // 1. SFS 64-bit atomics with log2(MipRegion) packed in low 4 bits [3:0]
    //    16K x 16K VSM with 128x128 mip region -> log2(128) = 7, 128x128 = 16,384 tiles
    scarlett::xg_xs::SfsResidencyTracker sfs(16384u, 16384u, 128u, 128u);
    check(sfs.log2_mip_region() == 7u && sfs.tile_cols() == 128u && sfs.tile_rows() == 128u,
          "16K VSM SFS tracker geometry invalid");

    // Multi-threaded 64-bit atomic min feedback recording
    std::vector<std::thread> workers;
    for (uint32_t t = 0u; t < 4u; ++t) {
        workers.emplace_back([&sfs, t]() {
            const uint8_t mip = static_cast<uint8_t>(6u - t); // min mip will be 3
            for (uint32_t y = 10u; y < 20u; ++y) {
                for (uint32_t x = 10u; x < 20u; ++x) {
                    sfs.atomic_record_feedback(x, y, mip, static_cast<uint16_t>(100u + t));
                }
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    const uint64_t sample_word = sfs.read_feedback_word(12u, 15u);
    check(scarlett_sfs_decode_log2_mip_region(sample_word) == 7u,
          "SFS bits [3:0] must pack log2(MipRegion) == 7");
    check(scarlett_sfs_decode_min_mip(sample_word) == 3u,
          "SFS 64-bit atomic min_mip must converge to 3");
    check((scarlett_sfs_decode_flags(sample_word) & SCARLETT_SFS_FLAG_PENDING) != 0u,
          "SFS unresident requested tile must be marked PENDING");

    auto pending = sfs.collect_pending_stream_tiles();
    check(pending.size() == 100u, "SFS must collect 100 pending tiles");

    // Mark tiles resident
    for (uint32_t y = 10u; y < 20u; ++y) {
        for (uint32_t x = 10u; x < 20u; ++x) {
            sfs.mark_tile_resident(x, y, 0xABCDULL, 0ULL, 105u);
        }
    }
    check(sfs.collect_pending_stream_tiles().empty(),
          "After binding tiles resident, pending list must be empty");

    // 2. 16K x 16K VSM vkQueueBindSparse coalescing stress test:
    //    Bind 4,096 contiguous 64 KiB tiles (64 x 64 region in 128 x 128 16K VSM page grid)
    std::vector<ScarlettSparseTileBindRequest> reqs;
    reqs.reserve(4096u);
    uint64_t running_mem_off = 0ULL;
    for (uint32_t y = 0u; y < 64u; ++y) {
        for (uint32_t x = 0u; x < 64u; ++x) {
            ScarlettSparseTileBindRequest r{};
            r.mip_level = 0u;
            r.array_layer = 0u;
            r.tile_x = x;
            r.tile_y = y;
            r.tile_width_texels = 128u;
            r.tile_height_texels = 128u;
            r.memory_handle = 0xDEADBEEFULL;
            r.memory_offset = running_mem_off;
            r.resource_offset = static_cast<uint64_t>(y * 64u + x) * SCARLETT_SFS_TILE_SIZE_BYTES;
            reqs.push_back(r);
            running_mem_off += SCARLETT_SFS_TILE_SIZE_BYTES;
        }
    }

    std::vector<ScarlettVkSparseImageMemoryBind> img_binds(4096u);
    std::vector<ScarlettVkSparseMemoryBind> mem_binds(4096u);
    uint32_t out_img_cnt = 0u;
    uint32_t out_mem_cnt = 0u;
    ScarlettSparseCoalesceStats stats{};

    check(XGSparseCoalesceBinds(
              reqs.data(), static_cast<uint32_t>(reqs.size()), 1u,
              img_binds.data(), static_cast<uint32_t>(img_binds.size()), &out_img_cnt,
              mem_binds.data(), static_cast<uint32_t>(mem_binds.size()), &out_mem_cnt,
              &stats) == SCARLETT_OK,
          "XGSparseCoalesceBinds failed");

    check(out_mem_cnt == 1u && mem_binds[0].size == 4096ULL * SCARLETT_SFS_TILE_SIZE_BYTES,
          "4,096 contiguous 64 KiB VSM tiles must coalesce into 1 VkSparseMemoryBind (256 MiB)");
    check(out_img_cnt == 1u && img_binds[0].extentWidth == 64u * 128u &&
              img_binds[0].extentHeight == 64u * 128u,
          "64x64 contiguous 16K VSM tile block must coalesce into 1 2D VkSparseImageMemoryBind (8192x8192)");
}

struct PixTraceLog {
    std::vector<std::string> begins;
    std::vector<std::string> markers;
    uint32_t ends = 0u;
    float last_color[4]{};
};

void test_pixevt_stack_and_debug_utils() {
    auto& backend = scarlett::xg_xs::PixEventBackend::instance();
    backend.clear_recorded_events();
    backend.set_enabled(true);

    PixTraceLog trace{};
    scarlett::xg_xs::PixDebugUtilsCallbacks cbs{};
    cbs.user_data = &trace;
    cbs.begin_label = [](uint64_t, const ScarlettVkDebugUtilsLabelEXT* label, void* ud) {
        auto* log = static_cast<PixTraceLog*>(ud);
        const char* name = reinterpret_cast<const char*>(static_cast<uintptr_t>(label->pLabelNameAddr));
        log->begins.emplace_back(name);
        std::memcpy(log->last_color, label->color, sizeof(log->last_color));
    };
    cbs.end_label = [](uint64_t, void* ud) {
        static_cast<PixTraceLog*>(ud)->ends++;
    };
    cbs.insert_label = [](uint64_t, const ScarlettVkDebugUtilsLabelEXT* label, void* ud) {
        auto* log = static_cast<PixTraceLog*>(ud);
        const char* name = reinterpret_cast<const char*>(static_cast<uintptr_t>(label->pLabelNameAddr));
        log->markers.emplace_back(name);
        std::memcpy(log->last_color, label->color, sizeof(log->last_color));
    };
    backend.set_callbacks(cbs);

    void* cmd = reinterpret_cast<void*>(static_cast<uintptr_t>(0xCAFEBABEULL));
    PIXBeginEvent(0xFFFF0000ULL, "Frame_%u", 42u);
    PIXBeginEventOnCommandList(cmd, 0x8000FF40ULL, "ShadowPass_%s", "VSM");
    PIXSetMarkerOnCommandList(cmd, 0xFF0000FFULL, "Cascade_%d", 3);

    uint32_t underflow = 0u;
    check(PIXGetEventStackDepth(nullptr, &underflow) == 1u && underflow == 0u,
          "Default queue PIX stack depth must be 1");
    check(PIXGetEventStackDepth(cmd, &underflow) == 2u - 1u && underflow == 0u,
          "CommandList PIX stack depth must be 1");

    PIXEndEventOnCommandList(cmd);
    PIXEndEventOnCommandList(cmd); // deliberate underflow check
    check(PIXGetEventStackDepth(cmd, &underflow) == 0u && underflow == 1u,
          "CommandList PIX stack underflow must be detected without calling vkCmdEndDebugUtilsLabelEXT");

    PIXSetMarker(0xFFFFFFFFULL, "FrameMarker");
    PIXEndEvent();

    check(trace.begins.size() == 2u && trace.begins[0] == "Frame_42" &&
              trace.begins[1] == "ShadowPass_VSM",
          "PIXBeginEvent labels mismatch");
    check(trace.markers.size() == 2u && trace.markers[0] == "Cascade_3" &&
              trace.markers[1] == "FrameMarker",
          "PIXSetMarker labels mismatch");
    check(trace.ends == 2u, "PIXEndEvent must not dispatch on stack underflow");
    check(std::fabs(trace.last_color[0] - 1.0f) < 1e-4f &&
              std::fabs(trace.last_color[3] - 1.0f) < 1e-4f,
          "PIX ARGB->RGBA color conversion invalid");

    // Color channel verification: 0x80204080 -> A=128/255, R=32/255, G=64/255, B=128/255
    float rgba[4]{};
    scarlett::xg_xs::PixEventBackend::argb_to_rgba_float(0x80204080ULL, rgba);
    check(std::fabs(rgba[0] - (32.0f / 255.0f)) < 1e-4f &&
              std::fabs(rgba[1] - (64.0f / 255.0f)) < 1e-4f &&
              std::fabs(rgba[2] - (128.0f / 255.0f)) < 1e-4f &&
              std::fabs(rgba[3] - (128.0f / 255.0f)) < 1e-4f,
          "ARGB 0x80204080 -> RGBA float conversion mismatch");

    backend.set_callbacks({});
}

struct StubCapture {
    int count = 0;
    scarlett_stub_context_t last{};
};

scarlett_stub_action_t test_stub_sink(const scarlett_stub_context_t* ctx, void* ud) {
    auto* cap = static_cast<StubCapture*>(ud);
    cap->count++;
    cap->last = *ctx;
    return SCARLETT_STUB_ACTION_LOG_AND_FAIL; /* prevent __builtin_trap() in unit test */
}

void test_auto_stub_thunks() {
    StubCapture cap{};
    scarlett_xg_xs_set_stub_sink(test_stub_sink, &cap);
    scarlett_xg_xs_set_strict_mode(0u);

    void* out_comp = nullptr;
    const scarlett_hresult_t hr1 = XGCreateTexture1DComputer(
        reinterpret_cast<const void*>(0x1234ULL), &out_comp);
    check(hr1 == SCARLETT_ERR_UNIMPLEMENTED_STUB && scarlett_failed(hr1),
          "Unimplemented XGCreateTexture1DComputer must never return S_OK");
    check(cap.count == 1 && std::string(cap.last.dll_name) == "xg_xs.dll" &&
              std::string(cap.last.symbol_name) == "XGCreateTexture1DComputer" &&
              cap.last.rcx == 0x1234ULL,
          "Stub sink did not capture XGCreateTexture1DComputer context/RCX");

    const scarlett_hresult_t hr2 = XboxBvhBuildProceduralGeometry(nullptr, nullptr);
    check(hr2 == SCARLETT_ERR_UNIMPLEMENTED_STUB && cap.count == 2 &&
              std::string(cap.last.dll_name) == "XboxBvhBuilder.dll",
          "XboxBvhBuildProceduralGeometry must route through auto-stub thunk");

    const scarlett_hresult_t hr3 = PIXGpuCaptureNextFrames(nullptr, 2u);
    check(hr3 == SCARLETT_ERR_UNIMPLEMENTED_STUB && cap.count == 3 &&
              std::string(cap.last.dll_name) == "pixevt.dll" && cap.last.rdx == 2u,
          "PIXGpuCaptureNextFrames must route through auto-stub thunk");

    scarlett_xg_xs_set_stub_sink(nullptr, nullptr);
}

} // namespace

int main() {
    test_unix_handshake();
    test_swizzle_roundtrip_matrix();
    test_40bit_above_4gib_addressing();
    test_spirv_deswizzle_and_transfer_paths();
    test_xbox_bvh4_and_refit();
    test_sfs_atomics_and_16k_vsm_coalescing();
    test_pixevt_stack_and_debug_utils();
    test_auto_stub_thunks();

    if (g_failures == 0) {
        std::cout << "[PASS] all xg_xs / XboxBvhBuilder / SFS / pixevt unit tests passed\n";
        return 0;
    }
    std::cerr << "[FAIL] " << g_failures << " test check(s) failed\n";
    return 1;
}
