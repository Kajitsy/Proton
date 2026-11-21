/*
 * Proton-Scarlett — Diagnostic Auto-Stub Trampoline Contract
 *
 * Enforces the Zero Silent Stubs policy:
 *   - Every unimplemented PE64 export or scarlett-xdl vtable slot MUST route
 *     through a diagnostic thunk carrying `scarlett_stub_descriptor_t`.
 *   - The thunk captures Win64 argument registers (RCX, RDX, R8, R9),
 *     caller return address & module RVA, DLL name, symbol name / ordinal,
 *     and active `PROTON_XBOX_STRICT` mode into `scarlett_stub_context_t`.
 *   - Under `PROTON_XBOX_STRICT=0`: logs full context and returns a non-zero
 *     error HRESULT (never S_OK).
 *   - Under `PROTON_XBOX_STRICT=1`: logs full context and immediately terminates
 *     execution with `SCARLETT_ERR_STRICT_STUB_ABORT`.
 *
 * Clean-room, freestanding C11 / C++20 header (zero libc/Vulkan/uring deps).
 */

#ifndef SCARLETT_STUB_THUNK_H
#define SCARLETT_STUB_THUNK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCARLETT_STUB_THUNK_ABI_VERSION UINT32_C(1)
#define SCARLETT_STUB_NAME_MAX          UINT32_C(128)
#define SCARLETT_STUB_ORDINAL_NONE      UINT32_C(0)
#define SCARLETT_STUB_SLOT_NONE         UINT32_C(0xFFFFFFFF)

typedef enum scarlett_stub_action_e {
    /* PROTON_XBOX_STRICT=0: emit diagnostic record and return failure HRESULT */
    SCARLETT_STUB_ACTION_LOG_AND_FAIL  = 0,
    /* PROTON_XBOX_STRICT=1: emit diagnostic record and halt process immediately */
    SCARLETT_STUB_ACTION_LOG_AND_ABORT = 1
} scarlett_stub_action_t;

typedef enum scarlett_stub_flags_e {
    SCARLETT_STUB_FLAG_NONE           = 0u,
    SCARLETT_STUB_FLAG_ORDINAL_EXPORT = (1u << 0),
    SCARLETT_STUB_FLAG_XDL_VTABLE     = (1u << 1),
    SCARLETT_STUB_FLAG_REMOVED_IN_ABI = (1u << 2),
    SCARLETT_STUB_FLAG_STRICT_MODE    = (1u << 3)
} scarlett_stub_flags_t;

/*
 * Static read-only descriptor emitted per unimplemented export or XDL slot.
 * Never permits a success HRESULT (`default_error` must be < 0).
 */
typedef struct scarlett_stub_descriptor_s {
    uint32_t abi_version;
    uint32_t flags;
    uint32_t ordinal;
    uint32_t vtable_slot;
    scarlett_hresult_t default_error;
    uint32_t reserved;
    const char *dll_name;
    const char *symbol_name;
} scarlett_stub_descriptor_t;

/*
 * Self-contained runtime diagnostic record captured by the Win64 auto-stub
 * trampoline. Fixed-width layout allows direct zero-chase forwarding across
 * WINE_UNIX_CALL (`SCARLETT_LOADER_UNIX_REPORT_STUB`).
 */
typedef struct scarlett_stub_context_s {
    uint32_t abi_version;
    uint32_t flags;
    uint32_t ordinal;
    uint32_t vtable_slot;
    uint32_t thread_id;
    uint32_t strict_mode;                  /* 0 or 1 (PROTON_XBOX_STRICT) */
    uint64_t caller_rva;                   /* return_address - caller_module_base */
    uint64_t caller_module_base;
    uint64_t return_address;
    uint64_t rcx;                          /* Win64 arg0 / this / api_id */
    uint64_t rdx;                          /* Win64 arg1 / riid */
    uint64_t r8;                           /* Win64 arg2 / out_impl */
    uint64_t r9;                           /* Win64 arg3 */
    scarlett_hresult_t return_hr;          /* Guaranteed < 0 (never SCARLETT_OK) */
    uint32_t hit_count;                    /* Monotonic invocation count */
    char dll_name[SCARLETT_STUB_NAME_MAX];
    char symbol_name[SCARLETT_STUB_NAME_MAX];
    char caller_module_name[SCARLETT_STUB_NAME_MAX];
} scarlett_stub_context_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_stub_context_t) == 472,
                       "scarlett_stub_context_t must have exact 472-byte ABI layout");

/* Diagnostic sink callback implemented by PE runtime or unit-test harness */
typedef scarlett_stub_action_t (*scarlett_stub_sink_fn_t)(
    const scarlett_stub_context_t *ctx,
    void *user_data);

/*
 * Guarantees no misconfigured descriptor can silently return success.
 * Any non-negative code is rewritten to SCARLETT_ERR_UNIMPLEMENTED_STUB.
 */
static inline scarlett_hresult_t
scarlett_stub_enforce_error(scarlett_hresult_t candidate_hr) {
    return scarlett_failed(candidate_hr) ? candidate_hr
                                         : SCARLETT_ERR_UNIMPLEMENTED_STUB;
}

static inline scarlett_stub_action_t
scarlett_stub_resolve_action(const scarlett_stub_context_t *ctx) {
    if (ctx != NULL && ctx->strict_mode != 0u) {
        return SCARLETT_STUB_ACTION_LOG_AND_ABORT;
    }
    return SCARLETT_STUB_ACTION_LOG_AND_FAIL;
}

/*
 * Win64 assembly thunk entry contract (implemented in PE64 runtime):
 *   - Invoked with descriptor pointer in R10 (or via generated stub prologue)
 *   - Saves volatile argument registers RCX, RDX, R8, R9 and return address [RSP]
 *   - Maintains 16-byte stack alignment and 32-byte Win64 shadow space
 *   - Populates `scarlett_stub_context_t`, resolves caller module & RVA,
 *     dispatches to diagnostic sink, and either aborts (strict_mode != 0)
 *     or returns `return_hr` in RAX (strict_mode == 0).
 */
scarlett_hresult_t scarlett_stub_dispatch_win64(
    const scarlett_stub_descriptor_t *desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_STUB_THUNK_H */
