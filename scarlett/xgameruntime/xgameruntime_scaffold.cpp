#include <cstdint>

namespace scarlett::xgameruntime {

struct RuntimeCapabilities {
    uint32_t feature_domain_count;
    uint32_t xgamesave_err_first;
    uint32_t xgamesave_err_last;
};

constexpr RuntimeCapabilities kCapabilities = {
    22U,
    0x80830001U,
    0x8083000FU,
};

const RuntimeCapabilities& query_capabilities() noexcept {
    return kCapabilities;
}

} // namespace scarlett::xgameruntime
