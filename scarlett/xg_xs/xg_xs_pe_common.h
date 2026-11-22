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
