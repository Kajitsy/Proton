/*
 * Proton-Scarlett — Comprehensive Unit & Live Vulkan 1.4 Integration Tests
 * for RDNA2 (GFX10.3) -> SPIR-V 1.5/1.6 Adaptive Lifter.
 */

#include "rdna2_lifter.hpp"
#include "rdna2_ir.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace scarlett::rdna2;

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                           \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " — " << msg \
                      << std::endl;                                                \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

uint32_t f2u(float f) noexcept {
    uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    return u;
}

float u2f(uint32_t u) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

/* IEEE-754 float word equality (treating any two quiet NaNs as equal canonical NaN) */
bool words_equivalent(uint32_t a, uint32_t b) noexcept {
    if (a == b) return true;
    const float fa = u2f(a);
    const float fb = u2f(b);
    if (std::isnan(fa) && std::isnan(fb)) return true;
    if (fa == 0.0f && fb == 0.0f) return true; /* +0.0 == -0.0 under FTZ/denorm flush */
    return false;
}

/* Minimal clean-room RDNA2 BVH4 buffer matching XboxBvhBuilder 96B header + 128B box + 64B leaf */
struct SyntheticBvh4 {
    std::vector<uint8_t> bytes;
    std::vector<SpirvVmInput::RayTri> vm_tris;
};

SyntheticBvh4 build_synthetic_bvh4_scene() {
    SyntheticBvh4 s{};
    /* Layout: [0..95] ScarlettBvh4Header (96B)
     *         [96..223] ScarlettRdn2Bvh4Box32Node (128B)
     *         [224..287] ScarlettRdn2Bvh4TriLeafNode (64B) */
    s.bytes.assign(288u, 0u);

    auto w32 = [&](size_t off, uint32_t v) { std::memcpy(s.bytes.data() + off, &v, 4); };
    auto w64 = [&](size_t off, uint64_t v) { std::memcpy(s.bytes.data() + off, &v, 8); };
    auto wf  = [&](size_t off, float v)    { std::memcpy(s.bytes.data() + off, &v, 4); };

    /* Header */
    w32(0, 0x34485642u);  /* "BVH4" */
    w32(4, 1u);
    w32(8, 1u);           /* BLAS */
    w32(16, (0u << 3u) | 5u); /* root_node_ptr: box 0, type 5 (BOX32) */
    w32(20, 1u);          /* box_node_count */
    w32(24, 1u);          /* leaf_node_count */
    w32(28, 1u);          /* primitive_count */
    w64(32, 96u);         /* box_nodes_offset */
    w64(40, 224u);        /* leaf_nodes_offset */
    w64(48, 288u);        /* total_bytes */

    /* Box32Node @ 96 */
    w32(96 + 0,  (0u << 3u) | 0u); /* child 0 = leaf 0 (type 0 = TRIANGLE) */
    w32(96 + 4,  0xFFFFFFFFu);
    w32(96 + 8,  0xFFFFFFFFu);
    w32(96 + 12, 0xFFFFFFFFu);
    wf(96 + 16, -2.0f);   /* min_x[0] */
    wf(96 + 32, -2.0f);   /* min_y[0] */
    wf(96 + 48,  4.0f);   /* min_z[0] */
    wf(96 + 64,  2.0f);   /* max_x[0] */
    wf(96 + 80,  2.0f);   /* max_y[0] */
    wf(96 + 96,  6.0f);   /* max_z[0] */
    w32(96 + 112, 1u);    /* child_count */

    /* TriLeafNode @ 224: triangle on plane z = 5.0 covering (-2,-2) to (2,2) */
    const float v0[3] = {-2.0f, -2.0f, 5.0f};
    const float v1[3] = { 2.0f, -2.0f, 5.0f};
    const float v2[3] = { 0.0f,  2.0f, 5.0f};
    for (int i = 0; i < 3; ++i) {
        wf(224 + 0  + i * 4, v0[i]);
        wf(224 + 12 + i * 4, v1[i]);
        wf(224 + 24 + i * 4, v2[i]);
    }
    w32(224 + 36, 7u); /* primitive_index */

    SpirvVmInput::RayTri tri{};
    std::memcpy(tri.v0, v0, 12);
    std::memcpy(tri.v1, v1, 12);
    std::memcpy(tri.v2, v2, 12);
    tri.prim_id = 7u;
    tri.inst_id = 0u;
    s.vm_tris.push_back(tri);
    return s;
}

void test_path_a_dxil_fast_path() {
    std::cout << "[TEST] Path A: DXIL Fast-Path (strip XBOX/XS2M + MD5 + bindings)..." << std::endl;
    std::vector<uint8_t> fake_bc = {'B', 'C', 0xC0, 0xDE, 1, 2, 3, 4, 5, 6, 7, 8};
    auto dxbc = pack_xbox_dxil_container(ShaderStage::Pixel, fake_bc, true, true);
    CHECK(detect_container_kind(dxbc.data(), dxbc.size()) == ContainerKind::DxbcDxil,
          "detect_container_kind should recognize DXBC");

    Rdna2Lifter lifter;
    LiftOptions opts{};
    auto res = lifter.lift_container(dxbc.data(), dxbc.size(), opts);
    CHECK(res.status == SCARLETT_OK, "Path A lift should succeed");
    CHECK(res.used_dxil_fast_path, "Path A flag must be set");
    CHECK(res.dxil_fast_path.stripped_xbox_chunks == 2u, "Should strip both XBOX and XS2M chunks");
    CHECK(res.dxil_fast_path.input_chunks == 4u && res.dxil_fast_path.output_chunks == 2u,
          "Chunk count should shrink from 4 to 2");
    CHECK(res.dxil_fast_path.stage == ShaderStage::Pixel, "Stage should be Pixel");
    CHECK(res.dxil_fast_path.remapped_bindings.size() == 2u, "Should extract 2 Xbox bindings");
    CHECK(!res.dxil_fast_path.clean_dxbc.empty() && !res.dxil_fast_path.dxil_bytecode.empty(),
          "Clean DXBC and DXIL bytecode must be non-empty");

    /* Re-running Path A on already-clean DXBC must strip 0 chunks and succeed */
    auto clean_res = extract_dxil_fast_path(res.dxil_fast_path.clean_dxbc.data(),
                                            res.dxil_fast_path.clean_dxbc.size());
    CHECK(clean_res.status == SCARLETT_OK && clean_res.stripped_xbox_chunks == 0u,
          "Idempotent re-strip of clean DXBC should succeed with 0 stripped");

    /* Negative: corrupt magic */
    uint8_t bad[32] = {0};
    auto bad_res = lifter.lift_container(bad, sizeof(bad), opts);
    CHECK(bad_res.status == SCARLETT_ERR_SHADER_INVALID_CONTAINER,
          "Corrupt container must return SCARLETT_ERR_SHADER_INVALID_CONTAINER");
}

void test_path_b_containers_and_full_isa_decoder() {
    std::cout << "[TEST] Path B: Containers (XS2/FSSM/PS09/SGD5) & all 12+ GFX10.3 ISA classes..." << std::endl;
    Rdna2Assembler a;
    /* 1. SOPK */
    a.sopk(sopk_op::S_MOVK_I32, 0, 42);
    a.sopk(sopk_op::S_ADDK_I32, 0, 8);
    /* 2. SOP2 */
    a.sop2(sop2_op::S_ADD_U32, 1, 0, reg_code::INLINE_INT_0 + 4);
    a.sop2(sop2_op::S_LSHL2_ADD_U32, 2, 1, 0);
    /* 3. SOP1 */
    a.sop1(sop1_op::S_MOV_B32, 3, 2);
    a.sop1(sop1_op::S_WQM_B64, 4, reg_code::EXEC_LO);
    a.sop1(sop1_op::S_BCNT1_I32_B64, 5, reg_code::VCC_LO);
    /* 4. SOPC */
    a.sopc(sopc_op::S_CMP_GT_U32, 1, 0);
    /* 5. SMEM */
    a.smem(smem_op::S_LOAD_DWORDX2, 6, 0, 16u);
    /* 6. VOP1 */
    a.vop1(vop1_op::V_CVT_F32_U32, 0, reg_code::VGPR_BASE + 0);
    a.vop1(vop1_op::V_FRACT_F32, 1, reg_code::VGPR_BASE + 0);
    a.vop1(vop1_op::V_READFIRSTLANE_B32, 8, reg_code::VGPR_BASE + 1);
    /* 7. VOP2 */
    a.vop2(vop2_op::V_ADD_F32, 2, reg_code::VGPR_BASE + 0, 1);
    a.vop2(vop2_op::V_FMAC_F32, 2, reg_code::INLINE_F_0_5, 1);
    a.vop2(vop2_op::V_MIN_F32, 3, reg_code::VGPR_BASE + 2, 1);
    /* 8. VOPC */
    a.vopc(vopc_op::V_CMP_GT_F32, reg_code::VGPR_BASE + 2, 1);
    /* 9. VOP3A */
    a.vop3a(vop3_op::V_FMA_F32, 4, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1, reg_code::VGPR_BASE + 2, true);
    a.vop3a(vop3_op::V_MED3_F32, 5, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1, reg_code::VGPR_BASE + 4);
    a.vop3a(vop3_op::V_READLANE_B32, 9, reg_code::VGPR_BASE + 4, reg_code::INLINE_INT_0 + 40);
    /* 10. VOP3B */
    a.vop3b(vop3_op::V_ADD_CO_U32, 6, 10, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1);
    /* 11. VOP3P (packed f16) */
    a.vop3p(vop3p_op::V_PK_FMA_F16, 7, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1, reg_code::VGPR_BASE + 2);
    a.vop3p(vop3p_op::V_DOT2_F32_F16, 8, reg_code::VGPR_BASE + 7, reg_code::VGPR_BASE + 7, reg_code::VGPR_BASE + 4);
    /* 12. VINTRP */
    a.vintrp(vintrp_op::V_INTERP_P1_F32, 9, 0, 0, 0);
    /* 13. DS (LDS + ds_bpermute_b32 + ds_swizzle_b32) */
    a.ds(ds_op::DS_WRITE_B32, 0, 0, 4);
    a.ds(ds_op::DS_READ_B32, 10, 0);
    a.ds(ds_op::DS_BPERMUTE_B32, 11, 0, 10);
    a.ds(ds_op::DS_SWIZZLE_B32, 12, 11, 0, 0, 0x1F, 0x80);
    /* 14. MUBUF & MTBUF */
    a.mubuf(mubuf_op::BUFFER_LOAD_DWORD, 13, 0, 0, 0u);
    a.mtbuf(mtbuf_op::TBUFFER_LOAD_FORMAT_X, 4u, 14, 0, 0, 0u);
    a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 12, 0, 0, 0u);
    /* 15. MIMG (including image_bvh_intersect_ray & image_bvh64_intersect_ray) */
    a.mimg(mimg_op::IMAGE_SAMPLE, 15, 0, 0, 0);
    a.mimg(mimg_op::IMAGE_BVH_INTERSECT_RAY, 16, 0, 1);
    a.mimg(mimg_op::IMAGE_BVH64_INTERSECT_RAY, 20, 0, 1);
    /* 16. FLAT / GLOBAL / SCRATCH */
    a.flat(flat_op::LOAD_DWORD, 0u, 24, 0, 0);
    a.flat(flat_op::LOAD_DWORD, 2u, 25, 0, 0);
    a.flat(flat_op::STORE_DWORD, 1u, 0, 0, 25);
    /* 17. EXP */
    a.exp(exp_target::MRT0, 4, 5, 8, 15, 0xFu, true);
    /* 18. SOPP */
    a.sopp(sopp_op::S_BARRIER);
    a.sopp(sopp_op::S_ENDPGM);

    std::vector<DecodedInst> decoded;
    uint32_t err_pc = 0;
    CHECK(decode_gfx103_stream(a.words().data(), a.words().size(), decoded, err_pc),
          "decode_gfx103_stream failed at pc=" << err_pc);

    bool seen[static_cast<size_t>(IsaClass::Count)]{};
    bool seen_bpermute = false, seen_swizzle = false, seen_bvh = false, seen_bvh64 = false;
    for (const auto& d : decoded) {
        seen[static_cast<size_t>(d.cls)] = true;
        if (d.cls == IsaClass::DS && d.opcode == ds_op::DS_BPERMUTE_B32) seen_bpermute = true;
        if (d.cls == IsaClass::DS && d.opcode == ds_op::DS_SWIZZLE_B32) seen_swizzle = true;
        if (d.cls == IsaClass::MIMG && d.opcode == mimg_op::IMAGE_BVH_INTERSECT_RAY) seen_bvh = true;
        if (d.cls == IsaClass::MIMG && d.opcode == mimg_op::IMAGE_BVH64_INTERSECT_RAY) seen_bvh64 = true;
    }

    const IsaClass required[] = {
        IsaClass::SOP1, IsaClass::SOP2, IsaClass::SOPC, IsaClass::SOPK, IsaClass::SOPP,
        IsaClass::SMEM, IsaClass::VOP1, IsaClass::VOP2, IsaClass::VOP3A, IsaClass::VOP3B,
        IsaClass::VOP3P, IsaClass::VOPC, IsaClass::VINTRP, IsaClass::DS,
        IsaClass::MUBUF, IsaClass::MTBUF, IsaClass::MIMG,
        IsaClass::FLAT, IsaClass::GLOBAL, IsaClass::SCRATCH, IsaClass::EXP
    };
    for (IsaClass c : required) {
        CHECK(seen[static_cast<size_t>(c)], "Missing decoded ISA class: " << isa_class_name(c));
    }
    CHECK(seen_bpermute && seen_swizzle && seen_bvh && seen_bvh64,
          "Must decode ds_bpermute_b32, ds_swizzle_b32, image_bvh_intersect_ray, image_bvh64_intersect_ray");

    /* Round-trip all 4 Path B containers + lift & spirv-val */
    Rdna2ShaderMetadata meta{};
    meta.stage = ShaderStage::Compute;
    meta.wave_size = 64u;
    meta.vgpr_count = 32u;
    meta.sgpr_count = 16u;
    meta.lds_bytes = 1024u;
    meta.workgroup_size[0] = 64u;
    meta.f32_denorm_ftz = true;

    Rdna2Lifter lifter;
    LiftOptions opts{};
    opts.gpu_profile = make_amd_radv_profile(true);
    opts.validate_with_spirv_tools = true;

    const ContainerKind kinds[] = {
        ContainerKind::XS2, ContainerKind::FSSM, ContainerKind::PS09, ContainerKind::SGD5
    };
    for (ContainerKind k : kinds) {
        auto blob = pack_rdna2_container(k, meta, a.words());
        CHECK(detect_container_kind(blob.data(), blob.size()) == k, "Container magic detection mismatch");
        auto lifted = lifter.lift_container(blob.data(), blob.size(), opts);
        CHECK(lifted.status == SCARLETT_OK,
              "Lift failed for container kind " << static_cast<int>(k) << ": " << lifted.diagnostics);
        CHECK(lifted.rt_strategy == RtStrategy::RayQueryKHR, "Expected RayQueryKHR on RADV");
        CHECK(lifted.wave_stats.chosen_strategy == WaveStrategy::NativeWave64,
              "Expected NativeWave64 on AMD RADV");
    }
}

void test_stage1_live_vulkan_microprobes_and_cache_key() {
    std::cout << "[TEST] Stage 1: Real Vulkan 1.4 GPU Microprobes & SHA-256 Pipeline Cache Key..." << std::endl;

    /* All 5 microprobe SPIR-V binaries MUST pass spirv-val --target-env vulkan1.4 */
    const std::vector<std::pair<const char*, std::vector<uint32_t>>> probes = {
        {"FMA (OpFmaKHR)", build_microprobe_fma_spirv(true)},
        {"FMA (GLSL Fma)", build_microprobe_fma_spirv(false)},
        {"Denorm FTZ",     build_microprobe_denorm_spirv(true)},
        {"Denorm Preserve",build_microprobe_denorm_spirv(false)},
        {"NaN MinMax",     build_microprobe_nan_minmax_spirv()},
        {"SubgroupSize",   build_microprobe_subgroup_size_spirv()},
        {"Float16 Packed", build_microprobe_f16_spirv()}
    };
    for (const auto& kv : probes) {
        std::string err;
        CHECK(validate_spirv_vulkan14(kv.second, &err),
              "Microprobe SPIR-V '" << kv.first << "' failed spirv-val: " << err);
    }

    /* Run real Vulkan 1.4 compute microprobes on live host GPU (GTX 1050 Ti) */
    GpuMicroprobeProfile live = run_vulkan_microprobes();
    std::cout << "  [LIVE GPU] " << live.device_name
              << " vendor=0x" << std::hex << live.vendor_id
              << " device=0x" << live.device_id
              << " subgroupSize=" << std::dec << live.measured_subgroup_size
              << " probe_bitmask=0x" << std::hex << live.probe_bitmask << std::dec
              << " fma_bits=0x" << std::hex << live.fma_probe_bits
              << " ftz_bits=0x" << live.denorm_ftz_probe_bits
              << " nan_min=0x" << live.nan_min_probe_bits << std::dec << std::endl;

    CHECK(live.vulkan_probed, "Live Vulkan 1.4 microprobe must execute on host GPU");
    CHECK(live.has(probe_bits::EXECUTED_ON_LIVE_VULKAN), "EXECUTED_ON_LIVE_VULKAN bit must be set");
    CHECK(live.has(probe_bits::FMA_SINGLE_ROUND_EXACT), "Host GPU FMA probe must verify single-rounding exactness");
    CHECK(live.has(probe_bits::F32_NAN_MINMAX_SUPPRESS), "Host GPU NMin/NMax probe must suppress NaN");
    CHECK(live.measured_subgroup_size == 32u || live.measured_subgroup_size == 64u,
          "Measured hardware subgroupSize must be 32 or 64");

    /* Verify SHA-256 pipeline cache key incorporates probe_bitmask */
    const uint8_t sample_shader[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    auto k1 = compute_pipeline_cache_key(live, SCARLETT_SHADER_MODE_ADAPTIVE,
                                         WaveStrategy::NarrowedWave32, RtStrategy::None,
                                         sample_shader, sizeof(sample_shader));
    GpuMicroprobeProfile mutated = live;
    mutated.probe_bitmask ^= probe_bits::F16_NATIVE_SUPPORTED;
    auto k2 = compute_pipeline_cache_key(mutated, SCARLETT_SHADER_MODE_ADAPTIVE,
                                         WaveStrategy::NarrowedWave32, RtStrategy::None,
                                         sample_shader, sizeof(sample_shader));
    CHECK(k1 != k2, "SHA-256 pipeline cache key MUST change when GPU probe bitmask changes");
    CHECK(k1.to_hex().size() == 64u, "SHA-256 hex digest must be 64 chars");
}

void test_stage2_known_fp_class_and_stage3_wave64() {
    std::cout << "[TEST] Stage 2 (computeKnownFPClass) & Stage 3 (Adaptive Wave64)..." << std::endl;

    /* Shader A: Wave32-safe compute shader with integer->f32 conversions (provably finite!) */
    Rdna2Assembler safe_asm;
    safe_asm.vop1(vop1_op::V_CVT_F32_U32, 0, reg_code::VGPR_BASE + 0);
    safe_asm.vop1(vop1_op::V_CVT_F32_U32, 1, reg_code::VGPR_BASE + 1);
    safe_asm.vop2(vop2_op::V_ADD_F32, 2, reg_code::VGPR_BASE + 0, 1);
    safe_asm.vop2(vop2_op::V_MIN_F32, 3, reg_code::VGPR_BASE + 2, 1);
    safe_asm.vop3a(vop3_op::V_MED3_F32, 4, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1, reg_code::VGPR_BASE + 3, true);
    safe_asm.mubuf(mubuf_op::BUFFER_STORE_DWORD, 4, 0, 0, 0u);
    safe_asm.sopp(sopp_op::S_ENDPGM);

    Rdna2ShaderMetadata meta{};
    meta.stage = ShaderStage::Compute;
    meta.wave_size = 64u;
    meta.workgroup_size[0] = 64u;
    meta.f32_denorm_ftz = true;

    Rdna2Lifter lifter;
    LiftOptions nv_adapt{};
    nv_adapt.mode = SCARLETT_SHADER_MODE_ADAPTIVE;
    nv_adapt.gpu_profile = make_nvidia_warp32_profile(false, true);
    nv_adapt.validate_with_spirv_tools = true;

    LiftOptions nv_exact = nv_adapt;
    nv_exact.mode = SCARLETT_SHADER_MODE_EXACT;

    auto r_adapt = lifter.lift_isa(meta, safe_asm.words().data(), safe_asm.words().size(), nv_adapt);
    auto r_exact = lifter.lift_isa(meta, safe_asm.words().data(), safe_asm.words().size(), nv_exact);
    CHECK(r_adapt.status == SCARLETT_OK && r_exact.status == SCARLETT_OK, "Wave32-safe lift failed");
    CHECK(!r_adapt.wave_stats.has_cross_half_wave_dep, "Wave32-safe shader should have no cross-half-wave deps");
    CHECK(r_adapt.wave_stats.chosen_strategy == WaveStrategy::NarrowedWave32,
          "NVIDIA adaptive must narrow Wave32-safe shader to Native Wave32");
    CHECK(r_exact.wave_stats.chosen_strategy == WaveStrategy::PairedWave64LdsEmulation,
          "Exact mode conservatively uses PairedWave64LdsEmulation");
    CHECK(r_adapt.fp_stats.proven_no_nan >= 3u && r_adapt.fp_stats.nan_trees_elided >= 2u,
          "computeKnownFPClass must prove non-NaN and elide NaN trees");
    CHECK(r_adapt.spirv_words.size() < r_exact.spirv_words.size(),
          "Adaptive SPIR-V must be strictly smaller than Exact SPIR-V");

    /* Shader B: Cross-half-wave Wave64 shader using ds_bpermute_b32 + v_readlane(40) + s_bcnt1_i32_b64 */
    Rdna2Assembler w64_asm;
    w64_asm.vopc(vopc_op::V_CMP_GT_U32, reg_code::VGPR_BASE + 0, 1);
    w64_asm.sop1(sop1_op::S_BCNT1_I32_B64, 2, reg_code::VCC_LO);
    w64_asm.vop3a(vop3_op::V_READLANE_B32, 3, reg_code::VGPR_BASE + 0, reg_code::INLINE_INT_0 + 40);
    w64_asm.ds(ds_op::DS_BPERMUTE_B32, 4, 1, 0);
    w64_asm.vop2(vop2_op::V_ADD_NC_U32, 5, 2, 4);
    w64_asm.vop2(vop2_op::V_ADD_NC_U32, 6, 3, 5);
    w64_asm.mubuf(mubuf_op::BUFFER_STORE_DWORD, 6, 0, 0, 0u);
    w64_asm.sopp(sopp_op::S_ENDPGM);

    auto r_nv_w64 = lifter.lift_isa(meta, w64_asm.words().data(), w64_asm.words().size(), nv_adapt);
    CHECK(r_nv_w64.status == SCARLETT_OK, "Wave64 cross-lane NVIDIA lift failed");
    CHECK(r_nv_w64.wave_stats.has_cross_half_wave_dep, "Must detect cross-half-wave deps");
    CHECK(r_nv_w64.wave_stats.uses_v_readlane && r_nv_w64.wave_stats.uses_ds_bpermute &&
          r_nv_w64.wave_stats.uses_bitcnt_b64,
          "Must flag v_readlane, ds_bpermute, s_bcnt1_i32_b64");
    CHECK(r_nv_w64.wave_stats.chosen_strategy == WaveStrategy::PairedWave64LdsEmulation,
          "NVIDIA must use PairedWave64LdsEmulation when cross-half-wave deps exist");

    LiftOptions amd_adapt = nv_adapt;
    amd_adapt.gpu_profile = make_amd_radv_profile(true);
    auto r_amd_w64 = lifter.lift_isa(meta, w64_asm.words().data(), w64_asm.words().size(), amd_adapt);
    CHECK(r_amd_w64.status == SCARLETT_OK, "Wave64 cross-lane AMD RADV lift failed");
    CHECK(r_amd_w64.wave_stats.chosen_strategy == WaveStrategy::NativeWave64,
          "AMD RADV must select NativeWave64 with zero LDS overhead");
    CHECK(r_amd_w64.spirv_words.size() * 2u < r_nv_w64.spirv_words.size(),
          "AMD RADV NativeWave64 must be >50% smaller than paired-lane LDS emulation");
}

void test_differential_adaptive_vs_exact_live_vulkan_and_vm() {
    std::cout << "[TEST] Differential Adaptive vs Exact (0 Discrepancies on Live Vulkan 1.4 GPU + 64-lane VM)..." << std::endl;
    GpuMicroprobeProfile live_gpu = run_vulkan_microprobes();
    Rdna2Lifter lifter;

    auto verify_case = [&](const char* name,
                           const Rdna2ShaderMetadata& meta,
                           const std::vector<uint32_t>& isa,
                           const std::vector<uint32_t>& in_buf,
                           bool run_on_live_gpu,
                           const SyntheticBvh4* bvh = nullptr) {
        /* 1. Live GPU exact vs adaptive verification (using real host GPU profile) */
        if (run_on_live_gpu && live_gpu.vulkan_probed) {
            LiftOptions opt_ex{};
            opt_ex.mode = SCARLETT_SHADER_MODE_EXACT;
            opt_ex.gpu_profile = live_gpu;
            opt_ex.prefer_ray_query = false;
            opt_ex.force_bda_bvh4 = (bvh != nullptr);
            opt_ex.validate_with_spirv_tools = true;

            LiftOptions opt_ad = opt_ex;
            opt_ad.mode = SCARLETT_SHADER_MODE_ADAPTIVE;

            auto lex = lifter.lift_isa(meta, isa.data(), isa.size(), opt_ex);
            auto lad = lifter.lift_isa(meta, isa.data(), isa.size(), opt_ad);
            CHECK(lex.status == SCARLETT_OK && lad.status == SCARLETT_OK,
                  "Live GPU lift failed for " << name << ": " << lex.diagnostics << " / " << lad.diagnostics);
            if (lex.status == SCARLETT_OK && lad.status == SCARLETT_OK) {
                const void* bda_ptr = bvh ? bvh->bytes.data() : nullptr;
                const size_t bda_sz = bvh ? bvh->bytes.size() : 0u;
                auto g_ex = run_spirv_compute_on_vulkan(lex.spirv_words, in_buf, 64u, 1u, bda_ptr, bda_sz);
                auto g_ad = run_spirv_compute_on_vulkan(lad.spirv_words, in_buf, 64u, 1u, bda_ptr, bda_sz);
                CHECK(g_ex.executed && g_ad.executed,
                      "Live Vulkan 1.4 dispatch failed for " << name << ": " << g_ex.error << " / " << g_ad.error);

                if (g_ex.executed && g_ad.executed) {
                    uint32_t diffs = 0u;
                    for (size_t i = 0; i < 64u; ++i) {
                        if (!words_equivalent(g_ex.output_words[i], g_ad.output_words[i])) {
                            ++diffs;
                            if (diffs <= 3u) {
                                std::cerr << "  [GPU DIFF] " << name << " idx=" << i
                                          << " exact=0x" << std::hex << g_ex.output_words[i]
                                          << " adaptive=0x" << g_ad.output_words[i] << std::dec << std::endl;
                            }
                        }
                    }
                    CHECK(diffs == 0u, "Live GPU exact vs adaptive buffer discrepancy on " << name << ": " << diffs);
                    if (bvh != nullptr) {
                        /* Ray origin (0,0,0) along +Z hits z=5.0 triangle -> exact hit_t = 5.0f */
                        const float hit_t = u2f(g_ad.output_words[0]);
                        CHECK(std::fabs(hit_t - 5.0f) < 1e-4f,
                              "Live GPU BDA BVH4 expected hit_t=5.0f, got " << hit_t);
                    }
                }
            }
        }

        /* 2. 64-lane SPIR-V VM differential exact vs adaptive across BOTH NVIDIA and AMD RADV profiles */
        const GpuMicroprobeProfile profiles[2] = {
            make_nvidia_warp32_profile(true, bvh == nullptr),
            make_amd_radv_profile(bvh == nullptr)
        };
        for (int pi = 0; pi < 2; ++pi) {
            LiftOptions o_ex{};
            o_ex.mode = SCARLETT_SHADER_MODE_EXACT;
            o_ex.gpu_profile = profiles[pi];
            o_ex.force_bda_bvh4 = (bvh != nullptr);
            o_ex.validate_with_spirv_tools = true;

            LiftOptions o_ad = o_ex;
            o_ad.mode = SCARLETT_SHADER_MODE_ADAPTIVE;

            auto lex = lifter.lift_isa(meta, isa.data(), isa.size(), o_ex);
            auto lad = lifter.lift_isa(meta, isa.data(), isa.size(), o_ad);
            CHECK(lex.status == SCARLETT_OK && lad.status == SCARLETT_OK,
                  "VM profile lift failed for " << name);

            SpirvVmInput vmin{};
            vmin.input_buffer = in_buf;
            vmin.output_word_count = 64u;
            vmin.dispatch_lanes = 64u;
            if (bvh != nullptr) {
                vmin.bda_memory = bvh->bytes.data();
                vmin.bda_memory_size = bvh->bytes.size();
                vmin.bda_base_va = 0x10000000ULL;
                if (vmin.input_buffer.size() < 2u) vmin.input_buffer.resize(2u, 0u);
                vmin.input_buffer[0] = 0x10000000u;
                vmin.input_buffer[1] = 0u;
                vmin.rt_triangles = bvh->vm_tris;
            }
            auto vex = evaluate_spirv_compute_vm(lex.spirv_words, vmin);
            auto vad = evaluate_spirv_compute_vm(lad.spirv_words, vmin);
            CHECK(vex.ok && vad.ok, "SPIR-V VM execution failed for " << name);

            uint32_t diffs = 0u;
            for (size_t i = 0; i < 64u; ++i) {
                if (!words_equivalent(vex.output_buffer[i], vad.output_buffer[i])) {
                    ++diffs;
                    if (diffs <= 3u) {
                        std::cerr << "  [VM DIFF] " << name << " prof=" << pi << " idx=" << i
                                  << " exact=0x" << std::hex << vex.output_buffer[i]
                                  << " adaptive=0x" << vad.output_buffer[i] << std::dec << std::endl;
                    }
                }
            }
            CHECK(diffs == 0u, "VM exact vs adaptive discrepancy on " << name << " prof=" << pi);
        }
    };

    Rdna2ShaderMetadata meta{};
    meta.stage = ShaderStage::Compute;
    meta.wave_size = 64u;
    meta.workgroup_size[0] = 64u;
    meta.f32_denorm_ftz = true;

    /* Build rich non-constant input buffer with normal, NaN, subnormal, negative, f16-packed values */
    std::vector<uint32_t> in_fp(256u, 0u);
    for (uint32_t l = 0; l < 64u; ++l) {
        in_fp[l * 4 + 0] = (l % 7u == 0u) ? 0x7FC00000u : f2u(static_cast<float>(l) * 0.25f - 4.0f);
        in_fp[l * 4 + 1] = (l % 5u == 0u) ? 0x00400000u : f2u(1.5f + static_cast<float>(l % 8u) * 0.5f);
        in_fp[l * 4 + 2] = f2u(3.25f - static_cast<float>(l % 6u) * 0.5f);
        in_fp[l * 4 + 3] = ((l + 3u) & 63u) << 2u; /* bpermute byte offset for lane */
    }

    /* Case 1: F32 ALU + FMA + NaN Min/Max/Med3 + Denormal FTZ */
    {
        Rdna2Assembler a;
        a.vop2(vop2_op::V_MIN_F32, 4, reg_code::VGPR_BASE + 0, 1);
        a.vop2(vop2_op::V_MAX_F32, 5, reg_code::VGPR_BASE + 1, 2);
        a.vop3a(vop3_op::V_FMA_F32, 6, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 2);
        a.vop3a(vop3_op::V_MED3_F32, 7, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 6);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 7, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);
        verify_case("f32_fma_nan_denorm_med3", meta, a.words(), in_fp, true);
    }

    /* Case 2: Packed F16 (VOP3P v_pk_add_f16 / v_pk_mul_f16 / v_pk_min_f16) */
    {
        std::vector<uint32_t> in_f16(256u, 0x3C003800u); /* {0.5h, 1.0h} */
        for (uint32_t l = 0; l < 64u; ++l) {
            in_f16[l * 4 + 0] = 0x40003C00u; /* {1.0h, 2.0h} */
            in_f16[l * 4 + 1] = 0x3E003800u; /* {0.5h, 1.5h} */
            in_f16[l * 4 + 2] = 0x42004000u; /* {2.0h, 3.0h} */
        }
        Rdna2Assembler a;
        a.vop3p(vop3p_op::V_PK_ADD_F16, 4, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1);
        a.vop3p(vop3p_op::V_PK_MUL_F16, 5, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 2);
        a.vop3p(vop3p_op::V_PK_MIN_F16, 6, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 2);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 6, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);
        verify_case("f16_packed_vop3p", meta, a.words(), in_f16, true);
    }

    /* Case 3: Wave64 Cross-Half-Wave (ds_bpermute + v_readlane(45) + s_bcnt1_i32_b64) */
    {
        std::vector<uint32_t> in_w64(256u, 0u);
        for (uint32_t l = 0; l < 64u; ++l) {
            in_w64[l * 4 + 0] = 100u + l * 7u;
            in_w64[l * 4 + 1] = ((l + 33u) & 63u) << 2u; /* cross-half byte offset! */
            in_w64[l * 4 + 2] = 120u;
        }
        Rdna2Assembler a;
        a.vopc(vopc_op::V_CMP_GT_U32, reg_code::VGPR_BASE + 0, 2);
        a.sop1(sop1_op::S_BCNT1_I32_B64, 4, reg_code::VCC_LO);
        a.vop3a(vop3_op::V_READLANE_B32, 5, reg_code::VGPR_BASE + 0, reg_code::INLINE_INT_0 + 45);
        a.ds(ds_op::DS_BPERMUTE_B32, 6, 1, 0);
        a.vop2(vop2_op::V_ADD_NC_U32, 7, 4, 6);
        a.vop2(vop2_op::V_ADD_NC_U32, 8, 5, 7);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 8, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);
        verify_case("wave64_cross_half_lane", meta, a.words(), in_w64, true);
    }

    /* Case 4: Stage 4 BVH4 RT (PhysicalStorageBuffer64 BDA 128B/64B traversal on Live GTX 1050 Ti + VM!) */
    {
        auto bvh = build_synthetic_bvh4_scene();
        std::vector<uint32_t> in_ray(256u, 0u);
        /* Slot [0..1] overwritten with BDA VA; lanes read v4..v7 = (ox, oy, oz, tmax)
         * For lane 0..63, v0..v3 come from lane*4 + 0..3; we use v4..v7 with explicit literal/moves */
        Rdna2Assembler a;
        /* Set ray origin = (0.0, 0.0, 0.0), tmax = 100.0f in v8..v11 */
        a.vop1(vop1_op::V_MOV_B32, 8,  reg_code::INLINE_INT_0 + 0); /* ox = 0.0f */
        a.vop1(vop1_op::V_MOV_B32, 9,  reg_code::INLINE_INT_0 + 0); /* oy = 0.0f */
        a.vop1(vop1_op::V_MOV_B32, 10, reg_code::INLINE_INT_0 + 0); /* oz = 0.0f */
        a.vop1(vop1_op::V_MOV_B32, 11, reg_code::LITERAL_32, true, f2u(100.0f));
        a.mimg(mimg_op::IMAGE_BVH64_INTERSECT_RAY, 12, 8, 1);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 12, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);

        Rdna2ShaderMetadata rt_meta = meta;
        rt_meta.uses_rt_bvh4 = true;
        verify_case("bvh4_bda_ray_traversal", rt_meta, a.words(), in_ray, true, &bvh);

        /* Also test VK_KHR_ray_query path on VM & verify exact == adaptive == 5.0f! */
        LiftOptions rq_ex{};
        rq_ex.mode = SCARLETT_SHADER_MODE_EXACT;
        rq_ex.gpu_profile = make_amd_radv_profile(true);
        rq_ex.prefer_ray_query = true;
        rq_ex.validate_with_spirv_tools = true;
        LiftOptions rq_ad = rq_ex;
        rq_ad.mode = SCARLETT_SHADER_MODE_ADAPTIVE;

        auto lex = lifter.lift_isa(rt_meta, a.words().data(), a.words().size(), rq_ex);
        auto lad = lifter.lift_isa(rt_meta, a.words().data(), a.words().size(), rq_ad);
        CHECK(lex.status == SCARLETT_OK && lad.status == SCARLETT_OK, "RayQueryKHR lift failed");
        CHECK(lad.rt_strategy == RtStrategy::RayQueryKHR, "Must select RayQueryKHR when GPU supports it");

        SpirvVmInput vmin{};
        vmin.input_buffer = in_ray;
        vmin.output_word_count = 64u;
        vmin.dispatch_lanes = 64u;
        vmin.rt_triangles = bvh.vm_tris;
        auto vex = evaluate_spirv_compute_vm(lex.spirv_words, vmin);
        auto vad = evaluate_spirv_compute_vm(lad.spirv_words, vmin);
        CHECK(vex.ok && vad.ok && vex.output_buffer == vad.output_buffer,
              "RayQueryKHR exact vs adaptive VM buffers must match bit-for-bit");
        CHECK(std::fabs(u2f(vad.output_buffer[0]) - 5.0f) < 1e-4f,
              "RayQueryKHR hit_t must equal 5.0f (matching BDA BVH4 hit_t!)");
    }
}

void test_c_abi_bridge() {
    std::cout << "[TEST] C ABI Bridge, SPIR-V 1.5, fast_f32 & PROTON_XBOX_SHADER_MODE..." << std::endl;
    CHECK(parse_shader_mode_env("exact") == SCARLETT_SHADER_MODE_EXACT, "parse exact");
    CHECK(parse_shader_mode_env("adaptive") == SCARLETT_SHADER_MODE_ADAPTIVE, "parse adaptive");
    CHECK(parse_shader_mode_env("fast_f32") == SCARLETT_SHADER_MODE_FAST_F32, "parse fast_f32");
    CHECK(parse_shader_mode_env("") == SCARLETT_SHADER_MODE_ADAPTIVE, "default adaptive");

    Rdna2Assembler a;
    a.vop2(vop2_op::V_ADD_F32, 2, reg_code::VGPR_BASE + 0, 1);
    a.vop3a(vop3_op::V_FMA_F32, 3, reg_code::VGPR_BASE + 2, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1);
    a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 3, 0, 0, 0u);
    a.sopp(sopp_op::S_ENDPGM);

    Rdna2ShaderMetadata meta{};
    meta.stage = ShaderStage::Compute;
    meta.wave_size = 64u;
    auto xs2 = pack_rdna2_container(ContainerKind::XS2, meta, a.words());

    /* Verify SPIR-V 1.5 + fast_f32 mode */
    Rdna2Lifter lifter;
    LiftOptions opt15{};
    opt15.mode = SCARLETT_SHADER_MODE_FAST_F32;
    opt15.spirv_version = SpirvTargetVersion::Spirv15;
    opt15.gpu_profile = make_nvidia_warp32_profile(false, false);
    opt15.validate_with_spirv_tools = true;
    auto r15 = lifter.lift_container(xs2.data(), xs2.size(), opt15);
    CHECK(r15.status == SCARLETT_OK && r15.spirv_words.size() > 10u && r15.spirv_words[1] == 0x00010500u,
          "SPIR-V 1.5 fast_f32 lift must succeed with version 0x00010500");

    scarlett_rdna2_lift_c_result_t c_res{};
    std::vector<uint32_t> spv_buf(4096u, 0u);
    auto hr = scarlett_rdna2_lift_shader_c(xs2.data(), xs2.size(),
                                           SCARLETT_SHADER_MODE_ADAPTIVE, 1u,
                                           spv_buf.data(), spv_buf.size(), &c_res);
    CHECK(hr == SCARLETT_OK && c_res.status == SCARLETT_OK, "C ABI lift failed");
    CHECK(c_res.used_dxil_fast_path == 0u && c_res.word_count > 10u, "C ABI must return valid SPIR-V words");
    spv_buf.resize(c_res.word_count);
    std::string err;
    CHECK(validate_spirv_vulkan14(spv_buf, &err), "C ABI emitted SPIR-V failed spirv-val: " << err);
}

} // namespace

int main() {
    test_path_a_dxil_fast_path();
    test_path_b_containers_and_full_isa_decoder();
    test_stage1_live_vulkan_microprobes_and_cache_key();
    test_stage2_known_fp_class_and_stage3_wave64();
    test_differential_adaptive_vs_exact_live_vulkan_and_vm();
    test_c_abi_bridge();

    if (g_failures == 0) {
        std::cout << "[PASS] All rdna2_lifter unit & live Vulkan 1.4 integration tests PASSED (0 failures)." << std::endl;
        return 0;
    }
    std::cerr << "[FAIL] rdna2_lifter tests completed with " << g_failures << " failure(s)." << std::endl;
    return 1;
}
