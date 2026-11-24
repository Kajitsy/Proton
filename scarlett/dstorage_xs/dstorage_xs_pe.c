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

static inline char *dstorage_pe_append_str(char *p, char *end, const char *s) {
    if (s == NULL) {
        s = "(null)";
    }
    while (*s != '\0' && p + 1 < end) {
        *p++ = *s++;
    }
    return p;
}

static inline char *dstorage_pe_append_hex64(char *p, char *end, uint64_t val) {
    static const char kHex[] = "0123456789abcdef";
    p = dstorage_pe_append_str(p, end, "0x");
    bool started = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint32_t nib = (uint32_t)((val >> shift) & 0xFu);
        if (nib != 0u || started || shift == 0) {
            started = true;
            if (p + 1 < end) {
                *p++ = kHex[nib];
            }
        }
    }
    return p;
}

#if defined(_WIN32) || defined(__MINGW32__)

/* ============================================================================
 * Freestanding PE64 PEB -> kernel32/kernelbase Export Resolver (Zero Imports)
 * ============================================================================ */

typedef struct scarlett_unicode_string_s {
    uint16_t Length;
    uint16_t MaximumLength;
    uint32_t Reserved;
    const uint16_t *Buffer;
} scarlett_unicode_string_t;

typedef struct scarlett_list_entry_s {
    struct scarlett_list_entry_s *Flink;
    struct scarlett_list_entry_s *Blink;
} scarlett_list_entry_t;

typedef void (SCARLETT_DSTORAGEXS_CALL *scarlett_ods_fn_t)(const char *lpOutputString);
typedef int32_t (SCARLETT_DSTORAGEXS_CALL *scarlett_set_event_fn_t)(void *hEvent);
typedef void *(SCARLETT_DSTORAGEXS_CALL *scarlett_create_thread_fn_t)(
    void *lpThreadAttributes,
    size_t dwStackSize,
    uint32_t (SCARLETT_DSTORAGEXS_CALL *lpStartAddress)(void *),
    void *lpParameter,
    uint32_t dwCreationFlags,
    uint32_t *lpThreadId);

typedef struct scarlett_d3d12_fence_vtbl_s {
    void *slots[10]; /* IUnknown(0..2), ID3D12Object(3..6), ID3D12DeviceChild(7), ID3D12Fence(8..9) */
    scarlett_hresult_t (SCARLETT_DSTORAGEXS_CALL *Signal)(void *self, uint64_t Value);
} scarlett_d3d12_fence_vtbl_t;

typedef struct scarlett_d3d12_fence_s {
    const scarlett_d3d12_fence_vtbl_t *lpVtbl;
} scarlett_d3d12_fence_t;

static scarlett_ods_fn_t g_pfn_OutputDebugStringA = NULL;
static scarlett_set_event_fn_t g_pfn_SetEvent = NULL;
static scarlett_create_thread_fn_t g_pfn_CreateThread = NULL;
static uint32_t g_k32_resolved = 0u;

static bool dstorage_pe_ascii_ieq(const char *a, const char *b) {
    if (a == NULL || b == NULL) return false;
    while (*a != '\0' && *b != '\0') {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static bool dstorage_pe_utf16_ascii_ieq(const uint16_t *w, uint16_t byte_len, const char *ascii) {
    uint32_t wchar_len = (uint32_t)(byte_len / 2u);
    uint32_t i = 0u;
    while (i < wchar_len && ascii[i] != '\0') {
        uint16_t wc = w[i];
        char cb = ascii[i];
        if (wc >= 'A' && wc <= 'Z') wc = (uint16_t)(wc + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (wc != (uint16_t)(uint8_t)cb) return false;
        ++i;
    }
    return i == wchar_len && ascii[i] == '\0';
}

static void *dstorage_pe_find_export(const uint8_t *base, const char *proc_name) {
    if (base == NULL || proc_name == NULL) return NULL;
    if (base[0] != 'M' || base[1] != 'Z') return NULL;
    uint32_t pe_off = *(const uint32_t *)(base + 0x3Cu);
    const uint8_t *nt = base + pe_off;
    if (nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0 || nt[3] != 0) return NULL;
    uint16_t magic = *(const uint16_t *)(nt + 0x18u);
    if (magic != 0x020Bu) return NULL; /* PE32+ */
    uint32_t exp_rva = *(const uint32_t *)(nt + 0x88u);
    uint32_t exp_size = *(const uint32_t *)(nt + 0x8Cu);
    if (exp_rva == 0u || exp_size == 0u) return NULL;

    const uint8_t *exp_dir = base + exp_rva;
    uint32_t num_names = *(const uint32_t *)(exp_dir + 0x18u);
    uint32_t addr_funcs_rva = *(const uint32_t *)(exp_dir + 0x1Cu);
    uint32_t addr_names_rva = *(const uint32_t *)(exp_dir + 0x20u);
    uint32_t addr_ords_rva = *(const uint32_t *)(exp_dir + 0x24u);

    const uint32_t *names = (const uint32_t *)(base + addr_names_rva);
    const uint16_t *ords = (const uint16_t *)(base + addr_ords_rva);
    const uint32_t *funcs = (const uint32_t *)(base + addr_funcs_rva);

    for (uint32_t i = 0u; i < num_names; ++i) {
        const char *name = (const char *)(base + names[i]);
        if (dstorage_pe_ascii_ieq(name, proc_name)) {
            uint32_t fn_rva = funcs[ords[i]];
            if (fn_rva >= exp_rva && fn_rva < exp_rva + exp_size) {
                return NULL; /* Forwarder */
            }
            return (void *)(uintptr_t)(base + fn_rva);
        }
    }
    return NULL;
}

static void dstorage_pe_resolve_kernel32(void) {
    if (__atomic_load_n(&g_k32_resolved, __ATOMIC_ACQUIRE) != 0u) {
        return;
    }
    const uint8_t *peb = NULL;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (peb == NULL) {
        return;
    }
    const uint8_t *ldr = *(const uint8_t *const *)(peb + 0x18u);
    if (ldr == NULL) {
        return;
    }
    const scarlett_list_entry_t *head = (const scarlett_list_entry_t *)(ldr + 0x20u);
    const uint8_t *k32_base = NULL;
    const uint8_t *kbase_base = NULL;

    for (const scarlett_list_entry_t *cur = head->Flink;
         cur != NULL && cur != head;
         cur = cur->Flink) {
        const uint8_t *entry = (const uint8_t *)cur;
        const uint8_t *dll_base = *(const uint8_t *const *)(entry + 0x20u);
        const scarlett_unicode_string_t *base_name =
            (const scarlett_unicode_string_t *)(entry + 0x48u);
        if (dll_base != NULL && base_name->Buffer != NULL) {
            if (dstorage_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernel32.dll")) {
                k32_base = dll_base;
            } else if (dstorage_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernelbase.dll")) {
                kbase_base = dll_base;
            }
        }
    }

    void *ods = dstorage_pe_find_export(k32_base, "OutputDebugStringA");
    if (ods == NULL) ods = dstorage_pe_find_export(kbase_base, "OutputDebugStringA");
    void *sev = dstorage_pe_find_export(k32_base, "SetEvent");
    if (sev == NULL) sev = dstorage_pe_find_export(kbase_base, "SetEvent");
    void *cth = dstorage_pe_find_export(k32_base, "CreateThread");
    if (cth == NULL) cth = dstorage_pe_find_export(kbase_base, "CreateThread");

    __atomic_store_n(&g_pfn_OutputDebugStringA, (scarlett_ods_fn_t)ods, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_SetEvent, (scarlett_set_event_fn_t)sev, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_CreateThread, (scarlett_create_thread_fn_t)cth, __ATOMIC_RELEASE);
    __atomic_store_n(&g_k32_resolved, 1u, __ATOMIC_RELEASE);
}

static inline char *dstorage_pe_append_hex_padded(
    char *p, char *end, uint64_t val, int digits) {
    static const char kHex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; --i) {
        if (p + 1 < end) {
            *p++ = kHex[(val >> (i * 4)) & 0xFu];
        }
    }
    return p;
}

static void dstorage_pe_ods(const char *msg) {
    dstorage_pe_resolve_kernel32();
    scarlett_ods_fn_t ods = __atomic_load_n(&g_pfn_OutputDebugStringA, __ATOMIC_ACQUIRE);
    if (ods != NULL && msg != NULL) {
        ods(msg);
    }
}

static void dstorage_pe_log_hr(const char *fn_name, const char *detail, scarlett_hresult_t hr) {
    char buf[512];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = dstorage_pe_append_str(p, end, "[scarlett:dstorage_xs] ");
    p = dstorage_pe_append_str(p, end, fn_name);
    if (detail != NULL) {
        p = dstorage_pe_append_str(p, end, " ");
        p = dstorage_pe_append_str(p, end, detail);
    }
    p = dstorage_pe_append_str(p, end, " hr=0x");
    p = dstorage_pe_append_hex_padded(p, end, (uint32_t)hr, 8);
    if (p + 1 < end) *p++ = '\n';
    *p = '\0';
    dstorage_pe_ods(buf);
}

static void dstorage_pe_log_unknown_iid(const char *where, const scarlett_guid_t *riid) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = dstorage_pe_append_str(p, end, "[scarlett:dstorage_xs] E_NOINTERFACE in ");
    p = dstorage_pe_append_str(p, end, where);
    p = dstorage_pe_append_str(p, end, " riid={");
    p = dstorage_pe_append_hex_padded(p, end, riid->data1, 8);
    if (p + 1 < end) *p++ = '-';
    p = dstorage_pe_append_hex_padded(p, end, riid->data2, 4);
    if (p + 1 < end) *p++ = '-';
    p = dstorage_pe_append_hex_padded(p, end, riid->data3, 4);
    if (p + 1 < end) *p++ = '-';
    p = dstorage_pe_append_hex_padded(p, end, riid->data4[0], 2);
    p = dstorage_pe_append_hex_padded(p, end, riid->data4[1], 2);
    if (p + 1 < end) *p++ = '-';
    for (int i = 2; i < 8; ++i) {
        p = dstorage_pe_append_hex_padded(p, end, riid->data4[i], 2);
    }
    p = dstorage_pe_append_str(p, end, "}\n");
    *p = '\0';
    dstorage_pe_ods(buf);
}

static void dstorage_pe_log_trace(
    const char *op,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    uint64_t a3) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = dstorage_pe_append_str(p, end, "[scarlett:dstorage_xs] ");
    p = dstorage_pe_append_str(p, end, op);
    p = dstorage_pe_append_str(p, end, " a0=");
    p = dstorage_pe_append_hex64(p, end, a0);
    p = dstorage_pe_append_str(p, end, " a1=");
    p = dstorage_pe_append_hex64(p, end, a1);
    p = dstorage_pe_append_str(p, end, " a2=");
    p = dstorage_pe_append_hex64(p, end, a2);
    p = dstorage_pe_append_str(p, end, " a3=");
    p = dstorage_pe_append_hex64(p, end, a3);
    p = dstorage_pe_append_str(p, end, "\n");
    *p = '\0';
    dstorage_pe_ods(buf);
}

#else

static inline void dstorage_pe_log_hr(
    const char *fn_name, const char *detail, scarlett_hresult_t hr) {
    (void)fn_name;
    (void)detail;
    (void)hr;
}

static inline void dstorage_pe_log_unknown_iid(
    const char *where, const scarlett_guid_t *riid) {
    (void)where;
    (void)riid;
}

static inline void dstorage_pe_log_trace(
    const char *op, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    (void)op;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
}

#endif /* _WIN32 || __MINGW32__ */

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

#if defined(_WIN32) || defined(__MINGW32__)
    dstorage_pe_resolve_kernel32();
    scarlett_ods_fn_t ods = __atomic_load_n(&g_pfn_OutputDebugStringA, __ATOMIC_ACQUIRE);
    if (ods != NULL) {
        char log_buf[384];
        char *p = log_buf;
        char *end = log_buf + sizeof(log_buf);
        p = dstorage_pe_append_str(p, end, "[scarlett:dstorage_xs:stub] dll=");
        p = dstorage_pe_append_str(p, end, params.ctx.dll_name);
        p = dstorage_pe_append_str(p, end, " fn=");
        p = dstorage_pe_append_str(p, end, params.ctx.symbol_name);
        p = dstorage_pe_append_str(p, end, " caller=");
        p = dstorage_pe_append_hex64(p, end, return_address);
        p = dstorage_pe_append_str(p, end, " rcx=");
        p = dstorage_pe_append_hex64(p, end, rcx);
        p = dstorage_pe_append_str(p, end, " rdx=");
        p = dstorage_pe_append_hex64(p, end, rdx);
        p = dstorage_pe_append_str(p, end, " r8=");
        p = dstorage_pe_append_hex64(p, end, r8);
        p = dstorage_pe_append_str(p, end, " r9=");
        p = dstorage_pe_append_hex64(p, end, r9);
        if (p + 1 < end) {
            *p++ = '\n';
        }
        *p = '\0';
        ods(log_buf);
    }
#endif

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

#if defined(_WIN32) || defined(__MINGW32__)
static uint32_t g_pe_signal_thread_started = 0u;

static void dstorage_pe_dispatch_signal_batch(
    const scarlett_dstorage_wait_signals_params_t *sp) {
    scarlett_set_event_fn_t set_event_fn = __atomic_load_n(&g_pfn_SetEvent, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0u; i < sp->out_count; ++i) {
        const scarlett_dstorage_pe_signal_item_t *item = &sp->items[i];
        if (item->target_addr == 0u) {
            continue;
        }
        if (item->mode == SCARLETT_DSTORAGE_SIGNAL_MODE_PE_FENCE) {
            scarlett_d3d12_fence_t *fence =
                (scarlett_d3d12_fence_t *)(uintptr_t)item->target_addr;
            if (fence->lpVtbl != NULL && fence->lpVtbl->Signal != NULL) {
                (void)fence->lpVtbl->Signal(fence, item->value);
            }
        } else if (item->mode == SCARLETT_DSTORAGE_SIGNAL_MODE_PE_EVENT) {
            if (set_event_fn != NULL) {
                (void)set_event_fn((void *)(uintptr_t)item->target_addr);
            }
        }
    }
}

static uint32_t SCARLETT_DSTORAGEXS_CALL dstorage_pe_signal_worker(void *arg) {
    (void)arg;
    for (;;) {
        scarlett_dstorage_wait_signals_params_t sp;
        dstorage_pe_zero(&sp, sizeof(sp));
        sp.blocking = 1u;
        int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_WAIT_SIGNALS, &sp);
        if (scarlett_failed(rc)) {
            break;
        }
        if (sp.out_count > 0u) {
            dstorage_pe_dispatch_signal_batch(&sp);
        }
        if (sp.out_stop != 0u) {
            break;
        }
    }
    return 0u;
}

static void dstorage_pe_ensure_signal_worker(void) {
    if (__atomic_load_n(&g_pe_signal_thread_started, __ATOMIC_ACQUIRE) != 0u) {
        return;
    }
    dstorage_pe_resolve_kernel32();
    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(&g_pe_signal_thread_started, &expected, 1u,
                                      false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        scarlett_create_thread_fn_t create_thread =
            __atomic_load_n(&g_pfn_CreateThread, __ATOMIC_ACQUIRE);
        if (create_thread != NULL) {
            void *th = create_thread(NULL, 0u, dstorage_pe_signal_worker, NULL, 0u, NULL);
            if (th == NULL) {
                __atomic_store_n(&g_pe_signal_thread_started, 0u, __ATOMIC_RELEASE);
            }
        } else {
            __atomic_store_n(&g_pe_signal_thread_started, 0u, __ATOMIC_RELEASE);
        }
    }
}

static void dstorage_pe_drain_signals_nonblocking(void) {
    scarlett_dstorage_wait_signals_params_t sp;
    for (;;) {
        dstorage_pe_zero(&sp, sizeof(sp));
        sp.blocking = 0u;
        int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_WAIT_SIGNALS, &sp);
        if (scarlett_failed(rc) || sp.out_count == 0u) {
            break;
        }
        dstorage_pe_dispatch_signal_batch(&sp);
    }
}
#endif

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

static bool dstorage_is_file_iid(const scarlett_guid_t *riid) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIFile = SCARLETT_IID_IDSTORAGEFILEX_INIT;
    static const scarlett_guid_t kIFileXbox = SCARLETT_IID_IDSTORAGEFILEX_XBOX_INIT;
    return dstorage_guid_eq(riid, &kIUnknown) ||
           dstorage_guid_eq(riid, &kIFile) ||
           dstorage_guid_eq(riid, &kIFileXbox);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL file_QueryInterface(
    IDStorageFileX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_is_file_iid(riid)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    dstorage_pe_log_unknown_iid("IDStorageFileX::QueryInterface", riid);
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

static bool dstorage_is_status_iid(const scarlett_guid_t *riid) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIStatus = SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT;
    static const scarlett_guid_t kIStatusXbox = SCARLETT_IID_IDSTORAGESTATUSARRAYX_XBOX_INIT;
    return dstorage_guid_eq(riid, &kIUnknown) ||
           dstorage_guid_eq(riid, &kIStatus) ||
           dstorage_guid_eq(riid, &kIStatusXbox);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL status_QueryInterface(
    IDStorageStatusArrayX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_is_status_iid(riid)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    dstorage_pe_log_unknown_iid("IDStorageStatusArrayX::QueryInterface", riid);
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
            g_queue_pool[i].device = NULL;
            g_queue_pool[i].name[0] = '\0';
            __atomic_store_n(&g_queue_pool[i].ref_count, 1u, __ATOMIC_RELEASE);
            return &g_queue_pool[i];
        }
    }
    return NULL;
}

static bool dstorage_is_queue_iid(const scarlett_guid_t *riid) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIQ = SCARLETT_IID_IDSTORAGEQUEUEX_INIT;
    static const scarlett_guid_t kIQ1 = SCARLETT_IID_IDSTORAGEQUEUEX1_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    static const scarlett_guid_t kIQXbox = SCARLETT_IID_IDSTORAGEQUEUEX_XBOX_INIT;
    return dstorage_guid_eq(riid, &kIUnknown) ||
           dstorage_guid_eq(riid, &kIQ) ||
           dstorage_guid_eq(riid, &kIQ1) ||
           dstorage_guid_eq(riid, &kIQ2) ||
           dstorage_guid_eq(riid, &kIQXbox);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL queue_QueryInterface(
    IDStorageQueueX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_is_queue_iid(riid)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    dstorage_pe_log_unknown_iid("IDStorageQueueX::QueryInterface", riid);
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

#if defined(_WIN32) || defined(__MINGW32__)
/*
 * Exact Xbox Scarlett GDK DSTORAGE_REQUEST layout (72 bytes, Destination-before-Source):
 *   +0x00: Options (8)
 *   +0x08: DestinationAddress (8)
 *   +0x10: DestinationReserved (8)
 *   +0x18: DestinationSize (4)
 *   +0x1C: DestinationReserved2 (4)
 *   +0x20: Source (IDStorageFileX* or SourceAddress) (8)
 *   +0x28: FileOffset (8)
 *   +0x30: SourceSize (4)
 *   +0x34: SwizzleSubresourceIndex / IntermediateSize (4)
 *   +0x38: CancellationTag (8)
 *   +0x40: Name (8)
 */
typedef struct scarlett_xbox_dstorage_request_s {
    DSTORAGE_REQUEST_OPTIONS Options;
    uint64_t DestinationAddress;
    uint64_t DestinationReserved;
    uint32_t DestinationSize;
    uint32_t DestinationReserved2;
    uint64_t Source;
    uint64_t FileOffset;
    uint32_t SourceSize;
    uint32_t SwizzleSubresourceIndex;
    uint64_t CancellationTag;
    const char *Name;
} scarlett_xbox_dstorage_request_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xbox_dstorage_request_t) == 72,
                       "Xbox Scarlett DSTORAGE_REQUEST must be 72 bytes");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, DestinationAddress) == 0x08,
                       "Xbox DestinationAddress offset");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, DestinationSize) == 0x18,
                       "Xbox DestinationSize offset");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, Source) == 0x20,
                       "Xbox Source offset");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, FileOffset) == 0x28,
                       "Xbox FileOffset offset");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, SourceSize) == 0x30,
                       "Xbox SourceSize offset");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, CancellationTag) == 0x38,
                       "Xbox CancellationTag offset");
SCARLETT_STATIC_ASSERT(offsetof(scarlett_xbox_dstorage_request_t, Name) == 0x40,
                       "Xbox Name offset");
#endif

static uint64_t dstorage_pe_unwrap_file_handle(uint64_t raw_handle) {
    if (raw_handle == 0u) {
        return 0u;
    }
    uintptr_t fh_ptr = (uintptr_t)raw_handle;
    uintptr_t pool_start = (uintptr_t)&g_file_pool[0];
    uintptr_t pool_end = (uintptr_t)&g_file_pool[SCARLETT_DSTORAGE_FILE_POOL_CAP];
    if (fh_ptr >= pool_start && fh_ptr < pool_end &&
        ((fh_ptr - pool_start) % sizeof(IDStorageFileX)) == 0u) {
        const IDStorageFileX *f = (const IDStorageFileX *)fh_ptr;
        if (__atomic_load_n(&f->in_use, __ATOMIC_ACQUIRE) != 0u &&
            f->lpVtbl == &g_file_vtbl) {
            return f->unix_file_handle;
        }
    }
    return raw_handle;
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

#if defined(_WIN32) || defined(__MINGW32__)
    const scarlett_xbox_dstorage_request_t *xreq =
        (const scarlett_xbox_dstorage_request_t *)(const void *)request;
    params.options.raw = xreq->Options.AsUInt64;
    params.dst_addr = xreq->DestinationAddress;
    params.dst_size = xreq->DestinationSize;
    if (xreq->Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE) {
        params.file_handle = dstorage_pe_unwrap_file_handle(xreq->Source);
        params.file_offset = xreq->FileOffset;
        params.src_addr = 0u;
    } else {
        params.file_handle = 0u;
        params.file_offset = 0u;
        params.src_addr = xreq->Source;
    }
    params.src_size = xreq->SourceSize;
    params.swizzle_subresource_index = xreq->SwizzleSubresourceIndex;
    params.cancellation_tag = xreq->CancellationTag;
    params.name_utf8_addr = (uint64_t)(uintptr_t)xreq->Name;
#else
    params.file_handle = (request->Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE)
                             ? dstorage_pe_unwrap_file_handle(request->FileHandle)
                             : request->FileHandle;
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
#endif
    {
        static uint32_t g_ds_req_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_ds_req_seq, 1u, __ATOMIC_RELAXED);
        if (params.dst_size >= (1024u * 1024u) || seq <= 8u || (seq % 200u) == 0u) {
            dstorage_pe_log_trace(
                "EnqueueRequest",
                params.dst_addr,
                (uint64_t)params.dst_size,
                params.file_offset,
                (uint64_t)seq);
        }
    }
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
#if defined(_WIN32) || defined(__MINGW32__)
    dstorage_pe_ensure_signal_worker();
    params.is_event = SCARLETT_DSTORAGE_SIGNAL_MODE_PE_FENCE;
#else
    params.is_event = SCARLETT_DSTORAGE_SIGNAL_MODE_HOST_FENCE;
#endif
    {
        static uint32_t g_ds_sig_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_ds_sig_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 12u || (seq % 100u) == 0u) {
            dstorage_pe_log_trace(
                "EnqueueSignal",
                (uint64_t)(uintptr_t)fence,
                value,
                self->unix_queue_handle,
                (uint64_t)seq);
        }
    }
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_ENQUEUE_SIGNAL, &params);
}

static void SCARLETT_DSTORAGEXS_CALL queue_Submit(IDStorageQueueX *self) {
    if (self == NULL || self->unix_queue_handle == 0u) {
        return;
    }
    {
        static uint32_t g_ds_sub_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_ds_sub_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 8u || (seq % 200u) == 0u) {
            dstorage_pe_log_trace(
                "Submit",
                (uint64_t)(uintptr_t)self,
                self->unix_queue_handle,
                (uint64_t)seq,
                0u);
        }
    }
    scarlett_dstorage_submit_queue_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.queue_handle = self->unix_queue_handle;
    params.wait_for_idle = 0u;
    (void)dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_SUBMIT_QUEUE, &params);
#if defined(_WIN32) || defined(__MINGW32__)
    if (__atomic_load_n(&g_pe_signal_thread_started, __ATOMIC_ACQUIRE) == 0u) {
        dstorage_pe_drain_signals_nonblocking();
    }
#endif
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
    const IDStorageQueueX2 *q2 = (const IDStorageQueueX2 *)self;
    info->Desc.SourceType = params.out_source_type;
    info->Desc.Capacity = (uint16_t)params.out_capacity;
    info->Desc.Priority = (int8_t)params.out_priority;
    dstorage_pe_zero(info->Desc.Reserved, sizeof(info->Desc.Reserved));
    info->Desc.Name = (q2->name[0] != '\0') ? q2->name : NULL;
    info->Desc.Device = q2->device;
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
#if defined(_WIN32) || defined(__MINGW32__)
    dstorage_pe_ensure_signal_worker();
    params.is_event = SCARLETT_DSTORAGE_SIGNAL_MODE_PE_EVENT;
#else
    params.is_event = SCARLETT_DSTORAGE_SIGNAL_MODE_HOST_EVENT;
#endif
    {
        static uint32_t g_ds_evt_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_ds_evt_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 12u || (seq % 100u) == 0u) {
            dstorage_pe_log_trace(
                "EnqueueSetEvent",
                (uint64_t)(uintptr_t)eventHandle,
                self->unix_queue_handle,
                (uint64_t)seq,
                0u);
        }
    }
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

static bool dstorage_is_factory_iid(const scarlett_guid_t *riid) {
    static const scarlett_guid_t kIUnknown = SCARLETT_IID_IUNKNOWN_INIT;
    static const scarlett_guid_t kIFacXbox = SCARLETT_IID_IDSTORAGEFACTORY_XBOX_INIT;
    static const scarlett_guid_t kIFacX = SCARLETT_IID_IDSTORAGEFACTORYX_INIT;
    static const scarlett_guid_t kIFacX1 = SCARLETT_IID_IDSTORAGEFACTORYX1_INIT;
    static const scarlett_guid_t kIFacX2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    return dstorage_guid_eq(riid, &kIUnknown) ||
           dstorage_guid_eq(riid, &kIFacXbox) ||
           dstorage_guid_eq(riid, &kIFacX) ||
           dstorage_guid_eq(riid, &kIFacX1) ||
           dstorage_guid_eq(riid, &kIFacX2);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_QueryInterface(
    IDStorageFactoryX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || riid == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (dstorage_is_factory_iid(riid)) {
        self->lpVtbl->AddRef(self);
        *ppvObject = self;
        return SCARLETT_OK;
    }
    dstorage_pe_log_unknown_iid("IDStorageFactoryX::QueryInterface", riid);
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
    (void)self;
    if (desc == NULL || riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;
    if (!dstorage_is_queue_iid(riid)) {
        dstorage_pe_log_unknown_iid("IDStorageFactoryX::CreateQueue", riid);
        return SCARLETT_E_NOINTERFACE;
    }

    if ((uint32_t)desc->Capacity < SCARLETT_DSTORAGE_MIN_QUEUE_CAPACITY ||
        (uint32_t)desc->Capacity > SCARLETT_DSTORAGE_MAX_QUEUE_CAPACITY) {
        dstorage_pe_log_hr("CreateQueue", desc->Name, SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY);
        return SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY;
    }
    int32_t raw_prio = (int32_t)desc->Priority;
    uint32_t prio = (raw_prio == -1) ? (uint32_t)DSTORAGE_PRIORITY_LOW : (uint32_t)raw_prio;
    if (raw_prio < -1 || prio >= SCARLETT_DSTORAGE_PRIORITY_COUNT) {
        dstorage_pe_log_hr("CreateQueue", desc->Name, SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY);
        return SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY;
    }

    IDStorageQueueX2 *queue = dstorage_pe_alloc_queue();
    if (queue == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_dstorage_create_queue_params_t params;
    dstorage_pe_zero(&params, sizeof(params));
    params.source_type = desc->SourceType;
    params.capacity = (uint32_t)desc->Capacity;
    params.priority = prio;
    params.name_utf8_addr = (uint64_t)(uintptr_t)desc->Name;
    params.name_utf8_len = dstorage_pe_strlen_bounded(desc->Name, 256u);

    int32_t rc = dstorage_pe_unix_call(SCARLETT_DSTORAGE_UNIX_CREATE_QUEUE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        __atomic_store_n(&queue->in_use, 0u, __ATOMIC_RELEASE);
        return scarlett_failed(rc) ? rc : params.out_status;
    }

    queue->unix_queue_handle = params.out_queue_handle;
    queue->capacity = (uint32_t)desc->Capacity;
    queue->priority = prio;
    queue->source_type = desc->SourceType;
    queue->device = desc->Device;
    dstorage_pe_copy_cstr(queue->name, sizeof(queue->name), desc->Name);
    *ppv = queue;
    dstorage_pe_log_hr("CreateQueue", desc->Name, SCARLETT_OK);
    return SCARLETT_OK;
}

static scarlett_hresult_t factory_open_file_common(
    const char *utf8_path,
    uint32_t utf8_len,
    const scarlett_guid_t *riid,
    void **ppv) {
    if (utf8_path == NULL || riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;
    if (!dstorage_is_file_iid(riid)) {
        dstorage_pe_log_unknown_iid("IDStorageFactoryX::OpenFile", riid);
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
        scarlett_hresult_t err = scarlett_failed(rc) ? rc : params.out_status;
        __atomic_store_n(&file->in_use, 0u, __ATOMIC_RELEASE);
        dstorage_pe_log_hr("OpenFile", utf8_path, err);
        return err;
    }

    file->unix_file_handle = params.out_file_handle;
    file->file_size_bytes = params.out_file_size;
    *ppv = file;
    dstorage_pe_log_hr("OpenFile", utf8_path, SCARLETT_OK);
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
    char utf8[2048];
    uint32_t len = dstorage_pe_utf16_to_utf8(path, utf8, (uint32_t)sizeof(utf8));
    return factory_open_file_common(utf8, len, riid, ppv);
}

static scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL factory_OpenFileUtf8(
    IDStorageFactoryX *self,
    const char *path_utf8,
    const scarlett_guid_t *riid,
    void **ppv) {
    (void)self;
    if (path_utf8 == NULL || riid == NULL || ppv == NULL) {
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
    (void)self;
    if (riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;
    if (!dstorage_is_status_iid(riid)) {
        dstorage_pe_log_unknown_iid("IDStorageFactoryX::CreateStatusArray", riid);
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
        scarlett_hresult_t err = scarlett_failed(rc) ? rc : params.out_status;
        __atomic_store_n(&status->in_use, 0u, __ATOMIC_RELEASE);
        dstorage_pe_log_hr("CreateStatusArray", name, err);
        return err;
    }

    status->unix_status_handle = params.out_status_array_handle;
    status->capacity = capacity;
    *ppv = status;
    dstorage_pe_log_hr("CreateStatusArray", name, SCARLETT_OK);
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
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : params.out_status;
    dstorage_pe_log_hr("SetStagingBufferSize", NULL, hr);
    return hr;
}

static uint64_t SCARLETT_DSTORAGEXS_CALL factory_GetStagingBufferSize(
    IDStorageFactoryX *self) {
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
    IDStorageFactoryX *self,
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
    IDStorageFactoryX *self,
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
    factory_QueryInterface,           /* 0 (0x00) */
    factory_AddRef,                   /* 1 (0x08) */
    factory_Release,                  /* 2 (0x10) */
    factory_CreateQueue,              /* 3 (0x18) */
    factory_OpenFile,                 /* 4 (0x20) */
    factory_CreateStatusArray,        /* 5 (0x28) */
    factory_OpenFileUtf8,             /* 6 (0x30) */
    factory_GetStagingBufferSize,     /* 7 (0x38) */
    factory_SetStagingBufferSize,     /* 8 (0x40) - matches Xbox Scarlett IDStorageFactoryX slot 8 */
    factory_QueryCapabilities,        /* 9 (0x48) */
    factory_CreateQueue,              /* 10 (0x50) - matches Xbox Scarlett CreateQueueX */
    factory_LoadBcpackPlugin          /* 11 (0x58) */
};

/* ============================================================================
 * PE64 Exported Functions
 * ============================================================================ */

SCARLETT_DSTORAGEXS_API scarlett_hresult_t SCARLETT_DSTORAGEXS_CALL DStorageGetFactory(
    const scarlett_guid_t *riid,
    void **ppv) {
    if (riid == NULL || ppv == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppv = NULL;
    if (!dstorage_is_factory_iid(riid)) {
        dstorage_pe_log_unknown_iid("DStorageGetFactory", riid);
        return SCARLETT_E_NOINTERFACE;
    }

    scarlett_hresult_t init_hr = dstorage_pe_ensure_unix_init();
    if (scarlett_failed(init_hr)) {
        dstorage_pe_log_hr("DStorageGetFactory", "unix_init", init_hr);
        return init_hr;
    }

    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(&g_factory_singleton.initialized, &expected, 1u,
                                      false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        g_factory_singleton.lpVtbl = &g_factory_vtbl;
        __atomic_store_n(&g_factory_singleton.ref_count, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&g_factory_singleton.initialized, 2u, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&g_factory_singleton.initialized, __ATOMIC_ACQUIRE) != 2u) {
            /* spin until singleton initialization completes */
        }
    }
    __atomic_add_fetch(&g_factory_singleton.ref_count, 1u, __ATOMIC_RELAXED);
    *ppv = &g_factory_singleton;
    dstorage_pe_log_hr("DStorageGetFactory", NULL, SCARLETT_OK);
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
