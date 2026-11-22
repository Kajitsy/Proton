#include "arena_scenes.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "rdna2_lifter/rdna2_lifter.hpp"

namespace scarlett::arena {

namespace {

uint32_t f2u(float f) noexcept {
    uint32_t u = 0u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

float u2f(uint32_t u) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

bool words_equivalent(uint32_t a, uint32_t b) noexcept {
    if (a == b) return true;
    const float fa = u2f(a);
    const float fb = u2f(b);
    if (std::isnan(fa) && std::isnan(fb)) return true;
    if (fa == 0.0f && fb == 0.0f) return true;
    return false;
}

const char* wave_strategy_name(scarlett::rdna2::WaveStrategy s) noexcept {
    switch (s) {
        case scarlett::rdna2::WaveStrategy::NativeWave64:            return "NativeWave64";
        case scarlett::rdna2::WaveStrategy::NarrowedWave32:          return "NarrowedWave32";
        case scarlett::rdna2::WaveStrategy::PairedWave64LdsEmulation: return "PairedWave64LdsEmulation";
        default:                                                     return "Unknown";
    }
}

bool run_external_spirv_val(const std::vector<uint32_t>& words) {
    const std::filesystem::path spv_path =
        std::filesystem::temp_directory_path() /
        ("scarlett_arena_probe_" + std::to_string(::getpid()) + ".spv");
    {
        std::ofstream ofs(spv_path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs.write(reinterpret_cast<const char*>(words.data()),
                  static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
        if (!ofs.good()) return false;
    }

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("spirv-val", "spirv-val", "--target-env", "vulkan1.4", spv_path.c_str(), nullptr);
        ::_exit(127);
    }
    int status = 0;
    const pid_t w = ::waitpid(pid, &status, 0);
    std::error_code ec;
    std::filesystem::remove(spv_path, ec);
    return w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace

bool run_scene2_rdna2_lifter(ArenaTelemetry& tel) {
    using namespace scarlett::rdna2;
    SceneChecker sc(tel, tel.s2.checks, "Scene2_RDNA2");

    /* 1. Live Vulkan 1.4 GPU Microprobes strictly via VkBuffer */
    const GpuMicroprobeProfile live_gpu = run_vulkan_microprobes();
    ARENA_CHECK(sc, live_gpu.vulkan_probed);
    ARENA_CHECK(sc, live_gpu.has(probe_bits::EXECUTED_ON_LIVE_VULKAN));
    ARENA_CHECK(sc, live_gpu.measured_subgroup_size == 32u || live_gpu.measured_subgroup_size == 64u);
    tel.s2.live_vulkan_probed = live_gpu.vulkan_probed;
    tel.s2.gpu_device_name = live_gpu.device_name;

    Rdna2Lifter lifter;
    uint32_t spv_val_ok = 0u;
    uint32_t spv_val_total = 0u;
    uint32_t gpu_diffs = 0u;
    uint32_t vm_diffs = 0u;
    bool cli_spirv_val_checked = false;

    auto verify_differential = [&](const char* name,
                                   const Rdna2ShaderMetadata& meta,
                                   const std::vector<uint32_t>& isa,
                                   const std::vector<uint32_t>& in_buf) -> LiftResult {
        LiftOptions opt_ex{};
        opt_ex.mode = SCARLETT_SHADER_MODE_EXACT;
        opt_ex.gpu_profile = live_gpu;
        opt_ex.prefer_ray_query = false;
        opt_ex.validate_with_spirv_tools = true;

        LiftOptions opt_ad = opt_ex;
        opt_ad.mode = SCARLETT_SHADER_MODE_ADAPTIVE;

        LiftResult lex = lifter.lift_isa(meta, isa.data(), isa.size(), opt_ex);
        LiftResult lad = lifter.lift_isa(meta, isa.data(), isa.size(), opt_ad);
        ARENA_CHECK(sc, lex.status == SCARLETT_OK && lad.status == SCARLETT_OK);

        ++spv_val_total;
        if (validate_spirv_vulkan14(lex.spirv_words)) {
            ++spv_val_ok;
        }
        ++spv_val_total;
        if (validate_spirv_vulkan14(lad.spirv_words)) {
            ++spv_val_ok;
        }
        if (!cli_spirv_val_checked) {
            cli_spirv_val_checked = true;
            ARENA_CHECK(sc, run_external_spirv_val(lad.spirv_words));
        }

        auto g_ex = run_spirv_compute_on_vulkan(lex.spirv_words, in_buf, 64u, 1u);
        auto g_ad = run_spirv_compute_on_vulkan(lad.spirv_words, in_buf, 64u, 1u);
        if (!g_ex.executed || !g_ad.executed) {
            if (g_ex.error.find("DEVICE_LOST") != std::string::npos ||
                g_ad.error.find("DEVICE_LOST") != std::string::npos) {
                ++tel.vk_error_device_lost_count;
            }
        }
        ARENA_CHECK(sc, g_ex.executed && g_ad.executed);
        if (g_ex.executed && g_ad.executed) {
            for (size_t i = 0u; i < 64u; ++i) {
                if (!words_equivalent(g_ex.output_words[i], g_ad.output_words[i])) {
                    ++gpu_diffs;
                }
            }
        }

        const GpuMicroprobeProfile profiles[2] = {
            make_nvidia_warp32_profile(true, true),
            make_amd_radv_profile(true),
        };
        for (const auto& prof : profiles) {
            LiftOptions o_ex{};
            o_ex.mode = SCARLETT_SHADER_MODE_EXACT;
            o_ex.gpu_profile = prof;
            o_ex.validate_with_spirv_tools = true;
            LiftOptions o_ad = o_ex;
            o_ad.mode = SCARLETT_SHADER_MODE_ADAPTIVE;

            auto px = lifter.lift_isa(meta, isa.data(), isa.size(), o_ex);
            auto pa = lifter.lift_isa(meta, isa.data(), isa.size(), o_ad);
            ARENA_CHECK(sc, px.status == SCARLETT_OK && pa.status == SCARLETT_OK);
            spv_val_total += 2u;
            if (validate_spirv_vulkan14(px.spirv_words)) ++spv_val_ok;
            if (validate_spirv_vulkan14(pa.spirv_words)) ++spv_val_ok;

            SpirvVmInput vmin{};
            vmin.input_buffer = in_buf;
            vmin.output_word_count = 64u;
            vmin.dispatch_lanes = 64u;
            auto vex = evaluate_spirv_compute_vm(px.spirv_words, vmin);
            auto vad = evaluate_spirv_compute_vm(pa.spirv_words, vmin);
            ARENA_CHECK(sc, vex.ok && vad.ok);
            if (vex.ok && vad.ok) {
                for (size_t i = 0u; i < 64u; ++i) {
                    if (!words_equivalent(vex.output_buffer[i], vad.output_buffer[i])) {
                        ++vm_diffs;
                    }
                }
            }
        }
        (void)name;
        return lad;
    };

    Rdna2ShaderMetadata meta{};
    meta.stage = ShaderStage::Compute;
    meta.wave_size = 64u;
    meta.workgroup_size[0] = 64u;
    meta.f32_denorm_ftz = true;

    /* Case 1: F32 NaN + denormals + FMA + MED3 */
    {
        std::vector<uint32_t> in_fp(256u, 0u);
        for (uint32_t l = 0u; l < 64u; ++l) {
            in_fp[l * 4u + 0u] = (l % 7u == 0u) ? 0x7FC00000u : f2u(static_cast<float>(l) * 0.25f - 4.0f);
            in_fp[l * 4u + 1u] = (l % 5u == 0u) ? 0x00400000u : f2u(1.5f + static_cast<float>(l % 8u) * 0.5f);
            in_fp[l * 4u + 2u] = f2u(3.25f - static_cast<float>(l % 6u) * 0.5f);
            in_fp[l * 4u + 3u] = ((l + 3u) & 63u) << 2u;
        }
        Rdna2Assembler a;
        a.vop2(vop2_op::V_MIN_F32, 4, reg_code::VGPR_BASE + 0, 1);
        a.vop2(vop2_op::V_MAX_F32, 5, reg_code::VGPR_BASE + 1, 2);
        a.vop3a(vop3_op::V_FMA_F32, 6, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 2);
        a.vop3a(vop3_op::V_MED3_F32, 7, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 6);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 7, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);
        verify_differential("f32_nan_denorm_med3", meta, a.words(), in_fp);
        tel.s2.f32_nan_denorm_ok = true;
    }

    /* Case 2: Packed f16 (VOP3P) */
    {
        std::vector<uint32_t> in_f16(256u, 0x3C003800u);
        for (uint32_t l = 0u; l < 64u; ++l) {
            in_f16[l * 4u + 0u] = 0x40003C00u;
            in_f16[l * 4u + 1u] = 0x3E003800u;
            in_f16[l * 4u + 2u] = 0x42004000u;
        }
        Rdna2Assembler a;
        a.vop3p(vop3p_op::V_PK_ADD_F16, 4, reg_code::VGPR_BASE + 0, reg_code::VGPR_BASE + 1);
        a.vop3p(vop3p_op::V_PK_MUL_F16, 5, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 2);
        a.vop3p(vop3p_op::V_PK_MIN_F16, 6, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 2);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 6, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);
        verify_differential("f16_packed_vop3p", meta, a.words(), in_f16);
        tel.s2.f16_packed_ok = true;
    }

    /* Case 3: Wave64 Cross-Half-Wave (ds_bpermute + v_readlane(45) + s_bcnt1_i32_b64) */
    {
        std::vector<uint32_t> in_w64(256u, 0u);
        for (uint32_t l = 0u; l < 64u; ++l) {
            in_w64[l * 4u + 0u] = 100u + l * 7u;
            in_w64[l * 4u + 1u] = ((l + 33u) & 63u) << 2u;
            in_w64[l * 4u + 2u] = 120u;
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

        verify_differential("wave64_cross_lane", meta, a.words(), in_w64);

        LiftOptions nv_opt{};
        nv_opt.mode = SCARLETT_SHADER_MODE_ADAPTIVE;
        nv_opt.gpu_profile = make_nvidia_warp32_profile(true, true);
        nv_opt.validate_with_spirv_tools = true;
        auto r_nv = lifter.lift_isa(meta, a.words().data(), a.words().size(), nv_opt);
        ARENA_CHECK(sc, r_nv.wave_stats.has_cross_half_wave_dep);
        ARENA_CHECK(sc, r_nv.wave_stats.chosen_strategy == WaveStrategy::PairedWave64LdsEmulation);
        tel.s2.wave64_cross_lane_strategy = wave_strategy_name(r_nv.wave_stats.chosen_strategy);
    }

    /* Case 4: Wave32-safe + computeKnownFPClass elision */
    {
        std::vector<uint32_t> in_u(256u, 0u);
        for (uint32_t l = 0u; l < 64u; ++l) {
            in_u[l * 4u + 0u] = l + 1u;
            in_u[l * 4u + 1u] = (l % 13u) + 2u;
        }
        Rdna2Assembler a;
        a.vop1(vop1_op::V_CVT_F32_U32, 4, reg_code::VGPR_BASE + 0);
        a.vop1(vop1_op::V_CVT_F32_U32, 5, reg_code::VGPR_BASE + 1);
        a.vop1(vop1_op::V_FRACT_F32, 6, reg_code::VGPR_BASE + 4);
        a.vop2(vop2_op::V_MIN_F32, 7, reg_code::VGPR_BASE + 4, 6);
        a.vop2(vop2_op::V_MAX_F32, 8, reg_code::VGPR_BASE + 5, 7);
        a.vop3a(vop3_op::V_FMA_F32, 9, reg_code::VGPR_BASE + 7, reg_code::VGPR_BASE + 8, reg_code::VGPR_BASE + 6);
        a.vop3a(vop3_op::V_MED3_F32, 10, reg_code::VGPR_BASE + 7, reg_code::VGPR_BASE + 8, reg_code::VGPR_BASE + 9);
        a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 10, 0, 0, 0u);
        a.sopp(sopp_op::S_ENDPGM);

        verify_differential("wave32_safe_known_fp", meta, a.words(), in_u);

        LiftOptions nv_opt{};
        nv_opt.mode = SCARLETT_SHADER_MODE_ADAPTIVE;
        nv_opt.gpu_profile = make_nvidia_warp32_profile(true, true);
        nv_opt.validate_with_spirv_tools = true;
        auto r_nv = lifter.lift_isa(meta, a.words().data(), a.words().size(), nv_opt);
        ARENA_CHECK(sc, !r_nv.wave_stats.has_cross_half_wave_dep);
        ARENA_CHECK(sc, r_nv.wave_stats.chosen_strategy == WaveStrategy::NarrowedWave32);
        ARENA_CHECK(sc, r_nv.fp_stats.proven_no_nan > 0u);
        ARENA_CHECK(sc, r_nv.fp_stats.nan_trees_elided > 0u);
        tel.s2.wave32_safe_strategy = wave_strategy_name(r_nv.wave_stats.chosen_strategy);
        tel.s2.known_fp_nan_trees_elided = r_nv.fp_stats.nan_trees_elided;
    }

    ARENA_CHECK(sc, gpu_diffs == 0u);
    ARENA_CHECK(sc, vm_diffs == 0u);
    tel.s2.exact_vs_adaptive_gpu_diffs = gpu_diffs;
    tel.s2.exact_vs_adaptive_vm_diffs = vm_diffs;

    /* 5. Measure SPIR-V code reduction (>= 25% NVIDIA, >= 50% AMD RADV) across 64 diverse shaders */
    uint64_t nv_exact_words = 0u;
    uint64_t nv_adapt_words = 0u;
    uint64_t radv_exact_words = 0u;
    uint64_t radv_adapt_words = 0u;
    std::vector<double> lift_times_ms;
    lift_times_ms.reserve(256u);

    const GpuMicroprobeProfile nv_prof = make_nvidia_warp32_profile(true, true);
    const GpuMicroprobeProfile radv_prof = make_amd_radv_profile(true);
    const ContainerKind kinds[4] = {
        ContainerKind::XS2, ContainerKind::FSSM, ContainerKind::PS09, ContainerKind::SGD5
    };

    for (uint32_t i = 0u; i < 64u; ++i) {
        Rdna2ShaderMetadata sm{};
        sm.container_kind = kinds[i % 4u];
        sm.stage = (i % 5u == 0u) ? ShaderStage::Pixel : ShaderStage::Compute;
        sm.wave_size = 64u;
        sm.workgroup_size[0] = 64u;
        sm.f32_denorm_ftz = true;

        Rdna2Assembler a;
        a.vop1(vop1_op::V_CVT_F32_U32, 4, reg_code::VGPR_BASE + 0);
        a.vop1(vop1_op::V_CVT_F32_U32, 5, reg_code::VGPR_BASE + 1);
        a.vop1(vop1_op::V_FRACT_F32, 6, reg_code::VGPR_BASE + 4);
        for (uint32_t k = 0u; k < 8u; ++k) {
            const uint8_t dst = static_cast<uint8_t>(7u + (k % 6u));
            switch ((i + k) % 5u) {
                case 0: a.vop2(vop2_op::V_MIN_F32, dst, reg_code::VGPR_BASE + 4, 5); break;
                case 1: a.vop2(vop2_op::V_MAX_F32, dst, reg_code::VGPR_BASE + 5, 6); break;
                case 2: a.vop3a(vop3_op::V_FMA_F32, dst, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 6); break;
                case 3: a.vop3a(vop3_op::V_MED3_F32, dst, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5, reg_code::VGPR_BASE + 6); break;
                default: a.vop3p(vop3p_op::V_PK_ADD_F16, dst, reg_code::VGPR_BASE + 4, reg_code::VGPR_BASE + 5); break;
            }
        }
        if ((i % 4u) == 0u) {
            a.vopc(vopc_op::V_CMP_GT_U32, reg_code::VGPR_BASE + 0, 1);
            a.sop1(sop1_op::S_BCNT1_I32_B64, 2, reg_code::VCC_LO);
        }
        if (sm.stage == ShaderStage::Pixel) {
            a.exp(0u, 7u, 8u, 9u, 10u);
        } else {
            a.mubuf(mubuf_op::BUFFER_STORE_DWORD, 7u, 0u, 0u, 0u);
        }
        a.sopp(sopp_op::S_ENDPGM);

        auto blob = pack_rdna2_container(sm.container_kind, sm, a.words());

        auto lift_timed = [&](const GpuMicroprobeProfile& prof, scarlett_shader_mode_t mode) -> LiftResult {
            LiftOptions opt{};
            opt.mode = mode;
            opt.gpu_profile = prof;
            opt.validate_with_spirv_tools = true;
            const auto t0 = std::chrono::steady_clock::now();
            LiftResult r = lifter.lift_container(blob.data(), blob.size(), opt);
            const auto t1 = std::chrono::steady_clock::now();
            lift_times_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            return r;
        };

        auto nv_ex = lift_timed(nv_prof, SCARLETT_SHADER_MODE_EXACT);
        auto nv_ad = lift_timed(nv_prof, SCARLETT_SHADER_MODE_ADAPTIVE);
        auto rd_ex = lift_timed(radv_prof, SCARLETT_SHADER_MODE_EXACT);
        auto rd_ad = lift_timed(radv_prof, SCARLETT_SHADER_MODE_ADAPTIVE);

        ARENA_CHECK(sc, nv_ex.status == SCARLETT_OK && nv_ad.status == SCARLETT_OK &&
                        rd_ex.status == SCARLETT_OK && rd_ad.status == SCARLETT_OK);
        spv_val_total += 4u;
        if (validate_spirv_vulkan14(nv_ex.spirv_words)) ++spv_val_ok;
        if (validate_spirv_vulkan14(nv_ad.spirv_words)) ++spv_val_ok;
        if (validate_spirv_vulkan14(rd_ex.spirv_words)) ++spv_val_ok;
        if (validate_spirv_vulkan14(rd_ad.spirv_words)) ++spv_val_ok;

        nv_exact_words += nv_ex.spirv_words.size();
        nv_adapt_words += nv_ad.spirv_words.size();
        radv_exact_words += rd_ex.spirv_words.size();
        radv_adapt_words += rd_ad.spirv_words.size();
    }

    const double nv_red = (1.0 - static_cast<double>(nv_adapt_words) / static_cast<double>(nv_exact_words)) * 100.0;
    const double radv_red = (1.0 - static_cast<double>(radv_adapt_words) / static_cast<double>(radv_exact_words)) * 100.0;
    std::sort(lift_times_ms.begin(), lift_times_ms.end());
    const size_t p99_idx = std::min(lift_times_ms.size() - 1u,
                                    static_cast<size_t>(std::ceil(lift_times_ms.size() * 0.99)) - 1u);

    tel.s2.spirv_val_passed = spv_val_ok;
    tel.s2.spirv_val_total = spv_val_total;
    tel.s2.nvidia_reduction_pct = nv_red;
    tel.s2.radv_reduction_pct = radv_red;
    tel.s2.cold_lift_p99_ms = lift_times_ms[p99_idx];

    ARENA_CHECK(sc, spv_val_total > 0u && spv_val_ok == spv_val_total);
    ARENA_CHECK(sc, nv_red >= 25.0);
    ARENA_CHECK(sc, radv_red >= 50.0);
    ARENA_CHECK(sc, tel.s2.cold_lift_p99_ms < 200.0);

    tel.s2.passed = sc.ok();
    return tel.s2.passed;
}

} // namespace scarlett::arena
