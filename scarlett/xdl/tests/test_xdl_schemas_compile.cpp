/*
 * Proton-Scarlett — Compile-Time & Runtime Verification of All 6 Generated XDL Headers
 *
 * Includes all 6 generated C++20 headers (`d3d12_xs`, `dstorage_xs`, `xg_xs`,
 * `xgameruntime`, `xaudio2_9_xs`, `gameinput_xs`) and validates ABI layouts,
 * GXDK 0x585D070E epoch splits, bitfield packing, GUIDs, 32-slot vtables,
 * zero-NULL slot guarantees, and PE64 ordinal metadata.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "d3d12_xs_xdl.hpp"
#include "dstorage_xs_xdl.hpp"
#include "xg_xs_xdl.hpp"
#include "xgameruntime_xdl.hpp"
#include "xaudio2_9_xs_xdl.hpp"
#include "gameinput_xs_xdl.hpp"

namespace {

using namespace scarlett::xdl;

/* ============================================================================
 * 1. d3d12_xs: D3D12XBOX_CREATE_DEVICE_PARAMETERS & [force_abi] checks
 * ============================================================================ */

template <typename T>
concept HasAmpShaderFields = requires(const T& t) {
    t.AmplificationShaderIndirectArgsBufferSize;
    t.AmplificationShaderPayloadBufferSize;
};

template <typename T>
concept HasDevKitDiagnosticField = requires(const T& t) {
    t.DevKitDiagnosticBufferSize;
};

template <scarlett_abi_t ABI>
consteval bool check_d3d12_pre_gxdk() {
    using Params = d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<ABI>;
    static_assert(sizeof(Params) == 40, "Pre-0x585D070E D3D12XBOX_CREATE_DEVICE_PARAMETERS must be 40 bytes");
    static_assert(!HasAmpShaderFields<Params>, "AmplificationShader fields must not exist before 0x585D070E");
    static_assert(!HasDevKitDiagnosticField<Params>, "DevKitDiagnosticBufferSize must not exist on non-DevKit ABI");
    static_assert(offsetof(Params, Version) == 0);
    static_assert(offsetof(Params, AutoBreadcrumbNodeCount) == 32);
    return true;
}

template <scarlett_abi_t ABI>
consteval bool check_d3d12_post_gxdk() {
    using Params = d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<ABI>;
    static_assert(sizeof(Params) == 48, "Post-0x585D070E D3D12XBOX_CREATE_DEVICE_PARAMETERS must be 48 bytes");
    static_assert(HasAmpShaderFields<Params>, "AmplificationShader fields must exist at >= 0x585D070E");
    static_assert(!HasDevKitDiagnosticField<Params>, "DevKitDiagnosticBufferSize must not exist on non-DevKit ABI");
    static_assert(offsetof(Params, AmplificationShaderIndirectArgsBufferSize) == 40);
    static_assert(offsetof(Params, AmplificationShaderPayloadBufferSize) == 44);
    return true;
}

static_assert(check_d3d12_pre_gxdk<kGameOs2020>());
static_assert(check_d3d12_pre_gxdk<kGameOs2021>());
static_assert(check_d3d12_pre_gxdk<kGameOs2022>());
static_assert(check_d3d12_pre_gxdk<kGameOs2023PreGxdk>());

static_assert(check_d3d12_post_gxdk<kGameOs2023>());
static_assert(check_d3d12_post_gxdk<kGameOs2024>());
static_assert(check_d3d12_post_gxdk<kGameOs2025>());
static_assert(check_d3d12_post_gxdk<kGameOs2026>());

inline constexpr scarlett_abi_t kDevKit2023 = kGameOs2023.as_model(kAbiFlagDevKit);
inline constexpr scarlett_abi_t kDevKit2026 = kGameOs2026.as_model(kAbiFlagDevKit);

static_assert(sizeof(d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<kDevKit2023>) == 56);
static_assert(HasDevKitDiagnosticField<d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<kDevKit2023>>);
static_assert(offsetof(d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<kDevKit2023>, DevKitDiagnosticBufferSize) == 48);
static_assert(sizeof(d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<kDevKit2026>) == 56);
static_assert(offsetof(d3d12_xs::D3D12XBOX_CREATE_DEVICE_PARAMETERS<kDevKit2026>, DevKitDiagnosticBufferSize) == 48);

static_assert(sizeof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kGameOs2020>) ==
              sizeof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kGameOs2026>),
              "[force_abi(10.0.19041.0)] must lock layout across all Game OS ABIs");
static_assert(alignof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kGameOs2020>) ==
              alignof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kGameOs2026>));
static_assert(sizeof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kGameOs2020>) ==
              sizeof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kDevKit2026>));
static_assert(sizeof(d3d12_xs::D3D12XBOX_LEGACY_DEVICE_DESC<kGameOs2020>) > 0);

static_assert(d3d12_xs::D3D12XBOX_COMMAND_LIST_TYPE_DMA == SCARLETT_D3D12_QUEUE_TYPE_DMA);
static_assert(d3d12_xs::D3D12XBOX_COMMAND_LIST_TYPE_COMPUTE_XS == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS);
static_assert(d3d12_xs::D3D12XBOX_DESCRIPTOR_HEAP_SAFE_LIMIT == SCARLETT_D3D12_DESC_HEAP_SAFE_LIMIT);

/* ============================================================================
 * 2. dstorage_xs: DSTORAGE_REQUEST_OPTIONS & DSTORAGE_REQUEST
 * ============================================================================ */

template <scarlett_abi_t ABI>
constexpr dstorage_xs::DSTORAGE_REQUEST_OPTIONS<ABI> make_packed_dstorage_options(
    uint64_t zlib,
    uint64_t bcpack,
    uint64_t swizzle,
    uint64_t dst_phys,
    uint64_t src_type,
    uint64_t src_phys) noexcept {
    using Opt = dstorage_xs::DSTORAGE_REQUEST_OPTIONS<ABI>;
    if constexpr (requires { Opt::pack(zlib, bcpack, swizzle, dst_phys, src_type, src_phys, 0ull); }) {
        return Opt::pack(zlib, bcpack, swizzle, dst_phys, src_type, src_phys, 0ull);
    } else {
        return Opt::pack(zlib, bcpack, swizzle, dst_phys, src_type, src_phys);
    }
}

template <scarlett_abi_t ABI>
consteval bool check_dstorage_options_abi() {
    using Opt = dstorage_xs::DSTORAGE_REQUEST_OPTIONS<ABI>;
    static_assert(sizeof(Opt) == 8, "DSTORAGE_REQUEST_OPTIONS must be 64 bits");
    static_assert(Opt::kZlibDecompressShift == SCARLETT_DSTORAGE_OPT_ZLIB_SHIFT);
    static_assert(Opt::kBcpackModeShift == SCARLETT_DSTORAGE_OPT_BCPACK_SHIFT);
    static_assert(Opt::kSwizzleModeShift == SCARLETT_DSTORAGE_OPT_SWIZZLE_SHIFT);
    static_assert(Opt::kDestinationIsPhysicalPagesShift == SCARLETT_DSTORAGE_OPT_DST_PHYS_SHIFT);
    static_assert(Opt::kSourceTypeShift == SCARLETT_DSTORAGE_OPT_SRC_TYPE_SHIFT);
    static_assert(Opt::kSourceIsPhysicalPagesShift == SCARLETT_DSTORAGE_OPT_SRC_PHYS_SHIFT);
    static_assert(Opt::kReservedShift == SCARLETT_DSTORAGE_OPT_RESERVED_SHIFT);

    constexpr Opt packed = make_packed_dstorage_options<ABI>(1ull, 0xAull, 0x1234ull, 1ull, 1ull, 1ull);
    static_assert(packed.get_ZlibDecompress() == 1ull);
    static_assert(packed.get_BcpackMode() == 0xAull);
    static_assert(packed.get_SwizzleMode() == 0x1234ull);
    static_assert(packed.get_DestinationIsPhysicalPages() == 1ull);
    static_assert(packed.get_SourceType() == 1ull);
    static_assert(packed.get_SourceIsPhysicalPages() == 1ull);
    static_assert(packed.get_Reserved() == 0ull);

    Opt mutated{};
    mutated.set_ZlibDecompress(1ull);
    mutated.set_BcpackMode(0xAull);
    mutated.set_SwizzleMode(0x1234ull);
    mutated.set_DestinationIsPhysicalPages(1ull);
    mutated.set_SourceType(1ull);
    mutated.set_SourceIsPhysicalPages(1ull);
    mutated.set_Reserved(0ull);
    return mutated.raw == packed.raw;
}

static_assert(check_dstorage_options_abi<kGameOs2020>());
static_assert(check_dstorage_options_abi<kGameOs2021>());
static_assert(check_dstorage_options_abi<kGameOs2022>());
static_assert(check_dstorage_options_abi<kGameOs2023PreGxdk>());
static_assert(check_dstorage_options_abi<kGameOs2023>());
static_assert(check_dstorage_options_abi<kGameOs2024>());
static_assert(check_dstorage_options_abi<kGameOs2025>());
static_assert(check_dstorage_options_abi<kGameOs2026>());

template <typename T>
concept HasDStorageSwizzleFields = requires(const T& t) {
    t.SwizzleSubresourceIndex;
    t.SwizzleReserved;
};

static_assert(offsetof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2020>, Options) == 0);
static_assert(offsetof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2023PreGxdk>, Options) == 0);
static_assert(offsetof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2023>, Options) == 0);
static_assert(offsetof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2026>, Options) == 0);
static_assert(!HasDStorageSwizzleFields<dstorage_xs::DSTORAGE_REQUEST<kGameOs2023PreGxdk>>);
static_assert(HasDStorageSwizzleFields<dstorage_xs::DSTORAGE_REQUEST<kGameOs2023>>);
static_assert(HasDStorageSwizzleFields<dstorage_xs::DSTORAGE_REQUEST<kGameOs2026>>);
static_assert(sizeof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2023>) >
              sizeof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2023PreGxdk>));
static_assert(sizeof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2023>) -
                  sizeof(dstorage_xs::DSTORAGE_REQUEST<kGameOs2023PreGxdk>) == 8);

/* ============================================================================
 * 3. xg_xs: 64-bit XG_SUBRESOURCE_LAYOUT, XG_SWIZZLE_MODE, XG_TEXTURE2D_DESC
 * ============================================================================ */

static_assert(sizeof(decltype(xg_xs::XG_SUBRESOURCE_LAYOUT<kGameOs2026>::SizeBytes)) == 8);
static_assert(sizeof(decltype(xg_xs::XG_SUBRESOURCE_LAYOUT<kGameOs2026>::OffsetBytes)) == 8);
static_assert(sizeof(decltype(xg_xs::XG_SUBRESOURCE_LAYOUT<kGameOs2026>::RowPitchBytes)) == 8);
static_assert(sizeof(decltype(xg_xs::XG_SUBRESOURCE_LAYOUT<kGameOs2026>::DepthPitchBytes)) == 8);
static_assert(sizeof(xg_xs::XG_SUBRESOURCE_LAYOUT<kGameOs2026>) == 48);

static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_LINEAR) == 0u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_4KB_D) == 1u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_4KB_S) == 2u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_64KB_S) == 3u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_64KB_D) == 4u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_64KB_R_X) == 5u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_64KB_D_X) == 6u);
static_assert(static_cast<uint32_t>(xg_xs::XG_SWIZZLE_MODE::XG_SWIZZLE_64KB_S_X) == 7u);

template <typename T>
concept HasSamplerFeedbackMipFields = requires(const T& t) {
    t.SamplerFeedbackMipRegionWidth;
    t.SamplerFeedbackMipRegionHeight;
};

static_assert(!HasSamplerFeedbackMipFields<xg_xs::XG_TEXTURE2D_DESC<kGameOs2020>>);
static_assert(!HasSamplerFeedbackMipFields<xg_xs::XG_TEXTURE2D_DESC<kGameOs2023PreGxdk>>);
static_assert(HasSamplerFeedbackMipFields<xg_xs::XG_TEXTURE2D_DESC<kGameOs2023>>);
static_assert(HasSamplerFeedbackMipFields<xg_xs::XG_TEXTURE2D_DESC<kGameOs2026>>);
static_assert(sizeof(xg_xs::XG_TEXTURE2D_DESC<kGameOs2023>) ==
              sizeof(xg_xs::XG_TEXTURE2D_DESC<kGameOs2023PreGxdk>) + 8);

/* ============================================================================
 * 4. xgameruntime: Canonical 32-Slot Dispatch GUIDs & VTables (2020..2026)
 * ============================================================================ */

inline constexpr scarlett_guid_t kExpectedSystemGuid = SCARLETT_GUID_SYSTEM_DISPATCH_INIT;
inline constexpr scarlett_guid_t kExpectedPlatformGuid = SCARLETT_GUID_PLATFORM_DISPATCH_INIT;

static_assert(guid_equal(xgameruntime::kIID_IScarlettSystemDispatch, kExpectedSystemGuid),
              "kIID_IScarlettSystemDispatch must match SCARLETT_GUID_SYSTEM_DISPATCH_INIT");
static_assert(guid_equal(xgameruntime::kIID_IScarlettPlatformDispatch, kExpectedPlatformGuid),
              "kIID_IScarlettPlatformDispatch must match SCARLETT_GUID_PLATFORM_DISPATCH_INIT");
static_assert(xgameruntime::kIScarlettSystemDispatchHasUuid);
static_assert(xgameruntime::kIScarlettPlatformDispatchHasUuid);
static_assert(xgameruntime::kIXGameRuntimeFlatTableHasUuid == false,
              "IXGameRuntimeFlatTable must have [no_uuid]");
static_assert(sizeof(xgameruntime::XGameRuntimeFeature) == 4);

template <scarlett_abi_t ABI>
consteval bool check_xgameruntime_vtable_sizes() {
    static_assert(sizeof(xgameruntime::IScarlettSystemDispatchVTable<ABI>) == 32u * 8u,
                  "IScarlettSystemDispatchVTable must be 256 bytes (32 slots)");
    static_assert(sizeof(xgameruntime::IScarlettPlatformDispatchVTable<ABI>) == 32u * 8u,
                  "IScarlettPlatformDispatchVTable must be 256 bytes (32 slots)");
    static_assert(sizeof(xgameruntime::IScarlettSystemDispatchProxy<ABI>) == 8u);
    static_assert(sizeof(xgameruntime::IScarlettPlatformDispatchProxy<ABI>) == 8u);
    static_assert(offsetof(xgameruntime::IScarlettSystemDispatchProxy<ABI>, lpVtbl) == 0u);
    static_assert(offsetof(xgameruntime::IScarlettPlatformDispatchProxy<ABI>, lpVtbl) == 0u);
    return true;
}

static_assert(check_xgameruntime_vtable_sizes<kGameOs2020>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2021>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2022>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2023PreGxdk>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2023>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2024>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2025>());
static_assert(check_xgameruntime_vtable_sizes<kGameOs2026>());

/* ============================================================================
 * 5. xaudio2_9_xs & gameinput_xs: Ordinals & Layouts
 * ============================================================================ */

static_assert(xaudio2_9::Export_XAudio2Create::kOrdinal == 1u);
static_assert(xaudio2_9::Export_CreateAudioVolumeMeter::kOrdinal == 2u);
static_assert(xaudio2_9::Export_CreateAudioVolumeMeter::kUnimplemented);

static_assert(gameinput::Export_GameInputCreate::kOrdinal == 1u);
static_assert(sizeof(gameinput::GameInputRumbleParams<kGameOs2026>) == 16u);
static_assert(offsetof(gameinput::GameInputRumbleParams<kGameOs2026>, lowFrequency) == 0u);
static_assert(offsetof(gameinput::GameInputRumbleParams<kGameOs2026>, highFrequency) == 4u);
static_assert(offsetof(gameinput::GameInputRumbleParams<kGameOs2026>, leftTrigger) == 8u);
static_assert(offsetof(gameinput::GameInputRumbleParams<kGameOs2026>, rightTrigger) == 12u);
static_assert(sizeof(gameinput::GameInputGamepadState<kGameOs2023>) >
              sizeof(gameinput::GameInputGamepadState<kGameOs2023PreGxdk>));

/* ============================================================================
 * Runtime Checks
 * ============================================================================ */

int g_pass_count = 0;
int g_fail_count = 0;

void check_cond(bool ok, const char* msg, const char* file, int line) {
    if (ok) {
        ++g_pass_count;
    } else {
        ++g_fail_count;
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, msg);
    }
}

#define CHECK(expr) check_cond(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

template <scarlett_abi_t ABI>
void verify_dispatch_vtables_non_null() {
    using SysProxy = xgameruntime::IScarlettSystemDispatchProxy<ABI>;
    using PlatProxy = xgameruntime::IScarlettPlatformDispatchProxy<ABI>;
    CHECK(SysProxy::verify_no_null_slots(SysProxy::default_vtable()));
    CHECK(PlatProxy::verify_no_null_slots(PlatProxy::default_vtable()));
}

void verify_dstorage_options_roundtrip() {
    struct Case {
        bool zlib;
        uint8_t bcpack;
        uint16_t swizzle;
        bool dst_phys;
        uint8_t src_type;
        bool src_phys;
    };
    const Case cases[] = {
        {false, 0u, 0u, false, 0u, false},
        {true, 0u, 0x0007u, true, 0u, false},
        {false, 0xFu, 0xFFFFu, false, 1u, true},
        {true, 0x5u, 0xA55Au, true, 1u, true},
    };

    for (const Case& c : cases) {
        const scarlett_dstorage_request_options_t c_opt = scarlett_dstorage_pack_options(
            c.zlib, c.bcpack, c.swizzle, c.dst_phys, c.src_type, c.src_phys);

        const auto xdl_packed = make_packed_dstorage_options<kGameOs2026>(
            c.zlib ? 1ull : 0ull,
            static_cast<uint64_t>(c.bcpack),
            static_cast<uint64_t>(c.swizzle),
            c.dst_phys ? 1ull : 0ull,
            static_cast<uint64_t>(c.src_type),
            c.src_phys ? 1ull : 0ull);

        CHECK(xdl_packed.raw == c_opt.raw);
        CHECK(xdl_packed.get_ZlibDecompress() == (scarlett_dstorage_opt_zlib(c_opt) ? 1ull : 0ull));
        CHECK(xdl_packed.get_BcpackMode() == scarlett_dstorage_opt_bcpack_mode(c_opt));
        CHECK(xdl_packed.get_SwizzleMode() == scarlett_dstorage_opt_swizzle_mode(c_opt));
        CHECK(xdl_packed.get_DestinationIsPhysicalPages() ==
              (scarlett_dstorage_opt_dst_is_physical(c_opt) ? 1ull : 0ull));
        CHECK(xdl_packed.get_SourceType() == scarlett_dstorage_opt_source_type(c_opt));
        CHECK(xdl_packed.get_SourceIsPhysicalPages() ==
              (scarlett_dstorage_opt_src_is_physical(c_opt) ? 1ull : 0ull));
        CHECK(xdl_packed.get_Reserved() == scarlett_dstorage_opt_reserved(c_opt));

        dstorage_xs::DSTORAGE_REQUEST_OPTIONS<kGameOs2026> built{};
        built.set_ZlibDecompress(c.zlib ? 1ull : 0ull);
        built.set_BcpackMode(c.bcpack);
        built.set_SwizzleMode(c.swizzle);
        built.set_DestinationIsPhysicalPages(c.dst_phys ? 1ull : 0ull);
        built.set_SourceType(c.src_type);
        built.set_SourceIsPhysicalPages(c.src_phys ? 1ull : 0ull);
        built.set_Reserved(0ull);
        CHECK(built.raw == c_opt.raw);
    }
}

} /* namespace */

int main() {
    verify_dispatch_vtables_non_null<kGameOs2020>();
    verify_dispatch_vtables_non_null<kGameOs2021>();
    verify_dispatch_vtables_non_null<kGameOs2022>();
    verify_dispatch_vtables_non_null<kGameOs2023PreGxdk>();
    verify_dispatch_vtables_non_null<kGameOs2023>();
    verify_dispatch_vtables_non_null<kGameOs2024>();
    verify_dispatch_vtables_non_null<kGameOs2025>();
    verify_dispatch_vtables_non_null<kGameOs2026>();
    std::printf("[PASS] xgameruntime 32-slot vtables zero-NULL verified (Game OS 2020..2026)\n");

    verify_dstorage_options_roundtrip();
    std::printf("[PASS] DSTORAGE_REQUEST_OPTIONS bit-for-bit pack/get/set round-trip\n");

    std::printf("test_xdl_schemas_compile summary: %d passed, %d failed\n",
                g_pass_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
