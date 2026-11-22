/*
 * Proton-Scarlett — DXIL Fast-Path (Path A): DXBC Container Stripper
 *
 * Parses standard DirectX DXBC/DXIL containers, removes proprietary Xbox
 * console private chunks ("XBOX" and "XS2M"), recalculates the chunk offset
 * table, total container byte length, and DXBC MD5 integrity digest, and
 * validates the presence of a standard "DXIL" or "ILDB" shader bytecode chunk
 * ready for stock dxil-spirv ingestion.
 */

#include "d3d12_xs_internal.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace scarlett::d3d12xs::unixlib {

namespace {

constexpr uint32_t kDxbcMagic = 0x43425844u;       /* 'DXBC' */
constexpr uint32_t kFourccXbox = 0x584F4258u;      /* 'XBOX' */
constexpr uint32_t kFourccXs2m = 0x4D325358u;      /* 'XS2M' */
constexpr uint32_t kFourccDxil = 0x4C495844u;      /* 'DXIL' */
constexpr uint32_t kFourccIldb = 0x42444C49u;      /* 'ILDB' */
constexpr uint64_t kDxbcHeaderSize = 32u;

uint32_t read_le32(const uint8_t *p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

void write_le32(uint8_t *p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24u) & 0xFFu);
}

uint32_t rotl32(uint32_t x, uint32_t s) noexcept {
    return (x << s) | (x >> (32u - s));
}

/*
 * Clean-room DXBC MD5 digest over container bytes [20 .. total_size).
 * Matches the modified MD5 length-word layout expected by DirectX / dxil-spirv.
 */
void compute_dxbc_digest(const uint8_t *data, uint32_t total_size, uint8_t out_digest[16]) noexcept {
    static constexpr uint32_t kShift[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    static constexpr uint32_t kSinTable[64] = {
        0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,
        0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
        0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,
        0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
        0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,
        0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
        0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,
        0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
        0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,
        0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
        0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,
        0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
        0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,
        0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
        0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,
        0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u
    };

    uint32_t state[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    auto process_block = [&](const uint8_t block[64]) {
        uint32_t m[16];
        for (uint32_t i = 0; i < 16u; ++i) {
            m[i] = read_le32(block + i * 4u);
        }
        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        for (uint32_t i = 0; i < 64u; ++i) {
            uint32_t f = 0;
            uint32_t g = 0;
            if (i < 16u) {
                f = (b & c) | ((~b) & d);
                g = i;
            } else if (i < 32u) {
                f = (d & b) | ((~d) & c);
                g = (5u * i + 1u) & 15u;
            } else if (i < 48u) {
                f = b ^ c ^ d;
                g = (3u * i + 5u) & 15u;
            } else {
                f = c ^ (b | (~d));
                g = (7u * i) & 15u;
            }
            const uint32_t tmp = d;
            d = c;
            c = b;
            b = b + rotl32(a + f + kSinTable[i] + m[g], kShift[i]);
            a = tmp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    };

    const uint8_t *payload = data + 20u;
    const uint32_t payload_len = total_size - 20u;
    uint32_t full_blocks = payload_len / 64u;
    for (uint32_t bi = 0; bi < full_blocks; ++bi) {
        process_block(payload + bi * 64u);
    }

    const uint32_t rem = payload_len % 64u;
    uint8_t tail[64];
    std::memset(tail, 0, sizeof(tail));
    if (rem > 0u) {
        std::memcpy(tail, payload + full_blocks * 64u, rem);
    }
    tail[rem] = 0x80u;
    if (rem >= 56u) {
        process_block(tail);
        std::memset(tail, 0, sizeof(tail));
    }
    /* DXBC stores bit length in word 0 and (byte_len >> 29 | 1) in word 15 for last block */
    const uint32_t bit_len = payload_len * 8u;
    write_le32(tail + 56u, bit_len);
    write_le32(tail + 60u, (payload_len >> 29u) | 1u);
    process_block(tail);

    for (uint32_t i = 0; i < 4u; ++i) {
        write_le32(out_digest + i * 4u, state[i]);
    }
}

struct ParsedChunk {
    uint32_t fourcc = 0;
    const uint8_t *payload = nullptr;
    uint32_t size = 0;
};

} // namespace

scarlett_hresult_t strip_xbox_dxil_container(
    const uint8_t *src,
    uint64_t src_size,
    uint8_t *dst,
    uint64_t dst_capacity,
    scarlett_d3d12xs_strip_dxil_params_t &out_info) noexcept {
    out_info.out_dst_size = 0u;
    out_info.out_input_chunks = 0u;
    out_info.out_output_chunks = 0u;
    out_info.out_stripped_chunks = 0u;
    out_info.out_has_valid_dxil = 0u;

    if (src == nullptr || src_size < kDxbcHeaderSize || src_size > UINT64_C(0x40000000)) {
        out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out_info.out_status;
    }

    const uint32_t magic = read_le32(src + 0u);
    if (magic != kDxbcMagic) {
        out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out_info.out_status;
    }

    const uint32_t version = read_le32(src + 20u);
    const uint32_t declared_total_size = read_le32(src + 24u);
    const uint32_t chunk_count = read_le32(src + 28u);

    if (declared_total_size < kDxbcHeaderSize || declared_total_size > src_size ||
        chunk_count == 0u || chunk_count > 1024u) {
        out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out_info.out_status;
    }

    const uint64_t offset_table_end = kDxbcHeaderSize + static_cast<uint64_t>(chunk_count) * 4u;
    if (offset_table_end > declared_total_size) {
        out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out_info.out_status;
    }

    std::vector<ParsedChunk> kept_chunks;
    kept_chunks.reserve(chunk_count);

    uint32_t stripped = 0u;
    bool has_dxil_or_ildb = false;

    for (uint32_t i = 0; i < chunk_count; ++i) {
        const uint32_t chunk_off = read_le32(src + kDxbcHeaderSize + i * 4u);
        if (chunk_off < offset_table_end ||
            static_cast<uint64_t>(chunk_off) + 8u > declared_total_size) {
            out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return out_info.out_status;
        }

        const uint32_t fourcc = read_le32(src + chunk_off);
        const uint32_t chunk_size = read_le32(src + chunk_off + 4u);
        if (static_cast<uint64_t>(chunk_off) + 8u + static_cast<uint64_t>(chunk_size) >
            declared_total_size) {
            out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return out_info.out_status;
        }

        if (fourcc == kFourccXbox || fourcc == kFourccXs2m) {
            ++stripped;
            continue;
        }

        if (fourcc == kFourccDxil || fourcc == kFourccIldb) {
            has_dxil_or_ildb = true;
        }

        ParsedChunk c{};
        c.fourcc = fourcc;
        c.payload = src + chunk_off + 8u;
        c.size = chunk_size;
        kept_chunks.push_back(c);
    }

    out_info.out_input_chunks = chunk_count;
    out_info.out_output_chunks = static_cast<uint32_t>(kept_chunks.size());
    out_info.out_stripped_chunks = stripped;
    out_info.out_has_valid_dxil = has_dxil_or_ildb ? 1u : 0u;

    /* DXIL Fast-Path A requires a standard DXIL or ILDB bytecode chunk */
    if (!has_dxil_or_ildb || kept_chunks.empty()) {
        out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out_info.out_status;
    }

    /* Compute exact output size with 4-byte chunk alignment */
    uint64_t out_total = kDxbcHeaderSize + static_cast<uint64_t>(kept_chunks.size()) * 4u;
    std::vector<uint32_t> new_offsets;
    new_offsets.reserve(kept_chunks.size());

    for (const auto &c : kept_chunks) {
        const uint64_t aligned_off = (out_total + 3u) & ~UINT64_C(3);
        if (aligned_off > UINT32_MAX) {
            out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return out_info.out_status;
        }
        new_offsets.push_back(static_cast<uint32_t>(aligned_off));
        out_total = aligned_off + 8u + static_cast<uint64_t>(c.size);
    }
    const uint64_t final_size = (out_total + 3u) & ~UINT64_C(3);
    if (final_size > UINT32_MAX) {
        out_info.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out_info.out_status;
    }

    out_info.out_dst_size = final_size;

    /* Size query mode if dst == nullptr */
    if (dst == nullptr) {
        out_info.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (dst_capacity < final_size) {
        out_info.out_status = SCARLETT_E_INVALIDARG;
        return out_info.out_status;
    }

    std::memset(dst, 0, static_cast<size_t>(final_size));
    write_le32(dst + 0u, kDxbcMagic);
    write_le32(dst + 20u, version);
    write_le32(dst + 24u, static_cast<uint32_t>(final_size));
    write_le32(dst + 28u, static_cast<uint32_t>(kept_chunks.size()));

    for (size_t i = 0; i < kept_chunks.size(); ++i) {
        write_le32(dst + kDxbcHeaderSize + i * 4u, new_offsets[i]);
        uint8_t *chunk_ptr = dst + new_offsets[i];
        write_le32(chunk_ptr + 0u, kept_chunks[i].fourcc);
        write_le32(chunk_ptr + 4u, kept_chunks[i].size);
        if (kept_chunks[i].size > 0u) {
            std::memcpy(chunk_ptr + 8u, kept_chunks[i].payload, kept_chunks[i].size);
        }
    }

    compute_dxbc_digest(dst, static_cast<uint32_t>(final_size), dst + 4u);
    out_info.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

} // namespace scarlett::d3d12xs::unixlib
