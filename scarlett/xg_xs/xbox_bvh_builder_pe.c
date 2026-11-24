/*
 * Proton-Scarlett — XboxBvhBuilder.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - ZERO references to libc, POSIX, <windows.h>, or libvulkan.
 *   - Crosses PE <-> xg_xs_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - All unimplemented exports route through scarlett_stub_thunk.h.
 */

#include "xg_xs_pe_common.h"

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XboxBvhGetPrebuildInfo(
    const XBVH_BUILD_INPUTS *pInputs,
    XBVH_PREBUILD_INFO *pOutInfo) {
    if (pInputs == NULL || pOutInfo == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xbvh_prebuild_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    xg_pe_copy_mem(&params.inputs, pInputs, sizeof(*pInputs));

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_BVH_PREBUILD_INFO, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    xg_pe_copy_mem(pOutInfo, &params.out_info, sizeof(*pOutInfo));
    return params.out_status;
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XboxBvhBuildAccelerationStructure(
    const XBVH_BUILD_INPUTS *pInputs,
    void *pDstBvh,
    uint64_t dstCapacityBytes,
    const void *pSrcBvh,
    void *pScratch,
    uint64_t scratchCapacityBytes,
    uint64_t *pOutWrittenBytes) {
    if (pInputs == NULL || pDstBvh == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xbvh_build_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    xg_pe_copy_mem(&params.inputs, pInputs, sizeof(*pInputs));
    params.dst_bvh_addr = (uint64_t)(uintptr_t)pDstBvh;
    params.dst_bvh_capacity = dstCapacityBytes;
    params.src_bvh_addr = (uint64_t)(uintptr_t)pSrcBvh;
    params.scratch_addr = (uint64_t)(uintptr_t)pScratch;
    params.scratch_capacity = scratchCapacityBytes;

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_BVH_BUILD, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (pOutWrittenBytes != NULL) {
        *pOutWrittenBytes = params.out_written_bytes;
    }
    return params.out_status;
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XboxBvhIntersectRay(
    const void *pBvhData,
    const XBVH_RAY_DESC *pRay,
    XBVH_RAY_HIT *pOutHit) {
    if (pBvhData == NULL || pRay == NULL || pOutHit == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xbvh_ray_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.bvh_addr = (uint64_t)(uintptr_t)pBvhData;
    xg_pe_copy_mem(&params.ray, pRay, sizeof(*pRay));

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_BVH_INTERSECT_RAY, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    xg_pe_copy_mem(pOutHit, &params.out_hit, sizeof(*pOutHit));
    return params.out_status;
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XboxBvhMapToVulkanBuildInfo(
    const XBVH_BUILD_INPUTS *pInputs,
    uint32_t hasVkKhrAccelStruct,
    uint64_t dstAsHandle,
    uint64_t srcAsHandle,
    uint64_t scratchBda,
    ScarlettVkAccelTrianglesDataKHR *pOutGeometries,
    uint32_t geometriesCapacity,
    ScarlettVkAccelBuildRangeInfoKHR *pOutRanges,
    ScarlettVkAccelBuildGeometryInfoKHR *pOutBuildInfo) {
    if (pInputs == NULL || pOutBuildInfo == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xbvh_map_vk_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    xg_pe_copy_mem(&params.inputs, pInputs, sizeof(*pInputs));
    params.has_vk_khr_accel_struct = hasVkKhrAccelStruct;
    params.dst_as_handle = dstAsHandle;
    params.src_as_handle = srcAsHandle;
    params.scratch_bda = scratchBda;
    params.out_vk_geometries_addr = (uint64_t)(uintptr_t)pOutGeometries;
    params.vk_geometries_capacity = geometriesCapacity;
    params.out_vk_ranges_addr = (uint64_t)(uintptr_t)pOutRanges;

    int32_t rc = xg_pe_unix_call(SCARLETT_XG_XS_UNIX_BVH_MAP_VULKAN, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    xg_pe_copy_mem(pOutBuildInfo, &params.out_build_info, sizeof(*pOutBuildInfo));
    return params.out_status;
}

static const scarlett_stub_descriptor_t g_stub_bvh_procedural = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "XboxBvhBuilder.dll",
    "XboxBvhBuildProceduralGeometry",
};

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XboxBvhBuildProceduralGeometry(
    const void *pDesc,
    void *pDstBvh) {
    return xg_pe_stub_dispatch_local(
        &g_stub_bvh_procedural,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pDesc,
        (uint64_t)(uintptr_t)pDstBvh,
        0u,
        0u);
}

#define XBVH_DEF_STUB_DESC(var_name, sym_str) \
    static const scarlett_stub_descriptor_t var_name = { \
        SCARLETT_STUB_THUNK_ABI_VERSION, \
        SCARLETT_STUB_FLAG_NONE, \
        SCARLETT_STUB_ORDINAL_NONE, \
        SCARLETT_STUB_SLOT_NONE, \
        SCARLETT_ERR_UNIMPLEMENTED_STUB, \
        0u, \
        "XboxBvhBuilder.dll", \
        sym_str, \
    }

typedef struct XboxBvhBuilder_Obj_S {
    void *pDevice;
    uint64_t flags;
    uint64_t reserved;
} XboxBvhBuilder_Obj;

SCARLETT_STATIC_ASSERT(sizeof(XboxBvhBuilder_Obj) == 24,
                       "XboxBvhBuilder_Obj must fit in 24 bytes");

typedef struct XboxBvhPrebuildInfoDxr_S {
    uint64_t ResultDataMaxSizeInBytes;
    uint64_t ScratchDataSizeInBytes;
    uint64_t UpdateScratchDataSizeInBytes;
} XboxBvhPrebuildInfoDxr;

XBVH_DEF_STUB_DESC(g_stub_bvh_build_batched2x, "XboxBvhBuilder::BuildRaytracingAccelerationStructureBatched2X");
XBVH_DEF_STUB_DESC(g_stub_bvh_copy_as, "XboxBvhBuilder::CopyRaytracingAccelerationStructure");
XBVH_DEF_STUB_DESC(g_stub_bvh_prebuild_dxr, "XboxBvhBuilder::GetRaytracingAccelerationStructurePrebuildInfo");
XBVH_DEF_STUB_DESC(g_stub_bvh_rehydrate_batched2x, "XboxBvhBuilder::RehydrateRaytracingAccelerationStructureBatched2X");

SCARLETT_XG_API void * SCARLETT_XG_CALL XboxBvhBuilder_ctor(
    void *self,
    void *pDevice,
    uint8_t flag) {
    if (self != NULL) {
        XboxBvhBuilder_Obj *obj = (XboxBvhBuilder_Obj *)self;
        xg_pe_zero_mem(obj, sizeof(*obj));
        obj->pDevice = pDevice;
        obj->flags = (uint64_t)(flag & 1u);
    }
    xg_pe_ods_printf(
        "[XboxBvhBuilder] ctor self=0x%llx device=0x%llx flag=%u\n",
        (unsigned long long)(uintptr_t)self,
        (unsigned long long)(uintptr_t)pDevice,
        (unsigned int)(flag & 1u));
    return self;
}

SCARLETT_XG_API void SCARLETT_XG_CALL XboxBvhBuilder_dtor(void *self) {
    if (self != NULL) {
        xg_pe_zero_mem(self, sizeof(XboxBvhBuilder_Obj));
    }
}

SCARLETT_XG_API void SCARLETT_XG_CALL XboxBvhBuilder_BuildRaytracingAccelerationStructureBatched2X(
    void *self,
    void *pCmdList,
    uint32_t count,
    const void *pDescs) {
    (void)xg_pe_stub_dispatch_local(
        &g_stub_bvh_build_batched2x,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pCmdList,
        (uint64_t)count,
        (uint64_t)(uintptr_t)pDescs);
}

SCARLETT_XG_API void SCARLETT_XG_CALL XboxBvhBuilder_CopyRaytracingAccelerationStructure(
    void *self,
    void *pCmdList,
    uint64_t dst,
    uint64_t src) {
    (void)xg_pe_stub_dispatch_local(
        &g_stub_bvh_copy_as,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pCmdList,
        dst,
        src);
}

SCARLETT_XG_API void SCARLETT_XG_CALL XboxBvhBuilder_GetRaytracingAccelerationStructurePrebuildInfo(
    void *self,
    const void *pInputs,
    const void *pStreamDesc,
    void *pOutInfo) {
    if (pOutInfo != NULL) {
        XboxBvhPrebuildInfoDxr *info = (XboxBvhPrebuildInfoDxr *)pOutInfo;
        info->ResultDataMaxSizeInBytes = UINT64_C(65536);
        info->ScratchDataSizeInBytes = UINT64_C(65536);
        info->UpdateScratchDataSizeInBytes = UINT64_C(65536);
    }
    (void)xg_pe_stub_dispatch_local(
        &g_stub_bvh_prebuild_dxr,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pInputs,
        (uint64_t)(uintptr_t)pStreamDesc,
        (uint64_t)(uintptr_t)pOutInfo);
}

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL XboxBvhBuilder_RehydrateRaytracingAccelerationStructureBatched2X(
    void *self,
    void *pCmdList,
    uint32_t count,
    const void *pDescs) {
    return xg_pe_stub_dispatch_local(
        &g_stub_bvh_rehydrate_batched2x,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pCmdList,
        (uint64_t)count,
        (uint64_t)(uintptr_t)pDescs);
}

SCARLETT_XG_API void SCARLETT_XG_CALL XboxBvhBuilder_Set64BitMortonCodes(
    void *self,
    uint8_t enabled) {
    if (self != NULL) {
        XboxBvhBuilder_Obj *obj = (XboxBvhBuilder_Obj *)self;
        if (enabled != 0u) {
            obj->flags |= UINT64_C(2);
        } else {
            obj->flags &= ~UINT64_C(2);
        }
    }
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_XG_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    return xg_pe_dll_main(reason);
}
#endif
