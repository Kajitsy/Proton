/*
 * Proton-Scarlett — Freestanding PE64 Common Bridge Helper (xg_xs / XboxBvhBuilder / pixevt)
 *
 * Strictly freestanding C11:
 *   - Zero dependencies on libc, POSIX, <windows.h>, or libvulkan.
 *   - Crosses PE <-> xg_xs_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - Volatile byte loops prevent clang -O2 from synthesizing memset/memcpy.
 */

#ifndef SCARLETT_XG_XS_PE_COMMON_H
#define SCARLETT_XG_XS_PE_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(SCARLETT_USE_WINE_UNIXLIB)
#include <wine/unixlib.h>
#endif

#include "xg_xs_unix_abi.h"

static inline void xg_pe_zero_mem(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0u; i < len; ++i) {
        p[i] = 0u;
    }
}

static inline void xg_pe_copy_mem(void *dst, const void *src, size_t len) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0u; i < len; ++i) {
        d[i] = s[i];
    }
}

static inline void xg_pe_copy_cstr(char *dst, size_t cap, const char *src) {
    if (dst == NULL || cap == 0u) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0u;
    while (i + 1u < cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static inline bool xg_pe_guid_equal(const scarlett_guid_t *a, const scarlett_guid_t *b) {
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

/* Freestanding minimal printf-style formatter for PIX event labels */
static inline void xg_pe_format_label_va(
    char *dst,
    size_t cap,
    const char *fmt,
    va_list ap) {
    if (dst == NULL || cap == 0u) {
        return;
    }
    if (fmt == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t out = 0u;
    for (size_t i = 0u; fmt[i] != '\0' && out + 1u < cap; ++i) {
        if (fmt[i] != '%' || fmt[i + 1u] == '\0') {
            dst[out++] = fmt[i];
            continue;
        }
        ++i;
        bool is_ll = false;
        if (fmt[i] == 'l' && fmt[i + 1u] == 'l') {
            is_ll = true;
            i += 2u;
        } else if (fmt[i] == 'l') {
            i += 1u;
        }
        char spec = fmt[i];
        if (spec == '\0') {
            break;
        }
        if (spec == '%') {
            dst[out++] = '%';
        } else if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            for (size_t j = 0u; s[j] != '\0' && out + 1u < cap; ++j) {
                dst[out++] = s[j];
            }
        } else if (spec == 'd' || spec == 'i') {
            int64_t val = is_ll ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
            uint64_t uval = 0u;
            if (val < 0) {
                if (out + 1u < cap) {
                    dst[out++] = '-';
                }
                uval = (uint64_t)(-(val + 1)) + UINT64_C(1);
            } else {
                uval = (uint64_t)val;
            }
            char tmp[24];
            size_t t = 0u;
            do {
                tmp[t++] = (char)('0' + (uval % UINT64_C(10)));
                uval /= UINT64_C(10);
            } while (uval != 0u && t < sizeof(tmp));
            while (t > 0u && out + 1u < cap) {
                dst[out++] = tmp[--t];
            }
        } else if (spec == 'u' || spec == 'x' || spec == 'X') {
            uint64_t uval = is_ll ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
            uint64_t base = (spec == 'u') ? UINT64_C(10) : UINT64_C(16);
            const char *digits = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[24];
            size_t t = 0u;
            do {
                tmp[t++] = digits[uval % base];
                uval /= base;
            } while (uval != 0u && t < sizeof(tmp));
            while (t > 0u && out + 1u < cap) {
                dst[out++] = tmp[--t];
            }
        } else {
            if (out + 1u < cap) {
                dst[out++] = '%';
            }
            if (out + 1u < cap) {
                dst[out++] = spec;
            }
        }
    }
    dst[out] = '\0';
}

#if defined(WINE_UNIX_CALL)

static inline int32_t xg_pe_unix_call(uint32_t opcode, void *params) {
    return SCARLETT_UNIX_CALL(opcode, params);
}

#elif defined(_WIN32) || defined(__MINGW32__)

typedef uint64_t scarlett_wine_unixlib_handle_t;
extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern int32_t __wine_init_unix_call(void);
extern int32_t (SCARLETT_XG_CALL *__wine_unix_call_dispatcher)(
    scarlett_wine_unixlib_handle_t handle,
    uint32_t code,
    void *args);

static inline int32_t xg_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, opcode, params);
}

#else

static inline int32_t xg_pe_unix_call(uint32_t opcode, void *params) {
    scarlett_unix_call_fn_t fn = scarlett_xg_xs_get_unix_call_bridge();
    if (fn == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return fn(opcode, params);
}

#endif

#if defined(_WIN32) || defined(__MINGW32__)

typedef void (SCARLETT_XG_CALL *scarlett_xg_ods_fn_t)(const char *lpOutputString);

typedef struct xg_pe_list_entry_s {
    struct xg_pe_list_entry_s *Flink;
    struct xg_pe_list_entry_s *Blink;
} xg_pe_list_entry_t;

typedef struct xg_pe_unicode_string_s {
    uint16_t Length;
    uint16_t MaximumLength;
    uint32_t Reserved;
    const uint16_t *Buffer;
} xg_pe_unicode_string_t;

static scarlett_xg_ods_fn_t g_xg_pfn_OutputDebugStringA = NULL;
static uint32_t g_xg_ods_resolved = 0u;

static inline bool xg_pe_ascii_ieq(const char *a, const char *b) {
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

static inline bool xg_pe_utf16_ascii_ieq(const uint16_t *w, uint16_t byte_len, const char *ascii) {
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

static inline void *xg_pe_find_export(const uint8_t *base, const char *proc_name) {
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
        if (xg_pe_ascii_ieq(name, proc_name)) {
            uint32_t fn_rva = funcs[ords[i]];
            if (fn_rva >= exp_rva && fn_rva < exp_rva + exp_size) {
                return NULL;
            }
            return (void *)(uintptr_t)(base + fn_rva);
        }
    }
    return NULL;
}

static inline void xg_pe_resolve_ods(void) {
    if (__atomic_load_n(&g_xg_ods_resolved, __ATOMIC_ACQUIRE) != 0u) {
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
    const xg_pe_list_entry_t *head = (const xg_pe_list_entry_t *)(ldr + 0x20u);
    const uint8_t *k32_base = NULL;
    const uint8_t *kbase_base = NULL;

    for (const xg_pe_list_entry_t *cur = head->Flink;
         cur != NULL && cur != head;
         cur = cur->Flink) {
        const uint8_t *entry = (const uint8_t *)cur;
        const uint8_t *dll_base = *(const uint8_t *const *)(entry + 0x20u);
        const xg_pe_unicode_string_t *base_name =
            (const xg_pe_unicode_string_t *)(entry + 0x48u);
        if (dll_base != NULL && base_name->Buffer != NULL) {
            if (xg_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernel32.dll")) {
                k32_base = dll_base;
            } else if (xg_pe_utf16_ascii_ieq(base_name->Buffer, base_name->Length, "kernelbase.dll")) {
                kbase_base = dll_base;
            }
        }
    }

    void *ods = xg_pe_find_export(k32_base, "OutputDebugStringA");
    if (ods == NULL) {
        ods = xg_pe_find_export(kbase_base, "OutputDebugStringA");
    }
    __atomic_store_n(&g_xg_pfn_OutputDebugStringA, (scarlett_xg_ods_fn_t)ods, __ATOMIC_RELEASE);
    __atomic_store_n(&g_xg_ods_resolved, 1u, __ATOMIC_RELEASE);
}

static inline void xg_pe_ods_printf(const char *fmt, ...) {
    xg_pe_resolve_ods();
    scarlett_xg_ods_fn_t ods = __atomic_load_n(&g_xg_pfn_OutputDebugStringA, __ATOMIC_ACQUIRE);
    if (ods == NULL || fmt == NULL) {
        return;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    xg_pe_format_label_va(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ods(buf);
}

#else

static inline void xg_pe_ods_printf(const char *fmt, ...) {
    (void)fmt;
}

#endif

static inline scarlett_hresult_t xg_pe_stub_dispatch_local(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_xg_report_stub_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    if (desc != NULL) {
        params.ctx.flags = desc->flags;
        params.ctx.ordinal = desc->ordinal;
        params.ctx.vtable_slot = desc->vtable_slot;
        params.ctx.return_hr = scarlett_stub_enforce_error(desc->default_error);
        xg_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), desc->dll_name);
        xg_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), desc->symbol_name);
    } else {
        params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
        params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
        params.ctx.return_hr = SCARLETT_ERR_UNIMPLEMENTED_STUB;
        xg_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), "xg_xs.dll");
        xg_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), "unknown_symbol");
    }

    params.ctx.return_address = return_address;
    params.ctx.caller_module_base = 0u;
    params.ctx.caller_rva = return_address;
    xg_pe_copy_cstr(params.ctx.caller_module_name,
                    sizeof(params.ctx.caller_module_name),
                    "pe64_caller");
    params.ctx.rcx = rcx;
    params.ctx.rdx = rdx;
    params.ctx.r8 = r8;
    params.ctx.r9 = r9;

    xg_pe_ods_printf(
        "[scarlett-stub] dll=%s sym=%s ret=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx\n",
        params.ctx.dll_name,
        params.ctx.symbol_name,
        (unsigned long long)return_address,
        (unsigned long long)rcx,
        (unsigned long long)rdx,
        (unsigned long long)r8,
        (unsigned long long)r9);

    (void)xg_pe_unix_call(SCARLETT_XG_XS_UNIX_REPORT_STUB, &params);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        __builtin_trap();
    }
    return scarlett_stub_enforce_error(params.ctx.return_hr);
}

#if defined(_WIN32) || defined(__MINGW32__)
#ifndef DLL_PROCESS_ATTACH
#define DLL_PROCESS_ATTACH 1u
#endif

static inline uint32_t xg_pe_dll_main(uint32_t reason) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (__wine_init_unix_call() != 0) {
            return 0u;
        }
        scarlett_unix_init_params_t init_params;
        xg_pe_zero_mem(&init_params, sizeof(init_params));
        init_params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
        init_params.module_id = SCARLETT_XG_XS_UNIX_MODULE_ID;
        init_params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
        init_params.shader_mode = (uint32_t)SCARLETT_SHADER_MODE_ADAPTIVE;
        init_params.net_mode = (uint32_t)SCARLETT_NET_MODE_OFFLINE;
        if (scarlett_failed(xg_pe_unix_call(SCARLETT_XG_XS_UNIX_INIT, &init_params))) {
            return 0u;
        }
    }
    return 1u;
}
#endif

#endif /* SCARLETT_XG_XS_PE_COMMON_H */
