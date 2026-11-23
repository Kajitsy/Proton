/*
 * Proton-Scarlett — DisplayRequest & HdmiDisplayInformation
 *
 * Clean-room implementations of:
 *   - Windows.System.Display.DisplayRequest (SetThreadExecutionState)
 *   - Windows.Graphics.Display.Core.HdmiDisplayInformation (+ HdmiDisplayMode)
 */

#include "winrt_ui_internal.h"

/* ============================================================================
 * Windows.System.Display.DisplayRequest
 * ============================================================================ */

typedef struct display_req_obj_s {
    UwpIDisplayRequest iface;
    volatile int32_t   ref_count;
    int32_t            instance_active_count;
} display_req_obj_t;

typedef struct display_req_factory_s {
    IActivationFactory act_iface;
    volatile int32_t   ref_count;
} display_req_factory_t;

static uwp_ui_lock_t g_dr_lock = UWP_UI_LOCK_INIT;
static int32_t       g_dr_global_active_count = 0;

static void dr_apply_execution_state(int32_t active_total) {
#ifdef _WIN32
    if (active_total > 0) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
#else
    (void)active_total;
#endif
}

static HRESULT STDMETHODCALLTYPE dr_QueryInterface(UwpIDisplayRequest *This, REFIID riid, void **ppv) {
    display_req_obj_t *dr = CONTAINING_RECORD(This, display_req_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IDisplayRequest)) {
        *ppv = &dr->iface;
        uwp_ui_atomic_inc(&dr->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dr_AddRef(UwpIDisplayRequest *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, display_req_obj_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE dr_Release(UwpIDisplayRequest *This) {
    display_req_obj_t *dr = CONTAINING_RECORD(This, display_req_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&dr->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_dr_lock);
        if (dr->instance_active_count > 0) {
            g_dr_global_active_count -= dr->instance_active_count;
            if (g_dr_global_active_count < 0) g_dr_global_active_count = 0;
            dr->instance_active_count = 0;
            dr_apply_execution_state(g_dr_global_active_count);
        }
        uwp_ui_lock_release(&g_dr_lock);
        uwp_ui_free(dr);
    }
    return ref;
}
static HRESULT STDMETHODCALLTYPE dr_GetIids(UwpIDisplayRequest *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IDisplayRequest };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE dr_GetRuntimeClassName(UwpIDisplayRequest *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.System.Display.DisplayRequest", name);
}
static HRESULT STDMETHODCALLTYPE dr_GetTrustLevel(UwpIDisplayRequest *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE dr_RequestActive(UwpIDisplayRequest *This) {
    display_req_obj_t *dr = CONTAINING_RECORD(This, display_req_obj_t, iface);
    uwp_ui_lock_acquire(&g_dr_lock);
    dr->instance_active_count++;
    g_dr_global_active_count++;
    dr_apply_execution_state(g_dr_global_active_count);
    uwp_ui_lock_release(&g_dr_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE dr_RequestRelease(UwpIDisplayRequest *This) {
    display_req_obj_t *dr = CONTAINING_RECORD(This, display_req_obj_t, iface);
    uwp_ui_lock_acquire(&g_dr_lock);
    if (dr->instance_active_count <= 0) {
        uwp_ui_lock_release(&g_dr_lock);
        uwp_bridge_log("winrt_ui", "DisplayRequest::RequestRelease called with active_count==0");
        return (HRESULT)0x8000000EL; /* E_ILLEGAL_METHOD_CALL */
    }
    dr->instance_active_count--;
    if (g_dr_global_active_count > 0) g_dr_global_active_count--;
    dr_apply_execution_state(g_dr_global_active_count);
    uwp_ui_lock_release(&g_dr_lock);
    return S_OK;
}

static const UwpIDisplayRequestVtbl g_dr_vtbl = {
    dr_QueryInterface, dr_AddRef, dr_Release,
    dr_GetIids, dr_GetRuntimeClassName, dr_GetTrustLevel,
    dr_RequestActive, dr_RequestRelease
};

static HRESULT STDMETHODCALLTYPE drf_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    display_req_factory_t *fac = CONTAINING_RECORD(This, display_req_factory_t, act_iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
        uwp_ui_atomic_inc(&fac->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE drf_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, display_req_factory_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE drf_Release(IActivationFactory *This) {
    display_req_factory_t *fac = CONTAINING_RECORD(This, display_req_factory_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE drf_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE drf_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.System.Display.DisplayRequest", name);
}
static HRESULT STDMETHODCALLTYPE drf_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE drf_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    display_req_obj_t *dr;
    (void)This;
    if (!instance) return E_POINTER;
    *instance = NULL;
    dr = (display_req_obj_t *)uwp_ui_alloc(sizeof(display_req_obj_t));
    if (!dr) return E_OUTOFMEMORY;
    dr->iface.lpVtbl = &g_dr_vtbl;
    dr->ref_count = 1;
    *instance = (IInspectable *)&dr->iface;
    return S_OK;
}

static const IActivationFactoryVtbl g_drf_vtbl = {
    drf_QueryInterface, drf_AddRef, drf_Release,
    drf_GetIids, drf_GetRuntimeClassName, drf_GetTrustLevel,
    drf_ActivateInstance
};

HRESULT uwp_ui_display_request_get_factory(REFIID iid, void **out_factory) {
    display_req_factory_t *fac = (display_req_factory_t *)uwp_ui_alloc(sizeof(display_req_factory_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->act_iface.lpVtbl = &g_drf_vtbl;
    fac->ref_count = 1;
    HRESULT hr = drf_QueryInterface(&fac->act_iface, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}

/* ============================================================================
 * Windows.Graphics.Display.Core.HdmiDisplayMode & HdmiDisplayInformation
 * ============================================================================ */

typedef struct hdmi_mode_obj_s {
    UwpIHdmiDisplayMode  m1_iface;
    UwpIHdmiDisplayMode2 m2_iface;
    volatile int32_t     ref_count;
    UINT32               width;
    UINT32               height;
    DOUBLE               refresh_hz;
    UINT16               bpp;
} hdmi_mode_obj_t;

typedef struct hdmi_mode_vec_s {
    UwpIVectorViewHdmiDisplayMode iface;
    volatile int32_t              ref_count;
    uint32_t                      count;
    hdmi_mode_obj_t             **items;
} hdmi_mode_vec_t;

typedef struct hdmi_info_obj_s {
    UwpIHdmiDisplayInformation iface;
    volatile int32_t           ref_count;
} hdmi_info_obj_t;

typedef struct hdmi_info_statics_s {
    UwpIHdmiDisplayInformationStatics statics_iface;
    IActivationFactory                act_iface;
    volatile int32_t                  ref_count;
} hdmi_info_statics_t;

/* IVectorView<HdmiDisplayMode*>: SCARLETT_IID_IVectorView_HdmiDisplayMode */
static uwp_ui_lock_t       g_hdmi_lock = UWP_UI_LOCK_INIT;
static hdmi_info_obj_t    *g_hdmi_singleton = NULL;
static uwp_ui_event_list_t g_hdmi_modes_changed = UWP_UI_EVENT_LIST_INIT;

static HRESULT hdmi_mode_qi(hdmi_mode_obj_t *m, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IHdmiDisplayMode)) {
        *ppv = &m->m1_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IHdmiDisplayMode2)) {
        *ppv = &m->m2_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&m->ref_count);
    return S_OK;
}
static ULONG hdmi_mode_addref(hdmi_mode_obj_t *m) {
    return uwp_ui_atomic_inc(&m->ref_count);
}
static ULONG hdmi_mode_release(hdmi_mode_obj_t *m) {
    ULONG ref = uwp_ui_atomic_dec(&m->ref_count);
    if (ref == 0) uwp_ui_free(m);
    return ref;
}
static HRESULT hdmi_mode_get_iids(ULONG *c, IID **iids) {
    const GUID list[] = { SCARLETT_IID_IHdmiDisplayMode, SCARLETT_IID_IHdmiDisplayMode2 };
    return uwp_ui_get_iids(list, 2, c, iids);
}

static HRESULT STDMETHODCALLTYPE hm1_QueryInterface(UwpIHdmiDisplayMode *This, REFIID riid, void **ppv) {
    return hdmi_mode_qi(CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hm1_AddRef(UwpIHdmiDisplayMode *This) {
    return hdmi_mode_addref(CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface));
}
static ULONG STDMETHODCALLTYPE hm1_Release(UwpIHdmiDisplayMode *This) {
    return hdmi_mode_release(CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface));
}
static HRESULT STDMETHODCALLTYPE hm1_GetIids(UwpIHdmiDisplayMode *This, ULONG *c, IID **iids) {
    (void)This; return hdmi_mode_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE hm1_GetRuntimeClassName(UwpIHdmiDisplayMode *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.Core.HdmiDisplayMode", name);
}
static HRESULT STDMETHODCALLTYPE hm1_GetTrustLevel(UwpIHdmiDisplayMode *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_ResolutionWidthInRawPixels(UwpIHdmiDisplayMode *This, UINT32 *value) {
    if (!value) return E_POINTER;
    *value = CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface)->width;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_ResolutionHeightInRawPixels(UwpIHdmiDisplayMode *This, UINT32 *value) {
    if (!value) return E_POINTER;
    *value = CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface)->height;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_RefreshRate(UwpIHdmiDisplayMode *This, DOUBLE *value) {
    if (!value) return E_POINTER;
    *value = CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface)->refresh_hz;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_StereoEnabled(UwpIHdmiDisplayMode *This, boolean *value) {
    (void)This; if (!value) return E_POINTER; *value = FALSE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_BitsPerPixel(UwpIHdmiDisplayMode *This, UINT16 *value) {
    if (!value) return E_POINTER;
    *value = CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface)->bpp;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_IsEqual(UwpIHdmiDisplayMode *This, UwpIHdmiDisplayMode *mode, boolean *result) {
    hdmi_mode_obj_t *a = CONTAINING_RECORD(This, hdmi_mode_obj_t, m1_iface);
    UINT32 w = 0, h = 0;
    DOUBLE hz = 0.0;
    UINT16 bpp = 0;
    if (!result) return E_POINTER;
    if (!mode) { *result = FALSE; return S_OK; }
    mode->lpVtbl->get_ResolutionWidthInRawPixels(mode, &w);
    mode->lpVtbl->get_ResolutionHeightInRawPixels(mode, &h);
    mode->lpVtbl->get_RefreshRate(mode, &hz);
    mode->lpVtbl->get_BitsPerPixel(mode, &bpp);
    *result = (a->width == w && a->height == h && a->bpp == bpp &&
               (a->refresh_hz - hz < 0.01 && hz - a->refresh_hz < 0.01)) ? TRUE : FALSE;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_ColorSpace(UwpIHdmiDisplayMode *This, UwpHdmiDisplayColorSpace *value) {
    (void)This; if (!value) return E_POINTER; *value = UwpHdmiDisplayColorSpace_RgbFull; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_PixelEncoding(UwpIHdmiDisplayMode *This, UwpHdmiDisplayPixelEncoding *value) {
    (void)This; if (!value) return E_POINTER; *value = UwpHdmiDisplayPixelEncoding_Rgb444; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_IsSdrLuminanceSupported(UwpIHdmiDisplayMode *This, boolean *value) {
    (void)This; if (!value) return E_POINTER; *value = TRUE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_IsSmpte2084Supported(UwpIHdmiDisplayMode *This, boolean *value) {
    (void)This; if (!value) return E_POINTER; *value = FALSE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm1_get_Is2086MetadataSupported(UwpIHdmiDisplayMode *This, boolean *value) {
    (void)This; if (!value) return E_POINTER; *value = FALSE; return S_OK;
}

static const UwpIHdmiDisplayModeVtbl g_hm1_vtbl = {
    hm1_QueryInterface, hm1_AddRef, hm1_Release,
    hm1_GetIids, hm1_GetRuntimeClassName, hm1_GetTrustLevel,
    hm1_get_ResolutionWidthInRawPixels,
    hm1_get_ResolutionHeightInRawPixels,
    hm1_get_RefreshRate,
    hm1_get_StereoEnabled,
    hm1_get_BitsPerPixel,
    hm1_IsEqual,
    hm1_get_ColorSpace,
    hm1_get_PixelEncoding,
    hm1_get_IsSdrLuminanceSupported,
    hm1_get_IsSmpte2084Supported,
    hm1_get_Is2086MetadataSupported
};

static HRESULT STDMETHODCALLTYPE hm2_QueryInterface(UwpIHdmiDisplayMode2 *This, REFIID riid, void **ppv) {
    return hdmi_mode_qi(CONTAINING_RECORD(This, hdmi_mode_obj_t, m2_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hm2_AddRef(UwpIHdmiDisplayMode2 *This) {
    return hdmi_mode_addref(CONTAINING_RECORD(This, hdmi_mode_obj_t, m2_iface));
}
static ULONG STDMETHODCALLTYPE hm2_Release(UwpIHdmiDisplayMode2 *This) {
    return hdmi_mode_release(CONTAINING_RECORD(This, hdmi_mode_obj_t, m2_iface));
}
static HRESULT STDMETHODCALLTYPE hm2_GetIids(UwpIHdmiDisplayMode2 *This, ULONG *c, IID **iids) {
    (void)This; return hdmi_mode_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE hm2_GetRuntimeClassName(UwpIHdmiDisplayMode2 *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.Core.HdmiDisplayMode", name);
}
static HRESULT STDMETHODCALLTYPE hm2_GetTrustLevel(UwpIHdmiDisplayMode2 *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hm2_get_IsDolbyVisionLowLatencySupported(UwpIHdmiDisplayMode2 *This, boolean *value) {
    (void)This; if (!value) return E_POINTER; *value = FALSE; return S_OK;
}

static const UwpIHdmiDisplayMode2Vtbl g_hm2_vtbl = {
    hm2_QueryInterface, hm2_AddRef, hm2_Release,
    hm2_GetIids, hm2_GetRuntimeClassName, hm2_GetTrustLevel,
    hm2_get_IsDolbyVisionLowLatencySupported
};

static hdmi_mode_obj_t *hdmi_mode_create_from_live(void) {
    uwp_ui_display_metrics_t m;
    hdmi_mode_obj_t *mode = (hdmi_mode_obj_t *)uwp_ui_alloc(sizeof(hdmi_mode_obj_t));
    if (!mode) return NULL;
    uwp_ui_query_display_metrics(&m);
    mode->m1_iface.lpVtbl = &g_hm1_vtbl;
    mode->m2_iface.lpVtbl = &g_hm2_vtbl;
    mode->ref_count = 1;
    mode->width = m.width_px;
    mode->height = m.height_px;
    mode->refresh_hz = m.refresh_hz;
    mode->bpp = m.bits_per_pixel;
    return mode;
}

/* --- IVectorView<HdmiDisplayMode*> --- */

static HRESULT STDMETHODCALLTYPE hmv_QueryInterface(UwpIVectorViewHdmiDisplayMode *This, REFIID riid, void **ppv) {
    hdmi_mode_vec_t *vec = CONTAINING_RECORD(This, hdmi_mode_vec_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IVectorView_HdmiDisplayMode)) {
        *ppv = &vec->iface;
        uwp_ui_atomic_inc(&vec->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE hmv_AddRef(UwpIVectorViewHdmiDisplayMode *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, hdmi_mode_vec_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE hmv_Release(UwpIVectorViewHdmiDisplayMode *This) {
    hdmi_mode_vec_t *vec = CONTAINING_RECORD(This, hdmi_mode_vec_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&vec->ref_count);
    if (ref == 0) {
        for (uint32_t i = 0; i < vec->count; ++i) {
            if (vec->items[i]) hdmi_mode_release(vec->items[i]);
        }
        uwp_ui_free(vec->items);
        uwp_ui_free(vec);
    }
    return ref;
}
static HRESULT STDMETHODCALLTYPE hmv_GetIids(UwpIVectorViewHdmiDisplayMode *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IVectorView_HdmiDisplayMode };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE hmv_GetRuntimeClassName(UwpIVectorViewHdmiDisplayMode *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Foundation.Collections.IVectorView`1<Windows.Graphics.Display.Core.HdmiDisplayMode>", name);
}
static HRESULT STDMETHODCALLTYPE hmv_GetTrustLevel(UwpIVectorViewHdmiDisplayMode *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hmv_GetAt(UwpIVectorViewHdmiDisplayMode *This, UINT32 index, UwpIHdmiDisplayMode **item) {
    hdmi_mode_vec_t *vec = CONTAINING_RECORD(This, hdmi_mode_vec_t, iface);
    if (!item) return E_POINTER;
    *item = NULL;
    if (index >= vec->count) return (HRESULT)0x8000000BL; /* E_BOUNDS */
    *item = &vec->items[index]->m1_iface;
    hdmi_mode_addref(vec->items[index]);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hmv_get_Size(UwpIVectorViewHdmiDisplayMode *This, UINT32 *size) {
    hdmi_mode_vec_t *vec = CONTAINING_RECORD(This, hdmi_mode_vec_t, iface);
    if (!size) return E_POINTER;
    *size = vec->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hmv_IndexOf(UwpIVectorViewHdmiDisplayMode *This, UwpIHdmiDisplayMode *value, UINT32 *index, boolean *found) {
    hdmi_mode_vec_t *vec = CONTAINING_RECORD(This, hdmi_mode_vec_t, iface);
    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    for (uint32_t i = 0; i < vec->count; ++i) {
        boolean eq = FALSE;
        if (SUCCEEDED(hm1_IsEqual(&vec->items[i]->m1_iface, value, &eq)) && eq) {
            *index = i;
            *found = TRUE;
            break;
        }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hmv_GetMany(UwpIVectorViewHdmiDisplayMode *This, UINT32 startIndex, UINT32 capacity, UwpIHdmiDisplayMode **items, UINT32 *actual) {
    hdmi_mode_vec_t *vec = CONTAINING_RECORD(This, hdmi_mode_vec_t, iface);
    if (!actual) return E_POINTER;
    *actual = 0;
    if (startIndex >= vec->count || capacity == 0 || !items) return S_OK;
    UINT32 n = vec->count - startIndex;
    if (n > capacity) n = capacity;
    for (UINT32 i = 0; i < n; ++i) {
        items[i] = &vec->items[startIndex + i]->m1_iface;
        hdmi_mode_addref(vec->items[startIndex + i]);
    }
    *actual = n;
    return S_OK;
}

static const UwpIVectorViewHdmiDisplayModeVtbl g_hmv_vtbl = {
    hmv_QueryInterface, hmv_AddRef, hmv_Release,
    hmv_GetIids, hmv_GetRuntimeClassName, hmv_GetTrustLevel,
    hmv_GetAt, hmv_get_Size, hmv_IndexOf, hmv_GetMany
};

/* --- IHdmiDisplayInformation --- */

static HRESULT STDMETHODCALLTYPE hdi_QueryInterface(UwpIHdmiDisplayInformation *This, REFIID riid, void **ppv) {
    hdmi_info_obj_t *obj = CONTAINING_RECORD(This, hdmi_info_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IHdmiDisplayInformation)) {
        *ppv = &obj->iface;
        uwp_ui_atomic_inc(&obj->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE hdi_AddRef(UwpIHdmiDisplayInformation *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, hdmi_info_obj_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE hdi_Release(UwpIHdmiDisplayInformation *This) {
    hdmi_info_obj_t *obj = CONTAINING_RECORD(This, hdmi_info_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&obj->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_hdmi_lock);
        if (uwp_ui_atomic_load(&obj->ref_count) == 0) {
            if (g_hdmi_singleton == obj) g_hdmi_singleton = NULL;
            uwp_ui_lock_release(&g_hdmi_lock);
            uwp_ui_free(obj);
            return 0;
        }
        uwp_ui_lock_release(&g_hdmi_lock);
    }
    return ref;
}
static HRESULT STDMETHODCALLTYPE hdi_GetIids(UwpIHdmiDisplayInformation *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IHdmiDisplayInformation };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE hdi_GetRuntimeClassName(UwpIHdmiDisplayInformation *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.Core.HdmiDisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE hdi_GetTrustLevel(UwpIHdmiDisplayInformation *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hdi_GetSupportedDisplayModes(UwpIHdmiDisplayInformation *This, UwpIVectorViewHdmiDisplayMode **result) {
    hdmi_mode_vec_t *vec;
    hdmi_mode_obj_t *cur;
    (void)This;
    if (!result) return E_POINTER;
    *result = NULL;
    cur = hdmi_mode_create_from_live();
    if (!cur) return E_OUTOFMEMORY;
    vec = (hdmi_mode_vec_t *)uwp_ui_alloc(sizeof(hdmi_mode_vec_t));
    if (!vec) {
        hdmi_mode_release(cur);
        return E_OUTOFMEMORY;
    }
    vec->items = (hdmi_mode_obj_t **)uwp_ui_alloc(sizeof(hdmi_mode_obj_t *));
    if (!vec->items) {
        uwp_ui_free(vec);
        hdmi_mode_release(cur);
        return E_OUTOFMEMORY;
    }
    vec->iface.lpVtbl = &g_hmv_vtbl;
    vec->ref_count = 1;
    vec->count = 1;
    vec->items[0] = cur;
    *result = &vec->iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hdi_GetCurrentDisplayMode(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode **result) {
    hdmi_mode_obj_t *mode;
    (void)This;
    if (!result) return E_POINTER;
    mode = hdmi_mode_create_from_live();
    if (!mode) return E_OUTOFMEMORY;
    *result = &mode->m1_iface;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hdi_SetDefaultDisplayModeAsync(UwpIHdmiDisplayInformation *This, void **op) {
    (void)This;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IHdmiDisplayInformation", "SetDefaultDisplayModeAsync");
}
static HRESULT STDMETHODCALLTYPE hdi_RequestSetCurrentDisplayModeAsync(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode *mode, void **op) {
    (void)This; (void)mode;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IHdmiDisplayInformation", "RequestSetCurrentDisplayModeAsync");
}
static HRESULT STDMETHODCALLTYPE hdi_RequestSetCurrentDisplayModeWithHdrAsync(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode *mode, UwpHdmiDisplayHdrOption hdrOption, void **op) {
    (void)This; (void)mode; (void)hdrOption;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IHdmiDisplayInformation", "RequestSetCurrentDisplayModeWithHdrAsync");
}
static HRESULT STDMETHODCALLTYPE hdi_RequestSetCurrentDisplayModeWithHdrAndMetadataAsync(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode *mode, UwpHdmiDisplayHdrOption hdrOption, UwpHdmiDisplayHdr2086Metadata meta, void **op) {
    (void)This; (void)mode; (void)hdrOption; (void)meta;
    if (op) *op = NULL;
    return uwp_ui_unimpl("IHdmiDisplayInformation", "RequestSetCurrentDisplayModeWithHdrAndMetadataAsync");
}
static HRESULT STDMETHODCALLTYPE hdi_add_DisplayModesChanged(UwpIHdmiDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_hdmi_modes_changed, handler, token);
}
static HRESULT STDMETHODCALLTYPE hdi_remove_DisplayModesChanged(UwpIHdmiDisplayInformation *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_hdmi_modes_changed, token);
}

static const UwpIHdmiDisplayInformationVtbl g_hdi_vtbl = {
    hdi_QueryInterface, hdi_AddRef, hdi_Release,
    hdi_GetIids, hdi_GetRuntimeClassName, hdi_GetTrustLevel,
    hdi_GetSupportedDisplayModes,
    hdi_GetCurrentDisplayMode,
    hdi_SetDefaultDisplayModeAsync,
    hdi_RequestSetCurrentDisplayModeAsync,
    hdi_RequestSetCurrentDisplayModeWithHdrAsync,
    hdi_RequestSetCurrentDisplayModeWithHdrAndMetadataAsync,
    hdi_add_DisplayModesChanged,
    hdi_remove_DisplayModesChanged
};

static HRESULT hdi_statics_qi(hdmi_info_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IHdmiDisplayInformationStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hdis_QueryInterface(UwpIHdmiDisplayInformationStatics *This, REFIID riid, void **ppv) {
    return hdi_statics_qi(CONTAINING_RECORD(This, hdmi_info_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hdis_AddRef(UwpIHdmiDisplayInformationStatics *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, hdmi_info_statics_t, statics_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE hdis_Release(UwpIHdmiDisplayInformationStatics *This) {
    hdmi_info_statics_t *fac = CONTAINING_RECORD(This, hdmi_info_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE hdis_GetIids(UwpIHdmiDisplayInformationStatics *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IHdmiDisplayInformationStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE hdis_GetRuntimeClassName(UwpIHdmiDisplayInformationStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.Core.HdmiDisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE hdis_GetTrustLevel(UwpIHdmiDisplayInformationStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hdis_GetForCurrentView(UwpIHdmiDisplayInformationStatics *This, UwpIHdmiDisplayInformation **result) {
    hdmi_info_obj_t *obj;
    (void)This;
    if (!result) return E_POINTER;
    uwp_ui_lock_acquire(&g_hdmi_lock);
    if (!g_hdmi_singleton) {
        obj = (hdmi_info_obj_t *)uwp_ui_alloc(sizeof(hdmi_info_obj_t));
        if (obj) {
            obj->iface.lpVtbl = &g_hdi_vtbl;
            obj->ref_count = 1;
            g_hdmi_singleton = obj;
        }
    } else {
        obj = g_hdmi_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_hdmi_lock);
    if (!obj) return E_OUTOFMEMORY;
    *result = &obj->iface;
    return S_OK;
}

static const UwpIHdmiDisplayInformationStaticsVtbl g_hdis_vtbl = {
    hdis_QueryInterface, hdis_AddRef, hdis_Release,
    hdis_GetIids, hdis_GetRuntimeClassName, hdis_GetTrustLevel,
    hdis_GetForCurrentView
};

static HRESULT STDMETHODCALLTYPE hdia_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return hdi_statics_qi(CONTAINING_RECORD(This, hdmi_info_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hdia_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, hdmi_info_statics_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE hdia_Release(IActivationFactory *This) {
    hdmi_info_statics_t *fac = CONTAINING_RECORD(This, hdmi_info_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE hdia_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IHdmiDisplayInformationStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE hdia_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Graphics.Display.Core.HdmiDisplayInformation", name);
}
static HRESULT STDMETHODCALLTYPE hdia_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hdia_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("HdmiDisplayInformationFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_hdia_vtbl = {
    hdia_QueryInterface, hdia_AddRef, hdia_Release,
    hdia_GetIids, hdia_GetRuntimeClassName, hdia_GetTrustLevel,
    hdia_ActivateInstance
};

HRESULT uwp_ui_hdmi_display_get_factory(REFIID iid, void **out_factory) {
    hdmi_info_statics_t *fac = (hdmi_info_statics_t *)uwp_ui_alloc(sizeof(hdmi_info_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_hdis_vtbl;
    fac->act_iface.lpVtbl = &g_hdia_vtbl;
    fac->ref_count = 1;
    HRESULT hr = hdi_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}
