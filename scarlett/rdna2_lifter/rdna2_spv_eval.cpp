/*
 * Proton-Scarlett — Deterministic 64-Lane SPIR-V 1.6 Compute Binary VM
 *
 * Executes actual emitted SPIR-V 1.5/1.6 binaries across 64 lanes in lockstep
 * (with real OpControlBarrier synchronization, Workgroup LDS arrays,
 * StorageBuffer I/O, GroupNonUniform 64-lane subgroup ops, Float16 v2f16,
 * OpFmaKHR, DenormFlushToZero, PhysicalStorageBuffer64 BDA loads, and
 * OpRayQuery* triangle traversal) for differential exact-vs-adaptive verification
 * across all GPU profiles (NVIDIA Warp32, AMD RADV Wave64+FP16, Intel ANV).
 */

#include "rdna2_lifter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace scarlett::rdna2 {

namespace {

uint32_t f2u(float f) noexcept {
    uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    return u;
}

float u2f(uint32_t u) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &u, 4);
    return f;
}

/* IEEE-754 binary16 <-> binary32 clean-room helpers */
float f16_to_f32(uint16_t h) noexcept {
    const uint32_t sign = (static_cast<uint32_t>(h & 0x8000u)) << 16u;
    const uint32_t exp = (h >> 10u) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    if (exp == 0u) {
        if (mant == 0u) return u2f(sign);
        /* Subnormal */
        float v = static_cast<float>(mant) * (1.0f / 16777216.0f);
        return (sign != 0u) ? -v : v;
    }
    if (exp == 31u) {
        if (mant == 0u) return u2f(sign | 0x7F800000u);
        return u2f(sign | 0x7FC00000u | (mant << 13u));
    }
    const uint32_t f_exp = (exp + 112u) << 23u;
    return u2f(sign | f_exp | (mant << 13u));
}

uint16_t f32_to_f16(float f) noexcept {
    const uint32_t bits = f2u(f);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
    const int32_t exp = static_cast<int32_t>((bits >> 23u) & 0xFFu) - 127;
    const uint32_t mant = bits & 0x007FFFFFu;
    if (exp == 128) {
        if (mant == 0u) return static_cast<uint16_t>(sign | 0x7C00u);
        return static_cast<uint16_t>(sign | 0x7E00u);
    }
    if (exp > 15) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (exp < -14) {
        if (exp < -24) return sign;
        const uint32_t full_m = mant | 0x00800000u;
        const uint32_t shift = static_cast<uint32_t>(-14 - exp + 13);
        return static_cast<uint16_t>(sign | (full_m >> shift));
    }
    const uint16_t he = static_cast<uint16_t>((exp + 15) << 10);
    const uint16_t hm = static_cast<uint16_t>((mant + 0x00001000u) >> 13u);
    return static_cast<uint16_t>(sign | (he + hm));
}

float flush_if_ftz(float v, bool ftz) noexcept {
    if (ftz && std::fpclassify(v) == FP_SUBNORMAL) {
        return std::copysign(0.0f, v);
    }
    return v;
}

float rdna_nmin(float a, float b) noexcept {
    if (std::isnan(a)) return b;
    if (std::isnan(b)) return a;
    return a < b ? a : b;
}

float rdna_nmax(float a, float b) noexcept {
    if (std::isnan(a)) return b;
    if (std::isnan(b)) return a;
    return a > b ? a : b;
}

struct VmVal {
    uint32_t u[4]{0, 0, 0, 0};
    uint64_t u64{0};
    uint8_t  width{1};
    bool     is_f16_vec{false};

    /* For pointers */
    uint32_t ptr_kind{0}; /* 1=in_ssbo, 2=out_ssbo, 3=wg_var, 4=func_var, 5=psb, 6=builtin */
    uint32_t ptr_var{0};
    uint32_t ptr_idx{0};
    uint64_t ptr_va{0};
};

struct SpvInstView {
    uint16_t op;
    uint16_t wc;
    const uint32_t* words;
};

struct RayQueryState {
    float orig[3]{0, 0, 0};
    float dir[3]{0, 0, 1};
    float tmin{0.0f};
    float tmax{1e30f};
    size_t next_tri{0};
    bool has_candidate{false};
    float cand_t{1e30f};
    bool has_committed{false};
    float comm_t{1e30f};
    uint32_t comm_prim{0};
    uint32_t comm_inst{0};
};

bool intersect_tri_mt(const float orig[3], const float dir[3],
                      const SpirvVmInput::RayTri& tri,
                      float tmin, float tmax, float& out_t) noexcept {
    const float e1[3] = {tri.v1[0] - tri.v0[0], tri.v1[1] - tri.v0[1], tri.v1[2] - tri.v0[2]};
    const float e2[3] = {tri.v2[0] - tri.v0[0], tri.v2[1] - tri.v0[1], tri.v2[2] - tri.v0[2]};
    const float h[3] = {
        dir[1] * e2[2] - dir[2] * e2[1],
        dir[2] * e2[0] - dir[0] * e2[2],
        dir[0] * e2[1] - dir[1] * e2[0]
    };
    const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (std::fabs(det) < 1e-7f) return false;
    const float inv_det = 1.0f / det;
    const float s[3] = {orig[0] - tri.v0[0], orig[1] - tri.v0[1], orig[2] - tri.v0[2]};
    const float u = (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;
    const float q[3] = {
        s[1] * e1[2] - s[2] * e1[1],
        s[2] * e1[0] - s[0] * e1[2],
        s[0] * e1[1] - s[1] * e1[0]
    };
    const float v = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv_det;
    if (t > tmin && t < tmax) {
        out_t = t;
        return true;
    }
    return false;
}

} // namespace

SpirvVmOutput evaluate_spirv_compute_vm(const std::vector<uint32_t>& spirv_words,
                                        const SpirvVmInput& input) noexcept {
    SpirvVmOutput out{};
    if (spirv_words.size() < 5u || spirv_words[0] != 0x07230203u) {
        out.error = "Invalid SPIR-V header";
        return out;
    }
    const uint32_t bound = spirv_words[3];
    if (bound == 0u || bound > 262144u) {
        out.error = "Invalid SPIR-V ID bound";
        return out;
    }

    /* Parse all instructions */
    std::vector<SpvInstView> insts;
    size_t pos = 5u;
    while (pos < spirv_words.size()) {
        const uint32_t hdr = spirv_words[pos];
        const uint16_t wc = static_cast<uint16_t>(hdr >> 16u);
        const uint16_t op = static_cast<uint16_t>(hdr & 0xFFFFu);
        if (wc == 0u || pos + wc > spirv_words.size()) {
            out.error = "Truncated SPIR-V instruction";
            return out;
        }
        insts.push_back({op, wc, &spirv_words[pos]});
        pos += wc;
    }

    /* Extract metadata: types, decorations, constants, globals, labels, execution modes */
    bool f32_ftz = false;
    uint32_t glsl_ext_id = 0u;
    std::unordered_map<uint32_t, uint32_t> binding_of;
    std::unordered_map<uint32_t, uint32_t> builtin_of;
    std::unordered_map<uint32_t, uint32_t> type_op;
    std::unordered_map<uint32_t, uint32_t> type_width;
    std::unordered_map<uint32_t, uint32_t> type_elem;
    std::unordered_map<uint32_t, uint32_t> var_sc;
    std::unordered_map<uint32_t, VmVal> const_map;
    std::unordered_map<uint32_t, size_t> label_pc;
    std::unordered_map<uint32_t, std::vector<uint32_t>> wg_arrays;

    size_t fn_start_pc = insts.size();

    for (size_t i = 0; i < insts.size(); ++i) {
        const auto& iv = insts[i];
        const uint32_t* w = iv.words;
        switch (iv.op) {
            case 11: /* OpExtInstImport */
                glsl_ext_id = w[1];
                break;
            case 16: /* OpExecutionMode */
                if (iv.wc >= 4 && w[2] == 4460u && w[3] == 32u) f32_ftz = true;
                break;
            case 71: /* OpDecorate */
                if (iv.wc >= 4) {
                    if (w[2] == 33u) binding_of[w[1]] = w[3];
                    else if (w[2] == 11u) builtin_of[w[1]] = w[3];
                }
                break;
            case 21: /* OpTypeInt */
                type_op[w[1]] = 21u;
                type_width[w[1]] = w[2];
                break;
            case 22: /* OpTypeFloat */
                type_op[w[1]] = 22u;
                type_width[w[1]] = w[2];
                break;
            case 23: /* OpTypeVector */
                type_op[w[1]] = 23u;
                type_elem[w[1]] = w[2];
                type_width[w[1]] = w[3];
                break;
            case 41: { /* OpConstantTrue */
                VmVal v{}; v.u[0] = 1u; const_map[w[2]] = v;
                break;
            }
            case 42: { /* OpConstantFalse */
                VmVal v{}; v.u[0] = 0u; const_map[w[2]] = v;
                break;
            }
            case 43: { /* OpConstant */
                VmVal v{};
                if (iv.wc >= 5 && type_width[w[1]] == 64u) {
                    v.u64 = static_cast<uint64_t>(w[3]) | (static_cast<uint64_t>(w[4]) << 32u);
                    v.u[0] = w[3];
                    v.u[1] = w[4];
                } else {
                    v.u[0] = w[3];
                }
                const_map[w[2]] = v;
                break;
            }
            case 59: /* OpVariable */
                if (fn_start_pc == insts.size()) {
                    var_sc[w[2]] = w[3];
                    if (w[3] == 4u) { /* Workgroup */
                        wg_arrays[w[2]] = std::vector<uint32_t>(256u, 0u);
                    }
                }
                break;
            case 54: /* OpFunction */
                fn_start_pc = i + 1u;
                break;
            case 248: /* OpLabel */
                label_pc[w[1]] = i + 1u;
                break;
            default:
                break;
        }
    }

    if (fn_start_pc >= insts.size()) {
        out.error = "No OpFunction found in SPIR-V";
        return out;
    }

    /* Setup shared SSBO memory */
    std::vector<uint32_t> in_mem = input.input_buffer;
    if (in_mem.size() < 256u) in_mem.resize(256u, 0u);
    std::vector<uint32_t> out_mem(std::max<uint32_t>(input.output_word_count, 256u), 0u);

    const uint32_t lanes = std::clamp(input.dispatch_lanes, 1u, 64u);

    struct LaneState {
        size_t pc{0};
        bool finished{false};
        bool at_barrier{false};
        std::vector<VmVal> regs;
        std::unordered_map<uint32_t, std::vector<uint32_t>> func_vars;
        std::unordered_map<uint32_t, RayQueryState> ray_queries;
    };

    std::vector<LaneState> state(lanes);
    for (uint32_t l = 0; l < lanes; ++l) {
        state[l].pc = fn_start_pc;
        state[l].regs.resize(bound);
        for (const auto& kv : const_map) {
            if (kv.first < bound) state[l].regs[kv.first] = kv.second;
        }
        /* Initialize global pointer handles */
        for (const auto& kv : var_sc) {
            const uint32_t vid = kv.first;
            const uint32_t sc = kv.second;
            if (vid >= bound) continue;
            VmVal pv{};
            pv.ptr_var = vid;
            if (sc == 12u) { /* StorageBuffer */
                pv.ptr_kind = (binding_of[vid] == 0u) ? 1u : 2u;
            } else if (sc == 4u) { /* Workgroup */
                pv.ptr_kind = 3u;
            } else if (sc == 1u) { /* Input BuiltIn */
                pv.ptr_kind = 6u;
                pv.ptr_idx = builtin_of[vid];
            }
            state[l].regs[vid] = pv;
        }
    }

    auto read_bda_u32 = [&](uint64_t va) -> uint32_t {
        if (input.bda_memory == nullptr || va < input.bda_base_va) return 0u;
        const uint64_t off = va - input.bda_base_va;
        if (off + 4u > input.bda_memory_size) return 0u;
        uint32_t v = 0;
        std::memcpy(&v, input.bda_memory + off, 4);
        return v;
    };

    /* Lockstep multi-lane execution with barrier & subgroup wave synchronization */
    constexpr uint32_t kMaxSteps = 200000u;
    for (uint32_t step = 0; step < kMaxSteps; ++step) {
        bool any_active = false;
        bool all_barrier_or_done = true;

        /* Check if all lanes are at the same subgroup op or barrier */
        bool all_same_pc = true;
        size_t common_pc = state[0].pc;
        for (uint32_t l = 0; l < lanes; ++l) {
            if (!state[l].finished) {
                any_active = true;
                if (!state[l].at_barrier) all_barrier_or_done = false;
                if (state[l].pc != common_pc) all_same_pc = false;
            }
        }
        if (!any_active) break;

        if (all_barrier_or_done) {
            for (uint32_t l = 0; l < lanes; ++l) {
                if (!state[l].finished) {
                    state[l].at_barrier = false;
                    ++state[l].pc;
                }
            }
            continue;
        }

        /* Handle lockstep subgroup ops when all active lanes are aligned */
        if (all_same_pc && common_pc < insts.size()) {
            const auto& iv = insts[common_pc];
            const uint32_t* w = iv.words;
            if (iv.op == 338u || iv.op == 337u || iv.op == 339u ||
                iv.op == 342u || iv.op == 345u || iv.op == 346u) {
                /* Execute cross-lane subgroup instruction across all active lanes simultaneously */
                std::vector<uint32_t> lane_vals(lanes, 0u);
                for (uint32_t l = 0; l < lanes; ++l) {
                    lane_vals[l] = state[l].regs[w[4]].u[0];
                }
                if (iv.op == 338u) { /* OpGroupNonUniformBroadcastFirst */
                    for (uint32_t l = 0; l < lanes; ++l) {
                        state[l].regs[w[2]].u[0] = lane_vals[0];
                        ++state[l].pc;
                    }
                } else if (iv.op == 337u) { /* OpGroupNonUniformBroadcast */
                    const uint32_t src_l = state[0].regs[w[5]].u[0] & (lanes - 1u);
                    for (uint32_t l = 0; l < lanes; ++l) {
                        state[l].regs[w[2]].u[0] = lane_vals[src_l];
                        ++state[l].pc;
                    }
                } else if (iv.op == 339u) { /* OpGroupNonUniformBallot */
                    uint32_t lo = 0u, hi = 0u;
                    for (uint32_t l = 0; l < lanes; ++l) {
                        if (lane_vals[l] != 0u) {
                            if (l < 32u) lo |= (1u << l);
                            else hi |= (1u << (l - 32u));
                        }
                    }
                    for (uint32_t l = 0; l < lanes; ++l) {
                        VmVal v{}; v.u[0] = lo; v.u[1] = hi; v.width = 4;
                        state[l].regs[w[2]] = v;
                        ++state[l].pc;
                    }
                } else if (iv.op == 342u) { /* OpGroupNonUniformBallotBitCount */
                    const VmVal& bal = state[0].regs[w[5]];
                    const uint32_t cnt = static_cast<uint32_t>(__builtin_popcount(bal.u[0]) +
                                                               __builtin_popcount(bal.u[1]));
                    for (uint32_t l = 0; l < lanes; ++l) {
                        state[l].regs[w[2]].u[0] = cnt;
                        ++state[l].pc;
                    }
                } else if (iv.op == 345u) { /* OpGroupNonUniformShuffle */
                    for (uint32_t l = 0; l < lanes; ++l) {
                        const uint32_t tgt = state[l].regs[w[5]].u[0] & (lanes - 1u);
                        state[l].regs[w[2]].u[0] = lane_vals[tgt];
                        ++state[l].pc;
                    }
                } else if (iv.op == 346u) { /* OpGroupNonUniformShuffleXor */
                    for (uint32_t l = 0; l < lanes; ++l) {
                        const uint32_t mask = state[l].regs[w[5]].u[0];
                        const uint32_t tgt = (l ^ mask) & (lanes - 1u);
                        state[l].regs[w[2]].u[0] = lane_vals[tgt];
                        ++state[l].pc;
                    }
                }
                continue;
            }
        }

        /* Advance each non-blocked lane one instruction */
        for (uint32_t l = 0; l < lanes; ++l) {
            auto& ls = state[l];
            if (ls.finished || ls.at_barrier) continue;
            if (ls.pc >= insts.size()) {
                ls.finished = true;
                continue;
            }

            const auto& iv = insts[ls.pc];
            const uint32_t* w = iv.words;
            /* Maximal reconvergence: pause lane at subgroup collective until all active lanes arrive */
            if (iv.op == 338u || iv.op == 337u || iv.op == 339u ||
                iv.op == 342u || iv.op == 345u || iv.op == 346u) {
                continue;
            }
            auto& R = ls.regs;
            bool jumped = false;

            switch (iv.op) {
                case 248: /* OpLabel */
                case 246: /* OpLoopMerge */
                case 247: /* OpSelectionMerge */
                case 0:   /* OpNop */
                    break;

                case 59: { /* Function-scoped OpVariable */
                    ls.func_vars[w[2]] = std::vector<uint32_t>(64u, 0u);
                    VmVal pv{};
                    pv.ptr_kind = 4u;
                    pv.ptr_var = w[2];
                    pv.ptr_idx = 0u;
                    R[w[2]] = pv;
                    break;
                }

                case 224: /* OpControlBarrier */
                    ls.at_barrier = true;
                    jumped = true;
                    break;

                case 253: /* OpReturn */
                case 56:  /* OpFunctionEnd */
                    ls.finished = true;
                    jumped = true;
                    break;

                case 249: /* OpBranch */
                    ls.pc = label_pc[w[1]];
                    jumped = true;
                    break;

                case 250: /* OpBranchConditional */
                    ls.pc = (R[w[1]].u[0] != 0u) ? label_pc[w[2]] : label_pc[w[3]];
                    jumped = true;
                    break;

                case 65: { /* OpAccessChain */
                    VmVal base = R[w[3]];
                    if (base.ptr_kind == 1u || base.ptr_kind == 2u) {
                        /* SSBO struct -> member 0 -> array idx */
                        const uint32_t idx = (iv.wc >= 6) ? R[w[5]].u[0] : 0u;
                        base.ptr_idx = idx;
                    } else if (base.ptr_kind == 3u || base.ptr_kind == 4u) {
                        const uint32_t idx = (iv.wc >= 5) ? R[w[4]].u[0] : 0u;
                        base.ptr_idx = idx;
                    }
                    R[w[2]] = base;
                    break;
                }

                case 120: { /* OpConvertUToPtr */
                    VmVal pv{};
                    pv.ptr_kind = 5u;
                    pv.ptr_va = R[w[3]].u64;
                    R[w[2]] = pv;
                    break;
                }

                case 61: { /* OpLoad */
                    const VmVal& p = R[w[3]];
                    VmVal v{};
                    if (p.ptr_kind == 1u) {
                        v.u[0] = (p.ptr_idx < in_mem.size()) ? in_mem[p.ptr_idx] : 0u;
                    } else if (p.ptr_kind == 2u) {
                        v.u[0] = (p.ptr_idx < out_mem.size()) ? out_mem[p.ptr_idx] : 0u;
                    } else if (p.ptr_kind == 3u) {
                        auto& arr = wg_arrays[p.ptr_var];
                        v.u[0] = (p.ptr_idx < arr.size()) ? arr[p.ptr_idx] : 0u;
                    } else if (p.ptr_kind == 4u) {
                        auto& arr = ls.func_vars[p.ptr_var];
                        v.u[0] = (p.ptr_idx < arr.size()) ? arr[p.ptr_idx] : 0u;
                    } else if (p.ptr_kind == 5u) {
                        v.u[0] = read_bda_u32(p.ptr_va);
                    } else if (p.ptr_kind == 6u) {
                        if (p.ptr_idx == 28u) { /* GlobalInvocationId */
                            v.u[0] = l; v.u[1] = 0u; v.u[2] = 0u; v.width = 3;
                        } else if (p.ptr_idx == 29u || p.ptr_idx == 41u) { /* LocalInvocationIndex */
                            v.u[0] = l;
                        } else if (p.ptr_idx == 36u) { /* SubgroupSize */
                            v.u[0] = lanes;
                        }
                    }
                    R[w[2]] = v;
                    break;
                }

                case 62: { /* OpStore */
                    const VmVal& p = R[w[1]];
                    const VmVal& v = R[w[2]];
                    if (p.ptr_kind == 2u && p.ptr_idx < out_mem.size()) {
                        out_mem[p.ptr_idx] = v.u[0];
                    } else if (p.ptr_kind == 3u) {
                        auto& arr = wg_arrays[p.ptr_var];
                        if (p.ptr_idx < arr.size()) arr[p.ptr_idx] = v.u[0];
                    } else if (p.ptr_kind == 4u) {
                        auto& arr = ls.func_vars[p.ptr_var];
                        if (p.ptr_idx < arr.size()) arr[p.ptr_idx] = v.u[0];
                    }
                    break;
                }

                case 234: { /* OpAtomicIAdd */
                    const VmVal& p = R[w[3]];
                    const uint32_t addv = R[w[6]].u[0];
                    uint32_t old = 0u;
                    if (p.ptr_kind == 2u && p.ptr_idx < out_mem.size()) {
                        old = out_mem[p.ptr_idx];
                        out_mem[p.ptr_idx] = old + addv;
                    } else if (p.ptr_kind == 3u) {
                        auto& arr = wg_arrays[p.ptr_var];
                        if (p.ptr_idx < arr.size()) {
                            old = arr[p.ptr_idx];
                            arr[p.ptr_idx] = old + addv;
                        }
                    }
                    R[w[2]].u[0] = old;
                    break;
                }

                case 80: { /* OpCompositeConstruct */
                    VmVal v{};
                    v.width = static_cast<uint8_t>(std::min<int>(iv.wc - 3, 4));
                    for (uint8_t k = 0; k < v.width; ++k) v.u[k] = R[w[3 + k]].u[0];
                    if (type_op[w[1]] == 23u && type_width[type_elem[w[1]]] == 16u) {
                        v.is_f16_vec = true;
                    }
                    R[w[2]] = v;
                    break;
                }

                case 81: { /* OpCompositeExtract */
                    const uint32_t idx = w[4];
                    VmVal v{};
                    v.u[0] = (idx < 4u) ? R[w[3]].u[idx] : 0u;
                    R[w[2]] = v;
                    break;
                }

                case 124: { /* OpBitcast */
                    const uint32_t res_t = w[1];
                    const VmVal& src = R[w[3]];
                    VmVal dst{};
                    if (type_op[res_t] == 23u && type_width[type_elem[res_t]] == 16u) {
                        /* u32 -> v2f16 */
                        dst.is_f16_vec = true;
                        dst.width = 2;
                        dst.u[0] = src.u[0] & 0xFFFFu;
                        dst.u[1] = (src.u[0] >> 16u) & 0xFFFFu;
                    } else if (src.is_f16_vec) {
                        /* v2f16 -> u32 */
                        dst.u[0] = (src.u[0] & 0xFFFFu) | ((src.u[1] & 0xFFFFu) << 16u);
                    } else {
                        dst.u[0] = src.u[0];
                    }
                    R[w[2]] = dst;
                    break;
                }

                case 113: { /* OpUConvert */
                    VmVal dst{};
                    if (type_width[w[1]] == 64u) {
                        dst.u64 = R[w[3]].u[0];
                        dst.u[0] = R[w[3]].u[0];
                    } else {
                        dst.u[0] = static_cast<uint32_t>(R[w[3]].u64);
                    }
                    R[w[2]] = dst;
                    break;
                }

                case 115: { /* OpFConvert (f32 <-> f16) */
                    VmVal dst{};
                    if (type_width[w[1]] == 16u) {
                        dst.u[0] = f32_to_f16(u2f(R[w[3]].u[0]));
                    } else {
                        dst.u[0] = f2u(f16_to_f32(static_cast<uint16_t>(R[w[3]].u[0])));
                    }
                    R[w[2]] = dst;
                    break;
                }

                case 109: R[w[2]].u[0] = static_cast<uint32_t>(u2f(R[w[3]].u[0])); break;
                case 110: R[w[2]].u[0] = static_cast<uint32_t>(static_cast<int32_t>(u2f(R[w[3]].u[0]))); break;
                case 111: R[w[2]].u[0] = f2u(static_cast<float>(static_cast<int32_t>(R[w[3]].u[0]))); break;
                case 112: R[w[2]].u[0] = f2u(static_cast<float>(R[w[3]].u[0])); break;

                case 128: /* OpIAdd */
                    if (type_width[w[1]] == 64u) R[w[2]].u64 = R[w[3]].u64 + R[w[4]].u64;
                    else R[w[2]].u[0] = R[w[3]].u[0] + R[w[4]].u[0];
                    break;
                case 130: /* OpISub */
                    if (type_width[w[1]] == 64u) R[w[2]].u64 = R[w[3]].u64 - R[w[4]].u64;
                    else R[w[2]].u[0] = R[w[3]].u[0] - R[w[4]].u[0];
                    break;
                case 132: R[w[2]].u[0] = R[w[3]].u[0] * R[w[4]].u[0]; break;
                case 151: { /* OpUMulExtended */
                    uint64_t p = static_cast<uint64_t>(R[w[3]].u[0]) * static_cast<uint64_t>(R[w[4]].u[0]);
                    R[w[2]].u[0] = static_cast<uint32_t>(p);
                    R[w[2]].u[1] = static_cast<uint32_t>(p >> 32u);
                    break;
                }
                case 152: { /* OpSMulExtended */
                    int64_t p = static_cast<int64_t>(static_cast<int32_t>(R[w[3]].u[0])) *
                                static_cast<int64_t>(static_cast<int32_t>(R[w[4]].u[0]));
                    R[w[2]].u[0] = static_cast<uint32_t>(p);
                    R[w[2]].u[1] = static_cast<uint32_t>(static_cast<uint64_t>(p) >> 32u);
                    break;
                }
                case 194: R[w[2]].u[0] = R[w[3]].u[0] >> (R[w[4]].u[0] & 31u); break;
                case 195: R[w[2]].u[0] = static_cast<uint32_t>(static_cast<int32_t>(R[w[3]].u[0]) >> (R[w[4]].u[0] & 31u)); break;
                case 196:
                    if (type_width[w[1]] == 64u) R[w[2]].u64 = R[w[3]].u64 << (R[w[4]].u64 & 63u);
                    else R[w[2]].u[0] = R[w[3]].u[0] << (R[w[4]].u[0] & 31u);
                    break;
                case 197:
                    if (type_width[w[1]] == 64u) R[w[2]].u64 = R[w[3]].u64 | R[w[4]].u64;
                    else R[w[2]].u[0] = R[w[3]].u[0] | R[w[4]].u[0];
                    break;
                case 198: R[w[2]].u[0] = R[w[3]].u[0] ^ R[w[4]].u[0]; break;
                case 199: R[w[2]].u[0] = R[w[3]].u[0] & R[w[4]].u[0]; break;
                case 200: R[w[2]].u[0] = ~R[w[3]].u[0]; break;
                case 205: R[w[2]].u[0] = static_cast<uint32_t>(__builtin_popcount(R[w[3]].u[0])); break;
                case 203: { /* OpBitFieldUExtract */
                    uint32_t off = R[w[4]].u[0] & 31u;
                    uint32_t cnt = R[w[5]].u[0] & 31u;
                    uint32_t m = (cnt == 0u) ? 0u : ((1u << cnt) - 1u);
                    R[w[2]].u[0] = (R[w[3]].u[0] >> off) & m;
                    break;
                }

                /* Floating-point arithmetic (f32 and native v2f16) */
                case 127: R[w[2]].u[0] = f2u(-u2f(R[w[3]].u[0])); break;
                case 129: /* OpFAdd */
                    if (R[w[3]].is_f16_vec) {
                        VmVal v{}; v.is_f16_vec = true; v.width = 2;
                        for (int k = 0; k < 2; ++k) {
                            float r = flush_if_ftz(f16_to_f32(static_cast<uint16_t>(R[w[3]].u[k])) +
                                                   f16_to_f32(static_cast<uint16_t>(R[w[4]].u[k])), f32_ftz);
                            v.u[k] = f32_to_f16(r);
                        }
                        R[w[2]] = v;
                    } else {
                        float a = flush_if_ftz(u2f(R[w[3]].u[0]), f32_ftz);
                        float b = flush_if_ftz(u2f(R[w[4]].u[0]), f32_ftz);
                        R[w[2]].u[0] = f2u(flush_if_ftz(a + b, f32_ftz));
                    }
                    break;
                case 131: { /* OpFSub */
                    float a = flush_if_ftz(u2f(R[w[3]].u[0]), f32_ftz);
                    float b = flush_if_ftz(u2f(R[w[4]].u[0]), f32_ftz);
                    R[w[2]].u[0] = f2u(flush_if_ftz(a - b, f32_ftz));
                    break;
                }
                case 133: /* OpFMul */
                    if (R[w[3]].is_f16_vec) {
                        VmVal v{}; v.is_f16_vec = true; v.width = 2;
                        for (int k = 0; k < 2; ++k) {
                            float r = flush_if_ftz(f16_to_f32(static_cast<uint16_t>(R[w[3]].u[k])) *
                                                   f16_to_f32(static_cast<uint16_t>(R[w[4]].u[k])), f32_ftz);
                            v.u[k] = f32_to_f16(r);
                        }
                        R[w[2]] = v;
                    } else {
                        float a = flush_if_ftz(u2f(R[w[3]].u[0]), f32_ftz);
                        float b = flush_if_ftz(u2f(R[w[4]].u[0]), f32_ftz);
                        R[w[2]].u[0] = f2u(flush_if_ftz(a * b, f32_ftz));
                    }
                    break;
                case 136: { /* OpFDiv */
                    float a = flush_if_ftz(u2f(R[w[3]].u[0]), f32_ftz);
                    float b = flush_if_ftz(u2f(R[w[4]].u[0]), f32_ftz);
                    R[w[2]].u[0] = f2u(flush_if_ftz(a / b, f32_ftz));
                    break;
                }
                case 4427: { /* OpFmaKHR */
                    float a = flush_if_ftz(u2f(R[w[3]].u[0]), f32_ftz);
                    float b = flush_if_ftz(u2f(R[w[4]].u[0]), f32_ftz);
                    float c = flush_if_ftz(u2f(R[w[5]].u[0]), f32_ftz);
                    R[w[2]].u[0] = f2u(flush_if_ftz(std::fma(a, b, c), f32_ftz));
                    break;
                }

                /* Logic, Comparisons, Select */
                case 156: R[w[2]].u[0] = std::isnan(u2f(R[w[3]].u[0])) ? 1u : 0u; break;
                case 166: R[w[2]].u[0] = (R[w[3]].u[0] != 0u || R[w[4]].u[0] != 0u) ? 1u : 0u; break;
                case 167: R[w[2]].u[0] = (R[w[3]].u[0] != 0u && R[w[4]].u[0] != 0u) ? 1u : 0u; break;
                case 168: R[w[2]].u[0] = (R[w[3]].u[0] == 0u) ? 1u : 0u; break;
                case 169: R[w[2]] = (R[w[3]].u[0] != 0u) ? R[w[4]] : R[w[5]]; break;
                case 170: R[w[2]].u[0] = (R[w[3]].u[0] == R[w[4]].u[0]) ? 1u : 0u; break;
                case 171:
                    if (type_width[w[1]] == 64u || R[w[3]].u64 != 0u || R[w[4]].u64 != 0u) {
                        R[w[2]].u[0] = (R[w[3]].u64 != R[w[4]].u64 || R[w[3]].u[0] != R[w[4]].u[0]) ? 1u : 0u;
                    } else {
                        R[w[2]].u[0] = (R[w[3]].u[0] != R[w[4]].u[0]) ? 1u : 0u;
                    }
                    break;
                case 172: R[w[2]].u[0] = (R[w[3]].u[0] > R[w[4]].u[0]) ? 1u : 0u; break;
                case 173: R[w[2]].u[0] = (static_cast<int32_t>(R[w[3]].u[0]) > static_cast<int32_t>(R[w[4]].u[0])) ? 1u : 0u; break;
                case 174: R[w[2]].u[0] = (R[w[3]].u[0] >= R[w[4]].u[0]) ? 1u : 0u; break;
                case 175: R[w[2]].u[0] = (static_cast<int32_t>(R[w[3]].u[0]) >= static_cast<int32_t>(R[w[4]].u[0])) ? 1u : 0u; break;
                case 176: R[w[2]].u[0] = (R[w[3]].u[0] < R[w[4]].u[0]) ? 1u : 0u; break;
                case 177: R[w[2]].u[0] = (static_cast<int32_t>(R[w[3]].u[0]) < static_cast<int32_t>(R[w[4]].u[0])) ? 1u : 0u; break;
                case 178: R[w[2]].u[0] = (R[w[3]].u[0] <= R[w[4]].u[0]) ? 1u : 0u; break;
                case 179: R[w[2]].u[0] = (static_cast<int32_t>(R[w[3]].u[0]) <= static_cast<int32_t>(R[w[4]].u[0])) ? 1u : 0u; break;
                case 180: R[w[2]].u[0] = (u2f(R[w[3]].u[0]) == u2f(R[w[4]].u[0])) ? 1u : 0u; break;
                case 183: R[w[2]].u[0] = (!(u2f(R[w[3]].u[0]) == u2f(R[w[4]].u[0]))) ? 1u : 0u; break;
                case 184: R[w[2]].u[0] = (u2f(R[w[3]].u[0]) < u2f(R[w[4]].u[0])) ? 1u : 0u; break;
                case 186: R[w[2]].u[0] = (u2f(R[w[3]].u[0]) > u2f(R[w[4]].u[0])) ? 1u : 0u; break;
                case 188: R[w[2]].u[0] = (u2f(R[w[3]].u[0]) <= u2f(R[w[4]].u[0])) ? 1u : 0u; break;
                case 190: R[w[2]].u[0] = (u2f(R[w[3]].u[0]) >= u2f(R[w[4]].u[0])) ? 1u : 0u; break;

                /* GLSL.std.450 Extended Instructions */
                case 12: {
                    if (w[3] != glsl_ext_id) break;
                    const uint32_t ext_op = w[4];
                    switch (ext_op) {
                        case 2:  R[w[2]].u[0] = f2u(std::nearbyint(u2f(R[w[5]].u[0]))); break;
                        case 3:  R[w[2]].u[0] = f2u(std::trunc(u2f(R[w[5]].u[0]))); break;
                        case 4:  R[w[2]].u[0] = f2u(std::fabs(u2f(R[w[5]].u[0]))); break;
                        case 8:  R[w[2]].u[0] = f2u(std::floor(u2f(R[w[5]].u[0]))); break;
                        case 9:  R[w[2]].u[0] = f2u(std::ceil(u2f(R[w[5]].u[0]))); break;
                        case 10: {
                            float x = u2f(R[w[5]].u[0]);
                            R[w[2]].u[0] = f2u(x - std::floor(x));
                            break;
                        }
                        case 13: R[w[2]].u[0] = f2u(std::sin(u2f(R[w[5]].u[0]))); break;
                        case 14: R[w[2]].u[0] = f2u(std::cos(u2f(R[w[5]].u[0]))); break;
                        case 28: R[w[2]].u[0] = f2u(std::exp2(u2f(R[w[5]].u[0]))); break;
                        case 30: R[w[2]].u[0] = f2u(std::log2(u2f(R[w[5]].u[0]))); break;
                        case 31: R[w[2]].u[0] = f2u(std::sqrt(u2f(R[w[5]].u[0]))); break;
                        case 32: R[w[2]].u[0] = f2u(1.0f / std::sqrt(u2f(R[w[5]].u[0]))); break;
                        case 37: /* FMin */
                        case 79: /* NMin */
                            if (R[w[5]].is_f16_vec) {
                                VmVal v{}; v.is_f16_vec = true; v.width = 2;
                                for (int k = 0; k < 2; ++k) {
                                    v.u[k] = f32_to_f16(rdna_nmin(f16_to_f32(static_cast<uint16_t>(R[w[5]].u[k])),
                                                                  f16_to_f32(static_cast<uint16_t>(R[w[6]].u[k]))));
                                }
                                R[w[2]] = v;
                            } else {
                                R[w[2]].u[0] = f2u(rdna_nmin(u2f(R[w[5]].u[0]), u2f(R[w[6]].u[0])));
                            }
                            break;
                        case 40: /* FMax */
                        case 80: /* NMax */
                            if (R[w[5]].is_f16_vec) {
                                VmVal v{}; v.is_f16_vec = true; v.width = 2;
                                for (int k = 0; k < 2; ++k) {
                                    v.u[k] = f32_to_f16(rdna_nmax(f16_to_f32(static_cast<uint16_t>(R[w[5]].u[k])),
                                                                  f16_to_f32(static_cast<uint16_t>(R[w[6]].u[k]))));
                                }
                                R[w[2]] = v;
                            } else {
                                R[w[2]].u[0] = f2u(rdna_nmax(u2f(R[w[5]].u[0]), u2f(R[w[6]].u[0])));
                            }
                            break;
                        case 38: R[w[2]].u[0] = std::min(R[w[5]].u[0], R[w[6]].u[0]); break;
                        case 41: R[w[2]].u[0] = std::max(R[w[5]].u[0], R[w[6]].u[0]); break;
                        case 39: R[w[2]].u[0] = static_cast<uint32_t>(std::min(static_cast<int32_t>(R[w[5]].u[0]), static_cast<int32_t>(R[w[6]].u[0]))); break;
                        case 42: R[w[2]].u[0] = static_cast<uint32_t>(std::max(static_cast<int32_t>(R[w[5]].u[0]), static_cast<int32_t>(R[w[6]].u[0]))); break;
                        case 43: { /* FClamp */
                            float x = u2f(R[w[5]].u[0]);
                            float lo = u2f(R[w[6]].u[0]);
                            float hi = u2f(R[w[7]].u[0]);
                            R[w[2]].u[0] = f2u(rdna_nmin(rdna_nmax(x, lo), hi));
                            break;
                        }
                        case 50: /* Fma */
                            if (R[w[5]].is_f16_vec) {
                                VmVal v{}; v.is_f16_vec = true; v.width = 2;
                                for (int k = 0; k < 2; ++k) {
                                    float r = std::fma(f16_to_f32(static_cast<uint16_t>(R[w[5]].u[k])),
                                                       f16_to_f32(static_cast<uint16_t>(R[w[6]].u[k])),
                                                       f16_to_f32(static_cast<uint16_t>(R[w[7]].u[k])));
                                    v.u[k] = f32_to_f16(flush_if_ftz(r, f32_ftz));
                                }
                                R[w[2]] = v;
                            } else {
                                float a = flush_if_ftz(u2f(R[w[5]].u[0]), f32_ftz);
                                float b = flush_if_ftz(u2f(R[w[6]].u[0]), f32_ftz);
                                float c = flush_if_ftz(u2f(R[w[7]].u[0]), f32_ftz);
                                R[w[2]].u[0] = f2u(flush_if_ftz(std::fma(a, b, c), f32_ftz));
                            }
                            break;
                        case 58: { /* PackHalf2x16 */
                            uint16_t lo = f32_to_f16(u2f(R[w[5]].u[0]));
                            uint16_t hi = f32_to_f16(u2f(R[w[5]].u[1]));
                            R[w[2]].u[0] = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16u);
                            break;
                        }
                        case 62: { /* UnpackHalf2x16 */
                            uint32_t pk = R[w[5]].u[0];
                            VmVal v{}; v.width = 2;
                            v.u[0] = f2u(f16_to_f32(static_cast<uint16_t>(pk & 0xFFFFu)));
                            v.u[1] = f2u(f16_to_f32(static_cast<uint16_t>(pk >> 16u)));
                            R[w[2]] = v;
                            break;
                        }
                        case 73: { /* FindILsb */
                            uint32_t x = R[w[5]].u[0];
                            R[w[2]].u[0] = x ? static_cast<uint32_t>(__builtin_ctz(x)) : 0xFFFFFFFFu;
                            break;
                        }
                        case 75: { /* FindUMsb */
                            uint32_t x = R[w[5]].u[0];
                            R[w[2]].u[0] = x ? static_cast<uint32_t>(31 - __builtin_clz(x)) : 0xFFFFFFFFu;
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }

                /* RayQueryKHR instructions */
                case 4473: { /* OpRayQueryInitializeKHR */
                    RayQueryState rq{};
                    rq.orig[0] = u2f(R[w[5]].u[0]);
                    rq.orig[1] = u2f(R[w[5]].u[1]);
                    rq.orig[2] = u2f(R[w[5]].u[2]);
                    rq.tmin = u2f(R[w[6]].u[0]);
                    rq.dir[0] = u2f(R[w[7]].u[0]);
                    rq.dir[1] = u2f(R[w[7]].u[1]);
                    rq.dir[2] = u2f(R[w[7]].u[2]);
                    rq.tmax = u2f(R[w[8]].u[0]);
                    rq.comm_t = rq.tmax;
                    ls.ray_queries[w[1]] = rq;
                    break;
                }
                case 4477: { /* OpRayQueryProceedKHR */
                    auto& rq = ls.ray_queries[w[3]];
                    rq.has_candidate = false;
                    while (rq.next_tri < input.rt_triangles.size()) {
                        float hit_t = 0.0f;
                        const auto& tri = input.rt_triangles[rq.next_tri++];
                        if (intersect_tri_mt(rq.orig, rq.dir, tri, rq.tmin, rq.comm_t, hit_t)) {
                            rq.has_candidate = true;
                            rq.cand_t = hit_t;
                            rq.comm_prim = tri.prim_id;
                            rq.comm_inst = tri.inst_id;
                            break;
                        }
                    }
                    R[w[2]].u[0] = rq.has_candidate ? 1u : 0u;
                    break;
                }
                case 4479: { /* OpRayQueryGetIntersectionTypeKHR */
                    const auto& rq = ls.ray_queries[w[3]];
                    const uint32_t is_comm = R[w[4]].u[0];
                    if (is_comm == 0u) R[w[2]].u[0] = rq.has_candidate ? 0u : 1u;
                    else R[w[2]].u[0] = rq.has_committed ? 1u : 0u;
                    break;
                }
                case 4476: { /* OpRayQueryConfirmIntersectionKHR */
                    auto& rq = ls.ray_queries[w[1]];
                    if (rq.has_candidate) {
                        rq.has_committed = true;
                        rq.comm_t = rq.cand_t;
                    }
                    break;
                }
                case 6018: { /* OpRayQueryGetIntersectionTKHR */
                    const auto& rq = ls.ray_queries[w[3]];
                    R[w[2]].u[0] = f2u(rq.comm_t);
                    break;
                }

                default:
                    break;
            }

            if (!jumped) {
                ++ls.pc;
            }
        }
    }

    out.output_buffer.assign(out_mem.begin(),
                             out_mem.begin() + std::min<size_t>(input.output_word_count, out_mem.size()));
    out.ok = true;
    return out;
}

} // namespace scarlett::rdna2
