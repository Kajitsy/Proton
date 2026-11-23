/*
 * Proton-Scarlett — UWPHOST.dll CoreApplication & CoreApplicationView
 *
 * Implements:
 *   - Windows.ApplicationModel.Core.CoreApplication
 *       (ICoreApplication, ICoreApplication2, ICoreApplicationExit,
 *        ICoreImmersiveApplication, ICoreApplicationUnhandledError,
 *        ICoreApplicationUseCount, IActivationFactory)
 *   - Windows.ApplicationModel.Core.CoreApplicationView
 *       (ICoreApplicationView, ICoreApplicationView2, ICoreApplicationView3,
 *        ICoreApplicationView5)
 *   - Windows.ApplicationModel.Core.CoreApplicationViewTitleBar
 *       (ICoreApplicationViewTitleBar)
 *   - Real IFrameworkViewSource -> IFrameworkView lifecycle in CoreApplication::Run
 */

#include "uwphost_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CoreApplicationViewTitleBar
 * ============================================================================ */

typedef struct UwpTitleBarObj {
    UwpICoreApplicationViewTitleBar iface;
    volatile int32_t ref;
    uint8_t extend_into_titlebar;
    uint8_t is_visible;
    uwp_event_list_t ev_layout_changed;
    uwp_event_list_t ev_visible_changed;
} UwpTitleBarObj;

static HRESULT UWP_CALL tb_QI(UwpICoreApplicationViewTitleBar *T, REFIID r, void **p) {
    if (!r || !p) return E_INVALIDARG;
    *p = NULL;
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) ||
        uwp_guid_equal(r, &UWP_IID_IInspectable) ||
        uwp_guid_equal(r, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(r, &UWP_IID_ICoreApplicationViewTitleBar)) {
        T->lpVtbl->AddRef(T);
        *p = T;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG UWP_CALL tb_AddRef(UwpICoreApplicationViewTitleBar *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->ref);
}
static ULONG UWP_CALL tb_Release(UwpICoreApplicationViewTitleBar *T) {
    UwpTitleBarObj *o = UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) {
        uwp_event_list_destroy(&o->ev_layout_changed);
        uwp_event_list_destroy(&o->ev_visible_changed);
        free(o);
    }
    return (ULONG)r;
}
static HRESULT UWP_CALL tb_GetIids(UwpICoreApplicationViewTitleBar *T, ULONG *c, IID **i) {
    (void)T;
    const GUID list[1] = { UWP_IID_ICoreApplicationViewTitleBar };
    return uwp_alloc_iids(list, 1u, c, i);
}
static HRESULT UWP_CALL tb_GetName(UwpICoreApplicationViewTitleBar *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplicationViewTitleBar", n);
}
static HRESULT UWP_CALL tb_GetTrust(UwpICoreApplicationViewTitleBar *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL tb_put_Extend(UwpICoreApplicationViewTitleBar *T, uint8_t v) {
    UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->extend_into_titlebar = v ? 1u : 0u;
    return S_OK;
}
static HRESULT UWP_CALL tb_get_Extend(UwpICoreApplicationViewTitleBar *T, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->extend_into_titlebar;
    return S_OK;
}
static HRESULT UWP_CALL tb_get_LeftInset(UwpICoreApplicationViewTitleBar *T, double *v) {
    (void)T; if (!v) return E_INVALIDARG; *v = 0.0; return S_OK;
}
static HRESULT UWP_CALL tb_get_RightInset(UwpICoreApplicationViewTitleBar *T, double *v) {
    (void)T; if (!v) return E_INVALIDARG; *v = 0.0; return S_OK;
}
static HRESULT UWP_CALL tb_get_Height(UwpICoreApplicationViewTitleBar *T, double *v) {
    (void)T; if (!v) return E_INVALIDARG; *v = 32.0; return S_OK;
}
static HRESULT UWP_CALL tb_add_Layout(UwpICoreApplicationViewTitleBar *T, UwpTypedEventHandler *h,
                                      UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->ev_layout_changed, h, tok);
}
static HRESULT UWP_CALL tb_remove_Layout(UwpICoreApplicationViewTitleBar *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->ev_layout_changed, tok);
}
static HRESULT UWP_CALL tb_get_IsVisible(UwpICoreApplicationViewTitleBar *T, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->is_visible;
    return S_OK;
}
static HRESULT UWP_CALL tb_add_Visible(UwpICoreApplicationViewTitleBar *T, UwpTypedEventHandler *h,
                                       UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->ev_visible_changed, h, tok);
}
static HRESULT UWP_CALL tb_remove_Visible(UwpICoreApplicationViewTitleBar *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpTitleBarObj, iface)->ev_visible_changed, tok);
}

static const UwpICoreApplicationViewTitleBarVtbl g_tb_vtbl = {
    tb_QI, tb_AddRef, tb_Release,
    tb_GetIids, tb_GetName, tb_GetTrust,
    tb_put_Extend, tb_get_Extend,
    tb_get_LeftInset, tb_get_RightInset, tb_get_Height,
    tb_add_Layout, tb_remove_Layout,
    tb_get_IsVisible, tb_add_Visible, tb_remove_Visible
};

static UwpTitleBarObj *uwp_create_titlebar(void) {
    UwpTitleBarObj *o = (UwpTitleBarObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->iface.lpVtbl = &g_tb_vtbl;
    o->ref = 1;
    o->is_visible = 1u;
    uwp_event_list_init(&o->ev_layout_changed);
    uwp_event_list_init(&o->ev_visible_changed);
    return o;
}

/* ============================================================================
 * CoreApplicationView
 * ============================================================================ */

typedef struct UwpCoreAppViewObj {
    UwpICoreApplicationView  view;
    UwpICoreApplicationView2 view2;
    UwpICoreApplicationView3 view3;
    UwpICoreApplicationView5 view5;
    volatile int32_t ref;
    uwp_mutex_t lock;
    uint8_t is_main;
    UwpCoreWindowObj *core_window;
    UwpTitleBarObj   *title_bar;
    UwpIMap          *properties;
    uwp_event_list_t  ev_activated;
    uwp_event_list_t  ev_hosted_closing;
} UwpCoreAppViewObj;

static void view_destroy(UwpCoreAppViewObj *v) {
    if (v->core_window) {
        UwpICoreWindow *cw = uwp_corewindow_iface(v->core_window);
        if (cw) {
            /* Release the extra ref from uwp_corewindow_iface AND our own ref */
            cw->lpVtbl->Release(cw);
            cw->lpVtbl->Release(cw);
        }
        v->core_window = NULL;
    }
    if (v->title_bar) v->title_bar->iface.lpVtbl->Release(&v->title_bar->iface);
    if (v->properties) v->properties->lpVtbl->Release(v->properties);
    uwp_event_list_destroy(&v->ev_activated);
    uwp_event_list_destroy(&v->ev_hosted_closing);
    uwp_mutex_destroy(&v->lock);
    free(v);
}

static HRESULT view_qi_impl(UwpCoreAppViewObj *v, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ICoreApplicationView)) {
        uwp_atomic_inc(&v->ref);
        *ppv = &v->view;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplicationView2)) {
        uwp_atomic_inc(&v->ref);
        *ppv = &v->view2;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplicationView3)) {
        uwp_atomic_inc(&v->ref);
        *ppv = &v->view3;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplicationView5)) {
        uwp_atomic_inc(&v->ref);
        *ppv = &v->view5;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT UWP_CALL v1_QI(UwpICoreApplicationView *T, REFIID r, void **p) {
    return view_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view), r, p);
}
static ULONG UWP_CALL v1_AddRef(UwpICoreApplicationView *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view)->ref);
}
static ULONG UWP_CALL v1_Release(UwpICoreApplicationView *T) {
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view);
    int32_t r = uwp_atomic_dec(&v->ref);
    if (r == 0) view_destroy(v);
    return (ULONG)r;
}
static HRESULT view_get_iids_impl(ULONG *c, IID **i) {
    const GUID list[4] = {
        UWP_IID_ICoreApplicationView,
        UWP_IID_ICoreApplicationView2,
        UWP_IID_ICoreApplicationView3,
        UWP_IID_ICoreApplicationView5
    };
    return uwp_alloc_iids(list, 4u, c, i);
}

static HRESULT UWP_CALL v1_GetIids(UwpICoreApplicationView *T, ULONG *c, IID **i) {
    (void)T; return view_get_iids_impl(c, i);
}
static HRESULT UWP_CALL v1_GetName(UwpICoreApplicationView *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplicationView", n);
}
static HRESULT UWP_CALL v1_GetTrust(UwpICoreApplicationView *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL v1_get_CoreWindow(UwpICoreApplicationView *T, void **val) {
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view);
    *val = uwp_corewindow_iface(v->core_window);
    return *val ? S_OK : E_FAIL;
}
static HRESULT UWP_CALL v1_add_Activated(UwpICoreApplicationView *T, UwpTypedEventHandler *h,
                                         UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view)->ev_activated, h, tok);
}
static HRESULT UWP_CALL v1_remove_Activated(UwpICoreApplicationView *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view)->ev_activated, tok);
}
static HRESULT UWP_CALL v1_get_IsMain(UwpICoreApplicationView *T, uint8_t *val) {
    if (!val) return E_INVALIDARG;
    *val = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view)->is_main;
    return S_OK;
}
static HRESULT UWP_CALL v1_get_IsHosted(UwpICoreApplicationView *T, uint8_t *val) {
    (void)T; if (!val) return E_INVALIDARG; *val = 0u; return S_OK;
}

static HRESULT UWP_CALL v2_QI(UwpICoreApplicationView2 *T, REFIID r, void **p) {
    return view_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view2), r, p);
}
static ULONG UWP_CALL v2_AddRef(UwpICoreApplicationView2 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view2)->ref);
}
static ULONG UWP_CALL v2_Release(UwpICoreApplicationView2 *T) {
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view2);
    int32_t r = uwp_atomic_dec(&v->ref);
    if (r == 0) view_destroy(v);
    return (ULONG)r;
}
static HRESULT UWP_CALL v2_GetIids(UwpICoreApplicationView2 *T, ULONG *c, IID **i) {
    (void)T; return view_get_iids_impl(c, i);
}
static HRESULT UWP_CALL v2_GetName(UwpICoreApplicationView2 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplicationView", n);
}
static HRESULT UWP_CALL v2_GetTrust(UwpICoreApplicationView2 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL v2_get_Dispatcher(UwpICoreApplicationView2 *T, void **val) {
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view2);
    *val = uwp_corewindow_dispatcher_iface(v->core_window);
    return *val ? S_OK : E_FAIL;
}

static HRESULT UWP_CALL v3_QI(UwpICoreApplicationView3 *T, REFIID r, void **p) {
    return view_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view3), r, p);
}
static ULONG UWP_CALL v3_AddRef(UwpICoreApplicationView3 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view3)->ref);
}
static ULONG UWP_CALL v3_Release(UwpICoreApplicationView3 *T) {
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view3);
    int32_t r = uwp_atomic_dec(&v->ref);
    if (r == 0) view_destroy(v);
    return (ULONG)r;
}
static HRESULT UWP_CALL v3_GetIids(UwpICoreApplicationView3 *T, ULONG *c, IID **i) {
    (void)T; return view_get_iids_impl(c, i);
}
static HRESULT UWP_CALL v3_GetName(UwpICoreApplicationView3 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplicationView", n);
}
static HRESULT UWP_CALL v3_GetTrust(UwpICoreApplicationView3 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL v3_get_IsComponent(UwpICoreApplicationView3 *T, uint8_t *val) {
    (void)T; if (!val) return E_INVALIDARG; *val = 0u; return S_OK;
}
static HRESULT UWP_CALL v3_get_TitleBar(UwpICoreApplicationView3 *T, void **val) {
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view3);
    uwp_mutex_lock(&v->lock);
    if (!v->title_bar) v->title_bar = uwp_create_titlebar();
    *val = v->title_bar ? &v->title_bar->iface : NULL;
    if (v->title_bar) v->title_bar->iface.lpVtbl->AddRef(&v->title_bar->iface);
    uwp_mutex_unlock(&v->lock);
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT UWP_CALL v3_add_Closing(UwpICoreApplicationView3 *T, UwpTypedEventHandler *h,
                                       UwpEventRegistrationToken *tok) {
    return uwp_event_list_add(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view3)->ev_hosted_closing, h, tok);
}
static HRESULT UWP_CALL v3_remove_Closing(UwpICoreApplicationView3 *T, UwpEventRegistrationToken tok) {
    return uwp_event_list_remove(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view3)->ev_hosted_closing, tok);
}

static HRESULT UWP_CALL v5_QI(UwpICoreApplicationView5 *T, REFIID r, void **p) {
    return view_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view5), r, p);
}
static ULONG UWP_CALL v5_AddRef(UwpICoreApplicationView5 *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view5)->ref);
}
static ULONG UWP_CALL v5_Release(UwpICoreApplicationView5 *T) {
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view5);
    int32_t r = uwp_atomic_dec(&v->ref);
    if (r == 0) view_destroy(v);
    return (ULONG)r;
}
static HRESULT UWP_CALL v5_GetIids(UwpICoreApplicationView5 *T, ULONG *c, IID **i) {
    (void)T; return view_get_iids_impl(c, i);
}
static HRESULT UWP_CALL v5_GetName(UwpICoreApplicationView5 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplicationView", n);
}
static HRESULT UWP_CALL v5_GetTrust(UwpICoreApplicationView5 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL v5_get_Properties(UwpICoreApplicationView5 *T, void **val) {
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *v = UWP_CONTAINING_RECORD(T, UwpCoreAppViewObj, view5);
    uwp_mutex_lock(&v->lock);
    if (!v->properties) v->properties = uwp_create_property_set();
    *val = v->properties;
    if (v->properties) v->properties->lpVtbl->AddRef(v->properties);
    uwp_mutex_unlock(&v->lock);
    return *val ? S_OK : E_OUTOFMEMORY;
}

static const UwpICoreApplicationViewVtbl g_v1_vtbl = {
    v1_QI, v1_AddRef, v1_Release,
    v1_GetIids, v1_GetName, v1_GetTrust,
    v1_get_CoreWindow, v1_add_Activated, v1_remove_Activated,
    v1_get_IsMain, v1_get_IsHosted
};
static const UwpICoreApplicationView2Vtbl g_v2_vtbl = {
    v2_QI, v2_AddRef, v2_Release,
    v2_GetIids, v2_GetName, v2_GetTrust,
    v2_get_Dispatcher
};
static const UwpICoreApplicationView3Vtbl g_v3_vtbl = {
    v3_QI, v3_AddRef, v3_Release,
    v3_GetIids, v3_GetName, v3_GetTrust,
    v3_get_IsComponent, v3_get_TitleBar,
    v3_add_Closing, v3_remove_Closing
};
static const UwpICoreApplicationView5Vtbl g_v5_vtbl = {
    v5_QI, v5_AddRef, v5_Release,
    v5_GetIids, v5_GetName, v5_GetTrust,
    v5_get_Properties
};

/* ============================================================================
 * CoreApplication Singleton State & Methods
 * ============================================================================ */

typedef struct UwpCoreAppSingleton {
    UwpIActivationFactory             factory;
    UwpICoreApplication               core_app;
    UwpICoreApplication2              core_app2;
    UwpICoreApplicationExit           core_exit;
    UwpICoreImmersiveApplication      immersive;
    UwpICoreApplicationUnhandledError unhandled_err;
    UwpICoreApplicationUseCount       use_count;

    uwp_mutex_t lock;
    int initialized;
    int headless;
    int exiting;
    uint8_t prelaunch_enabled;
    volatile int32_t app_use_count;

    UwpCoreAppViewObj *main_view;
    UwpIMap           *properties;
    UwpIGetActivationFactory *custom_factory;

    uwp_event_list_t ev_suspending;
    uwp_event_list_t ev_resuming;
    uwp_event_list_t ev_bg_activated;
    uwp_event_list_t ev_leaving_bg;
    uwp_event_list_t ev_entered_bg;
    uwp_event_list_t ev_exiting;
    uwp_event_list_t ev_unhandled_err;
} UwpCoreAppSingleton;

static UwpCoreAppSingleton g_core_app;

static void uwp_coreapp_ensure_init(void) {
    if (g_core_app.initialized) return;
    uwp_mutex_init(&g_core_app.lock);
    uwp_event_list_init(&g_core_app.ev_suspending);
    uwp_event_list_init(&g_core_app.ev_resuming);
    uwp_event_list_init(&g_core_app.ev_bg_activated);
    uwp_event_list_init(&g_core_app.ev_leaving_bg);
    uwp_event_list_init(&g_core_app.ev_entered_bg);
    uwp_event_list_init(&g_core_app.ev_exiting);
    uwp_event_list_init(&g_core_app.ev_unhandled_err);
    g_core_app.initialized = 1;
}

static UwpCoreAppViewObj *uwp_coreapp_get_or_create_main_view(void) {
    uwp_coreapp_ensure_init();
    uwp_mutex_lock(&g_core_app.lock);
    if (!g_core_app.main_view) {
        UwpCoreAppViewObj *v = (UwpCoreAppViewObj *)calloc(1, sizeof(*v));
        if (v) {
            v->view.lpVtbl  = &g_v1_vtbl;
            v->view2.lpVtbl = &g_v2_vtbl;
            v->view3.lpVtbl = &g_v3_vtbl;
            v->view5.lpVtbl = &g_v5_vtbl;
            v->ref = 1;
            v->is_main = 1u;
            uwp_mutex_init(&v->lock);
            uwp_event_list_init(&v->ev_activated);
            uwp_event_list_init(&v->ev_hosted_closing);
            v->core_window = uwp_corewindow_create(0, 0, NULL, g_core_app.headless);
            if (!v->core_window) {
                view_destroy(v);
                v = NULL;
            }
            g_core_app.main_view = v;
        }
    }
    UwpCoreAppViewObj *res = g_core_app.main_view;
    if (res) {
        uwp_atomic_inc(&res->ref);
    }
    uwp_mutex_unlock(&g_core_app.lock);
    return res;
}

void uwp_coreapp_set_headless(int headless) {
    uwp_coreapp_ensure_init();
    g_core_app.headless = headless ? 1 : 0;
}

static HRESULT coreapp_qi_impl(REFIID riid, void **ppv) {
    uwp_coreapp_ensure_init();
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IActivationFactory)) {
        *ppv = &g_core_app.factory;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplication)) {
        *ppv = &g_core_app.core_app;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplication2)) {
        *ppv = &g_core_app.core_app2;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplicationExit)) {
        *ppv = &g_core_app.core_exit;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreImmersiveApplication)) {
        *ppv = &g_core_app.immersive;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplicationUnhandledError)) {
        *ppv = &g_core_app.unhandled_err;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreApplicationUseCount)) {
        *ppv = &g_core_app.use_count;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT coreapp_get_iids_impl(ULONG *c, IID **i) {
    const GUID list[7] = {
        UWP_IID_IActivationFactory,
        UWP_IID_ICoreApplication,
        UWP_IID_ICoreApplication2,
        UWP_IID_ICoreApplicationExit,
        UWP_IID_ICoreImmersiveApplication,
        UWP_IID_ICoreApplicationUnhandledError,
        UWP_IID_ICoreApplicationUseCount
    };
    return uwp_alloc_iids(list, 7u, c, i);
}

/* IActivationFactory */
static HRESULT UWP_CALL caf_QI(UwpIActivationFactory *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL caf_AddRef(UwpIActivationFactory *T) { (void)T; return 1u; }
static ULONG UWP_CALL caf_Release(UwpIActivationFactory *T) { (void)T; return 1u; }
static HRESULT UWP_CALL caf_GetIids(UwpIActivationFactory *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL caf_GetName(UwpIActivationFactory *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL caf_GetTrust(UwpIActivationFactory *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL caf_ActivateInstance(UwpIActivationFactory *T, UwpIInspectable **inst) {
    (void)T; if (inst) *inst = NULL;
    return E_NOTIMPL; /* CoreApplication is a static runtimeclass */
}

/* ICoreApplication */
static HRESULT UWP_CALL ca_QI(UwpICoreApplication *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL ca_AddRef(UwpICoreApplication *T) { (void)T; return 1u; }
static ULONG UWP_CALL ca_Release(UwpICoreApplication *T) { (void)T; return 1u; }
static HRESULT UWP_CALL ca_GetIids(UwpICoreApplication *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL ca_GetName(UwpICoreApplication *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL ca_GetTrust(UwpICoreApplication *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL ca_get_Id(UwpICoreApplication *T, UWP_HSTRING *val) {
    (void)T;
    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    return uwp_hstring_create_from_wstr((id && id->app_id[0]) ? id->app_id : L"App", val);
}
static HRESULT UWP_CALL ca_add_Suspending(UwpICoreApplication *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_suspending, h, tok);
}
static HRESULT UWP_CALL ca_remove_Suspending(UwpICoreApplication *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_suspending, tok);
}
static HRESULT UWP_CALL ca_add_Resuming(UwpICoreApplication *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_resuming, h, tok);
}
static HRESULT UWP_CALL ca_remove_Resuming(UwpICoreApplication *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_resuming, tok);
}
static HRESULT UWP_CALL ca_get_Properties(UwpICoreApplication *T, void **val) {
    (void)T;
    if (!val) return E_INVALIDARG;
    uwp_coreapp_ensure_init();
    uwp_mutex_lock(&g_core_app.lock);
    if (!g_core_app.properties) g_core_app.properties = uwp_create_property_set();
    *val = g_core_app.properties;
    if (g_core_app.properties) g_core_app.properties->lpVtbl->AddRef(g_core_app.properties);
    uwp_mutex_unlock(&g_core_app.lock);
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT UWP_CALL ca_GetCurrentView(UwpICoreApplication *T, void **val) {
    (void)T;
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *v = uwp_coreapp_get_or_create_main_view();
    *val = v ? &v->view : NULL;
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT UWP_CALL ca_Run(UwpICoreApplication *T, UwpIFrameworkViewSource *view_source) {
    (void)T;
    return uwp_coreapp_run_view_source(view_source);
}
static HRESULT UWP_CALL ca_RunWithActivationFactories(UwpICoreApplication *T,
                                                      UwpIGetActivationFactory *factory) {
    (void)T;
    if (!factory) return E_INVALIDARG;
    uwp_coreapp_ensure_init();

    uwp_mutex_lock(&g_core_app.lock);
    UwpIGetActivationFactory *old = g_core_app.custom_factory;
    factory->lpVtbl->AddRef(factory);
    g_core_app.custom_factory = factory;
    uwp_mutex_unlock(&g_core_app.lock);
    if (old) old->lpVtbl->Release(old);

    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    if (id && id->entry_point[0] != L'\0') {
        UWP_HSTRING ep_hs = NULL;
        if (SUCCEEDED(uwp_hstring_create_from_wstr(id->entry_point, &ep_hs)) && ep_hs) {
            UwpIInspectable *af_insp = NULL;
            HRESULT hr = factory->lpVtbl->GetActivationFactory(factory, ep_hs, &af_insp);
            WindowsDeleteString(ep_hs);
            if (SUCCEEDED(hr) && af_insp) {
                UwpIActivationFactory *af = NULL;
                hr = af_insp->lpVtbl->QueryInterface(af_insp, &UWP_IID_IActivationFactory, (void **)&af);
                af_insp->lpVtbl->Release(af_insp);
                if (SUCCEEDED(hr) && af) {
                    UwpIInspectable *inst = NULL;
                    hr = af->lpVtbl->ActivateInstance(af, &inst);
                    af->lpVtbl->Release(af);
                    if (SUCCEEDED(hr) && inst) {
                        UwpIFrameworkViewSource *vs = NULL;
                        hr = inst->lpVtbl->QueryInterface(inst, &UWP_IID_IFrameworkViewSource, (void **)&vs);
                        inst->lpVtbl->Release(inst);
                        if (SUCCEEDED(hr) && vs) {
                            hr = uwp_coreapp_run_view_source(vs);
                            vs->lpVtbl->Release(vs);
                            return hr;
                        }
                    }
                }
            }
        }
    }

    uwp_bridge_log("coreapp", "RunWithActivationFactories could not activate manifest entry_point as IFrameworkViewSource");
    return E_NOTIMPL;
}

/* ICoreApplication2 */
static HRESULT UWP_CALL ca2_QI(UwpICoreApplication2 *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL ca2_AddRef(UwpICoreApplication2 *T) { (void)T; return 1u; }
static ULONG UWP_CALL ca2_Release(UwpICoreApplication2 *T) { (void)T; return 1u; }
static HRESULT UWP_CALL ca2_GetIids(UwpICoreApplication2 *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL ca2_GetName(UwpICoreApplication2 *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL ca2_GetTrust(UwpICoreApplication2 *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL ca2_add_BgAct(UwpICoreApplication2 *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_bg_activated, h, tok);
}
static HRESULT UWP_CALL ca2_remove_BgAct(UwpICoreApplication2 *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_bg_activated, tok);
}
static HRESULT UWP_CALL ca2_add_LeaveBg(UwpICoreApplication2 *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_leaving_bg, h, tok);
}
static HRESULT UWP_CALL ca2_remove_LeaveBg(UwpICoreApplication2 *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_leaving_bg, tok);
}
static HRESULT UWP_CALL ca2_add_EnterBg(UwpICoreApplication2 *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_entered_bg, h, tok);
}
static HRESULT UWP_CALL ca2_remove_EnterBg(UwpICoreApplication2 *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_entered_bg, tok);
}
static HRESULT UWP_CALL ca2_EnablePrelaunch(UwpICoreApplication2 *T, uint8_t value) {
    (void)T; uwp_coreapp_ensure_init();
    g_core_app.prelaunch_enabled = value ? 1u : 0u;
    return S_OK;
}

/* ICoreApplicationExit */
static HRESULT UWP_CALL cae_QI(UwpICoreApplicationExit *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL cae_AddRef(UwpICoreApplicationExit *T) { (void)T; return 1u; }
static ULONG UWP_CALL cae_Release(UwpICoreApplicationExit *T) { (void)T; return 1u; }
static HRESULT UWP_CALL cae_GetIids(UwpICoreApplicationExit *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL cae_GetName(UwpICoreApplicationExit *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL cae_GetTrust(UwpICoreApplicationExit *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cae_Exit(UwpICoreApplicationExit *T) {
    (void)T;
    uwp_coreapp_ensure_init();
    uwp_bridge_log("coreapp", "CoreApplication::Exit() invoked");
    g_core_app.exiting = 1;
    uwp_event_list_invoke(&g_core_app.ev_exiting, NULL, NULL);
    UwpCoreWindowObj *mw = uwp_corewindow_get_main();
    if (mw) {
        uwp_corewindow_request_close(mw);
    }
    return S_OK;
}
static HRESULT UWP_CALL cae_add_Exiting(UwpICoreApplicationExit *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_exiting, h, tok);
}
static HRESULT UWP_CALL cae_remove_Exiting(UwpICoreApplicationExit *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_exiting, tok);
}

/* Single-element IVectorView<CoreApplicationView*> + IIterable/IIterator */
typedef struct UwpViewIterIface UwpViewIterIface;
typedef struct UwpViewIterVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpViewIterIface);
    HRESULT (UWP_CALL *get_Current)(UwpViewIterIface *This, void **current);
    HRESULT (UWP_CALL *get_HasCurrent)(UwpViewIterIface *This, uint8_t *has_current);
    HRESULT (UWP_CALL *MoveNext)(UwpViewIterIface *This, uint8_t *has_current);
    HRESULT (UWP_CALL *GetMany)(UwpViewIterIface *This, uint32_t cap, void **items, uint32_t *actual);
} UwpViewIterVtbl;
struct UwpViewIterIface { const UwpViewIterVtbl *lpVtbl; };

typedef struct UwpViewIterObj {
    UwpViewIterIface iface;
    volatile int32_t ref;
    UwpCoreAppViewObj *view;
    uint32_t index;
} UwpViewIterObj;

static HRESULT UWP_CALL vi_QI(UwpViewIterIface *T, REFIID r, void **p) {
    if (!r || !p) return E_INVALIDARG;
    *p = NULL;
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) ||
        uwp_guid_equal(r, &UWP_IID_IInspectable) ||
        uwp_guid_equal(r, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(r, &UWP_IID_IIterator_CoreApplicationView)) {
        T->lpVtbl->AddRef(T);
        *p = T;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG UWP_CALL vi_AddRef(UwpViewIterIface *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpViewIterObj, iface)->ref);
}
static ULONG UWP_CALL vi_Release(UwpViewIterIface *T) {
    UwpViewIterObj *o = UWP_CONTAINING_RECORD(T, UwpViewIterObj, iface);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) {
        if (o->view) o->view->view.lpVtbl->Release(&o->view->view);
        free(o);
    }
    return (ULONG)r;
}
static HRESULT UWP_CALL vi_GetIids(UwpViewIterIface *T, ULONG *c, IID **i) {
    (void)T;
    const GUID list[1] = { UWP_IID_IIterator_CoreApplicationView };
    return uwp_alloc_iids(list, 1u, c, i);
}
static HRESULT UWP_CALL vi_GetName(UwpViewIterIface *T, UWP_HSTRING *n) {
    (void)T;
    return uwp_hstring_create_from_wstr(L"Windows.Foundation.Collections.IIterator`1<Windows.ApplicationModel.Core.CoreApplicationView>", n);
}
static HRESULT UWP_CALL vi_GetTrust(UwpViewIterIface *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL vi_get_Current(UwpViewIterIface *T, void **cur) {
    if (!cur) return E_INVALIDARG;
    UwpViewIterObj *o = UWP_CONTAINING_RECORD(T, UwpViewIterObj, iface);
    if (o->index > 0 || !o->view) { *cur = NULL; return E_BOUNDS; }
    o->view->view.lpVtbl->AddRef(&o->view->view);
    *cur = &o->view->view;
    return S_OK;
}
static HRESULT UWP_CALL vi_get_HasCurrent(UwpViewIterIface *T, uint8_t *has) {
    if (!has) return E_INVALIDARG;
    UwpViewIterObj *o = UWP_CONTAINING_RECORD(T, UwpViewIterObj, iface);
    *has = (o->index == 0 && o->view) ? 1u : 0u;
    return S_OK;
}
static HRESULT UWP_CALL vi_MoveNext(UwpViewIterIface *T, uint8_t *has) {
    if (!has) return E_INVALIDARG;
    UwpViewIterObj *o = UWP_CONTAINING_RECORD(T, UwpViewIterObj, iface);
    if (o->index == 0) o->index = 1;
    *has = 0u;
    return S_OK;
}
static HRESULT UWP_CALL vi_GetMany(UwpViewIterIface *T, uint32_t cap, void **items, uint32_t *actual) {
    if (!actual) return E_INVALIDARG;
    UwpViewIterObj *o = UWP_CONTAINING_RECORD(T, UwpViewIterObj, iface);
    if (cap > 0 && items && o->index == 0 && o->view) {
        o->view->view.lpVtbl->AddRef(&o->view->view);
        items[0] = &o->view->view;
        o->index = 1;
        *actual = 1u;
    } else {
        *actual = 0u;
    }
    return S_OK;
}
static const UwpViewIterVtbl g_vi_vtbl = {
    vi_QI, vi_AddRef, vi_Release, vi_GetIids, vi_GetName, vi_GetTrust,
    vi_get_Current, vi_get_HasCurrent, vi_MoveNext, vi_GetMany
};

typedef struct UwpViewVecIface UwpViewVecIface;
typedef struct UwpViewVecVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpViewVecIface);
    HRESULT (UWP_CALL *GetAt)(UwpViewVecIface *This, uint32_t index, void **item);
    HRESULT (UWP_CALL *get_Size)(UwpViewVecIface *This, uint32_t *size);
    HRESULT (UWP_CALL *IndexOf)(UwpViewVecIface *This, void *value, uint32_t *index, uint8_t *found);
    HRESULT (UWP_CALL *GetMany)(UwpViewVecIface *This, uint32_t start, uint32_t cap, void **items, uint32_t *actual);
} UwpViewVecVtbl;
struct UwpViewVecIface { const UwpViewVecVtbl *lpVtbl; };

typedef struct UwpViewIterableIface UwpViewIterableIface;
typedef struct UwpViewIterableVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpViewIterableIface);
    HRESULT (UWP_CALL *First)(UwpViewIterableIface *This, void **iter);
} UwpViewIterableVtbl;
struct UwpViewIterableIface { const UwpViewIterableVtbl *lpVtbl; };

typedef struct UwpCoreAppViewsObj {
    UwpViewVecIface vec;
    UwpViewIterableIface iterable;
    volatile int32_t ref;
    UwpCoreAppViewObj *view;
} UwpCoreAppViewsObj;

static HRESULT vv_qi_impl(UwpCoreAppViewsObj *o, REFIID r, void **p) {
    if (!r || !p) return E_INVALIDARG;
    *p = NULL;
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) ||
        uwp_guid_equal(r, &UWP_IID_IInspectable) ||
        uwp_guid_equal(r, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(r, &UWP_IID_IVectorView_CoreApplicationView)) {
        uwp_atomic_inc(&o->ref);
        *p = &o->vec;
        return S_OK;
    }
    if (uwp_guid_equal(r, &UWP_IID_IIterable_CoreApplicationView)) {
        uwp_atomic_inc(&o->ref);
        *p = &o->iterable;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT vv_get_iids(ULONG *c, IID **i) {
    const GUID list[2] = { UWP_IID_IVectorView_CoreApplicationView, UWP_IID_IIterable_CoreApplicationView };
    return uwp_alloc_iids(list, 2u, c, i);
}
static void vv_free(UwpCoreAppViewsObj *o) {
    if (o->view) o->view->view.lpVtbl->Release(&o->view->view);
    free(o);
}
static HRESULT UWP_CALL vv_QI(UwpViewVecIface *T, REFIID r, void **p) {
    return vv_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec), r, p);
}
static ULONG UWP_CALL vv_AddRef(UwpViewVecIface *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec)->ref);
}
static ULONG UWP_CALL vv_Release(UwpViewVecIface *T) {
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) vv_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL vv_GetIids(UwpViewVecIface *T, ULONG *c, IID **i) { (void)T; return vv_get_iids(c, i); }
static HRESULT UWP_CALL vv_GetName(UwpViewVecIface *T, UWP_HSTRING *n) {
    (void)T;
    return uwp_hstring_create_from_wstr(L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Core.CoreApplicationView>", n);
}
static HRESULT UWP_CALL vv_GetTrust(UwpViewVecIface *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL vv_GetAt(UwpViewVecIface *T, uint32_t index, void **item) {
    if (!item) return E_INVALIDARG;
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec);
    if (index != 0u || !o->view) { *item = NULL; return E_BOUNDS; }
    o->view->view.lpVtbl->AddRef(&o->view->view);
    *item = &o->view->view;
    return S_OK;
}
static HRESULT UWP_CALL vv_get_Size(UwpViewVecIface *T, uint32_t *size) {
    if (!size) return E_INVALIDARG;
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec);
    *size = o->view ? 1u : 0u;
    return S_OK;
}
static HRESULT UWP_CALL vv_IndexOf(UwpViewVecIface *T, void *val, uint32_t *index, uint8_t *found) {
    if (!index || !found) return E_INVALIDARG;
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec);
    *index = 0u;
    *found = (o->view && val == (void *)&o->view->view) ? 1u : 0u;
    return S_OK;
}
static HRESULT UWP_CALL vv_GetMany(UwpViewVecIface *T, uint32_t start, uint32_t cap, void **items, uint32_t *actual) {
    if (!actual) return E_INVALIDARG;
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, vec);
    if (start == 0u && cap > 0u && items && o->view) {
        o->view->view.lpVtbl->AddRef(&o->view->view);
        items[0] = &o->view->view;
        *actual = 1u;
    } else {
        *actual = 0u;
    }
    return S_OK;
}
static HRESULT UWP_CALL vit_QI(UwpViewIterableIface *T, REFIID r, void **p) {
    return vv_qi_impl(UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, iterable), r, p);
}
static ULONG UWP_CALL vit_AddRef(UwpViewIterableIface *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, iterable)->ref);
}
static ULONG UWP_CALL vit_Release(UwpViewIterableIface *T) {
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, iterable);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) vv_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL vit_GetIids(UwpViewIterableIface *T, ULONG *c, IID **i) { (void)T; return vv_get_iids(c, i); }
static HRESULT UWP_CALL vit_GetName(UwpViewIterableIface *T, UWP_HSTRING *n) {
    (void)T;
    return uwp_hstring_create_from_wstr(L"Windows.Foundation.Collections.IIterable`1<Windows.ApplicationModel.Core.CoreApplicationView>", n);
}
static HRESULT UWP_CALL vit_GetTrust(UwpViewIterableIface *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL vit_First(UwpViewIterableIface *T, void **iter) {
    if (!iter) return E_INVALIDARG;
    UwpCoreAppViewsObj *o = UWP_CONTAINING_RECORD(T, UwpCoreAppViewsObj, iterable);
    UwpViewIterObj *it = (UwpViewIterObj *)calloc(1, sizeof(*it));
    if (!it) { *iter = NULL; return E_OUTOFMEMORY; }
    it->iface.lpVtbl = &g_vi_vtbl;
    it->ref = 1;
    it->view = o->view;
    if (it->view) it->view->view.lpVtbl->AddRef(&it->view->view);
    it->index = 0u;
    *iter = &it->iface;
    return S_OK;
}
static const UwpViewVecVtbl g_vv_vtbl = {
    vv_QI, vv_AddRef, vv_Release, vv_GetIids, vv_GetName, vv_GetTrust,
    vv_GetAt, vv_get_Size, vv_IndexOf, vv_GetMany
};
static const UwpViewIterableVtbl g_vit_vtbl = {
    vit_QI, vit_AddRef, vit_Release, vit_GetIids, vit_GetName, vit_GetTrust,
    vit_First
};

/* ICoreImmersiveApplication */
static HRESULT UWP_CALL cai_QI(UwpICoreImmersiveApplication *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL cai_AddRef(UwpICoreImmersiveApplication *T) { (void)T; return 1u; }
static ULONG UWP_CALL cai_Release(UwpICoreImmersiveApplication *T) { (void)T; return 1u; }
static HRESULT UWP_CALL cai_GetIids(UwpICoreImmersiveApplication *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL cai_GetName(UwpICoreImmersiveApplication *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL cai_GetTrust(UwpICoreImmersiveApplication *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cai_get_Views(UwpICoreImmersiveApplication *T, void **val) {
    (void)T;
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *mv = uwp_coreapp_get_or_create_main_view();
    if (!mv) { *val = NULL; return E_OUTOFMEMORY; }
    UwpCoreAppViewsObj *vec = (UwpCoreAppViewsObj *)calloc(1, sizeof(*vec));
    if (!vec) {
        mv->view.lpVtbl->Release(&mv->view);
        *val = NULL;
        return E_OUTOFMEMORY;
    }
    vec->vec.lpVtbl = &g_vv_vtbl;
    vec->iterable.lpVtbl = &g_vit_vtbl;
    vec->ref = 1;
    vec->view = mv; /* takes ownership of mv ref */
    *val = &vec->vec;
    return S_OK;
}
static HRESULT UWP_CALL cai_CreateNewView(UwpICoreImmersiveApplication *T, UWP_HSTRING rt, UWP_HSTRING ep, void **view) {
    (void)T; (void)rt; (void)ep; if (view) *view = NULL;
    uwp_bridge_log("coreapp", "ICoreImmersiveApplication::CreateNewView (multi-view) not implemented");
    return E_NOTIMPL;
}
static HRESULT UWP_CALL cai_get_MainView(UwpICoreImmersiveApplication *T, void **val) {
    (void)T;
    if (!val) return E_INVALIDARG;
    UwpCoreAppViewObj *v = uwp_coreapp_get_or_create_main_view();
    *val = v ? &v->view : NULL;
    return *val ? S_OK : E_OUTOFMEMORY;
}

/* ICoreApplicationUnhandledError */
static HRESULT UWP_CALL cau_QI(UwpICoreApplicationUnhandledError *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL cau_AddRef(UwpICoreApplicationUnhandledError *T) { (void)T; return 1u; }
static ULONG UWP_CALL cau_Release(UwpICoreApplicationUnhandledError *T) { (void)T; return 1u; }
static HRESULT UWP_CALL cau_GetIids(UwpICoreApplicationUnhandledError *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL cau_GetName(UwpICoreApplicationUnhandledError *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL cau_GetTrust(UwpICoreApplicationUnhandledError *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cau_add(UwpICoreApplicationUnhandledError *T, UwpTypedEventHandler *h, UwpEventRegistrationToken *tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_add(&g_core_app.ev_unhandled_err, h, tok);
}
static HRESULT UWP_CALL cau_remove(UwpICoreApplicationUnhandledError *T, UwpEventRegistrationToken tok) {
    (void)T; uwp_coreapp_ensure_init();
    return uwp_event_list_remove(&g_core_app.ev_unhandled_err, tok);
}

/* ICoreApplicationUseCount */
static HRESULT UWP_CALL cauc_QI(UwpICoreApplicationUseCount *T, REFIID r, void **p) { (void)T; return coreapp_qi_impl(r, p); }
static ULONG UWP_CALL cauc_AddRef(UwpICoreApplicationUseCount *T) { (void)T; return 1u; }
static ULONG UWP_CALL cauc_Release(UwpICoreApplicationUseCount *T) { (void)T; return 1u; }
static HRESULT UWP_CALL cauc_GetIids(UwpICoreApplicationUseCount *T, ULONG *c, IID **i) {
    (void)T; return coreapp_get_iids_impl(c, i);
}
static HRESULT UWP_CALL cauc_GetName(UwpICoreApplicationUseCount *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", n);
}
static HRESULT UWP_CALL cauc_GetTrust(UwpICoreApplicationUseCount *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL cauc_Inc(UwpICoreApplicationUseCount *T) {
    (void)T; uwp_atomic_inc(&g_core_app.app_use_count); return S_OK;
}
static HRESULT UWP_CALL cauc_Dec(UwpICoreApplicationUseCount *T) {
    (void)T; uwp_atomic_dec(&g_core_app.app_use_count); return S_OK;
}

static const UwpIActivationFactoryVtbl g_caf_vtbl = {
    caf_QI, caf_AddRef, caf_Release, caf_GetIids, caf_GetName, caf_GetTrust,
    caf_ActivateInstance
};
static const UwpICoreApplicationVtbl g_ca_vtbl = {
    ca_QI, ca_AddRef, ca_Release, ca_GetIids, ca_GetName, ca_GetTrust,
    ca_get_Id, ca_add_Suspending, ca_remove_Suspending,
    ca_add_Resuming, ca_remove_Resuming,
    ca_get_Properties, ca_GetCurrentView, ca_Run, ca_RunWithActivationFactories
};
static const UwpICoreApplication2Vtbl g_ca2_vtbl = {
    ca2_QI, ca2_AddRef, ca2_Release, ca2_GetIids, ca2_GetName, ca2_GetTrust,
    ca2_add_BgAct, ca2_remove_BgAct,
    ca2_add_LeaveBg, ca2_remove_LeaveBg,
    ca2_add_EnterBg, ca2_remove_EnterBg,
    ca2_EnablePrelaunch
};
static const UwpICoreApplicationExitVtbl g_cae_vtbl = {
    cae_QI, cae_AddRef, cae_Release, cae_GetIids, cae_GetName, cae_GetTrust,
    cae_Exit, cae_add_Exiting, cae_remove_Exiting
};
static const UwpICoreImmersiveApplicationVtbl g_cai_vtbl = {
    cai_QI, cai_AddRef, cai_Release, cai_GetIids, cai_GetName, cai_GetTrust,
    cai_get_Views, cai_CreateNewView, cai_get_MainView
};
static const UwpICoreApplicationUnhandledErrorVtbl g_cau_vtbl = {
    cau_QI, cau_AddRef, cau_Release, cau_GetIids, cau_GetName, cau_GetTrust,
    cau_add, cau_remove
};
static const UwpICoreApplicationUseCountVtbl g_cauc_vtbl = {
    cauc_QI, cauc_AddRef, cauc_Release, cauc_GetIids, cauc_GetName, cauc_GetTrust,
    cauc_Inc, cauc_Dec
};

/* ============================================================================
 * Subsystem Init, Run Lifecycle & Event Triggers
 * ============================================================================ */

void uwp_coreapp_subsystem_init(void) {
    g_core_app.factory.lpVtbl       = &g_caf_vtbl;
    g_core_app.core_app.lpVtbl      = &g_ca_vtbl;
    g_core_app.core_app2.lpVtbl     = &g_ca2_vtbl;
    g_core_app.core_exit.lpVtbl     = &g_cae_vtbl;
    g_core_app.immersive.lpVtbl     = &g_cai_vtbl;
    g_core_app.unhandled_err.lpVtbl = &g_cau_vtbl;
    g_core_app.use_count.lpVtbl     = &g_cauc_vtbl;
    uwp_coreapp_ensure_init();
}

void uwp_coreapp_subsystem_shutdown(void) {
    if (!g_core_app.initialized) return;
    uwp_mutex_lock(&g_core_app.lock);
    UwpCoreAppViewObj *mv = g_core_app.main_view;
    g_core_app.main_view = NULL;
    UwpIMap *props = g_core_app.properties;
    g_core_app.properties = NULL;
    UwpIGetActivationFactory *cf = g_core_app.custom_factory;
    g_core_app.custom_factory = NULL;
    uwp_mutex_unlock(&g_core_app.lock);

    if (mv) mv->view.lpVtbl->Release(&mv->view);
    if (props) props->lpVtbl->Release(props);
    if (cf) cf->lpVtbl->Release(cf);

    uwp_event_list_destroy(&g_core_app.ev_suspending);
    uwp_event_list_destroy(&g_core_app.ev_resuming);
    uwp_event_list_destroy(&g_core_app.ev_bg_activated);
    uwp_event_list_destroy(&g_core_app.ev_leaving_bg);
    uwp_event_list_destroy(&g_core_app.ev_entered_bg);
    uwp_event_list_destroy(&g_core_app.ev_exiting);
    uwp_event_list_destroy(&g_core_app.ev_unhandled_err);
    uwp_mutex_destroy(&g_core_app.lock);
    g_core_app.initialized = 0;
}

HRESULT uwp_coreapp_get_activation_factory(REFIID iid, void **out_factory) {
    uwp_coreapp_subsystem_init();
    return coreapp_qi_impl(iid, out_factory);
}

void uwp_coreapp_notify_suspending(void) {
    uwp_coreapp_ensure_init();
    UwpISuspendingEventArgs *args = uwp_create_suspending_args();
    uwp_event_list_invoke(&g_core_app.ev_suspending, NULL, args);
    if (args) args->lpVtbl->Release(args);
}

void uwp_coreapp_notify_resuming(void) {
    uwp_coreapp_ensure_init();
    uwp_event_list_invoke(&g_core_app.ev_resuming, NULL, NULL);
}

void uwp_coreapp_notify_entered_background(void) {
    uwp_coreapp_ensure_init();
    UwpIBackgroundEventArgs *args = uwp_create_background_event_args(1);
    uwp_event_list_invoke(&g_core_app.ev_entered_bg, NULL, args);
    if (args) args->lpVtbl->Release(args);
}

void uwp_coreapp_notify_leaving_background(void) {
    uwp_coreapp_ensure_init();
    UwpIBackgroundEventArgs *args = uwp_create_background_event_args(0);
    uwp_event_list_invoke(&g_core_app.ev_leaving_bg, NULL, args);
    if (args) args->lpVtbl->Release(args);
}

void uwp_coreapp_trigger_view_activated(const wchar_t *arguments) {
    uwp_coreapp_ensure_init();
    UwpCoreAppViewObj *mv = uwp_coreapp_get_or_create_main_view();
    if (!mv) return;
    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    UwpIActivatedEventArgs *args =
        uwp_create_launch_activated_args(arguments ? arguments : L"",
                                         (id && id->app_id[0]) ? id->app_id : L"App");
    if (args) {
        uwp_event_list_invoke(&mv->ev_activated, &mv->view, args);
        args->lpVtbl->Release(args);
    }
    mv->view.lpVtbl->Release(&mv->view);
}

HRESULT uwp_coreapp_try_custom_factory(UWP_HSTRING class_id, REFIID iid, void **out_factory) {
    if (!g_core_app.initialized || !out_factory) return CLASS_E_CLASSNOTAVAILABLE;
    *out_factory = NULL;
    uwp_mutex_lock(&g_core_app.lock);
    UwpIGetActivationFactory *cf = g_core_app.custom_factory;
    if (cf) cf->lpVtbl->AddRef(cf);
    uwp_mutex_unlock(&g_core_app.lock);
    if (!cf) return CLASS_E_CLASSNOTAVAILABLE;

    UwpIInspectable *insp = NULL;
    HRESULT hr = cf->lpVtbl->GetActivationFactory(cf, class_id, &insp);
    cf->lpVtbl->Release(cf);
    if (SUCCEEDED(hr) && insp) {
        hr = insp->lpVtbl->QueryInterface(insp, iid, out_factory);
        insp->lpVtbl->Release(insp);
        return hr;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT uwp_coreapp_run_view_source(UwpIFrameworkViewSource *view_source) {
    if (!view_source) {
        return E_INVALIDARG;
    }
    uwp_coreapp_subsystem_init();
    g_core_app.exiting = 0;

    uwp_bridge_log("coreapp", "CoreApplication::Run(view_source=%p) starting UWP lifecycle",
                   (void *)view_source);

    UwpCoreAppViewObj *main_view = uwp_coreapp_get_or_create_main_view();
    if (!main_view) {
        return E_OUTOFMEMORY;
    }
    uwp_corewindow_bind_current_thread(main_view->core_window);

    UwpIFrameworkView *fview = NULL;
    HRESULT hr = view_source->lpVtbl->CreateView(view_source, &fview);
    if (FAILED(hr) || !fview) {
        uwp_bridge_log("coreapp", "IFrameworkViewSource::CreateView failed hr=0x%08X", (unsigned)hr);
        main_view->view.lpVtbl->Release(&main_view->view);
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = fview->lpVtbl->Initialize(fview, &main_view->view);
    if (FAILED(hr)) {
        uwp_bridge_log("coreapp", "IFrameworkView::Initialize failed hr=0x%08X", (unsigned)hr);
        fview->lpVtbl->Release(fview);
        main_view->view.lpVtbl->Release(&main_view->view);
        return hr;
    }
    uwp_bridge_log("coreapp", "IFrameworkView::Initialize succeeded");

    UwpICoreWindow *cw = uwp_corewindow_iface(main_view->core_window);
    hr = fview->lpVtbl->SetWindow(fview, cw);
    if (cw) cw->lpVtbl->Release(cw);
    if (FAILED(hr)) {
        uwp_bridge_log("coreapp", "IFrameworkView::SetWindow failed hr=0x%08X", (unsigned)hr);
        fview->lpVtbl->Uninitialize(fview);
        fview->lpVtbl->Release(fview);
        main_view->view.lpVtbl->Release(&main_view->view);
        return hr;
    }
    uwp_bridge_log("coreapp", "IFrameworkView::SetWindow succeeded");

    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    UWP_HSTRING ep_hs = NULL;
    uwp_hstring_create_from_wstr((id && id->entry_point[0]) ? id->entry_point : L"", &ep_hs);
    hr = fview->lpVtbl->Load(fview, ep_hs);
    if (ep_hs) WindowsDeleteString(ep_hs);
    if (FAILED(hr)) {
        uwp_bridge_log("coreapp", "IFrameworkView::Load failed hr=0x%08X", (unsigned)hr);
        fview->lpVtbl->Uninitialize(fview);
        fview->lpVtbl->Release(fview);
        main_view->view.lpVtbl->Release(&main_view->view);
        return hr;
    }
    uwp_bridge_log("coreapp", "IFrameworkView::Load succeeded");

    /* Fire CoreApplicationView::Activated(LaunchActivatedEventArgs) prior to Run() */
    uwp_bridge_log("coreapp", "Firing CoreApplicationView::Activated(Launch)");
    uwp_coreapp_trigger_view_activated(L"");

    /* Ensure CoreWindow::Activate() has fired CodeActivated (idempotent if OnActivated called it) */
    cw = uwp_corewindow_iface(main_view->core_window);
    if (cw) {
        cw->lpVtbl->Activate(cw);
        cw->lpVtbl->Release(cw);
    }

    uwp_bridge_log("coreapp", "Entering IFrameworkView::Run()");
    hr = fview->lpVtbl->Run(fview);
    uwp_bridge_log("coreapp", "IFrameworkView::Run returned hr=0x%08X", (unsigned)hr);

    uwp_coreapp_notify_suspending();
    if (!g_core_app.exiting) {
        g_core_app.exiting = 1;
        uwp_event_list_invoke(&g_core_app.ev_exiting, NULL, NULL);
    }

    fview->lpVtbl->Uninitialize(fview);
    fview->lpVtbl->Release(fview);
    main_view->view.lpVtbl->Release(&main_view->view);
    return hr;
}
