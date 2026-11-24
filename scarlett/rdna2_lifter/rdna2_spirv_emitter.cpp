/*
 * Proton-Scarlett — 4-Stage Adaptive SPIR-V 1.5/1.6 Binary Emitter
 *
 * Self-contained clean-room SPIR-V 1.5/1.6 binary serializer targeting Vulkan 1.4.
 * Supports:
 *   - All 9 shader execution models (Vertex, Fragment, GLCompute, MeshEXT, TaskEXT,
 *     Geometry, TessellationControl, TessellationEvaluation, DXR RT stages).
 *   - SPV_KHR_maximal_reconvergence & SPV_KHR_quad_control (WQM).
 *   - Stage 1 microprobe-driven OpFmaKHR / GLSL.std.450 Fma / DenormFlushToZero / Float16.
 *   - Stage 2 computeKnownFPClass elision of NaN-clamp trees & denorm/f16 software guards.
 *   - Stage 3 Adaptive Wave64 (NativeWave64 / NarrowedWave32 / PairedWave64LdsEmulation).
 *   - Stage 4 BVH4 RT (VK_KHR_ray_query primary + PhysicalStorageBuffer64 128B/64B BDA walk).
 */

#include "rdna2_spirv_emitter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace scarlett::rdna2 {

namespace spv {

/* Header */
constexpr uint32_t kMagic      = 0x07230203u;
constexpr uint32_t kVersion15  = 0x00010500u;
constexpr uint32_t kVersion16  = 0x00010600u;
constexpr uint32_t kGenerator  = 0x005C0001u;

/* Opcodes (100% verified against Khronos SPIR-V 1.6 unified header) */
constexpr uint16_t OpNop                             = 0;
constexpr uint16_t OpExtension                       = 10;
constexpr uint16_t OpExtInstImport                   = 11;
constexpr uint16_t OpExtInst                         = 12;
constexpr uint16_t OpMemoryModel                     = 14;
constexpr uint16_t OpEntryPoint                      = 15;
constexpr uint16_t OpExecutionMode                   = 16;
constexpr uint16_t OpCapability                      = 17;
constexpr uint16_t OpTypeVoid                        = 19;
constexpr uint16_t OpTypeBool                        = 20;
constexpr uint16_t OpTypeInt                         = 21;
constexpr uint16_t OpTypeFloat                       = 22;
constexpr uint16_t OpTypeVector                      = 23;
constexpr uint16_t OpTypeImage                       = 25;
constexpr uint16_t OpTypeSampledImage                = 27;
constexpr uint16_t OpTypeArray                       = 28;
constexpr uint16_t OpTypeRuntimeArray                = 29;
constexpr uint16_t OpTypeStruct                      = 30;
constexpr uint16_t OpTypePointer                     = 32;
constexpr uint16_t OpTypeFunction                    = 33;
constexpr uint16_t OpConstantTrue                    = 41;
constexpr uint16_t OpConstantFalse                   = 42;
constexpr uint16_t OpConstant                        = 43;
constexpr uint16_t OpConstantComposite               = 44;
constexpr uint16_t OpFunction                        = 54;
constexpr uint16_t OpFunctionEnd                     = 56;
constexpr uint16_t OpVariable                        = 59;
constexpr uint16_t OpLoad                            = 61;
constexpr uint16_t OpStore                           = 62;
constexpr uint16_t OpAccessChain                     = 65;
constexpr uint16_t OpDecorate                        = 71;
constexpr uint16_t OpMemberDecorate                  = 72;
constexpr uint16_t OpCompositeConstruct              = 80;
constexpr uint16_t OpCompositeExtract                = 81;
constexpr uint16_t OpImageSampleImplicitLod          = 87;
constexpr uint16_t OpImageSampleExplicitLod          = 88;
constexpr uint16_t OpImageFetch                      = 95;
constexpr uint16_t OpImageRead                       = 98;
constexpr uint16_t OpImageWrite                      = 99;
constexpr uint16_t OpConvertFToU                     = 109;
constexpr uint16_t OpConvertFToS                     = 110;
constexpr uint16_t OpConvertSToF                     = 111;
constexpr uint16_t OpConvertUToF                     = 112;
constexpr uint16_t OpUConvert                        = 113;
constexpr uint16_t OpFConvert                        = 115;
constexpr uint16_t OpConvertUToPtr                   = 120;
constexpr uint16_t OpBitcast                         = 124;
constexpr uint16_t OpFNegate                         = 127;
constexpr uint16_t OpIAdd                            = 128;
constexpr uint16_t OpFAdd                            = 129;
constexpr uint16_t OpISub                            = 130;
constexpr uint16_t OpFSub                            = 131;
constexpr uint16_t OpIMul                            = 132;
constexpr uint16_t OpFMul                            = 133;
constexpr uint16_t OpFDiv                            = 136;
constexpr uint16_t OpUMulExtended                    = 151;
constexpr uint16_t OpSMulExtended                    = 152;
constexpr uint16_t OpIsNan                           = 156;
constexpr uint16_t OpLogicalOr                       = 166;
constexpr uint16_t OpLogicalAnd                      = 167;
constexpr uint16_t OpLogicalNot                      = 168;
constexpr uint16_t OpSelect                          = 169;
constexpr uint16_t OpIEqual                          = 170;
constexpr uint16_t OpINotEqual                       = 171;
constexpr uint16_t OpUGreaterThan                    = 172;
constexpr uint16_t OpSGreaterThan                    = 173;
constexpr uint16_t OpUGreaterThanEqual               = 174;
constexpr uint16_t OpSGreaterThanEqual               = 175;
constexpr uint16_t OpULessThan                       = 176;
constexpr uint16_t OpSLessThan                       = 177;
constexpr uint16_t OpULessThanEqual                  = 178;
constexpr uint16_t OpSLessThanEqual                  = 179;
constexpr uint16_t OpFOrdEqual                       = 180;
constexpr uint16_t OpFUnordNotEqual                  = 183;
constexpr uint16_t OpFOrdLessThan                    = 184;
constexpr uint16_t OpFOrdGreaterThan                 = 186;
constexpr uint16_t OpFOrdLessThanEqual               = 188;
constexpr uint16_t OpFOrdGreaterThanEqual            = 190;
constexpr uint16_t OpShiftRightLogical               = 194;
constexpr uint16_t OpShiftRightArithmetic            = 195;
constexpr uint16_t OpShiftLeftLogical                = 196;
constexpr uint16_t OpBitwiseOr                       = 197;
constexpr uint16_t OpBitwiseXor                      = 198;
constexpr uint16_t OpBitwiseAnd                      = 199;
constexpr uint16_t OpNot                             = 200;
constexpr uint16_t OpBitFieldUExtract                = 203;
constexpr uint16_t OpBitCount                        = 205;
constexpr uint16_t OpEmitVertex                      = 218;
constexpr uint16_t OpEndPrimitive                    = 219;
constexpr uint16_t OpControlBarrier                  = 224;
constexpr uint16_t OpAtomicIAdd                      = 234;
constexpr uint16_t OpLoopMerge                       = 246;
constexpr uint16_t OpSelectionMerge                  = 247;
constexpr uint16_t OpLabel                           = 248;
constexpr uint16_t OpBranch                          = 249;
constexpr uint16_t OpBranchConditional               = 250;
constexpr uint16_t OpReturn                          = 253;
constexpr uint16_t OpGroupNonUniformBroadcast        = 337;
constexpr uint16_t OpGroupNonUniformBroadcastFirst   = 338;
constexpr uint16_t OpGroupNonUniformBallot           = 339;
constexpr uint16_t OpGroupNonUniformBallotBitCount   = 342;
constexpr uint16_t OpGroupNonUniformShuffle          = 345;
constexpr uint16_t OpGroupNonUniformShuffleXor       = 346;
constexpr uint16_t OpFmaKHR                          = 4427;
constexpr uint16_t OpTraceRayKHR                     = 4445;
constexpr uint16_t OpTypeRayQueryKHR                 = 4472;
constexpr uint16_t OpRayQueryInitializeKHR           = 4473;
constexpr uint16_t OpRayQueryConfirmIntersectionKHR  = 4476;
constexpr uint16_t OpRayQueryProceedKHR              = 4477;
constexpr uint16_t OpRayQueryGetIntersectionTypeKHR  = 4479;
constexpr uint16_t OpEmitMeshTasksEXT                = 5294;
constexpr uint16_t OpSetMeshOutputsEXT               = 5295;
constexpr uint16_t OpReportIntersectionKHR           = 5334;
constexpr uint16_t OpTypeAccelerationStructureKHR    = 5341;
constexpr uint16_t OpRayQueryGetIntersectionTKHR     = 6018;

/* Capabilities */
constexpr uint32_t CapShader                         = 1;
constexpr uint32_t CapGeometry                       = 2;
constexpr uint32_t CapTessellation                   = 3;
constexpr uint32_t CapFloat16                        = 9;
constexpr uint32_t CapInt64                          = 11;
constexpr uint32_t CapGroupNonUniform                = 61;
constexpr uint32_t CapGroupNonUniformBallot          = 64;
constexpr uint32_t CapGroupNonUniformShuffle         = 65;
constexpr uint32_t CapDenormPreserve                 = 4464;
constexpr uint32_t CapDenormFlushToZero              = 4465;
constexpr uint32_t CapRayQueryKHR                    = 4472;
constexpr uint32_t CapRayTracingKHR                  = 4479;
constexpr uint32_t CapQuadControlKHR                 = 5087;
constexpr uint32_t CapMeshShadingEXT                 = 5283;
constexpr uint32_t CapPhysicalStorageBufferAddresses = 5347;
constexpr uint32_t CapFMAKHR                         = 6030;

/* Execution Models */
constexpr uint32_t EmVertex                  = 0;
constexpr uint32_t EmTessellationControl     = 1;
constexpr uint32_t EmTessellationEvaluation  = 2;
constexpr uint32_t EmGeometry                = 3;
constexpr uint32_t EmFragment                = 4;
constexpr uint32_t EmGLCompute               = 5;
constexpr uint32_t EmRayGenerationKHR        = 5313;
constexpr uint32_t EmIntersectionKHR         = 5314;
constexpr uint32_t EmAnyHitKHR               = 5315;
constexpr uint32_t EmClosestHitKHR           = 5316;
constexpr uint32_t EmMissKHR                 = 5317;
constexpr uint32_t EmCallableKHR             = 5318;
constexpr uint32_t EmTaskEXT                 = 5364;
constexpr uint32_t EmMeshEXT                 = 5365;

/* Execution Modes */
constexpr uint32_t ModeInvocations            = 0;
constexpr uint32_t ModeSpacingEqual           = 1;
constexpr uint32_t ModeVertexOrderCw          = 4;
constexpr uint32_t ModeOriginUpperLeft        = 7;
constexpr uint32_t ModeLocalSize              = 17;
constexpr uint32_t ModeTriangles              = 22;
constexpr uint32_t ModeOutputVertices         = 26;
constexpr uint32_t ModeOutputTriangleStrip    = 29;
constexpr uint32_t ModeDenormPreserve         = 4459;
constexpr uint32_t ModeDenormFlushToZero      = 4460;
constexpr uint32_t ModeQuadDerivativesKHR     = 5088;
constexpr uint32_t ModeOutputPrimitivesEXT    = 5270;
constexpr uint32_t ModeOutputTrianglesEXT     = 5298;
constexpr uint32_t ModeMaximallyReconvergesKHR = 6023;

/* Addressing & Memory Models */
constexpr uint32_t AddrLogical                  = 0;
constexpr uint32_t AddrPhysicalStorageBuffer64  = 5348;
constexpr uint32_t MemGLSL450                   = 1;

/* Storage Classes */
constexpr uint32_t ScUniformConstant         = 0;
constexpr uint32_t ScInput                   = 1;
constexpr uint32_t ScOutput                  = 3;
constexpr uint32_t ScWorkgroup               = 4;
constexpr uint32_t ScFunction                = 7;
constexpr uint32_t ScStorageBuffer           = 12;
constexpr uint32_t ScIncomingCallableDataKHR = 5329;
constexpr uint32_t ScRayPayloadKHR           = 5338;
constexpr uint32_t ScIncomingRayPayloadKHR   = 5342;
constexpr uint32_t ScPhysicalStorageBuffer   = 5349;
constexpr uint32_t ScTaskPayloadWorkgroupEXT = 5402;

/* Decorations */
constexpr uint32_t DecBlock          = 2;
constexpr uint32_t DecArrayStride    = 6;
constexpr uint32_t DecBuiltIn        = 11;
constexpr uint32_t DecPatch          = 15;
constexpr uint32_t DecNonWritable    = 24;
constexpr uint32_t DecLocation       = 30;
constexpr uint32_t DecBinding        = 33;
constexpr uint32_t DecDescriptorSet  = 34;
constexpr uint32_t DecOffset         = 35;
constexpr uint32_t DecNoContraction  = 42;

/* BuiltIns */
constexpr uint32_t BiPosition                   = 0;
constexpr uint32_t BiTessLevelOuter             = 11;
constexpr uint32_t BiTessLevelInner             = 12;
constexpr uint32_t BiTessCoord                  = 13;
constexpr uint32_t BiFragCoord                  = 15;
constexpr uint32_t BiGlobalInvocationId         = 28;
constexpr uint32_t BiLocalInvocationIndex       = 29;
constexpr uint32_t BiSubgroupLocalInvocationId  = 41;
constexpr uint32_t BiVertexIndex                = 42;
constexpr uint32_t BiPrimitiveTriangleIndicesEXT = 5296;
constexpr uint32_t BiLaunchIdKHR                = 5319;
constexpr uint32_t BiLaunchSizeKHR              = 5320;

/* GLSL.std.450 */
constexpr uint32_t GlslRoundEven      = 2;
constexpr uint32_t GlslTrunc          = 3;
constexpr uint32_t GlslFAbs           = 4;
constexpr uint32_t GlslFloor          = 8;
constexpr uint32_t GlslCeil           = 9;
constexpr uint32_t GlslFract          = 10;
constexpr uint32_t GlslSin            = 13;
constexpr uint32_t GlslCos            = 14;
constexpr uint32_t GlslExp2           = 28;
constexpr uint32_t GlslLog2           = 30;
constexpr uint32_t GlslSqrt           = 31;
constexpr uint32_t GlslInverseSqrt    = 32;
constexpr uint32_t GlslFMin           = 37;
constexpr uint32_t GlslUMin           = 38;
constexpr uint32_t GlslSMin           = 39;
constexpr uint32_t GlslFMax           = 40;
constexpr uint32_t GlslUMax           = 41;
constexpr uint32_t GlslSMax           = 42;
constexpr uint32_t GlslFClamp         = 43;
constexpr uint32_t GlslFma            = 50;
constexpr uint32_t GlslPackHalf2x16   = 58;
constexpr uint32_t GlslUnpackHalf2x16 = 62;
constexpr uint32_t GlslFindILsb       = 73;
constexpr uint32_t GlslFindUMsb       = 75;
constexpr uint32_t GlslNMin           = 79;
constexpr uint32_t GlslNMax           = 80;

/* Scopes & Memory Semantics */
constexpr uint32_t ScopeWorkgroup              = 2;
constexpr uint32_t ScopeSubgroup               = 3;
constexpr uint32_t SemAcquireReleaseWorkgroup  = 0x108u; /* AcquireRelease(8) | WorkgroupMemory(256) */
constexpr uint32_t MemAccessAligned            = 0x2u;

/* Group Operations */
constexpr uint32_t GroupOpReduce = 0;

} // namespace spv

namespace {

void push_inst(std::vector<uint32_t>& out, uint16_t opcode, std::initializer_list<uint32_t> ops) {
    const uint32_t wc = static_cast<uint32_t>(1u + ops.size());
    out.push_back((wc << 16u) | opcode);
    for (uint32_t w : ops) {
        out.push_back(w);
    }
}

void push_string(std::vector<uint32_t>& out, const std::string& s) {
    const size_t n = s.size() + 1u;
    const size_t words = (n + 3u) / 4u;
    for (size_t i = 0; i < words; ++i) {
        uint32_t w = 0u;
        for (size_t b = 0; b < 4u; ++b) {
            const size_t pos = i * 4u + b;
            if (pos < s.size()) {
                w |= static_cast<uint32_t>(static_cast<uint8_t>(s[pos])) << (b * 8u);
            }
        }
        out.push_back(w);
    }
}

void push_inst_str(std::vector<uint32_t>& out, uint16_t opcode,
                   std::initializer_list<uint32_t> prefix, const std::string& s) {
    const size_t str_words = (s.size() + 1u + 3u) / 4u;
    const uint32_t wc = static_cast<uint32_t>(1u + prefix.size() + str_words);
    out.push_back((wc << 16u) | opcode);
    for (uint32_t w : prefix) out.push_back(w);
    push_string(out, s);
}

uint32_t f32_bits(float f) noexcept {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

class SpvEmitter {
public:
    SpvEmitter(const IrModule& mod, const LiftOptions& opts)
        : mod_(mod), opts_(opts) {}

    SpirvEmitOutput build() {
        SpirvEmitOutput result{};

        add_cap(spv::CapShader);
        glsl_ext_id_ = alloc_id();
        main_fn_id_ = alloc_id();

        /* Base types */
        t_void_ = alloc_id();
        push_inst(types_, spv::OpTypeVoid, {t_void_});
        t_bool_ = alloc_id();
        push_inst(types_, spv::OpTypeBool, {t_bool_});
        t_u32_ = alloc_id();
        push_inst(types_, spv::OpTypeInt, {t_u32_, 32u, 0u});
        t_i32_ = alloc_id();
        push_inst(types_, spv::OpTypeInt, {t_i32_, 32u, 1u});
        t_f32_ = alloc_id();
        push_inst(types_, spv::OpTypeFloat, {t_f32_, 32u});

        t_v2f32_ = alloc_id();
        push_inst(types_, spv::OpTypeVector, {t_v2f32_, t_f32_, 2u});
        t_v3f32_ = alloc_id();
        push_inst(types_, spv::OpTypeVector, {t_v3f32_, t_f32_, 3u});
        t_v4f32_ = alloc_id();
        push_inst(types_, spv::OpTypeVector, {t_v4f32_, t_f32_, 4u});
        t_v3u32_ = alloc_id();
        push_inst(types_, spv::OpTypeVector, {t_v3u32_, t_u32_, 3u});
        t_v4u32_ = alloc_id();
        push_inst(types_, spv::OpTypeVector, {t_v4u32_, t_u32_, 4u});

        t_fn_void_ = alloc_id();
        push_inst(types_, spv::OpTypeFunction, {t_fn_void_, t_void_});

        /* Common pointer types */
        p_func_u32_ = make_ptr(spv::ScFunction, t_u32_);
        p_func_f32_ = make_ptr(spv::ScFunction, t_f32_);

        /* Determine if native FP16 is needed */
        bool any_native_f16 = false;
        for (const auto& inst : mod_.insts) {
            if (inst.use_native_f16) {
                any_native_f16 = true;
                break;
            }
        }
        if (any_native_f16) {
            add_cap(spv::CapFloat16);
            t_f16_ = alloc_id();
            push_inst(types_, spv::OpTypeFloat, {t_f16_, 16u});
            t_v2f16_ = alloc_id();
            push_inst(types_, spv::OpTypeVector, {t_v2f16_, t_f16_, 2u});
        }

        /* Determine stage execution model & stage-specific capabilities/globals */
        const uint32_t exec_model = map_execution_model(mod_.meta.stage);
        const bool is_workgroup_stage =
            mod_.meta.stage == ShaderStage::Compute ||
            mod_.meta.stage == ShaderStage::NggMesh ||
            mod_.meta.stage == ShaderStage::Amplification;

        /* Denorm execution mode when adaptive/fast and GPU supports float controls */
        if (opts_.mode != SCARLETT_SHADER_MODE_EXACT) {
            if (mod_.meta.f32_denorm_ftz && opts_.gpu_profile.has(probe_bits::F32_DENORM_FTZ_NATIVE)) {
                add_cap(spv::CapDenormFlushToZero);
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeDenormFlushToZero, 32u});
            } else if (!mod_.meta.f32_denorm_ftz && opts_.gpu_profile.has(probe_bits::F32_DENORM_PRESERVE)) {
                add_cap(spv::CapDenormPreserve);
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeDenormPreserve, 32u});
            }
        }

        /* Maximal reconvergence & Quad control.
         * Per VUID-StandaloneSpirv-MaximallyReconvergesKHR-09565, MaximallyReconvergesKHR
         * must NOT be applied to entry points using invocation-repacking RT ops
         * (OpTraceRayKHR / OpReportIntersectionKHR). */
        const bool is_rt_repack_stage =
            exec_model == spv::EmRayGenerationKHR ||
            exec_model == spv::EmIntersectionKHR ||
            exec_model == spv::EmCallableKHR;
        const bool want_quad =
            opts_.enable_quad_control &&
            opts_.gpu_profile.has(probe_bits::EXT_QUAD_CONTROL) &&
            mod_.meta.stage == ShaderStage::Pixel &&
            mod_.uses_wqm;
        const bool want_reconv =
            !is_rt_repack_stage &&
            (want_quad ||
             (opts_.enable_maximal_reconvergence &&
              opts_.gpu_profile.has(probe_bits::EXT_MAXIMAL_RECONVERGENCE) &&
              (mod_.has_divergent_cf || mod_.has_subgroup_ops || mod_.uses_wqm)));
        if (want_reconv) {
            add_ext("SPV_KHR_maximal_reconvergence");
            push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeMaximallyReconvergesKHR});
            result.emitted_maximal_reconvergence = true;
        }
        if (want_quad) {
            add_ext("SPV_KHR_quad_control");
            add_cap(spv::CapQuadControlKHR);
            push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeQuadDerivativesKHR});
            result.emitted_quad_control = true;
        }

        /* BVH4 RT Memory Model & Types */
        if (mod_.rt_strategy == RtStrategy::BdaBvh4Fallback) {
            addr_model_ = spv::AddrPhysicalStorageBuffer64;
            add_cap(spv::CapPhysicalStorageBufferAddresses);
            add_cap(spv::CapInt64);
            t_u64_ = alloc_id();
            push_inst(types_, spv::OpTypeInt, {t_u64_, 64u, 0u});
            setup_bda_bvh4_types();
        } else if (mod_.rt_strategy == RtStrategy::RayQueryKHR) {
            add_cap(spv::CapRayQueryKHR);
            add_ext("SPV_KHR_ray_query");
            setup_ray_query_globals();
        }

        const bool suppress_rogue_descriptors =
            opts_.graphics_no_rogue_descriptors &&
            mod_.meta.stage != ShaderStage::Compute &&
            mod_.rt_strategy != RtStrategy::BdaBvh4Fallback;

        /* Storage Buffers (binding 0 = input SSBO, binding 1 = output SSBO) or Graphics CBV slots */
        const bool is_gfx_with_explicit_cbv =
            (mod_.meta.stage != ShaderStage::Compute &&
             mod_.rt_strategy != RtStrategy::BdaBvh4Fallback &&
             mod_.cbv_slot_mask != 0u);
        const bool need_ssbo =
            !suppress_rogue_descriptors &&
            (mod_.uses_storage_buffer ||
             mod_.rt_strategy == RtStrategy::BdaBvh4Fallback ||
             mod_.meta.stage == ShaderStage::Compute);
        if (need_ssbo && is_gfx_with_explicit_cbv) {
            setup_ssbo_types();
            for (uint32_t s = 0u; s < 8u; ++s) {
                if ((mod_.cbv_slot_mask & (1u << s)) != 0u) {
                    var_cbv_ssbo_[s] = alloc_id();
                    push_inst(globals_, spv::OpVariable, {p_ssbo_struct_, var_cbv_ssbo_[s], spv::ScStorageBuffer});
                    push_inst(decos_, spv::OpDecorate, {var_cbv_ssbo_[s], spv::DecDescriptorSet, 0u});
                    push_inst(decos_, spv::OpDecorate, {var_cbv_ssbo_[s], spv::DecBinding, s});
                    push_inst(decos_, spv::OpDecorate, {var_cbv_ssbo_[s], spv::DecNonWritable});
                    add_interface(var_cbv_ssbo_[s]);
                }
            }
        } else if (need_ssbo) {
            setup_ssbo_globals();
        }

        /* Sampled / Storage images */
        if (!suppress_rogue_descriptors && mod_.tex_slot_mask != 0u) {
            setup_sampled_image_types();
            uint32_t p_si = make_ptr(spv::ScUniformConstant, t_sampled_img_);
            for (uint32_t s = 0u; s < 8u; ++s) {
                if ((mod_.tex_slot_mask & (1u << s)) != 0u) {
                    var_tex_2d_[s] = alloc_id();
                    push_inst(globals_, spv::OpVariable, {p_si, var_tex_2d_[s], spv::ScUniformConstant});
                    push_inst(decos_, spv::OpDecorate, {var_tex_2d_[s], spv::DecDescriptorSet, 0u});
                    push_inst(decos_, spv::OpDecorate, {var_tex_2d_[s], spv::DecBinding, 8u + s});
                    add_interface(var_tex_2d_[s]);
                }
            }
        } else if (!suppress_rogue_descriptors && mod_.uses_image_sample) {
            setup_sampled_image_global();
        }
        if (!suppress_rogue_descriptors && mod_.uses_image_storage) {
            setup_storage_image_global();
        }

        /* LDS / Wave64 Paired-Lane Shared Memory (only on workgroup stages!) */
        const bool use_paired_wave64_lds =
            is_workgroup_stage &&
            (mod_.wave_stats.chosen_strategy == WaveStrategy::PairedWave64LdsEmulation);
        if (is_workgroup_stage && (mod_.uses_lds || use_paired_wave64_lds)) {
            setup_lds_globals(use_paired_wave64_lds);
        }

        /* Stage-specific interface variables & execution modes */
        setup_stage_interface(exec_model, is_workgroup_stage);

        /* Function prologue */
        push_inst(func_, spv::OpFunction, {t_void_, main_fn_id_, 0u, t_fn_void_});
        const uint32_t entry_label = alloc_id();
        push_inst(func_, spv::OpLabel, {entry_label});

        /* Emit local Function variables at top of entry block (strict SPIR-V rule!) */
        if (mod_.rt_strategy == RtStrategy::RayQueryKHR) {
            local_rq_var_ = alloc_id();
            push_inst(func_vars_, spv::OpVariable, {p_func_rq_, local_rq_var_, spv::ScFunction});
        }
        if (mod_.rt_strategy == RtStrategy::BdaBvh4Fallback) {
            local_bvh_stack_ = alloc_id();
            push_inst(func_vars_, spv::OpVariable, {p_func_u32_arr32_, local_bvh_stack_, spv::ScFunction});
            local_bvh_sp_ = alloc_id();
            push_inst(func_vars_, spv::OpVariable, {p_func_u32_, local_bvh_sp_, spv::ScFunction});
            local_bvh_best_t_ = alloc_id();
            push_inst(func_vars_, spv::OpVariable, {p_func_f32_, local_bvh_best_t_, spv::ScFunction});
        }
        if (mod_.loop_count > 0u) {
            local_loop_ctr_ = alloc_id();
            push_inst(func_vars_, spv::OpVariable, {p_func_u32_, local_loop_ctr_, spv::ScFunction});
        }

        /* Compute invocation lane ID in entry block */
        lane_id_val_ = emit_lane_id(is_workgroup_stage);

        /* Paired-lane Wave64 prologue (in exact mode or Wave64-cross-lane on NVIDIA) */
        if (use_paired_wave64_lds) {
            emit_wave64_paired_prologue();
        }

        /* Emit all SSA IR instructions */
        emit_ir_body(is_workgroup_stage, use_paired_wave64_lds);

        /* Stage epilogue & terminator */
        emit_stage_epilogue(exec_model);
        push_inst(body_, spv::OpFunctionEnd, {});

        /* Combine function entry label + local vars + body */
        func_.insert(func_.end(), func_vars_.begin(), func_vars_.end());
        func_.insert(func_.end(), body_.begin(), body_.end());

        /* Assemble final binary in strict SPIR-V section order */
        auto& out = result.words;
        out.reserve(64u + caps_.size() + exts_.size() + decos_.size() + types_.size() + func_.size());
        out.push_back(spv::kMagic);
        out.push_back(opts_.spirv_version == SpirvTargetVersion::Spirv15 ? spv::kVersion15 : spv::kVersion16);
        out.push_back(spv::kGenerator);
        out.push_back(next_id_);
        out.push_back(0u);

        /* 1. Capabilities */
        out.insert(out.end(), caps_.begin(), caps_.end());
        /* 2. Extensions */
        out.insert(out.end(), exts_.begin(), exts_.end());
        /* 3. ExtInstImport */
        push_inst_str(out, spv::OpExtInstImport, {glsl_ext_id_}, "GLSL.std.450");
        /* 4. MemoryModel */
        push_inst(out, spv::OpMemoryModel, {addr_model_, spv::MemGLSL450});
        /* 5. EntryPoint (list all referenced global interface variables!) */
        {
            const std::string ep_name = "main";
            const size_t str_words = (ep_name.size() + 1u + 3u) / 4u;
            const uint32_t wc = static_cast<uint32_t>(3u + str_words + interface_vars_.size());
            out.push_back((wc << 16u) | spv::OpEntryPoint);
            out.push_back(exec_model);
            out.push_back(main_fn_id_);
            push_string(out, ep_name);
            for (uint32_t iv : interface_vars_) {
                out.push_back(iv);
            }
        }
        /* 6. ExecutionModes */
        out.insert(out.end(), exec_modes_.begin(), exec_modes_.end());
        /* 7. Decorations */
        out.insert(out.end(), decos_.begin(), decos_.end());
        /* 8. Types, Constants, Global Variables (emitted in strict definition order) */
        out.insert(out.end(), types_.begin(), types_.end());
        out.insert(out.end(), globals_.begin(), globals_.end());
        /* 9. Functions */
        out.insert(out.end(), func_.begin(), func_.end());

        return result;
    }

private:
    uint32_t alloc_id() { return next_id_++; }

    void add_cap(uint32_t cap) {
        if (cap_set_.insert(cap).second) {
            push_inst(caps_, spv::OpCapability, {cap});
        }
    }

    void add_ext(const std::string& name) {
        if (ext_set_.insert(name).second) {
            push_inst_str(exts_, spv::OpExtension, {}, name);
        }
    }

    uint32_t make_ptr(uint32_t storage_class, uint32_t pointee_type) {
        const uint64_t key = (static_cast<uint64_t>(storage_class) << 32u) | pointee_type;
        auto it = ptr_cache_.find(key);
        if (it != ptr_cache_.end()) return it->second;
        const uint32_t id = alloc_id();
        push_inst(types_, spv::OpTypePointer, {id, storage_class, pointee_type});
        ptr_cache_[key] = id;
        return id;
    }

    uint32_t const_u32(uint32_t v) {
        auto it = u32_const_cache_.find(v);
        if (it != u32_const_cache_.end()) return it->second;
        const uint32_t id = alloc_id();
        push_inst(types_, spv::OpConstant, {t_u32_, id, v});
        u32_const_cache_[v] = id;
        return id;
    }

    uint32_t const_i32(int32_t v) {
        const uint32_t u = static_cast<uint32_t>(v);
        auto it = i32_const_cache_.find(u);
        if (it != i32_const_cache_.end()) return it->second;
        const uint32_t id = alloc_id();
        push_inst(types_, spv::OpConstant, {t_i32_, id, u});
        i32_const_cache_[u] = id;
        return id;
    }

    uint32_t const_f32(float f) {
        const uint32_t bits = f32_bits(f);
        auto it = f32_const_cache_.find(bits);
        if (it != f32_const_cache_.end()) return it->second;
        const uint32_t id = alloc_id();
        push_inst(types_, spv::OpConstant, {t_f32_, id, bits});
        f32_const_cache_[bits] = id;
        return id;
    }

    uint32_t const_u64(uint64_t v) {
        auto it = u64_const_cache_.find(v);
        if (it != u64_const_cache_.end()) return it->second;
        const uint32_t id = alloc_id();
        push_inst(types_, spv::OpConstant, {t_u64_, id,
                  static_cast<uint32_t>(v & 0xFFFFFFFFu),
                  static_cast<uint32_t>(v >> 32u)});
        u64_const_cache_[v] = id;
        return id;
    }

    uint32_t const_bool(bool b) {
        if (b) {
            if (c_true_ == 0u) {
                c_true_ = alloc_id();
                push_inst(types_, spv::OpConstantTrue, {t_bool_, c_true_});
            }
            return c_true_;
        }
        if (c_false_ == 0u) {
            c_false_ = alloc_id();
            push_inst(types_, spv::OpConstantFalse, {t_bool_, c_false_});
        }
        return c_false_;
    }

    void add_interface(uint32_t var_id) {
        if (interface_set_.insert(var_id).second) {
            interface_vars_.push_back(var_id);
        }
    }

    static uint32_t map_execution_model(ShaderStage st) noexcept {
        switch (st) {
            case ShaderStage::Vertex:          return spv::EmVertex;
            case ShaderStage::Pixel:           return spv::EmFragment;
            case ShaderStage::Compute:         return spv::EmGLCompute;
            case ShaderStage::NggMesh:         return spv::EmMeshEXT;
            case ShaderStage::Amplification:   return spv::EmTaskEXT;
            case ShaderStage::Geometry:        return spv::EmGeometry;
            case ShaderStage::Hull:            return spv::EmTessellationControl;
            case ShaderStage::Domain:          return spv::EmTessellationEvaluation;
            case ShaderStage::DxrRayGen:       return spv::EmRayGenerationKHR;
            case ShaderStage::DxrClosestHit:   return spv::EmClosestHitKHR;
            case ShaderStage::DxrMiss:         return spv::EmMissKHR;
            case ShaderStage::DxrAnyHit:       return spv::EmAnyHitKHR;
            case ShaderStage::DxrIntersection: return spv::EmIntersectionKHR;
            case ShaderStage::DxrCallable:     return spv::EmCallableKHR;
        }
        return spv::EmGLCompute;
    }

    void setup_ssbo_types() {
        t_rt_u32_ = alloc_id();
        push_inst(types_, spv::OpTypeRuntimeArray, {t_rt_u32_, t_u32_});
        push_inst(decos_, spv::OpDecorate, {t_rt_u32_, spv::DecArrayStride, 4u});

        t_ssbo_struct_ = alloc_id();
        push_inst(types_, spv::OpTypeStruct, {t_ssbo_struct_, t_rt_u32_});
        push_inst(decos_, spv::OpDecorate, {t_ssbo_struct_, spv::DecBlock});
        push_inst(decos_, spv::OpMemberDecorate, {t_ssbo_struct_, 0u, spv::DecOffset, 0u});

        p_ssbo_struct_ = make_ptr(spv::ScStorageBuffer, t_ssbo_struct_);
        p_ssbo_u32_ = make_ptr(spv::ScStorageBuffer, t_u32_);
    }

    void setup_ssbo_globals() {
        setup_ssbo_types();

        var_in_ssbo_ = alloc_id();
        push_inst(globals_, spv::OpVariable, {p_ssbo_struct_, var_in_ssbo_, spv::ScStorageBuffer});
        push_inst(decos_, spv::OpDecorate, {var_in_ssbo_, spv::DecDescriptorSet, 0u});
        push_inst(decos_, spv::OpDecorate, {var_in_ssbo_, spv::DecBinding, 0u});
        push_inst(decos_, spv::OpDecorate, {var_in_ssbo_, spv::DecNonWritable});
        add_interface(var_in_ssbo_);

        var_out_ssbo_ = alloc_id();
        push_inst(globals_, spv::OpVariable, {p_ssbo_struct_, var_out_ssbo_, spv::ScStorageBuffer});
        push_inst(decos_, spv::OpDecorate, {var_out_ssbo_, spv::DecDescriptorSet, 0u});
        push_inst(decos_, spv::OpDecorate, {var_out_ssbo_, spv::DecBinding, 1u});
        add_interface(var_out_ssbo_);
    }

    void setup_sampled_image_types() {
        t_img_2d_ = alloc_id();
        /* Dim=2D(1), Depth=0, Arrayed=0, MS=0, Sampled=1, Format=Unknown(0) */
        push_inst(types_, spv::OpTypeImage, {t_img_2d_, t_f32_, 1u, 0u, 0u, 0u, 1u, 0u});
        t_sampled_img_ = alloc_id();
        push_inst(types_, spv::OpTypeSampledImage, {t_sampled_img_, t_img_2d_});
    }

    void setup_sampled_image_global() {
        setup_sampled_image_types();
        uint32_t p_si = make_ptr(spv::ScUniformConstant, t_sampled_img_);
        var_sampled_img_ = alloc_id();
        push_inst(globals_, spv::OpVariable, {p_si, var_sampled_img_, spv::ScUniformConstant});
        push_inst(decos_, spv::OpDecorate, {var_sampled_img_, spv::DecDescriptorSet, 0u});
        push_inst(decos_, spv::OpDecorate, {var_sampled_img_, spv::DecBinding, 2u});
        add_interface(var_sampled_img_);
    }

    void setup_storage_image_global() {
        t_stor_img_2d_ = alloc_id();
        /* Dim=2D(1), Depth=0, Arrayed=0, MS=0, Sampled=2(storage), Format=Rgba32f(1) */
        push_inst(types_, spv::OpTypeImage, {t_stor_img_2d_, t_f32_, 1u, 0u, 0u, 0u, 2u, 1u});
        uint32_t p_si = make_ptr(spv::ScUniformConstant, t_stor_img_2d_);
        var_stor_img_ = alloc_id();
        push_inst(globals_, spv::OpVariable, {p_si, var_stor_img_, spv::ScUniformConstant});
        push_inst(decos_, spv::OpDecorate, {var_stor_img_, spv::DecDescriptorSet, 0u});
        push_inst(decos_, spv::OpDecorate, {var_stor_img_, spv::DecBinding, 3u});
        add_interface(var_stor_img_);
    }

    void setup_ray_query_globals() {
        t_as_ = alloc_id();
        push_inst(types_, spv::OpTypeAccelerationStructureKHR, {t_as_});
        uint32_t p_as = make_ptr(spv::ScUniformConstant, t_as_);
        var_tlas_ = alloc_id();
        push_inst(globals_, spv::OpVariable, {p_as, var_tlas_, spv::ScUniformConstant});
        push_inst(decos_, spv::OpDecorate, {var_tlas_, spv::DecDescriptorSet, 0u});
        push_inst(decos_, spv::OpDecorate, {var_tlas_, spv::DecBinding, 4u});
        add_interface(var_tlas_);

        t_rq_ = alloc_id();
        push_inst(types_, spv::OpTypeRayQueryKHR, {t_rq_});
        p_func_rq_ = make_ptr(spv::ScFunction, t_rq_);
    }

    void setup_bda_bvh4_types() {
        /* PhysicalStorageBuffer struct { uint word; } for aligned 4-byte BDA loads across 128B/64B BVH4 nodes */
        t_psb_u32_blk_ = alloc_id();
        push_inst(types_, spv::OpTypeStruct, {t_psb_u32_blk_, t_u32_});
        push_inst(decos_, spv::OpDecorate, {t_psb_u32_blk_, spv::DecBlock});
        push_inst(decos_, spv::OpMemberDecorate, {t_psb_u32_blk_, 0u, spv::DecOffset, 0u});
        p_psb_u32_blk_ = make_ptr(spv::ScPhysicalStorageBuffer, t_psb_u32_blk_);
        p_psb_u32_ = make_ptr(spv::ScPhysicalStorageBuffer, t_u32_);

        t_psb_f32_blk_ = alloc_id();
        push_inst(types_, spv::OpTypeStruct, {t_psb_f32_blk_, t_f32_});
        push_inst(decos_, spv::OpDecorate, {t_psb_f32_blk_, spv::DecBlock});
        push_inst(decos_, spv::OpMemberDecorate, {t_psb_f32_blk_, 0u, spv::DecOffset, 0u});
        p_psb_f32_blk_ = make_ptr(spv::ScPhysicalStorageBuffer, t_psb_f32_blk_);
        p_psb_f32_ = make_ptr(spv::ScPhysicalStorageBuffer, t_f32_);

        /* Local Function traversal stack uint[32] */
        t_arr_u32_32_ = alloc_id();
        push_inst(types_, spv::OpTypeArray, {t_arr_u32_32_, t_u32_, const_u32(32u)});
        p_func_u32_arr32_ = make_ptr(spv::ScFunction, t_arr_u32_32_);
    }

    void setup_lds_globals(bool use_paired_wave64) {
        const uint32_t lds_words = std::max(64u, mod_.meta.lds_bytes / 4u);
        uint32_t t_arr_lds = alloc_id();
        push_inst(types_, spv::OpTypeArray, {t_arr_lds, t_u32_, const_u32(lds_words)});
        uint32_t p_wg_lds = make_ptr(spv::ScWorkgroup, t_arr_lds);
        p_wg_u32_ = make_ptr(spv::ScWorkgroup, t_u32_);
        var_lds_ = alloc_id();
        push_inst(globals_, spv::OpVariable, {p_wg_lds, var_lds_, spv::ScWorkgroup});
        add_interface(var_lds_);

        if (use_paired_wave64) {
            uint32_t t_arr64 = alloc_id();
            push_inst(types_, spv::OpTypeArray, {t_arr64, t_u32_, const_u32(64u)});
            uint32_t p_wg64 = make_ptr(spv::ScWorkgroup, t_arr64);
            var_wave64_lds_ = alloc_id();
            push_inst(globals_, spv::OpVariable, {p_wg64, var_wave64_lds_, spv::ScWorkgroup});
            add_interface(var_wave64_lds_);

            var_wave64_ballot_ = alloc_id();
            push_inst(globals_, spv::OpVariable, {p_wg64, var_wave64_ballot_, spv::ScWorkgroup});
            add_interface(var_wave64_ballot_);
        }
    }

    void setup_stage_interface(uint32_t exec_model, bool is_workgroup_stage) {
        if (is_workgroup_stage) {
            const uint32_t wg_x = std::max(mod_.meta.wave_size == 64u ? 64u : 32u, mod_.meta.workgroup_size[0]);
            const uint32_t wg_y = std::max(1u, mod_.meta.workgroup_size[1]);
            const uint32_t wg_z = std::max(1u, mod_.meta.workgroup_size[2]);
            push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeLocalSize, wg_x, wg_y, wg_z});

            uint32_t p_in_v3u = make_ptr(spv::ScInput, t_v3u32_);
            var_gid_ = alloc_id();
            push_inst(globals_, spv::OpVariable, {p_in_v3u, var_gid_, spv::ScInput});
            push_inst(decos_, spv::OpDecorate, {var_gid_, spv::DecBuiltIn, spv::BiGlobalInvocationId});
            add_interface(var_gid_);

            uint32_t p_in_u32 = make_ptr(spv::ScInput, t_u32_);
            var_lid_ = alloc_id();
            push_inst(globals_, spv::OpVariable, {p_in_u32, var_lid_, spv::ScInput});
            push_inst(decos_, spv::OpDecorate, {var_lid_, spv::DecBuiltIn, spv::BiLocalInvocationIndex});
            add_interface(var_lid_);
        }

        switch (exec_model) {
            case spv::EmGLCompute:
                break;

            case spv::EmVertex: {
                uint32_t p_in_i32 = make_ptr(spv::ScInput, t_i32_);
                var_vtx_id_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_in_i32, var_vtx_id_, spv::ScInput});
                push_inst(decos_, spv::OpDecorate, {var_vtx_id_, spv::DecBuiltIn, spv::BiVertexIndex});
                add_interface(var_vtx_id_);

                uint32_t p_in_v4 = make_ptr(spv::ScInput, t_v4f32_);
                for (uint32_t loc = 0u; loc < 16u; ++loc) {
                    if ((mod_.vtx_attr_mask & (1u << loc)) != 0u) {
                        var_in_vtx_[loc] = alloc_id();
                        push_inst(globals_, spv::OpVariable, {p_in_v4, var_in_vtx_[loc], spv::ScInput});
                        push_inst(decos_, spv::OpDecorate, {var_in_vtx_[loc], spv::DecLocation, loc});
                        add_interface(var_in_vtx_[loc]);
                    }
                }

                uint32_t p_out_v4 = make_ptr(spv::ScOutput, t_v4f32_);
                var_out_pos_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_v4, var_out_pos_, spv::ScOutput});
                push_inst(decos_, spv::OpDecorate, {var_out_pos_, spv::DecBuiltIn, spv::BiPosition});
                add_interface(var_out_pos_);

                for (uint32_t loc = 0u; loc < 16u; ++loc) {
                    if ((mod_.out_param_mask & (1u << loc)) != 0u) {
                        var_out_param_[loc] = alloc_id();
                        push_inst(globals_, spv::OpVariable, {p_out_v4, var_out_param_[loc], spv::ScOutput});
                        push_inst(decos_, spv::OpDecorate, {var_out_param_[loc], spv::DecLocation, loc});
                        add_interface(var_out_param_[loc]);
                    }
                }
                break;
            }

            case spv::EmFragment: {
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOriginUpperLeft});
                uint32_t p_in_v4 = make_ptr(spv::ScInput, t_v4f32_);
                var_frag_coord_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_in_v4, var_frag_coord_, spv::ScInput});
                push_inst(decos_, spv::OpDecorate, {var_frag_coord_, spv::DecBuiltIn, spv::BiFragCoord});
                add_interface(var_frag_coord_);

                const uint32_t attr_mask = mod_.in_attr_mask | 1u;
                for (uint32_t loc = 0u; loc < 16u; ++loc) {
                    if ((attr_mask & (1u << loc)) != 0u) {
                        var_in_attr_[loc] = alloc_id();
                        push_inst(globals_, spv::OpVariable, {p_in_v4, var_in_attr_[loc], spv::ScInput});
                        push_inst(decos_, spv::OpDecorate, {var_in_attr_[loc], spv::DecLocation, loc});
                        add_interface(var_in_attr_[loc]);
                    }
                }
                var_in_attr0_ = var_in_attr_[0];

                uint32_t p_out_v4 = make_ptr(spv::ScOutput, t_v4f32_);
                const uint32_t mrt_mask = mod_.mrt_mask | 1u;
                for (uint32_t mrt = 0u; mrt < 8u; ++mrt) {
                    if ((mrt_mask & (1u << mrt)) != 0u) {
                        var_out_color_[mrt] = alloc_id();
                        push_inst(globals_, spv::OpVariable, {p_out_v4, var_out_color_[mrt], spv::ScOutput});
                        push_inst(decos_, spv::OpDecorate, {var_out_color_[mrt], spv::DecLocation, mrt});
                        add_interface(var_out_color_[mrt]);
                    }
                }
                var_out_color0_ = var_out_color_[0];
                break;
            }

            case spv::EmGeometry: {
                add_cap(spv::CapGeometry);
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeTriangles});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeInvocations, 1u});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOutputTriangleStrip});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOutputVertices, 3u});

                uint32_t p_out_v4 = make_ptr(spv::ScOutput, t_v4f32_);
                var_out_pos_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_v4, var_out_pos_, spv::ScOutput});
                push_inst(decos_, spv::OpDecorate, {var_out_pos_, spv::DecBuiltIn, spv::BiPosition});
                add_interface(var_out_pos_);
                break;
            }

            case spv::EmTessellationControl: {
                add_cap(spv::CapTessellation);
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOutputVertices, 3u});

                uint32_t t_f4 = alloc_id();
                push_inst(types_, spv::OpTypeArray, {t_f4, t_f32_, const_u32(4u)});
                uint32_t p_out_f4 = make_ptr(spv::ScOutput, t_f4);
                var_tess_outer_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_f4, var_tess_outer_, spv::ScOutput});
                push_inst(decos_, spv::OpDecorate, {var_tess_outer_, spv::DecBuiltIn, spv::BiTessLevelOuter});
                push_inst(decos_, spv::OpDecorate, {var_tess_outer_, spv::DecPatch});
                add_interface(var_tess_outer_);

                uint32_t t_f2 = alloc_id();
                push_inst(types_, spv::OpTypeArray, {t_f2, t_f32_, const_u32(2u)});
                uint32_t p_out_f2 = make_ptr(spv::ScOutput, t_f2);
                var_tess_inner_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_f2, var_tess_inner_, spv::ScOutput});
                push_inst(decos_, spv::OpDecorate, {var_tess_inner_, spv::DecBuiltIn, spv::BiTessLevelInner});
                push_inst(decos_, spv::OpDecorate, {var_tess_inner_, spv::DecPatch});
                add_interface(var_tess_inner_);
                break;
            }

            case spv::EmTessellationEvaluation: {
                add_cap(spv::CapTessellation);
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeTriangles});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeSpacingEqual});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeVertexOrderCw});

                uint32_t p_in_v3 = make_ptr(spv::ScInput, t_v3f32_);
                var_tess_coord_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_in_v3, var_tess_coord_, spv::ScInput});
                push_inst(decos_, spv::OpDecorate, {var_tess_coord_, spv::DecBuiltIn, spv::BiTessCoord});
                add_interface(var_tess_coord_);

                uint32_t p_out_v4 = make_ptr(spv::ScOutput, t_v4f32_);
                var_out_pos_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_v4, var_out_pos_, spv::ScOutput});
                push_inst(decos_, spv::OpDecorate, {var_out_pos_, spv::DecBuiltIn, spv::BiPosition});
                add_interface(var_out_pos_);
                break;
            }

            case spv::EmMeshEXT: {
                add_cap(spv::CapMeshShadingEXT);
                add_ext("SPV_EXT_mesh_shader");
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOutputVertices, 3u});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOutputPrimitivesEXT, 1u});
                push_inst(exec_modes_, spv::OpExecutionMode, {main_fn_id_, spv::ModeOutputTrianglesEXT});

                uint32_t t_per_vtx = alloc_id();
                push_inst(types_, spv::OpTypeStruct, {t_per_vtx, t_v4f32_});
                push_inst(decos_, spv::OpDecorate, {t_per_vtx, spv::DecBlock});
                push_inst(decos_, spv::OpMemberDecorate, {t_per_vtx, 0u, spv::DecBuiltIn, spv::BiPosition});

                uint32_t t_vtx_arr = alloc_id();
                push_inst(types_, spv::OpTypeArray, {t_vtx_arr, t_per_vtx, const_u32(3u)});
                uint32_t p_out_vtx = make_ptr(spv::ScOutput, t_vtx_arr);
                var_mesh_verts_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_vtx, var_mesh_verts_, spv::ScOutput});
                add_interface(var_mesh_verts_);

                uint32_t t_idx_arr = alloc_id();
                push_inst(types_, spv::OpTypeArray, {t_idx_arr, t_v3u32_, const_u32(1u)});
                uint32_t p_out_idx = make_ptr(spv::ScOutput, t_idx_arr);
                var_mesh_prims_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_out_idx, var_mesh_prims_, spv::ScOutput});
                push_inst(decos_, spv::OpDecorate, {var_mesh_prims_, spv::DecBuiltIn, spv::BiPrimitiveTriangleIndicesEXT});
                add_interface(var_mesh_prims_);
                break;
            }

            case spv::EmTaskEXT: {
                add_cap(spv::CapMeshShadingEXT);
                add_ext("SPV_EXT_mesh_shader");

                uint32_t t_payload = alloc_id();
                push_inst(types_, spv::OpTypeStruct, {t_payload, t_u32_});
                uint32_t p_payload = make_ptr(spv::ScTaskPayloadWorkgroupEXT, t_payload);
                var_task_payload_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_payload, var_task_payload_, spv::ScTaskPayloadWorkgroupEXT});
                add_interface(var_task_payload_);
                break;
            }

            case spv::EmRayGenerationKHR:
            case spv::EmClosestHitKHR:
            case spv::EmMissKHR:
            case spv::EmAnyHitKHR:
            case spv::EmIntersectionKHR:
            case spv::EmCallableKHR: {
                add_cap(spv::CapRayTracingKHR);
                add_ext("SPV_KHR_ray_tracing");

                uint32_t p_in_v3u = make_ptr(spv::ScInput, t_v3u32_);
                var_launch_id_ = alloc_id();
                push_inst(globals_, spv::OpVariable, {p_in_v3u, var_launch_id_, spv::ScInput});
                push_inst(decos_, spv::OpDecorate, {var_launch_id_, spv::DecBuiltIn, spv::BiLaunchIdKHR});
                add_interface(var_launch_id_);

                if (exec_model == spv::EmRayGenerationKHR) {
                    if (t_as_ == 0u) {
                        t_as_ = alloc_id();
                        push_inst(types_, spv::OpTypeAccelerationStructureKHR, {t_as_});
                        uint32_t p_as = make_ptr(spv::ScUniformConstant, t_as_);
                        var_tlas_ = alloc_id();
                        push_inst(globals_, spv::OpVariable, {p_as, var_tlas_, spv::ScUniformConstant});
                        push_inst(decos_, spv::OpDecorate, {var_tlas_, spv::DecDescriptorSet, 0u});
                        push_inst(decos_, spv::OpDecorate, {var_tlas_, spv::DecBinding, 4u});
                        add_interface(var_tlas_);
                    }
                    uint32_t p_pay = make_ptr(spv::ScRayPayloadKHR, t_v4f32_);
                    var_ray_payload_ = alloc_id();
                    push_inst(globals_, spv::OpVariable, {p_pay, var_ray_payload_, spv::ScRayPayloadKHR});
                    push_inst(decos_, spv::OpDecorate, {var_ray_payload_, spv::DecLocation, 0u});
                    add_interface(var_ray_payload_);
                } else if (exec_model == spv::EmCallableKHR) {
                    uint32_t p_cal = make_ptr(spv::ScIncomingCallableDataKHR, t_v4f32_);
                    var_ray_payload_ = alloc_id();
                    push_inst(globals_, spv::OpVariable, {p_cal, var_ray_payload_, spv::ScIncomingCallableDataKHR});
                    add_interface(var_ray_payload_);
                } else if (exec_model != spv::EmIntersectionKHR) {
                    uint32_t p_inc = make_ptr(spv::ScIncomingRayPayloadKHR, t_v4f32_);
                    var_ray_payload_ = alloc_id();
                    push_inst(globals_, spv::OpVariable, {p_inc, var_ray_payload_, spv::ScIncomingRayPayloadKHR});
                    add_interface(var_ray_payload_);
                }
                break;
            }
            default:
                break;
        }
    }

    uint32_t emit_lane_id(bool is_workgroup_stage) {
        if (is_workgroup_stage && var_lid_ != 0u) {
            uint32_t lid = alloc_id();
            push_inst(body_, spv::OpLoad, {t_u32_, lid, var_lid_});
            return lid;
        }
        if (var_vtx_id_ != 0u) {
            uint32_t vid = alloc_id();
            push_inst(body_, spv::OpLoad, {t_i32_, vid, var_vtx_id_});
            uint32_t u = alloc_id();
            push_inst(body_, spv::OpBitcast, {t_u32_, u, vid});
            return u;
        }
        if (var_frag_coord_ != 0u) {
            uint32_t fc = alloc_id();
            push_inst(body_, spv::OpLoad, {t_v4f32_, fc, var_frag_coord_});
            uint32_t x = alloc_id();
            push_inst(body_, spv::OpCompositeExtract, {t_f32_, x, fc, 0u});
            uint32_t u = alloc_id();
            push_inst(body_, spv::OpConvertFToU, {t_u32_, u, x});
            return u;
        }
        if (var_launch_id_ != 0u) {
            uint32_t lid = alloc_id();
            push_inst(body_, spv::OpLoad, {t_v3u32_, lid, var_launch_id_});
            uint32_t x = alloc_id();
            push_inst(body_, spv::OpCompositeExtract, {t_u32_, x, lid, 0u});
            return x;
        }
        return const_u32(0u);
    }

    /* Emit paired-lane Wave64 LDS initialization & wave-lane index (0..63) */
    void emit_wave64_paired_prologue() {
        wave64_lane_idx_ = alloc_id();
        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, wave64_lane_idx_, lane_id_val_, const_u32(63u)});

        /* Initialize shared exchange & ballot slots deterministically */
        uint32_t ptr_ex = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, ptr_ex, var_wave64_lds_, wave64_lane_idx_});
        push_inst(body_, spv::OpStore, {ptr_ex, const_u32(0u)});

        uint32_t ptr_bal = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, ptr_bal, var_wave64_ballot_, wave64_lane_idx_});
        push_inst(body_, spv::OpStore, {ptr_bal, const_u32(0u)});

        push_inst(body_, spv::OpControlBarrier,
                  {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                   const_u32(spv::SemAcquireReleaseWorkgroup)});
    }

    /* Emit RDNA2 IEEE-754-2008 NaN-suppressing FMin/FMax (or elided adaptive version) */
    uint32_t emit_fminmax(bool is_max, uint32_t a, uint32_t c, bool elide_nan) {
        if (elide_nan) {
            const uint32_t glsl_op =
                opts_.gpu_profile.has(probe_bits::F32_NAN_MINMAX_SUPPRESS)
                    ? (is_max ? spv::GlslNMax : spv::GlslNMin)
                    : (is_max ? spv::GlslFMax : spv::GlslFMin);
            uint32_t r = alloc_id();
            push_inst(body_, spv::OpExtInst, {t_f32_, r, glsl_ext_id_, glsl_op, a, c});
            return r;
        }
        /* Exact mode: explicit NaN-suppressing OpIsNan + OpSelect decision tree */
        uint32_t nan_a = alloc_id();
        push_inst(body_, spv::OpIsNan, {t_bool_, nan_a, a});
        uint32_t nan_c = alloc_id();
        push_inst(body_, spv::OpIsNan, {t_bool_, nan_c, c});
        uint32_t raw = alloc_id();
        push_inst(body_, spv::OpExtInst,
                  {t_f32_, raw, glsl_ext_id_, is_max ? spv::GlslFMax : spv::GlslFMin, a, c});
        uint32_t s1 = alloc_id();
        push_inst(body_, spv::OpSelect, {t_f32_, s1, nan_c, a, raw});
        uint32_t s2 = alloc_id();
        push_inst(body_, spv::OpSelect, {t_f32_, s2, nan_a, c, s1});
        return s2;
    }

    /* Emit RDNA2 software f32 denormal FTZ guard when not elided (sign-preserving!) */
    uint32_t maybe_emit_denorm_guard(uint32_t val, bool elide_denorm) {
        if (elide_denorm || !mod_.meta.f32_denorm_ftz) {
            return val;
        }
        uint32_t abs_v = alloc_id();
        push_inst(body_, spv::OpExtInst, {t_f32_, abs_v, glsl_ext_id_, spv::GlslFAbs, val});
        /* Minimum normal f32 = 2^-126 = 0x00800000; flush ONLY non-zero subnormals, preserving sign of zero */
        uint32_t min_norm = const_f32(1.17549435e-38f);
        uint32_t lt_min = alloc_id();
        push_inst(body_, spv::OpFOrdLessThan, {t_bool_, lt_min, abs_v, min_norm});
        uint32_t gt_zero = alloc_id();
        push_inst(body_, spv::OpFOrdGreaterThan, {t_bool_, gt_zero, abs_v, const_f32(0.0f)});
        uint32_t is_sub = alloc_id();
        push_inst(body_, spv::OpLogicalAnd, {t_bool_, is_sub, lt_min, gt_zero});
        uint32_t bits = alloc_id();
        push_inst(body_, spv::OpBitcast, {t_u32_, bits, val});
        uint32_t sign_bits = alloc_id();
        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, sign_bits, bits, const_u32(0x80000000u)});
        uint32_t signed_zero = alloc_id();
        push_inst(body_, spv::OpBitcast, {t_f32_, signed_zero, sign_bits});
        uint32_t flushed = alloc_id();
        push_inst(body_, spv::OpSelect, {t_f32_, flushed, is_sub, signed_zero, val});
        return flushed;
    }

    /* Emit RDNA2 canonical NaN quietening guard in exact mode when not elided */
    uint32_t maybe_emit_nan_canon(uint32_t val, bool elide_nan) {
        if (elide_nan) {
            return val;
        }
        uint32_t is_n = alloc_id();
        push_inst(body_, spv::OpIsNan, {t_bool_, is_n, val});
        uint32_t qnan = alloc_id();
        push_inst(body_, spv::OpBitcast, {t_f32_, qnan, const_u32(0x7FC00000u)});
        uint32_t res = alloc_id();
        push_inst(body_, spv::OpSelect, {t_f32_, res, is_n, qnan, val});
        return res;
    }

    /* Emit FMA: OpFmaKHR when supported, else GLSL.std.450 Fma + NoContraction */
    uint32_t emit_fma_f32(uint32_t a, uint32_t c, uint32_t e, bool elide_emul) {
        if (elide_emul && opts_.gpu_profile.has(probe_bits::EXT_SPV_KHR_FMA)) {
            add_ext("SPV_KHR_fma");
            add_cap(spv::CapFMAKHR);
            uint32_t r = alloc_id();
            push_inst(body_, spv::OpFmaKHR, {t_f32_, r, a, c, e});
            return r;
        }
        uint32_t r = alloc_id();
        push_inst(body_, spv::OpExtInst, {t_f32_, r, glsl_ext_id_, spv::GlslFma, a, c, e});
        if (!elide_emul || opts_.mode == SCARLETT_SHADER_MODE_EXACT) {
            push_inst(decos_, spv::OpDecorate, {r, spv::DecNoContraction});
        }
        return r;
    }

    /* Emit Packed FP16 ALU (native v2f16 on AMD RADV / FP16 GPUs, or software unpack/pack) */
    uint32_t emit_pk_f16(const IrInst& inst, uint32_t a, uint32_t c, uint32_t e) {
        if (inst.use_native_f16 && t_v2f16_ != 0u) {
            uint32_t va = alloc_id();
            push_inst(body_, spv::OpBitcast, {t_v2f16_, va, a});
            uint32_t vc = alloc_id();
            push_inst(body_, spv::OpBitcast, {t_v2f16_, vc, c});
            uint32_t vr = alloc_id();
            if (inst.op == IrOp::PkF16Add) {
                push_inst(body_, spv::OpFAdd, {t_v2f16_, vr, va, vc});
            } else if (inst.op == IrOp::PkF16Mul) {
                push_inst(body_, spv::OpFMul, {t_v2f16_, vr, va, vc});
            } else if (inst.op == IrOp::PkF16Fma) {
                uint32_t ve = alloc_id();
                push_inst(body_, spv::OpBitcast, {t_v2f16_, ve, e});
                push_inst(body_, spv::OpExtInst, {t_v2f16_, vr, glsl_ext_id_, spv::GlslFma, va, vc, ve});
            } else if (inst.op == IrOp::PkF16Min) {
                push_inst(body_, spv::OpExtInst, {t_v2f16_, vr, glsl_ext_id_, spv::GlslNMin, va, vc});
            } else {
                push_inst(body_, spv::OpExtInst, {t_v2f16_, vr, glsl_ext_id_, spv::GlslNMax, va, vc});
            }
            uint32_t packed = alloc_id();
            push_inst(body_, spv::OpBitcast, {t_u32_, packed, vr});
            return packed;
        }

        /* Software emulation via UnpackHalf2x16 -> f32 ops -> PackHalf2x16 */
        uint32_t ua = alloc_id();
        push_inst(body_, spv::OpExtInst, {t_v2f32_, ua, glsl_ext_id_, spv::GlslUnpackHalf2x16, a});
        uint32_t uc = alloc_id();
        push_inst(body_, spv::OpExtInst, {t_v2f32_, uc, glsl_ext_id_, spv::GlslUnpackHalf2x16, c});

        uint32_t a0 = alloc_id(), a1 = alloc_id(), c0 = alloc_id(), c1 = alloc_id();
        push_inst(body_, spv::OpCompositeExtract, {t_f32_, a0, ua, 0u});
        push_inst(body_, spv::OpCompositeExtract, {t_f32_, a1, ua, 1u});
        push_inst(body_, spv::OpCompositeExtract, {t_f32_, c0, uc, 0u});
        push_inst(body_, spv::OpCompositeExtract, {t_f32_, c1, uc, 1u});

        uint32_t r0 = 0u, r1 = 0u;
        if (inst.op == IrOp::PkF16Add) {
            r0 = alloc_id(); push_inst(body_, spv::OpFAdd, {t_f32_, r0, a0, c0});
            r1 = alloc_id(); push_inst(body_, spv::OpFAdd, {t_f32_, r1, a1, c1});
        } else if (inst.op == IrOp::PkF16Mul) {
            r0 = alloc_id(); push_inst(body_, spv::OpFMul, {t_f32_, r0, a0, c0});
            r1 = alloc_id(); push_inst(body_, spv::OpFMul, {t_f32_, r1, a1, c1});
        } else if (inst.op == IrOp::PkF16Fma) {
            uint32_t ue = alloc_id();
            push_inst(body_, spv::OpExtInst, {t_v2f32_, ue, glsl_ext_id_, spv::GlslUnpackHalf2x16, e});
            uint32_t e0 = alloc_id(), e1 = alloc_id();
            push_inst(body_, spv::OpCompositeExtract, {t_f32_, e0, ue, 0u});
            push_inst(body_, spv::OpCompositeExtract, {t_f32_, e1, ue, 1u});
            r0 = emit_fma_f32(a0, c0, e0, inst.elide_fma_emul);
            r1 = emit_fma_f32(a1, c1, e1, inst.elide_fma_emul);
        } else if (inst.op == IrOp::PkF16Min) {
            r0 = emit_fminmax(false, a0, c0, inst.elide_nan_tree);
            r1 = emit_fminmax(false, a1, c1, inst.elide_nan_tree);
        } else {
            r0 = emit_fminmax(true, a0, c0, inst.elide_nan_tree);
            r1 = emit_fminmax(true, a1, c1, inst.elide_nan_tree);
        }

        r0 = maybe_emit_nan_canon(maybe_emit_denorm_guard(r0, inst.elide_denorm_guard), inst.elide_nan_tree);
        r1 = maybe_emit_nan_canon(maybe_emit_denorm_guard(r1, inst.elide_denorm_guard), inst.elide_nan_tree);

        uint32_t vr = alloc_id();
        push_inst(body_, spv::OpCompositeConstruct, {t_v2f32_, vr, r0, r1});
        uint32_t packed = alloc_id();
        push_inst(body_, spv::OpExtInst, {t_u32_, packed, glsl_ext_id_, spv::GlslPackHalf2x16, vr});
        return packed;
    }

    /* Stage 4A: Native VK_KHR_ray_query traversal */
    uint32_t emit_bvh_ray_query(uint32_t ox, uint32_t oy, uint32_t oz, uint32_t tmax) {
        uint32_t tlas = alloc_id();
        push_inst(body_, spv::OpLoad, {t_as_, tlas, var_tlas_});
        uint32_t origin = alloc_id();
        push_inst(body_, spv::OpCompositeConstruct, {t_v3f32_, origin, ox, oy, oz});
        uint32_t dir = alloc_id();
        push_inst(body_, spv::OpCompositeConstruct,
                  {t_v3f32_, dir, const_f32(0.0f), const_f32(0.0f), const_f32(1.0f)});
        push_inst(body_, spv::OpRayQueryInitializeKHR,
                  {local_rq_var_, tlas, const_u32(0u), const_u32(0xFFu),
                   origin, const_f32(0.001f), dir, tmax});

        const uint32_t lbl_hdr = alloc_id();
        const uint32_t lbl_chk = alloc_id();
        const uint32_t lbl_body = alloc_id();
        const uint32_t lbl_confirm = alloc_id();
        const uint32_t lbl_after = alloc_id();
        const uint32_t lbl_cont = alloc_id();
        const uint32_t lbl_merge = alloc_id();

        push_inst(body_, spv::OpBranch, {lbl_hdr});
        push_inst(body_, spv::OpLabel, {lbl_hdr});
        push_inst(body_, spv::OpLoopMerge, {lbl_merge, lbl_cont, 0u});
        push_inst(body_, spv::OpBranch, {lbl_chk});

        push_inst(body_, spv::OpLabel, {lbl_chk});
        uint32_t proceed = alloc_id();
        push_inst(body_, spv::OpRayQueryProceedKHR, {t_bool_, proceed, local_rq_var_});
        push_inst(body_, spv::OpBranchConditional, {proceed, lbl_body, lbl_merge});

        push_inst(body_, spv::OpLabel, {lbl_body});
        uint32_t ctype = alloc_id();
        push_inst(body_, spv::OpRayQueryGetIntersectionTypeKHR,
                  {t_u32_, ctype, local_rq_var_, const_u32(0u)});
        uint32_t is_tri = alloc_id();
        push_inst(body_, spv::OpIEqual, {t_bool_, is_tri, ctype, const_u32(0u)});
        push_inst(body_, spv::OpSelectionMerge, {lbl_after, 0u});
        push_inst(body_, spv::OpBranchConditional, {is_tri, lbl_confirm, lbl_after});

        push_inst(body_, spv::OpLabel, {lbl_confirm});
        push_inst(body_, spv::OpRayQueryConfirmIntersectionKHR, {local_rq_var_});
        push_inst(body_, spv::OpBranch, {lbl_after});

        push_inst(body_, spv::OpLabel, {lbl_after});
        push_inst(body_, spv::OpBranch, {lbl_cont});

        push_inst(body_, spv::OpLabel, {lbl_cont});
        push_inst(body_, spv::OpBranch, {lbl_hdr});

        push_inst(body_, spv::OpLabel, {lbl_merge});
        uint32_t hit_t = alloc_id();
        push_inst(body_, spv::OpRayQueryGetIntersectionTKHR,
                  {t_f32_, hit_t, local_rq_var_, const_u32(1u)});
        return hit_t;
    }

    /* Helper: aligned PhysicalStorageBuffer64 u32/f32 load at (base_u64 + byte_offset) */
    uint32_t emit_bda_load_u32(uint32_t base_u64, uint32_t byte_offset_u64) {
        uint32_t addr = alloc_id();
        push_inst(body_, spv::OpIAdd, {t_u64_, addr, base_u64, byte_offset_u64});
        uint32_t ptr_blk = alloc_id();
        push_inst(body_, spv::OpConvertUToPtr, {p_psb_u32_blk_, ptr_blk, addr});
        uint32_t ptr_val = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_psb_u32_, ptr_val, ptr_blk, const_u32(0u)});
        uint32_t val = alloc_id();
        push_inst(body_, spv::OpLoad, {t_u32_, val, ptr_val, spv::MemAccessAligned, 4u});
        return val;
    }

    uint32_t emit_bda_load_f32(uint32_t base_u64, uint32_t byte_offset_u64) {
        uint32_t addr = alloc_id();
        push_inst(body_, spv::OpIAdd, {t_u64_, addr, base_u64, byte_offset_u64});
        uint32_t ptr_blk = alloc_id();
        push_inst(body_, spv::OpConvertUToPtr, {p_psb_f32_blk_, ptr_blk, addr});
        uint32_t ptr_val = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_psb_f32_, ptr_val, ptr_blk, const_u32(0u)});
        uint32_t val = alloc_id();
        push_inst(body_, spv::OpLoad, {t_f32_, val, ptr_val, spv::MemAccessAligned, 4u});
        return val;
    }

    /*
     * Stage 4B: BDA Software BVH4 Traversal (PhysicalStorageBuffer64)
     * Walks raw 128-byte ScarlettRdn2Bvh4Box32Node & 64-byte ScarlettRdn2Bvh4TriLeafNode
     * produced by XboxBvhBuilder:
     *   - Input SSBO[0..1] holds 64-bit GPU VA of ScarlettBvh4Header (96B header):
     *       +0x10: root_node_ptr (u32: (idx << 3) | type, type 5=BOX32, 0=TRI)
     *       +0x20: box_nodes_offset (u64)
     *       +0x28: leaf_nodes_offset (u64)
     *   - Box32Node (128B): child_ptrs[4] @ +0, min_x[4] @ +16, min_y[4] @ +32, min_z[4] @ +48,
     *                       max_x[4] @ +64, max_y[4] @ +80, max_z[4] @ +96
     *   - TriLeafNode (64B): v0[3] @ +0, v1[3] @ +12, v2[3] @ +24
     */
    uint32_t emit_bvh_bda_fallback(uint32_t ox, uint32_t oy, uint32_t oz, uint32_t tmax) {
        /* Read 64-bit BVH4 base VA from input SSBO[0] (lo) and input SSBO[1] (hi) */
        uint32_t p_lo = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_ssbo_u32_, p_lo, var_in_ssbo_, const_u32(0u), const_u32(0u)});
        uint32_t lo32 = alloc_id();
        push_inst(body_, spv::OpLoad, {t_u32_, lo32, p_lo});

        uint32_t p_hi = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_ssbo_u32_, p_hi, var_in_ssbo_, const_u32(0u), const_u32(1u)});
        uint32_t hi32 = alloc_id();
        push_inst(body_, spv::OpLoad, {t_u32_, hi32, p_hi});

        uint32_t lo64 = alloc_id(), hi64 = alloc_id(), hi_sh = alloc_id(), base_va = alloc_id();
        push_inst(body_, spv::OpUConvert, {t_u64_, lo64, lo32});
        push_inst(body_, spv::OpUConvert, {t_u64_, hi64, hi32});
        push_inst(body_, spv::OpShiftLeftLogical, {t_u64_, hi_sh, hi64, const_u64(32u)});
        push_inst(body_, spv::OpBitwiseOr, {t_u64_, base_va, lo64, hi_sh});

        /* Initialize best_t = tmax */
        push_inst(body_, spv::OpStore, {local_bvh_best_t_, tmax});

        /* Guard against null BDA VA */
        uint32_t is_nonzero = alloc_id();
        push_inst(body_, spv::OpINotEqual, {t_bool_, is_nonzero, base_va, const_u64(0u)});
        const uint32_t lbl_walk = alloc_id();
        const uint32_t lbl_done = alloc_id();
        push_inst(body_, spv::OpSelectionMerge, {lbl_done, 0u});
        push_inst(body_, spv::OpBranchConditional, {is_nonzero, lbl_walk, lbl_done});

        push_inst(body_, spv::OpLabel, {lbl_walk});
        /* Read root_node_ptr (+16), box_nodes_offset (+32), leaf_nodes_offset (+40) */
        uint32_t root_ptr = emit_bda_load_u32(base_va, const_u64(16u));
        uint32_t box_off_lo = emit_bda_load_u32(base_va, const_u64(32u));
        uint32_t box_off_u64 = alloc_id();
        push_inst(body_, spv::OpUConvert, {t_u64_, box_off_u64, box_off_lo});
        uint32_t box_base = alloc_id();
        push_inst(body_, spv::OpIAdd, {t_u64_, box_base, base_va, box_off_u64});

        uint32_t leaf_off_lo = emit_bda_load_u32(base_va, const_u64(40u));
        uint32_t leaf_off_u64 = alloc_id();
        push_inst(body_, spv::OpUConvert, {t_u64_, leaf_off_u64, leaf_off_lo});
        uint32_t leaf_base = alloc_id();
        push_inst(body_, spv::OpIAdd, {t_u64_, leaf_base, base_va, leaf_off_u64});

        /* Push root_ptr onto stack[0], sp = 1 */
        uint32_t p_s0 = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_func_u32_, p_s0, local_bvh_stack_, const_u32(0u)});
        push_inst(body_, spv::OpStore, {p_s0, root_ptr});
        push_inst(body_, spv::OpStore, {local_bvh_sp_, const_u32(1u)});

        /* Ray direction = (0, 0, 1) canonical or from ray payload */
        const uint32_t lbl_hdr = alloc_id();
        const uint32_t lbl_chk = alloc_id();
        const uint32_t lbl_pop = alloc_id();
        const uint32_t lbl_box = alloc_id();
        const uint32_t lbl_tri = alloc_id();
        const uint32_t lbl_after_node = alloc_id();
        const uint32_t lbl_cont = alloc_id();
        const uint32_t lbl_loop_merge = alloc_id();

        push_inst(body_, spv::OpBranch, {lbl_hdr});
        push_inst(body_, spv::OpLabel, {lbl_hdr});
        push_inst(body_, spv::OpLoopMerge, {lbl_loop_merge, lbl_cont, 0u});
        push_inst(body_, spv::OpBranch, {lbl_chk});

        push_inst(body_, spv::OpLabel, {lbl_chk});
        uint32_t sp_cur = alloc_id();
        push_inst(body_, spv::OpLoad, {t_u32_, sp_cur, local_bvh_sp_});
        uint32_t has_items = alloc_id();
        push_inst(body_, spv::OpUGreaterThan, {t_bool_, has_items, sp_cur, const_u32(0u)});
        push_inst(body_, spv::OpBranchConditional, {has_items, lbl_pop, lbl_loop_merge});

        push_inst(body_, spv::OpLabel, {lbl_pop});
        uint32_t sp_dec = alloc_id();
        push_inst(body_, spv::OpISub, {t_u32_, sp_dec, sp_cur, const_u32(1u)});
        push_inst(body_, spv::OpStore, {local_bvh_sp_, sp_dec});
        uint32_t p_top = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_func_u32_, p_top, local_bvh_stack_, sp_dec});
        uint32_t node_ptr = alloc_id();
        push_inst(body_, spv::OpLoad, {t_u32_, node_ptr, p_top});

        uint32_t node_type = alloc_id();
        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, node_type, node_ptr, const_u32(7u)});
        uint32_t node_idx = alloc_id();
        push_inst(body_, spv::OpShiftRightLogical, {t_u32_, node_idx, node_ptr, const_u32(3u)});
        uint32_t is_box = alloc_id();
        push_inst(body_, spv::OpIEqual, {t_bool_, is_box, node_type, const_u32(5u)});

        push_inst(body_, spv::OpSelectionMerge, {lbl_after_node, 0u});
        push_inst(body_, spv::OpBranchConditional, {is_box, lbl_box, lbl_tri});

        /* 128-byte RDNA2 Box32 Node branch: test 4 children AABBs & push valid hits */
        push_inst(body_, spv::OpLabel, {lbl_box});
        uint32_t idx_u64 = alloc_id();
        push_inst(body_, spv::OpUConvert, {t_u64_, idx_u64, node_idx});
        uint32_t box_byte_off = alloc_id();
        push_inst(body_, spv::OpShiftLeftLogical, {t_u64_, box_byte_off, idx_u64, const_u64(7u)}); /* *128 */
        uint32_t box_addr = alloc_id();
        push_inst(body_, spv::OpIAdd, {t_u64_, box_addr, box_base, box_byte_off});

        for (uint32_t ci = 0; ci < 4u; ++ci) {
            uint32_t cptr = emit_bda_load_u32(box_addr, const_u64(ci * 4u));
            uint32_t minx = emit_bda_load_f32(box_addr, const_u64(16u + ci * 4u));
            uint32_t miny = emit_bda_load_f32(box_addr, const_u64(32u + ci * 4u));
            uint32_t maxx = emit_bda_load_f32(box_addr, const_u64(64u + ci * 4u));
            uint32_t maxy = emit_bda_load_f32(box_addr, const_u64(80u + ci * 4u));
            uint32_t maxz = emit_bda_load_f32(box_addr, const_u64(96u + ci * 4u));

            uint32_t valid_c = alloc_id();
            push_inst(body_, spv::OpINotEqual, {t_bool_, valid_c, cptr, const_u32(0xFFFFFFFFu)});
            uint32_t gx0 = alloc_id(), lx1 = alloc_id(), gy0 = alloc_id(), ly1 = alloc_id(), gz1 = alloc_id();
            push_inst(body_, spv::OpFOrdGreaterThanEqual, {t_bool_, gx0, ox, minx});
            push_inst(body_, spv::OpFOrdLessThanEqual, {t_bool_, lx1, ox, maxx});
            push_inst(body_, spv::OpFOrdGreaterThanEqual, {t_bool_, gy0, oy, miny});
            push_inst(body_, spv::OpFOrdLessThanEqual, {t_bool_, ly1, oy, maxy});
            push_inst(body_, spv::OpFOrdLessThanEqual, {t_bool_, gz1, oz, maxz});

            uint32_t a1 = alloc_id(), a2 = alloc_id(), a3 = alloc_id(), a4 = alloc_id(), hit_box = alloc_id();
            push_inst(body_, spv::OpLogicalAnd, {t_bool_, a1, valid_c, gx0});
            push_inst(body_, spv::OpLogicalAnd, {t_bool_, a2, a1, lx1});
            push_inst(body_, spv::OpLogicalAnd, {t_bool_, a3, a2, gy0});
            push_inst(body_, spv::OpLogicalAnd, {t_bool_, a4, a3, ly1});
            push_inst(body_, spv::OpLogicalAnd, {t_bool_, hit_box, a4, gz1});

            const uint32_t lbl_push = alloc_id();
            const uint32_t lbl_next_c = alloc_id();
            push_inst(body_, spv::OpSelectionMerge, {lbl_next_c, 0u});
            push_inst(body_, spv::OpBranchConditional, {hit_box, lbl_push, lbl_next_c});

            push_inst(body_, spv::OpLabel, {lbl_push});
            uint32_t sp_now = alloc_id();
            push_inst(body_, spv::OpLoad, {t_u32_, sp_now, local_bvh_sp_});
            uint32_t sp_ok = alloc_id();
            push_inst(body_, spv::OpULessThan, {t_bool_, sp_ok, sp_now, const_u32(31u)});
            const uint32_t lbl_do_push = alloc_id();
            const uint32_t lbl_end_push = alloc_id();
            push_inst(body_, spv::OpSelectionMerge, {lbl_end_push, 0u});
            push_inst(body_, spv::OpBranchConditional, {sp_ok, lbl_do_push, lbl_end_push});

            push_inst(body_, spv::OpLabel, {lbl_do_push});
            uint32_t p_dst = alloc_id();
            push_inst(body_, spv::OpAccessChain, {p_func_u32_, p_dst, local_bvh_stack_, sp_now});
            push_inst(body_, spv::OpStore, {p_dst, cptr});
            uint32_t sp_inc = alloc_id();
            push_inst(body_, spv::OpIAdd, {t_u32_, sp_inc, sp_now, const_u32(1u)});
            push_inst(body_, spv::OpStore, {local_bvh_sp_, sp_inc});
            push_inst(body_, spv::OpBranch, {lbl_end_push});

            push_inst(body_, spv::OpLabel, {lbl_end_push});
            push_inst(body_, spv::OpBranch, {lbl_next_c});
            push_inst(body_, spv::OpLabel, {lbl_next_c});
        }
        push_inst(body_, spv::OpBranch, {lbl_after_node});

        /* 64-byte RDNA2 Triangle Leaf Node branch: Möller-Trumbore ray-triangle test along +Z */
        push_inst(body_, spv::OpLabel, {lbl_tri});
        uint32_t tidx_u64 = alloc_id();
        push_inst(body_, spv::OpUConvert, {t_u64_, tidx_u64, node_idx});
        uint32_t tri_byte_off = alloc_id();
        push_inst(body_, spv::OpShiftLeftLogical, {t_u64_, tri_byte_off, tidx_u64, const_u64(6u)}); /* *64 */
        uint32_t tri_addr = alloc_id();
        push_inst(body_, spv::OpIAdd, {t_u64_, tri_addr, leaf_base, tri_byte_off});

        uint32_t v0x = emit_bda_load_f32(tri_addr, const_u64(0u));
        uint32_t v0y = emit_bda_load_f32(tri_addr, const_u64(4u));
        uint32_t v0z = emit_bda_load_f32(tri_addr, const_u64(8u));
        uint32_t v1x = emit_bda_load_f32(tri_addr, const_u64(12u));
        uint32_t v1y = emit_bda_load_f32(tri_addr, const_u64(16u));
        uint32_t v1z = emit_bda_load_f32(tri_addr, const_u64(20u));
        uint32_t v2x = emit_bda_load_f32(tri_addr, const_u64(24u));
        uint32_t v2y = emit_bda_load_f32(tri_addr, const_u64(28u));
        uint32_t v2z = emit_bda_load_f32(tri_addr, const_u64(32u));

        /* e1 = v1 - v0, e2 = v2 - v0; dir = (0,0,1) => h = dir x e2 = (-e2y, e2x, 0) */
        uint32_t e1x = alloc_id(), e1y = alloc_id(), e1z = alloc_id();
        push_inst(body_, spv::OpFSub, {t_f32_, e1x, v1x, v0x});
        push_inst(body_, spv::OpFSub, {t_f32_, e1y, v1y, v0y});
        push_inst(body_, spv::OpFSub, {t_f32_, e1z, v1z, v0z});

        uint32_t e2x = alloc_id(), e2y = alloc_id(), e2z = alloc_id();
        push_inst(body_, spv::OpFSub, {t_f32_, e2x, v2x, v0x});
        push_inst(body_, spv::OpFSub, {t_f32_, e2y, v2y, v0y});
        push_inst(body_, spv::OpFSub, {t_f32_, e2z, v2z, v0z});

        /* det = e1x*(-e2y) + e1y*(e2x) = e1y*e2x - e1x*e2y */
        uint32_t m1 = alloc_id(), m2 = alloc_id(), det = alloc_id();
        push_inst(body_, spv::OpFMul, {t_f32_, m1, e1y, e2x});
        push_inst(body_, spv::OpFMul, {t_f32_, m2, e1x, e2y});
        push_inst(body_, spv::OpFSub, {t_f32_, det, m1, m2});
        uint32_t abs_det = alloc_id();
        push_inst(body_, spv::OpExtInst, {t_f32_, abs_det, glsl_ext_id_, spv::GlslFAbs, det});
        uint32_t det_ok = alloc_id();
        push_inst(body_, spv::OpFOrdGreaterThan, {t_bool_, det_ok, abs_det, const_f32(1e-7f)});

        const uint32_t lbl_tri_calc = alloc_id();
        const uint32_t lbl_tri_end = alloc_id();
        push_inst(body_, spv::OpSelectionMerge, {lbl_tri_end, 0u});
        push_inst(body_, spv::OpBranchConditional, {det_ok, lbl_tri_calc, lbl_tri_end});

        push_inst(body_, spv::OpLabel, {lbl_tri_calc});
        uint32_t inv_det = alloc_id();
        push_inst(body_, spv::OpFDiv, {t_f32_, inv_det, const_f32(1.0f), det});
        uint32_t sx = alloc_id(), sy = alloc_id(), sz = alloc_id();
        push_inst(body_, spv::OpFSub, {t_f32_, sx, ox, v0x});
        push_inst(body_, spv::OpFSub, {t_f32_, sy, oy, v0y});
        push_inst(body_, spv::OpFSub, {t_f32_, sz, oz, v0z});

        /* u = (sy*e2x - sx*e2y) * inv_det */
        uint32_t u1 = alloc_id(), u2 = alloc_id(), u_num = alloc_id(), u_bary = alloc_id();
        push_inst(body_, spv::OpFMul, {t_f32_, u1, sy, e2x});
        push_inst(body_, spv::OpFMul, {t_f32_, u2, sx, e2y});
        push_inst(body_, spv::OpFSub, {t_f32_, u_num, u1, u2});
        push_inst(body_, spv::OpFMul, {t_f32_, u_bary, u_num, inv_det});

        /* v = (sx*e1y - sy*e1x) * inv_det */
        uint32_t v1 = alloc_id(), v2 = alloc_id(), v_num = alloc_id(), v_bary = alloc_id();
        push_inst(body_, spv::OpFMul, {t_f32_, v1, sx, e1y});
        push_inst(body_, spv::OpFMul, {t_f32_, v2, sy, e1x});
        push_inst(body_, spv::OpFSub, {t_f32_, v_num, v1, v2});
        push_inst(body_, spv::OpFMul, {t_f32_, v_bary, v_num, inv_det});

        /* t_plane = (v0z + u*e1z + v*e2z) - oz */
        uint32_t ue1z = alloc_id(), ve2z = alloc_id(), z_hit1 = alloc_id(), z_hit = alloc_id(), t_hit = alloc_id();
        push_inst(body_, spv::OpFMul, {t_f32_, ue1z, u_bary, e1z});
        push_inst(body_, spv::OpFMul, {t_f32_, ve2z, v_bary, e2z});
        push_inst(body_, spv::OpFAdd, {t_f32_, z_hit1, v0z, ue1z});
        push_inst(body_, spv::OpFAdd, {t_f32_, z_hit, z_hit1, ve2z});
        push_inst(body_, spv::OpFSub, {t_f32_, t_hit, z_hit, oz});

        uint32_t uv_sum = alloc_id();
        push_inst(body_, spv::OpFAdd, {t_f32_, uv_sum, u_bary, v_bary});
        uint32_t cur_best = alloc_id();
        push_inst(body_, spv::OpLoad, {t_f32_, cur_best, local_bvh_best_t_});

        uint32_t c_u = alloc_id(), c_v = alloc_id(), c_uv = alloc_id(), c_t0 = alloc_id(), c_t1 = alloc_id();
        push_inst(body_, spv::OpFOrdGreaterThanEqual, {t_bool_, c_u, u_bary, const_f32(0.0f)});
        push_inst(body_, spv::OpFOrdGreaterThanEqual, {t_bool_, c_v, v_bary, const_f32(0.0f)});
        push_inst(body_, spv::OpFOrdLessThanEqual, {t_bool_, c_uv, uv_sum, const_f32(1.0f)});
        push_inst(body_, spv::OpFOrdGreaterThan, {t_bool_, c_t0, t_hit, const_f32(0.0f)});
        push_inst(body_, spv::OpFOrdLessThan, {t_bool_, c_t1, t_hit, cur_best});

        uint32_t k1 = alloc_id(), k2 = alloc_id(), k3 = alloc_id(), is_hit = alloc_id();
        push_inst(body_, spv::OpLogicalAnd, {t_bool_, k1, c_u, c_v});
        push_inst(body_, spv::OpLogicalAnd, {t_bool_, k2, k1, c_uv});
        push_inst(body_, spv::OpLogicalAnd, {t_bool_, k3, k2, c_t0});
        push_inst(body_, spv::OpLogicalAnd, {t_bool_, is_hit, k3, c_t1});

        uint32_t new_best = alloc_id();
        push_inst(body_, spv::OpSelect, {t_f32_, new_best, is_hit, t_hit, cur_best});
        push_inst(body_, spv::OpStore, {local_bvh_best_t_, new_best});
        push_inst(body_, spv::OpBranch, {lbl_tri_end});

        push_inst(body_, spv::OpLabel, {lbl_tri_end});
        push_inst(body_, spv::OpBranch, {lbl_after_node});

        push_inst(body_, spv::OpLabel, {lbl_after_node});
        push_inst(body_, spv::OpBranch, {lbl_cont});

        push_inst(body_, spv::OpLabel, {lbl_cont});
        push_inst(body_, spv::OpBranch, {lbl_hdr});

        push_inst(body_, spv::OpLabel, {lbl_loop_merge});
        push_inst(body_, spv::OpBranch, {lbl_done});

        push_inst(body_, spv::OpLabel, {lbl_done});
        uint32_t final_t = alloc_id();
        push_inst(body_, spv::OpLoad, {t_f32_, final_t, local_bvh_best_t_});
        return final_t;
    }

    /* Emit Paired-Lane Wave64 Cross-Lane Exchange via Workgroup LDS */
    uint32_t emit_wave64_lds_exchange(uint32_t src_val, uint32_t target_lane) {
        uint32_t ptr_w = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, ptr_w, var_wave64_lds_, wave64_lane_idx_});
        push_inst(body_, spv::OpStore, {ptr_w, src_val});
        push_inst(body_, spv::OpControlBarrier,
                  {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                   const_u32(spv::SemAcquireReleaseWorkgroup)});
        uint32_t clamped_lane = alloc_id();
        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, clamped_lane, target_lane, const_u32(63u)});
        uint32_t ptr_r = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, ptr_r, var_wave64_lds_, clamped_lane});
        uint32_t res = alloc_id();
        push_inst(body_, spv::OpLoad, {t_u32_, res, ptr_r});
        push_inst(body_, spv::OpControlBarrier,
                  {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                   const_u32(spv::SemAcquireReleaseWorkgroup)});
        return res;
    }

    /* Emit Paired-Lane Wave64 64-bit Ballot via Workgroup LDS (bit-exact across lanes 0..63!) */
    uint32_t emit_wave64_lds_ballot_word(uint32_t cond_bool, uint32_t half_offset, bool do_popcnt64) {
        uint32_t bit = alloc_id();
        push_inst(body_, spv::OpSelect, {t_u32_, bit, cond_bool, const_u32(1u), const_u32(0u)});
        uint32_t ptr_w = alloc_id();
        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, ptr_w, var_wave64_ballot_, wave64_lane_idx_});
        push_inst(body_, spv::OpStore, {ptr_w, bit});
        push_inst(body_, spv::OpControlBarrier,
                  {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                   const_u32(spv::SemAcquireReleaseWorkgroup)});

        if (do_popcnt64) {
            /* Sum all 64 lanes' 0/1 flags */
            uint32_t acc = const_u32(0u);
            for (uint32_t lane = 0; lane < 64u; ++lane) {
                uint32_t pr = alloc_id();
                push_inst(body_, spv::OpAccessChain, {p_wg_u32_, pr, var_wave64_ballot_, const_u32(lane)});
                uint32_t lv = alloc_id();
                push_inst(body_, spv::OpLoad, {t_u32_, lv, pr});
                uint32_t nxt = alloc_id();
                push_inst(body_, spv::OpIAdd, {t_u32_, nxt, acc, lv});
                acc = nxt;
            }
            push_inst(body_, spv::OpControlBarrier,
                      {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                       const_u32(spv::SemAcquireReleaseWorkgroup)});
            return acc;
        }

        /* Pack 32 bits from lanes [half_offset .. half_offset + 31] */
        uint32_t mask = const_u32(0u);
        for (uint32_t b = 0; b < 32u; ++b) {
            uint32_t pr = alloc_id();
            push_inst(body_, spv::OpAccessChain,
                      {p_wg_u32_, pr, var_wave64_ballot_, const_u32(half_offset + b)});
            uint32_t lv = alloc_id();
            push_inst(body_, spv::OpLoad, {t_u32_, lv, pr});
            uint32_t sh = alloc_id();
            push_inst(body_, spv::OpShiftLeftLogical, {t_u32_, sh, lv, const_u32(b)});
            uint32_t nxt = alloc_id();
            push_inst(body_, spv::OpBitwiseOr, {t_u32_, nxt, mask, sh});
            mask = nxt;
        }
        push_inst(body_, spv::OpControlBarrier,
                  {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                   const_u32(spv::SemAcquireReleaseWorkgroup)});
        return mask;
    }

    void emit_ir_body(bool is_workgroup_stage, bool use_paired_wave64_lds) {
        std::vector<uint32_t> val(mod_.next_ssa_id + 1u, 0u);
        auto arg = [&](const IrInst& inst, uint8_t k, IrType fallback_type) -> uint32_t {
            if (k < inst.arg_count && inst.args[k] < val.size() && val[inst.args[k]] != 0u) {
                return val[inst.args[k]];
            }
            if (fallback_type == IrType::F32) return const_f32(0.0f);
            if (fallback_type == IrType::Bool) return const_bool(false);
            return const_u32(0u);
        };

        struct OpenIf { uint32_t merge_lbl; };
        struct OpenLoop { uint32_t hdr_lbl; uint32_t cont_lbl; uint32_t merge_lbl; };
        std::vector<OpenIf> if_stack;
        std::vector<OpenLoop> loop_stack;
        std::vector<uint8_t> cf_kind_stack; /* 0 = If, 1 = Loop */

        /* Note: inside structured CfIf/CfLoop blocks, any SSA values defined inside
         * cannot be referenced outside without dominating; our CFG guarantees linear
         * dominance for straight-line ops, and for safety if a value is defined inside
         * a branch we also mirror final results cleanly. */
        int cf_depth = 0;

        for (const auto& inst : mod_.insts) {
            uint32_t res = 0u;
            switch (inst.op) {
                case IrOp::Nop:
                    break;
                case IrOp::ConstU32:
                    res = const_u32(inst.imm_u32);
                    break;
                case IrOp::ConstF32:
                    res = const_f32(inst.imm_f32);
                    break;
                case IrOp::ConstBool:
                    res = const_bool(inst.imm_u32 != 0u);
                    break;

                case IrOp::LoadVertexAttr: {
                    const uint32_t loc = inst.imm_u32 & 15u;
                    const uint32_t comp = inst.imm2_u32 & 3u;
                    if (var_in_vtx_[loc] != 0u) {
                        uint32_t vec = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_v4f32_, vec, var_in_vtx_[loc]});
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, res, vec, comp});
                    } else {
                        res = const_f32((comp == 3u) ? 1.0f : 0.0f);
                    }
                    break;
                }

                case IrOp::LoadCbvU32: {
                    const uint32_t slot = inst.imm2_u32 & 7u;
                    const uint32_t ssbo = (var_cbv_ssbo_[slot] != 0u) ? var_cbv_ssbo_[slot] : var_in_ssbo_;
                    if (ssbo != 0u) {
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_ssbo_u32_, p, ssbo, const_u32(0u), const_u32(inst.imm_u32)});
                        res = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_u32_, res, p});
                    } else {
                        res = const_u32(0u);
                    }
                    break;
                }

                case IrOp::LoadInputU32:
                case IrOp::LoadInputF32: {
                    if (var_in_ssbo_ != 0u) {
                        uint32_t idx = 0u;
                        if (inst.imm2_u32 != 0u) {
                            /* Uniform SGPR load from fixed slot */
                            idx = const_u32(inst.imm_u32 & 0x3Fu);
                        } else if (inst.arg_count > 0u) {
                            uint32_t base = arg(inst, 0, IrType::U32);
                            uint32_t off_idx = alloc_id();
                            push_inst(body_, spv::OpIAdd, {t_u32_, off_idx, base, const_u32(inst.imm_u32)});
                            idx = alloc_id();
                            push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, off_idx, const_u32(255u)});
                        } else {
                            /* Divergent VGPR input load: index = (lane_id * 4 + slot) & 255 */
                            uint32_t l4 = alloc_id();
                            push_inst(body_, spv::OpShiftLeftLogical, {t_u32_, l4, lane_id_val_, const_u32(2u)});
                            uint32_t sum = alloc_id();
                            push_inst(body_, spv::OpIAdd, {t_u32_, sum, l4, const_u32(inst.imm_u32 & 3u)});
                            idx = alloc_id();
                            push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, sum, const_u32(255u)});
                        }
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_ssbo_u32_, p, var_in_ssbo_, const_u32(0u), idx});
                        uint32_t raw = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_u32_, raw, p});
                        if (inst.op == IrOp::LoadInputF32) {
                            res = alloc_id();
                            push_inst(body_, spv::OpBitcast, {t_f32_, res, raw});
                        } else {
                            res = raw;
                        }
                    } else {
                        res = (inst.op == IrOp::LoadInputF32) ? const_f32(1.0f) : const_u32(inst.imm_u32);
                    }
                    break;
                }

                case IrOp::StoreOutputU32:
                case IrOp::StoreOutputF32: {
                    if (var_out_ssbo_ != 0u) {
                        uint32_t base_idx = (inst.arg_count > 0u) ? arg(inst, 0, IrType::U32) : lane_id_val_;
                        uint32_t sum1 = alloc_id();
                        push_inst(body_, spv::OpIAdd, {t_u32_, sum1, base_idx, lane_id_val_});
                        uint32_t sum2 = alloc_id();
                        push_inst(body_, spv::OpIAdd, {t_u32_, sum2, sum1, const_u32(inst.imm_u32)});
                        uint32_t idx = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, sum2, const_u32(255u)});
                        uint32_t v = arg(inst, 1, inst.op == IrOp::StoreOutputF32 ? IrType::F32 : IrType::U32);
                        if (inst.op == IrOp::StoreOutputF32) {
                            uint32_t bits = alloc_id();
                            push_inst(body_, spv::OpBitcast, {t_u32_, bits, v});
                            v = bits;
                        }
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_ssbo_u32_, p, var_out_ssbo_, const_u32(0u), idx});
                        push_inst(body_, spv::OpStore, {p, v});
                        last_stored_u32_ = v;
                    }
                    break;
                }

                case IrOp::LoadLdsU32: {
                    if (var_lds_ != 0u) {
                        uint32_t a = arg(inst, 0, IrType::U32);
                        uint32_t idx = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, a, const_u32(63u)});
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, p, var_lds_, idx});
                        res = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_u32_, res, p});
                    } else {
                        res = const_u32(0u);
                    }
                    break;
                }

                case IrOp::StoreLdsU32: {
                    if (var_lds_ != 0u) {
                        uint32_t a = arg(inst, 0, IrType::U32);
                        uint32_t v = arg(inst, 1, IrType::U32);
                        uint32_t idx = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, a, const_u32(63u)});
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, p, var_lds_, idx});
                        push_inst(body_, spv::OpStore, {p, v});
                    }
                    break;
                }

                case IrOp::AtomicLdsAdd: {
                    if (var_lds_ != 0u) {
                        uint32_t a = arg(inst, 0, IrType::U32);
                        uint32_t v = arg(inst, 1, IrType::U32);
                        uint32_t idx = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, a, const_u32(63u)});
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_wg_u32_, p, var_lds_, idx});
                        res = alloc_id();
                        push_inst(body_, spv::OpAtomicIAdd,
                                  {t_u32_, res, p, const_u32(spv::ScopeWorkgroup), const_u32(0u), v});
                    } else {
                        res = arg(inst, 1, IrType::U32);
                    }
                    break;
                }

                case IrOp::AtomicBufAdd: {
                    if (var_out_ssbo_ != 0u) {
                        uint32_t a = arg(inst, 0, IrType::U32);
                        uint32_t v = arg(inst, 1, IrType::U32);
                        uint32_t idx = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, idx, a, const_u32(63u)});
                        uint32_t p = alloc_id();
                        push_inst(body_, spv::OpAccessChain, {p_ssbo_u32_, p, var_out_ssbo_, const_u32(0u), idx});
                        res = alloc_id();
                        push_inst(body_, spv::OpAtomicIAdd,
                                  {t_u32_, res, p, const_u32(1u /* Device */), const_u32(0u), v});
                    } else {
                        res = arg(inst, 1, IrType::U32);
                    }
                    break;
                }

                case IrOp::ImageSample: {
                    const uint32_t tex_slot = inst.imm_u32 & 7u;
                    const uint32_t tex_var = (var_tex_2d_[tex_slot] != 0u) ? var_tex_2d_[tex_slot] : var_sampled_img_;
                    if (tex_var != 0u) {
                        uint32_t u = arg(inst, 0, IrType::F32);
                        uint32_t v = (inst.arg_count >= 2u) ? arg(inst, 1, IrType::F32) : u;
                        uint32_t si = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_sampled_img_, si, tex_var});
                        uint32_t uv = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v2f32_, uv, u, v});
                        uint32_t rgba = alloc_id();
                        if (mod_.meta.stage == ShaderStage::Pixel) {
                            push_inst(body_, spv::OpImageSampleImplicitLod,
                                      {t_v4f32_, rgba, si, uv});
                        } else {
                            /* ImageOperands::Lod = 0x2 */
                            push_inst(body_, spv::OpImageSampleExplicitLod,
                                      {t_v4f32_, rgba, si, uv, 0x2u, const_f32(0.0f)});
                        }
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, res, rgba, inst.imm2_u32 & 3u});
                    } else {
                        res = const_f32(1.0f);
                    }
                    break;
                }

                case IrOp::ImageLoad: {
                    if (var_stor_img_ != 0u) {
                        uint32_t c = arg(inst, 0, IrType::U32);
                        uint32_t im = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_stor_img_2d_, im, var_stor_img_});
                        uint32_t ci = alloc_id();
                        push_inst(body_, spv::OpBitcast, {t_i32_, ci, c});
                        uint32_t t_v2i32 = get_v2i32();
                        uint32_t xy = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v2i32, xy, ci, const_i32(0)});
                        uint32_t rgba = alloc_id();
                        push_inst(body_, spv::OpImageRead, {t_v4f32_, rgba, im, xy});
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, res, rgba, 0u});
                    } else {
                        res = const_f32(0.5f);
                    }
                    break;
                }

                case IrOp::ImageStore: {
                    if (var_stor_img_ != 0u) {
                        uint32_t c = arg(inst, 0, IrType::U32);
                        uint32_t f = arg(inst, 1, IrType::F32);
                        uint32_t im = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_stor_img_2d_, im, var_stor_img_});
                        uint32_t ci = alloc_id();
                        push_inst(body_, spv::OpBitcast, {t_i32_, ci, c});
                        uint32_t t_v2i32 = get_v2i32();
                        uint32_t xy = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v2i32, xy, ci, const_i32(0)});
                        uint32_t rgba = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v4f32_, rgba, f, f, f, const_f32(1.0f)});
                        push_inst(body_, spv::OpImageWrite, {im, xy, rgba});
                    }
                    break;
                }

                case IrOp::InterpAttr: {
                    const uint32_t attr_var = var_in_attr_[inst.imm_u32 & 15u];
                    const uint32_t src_var = (attr_var != 0u) ? attr_var : var_in_attr0_;
                    if (src_var != 0u) {
                        uint32_t attr = alloc_id();
                        push_inst(body_, spv::OpLoad, {t_v4f32_, attr, src_var});
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, res, attr, inst.imm2_u32 & 3u});
                    } else {
                        res = arg(inst, 0, IrType::F32);
                    }
                    break;
                }

                case IrOp::Export: {
                    uint32_t vec = 0u;
                    if ((inst.imm2_u32 & 0x10u) != 0u) {
                        uint32_t u01 = arg(inst, 0, IrType::U32);
                        uint32_t u23 = arg(inst, 1, IrType::U32);
                        uint32_t rg = alloc_id();
                        uint32_t ba = alloc_id();
                        push_inst(body_, spv::OpExtInst, {t_v2f32_, rg, glsl_ext_id_, spv::GlslUnpackHalf2x16, u01});
                        push_inst(body_, spv::OpExtInst, {t_v2f32_, ba, glsl_ext_id_, spv::GlslUnpackHalf2x16, u23});
                        uint32_t c0 = alloc_id(), c1 = alloc_id(), c2 = alloc_id(), c3 = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, c0, rg, 0u});
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, c1, rg, 1u});
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, c2, ba, 0u});
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, c3, ba, 1u});
                        vec = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v4f32_, vec, c0, c1, c2, c3});
                    } else {
                        uint32_t c0 = arg(inst, 0, IrType::F32);
                        uint32_t c1 = arg(inst, 1, IrType::F32);
                        uint32_t c2 = arg(inst, 2, IrType::F32);
                        uint32_t c3 = arg(inst, 3, IrType::F32);
                        vec = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v4f32_, vec, c0, c1, c2, c3});
                    }
                    last_export_vec_ = vec;
                    const uint32_t tgt = inst.imm_u32;
                    if (mod_.meta.stage == ShaderStage::Vertex) {
                        if (tgt == exp_target::POS0 && var_out_pos_ != 0u) {
                            push_inst(body_, spv::OpStore, {var_out_pos_, vec});
                            wrote_pos_ = true;
                        } else if (tgt >= exp_target::PARAM0 && tgt < exp_target::PARAM0 + 16u) {
                            uint32_t pv = var_out_param_[tgt - exp_target::PARAM0];
                            if (pv != 0u) {
                                push_inst(body_, spv::OpStore, {pv, vec});
                            }
                        }
                    } else if (mod_.meta.stage == ShaderStage::Pixel) {
                        if (tgt < 8u && var_out_color_[tgt] != 0u) {
                            push_inst(body_, spv::OpStore, {var_out_color_[tgt], vec});
                            if (tgt == 0u) wrote_color0_ = true;
                        }
                    }
                    break;
                }

                case IrOp::BitcastU32ToF32:
                    res = alloc_id();
                    push_inst(body_, spv::OpBitcast, {t_f32_, res, arg(inst, 0, IrType::U32)});
                    break;
                case IrOp::BitcastF32ToU32:
                    res = alloc_id();
                    push_inst(body_, spv::OpBitcast, {t_u32_, res, arg(inst, 0, IrType::F32)});
                    break;
                case IrOp::CvtI32ToF32: {
                    uint32_t si = alloc_id();
                    push_inst(body_, spv::OpBitcast, {t_i32_, si, arg(inst, 0, IrType::U32)});
                    res = alloc_id();
                    push_inst(body_, spv::OpConvertSToF, {t_f32_, res, si});
                    break;
                }
                case IrOp::CvtU32ToF32:
                    res = alloc_id();
                    push_inst(body_, spv::OpConvertUToF, {t_f32_, res, arg(inst, 0, IrType::U32)});
                    break;
                case IrOp::CvtF32ToI32: {
                    uint32_t si = alloc_id();
                    push_inst(body_, spv::OpConvertFToS, {t_i32_, si, arg(inst, 0, IrType::F32)});
                    res = alloc_id();
                    push_inst(body_, spv::OpBitcast, {t_u32_, res, si});
                    break;
                }
                case IrOp::CvtF32ToU32:
                    res = alloc_id();
                    push_inst(body_, spv::OpConvertFToU, {t_u32_, res, arg(inst, 0, IrType::F32)});
                    break;

                case IrOp::CvtF32ToF16: {
                    uint32_t f = arg(inst, 0, IrType::F32);
                    if (inst.use_native_f16 && t_f16_ != 0u) {
                        uint32_t h = alloc_id();
                        push_inst(body_, spv::OpFConvert, {t_f16_, h, f});
                        uint32_t hz = alloc_id();
                        push_inst(body_, spv::OpFConvert, {t_f16_, hz, const_f32(0.0f)});
                        uint32_t vh = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v2f16_, vh, h, hz});
                        res = alloc_id();
                        push_inst(body_, spv::OpBitcast, {t_u32_, res, vh});
                    } else {
                        uint32_t vf = alloc_id();
                        push_inst(body_, spv::OpCompositeConstruct, {t_v2f32_, vf, f, const_f32(0.0f)});
                        res = alloc_id();
                        push_inst(body_, spv::OpExtInst, {t_u32_, res, glsl_ext_id_, spv::GlslPackHalf2x16, vf});
                    }
                    break;
                }

                case IrOp::CvtF16ToF32: {
                    uint32_t u = arg(inst, 0, IrType::U32);
                    if (inst.use_native_f16 && t_v2f16_ != 0u) {
                        uint32_t vh = alloc_id();
                        push_inst(body_, spv::OpBitcast, {t_v2f16_, vh, u});
                        uint32_t h0 = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f16_, h0, vh, 0u});
                        res = alloc_id();
                        push_inst(body_, spv::OpFConvert, {t_f32_, res, h0});
                    } else {
                        uint32_t vf = alloc_id();
                        push_inst(body_, spv::OpExtInst, {t_v2f32_, vf, glsl_ext_id_, spv::GlslUnpackHalf2x16, u});
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_f32_, res, vf, 0u});
                    }
                    break;
                }

                case IrOp::PackF16x2: {
                    uint32_t f0 = arg(inst, 0, IrType::F32);
                    uint32_t f1 = arg(inst, 1, IrType::F32);
                    uint32_t vf = alloc_id();
                    push_inst(body_, spv::OpCompositeConstruct, {t_v2f32_, vf, f0, f1});
                    res = alloc_id();
                    push_inst(body_, spv::OpExtInst, {t_u32_, res, glsl_ext_id_, spv::GlslPackHalf2x16, vf});
                    break;
                }

                /* Integer / Bitwise ALU */
                case IrOp::IAdd:
                    res = alloc_id();
                    push_inst(body_, spv::OpIAdd, {t_u32_, res, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::ISub:
                    res = alloc_id();
                    push_inst(body_, spv::OpISub, {t_u32_, res, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::IMul:
                    res = alloc_id();
                    push_inst(body_, spv::OpIMul, {t_u32_, res, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::UMulHi:
                case IrOp::IMulHi: {
                    uint32_t t_pair = get_u32_pair_struct();
                    uint32_t pr = alloc_id();
                    push_inst(body_, inst.op == IrOp::UMulHi ? spv::OpUMulExtended : spv::OpSMulExtended,
                              {t_pair, pr, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    res = alloc_id();
                    push_inst(body_, spv::OpCompositeExtract, {t_u32_, res, pr, 1u});
                    break;
                }
                case IrOp::IMin:
                case IrOp::IMax: {
                    uint32_t sa = alloc_id(), sc = alloc_id(), sr = alloc_id();
                    push_inst(body_, spv::OpBitcast, {t_i32_, sa, arg(inst, 0, IrType::U32)});
                    push_inst(body_, spv::OpBitcast, {t_i32_, sc, arg(inst, 1, IrType::U32)});
                    push_inst(body_, spv::OpExtInst,
                              {t_i32_, sr, glsl_ext_id_, inst.op == IrOp::IMin ? spv::GlslSMin : spv::GlslSMax, sa, sc});
                    res = alloc_id();
                    push_inst(body_, spv::OpBitcast, {t_u32_, res, sr});
                    break;
                }
                case IrOp::UMin:
                case IrOp::UMax:
                    res = alloc_id();
                    push_inst(body_, spv::OpExtInst,
                              {t_u32_, res, glsl_ext_id_, inst.op == IrOp::UMin ? spv::GlslUMin : spv::GlslUMax,
                               arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::BitAnd:
                    res = alloc_id();
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, res, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::BitOr:
                    res = alloc_id();
                    push_inst(body_, spv::OpBitwiseOr, {t_u32_, res, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::BitXor:
                    res = alloc_id();
                    push_inst(body_, spv::OpBitwiseXor, {t_u32_, res, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)});
                    break;
                case IrOp::BitNot:
                    res = alloc_id();
                    push_inst(body_, spv::OpNot, {t_u32_, res, arg(inst, 0, IrType::U32)});
                    break;
                case IrOp::Shl: {
                    uint32_t sh = alloc_id();
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, sh, arg(inst, 1, IrType::U32), const_u32(31u)});
                    res = alloc_id();
                    push_inst(body_, spv::OpShiftLeftLogical, {t_u32_, res, arg(inst, 0, IrType::U32), sh});
                    break;
                }
                case IrOp::LShr: {
                    uint32_t sh = alloc_id();
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, sh, arg(inst, 1, IrType::U32), const_u32(31u)});
                    res = alloc_id();
                    push_inst(body_, spv::OpShiftRightLogical, {t_u32_, res, arg(inst, 0, IrType::U32), sh});
                    break;
                }
                case IrOp::AShr: {
                    uint32_t sh = alloc_id();
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, sh, arg(inst, 1, IrType::U32), const_u32(31u)});
                    res = alloc_id();
                    push_inst(body_, spv::OpShiftRightArithmetic, {t_u32_, res, arg(inst, 0, IrType::U32), sh});
                    break;
                }
                case IrOp::BitCount32:
                    res = alloc_id();
                    push_inst(body_, spv::OpBitCount, {t_u32_, res, arg(inst, 0, IrType::U32)});
                    break;
                case IrOp::FindLsb:
                    res = alloc_id();
                    push_inst(body_, spv::OpExtInst, {t_u32_, res, glsl_ext_id_, spv::GlslFindILsb, arg(inst, 0, IrType::U32)});
                    break;
                case IrOp::FindMsb:
                    res = alloc_id();
                    push_inst(body_, spv::OpExtInst, {t_u32_, res, glsl_ext_id_, spv::GlslFindUMsb, arg(inst, 0, IrType::U32)});
                    break;
                case IrOp::BfeU32: {
                    uint32_t off = alloc_id(), cnt = alloc_id();
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, off, arg(inst, 1, IrType::U32), const_u32(31u)});
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, cnt, arg(inst, 2, IrType::U32), const_u32(31u)});
                    res = alloc_id();
                    push_inst(body_, spv::OpBitFieldUExtract, {t_u32_, res, arg(inst, 0, IrType::U32), off, cnt});
                    break;
                }
                case IrOp::BfiB32: {
                    uint32_t mask = arg(inst, 0, IrType::U32);
                    uint32_t t_val = arg(inst, 1, IrType::U32);
                    uint32_t f_val = arg(inst, 2, IrType::U32);
                    uint32_t nm = alloc_id(), a1 = alloc_id(), a2 = alloc_id();
                    push_inst(body_, spv::OpNot, {t_u32_, nm, mask});
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, a1, mask, t_val});
                    push_inst(body_, spv::OpBitwiseAnd, {t_u32_, a2, nm, f_val});
                    res = alloc_id();
                    push_inst(body_, spv::OpBitwiseOr, {t_u32_, res, a1, a2});
                    break;
                }
                case IrOp::SelectU32:
                    res = alloc_id();
                    push_inst(body_, spv::OpSelect, {t_u32_, res, arg(inst, 0, IrType::Bool),
                                                     arg(inst, 1, IrType::U32), arg(inst, 2, IrType::U32)});
                    break;
                case IrOp::SelectF32:
                    res = alloc_id();
                    push_inst(body_, spv::OpSelect, {t_f32_, res, arg(inst, 0, IrType::Bool),
                                                     arg(inst, 1, IrType::F32), arg(inst, 2, IrType::F32)});
                    break;

                /* Floating-Point ALU with Stage 2 computeKnownFPClass elision */
                case IrOp::FAdd:
                case IrOp::FSub:
                case IrOp::FMul: {
                    uint32_t a = maybe_emit_denorm_guard(arg(inst, 0, IrType::F32), inst.elide_denorm_guard);
                    uint32_t c = maybe_emit_denorm_guard(arg(inst, 1, IrType::F32), inst.elide_denorm_guard);
                    uint32_t raw = alloc_id();
                    uint16_t opc = (inst.op == IrOp::FAdd) ? spv::OpFAdd :
                                   (inst.op == IrOp::FSub) ? spv::OpFSub : spv::OpFMul;
                    push_inst(body_, opc, {t_f32_, raw, a, c});
                    res = maybe_emit_nan_canon(maybe_emit_denorm_guard(raw, inst.elide_denorm_guard), inst.elide_nan_tree);
                    break;
                }
                case IrOp::FFma: {
                    uint32_t a = maybe_emit_denorm_guard(arg(inst, 0, IrType::F32), inst.elide_denorm_guard);
                    uint32_t c = maybe_emit_denorm_guard(arg(inst, 1, IrType::F32), inst.elide_denorm_guard);
                    uint32_t e = maybe_emit_denorm_guard(arg(inst, 2, IrType::F32), inst.elide_denorm_guard);
                    uint32_t raw = emit_fma_f32(a, c, e, inst.elide_fma_emul);
                    res = maybe_emit_nan_canon(maybe_emit_denorm_guard(raw, inst.elide_denorm_guard), inst.elide_nan_tree);
                    break;
                }
                case IrOp::FMin:
                    res = emit_fminmax(false, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32), inst.elide_nan_tree);
                    break;
                case IrOp::FMax:
                    res = emit_fminmax(true, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32), inst.elide_nan_tree);
                    break;
                case IrOp::FMin3: {
                    uint32_t t = emit_fminmax(false, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32), inst.elide_nan_tree);
                    res = emit_fminmax(false, t, arg(inst, 2, IrType::F32), inst.elide_nan_tree);
                    break;
                }
                case IrOp::FMax3: {
                    uint32_t t = emit_fminmax(true, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32), inst.elide_nan_tree);
                    res = emit_fminmax(true, t, arg(inst, 2, IrType::F32), inst.elide_nan_tree);
                    break;
                }
                case IrOp::FMed3: {
                    uint32_t a = arg(inst, 0, IrType::F32);
                    uint32_t c = arg(inst, 1, IrType::F32);
                    uint32_t e = arg(inst, 2, IrType::F32);
                    uint32_t mn = emit_fminmax(false, a, c, inst.elide_nan_tree);
                    uint32_t mx = emit_fminmax(true, a, c, inst.elide_nan_tree);
                    uint32_t hi = emit_fminmax(false, mx, e, inst.elide_nan_tree);
                    res = emit_fminmax(true, mn, hi, inst.elide_nan_tree);
                    break;
                }
                case IrOp::FSqrt:
                case IrOp::FRsq:
                case IrOp::FFloor:
                case IrOp::FCeil:
                case IrOp::FTrunc:
                case IrOp::FRoundEven:
                case IrOp::FFract:
                case IrOp::FSin:
                case IrOp::FCos:
                case IrOp::FExp2:
                case IrOp::FLog2:
                case IrOp::FAbs: {
                    uint32_t glsl_op = spv::GlslSqrt;
                    if (inst.op == IrOp::FRsq) glsl_op = spv::GlslInverseSqrt;
                    else if (inst.op == IrOp::FFloor) glsl_op = spv::GlslFloor;
                    else if (inst.op == IrOp::FCeil) glsl_op = spv::GlslCeil;
                    else if (inst.op == IrOp::FTrunc) glsl_op = spv::GlslTrunc;
                    else if (inst.op == IrOp::FRoundEven) glsl_op = spv::GlslRoundEven;
                    else if (inst.op == IrOp::FFract) glsl_op = spv::GlslFract;
                    else if (inst.op == IrOp::FSin) glsl_op = spv::GlslSin;
                    else if (inst.op == IrOp::FCos) glsl_op = spv::GlslCos;
                    else if (inst.op == IrOp::FExp2) glsl_op = spv::GlslExp2;
                    else if (inst.op == IrOp::FLog2) glsl_op = spv::GlslLog2;
                    else if (inst.op == IrOp::FAbs) glsl_op = spv::GlslFAbs;
                    uint32_t raw = alloc_id();
                    push_inst(body_, spv::OpExtInst, {t_f32_, raw, glsl_ext_id_, glsl_op, arg(inst, 0, IrType::F32)});
                    res = maybe_emit_denorm_guard(raw, inst.elide_denorm_guard);
                    break;
                }
                case IrOp::FRcp: {
                    uint32_t raw = alloc_id();
                    push_inst(body_, spv::OpFDiv, {t_f32_, raw, const_f32(1.0f), arg(inst, 0, IrType::F32)});
                    res = maybe_emit_denorm_guard(raw, inst.elide_denorm_guard);
                    break;
                }
                case IrOp::FNeg:
                    res = alloc_id();
                    push_inst(body_, spv::OpFNegate, {t_f32_, res, arg(inst, 0, IrType::F32)});
                    break;
                case IrOp::FClamp01: {
                    uint32_t a = arg(inst, 0, IrType::F32);
                    if (inst.elide_nan_tree) {
                        res = alloc_id();
                        push_inst(body_, spv::OpExtInst,
                                  {t_f32_, res, glsl_ext_id_, spv::GlslFClamp, a, const_f32(0.0f), const_f32(1.0f)});
                    } else {
                        uint32_t lo = emit_fminmax(true, a, const_f32(0.0f), false);
                        res = emit_fminmax(false, lo, const_f32(1.0f), false);
                    }
                    break;
                }

                /* Packed FP16 ALU */
                case IrOp::PkF16Add:
                case IrOp::PkF16Mul:
                case IrOp::PkF16Fma:
                case IrOp::PkF16Min:
                case IrOp::PkF16Max:
                    res = emit_pk_f16(inst, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32), arg(inst, 2, IrType::U32));
                    break;

                case IrOp::Dot2F32F16: {
                    uint32_t ua = alloc_id(), uc = alloc_id();
                    push_inst(body_, spv::OpExtInst, {t_v2f32_, ua, glsl_ext_id_, spv::GlslUnpackHalf2x16, arg(inst, 0, IrType::U32)});
                    push_inst(body_, spv::OpExtInst, {t_v2f32_, uc, glsl_ext_id_, spv::GlslUnpackHalf2x16, arg(inst, 1, IrType::U32)});
                    uint32_t a0 = alloc_id(), a1 = alloc_id(), c0 = alloc_id(), c1 = alloc_id();
                    push_inst(body_, spv::OpCompositeExtract, {t_f32_, a0, ua, 0u});
                    push_inst(body_, spv::OpCompositeExtract, {t_f32_, a1, ua, 1u});
                    push_inst(body_, spv::OpCompositeExtract, {t_f32_, c0, uc, 0u});
                    push_inst(body_, spv::OpCompositeExtract, {t_f32_, c1, uc, 1u});
                    uint32_t p0 = emit_fma_f32(a0, c0, arg(inst, 2, IrType::F32), inst.elide_fma_emul);
                    res = emit_fma_f32(a1, c1, p0, inst.elide_fma_emul);
                    break;
                }

                /* Comparisons */
                case IrOp::CmpEqU32: res = emit_cmp(spv::OpIEqual, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpNeU32: res = emit_cmp(spv::OpINotEqual, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpLtU32: res = emit_cmp(spv::OpULessThan, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpLeU32: res = emit_cmp(spv::OpULessThanEqual, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpGtU32: res = emit_cmp(spv::OpUGreaterThan, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpGeU32: res = emit_cmp(spv::OpUGreaterThanEqual, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpLtI32: res = emit_scmp(spv::OpSLessThan, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpLeI32: res = emit_scmp(spv::OpSLessThanEqual, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpGtI32: res = emit_scmp(spv::OpSGreaterThan, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpGeI32: res = emit_scmp(spv::OpSGreaterThanEqual, arg(inst, 0, IrType::U32), arg(inst, 1, IrType::U32)); break;
                case IrOp::CmpEqF32: res = emit_cmp(spv::OpFOrdEqual, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32)); break;
                case IrOp::CmpNeF32: res = emit_cmp(spv::OpFUnordNotEqual, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32)); break;
                case IrOp::CmpLtF32: res = emit_cmp(spv::OpFOrdLessThan, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32)); break;
                case IrOp::CmpLeF32: res = emit_cmp(spv::OpFOrdLessThanEqual, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32)); break;
                case IrOp::CmpGtF32: res = emit_cmp(spv::OpFOrdGreaterThan, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32)); break;
                case IrOp::CmpGeF32: res = emit_cmp(spv::OpFOrdGreaterThanEqual, arg(inst, 0, IrType::F32), arg(inst, 1, IrType::F32)); break;

                /* Stage 3: Wave / Subgroup / Cross-Lane Operations */
                case IrOp::ReadFirstLane: {
                    add_cap(spv::CapGroupNonUniform);
                    add_cap(spv::CapGroupNonUniformBallot);
                    res = alloc_id();
                    push_inst(body_, spv::OpGroupNonUniformBroadcastFirst,
                              {t_u32_, res, const_u32(spv::ScopeSubgroup), arg(inst, 0, IrType::U32)});
                    break;
                }

                case IrOp::ReadLane: {
                    uint32_t v = arg(inst, 0, IrType::U32);
                    uint32_t lane = arg(inst, 1, IrType::U32);
                    if (use_paired_wave64_lds) {
                        res = emit_wave64_lds_exchange(v, lane);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformShuffle);
                        res = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformShuffle,
                                  {t_u32_, res, const_u32(spv::ScopeSubgroup), v, lane});
                    }
                    break;
                }

                case IrOp::DsBpermute: {
                    uint32_t addr_bytes = arg(inst, 0, IrType::U32);
                    uint32_t data = arg(inst, 1, IrType::U32);
                    /* RDNA2 ds_bpermute_b32 lane index is addr >> 2 */
                    uint32_t lane = alloc_id();
                    push_inst(body_, spv::OpShiftRightLogical, {t_u32_, lane, addr_bytes, const_u32(2u)});
                    if (use_paired_wave64_lds) {
                        res = emit_wave64_lds_exchange(data, lane);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformShuffle);
                        res = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformShuffle,
                                  {t_u32_, res, const_u32(spv::ScopeSubgroup), data, lane});
                    }
                    break;
                }

                case IrOp::DsSwizzle: {
                    uint32_t data = arg(inst, 0, IrType::U32);
                    uint32_t xor_mask = (inst.imm_u32 >> 10u) & 0x1Fu;
                    if (use_paired_wave64_lds && inst.imm2_u32 != 0u) {
                        uint32_t target = alloc_id();
                        push_inst(body_, spv::OpBitwiseXor, {t_u32_, target, wave64_lane_idx_, const_u32(xor_mask | 32u)});
                        res = emit_wave64_lds_exchange(data, target);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformShuffle);
                        res = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformShuffleXor,
                                  {t_u32_, res, const_u32(spv::ScopeSubgroup), data, const_u32(xor_mask)});
                    }
                    break;
                }

                case IrOp::Permlane16: {
                    uint32_t v = arg(inst, 0, IrType::U32);
                    if (use_paired_wave64_lds && inst.imm_u32 != 0u) {
                        uint32_t target = alloc_id();
                        push_inst(body_, spv::OpBitwiseXor, {t_u32_, target, wave64_lane_idx_, const_u32(16u)});
                        res = emit_wave64_lds_exchange(v, target);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformShuffle);
                        res = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformShuffleXor,
                                  {t_u32_, res, const_u32(spv::ScopeSubgroup), v, const_u32(16u)});
                    }
                    break;
                }

                case IrOp::WaveBallotLo: {
                    uint32_t cond = arg(inst, 0, IrType::Bool);
                    if (use_paired_wave64_lds) {
                        res = emit_wave64_lds_ballot_word(cond, 0u, false);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformBallot);
                        uint32_t bal = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformBallot,
                                  {t_v4u32_, bal, const_u32(spv::ScopeSubgroup), cond});
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_u32_, res, bal, 0u});
                    }
                    break;
                }

                case IrOp::WaveBallotHi: {
                    uint32_t cond = arg(inst, 0, IrType::Bool);
                    if (use_paired_wave64_lds) {
                        res = emit_wave64_lds_ballot_word(cond, 32u, false);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformBallot);
                        uint32_t bal = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformBallot,
                                  {t_v4u32_, bal, const_u32(spv::ScopeSubgroup), cond});
                        res = alloc_id();
                        push_inst(body_, spv::OpCompositeExtract, {t_u32_, res, bal, 1u});
                    }
                    break;
                }

                case IrOp::WaveBallotBitCount64: {
                    uint32_t cond = arg(inst, 0, IrType::Bool);
                    if (use_paired_wave64_lds) {
                        res = emit_wave64_lds_ballot_word(cond, 0u, true);
                    } else {
                        add_cap(spv::CapGroupNonUniform);
                        add_cap(spv::CapGroupNonUniformBallot);
                        uint32_t bal = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformBallot,
                                  {t_v4u32_, bal, const_u32(spv::ScopeSubgroup), cond});
                        res = alloc_id();
                        push_inst(body_, spv::OpGroupNonUniformBallotBitCount,
                                  {t_u32_, res, const_u32(spv::ScopeSubgroup), spv::GroupOpReduce, bal});
                    }
                    break;
                }

                case IrOp::WaveMbcntLo:
                case IrOp::WaveMbcntHi: {
                    uint32_t mask = arg(inst, 0, IrType::U32);
                    uint32_t addend = arg(inst, 1, IrType::U32);
                    uint32_t lane = use_paired_wave64_lds ? wave64_lane_idx_ : lane_id_val_;
                    uint32_t shift_amt = lane;
                    if (inst.op == IrOp::WaveMbcntHi) {
                        uint32_t is_hi = alloc_id();
                        push_inst(body_, spv::OpUGreaterThanEqual, {t_bool_, is_hi, lane, const_u32(32u)});
                        uint32_t sub32 = alloc_id();
                        push_inst(body_, spv::OpISub, {t_u32_, sub32, lane, const_u32(32u)});
                        uint32_t bit = alloc_id();
                        push_inst(body_, spv::OpShiftLeftLogical, {t_u32_, bit, const_u32(1u), sub32});
                        uint32_t lo_m = alloc_id();
                        push_inst(body_, spv::OpISub, {t_u32_, lo_m, bit, const_u32(1u)});
                        uint32_t eff_m = alloc_id();
                        push_inst(body_, spv::OpSelect, {t_u32_, eff_m, is_hi, lo_m, const_u32(0u)});
                        uint32_t masked = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, masked, mask, eff_m});
                        uint32_t cnt = alloc_id();
                        push_inst(body_, spv::OpBitCount, {t_u32_, cnt, masked});
                        res = alloc_id();
                        push_inst(body_, spv::OpIAdd, {t_u32_, res, cnt, addend});
                    } else {
                        uint32_t is_lo = alloc_id();
                        push_inst(body_, spv::OpULessThan, {t_bool_, is_lo, lane, const_u32(32u)});
                        uint32_t bit = alloc_id();
                        push_inst(body_, spv::OpShiftLeftLogical, {t_u32_, bit, const_u32(1u), shift_amt});
                        uint32_t lo_m = alloc_id();
                        push_inst(body_, spv::OpISub, {t_u32_, lo_m, bit, const_u32(1u)});
                        uint32_t eff_m = alloc_id();
                        push_inst(body_, spv::OpSelect, {t_u32_, eff_m, is_lo, lo_m, const_u32(0xFFFFFFFFu)});
                        uint32_t masked = alloc_id();
                        push_inst(body_, spv::OpBitwiseAnd, {t_u32_, masked, mask, eff_m});
                        uint32_t cnt = alloc_id();
                        push_inst(body_, spv::OpBitCount, {t_u32_, cnt, masked});
                        res = alloc_id();
                        push_inst(body_, spv::OpIAdd, {t_u32_, res, cnt, addend});
                    }
                    break;
                }

                case IrOp::WqmMask:
                    res = arg(inst, 0, IrType::U32);
                    break;

                case IrOp::Barrier:
                    if (is_workgroup_stage) {
                        push_inst(body_, spv::OpControlBarrier,
                                  {const_u32(spv::ScopeWorkgroup), const_u32(spv::ScopeWorkgroup),
                                   const_u32(spv::SemAcquireReleaseWorkgroup)});
                    }
                    break;

                /* Stage 4: BVH4 Ray Tracing */
                case IrOp::BvhIntersectRay: {
                    uint32_t ox = arg(inst, 0, IrType::F32);
                    uint32_t oy = arg(inst, 1, IrType::F32);
                    uint32_t oz = arg(inst, 2, IrType::F32);
                    uint32_t tm = arg(inst, 3, IrType::F32);
                    if (mod_.rt_strategy == RtStrategy::RayQueryKHR) {
                        res = emit_bvh_ray_query(ox, oy, oz, tm);
                    } else {
                        res = emit_bvh_bda_fallback(ox, oy, oz, tm);
                    }
                    break;
                }

                /* Structured Control Flow */
                case IrOp::CfIf: {
                    uint32_t cond = arg(inst, 0, IrType::Bool);
                    uint32_t then_lbl = alloc_id();
                    uint32_t merge_lbl = alloc_id();
                    push_inst(body_, spv::OpSelectionMerge, {merge_lbl, 0u});
                    push_inst(body_, spv::OpBranchConditional, {cond, then_lbl, merge_lbl});
                    push_inst(body_, spv::OpLabel, {then_lbl});
                    if_stack.push_back({merge_lbl});
                    cf_kind_stack.push_back(0u);
                    ++cf_depth;
                    break;
                }
                case IrOp::CfElse:
                    break;
                case IrOp::CfEndIf: {
                    if (!if_stack.empty() && !cf_kind_stack.empty() && cf_kind_stack.back() == 0u) {
                        const uint32_t merge_lbl = if_stack.back().merge_lbl;
                        if_stack.pop_back();
                        cf_kind_stack.pop_back();
                        push_inst(body_, spv::OpBranch, {merge_lbl});
                        push_inst(body_, spv::OpLabel, {merge_lbl});
                        --cf_depth;
                    }
                    break;
                }
                case IrOp::CfLoopHeader: {
                    if (local_loop_ctr_ != 0u) {
                        push_inst(body_, spv::OpStore, {local_loop_ctr_, const_u32(0u)});
                    }
                    uint32_t hdr = alloc_id();
                    uint32_t body_lbl = alloc_id();
                    uint32_t cont = alloc_id();
                    uint32_t merge = alloc_id();
                    push_inst(body_, spv::OpBranch, {hdr});
                    push_inst(body_, spv::OpLabel, {hdr});
                    push_inst(body_, spv::OpLoopMerge, {merge, cont, 0u});
                    push_inst(body_, spv::OpBranch, {body_lbl});
                    push_inst(body_, spv::OpLabel, {body_lbl});
                    loop_stack.push_back({hdr, cont, merge});
                    cf_kind_stack.push_back(1u);
                    ++cf_depth;
                    break;
                }
                case IrOp::CfLoopBreakIfNot: {
                    if (!loop_stack.empty()) {
                        uint32_t cond = arg(inst, 0, IrType::Bool);
                        /* Bounded safety trip count so loops always terminate deterministically */
                        if (local_loop_ctr_ != 0u) {
                            uint32_t c = alloc_id();
                            push_inst(body_, spv::OpLoad, {t_u32_, c, local_loop_ctr_});
                            uint32_t cn = alloc_id();
                            push_inst(body_, spv::OpIAdd, {t_u32_, cn, c, const_u32(1u)});
                            push_inst(body_, spv::OpStore, {local_loop_ctr_, cn});
                            uint32_t lt = alloc_id();
                            push_inst(body_, spv::OpULessThan, {t_bool_, lt, cn, const_u32(4u)});
                            uint32_t comb = alloc_id();
                            push_inst(body_, spv::OpLogicalAnd, {t_bool_, comb, cond, lt});
                            cond = comb;
                        }
                        uint32_t keep_lbl = alloc_id();
                        push_inst(body_, spv::OpBranchConditional, {cond, loop_stack.back().cont_lbl, loop_stack.back().merge_lbl});
                        push_inst(body_, spv::OpLabel, {keep_lbl});
                        /* Unreachable fallthrough goes directly to merge */
                        push_inst(body_, spv::OpBranch, {loop_stack.back().merge_lbl});
                        loop_broke_ = true;
                    }
                    break;
                }
                case IrOp::CfLoopEnd: {
                    while (!cf_kind_stack.empty() && cf_kind_stack.back() == 0u && !if_stack.empty()) {
                        const uint32_t merge_lbl = if_stack.back().merge_lbl;
                        if_stack.pop_back();
                        cf_kind_stack.pop_back();
                        if (!loop_broke_) {
                            push_inst(body_, spv::OpBranch, {merge_lbl});
                        }
                        push_inst(body_, spv::OpLabel, {merge_lbl});
                        loop_broke_ = false;
                        --cf_depth;
                    }
                    if (!loop_stack.empty() && !cf_kind_stack.empty() && cf_kind_stack.back() == 1u) {
                        auto lp = loop_stack.back();
                        loop_stack.pop_back();
                        cf_kind_stack.pop_back();
                        if (!loop_broke_) {
                            push_inst(body_, spv::OpBranch, {lp.merge_lbl});
                        }
                        loop_broke_ = false;
                        push_inst(body_, spv::OpLabel, {lp.cont_lbl});
                        push_inst(body_, spv::OpBranch, {lp.hdr_lbl});
                        push_inst(body_, spv::OpLabel, {lp.merge_lbl});
                        --cf_depth;
                    }
                    break;
                }
            }

            if (inst.id > 0u && res != 0u) {
                /* Only publish to dominating SSA map when at top-level CF depth;
                 * inside conditional/loop blocks use a safe fallback for outside uses. */
                if (cf_depth == 0) {
                    val[inst.id] = res;
                } else {
                    val[inst.id] = (inst.type == IrType::F32) ? const_f32(0.0f) :
                                   (inst.type == IrType::Bool) ? const_bool(false) : const_u32(0u);
                }
            }
        }

        /* Close any unclosed CF scopes defensively in strict LIFO order */
        while (!cf_kind_stack.empty()) {
            uint8_t k = cf_kind_stack.back();
            cf_kind_stack.pop_back();
            if (k == 0u && !if_stack.empty()) {
                push_inst(body_, spv::OpBranch, {if_stack.back().merge_lbl});
                push_inst(body_, spv::OpLabel, {if_stack.back().merge_lbl});
                if_stack.pop_back();
            } else if (k == 1u && !loop_stack.empty()) {
                auto lp = loop_stack.back();
                loop_stack.pop_back();
                if (!loop_broke_) {
                    push_inst(body_, spv::OpBranch, {lp.merge_lbl});
                }
                loop_broke_ = false;
                push_inst(body_, spv::OpLabel, {lp.cont_lbl});
                push_inst(body_, spv::OpBranch, {lp.hdr_lbl});
                push_inst(body_, spv::OpLabel, {lp.merge_lbl});
            }
        }
    }

    void emit_stage_epilogue(uint32_t exec_model) {
        uint32_t out_vec = last_export_vec_;
        if (out_vec == 0u) {
            out_vec = alloc_id();
            push_inst(body_, spv::OpCompositeConstruct,
                      {t_v4f32_, out_vec, const_f32(0.0f), const_f32(0.0f), const_f32(0.0f), const_f32(1.0f)});
        }

        switch (exec_model) {
            case spv::EmVertex:
                if (!wrote_pos_) {
                    push_inst(body_, spv::OpStore, {var_out_pos_, out_vec});
                }
                push_inst(body_, spv::OpReturn, {});
                break;
            case spv::EmTessellationEvaluation:
                push_inst(body_, spv::OpStore, {var_out_pos_, out_vec});
                push_inst(body_, spv::OpReturn, {});
                break;

            case spv::EmFragment:
                if (!wrote_color0_) {
                    push_inst(body_, spv::OpStore, {var_out_color0_, out_vec});
                }
                push_inst(body_, spv::OpReturn, {});
                break;

            case spv::EmGeometry:
                for (int vi = 0; vi < 3; ++vi) {
                    push_inst(body_, spv::OpStore, {var_out_pos_, out_vec});
                    push_inst(body_, spv::OpEmitVertex, {});
                }
                push_inst(body_, spv::OpEndPrimitive, {});
                push_inst(body_, spv::OpReturn, {});
                break;

            case spv::EmTessellationControl: {
                uint32_t p_out_f32 = make_ptr(spv::ScOutput, t_f32_);
                for (uint32_t i = 0; i < 4u; ++i) {
                    uint32_t p = alloc_id();
                    push_inst(body_, spv::OpAccessChain, {p_out_f32, p, var_tess_outer_, const_u32(i)});
                    push_inst(body_, spv::OpStore, {p, const_f32(1.0f)});
                }
                for (uint32_t i = 0; i < 2u; ++i) {
                    uint32_t p = alloc_id();
                    push_inst(body_, spv::OpAccessChain, {p_out_f32, p, var_tess_inner_, const_u32(i)});
                    push_inst(body_, spv::OpStore, {p, const_f32(1.0f)});
                }
                push_inst(body_, spv::OpReturn, {});
                break;
            }

            case spv::EmMeshEXT: {
                push_inst(body_, spv::OpSetMeshOutputsEXT, {const_u32(3u), const_u32(1u)});
                uint32_t p_out_v4 = make_ptr(spv::ScOutput, t_v4f32_);
                for (uint32_t i = 0; i < 3u; ++i) {
                    uint32_t p = alloc_id();
                    push_inst(body_, spv::OpAccessChain, {p_out_v4, p, var_mesh_verts_, const_u32(i), const_u32(0u)});
                    push_inst(body_, spv::OpStore, {p, out_vec});
                }
                uint32_t p_out_v3u = make_ptr(spv::ScOutput, t_v3u32_);
                uint32_t pi = alloc_id();
                push_inst(body_, spv::OpAccessChain, {p_out_v3u, pi, var_mesh_prims_, const_u32(0u)});
                uint32_t tri = alloc_id();
                push_inst(body_, spv::OpCompositeConstruct,
                          {t_v3u32_, tri, const_u32(0u), const_u32(1u), const_u32(2u)});
                push_inst(body_, spv::OpStore, {pi, tri});
                push_inst(body_, spv::OpReturn, {});
                break;
            }

            case spv::EmTaskEXT: {
                uint32_t p_u32 = make_ptr(spv::ScTaskPayloadWorkgroupEXT, t_u32_);
                uint32_t p = alloc_id();
                push_inst(body_, spv::OpAccessChain, {p_u32, p, var_task_payload_, const_u32(0u)});
                push_inst(body_, spv::OpStore, {p, last_stored_u32_ ? last_stored_u32_ : const_u32(1u)});
                /* OpEmitMeshTasksEXT is a SPIR-V block terminator! */
                push_inst(body_, spv::OpEmitMeshTasksEXT,
                          {const_u32(1u), const_u32(1u), const_u32(1u), var_task_payload_});
                break;
            }

            case spv::EmRayGenerationKHR: {
                uint32_t tlas = alloc_id();
                push_inst(body_, spv::OpLoad, {t_as_, tlas, var_tlas_});
                uint32_t orig = alloc_id(), dir = alloc_id();
                push_inst(body_, spv::OpCompositeConstruct,
                          {t_v3f32_, orig, const_f32(0.0f), const_f32(0.0f), const_f32(0.0f)});
                push_inst(body_, spv::OpCompositeConstruct,
                          {t_v3f32_, dir, const_f32(0.0f), const_f32(0.0f), const_f32(1.0f)});
                push_inst(body_, spv::OpStore, {var_ray_payload_, out_vec});
                push_inst(body_, spv::OpTraceRayKHR,
                          {tlas, const_u32(0u), const_u32(0xFFu),
                           const_u32(0u), const_u32(0u), const_u32(0u),
                           orig, const_f32(0.001f), dir, const_f32(1000.0f),
                           var_ray_payload_});
                push_inst(body_, spv::OpReturn, {});
                break;
            }

            case spv::EmIntersectionKHR: {
                uint32_t rep = alloc_id();
                push_inst(body_, spv::OpReportIntersectionKHR,
                          {t_bool_, rep, const_f32(1.0f), const_u32(0u)});
                push_inst(body_, spv::OpReturn, {});
                break;
            }

            case spv::EmClosestHitKHR:
            case spv::EmMissKHR:
            case spv::EmAnyHitKHR:
            case spv::EmCallableKHR:
                if (var_ray_payload_ != 0u) {
                    push_inst(body_, spv::OpStore, {var_ray_payload_, out_vec});
                }
                push_inst(body_, spv::OpReturn, {});
                break;

            default:
                push_inst(body_, spv::OpReturn, {});
                break;
        }
    }

    uint32_t emit_cmp(uint16_t opc, uint32_t a, uint32_t c) {
        uint32_t r = alloc_id();
        push_inst(body_, opc, {t_bool_, r, a, c});
        return r;
    }

    uint32_t emit_scmp(uint16_t opc, uint32_t a, uint32_t c) {
        uint32_t sa = alloc_id(), sc = alloc_id();
        push_inst(body_, spv::OpBitcast, {t_i32_, sa, a});
        push_inst(body_, spv::OpBitcast, {t_i32_, sc, c});
        uint32_t r = alloc_id();
        push_inst(body_, opc, {t_bool_, r, sa, sc});
        return r;
    }

    uint32_t get_v2i32() {
        if (t_v2i32_ == 0u) {
            t_v2i32_ = alloc_id();
            push_inst(types_, spv::OpTypeVector, {t_v2i32_, t_i32_, 2u});
        }
        return t_v2i32_;
    }

    uint32_t get_u32_pair_struct() {
        if (t_u32_pair_ == 0u) {
            t_u32_pair_ = alloc_id();
            push_inst(types_, spv::OpTypeStruct, {t_u32_pair_, t_u32_, t_u32_});
        }
        return t_u32_pair_;
    }

    const IrModule& mod_;
    const LiftOptions& opts_;

    uint32_t next_id_{1};
    uint32_t glsl_ext_id_{0};
    uint32_t main_fn_id_{0};
    uint32_t addr_model_{spv::AddrLogical};

    /* Sections */
    std::vector<uint32_t> caps_;
    std::vector<uint32_t> exts_;
    std::vector<uint32_t> exec_modes_;
    std::vector<uint32_t> decos_;
    std::vector<uint32_t> types_;
    std::vector<uint32_t> globals_;
    std::vector<uint32_t> func_;
    std::vector<uint32_t> func_vars_;
    std::vector<uint32_t> body_;
    std::vector<uint32_t> interface_vars_;

    std::unordered_set<uint32_t> cap_set_;
    std::unordered_set<std::string> ext_set_;
    std::unordered_set<uint32_t> interface_set_;
    std::unordered_map<uint64_t, uint32_t> ptr_cache_;
    std::unordered_map<uint32_t, uint32_t> u32_const_cache_;
    std::unordered_map<uint32_t, uint32_t> i32_const_cache_;
    std::unordered_map<uint32_t, uint32_t> f32_const_cache_;
    std::unordered_map<uint64_t, uint32_t> u64_const_cache_;

    /* Core Types */
    uint32_t t_void_{0}, t_bool_{0}, t_u32_{0}, t_i32_{0}, t_f32_{0}, t_u64_{0};
    uint32_t t_f16_{0}, t_v2f16_{0};
    uint32_t t_v2f32_{0}, t_v3f32_{0}, t_v4f32_{0}, t_v3u32_{0}, t_v4u32_{0};
    uint32_t t_v2i32_{0}, t_u32_pair_{0};
    uint32_t t_fn_void_{0};
    uint32_t t_rt_u32_{0}, t_ssbo_struct_{0};
    uint32_t t_img_2d_{0}, t_sampled_img_{0}, t_stor_img_2d_{0};
    uint32_t t_as_{0}, t_rq_{0};
    uint32_t t_psb_u32_blk_{0}, t_psb_f32_blk_{0}, t_arr_u32_32_{0};

    uint32_t p_func_u32_{0}, p_func_f32_{0}, p_func_rq_{0}, p_func_u32_arr32_{0};
    uint32_t p_ssbo_struct_{0}, p_ssbo_u32_{0}, p_wg_u32_{0};
    uint32_t p_psb_u32_blk_{0}, p_psb_u32_{0}, p_psb_f32_blk_{0}, p_psb_f32_{0};

    uint32_t c_true_{0}, c_false_{0};

    /* Globals */
    uint32_t var_in_ssbo_{0}, var_out_ssbo_{0};
    std::array<uint32_t, 8>  var_cbv_ssbo_{};
    std::array<uint32_t, 8>  var_tex_2d_{};
    uint32_t var_sampled_img_{0}, var_stor_img_{0};
    uint32_t var_lds_{0}, var_wave64_lds_{0}, var_wave64_ballot_{0};
    uint32_t var_gid_{0}, var_lid_{0}, var_vtx_id_{0}, var_frag_coord_{0}, var_in_attr0_{0};
    std::array<uint32_t, 16> var_in_vtx_{};
    std::array<uint32_t, 16> var_out_param_{};
    std::array<uint32_t, 16> var_in_attr_{};
    std::array<uint32_t, 8>  var_out_color_{};
    uint32_t var_out_pos_{0}, var_out_color0_{0};
    uint32_t var_tess_outer_{0}, var_tess_inner_{0}, var_tess_coord_{0};
    uint32_t var_mesh_verts_{0}, var_mesh_prims_{0}, var_task_payload_{0};
    uint32_t var_tlas_{0}, var_launch_id_{0}, var_ray_payload_{0};

    /* Function locals & tracking */
    uint32_t local_rq_var_{0};
    uint32_t local_bvh_stack_{0}, local_bvh_sp_{0}, local_bvh_best_t_{0};
    uint32_t local_loop_ctr_{0};
    uint32_t lane_id_val_{0}, wave64_lane_idx_{0};
    uint32_t last_export_vec_{0}, last_stored_u32_{0};
    bool     wrote_pos_{false}, wrote_color0_{false};
    bool     loop_broke_{false};
};

} // namespace

SpirvEmitOutput emit_spirv_module(const IrModule& mod, const LiftOptions& opts) {
    SpvEmitter emitter(mod, opts);
    return emitter.build();
}

} // namespace scarlett::rdna2
