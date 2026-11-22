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

#define SCARLETT_XG_COM_POOL_CAP 1024u

typedef struct xg_com_computer_obj_s {
    IXGTextureAddressComputer iface;
    uint32_t in_use;
    uint32_t ref_count;
    uint64_t unix_handle;
} xg_com_computer_obj_t;

static xg_com_computer_obj_t g_xg_com_pool[SCARLETT_XG_COM_POOL_CAP];

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

static const IXGTextureAddressComputerVtbl g_xg_com_vtbl = {
    xg_com_query_interface,
    xg_com_add_ref,
    xg_com_release,
    xg_com_get_resource_layout,
    xg_com_get_texel_offset,
    xg_com_copy_into_subresource,
    xg_com_copy_from_subresource,
    xg_com_get_subresource_layout,
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

    for (uint32_t i = 0u; i < SCARLETT_XG_COM_POOL_CAP; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_xg_com_pool[i].in_use, &expected, 1u, false,
                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            g_xg_com_pool[i].iface.lpVtbl = &g_xg_com_vtbl;
            g_xg_com_pool[i].ref_count = 1u;
            g_xg_com_pool[i].unix_handle = params.out_computer_handle;
            *ppComputer = &g_xg_com_pool[i].iface;
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
