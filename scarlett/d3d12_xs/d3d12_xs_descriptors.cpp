/*
 * Proton-Scarlett — Descriptor Heap Policy (NVIDIA 610.xx & 1M Guard) & T#/V#/S# Encoder
 *
 * Implements the normative descriptor backend cascade from ABI_CONTRACTS.md:
 *   1. Primary: VK_EXT_descriptor_heap
 *   2. Mandatory Guard: if NVIDIA 610.xx driver is detected OR requested
 *      descriptors exceed 1,000,000 (SCARLETT_D3D12_DESC_HEAP_SAFE_LIMIT) OR
 *      VK_EXT_descriptor_heap is absent -> seamless fallback to
 *      VK_EXT_descriptor_buffer.
 *   3. Secondary Fallback: if VK_EXT_descriptor_buffer is absent -> fallback to
 *      VK_EXT_descriptor_indexing (updateAfterBind).
 */

#include "d3d12_xs_internal.hpp"

#include <cctype>
#include <cstring>

namespace scarlett::d3d12xs::unixlib {

namespace {

bool parse_dotted_triplet(const char *str, uint32_t &out_maj, uint32_t &out_min, uint32_t &out_pat) noexcept {
    if (str == nullptr || str[0] == '\0') {
        return false;
    }

    const char *p = str;
    while (*p != '\0' && !std::isdigit(static_cast<unsigned char>(*p))) {
        ++p;
    }
    if (*p == '\0') {
        return false;
    }

    auto read_u32 = [&](uint32_t &dst) -> bool {
        if (!std::isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
        uint64_t acc = 0;
        while (std::isdigit(static_cast<unsigned char>(*p))) {
            acc = acc * 10u + static_cast<uint64_t>(*p - '0');
            if (acc > 0xFFFFu) {
                acc = 0xFFFFu;
            }
            ++p;
        }
        dst = static_cast<uint32_t>(acc);
        return true;
    };

    uint32_t maj = 0;
    uint32_t min = 0;
    uint32_t pat = 0;
    if (!read_u32(maj)) {
        return false;
    }
    if (*p == '.') {
        ++p;
        (void)read_u32(min);
        if (*p == '.') {
            ++p;
            (void)read_u32(pat);
        }
    }
    out_maj = maj;
    out_min = min;
    out_pat = pat;
    return true;
}

} // namespace

DriverVersionParsed parse_driver_version(
    uint32_t vendor_id,
    uint32_t driver_id,
    uint32_t raw_driver_version,
    const char *driver_info) noexcept {
    DriverVersionParsed out{};

    /* Prefer human-readable VkPhysicalDeviceDriverProperties::driverInfo when parseable */
    uint32_t info_maj = 0;
    uint32_t info_min = 0;
    uint32_t info_pat = 0;
    if (parse_dotted_triplet(driver_info, info_maj, info_min, info_pat)) {
        out.major = info_maj;
        out.minor = info_min;
        out.patch = info_pat;
        out.parsed_from_info = true;
        return out;
    }

    /* Fall back to vendor-specific packed VkPhysicalDeviceProperties::driverVersion */
    const bool is_nvidia =
        (vendor_id == SCAR_VK_VENDOR_ID_NVIDIA) ||
        (driver_id == SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY);
    if (is_nvidia) {
        /* NVIDIA Vulkan packed driverVersion: 10-bit major, 8-bit minor, 8-bit secondary, 6-bit tertiary */
        out.major = (raw_driver_version >> 22u) & 0x3FFu;
        out.minor = (raw_driver_version >> 14u) & 0xFFu;
        out.patch = (raw_driver_version >> 6u) & 0xFFu;
    } else {
        /* Standard Vulkan VK_MAKE_API_VERSION encoding (Mesa RADV / Intel ANV) */
        out.major = SCAR_VK_API_VERSION_MAJOR(raw_driver_version);
        out.minor = SCAR_VK_API_VERSION_MINOR(raw_driver_version);
        out.patch = SCAR_VK_API_VERSION_PATCH(raw_driver_version);
    }
    out.parsed_from_info = false;
    return out;
}

DescriptorPolicyDecision evaluate_descriptor_policy(const DescriptorPolicyInput &in) noexcept {
    DescriptorPolicyDecision dec{};
    dec.parsed_version = parse_driver_version(
        in.vendor_id,
        in.driver_id,
        in.raw_driver_version,
        in.driver_info.empty() ? nullptr : in.driver_info.c_str());

    dec.is_nvidia =
        (in.vendor_id == SCAR_VK_VENDOR_ID_NVIDIA) ||
        (in.driver_id == SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY);

    /* Cross-check both driverInfo and packed driverVersion so 610.xx can never slip through */
    const uint32_t packed_nv_major = (in.raw_driver_version >> 22u) & 0x3FFu;
    dec.is_nvidia_610_guard =
        dec.is_nvidia &&
        (dec.parsed_version.major == 610u ||
         (!dec.parsed_version.parsed_from_info && packed_nv_major == 610u) ||
         (in.raw_driver_version != 0u && packed_nv_major == 610u));

    auto select_fallback = [&](scarlett_d3d12xs_desc_fallback_reason_t primary_reason) {
        if (in.has_descriptor_buffer_ext) {
            dec.selected_backend = SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER;
            dec.reason = primary_reason;
        } else {
            dec.selected_backend = SCARLETT_DESC_BACKEND_DESCRIPTOR_INDEXING;
            dec.reason = (primary_reason == SCARLETT_DESC_REASON_HEAP_EXT_MISSING)
                             ? SCARLETT_DESC_REASON_BUFFER_EXT_MISSING
                             : primary_reason;
        }
    };

    /* Rule 1: Mandatory NVIDIA 610.xx driver bug guard */
    if (dec.is_nvidia_610_guard) {
        select_fallback(SCARLETT_DESC_REASON_NVIDIA_610_GUARD);
        return dec;
    }

    /* Rule 2: Mandatory > 1,000,000 descriptors safe cap (NVIDIA & global) */
    if (in.requested_descriptors > SCARLETT_D3D12_DESC_HEAP_SAFE_LIMIT) {
        select_fallback(dec.is_nvidia ? SCARLETT_DESC_REASON_NVIDIA_1M_LIMIT
                                      : SCARLETT_DESC_REASON_GLOBAL_1M_LIMIT);
        return dec;
    }

    /* Rule 3: Native VK_EXT_descriptor_heap when present */
    if (in.has_descriptor_heap_ext) {
        dec.selected_backend = SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_HEAP;
        dec.reason = SCARLETT_DESC_REASON_NATIVE_HEAP;
        return dec;
    }

    /* Rule 4: Dynamic extension fallback cascade */
    select_fallback(SCARLETT_DESC_REASON_HEAP_EXT_MISSING);
    return dec;
}

scarlett_hresult_t encode_rdna2_descriptor(
    const scarlett_d3d12xs_write_desc_params_t &req,
    DescriptorSlot &out_slot) noexcept {
    out_slot = DescriptorSlot{};
    out_slot.kind = req.desc_kind;

    switch (req.desc_kind) {
        case SCARLETT_DESC_KIND_TEXTURE_T_SHARP: {
            /* RDNA2 256-bit T# image resource descriptor encoding */
            const uint64_t base_addr_256 = (req.resource_va >> 8u) & UINT64_C(0xFFFFFFFFFFFF);
            const uint64_t w_m1 = (req.width > 0u ? (req.width - 1u) : 0u) & UINT64_C(0x3FFF);
            const uint64_t h_m1 = (req.height > 0u ? (req.height - 1u) : 0u) & UINT64_C(0x3FFF);
            const uint64_t fmt = static_cast<uint64_t>(req.format & 0x1FFu);
            const uint64_t mips = static_cast<uint64_t>(req.mip_or_filter & 0xFu);

            out_slot.words[0] = base_addr_256 | (fmt << 48u);
            out_slot.words[1] = w_m1 | (h_m1 << 16u) | (mips << 32u) | (UINT64_C(0x5423) << 48u); /* 'T#' tag */
            out_slot.words[2] = req.resource_size;
            out_slot.words[3] = UINT64_C(0x1) | (static_cast<uint64_t>(req.slot_index) << 32u);
            return SCARLETT_OK;
        }
        case SCARLETT_DESC_KIND_BUFFER_V_SHARP: {
            /* RDNA2 128-bit V# buffer resource descriptor encoding */
            const uint64_t va_48 = req.resource_va & UINT64_C(0xFFFFFFFFFFFF);
            const uint64_t stride = static_cast<uint64_t>(req.width & 0x3FFFu);
            out_slot.words[0] = va_48 | (stride << 48u);
            out_slot.words[1] = (req.resource_size & UINT64_C(0xFFFFFFFF)) |
                                (static_cast<uint64_t>(req.format & 0xFFFFu) << 32u) |
                                (UINT64_C(0x5623) << 48u); /* 'V#' tag */
            return SCARLETT_OK;
        }
        case SCARLETT_DESC_KIND_SAMPLER_S_SHARP: {
            /* RDNA2 128-bit S# sampler descriptor encoding */
            const uint64_t filter = static_cast<uint64_t>(req.mip_or_filter);
            const uint64_t clamp_aniso = static_cast<uint64_t>(req.format);
            out_slot.words[0] = filter | (clamp_aniso << 32u);
            out_slot.words[1] = static_cast<uint64_t>(req.width) |
                                (static_cast<uint64_t>(req.height) << 16u) |
                                (UINT64_C(0x5323) << 48u); /* 'S#' tag */
            return SCARLETT_OK;
        }
        default:
            return SCARLETT_E_INVALIDARG;
    }
}

} // namespace scarlett::d3d12xs::unixlib
