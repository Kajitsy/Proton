#include "arena_scenes.hpp"
#include "arena_telemetry.hpp"

#include <cstdlib>
#include <dlfcn.h>
#include <iostream>
#include <string>
#include <string_view>
#include <unistd.h>

#include "audio_media/audio_media_unix_abi.h"
#include "d3d12_xs/d3d12_xs_api.h"
#include "dstorage_xs/dstorage_xs_api.h"
#include "loader/loader_unix_abi.h"
#include "scarlett_errors.h"
#include "scarlett_unix_call.h"
#include "xg_xs/xg_xs_unix_abi.h"
#include "xgameruntime/xgameruntime_api.h"

extern "C" {
int32_t scarlett_xmem_unix_call(uint32_t opcode, void* params);
void scarlett_xmem_set_unix_call_bridge(scarlett_unix_call_fn_t fn);
int32_t scarlett_audio_unix_dispatch(uint32_t opcode, void* params);
}

static scarlett_unix_call_fn_t g_xgr_dl_unix_call = nullptr;

namespace {

bool parse_model(std::string_view s, scarlett::arena::ArenaTelemetry& tel) {
    if (s == "anaconda") {
        tel.model_name = "anaconda";
        tel.model = SCARLETT_MODEL_ANACONDA;
        tel.memory_budget_bytes = SCARLETT_BUDGET_ANACONDA_BYTES;
        return true;
    }
    if (s == "lockhart") {
        tel.model_name = "lockhart";
        tel.model = SCARLETT_MODEL_LOCKHART;
        tel.memory_budget_bytes = SCARLETT_BUDGET_LOCKHART_BYTES;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    scarlett::arena::ArenaTelemetry tel{};
    tel.package_dir = SCARLETT_ARENA_PACKAGE_DIR;
    tel.bcpack_plugin_path = SCARLETT_TEST_BCPACK_PLUGIN_PATH;

    if (const char* env_model = std::getenv(SCARLETT_ENV_MODEL)) {
        (void)parse_model(env_model, tel);
    }

    std::string report_json_path;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg.rfind("--model=", 0) == 0) {
            std::string_view val = arg.substr(8);
            if (!parse_model(val, tel)) {
                std::cerr << "Unknown model: " << val << " (expected anaconda|lockhart)\n";
                return 2;
            }
        } else if (arg.rfind("--report-json=", 0) == 0) {
            report_json_path = std::string(arg.substr(14));
        } else if (arg.rfind("--package-dir=", 0) == 0) {
            tel.package_dir = std::string(arg.substr(14));
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 2;
        }
    }

    ::setenv(SCARLETT_ENV_MODEL, tel.model_name.c_str(), 1);
    const std::string arena_saves_dir = "/tmp/scarlett_arena_saves_" + std::to_string(::getpid());
    ::setenv("PROTON_XBOX_SAVES_DIR", arena_saves_dir.c_str(), 1);

    /* Isolate xgameruntime_unix.so __wine_unix_call_funcs with RTLD_LOCAL | RTLD_DEEPBIND */
    struct XgrDlGuard {
        void* handle = nullptr;
        ~XgrDlGuard() {
            if (handle != nullptr) {
                g_xgr_dl_unix_call = nullptr;
                ::dlclose(handle);
            }
        }
    } xgr_dl_guard;
    xgr_dl_guard.handle = ::dlmopen(LM_ID_NEWLM, SCARLETT_XGR_UNIX_SO_PATH, RTLD_NOW);
    if (xgr_dl_guard.handle == nullptr) {
        std::cerr << "[FAIL] dlopen(" << SCARLETT_XGR_UNIX_SO_PATH << ") failed: "
                  << (::dlerror() ? ::dlerror() : "unknown") << "\n";
        return 1;
    }
    g_xgr_dl_unix_call = reinterpret_cast<scarlett_unix_call_fn_t>(
        ::dlsym(xgr_dl_guard.handle, "scarlett_xgr_unix_call"));
    if (g_xgr_dl_unix_call == nullptr) {
        std::cerr << "[FAIL] dlsym(scarlett_xgr_unix_call) failed\n";
        return 1;
    }

    /* Wire PE64 host shim bridges to their unixlib dispatchers */
    scarlett_loader_pe_set_unix_bridge(&scarlett_loader_unix_dispatch);
    scarlett_xmem_set_unix_call_bridge(&scarlett_xmem_unix_call);
    scarlett_d3d12xs_set_unix_call_bridge(&scarlett_d3d12xs_unix_call);
    scarlett_dstorage_set_unix_call_bridge(&scarlett_dstorage_unix_call);
    scarlett_xg_xs_set_unix_call_bridge(&scarlett_xg_xs_unix_call);
    scarlett_xgr_set_unix_call_bridge(g_xgr_dl_unix_call);
    scarlett_audio_pe_set_unix_bridge(&scarlett_audio_unix_dispatch);

    std::cout << "=== ScarlettArena-Linux (model=" << tel.model_name
              << ", budget=" << tel.memory_budget_bytes << " B) ===\n";

    const bool ok1 = scarlett::arena::run_scene1_uma_aliasing(tel);
    std::cout << "[" << (ok1 ? "PASS" : "FAIL") << "] Scene 1 (UMA & Aliasing): checks="
              << tel.s1.checks << " tier=" << tel.s1.uma_tier
              << " pre_submit_flushes=" << tel.s1.uma_pre_submit_flushes
              << " present_flushes=" << tel.s1.uma_present_flushes << "\n";

    const bool ok2 = scarlett::arena::run_scene2_rdna2_lifter(tel);
    std::cout << "[" << (ok2 ? "PASS" : "FAIL") << "] Scene 2 (RDNA2 Lifter): checks="
              << tel.s2.checks << " gpu=\"" << tel.s2.gpu_device_name
              << "\" spirv-val=" << tel.s2.spirv_val_passed << "/" << tel.s2.spirv_val_total
              << " nv_red=" << tel.s2.nvidia_reduction_pct << "%"
              << " radv_red=" << tel.s2.radv_reduction_pct << "%"
              << " p99=" << tel.s2.cold_lift_p99_ms << "ms\n";

    const bool ok3 = scarlett::arena::run_scene3_dstorage_velocity(tel);
    std::cout << "[" << (ok3 ? "PASS" : "FAIL") << "] Scene 3 (DirectStorage 512MiB): checks="
              << tel.s3.checks << " throughput=" << tel.s3.dstorage_gbps << " GB/s"
              << " pfn_swizzle_modes=" << tel.s3.pfn_swizzle_modes_verified << "/4\n";

    const bool ok4 = scarlett::arena::run_scene4_dx12_ultimate_aaa(tel);
    std::cout << "[" << (ok4 ? "PASS" : "FAIL") << "] Scene 4 (DX12U & AAA): checks="
              << tel.s4.checks << " bvh_tris=" << tel.s4.bvh4_triangles
              << " vsm=" << tel.s4.vsm_input_tiles << "->" << tel.s4.vsm_coalesced_memory_binds
              << " fsr=" << tel.s4.fsr_version
              << " 40bit_gt4G=" << (tel.s4.xg_above_4gib_40bit_ok ? "OK" : "FAIL") << "\n";

    const bool ok5 = scarlett::arena::run_scene5_presentx_pacing(tel);
    std::cout << "[" << (ok5 ? "PASS" : "FAIL") << "] Scene 5 (PresentX & 60Hz Pacing): checks="
              << tel.s5.checks << " p99=" << tel.s5.frametime_p99_ms << " ms"
              << " cycles=" << tel.s5.alt_enter_suspend_cycles
              << " psm=" << tel.s5.psm_suspend_callbacks << "/" << tel.s5.psm_resume_callbacks << "\n";

    const bool ok6 = scarlett::arena::run_scene6_runtime_audio_vfs(tel);
    std::cout << "[" << (ok6 ? "PASS" : "FAIL") << "] Scene 6 (Audio/Input/XGameSave/VFS): checks="
              << tel.s6.checks << " opus=" << tel.s6.opus_adat_voices
              << " xma2=" << tel.s6.xma2_voices << " xruns=" << tel.s6.audio_xruns
              << " save=" << (tel.s6.xgamesave_12mib_bytes / (1024u * 1024u)) << "MiB\n";

    if (!report_json_path.empty()) {
        if (!scarlett::arena::write_telemetry_json_file(report_json_path, tel)) {
            std::cerr << "[FAIL] Failed to write telemetry JSON to " << report_json_path << "\n";
            return 1;
        }
        std::cout << "[INFO] Telemetry JSON written to " << report_json_path << "\n";
    }

    const bool all_ok = ok1 && ok2 && ok3 && ok4 && ok5 && ok6 &&
                        (tel.failed_checks == 0u) && (tel.vk_error_device_lost_count == 0u);
    std::cout << (all_ok ? "[PASS]" : "[FAIL]")
              << " ScarlettArena-Linux (" << tel.model_name << "): "
              << (tel.total_checks - tel.failed_checks) << "/" << tel.total_checks
              << " checks passed, VK_ERROR_DEVICE_LOST=" << tel.vk_error_device_lost_count << "\n";
    return all_ok ? 0 : 1;
}
