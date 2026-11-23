/*
 * Proton-Scarlett — Windows.UI.ViewManagement.ApplicationView
 *
 * Clean-room implementation of IApplicationViewStatics/2/3/Fullscreen/Interop
 * and IApplicationView/2/3/4 + ApplicationViewTitleBar backed by uwp_core_* and Win32 HWND.
 */

#include "winrt_ui_internal.h"

typedef struct app_titlebar_obj_s {
    UwpIApplicationViewTitleBar iface;
    volatile int32_t            ref_count;
    void                       *colors[12];
} app_titlebar_obj_t;

typedef struct app_view_obj_s {
    UwpIApplicationView  av1_iface;
    UwpIApplicationView2 av2_iface;
    UwpIApplicationView3 av3_iface;
    UwpIApplicationView4 av4_iface;
    volatile int32_t     ref_count;
} app_view_obj_t;

typedef struct app_view_statics_s {
    UwpIApplicationViewStatics           st1_iface;
    UwpIApplicationViewStatics2          st2_iface;
    UwpIApplicationViewStatics3          st3_iface;
    UwpIApplicationViewFullscreenStatics fs_iface;
    UwpIApplicationViewInteropStatics    interop_iface;
    IActivationFactory                   act_iface;
    volatile int32_t                     ref_count;
} app_view_statics_t;

static uwp_ui_lock_t                  g_av_lock = UWP_UI_LOCK_INIT;
static app_view_obj_t                *g_av_singleton = NULL;
static app_titlebar_obj_t            *g_tb_singleton = NULL;
static wchar_t                        g_cached_title[512] = L"";
static boolean                        g_terminate_on_final_close = TRUE;
static UwpApplicationViewWindowingMode g_launch_windowing_mode = UwpApplicationViewWindowingMode_Auto;
static UwpSize                        g_launch_view_size = { 1280.0f, 720.0f };
static UwpSize                        g_preferred_min_size = { 192.0f, 48.0f };
static UwpSize                        g_logical_view_size = { 0.0f, 0.0f };
static boolean                        g_screen_capture_enabled = TRUE;
static boolean                        g_suppress_overlays = FALSE;
static UwpApplicationViewBoundsMode   g_bounds_mode = UwpApplicationViewBoundsMode_UseVisible;
static UwpFullScreenSystemOverlayMode g_fs_overlay_mode = UwpFullScreenSystemOverlayMode_Standard;

static uwp_ui_event_list_t g_consolidated_events = UWP_UI_EVENT_LIST_INIT;
static uwp_ui_event_list_t g_visible_bounds_events = UWP_UI_EVENT_LIST_INIT;

/* ============================================================================
 * ApplicationViewTitleBar
 * ============================================================================ */

static HRESULT tb_set_prop(void **slot, void *val) {
    if (*slot == val) return S_OK;
    if (val) ((IUnknown *)val)->lpVtbl->AddRef((IUnknown *)val);
    if (*slot) ((IUnknown *)*slot)->lpVtbl->Release((IUnknown *)*slot);
    *slot = val;
    return S_OK;
}

static HRESULT tb_get_prop(void *slot, void **out) {
    if (!out) return E_POINTER;
    *out = slot;
    if (slot) ((IUnknown *)slot)->lpVtbl->AddRef((IUnknown *)slot);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tb_QueryInterface(UwpIApplicationViewTitleBar *This, REFIID riid, void **ppv) {
    app_titlebar_obj_t *tb = CONTAINING_RECORD(This, app_titlebar_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IApplicationViewTitleBar)) {
        *ppv = &tb->iface;
        uwp_ui_atomic_inc(&tb->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE tb_AddRef(UwpIApplicationViewTitleBar *This) {
    app_titlebar_obj_t *tb = CONTAINING_RECORD(This, app_titlebar_obj_t, iface);
    return uwp_ui_atomic_inc(&tb->ref_count);
}
static ULONG STDMETHODCALLTYPE tb_Release(UwpIApplicationViewTitleBar *This) {
    app_titlebar_obj_t *tb = CONTAINING_RECORD(This, app_titlebar_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&tb->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_av_lock);
        if (uwp_ui_atomic_load(&tb->ref_count) == 0) {
            if (g_tb_singleton == tb) g_tb_singleton = NULL;
            uwp_ui_lock_release(&g_av_lock);
            for (int i = 0; i < 12; ++i) {
                if (tb->colors[i]) ((IUnknown *)tb->colors[i])->lpVtbl->Release((IUnknown *)tb->colors[i]);
            }
            uwp_ui_free(tb);
            return 0;
        }
        uwp_ui_lock_release(&g_av_lock);
    }
    return ref;
}
static HRESULT STDMETHODCALLTYPE tb_GetIids(UwpIApplicationViewTitleBar *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IApplicationViewTitleBar };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE tb_GetRuntimeClassName(UwpIApplicationViewTitleBar *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationViewTitleBar", name);
}
static HRESULT STDMETHODCALLTYPE tb_GetTrustLevel(UwpIApplicationViewTitleBar *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

#define DEFINE_TB_COLOR_ACCESSORS(idx, name) \
static HRESULT STDMETHODCALLTYPE tb_put_##name(UwpIApplicationViewTitleBar *This, void *v) { \
    app_titlebar_obj_t *tb = CONTAINING_RECORD(This, app_titlebar_obj_t, iface); \
    return tb_set_prop(&tb->colors[idx], v); \
} \
static HRESULT STDMETHODCALLTYPE tb_get_##name(UwpIApplicationViewTitleBar *This, void **v) { \
    app_titlebar_obj_t *tb = CONTAINING_RECORD(This, app_titlebar_obj_t, iface); \
    return tb_get_prop(tb->colors[idx], v); \
}

DEFINE_TB_COLOR_ACCESSORS(0,  ForegroundColor)
DEFINE_TB_COLOR_ACCESSORS(1,  BackgroundColor)
DEFINE_TB_COLOR_ACCESSORS(2,  ButtonForegroundColor)
DEFINE_TB_COLOR_ACCESSORS(3,  ButtonBackgroundColor)
DEFINE_TB_COLOR_ACCESSORS(4,  ButtonHoverForegroundColor)
DEFINE_TB_COLOR_ACCESSORS(5,  ButtonHoverBackgroundColor)
DEFINE_TB_COLOR_ACCESSORS(6,  ButtonPressedForegroundColor)
DEFINE_TB_COLOR_ACCESSORS(7,  ButtonPressedBackgroundColor)
DEFINE_TB_COLOR_ACCESSORS(8,  InactiveForegroundColor)
DEFINE_TB_COLOR_ACCESSORS(9,  InactiveBackgroundColor)
DEFINE_TB_COLOR_ACCESSORS(10, ButtonInactiveForegroundColor)
DEFINE_TB_COLOR_ACCESSORS(11, ButtonInactiveBackgroundColor)

static const UwpIApplicationViewTitleBarVtbl g_tb_vtbl = {
    tb_QueryInterface, tb_AddRef, tb_Release,
    tb_GetIids, tb_GetRuntimeClassName, tb_GetTrustLevel,
    tb_put_ForegroundColor, tb_get_ForegroundColor,
    tb_put_BackgroundColor, tb_get_BackgroundColor,
    tb_put_ButtonForegroundColor, tb_get_ButtonForegroundColor,
    tb_put_ButtonBackgroundColor, tb_get_ButtonBackgroundColor,
    tb_put_ButtonHoverForegroundColor, tb_get_ButtonHoverForegroundColor,
    tb_put_ButtonHoverBackgroundColor, tb_get_ButtonHoverBackgroundColor,
    tb_put_ButtonPressedForegroundColor, tb_get_ButtonPressedForegroundColor,
    tb_put_ButtonPressedBackgroundColor, tb_get_ButtonPressedBackgroundColor,
    tb_put_InactiveForegroundColor, tb_get_InactiveForegroundColor,
    tb_put_InactiveBackgroundColor, tb_get_InactiveBackgroundColor,
    tb_put_ButtonInactiveForegroundColor, tb_get_ButtonInactiveForegroundColor,
    tb_put_ButtonInactiveBackgroundColor, tb_get_ButtonInactiveBackgroundColor
};

/* ============================================================================
 * ApplicationView Instance
 * ============================================================================ */

static HRESULT av_qi(app_view_obj_t *obj, REFIID iid, void **out) {
    if (!iid || !out) return E_INVALIDARG;
    *out = NULL;

    if (IsEqualGUID(iid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(iid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(iid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(iid, &SCARLETT_IID_IApplicationView)) {
        *out = &obj->av1_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IApplicationView2)) {
        *out = &obj->av2_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IApplicationView3)) {
        *out = &obj->av3_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IApplicationView4)) {
        *out = &obj->av4_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&obj->ref_count);
    return S_OK;
}

static ULONG av_addref(app_view_obj_t *obj) {
    return uwp_ui_atomic_inc(&obj->ref_count);
}

static ULONG av_release(app_view_obj_t *obj) {
    ULONG ref = uwp_ui_atomic_dec(&obj->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_av_lock);
        if (uwp_ui_atomic_load(&obj->ref_count) == 0) {
            if (g_av_singleton == obj) g_av_singleton = NULL;
            uwp_ui_lock_release(&g_av_lock);
            uwp_ui_free(obj);
            return 0;
        }
        uwp_ui_lock_release(&g_av_lock);
    }
    return ref;
}

static HRESULT av_get_iids(ULONG *count, IID **iids) {
    const GUID list[] = {
        SCARLETT_IID_IApplicationView,
        SCARLETT_IID_IApplicationView2,
        SCARLETT_IID_IApplicationView3,
        SCARLETT_IID_IApplicationView4
    };
    return uwp_ui_get_iids(list, 4, count, iids);
}

/* --- IApplicationView (v1) --- */

static HRESULT STDMETHODCALLTYPE av1_QueryInterface(UwpIApplicationView *This, REFIID riid, void **ppv) {
    return av_qi(CONTAINING_RECORD(This, app_view_obj_t, av1_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE av1_AddRef(UwpIApplicationView *This) {
    return av_addref(CONTAINING_RECORD(This, app_view_obj_t, av1_iface));
}
static ULONG STDMETHODCALLTYPE av1_Release(UwpIApplicationView *This) {
    return av_release(CONTAINING_RECORD(This, app_view_obj_t, av1_iface));
}
static HRESULT STDMETHODCALLTYPE av1_GetIids(UwpIApplicationView *This, ULONG *c, IID **iids) {
    (void)This;
    return av_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE av1_GetRuntimeClassName(UwpIApplicationView *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE av1_GetTrustLevel(UwpIApplicationView *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_Orientation(UwpIApplicationView *This, UwpApplicationViewOrientation *value) {
    uint32_t w = 0, h = 0;
    (void)This;
    if (!value) return E_POINTER;
    uwp_core_get_client_size(&w, &h);
    if (w == 0 || h == 0) {
        uwp_ui_display_metrics_t m;
        uwp_ui_query_display_metrics(&m);
        w = m.width_px;
        h = m.height_px;
    }
    *value = (h > w) ? UwpApplicationViewOrientation_Portrait : UwpApplicationViewOrientation_Landscape;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_AdjacentToLeftDisplayEdge(UwpIApplicationView *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    if (uwp_core_is_fullscreen()) {
        *value = TRUE;
        return S_OK;
    }
#ifdef _WIN32
    HWND hwnd = uwp_core_get_main_hwnd();
    if (hwnd) {
        RECT wr;
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        if (GetWindowRect(hwnd, &wr) && mon && GetMonitorInfoW(mon, &mi)) {
            *value = (wr.left <= mi.rcWork.left + 8) ? TRUE : FALSE;
            return S_OK;
        }
    }
#endif
    *value = TRUE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_AdjacentToRightDisplayEdge(UwpIApplicationView *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    if (uwp_core_is_fullscreen()) {
        *value = TRUE;
        return S_OK;
    }
#ifdef _WIN32
    HWND hwnd = uwp_core_get_main_hwnd();
    if (hwnd) {
        RECT wr;
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        if (GetWindowRect(hwnd, &wr) && mon && GetMonitorInfoW(mon, &mi)) {
            *value = (wr.right >= mi.rcWork.right - 8) ? TRUE : FALSE;
            return S_OK;
        }
    }
#endif
    *value = TRUE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_IsFullScreen(UwpIApplicationView *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = uwp_core_is_fullscreen() ? TRUE : FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_IsOnLockScreen(UwpIApplicationView *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_IsScreenCaptureEnabled(UwpIApplicationView *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_screen_capture_enabled;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_put_IsScreenCaptureEnabled(UwpIApplicationView *This, boolean value) {
    (void)This;
    uwp_ui_lock_acquire(&g_av_lock);
    g_screen_capture_enabled = value ? TRUE : FALSE;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_put_Title(UwpIApplicationView *This, HSTRING value) {
    uint32_t len = 0;
    const wchar_t *raw = uwp_ui_hstring_raw(value, &len);
    const wchar_t *safe = raw ? raw : L"";
    (void)This;
    (void)len;
    uwp_ui_lock_acquire(&g_av_lock);
    size_t i = 0;
    for (; i + 1 < (sizeof(g_cached_title) / sizeof(g_cached_title[0])) && safe[i] != L'\0'; ++i) {
        g_cached_title[i] = safe[i];
    }
    g_cached_title[i] = L'\0';
    uwp_ui_lock_release(&g_av_lock);
    uwp_core_set_window_title(safe);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_get_Title(UwpIApplicationView *This, HSTRING *value) {
    wchar_t buf[512];
    (void)This;
    if (!value) return E_POINTER;
    buf[0] = L'\0';
    uwp_core_get_window_title(buf, (uint32_t)(sizeof(buf) / sizeof(buf[0])));
    if (buf[0] == L'\0') {
        uwp_ui_lock_acquire(&g_av_lock);
        size_t i = 0;
        for (; i + 1 < (sizeof(buf) / sizeof(buf[0])) && g_cached_title[i] != L'\0'; ++i) {
            buf[i] = g_cached_title[i];
        }
        buf[i] = L'\0';
        uwp_ui_lock_release(&g_av_lock);
    }
    return uwp_ui_hstring_create(buf, (uint32_t)wcslen(buf), value);
}
static HRESULT STDMETHODCALLTYPE av1_get_Id(UwpIApplicationView *This, INT32 *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av1_add_Consolidated(UwpIApplicationView *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_consolidated_events, handler, token);
}
static HRESULT STDMETHODCALLTYPE av1_remove_Consolidated(UwpIApplicationView *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_consolidated_events, token);
}

static const UwpIApplicationViewVtbl g_av1_vtbl = {
    av1_QueryInterface, av1_AddRef, av1_Release,
    av1_GetIids, av1_GetRuntimeClassName, av1_GetTrustLevel,
    av1_get_Orientation,
    av1_get_AdjacentToLeftDisplayEdge,
    av1_get_AdjacentToRightDisplayEdge,
    av1_get_IsFullScreen,
    av1_get_IsOnLockScreen,
    av1_get_IsScreenCaptureEnabled,
    av1_put_IsScreenCaptureEnabled,
    av1_put_Title,
    av1_get_Title,
    av1_get_Id,
    av1_add_Consolidated,
    av1_remove_Consolidated
};

/* --- IApplicationView2 --- */

static HRESULT STDMETHODCALLTYPE av2_QueryInterface(UwpIApplicationView2 *This, REFIID riid, void **ppv) {
    return av_qi(CONTAINING_RECORD(This, app_view_obj_t, av2_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE av2_AddRef(UwpIApplicationView2 *This) {
    return av_addref(CONTAINING_RECORD(This, app_view_obj_t, av2_iface));
}
static ULONG STDMETHODCALLTYPE av2_Release(UwpIApplicationView2 *This) {
    return av_release(CONTAINING_RECORD(This, app_view_obj_t, av2_iface));
}
static HRESULT STDMETHODCALLTYPE av2_GetIids(UwpIApplicationView2 *This, ULONG *c, IID **iids) {
    (void)This;
    return av_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE av2_GetRuntimeClassName(UwpIApplicationView2 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE av2_GetTrustLevel(UwpIApplicationView2 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av2_get_SuppressSystemOverlays(UwpIApplicationView2 *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_suppress_overlays;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av2_put_SuppressSystemOverlays(UwpIApplicationView2 *This, boolean value) {
    (void)This;
    uwp_ui_lock_acquire(&g_av_lock);
    g_suppress_overlays = value ? TRUE : FALSE;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av2_get_VisibleBounds(UwpIApplicationView2 *This, UwpRect *value) {
    uint32_t w = 0, h = 0;
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;

    uwp_ui_query_display_metrics(&m);
    uwp_core_get_client_size(&w, &h);

#ifdef _WIN32
    if (w == 0 || h == 0) {
        HWND hwnd = uwp_core_get_main_hwnd();
        RECT cr;
        if (hwnd && GetClientRect(hwnd, &cr)) {
            if (cr.right > cr.left) w = (uint32_t)(cr.right - cr.left);
            if (cr.bottom > cr.top) h = (uint32_t)(cr.bottom - cr.top);
        }
    }
#endif

    if (w == 0 || h == 0) {
        uwp_ui_lock_acquire(&g_av_lock);
        if (g_logical_view_size.Width > 0.0f && g_logical_view_size.Height > 0.0f) {
            value->X = 0.0f;
            value->Y = 0.0f;
            value->Width = g_logical_view_size.Width;
            value->Height = g_logical_view_size.Height;
            uwp_ui_lock_release(&g_av_lock);
            return S_OK;
        }
        uwp_ui_lock_release(&g_av_lock);
        w = m.width_px;
        h = m.height_px;
    }

    float scale = (m.logical_dpi > 0.0f) ? (96.0f / m.logical_dpi) : 1.0f;
    value->X = 0.0f;
    value->Y = 0.0f;
    value->Width = (FLOAT)w * scale;
    value->Height = (FLOAT)h * scale;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av2_add_VisibleBoundsChanged(UwpIApplicationView2 *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_visible_bounds_events, handler, token);
}
static HRESULT STDMETHODCALLTYPE av2_remove_VisibleBoundsChanged(UwpIApplicationView2 *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_visible_bounds_events, token);
}
static HRESULT STDMETHODCALLTYPE av2_SetDesiredBoundsMode(UwpIApplicationView2 *This, UwpApplicationViewBoundsMode mode, boolean *success) {
    (void)This;
    if (mode != UwpApplicationViewBoundsMode_UseVisible && mode != UwpApplicationViewBoundsMode_UseCoreWindow) {
        if (success) *success = FALSE;
        return E_INVALIDARG;
    }
    uwp_ui_lock_acquire(&g_av_lock);
    g_bounds_mode = mode;
    uwp_ui_lock_release(&g_av_lock);
    if (success) *success = TRUE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av2_get_DesiredBoundsMode(UwpIApplicationView2 *This, UwpApplicationViewBoundsMode *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_bounds_mode;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}

static const UwpIApplicationView2Vtbl g_av2_vtbl = {
    av2_QueryInterface, av2_AddRef, av2_Release,
    av2_GetIids, av2_GetRuntimeClassName, av2_GetTrustLevel,
    av2_get_SuppressSystemOverlays,
    av2_put_SuppressSystemOverlays,
    av2_get_VisibleBounds,
    av2_add_VisibleBoundsChanged,
    av2_remove_VisibleBoundsChanged,
    av2_SetDesiredBoundsMode,
    av2_get_DesiredBoundsMode
};

/* --- IApplicationView3 --- */

static HRESULT STDMETHODCALLTYPE av3_QueryInterface(UwpIApplicationView3 *This, REFIID riid, void **ppv) {
    return av_qi(CONTAINING_RECORD(This, app_view_obj_t, av3_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE av3_AddRef(UwpIApplicationView3 *This) {
    return av_addref(CONTAINING_RECORD(This, app_view_obj_t, av3_iface));
}
static ULONG STDMETHODCALLTYPE av3_Release(UwpIApplicationView3 *This) {
    return av_release(CONTAINING_RECORD(This, app_view_obj_t, av3_iface));
}
static HRESULT STDMETHODCALLTYPE av3_GetIids(UwpIApplicationView3 *This, ULONG *c, IID **iids) {
    (void)This;
    return av_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE av3_GetRuntimeClassName(UwpIApplicationView3 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE av3_GetTrustLevel(UwpIApplicationView3 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_get_TitleBar(UwpIApplicationView3 *This, UwpIApplicationViewTitleBar **value) {
    app_titlebar_obj_t *tb;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    if (!g_tb_singleton) {
        tb = (app_titlebar_obj_t *)uwp_ui_alloc(sizeof(app_titlebar_obj_t));
        if (tb) {
            tb->iface.lpVtbl = &g_tb_vtbl;
            tb->ref_count = 1;
            g_tb_singleton = tb;
        }
    } else {
        tb = g_tb_singleton;
        uwp_ui_atomic_inc(&tb->ref_count);
    }
    uwp_ui_lock_release(&g_av_lock);
    if (!tb) return E_OUTOFMEMORY;
    *value = &tb->iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_get_FullScreenSystemOverlayMode(UwpIApplicationView3 *This, UwpFullScreenSystemOverlayMode *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_fs_overlay_mode;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_put_FullScreenSystemOverlayMode(UwpIApplicationView3 *This, UwpFullScreenSystemOverlayMode value) {
    (void)This;
    uwp_ui_lock_acquire(&g_av_lock);
    g_fs_overlay_mode = value;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_get_IsFullScreenMode(UwpIApplicationView3 *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = uwp_core_is_fullscreen() ? TRUE : FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_TryEnterFullScreenMode(UwpIApplicationView3 *This, boolean *success) {
    int32_t res = uwp_core_set_fullscreen(1);
    boolean ok = (res != 0) || (uwp_core_is_fullscreen() != 0);
    if (success) *success = ok ? TRUE : FALSE;
    if (ok) {
        uwp_ui_event_invoke(&g_visible_bounds_events, &CONTAINING_RECORD(This, app_view_obj_t, av3_iface)->av1_iface, NULL);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_ExitFullScreenMode(UwpIApplicationView3 *This) {
    uwp_core_set_fullscreen(0);
    uwp_ui_event_invoke(&g_visible_bounds_events, &CONTAINING_RECORD(This, app_view_obj_t, av3_iface)->av1_iface, NULL);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_ShowStandardSystemOverlays(UwpIApplicationView3 *This) {
    (void)This;
    return uwp_ui_unimpl("IApplicationView3", "ShowStandardSystemOverlays");
}
static HRESULT STDMETHODCALLTYPE av3_TryResizeView(UwpIApplicationView3 *This, UwpSize value, boolean *success) {
    app_view_obj_t *obj = CONTAINING_RECORD(This, app_view_obj_t, av3_iface);
    uwp_ui_display_metrics_t m;

    if (value.Width <= 0.0f || value.Height <= 0.0f) {
        if (success) *success = FALSE;
        return S_OK;
    }

    uwp_ui_lock_acquire(&g_av_lock);
    if (value.Width < g_preferred_min_size.Width || value.Height < g_preferred_min_size.Height) {
        uwp_ui_lock_release(&g_av_lock);
        if (success) *success = FALSE;
        return S_OK;
    }
    g_logical_view_size = value;
    uwp_ui_lock_release(&g_av_lock);

    uwp_ui_query_display_metrics(&m);
#ifdef _WIN32
    HWND hwnd = uwp_core_get_main_hwnd();
    if (hwnd && !uwp_core_is_fullscreen()) {
        float scale = (m.logical_dpi > 0.0f) ? (m.logical_dpi / 96.0f) : 1.0f;
        int cw = (int)(value.Width * scale + 0.5f);
        int ch = (int)(value.Height * scale + 0.5f);
        RECT rc = { 0, 0, cw, ch };
        DWORD style = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
        DWORD ex_style = (DWORD)GetWindowLongW(hwnd, GWL_EXSTYLE);
        AdjustWindowRectEx(&rc, style, FALSE, ex_style);
        if (!SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
            if (success) *success = FALSE;
            return S_OK;
        }
    }
#endif
    (void)m;
    if (success) *success = TRUE;
    uwp_ui_event_invoke(&g_visible_bounds_events, &obj->av1_iface, NULL);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av3_SetPreferredMinSize(UwpIApplicationView3 *This, UwpSize size) {
    (void)This;
    if (size.Width < 0.0f || size.Height < 0.0f) {
        return E_INVALIDARG;
    }
    uwp_ui_lock_acquire(&g_av_lock);
    g_preferred_min_size = size;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}

static const UwpIApplicationView3Vtbl g_av3_vtbl = {
    av3_QueryInterface, av3_AddRef, av3_Release,
    av3_GetIids, av3_GetRuntimeClassName, av3_GetTrustLevel,
    av3_get_TitleBar,
    av3_get_FullScreenSystemOverlayMode,
    av3_put_FullScreenSystemOverlayMode,
    av3_get_IsFullScreenMode,
    av3_TryEnterFullScreenMode,
    av3_ExitFullScreenMode,
    av3_ShowStandardSystemOverlays,
    av3_TryResizeView,
    av3_SetPreferredMinSize
};

/* --- IApplicationView4 --- */

static HRESULT STDMETHODCALLTYPE av4_QueryInterface(UwpIApplicationView4 *This, REFIID riid, void **ppv) {
    return av_qi(CONTAINING_RECORD(This, app_view_obj_t, av4_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE av4_AddRef(UwpIApplicationView4 *This) {
    return av_addref(CONTAINING_RECORD(This, app_view_obj_t, av4_iface));
}
static ULONG STDMETHODCALLTYPE av4_Release(UwpIApplicationView4 *This) {
    return av_release(CONTAINING_RECORD(This, app_view_obj_t, av4_iface));
}
static HRESULT STDMETHODCALLTYPE av4_GetIids(UwpIApplicationView4 *This, ULONG *c, IID **iids) {
    (void)This;
    return av_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE av4_GetRuntimeClassName(UwpIApplicationView4 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE av4_GetTrustLevel(UwpIApplicationView4 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av4_get_ViewMode(UwpIApplicationView4 *This, UwpApplicationViewMode *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = UwpApplicationViewMode_Default;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av4_IsViewModeSupported(UwpIApplicationView4 *This, UwpApplicationViewMode mode, boolean *is_supported) {
    (void)This;
    if (!is_supported) return E_POINTER;
    *is_supported = (mode == UwpApplicationViewMode_Default) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE av4_TryEnterViewModeAsync(UwpIApplicationView4 *This, UwpApplicationViewMode mode, void **op) {
    (void)This; (void)mode;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IApplicationView4", "TryEnterViewModeAsync");
}
static HRESULT STDMETHODCALLTYPE av4_TryEnterViewModeWithPreferencesAsync(UwpIApplicationView4 *This, UwpApplicationViewMode mode, void *prefs, void **op) {
    (void)This; (void)mode; (void)prefs;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IApplicationView4", "TryEnterViewModeWithPreferencesAsync");
}
static HRESULT STDMETHODCALLTYPE av4_TryConsolidateAsync(UwpIApplicationView4 *This, void **op) {
    (void)This;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IApplicationView4", "TryConsolidateAsync");
}

static const UwpIApplicationView4Vtbl g_av4_vtbl = {
    av4_QueryInterface, av4_AddRef, av4_Release,
    av4_GetIids, av4_GetRuntimeClassName, av4_GetTrustLevel,
    av4_get_ViewMode,
    av4_IsViewModeSupported,
    av4_TryEnterViewModeAsync,
    av4_TryEnterViewModeWithPreferencesAsync,
    av4_TryConsolidateAsync
};

static app_view_obj_t *av_get_or_create_instance(void) {
    app_view_obj_t *obj;
    uwp_ui_lock_acquire(&g_av_lock);
    if (!g_av_singleton) {
        obj = (app_view_obj_t *)uwp_ui_alloc(sizeof(app_view_obj_t));
        if (obj) {
            obj->av1_iface.lpVtbl = &g_av1_vtbl;
            obj->av2_iface.lpVtbl = &g_av2_vtbl;
            obj->av3_iface.lpVtbl = &g_av3_vtbl;
            obj->av4_iface.lpVtbl = &g_av4_vtbl;
            obj->ref_count = 1;
            g_av_singleton = obj;
        }
    } else {
        obj = g_av_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_av_lock);
    return obj;
}

/* ============================================================================
 * ApplicationView Statics / Factory
 * ============================================================================ */

static HRESULT av_statics_qi(app_view_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IApplicationViewStatics2)) {
        *ppv = &fac->st2_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IApplicationViewStatics)) {
        *ppv = &fac->st1_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IApplicationViewStatics3)) {
        *ppv = &fac->st3_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IApplicationViewFullscreenStatics)) {
        *ppv = &fac->fs_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IApplicationViewInteropStatics)) {
        *ppv = &fac->interop_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static ULONG av_statics_addref(app_view_statics_t *fac) {
    return uwp_ui_atomic_inc(&fac->ref_count);
}
static ULONG av_statics_release(app_view_statics_t *fac) {
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT av_statics_get_iids(ULONG *c, IID **iids) {
    const GUID list[] = {
        SCARLETT_IID_IApplicationViewStatics,
        SCARLETT_IID_IApplicationViewStatics2,
        SCARLETT_IID_IApplicationViewStatics3,
        SCARLETT_IID_IApplicationViewFullscreenStatics,
        SCARLETT_IID_IApplicationViewInteropStatics,
        SCARLETT_IID_IActivationFactory
    };
    return uwp_ui_get_iids(list, 6, c, iids);
}

/* Statics1 */
static HRESULT STDMETHODCALLTYPE avs1_QueryInterface(UwpIApplicationViewStatics *This, REFIID riid, void **ppv) {
    return av_statics_qi(CONTAINING_RECORD(This, app_view_statics_t, st1_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE avs1_AddRef(UwpIApplicationViewStatics *This) {
    return av_statics_addref(CONTAINING_RECORD(This, app_view_statics_t, st1_iface));
}
static ULONG STDMETHODCALLTYPE avs1_Release(UwpIApplicationViewStatics *This) {
    return av_statics_release(CONTAINING_RECORD(This, app_view_statics_t, st1_iface));
}
static HRESULT STDMETHODCALLTYPE avs1_GetIids(UwpIApplicationViewStatics *This, ULONG *c, IID **iids) {
    (void)This; return av_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE avs1_GetRuntimeClassName(UwpIApplicationViewStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE avs1_GetTrustLevel(UwpIApplicationViewStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs1_get_Value(UwpIApplicationViewStatics *This, UwpApplicationViewState *value) {
    uint32_t w = 0, h = 0;
    (void)This;
    if (!value) return E_POINTER;
    uwp_core_get_client_size(&w, &h);
    if (h > w) {
        *value = UwpApplicationViewState_FullScreenPortrait;
    } else if (uwp_core_is_fullscreen()) {
        *value = UwpApplicationViewState_FullScreenLandscape;
    } else {
        *value = UwpApplicationViewState_Filled;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs1_TryUnsnap(UwpIApplicationViewStatics *This, boolean *success) {
    (void)This;
    if (!success) return E_POINTER;
    *success = TRUE;
    return S_OK;
}
static const UwpIApplicationViewStaticsVtbl g_avs1_vtbl = {
    avs1_QueryInterface, avs1_AddRef, avs1_Release,
    avs1_GetIids, avs1_GetRuntimeClassName, avs1_GetTrustLevel,
    avs1_get_Value, avs1_TryUnsnap
};

/* Statics2 */
static HRESULT STDMETHODCALLTYPE avs2_QueryInterface(UwpIApplicationViewStatics2 *This, REFIID riid, void **ppv) {
    return av_statics_qi(CONTAINING_RECORD(This, app_view_statics_t, st2_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE avs2_AddRef(UwpIApplicationViewStatics2 *This) {
    return av_statics_addref(CONTAINING_RECORD(This, app_view_statics_t, st2_iface));
}
static ULONG STDMETHODCALLTYPE avs2_Release(UwpIApplicationViewStatics2 *This) {
    return av_statics_release(CONTAINING_RECORD(This, app_view_statics_t, st2_iface));
}
static HRESULT STDMETHODCALLTYPE avs2_GetIids(UwpIApplicationViewStatics2 *This, ULONG *c, IID **iids) {
    (void)This; return av_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE avs2_GetRuntimeClassName(UwpIApplicationViewStatics2 *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE avs2_GetTrustLevel(UwpIApplicationViewStatics2 *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs2_GetForCurrentView(UwpIApplicationViewStatics2 *This, UwpIApplicationView **current) {
    app_view_obj_t *obj;
    (void)This;
    if (!current) return E_POINTER;
    obj = av_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    *current = &obj->av1_iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs2_get_TerminateAppOnFinalViewClose(UwpIApplicationViewStatics2 *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_terminate_on_final_close;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs2_put_TerminateAppOnFinalViewClose(UwpIApplicationViewStatics2 *This, boolean value) {
    (void)This;
    uwp_ui_lock_acquire(&g_av_lock);
    g_terminate_on_final_close = value ? TRUE : FALSE;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static const UwpIApplicationViewStatics2Vtbl g_avs2_vtbl = {
    avs2_QueryInterface, avs2_AddRef, avs2_Release,
    avs2_GetIids, avs2_GetRuntimeClassName, avs2_GetTrustLevel,
    avs2_GetForCurrentView,
    avs2_get_TerminateAppOnFinalViewClose,
    avs2_put_TerminateAppOnFinalViewClose
};

/* Statics3 */
static HRESULT STDMETHODCALLTYPE avs3_QueryInterface(UwpIApplicationViewStatics3 *This, REFIID riid, void **ppv) {
    return av_statics_qi(CONTAINING_RECORD(This, app_view_statics_t, st3_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE avs3_AddRef(UwpIApplicationViewStatics3 *This) {
    return av_statics_addref(CONTAINING_RECORD(This, app_view_statics_t, st3_iface));
}
static ULONG STDMETHODCALLTYPE avs3_Release(UwpIApplicationViewStatics3 *This) {
    return av_statics_release(CONTAINING_RECORD(This, app_view_statics_t, st3_iface));
}
static HRESULT STDMETHODCALLTYPE avs3_GetIids(UwpIApplicationViewStatics3 *This, ULONG *c, IID **iids) {
    (void)This; return av_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE avs3_GetRuntimeClassName(UwpIApplicationViewStatics3 *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE avs3_GetTrustLevel(UwpIApplicationViewStatics3 *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs3_get_PreferredLaunchWindowingMode(UwpIApplicationViewStatics3 *This, UwpApplicationViewWindowingMode *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_launch_windowing_mode;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs3_put_PreferredLaunchWindowingMode(UwpIApplicationViewStatics3 *This, UwpApplicationViewWindowingMode value) {
    (void)This;
    uwp_ui_lock_acquire(&g_av_lock);
    g_launch_windowing_mode = value;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs3_get_PreferredLaunchViewSize(UwpIApplicationViewStatics3 *This, UwpSize *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_av_lock);
    *value = g_launch_view_size;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avs3_put_PreferredLaunchViewSize(UwpIApplicationViewStatics3 *This, UwpSize value) {
    (void)This;
    if (value.Width < 0.0f || value.Height < 0.0f) return E_INVALIDARG;
    uwp_ui_lock_acquire(&g_av_lock);
    g_launch_view_size = value;
    uwp_ui_lock_release(&g_av_lock);
    return S_OK;
}
static const UwpIApplicationViewStatics3Vtbl g_avs3_vtbl = {
    avs3_QueryInterface, avs3_AddRef, avs3_Release,
    avs3_GetIids, avs3_GetRuntimeClassName, avs3_GetTrustLevel,
    avs3_get_PreferredLaunchWindowingMode,
    avs3_put_PreferredLaunchWindowingMode,
    avs3_get_PreferredLaunchViewSize,
    avs3_put_PreferredLaunchViewSize
};

/* FullscreenStatics */
static HRESULT STDMETHODCALLTYPE avfs_QueryInterface(UwpIApplicationViewFullscreenStatics *This, REFIID riid, void **ppv) {
    return av_statics_qi(CONTAINING_RECORD(This, app_view_statics_t, fs_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE avfs_AddRef(UwpIApplicationViewFullscreenStatics *This) {
    return av_statics_addref(CONTAINING_RECORD(This, app_view_statics_t, fs_iface));
}
static ULONG STDMETHODCALLTYPE avfs_Release(UwpIApplicationViewFullscreenStatics *This) {
    return av_statics_release(CONTAINING_RECORD(This, app_view_statics_t, fs_iface));
}
static HRESULT STDMETHODCALLTYPE avfs_GetIids(UwpIApplicationViewFullscreenStatics *This, ULONG *c, IID **iids) {
    (void)This; return av_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE avfs_GetRuntimeClassName(UwpIApplicationViewFullscreenStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE avfs_GetTrustLevel(UwpIApplicationViewFullscreenStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE avfs_TryUnsnapToFullscreen(UwpIApplicationViewFullscreenStatics *This, boolean *success) {
    (void)This;
    int32_t res = uwp_core_set_fullscreen(1);
    if (success) *success = (res != 0 || uwp_core_is_fullscreen() != 0) ? TRUE : FALSE;
    return S_OK;
}
static const UwpIApplicationViewFullscreenStaticsVtbl g_avfs_vtbl = {
    avfs_QueryInterface, avfs_AddRef, avfs_Release,
    avfs_GetIids, avfs_GetRuntimeClassName, avfs_GetTrustLevel,
    avfs_TryUnsnapToFullscreen
};

/* InteropStatics */
static HRESULT STDMETHODCALLTYPE avis_QueryInterface(UwpIApplicationViewInteropStatics *This, REFIID riid, void **ppv) {
    return av_statics_qi(CONTAINING_RECORD(This, app_view_statics_t, interop_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE avis_AddRef(UwpIApplicationViewInteropStatics *This) {
    return av_statics_addref(CONTAINING_RECORD(This, app_view_statics_t, interop_iface));
}
static ULONG STDMETHODCALLTYPE avis_Release(UwpIApplicationViewInteropStatics *This) {
    return av_statics_release(CONTAINING_RECORD(This, app_view_statics_t, interop_iface));
}
static HRESULT STDMETHODCALLTYPE avis_GetIids(UwpIApplicationViewInteropStatics *This, ULONG *c, IID **iids) {
    (void)This; return av_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE avis_GetRuntimeClassName(UwpIApplicationViewInteropStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE avis_GetTrustLevel(UwpIApplicationViewInteropStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE avis_GetApplicationViewIdForWindow(UwpIApplicationViewInteropStatics *This, void *window, INT32 *id) {
    (void)This; (void)window;
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}
static const UwpIApplicationViewInteropStaticsVtbl g_avis_vtbl = {
    avis_QueryInterface, avis_AddRef, avis_Release,
    avis_GetIids, avis_GetRuntimeClassName, avis_GetTrustLevel,
    avis_GetApplicationViewIdForWindow
};

/* IActivationFactory */
static HRESULT STDMETHODCALLTYPE ava_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return av_statics_qi(CONTAINING_RECORD(This, app_view_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE ava_AddRef(IActivationFactory *This) {
    return av_statics_addref(CONTAINING_RECORD(This, app_view_statics_t, act_iface));
}
static ULONG STDMETHODCALLTYPE ava_Release(IActivationFactory *This) {
    return av_statics_release(CONTAINING_RECORD(This, app_view_statics_t, act_iface));
}
static HRESULT STDMETHODCALLTYPE ava_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This; return av_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE ava_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.ApplicationView", name);
}
static HRESULT STDMETHODCALLTYPE ava_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE ava_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("ApplicationViewFactory", "ActivateInstance");
}
static const IActivationFactoryVtbl g_av_act_vtbl = {
    ava_QueryInterface, ava_AddRef, ava_Release,
    ava_GetIids, ava_GetRuntimeClassName, ava_GetTrustLevel,
    ava_ActivateInstance
};

HRESULT uwp_ui_app_view_get_factory(REFIID iid, void **out_factory) {
    app_view_statics_t *fac = (app_view_statics_t *)uwp_ui_alloc(sizeof(app_view_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->st1_iface.lpVtbl = &g_avs1_vtbl;
    fac->st2_iface.lpVtbl = &g_avs2_vtbl;
    fac->st3_iface.lpVtbl = &g_avs3_vtbl;
    fac->fs_iface.lpVtbl = &g_avfs_vtbl;
    fac->interop_iface.lpVtbl = &g_avis_vtbl;
    fac->act_iface.lpVtbl = &g_av_act_vtbl;
    fac->ref_count = 1;
    HRESULT hr = av_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}

HRESULT uwp_winrt_ui_notify_visible_bounds_changed(void) {
    app_view_obj_t *obj = av_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    uwp_ui_event_invoke(&g_visible_bounds_events, &obj->av1_iface, NULL);
    av_release(obj);
    return S_OK;
}

/* ============================================================================
 * Windows.UI.ViewManagement.UIViewSettings
 * ============================================================================ */

typedef struct ui_view_settings_obj_s {
    UwpIUIViewSettings iface;
    volatile int32_t   ref_count;
} ui_view_settings_obj_t;

typedef struct ui_view_settings_statics_s {
    UwpIUIViewSettingsStatics statics_iface;
    IActivationFactory        act_iface;
    volatile int32_t          ref_count;
} ui_view_settings_statics_t;

static ui_view_settings_obj_t *g_uvs_singleton = NULL;

static HRESULT STDMETHODCALLTYPE uvs_QueryInterface(UwpIUIViewSettings *This, REFIID riid, void **ppv) {
    ui_view_settings_obj_t *obj = CONTAINING_RECORD(This, ui_view_settings_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IUIViewSettings)) {
        *ppv = &obj->iface;
        uwp_ui_atomic_inc(&obj->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE uvs_AddRef(UwpIUIViewSettings *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, ui_view_settings_obj_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE uvs_Release(UwpIUIViewSettings *This) {
    ui_view_settings_obj_t *obj = CONTAINING_RECORD(This, ui_view_settings_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&obj->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_av_lock);
        if (uwp_ui_atomic_load(&obj->ref_count) == 0) {
            if (g_uvs_singleton == obj) g_uvs_singleton = NULL;
            uwp_ui_lock_release(&g_av_lock);
            uwp_ui_free(obj);
            return 0;
        }
        uwp_ui_lock_release(&g_av_lock);
    }
    return ref;
}
static HRESULT STDMETHODCALLTYPE uvs_GetIids(UwpIUIViewSettings *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IUIViewSettings };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE uvs_GetRuntimeClassName(UwpIUIViewSettings *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.UIViewSettings", name);
}
static HRESULT STDMETHODCALLTYPE uvs_GetTrustLevel(UwpIUIViewSettings *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uvs_get_UserInteractionMode(UwpIUIViewSettings *This, UwpUserInteractionMode *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = UwpUserInteractionMode_Mouse;
    return S_OK;
}

static const UwpIUIViewSettingsVtbl g_uvs_vtbl = {
    uvs_QueryInterface, uvs_AddRef, uvs_Release,
    uvs_GetIids, uvs_GetRuntimeClassName, uvs_GetTrustLevel,
    uvs_get_UserInteractionMode
};

static ui_view_settings_obj_t *uvs_get_or_create_instance(void) {
    ui_view_settings_obj_t *obj;
    uwp_ui_lock_acquire(&g_av_lock);
    if (!g_uvs_singleton) {
        obj = (ui_view_settings_obj_t *)uwp_ui_alloc(sizeof(ui_view_settings_obj_t));
        if (obj) {
            obj->iface.lpVtbl = &g_uvs_vtbl;
            obj->ref_count = 1;
            g_uvs_singleton = obj;
        }
    } else {
        obj = g_uvs_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_av_lock);
    return obj;
}

static HRESULT uvs_statics_qi(ui_view_settings_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IUIViewSettingsStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}
static HRESULT uvs_statics_get_iids(ULONG *c, IID **iids) {
    const GUID list[] = { SCARLETT_IID_IUIViewSettingsStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}

static HRESULT STDMETHODCALLTYPE uvss_QueryInterface(UwpIUIViewSettingsStatics *This, REFIID riid, void **ppv) {
    return uvs_statics_qi(CONTAINING_RECORD(This, ui_view_settings_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE uvss_AddRef(UwpIUIViewSettingsStatics *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, ui_view_settings_statics_t, statics_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE uvss_Release(UwpIUIViewSettingsStatics *This) {
    ui_view_settings_statics_t *fac = CONTAINING_RECORD(This, ui_view_settings_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE uvss_GetIids(UwpIUIViewSettingsStatics *This, ULONG *c, IID **iids) {
    (void)This;
    return uvs_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE uvss_GetRuntimeClassName(UwpIUIViewSettingsStatics *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.UIViewSettings", name);
}
static HRESULT STDMETHODCALLTYPE uvss_GetTrustLevel(UwpIUIViewSettingsStatics *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uvss_GetForCurrentView(UwpIUIViewSettingsStatics *This, UwpIUIViewSettings **current) {
    ui_view_settings_obj_t *obj;
    (void)This;
    if (!current) return E_POINTER;
    obj = uvs_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    *current = &obj->iface;
    return S_OK;
}

static const UwpIUIViewSettingsStaticsVtbl g_uvss_vtbl = {
    uvss_QueryInterface, uvss_AddRef, uvss_Release,
    uvss_GetIids, uvss_GetRuntimeClassName, uvss_GetTrustLevel,
    uvss_GetForCurrentView
};

static HRESULT STDMETHODCALLTYPE uvsa_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return uvs_statics_qi(CONTAINING_RECORD(This, ui_view_settings_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE uvsa_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, ui_view_settings_statics_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE uvsa_Release(IActivationFactory *This) {
    ui_view_settings_statics_t *fac = CONTAINING_RECORD(This, ui_view_settings_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE uvsa_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    return uvs_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE uvsa_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.ViewManagement.UIViewSettings", name);
}
static HRESULT STDMETHODCALLTYPE uvsa_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uvsa_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("UIViewSettingsFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_uvsa_vtbl = {
    uvsa_QueryInterface, uvsa_AddRef, uvsa_Release,
    uvsa_GetIids, uvsa_GetRuntimeClassName, uvsa_GetTrustLevel,
    uvsa_ActivateInstance
};

HRESULT uwp_ui_view_settings_get_factory(REFIID iid, void **out_factory) {
    ui_view_settings_statics_t *fac = (ui_view_settings_statics_t *)uwp_ui_alloc(sizeof(ui_view_settings_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_uvss_vtbl;
    fac->act_iface.lpVtbl = &g_uvsa_vtbl;
    fac->ref_count = 1;
    HRESULT hr = uvs_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}
