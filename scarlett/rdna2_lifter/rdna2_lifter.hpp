/*
 * Proton-Scarlett — RDNA2 (GFX10.3) -> SPIR-V 1.5/1.6 Adaptive Lifter Contract
 *
 * Unified public C++20 API + C bridge for vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/.
 *
 * Supports:
 *   - Path A (DXIL Fast-Path): strip XBOX / XS2M chunks from DXBC/DXIL containers.
 *   - Path B (Xbox RDNA2 Binary Containers): XS2, FSSM, PS\x00\x09, SGD5 unpacker.
 *   - Full GFX10.3 ISA decoder (all 12+ classes).
 *   - SSA IR, CFG structurizer (EXEC/WQM, SPV_KHR_maximal_reconvergence, SPV_KHR_quad_control).
 *   - 4-stage adaptive SPIR-V 1.5/1.6 emitter:
 *       Stage 1: Real Vulkan 1.4 GPU microprobes via VkBuffer + SHA-256 pipeline cache key.
 *       Stage 2: Forward + backward computeKnownFPClass static analysis.
 *       Stage 3: Adaptive Wave64 (native Wave64 on RADV / Wave32-narrowing / paired-lane LDS).
 *       Stage 4: BVH4 RT (VK_KHR_ray_query primary + PhysicalStorageBuffer64 BDA fallback).
 */

#ifndef SCARLETT_RDNA2_LIFTER_HPP
#define SCARLETT_RDNA2_LIFTER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "rdna2_isa.hpp"

namespace scarlett::rdna2 {

/* ============================================================================
 * Container & Shader Stage Enums
 * ============================================================================ */

enum class ContainerKind : uint8_t {
    Unknown = 0,
    DxbcDxil,   /* Path A: DXBC with DXIL/ILDB + Xbox private chunks XBOX/XS2M */
    XS2,        /* Path B: Xbox Series 2 native shader container ("XS2\0"/"XS2B") */
    FSSM,       /* Path B: Frostbite/Scarlett Shader Stream Module ("FSSM") */
    PS09,       /* Path B: Pipeline Shader v9 ("PS\x00\x09") */
    SGD5        /* Path B: Scarlett Graphics Descriptor/Shader v5 ("SGD5") */
};

enum class ShaderStage : uint8_t {
    Vertex = 0,
    Pixel,
    Compute,
    NggMesh,         /* NGG Mesh Shader -> SPV_EXT_mesh_shader (MeshEXT) */
    Amplification,   /* Amplification / Task Shader -> TaskEXT */
    Geometry,
    Hull,            /* TessellationControl */
    Domain,          /* TessellationEvaluation */
    DxrRayGen,
    DxrClosestHit,
    DxrMiss,
    DxrAnyHit,
    DxrIntersection,
    DxrCallable
};

enum class WaveStrategy : uint8_t {
    NativeWave64 = 0,            /* AMD RADV / Intel with VK_EXT_subgroup_size_control = 64 */
    NarrowedWave32 = 1,          /* Wave32 GPU + no cross-half-wave dependencies */
    PairedWave64LdsEmulation = 2 /* Wave32 GPU + cross-half-wave deps -> paired lane + LDS */
};

enum class RtStrategy : uint8_t {
    None = 0,
    RayQueryKHR = 1,     /* Primary: VK_KHR_ray_query */
    BdaBvh4Fallback = 2  /* Fallback: PhysicalStorageBuffer64 128B/64B RDNA2 BVH4 walk */
};

enum class SpirvTargetVersion : uint8_t {
    Spirv15 = 0, /* 0x00010500 (Vulkan 1.2+) */
    Spirv16 = 1  /* 0x00010600 (Vulkan 1.3 / 1.4 default) */
};

/* ============================================================================
 * Path A (DXIL Fast-Path) & Path B (RDNA2 Container) Payloads
 * ============================================================================ */

struct XboxResourceBinding {
    uint32_t xbox_slot{0};
    uint32_t vulkan_set{0};
    uint32_t vulkan_binding{0};
    uint32_t descriptor_type{0}; /* 0=SSBO, 1=UBO, 2=SampledImage, 3=StorageImage, 4=AS */
};

struct DxilFastPathResult {
    scarlett_hresult_t status{SCARLETT_E_FAIL};
    std::vector<uint8_t> clean_dxbc;
    std::vector<uint8_t> dxil_bytecode;
    ShaderStage stage{ShaderStage::Compute};
    uint32_t input_chunks{0};
    uint32_t output_chunks{0};
    uint32_t stripped_xbox_chunks{0};
    std::vector<XboxResourceBinding> remapped_bindings;
};

struct Rdna2ShaderMetadata {
    ContainerKind container_kind{ContainerKind::Unknown};
    ShaderStage stage{ShaderStage::Compute};
    uint32_t wave_size{64};
    uint32_t vgpr_count{32};
    uint32_t sgpr_count{32};
    uint32_t lds_bytes{0};
    uint32_t workgroup_size[3]{64, 1, 1};
    uint32_t export_mask{0x1};
    uint32_t flags{0};
    bool uses_wqm{false};
    bool uses_rt_bvh4{false};
    bool f32_denorm_ftz{true};
    std::vector<XboxResourceBinding> bindings;
};

struct UnpackedRdna2Container {
    scarlett_hresult_t status{SCARLETT_E_FAIL};
    Rdna2ShaderMetadata meta{};
    std::vector<uint32_t> isa_words;
};

/* Path A: strip XBOX / XS2M private chunks & extract pure DXIL for dxil-spirv */
DxilFastPathResult extract_dxil_fast_path(const uint8_t* data, size_t size) noexcept;

/* Path B: unpack XS2 / FSSM / PS\x00\x09 / SGD5 containers */
UnpackedRdna2Container unpack_rdna2_container(const uint8_t* data, size_t size) noexcept;

/* Detect container magic without full parse */
ContainerKind detect_container_kind(const uint8_t* data, size_t size) noexcept;

/* Deterministic clean-room container serializers for toolchain & tests */
std::vector<uint8_t> pack_rdna2_container(ContainerKind kind,
                                          const Rdna2ShaderMetadata& meta,
                                          const std::vector<uint32_t>& isa_words);

std::vector<uint8_t> pack_xbox_dxil_container(ShaderStage stage,
                                              const std::vector<uint8_t>& dxil_payload,
                                              bool include_xbox_chunk = true,
                                              bool include_xs2m_chunk = true);

/* ============================================================================
 * Stage 1: GPU Microprobes & SHA-256 Pipeline Cache Key
 * ============================================================================ */

/* Empirical bitmask flags folded into SHA-256 pipeline cache key */
namespace probe_bits {
constexpr uint64_t FMA_SINGLE_ROUND_EXACT    = (1ULL << 0);
constexpr uint64_t F32_DENORM_FTZ_NATIVE     = (1ULL << 1);
constexpr uint64_t F32_DENORM_PRESERVE       = (1ULL << 2);
constexpr uint64_t F32_NAN_MINMAX_SUPPRESS   = (1ULL << 3);
constexpr uint64_t F16_NATIVE_SUPPORTED      = (1ULL << 4);
constexpr uint64_t F16_NAN_DENORM_EXACT      = (1ULL << 5);
constexpr uint64_t SUBGROUP_SIZE_64_NATIVE   = (1ULL << 6);
constexpr uint64_t SUBGROUP_SIZE_32_NATIVE   = (1ULL << 7);
constexpr uint64_t EXT_SUBGROUP_SIZE_CONTROL = (1ULL << 8);
constexpr uint64_t EXT_SPV_KHR_FMA           = (1ULL << 9);
constexpr uint64_t EXT_RAY_QUERY             = (1ULL << 10);
constexpr uint64_t EXT_BDA_64                = (1ULL << 11);
constexpr uint64_t EXT_MAXIMAL_RECONVERGENCE = (1ULL << 12);
constexpr uint64_t EXT_QUAD_CONTROL          = (1ULL << 13);
constexpr uint64_t EXT_FLOAT_CONTROLS2       = (1ULL << 14);
constexpr uint64_t EXECUTED_ON_LIVE_VULKAN   = (1ULL << 15);
} // namespace probe_bits

struct GpuMicroprobeProfile {
    uint32_t vendor_id{0};
    uint32_t device_id{0};
    uint32_t driver_version{0};
    uint32_t api_version{0};
    uint32_t subgroup_size{32};
    uint32_t min_subgroup_size{32};
    uint32_t max_subgroup_size{32};
    uint64_t probe_bitmask{0};
    std::string device_name;
    bool vulkan_probed{false};

    /* Raw empirical outputs read back from VkBuffer microprobes */
    uint32_t fma_probe_bits{0};
    uint32_t denorm_ftz_probe_bits{0};
    uint32_t nan_min_probe_bits{0};
    uint32_t nan_max_probe_bits{0};
    uint32_t f16_probe_bits{0};
    uint32_t measured_subgroup_size{0};

    bool has(uint64_t bit) const noexcept { return (probe_bitmask & bit) != 0ULL; }
};

struct Sha256Digest {
    std::array<uint8_t, 32> bytes{};

    bool operator==(const Sha256Digest& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const Sha256Digest& o) const noexcept { return bytes != o.bytes; }
    std::string to_hex() const;
};

/* Clean-room SHA-256 helper */
Sha256Digest compute_sha256(const uint8_t* data, size_t size) noexcept;

/* Compute deterministic pipeline cache key folding GPU IDs + probe_bitmask + mode + container */
Sha256Digest compute_pipeline_cache_key(const GpuMicroprobeProfile& profile,
                                        scarlett_shader_mode_t mode,
                                        WaveStrategy wave_strategy,
                                        RtStrategy rt_strategy,
                                        const uint8_t* shader_bytes,
                                        size_t shader_size) noexcept;

/* Run real Vulkan 1.4 compute microprobes on live GPU using non-constant VkBuffer operands */
GpuMicroprobeProfile run_vulkan_microprobes() noexcept;

/* Generate individual valid SPIR-V 1.6 microprobe compute shaders (all read operands from VkBuffer!) */
std::vector<uint32_t> build_microprobe_fma_spirv(bool use_op_fma_khr);
std::vector<uint32_t> build_microprobe_denorm_spirv(bool ftz_mode);
std::vector<uint32_t> build_microprobe_nan_minmax_spirv();
std::vector<uint32_t> build_microprobe_subgroup_size_spirv();
std::vector<uint32_t> build_microprobe_f16_spirv();

/* Deterministic hardware profiles for multi-vendor testing & cross-compilation */
GpuMicroprobeProfile make_nvidia_warp32_profile(bool native_fp16 = false,
                                                bool ray_query = true) noexcept;
GpuMicroprobeProfile make_amd_radv_profile(bool ray_query = true) noexcept;
GpuMicroprobeProfile make_intel_anv_profile(bool wave64 = true,
                                            bool ray_query = true) noexcept;

/* ============================================================================
 * Stage 2 & Stage 3 Analysis Reports
 * ============================================================================ */

struct FpClassAnalysisStats {
    uint32_t total_fp_ops{0};
    uint32_t proven_no_nan{0};
    uint32_t proven_no_inf{0};
    uint32_t proven_no_subnormal{0};
    uint32_t nan_trees_elided{0};
    uint32_t denorm_guards_elided{0};
    uint32_t fma_emulations_elided{0};
    uint32_t f16_soft_ops_elided{0};
    uint32_t backward_demands_propagated{0};
};

struct Wave64AnalysisStats {
    bool shader_is_wave64{true};
    bool has_cross_half_wave_dep{false};
    bool uses_v_readlane{false};
    bool uses_ds_bpermute{false};
    bool uses_ds_swizzle_cross32{false};
    bool uses_ballot_hi32{false};
    bool uses_bitcnt_b64{false};
    bool uses_mbcnt_hi{false};
    bool uses_permlane{false};
    WaveStrategy chosen_strategy{WaveStrategy::NarrowedWave32};
};

/* ============================================================================
 * Lifter Configuration & Result
 * ============================================================================ */

struct LiftOptions {
    scarlett_shader_mode_t mode{SCARLETT_SHADER_MODE_ADAPTIVE};
    SpirvTargetVersion spirv_version{SpirvTargetVersion::Spirv16};
    GpuMicroprobeProfile gpu_profile{make_nvidia_warp32_profile()};
    bool prefer_ray_query{true};          /* true -> VK_KHR_ray_query if GPU supports; false -> force BDA */
    bool force_bda_bvh4{false};           /* Force PhysicalStorageBuffer64 BDA BVH4 walk */
    bool enable_maximal_reconvergence{true};
    bool enable_quad_control{true};
    bool validate_with_spirv_tools{false}; /* Validate emitted binary via libSPIRV-Tools if loaded */
    bool graphics_no_rogue_descriptors{false}; /* Suppress unrequested SSBO/image descriptors in graphics stages */
};

struct LiftResult {
    scarlett_hresult_t status{SCARLETT_E_FAIL};
    ContainerKind container_kind{ContainerKind::Unknown};
    bool used_dxil_fast_path{false};

    /* Path A output */
    DxilFastPathResult dxil_fast_path{};

    /* Path B output */
    Rdna2ShaderMetadata metadata{};
    std::vector<uint32_t> spirv_words;
    Sha256Digest pipeline_cache_key{};

    /* Analysis telemetry */
    uint32_t decoded_instruction_count{0};
    uint32_t cfg_block_count{0};
    uint32_t cfg_loop_count{0};
    uint32_t cfg_branch_count{0};
    bool emitted_maximal_reconvergence{false};
    bool emitted_quad_control{false};
    FpClassAnalysisStats fp_stats{};
    Wave64AnalysisStats wave_stats{};
    RtStrategy rt_strategy{RtStrategy::None};

    double lift_time_ms{0.0};
    std::string diagnostics;
};

/* ============================================================================
 * Main Lifter Class
 * ============================================================================ */

/* Parse PROTON_XBOX_SHADER_MODE=exact|adaptive|fast_f32 (default: adaptive) */
scarlett_shader_mode_t parse_shader_mode_env(const char* override_value = nullptr) noexcept;

class Rdna2Lifter {
public:
    Rdna2Lifter() = default;

    /* Lift any Path A (DXBC/DXIL with XBOX/XS2M) or Path B (XS2/FSSM/PS09/SGD5) container */
    LiftResult lift_container(const uint8_t* data, size_t size, const LiftOptions& opts) const noexcept;

    /* Lift an already-unpacked GFX10.3 instruction stream */
    LiftResult lift_isa(const Rdna2ShaderMetadata& meta,
                        const uint32_t* isa_words,
                        size_t word_count,
                        const LiftOptions& opts) const noexcept;
};

/* ============================================================================
 * SPIR-V 1.6 Validation & Differential Execution Helpers
 * ============================================================================ */

/* Validate SPIR-V binary using libSPIRV-Tools (SPV_ENV_VULKAN_1_4) in-process */
bool validate_spirv_vulkan14(const std::vector<uint32_t>& words,
                             std::string* out_error = nullptr) noexcept;

/* Real Vulkan 1.4 Compute Pipeline Dispatcher on live GPU (VkBuffer in -> VkBuffer out) */
struct VulkanComputeRunResult {
    bool executed{false};
    bool pipeline_created{false};
    std::vector<uint32_t> output_words;
    std::string error;
};

VulkanComputeRunResult run_spirv_compute_on_vulkan(const std::vector<uint32_t>& spirv_words,
                                                   const std::vector<uint32_t>& input_words,
                                                   uint32_t output_word_count,
                                                   uint32_t dispatch_x = 1,
                                                   const void* bda_extra_buffer = nullptr,
                                                   size_t bda_extra_size = 0,
                                                   uint64_t* out_bda_device_addr = nullptr) noexcept;

/* Deterministic SPIR-V 1.6 Software VM Evaluator for 64-lane differential exact-vs-adaptive testing */
struct SpirvVmInput {
    std::vector<uint32_t> input_buffer;
    uint32_t output_word_count{64};
    uint32_t dispatch_lanes{64};
    const uint8_t* bda_memory{nullptr};
    size_t bda_memory_size{0};
    uint64_t bda_base_va{0};
    /* Optional synthetic triangle scene for OpRayQuery* evaluation */
    struct RayTri {
        float v0[3];
        float v1[3];
        float v2[3];
        uint32_t prim_id{0};
        uint32_t inst_id{0};
    };
    std::vector<RayTri> rt_triangles;
};

struct SpirvVmOutput {
    bool ok{false};
    std::vector<uint32_t> output_buffer;
    std::string error;
};

SpirvVmOutput evaluate_spirv_compute_vm(const std::vector<uint32_t>& spirv_words,
                                        const SpirvVmInput& input) noexcept;

} // namespace scarlett::rdna2

/* ============================================================================
 * C ABI Bridge for vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/ Drop-In
 * ============================================================================ */

extern "C" {

typedef struct scarlett_rdna2_lift_c_result_s {
    scarlett_hresult_t status;
    uint32_t used_dxil_fast_path;
    uint32_t word_count;
    uint32_t byte_count;
    uint32_t wave_strategy;
    uint32_t rt_strategy;
    uint8_t  cache_key_sha256[32];
} scarlett_rdna2_lift_c_result_t;

scarlett_hresult_t scarlett_rdna2_lift_shader_c(
    const uint8_t* container_bytes,
    size_t container_size,
    scarlett_shader_mode_t mode,
    uint32_t prefer_ray_query,
    uint32_t* out_words_buf,
    size_t out_words_capacity,
    scarlett_rdna2_lift_c_result_t* out_result);

} /* extern "C" */

#endif // SCARLETT_RDNA2_LIFTER_HPP
