#include <cstdint>

namespace scarlett::xg_xs {

struct TileAlignmentConstants {
    uint64_t tile_4kb_alignment;
    uint64_t tile_64kb_alignment;
};

constexpr TileAlignmentConstants kTileAlignments = {
    4096ULL,
    65536ULL,
};

const TileAlignmentConstants& query_tile_alignments() noexcept {
    return kTileAlignments;
}

} // namespace scarlett::xg_xs
