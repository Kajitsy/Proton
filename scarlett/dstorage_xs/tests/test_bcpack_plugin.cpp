#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "dstorage_xs_unix_abi.h"

namespace {

uint32_t crc32_ieee(const uint8_t *data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
            crc = (crc >> 1) ^ mask;
        }
    }
    return ~crc;
}

scarlett_hresult_t plugin_decode(
    scarlett_bcpack_decoder_t *self,
    uint32_t bcpack_mode,
    const uint8_t *src,
    uint64_t src_size,
    uint8_t *dst,
    uint64_t dst_size,
    uint64_t *out_bytes_written) {
    (void)self;
    if (src == nullptr || dst == nullptr || out_bytes_written == nullptr) {
        return SCARLETT_E_POINTER;
    }
    *out_bytes_written = 0u;
    if (src_size < sizeof(scarlett_bcpack_header_t)) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED;
    }

    scarlett_bcpack_header_t hdr{};
    std::memcpy(&hdr, src, sizeof(hdr));
    if (hdr.magic != SCARLETT_DSTORAGE_BCPACK_MAGIC || hdr.version != 1u) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED;
    }
    if (hdr.bcpack_mode != 0u && hdr.bcpack_mode != bcpack_mode) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED;
    }
    if (hdr.uncompressed_size > dst_size ||
        sizeof(hdr) + hdr.compressed_payload_size > src_size ||
        hdr.compressed_payload_size != hdr.uncompressed_size) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED;
    }

    const uint8_t *payload = src + sizeof(hdr);
    if (crc32_ieee(payload, hdr.compressed_payload_size) != hdr.payload_crc32) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED;
    }

    /* Clean-room reversible test transform: XOR with mode-derived key */
    const uint8_t key = static_cast<uint8_t>((bcpack_mode * 0x3Du) ^ 0xA5u);
    for (uint32_t i = 0; i < hdr.uncompressed_size; ++i) {
        dst[i] = static_cast<uint8_t>(payload[i] ^ key);
    }
    *out_bytes_written = hdr.uncompressed_size;
    return SCARLETT_OK;
}

void plugin_destroy(scarlett_bcpack_decoder_t *self) {
    delete self;
}

} // namespace

extern "C" {

__attribute__((visibility("default")))
scarlett_hresult_t scarlett_bcpack_create_decoder(
    uint32_t requested_abi_version,
    scarlett_bcpack_decoder_t **out_decoder) {
    if (out_decoder == nullptr) {
        return SCARLETT_E_POINTER;
    }
    *out_decoder = nullptr;
    if (requested_abi_version != SCARLETT_BCPACK_PLUGIN_ABI_VERSION) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH;
    }

    auto *dec = new scarlett_bcpack_decoder_t();
    dec->abi_version = SCARLETT_BCPACK_PLUGIN_ABI_VERSION;
    dec->reserved = 0u;
    dec->decode = plugin_decode;
    dec->destroy = plugin_destroy;
    dec->impl_ctx = nullptr;
    *out_decoder = dec;
    return SCARLETT_OK;
}

} // extern "C"
