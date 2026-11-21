/*
 * Proton-Scarlett — Freestanding PE64 Common Bridge Helper (Audio/Input/Media)
 *
 * Strictly freestanding C11 (zero libc, zero <windows.h>, zero POSIX).
 * Included by all 6 audio_media PE64 shims.
 */

#ifndef SCARLETT_AUDIO_PE_COMMON_H
#define SCARLETT_AUDIO_PE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(SCARLETT_USE_WINE_UNIXLIB)
#include <wine/unixlib.h>
#endif

#include "audio_media_unix_abi.h"

static inline void audio_pe_zero_mem(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0u; i < len; ++i) {
        p[i] = 0u;
    }
}

static inline void audio_pe_copy_mem(void *dst, const void *src, size_t len) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0u; i < len; ++i) {
        d[i] = s[i];
    }
}

static inline void audio_pe_copy_cstr(char *dst, size_t cap, const char *src) {
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

#if defined(WINE_UNIX_CALL)

static inline int32_t audio_pe_unix_call(uint32_t opcode, void *params) {
    return SCARLETT_UNIX_CALL(opcode, params);
}

#elif defined(_WIN32) || defined(__MINGW32__)

typedef uint64_t scarlett_wine_unixlib_handle_t;
extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern int32_t __wine_init_unix_call(void);
extern int32_t (SCARLETT_WIN64_API *__wine_unix_call_dispatcher)(
    scarlett_wine_unixlib_handle_t handle,
    uint32_t code,
    void *args);

static inline int32_t audio_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, opcode, params);
}

#else

/* Declared in xaudio2_9_pe.c for host Linux test bridge */
static inline int32_t audio_pe_unix_call(uint32_t opcode, void *params) {
    scarlett_audio_unix_bridge_fn_t fn = scarlett_audio_pe_get_unix_bridge();
    if (fn == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return fn(opcode, params);
}

#endif

static inline scarlett_hresult_t audio_pe_stub_dispatch_local(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_audio_report_stub_params_t params;
    audio_pe_zero_mem(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    if (desc != NULL) {
        params.ctx.flags = desc->flags;
        params.ctx.ordinal = desc->ordinal;
        params.ctx.vtable_slot = desc->vtable_slot;
        params.ctx.return_hr = scarlett_stub_enforce_error(desc->default_error);
        audio_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), desc->dll_name);
        audio_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), desc->symbol_name);
    } else {
        params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
        params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
        params.ctx.return_hr = SCARLETT_ERR_UNIMPLEMENTED_STUB;
        audio_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), "unknown.dll");
        audio_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), "unknown_symbol");
    }

    params.ctx.return_address = return_address;
    params.ctx.caller_module_base = 0u;
    params.ctx.caller_rva = return_address;
    audio_pe_copy_cstr(params.ctx.caller_module_name,
                       sizeof(params.ctx.caller_module_name),
                       "pe64_caller");
    params.ctx.rcx = rcx;
    params.ctx.rdx = rdx;
    params.ctx.r8 = r8;
    params.ctx.r9 = r9;

    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_REPORT_STUB, &params);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        __builtin_trap();
    }
    return scarlett_stub_enforce_error(params.ctx.return_hr);
}

#if defined(_WIN32) || defined(__MINGW32__)
#ifndef DLL_PROCESS_ATTACH
#define DLL_PROCESS_ATTACH 1u
#endif

static inline uint32_t audio_pe_dll_main(uint32_t reason) {
    if (reason == DLL_PROCESS_ATTACH) {
        if (__wine_init_unix_call() != 0) {
            return 0u;
        }
        scarlett_unix_init_params_t init_params;
        audio_pe_zero_mem(&init_params, sizeof(init_params));
        init_params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
        init_params.module_id = (uint32_t)SCARLETT_UNIX_MOD_AUDIO;
        init_params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
        (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_INIT, &init_params);
    }
    return 1u;
}
#endif

#endif /* SCARLETT_AUDIO_PE_COMMON_H */
