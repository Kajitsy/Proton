/*
 * Proton-Scarlett — Clean-Room PE64 Diagnostic Auto-Stub Runtime for *_APP.dll
 *
 * Enforces the Zero Silent Stubs contract (`scarlett_stub_thunk.h`):
 *   - Every UWP-only symbol missing from Wine's target .spec routes through
 *     a Win64 naked trampoline into `scarlett_app_stub_dispatch_win64`.
 *   - Captures DLL name, symbol/ordinal, caller module & RVA, and RCX/RDX/R8/R9.
 *   - Under PROTON_XBOX_STRICT=0: logs diagnostic record and returns negative
 *     HRESULT (never S_OK).
 *   - Under PROTON_XBOX_STRICT=1: logs diagnostic record and aborts via
 *     ExitProcess(SCARLETT_ERR_STRICT_STUB_ABORT).
 *   - Clean-room ONLY: PE64 path uses Win32 kernel32 APIs exclusively (zero POSIX).
 */

#ifndef SCARLETT_APP_STUB_RUNTIME_H
#define SCARLETT_APP_STUB_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct scarlett_app_stub_runtime_state_s {
    volatile uint32_t hit_counter;
    volatile int32_t strict_override; /* -1 = env, 0 = off, 1 = on */
    scarlett_stub_context_t last_context;
    scarlett_stub_sink_fn_t sink;
    void *sink_user_data;
} scarlett_app_stub_runtime_state_t;

static scarlett_app_stub_runtime_state_t g_scarlett_app_stub_state = {
    0u, -1, {0}, NULL, NULL
};

static inline void scarlett_app_copy_bounded(char *dst, size_t cap, const char *src) {
    size_t i = 0u;
    if (dst == NULL || cap == 0u) {
        return;
    }
    if (src != NULL) {
        while (i + 1u < cap && src[i] != '\0') {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

static inline size_t scarlett_app_append_cstr(char *buf, size_t cap, size_t pos, const char *s) {
    if (buf == NULL || cap == 0u || s == NULL) {
        return pos;
    }
    while (*s != '\0' && pos + 1u < cap) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    return pos;
}

static inline size_t scarlett_app_append_hex64(char *buf, size_t cap, size_t pos, uint64_t val, int width) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[16];
    int n = 0;
    if (val == 0u) {
        tmp[n++] = '0';
    } else {
        while (val != 0u && n < 16) {
            tmp[n++] = hex[val & 0xFu];
            val >>= 4;
        }
    }
    while (n < width && n < 16) {
        tmp[n++] = '0';
    }
    pos = scarlett_app_append_cstr(buf, cap, pos, "0x");
    while (n > 0 && pos + 1u < cap) {
        buf[pos++] = tmp[--n];
    }
    buf[pos] = '\0';
    return pos;
}

static inline size_t scarlett_app_append_u32(char *buf, size_t cap, size_t pos, uint32_t val) {
    char tmp[12];
    int n = 0;
    if (val == 0u) {
        tmp[n++] = '0';
    } else {
        while (val != 0u && n < 12) {
            tmp[n++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    while (n > 0 && pos + 1u < cap) {
        buf[pos++] = tmp[--n];
    }
    buf[pos] = '\0';
    return pos;
}

static inline void scarlett_app_stub_set_sink(scarlett_stub_sink_fn_t sink, void *user_data) {
    g_scarlett_app_stub_state.sink = sink;
    g_scarlett_app_stub_state.sink_user_data = user_data;
}

static inline void scarlett_app_stub_set_strict_override(int32_t mode) {
    g_scarlett_app_stub_state.strict_override = mode;
}

static inline void scarlett_app_stub_get_last_context(scarlett_stub_context_t *out_ctx) {
    if (out_ctx != NULL) {
        *out_ctx = g_scarlett_app_stub_state.last_context;
    }
}

static inline void scarlett_app_stub_reset_stats(void) {
    uint8_t *p = (uint8_t *)&g_scarlett_app_stub_state.last_context;
    g_scarlett_app_stub_state.hit_counter = 0u;
    for (size_t i = 0; i < sizeof(g_scarlett_app_stub_state.last_context); ++i) {
        p[i] = 0u;
    }
}

static inline int scarlett_app_is_strict_enabled(void) {
    int32_t ov = g_scarlett_app_stub_state.strict_override;
    if (ov == 0) {
        return 0;
    }
    if (ov > 0) {
        return 1;
    }
#ifdef _WIN32
    char env[16];
    DWORD n = GetEnvironmentVariableA("PROTON_XBOX_STRICT", env, (DWORD)sizeof(env));
    if (n == 0u || n >= sizeof(env)) {
        return 0;
    }
    return (env[0] == '1' && env[1] == '\0') ||
           (env[0] == 't' && env[1] == 'r' && env[2] == 'u' && env[3] == 'e' && env[4] == '\0') ||
           (env[0] == 'T' && env[1] == 'R' && env[2] == 'U' && env[3] == 'E' && env[4] == '\0');
#else
    const char *env = getenv("PROTON_XBOX_STRICT");
    if (env == NULL || env[0] == '\0') {
        return 0;
    }
    return (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "TRUE") == 0);
#endif
}

static inline void scarlett_app_resolve_caller(
    uint64_t return_address,
    uint64_t *out_base,
    uint64_t *out_rva,
    char *out_name,
    size_t out_cap) {
    *out_base = 0u;
    *out_rva = return_address;
    scarlett_app_copy_bounded(out_name, out_cap, "<unknown>");

#ifdef _WIN32
    HMODULE mod = NULL;
    if (return_address != 0u &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)return_address,
                           &mod) &&
        mod != NULL) {
        *out_base = (uint64_t)(uintptr_t)mod;
        if (return_address >= *out_base) {
            *out_rva = return_address - *out_base;
        }
        char mod_path[260];
        DWORD len = GetModuleFileNameA(mod, mod_path, (DWORD)sizeof(mod_path));
        if (len > 0u && len < sizeof(mod_path)) {
            const char *base_name = mod_path;
            for (DWORD i = 0u; i < len; ++i) {
                if (mod_path[i] == '\\' || mod_path[i] == '/') {
                    base_name = &mod_path[i + 1u];
                }
            }
            scarlett_app_copy_bounded(out_name, out_cap, base_name);
        }
    }
#endif
}

static inline void scarlett_app_log_stub(const scarlett_stub_context_t *ctx) {
    char msg[640];
    size_t pos = 0u;
    msg[0] = '\0';

    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, "[SCARLETT-STUB][");
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, ctx->strict_mode ? "STRICT" : "FAIL");
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, "] dll=");
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, ctx->dll_name);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " symbol=");
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, ctx->symbol_name);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " ord=");
    pos = scarlett_app_append_u32(msg, sizeof(msg), pos, ctx->ordinal);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " caller=");
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, ctx->caller_module_name);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " rva=");
    pos = scarlett_app_append_hex64(msg, sizeof(msg), pos, ctx->caller_rva, 0);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " RCX=");
    pos = scarlett_app_append_hex64(msg, sizeof(msg), pos, ctx->rcx, 16);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " RDX=");
    pos = scarlett_app_append_hex64(msg, sizeof(msg), pos, ctx->rdx, 16);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " R8=");
    pos = scarlett_app_append_hex64(msg, sizeof(msg), pos, ctx->r8, 16);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " R9=");
    pos = scarlett_app_append_hex64(msg, sizeof(msg), pos, ctx->r9, 16);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, " hr=");
    pos = scarlett_app_append_hex64(msg, sizeof(msg), pos, (uint64_t)(uint32_t)ctx->return_hr, 8);
    pos = scarlett_app_append_cstr(msg, sizeof(msg), pos, "\n");

#ifdef _WIN32
    OutputDebugStringA(msg);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != NULL && hErr != INVALID_HANDLE_VALUE) {
        DWORD written = 0u;
        WriteFile(hErr, msg, (DWORD)pos, &written, NULL);
    }
#else
    fputs(msg, stderr);
    fflush(stderr);
#endif
}

static inline scarlett_hresult_t scarlett_app_stub_dispatch_win64(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    scarlett_stub_context_t ctx;
    uint8_t *raw = (uint8_t *)&ctx;
    for (size_t i = 0; i < sizeof(ctx); ++i) {
        raw[i] = 0u;
    }

    int strict = scarlett_app_is_strict_enabled();
#ifdef _WIN32
    uint32_t hit = (uint32_t)__atomic_add_fetch(
        &g_scarlett_app_stub_state.hit_counter, 1u, __ATOMIC_ACQ_REL);
    uint32_t tid = (uint32_t)GetCurrentThreadId();
#else
    uint32_t hit = ++g_scarlett_app_stub_state.hit_counter;
    uint32_t tid = 1u;
#endif

    ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    ctx.flags = (desc != NULL) ? desc->flags : (uint32_t)SCARLETT_STUB_FLAG_NONE;
    if (strict) {
        ctx.flags |= (uint32_t)SCARLETT_STUB_FLAG_STRICT_MODE;
    }
    ctx.ordinal = (desc != NULL) ? desc->ordinal : SCARLETT_STUB_ORDINAL_NONE;
    ctx.vtable_slot = (desc != NULL) ? desc->vtable_slot : SCARLETT_STUB_SLOT_NONE;
    ctx.thread_id = tid;
    ctx.strict_mode = strict ? 1u : 0u;
    ctx.return_address = return_address;
    scarlett_app_resolve_caller(
        return_address,
        &ctx.caller_module_base,
        &ctx.caller_rva,
        ctx.caller_module_name,
        sizeof(ctx.caller_module_name));
    ctx.rcx = rcx;
    ctx.rdx = rdx;
    ctx.r8 = r8;
    ctx.r9 = r9;

    scarlett_hresult_t raw_hr =
        (desc != NULL) ? desc->default_error : SCARLETT_ERR_UNIMPLEMENTED_STUB;
    ctx.return_hr = strict ? SCARLETT_ERR_STRICT_STUB_ABORT
                           : scarlett_stub_enforce_error(raw_hr);
    ctx.hit_count = hit;

    scarlett_app_copy_bounded(
        ctx.dll_name, sizeof(ctx.dll_name),
        (desc != NULL && desc->dll_name != NULL) ? desc->dll_name : "<unknown.dll>");
    scarlett_app_copy_bounded(
        ctx.symbol_name, sizeof(ctx.symbol_name),
        (desc != NULL && desc->symbol_name != NULL) ? desc->symbol_name : "<unknown>");

    g_scarlett_app_stub_state.last_context = ctx;
    scarlett_app_log_stub(&ctx);

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&ctx);
    if (g_scarlett_app_stub_state.sink != NULL) {
        action = g_scarlett_app_stub_state.sink(&ctx, g_scarlett_app_stub_state.sink_user_data);
    }

    if (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
#ifdef _WIN32
        ExitProcess((UINT)SCARLETT_ERR_STRICT_STUB_ABORT);
#else
        return SCARLETT_ERR_STRICT_STUB_ABORT;
#endif
    }

    return scarlett_stub_enforce_error(ctx.return_hr);
}

#if defined(_WIN32) && defined(__x86_64__)
/* Non-static entry point so naked stub thunks can `jmp` to it cleanly */
__attribute__((used, noinline)) scarlett_hresult_t scarlett_app_stub_dispatch_entry(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    return scarlett_app_stub_dispatch_win64(desc, return_address, rcx, rdx, r8, r9);
}

__attribute__((naked, used)) void scarlett_app_stub_trampoline_win64(void) {
    __asm__ __volatile__(
        "sub $56, %rsp\n\t"
        "mov %r9, 40(%rsp)\n\t"
        "mov %r8, 32(%rsp)\n\t"
        "mov %rdx, %r9\n\t"
        "mov %rcx, %r8\n\t"
        "mov 56(%rsp), %rdx\n\t"
        "mov %r10, %rcx\n\t"
        "call scarlett_app_stub_dispatch_entry\n\t"
        "add $56, %rsp\n\t"
        "ret\n\t"
    );
}

#define SCARLETT_APP_DEFINE_STUB(dll_lit, sym, ord, default_hr)              \
    const scarlett_stub_descriptor_t scarlett_stub_desc_##sym                \
        __attribute__((used)) = {                                            \
            SCARLETT_STUB_THUNK_ABI_VERSION,                                 \
            ((ord) != 0u) ? (uint32_t)SCARLETT_STUB_FLAG_ORDINAL_EXPORT      \
                          : (uint32_t)SCARLETT_STUB_FLAG_NONE,               \
            (uint32_t)(ord),                                                 \
            SCARLETT_STUB_SLOT_NONE,                                         \
            (default_hr),                                                    \
            0u,                                                              \
            (dll_lit),                                                       \
            #sym                                                             \
        };                                                                   \
    __attribute__((naked)) scarlett_hresult_t scarlett_stub_fn_##sym(void) {   \
        __asm__ __volatile__(                                                \
            "lea scarlett_stub_desc_" #sym "(%rip), %r10\n\t"                \
            "jmp scarlett_app_stub_trampoline_win64\n\t"                     \
        );                                                                   \
    }
#else
#define SCARLETT_APP_DEFINE_STUB(dll_lit, sym, ord, default_hr)              \
    static const scarlett_stub_descriptor_t scarlett_stub_desc_##sym = {     \
        SCARLETT_STUB_THUNK_ABI_VERSION,                                     \
        ((ord) != 0u) ? (uint32_t)SCARLETT_STUB_FLAG_ORDINAL_EXPORT          \
                      : (uint32_t)SCARLETT_STUB_FLAG_NONE,                   \
        (uint32_t)(ord),                                                     \
        SCARLETT_STUB_SLOT_NONE,                                             \
        (default_hr),                                                        \
        0u,                                                                  \
        (dll_lit),                                                           \
        #sym                                                                 \
    };                                                                       \
    scarlett_hresult_t scarlett_stub_fn_##sym(                               \
        uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {                \
        return scarlett_app_stub_dispatch_win64(                             \
            &scarlett_stub_desc_##sym,                                       \
            (uint64_t)(uintptr_t)__builtin_return_address(0),                \
            a0, a1, a2, a3);                                                 \
    }
#endif

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_APP_STUB_RUNTIME_H */
