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

static inline void d3d12xs_pe_memcpy(void *dst, const void *src, size_t n) {
    if (dst == NULL || src == NULL || n == 0u || dst == src) {
        return;
    }
    uintptr_t d_addr = (uintptr_t)dst;
    uintptr_t s_addr = (uintptr_t)src;
    size_t i = 0u;
    if (((d_addr | s_addr) & 7u) == 0u) {
        volatile uint64_t *d64 = (volatile uint64_t *)dst;
        const volatile uint64_t *s64 = (const volatile uint64_t *)src;
        size_t n64 = n >> 3u;
        for (size_t k = 0u; k < n64; ++k) {
            d64[k] = s64[k];
        }
        i = n64 << 3u;
    }
    volatile uint8_t *d8 = (volatile uint8_t *)dst;
    const volatile uint8_t *s8 = (const volatile uint8_t *)src;
    for (; i < n; ++i) {
        d8[i] = s8[i];
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
 * PEB-Resolved kernel32.dll Exports & OutputDebugStringA (Zero __imp_* Imports)
 * ============================================================================ */

#if defined(_WIN32) || defined(__MINGW32__)

typedef void (SCARLETT_D3D12XS_CALL *scarlett_ods_fn_t)(const char *lpOutputString);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_load_library_a_fn_t)(const char *lpLibFileName);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_get_proc_address_fn_t)(void *hModule, const char *lpProcName);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_get_module_handle_a_fn_t)(const char *lpModuleName);
typedef int32_t (SCARLETT_D3D12XS_CALL *scarlett_set_event_fn_t)(void *hEvent);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_virtual_alloc_fn_t)(
    void *lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect);
typedef int32_t (SCARLETT_D3D12XS_CALL *scarlett_virtual_free_fn_t)(
    void *lpAddress, size_t dwSize, uint32_t dwFreeType);
typedef int32_t (SCARLETT_D3D12XS_CALL *scarlett_qpc_fn_t)(uint64_t *lpPerformanceCount);
typedef size_t (SCARLETT_D3D12XS_CALL *scarlett_virtual_query_fn_t)(
    const void *lpAddress, void *lpBuffer, size_t dwLength);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_create_event_a_fn_t)(
    void *lpEventAttributes, int32_t bManualReset, int32_t bInitialState, const char *lpName);
typedef uint32_t (SCARLETT_D3D12XS_CALL *scarlett_wait_single_fn_t)(
    void *hHandle, uint32_t dwMilliseconds);
typedef int32_t (SCARLETT_D3D12XS_CALL *scarlett_close_handle_fn_t)(void *hObject);

typedef struct scarlett_mbi_s {
    uint64_t BaseAddress;
    uint64_t AllocationBase;
    uint32_t AllocationProtect;
    uint16_t PartitionId;
    uint16_t _pad0;
    uint64_t RegionSize;
    uint32_t State;
    uint32_t Protect;
    uint32_t Type;
    uint32_t _pad1;
} scarlett_mbi_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_mbi_t) == 48,
                       "scarlett_mbi_t must match Win64 MEMORY_BASIC_INFORMATION");

typedef struct scarlett_list_entry_s {
    struct scarlett_list_entry_s *Flink;
    struct scarlett_list_entry_s *Blink;
} scarlett_list_entry_t;

typedef struct scarlett_unicode_string_s {
    uint16_t Length;
    uint16_t MaximumLength;
    uint32_t Reserved;
    const uint16_t *Buffer;
} scarlett_unicode_string_t;

static scarlett_ods_fn_t g_pfn_OutputDebugStringA = NULL;
static scarlett_load_library_a_fn_t g_pfn_LoadLibraryA = NULL;
static scarlett_get_proc_address_fn_t g_pfn_GetProcAddress = NULL;
static scarlett_get_module_handle_a_fn_t g_pfn_GetModuleHandleA = NULL;
static scarlett_set_event_fn_t g_pfn_SetEvent = NULL;
static scarlett_virtual_alloc_fn_t g_pfn_VirtualAlloc = NULL;
static scarlett_virtual_free_fn_t g_pfn_VirtualFree = NULL;
static scarlett_qpc_fn_t g_pfn_QueryPerformanceCounter = NULL;
static scarlett_virtual_query_fn_t g_pfn_VirtualQuery = NULL;
static scarlett_create_event_a_fn_t g_pfn_CreateEventA = NULL;
static scarlett_wait_single_fn_t g_pfn_WaitForSingleObject = NULL;
static scarlett_close_handle_fn_t g_pfn_CloseHandle = NULL;
static uint32_t g_k32_resolved = 0u;

static bool d3d12xs_pe_ascii_ieq(const char *a, const char *b) {
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

static bool d3d12xs_pe_utf16_ascii_ieq(const uint16_t *w, uint16_t byte_len, const char *ascii) {
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

static void *d3d12xs_pe_find_export(const uint8_t *base, const char *proc_name) {
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
    uint16_t magic = *(const uint16_t *)(nt + 0x18u);
    if (magic != 0x020Bu) {
        return NULL; /* PE32+ */
    }
    uint32_t exp_rva = *(const uint32_t *)(nt + 0x88u);
    uint32_t exp_size = *(const uint32_t *)(nt + 0x8Cu);
    if (exp_rva == 0u || exp_size == 0u) {
        return NULL;
    }

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
        if (d3d12xs_pe_ascii_ieq(name, proc_name)) {
            uint32_t fn_rva = funcs[ords[i]];
            if (fn_rva >= exp_rva && fn_rva < exp_rva + exp_size) {
                return NULL; /* Forwarder */
            }
            return (void *)(uintptr_t)(base + fn_rva);
        }
    }
    return NULL;
}

static void d3d12xs_pe_resolve_kernel32(void) {
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
            if (d3d12xs_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernel32.dll")) {
                k32_base = dll_base;
            } else if (d3d12xs_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernelbase.dll")) {
                kbase_base = dll_base;
            }
        }
    }

    void *ods = d3d12xs_pe_find_export(k32_base, "OutputDebugStringA");
    if (ods == NULL) {
        ods = d3d12xs_pe_find_export(kbase_base, "OutputDebugStringA");
    }
    void *lla = d3d12xs_pe_find_export(k32_base, "LoadLibraryA");
    if (lla == NULL) {
        lla = d3d12xs_pe_find_export(kbase_base, "LoadLibraryA");
    }
    void *gpa = d3d12xs_pe_find_export(k32_base, "GetProcAddress");
    if (gpa == NULL) {
        gpa = d3d12xs_pe_find_export(kbase_base, "GetProcAddress");
    }
    void *gmh = d3d12xs_pe_find_export(k32_base, "GetModuleHandleA");
    if (gmh == NULL) {
        gmh = d3d12xs_pe_find_export(kbase_base, "GetModuleHandleA");
    }
    void *sev = d3d12xs_pe_find_export(k32_base, "SetEvent");
    if (sev == NULL) {
        sev = d3d12xs_pe_find_export(kbase_base, "SetEvent");
    }
    void *va = d3d12xs_pe_find_export(k32_base, "VirtualAlloc");
    if (va == NULL) {
        va = d3d12xs_pe_find_export(kbase_base, "VirtualAlloc");
    }
    void *vf = d3d12xs_pe_find_export(k32_base, "VirtualFree");
    if (vf == NULL) {
        vf = d3d12xs_pe_find_export(kbase_base, "VirtualFree");
    }
    void *qpc = d3d12xs_pe_find_export(k32_base, "QueryPerformanceCounter");
    if (qpc == NULL) {
        qpc = d3d12xs_pe_find_export(kbase_base, "QueryPerformanceCounter");
    }
    void *vq = d3d12xs_pe_find_export(k32_base, "VirtualQuery");
    if (vq == NULL) {
        vq = d3d12xs_pe_find_export(kbase_base, "VirtualQuery");
    }
    void *cea = d3d12xs_pe_find_export(k32_base, "CreateEventA");
    if (cea == NULL) {
        cea = d3d12xs_pe_find_export(kbase_base, "CreateEventA");
    }
    void *wfso = d3d12xs_pe_find_export(k32_base, "WaitForSingleObject");
    if (wfso == NULL) {
        wfso = d3d12xs_pe_find_export(kbase_base, "WaitForSingleObject");
    }
    void *ch = d3d12xs_pe_find_export(k32_base, "CloseHandle");
    if (ch == NULL) {
        ch = d3d12xs_pe_find_export(kbase_base, "CloseHandle");
    }

    __atomic_store_n(&g_pfn_OutputDebugStringA, (scarlett_ods_fn_t)ods, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_LoadLibraryA, (scarlett_load_library_a_fn_t)lla, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_GetProcAddress, (scarlett_get_proc_address_fn_t)gpa, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_GetModuleHandleA, (scarlett_get_module_handle_a_fn_t)gmh, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_SetEvent, (scarlett_set_event_fn_t)sev, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_VirtualAlloc, (scarlett_virtual_alloc_fn_t)va, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_VirtualFree, (scarlett_virtual_free_fn_t)vf, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_QueryPerformanceCounter, (scarlett_qpc_fn_t)qpc, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_VirtualQuery, (scarlett_virtual_query_fn_t)vq, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_CreateEventA, (scarlett_create_event_a_fn_t)cea, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_WaitForSingleObject, (scarlett_wait_single_fn_t)wfso, __ATOMIC_RELEASE);
    __atomic_store_n(&g_pfn_CloseHandle, (scarlett_close_handle_fn_t)ch, __ATOMIC_RELEASE);
    __atomic_store_n(&g_k32_resolved, 1u, __ATOMIC_RELEASE);
}

static void *d3d12xs_pe_alloc_zero(size_t bytes) {
    if (bytes == 0u) {
        return NULL;
    }
    d3d12xs_pe_resolve_kernel32();
    scarlett_virtual_alloc_fn_t va =
        __atomic_load_n(&g_pfn_VirtualAlloc, __ATOMIC_ACQUIRE);
    if (va == NULL) {
        return NULL;
    }
    return va(NULL, bytes, 0x3000u, 0x04u);
}

static void d3d12xs_pe_free_mem(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    d3d12xs_pe_resolve_kernel32();
    scarlett_virtual_free_fn_t vf =
        __atomic_load_n(&g_pfn_VirtualFree, __ATOMIC_ACQUIRE);
    if (vf != NULL) {
        (void)vf(ptr, 0u, 0x8000u);
    }
}

static bool d3d12xs_pe_unix_check_mem(uint64_t addr, size_t bytes, bool check_write);

static bool d3d12xs_pe_is_readable(const void *ptr, size_t bytes) {
    uint64_t addr = (uint64_t)(uintptr_t)ptr;
    if (addr < UINT64_C(0x10000) || addr > UINT64_C(0x00007FFFFFFFFFFF) ||
        bytes == 0u || addr + bytes < addr) {
        return false;
    }
    d3d12xs_pe_resolve_kernel32();
    scarlett_virtual_query_fn_t vq =
        __atomic_load_n(&g_pfn_VirtualQuery, __ATOMIC_ACQUIRE);
    if (vq == NULL) {
        return true;
    }
    uint64_t cur = addr;
    uint64_t target_end = addr + (uint64_t)bytes;
    for (uint32_t step = 0u; step < 16u && cur < target_end; ++step) {
        scarlett_mbi_t mbi;
        d3d12xs_pe_zero(&mbi, sizeof(mbi));
        if (vq((const void *)(uintptr_t)cur, &mbi, sizeof(mbi)) < sizeof(mbi)) {
            return d3d12xs_pe_unix_check_mem(addr, bytes, false);
        }
        if (mbi.State == 0x10000u) {
            /* MEM_FREE in Wine tree may still be mmap-backed by xmem_unix.so */
            return d3d12xs_pe_unix_check_mem(addr, bytes, false);
        }
        if (mbi.State != 0x1000u || (mbi.Protect & 0x101u) != 0u ||
            (mbi.Protect & 0xEEu) == 0u || mbi.RegionSize == 0u) {
            return false;
        }
        uint64_t reg_end = mbi.BaseAddress + mbi.RegionSize;
        if (reg_end <= cur) {
            return false;
        }
        cur = reg_end;
    }
    return cur >= target_end;
}

static bool d3d12xs_pe_is_writable_va(uint64_t va, size_t bytes) {
    if (va < UINT64_C(0x10000) || va > UINT64_C(0x00007FFFFFFFFFFF) ||
        bytes == 0u || va + bytes < va) {
        return false;
    }
    static uint64_t g_last_rw_base = 0u;
    static uint64_t g_last_rw_end = 0u;
    uint64_t cbase = __atomic_load_n(&g_last_rw_base, __ATOMIC_ACQUIRE);
    uint64_t cend = __atomic_load_n(&g_last_rw_end, __ATOMIC_ACQUIRE);
    if (cbase != 0u && va >= cbase && (va + bytes) <= cend) {
        return true;
    }
    d3d12xs_pe_resolve_kernel32();
    scarlett_virtual_query_fn_t vq =
        __atomic_load_n(&g_pfn_VirtualQuery, __ATOMIC_ACQUIRE);
    if (vq == NULL) {
        return true;
    }
    uint64_t cur = va;
    uint64_t target_end = va + (uint64_t)bytes;
    uint64_t span_base = 0u;
    for (uint32_t step = 0u; step < 64u && cur < target_end; ++step) {
        scarlett_mbi_t mbi;
        d3d12xs_pe_zero(&mbi, sizeof(mbi));
        if (vq((const void *)(uintptr_t)cur, &mbi, sizeof(mbi)) < sizeof(mbi)) {
            return d3d12xs_pe_unix_check_mem(va, bytes, true);
        }
        if (mbi.State == 0x10000u) {
            return d3d12xs_pe_unix_check_mem(va, bytes, true);
        }
        if (mbi.State != 0x1000u || (mbi.Protect & 0x101u) != 0u ||
            (mbi.Protect & 0xCCu) == 0u || mbi.RegionSize == 0u) {
            return false;
        }
        if (step == 0u) {
            span_base = mbi.BaseAddress;
        }
        uint64_t reg_end = mbi.BaseAddress + mbi.RegionSize;
        if (reg_end <= cur) {
            return false;
        }
        cur = reg_end;
    }
    if (cur < target_end) {
        return false;
    }
    __atomic_store_n(&g_last_rw_base, span_base, __ATOMIC_RELEASE);
    __atomic_store_n(&g_last_rw_end, cur, __ATOMIC_RELEASE);
    return true;
}

static void d3d12xs_pe_ods(const char *msg) {
    d3d12xs_pe_resolve_kernel32();
    scarlett_ods_fn_t ods = __atomic_load_n(&g_pfn_OutputDebugStringA, __ATOMIC_ACQUIRE);
    if (ods != NULL && msg != NULL) {
        ods(msg);
    }
}

typedef void * (SCARLETT_D3D12XS_CALL *scarlett_find_window_a_fn_t)(
    const char *lpClassName, const char *lpWindowName);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_get_prop_a_fn_t)(
    void *hWnd, const char *lpString);
typedef intptr_t (SCARLETT_D3D12XS_CALL *scarlett_set_class_long_ptr_a_fn_t)(
    void *hWnd, int32_t nIndex, intptr_t dwNewLong);
typedef int32_t (SCARLETT_D3D12XS_CALL *scarlett_invalidate_rect_fn_t)(
    void *hWnd, const void *lpRect, int32_t bErase);
typedef void * (SCARLETT_D3D12XS_CALL *scarlett_get_stock_object_fn_t)(int32_t i);

static uint64_t d3d12xs_pe_get_host_x11_window(void) {
    static uint64_t g_cached_x11_win = 0u;
    static uint32_t g_brush_cleared = 0u;
    uint64_t cached = __atomic_load_n(&g_cached_x11_win, __ATOMIC_ACQUIRE);
    if (cached != 0u) {
        return cached;
    }

    d3d12xs_pe_resolve_kernel32();
    const uint8_t *peb = NULL;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (peb == NULL) {
        return 0u;
    }
    const uint8_t *ldr = *(const uint8_t *const *)(peb + 0x18u);
    if (ldr == NULL) {
        return 0u;
    }
    const scarlett_list_entry_t *head = (const scarlett_list_entry_t *)(ldr + 0x20u);
    const uint8_t *user32_base = NULL;
    const uint8_t *gdi32_base = NULL;
    for (const scarlett_list_entry_t *cur = head->Flink;
         cur != NULL && cur != head;
         cur = cur->Flink) {
        const uint8_t *entry = (const uint8_t *)cur;
        const uint8_t *dll_base = *(const uint8_t *const *)(entry + 0x20u);
        const scarlett_unicode_string_t *base_name =
            (const scarlett_unicode_string_t *)(entry + 0x48u);
        if (dll_base != NULL && base_name->Buffer != NULL) {
            if (d3d12xs_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "user32.dll")) {
                user32_base = dll_base;
            } else if (d3d12xs_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "gdi32.dll")) {
                gdi32_base = dll_base;
            }
        }
    }
    if (user32_base == NULL) {
        scarlett_get_module_handle_a_fn_t gmh =
            __atomic_load_n(&g_pfn_GetModuleHandleA, __ATOMIC_ACQUIRE);
        if (gmh != NULL) {
            user32_base = (const uint8_t *)gmh("user32.dll");
            if (gdi32_base == NULL) {
                gdi32_base = (const uint8_t *)gmh("gdi32.dll");
            }
        }
    }
    if (user32_base == NULL) {
        return 0u;
    }

    scarlett_find_window_a_fn_t pFindWindowA =
        (scarlett_find_window_a_fn_t)d3d12xs_pe_find_export(user32_base, "FindWindowA");
    scarlett_get_prop_a_fn_t pGetPropA =
        (scarlett_get_prop_a_fn_t)d3d12xs_pe_find_export(user32_base, "GetPropA");
    scarlett_set_class_long_ptr_a_fn_t pSetClassLongPtrA =
        (scarlett_set_class_long_ptr_a_fn_t)d3d12xs_pe_find_export(user32_base, "SetClassLongPtrA");
    scarlett_invalidate_rect_fn_t pInvalidateRect =
        (scarlett_invalidate_rect_fn_t)d3d12xs_pe_find_export(user32_base, "InvalidateRect");
    scarlett_get_stock_object_fn_t pGetStockObject =
        (gdi32_base != NULL)
            ? (scarlett_get_stock_object_fn_t)d3d12xs_pe_find_export(gdi32_base, "GetStockObject")
            : NULL;

    if (pFindWindowA == NULL || pGetPropA == NULL) {
        return 0u;
    }

    void *hwnd = pFindWindowA("sgaWindowClass", NULL);
    if (hwnd == NULL) {
        return 0u;
    }

    if (__atomic_exchange_n(&g_brush_cleared, 1u, __ATOMIC_ACQ_REL) == 0u) {
        if (pSetClassLongPtrA != NULL && pGetStockObject != NULL) {
            void *black_brush = pGetStockObject(4 /* BLACK_BRUSH */);
            if (black_brush != NULL) {
                (void)pSetClassLongPtrA(hwnd, -10 /* GCLP_HBRBACKGROUND */, (intptr_t)black_brush);
            }
        }
        if (pInvalidateRect != NULL) {
            (void)pInvalidateRect(hwnd, NULL, 1);
        }
    }

    uint64_t x11_win = (uint64_t)(uintptr_t)pGetPropA(hwnd, "__wine_x11_whole_window");
    if (x11_win != 0u) {
        __atomic_store_n(&g_cached_x11_win, x11_win, __ATOMIC_RELEASE);
    }
    return x11_win;
}

#else

extern void *calloc(size_t nmemb, size_t size);
extern void free(void *ptr);
extern int mincore(void *addr, size_t length, unsigned char *vec);

static inline void d3d12xs_pe_ods(const char *msg) {
    (void)msg;
}

static inline uint64_t d3d12xs_pe_get_host_x11_window(void) {
    return 0u;
}

static inline void *d3d12xs_pe_alloc_zero(size_t bytes) {
    return (bytes > 0u) ? calloc(1u, bytes) : NULL;
}

static inline void d3d12xs_pe_free_mem(void *ptr) {
    free(ptr);
}

static inline bool d3d12xs_pe_is_readable(const void *ptr, size_t bytes) {
    uint64_t addr = (uint64_t)(uintptr_t)ptr;
    if (addr < UINT64_C(0x10000) || addr > UINT64_C(0x00007FFFFFFFFFFF) ||
        bytes == 0u || (addr + bytes) < addr) {
        return false;
    }
    uintptr_t page = (uintptr_t)(addr & ~UINT64_C(4095));
    size_t span = (size_t)((addr + bytes) - (uint64_t)page);
    size_t npages = (span + 4095u) >> 12u;
    if (npages == 0u || npages > 4096u) {
        return false;
    }
    unsigned char vec[64];
    if (npages <= sizeof(vec)) {
        return mincore((void *)page, npages << 12u, vec) == 0;
    }
    return mincore((void *)page, sizeof(vec) << 12u, vec) == 0;
}

static inline bool d3d12xs_pe_is_writable_va(uint64_t va, size_t bytes) {
    return va >= UINT64_C(0x10000) && bytes > 0u && (va + bytes) > va;
}

#endif

static inline uint64_t d3d12xs_pe_get_qpc_timestamp(uint64_t fallback_ns) {
#if defined(_WIN32) || defined(__MINGW32__)
    d3d12xs_pe_resolve_kernel32();
    scarlett_qpc_fn_t qpc = __atomic_load_n(&g_pfn_QueryPerformanceCounter, __ATOMIC_ACQUIRE);
    if (qpc != NULL) {
        uint64_t val = 0u;
        if (qpc(&val) && val != 0u) {
            return val;
        }
    }
#endif
    if (fallback_ns != 0u) {
        return fallback_ns / 100u; /* 10MHz QPC ticks (100ns per tick) */
    }
    static uint64_t g_qpc_counter = 10000000ULL;
    return __atomic_add_fetch(&g_qpc_counter, 166666ULL, __ATOMIC_RELAXED); /* 60Hz at 10MHz */
}

static inline char *d3d12xs_pe_append_str(char *p, char *end, const char *s) {
    if (s == NULL) {
        s = "(null)";
    }
    while (*s != '\0' && p + 1 < end) {
        *p++ = *s++;
    }
    return p;
}

static inline char *d3d12xs_pe_append_hex_padded(
    char *p, char *end, uint64_t val, int digits) {
    static const char kHex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; --i) {
        if (p + 1 < end) {
            *p++ = kHex[(val >> (i * 4)) & 0xFu];
        }
    }
    return p;
}

static inline char *d3d12xs_pe_append_u32(char *p, char *end, uint32_t val) {
    char tmp[16];
    int n = 0;
    if (val == 0u) {
        tmp[n++] = '0';
    } else {
        while (val > 0u && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    while (n > 0 && p + 1 < end) {
        *p++ = tmp[--n];
    }
    return p;
}

static void d3d12xs_pe_log_stub_ods(
    const char *dll_name,
    const char *symbol_name,
    uint32_t vtable_slot,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9,
    scarlett_hresult_t hr,
    const char *note) {
    char buf[512];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = d3d12xs_pe_append_str(p, end, "[scarlett-stub] dll=");
    p = d3d12xs_pe_append_str(p, end, dll_name);
    p = d3d12xs_pe_append_str(p, end, " sym=");
    p = d3d12xs_pe_append_str(p, end, symbol_name);
    if (vtable_slot != SCARLETT_STUB_SLOT_NONE) {
        p = d3d12xs_pe_append_str(p, end, " slot=");
        p = d3d12xs_pe_append_u32(p, end, vtable_slot);
    }
    p = d3d12xs_pe_append_str(p, end, " caller=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, return_address, 16);
    p = d3d12xs_pe_append_str(p, end, " rcx=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, rcx, 16);
    p = d3d12xs_pe_append_str(p, end, " rdx=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, rdx, 16);
    p = d3d12xs_pe_append_str(p, end, " r8=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, r8, 16);
    p = d3d12xs_pe_append_str(p, end, " r9=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, r9, 16);
    p = d3d12xs_pe_append_str(p, end, " hr=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, (uint32_t)hr, 8);
    if (note != NULL) {
        p = d3d12xs_pe_append_str(p, end, " ");
        p = d3d12xs_pe_append_str(p, end, note);
    }
    p = d3d12xs_pe_append_str(p, end, "\n");
    *p = '\0';
    d3d12xs_pe_ods(buf);
}

static void d3d12xs_pe_log_guid_ods(
    const char *sym,
    uint32_t slot,
    uint64_t return_address,
    const scarlett_guid_t *riid,
    scarlett_hresult_t hr) {
    static uint32_t g_guid_ods_count = 0u;
    if (__atomic_add_fetch(&g_guid_ods_count, 1u, __ATOMIC_RELAXED) > 32u) {
        return;
    }
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = d3d12xs_pe_append_str(p, end, "[scarlett-iid] sym=");
    p = d3d12xs_pe_append_str(p, end, sym);
    if (slot != SCARLETT_STUB_SLOT_NONE) {
        p = d3d12xs_pe_append_str(p, end, " slot=");
        p = d3d12xs_pe_append_u32(p, end, slot);
    }
    p = d3d12xs_pe_append_str(p, end, " caller=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, return_address, 16);
    if (riid != NULL) {
        p = d3d12xs_pe_append_str(p, end, " iid={");
        p = d3d12xs_pe_append_hex_padded(p, end, riid->data1, 8);
        p = d3d12xs_pe_append_str(p, end, "-");
        p = d3d12xs_pe_append_hex_padded(p, end, riid->data2, 4);
        p = d3d12xs_pe_append_str(p, end, "-");
        p = d3d12xs_pe_append_hex_padded(p, end, riid->data3, 4);
        p = d3d12xs_pe_append_str(p, end, "-");
        p = d3d12xs_pe_append_hex_padded(p, end, riid->data4[0], 2);
        p = d3d12xs_pe_append_hex_padded(p, end, riid->data4[1], 2);
        p = d3d12xs_pe_append_str(p, end, "-");
        for (size_t i = 2u; i < 8u; ++i) {
            p = d3d12xs_pe_append_hex_padded(p, end, riid->data4[i], 2);
        }
        p = d3d12xs_pe_append_str(p, end, "}");
    } else {
        p = d3d12xs_pe_append_str(p, end, " iid=(null)");
    }
    p = d3d12xs_pe_append_str(p, end, " hr=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, (uint32_t)hr, 8);
    p = d3d12xs_pe_append_str(p, end, "\n");
    *p = '\0';
    d3d12xs_pe_ods(buf);
}

static void d3d12xs_pe_vkd3d_log(
    const char *tag,
    const void *obj,
    scarlett_hresult_t hr) {
    char buf[256];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = d3d12xs_pe_append_str(p, end, "[scarlett-vkd3d] ");
    p = d3d12xs_pe_append_str(p, end, tag);
    p = d3d12xs_pe_append_str(p, end, " obj=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, (uint64_t)(uintptr_t)obj, 16);
    p = d3d12xs_pe_append_str(p, end, " hr=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, (uint32_t)hr, 8);
    p = d3d12xs_pe_append_str(p, end, "\n");
    *p = '\0';
    d3d12xs_pe_ods(buf);
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

#if defined(_WIN32) || defined(__MINGW32__)
static bool d3d12xs_pe_unix_check_mem(uint64_t addr, size_t bytes, bool check_write) {
    scarlett_d3d12xs_check_mem_params_t p;
    d3d12xs_pe_zero(&p, sizeof(p));
    p.addr = addr;
    p.size = (uint64_t)bytes;
    p.check_write = check_write ? 1u : 0u;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_CHECK_MEM, &p);
    return scarlett_succeeded(rc) && scarlett_succeeded(p.out_status) && p.out_ok != 0u;
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
    scarlett_hresult_t final_hr = scarlett_failed(rc)
        ? scarlett_stub_enforce_error(rc)
        : scarlett_stub_enforce_error(params.ctx.return_hr);
    d3d12xs_pe_log_stub_ods(
        params.ctx.dll_name,
        params.ctx.symbol_name,
        params.ctx.vtable_slot,
        return_address,
        rcx,
        rdx,
        r8,
        r9,
        final_hr,
        NULL);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
#if defined(_WIN32) || defined(__MINGW32__)
        __builtin_trap();
#else
        return SCARLETT_ERR_STRICT_STUB_ABORT;
#endif
    }
    return final_hr;
}

/* ============================================================================
 * ID3D12DeviceX Static COM Object Pool & VTable (Matching d3d12_xs.xdl)
 * ============================================================================ */

#define SCARLETT_D3D12XS_MAX_COM_DEVICES 16u
static __attribute__((aligned(16))) ID3D12DeviceX g_com_devices[SCARLETT_D3D12XS_MAX_COM_DEVICES];

static const scarlett_guid_t kIidIUnknown = {
    UINT32_C(0x00000000), UINT16_C(0x0000), UINT16_C(0x0000),
    { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }
};

static const scarlett_guid_t kIidID3D12DeviceX = SCARLETT_IID_ID3D12DEVICEX_INIT;
static const scarlett_guid_t kIidID3D12DeviceX_Gdk = SCARLETT_IID_ID3D12DEVICEX_GDK_INIT;
/* IID_ID3D12CommandQueue = {0EC870A6-5D7E-4C22-8CFC-5BAAE07616ED} */
static const scarlett_guid_t kIidID3D12CommandQueue = {
    UINT32_C(0x0EC870A6), UINT16_C(0x5D7E), UINT16_C(0x4C22),
    { 0x8Cu, 0xFCu, 0x5Bu, 0xAAu, 0xE0u, 0x76u, 0x16u, 0xEDu }
};
#if defined(_WIN32) || defined(__MINGW32__)
/* IID_ID3D12Device (Standard) = {189819F1-1DB6-4B57-BE54-1821339B85F7} */
static const scarlett_guid_t kIidID3D12Device_Std = {
    UINT32_C(0x189819F1), UINT16_C(0x1DB6), UINT16_C(0x4B57),
    { 0xBEu, 0x54u, 0x18u, 0x21u, 0x33u, 0x9Bu, 0x85u, 0xF7u }
};
#endif
/* IID_ID3D12Pageable = {63EE58FB-1268-4835-86DA-F008CE62F0D6} */
static const scarlett_guid_t kIidID3D12Pageable = {
    UINT32_C(0x63EE58FB), UINT16_C(0x1268), UINT16_C(0x4835),
    { 0x86u, 0xDAu, 0xF0u, 0x08u, 0xCEu, 0x62u, 0xF0u, 0xD6u }
};
/* IID_ID3D12DeviceChild = {905DB94B-A00C-4140-9DF5-2B64CA9EA357} */
static const scarlett_guid_t kIidID3D12DeviceChild = {
    UINT32_C(0x905DB94B), UINT16_C(0xA00C), UINT16_C(0x4140),
    { 0x9Du, 0xF5u, 0x2Bu, 0x64u, 0xCAu, 0x9Eu, 0xA3u, 0x57u }
};
/* IID_ID3D12Object = {C4FEC28F-7966-4E95-9F94-F431CB56C3B8} */
static const scarlett_guid_t kIidID3D12Object = {
    UINT32_C(0xC4FEC28F), UINT16_C(0x7966), UINT16_C(0x4E95),
    { 0x9Fu, 0x94u, 0xF4u, 0x31u, 0xCBu, 0x56u, 0xC3u, 0xB8u }
};
/* IID_ID3D12Fence = {0A753DCF-C4D8-4B91-ADF6-BE5A60D95A76} */
static const scarlett_guid_t kIidID3D12Fence = {
    UINT32_C(0x0A753DCF), UINT16_C(0xC4D8), UINT16_C(0x4B91),
    { 0xADu, 0xF6u, 0xBEu, 0x5Au, 0x60u, 0xD9u, 0x5Au, 0x76u }
};
/* IID_ID3D12CommandAllocator = {6102DEE4-AF59-4B09-B999-B44D73F09B24} */
static const scarlett_guid_t kIidID3D12CommandAllocator = {
    UINT32_C(0x6102DEE4), UINT16_C(0xAF59), UINT16_C(0x4B09),
    { 0xB9u, 0x99u, 0xB4u, 0x4Du, 0x73u, 0xF0u, 0x9Bu, 0x24u }
};
/* IID_ID3D12GraphicsCommandList (Xbox GDK) = {C3827890-4024-474C-98CF-645B51DC4874} */
static const scarlett_guid_t kIidID3D12GraphicsCommandList = {
    UINT32_C(0xC3827890), UINT16_C(0x4024), UINT16_C(0x474C),
    { 0x98u, 0xCFu, 0x64u, 0x5Bu, 0x51u, 0xDCu, 0x48u, 0x74u }
};
/* IID_ID3D12GraphicsCommandList (Standard) = {5B160D0F-AC1B-4185-8BA8-B3AE42A5A455} */
static const scarlett_guid_t kIidID3D12GraphicsCommandList_Std = {
    UINT32_C(0x5B160D0F), UINT16_C(0xAC1B), UINT16_C(0x4185),
    { 0x8Bu, 0xA8u, 0xB3u, 0xAEu, 0x42u, 0xA5u, 0xA4u, 0x55u }
};
/* IID_ID3D12CommandList = {7116D91C-E7E4-4C2E-9D11-2E64E2E2B7A0} */
static const scarlett_guid_t kIidID3D12CommandList = {
    UINT32_C(0x7116D91C), UINT16_C(0xE7E4), UINT16_C(0x4C2E),
    { 0x9Du, 0x11u, 0x2Eu, 0x64u, 0xE2u, 0xE2u, 0xB7u, 0xA0u }
};
/* IID_ID3D12DescriptorHeap = {8EFB471D-616C-4F49-90F7-127BB763FA51} */
static const scarlett_guid_t kIidID3D12DescriptorHeap = {
    UINT32_C(0x8EFB471D), UINT16_C(0x616C), UINT16_C(0x4F49),
    { 0x90u, 0xF7u, 0x12u, 0x7Bu, 0xB7u, 0x63u, 0xFAu, 0x51u }
};
/* IID_ID3D12Resource = {696442BE-A72E-4059-BC79-5B5C98040FAD} */
static const scarlett_guid_t kIidID3D12Resource = {
    UINT32_C(0x696442BE), UINT16_C(0xA72E), UINT16_C(0x4059),
    { 0xBCu, 0x79u, 0x5Bu, 0x5Cu, 0x98u, 0x04u, 0x0Fu, 0xADu }
};
/* IID_ID3D12RootSignature = {C54A6B66-72DF-4EE8-8BE5-A946A1429214} */
static const scarlett_guid_t kIidID3D12RootSignature = {
    UINT32_C(0xC54A6B66), UINT16_C(0x72DF), UINT16_C(0x4EE8),
    { 0x8Bu, 0xE5u, 0xA9u, 0x46u, 0xA1u, 0x42u, 0x92u, 0x14u }
};
/* IID_ID3D12PipelineState = {765A30F3-F624-4C6F-A828-ACE948622445} */
static const scarlett_guid_t kIidID3D12PipelineState = {
    UINT32_C(0x765A30F3), UINT16_C(0xF624), UINT16_C(0x4C6F),
    { 0xA8u, 0x28u, 0xACu, 0xE9u, 0x48u, 0x62u, 0x24u, 0x45u }
};
/* IID_ID3D12StateObject (Xbox GDK) = {94A2B1C2-7684-8777-97A5-A69788777684} */
static const scarlett_guid_t kIidID3D12StateObject = {
    UINT32_C(0x94A2B1C2), UINT16_C(0x7684), UINT16_C(0x8777),
    { 0x97u, 0xA5u, 0xA6u, 0x97u, 0x88u, 0x77u, 0x76u, 0x84u }
};
/* IID_ID3D12StateObject (Standard) = {47016943-FCA8-4594-93EA-AF258B55346D} */
static const scarlett_guid_t kIidID3D12StateObject_Std = {
    UINT32_C(0x47016943), UINT16_C(0xFCA8), UINT16_C(0x4594),
    { 0x93u, 0xEAu, 0xAFu, 0x25u, 0x8Bu, 0x55u, 0x34u, 0x6Du }
};
/* IID_ID3D12StateObjectProperties = {DE5FA827-9BF9-4F26-89FF-D7F56FDE3860} */
static const scarlett_guid_t kIidID3D12StateObjectProperties = {
    UINT32_C(0xDE5FA827), UINT16_C(0x9BF9), UINT16_C(0x4F26),
    { 0x89u, 0xFFu, 0xD7u, 0xF5u, 0x6Fu, 0xDEu, 0x38u, 0x60u }
};
/* IID_ID3D12QueryHeap (Xbox GDK) = {718292A2-8675-A696-B6D7-B1A191817076} */
static const scarlett_guid_t kIidID3D12QueryHeap = {
    UINT32_C(0x718292A2), UINT16_C(0x8675), UINT16_C(0xA696),
    { 0xB6u, 0xD7u, 0xB1u, 0xA1u, 0x91u, 0x81u, 0x70u, 0x76u }
};
/* IID_ID3D12QueryHeap (Standard) = {0D9658AE-ED45-469E-A61D-970EC583CAB4} */
static const scarlett_guid_t kIidID3D12QueryHeap_Std = {
    UINT32_C(0x0D9658AE), UINT16_C(0xED45), UINT16_C(0x469E),
    { 0xA6u, 0x1Du, 0x97u, 0x0Eu, 0xC5u, 0x83u, 0xCAu, 0xB4u }
};
/* IID_ID3D12CommandSignature (Xbox GDK) = {838D9087-6F75-8C7E-9BA9-9B8B7A6D7682} */
static const scarlett_guid_t kIidID3D12CommandSignature = {
    UINT32_C(0x838D9087), UINT16_C(0x6F75), UINT16_C(0x8C7E),
    { 0x9Bu, 0xA9u, 0x9Bu, 0x8Bu, 0x7Au, 0x6Du, 0x76u, 0x82u }
};
/* IID_ID3D12CommandSignature (Standard) = {C36A797C-EC80-4F0A-8985-A7B2475082D1} */
static const scarlett_guid_t kIidID3D12CommandSignature_Std = {
    UINT32_C(0xC36A797C), UINT16_C(0xEC80), UINT16_C(0x4F0A),
    { 0x89u, 0x85u, 0xA7u, 0xB2u, 0x47u, 0x50u, 0x82u, 0xD1u }
};

static inline bool d3d12xs_is_device_iid(const scarlett_guid_t *riid) {
    return d3d12xs_guid_eq(riid, &kIidID3D12DeviceX_Gdk) ||
           d3d12xs_guid_eq(riid, &kIidID3D12DeviceX) ||
           d3d12xs_guid_eq(riid, &kIidIUnknown);
}

static inline bool d3d12xs_is_com_device(const void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_devices[0];
    uintptr_t end = (uintptr_t)&g_com_devices[SCARLETT_D3D12XS_MAX_COM_DEVICES];
    return p >= base && p < end;
}

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
    if (d3d12xs_is_device_iid(riid)) {
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
    if (pOutEventTimestamp != NULL && (uintptr_t)pOutEventTimestamp > 0x10000u) {
        *pOutEventTimestamp = out_wait.out_token ? out_wait.out_token : 1u;
    }
    if (hr != SCARLETT_OK) {
        hr = SCARLETT_OK;
    }
    return SCARLETT_OK;
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

#if !defined(_WIN32) && !defined(__MINGW32__)
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
#endif

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

/* ============================================================================
 * ID3D12CommandQueueX Static COM Object Pool & 64-Slot VTable
 * ============================================================================ */

typedef struct ID3D12CommandQueueXVtbl_S {
    void *slots[64];
} ID3D12CommandQueueXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12CommandQueueX_S {
    const ID3D12CommandQueueXVtbl *lpVtbl;
    uint8_t inline_scratchpad[256u - sizeof(void *)];
} ID3D12CommandQueueX;

typedef struct d3d12xs_cmd_queue_meta_s {
    uint64_t unix_device_handle;
    uint64_t unix_queue_handle;
    uint32_t ref_count;
    uint32_t in_use;
    void *real_obj;
    ID3D12DeviceX *owner_device;
} d3d12xs_cmd_queue_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_QUEUES 64u
static ID3D12CommandQueueX g_com_cmd_queues[SCARLETT_D3D12XS_MAX_COM_QUEUES];
static d3d12xs_cmd_queue_meta_t g_cmd_queue_meta[SCARLETT_D3D12XS_MAX_COM_QUEUES];

static inline d3d12xs_cmd_queue_meta_t *d3d12xs_get_cmd_queue_meta(const ID3D12CommandQueueX *q) {
    uintptr_t p = (uintptr_t)q;
    uintptr_t base = (uintptr_t)&g_com_cmd_queues[0];
    uintptr_t end = (uintptr_t)&g_com_cmd_queues[SCARLETT_D3D12XS_MAX_COM_QUEUES];
    if (p < base || p >= end) {
        return NULL;
    }
    size_t idx = (size_t)(q - &g_com_cmd_queues[0]);
    return &g_cmd_queue_meta[idx];
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_query_interface(
    ID3D12CommandQueueX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_cmd_queue_meta_t *qm = d3d12xs_get_cmd_queue_meta(self);
    if (qm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12CommandQueue) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&qm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_queue_add_ref(ID3D12CommandQueueX *self) {
    d3d12xs_cmd_queue_meta_t *qm = d3d12xs_get_cmd_queue_meta(self);
    if (qm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&qm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_queue_release(ID3D12CommandQueueX *self) {
    d3d12xs_cmd_queue_meta_t *qm = d3d12xs_get_cmd_queue_meta(self);
    if (qm == NULL || __atomic_load_n(&qm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&qm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (qm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)qm->real_obj;
            ((pfn_rel_t)vtbl[2])(qm->real_obj);
            qm->real_obj = NULL;
        }
        qm->unix_device_handle = 0u;
        qm->unix_queue_handle = 0u;
        qm->owner_device = NULL;
        __atomic_store_n(&qm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

#define D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(SLOT_NUM, SLOT_STR)                 \
    static scarlett_hresult_t SCARLETT_D3D12XS_CALL                            \
        cmd_queue_vtbl_slot_stub_##SLOT_NUM(                                   \
            uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {            \
        static const scarlett_stub_descriptor_t kDesc = {                      \
            SCARLETT_STUB_THUNK_ABI_VERSION,                                   \
            SCARLETT_STUB_FLAG_XDL_VTABLE,                                     \
            SCARLETT_STUB_ORDINAL_NONE,                                        \
            (uint32_t)(SLOT_NUM),                                              \
            SCARLETT_ERR_UNIMPLEMENTED_STUB,                                   \
            0u,                                                                \
            "d3d12_xs.dll",                                                    \
            "ID3D12CommandQueueX::Slot" SLOT_STR                               \
        };                                                                     \
        return d3d12xs_pe_dispatch_stub(                                       \
            &kDesc,                                                            \
            (uint64_t)(uintptr_t)__builtin_return_address(0),                  \
            rcx, rdx, r8, r9);                                                 \
    }

typedef struct ID3D12FenceX_S ID3D12FenceX;

static scarlett_hresult_t SCARLETT_D3D12XS_CALL fence_signal(
    ID3D12FenceX *self,
    uint64_t Value);

static inline bool d3d12xs_is_com_fence(const void *ptr);

static void *d3d12xs_unwrap_cmd_list_real(const void *ptr);
static void *d3d12xs_unwrap_fence_real(const ID3D12FenceX *f);
static void d3d12xs_cmd_list_flush_labels(void *ptr);
static uint32_t g_gpu_exec_seq = 0u;
static void d3d12xs_flush_cpu_uploads(d3d12xs_cmd_queue_meta_t *qm);
static bool d3d12xs_readback_plane0_if_dirty(
    d3d12xs_cmd_queue_meta_t *qm,
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *planes,
    uint32_t plane_count);

static void SCARLETT_D3D12XS_CALL cmd_queue_execute_command_lists(
    ID3D12CommandQueueX *self,
    uint32_t NumCommandLists,
    void * const *ppCommandLists) {
    d3d12xs_cmd_queue_meta_t *qm = d3d12xs_get_cmd_queue_meta(self);
    if (qm == NULL || ppCommandLists == NULL || NumCommandLists == 0u) {
        return;
    }
    for (uint32_t i = 0u; i < NumCommandLists; ++i) {
        d3d12xs_cmd_list_flush_labels(ppCommandLists[i]);
    }
    if (qm->real_obj == NULL) {
        return;
    }
    void *real_lists[64];
    uint32_t real_count = 0u;
    uint32_t limit = (NumCommandLists < 64u) ? NumCommandLists : 64u;
    for (uint32_t i = 0u; i < limit; ++i) {
        void *r = d3d12xs_unwrap_cmd_list_real(ppCommandLists[i]);
        if (r != NULL) {
            real_lists[real_count++] = r;
        }
    }
    if (real_count > 0u) {
        d3d12xs_flush_cpu_uploads(qm);
        static uint32_t g_ecl_seq = 0u;
        uint32_t eseq = __atomic_add_fetch(&g_ecl_seq, 1u, __ATOMIC_RELAXED);
        if (eseq <= 8u || (eseq % 250u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "ExecuteCommandLists", 10u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self, (uint64_t)NumCommandLists,
                (uint64_t)real_count, (uint64_t)(uintptr_t)real_lists[0],
                SCARLETT_OK, "(forwarded)");
        }
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_ecl_t)(
            void *, uint32_t, void * const *);
        void **vtbl = *(void ***)qm->real_obj;
        pfn_real_ecl_t fn = (pfn_real_ecl_t)vtbl[10];
        fn(qm->real_obj, real_count, real_lists);
        __atomic_add_fetch(&g_gpu_exec_seq, 1u, __ATOMIC_RELEASE);
    }
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_signal(
    ID3D12CommandQueueX *self,
    void *pFence,
    uint64_t Value) {
    d3d12xs_cmd_queue_meta_t *qm = d3d12xs_get_cmd_queue_meta(self);
    if (qm == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (d3d12xs_is_com_fence(pFence)) {
        ID3D12FenceX *f = (ID3D12FenceX *)pFence;
        void *real_f = d3d12xs_unwrap_fence_real(f);
        if (qm->real_obj != NULL && real_f != NULL) {
            typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_qsig_t)(
                void *, void *, uint64_t);
            void **vtbl = *(void ***)qm->real_obj;
            pfn_real_qsig_t fn = (pfn_real_qsig_t)vtbl[14];
            (void)fn(qm->real_obj, real_f, Value);
        }
        return fence_signal(f, Value);
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_wait(
    ID3D12CommandQueueX *self,
    void *pFence,
    uint64_t Value) {
    (void)pFence;
    (void)Value;
    return (self != NULL) ? SCARLETT_OK : SCARLETT_E_POINTER;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_get_timestamp_frequency(
    ID3D12CommandQueueX *self,
    uint64_t *pFrequency) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (pFrequency != NULL) {
        *pFrequency = UINT64_C(1000000000);
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_slot31_ok(
    ID3D12CommandQueueX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    static uint32_t logged = 0u;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (__atomic_exchange_n(&logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12CommandQueueX::Slot31",
            31u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            rdx,
            r8,
            r9,
            SCARLETT_OK,
            "(ok)");
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_slot32_present_pacing(
    ID3D12CommandQueueX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    d3d12xs_cmd_queue_meta_t *qm = d3d12xs_get_cmd_queue_meta(self);
    if (qm == NULL) {
        return SCARLETT_E_POINTER;
    }
    static uint32_t g_presentx_frame_counter = 0u;
    uint32_t frame = __atomic_add_fetch(&g_presentx_frame_counter, 1u, __ATOMIC_RELAXED);

    uint32_t plane_count = (rdx <= 2u) ? (uint32_t)rdx : 0u;
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *planes =
        (r8 > 0x10000u) ? (const D3D12XBOX_PRESENT_PLANE_PARAMETERS *)(uintptr_t)r8 : NULL;
    const D3D12XBOX_PRESENT_PARAMETERS *params =
        (r9 > 0x10000u) ? (const D3D12XBOX_PRESENT_PARAMETERS *)(uintptr_t)r9 : NULL;
    if (planes == NULL) {
        plane_count = 0u;
    }
    (void)d3d12xs_readback_plane0_if_dirty(qm, planes, plane_count);
    scarlett_d3d12xs_presentx_params_t out_present;
    scarlett_hresult_t hr = D3D12XboxPresentX(
        qm->unix_device_handle,
        plane_count,
        planes,
        params,
        &out_present);
    if (scarlett_failed(hr)) {
        hr = SCARLETT_OK;
    }

    if (frame <= 30u || (frame % 60u) == 0u) {
        uint64_t plane0_va = (plane_count > 0u) ? out_present.planes[0].gpu_va : 0u;
        char buf[256];
        char *p = buf;
        char *end = buf + sizeof(buf);
        p = d3d12xs_pe_append_str(p, end, "[scarlett-presentx] PresentX frame=");
        p = d3d12xs_pe_append_u32(p, end, frame);
        p = d3d12xs_pe_append_str(p, end, " caller=0x");
        p = d3d12xs_pe_append_hex_padded(
            p, end, (uint64_t)(uintptr_t)__builtin_return_address(0), 16);
        p = d3d12xs_pe_append_str(p, end, " rdx=0x");
        p = d3d12xs_pe_append_hex_padded(p, end, rdx, 16);
        p = d3d12xs_pe_append_str(p, end, " r8=0x");
        p = d3d12xs_pe_append_hex_padded(p, end, r8, 16);
        p = d3d12xs_pe_append_str(p, end, " r9=0x");
        p = d3d12xs_pe_append_hex_padded(p, end, r9, 16);
        p = d3d12xs_pe_append_str(p, end, " p0va=0x");
        p = d3d12xs_pe_append_hex_padded(p, end, plane0_va, 16);
        p = d3d12xs_pe_append_str(p, end, " hr=0x");
        p = d3d12xs_pe_append_hex_padded(p, end, (uint32_t)hr, 8);
        p = d3d12xs_pe_append_str(p, end, "\n");
        *p = '\0';
        d3d12xs_pe_ods(buf);
    }
    return hr;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_queue_execute_command_lists_x(
    ID3D12CommandQueueX *self,
    uint32_t NumCommandLists,
    void * const *ppCommandLists) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (ppCommandLists != NULL && NumCommandLists > 0u) {
        cmd_queue_execute_command_lists(self, NumCommandLists, ppCommandLists);
    }
    return SCARLETT_OK;
}

D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(3, "3")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(4, "4")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(5, "5")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(6, "6")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(7, "7")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(8, "8")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(9, "9")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(11, "11")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(12, "12")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(13, "13")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(19, "19")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(20, "20")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(21, "21")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(22, "22")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(23, "23")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(24, "24")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(25, "25")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(26, "26")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(27, "27")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(28, "28")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(29, "29")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(30, "30")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(33, "33")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(34, "34")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(36, "36")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(37, "37")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(38, "38")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(39, "39")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(40, "40")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(41, "41")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(42, "42")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(43, "43")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(44, "44")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(45, "45")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(46, "46")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(47, "47")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(48, "48")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(49, "49")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(50, "50")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(51, "51")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(52, "52")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(53, "53")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(54, "54")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(55, "55")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(56, "56")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(57, "57")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(58, "58")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(59, "59")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(60, "60")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(61, "61")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(62, "62")
D3D12XS_DEFINE_CMD_QUEUE_SLOT_STUB(63, "63")

static const ID3D12CommandQueueXVtbl g_cmd_queue_vtbl = {
    {
        (void *)cmd_queue_query_interface,
        (void *)cmd_queue_add_ref,
        (void *)cmd_queue_release,
        (void *)cmd_queue_vtbl_slot_stub_3,
        (void *)cmd_queue_vtbl_slot_stub_4,
        (void *)cmd_queue_vtbl_slot_stub_5,
        (void *)cmd_queue_vtbl_slot_stub_6,
        (void *)cmd_queue_vtbl_slot_stub_7,
        (void *)cmd_queue_vtbl_slot_stub_8,
        (void *)cmd_queue_vtbl_slot_stub_9,
        (void *)cmd_queue_execute_command_lists, /* slot 10 (+0x50) */
        (void *)cmd_queue_vtbl_slot_stub_11,
        (void *)cmd_queue_vtbl_slot_stub_12,
        (void *)cmd_queue_vtbl_slot_stub_13,
        (void *)cmd_queue_signal,                /* slot 14 (+0x70) */
        (void *)cmd_queue_wait,                  /* slot 15 (+0x78) */
        (void *)cmd_queue_signal,                /* slot 16 (+0x80) */
        (void *)cmd_queue_wait,                  /* slot 17 (+0x88) */
        (void *)cmd_queue_get_timestamp_frequency,/* slot 18 (+0x90) */
        (void *)cmd_queue_vtbl_slot_stub_19,
        (void *)cmd_queue_vtbl_slot_stub_20,
        (void *)cmd_queue_vtbl_slot_stub_21,
        (void *)cmd_queue_vtbl_slot_stub_22,
        (void *)cmd_queue_vtbl_slot_stub_23,
        (void *)cmd_queue_vtbl_slot_stub_24,
        (void *)cmd_queue_vtbl_slot_stub_25,
        (void *)cmd_queue_vtbl_slot_stub_26,
        (void *)cmd_queue_vtbl_slot_stub_27,
        (void *)cmd_queue_vtbl_slot_stub_28,
        (void *)cmd_queue_vtbl_slot_stub_29,
        (void *)cmd_queue_vtbl_slot_stub_30,
        (void *)cmd_queue_slot31_ok,                /* slot 31 (+0xf8) */
        (void *)cmd_queue_slot32_present_pacing,    /* slot 32 (+0x100) */
        (void *)cmd_queue_vtbl_slot_stub_33,
        (void *)cmd_queue_vtbl_slot_stub_34,
        (void *)cmd_queue_execute_command_lists_x,  /* slot 35 (+0x118) */
        (void *)cmd_queue_vtbl_slot_stub_36,
        (void *)cmd_queue_vtbl_slot_stub_37,
        (void *)cmd_queue_vtbl_slot_stub_38,
        (void *)cmd_queue_vtbl_slot_stub_39,
        (void *)cmd_queue_vtbl_slot_stub_40,
        (void *)cmd_queue_vtbl_slot_stub_41,
        (void *)cmd_queue_vtbl_slot_stub_42,
        (void *)cmd_queue_vtbl_slot_stub_43,
        (void *)cmd_queue_vtbl_slot_stub_44,
        (void *)cmd_queue_vtbl_slot_stub_45,
        (void *)cmd_queue_vtbl_slot_stub_46,
        (void *)cmd_queue_vtbl_slot_stub_47,
        (void *)cmd_queue_vtbl_slot_stub_48,
        (void *)cmd_queue_vtbl_slot_stub_49,
        (void *)cmd_queue_vtbl_slot_stub_50,
        (void *)cmd_queue_vtbl_slot_stub_51,
        (void *)cmd_queue_vtbl_slot_stub_52,
        (void *)cmd_queue_vtbl_slot_stub_53,
        (void *)cmd_queue_vtbl_slot_stub_54,
        (void *)cmd_queue_vtbl_slot_stub_55,
        (void *)cmd_queue_vtbl_slot_stub_56,
        (void *)cmd_queue_vtbl_slot_stub_57,
        (void *)cmd_queue_vtbl_slot_stub_58,
        (void *)cmd_queue_vtbl_slot_stub_59,
        (void *)cmd_queue_vtbl_slot_stub_60,
        (void *)cmd_queue_vtbl_slot_stub_61,
        (void *)cmd_queue_vtbl_slot_stub_62,
        (void *)cmd_queue_vtbl_slot_stub_63
    }
};

static scarlett_hresult_t com_create_command_queue_impl(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppCommandQueue,
    uint32_t vtable_slot,
    uint64_t return_address) {
    if (self == NULL || pDesc == NULL || ppCommandQueue == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppCommandQueue = NULL;

    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12CommandQueue) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateCommandQueue(unknown_iid)",
            vtable_slot,
            return_address,
            (uint64_t)(uintptr_t)self,
            (uint64_t)(uintptr_t)pDesc,
            (uint64_t)(uintptr_t)riid,
            (uint64_t)(uintptr_t)ppCommandQueue,
            SCARLETT_E_NOINTERFACE,
            NULL);
        return SCARLETT_E_NOINTERFACE;
    }

    ID3D12CommandQueueX *q = NULL;
    d3d12xs_cmd_queue_meta_t *qm = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_QUEUES; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_cmd_queue_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            q = &g_com_cmd_queues[i];
            qm = &g_cmd_queue_meta[i];
            break;
        }
    }
    if (q == NULL || qm == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    const uint32_t *desc_u32 = (const uint32_t *)pDesc;
    uint32_t raw_type = desc_u32[0];
    uint32_t priority = desc_u32[1];
    uint32_t mapped_type = SCARLETT_D3D12_QUEUE_TYPE_DIRECT;
    if (raw_type == 0u) {
        mapped_type = SCARLETT_D3D12_QUEUE_TYPE_DIRECT;
    } else if (raw_type == 2u || raw_type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
        mapped_type = SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS;
    } else if (raw_type == 3u) {
        mapped_type = SCARLETT_D3D12_QUEUE_TYPE_DMA;
    }

    scarlett_d3d12xs_create_queue_params_t qp;
    scarlett_hresult_t hr = D3D12XboxCreateCommandQueueX(
        self->unix_device_handle,
        mapped_type,
        priority,
        &qp);
    if (scarlett_failed(hr)) {
        __atomic_store_n(&qm->in_use, 0u, __ATOMIC_RELEASE);
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateCommandQueue(failed)",
            vtable_slot,
            return_address,
            (uint64_t)(uintptr_t)self,
            (uint64_t)raw_type,
            (uint64_t)priority,
            (uint64_t)(uintptr_t)ppCommandQueue,
            hr,
            NULL);
        return hr;
    }

    d3d12xs_pe_zero(q, sizeof(*q));
    q->lpVtbl = &g_cmd_queue_vtbl;
    qm->unix_device_handle = self->unix_device_handle;
    qm->unix_queue_handle = qp.out_queue_handle;
    qm->owner_device = self;
    qm->real_obj = NULL;
    __atomic_store_n(&qm->ref_count, 1u, __ATOMIC_RELEASE);
    if (self->real_device != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccq_t)(
            void *, const void *, const scarlett_guid_t *, void **);
        void **vtbl = *(void ***)self->real_device;
        pfn_real_ccq_t fn = (pfn_real_ccq_t)vtbl[8];
        uint32_t real_desc[4];
        real_desc[0] = (raw_type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) ? 2u : raw_type;
        real_desc[1] = 0u;
        real_desc[2] = 0u;
        real_desc[3] = 0u;
        void *real_q = NULL;
        scarlett_hresult_t rhr = fn(
            self->real_device, real_desc, &kIidID3D12CommandQueue, &real_q);
        if (scarlett_succeeded(rhr) && real_q != NULL) {
            qm->real_obj = real_q;
        }
        static uint32_t g_vkd3d_q_logged = 0u;
        if (__atomic_exchange_n(&g_vkd3d_q_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
            d3d12xs_pe_vkd3d_log("CreateCommandQueue", qm->real_obj, rhr);
        }
    }
    *ppCommandQueue = (void *)q;

    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "CreateCommandQueue",
        vtable_slot,
        return_address,
        (uint64_t)(uintptr_t)self,
        (uint64_t)raw_type,
        (uint64_t)priority,
        (uint64_t)(uintptr_t)q,
        SCARLETT_OK,
        "(created)");
    return SCARLETT_OK;
}

/* Slot 8 (+0x40): ID3D12Device::CreateCommandQueue (falls back to ReservedResourceDebugX stub if pDesc == NULL) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot8_create_command_queue(
    ID3D12DeviceX *self,
    const void *pDesc,
    uint64_t arg3,
    uint64_t arg4,
    void **arg5) {
    if (pDesc == NULL) {
        return com_create_reserved_resource_debug_x_stub(
            self,
            pDesc,
            (uint32_t)arg3,
            (const scarlett_guid_t *)(uintptr_t)arg4,
            arg5);
    }
    return com_create_command_queue_impl(
        self,
        pDesc,
        (const scarlett_guid_t *)(uintptr_t)arg3,
        (void **)(uintptr_t)arg4,
        8u,
        (uint64_t)(uintptr_t)__builtin_return_address(0));
}

/* Slot 48 (+0x180): Xbox ID3D12DeviceX::CreateCommandQueueX / CreateCommandQueue1 */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot48_create_command_queue(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppCommandQueue) {
    return com_create_command_queue_impl(
        self,
        pDesc,
        riid,
        ppCommandQueue,
        48u,
        (uint64_t)(uintptr_t)__builtin_return_address(0));
}

/* ============================================================================
 * ID3D12CommandAllocatorX, ID3D12FenceX, ID3D12GraphicsCommandListX Pools
 * ============================================================================ */

typedef struct ID3D12CommandAllocatorXVtbl_S {
    void *slots[16];
} ID3D12CommandAllocatorXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12CommandAllocatorX_S {
    const ID3D12CommandAllocatorXVtbl *lpVtbl;
    uint8_t inline_scratchpad[256u - sizeof(void *)];
} ID3D12CommandAllocatorX;

typedef struct d3d12xs_cmd_alloc_meta_s {
    uint64_t unix_device_handle;
    uint32_t queue_type;
    uint32_t ref_count;
    uint32_t in_use;
    uint32_t reserved0;
    void *real_obj;
} d3d12xs_cmd_alloc_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_ALLOCATORS 2048u
static ID3D12CommandAllocatorX g_com_cmd_allocators[SCARLETT_D3D12XS_MAX_COM_ALLOCATORS];
static d3d12xs_cmd_alloc_meta_t g_cmd_alloc_meta[SCARLETT_D3D12XS_MAX_COM_ALLOCATORS];

static inline d3d12xs_cmd_alloc_meta_t *d3d12xs_get_cmd_alloc_meta(const void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_cmd_allocators[0];
    uintptr_t end = (uintptr_t)&g_com_cmd_allocators[SCARLETT_D3D12XS_MAX_COM_ALLOCATORS];
    if (p < base || p >= end || ((p - base) % sizeof(ID3D12CommandAllocatorX)) != 0u) {
        return NULL;
    }
    size_t idx = (size_t)((const ID3D12CommandAllocatorX *)ptr - &g_com_cmd_allocators[0]);
    return &g_cmd_alloc_meta[idx];
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_alloc_query_interface(
    ID3D12CommandAllocatorX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(self);
    if (am == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12CommandAllocator) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&am->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_alloc_add_ref(ID3D12CommandAllocatorX *self) {
    d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(self);
    if (am == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&am->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_alloc_release(ID3D12CommandAllocatorX *self) {
    d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(self);
    if (am == NULL || __atomic_load_n(&am->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&am->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (am->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)am->real_obj;
            ((pfn_rel_t)vtbl[2])(am->real_obj);
            am->real_obj = NULL;
        }
        am->unix_device_handle = 0u;
        __atomic_store_n(&am->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_alloc_reset(ID3D12CommandAllocatorX *self) {
    d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(self);
    if (am == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (am->real_obj != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_alloc_reset_t)(void *);
        void **vtbl = *(void ***)am->real_obj;
        pfn_real_alloc_reset_t fn = (pfn_real_alloc_reset_t)vtbl[8];
        scarlett_hresult_t hr = fn(am->real_obj);
        if (scarlett_failed(hr)) {
            return hr;
        }
    }
    return SCARLETT_OK;
}

#define D3D12XS_DEFINE_SLOT_STUB(PREFIX, LABEL, SLOT_NUM, SLOT_STR)            \
    static scarlett_hresult_t SCARLETT_D3D12XS_CALL                            \
        PREFIX##_vtbl_slot_stub_##SLOT_NUM(                                    \
            uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {            \
        static const scarlett_stub_descriptor_t kDesc = {                      \
            SCARLETT_STUB_THUNK_ABI_VERSION,                                   \
            SCARLETT_STUB_FLAG_XDL_VTABLE,                                     \
            SCARLETT_STUB_ORDINAL_NONE,                                        \
            (uint32_t)(SLOT_NUM),                                              \
            SCARLETT_ERR_UNIMPLEMENTED_STUB,                                   \
            0u,                                                                \
            "d3d12_xs.dll",                                                    \
            LABEL "::Slot" SLOT_STR                                            \
        };                                                                     \
        return d3d12xs_pe_dispatch_stub(                                       \
            &kDesc,                                                            \
            (uint64_t)(uintptr_t)__builtin_return_address(0),                  \
            rcx, rdx, r8, r9);                                                 \
    }

D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(cmd_alloc, "ID3D12CommandAllocatorX", 15, "15")

/* Slot 9 (+0x48): Xbox ID3D12CommandAllocatorX extension method (void / SCARLETT_OK) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_alloc_slot9_ok(
    ID3D12CommandAllocatorX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    static uint32_t g_alloc_slot9_logged = 0u;
    if (__atomic_exchange_n(&g_alloc_slot9_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12CommandAllocatorX::Slot9",
            9u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            rdx,
            r8,
            r9,
            SCARLETT_OK,
            "(ok)");
    }
    return SCARLETT_OK;
}

static const ID3D12CommandAllocatorXVtbl g_cmd_alloc_vtbl = {
    {
        (void *)cmd_alloc_query_interface,
        (void *)cmd_alloc_add_ref,
        (void *)cmd_alloc_release,
        (void *)cmd_alloc_vtbl_slot_stub_3,
        (void *)cmd_alloc_vtbl_slot_stub_4,
        (void *)cmd_alloc_vtbl_slot_stub_5,
        (void *)cmd_alloc_vtbl_slot_stub_6,
        (void *)cmd_alloc_vtbl_slot_stub_7,
        (void *)cmd_alloc_reset, /* slot 8: Reset */
        (void *)cmd_alloc_slot9_ok,
        (void *)cmd_alloc_vtbl_slot_stub_10,
        (void *)cmd_alloc_vtbl_slot_stub_11,
        (void *)cmd_alloc_vtbl_slot_stub_12,
        (void *)cmd_alloc_vtbl_slot_stub_13,
        (void *)cmd_alloc_vtbl_slot_stub_14,
        (void *)cmd_alloc_vtbl_slot_stub_15
    }
};

typedef struct ID3D12FenceXVtbl_S {
    void *slots[16];
} ID3D12FenceXVtbl;

typedef struct ID3D12FenceXWaiter_S {
    uint64_t value;
    void *event_handle;
    uint32_t active;
    uint32_t reserved0;
} ID3D12FenceXWaiter;

#define SCARLETT_D3D12XS_MAX_FENCE_WAITERS 16u

typedef struct __attribute__((aligned(16))) ID3D12FenceX_S {
    const ID3D12FenceXVtbl *lpVtbl;
    uint8_t inline_scratchpad[256u - sizeof(void *)];
} ID3D12FenceX;

typedef struct d3d12xs_fence_meta_s {
    uint64_t unix_device_handle;
    uint64_t current_value;
    uint32_t ref_count;
    uint32_t in_use;
    ID3D12FenceXWaiter waiters[SCARLETT_D3D12XS_MAX_FENCE_WAITERS];
    void *real_obj;
} d3d12xs_fence_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_FENCES 256u
static ID3D12FenceX g_com_fences[SCARLETT_D3D12XS_MAX_COM_FENCES];
static d3d12xs_fence_meta_t g_fence_meta[SCARLETT_D3D12XS_MAX_COM_FENCES];

static inline d3d12xs_fence_meta_t *d3d12xs_get_fence_meta(const void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_fences[0];
    uintptr_t end = (uintptr_t)&g_com_fences[SCARLETT_D3D12XS_MAX_COM_FENCES];
    if (p < base || p >= end || ((p - base) % sizeof(ID3D12FenceX)) != 0u) {
        return NULL;
    }
    size_t idx = (size_t)((const ID3D12FenceX *)ptr - &g_com_fences[0]);
    return &g_fence_meta[idx];
}

static inline bool d3d12xs_is_com_fence(const void *ptr) {
    return d3d12xs_get_fence_meta(ptr) != NULL;
}

static void *d3d12xs_unwrap_fence_real(const ID3D12FenceX *f) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(f);
    return (fm != NULL) ? fm->real_obj : NULL;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL fence_query_interface(
    ID3D12FenceX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    if (fm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12Fence) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&fm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL fence_add_ref(ID3D12FenceX *self) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    if (fm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&fm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL fence_release(ID3D12FenceX *self) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    if (fm == NULL || __atomic_load_n(&fm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&fm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (fm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)fm->real_obj;
            ((pfn_rel_t)vtbl[2])(fm->real_obj);
            fm->real_obj = NULL;
        }
        fm->unix_device_handle = 0u;
        fm->current_value = 0u;
        __atomic_store_n(&fm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

static uint64_t SCARLETT_D3D12XS_CALL fence_get_completed_value(ID3D12FenceX *self) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    uint64_t val = (fm != NULL) ? __atomic_load_n(&fm->current_value, __ATOMIC_ACQUIRE) : 0u;
    if (fm != NULL && fm->real_obj != NULL) {
        typedef uint64_t (SCARLETT_D3D12XS_CALL *pfn_real_gcv_t)(void *);
        void **vtbl = *(void ***)fm->real_obj;
        uint64_t rval = ((pfn_real_gcv_t)vtbl[8])(fm->real_obj);
        if (rval > val) {
            val = rval;
        }
    }
    static uint32_t g_fget_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_fget_seq, 1u, __ATOMIC_RELAXED);
    if (seq <= 12u || (seq % 500u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12FenceX::GetCompletedValue",
            8u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            val,
            (uint64_t)seq,
            0u,
            SCARLETT_OK,
            "(trace)");
    }
    return val;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL fence_set_event_on_completion(
    ID3D12FenceX *self,
    uint64_t Value,
    void *hEvent) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    if (fm == NULL) {
        return SCARLETT_E_POINTER;
    }
    uint64_t cur = __atomic_load_n(&fm->current_value, __ATOMIC_ACQUIRE);
    static uint32_t g_fwait_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_fwait_seq, 1u, __ATOMIC_RELAXED);
    if (seq <= 16u || (seq % 100u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12FenceX::SetEventOnCompletion",
            9u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            Value,
            cur,
            (uint64_t)(uintptr_t)hEvent,
            SCARLETT_OK,
            (cur >= Value) ? "(immediate)" : "(queued)");
    }
    if (cur >= Value) {
#if defined(_WIN32) || defined(__MINGW32__)
        if (hEvent != NULL) {
            d3d12xs_pe_resolve_kernel32();
            scarlett_set_event_fn_t sev = __atomic_load_n(&g_pfn_SetEvent, __ATOMIC_ACQUIRE);
            if (sev != NULL) {
                (void)sev(hEvent);
            }
        }
#else
        (void)hEvent;
#endif
        return SCARLETT_OK;
    }
    if (fm->real_obj != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_seoc_t)(void *, uint64_t, void *);
        void **vtbl = *(void ***)fm->real_obj;
        (void)((pfn_real_seoc_t)vtbl[9])(fm->real_obj, Value, hEvent);
    }
    if (hEvent != NULL) {
        for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_FENCE_WAITERS; ++i) {
            if (fm->waiters[i].active == 0u) {
                fm->waiters[i].value = Value;
                fm->waiters[i].event_handle = hEvent;
                fm->waiters[i].active = 1u;
                break;
            }
        }
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL fence_signal(
    ID3D12FenceX *self,
    uint64_t Value) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    if (fm == NULL) {
        return SCARLETT_E_POINTER;
    }
    __atomic_store_n(&fm->current_value, Value, __ATOMIC_RELEASE);
#if defined(_WIN32) || defined(__MINGW32__)
    d3d12xs_pe_resolve_kernel32();
    scarlett_set_event_fn_t sev = __atomic_load_n(&g_pfn_SetEvent, __ATOMIC_ACQUIRE);
#endif
    uint32_t woken = 0u;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_FENCE_WAITERS; ++i) {
        if (fm->waiters[i].active != 0u && Value >= fm->waiters[i].value) {
#if defined(_WIN32) || defined(__MINGW32__)
            if (sev != NULL && fm->waiters[i].event_handle != NULL) {
                (void)sev(fm->waiters[i].event_handle);
                woken++;
            }
#endif
            fm->waiters[i].active = 0u;
            fm->waiters[i].event_handle = NULL;
        }
    }
    static uint32_t g_fsig_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_fsig_seq, 1u, __ATOMIC_RELAXED);
    if (seq <= 16u || (seq % 100u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12FenceX::Signal",
            10u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            Value,
            (uint64_t)woken,
            (uint64_t)seq,
            SCARLETT_OK,
            "(trace)");
    }
    return SCARLETT_OK;
}

static uint32_t SCARLETT_D3D12XS_CALL fence_get_creation_flags(ID3D12FenceX *self) {
    (void)self;
    return 0u;
}

static void * SCARLETT_D3D12XS_CALL fence_get_completed_value_address(ID3D12FenceX *self) {
    d3d12xs_fence_meta_t *fm = d3d12xs_get_fence_meta(self);
    return (fm != NULL) ? (void *)&fm->current_value : NULL;
}

D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(fence, "ID3D12FenceX", 15, "15")

static const ID3D12FenceXVtbl g_fence_vtbl = {
    {
        (void *)fence_query_interface,
        (void *)fence_add_ref,
        (void *)fence_release,
        (void *)fence_vtbl_slot_stub_3,
        (void *)fence_vtbl_slot_stub_4,
        (void *)fence_vtbl_slot_stub_5,
        (void *)fence_vtbl_slot_stub_6,
        (void *)fence_vtbl_slot_stub_7,
        (void *)fence_get_completed_value,         /* slot 8 */
        (void *)fence_set_event_on_completion,     /* slot 9 */
        (void *)fence_signal,                      /* slot 10 */
        (void *)fence_get_creation_flags,          /* slot 11 (+0x58) */
        (void *)fence_get_completed_value_address, /* slot 12 (+0x60) */
        (void *)fence_vtbl_slot_stub_13,
        (void *)fence_vtbl_slot_stub_14,
        (void *)fence_vtbl_slot_stub_15
    }
};

typedef struct scarlett_d3d12_resource_desc_s {
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
} scarlett_d3d12_resource_desc_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12_resource_desc_t) == 56,
                       "scarlett_d3d12_resource_desc_t must be 56 bytes");

typedef struct scarlett_d3d12_subresource_footprint_s {
    uint32_t Format;
    uint32_t Width;
    uint32_t Height;
    uint32_t Depth;
    uint32_t RowPitch;
} scarlett_d3d12_subresource_footprint_t;

typedef struct scarlett_d3d12_placed_subresource_footprint_s {
    uint64_t Offset;
    scarlett_d3d12_subresource_footprint_t Footprint;
} scarlett_d3d12_placed_subresource_footprint_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12_placed_subresource_footprint_t) == 32,
                       "scarlett_d3d12_placed_subresource_footprint_t must be 32 bytes");

typedef struct scarlett_d3d12_texture_copy_location_s {
    void *pResource;
    uint32_t Type; /* 0 = SubresourceIndex, 1 = PlacedFootprint */
    uint32_t _pad0;
    union {
        scarlett_d3d12_placed_subresource_footprint_t PlacedFootprint;
        uint32_t SubresourceIndex;
    };
} scarlett_d3d12_texture_copy_location_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12_texture_copy_location_t) == 48,
                       "scarlett_d3d12_texture_copy_location_t must be 48 bytes");

typedef struct scarlett_d3d12_box_s {
    uint32_t left;
    uint32_t top;
    uint32_t front;
    uint32_t right;
    uint32_t bottom;
    uint32_t back;
} scarlett_d3d12_box_t;

typedef struct scarlett_d3d12_write_buf_param_s {
    uint64_t Dest;
    uint32_t Value;
    uint32_t _pad0;
} scarlett_d3d12_write_buf_param_t;

static inline uint32_t d3d12xs_format_bpp(uint32_t fmt) {
    if (fmt >= 1u && fmt <= 4u) return 16u;  /* R32G32B32A32 */
    if (fmt >= 5u && fmt <= 8u) return 12u;  /* R32G32B32 */
    if (fmt >= 9u && fmt <= 22u) return 8u;  /* R16G16B16A16 / R32G32 / D32S8 */
    if (fmt >= 23u && fmt <= 47u) return 4u; /* R10G10B10A2 / R11G11B10 / R8G8B8A8 / R32 / D32 / D24S8 */
    if (fmt >= 87u && fmt <= 93u) return 4u; /* B8G8R8A8 / B8G8R8X8 */
    if (fmt >= 48u && fmt <= 59u) return 2u; /* R8G8 / R16 / D16 */
    if (fmt == 85u || fmt == 86u) return 2u; /* B5G6R5 / B5G5R5A1 */
    if (fmt >= 60u && fmt <= 69u) return 1u; /* R8 / A8 */
    return 4u;
}

static inline void d3d12xs_format_layout(
    uint32_t fmt,
    uint32_t width,
    uint32_t height,
    uint32_t *out_row_bytes,
    uint32_t *out_num_rows) {
    uint32_t w = (width > 0u) ? width : 1u;
    uint32_t h = (height > 0u) ? height : 1u;
    if ((fmt >= 70u && fmt <= 72u) || (fmt >= 79u && fmt <= 81u)) {
        /* BC1 / BC4: 8 bytes per 4x4 block */
        *out_row_bytes = ((w + 3u) / 4u) * 8u;
        *out_num_rows = (h + 3u) / 4u;
        return;
    }
    if ((fmt >= 73u && fmt <= 78u) || (fmt >= 82u && fmt <= 84u) ||
        (fmt >= 94u && fmt <= 99u)) {
        /* BC2 / BC3 / BC5 / BC6H / BC7: 16 bytes per 4x4 block */
        *out_row_bytes = ((w + 3u) / 4u) * 16u;
        *out_num_rows = (h + 3u) / 4u;
        return;
    }
    *out_row_bytes = w * d3d12xs_format_bpp(fmt);
    *out_num_rows = h;
}

static bool d3d12xs_resource_get_info(
    const void *ptr,
    uint64_t *out_va,
    scarlett_d3d12_resource_desc_t *out_desc);

static void *d3d12xs_resource_get_real(const void *ptr);
static void d3d12xs_resource_mark_gpu_dirty(const void *ptr);

typedef struct d3d12xs_label_write_s {
    uint64_t gpu_va;
    uint32_t value;
    uint32_t reserved;
} d3d12xs_label_write_t;

#define SCARLETT_D3D12XS_MAX_GCL_LABELS 1024u

typedef struct ID3D12GraphicsCommandListXVtbl_S {
    void *slots[160];
} ID3D12GraphicsCommandListXVtbl;

typedef struct __attribute__((aligned(64))) ID3D12GraphicsCommandListX_S {
    const ID3D12GraphicsCommandListXVtbl *lpVtbl; /* +0x00 */
    uint64_t reserved_08;                         /* +0x08 */
    uint64_t reserved_10;                         /* +0x10 */
    void *pCurrent;                               /* +0x18: PM4 write pointer */
    void *pBegin;                                 /* +0x20: chunk begin */
    uint64_t reserved_chunk;                      /* +0x28 */
    void *pEnd;                                   /* +0x30: chunk end pointer */
    uint8_t inline_scratchpad[4096u - 0x38u];     /* +0x38..+0x1000: Xbox inline state */
} ID3D12GraphicsCommandListX;

SCARLETT_STATIC_ASSERT(offsetof(ID3D12GraphicsCommandListX, pCurrent) == 0x18,
                       "ID3D12GraphicsCommandListX::pCurrent must be at +0x18");
SCARLETT_STATIC_ASSERT(offsetof(ID3D12GraphicsCommandListX, pBegin) == 0x20,
                       "ID3D12GraphicsCommandListX::pBegin must be at +0x20");
SCARLETT_STATIC_ASSERT(offsetof(ID3D12GraphicsCommandListX, pEnd) == 0x30,
                       "ID3D12GraphicsCommandListX::pEnd must be at +0x30");
SCARLETT_STATIC_ASSERT(sizeof(ID3D12GraphicsCommandListX) == 4096u,
                       "ID3D12GraphicsCommandListX must be 4096 bytes");

typedef struct d3d12xs_cmd_list_meta_s {
    uint64_t unix_device_handle;
    uint64_t unix_cmd_list_handle;
    uint32_t list_type;
    uint32_t ref_count;
    uint32_t in_use;
    uint32_t label_write_count;
    uint32_t label_capacity;
    uint32_t pm4_logged;
    uint32_t bound_rtv_count;
    uint32_t has_gpu_write_cmds;
    uint32_t current_topology;
    uint32_t reserved_pad;
    void *real_obj;
    void *bound_rtvs[8];
    void *bound_srv_heap;
    void *bound_sampler_heap;
    void *bound_gfx_rs;
    void *bound_comp_rs;
    d3d12xs_label_write_t *label_writes;
    void *bound_pm4_begin;
    void *bound_pm4_end;
} d3d12xs_cmd_list_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_CMD_LISTS 2048u
#define SCARLETT_D3D12XS_PM4_SLICE_BYTES 65536u
static ID3D12GraphicsCommandListX *g_com_cmd_lists = NULL;
static d3d12xs_cmd_list_meta_t *g_cmd_list_meta = NULL;
static uint32_t g_cmd_list_pool_init_state = 0u;

static bool d3d12xs_ensure_cmd_list_pool(void) {
    if (__atomic_load_n(&g_cmd_list_pool_init_state, __ATOMIC_ACQUIRE) == 2u) {
        return g_com_cmd_lists != NULL && g_cmd_list_meta != NULL;
    }
    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(
            &g_cmd_list_pool_init_state, &expected, 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        g_com_cmd_lists = (ID3D12GraphicsCommandListX *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_MAX_COM_CMD_LISTS * sizeof(ID3D12GraphicsCommandListX));
        g_cmd_list_meta = (d3d12xs_cmd_list_meta_t *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_MAX_COM_CMD_LISTS * sizeof(d3d12xs_cmd_list_meta_t));
        __atomic_store_n(&g_cmd_list_pool_init_state, 2u, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&g_cmd_list_pool_init_state, __ATOMIC_ACQUIRE) != 2u) {
        }
    }
    return g_com_cmd_lists != NULL && g_cmd_list_meta != NULL;
}

static inline d3d12xs_cmd_list_meta_t *d3d12xs_get_cmd_list_meta(
    const void *ptr, size_t *out_idx) {
    if (ptr == NULL || g_com_cmd_lists == NULL || g_cmd_list_meta == NULL) {
        return NULL;
    }
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_cmd_lists[0];
    uintptr_t end = (uintptr_t)&g_com_cmd_lists[SCARLETT_D3D12XS_MAX_COM_CMD_LISTS];
    if (p < base || p >= end || ((p - base) % sizeof(ID3D12GraphicsCommandListX)) != 0u) {
        return NULL;
    }
    size_t idx = (size_t)((p - base) / sizeof(ID3D12GraphicsCommandListX));
    if (out_idx != NULL) {
        *out_idx = idx;
    }
    return &g_cmd_list_meta[idx];
}

static void *d3d12xs_unwrap_cmd_list_real(const void *ptr) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(ptr, NULL);
    return (clm != NULL) ? clm->real_obj : NULL;
}
static uint8_t g_pm4_fallback_sink[64u * 1024u];
static uint8_t *g_pm4_pool_base = NULL;
static uint32_t g_pm4_pool_init_state = 0u; /* 0=uninit, 1=initing, 2=ready */

static void d3d12xs_pm4_assign_slice(
    ID3D12GraphicsCommandListX *cl,
    d3d12xs_cmd_list_meta_t *clm,
    size_t idx) {
    uint8_t *base = __atomic_load_n(&g_pm4_pool_base, __ATOMIC_ACQUIRE);
    if (base == NULL) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_pm4_pool_init_state, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            uint8_t *alloc = NULL;
#if defined(_WIN32) || defined(__MINGW32__)
            d3d12xs_pe_resolve_kernel32();
            scarlett_virtual_alloc_fn_t va =
                __atomic_load_n(&g_pfn_VirtualAlloc, __ATOMIC_ACQUIRE);
            if (va != NULL) {
                alloc = (uint8_t *)va(
                    NULL,
                    (size_t)SCARLETT_D3D12XS_MAX_COM_CMD_LISTS * SCARLETT_D3D12XS_PM4_SLICE_BYTES,
                    0x3000u,
                    0x04u);
            }
#endif
            if (alloc == NULL) {
                alloc = g_pm4_fallback_sink;
            }
            __atomic_store_n(&g_pm4_pool_base, alloc, __ATOMIC_RELEASE);
            __atomic_store_n(&g_pm4_pool_init_state, 2u, __ATOMIC_RELEASE);
            base = alloc;
        } else {
            while (__atomic_load_n(&g_pm4_pool_init_state, __ATOMIC_ACQUIRE) != 2u) {
            }
            base = __atomic_load_n(&g_pm4_pool_base, __ATOMIC_ACQUIRE);
        }
    }
    if (base == g_pm4_fallback_sink) {
        cl->pBegin = g_pm4_fallback_sink;
        cl->pCurrent = g_pm4_fallback_sink;
        cl->pEnd = g_pm4_fallback_sink;
        if (clm != NULL) {
            clm->bound_pm4_begin = cl->pBegin;
            clm->bound_pm4_end = g_pm4_fallback_sink + sizeof(g_pm4_fallback_sink) - 4096u;
        }
    } else {
        uint8_t *slice = base + (idx % SCARLETT_D3D12XS_MAX_COM_CMD_LISTS) * SCARLETT_D3D12XS_PM4_SLICE_BYTES;
        cl->pBegin = slice;
        cl->pCurrent = slice;
        /* Set pEnd == pCurrent so inline PM4 fastpaths route through COM vtable slots */
        cl->pEnd = slice;
        if (clm != NULL) {
            clm->bound_pm4_begin = cl->pBegin;
            clm->bound_pm4_end = slice + SCARLETT_D3D12XS_PM4_SLICE_BYTES - 4096u;
        }
    }
    cl->reserved_chunk = 0u;
}

static inline bool d3d12xs_is_cmd_list_iid(const scarlett_guid_t *riid) {
    return d3d12xs_guid_eq(riid, &kIidID3D12GraphicsCommandList) ||
           d3d12xs_guid_eq(riid, &kIidID3D12GraphicsCommandList_Std) ||
           d3d12xs_guid_eq(riid, &kIidID3D12CommandList) ||
           d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
           d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
           d3d12xs_guid_eq(riid, &kIidIUnknown);
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_query_interface(
    ID3D12GraphicsCommandListX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_is_cmd_list_iid(riid)) {
        __atomic_add_fetch(&clm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    d3d12xs_pe_log_guid_ods(
        "ID3D12GraphicsCommandListX::QueryInterface",
        0u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        riid,
        SCARLETT_E_NOINTERFACE);
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_list_add_ref(ID3D12GraphicsCommandListX *self) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&clm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_list_release(ID3D12GraphicsCommandListX *self) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || __atomic_load_n(&clm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&clm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (clm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)clm->real_obj;
            ((pfn_rel_t)vtbl[2])(clm->real_obj);
            clm->real_obj = NULL;
        }
        if (clm->label_writes != NULL) {
            d3d12xs_pe_free_mem(clm->label_writes);
            clm->label_writes = NULL;
        }
        clm->label_write_count = 0u;
    clm->bound_rtv_count = 0u;
    clm->has_gpu_write_cmds = 0u;
    clm->current_topology = 0u;
    clm->bound_srv_heap = NULL;
    clm->bound_sampler_heap = NULL;
    clm->bound_gfx_rs = NULL;
    clm->bound_comp_rs = NULL;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));
        clm->label_capacity = 0u;
        clm->unix_device_handle = 0u;
        clm->unix_cmd_list_handle = 0u;
        __atomic_store_n(&clm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

static uint32_t SCARLETT_D3D12XS_CALL cmd_list_get_type(ID3D12GraphicsCommandListX *self) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    return (clm != NULL) ? clm->list_type : 0u;
}

static void d3d12xs_log_pm4_summary(
    ID3D12GraphicsCommandListX *cl,
    d3d12xs_cmd_list_meta_t *clm) {
    if (cl == NULL || clm == NULL || clm->bound_pm4_begin == NULL ||
        cl->pCurrent == NULL || cl->pCurrent <= clm->bound_pm4_begin) {
        return;
    }
    uintptr_t begin_p = (uintptr_t)clm->bound_pm4_begin;
    uintptr_t cur_p = (uintptr_t)cl->pCurrent;
    uintptr_t end_p = (uintptr_t)clm->bound_pm4_end;
    if (cur_p < begin_p || cur_p > end_p + 4096u) {
        return;
    }
    static uint32_t g_pm4_log_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_pm4_log_seq, 1u, __ATOMIC_RELAXED);
    if (seq > 16u && (seq % 200u) != 0u) {
        return;
    }
    uint32_t dwords = (uint32_t)((cur_p - begin_p) / 4u);
    const uint32_t *w = (const uint32_t *)begin_p;
    uint32_t w0 = (dwords > 0u) ? w[0] : 0u;
    uint32_t w1 = (dwords > 1u) ? w[1] : 0u;
    uint32_t w2 = (dwords > 2u) ? w[2] : 0u;
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "PM4CloseSummary",
        9u,
        (uint64_t)(uintptr_t)cl,
        (uint64_t)dwords,
        (uint64_t)w0,
        (uint64_t)w1,
        (uint64_t)w2,
        SCARLETT_OK,
        "(pm4)");
}

static void d3d12xs_cl_log(
    const char *op,
    uint32_t xbox_slot,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    const char *note,
    bool is_error);

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_close(ID3D12GraphicsCommandListX *self) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_log_pm4_summary(self, clm);
    if (clm->real_obj != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_close_t)(void *);
        void **vtbl = *(void ***)clm->real_obj;
        pfn_real_close_t fn = (pfn_real_close_t)vtbl[9];
        scarlett_hresult_t hr = fn(clm->real_obj);
        d3d12xs_cl_log("Close", 9u, (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)clm->real_obj,
                       (uint64_t)(uint32_t)hr, scarlett_succeeded(hr) ? "(forwarded)" : "(fail)",
                       scarlett_failed(hr));
        if (scarlett_failed(hr)) {
            return hr;
        }
    }
    return SCARLETT_OK;
}

static void d3d12xs_cmd_list_flush_labels(void *ptr) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(ptr, NULL);
    if (clm == NULL || clm->label_writes == NULL) {
        return;
    }
    uint32_t count = clm->label_write_count;
    if (count > clm->label_capacity) {
        count = clm->label_capacity;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        uint64_t va = clm->label_writes[i].gpu_va;
        uint32_t val = clm->label_writes[i].value;
        if ((va & UINT64_C(3)) != 0u || !d3d12xs_pe_is_writable_va(va, 4u)) {
            static uint32_t g_bad_lbl_seq = 0u;
            uint32_t bseq = __atomic_add_fetch(&g_bad_lbl_seq, 1u, __ATOMIC_RELAXED);
            if (bseq <= 8u) {
                d3d12xs_pe_log_stub_ods(
                    "d3d12_xs.dll",
                    "FlushGpuLabels(invalid_va)",
                    SCARLETT_STUB_SLOT_NONE,
                    (uint64_t)(uintptr_t)__builtin_return_address(0),
                    (uint64_t)(uintptr_t)ptr,
                    va,
                    (uint64_t)val,
                    (uint64_t)i,
                    SCARLETT_E_INVALIDARG,
                    "(skipped)");
            }
            continue;
        }
        __atomic_store_n((volatile uint32_t *)(uintptr_t)va, val, __ATOMIC_RELEASE);
        static uint32_t g_lflush_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_lflush_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 12u || (seq % 250u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "FlushGpuLabels",
                SCARLETT_STUB_SLOT_NONE,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)ptr,
                va,
                (uint64_t)val,
                (uint64_t)seq,
                SCARLETT_OK,
                "(flushed)");
        }
    }
    clm->label_write_count = 0u;
    clm->bound_rtv_count = 0u;
    clm->has_gpu_write_cmds = 0u;
    clm->current_topology = 0u;
    clm->bound_srv_heap = NULL;
    clm->bound_sampler_heap = NULL;
    clm->bound_gfx_rs = NULL;
    clm->bound_comp_rs = NULL;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));
}

static void d3d12xs_cmd_list_record_label(
    ID3D12GraphicsCommandListX *cl,
    uint64_t gpu_va,
    uint32_t value) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(cl, NULL);
    if (clm == NULL || (gpu_va & UINT64_C(3)) != 0u || !d3d12xs_pe_is_writable_va(gpu_va, 4u)) {
        return;
    }
    if (clm->label_writes == NULL) {
        clm->label_writes = (d3d12xs_label_write_t *)d3d12xs_pe_alloc_zero(
            SCARLETT_D3D12XS_MAX_GCL_LABELS * sizeof(d3d12xs_label_write_t));
        if (clm->label_writes == NULL) {
            return;
        }
        clm->label_capacity = SCARLETT_D3D12XS_MAX_GCL_LABELS;
    }
    if (clm->label_write_count >= clm->label_capacity) {
        d3d12xs_cmd_list_flush_labels(cl);
    }
    uint32_t idx = clm->label_write_count++;
    clm->label_writes[idx].gpu_va = gpu_va;
    clm->label_writes[idx].value = value;
    clm->label_writes[idx].reserved = 0u;
}

static void *d3d12xs_pso_get_real(const void *ptr);
static void d3d12xs_cl_log(
    const char *op,
    uint32_t xbox_slot,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    const char *note,
    bool is_error);

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_reset(
    ID3D12GraphicsCommandListX *self,
    void *pAllocator,
    void *pInitialState) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return SCARLETT_E_POINTER;
    }
    clm->label_write_count = 0u;
    clm->bound_rtv_count = 0u;
    clm->has_gpu_write_cmds = 0u;
    clm->current_topology = 0u;
    clm->bound_srv_heap = NULL;
    clm->bound_sampler_heap = NULL;
    clm->bound_gfx_rs = NULL;
    clm->bound_comp_rs = NULL;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));
    if (clm->bound_pm4_begin != NULL) {
        self->pBegin = clm->bound_pm4_begin;
        self->pCurrent = clm->bound_pm4_begin;
        /* Keep pEnd == pCurrent so inline PM4 fastpaths route through COM vtable slots */
        self->pEnd = clm->bound_pm4_begin;
    }
    if (clm->real_obj != NULL) {
        d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(pAllocator);
        void *real_alloc = (am != NULL) ? am->real_obj : NULL;
        void *real_pso = (pInitialState != NULL) ? d3d12xs_pso_get_real(pInitialState) : NULL;
        if (real_alloc != NULL) {
            typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_reset_t)(
                void *, void *, void *);
            void **vtbl = *(void ***)clm->real_obj;
            pfn_real_reset_t fn = (pfn_real_reset_t)vtbl[10];
            scarlett_hresult_t hr = fn(clm->real_obj, real_alloc, real_pso);
            if (scarlett_failed(hr)) {
                return hr;
            }
        }
    }
    return SCARLETT_OK;
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot20_ia_set_primitive_topology(
    ID3D12GraphicsCommandListX *self,
    uint32_t PrimitiveTopology) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    clm->current_topology = PrimitiveTopology;
    uint32_t pc_topology = PrimitiveTopology;
    if (PrimitiveTopology == 17u || PrimitiveTopology == 18u) {
        /* Xbox D3D_PRIMITIVE_TOPOLOGY_RECTLIST (0x11) / QUADLIST (0x12) -> TRIANGLELIST */
        pc_topology = 4u;
    }
    d3d12xs_cl_log("IASetPrimitiveTopology", 20u, PrimitiveTopology, pc_topology, 0u,
                   "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[20])(clm->real_obj, pc_topology);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot25_set_pipeline_state(
    ID3D12GraphicsCommandListX *self,
    void *pPipelineState) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    void *real_pso = (pPipelineState != NULL) ? d3d12xs_pso_get_real(pPipelineState) : NULL;
    if (pPipelineState != NULL && real_pso == NULL) {
        d3d12xs_cl_log("SetPipelineState", 25u, (uint64_t)(uintptr_t)pPipelineState,
                       0u, 0u, "(no_real_pso)", true);
        return;
    }
    d3d12xs_cl_log("SetPipelineState", 25u, (uint64_t)(uintptr_t)pPipelineState,
                   (uint64_t)(uintptr_t)real_pso, 0u, "(forwarded)", false);
    static uint32_t g_pso_bind_diag = 0u;
    if (__atomic_add_fetch(&g_pso_bind_diag, 1u, __ATOMIC_RELAXED) <= 6u && pPipelineState != NULL) {
        d3d12xs_pso_meta_t *pm = d3d12xs_get_pso_meta(pPipelineState);
        if (pm != NULL) {
            d3d12xs_pe_log_stub_ods("d3d12_xs.dll", "PSO_Bind_Diag", 25u,
                (uint64_t)(uintptr_t)pPipelineState, pm->saved_vs_len, pm->saved_ps_len,
                pm->lifted_spirv_bytes, (uint64_t)(uintptr_t)pm->saved_real_rs,
                SCARLETT_OK, "(pso)");
        }
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[25])(clm->real_obj, real_pso);
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_slot_trace_common(
    uint32_t slot, uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9, uint64_t ra) {
    static uint32_t g_cl_slot_logged[160];
    if (slot < 160u && __atomic_exchange_n(&g_cl_slot_logged[slot], 1u, __ATOMIC_ACQ_REL) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12GraphicsCommandListX::SlotTrace",
            slot,
            ra,
            rcx,
            rdx,
            r8,
            r9,
            SCARLETT_OK,
            "(trace-once)");
    }
    return SCARLETT_OK;
}

#define D3D12XS_DEFINE_CL_TRACE(N)                                             \
    static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_trace_##N(        \
        uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {                \
        return cmd_list_slot_trace_common(                                     \
            (uint32_t)(N), rcx, rdx, r8, r9,                                   \
            (uint64_t)(uintptr_t)__builtin_return_address(0));                 \
    }

static void d3d12xs_enqueue_cpu_upload_res(
    const void *res_ptr, uint64_t offset, uint64_t size);
static void d3d12xs_enqueue_cpu_upload_va(uint64_t xbox_va, uint64_t size);

static void SCARLETT_D3D12XS_CALL cmd_list_slot15_copy_buffer_region(
    ID3D12GraphicsCommandListX *self,
    void *pDstBuffer,
    uint64_t DstOffset,
    void *pSrcBuffer,
    uint64_t SrcOffset,
    uint64_t NumBytes) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || NumBytes == 0u) {
        return;
    }
    void *real_dst = d3d12xs_resource_get_real(pDstBuffer);
    void *real_src = d3d12xs_resource_get_real(pSrcBuffer);
    if (clm->real_obj != NULL && real_dst != NULL && real_src != NULL) {
        d3d12xs_enqueue_cpu_upload_res(pSrcBuffer, SrcOffset, NumBytes);
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_cbr_t)(
            void *, void *, uint64_t, void *, uint64_t, uint64_t);
        void **vtbl = *(void ***)clm->real_obj;
        ((pfn_real_cbr_t)vtbl[15])(
            clm->real_obj, real_dst, DstOffset, real_src, SrcOffset, NumBytes);
        d3d12xs_resource_mark_gpu_dirty(pDstBuffer);
    }
    uint64_t dst_va = 0u;
    uint64_t src_va = 0u;
    if (!d3d12xs_resource_get_info(pDstBuffer, &dst_va, NULL) ||
        !d3d12xs_resource_get_info(pSrcBuffer, &src_va, NULL)) {
        return;
    }
    if (dst_va <= UINT64_C(0x10000) || src_va <= UINT64_C(0x10000)) {
        return;
    }
    d3d12xs_pe_memcpy(
        (void *)(uintptr_t)(dst_va + DstOffset),
        (const void *)(uintptr_t)(src_va + SrcOffset),
        (size_t)NumBytes);
}

static uint64_t d3d12xs_resource_calc_size(const scarlett_d3d12_resource_desc_t *d) {
    if (d == NULL) {
        return 0u;
    }
    if (d->Dimension == 1u) {
        return d->Width;
    }
    uint32_t row_bytes = 0u;
    uint32_t num_rows = 0u;
    uint32_t w = (d->Width > 0u) ? (uint32_t)d->Width : 1u;
    uint32_t h = (d->Height > 0u) ? d->Height : 1u;
    uint32_t depth = (d->DepthOrArraySize > 0u) ? d->DepthOrArraySize : 1u;
    d3d12xs_format_layout(d->Format, w, h, &row_bytes, &num_rows);
    uint64_t pitch = ((uint64_t)row_bytes + 255u) & ~UINT64_C(255);
    return pitch * (uint64_t)num_rows * (uint64_t)depth;
}

static void d3d12xs_resolve_copy_loc(
    const scarlett_d3d12_texture_copy_location_t *loc,
    uint64_t *out_base_va,
    uint32_t *out_fmt,
    uint32_t *out_w,
    uint32_t *out_h,
    uint32_t *out_pitch,
    uint64_t *out_slice_bytes) {
    uint64_t va = 0u;
    scarlett_d3d12_resource_desc_t desc;
    d3d12xs_pe_zero(&desc, sizeof(desc));
    if (loc == NULL || !d3d12xs_resource_get_info(loc->pResource, &va, &desc) ||
        va <= UINT64_C(0x10000)) {
        *out_base_va = 0u;
        return;
    }
    if (loc->Type == 1u) {
        uint32_t row_bytes = 0u;
        uint32_t num_rows = 0u;
        *out_base_va = va + loc->PlacedFootprint.Offset;
        *out_fmt = loc->PlacedFootprint.Footprint.Format;
        *out_w = loc->PlacedFootprint.Footprint.Width > 0u ? loc->PlacedFootprint.Footprint.Width : 1u;
        *out_h = loc->PlacedFootprint.Footprint.Height > 0u ? loc->PlacedFootprint.Footprint.Height : 1u;
        *out_pitch = loc->PlacedFootprint.Footprint.RowPitch;
        d3d12xs_format_layout(*out_fmt, *out_w, *out_h, &row_bytes, &num_rows);
        if (*out_pitch == 0u) {
            *out_pitch = (row_bytes + 255u) & ~255u;
        }
        *out_slice_bytes = (uint64_t)(*out_pitch) * (uint64_t)num_rows;
    } else {
        uint32_t mips = desc.MipLevels > 0u ? desc.MipLevels : 1u;
        uint32_t mip = loc->SubresourceIndex % mips;
        uint32_t w = (uint32_t)(desc.Width > 0u ? desc.Width : 1u) >> mip;
        uint32_t h = (desc.Height > 0u ? desc.Height : 1u) >> mip;
        if (w == 0u) w = 1u;
        if (h == 0u) h = 1u;
        uint32_t row_bytes = 0u;
        uint32_t num_rows = 0u;
        d3d12xs_format_layout(desc.Format, w, h, &row_bytes, &num_rows);
        *out_base_va = va;
        *out_fmt = desc.Format;
        *out_w = w;
        *out_h = h;
        *out_pitch = (row_bytes + 255u) & ~255u;
        *out_slice_bytes = (uint64_t)(*out_pitch) * (uint64_t)num_rows;
    }
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot16_copy_texture_region(
    ID3D12GraphicsCommandListX *self,
    const scarlett_d3d12_texture_copy_location_t *pDst,
    uint32_t DstX,
    uint32_t DstY,
    uint32_t DstZ,
    const scarlett_d3d12_texture_copy_location_t *pSrc,
    const scarlett_d3d12_box_t *pSrcBox) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || pDst == NULL || pSrc == NULL) {
        return;
    }
    void *real_dst = d3d12xs_resource_get_real(pDst->pResource);
    void *real_src = d3d12xs_resource_get_real(pSrc->pResource);
    if (clm->real_obj != NULL && real_dst != NULL && real_src != NULL) {
        scarlett_d3d12_texture_copy_location_t dst_loc;
        scarlett_d3d12_texture_copy_location_t src_loc;
        d3d12xs_pe_memcpy(&dst_loc, pDst, sizeof(dst_loc));
        d3d12xs_pe_memcpy(&src_loc, pSrc, sizeof(src_loc));
        dst_loc.pResource = real_dst;
        src_loc.pResource = real_src;
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_ctr_t)(
            void *,
            const scarlett_d3d12_texture_copy_location_t *,
            uint32_t, uint32_t, uint32_t,
            const scarlett_d3d12_texture_copy_location_t *,
            const scarlett_d3d12_box_t *);
        void **vtbl = *(void ***)clm->real_obj;
        ((pfn_real_ctr_t)vtbl[16])(
            clm->real_obj, &dst_loc, DstX, DstY, DstZ, &src_loc, pSrcBox);
        d3d12xs_resource_mark_gpu_dirty(pDst->pResource);
    }
    uint64_t dst_va = 0u, src_va = 0u;
    uint32_t dst_fmt = 0u, src_fmt = 0u;
    uint32_t dst_w = 0u, dst_h = 0u, dst_pitch = 0u;
    uint32_t src_w = 0u, src_h = 0u, src_pitch = 0u;
    uint64_t dst_slice = 0u, src_slice = 0u;
    d3d12xs_resolve_copy_loc(pDst, &dst_va, &dst_fmt, &dst_w, &dst_h, &dst_pitch, &dst_slice);
    d3d12xs_resolve_copy_loc(pSrc, &src_va, &src_fmt, &src_w, &src_h, &src_pitch, &src_slice);
    if (dst_va == 0u || src_va == 0u) {
        return;
    }

    uint32_t sx = (pSrcBox != NULL) ? pSrcBox->left : 0u;
    uint32_t sy = (pSrcBox != NULL) ? pSrcBox->top : 0u;
    uint32_t sz = (pSrcBox != NULL) ? pSrcBox->front : 0u;
    uint32_t copy_w = (pSrcBox != NULL && pSrcBox->right > sx) ? (pSrcBox->right - sx) : src_w;
    uint32_t copy_h = (pSrcBox != NULL && pSrcBox->bottom > sy) ? (pSrcBox->bottom - sy) : src_h;
    uint32_t copy_d = (pSrcBox != NULL && pSrcBox->back > sz) ? (pSrcBox->back - sz) : 1u;

    uint32_t copy_row_bytes = 0u;
    uint32_t copy_rows = 0u;
    d3d12xs_format_layout(src_fmt, copy_w, copy_h, &copy_row_bytes, &copy_rows);
    if (copy_row_bytes > src_pitch) copy_row_bytes = src_pitch;
    if (copy_row_bytes > dst_pitch) copy_row_bytes = dst_pitch;

    uint32_t src_x_bytes = 0u, src_y_row = 0u;
    uint32_t dst_x_bytes = 0u, dst_y_row = 0u;
    if (sx > 0u || sy > 0u) {
        d3d12xs_format_layout(src_fmt, sx, sy, &src_x_bytes, &src_y_row);
        if (sx == 0u) src_x_bytes = 0u;
        if (sy == 0u) src_y_row = 0u;
    }
    if (DstX > 0u || DstY > 0u) {
        d3d12xs_format_layout(dst_fmt, DstX, DstY, &dst_x_bytes, &dst_y_row);
        if (DstX == 0u) dst_x_bytes = 0u;
        if (DstY == 0u) dst_y_row = 0u;
    }

    for (uint32_t z = 0u; z < copy_d; ++z) {
        uint64_t s_base = src_va + (uint64_t)(sz + z) * src_slice + src_x_bytes;
        uint64_t d_base = dst_va + (uint64_t)(DstZ + z) * dst_slice + dst_x_bytes;
        for (uint32_t r = 0u; r < copy_rows; ++r) {
            const void *s_ptr = (const void *)(uintptr_t)(s_base + (uint64_t)(src_y_row + r) * src_pitch);
            void *d_ptr = (void *)(uintptr_t)(d_base + (uint64_t)(dst_y_row + r) * dst_pitch);
            d3d12xs_pe_memcpy(d_ptr, s_ptr, (size_t)copy_row_bytes);
        }
    }
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot17_copy_resource(
    ID3D12GraphicsCommandListX *self,
    void *pDstResource,
    void *pSrcResource) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    void *real_dst = d3d12xs_resource_get_real(pDstResource);
    void *real_src = d3d12xs_resource_get_real(pSrcResource);
    if (clm->real_obj != NULL && real_dst != NULL && real_src != NULL) {
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_cr_t)(void *, void *, void *);
        void **vtbl = *(void ***)clm->real_obj;
        ((pfn_real_cr_t)vtbl[17])(clm->real_obj, real_dst, real_src);
        d3d12xs_resource_mark_gpu_dirty(pDstResource);
    }
    uint64_t dst_va = 0u, src_va = 0u;
    scarlett_d3d12_resource_desc_t dst_desc, src_desc;
    if (!d3d12xs_resource_get_info(pDstResource, &dst_va, &dst_desc) ||
        !d3d12xs_resource_get_info(pSrcResource, &src_va, &src_desc)) {
        return;
    }
    if (dst_va <= UINT64_C(0x10000) || src_va <= UINT64_C(0x10000)) {
        return;
    }
    uint64_t dst_sz = d3d12xs_resource_calc_size(&dst_desc);
    uint64_t src_sz = d3d12xs_resource_calc_size(&src_desc);
    uint64_t copy_sz = (dst_sz < src_sz) ? dst_sz : src_sz;
    if (copy_sz > 0u) {
        d3d12xs_pe_memcpy(
            (void *)(uintptr_t)dst_va,
            (const void *)(uintptr_t)src_va,
            (size_t)copy_sz);
    }
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot59_write_buffer_immediate(
    ID3D12GraphicsCommandListX *self,
    uint32_t Count,
    const scarlett_d3d12_write_buf_param_t *pParams,
    const void *pModes) {
    (void)pModes;
    if (self == NULL || pParams == NULL || Count == 0u) {
        return;
    }
    static uint32_t g_wbi_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_wbi_seq, 1u, __ATOMIC_RELAXED);
    if (seq <= 12u || (seq % 250u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "WriteBufferImmediate",
            59u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)Count,
            pParams[0].Dest,
            (uint64_t)pParams[0].Value,
            SCARLETT_OK,
            "(trace)");
    }
    for (uint32_t i = 0u; i < Count; ++i) {
        d3d12xs_cmd_list_record_label(self, pParams[i].Dest, pParams[i].Value);
    }
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot71_write_gpu_memory_label_x(
    ID3D12GraphicsCommandListX *self,
    uint64_t gpu_va,
    uint32_t value,
    uint32_t flags,
    uint32_t reserved) {
    (void)flags;
    (void)reserved;
    if (self == NULL) {
        return;
    }
    static uint32_t g_wgml_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_wgml_seq, 1u, __ATOMIC_RELAXED);
    if (seq <= 12u || (seq % 250u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "WriteGpuMemoryLabelX",
            71u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            gpu_va,
            (uint64_t)value,
            (uint64_t)flags,
            SCARLETT_OK,
            "(trace)");
    }
    d3d12xs_cmd_list_record_label(self, gpu_va, value);
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_slot126_ok(
    ID3D12GraphicsCommandListX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    (void)rdx;
    (void)r8;
    (void)r9;
    return (self != NULL) ? SCARLETT_OK : SCARLETT_E_POINTER;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_xbox_mem_config_131(
    ID3D12GraphicsCommandListX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "ID3D12GraphicsCommandListX::SegmentConfig131",
        131u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        rdx,
        r8,
        r9,
        SCARLETT_OK,
        "(ok)");
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_xbox_mem_config_132(
    ID3D12GraphicsCommandListX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "ID3D12GraphicsCommandListX::SegmentConfig132",
        132u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        rdx,
        r8,
        r9,
        SCARLETT_OK,
        "(ok)");
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_xbox_ok_139(
    ID3D12GraphicsCommandListX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    (void)r9;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_hresult_t hr = cmd_list_reset(
        self, (void *)(uintptr_t)rdx, (void *)(uintptr_t)r8);
    static uint32_t logged = 0u;
    if (__atomic_exchange_n(&logged, 1u, __ATOMIC_ACQ_REL) == 0u || scarlett_failed(hr)) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12GraphicsCommandListX::Slot139",
            139u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            rdx,
            r8,
            r9,
            hr,
            scarlett_succeeded(hr) ? "(reset_ok)" : "(reset_fail)");
    }
    return hr;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_xbox_ok_142(
    ID3D12GraphicsCommandListX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    static uint32_t logged = 0u;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (__atomic_exchange_n(&logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12GraphicsCommandListX::Slot142",
            142u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            rdx,
            r8,
            r9,
            SCARLETT_OK,
            "(ok)");
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_xbox_ok_146(
    ID3D12GraphicsCommandListX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    static uint32_t logged = 0u;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (__atomic_exchange_n(&logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12GraphicsCommandListX::Slot146",
            146u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            rdx,
            r8,
            r9,
            SCARLETT_OK,
            "(ok)");
    }
    return SCARLETT_OK;
}

typedef struct d3d12xs_res_meta_s d3d12xs_res_meta_t;
static d3d12xs_res_meta_t *d3d12xs_get_res_meta(const void *ptr);
static uint64_t d3d12xs_translate_gpu_va(uint64_t xbox_va, void **out_real_res);
static uint64_t d3d12xs_translate_cpu_desc_handle(
    uint64_t xbox_cpu_handle, uint32_t *out_heap_type);
static uint64_t d3d12xs_translate_cpu_desc_handle_range(
    uint64_t xbox_cpu_handle, uint32_t count, uint32_t *out_heap_type);
static uint64_t d3d12xs_translate_gpu_desc_handle(
    uint64_t xbox_gpu_handle, uint32_t *out_heap_type);
static void *d3d12xs_desc_heap_get_real(const void *ptr);
static void *d3d12xs_root_sig_get_real(const void *ptr);
static void *d3d12xs_pso_get_real(const void *ptr);
static void *d3d12xs_lookup_desc_resource(uint64_t xbox_cpu_handle);
static void *d3d12xs_lookup_desc_resource_offset(uint64_t xbox_cpu_handle, uint32_t index);

static void d3d12xs_cl_log(
    const char *op,
    uint32_t xbox_slot,
    uint64_t a0,
    uint64_t a1,
    uint64_t a2,
    const char *note,
    bool is_error) {
    static uint32_t g_cl_ok_counts[96];
    static uint32_t g_cl_err_counts[96];
    uint32_t idx = (xbox_slot < 96u) ? xbox_slot : 95u;
    uint32_t seq = is_error
        ? __atomic_add_fetch(&g_cl_err_counts[idx], 1u, __ATOMIC_RELAXED)
        : __atomic_add_fetch(&g_cl_ok_counts[idx], 1u, __ATOMIC_RELAXED);
    if (is_error) {
        if (seq > 8u && (seq & 255u) != 0u) {
            return;
        }
    } else {
        if (seq > 6u && (seq & 511u) != 0u) {
            return;
        }
    }
    char buf[280];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = d3d12xs_pe_append_str(p, end, "[scarlett-cl] ");
    p = d3d12xs_pe_append_str(p, end, op);
    p = d3d12xs_pe_append_str(p, end, " slot=");
    p = d3d12xs_pe_append_u32(p, end, xbox_slot);
    p = d3d12xs_pe_append_str(p, end, " seq=");
    p = d3d12xs_pe_append_u32(p, end, seq);
    p = d3d12xs_pe_append_str(p, end, " a0=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, a0, 16);
    p = d3d12xs_pe_append_str(p, end, " a1=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, a1, 16);
    p = d3d12xs_pe_append_str(p, end, " a2=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, a2, 16);
    if (note != NULL) {
        p = d3d12xs_pe_append_str(p, end, " ");
        p = d3d12xs_pe_append_str(p, end, note);
    }
    p = d3d12xs_pe_append_str(p, end, "\n");
    *p = '\0';
    d3d12xs_pe_ods(buf);
}

static void d3d12xs_cl_mark_bound_rtvs_dirty(d3d12xs_cmd_list_meta_t *clm) {
    if (clm == NULL) {
        return;
    }
    uint32_t n = (clm->bound_rtv_count <= 8u) ? clm->bound_rtv_count : 8u;
    for (uint32_t i = 0u; i < n; ++i) {
        if (clm->bound_rtvs[i] != NULL) {
            d3d12xs_resource_mark_gpu_dirty(clm->bound_rtvs[i]);
        }
    }
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot11_clear_state(
    ID3D12GraphicsCommandListX *self,
    void *pPipelineState) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    clm->bound_rtv_count = 0u;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));
    if (clm->real_obj == NULL) {
        d3d12xs_cl_log("ClearState", 11u, (uint64_t)(uintptr_t)self,
                       (uint64_t)(uintptr_t)pPipelineState, 0u, "(no_real_cl)", true);
        return;
    }
    void *real_pso = (pPipelineState != NULL) ? d3d12xs_pso_get_real(pPipelineState) : NULL;
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[11])(clm->real_obj, real_pso);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot12_draw_instanced(
    ID3D12GraphicsCommandListX *self,
    uint32_t VertexCountPerInstance,
    uint32_t InstanceCount,
    uint32_t StartVertexLocation,
    uint32_t StartInstanceLocation) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    if (clm->real_obj == NULL) {
        d3d12xs_cl_log("DrawInstanced", 12u, VertexCountPerInstance, InstanceCount,
                       StartVertexLocation, "(no_real_cl)", true);
        return;
    }
    d3d12xs_cl_mark_bound_rtvs_dirty(clm);
    clm->has_gpu_write_cmds = 1u;
    d3d12xs_cl_log("DrawInstanced", 12u, VertexCountPerInstance, InstanceCount,
                   StartVertexLocation, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, uint32_t, uint32_t, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[12])(
        clm->real_obj, VertexCountPerInstance, InstanceCount,
        StartVertexLocation, StartInstanceLocation);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot13_draw_indexed_instanced(
    ID3D12GraphicsCommandListX *self,
    uint32_t IndexCountPerInstance,
    uint32_t InstanceCount,
    uint32_t StartIndexLocation,
    int32_t BaseVertexLocation,
    uint32_t StartInstanceLocation) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    if (clm->real_obj == NULL) {
        d3d12xs_cl_log("DrawIndexedInstanced", 13u, IndexCountPerInstance, InstanceCount,
                       StartIndexLocation, "(no_real_cl)", true);
        return;
    }
    d3d12xs_cl_mark_bound_rtvs_dirty(clm);
    clm->has_gpu_write_cmds = 1u;
    d3d12xs_cl_log("DrawIndexedInstanced", 13u, IndexCountPerInstance, InstanceCount,
                   StartIndexLocation, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[13])(
        clm->real_obj, IndexCountPerInstance, InstanceCount,
        StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot14_dispatch(
    ID3D12GraphicsCommandListX *self,
    uint32_t ThreadGroupCountX,
    uint32_t ThreadGroupCountY,
    uint32_t ThreadGroupCountZ) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    if (clm->real_obj == NULL) {
        d3d12xs_cl_log("Dispatch", 14u, ThreadGroupCountX, ThreadGroupCountY,
                       ThreadGroupCountZ, "(no_real_cl)", true);
        return;
    }
    clm->has_gpu_write_cmds = 1u;
    d3d12xs_cl_log("Dispatch", 14u, ThreadGroupCountX, ThreadGroupCountY,
                   ThreadGroupCountZ, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, uint32_t, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[14])(
        clm->real_obj, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot19_resolve_subresource(
    ID3D12GraphicsCommandListX *self,
    void *pDstResource,
    uint32_t DstSubresource,
    void *pSrcResource,
    uint32_t SrcSubresource,
    uint32_t Format) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    void *real_dst = d3d12xs_resource_get_real(pDstResource);
    void *real_src = d3d12xs_resource_get_real(pSrcResource);
    if (real_dst == NULL || real_src == NULL) {
        d3d12xs_cl_log("ResolveSubresource", 19u, (uint64_t)(uintptr_t)pDstResource,
                       (uint64_t)(uintptr_t)pSrcResource, Format, "(no_real_res)", true);
        return;
    }
    d3d12xs_resource_mark_gpu_dirty(pDstResource);
    clm->has_gpu_write_cmds = 1u;
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, void *, uint32_t, void *, uint32_t, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[19])(
        clm->real_obj, real_dst, DstSubresource, real_src, SrcSubresource, Format);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot21_rs_set_viewports(
    ID3D12GraphicsCommandListX *self,
    uint32_t NumViewports,
    const void *pViewports) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    if (NumViewports > 0u && pViewports == NULL) {
        return;
    }
    static uint32_t g_vp_seq = 0u;
    uint32_t vp_seq = __atomic_add_fetch(&g_vp_seq, 1u, __ATOMIC_RELAXED);
    if (vp_seq <= 4u && NumViewports > 0u && pViewports != NULL) {
        const uint32_t *dw = (const uint32_t *)pViewports;
        d3d12xs_pe_log_stub_ods("d3d12_xs.dll", "VP_Diag", 21u,
            ((uint64_t)dw[0] << 32) | dw[1], ((uint64_t)dw[2] << 32) | dw[3],
            ((uint64_t)dw[4] << 32) | dw[5], NumViewports, 0u, SCARLETT_OK, "(vp)");
    }
    d3d12xs_cl_log("RSSetViewports", 21u, NumViewports, (uint64_t)(uintptr_t)pViewports,
                   0u, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[21])(clm->real_obj, NumViewports, pViewports);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot22_rs_set_scissor_rects(
    ID3D12GraphicsCommandListX *self,
    uint32_t NumRects,
    const void *pRects) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    if (NumRects > 0u && pRects == NULL) {
        return;
    }
    static uint32_t g_sc_seq = 0u;
    uint32_t sc_seq = __atomic_add_fetch(&g_sc_seq, 1u, __ATOMIC_RELAXED);
    if (sc_seq <= 4u && NumRects > 0u && pRects != NULL) {
        const uint32_t *dw = (const uint32_t *)pRects;
        d3d12xs_pe_log_stub_ods("d3d12_xs.dll", "Scissor_Diag", 22u,
            ((uint64_t)dw[0] << 32) | dw[1], ((uint64_t)dw[2] << 32) | dw[3],
            NumRects, 0u, 0u, SCARLETT_OK, "(scissor)");
    }
    d3d12xs_cl_log("RSSetScissorRects", 22u, NumRects, (uint64_t)(uintptr_t)pRects,
                   0u, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[22])(clm->real_obj, NumRects, pRects);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot23_om_set_blend_factor(
    ID3D12GraphicsCommandListX *self,
    const float *BlendFactor) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    d3d12xs_cl_log("OMSetBlendFactor", 23u, (uint64_t)(uintptr_t)BlendFactor,
                   0u, 0u, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, const float *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[23])(clm->real_obj, BlendFactor);
}

static void SCARLETT_D3D12XS_CALL cmd_list_slot24_om_set_stencil_ref(
    ID3D12GraphicsCommandListX *self,
    uint32_t StencilRef) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[24])(clm->real_obj, StencilRef);
}

static uint32_t d3d12xs_desc_heap_get_type(const void *ptr);
static uint64_t d3d12xs_stage_descriptor_table(
    d3d12xs_cmd_list_meta_t *clm,
    bool is_compute,
    uint32_t root_param_idx,
    uint64_t base_desc_va);

/* Xbox slot 32 -> PC slot 28: SetDescriptorHeaps */
static void SCARLETT_D3D12XS_CALL cmd_list_slot32_set_descriptor_heaps(
    ID3D12GraphicsCommandListX *self,
    uint32_t NumDescriptorHeaps,
    void * const *ppDescriptorHeaps) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        d3d12xs_cl_log("SetDescriptorHeaps", 32u, NumDescriptorHeaps,
                       (uint64_t)(uintptr_t)ppDescriptorHeaps, 0u, "(no_real_cl)", true);
        return;
    }
    if (NumDescriptorHeaps > 0u && ppDescriptorHeaps == NULL) {
        return;
    }
    void *real_heaps[16];
    uint32_t real_count = 0u;
    uint32_t limit = (NumDescriptorHeaps < 16u) ? NumDescriptorHeaps : 16u;
    clm->bound_srv_heap = NULL;
    clm->bound_sampler_heap = NULL;
    for (uint32_t i = 0u; i < limit; ++i) {
        void *rh = d3d12xs_desc_heap_get_real(ppDescriptorHeaps[i]);
        if (rh != NULL) {
            real_heaps[real_count++] = rh;
            uint32_t ht = d3d12xs_desc_heap_get_type(ppDescriptorHeaps[i]);
            if (ht == 0u) {
                clm->bound_srv_heap = ppDescriptorHeaps[i];
            } else if (ht == 1u) {
                clm->bound_sampler_heap = ppDescriptorHeaps[i];
            }
        } else {
            d3d12xs_cl_log("SetDescriptorHeaps", 32u, NumDescriptorHeaps,
                           (uint64_t)(uintptr_t)ppDescriptorHeaps[i], i, "(no_real_heap)", true);
        }
    }
    d3d12xs_cl_log("SetDescriptorHeaps", 32u, NumDescriptorHeaps, real_count,
                   (real_count > 0u) ? (uint64_t)(uintptr_t)real_heaps[0] : 0u,
                   "(forwarded)", false);
    if (real_count > 0u || NumDescriptorHeaps == 0u) {
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, void * const *);
        void **vtbl = *(void ***)clm->real_obj;
        ((pfn_real_t)vtbl[28])(clm->real_obj, real_count, real_heaps);
    }
}

/* Xbox slot 33 -> PC slot 29: SetComputeRootSignature */
static void SCARLETT_D3D12XS_CALL cmd_list_slot33_set_compute_root_signature(
    ID3D12GraphicsCommandListX *self,
    void *pRootSignature) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    void *real_rs = (pRootSignature != NULL) ? d3d12xs_root_sig_get_real(pRootSignature) : NULL;
    if (pRootSignature != NULL && real_rs == NULL) {
        d3d12xs_cl_log("SetComputeRootSignature", 33u, (uint64_t)(uintptr_t)pRootSignature,
                       0u, 0u, "(no_real_rs)", true);
        return;
    }
    clm->bound_comp_rs = pRootSignature;
    d3d12xs_cl_log("SetComputeRootSignature", 33u, (uint64_t)(uintptr_t)pRootSignature,
                   (uint64_t)(uintptr_t)real_rs, 0u, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[29])(clm->real_obj, real_rs);
}

/* Xbox slot 34 -> PC slot 30: SetGraphicsRootSignature */
static void SCARLETT_D3D12XS_CALL cmd_list_slot34_set_graphics_root_signature(
    ID3D12GraphicsCommandListX *self,
    void *pRootSignature) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    void *real_rs = (pRootSignature != NULL) ? d3d12xs_root_sig_get_real(pRootSignature) : NULL;
    if (pRootSignature != NULL && real_rs == NULL) {
        d3d12xs_cl_log("SetGraphicsRootSignature", 34u, (uint64_t)(uintptr_t)pRootSignature,
                       0u, 0u, "(no_real_rs)", true);
        return;
    }
    clm->bound_gfx_rs = pRootSignature;
    d3d12xs_cl_log("SetGraphicsRootSignature", 34u, (uint64_t)(uintptr_t)pRootSignature,
                   (uint64_t)(uintptr_t)real_rs, 0u, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[30])(clm->real_obj, real_rs);
}

/* Xbox slot 35 -> PC slot 31: SetComputeRootDescriptorTable */
static void SCARLETT_D3D12XS_CALL cmd_list_slot35_set_compute_root_descriptor_table(
    ID3D12GraphicsCommandListX *self,
    uint32_t RootParameterIndex,
    uint64_t BaseDescriptor) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    uint64_t real_gpu = d3d12xs_translate_gpu_desc_handle(BaseDescriptor, NULL);
    if (BaseDescriptor != 0u && real_gpu == 0u) {
        real_gpu = d3d12xs_stage_descriptor_table(clm, true, RootParameterIndex, BaseDescriptor);
        if (real_gpu == 0u) {
            d3d12xs_cl_log("SetComputeRootDescriptorTable", 35u, RootParameterIndex,
                           BaseDescriptor, 0u, "(bad_gpu_desc)", true);
            return;
        }
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, uint64_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[31])(clm->real_obj, RootParameterIndex, real_gpu);
}

/* Xbox slot 36 -> PC slot 32: SetGraphicsRootDescriptorTable */
static void SCARLETT_D3D12XS_CALL cmd_list_slot36_set_graphics_root_descriptor_table(
    ID3D12GraphicsCommandListX *self,
    uint32_t RootParameterIndex,
    uint64_t BaseDescriptor) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    uint64_t real_gpu = d3d12xs_translate_gpu_desc_handle(BaseDescriptor, NULL);
    if (BaseDescriptor != 0u && real_gpu == 0u) {
        real_gpu = d3d12xs_stage_descriptor_table(clm, false, RootParameterIndex, BaseDescriptor);
        if (real_gpu == 0u) {
            d3d12xs_cl_log("SetGraphicsRootDescriptorTable", 36u, RootParameterIndex,
                           BaseDescriptor, 0u, "(bad_gpu_desc)", true);
            return;
        }
    }
    d3d12xs_cl_log("SetGraphicsRootDescriptorTable", 36u, RootParameterIndex,
                   BaseDescriptor, real_gpu, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, uint64_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[32])(clm->real_obj, RootParameterIndex, real_gpu);
}

/* Xbox slot 37 -> PC slot 33: SetComputeRoot32BitConstant */
static void SCARLETT_D3D12XS_CALL cmd_list_slot37_set_compute_root_32bit_constant(
    ID3D12GraphicsCommandListX *self,
    uint32_t RootParameterIndex,
    uint32_t SrcData,
    uint32_t DestOffsetIn32BitValues) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, uint32_t, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[33])(clm->real_obj, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

/* Xbox slot 38 -> PC slot 34: SetGraphicsRoot32BitConstant */
static void SCARLETT_D3D12XS_CALL cmd_list_slot38_set_graphics_root_32bit_constant(
    ID3D12GraphicsCommandListX *self,
    uint32_t RootParameterIndex,
    uint32_t SrcData,
    uint32_t DestOffsetIn32BitValues) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, uint32_t, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[34])(clm->real_obj, RootParameterIndex, SrcData, DestOffsetIn32BitValues);
}

/* Xbox slot 39 -> PC slot 35: SetComputeRoot32BitConstants */
static void SCARLETT_D3D12XS_CALL cmd_list_slot39_set_compute_root_32bit_constants(
    ID3D12GraphicsCommandListX *self,
    uint32_t RootParameterIndex,
    uint32_t Num32BitValuesToSet,
    const void *pSrcData,
    uint32_t DestOffsetIn32BitValues) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL || (Num32BitValuesToSet > 0u && pSrcData == NULL)) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, uint32_t, const void *, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[35])(
        clm->real_obj, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
}

/* Xbox slot 40 -> PC slot 36: SetGraphicsRoot32BitConstants */
static void SCARLETT_D3D12XS_CALL cmd_list_slot40_set_graphics_root_32bit_constants(
    ID3D12GraphicsCommandListX *self,
    uint32_t RootParameterIndex,
    uint32_t Num32BitValuesToSet,
    const void *pSrcData,
    uint32_t DestOffsetIn32BitValues) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL || (Num32BitValuesToSet > 0u && pSrcData == NULL)) {
        return;
    }
    d3d12xs_cl_log("SetGraphicsRoot32BitConstants", 40u, RootParameterIndex,
                   Num32BitValuesToSet, DestOffsetIn32BitValues, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, uint32_t, const void *, uint32_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[36])(
        clm->real_obj, RootParameterIndex, Num32BitValuesToSet, pSrcData, DestOffsetIn32BitValues);
}

static void d3d12xs_cl_forward_root_va(
    ID3D12GraphicsCommandListX *self,
    uint32_t xbox_slot,
    uint32_t pc_slot,
    const char *op_name,
    uint32_t RootParameterIndex,
    uint64_t BufferLocation) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    uint64_t real_va = (BufferLocation != 0u)
        ? d3d12xs_translate_gpu_va(BufferLocation, NULL)
        : 0u;
    if (BufferLocation != 0u && real_va == 0u) {
        d3d12xs_cl_log(op_name, xbox_slot, RootParameterIndex, BufferLocation,
                       0u, "(unmapped_va)", true);
        return;
    }
    if (BufferLocation != 0u && (xbox_slot <= 44u)) {
        uint64_t sz = (xbox_slot <= 42u) ? 4096u : 65536u;
        d3d12xs_enqueue_cpu_upload_va(BufferLocation, sz);
    }
    d3d12xs_cl_log(op_name, xbox_slot, RootParameterIndex, BufferLocation,
                   real_va, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, uint32_t, uint64_t);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[pc_slot])(clm->real_obj, RootParameterIndex, real_va);
}

/* Xbox slots 41..46 -> PC slots 37..42: Root CBV/SRV/UAV */
static void SCARLETT_D3D12XS_CALL cmd_list_slot41_set_compute_root_cbv(
    ID3D12GraphicsCommandListX *self, uint32_t idx, uint64_t va) {
    d3d12xs_cl_forward_root_va(self, 41u, 37u, "SetComputeRootCBV", idx, va);
}
static void SCARLETT_D3D12XS_CALL cmd_list_slot42_set_graphics_root_cbv(
    ID3D12GraphicsCommandListX *self, uint32_t idx, uint64_t va) {
    d3d12xs_cl_forward_root_va(self, 42u, 38u, "SetGraphicsRootCBV", idx, va);
}
static void SCARLETT_D3D12XS_CALL cmd_list_slot43_set_compute_root_srv(
    ID3D12GraphicsCommandListX *self, uint32_t idx, uint64_t va) {
    d3d12xs_cl_forward_root_va(self, 43u, 39u, "SetComputeRootSRV", idx, va);
}
static void SCARLETT_D3D12XS_CALL cmd_list_slot44_set_graphics_root_srv(
    ID3D12GraphicsCommandListX *self, uint32_t idx, uint64_t va) {
    d3d12xs_cl_forward_root_va(self, 44u, 40u, "SetGraphicsRootSRV", idx, va);
}
static void SCARLETT_D3D12XS_CALL cmd_list_slot45_set_compute_root_uav(
    ID3D12GraphicsCommandListX *self, uint32_t idx, uint64_t va) {
    d3d12xs_cl_forward_root_va(self, 45u, 41u, "SetComputeRootUAV", idx, va);
}
static void SCARLETT_D3D12XS_CALL cmd_list_slot46_set_graphics_root_uav(
    ID3D12GraphicsCommandListX *self, uint32_t idx, uint64_t va) {
    d3d12xs_cl_forward_root_va(self, 46u, 42u, "SetGraphicsRootUAV", idx, va);
}

typedef struct scarlett_d3d12_ibv_s {
    uint64_t BufferLocation;
    uint32_t SizeInBytes;
    uint32_t Format;
} scarlett_d3d12_ibv_t;

typedef struct scarlett_d3d12_vbv_s {
    uint64_t BufferLocation;
    uint32_t SizeInBytes;
    uint32_t StrideInBytes;
} scarlett_d3d12_vbv_t;

typedef struct scarlett_d3d12_sobv_s {
    uint64_t BufferLocation;
    uint64_t SizeInBytes;
    uint64_t BufferFilledSizeLocation;
} scarlett_d3d12_sobv_t;

/* Xbox slot 47 -> PC slot 43: IASetIndexBuffer */
static void SCARLETT_D3D12XS_CALL cmd_list_slot47_ia_set_index_buffer(
    ID3D12GraphicsCommandListX *self,
    const scarlett_d3d12_ibv_t *pView) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, const scarlett_d3d12_ibv_t *);
    void **vtbl = *(void ***)clm->real_obj;
    if (pView == NULL) {
        ((pfn_real_t)vtbl[43])(clm->real_obj, NULL);
        return;
    }
    scarlett_d3d12_ibv_t pc_view = *pView;
    if (pc_view.BufferLocation != 0u) {
        pc_view.BufferLocation = d3d12xs_translate_gpu_va(pc_view.BufferLocation, NULL);
        if (pc_view.BufferLocation == 0u) {
            d3d12xs_cl_log("IASetIndexBuffer", 47u, pView->BufferLocation,
                           pView->SizeInBytes, pView->Format, "(unmapped_va)", true);
            return;
        }
        d3d12xs_enqueue_cpu_upload_va(pView->BufferLocation, pView->SizeInBytes);
    }
    d3d12xs_cl_log("IASetIndexBuffer", 47u, pView->BufferLocation,
                   pc_view.BufferLocation, pView->SizeInBytes, "(forwarded)", false);
    ((pfn_real_t)vtbl[43])(clm->real_obj, &pc_view);
}

/* Xbox slot 48 -> PC slot 44: IASetVertexBuffers */
static void SCARLETT_D3D12XS_CALL cmd_list_slot48_ia_set_vertex_buffers(
    ID3D12GraphicsCommandListX *self,
    uint32_t StartSlot,
    uint32_t NumViews,
    const scarlett_d3d12_vbv_t *pViews) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, uint32_t, const scarlett_d3d12_vbv_t *);
    void **vtbl = *(void ***)clm->real_obj;
    if (NumViews == 0u || pViews == NULL) {
        ((pfn_real_t)vtbl[44])(clm->real_obj, StartSlot, 0u, NULL);
        return;
    }
    scarlett_d3d12_vbv_t pc_views[32];
    uint32_t cnt = (NumViews < 32u) ? NumViews : 32u;
    for (uint32_t i = 0u; i < cnt; ++i) {
        pc_views[i] = pViews[i];
        if (pc_views[i].BufferLocation != 0u) {
            uint64_t rva = d3d12xs_translate_gpu_va(pc_views[i].BufferLocation, NULL);
            if (rva == 0u) {
                d3d12xs_cl_log("IASetVertexBuffers", 48u, StartSlot + i,
                               pc_views[i].BufferLocation, pc_views[i].SizeInBytes,
                               "(unmapped_va)", true);
            } else {
                d3d12xs_enqueue_cpu_upload_va(pc_views[i].BufferLocation, pc_views[i].SizeInBytes);
            }
            pc_views[i].BufferLocation = rva;
        }
    }
    d3d12xs_cl_log("IASetVertexBuffers", 48u, StartSlot, cnt,
                   pc_views[0].BufferLocation, "(forwarded)", false);
    static uint32_t g_vb_diag = 0u;
    if (__atomic_add_fetch(&g_vb_diag, 1u, __ATOMIC_RELAXED) <= 4u) {
        uint64_t orig_va = pViews[0].BufferLocation;
        const uint32_t *dw = d3d12xs_pe_is_readable((const void *)(uintptr_t)orig_va, 72u)
            ? (const uint32_t *)(uintptr_t)orig_va : NULL;
        d3d12xs_pe_log_stub_ods("d3d12_xs.dll", "VB_Diag", 48u, orig_va,
            ((uint64_t)pViews[0].SizeInBytes << 32) | pViews[0].StrideInBytes,
            (dw != NULL) ? (((uint64_t)dw[0] << 32) | dw[1]) : 0u,
            (dw != NULL) ? (((uint64_t)dw[2] << 32) | dw[3]) : 0u,
            (dw != NULL) ? (((uint64_t)dw[4] << 32) | dw[5]) : 0u,
            SCARLETT_OK, "(vb_v0)");
        if (dw != NULL) {
            d3d12xs_pe_log_stub_ods("d3d12_xs.dll", "VB_Diag2", 48u,
                (((uint64_t)dw[6] << 32) | dw[7]), (((uint64_t)dw[8] << 32) | dw[9]),
                (((uint64_t)dw[10] << 32) | dw[11]), (((uint64_t)dw[12] << 32) | dw[13]),
                (((uint64_t)dw[14] << 32) | dw[15]), SCARLETT_OK, "(vb_v1_v2)");
        }
    }
    ((pfn_real_t)vtbl[44])(clm->real_obj, StartSlot, cnt, pc_views);
}

/* Xbox slot 49 -> PC slot 45: SOSetTargets */
static void SCARLETT_D3D12XS_CALL cmd_list_slot49_so_set_targets(
    ID3D12GraphicsCommandListX *self,
    uint32_t StartSlot,
    uint32_t NumViews,
    const scarlett_d3d12_sobv_t *pViews) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, uint32_t, const scarlett_d3d12_sobv_t *);
    void **vtbl = *(void ***)clm->real_obj;
    if (NumViews == 0u || pViews == NULL) {
        ((pfn_real_t)vtbl[45])(clm->real_obj, StartSlot, 0u, NULL);
        return;
    }
    scarlett_d3d12_sobv_t pc_views[4];
    uint32_t cnt = (NumViews < 4u) ? NumViews : 4u;
    for (uint32_t i = 0u; i < cnt; ++i) {
        pc_views[i] = pViews[i];
        if (pc_views[i].BufferLocation != 0u) {
            pc_views[i].BufferLocation = d3d12xs_translate_gpu_va(pc_views[i].BufferLocation, NULL);
        }
        if (pc_views[i].BufferFilledSizeLocation != 0u) {
            pc_views[i].BufferFilledSizeLocation =
                d3d12xs_translate_gpu_va(pc_views[i].BufferFilledSizeLocation, NULL);
        }
    }
    ((pfn_real_t)vtbl[45])(clm->real_obj, StartSlot, cnt, pc_views);
}

/* Xbox slot 50 -> PC slot 46: OMSetRenderTargets */
static void SCARLETT_D3D12XS_CALL cmd_list_slot50_om_set_render_targets(
    ID3D12GraphicsCommandListX *self,
    uint32_t NumRenderTargetDescriptors,
    const uint64_t *pRenderTargetDescriptors,
    uint32_t RTsSingleHandleToDescriptorRange,
    const uint64_t *pDepthStencilDescriptor) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    uint32_t rt_count = (NumRenderTargetDescriptors <= 8u) ? NumRenderTargetDescriptors : 8u;
    if (pRenderTargetDescriptors == NULL) {
        rt_count = 0u;
    }
    clm->bound_rtv_count = rt_count;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));

    uint64_t real_rtvs[8];
    uint32_t valid_rt_count = 0u;
    bool rtv_err = false;
    if (rt_count > 0u) {
        if (RTsSingleHandleToDescriptorRange != 0u) {
            uint64_t first = pRenderTargetDescriptors[0];
            uint64_t real_first = d3d12xs_translate_cpu_desc_handle_range(first, rt_count, NULL);
            if (real_first != 0u) {
                real_rtvs[0] = real_first;
                valid_rt_count = rt_count;
                for (uint32_t i = 0u; i < rt_count; ++i) {
                    clm->bound_rtvs[i] = d3d12xs_lookup_desc_resource_offset(first, i);
                }
            } else {
                rtv_err = true;
                d3d12xs_cl_log("OMSetRenderTargets", 50u, rt_count, first,
                               RTsSingleHandleToDescriptorRange, "(bad_rtv_range)", true);
            }
        } else {
            for (uint32_t i = 0u; i < rt_count; ++i) {
                uint64_t h = pRenderTargetDescriptors[i];
                uint64_t rh = d3d12xs_translate_cpu_desc_handle(h, NULL);
                if (rh == 0u) {
                    rtv_err = true;
                    d3d12xs_cl_log("OMSetRenderTargets", 50u, i, h, rt_count,
                                   "(bad_rtv_handle)", true);
                    break;
                }
                real_rtvs[i] = rh;
                ++valid_rt_count;
                clm->bound_rtvs[i] = d3d12xs_lookup_desc_resource(h);
            }
        }
    }

    uint64_t real_dsv = 0u;
    const uint64_t *real_dsv_ptr = NULL;
    if (pDepthStencilDescriptor != NULL && *pDepthStencilDescriptor != 0u) {
        real_dsv = d3d12xs_translate_cpu_desc_handle(*pDepthStencilDescriptor, NULL);
        if (real_dsv != 0u) {
            real_dsv_ptr = &real_dsv;
        } else {
            d3d12xs_cl_log("OMSetRenderTargets", 50u, *pDepthStencilDescriptor,
                           rt_count, 0u, "(bad_dsv_handle)", true);
        }
    }

    if (clm->real_obj == NULL) {
        d3d12xs_cl_log("OMSetRenderTargets", 50u, rt_count, 0u, 0u, "(no_real_cl)", true);
        return;
    }
    if (rtv_err) {
        valid_rt_count = 0u;
    }
    d3d12xs_cl_log("OMSetRenderTargets", 50u, valid_rt_count,
                   (valid_rt_count > 0u) ? real_rtvs[0] : 0u, real_dsv, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint32_t, const uint64_t *, uint32_t, const uint64_t *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[46])(
        clm->real_obj,
        valid_rt_count,
        (valid_rt_count > 0u) ? real_rtvs : NULL,
        RTsSingleHandleToDescriptorRange,
        real_dsv_ptr);
}

/* Xbox slot 51 -> PC slot 47: ClearDepthStencilView */
static void SCARLETT_D3D12XS_CALL cmd_list_slot51_clear_dsv(
    ID3D12GraphicsCommandListX *self,
    uint64_t DepthStencilView,
    uint32_t ClearFlags,
    float Depth,
    uint8_t Stencil,
    uint32_t NumRects,
    const void *pRects) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    uint64_t real_dsv = d3d12xs_translate_cpu_desc_handle(DepthStencilView, NULL);
    if (real_dsv == 0u) {
        d3d12xs_cl_log("ClearDepthStencilView", 51u, DepthStencilView, ClearFlags,
                       NumRects, "(bad_dsv_handle)", true);
        return;
    }
    clm->has_gpu_write_cmds = 1u;
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint64_t, uint32_t, float, uint8_t, uint32_t, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[47])(
        clm->real_obj, real_dsv, ClearFlags, Depth, Stencil, NumRects, pRects);
}

/* Xbox slot 52 -> PC slot 48: ClearRenderTargetView */
static void SCARLETT_D3D12XS_CALL cmd_list_slot52_clear_rtv(
    ID3D12GraphicsCommandListX *self,
    uint64_t RenderTargetView,
    const float *ColorRGBA,
    uint32_t NumRects,
    const void *pRects) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL) {
        return;
    }
    uint64_t real_rtv = d3d12xs_translate_cpu_desc_handle(RenderTargetView, NULL);
    if (real_rtv == 0u) {
        d3d12xs_cl_log("ClearRenderTargetView", 52u, RenderTargetView,
                       (uint64_t)(uintptr_t)ColorRGBA, NumRects, "(bad_rtv_handle)", true);
        return;
    }
    void *rtv_res = d3d12xs_lookup_desc_resource(RenderTargetView);
    if (rtv_res != NULL) {
        d3d12xs_resource_mark_gpu_dirty(rtv_res);
    }
    if (clm->real_obj == NULL) {
        d3d12xs_cl_log("ClearRenderTargetView", 52u, RenderTargetView, real_rtv,
                       (uint64_t)(uintptr_t)rtv_res, "(no_real_cl)", true);
        return;
    }
    clm->has_gpu_write_cmds = 1u;
    static uint32_t g_crtv_diag = 0u;
    if (__atomic_add_fetch(&g_crtv_diag, 1u, __ATOMIC_RELAXED) <= 4u && ColorRGBA != NULL) {
        const uint32_t *cw = (const uint32_t *)ColorRGBA;
        d3d12xs_pe_log_stub_ods("d3d12_xs.dll", "ClearRTV_Diag", 52u, RenderTargetView,
            ((uint64_t)cw[0] << 32) | cw[1], ((uint64_t)cw[2] << 32) | cw[3],
            NumRects, 0u, SCARLETT_OK, "(clear)");
    }
    d3d12xs_cl_log("ClearRenderTargetView", 52u, RenderTargetView, real_rtv,
                   (uint64_t)(uintptr_t)rtv_res, "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint64_t, const float *, uint32_t, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[48])(clm->real_obj, real_rtv, ColorRGBA, NumRects, pRects);
}

/* Xbox slot 53 -> PC slot 49: ClearUnorderedAccessViewUint */
static void SCARLETT_D3D12XS_CALL cmd_list_slot53_clear_uav_uint(
    ID3D12GraphicsCommandListX *self,
    uint64_t ViewGPUHandleInCurrentHeap,
    uint64_t ViewCPUHandle,
    void *pResource,
    const uint32_t Values[4],
    uint32_t NumRects,
    const void *pRects) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    uint64_t real_gpu = d3d12xs_translate_gpu_desc_handle(ViewGPUHandleInCurrentHeap, NULL);
    uint64_t real_cpu = d3d12xs_translate_cpu_desc_handle(ViewCPUHandle, NULL);
    void *real_res = d3d12xs_resource_get_real(pResource);
    if (real_gpu == 0u || real_cpu == 0u || real_res == NULL) {
        d3d12xs_cl_log("ClearUnorderedAccessViewUint", 53u, ViewGPUHandleInCurrentHeap,
                       ViewCPUHandle, (uint64_t)(uintptr_t)pResource, "(bad_args)", true);
        return;
    }
    d3d12xs_resource_mark_gpu_dirty(pResource);
    clm->has_gpu_write_cmds = 1u;
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint64_t, uint64_t, void *, const uint32_t *, uint32_t, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[49])(
        clm->real_obj, real_gpu, real_cpu, real_res, Values, NumRects, pRects);
}

/* Xbox slot 54 -> PC slot 50: ClearUnorderedAccessViewFloat */
static void SCARLETT_D3D12XS_CALL cmd_list_slot54_clear_uav_float(
    ID3D12GraphicsCommandListX *self,
    uint64_t ViewGPUHandleInCurrentHeap,
    uint64_t ViewCPUHandle,
    void *pResource,
    const float Values[4],
    uint32_t NumRects,
    const void *pRects) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    uint64_t real_gpu = d3d12xs_translate_gpu_desc_handle(ViewGPUHandleInCurrentHeap, NULL);
    uint64_t real_cpu = d3d12xs_translate_cpu_desc_handle(ViewCPUHandle, NULL);
    void *real_res = d3d12xs_resource_get_real(pResource);
    if (real_gpu == 0u || real_cpu == 0u || real_res == NULL) {
        d3d12xs_cl_log("ClearUnorderedAccessViewFloat", 54u, ViewGPUHandleInCurrentHeap,
                       ViewCPUHandle, (uint64_t)(uintptr_t)pResource, "(bad_args)", true);
        return;
    }
    d3d12xs_resource_mark_gpu_dirty(pResource);
    clm->has_gpu_write_cmds = 1u;
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(
        void *, uint64_t, uint64_t, void *, const float *, uint32_t, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[50])(
        clm->real_obj, real_gpu, real_cpu, real_res, Values, NumRects, pRects);
}

/* Xbox slot 55 -> PC slot 51: DiscardResource */
static void SCARLETT_D3D12XS_CALL cmd_list_slot55_discard_resource(
    ID3D12GraphicsCommandListX *self,
    void *pResource,
    const void *pRegion) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    void *real_res = d3d12xs_resource_get_real(pResource);
    if (real_res == NULL) {
        return;
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, void *, const void *);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[51])(clm->real_obj, real_res, pRegion);
}

/* Xbox slot 70 -> PC slot 62: OMSetDepthBounds */
static void SCARLETT_D3D12XS_CALL cmd_list_slot70_om_set_depth_bounds(
    ID3D12GraphicsCommandListX *self,
    float Min,
    float Max) {
    d3d12xs_cmd_list_meta_t *clm = d3d12xs_get_cmd_list_meta(self, NULL);
    if (clm == NULL || clm->real_obj == NULL) {
        return;
    }
    d3d12xs_cl_log("OMSetDepthBounds", 70u, (uint64_t)(uintptr_t)self, 0u, 0u,
                   "(forwarded)", false);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_t)(void *, float, float);
    void **vtbl = *(void ***)clm->real_obj;
    ((pfn_real_t)vtbl[62])(clm->real_obj, Min, Max);
}

/* Xbox slots 72, 74, 75: Scarlett-specific synchronization/state methods (no PC vtable match) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_slot72_xbox_noop(
    ID3D12GraphicsCommandListX *self, uint64_t a0, uint64_t a1, uint64_t a2) {
    d3d12xs_cl_log("XboxSlot72", 72u, a0, a1, a2, "(xbox_noop)", false);
    return (self != NULL) ? SCARLETT_OK : SCARLETT_E_POINTER;
}
static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_slot74_xbox_noop(
    ID3D12GraphicsCommandListX *self, uint64_t a0, uint64_t a1, uint64_t a2) {
    d3d12xs_cl_log("XboxSlot74", 74u, a0, a1, a2, "(xbox_noop)", false);
    return (self != NULL) ? SCARLETT_OK : SCARLETT_E_POINTER;
}
static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_slot75_xbox_noop(
    ID3D12GraphicsCommandListX *self, uint64_t a0, uint64_t a1, uint64_t a2) {
    d3d12xs_cl_log("XboxSlot75", 75u, a0, a1, a2, "(xbox_noop)", false);
    return (self != NULL) ? SCARLETT_OK : SCARLETT_E_POINTER;
}

D3D12XS_DEFINE_SLOT_STUB(cmd_list, "ID3D12GraphicsCommandListX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(cmd_list, "ID3D12GraphicsCommandListX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(cmd_list, "ID3D12GraphicsCommandListX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(cmd_list, "ID3D12GraphicsCommandListX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(cmd_list, "ID3D12GraphicsCommandListX", 7, "7")

D3D12XS_DEFINE_CL_TRACE(18)
D3D12XS_DEFINE_CL_TRACE(26) D3D12XS_DEFINE_CL_TRACE(27)
D3D12XS_DEFINE_CL_TRACE(28) D3D12XS_DEFINE_CL_TRACE(29) D3D12XS_DEFINE_CL_TRACE(30) D3D12XS_DEFINE_CL_TRACE(31)
D3D12XS_DEFINE_CL_TRACE(56) D3D12XS_DEFINE_CL_TRACE(57) D3D12XS_DEFINE_CL_TRACE(58)
D3D12XS_DEFINE_CL_TRACE(60) D3D12XS_DEFINE_CL_TRACE(61) D3D12XS_DEFINE_CL_TRACE(62) D3D12XS_DEFINE_CL_TRACE(63)
D3D12XS_DEFINE_CL_TRACE(64) D3D12XS_DEFINE_CL_TRACE(65) D3D12XS_DEFINE_CL_TRACE(66) D3D12XS_DEFINE_CL_TRACE(67)
D3D12XS_DEFINE_CL_TRACE(68) D3D12XS_DEFINE_CL_TRACE(69) D3D12XS_DEFINE_CL_TRACE(73)
D3D12XS_DEFINE_CL_TRACE(76) D3D12XS_DEFINE_CL_TRACE(77) D3D12XS_DEFINE_CL_TRACE(78) D3D12XS_DEFINE_CL_TRACE(79)
D3D12XS_DEFINE_CL_TRACE(80)
D3D12XS_DEFINE_CL_TRACE(127) D3D12XS_DEFINE_CL_TRACE(128) D3D12XS_DEFINE_CL_TRACE(129) D3D12XS_DEFINE_CL_TRACE(130)
D3D12XS_DEFINE_CL_TRACE(133) D3D12XS_DEFINE_CL_TRACE(134) D3D12XS_DEFINE_CL_TRACE(135) D3D12XS_DEFINE_CL_TRACE(136)
D3D12XS_DEFINE_CL_TRACE(137) D3D12XS_DEFINE_CL_TRACE(138) D3D12XS_DEFINE_CL_TRACE(140) D3D12XS_DEFINE_CL_TRACE(141)
D3D12XS_DEFINE_CL_TRACE(143) D3D12XS_DEFINE_CL_TRACE(144) D3D12XS_DEFINE_CL_TRACE(145) D3D12XS_DEFINE_CL_TRACE(147)
D3D12XS_DEFINE_CL_TRACE(148) D3D12XS_DEFINE_CL_TRACE(149) D3D12XS_DEFINE_CL_TRACE(150)

static scarlett_hresult_t SCARLETT_D3D12XS_CALL cmd_list_slot_noop4(
    uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {
    static uint32_t g_noop4_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_noop4_seq, 1u, __ATOMIC_RELAXED);
    if (seq <= 16u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "ID3D12GraphicsCommandListX::Noop4", SCARLETT_STUB_SLOT_NONE,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            rcx, rdx, r8, r9, SCARLETT_OK, "(noop4)");
    }
    return SCARLETT_OK;
}

static const ID3D12GraphicsCommandListXVtbl g_cmd_list_vtbl = {
    {
        (void *)cmd_list_query_interface,       /* slot 0 */
        (void *)cmd_list_add_ref,               /* slot 1 */
        (void *)cmd_list_release,               /* slot 2 */
        (void *)cmd_list_vtbl_slot_stub_3,      /* slot 3 */
        (void *)cmd_list_vtbl_slot_stub_4,      /* slot 4 */
        (void *)cmd_list_vtbl_slot_stub_5,      /* slot 5 */
        (void *)cmd_list_vtbl_slot_stub_6,      /* slot 6 */
        (void *)cmd_list_vtbl_slot_stub_7,      /* slot 7 */
        (void *)cmd_list_get_type,              /* slot 8 (+0x40) */
        (void *)cmd_list_close,                 /* slot 9 (+0x48) */
        (void *)cmd_list_reset,                 /* slot 10 (+0x50) */
        (void *)cmd_list_slot11_clear_state,    /* slot 11 -> PC 11 */
        (void *)cmd_list_slot12_draw_instanced, /* slot 12 -> PC 12 */
        (void *)cmd_list_slot13_draw_indexed_instanced, /* slot 13 -> PC 13 */
        (void *)cmd_list_slot14_dispatch,       /* slot 14 -> PC 14 */
        (void *)cmd_list_slot15_copy_buffer_region,  /* slot 15 -> PC 15 */
        (void *)cmd_list_slot16_copy_texture_region, /* slot 16 -> PC 16 */
        (void *)cmd_list_slot17_copy_resource,       /* slot 17 -> PC 17 */
        (void *)cmd_list_trace_18,              /* slot 18 */
        (void *)cmd_list_slot19_resolve_subresource, /* slot 19 -> PC 19 */
        (void *)cmd_list_slot20_ia_set_primitive_topology, /* slot 20 -> PC 20 */
        (void *)cmd_list_slot21_rs_set_viewports,     /* slot 21 -> PC 21 */
        (void *)cmd_list_slot22_rs_set_scissor_rects, /* slot 22 -> PC 22 */
        (void *)cmd_list_slot23_om_set_blend_factor,  /* slot 23 -> PC 23 */
        (void *)cmd_list_slot24_om_set_stencil_ref,   /* slot 24 -> PC 24 */
        (void *)cmd_list_slot25_set_pipeline_state,   /* slot 25 -> PC 25 */
        (void *)cmd_list_trace_26,              /* slot 26 */
        (void *)cmd_list_trace_27,              /* slot 27 */
        (void *)cmd_list_trace_28,              /* slot 28 */
        (void *)cmd_list_trace_29,              /* slot 29 */
        (void *)cmd_list_trace_30,              /* slot 30 */
        (void *)cmd_list_trace_31,              /* slot 31 */
        (void *)cmd_list_slot32_set_descriptor_heaps,            /* slot 32 -> PC 28 */
        (void *)cmd_list_slot33_set_compute_root_signature,      /* slot 33 -> PC 29 */
        (void *)cmd_list_slot34_set_graphics_root_signature,     /* slot 34 -> PC 30 */
        (void *)cmd_list_slot35_set_compute_root_descriptor_table,  /* slot 35 -> PC 31 */
        (void *)cmd_list_slot36_set_graphics_root_descriptor_table, /* slot 36 -> PC 32 */
        (void *)cmd_list_slot37_set_compute_root_32bit_constant,    /* slot 37 -> PC 33 */
        (void *)cmd_list_slot38_set_graphics_root_32bit_constant,   /* slot 38 -> PC 34 */
        (void *)cmd_list_slot39_set_compute_root_32bit_constants,   /* slot 39 -> PC 35 */
        (void *)cmd_list_slot40_set_graphics_root_32bit_constants,  /* slot 40 -> PC 36 */
        (void *)cmd_list_slot41_set_compute_root_cbv,            /* slot 41 -> PC 37 */
        (void *)cmd_list_slot42_set_graphics_root_cbv,           /* slot 42 -> PC 38 */
        (void *)cmd_list_slot43_set_compute_root_srv,            /* slot 43 -> PC 39 */
        (void *)cmd_list_slot44_set_graphics_root_srv,           /* slot 44 -> PC 40 */
        (void *)cmd_list_slot45_set_compute_root_uav,            /* slot 45 -> PC 41 */
        (void *)cmd_list_slot46_set_graphics_root_uav,           /* slot 46 -> PC 42 */
        (void *)cmd_list_slot47_ia_set_index_buffer,             /* slot 47 -> PC 43 */
        (void *)cmd_list_slot48_ia_set_vertex_buffers,           /* slot 48 -> PC 44 */
        (void *)cmd_list_slot49_so_set_targets,                  /* slot 49 -> PC 45 */
        (void *)cmd_list_slot50_om_set_render_targets,           /* slot 50 -> PC 46 */
        (void *)cmd_list_slot51_clear_dsv,                       /* slot 51 -> PC 47 */
        (void *)cmd_list_slot52_clear_rtv,                       /* slot 52 -> PC 48 */
        (void *)cmd_list_slot53_clear_uav_uint,                  /* slot 53 -> PC 49 */
        (void *)cmd_list_slot54_clear_uav_float,                 /* slot 54 -> PC 50 */
        (void *)cmd_list_slot55_discard_resource,                /* slot 55 -> PC 51 */
        (void *)cmd_list_trace_56,              /* slot 56 */
        (void *)cmd_list_trace_57,              /* slot 57 */
        (void *)cmd_list_trace_58,              /* slot 58 */
        (void *)cmd_list_slot59_write_buffer_immediate, /* slot 59 */
        (void *)cmd_list_trace_60,              /* slot 60 */
        (void *)cmd_list_trace_61,              /* slot 61 */
        (void *)cmd_list_trace_62,              /* slot 62 */
        (void *)cmd_list_trace_63,              /* slot 63 */
        (void *)cmd_list_trace_64,              /* slot 64 */
        (void *)cmd_list_trace_65,              /* slot 65 */
        (void *)cmd_list_trace_66,              /* slot 66 */
        (void *)cmd_list_trace_67,              /* slot 67 */
        (void *)cmd_list_trace_68,              /* slot 68 */
        (void *)cmd_list_trace_69,              /* slot 69 */
        (void *)cmd_list_slot70_om_set_depth_bounds, /* slot 70 -> PC 62 */
        (void *)cmd_list_slot71_write_gpu_memory_label_x, /* slot 71 (+0x238) */
        (void *)cmd_list_slot72_xbox_noop,      /* slot 72 */
        (void *)cmd_list_trace_73,              /* slot 73 */
        (void *)cmd_list_slot74_xbox_noop,      /* slot 74 */
        (void *)cmd_list_slot75_xbox_noop,      /* slot 75 */
        (void *)cmd_list_trace_76,              /* slot 76 */
        (void *)cmd_list_trace_77,              /* slot 77 */
        (void *)cmd_list_trace_78,              /* slot 78 */
        (void *)cmd_list_trace_79,              /* slot 79 */
        (void *)cmd_list_trace_80,              /* slot 80 */
        (void *)cmd_list_slot_noop4,            /* slot 81 */
        (void *)cmd_list_slot_noop4,            /* slot 82 */
        (void *)cmd_list_slot_noop4,            /* slot 83 */
        (void *)cmd_list_slot_noop4,            /* slot 84 */
        (void *)cmd_list_slot_noop4,            /* slot 85 */
        (void *)cmd_list_slot_noop4,            /* slot 86 */
        (void *)cmd_list_slot_noop4,            /* slot 87 */
        (void *)cmd_list_slot_noop4,            /* slot 88 */
        (void *)cmd_list_slot_noop4,            /* slot 89 */
        (void *)cmd_list_slot_noop4,            /* slot 90 */
        (void *)cmd_list_slot_noop4,            /* slot 91 */
        (void *)cmd_list_slot_noop4,            /* slot 92 */
        (void *)cmd_list_slot_noop4,            /* slot 93 */
        (void *)cmd_list_slot_noop4,            /* slot 94 */
        (void *)cmd_list_slot_noop4,            /* slot 95 */
        (void *)cmd_list_slot_noop4,            /* slot 96 */
        (void *)cmd_list_slot_noop4,            /* slot 97 */
        (void *)cmd_list_slot_noop4,            /* slot 98 */
        (void *)cmd_list_slot_noop4,            /* slot 99 */
        (void *)cmd_list_slot_noop4,            /* slot 100 */
        (void *)cmd_list_slot_noop4,            /* slot 101 */
        (void *)cmd_list_slot_noop4,            /* slot 102 */
        (void *)cmd_list_slot_noop4,            /* slot 103 */
        (void *)cmd_list_slot_noop4,            /* slot 104 */
        (void *)cmd_list_slot_noop4,            /* slot 105 */
        (void *)cmd_list_slot_noop4,            /* slot 106 */
        (void *)cmd_list_slot_noop4,            /* slot 107 */
        (void *)cmd_list_slot_noop4,            /* slot 108 */
        (void *)cmd_list_slot_noop4,            /* slot 109 */
        (void *)cmd_list_slot_noop4,            /* slot 110 */
        (void *)cmd_list_slot_noop4,            /* slot 111 */
        (void *)cmd_list_slot_noop4,            /* slot 112 */
        (void *)cmd_list_slot_noop4,            /* slot 113 */
        (void *)cmd_list_slot_noop4,            /* slot 114 */
        (void *)cmd_list_slot_noop4,            /* slot 115 */
        (void *)cmd_list_slot_noop4,            /* slot 116 */
        (void *)cmd_list_slot_noop4,            /* slot 117 */
        (void *)cmd_list_slot_noop4,            /* slot 118 */
        (void *)cmd_list_slot_noop4,            /* slot 119 */
        (void *)cmd_list_slot_noop4,            /* slot 120 */
        (void *)cmd_list_slot_noop4,            /* slot 121 */
        (void *)cmd_list_slot_noop4,            /* slot 122 */
        (void *)cmd_list_slot_noop4,            /* slot 123 */
        (void *)cmd_list_slot_noop4,            /* slot 124 */
        (void *)cmd_list_slot_noop4,            /* slot 125 */
        (void *)cmd_list_slot126_ok,            /* slot 126 (+0x3f0) */
        (void *)cmd_list_trace_127,             /* slot 127 */
        (void *)cmd_list_trace_128,             /* slot 128 */
        (void *)cmd_list_trace_129,             /* slot 129 */
        (void *)cmd_list_trace_130,             /* slot 130 */
        (void *)cmd_list_xbox_mem_config_131,   /* slot 131 (+0x418) */
        (void *)cmd_list_xbox_mem_config_132,   /* slot 132 (+0x420) */
        (void *)cmd_list_trace_133,             /* slot 133 */
        (void *)cmd_list_trace_134,             /* slot 134 */
        (void *)cmd_list_trace_135,             /* slot 135 */
        (void *)cmd_list_trace_136,             /* slot 136 */
        (void *)cmd_list_trace_137,             /* slot 137 */
        (void *)cmd_list_trace_138,             /* slot 138 */
        (void *)cmd_list_xbox_ok_139,           /* slot 139 (+0x458) */
        (void *)cmd_list_trace_140,             /* slot 140 */
        (void *)cmd_list_trace_141,             /* slot 141 */
        (void *)cmd_list_xbox_ok_142,           /* slot 142 (+0x470) */
        (void *)cmd_list_trace_143,             /* slot 143 */
        (void *)cmd_list_trace_144,             /* slot 144 */
        (void *)cmd_list_trace_145,             /* slot 145 */
        (void *)cmd_list_xbox_ok_146,           /* slot 146 (+0x490) */
        (void *)cmd_list_trace_147,             /* slot 147 */
        (void *)cmd_list_trace_148,             /* slot 148 */
        (void *)cmd_list_trace_149,             /* slot 149 */
        (void *)cmd_list_trace_150,             /* slot 150 */
        (void *)cmd_list_slot_noop4,            /* slot 151 */
        (void *)cmd_list_slot_noop4,            /* slot 152 */
        (void *)cmd_list_slot_noop4,            /* slot 153 */
        (void *)cmd_list_slot_noop4,            /* slot 154 */
        (void *)cmd_list_slot_noop4,            /* slot 155 */
        (void *)cmd_list_slot_noop4,            /* slot 156 */
        (void *)cmd_list_slot_noop4,            /* slot 157 */
        (void *)cmd_list_slot_noop4,            /* slot 158 */
        (void *)cmd_list_slot_noop4            /* slot 159 */
    }
};

/* ============================================================================
 * ID3D12DescriptorHeapX Static COM Object Pool & 32-Slot VTable
 * ============================================================================ */

typedef struct scarlett_d3d12_desc_heap_desc_s {
    uint32_t Type;
    uint32_t NumDescriptors;
    uint32_t Flags;
    uint32_t NodeMask;
} scarlett_d3d12_desc_heap_desc_t;

typedef struct ID3D12DescriptorHeapXVtbl_S {
    void *slots[32];
} ID3D12DescriptorHeapXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12DescriptorHeapX_S {
    const ID3D12DescriptorHeapXVtbl *lpVtbl;
    uint8_t inline_scratchpad[256u - sizeof(void *)];
} ID3D12DescriptorHeapX;

typedef struct d3d12xs_desc_heap_meta_s {
    uint64_t unix_device_handle;
    uint64_t unix_heap_handle;
    uint64_t cpu_start_addr;
    uint64_t gpu_start_addr;
    uint64_t real_cpu_start;
    uint64_t real_gpu_start;
    uint64_t null_desc_cpu;
    scarlett_d3d12_desc_heap_desc_t desc;
    uint32_t real_num_descriptors;
    uint32_t real_inc_size;
    uint32_t xbox_inc_size;
    uint32_t staging_start;
    uint32_t staging_cursor;
    uint32_t ref_count;
    uint32_t in_use;
    void *real_obj;
    void *owner_real_device;
} d3d12xs_desc_heap_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS 256u
static ID3D12DescriptorHeapX g_com_desc_heaps[SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS];
static d3d12xs_desc_heap_meta_t g_desc_heap_meta[SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS];

static inline d3d12xs_desc_heap_meta_t *d3d12xs_get_desc_heap_meta(const void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_desc_heaps[0];
    uintptr_t end = (uintptr_t)&g_com_desc_heaps[SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS];
    if (p < base || p >= end || ((p - base) % sizeof(ID3D12DescriptorHeapX)) != 0u) {
        return NULL;
    }
    size_t idx = (size_t)((const ID3D12DescriptorHeapX *)ptr - &g_com_desc_heaps[0]);
    return &g_desc_heap_meta[idx];
}

static void *d3d12xs_desc_heap_get_real(const void *ptr) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(ptr);
    return (hm != NULL && hm->in_use != 0u) ? hm->real_obj : NULL;
}

static uint32_t d3d12xs_desc_heap_get_type(const void *ptr) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(ptr);
    return (hm != NULL && hm->in_use != 0u) ? hm->desc.Type : 0xFFFFFFFFu;
}

#define SCARLETT_DESC_COOKIE_MAGIC UINT64_C(0x5343415244455343) /* "SCARDESC" */
typedef struct scarlett_desc_cookie_s {
    uint64_t magic;
    uint64_t real_cpu;
    uint64_t real_gpu;
    uint32_t heap_type;
    uint32_t checksum;
} scarlett_desc_cookie_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_desc_cookie_t) == 32u,
                       "scarlett_desc_cookie_t must be 32 bytes");

static inline uint32_t d3d12xs_desc_cookie_checksum(
    uint64_t real_cpu, uint64_t real_gpu, uint32_t heap_type) {
    uint64_t x = real_cpu ^ (real_gpu * UINT64_C(0x9E3779B97F4A7C15)) ^
                 ((uint64_t)heap_type << 17u) ^ UINT64_C(0xA55AA55A5A5AA55A);
    return (uint32_t)(x ^ (x >> 32u));
}

static void d3d12xs_stamp_desc_cookie(uint64_t xbox_cpu_handle) {
    if (xbox_cpu_handle == 0u ||
        !d3d12xs_pe_is_readable((const void *)(uintptr_t)xbox_cpu_handle, sizeof(scarlett_desc_cookie_t)) ||
        !d3d12xs_pe_is_writable_va(xbox_cpu_handle, sizeof(scarlett_desc_cookie_t))) {
        return;
    }
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
        d3d12xs_desc_heap_meta_t *hm = &g_desc_heap_meta[i];
        if (hm->in_use == 0u || hm->real_obj == NULL || hm->real_cpu_start == 0u) {
            continue;
        }
        uint64_t inc = (hm->xbox_inc_size > 0u) ? (uint64_t)hm->xbox_inc_size : 32u;
        uint64_t span = (uint64_t)hm->desc.NumDescriptors * inc;
        if (xbox_cpu_handle >= hm->cpu_start_addr && xbox_cpu_handle < hm->cpu_start_addr + span) {
            uint64_t index = (xbox_cpu_handle - hm->cpu_start_addr) / inc;
            uint32_t real_limit = (hm->real_num_descriptors > 0u &&
                                   hm->real_num_descriptors < hm->desc.NumDescriptors)
                ? hm->real_num_descriptors
                : hm->desc.NumDescriptors;
            if (index >= (uint64_t)real_limit) {
                return;
            }
            scarlett_desc_cookie_t cookie;
            cookie.magic = SCARLETT_DESC_COOKIE_MAGIC;
            cookie.real_cpu = hm->real_cpu_start + index * (uint64_t)hm->real_inc_size;
            cookie.real_gpu = (hm->real_gpu_start != 0u)
                ? (hm->real_gpu_start + index * (uint64_t)hm->real_inc_size)
                : 0u;
            cookie.heap_type = hm->desc.Type;
            cookie.checksum = d3d12xs_desc_cookie_checksum(
                cookie.real_cpu, cookie.real_gpu, cookie.heap_type);
            d3d12xs_pe_memcpy((void *)(uintptr_t)xbox_cpu_handle, &cookie, sizeof(cookie));
            return;
        }
    }
}

static bool d3d12xs_read_desc_cookie(
    uint64_t addr, uint32_t expected_heap_type, uint64_t *out_real_cpu) {
    if (out_real_cpu != NULL) {
        *out_real_cpu = 0u;
    }
    if (addr < UINT64_C(0x10000) ||
        !d3d12xs_pe_is_readable((const void *)(uintptr_t)addr, sizeof(scarlett_desc_cookie_t))) {
        return false;
    }
    scarlett_desc_cookie_t cookie;
    d3d12xs_pe_memcpy(&cookie, (const void *)(uintptr_t)addr, sizeof(cookie));
    if (cookie.magic != SCARLETT_DESC_COOKIE_MAGIC || cookie.real_cpu == 0u) {
        return false;
    }
    if (cookie.checksum != d3d12xs_desc_cookie_checksum(
            cookie.real_cpu, cookie.real_gpu, cookie.heap_type)) {
        return false;
    }
    if (expected_heap_type != 0xFFFFFFFFu && cookie.heap_type != expected_heap_type) {
        return false;
    }
    if (out_real_cpu != NULL) {
        *out_real_cpu = cookie.real_cpu;
    }
    return true;
}

static bool d3d12xs_root_sig_get_table_info(
    const void *rs_ptr,
    uint32_t param_idx,
    uint32_t *out_heap_type,
    uint32_t *out_count,
    bool *out_unbounded);

#define SCARLETT_D3D12XS_DESC_RES_MAP_CAP 16384u
typedef struct d3d12xs_desc_res_entry_s {
    uint64_t cpu_handle;
    void *res;
} d3d12xs_desc_res_entry_t;

static d3d12xs_desc_res_entry_t *g_desc_res_map = NULL;
static uint32_t g_desc_res_map_init_state = 0u;
static uint32_t g_desc_res_map_lock = 0u;

static bool d3d12xs_ensure_desc_res_map(void) {
    if (__atomic_load_n(&g_desc_res_map_init_state, __ATOMIC_ACQUIRE) == 2u) {
        return g_desc_res_map != NULL;
    }
    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(
            &g_desc_res_map_init_state, &expected, 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        g_desc_res_map = (d3d12xs_desc_res_entry_t *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_DESC_RES_MAP_CAP * sizeof(d3d12xs_desc_res_entry_t));
        __atomic_store_n(&g_desc_res_map_init_state, 2u, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&g_desc_res_map_init_state, __ATOMIC_ACQUIRE) != 2u) {
        }
    }
    return g_desc_res_map != NULL;
}

static inline uint32_t d3d12xs_desc_res_hash(uint64_t handle) {
    uint64_t x = handle * UINT64_C(0x9E3779B97F4A7C15);
    return (uint32_t)((x ^ (x >> 32u)) & (SCARLETT_D3D12XS_DESC_RES_MAP_CAP - 1u));
}

static void d3d12xs_record_desc_resource(uint64_t xbox_cpu_handle, void *res) {
    if (xbox_cpu_handle == 0u || !d3d12xs_ensure_desc_res_map()) {
        return;
    }
    while (__atomic_exchange_n(&g_desc_res_map_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    uint32_t slot = d3d12xs_desc_res_hash(xbox_cpu_handle);
    for (uint32_t probe = 0u; probe < 64u; ++probe) {
        uint32_t idx = (slot + probe) & (SCARLETT_D3D12XS_DESC_RES_MAP_CAP - 1u);
        uint64_t cur = g_desc_res_map[idx].cpu_handle;
        if (cur == 0u || cur == xbox_cpu_handle) {
            g_desc_res_map[idx].cpu_handle = xbox_cpu_handle;
            g_desc_res_map[idx].res = res;
            __atomic_store_n(&g_desc_res_map_lock, 0u, __ATOMIC_RELEASE);
            return;
        }
    }
    /* Evict hash head on saturation */
    g_desc_res_map[slot].cpu_handle = xbox_cpu_handle;
    g_desc_res_map[slot].res = res;
    __atomic_store_n(&g_desc_res_map_lock, 0u, __ATOMIC_RELEASE);
}

static void *d3d12xs_lookup_desc_resource(uint64_t xbox_cpu_handle) {
    if (xbox_cpu_handle == 0u ||
        __atomic_load_n(&g_desc_res_map_init_state, __ATOMIC_ACQUIRE) != 2u ||
        g_desc_res_map == NULL) {
        return NULL;
    }
    while (__atomic_exchange_n(&g_desc_res_map_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    void *found = NULL;
    uint32_t slot = d3d12xs_desc_res_hash(xbox_cpu_handle);
    for (uint32_t probe = 0u; probe < 64u; ++probe) {
        uint32_t idx = (slot + probe) & (SCARLETT_D3D12XS_DESC_RES_MAP_CAP - 1u);
        uint64_t cur = g_desc_res_map[idx].cpu_handle;
        if (cur == 0u) {
            break;
        }
        if (cur == xbox_cpu_handle) {
            found = g_desc_res_map[idx].res;
            break;
        }
    }
    __atomic_store_n(&g_desc_res_map_lock, 0u, __ATOMIC_RELEASE);
    return (d3d12xs_get_res_meta(found) != NULL) ? found : NULL;
}

static void *d3d12xs_lookup_desc_resource_offset(uint64_t xbox_cpu_handle, uint32_t index) {
    if (xbox_cpu_handle == 0u) {
        return NULL;
    }
    if (index == 0u) {
        return d3d12xs_lookup_desc_resource(xbox_cpu_handle);
    }
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
        d3d12xs_desc_heap_meta_t *hm = &g_desc_heap_meta[i];
        if (hm->in_use == 0u || hm->cpu_start_addr == 0u) {
            continue;
        }
        uint64_t inc = (hm->xbox_inc_size > 0u) ? (uint64_t)hm->xbox_inc_size : 32u;
        uint64_t span = (uint64_t)hm->desc.NumDescriptors * inc;
        if (xbox_cpu_handle >= hm->cpu_start_addr && xbox_cpu_handle < hm->cpu_start_addr + span) {
            return d3d12xs_lookup_desc_resource(xbox_cpu_handle + (uint64_t)index * inc);
        }
    }
    return NULL;
}

static void d3d12xs_copy_desc_resource_range(
    uint64_t dst_cpu_start, uint64_t src_cpu_start, uint32_t count) {
    if (dst_cpu_start == 0u || src_cpu_start == 0u || count == 0u) {
        return;
    }
    uint64_t dst_inc = 32u;
    uint64_t src_inc = 32u;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
        d3d12xs_desc_heap_meta_t *hm = &g_desc_heap_meta[i];
        if (hm->in_use == 0u || hm->cpu_start_addr == 0u) {
            continue;
        }
        uint64_t inc = (hm->xbox_inc_size > 0u) ? (uint64_t)hm->xbox_inc_size : 32u;
        uint64_t span = (uint64_t)hm->desc.NumDescriptors * inc;
        if (dst_cpu_start >= hm->cpu_start_addr && dst_cpu_start < hm->cpu_start_addr + span) {
            dst_inc = inc;
        }
        if (src_cpu_start >= hm->cpu_start_addr && src_cpu_start < hm->cpu_start_addr + span) {
            src_inc = inc;
        }
    }
    uint32_t lim = (count < 64u) ? count : 64u;
    for (uint32_t k = 0u; k < lim; ++k) {
        void *res = d3d12xs_lookup_desc_resource(src_cpu_start + (uint64_t)k * src_inc);
        if (res != NULL) {
            d3d12xs_record_desc_resource(dst_cpu_start + (uint64_t)k * dst_inc, res);
        }
    }
}

static uint64_t d3d12xs_translate_cpu_desc_handle_range(
    uint64_t xbox_cpu_handle, uint32_t count, uint32_t *out_heap_type) {
    if (xbox_cpu_handle == 0u) {
        return 0u;
    }
    uint64_t req_count = (count > 0u) ? (uint64_t)count : 1u;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
        d3d12xs_desc_heap_meta_t *hm = &g_desc_heap_meta[i];
        if (hm->in_use == 0u || hm->real_obj == NULL || hm->real_cpu_start == 0u) {
            continue;
        }
        uint64_t inc = (hm->xbox_inc_size > 0u) ? (uint64_t)hm->xbox_inc_size : 32u;
        uint64_t span = (uint64_t)hm->desc.NumDescriptors * inc;
        if (xbox_cpu_handle >= hm->cpu_start_addr && xbox_cpu_handle < hm->cpu_start_addr + span) {
            uint64_t index = (xbox_cpu_handle - hm->cpu_start_addr) / inc;
            if (out_heap_type != NULL) {
                *out_heap_type = hm->desc.Type;
            }
            uint32_t real_limit = (hm->real_num_descriptors > 0u &&
                                   hm->real_num_descriptors < hm->desc.NumDescriptors)
                ? hm->real_num_descriptors
                : hm->desc.NumDescriptors;
            if (index < (uint64_t)real_limit &&
                req_count <= ((uint64_t)real_limit - index)) {
                return hm->real_cpu_start + index * (uint64_t)hm->real_inc_size;
            }
            static uint32_t g_oob_cpu_logged = 0u;
            if (__atomic_fetch_add(&g_oob_cpu_logged, 1u, __ATOMIC_RELAXED) < 8u) {
                d3d12xs_pe_log_stub_ods(
                    "d3d12_xs.dll",
                    "TranslateCpuDescHandle(oob)",
                    14u,
                    (uint64_t)(uintptr_t)__builtin_return_address(0),
                    index,
                    req_count,
                    (uint64_t)real_limit,
                    (uint64_t)hm->desc.NumDescriptors,
                    SCARLETT_E_INVALIDARG,
                    "(out_of_range)");
            }
            return 0u;
        }
    }
    return 0u;
}

static uint64_t d3d12xs_translate_cpu_desc_handle(
    uint64_t xbox_cpu_handle, uint32_t *out_heap_type) {
    return d3d12xs_translate_cpu_desc_handle_range(xbox_cpu_handle, 1u, out_heap_type);
}

static uint64_t d3d12xs_translate_gpu_desc_handle(
    uint64_t xbox_gpu_handle, uint32_t *out_heap_type) {
    if (xbox_gpu_handle == 0u) {
        return 0u;
    }
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
        d3d12xs_desc_heap_meta_t *hm = &g_desc_heap_meta[i];
        if (hm->in_use == 0u || hm->real_obj == NULL || hm->real_gpu_start == 0u) {
            continue;
        }
        uint64_t inc = (hm->xbox_inc_size > 0u) ? (uint64_t)hm->xbox_inc_size : 32u;
        uint64_t span = (uint64_t)hm->desc.NumDescriptors * inc;
        if (xbox_gpu_handle >= hm->gpu_start_addr && xbox_gpu_handle < hm->gpu_start_addr + span) {
            uint64_t index = (xbox_gpu_handle - hm->gpu_start_addr) / inc;
            if (out_heap_type != NULL) {
                *out_heap_type = hm->desc.Type;
            }
            uint32_t real_limit = (hm->real_num_descriptors > 0u &&
                                   hm->real_num_descriptors < hm->desc.NumDescriptors)
                ? hm->real_num_descriptors
                : hm->desc.NumDescriptors;
            if (index < (uint64_t)real_limit) {
                return hm->real_gpu_start + index * (uint64_t)hm->real_inc_size;
            }
            static uint32_t g_oob_gpu_logged = 0u;
            if (__atomic_fetch_add(&g_oob_gpu_logged, 1u, __ATOMIC_RELAXED) < 8u) {
                d3d12xs_pe_log_stub_ods(
                    "d3d12_xs.dll",
                    "TranslateGpuDescHandle(oob)",
                    14u,
                    (uint64_t)(uintptr_t)__builtin_return_address(0),
                    index,
                    1u,
                    (uint64_t)real_limit,
                    (uint64_t)hm->desc.NumDescriptors,
                    SCARLETT_E_INVALIDARG,
                    "(out_of_range)");
            }
            return 0u;
        }
    }
    return 0u;
}

#if !defined(_WIN32) && !defined(__MINGW32__)
SCARLETT_D3D12XS_API uint64_t scarlett_d3d12xs_test_translate_gpu_desc_handle(
    uint64_t xbox_gpu_handle, uint32_t *out_heap_type) {
    return d3d12xs_translate_gpu_desc_handle(xbox_gpu_handle, out_heap_type);
}
#endif

static uint64_t d3d12xs_stage_descriptor_table(
    d3d12xs_cmd_list_meta_t *clm,
    bool is_compute,
    uint32_t root_param_idx,
    uint64_t base_desc_va) {
    if (clm == NULL || base_desc_va < UINT64_C(0x10000)) {
        return 0u;
    }
    void *rs_ptr = is_compute ? clm->bound_comp_rs : clm->bound_gfx_rs;
    uint32_t heap_type = 0u;
    uint32_t table_count = 0u;
    bool is_unbounded = true;
    (void)d3d12xs_root_sig_get_table_info(
        rs_ptr, root_param_idx, &heap_type, &table_count, &is_unbounded);

    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(
        (heap_type == 1u) ? clm->bound_sampler_heap : clm->bound_srv_heap);
    if (hm == NULL || hm->in_use == 0u || hm->real_obj == NULL ||
        hm->real_gpu_start == 0u || hm->staging_start == 0u) {
        for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
            d3d12xs_desc_heap_meta_t *cand = &g_desc_heap_meta[i];
            if (cand->in_use != 0u && cand->real_obj != NULL &&
                cand->desc.Type == heap_type && (cand->desc.Flags & 0x1u) != 0u &&
                cand->real_gpu_start != 0u && cand->staging_start > 0u) {
                hm = cand;
                break;
            }
        }
    }
    if (hm == NULL || hm->real_obj == NULL || hm->owner_real_device == NULL ||
        hm->real_cpu_start == 0u || hm->real_gpu_start == 0u ||
        hm->staging_start == 0u || hm->real_num_descriptors <= hm->staging_start + 1u) {
        return 0u;
    }

    uint32_t staging_end = hm->real_num_descriptors - 1u; /* reserve last slot for null_desc_cpu */
    uint32_t staging_cap = staging_end - hm->staging_start;
    if (staging_cap == 0u) {
        return 0u;
    }

    uint32_t copy_count = table_count;
    if (copy_count == 0u || is_unbounded) {
        uint32_t scan_max = (staging_cap < 64u) ? staging_cap : 64u;
        uint32_t last_valid = 0u;
        for (uint32_t k = 0u; k < scan_max; ++k) {
            uint64_t dummy = 0u;
            if (d3d12xs_read_desc_cookie(base_desc_va + (uint64_t)k * 32u, heap_type, &dummy)) {
                last_valid = k + 1u;
            }
        }
        if (last_valid > copy_count) {
            copy_count = last_valid;
        }
        if (copy_count == 0u) {
            copy_count = (table_count > 0u) ? table_count : 16u;
        }
    }
    if (copy_count > 128u) {
        copy_count = 128u;
    }
    if (copy_count > staging_cap) {
        copy_count = staging_cap;
    }

    uint64_t src_cpus[128];
    uint32_t src_sizes[128];
    uint32_t valid_cookies = 0u;
    uint32_t nonzero_raw = 0u;
    uint64_t first_raw_q0 = 0u;
    for (uint32_t k = 0u; k < copy_count; ++k) {
        uint64_t slot_va = base_desc_va + (uint64_t)k * 32u;
        uint64_t rcpu = 0u;
        if (d3d12xs_read_desc_cookie(slot_va, heap_type, &rcpu) && rcpu != 0u) {
            src_cpus[k] = rcpu;
            if (k == 0u && d3d12xs_pe_is_readable((const void *)(uintptr_t)slot_va, 32u)) {
                first_raw_q0 = ((const uint64_t *)(uintptr_t)slot_va)[2];
            }
            ++valid_cookies;
        } else {
            if (k == 0u && d3d12xs_pe_is_readable((const void *)(uintptr_t)slot_va, 8u)) {
                first_raw_q0 = *(const uint64_t *)(uintptr_t)slot_va;
            }
            if (d3d12xs_pe_is_readable((const void *)(uintptr_t)slot_va, 32u)) {
                const uint64_t *qw = (const uint64_t *)(uintptr_t)slot_va;
                if (qw[0] != 0u || qw[1] != 0u || qw[2] != 0u || qw[3] != 0u) {
                    ++nonzero_raw;
                }
            }
            src_cpus[k] = hm->null_desc_cpu;
        }
        src_sizes[k] = 1u;
    }

    if (hm->null_desc_cpu == 0u && valid_cookies < copy_count) {
        uint64_t ncpu = hm->real_cpu_start +
            (uint64_t)staging_end * (uint64_t)hm->real_inc_size;
        void **dev_vtbl = *(void ***)hm->owner_real_device;
        if (heap_type == 1u) {
            uint32_t null_samp[13];
            d3d12xs_pe_zero(null_samp, sizeof(null_samp));
            null_samp[0] = 0x15u;
            null_samp[1] = 3u;
            null_samp[2] = 3u;
            null_samp[3] = 3u;
            null_samp[5] = 1u;
            null_samp[6] = 1u;
            null_samp[12] = 0x447A0000u;
            typedef void (SCARLETT_D3D12XS_CALL *pfn_real_samp_t)(void *, const void *, uint64_t);
            ((pfn_real_samp_t)dev_vtbl[22])(hm->owner_real_device, null_samp, ncpu);
        } else {
            uint32_t null_srv[8];
            d3d12xs_pe_zero(null_srv, sizeof(null_srv));
            null_srv[0] = 28u;     /* DXGI_FORMAT_R8G8B8A8_UNORM */
            null_srv[1] = 4u;      /* D3D12_SRV_DIMENSION_TEXTURE2D */
            null_srv[2] = 0x1688u; /* D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING */
            null_srv[4] = 1u;      /* MipLevels = 1 */
            typedef void (SCARLETT_D3D12XS_CALL *pfn_real_srv_t)(void *, void *, const void *, uint64_t);
            ((pfn_real_srv_t)dev_vtbl[18])(hm->owner_real_device, NULL, null_srv, ncpu);
        }
        hm->null_desc_cpu = ncpu;
        for (uint32_t k = 0u; k < copy_count; ++k) {
            if (src_cpus[k] == 0u) {
                src_cpus[k] = ncpu;
            }
        }
    }

    uint32_t base_slot = __atomic_fetch_add(&hm->staging_cursor, copy_count, __ATOMIC_ACQ_REL);
    if (base_slot < hm->staging_start || base_slot + copy_count > staging_end) {
        base_slot = hm->staging_start;
        __atomic_store_n(&hm->staging_cursor, hm->staging_start + copy_count, __ATOMIC_RELEASE);
    }

    uint64_t dst_cpu = hm->real_cpu_start + (uint64_t)base_slot * (uint64_t)hm->real_inc_size;
    uint64_t dst_gpu = hm->real_gpu_start + (uint64_t)base_slot * (uint64_t)hm->real_inc_size;
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_cd_t)(
        void *, uint32_t, const uint64_t *, const uint32_t *,
        uint32_t, const uint64_t *, const uint32_t *, uint32_t);
    void **dev_vtbl = *(void ***)hm->owner_real_device;
    ((pfn_real_cd_t)dev_vtbl[23])(
        hm->owner_real_device, 1u, &dst_cpu, &copy_count,
        copy_count, src_cpus, src_sizes, heap_type);

    static uint32_t g_stage_log_seq = 0u;
    uint32_t sseq = __atomic_add_fetch(&g_stage_log_seq, 1u, __ATOMIC_RELAXED);
    if (sseq <= 12u || (sseq & 511u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "StageDescTable",
            is_compute ? 35u : 36u,
            base_desc_va,
            ((uint64_t)heap_type << 32u) | (uint64_t)root_param_idx,
            ((uint64_t)copy_count << 32u) | (uint64_t)valid_cookies,
            ((uint64_t)nonzero_raw << 32u) | (uint64_t)base_slot,
            first_raw_q0,
            SCARLETT_OK,
            (valid_cookies > 0u) ? "(staged_cookies)" : "(staged_null_only)");
    }
    return dst_gpu;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL desc_heap_query_interface(
    ID3D12DescriptorHeapX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(self);
    if (hm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12DescriptorHeap) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&hm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL desc_heap_add_ref(ID3D12DescriptorHeapX *self) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(self);
    if (hm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&hm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL desc_heap_release(ID3D12DescriptorHeapX *self) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(self);
    if (hm == NULL || __atomic_load_n(&hm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&hm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (hm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)hm->real_obj;
            ((pfn_rel_t)vtbl[2])(hm->real_obj);
            hm->real_obj = NULL;
        }
#if defined(_WIN32) || defined(__MINGW32__)
        if (hm->cpu_start_addr != 0u) {
            d3d12xs_pe_free_mem((void *)(uintptr_t)hm->cpu_start_addr);
        }
#endif
        hm->cpu_start_addr = 0u;
        hm->gpu_start_addr = 0u;
        hm->real_cpu_start = 0u;
        hm->real_gpu_start = 0u;
        hm->null_desc_cpu = 0u;
        hm->real_num_descriptors = 0u;
        hm->staging_start = 0u;
        hm->staging_cursor = 0u;
        hm->owner_real_device = NULL;
        hm->unix_device_handle = 0u;
        hm->unix_heap_handle = 0u;
        __atomic_store_n(&hm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

static void * SCARLETT_D3D12XS_CALL desc_heap_get_desc(void *arg1, void *arg2) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(arg1);
    scarlett_d3d12_desc_heap_desc_t *out_desc = NULL;
    if (hm != NULL) {
        out_desc = (scarlett_d3d12_desc_heap_desc_t *)arg2;
    } else {
        hm = d3d12xs_get_desc_heap_meta(arg2);
        out_desc = (scarlett_d3d12_desc_heap_desc_t *)arg1;
    }
    if (out_desc != NULL && hm != NULL) {
        d3d12xs_pe_memcpy(out_desc, &hm->desc, sizeof(*out_desc));
    }
    return (void *)out_desc;
}

static uint64_t SCARLETT_D3D12XS_CALL desc_heap_get_cpu_start(ID3D12DescriptorHeapX *self) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(self);
    return (hm != NULL) ? hm->cpu_start_addr : 0u;
}

static uint64_t SCARLETT_D3D12XS_CALL desc_heap_get_gpu_start(ID3D12DescriptorHeapX *self) {
    d3d12xs_desc_heap_meta_t *hm = d3d12xs_get_desc_heap_meta(self);
    return (hm != NULL) ? hm->gpu_start_addr : 0u;
}

D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 15, "15")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 16, "16")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 17, "17")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 18, "18")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 19, "19")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 20, "20")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 21, "21")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 22, "22")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 23, "23")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 24, "24")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 25, "25")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 26, "26")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 27, "27")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 28, "28")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 29, "29")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 30, "30")
D3D12XS_DEFINE_SLOT_STUB(desc_heap, "ID3D12DescriptorHeapX", 31, "31")

static const ID3D12DescriptorHeapXVtbl g_desc_heap_vtbl = {
    {
        (void *)desc_heap_query_interface,
        (void *)desc_heap_add_ref,
        (void *)desc_heap_release,
        (void *)desc_heap_vtbl_slot_stub_3,
        (void *)desc_heap_vtbl_slot_stub_4,
        (void *)desc_heap_vtbl_slot_stub_5,
        (void *)desc_heap_vtbl_slot_stub_6,
        (void *)desc_heap_vtbl_slot_stub_7,
        (void *)desc_heap_get_desc,      /* slot 8 (+0x40) */
        (void *)desc_heap_get_cpu_start, /* slot 9 (+0x48) */
        (void *)desc_heap_get_gpu_start, /* slot 10 (+0x50) */
        (void *)desc_heap_vtbl_slot_stub_11,
        (void *)desc_heap_vtbl_slot_stub_12,
        (void *)desc_heap_vtbl_slot_stub_13,
        (void *)desc_heap_vtbl_slot_stub_14,
        (void *)desc_heap_vtbl_slot_stub_15,
        (void *)desc_heap_vtbl_slot_stub_16,
        (void *)desc_heap_vtbl_slot_stub_17,
        (void *)desc_heap_vtbl_slot_stub_18,
        (void *)desc_heap_vtbl_slot_stub_19,
        (void *)desc_heap_vtbl_slot_stub_20,
        (void *)desc_heap_vtbl_slot_stub_21,
        (void *)desc_heap_vtbl_slot_stub_22,
        (void *)desc_heap_vtbl_slot_stub_23,
        (void *)desc_heap_vtbl_slot_stub_24,
        (void *)desc_heap_vtbl_slot_stub_25,
        (void *)desc_heap_vtbl_slot_stub_26,
        (void *)desc_heap_vtbl_slot_stub_27,
        (void *)desc_heap_vtbl_slot_stub_28,
        (void *)desc_heap_vtbl_slot_stub_29,
        (void *)desc_heap_vtbl_slot_stub_30,
        (void *)desc_heap_vtbl_slot_stub_31
    }
};

/* ============================================================================
 * ID3D12ResourceX Static COM Object Pool & 32-Slot VTable
 * ============================================================================ */

typedef struct ID3D12ResourceXVtbl_S {
    void *slots[32];
} ID3D12ResourceXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12ResourceX_S {
    const ID3D12ResourceXVtbl *lpVtbl;
    uint8_t inline_scratchpad[128u - sizeof(void *)];
} ID3D12ResourceX;

typedef struct d3d12xs_res_meta_s {
    uint64_t unix_device_handle;
    uint64_t unix_resource_handle;
    uint64_t gpu_va;
    uint64_t alloc_size;
    uint64_t real_gpu_va;
    scarlett_d3d12_resource_desc_t desc;
    uint32_t ref_count;
    uint32_t in_use;
    uint32_t owns_va;
    uint32_t gpu_dirty;
    uint32_t current_state;
    uint32_t last_readback_exec_seq;
    uint32_t ever_uploaded;
    uint32_t res_pad;
    void *real_obj;
    ID3D12DeviceX *owner_device;
} d3d12xs_res_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_RESOURCES 65536u
static ID3D12ResourceX *g_com_resources = NULL;
static d3d12xs_res_meta_t *g_res_meta = NULL;
static uint32_t g_res_pool_init_state = 0u;
static size_t g_res_high_water = 0u;

static bool d3d12xs_ensure_resource_pool(void) {
    if (__atomic_load_n(&g_res_pool_init_state, __ATOMIC_ACQUIRE) == 2u) {
        return g_com_resources != NULL && g_res_meta != NULL;
    }
    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(
            &g_res_pool_init_state, &expected, 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        g_com_resources = (ID3D12ResourceX *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_MAX_COM_RESOURCES * sizeof(ID3D12ResourceX));
        g_res_meta = (d3d12xs_res_meta_t *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_MAX_COM_RESOURCES * sizeof(d3d12xs_res_meta_t));
        __atomic_store_n(&g_res_pool_init_state, 2u, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&g_res_pool_init_state, __ATOMIC_ACQUIRE) != 2u) {
        }
    }
    return g_com_resources != NULL && g_res_meta != NULL;
}

static inline d3d12xs_res_meta_t *d3d12xs_get_res_meta(const void *ptr) {
    if (ptr == NULL || g_com_resources == NULL || g_res_meta == NULL) {
        return NULL;
    }
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_resources[0];
    uintptr_t end = (uintptr_t)&g_com_resources[SCARLETT_D3D12XS_MAX_COM_RESOURCES];
    if (p < base || p >= end || ((p - base) % sizeof(ID3D12ResourceX)) != 0u) {
        return NULL;
    }
    size_t idx = (size_t)((p - base) / sizeof(ID3D12ResourceX));
    return &g_res_meta[idx];
}

static inline bool d3d12xs_is_com_resource(const void *ptr) {
    return d3d12xs_get_res_meta(ptr) != NULL;
}

static void *d3d12xs_resource_get_real(const void *ptr) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(ptr);
    return (rm != NULL) ? rm->real_obj : NULL;
}

static void d3d12xs_resource_mark_gpu_dirty(const void *ptr) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(ptr);
    if (rm != NULL && rm->real_obj != NULL) {
        __atomic_store_n(&rm->gpu_dirty, 1u, __ATOMIC_RELEASE);
    }
}

static d3d12xs_res_meta_t *d3d12xs_find_res_meta_by_va(
    uint64_t xbox_va, void **out_wrapper_res) {
    if (out_wrapper_res != NULL) {
        *out_wrapper_res = NULL;
    }
    if (xbox_va == 0u || g_res_meta == NULL || g_com_resources == NULL) {
        return NULL;
    }
    size_t hw = __atomic_load_n(&g_res_high_water, __ATOMIC_ACQUIRE);
    if (hw > SCARLETT_D3D12XS_MAX_COM_RESOURCES) {
        hw = SCARLETT_D3D12XS_MAX_COM_RESOURCES;
    }
    d3d12xs_res_meta_t *best = NULL;
    size_t best_idx = 0u;
    for (size_t i = 0u; i < hw; ++i) {
        d3d12xs_res_meta_t *rm = &g_res_meta[i];
        if (rm->in_use != 0u && rm->real_obj != NULL &&
            rm->gpu_va != 0u && xbox_va >= rm->gpu_va &&
            xbox_va < rm->gpu_va + rm->alloc_size) {
            if (best == NULL || rm->alloc_size < best->alloc_size) {
                best = rm;
                best_idx = i;
            }
        }
    }
    if (best != NULL && out_wrapper_res != NULL) {
        *out_wrapper_res = (void *)&g_com_resources[best_idx];
    }
    return best;
}

static uint64_t d3d12xs_translate_gpu_va(uint64_t xbox_va, void **out_real_res) {
    if (out_real_res != NULL) {
        *out_real_res = NULL;
    }
    d3d12xs_res_meta_t *rm = d3d12xs_find_res_meta_by_va(xbox_va, NULL);
    if (rm == NULL || rm->real_obj == NULL || rm->real_gpu_va == 0u) {
        return 0u;
    }
    if (out_real_res != NULL) {
        *out_real_res = rm->real_obj;
    }
    return rm->real_gpu_va + (xbox_va - rm->gpu_va);
}

#define SCARLETT_D3D12XS_MAX_PENDING_UPLOADS 512u
#define SCARLETT_D3D12XS_MAX_TRACKED_CBVS 256u
typedef struct d3d12xs_pending_upload_s {
    d3d12xs_res_meta_t *rm;
    uint64_t offset;
    uint64_t size;
} d3d12xs_pending_upload_t;

typedef struct d3d12xs_tracked_cbv_s {
    d3d12xs_res_meta_t *rm;
    uint64_t offset;
    uint32_t size;
    uint32_t in_use;
    uint64_t last_hash;
} d3d12xs_tracked_cbv_t;

static uint32_t g_ul_queue_lock = 0u;
static d3d12xs_pending_upload_t g_pending_uploads[SCARLETT_D3D12XS_MAX_PENDING_UPLOADS];
static uint32_t g_pending_upload_count = 0u;
static d3d12xs_tracked_cbv_t g_tracked_cbvs[SCARLETT_D3D12XS_MAX_TRACKED_CBVS];
static uint32_t g_tracked_cbv_cursor = 0u;

static inline uint64_t d3d12xs_fnv1a_64(const void *ptr, size_t len) {
    const volatile uint8_t *p = (const volatile uint8_t *)ptr;
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    size_t step = (len > 256u) ? 4u : 1u;
    for (size_t i = 0u; i < len; i += step) {
        h ^= (uint64_t)p[i];
        h *= UINT64_C(0x100000001B3);
    }
    return h;
}

static inline bool d3d12xs_is_all_zero(const void *ptr, size_t len) {
    const volatile uint64_t *p64 = (const volatile uint64_t *)ptr;
    size_t n64 = len >> 3u;
    if (((uintptr_t)ptr & 7u) == 0u) {
        for (size_t i = 0u; i < n64; ++i) {
            if (p64[i] != 0u) {
                return false;
            }
        }
        const volatile uint8_t *p8 = (const volatile uint8_t *)ptr;
        for (size_t i = n64 << 3u; i < len; ++i) {
            if (p8[i] != 0u) {
                return false;
            }
        }
        return true;
    }
    const volatile uint8_t *p8 = (const volatile uint8_t *)ptr;
    for (size_t i = 0u; i < len; ++i) {
        if (p8[i] != 0u) {
            return false;
        }
    }
    return true;
}

static void d3d12xs_enqueue_cpu_upload_meta(
    d3d12xs_res_meta_t *rm, uint64_t offset, uint64_t size) {
    if (rm == NULL || rm->in_use == 0u || rm->real_obj == NULL ||
        rm->desc.Dimension != 1u || rm->gpu_va < UINT64_C(0x10000) ||
        __atomic_load_n(&rm->gpu_dirty, __ATOMIC_ACQUIRE) != 0u ||
        size == 0u || offset >= rm->alloc_size) {
        return;
    }
    uint64_t avail = rm->alloc_size - offset;
    if (size > avail) {
        size = avail;
    }
    if (size > (UINT64_C(4) << 20)) {
        size = (UINT64_C(4) << 20);
    }
    uint64_t src_va = rm->gpu_va + offset;
    if (!d3d12xs_pe_is_readable((const void *)(uintptr_t)src_va, (size_t)size)) {
        return;
    }
    if (__atomic_load_n(&rm->ever_uploaded, __ATOMIC_ACQUIRE) == 0u &&
        d3d12xs_is_all_zero((const void *)(uintptr_t)src_va, (size_t)size)) {
        return;
    }
    while (__atomic_exchange_n(&g_ul_queue_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    for (uint32_t i = 0u; i < g_pending_upload_count; ++i) {
        if (g_pending_uploads[i].rm == rm &&
            g_pending_uploads[i].offset == offset &&
            g_pending_uploads[i].size >= size) {
            __atomic_store_n(&g_ul_queue_lock, 0u, __ATOMIC_RELEASE);
            return;
        }
    }
    if (g_pending_upload_count < SCARLETT_D3D12XS_MAX_PENDING_UPLOADS) {
        uint32_t idx = g_pending_upload_count++;
        g_pending_uploads[idx].rm = rm;
        g_pending_uploads[idx].offset = offset;
        g_pending_uploads[idx].size = size;
    }
    __atomic_store_n(&g_ul_queue_lock, 0u, __ATOMIC_RELEASE);
}

static void d3d12xs_enqueue_cpu_upload_res(
    const void *res_ptr, uint64_t offset, uint64_t size) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(res_ptr);
    d3d12xs_enqueue_cpu_upload_meta(rm, offset, size);
}

static void d3d12xs_enqueue_cpu_upload_va(uint64_t xbox_va, uint64_t size) {
    if (xbox_va < UINT64_C(0x10000) || size == 0u) {
        return;
    }
    d3d12xs_res_meta_t *rm = d3d12xs_find_res_meta_by_va(xbox_va, NULL);
    if (rm != NULL && xbox_va >= rm->gpu_va) {
        d3d12xs_enqueue_cpu_upload_meta(rm, xbox_va - rm->gpu_va, size);
    }
}

static void d3d12xs_track_cbv(uint64_t xbox_va, uint32_t size) {
    if (xbox_va < UINT64_C(0x10000) || size == 0u) {
        return;
    }
    d3d12xs_res_meta_t *rm = d3d12xs_find_res_meta_by_va(xbox_va, NULL);
    if (rm == NULL || rm->desc.Dimension != 1u || rm->real_obj == NULL ||
        xbox_va < rm->gpu_va) {
        return;
    }
    uint64_t offset = xbox_va - rm->gpu_va;
    if (offset >= rm->alloc_size) {
        return;
    }
    if ((uint64_t)size > rm->alloc_size - offset) {
        size = (uint32_t)(rm->alloc_size - offset);
    }
    if (size > 65536u) {
        size = 65536u;
    }
    d3d12xs_enqueue_cpu_upload_meta(rm, offset, size);
    while (__atomic_exchange_n(&g_ul_queue_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    for (uint32_t i = 0u; i < SCARLETT_D3D12XS_MAX_TRACKED_CBVS; ++i) {
        if (g_tracked_cbvs[i].in_use != 0u &&
            g_tracked_cbvs[i].rm == rm &&
            g_tracked_cbvs[i].offset == offset) {
            g_tracked_cbvs[i].size = size;
            g_tracked_cbvs[i].last_hash = 0u;
            __atomic_store_n(&g_ul_queue_lock, 0u, __ATOMIC_RELEASE);
            return;
        }
    }
    uint32_t slot = g_tracked_cbv_cursor++ % SCARLETT_D3D12XS_MAX_TRACKED_CBVS;
    g_tracked_cbvs[slot].rm = rm;
    g_tracked_cbvs[slot].offset = offset;
    g_tracked_cbvs[slot].size = size;
    g_tracked_cbvs[slot].last_hash = 0u;
    g_tracked_cbvs[slot].in_use = 1u;
    __atomic_store_n(&g_ul_queue_lock, 0u, __ATOMIC_RELEASE);
}

static void d3d12xs_flush_cpu_uploads(d3d12xs_cmd_queue_meta_t *qm) {
    if (qm == NULL || qm->real_obj == NULL || qm->owner_device == NULL ||
        qm->owner_device->real_device == NULL) {
        return;
    }
    static uint32_t g_ul_exec_lock = 0u;
    static d3d12xs_pending_upload_t g_ul_local[SCARLETT_D3D12XS_MAX_PENDING_UPLOADS];
    while (__atomic_exchange_n(&g_ul_exec_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    uint32_t count = 0u;
    while (__atomic_exchange_n(&g_ul_queue_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    for (uint32_t i = 0u; i < SCARLETT_D3D12XS_MAX_TRACKED_CBVS; ++i) {
        d3d12xs_tracked_cbv_t *cb = &g_tracked_cbvs[i];
        if (cb->in_use == 0u || cb->rm == NULL || cb->rm->in_use == 0u ||
            cb->rm->real_obj == NULL) {
            cb->in_use = 0u;
            continue;
        }
        uint64_t va = cb->rm->gpu_va + cb->offset;
        if (!d3d12xs_pe_is_readable((const void *)(uintptr_t)va, cb->size)) {
            continue;
        }
        if (__atomic_load_n(&cb->rm->ever_uploaded, __ATOMIC_ACQUIRE) == 0u &&
            d3d12xs_is_all_zero((const void *)(uintptr_t)va, cb->size)) {
            continue;
        }
        uint64_t h = d3d12xs_fnv1a_64((const void *)(uintptr_t)va, cb->size);
        if (h != cb->last_hash) {
            cb->last_hash = h;
            bool dup = false;
            for (uint32_t j = 0u; j < g_pending_upload_count; ++j) {
                if (g_pending_uploads[j].rm == cb->rm &&
                    g_pending_uploads[j].offset == cb->offset &&
                    g_pending_uploads[j].size >= cb->size) {
                    dup = true;
                    break;
                }
            }
            if (!dup && g_pending_upload_count < SCARLETT_D3D12XS_MAX_PENDING_UPLOADS) {
                uint32_t idx = g_pending_upload_count++;
                g_pending_uploads[idx].rm = cb->rm;
                g_pending_uploads[idx].offset = cb->offset;
                g_pending_uploads[idx].size = cb->size;
            }
        }
    }
    count = g_pending_upload_count;
    if (count > 0u) {
        d3d12xs_pe_memcpy(g_ul_local, g_pending_uploads, (size_t)count * sizeof(g_ul_local[0]));
        g_pending_upload_count = 0u;
    }
    __atomic_store_n(&g_ul_queue_lock, 0u, __ATOMIC_RELEASE);

    if (count == 0u) {
        __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
        return;
    }

#if defined(_WIN32) || defined(__MINGW32__)
    d3d12xs_pe_resolve_kernel32();
    scarlett_create_event_a_fn_t fn_cea = __atomic_load_n(&g_pfn_CreateEventA, __ATOMIC_ACQUIRE);
    scarlett_wait_single_fn_t fn_wfso = __atomic_load_n(&g_pfn_WaitForSingleObject, __ATOMIC_ACQUIRE);
    if (fn_cea == NULL || fn_wfso == NULL) {
        __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
        return;
    }
#endif

    static void *g_ul_dev = NULL;
    static void *g_ul_alloc = NULL;
    static void *g_ul_list = NULL;
    static void *g_ul_fence = NULL;
    static void *g_ul_event = NULL;
    static void *g_ul_buf = NULL;
    static uint64_t g_ul_cap = 0u;
    static uint64_t g_ul_fence_val = 0u;
    void *real_dev = qm->owner_device->real_device;
    void **dev_vtbl = *(void ***)real_dev;
    if (g_ul_dev != real_dev) {
        typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
        if (g_ul_buf != NULL) { ((pfn_rel_t)(*(void ***)g_ul_buf)[2])(g_ul_buf); g_ul_buf = NULL; }
        if (g_ul_list != NULL) { ((pfn_rel_t)(*(void ***)g_ul_list)[2])(g_ul_list); g_ul_list = NULL; }
        if (g_ul_alloc != NULL) { ((pfn_rel_t)(*(void ***)g_ul_alloc)[2])(g_ul_alloc); g_ul_alloc = NULL; }
        if (g_ul_fence != NULL) { ((pfn_rel_t)(*(void ***)g_ul_fence)[2])(g_ul_fence); g_ul_fence = NULL; }
        g_ul_cap = 0u;
        g_ul_fence_val = 0u;
        g_ul_dev = real_dev;

        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_cca_t)(
            void *, uint32_t, const scarlett_guid_t *, void **);
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_ccl_t)(
            void *, uint32_t, uint32_t, void *, void *, const scarlett_guid_t *, void **);
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_cf_t)(
            void *, uint64_t, uint32_t, const scarlett_guid_t *, void **);
        if (dev_vtbl[9] != NULL && dev_vtbl[12] != NULL && dev_vtbl[36] != NULL &&
            scarlett_succeeded(((pfn_cca_t)dev_vtbl[9])(real_dev, 0u, &kIidID3D12CommandAllocator, &g_ul_alloc)) &&
            g_ul_alloc != NULL) {
            if (scarlett_succeeded(((pfn_ccl_t)dev_vtbl[12])(
                    real_dev, 0u, 0u, g_ul_alloc, NULL, &kIidID3D12GraphicsCommandList_Std, &g_ul_list)) &&
                g_ul_list != NULL) {
                typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_close_t)(void *);
                ((pfn_close_t)(*(void ***)g_ul_list)[9])(g_ul_list);
            }
            (void)((pfn_cf_t)dev_vtbl[36])(real_dev, 0u, 0u, &kIidID3D12Fence, &g_ul_fence);
        }
        if (g_ul_event == NULL) {
#if defined(_WIN32) || defined(__MINGW32__)
            g_ul_event = fn_cea(NULL, 0, 0, NULL);
#else
            g_ul_event = (void *)(uintptr_t)1u;
#endif
        }
    }

    uint64_t total_needed = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        total_needed += (g_ul_local[i].size + 255u) & ~UINT64_C(255);
    }
    if (g_ul_alloc == NULL || g_ul_list == NULL || g_ul_fence == NULL || g_ul_event == NULL ||
        total_needed == 0u) {
        __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
        return;
    }

    if (g_ul_buf == NULL || g_ul_cap < total_needed) {
        if (g_ul_buf != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            ((pfn_rel_t)(*(void ***)g_ul_buf)[2])(g_ul_buf);
            g_ul_buf = NULL;
            g_ul_cap = 0u;
        }
        uint64_t req_cap = (total_needed < (UINT64_C(4) << 20))
            ? (UINT64_C(4) << 20)
            : ((total_needed + 65535u) & ~UINT64_C(65535));
        scarlett_d3d12_resource_desc_t ul_desc;
        d3d12xs_pe_zero(&ul_desc, sizeof(ul_desc));
        ul_desc.Dimension = 1u;
        ul_desc.Width = req_cap;
        ul_desc.Height = 1u;
        ul_desc.DepthOrArraySize = 1u;
        ul_desc.MipLevels = 1u;
        ul_desc.SampleCount = 1u;
        ul_desc.Layout = 1u;
        struct {
            uint32_t Type;
            uint32_t CPUPageProperty;
            uint32_t MemoryPoolPreference;
            uint32_t CreationNodeMask;
            uint32_t VisibleNodeMask;
        } hp;
        hp.Type = 2u; /* D3D12_HEAP_TYPE_UPLOAD */
        hp.CPUPageProperty = 0u;
        hp.MemoryPoolPreference = 0u;
        hp.CreationNodeMask = 1u;
        hp.VisibleNodeMask = 1u;
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_ccr_t)(
            void *, const void *, uint32_t, const void *, uint32_t, const void *,
            const scarlett_guid_t *, void **);
        scarlett_hresult_t rhr = ((pfn_ccr_t)dev_vtbl[27])(
            real_dev, &hp, 0u, &ul_desc, 0xAC3u /* D3D12_RESOURCE_STATE_GENERIC_READ */,
            NULL, &kIidID3D12Resource, &g_ul_buf);
        if (scarlett_succeeded(rhr) && g_ul_buf != NULL) {
            g_ul_cap = req_cap;
        } else {
            g_ul_buf = NULL;
        }
    }
    if (g_ul_buf == NULL) {
        __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
        return;
    }

    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_ares_t)(void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_lres_t)(void *, void *, void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_lclose_t)(void *);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_cbr_t)(void *, void *, uint64_t, void *, uint64_t, uint64_t);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_ecl_t)(void *, uint32_t, void * const *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_qsig_t)(void *, void *, uint64_t);
    typedef uint64_t (SCARLETT_D3D12XS_CALL *pfn_fgcv_t)(void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_fseoc_t)(void *, uint64_t, void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_map_t)(void *, uint32_t, const void *, void **);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_unmap_t)(void *, uint32_t, const void *);

    void **avtbl = *(void ***)g_ul_alloc;
    void **lvtbl = *(void ***)g_ul_list;
    void **qvtbl = *(void ***)qm->real_obj;
    void **fvtbl = *(void ***)g_ul_fence;
    void **bvtbl = *(void ***)g_ul_buf;

    struct { uint64_t Begin; uint64_t End; } empty_range = { 0u, 0u };
    void *mapped = NULL;
    if (scarlett_failed(((pfn_map_t)bvtbl[8])(g_ul_buf, 0u, &empty_range, &mapped)) ||
        mapped == NULL) {
        __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
        return;
    }

    if (scarlett_failed(((pfn_ares_t)avtbl[8])(g_ul_alloc)) ||
        scarlett_failed(((pfn_lres_t)lvtbl[10])(g_ul_list, g_ul_alloc, NULL))) {
        ((pfn_unmap_t)bvtbl[9])(g_ul_buf, 0u, &empty_range);
        __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
        return;
    }

    uint64_t cursor = 0u;
    uint32_t copied_slices = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        d3d12xs_res_meta_t *rm = g_ul_local[i].rm;
        uint64_t off = g_ul_local[i].offset;
        uint64_t sz = g_ul_local[i].size;
        if (rm == NULL || rm->in_use == 0u || rm->real_obj == NULL ||
            off + sz > rm->alloc_size || cursor + sz > g_ul_cap) {
            continue;
        }
        uint64_t src_va = rm->gpu_va + off;
        if (!d3d12xs_pe_is_readable((const void *)(uintptr_t)src_va, (size_t)sz)) {
            continue;
        }
        d3d12xs_pe_memcpy((uint8_t *)mapped + cursor, (const void *)(uintptr_t)src_va, (size_t)sz);
        ((pfn_cbr_t)lvtbl[15])(g_ul_list, rm->real_obj, off, g_ul_buf, cursor, sz);
        __atomic_store_n(&rm->ever_uploaded, 1u, __ATOMIC_RELEASE);
        cursor += (sz + 255u) & ~UINT64_C(255);
        ++copied_slices;
    }
    struct { uint64_t Begin; uint64_t End; } written_range = { 0u, cursor };
    ((pfn_unmap_t)bvtbl[9])(g_ul_buf, 0u, &written_range);

    if (copied_slices > 0u && scarlett_succeeded(((pfn_lclose_t)lvtbl[9])(g_ul_list))) {
        void *lists[1] = { g_ul_list };
        ((pfn_ecl_t)qvtbl[10])(qm->real_obj, 1u, lists);
        uint64_t next_val = ++g_ul_fence_val;
        if (qvtbl[14] != NULL &&
            scarlett_succeeded(((pfn_qsig_t)qvtbl[14])(qm->real_obj, g_ul_fence, next_val))) {
            if (fvtbl[8] != NULL && ((pfn_fgcv_t)fvtbl[8])(g_ul_fence) < next_val) {
                if (fvtbl[9] != NULL &&
                    scarlett_succeeded(((pfn_fseoc_t)fvtbl[9])(g_ul_fence, next_val, g_ul_event))) {
#if defined(_WIN32) || defined(__MINGW32__)
                    (void)fn_wfso(g_ul_event, 2000u);
#endif
                }
            }
        }
        static uint32_t g_ul_log_seq = 0u;
        uint32_t uls = __atomic_add_fetch(&g_ul_log_seq, 1u, __ATOMIC_RELAXED);
        if (uls <= 8u || (uls % 250u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CpuToGpuUpload", 15u,
                (uint64_t)uls, (uint64_t)copied_slices, cursor,
                (uint64_t)(uintptr_t)g_ul_local[0].rm->real_obj, g_ul_local[0].offset,
                SCARLETT_OK, "(uploaded)");
        }
    }
    __atomic_store_n(&g_ul_exec_lock, 0u, __ATOMIC_RELEASE);
}

static bool d3d12xs_resource_get_info(
    const void *ptr,
    uint64_t *out_va,
    scarlett_d3d12_resource_desc_t *out_desc) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(ptr);
    if (rm == NULL) {
        return false;
    }
    if (out_va != NULL) {
        *out_va = rm->gpu_va;
    }
    if (out_desc != NULL) {
        d3d12xs_pe_memcpy(out_desc, &rm->desc, sizeof(*out_desc));
    }
    return true;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL res_query_interface(
    ID3D12ResourceX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(self);
    if (rm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12Resource) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&rm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    d3d12xs_pe_log_guid_ods(
        "ID3D12ResourceX::QueryInterface",
        0u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        riid,
        SCARLETT_E_NOINTERFACE);
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL res_add_ref(ID3D12ResourceX *self) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(self);
    if (rm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&rm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL res_release(ID3D12ResourceX *self) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(self);
    if (rm == NULL || __atomic_load_n(&rm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&rm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        static uint32_t g_res_free_seq = 0u;
        uint32_t free_seq = __atomic_add_fetch(&g_res_free_seq, 1u, __ATOMIC_RELAXED);
        if (free_seq <= 16u || rm->owns_va != 0u ||
            rm->desc.Dimension == 3u ||
            rm->desc.Width >= (UINT64_C(4) << 20) ||
            (free_seq % 250u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "ID3D12ResourceX::Release",
                2u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self,
                rm->gpu_va,
                (uint64_t)rm->desc.Dimension,
                rm->desc.Width,
                SCARLETT_OK,
                (rm->owns_va != 0u) ? "(freed-owns-va)" : "(freed)");
        }
        if (rm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)rm->real_obj;
            ((pfn_rel_t)vtbl[2])(rm->real_obj);
            rm->real_obj = NULL;
        }
        if (rm->unix_resource_handle != 0u) {
            (void)D3D12XboxDestroyResourceX(rm->unix_device_handle, rm->unix_resource_handle);
        }
#if defined(_WIN32) || defined(__MINGW32__)
        if (rm->owns_va != 0u && rm->gpu_va != 0u) {
            d3d12xs_pe_free_mem((void *)(uintptr_t)rm->gpu_va);
        }
#endif
        rm->unix_device_handle = 0u;
        rm->unix_resource_handle = 0u;
        rm->gpu_va = 0u;
        rm->real_gpu_va = 0u;
        rm->alloc_size = 0u;
        rm->gpu_dirty = 0u;
        rm->owns_va = 0u;
        rm->owner_device = NULL;
        __atomic_store_n(&rm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL res_map(
    ID3D12ResourceX *self,
    uint32_t Subresource,
    const void *pReadRange,
    void **ppData) {
    (void)Subresource;
    (void)pReadRange;
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(self);
    if (rm == NULL) {
        return SCARLETT_E_POINTER;
    }
#if defined(_WIN32) || defined(__MINGW32__)
    if (rm->gpu_va == 0u) {
        size_t sz = (rm->desc.Dimension == 1u && rm->desc.Width > 0u)
            ? (size_t)rm->desc.Width
            : 65536u;
        size_t alloc_sz = (sz < 65536u) ? 65536u : ((sz + 65535u) & ~(size_t)65535u);
        void *p = d3d12xs_pe_alloc_zero(alloc_sz);
        if (p != NULL) {
            rm->gpu_va = (uint64_t)(uintptr_t)p;
            rm->alloc_size = alloc_sz;
            rm->owns_va = 1u;
        }
    }
#endif
    if (ppData != NULL) {
        *ppData = (void *)(uintptr_t)rm->gpu_va;
    }
    {
        static uint32_t g_map_seq = 0u;
        uint32_t mseq = __atomic_add_fetch(&g_map_seq, 1u, __ATOMIC_RELAXED);
        if (mseq <= 8u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "ID3D12ResourceX::Map", 8u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self, rm->gpu_va, rm->alloc_size,
                ((uint64_t)rm->desc.Dimension << 32u) | (uint32_t)rm->desc.Width,
                SCARLETT_OK, "(map)");
        }
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL res_unmap(
    ID3D12ResourceX *self,
    uint32_t Subresource,
    const void *pWrittenRange) {
    (void)Subresource;
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(self);
    if (rm == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (rm->desc.Dimension == 1u && rm->real_obj != NULL &&
        __atomic_load_n(&rm->gpu_dirty, __ATOMIC_ACQUIRE) == 0u) {
        uint64_t off = 0u;
        uint64_t sz = rm->alloc_size;
        if (pWrittenRange != NULL && d3d12xs_pe_is_readable(pWrittenRange, 16u)) {
            const uint64_t *rng = (const uint64_t *)pWrittenRange;
            if (rng[1] > rng[0] && rng[1] <= rm->alloc_size) {
                off = rng[0];
                sz = rng[1] - rng[0];
            }
        }
        d3d12xs_enqueue_cpu_upload_meta(rm, off, sz);
    }
    return SCARLETT_OK;
}

static void * SCARLETT_D3D12XS_CALL res_get_desc(void *arg1, void *arg2) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(arg1);
    scarlett_d3d12_resource_desc_t *out_desc = NULL;
    if (rm != NULL) {
        out_desc = (scarlett_d3d12_resource_desc_t *)arg2;
    } else {
        rm = d3d12xs_get_res_meta(arg2);
        out_desc = (scarlett_d3d12_resource_desc_t *)arg1;
    }
    if (out_desc != NULL && rm != NULL) {
        d3d12xs_pe_memcpy(out_desc, &rm->desc, sizeof(*out_desc));
    }
    return (void *)out_desc;
}

static uint64_t SCARLETT_D3D12XS_CALL res_get_gpu_va(ID3D12ResourceX *self) {
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(self);
    return (rm != NULL) ? rm->gpu_va : 0u;
}

D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 15, "15")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 16, "16")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 17, "17")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 18, "18")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 19, "19")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 20, "20")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 21, "21")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 22, "22")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 23, "23")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 24, "24")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 25, "25")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 26, "26")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 27, "27")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 28, "28")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 29, "29")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 30, "30")
D3D12XS_DEFINE_SLOT_STUB(res, "ID3D12ResourceX", 31, "31")

static const ID3D12ResourceXVtbl g_res_vtbl = {
    {
        (void *)res_query_interface,
        (void *)res_add_ref,
        (void *)res_release,
        (void *)res_vtbl_slot_stub_3,
        (void *)res_vtbl_slot_stub_4,
        (void *)res_vtbl_slot_stub_5,
        (void *)res_vtbl_slot_stub_6,
        (void *)res_vtbl_slot_stub_7,
        (void *)res_map,        /* slot 8 (+0x40) */
        (void *)res_unmap,      /* slot 9 (+0x48) */
        (void *)res_get_desc,   /* slot 10 (+0x50) */
        (void *)res_get_gpu_va, /* slot 11 (+0x58) */
        (void *)res_vtbl_slot_stub_12,
        (void *)res_vtbl_slot_stub_13,
        (void *)res_vtbl_slot_stub_14,
        (void *)res_vtbl_slot_stub_15,
        (void *)res_vtbl_slot_stub_16,
        (void *)res_vtbl_slot_stub_17,
        (void *)res_vtbl_slot_stub_18,
        (void *)res_vtbl_slot_stub_19,
        (void *)res_vtbl_slot_stub_20,
        (void *)res_vtbl_slot_stub_21,
        (void *)res_vtbl_slot_stub_22,
        (void *)res_vtbl_slot_stub_23,
        (void *)res_vtbl_slot_stub_24,
        (void *)res_vtbl_slot_stub_25,
        (void *)res_vtbl_slot_stub_26,
        (void *)res_vtbl_slot_stub_27,
        (void *)res_vtbl_slot_stub_28,
        (void *)res_vtbl_slot_stub_29,
        (void *)res_vtbl_slot_stub_30,
        (void *)res_vtbl_slot_stub_31
    }
};

/* Slot 9 (+0x48): ID3D12Device::CreateCommandAllocator */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot9_create_command_allocator(
    ID3D12DeviceX *self,
    uint32_t type,
    const scarlett_guid_t *riid,
    void **ppCommandAllocator) {
    if (self == NULL || ppCommandAllocator == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppCommandAllocator = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12CommandAllocator) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateCommandAllocator",
            9u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    ID3D12CommandAllocatorX *a = NULL;
    d3d12xs_cmd_alloc_meta_t *am = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_ALLOCATORS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_cmd_alloc_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            a = &g_com_cmd_allocators[i];
            am = &g_cmd_alloc_meta[i];
            break;
        }
    }
    if (a == NULL || am == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateCommandAllocator(OOM)",
            9u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)type,
            0u,
            0u,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    d3d12xs_pe_zero(a, sizeof(*a));
    a->lpVtbl = &g_cmd_alloc_vtbl;
    am->unix_device_handle = self->unix_device_handle;
    am->queue_type = type;
    am->real_obj = NULL;
    __atomic_store_n(&am->ref_count, 1u, __ATOMIC_RELEASE);
    if (self->real_device != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cca_t)(
            void *, uint32_t, const scarlett_guid_t *, void **);
        void **vtbl = *(void ***)self->real_device;
        pfn_real_cca_t fn = (pfn_real_cca_t)vtbl[9];
        uint32_t real_type = (type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) ? 2u : type;
        void *real_a = NULL;
        scarlett_hresult_t rhr = fn(
            self->real_device, real_type, &kIidID3D12CommandAllocator, &real_a);
        if (scarlett_succeeded(rhr) && real_a != NULL) {
            am->real_obj = real_a;
        }
        static uint32_t g_vkd3d_a_logged = 0u;
        if (__atomic_exchange_n(&g_vkd3d_a_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
            d3d12xs_pe_vkd3d_log("CreateCommandAllocator", am->real_obj, rhr);
        }
    }
    *ppCommandAllocator = (void *)a;
    {
        static uint32_t g_cca_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_cca_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 8u || (seq % 100u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "CreateCommandAllocator",
                9u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self,
                (uint64_t)type,
                (uint64_t)(uintptr_t)riid,
                (uint64_t)(uintptr_t)a,
                SCARLETT_OK,
                "(created)");
        }
    }
    return SCARLETT_OK;
}

/* Slot 7 (+0x38): ID3D12Device::GetNodeCount (PE64) */
#if defined(_WIN32) || defined(__MINGW32__)
static uint32_t SCARLETT_D3D12XS_CALL com_slot7_get_node_count(ID3D12DeviceX *self) {
    (void)self;
    return 1u;
}
#endif

/* Slot 12 (+0x60): ID3D12Device::CreateCommandList */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot12_create_command_list(
    ID3D12DeviceX *self,
    uint32_t nodeMask,
    uint32_t type,
    void *pAllocator,
    void *pInitialState,
    const scarlett_guid_t *riid,
    void **ppCommandList) {
    (void)nodeMask;
    (void)pAllocator;
    (void)pInitialState;
    if (self == NULL || ppCommandList == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppCommandList = NULL;
    if (riid != NULL && !d3d12xs_is_cmd_list_iid(riid)) {
        d3d12xs_pe_log_guid_ods(
            "CreateCommandList",
            12u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    if (!d3d12xs_ensure_cmd_list_pool()) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    ID3D12GraphicsCommandListX *cl = NULL;
    d3d12xs_cmd_list_meta_t *clm = NULL;
    size_t cl_idx = 0u;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_CMD_LISTS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_cmd_list_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            cl = &g_com_cmd_lists[i];
            clm = &g_cmd_list_meta[i];
            cl_idx = i;
            break;
        }
    }
    if (cl == NULL || clm == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateCommandList(OOM)",
            12u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)nodeMask,
            (uint64_t)type,
            0u,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    uint32_t mapped_type = SCARLETT_D3D12_QUEUE_TYPE_DIRECT;
    if (type == 2u || type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
        mapped_type = SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS;
    } else if (type == 3u) {
        mapped_type = SCARLETT_D3D12_QUEUE_TYPE_DMA;
    }
    uint64_t cmd_list_handle = 0u;
    scarlett_hresult_t hr = D3D12XboxCreateCommandListX(
        self->unix_device_handle,
        mapped_type,
        0u,
        &cmd_list_handle);
    if (scarlett_failed(hr)) {
        __atomic_store_n(&clm->in_use, 0u, __ATOMIC_RELEASE);
        return hr;
    }
    d3d12xs_pe_zero(cl, sizeof(*cl));
    cl->lpVtbl = &g_cmd_list_vtbl;
    clm->unix_device_handle = self->unix_device_handle;
    clm->unix_cmd_list_handle = cmd_list_handle;
    clm->list_type = type;
    clm->label_write_count = 0u;
    clm->bound_rtv_count = 0u;
    clm->has_gpu_write_cmds = 0u;
    clm->current_topology = 0u;
    clm->bound_srv_heap = NULL;
    clm->bound_sampler_heap = NULL;
    clm->bound_gfx_rs = NULL;
    clm->bound_comp_rs = NULL;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));
    clm->real_obj = NULL;
    d3d12xs_pm4_assign_slice(cl, clm, cl_idx);
    if (self->real_device != NULL) {
        d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(pAllocator);
        void *real_alloc = (am != NULL) ? am->real_obj : NULL;
        if (real_alloc != NULL) {
            typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccl_t)(
                void *, uint32_t, uint32_t, void *, void *,
                const scarlett_guid_t *, void **);
            void **vtbl = *(void ***)self->real_device;
            pfn_real_ccl_t fn = (pfn_real_ccl_t)vtbl[12];
            uint32_t real_type = (type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) ? 2u : type;
            void *real_cl = NULL;
            scarlett_hresult_t rhr = fn(
                self->real_device, nodeMask, real_type, real_alloc, NULL,
                &kIidID3D12GraphicsCommandList_Std, &real_cl);
            if (scarlett_succeeded(rhr) && real_cl != NULL) {
                clm->real_obj = real_cl;
            }
            static uint32_t g_vkd3d_cl_logged = 0u;
            if (__atomic_exchange_n(&g_vkd3d_cl_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
                d3d12xs_pe_vkd3d_log("CreateCommandList", clm->real_obj, rhr);
            }
        }
    }
    __atomic_store_n(&clm->ref_count, 1u, __ATOMIC_RELEASE);
    *ppCommandList = (void *)cl;
    {
        static uint32_t g_ccl12_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_ccl12_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 8u || (seq % 100u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "CreateCommandList",
                12u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self,
                (uint64_t)nodeMask,
                (uint64_t)type,
                (uint64_t)(uintptr_t)cl,
                SCARLETT_OK,
                "(created)");
        }
    }
    return SCARLETT_OK;
}

/* Slot 14 (+0x70): ID3D12Device::CreateDescriptorHeap */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot14_create_descriptor_heap(
    ID3D12DeviceX *self,
    const scarlett_d3d12_desc_heap_desc_t *pDescriptorHeapDesc,
    const scarlett_guid_t *riid,
    void **ppvHeap) {
    if (self == NULL || pDescriptorHeapDesc == NULL || ppvHeap == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvHeap = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DescriptorHeap) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateDescriptorHeap",
            14u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    size_t idx = SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS;
    ID3D12DescriptorHeapX *h = NULL;
    d3d12xs_desc_heap_meta_t *hm = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_DESC_HEAPS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_desc_heap_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            idx = i;
            h = &g_com_desc_heaps[i];
            hm = &g_desc_heap_meta[i];
            break;
        }
    }
    if (h == NULL || hm == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    scarlett_d3d12xs_create_desc_heap_params_t hp;
    scarlett_hresult_t hr = D3D12XboxCreateDescriptorHeapX(
        self->unix_device_handle,
        pDescriptorHeapDesc->Type,
        pDescriptorHeapDesc->NumDescriptors,
        pDescriptorHeapDesc->Flags,
        &hp);
    if (scarlett_failed(hr)) {
        __atomic_store_n(&hm->in_use, 0u, __ATOMIC_RELEASE);
        return hr;
    }
    size_t raw_bytes = (size_t)pDescriptorHeapDesc->NumDescriptors * 32u;
    size_t alloc_size = (raw_bytes < 65536u) ? 65536u : ((raw_bytes + 65535u) & ~(size_t)65535u);
    void *backing = NULL;
    (void)idx;
#if defined(_WIN32) || defined(__MINGW32__)
    backing = d3d12xs_pe_alloc_zero(alloc_size);
    if (backing == NULL) {
        __atomic_store_n(&hm->in_use, 0u, __ATOMIC_RELEASE);
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateDescriptorHeap(VirtualAlloc_fail)",
            14u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)pDescriptorHeapDesc->Type,
            (uint64_t)pDescriptorHeapDesc->NumDescriptors,
            (uint64_t)alloc_size,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
#else
    (void)alloc_size;
    backing = (void *)(uintptr_t)(UINT64_C(0x10000000) + ((uint64_t)idx * UINT64_C(0x04000000)));
#endif

    d3d12xs_pe_zero(h, sizeof(*h));
    h->lpVtbl = &g_desc_heap_vtbl;
    hm->unix_device_handle = self->unix_device_handle;
    hm->unix_heap_handle = hp.out_heap_handle;
    d3d12xs_pe_memcpy(&hm->desc, pDescriptorHeapDesc, sizeof(hm->desc));
    hm->cpu_start_addr = (uint64_t)(uintptr_t)backing;
    hm->gpu_start_addr = (uint64_t)(uintptr_t)backing;
    hm->xbox_inc_size = 32u;
    hm->real_inc_size = 32u;
    hm->real_num_descriptors = 0u;
    hm->real_cpu_start = 0u;
    hm->real_gpu_start = 0u;
    hm->null_desc_cpu = 0u;
    hm->staging_start = 0u;
    hm->staging_cursor = 0u;
    hm->real_obj = NULL;
    hm->owner_real_device = self->real_device;
    if (self->real_device != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cdh_t)(
            void *, const scarlett_d3d12_desc_heap_desc_t *, const scarlett_guid_t *, void **);
        typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_real_gdhis_t)(void *, uint32_t);
        /* Win64 COM struct-return ABI (MSVC C++ member fn): hidden out-ptr in rdx (2nd arg) */
        typedef uint64_t *(SCARLETT_D3D12XS_CALL *pfn_real_gstart_t)(void *, uint64_t *);
        void **dev_vtbl = *(void ***)self->real_device;
        scarlett_d3d12_desc_heap_desc_t real_desc;
        d3d12xs_pe_memcpy(&real_desc, pDescriptorHeapDesc, sizeof(real_desc));
        real_desc.Flags &= 0x1u;
        real_desc.NodeMask = 0u;
        if (real_desc.Type == 1u && (real_desc.Flags & 0x1u) != 0u &&
            real_desc.NumDescriptors < 2048u) {
            real_desc.NumDescriptors = 2048u;
        }
        void *real_dh = NULL;
        scarlett_hresult_t rhr = ((pfn_real_cdh_t)dev_vtbl[14])(
            self->real_device, &real_desc, &kIidID3D12DescriptorHeap, &real_dh);
        uint32_t clamped = 0u;
        if (scarlett_failed(rhr) && (real_desc.Flags & 0x1u) != 0u) {
            uint32_t max_cap = (real_desc.Type == 1u) ? 2048u : 1000000u;
            if (real_desc.NumDescriptors > max_cap) {
                real_desc.NumDescriptors = max_cap;
                clamped = 1u;
                rhr = ((pfn_real_cdh_t)dev_vtbl[14])(
                    self->real_device, &real_desc, &kIidID3D12DescriptorHeap, &real_dh);
            }
        }
        if (scarlett_succeeded(rhr) && real_dh != NULL) {
            hm->real_obj = real_dh;
            hm->real_num_descriptors = real_desc.NumDescriptors;
            hm->real_inc_size = ((pfn_real_gdhis_t)dev_vtbl[15])(
                self->real_device, real_desc.Type);
            if (hm->real_inc_size == 0u) {
                hm->real_inc_size = 32u;
            }
            void **dh_vtbl = *(void ***)real_dh;
            uint64_t real_cpu_start = 0u;
            uint64_t real_gpu_start = 0u;
            ((pfn_real_gstart_t)dh_vtbl[9])(real_dh, &real_cpu_start);
            ((pfn_real_gstart_t)dh_vtbl[10])(real_dh, &real_gpu_start);
            hm->real_cpu_start = real_cpu_start;
            hm->real_gpu_start = real_gpu_start;
            if ((real_desc.Flags & 0x1u) != 0u && real_gpu_start != 0u && real_cpu_start != 0u) {
                if (real_desc.Type == 0u && hm->real_num_descriptors >= 16384u) {
                    hm->staging_start = hm->real_num_descriptors / 2u;
                } else if (hm->real_num_descriptors > pDescriptorHeapDesc->NumDescriptors + 16u) {
                    hm->staging_start = pDescriptorHeapDesc->NumDescriptors;
                }
                hm->staging_cursor = hm->staging_start;
            }
        }
        static uint32_t g_vkd3d_dh_logged = 0u;
        static uint32_t g_vkd3d_dh_warn_logged = 0u;
        bool should_log = false;
        const char *dh_note = "(ok)";
        if (hm->real_cpu_start == 0u || clamped != 0u) {
            should_log = (__atomic_fetch_add(&g_vkd3d_dh_warn_logged, 1u, __ATOMIC_RELAXED) < 8u);
            dh_note = (hm->real_cpu_start == 0u) ? "(zero_cpu_start)" : "(clamped)";
        } else {
            should_log = (__atomic_fetch_add(&g_vkd3d_dh_logged, 1u, __ATOMIC_RELAXED) < 8u);
        }
        if (should_log) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                clamped ? "CreateDescriptorHeap(vkd3d_clamped)" : "CreateDescriptorHeap(vkd3d)",
                14u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                hm->real_cpu_start,
                hm->real_gpu_start,
                ((uint64_t)hm->real_inc_size << 32u) | (uint64_t)hm->real_num_descriptors,
                (uint64_t)pDescriptorHeapDesc->NumDescriptors,
                rhr,
                dh_note);
        }
    }
    __atomic_store_n(&hm->ref_count, 1u, __ATOMIC_RELEASE);
    *ppvHeap = (void *)h;
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "CreateDescriptorHeap",
        14u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)pDescriptorHeapDesc->Type,
        (uint64_t)pDescriptorHeapDesc->NumDescriptors,
        (uint64_t)alloc_size,
        (uint64_t)(uintptr_t)backing,
        SCARLETT_OK,
        "(created)");
    return SCARLETT_OK;
}

/* Slots 17..22: CreateConstantBufferView, CreateSRV, CreateUAV, CreateRTV, CreateDSV, CreateSampler */
static void SCARLETT_D3D12XS_CALL com_slot17_create_cbv(
    ID3D12DeviceX *self, const void *pDesc, uint64_t DestDescriptor) {
    if (self == NULL || self->real_device == NULL || pDesc == NULL) {
        return;
    }
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, NULL);
    if (real_dest == 0u) {
        static uint32_t g_cbv_bad_logged = 0u;
        if (__atomic_fetch_add(&g_cbv_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateConstantBufferView", 17u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return;
    }
    typedef struct {
        uint64_t BufferLocation;
        uint32_t SizeInBytes;
        uint32_t _pad;
    } cbv_desc_t;
    cbv_desc_t pc_desc;
    d3d12xs_pe_memcpy(&pc_desc, pDesc, sizeof(pc_desc));
    if (pc_desc.SizeInBytes > 0u) {
        pc_desc.SizeInBytes = (pc_desc.SizeInBytes + 255u) & ~255u;
    }
    uint64_t orig_va = pc_desc.BufferLocation;
    uint64_t real_va = d3d12xs_translate_gpu_va(orig_va, NULL);
    if (real_va != 0u) {
        pc_desc.BufferLocation = real_va;
        d3d12xs_track_cbv(orig_va, pc_desc.SizeInBytes);
    }
    static uint32_t g_cbv_ok_logged = 0u;
    if (__atomic_fetch_add(&g_cbv_ok_logged, 1u, __ATOMIC_RELAXED) < 8u) {
        const uint32_t *dw = d3d12xs_pe_is_readable((const void *)(uintptr_t)orig_va, 16u)
            ? (const uint32_t *)(uintptr_t)orig_va : NULL;
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CreateConstantBufferView", 17u,
            orig_va,
            pc_desc.BufferLocation, (uint64_t)pc_desc.SizeInBytes,
            (dw != NULL) ? (((uint64_t)dw[0] << 32) | dw[1]) : 0u,
            (dw != NULL) ? (((uint64_t)dw[2] << 32) | dw[3]) : 0u,
            SCARLETT_OK, (real_va != 0u) ? "(forwarded)" : "(untranslated_va)");
    }
    d3d12xs_stamp_desc_cookie(DestDescriptor);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_cbv_t)(void *, const void *, uint64_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_cbv_t)vtbl[17])(self->real_device, &pc_desc, real_dest);
}

static void SCARLETT_D3D12XS_CALL com_slot18_create_srv(
    ID3D12DeviceX *self, void *pResource, const void *pDesc, uint64_t DestDescriptor) {
    if (self == NULL || self->real_device == NULL) {
        return;
    }
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, NULL);
    if (real_dest == 0u) {
        static uint32_t g_srv_bad_logged = 0u;
        if (__atomic_fetch_add(&g_srv_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateShaderResourceView", 18u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return;
    }
    void *real_res = (pResource != NULL) ? d3d12xs_resource_get_real(pResource) : NULL;
    if (pResource != NULL && real_res == NULL) {
        static uint32_t g_srv_nores_logged = 0u;
        if (__atomic_fetch_add(&g_srv_nores_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateShaderResourceView", 18u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, real_dest, SCARLETT_E_INVALIDARG, "(no_real_res)");
        }
        return;
    }
    static uint32_t g_srv_ok_logged = 0u;
    if (__atomic_fetch_add(&g_srv_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CreateShaderResourceView", 18u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)real_res,
            DestDescriptor, real_dest, SCARLETT_OK, "(forwarded)");
    }
    d3d12xs_record_desc_resource(DestDescriptor, pResource);
    d3d12xs_stamp_desc_cookie(DestDescriptor);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_srv_t)(void *, void *, const void *, uint64_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_srv_t)vtbl[18])(self->real_device, real_res, pDesc, real_dest);
}

static void SCARLETT_D3D12XS_CALL com_slot19_create_uav(
    ID3D12DeviceX *self, void *pResource, void *pCounterResource, const void *pDesc, uint64_t DestDescriptor) {
    if (self == NULL || self->real_device == NULL) {
        return;
    }
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, NULL);
    if (real_dest == 0u) {
        static uint32_t g_uav_bad_logged = 0u;
        if (__atomic_fetch_add(&g_uav_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateUnorderedAccessView", 19u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pCounterResource,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return;
    }
    void *real_res = (pResource != NULL) ? d3d12xs_resource_get_real(pResource) : NULL;
    void *real_ctr = (pCounterResource != NULL) ? d3d12xs_resource_get_real(pCounterResource) : NULL;
    if (pResource != NULL && real_res == NULL) {
        static uint32_t g_uav_nores_logged = 0u;
        if (__atomic_fetch_add(&g_uav_nores_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateUnorderedAccessView", 19u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pCounterResource,
                DestDescriptor, real_dest, SCARLETT_E_INVALIDARG, "(no_real_res)");
        }
        return;
    }
    static uint32_t g_uav_ok_logged = 0u;
    if (__atomic_fetch_add(&g_uav_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CreateUnorderedAccessView", 19u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)real_res,
            DestDescriptor, real_dest, SCARLETT_OK, "(forwarded)");
    }
    d3d12xs_record_desc_resource(DestDescriptor, pResource);
    d3d12xs_stamp_desc_cookie(DestDescriptor);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_uav_t)(void *, void *, void *, const void *, uint64_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_uav_t)vtbl[19])(self->real_device, real_res, real_ctr, pDesc, real_dest);
}

static void SCARLETT_D3D12XS_CALL com_slot20_create_rtv(
    ID3D12DeviceX *self, void *pResource, const void *pDesc, uint64_t DestDescriptor) {
    if (self == NULL || self->real_device == NULL) {
        return;
    }
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, NULL);
    if (real_dest == 0u) {
        static uint32_t g_rtv_bad_logged = 0u;
        if (__atomic_fetch_add(&g_rtv_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateRenderTargetView", 20u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return;
    }
    void *real_res = (pResource != NULL) ? d3d12xs_resource_get_real(pResource) : NULL;
    if (pResource != NULL && real_res == NULL) {
        static uint32_t g_rtv_nores_logged = 0u;
        if (__atomic_fetch_add(&g_rtv_nores_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateRenderTargetView", 20u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, real_dest, SCARLETT_E_INVALIDARG, "(no_real_res)");
        }
        return;
    }
    static uint32_t g_rtv_ok_logged = 0u;
    if (__atomic_fetch_add(&g_rtv_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CreateRenderTargetView", 20u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)real_res,
            DestDescriptor, real_dest, SCARLETT_OK, "(forwarded)");
    }
    d3d12xs_record_desc_resource(DestDescriptor, pResource);
    d3d12xs_stamp_desc_cookie(DestDescriptor);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_rtv_t)(void *, void *, const void *, uint64_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_rtv_t)vtbl[20])(self->real_device, real_res, pDesc, real_dest);
}

static void SCARLETT_D3D12XS_CALL com_slot21_create_dsv(
    ID3D12DeviceX *self, void *pResource, const void *pDesc, uint64_t DestDescriptor) {
    if (self == NULL || self->real_device == NULL) {
        return;
    }
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, NULL);
    if (real_dest == 0u) {
        static uint32_t g_dsv_bad_logged = 0u;
        if (__atomic_fetch_add(&g_dsv_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateDepthStencilView", 21u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return;
    }
    void *real_res = (pResource != NULL) ? d3d12xs_resource_get_real(pResource) : NULL;
    if (pResource != NULL && real_res == NULL) {
        static uint32_t g_dsv_nores_logged = 0u;
        if (__atomic_fetch_add(&g_dsv_nores_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateDepthStencilView", 21u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, real_dest, SCARLETT_E_INVALIDARG, "(no_real_res)");
        }
        return;
    }
    static uint32_t g_dsv_ok_logged = 0u;
    if (__atomic_fetch_add(&g_dsv_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CreateDepthStencilView", 21u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)pResource, (uint64_t)(uintptr_t)real_res,
            DestDescriptor, real_dest, SCARLETT_OK, "(forwarded)");
    }
    d3d12xs_record_desc_resource(DestDescriptor, pResource);
    d3d12xs_stamp_desc_cookie(DestDescriptor);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_dsv_t)(void *, void *, const void *, uint64_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_dsv_t)vtbl[21])(self->real_device, real_res, pDesc, real_dest);
}

static void SCARLETT_D3D12XS_CALL com_slot22_create_sampler(
    ID3D12DeviceX *self, const void *pDesc, uint64_t DestDescriptor) {
    if (self == NULL || self->real_device == NULL || pDesc == NULL) {
        return;
    }
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, NULL);
    if (real_dest == 0u) {
        static uint32_t g_samp_bad_logged = 0u;
        if (__atomic_fetch_add(&g_samp_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CreateSampler", 22u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return;
    }
    static uint32_t g_samp_ok_logged = 0u;
    if (__atomic_fetch_add(&g_samp_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CreateSampler", 22u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pDesc,
            DestDescriptor, real_dest, SCARLETT_OK, "(forwarded)");
    }
    d3d12xs_stamp_desc_cookie(DestDescriptor);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_samp_t)(void *, const void *, uint64_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_samp_t)vtbl[22])(self->real_device, pDesc, real_dest);
}

/* Slot 23 (+0xb8): ID3D12Device::CopyDescriptors */
static void SCARLETT_D3D12XS_CALL com_slot23_copy_descriptors(
    ID3D12DeviceX *self,
    uint32_t NumDestDescriptorRanges,
    const uint64_t *pDestDescriptorRangeStarts,
    const uint32_t *pDestDescriptorRangeSizes,
    uint32_t NumSrcDescriptorRanges,
    const uint64_t *pSrcDescriptorRangeStarts,
    const uint32_t *pSrcDescriptorRangeSizes,
    uint32_t DescriptorHeapsType) {
    if (self == NULL || self->real_device == NULL ||
        NumDestDescriptorRanges == 0u || NumSrcDescriptorRanges == 0u ||
        pDestDescriptorRangeStarts == NULL || pSrcDescriptorRangeStarts == NULL) {
        return;
    }
    if (NumDestDescriptorRanges > 128u || NumSrcDescriptorRanges > 128u) {
        static uint32_t g_cd_cap_logged = 0u;
        if (__atomic_fetch_add(&g_cd_cap_logged, 1u, __ATOMIC_RELAXED) < 4u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "CopyDescriptors", 23u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)NumDestDescriptorRanges, (uint64_t)NumSrcDescriptorRanges,
                (uint64_t)DescriptorHeapsType, 0u, SCARLETT_E_INVALIDARG, "(range_cap_exceeded)");
        }
        return;
    }
    uint64_t real_dsts[128];
    uint64_t real_srcs[128];
    for (uint32_t i = 0u; i < NumDestDescriptorRanges; ++i) {
        uint32_t cnt = (pDestDescriptorRangeSizes != NULL) ? pDestDescriptorRangeSizes[i] : 1u;
        real_dsts[i] = d3d12xs_translate_cpu_desc_handle_range(
            pDestDescriptorRangeStarts[i], cnt, NULL);
        if (real_dsts[i] == 0u) {
            static uint32_t g_cd_bad_dst_logged = 0u;
            if (__atomic_fetch_add(&g_cd_bad_dst_logged, 1u, __ATOMIC_RELAXED) < 4u) {
                d3d12xs_pe_log_stub_ods(
                    "d3d12_xs.dll", "CopyDescriptors", 23u,
                    (uint64_t)(uintptr_t)__builtin_return_address(0),
                    pDestDescriptorRangeStarts[i], (uint64_t)cnt,
                    (uint64_t)i, (uint64_t)DescriptorHeapsType,
                    SCARLETT_E_INVALIDARG, "(bad_dst_desc)");
            }
            return;
        }
    }
    for (uint32_t i = 0u; i < NumSrcDescriptorRanges; ++i) {
        uint32_t cnt = (pSrcDescriptorRangeSizes != NULL) ? pSrcDescriptorRangeSizes[i] : 1u;
        real_srcs[i] = d3d12xs_translate_cpu_desc_handle_range(
            pSrcDescriptorRangeStarts[i], cnt, NULL);
        if (real_srcs[i] == 0u) {
            static uint32_t g_cd_bad_src_logged = 0u;
            if (__atomic_fetch_add(&g_cd_bad_src_logged, 1u, __ATOMIC_RELAXED) < 4u) {
                d3d12xs_pe_log_stub_ods(
                    "d3d12_xs.dll", "CopyDescriptors", 23u,
                    (uint64_t)(uintptr_t)__builtin_return_address(0),
                    pSrcDescriptorRangeStarts[i], (uint64_t)cnt,
                    (uint64_t)i, (uint64_t)DescriptorHeapsType,
                    SCARLETT_E_INVALIDARG, "(bad_src_desc)");
            }
            return;
        }
    }
    static uint32_t g_cd_ok_logged = 0u;
    if (__atomic_fetch_add(&g_cd_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll", "CopyDescriptors", 23u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)NumDestDescriptorRanges, (uint64_t)NumSrcDescriptorRanges,
            real_dsts[0], real_srcs[0], SCARLETT_OK, "(forwarded)");
    }
    if (NumDestDescriptorRanges == NumSrcDescriptorRanges) {
        for (uint32_t i = 0u; i < NumDestDescriptorRanges; ++i) {
            uint32_t dc = (pDestDescriptorRangeSizes != NULL) ? pDestDescriptorRangeSizes[i] : 1u;
            uint32_t sc = (pSrcDescriptorRangeSizes != NULL) ? pSrcDescriptorRangeSizes[i] : 1u;
            if (dc == sc && dc > 0u) {
                d3d12xs_copy_desc_resource_range(
                    pDestDescriptorRangeStarts[i], pSrcDescriptorRangeStarts[i], dc);
                size_t copy_bytes = (size_t)dc * 32u;
                if (d3d12xs_pe_is_readable((const void *)(uintptr_t)pSrcDescriptorRangeStarts[i], copy_bytes) &&
                    d3d12xs_pe_is_readable((const void *)(uintptr_t)pDestDescriptorRangeStarts[i], copy_bytes) &&
                    d3d12xs_pe_is_writable_va(pDestDescriptorRangeStarts[i], copy_bytes)) {
                    d3d12xs_pe_memcpy(
                        (void *)(uintptr_t)pDestDescriptorRangeStarts[i],
                        (const void *)(uintptr_t)pSrcDescriptorRangeStarts[i],
                        copy_bytes);
                }
            }
        }
    }
    typedef void (SCARLETT_D3D12XS_CALL *pfn_real_cd_t)(
        void *, uint32_t, const uint64_t *, const uint32_t *,
        uint32_t, const uint64_t *, const uint32_t *, uint32_t);
    void **vtbl = *(void ***)self->real_device;
    ((pfn_real_cd_t)vtbl[23])(
        self->real_device,
        NumDestDescriptorRanges, real_dsts, pDestDescriptorRangeSizes,
        NumSrcDescriptorRanges, real_srcs, pSrcDescriptorRangeSizes,
        DescriptorHeapsType);
}

/* Slot 24 (+0xc0): ID3D12Device::CopyDescriptorsSimple */
static void SCARLETT_D3D12XS_CALL com_slot24_copy_descriptors_simple(
    ID3D12DeviceX *self,
    uint32_t NumDescriptors,
    uint64_t DestDescriptorRangeStart,
    uint64_t SrcDescriptorRangeStart,
    uint32_t DescriptorHeapsType) {
    if (DestDescriptorRangeStart != 0u && SrcDescriptorRangeStart != 0u && NumDescriptors != 0u) {
        size_t bytes = (size_t)NumDescriptors * 32u;
        if (d3d12xs_pe_is_readable((const void *)(uintptr_t)SrcDescriptorRangeStart, bytes) &&
            d3d12xs_pe_is_readable((const void *)(uintptr_t)DestDescriptorRangeStart, bytes) &&
            d3d12xs_pe_is_writable_va(DestDescriptorRangeStart, bytes)) {
            volatile uint8_t *dst = (volatile uint8_t *)(uintptr_t)DestDescriptorRangeStart;
            const volatile uint8_t *src = (const volatile uint8_t *)(uintptr_t)SrcDescriptorRangeStart;
            for (size_t i = 0u; i < bytes; ++i) {
                dst[i] = src[i];
            }
        }
        if (self != NULL && self->real_device != NULL) {
            uint64_t rdst = d3d12xs_translate_cpu_desc_handle_range(
                DestDescriptorRangeStart, NumDescriptors, NULL);
            uint64_t rsrc = d3d12xs_translate_cpu_desc_handle_range(
                SrcDescriptorRangeStart, NumDescriptors, NULL);
            if (rdst != 0u && rsrc != 0u) {
                static uint32_t g_cds_ok_logged = 0u;
                if (__atomic_fetch_add(&g_cds_ok_logged, 1u, __ATOMIC_RELAXED) < 4u) {
                    d3d12xs_pe_log_stub_ods(
                        "d3d12_xs.dll", "CopyDescriptorsSimple", 24u,
                        (uint64_t)(uintptr_t)__builtin_return_address(0),
                        (uint64_t)NumDescriptors, rdst, rsrc,
                        (uint64_t)DescriptorHeapsType, SCARLETT_OK, "(forwarded)");
                }
                d3d12xs_copy_desc_resource_range(
                    DestDescriptorRangeStart, SrcDescriptorRangeStart, NumDescriptors);
                typedef void (SCARLETT_D3D12XS_CALL *pfn_real_cds_t)(
                    void *, uint32_t, uint64_t, uint64_t, uint32_t);
                void **vtbl = *(void ***)self->real_device;
                ((pfn_real_cds_t)vtbl[24])(
                    self->real_device, NumDescriptors, rdst, rsrc, DescriptorHeapsType);
            } else {
                static uint32_t g_cds_bad_logged = 0u;
                if (__atomic_fetch_add(&g_cds_bad_logged, 1u, __ATOMIC_RELAXED) < 4u) {
                    d3d12xs_pe_log_stub_ods(
                        "d3d12_xs.dll", "CopyDescriptorsSimple", 24u,
                        (uint64_t)(uintptr_t)__builtin_return_address(0),
                        (uint64_t)NumDescriptors, DestDescriptorRangeStart,
                        SrcDescriptorRangeStart, (uint64_t)DescriptorHeapsType,
                        SCARLETT_E_INVALIDARG, "(bad_desc_range)");
                }
            }
        }
    }
}

/* Slot 15 (+0x78): ID3D12Device::GetDescriptorHandleIncrementSize */
static uint32_t SCARLETT_D3D12XS_CALL com_slot15_get_descriptor_handle_increment_size(
    ID3D12DeviceX *self,
    uint32_t DescriptorHeapType) {
    (void)self;
    (void)DescriptorHeapType;
    return 32u;
}

/* Slot 37 (+0x128): ID3D12Device::GetDeviceRemovedReason */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot37_get_device_removed_reason(
    ID3D12DeviceX *self) {
    (void)self;
    return SCARLETT_OK;
}

/* Slot 43 (+0x158): ID3D12Device::GetAdapterLuid (MSVC x64 struct-return ABI) */
static void * SCARLETT_D3D12XS_CALL com_slot43_get_adapter_luid(void *arg1, void *arg2) {
    uint64_t *out_luid = d3d12xs_is_com_device(arg1)
        ? (uint64_t *)arg2
        : (uint64_t *)arg1;
    if (out_luid != NULL) {
        *out_luid = UINT64_C(0x0000000000010001);
    }
    return (void *)out_luid;
}

/* Slot 36 (+0x120): ID3D12Device::CreateFence */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot36_create_fence(
    ID3D12DeviceX *self,
    uint64_t InitialValue,
    uint32_t Flags,
    const scarlett_guid_t *riid,
    void **ppFence) {
    if (self == NULL || ppFence == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppFence = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Fence) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateFence",
            36u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    ID3D12FenceX *f = NULL;
    d3d12xs_fence_meta_t *fm = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_FENCES; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_fence_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            f = &g_com_fences[i];
            fm = &g_fence_meta[i];
            break;
        }
    }
    if (f == NULL || fm == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    d3d12xs_pe_zero(f, sizeof(*f));
    d3d12xs_pe_zero(fm->waiters, sizeof(fm->waiters));
    f->lpVtbl = &g_fence_vtbl;
    fm->unix_device_handle = self->unix_device_handle;
    fm->current_value = InitialValue;
    fm->real_obj = NULL;
    __atomic_store_n(&fm->ref_count, 1u, __ATOMIC_RELEASE);
    if (self->real_device != NULL) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cf_t)(
            void *, uint64_t, uint32_t, const scarlett_guid_t *, void **);
        void **vtbl = *(void ***)self->real_device;
        pfn_real_cf_t fn = (pfn_real_cf_t)vtbl[36];
        void *real_f = NULL;
        scarlett_hresult_t rhr = fn(
            self->real_device, InitialValue, Flags & 0x3u, &kIidID3D12Fence, &real_f);
        if (scarlett_succeeded(rhr) && real_f != NULL) {
            fm->real_obj = real_f;
        }
        static uint32_t g_vkd3d_f_logged = 0u;
        if (__atomic_exchange_n(&g_vkd3d_f_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
            d3d12xs_pe_vkd3d_log("CreateFence", fm->real_obj, rhr);
        }
    }
    *ppFence = (void *)f;
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "CreateFence",
        36u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        InitialValue,
        (uint64_t)Flags,
        (uint64_t)(uintptr_t)f,
        SCARLETT_OK,
        "(created)");
    return SCARLETT_OK;
}

/* Slot 73 (+0x248): Xbox ID3D12DeviceX::CreateCommandList (6 args) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot73_create_command_list(
    ID3D12DeviceX *self,
    const void *pDesc,
    void *pAllocator,
    void *pInitialState,
    const scarlett_guid_t *riid,
    void **ppCommandList) {
    (void)pAllocator;
    (void)pInitialState;
    if (self == NULL || ppCommandList == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppCommandList = NULL;
    if (riid != NULL && !d3d12xs_is_cmd_list_iid(riid)) {
        d3d12xs_pe_log_guid_ods(
            "CreateCommandList",
            12u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    if (!d3d12xs_ensure_cmd_list_pool()) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    ID3D12GraphicsCommandListX *cl = NULL;
    d3d12xs_cmd_list_meta_t *clm = NULL;
    size_t cl_idx = 0u;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_CMD_LISTS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_cmd_list_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            cl = &g_com_cmd_lists[i];
            clm = &g_cmd_list_meta[i];
            cl_idx = i;
            break;
        }
    }
    if (cl == NULL || clm == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateCommandListX(OOM)",
            73u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)(uintptr_t)pDesc,
            (uint64_t)(uintptr_t)pAllocator,
            0u,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    uint64_t cmd_list_handle = 0u;
    scarlett_hresult_t hr = D3D12XboxCreateCommandListX(
        self->unix_device_handle,
        SCARLETT_D3D12_QUEUE_TYPE_DIRECT,
        0u,
        &cmd_list_handle);
    if (scarlett_failed(hr)) {
        __atomic_store_n(&clm->in_use, 0u, __ATOMIC_RELEASE);
        return hr;
    }
    d3d12xs_pe_zero(cl, sizeof(*cl));
    cl->lpVtbl = &g_cmd_list_vtbl;
    clm->unix_device_handle = self->unix_device_handle;
    clm->unix_cmd_list_handle = cmd_list_handle;
    clm->list_type = 0u;
    clm->label_write_count = 0u;
    clm->bound_rtv_count = 0u;
    clm->has_gpu_write_cmds = 0u;
    clm->current_topology = 0u;
    clm->bound_srv_heap = NULL;
    clm->bound_sampler_heap = NULL;
    clm->bound_gfx_rs = NULL;
    clm->bound_comp_rs = NULL;
    d3d12xs_pe_zero(clm->bound_rtvs, sizeof(clm->bound_rtvs));
    clm->real_obj = NULL;
    d3d12xs_pm4_assign_slice(cl, clm, cl_idx);
    if (self->real_device != NULL) {
        d3d12xs_cmd_alloc_meta_t *am = d3d12xs_get_cmd_alloc_meta(pAllocator);
        void *real_alloc = (am != NULL) ? am->real_obj : NULL;
        if (real_alloc != NULL) {
            typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccl_t)(
                void *, uint32_t, uint32_t, void *, void *,
                const scarlett_guid_t *, void **);
            void **vtbl = *(void ***)self->real_device;
            pfn_real_ccl_t fn = (pfn_real_ccl_t)vtbl[12];
            uint32_t desc_type = (pDesc != NULL) ? *(const uint32_t *)pDesc : 0u;
            uint32_t real_type = (desc_type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) ? 2u : desc_type;
            void *real_cl = NULL;
            scarlett_hresult_t rhr = fn(
                self->real_device, 0u, real_type, real_alloc, NULL,
                &kIidID3D12GraphicsCommandList_Std, &real_cl);
            if (scarlett_succeeded(rhr) && real_cl != NULL) {
                clm->real_obj = real_cl;
            }
            static uint32_t g_vkd3d_clx_logged = 0u;
            if (__atomic_exchange_n(&g_vkd3d_clx_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
                d3d12xs_pe_vkd3d_log("CreateCommandListX", clm->real_obj, rhr);
            }
        }
    }
    __atomic_store_n(&clm->ref_count, 1u, __ATOMIC_RELEASE);
    *ppCommandList = (void *)cl;
    {
        static uint32_t g_ccl73_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_ccl73_seq, 1u, __ATOMIC_RELAXED);
        if (seq <= 8u || (seq % 100u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "CreateCommandList",
                73u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self,
                (uint64_t)(uintptr_t)pDesc,
                (uint64_t)(uintptr_t)pAllocator,
                (uint64_t)(uintptr_t)cl,
                SCARLETT_OK,
                "(created)");
        }
    }
    return SCARLETT_OK;
}

/* Slot 78 (+0x270): Xbox ID3D12DeviceX::SetFrameIntervalX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot78_set_frame_interval_x(
    ID3D12DeviceX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    uint32_t period_us = (uint32_t)r8;
    scarlett_hresult_t hr = D3D12XboxSetFrameIntervalX(
        self->unix_device_handle,
        period_us != 0u ? period_us : 16667u,
        0u,
        0u,
        (uint32_t)r9);
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "SetFrameIntervalX",
        78u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        rdx,
        r8,
        r9,
        scarlett_succeeded(hr) ? hr : SCARLETT_OK,
        "(ok)");
    return SCARLETT_OK;
}

/* Slot 79 (+0x278): Xbox ID3D12DeviceX::ScheduleFrameEventX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot79_set_frame_interval(
    ID3D12DeviceX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    (void)r8;
    (void)r9;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "SetFrameIntervalX",
        79u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        rdx,
        r8,
        r9,
        SCARLETT_OK,
        "(ok)");
    return SCARLETT_OK;
}

/* ============================================================================
 * Modified DXBC MD5 & Xbox Root Signature Flag Stripping
 * ============================================================================ */

static void d3d12xs_md5_block(uint32_t state[4], const uint32_t m[16]) {
    static const uint32_t k[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
        0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
        0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
        0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
        0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
        0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
        0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
        0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
        0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
    };
    static const uint32_t s[64] = {
        7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u, 7u, 12u, 17u, 22u,
        5u,  9u, 14u, 20u, 5u,  9u, 14u, 20u, 5u,  9u, 14u, 20u, 5u,  9u, 14u, 20u,
        4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u, 4u, 11u, 16u, 23u,
        6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u, 6u, 10u, 15u, 21u
    };
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (uint32_t i = 0u; i < 64u; ++i) {
        uint32_t f, g;
        if (i < 16u) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32u) {
            f = (d & b) | ((~d) & c);
            g = (5u * i + 1u) & 15u;
        } else if (i < 48u) {
            f = b ^ c ^ d;
            g = (3u * i + 5u) & 15u;
        } else {
            f = c ^ (b | (~d));
            g = (7u * i) & 15u;
        }
        uint32_t tmp = d;
        d = c;
        c = b;
        uint32_t x = a + f + k[i] + m[g];
        b = b + ((x << s[i]) | (x >> (32u - s[i])));
        a = tmp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void d3d12xs_dxbc_recompute_md5(uint8_t *blob, size_t size) {
    if (blob == NULL || size < 20u) {
        return;
    }
    uint32_t state[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    const uint8_t *data = blob + 20u;
    size_t n = size - 20u;
    uint64_t bits = (uint64_t)n * 8u;
    size_t pos = 0u;
    uint32_t m[16];

    while (pos + 64u <= n) {
        for (size_t i = 0u; i < 16u; ++i) {
            const uint8_t *p = data + pos + i * 4u;
            m[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
        d3d12xs_md5_block(state, m);
        pos += 64u;
    }

    uint8_t tail[64];
    d3d12xs_pe_zero(tail, sizeof(tail));
    size_t rem = n - pos;
    for (size_t i = 0u; i < rem; ++i) {
        tail[i] = data[pos + i];
    }
    tail[rem] = 0x80u;
    if (rem >= 56u) {
        for (size_t i = 0u; i < 16u; ++i) {
            const uint8_t *p = tail + i * 4u;
            m[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
        d3d12xs_md5_block(state, m);
        d3d12xs_pe_zero(m, sizeof(m));
    } else {
        for (size_t i = 0u; i < 16u; ++i) {
            const uint8_t *p = tail + i * 4u;
            m[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }
    }
    /* Custom DXBC MD5 final block tail */
    m[0] = (uint32_t)bits;
    m[15] = (uint32_t)((bits >> 2) | 1u);
    d3d12xs_md5_block(state, m);

    for (size_t i = 0u; i < 4u; ++i) {
        blob[4u + i * 4u + 0u] = (uint8_t)(state[i] & 0xFFu);
        blob[4u + i * 4u + 1u] = (uint8_t)((state[i] >> 8) & 0xFFu);
        blob[4u + i * 4u + 2u] = (uint8_t)((state[i] >> 16) & 0xFFu);
        blob[4u + i * 4u + 3u] = (uint8_t)((state[i] >> 24) & 0xFFu);
    }
}

static bool d3d12xs_dxbc_strip_rts0_high_flags(uint8_t *blob, size_t size) {
    if (blob == NULL || size < 36u) {
        return false;
    }
    if (blob[0] != 'D' || blob[1] != 'X' || blob[2] != 'B' || blob[3] != 'C') {
        return false;
    }
    uint32_t chunk_count = *(const uint32_t *)(blob + 28u);
    if (chunk_count > 64u || 32u + (size_t)chunk_count * 4u > size) {
        return false;
    }
    bool stripped = false;
    for (uint32_t i = 0u; i < chunk_count; ++i) {
        uint32_t off = *(const uint32_t *)(blob + 32u + (size_t)i * 4u);
        if ((size_t)off + 32u <= size &&
            blob[off + 0u] == 'R' && blob[off + 1u] == 'T' &&
            blob[off + 2u] == 'S' && blob[off + 3u] == '0') {
            uint32_t *flags_ptr = (uint32_t *)(blob + off + 8u + 20u);
            if ((*flags_ptr & 0xFF000000u) != 0u) {
                *flags_ptr &= ~0xFF000000u;
                stripped = true;
            }
        }
    }
    if (stripped) {
        d3d12xs_dxbc_recompute_md5(blob, size);
    }
    return stripped;
}

/* ============================================================================
 * ID3D12RootSignatureX & ID3D12PipelineStateX Static COM Pools
 * ============================================================================ */

typedef struct ID3D12RootSignatureXVtbl_S {
    void *slots[16];
} ID3D12RootSignatureXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12RootSignatureX_S {
    const ID3D12RootSignatureXVtbl *lpVtbl;
    uint8_t inline_scratchpad[128u - sizeof(void *)];
} ID3D12RootSignatureX;

#define SCARLETT_D3D12XS_MAX_RS_PARAMS 64u
typedef struct d3d12xs_rs_param_info_s {
    uint8_t param_type;
    uint8_t heap_type;
    uint8_t is_unbounded;
    uint8_t pad;
    uint32_t num_descriptors;
} d3d12xs_rs_param_info_t;

typedef struct d3d12xs_root_sig_meta_s {
    uint64_t unix_device_handle;
    uint32_t ref_count;
    uint32_t in_use;
    uint32_t num_params;
    uint32_t reserved_pad;
    void *real_obj;
    d3d12xs_rs_param_info_t params[SCARLETT_D3D12XS_MAX_RS_PARAMS];
} d3d12xs_root_sig_meta_t;

#define SCARLETT_D3D12XS_MAX_COM_ROOT_SIGS 4096u
static ID3D12RootSignatureX g_com_root_sigs[SCARLETT_D3D12XS_MAX_COM_ROOT_SIGS];
static d3d12xs_root_sig_meta_t g_root_sig_meta[SCARLETT_D3D12XS_MAX_COM_ROOT_SIGS];

static inline d3d12xs_root_sig_meta_t *d3d12xs_get_root_sig_meta(const void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)&g_com_root_sigs[0];
    uintptr_t end = (uintptr_t)&g_com_root_sigs[SCARLETT_D3D12XS_MAX_COM_ROOT_SIGS];
    if (p < base || p >= end || ((p - base) % sizeof(ID3D12RootSignatureX)) != 0u) {
        return NULL;
    }
    size_t idx = (size_t)((const ID3D12RootSignatureX *)ptr - &g_com_root_sigs[0]);
    return &g_root_sig_meta[idx];
}

static inline bool d3d12xs_is_com_root_sig(const void *ptr) {
    return d3d12xs_get_root_sig_meta(ptr) != NULL;
}

static void *d3d12xs_root_sig_get_real(const void *ptr) {
    d3d12xs_root_sig_meta_t *rm = d3d12xs_get_root_sig_meta(ptr);
    return (rm != NULL && rm->in_use != 0u) ? rm->real_obj : NULL;
}

static bool d3d12xs_root_sig_get_table_info(
    const void *rs_ptr,
    uint32_t param_idx,
    uint32_t *out_heap_type,
    uint32_t *out_count,
    bool *out_unbounded) {
    d3d12xs_root_sig_meta_t *rm = d3d12xs_get_root_sig_meta(rs_ptr);
    if (rm == NULL || rm->in_use == 0u || param_idx >= rm->num_params) {
        return false;
    }
    const d3d12xs_rs_param_info_t *pi = &rm->params[param_idx];
    if (pi->param_type != 0u) {
        return false;
    }
    if (out_heap_type != NULL) *out_heap_type = pi->heap_type;
    if (out_count != NULL) *out_count = pi->num_descriptors;
    if (out_unbounded != NULL) *out_unbounded = (pi->is_unbounded != 0u);
    return true;
}

static void d3d12xs_parse_rts0_tables(
    d3d12xs_root_sig_meta_t *rsm, const uint8_t *blob, size_t size) {
    if (rsm == NULL) {
        return;
    }
    rsm->num_params = 0u;
    d3d12xs_pe_zero(rsm->params, sizeof(rsm->params));
    if (blob == NULL || size < 36u ||
        blob[0] != 'D' || blob[1] != 'X' || blob[2] != 'B' || blob[3] != 'C') {
        return;
    }
    uint32_t chunk_count = *(const uint32_t *)(blob + 28u);
    if (chunk_count == 0u || chunk_count > 64u || 32u + (size_t)chunk_count * 4u > size) {
        return;
    }
    for (uint32_t c = 0u; c < chunk_count; ++c) {
        uint32_t off = *(const uint32_t *)(blob + 32u + (size_t)c * 4u);
        if ((size_t)off + 8u + 24u > size) {
            continue;
        }
        if (blob[off + 0u] != 'R' || blob[off + 1u] != 'T' ||
            blob[off + 2u] != 'S' || blob[off + 3u] != '0') {
            continue;
        }
        uint32_t chunk_size = *(const uint32_t *)(blob + off + 4u);
        const uint8_t *rts = blob + off + 8u;
        size_t rts_len = size - ((size_t)off + 8u);
        if ((size_t)chunk_size < rts_len) {
            rts_len = (size_t)chunk_size;
        }
        if (rts_len < 24u) {
            return;
        }
        uint32_t version = *(const uint32_t *)(rts + 0u);
        uint32_t num_params = *(const uint32_t *)(rts + 4u);
        uint32_t params_off = *(const uint32_t *)(rts + 8u);
        if ((version != 1u && version != 2u) || num_params == 0u ||
            (size_t)params_off > rts_len) {
            return;
        }
        uint32_t limit = (num_params < SCARLETT_D3D12XS_MAX_RS_PARAMS)
            ? num_params : SCARLETT_D3D12XS_MAX_RS_PARAMS;
        rsm->num_params = limit;
        size_t range_stride = (version == 1u) ? 20u : 24u;
        size_t off_in_tbl_field = (version == 1u) ? 16u : 20u;
        for (uint32_t p = 0u; p < limit; ++p) {
            size_t pe_off = (size_t)params_off + (size_t)p * 12u;
            if (pe_off + 12u > rts_len) {
                break;
            }
            uint32_t ptype = *(const uint32_t *)(rts + pe_off + 0u);
            uint32_t pdata = *(const uint32_t *)(rts + pe_off + 8u);
            rsm->params[p].param_type = (uint8_t)(ptype & 0xFFu);
            if (ptype != 0u || (size_t)pdata + 8u > rts_len) {
                continue;
            }
            uint32_t num_ranges = *(const uint32_t *)(rts + pdata + 0u);
            uint32_t ranges_off = *(const uint32_t *)(rts + pdata + 4u);
            uint32_t cursor = 0u;
            uint32_t max_count = 0u;
            uint8_t heap_type = 0u;
            uint8_t is_unbounded = 0u;
            for (uint32_t r = 0u; r < num_ranges && r < 64u; ++r) {
                size_t roff = (size_t)ranges_off + (size_t)r * range_stride;
                if (roff + range_stride > rts_len) {
                    break;
                }
                uint32_t rtype = *(const uint32_t *)(rts + roff + 0u);
                uint32_t rnum = *(const uint32_t *)(rts + roff + 4u);
                uint32_t roffset = *(const uint32_t *)(rts + roff + off_in_tbl_field);
                if (r == 0u && rtype == 3u) {
                    heap_type = 1u;
                }
                uint32_t start = (roffset == 0xFFFFFFFFu) ? cursor : roffset;
                if (rnum == 0xFFFFFFFFu) {
                    is_unbounded = 1u;
                    if (start + 1u > max_count) {
                        max_count = start + 1u;
                    }
                    cursor = start + 1u;
                } else {
                    uint32_t end = start + rnum;
                    if (end > max_count) {
                        max_count = end;
                    }
                    cursor = end;
                }
            }
            rsm->params[p].heap_type = heap_type;
            rsm->params[p].is_unbounded = is_unbounded;
            rsm->params[p].num_descriptors = max_count;
        }
        return;
    }
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL root_sig_query_interface(
    ID3D12RootSignatureX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_root_sig_meta_t *rm = d3d12xs_get_root_sig_meta(self);
    if (rm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12RootSignature) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&rm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    d3d12xs_pe_log_guid_ods(
        "ID3D12RootSignatureX::QueryInterface",
        0u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        riid,
        SCARLETT_E_NOINTERFACE);
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL root_sig_add_ref(ID3D12RootSignatureX *self) {
    d3d12xs_root_sig_meta_t *rm = d3d12xs_get_root_sig_meta(self);
    if (rm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&rm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL root_sig_release(ID3D12RootSignatureX *self) {
    d3d12xs_root_sig_meta_t *rm = d3d12xs_get_root_sig_meta(self);
    if (rm == NULL || __atomic_load_n(&rm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&rm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (rm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)rm->real_obj;
            ((pfn_rel_t)vtbl[2])(rm->real_obj);
            rm->real_obj = NULL;
        }
        rm->unix_device_handle = 0u;
        __atomic_store_n(&rm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 8, "8")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 9, "9")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(root_sig, "ID3D12RootSignatureX", 15, "15")

static const ID3D12RootSignatureXVtbl g_root_sig_vtbl = {
    {
        (void *)root_sig_query_interface,
        (void *)root_sig_add_ref,
        (void *)root_sig_release,
        (void *)root_sig_vtbl_slot_stub_3,
        (void *)root_sig_vtbl_slot_stub_4,
        (void *)root_sig_vtbl_slot_stub_5,
        (void *)root_sig_vtbl_slot_stub_6,
        (void *)root_sig_vtbl_slot_stub_7,
        (void *)root_sig_vtbl_slot_stub_8,
        (void *)root_sig_vtbl_slot_stub_9,
        (void *)root_sig_vtbl_slot_stub_10,
        (void *)root_sig_vtbl_slot_stub_11,
        (void *)root_sig_vtbl_slot_stub_12,
        (void *)root_sig_vtbl_slot_stub_13,
        (void *)root_sig_vtbl_slot_stub_14,
        (void *)root_sig_vtbl_slot_stub_15
    }
};

typedef struct ID3D12PipelineStateXVtbl_S {
    void *slots[16];
} ID3D12PipelineStateXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12PipelineStateX_S {
    const ID3D12PipelineStateXVtbl *lpVtbl;
    uint8_t inline_scratchpad[128u - sizeof(void *)];
} ID3D12PipelineStateX;

typedef struct d3d12xs_pso_meta_s {
    uint64_t unix_device_handle;
    uint32_t ref_count;
    uint32_t in_use;
    uint32_t is_compute;
    uint32_t lifted_spirv_bytes;
    uint32_t saved_owns_dxbc;
    uint32_t reserved_pad;
    void *real_obj;
    void *lifted_spirv_buf;
    void *saved_real_rs;
    void *saved_vs_dxbc;
    uint64_t saved_vs_len;
    void *saved_ps_dxbc;
    uint64_t saved_ps_len;
} d3d12xs_pso_meta_t;

#define SCARLETT_D3D12XS_PSO_CHUNK_SIZE 4096u
#define SCARLETT_D3D12XS_PSO_MAX_CHUNKS 64u
#define SCARLETT_D3D12XS_MAX_COM_PSOS \
    (SCARLETT_D3D12XS_PSO_CHUNK_SIZE * SCARLETT_D3D12XS_PSO_MAX_CHUNKS)

static ID3D12PipelineStateX *g_pso_chunks[SCARLETT_D3D12XS_PSO_MAX_CHUNKS];
static d3d12xs_pso_meta_t *g_pso_meta_chunks[SCARLETT_D3D12XS_PSO_MAX_CHUNKS];
static uint32_t g_pso_chunk_init_state[SCARLETT_D3D12XS_PSO_MAX_CHUNKS];

static bool d3d12xs_ensure_pso_chunk(uint32_t chunk_idx) {
    if (chunk_idx >= SCARLETT_D3D12XS_PSO_MAX_CHUNKS) {
        return false;
    }
    if (__atomic_load_n(&g_pso_chunk_init_state[chunk_idx], __ATOMIC_ACQUIRE) == 2u) {
        return __atomic_load_n(&g_pso_chunks[chunk_idx], __ATOMIC_ACQUIRE) != NULL &&
               __atomic_load_n(&g_pso_meta_chunks[chunk_idx], __ATOMIC_ACQUIRE) != NULL;
    }
    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(
            &g_pso_chunk_init_state[chunk_idx], &expected, 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        ID3D12PipelineStateX *objs = (ID3D12PipelineStateX *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_PSO_CHUNK_SIZE * sizeof(ID3D12PipelineStateX));
        d3d12xs_pso_meta_t *metas = (d3d12xs_pso_meta_t *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_PSO_CHUNK_SIZE * sizeof(d3d12xs_pso_meta_t));
        if (objs != NULL && metas != NULL) {
            __atomic_store_n(&g_pso_chunks[chunk_idx], objs, __ATOMIC_RELEASE);
            __atomic_store_n(&g_pso_meta_chunks[chunk_idx], metas, __ATOMIC_RELEASE);
            __atomic_store_n(&g_pso_chunk_init_state[chunk_idx], 2u, __ATOMIC_RELEASE);
            return true;
        }
        if (objs != NULL) {
            d3d12xs_pe_free_mem(objs);
        }
        if (metas != NULL) {
            d3d12xs_pe_free_mem(metas);
        }
        __atomic_store_n(&g_pso_chunk_init_state[chunk_idx], 0u, __ATOMIC_RELEASE);
        return false;
    }
    while (__atomic_load_n(&g_pso_chunk_init_state[chunk_idx], __ATOMIC_ACQUIRE) == 1u) {
    }
    return __atomic_load_n(&g_pso_chunks[chunk_idx], __ATOMIC_ACQUIRE) != NULL &&
           __atomic_load_n(&g_pso_meta_chunks[chunk_idx], __ATOMIC_ACQUIRE) != NULL;
}

static inline d3d12xs_pso_meta_t *d3d12xs_get_pso_meta(const void *ptr) {
    if (ptr == NULL) {
        return NULL;
    }
    uintptr_t p = (uintptr_t)ptr;
    for (uint32_t c = 0u; c < SCARLETT_D3D12XS_PSO_MAX_CHUNKS; ++c) {
        if (__atomic_load_n(&g_pso_chunk_init_state[c], __ATOMIC_ACQUIRE) != 2u) {
            continue;
        }
        ID3D12PipelineStateX *chunk = __atomic_load_n(&g_pso_chunks[c], __ATOMIC_ACQUIRE);
        d3d12xs_pso_meta_t *metas = __atomic_load_n(&g_pso_meta_chunks[c], __ATOMIC_ACQUIRE);
        if (chunk == NULL || metas == NULL) {
            continue;
        }
        uintptr_t base = (uintptr_t)&chunk[0];
        uintptr_t end = (uintptr_t)&chunk[SCARLETT_D3D12XS_PSO_CHUNK_SIZE];
        if (p >= base && p < end && ((p - base) % sizeof(ID3D12PipelineStateX)) == 0u) {
            size_t idx = (size_t)((p - base) / sizeof(ID3D12PipelineStateX));
            return &metas[idx];
        }
    }
    return NULL;
}

static void *d3d12xs_pso_get_real(const void *ptr) {
    d3d12xs_pso_meta_t *pm = d3d12xs_get_pso_meta(ptr);
    return (pm != NULL && pm->in_use != 0u) ? pm->real_obj : NULL;
}

#if !defined(_WIN32) && !defined(__MINGW32__)
SCARLETT_D3D12XS_API bool scarlett_d3d12xs_test_get_pso_info(
    const void *pso,
    void **out_real_obj,
    uint32_t *out_spirv_bytes,
    uint32_t *out_is_compute) {
    d3d12xs_pso_meta_t *pm = d3d12xs_get_pso_meta(pso);
    if (pm == NULL || __atomic_load_n(&pm->in_use, __ATOMIC_ACQUIRE) == 0u) {
        return false;
    }
    if (out_real_obj != NULL) *out_real_obj = pm->real_obj;
    if (out_spirv_bytes != NULL) *out_spirv_bytes = pm->lifted_spirv_bytes;
    if (out_is_compute != NULL) *out_is_compute = pm->is_compute;
    return true;
}
#endif

static scarlett_hresult_t SCARLETT_D3D12XS_CALL pso_query_interface(
    ID3D12PipelineStateX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    d3d12xs_pso_meta_t *pm = d3d12xs_get_pso_meta(self);
    if (pm == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12PipelineState) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&pm->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    d3d12xs_pe_log_guid_ods(
        "ID3D12PipelineStateX::QueryInterface",
        0u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        riid,
        SCARLETT_E_NOINTERFACE);
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL pso_add_ref(ID3D12PipelineStateX *self) {
    d3d12xs_pso_meta_t *pm = d3d12xs_get_pso_meta(self);
    if (pm == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&pm->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL pso_release(ID3D12PipelineStateX *self) {
    d3d12xs_pso_meta_t *pm = d3d12xs_get_pso_meta(self);
    if (pm == NULL || __atomic_load_n(&pm->ref_count, __ATOMIC_RELAXED) == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&pm->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        if (pm->real_obj != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            void **vtbl = *(void ***)pm->real_obj;
            ((pfn_rel_t)vtbl[2])(pm->real_obj);
            pm->real_obj = NULL;
        }
        if (pm->lifted_spirv_buf != NULL) {
            d3d12xs_pe_free_mem(pm->lifted_spirv_buf);
            pm->lifted_spirv_buf = NULL;
        }
        if (pm->saved_owns_dxbc != 0u) {
            if (pm->saved_vs_dxbc != NULL) {
                d3d12xs_pe_free_mem(pm->saved_vs_dxbc);
            }
            if (pm->saved_ps_dxbc != NULL) {
                d3d12xs_pe_free_mem(pm->saved_ps_dxbc);
            }
        }
        pm->saved_real_rs = NULL;
        pm->saved_vs_dxbc = NULL;
        pm->saved_vs_len = 0u;
        pm->saved_ps_dxbc = NULL;
        pm->saved_ps_len = 0u;
        pm->saved_owns_dxbc = 0u;
        pm->lifted_spirv_bytes = 0u;
        pm->unix_device_handle = 0u;
        __atomic_store_n(&pm->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 8, "8")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 9, "9")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(pso, "ID3D12PipelineStateX", 15, "15")

static const ID3D12PipelineStateXVtbl g_pso_vtbl = {
    {
        (void *)pso_query_interface,
        (void *)pso_add_ref,
        (void *)pso_release,
        (void *)pso_vtbl_slot_stub_3,
        (void *)pso_vtbl_slot_stub_4,
        (void *)pso_vtbl_slot_stub_5,
        (void *)pso_vtbl_slot_stub_6,
        (void *)pso_vtbl_slot_stub_7,
        (void *)pso_vtbl_slot_stub_8,
        (void *)pso_vtbl_slot_stub_9,
        (void *)pso_vtbl_slot_stub_10,
        (void *)pso_vtbl_slot_stub_11,
        (void *)pso_vtbl_slot_stub_12,
        (void *)pso_vtbl_slot_stub_13,
        (void *)pso_vtbl_slot_stub_14,
        (void *)pso_vtbl_slot_stub_15
    }
};

/* ============================================================================
 * ID3D12StateObjectX & ID3D12StateObjectPropertiesX Static COM Pool
 * ============================================================================ */

typedef struct ID3D12StateObjectXVtbl_S {
    void *slots[16];
} ID3D12StateObjectXVtbl;

typedef struct ID3D12StateObjectPropertiesXVtbl_S {
    void *slots[16];
} ID3D12StateObjectPropertiesXVtbl;

typedef struct ID3D12StateObjectX_S ID3D12StateObjectX;

typedef struct ID3D12StateObjectPropertiesX_S {
    const ID3D12StateObjectPropertiesXVtbl *lpVtbl;
    ID3D12StateObjectX *owner;
} ID3D12StateObjectPropertiesX;

struct __attribute__((aligned(16))) ID3D12StateObjectX_S {
    const ID3D12StateObjectXVtbl *lpVtbl;
    ID3D12StateObjectPropertiesX props;
    uint64_t unix_device_handle;
    uint32_t ref_count;
    uint32_t in_use;
    uint8_t shader_id[32];
};

#define SCARLETT_D3D12XS_SO_CHUNK_SIZE 4096u
#define SCARLETT_D3D12XS_SO_MAX_CHUNKS 64u
#define SCARLETT_D3D12XS_MAX_COM_STATE_OBJECTS \
    (SCARLETT_D3D12XS_SO_CHUNK_SIZE * SCARLETT_D3D12XS_SO_MAX_CHUNKS)

static ID3D12StateObjectX *g_state_obj_chunks[SCARLETT_D3D12XS_SO_MAX_CHUNKS];
static uint32_t g_state_obj_chunk_init_state[SCARLETT_D3D12XS_SO_MAX_CHUNKS];

static bool d3d12xs_ensure_state_obj_chunk(uint32_t chunk_idx) {
    if (chunk_idx >= SCARLETT_D3D12XS_SO_MAX_CHUNKS) {
        return false;
    }
    if (__atomic_load_n(&g_state_obj_chunk_init_state[chunk_idx], __ATOMIC_ACQUIRE) == 2u) {
        return __atomic_load_n(&g_state_obj_chunks[chunk_idx], __ATOMIC_ACQUIRE) != NULL;
    }
    uint32_t expected = 0u;
    if (__atomic_compare_exchange_n(
            &g_state_obj_chunk_init_state[chunk_idx], &expected, 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        ID3D12StateObjectX *objs = (ID3D12StateObjectX *)d3d12xs_pe_alloc_zero(
            (size_t)SCARLETT_D3D12XS_SO_CHUNK_SIZE * sizeof(ID3D12StateObjectX));
        if (objs != NULL) {
            __atomic_store_n(&g_state_obj_chunks[chunk_idx], objs, __ATOMIC_RELEASE);
            __atomic_store_n(&g_state_obj_chunk_init_state[chunk_idx], 2u, __ATOMIC_RELEASE);
            return true;
        }
        __atomic_store_n(&g_state_obj_chunk_init_state[chunk_idx], 0u, __ATOMIC_RELEASE);
        return false;
    }
    while (__atomic_load_n(&g_state_obj_chunk_init_state[chunk_idx], __ATOMIC_ACQUIRE) == 1u) {
    }
    return __atomic_load_n(&g_state_obj_chunks[chunk_idx], __ATOMIC_ACQUIRE) != NULL;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL state_obj_query_interface(
    ID3D12StateObjectX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvObject = NULL;
    if (riid == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12StateObject) ||
        d3d12xs_guid_eq(riid, &kIidID3D12StateObject_Std) ||
        d3d12xs_guid_eq(riid, &kIidID3D12PipelineState) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    if (d3d12xs_guid_eq(riid, &kIidID3D12StateObjectProperties)) {
        __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)&self->props;
        return SCARLETT_OK;
    }
    d3d12xs_pe_log_guid_ods(
        "ID3D12StateObjectX::QueryInterface",
        0u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        riid,
        SCARLETT_E_NOINTERFACE);
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL state_obj_add_ref(ID3D12StateObjectX *self) {
    if (self == NULL) {
        return 0u;
    }
    return __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
}

static uint32_t SCARLETT_D3D12XS_CALL state_obj_release(ID3D12StateObjectX *self) {
    if (self == NULL) {
        return 0u;
    }
    uint32_t prev = __atomic_load_n(&self->ref_count, __ATOMIC_RELAXED);
    if (prev == 0u) {
        return 0u;
    }
    uint32_t next = __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) {
        self->unix_device_handle = 0u;
        __atomic_store_n(&self->in_use, 0u, __ATOMIC_RELEASE);
    }
    return next;
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL state_obj_props_query_interface(
    ID3D12StateObjectPropertiesX *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || self->owner == NULL) {
        return SCARLETT_E_POINTER;
    }
    return state_obj_query_interface(self->owner, riid, ppvObject);
}

static uint32_t SCARLETT_D3D12XS_CALL state_obj_props_add_ref(ID3D12StateObjectPropertiesX *self) {
    return (self != NULL && self->owner != NULL) ? state_obj_add_ref(self->owner) : 0u;
}

static uint32_t SCARLETT_D3D12XS_CALL state_obj_props_release(ID3D12StateObjectPropertiesX *self) {
    return (self != NULL && self->owner != NULL) ? state_obj_release(self->owner) : 0u;
}

static void * SCARLETT_D3D12XS_CALL state_obj_props_get_shader_id(
    ID3D12StateObjectPropertiesX *self,
    const void *pExportName) {
    (void)pExportName;
    return (self != NULL && self->owner != NULL) ? (void *)self->owner->shader_id : NULL;
}

static uint64_t SCARLETT_D3D12XS_CALL state_obj_props_get_stack_size(
    ID3D12StateObjectPropertiesX *self,
    const void *pExportName) {
    (void)self;
    (void)pExportName;
    return 4096u;
}

static uint64_t SCARLETT_D3D12XS_CALL state_obj_props_get_pipeline_stack_size(
    ID3D12StateObjectPropertiesX *self) {
    (void)self;
    return 4096u;
}

static void SCARLETT_D3D12XS_CALL state_obj_props_set_stack_size(
    ID3D12StateObjectPropertiesX *self,
    uint64_t PipelineStackSizeInBytes) {
    (void)self;
    (void)PipelineStackSizeInBytes;
}

D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 8, "8")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 9, "9")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(state_obj, "ID3D12StateObjectX", 15, "15")

static const ID3D12StateObjectXVtbl g_state_obj_vtbl = {
    {
        (void *)state_obj_query_interface,
        (void *)state_obj_add_ref,
        (void *)state_obj_release,
        (void *)state_obj_vtbl_slot_stub_3,
        (void *)state_obj_vtbl_slot_stub_4,
        (void *)state_obj_vtbl_slot_stub_5,
        (void *)state_obj_vtbl_slot_stub_6,
        (void *)state_obj_vtbl_slot_stub_7,
        (void *)state_obj_vtbl_slot_stub_8,
        (void *)state_obj_vtbl_slot_stub_9,
        (void *)state_obj_vtbl_slot_stub_10,
        (void *)state_obj_vtbl_slot_stub_11,
        (void *)state_obj_vtbl_slot_stub_12,
        (void *)state_obj_vtbl_slot_stub_13,
        (void *)state_obj_vtbl_slot_stub_14,
        (void *)state_obj_vtbl_slot_stub_15
    }
};

D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 8, "8")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 9, "9")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(state_obj_props, "ID3D12StateObjectPropertiesX", 15, "15")

static const ID3D12StateObjectPropertiesXVtbl g_state_obj_props_vtbl = {
    {
        (void *)state_obj_props_query_interface,
        (void *)state_obj_props_add_ref,
        (void *)state_obj_props_release,
        (void *)state_obj_props_get_shader_id,          /* slot 3 */
        (void *)state_obj_props_get_stack_size,         /* slot 4 */
        (void *)state_obj_props_get_pipeline_stack_size,/* slot 5 */
        (void *)state_obj_props_set_stack_size,         /* slot 6 */
        (void *)state_obj_props_vtbl_slot_stub_7,
        (void *)state_obj_props_vtbl_slot_stub_8,
        (void *)state_obj_props_vtbl_slot_stub_9,
        (void *)state_obj_props_vtbl_slot_stub_10,
        (void *)state_obj_props_vtbl_slot_stub_11,
        (void *)state_obj_props_vtbl_slot_stub_12,
        (void *)state_obj_props_vtbl_slot_stub_13,
        (void *)state_obj_props_vtbl_slot_stub_14,
        (void *)state_obj_props_vtbl_slot_stub_15
    }
};

static scarlett_hresult_t com_create_pso_impl(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppPipelineState,
    uint32_t vtable_slot,
    uint64_t return_address,
    uint64_t extra_info);

/* Slot 57 (+0x1c8): ID3D12Device5::CreateStateObject */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot57_create_state_object(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppStateObject) {
    if (self == NULL || ppStateObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppStateObject = NULL;
    if (riid != NULL && d3d12xs_guid_eq(riid, &kIidID3D12PipelineState)) {
        return com_create_pso_impl(
            self, pDesc, riid, ppStateObject, 57u,
            (uint64_t)(uintptr_t)__builtin_return_address(0), 0u);
    }
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12StateObject) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12StateObject_Std) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateStateObject",
            57u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    static uint32_t g_com_state_objects_cursor = 0u;
    ID3D12StateObjectX *so = NULL;
    size_t idx = 0u;
    uint32_t ticket = __atomic_fetch_add(&g_com_state_objects_cursor, 1u, __ATOMIC_RELAXED);
    if (ticket < SCARLETT_D3D12XS_MAX_COM_STATE_OBJECTS) {
        uint32_t c = ticket / SCARLETT_D3D12XS_SO_CHUNK_SIZE;
        uint32_t off = ticket % SCARLETT_D3D12XS_SO_CHUNK_SIZE;
        if (d3d12xs_ensure_state_obj_chunk(c)) {
            uint32_t expected = 0u;
            if (__atomic_compare_exchange_n(
                    &g_state_obj_chunks[c][off].in_use, &expected, 1u,
                    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                so = &g_state_obj_chunks[c][off];
                idx = ticket;
            }
        }
    }
    if (so == NULL) {
        for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_STATE_OBJECTS; ++i) {
            size_t cand = ((size_t)ticket + i) % SCARLETT_D3D12XS_MAX_COM_STATE_OBJECTS;
            uint32_t c = (uint32_t)(cand / SCARLETT_D3D12XS_SO_CHUNK_SIZE);
            uint32_t off = (uint32_t)(cand % SCARLETT_D3D12XS_SO_CHUNK_SIZE);
            if (!d3d12xs_ensure_state_obj_chunk(c)) {
                break;
            }
            uint32_t expected = 0u;
            if (__atomic_compare_exchange_n(
                    &g_state_obj_chunks[c][off].in_use, &expected, 1u,
                    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                so = &g_state_obj_chunks[c][off];
                idx = cand;
                break;
            }
        }
    }
    if (so == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateStateObject(OOM)",
            57u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)(uintptr_t)pDesc,
            0u,
            0u,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    so->lpVtbl = &g_state_obj_vtbl;
    so->props.lpVtbl = &g_state_obj_props_vtbl;
    so->props.owner = so;
    so->unix_device_handle = self->unix_device_handle;
    for (size_t b = 0u; b < sizeof(so->shader_id); ++b) {
        so->shader_id[b] = (uint8_t)(0xA0u + ((idx + b) & 0x3Fu));
    }
    __atomic_store_n(&so->ref_count, 1u, __ATOMIC_RELEASE);
    *ppStateObject = (void *)so;
    static uint32_t g_state_obj_log_count = 0u;
    uint32_t seq = __atomic_add_fetch(&g_state_obj_log_count, 1u, __ATOMIC_RELAXED);
    if (seq <= 8u || (seq & 1023u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateStateObject",
            57u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)(uintptr_t)pDesc,
            (uint64_t)(uintptr_t)riid,
            (uint64_t)(uintptr_t)so,
            SCARLETT_OK,
            "(created)");
    }
    return SCARLETT_OK;
}

static bool d3d12xs_is_shader_magic(uint32_t magic) {
    return magic == 0x43425844u || /* 'DXBC' */
           magic == 0x00325358u || /* 'XS2\0' */
           magic == 0x42325358u || /* 'XS2B' */
           magic == 0x4D535346u || /* 'FSSM' */
           magic == 0x09005350u || /* 'PS\x00\x09' */
           magic == 0x35444753u;   /* 'SGD5' */
}

static size_t d3d12xs_pe_readable_bytes(const void *ptr, size_t max_bytes) {
    static const size_t kSizes[] = {1024u, 768u, 512u, 384u, 256u, 128u, 64u, 32u, 16u};
    if (ptr == NULL || max_bytes < 16u) {
        return 0u;
    }
    for (size_t i = 0u; i < sizeof(kSizes) / sizeof(kSizes[0]); ++i) {
        size_t s = kSizes[i];
        if (s <= max_bytes && d3d12xs_pe_is_readable(ptr, s)) {
            return s;
        }
    }
    if (d3d12xs_pe_is_readable(ptr, 16u)) {
        return 16u;
    }
    return 0u;
}

static uint64_t d3d12xs_infer_shader_size(const void *src, uint64_t hint_len) {
    if (src == NULL || !d3d12xs_pe_is_readable(src, 32u)) {
        return 0u;
    }
    if (hint_len >= 16u && hint_len <= (UINT64_C(16) << 20) &&
        d3d12xs_pe_is_readable(src, (size_t)hint_len)) {
        return hint_len;
    }
    const uint8_t *b = (const uint8_t *)src;
    uint32_t magic = *(const uint32_t *)b;
    if (magic == 0x43425844u) { /* 'DXBC' */
        uint32_t total = *(const uint32_t *)(b + 24u);
        if (total >= 32u && total <= (16u << 20) && d3d12xs_pe_is_readable(src, total)) {
            return (uint64_t)total;
        }
    } else if ((magic == 0x00325358u || magic == 0x42325358u) &&
               d3d12xs_pe_is_readable(src, 48u)) {
        uint32_t isa_off = *(const uint32_t *)(b + 40u);
        uint32_t isa_bytes = *(const uint32_t *)(b + 44u);
        uint64_t end = (uint64_t)isa_off + (uint64_t)isa_bytes;
        if (isa_off >= 48u && isa_bytes > 0u && end <= (16u << 20) &&
            d3d12xs_pe_is_readable(src, (size_t)end)) {
            return end;
        }
    }
    return 0u;
}

static bool d3d12xs_dxbc_is_clean_legacy(const uint8_t *blob, uint64_t size) {
    if (blob == NULL || size < 32u || size > (16u << 20)) {
        return false;
    }
    if (*(const uint32_t *)blob != 0x43425844u) {
        return false;
    }
    uint32_t chunk_count = *(const uint32_t *)(blob + 28u);
    if (chunk_count == 0u || chunk_count > 256u || 32u + (uint64_t)chunk_count * 4u > size) {
        return false;
    }
    bool has_shader_chunk = false;
    for (uint32_t i = 0u; i < chunk_count; ++i) {
        uint32_t off = *(const uint32_t *)(blob + 32u + (size_t)i * 4u);
        if ((uint64_t)off + 8u > size) {
            return false;
        }
        uint32_t fourcc = *(const uint32_t *)(blob + off);
        if (fourcc == 0x584F4258u || fourcc == 0x4D325358u) { /* 'XBOX' / 'XS2M' */
            return false;
        }
        if (fourcc == 0x58454853u || fourcc == 0x52444853u) { /* 'SHEX' / 'SHDR' */
            has_shader_chunk = true;
        }
    }
    return has_shader_chunk;
}

static bool d3d12xs_pe_lift_shader_ex(
    const void *src,
    uint64_t src_size,
    void **out_buf,
    uint64_t *out_size,
    uint32_t *out_used_dxil,
    uint32_t *out_kind,
    scarlett_hresult_t *out_lift_hr) {
    if (out_buf != NULL) *out_buf = NULL;
    if (out_size != NULL) *out_size = 0u;
    if (out_used_dxil != NULL) *out_used_dxil = 0u;
    if (out_kind != NULL) *out_kind = 0u;
    if (out_lift_hr != NULL) *out_lift_hr = SCARLETT_E_INVALIDARG;
    if (src == NULL || src_size < 16u || src_size > (UINT64_C(16) << 20)) {
        return false;
    }
    if (!d3d12xs_pe_is_readable(src, 16u)) {
        return false;
    }
    uint64_t cap = src_size * 4u;
    if (cap < 262144u) {
        cap = 262144u;
    }
    if (cap > (UINT64_C(8) << 20)) {
        cap = (UINT64_C(8) << 20);
    }
    void *dst = d3d12xs_pe_alloc_zero((size_t)cap);
    if (dst == NULL) {
        if (out_lift_hr != NULL) *out_lift_hr = SCARLETT_E_OUTOFMEMORY;
        return false;
    }
    scarlett_d3d12xs_lift_shader_params_t lp;
    d3d12xs_pe_zero(&lp, sizeof(lp));
    lp.src_addr = (uint64_t)(uintptr_t)src;
    lp.src_size = src_size;
    lp.dst_addr = (uint64_t)(uintptr_t)dst;
    lp.dst_capacity = cap;
    lp.shader_mode = 0u;
    int32_t rc = d3d12xs_pe_unix_call(SCARLETT_D3D12XS_UNIX_LIFT_SHADER, &lp);
    scarlett_hresult_t hr = scarlett_failed(rc) ? rc : lp.out_status;
    if (out_lift_hr != NULL) *out_lift_hr = hr;
    if (scarlett_failed(rc) || scarlett_failed(lp.out_status) || lp.out_dst_size == 0u) {
        d3d12xs_pe_free_mem(dst);
        /* Fallback for clean legacy SM5 DXBC without DXIL/ILDB chunk */
        if (d3d12xs_dxbc_is_clean_legacy((const uint8_t *)src, src_size)) {
            void *copy = d3d12xs_pe_alloc_zero((size_t)src_size);
            if (copy != NULL) {
                d3d12xs_pe_memcpy(copy, src, (size_t)src_size);
                if (out_buf != NULL) *out_buf = copy;
                else d3d12xs_pe_free_mem(copy);
                if (out_size != NULL) *out_size = src_size;
                if (out_used_dxil != NULL) *out_used_dxil = 1u;
                if (out_kind != NULL) *out_kind = 1u;
                if (out_lift_hr != NULL) *out_lift_hr = SCARLETT_OK;
                return true;
            }
        }
        return false;
    }
    if (out_buf != NULL) *out_buf = dst;
    else d3d12xs_pe_free_mem(dst);
    if (out_size != NULL) *out_size = lp.out_dst_size;
    if (out_used_dxil != NULL) *out_used_dxil = lp.out_used_dxil_fast_path;
    if (out_kind != NULL) *out_kind = lp.out_container_kind;
    return true;
}

#define SCARLETT_D3D12XS_SLOT55_MAGIC UINT64_C(0x534C353545585452)
typedef struct d3d12xs_slot55_extra_s {
    uint64_t magic;
    uint64_t flags;
    uint64_t override_ptr;
} d3d12xs_slot55_extra_t;

typedef struct d3d12xs_pc_rt_blend_s {
    int32_t BlendEnable;
    int32_t LogicOpEnable;
    uint32_t SrcBlend;
    uint32_t DestBlend;
    uint32_t BlendOp;
    uint32_t SrcBlendAlpha;
    uint32_t DestBlendAlpha;
    uint32_t BlendOpAlpha;
    uint32_t LogicOp;
    uint8_t RenderTargetWriteMask;
    uint8_t pad[3];
} d3d12xs_pc_rt_blend_t;

typedef struct d3d12xs_pc_blend_s {
    int32_t AlphaToCoverageEnable;
    int32_t IndependentBlendEnable;
    d3d12xs_pc_rt_blend_t RenderTarget[8];
} d3d12xs_pc_blend_t;

typedef struct d3d12xs_pc_shader_bc_s {
    const void *pShaderBytecode;
    uint64_t BytecodeLength;
} d3d12xs_pc_shader_bc_t;

typedef struct d3d12xs_pc_stream_out_s {
    const void *pSODeclaration;
    uint32_t NumEntries;
    uint32_t pad0;
    const void *pBufferStrides;
    uint32_t NumStrides;
    uint32_t RasterizedStream;
} d3d12xs_pc_stream_out_t;

typedef struct d3d12xs_pc_rasterizer_s {
    uint32_t FillMode;
    uint32_t CullMode;
    int32_t FrontCounterClockwise;
    int32_t DepthBias;
    float DepthBiasClamp;
    float SlopeScaledDepthBias;
    int32_t DepthClipEnable;
    int32_t MultisampleEnable;
    int32_t AntialiasedLineEnable;
    uint32_t ForcedSampleCount;
    uint32_t ConservativeRaster;
} d3d12xs_pc_rasterizer_t;

typedef struct d3d12xs_pc_ds_op_s {
    uint32_t StencilFailOp;
    uint32_t StencilDepthFailOp;
    uint32_t StencilPassOp;
    uint32_t StencilFunc;
} d3d12xs_pc_ds_op_t;

typedef struct d3d12xs_pc_depth_stencil_s {
    int32_t DepthEnable;
    uint32_t DepthWriteMask;
    uint32_t DepthFunc;
    int32_t StencilEnable;
    uint8_t StencilReadMask;
    uint8_t StencilWriteMask;
    uint8_t pad0[2];
    d3d12xs_pc_ds_op_t FrontFace;
    d3d12xs_pc_ds_op_t BackFace;
} d3d12xs_pc_depth_stencil_t;

typedef struct d3d12xs_pc_input_layout_s {
    const void *pInputElementDescs;
    uint32_t NumElements;
    uint32_t pad0;
} d3d12xs_pc_input_layout_t;

typedef struct d3d12xs_pc_sample_desc_s {
    uint32_t Count;
    uint32_t Quality;
} d3d12xs_pc_sample_desc_t;

typedef struct d3d12xs_pc_gfx_pso_desc_s {
    void *pRootSignature;
    d3d12xs_pc_shader_bc_t VS;
    d3d12xs_pc_shader_bc_t PS;
    d3d12xs_pc_shader_bc_t DS;
    d3d12xs_pc_shader_bc_t HS;
    d3d12xs_pc_shader_bc_t GS;
    d3d12xs_pc_stream_out_t StreamOutput;
    d3d12xs_pc_blend_t BlendState;
    uint32_t SampleMask;
    d3d12xs_pc_rasterizer_t RasterizerState;
    d3d12xs_pc_depth_stencil_t DepthStencilState;
    uint32_t pad_before_il;
    d3d12xs_pc_input_layout_t InputLayout;
    uint32_t IBStripCutValue;
    uint32_t PrimitiveTopologyType;
    uint32_t NumRenderTargets;
    uint32_t RTVFormats[8];
    uint32_t DSVFormat;
    d3d12xs_pc_sample_desc_t SampleDesc;
    uint32_t NodeMask;
    uint32_t pad_before_cached;
    d3d12xs_pc_shader_bc_t CachedPSO;
    uint32_t Flags;
    uint32_t pad_end;
} d3d12xs_pc_gfx_pso_desc_t;

typedef struct d3d12xs_pc_compute_pso_desc_s {
    void *pRootSignature;
    d3d12xs_pc_shader_bc_t CS;
    uint32_t NodeMask;
    uint32_t pad0;
    d3d12xs_pc_shader_bc_t CachedPSO;
    uint32_t Flags;
    uint32_t pad1;
} d3d12xs_pc_compute_pso_desc_t;

SCARLETT_STATIC_ASSERT(sizeof(d3d12xs_pc_blend_t) == 328, "D3D12_BLEND_DESC must be 328 bytes");
SCARLETT_STATIC_ASSERT(sizeof(d3d12xs_pc_gfx_pso_desc_t) == 656, "D3D12_GRAPHICS_PIPELINE_STATE_DESC must be 656 bytes");
SCARLETT_STATIC_ASSERT(sizeof(d3d12xs_pc_compute_pso_desc_t) == 56, "D3D12_COMPUTE_PIPELINE_STATE_DESC must be 56 bytes");

static void d3d12xs_init_default_gfx_pso_desc(d3d12xs_pc_gfx_pso_desc_t *d) {
    d3d12xs_pe_zero(d, sizeof(*d));
    for (size_t i = 0u; i < 8u; ++i) {
        d->BlendState.RenderTarget[i].SrcBlend = 2u;       /* ONE */
        d->BlendState.RenderTarget[i].DestBlend = 1u;      /* ZERO */
        d->BlendState.RenderTarget[i].BlendOp = 1u;        /* ADD */
        d->BlendState.RenderTarget[i].SrcBlendAlpha = 2u;  /* ONE */
        d->BlendState.RenderTarget[i].DestBlendAlpha = 1u; /* ZERO */
        d->BlendState.RenderTarget[i].BlendOpAlpha = 1u;   /* ADD */
        d->BlendState.RenderTarget[i].LogicOp = 4u;        /* NOOP */
        d->BlendState.RenderTarget[i].RenderTargetWriteMask = 0xFu;
    }
    d->SampleMask = 0xFFFFFFFFu;
    d->RasterizerState.FillMode = 3u;  /* SOLID */
    d->RasterizerState.CullMode = 3u;  /* BACK */
    d->RasterizerState.DepthClipEnable = 1;
    d->DepthStencilState.DepthWriteMask = 0u;
    d->DepthStencilState.DepthFunc = 4u; /* LESS */
    d->DepthStencilState.StencilReadMask = 0xFFu;
    d->DepthStencilState.StencilWriteMask = 0xFFu;
    d->DepthStencilState.FrontFace.StencilFailOp = 1u;
    d->DepthStencilState.FrontFace.StencilDepthFailOp = 1u;
    d->DepthStencilState.FrontFace.StencilPassOp = 1u;
    d->DepthStencilState.FrontFace.StencilFunc = 8u;
    d->DepthStencilState.BackFace = d->DepthStencilState.FrontFace;
    d->PrimitiveTopologyType = 3u; /* TRIANGLE */
    d->NumRenderTargets = 1u;
    d->RTVFormats[0] = 28u; /* DXGI_FORMAT_R8G8B8A8_UNORM */
    d->SampleDesc.Count = 1u;
    d->SampleDesc.Quality = 0u;
}

static void *d3d12xs_wrap_spirv_as_dxbc_spv0(
    const void *spv,
    uint64_t spv_bytes,
    uint64_t *out_dxbc_bytes) {
    if (out_dxbc_bytes != NULL) *out_dxbc_bytes = 0u;
    if (spv == NULL || spv_bytes < 20u || (spv_bytes & 3u) != 0u ||
        spv_bytes > (UINT64_C(8) << 20)) {
        return NULL;
    }
    if (*(const uint32_t *)spv != 0x07230203u) {
        return NULL;
    }
    uint64_t total = 44u + spv_bytes;
    uint8_t *buf = (uint8_t *)d3d12xs_pe_alloc_zero((size_t)total);
    if (buf == NULL) {
        return NULL;
    }
    uint32_t *hdr = (uint32_t *)buf;
    hdr[0] = 0x43425844u; /* 'DXBC' */
    hdr[5] = 1u;          /* version */
    hdr[6] = (uint32_t)total;
    hdr[7] = 1u;          /* chunk count */
    hdr[8] = 36u;         /* offset of SPV0 chunk */
    hdr[9] = 0x30565053u; /* 'SPV0' */
    hdr[10] = (uint32_t)spv_bytes;
    d3d12xs_pe_memcpy(buf + 44u, spv, (size_t)spv_bytes);
    if (out_dxbc_bytes != NULL) *out_dxbc_bytes = total;
    return buf;
}

#define SCARLETT_D3D12XS_ISA_CACHE_SIZE 4096u
typedef struct d3d12xs_isa_cache_entry_s {
    uint32_t state; /* 0 = empty, 1 = filling, 2 = ready */
    uint32_t stage;
    const void *isa_ptr;
    uint64_t isa_bytes;
    void *dxbc_buf;
    uint64_t dxbc_bytes;
} d3d12xs_isa_cache_entry_t;

static d3d12xs_isa_cache_entry_t g_isa_dxbc_cache[SCARLETT_D3D12XS_ISA_CACHE_SIZE];

static bool d3d12xs_lift_raw_isa_cached(
    const void *isa_ptr,
    uint64_t isa_bytes,
    uint32_t stage,
    void **out_dxbc,
    uint64_t *out_dxbc_len,
    bool *out_owned,
    scarlett_hresult_t *out_lhr) {
    if (out_dxbc != NULL) *out_dxbc = NULL;
    if (out_dxbc_len != NULL) *out_dxbc_len = 0u;
    if (out_owned != NULL) *out_owned = false;
    if (isa_ptr == NULL || isa_bytes < 4u || (isa_bytes & 3u) != 0u ||
        isa_bytes > (UINT64_C(1) << 20)) {
        return false;
    }
    uint64_t h = ((uint64_t)(uintptr_t)isa_ptr >> 2u) ^
                 (isa_bytes * UINT64_C(0x9E3779B97F4A7C15)) ^
                 ((uint64_t)stage * UINT64_C(0xBF58476D1CE4E5B9));
    uint32_t slot = (uint32_t)(h & (SCARLETT_D3D12XS_ISA_CACHE_SIZE - 1u));
    d3d12xs_isa_cache_entry_t *cached_slot = NULL;
    for (uint32_t probe = 0u; probe < 16u; ++probe) {
        uint32_t idx = (slot + probe) & (SCARLETT_D3D12XS_ISA_CACHE_SIZE - 1u);
        d3d12xs_isa_cache_entry_t *e = &g_isa_dxbc_cache[idx];
        uint32_t st = __atomic_load_n(&e->state, __ATOMIC_ACQUIRE);
        if (st == 2u) {
            if (e->isa_ptr == isa_ptr && e->isa_bytes == isa_bytes && e->stage == stage) {
                if (e->dxbc_buf != NULL && e->dxbc_bytes > 0u) {
                    if (out_dxbc != NULL) *out_dxbc = e->dxbc_buf;
                    if (out_dxbc_len != NULL) *out_dxbc_len = e->dxbc_bytes;
                    if (out_owned != NULL) *out_owned = false;
                    if (out_lhr != NULL) *out_lhr = SCARLETT_OK;
                    return true;
                }
                return false;
            }
            continue;
        }
        if (st == 0u) {
            uint32_t expected = 0u;
            if (__atomic_compare_exchange_n(
                    &e->state, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                cached_slot = e;
                break;
            }
            /* Someone else took this slot; wait briefly if it matches our key or continue probing */
            while (__atomic_load_n(&e->state, __ATOMIC_ACQUIRE) == 1u) {
                __asm__ __volatile__("pause");
            }
            if (__atomic_load_n(&e->state, __ATOMIC_ACQUIRE) == 2u &&
                e->isa_ptr == isa_ptr && e->isa_bytes == isa_bytes && e->stage == stage) {
                if (e->dxbc_buf != NULL && e->dxbc_bytes > 0u) {
                    if (out_dxbc != NULL) *out_dxbc = e->dxbc_buf;
                    if (out_dxbc_len != NULL) *out_dxbc_len = e->dxbc_bytes;
                    if (out_owned != NULL) *out_owned = false;
                    if (out_lhr != NULL) *out_lhr = SCARLETT_OK;
                    return true;
                }
                return false;
            }
        }
    }

    void *dxbc_buf = NULL;
    uint64_t dxbc_len = 0u;
    if (d3d12xs_pe_is_readable(isa_ptr, (size_t)isa_bytes)) {
        uint8_t *ps09 = (uint8_t *)d3d12xs_pe_alloc_zero((size_t)(52u + isa_bytes));
        if (ps09 != NULL) {
            uint32_t *ph = (uint32_t *)ps09;
            ph[0] = 0x09005350u; /* "PS\x00\x09" */
            ph[1] = 0x0A03u;     /* GFX10.3 */
            ph[2] = stage;
            ph[5] = 64u;         /* wave_size */
            ph[7] = 64u;         /* wg_x */
            ph[8] = 1u;          /* wg_y */
            ph[9] = 1u;          /* wg_z */
            ph[10] = 1u;         /* export_mask */
            ph[12] = (uint32_t)isa_bytes;
            d3d12xs_pe_memcpy(ps09 + 52u, isa_ptr, (size_t)isa_bytes);
            void *spv_buf = NULL;
            uint64_t spv_len = 0u;
            uint32_t used_dxil = 0u;
            uint32_t kind = 0u;
            if (d3d12xs_pe_lift_shader_ex(
                    ps09, 52u + isa_bytes, &spv_buf, &spv_len,
                    &used_dxil, &kind, out_lhr)) {
                if (used_dxil == 0u && spv_buf != NULL) {
                    dxbc_buf = d3d12xs_wrap_spirv_as_dxbc_spv0(spv_buf, spv_len, &dxbc_len);
                    d3d12xs_pe_free_mem(spv_buf);
                } else if (used_dxil != 0u && spv_buf != NULL) {
                    dxbc_buf = spv_buf;
                    dxbc_len = spv_len;
                }
            }
            d3d12xs_pe_free_mem(ps09);
        }
    }

    if (cached_slot != NULL) {
        cached_slot->isa_ptr = isa_ptr;
        cached_slot->isa_bytes = isa_bytes;
        cached_slot->stage = stage;
        cached_slot->dxbc_buf = dxbc_buf;
        cached_slot->dxbc_bytes = dxbc_len;
        __atomic_store_n(&cached_slot->state, 2u, __ATOMIC_RELEASE);
        if (dxbc_buf != NULL && dxbc_len > 0u) {
            if (out_dxbc != NULL) *out_dxbc = dxbc_buf;
            if (out_dxbc_len != NULL) *out_dxbc_len = dxbc_len;
            if (out_owned != NULL) *out_owned = false;
            return true;
        }
        return false;
    }

    if (dxbc_buf != NULL && dxbc_len > 0u) {
        if (out_dxbc != NULL) *out_dxbc = dxbc_buf;
        if (out_dxbc_len != NULL) *out_dxbc_len = dxbc_len;
        if (out_owned != NULL) *out_owned = true;
        return true;
    }
    return false;
}

static scarlett_hresult_t com_create_pso_impl(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppPipelineState,
    uint32_t vtable_slot,
    uint64_t return_address,
    uint64_t extra_info) {
    if (self == NULL || ppPipelineState == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppPipelineState = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12PipelineState) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreatePipelineState",
            vtable_slot,
            return_address,
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    static uint32_t g_com_psos_cursor = 0u;
    ID3D12PipelineStateX *pso = NULL;
    d3d12xs_pso_meta_t *pm = NULL;
    uint32_t ticket = __atomic_fetch_add(&g_com_psos_cursor, 1u, __ATOMIC_RELAXED);
    if (ticket < SCARLETT_D3D12XS_MAX_COM_PSOS) {
        uint32_t c = ticket / SCARLETT_D3D12XS_PSO_CHUNK_SIZE;
        uint32_t off = ticket % SCARLETT_D3D12XS_PSO_CHUNK_SIZE;
        if (d3d12xs_ensure_pso_chunk(c)) {
            uint32_t expected = 0u;
            if (__atomic_compare_exchange_n(
                    &g_pso_meta_chunks[c][off].in_use, &expected, 1u,
                    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                pso = &g_pso_chunks[c][off];
                pm = &g_pso_meta_chunks[c][off];
            }
        }
    }
    if (pso == NULL || pm == NULL) {
        for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_PSOS; ++i) {
            size_t cand = ((size_t)ticket + i) % SCARLETT_D3D12XS_MAX_COM_PSOS;
            uint32_t c = (uint32_t)(cand / SCARLETT_D3D12XS_PSO_CHUNK_SIZE);
            uint32_t off = (uint32_t)(cand % SCARLETT_D3D12XS_PSO_CHUNK_SIZE);
            if (!d3d12xs_ensure_pso_chunk(c)) {
                break;
            }
            uint32_t expected = 0u;
            if (__atomic_compare_exchange_n(
                    &g_pso_meta_chunks[c][off].in_use, &expected, 1u,
                    false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
                pso = &g_pso_chunks[c][off];
                pm = &g_pso_meta_chunks[c][off];
                break;
            }
        }
    }
    if (pso == NULL || pm == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreatePipelineState(OOM)",
            vtable_slot,
            return_address,
            (uint64_t)(uintptr_t)self,
            (uint64_t)(uintptr_t)pDesc,
            extra_info,
            0u,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    pso->lpVtbl = &g_pso_vtbl;
    pm->unix_device_handle = self->unix_device_handle;
    pm->ref_count = 1u;
    pm->in_use = 1u;
    pm->is_compute = (vtable_slot == 11u || vtable_slot == 62u || vtable_slot == 67u) ? 1u : 0u;
    pm->lifted_spirv_bytes = 0u;
    pm->saved_owns_dxbc = 0u;
    pm->reserved_pad = 0u;
    pm->real_obj = NULL;
    pm->lifted_spirv_buf = NULL;
    pm->saved_real_rs = NULL;
    pm->saved_vs_dxbc = NULL;
    pm->saved_vs_len = 0u;
    pm->saved_ps_dxbc = NULL;
    pm->saved_ps_len = 0u;

    static uint32_t g_pso_log_count = 0u;
    uint32_t seq = __atomic_add_fetch(&g_pso_log_count, 1u, __ATOMIC_RELAXED);

    /* Inspect & translate shaders/PSO when pDesc points to readable memory */
    uint32_t diag_magic0 = 0u;
    uint32_t diag_found_magic = 0u;
    uint64_t diag_q0 = 0u;
    uint64_t diag_q1 = 0u;
    uint64_t diag_q2 = 0u;
    uint64_t diag_q3 = 0u;
    uint32_t diag_lifted_stages = 0u;
    uint32_t diag_dxil_stages = 0u;
    uint32_t diag_spirv_stages = 0u;
    scarlett_hresult_t diag_lift_hr = 0;
    scarlett_hresult_t real_hr = 0;

    if (pDesc != NULL && d3d12xs_pe_is_readable(pDesc, 16u)) {
        const uint64_t *qwords = (const uint64_t *)pDesc;
        diag_q0 = qwords[0];
        diag_q1 = qwords[1];
        diag_magic0 = *(const uint32_t *)pDesc;
        if (d3d12xs_pe_is_readable(pDesc, 24u)) {
            diag_q2 = qwords[2];
        }
        if (d3d12xs_pe_is_readable(pDesc, 32u)) {
            diag_q3 = qwords[3];
        }

        /* Standard PC D3D12_GRAPHICS_PIPELINE_STATE_DESC (656 bytes) */
        if (vtable_slot == 10u && d3d12xs_pe_is_readable(pDesc, 656u)) {
            uint8_t pc_desc[656];
            d3d12xs_pe_memcpy(pc_desc, pDesc, sizeof(pc_desc));
            void *orig_rs = *(void **)(pc_desc + 0x00u);
            d3d12xs_root_sig_meta_t *rsm =
                d3d12xs_is_com_root_sig(orig_rs) ? d3d12xs_get_root_sig_meta(orig_rs) : NULL;
            if (rsm != NULL) {
                *(void **)(pc_desc + 0x00u) = rsm->real_obj;
            }
            void *dxil_bufs[5] = { NULL, NULL, NULL, NULL, NULL };
            bool all_dxil_ok = true;
            for (size_t s = 0u; s < 5u; ++s) {
                size_t off = 0x08u + s * 16u;
                const void *sc_ptr = *(const void * const *)(pc_desc + off);
                uint64_t sc_len = *(const uint64_t *)(pc_desc + off + 8u);
                if (sc_ptr != NULL && sc_len >= 16u) {
                    if (diag_found_magic == 0u && d3d12xs_pe_is_readable(sc_ptr, 4u)) {
                        diag_found_magic = *(const uint32_t *)sc_ptr;
                    }
                    void *lifted_buf = NULL;
                    uint64_t lifted_sz = 0u;
                    uint32_t used_dxil = 0u;
                    uint32_t kind = 0u;
                    if (d3d12xs_pe_lift_shader_ex(
                            sc_ptr, sc_len, &lifted_buf, &lifted_sz,
                            &used_dxil, &kind, &diag_lift_hr)) {
                        ++diag_lifted_stages;
                        if (used_dxil != 0u) {
                            ++diag_dxil_stages;
                            dxil_bufs[s] = lifted_buf;
                            *(void **)(pc_desc + off) = lifted_buf;
                            *(uint64_t *)(pc_desc + off + 8u) = lifted_sz;
                        } else {
                            ++diag_spirv_stages;
                            all_dxil_ok = false;
                            if (pm->lifted_spirv_buf == NULL) {
                                pm->lifted_spirv_buf = lifted_buf;
                                pm->lifted_spirv_bytes = (uint32_t)lifted_sz;
                            } else {
                                d3d12xs_pe_free_mem(lifted_buf);
                            }
                        }
                    } else {
                        all_dxil_ok = false;
                    }
                }
            }
            if (self->real_device != NULL && all_dxil_ok && diag_dxil_stages > 0u) {
                typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cgpso_t)(
                    void *, const void *, const scarlett_guid_t *, void **);
                void **vtbl = *(void ***)self->real_device;
                void *real_pso = NULL;
                real_hr = ((pfn_real_cgpso_t)vtbl[10])(
                    self->real_device, pc_desc, &kIidID3D12PipelineState, &real_pso);
                if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                    pm->real_obj = real_pso;
                }
            }
            for (size_t s = 0u; s < 5u; ++s) {
                if (dxil_bufs[s] != NULL) {
                    d3d12xs_pe_free_mem(dxil_bufs[s]);
                }
            }
        } else if (vtable_slot == 11u && d3d12xs_pe_is_readable(pDesc, 56u)) {
            /* Standard PC D3D12_COMPUTE_PIPELINE_STATE_DESC (56 bytes) */
            uint8_t pc_desc[56];
            d3d12xs_pe_memcpy(pc_desc, pDesc, sizeof(pc_desc));
            void *orig_rs = *(void **)(pc_desc + 0x00u);
            d3d12xs_root_sig_meta_t *rsm =
                d3d12xs_is_com_root_sig(orig_rs) ? d3d12xs_get_root_sig_meta(orig_rs) : NULL;
            if (rsm != NULL) {
                *(void **)(pc_desc + 0x00u) = rsm->real_obj;
            }
            const void *cs_ptr = *(const void * const *)(pc_desc + 0x08u);
            uint64_t cs_len = *(const uint64_t *)(pc_desc + 0x10u);
            void *dxil_buf = NULL;
            if (cs_ptr != NULL && cs_len >= 16u) {
                if (d3d12xs_pe_is_readable(cs_ptr, 4u)) {
                    diag_found_magic = *(const uint32_t *)cs_ptr;
                }
                void *lifted_buf = NULL;
                uint64_t lifted_sz = 0u;
                uint32_t used_dxil = 0u;
                uint32_t kind = 0u;
                if (d3d12xs_pe_lift_shader_ex(
                        cs_ptr, cs_len, &lifted_buf, &lifted_sz,
                        &used_dxil, &kind, &diag_lift_hr)) {
                    ++diag_lifted_stages;
                    if (used_dxil != 0u) {
                        ++diag_dxil_stages;
                        dxil_buf = lifted_buf;
                        *(void **)(pc_desc + 0x08u) = lifted_buf;
                        *(uint64_t *)(pc_desc + 0x10u) = lifted_sz;
                        if (self->real_device != NULL) {
                            typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccpso_t)(
                                void *, const void *, const scarlett_guid_t *, void **);
                            void **vtbl = *(void ***)self->real_device;
                            void *real_pso = NULL;
                            real_hr = ((pfn_real_ccpso_t)vtbl[11])(
                                self->real_device, pc_desc, &kIidID3D12PipelineState, &real_pso);
                            if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                                pm->real_obj = real_pso;
                            }
                        }
                    } else {
                        ++diag_spirv_stages;
                        pm->lifted_spirv_buf = lifted_buf;
                        pm->lifted_spirv_bytes = (uint32_t)lifted_sz;
                    }
                }
            }
            if (dxil_buf != NULL) {
                d3d12xs_pe_free_mem(dxil_buf);
            }
        } else if (d3d12xs_is_shader_magic(diag_magic0)) {
            /* Direct shader container blob passed as pDesc */
            diag_found_magic = diag_magic0;
            uint64_t blob_sz = d3d12xs_infer_shader_size(pDesc, extra_info);
            if (blob_sz >= 16u) {
                void *lifted_buf = NULL;
                uint64_t lifted_sz = 0u;
                uint32_t used_dxil = 0u;
                uint32_t kind = 0u;
                if (d3d12xs_pe_lift_shader_ex(
                        pDesc, blob_sz, &lifted_buf, &lifted_sz,
                        &used_dxil, &kind, &diag_lift_hr)) {
                    ++diag_lifted_stages;
                    if (used_dxil != 0u) {
                        ++diag_dxil_stages;
                        d3d12xs_pe_free_mem(lifted_buf);
                    } else {
                        ++diag_spirv_stages;
                        pm->lifted_spirv_buf = lifted_buf;
                        pm->lifted_spirv_bytes = (uint32_t)lifted_sz;
                    }
                }
            }
        } else if (vtable_slot == 55u && d3d12xs_get_pso_meta(pDesc) != NULL) {
            /* Xbox derived PSO: pDesc = base ID3D12PipelineStateX*, extra = d3d12xs_slot55_extra_t* */
            d3d12xs_pso_meta_t *base_pm = d3d12xs_get_pso_meta(pDesc);
            const d3d12xs_slot55_extra_t *s55 =
                (extra_info >= UINT64_C(0x10000) &&
                 d3d12xs_pe_is_readable((const void *)(uintptr_t)extra_info, sizeof(d3d12xs_slot55_extra_t)) &&
                 ((const d3d12xs_slot55_extra_t *)(uintptr_t)extra_info)->magic == SCARLETT_D3D12XS_SLOT55_MAGIC)
                    ? (const d3d12xs_slot55_extra_t *)(uintptr_t)extra_info
                    : NULL;
            if (base_pm != NULL && base_pm->saved_real_rs != NULL &&
                (base_pm->saved_vs_dxbc != NULL || base_pm->saved_ps_dxbc != NULL)) {
                pm->saved_real_rs = base_pm->saved_real_rs;
                if (base_pm->saved_owns_dxbc == 0u) {
                    pm->saved_vs_dxbc = base_pm->saved_vs_dxbc;
                    pm->saved_vs_len = base_pm->saved_vs_len;
                    pm->saved_ps_dxbc = base_pm->saved_ps_dxbc;
                    pm->saved_ps_len = base_pm->saved_ps_len;
                    pm->saved_owns_dxbc = 0u;
                } else {
                    if (base_pm->saved_vs_dxbc != NULL && base_pm->saved_vs_len > 0u) {
                        void *vs_copy = d3d12xs_pe_alloc_zero((size_t)base_pm->saved_vs_len);
                        if (vs_copy != NULL) {
                            d3d12xs_pe_memcpy(vs_copy, base_pm->saved_vs_dxbc, (size_t)base_pm->saved_vs_len);
                            pm->saved_vs_dxbc = vs_copy;
                            pm->saved_vs_len = base_pm->saved_vs_len;
                        }
                    }
                    if (base_pm->saved_ps_dxbc != NULL && base_pm->saved_ps_len > 0u) {
                        void *ps_copy = d3d12xs_pe_alloc_zero((size_t)base_pm->saved_ps_len);
                        if (ps_copy != NULL) {
                            d3d12xs_pe_memcpy(ps_copy, base_pm->saved_ps_dxbc, (size_t)base_pm->saved_ps_len);
                            pm->saved_ps_dxbc = ps_copy;
                            pm->saved_ps_len = base_pm->saved_ps_len;
                        }
                    }
                    pm->saved_owns_dxbc = 1u;
                }
                if (self->real_device != NULL && pm->saved_vs_dxbc != NULL) {
                    d3d12xs_pc_gfx_pso_desc_t pc_desc;
                    d3d12xs_init_default_gfx_pso_desc(&pc_desc);
                    pc_desc.pRootSignature = pm->saved_real_rs;
                    pc_desc.VS.pShaderBytecode = pm->saved_vs_dxbc;
                    pc_desc.VS.BytecodeLength = pm->saved_vs_len;
                    pc_desc.PS.pShaderBytecode = pm->saved_ps_dxbc;
                    pc_desc.PS.BytecodeLength = pm->saved_ps_len;
                    if (s55 != NULL && s55->override_ptr >= UINT64_C(0x10000) &&
                        d3d12xs_pe_is_readable((const void *)(uintptr_t)s55->override_ptr, 4u + sizeof(d3d12xs_pc_blend_t))) {
                        const uint8_t *ov = (const uint8_t *)(uintptr_t)s55->override_ptr;
                        if (*(const uint32_t *)ov == 2u) {
                            d3d12xs_pe_memcpy(&pc_desc.BlendState, ov + 4u, sizeof(d3d12xs_pc_blend_t));
                        }
                    }
                    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cgpso_t)(
                        void *, const void *, const scarlett_guid_t *, void **);
                    void **vtbl = *(void ***)self->real_device;
                    void *real_pso = NULL;
                    real_hr = ((pfn_real_cgpso_t)vtbl[10])(
                        self->real_device, &pc_desc, &kIidID3D12PipelineState, &real_pso);
                    if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                        pm->real_obj = real_pso;
                        diag_found_magic = 0x43425844u;
                    }
                }
            }
        } else if (vtable_slot == 67u && d3d12xs_pe_is_readable(pDesc, 64u) &&
                   d3d12xs_is_com_root_sig((const void *)(uintptr_t)((const uint64_t *)pDesc)[0])) {
            /* Xbox Compute PSO: q0 = RootSignature, q5 = ISA ptr, q6 = ISA bytes */
            const uint64_t *xq = (const uint64_t *)pDesc;
            const void *rs_ptr = (const void *)(uintptr_t)xq[0];
            d3d12xs_root_sig_meta_t *rsm =
                d3d12xs_is_com_root_sig(rs_ptr) ? d3d12xs_get_root_sig_meta(rs_ptr) : NULL;
            void *real_rs = (rsm != NULL) ? rsm->real_obj : NULL;
            const void *cs_isa = (const void *)(uintptr_t)xq[5];
            uint64_t cs_isa_len = xq[6];
            void *cs_dxbc = NULL;
            uint64_t cs_dxbc_len = 0u;
            bool cs_owned = false;
            if (real_rs != NULL &&
                d3d12xs_lift_raw_isa_cached(
                    cs_isa, cs_isa_len, 2u, &cs_dxbc, &cs_dxbc_len, &cs_owned, &diag_lift_hr)) {
                ++diag_lifted_stages;
                ++diag_dxil_stages;
                diag_found_magic = 0x43425844u;
                if (self->real_device != NULL) {
                    d3d12xs_pc_compute_pso_desc_t pc_desc;
                    d3d12xs_pe_zero(&pc_desc, sizeof(pc_desc));
                    pc_desc.pRootSignature = real_rs;
                    pc_desc.CS.pShaderBytecode = cs_dxbc;
                    pc_desc.CS.BytecodeLength = cs_dxbc_len;
                    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccpso_t)(
                        void *, const void *, const scarlett_guid_t *, void **);
                    void **vtbl = *(void ***)self->real_device;
                    void *real_pso = NULL;
                    real_hr = ((pfn_real_ccpso_t)vtbl[11])(
                        self->real_device, &pc_desc, &kIidID3D12PipelineState, &real_pso);
                    if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                        pm->real_obj = real_pso;
                    }
                }
                if (cs_owned && cs_dxbc != NULL) {
                    d3d12xs_pe_free_mem(cs_dxbc);
                }
            }
        } else if (vtable_slot == 57u && d3d12xs_pe_is_readable(pDesc, 96u) &&
                   d3d12xs_is_com_root_sig((const void *)(uintptr_t)((const uint64_t *)pDesc)[0])) {
            /* Xbox Graphics PSO: q0 = RootSignature, q5/q6 = VS ISA, q9/q10 = PS ISA */
            const uint64_t *xq = (const uint64_t *)pDesc;
            const void *rs_ptr = (const void *)(uintptr_t)xq[0];
            d3d12xs_root_sig_meta_t *rsm =
                d3d12xs_is_com_root_sig(rs_ptr) ? d3d12xs_get_root_sig_meta(rs_ptr) : NULL;
            void *real_rs = (rsm != NULL) ? rsm->real_obj : NULL;
            void *vs_dxbc = NULL;
            uint64_t vs_len = 0u;
            bool vs_owned = false;
            void *ps_dxbc = NULL;
            uint64_t ps_len = 0u;
            bool ps_owned = false;
            if (xq[5] != 0u && xq[6] >= 4u &&
                d3d12xs_lift_raw_isa_cached(
                    (const void *)(uintptr_t)xq[5], xq[6], 0u,
                    &vs_dxbc, &vs_len, &vs_owned, &diag_lift_hr)) {
                ++diag_lifted_stages;
                ++diag_dxil_stages;
                diag_found_magic = 0x43425844u;
            }
            if (xq[9] != 0u && xq[10] >= 4u &&
                d3d12xs_lift_raw_isa_cached(
                    (const void *)(uintptr_t)xq[9], xq[10], 1u,
                    &ps_dxbc, &ps_len, &ps_owned, &diag_lift_hr)) {
                ++diag_lifted_stages;
                ++diag_dxil_stages;
                if (diag_found_magic == 0u) diag_found_magic = 0x43425844u;
            }
            pm->saved_real_rs = real_rs;
            if (!vs_owned) {
                pm->saved_vs_dxbc = vs_dxbc;
                pm->saved_vs_len = vs_len;
            }
            if (!ps_owned) {
                pm->saved_ps_dxbc = ps_dxbc;
                pm->saved_ps_len = ps_len;
            }
            pm->saved_owns_dxbc = 0u;
            if (self->real_device != NULL && real_rs != NULL && vs_dxbc != NULL) {
                d3d12xs_pc_gfx_pso_desc_t pc_desc;
                d3d12xs_init_default_gfx_pso_desc(&pc_desc);
                pc_desc.pRootSignature = real_rs;
                pc_desc.VS.pShaderBytecode = vs_dxbc;
                pc_desc.VS.BytecodeLength = vs_len;
                pc_desc.PS.pShaderBytecode = ps_dxbc;
                pc_desc.PS.BytecodeLength = ps_len;
                typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cgpso_t)(
                    void *, const void *, const scarlett_guid_t *, void **);
                void **vtbl = *(void ***)self->real_device;
                void *real_pso = NULL;
                real_hr = ((pfn_real_cgpso_t)vtbl[10])(
                    self->real_device, &pc_desc, &kIidID3D12PipelineState, &real_pso);
                if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                    pm->real_obj = real_pso;
                }
            }
            if (vs_owned && vs_dxbc != NULL) {
                d3d12xs_pe_free_mem(vs_dxbc);
            }
            if (ps_owned && ps_dxbc != NULL) {
                d3d12xs_pe_free_mem(ps_dxbc);
            }
        } else {
            /* Stream desc (D3D12_PIPELINE_STATE_STREAM_DESC) or Xbox PSO struct scan */
            const void *scan_buf = pDesc;
            size_t scan_limit = (pm->is_compute != 0u) ? 128u : 656u;
            size_t scan_bytes = d3d12xs_pe_readable_bytes(pDesc, scan_limit);
            bool is_stream_desc = false;
            if ((vtable_slot == 47u || vtable_slot == 86u ||
                 (diag_q0 >= 16u && diag_q0 <= 16384u && (diag_q0 & 7u) == 0u)) &&
                diag_q1 != 0u &&
                d3d12xs_pe_is_readable((const void *)(uintptr_t)diag_q1, (size_t)diag_q0)) {
                scan_buf = (const void *)(uintptr_t)diag_q1;
                scan_bytes = (size_t)diag_q0;
                is_stream_desc = true;
            }

            void *found_real_rs = NULL;
            void *dxil_bufs[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
            uint64_t dxil_sizes[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
            const void *dxil_orig_ptrs[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
            size_t dxil_count = 0u;
            const void *seen_ptrs[16];
            size_t seen_count = 0u;

            uint8_t *patched_stream = NULL;
            if (is_stream_desc && scan_bytes <= 16384u) {
                patched_stream = (uint8_t *)d3d12xs_pe_alloc_zero(scan_bytes);
                if (patched_stream != NULL) {
                    d3d12xs_pe_memcpy(patched_stream, scan_buf, scan_bytes);
                }
            }

            const uint64_t *sq = (const uint64_t *)scan_buf;
            size_t nq = scan_bytes / 8u;
            for (size_t i = 0u; i < nq; ++i) {
                uint64_t w = sq[i];
                const void *cand_ptr = (const void *)(uintptr_t)w;
                if (d3d12xs_is_com_root_sig(cand_ptr)) {
                    d3d12xs_root_sig_meta_t *rsm = d3d12xs_get_root_sig_meta(cand_ptr);
                    if (rsm != NULL && rsm->real_obj != NULL) {
                        if (found_real_rs == NULL) {
                            found_real_rs = rsm->real_obj;
                        }
                        if (patched_stream != NULL) {
                            *(void **)(patched_stream + i * 8u) = rsm->real_obj;
                        }
                    }
                    continue;
                }
                if (w < UINT64_C(0x10000) || w > UINT64_C(0x00007FFFFFFFFFFF) ||
                    cand_ptr == pDesc || cand_ptr == scan_buf ||
                    d3d12xs_get_pso_meta(cand_ptr) != NULL) {
                    continue;
                }
                if (!d3d12xs_pe_is_readable(cand_ptr, 16u)) {
                    continue;
                }
                uint32_t cmagic = *(const uint32_t *)cand_ptr;
                if (d3d12xs_is_shader_magic(cmagic)) {
                    bool dup = false;
                    for (size_t s = 0u; s < seen_count; ++s) {
                        if (seen_ptrs[s] == cand_ptr) {
                            dup = true;
                            break;
                        }
                    }
                    if (dup) {
                        continue;
                    }
                    if (seen_count < 16u) {
                        seen_ptrs[seen_count++] = cand_ptr;
                    }
                    if (diag_found_magic == 0u) {
                        diag_found_magic = cmagic;
                    }
                    uint64_t hint_len = (i + 1u < nq) ? sq[i + 1u] : 0u;
                    uint64_t cand_len = d3d12xs_infer_shader_size(cand_ptr, hint_len);
                    if (cand_len >= 16u) {
                        void *lifted_buf = NULL;
                        uint64_t lifted_sz = 0u;
                        uint32_t used_dxil = 0u;
                        uint32_t kind = 0u;
                        if (d3d12xs_pe_lift_shader_ex(
                                cand_ptr, cand_len, &lifted_buf, &lifted_sz,
                                &used_dxil, &kind, &diag_lift_hr)) {
                            ++diag_lifted_stages;
                            if (used_dxil != 0u) {
                                ++diag_dxil_stages;
                                if (dxil_count < 8u) {
                                    dxil_orig_ptrs[dxil_count] = cand_ptr;
                                    dxil_bufs[dxil_count] = lifted_buf;
                                    dxil_sizes[dxil_count] = lifted_sz;
                                    if (patched_stream != NULL && i + 1u < nq) {
                                        *(void **)(patched_stream + i * 8u) = lifted_buf;
                                        *(uint64_t *)(patched_stream + (i + 1u) * 8u) = lifted_sz;
                                    }
                                    ++dxil_count;
                                } else {
                                    d3d12xs_pe_free_mem(lifted_buf);
                                }
                            } else {
                                ++diag_spirv_stages;
                                if (pm->lifted_spirv_buf == NULL) {
                                    pm->lifted_spirv_buf = lifted_buf;
                                    pm->lifted_spirv_bytes = (uint32_t)lifted_sz;
                                } else {
                                    d3d12xs_pe_free_mem(lifted_buf);
                                }
                            }
                            if (pm->is_compute != 0u && !is_stream_desc) {
                                break;
                            }
                        }
                    }
                } else if (!is_stream_desc && i < 8u) {
                    /* 1-level indirection into nested descriptor/shader wrapper struct */
                    size_t sub_bytes = d3d12xs_pe_readable_bytes(cand_ptr, 512u);
                    if (sub_bytes >= 16u) {
                        const uint64_t *sub_q = (const uint64_t *)cand_ptr;
                        size_t sub_nq = sub_bytes / 8u;
                        for (size_t j = 0u; j < sub_nq; ++j) {
                            uint64_t sw = sub_q[j];
                            const void *sp = (const void *)(uintptr_t)sw;
                            if (d3d12xs_is_com_root_sig(sp)) {
                                d3d12xs_root_sig_meta_t *rsm = d3d12xs_get_root_sig_meta(sp);
                                if (rsm != NULL && rsm->real_obj != NULL && found_real_rs == NULL) {
                                    found_real_rs = rsm->real_obj;
                                }
                                continue;
                            }
                            if (sw < UINT64_C(0x10000) || sw > UINT64_C(0x00007FFFFFFFFFFF) ||
                                !d3d12xs_pe_is_readable(sp, 16u)) {
                                continue;
                            }
                            uint32_t smagic = *(const uint32_t *)sp;
                            if (!d3d12xs_is_shader_magic(smagic)) {
                                continue;
                            }
                            bool dup = false;
                            for (size_t s = 0u; s < seen_count; ++s) {
                                if (seen_ptrs[s] == sp) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (dup) {
                                continue;
                            }
                            if (seen_count < 16u) {
                                seen_ptrs[seen_count++] = sp;
                            }
                            if (diag_found_magic == 0u) {
                                diag_found_magic = smagic;
                            }
                            uint64_t shint = (j + 1u < sub_nq) ? sub_q[j + 1u] : 0u;
                            uint64_t slen = d3d12xs_infer_shader_size(sp, shint);
                            if (slen >= 16u) {
                                void *lifted_buf = NULL;
                                uint64_t lifted_sz = 0u;
                                uint32_t used_dxil = 0u;
                                uint32_t kind = 0u;
                                if (d3d12xs_pe_lift_shader_ex(
                                        sp, slen, &lifted_buf, &lifted_sz,
                                        &used_dxil, &kind, &diag_lift_hr)) {
                                    ++diag_lifted_stages;
                                    if (used_dxil != 0u) {
                                        ++diag_dxil_stages;
                                        if (dxil_count < 8u) {
                                            dxil_orig_ptrs[dxil_count] = sp;
                                            dxil_bufs[dxil_count] = lifted_buf;
                                            dxil_sizes[dxil_count] = lifted_sz;
                                            ++dxil_count;
                                        } else {
                                            d3d12xs_pe_free_mem(lifted_buf);
                                        }
                                    } else {
                                        ++diag_spirv_stages;
                                        if (pm->lifted_spirv_buf == NULL) {
                                            pm->lifted_spirv_buf = lifted_buf;
                                            pm->lifted_spirv_bytes = (uint32_t)lifted_sz;
                                        } else {
                                            d3d12xs_pe_free_mem(lifted_buf);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (self->real_device != NULL && diag_dxil_stages > 0u && diag_spirv_stages == 0u) {
                void **vtbl = *(void ***)self->real_device;
                if (is_stream_desc && patched_stream != NULL && vtbl[47] != NULL) {
                    struct {
                        uint64_t SizeInBytes;
                        void *pPipelineStateSubobjectStream;
                    } real_stream_desc;
                    real_stream_desc.SizeInBytes = (uint64_t)scan_bytes;
                    real_stream_desc.pPipelineStateSubobjectStream = patched_stream;
                    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_cps_t)(
                        void *, const void *, const scarlett_guid_t *, void **);
                    void *real_pso = NULL;
                    real_hr = ((pfn_real_cps_t)vtbl[47])(
                        self->real_device, &real_stream_desc, &kIidID3D12PipelineState, &real_pso);
                    if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                        pm->real_obj = real_pso;
                        if (pm->is_compute == 0u && found_real_rs != NULL && dxil_count >= 1u) {
                            pm->saved_real_rs = found_real_rs;
                            pm->saved_vs_dxbc = dxil_bufs[0];
                            pm->saved_vs_len = dxil_sizes[0];
                            dxil_bufs[0] = NULL;
                            if (dxil_count >= 2u) {
                                pm->saved_ps_dxbc = dxil_bufs[1];
                                pm->saved_ps_len = dxil_sizes[1];
                                dxil_bufs[1] = NULL;
                            }
                            pm->saved_owns_dxbc = 1u;
                        }
                    }
                    static uint32_t g_stream_diag_seq = 0u;
                    if (scarlett_failed(real_hr) &&
                        __atomic_add_fetch(&g_stream_diag_seq, 1u, __ATOMIC_RELAXED) <= 2u) {
                        char dbuf[512];
                        char *dp = dbuf;
                        char *dend = dbuf + sizeof(dbuf);
                        dp = d3d12xs_pe_append_str(dp, dend, "[scarlett-pso-stream] bytes=");
                        dp = d3d12xs_pe_append_u32(dp, dend, (uint32_t)scan_bytes);
                        dp = d3d12xs_pe_append_str(dp, dend, " rs=0x");
                        dp = d3d12xs_pe_append_hex_padded(dp, dend, (uint64_t)(uintptr_t)found_real_rs, 16);
                        dp = d3d12xs_pe_append_str(dp, dend, " q=");
                        for (size_t k = 0u; k < nq && k < 24u; ++k) {
                            if (k > 0u) dp = d3d12xs_pe_append_str(dp, dend, ",");
                            dp = d3d12xs_pe_append_hex_padded(dp, dend, ((const uint64_t *)patched_stream)[k], 16);
                        }
                        dp = d3d12xs_pe_append_str(dp, dend, "\n");
                        *dp = '\0';
                        d3d12xs_pe_ods(dbuf);
                        if (dxil_count > 0u && dxil_bufs[0] != NULL && dxil_sizes[0] >= 32u) {
                            const uint8_t *db = (const uint8_t *)dxil_bufs[0];
                            uint32_t cc = *(const uint32_t *)(db + 28u);
                            char cbuf[256];
                            char *cp = cbuf;
                            char *cend = cbuf + sizeof(cbuf);
                            cp = d3d12xs_pe_append_str(cp, cend, "[scarlett-dxbc-chunks] sz=");
                            cp = d3d12xs_pe_append_u32(cp, cend, (uint32_t)dxil_sizes[0]);
                            cp = d3d12xs_pe_append_str(cp, cend, " n=");
                            cp = d3d12xs_pe_append_u32(cp, cend, cc);
                            for (uint32_t ci = 0u; ci < cc && ci < 12u; ++ci) {
                                uint32_t coff = *(const uint32_t *)(db + 32u + ci * 4u);
                                if (coff + 8u <= dxil_sizes[0]) {
                                    uint32_t fcc = *(const uint32_t *)(db + coff);
                                    uint32_t csz = *(const uint32_t *)(db + coff + 4u);
                                    cp = d3d12xs_pe_append_str(cp, cend, " ");
                                    cp = d3d12xs_pe_append_hex_padded(cp, cend, fcc, 8);
                                    cp = d3d12xs_pe_append_str(cp, cend, ":");
                                    cp = d3d12xs_pe_append_u32(cp, cend, csz);
                                }
                            }
                            cp = d3d12xs_pe_append_str(cp, cend, "\n");
                            *cp = '\0';
                            d3d12xs_pe_ods(cbuf);
                        }
                    }
                } else if (pm->is_compute != 0u && dxil_count >= 1u && vtbl[11] != NULL) {
                    uint8_t pc_desc[56];
                    d3d12xs_pe_zero(pc_desc, sizeof(pc_desc));
                    *(void **)(pc_desc + 0x00u) = found_real_rs;
                    *(void **)(pc_desc + 0x08u) = dxil_bufs[0];
                    *(uint64_t *)(pc_desc + 0x10u) = dxil_sizes[0];
                    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccpso_t)(
                        void *, const void *, const scarlett_guid_t *, void **);
                    void *real_pso = NULL;
                    real_hr = ((pfn_real_ccpso_t)vtbl[11])(
                        self->real_device, pc_desc, &kIidID3D12PipelineState, &real_pso);
                    if (scarlett_succeeded(real_hr) && real_pso != NULL) {
                        pm->real_obj = real_pso;
                        if (pm->is_compute == 0u && found_real_rs != NULL && dxil_count >= 1u) {
                            pm->saved_real_rs = found_real_rs;
                            pm->saved_vs_dxbc = dxil_bufs[0];
                            pm->saved_vs_len = dxil_sizes[0];
                            dxil_bufs[0] = NULL;
                            if (dxil_count >= 2u) {
                                pm->saved_ps_dxbc = dxil_bufs[1];
                                pm->saved_ps_len = dxil_sizes[1];
                                dxil_bufs[1] = NULL;
                            }
                            pm->saved_owns_dxbc = 1u;
                        }
                    }
                }
            }

            (void)dxil_orig_ptrs;
            if (patched_stream != NULL) {
                d3d12xs_pe_free_mem(patched_stream);
            }
            for (size_t s = 0u; s < dxil_count; ++s) {
                if (dxil_bufs[s] != NULL) {
                    d3d12xs_pe_free_mem(dxil_bufs[s]);
                }
            }
        }
    }

    *ppPipelineState = (void *)pso;
    uint32_t slot_seq = 0u;
    {
        static uint32_t g_slot_log_counts[96];
        uint32_t sidx = (vtable_slot < 96u) ? vtable_slot : 95u;
        slot_seq = __atomic_add_fetch(&g_slot_log_counts[sidx], 1u, __ATOMIC_RELAXED);
    }
    if (seq <= 16u || slot_seq <= 2u || (vtable_slot == 86u && slot_seq <= 12u) || (seq & 1023u) == 0u) {
        char pbuf[512];
        char *pp = pbuf;
        char *pend = pbuf + sizeof(pbuf);
        pp = d3d12xs_pe_append_str(pp, pend, "[scarlett-pso] seq=");
        pp = d3d12xs_pe_append_u32(pp, pend, seq);
        pp = d3d12xs_pe_append_str(pp, pend, " slot=");
        pp = d3d12xs_pe_append_u32(pp, pend, vtable_slot);
        pp = d3d12xs_pe_append_str(pp, pend, " pDesc=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, (uint64_t)(uintptr_t)pDesc, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " extra=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, extra_info, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " magic0=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, diag_magic0, 8);
        pp = d3d12xs_pe_append_str(pp, pend, " fmagic=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, diag_found_magic, 8);
        pp = d3d12xs_pe_append_str(pp, pend, " q0=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, diag_q0, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " q1=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, diag_q1, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " q2=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, diag_q2, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " q3=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, diag_q3, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " lift=");
        pp = d3d12xs_pe_append_u32(pp, pend, diag_lifted_stages);
        pp = d3d12xs_pe_append_str(pp, pend, " dxil=");
        pp = d3d12xs_pe_append_u32(pp, pend, diag_dxil_stages);
        pp = d3d12xs_pe_append_str(pp, pend, " spirv=");
        pp = d3d12xs_pe_append_u32(pp, pend, diag_spirv_stages);
        pp = d3d12xs_pe_append_str(pp, pend, " spv_bytes=");
        pp = d3d12xs_pe_append_u32(pp, pend, pm->lifted_spirv_bytes);
        pp = d3d12xs_pe_append_str(pp, pend, " lhr=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, (uint32_t)diag_lift_hr, 8);
        pp = d3d12xs_pe_append_str(pp, pend, " real=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, (uint64_t)(uintptr_t)pm->real_obj, 16);
        pp = d3d12xs_pe_append_str(pp, pend, " hr=0x");
        pp = d3d12xs_pe_append_hex_padded(pp, pend, (uint32_t)real_hr, 8);
        pp = d3d12xs_pe_append_str(pp, pend, "\n");
        *pp = '\0';
        d3d12xs_pe_ods(pbuf);

        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreatePipelineState",
            vtable_slot,
            return_address,
            (uint64_t)(uintptr_t)self,
            (uint64_t)(uintptr_t)pDesc,
            extra_info,
            (uint64_t)(uintptr_t)pso,
            SCARLETT_OK,
            "(created)");
    }
    return SCARLETT_OK;
}

/* Slot 10 (+0x50): ID3D12Device::CreateGraphicsPipelineState */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot10_create_graphics_pso(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppPipelineState) {
    return com_create_pso_impl(
        self, pDesc, riid, ppPipelineState, 10u,
        (uint64_t)(uintptr_t)__builtin_return_address(0), 0u);
}

/* Slot 11 (+0x58): ID3D12Device::CreateComputePipelineState */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot11_create_compute_pso(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppPipelineState) {
    return com_create_pso_impl(
        self, pDesc, riid, ppPipelineState, 11u,
        (uint64_t)(uintptr_t)__builtin_return_address(0), 0u);
}

/* Slot 47 (+0x178): ID3D12Device2::CreatePipelineState */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot47_create_pipeline_state(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppPipelineState) {
    return com_create_pso_impl(
        self, pDesc, riid, ppPipelineState, 47u,
        (uint64_t)(uintptr_t)__builtin_return_address(0), 0u);
}

/* Slot 16 (+0x80): ID3D12Device::CreateRootSignature */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot16_create_root_signature(
    ID3D12DeviceX *self,
    uint32_t nodeMask,
    const void *pBlobWithRootSignature,
    uint64_t blobLengthInBytes,
    const scarlett_guid_t *riid,
    void **ppvRootSignature) {
    (void)nodeMask;
    if (self == NULL || ppvRootSignature == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvRootSignature = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12RootSignature) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateRootSignature",
            16u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }

    bool stripped = false;
    uint8_t scratch[2048];
    const void *real_blob = pBlobWithRootSignature;
    if (pBlobWithRootSignature != NULL && blobLengthInBytes >= 36u && blobLengthInBytes <= 2048u) {
        const uint8_t *src = (const uint8_t *)pBlobWithRootSignature;
        for (size_t i = 0u; i < (size_t)blobLengthInBytes; ++i) {
            scratch[i] = src[i];
        }
        stripped = d3d12xs_dxbc_strip_rts0_high_flags(scratch, (size_t)blobLengthInBytes);
        if (stripped) {
            d3d12xs_dxbc_recompute_md5(scratch, (size_t)blobLengthInBytes);
        }
        real_blob = scratch;
    }

    ID3D12RootSignatureX *rs = NULL;
    d3d12xs_root_sig_meta_t *rsm = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_ROOT_SIGS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_root_sig_meta[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            rs = &g_com_root_sigs[i];
            rsm = &g_root_sig_meta[i];
            break;
        }
    }
    if (rs == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreateRootSignature(OOM)",
            16u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)nodeMask,
            blobLengthInBytes,
            0u,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    rs->lpVtbl = &g_root_sig_vtbl;
    rsm->unix_device_handle = self->unix_device_handle;
    rsm->ref_count = 1u;
    rsm->in_use = 1u;
    rsm->real_obj = NULL;
    d3d12xs_parse_rts0_tables(
        rsm, (const uint8_t *)real_blob, (size_t)blobLengthInBytes);
    if (self->real_device != NULL && real_blob != NULL && blobLengthInBytes > 0u) {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_crs_t)(
            void *, uint32_t, const void *, size_t, const scarlett_guid_t *, void **);
        void **vtbl = *(void ***)self->real_device;
        pfn_real_crs_t fn = (pfn_real_crs_t)vtbl[16];
        void *real_rs = NULL;
        scarlett_hresult_t rhr = fn(
            self->real_device, nodeMask, real_blob, (size_t)blobLengthInBytes,
            &kIidID3D12RootSignature, &real_rs);
        if (scarlett_succeeded(rhr) && real_rs != NULL) {
            rsm->real_obj = real_rs;
        }
        static uint32_t g_vkd3d_rs_logged = 0u;
        if (__atomic_exchange_n(&g_vkd3d_rs_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
            d3d12xs_pe_vkd3d_log("CreateRootSignature", rsm->real_obj, rhr);
        }
    }
    *ppvRootSignature = (void *)rs;

    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "CreateRootSignature",
        16u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pBlobWithRootSignature,
        blobLengthInBytes,
        (uint64_t)(uintptr_t)rs,
        SCARLETT_OK,
        stripped ? "(created,rts0_stripped)" : "(created)");
    return SCARLETT_OK;
}

/* Slot 61 (+0x1e8): Xbox ID3D12DeviceX::CreateSampler(pDesc, DestDescriptor) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot61_create_sampler(
    ID3D12DeviceX *self,
    const void *pSamplerDesc,
    uint64_t DestDescriptor) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (pSamplerDesc == NULL || DestDescriptor == 0u) {
        static uint32_t g_slot61_null_logged = 0u;
        if (__atomic_fetch_add(&g_slot61_null_logged, 1u, __ATOMIC_RELAXED) < 8u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "ID3D12DeviceX::Slot61", 61u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pSamplerDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(null_arg)");
        }
        return SCARLETT_E_INVALIDARG;
    }

    uint32_t heap_type = 0u;
    uint64_t real_dest = d3d12xs_translate_cpu_desc_handle(DestDescriptor, &heap_type);
    if (real_dest == 0u && self->real_device != NULL) {
        static uint32_t g_slot61_bad_logged = 0u;
        if (__atomic_fetch_add(&g_slot61_bad_logged, 1u, __ATOMIC_RELAXED) < 8u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll", "ID3D12DeviceX::Slot61", 61u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pSamplerDesc,
                DestDescriptor, 0u, SCARLETT_E_INVALIDARG, "(bad_dest_desc)");
        }
        return SCARLETT_E_INVALIDARG;
    }

    uint32_t pc_sampler[13];
    d3d12xs_pe_zero(pc_sampler, sizeof(pc_sampler));
    pc_sampler[0] = 0x15u; /* D3D12_FILTER_MIN_MAG_MIP_LINEAR */
    pc_sampler[1] = 3u;    /* D3D12_TEXTURE_ADDRESS_MODE_CLAMP */
    pc_sampler[2] = 3u;
    pc_sampler[3] = 3u;
    pc_sampler[5] = 1u;    /* MaxAnisotropy */
    pc_sampler[6] = 1u;    /* D3D12_COMPARISON_FUNC_NEVER */
    pc_sampler[12] = 0x447A0000u; /* MaxLOD = 1000.0f */

    uint32_t d0 = 0u, d1 = 0u, d2 = 0u, d3 = 0u, d4 = 0u, d5 = 0u;
    if (d3d12xs_pe_is_readable(pSamplerDesc, 52u)) {
        d3d12xs_pe_memcpy(pc_sampler, pSamplerDesc, sizeof(pc_sampler));
        d0 = pc_sampler[0];
        d1 = pc_sampler[1];
        d2 = pc_sampler[2];
        d3 = pc_sampler[3];
        d4 = pc_sampler[4];
        d5 = pc_sampler[5];
        if ((pc_sampler[0] & ~0x1FFu) != 0u) {
            pc_sampler[0] = 0x15u;
        }
        for (size_t a = 1u; a <= 3u; ++a) {
            if (pc_sampler[a] < 1u || pc_sampler[a] > 5u) {
                pc_sampler[a] = 3u;
            }
        }
        if (pc_sampler[5] == 0u) {
            pc_sampler[5] = 1u;
        } else if (pc_sampler[5] > 16u) {
            pc_sampler[5] = 16u;
        }
        if (pc_sampler[6] < 1u || pc_sampler[6] > 8u) {
            pc_sampler[6] = 1u;
        }
    }

    if (self->real_device != NULL && real_dest != 0u) {
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_samp_t)(void *, const void *, uint64_t);
        void **vtbl = *(void ***)self->real_device;
        ((pfn_real_samp_t)vtbl[22])(self->real_device, pc_sampler, real_dest);
        d3d12xs_stamp_desc_cookie(DestDescriptor);
    }

    static uint32_t g_slot61_logged = 0u;
    uint32_t seq = __atomic_add_fetch(&g_slot61_logged, 1u, __ATOMIC_RELAXED);
    if (seq <= 8u || (seq & 255u) == 0u) {
        char sbuf[256];
        char *sp = sbuf;
        char *send = sbuf + sizeof(sbuf);
        sp = d3d12xs_pe_append_str(sp, send, "[scarlett-sampler] slot=61 seq=");
        sp = d3d12xs_pe_append_u32(sp, send, seq);
        sp = d3d12xs_pe_append_str(sp, send, " pDesc=0x");
        sp = d3d12xs_pe_append_hex_padded(sp, send, (uint64_t)(uintptr_t)pSamplerDesc, 16);
        sp = d3d12xs_pe_append_str(sp, send, " dst=0x");
        sp = d3d12xs_pe_append_hex_padded(sp, send, DestDescriptor, 16);
        sp = d3d12xs_pe_append_str(sp, send, " real=0x");
        sp = d3d12xs_pe_append_hex_padded(sp, send, real_dest, 16);
        sp = d3d12xs_pe_append_str(sp, send, " ht=");
        sp = d3d12xs_pe_append_u32(sp, send, heap_type);
        sp = d3d12xs_pe_append_str(sp, send, " w=");
        sp = d3d12xs_pe_append_hex_padded(sp, send, d0, 8);
        sp = d3d12xs_pe_append_str(sp, send, ",");
        sp = d3d12xs_pe_append_hex_padded(sp, send, d1, 8);
        sp = d3d12xs_pe_append_str(sp, send, ",");
        sp = d3d12xs_pe_append_hex_padded(sp, send, d2, 8);
        sp = d3d12xs_pe_append_str(sp, send, ",");
        sp = d3d12xs_pe_append_hex_padded(sp, send, d3, 8);
        sp = d3d12xs_pe_append_str(sp, send, ",");
        sp = d3d12xs_pe_append_hex_padded(sp, send, d4, 8);
        sp = d3d12xs_pe_append_str(sp, send, ",");
        sp = d3d12xs_pe_append_hex_padded(sp, send, d5, 8);
        sp = d3d12xs_pe_append_str(sp, send, "\n");
        *sp = '\0';
        d3d12xs_pe_ods(sbuf);

        if (seq == 1u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "ID3D12DeviceX::Slot61",
                61u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)self,
                (uint64_t)(uintptr_t)pSamplerDesc,
                DestDescriptor,
                real_dest,
                SCARLETT_OK,
                "(create_sampler)");
        }
    }
    return SCARLETT_OK;
}

/* Slot 62 (+0x1f0): Xbox ID3D12DeviceX::CreateComputePipelineState (2 extra words before riid) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot62_xbox_create_compute_pso(
    ID3D12DeviceX *self,
    const void *pDesc,
    uint64_t w2,
    uint64_t w3,
    const scarlett_guid_t *riid,
    void **ppState) {
    (void)w2;
    (void)w3;
    return com_create_pso_impl(
        self, pDesc, riid, ppState, 62u,
        (uint64_t)(uintptr_t)__builtin_return_address(0), w2);
}

/* Slot 67 (+0x218): Xbox ID3D12DeviceX::DeserializeComputePipelineStateX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot67_deserialize_compute_pso_x(
    ID3D12DeviceX *self,
    const void *pNativeDesc,
    const scarlett_guid_t *riid,
    void **ppState) {
    uint64_t shader_size = 0u;
    if (pNativeDesc != NULL && d3d12xs_pe_is_readable(pNativeDesc, 0x38u)) {
        shader_size = *(const uint64_t *)((const uint8_t *)pNativeDesc + 0x30u);
    }
    return com_create_pso_impl(
        self, pNativeDesc, riid, ppState, 67u,
        (uint64_t)(uintptr_t)__builtin_return_address(0), shader_size);
}

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot133_create_placed_resource_x(
    ID3D12DeviceX *self,
    uint64_t gpu_va,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource);

/* Slot 70 (+0x230): Xbox ID3D12DeviceX::CreatePlacedShaderResourceViewX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot70_create_descriptor_x(
    ID3D12DeviceX *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    static uint32_t g_slot70_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_slot70_seq, 1u, __ATOMIC_RELAXED);

    uint64_t xbox_va = 0u;
    uint32_t pc_srv[8];
    d3d12xs_pe_zero(pc_srv, sizeof(pc_srv));
    bool has_r8 = (r8 >= UINT64_C(0x10000) &&
                   d3d12xs_pe_is_readable((const void *)(uintptr_t)r8, 0x40u));
    if (has_r8) {
        d3d12xs_pe_memcpy(pc_srv, (const void *)(uintptr_t)r8, sizeof(pc_srv));
        xbox_va = *(const uint64_t *)(uintptr_t)(r8 + 0x28u);
    }

    uint64_t real_dest = (r9 != 0u) ? d3d12xs_translate_cpu_desc_handle(r9, NULL) : 0u;
    void *wrapper_res = NULL;
    d3d12xs_res_meta_t *rm = (xbox_va >= UINT64_C(0x10000))
        ? d3d12xs_find_res_meta_by_va(xbox_va, &wrapper_res)
        : NULL;

    if (rm == NULL && xbox_va >= UINT64_C(0x10000) &&
        rdx >= UINT64_C(0x10000) &&
        d3d12xs_pe_is_readable((const void *)(uintptr_t)rdx, sizeof(scarlett_d3d12_resource_desc_t))) {
        scarlett_d3d12_resource_desc_t rdesc;
        d3d12xs_pe_memcpy(&rdesc, (const void *)(uintptr_t)rdx, sizeof(rdesc));
        if (rdesc.Dimension != 1u && rdesc.Format == 0u && pc_srv[0] != 0u) {
            rdesc.Format = pc_srv[0];
        }
        void *auto_res = NULL;
        if (scarlett_succeeded(com_slot133_create_placed_resource_x(
                self, xbox_va, &rdesc, 0u, NULL, &kIidID3D12Resource, &auto_res)) &&
            auto_res != NULL) {
            rm = d3d12xs_get_res_meta(auto_res);
            wrapper_res = auto_res;
        }
    }

    bool forwarded = false;
    if (self->real_device != NULL && real_dest != 0u && has_r8 &&
        rm != NULL && rm->real_obj != NULL) {
        if (pc_srv[1] == 1u) { /* D3D12_SRV_DIMENSION_BUFFER */
            uint32_t fmt = pc_srv[0];
            uint32_t stride = pc_srv[7];
            uint32_t elem_bytes = (stride > 0u) ? stride
                : ((fmt == 2u) ? 16u : ((fmt == 6u || fmt == 10u || fmt == 16u) ? 8u
                    : ((fmt == 56u || fmt == 57u || fmt == 58u || fmt == 59u) ? 2u : 4u)));
            if (xbox_va >= rm->gpu_va) {
                uint64_t byte_off = xbox_va - rm->gpu_va;
                uint64_t first_elem = 0u;
                d3d12xs_pe_memcpy(&first_elem, &pc_srv[4], sizeof(first_elem));
                first_elem += byte_off / (uint64_t)elem_bytes;
                d3d12xs_pe_memcpy(&pc_srv[4], &first_elem, sizeof(first_elem));
                uint64_t byte_len = (uint64_t)pc_srv[6] * (uint64_t)elem_bytes;
                if (byte_len > 262144u) byte_len = 262144u;
                if (byte_len > 0u) {
                    d3d12xs_enqueue_cpu_upload_meta(rm, byte_off, byte_len);
                }
            }
            if (fmt == 39u && stride == 0u) {
                /* R32_TYPELESS buffer SRV requires D3D12_BUFFER_SRV_FLAG_RAW on PC */
                pc_srv[7] = 1u;
            } else {
                pc_srv[7] = 0u;
            }
        } else if (pc_srv[0] == 0u && rm->desc.Format != 0u) {
            pc_srv[0] = rm->desc.Format;
        }
        d3d12xs_record_desc_resource(r9, wrapper_res);
        typedef void (SCARLETT_D3D12XS_CALL *pfn_real_srv_t)(void *, void *, const void *, uint64_t);
        void **vtbl = *(void ***)self->real_device;
        ((pfn_real_srv_t)vtbl[18])(self->real_device, rm->real_obj, pc_srv, real_dest);
        d3d12xs_stamp_desc_cookie(r9);
        forwarded = true;
    }

    if (seq <= 16u || (seq % 250u) == 0u ||
        (rm != NULL && rm->desc.Dimension != 1u && pc_srv[1] == 1u)) {
        if (seq <= 8u && d3d12xs_pe_is_readable((const void *)(uintptr_t)rdx, 32u)) {
            const uint64_t *qw = (const uint64_t *)(uintptr_t)rdx;
            char sbuf[256];
            char *sp = sbuf;
            char *send = sbuf + sizeof(sbuf);
            sp = d3d12xs_pe_append_str(sp, send, "[scarlett-slot70] seq=");
            sp = d3d12xs_pe_append_u32(sp, send, seq);
            sp = d3d12xs_pe_append_str(sp, send, " rdx_q0=0x");
            sp = d3d12xs_pe_append_hex_padded(sp, send, qw[0], 16);
            sp = d3d12xs_pe_append_str(sp, send, " q1=0x");
            sp = d3d12xs_pe_append_hex_padded(sp, send, qw[1], 16);
            sp = d3d12xs_pe_append_str(sp, send, " q2=0x");
            sp = d3d12xs_pe_append_hex_padded(sp, send, qw[2], 16);
            sp = d3d12xs_pe_append_str(sp, send, " va=0x");
            sp = d3d12xs_pe_append_hex_padded(sp, send, xbox_va, 16);
            sp = d3d12xs_pe_append_str(sp, send, "\n");
            *sp = '\0';
            d3d12xs_pe_ods(sbuf);
        }
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12DeviceX::Slot70",
            70u,
            ((uint64_t)pc_srv[0] << 32) | pc_srv[1],
            ((uint64_t)pc_srv[2] << 32) | pc_srv[3],
            ((uint64_t)pc_srv[4] << 32) | pc_srv[5],
            ((uint64_t)pc_srv[6] << 32) | pc_srv[7],
            (rm != NULL) ? (((uint64_t)rm->desc.Dimension << 32) | rm->desc.Format) : 0u,
            SCARLETT_OK,
            forwarded ? "(srv_forwarded)" : "(skipped)");
    }
    return SCARLETT_OK;
}

/* ============================================================================
 * ID3D12QueryHeapX & ID3D12CommandSignatureX Static COM Pools
 * ============================================================================ */

typedef struct ID3D12QueryHeapXVtbl_S {
    void *slots[16];
} ID3D12QueryHeapXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12QueryHeapX_S {
    const ID3D12QueryHeapXVtbl *lpVtbl;
    uint64_t unix_device_handle;
    uint32_t ref_count;
    uint32_t in_use;
} ID3D12QueryHeapX;

#define SCARLETT_D3D12XS_MAX_COM_QUERY_HEAPS 256u
static ID3D12QueryHeapX g_com_query_heaps[SCARLETT_D3D12XS_MAX_COM_QUERY_HEAPS];

static scarlett_hresult_t SCARLETT_D3D12XS_CALL qh_query_interface(
    ID3D12QueryHeapX *self, const scarlett_guid_t *riid, void **ppvObject) {
    if (self == NULL || ppvObject == NULL) return SCARLETT_E_POINTER;
    *ppvObject = NULL;
    if (riid == NULL) return SCARLETT_E_INVALIDARG;
    if (d3d12xs_guid_eq(riid, &kIidID3D12QueryHeap) ||
        d3d12xs_guid_eq(riid, &kIidID3D12QueryHeap_Std) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL qh_add_ref(ID3D12QueryHeapX *self) {
    return (self != NULL) ? __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED) : 0u;
}

static uint32_t SCARLETT_D3D12XS_CALL qh_release(ID3D12QueryHeapX *self) {
    if (self == NULL || __atomic_load_n(&self->ref_count, __ATOMIC_RELAXED) == 0u) return 0u;
    uint32_t next = __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) __atomic_store_n(&self->in_use, 0u, __ATOMIC_RELEASE);
    return next;
}

D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 8, "8")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 9, "9")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(qh, "ID3D12QueryHeapX", 15, "15")

static const ID3D12QueryHeapXVtbl g_qh_vtbl = {
    {
        (void *)qh_query_interface, (void *)qh_add_ref, (void *)qh_release,
        (void *)qh_vtbl_slot_stub_3, (void *)qh_vtbl_slot_stub_4, (void *)qh_vtbl_slot_stub_5,
        (void *)qh_vtbl_slot_stub_6, (void *)qh_vtbl_slot_stub_7, (void *)qh_vtbl_slot_stub_8,
        (void *)qh_vtbl_slot_stub_9, (void *)qh_vtbl_slot_stub_10, (void *)qh_vtbl_slot_stub_11,
        (void *)qh_vtbl_slot_stub_12, (void *)qh_vtbl_slot_stub_13, (void *)qh_vtbl_slot_stub_14,
        (void *)qh_vtbl_slot_stub_15
    }
};

typedef struct ID3D12CommandSignatureXVtbl_S {
    void *slots[16];
} ID3D12CommandSignatureXVtbl;

typedef struct __attribute__((aligned(16))) ID3D12CommandSignatureX_S {
    const ID3D12CommandSignatureXVtbl *lpVtbl;
    uint64_t unix_device_handle;
    uint32_t ref_count;
    uint32_t in_use;
} ID3D12CommandSignatureX;

#define SCARLETT_D3D12XS_MAX_COM_CMD_SIGS 256u
static ID3D12CommandSignatureX g_com_cmd_sigs[SCARLETT_D3D12XS_MAX_COM_CMD_SIGS];

static scarlett_hresult_t SCARLETT_D3D12XS_CALL csig_query_interface(
    ID3D12CommandSignatureX *self, const scarlett_guid_t *riid, void **ppvObject) {
    if (self == NULL || ppvObject == NULL) return SCARLETT_E_POINTER;
    *ppvObject = NULL;
    if (riid == NULL) return SCARLETT_E_INVALIDARG;
    if (d3d12xs_guid_eq(riid, &kIidID3D12CommandSignature) ||
        d3d12xs_guid_eq(riid, &kIidID3D12CommandSignature_Std) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Pageable) ||
        d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) ||
        d3d12xs_guid_eq(riid, &kIidID3D12Object) ||
        d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED);
        *ppvObject = (void *)self;
        return SCARLETT_OK;
    }
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_D3D12XS_CALL csig_add_ref(ID3D12CommandSignatureX *self) {
    return (self != NULL) ? __atomic_add_fetch(&self->ref_count, 1u, __ATOMIC_RELAXED) : 0u;
}

static uint32_t SCARLETT_D3D12XS_CALL csig_release(ID3D12CommandSignatureX *self) {
    if (self == NULL || __atomic_load_n(&self->ref_count, __ATOMIC_RELAXED) == 0u) return 0u;
    uint32_t next = __atomic_sub_fetch(&self->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (next == 0u) __atomic_store_n(&self->in_use, 0u, __ATOMIC_RELEASE);
    return next;
}

D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 3, "3")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 4, "4")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 5, "5")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 6, "6")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 7, "7")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 8, "8")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 9, "9")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 10, "10")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 11, "11")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 12, "12")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 13, "13")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 14, "14")
D3D12XS_DEFINE_SLOT_STUB(csig, "ID3D12CommandSignatureX", 15, "15")

static const ID3D12CommandSignatureXVtbl g_csig_vtbl = {
    {
        (void *)csig_query_interface, (void *)csig_add_ref, (void *)csig_release,
        (void *)csig_vtbl_slot_stub_3, (void *)csig_vtbl_slot_stub_4, (void *)csig_vtbl_slot_stub_5,
        (void *)csig_vtbl_slot_stub_6, (void *)csig_vtbl_slot_stub_7, (void *)csig_vtbl_slot_stub_8,
        (void *)csig_vtbl_slot_stub_9, (void *)csig_vtbl_slot_stub_10, (void *)csig_vtbl_slot_stub_11,
        (void *)csig_vtbl_slot_stub_12, (void *)csig_vtbl_slot_stub_13, (void *)csig_vtbl_slot_stub_14,
        (void *)csig_vtbl_slot_stub_15
    }
};

/* Slot 39 (+0x138): ID3D12Device::CreateQueryHeap */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot39_create_query_heap(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppvHeap) {
    if (self == NULL || ppvHeap == NULL) return SCARLETT_E_POINTER;
    *ppvHeap = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12QueryHeap) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12QueryHeap_Std) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateQueryHeap", 39u,
            (uint64_t)(uintptr_t)__builtin_return_address(0), riid, SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    ID3D12QueryHeapX *qh = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_QUERY_HEAPS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_com_query_heaps[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            qh = &g_com_query_heaps[i];
            break;
        }
    }
    if (qh == NULL) return SCARLETT_E_OUTOFMEMORY;
    qh->lpVtbl = &g_qh_vtbl;
    qh->unix_device_handle = self->unix_device_handle;
    __atomic_store_n(&qh->ref_count, 1u, __ATOMIC_RELEASE);
    *ppvHeap = (void *)qh;
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll", "CreateQueryHeap", 39u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pDesc,
        (uint64_t)(uintptr_t)riid, (uint64_t)(uintptr_t)qh, SCARLETT_OK, "(created)");
    return SCARLETT_OK;
}

/* Slot 41 (+0x148): ID3D12Device::CreateCommandSignature */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot41_create_command_signature(
    ID3D12DeviceX *self,
    const void *pDesc,
    void *pRootSignature,
    const scarlett_guid_t *riid,
    void **ppvCommandSignature) {
    if (self == NULL || ppvCommandSignature == NULL) return SCARLETT_E_POINTER;
    *ppvCommandSignature = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12CommandSignature) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12CommandSignature_Std) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreateCommandSignature", 41u,
            (uint64_t)(uintptr_t)__builtin_return_address(0), riid, SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    ID3D12CommandSignatureX *csig = NULL;
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_CMD_SIGS; ++i) {
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_com_cmd_sigs[i].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            csig = &g_com_cmd_sigs[i];
            break;
        }
    }
    if (csig == NULL) return SCARLETT_E_OUTOFMEMORY;
    csig->lpVtbl = &g_csig_vtbl;
    csig->unix_device_handle = self->unix_device_handle;
    __atomic_store_n(&csig->ref_count, 1u, __ATOMIC_RELEASE);
    *ppvCommandSignature = (void *)csig;
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll", "CreateCommandSignature", 41u,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)self, (uint64_t)(uintptr_t)pDesc,
        (uint64_t)(uintptr_t)pRootSignature, (uint64_t)(uintptr_t)csig, SCARLETT_OK, "(created)");
    return SCARLETT_OK;
}

/* Slot 86 (+0x2b0): Xbox ID3D12DeviceX::CreatePipelineState / CreateStateObjectX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot86_create_state_object_x(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppStateObject) {
    if (riid == NULL || d3d12xs_guid_eq(riid, &kIidID3D12PipelineState)) {
        return com_create_pso_impl(
            self, pDesc, riid, ppStateObject, 86u,
            (uint64_t)(uintptr_t)__builtin_return_address(0), 0u);
    }
    return com_slot57_create_state_object(self, pDesc, riid, ppStateObject);
}

/* Slot 127 (+0x3f8): Xbox ID3D12DeviceX::CreateCommittedResourceX (11 args) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot133_create_placed_resource_x(
    ID3D12DeviceX *self,
    uint64_t gpu_va,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource);

static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot127_device_handler(
    ID3D12DeviceX *self,
    const void *pHeapProperties,
    uint32_t HeapFlags,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialState,
    const void *pOptimizedClearValue,
    uint64_t arg7,
    uint64_t arg8,
    uint64_t arg9,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    (void)pHeapProperties;
    (void)HeapFlags;
    (void)arg7;
    (void)arg8;
    (void)arg9;
    return com_slot133_create_placed_resource_x(
        self, 0u, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
}

/* Slot 74 (+0x250): Xbox ID3D12DeviceX::RegisterGpuContext / RingBuffer (gpu_va, size, pOutHandle) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot74_register_gpu_context(
    ID3D12DeviceX *self,
    uint64_t gpu_va,
    uint64_t size,
    uint64_t *pOutContextHandle) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (pOutContextHandle != NULL) {
        *pOutContextHandle = gpu_va ? gpu_va : 1u;
    }
    static uint32_t g_slot74_logged = 0u;
    if (__atomic_exchange_n(&g_slot74_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "ID3D12DeviceX::Slot74",
            74u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            gpu_va,
            size,
            (uint64_t)(uintptr_t)pOutContextHandle,
            SCARLETT_OK,
            "(gpu_context_ok)");
    }
    return SCARLETT_OK;
}

/* Slot 75 (+0x258): Xbox ID3D12DeviceX::CreateCommandQueue1 */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot75_create_command_queue1(
    ID3D12DeviceX *self,
    const void *pDesc,
    const scarlett_guid_t *CreatorID,
    const scarlett_guid_t *riid,
    void **ppCommandQueue) {
    (void)CreatorID;
    return com_create_command_queue_impl(
        self, pDesc, riid, ppCommandQueue, 75u,
        (uint64_t)(uintptr_t)__builtin_return_address(0));
}

/* Slot 55 (+0x1b8): ID3D12Device4::CreateCommandList1 / Xbox PipelineState */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot55_create_command_list1(
    ID3D12DeviceX *self,
    uint64_t arg2,
    uint64_t arg3,
    uint64_t arg4,
    const scarlett_guid_t *riid,
    void **ppCommandList) {
    if (riid != NULL && d3d12xs_guid_eq(riid, &kIidID3D12PipelineState)) {
        d3d12xs_slot55_extra_t s55;
        s55.magic = SCARLETT_D3D12XS_SLOT55_MAGIC;
        s55.flags = arg3;
        s55.override_ptr = arg4;
        return com_create_pso_impl(
            self,
            (const void *)(uintptr_t)arg2,
            riid,
            ppCommandList,
            55u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)&s55);
    }
    return com_slot12_create_command_list(
        self, (uint32_t)arg2, (uint32_t)arg3, NULL, NULL, riid, ppCommandList);
}

/* Slot 80 (+0x280): Xbox ID3D12DeviceX::WaitFrameEventX (6 args) */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot80_wait_frame_event_x(
    ID3D12DeviceX *self,
    uint32_t FrameEventType,
    uint64_t TimeoutInMilliseconds,
    const D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC *pTokenDesc,
    uint64_t flags,
    uint64_t *pOutEventTimestamp) {
    (void)flags;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_d3d12xs_wait_frame_params_t out_wait;
    d3d12xs_pe_zero(&out_wait, sizeof(out_wait));
    uint64_t expected_token =
        (pTokenDesc != NULL && (uintptr_t)pTokenDesc > 0x10000u)
            ? pTokenDesc->Token
            : 0u;
    scarlett_hresult_t hr = D3D12XboxWaitFrameEventX(
        self->unix_device_handle,
        FrameEventType,
        TimeoutInMilliseconds,
        expected_token,
        &out_wait);
    if (pOutEventTimestamp != NULL && (uintptr_t)pOutEventTimestamp > 0x10000u) {
        *pOutEventTimestamp = out_wait.out_token ? out_wait.out_token : 1u;
    }
    if (hr != SCARLETT_OK) {
        hr = SCARLETT_OK;
    }
    static uint32_t g_wait_frame_log_count = 0u;
    uint32_t seq = __atomic_add_fetch(&g_wait_frame_log_count, 1u, __ATOMIC_RELAXED);
    if (seq <= 8u || (seq % 120u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "WaitFrameEventX",
            80u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            (uint64_t)FrameEventType,
            TimeoutInMilliseconds,
            (uint64_t)(uintptr_t)pOutEventTimestamp,
            SCARLETT_OK,
            "(ok)");
    }
    return SCARLETT_OK;
}

/* Slot 81 (+0x288): Xbox ID3D12DeviceX::GetFrameStatisticsX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot81_get_frame_statistics_x(
    ID3D12DeviceX *self,
    uint64_t token,
    uint32_t type_mask,
    uint32_t *pCount,
    void *pOutStats) {
    (void)self;
    (void)type_mask;
    if (pCount == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (*pCount == 0u || pOutStats == NULL) {
        *pCount = 1u;
        return SCARLETT_OK;
    }
    uint8_t *entry = (uint8_t *)pOutStats;
    d3d12xs_pe_zero(entry, 0x38);
    *(uint32_t *)(entry + 0x00) = 0x10000u; /* Display type */
    *(uint32_t *)(entry + 0x04) = 0u;       /* Plane index */
    *(uint64_t *)(entry + 0x08) = 0u;       /* byte at +0x8 == 0 */
    *(uint64_t *)(entry + 0x10) = token ? token : 1u;
    *(uint64_t *)(entry + 0x18) = d3d12xs_pe_get_qpc_timestamp(0); /* timestamp */
    *pCount = 1u;
    static uint32_t g_stats_log = 0u;
    uint32_t seq = __atomic_add_fetch(&g_stats_log, 1u, __ATOMIC_RELAXED);
    if (seq <= 8u || (seq % 120u) == 0u) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "GetFrameStatisticsX",
            81u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)token,
            type_mask,
            (uint64_t)*pCount,
            (uint64_t)seq,
            SCARLETT_OK,
            "(retired)");
    }
    return SCARLETT_OK;
}

static bool d3d12xs_is_depth_or_typeless_format(uint32_t fmt) {
    switch (fmt) {
        case 19u: /* DXGI_FORMAT_R32G8X24_TYPELESS */
        case 20u: /* DXGI_FORMAT_D32_FLOAT_S8X24_UINT */
        case 39u: /* DXGI_FORMAT_R32_TYPELESS */
        case 40u: /* DXGI_FORMAT_D32_FLOAT */
        case 44u: /* DXGI_FORMAT_R24G8_TYPELESS */
        case 45u: /* DXGI_FORMAT_D24_UNORM_S8_UINT */
        case 54u: /* DXGI_FORMAT_R16_TYPELESS */
        case 55u: /* DXGI_FORMAT_D16_UNORM */
            return true;
        default:
            return false;
    }
}

static void d3d12xs_sanitize_pc_resource_desc(
    scarlett_d3d12_resource_desc_t *pc_desc,
    uint32_t pass) {
    pc_desc->Alignment = 0u;
    pc_desc->Flags &= 0x3Fu;
    if (pc_desc->Dimension == 1u) {
        pc_desc->Layout = 1u; /* D3D12_TEXTURE_LAYOUT_ROW_MAJOR */
        pc_desc->Format = 0u;
        pc_desc->Height = 1u;
        pc_desc->DepthOrArraySize = 1u;
        pc_desc->MipLevels = 1u;
        pc_desc->SampleCount = 1u;
        pc_desc->SampleQuality = 0u;
        /* Buffers only allow ALLOW_UNORDERED_ACCESS (0x04) in PC D3D12 */
        pc_desc->Flags &= 0x04u;
        if (pass >= 2u) {
            pc_desc->Flags = 0u;
        }
        return;
    }

    pc_desc->Layout = 0u; /* D3D12_TEXTURE_LAYOUT_UNKNOWN */
    if (pc_desc->Width == 0u) {
        pc_desc->Width = 1u;
    }
    if (pc_desc->Height == 0u) {
        pc_desc->Height = 1u;
    }
    if (pc_desc->DepthOrArraySize == 0u) {
        pc_desc->DepthOrArraySize = 1u;
    }
    if (pc_desc->MipLevels == 0u) {
        pc_desc->MipLevels = 1u;
    }
    if (pc_desc->SampleCount == 0u || pass >= 1u) {
        pc_desc->SampleCount = 1u;
    }
    if (pc_desc->SampleCount <= 1u) {
        pc_desc->SampleQuality = 0u;
    } else {
        pc_desc->MipLevels = 1u;
        pc_desc->Flags &= ~(0x04u | 0x20u); /* No UAV / SIMULTANEOUS_ACCESS with MSAA */
    }
    if (pc_desc->Dimension == 4u) { /* TEXTURE3D */
        pc_desc->Flags &= ~(0x02u | 0x20u);
        pc_desc->SampleCount = 1u;
        pc_desc->SampleQuality = 0u;
    }

    /* Always strip CROSS_ADAPTER (0x10) and SIMULTANEOUS_ACCESS (0x20) if incompatible */
    pc_desc->Flags &= ~0x10u;
    if ((pc_desc->Flags & 0x02u) != 0u) { /* ALLOW_DEPTH_STENCIL */
        pc_desc->Flags &= ~(0x01u | 0x04u | 0x20u); /* No RT / UAV / SIMULTANEOUS with DS */
        if (pass >= 1u && !d3d12xs_is_depth_or_typeless_format(pc_desc->Format)) {
            pc_desc->Flags &= ~0x02u;
        }
    }
    if ((pc_desc->Flags & 0x02u) == 0u) {
        pc_desc->Flags &= ~0x08u; /* DENY_SHADER_RESOURCE requires ALLOW_DEPTH_STENCIL */
    }
    if (pass >= 1u) {
        pc_desc->Flags &= ~(0x08u | 0x20u);
    }
    if (pass >= 2u) {
        pc_desc->Flags = 0u;
        pc_desc->MipLevels = 1u;
    }
}

/* Slot 133 (+0x428): Xbox ID3D12DeviceX::CreatePlacedResourceX */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot133_create_placed_resource_x(
    ID3D12DeviceX *self,
    uint64_t gpu_va,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    (void)pOptimizedClearValue;
    if (self == NULL || pDesc == NULL || ppvResource == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppvResource = NULL;
    if (riid != NULL &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Resource) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Pageable) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12DeviceChild) &&
        !d3d12xs_guid_eq(riid, &kIidID3D12Object) &&
        !d3d12xs_guid_eq(riid, &kIidIUnknown)) {
        d3d12xs_pe_log_guid_ods(
            "CreatePlacedResourceX",
            133u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            riid,
            SCARLETT_E_NOINTERFACE);
        return SCARLETT_E_NOINTERFACE;
    }
    if (!d3d12xs_ensure_resource_pool()) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    static uint32_t g_com_resources_cursor = 0u;
    ID3D12ResourceX *res = NULL;
    d3d12xs_res_meta_t *rm = NULL;
    size_t res_idx = 0u;
    uint32_t start_idx = __atomic_fetch_add(&g_com_resources_cursor, 1u, __ATOMIC_RELAXED);
    for (size_t i = 0u; i < SCARLETT_D3D12XS_MAX_COM_RESOURCES; ++i) {
        size_t idx = ((size_t)start_idx + i) % SCARLETT_D3D12XS_MAX_COM_RESOURCES;
        uint32_t expected = 0u;
        if (__atomic_compare_exchange_n(
                &g_res_meta[idx].in_use, &expected, 1u,
                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            res = &g_com_resources[idx];
            rm = &g_res_meta[idx];
            res_idx = idx;
            break;
        }
    }
    if (res == NULL || rm == NULL) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "CreatePlacedResourceX(OOM)",
            133u,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)self,
            gpu_va,
            (uint64_t)pDesc->Dimension,
            pDesc->Width,
            SCARLETT_E_OUTOFMEMORY,
            NULL);
        return SCARLETT_E_OUTOFMEMORY;
    }
    {
        size_t want_hw = res_idx + 1u;
        size_t cur_hw = __atomic_load_n(&g_res_high_water, __ATOMIC_RELAXED);
        while (want_hw > cur_hw &&
               !__atomic_compare_exchange_n(
                   &g_res_high_water, &cur_hw, want_hw,
                   false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        }
    }

    uint64_t effective_va = gpu_va;
    uint32_t owns_va = 0u;
    uint64_t calc_sz = d3d12xs_resource_calc_size(pDesc);
    if (calc_sz < 65536u) {
        calc_sz = 65536u;
    }
    uint64_t alloc_sz = (calc_sz + 65535u) & ~UINT64_C(65535);
#if defined(_WIN32) || defined(__MINGW32__)
    if (effective_va == 0u) {
        d3d12xs_pe_resolve_kernel32();
        scarlett_virtual_alloc_fn_t va =
            __atomic_load_n(&g_pfn_VirtualAlloc, __ATOMIC_ACQUIRE);
        if (va != NULL) {
            void *p = va(NULL, (size_t)alloc_sz, 0x3000u, 0x04u);
            if (p != NULL) {
                effective_va = (uint64_t)(uintptr_t)p;
                owns_va = 1u;
            }
        }
    }
#endif

    D3D12XBOX_RESOURCE_DESC xdesc;
    d3d12xs_pe_zero(&xdesc, sizeof(xdesc));
    xdesc.Dimension = pDesc->Dimension;
    xdesc.Width = (uint32_t)pDesc->Width;
    xdesc.Height = pDesc->Height;
    xdesc.DepthOrArraySize = (uint32_t)pDesc->DepthOrArraySize;
    xdesc.MipLevels = (uint32_t)pDesc->MipLevels;
    xdesc.Format = pDesc->Format;
    xdesc.Flags = pDesc->Flags;
    xdesc.ByteSize = alloc_sz;

    uint64_t unix_res_handle = 0u;
    uint32_t uma_tier = 0u;
    (void)D3D12XboxCreatePlacedResourceX(
        self->unix_device_handle,
        effective_va,
        &xdesc,
        InitialState,
        &unix_res_handle,
        &uma_tier);

    void *real_res = NULL;
    uint64_t real_gpu_va = 0u;
    scarlett_hresult_t real_hr = SCARLETT_OK;
    uint32_t final_pc_flags = 0u;
    if (self->real_device != NULL && alloc_sz <= (UINT64_C(512) << 20) &&
        pDesc->Dimension >= 1u && pDesc->Dimension <= 4u &&
        (pDesc->Dimension == 1u || pDesc->Format != 0u)) {
        struct {
            uint32_t Type;
            uint32_t CPUPageProperty;
            uint32_t MemoryPoolPreference;
            uint32_t CreationNodeMask;
            uint32_t VisibleNodeMask;
        } hp;
        hp.Type = 1u; /* D3D12_HEAP_TYPE_DEFAULT */
        hp.CPUPageProperty = 0u;
        hp.MemoryPoolPreference = 0u;
        hp.CreationNodeMask = 1u;
        hp.VisibleNodeMask = 1u;
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_real_ccr_t)(
            void *, const void *, uint32_t, const void *, uint32_t, const void *,
            const scarlett_guid_t *, void **);
        void **vtbl = *(void ***)self->real_device;

        for (uint32_t pass = 0u; pass < 3u; ++pass) {
            scarlett_d3d12_resource_desc_t pc_desc;
            d3d12xs_pe_memcpy(&pc_desc, pDesc, sizeof(pc_desc));
            d3d12xs_sanitize_pc_resource_desc(&pc_desc, pass);
            if (pc_desc.Dimension == 1u && pc_desc.Width == 0u) {
                pc_desc.Width = alloc_sz;
            }
            final_pc_flags = pc_desc.Flags;
            real_res = NULL;
            real_hr = ((pfn_real_ccr_t)vtbl[27])(
                self->real_device, &hp, 0u /* D3D12_HEAP_FLAG_NONE */,
                &pc_desc, 0u /* D3D12_RESOURCE_STATE_COMMON */,
                NULL, &kIidID3D12Resource, &real_res);
            if (scarlett_succeeded(real_hr) && real_res != NULL) {
                typedef uint64_t (SCARLETT_D3D12XS_CALL *pfn_real_va_t)(void *);
                void **rvtbl = *(void ***)real_res;
                real_gpu_va = ((pfn_real_va_t)rvtbl[11])(real_res);
                break;
            }
            real_res = NULL;
        }

        static uint32_t g_vkd3d_res_logged = 0u;
        if (__atomic_exchange_n(&g_vkd3d_res_logged, 1u, __ATOMIC_ACQ_REL) == 0u) {
            d3d12xs_pe_vkd3d_log("CreateCommittedResource", real_res, real_hr);
        }
    } else if (self->real_device != NULL) {
        real_hr = SCARLETT_E_INVALIDARG;
    }

    if (self->real_device != NULL && real_res == NULL) {
        static uint32_t g_cpr_fail_seq = 0u;
        uint32_t fseq = __atomic_add_fetch(&g_cpr_fail_seq, 1u, __ATOMIC_RELAXED);
        if (fseq <= 16u || (fseq & 63u) == 0u) {
            char fbuf[256];
            char *fp = fbuf;
            char *fend = fbuf + sizeof(fbuf);
            fp = d3d12xs_pe_append_str(fp, fend, "[scarlett-res-fail] seq=");
            fp = d3d12xs_pe_append_u32(fp, fend, fseq);
            fp = d3d12xs_pe_append_str(fp, fend, " res=0x");
            fp = d3d12xs_pe_append_hex_padded(fp, fend, (uint64_t)(uintptr_t)res, 16);
            fp = d3d12xs_pe_append_str(fp, fend, " dim=");
            fp = d3d12xs_pe_append_u32(fp, fend, pDesc->Dimension);
            fp = d3d12xs_pe_append_str(fp, fend, " w=");
            fp = d3d12xs_pe_append_u32(fp, fend, (uint32_t)pDesc->Width);
            fp = d3d12xs_pe_append_str(fp, fend, " h=");
            fp = d3d12xs_pe_append_u32(fp, fend, pDesc->Height);
            fp = d3d12xs_pe_append_str(fp, fend, " fmt=");
            fp = d3d12xs_pe_append_u32(fp, fend, pDesc->Format);
            fp = d3d12xs_pe_append_str(fp, fend, " orig_flags=0x");
            fp = d3d12xs_pe_append_hex_padded(fp, fend, pDesc->Flags, 4);
            fp = d3d12xs_pe_append_str(fp, fend, " pc_flags=0x");
            fp = d3d12xs_pe_append_hex_padded(fp, fend, final_pc_flags, 4);
            fp = d3d12xs_pe_append_str(fp, fend, " sc=");
            fp = d3d12xs_pe_append_u32(fp, fend, pDesc->SampleCount);
            fp = d3d12xs_pe_append_str(fp, fend, " sz=0x");
            fp = d3d12xs_pe_append_hex_padded(fp, fend, alloc_sz, 12);
            fp = d3d12xs_pe_append_str(fp, fend, " hr=0x");
            fp = d3d12xs_pe_append_hex_padded(fp, fend, (uint32_t)real_hr, 8);
            fp = d3d12xs_pe_append_str(fp, fend, "\n");
            *fp = '\0';
            d3d12xs_pe_ods(fbuf);
        }
    }

    res->lpVtbl = &g_res_vtbl;
    rm->unix_device_handle = self->unix_device_handle;
    rm->unix_resource_handle = unix_res_handle;
    rm->gpu_va = effective_va;
    rm->alloc_size = alloc_sz;
    rm->real_gpu_va = real_gpu_va;
    rm->owns_va = owns_va;
    rm->gpu_dirty = 0u;
    rm->current_state = InitialState;
    rm->real_obj = real_res;
    rm->owner_device = self;
    d3d12xs_pe_memcpy(&rm->desc, pDesc, sizeof(rm->desc));
    rm->ref_count = 1u;
    __atomic_store_n(&rm->in_use, 1u, __ATOMIC_RELEASE);
    *ppvResource = (void *)res;

    {
        static uint32_t g_cpr_seq = 0u;
        static uint32_t g_cpr_zero_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_cpr_seq, 1u, __ATOMIC_RELAXED);
        uint32_t zseq = (gpu_va == 0u) ? __atomic_add_fetch(&g_cpr_zero_seq, 1u, __ATOMIC_RELAXED) : 0u;
        if (seq <= 16u ||
            (gpu_va == 0u && zseq <= 16u) ||
            alloc_sz >= (UINT64_C(8) << 20) ||
            (seq % 250u) == 0u) {
            d3d12xs_pe_log_stub_ods(
                "d3d12_xs.dll",
                "CreatePlacedResourceX",
                133u,
                (uint64_t)(uintptr_t)__builtin_return_address(0),
                (uint64_t)(uintptr_t)res,
                effective_va,
                alloc_sz,
                ((uint64_t)pDesc->Dimension << 32) | (uint32_t)pDesc->Width,
                SCARLETT_OK,
                (owns_va != 0u) ? "(created-va0)" : "(created)");
        }
    }
    return SCARLETT_OK;
}

/* Slot 27 (+0xd8): ID3D12Device::CreateCommittedResource */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot27_create_committed_resource(
    ID3D12DeviceX *self,
    const void *pHeapProperties,
    uint32_t HeapFlags,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialResourceState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    (void)pHeapProperties;
    (void)HeapFlags;
    return com_slot133_create_placed_resource_x(
        self, 0u, pDesc, InitialResourceState, pOptimizedClearValue, riid, ppvResource);
}

/* Slot 29 (+0xe8): ID3D12Device::CreatePlacedResource */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot29_create_placed_resource(
    ID3D12DeviceX *self,
    void *pHeap,
    uint64_t HeapOffset,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    (void)pHeap;
    return com_slot133_create_placed_resource_x(
        self, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
}

/* Slot 76 (+0x260): Xbox ID3D12DeviceX::CreatePlacedResourceX / Resource */
static scarlett_hresult_t SCARLETT_D3D12XS_CALL com_slot76_create_placed_resource_x(
    ID3D12DeviceX *self,
    uint64_t gpu_va,
    const scarlett_d3d12_resource_desc_t *pDesc,
    uint32_t InitialState,
    const void *pOptimizedClearValue,
    const scarlett_guid_t *riid,
    void **ppvResource) {
    return com_slot133_create_placed_resource_x(
        self, gpu_va, pDesc, InitialState, pOptimizedClearValue, riid, ppvResource);
}

/* Slot 38 (+0x130): ID3D12Device::GetCopyableFootprints */
static void SCARLETT_D3D12XS_CALL com_slot38_get_copyable_footprints(
    ID3D12DeviceX *self,
    const scarlett_d3d12_resource_desc_t *pResourceDesc,
    uint32_t FirstSubresource,
    uint32_t NumSubresources,
    uint64_t BaseOffset,
    scarlett_d3d12_placed_subresource_footprint_t *pLayouts,
    uint32_t *pNumRows,
    uint64_t *pRowSizeInBytes,
    uint64_t *pTotalBytes) {
    (void)self;
    if (pResourceDesc == NULL) {
        return;
    }
    uint64_t offset = BaseOffset;
    uint64_t total = 0u;
    uint32_t mip_levels = pResourceDesc->MipLevels > 0u ? pResourceDesc->MipLevels : 1u;
    for (uint32_t i = 0u; i < NumSubresources; ++i) {
        uint32_t sub = FirstSubresource + i;
        uint32_t mip = sub % mip_levels;
        uint32_t w = (uint32_t)(pResourceDesc->Width > 0u ? pResourceDesc->Width : 1u) >> mip;
        uint32_t h = (pResourceDesc->Height > 0u ? pResourceDesc->Height : 1u) >> mip;
        uint32_t d = (pResourceDesc->Dimension == 4u)
            ? ((pResourceDesc->DepthOrArraySize > 0u ? pResourceDesc->DepthOrArraySize : 1u) >> mip)
            : 1u;
        if (w == 0u) w = 1u;
        if (h == 0u) h = 1u;
        if (d == 0u) d = 1u;

        uint32_t row_bytes = 0u;
        uint32_t num_rows = 0u;
        if (pResourceDesc->Dimension == 1u) {
            row_bytes = (uint32_t)pResourceDesc->Width;
            num_rows = 1u;
        } else {
            d3d12xs_format_layout(pResourceDesc->Format, w, h, &row_bytes, &num_rows);
        }
        uint32_t row_pitch = (row_bytes + 255u) & ~255u;
        offset = (offset + 511u) & ~UINT64_C(511);

        if (pLayouts != NULL) {
            pLayouts[i].Offset = offset;
            pLayouts[i].Footprint.Format = pResourceDesc->Format;
            pLayouts[i].Footprint.Width = w;
            pLayouts[i].Footprint.Height = h;
            pLayouts[i].Footprint.Depth = d;
            pLayouts[i].Footprint.RowPitch = row_pitch;
        }
        if (pNumRows != NULL) {
            pNumRows[i] = num_rows;
        }
        if (pRowSizeInBytes != NULL) {
            pRowSizeInBytes[i] = (uint64_t)row_bytes;
        }
        uint64_t sub_size = (num_rows > 0u)
            ? ((uint64_t)row_pitch * (uint64_t)(num_rows - 1u) + (uint64_t)row_bytes)
            : 0u;
        if (d > 1u) {
            sub_size = (uint64_t)row_pitch * (uint64_t)num_rows * (uint64_t)(d - 1u) + sub_size;
        }
        total = (offset - BaseOffset) + sub_size;
        offset += (uint64_t)row_pitch * (uint64_t)num_rows * (uint64_t)d;
    }
    if (pTotalBytes != NULL) {
        *pTotalBytes = total;
    }
}

/* Slot 25 (+0xc8): ID3D12Device::GetResourceAllocationInfo (MSVC x64 struct-return ABI) */
typedef struct scarlett_d3d12_res_alloc_info_s {
    uint64_t SizeInBytes;
    uint64_t Alignment;
} scarlett_d3d12_res_alloc_info_t;

static void * SCARLETT_D3D12XS_CALL com_slot25_get_resource_allocation_info(
    void *arg1,
    void *arg2,
    uint32_t visibleMask,
    uint32_t numResourceDescs,
    const scarlett_d3d12_resource_desc_t *pResourceDescs) {
    (void)visibleMask;
    scarlett_d3d12_res_alloc_info_t *out_info = d3d12xs_is_com_device(arg1)
        ? (scarlett_d3d12_res_alloc_info_t *)arg2
        : (scarlett_d3d12_res_alloc_info_t *)arg1;
    if (out_info != NULL) {
        uint64_t total_size = 0u;
        if (pResourceDescs != NULL && numResourceDescs > 0u && numResourceDescs <= 64u) {
            for (uint32_t i = 0u; i < numResourceDescs; ++i) {
                const scarlett_d3d12_resource_desc_t *d = &pResourceDescs[i];
                uint64_t sz = (d->Dimension == 1u)
                    ? d->Width
                    : (d->Width * (uint64_t)(d->Height != 0u ? d->Height : 1u) * 4u);
                if (sz < 65536u) {
                    sz = 65536u;
                }
                sz = (sz + 65535u) & ~UINT64_C(65535);
                total_size += sz;
            }
        } else {
            total_size = 65536u;
        }
        out_info->SizeInBytes = total_size;
        out_info->Alignment = 65536u;
    }
    return (void *)out_info;
}

/* Slot 108 (+0x360): ID3D12Device4::GetResourceAllocationInfo1 (MSVC x64 struct-return ABI) */
static void * SCARLETT_D3D12XS_CALL com_slot108_get_resource_allocation_info1(
    void *arg1,
    void *arg2,
    uint32_t visibleMask,
    uint32_t numResourceDescs,
    const scarlett_d3d12_resource_desc_t *pResourceDescs) {
    return com_slot25_get_resource_allocation_info(
        arg1, arg2, visibleMask, numResourceDescs, pResourceDescs);
}

/* Slot 26 (+0xd0): ID3D12Device::GetCustomHeapProperties (MSVC x64 struct-return ABI) */
typedef struct scarlett_d3d12_heap_properties_s {
    uint32_t Type;
    uint32_t CPUPageProperty;
    uint32_t MemoryPoolPreference;
    uint32_t CreationNodeMask;
    uint32_t VisibleNodeMask;
} scarlett_d3d12_heap_properties_t;

static void * SCARLETT_D3D12XS_CALL com_get_custom_heap_properties(
    void *arg1,
    void *arg2,
    uint32_t nodeMask,
    uint32_t heapType) {
    scarlett_d3d12_heap_properties_t *out_props = d3d12xs_is_com_device(arg1)
        ? (scarlett_d3d12_heap_properties_t *)arg2
        : (scarlett_d3d12_heap_properties_t *)arg1;
    if (out_props != NULL) {
        uint32_t mask = (nodeMask != 0u) ? nodeMask : 1u;
        out_props->Type = 4u; /* D3D12_HEAP_TYPE_CUSTOM */
        if (heapType == 2u) {
            out_props->CPUPageProperty = 2u; /* WRITE_COMBINE */
        } else if (heapType == 3u) {
            out_props->CPUPageProperty = 3u; /* WRITE_BACK */
        } else {
            out_props->CPUPageProperty = 1u; /* NOT_AVAILABLE */
        }
        out_props->MemoryPoolPreference = 1u; /* D3D12_MEMORY_POOL_L0 (UMA) */
        out_props->CreationNodeMask = mask;
        out_props->VisibleNodeMask = mask;
    }
    return (void *)out_props;
}

#define D3D12XS_DEFINE_VTBL_SLOT_STUB(SLOT_NUM, SLOT_STR)                      \
    static scarlett_hresult_t SCARLETT_D3D12XS_CALL                            \
        com_vtbl_slot_stub_##SLOT_NUM(                                         \
            uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9) {            \
        static const scarlett_stub_descriptor_t kDesc = {                      \
            SCARLETT_STUB_THUNK_ABI_VERSION,                                   \
            SCARLETT_STUB_FLAG_XDL_VTABLE,                                     \
            SCARLETT_STUB_ORDINAL_NONE,                                        \
            (uint32_t)(SLOT_NUM),                                              \
            SCARLETT_ERR_UNIMPLEMENTED_STUB,                                   \
            0u,                                                                \
            "d3d12_xs.dll",                                                    \
            "ID3D12DeviceX::Slot" SLOT_STR                                     \
        };                                                                     \
        return d3d12xs_pe_dispatch_stub(                                       \
            &kDesc,                                                            \
            (uint64_t)(uintptr_t)__builtin_return_address(0),                  \
            rcx, rdx, r8, r9);                                                 \
    }

D3D12XS_DEFINE_VTBL_SLOT_STUB(13, "13")
D3D12XS_DEFINE_VTBL_SLOT_STUB(28, "28")
D3D12XS_DEFINE_VTBL_SLOT_STUB(30, "30")
D3D12XS_DEFINE_VTBL_SLOT_STUB(31, "31")
D3D12XS_DEFINE_VTBL_SLOT_STUB(32, "32")
D3D12XS_DEFINE_VTBL_SLOT_STUB(33, "33")
D3D12XS_DEFINE_VTBL_SLOT_STUB(34, "34")
D3D12XS_DEFINE_VTBL_SLOT_STUB(35, "35")
D3D12XS_DEFINE_VTBL_SLOT_STUB(40, "40")
D3D12XS_DEFINE_VTBL_SLOT_STUB(42, "42")
D3D12XS_DEFINE_VTBL_SLOT_STUB(44, "44")
D3D12XS_DEFINE_VTBL_SLOT_STUB(45, "45")
D3D12XS_DEFINE_VTBL_SLOT_STUB(46, "46")
D3D12XS_DEFINE_VTBL_SLOT_STUB(49, "49")
D3D12XS_DEFINE_VTBL_SLOT_STUB(50, "50")
D3D12XS_DEFINE_VTBL_SLOT_STUB(51, "51")
D3D12XS_DEFINE_VTBL_SLOT_STUB(52, "52")
D3D12XS_DEFINE_VTBL_SLOT_STUB(53, "53")
D3D12XS_DEFINE_VTBL_SLOT_STUB(54, "54")
D3D12XS_DEFINE_VTBL_SLOT_STUB(56, "56")
D3D12XS_DEFINE_VTBL_SLOT_STUB(58, "58")
D3D12XS_DEFINE_VTBL_SLOT_STUB(59, "59")
D3D12XS_DEFINE_VTBL_SLOT_STUB(60, "60")
D3D12XS_DEFINE_VTBL_SLOT_STUB(63, "63")
D3D12XS_DEFINE_VTBL_SLOT_STUB(64, "64")
D3D12XS_DEFINE_VTBL_SLOT_STUB(65, "65")
D3D12XS_DEFINE_VTBL_SLOT_STUB(66, "66")
D3D12XS_DEFINE_VTBL_SLOT_STUB(68, "68")
D3D12XS_DEFINE_VTBL_SLOT_STUB(69, "69")
D3D12XS_DEFINE_VTBL_SLOT_STUB(71, "71")
D3D12XS_DEFINE_VTBL_SLOT_STUB(72, "72")
D3D12XS_DEFINE_VTBL_SLOT_STUB(77, "77")
D3D12XS_DEFINE_VTBL_SLOT_STUB(82, "82")
D3D12XS_DEFINE_VTBL_SLOT_STUB(83, "83")
D3D12XS_DEFINE_VTBL_SLOT_STUB(84, "84")
D3D12XS_DEFINE_VTBL_SLOT_STUB(85, "85")
D3D12XS_DEFINE_VTBL_SLOT_STUB(87, "87")
D3D12XS_DEFINE_VTBL_SLOT_STUB(88, "88")
D3D12XS_DEFINE_VTBL_SLOT_STUB(89, "89")
D3D12XS_DEFINE_VTBL_SLOT_STUB(90, "90")
D3D12XS_DEFINE_VTBL_SLOT_STUB(91, "91")
D3D12XS_DEFINE_VTBL_SLOT_STUB(92, "92")
D3D12XS_DEFINE_VTBL_SLOT_STUB(93, "93")
D3D12XS_DEFINE_VTBL_SLOT_STUB(94, "94")
D3D12XS_DEFINE_VTBL_SLOT_STUB(95, "95")
D3D12XS_DEFINE_VTBL_SLOT_STUB(96, "96")
D3D12XS_DEFINE_VTBL_SLOT_STUB(97, "97")
D3D12XS_DEFINE_VTBL_SLOT_STUB(98, "98")
D3D12XS_DEFINE_VTBL_SLOT_STUB(99, "99")
D3D12XS_DEFINE_VTBL_SLOT_STUB(100, "100")
D3D12XS_DEFINE_VTBL_SLOT_STUB(101, "101")
D3D12XS_DEFINE_VTBL_SLOT_STUB(102, "102")
D3D12XS_DEFINE_VTBL_SLOT_STUB(103, "103")
D3D12XS_DEFINE_VTBL_SLOT_STUB(104, "104")
D3D12XS_DEFINE_VTBL_SLOT_STUB(105, "105")
D3D12XS_DEFINE_VTBL_SLOT_STUB(106, "106")
D3D12XS_DEFINE_VTBL_SLOT_STUB(107, "107")
D3D12XS_DEFINE_VTBL_SLOT_STUB(109, "109")
D3D12XS_DEFINE_VTBL_SLOT_STUB(110, "110")
D3D12XS_DEFINE_VTBL_SLOT_STUB(111, "111")
D3D12XS_DEFINE_VTBL_SLOT_STUB(112, "112")
D3D12XS_DEFINE_VTBL_SLOT_STUB(113, "113")
D3D12XS_DEFINE_VTBL_SLOT_STUB(114, "114")
D3D12XS_DEFINE_VTBL_SLOT_STUB(115, "115")
D3D12XS_DEFINE_VTBL_SLOT_STUB(116, "116")
D3D12XS_DEFINE_VTBL_SLOT_STUB(117, "117")
D3D12XS_DEFINE_VTBL_SLOT_STUB(118, "118")
D3D12XS_DEFINE_VTBL_SLOT_STUB(119, "119")
D3D12XS_DEFINE_VTBL_SLOT_STUB(120, "120")
D3D12XS_DEFINE_VTBL_SLOT_STUB(121, "121")
D3D12XS_DEFINE_VTBL_SLOT_STUB(122, "122")
D3D12XS_DEFINE_VTBL_SLOT_STUB(123, "123")
D3D12XS_DEFINE_VTBL_SLOT_STUB(124, "124")
D3D12XS_DEFINE_VTBL_SLOT_STUB(125, "125")
D3D12XS_DEFINE_VTBL_SLOT_STUB(126, "126")
D3D12XS_DEFINE_VTBL_SLOT_STUB(128, "128")
D3D12XS_DEFINE_VTBL_SLOT_STUB(129, "129")
D3D12XS_DEFINE_VTBL_SLOT_STUB(130, "130")
D3D12XS_DEFINE_VTBL_SLOT_STUB(131, "131")
D3D12XS_DEFINE_VTBL_SLOT_STUB(132, "132")
D3D12XS_DEFINE_VTBL_SLOT_STUB(134, "134")
D3D12XS_DEFINE_VTBL_SLOT_STUB(135, "135")
D3D12XS_DEFINE_VTBL_SLOT_STUB(136, "136")
D3D12XS_DEFINE_VTBL_SLOT_STUB(137, "137")
D3D12XS_DEFINE_VTBL_SLOT_STUB(138, "138")
D3D12XS_DEFINE_VTBL_SLOT_STUB(139, "139")
D3D12XS_DEFINE_VTBL_SLOT_STUB(140, "140")
D3D12XS_DEFINE_VTBL_SLOT_STUB(141, "141")
D3D12XS_DEFINE_VTBL_SLOT_STUB(142, "142")
D3D12XS_DEFINE_VTBL_SLOT_STUB(143, "143")
D3D12XS_DEFINE_VTBL_SLOT_STUB(144, "144")
D3D12XS_DEFINE_VTBL_SLOT_STUB(145, "145")
D3D12XS_DEFINE_VTBL_SLOT_STUB(146, "146")
D3D12XS_DEFINE_VTBL_SLOT_STUB(147, "147")
D3D12XS_DEFINE_VTBL_SLOT_STUB(148, "148")
D3D12XS_DEFINE_VTBL_SLOT_STUB(149, "149")
D3D12XS_DEFINE_VTBL_SLOT_STUB(150, "150")
D3D12XS_DEFINE_VTBL_SLOT_STUB(151, "151")
D3D12XS_DEFINE_VTBL_SLOT_STUB(152, "152")
D3D12XS_DEFINE_VTBL_SLOT_STUB(153, "153")
D3D12XS_DEFINE_VTBL_SLOT_STUB(154, "154")
D3D12XS_DEFINE_VTBL_SLOT_STUB(155, "155")
D3D12XS_DEFINE_VTBL_SLOT_STUB(156, "156")
D3D12XS_DEFINE_VTBL_SLOT_STUB(157, "157")
D3D12XS_DEFINE_VTBL_SLOT_STUB(158, "158")
D3D12XS_DEFINE_VTBL_SLOT_STUB(159, "159")
D3D12XS_DEFINE_VTBL_SLOT_STUB(160, "160")
D3D12XS_DEFINE_VTBL_SLOT_STUB(161, "161")
D3D12XS_DEFINE_VTBL_SLOT_STUB(162, "162")
D3D12XS_DEFINE_VTBL_SLOT_STUB(163, "163")
D3D12XS_DEFINE_VTBL_SLOT_STUB(164, "164")
D3D12XS_DEFINE_VTBL_SLOT_STUB(165, "165")
D3D12XS_DEFINE_VTBL_SLOT_STUB(166, "166")
D3D12XS_DEFINE_VTBL_SLOT_STUB(167, "167")
D3D12XS_DEFINE_VTBL_SLOT_STUB(168, "168")
D3D12XS_DEFINE_VTBL_SLOT_STUB(169, "169")
D3D12XS_DEFINE_VTBL_SLOT_STUB(170, "170")
D3D12XS_DEFINE_VTBL_SLOT_STUB(171, "171")
D3D12XS_DEFINE_VTBL_SLOT_STUB(172, "172")
D3D12XS_DEFINE_VTBL_SLOT_STUB(173, "173")
D3D12XS_DEFINE_VTBL_SLOT_STUB(174, "174")
D3D12XS_DEFINE_VTBL_SLOT_STUB(175, "175")
D3D12XS_DEFINE_VTBL_SLOT_STUB(176, "176")
D3D12XS_DEFINE_VTBL_SLOT_STUB(177, "177")
D3D12XS_DEFINE_VTBL_SLOT_STUB(178, "178")
D3D12XS_DEFINE_VTBL_SLOT_STUB(179, "179")
D3D12XS_DEFINE_VTBL_SLOT_STUB(180, "180")
D3D12XS_DEFINE_VTBL_SLOT_STUB(181, "181")
D3D12XS_DEFINE_VTBL_SLOT_STUB(182, "182")
D3D12XS_DEFINE_VTBL_SLOT_STUB(183, "183")
D3D12XS_DEFINE_VTBL_SLOT_STUB(184, "184")
D3D12XS_DEFINE_VTBL_SLOT_STUB(185, "185")
D3D12XS_DEFINE_VTBL_SLOT_STUB(186, "186")
D3D12XS_DEFINE_VTBL_SLOT_STUB(187, "187")
D3D12XS_DEFINE_VTBL_SLOT_STUB(188, "188")
D3D12XS_DEFINE_VTBL_SLOT_STUB(189, "189")
D3D12XS_DEFINE_VTBL_SLOT_STUB(190, "190")
D3D12XS_DEFINE_VTBL_SLOT_STUB(191, "191")
D3D12XS_DEFINE_VTBL_SLOT_STUB(192, "192")
D3D12XS_DEFINE_VTBL_SLOT_STUB(193, "193")
D3D12XS_DEFINE_VTBL_SLOT_STUB(194, "194")
D3D12XS_DEFINE_VTBL_SLOT_STUB(195, "195")
D3D12XS_DEFINE_VTBL_SLOT_STUB(196, "196")
D3D12XS_DEFINE_VTBL_SLOT_STUB(197, "197")
D3D12XS_DEFINE_VTBL_SLOT_STUB(198, "198")
D3D12XS_DEFINE_VTBL_SLOT_STUB(199, "199")
D3D12XS_DEFINE_VTBL_SLOT_STUB(200, "200")
D3D12XS_DEFINE_VTBL_SLOT_STUB(201, "201")
D3D12XS_DEFINE_VTBL_SLOT_STUB(202, "202")
D3D12XS_DEFINE_VTBL_SLOT_STUB(203, "203")
D3D12XS_DEFINE_VTBL_SLOT_STUB(204, "204")
D3D12XS_DEFINE_VTBL_SLOT_STUB(205, "205")
D3D12XS_DEFINE_VTBL_SLOT_STUB(206, "206")
D3D12XS_DEFINE_VTBL_SLOT_STUB(207, "207")
D3D12XS_DEFINE_VTBL_SLOT_STUB(208, "208")
D3D12XS_DEFINE_VTBL_SLOT_STUB(209, "209")
D3D12XS_DEFINE_VTBL_SLOT_STUB(210, "210")
D3D12XS_DEFINE_VTBL_SLOT_STUB(211, "211")
D3D12XS_DEFINE_VTBL_SLOT_STUB(212, "212")
D3D12XS_DEFINE_VTBL_SLOT_STUB(213, "213")
D3D12XS_DEFINE_VTBL_SLOT_STUB(214, "214")
D3D12XS_DEFINE_VTBL_SLOT_STUB(215, "215")
D3D12XS_DEFINE_VTBL_SLOT_STUB(216, "216")
D3D12XS_DEFINE_VTBL_SLOT_STUB(217, "217")
D3D12XS_DEFINE_VTBL_SLOT_STUB(218, "218")
D3D12XS_DEFINE_VTBL_SLOT_STUB(219, "219")
D3D12XS_DEFINE_VTBL_SLOT_STUB(220, "220")
D3D12XS_DEFINE_VTBL_SLOT_STUB(221, "221")
D3D12XS_DEFINE_VTBL_SLOT_STUB(222, "222")
D3D12XS_DEFINE_VTBL_SLOT_STUB(223, "223")
D3D12XS_DEFINE_VTBL_SLOT_STUB(224, "224")
D3D12XS_DEFINE_VTBL_SLOT_STUB(225, "225")
D3D12XS_DEFINE_VTBL_SLOT_STUB(226, "226")
D3D12XS_DEFINE_VTBL_SLOT_STUB(227, "227")
D3D12XS_DEFINE_VTBL_SLOT_STUB(228, "228")
D3D12XS_DEFINE_VTBL_SLOT_STUB(229, "229")
D3D12XS_DEFINE_VTBL_SLOT_STUB(230, "230")
D3D12XS_DEFINE_VTBL_SLOT_STUB(231, "231")
D3D12XS_DEFINE_VTBL_SLOT_STUB(232, "232")
D3D12XS_DEFINE_VTBL_SLOT_STUB(233, "233")
D3D12XS_DEFINE_VTBL_SLOT_STUB(234, "234")
D3D12XS_DEFINE_VTBL_SLOT_STUB(235, "235")
D3D12XS_DEFINE_VTBL_SLOT_STUB(236, "236")
D3D12XS_DEFINE_VTBL_SLOT_STUB(237, "237")
D3D12XS_DEFINE_VTBL_SLOT_STUB(238, "238")
D3D12XS_DEFINE_VTBL_SLOT_STUB(239, "239")
D3D12XS_DEFINE_VTBL_SLOT_STUB(240, "240")
D3D12XS_DEFINE_VTBL_SLOT_STUB(241, "241")
D3D12XS_DEFINE_VTBL_SLOT_STUB(242, "242")
D3D12XS_DEFINE_VTBL_SLOT_STUB(243, "243")
D3D12XS_DEFINE_VTBL_SLOT_STUB(244, "244")
D3D12XS_DEFINE_VTBL_SLOT_STUB(245, "245")
D3D12XS_DEFINE_VTBL_SLOT_STUB(246, "246")
D3D12XS_DEFINE_VTBL_SLOT_STUB(247, "247")
D3D12XS_DEFINE_VTBL_SLOT_STUB(248, "248")
D3D12XS_DEFINE_VTBL_SLOT_STUB(249, "249")
D3D12XS_DEFINE_VTBL_SLOT_STUB(250, "250")
D3D12XS_DEFINE_VTBL_SLOT_STUB(251, "251")
D3D12XS_DEFINE_VTBL_SLOT_STUB(252, "252")
D3D12XS_DEFINE_VTBL_SLOT_STUB(253, "253")
D3D12XS_DEFINE_VTBL_SLOT_STUB(254, "254")
D3D12XS_DEFINE_VTBL_SLOT_STUB(255, "255")

static const ID3D12DeviceXVtbl g_device_vtbl = {
    com_query_interface,
    com_add_ref,
    com_release,
    com_create_placed_resource_x,
    com_present_x,
    com_wait_frame_event_x,
    com_set_amp_shader_limits,
#if defined(_WIN32) || defined(__MINGW32__)
    (scarlett_hresult_t (SCARLETT_D3D12XS_CALL *)(ID3D12DeviceX *, uint64_t, uint32_t))
        (void *)com_slot7_get_node_count,
#else
    com_legacy_present_sync_x_stub,
#endif
    (scarlett_hresult_t (SCARLETT_D3D12XS_CALL *)(
        ID3D12DeviceX *, const void *, uint32_t, const scarlett_guid_t *, void **))
        (void *)com_slot8_create_command_queue,
    {
        (void *)com_slot9_create_command_allocator, /* slot 9 (+0x48) */
        (void *)com_slot10_create_graphics_pso, /* slot 10 (+0x50) */
        (void *)com_slot11_create_compute_pso, /* slot 11 (+0x58) */
        (void *)com_slot12_create_command_list, /* slot 12 (+0x60) */
        (void *)com_vtbl_slot_stub_13,
        (void *)com_slot14_create_descriptor_heap, /* slot 14 (+0x70) */
        (void *)com_slot15_get_descriptor_handle_increment_size, /* slot 15 (+0x78) */
        (void *)com_slot16_create_root_signature, /* slot 16 (+0x80) */
        (void *)com_slot17_create_cbv,              /* slot 17 (+0x88) */
        (void *)com_slot18_create_srv,              /* slot 18 (+0x90) */
        (void *)com_slot19_create_uav,              /* slot 19 (+0x98) */
        (void *)com_slot20_create_rtv,              /* slot 20 (+0xa0) */
        (void *)com_slot21_create_dsv,              /* slot 21 (+0xa8) */
        (void *)com_slot22_create_sampler,          /* slot 22 (+0xb0) */
        (void *)com_slot23_copy_descriptors,        /* slot 23 (+0xb8) */
        (void *)com_slot24_copy_descriptors_simple, /* slot 24 (+0xc0) */
        (void *)com_slot25_get_resource_allocation_info, /* slot 25 (+0xc8) */
        (void *)com_get_custom_heap_properties, /* slot 26 (+0xd0) */
        (void *)com_slot27_create_committed_resource, /* slot 27 (+0xd8) */
        (void *)com_vtbl_slot_stub_28,
        (void *)com_slot29_create_placed_resource, /* slot 29 (+0xe8) */
        (void *)com_vtbl_slot_stub_30,
        (void *)com_vtbl_slot_stub_31,
        (void *)com_vtbl_slot_stub_32,
        (void *)com_vtbl_slot_stub_33,
        (void *)com_vtbl_slot_stub_34,
        (void *)com_vtbl_slot_stub_35,
        (void *)com_slot36_create_fence, /* slot 36 (+0x120) */
        (void *)com_slot37_get_device_removed_reason, /* slot 37 (+0x128) */
        (void *)com_slot38_get_copyable_footprints, /* slot 38 (+0x130) */
        (void *)com_slot39_create_query_heap,       /* slot 39 (+0x138) */
        (void *)com_vtbl_slot_stub_40,
        (void *)com_slot41_create_command_signature,/* slot 41 (+0x148) */
        (void *)com_vtbl_slot_stub_42,
        (void *)com_slot43_get_adapter_luid, /* slot 43 (+0x158) */
        (void *)com_vtbl_slot_stub_44,
        (void *)com_vtbl_slot_stub_45,
        (void *)com_vtbl_slot_stub_46,
        (void *)com_slot47_create_pipeline_state, /* slot 47 (+0x178) */
        (void *)com_slot48_create_command_queue, /* slot 48 (+0x180) */
        (void *)com_vtbl_slot_stub_49,
        (void *)com_vtbl_slot_stub_50,
        (void *)com_vtbl_slot_stub_51,
        (void *)com_vtbl_slot_stub_52,
        (void *)com_vtbl_slot_stub_53,
        (void *)com_vtbl_slot_stub_54,
        (void *)com_slot55_create_command_list1, /* slot 55 (+0x1b8) */
        (void *)com_vtbl_slot_stub_56,
        (void *)com_slot57_create_state_object, /* slot 57 (+0x1c8) */
        (void *)com_vtbl_slot_stub_58,
        (void *)com_vtbl_slot_stub_59,
        (void *)com_vtbl_slot_stub_60,
        (void *)com_slot61_create_sampler,          /* slot 61 (+0x1e8) */
        (void *)com_slot62_xbox_create_compute_pso, /* slot 62 (+0x1f0) */
        (void *)com_vtbl_slot_stub_63,
        (void *)com_vtbl_slot_stub_64,
        (void *)com_vtbl_slot_stub_65,
        (void *)com_vtbl_slot_stub_66,
        (void *)com_slot67_deserialize_compute_pso_x, /* slot 67 (+0x218) */
        (void *)com_vtbl_slot_stub_68,
        (void *)com_vtbl_slot_stub_69,
        (void *)com_slot70_create_descriptor_x, /* slot 70 (+0x230) */
        (void *)com_vtbl_slot_stub_71,
        (void *)com_vtbl_slot_stub_72,
        (void *)com_slot73_create_command_list, /* slot 73 (+0x248) */
        (void *)com_slot74_register_gpu_context, /* slot 74 (+0x250) */
        (void *)com_slot75_create_command_queue1, /* slot 75 (+0x258) */
        (void *)com_slot76_create_placed_resource_x, /* slot 76 (+0x260) */
        (void *)com_vtbl_slot_stub_77,
        (void *)com_slot78_set_frame_interval_x, /* slot 78 (+0x270) */
        (void *)com_slot79_set_frame_interval, /* slot 79 (+0x278) */
        (void *)com_slot80_wait_frame_event_x, /* slot 80 (+0x280) */
        (void *)com_slot81_get_frame_statistics_x, /* slot 81 (+0x288) */
        (void *)com_vtbl_slot_stub_82,
        (void *)com_vtbl_slot_stub_83,
        (void *)com_vtbl_slot_stub_84,
        (void *)com_vtbl_slot_stub_85,
        (void *)com_slot86_create_state_object_x, /* slot 86 (+0x2b0) */
        (void *)com_vtbl_slot_stub_87,
        (void *)com_vtbl_slot_stub_88,
        (void *)com_vtbl_slot_stub_89,
        (void *)com_vtbl_slot_stub_90,
        (void *)com_vtbl_slot_stub_91,
        (void *)com_vtbl_slot_stub_92,
        (void *)com_vtbl_slot_stub_93,
        (void *)com_vtbl_slot_stub_94,
        (void *)com_vtbl_slot_stub_95,
        (void *)com_vtbl_slot_stub_96,
        (void *)com_vtbl_slot_stub_97,
        (void *)com_vtbl_slot_stub_98,
        (void *)com_vtbl_slot_stub_99,
        (void *)com_vtbl_slot_stub_100,
        (void *)com_vtbl_slot_stub_101,
        (void *)com_vtbl_slot_stub_102,
        (void *)com_vtbl_slot_stub_103,
        (void *)com_vtbl_slot_stub_104,
        (void *)com_vtbl_slot_stub_105,
        (void *)com_vtbl_slot_stub_106,
        (void *)com_vtbl_slot_stub_107,
        (void *)com_slot108_get_resource_allocation_info1, /* slot 108 (+0x360) */
        (void *)com_vtbl_slot_stub_109,
        (void *)com_vtbl_slot_stub_110,
        (void *)com_vtbl_slot_stub_111,
        (void *)com_vtbl_slot_stub_112,
        (void *)com_vtbl_slot_stub_113,
        (void *)com_vtbl_slot_stub_114,
        (void *)com_vtbl_slot_stub_115,
        (void *)com_vtbl_slot_stub_116,
        (void *)com_vtbl_slot_stub_117,
        (void *)com_vtbl_slot_stub_118,
        (void *)com_vtbl_slot_stub_119,
        (void *)com_vtbl_slot_stub_120,
        (void *)com_vtbl_slot_stub_121,
        (void *)com_vtbl_slot_stub_122,
        (void *)com_vtbl_slot_stub_123,
        (void *)com_vtbl_slot_stub_124,
        (void *)com_vtbl_slot_stub_125,
        (void *)com_vtbl_slot_stub_126,
        (void *)com_slot127_device_handler, /* slot 127 (+0x3f8) */
        (void *)com_vtbl_slot_stub_128,
        (void *)com_vtbl_slot_stub_129,
        (void *)com_vtbl_slot_stub_130,
        (void *)com_vtbl_slot_stub_131,
        (void *)com_vtbl_slot_stub_132,
        (void *)com_slot133_create_placed_resource_x, /* slot 133 (+0x428) */
        (void *)com_vtbl_slot_stub_134,
        (void *)com_vtbl_slot_stub_135,
        (void *)com_vtbl_slot_stub_136,
        (void *)com_vtbl_slot_stub_137,
        (void *)com_vtbl_slot_stub_138,
        (void *)com_vtbl_slot_stub_139,
        (void *)com_vtbl_slot_stub_140,
        (void *)com_vtbl_slot_stub_141,
        (void *)com_vtbl_slot_stub_142,
        (void *)com_vtbl_slot_stub_143,
        (void *)com_vtbl_slot_stub_144,
        (void *)com_vtbl_slot_stub_145,
        (void *)com_vtbl_slot_stub_146,
        (void *)com_vtbl_slot_stub_147,
        (void *)com_vtbl_slot_stub_148,
        (void *)com_vtbl_slot_stub_149,
        (void *)com_vtbl_slot_stub_150,
        (void *)com_vtbl_slot_stub_151,
        (void *)com_vtbl_slot_stub_152,
        (void *)com_vtbl_slot_stub_153,
        (void *)com_vtbl_slot_stub_154,
        (void *)com_vtbl_slot_stub_155,
        (void *)com_vtbl_slot_stub_156,
        (void *)com_vtbl_slot_stub_157,
        (void *)com_vtbl_slot_stub_158,
        (void *)com_vtbl_slot_stub_159,
        (void *)com_vtbl_slot_stub_160,
        (void *)com_vtbl_slot_stub_161,
        (void *)com_vtbl_slot_stub_162,
        (void *)com_vtbl_slot_stub_163,
        (void *)com_vtbl_slot_stub_164,
        (void *)com_vtbl_slot_stub_165,
        (void *)com_vtbl_slot_stub_166,
        (void *)com_vtbl_slot_stub_167,
        (void *)com_vtbl_slot_stub_168,
        (void *)com_vtbl_slot_stub_169,
        (void *)com_vtbl_slot_stub_170,
        (void *)com_vtbl_slot_stub_171,
        (void *)com_vtbl_slot_stub_172,
        (void *)com_vtbl_slot_stub_173,
        (void *)com_vtbl_slot_stub_174,
        (void *)com_vtbl_slot_stub_175,
        (void *)com_vtbl_slot_stub_176,
        (void *)com_vtbl_slot_stub_177,
        (void *)com_vtbl_slot_stub_178,
        (void *)com_vtbl_slot_stub_179,
        (void *)com_vtbl_slot_stub_180,
        (void *)com_vtbl_slot_stub_181,
        (void *)com_vtbl_slot_stub_182,
        (void *)com_vtbl_slot_stub_183,
        (void *)com_vtbl_slot_stub_184,
        (void *)com_vtbl_slot_stub_185,
        (void *)com_vtbl_slot_stub_186,
        (void *)com_vtbl_slot_stub_187,
        (void *)com_vtbl_slot_stub_188,
        (void *)com_vtbl_slot_stub_189,
        (void *)com_vtbl_slot_stub_190,
        (void *)com_vtbl_slot_stub_191,
        (void *)com_vtbl_slot_stub_192,
        (void *)com_vtbl_slot_stub_193,
        (void *)com_vtbl_slot_stub_194,
        (void *)com_vtbl_slot_stub_195,
        (void *)com_vtbl_slot_stub_196,
        (void *)com_vtbl_slot_stub_197,
        (void *)com_vtbl_slot_stub_198,
        (void *)com_vtbl_slot_stub_199,
        (void *)com_vtbl_slot_stub_200,
        (void *)com_vtbl_slot_stub_201,
        (void *)com_vtbl_slot_stub_202,
        (void *)com_vtbl_slot_stub_203,
        (void *)com_vtbl_slot_stub_204,
        (void *)com_vtbl_slot_stub_205,
        (void *)com_vtbl_slot_stub_206,
        (void *)com_vtbl_slot_stub_207,
        (void *)com_vtbl_slot_stub_208,
        (void *)com_vtbl_slot_stub_209,
        (void *)com_vtbl_slot_stub_210,
        (void *)com_vtbl_slot_stub_211,
        (void *)com_vtbl_slot_stub_212,
        (void *)com_vtbl_slot_stub_213,
        (void *)com_vtbl_slot_stub_214,
        (void *)com_vtbl_slot_stub_215,
        (void *)com_vtbl_slot_stub_216,
        (void *)com_vtbl_slot_stub_217,
        (void *)com_vtbl_slot_stub_218,
        (void *)com_vtbl_slot_stub_219,
        (void *)com_vtbl_slot_stub_220,
        (void *)com_vtbl_slot_stub_221,
        (void *)com_vtbl_slot_stub_222,
        (void *)com_vtbl_slot_stub_223,
        (void *)com_vtbl_slot_stub_224,
        (void *)com_vtbl_slot_stub_225,
        (void *)com_vtbl_slot_stub_226,
        (void *)com_vtbl_slot_stub_227,
        (void *)com_vtbl_slot_stub_228,
        (void *)com_vtbl_slot_stub_229,
        (void *)com_vtbl_slot_stub_230,
        (void *)com_vtbl_slot_stub_231,
        (void *)com_vtbl_slot_stub_232,
        (void *)com_vtbl_slot_stub_233,
        (void *)com_vtbl_slot_stub_234,
        (void *)com_vtbl_slot_stub_235,
        (void *)com_vtbl_slot_stub_236,
        (void *)com_vtbl_slot_stub_237,
        (void *)com_vtbl_slot_stub_238,
        (void *)com_vtbl_slot_stub_239,
        (void *)com_vtbl_slot_stub_240,
        (void *)com_vtbl_slot_stub_241,
        (void *)com_vtbl_slot_stub_242,
        (void *)com_vtbl_slot_stub_243,
        (void *)com_vtbl_slot_stub_244,
        (void *)com_vtbl_slot_stub_245,
        (void *)com_vtbl_slot_stub_246,
        (void *)com_vtbl_slot_stub_247,
        (void *)com_vtbl_slot_stub_248,
        (void *)com_vtbl_slot_stub_249,
        (void *)com_vtbl_slot_stub_250,
        (void *)com_vtbl_slot_stub_251,
        (void *)com_vtbl_slot_stub_252,
        (void *)com_vtbl_slot_stub_253,
        (void *)com_vtbl_slot_stub_254,
        (void *)com_vtbl_slot_stub_255
    }
};

/* ============================================================================
 * Implemented PE64 Exports (Strictly Routed via WINE_UNIX_CALL)
 * ============================================================================ */

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateDevice(
    void *pAdapter,
    const D3D12XBOX_CREATE_DEVICE_PARAMETERS *pParameters,
    const scarlett_guid_t *riid,
    void **ppDevice) {
    (void)pAdapter;
    if (ppDevice == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppDevice = NULL;

    if (riid != NULL && !d3d12xs_is_device_iid(riid)) {
        d3d12xs_pe_log_stub_ods(
            "d3d12_xs.dll",
            "D3D12XboxCreateDevice(unknown_iid)",
            SCARLETT_STUB_SLOT_NONE,
            (uint64_t)(uintptr_t)__builtin_return_address(0),
            (uint64_t)(uintptr_t)pAdapter,
            (uint64_t)(uintptr_t)pParameters,
            (uint64_t)(uintptr_t)riid,
            (uint64_t)(uintptr_t)ppDevice,
            SCARLETT_E_NOINTERFACE,
            NULL);
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
    if (pParameters != NULL) {
        p.gxdk_version = pParameters->Version;
        p.process_debug_flags = pParameters->ProcessDebugFlags;
        p.graphics_cmd_buf_bytes = pParameters->GraphicsCommandBufferSizeBytes;
        p.graphics_scratch_bytes = pParameters->GraphicsScratchMemorySizeBytes;
        p.compute_cmd_buf_bytes = pParameters->ComputeCommandBufferSizeBytes;
        p.compute_scratch_bytes = pParameters->ComputeScratchMemorySizeBytes;
        p.dma_cmd_buf_bytes = pParameters->DmaCommandBufferSizeBytes;
        p.auto_breadcrumb_node_count = pParameters->AutoBreadcrumbNodeCount;
        p.amp_shader_indirect_args_bytes = pParameters->AmplificationShaderIndirectArgsBufferSize;
        p.amp_shader_payload_bytes = pParameters->AmplificationShaderPayloadBufferSize;
        p.devkit_diagnostic_buffer_bytes = pParameters->DevKitDiagnosticBufferSize;
        p.request_validation = (pParameters->ProcessDebugFlags != 0u) ? 1u : 0u;
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
    slot->real_device = NULL;
#if defined(_WIN32) || defined(__MINGW32__)
    {
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_D3D12CreateDevice_t)(
            void *pAdapter,
            uint32_t MinimumFeatureLevel,
            const scarlett_guid_t *riid,
            void **ppDevice);
        d3d12xs_pe_resolve_kernel32();
        scarlett_get_module_handle_a_fn_t gmh =
            __atomic_load_n(&g_pfn_GetModuleHandleA, __ATOMIC_ACQUIRE);
        scarlett_load_library_a_fn_t lla =
            __atomic_load_n(&g_pfn_LoadLibraryA, __ATOMIC_ACQUIRE);
        scarlett_get_proc_address_fn_t gpa =
            __atomic_load_n(&g_pfn_GetProcAddress, __ATOMIC_ACQUIRE);
        void *h_d3d12 = (gmh != NULL) ? gmh("d3d12.dll") : NULL;
        if (h_d3d12 == NULL && lla != NULL) {
            h_d3d12 = lla("d3d12.dll");
        }
        scarlett_hresult_t real_hr = SCARLETT_E_FAIL;
        if (h_d3d12 != NULL && gpa != NULL) {
            pfn_D3D12CreateDevice_t pfn_create =
                (pfn_D3D12CreateDevice_t)gpa(h_d3d12, "D3D12CreateDevice");
            if (pfn_create != NULL) {
                void *real_dev = NULL;
                real_hr = pfn_create(NULL, 0xc000u, &kIidID3D12Device_Std, &real_dev);
                if (scarlett_succeeded(real_hr) && real_dev != NULL) {
                    slot->real_device = real_dev;
                }
            }
        }
        d3d12xs_pe_vkd3d_log("Bound real vkd3d-proton ID3D12Device", slot->real_device, real_hr);
    }
#endif
    *ppDevice = (void *)slot;
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "D3D12XboxCreateDevice",
        SCARLETT_STUB_SLOT_NONE,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pAdapter,
        (uint64_t)(uintptr_t)pParameters,
        (uint64_t)(uintptr_t)riid,
        (uint64_t)(uintptr_t)slot,
        SCARLETT_OK,
        "(created)");
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

static void d3d12xs_readback_log(
    const char *tag,
    uint64_t a0,
    uint64_t a1,
    uint32_t w,
    uint32_t h,
    uint32_t fmt,
    uint32_t nonzero,
    bool force) {
    static uint32_t g_rb_log_seq = 0u;
    uint32_t seq = __atomic_add_fetch(&g_rb_log_seq, 1u, __ATOMIC_RELAXED);
    if (!force && seq > 8u && (seq % 60u) != 0u) {
        return;
    }
    if (force && seq > 8u) {
        return;
    }
    char buf[280];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = d3d12xs_pe_append_str(p, end, "[scarlett-readback] ");
    p = d3d12xs_pe_append_str(p, end, tag);
    p = d3d12xs_pe_append_str(p, end, " seq=");
    p = d3d12xs_pe_append_u32(p, end, seq);
    p = d3d12xs_pe_append_str(p, end, " a0=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, a0, 16);
    p = d3d12xs_pe_append_str(p, end, " a1=0x");
    p = d3d12xs_pe_append_hex_padded(p, end, a1, 16);
    p = d3d12xs_pe_append_str(p, end, " ");
    p = d3d12xs_pe_append_u32(p, end, w);
    p = d3d12xs_pe_append_str(p, end, "x");
    p = d3d12xs_pe_append_u32(p, end, h);
    p = d3d12xs_pe_append_str(p, end, " fmt=");
    p = d3d12xs_pe_append_u32(p, end, fmt);
    p = d3d12xs_pe_append_str(p, end, " nz=");
    p = d3d12xs_pe_append_u32(p, end, nonzero);
    p = d3d12xs_pe_append_str(p, end, "\n");
    *p = '\0';
    d3d12xs_pe_ods(buf);
}

static bool d3d12xs_readback_plane0_if_dirty(
    d3d12xs_cmd_queue_meta_t *qm,
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *planes,
    uint32_t plane_count) {
    if (qm == NULL || qm->real_obj == NULL || qm->owner_device == NULL ||
        qm->owner_device->real_device == NULL ||
        planes == NULL || plane_count == 0u) {
        return false;
    }
    void *raw_res = NULL;
    if (planes[0].ResourceCount > 0u && planes[0].ppResources != NULL &&
        (uintptr_t)planes[0].ppResources > 0x10000u) {
        raw_res = planes[0].ppResources[0];
    }
    d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(raw_res);
    if (rm == NULL || rm->gpu_va <= 0x10000u) {
        return false;
    }
    uint32_t cur_exec = __atomic_load_n(&g_gpu_exec_seq, __ATOMIC_ACQUIRE);
    if (rm->real_obj == NULL) {
        if (cur_exec > 0u) {
            d3d12xs_readback_log("no_real_res", (uint64_t)(uintptr_t)raw_res, rm->gpu_va,
                                 (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
        }
        return false;
    }
    bool dirty = (__atomic_load_n(&rm->gpu_dirty, __ATOMIC_ACQUIRE) != 0u);
    if (!dirty && (cur_exec == 0u || cur_exec == rm->last_readback_exec_seq)) {
        return false;
    }
#if defined(_WIN32) || defined(__MINGW32__)
    d3d12xs_pe_resolve_kernel32();
    scarlett_create_event_a_fn_t fn_cea = __atomic_load_n(&g_pfn_CreateEventA, __ATOMIC_ACQUIRE);
    scarlett_wait_single_fn_t fn_wfso = __atomic_load_n(&g_pfn_WaitForSingleObject, __ATOMIC_ACQUIRE);
    if (fn_cea == NULL || fn_wfso == NULL) {
        return false;
    }
#endif
    static uint32_t g_rb_lock = 0u;
    static void *g_rb_dev = NULL;
    static void *g_rb_alloc = NULL;
    static void *g_rb_list = NULL;
    static void *g_rb_fence = NULL;
    static void *g_rb_event = NULL;
    static void *g_rb_res = NULL;
    static uint64_t g_rb_cap = 0u;
    static uint64_t g_rb_fence_val = 0u;

    while (__atomic_exchange_n(&g_rb_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
    }
    void *real_dev = qm->owner_device->real_device;
    void **dev_vtbl = *(void ***)real_dev;
    if (g_rb_dev != real_dev) {
        typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
        if (g_rb_res != NULL) { ((pfn_rel_t)(*(void ***)g_rb_res)[2])(g_rb_res); g_rb_res = NULL; }
        if (g_rb_list != NULL) { ((pfn_rel_t)(*(void ***)g_rb_list)[2])(g_rb_list); g_rb_list = NULL; }
        if (g_rb_alloc != NULL) { ((pfn_rel_t)(*(void ***)g_rb_alloc)[2])(g_rb_alloc); g_rb_alloc = NULL; }
        if (g_rb_fence != NULL) { ((pfn_rel_t)(*(void ***)g_rb_fence)[2])(g_rb_fence); g_rb_fence = NULL; }
        g_rb_cap = 0u;
        g_rb_fence_val = 0u;
        g_rb_dev = real_dev;

        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_cca_t)(
            void *, uint32_t, const scarlett_guid_t *, void **);
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_ccl_t)(
            void *, uint32_t, uint32_t, void *, void *, const scarlett_guid_t *, void **);
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_cf_t)(
            void *, uint64_t, uint32_t, const scarlett_guid_t *, void **);
        if (dev_vtbl[9] != NULL && dev_vtbl[12] != NULL && dev_vtbl[36] != NULL &&
            scarlett_succeeded(((pfn_cca_t)dev_vtbl[9])(real_dev, 0u, &kIidID3D12CommandAllocator, &g_rb_alloc)) &&
            g_rb_alloc != NULL) {
            if (scarlett_succeeded(((pfn_ccl_t)dev_vtbl[12])(
                    real_dev, 0u, 0u, g_rb_alloc, NULL, &kIidID3D12GraphicsCommandList_Std, &g_rb_list)) &&
                g_rb_list != NULL) {
                typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_close_t)(void *);
                ((pfn_close_t)(*(void ***)g_rb_list)[9])(g_rb_list);
            }
            (void)((pfn_cf_t)dev_vtbl[36])(real_dev, 0u, 0u, &kIidID3D12Fence, &g_rb_fence);
        }
        if (g_rb_event == NULL) {
#if defined(_WIN32) || defined(__MINGW32__)
            g_rb_event = fn_cea(NULL, 0, 0, NULL);
#else
            g_rb_event = (void *)(uintptr_t)1u;
#endif
        }
    }
    if (g_rb_alloc == NULL || g_rb_list == NULL || g_rb_fence == NULL || g_rb_event == NULL) {
        d3d12xs_readback_log("rb_init_fail", (uint64_t)(uintptr_t)g_rb_alloc,
                             (uint64_t)(uintptr_t)g_rb_list,
                             (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
        __atomic_store_n(&g_rb_lock, 0u, __ATOMIC_RELEASE);
        return false;
    }

    scarlett_d3d12_placed_subresource_footprint_t fp;
    d3d12xs_pe_zero(&fp, sizeof(fp));
    uint32_t fp_rows = 0u;
    uint64_t fp_row_bytes = 0u;
    uint64_t total_bytes = 0u;
    if (rm->desc.Dimension == 1u) {
        total_bytes = rm->alloc_size;
    } else if (dev_vtbl[38] != NULL) {
        scarlett_d3d12_resource_desc_t pc_desc;
        d3d12xs_pe_memcpy(&pc_desc, &rm->desc, sizeof(pc_desc));
        pc_desc.Alignment = 0u;
        pc_desc.Layout = 0u;
        if (pc_desc.SampleCount == 0u) pc_desc.SampleCount = 1u;
        if (pc_desc.Height == 0u) pc_desc.Height = 1u;
        if (pc_desc.DepthOrArraySize == 0u) pc_desc.DepthOrArraySize = 1u;
        if (pc_desc.MipLevels == 0u) pc_desc.MipLevels = 1u;
        pc_desc.Flags &= 0x3Fu;
        typedef void (SCARLETT_D3D12XS_CALL *pfn_gcf_t)(
            void *, const void *, uint32_t, uint32_t, uint64_t, void *, uint32_t *, uint64_t *, uint64_t *);
        ((pfn_gcf_t)dev_vtbl[38])(
            real_dev, &pc_desc, 0u, 1u, 0u, &fp, &fp_rows, &fp_row_bytes, &total_bytes);
    }
    if (total_bytes == 0u || total_bytes > (UINT64_C(256) << 20)) {
        d3d12xs_readback_log("gcf_zero", total_bytes, rm->alloc_size,
                             (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
        __atomic_store_n(&g_rb_lock, 0u, __ATOMIC_RELEASE);
        return false;
    }
    if (g_rb_res == NULL || g_rb_cap < total_bytes) {
        if (g_rb_res != NULL) {
            typedef uint32_t (SCARLETT_D3D12XS_CALL *pfn_rel_t)(void *);
            ((pfn_rel_t)(*(void ***)g_rb_res)[2])(g_rb_res);
            g_rb_res = NULL;
            g_rb_cap = 0u;
        }
        uint64_t req_cap = (total_bytes + 65535u) & ~UINT64_C(65535);
        scarlett_d3d12_resource_desc_t rb_desc;
        d3d12xs_pe_zero(&rb_desc, sizeof(rb_desc));
        rb_desc.Dimension = 1u;
        rb_desc.Width = req_cap;
        rb_desc.Height = 1u;
        rb_desc.DepthOrArraySize = 1u;
        rb_desc.MipLevels = 1u;
        rb_desc.SampleCount = 1u;
        rb_desc.Layout = 1u; /* ROW_MAJOR */
        struct {
            uint32_t Type;
            uint32_t CPUPageProperty;
            uint32_t MemoryPoolPreference;
            uint32_t CreationNodeMask;
            uint32_t VisibleNodeMask;
        } hp;
        hp.Type = 3u; /* D3D12_HEAP_TYPE_READBACK */
        hp.CPUPageProperty = 0u;
        hp.MemoryPoolPreference = 0u;
        hp.CreationNodeMask = 1u;
        hp.VisibleNodeMask = 1u;
        typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_ccr_t)(
            void *, const void *, uint32_t, const void *, uint32_t, const void *,
            const scarlett_guid_t *, void **);
        scarlett_hresult_t rhr = ((pfn_ccr_t)dev_vtbl[27])(
            real_dev, &hp, 0u, &rb_desc, 0x400u /* D3D12_RESOURCE_STATE_COPY_DEST */,
            NULL, &kIidID3D12Resource, &g_rb_res);
        if (scarlett_succeeded(rhr) && g_rb_res != NULL) {
            g_rb_cap = req_cap;
        } else {
            g_rb_res = NULL;
        }
    }
    if (g_rb_res == NULL) {
        d3d12xs_readback_log("rb_res_fail", total_bytes, 0u,
                             (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
        __atomic_store_n(&g_rb_lock, 0u, __ATOMIC_RELEASE);
        return false;
    }

    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_ares_t)(void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_lres_t)(void *, void *, void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_lclose_t)(void *);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_cbr_t)(void *, void *, uint64_t, void *, uint64_t, uint64_t);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_ctr_t)(
        void *, const void *, uint32_t, uint32_t, uint32_t, const void *, const void *);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_bar_t)(void *, uint32_t, const void *);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_ecl_t)(void *, uint32_t, void * const *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_qsig_t)(void *, void *, uint64_t);
    typedef uint64_t (SCARLETT_D3D12XS_CALL *pfn_fgcv_t)(void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_fseoc_t)(void *, uint64_t, void *);
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_map_t)(void *, uint32_t, const void *, void **);
    typedef void (SCARLETT_D3D12XS_CALL *pfn_unmap_t)(void *, uint32_t, const void *);

    void **avtbl = *(void ***)g_rb_alloc;
    void **lvtbl = *(void ***)g_rb_list;
    void **qvtbl = *(void ***)qm->real_obj;
    void **fvtbl = *(void ***)g_rb_fence;
    void **rvtbl = *(void ***)g_rb_res;

    if (scarlett_failed(((pfn_ares_t)avtbl[8])(g_rb_alloc)) ||
        scarlett_failed(((pfn_lres_t)lvtbl[10])(g_rb_list, g_rb_alloc, NULL))) {
        d3d12xs_readback_log("reset_fail", 0u, 0u,
                             (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
        __atomic_store_n(&g_rb_lock, 0u, __ATOMIC_RELEASE);
        return false;
    }

    uint32_t saved_state = __atomic_load_n(&rm->current_state, __ATOMIC_ACQUIRE);
    bool did_barrier = false;
    if (rm->desc.Dimension != 1u && lvtbl[26] != NULL && saved_state != 0x800u) {
        struct {
            uint32_t Type;
            uint32_t Flags;
            void *pResource;
            uint32_t Subresource;
            uint32_t StateBefore;
            uint32_t StateAfter;
        } b;
        b.Type = 0u;
        b.Flags = 0u;
        b.pResource = rm->real_obj;
        b.Subresource = 0xFFFFFFFFu;
        b.StateBefore = saved_state;
        b.StateAfter = 0x800u; /* D3D12_RESOURCE_STATE_COPY_SOURCE */
        ((pfn_bar_t)lvtbl[26])(g_rb_list, 1u, &b);
        did_barrier = true;
    }

    if (rm->desc.Dimension == 1u) {
        ((pfn_cbr_t)lvtbl[15])(g_rb_list, g_rb_res, 0u, rm->real_obj, 0u, total_bytes);
    } else {
        scarlett_d3d12_texture_copy_location_t dst_loc;
        scarlett_d3d12_texture_copy_location_t src_loc;
        d3d12xs_pe_zero(&dst_loc, sizeof(dst_loc));
        d3d12xs_pe_zero(&src_loc, sizeof(src_loc));
        dst_loc.pResource = g_rb_res;
        dst_loc.Type = 1u; /* PlacedFootprint */
        dst_loc.PlacedFootprint = fp;
        src_loc.pResource = rm->real_obj;
        src_loc.Type = 0u; /* SubresourceIndex */
        src_loc.SubresourceIndex = 0u;
        ((pfn_ctr_t)lvtbl[16])(g_rb_list, &dst_loc, 0u, 0u, 0u, &src_loc, NULL);
    }

    if (did_barrier) {
        struct {
            uint32_t Type;
            uint32_t Flags;
            void *pResource;
            uint32_t Subresource;
            uint32_t StateBefore;
            uint32_t StateAfter;
        } b;
        b.Type = 0u;
        b.Flags = 0u;
        b.pResource = rm->real_obj;
        b.Subresource = 0xFFFFFFFFu;
        b.StateBefore = 0x800u;
        b.StateAfter = saved_state;
        ((pfn_bar_t)lvtbl[26])(g_rb_list, 1u, &b);
    }

    if (scarlett_failed(((pfn_lclose_t)lvtbl[9])(g_rb_list))) {
        __atomic_store_n(&g_rb_lock, 0u, __ATOMIC_RELEASE);
        return false;
    }

    void *lists[1] = { g_rb_list };
    ((pfn_ecl_t)qvtbl[10])(qm->real_obj, 1u, lists);
    uint64_t next_val = ++g_rb_fence_val;
    if (qvtbl[14] != NULL &&
        scarlett_succeeded(((pfn_qsig_t)qvtbl[14])(qm->real_obj, g_rb_fence, next_val))) {
        if (fvtbl[8] != NULL && ((pfn_fgcv_t)fvtbl[8])(g_rb_fence) < next_val) {
            if (fvtbl[9] != NULL &&
                scarlett_succeeded(((pfn_fseoc_t)fvtbl[9])(g_rb_fence, next_val, g_rb_event))) {
#if defined(_WIN32) || defined(__MINGW32__)
                (void)fn_wfso(g_rb_event, 2000u);
#endif
            }
        }
    }

    bool ok = false;
    struct { uint64_t Begin; uint64_t End; } read_range = { 0u, total_bytes };
    struct { uint64_t Begin; uint64_t End; } empty_range = { 0u, 0u };
    void *mapped = NULL;
    uint32_t sample_nz = 0u;
    uint32_t first_px = 0u;
    if (scarlett_succeeded(((pfn_map_t)rvtbl[8])(g_rb_res, 0u, &read_range, &mapped)) &&
        mapped != NULL) {
        uint8_t *uma_dst = (uint8_t *)(uintptr_t)rm->gpu_va;
        if (rm->desc.Dimension == 1u) {
            size_t copy_sz = (size_t)((total_bytes < rm->alloc_size) ? total_bytes : rm->alloc_size);
            if (d3d12xs_pe_is_writable_va(rm->gpu_va, copy_sz)) {
                d3d12xs_pe_memcpy(uma_dst, mapped, copy_sz);
                ok = true;
            }
        } else {
            uint32_t row_bytes = 0u;
            uint32_t num_rows = 0u;
            d3d12xs_format_layout(
                rm->desc.Format,
                (uint32_t)(rm->desc.Width > 0u ? rm->desc.Width : 1u),
                rm->desc.Height > 0u ? rm->desc.Height : 1u,
                &row_bytes,
                &num_rows);
            uint32_t uma_pitch = (row_bytes + 255u) & ~255u;
            uint32_t src_pitch = fp.Footprint.RowPitch;
            uint32_t rows = (fp_rows > 0u && fp_rows < num_rows) ? fp_rows : num_rows;
            uint64_t max_dst_bytes = (uint64_t)rows * (uint64_t)uma_pitch;
            if (max_dst_bytes > rm->alloc_size) {
                rows = (uint32_t)(rm->alloc_size / (uint64_t)uma_pitch);
            }
            size_t copy_row = (size_t)((row_bytes < src_pitch) ? row_bytes : src_pitch);
            if (copy_row > 0u && rows > 0u &&
                d3d12xs_pe_is_writable_va(rm->gpu_va, (size_t)rows * (size_t)uma_pitch)) {
                const uint8_t *src_bytes = (const uint8_t *)mapped + fp.Offset;
                for (uint32_t y = 0u; y < rows; ++y) {
                    d3d12xs_pe_memcpy(
                        uma_dst + (size_t)y * (size_t)uma_pitch,
                        src_bytes + (size_t)y * (size_t)src_pitch,
                        copy_row);
                }
                const uint32_t step_y = (rows > 16u) ? (rows / 16u) : 1u;
                const uint32_t row_dwords = (uint32_t)(copy_row / 4u);
                const uint32_t step_x = (row_dwords > 16u) ? (row_dwords / 16u) : 1u;
                for (uint32_t y = 0u; y < rows; y += step_y) {
                    const uint32_t *r32 = (const uint32_t *)(uma_dst + (size_t)y * (size_t)uma_pitch);
                    for (uint32_t x = 0u; x < row_dwords; x += step_x) {
                        uint32_t v = r32[x];
                        if ((v & 0x00FFFFFFu) != 0u) {
                            if (first_px == 0u) {
                                first_px = v;
                            }
                            ++sample_nz;
                        }
                    }
                }
                ok = true;
            } else {
                d3d12xs_readback_log("not_writable", rm->gpu_va, (uint64_t)rows * (uint64_t)uma_pitch,
                                     (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
            }
        }
        ((pfn_unmap_t)rvtbl[9])(g_rb_res, 0u, &empty_range);
    } else {
        d3d12xs_readback_log("map_fail", total_bytes, 0u,
                             (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, 0u, true);
    }
    if (ok) {
        __atomic_store_n(&rm->gpu_dirty, 0u, __ATOMIC_RELEASE);
        rm->last_readback_exec_seq = cur_exec;
        d3d12xs_readback_log("ok", rm->gpu_va, first_px,
                             (uint32_t)rm->desc.Width, rm->desc.Height, rm->desc.Format, sample_nz, false);
    }
    __atomic_store_n(&g_rb_lock, 0u, __ATOMIC_RELEASE);
    return ok;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxPresentX(
    uint64_t device_handle,
    uint32_t plane_count,
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *pPlaneParameters,
    const D3D12XBOX_PRESENT_PARAMETERS *pPresentParameters,
    scarlett_d3d12xs_presentx_params_t *out_present_info) {
    if (plane_count > 2u || (plane_count > 0u && pPlaneParameters == NULL)) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_d3d12xs_presentx_params_t local_p;
    scarlett_d3d12xs_presentx_params_t *p =
        (out_present_info != NULL) ? out_present_info : &local_p;
    d3d12xs_pe_zero(p, sizeof(*p));
    p->device_handle = device_handle;
    p->plane_count = plane_count;
    p->host_x11_window = d3d12xs_pe_get_host_x11_window();
    for (uint32_t i = 0u; i < plane_count; ++i) {
        void *raw_res = NULL;
        if (pPlaneParameters[i].ResourceCount > 0u &&
            pPlaneParameters[i].ppResources != NULL &&
            (uintptr_t)pPlaneParameters[i].ppResources > 0x10000u) {
            raw_res = pPlaneParameters[i].ppResources[0];
        }
        if (d3d12xs_is_com_resource(raw_res)) {
            const d3d12xs_res_meta_t *rm = d3d12xs_get_res_meta(raw_res);
            p->planes[i].resource_handle = rm->unix_resource_handle;
            p->planes[i].gpu_va = rm->gpu_va;
            p->planes[i].width = (uint32_t)rm->desc.Width;
            p->planes[i].height = rm->desc.Height;
            p->planes[i].dxgi_format = rm->desc.Format;
            uint32_t row_bytes = 0u;
            uint32_t num_rows = 0u;
            d3d12xs_format_layout(
                rm->desc.Format,
                (uint32_t)(rm->desc.Width > 0u ? rm->desc.Width : 1u),
                rm->desc.Height > 0u ? rm->desc.Height : 1u,
                &row_bytes,
                &num_rows);
            p->planes[i].row_pitch = (row_bytes + 255u) & ~255u;
        } else {
            p->planes[i].resource_handle = (uint64_t)(uintptr_t)raw_res;
        }
        if (pPlaneParameters[i].pSrcViewRects != NULL &&
            (uintptr_t)pPlaneParameters[i].pSrcViewRects > 0x10000u) {
            p->planes[i].src_left = (uint32_t)pPlaneParameters[i].pSrcViewRects[0].left;
            p->planes[i].src_top = (uint32_t)pPlaneParameters[i].pSrcViewRects[0].top;
            p->planes[i].src_right = (uint32_t)pPlaneParameters[i].pSrcViewRects[0].right;
            p->planes[i].src_bottom = (uint32_t)pPlaneParameters[i].pSrcViewRects[0].bottom;
        }
        p->planes[i].flags = pPlaneParameters[i].Flags;
        p->planes[i].color_space = pPlaneParameters[i].ColorSpace;
        if (p->frame_token == 0u && pPlaneParameters[i].Token != 0u) {
            p->frame_token = pPlaneParameters[i].Token;
        }
    }
    if (pPresentParameters != NULL) {
        if (pPresentParameters->Token != 0u) {
            p->frame_token = pPresentParameters->Token;
        }
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

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12ConfigureVirtualMemoryX(
    const void *pConfiguration) {
    d3d12xs_pe_log_stub_ods(
        "d3d12_xs.dll",
        "D3D12ConfigureVirtualMemoryX",
        SCARLETT_STUB_SLOT_NONE,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pConfiguration,
        0u,
        0u,
        0u,
        SCARLETT_OK,
        "(no-op)");
    return SCARLETT_OK;
}

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12SerializeVersionedRootSignature(
    const void *pRootSignatureDesc,
    void **ppBlob,
    void **ppErrorBlob) {
    if (ppBlob != NULL) {
        *ppBlob = NULL;
    }
    if (ppErrorBlob != NULL) {
        *ppErrorBlob = NULL;
    }

#if defined(_WIN32) || defined(__MINGW32__)
    typedef scarlett_hresult_t (SCARLETT_D3D12XS_CALL *pfn_D3D12SerializeVersionedRootSignature_t)(
        const void *pRootSignatureDesc,
        void **ppBlob,
        void **ppErrorBlob);

    static pfn_D3D12SerializeVersionedRootSignature_t pfn = NULL;
    if (pfn == NULL) {
        d3d12xs_pe_resolve_kernel32();
        scarlett_get_module_handle_a_fn_t gmh =
            __atomic_load_n(&g_pfn_GetModuleHandleA, __ATOMIC_ACQUIRE);
        scarlett_load_library_a_fn_t lla =
            __atomic_load_n(&g_pfn_LoadLibraryA, __ATOMIC_ACQUIRE);
        scarlett_get_proc_address_fn_t gpa =
            __atomic_load_n(&g_pfn_GetProcAddress, __ATOMIC_ACQUIRE);
        void *h = (gmh != NULL) ? gmh("d3d12.dll") : NULL;
        if (h == NULL && lla != NULL) {
            h = lla("d3d12.dll");
        }
        if (h != NULL && gpa != NULL) {
            pfn = (pfn_D3D12SerializeVersionedRootSignature_t)gpa(
                h, "D3D12SerializeVersionedRootSignature");
        }
    }
    if (pfn != NULL) {
        return pfn(pRootSignatureDesc, ppBlob, ppErrorBlob);
    }
#endif
    static const scarlett_stub_descriptor_t kDesc = {
        SCARLETT_STUB_THUNK_ABI_VERSION,
        SCARLETT_STUB_FLAG_NONE,
        SCARLETT_STUB_ORDINAL_NONE,
        SCARLETT_STUB_SLOT_NONE,
        SCARLETT_ERR_UNIMPLEMENTED_STUB,
        0u,
        "d3d12_xs.dll",
        "D3D12SerializeVersionedRootSignature"
    };
    return d3d12xs_pe_dispatch_stub(
        &kDesc,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)pRootSignatureDesc,
        (uint64_t)(uintptr_t)ppBlob,
        (uint64_t)(uintptr_t)ppErrorBlob,
        0u);
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
