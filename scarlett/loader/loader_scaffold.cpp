#include <cstdint>

namespace scarlett::loader {

struct LoaderDescriptor {
    const char* name;
    uint32_t xvd_header_span_bytes;
    uint32_t encrypted_package_error_code;
};

constexpr LoaderDescriptor kDescriptor = {
    "scarlett-loader",
    0x200U,
    0x80831001U, // SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED
};

const LoaderDescriptor& query_descriptor() noexcept {
    return kDescriptor;
}

uint32_t verify_unencrypted_key_count(uint32_t key_count) noexcept {
    if (key_count != 0U) {
        return kDescriptor.encrypted_package_error_code;
    }
    return 0U;
}

} // namespace scarlett::loader
