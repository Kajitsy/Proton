/*
 * Proton-Scarlett — WinRT UI Common Infrastructure & Dispatch
 *
 * Implements shared locking, event lists, HSTRING/IInspectable helpers,
 * live Win32 display metrics, and uwp_winrt_ui_get_activation_factory().
 */

#include "winrt_ui_internal.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ============================================================================
 * Memory Allocation
 * ============================================================================ */

void *uwp_ui_alloc(size_t bytes) {
    return calloc(1, bytes);
}

void uwp_ui_free(void *ptr) {
    free(ptr);
}

/* ============================================================================
 * Thread-Safe Locking
 * ============================================================================ */

void uwp_ui_lock_acquire(uwp_ui_lock_t *lock) {
#ifdef _WIN32
    if (lock->state != 2) {
        for (;;) {
            LONG prev = InterlockedCompareExchange(&lock->state, 1, 0);
            if (prev == 0) {
                InitializeCriticalSection(&lock->cs);
                InterlockedExchange(&lock->state, 2);
                break;
            }
            if (prev == 2) {
                break;
            }
            Sleep(0);
        }
    }
    EnterCriticalSection(&lock->cs);
#else
    while (atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire)) {
        /* spin briefly on host test contention */
    }
#endif
}

void uwp_ui_lock_release(uwp_ui_lock_t *lock) {
#ifdef _WIN32
    LeaveCriticalSection(&lock->cs);
#else
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
#endif
}

/* ============================================================================
 * Event Registration List
 * ============================================================================ */

HRESULT uwp_ui_event_add(uwp_ui_event_list_t *list, UwpEventHandler *handler, EventRegistrationToken *out_token) {
    uint32_t i;

    if (!list || !handler || !out_token) {
        return E_INVALIDARG;
    }

    uwp_ui_lock_acquire(&list->lock);
    for (i = 0; i < list->count; ++i) {
        if (list->entries[i].handler == NULL) {
            break;
        }
    }
    if (i == list->count) {
        if (list->count >= UWP_UI_MAX_EVENT_LISTENERS) {
            uwp_ui_lock_release(&list->lock);
            uwp_bridge_log("winrt_ui", "Event listener table full (%u)", UWP_UI_MAX_EVENT_LISTENERS);
            return E_OUTOFMEMORY;
        }
        list->count++;
    }

    if (list->next_token <= 0) {
        list->next_token = 1;
    }
    int64_t tok = list->next_token++;
    handler->lpVtbl->AddRef(handler);
    list->entries[i].token = tok;
    list->entries[i].handler = handler;
    out_token->value = tok;
    uwp_ui_lock_release(&list->lock);
    return S_OK;
}

HRESULT uwp_ui_event_remove(uwp_ui_event_list_t *list, EventRegistrationToken token) {
    UwpEventHandler *removed = NULL;

    if (!list) {
        return E_INVALIDARG;
    }
    if (token.value == 0) {
        return S_OK;
    }

    uwp_ui_lock_acquire(&list->lock);
    for (uint32_t i = 0; i < list->count; ++i) {
        if (list->entries[i].token == token.value && list->entries[i].handler != NULL) {
            removed = list->entries[i].handler;
            list->entries[i].handler = NULL;
            list->entries[i].token = 0;
            break;
        }
    }
    uwp_ui_lock_release(&list->lock);

    if (removed) {
        removed->lpVtbl->Release(removed);
    }
    return S_OK;
}

void uwp_ui_event_invoke(uwp_ui_event_list_t *list, void *sender, void *args) {
    UwpEventHandler *snapshot[UWP_UI_MAX_EVENT_LISTENERS];
    uint32_t snap_count = 0;

    if (!list) {
        return;
    }

    uwp_ui_lock_acquire(&list->lock);
    for (uint32_t i = 0; i < list->count; ++i) {
        if (list->entries[i].handler != NULL) {
            list->entries[i].handler->lpVtbl->AddRef(list->entries[i].handler);
            snapshot[snap_count++] = list->entries[i].handler;
        }
    }
    uwp_ui_lock_release(&list->lock);

    for (uint32_t i = 0; i < snap_count; ++i) {
        snapshot[i]->lpVtbl->Invoke(snapshot[i], sender, args);
        snapshot[i]->lpVtbl->Release(snapshot[i]);
    }
}

void uwp_ui_event_clear(uwp_ui_event_list_t *list) {
    UwpEventHandler *snapshot[UWP_UI_MAX_EVENT_LISTENERS];
    uint32_t snap_count = 0;

    if (!list) {
        return;
    }

    uwp_ui_lock_acquire(&list->lock);
    for (uint32_t i = 0; i < list->count; ++i) {
        if (list->entries[i].handler != NULL) {
            snapshot[snap_count++] = list->entries[i].handler;
            list->entries[i].handler = NULL;
            list->entries[i].token = 0;
        }
    }
    list->count = 0;
    uwp_ui_lock_release(&list->lock);

    for (uint32_t i = 0; i < snap_count; ++i) {
        snapshot[i]->lpVtbl->Release(snapshot[i]);
    }
}

/* ============================================================================
 * HSTRING Helpers (Dynamic combase/uwphost resolution on PE64, host fallback)
 * ============================================================================ */

#ifdef _WIN32
typedef HRESULT (WINAPI *pfn_WindowsCreateString)(LPCWSTR, UINT32, HSTRING *);
typedef PCWSTR  (WINAPI *pfn_WindowsGetStringRawBuffer)(HSTRING, UINT32 *);
typedef HRESULT (WINAPI *pfn_WindowsDeleteString)(HSTRING);
typedef LPVOID  (WINAPI *pfn_CoTaskMemAlloc)(SIZE_T);

static void *uwp_ui_resolve_winstring_proc(const char *name) {
    static const wchar_t *const candidates[] = {
        L"combase.dll",
        L"api-ms-win-core-winrt-string-l1-1-0.dll",
        L"UWPHOST.dll"
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        HMODULE mod = GetModuleHandleW(candidates[i]);
        if (mod) {
            FARPROC p = GetProcAddress(mod, name);
            if (p) return (void *)p;
        }
    }
    HMODULE self_mod = NULL;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&uwp_ui_resolve_winstring_proc,
            &self_mod) && self_mod) {
        FARPROC p = GetProcAddress(self_mod, name);
        if (p) return (void *)p;
    }
    HMODULE combase = LoadLibraryW(L"combase.dll");
    if (combase) {
        FARPROC p = GetProcAddress(combase, name);
        if (p) return (void *)p;
    }
    return NULL;
}
#else
#define UWP_HOST_HSTRING_MAGIC UINT32_C(0x55575048)
typedef struct uwp_host_hstring_s {
    uint32_t magic;
    uint32_t length;
    wchar_t  chars[1];
} uwp_host_hstring_t;
#endif

HRESULT uwp_ui_hstring_create(const wchar_t *str, uint32_t len, HSTRING *out_hstr) {
    if (!out_hstr) {
        return E_INVALIDARG;
    }
    *out_hstr = NULL;
    if (len == 0 || !str) {
        return S_OK;
    }

#ifdef _WIN32
    pfn_WindowsCreateString fn = (pfn_WindowsCreateString)uwp_ui_resolve_winstring_proc("WindowsCreateString");
    if (!fn) {
        uwp_bridge_log("winrt_ui", "WindowsCreateString unavailable");
        return E_NOTIMPL;
    }
    return fn(str, len, out_hstr);
#else
    uwp_host_hstring_t *hs = (uwp_host_hstring_t *)calloc(1, sizeof(uwp_host_hstring_t) + (size_t)len * sizeof(wchar_t));
    if (!hs) {
        return E_OUTOFMEMORY;
    }
    hs->magic = UWP_HOST_HSTRING_MAGIC;
    hs->length = len;
    memcpy(hs->chars, str, (size_t)len * sizeof(wchar_t));
    hs->chars[len] = L'\0';
    *out_hstr = (HSTRING)hs;
    return S_OK;
#endif
}

const wchar_t *uwp_ui_hstring_raw(HSTRING hstr, uint32_t *out_len) {
    if (out_len) {
        *out_len = 0;
    }
    if (!hstr) {
        return L"";
    }

#ifdef _WIN32
    pfn_WindowsGetStringRawBuffer fn =
        (pfn_WindowsGetStringRawBuffer)uwp_ui_resolve_winstring_proc("WindowsGetStringRawBuffer");
    if (!fn) {
        return L"";
    }
    UINT32 ulen = 0;
    const wchar_t *raw = fn(hstr, &ulen);
    if (out_len) {
        *out_len = ulen;
    }
    return raw ? raw : L"";
#else
    const uwp_host_hstring_t *hs = (const uwp_host_hstring_t *)hstr;
    if (hs->magic == UWP_HOST_HSTRING_MAGIC) {
        if (out_len) {
            *out_len = hs->length;
        }
        return hs->chars;
    }
    const wchar_t *fallback = (const wchar_t *)hstr;
    if (out_len) {
        *out_len = (uint32_t)wcslen(fallback);
    }
    return fallback;
#endif
}

void uwp_ui_hstring_delete(HSTRING hstr) {
    if (!hstr) {
        return;
    }
#ifdef _WIN32
    pfn_WindowsDeleteString fn =
        (pfn_WindowsDeleteString)uwp_ui_resolve_winstring_proc("WindowsDeleteString");
    if (fn) {
        fn(hstr);
    }
#else
    uwp_host_hstring_t *hs = (uwp_host_hstring_t *)hstr;
    if (hs->magic == UWP_HOST_HSTRING_MAGIC) {
        hs->magic = 0;
        free(hs);
    }
#endif
}

/* ============================================================================
 * IInspectable & Diagnostic Helpers
 * ============================================================================ */

HRESULT uwp_ui_get_runtime_class_name(const wchar_t *class_name, HSTRING *out_name) {
    if (!out_name) {
        return E_POINTER;
    }
    if (!class_name) {
        *out_name = NULL;
        return S_OK;
    }
    return uwp_ui_hstring_create(class_name, (uint32_t)wcslen(class_name), out_name);
}

HRESULT uwp_ui_get_iids(const GUID *iids, ULONG count, ULONG *out_count, IID **out_iids) {
    if (!out_count || !out_iids) {
        return E_POINTER;
    }
    *out_count = 0;
    *out_iids = NULL;

    if (count == 0 || !iids) {
        return S_OK;
    }

    size_t sz = (size_t)count * sizeof(IID);
    IID *buf = NULL;
#ifdef _WIN32
    HMODULE ole32 = GetModuleHandleW(L"ole32.dll");
    if (!ole32) {
        ole32 = LoadLibraryW(L"ole32.dll");
    }
    pfn_CoTaskMemAlloc alloc_fn = ole32 ? (pfn_CoTaskMemAlloc)(void *)GetProcAddress(ole32, "CoTaskMemAlloc") : NULL;
    buf = alloc_fn ? (IID *)alloc_fn(sz) : (IID *)malloc(sz);
#else
    buf = (IID *)malloc(sz);
#endif
    if (!buf) {
        return E_OUTOFMEMORY;
    }
    memcpy(buf, iids, sz);
    *out_count = count;
    *out_iids = buf;
    return S_OK;
}

HRESULT uwp_ui_unimpl(const char *iface_name, const char *method_name) {
    uwp_bridge_log("winrt_ui", "%s::%s is not implemented -> returning E_NOTIMPL",
                   iface_name ? iface_name : "<unknown>",
                   method_name ? method_name : "<unknown>");
    return E_NOTIMPL;
}

/* ============================================================================
 * Live Display Metrics
 * ============================================================================ */

UwpResolutionScale uwp_ui_dpi_to_resolution_scale(float dpi) {
    int pct = (int)((dpi / 96.0f) * 100.0f + 0.5f);
    if (pct <= 100) return UwpResolutionScale_Scale100Percent;
    if (pct <= 120) return UwpResolutionScale_Scale120Percent;
    if (pct <= 125) return UwpResolutionScale_Scale125Percent;
    if (pct <= 140) return UwpResolutionScale_Scale140Percent;
    if (pct <= 150) return UwpResolutionScale_Scale150Percent;
    if (pct <= 160) return UwpResolutionScale_Scale160Percent;
    if (pct <= 175) return UwpResolutionScale_Scale175Percent;
    if (pct <= 180) return UwpResolutionScale_Scale180Percent;
    if (pct <= 200) return UwpResolutionScale_Scale200Percent;
    if (pct <= 225) return UwpResolutionScale_Scale225Percent;
    if (pct <= 250) return UwpResolutionScale_Scale250Percent;
    if (pct <= 300) return UwpResolutionScale_Scale300Percent;
    if (pct <= 350) return UwpResolutionScale_Scale350Percent;
    if (pct <= 400) return UwpResolutionScale_Scale400Percent;
    if (pct <= 450) return UwpResolutionScale_Scale450Percent;
    return UwpResolutionScale_Scale500Percent;
}

void uwp_ui_query_display_metrics(uwp_ui_display_metrics_t *out_metrics) {
    uint32_t w = 0;
    uint32_t h = 0;
    float dpi = 96.0f;
    double refresh_hz = 60.0;
    uint16_t bpp = 32;

    if (!out_metrics) {
        return;
    }

#ifdef _WIN32
    typedef UINT (WINAPI *pfn_GetDpiForWindow)(HWND);
    HWND hwnd = uwp_core_get_main_hwnd();

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 && hwnd) {
        pfn_GetDpiForWindow pGetDpiForWindow =
            (pfn_GetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");
        if (pGetDpiForWindow) {
            UINT win_dpi = pGetDpiForWindow(hwnd);
            if (win_dpi >= 48 && win_dpi <= 960) {
                dpi = (float)win_dpi;
            }
        }
    }
    if (dpi == 96.0f) {
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            int log_x = GetDeviceCaps(hdc, LOGPIXELSX);
            if (log_x >= 48 && log_x <= 960) {
                dpi = (float)log_x;
            }
            ReleaseDC(hwnd, hdc);
        }
    }

    MONITORINFOEXW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY)
                        : MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    if (mon && GetMonitorInfoW(mon, (MONITORINFO *)&mi)) {
        LONG mw = mi.rcMonitor.right - mi.rcMonitor.left;
        LONG mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        if (mw > 0 && mh > 0) {
            w = (uint32_t)mw;
            h = (uint32_t)mh;
        }
        DEVMODEW dm;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            if (dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0) {
                w = dm.dmPelsWidth;
                h = dm.dmPelsHeight;
            }
            if (dm.dmDisplayFrequency > 1) {
                refresh_hz = (double)dm.dmDisplayFrequency;
            }
            if (dm.dmBitsPerPel > 0) {
                bpp = (uint16_t)dm.dmBitsPerPel;
            }
        }
    }
    if (w == 0 || h == 0) {
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        if (sx > 0 && sy > 0) {
            w = (uint32_t)sx;
            h = (uint32_t)sy;
        }
    }
#endif

    if (w == 0 || h == 0) {
        uwp_core_get_client_size(&w, &h);
    }
    if (w == 0 || h == 0) {
        w = 1280;
        h = 720;
    }

    out_metrics->width_px = w;
    out_metrics->height_px = h;
    out_metrics->logical_dpi = dpi;
    out_metrics->refresh_hz = refresh_hz;
    out_metrics->bits_per_pixel = bpp;
    out_metrics->is_portrait = (h > w) ? 1 : 0;
}

/* ============================================================================
 * WinRT UI Activation Factory Dispatch
 * ============================================================================ */

HRESULT uwp_winrt_ui_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory) {
    if (!out_factory) {
        return E_POINTER;
    }
    *out_factory = NULL;
    if (!class_name || !iid) {
        return E_INVALIDARG;
    }

    if (wcscmp(class_name, L"Windows.Graphics.Display.DisplayInformation") == 0) {
        return uwp_ui_display_info_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.UI.ViewManagement.ApplicationView") == 0) {
        return uwp_ui_app_view_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.UI.Core.SystemNavigationManager") == 0) {
        return uwp_ui_sys_nav_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.UI.Core.CoreCursor") == 0) {
        return uwp_ui_core_cursor_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Devices.Input.MouseDevice") == 0) {
        return uwp_ui_mouse_device_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.System.Display.DisplayRequest") == 0) {
        return uwp_ui_display_request_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Foundation.Metadata.ApiInformation") == 0) {
        return uwp_ui_api_info_get_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Graphics.Display.Core.HdmiDisplayInformation") == 0) {
        return uwp_ui_hdmi_display_get_factory(iid, out_factory);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}
