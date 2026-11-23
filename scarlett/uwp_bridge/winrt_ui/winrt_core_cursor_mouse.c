/*
 * Proton-Scarlett — CoreCursor & MouseDevice
 *
 * Clean-room implementations of:
 *   - Windows.UI.Core.CoreCursor (ICoreCursorFactory, ICoreCursor)
 *   - Windows.Devices.Input.MouseDevice (IMouseDeviceStatics, IMouseDevice, MouseEventArgs)
 */

#include "winrt_ui_internal.h"

/* ============================================================================
 * Windows.UI.Core.CoreCursor
 * ============================================================================ */

typedef struct core_cursor_obj_s {
    UwpICoreCursor    iface;
    volatile int32_t  ref_count;
    UwpCoreCursorType type;
    UINT32            id;
} core_cursor_obj_t;

typedef struct core_cursor_factory_s {
    UwpICoreCursorFactory factory_iface;
    IActivationFactory    act_iface;
    volatile int32_t      ref_count;
} core_cursor_factory_t;

static HRESULT STDMETHODCALLTYPE cc_QueryInterface(UwpICoreCursor *This, REFIID riid, void **ppv) {
    core_cursor_obj_t *cur = CONTAINING_RECORD(This, core_cursor_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_ICoreCursor)) {
        *ppv = &cur->iface;
        uwp_ui_atomic_inc(&cur->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cc_AddRef(UwpICoreCursor *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, core_cursor_obj_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE cc_Release(UwpICoreCursor *This) {
    core_cursor_obj_t *cur = CONTAINING_RECORD(This, core_cursor_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&cur->ref_count);
    if (ref == 0) uwp_ui_free(cur);
    return ref;
}
static HRESULT STDMETHODCALLTYPE cc_GetIids(UwpICoreCursor *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_ICoreCursor };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE cc_GetRuntimeClassName(UwpICoreCursor *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.CoreCursor", name);
}
static HRESULT STDMETHODCALLTYPE cc_GetTrustLevel(UwpICoreCursor *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cc_get_Id(UwpICoreCursor *This, UINT32 *value) {
    core_cursor_obj_t *cur = CONTAINING_RECORD(This, core_cursor_obj_t, iface);
    if (!value) return E_POINTER;
    *value = cur->id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cc_get_Type(UwpICoreCursor *This, UwpCoreCursorType *value) {
    core_cursor_obj_t *cur = CONTAINING_RECORD(This, core_cursor_obj_t, iface);
    if (!value) return E_POINTER;
    *value = cur->type;
    return S_OK;
}

static const UwpICoreCursorVtbl g_cc_vtbl = {
    cc_QueryInterface, cc_AddRef, cc_Release,
    cc_GetIids, cc_GetRuntimeClassName, cc_GetTrustLevel,
    cc_get_Id, cc_get_Type
};

static HRESULT ccf_qi(core_cursor_factory_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_ICoreCursorFactory)) {
        *ppv = &fac->factory_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ccf_QueryInterface(UwpICoreCursorFactory *This, REFIID riid, void **ppv) {
    return ccf_qi(CONTAINING_RECORD(This, core_cursor_factory_t, factory_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE ccf_AddRef(UwpICoreCursorFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, core_cursor_factory_t, factory_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE ccf_Release(UwpICoreCursorFactory *This) {
    core_cursor_factory_t *fac = CONTAINING_RECORD(This, core_cursor_factory_t, factory_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE ccf_GetIids(UwpICoreCursorFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_ICoreCursorFactory, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE ccf_GetRuntimeClassName(UwpICoreCursorFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.CoreCursor", name);
}
static HRESULT STDMETHODCALLTYPE ccf_GetTrustLevel(UwpICoreCursorFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE ccf_CreateCursor(UwpICoreCursorFactory *This, UwpCoreCursorType type, UINT32 id, UwpICoreCursor **cursor) {
    core_cursor_obj_t *cur;
    (void)This;
    if (!cursor) return E_POINTER;
    *cursor = NULL;
    if ((int)type < (int)UwpCoreCursorType_Arrow || (int)type > (int)UwpCoreCursorType_Person) {
        return E_INVALIDARG;
    }
    cur = (core_cursor_obj_t *)uwp_ui_alloc(sizeof(core_cursor_obj_t));
    if (!cur) return E_OUTOFMEMORY;
    cur->iface.lpVtbl = &g_cc_vtbl;
    cur->ref_count = 1;
    cur->type = type;
    cur->id = id;
    *cursor = &cur->iface;
    return S_OK;
}

static const UwpICoreCursorFactoryVtbl g_ccf_vtbl = {
    ccf_QueryInterface, ccf_AddRef, ccf_Release,
    ccf_GetIids, ccf_GetRuntimeClassName, ccf_GetTrustLevel,
    ccf_CreateCursor
};

static HRESULT STDMETHODCALLTYPE cca_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return ccf_qi(CONTAINING_RECORD(This, core_cursor_factory_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE cca_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, core_cursor_factory_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE cca_Release(IActivationFactory *This) {
    core_cursor_factory_t *fac = CONTAINING_RECORD(This, core_cursor_factory_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE cca_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_ICoreCursorFactory, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE cca_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.UI.Core.CoreCursor", name);
}
static HRESULT STDMETHODCALLTYPE cca_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cca_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("CoreCursorFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_cca_vtbl = {
    cca_QueryInterface, cca_AddRef, cca_Release,
    cca_GetIids, cca_GetRuntimeClassName, cca_GetTrustLevel,
    cca_ActivateInstance
};

HRESULT uwp_ui_core_cursor_get_factory(REFIID iid, void **out_factory) {
    core_cursor_factory_t *fac = (core_cursor_factory_t *)uwp_ui_alloc(sizeof(core_cursor_factory_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->factory_iface.lpVtbl = &g_ccf_vtbl;
    fac->act_iface.lpVtbl = &g_cca_vtbl;
    fac->ref_count = 1;
    HRESULT hr = ccf_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}

const wchar_t *uwp_winrt_ui_cursor_type_to_win32_resource(UwpCoreCursorType type) {
    switch (type) {
    case UwpCoreCursorType_Arrow:                  return (const wchar_t *)(uintptr_t)32512; /* IDC_ARROW */
    case UwpCoreCursorType_IBeam:                  return (const wchar_t *)(uintptr_t)32513; /* IDC_IBEAM */
    case UwpCoreCursorType_Wait:                   return (const wchar_t *)(uintptr_t)32514; /* IDC_WAIT */
    case UwpCoreCursorType_Cross:                  return (const wchar_t *)(uintptr_t)32515; /* IDC_CROSS */
    case UwpCoreCursorType_UpArrow:                return (const wchar_t *)(uintptr_t)32516; /* IDC_UPARROW */
    case UwpCoreCursorType_SizeNorthwestSoutheast: return (const wchar_t *)(uintptr_t)32642; /* IDC_SIZENWSE */
    case UwpCoreCursorType_SizeNortheastSouthwest: return (const wchar_t *)(uintptr_t)32643; /* IDC_SIZENESW */
    case UwpCoreCursorType_SizeWestEast:           return (const wchar_t *)(uintptr_t)32644; /* IDC_SIZEWE */
    case UwpCoreCursorType_SizeNorthSouth:         return (const wchar_t *)(uintptr_t)32645; /* IDC_SIZENS */
    case UwpCoreCursorType_SizeAll:                return (const wchar_t *)(uintptr_t)32646; /* IDC_SIZEALL */
    case UwpCoreCursorType_UniversalNo:            return (const wchar_t *)(uintptr_t)32648; /* IDC_NO */
    case UwpCoreCursorType_Hand:                   return (const wchar_t *)(uintptr_t)32649; /* IDC_HAND */
    case UwpCoreCursorType_Help:                   return (const wchar_t *)(uintptr_t)32651; /* IDC_HELP */
    case UwpCoreCursorType_Pin:
    case UwpCoreCursorType_Person:                 return (const wchar_t *)(uintptr_t)32649; /* IDC_HAND */
    case UwpCoreCursorType_Custom:
    default:                                       return NULL;
    }
}

/* ============================================================================
 * Windows.Devices.Input.MouseDevice
 * ============================================================================ */

typedef struct mouse_event_args_s {
    UwpIMouseEventArgs iface;
    volatile int32_t   ref_count;
    UwpMouseDelta      delta;
} mouse_event_args_t;

typedef struct mouse_device_obj_s {
    UwpIMouseDevice  iface;
    volatile int32_t ref_count;
} mouse_device_obj_t;

typedef struct mouse_device_statics_s {
    UwpIMouseDeviceStatics statics_iface;
    IActivationFactory     act_iface;
    volatile int32_t       ref_count;
} mouse_device_statics_t;

static uwp_ui_lock_t        g_md_lock = UWP_UI_LOCK_INIT;
static mouse_device_obj_t  *g_md_singleton = NULL;
static uwp_ui_event_list_t  g_mouse_moved_events = UWP_UI_EVENT_LIST_INIT;

static HRESULT STDMETHODCALLTYPE mea_QueryInterface(UwpIMouseEventArgs *This, REFIID riid, void **ppv) {
    mouse_event_args_t *args = CONTAINING_RECORD(This, mouse_event_args_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IMouseEventArgs)) {
        *ppv = &args->iface;
        uwp_ui_atomic_inc(&args->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE mea_AddRef(UwpIMouseEventArgs *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, mouse_event_args_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE mea_Release(UwpIMouseEventArgs *This) {
    mouse_event_args_t *args = CONTAINING_RECORD(This, mouse_event_args_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&args->ref_count);
    if (ref == 0) uwp_ui_free(args);
    return ref;
}
static HRESULT STDMETHODCALLTYPE mea_GetIids(UwpIMouseEventArgs *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IMouseEventArgs };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE mea_GetRuntimeClassName(UwpIMouseEventArgs *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Devices.Input.MouseEventArgs", name);
}
static HRESULT STDMETHODCALLTYPE mea_GetTrustLevel(UwpIMouseEventArgs *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mea_get_MouseDelta(UwpIMouseEventArgs *This, UwpMouseDelta *value) {
    mouse_event_args_t *args = CONTAINING_RECORD(This, mouse_event_args_t, iface);
    if (!value) return E_POINTER;
    *value = args->delta;
    return S_OK;
}

static const UwpIMouseEventArgsVtbl g_mea_vtbl = {
    mea_QueryInterface, mea_AddRef, mea_Release,
    mea_GetIids, mea_GetRuntimeClassName, mea_GetTrustLevel,
    mea_get_MouseDelta
};

static HRESULT STDMETHODCALLTYPE md_QueryInterface(UwpIMouseDevice *This, REFIID riid, void **ppv) {
    mouse_device_obj_t *md = CONTAINING_RECORD(This, mouse_device_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IMouseDevice)) {
        *ppv = &md->iface;
        uwp_ui_atomic_inc(&md->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE md_AddRef(UwpIMouseDevice *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, mouse_device_obj_t, iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE md_Release(UwpIMouseDevice *This) {
    mouse_device_obj_t *md = CONTAINING_RECORD(This, mouse_device_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&md->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_md_lock);
        if (uwp_ui_atomic_load(&md->ref_count) == 0) {
            if (g_md_singleton == md) g_md_singleton = NULL;
            uwp_ui_lock_release(&g_md_lock);
            uwp_ui_free(md);
            return 0;
        }
        uwp_ui_lock_release(&g_md_lock);
    }
    return ref;
}
static HRESULT STDMETHODCALLTYPE md_GetIids(UwpIMouseDevice *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IMouseDevice };
    return uwp_ui_get_iids(list, 1, c, iids);
}
static HRESULT STDMETHODCALLTYPE md_GetRuntimeClassName(UwpIMouseDevice *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Devices.Input.MouseDevice", name);
}
static HRESULT STDMETHODCALLTYPE md_GetTrustLevel(UwpIMouseDevice *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE md_add_MouseMoved(UwpIMouseDevice *This, UwpEventHandler *handler, EventRegistrationToken *token) {
    (void)This;
    return uwp_ui_event_add(&g_mouse_moved_events, handler, token);
}
static HRESULT STDMETHODCALLTYPE md_remove_MouseMoved(UwpIMouseDevice *This, EventRegistrationToken token) {
    (void)This;
    return uwp_ui_event_remove(&g_mouse_moved_events, token);
}

static const UwpIMouseDeviceVtbl g_md_vtbl = {
    md_QueryInterface, md_AddRef, md_Release,
    md_GetIids, md_GetRuntimeClassName, md_GetTrustLevel,
    md_add_MouseMoved, md_remove_MouseMoved
};

static mouse_device_obj_t *md_get_or_create_instance(void) {
    mouse_device_obj_t *obj;
    uwp_ui_lock_acquire(&g_md_lock);
    if (!g_md_singleton) {
        obj = (mouse_device_obj_t *)uwp_ui_alloc(sizeof(mouse_device_obj_t));
        if (obj) {
            obj->iface.lpVtbl = &g_md_vtbl;
            obj->ref_count = 1;
            g_md_singleton = obj;
        }
    } else {
        obj = g_md_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_md_lock);
    return obj;
}

static HRESULT md_statics_qi(mouse_device_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IMouseDeviceStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mds_QueryInterface(UwpIMouseDeviceStatics *This, REFIID riid, void **ppv) {
    return md_statics_qi(CONTAINING_RECORD(This, mouse_device_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE mds_AddRef(UwpIMouseDeviceStatics *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, mouse_device_statics_t, statics_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE mds_Release(UwpIMouseDeviceStatics *This) {
    mouse_device_statics_t *fac = CONTAINING_RECORD(This, mouse_device_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE mds_GetIids(UwpIMouseDeviceStatics *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IMouseDeviceStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE mds_GetRuntimeClassName(UwpIMouseDeviceStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Devices.Input.MouseDevice", name);
}
static HRESULT STDMETHODCALLTYPE mds_GetTrustLevel(UwpIMouseDeviceStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mds_GetForCurrentView(UwpIMouseDeviceStatics *This, UwpIMouseDevice **mouseDevice) {
    mouse_device_obj_t *obj;
    (void)This;
    if (!mouseDevice) return E_POINTER;
    obj = md_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    *mouseDevice = &obj->iface;
    return S_OK;
}

static const UwpIMouseDeviceStaticsVtbl g_mds_vtbl = {
    mds_QueryInterface, mds_AddRef, mds_Release,
    mds_GetIids, mds_GetRuntimeClassName, mds_GetTrustLevel,
    mds_GetForCurrentView
};

static HRESULT STDMETHODCALLTYPE mda_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return md_statics_qi(CONTAINING_RECORD(This, mouse_device_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE mda_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, mouse_device_statics_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE mda_Release(IActivationFactory *This) {
    mouse_device_statics_t *fac = CONTAINING_RECORD(This, mouse_device_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE mda_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IMouseDeviceStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE mda_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Devices.Input.MouseDevice", name);
}
static HRESULT STDMETHODCALLTYPE mda_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mda_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("MouseDeviceFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_mda_vtbl = {
    mda_QueryInterface, mda_AddRef, mda_Release,
    mda_GetIids, mda_GetRuntimeClassName, mda_GetTrustLevel,
    mda_ActivateInstance
};

HRESULT uwp_ui_mouse_device_get_factory(REFIID iid, void **out_factory) {
    mouse_device_statics_t *fac = (mouse_device_statics_t *)uwp_ui_alloc(sizeof(mouse_device_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_mds_vtbl;
    fac->act_iface.lpVtbl = &g_mda_vtbl;
    fac->ref_count = 1;
    HRESULT hr = md_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}

HRESULT uwp_winrt_ui_notify_mouse_moved(int32_t dx, int32_t dy) {
    mouse_device_obj_t *md = md_get_or_create_instance();
    mouse_event_args_t *args;
    if (!md) return E_OUTOFMEMORY;

    args = (mouse_event_args_t *)uwp_ui_alloc(sizeof(mouse_event_args_t));
    if (!args) {
        md_Release(&md->iface);
        return E_OUTOFMEMORY;
    }
    args->iface.lpVtbl = &g_mea_vtbl;
    args->ref_count = 1;
    args->delta.X = dx;
    args->delta.Y = dy;

    uwp_ui_event_invoke(&g_mouse_moved_events, &md->iface, &args->iface);

    mea_Release(&args->iface);
    md_Release(&md->iface);
    return S_OK;
}

/* ============================================================================
 * Windows.UI.Input.PointerVisualizationSettings
 * ============================================================================ */

typedef struct pointer_vis_obj_s {
    UwpIPointerVisualizationSettings iface;
    volatile int32_t                 ref_count;
    boolean                          is_contact_feedback_enabled;
    boolean                          is_barrel_button_feedback_enabled;
} pointer_vis_obj_t;

typedef struct pointer_vis_statics_s {
    UwpIPointerVisualizationSettingsStatics statics_iface;
    IActivationFactory                      act_iface;
    volatile int32_t                        ref_count;
} pointer_vis_statics_t;

static pointer_vis_obj_t *g_pvs_singleton = NULL;
static uwp_ui_lock_t      g_pvs_lock = UWP_UI_LOCK_INIT;

static HRESULT STDMETHODCALLTYPE pvs_QueryInterface(UwpIPointerVisualizationSettings *This, REFIID riid, void **ppv) {
    pointer_vis_obj_t *cur = CONTAINING_RECORD(This, pointer_vis_obj_t, iface);
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IPointerVisualizationSettings) ||
        IsEqualGUID(riid, &SCARLETT_IID_IPointerVisualizationSettings_Win81)) {
        *ppv = &cur->iface;
        uwp_ui_atomic_inc(&cur->ref_count);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE pvs_AddRef(UwpIPointerVisualizationSettings *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, pointer_vis_obj_t, iface)->ref_count);
}

static ULONG STDMETHODCALLTYPE pvs_Release(UwpIPointerVisualizationSettings *This) {
    pointer_vis_obj_t *cur = CONTAINING_RECORD(This, pointer_vis_obj_t, iface);
    ULONG ref = uwp_ui_atomic_dec(&cur->ref_count);
    if (ref == 0) {
        uwp_ui_lock_acquire(&g_pvs_lock);
        if (g_pvs_singleton == cur && uwp_ui_atomic_load(&cur->ref_count) == 0) {
            g_pvs_singleton = NULL;
        }
        uwp_ui_lock_release(&g_pvs_lock);
        uwp_ui_free(cur);
    }
    return ref;
}

static HRESULT STDMETHODCALLTYPE pvs_GetIids(UwpIPointerVisualizationSettings *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = {
        SCARLETT_IID_IPointerVisualizationSettings,
        SCARLETT_IID_IPointerVisualizationSettings_Win81
    };
    return uwp_ui_get_iids(list, 2, c, iids);
}

static HRESULT STDMETHODCALLTYPE pvs_GetRuntimeClassName(UwpIPointerVisualizationSettings *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.Input.PointerVisualizationSettings", name);
}

static HRESULT STDMETHODCALLTYPE pvs_GetTrustLevel(UwpIPointerVisualizationSettings *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pvs_put_IsContactFeedbackEnabled(UwpIPointerVisualizationSettings *This, boolean value) {
    pointer_vis_obj_t *cur = CONTAINING_RECORD(This, pointer_vis_obj_t, iface);
    cur->is_contact_feedback_enabled = value ? 1 : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pvs_get_IsContactFeedbackEnabled(UwpIPointerVisualizationSettings *This, boolean *value) {
    pointer_vis_obj_t *cur = CONTAINING_RECORD(This, pointer_vis_obj_t, iface);
    if (!value) return E_POINTER;
    *value = cur->is_contact_feedback_enabled;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pvs_put_IsBarrelButtonFeedbackEnabled(UwpIPointerVisualizationSettings *This, boolean value) {
    pointer_vis_obj_t *cur = CONTAINING_RECORD(This, pointer_vis_obj_t, iface);
    cur->is_barrel_button_feedback_enabled = value ? 1 : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pvs_get_IsBarrelButtonFeedbackEnabled(UwpIPointerVisualizationSettings *This, boolean *value) {
    pointer_vis_obj_t *cur = CONTAINING_RECORD(This, pointer_vis_obj_t, iface);
    if (!value) return E_POINTER;
    *value = cur->is_barrel_button_feedback_enabled;
    return S_OK;
}

static const UwpIPointerVisualizationSettingsVtbl g_pvs_vtbl = {
    pvs_QueryInterface, pvs_AddRef, pvs_Release,
    pvs_GetIids, pvs_GetRuntimeClassName, pvs_GetTrustLevel,
    pvs_put_IsContactFeedbackEnabled,
    pvs_get_IsContactFeedbackEnabled,
    pvs_put_IsBarrelButtonFeedbackEnabled,
    pvs_get_IsBarrelButtonFeedbackEnabled
};

static pointer_vis_obj_t *pvs_get_or_create_instance(void) {
    pointer_vis_obj_t *obj;
    uwp_ui_lock_acquire(&g_pvs_lock);
    if (!g_pvs_singleton) {
        obj = (pointer_vis_obj_t *)uwp_ui_alloc(sizeof(pointer_vis_obj_t));
        if (obj) {
            obj->iface.lpVtbl = &g_pvs_vtbl;
            obj->ref_count = 1;
            obj->is_contact_feedback_enabled = TRUE;
            obj->is_barrel_button_feedback_enabled = TRUE;
            g_pvs_singleton = obj;
        }
    } else {
        obj = g_pvs_singleton;
        uwp_ui_atomic_inc(&obj->ref_count);
    }
    uwp_ui_lock_release(&g_pvs_lock);
    return obj;
}

static HRESULT pvs_statics_qi(pointer_vis_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IPointerVisualizationSettingsStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT pvs_statics_get_iids(ULONG *c, IID **iids) {
    const GUID list[] = { SCARLETT_IID_IPointerVisualizationSettingsStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}

static HRESULT STDMETHODCALLTYPE pvss_QueryInterface(UwpIPointerVisualizationSettingsStatics *This, REFIID riid, void **ppv) {
    return pvs_statics_qi(CONTAINING_RECORD(This, pointer_vis_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE pvss_AddRef(UwpIPointerVisualizationSettingsStatics *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, pointer_vis_statics_t, statics_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE pvss_Release(UwpIPointerVisualizationSettingsStatics *This) {
    pointer_vis_statics_t *fac = CONTAINING_RECORD(This, pointer_vis_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE pvss_GetIids(UwpIPointerVisualizationSettingsStatics *This, ULONG *c, IID **iids) {
    (void)This;
    return pvs_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE pvss_GetRuntimeClassName(UwpIPointerVisualizationSettingsStatics *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.Input.PointerVisualizationSettings", name);
}
static HRESULT STDMETHODCALLTYPE pvss_GetTrustLevel(UwpIPointerVisualizationSettingsStatics *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pvss_GetForCurrentView(UwpIPointerVisualizationSettingsStatics *This, UwpIPointerVisualizationSettings **current) {
    pointer_vis_obj_t *obj;
    (void)This;
    if (!current) return E_POINTER;
    obj = pvs_get_or_create_instance();
    if (!obj) return E_OUTOFMEMORY;
    *current = &obj->iface;
    return S_OK;
}

static const UwpIPointerVisualizationSettingsStaticsVtbl g_pvss_vtbl = {
    pvss_QueryInterface, pvss_AddRef, pvss_Release,
    pvss_GetIids, pvss_GetRuntimeClassName, pvss_GetTrustLevel,
    pvss_GetForCurrentView
};

static HRESULT STDMETHODCALLTYPE pvsa_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return pvs_statics_qi(CONTAINING_RECORD(This, pointer_vis_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE pvsa_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, pointer_vis_statics_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE pvsa_Release(IActivationFactory *This) {
    pointer_vis_statics_t *fac = CONTAINING_RECORD(This, pointer_vis_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE pvsa_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    return pvs_statics_get_iids(c, iids);
}
static HRESULT STDMETHODCALLTYPE pvsa_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This;
    return uwp_ui_get_runtime_class_name(L"Windows.UI.Input.PointerVisualizationSettings", name);
}
static HRESULT STDMETHODCALLTYPE pvsa_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pvsa_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("PointerVisualizationSettingsFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_pvsa_vtbl = {
    pvsa_QueryInterface, pvsa_AddRef, pvsa_Release,
    pvsa_GetIids, pvsa_GetRuntimeClassName, pvsa_GetTrustLevel,
    pvsa_ActivateInstance
};

HRESULT uwp_ui_pointer_vis_get_factory(REFIID iid, void **out_factory) {
    pointer_vis_statics_t *fac = (pointer_vis_statics_t *)uwp_ui_alloc(sizeof(pointer_vis_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_pvss_vtbl;
    fac->act_iface.lpVtbl = &g_pvsa_vtbl;
    fac->ref_count = 1;
    HRESULT hr = pvs_statics_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}
