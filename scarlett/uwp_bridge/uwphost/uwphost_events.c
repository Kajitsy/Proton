/*
 * Proton-Scarlett — UWPHOST.dll WinRT Event Lists, EventArgs, IAsyncAction & PropertySet
 *
 * Clean-room COM/WinRT C11 implementations of all CoreApplication,
 * CoreWindow, and CoreDispatcher event argument objects, async action,
 * pointer input objects, and property set.
 */

#include "uwphost_internal.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Thread-Safe WinRT Event Listener List
 * ============================================================================ */

void uwp_event_list_init(uwp_event_list_t *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
    uwp_mutex_init(&list->lock);
    list->next_token = 1;
}

void uwp_event_list_destroy(uwp_event_list_t *list) {
    if (!list) return;
    uwp_mutex_lock(&list->lock);
    for (size_t i = 0; i < UWP_MAX_EVENT_LISTENERS; ++i) {
        if (list->entries[i].handler) {
            list->entries[i].handler->lpVtbl->Release(list->entries[i].handler);
            list->entries[i].handler = NULL;
            list->entries[i].token = 0;
        }
    }
    uwp_mutex_unlock(&list->lock);
    uwp_mutex_destroy(&list->lock);
}

HRESULT uwp_event_list_add(uwp_event_list_t *list, UwpTypedEventHandler *handler,
                           UwpEventRegistrationToken *out_token) {
    if (!list || !handler || !out_token) {
        return E_INVALIDARG;
    }
    out_token->value = 0;
    uwp_mutex_lock(&list->lock);
    for (size_t i = 0; i < UWP_MAX_EVENT_LISTENERS; ++i) {
        if (list->entries[i].handler == NULL) {
            int64_t tok = list->next_token++;
            if (tok == 0) tok = list->next_token++;
            handler->lpVtbl->AddRef(handler);
            list->entries[i].token = tok;
            list->entries[i].handler = handler;
            out_token->value = tok;
            uwp_mutex_unlock(&list->lock);
            return S_OK;
        }
    }
    uwp_mutex_unlock(&list->lock);
    return E_OUTOFMEMORY;
}

HRESULT uwp_event_list_remove(uwp_event_list_t *list, UwpEventRegistrationToken token) {
    if (!list) {
        return E_INVALIDARG;
    }
    if (token.value == 0) {
        return S_OK;
    }
    UwpTypedEventHandler *to_release = NULL;
    uwp_mutex_lock(&list->lock);
    for (size_t i = 0; i < UWP_MAX_EVENT_LISTENERS; ++i) {
        if (list->entries[i].handler != NULL && list->entries[i].token == token.value) {
            to_release = list->entries[i].handler;
            list->entries[i].handler = NULL;
            list->entries[i].token = 0;
            break;
        }
    }
    uwp_mutex_unlock(&list->lock);
    if (to_release) {
        to_release->lpVtbl->Release(to_release);
    }
    return S_OK;
}

void uwp_event_list_invoke(uwp_event_list_t *list, void *sender, void *args) {
    if (!list) return;
    UwpTypedEventHandler *snapshot[UWP_MAX_EVENT_LISTENERS];
    size_t count = 0;

    uwp_mutex_lock(&list->lock);
    for (size_t i = 0; i < UWP_MAX_EVENT_LISTENERS; ++i) {
        if (list->entries[i].handler != NULL) {
            list->entries[i].handler->lpVtbl->AddRef(list->entries[i].handler);
            snapshot[count++] = list->entries[i].handler;
        }
    }
    uwp_mutex_unlock(&list->lock);

    for (size_t i = 0; i < count; ++i) {
        snapshot[i]->lpVtbl->Invoke(snapshot[i], sender, args);
        snapshot[i]->lpVtbl->Release(snapshot[i]);
    }
}

size_t uwp_event_list_count(uwp_event_list_t *list) {
    if (!list) return 0;
    size_t count = 0;
    uwp_mutex_lock(&list->lock);
    for (size_t i = 0; i < UWP_MAX_EVENT_LISTENERS; ++i) {
        if (list->entries[i].handler != NULL) ++count;
    }
    uwp_mutex_unlock(&list->lock);
    return count;
}

/* ============================================================================
 * Shared IInspectable Helpers
 * ============================================================================ */

static HRESULT uwp_std_get_iids(ULONG *count, IID **iids) {
    if (!count || !iids) return E_INVALIDARG;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT uwp_std_get_trust_level(UwpTrustLevel *trust) {
    if (!trust) return E_INVALIDARG;
    *trust = UWP_TRUST_BASE;
    return S_OK;
}

/* ============================================================================
 * 1. CoreWindowEventArgs (ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpCoreWindowEventArgsObj {
    UwpICoreWindowEventArgs iface;
    volatile int32_t ref;
    uint8_t handled;
} UwpCoreWindowEventArgsObj;

static HRESULT UWP_CALL cwea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        This->lpVtbl->AddRef(This);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG UWP_CALL cwea_AddRef(UwpICoreWindowEventArgs *This) {
    UwpCoreWindowEventArgsObj *o = UWP_CONTAINING_RECORD(This, UwpCoreWindowEventArgsObj, iface);
    return (ULONG)uwp_atomic_inc(&o->ref);
}
static ULONG UWP_CALL cwea_Release(UwpICoreWindowEventArgs *This) {
    UwpCoreWindowEventArgsObj *o = UWP_CONTAINING_RECORD(This, UwpCoreWindowEventArgsObj, iface);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL cwea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL cwea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindowEventArgs", n);
}
static HRESULT UWP_CALL cwea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL cwea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    UwpCoreWindowEventArgsObj *o = UWP_CONTAINING_RECORD(This, UwpCoreWindowEventArgsObj, iface);
    *v = o->handled;
    return S_OK;
}
static HRESULT UWP_CALL cwea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UwpCoreWindowEventArgsObj *o = UWP_CONTAINING_RECORD(This, UwpCoreWindowEventArgsObj, iface);
    o->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpICoreWindowEventArgsVtbl g_cwea_vtbl = {
    cwea_QueryInterface, cwea_AddRef, cwea_Release,
    cwea_GetIids, cwea_GetRuntimeClassName, cwea_GetTrustLevel,
    cwea_get_Handled, cwea_put_Handled
};

UwpICoreWindowEventArgs *uwp_create_core_window_event_args(void) {
    UwpCoreWindowEventArgsObj *o = (UwpCoreWindowEventArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->iface.lpVtbl = &g_cwea_vtbl;
    o->ref = 1;
    return &o->iface;
}

/* ============================================================================
 * 2. WindowSizeChangedEventArgs (IWindowSizeChangedEventArgs + ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpSizeChangedArgsObj {
    UwpIWindowSizeChangedEventArgs primary;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    UwpSize size;
    uint8_t handled;
} UwpSizeChangedArgsObj;

static HRESULT UWP_CALL sc_qi_impl(UwpSizeChangedArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IWindowSizeChangedEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL sc_QueryInterface(UwpIWindowSizeChangedEventArgs *This, REFIID riid, void **ppv) {
    return sc_qi_impl(UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL sc_AddRef(UwpIWindowSizeChangedEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, primary)->ref);
}
static ULONG UWP_CALL sc_Release(UwpIWindowSizeChangedEventArgs *This) {
    UwpSizeChangedArgsObj *o = UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL sc_GetIids(UwpIWindowSizeChangedEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL sc_GetRuntimeClassName(UwpIWindowSizeChangedEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.WindowSizeChangedEventArgs", n);
}
static HRESULT UWP_CALL sc_GetTrustLevel(UwpIWindowSizeChangedEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL sc_get_Size(UwpIWindowSizeChangedEventArgs *This, UwpSize *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, primary)->size;
    return S_OK;
}
static HRESULT UWP_CALL sc_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return sc_qi_impl(UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL sc_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, event_args)->ref);
}
static ULONG UWP_CALL sc_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpSizeChangedArgsObj *o = UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL sc_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL sc_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.WindowSizeChangedEventArgs", n);
}
static HRESULT UWP_CALL sc_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL sc_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL sc_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpSizeChangedArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpIWindowSizeChangedEventArgsVtbl g_sc_vtbl = {
    sc_QueryInterface, sc_AddRef, sc_Release,
    sc_GetIids, sc_GetRuntimeClassName, sc_GetTrustLevel,
    sc_get_Size
};
static const UwpICoreWindowEventArgsVtbl g_sc_ea_vtbl = {
    sc_ea_QueryInterface, sc_ea_AddRef, sc_ea_Release,
    sc_ea_GetIids, sc_ea_GetRuntimeClassName, sc_ea_GetTrustLevel,
    sc_ea_get_Handled, sc_ea_put_Handled
};

UwpIWindowSizeChangedEventArgs *uwp_create_size_changed_args(float width, float height) {
    UwpSizeChangedArgsObj *o = (UwpSizeChangedArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->primary.lpVtbl = &g_sc_vtbl;
    o->event_args.lpVtbl = &g_sc_ea_vtbl;
    o->ref = 1;
    o->size.Width = width;
    o->size.Height = height;
    return &o->primary;
}

/* ============================================================================
 * 3. WindowActivatedEventArgs (IWindowActivatedEventArgs + ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpActivatedArgsObj {
    UwpIWindowActivatedEventArgs primary;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    UwpCoreWindowActivationState state;
    uint8_t handled;
} UwpActivatedArgsObj;

static HRESULT UWP_CALL wa_qi_impl(UwpActivatedArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IWindowActivatedEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL wa_QueryInterface(UwpIWindowActivatedEventArgs *This, REFIID riid, void **ppv) {
    return wa_qi_impl(UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL wa_AddRef(UwpIWindowActivatedEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, primary)->ref);
}
static ULONG UWP_CALL wa_Release(UwpIWindowActivatedEventArgs *This) {
    UwpActivatedArgsObj *o = UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL wa_GetIids(UwpIWindowActivatedEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL wa_GetRuntimeClassName(UwpIWindowActivatedEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.WindowActivatedEventArgs", n);
}
static HRESULT UWP_CALL wa_GetTrustLevel(UwpIWindowActivatedEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL wa_get_State(UwpIWindowActivatedEventArgs *This, UwpCoreWindowActivationState *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, primary)->state;
    return S_OK;
}
static HRESULT UWP_CALL wa_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return wa_qi_impl(UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL wa_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, event_args)->ref);
}
static ULONG UWP_CALL wa_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpActivatedArgsObj *o = UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL wa_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL wa_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.WindowActivatedEventArgs", n);
}
static HRESULT UWP_CALL wa_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL wa_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL wa_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpActivatedArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpIWindowActivatedEventArgsVtbl g_wa_vtbl = {
    wa_QueryInterface, wa_AddRef, wa_Release,
    wa_GetIids, wa_GetRuntimeClassName, wa_GetTrustLevel,
    wa_get_State
};
static const UwpICoreWindowEventArgsVtbl g_wa_ea_vtbl = {
    wa_ea_QueryInterface, wa_ea_AddRef, wa_ea_Release,
    wa_ea_GetIids, wa_ea_GetRuntimeClassName, wa_ea_GetTrustLevel,
    wa_ea_get_Handled, wa_ea_put_Handled
};

UwpIWindowActivatedEventArgs *uwp_create_activated_args(UwpCoreWindowActivationState state) {
    UwpActivatedArgsObj *o = (UwpActivatedArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->primary.lpVtbl = &g_wa_vtbl;
    o->event_args.lpVtbl = &g_wa_ea_vtbl;
    o->ref = 1;
    o->state = state;
    return &o->primary;
}

/* ============================================================================
 * 4. VisibilityChangedEventArgs (IVisibilityChangedEventArgs + ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpVisibilityArgsObj {
    UwpIVisibilityChangedEventArgs primary;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    uint8_t visible;
    uint8_t handled;
} UwpVisibilityArgsObj;

static HRESULT UWP_CALL vis_qi_impl(UwpVisibilityArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IVisibilityChangedEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL vis_QueryInterface(UwpIVisibilityChangedEventArgs *This, REFIID riid, void **ppv) {
    return vis_qi_impl(UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL vis_AddRef(UwpIVisibilityChangedEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, primary)->ref);
}
static ULONG UWP_CALL vis_Release(UwpIVisibilityChangedEventArgs *This) {
    UwpVisibilityArgsObj *o = UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL vis_GetIids(UwpIVisibilityChangedEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL vis_GetRuntimeClassName(UwpIVisibilityChangedEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.VisibilityChangedEventArgs", n);
}
static HRESULT UWP_CALL vis_GetTrustLevel(UwpIVisibilityChangedEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL vis_get_Visible(UwpIVisibilityChangedEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, primary)->visible;
    return S_OK;
}
static HRESULT UWP_CALL vis_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return vis_qi_impl(UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL vis_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, event_args)->ref);
}
static ULONG UWP_CALL vis_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpVisibilityArgsObj *o = UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL vis_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL vis_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.VisibilityChangedEventArgs", n);
}
static HRESULT UWP_CALL vis_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL vis_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL vis_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpVisibilityArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpIVisibilityChangedEventArgsVtbl g_vis_vtbl = {
    vis_QueryInterface, vis_AddRef, vis_Release,
    vis_GetIids, vis_GetRuntimeClassName, vis_GetTrustLevel,
    vis_get_Visible
};
static const UwpICoreWindowEventArgsVtbl g_vis_ea_vtbl = {
    vis_ea_QueryInterface, vis_ea_AddRef, vis_ea_Release,
    vis_ea_GetIids, vis_ea_GetRuntimeClassName, vis_ea_GetTrustLevel,
    vis_ea_get_Handled, vis_ea_put_Handled
};

UwpIVisibilityChangedEventArgs *uwp_create_visibility_changed_args(uint8_t visible) {
    UwpVisibilityArgsObj *o = (UwpVisibilityArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->primary.lpVtbl = &g_vis_vtbl;
    o->event_args.lpVtbl = &g_vis_ea_vtbl;
    o->ref = 1;
    o->visible = visible ? 1u : 0u;
    return &o->primary;
}

/* ============================================================================
 * 5. KeyEventArgs (IKeyEventArgs + IKeyEventArgs2 + ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpKeyArgsObj {
    UwpIKeyEventArgs primary;
    UwpIKeyEventArgs2 key2;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    int32_t virtual_key;
    UwpCorePhysicalKeyStatus key_status;
    uint8_t handled;
} UwpKeyArgsObj;

static HRESULT UWP_CALL key_qi_impl(UwpKeyArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IKeyEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_IKeyEventArgs2)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->key2;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL key_QueryInterface(UwpIKeyEventArgs *This, REFIID riid, void **ppv) {
    return key_qi_impl(UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL key_AddRef(UwpIKeyEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, primary)->ref);
}
static ULONG UWP_CALL key_Release(UwpIKeyEventArgs *This) {
    UwpKeyArgsObj *o = UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL key_GetIids(UwpIKeyEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL key_GetRuntimeClassName(UwpIKeyEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.KeyEventArgs", n);
}
static HRESULT UWP_CALL key_GetTrustLevel(UwpIKeyEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL key_get_VirtualKey(UwpIKeyEventArgs *This, int32_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, primary)->virtual_key;
    return S_OK;
}
static HRESULT UWP_CALL key_get_KeyStatus(UwpIKeyEventArgs *This, UwpCorePhysicalKeyStatus *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, primary)->key_status;
    return S_OK;
}
static HRESULT UWP_CALL key2_QueryInterface(UwpIKeyEventArgs2 *This, REFIID riid, void **ppv) {
    return key_qi_impl(UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, key2), riid, ppv);
}
static ULONG UWP_CALL key2_AddRef(UwpIKeyEventArgs2 *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, key2)->ref);
}
static ULONG UWP_CALL key2_Release(UwpIKeyEventArgs2 *This) {
    UwpKeyArgsObj *o = UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, key2);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL key2_GetIids(UwpIKeyEventArgs2 *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL key2_GetRuntimeClassName(UwpIKeyEventArgs2 *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.KeyEventArgs", n);
}
static HRESULT UWP_CALL key2_GetTrustLevel(UwpIKeyEventArgs2 *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL key2_get_DeviceId(UwpIKeyEventArgs2 *This, UWP_HSTRING *v) {
    (void)This; return uwp_hstring_create_from_wstr(L"keyboard:0", v);
}
static HRESULT UWP_CALL key_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return key_qi_impl(UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL key_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, event_args)->ref);
}
static ULONG UWP_CALL key_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpKeyArgsObj *o = UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL key_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL key_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.KeyEventArgs", n);
}
static HRESULT UWP_CALL key_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL key_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL key_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpKeyArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpIKeyEventArgsVtbl g_key_vtbl = {
    key_QueryInterface, key_AddRef, key_Release,
    key_GetIids, key_GetRuntimeClassName, key_GetTrustLevel,
    key_get_VirtualKey, key_get_KeyStatus
};
static const UwpIKeyEventArgs2Vtbl g_key2_vtbl = {
    key2_QueryInterface, key2_AddRef, key2_Release,
    key2_GetIids, key2_GetRuntimeClassName, key2_GetTrustLevel,
    key2_get_DeviceId
};
static const UwpICoreWindowEventArgsVtbl g_key_ea_vtbl = {
    key_ea_QueryInterface, key_ea_AddRef, key_ea_Release,
    key_ea_GetIids, key_ea_GetRuntimeClassName, key_ea_GetTrustLevel,
    key_ea_get_Handled, key_ea_put_Handled
};

UwpIKeyEventArgs *uwp_create_key_event_args(int32_t virtual_key, const UwpCorePhysicalKeyStatus *status) {
    UwpKeyArgsObj *o = (UwpKeyArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->primary.lpVtbl = &g_key_vtbl;
    o->key2.lpVtbl = &g_key2_vtbl;
    o->event_args.lpVtbl = &g_key_ea_vtbl;
    o->ref = 1;
    o->virtual_key = virtual_key;
    if (status) o->key_status = *status;
    return &o->primary;
}

/* ============================================================================
 * 6. CharacterReceivedEventArgs (ICharacterReceivedEventArgs + ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpCharArgsObj {
    UwpICharacterReceivedEventArgs primary;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    uint32_t key_code;
    UwpCorePhysicalKeyStatus key_status;
    uint8_t handled;
} UwpCharArgsObj;

static HRESULT UWP_CALL chr_qi_impl(UwpCharArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ICharacterReceivedEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL chr_QueryInterface(UwpICharacterReceivedEventArgs *This, REFIID riid, void **ppv) {
    return chr_qi_impl(UWP_CONTAINING_RECORD(This, UwpCharArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL chr_AddRef(UwpICharacterReceivedEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpCharArgsObj, primary)->ref);
}
static ULONG UWP_CALL chr_Release(UwpICharacterReceivedEventArgs *This) {
    UwpCharArgsObj *o = UWP_CONTAINING_RECORD(This, UwpCharArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL chr_GetIids(UwpICharacterReceivedEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL chr_GetRuntimeClassName(UwpICharacterReceivedEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CharacterReceivedEventArgs", n);
}
static HRESULT UWP_CALL chr_GetTrustLevel(UwpICharacterReceivedEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL chr_get_KeyCode(UwpICharacterReceivedEventArgs *This, uint32_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpCharArgsObj, primary)->key_code;
    return S_OK;
}
static HRESULT UWP_CALL chr_get_KeyStatus(UwpICharacterReceivedEventArgs *This, UwpCorePhysicalKeyStatus *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpCharArgsObj, primary)->key_status;
    return S_OK;
}
static HRESULT UWP_CALL chr_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return chr_qi_impl(UWP_CONTAINING_RECORD(This, UwpCharArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL chr_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpCharArgsObj, event_args)->ref);
}
static ULONG UWP_CALL chr_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpCharArgsObj *o = UWP_CONTAINING_RECORD(This, UwpCharArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL chr_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL chr_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.CharacterReceivedEventArgs", n);
}
static HRESULT UWP_CALL chr_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL chr_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpCharArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL chr_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpCharArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpICharacterReceivedEventArgsVtbl g_chr_vtbl = {
    chr_QueryInterface, chr_AddRef, chr_Release,
    chr_GetIids, chr_GetRuntimeClassName, chr_GetTrustLevel,
    chr_get_KeyCode, chr_get_KeyStatus
};
static const UwpICoreWindowEventArgsVtbl g_chr_ea_vtbl = {
    chr_ea_QueryInterface, chr_ea_AddRef, chr_ea_Release,
    chr_ea_GetIids, chr_ea_GetRuntimeClassName, chr_ea_GetTrustLevel,
    chr_ea_get_Handled, chr_ea_put_Handled
};

UwpICharacterReceivedEventArgs *uwp_create_char_received_args(uint32_t key_code,
                                                              const UwpCorePhysicalKeyStatus *status) {
    UwpCharArgsObj *o = (UwpCharArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->primary.lpVtbl = &g_chr_vtbl;
    o->event_args.lpVtbl = &g_chr_ea_vtbl;
    o->ref = 1;
    o->key_code = key_code;
    if (status) o->key_status = *status;
    return &o->primary;
}

/* ============================================================================
 * 7. AcceleratorKeyEventArgs (IAcceleratorKeyEventArgs + ICoreWindowEventArgs)
 * ============================================================================ */

typedef struct UwpAccelArgsObj {
    UwpIAcceleratorKeyEventArgs primary;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    UwpCoreAcceleratorKeyEventType event_type;
    int32_t virtual_key;
    UwpCorePhysicalKeyStatus key_status;
    uint8_t handled;
} UwpAccelArgsObj;

static HRESULT UWP_CALL acc_qi_impl(UwpAccelArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IAcceleratorKeyEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL acc_QueryInterface(UwpIAcceleratorKeyEventArgs *This, REFIID riid, void **ppv) {
    return acc_qi_impl(UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL acc_AddRef(UwpIAcceleratorKeyEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, primary)->ref);
}
static ULONG UWP_CALL acc_Release(UwpIAcceleratorKeyEventArgs *This) {
    UwpAccelArgsObj *o = UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL acc_GetIids(UwpIAcceleratorKeyEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL acc_GetRuntimeClassName(UwpIAcceleratorKeyEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.AcceleratorKeyEventArgs", n);
}
static HRESULT UWP_CALL acc_GetTrustLevel(UwpIAcceleratorKeyEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL acc_get_EventType(UwpIAcceleratorKeyEventArgs *This, UwpCoreAcceleratorKeyEventType *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, primary)->event_type;
    return S_OK;
}
static HRESULT UWP_CALL acc_get_VirtualKey(UwpIAcceleratorKeyEventArgs *This, int32_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, primary)->virtual_key;
    return S_OK;
}
static HRESULT UWP_CALL acc_get_KeyStatus(UwpIAcceleratorKeyEventArgs *This, UwpCorePhysicalKeyStatus *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, primary)->key_status;
    return S_OK;
}
static HRESULT UWP_CALL acc_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return acc_qi_impl(UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL acc_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, event_args)->ref);
}
static ULONG UWP_CALL acc_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpAccelArgsObj *o = UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL acc_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL acc_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.AcceleratorKeyEventArgs", n);
}
static HRESULT UWP_CALL acc_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL acc_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL acc_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpAccelArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpIAcceleratorKeyEventArgsVtbl g_acc_vtbl = {
    acc_QueryInterface, acc_AddRef, acc_Release,
    acc_GetIids, acc_GetRuntimeClassName, acc_GetTrustLevel,
    acc_get_EventType, acc_get_VirtualKey, acc_get_KeyStatus
};
static const UwpICoreWindowEventArgsVtbl g_acc_ea_vtbl = {
    acc_ea_QueryInterface, acc_ea_AddRef, acc_ea_Release,
    acc_ea_GetIids, acc_ea_GetRuntimeClassName, acc_ea_GetTrustLevel,
    acc_ea_get_Handled, acc_ea_put_Handled
};

UwpIAcceleratorKeyEventArgs *uwp_create_accel_key_args(UwpCoreAcceleratorKeyEventType type,
                                                       int32_t vkey,
                                                       const UwpCorePhysicalKeyStatus *status) {
    UwpAccelArgsObj *o = (UwpAccelArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->primary.lpVtbl = &g_acc_vtbl;
    o->event_args.lpVtbl = &g_acc_ea_vtbl;
    o->ref = 1;
    o->event_type = type;
    o->virtual_key = vkey;
    if (status) o->key_status = *status;
    return &o->primary;
}

/* ============================================================================
 * 8. PointerDevice, PointerPointProperties, PointerPoint, PointerEventArgs
 * ============================================================================ */

typedef struct UwpPointerDeviceObj {
    UwpIPointerDevice iface;
    volatile int32_t ref;
} UwpPointerDeviceObj;

static HRESULT UWP_CALL pd_QueryInterface(UwpIPointerDevice *This, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IPointerDevice)) {
        This->lpVtbl->AddRef(This);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG UWP_CALL pd_AddRef(UwpIPointerDevice *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpPointerDeviceObj, iface)->ref);
}
static ULONG UWP_CALL pd_Release(UwpIPointerDevice *This) {
    UwpPointerDeviceObj *o = UWP_CONTAINING_RECORD(This, UwpPointerDeviceObj, iface);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL pd_GetIids(UwpIPointerDevice *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL pd_GetRuntimeClassName(UwpIPointerDevice *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.Devices.Input.PointerDevice", n);
}
static HRESULT UWP_CALL pd_GetTrustLevel(UwpIPointerDevice *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL pd_get_PointerDeviceType(UwpIPointerDevice *This, UwpPointerDeviceType *v) {
    (void)This;
    if (!v) return E_INVALIDARG;
    *v = UWP_POINTER_DEVICE_MOUSE;
    return S_OK;
}
static HRESULT UWP_CALL pd_get_IsIntegrated(UwpIPointerDevice *This, uint8_t *v) {
    (void)This;
    if (!v) return E_INVALIDARG;
    *v = 0u;
    return S_OK;
}
static HRESULT UWP_CALL pd_get_MaxContacts(UwpIPointerDevice *This, uint32_t *v) {
    (void)This;
    if (!v) return E_INVALIDARG;
    *v = 1u;
    return S_OK;
}
static HRESULT UWP_CALL pd_get_PhysicalDeviceRect(UwpIPointerDevice *This, UwpRect *v) {
    (void)This;
    if (!v) return E_INVALIDARG;
    uint32_t w = 0, h = 0;
    uwp_core_get_client_size(&w, &h);
    v->X = 0.0f; v->Y = 0.0f;
    v->Width = (float)(w ? w : 1280u);
    v->Height = (float)(h ? h : 720u);
    return S_OK;
}
static HRESULT UWP_CALL pd_get_ScreenRect(UwpIPointerDevice *This, UwpRect *v) {
    return pd_get_PhysicalDeviceRect(This, v);
}
static HRESULT UWP_CALL pd_get_SupportedUsages(UwpIPointerDevice *This, void **v) {
    (void)This;
    if (v) *v = NULL;
    uwp_bridge_log("input", "IPointerDevice::get_SupportedUsages not implemented");
    return E_NOTIMPL;
}

static const UwpIPointerDeviceVtbl g_pd_vtbl = {
    pd_QueryInterface, pd_AddRef, pd_Release,
    pd_GetIids, pd_GetRuntimeClassName, pd_GetTrustLevel,
    pd_get_PointerDeviceType, pd_get_IsIntegrated, pd_get_MaxContacts,
    pd_get_PhysicalDeviceRect, pd_get_ScreenRect, pd_get_SupportedUsages
};

typedef struct UwpPointerPointObj {
    UwpIPointerPoint point;
    UwpIPointerPointProperties props;
    volatile int32_t ref;
    uwp_pointer_event_params_t p;
} UwpPointerPointObj;

static HRESULT UWP_CALL pp_qi_impl(UwpPointerPointObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IPointerPoint)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->point;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_IPointerPointProperties)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->props;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL pp_QueryInterface(UwpIPointerPoint *This, REFIID riid, void **ppv) {
    return pp_qi_impl(UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point), riid, ppv);
}
static ULONG UWP_CALL pp_AddRef(UwpIPointerPoint *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point)->ref);
}
static ULONG UWP_CALL pp_Release(UwpIPointerPoint *This) {
    UwpPointerPointObj *o = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL pp_GetIids(UwpIPointerPoint *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL pp_GetRuntimeClassName(UwpIPointerPoint *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Input.PointerPoint", n);
}
static HRESULT UWP_CALL pp_GetTrustLevel(UwpIPointerPoint *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL pp_get_PointerDevice(UwpIPointerPoint *This, UwpIPointerDevice **v) {
    (void)This;
    if (!v) return E_INVALIDARG;
    UwpPointerDeviceObj *d = (UwpPointerDeviceObj *)calloc(1, sizeof(*d));
    if (!d) return E_OUTOFMEMORY;
    d->iface.lpVtbl = &g_pd_vtbl;
    d->ref = 1;
    *v = &d->iface;
    return S_OK;
}
static HRESULT UWP_CALL pp_get_Position(UwpIPointerPoint *This, UwpPoint *v) {
    if (!v) return E_INVALIDARG;
    UwpPointerPointObj *o = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point);
    v->X = o->p.x;
    v->Y = o->p.y;
    return S_OK;
}
static HRESULT UWP_CALL pp_get_RawPosition(UwpIPointerPoint *This, UwpPoint *v) {
    return pp_get_Position(This, v);
}
static HRESULT UWP_CALL pp_get_PointerId(UwpIPointerPoint *This, uint32_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point)->p.pointer_id;
    return S_OK;
}
static HRESULT UWP_CALL pp_get_FrameId(UwpIPointerPoint *This, uint32_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point)->p.frame_id;
    return S_OK;
}
static HRESULT UWP_CALL pp_get_Timestamp(UwpIPointerPoint *This, uint64_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point)->p.timestamp_us;
    return S_OK;
}
static HRESULT UWP_CALL pp_get_IsInContact(UwpIPointerPoint *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point)->p.in_contact;
    return S_OK;
}
static HRESULT UWP_CALL pp_get_Properties(UwpIPointerPoint *This, UwpIPointerPointProperties **v) {
    if (!v) return E_INVALIDARG;
    UwpPointerPointObj *o = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, point);
    uwp_atomic_inc(&o->ref);
    *v = &o->props;
    return S_OK;
}

static HRESULT UWP_CALL ppp_QueryInterface(UwpIPointerPointProperties *This, REFIID riid, void **ppv) {
    return pp_qi_impl(UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props), riid, ppv);
}
static ULONG UWP_CALL ppp_AddRef(UwpIPointerPointProperties *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->ref);
}
static ULONG UWP_CALL ppp_Release(UwpIPointerPointProperties *This) {
    UwpPointerPointObj *o = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL ppp_GetIids(UwpIPointerPointProperties *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL ppp_GetRuntimeClassName(UwpIPointerPointProperties *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Input.PointerPointProperties", n);
}
static HRESULT UWP_CALL ppp_GetTrustLevel(UwpIPointerPointProperties *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL ppp_get_Pressure(UwpIPointerPointProperties *This, float *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.in_contact ? 0.5f : 0.0f;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_False(UwpIPointerPointProperties *This, uint8_t *v) {
    (void)This; if (!v) return E_INVALIDARG; *v = 0u; return S_OK;
}
static HRESULT UWP_CALL ppp_get_True(UwpIPointerPointProperties *This, uint8_t *v) {
    (void)This; if (!v) return E_INVALIDARG; *v = 1u; return S_OK;
}
static HRESULT UWP_CALL ppp_get_ZeroFloat(UwpIPointerPointProperties *This, float *v) {
    (void)This; if (!v) return E_INVALIDARG; *v = 0.0f; return S_OK;
}
static HRESULT UWP_CALL ppp_get_ContactRect(UwpIPointerPointProperties *This, UwpRect *v) {
    if (!v) return E_INVALIDARG;
    UwpPointerPointObj *o = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props);
    v->X = o->p.x; v->Y = o->p.y; v->Width = 1.0f; v->Height = 1.0f;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_IsLeft(UwpIPointerPointProperties *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.left_button;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_IsRight(UwpIPointerPointProperties *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.right_button;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_IsMiddle(UwpIPointerPointProperties *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.middle_button;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_WheelDelta(UwpIPointerPointProperties *This, int32_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.wheel_delta;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_IsHorizWheel(UwpIPointerPointProperties *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.horizontal_wheel;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_IsX1(UwpIPointerPointProperties *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.xbutton1;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_IsX2(UwpIPointerPointProperties *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.xbutton2;
    return S_OK;
}
static HRESULT UWP_CALL ppp_get_UpdateKind(UwpIPointerPointProperties *This, UwpPointerUpdateKind *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerPointObj, props)->p.update_kind;
    return S_OK;
}
static HRESULT UWP_CALL ppp_HasUsage(UwpIPointerPointProperties *This, uint32_t p, uint32_t u, uint8_t *v) {
    (void)This; (void)p; (void)u;
    if (!v) return E_INVALIDARG;
    *v = 0u;
    return S_OK;
}
static HRESULT UWP_CALL ppp_GetUsageValue(UwpIPointerPointProperties *This, uint32_t p, uint32_t u, int32_t *v) {
    (void)This; (void)p; (void)u;
    if (v) *v = 0;
    return E_INVALIDARG;
}

static const UwpIPointerPointVtbl g_pp_vtbl = {
    pp_QueryInterface, pp_AddRef, pp_Release,
    pp_GetIids, pp_GetRuntimeClassName, pp_GetTrustLevel,
    pp_get_PointerDevice, pp_get_Position, pp_get_RawPosition,
    pp_get_PointerId, pp_get_FrameId, pp_get_Timestamp,
    pp_get_IsInContact, pp_get_Properties
};

static const UwpIPointerPointPropertiesVtbl g_ppp_vtbl = {
    ppp_QueryInterface, ppp_AddRef, ppp_Release,
    ppp_GetIids, ppp_GetRuntimeClassName, ppp_GetTrustLevel,
    ppp_get_Pressure,
    ppp_get_False, /* IsInverted */
    ppp_get_False, /* IsEraser */
    ppp_get_ZeroFloat, /* Orientation */
    ppp_get_ZeroFloat, /* XTilt */
    ppp_get_ZeroFloat, /* YTilt */
    ppp_get_ZeroFloat, /* Twist */
    ppp_get_ContactRect,
    ppp_get_ContactRect,
    ppp_get_False, /* TouchConfidence */
    ppp_get_IsLeft,
    ppp_get_IsRight,
    ppp_get_IsMiddle,
    ppp_get_WheelDelta,
    ppp_get_IsHorizWheel,
    ppp_get_True, /* IsPrimary */
    ppp_get_True, /* IsInRange */
    ppp_get_False, /* IsCanceled */
    ppp_get_False, /* IsBarrelButtonPressed */
    ppp_get_IsX1,
    ppp_get_IsX2,
    ppp_get_UpdateKind,
    ppp_HasUsage,
    ppp_GetUsageValue
};

typedef struct UwpPointerArgsObj {
    UwpIPointerEventArgs primary;
    UwpICoreWindowEventArgs event_args;
    volatile int32_t ref;
    UwpPointerPointObj *point;
    UwpVirtualKeyModifiers modifiers;
    uint8_t handled;
} UwpPointerArgsObj;

static void ptr_args_free(UwpPointerArgsObj *o) {
    if (o->point) {
        o->point->point.lpVtbl->Release(&o->point->point);
    }
    free(o);
}

static HRESULT UWP_CALL pa_qi_impl(UwpPointerArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IPointerEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->primary;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ICoreWindowEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->event_args;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL pa_QueryInterface(UwpIPointerEventArgs *This, REFIID riid, void **ppv) {
    return pa_qi_impl(UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, primary), riid, ppv);
}
static ULONG UWP_CALL pa_AddRef(UwpIPointerEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, primary)->ref);
}
static ULONG UWP_CALL pa_Release(UwpIPointerEventArgs *This) {
    UwpPointerArgsObj *o = UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, primary);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) ptr_args_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL pa_GetIids(UwpIPointerEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL pa_GetRuntimeClassName(UwpIPointerEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.PointerEventArgs", n);
}
static HRESULT UWP_CALL pa_GetTrustLevel(UwpIPointerEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL pa_get_CurrentPoint(UwpIPointerEventArgs *This, UwpIPointerPoint **v) {
    if (!v) return E_INVALIDARG;
    UwpPointerArgsObj *o = UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, primary);
    o->point->point.lpVtbl->AddRef(&o->point->point);
    *v = &o->point->point;
    return S_OK;
}
static HRESULT UWP_CALL pa_get_KeyModifiers(UwpIPointerEventArgs *This, UwpVirtualKeyModifiers *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, primary)->modifiers;
    return S_OK;
}
static HRESULT UWP_CALL pa_GetIntermediatePoints(UwpIPointerEventArgs *This, void **v) {
    (void)This;
    if (v) *v = NULL;
    uwp_bridge_log("input", "IPointerEventArgs::GetIntermediatePoints not implemented");
    return E_NOTIMPL;
}
static HRESULT UWP_CALL pa_ea_QueryInterface(UwpICoreWindowEventArgs *This, REFIID riid, void **ppv) {
    return pa_qi_impl(UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, event_args), riid, ppv);
}
static ULONG UWP_CALL pa_ea_AddRef(UwpICoreWindowEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, event_args)->ref);
}
static ULONG UWP_CALL pa_ea_Release(UwpICoreWindowEventArgs *This) {
    UwpPointerArgsObj *o = UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, event_args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) ptr_args_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL pa_ea_GetIids(UwpICoreWindowEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL pa_ea_GetRuntimeClassName(UwpICoreWindowEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.PointerEventArgs", n);
}
static HRESULT UWP_CALL pa_ea_GetTrustLevel(UwpICoreWindowEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL pa_ea_get_Handled(UwpICoreWindowEventArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, event_args)->handled;
    return S_OK;
}
static HRESULT UWP_CALL pa_ea_put_Handled(UwpICoreWindowEventArgs *This, uint8_t v) {
    UWP_CONTAINING_RECORD(This, UwpPointerArgsObj, event_args)->handled = v ? 1u : 0u;
    return S_OK;
}

static const UwpIPointerEventArgsVtbl g_pa_vtbl = {
    pa_QueryInterface, pa_AddRef, pa_Release,
    pa_GetIids, pa_GetRuntimeClassName, pa_GetTrustLevel,
    pa_get_CurrentPoint, pa_get_KeyModifiers, pa_GetIntermediatePoints
};
static const UwpICoreWindowEventArgsVtbl g_pa_ea_vtbl = {
    pa_ea_QueryInterface, pa_ea_AddRef, pa_ea_Release,
    pa_ea_GetIids, pa_ea_GetRuntimeClassName, pa_ea_GetTrustLevel,
    pa_ea_get_Handled, pa_ea_put_Handled
};

UwpIPointerEventArgs *uwp_create_pointer_event_args(const uwp_pointer_event_params_t *p) {
    UwpPointerArgsObj *o = (UwpPointerArgsObj *)calloc(1, sizeof(*o));
    UwpPointerPointObj *pt = (UwpPointerPointObj *)calloc(1, sizeof(*pt));
    if (!o || !pt) {
        free(o);
        free(pt);
        return NULL;
    }
    pt->point.lpVtbl = &g_pp_vtbl;
    pt->props.lpVtbl = &g_ppp_vtbl;
    pt->ref = 1;
    if (p) pt->p = *p;

    o->primary.lpVtbl = &g_pa_vtbl;
    o->event_args.lpVtbl = &g_pa_ea_vtbl;
    o->ref = 1;
    o->point = pt;
    o->modifiers = p ? p->key_modifiers : UWP_MOD_NONE;
    return &o->primary;
}

/* ============================================================================
 * 9. IdleDispatchedHandlerArgs
 * ============================================================================ */

typedef struct UwpIdleArgsObj {
    UwpIIdleDispatchedHandlerArgs iface;
    volatile int32_t ref;
    uint8_t is_idle;
} UwpIdleArgsObj;

static HRESULT UWP_CALL idle_QueryInterface(UwpIIdleDispatchedHandlerArgs *This, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IIdleDispatchedHandlerArgs)) {
        This->lpVtbl->AddRef(This);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG UWP_CALL idle_AddRef(UwpIIdleDispatchedHandlerArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpIdleArgsObj, iface)->ref);
}
static ULONG UWP_CALL idle_Release(UwpIIdleDispatchedHandlerArgs *This) {
    UwpIdleArgsObj *o = UWP_CONTAINING_RECORD(This, UwpIdleArgsObj, iface);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL idle_GetIids(UwpIIdleDispatchedHandlerArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL idle_GetRuntimeClassName(UwpIIdleDispatchedHandlerArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.UI.Core.IdleDispatchedHandlerArgs", n);
}
static HRESULT UWP_CALL idle_GetTrustLevel(UwpIIdleDispatchedHandlerArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL idle_get_IsDispatcherIdle(UwpIIdleDispatchedHandlerArgs *This, uint8_t *v) {
    if (!v) return E_INVALIDARG;
    *v = UWP_CONTAINING_RECORD(This, UwpIdleArgsObj, iface)->is_idle;
    return S_OK;
}

static const UwpIIdleDispatchedHandlerArgsVtbl g_idle_vtbl = {
    idle_QueryInterface, idle_AddRef, idle_Release,
    idle_GetIids, idle_GetRuntimeClassName, idle_GetTrustLevel,
    idle_get_IsDispatcherIdle
};

UwpIIdleDispatchedHandlerArgs *uwp_create_idle_dispatched_args(uint8_t is_idle) {
    UwpIdleArgsObj *o = (UwpIdleArgsObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->iface.lpVtbl = &g_idle_vtbl;
    o->ref = 1;
    o->is_idle = is_idle ? 1u : 0u;
    return &o->iface;
}

/* ============================================================================
 * 10. SplashScreen + LaunchActivatedEventArgs
 * ============================================================================ */

typedef struct UwpSplashScreenObj {
    UwpISplashScreen iface;
    volatile int32_t ref;
    uwp_event_list_t dismissed;
} UwpSplashScreenObj;

static HRESULT UWP_CALL spl_QueryInterface(UwpISplashScreen *This, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ISplashScreen)) {
        This->lpVtbl->AddRef(This);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG UWP_CALL spl_AddRef(UwpISplashScreen *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpSplashScreenObj, iface)->ref);
}
static ULONG UWP_CALL spl_Release(UwpISplashScreen *This) {
    UwpSplashScreenObj *o = UWP_CONTAINING_RECORD(This, UwpSplashScreenObj, iface);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) {
        uwp_event_list_destroy(&o->dismissed);
        free(o);
    }
    return (ULONG)r;
}
static HRESULT UWP_CALL spl_GetIids(UwpISplashScreen *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL spl_GetRuntimeClassName(UwpISplashScreen *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Activation.SplashScreen", n);
}
static HRESULT UWP_CALL spl_GetTrustLevel(UwpISplashScreen *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL spl_get_ImageLocation(UwpISplashScreen *This, UwpRect *v) {
    (void)This;
    if (!v) return E_INVALIDARG;
    uint32_t w = 0, h = 0;
    uwp_core_get_client_size(&w, &h);
    v->X = 0.0f; v->Y = 0.0f;
    v->Width = (float)(w ? w : 620u);
    v->Height = (float)(h ? h : 300u);
    return S_OK;
}
static HRESULT UWP_CALL spl_add_Dismissed(UwpISplashScreen *This, UwpTypedEventHandler *h,
                                          UwpEventRegistrationToken *t) {
    UwpSplashScreenObj *o = UWP_CONTAINING_RECORD(This, UwpSplashScreenObj, iface);
    return uwp_event_list_add(&o->dismissed, h, t);
}
static HRESULT UWP_CALL spl_remove_Dismissed(UwpISplashScreen *This, UwpEventRegistrationToken t) {
    UwpSplashScreenObj *o = UWP_CONTAINING_RECORD(This, UwpSplashScreenObj, iface);
    return uwp_event_list_remove(&o->dismissed, t);
}

static const UwpISplashScreenVtbl g_spl_vtbl = {
    spl_QueryInterface, spl_AddRef, spl_Release,
    spl_GetIids, spl_GetRuntimeClassName, spl_GetTrustLevel,
    spl_get_ImageLocation, spl_add_Dismissed, spl_remove_Dismissed
};

typedef struct UwpLaunchArgsObj {
    UwpIActivatedEventArgs activated;
    UwpILaunchActivatedEventArgs launch;
    volatile int32_t ref;
    UwpSplashScreenObj *splash;
    wchar_t arguments[512];
    wchar_t tile_id[128];
} UwpLaunchArgsObj;

static void launch_args_free(UwpLaunchArgsObj *o) {
    if (o->splash) o->splash->iface.lpVtbl->Release(&o->splash->iface);
    free(o);
}
static HRESULT UWP_CALL la_qi_impl(UwpLaunchArgsObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IActivatedEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->activated;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ILaunchActivatedEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->launch;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL la_QueryInterface(UwpIActivatedEventArgs *This, REFIID riid, void **ppv) {
    return la_qi_impl(UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, activated), riid, ppv);
}
static ULONG UWP_CALL la_AddRef(UwpIActivatedEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, activated)->ref);
}
static ULONG UWP_CALL la_Release(UwpIActivatedEventArgs *This) {
    UwpLaunchArgsObj *o = UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, activated);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) launch_args_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL la_GetIids(UwpIActivatedEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL la_GetRuntimeClassName(UwpIActivatedEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs", n);
}
static HRESULT UWP_CALL la_GetTrustLevel(UwpIActivatedEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL la_get_Kind(UwpIActivatedEventArgs *This, UwpActivationKind *v) {
    (void)This; if (!v) return E_INVALIDARG;
    *v = UWP_ACTIVATION_KIND_LAUNCH;
    return S_OK;
}
static HRESULT UWP_CALL la_get_PreviousExecutionState(UwpIActivatedEventArgs *This, UwpApplicationExecutionState *v) {
    (void)This; if (!v) return E_INVALIDARG;
    *v = UWP_EXEC_STATE_NOT_RUNNING;
    return S_OK;
}
static HRESULT UWP_CALL la_get_SplashScreen(UwpIActivatedEventArgs *This, UwpISplashScreen **v) {
    if (!v) return E_INVALIDARG;
    UwpLaunchArgsObj *o = UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, activated);
    o->splash->iface.lpVtbl->AddRef(&o->splash->iface);
    *v = &o->splash->iface;
    return S_OK;
}
static HRESULT UWP_CALL lla_QueryInterface(UwpILaunchActivatedEventArgs *This, REFIID riid, void **ppv) {
    return la_qi_impl(UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, launch), riid, ppv);
}
static ULONG UWP_CALL lla_AddRef(UwpILaunchActivatedEventArgs *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, launch)->ref);
}
static ULONG UWP_CALL lla_Release(UwpILaunchActivatedEventArgs *This) {
    UwpLaunchArgsObj *o = UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, launch);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) launch_args_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL lla_GetIids(UwpILaunchActivatedEventArgs *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL lla_GetRuntimeClassName(UwpILaunchActivatedEventArgs *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs", n);
}
static HRESULT UWP_CALL lla_GetTrustLevel(UwpILaunchActivatedEventArgs *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL lla_get_Arguments(UwpILaunchActivatedEventArgs *This, UWP_HSTRING *v) {
    UwpLaunchArgsObj *o = UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, launch);
    return uwp_hstring_create_from_wstr(o->arguments, v);
}
static HRESULT UWP_CALL lla_get_TileId(UwpILaunchActivatedEventArgs *This, UWP_HSTRING *v) {
    UwpLaunchArgsObj *o = UWP_CONTAINING_RECORD(This, UwpLaunchArgsObj, launch);
    return uwp_hstring_create_from_wstr(o->tile_id, v);
}

static const UwpIActivatedEventArgsVtbl g_la_vtbl = {
    la_QueryInterface, la_AddRef, la_Release,
    la_GetIids, la_GetRuntimeClassName, la_GetTrustLevel,
    la_get_Kind, la_get_PreviousExecutionState, la_get_SplashScreen
};
static const UwpILaunchActivatedEventArgsVtbl g_lla_vtbl = {
    lla_QueryInterface, lla_AddRef, lla_Release,
    lla_GetIids, lla_GetRuntimeClassName, lla_GetTrustLevel,
    lla_get_Arguments, lla_get_TileId
};

UwpIActivatedEventArgs *uwp_create_launch_activated_args(const wchar_t *arguments, const wchar_t *tile_id) {
    UwpLaunchArgsObj *o = (UwpLaunchArgsObj *)calloc(1, sizeof(*o));
    UwpSplashScreenObj *s = (UwpSplashScreenObj *)calloc(1, sizeof(*s));
    if (!o || !s) {
        free(o); free(s);
        return NULL;
    }
    s->iface.lpVtbl = &g_spl_vtbl;
    s->ref = 1;
    uwp_event_list_init(&s->dismissed);

    o->activated.lpVtbl = &g_la_vtbl;
    o->launch.lpVtbl = &g_lla_vtbl;
    o->ref = 1;
    o->splash = s;
    if (arguments) {
        wcsncpy(o->arguments, arguments, sizeof(o->arguments) / sizeof(wchar_t) - 1);
    }
    if (tile_id) {
        wcsncpy(o->tile_id, tile_id, sizeof(o->tile_id) / sizeof(wchar_t) - 1);
    } else {
        wcscpy(o->tile_id, L"App");
    }
    return &o->activated;
}

/* ============================================================================
 * 11. SuspendingEventArgs + Entered/LeavingBackgroundEventArgs + Deferral
 * ============================================================================ */

typedef struct UwpSuspendingObj {
    UwpISuspendingEventArgs args;
    UwpISuspendingOperation op;
    UwpISuspendingDeferral deferral;
    volatile int32_t ref;
    int completed;
} UwpSuspendingObj;

static HRESULT UWP_CALL susp_qi_impl(UwpSuspendingObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_ISuspendingEventArgs)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->args;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ISuspendingOperation)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->op;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_ISuspendingDeferral)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->deferral;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL susp_args_QI(UwpISuspendingEventArgs *T, REFIID r, void **p) {
    return susp_qi_impl(UWP_CONTAINING_RECORD(T, UwpSuspendingObj, args), r, p);
}
static ULONG UWP_CALL susp_args_AddRef(UwpISuspendingEventArgs *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpSuspendingObj, args)->ref);
}
static ULONG UWP_CALL susp_args_Release(UwpISuspendingEventArgs *T) {
    UwpSuspendingObj *o = UWP_CONTAINING_RECORD(T, UwpSuspendingObj, args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL susp_args_GetIids(UwpISuspendingEventArgs *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL susp_args_GetName(UwpISuspendingEventArgs *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.SuspendingEventArgs", n);
}
static HRESULT UWP_CALL susp_args_GetTrust(UwpISuspendingEventArgs *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL susp_args_get_Op(UwpISuspendingEventArgs *T, UwpISuspendingOperation **v) {
    if (!v) return E_INVALIDARG;
    UwpSuspendingObj *o = UWP_CONTAINING_RECORD(T, UwpSuspendingObj, args);
    uwp_atomic_inc(&o->ref);
    *v = &o->op;
    return S_OK;
}

static HRESULT UWP_CALL susp_op_QI(UwpISuspendingOperation *T, REFIID r, void **p) {
    return susp_qi_impl(UWP_CONTAINING_RECORD(T, UwpSuspendingObj, op), r, p);
}
static ULONG UWP_CALL susp_op_AddRef(UwpISuspendingOperation *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpSuspendingObj, op)->ref);
}
static ULONG UWP_CALL susp_op_Release(UwpISuspendingOperation *T) {
    UwpSuspendingObj *o = UWP_CONTAINING_RECORD(T, UwpSuspendingObj, op);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL susp_op_GetIids(UwpISuspendingOperation *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL susp_op_GetName(UwpISuspendingOperation *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.SuspendingOperation", n);
}
static HRESULT UWP_CALL susp_op_GetTrust(UwpISuspendingOperation *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL susp_op_GetDeferral(UwpISuspendingOperation *T, UwpISuspendingDeferral **v) {
    if (!v) return E_INVALIDARG;
    UwpSuspendingObj *o = UWP_CONTAINING_RECORD(T, UwpSuspendingObj, op);
    uwp_atomic_inc(&o->ref);
    *v = &o->deferral;
    return S_OK;
}
static HRESULT UWP_CALL susp_op_get_Deadline(UwpISuspendingOperation *T, UwpDateTime *v) {
    (void)T;
    if (!v) return E_INVALIDARG;
    v->UniversalTime = 50000000LL; /* 5s relative window */
    return S_OK;
}

static HRESULT UWP_CALL susp_def_QI(UwpISuspendingDeferral *T, REFIID r, void **p) {
    return susp_qi_impl(UWP_CONTAINING_RECORD(T, UwpSuspendingObj, deferral), r, p);
}
static ULONG UWP_CALL susp_def_AddRef(UwpISuspendingDeferral *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpSuspendingObj, deferral)->ref);
}
static ULONG UWP_CALL susp_def_Release(UwpISuspendingDeferral *T) {
    UwpSuspendingObj *o = UWP_CONTAINING_RECORD(T, UwpSuspendingObj, deferral);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL susp_def_GetIids(UwpISuspendingDeferral *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL susp_def_GetName(UwpISuspendingDeferral *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.SuspendingDeferral", n);
}
static HRESULT UWP_CALL susp_def_GetTrust(UwpISuspendingDeferral *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL susp_def_Complete(UwpISuspendingDeferral *T) {
    UWP_CONTAINING_RECORD(T, UwpSuspendingObj, deferral)->completed = 1;
    return S_OK;
}

static const UwpISuspendingEventArgsVtbl g_susp_args_vtbl = {
    susp_args_QI, susp_args_AddRef, susp_args_Release,
    susp_args_GetIids, susp_args_GetName, susp_args_GetTrust,
    susp_args_get_Op
};
static const UwpISuspendingOperationVtbl g_susp_op_vtbl = {
    susp_op_QI, susp_op_AddRef, susp_op_Release,
    susp_op_GetIids, susp_op_GetName, susp_op_GetTrust,
    susp_op_GetDeferral, susp_op_get_Deadline
};
static const UwpISuspendingDeferralVtbl g_susp_def_vtbl = {
    susp_def_QI, susp_def_AddRef, susp_def_Release,
    susp_def_GetIids, susp_def_GetName, susp_def_GetTrust,
    susp_def_Complete
};

UwpISuspendingEventArgs *uwp_create_suspending_args(void) {
    UwpSuspendingObj *o = (UwpSuspendingObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->args.lpVtbl = &g_susp_args_vtbl;
    o->op.lpVtbl = &g_susp_op_vtbl;
    o->deferral.lpVtbl = &g_susp_def_vtbl;
    o->ref = 1;
    return &o->args;
}

typedef struct UwpBgEventObj {
    UwpIBackgroundEventArgs args;
    UwpIDeferral deferral;
    volatile int32_t ref;
    int is_entering;
    int completed;
} UwpBgEventObj;

static HRESULT UWP_CALL bg_qi_impl(UwpBgEventObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    const GUID *primary_iid = o->is_entering ? &UWP_IID_IEnteredBackgroundEventArgs
                                             : &UWP_IID_ILeavingBackgroundEventArgs;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, primary_iid)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->args;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_IDeferral) || uwp_guid_equal(riid, &UWP_IID_IClosable)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->deferral;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL bg_QI(UwpIBackgroundEventArgs *T, REFIID r, void **p) {
    return bg_qi_impl(UWP_CONTAINING_RECORD(T, UwpBgEventObj, args), r, p);
}
static ULONG UWP_CALL bg_AddRef(UwpIBackgroundEventArgs *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpBgEventObj, args)->ref);
}
static ULONG UWP_CALL bg_Release(UwpIBackgroundEventArgs *T) {
    UwpBgEventObj *o = UWP_CONTAINING_RECORD(T, UwpBgEventObj, args);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL bg_GetIids(UwpIBackgroundEventArgs *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL bg_GetName(UwpIBackgroundEventArgs *T, UWP_HSTRING *n) {
    UwpBgEventObj *o = UWP_CONTAINING_RECORD(T, UwpBgEventObj, args);
    return uwp_hstring_create_from_wstr(
        o->is_entering ? L"Windows.ApplicationModel.EnteredBackgroundEventArgs"
                       : L"Windows.ApplicationModel.LeavingBackgroundEventArgs", n);
}
static HRESULT UWP_CALL bg_GetTrust(UwpIBackgroundEventArgs *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL bg_GetDeferral(UwpIBackgroundEventArgs *T, UwpIDeferral **v) {
    if (!v) return E_INVALIDARG;
    UwpBgEventObj *o = UWP_CONTAINING_RECORD(T, UwpBgEventObj, args);
    uwp_atomic_inc(&o->ref);
    *v = &o->deferral;
    return S_OK;
}

static HRESULT UWP_CALL def_QI(UwpIDeferral *T, REFIID r, void **p) {
    return bg_qi_impl(UWP_CONTAINING_RECORD(T, UwpBgEventObj, deferral), r, p);
}
static ULONG UWP_CALL def_AddRef(UwpIDeferral *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpBgEventObj, deferral)->ref);
}
static ULONG UWP_CALL def_Release(UwpIDeferral *T) {
    UwpBgEventObj *o = UWP_CONTAINING_RECORD(T, UwpBgEventObj, deferral);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL def_GetIids(UwpIDeferral *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL def_GetName(UwpIDeferral *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.Foundation.Deferral", n);
}
static HRESULT UWP_CALL def_GetTrust(UwpIDeferral *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL def_Complete(UwpIDeferral *T) {
    UWP_CONTAINING_RECORD(T, UwpBgEventObj, deferral)->completed = 1;
    return S_OK;
}

static const UwpIBackgroundEventArgsVtbl g_bg_vtbl = {
    bg_QI, bg_AddRef, bg_Release,
    bg_GetIids, bg_GetName, bg_GetTrust,
    bg_GetDeferral
};
static const UwpIDeferralVtbl g_def_vtbl = {
    def_QI, def_AddRef, def_Release,
    def_GetIids, def_GetName, def_GetTrust,
    def_Complete
};

UwpIBackgroundEventArgs *uwp_create_background_event_args(int is_entering) {
    UwpBgEventObj *o = (UwpBgEventObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->args.lpVtbl = &g_bg_vtbl;
    o->deferral.lpVtbl = &g_def_vtbl;
    o->ref = 1;
    o->is_entering = is_entering ? 1 : 0;
    return &o->args;
}

/* ============================================================================
 * 12. UwpAsyncAction (IAsyncAction + IAsyncInfo) for CoreDispatcher::RunAsync
 * ============================================================================ */

static volatile int32_t g_next_async_id = 1;

struct UwpAsyncActionObj {
    UwpIAsyncAction action;
    UwpIAsyncInfo info;
    volatile int32_t ref;
    uwp_mutex_t lock;
    uint32_t id;
    UwpAsyncStatus status;
    HRESULT error_code;
    UwpAsyncActionCompletedHandler *completed_handler;
    int handler_invoked;
};

static void aa_free(UwpAsyncActionObj *o) {
    if (o->completed_handler) {
        o->completed_handler->lpVtbl->Release(o->completed_handler);
    }
    uwp_mutex_destroy(&o->lock);
    free(o);
}

static HRESULT UWP_CALL aa_qi_impl(UwpAsyncActionObj *o, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IAsyncAction)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->action;
        return S_OK;
    }
    if (uwp_guid_equal(riid, &UWP_IID_IAsyncInfo)) {
        uwp_atomic_inc(&o->ref);
        *ppv = &o->info;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static HRESULT UWP_CALL aa_QI(UwpIAsyncAction *T, REFIID r, void **p) {
    return aa_qi_impl(UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, action), r, p);
}
static ULONG UWP_CALL aa_AddRef(UwpIAsyncAction *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, action)->ref);
}
static ULONG UWP_CALL aa_Release(UwpIAsyncAction *T) {
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, action);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) aa_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL aa_GetIids(UwpIAsyncAction *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL aa_GetName(UwpIAsyncAction *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.Foundation.IAsyncAction", n);
}
static HRESULT UWP_CALL aa_GetTrust(UwpIAsyncAction *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL aa_put_Completed(UwpIAsyncAction *T, UwpAsyncActionCompletedHandler *handler) {
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, action);
    UwpAsyncActionCompletedHandler *invoke_now = NULL;
    UwpAsyncStatus invoke_status = UWP_ASYNC_STARTED;

    uwp_mutex_lock(&o->lock);
    if (o->completed_handler) {
        uwp_mutex_unlock(&o->lock);
        return E_ILLEGAL_METHOD_CALL;
    }
    if (handler) {
        handler->lpVtbl->AddRef(handler);
        o->completed_handler = handler;
        if (o->status != UWP_ASYNC_STARTED && !o->handler_invoked) {
            o->handler_invoked = 1;
            invoke_now = handler;
            invoke_now->lpVtbl->AddRef(invoke_now);
            invoke_status = o->status;
        }
    }
    uwp_mutex_unlock(&o->lock);

    if (invoke_now) {
        invoke_now->lpVtbl->Invoke(invoke_now, &o->action, invoke_status);
        invoke_now->lpVtbl->Release(invoke_now);
    }
    return S_OK;
}
static HRESULT UWP_CALL aa_get_Completed(UwpIAsyncAction *T, UwpAsyncActionCompletedHandler **handler) {
    if (!handler) return E_INVALIDARG;
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, action);
    uwp_mutex_lock(&o->lock);
    *handler = o->completed_handler;
    if (*handler) (*handler)->lpVtbl->AddRef(*handler);
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL aa_GetResults(UwpIAsyncAction *T) {
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, action);
    uwp_mutex_lock(&o->lock);
    HRESULT hr = (o->status == UWP_ASYNC_ERROR) ? o->error_code : S_OK;
    uwp_mutex_unlock(&o->lock);
    return hr;
}

static HRESULT UWP_CALL ai_QI(UwpIAsyncInfo *T, REFIID r, void **p) {
    return aa_qi_impl(UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info), r, p);
}
static ULONG UWP_CALL ai_AddRef(UwpIAsyncInfo *T) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info)->ref);
}
static ULONG UWP_CALL ai_Release(UwpIAsyncInfo *T) {
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) aa_free(o);
    return (ULONG)r;
}
static HRESULT UWP_CALL ai_GetIids(UwpIAsyncInfo *T, ULONG *c, IID **i) {
    (void)T; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL ai_GetName(UwpIAsyncInfo *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.Foundation.IAsyncAction", n);
}
static HRESULT UWP_CALL ai_GetTrust(UwpIAsyncInfo *T, UwpTrustLevel *t) {
    (void)T; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL ai_get_Id(UwpIAsyncInfo *T, uint32_t *id) {
    if (!id) return E_INVALIDARG;
    *id = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info)->id;
    return S_OK;
}
static HRESULT UWP_CALL ai_get_Status(UwpIAsyncInfo *T, UwpAsyncStatus *s) {
    if (!s) return E_INVALIDARG;
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info);
    uwp_mutex_lock(&o->lock);
    *s = o->status;
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL ai_get_ErrorCode(UwpIAsyncInfo *T, HRESULT *hr) {
    if (!hr) return E_INVALIDARG;
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info);
    uwp_mutex_lock(&o->lock);
    *hr = o->error_code;
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL ai_Cancel(UwpIAsyncInfo *T) {
    UwpAsyncActionObj *o = UWP_CONTAINING_RECORD(T, UwpAsyncActionObj, info);
    uwp_mutex_lock(&o->lock);
    if (o->status == UWP_ASYNC_STARTED) {
        o->status = UWP_ASYNC_CANCELED;
    }
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL ai_Close(UwpIAsyncInfo *T) {
    (void)T;
    return S_OK;
}

static const UwpIAsyncActionVtbl g_aa_vtbl = {
    aa_QI, aa_AddRef, aa_Release,
    aa_GetIids, aa_GetName, aa_GetTrust,
    aa_put_Completed, aa_get_Completed, aa_GetResults
};
static const UwpIAsyncInfoVtbl g_ai_vtbl = {
    ai_QI, ai_AddRef, ai_Release,
    ai_GetIids, ai_GetName, ai_GetTrust,
    ai_get_Id, ai_get_Status, ai_get_ErrorCode, ai_Cancel, ai_Close
};

UwpAsyncActionObj *uwp_async_action_create(void) {
    UwpAsyncActionObj *o = (UwpAsyncActionObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->action.lpVtbl = &g_aa_vtbl;
    o->info.lpVtbl = &g_ai_vtbl;
    o->ref = 1;
    uwp_mutex_init(&o->lock);
    o->id = (uint32_t)uwp_atomic_inc(&g_next_async_id);
    o->status = UWP_ASYNC_STARTED;
    o->error_code = S_OK;
    return o;
}

UwpIAsyncAction *uwp_async_action_iface(UwpAsyncActionObj *obj) {
    if (!obj) return NULL;
    uwp_atomic_inc(&obj->ref);
    return &obj->action;
}

void uwp_async_action_complete(UwpAsyncActionObj *obj, HRESULT result_hr) {
    if (!obj) return;
    UwpAsyncActionCompletedHandler *invoke = NULL;
    UwpAsyncStatus final_status;

    uwp_mutex_lock(&obj->lock);
    if (obj->status == UWP_ASYNC_STARTED) {
        obj->error_code = result_hr;
        obj->status = SUCCEEDED(result_hr) ? UWP_ASYNC_COMPLETED : UWP_ASYNC_ERROR;
    }
    final_status = obj->status;
    if (obj->completed_handler && !obj->handler_invoked) {
        obj->handler_invoked = 1;
        invoke = obj->completed_handler;
        invoke->lpVtbl->AddRef(invoke);
    }
    uwp_mutex_unlock(&obj->lock);

    if (invoke) {
        invoke->lpVtbl->Invoke(invoke, &obj->action, final_status);
        invoke->lpVtbl->Release(invoke);
    }
}

void uwp_async_action_release(UwpAsyncActionObj *obj) {
    if (!obj) return;
    obj->action.lpVtbl->Release(&obj->action);
}

/* ============================================================================
 * 13. PropertySet (IPropertySet + IMap<HSTRING, IInspectable*>)
 * ============================================================================ */

#define UWP_PROPSET_MAX_ITEMS 64

typedef struct uwp_propset_entry_s {
    wchar_t key[128];
    UwpIInspectable *value;
    int occupied;
} uwp_propset_entry_t;

typedef struct UwpPropertySetObj {
    UwpIMap map;
    volatile int32_t ref;
    uwp_mutex_t lock;
    uwp_propset_entry_t items[UWP_PROPSET_MAX_ITEMS];
    uint32_t count;
} UwpPropertySetObj;

static HRESULT UWP_CALL ps_QueryInterface(UwpIMap *This, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    if (uwp_guid_equal(riid, &UWP_IID_IUnknown) ||
        uwp_guid_equal(riid, &UWP_IID_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(riid, &UWP_IID_IPropertySet) ||
        uwp_guid_equal(riid, &UWP_IID_IMap_HSTRING_IInspectable) ||
        uwp_guid_equal(riid, &UWP_IID_IObservableMap_HSTRING_IInspectable)) {
        This->lpVtbl->AddRef(This);
        *ppv = This;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG UWP_CALL ps_AddRef(UwpIMap *This) {
    return (ULONG)uwp_atomic_inc(&UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map)->ref);
}
static ULONG UWP_CALL ps_Release(UwpIMap *This) {
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    int32_t r = uwp_atomic_dec(&o->ref);
    if (r == 0) {
        for (size_t i = 0; i < UWP_PROPSET_MAX_ITEMS; ++i) {
            if (o->items[i].occupied && o->items[i].value) {
                o->items[i].value->lpVtbl->Release(o->items[i].value);
            }
        }
        uwp_mutex_destroy(&o->lock);
        free(o);
    }
    return (ULONG)r;
}
static HRESULT UWP_CALL ps_GetIids(UwpIMap *This, ULONG *c, IID **i) {
    (void)This; return uwp_std_get_iids(c, i);
}
static HRESULT UWP_CALL ps_GetName(UwpIMap *This, UWP_HSTRING *n) {
    (void)This; return uwp_hstring_create_from_wstr(L"Windows.Foundation.Collections.PropertySet", n);
}
static HRESULT UWP_CALL ps_GetTrust(UwpIMap *This, UwpTrustLevel *t) {
    (void)This; return uwp_std_get_trust_level(t);
}
static HRESULT UWP_CALL ps_Lookup(UwpIMap *This, UWP_HSTRING key, UwpIInspectable **value) {
    if (!value) return E_INVALIDARG;
    *value = NULL;
    const wchar_t *k = WindowsGetStringRawBuffer(key, NULL);
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    uwp_mutex_lock(&o->lock);
    for (size_t i = 0; i < UWP_PROPSET_MAX_ITEMS; ++i) {
        if (o->items[i].occupied && wcscmp(o->items[i].key, k) == 0) {
            *value = o->items[i].value;
            if (*value) (*value)->lpVtbl->AddRef(*value);
            uwp_mutex_unlock(&o->lock);
            return S_OK;
        }
    }
    uwp_mutex_unlock(&o->lock);
    return E_BOUNDS;
}
static HRESULT UWP_CALL ps_get_Size(UwpIMap *This, uint32_t *size) {
    if (!size) return E_INVALIDARG;
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    uwp_mutex_lock(&o->lock);
    *size = o->count;
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL ps_HasKey(UwpIMap *This, UWP_HSTRING key, uint8_t *found) {
    if (!found) return E_INVALIDARG;
    *found = 0u;
    const wchar_t *k = WindowsGetStringRawBuffer(key, NULL);
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    uwp_mutex_lock(&o->lock);
    for (size_t i = 0; i < UWP_PROPSET_MAX_ITEMS; ++i) {
        if (o->items[i].occupied && wcscmp(o->items[i].key, k) == 0) {
            *found = 1u;
            break;
        }
    }
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL ps_GetView(UwpIMap *This, void **view) {
    (void)This;
    if (view) *view = NULL;
    uwp_bridge_log("collections", "PropertySet::GetView not implemented");
    return E_NOTIMPL;
}
static HRESULT UWP_CALL ps_Insert(UwpIMap *This, UWP_HSTRING key, UwpIInspectable *value, uint8_t *replaced) {
    if (!replaced) return E_INVALIDARG;
    *replaced = 0u;
    const wchar_t *k = WindowsGetStringRawBuffer(key, NULL);
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    uwp_mutex_lock(&o->lock);
    int free_idx = -1;
    for (int i = 0; i < UWP_PROPSET_MAX_ITEMS; ++i) {
        if (o->items[i].occupied && wcscmp(o->items[i].key, k) == 0) {
            UwpIInspectable *old = o->items[i].value;
            if (value) value->lpVtbl->AddRef(value);
            o->items[i].value = value;
            if (old) old->lpVtbl->Release(old);
            *replaced = 1u;
            uwp_mutex_unlock(&o->lock);
            return S_OK;
        }
        if (!o->items[i].occupied && free_idx < 0) free_idx = i;
    }
    if (free_idx < 0) {
        uwp_mutex_unlock(&o->lock);
        return E_OUTOFMEMORY;
    }
    wcsncpy(o->items[free_idx].key, k, 127);
    o->items[free_idx].key[127] = L'\0';
    if (value) value->lpVtbl->AddRef(value);
    o->items[free_idx].value = value;
    o->items[free_idx].occupied = 1;
    o->count++;
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}
static HRESULT UWP_CALL ps_Remove(UwpIMap *This, UWP_HSTRING key) {
    const wchar_t *k = WindowsGetStringRawBuffer(key, NULL);
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    uwp_mutex_lock(&o->lock);
    for (size_t i = 0; i < UWP_PROPSET_MAX_ITEMS; ++i) {
        if (o->items[i].occupied && wcscmp(o->items[i].key, k) == 0) {
            UwpIInspectable *old = o->items[i].value;
            o->items[i].occupied = 0;
            o->items[i].value = NULL;
            o->count--;
            uwp_mutex_unlock(&o->lock);
            if (old) old->lpVtbl->Release(old);
            return S_OK;
        }
    }
    uwp_mutex_unlock(&o->lock);
    return E_BOUNDS;
}
static HRESULT UWP_CALL ps_Clear(UwpIMap *This) {
    UwpPropertySetObj *o = UWP_CONTAINING_RECORD(This, UwpPropertySetObj, map);
    uwp_mutex_lock(&o->lock);
    for (size_t i = 0; i < UWP_PROPSET_MAX_ITEMS; ++i) {
        if (o->items[i].occupied) {
            if (o->items[i].value) o->items[i].value->lpVtbl->Release(o->items[i].value);
            o->items[i].value = NULL;
            o->items[i].occupied = 0;
        }
    }
    o->count = 0;
    uwp_mutex_unlock(&o->lock);
    return S_OK;
}

static const UwpIMapVtbl g_ps_vtbl = {
    ps_QueryInterface, ps_AddRef, ps_Release,
    ps_GetIids, ps_GetName, ps_GetTrust,
    ps_Lookup, ps_get_Size, ps_HasKey, ps_GetView, ps_Insert, ps_Remove, ps_Clear
};

UwpIMap *uwp_create_property_set(void) {
    UwpPropertySetObj *o = (UwpPropertySetObj *)calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->map.lpVtbl = &g_ps_vtbl;
    o->ref = 1;
    uwp_mutex_init(&o->lock);
    return &o->map;
}
