/*
 * Proton-Scarlett — d3d12_xs.dll Freestanding PE64 Layer
 *
 * Strict Wine 11.0 PE64 isolation:
 *   - Strictly freestanding C11 (zero libc, zero <windows.h>, zero libvulkan, zero POSIX).
 *   - Undefined symbols under `clang --target=x86_64-w64-mingw32 -std=c11 -ffreestanding -O2 -c`
 *     are strictly ONLY: `__wine_init_unix_call`, `__wine_unix_call_dispatcher`,
 *     and `__wine_unixlib_handle`.
 *   - Zero silent stubs: every unimplemented export and removed/unimplemented COM vtable
 *     slot routes through the diagnostic auto-stub trampoline (never returns S_OK).
 */

#include "d3d12_xs_api.h"

/* ============================================================================
 * Freestanding Memory Helpers (volatile loops prevent memset/memcpy emission)
 * ============================================================================ */

static inline void d3d12xs_pe_zero(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0u; i < n; ++i) {
        p[i] = 0u;
    }
}

static inline void d3d12xs_pe_copy_cstr(char *dst, size_t cap, const char *src) {
    if (dst == NULL || cap == 0u) {
        return;
    }
    size_t i = 0u;
    if (src != NULL) {
        while (i + 1u < cap && src[i] != '\0') {
            dst[i] = src[i];
            ++i;
        }
    }
    while (i < cap) {
        dst[i] = '\0';
        ++i;
    }
}

static inline bool d3d12xs_guid_eq(const scarlett_guid_t *a, const scarlett_guid_t *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) {
        return false;
    }
    for (size_t i = 0u; i < 8u; ++i) {
        if (a->data4[i] != b->data4[i]) {
            return false;
        }
    }
    return true;
}

/* ============================================================================
 * WINE_UNIX_CALL Bridge Abstraction (PE64 vs Linux Host Test Link)
 * ============================================================================ */

#if defined(_WIN32) || defined(__MINGW32__)

#if defined(WINE_UNIX_CALL)
static inline int32_t d3d12xs_pe_unix_call(uint32_t opcode, void *params) {
    return (int32_t)WINE_UNIX_CALL(opcode, params);
}
static inline int32_t d3d12xs_pe_init_unix_bridge(void) {
    return (int32_t)__wine_init_unix_call();
}
#else
typedef uint64_t scarlett_wine_unixlib_handle_t;
typedef int32_t (SCARLETT_D3D12XS_CALL *scarlett_wine_unix_dispatcher_t)(
    scarlett_wine_unixlib_handle_t handle,
    uint32_t code,
    void *args);

extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern scarlett_wine_unix_dispatcher_t __wine_unix_call_dispatcher;
extern int32_t __wine_init_unix_call(void);

static inline int32_t d3d12xs_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, opcode, params);
}

static inline int32_t d3d12xs_pe_init_unix_bridge(void) {
    return __wine_init_unix_call();
}
#endif

#else

static scarlett_unix_call_fn_t g_d3d12xs_unix_bridge = scarlett_d3d12xs_unix_call;

SCARLETT_D3D12XS_API void scarlett_d3d12xs_set_unix_call_bridge(scarlett_unix_call_fn_t fn) {
    g_d3d12xs_unix_bridge = fn;
}

SCARLETT_D3D12XS_API scarlett_unix_call_fn_t scarlett_d3d12xs_get_unix_call_bridge(void) {
    return g_d3d12xs_unix_bridge;
}

static inline int32_t d3d12xs_pe_unix_call(uint32_t opcode, void *params) {
    if (g_d3d12xs_unix_bridge == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return g_d3d12xs_unix_bridge(opcode, params);
}

#endif

/* ============================================================================
 * Diagnostic Auto-Stub Trampoline (Zero Silent Stubs)
 * ============================================================================ */

static uint32_t g_d3d12xs_stub_hit_counter = 0u;

static scarlett_hresult_t d3d12xs_pe_dispatch_stub(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_d3d12xs_report_stub_params_t params;
    d3d12xs_pe_zero(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    if (desc != NULL) {
        params.ctx.flags = desc->flags;
        params.ctx.ordinal = desc->ordinal;
        params.ctx.vtable_slot = desc->vtable_slot;
        params.ctx.return_hr = scarlett_stub_enforce_error(desc->default_error);
        d3d12xs_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), desc->dll_name);
        d3d12xs_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), desc->symbol_name);
    } else {
        params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
        params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
        params.ctx.return_hr = SCARLETT_ERR_UNIMPLEMENTED_STUB;
        d3d12xs_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), "d3d12_xs.dll");
        d3d12xs_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), "unknown_symbol");
    }

    params.ctx.return_address = return_address;
    params.ctx.caller_module_base = 0u;
    params.ctx.caller_rva = return_address;
    params.ctx.rcx = rcx;
    params.ctx.rdx = rdx;
    params.ctx.r8 = r8;
    params.ctx.r9 = r9;
    params.ctx.hit_count = ++g_d3d12xs_stub_hit_counter;
    d3d12xs_pe_copy_cstr(params.ctx.caller_module_name,
                         sizeof(params.ctx.caller_module_name),
                         "d3d12_xs_caller");

    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_REPORT_STUB, &params);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
#if defined(_WIN32) || defined(__MINGW32__)
        __builtin_trap();
#else
        return SCARLETT_ERR_STRICT_STUB_ABORT;
#endif
    }
    if (scarlett_failed(rc)) {
        return scarlett_stub_enforce_error(rc);
    }
    return scarlett_stub_enforce_error(params.ctx.return_hr);
}

/* ============================================================================
 * ID3D12DeviceX Static COM Object Pool & VTable (Matching d3d12_xs.xdl)
 * ============================================================================ */

#define SCARLETT_D3D12XS_MAX_COM_DEVICES 16u
static ID3D12DeviceX g_com_devices[SCARLETT_D3D12XS_MAX_COM_DEVICES];

static const scarlett_guid_t kIidIUnknown = {
    UINT32_C(0x00000000), UINT16_C(0x0000), UINT16_C(0x0000),
    { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }
};

static const scarlett_guid_t kIidID3D12DeviceX = SCARLETT_IID_ID3D12DEVICEX_INIT;

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_query_interface(
    ID3D12DeviceX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidIUnknown) || d3d12xs_guid_eq(riid, &kIidID3D12DeviceX)) {
        self->ref_count++;
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL com_add_ref(ID3D12DeviceX *self) {
    if (self == NULL) {
        return 0u;
    }
    self->ref_count++;
    return self->ref_count;
}

static uint32_t SCARLETT_D3D12XS_CALL com_release(ID3D12DeviceX *self) {
    if (self == NULL || self->ref_count == 0u) {
        return 0u;
    }
    self->ref_count--;
    if (self->ref_count == 0u) {
        (void)D3D12XboxDestroyDeviceX(self->unix_device_handle);
        self->unix_device_handle = 0u;
        self->in_use = 0u;
    }
    return self->ref_count;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_create_placed_resource_x(
    ID3D12DeviceX *self,
    uint64_t gpuAddress,
    const void *pDesc,
    uint32_t InitialResourceState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    (void)pOptimizedClearValue;
    (void)riid;
    if (self == NULL || ppvResource == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvResource = NULL;
    uint64_t res_handle = 0u;
    uint32_t uma_tier = 0u;
    scarlett_hresult_t hr = D3D12XboxCreatePlacedResourceX(
        self->unix_device_handle,
        gpuAddress,
        (const D3D12XBOX_RESOURCE_DESC *)pDesc,
        InitialResourceState,
        &res_handle,
        &uma_tier);
    if (scarlett_succeeded(hr)) {
        *ppvResource = (void *)(uintptr_t)res_handle;
    }
    return hr;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_present_x(
    ID3D12DeviceX *self,
    uint32_t PlaneCount,
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *pPlaneParameters,
    const void *pPresentParameters) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    return D3D12XboxPresentX(
        self->unix_device_handle,
        PlaneCount,
        pPlaneParameters,
        (const D3D12XBOX_PRESENT_PARAMETERS *)pPresentParameters,
        NULL);
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_wait_frame_event_x(
    ID3D12DeviceX *self,
    uint32_t FrameEventType,
    uint64_t TimeoutInMilliseconds,
    const D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC *pTokenDesc,
    uint64_t *pOutEventTimestamp) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_d3d12xs_wait_frame_params_t out_wait;
    d3d12xs_pe_zero(&out_wait, sizeof(out_wait));
    uint64_t expected_token = (pTokenDesc != NULL) ? pTokenDesc->Token : 0u;
    scarlett_hresult_t hr = D3D12XboxWaitFrameEventX(
        self->unix_device_handle,
        FrameEventType,
        TimeoutInMilliseconds,
        expected_token,
        &out_wait);
    if (pOutEventTimestamp != NULL) {
        *pOutEventTimestamp = out_wait.out_event_timestamp_ns;
    }
    return hr;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_set_amp_shader_limits(
    ID3D12DeviceX *self,
    uint32_t IndirectArgsBufferSize,
    uint32_t PayloadBufferSize) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_d3d12xs_set_amp_limits_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = self->unix_device_handle;
    p.indirect_args_buffer_bytes = IndirectArgsBufferSize;
    p.payload_buffer_bytes = PayloadBufferSize;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_SET_AMP_SHADER_LIMITS, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p.out_status;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_legacy_present_sync_x_stub(
    ID3D12DeviceX *self,
    uint64_t FrameToken,
    uint32_t TimeoutMs) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_XDL_VTABLE | SCARLETT_STUB_FLAG_REMOVED_IN_ABI,
        SCARLETT_STUB_ORDINAL_NONE,
        7u,
        SCARLETT_E_NOTIMPL,
        0u,
        "d3d12_xs.dll",
        "ID3D12DeviceX::LegacyPresentSyncX"
    };
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        FrameToken,
        (uint64_t)TimeoutMs,
        0u);
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_create_reserved_resource_debug_x_stub(
    ID3D12DeviceX *self,
    const void *pDesc,
    uint32_t InitialState,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_XDL_VTABLE,
        SCARLETT_STUB_ORDINAL_NONE,
        8u,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "ID3D12DeviceX::CreateReservedResourceDebugX"
    };
    if (ppvResource != NULL) {
        *ppvResource = NULL;
    }
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pDesc,
        (uint64_t)InitialState,
        (uint64_t)(uintptr_t)riid);
}

static const ID3D12DeviceXVtbl g_device_vtbl = {
    com_query_interface,
    com_add_ref,
    com_release,
    com_create_placed_resource_x,
    com_present_x,
    com_wait_frame_event_x,
    com_set_amp_shader_limits,
    com_legacy_present_sync_x_stub,
    com_create_reserved_resource_debug_x_stub
};

/* ============================================================================
 * Implemented PE64 Exports (Strictly Routed via WINE_UNIX_CALL)
 * ============================================================================ */

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateDevice(
    const D3D12XBOX_CREATE_DEVICE_PARAMETERS *pParams,
    const scarlett_guid_t *riid,
    void **ppDevice) {
    if (ppDevice == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppDevice = NULL;

    if (riid != NULL && !d3d12xs_guid_eq(riid, &kIidID3D12DeviceX) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        return SCARLETT_E_NOINTERFACE;
    }

    ID3D12DeviceX *slot = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DEVICES; ++i) {
        if (g_com_devices[i].in_use == 0u) {
            slot = &g_com_devices[i];
            break;
        }
    }
    if (slot == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_d3d12xs_create_device_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    if (pParams != NULL) {
        p.gxdk_version = pParams->Version;
        p.process_debug_flags = pParams->ProcessDebugFlags;
        p.graphics_cmd_buf_bytes = pParams->GraphicsCommandBufferSizeBytes;
        p.graphics_scratch_bytes = pParams->GraphicsScratchMemorySizeBytes;
        p.compute_cmd_buf_bytes = pParams->ComputeCommandBufferSizeBytes;
        p.compute_scratch_bytes = pParams->ComputeScratchMemorySizeBytes;
        p.dma_cmd_buf_bytes = pParams->DmaCommandBufferSizeBytes;
        p.auto_breadcrumb_node_count = pParams->AutoBreadcrumbNodeCount;
        p.amp_shader_indirect_args_bytes = pParams->AmplificationShaderIndirectArgsBufferSize;
        p.amp_shader_payload_bytes = pParams->AmplificationShaderPayloadBufferSize;
        p.devkit_diagnostic_buffer_bytes = pParams->DevKitDiagnosticBufferSize;
        p.request_validation = (pParams->ProcessDebugFlags != 0u) ? 1u : 0u;
    } else {
        p.gxdk_version = SCARLETT_GXDK_MAGIC;
        p.request_validation = 1u;
    }

    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_CREATE_DEVICE, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (scarlett_failed(p.out_status)) {
        return p.out_status;
    }

    slot->lpVtbl = &g_device_vtbl;
    slot->unix_device_handle = p.out_device_handle;
    slot->ref_count = 1u;
    slot->in_use = 1u;
    *ppDevice = (void *)slot;
    return SCARLETT_OK;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxDestroyDeviceX(
    uint64_t device_handle) {
    scarlett_d3d12xs_destroy_device_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = device_handle;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_DESTROY_DEVICE, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p.out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxQueryDeviceCapsX(
    uint64_t device_handle,
    scarlett_d3d12xs_query_caps_params_t *out_caps) {
    if (out_caps == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_pe_zero(out_caps, sizeof(*out_caps));
    out_caps->device_handle = device_handle;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_QUERY_CAPS, out_caps);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return out_caps->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateCommandQueueX(
    uint64_t device_handle,
    uint32_t queue_type,
    uint32_t priority,
    scarlett_d3d12xs_create_queue_params_t *out_queue_info) {
    if (out_queue_info == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_pe_zero(out_queue_info, sizeof(*out_queue_info));
    out_queue_info->device_handle = device_handle;
    out_queue_info->queue_type = queue_type;
    out_queue_info->priority = priority;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_CREATE_QUEUE, out_queue_info);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return out_queue_info->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateCommandListX(
    uint64_t device_handle,
    uint32_t queue_type,
    uint32_t flags,
    uint64_t *out_cmd_list_handle) {
    if (out_cmd_list_handle == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_cmd_list_handle = 0u;
    scarlett_d3d12xs_create_cmd_list_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = device_handle;
    p.queue_type = queue_type;
    p.flags = flags;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_CREATE_CMD_LIST, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    *out_cmd_list_handle = p.out_cmd_list_handle;
    return p.out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreatePlacedResourceX(
    uint64_t device_handle,
    uint64_t gpu_va,
    const D3D12XBOX_RESOURCE_DESC *pDesc,
    uint32_t initial_state,
    uint64_t *out_resource_handle,
    uint32_t *out_uma_tier) {
    if (pDesc == NULL || out_resource_handle == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_resource_handle = 0u;
    scarlett_d3d12xs_create_placed_resource_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = device_handle;
    p.gpu_va = gpu_va;
    p.size_bytes = pDesc->ByteSize;
    p.dimension = pDesc->Dimension;
    p.width = pDesc->Width;
    p.height = pDesc->Height;
    p.depth_or_array = pDesc->DepthOrArraySize;
    p.mip_levels = pDesc->MipLevels;
    p.dxgi_format = pDesc->Format;
    p.swizzle_mode = pDesc->SwizzleMode;
    p.initial_state = initial_state;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_CREATE_PLACED_RESOURCE, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    *out_resource_handle = p.out_resource_handle;
    if (out_uma_tier != NULL) {
        *out_uma_tier = p.out_uma_tier;
    }
    return p.out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxDestroyResourceX(
    uint64_t device_handle,
    uint64_t resource_handle) {
    scarlett_d3d12xs_destroy_resource_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = device_handle;
    p.resource_handle = resource_handle;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_DESTROY_RESOURCE, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p.out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateDescriptorHeapX(
    uint64_t device_handle,
    uint32_t heap_type,
    uint32_t num_descriptors,
    uint32_t flags,
    scarlett_d3d12xs_create_desc_heap_params_t *out_heap_info) {
    if (out_heap_info == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_pe_zero(out_heap_info, sizeof(*out_heap_info));
    out_heap_info->device_handle = device_handle;
    out_heap_info->heap_type = heap_type;
    out_heap_info->num_descriptors = num_descriptors;
    out_heap_info->flags = flags;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_CREATE_DESC_HEAP, out_heap_info);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return out_heap_info->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxWriteDescriptorX(
    scarlett_d3d12xs_write_desc_params_t *params) {
    if (params == NULL) {
        return SCARLETT_E_POINTER;
    }
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_WRITE_DESCRIPTOR, params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxEvaluateDescriptorPolicyX(
    scarlett_d3d12xs_eval_desc_policy_params_t *params) {
    if (params == NULL) {
        return SCARLETT_E_POINTER;
    }
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_EVAL_DESC_POLICY, params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxRecordEnhancedBarrierX(
    scarlett_d3d12xs_record_barrier_params_t *params) {
    if (params == NULL) {
        return SCARLETT_E_POINTER;
    }
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_RECORD_BARRIER, params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxExecuteIndirectX(
    scarlett_d3d12xs_record_indirect_params_t *params) {
    if (params == NULL) {
        return SCARLETT_E_POINTER;
    }
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_RECORD_INDIRECT, params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxExecuteCommandListsX(
    uint64_t device_handle,
    uint64_t queue_handle,
    const uint64_t *cmd_list_handles,
    uint32_t cmd_list_count,
    uint64_t uma_region_addr,
    uint64_t uma_region_size,
    scarlett_d3d12xs_execute_cmd_lists_params_t *out_exec_info) {
    if (cmd_list_handles == NULL || cmd_list_count == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_d3d12xs_execute_cmd_lists_params_t local_p;
    scarlett_d3d12xs_execute_cmd_lists_params_t *p =
        (out_exec_info != NULL) ? out_exec_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->queue_handle = queue_handle;
    p->cmd_list_handles_addr = (uint64_t)(uintptr_t)cmd_list_handles;
    p->cmd_list_count = cmd_list_count;
    p->uma_region_addr = uma_region_addr;
    p->uma_region_size = uma_region_size;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_EXECUTE_CMD_LISTS, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxStripDxilContainerX(
    const void *src_bytes,
    uint64_t src_size,
    void *dst_bytes,
    uint64_t dst_capacity,
    scarlett_d3d12xs_strip_dxil_params_t *out_info) {
    scarlett_d3d12xs_strip_dxil_params_t local_p;
    scarlett_d3d12xs_strip_dxil_params_t *p = (out_info != NULL) ? out_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->src_addr = (uint64_t)(uintptr_t)src_bytes;
    p->src_size = src_size;
    p->dst_addr = (uint64_t)(uintptr_t)dst_bytes;
    p->dst_capacity = dst_capacity;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_STRIP_DXIL, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxSetFrameIntervalX(
    uint64_t device_handle,
    uint32_t period_us,
    uint32_t origin_offset_us,
    uint32_t vrr_enabled,
    uint32_t flags) {
    scarlett_d3d12xs_frame_interval_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = device_handle;
    p.period_us = period_us;
    p.origin_offset_us = origin_offset_us;
    p.vrr_enabled = vrr_enabled;
    p.flags = flags;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_SET_FRAME_INTERVAL, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p.out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxScheduleFrameEventX(
    uint64_t device_handle,
    uint32_t event_type,
    uint32_t delay_us,
    uint64_t *out_token,
    uint64_t *out_target_ns) {
    scarlett_d3d12xs_schedule_frame_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.device_handle = device_handle;
    p.event_type = event_type;
    p.delay_us = delay_us;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_SCHEDULE_FRAME_EVENT, &p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_token != NULL) {
        *out_token = p.out_token;
    }
    if (out_target_ns != NULL) {
        *out_target_ns = p.out_target_ns;
    }
    return p.out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxWaitFrameEventX(
    uint64_t device_handle,
    uint32_t event_type,
    uint64_t timeout_ms,
    uint64_t expected_token,
    scarlett_d3d12xs_wait_frame_params_t *out_wait_info) {
    scarlett_d3d12xs_wait_frame_params_t local_p;
    scarlett_d3d12xs_wait_frame_params_t *p = (out_wait_info != NULL) ? out_wait_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->event_type = event_type;
    p->timeout_ms = timeout_ms;
    p->expected_token = expected_token;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_WAIT_FRAME_EVENT, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxPresentX(
    uint64_t device_handle,
    uint32_t plane_count,
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *pPlaneParameters,
    const D3D12XBOX_PRESENT_PARAMETERS *pPresentParameters,
    scarlett_d3d12xs_presentx_params_t *out_present_info) {
    if (pPlaneParameters == NULL || (plane_count != 1u && plane_count != 2u)) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_d3d12xs_presentx_params_t local_p;
    scarlett_d3d12xs_presentx_params_t *p =
        (out_present_info != NULL) ? out_present_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->plane_count = plane_count;
    for (uint32_t i = 0u; i < plane_count; ++i) {
        p->planes[i].resource_handle = (uint64_t)(uintptr_t)pPlaneParameters[i].pResource;
        p->planes[i].subresource_index = pPlaneParameters[i].SubresourceIndex;
        p->planes[i].flags = pPlaneParameters[i].Flags;
        p->planes[i].src_left = pPlaneParameters[i].SourceRectLeft;
        p->planes[i].src_top = pPlaneParameters[i].SourceRectTop;
        p->planes[i].src_right = pPlaneParameters[i].SourceRectRight;
        p->planes[i].src_bottom = pPlaneParameters[i].SourceRectBottom;
        p->planes[i].color_space = pPlaneParameters[i].ColorSpace;
    }
    if (pPresentParameters != NULL) {
        p->frame_token = pPresentParameters->Token;
        p->flags = pPresentParameters->Flags;
        p->hdr_max_luminance = pPresentParameters->HdrMaxLuminance;
        p->hdr_min_luminance = pPresentParameters->HdrMinLuminance;
        p->hdr_max_cll = pPresentParameters->HdrMaxContentLightLevel;
        p->hdr_max_fall = pPresentParameters->HdrMaxFrameAverageLightLevel;
    }
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_PRESENTX, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxResizeSwapChainX(
    uint64_t device_handle,
    uint32_t width,
    uint32_t height,
    uint32_t fullscreen,
    uint32_t hdr_enabled,
    uint32_t buffer_count,
    scarlett_d3d12xs_resize_swapchain_params_t *out_resize_info) {
    scarlett_d3d12xs_resize_swapchain_params_t local_p;
    scarlett_d3d12xs_resize_swapchain_params_t *p =
        (out_resize_info != NULL) ? out_resize_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->width = width;
    p->height = height;
    p->fullscreen = fullscreen;
    p->hdr_enabled = hdr_enabled;
    p->buffer_count = buffer_count;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_RESIZE_SWAPCHAIN, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxSuspendX(
    uint64_t device_handle,
    scarlett_d3d12xs_power_state_params_t *out_power_info) {
    scarlett_d3d12xs_power_state_params_t local_p;
    scarlett_d3d12xs_power_state_params_t *p =
        (out_power_info != NULL) ? out_power_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->power_op = (uint32_t)SCARLETT_POWER_OP_SUSPEND;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_POWER_STATE, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxResumeX(
    uint64_t device_handle,
    scarlett_d3d12xs_power_state_params_t *out_power_info) {
    scarlett_d3d12xs_power_state_params_t local_p;
    scarlett_d3d12xs_power_state_params_t *p =
        (out_power_info != NULL) ? out_power_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->power_op = (uint32_t)SCARLETT_POWER_OP_RESUME;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_POWER_STATE, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxDumpDredFaultX(
    uint64_t device_handle,
    char *out_buffer,
    uint32_t out_buffer_capacity,
    uint32_t simulate_fault,
    scarlett_d3d12xs_dred_dump_params_t *out_dump_info) {
    scarlett_d3d12xs_dred_dump_params_t local_p;
    scarlett_d3d12xs_dred_dump_params_t *p =
        (out_dump_info != NULL) ? out_dump_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->out_buf_addr = (uint64_t)(uintptr_t)out_buffer;
    p->out_buf_capacity = out_buffer_capacity;
    p->simulate_fault = simulate_fault;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_DUMP_DRED_FAULT, p);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return p->out_status;
}

/* ============================================================================
 * Unimplemented Exports Routed Strictly Through Diagnostic Auto-Stub Thunk
 * ============================================================================ */

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12GetDebugInterface(
    const scarlett_guid_t *riid,
    void **ppvDebug) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "D3D12GetDebugInterface"
    };
    if (ppvDebug != NULL) {
        *ppvDebug = NULL;
    }
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)riid,
        (uint64_t)(uintptr_t)ppvDebug,
        0u,
        0u);
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateMeshPipelineState(
    void *pDevice,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppState) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "D3D12XboxCreateMeshPipelineState"
    };
    if (ppState != NULL) {
        *ppState = NULL;
    }
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pDevice,
        (uint64_t)(uintptr_t)pDesc,
        (uint64_t)(uintptr_t)riid,
        (uint64_t)(uintptr_t)ppState);
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12SerializeRootSignature(
    const void *pRootSignature,
    uint32_t Version,
    void **ppBlob,
    void **ppErrorBlob) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "D3D12SerializeRootSignature"
    };
    if (ppBlob != NULL) {
        *ppBlob = NULL;
    }
    if (ppErrorBlob != NULL) {
        *ppErrorBlob = NULL;
    }
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pRootSignature,
        (uint64_t)Version,
        (uint64_t)(uintptr_t)ppBlob,
        (uint64_t)(uintptr_t)ppErrorBlob);
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12CreateRootSignatureDeserializer(
    const void *pSrcData,
    uint64_t SrcDataSizeInBytes,
    const scarlett_guid_t *pRootSignatureDeserializerInterface,
    void **ppRootSignatureDeserializer) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "D3D12CreateRootSignatureDeserializer"
    };
    if (ppRootSignatureDeserializer != NULL) {
        *ppRootSignatureDeserializer = NULL;
    }
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pSrcData,
        SrcDataSizeInBytes,
        (uint64_t)(uintptr_t)pRootSignatureDeserializerInterface,
        (uint64_t)(uintptr_t)ppRootSignatureDeserializer);
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateReservedResourceDebugX(
    void *pDevice,
    const void *pDesc,
    uint32_t InitialState,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "D3D12XboxCreateReservedResourceDebugX"
    };
    if (ppvResource != NULL) {
        *ppvResource = NULL;
    }
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pDevice,
        (uint64_t)(uintptr_t)pDesc,
        (uint64_t)InitialState,
        (uint64_t)(uintptr_t)riid);
}

/* ============================================================================
 * PE64 DllMain Entry (Calls __wine_init_unix_call() Once on DLL_PROCESS_ATTACH)
 * ============================================================================ */

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_D3D12XS_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == 1u) { /* DLL_PROCESS_ATTACH */
        if (d3d12xs_pe_init_unix_bridge() != 0) {
            return 0u;
        }
        scarlett_unix_init_params_t init_params;
        d3d12xs_pe_zero(&init_params, sizeof(init_params));
        init_params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
        init_params.module_id = SCARLETT_D3D12XS_UNIX_MODULE_ID;
        init_params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
        init_params.shader_mode = (uint32_t)SCARLETT_SHADER_MODE_ADAPTIVE;
        init_params.net_mode = (uint32_t)SCARLETT_NET_MODE_OFFLINE;
        if (scarlett_failed(d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_INIT, &init_params)) ||
            scarlett_failed(init_params.out_status)) {
            return 0u;
        }
    }
    return 1u;
}
#endif
