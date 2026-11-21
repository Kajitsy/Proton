#include <cstdint>

namespace scarlett::xdl {

struct SubsystemDescriptor {
    const char* name;
    const char* target_artifact;
    uint32_t min_gameos_year;
    uint32_t max_gameos_year;
    uint32_t vtable_slot_count;
};

constexpr SubsystemDescriptor kDescriptor = {
    "scarlett-xdl",
    "scarlett-xdl",
    2020U,
    2026U,
    32U,
};

const SubsystemDescriptor& query_descriptor() noexcept {
    return kDescriptor;
}

uint32_t unimplemented_thunk_status() noexcept {
    // Explicit fail-loud status code (E_NOTIMPL = 0x80004001)
    return 0x80004001U;
}

} // namespace scarlett::xdl
