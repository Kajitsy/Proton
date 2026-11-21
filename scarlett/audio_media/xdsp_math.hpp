#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio_media_unix_abi.h"

namespace scarlett::audio_media {

/*
 * XDSP: Radix-2 Cooley-Tukey FFT / IFFT.
 * Uses AVX2 vectorization when compiled with __AVX2__ and clean scalar fallback otherwise.
 * `n` must be a power of 2 >= 2.
 */
bool xdsp_fft_f32(uint32_t n, float* real_io, float* imag_io, bool inverse) noexcept;

/*
 * XDSP Overlap-Add Convolution Reverb.
 * Maintains overlap tail across consecutive audio quanta.
 */
class ConvolutionReverb {
public:
    bool configure(const float* ir, uint32_t ir_len, float wet_mix = 0.35f) noexcept;
    void reset() noexcept;
    void process_block(const float* input, float* output, uint32_t frame_count) noexcept;

    uint32_t ir_length() const noexcept { return static_cast<uint32_t>(ir_.size()); }

private:
    std::vector<float> ir_;
    std::vector<float> overlap_tail_;
    std::vector<float> ir_fft_re_;
    std::vector<float> ir_fft_im_;
    uint32_t fft_size_ = 0;
    uint32_t block_size_ = 0;
    float wet_mix_ = 0.35f;
};

/*
 * HSRC: High-Quality Sample Rate Converter (Polyphase Kaiser-windowed Sinc + Cubic fallback).
 */
class PolyphaseResampler {
public:
    static constexpr uint32_t kNumPhases = 64;
    static constexpr uint32_t kTapsPerPhase = 16;

    void configure(uint32_t src_rate_hz, uint32_t dst_rate_hz, uint32_t channels) noexcept;
    void reset() noexcept;

    /* Resamples `in_frames` interleaved frames, appending/writing up to `max_out_frames`.
     * Returns actual output frames produced. */
    uint32_t process(const float* in,
                     uint32_t in_frames,
                     float* out,
                     uint32_t max_out_frames) noexcept;

    uint32_t src_rate() const noexcept { return src_rate_; }
    uint32_t dst_rate() const noexcept { return dst_rate_; }
    bool is_bypass() const noexcept { return src_rate_ == dst_rate_; }

private:
    void build_filter_table() noexcept;

    uint32_t src_rate_ = 48000;
    uint32_t dst_rate_ = 48000;
    uint32_t channels_ = 2;
    double phase_pos_ = 0.0;
    double step_ = 1.0;
    std::vector<float> coeffs_; /* [kNumPhases * kTapsPerPhase] */
    std::vector<float> history_; /* [kTapsPerPhase * channels_] */
};

/*
 * Spatial Audio -> Binaural HRTF Renderer.
 * Deterministic headless ITD (fractional delay) + ILD (head-shadowing one-pole LPF + gain).
 */
class HrtfBinauralRenderer {
public:
    void configure(uint32_t sample_rate_hz) noexcept;
    void set_direction(float azimuth_rad, float elevation_rad, float distance) noexcept;
    void reset() noexcept;

    /* Renders `frames` mono (or downmixed) samples into interleaved stereo [L, R, ...]. */
    void render_mono_to_stereo(const float* mono_in,
                               float* stereo_out,
                               uint32_t frames) noexcept;

    /* Accumulates into existing stereo mix buffer. */
    void accumulate_stereo(const float* in_interleaved,
                           uint32_t in_channels,
                           float* stereo_mix_out,
                           uint32_t frames,
                           float gain) noexcept;

private:
    static constexpr uint32_t kDelayLineLen = 256;

    uint32_t sample_rate_ = 48000;
    float azimuth_rad_ = 0.0f;
    float elevation_rad_ = 0.0f;
    float distance_ = 1.0f;

    float delay_l_samples_ = 0.0f;
    float delay_r_samples_ = 0.0f;
    float gain_l_ = 0.70710678f;
    float gain_r_ = 0.70710678f;
    float lpf_alpha_l_ = 1.0f;
    float lpf_alpha_r_ = 1.0f;

    std::vector<float> delay_buf_;
    uint32_t write_pos_ = 0;
    float lpf_state_l_ = 0.0f;
    float lpf_state_r_ = 0.0f;
};

/*
 * Probes PipeWire / OpenAL Soft availability and returns the active sink mode.
 * Always falls back cleanly to SCARLETT_AUDIO_SINK_HEADLESS in CI without audio hardware.
 */
scarlett_audio_sink_mode_t probe_audio_sink(scarlett_audio_sink_mode_t requested) noexcept;

} // namespace scarlett::audio_media
