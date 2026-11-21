/*
 * Proton-Scarlett — xgameplatform.dll PE64 Shim & WINAPI_FAMILY_GAMES API-Set Resolver
 *
 * Strictly freestanding C11:
 *   - Zero direct references to libc, POSIX, or libvulkan.
 *   - Crosses PE <-> unixlib boundary ONLY via WINE_UNIX_CALL / SCARLETT_UNIX_CALL.
 *   - Compiles identically with:
 *       clang --target=x86_64-w64-mingw32 -std=c11 -ffreestanding -c
 *     and on Linux host via the bridge for unit testing.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(SCARLETT_USE_WINE_UNIXLIB)
#include <wine/unixlib.h>
#endif

#include "loader_unix_abi.h"
#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

/* ============================================================================
 * Freestanding Memory & String Helpers (Zero libc dependencies)
 * ============================================================================ */

static void scarlett_zero_mem(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0u;
    }
}

static void scarlett_copy_cstr_trunc(char *dst, size_t cap, const char *src) {
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

static char scarlett_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static size_t scarlett_cstr_len(const char *s) {
    if (s == NULL) {
        return 0u;
    }
    size_t n = 0u;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static bool scarlett_cstr_iequals(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    size_t i = 0u;
    while (a[i] != '\0' && b[i] != '\0') {
        if (scarlett_ascii_lower(a[i]) != scarlett_ascii_lower(b[i])) {
            return false;
        }
        ++i;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static bool scarlett_cstr_istarts_with(const char *str, const char *prefix) {
    if (str == NULL || prefix == NULL) {
        return false;
    }
    size_t i = 0u;
    while (prefix[i] != '\0') {
        if (str[i] == '\0' ||
            scarlett_ascii_lower(str[i]) != scarlett_ascii_lower(prefix[i])) {
            return false;
        }
        ++i;
    }
    return true;
}

/* ============================================================================
 * WINE_UNIX_CALL Bridge Layer (PE64 Wine ABI or Host Test Bridge)
 * ============================================================================ */

#if defined(WINE_UNIX_CALL)

static int32_t scarlett_pe_unix_call(uint32_t opcode, void *params) {
    return SCARLETT_UNIX_CALL(opcode, params);
}

#elif defined(_WIN32) || defined(__MINGW32__)

/* Wine 11.0 unixlib PE64 ABI symbols */
typedef uint64_t scarlett_wine_unixlib_handle_t;
extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern int32_t __wine_init_unix_call(void);
extern int32_t (SCARLETT_WIN64_API *__wine_unix_call_dispatcher)(
    scarlett_wine_unixlib_handle_t handle,
    uint32_t code,
    void *args);

static int32_t scarlett_pe_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, opcode, params);
}

#else

static scarlett_loader_unix_bridge_fn_t g_host_unix_bridge = NULL;

void scarlett_loader_pe_set_unix_bridge(scarlett_loader_unix_bridge_fn_t bridge_fn) {
    g_host_unix_bridge = bridge_fn;
}

scarlett_loader_unix_bridge_fn_t scarlett_loader_pe_get_unix_bridge(void) {
    return g_host_unix_bridge;
}

static int32_t scarlett_pe_unix_call(uint32_t opcode, void *params) {
    if (g_host_unix_bridge == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return g_host_unix_bridge(opcode, params);
}

#endif

/* ============================================================================
 * Diagnostic Auto-Stub Trampoline Implementation (scarlett_stub_thunk.h)
 * ============================================================================ */

scarlett_hresult_t scarlett_stub_dispatch_win64(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_loader_report_stub_params_t params;
    scarlett_zero_mem(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    if (desc != NULL) {
        params.ctx.flags = desc->flags;
        params.ctx.ordinal = desc->ordinal;
        params.ctx.vtable_slot = desc->vtable_slot;
        params.ctx.return_hr = scarlett_stub_enforce_error(desc->default_error);
        scarlett_copy_cstr_trunc(params.ctx.dll_name, sizeof(params.ctx.dll_name), desc->dll_name);
        scarlett_copy_cstr_trunc(params.ctx.symbol_name, sizeof(params.ctx.symbol_name),
                                 desc->symbol_name);
    } else {
        params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
        params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
        params.ctx.return_hr = SCARLETT_ERR_UNIMPLEMENTED_STUB;
        scarlett_copy_cstr_trunc(params.ctx.dll_name, sizeof(params.ctx.dll_name), "unknown.dll");
        scarlett_copy_cstr_trunc(params.ctx.symbol_name, sizeof(params.ctx.symbol_name),
                                 "unknown_symbol");
    }

    params.ctx.return_address = return_address;
    params.ctx.caller_module_base = 0u;
    params.ctx.caller_rva = return_address;
    scarlett_copy_cstr_trunc(params.ctx.caller_module_name,
                             sizeof(params.ctx.caller_module_name),
                             "pe64_caller");
    params.ctx.rcx = rcx;
    params.ctx.rdx = rdx;
    params.ctx.r8 = r8;
    params.ctx.r9 = r9;

    (void)scarlett_pe_unix_call(SCARLETT_LOADER_UNIX_REPORT_STUB, &params);
    if (params.out_action == (uint32_t)SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        __builtin_trap();
    }
    return scarlett_stub_enforce_error(params.ctx.return_hr);
}

/* ============================================================================
 * WINAPI_FAMILY_GAMES API-Set Resolver & Forwarding Table
 * ============================================================================ */

typedef struct scarlett_apiset_entry_s {
    const char *prefix;
    const char *target_dll;
} scarlett_apiset_entry_t;

static const scarlett_apiset_entry_t g_winapi_family_games_apisets[] = {
    { "api-ms-win-core-psm-appnotify-l1-1",      "api-ms-win-core-psm-appnotify-l1-1-0.dll" },
    { "api-ms-win-gaming-expandedresources-l1",  "kernelx.dll" },
    { "api-ms-win-gaming-deviceinformation-l1",  "xgameplatform.dll" },
    { "api-ms-win-gaming-processcreation-l1",    "kernelx.dll" },
    { "ext-ms-win-gaming-xbl-l1",                "xgameruntime.dll" },
    { "ext-ms-win-gaming-xinput-l1",             "GameInput.dll" },
    { "api-ms-win-core-xstate-l2",               "kernelbase.dll" },
    { "api-ms-win-core-memory-l1-1",             "xmem.dll" },
    { "api-ms-win-core-synch-l1-2",              "kernelbase.dll" },
    { "api-ms-win-core-threadpool-l1-2",         "kernelbase.dll" },
    { "api-ms-win-core-com-l1-1",                "ole32.dll" },
    { "xgameplatform",                           "xgameplatform.dll" },
};

static const char *strip_dll_ext(const char *name, char *buf, size_t cap) {
    size_t len = scarlett_cstr_len(name);
    if (len >= 4u &&
        scarlett_cstr_iequals(name + len - 4u, ".dll")) {
        len -= 4u;
    }
    if (len >= cap) {
        len = cap - 1u;
    }
    for (size_t i = 0u; i < len; ++i) {
        buf[i] = name[i];
    }
    buf[len] = '\0';
    return buf;
}

scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformResolveApiSet(
    const char *api_set_name,
    char *out_target_dll,
    uint32_t out_target_cap) {
    if (api_set_name == NULL || out_target_dll == NULL || out_target_cap == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    char normalized[128];
    strip_dll_ext(api_set_name, normalized, sizeof(normalized));

    size_t count = sizeof(g_winapi_family_games_apisets) /
                   sizeof(g_winapi_family_games_apisets[0]);
    for (size_t i = 0u; i < count; ++i) {
        if (scarlett_cstr_istarts_with(normalized, g_winapi_family_games_apisets[i].prefix)) {
            size_t need = scarlett_cstr_len(g_winapi_family_games_apisets[i].target_dll) + 1u;
            if (need > out_target_cap) {
                return SCARLETT_E_INVALIDARG;
            }
            scarlett_copy_cstr_trunc(out_target_dll, out_target_cap,
                                     g_winapi_family_games_apisets[i].target_dll);
            return SCARLETT_OK;
        }
    }

    out_target_dll[0] = '\0';
    return SCARLETT_E_NOINTERFACE;
}

scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformGetTitleMetadata(
    scarlett_loader_title_metadata_t *out_metadata) {
    if (out_metadata == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_zero_mem(out_metadata, sizeof(*out_metadata));
    return scarlett_pe_unix_call(SCARLETT_LOADER_UNIX_EX_GET_METADATA, out_metadata);
}

scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformGetPackageFullName(
    char *out_buffer,
    uint32_t out_cap) {
    if (out_buffer == NULL || out_cap == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_loader_title_metadata_t meta;
    scarlett_hresult_t hr = XGamePlatformGetTitleMetadata(&meta);
    if (scarlett_failed(hr)) {
        out_buffer[0] = '\0';
        return hr;
    }
    size_t need = scarlett_cstr_len(meta.package_full_name) + 1u;
    if (need > out_cap) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_copy_cstr_trunc(out_buffer, out_cap, meta.package_full_name);
    return SCARLETT_OK;
}

uint32_t SCARLETT_WIN64_API XGamePlatformGetConsoleModel(void) {
    scarlett_loader_title_metadata_t meta;
    if (scarlett_succeeded(XGamePlatformGetTitleMetadata(&meta))) {
        return meta.console_model;
    }
    return (uint32_t)SCARLETT_MODEL_ANACONDA;
}

/* Unimplemented export routed through diagnostic auto-stub thunk (0 silent stubs) */
static const scarlett_stub_descriptor_t g_stub_create_activation_ctx = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "xgameplatform.dll",
    "XGamePlatformCreateActivationContext"
};

scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformCreateActivationContext(
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3) {
    uint64_t ret_addr = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return scarlett_stub_dispatch_win64(
        &g_stub_create_activation_ctx,
        ret_addr,
        arg0,
        arg1,
        arg2,
        arg3);
}

#if defined(_WIN32) || defined(__MINGW32__)
#ifndef DLL_PROCESS_ATTACH
#define DLL_PROCESS_ATTACH 1u
#endif

uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
#if defined(WINE_UNIX_CALL) || defined(_WIN32) || defined(__MINGW32__)
        if (__wine_init_unix_call() != 0) {
            return 0u;
        }
#endif
        scarlett_unix_init_params_t init_params;
        scarlett_zero_mem(&init_params, sizeof(init_params));
        init_params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
        init_params.module_id = (uint32_t)SCARLETT_UNIX_MOD_LOADER;
        init_params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
        (void)scarlett_pe_unix_call(SCARLETT_LOADER_UNIX_INIT, &init_params);
    }
    return 1u;
}
#endif
