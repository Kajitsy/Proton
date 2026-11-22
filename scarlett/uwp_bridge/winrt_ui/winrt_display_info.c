/*
 * Proton-Scarlett — Windows.Graphics.Display.DisplayInformation
 *
 * Clean-room implementation of IDisplayInformationStatics and
 * IDisplayInformation .. IDisplayInformation5 backed by live Win32 monitor/DPI metrics.
 */

#include "winrt_ui_internal.h"

typedef struct display_info_obj_s {
    UwpIDisplayInformation  di1_iface;
    UwpIDisplayInformation2 di2_iface;
    UwpIDisplayInformation3 di3_iface;
    UwpIDisplayInformation4 di4_iface;
    UwpIDisplayInformation5 di5_iface;
    volatile int32_t        ref_count;
} display_info_obj_t;

typedef struct display_info_statics_s {
    UwpIDisplayInformationStatics statics_iface;
    IActivationFactory            act_iface;
    volatile int32_t              ref_count;
} display_info_statics_t;

static uwp_ui_lock_t           g_di_lock = UWP_UI_LOCK_INIT;
static UwpDisplayOrientations  g_auto_rotation = UwpDisplayOrientations_Landscape;
static uwp_ui_event_list_t     g_orientation_changed = UWP_UI_EVENT_LIST_INIT;
static uwp_ui_event_list_t     g_dpi_changed = UWP_UI_EVENT_LIST_INIT;
static uwp_ui_event_list_t     g_stereo_changed = UWP_UI_EVENT_LIST_INIT;
static uwp_ui_event_list_t     g_color_profile_changed = UWP_UI_EVENT_LIST_INIT;
static uwp_ui_event_list_t     g_contents_invalidated = UWP_UI_EVENT_LIST_INIT;
static uwp_ui_event_list_t     g_advanced_color_changed = UWP_UI_EVENT_LIST_INIT;

static display_info_obj_t *g_di_singleton = NULL;

static inline display_info_obj_t *di_from_1(UwpIDisplayInformation *iface) {
    return CONTAINING_RECORD(iface, display_info_obj_t, di1_iface);
}
static inline display_info_obj_t *di_from_2(UwpIDisplayInformation2 *iface) {
    return CONTAINING_RECORD(iface, display_info_obj_t, di2_iface);
}
static inline display_info_obj_t *di_from_3(UwpIDisplayInformation3 *iface) {
    return CONTAINING_RECORD(iface, display_info_obj_t, di3_iface);
}
static inline display_info_obj_t *di_from_4(UwpIDisplayInformation4 *iface) {
    return CONTAINING_RECORD(iface, display_info_obj_t, di4_iface);
}
static inline display_info_obj_t *di_from_5(UwpIDisplayInformation5 *iface) {
    return CONTAINING_RECORD(iface, display_info_obj_t, di5_iface);
}

static HRESULT di_qi(display_info_obj_t *obj, REFIID iid, void **out) {
    if (!iid || !out) {
        return E_INVALIDARG;
    }
    *out = NULL;

    if (IsEqualGUID(iid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(iid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(iid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(iid, &SCARLETT_IID_IDisplayInformation)) {
        *out = &obj->di1_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IDisplayInformation2)) {
        *out = &obj->di2_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IDisplayInformation3)) {
        *out = &obj->di3_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IDisplayInformation4)) {
        *out = &obj->di4_iface;
    } else if (IsEqualGUID(iid, &SCARLETT_IID_IDisplayInformation5)) {
        *out = &obj->di5_iface;
    } else {
        return E_NOINTERFACE;
    }

    uwp_ui_atomic_inc(&obj->ref_count);
    return S_OK;
}

static ULONG di_addref(display_info_obj_t *obj) {
    return uwp_ui_atomic_inc(&obj->ref_count);
}

static ULONG di_release(display_info_obj_t *obj) {
    ULONG ref = uwp_ui_atomic_dec(&obj->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_di_lock);
        if (g_di_singleton == obj) {
            g_di_singleton = NULL;
        }
        uwp_ui_lock_release(&g_di_lock);
        uwp_ui_free(obj);
    }
    return ref;
}

static HRESULT di_get_iids(ULONG *count, IID **iids) {
    const GUID list[] = {
        SCARLETT_IID_IDisplayInformation,
        SCARLETT_IID_IDisplayInformation2,
        SCARLETT_IID_IDisplayInformation3,
        SCARLETT_IID_IDisplayInformation4,
        SCARLETT_IID_IDisplayInformation5
    };
    return uwp_ui_get_iids(list, 5, count, iids);
}

/* --- IDisplayInformation (v1) --- */

static HRESULT STDMETHODCALLTYPE di1_QueryInterface(UwpIDisplayInformation *This, REFIID riid, void **ppv) {
    return di_qi(di_from_1(This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE di1_AddRef(UwpIDisplayInformation *This) {
    return di_addref(di_from_1(This));
}
static ULONG STDMETHODCALLTYPE di1_Release(UwpIDisplayInformation *This) {
    return di_release(di_from_1(This));
}
static HRESULT STDMETHODCALLTYPE di1_GetIids(UwpIDisplayInformation *This, ULONG *c, IID **iids) {
    (void)This;
    return di_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE di1_GetRuntimeClassName(UwpIDisplayInformation *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE di1_GetTrustLevel(UwpIDisplayInformation *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_get_CurrentOrientation(UwpIDisplayInformation *This, UwpDisplayOrientations *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.is_portrait ? UwpDisplayOrientations_Portrait : UwpDisplayOrientations_Landscape;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_get_NativeOrientation(UwpIDisplayInformation *This, UwpDisplayOrientations *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.is_portrait ? UwpDisplayOrientations_Portrait : UwpDisplayOrientations_Landscape;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_add_OrientationChanged(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_orientation_changed, handler, token);
}
static HRESULT STDMETHODCALLTYPE di1_remove_OrientationChanged(UwpIDisplayInformation *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_orientation_changed, token);
}
static HRESULT STDMETHODCALLTYPE di1_get_ResolutionScale(UwpIDisplayInformation *This, UwpResolutionScale *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = uwp_ui_dpi_to_resolution_scale(m.logical_dpi);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_get_LogicalDpi(UwpIDisplayInformation *This, FLOAT *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.logical_dpi;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_get_RawDpiX(UwpIDisplayInformation *This, FLOAT *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.logical_dpi;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_get_RawDpiY(UwpIDisplayInformation *This, FLOAT *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.logical_dpi;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_add_DpiChanged(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_dpi_changed, handler, token);
}
static HRESULT STDMETHODCALLTYPE di1_remove_DpiChanged(UwpIDisplayInformation *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_dpi_changed, token);
}
static HRESULT STDMETHODCALLTYPE di1_get_StereoEnabled(UwpIDisplayInformation *This, boolean *value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di1_add_StereoEnabledChanged(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_stereo_changed, handler, token);
}
static HRESULT STDMETHODCALLTYPE di1_remove_StereoEnabledChanged(UwpIDisplayInformation *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_stereo_changed, token);
}
static HRESULT STDMETHODCALLTYPE di1_GetColorProfileAsync(UwpIDisplayInformation *This, void **asyncInfo) {
    (void)This;
    if (asyncInfo) *asyncInfo = NULL;
    return uwp_ui_unimpl("IDisplayInformation", "GetColorProfileAsync");
}
static HRESULT STDMETHODCALLTYPE di1_add_ColorProfileChanged(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_color_profile_changed, handler, token);
}
static HRESULT STDMETHODCALLTYPE di1_remove_ColorProfileChanged(UwpIDisplayInformation *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_color_profile_changed, token);
}

static const UwpIDisplayInformationVtbl g_di1_vtbl = {
    di1_QueryInterface,
    di1_AddRef,
    di1_Release,
    di1_GetIids,
    di1_GetRuntimeClassName,
    di1_GetTrustLevel,
    di1_get_CurrentOrientation,
    di1_get_NativeOrientation,
    di1_add_OrientationChanged,
    di1_remove_OrientationChanged,
    di1_get_ResolutionScale,
    di1_get_LogicalDpi,
    di1_get_RawDpiX,
    di1_get_RawDpiY,
    di1_add_DpiChanged,
    di1_remove_DpiChanged,
    di1_get_StereoEnabled,
    di1_add_StereoEnabledChanged,
    di1_remove_StereoEnabledChanged,
    di1_GetColorProfileAsync,
    di1_add_ColorProfileChanged,
    di1_remove_ColorProfileChanged
};

/* --- IDisplayInformation2 --- */

static HRESULT STDMETHODCALLTYPE di2_QueryInterface(UwpIDisplayInformation2 *This, REFIID riid, void **ppv) {
    return di_qi(di_from_2(This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE di2_AddRef(UwpIDisplayInformation2 *This) {
    return di_addref(di_from_2(This));
}
static ULONG STDMETHODCALLTYPE di2_Release(UwpIDisplayInformation2 *This) {
    return di_release(di_from_2(This));
}
static HRESULT STDMETHODCALLTYPE di2_GetIids(UwpIDisplayInformation2 *This, ULONG *c, IID **iids) {
    (void)This;
    return di_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE di2_GetRuntimeClassName(UwpIDisplayInformation2 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE di2_GetTrustLevel(UwpIDisplayInformation2 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di2_get_RawPixelsPerViewPixel(UwpIDisplayInformation2 *This, DOUBLE *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = (DOUBLE)m.logical_dpi / 96.0;
    return S_OK;
}

static const UwpIDisplayInformation2Vtbl g_di2_vtbl = {
    di2_QueryInterface,
    di2_AddRef,
    di2_Release,
    di2_GetIids,
    di2_GetRuntimeClassName,
    di2_GetTrustLevel,
    di2_get_RawPixelsPerViewPixel
};

/* --- IDisplayInformation3 --- */

static HRESULT STDMETHODCALLTYPE di3_QueryInterface(UwpIDisplayInformation3 *This, REFIID riid, void **ppv) {
    return di_qi(di_from_3(This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE di3_AddRef(UwpIDisplayInformation3 *This) {
    return di_addref(di_from_3(This));
}
static ULONG STDMETHODCALLTYPE di3_Release(UwpIDisplayInformation3 *This) {
    return di_release(di_from_3(This));
}
static HRESULT STDMETHODCALLTYPE di3_GetIids(UwpIDisplayInformation3 *This, ULONG *c, IID **iids) {
    (void)This;
    return di_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE di3_GetRuntimeClassName(UwpIDisplayInformation3 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE di3_GetTrustLevel(UwpIDisplayInformation3 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di3_get_DiagonalSizeInInches(UwpIDisplayInformation3 *This, void **value) {
    (void)This;
    if (!value) return E_POINTER;
    /* WinRT explicitly defines null IReference<double> when physical monitor size is unavailable */
    *value = NULL;
    return S_OK;
}

static const UwpIDisplayInformation3Vtbl g_di3_vtbl = {
    di3_QueryInterface,
    di3_AddRef,
    di3_Release,
    di3_GetIids,
    di3_GetRuntimeClassName,
    di3_GetTrustLevel,
    di3_get_DiagonalSizeInInches
};

/* --- IDisplayInformation4 --- */

static HRESULT STDMETHODCALLTYPE di4_QueryInterface(UwpIDisplayInformation4 *This, REFIID riid, void **ppv) {
    return di_qi(di_from_4(This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE di4_AddRef(UwpIDisplayInformation4 *This) {
    return di_addref(di_from_4(This));
}
static ULONG STDMETHODCALLTYPE di4_Release(UwpIDisplayInformation4 *This) {
    return di_release(di_from_4(This));
}
static HRESULT STDMETHODCALLTYPE di4_GetIids(UwpIDisplayInformation4 *This, ULONG *c, IID **iids) {
    (void)This;
    return di_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE di4_GetRuntimeClassName(UwpIDisplayInformation4 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE di4_GetTrustLevel(UwpIDisplayInformation4 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di4_get_ScreenWidthInRawPixels(UwpIDisplayInformation4 *This, UINT32 *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.width_px;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di4_get_ScreenHeightInRawPixels(UwpIDisplayInformation4 *This, UINT32 *value) {
    uwp_ui_display_metrics_t m;
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_query_display_metrics(&m);
    *value = m.height_px;
    return S_OK;
}

static const UwpIDisplayInformation4Vtbl g_di4_vtbl = {
    di4_QueryInterface,
    di4_AddRef,
    di4_Release,
    di4_GetIids,
    di4_GetRuntimeClassName,
    di4_GetTrustLevel,
    di4_get_ScreenWidthInRawPixels,
    di4_get_ScreenHeightInRawPixels
};

/* --- IDisplayInformation5 --- */

static HRESULT STDMETHODCALLTYPE di5_QueryInterface(UwpIDisplayInformation5 *This, REFIID riid, void **ppv) {
    return di_qi(di_from_5(This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE di5_AddRef(UwpIDisplayInformation5 *This) {
    return di_addref(di_from_5(This));
}
static ULONG STDMETHODCALLTYPE di5_Release(UwpIDisplayInformation5 *This) {
    return di_release(di_from_5(This));
}
static HRESULT STDMETHODCALLTYPE di5_GetIids(UwpIDisplayInformation5 *This, ULONG *c, IID **iids) {
    (void)This;
    return di_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE di5_GetRuntimeClassName(UwpIDisplayInformation5 *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE di5_GetTrustLevel(UwpIDisplayInformation5 *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE di5_GetAdvancedColorInfo(UwpIDisplayInformation5 *This, void **value) {
    (void)This;
    if (value) *value = NULL;
    return uwp_ui_unimpl("IDisplayInformation5", "GetAdvancedColorInfo");
}
static HRESULT STDMETHODCALLTYPE di5_add_AdvancedColorInfoChanged(UwpIDisplayInformation5 *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_advanced_color_changed, handler, token);
}
static HRESULT STDMETHODCALLTYPE di5_remove_AdvancedColorInfoChanged(UwpIDisplayInformation5 *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_advanced_color_changed, token);
}

static const UwpIDisplayInformation5Vtbl g_di5_vtbl = {
    di5_QueryInterface,
    di5_AddRef,
    di5_Release,
    di5_GetIids,
    di5_GetRuntimeClassName,
    di5_GetTrustLevel,
    di5_GetAdvancedColorInfo,
    di5_add_AdvancedColorInfoChanged,
    di5_remove_AdvancedColorInfoChanged
};

static display_info_obj_t *di_get_or_create_instance(void) {
    display_info_obj_t *obj;
    uwp_ui_lock_acquire(&g_di_lock);
    if (!g_di_singleton) {
        obj = (display_info_obj_t *)uwp_ui_alloc(sizeof(display_info_obj_t));
        if (obj) {
            obj->di1_iface.lpVtbl = &g_di1_vtbl;
            obj->di2_iface.lpVtbl = &g_di2_vtbl;
            obj->di3_iface.lpVtbl = &g_di3_vtbl;
            obj->di4_iface.lpVtbl = &g_di4_vtbl;
            obj->di5_iface.lpVtbl = &g_di5_vtbl;
            obj->ref_count = 1;
            g_di_singleton = obj;
        }
    } else {
        obj = g_di_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_di_lock);
    return obj;
}

/* --- IDisplayInformationStatics & IActivationFactory --- */

static HRESULT di_statics_qi(display_info_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IDisplayInformationStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dis_QueryInterface(UwpIDisplayInformationStatics *This, REFIID riid, void **ppv) {
    return di_statics_qi(CONTAINING_RECORD(This, display_info_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE dis_AddRef(UwpIDisplayInformationStatics *This) {
    display_info_statics_t *fac = CONTAINING_RECORD(This, display_info_statics_t, statics_iface);
    return uwp_ui_atomic_inc(&fac->ref_count);
}
static ULONG STDMETHODCALLTYPE dis_Release(UwpIDisplayInformationStatics *This) {
    display_info_statics_t *fac = CONTAINING_RECORD(This, display_info_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE dis_GetIids(UwpIDisplayInformationStatics *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IDisplayInformationStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE dis_GetRuntimeClassName(UwpIDisplayInformationStatics *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE dis_GetTrustLevel(UwpIDisplayInformationStatics *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dis_GetForCurrentView(UwpIDisplayInformationStatics *This, UwpIDisplayInformation **current) {
    display_info_obj_t *obj;
    (void)This;
    if (!current) return E_POINTER;
    obj = di_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    *current = &obj->di1_iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dis_get_AutoRotationPreferences(UwpIDisplayInformationStatics *This, UwpDisplayOrientations *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_di_lock);
    *value = g_auto_rotation;
    uwp_ui_lock_release(&g_di_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dis_put_AutoRotationPreferences(UwpIDisplayInformationStatics *This, UwpDisplayOrientations value) {
    (void)This;
    uwp_ui_lock_acquire(&g_di_lock);
    g_auto_rotation = value;
    uwp_ui_lock_release(&g_di_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dis_add_DisplayContentsInvalidated(UwpIDisplayInformationStatics *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_contents_invalidated, handler, token);
}
static HRESULT STDMETHODCALLTYPE dis_remove_DisplayContentsInvalidated(UwpIDisplayInformationStatics *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_contents_invalidated, token);
}

static const UwpIDisplayInformationStaticsVtbl g_di_statics_vtbl = {
    dis_QueryInterface,
    dis_AddRef,
    dis_Release,
    dis_GetIids,
    dis_GetRuntimeClassName,
    dis_GetTrustLevel,
    dis_GetForCurrentView,
    dis_get_AutoRotationPreferences,
    dis_put_AutoRotationPreferences,
    dis_add_DisplayContentsInvalidated,
    dis_remove_DisplayContentsInvalidated
};

static HRESULT STDMETHODCALLTYPE dia_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return di_statics_qi(CONTAINING_RECORD(This, display_info_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE dia_AddRef(IActivationFactory *This) {
    display_info_statics_t *fac = CONTAINING_RECORD(This, display_info_statics_t, act_iface);
    return uwp_ui_atomic_inc(&fac->ref_count);
}
static ULONG STDMETHODCALLTYPE dia_Release(IActivationFactory *This) {
    display_info_statics_t *fac = CONTAINING_RECORD(This, display_info_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE dia_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IDisplayInformationStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE dia_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.DisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE dia_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dia_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("DisplayInformationFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_di_act_vtbl = {
    dia_QueryInterface,
    dia_AddRef,
    dia_Release,
    dia_GetIids,
    dia_GetRuntimeClassName,
    dia_GetTrustLevel,
    dia_ActivateInstance
};

HRESULT uwp_ui_display_info_get_factory(REFIID iid, void **out_factory) {
    display_info_statics_t *fac = (display_info_statics_t *)uwp_ui_alloc(sizeof(display_info_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_di_statics_vtbl;
    fac->act_iface.lpVtbl = &g_di_act_vtbl;
    fac->ref_count = 1;
    HRESULT hr = di_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) {
        uwp_ui_free(fac);
    }
    return hr;
}

HRESULT uwp_winrt_ui_notify_dpi_changed(void) {
    display_info_obj_t *obj = di_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    uwp_ui_event_invoke(&g_dpi_changed, &obj->di1_iface, NULL);
    di_release(obj);
    return S_OK;
}

HRESULT uwp_winrt_ui_notify_orientation_changed(void) {
    display_info_obj_t *obj = di_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    uwp_ui_event_invoke(&g_orientation_changed, &obj->di1_iface, NULL);
    di_release(obj);
    return S_OK;
}
