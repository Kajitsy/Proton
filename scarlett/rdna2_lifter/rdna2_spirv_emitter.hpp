/*
 * Proton-Scarlett — SPIR-V 1.5/1.6 Binary Emitter for RDNA2 Lifter
 */

#ifndef SCARLETT_RDNA2_SPIRV_EMITTER_HPP
#define SCARLETT_RDNA2_SPIRV_EMITTER_HPP

#include <cstdint>
#include <vector>

#include "rdna2_ir.hpp"

namespace scarlett::rdna2 {

struct SpirvEmitOutput {
    std::vector<uint32_t> words;
    bool emitted_maximal_reconvergence{false};
    bool emitted_quad_control{false};
};

/* Emit a complete, Vulkan 1.4 compliant SPIR-V 1.5/1.6 binary from analyzed SSA IR */
SpirvEmitOutput emit_spirv_module(const IrModule& mod, const LiftOptions& opts);

} // namespace scarlett::rdna2

#endif // SCARLETT_RDNA2_SPIRV_EMITTER_HPP
