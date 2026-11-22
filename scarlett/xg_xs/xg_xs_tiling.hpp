#ifndef SCARLETT_XG_XS_TILING_HPP
#define SCARLETT_XG_XS_TILING_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "xg_xs_unix_abi.h"

namespace scarlett::xg_xs {

struct FormatMetrics {
    uint32_t bytes_per_element;
    uint32_t bpe_log2;
    uint32_t block_width;
    uint32_t block_height;
    bool is_block_compressed;
    bool valid;
};

FormatMetrics query_format_metrics(uint32_t xg_format) noexcept;

bool is_valid_swizzle_mode(uint32_t swizzle_mode) noexcept;

uint32_t compute_optimal_swizzle_mode(
    uint32_t resource_type,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t depth_or_array,
    uint32_t sample_count,
    uint32_t bind_flags) noexcept;

scarlett_hresult_t select_transfer_path(
    const XG_GPU_CAPS_PROBE& caps,
    uint32_t swizzle_mode,
    uint32_t format,
    XG_TRANSFER_PATH_DECISION& out_decision) noexcept;

/* Generates GLSL 4.60 compute shader for 8..128bpp, MSAA, 40-bit swizzle/deswizzle */
std::string generate_deswizzle_compute_glsl();

/* Compiles & validates GLSL compute shader via glslangValidator + spirv-val (--target-env vulkan1.4) */
bool compile_and_validate_deswizzle_spirv(
    std::vector<uint32_t>& out_spirv,
    std::string* out_diagnostics = nullptr);

class TextureAddressComputer {
public:
    scarlett_hresult_t initialize(const XG_TEXTURE2D_DESC& desc) noexcept;

    scarlett_hresult_t get_resource_layout(XG_RESOURCE_LAYOUT& out_layout) const noexcept;
    scarlett_hresult_t get_subresource_layout(
        uint32_t subresource,
        XG_SUBRESOURCE_LAYOUT& out_layout) const noexcept;

    uint64_t get_texel_element_offset_bytes(
        uint32_t plane,
        uint32_t mip_level,
        uint64_t x,
        uint64_t y,
        uint32_t z_or_slice,
        uint32_t sample) const noexcept;

    scarlett_hresult_t copy_into_subresource(
        void* dst_swizzled_resource,
        uint32_t linear_row_pitch_bytes,
        uint32_t subresource,
        const void* src_linear_buffer) const noexcept;

    scarlett_hresult_t copy_from_subresource(
        void* dst_linear_buffer,
        uint32_t linear_row_pitch_bytes,
        uint32_t subresource,
        const void* src_swizzled_resource) const noexcept;

    const XG_TEXTURE2D_DESC& desc() const noexcept { return desc_; }
    const FormatMetrics& metrics() const noexcept { return fmt_; }

private:
    struct MipLevelLayout {
        uint64_t offset_bytes;
        uint64_t total_size_bytes;
        uint64_t slice_stride_bytes;
        uint64_t linear_row_pitch_bytes;
        uint32_t width_elements;
        uint32_t height_elements;
        uint32_t tile_width_elements;
        uint32_t tile_height_elements;
        uint32_t tiles_per_row;
        uint32_t tiles_per_col;
        uint32_t x_bits;
        uint32_t y_bits;
        uint32_t coord_bits;
        uint32_t tile_log2;
    };

    uint64_t compute_intra_tile_element(
        const MipLevelLayout& mip,
        uint32_t x_local,
        uint32_t y_local,
        uint32_t sample,
        uint64_t tile_x,
        uint64_t tile_y) const noexcept;

    XG_TEXTURE2D_DESC desc_{};
    FormatMetrics fmt_{};
    uint32_t sample_count_{1};
    uint32_t sample_log2_{0};
    uint32_t mip_levels_{1};
    uint32_t array_size_{1};
    uint64_t total_size_bytes_{0};
    uint64_t base_alignment_bytes_{65536};
    std::vector<MipLevelLayout> mips_;
};

class TextureComputerRegistry {
public:
    static TextureComputerRegistry& instance();

    scarlett_hresult_t create_computer(
        const XG_TEXTURE2D_DESC& desc,
        uint64_t& out_handle);

    scarlett_hresult_t destroy_computer(uint64_t handle);

    scarlett_hresult_t get_resource_layout(
        uint64_t handle,
        XG_RESOURCE_LAYOUT& out_layout);

    scarlett_hresult_t get_subresource_layout(
        uint64_t handle,
        uint32_t subresource,
        XG_SUBRESOURCE_LAYOUT& out_layout);

    scarlett_hresult_t get_texel_offset(
        uint64_t handle,
        uint32_t plane,
        uint32_t mip_level,
        uint64_t x,
        uint64_t y,
        uint32_t z_or_slice,
        uint32_t sample,
        uint64_t& out_offset);

    scarlett_hresult_t copy_into_subresource(
        uint64_t handle,
        void* dst,
        uint32_t pitch,
        uint32_t subresource,
        const void* src);

    scarlett_hresult_t copy_from_subresource(
        uint64_t handle,
        void* dst,
        uint32_t pitch,
        uint32_t subresource,
        const void* src);
};

} // namespace scarlett::xg_xs

#endif // SCARLETT_XG_XS_TILING_HPP
