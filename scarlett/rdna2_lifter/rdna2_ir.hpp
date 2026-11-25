/*
 * Proton-Scarlett — RDNA2 SSA IR, CFG Structurizer, computeKnownFPClass & Wave64 Analyzer
 */

#ifndef SCARLETT_RDNA2_IR_HPP
#define SCARLETT_RDNA2_IR_HPP

#include <cstdint>
#include <vector>

#include "rdna2_lifter.hpp"

namespace scarlett::rdna2 {

enum class IrType : uint8_t {
    Void = 0,
    Bool,
    U32,
    I32,
    F32,
    F16,
    V2F16,
    U64
};

enum class ValueUniformity : uint8_t {
    Uniform = 0,   /* SGPR / SMEM / SOP* / workgroup-uniform constant */
    Divergent = 1  /* VGPR / VOP* / lane-varying */
};

/* Stage 2 KnownFPClass bit flags (forward + backward dataflow) */
namespace fp_class {
constexpr uint16_t NONE            = 0u;
constexpr uint16_t NO_NAN          = (1u << 0);
constexpr uint16_t NO_INF          = (1u << 1);
constexpr uint16_t NO_SUBNORMAL    = (1u << 2);
constexpr uint16_t NON_NEGATIVE    = (1u << 3);
constexpr uint16_t UNIT_RANGE      = (1u << 4); /* [0.0, 1.0] */
constexpr uint16_t EXACT_F16_RANGE = (1u << 5); /* Fits f16 normal range without overflow/subnormal */
constexpr uint16_t DEMAND_NO_NAN   = (1u << 8); /* Backward demand: consumer tolerates/masks NaN */
constexpr uint16_t DEMAND_SATURATE = (1u << 9); /* Backward demand: consumer clamps to [0,1] */
} // namespace fp_class

enum class IrOp : uint16_t {
    Nop = 0,
    ConstU32,
    ConstF32,
    ConstBool,
    LoadInputU32,       /* Read from SSBO input buffer[idx] (lane or uniform) */
    LoadInputF32,
    LoadVertexAttr,     /* VS vertex input attribute (imm=location, imm2=component 0..3) */
    LoadVertexId,       /* VS gl_VertexIndex as U32 */
    LoadCbvU32,         /* Graphics CBV/SSBO load (imm=dword_offset, imm2=slot 0..7) */
    LoadRootSrvU32,     /* Graphics Root SRV SSBO load (imm=offset, imm2=root_idx/comp) */
    StoreOutputU32,     /* Write to SSBO output buffer[idx] */
    StoreOutputF32,
    LoadLdsU32,
    StoreLdsU32,
    AtomicLdsAdd,
    AtomicBufAdd,
    ImageSample,
    ImageLoad,
    ImageStore,
    InterpAttr,
    Export,             /* Stage output export (MRT/POS/PARAM/PRIM) */
    BitcastU32ToF32,
    BitcastF32ToU32,
    CvtI32ToF32,
    CvtU32ToF32,
    CvtF32ToI32,
    CvtF32ToU32,
    CvtF32ToF16,        /* f32 -> u32 (low 16 bits half) */
    CvtF16ToF32,        /* u32 (low 16 bits half) -> f32 */
    PackF16x2,          /* (f32, f32) -> packed u32 f16x2 */
    /* Integer / Bitwise ALU */
    IAdd,
    ISub,
    IMul,
    UMulHi,
    IMulHi,
    IMin,
    IMax,
    UMin,
    UMax,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    Shl,
    LShr,
    AShr,
    BitCount32,
    FindLsb,
    FindMsb,
    BfeU32,
    BfiB32,
    SelectU32,
    SelectF32,
    LogicalNot,
    Phi,                /* Structured CfEndIf merge (args={pre_val, then_val}) */
    /* Floating-Point ALU (FP32) */
    FAdd,
    FSub,
    FMul,
    FFma,
    FMin,
    FMax,
    FMin3,
    FMax3,
    FMed3,
    FSqrt,
    FRcp,
    FRsq,
    FFloor,
    FCeil,
    FTrunc,
    FRoundEven,
    FFract,
    FSin,
    FCos,
    FExp2,
    FLog2,
    FAbs,
    FNeg,
    FClamp01,
    /* Packed FP16 ALU (VOP3P / VOP2 f16) */
    PkF16Add,
    PkF16Mul,
    PkF16Fma,
    PkF16Min,
    PkF16Max,
    Dot2F32F16,
    /* Comparisons -> Bool */
    CmpEqU32,
    CmpNeU32,
    CmpLtU32,
    CmpLeU32,
    CmpGtU32,
    CmpGeU32,
    CmpLtI32,
    CmpLeI32,
    CmpGtI32,
    CmpGeI32,
    CmpEqF32,
    CmpNeF32,
    CmpLtF32,
    CmpLeF32,
    CmpGtF32,
    CmpGeF32,
    /* Wave / Subgroup / Cross-lane Operations */
    ReadFirstLane,      /* v_readfirstlane_b32 (safe in Wave32 & Wave64) */
    ReadLane,           /* v_readlane_b32 (cross-half-wave if lane >= 32 or dynamic) */
    WaveBallotLo,       /* low 32 bits of 64-bit ballot */
    WaveBallotHi,       /* high 32 bits of 64-bit ballot (cross-half-wave!) */
    WaveBallotBitCount64,/* s_bcnt1_i32_b64 over 64-bit ballot (cross-half-wave!) */
    WaveMbcntLo,        /* v_mbcnt_lo_u32_b32 */
    WaveMbcntHi,        /* v_mbcnt_hi_u32_b32 (cross-half-wave!) */
    DsBpermute,         /* ds_bpermute_b32 (arbitrary 64-lane permute!) */
    DsSwizzle,          /* ds_swizzle_b32 */
    Permlane16,         /* v_permlane16_b32 / v_permlanex16_b32 */
    Barrier,            /* s_barrier */
    WqmMask,            /* s_wqm_b32 / s_wqm_b64 */
    /* Ray Tracing BVH4 (image_bvh_intersect_ray / image_bvh64_intersect_ray) */
    BvhIntersectRay,    /* returns hit.t (also populates hit u,v,prim registers) */
    /* Structured Control Flow Markers (produced by CFG structurizer) */
    CfIf,               /* arg[0] = Bool condition */
    CfElse,
    CfEndIf,
    CfLoopHeader,       /* arg[0] = max trip hint */
    CfLoopBreakIfNot,   /* arg[0] = Bool continue cond */
    CfLoopEnd
};

struct IrInst {
    uint32_t id{0};          /* 1-based SSA value ID (0 if void) */
    IrOp     op{IrOp::Nop};
    IrType   type{IrType::Void};
    ValueUniformity uniformity{ValueUniformity::Divergent};

    uint32_t args[4]{0, 0, 0, 0};
    uint8_t  arg_count{0};

    uint32_t imm_u32{0};
    uint32_t imm2_u32{0};
    float    imm_f32{0.0f};
    bool     clamp_result{false};
    bool     is_bvh64{false};

    /* Populated by Stage 2 computeKnownFPClass */
    uint16_t fp_flags{fp_class::NONE};
    bool     elide_nan_tree{false};
    bool     elide_denorm_guard{false};
    bool     elide_fma_emul{false};
    bool     use_native_f16{false};
};

struct CfgBlock {
    uint32_t block_id{0};
    uint32_t start_inst_idx{0};
    uint32_t end_inst_idx{0}; /* exclusive in decoded stream */
    std::vector<uint32_t> succs;
    bool is_loop_header{false};
    bool has_back_edge{false};
};

struct IrModule {
    Rdna2ShaderMetadata meta{};
    std::vector<CfgBlock> cfg_blocks;
    std::vector<IrInst> insts;
    uint32_t next_ssa_id{1};

    /* Summary flags detected during lifting */
    bool uses_lds{false};
    bool uses_storage_buffer{false};
    bool uses_image_sample{false};
    bool uses_image_storage{false};
    bool uses_bvh_rt{false};
    bool uses_bvh64_rt{false};
    bool uses_f16{false};
    bool uses_wqm{false};
    bool has_divergent_cf{false};
    bool has_subgroup_ops{false};
    uint32_t loop_count{0};
    uint32_t branch_count{0};
    uint32_t max_lds_words{64};
    uint32_t cbv_slot_mask{0};
    uint32_t root_srv_mask{0};
    uint32_t tex_slot_mask{0};
    uint32_t vtx_attr_mask{0};
    uint32_t out_param_mask{0};
    uint32_t in_attr_mask{0};
    uint32_t mrt_mask{0};

    /* Stage 2 & Stage 3 analysis outputs */
    FpClassAnalysisStats fp_stats{};
    Wave64AnalysisStats wave_stats{};
    RtStrategy rt_strategy{RtStrategy::None};
};

/* Build SSA IR & structurized CFG from decoded GFX10.3 instructions */
IrModule build_ssa_ir_and_structurize(const Rdna2ShaderMetadata& meta,
                                      const std::vector<DecodedInst>& decoded) noexcept;

/* Stage 2: Run forward + backward computeKnownFPClass dataflow & mark safe elisions */
void run_compute_known_fp_class(IrModule& mod, const LiftOptions& opts) noexcept;

/* Stage 3: Analyze Wave64 cross-half-wave dependencies & select WaveStrategy */
void run_wave64_dependency_analysis(IrModule& mod, const LiftOptions& opts) noexcept;

/* Stage 4: Choose BVH4 RT strategy (RayQueryKHR vs PhysicalStorageBuffer64 BDA fallback) */
void run_rt_bvh4_strategy_selection(IrModule& mod, const LiftOptions& opts) noexcept;

} // namespace scarlett::rdna2

#endif // SCARLETT_RDNA2_IR_HPP
