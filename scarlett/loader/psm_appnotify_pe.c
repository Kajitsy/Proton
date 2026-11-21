/*
 * Proton-Scarlett — api-ms-win-core-psm-appnotify-l1-1-0.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - Zero direct references to libc, POSIX, or libvulkan.
 *   - Crosses PE <-> unixlib boundary ONLY via WINE_UNIX_CALL / SCARLETT_UNIX_CALL.
 *   - Delivers real Suspend / Resume / Constrained events to registered callbacks.
 *   - Routes unimplemented exports via scarlett_stub_thunk.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(SCARLETT_USE_WINE_UNIXLIB)
#include <wine/unixlib.h>
#endif

#include "loader_unix_abi.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

#define SCARLETT_WIN32_ERROR_SUCCESS           UINT32_C(0)
#define SCARLETT_WIN32_ERROR_INVALID_PARAMETER UINT32_C(87)
#define SCARLETT_WIN32_ERROR_GEN_FAILURE       UINT32_C(31)

static void psm_zero_mem(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (size_t i = 0; i < len; ++i) {
        p[i] = 0u;
    }
}

#if defined(WINE_UNIX_CALL)

static int32_t psm_unix_call(uint32_t opcode, void *params) {
    return SCARLETT_UNIX_CALL(opcode, params);
}

#elif defined(_WIN32) || defined(__MINGW32__)

typedef uint64_t scarlett_wine_unixlib_handle_t;
extern scarlett_wine_unixlib_handle_t __wine_unixlib_handle;
extern int32_t (SCARLETT_WIN64_API *__wine_unix_call_dispatcher)(
    scarlett_wine_unixlib_handle_t handle,
    uint32_t code,
    void *args);

static int32_t psm_unix_call(uint32_t opcode, void *params) {
    if (__wine_unix_call_dispatcher == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return __wine_unix_call_dispatcher(__wine_unixlib_handle, opcode, params);
}

#else

static int32_t psm_unix_call(uint32_t opcode, void *params) {
    scarlett_loader_unix_bridge_fn_t fn = scarlett_loader_pe_get_unix_bridge();
    if (fn == NULL) {
        return SCARLETT_ERR_UNIX_BRIDGE_UNAVAILABLE;
    }
    return fn(opcode, params);
}

#endif

static uint32_t psm_register_common(
    scarlett_psm_callback_fn_t routine,
    void *context,
    void **registration,
    uint32_t notify_kind) {
    if (routine == NULL || registration == NULL) {
        return SCARLETT_WIN32_ERROR_INVALID_PARAMETER;
    }
    *registration = NULL;

    scarlett_psm_register_params_t params;
    psm_zero_mem(&params, sizeof(params));
    params.callback_addr = (uint64_t)(uintptr_t)routine;
    params.context_addr = (uint64_t)(uintptr_t)context;
    params.notify_kind = notify_kind;

    int32_t hr = psm_unix_call(SCARLETT_LOADER_UNIX_EX_PSM_REGISTER, &params);
    if (scarlett_failed(hr) || scarlett_failed(params.out_status)) {
        return SCARLETT_WIN32_ERROR_GEN_FAILURE;
    }

    *registration = (void *)(uintptr_t)params.out_registration_handle;
    return SCARLETT_WIN32_ERROR_SUCCESS;
}

static void psm_unregister_common(void *registration, uint32_t notify_kind) {
    if (registration == NULL) {
        return;
    }
    scarlett_psm_unregister_params_t params;
    psm_zero_mem(&params, sizeof(params));
    params.registration_handle = (uint64_t)(uintptr_t)registration;
    params.notify_kind = notify_kind;
    (void)psm_unix_call(SCARLETT_LOADER_UNIX_EX_PSM_UNREGISTER, &params);
}

uint32_t SCARLETT_WIN64_API RegisterAppStateChangeNotification(
    scarlett_psm_callback_fn_t routine,
    void *context,
    void **registration) {
    return psm_register_common(
        routine, context, registration, (uint32_t)SCARLETT_PSM_NOTIFY_APP_STATE);
}

void SCARLETT_WIN64_API UnregisterAppStateChangeNotification(void *registration) {
    psm_unregister_common(registration, (uint32_t)SCARLETT_PSM_NOTIFY_APP_STATE);
}

uint32_t SCARLETT_WIN64_API RegisterAppConstrainedChangeNotification(
    scarlett_psm_callback_fn_t routine,
    void *context,
    void **registration) {
    return psm_register_common(
        routine, context, registration, (uint32_t)SCARLETT_PSM_NOTIFY_CONSTRAINED);
}

void SCARLETT_WIN64_API UnregisterAppConstrainedChangeNotification(void *registration) {
    psm_unregister_common(registration, (uint32_t)SCARLETT_PSM_NOTIFY_CONSTRAINED);
}

scarlett_hresult_t SCARLETT_WIN64_API ScarlettPsmDispatchEvent(uint32_t event_type) {
    scarlett_psm_cb_entry_t entries[SCARLETT_PSM_MAX_SNAPSHOT_CALLBACKS];
    psm_zero_mem(entries, sizeof(entries));

    scarlett_psm_snapshot_params_t params;
    psm_zero_mem(&params, sizeof(params));
    params.event_type = event_type;
    params.out_entries_addr = (uint64_t)(uintptr_t)entries;
    params.capacity = SCARLETT_PSM_MAX_SNAPSHOT_CALLBACKS;

    int32_t hr = psm_unix_call(SCARLETT_LOADER_UNIX_EX_PSM_SNAPSHOT_CB, &params);
    if (scarlett_failed(hr) || scarlett_failed(params.out_status)) {
        return scarlett_failed(params.out_status) ? params.out_status : hr;
    }

    uint8_t state_flag =
        (event_type == (uint32_t)SCARLETT_PSM_EVENT_SUSPEND ||
         event_type == (uint32_t)SCARLETT_PSM_EVENT_CONSTRAINED)
            ? 1u
            : 0u;

    uint32_t invoke_count = params.out_count;
    if (invoke_count > SCARLETT_PSM_MAX_SNAPSHOT_CALLBACKS) {
        invoke_count = SCARLETT_PSM_MAX_SNAPSHOT_CALLBACKS;
    }
    for (uint32_t i = 0u; i < invoke_count; ++i) {
        scarlett_psm_callback_fn_t cb =
            (scarlett_psm_callback_fn_t)(uintptr_t)entries[i].callback_addr;
        void *ctx = (void *)(uintptr_t)entries[i].context_addr;
        if (cb != NULL) {
            cb(state_flag, ctx);
        }
    }
    return SCARLETT_OK;
}

static const scarlett_stub_descriptor_t g_stub_psm_register_ex = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "api-ms-win-core-psm-appnotify-l1-1-0.dll",
    "RegisterAppStateChangeNotificationEx"
};

scarlett_hresult_t SCARLETT_WIN64_API RegisterAppStateChangeNotificationEx(
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3) {
    uint64_t ret_addr = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return scarlett_stub_dispatch_win64(
        &g_stub_psm_register_ex,
        ret_addr,
        arg0,
        arg1,
        arg2,
        arg3);
}
