#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "audio_media_unix_abi.h"

namespace scarlett::audio_media {

/*
 * Mandatory big-endian 32-bit swap helper for XMA2 seek tables (_byteswap_ulong / bswap32).
 */
inline uint32_t scarlett_bswap32(uint32_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(v);
#else
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
#endif
}

/*
 * ADAT 512 KiB Ring Container Header (32 bytes)
 * Wraps Opus 0x704F packets in a 524,288-byte circular ring buffer.
 */
struct AdatRingHeader {
    uint32_t magic;               /* SCARLETT_ADAT_MAGIC ('ADAT' LE) */
    uint32_t version;             /* 1 */
    uint32_t sample_rate_hz;
    uint32_t channels;
    uint32_t ring_capacity_bytes; /* up to SCARLETT_ADAT_RING_CAPACITY_BYTES (512 KiB) */
    uint32_t write_offset;
    uint32_t read_offset;
    uint32_t packet_count;
};

static_assert(sizeof(AdatRingHeader) == 32, "AdatRingHeader must be 32 bytes");

struct AdatPacketHeader {
    uint32_t frame_bytes;
    uint32_t encoder_delay_samples;
};

static_assert(sizeof(AdatPacketHeader) == 8, "AdatPacketHeader must be 8 bytes");

/*
 * Parsed XMA2 Seek Table (big-endian uint32_t block sample offsets -> host uint32_t).
 */
struct Xma2SeekEntry {
    uint32_t block_index;
    uint32_t byte_offset;
    uint32_t cumulative_samples;
};

bool parse_xma2_seek_table(const uint32_t* big_endian_table,
                           uint32_t block_count,
                           uint32_t bytes_per_block,
                           std::vector<Xma2SeekEntry>& out_entries) noexcept;

bool xma2_seek_to_sample(const std::vector<Xma2SeekEntry>& entries,
                         uint32_t target_sample,
                         uint32_t& out_block_index,
                         uint32_t& out_byte_offset) noexcept;

/*
 * Real libopus decoder wrapper for Xbox Opus (0x704F) and ADAT 512 KiB ring containers.
 */
class OpusStreamDecoder {
public:
    OpusStreamDecoder() noexcept;
    ~OpusStreamDecoder() noexcept;

    OpusStreamDecoder(const OpusStreamDecoder&) = delete;
    OpusStreamDecoder& operator=(const OpusStreamDecoder&) = delete;

    bool init(uint32_t sample_rate_hz, uint32_t channels) noexcept;
    void reset() noexcept;

    /* Decodes either an ADAT 512 KiB ring container or raw/framed 0x704F Opus packets into f32 PCM. */
    bool decode(const uint8_t* data,
                uint32_t size_bytes,
                uint32_t packet_count,
                std::vector<float>& out_pcm_f32) noexcept;

    uint32_t sample_rate() const noexcept { return sample_rate_; }
    uint32_t channels() const noexcept { return channels_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t sample_rate_ = 48000;
    uint32_t channels_ = 2;
};

/*
 * Real FFmpeg libavcodec AV_CODEC_ID_XMA2 decoder wrapper.
 * Automatically normalizes 52-byte XMA2WAVEFORMATEX -> 34-byte FFmpeg extradata.
 */
class Xma2StreamDecoder {
public:
    Xma2StreamDecoder() noexcept;
    ~Xma2StreamDecoder() noexcept;

    Xma2StreamDecoder(const Xma2StreamDecoder&) = delete;
    Xma2StreamDecoder& operator=(const Xma2StreamDecoder&) = delete;

    bool init(uint32_t sample_rate_hz,
              uint32_t channels,
              const uint8_t* extradata,
              uint32_t extradata_bytes) noexcept;
    void flush() noexcept;

    /* Feeds a 2048B-aligned XMA2 packet/buffer into AV_CODEC_ID_XMA2.
     * Returns SCARLETT_OK if packet accepted (even if EAGAIN/0 frames yet),
     * or SCARLETT_E_FAIL on invalid data. */
    scarlett_hresult_t decode_packet(const uint8_t* xma2_data,
                                     uint32_t xma2_bytes,
                                     std::vector<float>& out_pcm_f32) noexcept;

    uint32_t sample_rate() const noexcept { return sample_rate_; }
    uint32_t channels() const noexcept { return channels_; }
    bool is_open() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t sample_rate_ = 48000;
    uint32_t channels_ = 2;
};

/*
 * Clean-Room IMA-ADPCM, MS-ADPCM, and xWMA decoders.
 */
bool decode_ima_adpcm_to_f32(const uint8_t* src,
                             uint32_t src_bytes,
                             uint32_t channels,
                             uint32_t block_align,
                             std::vector<float>& out_pcm_f32) noexcept;

bool decode_ms_adpcm_to_f32(const uint8_t* src,
                            uint32_t src_bytes,
                            uint32_t channels,
                            uint32_t block_align,
                            std::vector<float>& out_pcm_f32) noexcept;

bool decode_xwma_to_f32(const uint8_t* src,
                        uint32_t src_bytes,
                        uint32_t sample_rate_hz,
                        uint32_t channels,
                        uint32_t block_align,
                        std::vector<float>& out_pcm_f32) noexcept;

/*
 * APU Memory Heap & AcpHal SHAPE Context Manager.
 */
class ApuAndShapeManager {
public:
    scarlett_hresult_t create_heap(uint64_t size_bytes) noexcept;
    scarlett_hresult_t apu_alloc(uint64_t size_bytes,
                                 uint32_t alignment,
                                 uint64_t& out_ptr) noexcept;
    scarlett_hresult_t apu_free(uint64_t ptr) noexcept;

    scarlett_hresult_t acphal_create(uint64_t& out_hal_handle) noexcept;
    scarlett_hresult_t acphal_release(uint64_t hal_handle) noexcept;
    scarlett_hresult_t alloc_shape_contexts(uint64_t hal_handle,
                                            uint32_t count,
                                            uint64_t& out_ctx_handle) noexcept;
    scarlett_hresult_t free_shape_contexts(uint64_t ctx_handle) noexcept;

    scarlett_hresult_t acphal_decode_xma2(uint64_t ctx_handle,
                                          const uint8_t* src_xma2,
                                          uint32_t src_bytes,
                                          uint8_t* dst_pcm,
                                          uint32_t dst_capacity_bytes,
                                          uint32_t& out_decoded_bytes) noexcept;

private:
    struct ApuBlock {
        std::vector<uint8_t> storage;
        uint64_t aligned_addr = 0;
        uint64_t requested_size = 0;
    };

    struct ShapeContextGroup {
        uint64_t hal_handle = 0;
        uint32_t context_count = 0;
        std::unique_ptr<Xma2StreamDecoder> xma2;
    };

    std::mutex mutex_;
    uint64_t heap_limit_bytes_ = UINT64_C(256) * 1024u * 1024u;
    uint64_t heap_used_bytes_ = 0;
    std::unordered_map<uint64_t, ApuBlock> apu_allocs_;

    uint64_t next_hal_handle_ = 1;
    std::unordered_map<uint64_t, uint32_t> hals_;
    uint64_t next_ctx_handle_ = 1;
    std::unordered_map<uint64_t, ShapeContextGroup> shape_groups_;
};

} // namespace scarlett::audio_media
