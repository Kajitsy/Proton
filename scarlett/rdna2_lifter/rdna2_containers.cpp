/*
 * Proton-Scarlett — Path A (DXIL Fast-Path) & Path B (Xbox RDNA2 Containers)
 *
 * Path A:
 *   Strips proprietary Xbox chunks ('XBOX', 'XS2M') from DXBC/DXIL containers,
 *   recomputes offsets & DXBC MD5 digest, remaps Xbox resource bindings, and
 *   extracts clean DXIL/ILDB bytecode ready for dxil-spirv.
 *
 * Path B:
 *   Unpacks native Xbox Series X|S RDNA2 shader containers:
 *     - XS2  ("XS2\0" / "XS2B")
 *     - FSSM ("FSSM")
 *     - PS09 ("PS\x00\x09")
 *     - SGD5 ("SGD5")
 *   Extracts stage metadata (VS, PS, CS, NGG Mesh, Task/Amplification, GS, HS,
 *   DS, DXR stages), wave size, register counts, LDS size, workgroup size,
 *   resource bindings, and the raw 32/64-bit GFX10.3 instruction word stream.
 */

#include "rdna2_lifter.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace scarlett::rdna2 {

namespace {

constexpr uint32_t kFourccDxbc = 0x43425844u; /* "DXBC" */
constexpr uint32_t kFourccXbox = 0x584F4258u; /* "XBOX" */
constexpr uint32_t kFourccXs2m = 0x4D325358u; /* "XS2M" */
constexpr uint32_t kFourccDxil = 0x4C495844u; /* "DXIL" */
constexpr uint32_t kFourccIldb = 0x42444C49u; /* "ILDB" */
constexpr uint32_t kFourccShex = 0x58454853u; /* "SHEX" */
constexpr uint32_t kFourccSfi0 = 0x30494653u; /* "SFI0" */

constexpr uint32_t kMagicXs2Null = 0x00325358u; /* "XS2\0" */
constexpr uint32_t kMagicXs2Bin  = 0x42325358u; /* "XS2B" */
constexpr uint32_t kMagicFssm    = 0x4D535346u; /* "FSSM" */
constexpr uint32_t kMagicPs09    = 0x09005350u; /* "PS\x00\x09" */
constexpr uint32_t kMagicSgd5    = 0x35444753u; /* "SGD5" */

uint32_t read_le32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

uint16_t read_le16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8u);
}

void write_le32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24u) & 0xFFu);
}

void append_le32(std::vector<uint8_t>& buf, uint32_t v) {
    uint8_t tmp[4];
    write_le32(tmp, v);
    buf.insert(buf.end(), tmp, tmp + 4);
}

void append_le16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<uint8_t>((v >> 8u) & 0xFFu));
}

uint32_t rotl32(uint32_t x, uint32_t s) noexcept {
    return (x << s) | (x >> (32u - s));
}

/* Modified MD5 matching DirectX DXBC container checksum specification */
void compute_dxbc_md5(const uint8_t* data, uint32_t total_size, uint8_t out_digest[16]) noexcept {
    static constexpr uint32_t kShift[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    static constexpr uint32_t kSin[64] = {
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

    uint32_t st[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    auto step_block = [&](const uint8_t block[64]) {
        uint32_t m[16];
        for (uint32_t i = 0; i < 16u; ++i) {
            m[i] = read_le32(block + i * 4u);
        }
        uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
        for (uint32_t i = 0; i < 64u; ++i) {
            uint32_t f = 0, g = 0;
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
            uint32_t tmp = d;
            d = c;
            c = b;
            b = b + rotl32(a + f + kSin[i] + m[g], kShift[i]);
            a = tmp;
        }
        st[0] += a;
        st[1] += b;
        st[2] += c;
        st[3] += d;
    };

    const uint8_t* payload = data + 20u;
    const uint32_t payload_len = total_size - 20u;
    const uint32_t full = payload_len / 64u;
    for (uint32_t i = 0; i < full; ++i) {
        step_block(payload + i * 64u);
    }
    uint8_t tail[64];
    std::memset(tail, 0, sizeof(tail));
    const uint32_t rem = payload_len % 64u;
    if (rem > 0u) {
        std::memcpy(tail, payload + full * 64u, rem);
    }
    tail[rem] = 0x80u;
    if (rem >= 56u) {
        step_block(tail);
        std::memset(tail, 0, sizeof(tail));
    }
    write_le32(tail + 56u, payload_len * 8u);
    write_le32(tail + 60u, (payload_len >> 29u) | 1u);
    step_block(tail);

    for (uint32_t i = 0; i < 4u; ++i) {
        write_le32(out_digest + i * 4u, st[i]);
    }
}

bool valid_stage_code(uint32_t v) noexcept {
    return v <= static_cast<uint32_t>(ShaderStage::DxrCallable);
}

ShaderStage map_dxil_program_type(uint16_t program_type) noexcept {
    switch (program_type) {
        case 0:  return ShaderStage::Pixel;
        case 1:  return ShaderStage::Vertex;
        case 2:  return ShaderStage::Geometry;
        case 3:  return ShaderStage::Hull;
        case 4:  return ShaderStage::Domain;
        case 5:  return ShaderStage::Compute;
        case 7:  return ShaderStage::DxrRayGen;
        case 8:  return ShaderStage::DxrIntersection;
        case 9:  return ShaderStage::DxrAnyHit;
        case 10: return ShaderStage::DxrClosestHit;
        case 11: return ShaderStage::DxrMiss;
        case 12: return ShaderStage::DxrCallable;
        case 13: return ShaderStage::NggMesh;
        case 14: return ShaderStage::Amplification;
        default: return ShaderStage::Compute;
    }
}

uint16_t stage_to_dxil_program_type(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Pixel:           return 0;
        case ShaderStage::Vertex:          return 1;
        case ShaderStage::Geometry:        return 2;
        case ShaderStage::Hull:            return 3;
        case ShaderStage::Domain:          return 4;
        case ShaderStage::Compute:         return 5;
        case ShaderStage::DxrRayGen:       return 7;
        case ShaderStage::DxrIntersection: return 8;
        case ShaderStage::DxrAnyHit:       return 9;
        case ShaderStage::DxrClosestHit:   return 10;
        case ShaderStage::DxrMiss:         return 11;
        case ShaderStage::DxrCallable:     return 12;
        case ShaderStage::NggMesh:         return 13;
        case ShaderStage::Amplification:   return 14;
    }
    return 5;
}

/* FNV-1a 32-bit integrity checksum for Path B containers */
uint32_t fnv1a_32(const uint8_t* data, size_t size) noexcept {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

/* Common Path B header flags */
constexpr uint32_t kFlagUsesWqm       = (1u << 0);
constexpr uint32_t kFlagUsesRtBvh4    = (1u << 1);
constexpr uint32_t kFlagF32DenormFtz  = (1u << 2);

} // namespace

ContainerKind detect_container_kind(const uint8_t* data, size_t size) noexcept {
    if (data == nullptr || size < 4u) {
        return ContainerKind::Unknown;
    }
    const uint32_t magic = read_le32(data);
    if (magic == kFourccDxbc) {
        return ContainerKind::DxbcDxil;
    }
    if (magic == kMagicXs2Null || magic == kMagicXs2Bin) {
        return ContainerKind::XS2;
    }
    if (magic == kMagicFssm) {
        return ContainerKind::FSSM;
    }
    if (magic == kMagicPs09) {
        return ContainerKind::PS09;
    }
    if (magic == kMagicSgd5) {
        return ContainerKind::SGD5;
    }
    return ContainerKind::Unknown;
}

/* ============================================================================
 * Path A: DXIL Fast-Path Container Stripper
 * ============================================================================ */

DxilFastPathResult extract_dxil_fast_path(const uint8_t* data, size_t size) noexcept {
    DxilFastPathResult res{};
    constexpr size_t kDxbcHdr = 32u;

    if (data == nullptr || size < kDxbcHdr || size > 0x40000000u) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return res;
    }
    if (read_le32(data) != kFourccDxbc) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return res;
    }

    const uint32_t version = read_le32(data + 20u);
    const uint32_t declared_size = read_le32(data + 24u);
    const uint32_t chunk_count = read_le32(data + 28u);
    if (declared_size < kDxbcHdr || declared_size > size ||
        chunk_count == 0u || chunk_count > 1024u) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return res;
    }

    const uint64_t table_end = kDxbcHdr + static_cast<uint64_t>(chunk_count) * 4u;
    if (table_end > declared_size) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return res;
    }

    struct ChunkView {
        uint32_t fourcc;
        const uint8_t* payload;
        uint32_t length;
    };
    std::vector<ChunkView> kept;
    kept.reserve(chunk_count);

    bool has_dxil = false;
    uint32_t stripped = 0u;

    for (uint32_t i = 0; i < chunk_count; ++i) {
        const uint32_t off = read_le32(data + kDxbcHdr + i * 4u);
        if (off < table_end || static_cast<uint64_t>(off) + 8u > declared_size) {
            res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return res;
        }
        const uint32_t fourcc = read_le32(data + off);
        const uint32_t len = read_le32(data + off + 4u);
        if (static_cast<uint64_t>(off) + 8u + static_cast<uint64_t>(len) > declared_size) {
            res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return res;
        }
        const uint8_t* payload = data + off + 8u;

        /* Strip private Xbox console chunks ('XBOX', 'XS2M') while harvesting binding hints */
        if (fourcc == kFourccXbox || fourcc == kFourccXs2m) {
            ++stripped;
            if (len >= 16u) {
                const uint32_t binding_count = std::min(read_le32(payload), 16u);
                for (uint32_t bi = 0; bi < binding_count; ++bi) {
                    const size_t rec_off = 4u + static_cast<size_t>(bi) * 12u;
                    if (rec_off + 12u <= len) {
                        XboxResourceBinding b{};
                        b.xbox_slot = read_le32(payload + rec_off);
                        b.vulkan_set = 0u;
                        b.vulkan_binding = read_le32(payload + rec_off + 4u);
                        b.descriptor_type = read_le32(payload + rec_off + 8u);
                        res.remapped_bindings.push_back(b);
                    }
                }
            }
            continue;
        }

        if (fourcc == kFourccDxil || fourcc == kFourccIldb) {
            has_dxil = true;
            res.dxil_bytecode.assign(payload, payload + len);
            if (len >= 4u) {
                const uint32_t prog_ver = read_le32(payload);
                const uint16_t prog_type = static_cast<uint16_t>((prog_ver >> 16u) & 0xFFFFu);
                res.stage = map_dxil_program_type(prog_type);
            }
        }
        kept.push_back({fourcc, payload, len});
    }

    res.input_chunks = chunk_count;
    res.output_chunks = static_cast<uint32_t>(kept.size());
    res.stripped_xbox_chunks = stripped;

    if (!has_dxil || kept.empty()) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return res;
    }

    /* Rebuild clean standard DXBC container with recomputed chunk table & MD5 */
    uint64_t total = kDxbcHdr + static_cast<uint64_t>(kept.size()) * 4u;
    std::vector<uint32_t> new_offsets;
    new_offsets.reserve(kept.size());
    for (const auto& c : kept) {
        const uint64_t aligned = (total + 3u) & ~UINT64_C(3);
        if (aligned > UINT32_MAX) {
            res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return res;
        }
        new_offsets.push_back(static_cast<uint32_t>(aligned));
        total = aligned + 8u + static_cast<uint64_t>(c.length);
    }
    const uint64_t final_len = (total + 3u) & ~UINT64_C(3);
    if (final_len > UINT32_MAX) {
        res.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return res;
    }

    res.clean_dxbc.assign(static_cast<size_t>(final_len), 0u);
    write_le32(res.clean_dxbc.data() + 0u, kFourccDxbc);
    write_le32(res.clean_dxbc.data() + 20u, version);
    write_le32(res.clean_dxbc.data() + 24u, static_cast<uint32_t>(final_len));
    write_le32(res.clean_dxbc.data() + 28u, static_cast<uint32_t>(kept.size()));

    for (size_t i = 0; i < kept.size(); ++i) {
        write_le32(res.clean_dxbc.data() + kDxbcHdr + i * 4u, new_offsets[i]);
        uint8_t* dst = res.clean_dxbc.data() + new_offsets[i];
        write_le32(dst + 0u, kept[i].fourcc);
        write_le32(dst + 4u, kept[i].length);
        if (kept[i].length > 0u) {
            std::memcpy(dst + 8u, kept[i].payload, kept[i].length);
        }
    }
    compute_dxbc_md5(res.clean_dxbc.data(), static_cast<uint32_t>(final_len), res.clean_dxbc.data() + 4u);

    res.status = SCARLETT_OK;
    return res;
}

std::vector<uint8_t> pack_xbox_dxil_container(ShaderStage stage,
                                              const std::vector<uint8_t>& dxil_payload,
                                              bool include_xbox_chunk,
                                              bool include_xs2m_chunk) {
    struct OwnedChunk {
        uint32_t fourcc;
        std::vector<uint8_t> data;
    };
    std::vector<OwnedChunk> chunks;

    /* Standard SFI0 feature flags chunk */
    {
        OwnedChunk sfi{kFourccSfi0, {}};
        append_le32(sfi.data, 0u);
        append_le32(sfi.data, 0u);
        chunks.push_back(std::move(sfi));
    }

    /* Optional proprietary XBOX private chunk (contains Xbox resource remap table) */
    if (include_xbox_chunk) {
        OwnedChunk xb{kFourccXbox, {}};
        append_le32(xb.data, 2u); /* 2 remapped bindings */
        append_le32(xb.data, 0u); append_le32(xb.data, 0u); append_le32(xb.data, 0u);
        append_le32(xb.data, 1u); append_le32(xb.data, 1u); append_le32(xb.data, 0u);
        chunks.push_back(std::move(xb));
    }

    /* Standard DXIL chunk with 24-byte DXIL program header + LLVM bitcode payload */
    {
        OwnedChunk dxil{kFourccDxil, {}};
        const uint16_t ptype = stage_to_dxil_program_type(stage);
        const uint32_t prog_ver = (static_cast<uint32_t>(ptype) << 16u) | 0x0066u; /* SM 6.6 */
        const uint32_t bc_words = static_cast<uint32_t>((dxil_payload.size() + 3u) / 4u);
        append_le32(dxil.data, prog_ver);
        append_le32(dxil.data, 6u + bc_words);
        append_le32(dxil.data, 0x4C495844u); /* 'DXIL' */
        append_le32(dxil.data, 0x00000106u); /* DXIL 1.6 */
        append_le32(dxil.data, 16u);         /* BitcodeOffset */
        append_le32(dxil.data, static_cast<uint32_t>(dxil_payload.size()));
        dxil.data.insert(dxil.data.end(), dxil_payload.begin(), dxil_payload.end());
        while ((dxil.data.size() & 3u) != 0u) {
            dxil.data.push_back(0u);
        }
        chunks.push_back(std::move(dxil));
    }

    /* Optional XS2M Xbox Series microcode metadata chunk */
    if (include_xs2m_chunk) {
        OwnedChunk xs2m{kFourccXs2m, {}};
        append_le32(xs2m.data, 0u);
        append_le32(xs2m.data, 0x5853324Du);
        append_le32(xs2m.data, 64u); /* wave64 hint */
        append_le32(xs2m.data, 0u);
        chunks.push_back(std::move(xs2m));
    }

    const uint32_t kHdr = 32u;
    uint32_t total = kHdr + static_cast<uint32_t>(chunks.size()) * 4u;
    std::vector<uint32_t> offsets;
    for (const auto& c : chunks) {
        total = (total + 3u) & ~3u;
        offsets.push_back(total);
        total += 8u + static_cast<uint32_t>(c.data.size());
    }
    total = (total + 3u) & ~3u;

    std::vector<uint8_t> out(total, 0u);
    write_le32(out.data() + 0u, kFourccDxbc);
    write_le32(out.data() + 20u, 1u);
    write_le32(out.data() + 24u, total);
    write_le32(out.data() + 28u, static_cast<uint32_t>(chunks.size()));
    for (size_t i = 0; i < chunks.size(); ++i) {
        write_le32(out.data() + kHdr + i * 4u, offsets[i]);
        uint8_t* p = out.data() + offsets[i];
        write_le32(p + 0u, chunks[i].fourcc);
        write_le32(p + 4u, static_cast<uint32_t>(chunks[i].data.size()));
        if (!chunks[i].data.empty()) {
            std::memcpy(p + 8u, chunks[i].data.data(), chunks[i].data.size());
        }
    }
    compute_dxbc_md5(out.data(), total, out.data() + 4u);
    return out;
}

/* ============================================================================
 * Path B: Native Xbox RDNA2 Binary Containers (XS2, FSSM, PS\x00\x09, SGD5)
 * ============================================================================ */

namespace {

bool extract_isa_words(const uint8_t* data,
                       size_t size,
                       uint32_t isa_offset,
                       uint32_t isa_bytes,
                       std::vector<uint32_t>& out_words) noexcept {
    if (isa_bytes == 0u || (isa_bytes & 3u) != 0u) {
        return false;
    }
    if (static_cast<uint64_t>(isa_offset) + static_cast<uint64_t>(isa_bytes) > size) {
        return false;
    }
    const uint32_t count = isa_bytes / 4u;
    if (count > 1048576u) {
        return false;
    }
    out_words.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        out_words[i] = read_le32(data + isa_offset + i * 4u);
    }
    return true;
}

void unpack_flags(uint32_t flags, Rdna2ShaderMetadata& meta) noexcept {
    meta.flags = flags;
    meta.uses_wqm = (flags & kFlagUsesWqm) != 0u;
    meta.uses_rt_bvh4 = (flags & kFlagUsesRtBvh4) != 0u;
    meta.f32_denorm_ftz = (flags & kFlagF32DenormFtz) != 0u;
}

uint32_t pack_flags(const Rdna2ShaderMetadata& meta) noexcept {
    uint32_t flags = meta.flags;
    if (meta.uses_wqm)       flags |= kFlagUsesWqm;
    if (meta.uses_rt_bvh4)   flags |= kFlagUsesRtBvh4;
    if (meta.f32_denorm_ftz) flags |= kFlagF32DenormFtz;
    return flags;
}

/*
 * XS2 Layout (48-byte header):
 *   0x00: magic ('XS2\0' or 'XS2B')
 *   0x04: version (u16), stage (u16)
 *   0x08: wave_size (u16), vgpr_count (u16)
 *   0x0C: sgpr_count (u16), binding_count (u16)
 *   0x10: lds_bytes (u32)
 *   0x14: wg_x (u32), wg_y (u32), wg_z (u32)
 *   0x20: export_mask (u32)
 *   0x24: flags (u32)
 *   0x28: isa_offset (u32)
 *   0x2C: isa_bytes (u32)
 *   [optional bindings: binding_count * 16 bytes]
 *   [isa_bytes of GFX10.3 little-endian words]
 */
UnpackedRdna2Container unpack_xs2(const uint8_t* data, size_t size) noexcept {
    UnpackedRdna2Container out{};
    if (size < 48u) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    const uint16_t stage_u16 = read_le16(data + 6u);
    if (!valid_stage_code(stage_u16)) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    out.meta.container_kind = ContainerKind::XS2;
    out.meta.stage = static_cast<ShaderStage>(stage_u16);
    out.meta.wave_size = read_le16(data + 8u);
    if (out.meta.wave_size != 32u && out.meta.wave_size != 64u) {
        out.meta.wave_size = 64u;
    }
    out.meta.vgpr_count = read_le16(data + 10u);
    out.meta.sgpr_count = read_le16(data + 12u);
    const uint16_t bind_cnt = read_le16(data + 14u);
    out.meta.lds_bytes = read_le32(data + 16u);
    out.meta.workgroup_size[0] = std::max(1u, read_le32(data + 20u));
    out.meta.workgroup_size[1] = std::max(1u, read_le32(data + 24u));
    out.meta.workgroup_size[2] = std::max(1u, read_le32(data + 28u));
    out.meta.export_mask = read_le32(data + 32u);
    unpack_flags(read_le32(data + 36u), out.meta);
    const uint32_t isa_off = read_le32(data + 40u);
    const uint32_t isa_bytes = read_le32(data + 44u);

    if (48u + static_cast<uint64_t>(bind_cnt) * 16u > isa_off || isa_off > size) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    out.meta.bindings.reserve(bind_cnt);
    for (uint16_t i = 0; i < bind_cnt; ++i) {
        const uint8_t* bp = data + 48u + static_cast<size_t>(i) * 16u;
        XboxResourceBinding b{};
        b.xbox_slot = read_le32(bp + 0u);
        b.vulkan_set = read_le32(bp + 4u);
        b.vulkan_binding = read_le32(bp + 8u);
        b.descriptor_type = read_le32(bp + 12u);
        out.meta.bindings.push_back(b);
    }

    if (!extract_isa_words(data, size, isa_off, isa_bytes, out.isa_words)) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    out.status = SCARLETT_OK;
    return out;
}

/*
 * FSSM Layout (Chunked Frostbite/Scarlett Shader Stream Module):
 *   0x00: magic "FSSM"
 *   0x04: total_bytes (u32)
 *   0x08: stream_flags (u32)
 *   0x0C: section_count (u32)
 *   Sections: [tag(u32), len(u32), payload...]
 *     - 'META': stage(u32), wave(u32), vgpr(u32), sgpr(u32), lds(u32), wg[3](u32), export_mask(u32), flags(u32)
 *     - 'CODE': raw GFX10.3 instruction stream
 */
UnpackedRdna2Container unpack_fssm(const uint8_t* data, size_t size) noexcept {
    UnpackedRdna2Container out{};
    if (size < 16u) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    const uint32_t word1 = read_le32(data + 4u);
    const uint32_t word2 = read_le32(data + 8u);
    if (word1 == 0x00000101u && size >= 0xE0u &&
        static_cast<uint64_t>(word2) + 16u <= size) {
        /* Real Xbox Scarlett FSSM v1.1 module (decompressed from XDAT/XBCH) */
        const uint32_t hdr_off = read_le32(data + 0x40u);
        const uint32_t isa_meta_off =
            (hdr_off >= 0x40u && static_cast<uint64_t>(hdr_off) + 0x20u <= size)
                ? (hdr_off + 0x10u)
                : 0xC0u;
        const uint32_t isa_base = read_le32(data + isa_meta_off);
        const uint32_t isa_len = read_le32(data + isa_meta_off + 8u);
        if (isa_len >= 0x2Cu &&
            static_cast<uint64_t>(isa_base) + static_cast<uint64_t>(isa_len) <= size) {
            const uint32_t isa_start = isa_base + 0x28u;
            const uint32_t isa_bytes = isa_len - 0x28u;
            if ((isa_bytes & 3u) == 0u && isa_bytes >= 4u) {
                out.meta.container_kind = ContainerKind::FSSM;
                out.meta.stage = ShaderStage::Pixel; /* Caller overrides from DXIL header */
                out.meta.wave_size = 64u;
                out.meta.vgpr_count = 32u;
                out.meta.sgpr_count = 32u;
                out.meta.export_mask = 0x1u;
                out.meta.f32_denorm_ftz = true;
                const size_t nwords = isa_bytes / 4u;
                out.isa_words.resize(nwords);
                for (size_t i = 0; i < nwords; ++i) {
                    out.isa_words[i] = read_le32(data + isa_start + i * 4u);
                }
                while (out.isa_words.size() > 1u && out.isa_words.back() == 0xBF9F0000u) {
                    out.isa_words.pop_back();
                }
                out.status = SCARLETT_OK;
                return out;
            }
        }
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }

    const uint32_t total_bytes = word1;
    const uint32_t section_count = read_le32(data + 12u);
    if (total_bytes < 16u || total_bytes > size || section_count == 0u || section_count > 64u) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }

    out.meta.container_kind = ContainerKind::FSSM;
    bool got_meta = false;
    bool got_code = false;
    uint32_t cursor = 16u;

    for (uint32_t i = 0; i < section_count; ++i) {
        if (static_cast<uint64_t>(cursor) + 8u > total_bytes) {
            out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return out;
        }
        const uint32_t tag = read_le32(data + cursor);
        const uint32_t len = read_le32(data + cursor + 4u);
        cursor += 8u;
        if (static_cast<uint64_t>(cursor) + static_cast<uint64_t>(len) > total_bytes) {
            out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return out;
        }
        if (tag == 0x4154454Du /* "META" */) {
            if (len < 40u) {
                out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
                return out;
            }
            const uint32_t st = read_le32(data + cursor + 0u);
            if (!valid_stage_code(st)) {
                out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
                return out;
            }
            out.meta.stage = static_cast<ShaderStage>(st);
            out.meta.wave_size = read_le32(data + cursor + 4u);
            if (out.meta.wave_size != 32u && out.meta.wave_size != 64u) out.meta.wave_size = 64u;
            out.meta.vgpr_count = read_le32(data + cursor + 8u);
            out.meta.sgpr_count = read_le32(data + cursor + 12u);
            out.meta.lds_bytes = read_le32(data + cursor + 16u);
            out.meta.workgroup_size[0] = std::max(1u, read_le32(data + cursor + 20u));
            out.meta.workgroup_size[1] = std::max(1u, read_le32(data + cursor + 24u));
            out.meta.workgroup_size[2] = std::max(1u, read_le32(data + cursor + 28u));
            out.meta.export_mask = read_le32(data + cursor + 32u);
            unpack_flags(read_le32(data + cursor + 36u), out.meta);
            got_meta = true;
        } else if (tag == 0x45444F43u /* "CODE" */) {
            if (!extract_isa_words(data, size, cursor, len, out.isa_words)) {
                out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
                return out;
            }
            got_code = true;
        }
        cursor += len;
    }

    if (!got_meta || !got_code) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    out.status = SCARLETT_OK;
    return out;
}

/*
 * PS\x00\x09 Layout (Pipeline Shader v9, 52-byte header):
 *   0x00: magic "PS\x00\x09" (0x09005350)
 *   0x04: gfx_gen (u32, 0x0A03 = GFX10.3)
 *   0x08: stage (u32)
 *   0x0C: pgm_rsrc1 (u32)
 *   0x10: pgm_rsrc2 (u32)
 *   0x14: wave_size (u32)
 *   0x18: lds_bytes (u32)
 *   0x1C: wg_x (u32), wg_y (u32), wg_z (u32)
 *   0x28: export_mask (u32)
 *   0x2C: flags (u32)
 *   0x30: isa_bytes (u32)
 *   0x34: raw GFX10.3 words
 */
UnpackedRdna2Container unpack_ps09(const uint8_t* data, size_t size) noexcept {
    UnpackedRdna2Container out{};
    if (size < 52u) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    const uint32_t gfx_gen = read_le32(data + 4u);
    if (gfx_gen != 0x0A03u && gfx_gen != 0x0A00u && gfx_gen != 0x1030u) {
        out.status = SCARLETT_ERR_SHADER_UNSUPPORTED_ISA;
        return out;
    }
    const uint32_t st = read_le32(data + 8u);
    if (!valid_stage_code(st)) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    out.meta.container_kind = ContainerKind::PS09;
    out.meta.stage = static_cast<ShaderStage>(st);
    const uint32_t pgm_rsrc1 = read_le32(data + 12u);
    out.meta.vgpr_count = ((pgm_rsrc1 & 0x3Fu) + 1u) * 4u;
    out.meta.sgpr_count = (((pgm_rsrc1 >> 6u) & 0xFu) + 1u) * 8u;
    out.meta.wave_size = read_le32(data + 20u);
    if (out.meta.wave_size != 32u && out.meta.wave_size != 64u) out.meta.wave_size = 64u;
    out.meta.lds_bytes = read_le32(data + 24u);
    out.meta.workgroup_size[0] = std::max(1u, read_le32(data + 28u));
    out.meta.workgroup_size[1] = std::max(1u, read_le32(data + 32u));
    out.meta.workgroup_size[2] = std::max(1u, read_le32(data + 36u));
    out.meta.export_mask = read_le32(data + 40u);
    unpack_flags(read_le32(data + 44u), out.meta);
    const uint32_t isa_bytes = read_le32(data + 48u);

    if (!extract_isa_words(data, size, 52u, isa_bytes, out.isa_words)) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    while (out.isa_words.size() > 1u && out.isa_words.back() == 0xBF9F0000u) {
        out.isa_words.pop_back();
    }
    out.status = SCARLETT_OK;
    return out;
}

/*
 * SGD5 Layout (Scarlett Graphics Descriptor/Shader v5, 56-byte header + CRC):
 *   0x00: magic "SGD5"
 *   0x04: header_checksum (FNV-1a of ISA payload)
 *   0x08: stage (u32)
 *   0x0C: wave_size (u32)
 *   0x10: vgpr_count (u32)
 *   0x14: sgpr_count (u32)
 *   0x18: lds_bytes (u32)
 *   0x1C: wg_x (u32), wg_y (u32), wg_z (u32)
 *   0x28: export_mask (u32)
 *   0x2C: flags (u32)
 *   0x30: isa_offset (u32)
 *   0x34: isa_bytes (u32)
 */
UnpackedRdna2Container unpack_sgd5(const uint8_t* data, size_t size) noexcept {
    UnpackedRdna2Container out{};
    if (size < 56u) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    const uint32_t expected_hash = read_le32(data + 4u);
    const uint32_t st = read_le32(data + 8u);
    if (!valid_stage_code(st)) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    out.meta.container_kind = ContainerKind::SGD5;
    out.meta.stage = static_cast<ShaderStage>(st);
    out.meta.wave_size = read_le32(data + 12u);
    if (out.meta.wave_size != 32u && out.meta.wave_size != 64u) out.meta.wave_size = 64u;
    out.meta.vgpr_count = read_le32(data + 16u);
    out.meta.sgpr_count = read_le32(data + 20u);
    out.meta.lds_bytes = read_le32(data + 24u);
    out.meta.workgroup_size[0] = std::max(1u, read_le32(data + 28u));
    out.meta.workgroup_size[1] = std::max(1u, read_le32(data + 32u));
    out.meta.workgroup_size[2] = std::max(1u, read_le32(data + 36u));
    out.meta.export_mask = read_le32(data + 40u);
    unpack_flags(read_le32(data + 44u), out.meta);
    const uint32_t isa_off = read_le32(data + 48u);
    const uint32_t isa_bytes = read_le32(data + 52u);

    if (isa_off < 56u || !extract_isa_words(data, size, isa_off, isa_bytes, out.isa_words)) {
        out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return out;
    }
    if (expected_hash != 0u) {
        const uint32_t actual = fnv1a_32(data + isa_off, isa_bytes);
        if (actual != expected_hash) {
            out.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return out;
        }
    }
    out.status = SCARLETT_OK;
    return out;
}

} // namespace

UnpackedRdna2Container unpack_rdna2_container(const uint8_t* data, size_t size) noexcept {
    const ContainerKind kind = detect_container_kind(data, size);
    switch (kind) {
        case ContainerKind::XS2:  return unpack_xs2(data, size);
        case ContainerKind::FSSM: return unpack_fssm(data, size);
        case ContainerKind::PS09: return unpack_ps09(data, size);
        case ContainerKind::SGD5: return unpack_sgd5(data, size);
        default: {
            UnpackedRdna2Container err{};
            err.status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
            return err;
        }
    }
}

std::vector<uint8_t> pack_rdna2_container(ContainerKind kind,
                                          const Rdna2ShaderMetadata& meta,
                                          const std::vector<uint32_t>& isa_words) {
    std::vector<uint8_t> out;
    const uint32_t isa_bytes = static_cast<uint32_t>(isa_words.size() * 4u);
    const uint32_t flags = pack_flags(meta);

    if (kind == ContainerKind::XS2) {
        const uint16_t bind_cnt = static_cast<uint16_t>(meta.bindings.size());
        const uint32_t isa_off = 48u + static_cast<uint32_t>(bind_cnt) * 16u;
        out.reserve(isa_off + isa_bytes);
        append_le32(out, kMagicXs2Null);
        append_le16(out, 2u);
        append_le16(out, static_cast<uint16_t>(meta.stage));
        append_le16(out, static_cast<uint16_t>(meta.wave_size));
        append_le16(out, static_cast<uint16_t>(meta.vgpr_count));
        append_le16(out, static_cast<uint16_t>(meta.sgpr_count));
        append_le16(out, bind_cnt);
        append_le32(out, meta.lds_bytes);
        append_le32(out, meta.workgroup_size[0]);
        append_le32(out, meta.workgroup_size[1]);
        append_le32(out, meta.workgroup_size[2]);
        append_le32(out, meta.export_mask);
        append_le32(out, flags);
        append_le32(out, isa_off);
        append_le32(out, isa_bytes);
        for (const auto& b : meta.bindings) {
            append_le32(out, b.xbox_slot);
            append_le32(out, b.vulkan_set);
            append_le32(out, b.vulkan_binding);
            append_le32(out, b.descriptor_type);
        }
        for (uint32_t w : isa_words) {
            append_le32(out, w);
        }
        return out;
    }

    if (kind == ContainerKind::FSSM) {
        const uint32_t meta_len = 40u;
        const uint32_t total_bytes = 16u + 8u + meta_len + 8u + isa_bytes;
        out.reserve(total_bytes);
        append_le32(out, kMagicFssm);
        append_le32(out, total_bytes);
        append_le32(out, flags);
        append_le32(out, 2u);
        /* META section */
        append_le32(out, 0x4154454Du);
        append_le32(out, meta_len);
        append_le32(out, static_cast<uint32_t>(meta.stage));
        append_le32(out, meta.wave_size);
        append_le32(out, meta.vgpr_count);
        append_le32(out, meta.sgpr_count);
        append_le32(out, meta.lds_bytes);
        append_le32(out, meta.workgroup_size[0]);
        append_le32(out, meta.workgroup_size[1]);
        append_le32(out, meta.workgroup_size[2]);
        append_le32(out, meta.export_mask);
        append_le32(out, flags);
        /* CODE section */
        append_le32(out, 0x45444F43u);
        append_le32(out, isa_bytes);
        for (uint32_t w : isa_words) {
            append_le32(out, w);
        }
        return out;
    }

    if (kind == ContainerKind::PS09) {
        out.reserve(52u + isa_bytes);
        const uint32_t vgpr_blocks = meta.vgpr_count > 4u ? ((meta.vgpr_count - 1u) / 4u) : 0u;
        const uint32_t sgpr_blocks = meta.sgpr_count > 8u ? ((meta.sgpr_count - 1u) / 8u) : 0u;
        const uint32_t pgm_rsrc1 = (vgpr_blocks & 0x3Fu) | ((sgpr_blocks & 0xFu) << 6u);
        const uint32_t pgm_rsrc2 = (meta.lds_bytes / 512u) << 15u;
        append_le32(out, kMagicPs09);
        append_le32(out, 0x0A03u); /* GFX10.3 */
        append_le32(out, static_cast<uint32_t>(meta.stage));
        append_le32(out, pgm_rsrc1);
        append_le32(out, pgm_rsrc2);
        append_le32(out, meta.wave_size);
        append_le32(out, meta.lds_bytes);
        append_le32(out, meta.workgroup_size[0]);
        append_le32(out, meta.workgroup_size[1]);
        append_le32(out, meta.workgroup_size[2]);
        append_le32(out, meta.export_mask);
        append_le32(out, flags);
        append_le32(out, isa_bytes);
        for (uint32_t w : isa_words) {
            append_le32(out, w);
        }
        return out;
    }

    /* Default: SGD5 */
    std::vector<uint8_t> raw_isa;
    raw_isa.reserve(isa_bytes);
    for (uint32_t w : isa_words) {
        append_le32(raw_isa, w);
    }
    const uint32_t checksum = fnv1a_32(raw_isa.data(), raw_isa.size());
    out.reserve(56u + isa_bytes);
    append_le32(out, kMagicSgd5);
    append_le32(out, checksum);
    append_le32(out, static_cast<uint32_t>(meta.stage));
    append_le32(out, meta.wave_size);
    append_le32(out, meta.vgpr_count);
    append_le32(out, meta.sgpr_count);
    append_le32(out, meta.lds_bytes);
    append_le32(out, meta.workgroup_size[0]);
    append_le32(out, meta.workgroup_size[1]);
    append_le32(out, meta.workgroup_size[2]);
    append_le32(out, meta.export_mask);
    append_le32(out, flags);
    append_le32(out, 56u);
    append_le32(out, isa_bytes);
    out.insert(out.end(), raw_isa.begin(), raw_isa.end());
    return out;
}

} // namespace scarlett::rdna2
