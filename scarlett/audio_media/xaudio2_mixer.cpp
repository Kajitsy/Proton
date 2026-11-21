#include "xaudio2_mixer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace scarlett::audio_media {

namespace {

std::vector<float> build_default_reverb_ir(uint32_t sample_rate_hz) {
    const uint32_t len = std::min<uint32_t>(256u, std::max<uint32_t>(32u, sample_rate_hz / 200u));
    std::vector<float> ir(len, 0.0f);
    ir[0] = 1.0f;
    for (uint32_t i = 1u; i < len; ++i) {
        ir[i] = 0.25f * std::exp(-static_cast<float>(i) / 36.0f);
    }
    return ir;
}

} // namespace

XAudio2MixerManager::~XAudio2MixerManager() noexcept {
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        ids.reserve(engines_.size());
        for (const auto& kv : engines_) {
            ids.push_back(kv.first);
        }
    }
    for (uint64_t id : ids) {
        (void)destroy_engine(id);
    }
}

scarlett_hresult_t XAudio2MixerManager::create_engine(
    const scarlett_audio_engine_create_params_t& req,
    uint64_t& out_engine_handle,
    uint32_t& out_active_sink) noexcept {
    auto eng = std::make_unique<Engine>();
    eng->sample_rate_hz = req.sample_rate_hz == 0u ? 48000u : req.sample_rate_hz;
    eng->channels = std::clamp(req.channels == 0u ? 2u : req.channels, 1u, SCARLETT_AUDIO_MAX_CHANNELS);
    eng->active_sink = probe_audio_sink(static_cast<scarlett_audio_sink_mode_t>(req.sink_mode));
    eng->enable_worker = (req.enable_worker_thread != 0u);
    eng->enable_hrtf = (req.enable_hrtf != 0u);
    eng->last_quantum_mix.assign(SCARLETT_AUDIO_QUANTUM_FRAMES * eng->channels, 0.0f);

    std::lock_guard<std::mutex> lock(global_mutex_);
    const uint64_t id = next_handle_++;
    eng->handle = id;
    out_engine_handle = id;
    out_active_sink = static_cast<uint32_t>(eng->active_sink);
    engines_.emplace(id, std::move(eng));

    if (req.enable_worker_thread != 0u) {
        return start_engine(id);
    }
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::destroy_engine(uint64_t engine_handle) noexcept {
    std::unique_ptr<Engine> eng_owned;
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        auto it = engines_.find(engine_handle);
        if (it == engines_.end()) {
            return SCARLETT_E_HANDLE;
        }
        it->second->worker_running.store(false, std::memory_order_release);
        eng_owned = std::move(it->second);
        engines_.erase(it);
    }

    if (eng_owned->worker_thread.joinable()) {
        eng_owned->worker_thread.join();
    }

    std::lock_guard<std::mutex> lock(global_mutex_);
    for (uint64_t vid : eng_owned->voice_ids) {
        voices_.erase(vid);
    }
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::start_engine(uint64_t engine_handle) noexcept {
    Engine* eng = nullptr;
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        auto it = engines_.find(engine_handle);
        if (it == engines_.end()) {
            return SCARLETT_E_HANDLE;
        }
        eng = it->second.get();
    }

    if (eng->worker_running.exchange(true, std::memory_order_acq_rel)) {
        return SCARLETT_OK;
    }

    eng->worker_thread = std::thread([this, eng]() {
        using clock = std::chrono::steady_clock;
        const auto quantum_dur = std::chrono::microseconds(
            (SCARLETT_AUDIO_QUANTUM_FRAMES * 1000000ull) / eng->sample_rate_hz);
        auto next_tick = clock::now() + quantum_dur;
        while (eng->worker_running.load(std::memory_order_acquire)) {
            {
                std::lock_guard<std::mutex> glock(global_mutex_);
                if (engines_.find(eng->handle) == engines_.end()) {
                    break;
                }
                (void)render_engine_quantum_locked(*eng, SCARLETT_AUDIO_QUANTUM_FRAMES, nullptr, nullptr);
            }
            std::this_thread::sleep_until(next_tick);
            next_tick += quantum_dur;
            const auto now = clock::now();
            if (next_tick + quantum_dur * 4 < now) {
                next_tick = now + quantum_dur;
            }
        }
    });
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::stop_engine(uint64_t engine_handle) noexcept {
    std::thread to_join;
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        auto it = engines_.find(engine_handle);
        if (it == engines_.end()) {
            return SCARLETT_E_HANDLE;
        }
        it->second->worker_running.store(false, std::memory_order_release);
        to_join = std::move(it->second->worker_thread);
    }
    if (to_join.joinable()) {
        to_join.join();
    }
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::create_voice(
    const scarlett_audio_voice_create_params_t& req,
    uint64_t& out_voice_handle) noexcept {
    std::lock_guard<std::mutex> lock(global_mutex_);
    auto eit = engines_.find(req.engine_handle);
    if (eit == engines_.end()) {
        return SCARLETT_E_HANDLE;
    }
    Engine& eng = *eit->second;

    if (req.submix_voice_handle != 0u && voices_.find(req.submix_voice_handle) == voices_.end()) {
        return SCARLETT_E_INVALIDARG;
    }

    auto v = std::make_unique<Voice>();
    v->engine_handle = req.engine_handle;
    v->submix_handle = req.submix_voice_handle;
    v->voice_kind = req.voice_kind;
    v->format_tag = req.format_tag == 0u ? SCARLETT_AUDIO_FORMAT_IEEE_FLOAT : req.format_tag;
    v->channels = std::clamp(req.channels == 0u ? eng.channels : req.channels, 1u, SCARLETT_AUDIO_MAX_CHANNELS);
    v->sample_rate_hz = req.sample_rate_hz == 0u ? eng.sample_rate_hz : req.sample_rate_hz;
    v->block_align = req.block_align;
    v->bits_per_sample = req.bits_per_sample == 0u ? 16u : req.bits_per_sample;
    v->hrtf_enabled = (req.enable_hrtf != 0u) || eng.enable_hrtf;
    v->xapo_type = req.xapo_type;
    v->playing = (v->voice_kind != SCARLETT_VOICE_SOURCE);

    v->resampler.configure(v->sample_rate_hz, eng.sample_rate_hz, v->channels);
    v->hrtf.configure(eng.sample_rate_hz);
    auto ir = build_default_reverb_ir(eng.sample_rate_hz);
    v->reverb.configure(ir.data(), static_cast<uint32_t>(ir.size()), 0.35f);

    if (v->format_tag == SCARLETT_AUDIO_FORMAT_OPUS ||
        v->format_tag == SCARLETT_AUDIO_FORMAT_ADAT_OPUS) {
        v->opus_dec = std::make_unique<OpusStreamDecoder>();
        if (!v->opus_dec->init(v->sample_rate_hz, v->channels)) {
            return SCARLETT_E_FAIL;
        }
    } else if (v->format_tag == SCARLETT_AUDIO_FORMAT_XMA2) {
        v->xma2_dec = std::make_unique<Xma2StreamDecoder>();
        const uint8_t* extra = req.extradata_addr != 0u
                                   ? reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(req.extradata_addr))
                                   : nullptr;
        if (!v->xma2_dec->init(v->sample_rate_hz, v->channels, extra, req.extradata_bytes)) {
            return SCARLETT_E_FAIL;
        }
    }

    const uint64_t vid = next_handle_++;
    v->handle = vid;
    out_voice_handle = vid;

    {
        std::lock_guard<std::mutex> vlock(eng.voices_mutex);
        eng.voice_ids.push_back(vid);
    }
    voices_.emplace(vid, std::move(v));
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::destroy_voice(uint64_t voice_handle) noexcept {
    std::lock_guard<std::mutex> lock(global_mutex_);
    auto vit = voices_.find(voice_handle);
    if (vit == voices_.end()) {
        return SCARLETT_E_HANDLE;
    }
    const uint64_t eid = vit->second->engine_handle;
    auto eit = engines_.find(eid);
    if (eit != engines_.end()) {
        std::lock_guard<std::mutex> vlock(eit->second->voices_mutex);
        auto& vec = eit->second->voice_ids;
        vec.erase(std::remove(vec.begin(), vec.end(), voice_handle), vec.end());
    }
    voices_.erase(vit);
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::voice_control(
    scarlett_audio_voice_ctrl_params_t& params) noexcept {
    std::lock_guard<std::mutex> lock(global_mutex_);
    auto vit = voices_.find(params.voice_handle);
    if (vit == voices_.end()) {
        return SCARLETT_E_HANDLE;
    }
    Voice& v = *vit->second;

    switch (params.ctrl_op) {
        case SCARLETT_VOICE_CTRL_START:
            v.playing = true;
            return SCARLETT_OK;
        case SCARLETT_VOICE_CTRL_STOP:
            v.playing = false;
            return SCARLETT_OK;
        case SCARLETT_VOICE_CTRL_SET_VOLUME:
            v.volume = std::max(0.0f, params.param_f0);
            return SCARLETT_OK;
        case SCARLETT_VOICE_CTRL_GET_VOLUME:
            params.param_f0 = v.volume;
            return SCARLETT_OK;
        case SCARLETT_VOICE_CTRL_FLUSH: {
            std::lock_guard<std::mutex> qlock(v.queue_mutex);
            v.pcm_queue.clear();
            v.resampler.reset();
            v.hrtf.reset();
            v.reverb.reset();
            if (v.opus_dec) v.opus_dec->reset();
            if (v.xma2_dec) v.xma2_dec->flush();
            return SCARLETT_OK;
        }
        case SCARLETT_VOICE_CTRL_SET_HRTF_POS:
            v.hrtf_enabled = true;
            v.hrtf.set_direction(params.param_f0, params.param_f1, params.param_f2);
            return SCARLETT_OK;
        case SCARLETT_VOICE_CTRL_SET_XAPO:
            v.xapo_type = params.param_u32;
            v.xapo_param = params.param_f0;
            return SCARLETT_OK;
        case SCARLETT_VOICE_CTRL_GET_DETAILS:
            params.out_channels = v.channels;
            params.out_sample_rate = v.sample_rate_hz;
            params.param_f0 = v.volume;
            return SCARLETT_OK;
        default:
            return SCARLETT_E_INVALIDARG;
    }
}

scarlett_hresult_t XAudio2MixerManager::submit_voice_buffer(
    const scarlett_audio_voice_buffer_params_t& req) noexcept {
    if (req.buffer_addr == 0u || req.buffer_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(req.buffer_addr));

    Voice* v = nullptr;
    uint32_t eng_rate = 48000u;
    {
        std::lock_guard<std::mutex> lock(global_mutex_);
        auto vit = voices_.find(req.voice_handle);
        if (vit == voices_.end()) {
            return SCARLETT_E_HANDLE;
        }
        v = vit->second.get();
        auto eit = engines_.find(v->engine_handle);
        if (eit != engines_.end()) {
            eng_rate = eit->second->sample_rate_hz;
        }
    }

    std::vector<float> decoded;
    switch (v->format_tag) {
        case SCARLETT_AUDIO_FORMAT_IEEE_FLOAT: {
            const size_t count = req.buffer_bytes / sizeof(float);
            decoded.resize(count);
            std::memcpy(decoded.data(), src, count * sizeof(float));
            break;
        }
        case SCARLETT_AUDIO_FORMAT_PCM_S16: {
            const size_t count = req.buffer_bytes / sizeof(int16_t);
            decoded.resize(count);
            const int16_t* s16 = reinterpret_cast<const int16_t*>(src);
            for (size_t i = 0; i < count; ++i) {
                decoded[i] = static_cast<float>(s16[i]) / 32768.0f;
            }
            break;
        }
        case SCARLETT_AUDIO_FORMAT_IMA_ADPCM:
            if (!decode_ima_adpcm_to_f32(src, req.buffer_bytes, v->channels, v->block_align, decoded)) {
                return SCARLETT_E_FAIL;
            }
            break;
        case SCARLETT_AUDIO_FORMAT_MS_ADPCM:
            if (!decode_ms_adpcm_to_f32(src, req.buffer_bytes, v->channels, v->block_align, decoded)) {
                return SCARLETT_E_FAIL;
            }
            break;
        case SCARLETT_AUDIO_FORMAT_OPUS:
        case SCARLETT_AUDIO_FORMAT_ADAT_OPUS:
            if (!v->opus_dec || !v->opus_dec->decode(src, req.buffer_bytes, req.packet_count, decoded)) {
                return SCARLETT_E_FAIL;
            }
            if (v->opus_dec->sample_rate() != v->sample_rate_hz ||
                v->opus_dec->channels() != v->channels) {
                v->sample_rate_hz = v->opus_dec->sample_rate();
                v->channels = v->opus_dec->channels();
                v->resampler.configure(v->sample_rate_hz, eng_rate, v->channels);
            }
            break;
        case SCARLETT_AUDIO_FORMAT_XMA2:
            if (!v->xma2_dec) {
                return SCARLETT_E_FAIL;
            }
            if (scarlett_failed(v->xma2_dec->decode_packet(src, req.buffer_bytes, decoded))) {
                return SCARLETT_E_FAIL;
            }
            break;
        case SCARLETT_AUDIO_FORMAT_XWMA:
            if (!decode_xwma_to_f32(src, req.buffer_bytes, v->sample_rate_hz, v->channels,
                                    v->block_align, decoded)) {
                return SCARLETT_E_FAIL;
            }
            break;
        default:
            return SCARLETT_E_NOTIMPL;
    }

    if (decoded.empty()) {
        return SCARLETT_OK;
    }

    const uint32_t in_frames = static_cast<uint32_t>(decoded.size() / v->channels);
    std::vector<float> resampled;
    const float* final_ptr = decoded.data();
    uint32_t final_samples = static_cast<uint32_t>(decoded.size());

    if (!v->resampler.is_bypass()) {
        const uint32_t max_out = static_cast<uint32_t>(
            std::ceil(static_cast<double>(in_frames) *
                      static_cast<double>(eng_rate) /
                      static_cast<double>(v->sample_rate_hz))) + 32u;
        resampled.resize(static_cast<size_t>(max_out) * v->channels, 0.0f);
        const uint32_t produced = v->resampler.process(decoded.data(), in_frames,
                                                       resampled.data(), max_out);
        final_ptr = resampled.data();
        final_samples = produced * v->channels;
    }

    const uint32_t repeats = std::max(1u, req.loop_count + 1u);
    std::lock_guard<std::mutex> qlock(v->queue_mutex);
    for (uint32_t r = 0u; r < repeats; ++r) {
        v->pcm_queue.insert(v->pcm_queue.end(), final_ptr, final_ptr + final_samples);
    }
    return SCARLETT_OK;
}

void XAudio2MixerManager::apply_xapo(Voice& v, float* buf, uint32_t frames, uint32_t ch) noexcept {
    if (v.xapo_type == SCARLETT_XAPO_LOWPASS) {
        const float alpha = std::clamp(v.xapo_param <= 0.0f ? 0.25f : v.xapo_param, 0.02f, 1.0f);
        for (uint32_t f = 0u; f < frames; ++f) {
            for (uint32_t c = 0u; c < ch; ++c) {
                const float x = buf[f * ch + c];
                v.lpf_state[c] += alpha * (x - v.lpf_state[c]);
                buf[f * ch + c] = v.lpf_state[c];
            }
        }
    } else if (v.xapo_type == SCARLETT_XAPO_REVERB && ch >= 1u) {
        std::vector<float> mono_in(frames, 0.0f);
        std::vector<float> mono_out(frames, 0.0f);
        const float inv_ch = 1.0f / static_cast<float>(ch);
        for (uint32_t f = 0u; f < frames; ++f) {
            float s = 0.0f;
            for (uint32_t c = 0u; c < ch; ++c) {
                s += buf[f * ch + c];
            }
            mono_in[f] = s * inv_ch;
        }
        v.reverb.process_block(mono_in.data(), mono_out.data(), frames);
        for (uint32_t f = 0u; f < frames; ++f) {
            const float delta = mono_out[f] - mono_in[f];
            for (uint32_t c = 0u; c < ch; ++c) {
                buf[f * ch + c] += delta;
            }
        }
    }
}

scarlett_hresult_t XAudio2MixerManager::render_engine_quantum_locked(
    Engine& eng,
    uint32_t frame_count,
    float* out_pcm_f32,
    scarlett_audio_render_quantum_params_t* stats) noexcept {
    const uint32_t frames = frame_count == 0u ? SCARLETT_AUDIO_QUANTUM_FRAMES : frame_count;
    const uint32_t ch = eng.channels;
    std::vector<float> mix_buf(static_cast<size_t>(frames) * ch, 0.0f);

    std::vector<uint64_t> voice_ids;
    {
        std::lock_guard<std::mutex> vlock(eng.voices_mutex);
        voice_ids = eng.voice_ids;
    }

    uint32_t active_voices = 0u;
    uint32_t active_resamplers = 0u;
    uint32_t active_xma = 0u;

    /* Clear submix accumulators */
    for (uint64_t vid : voice_ids) {
        auto vit = voices_.find(vid);
        if (vit != voices_.end() && vit->second->voice_kind == SCARLETT_VOICE_SUBMIX) {
            vit->second->submix_accum.assign(static_cast<size_t>(frames) * ch, 0.0f);
        }
    }

    /* Mix source voices -> submix or master */
    std::vector<float> voice_scratch;
    for (uint64_t vid : voice_ids) {
        auto vit = voices_.find(vid);
        if (vit == voices_.end()) {
            continue;
        }
        Voice& v = *vit->second;
        if (v.voice_kind != SCARLETT_VOICE_SOURCE || !v.playing) {
            continue;
        }

        const size_t need_samples = static_cast<size_t>(frames) * v.channels;
        voice_scratch.assign(need_samples, 0.0f);
        bool had_data = false;
        {
            std::lock_guard<std::mutex> qlock(v.queue_mutex);
            const size_t avail = std::min(need_samples, v.pcm_queue.size());
            if (avail > 0u) {
                had_data = true;
                for (size_t i = 0u; i < avail; ++i) {
                    voice_scratch[i] = v.pcm_queue.front() * v.volume;
                    v.pcm_queue.pop_front();
                }
            }
        }

        if (!had_data) {
            continue;
        }
        ++active_voices;
        if (!v.resampler.is_bypass()) {
            ++active_resamplers;
        }
        if (v.format_tag == SCARLETT_AUDIO_FORMAT_XMA2) {
            ++active_xma;
        }

        apply_xapo(v, voice_scratch.data(), frames, v.channels);

        float* target = mix_buf.data();
        if (v.submix_handle != 0u) {
            auto sit = voices_.find(v.submix_handle);
            if (sit != voices_.end() && sit->second->submix_accum.size() == mix_buf.size()) {
                target = sit->second->submix_accum.data();
            }
        }

        if (v.hrtf_enabled && ch == 2u) {
            v.hrtf.accumulate_stereo(voice_scratch.data(), v.channels, target, frames, 1.0f);
        } else {
            for (uint32_t f = 0u; f < frames; ++f) {
                for (uint32_t c = 0u; c < ch; ++c) {
                    const uint32_t sc = std::min(c, v.channels - 1u);
                    target[f * ch + c] += voice_scratch[f * v.channels + sc];
                }
            }
        }
    }

    /* Process submix voices -> master */
    for (uint64_t vid : voice_ids) {
        auto vit = voices_.find(vid);
        if (vit == voices_.end() || vit->second->voice_kind != SCARLETT_VOICE_SUBMIX) {
            continue;
        }
        Voice& sub = *vit->second;
        if (sub.submix_accum.size() != mix_buf.size()) {
            continue;
        }
        apply_xapo(sub, sub.submix_accum.data(), frames, ch);
        for (size_t i = 0u; i < mix_buf.size(); ++i) {
            mix_buf[i] += sub.submix_accum[i] * sub.volume;
        }
    }

    /* Soft-clip mastering stage */
    for (float& s : mix_buf) {
        s = std::clamp(s, -1.0f, 1.0f);
    }

    eng.last_quantum_mix = mix_buf;
    const uint64_t cycles = eng.total_cycles.fetch_add(
        static_cast<uint64_t>(frames) * std::max(1u, active_voices),
        std::memory_order_relaxed) + static_cast<uint64_t>(frames);

    if (out_pcm_f32 != nullptr) {
        std::copy(mix_buf.begin(), mix_buf.end(), out_pcm_f32);
    }

    if (stats != nullptr) {
        stats->out_frames_rendered = frames;
        stats->out_active_voices = active_voices;
        stats->out_xruns = eng.xruns.load(std::memory_order_relaxed);
        stats->out_resamplers = active_resamplers;
        stats->out_xma_voices = active_xma;
        stats->out_total_cycles = cycles;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t XAudio2MixerManager::render_quantum(
    scarlett_audio_render_quantum_params_t& params) noexcept {
    std::lock_guard<std::mutex> lock(global_mutex_);
    auto eit = engines_.find(params.engine_handle);
    if (eit == engines_.end()) {
        return SCARLETT_E_HANDLE;
    }
    float* out_ptr = params.out_pcm_f32_addr != 0u
                         ? reinterpret_cast<float*>(static_cast<uintptr_t>(params.out_pcm_f32_addr))
                         : nullptr;
    return render_engine_quantum_locked(*eit->second, params.frame_count, out_ptr, &params);
}

} // namespace scarlett::audio_media
