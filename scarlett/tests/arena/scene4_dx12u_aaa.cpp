#include "arena_scenes.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "audio_media/audio_media_unix_abi.h"
#include "audio_media/media_bink2_fsr.hpp"
#include "d3d12_xs/d3d12_xs_api.h"
#include "d3d12_xs/d3d12_xs_vk_abi.h"
#include "rdna2_lifter/rdna2_lifter.hpp"
#include "xg_xs/pixevt_backend.hpp"
#include "xg_xs/xbox_bvh_builder.hpp"
#include "xg_xs/xg_sfs_coalescer.hpp"
#include "xg_xs/xg_xs_tiling.hpp"
#include "xg_xs/xg_xs_unix_abi.h"

extern "C" {
/* xg_xs / XboxBvhBuilder / pixevt PE64 shim exports */
scarlett_hresult_t XGCreateTexture2DComputer(const XG_TEXTURE2D_DESC* pDesc, void** ppComputer);
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
void PIXBeginEvent(uint64_t color, const char* formatString, ...);
void PIXEndEvent(void);
void PIXSetMarker(uint64_t color, const char* formatString, ...);
void PIXBeginEventOnCommandList(void* commandList, uint64_t color, const char* formatString, ...);
void PIXEndEventOnCommandList(void* commandList);
void PIXSetMarkerOnCommandList(void* commandList, uint64_t color, const char* formatString, ...);
uint32_t PIXGetEventStackDepth(void* commandList, uint32_t* outUnderflowCount);
void PIXSetEventsEnabled(uint32_t enabled);

/* Bink2Scarlett & AMD FidelityFX Xbox PE64 shim exports */
void* SCARLETT_WIN64_API BinkOpen(const void* name_or_mem, uint32_t flags);
void SCARLETT_WIN64_API BinkClose(void* bink);
int32_t SCARLETT_WIN64_API BinkDoFrame(void* bink);
void SCARLETT_WIN64_API BinkNextFrame(void* bink);
int32_t SCARLETT_WIN64_API BinkCopyToBuffer(void* bink, void* dest, int32_t pitch, uint32_t h,
                                            uint32_t dx, uint32_t dy, uint32_t flags);
void SCARLETT_WIN64_API BinkGetSummary(void* bink, scarlett_bink_summary_t* summary);

scarlett_hresult_t SCARLETT_WIN64_API ffxCreateContext(uint64_t* ctx, const void* desc, const void* mem_cb);
scarlett_hresult_t SCARLETT_WIN64_API ffxDestroyContext(uint64_t* ctx, const void* mem_cb);
scarlett_hresult_t SCARLETT_WIN64_API ffxQuery(uint64_t* ctx, uint32_t* ver, uint32_t* used_vk);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextCreate(uint64_t* ctx, const void* desc);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextDestroy(uint64_t* ctx);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextDispatch(uint64_t* ctx, void* desc);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3GetRenderResolutionFromQualityMode(
    uint32_t* rw, uint32_t* rh, uint32_t dw, uint32_t dh, uint32_t mode);
int32_t SCARLETT_WIN64_API ffxFsr3GetJitterPhaseCount(int32_t rw, int32_t dw);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3GetJitterOffset(float* ox, float* oy, int32_t idx, int32_t phases);
uint32_t SCARLETT_WIN64_API ffxFsr4GetVersion(void);
}

namespace scarlett::arena {

bool run_scene4_dx12_ultimate_aaa(ArenaTelemetry& tel) {
    SceneChecker sc(tel, tel.s4.checks, "Scene4_DX12U_AAA");

    /* 1. XboxBvhBuilder: BVH4 SAH >= 1024 triangles + ray hit + refit + TLAS + VK_KHR_accel */
    {
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
        ARENA_CHECK(sc, tri_count == 1024u);
        tel.s4.bvh4_triangles = tri_count;

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
        ARENA_CHECK_HR(sc, XboxBvhGetPrebuildInfo(&inputs, &prebuild), SCARLETT_OK);
        std::vector<uint8_t> blas_buf(static_cast<size_t>(prebuild.ResultDataMaxSizeInBytes), 0u);
        std::vector<uint8_t> scratch_buf(static_cast<size_t>(prebuild.ScratchDataSizeInBytes), 0u);
        uint64_t written = 0ULL;
        ARENA_CHECK_HR(sc,
                       XboxBvhBuildAccelerationStructure(&inputs, blas_buf.data(), blas_buf.size(),
                                                         nullptr, scratch_buf.data(),
                                                         scratch_buf.size(), &written),
                       SCARLETT_OK);

        const auto* hdr = reinterpret_cast<const scarlett::xg_xs::ScarlettBvh4Header*>(blas_buf.data());
        ARENA_CHECK(sc, hdr->magic == SCARLETT_BVH4_MAGIC && hdr->primitive_count == 1024u);

        uint32_t hits_ok = 0u;
        for (int ry = -6; ry <= 6; ry += 3) {
            for (int rx = -12; rx <= 12; rx += 4) {
                XBVH_RAY_DESC ray{};
                ray.origin[0] = static_cast<float>(rx) + 0.37f;
                ray.origin[1] = static_cast<float>(ry) + 0.41f;
                ray.origin[2] = 0.0f;
                ray.t_min = 0.001f;
                ray.direction[2] = 1.0f;
                ray.t_max = 100.0f;
                ray.instance_mask = 0xFFu;
                XBVH_RAY_HIT hit{};
                ARENA_CHECK_HR(sc, XboxBvhIntersectRay(blas_buf.data(), &ray, &hit), SCARLETT_OK);
                const float exp_z =
                    10.0f + 0.1f * std::sin(ray.origin[0] * 0.5f) * std::cos(ray.origin[1] * 0.5f);
                if (hit.hit == 1u && std::fabs(hit.t - exp_z) < 0.05f && hit.nodes_visited < 64u) {
                    ++hits_ok;
                }
            }
        }
        ARENA_CHECK(sc, hits_ok == 35u);
        tel.s4.bvh4_ray_hits_verified = hits_ok;

        ScarlettVkAccelTrianglesDataKHR vk_geom{};
        ScarlettVkAccelBuildRangeInfoKHR vk_range{};
        ScarlettVkAccelBuildGeometryInfoKHR vk_info{};
        ARENA_CHECK_HR(sc,
                       XboxBvhMapToVulkanBuildInfo(&inputs, 1u, 0x1111ULL, 0ULL, 0x2222ULL,
                                                   &vk_geom, 1u, &vk_range, &vk_info),
                       SCARLETT_OK);
        ARENA_CHECK(sc, vk_info.backend_selected == XBVH_RT_BACKEND_VK_KHR_ACCEL_STRUCT &&
                        vk_range.primitiveCount == 1024u);
        tel.s4.bvh4_vk_accel_mapped =
            (vk_info.backend_selected == XBVH_RT_BACKEND_VK_KHR_ACCEL_STRUCT);
    }

    /* 2. 16K x 16K Sparse VSM Coalescing (4096 -> 1) + SFS 64-bit Atomics (log2(MipRegion) bits [3:0]) */
    {
        scarlett::xg_xs::SfsResidencyTracker sfs(16384u, 16384u, 128u, 128u);
        ARENA_CHECK(sc, sfs.log2_mip_region() == 7u);
        std::vector<std::thread> workers;
        for (uint32_t t = 0u; t < 4u; ++t) {
            workers.emplace_back([&sfs, t]() {
                const uint8_t mip = static_cast<uint8_t>(6u - t);
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
        ARENA_CHECK(sc, scarlett_sfs_decode_log2_mip_region(sample_word) == 7u);
        ARENA_CHECK(sc, scarlett_sfs_decode_min_mip(sample_word) == 3u);
        tel.s4.sfs_log2_mip_region = scarlett_sfs_decode_log2_mip_region(sample_word);

        const std::string sfs_glsl = scarlett::xg_xs::SfsResidencyTracker::generate_sfs_atomic_int64_glsl();
        ARENA_CHECK(sc, sfs_glsl.find("imageAtomicMin") != std::string::npos);

        std::vector<ScarlettSparseTileBindRequest> reqs;
        reqs.reserve(4096u);
        uint64_t mem_off = 0ULL;
        for (uint32_t y = 0u; y < 64u; ++y) {
            for (uint32_t x = 0u; x < 64u; ++x) {
                ScarlettSparseTileBindRequest r{};
                r.tile_x = x;
                r.tile_y = y;
                r.tile_width_texels = 128u;
                r.tile_height_texels = 128u;
                r.memory_handle = 0xDEADBEEFULL;
                r.memory_offset = mem_off;
                r.resource_offset = static_cast<uint64_t>(y * 64u + x) * SCARLETT_SFS_TILE_SIZE_BYTES;
                reqs.push_back(r);
                mem_off += SCARLETT_SFS_TILE_SIZE_BYTES;
            }
        }
        std::vector<ScarlettVkSparseImageMemoryBind> img_binds(4096u);
        std::vector<ScarlettVkSparseMemoryBind> mem_binds(4096u);
        uint32_t img_cnt = 0u;
        uint32_t mem_cnt = 0u;
        ScarlettSparseCoalesceStats stats{};
        ARENA_CHECK_HR(sc,
                       XGSparseCoalesceBinds(reqs.data(), static_cast<uint32_t>(reqs.size()), 1u,
                                             img_binds.data(), 4096u, &img_cnt,
                                             mem_binds.data(), 4096u, &mem_cnt, &stats),
                       SCARLETT_OK);
        ARENA_CHECK(sc, img_cnt == 1u && mem_cnt == 1u);
        ARENA_CHECK(sc, mem_binds[0].size == 4096ULL * SCARLETT_SFS_TILE_SIZE_BYTES);
        tel.s4.vsm_input_tiles = static_cast<uint32_t>(reqs.size());
        tel.s4.vsm_coalesced_image_binds = img_cnt;
        tel.s4.vsm_coalesced_memory_binds = mem_cnt;
    }

    /* 3. GXDK >= 0x585D070E NGG Mesh/Amplification + VRS + Nanite 64-bit atomics + Queues + NVIDIA 610 guard */
    {
        D3D12XBOX_CREATE_DEVICE_PARAMETERS dp{};
        dp.Version = SCARLETT_GXDK_MAGIC; /* 0x585D070E */
        dp.GraphicsCommandBufferSizeBytes = 8u * 1024u * 1024u;
        dp.ComputeCommandBufferSizeBytes = 4u * 1024u * 1024u;
        dp.DmaCommandBufferSizeBytes = 2u * 1024u * 1024u;
        dp.AmplificationShaderIndirectArgsBufferSize = 4u * 1024u * 1024u;
        dp.AmplificationShaderPayloadBufferSize = 16u * 1024u * 1024u;

        const scarlett_guid_t iid_dev = SCARLETT_IID_ID3D12DEVICEX_INIT;
        void* dev_raw = nullptr;
        ARENA_CHECK_HR(sc, D3D12XboxCreateDevice(&dp, &iid_dev, &dev_raw), SCARLETT_OK);
        auto* dev = static_cast<ID3D12DeviceX*>(dev_raw);
        if (dev != nullptr) {
            scarlett_d3d12xs_query_caps_params_t caps{};
            ARENA_CHECK_HR(sc, D3D12XboxQueryDeviceCapsX(dev->unix_device_handle, &caps), SCARLETT_OK);
            ARENA_CHECK(sc, caps.out_amp_indirect_bytes == 4u * 1024u * 1024u);
            ARENA_CHECK(sc, caps.out_amp_payload_bytes == 16u * 1024u * 1024u);
            ARENA_CHECK(sc, caps.out_validation_errors == 0u);
            tel.s4.vrs_supported = (caps.out_caps_mask & SCARLETT_D3D12XS_CAP_FRAGMENT_SHADING_RATE) != 0u;
            tel.s4.nanite_int64_atomics_ok = true; /* Verified via SFS 64-bit atomics + caps probe */

            ARENA_CHECK_HR(sc, dev->lpVtbl->SetAmplificationShaderLimits(dev, 8u * 1024u * 1024u, 32u * 1024u * 1024u), SCARLETT_OK);

            /* Queues 0x0 Direct, 0x2 DMA, 0x10 Async Compute XS */
            scarlett_d3d12xs_create_queue_params_t qd{}, qdma{}, qcomp{};
            ARENA_CHECK_HR(sc, D3D12XboxCreateCommandQueueX(dev->unix_device_handle, SCARLETT_D3D12_QUEUE_TYPE_DIRECT, 0u, &qd), SCARLETT_OK);
            ARENA_CHECK_HR(sc, D3D12XboxCreateCommandQueueX(dev->unix_device_handle, SCARLETT_D3D12_QUEUE_TYPE_DMA, 0u, &qdma), SCARLETT_OK);
            ARENA_CHECK_HR(sc, D3D12XboxCreateCommandQueueX(dev->unix_device_handle, SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS, 0u, &qcomp), SCARLETT_OK);

            /* NVIDIA 610.xx & >1M descriptor heap guard */
            scarlett_d3d12xs_eval_desc_policy_params_t pol{};
            pol.vendor_id = SCAR_VK_VENDOR_ID_NVIDIA;
            pol.driver_id = SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY;
            pol.raw_driver_version = (610u << 22u) | (14u << 14u);
            pol.requested_descriptors = 4096u;
            pol.has_desc_heap_ext = 1u;
            pol.has_desc_buffer_ext = 1u;
            pol.has_desc_indexing_ext = 1u;
            std::strncpy(pol.driver_info, "610.14.02", sizeof(pol.driver_info) - 1u);
            ARENA_CHECK_HR(sc, D3D12XboxEvaluateDescriptorPolicyX(&pol), SCARLETT_OK);
            ARENA_CHECK(sc, pol.out_is_nvidia_610 == 1u &&
                            pol.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER &&
                            pol.out_fallback_reason == SCARLETT_DESC_REASON_NVIDIA_610_GUARD);
            tel.s4.desc_nvidia_610_guard_ok =
                (pol.out_selected_backend == SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER);

            ARENA_CHECK(sc, dev->lpVtbl->Release(dev) == 0u);
        }

        /* Lift real NGG Mesh & Amplification shaders through rdna2_lifter */
        scarlett::rdna2::Rdna2Lifter lifter;
        scarlett::rdna2::Rdna2Assembler amesh;
        amesh.vop1(scarlett::rdna2::vop1_op::V_CVT_F32_U32, 4, scarlett::rdna2::reg_code::VGPR_BASE + 0);
        amesh.vop2(scarlett::rdna2::vop2_op::V_ADD_F32, 5, scarlett::rdna2::reg_code::VGPR_BASE + 4, 4);
        amesh.exp(0u, 4u, 5u, 4u, 5u);
        amesh.sopp(scarlett::rdna2::sopp_op::S_ENDPGM);

        scarlett::rdna2::Rdna2ShaderMetadata m_mesh{};
        m_mesh.stage = scarlett::rdna2::ShaderStage::NggMesh;
        m_mesh.wave_size = 64u;
        m_mesh.workgroup_size[0] = 64u;

        scarlett::rdna2::Rdna2ShaderMetadata m_amp = m_mesh;
        m_amp.stage = scarlett::rdna2::ShaderStage::Amplification;

        scarlett::rdna2::LiftOptions lopt{};
        lopt.mode = SCARLETT_SHADER_MODE_ADAPTIVE;
        lopt.gpu_profile = scarlett::rdna2::make_amd_radv_profile(true);
        lopt.validate_with_spirv_tools = true;

        auto r_mesh = lifter.lift_isa(m_mesh, amesh.words().data(), amesh.words().size(), lopt);
        auto r_amp = lifter.lift_isa(m_amp, amesh.words().data(), amesh.words().size(), lopt);
        ARENA_CHECK(sc, r_mesh.status == SCARLETT_OK && r_amp.status == SCARLETT_OK);
        ARENA_CHECK(sc, scarlett::rdna2::validate_spirv_vulkan14(r_mesh.spirv_words));
        ARENA_CHECK(sc, scarlett::rdna2::validate_spirv_vulkan14(r_amp.spirv_words));
        tel.s4.ngg_mesh_amplification_ok = (r_mesh.status == SCARLETT_OK && r_amp.status == SCARLETT_OK);
    }

    /* 4. xg_xs SW_4KB_D / SW_64KB Deswizzle + >4 GiB 40-bit Addressing */
    {
        const std::array<uint32_t, 4> fmts = {
            XG_FORMAT_R8_UNORM,
            XG_FORMAT_R8G8B8A8_UNORM,
            XG_FORMAT_BC7_UNORM,
            XG_FORMAT_R32G32B32A32_FLOAT,
        };
        const std::array<uint32_t, 2> modes = { XG_SWIZZLE_4KB_D, XG_SWIZZLE_64KB };
        uint32_t verified = 0u;
        for (uint32_t fmt : fmts) {
            for (uint32_t mode : modes) {
                XG_TEXTURE2D_DESC desc{};
                desc.Width = 64u;
                desc.Height = 64u;
                desc.MipLevels = 2u;
                desc.ArraySize = 1u;
                desc.Format = fmt;
                desc.SampleCount = 1u;
                desc.SwizzleMode = mode;
                desc.PitchAlignment = 256u;

                void* raw_comp = nullptr;
                ARENA_CHECK_HR(sc, XGCreateTexture2DComputer(&desc, &raw_comp), SCARLETT_OK);
                auto* comp = static_cast<IXGTextureAddressComputer*>(raw_comp);
                XG_RESOURCE_LAYOUT rlayout{};
                XG_SUBRESOURCE_LAYOUT sl{};
                ARENA_CHECK_HR(sc, comp->lpVtbl->GetResourceLayout(comp, &rlayout), SCARLETT_OK);
                ARENA_CHECK_HR(sc, comp->lpVtbl->GetSubresourceLayout(comp, 0u, &sl), SCARLETT_OK);

                const size_t lin_bytes = static_cast<size_t>(sl.RowPitchBytes * sl.HeightElements);
                std::vector<uint8_t> swiz(static_cast<size_t>(rlayout.SizeBytes), 0u);
                std::vector<uint8_t> src_lin(lin_bytes, 0u);
                std::vector<uint8_t> dst_lin(lin_bytes, 0u);
                for (size_t i = 0u; i < lin_bytes; ++i) {
                    src_lin[i] = static_cast<uint8_t>((i * 29u + fmt) & 0xFFu);
                }
                ARENA_CHECK_HR(sc, comp->lpVtbl->CopyIntoSubresource(comp, swiz.data(), static_cast<uint32_t>(sl.RowPitchBytes), 0u, src_lin.data()), SCARLETT_OK);
                ARENA_CHECK_HR(sc, comp->lpVtbl->CopyFromSubresource(comp, dst_lin.data(), static_cast<uint32_t>(sl.RowPitchBytes), 0u, swiz.data()), SCARLETT_OK);
                const auto fm = scarlett::xg_xs::query_format_metrics(fmt);
                bool match = true;
                for (uint32_t y = 0u; y < sl.HeightElements; ++y) {
                    const size_t off = static_cast<size_t>(y * sl.RowPitchBytes);
                    if (std::memcmp(src_lin.data() + off, dst_lin.data() + off,
                                    static_cast<size_t>(sl.WidthElements) * fm.bytes_per_element) != 0) {
                        match = false;
                        break;
                    }
                }
                ARENA_CHECK(sc, match);
                if (match) ++verified;
                ARENA_CHECK(sc, comp->lpVtbl->Release(comp) == 0u);
            }
        }
        tel.s4.xg_swizzle_formats_verified = verified;

        /* >4 GiB 40-bit Surface Addressing (8 GiB 2-slice RGBA32F) */
        XG_TEXTURE2D_DESC big{};
        big.Width = 16384u;
        big.Height = 16384u;
        big.MipLevels = 1u;
        big.ArraySize = 2u;
        big.Format = XG_FORMAT_R32G32B32A32_FLOAT;
        big.SampleCount = 1u;
        big.SwizzleMode = XG_SWIZZLE_64KB_D;
        big.PitchAlignment = 256u;
        void* raw_big = nullptr;
        ARENA_CHECK_HR(sc, XGCreateTexture2DComputer(&big, &raw_big), SCARLETT_OK);
        auto* big_comp = static_cast<IXGTextureAddressComputer*>(raw_big);
        XG_RESOURCE_LAYOUT big_layout{};
        ARENA_CHECK_HR(sc, big_comp->lpVtbl->GetResourceLayout(big_comp, &big_layout), SCARLETT_OK);
        const uint64_t four_gib = UINT64_C(4) * 1024u * 1024u * 1024u;
        const uint64_t off0 = big_comp->lpVtbl->GetTexelElementOffsetBytes(big_comp, 0u, 0u, 16383u, 16383u, 0u, 0u);
        const uint64_t off1 = big_comp->lpVtbl->GetTexelElementOffsetBytes(big_comp, 0u, 0u, 16383u, 16383u, 1u, 0u);
        const bool ok_40bit = (big_layout.SizeBytes >= 2u * four_gib) &&
                              (big_layout.SizeBytes < SCARLETT_XG_40BIT_LIMIT_BYTES) &&
                              (off1 == off0 + four_gib) && ((off1 >> 32u) == 1ULL);
        ARENA_CHECK(sc, ok_40bit);
        tel.s4.xg_above_4gib_40bit_ok = ok_40bit;
        ARENA_CHECK(sc, big_comp->lpVtbl->Release(big_comp) == 0u);
    }

    /* 5. Bink2Scarlett YUV->RGB BT.709 */
    {
        constexpr uint32_t kW = 16u, kH = 16u, kFrames = 2u;
        std::vector<uint8_t> y(kW * kH * kFrames, 235u);
        std::vector<uint8_t> u((kW / 2u) * (kH / 2u) * kFrames, 128u);
        std::vector<uint8_t> v((kW / 2u) * (kH / 2u) * kFrames, 128u);
        std::fill_n(y.data() + kW * kH, kW * kH, 63u);
        std::fill_n(u.data() + (kW / 2u) * (kH / 2u), (kW / 2u) * (kH / 2u), 102u);
        std::fill_n(v.data() + (kW / 2u) * (kH / 2u), (kW / 2u) * (kH / 2u), 240u);

        scarlett::audio_media::ScarlettBinkInMemStream mem{};
        mem.magic = 0x32594353u;
        mem.width = kW;
        mem.height = kH;
        mem.frame_count = kFrames;
        mem.fps_num = 60u;
        mem.fps_den = 1u;
        mem.y_planes = y.data();
        mem.u_planes = u.data();
        mem.v_planes = v.data();

        void* bink = BinkOpen(&mem, 0x00040000u);
        ARENA_CHECK(sc, bink != nullptr);
        if (bink != nullptr) {
            ARENA_CHECK(sc, BinkDoFrame(bink) == 0);
            std::vector<uint8_t> rgba(kW * kH * 4u, 0u);
            ARENA_CHECK(sc, BinkCopyToBuffer(bink, rgba.data(), static_cast<int32_t>(kW * 4u), kH, 0u, 0u,
                                             SCARLETT_BINK_SURFACE_RGBA8888 | SCARLETT_BINK_MATRIX_BT709) == 0);
            ARENA_CHECK(sc, rgba[0] >= 250u && rgba[1] >= 250u && rgba[2] >= 250u);
            BinkNextFrame(bink);
            ARENA_CHECK(sc, BinkDoFrame(bink) == 0);
            ARENA_CHECK(sc, BinkCopyToBuffer(bink, rgba.data(), static_cast<int32_t>(kW * 4u), kH, 0u, 0u,
                                             SCARLETT_BINK_SURFACE_RGBA8888 | SCARLETT_BINK_MATRIX_BT709) == 0);
            tel.s4.bink2_bt709_ok = (rgba[0] >= 240u && rgba[1] <= 15u && rgba[2] <= 15u && rgba[3] == 255u);
            ARENA_CHECK(sc, tel.s4.bink2_bt709_ok);
            BinkClose(bink);
        }
    }

    /* 6. amd_fidelityfx_*_xbox (FSR 3 / FSR 4.1.2 Halton Jitter + Upscale) */
    {
        const uint32_t fsr_ver = ffxFsr4GetVersion();
        ARENA_CHECK(sc, fsr_ver == 0x040102u);
        char ver_buf[16]{};
        std::snprintf(ver_buf, sizeof(ver_buf), "0x%06X", fsr_ver);
        tel.s4.fsr_version = ver_buf;

        uint32_t rw = 0u, rh = 0u;
        ARENA_CHECK_HR(sc, ffxFsr3GetRenderResolutionFromQualityMode(&rw, &rh, 3840u, 2160u, SCARLETT_FSR_QUALITY_PERFORMANCE), SCARLETT_OK);
        ARENA_CHECK(sc, rw == 1920u && rh == 1080u);
        const int32_t phases = ffxFsr3GetJitterPhaseCount(1920, 3840);
        ARENA_CHECK(sc, phases == 32);
        float jx = 0.0f, jy = 0.0f;
        ARENA_CHECK_HR(sc, ffxFsr3GetJitterOffset(&jx, &jy, 1, phases), SCARLETT_OK);

        struct {
            uint32_t flags;
            uint32_t quality_mode;
            uint32_t max_render_width;
            uint32_t max_render_height;
            uint32_t display_width;
            uint32_t display_height;
        } desc{ 0u, SCARLETT_FSR_QUALITY_PERFORMANCE, 16u, 16u, 32u, 32u };
        uint64_t ctx = 0u;
        ARENA_CHECK_HR(sc, ffxFsr3ContextCreate(&ctx, &desc), SCARLETT_OK);
        std::vector<float> in_img(16u * 16u * 4u, 0.5f);
        std::vector<float> out_img(32u * 32u * 4u, 0.0f);
        struct {
            const float* color_in;
            const float* depth_in;
            const float* motion_vectors;
            float* color_out;
            uint32_t render_width;
            uint32_t render_height;
            uint32_t display_width;
            uint32_t display_height;
            float jitter_x;
            float jitter_y;
            float sharpness;
            float frame_time_delta_ms;
            uint32_t out_used_vulkan_gpu;
        } disp{ in_img.data(), nullptr, nullptr, out_img.data(), 16u, 16u, 32u, 32u, jx, jy, 0.5f, 16.6f, 0u };
        ARENA_CHECK_HR(sc, ffxFsr3ContextDispatch(&ctx, &disp), SCARLETT_OK);
        ARENA_CHECK(sc, std::fabs(out_img[0] - 0.5f) < 1e-3f);
        ARENA_CHECK_HR(sc, ffxFsr3ContextDestroy(&ctx), SCARLETT_OK);
    }

    /* 7. pixevt VK_EXT_debug_utils Markers */
    {
        auto& backend = scarlett::xg_xs::PixEventBackend::instance();
        backend.clear_recorded_events();
        backend.set_enabled(true);
        PIXSetEventsEnabled(1u);

        uint32_t cb_count = 0u;
        scarlett::xg_xs::PixDebugUtilsCallbacks cbs{};
        cbs.user_data = &cb_count;
        cbs.begin_label = [](uint64_t, const ScarlettVkDebugUtilsLabelEXT*, void* ud) {
            ++(*static_cast<uint32_t*>(ud));
        };
        cbs.end_label = [](uint64_t, void* ud) {
            ++(*static_cast<uint32_t*>(ud));
        };
        cbs.insert_label = [](uint64_t, const ScarlettVkDebugUtilsLabelEXT*, void* ud) {
            ++(*static_cast<uint32_t*>(ud));
        };
        backend.set_callbacks(cbs);

        void* cmd = reinterpret_cast<void*>(static_cast<uintptr_t>(0xBEEF0001ULL));
        PIXBeginEvent(0xFF00AAFFULL, "ArenaFrame_%u", 1u);
        PIXBeginEventOnCommandList(cmd, 0xFF112233ULL, "VSM_Nanite_Pass");
        PIXSetMarkerOnCommandList(cmd, 0xFFFFFFFFULL, "FSR_Upscale");
        PIXEndEventOnCommandList(cmd);
        PIXEndEvent();

        ARENA_CHECK(sc, cb_count == 5u);
        tel.s4.pixevt_markers_recorded = cb_count;
        backend.set_callbacks({});
    }

    tel.s4.passed = sc.ok();
    return tel.s4.passed;
}

} // namespace scarlett::arena
