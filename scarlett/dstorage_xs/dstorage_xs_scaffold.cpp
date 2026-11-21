#include <cstdint>

namespace scarlett::dstorage_xs {

struct DStorageRequestOptionsLayout {
    uint64_t zlib_decompress : 1;
    uint64_t bcpack_mode : 4;
    uint64_t swizzle_mode : 16;
    uint64_t destination_is_physical_pages : 1;
    uint64_t source_type : 1;
    uint64_t source_is_physical_pages : 1;
    uint64_t reserved : 40;
};

static_assert(sizeof(DStorageRequestOptionsLayout) == 8,
              "DStorageRequestOptionsLayout must occupy exactly 64 bits");

uint32_t query_options_bit_width() noexcept {
    return static_cast<uint32_t>(sizeof(DStorageRequestOptionsLayout) * 8U);
}

} // namespace scarlett::dstorage_xs
