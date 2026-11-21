#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace scarlett::xdl {
struct SubsystemDescriptor {
    const char* name;
    const char* target_artifact;
    uint32_t min_gameos_year;
    uint32_t max_gameos_year;
    uint32_t vtable_slot_count;
};
const SubsystemDescriptor& query_descriptor() noexcept;
uint32_t unimplemented_thunk_status() noexcept;
} // namespace scarlett::xdl

namespace scarlett::loader {
struct LoaderDescriptor {
    const char* name;
    uint32_t xvd_header_span_bytes;
    uint32_t encrypted_package_error_code;
};
const LoaderDescriptor& query_descriptor() noexcept;
uint32_t verify_unencrypted_key_count(uint32_t key_count) noexcept;
} // namespace scarlett::loader

namespace scarlett::xmem {
struct MemoryArenaSpecification {
    uint64_t mem_graphics_base;
    uint64_t mem_graphics_limit;
    uint64_t mem_title_base;
    uint64_t mem_title_limit;
    uint64_t anaconda_title_bytes;
    uint64_t lockhart_title_bytes;
    uint64_t devkit_title_bytes;
};
const MemoryArenaSpecification& query_arena_specification() noexcept;
} // namespace scarlett::xmem

namespace scarlett::dstorage_xs {
uint32_t query_options_bit_width() noexcept;
} // namespace scarlett::dstorage_xs

namespace scarlett::xg_xs {
struct TileAlignmentConstants {
    uint64_t tile_4kb_alignment;
    uint64_t tile_64kb_alignment;
};
const TileAlignmentConstants& query_tile_alignments() noexcept;
} // namespace scarlett::xg_xs

namespace scarlett::xgameruntime {
struct RuntimeCapabilities {
    uint32_t feature_domain_count;
    uint32_t xgamesave_err_first;
    uint32_t xgamesave_err_last;
};
const RuntimeCapabilities& query_capabilities() noexcept;
} // namespace scarlett::xgameruntime

namespace scarlett::audio_media {
struct AudioInputSpecification {
    uint32_t xaudio2_create_ordinal;
    uint32_t gameinput_create_ordinal;
    uint32_t impulse_rumble_motor_count;
};
const AudioInputSpecification& query_audio_input_specification() noexcept;
} // namespace scarlett::audio_media

namespace {

bool require_condition(bool cond, std::string_view message) {
    if (!cond) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::string read_text_file(const std::filesystem::path& file_path) {
    std::ifstream in(file_path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main() {
    bool ok = true;

    // 1. Verify subsystem scaffold linkage and invariants
    const auto& xdl_desc = scarlett::xdl::query_descriptor();
    ok &= require_condition(xdl_desc.min_gameos_year == 2020U && xdl_desc.max_gameos_year == 2026U,
                            "xdl Game OS year range must span 2020..2026");
    ok &= require_condition(xdl_desc.vtable_slot_count == 32U,
                            "xdl vtable slot count must equal 32");
    ok &= require_condition(scarlett::xdl::unimplemented_thunk_status() == 0x80004001U,
                            "xdl unimplemented thunk must return fail-loud status 0x80004001");

    const auto& loader_desc = scarlett::loader::query_descriptor();
    ok &= require_condition(loader_desc.xvd_header_span_bytes == 0x200U,
                            "loader XVD header span must be 0x200 bytes");
    ok &= require_condition(scarlett::loader::verify_unencrypted_key_count(0U) == 0U,
                            "loader must accept unencrypted XVC with key_count == 0");
    ok &= require_condition(scarlett::loader::verify_unencrypted_key_count(1U) == 0x80831001U,
                            "loader must reject encrypted XVC with SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED");

    const auto& mem_spec = scarlett::xmem::query_arena_specification();
    ok &= require_condition(mem_spec.mem_graphics_base == 0x100000000ULL &&
                            mem_spec.mem_graphics_limit == 0xFFFFFFFFFFULL,
                            "xmem MEM_GRAPHICS range mismatch");
    ok &= require_condition(mem_spec.mem_title_base == 0x40000000000ULL &&
                            mem_spec.mem_title_limit == 0x7FFFFFFFFFFULL,
                            "xmem MEM_TITLE range mismatch");
    ok &= require_condition(mem_spec.anaconda_title_bytes > mem_spec.lockhart_title_bytes &&
                            mem_spec.devkit_title_bytes > mem_spec.anaconda_title_bytes,
                            "xmem console memory tier sizes invalid");

    ok &= require_condition(scarlett::dstorage_xs::query_options_bit_width() == 64U,
                            "dstorage_xs request options bitfield must be 64 bits");

    const auto& tile_align = scarlett::xg_xs::query_tile_alignments();
    ok &= require_condition(tile_align.tile_4kb_alignment == 4096ULL &&
                            tile_align.tile_64kb_alignment == 65536ULL,
                            "xg_xs tile alignments invalid");

    const auto& rt_caps = scarlett::xgameruntime::query_capabilities();
    ok &= require_condition(rt_caps.feature_domain_count == 22U &&
                            rt_caps.xgamesave_err_first == 0x80830001U &&
                            rt_caps.xgamesave_err_last == 0x8083000FU,
                            "xgameruntime capability constants invalid");

    const auto& audio_spec = scarlett::audio_media::query_audio_input_specification();
    ok &= require_condition(audio_spec.xaudio2_create_ordinal == 1U &&
                            audio_spec.gameinput_create_ordinal == 1U &&
                            audio_spec.impulse_rumble_motor_count == 4U,
                            "audio_media specification invalid");

    // 2. Verify repository files and build integration
    const std::filesystem::path repo_root(SCARLETT_REPO_ROOT);
    const std::filesystem::path scarlett_root(SCARLETT_SOURCE_ROOT);

    const std::vector<std::string> required_subdirs = {
        "xdl", "loader", "xmem", "dstorage_xs", "xg_xs", "xgameruntime", "audio_media", "tests"
    };
    for (const auto& dir_name : required_subdirs) {
        const auto dir_path = scarlett_root / dir_name;
        ok &= require_condition(std::filesystem::is_directory(dir_path),
                                "missing scarlett subdirectory: " + dir_name);
        ok &= require_condition(std::filesystem::is_regular_file(dir_path / "CMakeLists.txt"),
                                "missing CMakeLists.txt in " + dir_name);
        ok &= require_condition(std::filesystem::is_regular_file(dir_path / "meson.build"),
                                "missing meson.build in " + dir_name);
    }

    const std::string makefile_in = read_text_file(repo_root / "Makefile.in");
    ok &= require_condition(!makefile_in.empty(), "failed to read Makefile.in");
    ok &= require_condition(makefile_in.find("$(eval $(call rules-source,scarlett,$(SRCDIR)/scarlett))") != std::string::npos,
                            "Makefile.in missing rules-source for scarlett");
    ok &= require_condition(makefile_in.find("$(eval $(call rules-meson,scarlett,x86_64,windows))") != std::string::npos,
                            "Makefile.in missing rules-meson for scarlett");
    ok &= require_condition(makefile_in.find("wine/scarlett/x86_64-windows") != std::string::npos &&
                            makefile_in.find("wine/scarlett/x86_64-unix") != std::string::npos &&
                            makefile_in.find("wine/scarlett/version") != std::string::npos,
                            "Makefile.in missing scarlett destination directories or version file");
    ok &= require_condition(makefile_in.find("compile_commands/scarlett.json") != std::string::npos,
                            "Makefile.in missing compile_commands/scarlett.json generation");
    ok &= require_condition(makefile_in.find("scarlett-tests:") != std::string::npos,
                            "Makefile.in missing scarlett-tests target");

    const std::string proton_py = read_text_file(repo_root / "proton");
    ok &= require_condition(!proton_py.empty(), "failed to read proton script");
    const std::vector<std::string> required_env_vars = {
        "PROTON_XBOX_SCARLETT",
        "PROTON_XBOX_MODEL",
        "PROTON_XBOX_OVERLAY_DIR",
        "PROTON_XBOX_SHADER_MODE",
        "PROTON_XBOX_STRICT",
        "PROTON_XBOX_BCPACK_PLUGIN",
        "PROTON_XBOX_NET_MODE",
    };
    for (const auto& env_var : required_env_vars) {
        ok &= require_condition(proton_py.find(env_var) != std::string::npos,
                                "proton script missing support for " + env_var);
    }

    const std::vector<std::string> required_dlls = {
        "xgameruntime", "xgameplatform", "d3d12_xs", "xg_xs", "xmem", "kernelx",
        "pixevt", "dstorage_xs", "XboxBvhBuilder", "GameInput", "xaudio2_9",
        "AcpHal", "Bink2Scarlett", "amd_fidelityfx_loader_xbox",
        "amd_fidelityfx_upscaler_xbox", "libHttpClient", "XCurl",
        "api-ms-win-core-psm-appnotify-l1-1-0"
    };
    for (const auto& dll_name : required_dlls) {
        ok &= require_condition(proton_py.find("\"" + dll_name + "\"") != std::string::npos,
                                "proton script missing console DLL override for " + dll_name);
    }

    const std::string default_pfx_py = read_text_file(repo_root / "default_pfx.py");
    ok &= require_condition(!default_pfx_py.empty(), "failed to read default_pfx.py");
    ok &= require_condition(default_pfx_py.find("lib/wine/scarlett/x86_64-windows") != std::string::npos,
                            "default_pfx.py missing lib/wine/scarlett/x86_64-windows handling");

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "[PASS] Proton-Scarlett Phase 0 scaffold and build integration verified.\n";
    return EXIT_SUCCESS;
}
