/*
 * Proton-Scarlett — XG_XS / XboxBvhBuilder / SFS / PIXEVT ABI Contract
 *
 * Clean-room, freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLLs (xg_xs.dll, XboxBvhBuilder.dll, pixevt.dll)
 *   - Linux native unixlib (xg_xs_unix.so)
 *   - Host C++20 unit tests
 *
 * Strictly zero dependencies on <windows.h>, libc, POSIX, or <vulkan/vulkan.h>.
 * All cross-boundary structs use fixed-width types, explicit padding, and
 * SCARLETT_STATIC_ASSERT size guarantees.
 */

#ifndef SCARLETT_XG_XS_UNIX_ABI_H
#define SCARLETT_XG_XS_UNIX_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__MINGW32__)
#define SCARLETT_XG_CALL __stdcall
#if defined(__GNUC__) || defined(__clang__)
#define SCARLETT_XG_API __attribute__((visibility("default")))
#else
#define SCARLETT_XG_API __declspec(dllexport)
#endif
#else
#define SCARLETT_XG_CALL
#if defined(__GNUC__) || defined(__clang__)
#define SCARLETT_XG_API __attribute__((visibility("default")))
#else
#define SCARLETT_XG_API
#endif
#endif

#define SCARLETT_XG_XS_UNIX_MODULE_ID UINT32_C(6)

/* 40-bit GPU VA upper bound for Scarlett surface addressing (1 TiB = 2^40) */
#define SCARLETT_XG_40BIT_LIMIT_BYTES (UINT64_C(1) << 40)

/* ============================================================================
 * DXGI / Xbox Format Subset (8..128 bpp, BC1..BC7, BC6H, Depth/Stencil)
 * ============================================================================ */

typedef enum XG_FORMAT_E {
    XG_FORMAT_UNKNOWN                = 0,
    XG_FORMAT_R32G32B32A32_FLOAT     = 2,   /* 128 bpp */
    XG_FORMAT_R32G32B32A32_UINT      = 3,   /* 128 bpp */
    XG_FORMAT_R16G16B16A16_FLOAT     = 10,  /* 64 bpp */
    XG_FORMAT_R16G16B16A16_UNORM     = 11,  /* 64 bpp */
    XG_FORMAT_R32G32_FLOAT           = 16,  /* 64 bpp */
    XG_FORMAT_R32G32_UINT            = 17,  /* 64 bpp */
    XG_FORMAT_R10G10B10A2_UNORM      = 24,  /* 32 bpp */
    XG_FORMAT_R8G8B8A8_UNORM         = 28,  /* 32 bpp */
    XG_FORMAT_R8G8B8A8_UNORM_SRGB    = 29,  /* 32 bpp */
    XG_FORMAT_R8G8B8A8_UINT          = 30,  /* 32 bpp */
    XG_FORMAT_R16G16_FLOAT           = 34,  /* 32 bpp */
    XG_FORMAT_R32_FLOAT              = 41,  /* 32 bpp */
    XG_FORMAT_R32_UINT               = 42,  /* 32 bpp */
    XG_FORMAT_D32_FLOAT              = 40,  /* 32 bpp */
    XG_FORMAT_D24_UNORM_S8_UINT      = 45,  /* 32 bpp */
    XG_FORMAT_R8G8_UNORM             = 49,  /* 16 bpp */
    XG_FORMAT_R16_FLOAT              = 54,  /* 16 bpp */
    XG_FORMAT_D16_UNORM              = 55,  /* 16 bpp */
    XG_FORMAT_R16_UNORM              = 56,  /* 16 bpp */
    XG_FORMAT_R16_UINT               = 57,  /* 16 bpp */
    XG_FORMAT_R8_UNORM               = 61,  /* 8 bpp */
    XG_FORMAT_R8_UINT                = 62,  /* 8 bpp */
    XG_FORMAT_BC1_UNORM              = 71,  /* 64 bpe (4x4 block = 8 bytes) */
    XG_FORMAT_BC1_UNORM_SRGB         = 72,
    XG_FORMAT_BC2_UNORM              = 74,  /* 128 bpe (4x4 block = 16 bytes) */
    XG_FORMAT_BC2_UNORM_SRGB         = 75,
    XG_FORMAT_BC3_UNORM              = 77,  /* 128 bpe (4x4 block = 16 bytes) */
    XG_FORMAT_BC3_UNORM_SRGB         = 78,
    XG_FORMAT_BC4_UNORM              = 80,  /* 64 bpe (4x4 block = 8 bytes) */
    XG_FORMAT_BC4_SNORM              = 81,
    XG_FORMAT_BC5_UNORM              = 83,  /* 128 bpe (4x4 block = 16 bytes) */
    XG_FORMAT_BC5_SNORM              = 84,
    XG_FORMAT_B8G8R8A8_UNORM         = 87,  /* 32 bpp */
    XG_FORMAT_BC6H_UF16              = 95,  /* 128 bpe (4x4 block = 16 bytes) */
    XG_FORMAT_BC6H_SF16              = 96,
    XG_FORMAT_BC7_UNORM              = 98,  /* 128 bpe (4x4 block = 16 bytes) */
    XG_FORMAT_BC7_UNORM_SRGB         = 99
} XG_FORMAT;

/* ============================================================================
 * XG Swizzle Modes & Texture Layout Descriptors (aligned with xg_xs.xdl)
 * ============================================================================ */

typedef enum XG_SWIZZLE_MODE_E {
    XG_SWIZZLE_LINEAR   = 0,
    XG_SWIZZLE_4KB_D    = 1,
    XG_SWIZZLE_4KB_S    = 2,
    XG_SWIZZLE_64KB_S   = 3,
    XG_SWIZZLE_64KB_D   = 4,
    XG_SWIZZLE_64KB_R_X = 5,
    XG_SWIZZLE_64KB_D_X = 6,
    XG_SWIZZLE_64KB_S_X = 7,
    XG_SWIZZLE_64KB     = XG_SWIZZLE_64KB_D /* Canonical 64KB alias */
} XG_SWIZZLE_MODE;

typedef enum XG_TRANSFER_PATH_E {
    XG_TRANSFER_PATH_CPU_FALLBACK            = 0,
    XG_TRANSFER_PATH_RADV_ZERO_COPY          = 1,
    XG_TRANSFER_PATH_VULKAN_COMPUTE_DESWIZZLE = 2
} XG_TRANSFER_PATH;

typedef enum XG_GPU_VENDOR_E {
    XG_GPU_VENDOR_UNKNOWN = 0,
    XG_GPU_VENDOR_AMD     = 0x1002u,
    XG_GPU_VENDOR_NVIDIA  = 0x10DEu,
    XG_GPU_VENDOR_INTEL   = 0x8086u
} XG_GPU_VENDOR;

typedef enum XG_VK_DRIVER_ID_E {
    XG_VK_DRIVER_UNKNOWN            = 0,
    XG_VK_DRIVER_MESA_RADV          = 3,
    XG_VK_DRIVER_NVIDIA_PROPRIETARY = 4,
    XG_VK_DRIVER_INTEL_OPEN_MESA    = 6
} XG_VK_DRIVER_ID;

/* AMD DRM_FORMAT_MOD_AMD_GFX10_64KB_D (clean-room DRM FourCC encoding) */
#define SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_D   UINT64_C(0x0200000010A00401)
#define SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_S   UINT64_C(0x0200000010A00301)
#define SCARLETT_DRM_FORMAT_MOD_AMD_GFX10_64KB_R_X UINT64_C(0x0200000010A00501)
#define SCARLETT_DRM_FORMAT_MOD_LINEAR             UINT64_C(0x0000000000000000)

typedef struct XG_SUBRESOURCE_LAYOUT_S {
    uint64_t SizeBytes;
    uint64_t OffsetBytes;
    uint64_t RowPitchBytes;
    uint64_t DepthPitchBytes;
    uint32_t WidthElements;
    uint32_t HeightElements;
    uint32_t Depth;
    uint32_t TileMode;
} XG_SUBRESOURCE_LAYOUT;

SCARLETT_STATIC_ASSERT(sizeof(XG_SUBRESOURCE_LAYOUT) == 48,
                       "XG_SUBRESOURCE_LAYOUT must be 48 bytes");

typedef struct XG_RESOURCE_LAYOUT_S {
    uint64_t SizeBytes;
    uint64_t BaseAlignmentBytes;
    uint32_t MipLevels;
    uint32_t Planes;
    uint32_t SwizzleMode; /* XG_SWIZZLE_MODE */
    uint32_t Flags;
} XG_RESOURCE_LAYOUT;

SCARLETT_STATIC_ASSERT(sizeof(XG_RESOURCE_LAYOUT) == 32,
                       "XG_RESOURCE_LAYOUT must be 32 bytes");

typedef struct XG_TEXTURE2D_DESC_S {
    uint32_t Width;
    uint32_t Height;
    uint32_t MipLevels;
    uint32_t ArraySize;
    uint32_t Format;
    uint32_t SampleCount;
    uint32_t SampleQuality;
    uint32_t Usage;
    uint32_t BindFlags;
    uint32_t MiscFlags;
    uint32_t SwizzleMode; /* XG_SWIZZLE_MODE */
    uint32_t PitchAlignment;
    uint32_t SamplerFeedbackMipRegionWidth;
    uint32_t SamplerFeedbackMipRegionHeight;
} XG_TEXTURE2D_DESC;

SCARLETT_STATIC_ASSERT(sizeof(XG_TEXTURE2D_DESC) == 56,
                       "XG_TEXTURE2D_DESC must be 56 bytes");

/* {A510E860-6632-449B-A547-6A6D2F09D3C1} */
#define SCARLETT_IID_IXG_TEXTURE_ADDRESS_COMPUTER_INIT                         \
    {                                                                          \
        UINT32_C(0xA510E860), UINT16_C(0x6632), UINT16_C(0x449B),              \
        { 0xA5u, 0x47u, 0x6Au, 0x6Du, 0x2Fu, 0x09u, 0xD3u, 0xC1u }             \
    }

/* {00000000-0000-0000-C000-000000000046} */
#define SCARLETT_IID_IUNKNOWN_INIT                                             \
    {                                                                          \
        UINT32_C(0x00000000), UINT16_C(0x0000), UINT16_C(0x0000),              \
        { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }             \
    }

typedef struct IXGTextureAddressComputer IXGTextureAddressComputer;

typedef struct IXGTextureAddressComputerVtbl {
    scarlett_hresult_t (SCARLETT_XG_CALL *QueryInterface)(
        IXGTextureAddressComputer *self,
        const scarlett_guid_t *riid,
        void **ppvObject);
    uint32_t (SCARLETT_XG_CALL *AddRef)(IXGTextureAddressComputer *self);
    uint32_t (SCARLETT_XG_CALL *Release)(IXGTextureAddressComputer *self);
    scarlett_hresult_t (SCARLETT_XG_CALL *GetResourceLayout)(
        IXGTextureAddressComputer *self,
        XG_RESOURCE_LAYOUT *pLayout);
    uint64_t (SCARLETT_XG_CALL *GetTexelElementOffsetBytes)(
        IXGTextureAddressComputer *self,
        uint32_t plane,
        uint32_t mipLevel,
        uint64_t x,
        uint64_t y,
        uint32_t zOrSlice,
        uint32_t sample);
    scarlett_hresult_t (SCARLETT_XG_CALL *CopyIntoSubresource)(
        IXGTextureAddressComputer *self,
        void *pDst,
        uint32_t dstPitch,
        uint32_t subresource,
        const void *pSrc);
    scarlett_hresult_t (SCARLETT_XG_CALL *CopyFromSubresource)(
        IXGTextureAddressComputer *self,
        void *pDst,
        uint32_t dstPitch,
        uint32_t subresource,
        const void *pSrc);
    scarlett_hresult_t (SCARLETT_XG_CALL *GetSubresourceLayout)(
        IXGTextureAddressComputer *self,
        uint32_t subresource,
        XG_SUBRESOURCE_LAYOUT *pSubLayout);
} IXGTextureAddressComputerVtbl;

struct IXGTextureAddressComputer {
    const IXGTextureAddressComputerVtbl *lpVtbl;
};

typedef struct XG_GPU_CAPS_PROBE_S {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_id;
    uint32_t has_drm_format_modifier;
    uint32_t has_int64_atomics;
    uint32_t has_vulkan_compute;
    uint64_t drm_modifier_mask; /* Bitmask of supported AMD GFX10 modifiers */
} XG_GPU_CAPS_PROBE;

SCARLETT_STATIC_ASSERT(sizeof(XG_GPU_CAPS_PROBE) == 32,
                       "XG_GPU_CAPS_PROBE must be 32 bytes");

typedef struct XG_TRANSFER_PATH_DECISION_S {
    uint32_t path;             /* XG_TRANSFER_PATH */
    uint32_t zero_copy_active; /* 1 if RADV DRM modifier zero-copy selected */
    uint64_t drm_modifier;
    uint32_t spirv_validated;
    uint32_t reserved;
} XG_TRANSFER_PATH_DECISION;

SCARLETT_STATIC_ASSERT(sizeof(XG_TRANSFER_PATH_DECISION) == 24,
                       "XG_TRANSFER_PATH_DECISION must be 24 bytes");

/* ============================================================================
 * XboxBvhBuilder — RDNA2 BVH4 Layout & VK_KHR_acceleration_structure Mapping
 * ============================================================================ */

#define SCARLETT_BVH4_NODE_TRIANGLE  UINT32_C(0)
#define SCARLETT_BVH4_NODE_BOX32     UINT32_C(5)
#define SCARLETT_BVH4_INVALID_PTR    UINT32_C(0xFFFFFFFF)

typedef enum XBVH_BUILD_FLAGS_E {
    XBVH_BUILD_FLAG_NONE            = 0u,
    XBVH_BUILD_FLAG_ALLOW_UPDATE    = (1u << 0),
    XBVH_BUILD_FLAG_ALLOW_COMPACTION = (1u << 1),
    XBVH_BUILD_FLAG_PREFER_FAST_TRACE = (1u << 2),
    XBVH_BUILD_FLAG_PREFER_FAST_BUILD = (1u << 3),
    XBVH_BUILD_FLAG_PERFORM_UPDATE  = (1u << 5)
} XBVH_BUILD_FLAGS;

typedef enum XBVH_AS_TYPE_E {
    XBVH_AS_TYPE_TOP_LEVEL    = 0,
    XBVH_AS_TYPE_BOTTOM_LEVEL = 1
} XBVH_AS_TYPE;

typedef enum XBVH_RT_BACKEND_E {
    XBVH_RT_BACKEND_BDA_BVH4_SOFTWARE = 0,
    XBVH_RT_BACKEND_VK_KHR_ACCEL_STRUCT = 1
} XBVH_RT_BACKEND;

/* Matches D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO */
typedef struct XBVH_PREBUILD_INFO_S {
    uint64_t ResultDataMaxSizeInBytes;
    uint64_t ScratchDataSizeInBytes;
    uint64_t UpdateScratchDataSizeInBytes;
} XBVH_PREBUILD_INFO;

SCARLETT_STATIC_ASSERT(sizeof(XBVH_PREBUILD_INFO) == 24,
                       "XBVH_PREBUILD_INFO must be 24 bytes");

/* RDNA2 64-byte Triangle Leaf Node (BDA software traversal fallback) */
typedef struct ScarlettRdn2Bvh4TriLeafNode {
    float v0[3];
    float v1[3];
    float v2[3];
    uint32_t primitive_index;
    uint32_t geometry_index_and_flags;
    uint32_t instance_index;
    uint32_t parent_node_index;
    uint32_t reserved[3];
} ScarlettRdn2Bvh4TriLeafNode;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettRdn2Bvh4TriLeafNode) == 64,
                       "ScarlettRdn2Bvh4TriLeafNode must be 64 bytes");

/* RDNA2 128-byte FP32 Box4 Internal Node (4-way SAH hierarchy) */
typedef struct ScarlettRdn2Bvh4Box32Node {
    uint32_t child_ptrs[4]; /* (node_index << 3) | node_type, or INVALID_PTR */
    float min_x[4];
    float min_y[4];
    float min_z[4];
    float max_x[4];
    float max_y[4];
    float max_z[4];
    uint32_t child_count;
    uint32_t parent_node_index;
    uint32_t first_prim_or_leaf;
    uint32_t reserved;
} ScarlettRdn2Bvh4Box32Node;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettRdn2Bvh4Box32Node) == 128,
                       "ScarlettRdn2Bvh4Box32Node must be 128 bytes");

typedef struct XBVH_TRIANGLES_DESC_S {
    uint64_t vertex_buffer_addr;   /* Host/GPU pointer cast to uint64_t */
    uint64_t index_buffer_addr;    /* Optional uint16/uint32 index buffer */
    uint32_t vertex_count;
    uint32_t vertex_stride_bytes;  /* >= 12 */
    uint32_t index_count;          /* 0 if non-indexed */
    uint32_t index_format_bytes;   /* 0, 2, or 4 */
    uint32_t geometry_flags;
    uint32_t reserved;
} XBVH_TRIANGLES_DESC;

SCARLETT_STATIC_ASSERT(sizeof(XBVH_TRIANGLES_DESC) == 40,
                       "XBVH_TRIANGLES_DESC must be 40 bytes");

typedef struct XBVH_INSTANCE_DESC_S {
    float transform[12]; /* Row-major 3x4 affine matrix */
    uint32_t instance_id;
    uint32_t instance_mask;
    uint32_t instance_contribution_to_hit_group_index;
    uint32_t flags;
    uint64_t blas_header_addr; /* Pointer/BDA to BLAS ScarlettBvh4Header */
} XBVH_INSTANCE_DESC;

SCARLETT_STATIC_ASSERT(sizeof(XBVH_INSTANCE_DESC) == 72,
                       "XBVH_INSTANCE_DESC must be 72 bytes");

typedef struct XBVH_BUILD_INPUTS_S {
    uint32_t type;        /* XBVH_AS_TYPE */
    uint32_t flags;       /* XBVH_BUILD_FLAGS */
    uint32_t num_descs;   /* Geometry count (BLAS) or instance count (TLAS) */
    uint32_t reserved;
    uint64_t descs_addr;  /* Pointer to XBVH_TRIANGLES_DESC[] or XBVH_INSTANCE_DESC[] */
} XBVH_BUILD_INPUTS;

SCARLETT_STATIC_ASSERT(sizeof(XBVH_BUILD_INPUTS) == 24,
                       "XBVH_BUILD_INPUTS must be 24 bytes");

typedef struct XBVH_RAY_DESC_S {
    float origin[3];
    float t_min;
    float direction[3];
    float t_max;
    uint32_t instance_mask;
    uint32_t ray_flags;
} XBVH_RAY_DESC;

SCARLETT_STATIC_ASSERT(sizeof(XBVH_RAY_DESC) == 40,
                       "XBVH_RAY_DESC must be 40 bytes");

typedef struct XBVH_RAY_HIT_S {
    uint32_t hit;
    uint32_t primitive_index;
    uint32_t instance_index;
    uint32_t nodes_visited;
    float t;
    float u;
    float v;
    uint32_t reserved;
} XBVH_RAY_HIT;

SCARLETT_STATIC_ASSERT(sizeof(XBVH_RAY_HIT) == 32,
                       "XBVH_RAY_HIT must be 32 bytes");

/* Self-contained Vulkan VK_KHR_acceleration_structure ABI mirrors */
#define SCARLETT_VK_STYPE_ACCEL_BUILD_GEOMETRY_INFO_KHR 1000150000
#define SCARLETT_VK_STYPE_ACCEL_GEOMETRY_KHR            1000150006
#define SCARLETT_VK_STYPE_ACCEL_GEOMETRY_TRIANGLES_KHR  1000150003
#define SCARLETT_VK_STYPE_ACCEL_GEOMETRY_INSTANCES_KHR  1000150004
#define SCARLETT_VK_STYPE_ACCEL_BUILD_SIZES_INFO_KHR    1000150020

typedef struct ScarlettVkAccelBuildRangeInfoKHR {
    uint32_t primitiveCount;
    uint32_t primitiveOffset;
    uint32_t firstVertex;
    uint32_t transformOffset;
} ScarlettVkAccelBuildRangeInfoKHR;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettVkAccelBuildRangeInfoKHR) == 16,
                       "ScarlettVkAccelBuildRangeInfoKHR must be 16 bytes");

typedef struct ScarlettVkAccelTrianglesDataKHR {
    uint32_t sType;
    uint32_t reserved0;
    uint64_t pNext;
    uint32_t vertexFormat; /* VK_FORMAT_R32G32B32_SFLOAT = 106 */
    uint32_t reserved1;
    uint64_t vertexDataDeviceAddress;
    uint64_t vertexStride;
    uint32_t maxVertex;
    uint32_t indexType; /* VK_INDEX_TYPE_UINT16=0, UINT32=1, NONE_KHR=1000165000 */
    uint64_t indexDataDeviceAddress;
    uint64_t transformDataDeviceAddress;
} ScarlettVkAccelTrianglesDataKHR;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettVkAccelTrianglesDataKHR) == 64,
                       "ScarlettVkAccelTrianglesDataKHR must be 64 bytes");

typedef struct ScarlettVkAccelBuildGeometryInfoKHR {
    uint32_t sType;
    uint32_t type;   /* 0=TOP_LEVEL, 1=BOTTOM_LEVEL */
    uint32_t flags;
    uint32_t mode;   /* 0=BUILD, 1=UPDATE */
    uint64_t srcAccelerationStructure;
    uint64_t dstAccelerationStructure;
    uint32_t geometryCount;
    uint32_t backend_selected; /* XBVH_RT_BACKEND */
    uint64_t pGeometriesAddr;
    uint64_t scratchDataDeviceAddress;
} ScarlettVkAccelBuildGeometryInfoKHR;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettVkAccelBuildGeometryInfoKHR) == 56,
                       "ScarlettVkAccelBuildGeometryInfoKHR must be 56 bytes");

/* ============================================================================
 * SFS (Sampler Feedback Streaming) & 16K VSM vkQueueBindSparse Coalescing
 * ============================================================================ */

/*
 * SFS 64-bit Feedback Word Layout (updated via VK_EXT_shader_image_atomic_int64 / CPU atomic64):
 *   bits [3:0]   : log2(MipRegion) (0..15)
 *   bits [7:4]   : min_mip_requested (0..15, 0xF = unrequested)
 *   bits [15:8]  : residency_flags (bit0=resident, bit1=pending, bit2=dirty, bit3=evicted)
 *   bits [31:16] : frame_epoch
 *   bits [63:32] : tile_id
 */
#define SCARLETT_SFS_FLAG_RESIDENT UINT8_C(0x01)
#define SCARLETT_SFS_FLAG_PENDING  UINT8_C(0x02)
#define SCARLETT_SFS_FLAG_DIRTY    UINT8_C(0x04)
#define SCARLETT_SFS_FLAG_EVICTED  UINT8_C(0x08)

#define SCARLETT_SFS_TILE_SIZE_BYTES UINT64_C(65536) /* 64 KiB */

static inline uint64_t scarlett_sfs_pack_word(
    uint8_t log2_mip_region,
    uint8_t min_mip_requested,
    uint8_t flags,
    uint16_t frame_epoch,
    uint32_t tile_id) {
    return ((uint64_t)(log2_mip_region & 0x0Fu)) |
           (((uint64_t)(min_mip_requested & 0x0Fu)) << 4u) |
           (((uint64_t)flags) << 8u) |
           (((uint64_t)frame_epoch) << 16u) |
           (((uint64_t)tile_id) << 32u);
}

static inline uint8_t scarlett_sfs_decode_log2_mip_region(uint64_t word) {
    return (uint8_t)(word & UINT64_C(0xF));
}

static inline uint8_t scarlett_sfs_decode_min_mip(uint64_t word) {
    return (uint8_t)((word >> 4u) & UINT64_C(0xF));
}

static inline uint8_t scarlett_sfs_decode_flags(uint64_t word) {
    return (uint8_t)((word >> 8u) & UINT64_C(0xFF));
}

static inline uint16_t scarlett_sfs_decode_epoch(uint64_t word) {
    return (uint16_t)((word >> 16u) & UINT64_C(0xFFFF));
}

static inline uint32_t scarlett_sfs_decode_tile_id(uint64_t word) {
    return (uint32_t)((word >> 32u) & UINT64_C(0xFFFFFFFF));
}

typedef struct ScarlettSparseTileBindRequest {
    uint32_t mip_level;
    uint32_t array_layer;
    uint32_t tile_x;
    uint32_t tile_y;
    uint32_t tile_width_texels;
    uint32_t tile_height_texels;
    uint64_t memory_handle;   /* VkDeviceMemory handle (0 for unbind) */
    uint64_t memory_offset;
    uint64_t resource_offset; /* Byte offset for opaque VkSparseMemoryBind */
    uint32_t flags;
    uint32_t reserved;
} ScarlettSparseTileBindRequest;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettSparseTileBindRequest) == 56,
                       "ScarlettSparseTileBindRequest must be 56 bytes");

/* Vulkan ABI-compatible VkSparseMemoryBind & VkSparseImageMemoryBind */
typedef struct ScarlettVkSparseMemoryBind {
    uint64_t resourceOffset;
    uint64_t size;
    uint64_t memory;
    uint64_t memoryOffset;
    uint32_t flags;
    uint32_t reserved;
} ScarlettVkSparseMemoryBind;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettVkSparseMemoryBind) == 40,
                       "ScarlettVkSparseMemoryBind must be 40 bytes");

typedef struct ScarlettVkSparseImageMemoryBind {
    uint32_t aspectMask;
    uint32_t mipLevel;
    uint32_t arrayLayer;
    int32_t  offsetX;
    int32_t  offsetY;
    int32_t  offsetZ;
    uint32_t extentWidth;
    uint32_t extentHeight;
    uint32_t extentDepth;
    uint32_t flags;
    uint64_t memory;
    uint64_t memoryOffset;
} ScarlettVkSparseImageMemoryBind;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettVkSparseImageMemoryBind) == 56,
                       "ScarlettVkSparseImageMemoryBind must be 56 bytes");

typedef struct ScarlettSparseCoalesceStats {
    uint32_t input_tile_count;
    uint32_t coalesced_image_bind_count;
    uint32_t coalesced_memory_bind_count;
    uint32_t total_bound_64kb_tiles;
} ScarlettSparseCoalesceStats;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettSparseCoalesceStats) == 16,
                       "ScarlettSparseCoalesceStats must be 16 bytes");

/* ============================================================================
 * PIXEVT — VK_EXT_debug_utils Label Mapping
 * ============================================================================ */

#define SCARLETT_VK_STYPE_DEBUG_UTILS_LABEL_EXT 1000128002
#define SCARLETT_PIX_MAX_LABEL_LEN UINT32_C(128)
#define SCARLETT_PIX_MAX_STACK_DEPTH UINT32_C(256)

typedef struct ScarlettVkDebugUtilsLabelEXT {
    uint32_t sType;
    uint32_t reserved;
    uint64_t pNext;
    uint64_t pLabelNameAddr;
    float    color[4];
} ScarlettVkDebugUtilsLabelEXT;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettVkDebugUtilsLabelEXT) == 40,
                       "ScarlettVkDebugUtilsLabelEXT must be 40 bytes");

typedef struct ScarlettPixDebugUtilsRecord {
    uint32_t op_kind; /* 0=Begin, 1=End, 2=InsertMarker */
    uint32_t stack_depth;
    uint64_t command_list;
    float    color[4];
    char     label[SCARLETT_PIX_MAX_LABEL_LEN];
} ScarlettPixDebugUtilsRecord;

SCARLETT_STATIC_ASSERT(sizeof(ScarlettPixDebugUtilsRecord) == 160,
                       "ScarlettPixDebugUtilsRecord must be 160 bytes");

/* ============================================================================
 * WINE_UNIX_CALL Opcodes & Parameter Structs for xg_xs_unix.so
 * ============================================================================ */

typedef enum scarlett_xg_xs_unix_op_e {
    SCARLETT_XG_XS_UNIX_INIT                    = 0,
    SCARLETT_XG_XS_UNIX_CREATE_TEX2D_COMPUTER   = 1,
    SCARLETT_XG_XS_UNIX_DESTROY_COMPUTER        = 2,
    SCARLETT_XG_XS_UNIX_GET_RESOURCE_LAYOUT     = 3,
    SCARLETT_XG_XS_UNIX_GET_SUBRESOURCE_LAYOUT  = 4,
    SCARLETT_XG_XS_UNIX_GET_TEXEL_OFFSET        = 5,
    SCARLETT_XG_XS_UNIX_COPY_INTO_SUBRESOURCE   = 6,
    SCARLETT_XG_XS_UNIX_COPY_FROM_SUBRESOURCE   = 7,
    SCARLETT_XG_XS_UNIX_COMPUTE_OPTIMAL_SWIZZLE = 8,
    SCARLETT_XG_XS_UNIX_SELECT_TRANSFER_PATH    = 9,
    SCARLETT_XG_XS_UNIX_BVH_PREBUILD_INFO       = 10,
    SCARLETT_XG_XS_UNIX_BVH_BUILD               = 11,
    SCARLETT_XG_XS_UNIX_BVH_INTERSECT_RAY       = 12,
    SCARLETT_XG_XS_UNIX_BVH_MAP_VULKAN          = 13,
    SCARLETT_XG_XS_UNIX_SFS_COALESCE_BINDS      = 14,
    SCARLETT_XG_XS_UNIX_PIX_EVENT               = 15,
    SCARLETT_XG_XS_UNIX_REPORT_STUB             = 16,
    SCARLETT_XG_XS_UNIX_OP_COUNT                = 17
} scarlett_xg_xs_unix_op_t;

typedef struct scarlett_xg_create_computer_params_s {
    XG_TEXTURE2D_DESC desc;
    uint64_t out_computer_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xg_create_computer_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_create_computer_params_t) == 72,
                       "scarlett_xg_create_computer_params_t must be 72 bytes");

typedef struct scarlett_xg_destroy_computer_params_s {
    uint64_t computer_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xg_destroy_computer_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_destroy_computer_params_t) == 16,
                       "scarlett_xg_destroy_computer_params_t must be 16 bytes");

typedef struct scarlett_xg_layout_params_s {
    uint64_t computer_handle;
    uint32_t subresource;
    uint32_t reserved;
    XG_RESOURCE_LAYOUT resource_layout;
    XG_SUBRESOURCE_LAYOUT subresource_layout;
    scarlett_hresult_t out_status;
    uint32_t reserved2;
} scarlett_xg_layout_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_layout_params_t) == 104,
                       "scarlett_xg_layout_params_t must be 104 bytes");

typedef struct scarlett_xg_texel_offset_params_s {
    uint64_t computer_handle;
    uint64_t x;
    uint64_t y;
    uint32_t plane;
    uint32_t mip_level;
    uint32_t z_or_slice;
    uint32_t sample;
    uint64_t out_offset_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xg_texel_offset_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_texel_offset_params_t) == 56,
                       "scarlett_xg_texel_offset_params_t must be 56 bytes");

typedef struct scarlett_xg_copy_subresource_params_s {
    uint64_t computer_handle;
    uint64_t dst_addr;
    uint64_t src_addr;
    uint32_t pitch_bytes;
    uint32_t subresource;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xg_copy_subresource_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_copy_subresource_params_t) == 40,
                       "scarlett_xg_copy_subresource_params_t must be 40 bytes");

typedef struct scarlett_xg_optimal_swizzle_params_s {
    uint32_t resource_type;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t depth_or_array;
    uint32_t sample_count;
    uint32_t bind_flags;
    uint32_t out_swizzle_mode;
} scarlett_xg_optimal_swizzle_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_optimal_swizzle_params_t) == 32,
                       "scarlett_xg_optimal_swizzle_params_t must be 32 bytes");

typedef struct scarlett_xg_transfer_path_params_s {
    XG_GPU_CAPS_PROBE caps;
    uint32_t swizzle_mode;
    uint32_t format;
    XG_TRANSFER_PATH_DECISION out_decision;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xg_transfer_path_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_transfer_path_params_t) == 72,
                       "scarlett_xg_transfer_path_params_t must be 72 bytes");

typedef struct scarlett_xbvh_prebuild_params_s {
    XBVH_BUILD_INPUTS inputs;
    XBVH_PREBUILD_INFO out_info;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xbvh_prebuild_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xbvh_prebuild_params_t) == 56,
                       "scarlett_xbvh_prebuild_params_t must be 56 bytes");

typedef struct scarlett_xbvh_build_params_s {
    XBVH_BUILD_INPUTS inputs;
    uint64_t dst_bvh_addr;
    uint64_t dst_bvh_capacity;
    uint64_t src_bvh_addr;
    uint64_t scratch_addr;
    uint64_t scratch_capacity;
    uint64_t out_written_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xbvh_build_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xbvh_build_params_t) == 80,
                       "scarlett_xbvh_build_params_t must be 80 bytes");

typedef struct scarlett_xbvh_ray_params_s {
    uint64_t bvh_addr;
    XBVH_RAY_DESC ray;
    XBVH_RAY_HIT out_hit;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xbvh_ray_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xbvh_ray_params_t) == 88,
                       "scarlett_xbvh_ray_params_t must be 88 bytes");

typedef struct scarlett_xbvh_map_vk_params_s {
    XBVH_BUILD_INPUTS inputs;
    uint32_t has_vk_khr_accel_struct;
    uint32_t reserved0;
    uint64_t dst_as_handle;
    uint64_t src_as_handle;
    uint64_t scratch_bda;
    uint64_t out_vk_geometries_addr;
    uint32_t vk_geometries_capacity;
    uint32_t reserved1;
    uint64_t out_vk_ranges_addr;
    ScarlettVkAccelBuildGeometryInfoKHR out_build_info;
    scarlett_hresult_t out_status;
    uint32_t reserved2;
} scarlett_xbvh_map_vk_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xbvh_map_vk_params_t) == 144,
                       "scarlett_xbvh_map_vk_params_t must be 144 bytes");

typedef struct scarlett_sfs_coalesce_params_s {
    uint64_t requests_addr;
    uint32_t request_count;
    uint32_t aspect_mask;
    uint64_t out_image_binds_addr;
    uint32_t image_binds_capacity;
    uint32_t out_image_bind_count;
    uint64_t out_memory_binds_addr;
    uint32_t memory_binds_capacity;
    uint32_t out_memory_bind_count;
    ScarlettSparseCoalesceStats out_stats;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_sfs_coalesce_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_sfs_coalesce_params_t) == 72,
                       "scarlett_sfs_coalesce_params_t must be 72 bytes");

typedef struct scarlett_pix_event_params_s {
    uint32_t op_kind; /* 0=Begin, 1=End, 2=SetMarker, 3=QueryDepth, 4=SetEnabled */
    uint32_t enabled_flag;
    uint64_t command_list;
    uint64_t argb_color;
    uint64_t label_utf8_addr;
    uint32_t out_depth;
    uint32_t out_underflow_count;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_pix_event_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_pix_event_params_t) == 48,
                       "scarlett_pix_event_params_t must be 48 bytes");

typedef struct scarlett_xg_report_stub_params_s {
    scarlett_stub_context_t ctx;
    uint32_t out_action; /* scarlett_stub_action_t */
    scarlett_hresult_t out_status;
} scarlett_xg_report_stub_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xg_report_stub_params_t) == 480,
                       "scarlett_xg_report_stub_params_t must be 480 bytes");

/* Host-side unixlib dispatcher entry */
SCARLETT_XG_API int32_t scarlett_xg_xs_unix_call(uint32_t opcode, void *params);

#if !defined(_WIN32)
SCARLETT_XG_API void scarlett_xg_xs_set_unix_call_bridge(scarlett_unix_call_fn_t fn);
SCARLETT_XG_API scarlett_unix_call_fn_t scarlett_xg_xs_get_unix_call_bridge(void);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_XG_XS_UNIX_ABI_H */
