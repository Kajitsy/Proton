/*
 * Proton-Scarlett — Clean-Room AMD RDNA2 (GFX10.3) ISA Definitions & Assembler
 *
 * Complete binary encoding definitions for all 12+ GFX10.3 instruction classes:
 *   SOP1, SOP2, SOPC, SOPK, SOPP, SMEM,
 *   VOP1, VOP2, VOP3A, VOP3B, VOP3P (packed f16/i16/dot), VOPC,
 *   VINTRP, DS (LDS + ds_bpermute_b32 / ds_swizzle_b32),
 *   MUBUF, MTBUF, MIMG (including image_bvh_intersect_ray / image_bvh64_intersect_ray),
 *   FLAT / GLOBAL / SCRATCH, EXP.
 */

#ifndef SCARLETT_RDNA2_ISA_HPP
#define SCARLETT_RDNA2_ISA_HPP

#include <cstdint>
#include <string_view>
#include <vector>

namespace scarlett::rdna2 {

/* ============================================================================
 * GFX10.3 Instruction Classes (All 12+ Hardware Encodings)
 * ============================================================================ */

enum class IsaClass : uint8_t {
    Unknown = 0,
    SOP1,
    SOP2,
    SOPC,
    SOPK,
    SOPP,
    SMEM,
    VOP1,
    VOP2,
    VOP3A,
    VOP3B,
    VOP3P,
    VOPC,
    VINTRP,
    DS,
    MUBUF,
    MTBUF,
    MIMG,
    FLAT,
    GLOBAL,
    SCRATCH,
    EXP,
    Count
};

inline std::string_view isa_class_name(IsaClass cls) noexcept {
    switch (cls) {
        case IsaClass::SOP1:    return "SOP1";
        case IsaClass::SOP2:    return "SOP2";
        case IsaClass::SOPC:    return "SOPC";
        case IsaClass::SOPK:    return "SOPK";
        case IsaClass::SOPP:    return "SOPP";
        case IsaClass::SMEM:    return "SMEM";
        case IsaClass::VOP1:    return "VOP1";
        case IsaClass::VOP2:    return "VOP2";
        case IsaClass::VOP3A:   return "VOP3A";
        case IsaClass::VOP3B:   return "VOP3B";
        case IsaClass::VOP3P:   return "VOP3P";
        case IsaClass::VOPC:    return "VOPC";
        case IsaClass::VINTRP:  return "VINTRP";
        case IsaClass::DS:      return "DS";
        case IsaClass::MUBUF:   return "MUBUF";
        case IsaClass::MTBUF:   return "MTBUF";
        case IsaClass::MIMG:    return "MIMG";
        case IsaClass::FLAT:    return "FLAT";
        case IsaClass::GLOBAL:  return "GLOBAL";
        case IsaClass::SCRATCH: return "SCRATCH";
        case IsaClass::EXP:     return "EXP";
        default:                return "UNKNOWN";
    }
}

/* ============================================================================
 * Canonical GFX10.3 Opcodes (Clean-Room Spec Constants)
 * ============================================================================ */

namespace sop2_op {
constexpr uint16_t S_ADD_U32          = 0x00;
constexpr uint16_t S_SUB_U32          = 0x01;
constexpr uint16_t S_ADD_I32          = 0x02;
constexpr uint16_t S_SUB_I32          = 0x03;
constexpr uint16_t S_ADDC_U32         = 0x04;
constexpr uint16_t S_SUBB_U32         = 0x05;
constexpr uint16_t S_MIN_I32          = 0x06;
constexpr uint16_t S_MIN_U32          = 0x07;
constexpr uint16_t S_MAX_I32          = 0x08;
constexpr uint16_t S_MAX_U32          = 0x09;
constexpr uint16_t S_CSELECT_B32      = 0x0A;
constexpr uint16_t S_CSELECT_B64      = 0x0B;
constexpr uint16_t S_AND_B32          = 0x0E;
constexpr uint16_t S_AND_B64          = 0x0F;
constexpr uint16_t S_OR_B32           = 0x10;
constexpr uint16_t S_OR_B64           = 0x11;
constexpr uint16_t S_XOR_B32          = 0x12;
constexpr uint16_t S_XOR_B64          = 0x13;
constexpr uint16_t S_ANDN2_B32        = 0x14;
constexpr uint16_t S_ANDN2_B64        = 0x15;
constexpr uint16_t S_ORN2_B32         = 0x16;
constexpr uint16_t S_ORN2_B64         = 0x17;
constexpr uint16_t S_NAND_B32         = 0x18;
constexpr uint16_t S_NAND_B64         = 0x19;
constexpr uint16_t S_NOR_B32          = 0x1A;
constexpr uint16_t S_NOR_B64          = 0x1B;
constexpr uint16_t S_XNOR_B32         = 0x1C;
constexpr uint16_t S_XNOR_B64         = 0x1D;
constexpr uint16_t S_LSHL_B32         = 0x1E;
constexpr uint16_t S_LSHL_B64         = 0x1F;
constexpr uint16_t S_LSHR_B32         = 0x20;
constexpr uint16_t S_LSHR_B64         = 0x21;
constexpr uint16_t S_ASHR_I32         = 0x22;
constexpr uint16_t S_ASHR_I64         = 0x23;
constexpr uint16_t S_BFM_B32          = 0x24;
constexpr uint16_t S_MUL_I32          = 0x26;
constexpr uint16_t S_BFE_U32          = 0x27;
constexpr uint16_t S_BFE_I32          = 0x28;
constexpr uint16_t S_MUL_HI_U32       = 0x2C;
constexpr uint16_t S_MUL_HI_I32       = 0x2D;
constexpr uint16_t S_LSHL1_ADD_U32    = 0x2E;
constexpr uint16_t S_LSHL2_ADD_U32    = 0x2F;
constexpr uint16_t S_LSHL3_ADD_U32    = 0x30;
constexpr uint16_t S_LSHL4_ADD_U32    = 0x31;
constexpr uint16_t S_PACK_LL_B32_B16  = 0x32;
constexpr uint16_t S_PACK_LH_B32_B16  = 0x33;
constexpr uint16_t S_PACK_HH_B32_B16  = 0x34;
} // namespace sop2_op

namespace sopk_op {
constexpr uint16_t S_MOVK_I32         = 0x00;
constexpr uint16_t S_CMOVK_I32        = 0x02;
constexpr uint16_t S_CMPK_EQ_I32      = 0x03;
constexpr uint16_t S_CMPK_LG_I32      = 0x04;
constexpr uint16_t S_CMPK_GT_I32      = 0x05;
constexpr uint16_t S_CMPK_GE_I32      = 0x06;
constexpr uint16_t S_CMPK_LT_I32      = 0x07;
constexpr uint16_t S_CMPK_LE_I32      = 0x08;
constexpr uint16_t S_CMPK_EQ_U32      = 0x09;
constexpr uint16_t S_CMPK_LG_U32      = 0x0A;
constexpr uint16_t S_CMPK_GT_U32      = 0x0B;
constexpr uint16_t S_CMPK_GE_U32      = 0x0C;
constexpr uint16_t S_CMPK_LT_U32      = 0x0D;
constexpr uint16_t S_CMPK_LE_U32      = 0x0E;
constexpr uint16_t S_ADDK_I32         = 0x0F;
constexpr uint16_t S_MULK_I32         = 0x10;
constexpr uint16_t S_GETREG_B32       = 0x12;
constexpr uint16_t S_SETREG_B32       = 0x13;
constexpr uint16_t S_SETREG_IMM32_B32 = 0x15;
} // namespace sopk_op

namespace sop1_op {
constexpr uint16_t S_MOV_B32          = 0x03;
constexpr uint16_t S_MOV_B64          = 0x04;
constexpr uint16_t S_CMOV_B32         = 0x05;
constexpr uint16_t S_CMOV_B64         = 0x06;
constexpr uint16_t S_NOT_B32          = 0x07;
constexpr uint16_t S_NOT_B64          = 0x08;
constexpr uint16_t S_WQM_B32          = 0x09;
constexpr uint16_t S_WQM_B64          = 0x0A;
constexpr uint16_t S_BREV_B32         = 0x0B;
constexpr uint16_t S_BREV_B64         = 0x0C;
constexpr uint16_t S_BCNT0_I32_B32    = 0x0D;
constexpr uint16_t S_BCNT0_I32_B64    = 0x0E;
constexpr uint16_t S_BCNT1_I32_B32    = 0x0F;
constexpr uint16_t S_BCNT1_I32_B64    = 0x10;
constexpr uint16_t S_FF0_I32_B32      = 0x11;
constexpr uint16_t S_FF0_I32_B64      = 0x12;
constexpr uint16_t S_FF1_I32_B32      = 0x13;
constexpr uint16_t S_FF1_I32_B64      = 0x14;
constexpr uint16_t S_FLBIT_I32_B32    = 0x15;
constexpr uint16_t S_FLBIT_I32_B64    = 0x16;
constexpr uint16_t S_FLBIT_I32        = 0x17;
constexpr uint16_t S_SEXT_I32_I8      = 0x19;
constexpr uint16_t S_SEXT_I32_I16     = 0x1A;
constexpr uint16_t S_BITSET0_B32      = 0x1B;
constexpr uint16_t S_BITSET1_B32      = 0x1D;
constexpr uint16_t S_AND_SAVEEXEC_B64 = 0x24;
constexpr uint16_t S_OR_SAVEEXEC_B64  = 0x25;
constexpr uint16_t S_XOR_SAVEEXEC_B64 = 0x26;
constexpr uint16_t S_ANDN2_SAVEEXEC_B64 = 0x27;
constexpr uint16_t S_ORN2_SAVEEXEC_B64  = 0x28;
constexpr uint16_t S_NAND_SAVEEXEC_B64  = 0x29;
constexpr uint16_t S_NOR_SAVEEXEC_B64   = 0x2A;
constexpr uint16_t S_XNOR_SAVEEXEC_B64  = 0x2B;
constexpr uint16_t S_QUADMASK_B32     = 0x2C;
constexpr uint16_t S_QUADMASK_B64     = 0x2D;
constexpr uint16_t S_ABS_I32          = 0x34;
constexpr uint16_t S_AND_SAVEEXEC_B32 = 0x3C;
constexpr uint16_t S_OR_SAVEEXEC_B32  = 0x3D;
constexpr uint16_t S_XOR_SAVEEXEC_B32 = 0x3E;
constexpr uint16_t S_ANDN2_SAVEEXEC_B32 = 0x3F;
} // namespace sop1_op

namespace sopc_op {
constexpr uint16_t S_CMP_EQ_I32       = 0x00;
constexpr uint16_t S_CMP_LG_I32       = 0x01;
constexpr uint16_t S_CMP_GT_I32       = 0x02;
constexpr uint16_t S_CMP_GE_I32       = 0x03;
constexpr uint16_t S_CMP_LT_I32       = 0x04;
constexpr uint16_t S_CMP_LE_I32       = 0x05;
constexpr uint16_t S_CMP_EQ_U32       = 0x06;
constexpr uint16_t S_CMP_LG_U32       = 0x07;
constexpr uint16_t S_CMP_GT_U32       = 0x08;
constexpr uint16_t S_CMP_GE_U32       = 0x09;
constexpr uint16_t S_CMP_LT_U32       = 0x0A;
constexpr uint16_t S_CMP_LE_U32       = 0x0B;
constexpr uint16_t S_BITCMP0_B32      = 0x0C;
constexpr uint16_t S_BITCMP1_B32      = 0x0D;
constexpr uint16_t S_BITCMP0_B64      = 0x0E;
constexpr uint16_t S_BITCMP1_B64      = 0x0F;
constexpr uint16_t S_CMP_EQ_U64       = 0x12;
constexpr uint16_t S_CMP_LG_U64       = 0x13;
} // namespace sopc_op

namespace sopp_op {
constexpr uint16_t S_NOP              = 0x00;
constexpr uint16_t S_ENDPGM           = 0x01;
constexpr uint16_t S_BRANCH           = 0x02;
constexpr uint16_t S_WAKEUP           = 0x03;
constexpr uint16_t S_CBRANCH_SCC0     = 0x04;
constexpr uint16_t S_CBRANCH_SCC1     = 0x05;
constexpr uint16_t S_CBRANCH_VCCZ     = 0x06;
constexpr uint16_t S_CBRANCH_VCCNZ    = 0x07;
constexpr uint16_t S_CBRANCH_EXECZ    = 0x08;
constexpr uint16_t S_CBRANCH_EXECNZ   = 0x09;
constexpr uint16_t S_BARRIER          = 0x0A;
constexpr uint16_t S_WAITCNT          = 0x0C;
constexpr uint16_t S_SETHALT          = 0x0D;
constexpr uint16_t S_SLEEP            = 0x0E;
constexpr uint16_t S_SENDMSG          = 0x10;
constexpr uint16_t S_ENDPGM_SAVED     = 0x1B;
constexpr uint16_t S_WAITCNT_VSCNT    = 0x17;
constexpr uint16_t S_WAITCNT_VMCNT    = 0x18;
constexpr uint16_t S_WAITCNT_EXPCNT   = 0x19;
constexpr uint16_t S_WAITCNT_LGKMCNT  = 0x1A;
constexpr uint16_t S_CLAUSE           = 0x21;
constexpr uint16_t S_CODE_END         = 0x1F;
} // namespace sopp_op

namespace smem_op {
constexpr uint16_t S_LOAD_DWORD        = 0x00;
constexpr uint16_t S_LOAD_DWORDX2      = 0x01;
constexpr uint16_t S_LOAD_DWORDX4      = 0x02;
constexpr uint16_t S_LOAD_DWORDX8      = 0x03;
constexpr uint16_t S_LOAD_DWORDX16     = 0x04;
constexpr uint16_t S_BUFFER_LOAD_DWORD   = 0x08;
constexpr uint16_t S_BUFFER_LOAD_DWORDX2 = 0x09;
constexpr uint16_t S_BUFFER_LOAD_DWORDX4 = 0x0A;
constexpr uint16_t S_BUFFER_LOAD_DWORDX8 = 0x0B;
constexpr uint16_t S_BUFFER_LOAD_DWORDX16 = 0x0C;
constexpr uint16_t S_STORE_DWORD       = 0x10;
constexpr uint16_t S_STORE_DWORDX2     = 0x11;
constexpr uint16_t S_STORE_DWORDX4     = 0x12;
constexpr uint16_t S_BUFFER_STORE_DWORD = 0x18;
constexpr uint16_t S_DCACHE_INV        = 0x20;
constexpr uint16_t S_MEMTIME           = 0x24;
} // namespace smem_op

namespace vop2_op {
constexpr uint16_t V_CNDMASK_B32       = 0x01;
constexpr uint16_t V_ADD_F32           = 0x03;
constexpr uint16_t V_SUB_F32           = 0x04;
constexpr uint16_t V_SUBREV_F32        = 0x05;
constexpr uint16_t V_FMAC_F32          = 0x2B;
constexpr uint16_t V_MUL_LEGACY_F32    = 0x07;
constexpr uint16_t V_MUL_F32           = 0x08;
constexpr uint16_t V_MUL_I32_I24       = 0x09;
constexpr uint16_t V_MUL_U32_U24       = 0x0B;
constexpr uint16_t V_MIN_F32           = 0x0F;
constexpr uint16_t V_MAX_F32           = 0x10;
constexpr uint16_t V_MIN_I32           = 0x11;
constexpr uint16_t V_MAX_I32           = 0x12;
constexpr uint16_t V_MIN_U32           = 0x13;
constexpr uint16_t V_MAX_U32           = 0x14;
constexpr uint16_t V_LSHRREV_B32       = 0x16;
constexpr uint16_t V_ASHRREV_I32       = 0x17;
constexpr uint16_t V_LSHLREV_B32       = 0x18;
constexpr uint16_t V_AND_B32           = 0x1B;
constexpr uint16_t V_OR_B32            = 0x1C;
constexpr uint16_t V_XOR_B32           = 0x1D;
constexpr uint16_t V_XNOR_B32          = 0x1E;
constexpr uint16_t V_ADD_CO_CI_U32     = 0x28;
constexpr uint16_t V_SUB_CO_CI_U32     = 0x29;
constexpr uint16_t V_ADD_NC_U32        = 0x25;
constexpr uint16_t V_SUB_NC_U32        = 0x26;
constexpr uint16_t V_SUBREV_NC_U32     = 0x27;
constexpr uint16_t V_CVT_PKRTZ_F16_F32 = 0x2F;
constexpr uint16_t V_FMAMK_F32         = 0x2C;
constexpr uint16_t V_FMAAK_F32         = 0x2D;
constexpr uint16_t V_ADD_F16           = 0x32;
constexpr uint16_t V_SUB_F16           = 0x33;
constexpr uint16_t V_SUBREV_F16        = 0x34;
constexpr uint16_t V_MUL_F16           = 0x35;
constexpr uint16_t V_FMAC_F16          = 0x36;
constexpr uint16_t V_FMAMK_F16         = 0x37;
constexpr uint16_t V_FMAAK_F16         = 0x38;
constexpr uint16_t V_MAX_F16           = 0x39;
constexpr uint16_t V_MIN_F16           = 0x3A;
constexpr uint16_t V_LDEXP_F16         = 0x3B;
constexpr uint16_t V_DOT2C_F32_F16     = 0x02;
constexpr uint16_t V_DOT4C_I32_I8      = 0x0D;
} // namespace vop2_op

namespace vop1_op {
constexpr uint16_t V_NOP               = 0x00;
constexpr uint16_t V_MOV_B32           = 0x01;
constexpr uint16_t V_READFIRSTLANE_B32 = 0x02;
constexpr uint16_t V_CVT_F32_I32       = 0x05;
constexpr uint16_t V_CVT_F32_U32       = 0x06;
constexpr uint16_t V_CVT_U32_F32       = 0x07;
constexpr uint16_t V_CVT_I32_F32       = 0x08;
constexpr uint16_t V_CVT_F16_F32       = 0x0A;
constexpr uint16_t V_CVT_F32_F16       = 0x0B;
constexpr uint16_t V_CVT_RPI_I32_F32   = 0x0C;
constexpr uint16_t V_CVT_FLR_I32_F32   = 0x0D;
constexpr uint16_t V_FRACT_F32         = 0x20;
constexpr uint16_t V_TRUNC_F32         = 0x21;
constexpr uint16_t V_CEIL_F32          = 0x22;
constexpr uint16_t V_RNDNE_F32         = 0x23;
constexpr uint16_t V_FLOOR_F32         = 0x24;
constexpr uint16_t V_EXP_F32           = 0x25;
constexpr uint16_t V_LOG_F32           = 0x27;
constexpr uint16_t V_RCP_F32           = 0x2A;
constexpr uint16_t V_RCP_IFLAG_F32     = 0x2B;
constexpr uint16_t V_RSQ_F32           = 0x2E;
constexpr uint16_t V_SQRT_F32          = 0x33;
constexpr uint16_t V_SIN_F32           = 0x35;
constexpr uint16_t V_COS_F32           = 0x36;
constexpr uint16_t V_NOT_B32           = 0x37;
constexpr uint16_t V_BFREV_B32         = 0x38;
constexpr uint16_t V_FFBH_U32          = 0x39;
constexpr uint16_t V_FFBL_B32          = 0x3A;
constexpr uint16_t V_FFBH_I32          = 0x3B;
constexpr uint16_t V_FREXP_EXP_I32_F32 = 0x3F;
constexpr uint16_t V_FREXP_MANT_F32    = 0x40;
constexpr uint16_t V_CVT_F16_U16       = 0x50;
constexpr uint16_t V_CVT_F16_I16       = 0x51;
constexpr uint16_t V_CVT_U16_F16       = 0x52;
constexpr uint16_t V_CVT_I16_F16       = 0x53;
constexpr uint16_t V_RCP_F16           = 0x54;
constexpr uint16_t V_SQRT_F16          = 0x55;
constexpr uint16_t V_RSQ_F16           = 0x56;
constexpr uint16_t V_LOG_F16           = 0x57;
constexpr uint16_t V_EXP_F16           = 0x58;
constexpr uint16_t V_FLOOR_F16         = 0x5B;
constexpr uint16_t V_CEIL_F16          = 0x5C;
constexpr uint16_t V_TRUNC_F16         = 0x5D;
constexpr uint16_t V_RNDNE_F16         = 0x5E;
constexpr uint16_t V_FRACT_F16         = 0x5F;
constexpr uint16_t V_SIN_F16           = 0x60;
constexpr uint16_t V_COS_F16           = 0x61;
constexpr uint16_t V_SWAP_B32          = 0x65;
} // namespace vop1_op

namespace vopc_op {
constexpr uint16_t V_CMP_F_F32         = 0x10;
constexpr uint16_t V_CMP_LT_F32        = 0x11;
constexpr uint16_t V_CMP_EQ_F32        = 0x12;
constexpr uint16_t V_CMP_LE_F32        = 0x13;
constexpr uint16_t V_CMP_GT_F32        = 0x14;
constexpr uint16_t V_CMP_LG_F32        = 0x15;
constexpr uint16_t V_CMP_GE_F32        = 0x16;
constexpr uint16_t V_CMP_O_F32         = 0x17;
constexpr uint16_t V_CMP_U_F32         = 0x18;
constexpr uint16_t V_CMP_NGE_F32       = 0x19;
constexpr uint16_t V_CMP_NLG_F32       = 0x1A;
constexpr uint16_t V_CMP_NGT_F32       = 0x1B;
constexpr uint16_t V_CMP_NLE_F32       = 0x1C;
constexpr uint16_t V_CMP_NEQ_F32       = 0x1D;
constexpr uint16_t V_CMP_NLT_F32       = 0x1E;
constexpr uint16_t V_CMP_TRU_F32       = 0x1F;
constexpr uint16_t V_CMP_LT_F16        = 0x51;
constexpr uint16_t V_CMP_EQ_F16        = 0x52;
constexpr uint16_t V_CMP_LE_F16        = 0x53;
constexpr uint16_t V_CMP_GT_F16        = 0x54;
constexpr uint16_t V_CMP_GE_F16        = 0x56;
constexpr uint16_t V_CMP_NEQ_F16       = 0x5D;
constexpr uint16_t V_CMP_LT_I32        = 0x81;
constexpr uint16_t V_CMP_EQ_I32        = 0x82;
constexpr uint16_t V_CMP_LE_I32        = 0x83;
constexpr uint16_t V_CMP_GT_I32        = 0x84;
constexpr uint16_t V_CMP_NE_I32        = 0x85;
constexpr uint16_t V_CMP_GE_I32        = 0x86;
constexpr uint16_t V_CMP_LT_U32        = 0xC1;
constexpr uint16_t V_CMP_EQ_U32        = 0xC2;
constexpr uint16_t V_CMP_LE_U32        = 0xC3;
constexpr uint16_t V_CMP_GT_U32        = 0xC4;
constexpr uint16_t V_CMP_NE_U32        = 0xC5;
constexpr uint16_t V_CMP_GE_U32        = 0xC6;
constexpr uint16_t V_CMP_CLASS_F32     = 0x88;
constexpr uint16_t V_CMP_CLASS_F16     = 0x8F;
/* V_CMPX (write EXEC directly): bit 4 set in GFX10.3 wave64 / 0x3x..0xEx */
constexpr uint16_t V_CMPX_LT_F32       = 0x31;
constexpr uint16_t V_CMPX_EQ_F32       = 0x32;
constexpr uint16_t V_CMPX_GT_F32       = 0x34;
constexpr uint16_t V_CMPX_LT_U32       = 0xE1;
constexpr uint16_t V_CMPX_NE_U32       = 0xE5;
} // namespace vopc_op

namespace vop3_op {
constexpr uint16_t V_FMA_F32           = 0x14B;
constexpr uint16_t V_MAD_F32           = 0x141;
constexpr uint16_t V_FMA_F16           = 0x14E;
constexpr uint16_t V_MAD_F16           = 0x143;
constexpr uint16_t V_MIN3_F32          = 0x150;
constexpr uint16_t V_MIN3_I32          = 0x151;
constexpr uint16_t V_MIN3_U32          = 0x152;
constexpr uint16_t V_MAX3_F32          = 0x153;
constexpr uint16_t V_MAX3_I32          = 0x154;
constexpr uint16_t V_MAX3_U32          = 0x155;
constexpr uint16_t V_MED3_F32          = 0x156;
constexpr uint16_t V_MED3_I32          = 0x157;
constexpr uint16_t V_MED3_U32          = 0x158;
constexpr uint16_t V_MIN3_F16          = 0x159;
constexpr uint16_t V_MAX3_F16          = 0x15C;
constexpr uint16_t V_MED3_F16          = 0x15F;
constexpr uint16_t V_BFE_U32           = 0x148;
constexpr uint16_t V_BFE_I32           = 0x149;
constexpr uint16_t V_BFI_B32           = 0x14A;
constexpr uint16_t V_ALIGNBIT_B32      = 0x146;
constexpr uint16_t V_ALIGNBYTE_B32     = 0x147;
constexpr uint16_t V_MUL_LO_U32        = 0x169;
constexpr uint16_t V_MUL_HI_U32        = 0x16A;
constexpr uint16_t V_MUL_HI_I32        = 0x16C;
constexpr uint16_t V_CVT_PKRTZ_F16_F32 = 0x16F;
constexpr uint16_t V_CVT_PKNORM_I16_F32 = 0x168;
constexpr uint16_t V_CVT_PKNORM_U16_F32 = 0x16B;
constexpr uint16_t V_PERM_B32          = 0x144;
constexpr uint16_t V_READLANE_B32      = 0x360;
constexpr uint16_t V_WRITELANE_B32     = 0x361;
constexpr uint16_t V_PERMLANE16_B32    = 0x377;
constexpr uint16_t V_PERMLANEX16_B32   = 0x378;
constexpr uint16_t V_ADD3_U32          = 0x36D;
constexpr uint16_t V_LSHL_ADD_U32      = 0x36E;
constexpr uint16_t V_ADD_LSHL_U32      = 0x36F;
constexpr uint16_t V_AND_OR_B32        = 0x371;
constexpr uint16_t V_OR3_B32           = 0x372;
constexpr uint16_t V_XAD_U32           = 0x345;
constexpr uint16_t V_MBCNT_LO_U32_B32  = 0x34C;
constexpr uint16_t V_MBCNT_HI_U32_B32  = 0x34D;
/* VOP3B (dual-dest: vdst + sdst carry/scale) */
constexpr uint16_t V_ADD_CO_U32        = 0x30F;
constexpr uint16_t V_SUB_CO_U32        = 0x310;
constexpr uint16_t V_SUBREV_CO_U32     = 0x319;
constexpr uint16_t V_DIV_SCALE_F32     = 0x16D;
constexpr uint16_t V_MAD_U64_U32       = 0x176;
constexpr uint16_t V_MAD_I64_I32       = 0x177;

inline bool is_vop3b_opcode(uint16_t op) noexcept {
    switch (op) {
        case V_ADD_CO_U32:
        case V_SUB_CO_U32:
        case V_SUBREV_CO_U32:
        case V_DIV_SCALE_F32:
        case V_MAD_U64_U32:
        case V_MAD_I64_I32:
            return true;
        default:
            return false;
    }
}
} // namespace vop3_op

namespace vop3p_op {
constexpr uint16_t V_PK_MAD_I16        = 0x00;
constexpr uint16_t V_PK_MUL_LO_U16     = 0x01;
constexpr uint16_t V_PK_ADD_I16        = 0x02;
constexpr uint16_t V_PK_SUB_I16        = 0x03;
constexpr uint16_t V_PK_LSHLREV_B16    = 0x04;
constexpr uint16_t V_PK_LSHRREV_B16    = 0x05;
constexpr uint16_t V_PK_ASHRREV_I16    = 0x06;
constexpr uint16_t V_PK_MAX_I16        = 0x07;
constexpr uint16_t V_PK_MIN_I16        = 0x08;
constexpr uint16_t V_PK_ADD_U16        = 0x0A;
constexpr uint16_t V_PK_SUB_U16        = 0x0B;
constexpr uint16_t V_PK_MAX_U16        = 0x0C;
constexpr uint16_t V_PK_MIN_U16        = 0x0D;
constexpr uint16_t V_PK_FMA_F16        = 0x0E;
constexpr uint16_t V_PK_ADD_F16        = 0x0F;
constexpr uint16_t V_PK_MUL_F16        = 0x10;
constexpr uint16_t V_PK_MIN_F16        = 0x11;
constexpr uint16_t V_PK_MAX_F16        = 0x12;
constexpr uint16_t V_DOT2_F32_F16      = 0x13;
constexpr uint16_t V_DOT2_I32_I16      = 0x14;
constexpr uint16_t V_DOT2_U32_U16      = 0x15;
constexpr uint16_t V_DOT4_I32_I8       = 0x16;
constexpr uint16_t V_DOT4_U32_U8       = 0x17;
constexpr uint16_t V_FMA_MIX_F32       = 0x20;
constexpr uint16_t V_FMA_MIXLO_F16     = 0x21;
constexpr uint16_t V_FMA_MIXHI_F16     = 0x22;
} // namespace vop3p_op

namespace vintrp_op {
constexpr uint16_t V_INTERP_P1_F32     = 0x00;
constexpr uint16_t V_INTERP_P2_F32     = 0x01;
constexpr uint16_t V_INTERP_MOV_F32    = 0x02;
} // namespace vintrp_op

namespace ds_op {
constexpr uint16_t DS_ADD_U32          = 0x00;
constexpr uint16_t DS_SUB_U32          = 0x01;
constexpr uint16_t DS_MIN_I32          = 0x05;
constexpr uint16_t DS_MAX_I32          = 0x06;
constexpr uint16_t DS_MIN_U32          = 0x07;
constexpr uint16_t DS_MAX_U32          = 0x08;
constexpr uint16_t DS_AND_B32          = 0x09;
constexpr uint16_t DS_OR_B32           = 0x0A;
constexpr uint16_t DS_XOR_B32          = 0x0B;
constexpr uint16_t DS_WRITE_B32        = 0x0D;
constexpr uint16_t DS_WRITE2_B32       = 0x0E;
constexpr uint16_t DS_CMPST_B32        = 0x10;
constexpr uint16_t DS_ADD_RTN_U32      = 0x20;
constexpr uint16_t DS_SWIZZLE_B32      = 0x35;
constexpr uint16_t DS_READ_B32         = 0x36;
constexpr uint16_t DS_READ2_B32        = 0x37;
constexpr uint16_t DS_WRITE_B64        = 0x4D;
constexpr uint16_t DS_READ_B64         = 0x76;
constexpr uint16_t DS_PERMUTE_B32      = 0xB2;
constexpr uint16_t DS_BPERMUTE_B32     = 0xB3;
constexpr uint16_t DS_WRITE_B128       = 0xDF;
constexpr uint16_t DS_READ_B128        = 0xFF;
} // namespace ds_op

namespace mubuf_op {
constexpr uint16_t BUFFER_LOAD_FORMAT_X    = 0x00;
constexpr uint16_t BUFFER_LOAD_FORMAT_XY   = 0x01;
constexpr uint16_t BUFFER_LOAD_FORMAT_XYZ  = 0x02;
constexpr uint16_t BUFFER_LOAD_FORMAT_XYZW = 0x03;
constexpr uint16_t BUFFER_STORE_FORMAT_X   = 0x04;
constexpr uint16_t BUFFER_STORE_FORMAT_XY  = 0x05;
constexpr uint16_t BUFFER_STORE_FORMAT_XYZ = 0x06;
constexpr uint16_t BUFFER_STORE_FORMAT_XYZW = 0x07;
constexpr uint16_t BUFFER_LOAD_UBYTE       = 0x08;
constexpr uint16_t BUFFER_LOAD_USHORT      = 0x0A;
constexpr uint16_t BUFFER_LOAD_DWORD       = 0x0C;
constexpr uint16_t BUFFER_LOAD_DWORDX2     = 0x0D;
constexpr uint16_t BUFFER_LOAD_DWORDX4     = 0x0E;
constexpr uint16_t BUFFER_LOAD_DWORDX3     = 0x0F;
constexpr uint16_t BUFFER_STORE_DWORD      = 0x1C;
constexpr uint16_t BUFFER_STORE_DWORDX2    = 0x1D;
constexpr uint16_t BUFFER_STORE_DWORDX3    = 0x1E;
constexpr uint16_t BUFFER_STORE_DWORDX4    = 0x1F;
constexpr uint16_t BUFFER_ATOMIC_SWAP      = 0x30;
constexpr uint16_t BUFFER_ATOMIC_CMPSWAP   = 0x31;
constexpr uint16_t BUFFER_ATOMIC_ADD       = 0x32;
constexpr uint16_t BUFFER_ATOMIC_SUB       = 0x33;
constexpr uint16_t BUFFER_ATOMIC_SMIN      = 0x34;
constexpr uint16_t BUFFER_ATOMIC_UMIN      = 0x35;
constexpr uint16_t BUFFER_ATOMIC_SMAX      = 0x36;
constexpr uint16_t BUFFER_ATOMIC_UMAX      = 0x37;
constexpr uint16_t BUFFER_ATOMIC_AND       = 0x38;
constexpr uint16_t BUFFER_ATOMIC_OR        = 0x39;
constexpr uint16_t BUFFER_ATOMIC_XOR       = 0x3A;
} // namespace mubuf_op

namespace mtbuf_op {
constexpr uint16_t TBUFFER_LOAD_FORMAT_X    = 0x00;
constexpr uint16_t TBUFFER_LOAD_FORMAT_XY   = 0x01;
constexpr uint16_t TBUFFER_LOAD_FORMAT_XYZ  = 0x02;
constexpr uint16_t TBUFFER_LOAD_FORMAT_XYZW = 0x03;
constexpr uint16_t TBUFFER_STORE_FORMAT_X   = 0x04;
constexpr uint16_t TBUFFER_STORE_FORMAT_XY  = 0x05;
constexpr uint16_t TBUFFER_STORE_FORMAT_XYZ = 0x06;
constexpr uint16_t TBUFFER_STORE_FORMAT_XYZW = 0x07;
} // namespace mtbuf_op

namespace mimg_op {
constexpr uint16_t IMAGE_LOAD               = 0x00;
constexpr uint16_t IMAGE_LOAD_MIP           = 0x01;
constexpr uint16_t IMAGE_STORE              = 0x08;
constexpr uint16_t IMAGE_STORE_MIP          = 0x09;
constexpr uint16_t IMAGE_GET_RESINFO        = 0x0E;
constexpr uint16_t IMAGE_ATOMIC_SWAP        = 0x10;
constexpr uint16_t IMAGE_ATOMIC_CMPSWAP     = 0x11;
constexpr uint16_t IMAGE_ATOMIC_ADD         = 0x12;
constexpr uint16_t IMAGE_ATOMIC_SMIN        = 0x14;
constexpr uint16_t IMAGE_ATOMIC_SMAX        = 0x16;
constexpr uint16_t IMAGE_SAMPLE             = 0x20;
constexpr uint16_t IMAGE_SAMPLE_D           = 0x22;
constexpr uint16_t IMAGE_SAMPLE_L           = 0x24;
constexpr uint16_t IMAGE_SAMPLE_B           = 0x25;
constexpr uint16_t IMAGE_SAMPLE_LZ          = 0x27;
constexpr uint16_t IMAGE_SAMPLE_C           = 0x28;
constexpr uint16_t IMAGE_GATHER4            = 0x40;
/* GFX10.3 Hardware BVH4 Ray-Intersection Instructions */
constexpr uint16_t IMAGE_BVH_INTERSECT_RAY   = 0x65;
constexpr uint16_t IMAGE_BVH64_INTERSECT_RAY = 0x66;
} // namespace mimg_op

namespace flat_op {
constexpr uint16_t LOAD_UBYTE          = 0x08;
constexpr uint16_t LOAD_USHORT         = 0x0A;
constexpr uint16_t LOAD_DWORD          = 0x0C;
constexpr uint16_t LOAD_DWORDX2        = 0x0D;
constexpr uint16_t LOAD_DWORDX3        = 0x0E;
constexpr uint16_t LOAD_DWORDX4        = 0x0F;
constexpr uint16_t STORE_DWORD         = 0x1C;
constexpr uint16_t STORE_DWORDX2       = 0x1D;
constexpr uint16_t STORE_DWORDX3       = 0x1E;
constexpr uint16_t STORE_DWORDX4       = 0x1F;
constexpr uint16_t ATOMIC_CMPSWAP      = 0x31;
constexpr uint16_t ATOMIC_ADD          = 0x32;
constexpr uint16_t ATOMIC_SMIN         = 0x34;
constexpr uint16_t ATOMIC_SMAX         = 0x36;
constexpr uint16_t ATOMIC_AND          = 0x38;
constexpr uint16_t ATOMIC_OR           = 0x39;
constexpr uint16_t ATOMIC_XOR          = 0x3A;
} // namespace flat_op

namespace exp_target {
constexpr uint8_t MRT0      = 0;
constexpr uint8_t MRT7      = 7;
constexpr uint8_t MRTZ      = 8;
constexpr uint8_t NULL_EXP  = 9;
constexpr uint8_t POS0      = 12;
constexpr uint8_t POS1      = 13;
constexpr uint8_t POS2      = 14;
constexpr uint8_t POS3      = 15;
constexpr uint8_t PRIM      = 20; /* NGG Mesh primitive export */
constexpr uint8_t PARAM0    = 32;
} // namespace exp_target

/* ============================================================================
 * GFX10.3 Operand Codes (src0..src2)
 * ============================================================================ */

namespace reg_code {
constexpr uint16_t SGPR_BASE     = 0;   /* 0..105 */
constexpr uint16_t VCC_LO        = 106;
constexpr uint16_t VCC_HI        = 107;
constexpr uint16_t M0            = 124;
constexpr uint16_t EXEC_LO       = 126;
constexpr uint16_t EXEC_HI       = 127;
constexpr uint16_t INLINE_INT_0  = 128; /* 128..192 -> 0..64 */
constexpr uint16_t INLINE_NEG_1  = 193; /* 193..208 -> -1..-16 */
constexpr uint16_t INLINE_F_0_5  = 240; /* 0.5f */
constexpr uint16_t INLINE_F_N0_5 = 241; /* -0.5f */
constexpr uint16_t INLINE_F_1_0  = 242; /* 1.0f */
constexpr uint16_t INLINE_F_N1_0 = 243; /* -1.0f */
constexpr uint16_t INLINE_F_2_0  = 244; /* 2.0f */
constexpr uint16_t INLINE_F_N2_0 = 245; /* -2.0f */
constexpr uint16_t INLINE_F_4_0  = 246; /* 4.0f */
constexpr uint16_t INLINE_F_N4_0 = 247; /* -4.0f */
constexpr uint16_t INLINE_INV2PI = 248; /* 1/(2*pi) */
constexpr uint16_t SCC           = 253;
constexpr uint16_t LITERAL_32    = 255;
constexpr uint16_t VGPR_BASE     = 256; /* 256..511 -> v0..v255 */
} // namespace reg_code

/* ============================================================================
 * Decoded GFX10.3 Instruction
 * ============================================================================ */

struct DecodedInst {
    uint32_t pc_words{0};    /* Word offset in instruction stream */
    uint8_t  word_count{1};  /* 1, 2, or 3 32-bit words */
    IsaClass cls{IsaClass::Unknown};
    uint16_t opcode{0};

    /* Destinations */
    uint16_t dst{0};         /* SGPR or VGPR index */
    uint16_t sdst{0};        /* Secondary scalar dest for VOP3B / SMEM */
    bool     dst_is_vgpr{false};
    bool     has_sdst{false};

    /* Sources (GFX10.3 unified 9-bit operand space 0..511) */
    uint16_t src[4]{0, 0, 0, 0};
    uint8_t  src_count{0};

    /* Modifiers & immediates */
    uint32_t literal{0};
    bool     has_literal{false};
    int16_t  simm16{0};
    uint32_t offset{0};
    uint8_t  offset0{0};
    uint8_t  offset1{0};
    uint8_t  abs_mask{0};
    uint8_t  neg_mask{0};
    uint8_t  op_sel{0};
    uint8_t  op_sel_hi{0};
    uint8_t  omod{0};
    bool     clamp{false};

    /* Memory / Image / Export / Interp descriptors */
    uint8_t  srsrc{0};       /* SGPR group index (x4) */
    uint8_t  ssamp{0};       /* Sampler SGPR group */
    uint8_t  dmask{0};       /* Image / Export channel mask */
    uint8_t  dim{0};         /* Image dimension */
    uint8_t  attr{0};        /* VINTRP attribute */
    uint8_t  attr_chan{0};   /* VINTRP channel */
    uint8_t  exp_target{0};  /* EXP target */
    bool     exp_done{false};
    bool     exp_compr{false};
    bool     glc{false};
    bool     dlc{false};
    bool     slc{false};
    bool     offen{false};
    bool     idxen{false};
    bool     a16{false};
    bool     d16{false};
    uint8_t  tbuf_format{0};
    uint8_t  raw_seg{0};     /* 0=FLAT, 1=SCRATCH, 2=GLOBAL */
};

/* ============================================================================
 * Clean-Room GFX10.3 Binary Instruction Encoder (Assembler Helpers)
 * Used by corpus generator & unit tests to build authentic binary ISA streams.
 * ============================================================================ */

class Rdna2Assembler {
public:
    void clear() { words_.clear(); }
    const std::vector<uint32_t>& words() const noexcept { return words_; }
    uint32_t pc() const noexcept { return static_cast<uint32_t>(words_.size()); }

    /* SOP2: [31:30]=10, [29:23]=op, [22:16]=sdst, [15:8]=ssrc1, [7:0]=ssrc0 */
    void sop2(uint16_t op, uint8_t sdst, uint8_t ssrc0, uint8_t ssrc1,
              bool has_lit = false, uint32_t lit = 0) {
        uint32_t w = (0x2u << 30) | ((uint32_t)(op & 0x7Fu) << 23) |
                     ((uint32_t)(sdst & 0x7Fu) << 16) |
                     ((uint32_t)(ssrc1 & 0xFFu) << 8) | (uint32_t)(ssrc0 & 0xFFu);
        words_.push_back(w);
        if (has_lit || ssrc0 == 255u || ssrc1 == 255u) {
            words_.push_back(lit);
        }
    }

    /* SOPK: [31:28]=1011, [27:23]=op, [22:16]=sdst, [15:0]=simm16 */
    void sopk(uint16_t op, uint8_t sdst, int16_t simm16, bool has_imm32 = false, uint32_t imm32 = 0) {
        uint32_t w = (0xBu << 28) | ((uint32_t)(op & 0x1Fu) << 23) |
                     ((uint32_t)(sdst & 0x7Fu) << 16) |
                     static_cast<uint16_t>(simm16);
        words_.push_back(w);
        if (has_imm32 || op == sopk_op::S_SETREG_IMM32_B32) {
            words_.push_back(imm32);
        }
    }

    /* SOP1: [31:23]=101111101 (0x17D), [22:16]=sdst, [15:8]=op, [7:0]=ssrc0 */
    void sop1(uint16_t op, uint8_t sdst, uint8_t ssrc0, bool has_lit = false, uint32_t lit = 0) {
        uint32_t w = (0x17Du << 23) | ((uint32_t)(sdst & 0x7Fu) << 16) |
                     ((uint32_t)(op & 0xFFu) << 8) | (uint32_t)(ssrc0 & 0xFFu);
        words_.push_back(w);
        if (has_lit || ssrc0 == 255u) {
            words_.push_back(lit);
        }
    }

    /* SOPC: [31:23]=101111110 (0x17E), [22:16]=op, [15:8]=ssrc1, [7:0]=ssrc0 */
    void sopc(uint16_t op, uint8_t ssrc0, uint8_t ssrc1, bool has_lit = false, uint32_t lit = 0) {
        uint32_t w = (0x17Eu << 23) | ((uint32_t)(op & 0x7Fu) << 16) |
                     ((uint32_t)(ssrc1 & 0xFFu) << 8) | (uint32_t)(ssrc0 & 0xFFu);
        words_.push_back(w);
        if (has_lit || ssrc0 == 255u || ssrc1 == 255u) {
            words_.push_back(lit);
        }
    }

    /* SOPP: [31:23]=101111111 (0x17F), [22:16]=op, [15:0]=simm16 */
    void sopp(uint16_t op, int16_t simm16 = 0) {
        uint32_t w = (0x17Fu << 23) | ((uint32_t)(op & 0x7Fu) << 16) |
                     static_cast<uint16_t>(simm16);
        words_.push_back(w);
    }

    /* Patch a previously emitted SOPP branch relative offset (in dwords from next PC) */
    void patch_sopp_branch(uint32_t branch_pc, uint32_t target_pc) {
        int32_t rel = static_cast<int32_t>(target_pc) - static_cast<int32_t>(branch_pc + 1u);
        words_[branch_pc] = (words_[branch_pc] & 0xFFFF0000u) |
                            static_cast<uint16_t>(static_cast<int16_t>(rel));
    }

    /* SMEM: [31:26]=111101 (0x3D), [25:18]=op, [16]=glc, [14]=dlc, [12:6]=sdata, [5:0]=sbase */
    void smem(uint16_t op, uint8_t sdata, uint8_t sbase_div2, uint32_t offset,
              uint8_t soffset = 124u, bool glc = false, bool dlc = false) {
        uint32_t w0 = (0x3Du << 26) | ((uint32_t)(op & 0xFFu) << 18) |
                      (glc ? (1u << 16) : 0u) | (dlc ? (1u << 14) : 0u) |
                      ((uint32_t)(sdata & 0x7Fu) << 6) | (uint32_t)(sbase_div2 & 0x3Fu);
        uint32_t w1 = ((uint32_t)(soffset & 0x7Fu) << 25) | (offset & 0x1FFFFFu);
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* VOP2: [31]=0, [30:25]=op (0..61), [24:17]=vdst, [16:9]=vsrc1, [8:0]=src0 */
    void vop2(uint16_t op, uint8_t vdst, uint16_t src0, uint8_t vsrc1,
              bool has_lit = false, uint32_t lit = 0) {
        uint32_t w = ((uint32_t)(op & 0x3Fu) << 25) | ((uint32_t)vdst << 17) |
                     ((uint32_t)vsrc1 << 9) | (uint32_t)(src0 & 0x1FFu);
        words_.push_back(w);
        bool mandatory_lit = (op == vop2_op::V_FMAAK_F32 || op == vop2_op::V_FMAMK_F32 ||
                              op == vop2_op::V_FMAAK_F16 || op == vop2_op::V_FMAMK_F16);
        if (has_lit || mandatory_lit || src0 == reg_code::LITERAL_32) {
            words_.push_back(lit);
        }
    }

    /* VOP1: [31:25]=0111111 (0x3F), [24:17]=vdst, [16:9]=op, [8:0]=src0 */
    void vop1(uint16_t op, uint8_t vdst, uint16_t src0, bool has_lit = false, uint32_t lit = 0) {
        uint32_t w = (0x3Fu << 25) | ((uint32_t)vdst << 17) |
                     ((uint32_t)(op & 0xFFu) << 9) | (uint32_t)(src0 & 0x1FFu);
        words_.push_back(w);
        if (has_lit || src0 == reg_code::LITERAL_32) {
            words_.push_back(lit);
        }
    }

    /* VOPC: [31:25]=0111110 (0x3E), [24:17]=op, [16:9]=vsrc1, [8:0]=src0 */
    void vopc(uint16_t op, uint16_t src0, uint8_t vsrc1, bool has_lit = false, uint32_t lit = 0) {
        uint32_t w = (0x3Eu << 25) | ((uint32_t)(op & 0xFFu) << 17) |
                     ((uint32_t)vsrc1 << 9) | (uint32_t)(src0 & 0x1FFu);
        words_.push_back(w);
        if (has_lit || src0 == reg_code::LITERAL_32) {
            words_.push_back(lit);
        }
    }

    /* VOP3A: [31:26]=110101 (0x35), [25:16]=op, [15]=clamp, [14:11]=op_sel, [10:8]=abs, [7:0]=vdst */
    void vop3a(uint16_t op, uint8_t vdst, uint16_t src0, uint16_t src1, uint16_t src2 = 0,
               bool clamp = false, uint8_t abs_mask = 0, uint8_t neg_mask = 0,
               uint8_t op_sel = 0, uint8_t omod = 0) {
        uint32_t w0 = (0x35u << 26) | ((uint32_t)(op & 0x3FFu) << 16) |
                      (clamp ? (1u << 15) : 0u) | ((uint32_t)(op_sel & 0xFu) << 11) |
                      ((uint32_t)(abs_mask & 0x7u) << 8) | (uint32_t)vdst;
        uint32_t w1 = ((uint32_t)(neg_mask & 0x7u) << 29) | ((uint32_t)(omod & 0x3u) << 27) |
                      ((uint32_t)(src2 & 0x1FFu) << 18) | ((uint32_t)(src1 & 0x1FFu) << 9) |
                      (uint32_t)(src0 & 0x1FFu);
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* VOP3B: [31:26]=110101 (0x35), [25:16]=op, [15]=clamp, [14:8]=sdst, [7:0]=vdst */
    void vop3b(uint16_t op, uint8_t vdst, uint8_t sdst, uint16_t src0, uint16_t src1, uint16_t src2 = 0,
               bool clamp = false, uint8_t neg_mask = 0, uint8_t omod = 0) {
        uint32_t w0 = (0x35u << 26) | ((uint32_t)(op & 0x3FFu) << 16) |
                      (clamp ? (1u << 15) : 0u) | ((uint32_t)(sdst & 0x7Fu) << 8) |
                      (uint32_t)vdst;
        uint32_t w1 = ((uint32_t)(neg_mask & 0x7u) << 29) | ((uint32_t)(omod & 0x3u) << 27) |
                      ((uint32_t)(src2 & 0x1FFu) << 18) | ((uint32_t)(src1 & 0x1FFu) << 9) |
                      (uint32_t)(src0 & 0x1FFu);
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* VOP3P: [31:26]=110011 (0x33), [22:16]=op, [15]=clamp, [14]=op_sel_hi2, [13:11]=op_sel, [10:8]=neg_hi, [7:0]=vdst */
    void vop3p(uint16_t op, uint8_t vdst, uint16_t src0, uint16_t src1, uint16_t src2 = 0,
               bool clamp = false, uint8_t op_sel = 0, uint8_t op_sel_hi = 0x7,
               uint8_t neg_lo = 0, uint8_t neg_hi = 0) {
        uint32_t w0 = (0x33u << 26) | ((uint32_t)(op & 0x7Fu) << 16) |
                      (clamp ? (1u << 15) : 0u) | (((uint32_t)(op_sel_hi >> 2) & 1u) << 14) |
                      ((uint32_t)(op_sel & 0x7u) << 11) | ((uint32_t)(neg_hi & 0x7u) << 8) |
                      (uint32_t)vdst;
        uint32_t w1 = ((uint32_t)(neg_lo & 0x7u) << 29) | ((uint32_t)(op_sel_hi & 0x3u) << 27) |
                      ((uint32_t)(src2 & 0x1FFu) << 18) | ((uint32_t)(src1 & 0x1FFu) << 9) |
                      (uint32_t)(src0 & 0x1FFu);
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* VINTRP: [31:26]=110010 (0x32), [25:18]=vdst, [17:16]=op, [15:10]=attr, [9:8]=attrchan, [7:0]=vsrc */
    void vintrp(uint16_t op, uint8_t vdst, uint8_t vsrc, uint8_t attr, uint8_t attr_chan) {
        uint32_t w = (0x32u << 26) | ((uint32_t)vdst << 18) | ((uint32_t)(op & 0x3u) << 16) |
                     ((uint32_t)(attr & 0x3Fu) << 10) | ((uint32_t)(attr_chan & 0x3u) << 8) |
                     (uint32_t)vsrc;
        words_.push_back(w);
    }

    /* DS: [31:26]=110110 (0x36), [25:18]=op, [15:8]=offset1, [7:0]=offset0; w1: [31:24]=vdst, [23:16]=data1, [15:8]=data0, [7:0]=addr */
    void ds(uint16_t op, uint8_t vdst, uint8_t addr, uint8_t data0 = 0, uint8_t data1 = 0,
            uint8_t offset0 = 0, uint8_t offset1 = 0) {
        uint32_t w0 = (0x36u << 26) | ((uint32_t)(op & 0xFFu) << 18) |
                      ((uint32_t)offset1 << 8) | (uint32_t)offset0;
        uint32_t w1 = ((uint32_t)vdst << 24) | ((uint32_t)data1 << 16) |
                      ((uint32_t)data0 << 8) | (uint32_t)addr;
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* MUBUF: [31:26]=111000 (0x38), [24:18]=op, [15]=dlc, [14]=glc, [13]=idxen, [12]=offen, [11:0]=offset */
    void mubuf(uint16_t op, uint8_t vdata, uint8_t vaddr, uint8_t srsrc_div4,
               uint16_t offset = 0, uint8_t soffset = 128u, bool offen = true,
               bool idxen = false, bool glc = false) {
        uint32_t w0 = (0x38u << 26) | ((uint32_t)(op & 0x7Fu) << 18) |
                      (glc ? (1u << 14) : 0u) | (idxen ? (1u << 13) : 0u) |
                      (offen ? (1u << 12) : 0u) | (uint32_t)(offset & 0xFFFu);
        uint32_t w1 = ((uint32_t)soffset << 24) | ((uint32_t)(srsrc_div4 & 0x1Fu) << 16) |
                      ((uint32_t)vdata << 8) | (uint32_t)vaddr;
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* MTBUF: [31:26]=111010 (0x3A), [25:19]=format, [18:15]=op, [14]=glc, [13]=idxen, [12]=offen, [11:0]=offset */
    void mtbuf(uint16_t op, uint8_t format, uint8_t vdata, uint8_t vaddr, uint8_t srsrc_div4,
               uint16_t offset = 0, uint8_t soffset = 128u, bool offen = true) {
        uint32_t w0 = (0x3Au << 26) | ((uint32_t)(format & 0x7Fu) << 19) |
                      ((uint32_t)(op & 0xFu) << 15) | (offen ? (1u << 12) : 0u) |
                      (uint32_t)(offset & 0xFFFu);
        uint32_t w1 = ((uint32_t)soffset << 24) | ((uint32_t)(srsrc_div4 & 0x1Fu) << 16) |
                      ((uint32_t)vdata << 8) | (uint32_t)vaddr;
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* MIMG: [31:26]=111100 (0x3C), [24:18]=op, [15]=r128, [13]=glc, [11:8]=dmask, [5:3]=dim */
    void mimg(uint16_t op, uint8_t vdata, uint8_t vaddr, uint8_t srsrc_div4,
              uint8_t ssamp_div4 = 0, uint8_t dmask = 0xFu, uint8_t dim = 1u,
              bool d16 = false, bool a16 = false) {
        uint32_t w0 = (0x3Cu << 26) | ((uint32_t)(op & 0x7Fu) << 18) |
                      (a16 ? (1u << 16) : 0u) | ((uint32_t)(dmask & 0xFu) << 8) |
                      ((uint32_t)(dim & 0x7u) << 3);
        uint32_t w1 = (d16 ? (1u << 31) : 0u) | ((uint32_t)(ssamp_div4 & 0x1Fu) << 21) |
                      ((uint32_t)(srsrc_div4 & 0x1Fu) << 16) | ((uint32_t)vdata << 8) |
                      (uint32_t)vaddr;
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* FLAT/GLOBAL/SCRATCH: [31:26]=110111 (0x37), [24:18]=op, [15:14]=seg (0=FLAT,1=SCRATCH,2=GLOBAL), [11:0]=offset */
    void flat(uint16_t op, uint8_t seg, uint8_t vdst, uint8_t vaddr, uint8_t vdata,
              uint8_t saddr = 0x7Fu, uint16_t offset = 0, bool glc = false) {
        uint32_t w0 = (0x37u << 26) | ((uint32_t)(op & 0x7Fu) << 18) |
                      (glc ? (1u << 16) : 0u) | ((uint32_t)(seg & 0x3u) << 14) |
                      (uint32_t)(offset & 0xFFFu);
        uint32_t w1 = ((uint32_t)vdst << 24) | ((uint32_t)(saddr & 0x7Fu) << 16) |
                      ((uint32_t)vdata << 8) | (uint32_t)vaddr;
        words_.push_back(w0);
        words_.push_back(w1);
    }

    /* EXP: [31:26]=111110 (0x3E), [12]=vm, [11]=done, [10]=compr, [9:4]=target, [3:0]=en */
    void exp(uint8_t target, uint8_t vsrc0, uint8_t vsrc1, uint8_t vsrc2, uint8_t vsrc3,
             uint8_t en = 0xFu, bool done = true, bool compr = false, bool vm = false) {
        uint32_t w0 = (0x3Eu << 26) | (vm ? (1u << 12) : 0u) |
                      (done ? (1u << 11) : 0u) | (compr ? (1u << 10) : 0u) |
                      ((uint32_t)(target & 0x3Fu) << 4) | (uint32_t)(en & 0xFu);
        uint32_t w1 = ((uint32_t)vsrc3 << 24) | ((uint32_t)vsrc2 << 16) |
                      ((uint32_t)vsrc1 << 8) | (uint32_t)vsrc0;
        words_.push_back(w0);
        words_.push_back(w1);
    }

private:
    std::vector<uint32_t> words_;
};

/* Decode an entire raw GFX10.3 word stream into structured DecodedInst records. */
bool decode_gfx103_stream(const uint32_t* words,
                          size_t word_count,
                          std::vector<DecodedInst>& out_insts,
                          uint32_t& out_error_pc) noexcept;

} // namespace scarlett::rdna2

#endif // SCARLETT_RDNA2_ISA_HPP
