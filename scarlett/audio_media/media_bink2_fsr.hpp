#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "audio_media_unix_abi.h"

namespace scarlett::audio_media {

/*
 * Clean-Room in-memory YUV420p stream header for BinkOpen(ptr, BINKFROMMEMORY).
 */
struct ScarlettBinkInMemStream {
    uint32_t magic;        /* 0x32594353 ('SCY2') */
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t fps_num;
    uint32_t fps_den;
    const uint8_t* y_planes; /* frame_count * width * height */
    const uint8_t* u_planes; /* frame_count * (width/2) * (height/2) */
    const uint8_t* v_planes; /* frame_count * (width/2) * (height/2) */
};

void convert_yuv420_to_rgb32(const uint8_t* y_plane,
                             const uint8_t* u_plane,
                             const uint8_t* v_plane,
                             uint32_t y_stride,
                             uint32_t uv_stride,
                             uint32_t src_x,
                             uint32_t src_y,
                             uint32_t copy_w,
                             uint32_t copy_h,
                             uint8_t* dst_buf,
                             int32_t dst_pitch,
                             uint32_t flags) noexcept;

class Bink2Manager {
public:
    scarlett_hresult_t handle_op(scarlett_bink2_op_params_t& params) noexcept;

private:
    struct BinkSession {
        uint64_t handle = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t total_frames = 0;
        uint32_t current_frame = 0; /* 0-based internal index; Bink summary is 1-based */
        uint32_t fps_num = 60;
        uint32_t fps_den = 1;
        uint32_t sound_enabled = 1;
        int32_t  volume = 32768;
        uint32_t paused = 0;
        bool     frame_decoded = false;

        std::vector<uint8_t> y_storage;
        std::vector<uint8_t> u_storage;
        std::vector<uint8_t> v_storage;
    };

    scarlett_hresult_t open_session(scarlett_bink2_op_params_t& params) noexcept;
    void populate_test_pattern_yuv(BinkSession& s, uint32_t width, uint32_t height, uint32_t frames) noexcept;

    std::mutex mutex_;
    uint64_t next_handle_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<BinkSession>> sessions_;
};

class FidelityFxManager {
public:
    static float halton_sequence(uint32_t index, uint32_t base) noexcept;
    static float get_upscale_ratio(uint32_t quality_mode) noexcept;
    static void get_render_resolution(uint32_t display_w,
                                      uint32_t display_h,
                                      uint32_t quality_mode,
                                      uint32_t& out_render_w,
                                      uint32_t& out_render_h) noexcept;
    static uint32_t get_jitter_phase_count(uint32_t render_w, uint32_t display_w) noexcept;
    static void get_jitter_offset(uint32_t index,
                                  uint32_t phase_count,
                                  float& out_x,
                                  float& out_y) noexcept;

    scarlett_hresult_t handle_op(scarlett_fsr_op_params_t& params) noexcept;

private:
    struct FsrContext {
        uint64_t handle = 0;
        uint32_t display_w = 0;
        uint32_t display_h = 0;
        uint32_t max_render_w = 0;
        uint32_t max_render_h = 0;
        uint32_t version_packed = 0x040102u; /* FSR 4.1.2 */
        bool vulkan_compute_available = false;
        std::vector<float> history_rgba;
    };

    bool probe_vulkan_compute() noexcept;
    void cpu_reference_upscale(const FsrContext& ctx,
                               const float* color_in,
                               uint32_t render_w,
                               uint32_t render_h,
                               float* color_out,
                               uint32_t display_w,
                               uint32_t display_h,
                               float sharpness) noexcept;

    std::mutex mutex_;
    uint64_t next_handle_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<FsrContext>> contexts_;
};

} // namespace scarlett::audio_media
