#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "audio_media_unix_abi.h"
#include "xapu_codecs.hpp"
#include "xdsp_math.hpp"

namespace scarlett::audio_media {

class XAudio2MixerManager {
public:
    XAudio2MixerManager() noexcept = default;
    ~XAudio2MixerManager() noexcept;

    XAudio2MixerManager(const XAudio2MixerManager&) = delete;
    XAudio2MixerManager& operator=(const XAudio2MixerManager&) = delete;

    scarlett_hresult_t create_engine(const scarlett_audio_engine_create_params_t& req,
                                     uint64_t& out_engine_handle,
                                     uint32_t& out_active_sink) noexcept;
    scarlett_hresult_t destroy_engine(uint64_t engine_handle) noexcept;

    scarlett_hresult_t start_engine(uint64_t engine_handle) noexcept;
    scarlett_hresult_t stop_engine(uint64_t engine_handle) noexcept;

    scarlett_hresult_t create_voice(const scarlett_audio_voice_create_params_t& req,
                                    uint64_t& out_voice_handle) noexcept;
    scarlett_hresult_t destroy_voice(uint64_t voice_handle) noexcept;
    scarlett_hresult_t voice_control(scarlett_audio_voice_ctrl_params_t& params) noexcept;
    scarlett_hresult_t submit_voice_buffer(const scarlett_audio_voice_buffer_params_t& req) noexcept;

    scarlett_hresult_t render_quantum(scarlett_audio_render_quantum_params_t& params) noexcept;

private:
    struct Voice {
        uint64_t handle = 0;
        uint64_t engine_handle = 0;
        uint64_t submix_handle = 0;
        uint32_t voice_kind = SCARLETT_VOICE_SOURCE;
        uint32_t format_tag = SCARLETT_AUDIO_FORMAT_IEEE_FLOAT;
        uint32_t channels = 2;
        uint32_t sample_rate_hz = 48000;
        uint32_t block_align = 0;
        uint32_t bits_per_sample = 32;
        bool playing = false;
        bool hrtf_enabled = false;
        float volume = 1.0f;
        uint32_t xapo_type = SCARLETT_XAPO_NONE;
        float xapo_param = 0.35f;
        float lpf_state[SCARLETT_AUDIO_MAX_CHANNELS]{};

        std::mutex queue_mutex;
        std::deque<float> pcm_queue; /* Already decoded & resampled to engine sample_rate_hz */
        std::deque<uint64_t> queued_buffer_frames;
        uint64_t samples_played = 0;
        std::vector<float> submix_accum;

        std::unique_ptr<OpusStreamDecoder> opus_dec;
        std::unique_ptr<Xma2StreamDecoder> xma2_dec;
        PolyphaseResampler resampler;
        HrtfBinauralRenderer hrtf;
        ConvolutionReverb reverb;
    };

    struct Engine {
        uint64_t handle = 0;
        uint32_t sample_rate_hz = 48000;
        uint32_t channels = 2;
        scarlett_audio_sink_mode_t active_sink = SCARLETT_AUDIO_SINK_HEADLESS;
        bool enable_worker = false;
        bool enable_hrtf = false;

        std::atomic<bool> worker_running{false};
        std::thread worker_thread;

        std::mutex voices_mutex;
        std::vector<uint64_t> voice_ids;
        std::vector<float> last_quantum_mix;

        std::atomic<uint32_t> xruns{0};
        std::atomic<uint64_t> total_cycles{0};

        ~Engine() {
            worker_running.store(false, std::memory_order_release);
            if (worker_thread.joinable()) {
                worker_thread.join();
            }
        }
    };

    scarlett_hresult_t render_engine_quantum_locked(Engine& eng,
                                                    uint32_t frame_count,
                                                    float* out_pcm_f32,
                                                    scarlett_audio_render_quantum_params_t* stats) noexcept;

    void apply_xapo(Voice& v, float* buf, uint32_t frames, uint32_t ch) noexcept;

    std::mutex global_mutex_;
    uint64_t next_handle_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<Engine>> engines_;
    std::unordered_map<uint64_t, std::unique_ptr<Voice>> voices_;
};

} // namespace scarlett::audio_media
