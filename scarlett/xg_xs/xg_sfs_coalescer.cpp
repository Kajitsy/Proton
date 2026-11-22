#include "xg_sfs_coalescer.hpp"

#include <algorithm>
#include <cstring>

namespace scarlett::xg_xs {

namespace {

uint8_t compute_log2_u32(uint32_t v) noexcept {
    uint8_t l = 0u;
    while (v > 1u && l < 15u) {
        v >>= 1u;
        ++l;
    }
    return l;
}

} // namespace

SfsResidencyTracker::SfsResidencyTracker(
    uint32_t width_texels,
    uint32_t height_texels,
    uint32_t mip_region_width,
    uint32_t mip_region_height)
    : width_texels_(std::max(1u, width_texels)),
      height_texels_(std::max(1u, height_texels)),
      mip_region_w_(std::max(1u, mip_region_width)),
      mip_region_h_(std::max(1u, mip_region_height)),
      log2_mip_region_(compute_log2_u32(std::max(mip_region_w_, mip_region_h_))),
      tile_cols_((width_texels_ + mip_region_w_ - 1u) / mip_region_w_),
      tile_rows_((height_texels_ + mip_region_h_ - 1u) / mip_region_h_),
      feedback_words_(static_cast<size_t>(tile_cols_) * tile_rows_) {
    for (uint32_t y = 0u; y < tile_rows_; ++y) {
        for (uint32_t x = 0u; x < tile_cols_; ++x) {
            const uint32_t tid = y * tile_cols_ + x;
            const uint64_t initial = scarlett_sfs_pack_word(
                log2_mip_region_, 0xFu, 0u, 0u, tid);
            feedback_words_[tid].store(initial, std::memory_order_relaxed);
        }
    }
}

uint64_t SfsResidencyTracker::atomic_record_feedback(
    uint32_t tile_x,
    uint32_t tile_y,
    uint8_t requested_mip,
    uint16_t frame_epoch) {
    if (tile_x >= tile_cols_ || tile_y >= tile_rows_) {
        return UINT64_MAX;
    }
    const uint32_t tid = tile_y * tile_cols_ + tile_x;
    auto& slot = feedback_words_[tid];
    uint64_t cur = slot.load(std::memory_order_acquire);
    for (;;) {
        const uint8_t cur_min = scarlett_sfs_decode_min_mip(cur);
        const uint8_t next_min = std::min<uint8_t>(cur_min, requested_mip & 0x0Fu);
        uint8_t flags = scarlett_sfs_decode_flags(cur);
        if ((flags & SCARLETT_SFS_FLAG_RESIDENT) == 0u) {
            flags |= SCARLETT_SFS_FLAG_PENDING;
        }
        flags &= static_cast<uint8_t>(~SCARLETT_SFS_FLAG_EVICTED);
        const uint64_t desired = scarlett_sfs_pack_word(
            log2_mip_region_, next_min, flags, frame_epoch, tid);
        if (slot.compare_exchange_weak(
                cur, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return desired;
        }
    }
}

uint64_t SfsResidencyTracker::read_feedback_word(uint32_t tile_x, uint32_t tile_y) const noexcept {
    if (tile_x >= tile_cols_ || tile_y >= tile_rows_) {
        return 0ULL;
    }
    return feedback_words_[tile_y * tile_cols_ + tile_x].load(std::memory_order_acquire);
}

void SfsResidencyTracker::mark_tile_resident(
    uint32_t tile_x,
    uint32_t tile_y,
    uint64_t memory_handle,
    uint64_t memory_offset,
    uint16_t frame_epoch) {
    (void)memory_handle;
    (void)memory_offset;
    if (tile_x >= tile_cols_ || tile_y >= tile_rows_) {
        return;
    }
    const uint32_t tid = tile_y * tile_cols_ + tile_x;
    auto& slot = feedback_words_[tid];
    uint64_t cur = slot.load(std::memory_order_acquire);
    for (;;) {
        const uint8_t cur_min = scarlett_sfs_decode_min_mip(cur);
        uint8_t flags = scarlett_sfs_decode_flags(cur);
        flags |= SCARLETT_SFS_FLAG_RESIDENT;
        flags &= static_cast<uint8_t>(~(SCARLETT_SFS_FLAG_PENDING | SCARLETT_SFS_FLAG_EVICTED));
        const uint64_t desired = scarlett_sfs_pack_word(
            log2_mip_region_, cur_min, flags, frame_epoch, tid);
        if (slot.compare_exchange_weak(
                cur, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
    }
}

void SfsResidencyTracker::mark_tile_evicted(
    uint32_t tile_x,
    uint32_t tile_y,
    uint16_t frame_epoch) {
    if (tile_x >= tile_cols_ || tile_y >= tile_rows_) {
        return;
    }
    const uint32_t tid = tile_y * tile_cols_ + tile_x;
    auto& slot = feedback_words_[tid];
    uint64_t cur = slot.load(std::memory_order_acquire);
    for (;;) {
        uint8_t flags = scarlett_sfs_decode_flags(cur);
        flags &= static_cast<uint8_t>(~(SCARLETT_SFS_FLAG_RESIDENT | SCARLETT_SFS_FLAG_PENDING));
        flags |= SCARLETT_SFS_FLAG_EVICTED;
        const uint64_t desired = scarlett_sfs_pack_word(
            log2_mip_region_, 0xFu, flags, frame_epoch, tid);
        if (slot.compare_exchange_weak(
                cur, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
    }
}

std::vector<uint32_t> SfsResidencyTracker::collect_pending_stream_tiles() const {
    std::vector<uint32_t> pending;
    pending.reserve(feedback_words_.size() / 4u);
    for (size_t i = 0; i < feedback_words_.size(); ++i) {
        const uint64_t w = feedback_words_[i].load(std::memory_order_acquire);
        const uint8_t flags = scarlett_sfs_decode_flags(w);
        if ((flags & SCARLETT_SFS_FLAG_PENDING) != 0u &&
            (flags & SCARLETT_SFS_FLAG_RESIDENT) == 0u) {
            pending.push_back(static_cast<uint32_t>(i));
        }
    }
    return pending;
}

std::string SfsResidencyTracker::generate_sfs_atomic_int64_glsl() {
    return R"GLSL(#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_shader_image_int64 : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(r64ui, binding = 0) uniform u64image2D u_sfs_feedback;

layout(push_constant) uniform SfsPush {
    uint log2_mip_region;
    uint requested_mip;
    uint frame_epoch;
    uint tile_cols;
} pc;

void main() {
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    uint64_t tile_id = uint64_t(coord.y) * uint64_t(pc.tile_cols) + uint64_t(coord.x);
    uint64_t word = uint64_t(pc.log2_mip_region & 0xFu) |
                    (uint64_t(pc.requested_mip & 0xFu) << 4ul) |
                    (uint64_t(0x02u) << 8ul) |
                    (uint64_t(pc.frame_epoch & 0xFFFFu) << 16ul) |
                    (tile_id << 32ul);
    imageAtomicMin(u_sfs_feedback, coord, word);
}
)GLSL";
}

scarlett_hresult_t ScarlettSparseBindCoalescer::coalesce_binds(
    const ScarlettSparseTileBindRequest* requests,
    uint32_t request_count,
    uint32_t aspect_mask,
    std::vector<ScarlettVkSparseImageMemoryBind>& out_image_binds,
    std::vector<ScarlettVkSparseMemoryBind>& out_memory_binds,
    ScarlettSparseCoalesceStats& out_stats) noexcept {
    out_image_binds.clear();
    out_memory_binds.clear();
    std::memset(&out_stats, 0, sizeof(out_stats));

    if (request_count == 0u) {
        return SCARLETT_OK;
    }
    if (requests == nullptr) {
        return SCARLETT_E_POINTER;
    }

    std::vector<ScarlettSparseTileBindRequest> sorted(requests, requests + request_count);

    // 1. Coalesce opaque linear VkSparseMemoryBind by resource_offset
    std::sort(sorted.begin(), sorted.end(),
              [](const ScarlettSparseTileBindRequest& a,
                 const ScarlettSparseTileBindRequest& b) {
                  if (a.resource_offset != b.resource_offset) {
                      return a.resource_offset < b.resource_offset;
                  }
                  if (a.memory_handle != b.memory_handle) {
                      return a.memory_handle < b.memory_handle;
                  }
                  return a.memory_offset < b.memory_offset;
              });

    for (const auto& req : sorted) {
        if (!out_memory_binds.empty()) {
            auto& prev = out_memory_binds.back();
            const bool contiguous_res =
                (prev.resourceOffset + prev.size == req.resource_offset);
            const bool same_mem = (prev.memory == req.memory_handle) &&
                                  (prev.flags == req.flags);
            const bool contiguous_mem =
                (req.memory_handle == 0ULL) ||
                (prev.memoryOffset + prev.size == req.memory_offset);
            if (contiguous_res && same_mem && contiguous_mem) {
                prev.size += SCARLETT_SFS_TILE_SIZE_BYTES;
                continue;
            }
        }
        ScarlettVkSparseMemoryBind bind{};
        bind.resourceOffset = req.resource_offset;
        bind.size = SCARLETT_SFS_TILE_SIZE_BYTES;
        bind.memory = req.memory_handle;
        bind.memoryOffset = req.memory_offset;
        bind.flags = req.flags;
        out_memory_binds.push_back(bind);
    }

    // 2. Coalesce 2D VkSparseImageMemoryBind:
    //    First horizontal runs along tile_x, then vertical 2D rectangle merging along tile_y
    std::sort(sorted.begin(), sorted.end(),
              [](const ScarlettSparseTileBindRequest& a,
                 const ScarlettSparseTileBindRequest& b) {
                  if (a.mip_level != b.mip_level) return a.mip_level < b.mip_level;
                  if (a.array_layer != b.array_layer) return a.array_layer < b.array_layer;
                  if (a.tile_y != b.tile_y) return a.tile_y < b.tile_y;
                  return a.tile_x < b.tile_x;
              });

    struct Run2D {
        uint32_t mip_level;
        uint32_t array_layer;
        uint32_t tile_x;
        uint32_t tile_y;
        uint32_t tile_cols;
        uint32_t tile_rows;
        uint32_t tile_w_texels;
        uint32_t tile_h_texels;
        uint64_t memory;
        uint64_t memory_offset;
        uint64_t byte_span;
        uint32_t flags;
    };

    std::vector<Run2D> horiz_runs;
    horiz_runs.reserve(sorted.size());

    for (const auto& req : sorted) {
        const uint32_t tw = std::max(1u, req.tile_width_texels);
        const uint32_t th = std::max(1u, req.tile_height_texels);
        if (!horiz_runs.empty()) {
            auto& prev = horiz_runs.back();
            const bool same_row = (prev.mip_level == req.mip_level) &&
                                  (prev.array_layer == req.array_layer) &&
                                  (prev.tile_y == req.tile_y) &&
                                  (prev.tile_x + prev.tile_cols == req.tile_x) &&
                                  (prev.tile_w_texels == tw) &&
                                  (prev.tile_h_texels == th);
            const bool same_mem = (prev.memory == req.memory_handle) &&
                                  (prev.flags == req.flags);
            const bool contiguous_mem =
                (req.memory_handle == 0ULL) ||
                (prev.memory_offset + prev.byte_span == req.memory_offset);
            if (same_row && same_mem && contiguous_mem) {
                prev.tile_cols += 1u;
                prev.byte_span += SCARLETT_SFS_TILE_SIZE_BYTES;
                continue;
            }
        }
        horiz_runs.push_back(Run2D{
            req.mip_level,
            req.array_layer,
            req.tile_x,
            req.tile_y,
            1u,
            1u,
            tw,
            th,
            req.memory_handle,
            req.memory_offset,
            SCARLETT_SFS_TILE_SIZE_BYTES,
            req.flags,
        });
    }

    // Merge identical-width vertical strips when consecutive rows also have contiguous memory
    std::vector<Run2D> merged_2d;
    merged_2d.reserve(horiz_runs.size());
    std::vector<bool> consumed(horiz_runs.size(), false);

    for (size_t i = 0; i < horiz_runs.size(); ++i) {
        if (consumed[i]) {
            continue;
        }
        Run2D rect = horiz_runs[i];
        consumed[i] = true;
        for (size_t j = i + 1u; j < horiz_runs.size(); ++j) {
            if (consumed[j]) {
                continue;
            }
            const auto& cand = horiz_runs[j];
            if (cand.mip_level != rect.mip_level || cand.array_layer != rect.array_layer) {
                break;
            }
            if (cand.tile_y > rect.tile_y + rect.tile_rows) {
                break;
            }
            if (cand.tile_y == rect.tile_y + rect.tile_rows &&
                cand.tile_x == rect.tile_x &&
                cand.tile_cols == rect.tile_cols &&
                cand.tile_w_texels == rect.tile_w_texels &&
                cand.tile_h_texels == rect.tile_h_texels &&
                cand.memory == rect.memory &&
                cand.flags == rect.flags &&
                (rect.memory == 0ULL ||
                 rect.memory_offset + rect.byte_span == cand.memory_offset)) {
                rect.tile_rows += 1u;
                rect.byte_span += cand.byte_span;
                consumed[j] = true;
            }
        }
        merged_2d.push_back(rect);
    }

    out_image_binds.reserve(merged_2d.size());
    for (const auto& r : merged_2d) {
        ScarlettVkSparseImageMemoryBind ib{};
        ib.aspectMask = (aspect_mask == 0u) ? 1u : aspect_mask; /* VK_IMAGE_ASPECT_COLOR_BIT */
        ib.mipLevel = r.mip_level;
        ib.arrayLayer = r.array_layer;
        ib.offsetX = static_cast<int32_t>(r.tile_x * r.tile_w_texels);
        ib.offsetY = static_cast<int32_t>(r.tile_y * r.tile_h_texels);
        ib.offsetZ = 0;
        ib.extentWidth = r.tile_cols * r.tile_w_texels;
        ib.extentHeight = r.tile_rows * r.tile_h_texels;
        ib.extentDepth = 1u;
        ib.flags = r.flags;
        ib.memory = r.memory;
        ib.memoryOffset = r.memory_offset;
        out_image_binds.push_back(ib);
    }

    out_stats.input_tile_count = request_count;
    out_stats.coalesced_image_bind_count = static_cast<uint32_t>(out_image_binds.size());
    out_stats.coalesced_memory_bind_count = static_cast<uint32_t>(out_memory_binds.size());
    out_stats.total_bound_64kb_tiles = request_count;
    return SCARLETT_OK;
}

} // namespace scarlett::xg_xs
