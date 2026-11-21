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

#endif /* SCARLETT_XMEM_PE_BRIDGE_H */
