#ifndef SCARLETT_DSTORAGE_XS_SHADERS_HPP
#define SCARLETT_DSTORAGE_XS_SHADERS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "dstorage_xs_unix_abi.h"

namespace scarlett::dstorage_xs::unixlib {

struct VulkanDecompProbeResult {
    bool vulkan_available = false;
    bool vk_ext_memory_decompression = false;
    bool vkdeflate_spirv_valid = false;
    bool swizzle_spirv_valid = false;
    uint32_t gpu_vendor_id = 0;
    uint32_t gpu_device_id = 0;
    std::string device_name;
    std::string diagnostics;
};

/* Probes libvulkan.so.1 dynamically for VK_EXT_memory_decompression (NVIDIA) vs vkdeflate fallback */
VulkanDecompProbeResult probe_vulkan_decompression();

/* Generates GLSL 4.60 compute shader for vkdeflate:
 * - 32 threads per 64 KiB tile (local_size_x = 32)
 * - 512-byte ring mirror in shared memory
 * - 9-bit literal lookup table sized for >=10 waves/SIMD
 * - Tile descriptors sorted by compressed size
 */
std::string generate_vkdeflate_compute_glsl();

/* Generates GLSL 4.60 compute shader for Xbox Velocity Compute-Swizzle:
 * - 4K_4K_8K (1), 4K_4K_24K (2), 4K_12K_4K_4K_8K (3), 4K_12K_4K_12K (4)
 */
std::string generate_swizzle_compute_glsl();

/* Compiles GLSL compute shader with glslangValidator -V --target-env vulkan1.4
 * and validates SPIR-V with spirv-val --target-env vulkan1.4 */
bool compile_and_validate_spirv_v14(
    const std::string &glsl_source,
    const char *tag,
    std::vector<uint32_t> &out_spirv,
    std::string *out_diagnostics = nullptr);

/* Reversible CPU Compute-Swizzle reference matching the GLSL compute shader 1:1 */
scarlett_hresult_t apply_swizzle_cpu(
    uint32_t swizzle_mode,
    bool inverse,
    const uint8_t *src,
    uint8_t *dst,
    size_t byte_size) noexcept;

} // namespace scarlett::dstorage_xs::unixlib

#endif /* SCARLETT_DSTORAGE_XS_SHADERS_HPP */
