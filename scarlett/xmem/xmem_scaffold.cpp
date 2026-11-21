#include <cstdint>

namespace scarlett::xmem {

struct MemoryArenaSpecification {
    uint64_t mem_graphics_base;
    uint64_t mem_graphics_limit;
    uint64_t mem_title_base;
    uint64_t mem_title_limit;
    uint64_t anaconda_title_bytes;
    uint64_t lockhart_title_bytes;
    uint64_t devkit_title_bytes;
};

constexpr MemoryArenaSpecification kArenaSpec = {
    0x0000000100000000ULL,
    0x000000FFFFFFFFFFULL,
    0x0000040000000000ULL,
    0x000007FFFFFFFFFFULL,
    14495514624ULL, // 13.5 GiB
    9126805504ULL,  // 8.5 GiB
    42949672960ULL, // 40.0 GiB
};

const MemoryArenaSpecification& query_arena_specification() noexcept {
    return kArenaSpec;
}

} // namespace scarlett::xmem
