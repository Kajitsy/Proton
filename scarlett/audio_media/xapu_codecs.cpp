#include "xapu_codecs.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <opus/opus.h>
}

namespace scarlett::audio_media {

namespace {

constexpr uint32_t kMaxOpusFrameSamples = 5760u; /* 120ms @ 48kHz */

uint16_t read_le16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

int16_t read_le_i16(const uint8_t* p) noexcept {
    return static_cast<int16_t>(read_le16(p));
}

uint32_t read_le32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

/* Standard IMA-ADPCM step and index tables */
constexpr int32_t kImaIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

constexpr int32_t kImaStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

int16_t ima_step_sample(uint8_t nibble, int32_t& predictor, int32_t& step_index) noexcept {
    const int32_t step = kImaStepTable[step_index];
    int32_t diff = step >> 3;
    if ((nibble & 1u) != 0u) diff += step >> 2;
    if ((nibble & 2u) != 0u) diff += step >> 1;
    if ((nibble & 4u) != 0u) diff += step;
    if ((nibble & 8u) != 0u) {
        predictor -= diff;
    } else {
        predictor += diff;
    }
    predictor = std::clamp(predictor, -32768, 32767);
    step_index = std::clamp(step_index + kImaIndexTable[nibble & 0x0Fu], 0, 88);
    return static_cast<int16_t>(predictor);
}

/* Standard MS-ADPCM predictor adaptation tables */
constexpr int32_t kMsAdaptationTable[16] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

constexpr int32_t kMsAdaptCoeff1[7] = { 256, 512, 0, 192, 240, 460, 392 };
constexpr int32_t kMsAdaptCoeff2[7] = { 0, -256, 0, 64, 0, -208, -232 };

void append_avframe_to_f32(const AVFrame* frame,
                           uint32_t expected_channels,
                           std::vector<float>& out_pcm_f32) noexcept {
    if (frame == nullptr || frame->nb_samples <= 0) {
        return;
    }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
    const int nb_ch = frame->ch_layout.nb_channels > 0
                          ? frame->ch_layout.nb_channels
                          : static_cast<int>(expected_channels);
#else
    const int nb_ch = frame->channels > 0
                          ? frame->channels
                          : static_cast<int>(expected_channels);
#endif
    const int nb_samples = frame->nb_samples;
    const size_t base = out_pcm_f32.size();
    out_pcm_f32.resize(base + static_cast<size_t>(nb_samples) * static_cast<size_t>(nb_ch));

    if (frame->format == AV_SAMPLE_FMT_FLTP) {
        for (int s = 0; s < nb_samples; ++s) {
            for (int c = 0; c < nb_ch; ++c) {
                const float* plane = reinterpret_cast<const float*>(frame->extended_data[c]);
                out_pcm_f32[base + static_cast<size_t>(s) * static_cast<size_t>(nb_ch) + static_cast<size_t>(c)] =
                    plane != nullptr ? plane[s] : 0.0f;
            }
        }
    } else if (frame->format == AV_SAMPLE_FMT_FLT) {
        const float* src = reinterpret_cast<const float*>(frame->data[0]);
        if (src != nullptr) {
            std::copy(src, src + nb_samples * nb_ch, out_pcm_f32.begin() + static_cast<std::ptrdiff_t>(base));
        }
    } else if (frame->format == AV_SAMPLE_FMT_S16P) {
        for (int s = 0; s < nb_samples; ++s) {
            for (int c = 0; c < nb_ch; ++c) {
                const int16_t* plane = reinterpret_cast<const int16_t*>(frame->extended_data[c]);
                const float val = plane != nullptr ? (static_cast<float>(plane[s]) / 32768.0f) : 0.0f;
                out_pcm_f32[base + static_cast<size_t>(s) * static_cast<size_t>(nb_ch) + static_cast<size_t>(c)] = val;
            }
        }
    } else if (frame->format == AV_SAMPLE_FMT_S16) {
        const int16_t* src = reinterpret_cast<const int16_t*>(frame->data[0]);
        if (src != nullptr) {
            for (int i = 0; i < nb_samples * nb_ch; ++i) {
                out_pcm_f32[base + static_cast<size_t>(i)] = static_cast<float>(src[i]) / 32768.0f;
            }
        }
    }
}

} // namespace

/* ============================================================================
 * XMA2 Big-Endian Seek Table Parser (bswap32)
 * ============================================================================ */

bool parse_xma2_seek_table(const uint32_t* big_endian_table,
                           uint32_t block_count,
                           uint32_t bytes_per_block,
                           std::vector<Xma2SeekEntry>& out_entries) noexcept {
    if (big_endian_table == nullptr || block_count == 0u || bytes_per_block == 0u) {
        return false;
    }
    out_entries.clear();
    out_entries.reserve(block_count);

    uint32_t prev_samples = 0u;
    for (uint32_t i = 0u; i < block_count; ++i) {
        const uint32_t host_samples = scarlett_bswap32(big_endian_table[i]);
        if (host_samples < prev_samples) {
            return false;
        }
        prev_samples = host_samples;
        Xma2SeekEntry entry{};
        entry.block_index = i;
        entry.byte_offset = i * bytes_per_block;
        entry.cumulative_samples = host_samples;
        out_entries.push_back(entry);
    }
    return true;
}

bool xma2_seek_to_sample(const std::vector<Xma2SeekEntry>& entries,
                         uint32_t target_sample,
                         uint32_t& out_block_index,
                         uint32_t& out_byte_offset) noexcept {
    if (entries.empty()) {
        return false;
    }
    for (const auto& entry : entries) {
        if (target_sample < entry.cumulative_samples) {
            out_block_index = entry.block_index;
            out_byte_offset = entry.byte_offset;
            return true;
        }
    }
    out_block_index = entries.back().block_index;
    out_byte_offset = entries.back().byte_offset;
    return true;
}

/* ============================================================================
 * Opus 0x704F & ADAT 512 KiB Ring Container Decoder (libopus)
 * ============================================================================ */

struct OpusStreamDecoder::Impl {
    OpusDecoder* dec = nullptr;

    ~Impl() noexcept {
        if (dec != nullptr) {
            opus_decoder_destroy(dec);
            dec = nullptr;
        }
    }
};

OpusStreamDecoder::OpusStreamDecoder() noexcept = default;
OpusStreamDecoder::~OpusStreamDecoder() noexcept = default;

bool OpusStreamDecoder::init(uint32_t sample_rate_hz, uint32_t channels) noexcept {
    const uint32_t sr = sample_rate_hz == 0u ? 48000u : sample_rate_hz;
    const uint32_t ch = std::clamp(channels == 0u ? 2u : channels, 1u, 2u);
    int err = OPUS_OK;
    OpusDecoder* dec = opus_decoder_create(static_cast<opus_int32>(sr),
                                           static_cast<int>(ch),
                                           &err);
    if (dec == nullptr || err != OPUS_OK) {
        return false;
    }
    impl_ = std::make_unique<Impl>();
    impl_->dec = dec;
    sample_rate_ = sr;
    channels_ = ch;
    return true;
}

void OpusStreamDecoder::reset() noexcept {
    if (impl_ && impl_->dec != nullptr) {
        opus_decoder_ctl(impl_->dec, OPUS_RESET_STATE);
    }
}

bool OpusStreamDecoder::decode(const uint8_t* data,
                               uint32_t size_bytes,
                               uint32_t packet_count,
                               std::vector<float>& out_pcm_f32) noexcept {
    if (data == nullptr || size_bytes == 0u) {
        return false;
    }
    if (!impl_ || impl_->dec == nullptr) {
        if (!init(sample_rate_, channels_)) {
            return false;
        }
    }

    out_pcm_f32.clear();
    std::vector<float> frame_buf(static_cast<size_t>(kMaxOpusFrameSamples) * channels_, 0.0f);

    auto decode_one_packet = [&](const uint8_t* pkt, uint32_t pkt_len) -> bool {
        if (pkt == nullptr || pkt_len == 0u) {
            return false;
        }
        const int decoded_frames = opus_decode_float(
            impl_->dec,
            pkt,
            static_cast<opus_int32>(pkt_len),
            frame_buf.data(),
            static_cast<int>(kMaxOpusFrameSamples),
            0);
        if (decoded_frames < 0) {
            return false;
        }
        const size_t n = static_cast<size_t>(decoded_frames) * channels_;
        out_pcm_f32.insert(out_pcm_f32.end(), frame_buf.data(), frame_buf.data() + n);
        return true;
    };

    /* Check for ADAT 512 KiB ring container header */
    if (size_bytes >= sizeof(AdatRingHeader) &&
        read_le32(data) == SCARLETT_ADAT_MAGIC) {
        AdatRingHeader hdr{};
        std::memcpy(&hdr, data, sizeof(hdr));
        if (hdr.ring_capacity_bytes == 0u ||
            hdr.ring_capacity_bytes > SCARLETT_ADAT_RING_CAPACITY_BYTES ||
            sizeof(AdatRingHeader) + hdr.ring_capacity_bytes > size_bytes) {
            return false;
        }

        if (hdr.sample_rate_hz != 0u && hdr.channels != 0u &&
            (hdr.sample_rate_hz != sample_rate_ || hdr.channels != channels_)) {
            if (!init(hdr.sample_rate_hz, hdr.channels)) {
                return false;
            }
            frame_buf.assign(static_cast<size_t>(kMaxOpusFrameSamples) * channels_, 0.0f);
        }

        const uint8_t* ring = data + sizeof(AdatRingHeader);
        const uint32_t cap = hdr.ring_capacity_bytes;
        uint32_t cursor = hdr.read_offset % cap;
        std::vector<uint8_t> scratch;

        auto read_ring_bytes = [&](uint32_t offset, uint8_t* dst, uint32_t count) {
            for (uint32_t i = 0u; i < count; ++i) {
                dst[i] = ring[(offset + i) % cap];
            }
        };

        for (uint32_t p = 0u; p < hdr.packet_count; ++p) {
            uint8_t phdr_raw[sizeof(AdatPacketHeader)];
            read_ring_bytes(cursor, phdr_raw, sizeof(AdatPacketHeader));
            AdatPacketHeader phdr{};
            std::memcpy(&phdr, phdr_raw, sizeof(phdr));
            cursor = (cursor + static_cast<uint32_t>(sizeof(AdatPacketHeader))) % cap;

            if (phdr.frame_bytes == 0u || phdr.frame_bytes > cap) {
                return false;
            }
            scratch.resize(phdr.frame_bytes);
            read_ring_bytes(cursor, scratch.data(), phdr.frame_bytes);
            cursor = (cursor + phdr.frame_bytes) % cap;

            if (!decode_one_packet(scratch.data(), phdr.frame_bytes)) {
                return false;
            }
        }
        return !out_pcm_f32.empty();
    }

    /* Multi-packet length-prefixed Xbox Opus 0x704F stream */
    if (packet_count > 1u) {
        uint32_t offset = 0u;
        for (uint32_t p = 0u; p < packet_count; ++p) {
            if (offset + 4u > size_bytes) {
                return false;
            }
            const uint32_t pkt_len = read_le32(data + offset);
            offset += 4u;
            if (pkt_len == 0u || offset + pkt_len > size_bytes) {
                return false;
            }
            if (!decode_one_packet(data + offset, pkt_len)) {
                return false;
            }
            offset += pkt_len;
        }
        return !out_pcm_f32.empty();
    }

    /* Single raw Opus packet */
    return decode_one_packet(data, size_bytes);
}

/* ============================================================================
 * Real FFmpeg libavcodec AV_CODEC_ID_XMA2 Decoder
 * ============================================================================ */

struct Xma2StreamDecoder::Impl {
    const AVCodec* codec = nullptr;
    AVCodecContext* ctx = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;

    ~Impl() noexcept {
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (pkt != nullptr) {
            av_packet_free(&pkt);
        }
        if (ctx != nullptr) {
            avcodec_free_context(&ctx);
        }
    }
};

Xma2StreamDecoder::Xma2StreamDecoder() noexcept = default;
Xma2StreamDecoder::~Xma2StreamDecoder() noexcept = default;

bool Xma2StreamDecoder::is_open() const noexcept {
    return impl_ && impl_->ctx != nullptr;
}

bool Xma2StreamDecoder::init(uint32_t sample_rate_hz,
                             uint32_t channels,
                             const uint8_t* extradata,
                             uint32_t extradata_bytes) noexcept {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    if (codec == nullptr) {
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->codec = codec;
    impl->ctx = avcodec_alloc_context3(codec);
    impl->pkt = av_packet_alloc();
    impl->frame = av_frame_alloc();
    if (impl->ctx == nullptr || impl->pkt == nullptr || impl->frame == nullptr) {
        return false;
    }

    const uint32_t sr = sample_rate_hz == 0u ? 48000u : sample_rate_hz;
    const uint32_t ch = std::clamp(channels == 0u ? 2u : channels, 1u, 8u);
    impl->ctx->sample_rate = static_cast<int>(sr);
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
    av_channel_layout_default(&impl->ctx->ch_layout, static_cast<int>(ch));
#else
    impl->ctx->channels = static_cast<int>(ch);
    impl->ctx->channel_layout = static_cast<uint64_t>(av_get_default_channel_layout(static_cast<int>(ch)));
#endif

    /* FFmpeg AV_CODEC_ID_XMA2 expects 34-byte XMA2 extradata.
     * If caller supplies full 52-byte XMA2WAVEFORMATEX (18B WAVEFORMATEX + 34B), strip 18B. */
    uint8_t norm_extra[34]{};
    if (extradata != nullptr && extradata_bytes >= 52u) {
        std::memcpy(norm_extra, extradata + 18u, 34u);
    } else if (extradata != nullptr && extradata_bytes >= 34u) {
        std::memcpy(norm_extra, extradata, 34u);
    } else {
        norm_extra[0] = 1u; /* NumStreams = 1 */
    }

    impl->ctx->extradata = static_cast<uint8_t*>(
        av_mallocz(34u + AV_INPUT_BUFFER_PADDING_SIZE));
    if (impl->ctx->extradata == nullptr) {
        return false;
    }
    std::memcpy(impl->ctx->extradata, norm_extra, 34u);
    impl->ctx->extradata_size = 34;

    if (avcodec_open2(impl->ctx, codec, nullptr) < 0) {
        return false;
    }

    impl_ = std::move(impl);
    sample_rate_ = sr;
    channels_ = ch;
    return true;
}

void Xma2StreamDecoder::flush() noexcept {
    if (impl_ && impl_->ctx != nullptr) {
        avcodec_flush_buffers(impl_->ctx);
    }
}

scarlett_hresult_t Xma2StreamDecoder::decode_packet(const uint8_t* xma2_data,
                                                    uint32_t xma2_bytes,
                                                    std::vector<float>& out_pcm_f32) noexcept {
    if (xma2_data == nullptr || xma2_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    if (!is_open()) {
        if (!init(sample_rate_, channels_, nullptr, 0u)) {
            return SCARLETT_E_FAIL;
        }
    }

    out_pcm_f32.clear();
    av_packet_unref(impl_->pkt);
    if (av_new_packet(impl_->pkt, static_cast<int>(xma2_bytes)) < 0) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    std::memcpy(impl_->pkt->data, xma2_data, xma2_bytes);

    const int send_rc = avcodec_send_packet(impl_->ctx, impl_->pkt);
    av_packet_unref(impl_->pkt);
    if (send_rc < 0 && send_rc != AVERROR(EAGAIN)) {
        return SCARLETT_E_FAIL;
    }

    while (true) {
        const int recv_rc = avcodec_receive_frame(impl_->ctx, impl_->frame);
        if (recv_rc == AVERROR(EAGAIN) || recv_rc == AVERROR_EOF) {
            break;
        }
        if (recv_rc < 0) {
            return SCARLETT_E_FAIL;
        }
        append_avframe_to_f32(impl_->frame, channels_, out_pcm_f32);
        av_frame_unref(impl_->frame);
    }

    return SCARLETT_OK;
}

/* ============================================================================
 * Clean-Room IMA-ADPCM, MS-ADPCM, and xWMA Decoders
 * ============================================================================ */

bool decode_ima_adpcm_to_f32(const uint8_t* src,
                             uint32_t src_bytes,
                             uint32_t channels,
                             uint32_t block_align,
                             std::vector<float>& out_pcm_f32) noexcept {
    if (src == nullptr || src_bytes == 0u || channels == 0u || channels > 2u) {
        return false;
    }
    const uint32_t blk = block_align == 0u ? src_bytes : block_align;
    if (blk < 4u * channels) {
        return false;
    }
    out_pcm_f32.clear();

    uint32_t pos = 0u;
    while (pos + 4u * channels <= src_bytes) {
        const uint32_t cur_blk = std::min(blk, src_bytes - pos);
        int32_t pred[2]{};
        int32_t step_idx[2]{};
        for (uint32_t c = 0u; c < channels; ++c) {
            pred[c] = read_le_i16(src + pos + c * 4u);
            step_idx[c] = std::clamp(static_cast<int32_t>(src[pos + c * 4u + 2u]), 0, 88);
            out_pcm_f32.push_back(static_cast<float>(pred[c]) / 32768.0f);
        }

        uint32_t bpos = pos + 4u * channels;
        const uint32_t bend = pos + cur_blk;
        if (channels == 1u) {
            while (bpos < bend) {
                const uint8_t byte_val = src[bpos++];
                out_pcm_f32.push_back(static_cast<float>(
                    ima_step_sample(byte_val & 0x0Fu, pred[0], step_idx[0])) / 32768.0f);
                out_pcm_f32.push_back(static_cast<float>(
                    ima_step_sample((byte_val >> 4) & 0x0Fu, pred[0], step_idx[0])) / 32768.0f);
            }
        } else {
            /* Stereo IMA-ADPCM interleaves 4-byte (8 nibble) chunks per channel */
            while (bpos + 8u <= bend) {
                int16_t ch_samples[2][8]{};
                for (uint32_t c = 0u; c < 2u; ++c) {
                    for (uint32_t k = 0u; k < 4u; ++k) {
                        const uint8_t byte_val = src[bpos + c * 4u + k];
                        ch_samples[c][k * 2u] = ima_step_sample(byte_val & 0x0Fu, pred[c], step_idx[c]);
                        ch_samples[c][k * 2u + 1u] = ima_step_sample((byte_val >> 4) & 0x0Fu, pred[c], step_idx[c]);
                    }
                }
                for (uint32_t s = 0u; s < 8u; ++s) {
                    out_pcm_f32.push_back(static_cast<float>(ch_samples[0][s]) / 32768.0f);
                    out_pcm_f32.push_back(static_cast<float>(ch_samples[1][s]) / 32768.0f);
                }
                bpos += 8u;
            }
        }
        pos += cur_blk;
    }

    return !out_pcm_f32.empty();
}

bool decode_ms_adpcm_to_f32(const uint8_t* src,
                            uint32_t src_bytes,
                            uint32_t channels,
                            uint32_t block_align,
                            std::vector<float>& out_pcm_f32) noexcept {
    if (src == nullptr || src_bytes == 0u || channels == 0u || channels > 2u) {
        return false;
    }
    const uint32_t hdr_size = 7u * channels;
    const uint32_t blk = block_align == 0u ? src_bytes : block_align;
    if (blk < hdr_size) {
        return false;
    }
    out_pcm_f32.clear();

    uint32_t pos = 0u;
    while (pos + hdr_size <= src_bytes) {
        const uint32_t cur_blk = std::min(blk, src_bytes - pos);
        uint32_t p = pos;
        int32_t coeff1[2]{};
        int32_t coeff2[2]{};
        int32_t delta[2]{};
        int32_t samp1[2]{};
        int32_t samp2[2]{};

        for (uint32_t c = 0u; c < channels; ++c) {
            const uint8_t pred_idx = std::min<uint8_t>(src[p++], 6u);
            coeff1[c] = kMsAdaptCoeff1[pred_idx];
            coeff2[c] = kMsAdaptCoeff2[pred_idx];
        }
        for (uint32_t c = 0u; c < channels; ++c) {
            delta[c] = read_le_i16(src + p);
            p += 2u;
        }
        for (uint32_t c = 0u; c < channels; ++c) {
            samp1[c] = read_le_i16(src + p);
            p += 2u;
        }
        for (uint32_t c = 0u; c < channels; ++c) {
            samp2[c] = read_le_i16(src + p);
            p += 2u;
        }

        for (uint32_t c = 0u; c < channels; ++c) {
            out_pcm_f32.push_back(static_cast<float>(samp2[c]) / 32768.0f);
        }
        for (uint32_t c = 0u; c < channels; ++c) {
            out_pcm_f32.push_back(static_cast<float>(samp1[c]) / 32768.0f);
        }

        auto step_ms = [&](uint32_t c, int32_t err_nibble) -> float {
            const int32_t signed_nib = (err_nibble >= 8) ? (err_nibble - 16) : err_nibble;
            int32_t pred = ((samp1[c] * coeff1[c]) + (samp2[c] * coeff2[c])) / 256;
            pred += signed_nib * delta[c];
            pred = std::clamp(pred, -32768, 32767);
            samp2[c] = samp1[c];
            samp1[c] = pred;
            delta[c] = std::max(16, (kMsAdaptationTable[err_nibble & 0x0F] * delta[c]) / 256);
            return static_cast<float>(pred) / 32768.0f;
        };

        const uint32_t bend = pos + cur_blk;
        if (channels == 1u) {
            while (p < bend) {
                const uint8_t b = src[p++];
                out_pcm_f32.push_back(step_ms(0u, (b >> 4) & 0x0F));
                out_pcm_f32.push_back(step_ms(0u, b & 0x0F));
            }
        } else {
            while (p < bend) {
                const uint8_t b = src[p++];
                out_pcm_f32.push_back(step_ms(0u, (b >> 4) & 0x0F));
                out_pcm_f32.push_back(step_ms(1u, b & 0x0F));
            }
        }
        pos += cur_blk;
    }
    return !out_pcm_f32.empty();
}

bool decode_xwma_to_f32(const uint8_t* src,
                        uint32_t src_bytes,
                        uint32_t sample_rate_hz,
                        uint32_t channels,
                        uint32_t block_align,
                        std::vector<float>& out_pcm_f32) noexcept {
    if (src == nullptr || src_bytes == 0u) {
        return false;
    }
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_WMAV2);
    if (codec == nullptr) {
        return false;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (ctx == nullptr || pkt == nullptr || frame == nullptr) {
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (ctx) avcodec_free_context(&ctx);
        return false;
    }

    ctx->sample_rate = static_cast<int>(sample_rate_hz == 0u ? 48000u : sample_rate_hz);
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
    av_channel_layout_default(&ctx->ch_layout, static_cast<int>(std::clamp(channels, 1u, 6u)));
#else
    ctx->channels = static_cast<int>(std::clamp(channels, 1u, 6u));
    ctx->channel_layout = static_cast<uint64_t>(av_get_default_channel_layout(ctx->channels));
#endif
    ctx->block_align = static_cast<int>(block_align == 0u ? src_bytes : block_align);
    ctx->bit_rate = 128000;

    bool ok = false;
    if (avcodec_open2(ctx, codec, nullptr) >= 0) {
        if (av_new_packet(pkt, static_cast<int>(src_bytes)) >= 0) {
            std::memcpy(pkt->data, src, src_bytes);
            if (avcodec_send_packet(ctx, pkt) >= 0) {
                ok = true;
                while (avcodec_receive_frame(ctx, frame) >= 0) {
                    append_avframe_to_f32(frame, channels, out_pcm_f32);
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(pkt);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return ok;
}

/* ============================================================================
 * APU Memory Heap & AcpHal SHAPE Context Manager
 * ============================================================================ */

scarlett_hresult_t ApuAndShapeManager::create_heap(uint64_t size_bytes) noexcept {
    if (size_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    heap_limit_bytes_ = size_bytes;
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::apu_alloc(uint64_t size_bytes,
                                                 uint32_t alignment,
                                                 uint64_t& out_ptr) noexcept {
    if (size_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    const uint32_t align = alignment < 16u ? 16u : alignment;
    if ((align & (align - 1u)) != 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (heap_used_bytes_ + size_bytes > heap_limit_bytes_) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    ApuBlock block{};
    block.storage.resize(static_cast<size_t>(size_bytes) + align, 0u);
    const uintptr_t raw = reinterpret_cast<uintptr_t>(block.storage.data());
    const uintptr_t aligned = (raw + static_cast<uintptr_t>(align - 1u)) &
                              ~static_cast<uintptr_t>(align - 1u);
    block.aligned_addr = static_cast<uint64_t>(aligned);
    block.requested_size = size_bytes;

    heap_used_bytes_ += size_bytes;
    out_ptr = block.aligned_addr;
    apu_allocs_.emplace(block.aligned_addr, std::move(block));
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::apu_free(uint64_t ptr) noexcept {
    if (ptr == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = apu_allocs_.find(ptr);
    if (it == apu_allocs_.end()) {
        return SCARLETT_E_HANDLE;
    }
    heap_used_bytes_ -= it->second.requested_size;
    apu_allocs_.erase(it);
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::acphal_create(uint64_t& out_hal_handle) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t h = next_hal_handle_++;
    hals_.emplace(h, 0u);
    out_hal_handle = h;
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::acphal_release(uint64_t hal_handle) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hals_.erase(hal_handle) == 0u) {
        return SCARLETT_E_HANDLE;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::alloc_shape_contexts(uint64_t hal_handle,
                                                            uint32_t count,
                                                            uint64_t& out_ctx_handle) noexcept {
    if (count == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto hit = hals_.find(hal_handle);
    if (hit == hals_.end()) {
        return SCARLETT_E_HANDLE;
    }

    ShapeContextGroup group{};
    group.hal_handle = hal_handle;
    group.context_count = count;
    group.xma2 = std::make_unique<Xma2StreamDecoder>();
    if (!group.xma2->init(48000u, 2u, nullptr, 0u)) {
        return SCARLETT_E_FAIL;
    }

    hit->second += count;
    const uint64_t ch = next_ctx_handle_++;
    shape_groups_.emplace(ch, std::move(group));
    out_ctx_handle = ch;
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::free_shape_contexts(uint64_t ctx_handle) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = shape_groups_.find(ctx_handle);
    if (it == shape_groups_.end()) {
        return SCARLETT_E_HANDLE;
    }
    auto hit = hals_.find(it->second.hal_handle);
    if (hit != hals_.end() && hit->second >= it->second.context_count) {
        hit->second -= it->second.context_count;
    }
    shape_groups_.erase(it);
    return SCARLETT_OK;
}

scarlett_hresult_t ApuAndShapeManager::acphal_decode_xma2(uint64_t ctx_handle,
                                                          const uint8_t* src_xma2,
                                                          uint32_t src_bytes,
                                                          uint8_t* dst_pcm,
                                                          uint32_t dst_capacity_bytes,
                                                          uint32_t& out_decoded_bytes) noexcept {
    out_decoded_bytes = 0u;
    if (src_xma2 == nullptr || src_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Xma2StreamDecoder* decoder = nullptr;
    if (ctx_handle != 0u) {
        auto it = shape_groups_.find(ctx_handle);
        if (it == shape_groups_.end()) {
            return SCARLETT_E_HANDLE;
        }
        decoder = it->second.xma2.get();
    } else {
        static Xma2StreamDecoder default_decoder;
        decoder = &default_decoder;
    }

    std::vector<float> pcm_f32;
    const scarlett_hresult_t hr = decoder->decode_packet(src_xma2, src_bytes, pcm_f32);
    if (scarlett_failed(hr)) {
        return hr;
    }

    const uint32_t need_bytes = static_cast<uint32_t>(pcm_f32.size() * sizeof(float));
    if (need_bytes > 0u) {
        if (dst_pcm == nullptr || dst_capacity_bytes < need_bytes) {
            return SCARLETT_E_INVALIDARG;
        }
        std::memcpy(dst_pcm, pcm_f32.data(), need_bytes);
    }
    out_decoded_bytes = need_bytes;
    return SCARLETT_OK;
}

} // namespace scarlett::audio_media
