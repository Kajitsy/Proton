/*
 * Proton-Scarlett — xgameruntime.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - Zero direct references to libc, POSIX, or <windows.h>.
 *   - Crosses PE <-> xgameruntime_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - Implements 22 XGameRuntimeFeature domains, Win64 InitializeApiImpl / QueryApiImpl
 *     with 32-slot COM vtables (0 NULL slots), XTaskQueue, XAsync, XSystem, XThread,
 *     XUser, XPackage, XPersistentLocalStorage, XStore, XGameUI, XGameSave,
 *     XGameSaveFiles, XSAPI, PlayFab, and diagnostic auto-stub thunks.
 */

#include "xgameruntime_pe_bridge.h"

#if !defined(_WIN32)
static scarlett_unix_call_fn_t g_xgr_unix_bridge = scarlett_xgr_unix_call;

SCARLETT_XGR_API void scarlett_xgr_set_unix_call_bridge(scarlett_unix_call_fn_t fn) {
    g_xgr_unix_bridge = fn;
}

SCARLETT_XGR_API scarlett_unix_call_fn_t scarlett_xgr_get_unix_call_bridge(void) {
    return g_xgr_unix_bridge;
}
#endif

/* ============================================================================
 * Helpers for Async-over-Unixlib Operations
 * ============================================================================ */

static scarlett_hresult_t xgr_run_immediate_async(
    XAsyncBlock *async,
    const void *identity,
    scarlett_hresult_t hr,
    uint64_t req_buf_size,
    XAsyncProvider provider) {
    if (async == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_hresult_t bhr = XAsyncBegin(async, NULL, identity, "ScarlettAsync", provider);
    if (scarlett_failed(bhr)) {
        return bhr;
    }
    XAsyncComplete(async, hr, req_buf_size);
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_XGR_CALL xgr_noop_async_provider(
    XAsyncOp op,
    const XAsyncProviderData *data) {
    (void)data;
    if (op == XAsyncOp_Begin || op == XAsyncOp_GetResult || op == XAsyncOp_Cleanup) {
        return SCARLETT_OK;
    }
    return SCARLETT_OK;
}

/* ============================================================================
 * 1. Core Lifecycle & 22 XGameRuntimeFeature Domains
 * ============================================================================ */

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameRuntimeInitialize(void) {
    scarlett_unix_init_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    params.module_id = SCARLETT_XGR_UNIX_MODULE_ID;
    params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
    params.strict_mode = 0u;
    params.shader_mode = (uint32_t)SCARLETT_SHADER_MODE_ADAPTIVE;
    params.net_mode = (uint32_t)SCARLETT_NET_MODE_OFFLINE;

    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_INIT, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameRuntimeUninitialize(void) {
    uint32_t dummy = 0u;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_SHUTDOWN, &dummy);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameRuntimeIsFeatureAvailable(
    scarlett_game_runtime_feature_t feature,
    uint32_t *isAvailable) {
    if (isAvailable == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xgr_feature_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.feature = (uint32_t)feature;

    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_IS_FEATURE_AVAILABLE, &params);
    *isAvailable = params.out_available;
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

static const scarlett_stub_descriptor_t g_stub_get_diagnostics = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "xgameruntime.dll",
    "XGameRuntimeGetDiagnostics"
};

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameRuntimeGetDiagnostics(
    void *outBuf,
    uint32_t bufSize) {
    uint64_t ret_addr = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return xgr_pe_dispatch_stub(
        &g_stub_get_diagnostics,
        ret_addr,
        (uint64_t)(uintptr_t)outBuf,
        (uint64_t)bufSize,
        0u,
        0u);
}

/* ============================================================================
 * 2. Win64 Register ABI QueryApiImpl / InitializeApiImpl & 32-Slot COM VTables
 * ============================================================================ */

static int xgr_guid_eq(const scarlett_guid_t *a, const scarlett_guid_t *b) {
    if (a == NULL || b == NULL) return 0;
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) return 0;
    for (size_t i = 0u; i < 8u; ++i) {
        if (a->data4[i] != b->data4[i]) return 0;
    }
    return 1;
}

static const scarlett_guid_t g_iid_system_dispatch = SCARLETT_GUID_SYSTEM_DISPATCH_INIT;
static const scarlett_guid_t g_iid_platform_dispatch = SCARLETT_GUID_PLATFORM_DISPATCH_INIT;
static const scarlett_guid_t g_iid_unknown = {
    0x00000000u, 0x0000u, 0x0000u, {0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u}
};

typedef struct xgr_com_object_s {
    void *const *lpVtbl;
    uint32_t ref_count;
} xgr_com_object_t;

static xgr_com_object_t g_sys_dispatch_obj;
static xgr_com_object_t g_plat_dispatch_obj;

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI xgr_com_query_interface(
    void *self,
    const scarlett_guid_t *riid,
    void **ppv) {
    (void)self;
    if (ppv == NULL || riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    *ppv = NULL;
    if (xgr_guid_eq(riid, &g_iid_system_dispatch) || xgr_guid_eq(riid, &g_iid_unknown)) {
        *ppv = &g_sys_dispatch_obj;
        return SCARLETT_OK;
    }
    if (xgr_guid_eq(riid, &g_iid_platform_dispatch)) {
        *ppv = &g_plat_dispatch_obj;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_XGR_WIN64_ABI xgr_com_addref(void *self) {
    xgr_com_object_t *obj = (xgr_com_object_t *)self;
    if (obj != NULL) {
        return ++obj->ref_count;
    }
    return 1u;
}

static uint32_t SCARLETT_XGR_WIN64_ABI xgr_com_release(void *self) {
    xgr_com_object_t *obj = (xgr_com_object_t *)self;
    if (obj != NULL && obj->ref_count > 1u) {
        return --obj->ref_count;
    }
    return 1u;
}

/* System Dispatch implemented slots */
static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI sys_init_runtime(void *self) {
    (void)self;
    return XGameRuntimeInitialize();
}

static void SCARLETT_XGR_WIN64_ABI sys_shutdown_runtime(void *self) {
    (void)self;
    XGameRuntimeUninitialize();
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI sys_query_feature(
    void *self,
    scarlett_game_runtime_feature_t feature,
    uint32_t *isAvailable) {
    (void)self;
    return XGameRuntimeIsFeatureAvailable(feature, isAvailable);
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI sys_get_gameos_version(
    void *self,
    uint64_t *outPackedVersion) {
    (void)self;
    if (outPackedVersion == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_GAMEOS_VERSION;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params);
    if (scarlett_failed(rc)) return rc;
    *outPackedVersion = params.out_u64_a;
    return params.out_status;
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI sys_create_task_queue(
    void *self,
    uint32_t workMode,
    uint32_t completionMode,
    void **outQueue) {
    (void)self;
    if (outQueue == NULL) return SCARLETT_E_POINTER;
    XTaskQueueHandle q = 0u;
    scarlett_hresult_t hr = XTaskQueueCreate(
        (XTaskQueueDispatchMode)workMode,
        (XTaskQueueDispatchMode)completionMode,
        &q);
    *outQueue = (void *)(uintptr_t)q;
    return hr;
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI sys_submit_async(
    void *self,
    void *asyncBlock,
    void *workCallback) {
    (void)self;
    return XAsyncRun((XAsyncBlock *)asyncBlock, (XAsyncWork)workCallback);
}

static void SCARLETT_XGR_WIN64_ABI sys_cancel_async(void *self, void *asyncBlock) {
    (void)self;
    XAsyncCancel((XAsyncBlock *)asyncBlock);
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI sys_query_gxdk_caps(
    void *self,
    uint32_t *outCaps) {
    (void)self;
    if (outCaps == NULL) return SCARLETT_E_POINTER;
    *outCaps = SCARLETT_GXDK_MAGIC;
    return SCARLETT_OK;
}

/* Platform Dispatch implemented slots */
static uint32_t SCARLETT_XGR_WIN64_ABI plat_get_console_model(void *self) {
    (void)self;
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_DEVICE_TYPE;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params))) {
        return (uint32_t)SCARLETT_MODEL_ANACONDA;
    }
    return params.out_console_model;
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI plat_get_memory_budget(
    void *self,
    uint64_t *outTotalBytes,
    uint64_t *outFastBytes) {
    (void)self;
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_MEMORY_BUDGET;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params);
    if (scarlett_failed(rc)) return rc;
    if (outTotalBytes != NULL) *outTotalBytes = params.out_u64_a;
    if (outFastBytes != NULL) *outFastBytes = params.out_u64_b;
    return params.out_status;
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI plat_gs_init_provider(
    void *self,
    void *userHandle,
    const char *scid,
    uint32_t syncOnDemand,
    void **outProvider) {
    (void)self;
    if (outProvider == NULL) return SCARLETT_E_POINTER;
    XGameSaveProviderHandle ph = 0u;
    scarlett_hresult_t hr = XGameSaveInitializeProvider(
        (XUserHandle)(uintptr_t)userHandle,
        scid,
        syncOnDemand != 0u,
        &ph);
    *outProvider = (void *)(uintptr_t)ph;
    return hr;
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI plat_gs_commit_blob(
    void *self,
    void *updateHandle,
    const char *blobName,
    const uint8_t *data,
    uint32_t size) {
    (void)self;
    scarlett_hresult_t hr = XGameSaveSubmitBlobWrite(
        (XGameSaveUpdateHandle)(uintptr_t)updateHandle,
        blobName,
        data,
        (uint64_t)size);
    if (scarlett_failed(hr)) return hr;
    return XGameSaveSubmitUpdate((XGameSaveUpdateHandle)(uintptr_t)updateHandle);
}

static uint32_t SCARLETT_XGR_WIN64_ABI plat_xthread_is_ts(void *self) {
    (void)self;
    return XThreadIsTimeSensitive();
}

static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI plat_xthread_set_ts(
    void *self,
    uint32_t isTimeSensitive) {
    (void)self;
    return XThreadSetTimeSensitive(isTimeSensitive);
}

/* Diagnostic Auto-Stub Slots for 32-Slot VTables (0 NULL slots!) */
#define DEFINE_VTABLE_STUB_SLOT(prefix, slot_idx, flags_val, sym_str)          \
    static const scarlett_stub_descriptor_t g_desc_##prefix##_##slot_idx = {   \
        SCARLETT_STUB_THUNK_ABI_VERSION,                                       \
        (flags_val),                                                           \
        SCARLETT_STUB_ORDINAL_NONE,                                            \
        (slot_idx),                                                            \
        SCARLETT_ERR_UNIMPLEMENTED_STUB,                                       \
        0u,                                                                    \
        "xgameruntime.dll",                                                    \
        (sym_str)                                                              \
    };                                                                         \
    static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI                           \
    vtbl_##prefix##_slot##slot_idx(                                            \
        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {                  \
        uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);       \
        return xgr_pe_dispatch_stub(                                           \
            &g_desc_##prefix##_##slot_idx, ret, a0, a1, a2, a3);               \
    }

DEFINE_VTABLE_STUB_SLOT(sys, 11, SCARLETT_STUB_FLAG_XDL_VTABLE | SCARLETT_STUB_FLAG_REMOVED_IN_ABI, "IScarlettSystemDispatch::LegacyTelemetryFlush")
static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI vtbl_sys_slot12(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return SCARLETT_OK;
}
static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI vtbl_sys_slot13(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return SCARLETT_OK;
}
static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI vtbl_sys_slot14(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return SCARLETT_OK;
}
DEFINE_VTABLE_STUB_SLOT(sys, 15, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_15")
DEFINE_VTABLE_STUB_SLOT(sys, 16, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_16")
DEFINE_VTABLE_STUB_SLOT(sys, 17, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_17")
DEFINE_VTABLE_STUB_SLOT(sys, 18, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_18")
DEFINE_VTABLE_STUB_SLOT(sys, 19, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_19")
DEFINE_VTABLE_STUB_SLOT(sys, 20, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_20")
DEFINE_VTABLE_STUB_SLOT(sys, 21, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_21")
DEFINE_VTABLE_STUB_SLOT(sys, 22, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_22")
DEFINE_VTABLE_STUB_SLOT(sys, 23, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_23")
DEFINE_VTABLE_STUB_SLOT(sys, 24, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_24")
static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI vtbl_sys_slot25(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return SCARLETT_OK;
}
DEFINE_VTABLE_STUB_SLOT(sys, 26, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_26")
DEFINE_VTABLE_STUB_SLOT(sys, 27, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_27")
DEFINE_VTABLE_STUB_SLOT(sys, 28, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_28")
static scarlett_hresult_t SCARLETT_XGR_WIN64_ABI vtbl_sys_slot29(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)a0; (void)a1; (void)a2; (void)a3;
    return SCARLETT_OK;
}
DEFINE_VTABLE_STUB_SLOT(sys, 30, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSlot_30")
DEFINE_VTABLE_STUB_SLOT(sys, 31, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettSystemDispatch::ReservedSystemTail")

DEFINE_VTABLE_STUB_SLOT(plat, 9, SCARLETT_STUB_FLAG_XDL_VTABLE | SCARLETT_STUB_FLAG_REMOVED_IN_ABI, "IScarlettPlatformDispatch::LegacySuspendCallback")
DEFINE_VTABLE_STUB_SLOT(plat, 10, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::QueryTitleLicenseStub")
DEFINE_VTABLE_STUB_SLOT(plat, 11, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_11")
DEFINE_VTABLE_STUB_SLOT(plat, 12, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_12")
DEFINE_VTABLE_STUB_SLOT(plat, 13, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_13")
DEFINE_VTABLE_STUB_SLOT(plat, 14, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_14")
DEFINE_VTABLE_STUB_SLOT(plat, 15, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_15")
DEFINE_VTABLE_STUB_SLOT(plat, 16, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_16")
DEFINE_VTABLE_STUB_SLOT(plat, 17, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_17")
DEFINE_VTABLE_STUB_SLOT(plat, 18, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_18")
DEFINE_VTABLE_STUB_SLOT(plat, 19, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_19")
DEFINE_VTABLE_STUB_SLOT(plat, 20, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_20")
DEFINE_VTABLE_STUB_SLOT(plat, 21, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_21")
DEFINE_VTABLE_STUB_SLOT(plat, 22, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_22")
DEFINE_VTABLE_STUB_SLOT(plat, 23, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_23")
DEFINE_VTABLE_STUB_SLOT(plat, 24, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_24")
DEFINE_VTABLE_STUB_SLOT(plat, 25, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_25")
DEFINE_VTABLE_STUB_SLOT(plat, 26, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_26")
DEFINE_VTABLE_STUB_SLOT(plat, 27, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_27")
DEFINE_VTABLE_STUB_SLOT(plat, 28, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_28")
DEFINE_VTABLE_STUB_SLOT(plat, 29, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_29")
DEFINE_VTABLE_STUB_SLOT(plat, 30, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedSlot_30")
DEFINE_VTABLE_STUB_SLOT(plat, 31, SCARLETT_STUB_FLAG_XDL_VTABLE, "IScarlettPlatformDispatch::ReservedPlatformTail")

static void *const g_sys_dispatch_vtable[SCARLETT_XDL_VTABLE_SLOTS] = {
    (void *)(uintptr_t)xgr_com_query_interface,
    (void *)(uintptr_t)xgr_com_addref,
    (void *)(uintptr_t)xgr_com_release,
    (void *)(uintptr_t)sys_init_runtime,
    (void *)(uintptr_t)sys_shutdown_runtime,
    (void *)(uintptr_t)sys_query_feature,
    (void *)(uintptr_t)sys_get_gameos_version,
    (void *)(uintptr_t)sys_create_task_queue,
    (void *)(uintptr_t)sys_submit_async,
    (void *)(uintptr_t)sys_cancel_async,
    (void *)(uintptr_t)sys_query_gxdk_caps,
    (void *)(uintptr_t)vtbl_sys_slot11,
    (void *)(uintptr_t)vtbl_sys_slot12,
    (void *)(uintptr_t)vtbl_sys_slot13,
    (void *)(uintptr_t)vtbl_sys_slot14,
    (void *)(uintptr_t)vtbl_sys_slot15,
    (void *)(uintptr_t)vtbl_sys_slot16,
    (void *)(uintptr_t)vtbl_sys_slot17,
    (void *)(uintptr_t)vtbl_sys_slot18,
    (void *)(uintptr_t)vtbl_sys_slot19,
    (void *)(uintptr_t)vtbl_sys_slot20,
    (void *)(uintptr_t)vtbl_sys_slot21,
    (void *)(uintptr_t)vtbl_sys_slot22,
    (void *)(uintptr_t)vtbl_sys_slot23,
    (void *)(uintptr_t)vtbl_sys_slot24,
    (void *)(uintptr_t)vtbl_sys_slot25,
    (void *)(uintptr_t)vtbl_sys_slot26,
    (void *)(uintptr_t)vtbl_sys_slot27,
    (void *)(uintptr_t)vtbl_sys_slot28,
    (void *)(uintptr_t)vtbl_sys_slot29,
    (void *)(uintptr_t)vtbl_sys_slot30,
    (void *)(uintptr_t)vtbl_sys_slot31
};

static void *const g_plat_dispatch_vtable[SCARLETT_XDL_VTABLE_SLOTS] = {
    (void *)(uintptr_t)xgr_com_query_interface,
    (void *)(uintptr_t)xgr_com_addref,
    (void *)(uintptr_t)xgr_com_release,
    (void *)(uintptr_t)plat_get_console_model,
    (void *)(uintptr_t)plat_get_memory_budget,
    (void *)(uintptr_t)plat_gs_init_provider,
    (void *)(uintptr_t)plat_gs_commit_blob,
    (void *)(uintptr_t)plat_xthread_is_ts,
    (void *)(uintptr_t)plat_xthread_set_ts,
    (void *)(uintptr_t)vtbl_plat_slot9,
    (void *)(uintptr_t)vtbl_plat_slot10,
    (void *)(uintptr_t)vtbl_plat_slot11,
    (void *)(uintptr_t)vtbl_plat_slot12,
    (void *)(uintptr_t)vtbl_plat_slot13,
    (void *)(uintptr_t)vtbl_plat_slot14,
    (void *)(uintptr_t)vtbl_plat_slot15,
    (void *)(uintptr_t)vtbl_plat_slot16,
    (void *)(uintptr_t)vtbl_plat_slot17,
    (void *)(uintptr_t)vtbl_plat_slot18,
    (void *)(uintptr_t)vtbl_plat_slot19,
    (void *)(uintptr_t)vtbl_plat_slot20,
    (void *)(uintptr_t)vtbl_plat_slot21,
    (void *)(uintptr_t)vtbl_plat_slot22,
    (void *)(uintptr_t)vtbl_plat_slot23,
    (void *)(uintptr_t)vtbl_plat_slot24,
    (void *)(uintptr_t)vtbl_plat_slot25,
    (void *)(uintptr_t)vtbl_plat_slot26,
    (void *)(uintptr_t)vtbl_plat_slot27,
    (void *)(uintptr_t)vtbl_plat_slot28,
    (void *)(uintptr_t)vtbl_plat_slot29,
    (void *)(uintptr_t)vtbl_plat_slot30,
    (void *)(uintptr_t)vtbl_plat_slot31
};

SCARLETT_STATIC_ASSERT(sizeof(g_sys_dispatch_vtable) == 256u,
                       "IScarlettSystemDispatch vtable must be 32 slots (256 bytes)");
SCARLETT_STATIC_ASSERT(sizeof(g_plat_dispatch_vtable) == 256u,
                       "IScarlettPlatformDispatch vtable must be 32 slots (256 bytes)");

static xgr_com_object_t g_sys_dispatch_obj = { g_sys_dispatch_vtable, 1u };
static xgr_com_object_t g_plat_dispatch_obj = { g_plat_dispatch_vtable, 1u };

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_WIN64_ABI QueryApiImpl(
    uint64_t api_id,
    const scarlett_guid_t *riid,
    void **out_impl) {
    (void)api_id;
    if (riid == NULL || out_impl == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    *out_impl = NULL;
    if (xgr_guid_eq(riid, &g_iid_system_dispatch)) {
        *out_impl = &g_sys_dispatch_obj;
        return SCARLETT_OK;
    }
    if (xgr_guid_eq(riid, &g_iid_platform_dispatch)) {
        *out_impl = &g_plat_dispatch_obj;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_WIN64_ABI InitializeApiImpl(
    uint64_t title_version_packed,
    uint64_t build_hash) {
    (void)title_version_packed;
    (void)build_hash;
    return XGameRuntimeInitialize();
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_WIN64_ABI InitializeApiImplEx(
    uint64_t title_version_packed,
    uint64_t build_hash,
    uint32_t flags) {
    (void)title_version_packed;
    (void)build_hash;
    (void)flags;
    return XGameRuntimeInitialize();
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_WIN64_ABI InitializeApiImplEx2(
    uint64_t title_version_packed,
    uint64_t build_hash,
    uint32_t flags,
    void *reserved) {
    (void)title_version_packed;
    (void)build_hash;
    (void)flags;
    (void)reserved;
    return XGameRuntimeInitialize();
}

SCARLETT_XGR_API void SCARLETT_XGR_WIN64_ABI UninitializeApiImpl(void) {
    XGameRuntimeUninitialize();
}

SCARLETT_XGR_API void SCARLETT_XGR_WIN64_ABI XErrorReport(
    uint32_t hr,
    const char *msg) {
    (void)hr;
    (void)msg;
}

/* ============================================================================
 * 3. XTaskQueue & XAsync / XAsyncProvider
 * ============================================================================ */

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueCreate(
    XTaskQueueDispatchMode workDispatchMode,
    XTaskQueueDispatchMode completionDispatchMode,
    XTaskQueueHandle *queue) {
    if (queue == NULL) return SCARLETT_E_POINTER;
    *queue = 0u;
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.work_mode = (uint32_t)workDispatchMode;
    params.completion_mode = (uint32_t)completionDispatchMode;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_CREATE, &params);
    if (scarlett_failed(rc)) return rc;
    *queue = params.out_handle;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueCreateComposite(
    XTaskQueuePortHandle workPort,
    XTaskQueuePortHandle completionPort,
    XTaskQueueHandle *queue) {
    if (queue == NULL) return SCARLETT_E_POINTER;
    *queue = 0u;
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.work_port_handle = workPort;
    params.completion_port_handle = completionPort;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_CREATE_COMPOSITE, &params);
    if (scarlett_failed(rc)) return rc;
    *queue = params.out_handle;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueGetPort(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    XTaskQueuePortHandle *portHandle) {
    if (portHandle == NULL) return SCARLETT_E_POINTER;
    *portHandle = 0u;
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    params.port_kind = (uint32_t)port;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_GET_PORT, &params);
    if (scarlett_failed(rc)) return rc;
    *portHandle = params.out_handle;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueDuplicateHandle(
    XTaskQueueHandle queueHandle,
    XTaskQueueHandle *duplicatedHandle) {
    if (duplicatedHandle == NULL) return SCARLETT_E_POINTER;
    *duplicatedHandle = 0u;
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queueHandle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_DUP, &params);
    if (scarlett_failed(rc)) return rc;
    *duplicatedHandle = params.out_handle;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XTaskQueueCloseHandle(XTaskQueueHandle queue) {
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_CLOSE, &params);
}

SCARLETT_XGR_API bool SCARLETT_XGR_CALL XTaskQueueGetCurrentProcessTaskQueue(
    XTaskQueueHandle *queue) {
    if (queue == NULL) return false;
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.is_set_op = 0u;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_PROCESS_QUEUE, &params))) {
        return false;
    }
    *queue = params.out_handle;
    return scarlett_succeeded(params.out_status) && (*queue != 0u);
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XTaskQueueSetCurrentProcessTaskQueue(
    XTaskQueueHandle queue) {
    scarlett_xgr_taskq_create_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.is_set_op = 1u;
    params.queue_handle = queue;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_PROCESS_QUEUE, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueSubmitCallback(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    void *callbackContext,
    XTaskQueueCallback callback) {
    return XTaskQueueSubmitDelayedCallback(queue, port, 0u, callbackContext, callback);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueSubmitDelayedCallback(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    uint32_t delayMs,
    void *callbackContext,
    XTaskQueueCallback callback) {
    scarlett_xgr_taskq_submit_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    params.port = (uint32_t)port;
    params.delay_ms = delayMs;
    params.callback_addr = (uint64_t)(uintptr_t)callback;
    params.context_addr = (uint64_t)(uintptr_t)callbackContext;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_SUBMIT, &params);
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API bool SCARLETT_XGR_CALL XTaskQueueDispatch(
    XTaskQueueHandle queue,
    XTaskQueuePort port,
    uint32_t timeoutInMs) {
    scarlett_xgr_taskq_dispatch_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    params.port = (uint32_t)port;
    params.timeout_ms = timeoutInMs;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_DISPATCH, &params))) {
        return false;
    }
    return params.out_dispatched != 0u;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueTerminate(
    XTaskQueueHandle queue,
    bool wait,
    void *callbackContext,
    XTaskQueueTerminatedCallback callback) {
    scarlett_xgr_taskq_terminate_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    params.wait_for_completion = wait ? 1u : 0u;
    params.context_addr = (uint64_t)(uintptr_t)callbackContext;
    params.callback_addr = (uint64_t)(uintptr_t)callback;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_TERMINATE, &params);
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XTaskQueueRegisterMonitor(
    XTaskQueueHandle queue,
    void *callbackContext,
    XTaskQueueMonitorCallback callback,
    XTaskQueueRegistrationToken *token) {
    if (token == NULL) return SCARLETT_E_POINTER;
    *token = 0u;
    scarlett_xgr_taskq_monitor_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    params.context_addr = (uint64_t)(uintptr_t)callbackContext;
    params.callback_addr = (uint64_t)(uintptr_t)callback;
    params.is_unregister = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_MONITOR, &params);
    if (scarlett_failed(rc)) return rc;
    *token = params.token;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XTaskQueueUnregisterMonitor(
    XTaskQueueHandle queue,
    XTaskQueueRegistrationToken token) {
    scarlett_xgr_taskq_monitor_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.queue_handle = queue;
    params.token = token;
    params.is_unregister = 1u;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_TASKQ_MONITOR, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncBegin(
    XAsyncBlock *asyncBlock,
    void *context,
    const void *identity,
    const char *identityName,
    XAsyncProvider provider) {
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    params.context_addr = (uint64_t)(uintptr_t)context;
    params.identity_addr = (uint64_t)(uintptr_t)identity;
    params.identity_name_addr = (uint64_t)(uintptr_t)identityName;
    params.provider_addr = (uint64_t)(uintptr_t)provider;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_BEGIN, &params);
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncSchedule(
    XAsyncBlock *asyncBlock,
    uint32_t delayInMs) {
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    params.delay_ms = delayInMs;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_SCHEDULE, &params);
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XAsyncComplete(
    XAsyncBlock *asyncBlock,
    scarlett_hresult_t result,
    uint64_t requiredBufferSize) {
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    params.result_hr = result;
    params.buffer_size = requiredBufferSize;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_COMPLETE, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncGetStatus(
    XAsyncBlock *asyncBlock,
    bool wait) {
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    params.wait_flag = wait ? 1u : 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_GET_STATUS, &params);
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncGetResultSize(
    XAsyncBlock *asyncBlock,
    uint64_t *bufferSize) {
    if (bufferSize == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    params.wait_flag = 1u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_GET_RESULT, &params);
    *bufferSize = params.out_buffer_used;
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncGetResult(
    XAsyncBlock *asyncBlock,
    const void *identity,
    uint64_t bufferSize,
    void *buffer,
    uint64_t *bufferUsed) {
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    params.identity_addr = (uint64_t)(uintptr_t)identity;
    params.buffer_size = bufferSize;
    params.buffer_addr = (uint64_t)(uintptr_t)buffer;
    params.wait_flag = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_GET_RESULT, &params);
    if (bufferUsed != NULL) {
        *bufferUsed = params.out_buffer_used;
    }
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XAsyncCancel(XAsyncBlock *asyncBlock) {
    scarlett_xgr_async_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.async_block_addr = (uint64_t)(uintptr_t)asyncBlock;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_ASYNC_CANCEL, &params);
}

static scarlett_hresult_t SCARLETT_XGR_CALL xgr_async_run_provider(
    XAsyncOp op,
    const XAsyncProviderData *data) {
    if (op == XAsyncOp_Begin) {
        return XAsyncSchedule(data->async, 0u);
    }
    if (op == XAsyncOp_DoWork) {
        XAsyncWork work = (XAsyncWork)(uintptr_t)data->context;
        scarlett_hresult_t hr = (work != NULL) ? work(data->async) : SCARLETT_E_POINTER;
        XAsyncComplete(data->async, hr, 0u);
        return SCARLETT_OK;
    }
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XAsyncRun(
    XAsyncBlock *asyncBlock,
    XAsyncWork work) {
    if (asyncBlock == NULL || work == NULL) return SCARLETT_E_POINTER;
    return XAsyncBegin(asyncBlock, (void *)(uintptr_t)work, (const void *)(uintptr_t)work, "XAsyncRun", xgr_async_run_provider);
}

/* ============================================================================
 * 4. XSystem & XThread
 * ============================================================================ */

SCARLETT_XGR_API XSystemDeviceType SCARLETT_XGR_CALL XSystemGetDeviceType(void) {
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_DEVICE_TYPE;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params))) {
        return XSystemDeviceType_XboxScarlettAnaconda;
    }
    return (XSystemDeviceType)params.out_device_type;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XSystemGetConsoleId(
    uint32_t consoleIdSize,
    char *consoleId,
    uint32_t *consoleIdUsed) {
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_CONSOLE_ID;
    params.buf_addr = (uint64_t)(uintptr_t)consoleId;
    params.buf_cap = consoleIdSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params);
    if (consoleIdUsed != NULL) *consoleIdUsed = (uint32_t)params.out_used;
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XSystemGetXboxLiveSandboxId(
    uint32_t sandboxIdSize,
    char *sandboxId,
    uint32_t *sandboxIdUsed) {
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_SANDBOX_ID;
    params.buf_addr = (uint64_t)(uintptr_t)sandboxId;
    params.buf_cap = sandboxIdSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params);
    if (sandboxIdUsed != NULL) *sandboxIdUsed = (uint32_t)params.out_used;
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API XSystemAnalyticsInfo SCARLETT_XGR_CALL XSystemGetAnalyticsInfo(void) {
    XSystemAnalyticsInfo info;
    xgr_pe_zero(&info, sizeof(info));
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_ANALYTICS_INFO;
    params.buf_addr = (uint64_t)(uintptr_t)&info;
    params.buf_cap = (uint32_t)sizeof(info);
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params);
    return info;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XSystemGetAppSpecificDeviceId(
    uint32_t appSpecificDeviceIdSize,
    char *appSpecificDeviceId,
    uint32_t *appSpecificDeviceIdUsed) {
    scarlett_xgr_xsystem_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSYS_GET_DEVICE_ID;
    params.buf_addr = (uint64_t)(uintptr_t)appSpecificDeviceId;
    params.buf_cap = appSpecificDeviceIdSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSYSTEM_QUERY, &params);
    if (appSpecificDeviceIdUsed != NULL) *appSpecificDeviceIdUsed = (uint32_t)params.out_used;
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API uint32_t SCARLETT_XGR_CALL XThreadIsTimeSensitive(void) {
    scarlett_loader_thread_rt_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.is_time_sensitive = UINT32_C(0xFFFFFFFF);
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XTHREAD_TIME_SENSITIVE, &params))) {
        return 0u;
    }
    return params.out_previous_state;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XThreadSetTimeSensitive(
    uint32_t isTimeSensitive) {
    scarlett_loader_thread_rt_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.is_time_sensitive = (isTimeSensitive != 0u) ? 1u : 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XTHREAD_TIME_SENSITIVE, &params);
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

/* ============================================================================
 * 5. XUser, XPackage, XPersistentLocalStorage, XStore, XGameUI
 * ============================================================================ */

static const uint8_t g_id_xuser_add = 1u;
static const uint8_t g_id_xpkg_mount = 2u;
static const uint8_t g_id_xstore_lic = 3u;
static const uint8_t g_id_xstore_query = 4u;
static const uint8_t g_id_xstore_cons = 5u;
static const uint8_t g_id_ui_msg = 6u;
static const uint8_t g_id_ui_txt = 7u;
static const uint8_t g_id_gs_prov = 8u;
static const uint8_t g_id_gs_quota = 9u;
static const uint8_t g_id_gs_read = 10u;
static const uint8_t g_id_gs_files = 11u;
static const uint8_t g_id_xbl_ach = 12u;
static const uint8_t g_id_xbl_stat = 13u;
static const uint8_t g_id_xbl_soc = 14u;
static const uint8_t g_id_pf_login = 15u;
static const uint8_t g_id_pf_data = 16u;

typedef struct xgr_async_u64_box_s {
    uint64_t val;
    uint32_t u32;
    char text[512];
} xgr_async_u64_box_t;

static scarlett_hresult_t SCARLETT_XGR_CALL xgr_u64_box_provider(
    XAsyncOp op,
    const XAsyncProviderData *data) {
    if (op == XAsyncOp_GetResult && data->buffer != NULL && data->bufferSize >= sizeof(uint64_t)) {
        xgr_pe_copy(data->buffer, data->context, sizeof(uint64_t));
        return SCARLETT_OK;
    }
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserAddAsync(
    XUserAddOptions options,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_ADD;
    params.options_or_arg = (uint32_t)options;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    /* Store result in asyncBlock->internal[16..23] safely after XAsyncBegin */
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xuser_add, "XUserAddAsync", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.out_handle_or_u64, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserAddResult(
    XAsyncBlock *async,
    XUserHandle *newUser) {
    if (newUser == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xuser_add, sizeof(uint64_t), newUser, NULL);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserDuplicateHandle(
    XUserHandle handle,
    XUserHandle *duplicatedHandle) {
    if (duplicatedHandle == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_DUPLICATE;
    params.user_handle = handle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *duplicatedHandle = params.out_handle_or_u64;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XUserCloseHandle(XUserHandle user) {
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_CLOSE;
    params.user_handle = user;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetId(
    XUserHandle user,
    uint64_t *userId) {
    if (userId == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_GET_ID;
    params.user_handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *userId = params.out_handle_or_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetLocalId(
    XUserHandle user,
    XUserLocalId *userLocalId) {
    if (userLocalId == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_GET_LOCAL_ID;
    params.user_handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *userLocalId = params.out_handle_or_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetGamertag(
    XUserHandle user,
    XUserGamertagComponent gamertagComponent,
    uint32_t gamertagSize,
    char *gamertag,
    uint32_t *gamertagUsed) {
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_GET_GAMERTAG;
    params.user_handle = user;
    params.options_or_arg = (uint32_t)gamertagComponent;
    params.buf_addr = (uint64_t)(uintptr_t)gamertag;
    params.buf_cap = gamertagSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (gamertagUsed != NULL) *gamertagUsed = params.out_u32;
    if (scarlett_failed(rc)) return rc;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetAgeGroup(
    XUserHandle user,
    XUserAgeGroup *ageGroup) {
    if (ageGroup == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_GET_AGE_GROUP;
    params.user_handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *ageGroup = (XUserAgeGroup)params.out_u32;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserGetState(
    XUserHandle user,
    XUserState *state) {
    if (state == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_GET_STATE;
    params.user_handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *state = (XUserState)params.out_u32;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XUserCheckPrivilege(
    XUserHandle user,
    uint32_t options,
    XUserPrivilege privilege,
    bool *hasPrivilege,
    uint32_t *reason) {
    if (hasPrivilege == NULL) return SCARLETT_E_POINTER;
    (void)options;
    scarlett_xgr_xuser_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XUSER_CHECK_PRIVILEGE;
    params.user_handle = user;
    params.options_or_arg = (uint32_t)privilege;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *hasPrivilege = (params.out_u32 != 0u);
    if (reason != NULL) *reason = params.out_u32_b;
    return params.out_status;
}

/* XPackage */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageGetCurrentProcessPackageIdentifier(
    uint32_t bufferSize,
    char *buffer) {
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_GET_CUR_ID;
    params.str_addr = (uint64_t)(uintptr_t)buffer;
    params.buf_cap = bufferSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageEnumeratePackages(
    XPackageKind kind,
    uint32_t scope,
    void *context,
    XPackageEnumerationCallback callback) {
    (void)scope;
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_ENUM_PACKAGES;
    params.chunk_id = (uint32_t)kind;
    params.handle = (uint64_t)(uintptr_t)context;
    params.aux_addr = (uint64_t)(uintptr_t)callback;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageEnumerateChunks(
    const char *packageIdentifier,
    uint32_t selectorCount,
    const XPackageChunkSelector *selectors,
    void *context,
    XPackageChunkEnumerationCallback callback) {
    (void)selectorCount;
    (void)selectors;
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_ENUM_CHUNKS;
    params.str_addr = (uint64_t)(uintptr_t)packageIdentifier;
    params.handle = (uint64_t)(uintptr_t)context;
    params.aux_addr = (uint64_t)(uintptr_t)callback;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageMountWithUiAsync(
    const char *packageIdentifier,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_MOUNT;
    params.str_addr = (uint64_t)(uintptr_t)packageIdentifier;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xpkg_mount, "XPackageMount", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.handle, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageMountWithUiResult(
    XAsyncBlock *async,
    XPackageMountHandle *mount) {
    if (mount == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xpkg_mount, sizeof(uint64_t), mount, NULL);
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XPackageCloseMountHandle(XPackageMountHandle mount) {
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_CLOSE_MOUNT;
    params.handle = mount;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageGetMountPathSize(
    XPackageMountHandle mount,
    uint32_t *pathSize) {
    if (pathSize == NULL) return SCARLETT_E_POINTER;
    char tmp[8];
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_GET_MOUNT_PATH;
    params.handle = mount;
    params.str_addr = (uint64_t)(uintptr_t)tmp;
    params.buf_cap = 0u;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    *pathSize = params.out_count;
    return (params.out_count > 0u) ? SCARLETT_OK : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageGetMountPath(
    XPackageMountHandle mount,
    uint32_t pathSize,
    char *path) {
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_GET_MOUNT_PATH;
    params.handle = mount;
    params.str_addr = (uint64_t)(uintptr_t)path;
    params.buf_cap = pathSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPackageFindChunkAvailability(
    const char *packageIdentifier,
    uint32_t selectorCount,
    const XPackageChunkSelector *selectors,
    XPackageChunkAvailability *availability) {
    if (availability == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xpackage_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPKG_FIND_CHUNK_AVAIL;
    params.str_addr = (uint64_t)(uintptr_t)packageIdentifier;
    params.aux_addr = (uint64_t)(uintptr_t)selectors;
    params.buf_cap = selectorCount;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPACKAGE_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *availability = (XPackageChunkAvailability)params.out_avail;
    return params.out_status;
}

/* XPersistentLocalStorage */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStorageGetPathSize(
    uint32_t *pathSize) {
    if (pathSize == NULL) return SCARLETT_E_POINTER;
    char dummy[4];
    scarlett_xgr_xpls_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPLS_GET_PATH;
    params.buf_addr = (uint64_t)(uintptr_t)dummy;
    params.buf_cap = 0u;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPLS_OP, &params);
    *pathSize = (uint32_t)params.out_u64_a;
    return (*pathSize > 0u) ? SCARLETT_OK : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStorageGetPath(
    uint32_t pathSize,
    char *path) {
    scarlett_xgr_xpls_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPLS_GET_PATH;
    params.buf_addr = (uint64_t)(uintptr_t)path;
    params.buf_cap = pathSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPLS_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStorageGetSpaceInfo(
    uint64_t *availableBytes,
    uint64_t *totalBytes) {
    scarlett_xgr_xpls_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPLS_GET_SPACE_INFO;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPLS_OP, &params);
    if (scarlett_failed(rc)) return rc;
    if (availableBytes != NULL) *availableBytes = params.out_u64_a;
    if (totalBytes != NULL) *totalBytes = params.out_u64_b;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStoragePromptUserForSpaceAsync(
    uint64_t requestedBytes,
    XAsyncBlock *async) {
    scarlett_xgr_xpls_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XPLS_PROMPT_SPACE;
    params.req_bytes = requestedBytes;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XPLS_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    return xgr_run_immediate_async(async, &g_id_xuser_add, hr, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XPersistentLocalStoragePromptUserForSpaceResult(
    XAsyncBlock *async) {
    return XAsyncGetStatus(async, true);
}

/* XStore */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreCreateContext(
    XUserHandle user,
    XStoreContextHandle *storeContextHandle) {
    if (storeContextHandle == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_CREATE_CTX;
    params.handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *storeContextHandle = params.handle;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XStoreCloseContextHandle(XStoreContextHandle storeContextHandle) {
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_CLOSE_CTX;
    params.handle = storeContextHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForPackageAsync(
    XStoreContextHandle storeContextHandle,
    const char *packageIdentifier,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_ACQUIRE_PKG_LIC;
    params.handle = storeContextHandle;
    params.str_addr = (uint64_t)(uintptr_t)packageIdentifier;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xstore_lic, "XStoreAcquireLicensePkg", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.aux_handle, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForPackageResult(
    XAsyncBlock *async,
    XStoreLicenseHandle *storeLicenseHandle) {
    if (storeLicenseHandle == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xstore_lic, sizeof(uint64_t), storeLicenseHandle, NULL);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForDurablesAsync(
    XStoreContextHandle storeContextHandle,
    const char *storeId,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_ACQUIRE_DUR_LIC;
    params.handle = storeContextHandle;
    params.str_addr = (uint64_t)(uintptr_t)storeId;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xstore_lic, "XStoreAcquireLicenseDur", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.aux_handle, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreAcquireLicenseForDurablesResult(
    XAsyncBlock *async,
    XStoreLicenseHandle *storeLicenseHandle) {
    if (storeLicenseHandle == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xstore_lic, sizeof(uint64_t), storeLicenseHandle, NULL);
}

SCARLETT_XGR_API bool SCARLETT_XGR_CALL XStoreIsLicenseValid(XStoreLicenseHandle storeLicenseHandle) {
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_IS_LIC_VALID;
    params.handle = storeLicenseHandle;
    if (scarlett_failed(xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params))) return false;
    return params.out_bool != 0u;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XStoreCloseLicenseHandle(XStoreLicenseHandle storeLicenseHandle) {
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_CLOSE_LIC;
    params.handle = storeLicenseHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryAssociatedProductsAsync(
    XStoreContextHandle storeContextHandle,
    uint32_t productKinds,
    uint32_t maxItemsToRetrievePerPage,
    XAsyncBlock *async) {
    (void)productKinds;
    (void)maxItemsToRetrievePerPage;
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_QUERY_ASSOC;
    params.handle = storeContextHandle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xstore_query, "XStoreQueryAssoc", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.aux_handle, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryAssociatedProductsResult(
    XAsyncBlock *async,
    XStoreProductQueryHandle *productQueryHandle) {
    if (productQueryHandle == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xstore_query, sizeof(uint64_t), productQueryHandle, NULL);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryProductForCurrentGameAsync(
    XStoreContextHandle storeContextHandle,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_QUERY_CUR_GAME;
    params.handle = storeContextHandle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xstore_query, "XStoreQueryCurGame", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.aux_handle, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryProductForCurrentGameResult(
    XAsyncBlock *async,
    XStoreProductQueryHandle *productQueryHandle) {
    if (productQueryHandle == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xstore_query, sizeof(uint64_t), productQueryHandle, NULL);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreEnumerateProductsQuery(
    XStoreProductQueryHandle productQueryHandle,
    void *context,
    XStoreProductQueryCallback callback) {
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_ENUM_PRODUCTS;
    params.handle = productQueryHandle;
    params.cb_addr = (uint64_t)(uintptr_t)callback;
    params.ctx_addr = (uint64_t)(uintptr_t)context;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XStoreCloseProductsQueryHandle(
    XStoreProductQueryHandle productQueryHandle) {
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_CLOSE_QUERY;
    params.handle = productQueryHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryConsumableBalanceRemainingAsync(
    XStoreContextHandle storeContextHandle,
    const char *storeProductId,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xstore_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSTORE_QUERY_CONSUMABLE;
    params.handle = storeContextHandle;
    params.str_addr = (uint64_t)(uintptr_t)storeProductId;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSTORE_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xstore_cons, "XStoreQueryConsumable", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t qty = (uint64_t)params.out_u32;
    xgr_pe_copy(&async->internal[16], &qty, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XStoreQueryConsumableBalanceRemainingResult(
    XAsyncBlock *async,
    uint32_t *quantity) {
    if (quantity == NULL) return SCARLETT_E_POINTER;
    uint64_t qty = 0u;
    scarlett_hresult_t hr = XAsyncGetResult(async, &g_id_xstore_cons, sizeof(uint64_t), &qty, NULL);
    *quantity = (uint32_t)qty;
    return hr;
}

/* XGameUI */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowMessageDialogAsync(
    XAsyncBlock *async,
    const char *titleText,
    const char *contentText,
    const char *firstButtonText,
    const char *secondButtonText,
    const char *thirdButtonText,
    XGameUiMessageDialogButton defaultButton,
    XGameUiMessageDialogButton cancelButton) {
    (void)firstButtonText;
    (void)secondButtonText;
    (void)thirdButtonText;
    (void)cancelButton;
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xgameui_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_UI_MSG_DIALOG;
    params.arg_u32 = (uint32_t)defaultButton;
    params.str_a_addr = (uint64_t)(uintptr_t)titleText;
    params.str_b_addr = (uint64_t)(uintptr_t)contentText;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XGAMEUI_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_ui_msg, "XGameUiMsgDialog", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t btn = (uint64_t)params.out_u32;
    xgr_pe_copy(&async->internal[16], &btn, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowMessageDialogResult(
    XAsyncBlock *async,
    XGameUiMessageDialogButton *resultButton) {
    if (resultButton == NULL) return SCARLETT_E_POINTER;
    uint64_t btn = 0u;
    scarlett_hresult_t hr = XAsyncGetResult(async, &g_id_ui_msg, sizeof(uint64_t), &btn, NULL);
    *resultButton = (XGameUiMessageDialogButton)btn;
    return hr;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowTextEntryAsync(
    XAsyncBlock *async,
    const char *titleText,
    const char *descriptionText,
    const char *defaultText,
    uint32_t inputScope,
    uint32_t maxTextLength) {
    (void)titleText;
    (void)descriptionText;
    (void)inputScope;
    (void)maxTextLength;
    if (async == NULL) return SCARLETT_E_POINTER;
    uint64_t txt_ptr = (uint64_t)(uintptr_t)(defaultText != NULL ? defaultText : "ScarlettInput");
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_ui_txt, "XGameUiTextEntry", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &txt_ptr, sizeof(uint64_t));
    XAsyncComplete(async, SCARLETT_OK, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowTextEntryResultSize(
    XAsyncBlock *async,
    uint32_t *resultTextBufferSize) {
    if (resultTextBufferSize == NULL) return SCARLETT_E_POINTER;
    uint64_t txt_ptr = 0u;
    scarlett_hresult_t hr = XAsyncGetResult(async, &g_id_ui_txt, sizeof(uint64_t), &txt_ptr, NULL);
    if (scarlett_failed(hr)) return hr;
    const char *s = (const char *)(uintptr_t)txt_ptr;
    *resultTextBufferSize = (uint32_t)(xgr_pe_cstrlen(s) + 1u);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowTextEntryResult(
    XAsyncBlock *async,
    uint32_t resultTextBufferSize,
    char *resultTextBuffer,
    uint32_t *resultTextBufferUsed) {
    if (resultTextBuffer == NULL) return SCARLETT_E_POINTER;
    uint64_t txt_ptr = 0u;
    scarlett_hresult_t hr = XAsyncGetResult(async, &g_id_ui_txt, sizeof(uint64_t), &txt_ptr, NULL);
    if (scarlett_failed(hr)) return hr;
    const char *s = (const char *)(uintptr_t)txt_ptr;
    uint32_t need = (uint32_t)(xgr_pe_cstrlen(s) + 1u);
    if (resultTextBufferUsed != NULL) *resultTextBufferUsed = need;
    if (resultTextBufferSize < need) return SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL;
    xgr_pe_copy_cstr(resultTextBuffer, resultTextBufferSize, s);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowErrorDialogAsync(
    XAsyncBlock *async,
    const char *context,
    scarlett_hresult_t errorCode) {
    scarlett_xgr_xgameui_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_UI_ERROR_DIALOG;
    params.str_a_addr = (uint64_t)(uintptr_t)context;
    params.arg_u32 = (uint32_t)errorCode;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XGAMEUI_OP, &params);
    return xgr_run_immediate_async(async, &g_id_ui_msg, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowErrorDialogResult(XAsyncBlock *async) {
    return XAsyncGetStatus(async, true);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowPlayerProfileCardAsync(
    XAsyncBlock *async,
    XUserHandle requestor,
    uint64_t targetUser) {
    (void)requestor;
    (void)targetUser;
    scarlett_xgr_xgameui_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_UI_PROFILE_CARD;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XGAMEUI_OP, &params);
    return xgr_run_immediate_async(async, &g_id_ui_msg, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiShowPlayerProfileCardResult(XAsyncBlock *async) {
    return XAsyncGetStatus(async, true);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameUiSetNotificationPositionHint(
    XGameUiNotificationPositionHint position) {
    scarlett_xgr_xgameui_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_UI_NOTIF_HINT;
    params.arg_u32 = (uint32_t)position;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XGAMEUI_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

/* ============================================================================
 * 6. XGameSave & XGameSaveFiles
 * ============================================================================ */

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveInitializeProvider(
    XUserHandle requestingUser,
    const char *configurationId,
    bool syncOnDemand,
    XGameSaveProviderHandle *provider) {
    if (provider == NULL) return SCARLETT_E_POINTER;
    *provider = 0u;
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_INIT_PROVIDER;
    params.user_handle = requestingUser;
    params.str_addr = (uint64_t)(uintptr_t)configurationId;
    params.flags_or_bool = syncOnDemand ? 1u : 0u;
    params.reserved = 0u; /* sync call */
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *provider = params.provider_handle;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveInitializeProviderAsync(
    XUserHandle requestingUser,
    const char *configurationId,
    bool syncOnDemand,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_INIT_PROVIDER;
    params.user_handle = requestingUser;
    params.str_addr = (uint64_t)(uintptr_t)configurationId;
    params.flags_or_bool = syncOnDemand ? 1u : 0u;
    params.reserved = 1u; /* async call allowed on time-sensitive thread */
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_gs_prov, "XGameSaveInitProv", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.provider_handle, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveInitializeProviderResult(
    XAsyncBlock *async,
    XGameSaveProviderHandle *provider) {
    if (provider == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_gs_prov, sizeof(uint64_t), provider, NULL);
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveCloseProvider(XGameSaveProviderHandle provider) {
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_CLOSE_PROVIDER;
    params.provider_handle = provider;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveGetRemainingQuota(
    XGameSaveProviderHandle provider,
    int64_t *remainingQuota) {
    if (remainingQuota == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_GET_QUOTA;
    params.provider_handle = provider;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *remainingQuota = (int64_t)params.quota_val;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveGetRemainingQuotaAsync(
    XGameSaveProviderHandle provider,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_GET_QUOTA;
    params.provider_handle = provider;
    params.reserved = 1u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_gs_quota, "XGameSaveQuota", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.quota_val, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveGetRemainingQuotaResult(
    XAsyncBlock *async,
    int64_t *remainingQuota) {
    if (remainingQuota == NULL) return SCARLETT_E_POINTER;
    uint64_t v = 0u;
    scarlett_hresult_t hr = XAsyncGetResult(async, &g_id_gs_quota, sizeof(uint64_t), &v, NULL);
    *remainingQuota = (int64_t)v;
    return hr;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveDeleteContainer(
    XGameSaveProviderHandle provider,
    const char *containerName) {
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_DELETE_CONTAINER;
    params.provider_handle = provider;
    params.str_addr = (uint64_t)(uintptr_t)containerName;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveDeleteContainerAsync(
    XGameSaveProviderHandle provider,
    const char *containerName,
    XAsyncBlock *async) {
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_DELETE_CONTAINER;
    params.provider_handle = provider;
    params.str_addr = (uint64_t)(uintptr_t)containerName;
    params.reserved = 1u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    return xgr_run_immediate_async(async, &g_id_gs_prov, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveDeleteContainerResult(XAsyncBlock *async) {
    return XAsyncGetStatus(async, true);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveCreateContainer(
    XGameSaveProviderHandle provider,
    const char *containerName,
    XGameSaveContainerHandle *containerContext) {
    if (containerContext == NULL) return SCARLETT_E_POINTER;
    *containerContext = 0u;
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_CREATE_CONTAINER;
    params.provider_or_container_handle = provider;
    params.name_addr = (uint64_t)(uintptr_t)containerName;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *containerContext = params.provider_or_container_handle;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveCloseContainer(XGameSaveContainerHandle context) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_CLOSE_CONTAINER;
    params.provider_or_container_handle = context;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateContainers(
    XGameSaveProviderHandle provider,
    void *context,
    XGameSaveContainerInfoCallback callback) {
    return XGameSaveEnumerateContainersByName(provider, NULL, context, callback);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateContainersByName(
    XGameSaveProviderHandle provider,
    const char *containerNamePrefix,
    void *context,
    XGameSaveContainerInfoCallback callback) {
    scarlett_xgr_gs_provider_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_ENUM_CONTAINERS;
    params.provider_handle = provider;
    params.str_addr = (uint64_t)(uintptr_t)containerNamePrefix;
    params.ctx_addr = (uint64_t)(uintptr_t)context;
    params.cb_addr = (uint64_t)(uintptr_t)callback;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateBlobInfo(
    XGameSaveContainerHandle container,
    void *context,
    XGameSaveBlobInfoCallback callback) {
    return XGameSaveEnumerateBlobInfoByName(container, NULL, context, callback);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveEnumerateBlobInfoByName(
    XGameSaveContainerHandle container,
    const char *blobNamePrefix,
    void *context,
    XGameSaveBlobInfoCallback callback) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_ENUM_BLOBS;
    params.provider_or_container_handle = container;
    params.name_addr = (uint64_t)(uintptr_t)blobNamePrefix;
    params.data_addr = (uint64_t)(uintptr_t)context;
    params.aux_addr = (uint64_t)(uintptr_t)callback;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveReadBlobData(
    XGameSaveContainerHandle container,
    const char **blobNames,
    uint32_t *countOfBlobs,
    uint32_t blobsSize,
    XGameSaveBlob *blobData) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_READ_BLOB;
    params.provider_or_container_handle = container;
    params.name_addr = (uint64_t)(uintptr_t)blobNames;
    params.aux_addr = (uint64_t)(uintptr_t)countOfBlobs;
    params.data_size = (uint64_t)blobsSize;
    params.data_addr = (uint64_t)(uintptr_t)blobData;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

typedef struct xgr_gs_read_req_s {
    XGameSaveContainerHandle container;
    const char **blob_names;
    uint32_t count;
} xgr_gs_read_req_t;

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveReadBlobDataAsync(
    XGameSaveContainerHandle container,
    const char **blobNames,
    uint32_t countOfBlobs,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    uint64_t packed0 = container;
    uint64_t packed1 = (uint64_t)(uintptr_t)blobNames;
    uint32_t packed2 = countOfBlobs;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_gs_read, "XGameSaveReadBlob", xgr_noop_async_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &packed0, 8u);
    xgr_pe_copy(&async->internal[24], &packed1, 8u);
    /* Store count inside async->context only if not overwriting user context:
     * instead complete with count as requiredBufferSize! */
    (void)packed2;
    XAsyncComplete(async, SCARLETT_OK, (uint64_t)countOfBlobs);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveReadBlobDataResult(
    XAsyncBlock *async,
    uint32_t blobsSize,
    XGameSaveBlob *blobData,
    uint32_t *countOfBlobs) {
    if (async == NULL || countOfBlobs == NULL) return SCARLETT_E_POINTER;
    uint64_t container = 0u;
    uint64_t blob_names_addr = 0u;
    uint64_t cnt = 0u;
    xgr_pe_copy(&container, &async->internal[16], 8u);
    xgr_pe_copy(&blob_names_addr, &async->internal[24], 8u);
    scarlett_hresult_t hr = XAsyncGetResultSize(async, &cnt);
    if (scarlett_failed(hr)) return hr;
    *countOfBlobs = (uint32_t)cnt;

    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_READ_BLOB;
    params.provider_or_container_handle = container;
    params.name_addr = blob_names_addr;
    params.aux_addr = (uint64_t)(uintptr_t)countOfBlobs;
    params.data_size = (uint64_t)blobsSize;
    params.data_addr = (uint64_t)(uintptr_t)blobData;
    params.reserved = 1u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveCreateUpdate(
    XGameSaveContainerHandle container,
    const char *containerDisplayName,
    XGameSaveUpdateHandle *updateContext) {
    if (updateContext == NULL) return SCARLETT_E_POINTER;
    *updateContext = 0u;
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_CREATE_UPDATE;
    params.provider_or_container_handle = container;
    params.name_addr = (uint64_t)(uintptr_t)containerDisplayName;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *updateContext = params.update_handle;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitBlobWrite(
    XGameSaveUpdateHandle updateContext,
    const char *blobName,
    const uint8_t *data,
    uint64_t size) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_UPDATE_WRITE_BLOB;
    params.update_handle = updateContext;
    params.name_addr = (uint64_t)(uintptr_t)blobName;
    params.data_addr = (uint64_t)(uintptr_t)data;
    params.data_size = size;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitBlobDelete(
    XGameSaveUpdateHandle updateContext,
    const char *blobName) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_UPDATE_DELETE_BLOB;
    params.update_handle = updateContext;
    params.name_addr = (uint64_t)(uintptr_t)blobName;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitUpdate(
    XGameSaveUpdateHandle updateContext) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_SUBMIT_UPDATE;
    params.update_handle = updateContext;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitUpdateAsync(
    XGameSaveUpdateHandle updateContext,
    XAsyncBlock *async) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_SUBMIT_UPDATE;
    params.update_handle = updateContext;
    params.reserved = 1u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
    return xgr_run_immediate_async(async, &g_id_gs_prov, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveSubmitUpdateResult(XAsyncBlock *async) {
    return XAsyncGetStatus(async, true);
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveCloseUpdate(XGameSaveUpdateHandle updateContext) {
    scarlett_xgr_gs_container_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GS_CLOSE_UPDATE;
    params.update_handle = updateContext;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP, &params);
}

/* XGameSaveFiles */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveFilesGetFolderWithUiAsync(
    XUserHandle requestingUser,
    const char *configurationId,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_gs_files, "XGameSaveFilesFolder", xgr_noop_async_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t u = requestingUser;
    uint64_t s = (uint64_t)(uintptr_t)configurationId;
    xgr_pe_copy(&async->internal[16], &u, 8u);
    xgr_pe_copy(&async->internal[24], &s, 8u);
    XAsyncComplete(async, SCARLETT_OK, 0u);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveFilesGetFolderWithUiResult(
    XAsyncBlock *async,
    uint32_t folderSize,
    char *folder) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_hresult_t hr = XAsyncGetStatus(async, true);
    if (scarlett_failed(hr)) return hr;
    uint64_t user_h = 0u;
    uint64_t scid_addr = 0u;
    xgr_pe_copy(&user_h, &async->internal[16], 8u);
    xgr_pe_copy(&scid_addr, &async->internal[24], 8u);

    scarlett_xgr_gs_files_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GSF_GET_FOLDER;
    params.user_handle = user_h;
    params.scid_addr = scid_addr;
    params.out_path_addr = (uint64_t)(uintptr_t)folder;
    params.buf_cap = folderSize;
    params.reserved = 1u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_FILES_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XGameSaveFilesGetRemainingQuota(
    XUserHandle requestingUser,
    const char *configurationId,
    int64_t *remainingQuota) {
    if (remainingQuota == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_gs_files_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GSF_GET_QUOTA;
    params.user_handle = requestingUser;
    params.scid_addr = (uint64_t)(uintptr_t)configurationId;
    params.reserved = 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_FILES_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *remainingQuota = (int64_t)params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XGameSaveFilesCloseProvider(
    XUserHandle requestingUser,
    const char *configurationId) {
    scarlett_xgr_gs_files_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_GSF_CLOSE_PROVIDER;
    params.user_handle = requestingUser;
    params.scid_addr = (uint64_t)(uintptr_t)configurationId;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_FILES_OP, &params);
}

/* ============================================================================
 * 7. XSAPI (Xbl*) & PlayFab (PF*)
 * ============================================================================ */

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblInitialize(const XblInitArgs *args) {
    if (args == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_INIT;
    params.handle = args->queue;
    params.str_a_addr = (uint64_t)(uintptr_t)args->scid;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblCleanupAsync(XAsyncBlock *async) {
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_CLEANUP;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    return xgr_run_immediate_async(async, &g_id_xbl_ach, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblContextCreateHandle(
    XUserHandle user,
    XblContextHandle *context) {
    if (context == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_CTX_CREATE;
    params.handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *context = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XblContextCloseHandle(XblContextHandle xboxLiveContextHandle) {
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_CTX_CLOSE;
    params.handle = xboxLiveContextHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblContextDuplicateHandle(
    XblContextHandle xboxLiveContextHandle,
    XblContextHandle *duplicatedHandle) {
    if (duplicatedHandle == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_CTX_DUP;
    params.handle = xboxLiveContextHandle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *duplicatedHandle = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblContextGetUser(
    XblContextHandle context,
    XUserHandle *user) {
    if (user == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_CTX_GET_USER;
    params.handle = context;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *user = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsGetAchievementsForTitleIdAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    uint32_t titleId,
    uint32_t type,
    bool unlockedOnly,
    uint32_t orderBy,
    uint32_t skipItems,
    uint32_t maxItems,
    XAsyncBlock *async) {
    (void)xboxUserId;
    (void)titleId;
    (void)type;
    (void)orderBy;
    (void)skipItems;
    (void)maxItems;
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_ACH_GET;
    params.handle = xboxLiveContext;
    params.u32_a = unlockedOnly ? 1u : 0u;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xbl_ach, "XblAchGet", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.out_u64, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsGetAchievementsForTitleIdResult(
    XAsyncBlock *async,
    XblAchievementsResultHandle *result) {
    if (result == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_xbl_ach, sizeof(uint64_t), result, NULL);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsResultGetAchievements(
    XblAchievementsResultHandle resultHandle,
    const XblAchievement **achievements,
    uint32_t *achievementsCount) {
    if (achievements == NULL || achievementsCount == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_ACH_READ;
    params.handle = resultHandle;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *achievements = (const XblAchievement *)(uintptr_t)params.out_u64;
    *achievementsCount = params.out_u32;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL XblAchievementsResultCloseHandle(
    XblAchievementsResultHandle resultHandle) {
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_ACH_CLOSE;
    params.handle = resultHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblAchievementsUpdateAchievementAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    const char *achievementId,
    uint32_t percentComplete,
    XAsyncBlock *async) {
    (void)xboxUserId;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_ACH_UPDATE;
    params.handle = xboxLiveContext;
    params.str_a_addr = (uint64_t)(uintptr_t)achievementId;
    params.u32_a = percentComplete;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    return xgr_run_immediate_async(async, &g_id_xbl_ach, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblUserStatisticsGetSingleUserStatisticAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    const char *serviceConfigurationId,
    const char *statisticName,
    XAsyncBlock *async) {
    (void)xboxLiveContext;
    (void)xboxUserId;
    (void)serviceConfigurationId;
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xbl_stat, "XblStatGet", xgr_noop_async_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t sname = (uint64_t)(uintptr_t)statisticName;
    xgr_pe_copy(&async->internal[16], &sname, sizeof(uint64_t));
    XAsyncComplete(async, SCARLETT_OK, 0u);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblUserStatisticsGetSingleUserStatisticResult(
    XAsyncBlock *async,
    uint32_t bufferSize,
    char *buffer,
    uint32_t *bufferUsed) {
    if (async == NULL) return SCARLETT_E_POINTER;
    uint64_t sname = 0u;
    xgr_pe_copy(&sname, &async->internal[16], sizeof(uint64_t));
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_STATS_GET;
    params.str_b_addr = sname;
    params.out_buf_addr = (uint64_t)(uintptr_t)buffer;
    params.u32_a = bufferSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    if (bufferUsed != NULL) *bufferUsed = params.out_u32;
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblSocialGetSocialRelationshipsAsync(
    XblContextHandle xboxLiveContext,
    uint64_t xboxUserId,
    uint32_t filter,
    uint32_t startIndex,
    uint32_t maxItems,
    XAsyncBlock *async) {
    (void)xboxUserId;
    (void)filter;
    (void)startIndex;
    (void)maxItems;
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_xsapi_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_XSAPI_SOCIAL_GET;
    params.handle = xboxLiveContext;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_XSAPI_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_xbl_soc, "XblSocialGet", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t cnt = params.out_u32;
    xgr_pe_copy(&async->internal[16], &cnt, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblSocialGetSocialRelationshipsResult(
    XAsyncBlock *async,
    uint32_t *outRelationshipCount) {
    if (outRelationshipCount == NULL) return SCARLETT_E_POINTER;
    uint64_t cnt = 0u;
    scarlett_hresult_t hr = XAsyncGetResult(async, &g_id_xbl_soc, sizeof(uint64_t), &cnt, NULL);
    *outRelationshipCount = (uint32_t)cnt;
    return hr;
}

static const scarlett_stub_descriptor_t g_stub_xbl_matchmaking = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "xgameruntime.dll",
    "XblMatchmakingCreateMatchTicketAsync"
};

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL XblMatchmakingCreateMatchTicketAsync(
    XblContextHandle xboxLiveContext,
    const char *hopperName,
    const char *attributesJson,
    uint32_t ticketTimeoutSeconds,
    XAsyncBlock *async) {
    (void)async;
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return xgr_pe_dispatch_stub(
        &g_stub_xbl_matchmaking,
        ret,
        xboxLiveContext,
        (uint64_t)(uintptr_t)hopperName,
        (uint64_t)(uintptr_t)attributesJson,
        (uint64_t)ticketTimeoutSeconds);
}

/* PlayFab */
SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFInitialize(XTaskQueueHandle backgroundQueue) {
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_INIT;
    params.handle = backgroundQueue;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFUninitializeAsync(XAsyncBlock *async) {
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_UNINIT;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    return xgr_run_immediate_async(async, &g_id_pf_login, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFServiceConfigCreateHandle(
    const char *connectionString,
    const char *titleId,
    PFServiceConfigHandle *serviceConfigHandle) {
    if (serviceConfigHandle == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_CFG_CREATE;
    params.str_a_addr = (uint64_t)(uintptr_t)connectionString;
    params.str_b_addr = (uint64_t)(uintptr_t)titleId;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    if (scarlett_failed(rc)) return rc;
    *serviceConfigHandle = params.out_u64;
    return params.out_status;
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL PFServiceConfigCloseHandle(PFServiceConfigHandle serviceConfigHandle) {
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_CFG_CLOSE;
    params.handle = serviceConfigHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFAuthenticationLoginWithXboxAsync(
    PFServiceConfigHandle serviceConfigHandle,
    XUserHandle user,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_LOGIN_XBOX;
    params.handle = serviceConfigHandle;
    params.aux_handle = user;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_pf_login, "PFLoginXbox", xgr_u64_box_provider);
    if (scarlett_failed(bhr)) return bhr;
    xgr_pe_copy(&async->internal[16], &params.out_u64, sizeof(uint64_t));
    XAsyncComplete(async, hr, sizeof(uint64_t));
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFAuthenticationLoginWithXboxResult(
    XAsyncBlock *async,
    PFEntityHandle *entityHandle) {
    if (entityHandle == NULL) return SCARLETT_E_POINTER;
    return XAsyncGetResult(async, &g_id_pf_login, sizeof(uint64_t), entityHandle, NULL);
}

SCARLETT_XGR_API void SCARLETT_XGR_CALL PFEntityCloseHandle(PFEntityHandle entityHandle) {
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_ENTITY_CLOSE;
    params.handle = entityHandle;
    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFTitleDataGetTitleDataAsync(
    PFEntityHandle entityHandle,
    const char *key,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_pf_data, "PFTitleDataGet", xgr_noop_async_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t ent = entityHandle;
    uint64_t k = (uint64_t)(uintptr_t)key;
    xgr_pe_copy(&async->internal[16], &ent, 8u);
    xgr_pe_copy(&async->internal[24], &k, 8u);
    XAsyncComplete(async, SCARLETT_OK, 0u);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFTitleDataGetTitleDataResult(
    XAsyncBlock *async,
    uint32_t bufferSize,
    char *buffer,
    uint32_t *bufferUsed) {
    if (async == NULL) return SCARLETT_E_POINTER;
    uint64_t ent = 0u;
    uint64_t k = 0u;
    xgr_pe_copy(&ent, &async->internal[16], 8u);
    xgr_pe_copy(&k, &async->internal[24], 8u);
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_TITLE_DATA_GET;
    params.handle = ent;
    params.str_a_addr = k;
    params.out_buf_addr = (uint64_t)(uintptr_t)buffer;
    params.buf_cap = bufferSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    if (bufferUsed != NULL) *bufferUsed = params.out_used;
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataGetUserDataAsync(
    PFEntityHandle entityHandle,
    const char *key,
    XAsyncBlock *async) {
    if (async == NULL) return SCARLETT_E_POINTER;
    scarlett_hresult_t bhr = XAsyncBegin(async, &async->internal[16], &g_id_pf_data, "PFPlayerDataGet", xgr_noop_async_provider);
    if (scarlett_failed(bhr)) return bhr;
    uint64_t ent = entityHandle;
    uint64_t k = (uint64_t)(uintptr_t)key;
    xgr_pe_copy(&async->internal[16], &ent, 8u);
    xgr_pe_copy(&async->internal[24], &k, 8u);
    XAsyncComplete(async, SCARLETT_OK, 0u);
    return SCARLETT_OK;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataGetUserDataResult(
    XAsyncBlock *async,
    uint32_t bufferSize,
    char *buffer,
    uint32_t *bufferUsed) {
    if (async == NULL) return SCARLETT_E_POINTER;
    uint64_t ent = 0u;
    uint64_t k = 0u;
    xgr_pe_copy(&ent, &async->internal[16], 8u);
    xgr_pe_copy(&k, &async->internal[24], 8u);
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_PLAYER_DATA_GET;
    params.handle = ent;
    params.str_a_addr = k;
    params.out_buf_addr = (uint64_t)(uintptr_t)buffer;
    params.buf_cap = bufferSize;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    if (bufferUsed != NULL) *bufferUsed = params.out_used;
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataUpdateUserDataAsync(
    PFEntityHandle entityHandle,
    const char *key,
    const char *valueJson,
    XAsyncBlock *async) {
    scarlett_xgr_playfab_params_t params;
    xgr_pe_zero(&params, sizeof(params));
    params.subop = SCARLETT_XGR_PF_PLAYER_DATA_SET;
    params.handle = entityHandle;
    params.str_a_addr = (uint64_t)(uintptr_t)key;
    params.str_b_addr = (uint64_t)(uintptr_t)valueJson;
    int32_t rc = xgr_pe_unix_call(SCARLETT_XGR_UNIX_PLAYFAB_OP, &params);
    return xgr_run_immediate_async(async, &g_id_pf_data, scarlett_failed(rc) ? rc : params.out_status, 0u, xgr_noop_async_provider);
}

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFPlayerDataUpdateUserDataResult(XAsyncBlock *async) {
    return XAsyncGetStatus(async, true);
}

static const scarlett_stub_descriptor_t g_stub_pf_matchmaking = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "xgameruntime.dll",
    "PFMatchmakingCreateMatchmakingTicketAsync"
};

SCARLETT_XGR_API scarlett_hresult_t SCARLETT_XGR_CALL PFMatchmakingCreateMatchmakingTicketAsync(
    PFEntityHandle entityHandle,
    const char *queueName,
    XAsyncBlock *async) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return xgr_pe_dispatch_stub(
        &g_stub_pf_matchmaking,
        ret,
        entityHandle,
        (uint64_t)(uintptr_t)queueName,
        (uint64_t)(uintptr_t)async,
        0u);
}

#if defined(_WIN32)
typedef struct scarlett_win_exception_record_s {
    uint32_t ExceptionCode;
    uint32_t ExceptionFlags;
    struct scarlett_win_exception_record_s *ExceptionRecord;
    void *ExceptionAddress;
    uint32_t NumberParameters;
    uintptr_t ExceptionInformation[15];
} scarlett_win_exception_record_t;

typedef struct scarlett_win_context_s {
    uint64_t P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    uint32_t ContextFlags, MxCsr;
    uint16_t SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    uint32_t EFlags;
    uint64_t Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    uint64_t Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi;
    uint64_t R8, R9, R10, R11, R12, R13, R14, R15;
    uint64_t Rip;
} scarlett_win_context_t;

typedef struct scarlett_win_exception_pointers_s {
    scarlett_win_exception_record_t *ExceptionRecord;
    scarlett_win_context_t *ContextRecord;
} scarlett_win_exception_pointers_t;

typedef int32_t (__stdcall *scarlett_pfn_veh_t)(scarlett_win_exception_pointers_t *ep);
typedef int32_t (__stdcall *scarlett_pfn_virtual_protect_t)(
    void *lpAddress, size_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect);
typedef void * (__stdcall *scarlett_pfn_virtual_alloc_t)(
    void *lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
__declspec(dllimport) void * __stdcall AddVectoredExceptionHandler(uint32_t First, scarlett_pfn_veh_t Handler);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char *lpOutputString);

typedef struct scarlett_veh_list_entry_s {
    struct scarlett_veh_list_entry_s *Flink;
    struct scarlett_veh_list_entry_s *Blink;
} scarlett_veh_list_entry_t;

typedef struct scarlett_veh_unicode_string_s {
    uint16_t Length;
    uint16_t MaximumLength;
    uint32_t Reserved;
    const uint16_t *Buffer;
} scarlett_veh_unicode_string_t;

static scarlett_pfn_virtual_protect_t g_veh_virtual_protect = NULL;
static scarlett_pfn_virtual_alloc_t g_veh_virtual_alloc = NULL;
static uint32_t g_veh_vp_resolved = 0u;
static volatile uint32_t g_veh_patch_lock = 0u;
static uint8_t *g_clzero_tramp_page = NULL;
static size_t g_clzero_tramp_used = 0u;
static uint8_t *g_clzero_tramp_stubs[16] = { NULL };

static bool xgr_veh_ascii_ieq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

static bool xgr_veh_utf16_ascii_ieq(const uint16_t *w, uint16_t byte_len, const char *ascii) {
    uint32_t wchar_len = (uint32_t)(byte_len / 2u);
    uint32_t i = 0u;
    while (i < wchar_len && ascii[i] != '\0') {
        uint16_t wc = w[i];
        char cb = ascii[i];
        if (wc >= 'A' && wc <= 'Z') {
            wc = (uint16_t)(wc + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (wc != (uint16_t)(uint8_t)cb) {
            return false;
        }
        ++i;
    }
    return i == wchar_len && ascii[i] == '\0';
}

static void *xgr_veh_find_export(const uint8_t *base, const char *proc_name) {
    if (base == NULL || proc_name == NULL) {
        return NULL;
    }
    if (base[0] != 'M' || base[1] != 'Z') {
        return NULL;
    }
    uint32_t pe_off = *(const uint32_t *)(base + 0x3Cu);
    const uint8_t *nt = base + pe_off;
    if (nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0 || nt[3] != 0) {
        return NULL;
    }
    if (*(const uint16_t *)(nt + 0x18u) != 0x020Bu) {
        return NULL;
    }
    uint32_t exp_rva = *(const uint32_t *)(nt + 0x88u);
    uint32_t exp_size = *(const uint32_t *)(nt + 0x8Cu);
    if (exp_rva == 0u || exp_size == 0u) {
        return NULL;
    }

    const uint8_t *exp_dir = base + exp_rva;
    uint32_t num_names = *(const uint32_t *)(exp_dir + 0x18u);
    const uint32_t *funcs = (const uint32_t *)(base + *(const uint32_t *)(exp_dir + 0x1Cu));
    const uint32_t *names = (const uint32_t *)(base + *(const uint32_t *)(exp_dir + 0x20u));
    const uint16_t *ords = (const uint16_t *)(base + *(const uint32_t *)(exp_dir + 0x24u));

    for (uint32_t i = 0u; i < num_names; ++i) {
        const char *name = (const char *)(base + names[i]);
        if (xgr_veh_ascii_ieq(name, proc_name)) {
            uint32_t fn_rva = funcs[ords[i]];
            if (fn_rva >= exp_rva && fn_rva < exp_rva + exp_size) {
                return NULL; /* Forwarder */
            }
            return (void *)(uintptr_t)(base + fn_rva);
        }
    }
    return NULL;
}

static scarlett_pfn_virtual_protect_t xgr_veh_get_virtual_protect(void) {
    if (__atomic_load_n(&g_veh_vp_resolved, __ATOMIC_ACQUIRE) != 0u) {
        return g_veh_virtual_protect;
    }
    const uint8_t *peb = NULL;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (peb != NULL) {
        const uint8_t *ldr = *(const uint8_t *const *)(peb + 0x18u);
        if (ldr != NULL) {
            const scarlett_veh_list_entry_t *head = (const scarlett_veh_list_entry_t *)(ldr + 0x20u);
            const uint8_t *k32_base = NULL;
            const uint8_t *kbase_base = NULL;
            for (const scarlett_veh_list_entry_t *cur = head->Flink;
                 cur != NULL && cur != head;
                 cur = cur->Flink) {
                const uint8_t *entry = (const uint8_t *)cur;
                const uint8_t *dll_base = *(const uint8_t *const *)(entry + 0x20u);
                const scarlett_veh_unicode_string_t *base_name =
                    (const scarlett_veh_unicode_string_t *)(entry + 0x48u);
                if (dll_base != NULL && base_name->Buffer != NULL) {
                    if (xgr_veh_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernel32.dll")) {
                        k32_base = dll_base;
                    } else if (xgr_veh_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernelbase.dll")) {
                        kbase_base = dll_base;
                    }
                }
            }
            void *vp = xgr_veh_find_export(k32_base, "VirtualProtect");
            if (vp == NULL) {
                vp = xgr_veh_find_export(kbase_base, "VirtualProtect");
            }
            void *va = xgr_veh_find_export(k32_base, "VirtualAlloc");
            if (va == NULL) {
                va = xgr_veh_find_export(kbase_base, "VirtualAlloc");
            }
            g_veh_virtual_protect = (scarlett_pfn_virtual_protect_t)vp;
            g_veh_virtual_alloc = (scarlett_pfn_virtual_alloc_t)va;
        }
    }
    __atomic_store_n(&g_veh_vp_resolved, 1u, __ATOMIC_RELEASE);
    return g_veh_virtual_protect;
}

static size_t xgr_veh_append_cstr(char *buf, size_t cap, size_t pos, const char *s) {
    while (*s != '\0' && pos + 1u < cap) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    return pos;
}

static size_t xgr_veh_append_hex64(char *buf, size_t cap, size_t pos, uint64_t val) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4) {
        if (pos + 1u < cap) {
            buf[pos++] = hex[(val >> shift) & 0xFu];
        }
    }
    buf[pos] = '\0';
    return pos;
}

static size_t xgr_veh_append_u32(char *buf, size_t cap, size_t pos, uint32_t val) {
    char tmp[12];
    uint32_t n = 0u;
    if (val == 0u) {
        tmp[n++] = '0';
    } else {
        while (val > 0u && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    while (n > 0u && pos + 1u < cap) {
        buf[pos++] = tmp[--n];
    }
    buf[pos] = '\0';
    return pos;
}

static void xgr_veh_log_patched(const char *insn_name, uint64_t rip, uint32_t len) {
    char msg[128];
    size_t pos = 0u;
    pos = xgr_veh_append_cstr(msg, sizeof(msg), pos, "[scarlett-veh] Patched ");
    pos = xgr_veh_append_cstr(msg, sizeof(msg), pos, insn_name);
    pos = xgr_veh_append_cstr(msg, sizeof(msg), pos, " at 0x");
    pos = xgr_veh_append_hex64(msg, sizeof(msg), pos, rip);
    pos = xgr_veh_append_cstr(msg, sizeof(msg), pos, " (len=");
    pos = xgr_veh_append_u32(msg, sizeof(msg), pos, len);
    xgr_veh_append_cstr(msg, sizeof(msg), pos, ")\n");
    OutputDebugStringA(msg);
}

static uint64_t xgr_veh_get_reg64(const scarlett_win_context_t *ctx, uint32_t reg_idx) {
    switch (reg_idx & 0xFu) {
        case 0u: return ctx->Rax;
        case 1u: return ctx->Rcx;
        case 2u: return ctx->Rdx;
        case 3u: return ctx->Rbx;
        case 4u: return ctx->Rsp;
        case 5u: return ctx->Rbp;
        case 6u: return ctx->Rsi;
        case 7u: return ctx->Rdi;
        case 8u: return ctx->R8;
        case 9u: return ctx->R9;
        case 10u: return ctx->R10;
        case 11u: return ctx->R11;
        case 12u: return ctx->R12;
        case 13u: return ctx->R13;
        case 14u: return ctx->R14;
        case 15u: return ctx->R15;
        default: return 0u;
    }
}

static void xgr_veh_zero_reg64(scarlett_win_context_t *ctx, uint32_t reg_idx) {
    switch (reg_idx & 0xFu) {
        case 0u: ctx->Rax = 0u; break;
        case 1u: ctx->Rcx = 0u; break;
        case 2u: ctx->Rdx = 0u; break;
        case 3u: ctx->Rbx = 0u; break;
        case 4u: ctx->Rsp = 0u; break;
        case 5u: ctx->Rbp = 0u; break;
        case 6u: ctx->Rsi = 0u; break;
        case 7u: ctx->Rdi = 0u; break;
        case 8u: ctx->R8 = 0u; break;
        case 9u: ctx->R9 = 0u; break;
        case 10u: ctx->R10 = 0u; break;
        case 11u: ctx->R11 = 0u; break;
        case 12u: ctx->R12 = 0u; break;
        case 13u: ctx->R13 = 0u; break;
        case 14u: ctx->R14 = 0u; break;
        case 15u: ctx->R15 = 0u; break;
        default: break;
    }
}

static int32_t xgr_veh_hotpatch_site(
    scarlett_win_context_t *ctx,
    const char *insn_name,
    uint32_t len,
    uint8_t expected_b0,
    const uint8_t *patch_bytes) {
    uint64_t rip = ctx->Rip;
    const volatile uint8_t *code = (const volatile uint8_t *)(uintptr_t)rip;

    while (__atomic_exchange_n(&g_veh_patch_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        __asm__ volatile("pause");
    }
    if (*code != expected_b0) {
        /* Another thread already patched this site */
        __atomic_store_n(&g_veh_patch_lock, 0u, __ATOMIC_RELEASE);
        return -1; /* EXCEPTION_CONTINUE_EXECUTION */
    }
    scarlett_pfn_virtual_protect_t vp = xgr_veh_get_virtual_protect();
    uint32_t old_prot = 0u;
    void *patch_addr = (void *)(uintptr_t)rip;
    if (vp != NULL && vp(patch_addr, (size_t)len, 0x40u /* PAGE_EXECUTE_READWRITE */, &old_prot) != 0) {
        volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)rip;
        for (uint32_t i = 0u; i < len; ++i) {
            dst[i] = (patch_bytes != NULL) ? patch_bytes[i] : 0x90u;
        }
        __builtin_ia32_sfence();
        uint32_t dummy_prot = 0u;
        (void)vp(patch_addr, (size_t)len, old_prot, &dummy_prot);
        __atomic_store_n(&g_veh_patch_lock, 0u, __ATOMIC_RELEASE);
        xgr_veh_log_patched(insn_name, rip, len);
        return -1; /* EXCEPTION_CONTINUE_EXECUTION */
    }
    __atomic_store_n(&g_veh_patch_lock, 0u, __ATOMIC_RELEASE);
    ctx->Rip += len;
    return -1; /* EXCEPTION_CONTINUE_EXECUTION */
}

static uint8_t *xgr_veh_get_clzero_trampoline(
    uint64_t rip,
    const uint8_t *cmp_bytes,
    uint32_t bound_reg) {
    if (bound_reg == 0u || bound_reg == 4u || bound_reg >= 16u) {
        return NULL;
    }
    (void)xgr_veh_get_virtual_protect();
    scarlett_pfn_virtual_alloc_t va = g_veh_virtual_alloc;
    if (va == NULL) {
        return NULL;
    }
    while (__atomic_exchange_n(&g_veh_patch_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        __asm__ volatile("pause");
    }
    uint8_t *stub = g_clzero_tramp_stubs[bound_reg];
    if (stub != NULL) {
        int64_t rel = (int64_t)(uintptr_t)stub - (int64_t)(rip + 5u);
        if (rel >= (int64_t)INT32_MIN && rel <= (int64_t)INT32_MAX) {
            __atomic_store_n(&g_veh_patch_lock, 0u, __ATOMIC_RELEASE);
            return stub;
        }
        stub = NULL;
    }
    if (g_clzero_tramp_page != NULL && (g_clzero_tramp_used + 128u) <= 4096u) {
        int64_t page_rel = (int64_t)(uintptr_t)g_clzero_tramp_page - (int64_t)(rip + 5u);
        if (page_rel < -0x7FFF0000LL || page_rel > 0x7FFF0000LL) {
            g_clzero_tramp_page = NULL;
            g_clzero_tramp_used = 0u;
        }
    } else {
        g_clzero_tramp_page = NULL;
        g_clzero_tramp_used = 0u;
    }
    if (g_clzero_tramp_page == NULL) {
        uint64_t base_hint = rip & ~0xFFFFULL;
        for (uint64_t off = 0x01000000ULL; off <= 0x70000000ULL; off += 0x01000000ULL) {
            void *p = va((void *)(uintptr_t)(base_hint + off), 4096u, 0x3000u, 0x40u);
            if (p != NULL) {
                int64_t d = (int64_t)(uintptr_t)p - (int64_t)(rip + 5u);
                if (d >= -0x7FFF0000LL && d <= 0x7FFF0000LL) {
                    g_clzero_tramp_page = (uint8_t *)p;
                    g_clzero_tramp_used = 0u;
                    break;
                }
            }
            if (base_hint > off + 0x100000ULL) {
                p = va((void *)(uintptr_t)(base_hint - off), 4096u, 0x3000u, 0x40u);
                if (p != NULL) {
                    int64_t d = (int64_t)(uintptr_t)p - (int64_t)(rip + 5u);
                    if (d >= -0x7FFF0000LL && d <= 0x7FFF0000LL) {
                        g_clzero_tramp_page = (uint8_t *)p;
                        g_clzero_tramp_used = 0u;
                        break;
                    }
                }
            }
        }
    }
    if (g_clzero_tramp_page == NULL || (g_clzero_tramp_used + 128u) > 4096u) {
        __atomic_store_n(&g_veh_patch_lock, 0u, __ATOMIC_RELEASE);
        return NULL;
    }
    stub = g_clzero_tramp_page + g_clzero_tramp_used;
    size_t n = 0u;
    uint32_t s = (bound_reg != 7u) ? 7u : 6u; /* rdi or rsi */
    stub[n++] = (uint8_t)(0x50u + s);          /* push %rdi/%rsi */
    size_t loop_off = n;
    stub[n++] = 0x48u;
    stub[n++] = 0x89u;
    stub[n++] = (uint8_t)(0xC0u + s);          /* mov %rax, %rdi/%rsi */
    stub[n++] = 0x48u;
    stub[n++] = 0x83u;
    stub[n++] = (uint8_t)(0xE0u + s);
    stub[n++] = 0xC0u;                         /* and $-64, %rdi/%rsi */
    for (uint32_t k = 0u; k < 8u; ++k) {
        stub[n++] = 0x48u;
        stub[n++] = 0xC7u;
        stub[n++] = (uint8_t)(0x40u + s);
        stub[n++] = (uint8_t)(k * 8u);
        stub[n++] = 0x00u;
        stub[n++] = 0x00u;
        stub[n++] = 0x00u;
        stub[n++] = 0x00u;                     /* movq $0, (k*8)(%rdi/%rsi) */
    }
    stub[n++] = 0x48u;
    stub[n++] = 0x83u;
    stub[n++] = 0xC0u;
    stub[n++] = 0x40u;                         /* add $0x40, %rax */
    stub[n++] = cmp_bytes[0];
    stub[n++] = cmp_bytes[1];
    stub[n++] = cmp_bytes[2];                  /* cmp %bound_reg, %rax */
    int32_t jb_rel = (int32_t)loop_off - (int32_t)(n + 2u);
    stub[n++] = 0x72u;
    stub[n++] = (uint8_t)(int8_t)jb_rel;       /* jb loop_off */
    stub[n++] = (uint8_t)(0x58u + s);          /* pop %rdi/%rsi (preserves EFLAGS) */
    stub[n++] = 0xC3u;                         /* ret */
    g_clzero_tramp_used += (n + 15u) & ~(size_t)15u;
    g_clzero_tramp_stubs[bound_reg] = stub;
    __builtin_ia32_sfence();
    __atomic_store_n(&g_veh_patch_lock, 0u, __ATOMIC_RELEASE);
    return stub;
}

static int32_t xgr_veh_handle_illegal_insn(scarlett_win_context_t *ctx) {
    uint64_t rip = ctx->Rip;
    const uint8_t *code = (const uint8_t *)(uintptr_t)rip;
    if (code == NULL) {
        return 0; /* EXCEPTION_CONTINUE_SEARCH */
    }

    /* Check AMD monitorx (0F 01 FA) and mwaitx (0F 01 FB) */
    if (code[0] == 0x0Fu && code[1] == 0x01u) {
        if (code[2] == 0xFAu) {
            return xgr_veh_hotpatch_site(ctx, "monitorx", 3u, 0x0Fu, NULL);
        }
        if (code[2] == 0xFBu) {
            static const uint8_t pause_nop[3] = { 0xF3u, 0x90u, 0x90u };
            return xgr_veh_hotpatch_site(ctx, "mwaitx", 3u, 0x0Fu, pause_nop);
        }
    }

    /* Check clzero: 0F 01 FC (zeroes 64-byte cache line at RAX & ~63) */
    if (code[0] == 0x0Fu && code[1] == 0x01u && code[2] == 0xFCu) {
        static uint32_t s_clzero_logged = 0u;
        uint64_t rax = ctx->Rax;
        uint64_t line_base = rax & ~63ULL;
        if (__atomic_exchange_n(&s_clzero_logged, 1u, __ATOMIC_RELAXED) == 0u) {
            OutputDebugStringA("[scarlett-veh] Emulating/patching AMD clzero (0F 01 FC)\n");
        }
        /* Fast-path tight loop: clzero; add $0x40, %rax; cmp %rXX, %rax; jb -12 */
        if (line_base != 0u &&
            code[3] == 0x48u && code[4] == 0x83u && code[5] == 0xC0u && code[6] == 0x40u &&
            (code[7] & 0xFBu) == 0x48u && code[8] == 0x39u && (code[9] & 0xC7u) == 0xC0u &&
            code[10] == 0x72u && code[11] == 0xF4u) {
            uint32_t bound_reg = ((uint32_t)(code[9] >> 3) & 7u) | (((uint32_t)(code[7] & 0x04u)) << 1);
            uint8_t *stub = xgr_veh_get_clzero_trampoline(rip, &code[7], bound_reg);
            if (stub != NULL) {
                int64_t rel = (int64_t)(uintptr_t)stub - (int64_t)(rip + 5u);
                if (rel >= (int64_t)INT32_MIN && rel <= (int64_t)INT32_MAX) {
                    int32_t rel32 = (int32_t)rel;
                    uint8_t patch[12];
                    patch[0] = 0xE8u; /* call rel32 */
                    patch[1] = (uint8_t)(rel32 & 0xFF);
                    patch[2] = (uint8_t)((rel32 >> 8) & 0xFF);
                    patch[3] = (uint8_t)((rel32 >> 16) & 0xFF);
                    patch[4] = (uint8_t)((rel32 >> 24) & 0xFF);
                    for (uint32_t i = 5u; i < 12u; ++i) {
                        patch[i] = 0x90u;
                    }
                    return xgr_veh_hotpatch_site(ctx, "clzero_loop", 12u, 0x0Fu, patch);
                }
            }
            uint64_t bound = xgr_veh_get_reg64(ctx, bound_reg);
            if (rax < bound && (bound - rax) <= (256ULL * 1024ULL * 1024ULL)) {
                uint64_t n_iters = (bound - rax + 63ULL) / 64ULL;
                volatile uint64_t *q = (volatile uint64_t *)(uintptr_t)line_base;
                size_t n_qwords = (size_t)(n_iters * 8ULL);
                for (size_t i = 0u; i < n_qwords; ++i) {
                    q[i] = 0u;
                }
                ctx->Rax = rax + n_iters * 64ULL;
                ctx->Rip += 12u;
                return -1; /* EXCEPTION_CONTINUE_EXECUTION */
            }
        }
        if (line_base != 0u) {
            volatile uint64_t *q = (volatile uint64_t *)(uintptr_t)line_base;
            for (size_t i = 0u; i < 8u; ++i) {
                q[i] = 0u;
            }
        }
        ctx->Rip += 3u;
        return -1; /* EXCEPTION_CONTINUE_EXECUTION */
    }

    /* Check rdpid: F3 [REX 0x40..0x4F] 0F C7 /7 (mod == 3) */
    if (code[0] == 0xF3u) {
        uint32_t pos = 1u;
        uint8_t rex = 0u;
        if (code[pos] >= 0x40u && code[pos] <= 0x4Fu) {
            rex = code[pos++];
        }
        if (code[pos] == 0x0Fu && code[pos + 1u] == 0xC7u) {
            uint8_t modrm = code[pos + 2u];
            if ((modrm >> 6) == 3u && ((modrm >> 3) & 7u) == 7u) {
                uint32_t reg_idx = (uint32_t)(modrm & 7u) | (((uint32_t)(rex & 1u)) << 3);
                uint32_t len = pos + 3u;
                xgr_veh_zero_reg64(ctx, reg_idx);
                ctx->Rip += len;
                return -1; /* EXCEPTION_CONTINUE_EXECUTION */
            }
        }
        return 0;
    }

    /* Check clwb / clflushopt: 66 [REX 0x40..0x4F] 0F AE /6 or /7 (mod != 3) */
    if (code[0] == 0x66u) {
        uint32_t pos = 1u;
        if (code[pos] >= 0x40u && code[pos] <= 0x4Fu) {
            pos++;
        }
        if (code[pos] == 0x0Fu && code[pos + 1u] == 0xAEu) {
            pos += 2u;
            uint8_t modrm = code[pos++];
            uint8_t mod = (modrm >> 6) & 3u;
            uint8_t reg = (modrm >> 3) & 7u;
            uint8_t rm = modrm & 7u;
            if ((reg == 6u || reg == 7u) && mod != 3u) {
                if (rm == 4u) {
                    uint8_t sib = code[pos++];
                    uint8_t sib_base = sib & 7u;
                    if (mod == 0u && sib_base == 5u) {
                        pos += 4u;
                    }
                } else if (mod == 0u && rm == 5u) {
                    pos += 4u;
                }
                if (mod == 1u) {
                    pos += 1u;
                } else if (mod == 2u) {
                    pos += 4u;
                }
                uint32_t len = pos;
                const char *insn_name = (reg == 6u) ? "clwb" : "clflushopt";
                return xgr_veh_hotpatch_site(ctx, insn_name, len, 0x66u, NULL);
            }
        }
    }

    {
        static const char hex[] = "0123456789abcdef";
        char msg[128];
        size_t pos = 0u;
        pos = xgr_veh_append_cstr(msg, sizeof(msg), pos, "[scarlett-veh] Unhandled illegal insn at 0x");
        pos = xgr_veh_append_hex64(msg, sizeof(msg), pos, rip);
        pos = xgr_veh_append_cstr(msg, sizeof(msg), pos, " bytes=");
        for (uint32_t i = 0u; i < 8u; ++i) {
            if (pos + 2u < sizeof(msg)) {
                msg[pos++] = hex[(code[i] >> 4) & 0xFu];
                msg[pos++] = hex[code[i] & 0xFu];
            }
        }
        xgr_veh_append_cstr(msg, sizeof(msg), pos, "\n");
        OutputDebugStringA(msg);
    }

    return 0; /* EXCEPTION_CONTINUE_SEARCH */
}

static int32_t __stdcall scarlett_veh_skip_int3_handler(scarlett_win_exception_pointers_t *ep) {
    if (ep == NULL || ep->ExceptionRecord == NULL || ep->ContextRecord == NULL) {
        return 0; /* EXCEPTION_CONTINUE_SEARCH */
    }
    uint32_t code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x80000003u /* EXCEPTION_BREAKPOINT */) {
        const unsigned char *at = (const unsigned char *)ep->ExceptionRecord->ExceptionAddress;
        if (at != NULL && *at == 0xCCu) {
            if (ep->ContextRecord->Rip == (uint64_t)(uintptr_t)at) {
                ep->ContextRecord->Rip += 1u;
            }
            OutputDebugStringA("[scarlett-veh] Stepped over int3 soft assert\n");
            return -1; /* EXCEPTION_CONTINUE_EXECUTION */
        }
    } else if (code == 0xC000001Du /* EXCEPTION_ILLEGAL_INSTRUCTION */) {
        return xgr_veh_handle_illegal_insn(ep->ContextRecord);
    }
    return 0; /* EXCEPTION_CONTINUE_SEARCH */
}

uint32_t SCARLETT_XGR_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == 1u) {
        AddVectoredExceptionHandler(1u, scarlett_veh_skip_int3_handler);
        if (xgr_pe_init_unix_bridge() != 0) {
            return 0u;
        }
        if (scarlett_failed(XGameRuntimeInitialize())) {
            return 0u;
        }
    }
    return 1u;
}
#endif
