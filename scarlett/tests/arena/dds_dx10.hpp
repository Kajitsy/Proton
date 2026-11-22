/*
 * Proton-Scarlett — Strict 148-Byte DDS + DX10 Header Parser & Builder
 *
 * Exact binary layout:
 *   4 bytes  : Magic "DDS " (0x20534444 LE)
 *   124 bytes: DDS_HEADER
 *   20 bytes : DDS_HEADER_DXT10
 *   Total    : 148 bytes + pixel payload (BC7 / RGBA8)
 */

#ifndef SCARLETT_ARENA_DDS_DX10_HPP
#define SCARLETT_ARENA_DDS_DX10_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scarlett::arena {

inline constexpr uint32_t kDdsMagic = 0x20534444u;        /* "DDS " */
inline constexpr uint32_t kDdsFourccDx10 = 0x30315844u;   /* "DX10" */

inline constexpr uint32_t kDdsdCaps = 0x1u;
inline constexpr uint32_t kDdsdHeight = 0x2u;
inline constexpr uint32_t kDdsdWidth = 0x4u;
inline constexpr uint32_t kDdsdPitch = 0x8u;
inline constexpr uint32_t kDdsdPixelFormat = 0x1000u;
inline constexpr uint32_t kDdsdMipMapCount = 0x20000u;
inline constexpr uint32_t kDdsdLinearSize = 0x80000u;

inline constexpr uint32_t kDdpfFourcc = 0x4u;
inline constexpr uint32_t kDdscapsTexture = 0x1000u;
inline constexpr uint32_t kDdsDimensionTexture2D = 3u;

inline constexpr uint32_t kDxgiFormatR8G8B8A8Unorm = 28u;
inline constexpr uint32_t kDxgiFormatBc7Unorm = 98u;

#pragma pack(push, 1)
struct DdsPixelFormat {
    uint32_t size;          /* 32 */
    uint32_t flags;         /* kDdpfFourcc */
    uint32_t four_cc;       /* "DX10" */
    uint32_t rgb_bit_count;
    uint32_t r_bit_mask;
    uint32_t g_bit_mask;
    uint32_t b_bit_mask;
    uint32_t a_bit_mask;
};

struct DdsHeader {
    uint32_t size;              /* 124 */
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch_or_linear_size;
    uint32_t depth;
    uint32_t mip_map_count;
    uint32_t reserved1[11];
    DdsPixelFormat ddspf;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct DdsHeaderDxt10 {
    uint32_t dxgi_format;
    uint32_t resource_dimension;
    uint32_t misc_flag;
    uint32_t array_size;
    uint32_t misc_flags2;
};

struct DdsDx10FileHeader {
    uint32_t magic;             /* 4 bytes */
    DdsHeader header;           /* 124 bytes */
    DdsHeaderDxt10 dx10;        /* 20 bytes */
};
#pragma pack(pop)

static_assert(sizeof(DdsPixelFormat) == 32, "DDS_PIXELFORMAT must be 32 bytes");
static_assert(sizeof(DdsHeader) == 124, "DDS_HEADER must be 124 bytes");
static_assert(sizeof(DdsHeaderDxt10) == 20, "DDS_HEADER_DXT10 must be 20 bytes");
static_assert(sizeof(DdsDx10FileHeader) == 148,
              "Strict DDS + DX10 header must be exactly 148 bytes");

struct ParsedDdsDx10 {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_count = 0;
    uint32_t dxgi_format = 0;
    uint32_t array_size = 0;
    size_t payload_offset = 0;
    size_t payload_bytes = 0;
};

inline bool parse_strict_dds_dx10(const uint8_t* data,
                                  size_t size,
                                  ParsedDdsDx10& out) noexcept {
    if (data == nullptr || size < sizeof(DdsDx10FileHeader)) {
        return false;
    }

    DdsDx10FileHeader hdr{};
    std::memcpy(&hdr, data, sizeof(hdr));

    if (hdr.magic != kDdsMagic) {
        return false;
    }
    if (hdr.header.size != 124u || hdr.header.ddspf.size != 32u) {
        return false;
    }
    if ((hdr.header.ddspf.flags & kDdpfFourcc) == 0u ||
        hdr.header.ddspf.four_cc != kDdsFourccDx10) {
        return false;
    }
    if (hdr.header.width == 0u || hdr.header.height == 0u) {
        return false;
    }
    if (hdr.dx10.resource_dimension != kDdsDimensionTexture2D || hdr.dx10.array_size == 0u) {
        return false;
    }

    size_t expected_payload = 0u;
    if (hdr.dx10.dxgi_format == kDxgiFormatBc7Unorm) {
        const size_t bx = (static_cast<size_t>(hdr.header.width) + 3u) / 4u;
        const size_t by = (static_cast<size_t>(hdr.header.height) + 3u) / 4u;
        expected_payload = bx * by * 16u * static_cast<size_t>(hdr.dx10.array_size);
    } else if (hdr.dx10.dxgi_format == kDxgiFormatR8G8B8A8Unorm) {
        expected_payload = static_cast<size_t>(hdr.header.width) *
                           static_cast<size_t>(hdr.header.height) * 4u *
                           static_cast<size_t>(hdr.dx10.array_size);
    } else {
        return false;
    }

    if (size != sizeof(DdsDx10FileHeader) + expected_payload) {
        return false;
    }

    out.width = hdr.header.width;
    out.height = hdr.header.height;
    out.mip_count = hdr.header.mip_map_count == 0u ? 1u : hdr.header.mip_map_count;
    out.dxgi_format = hdr.dx10.dxgi_format;
    out.array_size = hdr.dx10.array_size;
    out.payload_offset = sizeof(DdsDx10FileHeader);
    out.payload_bytes = expected_payload;
    return true;
}

inline std::vector<uint8_t> build_strict_dds_dx10(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t dxgi_format,
                                                  const std::vector<uint8_t>& pixels) {
    DdsDx10FileHeader hdr{};
    hdr.magic = kDdsMagic;
    hdr.header.size = 124u;
    hdr.header.flags = kDdsdCaps | kDdsdHeight | kDdsdWidth | kDdsdPixelFormat | kDdsdMipMapCount |
                       (dxgi_format == kDxgiFormatBc7Unorm ? kDdsdLinearSize : kDdsdPitch);
    hdr.header.height = height;
    hdr.header.width = width;
    if (dxgi_format == kDxgiFormatBc7Unorm) {
        const uint32_t bx = (width + 3u) / 4u;
        const uint32_t by = (height + 3u) / 4u;
        hdr.header.pitch_or_linear_size = bx * by * 16u;
    } else {
        hdr.header.pitch_or_linear_size = width * 4u;
    }
    hdr.header.depth = 1u;
    hdr.header.mip_map_count = 1u;
    hdr.header.ddspf.size = 32u;
    hdr.header.ddspf.flags = kDdpfFourcc;
    hdr.header.ddspf.four_cc = kDdsFourccDx10;
    hdr.header.caps = kDdscapsTexture;

    hdr.dx10.dxgi_format = dxgi_format;
    hdr.dx10.resource_dimension = kDdsDimensionTexture2D;
    hdr.dx10.misc_flag = 0u;
    hdr.dx10.array_size = 1u;
    hdr.dx10.misc_flags2 = 0u;

    std::vector<uint8_t> out(sizeof(hdr) + pixels.size(), 0u);
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    if (!pixels.empty()) {
        std::memcpy(out.data() + sizeof(hdr), pixels.data(), pixels.size());
    }
    return out;
}

} // namespace scarlett::arena

#endif /* SCARLETT_ARENA_DDS_DX10_HPP */
