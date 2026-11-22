#include "xg_xs_tiling.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unistd.h>
#include <unordered_map>

namespace scarlett::xg_xs {

namespace {

uint32_t ceil_div_u32(uint32_t a, uint32_t b) noexcept {
    return (a + b - 1U) / b;
}

uint64_t align_up_u64(uint64_t value, uint64_t alignment) noexcept {
    if (alignment <= 1ULL) {
        return value;
    }
    const uint64_t rem = value % alignment;
    return (rem == 0ULL) ? value : (value + (alignment - rem));
}

bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    __uint128_t prod = static_cast<__uint128_t>(a) * static_cast<__uint128_t>(b);
    if (prod > static_cast<__uint128_t>(UINT64_MAX)) {
        return false;
    }
    out = static_cast<uint64_t>(prod);
    return true;
}

bool checked_add_u64(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    if (a > UINT64_MAX - b) {
        return false;
    }
    out = a + b;
    return true;
}

} // namespace

FormatMetrics query_format_metrics(uint32_t xg_format) noexcept {
    switch (xg_format) {
        // 8 bpp (1 byte/texel)
        case XG_FORMAT_R8_UNORM:
        case XG_FORMAT_R8_UINT:
            return {1u, 0u, 1u, 1u, false, true};

        // 16 bpp (2 bytes/texel)
        case XG_FORMAT_R8G8_UNORM:
        case XG_FORMAT_R16_FLOAT:
        case XG_FORMAT_D16_UNORM:
        case XG_FORMAT_R16_UNORM:
        case XG_FORMAT_R16_UINT:
            return {2u, 1u, 1u, 1u, false, true};

        // 32 bpp (4 bytes/texel)
        case XG_FORMAT_R10G10B10A2_UNORM:
        case XG_FORMAT_R8G8B8A8_UNORM:
        case XG_FORMAT_R8G8B8A8_UNORM_SRGB:
        case XG_FORMAT_R8G8B8A8_UINT:
        case XG_FORMAT_B8G8R8A8_UNORM:
        case XG_FORMAT_R16G16_FLOAT:
        case XG_FORMAT_D32_FLOAT:
        case XG_FORMAT_R32_FLOAT:
        case XG_FORMAT_R32_UINT:
        case XG_FORMAT_D24_UNORM_S8_UINT:
            return {4u, 2u, 1u, 1u, false, true};

        // 64 bpp (8 bytes/texel)
        case XG_FORMAT_R16G16B16A16_FLOAT:
        case XG_FORMAT_R16G16B16A16_UNORM:
        case XG_FORMAT_R32G32_FLOAT:
        case XG_FORMAT_R32G32_UINT:
            return {8u, 3u, 1u, 1u, false, true};

        // 128 bpp (16 bytes/texel)
        case XG_FORMAT_R32G32B32A32_FLOAT:
        case XG_FORMAT_R32G32B32A32_UINT:
            return {16u, 4u, 1u, 1u, false, true};

        // BC1 / BC4: 4x4 block = 8 bytes (64 bpe)
        case XG_FORMAT_BC1_UNORM:
        case XG_FORMAT_BC1_UNORM_SRGB:
        case XG_FORMAT_BC4_UNORM:
        case XG_FORMAT_BC4_SNORM:
            return {8u, 3u, 4u, 4u, true, true};

        // BC2 / BC3 / BC5 / BC6H / BC7: 4x4 block = 16 bytes (128 bpe)
        case XG_FORMAT_BC2_UNORM:
        case XG_FORMAT_BC2_UNORM_SRGB:
        case XG_FORMAT_BC3_UNORM:
        case XG_FORMAT_BC3_UNORM_SRGB:
        case XG_FORMAT_BC5_UNORM:
        case XG_FORMAT_BC5_SNORM:
        case XG_FORMAT_BC6H_UF16:
        case XG_FORMAT_BC6H_SF16:
        case XG_FORMAT_BC7_UNORM:
        case XG_FORMAT_BC7_UNORM_SRGB:
            return {16u, 4u, 4u, 4u, true, true};

        default:
            return {0u, 0u, 0u, 0u, false, false};
    }
}

bool is_valid_swizzle_mode(uint32_t swizzle_mode) noexcept {
    return swizzle_mode <= static_cast<uint32_t>(XG_SWIZZLE_64KB_S_X);
}

uint32_t compute_optimal_swizzle_mode(
    uint32_t resource_type,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t depth_or_array,
    uint32_t sample_count,
    uint32_t bind_flags) noexcept {
    (void)resource_type;
    const FormatMetrics fmt = query_format_metrics(format);
    const uint32_t bpe = fmt.valid ? fmt.bytes_per_element : 4u;
    const uint64_t approx_bytes = static_cast<uint64_t>(std::max(1u, width)) *
                                  static_cast<uint64_t>(std::max(1u, height)) *
                                  static_cast<uint64_t>(std::max(1u, depth_or_array)) *
                                  static_cast<uint64_t>(bpe);

    // MSAA render targets prefer 64KB_S (Standard) or 64KB_R_X
    if (sample_count > 1u) {
        return static_cast<uint32_t>(XG_SWIZZLE_64KB_S);
    }
    // Render target / Depth-stencil (bind_flags bit 0x20 = RT, 0x40 = DS)
    if ((bind_flags & 0x60u) != 0u) {
        return (approx_bytes >= 65536ULL)
                   ? static_cast<uint32_t>(XG_SWIZZLE_64KB_R_X)
                   : static_cast<uint32_t>(XG_SWIZZLE_4KB_D);
    }
    // Small textures fit cleanly in 4KB_D
    if (approx_bytes <= 16384ULL) {
        return static_cast<uint32_t>(XG_SWIZZLE_4KB_D);
    }
    // Displayable / SRV / BC textures default to 64KB_D (matches AMD_GFX10_64KB_D zero-copy)
    return static_cast<uint32_t>(XG_SWIZZLE_64KB_D);
}

scarlett_hresult_t select_transfer_path(
    const XG_GPU_CAPS_PROBE& caps,
    uint32_t swizzle_mode,
    uint32_t format,
    XG_TRANSFER_PATH_DECISION& out_decision) noexcept {
    if (!is_valid_swizzle_mode(swizzle_mode)) {
        return SCARLETT_ERR_XG_INVALID_SWIZZLE_MODE;
    }
    const FormatMetrics fmt = query_format_metrics(format);
    if (!fmt.valid) {
        return SCARLETT_E_INVALIDARG;
    }

    std::memset(&out_decision, 0, sizeof(out_decision));
    out_decision.drm_modifier = SCARLETT_DRM_FORMAT_MOD_LINEAR;

    if (swizzle_mode == XG_SWIZZLE_LINEAR) {
        out_decision.path = XG_TRANSFER_PATH_RADV_ZERO_COPY;
        out_decision.zero_copy_active = 1u;
        return SCARLETT_OK;
    }

    const bool is_amd_radv = (caps.vendor_id == XG_GPU_VENDOR_AMD) &&
                             (caps.driver_id == XG_VK_DRIVER_MESA_RADV);
    if (is_amd_radv && caps.has_drm_format_modifier != 0u) {
        uint64_t mod = 0u;
        if (swizzle_mode == XG_SWIZZLE_64KB_D && (caps.drm_modifier_mask & 1ULL) != 0u) {
            mod = SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_D;
        } else if (swizzle_mode == XG_SWIZZLE_64KB_S && (caps.drm_modifier_mask & 2ULL) != 0u) {
            mod = SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_S;
        } else if (swizzle_mode == XG_SWIZZLE_64KB_R_X && (caps.drm_modifier_mask & 4ULL) != 0u) {
            mod = SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_R_X;
        }
        if (mod != 0u) {
            out_decision.path = XG_TRANSFER_PATH_RADV_ZERO_COPY;
            out_decision.zero_copy_active = 1u;
            out_decision.drm_modifier = mod;
            return SCARLETT_OK;
        }
    }

    if (caps.has_vulkan_compute != 0u) {
        out_decision.path = XG_TRANSFER_PATH_VULKAN_COMPUTE_DESWIZZLE;
        out_decision.zero_copy_active = 0u;
        return SCARLETT_OK;
    }

    out_decision.path = XG_TRANSFER_PATH_CPU_FALLBACK;
    out_decision.zero_copy_active = 0u;
    return SCARLETT_OK;
}

scarlett_hresult_t TextureAddressComputer::initialize(const XG_TEXTURE2D_DESC& desc) noexcept {
    if (desc.Width == 0u || desc.Height == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    if (!is_valid_swizzle_mode(desc.SwizzleMode)) {
        return SCARLETT_ERR_XG_INVALID_SWIZZLE_MODE;
    }
    fmt_ = query_format_metrics(desc.Format);
    if (!fmt_.valid) {
        return SCARLETT_E_INVALIDARG;
    }

    const uint32_t samples = (desc.SampleCount == 0u) ? 1u : desc.SampleCount;
    switch (samples) {
        case 1u: sample_log2_ = 0u; break;
        case 2u: sample_log2_ = 1u; break;
        case 4u: sample_log2_ = 2u; break;
        case 8u: sample_log2_ = 3u; break;
        default: return SCARLETT_E_INVALIDARG;
    }
    sample_count_ = samples;
    array_size_ = (desc.ArraySize == 0u) ? 1u : desc.ArraySize;

    const uint32_t max_dim = std::max(desc.Width, desc.Height);
    uint32_t full_mips = 1u;
    for (uint32_t d = max_dim; d > 1u; d >>= 1u) {
        ++full_mips;
    }
    mip_levels_ = (desc.MipLevels == 0u) ? full_mips : std::min(desc.MipLevels, full_mips);

    desc_ = desc;
    desc_.SampleCount = sample_count_;
    desc_.ArraySize = array_size_;
    desc_.MipLevels = mip_levels_;

    const bool is_linear = (desc_.SwizzleMode == XG_SWIZZLE_LINEAR);
    const bool is_4kb = (desc_.SwizzleMode == XG_SWIZZLE_4KB_D ||
                         desc_.SwizzleMode == XG_SWIZZLE_4KB_S);
    const uint32_t tile_log2 = is_linear ? 0u : (is_4kb ? 12u : 16u);
    base_alignment_bytes_ = is_linear ? std::max<uint64_t>(256ULL, desc_.PitchAlignment)
                                      : (UINT64_C(1) << tile_log2);

    mips_.clear();
    mips_.reserve(mip_levels_);

    uint64_t running_offset = 0ULL;

    for (uint32_t m = 0u; m < mip_levels_; ++m) {
        const uint32_t mip_w_tex = std::max(1u, desc_.Width >> m);
        const uint32_t mip_h_tex = std::max(1u, desc_.Height >> m);
        const uint32_t w_elems = fmt_.is_block_compressed
                                     ? ceil_div_u32(mip_w_tex, fmt_.block_width)
                                     : mip_w_tex;
        const uint32_t h_elems = fmt_.is_block_compressed
                                     ? ceil_div_u32(mip_h_tex, fmt_.block_height)
                                     : mip_h_tex;

        MipLevelLayout ml{};
        ml.width_elements = w_elems;
        ml.height_elements = h_elems;
        ml.tile_log2 = tile_log2;

        const uint64_t min_row_bytes = static_cast<uint64_t>(w_elems) * fmt_.bytes_per_element;
        const uint64_t pitch_align = std::max<uint64_t>(1ULL, desc_.PitchAlignment);
        ml.linear_row_pitch_bytes = align_up_u64(min_row_bytes, pitch_align);

        if (is_linear) {
            ml.tile_width_elements = w_elems;
            ml.tile_height_elements = h_elems;
            ml.tiles_per_row = 1u;
            ml.tiles_per_col = 1u;
            ml.x_bits = 0u;
            ml.y_bits = 0u;
            ml.coord_bits = 0u;

            uint64_t slice_bytes = 0ULL;
            if (!checked_mul_u64(ml.linear_row_pitch_bytes, h_elems, slice_bytes) ||
                !checked_mul_u64(slice_bytes, sample_count_, slice_bytes)) {
                return SCARLETT_ERR_XG_SURFACE_OVERFLOW;
            }
            ml.slice_stride_bytes = align_up_u64(slice_bytes, 256ULL);
            running_offset = align_up_u64(running_offset, 256ULL);
            ml.offset_bytes = running_offset;
        } else {
            ml.coord_bits = tile_log2 - fmt_.bpe_log2;
            const uint32_t xy_bits = ml.coord_bits - sample_log2_;
            ml.x_bits = (xy_bits + 1u) / 2u;
            ml.y_bits = xy_bits / 2u;
            ml.tile_width_elements = 1u << ml.x_bits;
            ml.tile_height_elements = 1u << ml.y_bits;
            ml.tiles_per_row = ceil_div_u32(w_elems, ml.tile_width_elements);
            ml.tiles_per_col = ceil_div_u32(h_elems, ml.tile_height_elements);

            const uint64_t tile_bytes = UINT64_C(1) << tile_log2;
            uint64_t total_tiles_per_slice = 0ULL;
            if (!checked_mul_u64(ml.tiles_per_row, ml.tiles_per_col, total_tiles_per_slice) ||
                !checked_mul_u64(total_tiles_per_slice, tile_bytes, ml.slice_stride_bytes)) {
                return SCARLETT_ERR_XG_SURFACE_OVERFLOW;
            }
            running_offset = align_up_u64(running_offset, tile_bytes);
            ml.offset_bytes = running_offset;
        }

        if (!checked_mul_u64(ml.slice_stride_bytes, array_size_, ml.total_size_bytes)) {
            return SCARLETT_ERR_XG_SURFACE_OVERFLOW;
        }
        if (!checked_add_u64(running_offset, ml.total_size_bytes, running_offset)) {
            return SCARLETT_ERR_XG_SURFACE_OVERFLOW;
        }
        if (running_offset > SCARLETT_XG_40BIT_LIMIT_BYTES) {
            return SCARLETT_ERR_XG_SURFACE_OVERFLOW;
        }

        mips_.push_back(ml);
    }

    total_size_bytes_ = align_up_u64(running_offset, base_alignment_bytes_);
    if (total_size_bytes_ > SCARLETT_XG_40BIT_LIMIT_BYTES) {
        return SCARLETT_ERR_XG_SURFACE_OVERFLOW;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t TextureAddressComputer::get_resource_layout(
    XG_RESOURCE_LAYOUT& out_layout) const noexcept {
    out_layout.SizeBytes = total_size_bytes_;
    out_layout.BaseAlignmentBytes = base_alignment_bytes_;
    out_layout.MipLevels = mip_levels_;
    out_layout.Planes = 1u;
    out_layout.SwizzleMode = desc_.SwizzleMode;
    out_layout.Flags = desc_.MiscFlags;
    return SCARLETT_OK;
}

scarlett_hresult_t TextureAddressComputer::get_subresource_layout(
    uint32_t subresource,
    XG_SUBRESOURCE_LAYOUT& out_layout) const noexcept {
    const uint32_t total_subs = mip_levels_ * array_size_;
    if (subresource >= total_subs) {
        return SCARLETT_E_INVALIDARG;
    }
    const uint32_t mip = subresource % mip_levels_;
    const uint32_t slice = subresource / mip_levels_;
    const MipLevelLayout& ml = mips_[mip];

    out_layout.SizeBytes = ml.slice_stride_bytes;
    out_layout.OffsetBytes = ml.offset_bytes + static_cast<uint64_t>(slice) * ml.slice_stride_bytes;
    out_layout.RowPitchBytes = ml.linear_row_pitch_bytes;
    out_layout.DepthPitchBytes = ml.linear_row_pitch_bytes * static_cast<uint64_t>(ml.height_elements);
    out_layout.WidthElements = ml.width_elements;
    out_layout.HeightElements = ml.height_elements;
    out_layout.Depth = 1u;
    out_layout.TileMode = desc_.SwizzleMode;
    return SCARLETT_OK;
}

uint64_t TextureAddressComputer::compute_intra_tile_element(
    const MipLevelLayout& ml,
    uint32_t x_local,
    uint32_t y_local,
    uint32_t sample,
    uint64_t tile_x,
    uint64_t tile_y) const noexcept {
    uint64_t intra = 0ULL;
    uint32_t out_bit = 0u;
    uint32_t xi = 0u;
    uint32_t yi = 0u;
    uint32_t si = 0u;

    const auto emit_x = [&](uint32_t bit_idx) {
        intra |= (static_cast<uint64_t>((x_local >> bit_idx) & 1u) << out_bit++);
    };
    const auto emit_y = [&](uint32_t bit_idx) {
        intra |= (static_cast<uint64_t>((y_local >> bit_idx) & 1u) << out_bit++);
    };
    const auto emit_s = [&](uint32_t bit_idx) {
        intra |= (static_cast<uint64_t>((sample >> bit_idx) & 1u) << out_bit++);
    };

    const uint32_t mode = desc_.SwizzleMode;
    if (mode == XG_SWIZZLE_4KB_S || mode == XG_SWIZZLE_64KB_S || mode == XG_SWIZZLE_64KB_S_X) {
        if (xi < ml.x_bits) emit_x(xi++);
        if (yi < ml.y_bits) emit_y(yi++);
        while (si < sample_log2_) emit_s(si++);
        while (xi < ml.x_bits || yi < ml.y_bits) {
            if (xi < ml.x_bits) emit_x(xi++);
            if (xi < ml.x_bits) emit_x(xi++);
            if (yi < ml.y_bits) emit_y(yi++);
            if (yi < ml.y_bits) emit_y(yi++);
        }
    } else if (mode == XG_SWIZZLE_64KB_R_X) {
        while (xi < ml.x_bits || yi < ml.y_bits) {
            if (yi < ml.y_bits) emit_y(yi++);
            if (xi < ml.x_bits) emit_x(xi++);
        }
        while (si < sample_log2_) emit_s(si++);
    } else {
        // _D modes: 4KB_D, 64KB_D, 64KB_D_X
        while (xi < ml.x_bits || yi < ml.y_bits) {
            if (xi < ml.x_bits) emit_x(xi++);
            if (yi < ml.y_bits) emit_y(yi++);
        }
        while (si < sample_log2_) emit_s(si++);
    }

    // Invertible pipe/bank XOR hash on top 4 bits of intra-tile element index for _X modes
    const uint64_t coord_mask = (UINT64_C(1) << ml.coord_bits) - 1ULL;
    const uint32_t xor_shift = (ml.coord_bits > 4u) ? (ml.coord_bits - 4u) : 0u;
    uint64_t bank_xor = 0ULL;
    if (mode == XG_SWIZZLE_64KB_D_X) {
        bank_xor = ((tile_x ^ tile_y) & 0xFULL) << xor_shift;
    } else if (mode == XG_SWIZZLE_64KB_S_X) {
        bank_xor = ((tile_x ^ (tile_y << 1u)) & 0xFULL) << xor_shift;
    } else if (mode == XG_SWIZZLE_64KB_R_X) {
        bank_xor = (((tile_x * 3ULL) ^ tile_y) & 0xFULL) << xor_shift;
    }

    return (intra ^ bank_xor) & coord_mask;
}

uint64_t TextureAddressComputer::get_texel_element_offset_bytes(
    uint32_t plane,
    uint32_t mip_level,
    uint64_t x,
    uint64_t y,
    uint32_t z_or_slice,
    uint32_t sample) const noexcept {
    if (plane != 0u || mip_level >= mip_levels_ ||
        z_or_slice >= array_size_ || sample >= sample_count_) {
        return UINT64_MAX;
    }
    const MipLevelLayout& ml = mips_[mip_level];
    if (x >= ml.width_elements || y >= ml.height_elements) {
        return UINT64_MAX;
    }

    const uint64_t slice_base =
        ml.offset_bytes + static_cast<uint64_t>(z_or_slice) * ml.slice_stride_bytes;

    if (desc_.SwizzleMode == XG_SWIZZLE_LINEAR) {
        const uint64_t sample_plane_bytes =
            ml.linear_row_pitch_bytes * static_cast<uint64_t>(ml.height_elements);
        return slice_base +
               static_cast<uint64_t>(sample) * sample_plane_bytes +
               y * ml.linear_row_pitch_bytes +
               x * static_cast<uint64_t>(fmt_.bytes_per_element);
    }

    const uint64_t tile_x = x >> ml.x_bits;
    const uint64_t tile_y = y >> ml.y_bits;
    const uint32_t x_local = static_cast<uint32_t>(x & ((UINT64_C(1) << ml.x_bits) - 1ULL));
    const uint32_t y_local = static_cast<uint32_t>(y & ((UINT64_C(1) << ml.y_bits) - 1ULL));

    const uint64_t tile_index =
        tile_y * static_cast<uint64_t>(ml.tiles_per_row) + tile_x;
    const uint64_t tile_bytes = UINT64_C(1) << ml.tile_log2;
    const uint64_t intra_elem =
        compute_intra_tile_element(ml, x_local, y_local, sample, tile_x, tile_y);

    return slice_base +
           tile_index * tile_bytes +
           (intra_elem << fmt_.bpe_log2);
}

scarlett_hresult_t TextureAddressComputer::copy_into_subresource(
    void* dst_swizzled_resource,
    uint32_t linear_row_pitch_bytes,
    uint32_t subresource,
    const void* src_linear_buffer) const noexcept {
    if (dst_swizzled_resource == nullptr || src_linear_buffer == nullptr) {
        return SCARLETT_E_POINTER;
    }
    const uint32_t total_subs = mip_levels_ * array_size_;
    if (subresource >= total_subs) {
        return SCARLETT_E_INVALIDARG;
    }
    const uint32_t mip = subresource % mip_levels_;
    const uint32_t slice = subresource / mip_levels_;
    const MipLevelLayout& ml = mips_[mip];

    const uint64_t min_pitch =
        static_cast<uint64_t>(ml.width_elements) * fmt_.bytes_per_element;
    const uint64_t row_pitch =
        (linear_row_pitch_bytes == 0u) ? min_pitch : static_cast<uint64_t>(linear_row_pitch_bytes);
    if (row_pitch < min_pitch) {
        return SCARLETT_E_INVALIDARG;
    }

    auto* dst_bytes = static_cast<uint8_t*>(dst_swizzled_resource);
    const auto* src_bytes = static_cast<const uint8_t*>(src_linear_buffer);
    const uint64_t sample_plane_bytes = row_pitch * static_cast<uint64_t>(ml.height_elements);
    const uint32_t bpe = fmt_.bytes_per_element;

    for (uint32_t s = 0u; s < sample_count_; ++s) {
        const uint64_t sample_base = static_cast<uint64_t>(s) * sample_plane_bytes;
        for (uint32_t y = 0u; y < ml.height_elements; ++y) {
            const uint64_t row_off = sample_base + static_cast<uint64_t>(y) * row_pitch;
            for (uint32_t x = 0u; x < ml.width_elements; ++x) {
                const uint64_t dst_off =
                    get_texel_element_offset_bytes(0u, mip, x, y, slice, s);
                const uint64_t src_off = row_off + static_cast<uint64_t>(x) * bpe;
                std::memcpy(dst_bytes + dst_off, src_bytes + src_off, bpe);
            }
        }
    }
    return SCARLETT_OK;
}

scarlett_hresult_t TextureAddressComputer::copy_from_subresource(
    void* dst_linear_buffer,
    uint32_t linear_row_pitch_bytes,
    uint32_t subresource,
    const void* src_swizzled_resource) const noexcept {
    if (dst_linear_buffer == nullptr || src_swizzled_resource == nullptr) {
        return SCARLETT_E_POINTER;
    }
    const uint32_t total_subs = mip_levels_ * array_size_;
    if (subresource >= total_subs) {
        return SCARLETT_E_INVALIDARG;
    }
    const uint32_t mip = subresource % mip_levels_;
    const uint32_t slice = subresource / mip_levels_;
    const MipLevelLayout& ml = mips_[mip];

    const uint64_t min_pitch =
        static_cast<uint64_t>(ml.width_elements) * fmt_.bytes_per_element;
    const uint64_t row_pitch =
        (linear_row_pitch_bytes == 0u) ? min_pitch : static_cast<uint64_t>(linear_row_pitch_bytes);
    if (row_pitch < min_pitch) {
        return SCARLETT_E_INVALIDARG;
    }

    auto* dst_bytes = static_cast<uint8_t*>(dst_linear_buffer);
    const auto* src_bytes = static_cast<const uint8_t*>(src_swizzled_resource);
    const uint64_t sample_plane_bytes = row_pitch * static_cast<uint64_t>(ml.height_elements);
    const uint32_t bpe = fmt_.bytes_per_element;

    for (uint32_t s = 0u; s < sample_count_; ++s) {
        const uint64_t sample_base = static_cast<uint64_t>(s) * sample_plane_bytes;
        for (uint32_t y = 0u; y < ml.height_elements; ++y) {
            const uint64_t row_off = sample_base + static_cast<uint64_t>(y) * row_pitch;
            for (uint32_t x = 0u; x < ml.width_elements; ++x) {
                const uint64_t src_off =
                    get_texel_element_offset_bytes(0u, mip, x, y, slice, s);
                const uint64_t dst_off = row_off + static_cast<uint64_t>(x) * bpe;
                std::memcpy(dst_bytes + dst_off, src_bytes + src_off, bpe);
            }
        }
    }
    return SCARLETT_OK;
}

std::string generate_deswizzle_compute_glsl() {
    return R"GLSL(#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(buffer_reference, std430, buffer_reference_align = 4) buffer WordRef {
    uint word;
};

layout(push_constant) uniform DeswizzlePushConstants {
    uint64_t linear_base_addr;
    uint64_t swizzled_base_addr;
    uint width_elems;
    uint height_elems;
    uint linear_row_pitch_bytes;
    uint bpe_log2;
    uint swizzle_mode;
    uint sample_log2;
    uint tiles_per_row;
    uint direction; /* 0 = deswizzle (swizzled -> linear), 1 = swizzle (linear -> swizzled) */
    uint sample_idx;
} pc;

uint compute_intra_tile(
    uint x_local,
    uint y_local,
    uint sample_i,
    uint x_bits,
    uint y_bits,
    uint coord_bits,
    uint tile_x,
    uint tile_y) {
    uint intra = 0u;
    uint out_bit = 0u;
    uint xi = 0u;
    uint yi = 0u;
    uint si = 0u;

    if (pc.swizzle_mode == 2u || pc.swizzle_mode == 3u || pc.swizzle_mode == 7u) {
        if (xi < x_bits) { intra |= (((x_local >> xi) & 1u) << out_bit); out_bit++; xi++; }
        if (yi < y_bits) { intra |= (((y_local >> yi) & 1u) << out_bit); out_bit++; yi++; }
        while (si < pc.sample_log2) {
            intra |= (((sample_i >> si) & 1u) << out_bit);
            out_bit++;
            si++;
        }
        while (xi < x_bits || yi < y_bits) {
            if (xi < x_bits) { intra |= (((x_local >> xi) & 1u) << out_bit); out_bit++; xi++; }
            if (xi < x_bits) { intra |= (((x_local >> xi) & 1u) << out_bit); out_bit++; xi++; }
            if (yi < y_bits) { intra |= (((y_local >> yi) & 1u) << out_bit); out_bit++; yi++; }
            if (yi < y_bits) { intra |= (((y_local >> yi) & 1u) << out_bit); out_bit++; yi++; }
        }
    } else if (pc.swizzle_mode == 5u) {
        while (xi < x_bits || yi < y_bits) {
            if (yi < y_bits) { intra |= (((y_local >> yi) & 1u) << out_bit); out_bit++; yi++; }
            if (xi < x_bits) { intra |= (((x_local >> xi) & 1u) << out_bit); out_bit++; xi++; }
        }
        while (si < pc.sample_log2) {
            intra |= (((sample_i >> si) & 1u) << out_bit);
            out_bit++;
            si++;
        }
    } else {
        while (xi < x_bits || yi < y_bits) {
            if (xi < x_bits) { intra |= (((x_local >> xi) & 1u) << out_bit); out_bit++; xi++; }
            if (yi < y_bits) { intra |= (((y_local >> yi) & 1u) << out_bit); out_bit++; yi++; }
        }
        while (si < pc.sample_log2) {
            intra |= (((sample_i >> si) & 1u) << out_bit);
            out_bit++;
            si++;
        }
    }

    uint xor_shift = (coord_bits > 4u) ? (coord_bits - 4u) : 0u;
    uint bank_xor = 0u;
    if (pc.swizzle_mode == 6u) {
        bank_xor = ((tile_x ^ tile_y) & 0xFu) << xor_shift;
    } else if (pc.swizzle_mode == 7u) {
        bank_xor = ((tile_x ^ (tile_y << 1u)) & 0xFu) << xor_shift;
    } else if (pc.swizzle_mode == 5u) {
        bank_xor = (((tile_x * 3u) ^ tile_y) & 0xFu) << xor_shift;
    }
    uint coord_mask = (1u << coord_bits) - 1u;
    return (intra ^ bank_xor) & coord_mask;
}

uint read_subword(uint64_t byte_addr, uint bpe) {
    uint64_t aligned = byte_addr & ~uint64_t(3);
    uint shift = uint(byte_addr & uint64_t(3)) * 8u;
    uint w = WordRef(aligned).word;
    uint mask = (bpe == 1u) ? 0xFFu : 0xFFFFu;
    return (w >> shift) & mask;
}

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if (x >= pc.width_elems || y >= pc.height_elems) {
        return;
    }

    uint bpe = 1u << pc.bpe_log2;
    uint64_t sample_plane = uint64_t(pc.height_elems) * uint64_t(pc.linear_row_pitch_bytes);
    uint64_t lin_off = uint64_t(pc.sample_idx) * sample_plane +
                       uint64_t(y) * uint64_t(pc.linear_row_pitch_bytes) +
                       uint64_t(x) * uint64_t(bpe);

    uint64_t swiz_off = lin_off;
    if (pc.swizzle_mode != 0u) {
        uint tile_log2 = (pc.swizzle_mode == 1u || pc.swizzle_mode == 2u) ? 12u : 16u;
        uint coord_bits = tile_log2 - pc.bpe_log2;
        uint xy_bits = coord_bits - pc.sample_log2;
        uint x_bits = (xy_bits + 1u) / 2u;
        uint y_bits = xy_bits / 2u;

        uint tile_x = x >> x_bits;
        uint tile_y = y >> y_bits;
        uint x_local = x & ((1u << x_bits) - 1u);
        uint y_local = y & ((1u << y_bits) - 1u);
        uint64_t tile_index = uint64_t(tile_y) * uint64_t(pc.tiles_per_row) + uint64_t(tile_x);
        uint intra_elem = compute_intra_tile(
            x_local, y_local, pc.sample_idx, x_bits, y_bits, coord_bits, tile_x, tile_y);
        swiz_off = (tile_index << uint64_t(tile_log2)) + (uint64_t(intra_elem) << uint64_t(pc.bpe_log2));
    }

    uint64_t src_addr = (pc.direction == 0u) ? (pc.swizzled_base_addr + swiz_off)
                                             : (pc.linear_base_addr + lin_off);
    uint64_t dst_addr = (pc.direction == 0u) ? (pc.linear_base_addr + lin_off)
                                             : (pc.swizzled_base_addr + swiz_off);

    if (bpe >= 4u) {
        uint words = bpe >> 2u;
        for (uint w = 0u; w < words; ++w) {
            uint64_t off = uint64_t(w) << 2ul;
            WordRef(dst_addr + off).word = WordRef(src_addr + off).word;
        }
    } else if (pc.bpe_log2 == 1u) {
        uint v = read_subword(src_addr, 2u);
        uint64_t aligned = dst_addr & ~uint64_t(3);
        uint shift = uint(dst_addr & uint64_t(3)) * 8u;
        uint old_w = WordRef(aligned).word;
        WordRef(aligned).word = (old_w & ~(0xFFFFu << shift)) | (v << shift);
    } else {
        uint v = read_subword(src_addr, 1u);
        uint64_t aligned = dst_addr & ~uint64_t(3);
        uint shift = uint(dst_addr & uint64_t(3)) * 8u;
        uint old_w = WordRef(aligned).word;
        WordRef(aligned).word = (old_w & ~(0xFFu << shift)) | (v << shift);
    }
}
)GLSL";
}

bool compile_and_validate_deswizzle_spirv(
    std::vector<uint32_t>& out_spirv,
    std::string* out_diagnostics) {
    out_spirv.clear();

    const std::filesystem::path tmp_dir =
        std::filesystem::temp_directory_path() /
        ("scarlett_xg_spv_" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);

    const std::filesystem::path glsl_path = tmp_dir / "xg_deswizzle.comp";
    const std::filesystem::path spv_path = tmp_dir / "xg_deswizzle.spv";
    const std::filesystem::path log_path = tmp_dir / "xg_deswizzle.log";

    {
        std::ofstream f(glsl_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            if (out_diagnostics != nullptr) {
                *out_diagnostics = "failed to create temporary GLSL file";
            }
            return false;
        }
        f << generate_deswizzle_compute_glsl();
    }

    const std::string glslang_cmd =
        "glslangValidator -V --target-env vulkan1.4 \"" + glsl_path.string() +
        "\" -o \"" + spv_path.string() + "\" >\"" + log_path.string() + "\" 2>&1";
    const int glsl_rc = std::system(glslang_cmd.c_str());
    if (glsl_rc != 0) {
        if (out_diagnostics != nullptr) {
            std::ifstream log_in(log_path);
            std::ostringstream ss;
            ss << "glslangValidator failed (rc=" << glsl_rc << "): " << log_in.rdbuf();
            *out_diagnostics = ss.str();
        }
        std::filesystem::remove_all(tmp_dir, ec);
        return false;
    }

    const std::string val_cmd =
        "spirv-val --target-env vulkan1.4 \"" + spv_path.string() +
        "\" >>\"" + log_path.string() + "\" 2>&1";
    const int val_rc = std::system(val_cmd.c_str());
    if (val_rc != 0) {
        if (out_diagnostics != nullptr) {
            std::ifstream log_in(log_path);
            std::ostringstream ss;
            ss << "spirv-val --target-env vulkan1.4 failed (rc=" << val_rc << "): " << log_in.rdbuf();
            *out_diagnostics = ss.str();
        }
        std::filesystem::remove_all(tmp_dir, ec);
        return false;
    }

    std::ifstream spv_in(spv_path, std::ios::binary | std::ios::ate);
    if (!spv_in.is_open()) {
        if (out_diagnostics != nullptr) {
            *out_diagnostics = "failed to open compiled SPIR-V output";
        }
        std::filesystem::remove_all(tmp_dir, ec);
        return false;
    }
    const std::streamsize byte_size = spv_in.tellg();
    if (byte_size <= 0 || (byte_size % 4) != 0) {
        if (out_diagnostics != nullptr) {
            *out_diagnostics = "invalid SPIR-V byte size";
        }
        std::filesystem::remove_all(tmp_dir, ec);
        return false;
    }
    spv_in.seekg(0, std::ios::beg);
    out_spirv.resize(static_cast<size_t>(byte_size / 4));
    spv_in.read(reinterpret_cast<char*>(out_spirv.data()), byte_size);
    spv_in.close();

    if (out_diagnostics != nullptr) {
        *out_diagnostics = "SPIR-V 1.6 validated cleanly for vulkan1.4";
    }
    std::filesystem::remove_all(tmp_dir, ec);
    return !out_spirv.empty() && out_spirv[0] == 0x07230203U;
}

namespace {

struct RegistryState {
    std::mutex mutex;
    uint64_t next_handle = 1ULL;
    std::unordered_map<uint64_t, TextureAddressComputer> items;
};

RegistryState& get_registry_state() {
    static RegistryState state;
    return state;
}

} // namespace

TextureComputerRegistry& TextureComputerRegistry::instance() {
    static TextureComputerRegistry reg;
    return reg;
}

scarlett_hresult_t TextureComputerRegistry::create_computer(
    const XG_TEXTURE2D_DESC& desc,
    uint64_t& out_handle) {
    out_handle = 0ULL;
    TextureAddressComputer comp;
    scarlett_hresult_t hr = comp.initialize(desc);
    if (scarlett_failed(hr)) {
        return hr;
    }
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    const uint64_t h = state.next_handle++;
    state.items.emplace(h, std::move(comp));
    out_handle = h;
    return SCARLETT_OK;
}

scarlett_hresult_t TextureComputerRegistry::destroy_computer(uint64_t handle) {
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    return (state.items.erase(handle) > 0u) ? SCARLETT_OK : SCARLETT_E_HANDLE;
}

scarlett_hresult_t TextureComputerRegistry::get_resource_layout(
    uint64_t handle,
    XG_RESOURCE_LAYOUT& out_layout) {
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.items.find(handle);
    if (it == state.items.end()) {
        return SCARLETT_E_HANDLE;
    }
    return it->second.get_resource_layout(out_layout);
}

scarlett_hresult_t TextureComputerRegistry::get_subresource_layout(
    uint64_t handle,
    uint32_t subresource,
    XG_SUBRESOURCE_LAYOUT& out_layout) {
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.items.find(handle);
    if (it == state.items.end()) {
        return SCARLETT_E_HANDLE;
    }
    return it->second.get_subresource_layout(subresource, out_layout);
}

scarlett_hresult_t TextureComputerRegistry::get_texel_offset(
    uint64_t handle,
    uint32_t plane,
    uint32_t mip_level,
    uint64_t x,
    uint64_t y,
    uint32_t z_or_slice,
    uint32_t sample,
    uint64_t& out_offset) {
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.items.find(handle);
    if (it == state.items.end()) {
        out_offset = UINT64_MAX;
        return SCARLETT_E_HANDLE;
    }
    out_offset = it->second.get_texel_element_offset_bytes(
        plane, mip_level, x, y, z_or_slice, sample);
    return (out_offset == UINT64_MAX) ? SCARLETT_E_INVALIDARG : SCARLETT_OK;
}

scarlett_hresult_t TextureComputerRegistry::copy_into_subresource(
    uint64_t handle,
    void* dst,
    uint32_t pitch,
    uint32_t subresource,
    const void* src) {
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.items.find(handle);
    if (it == state.items.end()) {
        return SCARLETT_E_HANDLE;
    }
    return it->second.copy_into_subresource(dst, pitch, subresource, src);
}

scarlett_hresult_t TextureComputerRegistry::copy_from_subresource(
    uint64_t handle,
    void* dst,
    uint32_t pitch,
    uint32_t subresource,
    const void* src) {
    auto& state = get_registry_state();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.items.find(handle);
    if (it == state.items.end()) {
        return SCARLETT_E_HANDLE;
    }
    return it->second.copy_from_subresource(dst, pitch, subresource, src);
}

} // namespace scarlett::xg_xs
