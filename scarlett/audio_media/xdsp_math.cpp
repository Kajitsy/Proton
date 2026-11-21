#include "xdsp_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(SCARLETT_HAVE_OPENAL)
#include <AL/alc.h>
#include <AL/alext.h>
#endif

#if defined(SCARLETT_HAVE_PIPEWIRE)
#include <pipewire/pipewire.h>
#endif

namespace scarlett::audio_media {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool is_power_of_two(uint32_t v) noexcept {
    return v >= 2u && (v & (v - 1u)) == 0u;
}

uint32_t next_power_of_two(uint32_t v) noexcept {
    uint32_t p = 1u;
    while (p < v && p < (1u << 30)) {
        p <<= 1u;
    }
    return p < 2u ? 2u : p;
}

double bessel_i0(double x) noexcept {
    double sum = 1.0;
    double term = 1.0;
    const double half_x = x * 0.5;
    for (int k = 1; k <= 20; ++k) {
        const double ratio = half_x / static_cast<double>(k);
        term *= ratio * ratio;
        sum += term;
        if (term < 1e-12 * sum) {
            break;
        }
    }
    return sum;
}

} // namespace

bool xdsp_fft_f32(uint32_t n, float* real_io, float* imag_io, bool inverse) noexcept {
    if (!is_power_of_two(n) || real_io == nullptr || imag_io == nullptr) {
        return false;
    }

    /* Bit-reversal permutation */
    uint32_t j = 0u;
    for (uint32_t i = 1u; i < n; ++i) {
        uint32_t bit = n >> 1u;
        while ((j & bit) != 0u) {
            j ^= bit;
            bit >>= 1u;
        }
        j ^= bit;
        if (i < j) {
            std::swap(real_io[i], real_io[j]);
            std::swap(imag_io[i], imag_io[j]);
        }
    }

    const double sign = inverse ? 1.0 : -1.0;

    for (uint32_t len = 2u; len <= n; len <<= 1u) {
        const uint32_t half = len >> 1u;
        const double theta = sign * (2.0 * kPi / static_cast<double>(len));
        const float wstep_re = static_cast<float>(std::cos(theta));
        const float wstep_im = static_cast<float>(std::sin(theta));

#if defined(__AVX2__)
        if (half >= 8u) {
            alignas(32) float tw_re_arr[8];
            alignas(32) float tw_im_arr[8];
            double cur_re = 1.0;
            double cur_im = 0.0;
            for (uint32_t k = 0u; k < 8u; ++k) {
                const double ang = theta * static_cast<double>(k);
                tw_re_arr[k] = static_cast<float>(std::cos(ang));
                tw_im_arr[k] = static_cast<float>(std::sin(ang));
            }
            const double ang8 = theta * 8.0;
            const __m256 step8_re = _mm256_set1_ps(static_cast<float>(std::cos(ang8)));
            const __m256 step8_im = _mm256_set1_ps(static_cast<float>(std::sin(ang8)));

            for (uint32_t base = 0u; base < n; base += len) {
                __m256 w_re = _mm256_load_ps(tw_re_arr);
                __m256 w_im = _mm256_load_ps(tw_im_arr);

                for (uint32_t k = 0u; k < half; k += 8u) {
                    const uint32_t idx0 = base + k;
                    const uint32_t idx1 = idx0 + half;

                    __m256 u_re = _mm256_loadu_ps(real_io + idx0);
                    __m256 u_im = _mm256_loadu_ps(imag_io + idx0);
                    __m256 v_re = _mm256_loadu_ps(real_io + idx1);
                    __m256 v_im = _mm256_loadu_ps(imag_io + idx1);

                    __m256 t_re = _mm256_sub_ps(_mm256_mul_ps(v_re, w_re),
                                                _mm256_mul_ps(v_im, w_im));
                    __m256 t_im = _mm256_add_ps(_mm256_mul_ps(v_re, w_im),
                                                _mm256_mul_ps(v_im, w_re));

                    _mm256_storeu_ps(real_io + idx0, _mm256_add_ps(u_re, t_re));
                    _mm256_storeu_ps(imag_io + idx0, _mm256_add_ps(u_im, t_im));
                    _mm256_storeu_ps(real_io + idx1, _mm256_sub_ps(u_re, t_re));
                    _mm256_storeu_ps(imag_io + idx1, _mm256_sub_ps(u_im, t_im));

                    __m256 next_w_re = _mm256_sub_ps(_mm256_mul_ps(w_re, step8_re),
                                                     _mm256_mul_ps(w_im, step8_im));
                    __m256 next_w_im = _mm256_add_ps(_mm256_mul_ps(w_re, step8_im),
                                                     _mm256_mul_ps(w_im, step8_re));
                    w_re = next_w_re;
                    w_im = next_w_im;
                }
            }
            (void)cur_re;
            (void)cur_im;
            continue;
        }
#endif

        for (uint32_t base = 0u; base < n; base += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (uint32_t k = 0u; k < half; ++k) {
                const uint32_t idx0 = base + k;
                const uint32_t idx1 = idx0 + half;

                const float v_re = real_io[idx1];
                const float v_im = imag_io[idx1];
                const float t_re = v_re * w_re - v_im * w_im;
                const float t_im = v_re * w_im + v_im * w_re;

                const float u_re = real_io[idx0];
                const float u_im = imag_io[idx0];
                real_io[idx0] = u_re + t_re;
                imag_io[idx0] = u_im + t_im;
                real_io[idx1] = u_re - t_re;
                imag_io[idx1] = u_im - t_im;

                const float next_re = w_re * wstep_re - w_im * wstep_im;
                const float next_im = w_re * wstep_im + w_im * wstep_re;
                w_re = next_re;
                w_im = next_im;
            }
        }
    }

    if (inverse) {
        const float inv_n = 1.0f / static_cast<float>(n);
        for (uint32_t i = 0u; i < n; ++i) {
            real_io[i] *= inv_n;
            imag_io[i] *= inv_n;
        }
    }

    return true;
}

/* ============================================================================
 * Overlap-Add Convolution Reverb
 * ============================================================================ */

bool ConvolutionReverb::configure(const float* ir, uint32_t ir_len, float wet_mix) noexcept {
    if (ir == nullptr || ir_len == 0u) {
        return false;
    }
    ir_.assign(ir, ir + ir_len);
    wet_mix_ = std::clamp(wet_mix, 0.0f, 1.0f);
    block_size_ = SCARLETT_AUDIO_QUANTUM_FRAMES;
    fft_size_ = next_power_of_two(block_size_ + ir_len - 1u);

    ir_fft_re_.assign(fft_size_, 0.0f);
    ir_fft_im_.assign(fft_size_, 0.0f);
    std::copy(ir_.begin(), ir_.end(), ir_fft_re_.begin());
    xdsp_fft_f32(fft_size_, ir_fft_re_.data(), ir_fft_im_.data(), false);

    overlap_tail_.assign(fft_size_, 0.0f);
    return true;
}

void ConvolutionReverb::reset() noexcept {
    std::fill(overlap_tail_.begin(), overlap_tail_.end(), 0.0f);
}

void ConvolutionReverb::process_block(const float* input,
                                      float* output,
                                      uint32_t frame_count) noexcept {
    if (input == nullptr || output == nullptr || frame_count == 0u) {
        return;
    }
    if (ir_.empty() || fft_size_ < frame_count + ir_.size() - 1u) {
        if (input != output) {
            std::copy(input, input + frame_count, output);
        }
        return;
    }

    std::vector<float> x_re(fft_size_, 0.0f);
    std::vector<float> x_im(fft_size_, 0.0f);
    std::copy(input, input + frame_count, x_re.begin());

    xdsp_fft_f32(fft_size_, x_re.data(), x_im.data(), false);

    for (uint32_t i = 0u; i < fft_size_; ++i) {
        const float ar = x_re[i];
        const float ai = x_im[i];
        const float br = ir_fft_re_[i];
        const float bi = ir_fft_im_[i];
        x_re[i] = ar * br - ai * bi;
        x_im[i] = ar * bi + ai * br;
    }

    xdsp_fft_f32(fft_size_, x_re.data(), x_im.data(), true);

    const float dry = 1.0f - wet_mix_;
    for (uint32_t i = 0u; i < frame_count; ++i) {
        const float conv = x_re[i] + overlap_tail_[i];
        output[i] = dry * input[i] + wet_mix_ * conv;
    }

    const uint32_t tail_len = fft_size_ - frame_count;
    for (uint32_t i = 0u; i < tail_len; ++i) {
        overlap_tail_[i] = x_re[i + frame_count] + overlap_tail_[i + frame_count];
    }
    for (uint32_t i = tail_len; i < fft_size_; ++i) {
        overlap_tail_[i] = 0.0f;
    }
}

/* ============================================================================
 * Polyphase HSRC Resampler
 * ============================================================================ */

void PolyphaseResampler::build_filter_table() noexcept {
    coeffs_.assign(kNumPhases * kTapsPerPhase, 0.0f);
    const double cutoff = std::min(1.0, static_cast<double>(dst_rate_) /
                                            static_cast<double>(src_rate_)) * 0.92;
    constexpr double kBeta = 6.5;
    const double inv_i0_beta = 1.0 / bessel_i0(kBeta);
    const double half_span = static_cast<double>(kTapsPerPhase) * 0.5;

    for (uint32_t p = 0u; p < kNumPhases; ++p) {
        const double frac = static_cast<double>(p) / static_cast<double>(kNumPhases);
        double sum = 0.0;
        for (uint32_t t = 0u; t < kTapsPerPhase; ++t) {
            const double x = static_cast<double>(t) - (half_span - 1.0) - frac;
            const double norm_x = x / half_span;
            double win = 0.0;
            if (std::abs(norm_x) <= 1.0) {
                win = bessel_i0(kBeta * std::sqrt(1.0 - norm_x * norm_x)) * inv_i0_beta;
            }
            const double sinc_arg = x * cutoff;
            const double s = (std::abs(sinc_arg) < 1e-9)
                                 ? cutoff
                                 : (std::sin(kPi * sinc_arg) / (kPi * x));
            const double c = s * win;
            coeffs_[p * kTapsPerPhase + t] = static_cast<float>(c);
            sum += c;
        }
        if (sum > 1e-9) {
            const float inv_sum = static_cast<float>(1.0 / sum);
            for (uint32_t t = 0u; t < kTapsPerPhase; ++t) {
                coeffs_[p * kTapsPerPhase + t] *= inv_sum;
            }
        }
    }
}

void PolyphaseResampler::configure(uint32_t src_rate_hz,
                                   uint32_t dst_rate_hz,
                                   uint32_t channels) noexcept {
    src_rate_ = src_rate_hz == 0u ? 48000u : src_rate_hz;
    dst_rate_ = dst_rate_hz == 0u ? 48000u : dst_rate_hz;
    channels_ = std::clamp(channels, 1u, SCARLETT_AUDIO_MAX_CHANNELS);
    step_ = static_cast<double>(src_rate_) / static_cast<double>(dst_rate_);
    phase_pos_ = 0.0;
    history_.assign(kTapsPerPhase * channels_, 0.0f);
    build_filter_table();
}

void PolyphaseResampler::reset() noexcept {
    phase_pos_ = 0.0;
    std::fill(history_.begin(), history_.end(), 0.0f);
}

uint32_t PolyphaseResampler::process(const float* in,
                                     uint32_t in_frames,
                                     float* out,
                                     uint32_t max_out_frames) noexcept {
    if (in == nullptr || out == nullptr || in_frames == 0u || max_out_frames == 0u) {
        return 0u;
    }
    if (is_bypass()) {
        const uint32_t copy_frames = std::min(in_frames, max_out_frames);
        std::copy(in, in + copy_frames * channels_, out);
        return copy_frames;
    }

    const uint32_t hist_frames = kTapsPerPhase;
    std::vector<float> work((hist_frames + in_frames) * channels_, 0.0f);
    std::copy(history_.begin(), history_.end(), work.begin());
    std::copy(in, in + in_frames * channels_, work.begin() + hist_frames * channels_);

    uint32_t out_count = 0u;
    double pos = phase_pos_;
    const double max_pos = static_cast<double>(in_frames);

    while (pos < max_pos && out_count < max_out_frames) {
        const uint32_t ipos = static_cast<uint32_t>(pos);
        const double frac = pos - static_cast<double>(ipos);
        const uint32_t phase_idx = std::min(
            kNumPhases - 1u,
            static_cast<uint32_t>(frac * static_cast<double>(kNumPhases)));
        const float* taps = &coeffs_[phase_idx * kTapsPerPhase];

        /* Center tap around hist_frames + ipos - kTapsPerPhase/2 + 1 */
        const uint32_t base_frame = ipos + 1u;
        for (uint32_t ch = 0u; ch < channels_; ++ch) {
            float acc = 0.0f;
            for (uint32_t t = 0u; t < kTapsPerPhase; ++t) {
                acc += taps[t] * work[(base_frame + t) * channels_ + ch];
            }
            out[out_count * channels_ + ch] = acc;
        }
        ++out_count;
        pos += step_;
    }

    phase_pos_ = pos - max_pos;
    if (phase_pos_ < 0.0) {
        phase_pos_ = 0.0;
    }

    const uint32_t total_frames = hist_frames + in_frames;
    const uint32_t tail_start = total_frames - hist_frames;
    std::copy(work.begin() + tail_start * channels_,
              work.end(),
              history_.begin());

    return out_count;
}

/* ============================================================================
 * Spatial Audio -> Binaural HRTF Renderer
 * ============================================================================ */

void HrtfBinauralRenderer::configure(uint32_t sample_rate_hz) noexcept {
    sample_rate_ = sample_rate_hz == 0u ? 48000u : sample_rate_hz;
    delay_buf_.assign(kDelayLineLen, 0.0f);
    write_pos_ = 0u;
    lpf_state_l_ = 0.0f;
    lpf_state_r_ = 0.0f;
    set_direction(azimuth_rad_, elevation_rad_, distance_);
}

void HrtfBinauralRenderer::set_direction(float azimuth_rad,
                                         float elevation_rad,
                                         float distance) noexcept {
    azimuth_rad_ = azimuth_rad;
    elevation_rad_ = elevation_rad;
    distance_ = std::max(0.25f, distance);

    /* Woodworth spherical head ITD model (~0.66ms max interaural delay) */
    const float pan = std::sin(azimuth_rad_) * std::cos(elevation_rad_);
    const float max_itd_samples = 0.00066f * static_cast<float>(sample_rate_);
    if (pan >= 0.0f) {
        delay_r_samples_ = 1.0f;
        delay_l_samples_ = 1.0f + pan * max_itd_samples;
    } else {
        delay_l_samples_ = 1.0f;
        delay_r_samples_ = 1.0f + (-pan) * max_itd_samples;
    }

    const float atten = 1.0f / (1.0f + 0.12f * (distance_ - 1.0f));
    const float theta = std::clamp((pan + 1.0f) * 0.5f, 0.0f, 1.0f) *
                        static_cast<float>(kPi * 0.5);
    gain_l_ = std::cos(theta) * atten;
    gain_r_ = std::sin(theta) * atten;

    /* Head-shadowing low-pass filter on the contralateral ear */
    lpf_alpha_l_ = pan > 0.0f ? std::max(0.22f, 1.0f - 0.65f * pan) : 1.0f;
    lpf_alpha_r_ = pan < 0.0f ? std::max(0.22f, 1.0f - 0.65f * (-pan)) : 1.0f;
}

void HrtfBinauralRenderer::reset() noexcept {
    std::fill(delay_buf_.begin(), delay_buf_.end(), 0.0f);
    write_pos_ = 0u;
    lpf_state_l_ = 0.0f;
    lpf_state_r_ = 0.0f;
}

void HrtfBinauralRenderer::render_mono_to_stereo(const float* mono_in,
                                                 float* stereo_out,
                                                 uint32_t frames) noexcept {
    if (mono_in == nullptr || stereo_out == nullptr || frames == 0u) {
        return;
    }
    if (delay_buf_.size() != kDelayLineLen) {
        configure(sample_rate_);
    }

    const uint32_t mask = kDelayLineLen - 1u;
    auto read_frac = [&](float delay_samples) -> float {
        const uint32_t d_int = static_cast<uint32_t>(delay_samples);
        const float frac = delay_samples - static_cast<float>(d_int);
        const uint32_t i0 = (write_pos_ - d_int) & mask;
        const uint32_t i1 = (write_pos_ - d_int - 1u) & mask;
        return delay_buf_[i0] * (1.0f - frac) + delay_buf_[i1] * frac;
    };

    for (uint32_t i = 0u; i < frames; ++i) {
        delay_buf_[write_pos_] = mono_in[i];

        const float raw_l = read_frac(delay_l_samples_) * gain_l_;
        const float raw_r = read_frac(delay_r_samples_) * gain_r_;

        lpf_state_l_ += lpf_alpha_l_ * (raw_l - lpf_state_l_);
        lpf_state_r_ += lpf_alpha_r_ * (raw_r - lpf_state_r_);

        stereo_out[i * 2u + 0u] = lpf_state_l_;
        stereo_out[i * 2u + 1u] = lpf_state_r_;

        write_pos_ = (write_pos_ + 1u) & mask;
    }
}

void HrtfBinauralRenderer::accumulate_stereo(const float* in_interleaved,
                                             uint32_t in_channels,
                                             float* stereo_mix_out,
                                             uint32_t frames,
                                             float gain) noexcept {
    if (in_interleaved == nullptr || stereo_mix_out == nullptr ||
        frames == 0u || in_channels == 0u) {
        return;
    }
    std::vector<float> mono(frames, 0.0f);
    std::vector<float> tmp_stereo(frames * 2u, 0.0f);
    const float inv_ch = 1.0f / static_cast<float>(in_channels);
    for (uint32_t i = 0u; i < frames; ++i) {
        float sum = 0.0f;
        for (uint32_t c = 0u; c < in_channels; ++c) {
            sum += in_interleaved[i * in_channels + c];
        }
        mono[i] = sum * inv_ch;
    }
    render_mono_to_stereo(mono.data(), tmp_stereo.data(), frames);
    for (uint32_t i = 0u; i < frames * 2u; ++i) {
        stereo_mix_out[i] += tmp_stereo[i] * gain;
    }
}

scarlett_audio_sink_mode_t probe_audio_sink(scarlett_audio_sink_mode_t requested) noexcept {
    if (requested == SCARLETT_AUDIO_SINK_HEADLESS) {
        return SCARLETT_AUDIO_SINK_HEADLESS;
    }

#if defined(SCARLETT_HAVE_PIPEWIRE)
    if (requested == SCARLETT_AUDIO_SINK_PIPEWIRE || requested == SCARLETT_AUDIO_SINK_AUTO) {
        pw_init(nullptr, nullptr);
        pw_main_loop* loop = pw_main_loop_new(nullptr);
        if (loop != nullptr) {
            pw_context* ctx = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
            if (ctx != nullptr) {
                pw_core* core = pw_context_connect(ctx, nullptr, 0);
                if (core != nullptr) {
                    pw_core_disconnect(core);
                    pw_context_destroy(ctx);
                    pw_main_loop_destroy(loop);
                    return SCARLETT_AUDIO_SINK_PIPEWIRE;
                }
                pw_context_destroy(ctx);
            }
            pw_main_loop_destroy(loop);
        }
        if (requested == SCARLETT_AUDIO_SINK_PIPEWIRE) {
            return SCARLETT_AUDIO_SINK_HEADLESS;
        }
    }
#endif

#if defined(SCARLETT_HAVE_OPENAL)
    if (requested == SCARLETT_AUDIO_SINK_OPENAL || requested == SCARLETT_AUDIO_SINK_AUTO) {
        ALCdevice* dev = alcOpenDevice(nullptr);
        if (dev != nullptr) {
            ALCint attrs[] = {
#ifdef ALC_HRTF_SOFT
                ALC_HRTF_SOFT, ALC_TRUE,
#endif
                0
            };
            ALCcontext* ctx = alcCreateContext(dev, attrs);
            if (ctx != nullptr) {
                alcDestroyContext(ctx);
                alcCloseDevice(dev);
                return SCARLETT_AUDIO_SINK_OPENAL;
            }
            alcCloseDevice(dev);
        }
    }
#endif

    return SCARLETT_AUDIO_SINK_HEADLESS;
}

} // namespace scarlett::audio_media
