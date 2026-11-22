/*
 * Proton-Scarlett — Full AMD RDNA2 (GFX10.3) Binary ISA Decoder
 *
 * Decodes all 12+ hardware encoding classes of RDNA2 / GFX10.3:
 *   SOP1, SOP2, SOPC, SOPK, SOPP, SMEM,
 *   VOP1, VOP2, VOP3A, VOP3B, VOP3P, VOPC,
 *   VINTRP, DS, MUBUF, MTBUF, MIMG, FLAT/GLOBAL/SCRATCH, EXP.
 */

#include "rdna2_isa.hpp"

namespace scarlett::rdna2 {

namespace {

bool is_vop2_mandatory_literal(uint16_t op) noexcept {
    return op == vop2_op::V_FMAAK_F32 ||
           op == vop2_op::V_FMAMK_F32 ||
           op == vop2_op::V_FMAAK_F16 ||
           op == vop2_op::V_FMAMK_F16;
}

} // namespace

bool decode_gfx103_stream(const uint32_t* words,
                          size_t word_count,
                          std::vector<DecodedInst>& out_insts,
                          uint32_t& out_error_pc) noexcept {
    out_insts.clear();
    out_error_pc = 0u;
    if (words == nullptr || word_count == 0u) {
        return false;
    }

    out_insts.reserve(word_count);
    size_t i = 0;

    while (i < word_count) {
        const uint32_t w0 = words[i];
        DecodedInst inst{};
        inst.pc_words = static_cast<uint32_t>(i);

        /* ================================================================
         * Bit 31 == 0 -> VOP1, VOPC, or VOP2 (32-bit + optional 32-bit lit)
         * ================================================================ */
        if ((w0 & 0x80000000u) == 0u) {
            const uint32_t enc7 = (w0 >> 25u) & 0x7Fu;
            if (enc7 == 0x3Fu) {
                /* VOP1 */
                inst.cls = IsaClass::VOP1;
                inst.dst = static_cast<uint16_t>((w0 >> 17u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.opcode = static_cast<uint16_t>((w0 >> 9u) & 0xFFu);
                inst.src[0] = static_cast<uint16_t>(w0 & 0x1FFu);
                inst.src_count = 1u;
                inst.word_count = 1u;
                if (inst.src[0] == reg_code::LITERAL_32) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            } else if (enc7 == 0x3Eu) {
                /* VOPC */
                inst.cls = IsaClass::VOPC;
                inst.opcode = static_cast<uint16_t>((w0 >> 17u) & 0xFFu);
                inst.dst = reg_code::VCC_LO;
                inst.dst_is_vgpr = false;
                inst.src[0] = static_cast<uint16_t>(w0 & 0x1FFu);
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w0 >> 9u) & 0xFFu));
                inst.src_count = 2u;
                inst.word_count = 1u;
                if (inst.src[0] == reg_code::LITERAL_32) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            } else {
                /* VOP2 */
                inst.cls = IsaClass::VOP2;
                inst.opcode = static_cast<uint16_t>(enc7);
                inst.dst = static_cast<uint16_t>((w0 >> 17u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.src[0] = static_cast<uint16_t>(w0 & 0x1FFu);
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w0 >> 9u) & 0xFFu));
                inst.src_count = 2u;
                inst.word_count = 1u;
                if (inst.src[0] == reg_code::LITERAL_32 || is_vop2_mandatory_literal(inst.opcode)) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            }
            out_insts.push_back(inst);
            i += inst.word_count;
            continue;
        }

        /* ================================================================
         * Bits [31:30] == 10 -> SOP1, SOPC, SOPP, SOPK, SOP2
         * ================================================================ */
        if (((w0 >> 30u) & 0x3u) == 0x2u) {
            const uint32_t enc9 = (w0 >> 23u) & 0x1FFu;
            if (enc9 == 0x17Du) {
                /* SOP1 */
                inst.cls = IsaClass::SOP1;
                inst.dst = static_cast<uint16_t>((w0 >> 16u) & 0x7Fu);
                inst.dst_is_vgpr = false;
                inst.opcode = static_cast<uint16_t>((w0 >> 8u) & 0xFFu);
                inst.src[0] = static_cast<uint16_t>(w0 & 0xFFu);
                inst.src_count = 1u;
                inst.word_count = 1u;
                if (inst.src[0] == reg_code::LITERAL_32) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            } else if (enc9 == 0x17Eu) {
                /* SOPC */
                inst.cls = IsaClass::SOPC;
                inst.opcode = static_cast<uint16_t>((w0 >> 16u) & 0x7Fu);
                inst.dst = reg_code::SCC;
                inst.dst_is_vgpr = false;
                inst.src[0] = static_cast<uint16_t>(w0 & 0xFFu);
                inst.src[1] = static_cast<uint16_t>((w0 >> 8u) & 0xFFu);
                inst.src_count = 2u;
                inst.word_count = 1u;
                if (inst.src[0] == reg_code::LITERAL_32 || inst.src[1] == reg_code::LITERAL_32) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            } else if (enc9 == 0x17Fu) {
                /* SOPP */
                inst.cls = IsaClass::SOPP;
                inst.opcode = static_cast<uint16_t>((w0 >> 16u) & 0x7Fu);
                inst.simm16 = static_cast<int16_t>(w0 & 0xFFFFu);
                inst.src_count = 0u;
                inst.word_count = 1u;
            } else if (((w0 >> 28u) & 0xFu) == 0xBu) {
                /* SOPK */
                inst.cls = IsaClass::SOPK;
                inst.opcode = static_cast<uint16_t>((w0 >> 23u) & 0x1Fu);
                inst.dst = static_cast<uint16_t>((w0 >> 16u) & 0x7Fu);
                inst.dst_is_vgpr = false;
                inst.simm16 = static_cast<int16_t>(w0 & 0xFFFFu);
                inst.src_count = 0u;
                inst.word_count = 1u;
                if (inst.opcode == sopk_op::S_SETREG_IMM32_B32) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            } else {
                /* SOP2 */
                inst.cls = IsaClass::SOP2;
                inst.opcode = static_cast<uint16_t>((w0 >> 23u) & 0x7Fu);
                inst.dst = static_cast<uint16_t>((w0 >> 16u) & 0x7Fu);
                inst.dst_is_vgpr = false;
                inst.src[0] = static_cast<uint16_t>(w0 & 0xFFu);
                inst.src[1] = static_cast<uint16_t>((w0 >> 8u) & 0xFFu);
                inst.src_count = 2u;
                inst.word_count = 1u;
                if (inst.src[0] == reg_code::LITERAL_32 || inst.src[1] == reg_code::LITERAL_32) {
                    if (i + 1u >= word_count) {
                        out_error_pc = static_cast<uint32_t>(i);
                        return false;
                    }
                    inst.has_literal = true;
                    inst.literal = words[i + 1u];
                    inst.word_count = 2u;
                }
            }
            out_insts.push_back(inst);
            i += inst.word_count;
            continue;
        }

        /* ================================================================
         * Bits [31:30] == 11 -> 6-bit primary encoding (VINTRP + 64-bit ops)
         * ================================================================ */
        const uint32_t enc6 = (w0 >> 26u) & 0x3Fu;
        if (enc6 == 0x32u) {
            /* VINTRP (32-bit) */
            inst.cls = IsaClass::VINTRP;
            inst.dst = static_cast<uint16_t>((w0 >> 18u) & 0xFFu);
            inst.dst_is_vgpr = true;
            inst.opcode = static_cast<uint16_t>((w0 >> 16u) & 0x3u);
            inst.attr = static_cast<uint8_t>((w0 >> 10u) & 0x3Fu);
            inst.attr_chan = static_cast<uint8_t>((w0 >> 8u) & 0x3u);
            inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w0 & 0xFFu));
            inst.src_count = 1u;
            inst.word_count = 1u;
            out_insts.push_back(inst);
            i += 1u;
            continue;
        }

        /* All remaining 0b11xxxx encodings are 64-bit (2 words) */
        if (i + 1u >= word_count) {
            out_error_pc = static_cast<uint32_t>(i);
            return false;
        }
        const uint32_t w1 = words[i + 1u];
        inst.word_count = 2u;

        switch (enc6) {
            case 0x33u: {
                /* VOP3P (packed f16 / i16 / dot) */
                inst.cls = IsaClass::VOP3P;
                inst.opcode = static_cast<uint16_t>((w0 >> 16u) & 0x7Fu);
                inst.clamp = ((w0 >> 15u) & 1u) != 0u;
                inst.op_sel = static_cast<uint8_t>((w0 >> 11u) & 0x7u);
                inst.op_sel_hi = static_cast<uint8_t>((((w0 >> 14u) & 1u) << 2u) | ((w1 >> 27u) & 0x3u));
                inst.neg_mask = static_cast<uint8_t>((w1 >> 29u) & 0x7u);
                inst.abs_mask = static_cast<uint8_t>((w0 >> 8u) & 0x7u); /* neg_hi stored in abs_mask */
                inst.dst = static_cast<uint16_t>(w0 & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.src[0] = static_cast<uint16_t>(w1 & 0x1FFu);
                inst.src[1] = static_cast<uint16_t>((w1 >> 9u) & 0x1FFu);
                inst.src[2] = static_cast<uint16_t>((w1 >> 18u) & 0x1FFu);
                inst.src_count = 3u;
                break;
            }
            case 0x35u: {
                /* VOP3A / VOP3B */
                inst.opcode = static_cast<uint16_t>((w0 >> 16u) & 0x3FFu);
                inst.clamp = ((w0 >> 15u) & 1u) != 0u;
                inst.dst = static_cast<uint16_t>(w0 & 0xFFu);
                inst.dst_is_vgpr = (inst.opcode != vop3_op::V_READLANE_B32 && inst.opcode >= 0x100u);
                if (vop3_op::is_vop3b_opcode(inst.opcode)) {
                    inst.cls = IsaClass::VOP3B;
                    inst.sdst = static_cast<uint16_t>((w0 >> 8u) & 0x7Fu);
                    inst.has_sdst = true;
                } else {
                    inst.cls = IsaClass::VOP3A;
                    inst.op_sel = static_cast<uint8_t>((w0 >> 11u) & 0xFu);
                    inst.abs_mask = static_cast<uint8_t>((w0 >> 8u) & 0x7u);
                }
                inst.neg_mask = static_cast<uint8_t>((w1 >> 29u) & 0x7u);
                inst.omod = static_cast<uint8_t>((w1 >> 27u) & 0x3u);
                inst.src[0] = static_cast<uint16_t>(w1 & 0x1FFu);
                inst.src[1] = static_cast<uint16_t>((w1 >> 9u) & 0x1FFu);
                inst.src[2] = static_cast<uint16_t>((w1 >> 18u) & 0x1FFu);
                inst.src_count = 3u;
                break;
            }
            case 0x36u: {
                /* DS (LDS + ds_bpermute_b32 / ds_swizzle_b32) */
                inst.cls = IsaClass::DS;
                inst.opcode = static_cast<uint16_t>((w0 >> 18u) & 0xFFu);
                inst.offset0 = static_cast<uint8_t>(w0 & 0xFFu);
                inst.offset1 = static_cast<uint8_t>((w0 >> 8u) & 0xFFu);
                inst.offset = static_cast<uint32_t>(inst.offset0) |
                              (static_cast<uint32_t>(inst.offset1) << 8u);
                inst.dst = static_cast<uint16_t>((w1 >> 24u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w1 & 0xFFu));          /* addr */
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 8u) & 0xFFu));  /* data0 */
                inst.src[2] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 16u) & 0xFFu)); /* data1 */
                inst.src_count = 3u;
                break;
            }
            case 0x37u: {
                /* FLAT / SCRATCH / GLOBAL */
                inst.raw_seg = static_cast<uint8_t>((w0 >> 14u) & 0x3u);
                if (inst.raw_seg == 1u) {
                    inst.cls = IsaClass::SCRATCH;
                } else if (inst.raw_seg == 2u) {
                    inst.cls = IsaClass::GLOBAL;
                } else {
                    inst.cls = IsaClass::FLAT;
                }
                inst.opcode = static_cast<uint16_t>((w0 >> 18u) & 0x7Fu);
                inst.glc = ((w0 >> 16u) & 1u) != 0u;
                inst.slc = ((w0 >> 17u) & 1u) != 0u;
                inst.dlc = ((w0 >> 13u) & 1u) != 0u;
                inst.offset = w0 & 0xFFFu;
                inst.dst = static_cast<uint16_t>((w1 >> 24u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.srsrc = static_cast<uint8_t>((w1 >> 16u) & 0x7Fu); /* saddr */
                inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w1 & 0xFFu));         /* vaddr */
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 8u) & 0xFFu)); /* vdata */
                inst.src_count = 2u;
                break;
            }
            case 0x38u: {
                /* MUBUF */
                inst.cls = IsaClass::MUBUF;
                inst.opcode = static_cast<uint16_t>((w0 >> 18u) & 0x7Fu);
                inst.dlc = ((w0 >> 15u) & 1u) != 0u;
                inst.glc = ((w0 >> 14u) & 1u) != 0u;
                inst.slc = ((w1 >> 22u) & 1u) != 0u;
                inst.offset = w0 & 0xFFFu;
                inst.srsrc = static_cast<uint8_t>(((w1 >> 16u) & 0x1Fu) * 4u);
                inst.dst = static_cast<uint16_t>((w1 >> 8u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w1 & 0xFFu)); /* vaddr */
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 8u) & 0xFFu)); /* vdata */
                inst.src[2] = static_cast<uint16_t>((w1 >> 24u) & 0xFFu); /* soffset */
                inst.src_count = 3u;
                break;
            }
            case 0x3Au: {
                /* MTBUF */
                inst.cls = IsaClass::MTBUF;
                inst.tbuf_format = static_cast<uint8_t>((w0 >> 19u) & 0x7Fu);
                inst.opcode = static_cast<uint16_t>((w0 >> 15u) & 0xFu);
                inst.glc = ((w0 >> 14u) & 1u) != 0u;
                inst.offset = w0 & 0xFFFu;
                inst.srsrc = static_cast<uint8_t>(((w1 >> 16u) & 0x1Fu) * 4u);
                inst.dst = static_cast<uint16_t>((w1 >> 8u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w1 & 0xFFu));
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 8u) & 0xFFu));
                inst.src[2] = static_cast<uint16_t>((w1 >> 24u) & 0xFFu);
                inst.src_count = 3u;
                break;
            }
            case 0x3Cu: {
                /* MIMG (including image_bvh_intersect_ray & image_bvh64_intersect_ray) */
                inst.cls = IsaClass::MIMG;
                inst.opcode = static_cast<uint16_t>((w0 >> 18u) & 0x7Fu);
                inst.a16 = ((w0 >> 16u) & 1u) != 0u;
                inst.glc = ((w0 >> 13u) & 1u) != 0u;
                inst.dmask = static_cast<uint8_t>((w0 >> 8u) & 0xFu);
                inst.dlc = ((w0 >> 7u) & 1u) != 0u;
                inst.dim = static_cast<uint8_t>((w0 >> 3u) & 0x7u);
                inst.d16 = ((w1 >> 31u) & 1u) != 0u;
                inst.ssamp = static_cast<uint8_t>(((w1 >> 21u) & 0x1Fu) * 4u);
                inst.srsrc = static_cast<uint8_t>(((w1 >> 16u) & 0x1Fu) * 4u);
                inst.dst = static_cast<uint16_t>((w1 >> 8u) & 0xFFu);
                inst.dst_is_vgpr = true;
                inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w1 & 0xFFu));
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 8u) & 0xFFu));
                inst.src_count = 2u;
                break;
            }
            case 0x3Du: {
                /* SMEM */
                inst.cls = IsaClass::SMEM;
                inst.opcode = static_cast<uint16_t>((w0 >> 18u) & 0xFFu);
                inst.glc = ((w0 >> 16u) & 1u) != 0u;
                inst.dlc = ((w0 >> 14u) & 1u) != 0u;
                inst.dst = static_cast<uint16_t>((w0 >> 6u) & 0x7Fu);
                inst.dst_is_vgpr = false;
                inst.srsrc = static_cast<uint8_t>((w0 & 0x3Fu) * 2u); /* sbase */
                inst.src[0] = inst.srsrc;
                inst.src[1] = static_cast<uint16_t>((w1 >> 25u) & 0x7Fu); /* soffset */
                inst.src_count = 2u;
                inst.offset = w1 & 0x1FFFFFu;
                break;
            }
            case 0x3Eu: {
                /* EXP */
                inst.cls = IsaClass::EXP;
                inst.opcode = 0u;
                inst.exp_done = ((w0 >> 11u) & 1u) != 0u;
                inst.exp_compr = ((w0 >> 10u) & 1u) != 0u;
                inst.exp_target = static_cast<uint8_t>((w0 >> 4u) & 0x3Fu);
                inst.dmask = static_cast<uint8_t>(w0 & 0xFu);
                inst.src[0] = static_cast<uint16_t>(reg_code::VGPR_BASE + (w1 & 0xFFu));
                inst.src[1] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 8u) & 0xFFu));
                inst.src[2] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 16u) & 0xFFu));
                inst.src[3] = static_cast<uint16_t>(reg_code::VGPR_BASE + ((w1 >> 24u) & 0xFFu));
                inst.src_count = 4u;
                break;
            }
            default:
                out_error_pc = static_cast<uint32_t>(i);
                return false;
        }

        out_insts.push_back(inst);
        i += inst.word_count;
    }

    return !out_insts.empty();
}

} // namespace scarlett::rdna2
