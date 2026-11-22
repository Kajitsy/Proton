#ifndef SCARLETT_XG_SFS_COALESCER_HPP
#define SCARLETT_XG_SFS_COALESCER_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "xg_xs_unix_abi.h"

namespace scarlett::xg_xs {

struct SfsTileResidencyEntry {
    uint64_t word;
    bool resident;
    uint32_t mip_level;
    uint32_t tile_x;
    uint32_t tile_y;
    uint64_t memory_handle;
    uint64_t memory_offset;
};

class SfsResidencyTracker {
public:
    SfsResidencyTracker(
        uint32_t width_texels,
        uint32_t height_texels,
        uint32_t mip_region_width,
        uint32_t mip_region_height);

    uint8_t log2_mip_region() const noexcept { return log2_mip_region_; }
    uint32_t tile_cols() const noexcept { return tile_cols_; }
    uint32_t tile_rows() const noexcept { return tile_rows_; }
    uint32_t total_tiles() const noexcept { return tile_cols_ * tile_rows_; }

    /* CPU atomic64 update matching VK_EXT_shader_image_atomic_int64 semantics */
    uint64_t atomic_record_feedback(
        uint32_t tile_x,
        uint32_t tile_y,
        uint8_t requested_mip,
        uint16_t frame_epoch);

    uint64_t read_feedback_word(uint32_t tile_x, uint32_t tile_y) const noexcept;

    void mark_tile_resident(
        uint32_t tile_x,
        uint32_t tile_y,
        uint64_t memory_handle,
        uint64_t memory_offset,
        uint16_t frame_epoch);

    void mark_tile_evicted(uint32_t tile_x, uint32_t tile_y, uint16_t frame_epoch);

    std::vector<uint32_t> collect_pending_stream_tiles() const;

    static std::string generate_sfs_atomic_int64_glsl();

private:
    uint32_t width_texels_{0};
    uint32_t height_texels_{0};
    uint32_t mip_region_w_{1};
    uint32_t mip_region_h_{1};
    uint8_t log2_mip_region_{0};
    uint32_t tile_cols_{0};
    uint32_t tile_rows_{0};
    std::vector<std::atomic<uint64_t>> feedback_words_;
};

class ScarlettSparseBindCoalescer {
public:
    static scarlett_hresult_t coalesce_binds(
        const ScarlettSparseTileBindRequest* requests,
        uint32_t request_count,
        uint32_t aspect_mask,
        std::vector<ScarlettVkSparseImageMemoryBind>& out_image_binds,
        std::vector<ScarlettVkSparseMemoryBind>& out_memory_binds,
        ScarlettSparseCoalesceStats& out_stats) noexcept;
};

} // namespace scarlett::xg_xs

#endif // SCARLETT_XG_SFS_COALESCER_HPP
