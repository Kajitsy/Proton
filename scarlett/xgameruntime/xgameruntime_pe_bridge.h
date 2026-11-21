/*
 * Proton-Scarlett — Freestanding PE64 <-> xgameruntime_unix.so WINE_UNIX_CALL Bridge
 *
 * Strictly zero dependencies on libc, POSIX, <windows.h>, or libcurl.
 * Compiles identically under:
 *   - clang --target=x86_64-w64-mingw32 -ffreestanding -std=c11 -c
 *   - Wine 11.0 PE build (with <wine/unixlib.h>)
 *   - Host Linux C11 builds (linked against xgameruntime_unix.so via bridge pointer)
 */

#ifndef SCARLETT_XGAMERUNTIME_PE_BRIDGE_H
#define SCARLETT_XGAMERUNTIME_PE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "xgameruntime_api.h"

#if defined(_WIN32)
#if defined(WINE_UNIX_CALL)
static inline int32_t xgr_pe_unix_call(uint32_t opcode, void *params) {
    return (int32_t)WINE_UNIX_CALL(opcode, params);
}
static inline int32_t xgr_pe_init_unix_bridge(void) {
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

static inline int32_t xgr_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(
        __wine_unixlib_handle, (unsigned int)opcode, params);
}

static inline int32_t xgr_pe_init_unix_bridge(void) {
    return __wine_init_unix_call();
}
#endif
#else
static inline int32_t xgr_pe_unix_call(uint32_t opcode, void *params) {
    scarlett_unix_call_fn_t fn = scarlett_xgr_get_unix_call_bridge();
    if (fn == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return fn(opcode, params);
}

static inline int32_t xgr_pe_init_unix_bridge(void) {
    return (scarlett_xgr_get_unix_call_bridge() != NULL)
               ? SCARLETT_OK
               : SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
}
#endif

/* Volatile zero/copy prevents -ffreestanding Clang from synthesizing libc memset/memcpy */
static inline void xgr_pe_zero(void *dst, size_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    for (size_t i = 0; i < n; ++i) {
        p[i] = 0u;
    }
}

static inline void xgr_pe_copy(void *dst, const void *src, size_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}

static inline void xgr_pe_copy_cstr(char *dst, size_t cap, const char *src) {
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

static inline size_t xgr_pe_cstrlen(const char *s) {
    if (s == NULL) {
        return 0u;
    }
    size_t n = 0u;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static inline scarlett_hresult_t xgr_pe_dispatch_stub(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_loader_report_stub_params_t params;
    xgr_pe_zero(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    if (desc != NULL) {
        params.ctx.flags = desc->flags;
        params.ctx.ordinal = desc->ordinal;
        params.ctx.vtable_slot = desc->vtable_slot;
        params.ctx.return_hr = scarlett_stub_enforce_error(desc->default_error);
        xgr_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), desc->dll_name);
        xgr_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), desc->symbol_name);
    } else {
        params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
        params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
        params.ctx.return_hr = SCARLETT_ERR_UNIMPLEMENTED_STUB;
        xgr_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), "xgameruntime.dll");
        xgr_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), "unknown_stub");
    }

    params.ctx.return_address = return_address;
    params.ctx.caller_module_base = 0u;
    params.ctx.caller_rva = return_address;
    xgr_pe_copy_cstr(params.ctx.caller_module_name,
                     sizeof(params.ctx.caller_module_name),
                     "pe64_caller");
    params.ctx.rcx = rcx;
    params.ctx.rdx = rdx;
    params.ctx.r8 = r8;
    params.ctx.r9 = r9;

    (void)xgr_pe_unix_call(SCARLETT_XGR_UNIX_REPORT_STUB, &params);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        __builtin_trap();
    }
    return scarlett_stub_enforce_error(params.ctx.return_hr);
}

#endif /* SCARLETT_XGAMERUNTIME_PE_BRIDGE_H */
