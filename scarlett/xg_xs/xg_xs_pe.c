/*
 * Proton-Scarlett — xg_xs.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - ZERO references to libc, POSIX, <windows.h>, or libvulkan.
 *   - Crosses PE <-> xg_xs_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - All unimplemented exports route through scarlett_stub_thunk.h.
 */

#include "xg_xs_pe_common.h"

#if !defined(_WIN32)
static scarlett_unix_call_fn_t g_xg_xs_unix_bridge = scarlett_xg_xs_unix_call;

SCARLETT_XG_API void scarlett_xg_xs_set_unix_call_bridge(scarlett_unix_call_fn_t fn) {
    g_xg_xs_unix_bridge = fn;
}

SCARLETT_XG_API scarlett_unix_call_fn_t scarlett_xg_xs_get_unix_call_bridge(void) {
    return g_xg_xs_unix_bridge;
}
#endif

#define SCARLETT_XG_COM_POOL_CAP 4096u

typedef struct xg_com_computer_obj_s {
    IXGTextureAddressComputer iface;
    uint32_t in_use;
    uint32_t ref_count;
    uint64_t unix_handle;
} xg_com_computer_obj_t;

static xg_com_computer_obj_t g_xg_com_pool[SCARLETT_XG_COM_POOL_CAP];
static uint32_t g_xg_com_cursor = 0u;

static const scarlett_guid_t k_iid_iunknown = SCARLETT_IID_IUNKNOWN_INIT;
static const scarlett_guid_t k_iid_ixg_tex_computer = SCARLETT_IID_IXG_TEXTURE_ADDRESS_COMPUTER_INIT;

static scarlett_hresult_t SCARLETT_XG_CALL xg_com_query_interface(
    IXGTextureAddressComputer *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (self == NULL || riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (xg_pe_guid_equal(riid, &k_iid_iunknown) ||
        xg_pe_guid_equal(riid, &k_iid_ixg_tex_computer)) {
        *ppvObject = self;
        self->lpVtbl->AddRef(self);
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_XG_CALL xg_com_add_ref(IXGTextureAddressComputer *self) {
    if (self == NULL) {
        return 0u;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    return __atomic_add_fetch(&obj->ref_count, 1u, __ATOMIC_ACQ_REL);
}

static uint32_t SCARLETT_XG_CALL xg_com_release(IXGTextureAddressComputer *self) {
    if (self == NULL) {
        return 0u;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    uint32_t remaining = __atomic_sub_fetch(&obj->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (remaining == 0u) {
        uint64_t h = obj->unix_handle;
        obj->unix_handle = 0u;
        if (h != 0u) {
            scarlett_xg_destroy_computer_params_t params;
            xg_pe_zero_mem(&params, sizeof(params));
            params.computer_handle = h;
            (void)xg_pe_unix_call(SCARLETT_XG_XS_UNIX_DESTROY_COMPUTER, &params);
        }
        __atomic_store_n(&obj->in_use, 0u, __ATOMIC_RELEASE);
    }
    return remaining;
}

static scarlett_hresult_t SCARLETT_XG_CALL xg_com_get_resource_layout(
    IXGTextureAddressComputer *self,
    XG_RESOURCE_LAYOUT *pLayout) {
    if (self == NULL || pLayout == NULL) {
        return SCARLETT_E_POINTER;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    scarlett_xg_layout_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.computer_handle = obj->unix_handle;
    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_GET_RESOURCE_LAYOUT, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    xg_pe_copy_mem(pLayout, &params.resource_layout, sizeof(*pLayout));
    return params.out_status;
}

static scarlett_hresult_t SCARLETT_XG_CALL xg_com_get_subresource_layout(
    IXGTextureAddressComputer *self,
    uint32_t subresource,
    XG_SUBRESOURCE_LAYOUT *pSubLayout) {
    if (self == NULL || pSubLayout == NULL) {
        return SCARLETT_E_POINTER;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    scarlett_xg_layout_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.computer_handle = obj->unix_handle;
    params.subresource = subresource;
    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_GET_SUBRESOURCE_LAYOUT, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    xg_pe_copy_mem(pSubLayout, &params.subresource_layout, sizeof(*pSubLayout));
    return params.out_status;
}

static uint64_t SCARLETT_XG_CALL xg_com_get_texel_offset(
    IXGTextureAddressComputer *self,
    uint32_t plane,
    uint32_t mipLevel,
    uint64_t x,
    uint64_t y,
    uint32_t zOrSlice,
    uint32_t sample) {
    if (self == NULL) {
        return UINT64_MAX;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    scarlett_xg_texel_offset_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.computer_handle = obj->unix_handle;
    params.plane = plane;
    params.mip_level = mipLevel;
    params.x = x;
    params.y = y;
    params.z_or_slice = zOrSlice;
    params.sample = sample;
    params.out_offset_bytes = UINT64_MAX;
    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_GET_TEXEL_OFFSET, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return UINT64_MAX;
    }
    return params.out_offset_bytes;
}

static scarlett_hresult_t SCARLETT_XG_CALL xg_com_copy_into_subresource(
    IXGTextureAddressComputer *self,
    void *pDst,
    uint32_t dstPitch,
    uint32_t subresource,
    const void *pSrc) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    scarlett_xg_copy_subresource_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.computer_handle = obj->unix_handle;
    params.dst_addr = (uint64_t)(uintptr_t)pDst;
    params.src_addr = (uint64_t)(uintptr_t)pSrc;
    params.pitch_bytes = dstPitch;
    params.subresource = subresource;
    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_COPY_INTO_SUBRESOURCE, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

static scarlett_hresult_t SCARLETT_XG_CALL xg_com_copy_from_subresource(
    IXGTextureAddressComputer *self,
    void *pDst,
    uint32_t dstPitch,
    uint32_t subresource,
    const void *pSrc) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    xg_com_computer_obj_t *obj = (xg_com_computer_obj_t *)self;
    scarlett_xg_copy_subresource_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.computer_handle = obj->unix_handle;
    params.dst_addr = (uint64_t)(uintptr_t)pDst;
    params.src_addr = (uint64_t)(uintptr_t)pSrc;
    params.pitch_bytes = dstPitch;
    params.subresource = subresource;
    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_COPY_FROM_SUBRESOURCE, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

#define XG_COM_DEF_STUB_SLOT(slot_idx) \
    static const scarlett_stub_descriptor_t g_stub_xg_com_slot_##slot_idx = { \
        SCARLETT_STUB_THUNK_ABI_VERSION, \
        SCARLETT_STUB_FLAG_NONE, \
        SCARLETT_STUB_ORDINAL_NONE, \
        (uint32_t)(slot_idx), \
        SCARLETT_ERR_UNIMPLEMENTED_STUB, \
        0u, \
        "xg_xs.dll", \
        "IXGTextureAddressComputer::Slot" #slot_idx, \
    }; \
    static scarlett_hresult_t SCARLETT_XG_CALL xg_com_stub_slot_##slot_idx( \
        IXGTextureAddressComputer *self, \
        uint64_t arg1, \
        uint64_t arg2, \
        uint64_t arg3) { \
        return xg_pe_stub_dispatch_local( \
            &g_stub_xg_com_slot_##slot_idx, \
            (uint64_t)(uintptr_t)__builtin_return_address(0), \
            (uint64_t)(uintptr_t)self, \
            arg1, \
            arg2, \
            arg3); \
    }

XG_COM_DEF_STUB_SLOT(3)
XG_COM_DEF_STUB_SLOT(4)
XG_COM_DEF_STUB_SLOT(5)
XG_COM_DEF_STUB_SLOT(6)
XG_COM_DEF_STUB_SLOT(7)
XG_COM_DEF_STUB_SLOT(8)
XG_COM_DEF_STUB_SLOT(9)
XG_COM_DEF_STUB_SLOT(10)
XG_COM_DEF_STUB_SLOT(11)
XG_COM_DEF_STUB_SLOT(12)
XG_COM_DEF_STUB_SLOT(13)
XG_COM_DEF_STUB_SLOT(14)
XG_COM_DEF_STUB_SLOT(15)
XG_COM_DEF_STUB_SLOT(16)
XG_COM_DEF_STUB_SLOT(17)
XG_COM_DEF_STUB_SLOT(18)
XG_COM_DEF_STUB_SLOT(19)
XG_COM_DEF_STUB_SLOT(20)
XG_COM_DEF_STUB_SLOT(21)
XG_COM_DEF_STUB_SLOT(22)
XG_COM_DEF_STUB_SLOT(23)
XG_COM_DEF_STUB_SLOT(24)
XG_COM_DEF_STUB_SLOT(25)
XG_COM_DEF_STUB_SLOT(26)
XG_COM_DEF_STUB_SLOT(27)
XG_COM_DEF_STUB_SLOT(28)
XG_COM_DEF_STUB_SLOT(29)
XG_COM_DEF_STUB_SLOT(30)
XG_COM_DEF_STUB_SLOT(31)

static const IXGTextureAddressComputerVtbl g_xg_com_vtbl = {
    xg_com_query_interface,
    xg_com_add_ref,
    xg_com_release,
    xg_com_get_resource_layout,
    xg_com_get_texel_offset,
    xg_com_copy_into_subresource,
    xg_com_copy_from_subresource,
    xg_com_get_subresource_layout,
    {
        (void *)xg_com_stub_slot_8,
        (void *)xg_com_stub_slot_9,
        (void *)xg_com_stub_slot_10,
        (void *)xg_com_stub_slot_11,
        (void *)xg_com_stub_slot_12,
        (void *)xg_com_stub_slot_13,
        (void *)xg_com_stub_slot_14,
        (void *)xg_com_stub_slot_15,
        (void *)xg_com_stub_slot_16,
        (void *)xg_com_stub_slot_17,
        (void *)xg_com_stub_slot_18,
        (void *)xg_com_stub_slot_19,
        (void *)xg_com_stub_slot_20,
        (void *)xg_com_stub_slot_21,
        (void *)xg_com_stub_slot_22,
        (void *)xg_com_stub_slot_23,
        (void *)xg_com_stub_slot_24,
        (void *)xg_com_stub_slot_25,
        (void *)xg_com_stub_slot_26,
        (void *)xg_com_stub_slot_27,
        (void *)xg_com_stub_slot_28,
        (void *)xg_com_stub_slot_29,
        (void *)xg_com_stub_slot_30,
        (void *)xg_com_stub_slot_31,
    },
};

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGCreateTexture2DComputer(
    const XG_TEXTURE2D_DESC *pDesc,
    void **ppComputer) {
    if (pDesc == NULL || ppComputer == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppComputer = NULL;

    scarlett_xg_create_computer_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    xg_pe_copy_mem(&params.desc, pDesc, sizeof(*pDesc));

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_CREATE_TEX2D_COMPUTER, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (scarlett_failed(params.out_status)) {
        return params.out_status;
    }

    uint32_t start = __atomic_fetch_add(&g_xg_com_cursor, 1u, __ATOMIC_RELAXED) % SCARLETT_XG_COM_POOL_CAP;
    for (uint32_t i = 0u; i < SCARLETT_XG_COM_POOL_CAP; ++i) {
        uint32_t idx = (start + i) % SCARLETT_XG_COM_POOL_CAP;
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_xg_com_pool[idx].in_use, &expected, 1u, false,
                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            g_xg_com_pool[idx].iface.lpVtbl = &g_xg_com_vtbl;
            g_xg_com_pool[idx].ref_count = 1u;
            g_xg_com_pool[idx].unix_handle = params.out_computer_handle;
            *ppComputer = &g_xg_com_pool[idx].iface;
            return SCARLETT_OK;
        }
    }

    scarlett_xg_destroy_computer_params_t dparams;
    xg_pe_zero_mem(&dparams, sizeof(dparams));
    dparams.computer_handle = params.out_computer_handle;
    (void)xg_pe_unix_call(SCARLETT_XG_XS_UNIX_DESTROY_COMPUTER, &dparams);
    return SCARLETT_E_OUTOFMEMORY;
}

SCARLETT_XG_API uint32_t SCARLETT_XG_CALL XGComputeOptimalSwizzleMode(
    uint32_t resourceType,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t depthOrArray,
    uint32_t sampleCount,
    uint32_t bindFlags) {
    scarlett_xg_optimal_swizzle_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.resource_type = resourceType;
    params.format = format;
    params.width = width;
    params.height = height;
    params.depth_or_array = depthOrArray;
    params.sample_count = sampleCount;
    params.bind_flags = bindFlags;

    if (scarlett_failed(xg_pe_unix_call(SCARLETT_XG_XS_UNIX_COMPUTE_OPTIMAL_SWIZZLE, &params))) {
        return (uint32_t)XG_SWIZZLE_64KB_D;
    }
    return params.out_swizzle_mode;
}

typedef struct XG_D3D12_RESOURCE_DESC_S {
    uint32_t Dimension;
    uint32_t _pad0;
    uint64_t Alignment;
    uint64_t Width;
    uint32_t Height;
    uint16_t DepthOrArraySize;
    uint16_t MipLevels;
    uint32_t Format;
    uint32_t SampleCount;
    uint32_t SampleQuality;
    uint32_t Layout;
    uint32_t Flags;
    uint32_t _pad1;
} XG_D3D12_RESOURCE_DESC;

SCARLETT_STATIC_ASSERT(sizeof(XG_D3D12_RESOURCE_DESC) == 56,
                       "XG_D3D12_RESOURCE_DESC must be 56 bytes");

static inline uint32_t xg_d3d12_flags_to_bind_flags(uint32_t d3d12_flags) {
    uint32_t bind_flags = 0u;
    if ((d3d12_flags & 0x1u) != 0u) {
        bind_flags |= 0x20u; /* D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET */
    }
    if ((d3d12_flags & 0x2u) != 0u) {
        bind_flags |= 0x40u; /* D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL */
    }
    return bind_flags;
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGSuggestSwizzleMode(
    const XG_D3D12_RESOURCE_DESC *pDesc,
    uint32_t flags,
    uint32_t *pOutLayout) {
    (void)flags;
    if (pDesc == NULL || pOutLayout == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (pDesc->Dimension == 1u) {
        *pOutLayout = UINT32_C(0x100) | (uint32_t)XG_SWIZZLE_LINEAR;
        return SCARLETT_OK;
    }
    uint32_t width = (pDesc->Width > UINT64_C(0xFFFFFFFF))
                         ? UINT32_C(0xFFFFFFFF)
                         : (uint32_t)pDesc->Width;
    if (width == 0u) {
        width = 1u;
    }
    uint32_t height = (pDesc->Height == 0u) ? 1u : pDesc->Height;
    uint32_t depth_or_array = (pDesc->DepthOrArraySize == 0u) ? 1u : (uint32_t)pDesc->DepthOrArraySize;
    uint32_t sample_count = (pDesc->SampleCount == 0u) ? 1u : pDesc->SampleCount;
    uint32_t bind_flags = xg_d3d12_flags_to_bind_flags(pDesc->Flags);

    uint32_t swizzle = XGComputeOptimalSwizzleMode(
        pDesc->Dimension,
        pDesc->Format,
        width,
        height,
        depth_or_array,
        sample_count,
        bind_flags);
    *pOutLayout = UINT32_C(0x100) | (swizzle & 0xFFu);
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_XG_CALL xg_scarlett_com_copy_into_subresource(
    IXGTextureAddressComputer *self,
    void *pSurfaceBaseAddress,
    uint32_t plane,
    uint32_t subresource,
    const void *pSrcData,
    uint32_t srcRowPitch,
    uint32_t srcSlicePitch) {
    (void)plane;
    (void)srcSlicePitch;
    return xg_com_copy_into_subresource(
        self, pSurfaceBaseAddress, srcRowPitch, subresource, pSrcData);
}

static scarlett_hresult_t SCARLETT_XG_CALL xg_scarlett_com_copy_from_subresource(
    IXGTextureAddressComputer *self,
    void *pDstData,
    uint32_t plane,
    uint32_t subresource,
    const void *pSurfaceBaseAddress,
    uint32_t dstRowPitch,
    uint32_t dstSlicePitch) {
    (void)plane;
    (void)dstSlicePitch;
    return xg_com_copy_from_subresource(
        self, pDstData, dstRowPitch, subresource, pSurfaceBaseAddress);
}

typedef struct XG_PACKED_MIP_DESC_S {
    uint8_t NumStandardMips;
    uint8_t NumPackedMips;
    uint16_t _pad;
    uint32_t NumTilesForPackedMips;
    uint32_t StartTileIndexInOverallResource;
} XG_PACKED_MIP_DESC;

typedef struct XG_TILE_SHAPE_S {
    uint32_t WidthInTexels;
    uint32_t HeightInTexels;
    uint32_t DepthInTexels;
} XG_TILE_SHAPE;

typedef struct XG_SUBRESOURCE_TILING_S {
    uint32_t WidthInTiles;
    uint16_t HeightInTiles;
    uint16_t DepthInTiles;
    uint32_t StartTileIndexInOverallResource;
} XG_SUBRESOURCE_TILING;

SCARLETT_STATIC_ASSERT(sizeof(XG_PACKED_MIP_DESC) == 12, "XG_PACKED_MIP_DESC must be 12 bytes");
SCARLETT_STATIC_ASSERT(sizeof(XG_TILE_SHAPE) == 12, "XG_TILE_SHAPE must be 12 bytes");
SCARLETT_STATIC_ASSERT(sizeof(XG_SUBRESOURCE_TILING) == 12, "XG_SUBRESOURCE_TILING must be 12 bytes");

static scarlett_hresult_t SCARLETT_XG_CALL xg_scarlett_com_get_resource_tiling(
    IXGTextureAddressComputer *self,
    uint32_t *pNumTilesForEntireResource,
    XG_PACKED_MIP_DESC *pPackedMipDesc,
    XG_TILE_SHAPE *pStandardTileShapeForNonPackedMips,
    uint32_t *pNumSubresourceTilings,
    uint32_t firstSubresourceTilingToGet,
    XG_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }

    XG_RESOURCE_LAYOUT layout;
    xg_pe_zero_mem(&layout, sizeof(layout));
    scarlett_hresult_t hr = xg_com_get_resource_layout(self, &layout);
    if (scarlett_failed(hr)) {
        return hr;
    }

    uint32_t mip_levels = (layout.MipLevels > 0u && layout.MipLevels <= 15u)
                              ? layout.MipLevels
                              : 1u;
    uint32_t num_std = (mip_levels > 5u) ? 5u : mip_levels;
    uint32_t num_packed = mip_levels - num_std;

    if (pStandardTileShapeForNonPackedMips != NULL) {
        pStandardTileShapeForNonPackedMips->WidthInTexels = 128u;
        pStandardTileShapeForNonPackedMips->HeightInTexels = 128u;
        pStandardTileShapeForNonPackedMips->DepthInTexels = 1u;
    }

    uint32_t cap = (pNumSubresourceTilings != NULL) ? *pNumSubresourceTilings : 0u;
    uint32_t avail = (firstSubresourceTilingToGet < num_std)
                         ? (num_std - firstSubresourceTilingToGet)
                         : 0u;
    uint32_t to_write = (cap < avail) ? cap : avail;

    uint32_t running_tile = 0u;
    for (uint32_t m = 0u; m < num_std; ++m) {
        XG_SUBRESOURCE_LAYOUT sub;
        xg_pe_zero_mem(&sub, sizeof(sub));
        uint32_t w_tiles = 1u;
        uint32_t h_tiles = 1u;
        if (scarlett_succeeded(xg_com_get_subresource_layout(self, m, &sub))) {
            w_tiles = (sub.WidthElements + 127u) / 128u;
            h_tiles = (sub.HeightElements + 127u) / 128u;
            if (w_tiles == 0u) w_tiles = 1u;
            if (h_tiles == 0u) h_tiles = 1u;
            if (h_tiles > 0xFFFFu) h_tiles = 0xFFFFu;
        }
        if (pSubresourceTilingsForNonPackedMips != NULL &&
            m >= firstSubresourceTilingToGet &&
            (m - firstSubresourceTilingToGet) < to_write) {
            uint32_t out_idx = m - firstSubresourceTilingToGet;
            pSubresourceTilingsForNonPackedMips[out_idx].WidthInTiles = w_tiles;
            pSubresourceTilingsForNonPackedMips[out_idx].HeightInTiles = (uint16_t)h_tiles;
            pSubresourceTilingsForNonPackedMips[out_idx].DepthInTiles = 1u;
            pSubresourceTilingsForNonPackedMips[out_idx].StartTileIndexInOverallResource = running_tile;
        }
        running_tile += w_tiles * h_tiles;
    }

    if (pNumSubresourceTilings != NULL) {
        *pNumSubresourceTilings = to_write;
    }

    uint32_t packed_tiles = (num_packed > 0u) ? 1u : 0u;
    if (pPackedMipDesc != NULL) {
        pPackedMipDesc->NumStandardMips = (uint8_t)num_std;
        pPackedMipDesc->NumPackedMips = (uint8_t)num_packed;
        pPackedMipDesc->_pad = 0u;
        pPackedMipDesc->NumTilesForPackedMips = packed_tiles;
        pPackedMipDesc->StartTileIndexInOverallResource = running_tile;
    }

    if (pNumTilesForEntireResource != NULL) {
        *pNumTilesForEntireResource = running_tile + packed_tiles;
    }
    return SCARLETT_OK;
}

/* Xbox Scarlett XGTextureAddressComputer vtable (no IUnknown::QueryInterface):
 *   0: AddRef, 1: Release, 2: GetResourceLayout, 3..7: size/align/mip/texel,
 *   8: CopyIntoSubresource, 9: CopyFromSubresource, 10: GetResourceTiling, 11..31: stub thunks.
 */
static const void * const g_xg_scarlett_com_vtbl[32] = {
    (const void *)xg_com_add_ref,
    (const void *)xg_com_release,
    (const void *)xg_com_get_resource_layout,
    (const void *)xg_com_stub_slot_3,
    (const void *)xg_com_stub_slot_4,
    (const void *)xg_com_stub_slot_5,
    (const void *)xg_com_stub_slot_6,
    (const void *)xg_com_stub_slot_7,
    (const void *)xg_scarlett_com_copy_into_subresource,
    (const void *)xg_scarlett_com_copy_from_subresource,
    (const void *)xg_scarlett_com_get_resource_tiling,
    (const void *)xg_com_stub_slot_11,
    (const void *)xg_com_stub_slot_12,
    (const void *)xg_com_stub_slot_13,
    (const void *)xg_com_stub_slot_14,
    (const void *)xg_com_stub_slot_15,
    (const void *)xg_com_stub_slot_16,
    (const void *)xg_com_stub_slot_17,
    (const void *)xg_com_stub_slot_18,
    (const void *)xg_com_stub_slot_19,
    (const void *)xg_com_stub_slot_20,
    (const void *)xg_com_stub_slot_21,
    (const void *)xg_com_stub_slot_22,
    (const void *)xg_com_stub_slot_23,
    (const void *)xg_com_stub_slot_24,
    (const void *)xg_com_stub_slot_25,
    (const void *)xg_com_stub_slot_26,
    (const void *)xg_com_stub_slot_27,
    (const void *)xg_com_stub_slot_28,
    (const void *)xg_com_stub_slot_29,
    (const void *)xg_com_stub_slot_30,
    (const void *)xg_com_stub_slot_31,
};

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGCreateTextureComputer(
    const XG_D3D12_RESOURCE_DESC *pDesc,
    void **ppComputer) {
    if (pDesc == NULL || ppComputer == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppComputer = NULL;

    XG_TEXTURE2D_DESC tex_desc;
    xg_pe_zero_mem(&tex_desc, sizeof(tex_desc));
    tex_desc.Width = (pDesc->Width > UINT64_C(0xFFFFFFFF))
                         ? UINT32_C(0xFFFFFFFF)
                         : (uint32_t)pDesc->Width;
    if (tex_desc.Width == 0u) {
        tex_desc.Width = 1u;
    }
    tex_desc.Height = (pDesc->Dimension == 2u || pDesc->Dimension == 1u || pDesc->Height == 0u)
                          ? 1u
                          : pDesc->Height;
    tex_desc.MipLevels = (uint32_t)pDesc->MipLevels;
    tex_desc.ArraySize = (pDesc->DepthOrArraySize == 0u) ? 1u : (uint32_t)pDesc->DepthOrArraySize;
    tex_desc.Format = pDesc->Format;
    tex_desc.SampleCount = (pDesc->SampleCount == 0u) ? 1u : pDesc->SampleCount;
    tex_desc.SampleQuality = pDesc->SampleQuality;
    tex_desc.BindFlags = xg_d3d12_flags_to_bind_flags(pDesc->Flags);

    if ((pDesc->Layout & UINT32_C(0x100)) != 0u &&
        (pDesc->Layout & 0xFFu) <= (uint32_t)XG_SWIZZLE_64KB_S_X) {
        tex_desc.SwizzleMode = pDesc->Layout & 0xFFu;
    } else if (pDesc->Layout == 1u) {
        tex_desc.SwizzleMode = (uint32_t)XG_SWIZZLE_LINEAR;
    } else if (pDesc->Layout == 2u) {
        tex_desc.SwizzleMode = (uint32_t)XG_SWIZZLE_64KB_D;
    } else if (pDesc->Layout == 3u) {
        tex_desc.SwizzleMode = (uint32_t)XG_SWIZZLE_64KB_S;
    } else {
        tex_desc.SwizzleMode = XGComputeOptimalSwizzleMode(
            pDesc->Dimension,
            tex_desc.Format,
            tex_desc.Width,
            tex_desc.Height,
            tex_desc.ArraySize,
            tex_desc.SampleCount,
            tex_desc.BindFlags);
    }

    scarlett_hresult_t hr = XGCreateTexture2DComputer(&tex_desc, ppComputer);
    if (scarlett_succeeded(hr) && *ppComputer != NULL) {
        ((IXGTextureAddressComputer *)(*ppComputer))->lpVtbl =
            (const IXGTextureAddressComputerVtbl *)(const void *)g_xg_scarlett_com_vtbl;
    } else {
        xg_pe_ods_printf(
            "[xg_xs] XGCreateTextureComputer failed hr=0x%08x dim=%u w=%llu h=%u d=%u fmt=%u mips=%u layout=0x%x\n",
            (unsigned int)hr,
            (unsigned int)pDesc->Dimension,
            (unsigned long long)pDesc->Width,
            (unsigned int)pDesc->Height,
            (unsigned int)pDesc->DepthOrArraySize,
            (unsigned int)pDesc->Format,
            (unsigned int)pDesc->MipLevels,
            (unsigned int)pDesc->Layout);
    }
    return hr;
}

SCARLETT_XG_API uint32_t SCARLETT_XG_CALL XGComputeOptimalTileMode(
    uint32_t resourceType,
    uint32_t format,
    uint32_t width,
    uint32_t height) {
    return XGComputeOptimalSwizzleMode(resourceType, format, width, height, 1u, 1u, 0u);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGTextureAddressComputer_GetResourceLayout(
    IXGTextureAddressComputer *pComputer,
    XG_RESOURCE_LAYOUT *pLayout) {
    return xg_com_get_resource_layout(pComputer, pLayout);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGTextureAddressComputer_GetSubresourceLayout(
    IXGTextureAddressComputer *pComputer,
    uint32_t subresource,
    XG_SUBRESOURCE_LAYOUT *pSubLayout) {
    return xg_com_get_subresource_layout(pComputer, subresource, pSubLayout);
}

SCARLETT_XG_API uint64_t SCARLETT_XG_CALL XGTextureAddressComputer_GetTexelElementOffsetBytes(
    IXGTextureAddressComputer *pComputer,
    uint32_t plane,
    uint32_t mipLevel,
    uint64_t x,
    uint64_t y,
    uint32_t zOrSlice,
    uint32_t sample) {
    return xg_com_get_texel_offset(pComputer, plane, mipLevel, x, y, zOrSlice, sample);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGTextureAddressComputer_CopyIntoSubresource(
    IXGTextureAddressComputer *pComputer,
    void *pDst,
    uint32_t dstPitch,
    uint32_t subresource,
    const void *pSrc) {
    return xg_com_copy_into_subresource(pComputer, pDst, dstPitch, subresource, pSrc);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGTextureAddressComputer_CopyFromSubresource(
    IXGTextureAddressComputer *pComputer,
    void *pDst,
    uint32_t dstPitch,
    uint32_t subresource,
    const void *pSrc) {
    return xg_com_copy_from_subresource(pComputer, pDst, dstPitch, subresource, pSrc);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGSelectTransferPath(
    const XG_GPU_CAPS_PROBE *pCaps,
    uint32_t swizzleMode,
    uint32_t format,
    XG_TRANSFER_PATH_DECISION *pOutDecision) {
    if (pCaps == NULL || pOutDecision == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xg_transfer_path_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    xg_pe_copy_mem(&params.caps, pCaps, sizeof(*pCaps));
    params.swizzle_mode = swizzleMode;
    params.format = format;

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_SELECT_TRANSFER_PATH, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    xg_pe_copy_mem(pOutDecision, &params.out_decision, sizeof(*pOutDecision));
    return params.out_status;
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGSparseCoalesceBinds(
    const ScarlettSparseTileBindRequest *requests,
    uint32_t requestCount,
    uint32_t aspectMask,
    ScarlettVkSparseImageMemoryBind *outImageBinds,
    uint32_t imageBindsCapacity,
    uint32_t *outImageBindCount,
    ScarlettVkSparseMemoryBind *outMemoryBinds,
    uint32_t memoryBindsCapacity,
    uint32_t *outMemoryBindCount,
    ScarlettSparseCoalesceStats *outStats) {
    scarlett_sfs_coalesce_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.requests_addr = (uint64_t)(uintptr_t)requests;
    params.request_count = requestCount;
    params.aspect_mask = aspectMask;
    params.out_image_binds_addr = (uint64_t)(uintptr_t)outImageBinds;
    params.image_binds_capacity = imageBindsCapacity;
    params.out_memory_binds_addr = (uint64_t)(uintptr_t)outMemoryBinds;
    params.memory_binds_capacity = memoryBindsCapacity;

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_SFS_COALESCE_BINDS, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (outImageBindCount != NULL) {
        *outImageBindCount = params.out_image_bind_count;
    }
    if (outMemoryBindCount != NULL) {
        *outMemoryBindCount = params.out_memory_bind_count;
    }
    if (outStats != NULL) {
        xg_pe_copy_mem(outStats, &params.out_stats, sizeof(*outStats));
    }
    return params.out_status;
}

/* Unimplemented exports routed strictly through diagnostic auto-stub thunk */
static const scarlett_stub_descriptor_t g_stub_xg_1d = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "xg_xs.dll",
    "XGCreateTexture1DComputer",
};

static const scarlett_stub_descriptor_t g_stub_xg_3d = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "xg_xs.dll",
    "XGCreateTexture3DComputer",
};

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGCreateTexture1DComputer(
    const void *pDesc,
    void **ppComputer) {
    return xg_pe_stub_dispatch_local(
        &g_stub_xg_1d,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pDesc,
        (uint64_t)(uintptr_t)ppComputer,
        0u,
        0u);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XGCreateTexture3DComputer(
    const void *pDesc,
    void **ppComputer) {
    return xg_pe_stub_dispatch_local(
        &g_stub_xg_3d,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pDesc,
        (uint64_t)(uintptr_t)ppComputer,
        0u,
        0u);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_XG_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    return xg_pe_dll_main(reason);
}
#endif
