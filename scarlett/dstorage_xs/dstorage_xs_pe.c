/*
 * Proton-Scarlett — dstorage_xs.dll Freestanding PE64 Layer
 *
 * Strict Wine 11.0 PE64 isolation:
 *   - Strictly freestanding C11 (zero libc, zero <windows.h>, zero liburing, zero libvulkan).
 *   - Undefined symbols under `clang --target=x86_64-w64-mingw32 -std=c11 -ffreestanding -O2 -c`
 *     are strictly ONLY: `__wine_init_unix_call`, `__wine_unix_call_dispatcher`,
 *     and `__wine_unixlib_handle`.
 *   - Zero silent stubs: every unimplemented export or vtable slot routes through the
 *     diagnostic auto-stub trampoline (never returns S_OK).
 */

#include "dstorage_xs_api.h"

/* ============================================================================
 * Freestanding Memory & String Helpers (volatile prevents memset/memcpy emission)
 * ============================================================================ */

static inline void dstorage_pe_zero(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0u; i < n; ++i) {
        p[i] = 0u;
    }
}

static inline void dstorage_pe_copy_cstr(char *dst, size_t cap, const char *src) {
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

static inline uint32_t dstorage_pe_strlen_bounded(const char *s, uint32_t max_len) {
    if (s == NULL) {
        return 0u;
    }
    uint32_t n = 0u;
    while (n < max_len && s[n] != '\0') {
        ++n;
    }
    return n;
}

static uint32_t dstorage_pe_utf16_to_utf8(const uint16_t *src, char *dst, uint32_t dst_cap) {
    if (src == NULL || dst == NULL || dst_cap == 0u) {
        return 0u;
    }
    uint32_t out = 0u;
    uint32_t i = 0u;
    while (src[i] != 0u) {
        uint32_t cp = src[i++];
        if (cp >= 0xD800u && cp <= 0xDBFFu && src[i] >= 0xDC00u && src[i] <= 0xDFFFu) {
            uint32_t lo = src[i++];
            cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
        }
        if (cp < 0x80u) {
            if (out + 1u >= dst_cap) break;
            dst[out++] = (char)cp;
        } else if (cp < 0x800u) {
            if (out + 2u >= dst_cap) break;
            dst[out++] = (char)(0xC0u | (cp >> 6));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            if (out + 3u >= dst_cap) break;
            dst[out++] = (char)(0xE0u | (cp >> 12));
            dst[out++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            if (out + 4u >= dst_cap) break;
            dst[out++] = (char)(0xF0u | (cp >> 18));
            dst[out++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            dst[out++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    dst[out] = '\0';
    return out;
}

static inline bool dstorage_guid_eq(const scarlett_guid_t *a, const scarlett_guid_t *b) {
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
static inline int32_t dstorage_pe_unix_call(uint32_t opcode, void *params) {
    return (int32_t)WINE_UNIX_CALL(opcode, params);
}
static inline int32_t dstorage_pe_init_unix_bridge(void) {
    return (int32_t)__wine_init_unix_call();
}
#else
typedef uint64_t scarlett_wine_unixlib_handle_t;
typedef int32_t (SCARLETT_DSTORAGEXS_CALL *scarlett_wine_unix_dispatcher_t)(
    scarlett_wine_unixlib_handle_t handle,
    uint32_t code,
    void *args);

extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern scarlett_wine_unix_dispatcher_t __wine_unix_call_dispatcher;
extern int32_t __wine_init_unix_call(void);

static inline int32_t dstorage_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, opcode, params);
}

static inline int32_t dstorage_pe_init_unix_bridge(void) {
    return __wine_init_unix_call();
}
#endif

#else

static scarlett_unix_call_fn_t g_dstorage_unix_bridge = scarlett_dstorage_unix_call;

SCARLETT_DSTORAGEXS_API void scarlett_dstorage_set_unix_call_bridge(scarlett_unix_call_fn_t fn) {
    g_dstorage_unix_bridge = fn;
}

SCARLETT_DSTORAGEXS_API scarlett_unix_call_fn_t scarlett_dstorage_get_unix_call_bridge(void) {
    return g_dstorage_unix_bridge;
}

static inline int32_t dstorage_pe_unix_call(uint32_t opcode, void *params) {
    if (g_dstorage_unix_bridge == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return g_dstorage_unix_bridge(opcode, params);
}

#endif

/* ============================================================================
 * Diagnostic Auto-Stub Trampoline (Zero Silent Stubs)
 * ============================================================================ */

static uint32_t g_dstorage_stub_hit_counter = 0u;

static scarlett_hresult_t dstorage_pe_dispatch_stub(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_dstorage_report_stub_params_t params;
    dstorage_pe_zero(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    if (desc != NULL) {
        params.ctx.flags = desc->flags;
        params.ctx.ordinal = desc->ordinal;
        params.ctx.vtable_slot = desc->vtable_slot;
        params.ctx.return_hr = scarlett_stub_enforce_error(desc->default_error);
        dstorage_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), desc->dll_name);
        dstorage_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), desc->symbol_name);
    } else {
        params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
        params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
        params.ctx.return_hr = SCARLETT_ERR_UNIMPLEMENTED_STUB;
        dstorage_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), "dstorage_xs.dll");
        dstorage_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), "unknown_symbol");
    }

    params.ctx.return_address = return_address;
    params.ctx.caller_module_base = 0u;
    params.ctx.caller_rva = return_address;
    params.ctx.rcx = rcx;
    params.ctx.rdx = rdx;
    params.ctx.r8 = r8;
    params.ctx.r9 = r9;
    params.ctx.hit_count = __atomic_add_fetch(&g_dstorage_stub_hit_counter, 1u, __ATOMIC_RELAXED);
    dstorage_pe_copy_cstr(params.ctx.caller_module_name,
                          sizeof(params.ctx.caller_module_name),
                          "dstorage_xs_caller");

    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_REPORT_STUB, &params);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        return SCARLETT_ERR_STRICT_STUB_ABORT;
    }
    if (scarlett_failed(rc) && params.ctx.return_hr == SCARLETT_OK) {
        return SCARLETT_ERR_UNIMPLEMENTED_STUB;
    }
    return scarlett_stub_enforce_error(params.ctx.return_hr);
}

/* ============================================================================
 * Freestanding Object Pools & Initialization
 * ============================================================================ */

#define SCARLETT_DSTORAGE_QUEUE_POOL_CAP  64u
#define SCARLETT_DSTORAGE_FILE_POOL_CAP   256u
#define SCARLETT_DSTORAGE_STATUS_POOL_CAP 256u

static IDStorageFactoryX2 g_factory_singleton;
static IDStorageQueueX2 g_queue_pool[SCARLETT_DSTORAGE_QUEUE_POOL_CAP];
static IDStorageFileX g_file_pool[SCARLETT_DSTORAGE_FILE_POOL_CAP];
static IDStorageStatusArrayX g_status_pool[SCARLETT_DSTORAGE_STATUS_POOL_CAP];

static uint32_t g_unix_initialized = 0u;

static scarlett_hresult_t dstorage_pe_ensure_unix_init(void) {
    if (__atomic_load_n(&g_unix_initialized, __ATOMIC_ACQUIRE) != 0u) {
        return SCARLETT_OK;
    }
    scarlett_unix_init_params_t init_params;
    dstorage_pe_zero(&init_params, sizeof(init_params));
    init_params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    init_params.module_id = (uint32_t)SCARLETT_UNIX_MOD_DSTORAGE;
    init_params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
    init_params.shader_mode = (uint32_t)SCARLETT_SHADER_MODE_ADAPTIVE;
    init_params.net_mode = (uint32_t)SCARLETT_NET_MODE_OFFLINE;

    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_INIT, &init_params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (scarlett_failed(init_params.out_status)) {
        return init_params.out_status;
    }
    __atomic_store_n(&g_unix_initialized, 1u, __ATOMIC_RELEASE);
    return SCARLETT_OK;
}

/* ============================================================================
 * IDStorageFileX COM Implementation
 * ============================================================================ */

static const IDStorageFileXVtbl g_file_vtbl;

static IDStorageFileX *dstorage_pe_alloc_file(void) {
    for (uint32_t i = 0u; i < SCARLETT_DSTORAGE_FILE_POOL_CAP; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(&g_file_pool[i].in_use, &expected, 1u,
                                          false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            g_file_pool[i].lpVtbl = &g_file_vtbl;
            g_file_pool[i].unix_file_handle = 0u;
            g_file_pool[i].file_size_bytes = 0u;
            __atomic_store_n(&g_file_pool[i].ref_count, 1u, __ATOMIC_RELEASE);
            return &g_file_pool[i];
        }
    }
    return NULL;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL file_QueryInterface(
    IDStorageFileX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIFile = SCARLETT_IID_IDSTORAGEFILEX_INIT;
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_guid_eq(riid, &kIUnknown) || dstorage_guid_eq(riid, &kIFile)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL file_AddRef(IDStorageFileX *self) {
    if (self == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
}

static void SCARLETT_DSTORAGEXS_CALL file_Close(IDStorageFileX *self) {
    if (self == NULL || self->unix_file_handle == 0u) {
        return;
    }
    scarlett_dstorage_close_file_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.file_handle = self->unix_file_handle;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_CLOSE_FILE, &params);
    self->unix_file_handle = 0u;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL file_Release(IDStorageFileX *self) {
    if (self == NULL) {
        return 0u;
    }
    uint32_t refs = __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (refs == 0u) {
        file_Close(self);
        self->file_size_bytes = 0u;
        __atomic_store_n(&self->in_use, 0u, __ATOMIC_RELEASE);
    }
    return refs;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL file_GetFileInformation(
    IDStorageFileX *self,
    DSTORAGE_FILE_INFO *info) {
    if (self == NULL || info == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (self->unix_file_handle == 0u) {
        return SCARLETT_E_DSTORAGE_FILE_NOT_OPEN;
    }
    scarlett_dstorage_file_info_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.file_handle = self->unix_file_handle;
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_GET_FILE_INFO, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (scarlett_failed(params.out_status)) {
        return params.out_status;
    }
    info->FileSizeBytes = params.out_file_size_bytes;
    info->OpenFlags = params.out_open_flags;
    info->Reserved = 0u;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL file_GetFileHandle(
    IDStorageFileX *self,
    void **outHandle) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_XDL_VTABLE,
        SCARLETT_STUB_ORDINAL_NONE,
        5u,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "dstorage_xs.dll",
        "IDStorageFileX::GetFileHandle"
    };
    if (outHandle != NULL) {
        *outHandle = NULL;
    }
    return dstorage_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)outHandle,
        0u,
        0u);
}

static const IDStorageFileXVtbl g_file_vtbl = {
    file_QueryInterface,
    file_AddRef,
    file_Release,
    file_Close,
    file_GetFileInformation,
    file_GetFileHandle
};

/* ============================================================================
 * IDStorageStatusArrayX COM Implementation
 * ============================================================================ */

static const IDStorageStatusArrayXVtbl g_status_vtbl;

static IDStorageStatusArrayX *dstorage_pe_alloc_status(void) {
    for (uint32_t i = 0u; i < SCARLETT_DSTORAGE_STATUS_POOL_CAP; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(&g_status_pool[i].in_use, &expected, 1u,
                                          false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            g_status_pool[i].lpVtbl = &g_status_vtbl;
            g_status_pool[i].unix_status_handle = 0u;
            g_status_pool[i].capacity = 0u;
            __atomic_store_n(&g_status_pool[i].ref_count, 1u, __ATOMIC_RELEASE);
            return &g_status_pool[i];
        }
    }
    return NULL;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL status_QueryInterface(
    IDStorageStatusArrayX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIStatus = SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT;
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_guid_eq(riid, &kIUnknown) || dstorage_guid_eq(riid, &kIStatus)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL status_AddRef(IDStorageStatusArrayX *self) {
    if (self == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_DSTORAGEXS_CALL status_Release(IDStorageStatusArrayX *self) {
    if (self == NULL) {
        return 0u;
    }
    uint32_t refs = __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (refs == 0u) {
        if (self->unix_status_handle != 0u) {
            scarlett_dstorage_destroy_status_params_t params;
            dstorage_pe_zero(&params, sizeof(params));
            params.status_array_handle = self->unix_status_handle;
            (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_DESTROY_STATUS_ARRAY, &params);
            self->unix_status_handle = 0u;
        }
        self->capacity = 0u;
        __atomic_store_n(&self->in_use, 0u, __ATOMIC_RELEASE);
    }
    return refs;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL status_IsComplete(
    IDStorageStatusArrayX *self,
    uint32_t index) {
    if (self == NULL || self->unix_status_handle == 0u) {
        return 0u;
    }
    scarlett_dstorage_query_status_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.status_array_handle = self->unix_status_handle;
    params.index = index;
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_QUERY_STATUS, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return 0u;
    }
    return params.out_is_complete;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL status_GetHResult(
    IDStorageStatusArrayX *self,
    uint32_t index) {
    if (self == NULL || self->unix_status_handle == 0u) {
        return SCARLETT_E_POINTER;
    }
    scarlett_dstorage_query_status_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.status_array_handle = self->unix_status_handle;
    params.index = index;
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_QUERY_STATUS, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (scarlett_failed(params.out_status)) {
        return params.out_status;
    }
    return params.out_entry_hresult;
}

static const IDStorageStatusArrayXVtbl g_status_vtbl = {
    status_QueryInterface,
    status_AddRef,
    status_Release,
    status_IsComplete,
    status_GetHResult
};

/* ============================================================================
 * IDStorageQueueX / X1 / X2 COM Implementation
 * ============================================================================ */

static const IDStorageQueueX2Vtbl g_queue_vtbl;

static IDStorageQueueX2 *dstorage_pe_alloc_queue(void) {
    for (uint32_t i = 0u; i < SCARLETT_DSTORAGE_QUEUE_POOL_CAP; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(&g_queue_pool[i].in_use, &expected, 1u,
                                          false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            g_queue_pool[i].lpVtbl = &g_queue_vtbl;
            g_queue_pool[i].unix_queue_handle = 0u;
            g_queue_pool[i].capacity = 0u;
            g_queue_pool[i].priority = 0u;
            g_queue_pool[i].source_type = 0u;
            __atomic_store_n(&g_queue_pool[i].ref_count, 1u, __ATOMIC_RELEASE);
            return &g_queue_pool[i];
        }
    }
    return NULL;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL queue_QueryInterface(
    IDStorageQueueX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIQ0 = SCARLETT_IID_IDSTORAGEQUEUEX_INIT;
    static const scarlett_guid_t kIQ1 = SCARLETT_IID_IDSTORAGEQUEUEX1_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_guid_eq(riid, &kIUnknown) ||
        dstorage_guid_eq(riid, &kIQ0) ||
        dstorage_guid_eq(riid, &kIQ1) ||
        dstorage_guid_eq(riid, &kIQ2)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL queue_AddRef(IDStorageQueueX *self) {
    if (self == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
}

static void SCARLETT_DSTORAGEXS_CALL queue_Close(IDStorageQueueX *self) {
    if (self == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_destroy_queue_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_DESTROY_QUEUE, &params);
    self->unix_queue_handle = 0u;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL queue_Release(IDStorageQueueX *self) {
    if (self == NULL) {
        return 0u;
    }
    uint32_t refs = __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (refs == 0u) {
        queue_Close(self);
        self->capacity = 0u;
        self->priority = 0u;
        self->source_type = 0u;
        __atomic_store_n(&self->in_use, 0u, __ATOMIC_RELEASE);
    }
    return refs;
}

static void SCARLETT_DSTORAGEXS_CALL queue_EnqueueRequest(
    IDStorageQueueX *self,
    const DSTORAGE_REQUEST *request) {
    if (self == NULL || request == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_enqueue_request_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.file_handle = request->FileHandle;
    params.file_offset = request->FileOffset;
    params.src_addr = request->SourceAddress;
    params.dst_addr = request->DestinationAddress;
    params.cancellation_tag = request->CancellationTag;
    params.name_utf8_addr = (uint64_t)(uintptr_t)request->Name;
    params.options.raw = request->Options.AsUInt64;
    params.src_size = request->SourceSize;
    params.dst_size = request->DestinationSize;
    params.swizzle_subresource_index = request->SwizzleSubresourceIndex;
    params.swizzle_reserved = request->SwizzleReserved;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_ENQUEUE_REQUEST, &params);
}

static void SCARLETT_DSTORAGEXS_CALL queue_EnqueueStatus(
    IDStorageQueueX *self,
    IDStorageStatusArrayX *statusArray,
    uint32_t index) {
    if (self == NULL || statusArray == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_enqueue_status_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.status_array_handle = statusArray->unix_status_handle;
    params.index = index;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_ENQUEUE_STATUS, &params);
}

static void SCARLETT_DSTORAGEXS_CALL queue_EnqueueSignal(
    IDStorageQueueX *self,
    void *fence,
    uint64_t value) {
    if (self == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_enqueue_signal_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.fence_or_event_addr = (uint64_t)(uintptr_t)fence;
    params.value = value;
    params.is_event = 0u;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_ENQUEUE_SIGNAL, &params);
}

static void SCARLETT_DSTORAGEXS_CALL queue_Submit(IDStorageQueueX *self) {
    if (self == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_submit_queue_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.wait_for_idle = 0u;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_SUBMIT_QUEUE, &params);
}

static void SCARLETT_DSTORAGEXS_CALL queue_CancelRequestsWithTag(
    IDStorageQueueX *self,
    uint64_t mask,
    uint64_t value) {
    if (self == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_cancel_tag_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.mask = mask;
    params.value = value;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_CANCEL_TAG, &params);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL queue_Query(
    IDStorageQueueX *self,
    DSTORAGE_QUEUE_INFO *info) {
    if (self == NULL || info == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (self->unix_queue_handle == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_dstorage_query_queue_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_QUERY_QUEUE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (scarlett_failed(params.out_status)) {
        return params.out_status;
    }
    info->Desc.SourceType = params.out_source_type;
    info->Desc.Capacity = params.out_capacity;
    info->Desc.Priority = params.out_priority;
    info->Desc.Name = NULL;
    info->Desc.Device = NULL;
    info->EmptySlotCount = params.out_empty_slots;
    info->RequestCountUntilAutoSubmit = params.out_until_auto_submit;
    info->PendingRequestCount = params.out_pending_count;
    info->AutoSubmitCount = params.out_auto_submit_count;
    info->TotalSubmittedRequests = params.out_submitted_requests;
    info->TotalCompletedRequests = params.out_completed_requests;
    info->TotalCancelledRequests = params.out_cancelled_requests;
    return SCARLETT_OK;
}

static void SCARLETT_DSTORAGEXS_CALL queue_EnqueueSetEvent(
    IDStorageQueueX1 *self,
    void *eventHandle) {
    if (self == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    scarlett_dstorage_enqueue_signal_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.fence_or_event_addr = (uint64_t)(uintptr_t)eventHandle;
    params.value = 1u;
    params.is_event = 1u;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_ENQUEUE_SIGNAL, &params);
}

static void SCARLETT_DSTORAGEXS_CALL queue_RetrieveErrorRecord(
    IDStorageQueueX2 *self,
    DSTORAGE_ERROR_RECORD *record) {
    if (self == NULL || record == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    dstorage_pe_zero(record, sizeof(*record));
    scarlett_dstorage_error_record_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.out_record_addr = (uint64_t)(uintptr_t)record;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_RETRIEVE_ERROR_RECORD, &params);
}

static uint32_t SCARLETT_DSTORAGEXS_CALL queue_GetCompressionSupport(
    IDStorageQueueX2 *self,
    uint32_t format) {
    (void)self;
    scarlett_dstorage_caps_params_t caps;
    dstorage_pe_zero(&caps, sizeof(caps));
    if (scarlett_failed(dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_QUERY_CAPS, &caps)) ||
        scarlett_failed(caps.out_status)) {
        return 0u;
    }
    /* format 0=None, 1=GDeflate/Zlib, 2=BCPack */
    if (format == 1u) {
        return (caps.out_vk_memory_decompression != 0u || caps.out_vkdeflate_spirv_valid != 0u)
                   ? 2u /* GPU/Compute + CPU */
                   : 1u;
    }
    if (format == 2u) {
        return caps.out_bcpack_plugin_loaded != 0u ? 1u : 0u;
    }
    return 1u;
}

static const IDStorageQueueX2Vtbl g_queue_vtbl = {
    queue_QueryInterface,
    queue_AddRef,
    queue_Release,
    queue_EnqueueRequest,
    queue_EnqueueStatus,
    queue_EnqueueSignal,
    queue_Submit,
    queue_CancelRequestsWithTag,
    queue_Close,
    queue_Query,
    queue_EnqueueSetEvent,
    queue_RetrieveErrorRecord,
    queue_GetCompressionSupport
};

/* ============================================================================
 * IDStorageFactoryX / X1 / X2 COM Implementation
 * ============================================================================ */

static const IDStorageFactoryX2Vtbl g_factory_vtbl;

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_QueryInterface(
    IDStorageFactoryX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIF0 = SCARLETT_IID_IDSTORAGEFACTORYX_INIT;
    static const scarlett_guid_t kIF1 = SCARLETT_IID_IDSTORAGEFACTORYX1_INIT;
    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_guid_eq(riid, &kIUnknown) ||
        dstorage_guid_eq(riid, &kIF0) ||
        dstorage_guid_eq(riid, &kIF1) ||
        dstorage_guid_eq(riid, &kIF2)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_DSTORAGEXS_CALL factory_AddRef(IDStorageFactoryX *self) {
    if (self == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_DSTORAGEXS_CALL factory_Release(IDStorageFactoryX *self) {
    if (self == NULL) {
        return 0u;
    }
    uint32_t cur = __atomic_load_n(&self->ref_count, __ATOMIC_RELAXED);
    if (cur <= 1u) {
        return 1u;
    }
    return __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_CreateQueue(
    IDStorageFactoryX *self,
    const DSTORAGE_QUEUE_DESC *desc,
    const scarlett_guid_t *riid,
    void **ppv) {
    static const scarlett_guid_t kIQ0 = SCARLETT_IID_IDSTORAGEQUEUEX_INIT;
    static const scarlett_guid_t kIQ1 = SCARLETT_IID_IDSTORAGEQUEUEX1_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    (void)self;
    if (desc == NULL || riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;

    if (!dstorage_guid_eq(riid, &kIQ0) &&
        !dstorage_guid_eq(riid, &kIQ1) &&
        !dstorage_guid_eq(riid, &kIQ2)) {
        return SCARLETT_E_NOINTERFACE;
    }
    if (desc->Capacity < SCARLETT_DSTORAGE_MIN_QUEUE_CAPACITY ||
        desc->Capacity > SCARLETT_DSTORAGE_MAX_QUEUE_CAPACITY) {
        return SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY;
    }
    if (desc->Priority >= SCARLETT_DSTORAGE_PRIORITY_COUNT) {
        return SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY;
    }

    IDStorageQueueX2 *queue = dstorage_pe_alloc_queue();
    if (queue == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_dstorage_create_queue_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.source_type = desc->SourceType;
    params.capacity = desc->Capacity;
    params.priority = desc->Priority;
    params.name_utf8_addr = (uint64_t)(uintptr_t)desc->Name;
    params.name_utf8_len = dstorage_pe_strlen_bounded(desc->Name, 256u);

    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_CREATE_QUEUE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        __atomic_store_n(&queue->in_use, 0u, __ATOMIC_RELEASE);
        return scarlett_failed(rc) ? rc : params.out_status;
    }

    queue->unix_queue_handle = params.out_queue_handle;
    queue->capacity = desc->Capacity;
    queue->priority = desc->Priority;
    queue->source_type = desc->SourceType;
    *ppv = queue;
    return SCARLETT_OK;
}

static scarlett_hresult_t factory_open_file_common(
    const char *utf8_path,
    uint32_t utf8_len,
    const scarlett_guid_t *riid,
    void **ppv) {
    static const scarlett_guid_t kIFile = SCARLETT_IID_IDSTORAGEFILEX_INIT;
    if (utf8_path == NULL || riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;
    if (!dstorage_guid_eq(riid, &kIFile)) {
        return SCARLETT_E_NOINTERFACE;
    }
    if (utf8_len == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    IDStorageFileX *file = dstorage_pe_alloc_file();
    if (file == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_dstorage_open_file_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.path_utf8_addr = (uint64_t)(uintptr_t)utf8_path;
    params.path_utf8_len = utf8_len;
    params.open_flags = 0u;

    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_OPEN_FILE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        __atomic_store_n(&file->in_use, 0u, __ATOMIC_RELEASE);
        return scarlett_failed(rc) ? rc : params.out_status;
    }

    file->unix_file_handle = params.out_file_handle;
    file->file_size_bytes = params.out_file_size;
    *ppv = file;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_OpenFile(
    IDStorageFactoryX *self,
    const uint16_t *path,
    const scarlett_guid_t *riid,
    void **ppv) {
    (void)self;
    if (path == NULL || riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    char utf8[1024];
    uint32_t len = dstorage_pe_utf16_to_utf8(path, utf8, (uint32_t)sizeof(utf8));
    return factory_open_file_common(utf8, len, riid, ppv);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_OpenFileUtf8(
    IDStorageFactoryX1 *self,
    const char *path_utf8,
    const scarlett_guid_t *riid,
    void **ppv) {
    (void)self;
    if (path_utf8 == NULL) {
        return SCARLETT_E_POINTER;
    }
    uint32_t len = dstorage_pe_strlen_bounded(path_utf8, 4096u);
    return factory_open_file_common(path_utf8, len, riid, ppv);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_CreateStatusArray(
    IDStorageFactoryX *self,
    uint32_t capacity,
    const char *name,
    const scarlett_guid_t *riid,
    void **ppv) {
    static const scarlett_guid_t kIStatus = SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT;
    (void)self;
    (void)name;
    if (riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;
    if (!dstorage_guid_eq(riid, &kIStatus)) {
        return SCARLETT_E_NOINTERFACE;
    }
    if (capacity == 0u || capacity > SCARLETT_DSTORAGE_MAX_QUEUE_CAPACITY) {
        return SCARLETT_E_INVALIDARG;
    }

    IDStorageStatusArrayX *status = dstorage_pe_alloc_status();
    if (status == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_dstorage_create_status_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.capacity = capacity;
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_CREATE_STATUS_ARRAY, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        __atomic_store_n(&status->in_use, 0u, __ATOMIC_RELEASE);
        return scarlett_failed(rc) ? rc : params.out_status;
    }

    status->unix_status_handle = params.out_status_array_handle;
    status->capacity = capacity;
    *ppv = status;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_SetStagingBufferSize(
    IDStorageFactoryX *self,
    uint32_t size) {
    (void)self;
    scarlett_dstorage_staging_size_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.size_bytes = (uint64_t)size;
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_SET_STAGING_SIZE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

static uint64_t SCARLETT_DSTORAGEXS_CALL factory_GetStagingBufferSize(
    IDStorageFactoryX1 *self) {
    (void)self;
    scarlett_dstorage_caps_params_t caps;
    dstorage_pe_zero(&caps, sizeof(caps));
    if (scarlett_failed(dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_QUERY_CAPS, &caps)) ||
        scarlett_failed(caps.out_status)) {
        return 0u;
    }
    return caps.out_staging_buffer_bytes;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_QueryCapabilities(
    IDStorageFactoryX2 *self,
    scarlett_dstorage_caps_params_t *out_caps) {
    (void)self;
    if (out_caps == NULL) {
        return SCARLETT_E_POINTER;
    }
    dstorage_pe_zero(out_caps, sizeof(*out_caps));
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_QUERY_CAPS, out_caps);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return out_caps->out_status;
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_LoadBcpackPlugin(
    IDStorageFactoryX2 *self,
    const char *plugin_path_utf8,
    uint32_t *out_plugin_abi_version) {
    (void)self;
    scarlett_dstorage_bcpack_plugin_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.plugin_path_utf8_addr = (uint64_t)(uintptr_t)plugin_path_utf8;
    params.plugin_path_len = dstorage_pe_strlen_bounded(plugin_path_utf8, 4096u);
    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_LOAD_BCPACK_PLUGIN, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_plugin_abi_version != NULL) {
        *out_plugin_abi_version = params.out_plugin_abi_version;
    }
    return params.out_status;
}

static const IDStorageFactoryX2Vtbl g_factory_vtbl = {
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_CreateQueue,
    factory_OpenFile,
    factory_CreateStatusArray,
    factory_SetStagingBufferSize,
    factory_OpenFileUtf8,
    factory_GetStagingBufferSize,
    factory_QueryCapabilities,
    factory_LoadBcpackPlugin
};

/* ============================================================================
 * PE64 Exported Functions
 * ============================================================================ */

SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageGetFactory(
    const scarlett_guid_t *riid,
    void **ppv) {
    static const scarlett_guid_t kIF0 = SCARLETT_IID_IDSTORAGEFACTORYX_INIT;
    static const scarlett_guid_t kIF1 = SCARLETT_IID_IDSTORAGEFACTORYX1_INIT;
    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;

    if (riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;

    scarlett_hresult_t init_hr = dstorage_pe_ensure_unix_init();
    if (scarlett_failed(init_hr)) {
        return init_hr;
    }

    if (!dstorage_guid_eq(riid, &kIUnknown) &&
        !dstorage_guid_eq(riid, &kIF0) &&
        !dstorage_guid_eq(riid, &kIF1) &&
        !dstorage_guid_eq(riid, &kIF2)) {
        return SCARLETT_E_NOINTERFACE;
    }

    g_factory_singleton.lpVtbl = &g_factory_vtbl;
    if (__atomic_load_n(&g_factory_singleton.initialized, __ATOMIC_RELAXED) == 0u) {
        __atomic_store_n(&g_factory_singleton.ref_count, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&g_factory_singleton.initialized, 1u, __ATOMIC_RELEASE);
    }
    __atomic_add_fetch(&g_factory_singleton.ref_count, 1u, __ATOMIC_RELAXED);
    *ppv = &g_factory_singleton;
    return SCARLETT_OK;
}

SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageSetConfiguration(
    const void *configuration) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "dstorage_xs.dll",
        "DStorageSetConfiguration"
    };
    return dstorage_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)configuration,
        0u,
        0u,
        0u);
}

SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageCreateCompressionCodec(
    uint32_t format,
    uint32_t numThreads,
    const scarlett_guid_t *riid,
    void **ppv) {
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "dstorage_xs.dll",
        "DStorageCreateCompressionCodec"
    };
    if (ppv != NULL) {
        *ppv = NULL;
    }
    return dstorage_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)format,
        (uint64_t)numThreads,
        (uint64_t)(uintptr_t)riid,
        (uint64_t)(uintptr_t)ppv);
}

/* ============================================================================
 * PE64 DllMain Entry (Calls __wine_init_unix_call() Once on DLL_PROCESS_ATTACH)
 * ============================================================================ */

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_DSTORAGEXS_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == 1u) { /* DLL_PROCESS_ATTACH */
        if (dstorage_pe_init_unix_bridge() != 0) {
            return 0u;
        }
        if (scarlett_failed(dstorage_pe_ensure_unix_init())) {
            return 0u;
        }
    }
    return 1u;
}
#endif
