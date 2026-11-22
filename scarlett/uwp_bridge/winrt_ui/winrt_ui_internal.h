/*
 * Proton-Scarlett — WinRT UI Internal Shared Helpers
 *
 * Clean-room C11 helpers shared across all winrt_ui classes.
 * Zero POSIX dependencies; compiles on MinGW PE64 and host Linux tests.
 */

#ifndef SCARLETT_UWP_WINRT_UI_INTERNAL_H
#define SCARLETT_UWP_WINRT_UI_INTERNAL_H

#include "winrt_ui_abi.h"

#ifndef _WIN32
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) \
    ((type *)((char *)(address) - offsetof(type, field)))
#endif

/* ============================================================================
 * Thread-Safe Refcounting & Locking (Win32 on PE64, C11 atomics on host)
 * ============================================================================ */

static inline ULONG uwp_ui_atomic_inc(volatile int32_t *val) {
#ifdef _WIN32
    return (ULONG)InterlockedIncrement((volatile LONG *)val);
#else
    return (ULONG)__atomic_add_fetch(val, 1, __ATOMIC_ACQ_REL);
#endif
}

static inline ULONG uwp_ui_atomic_dec(volatile int32_t *val) {
#ifdef _WIN32
    return (ULONG)InterlockedDecrement((volatile LONG *)val);
#else
    return (ULONG)__atomic_sub_fetch(val, 1, __ATOMIC_ACQ_REL);
#endif
}

typedef struct uwp_ui_lock_s {
#ifdef _WIN32
    CRITICAL_SECTION cs;
    volatile LONG    state; /* 0 = uninit, 1 = initializing, 2 = ready */
#else
    atomic_flag      flag;
#endif
} uwp_ui_lock_t;

#ifdef _WIN32
#define UWP_UI_LOCK_INIT { { 0 }, 0 }
#else
#define UWP_UI_LOCK_INIT { ATOMIC_FLAG_INIT }
#endif

void uwp_ui_lock_acquire(uwp_ui_lock_t *lock);
void uwp_ui_lock_release(uwp_ui_lock_t *lock);

/* ============================================================================
 * Event Listener List
 * ============================================================================ */

#define UWP_UI_MAX_EVENT_LISTENERS 64

typedef struct uwp_ui_event_entry_s {
    int64_t          token;
    UwpEventHandler *handler;
} uwp_ui_event_entry_t;

typedef struct uwp_ui_event_list_s {
    uwp_ui_lock_t        lock;
    int64_t              next_token;
    uint32_t             count;
    uwp_ui_event_entry_t entries[UWP_UI_MAX_EVENT_LISTENERS];
} uwp_ui_event_list_t;

#define UWP_UI_EVENT_LIST_INIT { UWP_UI_LOCK_INIT, 1, 0, { { 0, NULL } } }

HRESULT uwp_ui_event_add(uwp_ui_event_list_t *list, UwpEventHandler *handler, EventRegistrationToken *out_token);
HRESULT uwp_ui_event_remove(uwp_ui_event_list_t *list, EventRegistrationToken token);
void    uwp_ui_event_invoke(uwp_ui_event_list_t *list, void *sender, void *args);
void    uwp_ui_event_clear(uwp_ui_event_list_t *list);

/* ============================================================================
 * HSTRING, IInspectable, Allocation & Diagnostic Helpers
 * ============================================================================ */

HRESULT     uwp_ui_hstring_create(const wchar_t *str, uint32_t len, HSTRING *out_hstr);
const wchar_t *uwp_ui_hstring_raw(HSTRING hstr, uint32_t *out_len);
void        uwp_ui_hstring_delete(HSTRING hstr);

HRESULT uwp_ui_get_runtime_class_name(const wchar_t *class_name, HSTRING *out_name);
HRESULT uwp_ui_get_iids(const GUID *iids, ULONG count, ULONG *out_count, IID **out_iids);
HRESULT uwp_ui_unimpl(const char *iface_name, const char *method_name);

void *uwp_ui_alloc(size_t bytes);
void  uwp_ui_free(void *ptr);

/* ============================================================================
 * Live Display Metrics (shared by DisplayInformation & HdmiDisplayInformation)
 * ============================================================================ */

typedef struct uwp_ui_display_metrics_s {
    uint32_t width_px;
    uint32_t height_px;
    float    logical_dpi;
    double   refresh_hz;
    uint16_t bits_per_pixel;
    int32_t  is_portrait;
} uwp_ui_display_metrics_t;

void uwp_ui_query_display_metrics(uwp_ui_display_metrics_t *out_metrics);
UwpResolutionScale uwp_ui_dpi_to_resolution_scale(float dpi);

/* ============================================================================
 * Per-Class Factory Entry Points
 * ============================================================================ */

HRESULT uwp_ui_display_info_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_app_view_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_sys_nav_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_core_cursor_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_mouse_device_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_display_request_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_api_info_get_factory(REFIID iid, void **out_factory);
HRESULT uwp_ui_hdmi_display_get_factory(REFIID iid, void **out_factory);

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_UWP_WINRT_UI_INTERNAL_H */
