/*
 * Proton-Scarlett — SSA IR Builder, CFG Structurizer,
 * Stage 2 computeKnownFPClass (Forward + Backward Dataflow),
 * Stage 3 Adaptive Wave64 Cross-Half-Wave Analyzer,
 * Stage 4 BVH4 RT Strategy Selector.
 */

#include "rdna2_ir.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace scarlett::rdna2 {

namespace {

uint32_t float_to_bits(float f) noexcept {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

float bits_to_float(uint32_t u) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

bool is_fp_opcode(IrOp op) noexcept {
    switch (op) {
        case IrOp::FAdd:
        case IrOp::FSub:
        case IrOp::FMul:
        case IrOp::FFma:
        case IrOp::FMin:
        case IrOp::FMax:
        case IrOp::FMin3:
        case IrOp::FMax3:
        case IrOp::FMed3:
        case IrOp::FSqrt:
        case IrOp::FRcp:
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
        case IrOp::FAbs:
        case IrOp::FNeg:
        case IrOp::FClamp01:
        case IrOp::PkF16Add:
        case IrOp::PkF16Mul:
        case IrOp::PkF16Fma:
        case IrOp::PkF16Min:
        case IrOp::PkF16Max:
        case IrOp::Dot2F32F16:
        case IrOp::CvtF32ToF16:
        case IrOp::CvtF16ToF32:
            return true;
        default:
            return false;
    }
}

uint16_t classify_f32_constant(float f) noexcept {
    uint16_t flags = fp_class::NONE;
    if (!std::isnan(f)) {
        flags |= fp_class::NO_NAN;
    }
    if (!std::isinf(f)) {
        flags |= fp_class::NO_INF;
    }
    if (std::fpclassify(f) != FP_SUBNORMAL) {
        flags |= fp_class::NO_SUBNORMAL;
    }
    if (!std::isnan(f) && f >= 0.0f) {
        flags |= fp_class::NON_NEGATIVE;
        if (f <= 1.0f) {
            flags |= fp_class::UNIT_RANGE;
        }
    }
    if ((flags & (fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL)) ==
        (fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL)) {
        const float af = std::fabs(f);
        if (af == 0.0f || (af >= 6.103515625e-5f && af <= 65504.0f)) {
            flags |= fp_class::EXACT_F16_RANGE;
        }
    }
    return flags;
}

class IrBuilder {
public:
    explicit IrBuilder(IrModule& mod) : mod_(mod) {
        sgpr_.fill(0u);
        vgpr_.fill(0u);
        vgpr_type_.fill(IrType::U32);
    }

    uint32_t emit(IrOp op, IrType type, ValueUniformity u,
                  std::initializer_list<uint32_t> args = {},
                  uint32_t imm = 0u, float imm_f = 0.0f,
                  bool clamp = false, uint32_t imm2 = 0u) {
        IrInst inst{};
        inst.op = op;
        inst.type = type;
        inst.uniformity = u;
        inst.imm_u32 = imm;
        inst.imm2_u32 = imm2;
        inst.imm_f32 = imm_f;
        inst.clamp_result = clamp;
        uint8_t idx = 0;
        for (uint32_t a : args) {
            if (idx < 4u) {
                inst.args[idx++] = a;
            }
        }
        inst.arg_count = idx;
        if (type != IrType::Void) {
            inst.id = mod_.next_ssa_id++;
        }
        mod_.insts.push_back(inst);
        return inst.id;
    }

    uint32_t const_u32(uint32_t v) {
        return emit(IrOp::ConstU32, IrType::U32, ValueUniformity::Uniform, {}, v);
    }

    uint32_t const_f32(float f) {
        return emit(IrOp::ConstF32, IrType::F32, ValueUniformity::Uniform, {}, float_to_bits(f), f);
    }

    uint32_t const_bool(bool b) {
        return emit(IrOp::ConstBool, IrType::Bool, ValueUniformity::Uniform, {}, b ? 1u : 0u);
    }

    /* Resolve a unified 9-bit GFX10.3 source operand as U32 */
    uint32_t read_src_u32(uint16_t code, bool has_lit, uint32_t lit) {
        if (code < 106u) {
            if (sgpr_[code] == 0u) {
                mod_.uses_storage_buffer = true;
                sgpr_[code] = emit(IrOp::LoadInputU32, IrType::U32, ValueUniformity::Uniform,
                                   {}, code, 0.0f, false, 1u);
            }
            return sgpr_[code];
        }
        if (code == reg_code::VCC_LO) {
            if (vcc_lo_ == 0u) vcc_lo_ = const_u32(0u);
            return vcc_lo_;
        }
        if (code == reg_code::VCC_HI) {
            mod_.wave_stats.uses_ballot_hi32 = true;
            if (vcc_hi_ == 0u) {
                mod_.has_subgroup_ops = true;
                vcc_hi_ = emit(IrOp::WaveBallotHi, IrType::U32, ValueUniformity::Uniform,
                               {last_vcc_bool_ ? last_vcc_bool_ : const_bool(true)});
            }
            return vcc_hi_;
        }
        if (code == reg_code::M0) {
            return m0_ ? m0_ : const_u32(0u);
        }
        if (code == reg_code::EXEC_LO) {
            return exec_lo_ ? exec_lo_ : const_u32(0xFFFFFFFFu);
        }
        if (code == reg_code::EXEC_HI) {
            return exec_hi_ ? exec_hi_ : const_u32(0xFFFFFFFFu);
        }
        if (code >= reg_code::INLINE_INT_0 && code <= 192u) {
            return const_u32(static_cast<uint32_t>(code - reg_code::INLINE_INT_0));
        }
        if (code >= reg_code::INLINE_NEG_1 && code <= 208u) {
            int32_t neg = -static_cast<int32_t>(code - reg_code::INLINE_NEG_1 + 1u);
            return const_u32(static_cast<uint32_t>(neg));
        }
        if (code >= reg_code::INLINE_F_0_5 && code <= reg_code::INLINE_INV2PI) {
            return const_u32(float_to_bits(inline_float_val(code)));
        }
        if (code == reg_code::SCC) {
            if (scc_ == 0u) return const_u32(0u);
            return emit(IrOp::SelectU32, IrType::U32, ValueUniformity::Uniform,
                        {scc_, const_u32(1u), const_u32(0u)});
        }
        if (code == reg_code::LITERAL_32) {
            return const_u32(has_lit ? lit : 0u);
        }
        if (code >= reg_code::VGPR_BASE && code < 512u) {
            const uint16_t v = code - reg_code::VGPR_BASE;
            return read_vgpr_u32(v);
        }
        return const_u32(0u);
    }

    /* Resolve a unified 9-bit GFX10.3 source operand as F32 (applying optional abs/neg) */
    uint32_t read_src_f32(uint16_t code, bool has_lit, uint32_t lit,
                          bool apply_abs = false, bool apply_neg = false) {
        uint32_t id = 0u;
        ValueUniformity u = ValueUniformity::Uniform;
        if (code >= reg_code::INLINE_F_0_5 && code <= reg_code::INLINE_INV2PI) {
            id = const_f32(inline_float_val(code));
        } else if (code == reg_code::LITERAL_32) {
            id = const_f32(bits_to_float(has_lit ? lit : 0u));
        } else if (code >= reg_code::INLINE_INT_0 && code <= 192u) {
            id = const_f32(bits_to_float(static_cast<uint32_t>(code - reg_code::INLINE_INT_0)));
        } else if (code >= reg_code::VGPR_BASE && code < 512u) {
            const uint16_t v = code - reg_code::VGPR_BASE;
            id = read_vgpr_f32(v);
            u = ValueUniformity::Divergent;
        } else {
            uint32_t raw = read_src_u32(code, has_lit, lit);
            id = emit(IrOp::BitcastU32ToF32, IrType::F32, ValueUniformity::Uniform, {raw});
        }
        if (apply_abs) {
            id = emit(IrOp::FAbs, IrType::F32, u, {id});
        }
        if (apply_neg) {
            id = emit(IrOp::FNeg, IrType::F32, u, {id});
        }
        return id;
    }

    uint32_t read_vgpr_u32(uint16_t v) {
        v &= 0xFFu;
        if (vgpr_[v] == 0u) {
            mod_.uses_storage_buffer = true;
            vgpr_[v] = emit(IrOp::LoadInputU32, IrType::U32, ValueUniformity::Divergent,
                            {}, v, 0.0f, false, 0u);
            vgpr_type_[v] = IrType::U32;
            return vgpr_[v];
        }
        if (vgpr_type_[v] == IrType::F32) {
            return emit(IrOp::BitcastF32ToU32, IrType::U32, ValueUniformity::Divergent, {vgpr_[v]});
        }
        return vgpr_[v];
    }

    uint32_t read_vgpr_f32(uint16_t v) {
        v &= 0xFFu;
        if (vgpr_[v] == 0u) {
            mod_.uses_storage_buffer = true;
            vgpr_[v] = emit(IrOp::LoadInputF32, IrType::F32, ValueUniformity::Divergent,
                            {}, v, 0.0f, false, 0u);
            vgpr_type_[v] = IrType::F32;
            return vgpr_[v];
        }
        if (vgpr_type_[v] != IrType::F32) {
            return emit(IrOp::BitcastU32ToF32, IrType::F32, ValueUniformity::Divergent, {vgpr_[v]});
        }
        return vgpr_[v];
    }

    void write_vgpr(uint16_t v, uint32_t ssa_id, IrType type) {
        v &= 0xFFu;
        vgpr_[v] = ssa_id;
        vgpr_type_[v] = type;
        last_written_vgpr_ = v;
        wrote_any_output_ |= false;
    }

    void write_sgpr(uint16_t s, uint32_t ssa_id) {
        if (s < 106u) {
            sgpr_[s] = ssa_id;
        } else if (s == reg_code::VCC_LO) {
            vcc_lo_ = ssa_id;
        } else if (s == reg_code::VCC_HI) {
            vcc_hi_ = ssa_id;
        } else if (s == reg_code::M0) {
            m0_ = ssa_id;
        } else if (s == reg_code::EXEC_LO) {
            exec_lo_ = ssa_id;
        } else if (s == reg_code::EXEC_HI) {
            exec_hi_ = ssa_id;
        }
    }

    void set_scc(uint32_t bool_id) { scc_ = bool_id; }
    uint32_t get_scc() { return scc_ ? scc_ : const_bool(true); }

    void set_vcc_bool(uint32_t bool_id, bool writes_exec) {
        last_vcc_bool_ = bool_id;
        if (writes_exec) {
            mod_.has_divergent_cf = true;
        }
    }
    uint32_t get_vcc_bool() { return last_vcc_bool_ ? last_vcc_bool_ : const_bool(true); }

    void mark_output_written() { wrote_any_output_ = true; }
    bool has_written_output() const noexcept { return wrote_any_output_; }
    uint16_t last_written_vgpr() const noexcept { return last_written_vgpr_; }

    /* Track which SGPRs hold 64-bit ballot results from VOPC / EXEC mask ops */
    void mark_sgpr_ballot_pair(uint16_t s) {
        if (s < 105u) {
            sgpr_is_ballot_pair_[s] = true;
        }
    }
    bool is_sgpr_ballot_pair(uint16_t s) const noexcept {
        return s < 105u && sgpr_is_ballot_pair_[s];
    }

    enum class SgprKind : uint8_t {
        Unknown = 0,
        VbTablePtr,
        DescTablePtr,
        VbDesc,
        CbvDesc,
        TexSrvDesc
    };
    void set_sgpr_desc(uint16_t s, SgprKind kind, uint32_t slot = 0u) {
        if (s < 106u) {
            sgpr_kind_[s] = kind;
            sgpr_slot_[s] = slot;
        }
    }
    SgprKind get_sgpr_kind(uint16_t s) const noexcept {
        return (s < 106u) ? sgpr_kind_[s] : SgprKind::Unknown;
    }
    uint32_t get_sgpr_slot(uint16_t s) const noexcept {
        return (s < 106u) ? sgpr_slot_[s] : 0u;
    }

private:
    static float inline_float_val(uint16_t code) noexcept {
        switch (code) {
            case reg_code::INLINE_F_0_5:  return 0.5f;
            case reg_code::INLINE_F_N0_5: return -0.5f;
            case reg_code::INLINE_F_1_0:  return 1.0f;
            case reg_code::INLINE_F_N1_0: return -1.0f;
            case reg_code::INLINE_F_2_0:  return 2.0f;
            case reg_code::INLINE_F_N2_0: return -2.0f;
            case reg_code::INLINE_F_4_0:  return 4.0f;
            case reg_code::INLINE_F_N4_0: return -4.0f;
            case reg_code::INLINE_INV2PI: return 0.15915494309189535f;
            default:                      return 0.0f;
        }
    }

    IrModule& mod_;
    std::array<uint32_t, 106> sgpr_{};
    std::array<bool, 106>     sgpr_is_ballot_pair_{};
    std::array<SgprKind, 106> sgpr_kind_{};
    std::array<uint32_t, 106> sgpr_slot_{};
    std::array<uint32_t, 256> vgpr_{};
    std::array<IrType, 256>   vgpr_type_{};
    uint32_t scc_{0};
    uint32_t vcc_lo_{0};
    uint32_t vcc_hi_{0};
    uint32_t last_vcc_bool_{0};
    uint32_t exec_lo_{0};
    uint32_t exec_hi_{0};
    uint32_t m0_{0};
    uint16_t last_written_vgpr_{0};
    bool     wrote_any_output_{false};
};

} // namespace

/* ============================================================================
 * Build SSA IR + CFG Structurization
 * ============================================================================ */

IrModule build_ssa_ir_and_structurize(const Rdna2ShaderMetadata& meta,
                                      const std::vector<DecodedInst>& decoded) noexcept {
    IrModule mod{};
    mod.meta = meta;
    mod.uses_wqm = meta.uses_wqm;
    mod.uses_bvh_rt = meta.uses_rt_bvh4;

    if (decoded.empty()) {
        return mod;
    }

    /* Pre-collect vertex buffer load offsets in VS so each maps deterministically to Location 0, 1, ... */
    std::vector<uint32_t> vs_attr_offsets;
    if (meta.stage == ShaderStage::Vertex) {
        for (const auto& di : decoded) {
            if (di.cls == IsaClass::MUBUF || di.cls == IsaClass::MTBUF) {
                const bool is_load =
                    (di.cls == IsaClass::MUBUF && di.opcode >= mubuf_op::BUFFER_LOAD_DWORD && di.opcode <= mubuf_op::BUFFER_LOAD_DWORDX3) ||
                    (di.cls == IsaClass::MUBUF && di.opcode >= mubuf_op::BUFFER_LOAD_FORMAT_X && di.opcode <= mubuf_op::BUFFER_LOAD_FORMAT_XYZW) ||
                    (di.cls == IsaClass::MTBUF && di.opcode <= mtbuf_op::TBUFFER_LOAD_FORMAT_XYZW);
                if (is_load) {
                    if (std::find(vs_attr_offsets.begin(), vs_attr_offsets.end(), di.offset) == vs_attr_offsets.end()) {
                        vs_attr_offsets.push_back(di.offset);
                    }
                }
            }
        }
        std::sort(vs_attr_offsets.begin(), vs_attr_offsets.end());
    }

    /* Step 1: Map word PC -> instruction index & discover CFG block leaders */
    std::unordered_map<uint32_t, uint32_t> pc_to_idx;
    pc_to_idx.reserve(decoded.size() * 2u);
    for (uint32_t idx = 0; idx < decoded.size(); ++idx) {
        pc_to_idx[decoded[idx].pc_words] = idx;
    }

    std::vector<bool> is_leader(decoded.size(), false);
    is_leader[0] = true;

    /* Track structured branch/loop targets by instruction index */
    struct PendingCf {
        uint32_t src_idx;
        uint32_t dst_idx;
        bool is_back_edge;
        bool is_conditional;
        uint16_t branch_op;
    };
    std::vector<PendingCf> branches;

    for (uint32_t idx = 0; idx < decoded.size(); ++idx) {
        const auto& d = decoded[idx];
        if (d.cls == IsaClass::SOPP) {
            const bool is_cond =
                d.opcode == sopp_op::S_CBRANCH_SCC0 ||
                d.opcode == sopp_op::S_CBRANCH_SCC1 ||
                d.opcode == sopp_op::S_CBRANCH_VCCZ ||
                d.opcode == sopp_op::S_CBRANCH_VCCNZ ||
                d.opcode == sopp_op::S_CBRANCH_EXECZ ||
                d.opcode == sopp_op::S_CBRANCH_EXECNZ;
            const bool is_uncond = (d.opcode == sopp_op::S_BRANCH);
            if (is_cond || is_uncond) {
                const int32_t target_pc = static_cast<int32_t>(d.pc_words + d.word_count) +
                                          static_cast<int32_t>(d.simm16);
                auto it = pc_to_idx.find(static_cast<uint32_t>(std::max(0, target_pc)));
                if (it != pc_to_idx.end()) {
                    const uint32_t dst = it->second;
                    is_leader[dst] = true;
                    if (idx + 1u < decoded.size()) {
                        is_leader[idx + 1u] = true;
                    }
                    branches.push_back({idx, dst, dst <= idx, is_cond, d.opcode});
                }
            } else if (d.opcode == sopp_op::S_ENDPGM || d.opcode == sopp_op::S_ENDPGM_SAVED) {
                if (idx + 1u < decoded.size()) {
                    is_leader[idx + 1u] = true;
                }
            }
        }
    }

    /* Partition decoded instructions into basic blocks */
    std::vector<uint32_t> inst_to_block(decoded.size(), 0u);
    for (uint32_t idx = 0; idx < decoded.size(); ++idx) {
        if (is_leader[idx]) {
            CfgBlock blk{};
            blk.block_id = static_cast<uint32_t>(mod.cfg_blocks.size());
            blk.start_inst_idx = idx;
            blk.end_inst_idx = static_cast<uint32_t>(decoded.size());
            if (!mod.cfg_blocks.empty()) {
                mod.cfg_blocks.back().end_inst_idx = idx;
            }
            mod.cfg_blocks.push_back(blk);
        }
        inst_to_block[idx] = static_cast<uint32_t>(mod.cfg_blocks.size() - 1u);
    }

    /* Connect CFG edges & mark loop headers / structured forward regions */
    std::unordered_map<uint32_t, uint32_t> loop_header_end;     /* header_idx -> back_branch_idx */
    std::unordered_map<uint32_t, uint32_t> if_merge_target;     /* branch_idx -> merge_idx */
    std::vector<uint32_t> merge_close_count(decoded.size() + 1u, 0u);

    for (const auto& br : branches) {
        const uint32_t sb = inst_to_block[br.src_idx];
        const uint32_t db = inst_to_block[br.dst_idx];
        mod.cfg_blocks[sb].succs.push_back(db);
        if (br.is_back_edge) {
            mod.cfg_blocks[db].is_loop_header = true;
            mod.cfg_blocks[sb].has_back_edge = true;
            loop_header_end[br.dst_idx] = br.src_idx;
            ++mod.loop_count;
            if (br.branch_op == sopp_op::S_CBRANCH_EXECNZ || br.branch_op == sopp_op::S_CBRANCH_VCCNZ) {
                mod.has_divergent_cf = true;
            }
        } else if (br.is_conditional && br.dst_idx > br.src_idx + 1u) {
            if_merge_target[br.src_idx] = br.dst_idx;
            ++merge_close_count[br.dst_idx];
            ++mod.branch_count;
            if (br.branch_op == sopp_op::S_CBRANCH_EXECZ || br.branch_op == sopp_op::S_CBRANCH_EXECNZ ||
                br.branch_op == sopp_op::S_CBRANCH_VCCZ || br.branch_op == sopp_op::S_CBRANCH_VCCNZ) {
                mod.has_divergent_cf = true;
            }
        }
    }

    /* Step 2: Lower decoded GFX10.3 stream to SSA IR with structured CF markers */
    IrBuilder b(mod);
    if (meta.stage == ShaderStage::Vertex) {
        b.set_sgpr_desc(0u, IrBuilder::SgprKind::VbTablePtr, 0u);
        b.set_sgpr_desc(8u, IrBuilder::SgprKind::DescTablePtr, 0u);
    } else if (meta.stage == ShaderStage::Pixel) {
        b.set_sgpr_desc(8u, IrBuilder::SgprKind::DescTablePtr, 0u);
    }
    std::vector<uint32_t> open_if_merges;
    int open_loops = 0;

    for (uint32_t idx = 0; idx < decoded.size(); ++idx) {
        /* Close any structured IF regions whose merge point is idx */
        while (!open_if_merges.empty() && open_if_merges.back() == idx) {
            b.emit(IrOp::CfEndIf, IrType::Void, ValueUniformity::Uniform);
            open_if_merges.pop_back();
        }

        /* Open structured Loop if idx is a loop header */
        if (loop_header_end.count(idx) > 0u) {
            b.emit(IrOp::CfLoopHeader, IrType::Void, ValueUniformity::Uniform, {}, 4u);
            ++open_loops;
        }

        const auto& d = decoded[idx];
        switch (d.cls) {
            case IsaClass::SOP2: {
                const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                const uint32_t c = b.read_src_u32(d.src[1], d.has_literal, d.literal);
                uint32_t r = 0u;
                switch (d.opcode) {
                    case sop2_op::S_ADD_U32:
                    case sop2_op::S_ADD_I32:
                    case sop2_op::S_ADDC_U32:
                        r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_SUB_U32:
                    case sop2_op::S_SUB_I32:
                    case sop2_op::S_SUBB_U32:
                        r = b.emit(IrOp::ISub, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MIN_I32:
                        r = b.emit(IrOp::IMin, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MIN_U32:
                        r = b.emit(IrOp::UMin, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MAX_I32:
                        r = b.emit(IrOp::IMax, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MAX_U32:
                        r = b.emit(IrOp::UMax, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_CSELECT_B32:
                    case sop2_op::S_CSELECT_B64:
                        r = b.emit(IrOp::SelectU32, IrType::U32, ValueUniformity::Uniform, {b.get_scc(), a, c});
                        break;
                    case sop2_op::S_AND_B32:
                    case sop2_op::S_AND_B64:
                        if (d.opcode == sop2_op::S_AND_B64 &&
                            (d.src[0] == reg_code::VCC_LO || d.src[1] == reg_code::VCC_LO ||
                             b.is_sgpr_ballot_pair(d.src[0]))) {
                            mod.wave_stats.uses_ballot_hi32 = true;
                            b.mark_sgpr_ballot_pair(d.dst);
                        }
                        r = b.emit(IrOp::BitAnd, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_OR_B32:
                    case sop2_op::S_OR_B64:
                        if (d.opcode == sop2_op::S_OR_B64 && d.dst == reg_code::EXEC_LO) {
                            /* Restoring EXEC mask after branch */
                            mod.has_divergent_cf = true;
                        }
                        r = b.emit(IrOp::BitOr, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_XOR_B32:
                    case sop2_op::S_XOR_B64:
                        r = b.emit(IrOp::BitXor, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_ANDN2_B32:
                    case sop2_op::S_ANDN2_B64: {
                        uint32_t nc = b.emit(IrOp::BitNot, IrType::U32, ValueUniformity::Uniform, {c});
                        r = b.emit(IrOp::BitAnd, IrType::U32, ValueUniformity::Uniform, {a, nc});
                        break;
                    }
                    case sop2_op::S_LSHL_B32:
                    case sop2_op::S_LSHL_B64:
                        r = b.emit(IrOp::Shl, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_LSHR_B32:
                    case sop2_op::S_LSHR_B64:
                        r = b.emit(IrOp::LShr, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_ASHR_I32:
                    case sop2_op::S_ASHR_I64:
                        r = b.emit(IrOp::AShr, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MUL_I32:
                        r = b.emit(IrOp::IMul, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MUL_HI_U32:
                        r = b.emit(IrOp::UMulHi, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_MUL_HI_I32:
                        r = b.emit(IrOp::IMulHi, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                    case sop2_op::S_LSHL1_ADD_U32:
                    case sop2_op::S_LSHL2_ADD_U32:
                    case sop2_op::S_LSHL3_ADD_U32:
                    case sop2_op::S_LSHL4_ADD_U32: {
                        uint32_t sh = static_cast<uint32_t>(d.opcode - sop2_op::S_LSHL1_ADD_U32 + 1u);
                        uint32_t s = b.emit(IrOp::Shl, IrType::U32, ValueUniformity::Uniform, {a, b.const_u32(sh)});
                        r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Uniform, {s, c});
                        break;
                    }
                    default:
                        r = b.emit(IrOp::BitOr, IrType::U32, ValueUniformity::Uniform, {a, c});
                        break;
                }
                b.write_sgpr(d.dst, r);
                b.set_scc(b.emit(IrOp::CmpNeU32, IrType::Bool, ValueUniformity::Uniform, {r, b.const_u32(0u)}));
                break;
            }

            case IsaClass::SOPK: {
                const uint32_t imm = static_cast<uint32_t>(static_cast<int32_t>(d.simm16));
                const uint32_t imm_id = b.const_u32(imm);
                switch (d.opcode) {
                    case sopk_op::S_MOVK_I32:
                        b.write_sgpr(d.dst, imm_id);
                        break;
                    case sopk_op::S_CMOVK_I32: {
                        uint32_t cur = b.read_src_u32(d.dst, false, 0u);
                        uint32_t sel = b.emit(IrOp::SelectU32, IrType::U32, ValueUniformity::Uniform,
                                              {b.get_scc(), imm_id, cur});
                        b.write_sgpr(d.dst, sel);
                        break;
                    }
                    case sopk_op::S_ADDK_I32: {
                        uint32_t cur = b.read_src_u32(d.dst, false, 0u);
                        uint32_t r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Uniform, {cur, imm_id});
                        b.write_sgpr(d.dst, r);
                        b.set_scc(b.emit(IrOp::CmpNeU32, IrType::Bool, ValueUniformity::Uniform, {r, b.const_u32(0u)}));
                        break;
                    }
                    case sopk_op::S_MULK_I32: {
                        uint32_t cur = b.read_src_u32(d.dst, false, 0u);
                        uint32_t r = b.emit(IrOp::IMul, IrType::U32, ValueUniformity::Uniform, {cur, imm_id});
                        b.write_sgpr(d.dst, r);
                        break;
                    }
                    case sopk_op::S_CMPK_EQ_I32:
                    case sopk_op::S_CMPK_EQ_U32:
                        b.set_scc(b.emit(IrOp::CmpEqU32, IrType::Bool, ValueUniformity::Uniform,
                                         {b.read_src_u32(d.dst, false, 0u), imm_id}));
                        break;
                    case sopk_op::S_CMPK_LG_I32:
                    case sopk_op::S_CMPK_LG_U32:
                        b.set_scc(b.emit(IrOp::CmpNeU32, IrType::Bool, ValueUniformity::Uniform,
                                         {b.read_src_u32(d.dst, false, 0u), imm_id}));
                        break;
                    case sopk_op::S_CMPK_LT_I32:
                        b.set_scc(b.emit(IrOp::CmpLtI32, IrType::Bool, ValueUniformity::Uniform,
                                         {b.read_src_u32(d.dst, false, 0u), imm_id}));
                        break;
                    case sopk_op::S_CMPK_GT_I32:
                        b.set_scc(b.emit(IrOp::CmpGtI32, IrType::Bool, ValueUniformity::Uniform,
                                         {b.read_src_u32(d.dst, false, 0u), imm_id}));
                        break;
                    case sopk_op::S_CMPK_LT_U32:
                        b.set_scc(b.emit(IrOp::CmpLtU32, IrType::Bool, ValueUniformity::Uniform,
                                         {b.read_src_u32(d.dst, false, 0u), imm_id}));
                        break;
                    case sopk_op::S_CMPK_GT_U32:
                        b.set_scc(b.emit(IrOp::CmpGtU32, IrType::Bool, ValueUniformity::Uniform,
                                         {b.read_src_u32(d.dst, false, 0u), imm_id}));
                        break;
                    default:
                        b.write_sgpr(d.dst, imm_id);
                        break;
                }
                break;
            }

            case IsaClass::SOP1: {
                const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                uint32_t r = a;
                switch (d.opcode) {
                    case sop1_op::S_MOV_B32:
                    case sop1_op::S_MOV_B64:
                        r = a;
                        if (d.opcode == sop1_op::S_MOV_B64 && d.src[0] == reg_code::VCC_LO) {
                            b.mark_sgpr_ballot_pair(d.dst);
                        }
                        if (d.src[0] < 106u) {
                            b.set_sgpr_desc(d.dst, b.get_sgpr_kind(d.src[0]), b.get_sgpr_slot(d.src[0]));
                        }
                        break;
                    case sop1_op::S_NOT_B32:
                    case sop1_op::S_NOT_B64:
                        r = b.emit(IrOp::BitNot, IrType::U32, ValueUniformity::Uniform, {a});
                        break;
                    case sop1_op::S_WQM_B32:
                    case sop1_op::S_WQM_B64:
                        mod.uses_wqm = true;
                        mod.has_subgroup_ops = true;
                        r = b.emit(IrOp::WqmMask, IrType::U32, ValueUniformity::Uniform, {a});
                        break;
                    case sop1_op::S_QUADMASK_B32:
                    case sop1_op::S_QUADMASK_B64:
                        mod.uses_wqm = true;
                        mod.has_subgroup_ops = true;
                        r = b.emit(IrOp::WqmMask, IrType::U32, ValueUniformity::Uniform, {a});
                        break;
                    case sop1_op::S_BCNT1_I32_B32:
                        r = b.emit(IrOp::BitCount32, IrType::U32, ValueUniformity::Uniform, {a});
                        break;
                    case sop1_op::S_BCNT1_I32_B64:
                    case sop1_op::S_BCNT0_I32_B64:
                        /* 64-bit ballot bitcount -> cross-half-wave dependency! */
                        mod.has_subgroup_ops = true;
                        mod.wave_stats.uses_bitcnt_b64 = true;
                        mod.wave_stats.uses_ballot_hi32 = true;
                        r = b.emit(IrOp::WaveBallotBitCount64, IrType::U32, ValueUniformity::Uniform,
                                   {b.get_vcc_bool()});
                        break;
                    case sop1_op::S_FF1_I32_B32:
                    case sop1_op::S_FF1_I32_B64:
                        if (d.opcode == sop1_op::S_FF1_I32_B64) {
                            mod.wave_stats.uses_ballot_hi32 = true;
                        }
                        r = b.emit(IrOp::FindLsb, IrType::U32, ValueUniformity::Uniform, {a});
                        break;
                    case sop1_op::S_FLBIT_I32_B32:
                    case sop1_op::S_FLBIT_I32_B64:
                        if (d.opcode == sop1_op::S_FLBIT_I32_B64) {
                            mod.wave_stats.uses_ballot_hi32 = true;
                        }
                        r = b.emit(IrOp::FindMsb, IrType::U32, ValueUniformity::Uniform, {a});
                        break;
                    case sop1_op::S_AND_SAVEEXEC_B64:
                    case sop1_op::S_AND_SAVEEXEC_B32:
                    case sop1_op::S_XOR_SAVEEXEC_B64:
                    case sop1_op::S_XOR_SAVEEXEC_B32:
                    case sop1_op::S_OR_SAVEEXEC_B64:
                    case sop1_op::S_OR_SAVEEXEC_B32:
                    case sop1_op::S_ANDN2_SAVEEXEC_B64:
                    case sop1_op::S_ANDN2_SAVEEXEC_B32:
                        mod.has_divergent_cf = true;
                        r = b.read_src_u32(reg_code::EXEC_LO, false, 0u);
                        b.write_sgpr(reg_code::EXEC_LO, a);
                        b.set_scc(b.get_vcc_bool());
                        break;
                    default:
                        r = a;
                        break;
                }
                b.write_sgpr(d.dst, r);
                break;
            }

            case IsaClass::SOPC: {
                const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                const uint32_t c = b.read_src_u32(d.src[1], d.has_literal, d.literal);
                IrOp cmp = IrOp::CmpEqU32;
                switch (d.opcode) {
                    case sopc_op::S_CMP_EQ_I32:
                    case sopc_op::S_CMP_EQ_U32:
                    case sopc_op::S_CMP_EQ_U64: cmp = IrOp::CmpEqU32; break;
                    case sopc_op::S_CMP_LG_I32:
                    case sopc_op::S_CMP_LG_U32:
                    case sopc_op::S_CMP_LG_U64: cmp = IrOp::CmpNeU32; break;
                    case sopc_op::S_CMP_LT_I32: cmp = IrOp::CmpLtI32; break;
                    case sopc_op::S_CMP_LE_I32: cmp = IrOp::CmpLeI32; break;
                    case sopc_op::S_CMP_GT_I32: cmp = IrOp::CmpGtI32; break;
                    case sopc_op::S_CMP_GE_I32: cmp = IrOp::CmpGeI32; break;
                    case sopc_op::S_CMP_LT_U32: cmp = IrOp::CmpLtU32; break;
                    case sopc_op::S_CMP_LE_U32: cmp = IrOp::CmpLeU32; break;
                    case sopc_op::S_CMP_GT_U32: cmp = IrOp::CmpGtU32; break;
                    case sopc_op::S_CMP_GE_U32: cmp = IrOp::CmpGeU32; break;
                    default:                    cmp = IrOp::CmpNeU32; break;
                }
                b.set_scc(b.emit(cmp, IrType::Bool, ValueUniformity::Uniform, {a, c}));
                break;
            }

            case IsaClass::SOPP: {
                if (d.opcode == sopp_op::S_BARRIER) {
                    b.emit(IrOp::Barrier, IrType::Void, ValueUniformity::Uniform);
                } else if (if_merge_target.count(idx) > 0u) {
                    /* Forward conditional skip -> structured CfIf */
                    uint32_t cond = b.get_scc();
                    if (d.opcode == sopp_op::S_CBRANCH_EXECZ || d.opcode == sopp_op::S_CBRANCH_VCCZ) {
                        cond = b.get_vcc_bool();
                    }
                    b.emit(IrOp::CfIf, IrType::Void,
                           (d.opcode == sopp_op::S_CBRANCH_EXECZ || d.opcode == sopp_op::S_CBRANCH_VCCZ)
                               ? ValueUniformity::Divergent
                               : ValueUniformity::Uniform,
                           {cond});
                    open_if_merges.push_back(if_merge_target[idx]);
                } else if (open_loops > 0 &&
                           (d.opcode == sopp_op::S_CBRANCH_SCC1 ||
                            d.opcode == sopp_op::S_CBRANCH_SCC0 ||
                            d.opcode == sopp_op::S_CBRANCH_EXECNZ ||
                            d.opcode == sopp_op::S_CBRANCH_VCCNZ)) {
                    const int32_t target_pc = static_cast<int32_t>(d.pc_words + d.word_count) +
                                              static_cast<int32_t>(d.simm16);
                    if (target_pc <= static_cast<int32_t>(d.pc_words)) {
                        uint32_t cond = (d.opcode == sopp_op::S_CBRANCH_EXECNZ ||
                                         d.opcode == sopp_op::S_CBRANCH_VCCNZ)
                                            ? b.get_vcc_bool()
                                            : b.get_scc();
                        b.emit(IrOp::CfLoopBreakIfNot, IrType::Void, ValueUniformity::Uniform, {cond});
                        b.emit(IrOp::CfLoopEnd, IrType::Void, ValueUniformity::Uniform);
                        --open_loops;
                    }
                }
                break;
            }

            case IsaClass::SMEM: {
                uint32_t count = 1u;
                switch (d.opcode) {
                    case smem_op::S_LOAD_DWORDX2:
                    case smem_op::S_BUFFER_LOAD_DWORDX2: count = 2u; break;
                    case smem_op::S_LOAD_DWORDX4:
                    case smem_op::S_BUFFER_LOAD_DWORDX4: count = 4u; break;
                    case smem_op::S_LOAD_DWORDX8:
                    case smem_op::S_BUFFER_LOAD_DWORDX8: count = 8u; break;
                    case smem_op::S_LOAD_DWORDX16:
                    case smem_op::S_BUFFER_LOAD_DWORDX16: count = 16u; break;
                    default: count = 1u; break;
                }
                const bool is_sbuf = (d.opcode >= smem_op::S_BUFFER_LOAD_DWORD &&
                                      d.opcode <= smem_op::S_BUFFER_LOAD_DWORDX16);
                const auto src_kind = b.get_sgpr_kind(d.src[0]);
                const uint32_t src_slot = b.get_sgpr_slot(d.src[0]);
                if (!is_sbuf && src_kind == IrBuilder::SgprKind::DescTablePtr) {
                    const uint32_t slot = (d.offset / 32u) & 7u;
                    const auto res_kind = (count >= 8u)
                        ? IrBuilder::SgprKind::TexSrvDesc
                        : IrBuilder::SgprKind::CbvDesc;
                    b.set_sgpr_desc(d.dst, res_kind, slot);
                    for (uint32_t k = 0; k < count; ++k) {
                        b.write_sgpr(static_cast<uint16_t>(d.dst + k), b.const_u32(slot));
                    }
                } else if (!is_sbuf && src_kind == IrBuilder::SgprKind::VbTablePtr) {
                    b.set_sgpr_desc(d.dst, IrBuilder::SgprKind::VbDesc, d.offset / 16u);
                    for (uint32_t k = 0; k < count; ++k) {
                        b.write_sgpr(static_cast<uint16_t>(d.dst + k), b.const_u32(0u));
                    }
                } else if (is_sbuf && (meta.stage == ShaderStage::Vertex || meta.stage == ShaderStage::Pixel)) {
                    const uint32_t cbv_slot = (src_kind == IrBuilder::SgprKind::CbvDesc) ? (src_slot & 7u) : 0u;
                    mod.cbv_slot_mask |= (1u << cbv_slot);
                    mod.uses_storage_buffer = true;
                    for (uint32_t k = 0; k < count; ++k) {
                        const uint32_t dw_idx = (d.offset / 4u) + k;
                        uint32_t v = b.emit(IrOp::LoadCbvU32, IrType::U32, ValueUniformity::Uniform,
                                            {}, dw_idx, 0.0f, false, cbv_slot);
                        b.write_sgpr(static_cast<uint16_t>(d.dst + k), v);
                    }
                } else {
                    mod.uses_storage_buffer = true;
                    for (uint32_t k = 0; k < count; ++k) {
                        const uint32_t slot = (d.offset / 4u) + k;
                        uint32_t v = b.emit(IrOp::LoadInputU32, IrType::U32, ValueUniformity::Uniform,
                                            {}, slot, 0.0f, false, 1u);
                        b.write_sgpr(static_cast<uint16_t>(d.dst + k), v);
                    }
                }
                break;
            }

            case IsaClass::VOP2: {
                const uint16_t op = d.opcode;
                switch (op) {
                    case vop2_op::V_ADD_F32:
                    case vop2_op::V_SUB_F32:
                    case vop2_op::V_SUBREV_F32:
                    case vop2_op::V_MUL_F32:
                    case vop2_op::V_MUL_LEGACY_F32:
                    case vop2_op::V_MIN_F32:
                    case vop2_op::V_MAX_F32: {
                        const uint32_t a = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                        const uint32_t c = b.read_src_f32(d.src[1], false, 0u);
                        IrOp irop = IrOp::FAdd;
                        uint32_t lhs = a, rhs = c;
                        if (op == vop2_op::V_SUB_F32) irop = IrOp::FSub;
                        else if (op == vop2_op::V_SUBREV_F32) { irop = IrOp::FSub; lhs = c; rhs = a; }
                        else if (op == vop2_op::V_MUL_F32 || op == vop2_op::V_MUL_LEGACY_F32) irop = IrOp::FMul;
                        else if (op == vop2_op::V_MIN_F32) irop = IrOp::FMin;
                        else if (op == vop2_op::V_MAX_F32) irop = IrOp::FMax;
                        uint32_t r = b.emit(irop, IrType::F32, ValueUniformity::Divergent, {lhs, rhs});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop2_op::V_FMAC_F32: {
                        const uint32_t a = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                        const uint32_t c = b.read_src_f32(d.src[1], false, 0u);
                        const uint32_t acc = b.read_vgpr_f32(d.dst);
                        uint32_t r = b.emit(IrOp::FFma, IrType::F32, ValueUniformity::Divergent, {a, c, acc});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop2_op::V_FMAMK_F32: {
                        const uint32_t a = b.read_src_f32(d.src[0], false, 0u);
                        const uint32_t k = b.const_f32(bits_to_float(d.literal));
                        const uint32_t c = b.read_src_f32(d.src[1], false, 0u);
                        uint32_t r = b.emit(IrOp::FFma, IrType::F32, ValueUniformity::Divergent, {a, k, c});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop2_op::V_FMAAK_F32: {
                        const uint32_t a = b.read_src_f32(d.src[0], false, 0u);
                        const uint32_t c = b.read_src_f32(d.src[1], false, 0u);
                        const uint32_t k = b.const_f32(bits_to_float(d.literal));
                        uint32_t r = b.emit(IrOp::FFma, IrType::F32, ValueUniformity::Divergent, {a, c, k});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop2_op::V_ADD_F16:
                    case vop2_op::V_SUB_F16:
                    case vop2_op::V_MUL_F16:
                    case vop2_op::V_FMAC_F16:
                    case vop2_op::V_MIN_F16:
                    case vop2_op::V_MAX_F16: {
                        mod.uses_f16 = true;
                        const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        const uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        IrOp irop = IrOp::PkF16Add;
                        if (op == vop2_op::V_MUL_F16) irop = IrOp::PkF16Mul;
                        else if (op == vop2_op::V_MIN_F16) irop = IrOp::PkF16Min;
                        else if (op == vop2_op::V_MAX_F16) irop = IrOp::PkF16Max;
                        else if (op == vop2_op::V_FMAC_F16) {
                            const uint32_t acc = b.read_vgpr_u32(d.dst);
                            uint32_t r = b.emit(IrOp::PkF16Fma, IrType::U32, ValueUniformity::Divergent, {a, c, acc});
                            b.write_vgpr(d.dst, r, IrType::U32);
                            break;
                        }
                        uint32_t r = b.emit(irop, IrType::U32, ValueUniformity::Divergent, {a, c});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop2_op::V_DOT2C_F32_F16: {
                        mod.uses_f16 = true;
                        const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        const uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        const uint32_t acc = b.read_vgpr_f32(d.dst);
                        uint32_t r = b.emit(IrOp::Dot2F32F16, IrType::F32, ValueUniformity::Divergent, {a, c, acc});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop2_op::V_CNDMASK_B32: {
                        const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        const uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        uint32_t r = b.emit(IrOp::SelectU32, IrType::U32, ValueUniformity::Divergent,
                                            {b.get_vcc_bool(), c, a});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    default: {
                        const uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        const uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        IrOp irop = IrOp::IAdd;
                        uint32_t lhs = a, rhs = c;
                        switch (op) {
                            case vop2_op::V_ADD_NC_U32:
                            case vop2_op::V_ADD_CO_CI_U32: irop = IrOp::IAdd; break;
                            case vop2_op::V_SUB_NC_U32:
                            case vop2_op::V_SUB_CO_CI_U32: irop = IrOp::ISub; break;
                            case vop2_op::V_SUBREV_NC_U32: irop = IrOp::ISub; lhs = c; rhs = a; break;
                            case vop2_op::V_MUL_I32_I24:
                            case vop2_op::V_MUL_U32_U24:   irop = IrOp::IMul; break;
                            case vop2_op::V_MIN_I32:       irop = IrOp::IMin; break;
                            case vop2_op::V_MAX_I32:       irop = IrOp::IMax; break;
                            case vop2_op::V_MIN_U32:       irop = IrOp::UMin; break;
                            case vop2_op::V_MAX_U32:       irop = IrOp::UMax; break;
                            case vop2_op::V_LSHLREV_B32:   irop = IrOp::Shl; lhs = c; rhs = a; break;
                            case vop2_op::V_LSHRREV_B32:   irop = IrOp::LShr; lhs = c; rhs = a; break;
                            case vop2_op::V_ASHRREV_I32:   irop = IrOp::AShr; lhs = c; rhs = a; break;
                            case vop2_op::V_AND_B32:       irop = IrOp::BitAnd; break;
                            case vop2_op::V_OR_B32:        irop = IrOp::BitOr; break;
                            case vop2_op::V_XOR_B32:       irop = IrOp::BitXor; break;
                            default:                       irop = IrOp::IAdd; break;
                        }
                        uint32_t r = b.emit(irop, IrType::U32, ValueUniformity::Divergent, {lhs, rhs});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                }
                break;
            }

            case IsaClass::VOP1: {
                const uint16_t op = d.opcode;
                switch (op) {
                    case vop1_op::V_NOP:
                        break;
                    case vop1_op::V_MOV_B32: {
                        if (d.src[0] >= reg_code::INLINE_F_0_5 && d.src[0] <= reg_code::INLINE_INV2PI) {
                            b.write_vgpr(d.dst, b.read_src_f32(d.src[0], d.has_literal, d.literal), IrType::F32);
                        } else {
                            b.write_vgpr(d.dst, b.read_src_u32(d.src[0], d.has_literal, d.literal), IrType::U32);
                        }
                        break;
                    }
                    case vop1_op::V_READFIRSTLANE_B32: {
                        mod.has_subgroup_ops = true;
                        uint32_t v = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t r = b.emit(IrOp::ReadFirstLane, IrType::U32, ValueUniformity::Uniform, {v});
                        b.write_sgpr(d.dst, r);
                        break;
                    }
                    case vop1_op::V_CVT_F32_I32: {
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::CvtI32ToF32, IrType::F32, ValueUniformity::Divergent, {v}), IrType::F32);
                        break;
                    }
                    case vop1_op::V_CVT_F32_U32: {
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::CvtU32ToF32, IrType::F32, ValueUniformity::Divergent, {v}), IrType::F32);
                        break;
                    }
                    case vop1_op::V_CVT_I32_F32:
                    case vop1_op::V_CVT_RPI_I32_F32:
                    case vop1_op::V_CVT_FLR_I32_F32: {
                        uint32_t v = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::CvtF32ToI32, IrType::U32, ValueUniformity::Divergent, {v}), IrType::U32);
                        break;
                    }
                    case vop1_op::V_CVT_U32_F32: {
                        uint32_t v = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::CvtF32ToU32, IrType::U32, ValueUniformity::Divergent, {v}), IrType::U32);
                        break;
                    }
                    case vop1_op::V_CVT_F16_F32: {
                        mod.uses_f16 = true;
                        uint32_t v = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::CvtF32ToF16, IrType::U32, ValueUniformity::Divergent, {v}), IrType::U32);
                        break;
                    }
                    case vop1_op::V_CVT_F32_F16: {
                        mod.uses_f16 = true;
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::CvtF16ToF32, IrType::F32, ValueUniformity::Divergent, {v}), IrType::F32);
                        break;
                    }
                    case vop1_op::V_FRACT_F32:
                    case vop1_op::V_TRUNC_F32:
                    case vop1_op::V_CEIL_F32:
                    case vop1_op::V_RNDNE_F32:
                    case vop1_op::V_FLOOR_F32:
                    case vop1_op::V_EXP_F32:
                    case vop1_op::V_LOG_F32:
                    case vop1_op::V_RCP_F32:
                    case vop1_op::V_RCP_IFLAG_F32:
                    case vop1_op::V_RSQ_F32:
                    case vop1_op::V_SQRT_F32:
                    case vop1_op::V_SIN_F32:
                    case vop1_op::V_COS_F32: {
                        uint32_t v = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                        IrOp irop = IrOp::FSqrt;
                        if (op == vop1_op::V_FRACT_F32) irop = IrOp::FFract;
                        else if (op == vop1_op::V_TRUNC_F32) irop = IrOp::FTrunc;
                        else if (op == vop1_op::V_CEIL_F32) irop = IrOp::FCeil;
                        else if (op == vop1_op::V_RNDNE_F32) irop = IrOp::FRoundEven;
                        else if (op == vop1_op::V_FLOOR_F32) irop = IrOp::FFloor;
                        else if (op == vop1_op::V_EXP_F32) irop = IrOp::FExp2;
                        else if (op == vop1_op::V_LOG_F32) irop = IrOp::FLog2;
                        else if (op == vop1_op::V_RCP_F32 || op == vop1_op::V_RCP_IFLAG_F32) irop = IrOp::FRcp;
                        else if (op == vop1_op::V_RSQ_F32) irop = IrOp::FRsq;
                        else if (op == vop1_op::V_SIN_F32) irop = IrOp::FSin;
                        else if (op == vop1_op::V_COS_F32) irop = IrOp::FCos;
                        b.write_vgpr(d.dst, b.emit(irop, IrType::F32, ValueUniformity::Divergent, {v}), IrType::F32);
                        break;
                    }
                    case vop1_op::V_NOT_B32: {
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::BitNot, IrType::U32, ValueUniformity::Divergent, {v}), IrType::U32);
                        break;
                    }
                    case vop1_op::V_FFBL_B32: {
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::FindLsb, IrType::U32, ValueUniformity::Divergent, {v}), IrType::U32);
                        break;
                    }
                    case vop1_op::V_FFBH_U32:
                    case vop1_op::V_FFBH_I32: {
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, b.emit(IrOp::FindMsb, IrType::U32, ValueUniformity::Divergent, {v}), IrType::U32);
                        break;
                    }
                    default: {
                        uint32_t v = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                        b.write_vgpr(d.dst, v, IrType::U32);
                        break;
                    }
                }
                break;
            }

            case IsaClass::VOPC: {
                const uint16_t op = d.opcode;
                const bool writes_exec = ((op & 0x20u) != 0u && op < 0x40u) || (op >= 0xE0u);
                const bool is_float = (op < 0x80u);
                uint32_t cond = 0u;
                if (is_float) {
                    uint32_t a = b.read_src_f32(d.src[0], d.has_literal, d.literal);
                    uint32_t c = b.read_src_f32(d.src[1], false, 0u);
                    IrOp cmp = IrOp::CmpLtF32;
                    switch (op & 0x0Fu) {
                        case 0x1u: cmp = IrOp::CmpLtF32; break;
                        case 0x2u: cmp = IrOp::CmpEqF32; break;
                        case 0x3u: cmp = IrOp::CmpLeF32; break;
                        case 0x4u: cmp = IrOp::CmpGtF32; break;
                        case 0x6u: cmp = IrOp::CmpGeF32; break;
                        default:   cmp = IrOp::CmpNeF32; break;
                    }
                    cond = b.emit(cmp, IrType::Bool, ValueUniformity::Divergent, {a, c});
                } else {
                    uint32_t a = b.read_src_u32(d.src[0], d.has_literal, d.literal);
                    uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                    const bool is_signed = (op < 0xC0u);
                    IrOp cmp = IrOp::CmpEqU32;
                    switch (op & 0x0Fu) {
                        case 0x1u: cmp = is_signed ? IrOp::CmpLtI32 : IrOp::CmpLtU32; break;
                        case 0x2u: cmp = IrOp::CmpEqU32; break;
                        case 0x3u: cmp = is_signed ? IrOp::CmpLeI32 : IrOp::CmpLeU32; break;
                        case 0x4u: cmp = is_signed ? IrOp::CmpGtI32 : IrOp::CmpGtU32; break;
                        case 0x5u: cmp = IrOp::CmpNeU32; break;
                        case 0x6u: cmp = is_signed ? IrOp::CmpGeI32 : IrOp::CmpGeU32; break;
                        default:   cmp = IrOp::CmpNeU32; break;
                    }
                    cond = b.emit(cmp, IrType::Bool, ValueUniformity::Divergent, {a, c});
                }
                b.set_vcc_bool(cond, writes_exec);
                mod.has_subgroup_ops = true;
                uint32_t bal_lo = b.emit(IrOp::WaveBallotLo, IrType::U32, ValueUniformity::Uniform, {cond});
                b.write_sgpr(reg_code::VCC_LO, bal_lo);
                break;
            }

            case IsaClass::VOP3A:
            case IsaClass::VOP3B: {
                const uint16_t op = d.opcode;
                const auto fsrc = [&](uint8_t idx_arg) {
                    return b.read_src_f32(d.src[idx_arg], false, 0u,
                                          ((d.abs_mask >> idx_arg) & 1u) != 0u,
                                          ((d.neg_mask >> idx_arg) & 1u) != 0u);
                };
                switch (op) {
                    case vop3_op::V_FMA_F32:
                    case vop3_op::V_MAD_F32: {
                        uint32_t r = b.emit(IrOp::FFma, IrType::F32, ValueUniformity::Divergent,
                                            {fsrc(0), fsrc(1), fsrc(2)}, 0u, 0.0f, d.clamp);
                        if (d.clamp) r = b.emit(IrOp::FClamp01, IrType::F32, ValueUniformity::Divergent, {r});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop3_op::V_FMA_F16:
                    case vop3_op::V_MAD_F16: {
                        mod.uses_f16 = true;
                        uint32_t r = b.emit(IrOp::PkF16Fma, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u),
                                             b.read_src_u32(d.src[2], false, 0u)},
                                            0u, 0.0f, d.clamp);
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_MIN3_F32:
                    case vop3_op::V_MAX3_F32:
                    case vop3_op::V_MED3_F32: {
                        IrOp irop = (op == vop3_op::V_MIN3_F32) ? IrOp::FMin3 :
                                    (op == vop3_op::V_MAX3_F32) ? IrOp::FMax3 : IrOp::FMed3;
                        uint32_t r = b.emit(irop, IrType::F32, ValueUniformity::Divergent,
                                            {fsrc(0), fsrc(1), fsrc(2)}, 0u, 0.0f, d.clamp);
                        if (d.clamp) r = b.emit(IrOp::FClamp01, IrType::F32, ValueUniformity::Divergent, {r});
                        b.write_vgpr(d.dst, r, IrType::F32);
                        break;
                    }
                    case vop3_op::V_MIN3_I32:
                    case vop3_op::V_MAX3_I32:
                    case vop3_op::V_MED3_I32:
                    case vop3_op::V_MIN3_U32:
                    case vop3_op::V_MAX3_U32:
                    case vop3_op::V_MED3_U32: {
                        uint32_t a = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        uint32_t e = b.read_src_u32(d.src[2], false, 0u);
                        const bool is_u = (op == vop3_op::V_MIN3_U32 || op == vop3_op::V_MAX3_U32 || op == vop3_op::V_MED3_U32);
                        IrOp min_op = is_u ? IrOp::UMin : IrOp::IMin;
                        IrOp max_op = is_u ? IrOp::UMax : IrOp::IMax;
                        uint32_t r = 0u;
                        if (op == vop3_op::V_MIN3_I32 || op == vop3_op::V_MIN3_U32) {
                            uint32_t t = b.emit(min_op, IrType::U32, ValueUniformity::Divergent, {a, c});
                            r = b.emit(min_op, IrType::U32, ValueUniformity::Divergent, {t, e});
                        } else if (op == vop3_op::V_MAX3_I32 || op == vop3_op::V_MAX3_U32) {
                            uint32_t t = b.emit(max_op, IrType::U32, ValueUniformity::Divergent, {a, c});
                            r = b.emit(max_op, IrType::U32, ValueUniformity::Divergent, {t, e});
                        } else {
                            uint32_t mn = b.emit(min_op, IrType::U32, ValueUniformity::Divergent, {a, c});
                            uint32_t mx = b.emit(max_op, IrType::U32, ValueUniformity::Divergent, {a, c});
                            uint32_t t = b.emit(min_op, IrType::U32, ValueUniformity::Divergent, {mx, e});
                            r = b.emit(max_op, IrType::U32, ValueUniformity::Divergent, {mn, t});
                        }
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_CVT_PKRTZ_F16_F32: {
                        mod.uses_f16 = true;
                        uint32_t r = b.emit(IrOp::PackF16x2, IrType::U32, ValueUniformity::Divergent,
                                            {fsrc(0), fsrc(1)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_READLANE_B32: {
                        mod.has_subgroup_ops = true;
                        uint32_t v = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t lane = b.read_src_u32(d.src[1], false, 0u);
                        /* Check if lane index can reach >= 32 (cross-half-wave!) */
                        bool cross_half = true;
                        if (d.src[1] >= reg_code::INLINE_INT_0 && d.src[1] <= 192u) {
                            const uint32_t lane_imm = d.src[1] - reg_code::INLINE_INT_0;
                            cross_half = (lane_imm >= 32u);
                        }
                        if (cross_half) {
                            mod.wave_stats.uses_v_readlane = true;
                        }
                        uint32_t r = b.emit(IrOp::ReadLane, IrType::U32, ValueUniformity::Uniform,
                                            {v, lane}, cross_half ? 1u : 0u);
                        b.write_sgpr(d.dst, r);
                        break;
                    }
                    case vop3_op::V_PERMLANE16_B32:
                    case vop3_op::V_PERMLANEX16_B32: {
                        mod.has_subgroup_ops = true;
                        if (op == vop3_op::V_PERMLANEX16_B32) {
                            mod.wave_stats.uses_permlane = true;
                        }
                        uint32_t v = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t r = b.emit(IrOp::Permlane16, IrType::U32, ValueUniformity::Divergent,
                                            {v}, op == vop3_op::V_PERMLANEX16_B32 ? 1u : 0u);
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_MBCNT_LO_U32_B32: {
                        mod.has_subgroup_ops = true;
                        uint32_t a = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        uint32_t r = b.emit(IrOp::WaveMbcntLo, IrType::U32, ValueUniformity::Divergent, {a, c});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_MBCNT_HI_U32_B32: {
                        mod.has_subgroup_ops = true;
                        mod.wave_stats.uses_mbcnt_hi = true;
                        uint32_t a = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        uint32_t r = b.emit(IrOp::WaveMbcntHi, IrType::U32, ValueUniformity::Divergent, {a, c});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_ADD_CO_U32:
                    case vop3_op::V_SUB_CO_U32:
                    case vop3_op::V_SUBREV_CO_U32: {
                        uint32_t a = b.read_src_u32(d.src[0], false, 0u);
                        uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                        IrOp irop = (op == vop3_op::V_ADD_CO_U32) ? IrOp::IAdd : IrOp::ISub;
                        uint32_t lhs = (op == vop3_op::V_SUBREV_CO_U32) ? c : a;
                        uint32_t rhs = (op == vop3_op::V_SUBREV_CO_U32) ? a : c;
                        uint32_t r = b.emit(irop, IrType::U32, ValueUniformity::Divergent, {lhs, rhs});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        uint32_t carry = b.emit(IrOp::CmpLtU32, IrType::Bool, ValueUniformity::Divergent, {r, lhs});
                        b.set_vcc_bool(carry, false);
                        if (d.has_sdst) {
                            mod.has_subgroup_ops = true;
                            uint32_t bal = b.emit(IrOp::WaveBallotLo, IrType::U32, ValueUniformity::Uniform, {carry});
                            b.write_sgpr(d.sdst, bal);
                        }
                        break;
                    }
                    case vop3_op::V_BFE_U32:
                    case vop3_op::V_BFE_I32: {
                        uint32_t r = b.emit(IrOp::BfeU32, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u),
                                             b.read_src_u32(d.src[2], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_BFI_B32: {
                        uint32_t r = b.emit(IrOp::BfiB32, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u),
                                             b.read_src_u32(d.src[2], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_MUL_LO_U32: {
                        uint32_t r = b.emit(IrOp::IMul, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_MUL_HI_U32: {
                        uint32_t r = b.emit(IrOp::UMulHi, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_MUL_HI_I32: {
                        uint32_t r = b.emit(IrOp::IMulHi, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_ADD3_U32: {
                        uint32_t t = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        uint32_t r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Divergent,
                                            {t, b.read_src_u32(d.src[2], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_LSHL_ADD_U32: {
                        uint32_t s = b.emit(IrOp::Shl, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        uint32_t r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Divergent,
                                            {s, b.read_src_u32(d.src[2], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    case vop3_op::V_AND_OR_B32: {
                        uint32_t t = b.emit(IrOp::BitAnd, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        uint32_t r = b.emit(IrOp::BitOr, IrType::U32, ValueUniformity::Divergent,
                                            {t, b.read_src_u32(d.src[2], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                    default: {
                        uint32_t r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Divergent,
                                            {b.read_src_u32(d.src[0], false, 0u),
                                             b.read_src_u32(d.src[1], false, 0u)});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                }
                break;
            }

            case IsaClass::VOP3P: {
                mod.uses_f16 = true;
                const uint32_t a = b.read_src_u32(d.src[0], false, 0u);
                const uint32_t c = b.read_src_u32(d.src[1], false, 0u);
                const uint32_t e = b.read_src_u32(d.src[2], false, 0u);
                switch (d.opcode) {
                    case vop3p_op::V_PK_ADD_F16:
                        b.write_vgpr(d.dst, b.emit(IrOp::PkF16Add, IrType::U32, ValueUniformity::Divergent, {a, c}, 0u, 0.0f, d.clamp), IrType::U32);
                        break;
                    case vop3p_op::V_PK_MUL_F16:
                        b.write_vgpr(d.dst, b.emit(IrOp::PkF16Mul, IrType::U32, ValueUniformity::Divergent, {a, c}, 0u, 0.0f, d.clamp), IrType::U32);
                        break;
                    case vop3p_op::V_PK_FMA_F16:
                        b.write_vgpr(d.dst, b.emit(IrOp::PkF16Fma, IrType::U32, ValueUniformity::Divergent, {a, c, e}, 0u, 0.0f, d.clamp), IrType::U32);
                        break;
                    case vop3p_op::V_PK_MIN_F16:
                        b.write_vgpr(d.dst, b.emit(IrOp::PkF16Min, IrType::U32, ValueUniformity::Divergent, {a, c}, 0u, 0.0f, d.clamp), IrType::U32);
                        break;
                    case vop3p_op::V_PK_MAX_F16:
                        b.write_vgpr(d.dst, b.emit(IrOp::PkF16Max, IrType::U32, ValueUniformity::Divergent, {a, c}, 0u, 0.0f, d.clamp), IrType::U32);
                        break;
                    case vop3p_op::V_DOT2_F32_F16: {
                        const uint32_t acc = b.read_src_f32(d.src[2], false, 0u);
                        b.write_vgpr(d.dst, b.emit(IrOp::Dot2F32F16, IrType::F32, ValueUniformity::Divergent, {a, c, acc}, 0u, 0.0f, d.clamp), IrType::F32);
                        break;
                    }
                    case vop3p_op::V_FMA_MIX_F32:
                    case vop3p_op::V_FMA_MIXLO_F16:
                    case vop3p_op::V_FMA_MIXHI_F16: {
                        auto read_mix = [&](uint32_t k) -> uint32_t {
                            const bool is_f16 = ((d.op_sel_hi >> k) & 1u) != 0u;
                            const bool is_abs = ((d.abs_mask >> k) & 1u) != 0u;
                            const bool is_neg = ((d.neg_mask >> k) & 1u) != 0u;
                            uint32_t v = 0u;
                            if (!is_f16) {
                                v = b.read_src_f32(d.src[k], d.has_literal, d.literal, is_abs, is_neg);
                            } else {
                                if (d.src[k] == reg_code::INLINE_INT_0) {
                                    v = b.const_f32(0.0f);
                                } else {
                                    uint32_t raw = b.read_src_u32(d.src[k], d.has_literal, d.literal);
                                    uint32_t half_u = raw;
                                    if (((d.op_sel >> k) & 1u) != 0u) {
                                        half_u = b.emit(IrOp::LShr, IrType::U32, ValueUniformity::Divergent,
                                                        {raw, b.const_u32(16u)});
                                    }
                                    v = b.emit(IrOp::CvtF16ToF32, IrType::F32, ValueUniformity::Divergent, {half_u});
                                }
                                if (is_abs) {
                                    v = b.emit(IrOp::FAbs, IrType::F32, ValueUniformity::Divergent, {v});
                                }
                                if (is_neg) {
                                    v = b.emit(IrOp::FNeg, IrType::F32, ValueUniformity::Divergent, {v});
                                }
                            }
                            return v;
                        };
                        uint32_t fa = read_mix(0u);
                        uint32_t fc = read_mix(1u);
                        uint32_t fe = read_mix(2u);
                        uint32_t fma = b.emit(IrOp::FFma, IrType::F32, ValueUniformity::Divergent,
                                              {fa, fc, fe}, 0u, 0.0f, d.clamp);
                        if (d.opcode == vop3p_op::V_FMA_MIX_F32) {
                            b.write_vgpr(d.dst, fma, IrType::F32);
                        } else {
                            uint32_t h = b.emit(IrOp::CvtF32ToF16, IrType::U32, ValueUniformity::Divergent, {fma});
                            uint32_t prev = b.read_vgpr_u32(d.dst);
                            if (d.opcode == vop3p_op::V_FMA_MIXLO_F16) {
                                uint32_t hi = b.emit(IrOp::BitAnd, IrType::U32, ValueUniformity::Divergent,
                                                     {prev, b.const_u32(0xFFFF0000u)});
                                uint32_t lo = b.emit(IrOp::BitAnd, IrType::U32, ValueUniformity::Divergent,
                                                     {h, b.const_u32(0xFFFFu)});
                                b.write_vgpr(d.dst, b.emit(IrOp::BitOr, IrType::U32, ValueUniformity::Divergent, {hi, lo}), IrType::U32);
                            } else {
                                uint32_t lo = b.emit(IrOp::BitAnd, IrType::U32, ValueUniformity::Divergent,
                                                     {prev, b.const_u32(0xFFFFu)});
                                uint32_t hi = b.emit(IrOp::Shl, IrType::U32, ValueUniformity::Divergent,
                                                     {h, b.const_u32(16u)});
                                b.write_vgpr(d.dst, b.emit(IrOp::BitOr, IrType::U32, ValueUniformity::Divergent, {lo, hi}), IrType::U32);
                            }
                        }
                        break;
                    }
                    default: {
                        uint32_t r = b.emit(IrOp::IAdd, IrType::U32, ValueUniformity::Divergent, {a, c});
                        b.write_vgpr(d.dst, r, IrType::U32);
                        break;
                    }
                }
                break;
            }

            case IsaClass::VINTRP: {
                mod.in_attr_mask |= (1u << (d.attr & 15u));
                const uint32_t v = b.read_src_f32(d.src[0], false, 0u);
                uint32_t r = b.emit(IrOp::InterpAttr, IrType::F32, ValueUniformity::Divergent,
                                    {v}, d.attr, 0.0f, false, d.attr_chan);
                b.write_vgpr(d.dst, r, IrType::F32);
                break;
            }

            case IsaClass::DS: {
                if (d.opcode == ds_op::DS_BPERMUTE_B32 || d.opcode == ds_op::DS_PERMUTE_B32) {
                    mod.has_subgroup_ops = true;
                    mod.wave_stats.uses_ds_bpermute = true;
                    const uint32_t addr = b.read_src_u32(d.src[0], false, 0u);
                    const uint32_t data = b.read_src_u32(d.src[1], false, 0u);
                    uint32_t r = b.emit(IrOp::DsBpermute, IrType::U32, ValueUniformity::Divergent, {addr, data});
                    b.write_vgpr(d.dst, r, IrType::U32);
                } else if (d.opcode == ds_op::DS_SWIZZLE_B32) {
                    mod.has_subgroup_ops = true;
                    /* Bit 15 == 1 -> quad/32-lane QDMode/FFT; check if offset crosses 32-lane half */
                    const bool cross32 = ((d.offset & 0x8000u) == 0u) && ((d.offset & 0x0020u) != 0u);
                    if (cross32) {
                        mod.wave_stats.uses_ds_swizzle_cross32 = true;
                    }
                    const uint32_t data = b.read_src_u32(d.src[0], false, 0u);
                    uint32_t r = b.emit(IrOp::DsSwizzle, IrType::U32, ValueUniformity::Divergent,
                                        {data}, d.offset, 0.0f, false, cross32 ? 1u : 0u);
                    b.write_vgpr(d.dst, r, IrType::U32);
                } else if (d.opcode == ds_op::DS_WRITE_B32 || d.opcode == ds_op::DS_WRITE_B64 ||
                           d.opcode == ds_op::DS_WRITE_B128 || d.opcode == ds_op::DS_WRITE2_B32) {
                    mod.uses_lds = true;
                    const uint32_t addr = b.read_src_u32(d.src[0], false, 0u);
                    const uint32_t val = b.read_src_u32(d.src[1], false, 0u);
                    b.emit(IrOp::StoreLdsU32, IrType::Void, ValueUniformity::Divergent, {addr, val}, d.offset0);
                } else if (d.opcode == ds_op::DS_READ_B32 || d.opcode == ds_op::DS_READ_B64 ||
                           d.opcode == ds_op::DS_READ_B128 || d.opcode == ds_op::DS_READ2_B32) {
                    mod.uses_lds = true;
                    const uint32_t addr = b.read_src_u32(d.src[0], false, 0u);
                    uint32_t r = b.emit(IrOp::LoadLdsU32, IrType::U32, ValueUniformity::Divergent, {addr}, d.offset0);
                    b.write_vgpr(d.dst, r, IrType::U32);
                } else {
                    mod.uses_lds = true;
                    const uint32_t addr = b.read_src_u32(d.src[0], false, 0u);
                    const uint32_t val = b.read_src_u32(d.src[1], false, 0u);
                    uint32_t r = b.emit(IrOp::AtomicLdsAdd, IrType::U32, ValueUniformity::Divergent, {addr, val}, d.offset0);
                    b.write_vgpr(d.dst, r, IrType::U32);
                }
                break;
            }

            case IsaClass::MUBUF:
            case IsaClass::MTBUF: {
                const bool is_store = (d.cls == IsaClass::MUBUF &&
                                       ((d.opcode >= mubuf_op::BUFFER_STORE_FORMAT_X && d.opcode <= mubuf_op::BUFFER_STORE_FORMAT_XYZW) ||
                                        (d.opcode >= mubuf_op::BUFFER_STORE_DWORD && d.opcode <= mubuf_op::BUFFER_STORE_DWORDX4))) ||
                                      (d.cls == IsaClass::MTBUF && d.opcode >= mtbuf_op::TBUFFER_STORE_FORMAT_X);
                const bool is_atomic = (d.cls == IsaClass::MUBUF && d.opcode >= mubuf_op::BUFFER_ATOMIC_SWAP);
                const uint32_t vaddr = b.read_src_u32(d.src[0], false, 0u);
                if (is_store) {
                    mod.uses_storage_buffer = true;
                    const uint32_t val = b.read_vgpr_u32(d.dst);
                    b.emit(IrOp::StoreOutputU32, IrType::Void, ValueUniformity::Divergent,
                           {vaddr, val}, d.offset / 4u);
                    b.mark_output_written();
                } else if (is_atomic) {
                    mod.uses_storage_buffer = true;
                    const uint32_t val = b.read_vgpr_u32(d.dst);
                    uint32_t r = b.emit(IrOp::AtomicBufAdd, IrType::U32, ValueUniformity::Divergent,
                                        {vaddr, val}, d.offset / 4u);
                    b.write_vgpr(d.dst, r, IrType::U32);
                    b.mark_output_written();
                } else {
                    uint32_t cnt = 1u;
                    if (d.cls == IsaClass::MTBUF) {
                        cnt = (d.opcode == mtbuf_op::TBUFFER_LOAD_FORMAT_XYZW) ? 4u :
                              (d.opcode == mtbuf_op::TBUFFER_LOAD_FORMAT_XYZ)  ? 3u :
                              (d.opcode == mtbuf_op::TBUFFER_LOAD_FORMAT_XY)   ? 2u : 1u;
                    } else {
                        cnt = (d.opcode == mubuf_op::BUFFER_LOAD_DWORDX4 || d.opcode == mubuf_op::BUFFER_LOAD_FORMAT_XYZW) ? 4u :
                              (d.opcode == mubuf_op::BUFFER_LOAD_DWORDX3 || d.opcode == mubuf_op::BUFFER_LOAD_FORMAT_XYZ)  ? 3u :
                              (d.opcode == mubuf_op::BUFFER_LOAD_DWORDX2 || d.opcode == mubuf_op::BUFFER_LOAD_FORMAT_XY)   ? 2u : 1u;
                    }
                    if (meta.stage == ShaderStage::Vertex && !vs_attr_offsets.empty()) {
                        uint32_t loc = 0u;
                        for (size_t ai = 0u; ai < vs_attr_offsets.size(); ++ai) {
                            if (vs_attr_offsets[ai] == d.offset) {
                                loc = static_cast<uint32_t>(ai);
                                break;
                            }
                        }
                        if (loc < 16u) {
                            mod.vtx_attr_mask |= (1u << loc);
                        }
                        for (uint32_t k = 0; k < cnt; ++k) {
                            uint32_t r = b.emit(IrOp::LoadVertexAttr, IrType::F32, ValueUniformity::Divergent,
                                                {}, loc, 0.0f, false, k);
                            b.write_vgpr(static_cast<uint16_t>(d.dst + k), r, IrType::F32);
                        }
                    } else {
                        mod.uses_storage_buffer = true;
                        for (uint32_t k = 0; k < cnt; ++k) {
                            uint32_t r = b.emit(IrOp::LoadInputU32, IrType::U32, ValueUniformity::Divergent,
                                                {vaddr}, (d.offset / 4u) + k, 0.0f, false, 0u);
                            b.write_vgpr(static_cast<uint16_t>(d.dst + k), r, IrType::U32);
                        }
                    }
                }
                break;
            }

            case IsaClass::MIMG: {
                if (d.opcode == mimg_op::IMAGE_BVH_INTERSECT_RAY ||
                    d.opcode == mimg_op::IMAGE_BVH64_INTERSECT_RAY) {
                    mod.uses_bvh_rt = true;
                    if (d.opcode == mimg_op::IMAGE_BVH64_INTERSECT_RAY) {
                        mod.uses_bvh64_rt = true;
                    }
                    const uint16_t base_v = static_cast<uint16_t>(d.src[0] - reg_code::VGPR_BASE);
                    const uint32_t orig_x = b.read_vgpr_f32(base_v + 0u);
                    const uint32_t orig_y = b.read_vgpr_f32(base_v + 1u);
                    const uint32_t orig_z = b.read_vgpr_f32(base_v + 2u);
                    const uint32_t tmax   = b.read_vgpr_f32(base_v + 3u);
                    uint32_t hit = b.emit(IrOp::BvhIntersectRay, IrType::F32, ValueUniformity::Divergent,
                                          {orig_x, orig_y, orig_z, tmax}, base_v, 0.0f, false, d.dst);
                    mod.insts.back().is_bvh64 = (d.opcode == mimg_op::IMAGE_BVH64_INTERSECT_RAY);
                    b.write_vgpr(d.dst, hit, IrType::F32);
                    b.write_vgpr(static_cast<uint16_t>(d.dst + 1u), hit, IrType::F32);
                    b.write_vgpr(static_cast<uint16_t>(d.dst + 2u), hit, IrType::F32);
                    b.write_vgpr(static_cast<uint16_t>(d.dst + 3u),
                                 b.emit(IrOp::BitcastF32ToU32, IrType::U32, ValueUniformity::Divergent, {hit}),
                                 IrType::U32);
                } else if (d.opcode == mimg_op::IMAGE_STORE || d.opcode == mimg_op::IMAGE_STORE_MIP) {
                    mod.uses_image_storage = true;
                    const uint32_t coord = b.read_src_u32(d.src[0], false, 0u);
                    const uint32_t val = b.read_vgpr_f32(d.dst);
                    b.emit(IrOp::ImageStore, IrType::Void, ValueUniformity::Divergent, {coord, val});
                    b.mark_output_written();
                } else if (d.opcode == mimg_op::IMAGE_LOAD || d.opcode == mimg_op::IMAGE_LOAD_MIP ||
                           d.opcode == mimg_op::IMAGE_GET_RESINFO) {
                    mod.uses_image_storage = true;
                    const uint32_t coord = b.read_src_u32(d.src[0], false, 0u);
                    uint32_t r = b.emit(IrOp::ImageLoad, IrType::F32, ValueUniformity::Divergent, {coord});
                    b.write_vgpr(d.dst, r, IrType::F32);
                } else {
                    mod.uses_image_sample = true;
                    if (meta.stage == ShaderStage::Pixel && d.opcode == mimg_op::IMAGE_SAMPLE) {
                        mod.uses_wqm = true;
                    }
                    const uint16_t base_v = (d.src[0] >= reg_code::VGPR_BASE)
                        ? static_cast<uint16_t>(d.src[0] - reg_code::VGPR_BASE)
                        : 0u;
                    const uint32_t u = b.read_vgpr_f32(base_v);
                    const uint32_t v = b.read_vgpr_f32(static_cast<uint16_t>(base_v + 1u));
                    const uint32_t tex_slot =
                        (b.get_sgpr_kind(d.srsrc) == IrBuilder::SgprKind::TexSrvDesc)
                            ? (b.get_sgpr_slot(d.srsrc) & 7u)
                            : 0u;
                    mod.tex_slot_mask |= (1u << tex_slot);
                    uint32_t mask = (d.dmask != 0u) ? (d.dmask & 0xFu) : 1u;
                    uint32_t out_off = 0u;
                    for (uint32_t ch = 0u; ch < 4u; ++ch) {
                        if ((mask & (1u << ch)) != 0u) {
                            uint32_t r = b.emit(IrOp::ImageSample, IrType::F32, ValueUniformity::Divergent,
                                                {u, v}, tex_slot, 0.0f, false, ch);
                            b.write_vgpr(static_cast<uint16_t>(d.dst + out_off), r, IrType::F32);
                            ++out_off;
                        }
                    }
                }
                break;
            }

            case IsaClass::FLAT:
            case IsaClass::GLOBAL:
            case IsaClass::SCRATCH: {
                mod.uses_storage_buffer = true;
                const uint32_t addr = b.read_src_u32(d.src[0], false, 0u);
                if (d.opcode >= flat_op::STORE_DWORD && d.opcode <= flat_op::STORE_DWORDX4) {
                    const uint32_t val = b.read_src_u32(d.src[1], false, 0u);
                    b.emit(IrOp::StoreOutputU32, IrType::Void, ValueUniformity::Divergent,
                           {addr, val}, d.offset / 4u);
                    b.mark_output_written();
                } else if (d.opcode >= flat_op::ATOMIC_CMPSWAP) {
                    const uint32_t val = b.read_src_u32(d.src[1], false, 0u);
                    uint32_t r = b.emit(IrOp::AtomicBufAdd, IrType::U32, ValueUniformity::Divergent,
                                        {addr, val}, d.offset / 4u);
                    b.write_vgpr(d.dst, r, IrType::U32);
                    b.mark_output_written();
                } else {
                    uint32_t r = b.emit(IrOp::LoadInputU32, IrType::U32, ValueUniformity::Divergent,
                                        {addr}, d.offset / 4u, 0.0f, false, 0u);
                    b.write_vgpr(d.dst, r, IrType::U32);
                }
                break;
            }

            case IsaClass::EXP: {
                if (meta.stage == ShaderStage::Vertex && d.exp_target >= 32u && d.exp_target < 48u) {
                    mod.out_param_mask |= (1u << (d.exp_target - 32u));
                } else if (meta.stage == ShaderStage::Pixel && d.exp_target < 8u) {
                    mod.mrt_mask |= (1u << d.exp_target);
                }
                if (d.exp_compr) {
                    const uint32_t u01 = b.read_src_u32(d.src[0], false, 0u);
                    const uint32_t u23 = b.read_src_u32(d.src[1], false, 0u);
                    b.emit(IrOp::Export, IrType::Void, ValueUniformity::Divergent,
                           {u01, u23, u01, u23}, d.exp_target, 0.0f, false, (d.dmask & 0xFu) | 0x10u);
                } else {
                    const uint32_t c0 = b.read_src_f32(d.src[0], false, 0u);
                    const uint32_t c1 = b.read_src_f32(d.src[1], false, 0u);
                    const uint32_t c2 = b.read_src_f32(d.src[2], false, 0u);
                    const uint32_t c3 = b.read_src_f32(d.src[3], false, 0u);
                    b.emit(IrOp::Export, IrType::Void, ValueUniformity::Divergent,
                           {c0, c1, c2, c3}, d.exp_target, 0.0f, false, d.dmask & 0xFu);
                }
                b.mark_output_written();
                break;
            }

            default:
                break;
        }
    }

    /* Close any remaining open structured regions cleanly */
    while (!open_if_merges.empty()) {
        b.emit(IrOp::CfEndIf, IrType::Void, ValueUniformity::Uniform);
        open_if_merges.pop_back();
    }
    while (open_loops > 0) {
        b.emit(IrOp::CfLoopEnd, IrType::Void, ValueUniformity::Uniform);
        --open_loops;
    }

    /* Ensure compute shaders always write final result to output buffer if no explicit store occurred */
    if (!b.has_written_output() && meta.stage == ShaderStage::Compute) {
        mod.uses_storage_buffer = true;
        uint32_t v = b.read_vgpr_u32(b.last_written_vgpr());
        b.emit(IrOp::StoreOutputU32, IrType::Void, ValueUniformity::Divergent,
               {b.const_u32(0u), v}, 0u);
    }

    return mod;
}

/* ============================================================================
 * Stage 2: Forward + Backward computeKnownFPClass Static Dataflow Analysis
 * ============================================================================ */

void run_compute_known_fp_class(IrModule& mod, const LiftOptions& opts) noexcept {
    FpClassAnalysisStats& st = mod.fp_stats;
    st = FpClassAnalysisStats{};

    const size_t n = mod.insts.size();
    std::vector<uint16_t> ssa_flags(mod.next_ssa_id + 1u, fp_class::NONE);

    auto get_flags = [&](uint32_t id) -> uint16_t {
        return (id < ssa_flags.size()) ? ssa_flags[id] : fp_class::NONE;
    };

    const bool mode_exact = (opts.mode == SCARLETT_SHADER_MODE_EXACT);
    const bool mode_fast = (opts.mode == SCARLETT_SHADER_MODE_FAST_F32);
    const bool gpu_fma_exact = opts.gpu_profile.has(probe_bits::FMA_SINGLE_ROUND_EXACT);
    const bool gpu_nan_minmax = opts.gpu_profile.has(probe_bits::F32_NAN_MINMAX_SUPPRESS);
    const bool gpu_ftz_match = mod.meta.f32_denorm_ftz
                                   ? opts.gpu_profile.has(probe_bits::F32_DENORM_FTZ_NATIVE)
                                   : opts.gpu_profile.has(probe_bits::F32_DENORM_PRESERVE);
    const bool gpu_native_fp16 = opts.gpu_profile.has(probe_bits::F16_NATIVE_SUPPORTED) &&
                                 opts.gpu_profile.has(probe_bits::F16_NAN_DENORM_EXACT);

    /* Forward Dataflow Pass (fixpoint over SSA defs) */
    for (int iter = 0; iter < 3; ++iter) {
        bool changed = false;
        for (auto& inst : mod.insts) {
            if (inst.id == 0u) continue;
            uint16_t f = fp_class::NONE;
            switch (inst.op) {
                case IrOp::ConstF32:
                    f = classify_f32_constant(inst.imm_f32);
                    break;
                case IrOp::ConstU32:
                    f = fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL;
                    break;
                case IrOp::CvtI32ToF32:
                case IrOp::CvtU32ToF32:
                    /* Integer -> float32 conversions NEVER produce NaN, Inf, or subnormal! */
                    f = fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL;
                    if (inst.op == IrOp::CvtU32ToF32) f |= fp_class::NON_NEGATIVE;
                    break;
                case IrOp::FClamp01:
                    f = fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL |
                        fp_class::NON_NEGATIVE | fp_class::UNIT_RANGE | fp_class::EXACT_F16_RANGE;
                    break;
                case IrOp::FFract: {
                    const uint16_t a = get_flags(inst.args[0]);
                    if ((a & (fp_class::NO_NAN | fp_class::NO_INF)) == (fp_class::NO_NAN | fp_class::NO_INF)) {
                        f = fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL |
                            fp_class::NON_NEGATIVE | fp_class::UNIT_RANGE;
                    }
                    break;
                }
                case IrOp::FSin:
                case IrOp::FCos: {
                    const uint16_t a = get_flags(inst.args[0]);
                    if ((a & (fp_class::NO_NAN | fp_class::NO_INF)) == (fp_class::NO_NAN | fp_class::NO_INF)) {
                        f = fp_class::NO_NAN | fp_class::NO_INF | fp_class::EXACT_F16_RANGE;
                    }
                    break;
                }
                case IrOp::FAbs: {
                    const uint16_t a = get_flags(inst.args[0]);
                    f = a | fp_class::NON_NEGATIVE;
                    break;
                }
                case IrOp::FNeg: {
                    const uint16_t a = get_flags(inst.args[0]);
                    f = static_cast<uint16_t>(a & ~(fp_class::NON_NEGATIVE | fp_class::UNIT_RANGE));
                    break;
                }
                case IrOp::FSqrt: {
                    const uint16_t a = get_flags(inst.args[0]);
                    if ((a & (fp_class::NO_NAN | fp_class::NON_NEGATIVE)) ==
                        (fp_class::NO_NAN | fp_class::NON_NEGATIVE)) {
                        f = fp_class::NO_NAN | fp_class::NON_NEGATIVE |
                            (a & (fp_class::NO_INF | fp_class::NO_SUBNORMAL | fp_class::UNIT_RANGE));
                    }
                    break;
                }
                case IrOp::FFloor:
                case IrOp::FCeil:
                case IrOp::FTrunc:
                case IrOp::FRoundEven: {
                    const uint16_t a = get_flags(inst.args[0]);
                    f = static_cast<uint16_t>(a | fp_class::NO_SUBNORMAL);
                    break;
                }
                case IrOp::FAdd:
                case IrOp::FSub:
                case IrOp::FMul: {
                    const uint16_t a = get_flags(inst.args[0]);
                    const uint16_t c = get_flags(inst.args[1]);
                    const uint16_t both = a & c;
                    if ((both & (fp_class::NO_NAN | fp_class::NO_INF)) == (fp_class::NO_NAN | fp_class::NO_INF)) {
                        f |= fp_class::NO_NAN;
                        if (inst.op == IrOp::FMul && ((both & fp_class::UNIT_RANGE) != 0u)) {
                            f |= fp_class::NO_INF | fp_class::NON_NEGATIVE | fp_class::UNIT_RANGE;
                        }
                        if (inst.op == IrOp::FAdd && ((both & fp_class::NON_NEGATIVE) != 0u)) {
                            f |= fp_class::NON_NEGATIVE;
                        }
                        if (gpu_ftz_match || ((both & fp_class::NO_SUBNORMAL) != 0u && inst.op == IrOp::FAdd)) {
                            f |= fp_class::NO_SUBNORMAL;
                        }
                    }
                    break;
                }
                case IrOp::FFma: {
                    const uint16_t a = get_flags(inst.args[0]);
                    const uint16_t c = get_flags(inst.args[1]);
                    const uint16_t e = get_flags(inst.args[2]);
                    const uint16_t all = a & c & e;
                    if ((all & (fp_class::NO_NAN | fp_class::NO_INF)) == (fp_class::NO_NAN | fp_class::NO_INF)) {
                        f |= fp_class::NO_NAN;
                        if ((all & fp_class::NON_NEGATIVE) != 0u) f |= fp_class::NON_NEGATIVE;
                        if ((all & fp_class::UNIT_RANGE) != 0u) f |= fp_class::NO_INF;
                    }
                    if (inst.clamp_result) {
                        f |= fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL |
                             fp_class::NON_NEGATIVE | fp_class::UNIT_RANGE;
                    }
                    break;
                }
                case IrOp::FMin:
                case IrOp::FMax: {
                    const uint16_t a = get_flags(inst.args[0]);
                    const uint16_t c = get_flags(inst.args[1]);
                    f = a & c;
                    if ((a & fp_class::NO_NAN) || (c & fp_class::NO_NAN)) {
                        /* RDNA2 v_min_f32/v_max_f32 suppresses a single NaN */
                        f |= fp_class::NO_NAN;
                    }
                    break;
                }
                case IrOp::FMin3:
                case IrOp::FMax3:
                case IrOp::FMed3: {
                    const uint16_t a = get_flags(inst.args[0]);
                    const uint16_t c = get_flags(inst.args[1]);
                    const uint16_t e = get_flags(inst.args[2]);
                    f = a & c & e;
                    break;
                }
                case IrOp::SelectF32: {
                    f = get_flags(inst.args[1]) & get_flags(inst.args[2]);
                    break;
                }
                case IrOp::InterpAttr:
                case IrOp::ImageSample:
                case IrOp::ImageLoad:
                case IrOp::BvhIntersectRay:
                    /* Hardware interpolator / sampler / RT hit t: finite in non-NaN mode */
                    f = fp_class::NO_SUBNORMAL;
                    break;
                default:
                    break;
            }
            if (inst.clamp_result) {
                f |= fp_class::NO_NAN | fp_class::NO_INF | fp_class::NO_SUBNORMAL |
                     fp_class::NON_NEGATIVE | fp_class::UNIT_RANGE;
            }
            if (f != ssa_flags[inst.id]) {
                ssa_flags[inst.id] = f;
                inst.fp_flags = f;
                changed = true;
            }
        }
        if (!changed) break;
    }

    /* Backward Demand Dataflow Pass: propagate consumer demands from sinks upward */
    std::vector<uint16_t> demand(mod.next_ssa_id + 1u, fp_class::NONE);
    for (size_t ri = n; ri-- > 0;) {
        auto& inst = mod.insts[ri];
        uint16_t d = (inst.id < demand.size()) ? demand[inst.id] : fp_class::NONE;

        if (inst.op == IrOp::FClamp01 || inst.clamp_result ||
            inst.op == IrOp::CvtF32ToI32 || inst.op == IrOp::CvtF32ToU32) {
            d |= fp_class::DEMAND_SATURATE;
            for (uint8_t k = 0; k < inst.arg_count; ++k) {
                const uint32_t aid = inst.args[k];
                if (aid > 0u && aid < demand.size()) {
                    if ((demand[aid] & fp_class::DEMAND_SATURATE) == 0u) {
                        demand[aid] |= fp_class::DEMAND_SATURATE;
                        ++st.backward_demands_propagated;
                    }
                }
            }
        } else if (inst.op == IrOp::Export) {
            for (uint8_t k = 0; k < inst.arg_count; ++k) {
                const uint32_t aid = inst.args[k];
                if (aid > 0u && aid < demand.size()) {
                    if ((demand[aid] & fp_class::DEMAND_NO_NAN) == 0u) {
                        demand[aid] |= fp_class::DEMAND_NO_NAN;
                        ++st.backward_demands_propagated;
                    }
                }
            }
        } else if (d != fp_class::NONE) {
            /* Propagate saturation / no-NaN demand through monotone FP ops */
            if (inst.op == IrOp::FAbs || inst.op == IrOp::FNeg || inst.op == IrOp::FMin || inst.op == IrOp::FMax) {
                for (uint8_t k = 0; k < inst.arg_count; ++k) {
                    const uint32_t aid = inst.args[k];
                    if (aid > 0u && aid < demand.size() && (demand[aid] | d) != demand[aid]) {
                        demand[aid] |= d;
                        ++st.backward_demands_propagated;
                    }
                }
            }
        }
        if (inst.id > 0u) {
            inst.fp_flags |= d;
        }
    }

    /* Final Elision Decision per Instruction */
    for (auto& inst : mod.insts) {
        if (!is_fp_opcode(inst.op)) continue;
        ++st.total_fp_ops;

        if ((inst.fp_flags & fp_class::NO_NAN) != 0u)       ++st.proven_no_nan;
        if ((inst.fp_flags & fp_class::NO_INF) != 0u)       ++st.proven_no_inf;
        if ((inst.fp_flags & fp_class::NO_SUBNORMAL) != 0u) ++st.proven_no_subnormal;

        if (mode_exact) {
            /* Exact mode keeps full conservative IEEE/RDNA2 software guards */
            inst.elide_nan_tree = false;
            inst.elide_denorm_guard = false;
            inst.elide_fma_emul = false;
            inst.use_native_f16 = false;
            continue;
        }

        /* Adaptive or FastF32 mode */
        bool args_no_nan = (inst.arg_count > 0u);
        bool args_no_subnorm = (inst.arg_count > 0u);
        for (uint8_t k = 0; k < inst.arg_count; ++k) {
            const uint16_t af = get_flags(inst.args[k]);
            if ((af & fp_class::NO_NAN) == 0u) args_no_nan = false;
            if ((af & fp_class::NO_SUBNORMAL) == 0u) args_no_subnorm = false;
        }

        if (inst.op == IrOp::FMin || inst.op == IrOp::FMax ||
            inst.op == IrOp::FMin3 || inst.op == IrOp::FMax3 || inst.op == IrOp::FMed3) {
            /* Safe to elide explicit OpIsNan/OpSelect tree if operands are proven non-NaN OR GPU NMin/NMax matches RDNA2! */
            if (mode_fast || args_no_nan || gpu_nan_minmax) {
                inst.elide_nan_tree = true;
                ++st.nan_trees_elided;
            }
        } else if (inst.op == IrOp::FAdd || inst.op == IrOp::FSub || inst.op == IrOp::FMul ||
                   inst.op == IrOp::FFma || inst.op == IrOp::FSqrt || inst.op == IrOp::FRcp || inst.op == IrOp::FRsq) {
            if (mode_fast || args_no_nan || ((inst.fp_flags & fp_class::DEMAND_NO_NAN) != 0u)) {
                inst.elide_nan_tree = true;
                ++st.nan_trees_elided;
            }
        }

        /* Denormal FTZ guard elision */
        if (mode_fast || gpu_ftz_match || args_no_subnorm ||
            ((inst.fp_flags & fp_class::NO_SUBNORMAL) != 0u)) {
            inst.elide_denorm_guard = true;
            ++st.denorm_guards_elided;
        }

        /* FMA emulation elision when GPU microprobe verified single-rounding FMA */
        if (inst.op == IrOp::FFma && (mode_fast || gpu_fma_exact)) {
            inst.elide_fma_emul = true;
            ++st.fma_emulations_elided;
        }

        /* Native FP16 direct emission when GPU microprobe verified native FP16 exactness */
        if (inst.op == IrOp::PkF16Add || inst.op == IrOp::PkF16Mul ||
            inst.op == IrOp::PkF16Fma || inst.op == IrOp::PkF16Min ||
            inst.op == IrOp::PkF16Max || inst.op == IrOp::Dot2F32F16 ||
            inst.op == IrOp::CvtF32ToF16 || inst.op == IrOp::CvtF16ToF32) {
            if (gpu_native_fp16 || (mode_fast && opts.gpu_profile.has(probe_bits::F16_NATIVE_SUPPORTED))) {
                inst.use_native_f16 = true;
                ++st.f16_soft_ops_elided;
            }
        }
    }
}

/* ============================================================================
 * Stage 3: Adaptive Wave64 Cross-Half-Wave Dependency Analysis
 * ============================================================================ */

void run_wave64_dependency_analysis(IrModule& mod, const LiftOptions& opts) noexcept {
    Wave64AnalysisStats& ws = mod.wave_stats;
    ws.shader_is_wave64 = (mod.meta.wave_size == 64u);

    /* Scan IR instructions for cross-half-wave dependencies (lanes 0..31 <-> 32..63) */
    for (const auto& inst : mod.insts) {
        switch (inst.op) {
            case IrOp::ReadLane:
                if (inst.imm_u32 != 0u) ws.uses_v_readlane = true;
                break;
            case IrOp::DsBpermute:
                ws.uses_ds_bpermute = true;
                break;
            case IrOp::DsSwizzle:
                if (inst.imm2_u32 != 0u) ws.uses_ds_swizzle_cross32 = true;
                break;
            case IrOp::WaveBallotHi:
                ws.uses_ballot_hi32 = true;
                break;
            case IrOp::WaveBallotBitCount64:
                ws.uses_bitcnt_b64 = true;
                ws.uses_ballot_hi32 = true;
                break;
            case IrOp::WaveMbcntHi:
                ws.uses_mbcnt_hi = true;
                break;
            case IrOp::Permlane16:
                if (inst.imm_u32 != 0u) ws.uses_permlane = true;
                break;
            default:
                break;
        }
    }

    ws.has_cross_half_wave_dep =
        ws.uses_v_readlane ||
        ws.uses_ds_bpermute ||
        ws.uses_ds_swizzle_cross32 ||
        ws.uses_ballot_hi32 ||
        ws.uses_bitcnt_b64 ||
        ws.uses_mbcnt_hi ||
        ws.uses_permlane;

    const bool gpu_native_wave64 =
        opts.gpu_profile.has(probe_bits::SUBGROUP_SIZE_64_NATIVE) ||
        (opts.gpu_profile.has(probe_bits::EXT_SUBGROUP_SIZE_CONTROL) &&
         opts.gpu_profile.max_subgroup_size >= 64u);

    /* In EXACT mode, conservatively force paired-lane Wave64 LDS scaffolding on Wave64 shaders */
    if (opts.mode == SCARLETT_SHADER_MODE_EXACT && ws.shader_is_wave64) {
        ws.chosen_strategy = WaveStrategy::PairedWave64LdsEmulation;
        return;
    }

    /* Adaptive / FastF32 mode: 3-tier Wave64 decision */
    if (!ws.shader_is_wave64) {
        ws.chosen_strategy = WaveStrategy::NarrowedWave32;
    } else if (gpu_native_wave64) {
        /* AMD RADV / Intel with native subgroupSize=64 -> zero overhead native Wave64 */
        ws.chosen_strategy = WaveStrategy::NativeWave64;
    } else if (!ws.has_cross_half_wave_dep) {
        /* NVIDIA Warp32 with NO cross-half-wave deps -> safely narrow to native Wave32! */
        ws.chosen_strategy = WaveStrategy::NarrowedWave32;
    } else {
        /* NVIDIA Warp32 with proven cross-half-wave deps -> paired-lane LDS Wave64 emulation */
        ws.chosen_strategy = WaveStrategy::PairedWave64LdsEmulation;
    }
}

/* ============================================================================
 * Stage 4: BVH4 Ray Tracing Strategy Selection
 * ============================================================================ */

void run_rt_bvh4_strategy_selection(IrModule& mod, const LiftOptions& opts) noexcept {
    if (!mod.uses_bvh_rt && !mod.uses_bvh64_rt) {
        mod.rt_strategy = RtStrategy::None;
        return;
    }
    if (opts.force_bda_bvh4) {
        mod.rt_strategy = RtStrategy::BdaBvh4Fallback;
        return;
    }
    if (opts.prefer_ray_query && opts.gpu_profile.has(probe_bits::EXT_RAY_QUERY)) {
        mod.rt_strategy = RtStrategy::RayQueryKHR;
    } else {
        mod.rt_strategy = RtStrategy::BdaBvh4Fallback;
    }
}

} // namespace scarlett::rdna2
