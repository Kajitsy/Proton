#ifndef SCARLETT_ARENA_TELEMETRY_HPP
#define SCARLETT_ARENA_TELEMETRY_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"

namespace scarlett::arena {

struct ArenaTelemetry {
    std::string model_name = "anaconda";
    scarlett_console_model_t model = SCARLETT_MODEL_ANACONDA;
    uint64_t memory_budget_bytes = SCARLETT_BUDGET_ANACONDA_BYTES;
    std::string package_dir;
    std::string bcpack_plugin_path;

    uint32_t total_checks = 0u;
    uint32_t failed_checks = 0u;
    uint32_t vk_error_device_lost_count = 0u;

    /* Scene 1: UMA Mid-Frame CPU->GPU & Aliasing */
    struct {
        bool passed = false;
        uint32_t checks = 0u;
        uint64_t model_budget_bytes = 0u;
        bool budget_exceeded_enforced = false;
        bool ro_rw_aliasing_ok = false;
        bool cache_conflict_rejected = false;
        bool cross_alloc_rejected = false;
        uint32_t uma_tier = 0u;
        uint64_t uma_pre_submit_flushes = 0u;
        uint64_t uma_present_flushes = 0u;
        bool zero_frame_lag_verified = false;
    } s1;

    /* Scene 2: RDNA2 ISA + Wave64 + f16/f32 NaN */
    struct {
        bool passed = false;
        uint32_t checks = 0u;
        bool live_vulkan_probed = false;
        std::string gpu_device_name;
        uint32_t spirv_val_passed = 0u;
        uint32_t spirv_val_total = 0u;
        uint32_t exact_vs_adaptive_gpu_diffs = 0u;
        uint32_t exact_vs_adaptive_vm_diffs = 0u;
        std::string wave64_cross_lane_strategy;
        std::string wave32_safe_strategy;
        bool f16_packed_ok = false;
        bool f32_nan_denorm_ok = false;
        uint32_t known_fp_nan_trees_elided = 0u;
        double nvidia_reduction_pct = 0.0;
        double radv_reduction_pct = 0.0;
        double cold_lift_p99_ms = 0.0;
    } s2;

    /* Scene 3: DirectStorage 512 MiB Zlib/GDeflate + Swizzle in Physical Pages */
    struct {
        bool passed = false;
        uint32_t checks = 0u;
        double payload_mib = 512.0;
        bool io_uring_active = false;
        double dstorage_gbps = 0.0;
        uint32_t pfn_swizzle_modes_verified = 0u;
        bool pfn_swizzle_crc_ok = false;
        bool bcpack_missing_plugin_rejected = false;
        uint64_t bcpack_plugin_decoded_bytes = 0u;
        bool vkdeflate_spirv_valid = false;
        bool swizzle_spirv_valid = false;
    } s3;

    /* Scene 4: DX12 Ultimate & AAA Middleware */
    struct {
        bool passed = false;
        uint32_t checks = 0u;
        uint32_t bvh4_triangles = 0u;
        uint32_t bvh4_ray_hits_verified = 0u;
        bool bvh4_vk_accel_mapped = false;
        uint32_t vsm_input_tiles = 0u;
        uint32_t vsm_coalesced_image_binds = 0u;
        uint32_t vsm_coalesced_memory_binds = 0u;
        uint32_t sfs_log2_mip_region = 0u;
        bool ngg_mesh_amplification_ok = false;
        bool vrs_supported = false;
        bool nanite_int64_atomics_ok = false;
        bool xg_above_4gib_40bit_ok = false;
        uint32_t xg_swizzle_formats_verified = 0u;
        bool bink2_bt709_ok = false;
        std::string fsr_version = "0x000000";
        uint32_t pixevt_markers_recorded = 0u;
        bool desc_nvidia_610_guard_ok = false;
    } s4;

    /* Scene 5: Dual-Plane HDR10 PresentX + 60 Hz Pacing + 10x Alt+Enter/SuspendX */
    struct {
        bool passed = false;
        uint32_t checks = 0u;
        bool hdr10_pq_colorspace_ok = false;
        bool gamedvr_plane1_captured = false;
        uint32_t frames_paced = 0u;
        double frametime_p50_ms = 0.0;
        double frametime_p95_ms = 0.0;
        double frametime_p99_ms = 0.0;
        double frametime_max_ms = 0.0;
        uint32_t alt_enter_suspend_cycles = 0u;
        uint32_t psm_suspend_callbacks = 0u;
        uint32_t psm_resume_callbacks = 0u;
        bool preserved_resources_ok = false;
    } s5;

    /* Scene 6: Audio + Input + XGameSave 12 MiB + GAME: CoW VFS */
    struct {
        bool passed = false;
        uint32_t checks = 0u;
        uint32_t opus_adat_voices = 0u;
        uint32_t xma2_voices = 0u;
        uint32_t audio_xruns = 0u;
        double xdsp_fft_max_err = 0.0;
        bool xdsp_reverb_ok = false;
        bool xdsp_hrtf_ok = false;
        uint32_t gameinput_readings_1000hz = 0u;
        bool gameinput_4motor_rumble_ok = false;
        uint64_t xgamesave_12mib_bytes = 0u;
        bool xgamesave_over16mib_rejected = false;
        uint32_t xsystem_device_type = 0u;
        bool xtaskqueue_ok = false;
        uint32_t xdl_domains_available = 0u;
        std::string game_config_title_id;
        bool dds_148b_bc7_ok = false;
        bool dds_148b_rgba8_ok = false;
        bool cow_base_untouched = false;
        bool encrypted_xvc_rejected = false;
    } s6;
};

class SceneChecker {
public:
    SceneChecker(ArenaTelemetry& tel, uint32_t& scene_checks, const char* scene_name)
        : tel_(tel), scene_checks_(scene_checks), scene_name_(scene_name) {}

    bool check(bool cond, std::string_view expr, const char* file, int line) {
        ++tel_.total_checks;
        ++scene_checks_;
        if (!cond) {
            ++tel_.failed_checks;
            ++local_failures_;
            std::cerr << "[FAIL][" << scene_name_ << "] " << file << ":" << line
                      << " (" << expr << ")\n";
        }
        return cond;
    }

    bool check_hr(scarlett_hresult_t actual,
                  scarlett_hresult_t expected,
                  std::string_view expr,
                  const char* file,
                  int line) {
        ++tel_.total_checks;
        ++scene_checks_;
        if (actual != expected) {
            ++tel_.failed_checks;
            ++local_failures_;
            std::cerr << "[FAIL][" << scene_name_ << "] " << file << ":" << line
                      << " " << expr << " returned 0x" << std::hex
                      << static_cast<uint32_t>(actual) << " (" << scarlett_error_name(actual)
                      << "), expected 0x" << static_cast<uint32_t>(expected) << " ("
                      << scarlett_error_name(expected) << ")" << std::dec << "\n";
            return false;
        }
        return true;
    }

    bool ok() const noexcept { return local_failures_ == 0u; }

private:
    ArenaTelemetry& tel_;
    uint32_t& scene_checks_;
    const char* scene_name_;
    uint32_t local_failures_ = 0u;
};

#define ARENA_CHECK(sc, cond) (sc).check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define ARENA_CHECK_HR(sc, expr, exp_hr) \
    (sc).check_hr((expr), (exp_hr), #expr " == " #exp_hr, __FILE__, __LINE__)

inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8u);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

inline std::string serialize_telemetry_json(const ArenaTelemetry& t) {
    auto bstr = [](bool b) -> const char* { return b ? "true" : "false"; };
    std::ostringstream os;
    os << std::fixed << std::setprecision(4);
    os << "{\n";
    os << "  \"title\": \"ScarlettArena-Linux\",\n";
    os << "  \"target_device_family\": \"Gaming.Xbox.Scarlett.x64\",\n";
    os << "  \"model\": \"" << json_escape(t.model_name) << "\",\n";
    os << "  \"memory_budget_bytes\": " << t.memory_budget_bytes << ",\n";
    os << "  \"all_passed\": " << bstr(t.failed_checks == 0u && t.s1.passed && t.s2.passed &&
                                       t.s3.passed && t.s4.passed && t.s5.passed && t.s6.passed) << ",\n";
    os << "  \"total_checks\": " << t.total_checks << ",\n";
    os << "  \"failed_checks\": " << t.failed_checks << ",\n";
    os << "  \"vk_error_device_lost_count\": " << t.vk_error_device_lost_count << ",\n";

    os << "  \"scene1_uma\": {\n";
    os << "    \"passed\": " << bstr(t.s1.passed) << ",\n";
    os << "    \"checks\": " << t.s1.checks << ",\n";
    os << "    \"model_budget_bytes\": " << t.s1.model_budget_bytes << ",\n";
    os << "    \"budget_exceeded_enforced\": " << bstr(t.s1.budget_exceeded_enforced) << ",\n";
    os << "    \"ro_rw_aliasing_ok\": " << bstr(t.s1.ro_rw_aliasing_ok) << ",\n";
    os << "    \"cache_conflict_rejected\": " << bstr(t.s1.cache_conflict_rejected) << ",\n";
    os << "    \"cross_alloc_rejected\": " << bstr(t.s1.cross_alloc_rejected) << ",\n";
    os << "    \"uma_tier\": " << t.s1.uma_tier << ",\n";
    os << "    \"uma_pre_submit_flushes\": " << t.s1.uma_pre_submit_flushes << ",\n";
    os << "    \"uma_present_flushes\": " << t.s1.uma_present_flushes << ",\n";
    os << "    \"zero_frame_lag_verified\": " << bstr(t.s1.zero_frame_lag_verified) << "\n";
    os << "  },\n";

    os << "  \"scene2_rdna2\": {\n";
    os << "    \"passed\": " << bstr(t.s2.passed) << ",\n";
    os << "    \"checks\": " << t.s2.checks << ",\n";
    os << "    \"live_vulkan_probed\": " << bstr(t.s2.live_vulkan_probed) << ",\n";
    os << "    \"gpu_device_name\": \"" << json_escape(t.s2.gpu_device_name) << "\",\n";
    os << "    \"spirv_val_passed\": " << t.s2.spirv_val_passed << ",\n";
    os << "    \"spirv_val_total\": " << t.s2.spirv_val_total << ",\n";
    os << "    \"exact_vs_adaptive_gpu_diffs\": " << t.s2.exact_vs_adaptive_gpu_diffs << ",\n";
    os << "    \"exact_vs_adaptive_vm_diffs\": " << t.s2.exact_vs_adaptive_vm_diffs << ",\n";
    os << "    \"wave64_cross_lane_strategy\": \"" << json_escape(t.s2.wave64_cross_lane_strategy) << "\",\n";
    os << "    \"wave32_safe_strategy\": \"" << json_escape(t.s2.wave32_safe_strategy) << "\",\n";
    os << "    \"f16_packed_ok\": " << bstr(t.s2.f16_packed_ok) << ",\n";
    os << "    \"f32_nan_denorm_ok\": " << bstr(t.s2.f32_nan_denorm_ok) << ",\n";
    os << "    \"known_fp_nan_trees_elided\": " << t.s2.known_fp_nan_trees_elided << ",\n";
    os << "    \"nvidia_reduction_pct\": " << t.s2.nvidia_reduction_pct << ",\n";
    os << "    \"radv_reduction_pct\": " << t.s2.radv_reduction_pct << ",\n";
    os << "    \"cold_lift_p99_ms\": " << t.s2.cold_lift_p99_ms << "\n";
    os << "  },\n";

    os << "  \"scene3_dstorage\": {\n";
    os << "    \"passed\": " << bstr(t.s3.passed) << ",\n";
    os << "    \"checks\": " << t.s3.checks << ",\n";
    os << "    \"payload_mib\": " << t.s3.payload_mib << ",\n";
    os << "    \"io_uring_active\": " << bstr(t.s3.io_uring_active) << ",\n";
    os << "    \"dstorage_gbps\": " << t.s3.dstorage_gbps << ",\n";
    os << "    \"pfn_swizzle_modes_verified\": " << t.s3.pfn_swizzle_modes_verified << ",\n";
    os << "    \"pfn_swizzle_crc_ok\": " << bstr(t.s3.pfn_swizzle_crc_ok) << ",\n";
    os << "    \"bcpack_missing_plugin_rejected\": " << bstr(t.s3.bcpack_missing_plugin_rejected) << ",\n";
    os << "    \"bcpack_plugin_decoded_bytes\": " << t.s3.bcpack_plugin_decoded_bytes << ",\n";
    os << "    \"vkdeflate_spirv_valid\": " << bstr(t.s3.vkdeflate_spirv_valid) << ",\n";
    os << "    \"swizzle_spirv_valid\": " << bstr(t.s3.swizzle_spirv_valid) << "\n";
    os << "  },\n";

    os << "  \"scene4_dx12u_aaa\": {\n";
    os << "    \"passed\": " << bstr(t.s4.passed) << ",\n";
    os << "    \"checks\": " << t.s4.checks << ",\n";
    os << "    \"bvh4_triangles\": " << t.s4.bvh4_triangles << ",\n";
    os << "    \"bvh4_ray_hits_verified\": " << t.s4.bvh4_ray_hits_verified << ",\n";
    os << "    \"bvh4_vk_accel_mapped\": " << bstr(t.s4.bvh4_vk_accel_mapped) << ",\n";
    os << "    \"vsm_input_tiles\": " << t.s4.vsm_input_tiles << ",\n";
    os << "    \"vsm_coalesced_image_binds\": " << t.s4.vsm_coalesced_image_binds << ",\n";
    os << "    \"vsm_coalesced_memory_binds\": " << t.s4.vsm_coalesced_memory_binds << ",\n";
    os << "    \"sfs_log2_mip_region\": " << t.s4.sfs_log2_mip_region << ",\n";
    os << "    \"ngg_mesh_amplification_ok\": " << bstr(t.s4.ngg_mesh_amplification_ok) << ",\n";
    os << "    \"vrs_supported\": " << bstr(t.s4.vrs_supported) << ",\n";
    os << "    \"nanite_int64_atomics_ok\": " << bstr(t.s4.nanite_int64_atomics_ok) << ",\n";
    os << "    \"xg_above_4gib_40bit_ok\": " << bstr(t.s4.xg_above_4gib_40bit_ok) << ",\n";
    os << "    \"xg_swizzle_formats_verified\": " << t.s4.xg_swizzle_formats_verified << ",\n";
    os << "    \"bink2_bt709_ok\": " << bstr(t.s4.bink2_bt709_ok) << ",\n";
    os << "    \"fsr_version\": \"" << json_escape(t.s4.fsr_version) << "\",\n";
    os << "    \"pixevt_markers_recorded\": " << t.s4.pixevt_markers_recorded << ",\n";
    os << "    \"desc_nvidia_610_guard_ok\": " << bstr(t.s4.desc_nvidia_610_guard_ok) << "\n";
    os << "  },\n";

    os << "  \"scene5_presentx_pacing\": {\n";
    os << "    \"passed\": " << bstr(t.s5.passed) << ",\n";
    os << "    \"checks\": " << t.s5.checks << ",\n";
    os << "    \"hdr10_pq_colorspace_ok\": " << bstr(t.s5.hdr10_pq_colorspace_ok) << ",\n";
    os << "    \"gamedvr_plane1_captured\": " << bstr(t.s5.gamedvr_plane1_captured) << ",\n";
    os << "    \"frames_paced\": " << t.s5.frames_paced << ",\n";
    os << "    \"frametime_p50_ms\": " << t.s5.frametime_p50_ms << ",\n";
    os << "    \"frametime_p95_ms\": " << t.s5.frametime_p95_ms << ",\n";
    os << "    \"frametime_p99_ms\": " << t.s5.frametime_p99_ms << ",\n";
    os << "    \"frametime_max_ms\": " << t.s5.frametime_max_ms << ",\n";
    os << "    \"alt_enter_suspend_cycles\": " << t.s5.alt_enter_suspend_cycles << ",\n";
    os << "    \"psm_suspend_callbacks\": " << t.s5.psm_suspend_callbacks << ",\n";
    os << "    \"psm_resume_callbacks\": " << t.s5.psm_resume_callbacks << ",\n";
    os << "    \"preserved_resources_ok\": " << bstr(t.s5.preserved_resources_ok) << "\n";
    os << "  },\n";

    os << "  \"scene6_runtime_audio_vfs\": {\n";
    os << "    \"passed\": " << bstr(t.s6.passed) << ",\n";
    os << "    \"checks\": " << t.s6.checks << ",\n";
    os << "    \"opus_adat_voices\": " << t.s6.opus_adat_voices << ",\n";
    os << "    \"xma2_voices\": " << t.s6.xma2_voices << ",\n";
    os << "    \"audio_xruns\": " << t.s6.audio_xruns << ",\n";
    os << "    \"xdsp_fft_max_err\": " << std::scientific << t.s6.xdsp_fft_max_err << std::fixed << ",\n";
    os << "    \"xdsp_reverb_ok\": " << bstr(t.s6.xdsp_reverb_ok) << ",\n";
    os << "    \"xdsp_hrtf_ok\": " << bstr(t.s6.xdsp_hrtf_ok) << ",\n";
    os << "    \"gameinput_readings_1000hz\": " << t.s6.gameinput_readings_1000hz << ",\n";
    os << "    \"gameinput_4motor_rumble_ok\": " << bstr(t.s6.gameinput_4motor_rumble_ok) << ",\n";
    os << "    \"xgamesave_12mib_bytes\": " << t.s6.xgamesave_12mib_bytes << ",\n";
    os << "    \"xgamesave_over16mib_rejected\": " << bstr(t.s6.xgamesave_over16mib_rejected) << ",\n";
    os << "    \"xsystem_device_type\": " << t.s6.xsystem_device_type << ",\n";
    os << "    \"xtaskqueue_ok\": " << bstr(t.s6.xtaskqueue_ok) << ",\n";
    os << "    \"xdl_domains_available\": " << t.s6.xdl_domains_available << ",\n";
    os << "    \"game_config_title_id\": \"" << json_escape(t.s6.game_config_title_id) << "\",\n";
    os << "    \"dds_148b_bc7_ok\": " << bstr(t.s6.dds_148b_bc7_ok) << ",\n";
    os << "    \"dds_148b_rgba8_ok\": " << bstr(t.s6.dds_148b_rgba8_ok) << ",\n";
    os << "    \"cow_base_untouched\": " << bstr(t.s6.cow_base_untouched) << ",\n";
    os << "    \"encrypted_xvc_rejected\": " << bstr(t.s6.encrypted_xvc_rejected) << "\n";
    os << "  }\n";
    os << "}\n";
    return os.str();
}

inline bool write_telemetry_json_file(const std::filesystem::path& path,
                                      const ArenaTelemetry& tel) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    const std::string json = serialize_telemetry_json(tel);
    ofs.write(json.data(), static_cast<std::streamsize>(json.size()));
    return ofs.good();
}

} // namespace scarlett::arena

#endif /* SCARLETT_ARENA_TELEMETRY_HPP */
