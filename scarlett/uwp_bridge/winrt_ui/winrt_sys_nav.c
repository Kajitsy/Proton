/*
 * Proton-Scarlett — Windows.UI.Core.SystemNavigationManager
 *
 * Clean-room implementation of ISystemNavigationManagerStatics,
 * ISystemNavigationManager, ISystemNavigationManager2, and BackRequestedEventArgs.
 */

#include "winrt_ui_internal.h"

typedef struct back_req_args_s {
    UwpIBackRequestedEventArgs iface;
    volatile int32_t           ref_count;
    boolean                    handled;
} back_req_args_t;

typedef struct sys_nav_obj_s {
    UwpISystemNavigationManager  sn1_iface;
    UwpISystemNavigationManager2 sn2_iface;
    volatile int32_t             ref_count;
} sys_nav_obj_t;

typedef struct sys_nav_statics_s {
    UwpISystemNavigationManagerStatics statics_iface;
    IActivationFactory                 act_iface;
    volatile int32_t                   ref_count;
} sys_nav_statics_t;

static uwp_ui_lock_t                  g_sn_lock = UWP_UI_LOCK_INIT;
static sys_nav_obj_t                 *g_sn_singleton = NULL;
static UwpAppViewBackButtonVisibility g_back_btn_visibility = UwpAppViewBackButtonVisibility_Collapsed;
static uwp_ui_event_list_t            g_back_requested_events = UWP_UI_EVENT_LIST_INIT;

/* ============================================================================
 * BackRequestedEventArgs
 * ============================================================================ */

static HRESULT STDMETHODCALLTYPE bra_QueryInterface(UwpIBackRequestedEventArgs *This, REFIID riid, void **ppv) {
    back_req_args_t *args = CONTAINING_RECORD(This, back_req_args_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IBackRequestedEventArgs)) {
        *ppv = &args->iface;
        uwp_ui_atomic_inc(&args->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE bra_AddRef(UwpIBackRequestedEventArgs *This) {
    back_req_args_t *args = CONTAINING_RECORD(This, back_req_args_t, iface);
    return uwp_ui_atomic_inc(&args->ref_count);
}
static ULONG STDMETHODCALLTYPE bra_Release(UwpIBackRequestedEventArgs *This) {
    back_req_args_t *args = CONTAINING_RECORD(This, back_req_args_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&args->ref_count);
    if (ref == 0) uwp_ui_free(args);
    return ref;
}
static HRESULT STDMETHODCALLTYPE bra_GetIids(UwpIBackRequestedEventArgs *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IBackRequestedEventArgs };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE bra_GetRuntimeClassName(UwpIBackRequestedEventArgs *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.BackRequestedEventArgs", name);
}
static HRESULT STDMETHODCALLTYPE bra_GetTrustLevel(UwpIBackRequestedEventArgs *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bra_get_Handled(UwpIBackRequestedEventArgs *This, boolean *value) {
    back_req_args_t *args = CONTAINING_RECORD(This, back_req_args_t, iface);
    if (!value) return E_POINTER;
    *value = args->handled;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE bra_put_Handled(UwpIBackRequestedEventArgs *This, boolean value) {
    back_req_args_t *args = CONTAINING_RECORD(This, back_req_args_t, iface);
    args->handled = value ? TRUE : FALSE;
    return S_OK;
}

static const UwpIBackRequestedEventArgsVtbl g_bra_vtbl = {
    bra_QueryInterface, bra_AddRef, bra_Release,
    bra_GetIids, bra_GetRuntimeClassName, bra_GetTrustLevel,
    bra_get_Handled, bra_put_Handled
};

/* ============================================================================
 * SystemNavigationManager Instance
 * ============================================================================ */

static HRESULT sn_qi(sys_nav_obj_t *obj, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_ISystemNavigationManager)) {
        *ppv = &obj->sn1_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_ISystemNavigationManager2)) {
        *ppv = &obj->sn2_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&obj->ref_count);
    return S_OK;
}
static ULONG sn_addref(sys_nav_obj_t *obj) {
    return uwp_ui_atomic_inc(&obj->ref_count);
}
static ULONG sn_release(sys_nav_obj_t *obj) {
    ULONG ref = uwp_ui_atomic_dec(&obj->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_sn_lock);
        if (g_sn_singleton == obj) g_sn_singleton = NULL;
        uwp_ui_lock_release(&g_sn_lock);
        uwp_ui_free(obj);
    }
    return ref;
}
static HRESULT sn_get_iids(ULONG *c, IID **iids) {
    const GUID list[] = { SCARLETT_IID_ISystemNavigationManager, SCARLETT_IID_ISystemNavigationManager2 };
    return uwp_ui_get_iids(list, 2, c, iids);
}

static HRESULT STDMETHODCALLTYPE sn1_QueryInterface(UwpISystemNavigationManager *This, REFIID riid, void **ppv) {
    return sn_qi(CONTAINING_RECORD(This, sys_nav_obj_t, sn1_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE sn1_AddRef(UwpISystemNavigationManager *This) {
    return sn_addref(CONTAINING_RECORD(This, sys_nav_obj_t, sn1_iface));
}
static ULONG STDMETHODCALLTYPE sn1_Release(UwpISystemNavigationManager *This) {
    return sn_release(CONTAINING_RECORD(This, sys_nav_obj_t, sn1_iface));
}
static HRESULT STDMETHODCALLTYPE sn1_GetIids(UwpISystemNavigationManager *This, ULONG *c, IID **iids) {
    (void)This; return sn_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE sn1_GetRuntimeClassName(UwpISystemNavigationManager *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.SystemNavigationManager", name);
}
static HRESULT STDMETHODCALLTYPE sn1_GetTrustLevel(UwpISystemNavigationManager *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE sn1_add_BackRequested(UwpISystemNavigationManager *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_back_requested_events, handler, token);
}
static HRESULT STDMETHODCALLTYPE sn1_remove_BackRequested(UwpISystemNavigationManager *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_back_requested_events, token);
}

static const UwpISystemNavigationManagerVtbl g_sn1_vtbl = {
    sn1_QueryInterface, sn1_AddRef, sn1_Release,
    sn1_GetIids, sn1_GetRuntimeClassName, sn1_GetTrustLevel,
    sn1_add_BackRequested, sn1_remove_BackRequested
};

static HRESULT STDMETHODCALLTYPE sn2_QueryInterface(UwpISystemNavigationManager2 *This, REFIID riid, void **ppv) {
    return sn_qi(CONTAINING_RECORD(This, sys_nav_obj_t, sn2_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE sn2_AddRef(UwpISystemNavigationManager2 *This) {
    return sn_addref(CONTAINING_RECORD(This, sys_nav_obj_t, sn2_iface));
}
static ULONG STDMETHODCALLTYPE sn2_Release(UwpISystemNavigationManager2 *This) {
    return sn_release(CONTAINING_RECORD(This, sys_nav_obj_t, sn2_iface));
}
static HRESULT STDMETHODCALLTYPE sn2_GetIids(UwpISystemNavigationManager2 *This, ULONG *c, IID **iids) {
    (void)This; return sn_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE sn2_GetRuntimeClassName(UwpISystemNavigationManager2 *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.SystemNavigationManager", name);
}
static HRESULT STDMETHODCALLTYPE sn2_GetTrustLevel(UwpISystemNavigationManager2 *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE sn2_get_AppViewBackButtonVisibility(UwpISystemNavigationManager2 *This, UwpAppViewBackButtonVisibility *value) {
    (void)This;
    if (!value) return E_POINTER;
    uwp_ui_lock_acquire(&g_sn_lock);
    *value = g_back_btn_visibility;
    uwp_ui_lock_release(&g_sn_lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sn2_put_AppViewBackButtonVisibility(UwpISystemNavigationManager2 *This, UwpAppViewBackButtonVisibility value) {
    (void)This;
    uwp_ui_lock_acquire(&g_sn_lock);
    g_back_btn_visibility = value;
    uwp_ui_lock_release(&g_sn_lock);
    return S_OK;
}

static const UwpISystemNavigationManager2Vtbl g_sn2_vtbl = {
    sn2_QueryInterface, sn2_AddRef, sn2_Release,
    sn2_GetIids, sn2_GetRuntimeClassName, sn2_GetTrustLevel,
    sn2_get_AppViewBackButtonVisibility, sn2_put_AppViewBackButtonVisibility
};

static sys_nav_obj_t *sn_get_or_create_instance(void) {
    sys_nav_obj_t *obj;
    uwp_ui_lock_acquire(&g_sn_lock);
    if (!g_sn_singleton) {
        obj = (sys_nav_obj_t *)uwp_ui_alloc(sizeof(sys_nav_obj_t));
        if (obj) {
            obj->sn1_iface.lpVtbl = &g_sn1_vtbl;
            obj->sn2_iface.lpVtbl = &g_sn2_vtbl;
            obj->ref_count = 1;
            g_sn_singleton = obj;
        }
    } else {
        obj = g_sn_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_sn_lock);
    return obj;
}

/* ============================================================================
 * SystemNavigationManager Statics / Factory
 * ============================================================================ */

static HRESULT sn_statics_qi(sys_nav_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_ISystemNavigationManagerStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sns_QueryInterface(UwpISystemNavigationManagerStatics *This, REFIID riid, void **ppv) {
    return sn_statics_qi(CONTAINING_RECORD(This, sys_nav_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE sns_AddRef(UwpISystemNavigationManagerStatics *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, sys_nav_statics_t, statics_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE sns_Release(UwpISystemNavigationManagerStatics *This) {
    sys_nav_statics_t *fac = CONTAINING_RECORD(This, sys_nav_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE sns_GetIids(UwpISystemNavigationManagerStatics *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_ISystemNavigationManagerStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE sns_GetRuntimeClassName(UwpISystemNavigationManagerStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.SystemNavigationManager", name);
}
static HRESULT STDMETHODCALLTYPE sns_GetTrustLevel(UwpISystemNavigationManagerStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE sns_GetForCurrentView(UwpISystemNavigationManagerStatics *This, UwpISystemNavigationManager **current) {
    sys_nav_obj_t *obj;
    (void)This;
    if (!current) return E_POINTER;
    obj = sn_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    *current = &obj->sn1_iface;
    return S_OK;
}

static const UwpISystemNavigationManagerStaticsVtbl g_sns_vtbl = {
    sns_QueryInterface, sns_AddRef, sns_Release,
    sns_GetIids, sns_GetRuntimeClassName, sns_GetTrustLevel,
    sns_GetForCurrentView
};

static HRESULT STDMETHODCALLTYPE sna_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return sn_statics_qi(CONTAINING_RECORD(This, sys_nav_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE sna_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, sys_nav_statics_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE sna_Release(IActivationFactory *This) {
    sys_nav_statics_t *fac = CONTAINING_RECORD(This, sys_nav_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE sna_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_ISystemNavigationManagerStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE sna_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.SystemNavigationManager", name);
}
static HRESULT STDMETHODCALLTYPE sna_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE sna_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("SystemNavigationManagerFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_sna_vtbl = {
    sna_QueryInterface, sna_AddRef, sna_Release,
    sna_GetIids, sna_GetRuntimeClassName, sna_GetTrustLevel,
    sna_ActivateInstance
};

HRESULT uwp_ui_sys_nav_get_factory(REFIID iid, void **out_factory) {
    sys_nav_statics_t *fac = (sys_nav_statics_t *)uwp_ui_alloc(sizeof(sys_nav_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_sns_vtbl;
    fac->act_iface.lpVtbl = &g_sna_vtbl;
    fac->ref_count = 1;
    HRESULT hr = sn_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}

HRESULT uwp_winrt_ui_trigger_back_requested(int32_t *out_handled) {
    sys_nav_obj_t *sn = sn_get_or_create_instance();
    back_req_args_t *args;
    if (!sn) return E_OUTOFMEMORY;

    args = (back_req_args_t *)uwp_ui_alloc(sizeof(back_req_args_t));
    if (!args) {
        sn_release(sn);
        return E_OUTOFMEMORY;
    }
    args->iface.lpVtbl = &g_bra_vtbl;
    args->ref_count = 1;
    args->handled = FALSE;

    uwp_ui_event_invoke(&g_back_requested_events, &sn->sn1_iface, &args->iface);

    if (out_handled) {
        *out_handled = args->handled ? 1 : 0;
    }
    bra_Release(&args->iface);
    sn_release(sn);
    return S_OK;
}
