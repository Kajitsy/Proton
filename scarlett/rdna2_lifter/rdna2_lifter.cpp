/*
 * Proton-Scarlett — Top-Level RDNA2 -> SPIR-V 1.5/1.6 Lifter Orchestrator & C ABI Bridge
 */

#include "rdna2_lifter.hpp"
#include "rdna2_ir.hpp"
#include "rdna2_spirv_emitter.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace scarlett::rdna2 {

scarlett_shader_mode_t parse_shader_mode_env(const char* override_value) noexcept {
    const char* val = override_value ? override_value : std::getenv(SCARLETT_ENV_SHADER_MODE);
    if (val == nullptr || val[0] == '\0') {
        return SCARLETT_SHADER_MODE_ADAPTIVE;
    }
    if (std::strcmp(val, "exact") == 0) {
        return SCARLETT_SHADER_MODE_EXACT;
    }
    if (std::strcmp(val, "fast_f32") == 0) {
        return SCARLETT_SHADER_MODE_FAST_F32;
    }
    return SCARLETT_SHADER_MODE_ADAPTIVE;
}

LiftResult Rdna2Lifter::lift_container(const uint8_t* data,
                                       size_t size,
                                       const LiftOptions& opts) const noexcept {
    const auto t0 = std::chrono::steady_clock::now();
    LiftResult res{};

    const ContainerKind kind = detect_container_kind(data, size);
    res.container_kind = kind;

    /* Path A: DXBC / DXIL Fast-Path (strip XBOX / XS2M -> clean DXIL for dxil-spirv) */
    if (kind == ContainerKind::DxbcDxil) {
        res.used_dxil_fast_path = true;
        res.dxil_fast_path = extract_dxil_fast_path(data, size);
        res.status = res.dxil_fast_path.status;
        res.metadata.container_kind = kind;
        res.metadata.stage = res.dxil_fast_path.stage;
        res.metadata.bindings = res.dxil_fast_path.remapped_bindings;
        if (res.status == SCARLETT_OK) {
            res.pipeline_cache_key = compute_pipeline_cache_key(
                opts.gpu_profile, opts.mode, WaveStrategy::NarrowedWave32, RtStrategy::None,
                res.dxil_fast_path.clean_dxbc.data(), res.dxil_fast_path.clean_dxbc.size());
        } else {
            res.diagnostics = "Path A DXIL container invalid or missing DXIL/ILDB chunk";
        }
        const auto t1 = std::chrono::steady_clock::now();
        res.lift_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return res;
    }

    /* Path B: Native Xbox RDNA2 containers (XS2 / FSSM / PS\x00\x09 / SGD5) */
    if (kind == ContainerKind::Unknown) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        res.diagnostics = "Unrecognized shader container magic";
        return res;
    }

    UnpackedRdna2Container unpacked = unpack_rdna2_container(data, size);
    if (unpacked.status != SCARLETT_OK) {
        res.status = unpacked.status;
        res.diagnostics = "Failed to unpack Path B RDNA2 container";
        return res;
    }

    res = lift_isa(unpacked.meta, unpacked.isa_words.data(), unpacked.isa_words.size(), opts);
    res.container_kind = kind;
    if (res.status == SCARLETT_OK) {
        res.pipeline_cache_key = compute_pipeline_cache_key(
            opts.gpu_profile, opts.mode, res.wave_stats.chosen_strategy, res.rt_strategy,
            data, size);
    }
    const auto t1 = std::chrono::steady_clock::now();
    res.lift_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

LiftResult Rdna2Lifter::lift_isa(const Rdna2ShaderMetadata& meta,
                                 const uint32_t* isa_words,
                                 size_t word_count,
                                 const LiftOptions& opts) const noexcept {
    const auto t0 = std::chrono::steady_clock::now();
    LiftResult res{};
    res.metadata = meta;
    res.container_kind = meta.container_kind;

    /* 0. Skip Xbox Scarlett 36-byte pre-header (d580.... .. s_inst_prefetch 0x3) if present */
    if (isa_words != nullptr && word_count > 10u &&
        ((isa_words[0] >> 16u) == 0xD580u) &&
        isa_words[6] == 0xD5800000u &&
        isa_words[9] == 0xBFA00003u) {
        isa_words += 9u;
        word_count -= 9u;
        /* Trim trailing s_code_end (0xBF9F0000) + Scarlett footer after s_endpgm (0xBF810000) */
        for (size_t k = 0u; k + 2u < word_count; ++k) {
            if (isa_words[k] == 0xBF810000u &&
                ((isa_words[k + 1u] == 0xBF9F0000u && isa_words[k + 2u] == 0xBF9F0000u) ||
                 (isa_words[k + 1u] == 0x00000000u && isa_words[k + 2u] == 0x00000000u))) {
                word_count = k + 1u;
                break;
            }
        }
    }

    /* 1. Decode GFX10.3 binary instruction stream */
    std::vector<DecodedInst> decoded;
    uint32_t err_pc = 0u;
    if (!decode_gfx103_stream(isa_words, word_count, decoded, err_pc)) {
        res.status = SCARLETT_ERR_SHADER_UNSUPPORTED_ISA;
        std::string ctx;
        const size_t start = (err_pc > 6u) ? (err_pc - 6u) : 0u;
        const size_t end = std::min(word_count, static_cast<size_t>(err_pc) + 3u);
        for (size_t k = start; k < end; ++k) {
            char wb[48];
            std::snprintf(wb, sizeof(wb), "%s%zu:%08x", (k == start ? "" : " "), k, isa_words[k]);
            ctx += wb;
        }
        res.diagnostics = "GFX10.3 ISA decode error at word " + std::to_string(err_pc) +
                          "/" + std::to_string(word_count) + " [" + ctx + "]";
        return res;
    }
    res.decoded_instruction_count = static_cast<uint32_t>(decoded.size());

    /* 2. Build SSA IR & structurize CFG */
    IrModule ir = build_ssa_ir_and_structurize(meta, decoded);
    res.cfg_block_count = static_cast<uint32_t>(ir.cfg_blocks.size());
    res.cfg_loop_count = ir.loop_count;
    res.cfg_branch_count = ir.branch_count;

    /* 3. Stage 2: computeKnownFPClass (forward + backward dataflow) */
    run_compute_known_fp_class(ir, opts);
    res.fp_stats = ir.fp_stats;

    /* 4. Stage 3: Adaptive Wave64 cross-half-wave dependency analysis */
    run_wave64_dependency_analysis(ir, opts);
    res.wave_stats = ir.wave_stats;

    /* 5. Stage 4: BVH4 RT strategy selection (RayQueryKHR vs BDA fallback) */
    run_rt_bvh4_strategy_selection(ir, opts);
    res.rt_strategy = ir.rt_strategy;

    /* 6. Emit SPIR-V 1.5/1.6 binary */
    SpirvEmitOutput emitted = emit_spirv_module(ir, opts);
    res.spirv_words = std::move(emitted.words);
    res.emitted_maximal_reconvergence = emitted.emitted_maximal_reconvergence;
    res.emitted_quad_control = emitted.emitted_quad_control;

    /* 7. Optional in-process SPIRV-Tools Vulkan 1.4 validation */
    if (opts.validate_with_spirv_tools) {
        std::string val_err;
        if (!validate_spirv_vulkan14(res.spirv_words, &val_err)) {
            res.status = SCARLETT_ERR_SHADER_SPIRV_INVALID;
            res.diagnostics = "spirv-val Vulkan 1.4 validation failed: " + val_err;
            return res;
        }
    }

    res.pipeline_cache_key = compute_pipeline_cache_key(
        opts.gpu_profile, opts.mode, res.wave_stats.chosen_strategy, res.rt_strategy,
        reinterpret_cast<const uint8_t*>(isa_words), word_count * sizeof(uint32_t));

    res.status = SCARLETT_OK;
    const auto t1 = std::chrono::steady_clock::now();
    res.lift_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return res;
}

} // namespace scarlett::rdna2

/* ============================================================================
 * C ABI Bridge for vkd3d-proton/subprojects/dxil-spirv/rdna2-lifter/
 * ============================================================================ */

extern "C" scarlett_hresult_t scarlett_rdna2_lift_shader_c(
    const uint8_t* container_bytes,
    size_t container_size,
    scarlett_shader_mode_t mode,
    uint32_t prefer_ray_query,
    uint32_t* out_words_buf,
    size_t out_words_capacity,
    scarlett_rdna2_lift_c_result_t* out_result) {
    if (container_bytes == nullptr || container_size == 0u || out_result == nullptr) {
        return SCARLETT_E_INVALIDARG;
    }

    std::memset(out_result, 0, sizeof(*out_result));

    scarlett::rdna2::LiftOptions opts{};
    opts.mode = mode;
    opts.prefer_ray_query = (prefer_ray_query != 0u);
    opts.gpu_profile = scarlett::rdna2::make_amd_radv_profile(opts.prefer_ray_query);

    scarlett::rdna2::Rdna2Lifter lifter;
    auto res = lifter.lift_container(container_bytes, container_size, opts);
    out_result->status = res.status;
    out_result->used_dxil_fast_path = res.used_dxil_fast_path ? 1u : 0u;
    out_result->wave_strategy = static_cast<uint32_t>(res.wave_stats.chosen_strategy);
    out_result->rt_strategy = static_cast<uint32_t>(res.rt_strategy);
    std::memcpy(out_result->cache_key_sha256, res.pipeline_cache_key.bytes.data(), 32u);

    if (res.status != SCARLETT_OK) {
        return res.status;
    }

    if (res.used_dxil_fast_path) {
        out_result->byte_count = static_cast<uint32_t>(res.dxil_fast_path.clean_dxbc.size());
        out_result->word_count = (out_result->byte_count + 3u) / 4u;
        if (out_words_buf != nullptr) {
            if (out_words_capacity * 4u < res.dxil_fast_path.clean_dxbc.size()) {
                return SCARLETT_E_INVALIDARG;
            }
            std::memcpy(out_words_buf, res.dxil_fast_path.clean_dxbc.data(),
                        res.dxil_fast_path.clean_dxbc.size());
        }
        return SCARLETT_OK;
    }

    out_result->word_count = static_cast<uint32_t>(res.spirv_words.size());
    out_result->byte_count = out_result->word_count * 4u;
    if (out_words_buf != nullptr) {
        if (out_words_capacity < res.spirv_words.size()) {
            return SCARLETT_E_INVALIDARG;
        }
        std::memcpy(out_words_buf, res.spirv_words.data(), res.spirv_words.size() * sizeof(uint32_t));
    }
    return SCARLETT_OK;
}
