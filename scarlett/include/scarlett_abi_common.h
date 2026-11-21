/*
 * Proton-Scarlett — Common ABI, Platform Profiles, Memory Ranges & Subsystem Constants
 *
 * Clean-room, freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLLs (x86_64-windows)
 *   - Wine unixlib ELF64 modules (x86_64-unix)
 *   - Host tooling and tests
 *
 * Zero dependencies on <windows.h>, libc, libvulkan, or liburing.
 */

#ifndef SCARLETT_ABI_COMMON_H
#define SCARLETT_ABI_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Proton 11.0 Base & Environment Variable Contract
 * ============================================================================ */

#define SCARLETT_PROTON_PREFIX_VERSION     "11.0-100"
#define SCARLETT_CONTRACT_ABI_VERSION      UINT32_C(1)
#define SCARLETT_GXDK_MAGIC                UINT32_C(0x585D070E)

#define SCARLETT_GAMEOS_EPOCH_YEAR_MIN     UINT32_C(2020)
#define SCARLETT_GAMEOS_EPOCH_YEAR_MAX     UINT32_C(2026)

#define SCARLETT_ENV_ENABLE                "PROTON_XBOX_SCARLETT"
#define SCARLETT_ENV_MODEL                 "PROTON_XBOX_MODEL"
#define SCARLETT_ENV_OVERLAY_DIR           "PROTON_XBOX_OVERLAY_DIR"
#define SCARLETT_ENV_SHADER_MODE           "PROTON_XBOX_SHADER_MODE"
#define SCARLETT_ENV_STRICT                "PROTON_XBOX_STRICT"
#define SCARLETT_ENV_BCPACK_PLUGIN         "PROTON_XBOX_BCPACK_PLUGIN"
#define SCARLETT_ENV_NET_MODE              "PROTON_XBOX_NET_MODE"

/* ============================================================================
 * Game OS / GXDK ABI Version Descriptor (scarlett-xdl abi_t)
 * ============================================================================ */

typedef struct scarlett_abi_version_s {
    uint16_t major;
    uint16_t minor;
    uint16_t build;
    uint16_t revision;
    uint32_t gxdk_magic; /* Expected SCARLETT_GXDK_MAGIC (0x585D070E) */
    uint32_t flags;      /* Edition / feature flags; 0 for standard Scarlett */
} scarlett_abi_version_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_abi_version_t) == 16,
                       "scarlett_abi_version_t must be 16 bytes");

static inline uint64_t scarlett_abi_pack_u64(scarlett_abi_version_t v) {
    return ((uint64_t)v.major << 48) |
           ((uint64_t)v.minor << 32) |
           ((uint64_t)v.build << 16) |
           ((uint64_t)v.revision);
}

static inline int scarlett_abi_compare(scarlett_abi_version_t a,
                                       scarlett_abi_version_t b) {
    uint64_t pa = scarlett_abi_pack_u64(a);
    uint64_t pb = scarlett_abi_pack_u64(b);
    return (pa > pb) - (pa < pb);
}

static inline bool scarlett_abi_is_valid_gxdk(scarlett_abi_version_t v) {
    return v.gxdk_magic == SCARLETT_GXDK_MAGIC;
}

/* ============================================================================
 * XDL Core Interface GUIDs & 32-Slot VTable Invariants
 * ============================================================================ */

#define SCARLETT_XDL_VTABLE_SLOTS UINT32_C(32)

typedef struct scarlett_guid_s {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} scarlett_guid_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_guid_t) == 16,
                       "scarlett_guid_t must be 16 bytes");

/* {073B7DCB-1FCF-4030-94BE-E3C9EB623428} — Primary 32-slot Game OS API dispatch */
#define SCARLETT_GUID_SYSTEM_DISPATCH_INIT                                     \
    {                                                                          \
        UINT32_C(0x073B7DCB), UINT16_C(0x1FCF), UINT16_C(0x4030),              \
        { 0x94u, 0xBEu, 0xE3u, 0xC9u, 0xEBu, 0x62u, 0x34u, 0x28u }             \
    }

/* {37E56907-2F10-41E8-B72F-36EDB185331A} — Secondary 32-slot Title/Platform dispatch */
#define SCARLETT_GUID_PLATFORM_DISPATCH_INIT                                   \
    {                                                                          \
        UINT32_C(0x37E56907), UINT16_C(0x2F10), UINT16_C(0x41E8),              \
        { 0xB7u, 0x2Fu, 0x36u, 0xEDu, 0xB1u, 0x85u, 0x33u, 0x1Au }             \
    }

/* ============================================================================
 * Console Hardware Profiles (PROTON_XBOX_MODEL)
 * ============================================================================ */

typedef enum scarlett_console_model_e {
    SCARLETT_MODEL_ANACONDA = 0, /* Xbox Series X (13.5 GiB title budget) */
    SCARLETT_MODEL_LOCKHART = 1, /* Xbox Series S (8.5 GiB title budget) */
    SCARLETT_MODEL_DEVKIT   = 2  /* Xbox Series X DevKit (40.0 GiB title budget) */
} scarlett_console_model_t;

/* Exact memory budgets in bytes (1 GiB = 1024^3 bytes) */
#define SCARLETT_BUDGET_ANACONDA_BYTES      UINT64_C(14495514624) /* 13.5 GiB */
#define SCARLETT_BUDGET_ANACONDA_FAST_BYTES UINT64_C(10737418240) /* 10.0 GiB */
#define SCARLETT_BUDGET_LOCKHART_BYTES      UINT64_C(9126805504)  /*  8.5 GiB */
#define SCARLETT_BUDGET_LOCKHART_FAST_BYTES UINT64_C(8589934592)  /*  8.0 GiB */
#define SCARLETT_BUDGET_DEVKIT_BYTES        UINT64_C(42949672960) /* 40.0 GiB */

static inline uint64_t
scarlett_model_memory_budget_bytes(scarlett_console_model_t model) {
    switch (model) {
        case SCARLETT_MODEL_ANACONDA: return SCARLETT_BUDGET_ANACONDA_BYTES;
        case SCARLETT_MODEL_LOCKHART: return SCARLETT_BUDGET_LOCKHART_BYTES;
        case SCARLETT_MODEL_DEVKIT:   return SCARLETT_BUDGET_DEVKIT_BYTES;
        default:                      return SCARLETT_BUDGET_ANACONDA_BYTES;
    }
}

/* ============================================================================
 * Virtual Address Ranges (MEM_GRAPHICS / MEM_TITLE), Aliasing & UMA Tiers
 * ============================================================================ */

#define SCARLETT_MEM_GRAPHICS_BASE   UINT64_C(0x0000000100000000)
#define SCARLETT_MEM_GRAPHICS_END    UINT64_C(0x000000FFFFFFFFFF)
#define SCARLETT_MEM_GRAPHICS_SIZE                                             \
    (SCARLETT_MEM_GRAPHICS_END - SCARLETT_MEM_GRAPHICS_BASE + UINT64_C(1))

#define SCARLETT_MEM_TITLE_BASE      UINT64_C(0x0000040000000000)
#define SCARLETT_MEM_TITLE_END       UINT64_C(0x000007FFFFFFFFFF)
#define SCARLETT_MEM_TITLE_SIZE                                                \
    (SCARLETT_MEM_TITLE_END - SCARLETT_MEM_TITLE_BASE + UINT64_C(1))

#define SCARLETT_PAGE_SIZE_4KB       UINT64_C(0x00001000)
#define SCARLETT_PAGE_SIZE_64KB      UINT64_C(0x00010000)
#define SCARLETT_PAGE_SIZE_2MB       UINT64_C(0x00200000)
#define SCARLETT_PAGE_SIZE_4MB       UINT64_C(0x00400000)

typedef enum scarlett_mem_region_e {
    SCARLETT_MEM_REGION_UNKNOWN  = 0,
    SCARLETT_MEM_REGION_GRAPHICS = 1,
    SCARLETT_MEM_REGION_TITLE    = 2
} scarlett_mem_region_t;

typedef enum scarlett_mem_access_e {
    SCARLETT_MEM_ACCESS_NOACCESS  = 0,
    SCARLETT_MEM_ACCESS_READONLY  = 1,
    SCARLETT_MEM_ACCESS_READWRITE = 2
} scarlett_mem_access_t;

typedef enum scarlett_mem_cache_attr_e {
    SCARLETT_MEM_CACHE_WRITEBACK     = 0,
    SCARLETT_MEM_CACHE_WRITECOMBINE  = 1,
    SCARLETT_MEM_CACHE_UNCACHED      = 2,
    SCARLETT_MEM_CACHE_GPU_COHERENT  = 3
} scarlett_mem_cache_attr_t;

typedef enum scarlett_uma_tier_e {
    SCARLETT_UMA_TIER_AUTO            = 0,
    SCARLETT_UMA_TIER_A_REBAR_DMABUF  = 1, /* VK_KHR_external_memory_fd + DMA-BUF */
    SCARLETT_UMA_TIER_B_EXT_HOST      = 2, /* VK_EXT_external_memory_host */
    SCARLETT_UMA_TIER_C_UFFD_WP       = 3  /* userfaultfd UFFDIO_WRITEPROTECT */
} scarlett_uma_tier_t;

static inline bool scarlett_addr_is_graphics(uint64_t addr, uint64_t size) {
    if (size == 0u || addr < SCARLETT_MEM_GRAPHICS_BASE) {
        return false;
    }
    if (addr > SCARLETT_MEM_GRAPHICS_END) {
        return false;
    }
    return (size - UINT64_C(1)) <= (SCARLETT_MEM_GRAPHICS_END - addr);
}

static inline bool scarlett_addr_is_title(uint64_t addr, uint64_t size) {
    if (size == 0u || addr < SCARLETT_MEM_TITLE_BASE) {
        return false;
    }
    if (addr > SCARLETT_MEM_TITLE_END) {
        return false;
    }
    return (size - UINT64_C(1)) <= (SCARLETT_MEM_TITLE_END - addr);
}

/* ============================================================================
 * XVD / XVC Package Header Bounds (Clean-Room Unencrypted Inspection Only)
 * ============================================================================ */

#define SCARLETT_XVD_SIGNATURE_OFFSET          UINT32_C(0x000)
#define SCARLETT_XVD_SIGNATURE_SIZE            UINT32_C(0x200)
#define SCARLETT_XVD_HEADER_OFFSET             UINT32_C(0x200)
#define SCARLETT_XVD_MAGIC_LE64                UINT64_C(0x6476782D7466736D) /* "msft-xvd" */

#define SCARLETT_XVD_VOLUME_FLAG_READ_ONLY           UINT32_C(0x00000001)
#define SCARLETT_XVD_VOLUME_FLAG_ENCRYPTION_DISABLED UINT32_C(0x00000002)

/* ============================================================================
 * Shader Lifting Modes (PROTON_XBOX_SHADER_MODE) & D3D12_XS / Descriptor Policy
 * ============================================================================ */

typedef enum scarlett_shader_mode_e {
    SCARLETT_SHADER_MODE_EXACT    = 0, /* Bit-exact RDNA2 semantics */
    SCARLETT_SHADER_MODE_ADAPTIVE = 1, /* Microprobe + computeKnownFPClass (default) */
    SCARLETT_SHADER_MODE_FAST_F32 = 2  /* Relaxed fast FP32 math */
} scarlett_shader_mode_t;

typedef enum scarlett_descriptor_backend_e {
    SCARLETT_DESC_BACKEND_AUTO               = 0,
    SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_HEAP   = 1, /* VK_EXT_descriptor_heap */
    SCARLETT_DESC_BACKEND_EXT_DESCRIPTOR_BUFFER = 2, /* VK_EXT_descriptor_buffer */
    SCARLETT_DESC_BACKEND_DESCRIPTOR_INDEXING   = 3  /* VK_EXT_descriptor_indexing */
} scarlett_descriptor_backend_t;

#define SCARLETT_D3D12_QUEUE_TYPE_DMA              UINT32_C(0x02)
#define SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS       UINT32_C(0x10)
#define SCARLETT_D3D12_DESC_HEAP_SAFE_LIMIT        UINT32_C(1000000)

/* ============================================================================
 * DirectStorage_XS Request Options (Exact 64-bit Bit Layout)
 *
 * Bit 0      (1 bit)  : ZlibDecompress
 * Bits 1..4  (4 bits) : BcpackMode
 * Bits 5..20 (16 bits): SwizzleMode
 * Bit 21     (1 bit)  : DestinationIsPhysicalPages
 * Bit 22     (1 bit)  : SourceType (0 = File, 1 = Memory)
 * Bit 23     (1 bit)  : SourceIsPhysicalPages
 * Bits 24..63(40 bits): Reserved (must be 0)
 * ============================================================================ */

typedef struct scarlett_dstorage_request_options_s {
    uint64_t raw;
} scarlett_dstorage_request_options_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_request_options_t) == 8,
                       "DSTORAGE_REQUEST_OPTIONS must be exactly 64 bits");

#define SCARLETT_DSTORAGE_OPT_ZLIB_SHIFT          0u
#define SCARLETT_DSTORAGE_OPT_ZLIB_MASK           UINT64_C(0x1)
#define SCARLETT_DSTORAGE_OPT_BCPACK_SHIFT        1u
#define SCARLETT_DSTORAGE_OPT_BCPACK_MASK         UINT64_C(0xF)
#define SCARLETT_DSTORAGE_OPT_SWIZZLE_SHIFT       5u
#define SCARLETT_DSTORAGE_OPT_SWIZZLE_MASK        UINT64_C(0xFFFF)
#define SCARLETT_DSTORAGE_OPT_DST_PHYS_SHIFT      21u
#define SCARLETT_DSTORAGE_OPT_DST_PHYS_MASK       UINT64_C(0x1)
#define SCARLETT_DSTORAGE_OPT_SRC_TYPE_SHIFT      22u
#define SCARLETT_DSTORAGE_OPT_SRC_TYPE_MASK       UINT64_C(0x1)
#define SCARLETT_DSTORAGE_OPT_SRC_PHYS_SHIFT      23u
#define SCARLETT_DSTORAGE_OPT_SRC_PHYS_MASK       UINT64_C(0x1)
#define SCARLETT_DSTORAGE_OPT_RESERVED_SHIFT      24u
#define SCARLETT_DSTORAGE_OPT_RESERVED_MASK       UINT64_C(0xFFFFFFFFFF)

static inline scarlett_dstorage_request_options_t
scarlett_dstorage_pack_options(bool zlib_decompress,
                               uint8_t bcpack_mode,
                               uint16_t swizzle_mode,
                               bool dst_is_physical_pages,
                               uint8_t source_type,
                               bool src_is_physical_pages) {
    scarlett_dstorage_request_options_t opt;
    opt.raw =
        (((uint64_t)(zlib_decompress ? 1u : 0u) & SCARLETT_DSTORAGE_OPT_ZLIB_MASK)
            << SCARLETT_DSTORAGE_OPT_ZLIB_SHIFT) |
        (((uint64_t)bcpack_mode & SCARLETT_DSTORAGE_OPT_BCPACK_MASK)
            << SCARLETT_DSTORAGE_OPT_BCPACK_SHIFT) |
        (((uint64_t)swizzle_mode & SCARLETT_DSTORAGE_OPT_SWIZZLE_MASK)
            << SCARLETT_DSTORAGE_OPT_SWIZZLE_SHIFT) |
        (((uint64_t)(dst_is_physical_pages ? 1u : 0u) & SCARLETT_DSTORAGE_OPT_DST_PHYS_MASK)
            << SCARLETT_DSTORAGE_OPT_DST_PHYS_SHIFT) |
        (((uint64_t)source_type & SCARLETT_DSTORAGE_OPT_SRC_TYPE_MASK)
            << SCARLETT_DSTORAGE_OPT_SRC_TYPE_SHIFT) |
        (((uint64_t)(src_is_physical_pages ? 1u : 0u) & SCARLETT_DSTORAGE_OPT_SRC_PHYS_MASK)
            << SCARLETT_DSTORAGE_OPT_SRC_PHYS_SHIFT);
    return opt;
}

static inline bool
scarlett_dstorage_opt_zlib(scarlett_dstorage_request_options_t opt) {
    return ((opt.raw >> SCARLETT_DSTORAGE_OPT_ZLIB_SHIFT) &
            SCARLETT_DSTORAGE_OPT_ZLIB_MASK) != 0u;
}

static inline uint8_t
scarlett_dstorage_opt_bcpack_mode(scarlett_dstorage_request_options_t opt) {
    return (uint8_t)((opt.raw >> SCARLETT_DSTORAGE_OPT_BCPACK_SHIFT) &
                     SCARLETT_DSTORAGE_OPT_BCPACK_MASK);
}

static inline uint16_t
scarlett_dstorage_opt_swizzle_mode(scarlett_dstorage_request_options_t opt) {
    return (uint16_t)((opt.raw >> SCARLETT_DSTORAGE_OPT_SWIZZLE_SHIFT) &
                      SCARLETT_DSTORAGE_OPT_SWIZZLE_MASK);
}

static inline bool
scarlett_dstorage_opt_dst_is_physical(scarlett_dstorage_request_options_t opt) {
    return ((opt.raw >> SCARLETT_DSTORAGE_OPT_DST_PHYS_SHIFT) &
            SCARLETT_DSTORAGE_OPT_DST_PHYS_MASK) != 0u;
}

static inline uint8_t
scarlett_dstorage_opt_source_type(scarlett_dstorage_request_options_t opt) {
    return (uint8_t)((opt.raw >> SCARLETT_DSTORAGE_OPT_SRC_TYPE_SHIFT) &
                     SCARLETT_DSTORAGE_OPT_SRC_TYPE_MASK);
}

static inline bool
scarlett_dstorage_opt_src_is_physical(scarlett_dstorage_request_options_t opt) {
    return ((opt.raw >> SCARLETT_DSTORAGE_OPT_SRC_PHYS_SHIFT) &
            SCARLETT_DSTORAGE_OPT_SRC_PHYS_MASK) != 0u;
}

static inline uint64_t
scarlett_dstorage_opt_reserved(scarlett_dstorage_request_options_t opt) {
    return (opt.raw >> SCARLETT_DSTORAGE_OPT_RESERVED_SHIFT) &
           SCARLETT_DSTORAGE_OPT_RESERVED_MASK;
}

/* ============================================================================
 * Clean-Room Pluggable IBcpackDecoder ABI (PROTON_XBOX_BCPACK_PLUGIN)
 * ============================================================================ */

#define SCARLETT_BCPACK_PLUGIN_ABI_VERSION UINT32_C(1)
#define SCARLETT_BCPACK_CREATE_SYMBOL      "scarlett_bcpack_create_decoder"

typedef struct scarlett_bcpack_decoder_s scarlett_bcpack_decoder_t;

typedef scarlett_hresult_t (*scarlett_bcpack_decode_fn_t)(
    scarlett_bcpack_decoder_t *self,
    uint32_t bcpack_mode,
    const uint8_t *src,
    uint64_t src_size,
    uint8_t *dst,
    uint64_t dst_size,
    uint64_t *out_bytes_written);

typedef void (*scarlett_bcpack_destroy_fn_t)(scarlett_bcpack_decoder_t *self);

struct scarlett_bcpack_decoder_s {
    uint32_t abi_version;
    uint32_t reserved;
    scarlett_bcpack_decode_fn_t decode;
    scarlett_bcpack_destroy_fn_t destroy;
    void *impl_ctx;
};

typedef scarlett_hresult_t (*scarlett_bcpack_create_decoder_fn_t)(
    uint32_t requested_abi_version,
    scarlett_bcpack_decoder_t **out_decoder);

/* ============================================================================
 * Network Modes (PROTON_XBOX_NET_MODE) & 22 XGameRuntimeFeature Domains
 * ============================================================================ */

typedef enum scarlett_net_mode_e {
    SCARLETT_NET_MODE_OFFLINE     = 0, /* Deterministic offline isolation (default) */
    SCARLETT_NET_MODE_LAN         = 1, /* Loopback + RFC1918 / link-local only */
    SCARLETT_NET_MODE_PASSTHROUGH = 2  /* Full host network + system CA certs */
} scarlett_net_mode_t;

typedef enum scarlett_game_runtime_feature_e {
    SCARLETT_GRF_XACCESSIBILITY          = 0,
    SCARLETT_GRF_XAPPCAPTURE             = 1,
    SCARLETT_GRF_XASYNC                  = 2,
    SCARLETT_GRF_XASYNCPROVIDER          = 3,
    SCARLETT_GRF_XDISPLAY                = 4,
    SCARLETT_GRF_XGAME                   = 5,
    SCARLETT_GRF_XGAMEINVITE             = 6,
    SCARLETT_GRF_XGAMESAVE               = 7,
    SCARLETT_GRF_XGAMEUI                 = 8,
    SCARLETT_GRF_XLAUNCHER               = 9,
    SCARLETT_GRF_XNETWORKING             = 10,
    SCARLETT_GRF_XPACKAGE                = 11,
    SCARLETT_GRF_XPERSISTENTLOCALSTORAGE = 12,
    SCARLETT_GRF_XSPEECHSYNTHESIZER      = 13,
    SCARLETT_GRF_XSTORE                  = 14,
    SCARLETT_GRF_XSYSTEM                 = 15,
    SCARLETT_GRF_XTASKQUEUE              = 16,
    SCARLETT_GRF_XTHREAD                 = 17,
    SCARLETT_GRF_XUSER                   = 18,
    SCARLETT_GRF_XERROR                  = 19,
    SCARLETT_GRF_XGAMEEVENT              = 20,
    SCARLETT_GRF_XGAMESTREAMING          = 21,
    SCARLETT_GRF_COUNT                   = 22
} scarlett_game_runtime_feature_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_ABI_COMMON_H */
