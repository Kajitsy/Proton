/*
 * Proton-Scarlett — Freestanding PE64 <-> xmem_unix.so WINE_UNIX_CALL Bridge
 *
 * Strictly zero dependencies on libc, POSIX, <windows.h>, or libvulkan.
 * Compiles identically under:
 *   - clang --target=x86_64-w64-mingw32 -ffreestanding -std=c11 -c
 *   - Wine 11.0 PE build (with <wine/unixlib.h>)
 *   - Host Linux C11 builds (linked against xmem_unix.so via bridge pointer)
 */

#ifndef SCARLETT_XMEM_PE_BRIDGE_H
#define SCARLETT_XMEM_PE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "xmem_api.h"

#if defined(_WIN32)
#if defined(WINE_UNIX_CALL)
static inline int32_t xmem_pe_unix_call(uint32_t opcode, void *params) {
    return (int32_t)WINE_UNIX_CALL(opcode, params);
}
static inline int32_t xmem_pe_init_unix_bridge(void) {
    return (int32_t)__wine_init_unix_call();
}
#else
typedef uint64_t scarlett_wine_unixlib_handle_t;
typedef int32_t (*scarlett_wine_unix_dispatcher_t)(
    scarlett_wine_unixlib_handle_t handle,
    unsigned int code,
    void *args);

extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern scarlett_wine_unix_dispatcher_t __wine_unix_call_dispatcher;
extern int32_t __wine_init_unix_call(void);

static inline int32_t xmem_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(
        __wine_unixlib_handle, (unsigned int)opcode, params);
}

static inline int32_t xmem_pe_init_unix_bridge(void) {
    return __wine_init_unix_call();
}
#endif
#else
static inline int32_t xmem_pe_unix_call(uint32_t opcode, void *params) {
    scarlett_unix_call_fn_t fn = scarlett_xmem_get_unix_call_bridge();
    if (fn == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return fn(opcode, params);
}

static inline int32_t xmem_pe_init_unix_bridge(void) {
    return (scarlett_xmem_get_unix_call_bridge() != NULL)
               ? SCARLETT_OK
               : SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
}
#endif

static inline void xmem_pe_copy(void *dst, const void *src, size_t n) {
    if (dst == NULL || src == NULL || n == 0u) {
        return;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0u; i < n; ++i) {
        d[i] = s[i];
    }
}

/* Volatile zeroing prevents -ffreestanding compilers from synthesizing libc memset */
static inline void xmem_pe_zero(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0; i < n; ++i) {
        p[i] = 0u;
    }
}

static inline void xmem_pe_copy_cstr(char *dst, size_t cap, const char *src) {
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

/* ============================================================================
 * Freestanding PEB-Resolved Kernel32/KernelBase Exports & Diagnostic Formatting
 * (Zero __imp_* imports, zero libc dependencies)
 * ============================================================================ */

#define XMEM_MEM_PRESERVE_PLACEHOLDER  UINT32_C(0x00000002)
#define XMEM_MEM_COALESCE_PLACEHOLDERS UINT32_C(0x00000001)
#define XMEM_MEM_REPLACE_PLACEHOLDER   UINT32_C(0x00004000)
#define XMEM_MEM_RESERVE_PLACEHOLDER   UINT32_C(0x00040000)

#if defined(_WIN32) || defined(__MINGW32__)

typedef void (SCARLETT_XMEM_CALL *xmem_pe_ods_fn_t)(const char *lpOutputString);
typedef void * (SCARLETT_XMEM_CALL *xmem_pe_va2_fn_t)(
    void *Process,
    void *BaseAddress,
    size_t Size,
    uint32_t AllocationType,
    uint32_t PageProtection,
    void *ExtendedParameters,
    uint32_t ParameterCount);
typedef int32_t (SCARLETT_XMEM_CALL *xmem_pe_vf_fn_t)(
    void *lpAddress,
    size_t dwSize,
    uint32_t dwFreeType);
typedef uint32_t (SCARLETT_XMEM_CALL *xmem_pe_gle_fn_t)(void);

typedef struct xmem_pe_list_entry_s {
    struct xmem_pe_list_entry_s *Flink;
    struct xmem_pe_list_entry_s *Blink;
} xmem_pe_list_entry_t;

typedef struct xmem_pe_unicode_string_s {
    uint16_t Length;
    uint16_t MaximumLength;
    uint32_t Reserved;
    const uint16_t *Buffer;
} xmem_pe_unicode_string_t;

static inline bool xmem_pe_ascii_ieq(const char *a, const char *b) {
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

static inline bool xmem_pe_utf16_ascii_ieq(const uint16_t *w, uint16_t byte_len, const char *ascii) {
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

static inline void *xmem_pe_find_export(const uint8_t *base, const char *proc_name) {
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
        return NULL;
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
        if (xmem_pe_ascii_ieq(name, proc_name)) {
            uint32_t fn_rva = funcs[ords[i]];
            if (fn_rva >= exp_rva && fn_rva < exp_rva + exp_size) {
                return NULL;
            }
            return (void *)(uintptr_t)(base + fn_rva);
        }
    }
    return NULL;
}

typedef struct xmem_pe_k32_exports_s {
    xmem_pe_ods_fn_t ods;
    xmem_pe_va2_fn_t va2;
    xmem_pe_vf_fn_t vf;
    xmem_pe_gle_fn_t gle;
} xmem_pe_k32_exports_t;

static inline const xmem_pe_k32_exports_t *xmem_pe_resolve_k32(void) {
    static xmem_pe_k32_exports_t s_exp = { NULL, NULL, NULL, NULL };
    static uint32_t s_resolved = 0u;

    if (__atomic_load_n(&s_resolved, __ATOMIC_ACQUIRE) != 0u) {
        return &s_exp;
    }

    const uint8_t *peb = NULL;
    __asm__ volatile("movq %%gs:0x60, %0" : "=r"(peb));
    if (peb == NULL) {
        return &s_exp;
    }
    const uint8_t *ldr = *(const uint8_t *const *)(peb + 0x18u);
    if (ldr == NULL) {
        return &s_exp;
    }
    const xmem_pe_list_entry_t *head = (const xmem_pe_list_entry_t *)(ldr + 0x20u);
    const uint8_t *k32_base = NULL;
    const uint8_t *kbase_base = NULL;

    for (const xmem_pe_list_entry_t *cur = head->Flink;
         cur != NULL && cur != head;
         cur = cur->Flink) {
        const uint8_t *entry = (const uint8_t *)cur;
        const uint8_t *dll_base = *(const uint8_t *const *)(entry + 0x20u);
        const xmem_pe_unicode_string_t *base_name =
            (const xmem_pe_unicode_string_t *)(entry + 0x48u);
        if (dll_base != NULL && base_name->Buffer != NULL) {
            if (xmem_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernel32.dll")) {
                k32_base = dll_base;
            } else if (xmem_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernelbase.dll")) {
                kbase_base = dll_base;
            }
        }
    }

    void *ods = xmem_pe_find_export(k32_base, "OutputDebugStringA");
    if (ods == NULL) {
        ods = xmem_pe_find_export(kbase_base, "OutputDebugStringA");
    }
    void *va2 = xmem_pe_find_export(kbase_base, "VirtualAlloc2");
    if (va2 == NULL) {
        va2 = xmem_pe_find_export(k32_base, "VirtualAlloc2");
    }
    void *vf = xmem_pe_find_export(k32_base, "VirtualFree");
    if (vf == NULL) {
        vf = xmem_pe_find_export(kbase_base, "VirtualFree");
    }
    void *gle = xmem_pe_find_export(k32_base, "GetLastError");
    if (gle == NULL) {
        gle = xmem_pe_find_export(kbase_base, "GetLastError");
    }

    __atomic_store_n(&s_exp.ods, (xmem_pe_ods_fn_t)ods, __ATOMIC_RELEASE);
    __atomic_store_n(&s_exp.va2, (xmem_pe_va2_fn_t)va2, __ATOMIC_RELEASE);
    __atomic_store_n(&s_exp.vf, (xmem_pe_vf_fn_t)vf, __ATOMIC_RELEASE);
    __atomic_store_n(&s_exp.gle, (xmem_pe_gle_fn_t)gle, __ATOMIC_RELEASE);
    __atomic_store_n(&s_resolved, 1u, __ATOMIC_RELEASE);
    return &s_exp;
}

static inline void xmem_pe_ods(const char *msg) {
    xmem_pe_ods_fn_t ods = xmem_pe_resolve_k32()->ods;
    if (ods != NULL && msg != NULL) {
        ods(msg);
    }
}

static inline void *xmem_pe_virtual_alloc2(
    void *base_addr,
    size_t size_bytes,
    uint32_t alloc_type,
    uint32_t protect) {
    xmem_pe_va2_fn_t va2 = xmem_pe_resolve_k32()->va2;
    if (va2 == NULL) {
        return NULL;
    }
    return va2(NULL, base_addr, size_bytes, alloc_type, protect, NULL, 0u);
}

static inline int32_t xmem_pe_virtual_free(
    void *addr,
    size_t size_bytes,
    uint32_t free_type,
    int32_t *out_resolved) {
    xmem_pe_vf_fn_t vf = xmem_pe_resolve_k32()->vf;
    if (vf == NULL) {
        if (out_resolved != NULL) {
            *out_resolved = 0;
        }
        return 0;
    }
    if (out_resolved != NULL) {
        *out_resolved = 1;
    }
    return vf(addr, size_bytes, free_type);
}

static inline uint32_t xmem_pe_get_last_error(void) {
    xmem_pe_gle_fn_t gle = xmem_pe_resolve_k32()->gle;
    return gle != NULL ? gle() : 0u;
}

#else

static inline void xmem_pe_ods(const char *msg) {
    (void)msg;
}

static inline void *xmem_pe_virtual_alloc2(
    void *base_addr,
    size_t size_bytes,
    uint32_t alloc_type,
    uint32_t protect) {
    (void)base_addr;
    (void)size_bytes;
    (void)alloc_type;
    (void)protect;
    return NULL;
}

static inline int32_t xmem_pe_virtual_free(
    void *addr,
    size_t size_bytes,
    uint32_t free_type,
    int32_t *out_resolved) {
    (void)addr;
    (void)size_bytes;
    (void)free_type;
    if (out_resolved != NULL) {
        *out_resolved = 0;
    }
    return 0;
}

static inline uint32_t xmem_pe_get_last_error(void) {
    return 0u;
}

#endif

static inline char *xmem_pe_append_str(char *p, char *end, const char *s) {
    if (s == NULL) {
        s = "(null)";
    }
    while (*s != '\0' && p + 1 < end) {
        *p++ = *s++;
    }
    return p;
}

static inline char *xmem_pe_append_hex(char *p, char *end, uint64_t val) {
    static const char kHex[] = "0123456789abcdef";
    p = xmem_pe_append_str(p, end, "0x");
    if (val == 0u) {
        if (p + 1 < end) {
            *p++ = '0';
        }
        return p;
    }
    int started = 0;
    for (int i = 15; i >= 0; --i) {
        uint8_t nib = (uint8_t)((val >> (i * 4)) & 0xFu);
        if (nib != 0u || started != 0) {
            started = 1;
            if (p + 1 < end) {
                *p++ = kHex[nib];
            }
        }
    }
    return p;
}

static inline void xmem_pe_log_diag(
    const char *tag,
    const char *dll_name,
    const char *symbol_name,
    uint64_t caller_addr,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9,
    int32_t status) {
    char buf[384];
    char *p = buf;
    char *end = buf + sizeof(buf);
    p = xmem_pe_append_str(p, end, tag);
    p = xmem_pe_append_str(p, end, " dll=");
    p = xmem_pe_append_str(p, end, dll_name);
    p = xmem_pe_append_str(p, end, " sym=");
    p = xmem_pe_append_str(p, end, symbol_name);
    p = xmem_pe_append_str(p, end, " caller=");
    p = xmem_pe_append_hex(p, end, caller_addr);
    p = xmem_pe_append_str(p, end, " rcx=");
    p = xmem_pe_append_hex(p, end, rcx);
    p = xmem_pe_append_str(p, end, " rdx=");
    p = xmem_pe_append_hex(p, end, rdx);
    p = xmem_pe_append_str(p, end, " r8=");
    p = xmem_pe_append_hex(p, end, r8);
    p = xmem_pe_append_str(p, end, " r9=");
    p = xmem_pe_append_hex(p, end, r9);
    p = xmem_pe_append_str(p, end, " status=");
    p = xmem_pe_append_hex(p, end, (uint64_t)(uint32_t)status);
    p = xmem_pe_append_str(p, end, "\n");
    *p = '\0';
    xmem_pe_ods(buf);
}

#endif /* SCARLETT_XMEM_PE_BRIDGE_H */
