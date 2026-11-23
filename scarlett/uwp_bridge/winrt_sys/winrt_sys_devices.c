/*
 * Proton-Scarlett — UWP Bridge System WinRT: Devices & System Status
 *
 * Implements:
 *   - Windows.Devices.Enumeration.Pnp.PnpObject (IPnpObjectStatics, IPnpObject,
 *     PnpObjectCollection IVectorView/IIterable/IIterator, IPnpObjectWatcher)
 *   - Windows.Devices.HumanInterfaceDevice.HidDevice (IHidDeviceStatics)
 *   - Windows.Devices.Input.TouchCapabilities (ITouchCapabilities)
 *   - Windows.Devices.Input.MouseCapabilities (IMouseCapabilities)
 *   - Windows.Devices.Input.KeyboardCapabilities (IKeyboardCapabilities)
 *   - Windows.System.Profile.HardwareIdentification (IHardwareIdentificationStatics, IHardwareToken)
 *   - Windows.System.MemoryManager (IMemoryManagerStatics1..3, IAppMemoryReport, IProcessMemoryReport)
 *   - Windows.System.Power.PowerManager (IPowerManagerStatics)
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls).
 */

#include "winrt_sys_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * IIDs (verified against Windows.winmd & RFC 4122 UUIDv5)
 * ============================================================================ */

/* Windows.Devices.Enumeration.Pnp */
static const GUID WINRT_IID_IPnpObjectStatics =
    { 0xb3c32a3du, 0xd168u, 0x4660u, { 0xbbu, 0xf3u, 0xa7u, 0x33u, 0xb1u, 0x4bu, 0x6eu, 0x01u } };
static const GUID WINRT_IID_IPnpObject =
    { 0x95c66258u, 0x733bu, 0x4a8fu, { 0x93u, 0xa3u, 0xdbu, 0x07u, 0x8au, 0xc8u, 0x70u, 0xc1u } };
static const GUID WINRT_IID_IPnpObjectWatcher =
    { 0x83c95ca8u, 0x4772u, 0x4a7au, { 0xacu, 0xa8u, 0xe4u, 0x8cu, 0x42u, 0xa8u, 0x9cu, 0x44u } };
static const GUID WINRT_IID_IVectorView_PnpObject =
    { 0xcce5a798u, 0xd269u, 0x5fceu, { 0x99u, 0xceu, 0xefu, 0x0au, 0xe3u, 0xcdu, 0x05u, 0x69u } };
static const GUID WINRT_IID_IIterable_PnpObject =
    { 0x30b50092u, 0x36eeu, 0x53ffu, { 0x94u, 0x50u, 0x02u, 0x90u, 0x04u, 0x43u, 0x6cu, 0x60u } };
static const GUID WINRT_IID_IIterator_PnpObject =
    { 0x6bb6d2f1u, 0xb5fbu, 0x57f0u, { 0x82u, 0x51u, 0xf2u, 0x0cu, 0xdeu, 0x5au, 0x68u, 0x71u } };
static const GUID WINRT_IID_IAsyncOperation_PnpObjectCollection =
    { 0x0659eab6u, 0x97a1u, 0x54fcu, { 0x93u, 0x56u, 0xffu, 0x15u, 0xe3u, 0xb0u, 0xa8u, 0xa8u } };
static const GUID WINRT_IID_IAsyncOperation_PnpObject =
    { 0x22b0fb93u, 0x30e6u, 0x501au, { 0xbdu, 0x3bu, 0x9fu, 0xa3u, 0x06u, 0x3eu, 0x9cu, 0x16u } };

/* Windows.Devices.HumanInterfaceDevice */
static const GUID WINRT_IID_IHidDeviceStatics =
    { 0x9e5981e4u, 0x9856u, 0x418cu, { 0x9fu, 0x73u, 0x77u, 0xdeu, 0x0cu, 0xd8u, 0x57u, 0x54u } };
static const GUID WINRT_IID_IAsyncOperation_HidDevice =
    { 0xa76a4fbfu, 0x5177u, 0x5256u, { 0x84u, 0xa8u, 0xb3u, 0x1au, 0x8du, 0xcfu, 0x10u, 0x48u } };

/* Windows.Devices.Input */
static const GUID WINRT_IID_ITouchCapabilities =
    { 0x20dd55f9u, 0x13f1u, 0x46c8u, { 0x92u, 0x85u, 0x2cu, 0x05u, 0xfau, 0x3eu, 0xdau, 0x6fu } };
static const GUID WINRT_IID_IMouseCapabilities =
    { 0xbca5e023u, 0x7dd9u, 0x4b6bu, { 0x9au, 0x92u, 0x55u, 0xd4u, 0x3cu, 0xb3u, 0x8fu, 0x73u } };
static const GUID WINRT_IID_IKeyboardCapabilities =
    { 0x3a3f9b56u, 0x6798u, 0x4bbcu, { 0x83u, 0x3eu, 0x0fu, 0x34u, 0xb1u, 0x7cu, 0x65u, 0xffu } };

/* Windows.System.Profile.HardwareIdentification */
static const GUID WINRT_IID_IHardwareIdentificationStatics =
    { 0x971260e0u, 0xf170u, 0x4a42u, { 0xbdu, 0x55u, 0xa9u, 0x00u, 0xb2u, 0x12u, 0xdau, 0xe2u } };
static const GUID WINRT_IID_IHardwareToken =
    { 0x28f6d4c0u, 0xfb12u, 0x40a4u, { 0x81u, 0x67u, 0x7fu, 0x4eu, 0x03u, 0xd2u, 0x72u, 0x4cu } };

/* Windows.System.MemoryManager */
static const GUID WINRT_IID_IMemoryManagerStatics =
    { 0x5c6c279cu, 0xd7cau, 0x4779u, { 0x91u, 0x88u, 0x40u, 0x57u, 0x21u, 0x9cu, 0xe6u, 0x4cu } };
static const GUID WINRT_IID_IMemoryManagerStatics2 =
    { 0x6eee351fu, 0x6d62u, 0x423fu, { 0x94u, 0x79u, 0xb0u, 0x1fu, 0x9cu, 0x9fu, 0x76u, 0x69u } };
static const GUID WINRT_IID_IMemoryManagerStatics3 =
    { 0x149b59ceu, 0x92adu, 0x4e35u, { 0x89u, 0xebu, 0x50u, 0xdfu, 0xb4u, 0xc0u, 0xd9u, 0x1cu } };
static const GUID WINRT_IID_IAppMemoryReport =
    { 0x6d65339bu, 0x4d6fu, 0x45bcu, { 0x9cu, 0x5eu, 0xe4u, 0x9bu, 0x3fu, 0xf2u, 0x75u, 0x8du } };
static const GUID WINRT_IID_IProcessMemoryReport =
    { 0x087305a8u, 0x9b70u, 0x4782u, { 0x87u, 0x41u, 0x3au, 0x98u, 0x2bu, 0x6cu, 0xe5u, 0xe4u } };

/* Windows.System.Power.PowerManager */
static const GUID WINRT_IID_IPowerManagerStatics =
    { 0x1394825du, 0x62ceu, 0x4364u, { 0x98u, 0xd5u, 0xaau, 0x28u, 0xc7u, 0xfbu, 0xd1u, 0x5bu } };

/* ============================================================================
 * Shared Empty IMapView<HSTRING, IInspectable*> & Token Counter
 * ============================================================================ */

static volatile LONG g_dev_next_token = 100;

static HRESULT dev_add_token(void *handler, EventRegistrationToken *token) {
    if (!token) return E_POINTER;
    token->value = 0;
    if (!handler) return E_INVALIDARG;
    token->value = (int64_t)InterlockedIncrement(&g_dev_next_token);
    return S_OK;
}

typedef struct EmptyMapViewObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *Lookup)(void *This, HSTRING key, void **val);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
        HRESULT (STDMETHODCALLTYPE *HasKey)(void *This, HSTRING key, boolean *found);
        HRESULT (STDMETHODCALLTYPE *Split)(void *This, void **first, void **second);
    } *lpVtblView;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *First)(void *This, void **iter);
    } *lpVtblIter;
    volatile LONG ref_count;
} EmptyMapViewObj;

typedef struct EmptyKvpIterObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **cur);
        HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *has);
        HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *moved);
        HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t cap, void **items, uint32_t *actual);
    } *lpVtbl;
    volatile LONG ref_count;
} EmptyKvpIterObj;

static HRESULT STDMETHODCALLTYPE ekiter_qi(void *This, REFIID riid, void **ppv) {
    EmptyKvpIterObj *o = (EmptyKvpIterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IIterator_KVP_HSTRING_IInspectable)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ekiter_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((EmptyKvpIterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE ekiter_rel(void *This) {
    EmptyKvpIterObj *o = (EmptyKvpIterObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE ekiter_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IIterator_KVP_HSTRING_IInspectable, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE ekiter_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1<Windows.Foundation.Collections.IKeyValuePair`2<String, Object>>", n);
}
static HRESULT STDMETHODCALLTYPE ekiter_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ekiter_cur(void *This, void **cur) {
    (void)This;
    if (cur) *cur = NULL;
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE ekiter_has(void *This, boolean *has) {
    (void)This;
    if (!has) return E_POINTER;
    *has = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ekiter_next(void *This, boolean *moved) {
    (void)This;
    if (!moved) return E_POINTER;
    *moved = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ekiter_many(void *This, uint32_t cap, void **items, uint32_t *actual) {
    (void)This; (void)cap; (void)items;
    if (!actual) return E_POINTER;
    *actual = 0;
    return S_OK;
}
static const void *g_ekiter_vtbl[] = {
    ekiter_qi, ekiter_ar, ekiter_rel, ekiter_iids, ekiter_rcn, ekiter_tl,
    ekiter_cur, ekiter_has, ekiter_next, ekiter_many
};

static HRESULT emv_qi_impl(EmptyMapViewObj *o, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IMapView_HSTRING_IInspectable)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtblView;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_KVP_HSTRING_IInspectable)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG emv_rel_impl(EmptyMapViewObj *o) {
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE emv_qi(void *This, REFIID r, void **p) {
    return emv_qi_impl(WINRT_SYS_FROM(EmptyMapViewObj, lpVtblView, This), r, p);
}
static ULONG STDMETHODCALLTYPE emv_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(EmptyMapViewObj, lpVtblView, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE emv_rel(void *This) {
    return emv_rel_impl(WINRT_SYS_FROM(EmptyMapViewObj, lpVtblView, This));
}
static HRESULT STDMETHODCALLTYPE emv_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IMapView_HSTRING_IInspectable, WINRT_IID_IIterable_KVP_HSTRING_IInspectable };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE emv_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IMapView`2<String, Object>", n);
}
static HRESULT STDMETHODCALLTYPE emv_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE emv_lookup(void *This, HSTRING key, void **val) {
    (void)This; (void)key;
    if (val) *val = NULL;
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE emv_size(void *This, uint32_t *size) {
    (void)This;
    if (!size) return E_POINTER;
    *size = 0u;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE emv_haskey(void *This, HSTRING key, boolean *found) {
    (void)This; (void)key;
    if (!found) return E_POINTER;
    *found = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE emv_split(void *This, void **first, void **second) {
    (void)This;
    if (first) *first = NULL;
    if (second) *second = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE emvi_qi(void *This, REFIID r, void **p) {
    return emv_qi_impl(WINRT_SYS_FROM(EmptyMapViewObj, lpVtblIter, This), r, p);
}
static ULONG STDMETHODCALLTYPE emvi_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(EmptyMapViewObj, lpVtblIter, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE emvi_rel(void *This) {
    return emv_rel_impl(WINRT_SYS_FROM(EmptyMapViewObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE emvi_first(void *This, void **iter) {
    (void)This;
    if (!iter) return E_POINTER;
    EmptyKvpIterObj *it = (EmptyKvpIterObj *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = (const void *)&g_ekiter_vtbl;
    it->ref_count = 1;
    *iter = it;
    return S_OK;
}
static const void *g_emv_view_vtbl[] = {
    emv_qi, emv_ar, emv_rel, emv_iids, emv_rcn, emv_tl,
    emv_lookup, emv_size, emv_haskey, emv_split
};
static const void *g_emv_iter_vtbl[] = {
    emvi_qi, emvi_ar, emvi_rel, emv_iids, emv_rcn, emv_tl, emvi_first
};

static HRESULT create_empty_map_view(void **out_mv) {
    EmptyMapViewObj *o = (EmptyMapViewObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return E_OUTOFMEMORY;
    o->lpVtblView = (const void *)&g_emv_view_vtbl;
    o->lpVtblIter = (const void *)&g_emv_iter_vtbl;
    o->ref_count = 1;
    *out_mv = &o->lpVtblView;
    return S_OK;
}

/* ============================================================================
 * 1. Windows.Devices.Enumeration.Pnp.PnpObject
 * ============================================================================ */

typedef struct WinrtPnpObjectObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Type)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, HSTRING *val);
        HRESULT (STDMETHODCALLTYPE *get_Properties)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *Update)(void *This, void *updateInfo);
    } *lpVtbl;
    volatile LONG ref_count;
    uint32_t type;
    HSTRING id;
} WinrtPnpObjectObj;

static HRESULT STDMETHODCALLTYPE pnp_obj_qi(void *This, REFIID riid, void **ppv) {
    WinrtPnpObjectObj *o = (WinrtPnpObjectObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPnpObject)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pnp_obj_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPnpObjectObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pnp_obj_rel(void *This) {
    WinrtPnpObjectObj *o = (WinrtPnpObjectObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) {
        if (o->id) WindowsDeleteString(o->id);
        winrt_sys_free(o);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pnp_obj_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IPnpObject, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE pnp_obj_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Devices.Enumeration.Pnp.PnpObject", n);
}
static HRESULT STDMETHODCALLTYPE pnp_obj_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_obj_type(void *This, uint32_t *val) {
    if (!val) return E_POINTER;
    *val = ((WinrtPnpObjectObj *)This)->type;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_obj_id(void *This, HSTRING *val) {
    return winrt_sys_dup_hstring(((WinrtPnpObjectObj *)This)->id, val);
}
static HRESULT STDMETHODCALLTYPE pnp_obj_props(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    return create_empty_map_view(val);
}
static HRESULT STDMETHODCALLTYPE pnp_obj_update(void *This, void *updateInfo) {
    (void)This; (void)updateInfo;
    return S_OK;
}
static const void *g_pnp_obj_vtbl[] = {
    pnp_obj_qi, pnp_obj_ar, pnp_obj_rel, pnp_obj_iids, pnp_obj_rcn, pnp_obj_tl,
    pnp_obj_type, pnp_obj_id, pnp_obj_props, pnp_obj_update
};

/* PnpObjectCollection (empty IVectorView<PnpObject*> + IIterable<PnpObject*>) */
typedef struct WinrtPnpIterObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **cur);
        HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *has);
        HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *moved);
        HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t cap, void **items, uint32_t *actual);
    } *lpVtbl;
    volatile LONG ref_count;
} WinrtPnpIterObj;

static HRESULT STDMETHODCALLTYPE pnp_iter_qi(void *This, REFIID riid, void **ppv) {
    WinrtPnpIterObj *o = (WinrtPnpIterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IIterator_PnpObject)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pnp_iter_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPnpIterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pnp_iter_rel(void *This) {
    WinrtPnpIterObj *o = (WinrtPnpIterObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pnp_iter_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IIterator_PnpObject, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE pnp_iter_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1<Windows.Devices.Enumeration.Pnp.PnpObject>", n);
}
static HRESULT STDMETHODCALLTYPE pnp_iter_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_iter_cur(void *This, void **cur) {
    (void)This;
    if (cur) *cur = NULL;
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE pnp_iter_has(void *This, boolean *has) {
    (void)This;
    if (!has) return E_POINTER;
    *has = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_iter_next(void *This, boolean *moved) {
    (void)This;
    if (!moved) return E_POINTER;
    *moved = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_iter_many(void *This, uint32_t cap, void **items, uint32_t *actual) {
    (void)This; (void)cap; (void)items;
    if (!actual) return E_POINTER;
    *actual = 0;
    return S_OK;
}
static const void *g_pnp_iter_vtbl[] = {
    pnp_iter_qi, pnp_iter_ar, pnp_iter_rel, pnp_iter_iids, pnp_iter_rcn, pnp_iter_tl,
    pnp_iter_cur, pnp_iter_has, pnp_iter_next, pnp_iter_many
};

typedef struct WinrtPnpCollObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t idx, void **val);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, void *elem, uint32_t *idx, boolean *found);
        HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t start, uint32_t cap, void **items, uint32_t *actual);
    } *lpVtblView;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *First)(void *This, void **iter);
    } *lpVtblIter;
    volatile LONG ref_count;
} WinrtPnpCollObj;

static HRESULT pnp_coll_qi_impl(WinrtPnpCollObj *o, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IVectorView_PnpObject)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtblView;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_PnpObject)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG pnp_coll_rel_impl(WinrtPnpCollObj *o) {
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pnp_cv_qi(void *This, REFIID r, void **p) {
    return pnp_coll_qi_impl(WINRT_SYS_FROM(WinrtPnpCollObj, lpVtblView, This), r, p);
}
static ULONG STDMETHODCALLTYPE pnp_cv_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtPnpCollObj, lpVtblView, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pnp_cv_rel(void *This) {
    return pnp_coll_rel_impl(WINRT_SYS_FROM(WinrtPnpCollObj, lpVtblView, This));
}
static HRESULT STDMETHODCALLTYPE pnp_cv_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IVectorView_PnpObject, WINRT_IID_IIterable_PnpObject };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE pnp_cv_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Devices.Enumeration.Pnp.PnpObjectCollection", n);
}
static HRESULT STDMETHODCALLTYPE pnp_cv_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_cv_getat(void *This, uint32_t idx, void **val) {
    (void)This; (void)idx;
    if (val) *val = NULL;
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE pnp_cv_size(void *This, uint32_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0u;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_cv_indexof(void *This, void *elem, uint32_t *idx, boolean *found) {
    (void)This; (void)elem;
    if (!idx || !found) return E_POINTER;
    *idx = 0u;
    *found = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_cv_getmany(void *This, uint32_t start, uint32_t cap, void **items, uint32_t *actual) {
    (void)This; (void)start; (void)cap; (void)items;
    if (!actual) return E_POINTER;
    *actual = 0u;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_ci_qi(void *This, REFIID r, void **p) {
    return pnp_coll_qi_impl(WINRT_SYS_FROM(WinrtPnpCollObj, lpVtblIter, This), r, p);
}
static ULONG STDMETHODCALLTYPE pnp_ci_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtPnpCollObj, lpVtblIter, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pnp_ci_rel(void *This) {
    return pnp_coll_rel_impl(WINRT_SYS_FROM(WinrtPnpCollObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE pnp_ci_first(void *This, void **iter) {
    (void)This;
    if (!iter) return E_POINTER;
    WinrtPnpIterObj *it = (WinrtPnpIterObj *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = (const void *)&g_pnp_iter_vtbl;
    it->ref_count = 1;
    *iter = it;
    return S_OK;
}
static const void *g_pnp_coll_view_vtbl[] = {
    pnp_cv_qi, pnp_cv_ar, pnp_cv_rel, pnp_cv_iids, pnp_cv_rcn, pnp_cv_tl,
    pnp_cv_getat, pnp_cv_size, pnp_cv_indexof, pnp_cv_getmany
};
static const void *g_pnp_coll_iter_vtbl[] = {
    pnp_ci_qi, pnp_ci_ar, pnp_ci_rel, pnp_cv_iids, pnp_cv_rcn, pnp_cv_tl,
    pnp_ci_first
};

static WinrtPnpCollObj *create_empty_pnp_collection(void) {
    WinrtPnpCollObj *c = (WinrtPnpCollObj *)winrt_sys_alloc(sizeof(*c));
    if (!c) return NULL;
    c->lpVtblView = (const void *)&g_pnp_coll_view_vtbl;
    c->lpVtblIter = (const void *)&g_pnp_coll_iter_vtbl;
    c->ref_count = 1;
    return c;
}

/* PnpObjectWatcher */
typedef struct WinrtPnpWatcherObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *add_Added)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_Added)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_Updated)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_Updated)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_Removed)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_Removed)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_EnumerationCompleted)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_EnumerationCompleted)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_Stopped)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_Stopped)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *get_Status)(void *This, uint32_t *status);
        HRESULT (STDMETHODCALLTYPE *Start)(void *This);
        HRESULT (STDMETHODCALLTYPE *Stop)(void *This);
    } *lpVtbl;
    volatile LONG ref_count;
    uint32_t status;
    WinrtEventHandlerPtrVtbl *enum_completed_handler;
    IUnknown *enum_completed_raw;
    int64_t enum_completed_tok;
} WinrtPnpWatcherObj;

static HRESULT STDMETHODCALLTYPE pnp_w_qi(void *This, REFIID riid, void **ppv) {
    WinrtPnpWatcherObj *o = (WinrtPnpWatcherObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPnpObjectWatcher)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pnp_w_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPnpWatcherObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pnp_w_rel(void *This) {
    WinrtPnpWatcherObj *o = (WinrtPnpWatcherObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) {
        if (o->enum_completed_raw) o->enum_completed_raw->lpVtbl->Release(o->enum_completed_raw);
        winrt_sys_free(o);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pnp_w_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IPnpObjectWatcher, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE pnp_w_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Devices.Enumeration.Pnp.PnpObjectWatcher", n);
}
static HRESULT STDMETHODCALLTYPE pnp_w_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_w_add_noop(void *This, void *h, EventRegistrationToken *tok) {
    (void)This;
    return dev_add_token(h, tok);
}
static HRESULT STDMETHODCALLTYPE pnp_w_rem_noop(void *This, EventRegistrationToken tok) {
    (void)This; (void)tok;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_w_add_ec(void *This, void *h, EventRegistrationToken *tok) {
    WinrtPnpWatcherObj *o = (WinrtPnpWatcherObj *)This;
    HRESULT hr = dev_add_token(h, tok);
    if (FAILED(hr)) return hr;
    if (o->enum_completed_raw) o->enum_completed_raw->lpVtbl->Release(o->enum_completed_raw);
    o->enum_completed_raw = (IUnknown *)h;
    o->enum_completed_raw->lpVtbl->AddRef(o->enum_completed_raw);
    o->enum_completed_tok = tok->value;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_w_rem_ec(void *This, EventRegistrationToken tok) {
    WinrtPnpWatcherObj *o = (WinrtPnpWatcherObj *)This;
    if (o->enum_completed_raw && o->enum_completed_tok == tok.value) {
        o->enum_completed_raw->lpVtbl->Release(o->enum_completed_raw);
        o->enum_completed_raw = NULL;
        o->enum_completed_tok = 0;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_w_status(void *This, uint32_t *status) {
    if (!status) return E_POINTER;
    *status = ((WinrtPnpWatcherObj *)This)->status;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_w_start(void *This) {
    WinrtPnpWatcherObj *o = (WinrtPnpWatcherObj *)This;
    o->status = 2u; /* EnumerationCompleted */
    if (o->enum_completed_raw) {
        typedef struct { const WinrtEventHandlerPtrVtbl *lpVtbl; } EvIf;
        EvIf *ev = (EvIf *)o->enum_completed_raw;
        ev->lpVtbl->Invoke(ev, (IInspectable *)o, NULL);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_w_stop(void *This) {
    ((WinrtPnpWatcherObj *)This)->status = 4u; /* Stopped */
    return S_OK;
}
static const void *g_pnp_watcher_vtbl[] = {
    pnp_w_qi, pnp_w_ar, pnp_w_rel, pnp_w_iids, pnp_w_rcn, pnp_w_tl,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_add_ec, pnp_w_rem_ec,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_status, pnp_w_start, pnp_w_stop
};

/* PnpObject Activation Factory */
typedef struct WinrtPnpObjectFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *CreateFromIdAsync)(void *This, uint32_t type, HSTRING id, void *reqProps, void **op);
        HRESULT (STDMETHODCALLTYPE *FindAllAsync)(void *This, uint32_t type, void *reqProps, void **op);
        HRESULT (STDMETHODCALLTYPE *FindAllAsyncAqsFilter)(void *This, uint32_t type, void *reqProps, HSTRING aqs, void **op);
        HRESULT (STDMETHODCALLTYPE *CreateWatcher)(void *This, uint32_t type, void *reqProps, void **watcher);
        HRESULT (STDMETHODCALLTYPE *CreateWatcherAqsFilter)(void *This, uint32_t type, void *reqProps, HSTRING aqs, void **watcher);
    } *lpVtblStatics;
} WinrtPnpObjectFactory;

static WinrtPnpObjectFactory g_pnp_factory;

static HRESULT STDMETHODCALLTYPE pnp_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_pnp_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPnpObjectStatics)) {
        *ppv = &g_pnp_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pnp_fac_ar(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE pnp_fac_rel(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE pnp_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IActivationFactory, WINRT_IID_IPnpObjectStatics };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE pnp_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE pnp_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("PnpObject::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE pnp_st_create_id(void *This, uint32_t type, HSTRING id, void *reqProps, void **op) {
    (void)This; (void)reqProps;
    if (!op) return E_POINTER;
    *op = NULL;
    WinrtPnpObjectObj *p = (WinrtPnpObjectObj *)winrt_sys_alloc(sizeof(*p));
    if (!p) return E_OUTOFMEMORY;
    p->lpVtbl = (const void *)&g_pnp_obj_vtbl;
    p->ref_count = 1;
    p->type = type;
    winrt_sys_dup_hstring(id, &p->id);
    HRESULT hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_PnpObject,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Devices.Enumeration.Pnp.PnpObject>",
        (IUnknown *)p, S_OK, op);
    pnp_obj_rel(p);
    return hr;
}
static HRESULT STDMETHODCALLTYPE pnp_st_findall(void *This, uint32_t type, void *reqProps, void **op) {
    (void)This; (void)type; (void)reqProps;
    if (!op) return E_POINTER;
    *op = NULL;
    WinrtPnpCollObj *c = create_empty_pnp_collection();
    if (!c) return E_OUTOFMEMORY;
    HRESULT hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_PnpObjectCollection,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Devices.Enumeration.Pnp.PnpObjectCollection>",
        (IUnknown *)&c->lpVtblView, S_OK, op);
    pnp_coll_rel_impl(c);
    return hr;
}
static HRESULT STDMETHODCALLTYPE pnp_st_findall_aqs(void *This, uint32_t type, void *reqProps, HSTRING aqs, void **op) {
    (void)aqs;
    return pnp_st_findall(This, type, reqProps, op);
}
static HRESULT STDMETHODCALLTYPE pnp_st_watcher(void *This, uint32_t type, void *reqProps, void **watcher) {
    (void)This; (void)type; (void)reqProps;
    if (!watcher) return E_POINTER;
    *watcher = NULL;
    WinrtPnpWatcherObj *w = (WinrtPnpWatcherObj *)winrt_sys_alloc(sizeof(*w));
    if (!w) return E_OUTOFMEMORY;
    w->lpVtbl = (const void *)&g_pnp_watcher_vtbl;
    w->ref_count = 1;
    w->status = 0u;
    *watcher = w;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pnp_st_watcher_aqs(void *This, uint32_t type, void *reqProps, HSTRING aqs, void **watcher) {
    (void)aqs;
    return pnp_st_watcher(This, type, reqProps, watcher);
}

static const WinrtActivationFactoryVtbl g_pnp_act_vtbl = {
    pnp_fac_qi, pnp_fac_ar, pnp_fac_rel, pnp_fac_iids, pnp_fac_rcn, pnp_fac_tl, pnp_fac_act
};
static const void *g_pnp_stat_vtbl[] = {
    pnp_fac_qi, pnp_fac_ar, pnp_fac_rel, pnp_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    pnp_st_create_id, pnp_st_findall, pnp_st_findall_aqs, pnp_st_watcher, pnp_st_watcher_aqs
};
static WinrtPnpObjectFactory g_pnp_factory = { &g_pnp_act_vtbl, (const void *)&g_pnp_stat_vtbl };

HRESULT winrt_sys_get_pnp_object_factory(REFIID iid, void **out_factory) {
    return pnp_fac_qi(&g_pnp_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 2. Windows.Devices.HumanInterfaceDevice.HidDevice
 * ============================================================================ */

typedef struct WinrtHidDeviceFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetDeviceSelector)(void *This, uint16_t usagePage, uint16_t usageId, HSTRING *sel);
        HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorVidPid)(void *This, uint16_t usagePage, uint16_t usageId, uint16_t vid, uint16_t pid, HSTRING *sel);
        HRESULT (STDMETHODCALLTYPE *FromIdAsync)(void *This, HSTRING deviceId, uint32_t accessMode, void **op);
    } *lpVtblStatics;
} WinrtHidDeviceFactory;

static WinrtHidDeviceFactory g_hid_factory;

static HRESULT STDMETHODCALLTYPE hid_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_hid_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IHidDeviceStatics)) {
        *ppv = &g_hid_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE hid_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IActivationFactory, WINRT_IID_IHidDeviceStatics };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE hid_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("HidDevice::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE hid_st_sel(void *This, uint16_t usagePage, uint16_t usageId, HSTRING *sel) {
    (void)This;
    wchar_t buf[256];
    swprintf(buf, 256u,
             L"System.Devices.InterfaceClassGuid:=\"{4D1E55B2-F16F-11CF-88CB-001111000030}\" AND "
             L"System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True AND "
             L"System.DeviceInterface.Hid.UsagePage:=%u AND System.DeviceInterface.Hid.UsageId:=%u",
             (unsigned)usagePage, (unsigned)usageId);
    buf[255] = L'\0';
    return winrt_sys_make_hstring(buf, sel);
}
static HRESULT STDMETHODCALLTYPE hid_st_sel_vp(void *This, uint16_t usagePage, uint16_t usageId,
                                               uint16_t vid, uint16_t pid, HSTRING *sel) {
    (void)This;
    wchar_t buf[320];
    swprintf(buf, 320u,
             L"System.Devices.InterfaceClassGuid:=\"{4D1E55B2-F16F-11CF-88CB-001111000030}\" AND "
             L"System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True AND "
             L"System.DeviceInterface.Hid.UsagePage:=%u AND System.DeviceInterface.Hid.UsageId:=%u AND "
             L"System.DeviceInterface.Hid.VendorId:=%u AND System.DeviceInterface.Hid.ProductId:=%u",
             (unsigned)usagePage, (unsigned)usageId, (unsigned)vid, (unsigned)pid);
    buf[319] = L'\0';
    return winrt_sys_make_hstring(buf, sel);
}
static HRESULT STDMETHODCALLTYPE hid_st_from_id(void *This, HSTRING deviceId, uint32_t accessMode, void **op) {
    (void)This; (void)deviceId; (void)accessMode;
    if (!op) return E_POINTER;
    *op = NULL;
    return winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_HidDevice,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Devices.HumanInterfaceDevice.HidDevice>",
        NULL, S_OK, op);
}
static const WinrtActivationFactoryVtbl g_hid_act_vtbl = {
    hid_fac_qi, pnp_fac_ar, pnp_fac_rel, hid_fac_iids, pnp_fac_rcn, pnp_fac_tl, hid_fac_act
};
static const void *g_hid_stat_vtbl[] = {
    hid_fac_qi, pnp_fac_ar, pnp_fac_rel, hid_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    hid_st_sel, hid_st_sel_vp, hid_st_from_id
};
static WinrtHidDeviceFactory g_hid_factory = { &g_hid_act_vtbl, (const void *)&g_hid_stat_vtbl };

HRESULT winrt_sys_get_hid_device_factory(REFIID iid, void **out_factory) {
    return hid_fac_qi(&g_hid_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 3. Windows.Devices.Input.{TouchCapabilities, MouseCapabilities, KeyboardCapabilities}
 * ============================================================================ */

typedef struct DevCapObj {
    const void *lpVtbl;
    volatile LONG ref_count;
    int kind; /* 0 = touch, 1 = mouse, 2 = keyboard */
} DevCapObj;

static HRESULT STDMETHODCALLTYPE dcap_qi(void *This, REFIID riid, void **ppv) {
    DevCapObj *o = (DevCapObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    const GUID *primary = (o->kind == 0) ? &WINRT_IID_ITouchCapabilities
                        : (o->kind == 1) ? &WINRT_IID_IMouseCapabilities
                                         : &WINRT_IID_IKeyboardCapabilities;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, primary)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE dcap_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((DevCapObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE dcap_rel(void *This) {
    DevCapObj *o = (DevCapObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE dcap_iids(void *This, ULONG *c, IID **i) {
    DevCapObj *o = (DevCapObj *)This;
    const GUID *primary = (o->kind == 0) ? &WINRT_IID_ITouchCapabilities
                        : (o->kind == 1) ? &WINRT_IID_IMouseCapabilities
                                         : &WINRT_IID_IKeyboardCapabilities;
    return winrt_sys_get_iids_copy(primary, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE dcap_rcn(void *This, HSTRING *n) {
    DevCapObj *o = (DevCapObj *)This;
    const wchar_t *name = (o->kind == 0) ? L"Windows.Devices.Input.TouchCapabilities"
                        : (o->kind == 1) ? L"Windows.Devices.Input.MouseCapabilities"
                                         : L"Windows.Devices.Input.KeyboardCapabilities";
    return winrt_sys_make_hstring(name, n);
}
static HRESULT STDMETHODCALLTYPE dcap_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE touch_present(void *This, int32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE touch_contacts(void *This, uint32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 0u; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mouse_present(void *This, int32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 1; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mouse_vwheel(void *This, int32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 1; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mouse_hwheel(void *This, int32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mouse_swap(void *This, int32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mouse_buttons(void *This, uint32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 3u; return S_OK;
}
static HRESULT STDMETHODCALLTYPE kbd_present(void *This, int32_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 1; return S_OK;
}

static const void *g_touch_vtbl[] = {
    dcap_qi, dcap_ar, dcap_rel, dcap_iids, dcap_rcn, dcap_tl, touch_present, touch_contacts
};
static const void *g_mouse_vtbl[] = {
    dcap_qi, dcap_ar, dcap_rel, dcap_iids, dcap_rcn, dcap_tl,
    mouse_present, mouse_vwheel, mouse_hwheel, mouse_swap, mouse_buttons
};
static const void *g_kbd_vtbl[] = {
    dcap_qi, dcap_ar, dcap_rel, dcap_iids, dcap_rcn, dcap_tl, kbd_present
};

static HRESULT STDMETHODCALLTYPE dcap_fac_qi(void *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = This;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE dcap_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IActivationFactory, 1, c, i);
}
static HRESULT dcap_activate(int kind, IInspectable **inst) {
    if (!inst) return E_POINTER;
    *inst = NULL;
    DevCapObj *o = (DevCapObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return E_OUTOFMEMORY;
    o->kind = kind;
    o->ref_count = 1;
    o->lpVtbl = (kind == 0) ? (const void *)&g_touch_vtbl
              : (kind == 1) ? (const void *)&g_mouse_vtbl
                            : (const void *)&g_kbd_vtbl;
    *inst = (IInspectable *)o;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE touch_fac_act(void *This, IInspectable **inst) { (void)This; return dcap_activate(0, inst); }
static HRESULT STDMETHODCALLTYPE mouse_fac_act(void *This, IInspectable **inst) { (void)This; return dcap_activate(1, inst); }
static HRESULT STDMETHODCALLTYPE kbd_fac_act(void *This, IInspectable **inst)   { (void)This; return dcap_activate(2, inst); }

static const WinrtActivationFactoryVtbl g_touch_fac_vtbl = {
    dcap_fac_qi, pnp_fac_ar, pnp_fac_rel, dcap_fac_iids, pnp_fac_rcn, pnp_fac_tl, touch_fac_act
};
static const WinrtActivationFactoryVtbl g_mouse_fac_vtbl = {
    dcap_fac_qi, pnp_fac_ar, pnp_fac_rel, dcap_fac_iids, pnp_fac_rcn, pnp_fac_tl, mouse_fac_act
};
static const WinrtActivationFactoryVtbl g_kbd_fac_vtbl = {
    dcap_fac_qi, pnp_fac_ar, pnp_fac_rel, dcap_fac_iids, pnp_fac_rcn, pnp_fac_tl, kbd_fac_act
};
static const WinrtActivationFactoryVtbl *g_touch_fac_obj = &g_touch_fac_vtbl;
static const WinrtActivationFactoryVtbl *g_mouse_fac_obj = &g_mouse_fac_vtbl;
static const WinrtActivationFactoryVtbl *g_kbd_fac_obj   = &g_kbd_fac_vtbl;

HRESULT winrt_sys_get_touch_capabilities_factory(REFIID iid, void **out_factory) {
    return dcap_fac_qi(&g_touch_fac_obj, iid, out_factory);
}
HRESULT winrt_sys_get_mouse_capabilities_factory(REFIID iid, void **out_factory) {
    return dcap_fac_qi(&g_mouse_fac_obj, iid, out_factory);
}
HRESULT winrt_sys_get_keyboard_capabilities_factory(REFIID iid, void **out_factory) {
    return dcap_fac_qi(&g_kbd_fac_obj, iid, out_factory);
}

/* ============================================================================
 * 4. Windows.System.Profile.HardwareIdentification
 * ============================================================================ */

typedef struct HardwareTokenObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, void **buf);
        HRESULT (STDMETHODCALLTYPE *get_Signature)(void *This, void **buf);
        HRESULT (STDMETHODCALLTYPE *get_Certificate)(void *This, void **buf);
    } *lpVtbl;
    volatile LONG ref_count;
} HardwareTokenObj;

static HRESULT STDMETHODCALLTYPE hwt_qi(void *This, REFIID riid, void **ppv) {
    HardwareTokenObj *o = (HardwareTokenObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IHardwareToken)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE hwt_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((HardwareTokenObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE hwt_rel(void *This) {
    HardwareTokenObj *o = (HardwareTokenObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE hwt_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IHardwareToken, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE hwt_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.System.Profile.HardwareToken", n);
}
static HRESULT STDMETHODCALLTYPE hwt_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hwt_get_id(void *This, void **buf) {
    (void)This;
    static const uint8_t k_id[16] = {
        0x01, 0x00, 0x04, 0x00, 0x53, 0x43, 0x41, 0x52,
        0x4C, 0x45, 0x54, 0x54, 0x50, 0x43, 0x30, 0x31
    };
    return winrt_sys_create_buffer(k_id, (uint32_t)sizeof(k_id), buf);
}
static HRESULT STDMETHODCALLTYPE hwt_get_sig(void *This, void **buf) {
    (void)This;
    static const uint8_t k_sig[32] = { 0x53, 0x49, 0x47, 0x31 };
    return winrt_sys_create_buffer(k_sig, (uint32_t)sizeof(k_sig), buf);
}
static HRESULT STDMETHODCALLTYPE hwt_get_cert(void *This, void **buf) {
    (void)This;
    static const uint8_t k_cert[32] = { 0x43, 0x45, 0x52, 0x54 };
    return winrt_sys_create_buffer(k_cert, (uint32_t)sizeof(k_cert), buf);
}
static const void *g_hwt_vtbl[] = {
    hwt_qi, hwt_ar, hwt_rel, hwt_iids, hwt_rcn, hwt_tl,
    hwt_get_id, hwt_get_sig, hwt_get_cert
};

typedef struct WinrtHardwareIdFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetPackageSpecificToken)(void *This, void *nonce, void **token);
    } *lpVtblStatics;
} WinrtHardwareIdFactory;

static WinrtHardwareIdFactory g_hwid_factory;

static HRESULT STDMETHODCALLTYPE hwid_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_hwid_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IHardwareIdentificationStatics)) {
        *ppv = &g_hwid_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE hwid_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IActivationFactory, WINRT_IID_IHardwareIdentificationStatics };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE hwid_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("HardwareIdentification::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE hwid_get_token(void *This, void *nonce, void **token) {
    (void)This; (void)nonce;
    if (!token) return E_POINTER;
    *token = NULL;
    HardwareTokenObj *t = (HardwareTokenObj *)winrt_sys_alloc(sizeof(*t));
    if (!t) return E_OUTOFMEMORY;
    t->lpVtbl = (const void *)&g_hwt_vtbl;
    t->ref_count = 1;
    *token = t;
    return S_OK;
}
static const WinrtActivationFactoryVtbl g_hwid_act_vtbl = {
    hwid_fac_qi, pnp_fac_ar, pnp_fac_rel, hwid_fac_iids, pnp_fac_rcn, pnp_fac_tl, hwid_fac_act
};
static const void *g_hwid_stat_vtbl[] = {
    hwid_fac_qi, pnp_fac_ar, pnp_fac_rel, hwid_fac_iids, pnp_fac_rcn, pnp_fac_tl, hwid_get_token
};
static WinrtHardwareIdFactory g_hwid_factory = { &g_hwid_act_vtbl, (const void *)&g_hwid_stat_vtbl };

HRESULT winrt_sys_get_hardware_identification_factory(REFIID iid, void **out_factory) {
    return hwid_fac_qi(&g_hwid_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 5. Windows.System.MemoryManager
 * ============================================================================ */

typedef struct MemReportObj {
    const void *lpVtbl;
    volatile LONG ref_count;
    int is_process;
} MemReportObj;

static HRESULT STDMETHODCALLTYPE mrep_qi(void *This, REFIID riid, void **ppv) {
    MemReportObj *o = (MemReportObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    const GUID *primary = o->is_process ? &WINRT_IID_IProcessMemoryReport : &WINRT_IID_IAppMemoryReport;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, primary)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE mrep_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((MemReportObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE mrep_rel(void *This) {
    MemReportObj *o = (MemReportObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE mrep_iids(void *This, ULONG *c, IID **i) {
    MemReportObj *o = (MemReportObj *)This;
    return winrt_sys_get_iids_copy(o->is_process ? &WINRT_IID_IProcessMemoryReport : &WINRT_IID_IAppMemoryReport, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE mrep_rcn(void *This, HSTRING *n) {
    MemReportObj *o = (MemReportObj *)This;
    return winrt_sys_make_hstring(o->is_process ? L"Windows.System.ProcessMemoryReport"
                                                : L"Windows.System.AppMemoryReport", n);
}
static HRESULT STDMETHODCALLTYPE mrep_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mrep_64m(void *This, uint64_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 64ULL * 1024ULL * 1024ULL; return S_OK;
}
static HRESULT STDMETHODCALLTYPE mrep_4g(void *This, uint64_t *v) {
    (void)This; if (!v) return E_POINTER; *v = 4ULL * 1024ULL * 1024ULL * 1024ULL; return S_OK;
}
static const void *g_app_mem_rep_vtbl[] = {
    mrep_qi, mrep_ar, mrep_rel, mrep_iids, mrep_rcn, mrep_tl,
    mrep_64m, mrep_64m, mrep_64m, mrep_4g
};
static const void *g_proc_mem_rep_vtbl[] = {
    mrep_qi, mrep_ar, mrep_rel, mrep_iids, mrep_rcn, mrep_tl,
    mrep_64m, mrep_64m
};

typedef struct WinrtMemoryManagerFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_AppMemoryUsage)(void *This, uint64_t *val);
        HRESULT (STDMETHODCALLTYPE *get_AppMemoryUsageLimit)(void *This, uint64_t *val);
        HRESULT (STDMETHODCALLTYPE *get_AppMemoryUsageLevel)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *add_AppMemoryUsageIncreased)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_AppMemoryUsageIncreased)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_AppMemoryUsageDecreased)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_AppMemoryUsageDecreased)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_AppMemoryUsageLimitChanging)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_AppMemoryUsageLimitChanging)(void *This, EventRegistrationToken tok);
    } *lpVtblStatics;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetAppMemoryReport)(void *This, void **rep);
        HRESULT (STDMETHODCALLTYPE *GetProcessMemoryReport)(void *This, void **rep);
    } *lpVtblStatics2;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *TrySetAppMemoryUsageLimit)(void *This, uint64_t val, boolean *ok);
    } *lpVtblStatics3;
} WinrtMemoryManagerFactory;

static WinrtMemoryManagerFactory g_mem_factory;

static HRESULT STDMETHODCALLTYPE mem_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_mem_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IMemoryManagerStatics)) {
        *ppv = &g_mem_factory.lpVtblStatics;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IMemoryManagerStatics2)) {
        *ppv = &g_mem_factory.lpVtblStatics2;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IMemoryManagerStatics3)) {
        *ppv = &g_mem_factory.lpVtblStatics3;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE mem_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[4] = {
        WINRT_IID_IActivationFactory,
        WINRT_IID_IMemoryManagerStatics,
        WINRT_IID_IMemoryManagerStatics2,
        WINRT_IID_IMemoryManagerStatics3
    };
    return winrt_sys_get_iids_copy(list, 4, c, i);
}
static HRESULT STDMETHODCALLTYPE mem_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("MemoryManager::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE mem_get_usage(void *This, uint64_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 64ULL * 1024ULL * 1024ULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mem_get_limit(void *This, uint64_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mem_get_level(void *This, uint32_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0u; /* AppMemoryUsageLevel_Low */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mem_get_app_rep(void *This, void **rep) {
    (void)This;
    if (!rep) return E_POINTER;
    MemReportObj *r = (MemReportObj *)winrt_sys_alloc(sizeof(*r));
    if (!r) return E_OUTOFMEMORY;
    r->lpVtbl = (const void *)&g_app_mem_rep_vtbl;
    r->ref_count = 1;
    r->is_process = 0;
    *rep = r;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mem_get_proc_rep(void *This, void **rep) {
    (void)This;
    if (!rep) return E_POINTER;
    MemReportObj *r = (MemReportObj *)winrt_sys_alloc(sizeof(*r));
    if (!r) return E_OUTOFMEMORY;
    r->lpVtbl = (const void *)&g_proc_mem_rep_vtbl;
    r->ref_count = 1;
    r->is_process = 1;
    *rep = r;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mem_try_set_limit(void *This, uint64_t val, boolean *ok) {
    (void)This; (void)val;
    if (!ok) return E_POINTER;
    *ok = 1;
    return S_OK;
}

static const WinrtActivationFactoryVtbl g_mem_act_vtbl = {
    mem_fac_qi, pnp_fac_ar, pnp_fac_rel, mem_fac_iids, pnp_fac_rcn, pnp_fac_tl, mem_fac_act
};
static const void *g_mem_stat_vtbl[] = {
    mem_fac_qi, pnp_fac_ar, pnp_fac_rel, mem_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    mem_get_usage, mem_get_limit, mem_get_level,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_add_noop, pnp_w_rem_noop
};
static const void *g_mem_stat2_vtbl[] = {
    mem_fac_qi, pnp_fac_ar, pnp_fac_rel, mem_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    mem_get_app_rep, mem_get_proc_rep
};
static const void *g_mem_stat3_vtbl[] = {
    mem_fac_qi, pnp_fac_ar, pnp_fac_rel, mem_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    mem_try_set_limit
};
static WinrtMemoryManagerFactory g_mem_factory = {
    &g_mem_act_vtbl,
    (const void *)&g_mem_stat_vtbl,
    (const void *)&g_mem_stat2_vtbl,
    (const void *)&g_mem_stat3_vtbl
};

HRESULT winrt_sys_get_memory_manager_factory(REFIID iid, void **out_factory) {
    return mem_fac_qi(&g_mem_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 6. Windows.System.Power.PowerManager
 * ============================================================================ */

typedef struct WinrtPowerManagerFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_EnergySaverStatus)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *add_EnergySaverStatusChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_EnergySaverStatusChanged)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *get_BatteryStatus)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *add_BatteryStatusChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_BatteryStatusChanged)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *get_PowerSupplyStatus)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *add_PowerSupplyStatusChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_PowerSupplyStatusChanged)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *get_RemainingChargePercent)(void *This, int32_t *val);
        HRESULT (STDMETHODCALLTYPE *add_RemainingChargePercentChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_RemainingChargePercentChanged)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *get_RemainingDischargeTime)(void *This, int64_t *val);
        HRESULT (STDMETHODCALLTYPE *add_RemainingDischargeTimeChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_RemainingDischargeTimeChanged)(void *This, EventRegistrationToken tok);
    } *lpVtblStatics;
} WinrtPowerManagerFactory;

static WinrtPowerManagerFactory g_pwr_factory;

static HRESULT STDMETHODCALLTYPE pwr_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_pwr_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPowerManagerStatics)) {
        *ppv = &g_pwr_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE pwr_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IActivationFactory, WINRT_IID_IPowerManagerStatics };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE pwr_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("PowerManager::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE pwr_energy_saver(void *This, uint32_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 0u; /* Disabled */ return S_OK;
}
static HRESULT STDMETHODCALLTYPE pwr_battery_status(void *This, uint32_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 0u; /* NotPresent */ return S_OK;
}
static HRESULT STDMETHODCALLTYPE pwr_supply_status(void *This, uint32_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 2u; /* Adequate */ return S_OK;
}
static HRESULT STDMETHODCALLTYPE pwr_charge_pct(void *This, int32_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 100; return S_OK;
}
static HRESULT STDMETHODCALLTYPE pwr_discharge_time(void *This, int64_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 0; return S_OK;
}

static const WinrtActivationFactoryVtbl g_pwr_act_vtbl = {
    pwr_fac_qi, pnp_fac_ar, pnp_fac_rel, pwr_fac_iids, pnp_fac_rcn, pnp_fac_tl, pwr_fac_act
};
static const void *g_pwr_stat_vtbl[] = {
    pwr_fac_qi, pnp_fac_ar, pnp_fac_rel, pwr_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    pwr_energy_saver, pnp_w_add_noop, pnp_w_rem_noop,
    pwr_battery_status, pnp_w_add_noop, pnp_w_rem_noop,
    pwr_supply_status, pnp_w_add_noop, pnp_w_rem_noop,
    pwr_charge_pct, pnp_w_add_noop, pnp_w_rem_noop,
    pwr_discharge_time, pnp_w_add_noop, pnp_w_rem_noop
};
static WinrtPowerManagerFactory g_pwr_factory = { &g_pwr_act_vtbl, (const void *)&g_pwr_stat_vtbl };

HRESULT winrt_sys_get_power_manager_factory(REFIID iid, void **out_factory) {
    return pwr_fac_qi(&g_pwr_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 7. Windows.Networking.HostName & Windows.Networking.Connectivity.NetworkInformation
 * ============================================================================ */

static const GUID WINRT_IID_IHostName =
    { 0xbf8ecaadu, 0xed96u, 0x49a7u, { 0x90u, 0x84u, 0xd4u, 0x16u, 0xcau, 0xe8u, 0x8du, 0xcbu } };
static const GUID WINRT_IID_IHostNameFactory =
    { 0x458c23edu, 0x712fu, 0x4576u, { 0xadu, 0xf1u, 0xc2u, 0x0bu, 0x2cu, 0x64u, 0x35u, 0x58u } };
static const GUID WINRT_IID_IHostNameStatics =
    { 0xf68cd4bfu, 0xa388u, 0x4e8bu, { 0x91u, 0xeau, 0x54u, 0xddu, 0x6du, 0xd9u, 0x01u, 0xc0u } };
static const GUID WINRT_IID_IVectorView_HostName =
    { 0xf4706ab1u, 0x55a3u, 0x5270u, { 0xafu, 0xb2u, 0x73u, 0x29u, 0x88u, 0xfeu, 0x82u, 0x27u } };
static const GUID WINRT_IID_IIterable_HostName =
    { 0x9e5f3ed0u, 0xcf1cu, 0x5d38u, { 0x83u, 0x2cu, 0xacu, 0xeau, 0x61u, 0x64u, 0xbfu, 0x5cu } };
static const GUID WINRT_IID_IIterator_HostName =
    { 0x557bf83cu, 0xa428u, 0x5dbdu, { 0xa0u, 0xfeu, 0x05u, 0xf6u, 0xeeu, 0x54u, 0x3du, 0x45u } };

static const GUID WINRT_IID_INetworkInformationStatics =
    { 0x5074f851u, 0x950du, 0x4165u, { 0x9cu, 0x15u, 0x36u, 0x56u, 0x19u, 0x48u, 0x1eu, 0xeau } };
static const GUID WINRT_IID_INetworkInformationStatics2 =
    { 0x459ced14u, 0x2832u, 0x49b6u, { 0xbau, 0x6eu, 0xe2u, 0x65u, 0xf0u, 0x47u, 0x86u, 0xa8u } };
static const GUID WINRT_IID_IConnectionProfile =
    { 0x71ba143cu, 0x598eu, 0x49d0u, { 0x84u, 0xebu, 0x8fu, 0xebu, 0xaeu, 0xdcu, 0xc1u, 0x95u } };
static const GUID WINRT_IID_IConnectionProfile2 =
    { 0xe2045145u, 0x4c9fu, 0x400cu, { 0x91u, 0x50u, 0x7eu, 0xc7u, 0xd6u, 0xe2u, 0x88u, 0x8au } };
static const GUID WINRT_IID_INetworkAdapter =
    { 0x3b542e03u, 0x5388u, 0x496cu, { 0xa8u, 0xa3u, 0xafu, 0xfdu, 0x39u, 0xaeu, 0xc2u, 0xe6u } };
static const GUID WINRT_IID_IVectorView_ConnectionProfile =
    { 0x6db1b492u, 0x3852u, 0x5df8u, { 0xa2u, 0x9du, 0x69u, 0x44u, 0x00u, 0x2fu, 0x58u, 0xd4u } };
static const GUID WINRT_IID_IIterable_ConnectionProfile =
    { 0x34dabef9u, 0x87d0u, 0x5b1cu, { 0xa7u, 0xacu, 0x9du, 0x29u, 0x0au, 0xdeu, 0xb0u, 0xc8u } };
static const GUID WINRT_IID_IIterator_ConnectionProfile =
    { 0x89913732u, 0xa08bu, 0x5cb2u, { 0xafu, 0x16u, 0xbbu, 0xbbu, 0x22u, 0x23u, 0x83u, 0x9eu } };
static const GUID WINRT_IID_IVectorView_HSTRING =
    { 0x2f13c006u, 0xa03au, 0x5f69u, { 0xb0u, 0x90u, 0x75u, 0xa4u, 0x3eu, 0x33u, 0x42u, 0x3eu } };
static const GUID WINRT_IID_IIterable_HSTRING =
    { 0xe2fcc7c1u, 0x3bfcu, 0x5a0bu, { 0xb2u, 0xb0u, 0x72u, 0xe7u, 0x69u, 0xd1u, 0xcbu, 0x7eu } };
static const GUID WINRT_IID_IIterator_HSTRING =
    { 0x8c304ebbu, 0x6615u, 0x50a4u, { 0x88u, 0x29u, 0x87u, 0x9eu, 0xcdu, 0x44u, 0x32u, 0x36u } };

/* HostName */
typedef struct WinrtHostNameObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_IPInformation)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *get_RawName)(void *This, HSTRING *val);
        HRESULT (STDMETHODCALLTYPE *get_DisplayName)(void *This, HSTRING *val);
        HRESULT (STDMETHODCALLTYPE *get_CanonicalName)(void *This, HSTRING *val);
        HRESULT (STDMETHODCALLTYPE *get_Type)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *IsEqual)(void *This, void *other, boolean *val);
    } *lpVtbl;
    volatile LONG ref_count;
    HSTRING name;
    uint32_t type;
} WinrtHostNameObj;

static HRESULT STDMETHODCALLTYPE hn_qi(void *This, REFIID riid, void **ppv) {
    WinrtHostNameObj *o = (WinrtHostNameObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IHostName)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE hn_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtHostNameObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE hn_rel(void *This) {
    WinrtHostNameObj *o = (WinrtHostNameObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) {
        if (o->name) WindowsDeleteString(o->name);
        winrt_sys_free(o);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE hn_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IHostName, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE hn_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Networking.HostName", n);
}
static HRESULT STDMETHODCALLTYPE hn_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE hn_ipinfo(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hn_name(void *This, HSTRING *val) {
    return winrt_sys_dup_hstring(((WinrtHostNameObj *)This)->name, val);
}
static HRESULT STDMETHODCALLTYPE hn_type(void *This, uint32_t *val) {
    if (!val) return E_POINTER;
    *val = ((WinrtHostNameObj *)This)->type;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hn_isequal(void *This, void *other, boolean *val) {
    WinrtHostNameObj *o = (WinrtHostNameObj *)This;
    if (!val) return E_POINTER;
    *val = 0;
    if (!other) return S_OK;
    WinrtHostNameObj *oh = NULL;
    if (SUCCEEDED(((IUnknown *)other)->lpVtbl->QueryInterface((IUnknown *)other, &WINRT_IID_IHostName, (void **)&oh)) && oh) {
        HSTRING other_name = NULL;
        if (SUCCEEDED(oh->lpVtbl->get_CanonicalName(oh, &other_name))) {
            *val = (winrt_sys_wcsicmp(winrt_sys_hstring_buf(o->name, NULL),
                                      winrt_sys_hstring_buf(other_name, NULL)) == 0) ? 1 : 0;
            WindowsDeleteString(other_name);
        }
        oh->lpVtbl->Release(oh);
    }
    return S_OK;
}
static const void *g_hn_vtbl[] = {
    hn_qi, hn_ar, hn_rel, hn_iids, hn_rcn, hn_tl,
    hn_ipinfo, hn_name, hn_name, hn_name, hn_type, hn_isequal
};

static WinrtHostNameObj *create_host_name_obj(const wchar_t *name) {
    WinrtHostNameObj *o = (WinrtHostNameObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return NULL;
    o->lpVtbl = (const void *)&g_hn_vtbl;
    o->ref_count = 1;
    winrt_sys_make_hstring(name ? name : L"localhost", &o->name);
    o->type = 0u; /* HostNameType_DomainName */
    return o;
}

/* HostName Factory */
typedef struct WinrtHostNameFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *CreateHostName)(void *This, HSTRING hostName, void **value);
    } *lpVtblCtor;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *Compare)(void *This, HSTRING value1, HSTRING value2, int32_t *result);
    } *lpVtblStatics;
} WinrtHostNameFactory;

static WinrtHostNameFactory g_hn_factory;

static HRESULT STDMETHODCALLTYPE hnf_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_hn_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IHostNameFactory)) {
        *ppv = &g_hn_factory.lpVtblCtor;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IHostNameStatics)) {
        *ppv = &g_hn_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE hnf_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[3] = { WINRT_IID_IActivationFactory, WINRT_IID_IHostNameFactory, WINRT_IID_IHostNameStatics };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE hnf_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("HostName::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE hnf_create(void *This, HSTRING hostName, void **value) {
    (void)This;
    if (!value) return E_POINTER;
    *value = NULL;
    const wchar_t *s = winrt_sys_hstring_buf(hostName, NULL);
    if (!s || s[0] == L'\0') return E_INVALIDARG;
    WinrtHostNameObj *o = create_host_name_obj(s);
    if (!o) return E_OUTOFMEMORY;
    *value = o;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hnf_compare(void *This, HSTRING v1, HSTRING v2, int32_t *res) {
    (void)This;
    if (!res) return E_POINTER;
    *res = winrt_sys_wcsicmp(winrt_sys_hstring_buf(v1, NULL), winrt_sys_hstring_buf(v2, NULL));
    return S_OK;
}
static const WinrtActivationFactoryVtbl g_hnf_act_vtbl = {
    hnf_qi, pnp_fac_ar, pnp_fac_rel, hnf_iids, pnp_fac_rcn, pnp_fac_tl, hnf_act
};
static const void *g_hnf_ctor_vtbl[] = {
    hnf_qi, pnp_fac_ar, pnp_fac_rel, hnf_iids, pnp_fac_rcn, pnp_fac_tl, hnf_create
};
static const void *g_hnf_stat_vtbl[] = {
    hnf_qi, pnp_fac_ar, pnp_fac_rel, hnf_iids, pnp_fac_rcn, pnp_fac_tl, hnf_compare
};
static WinrtHostNameFactory g_hn_factory = {
    &g_hnf_act_vtbl, (const void *)&g_hnf_ctor_vtbl, (const void *)&g_hnf_stat_vtbl
};

HRESULT winrt_sys_get_host_name_factory(REFIID iid, void **out_factory) {
    return hnf_qi(&g_hn_factory.lpVtblAct, iid, out_factory);
}

/* Generic Read-Only IVectorView<T*> + IIterable<T*> + IIterator<T*> for Networking lists */
typedef struct NetVecObj WinrtNetVecObj;
typedef struct NetVecIterObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **cur);
        HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *has);
        HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *moved);
        HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t cap, void **items, uint32_t *actual);
    } *lpVtbl;
    volatile LONG ref_count;
    WinrtNetVecObj *vec;
    uint32_t idx;
} NetVecIterObj;

struct NetVecObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t idx, void **val);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, void *elem, uint32_t *idx, boolean *found);
        HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t start, uint32_t cap, void **items, uint32_t *actual);
    } *lpVtblView;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *First)(void *This, void **iter);
    } *lpVtblIter;
    volatile LONG ref_count;
    GUID view_iid;
    GUID iter_iid;
    GUID it_iid;
    const wchar_t *rcn;
    IUnknown *item;
};

static ULONG nv_rel_impl(WinrtNetVecObj *v) {
    LONG rc = InterlockedDecrement(&v->ref_count);
    if (rc == 0) {
        if (v->item) v->item->lpVtbl->Release(v->item);
        winrt_sys_free(v);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE nvi_qi(void *This, REFIID riid, void **ppv) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &it->vec->it_iid)) {
        InterlockedIncrement(&it->ref_count);
        *ppv = it;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE nvi_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((NetVecIterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE nvi_rel(void *This) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    LONG rc = InterlockedDecrement(&it->ref_count);
    if (rc == 0) {
        nv_rel_impl(it->vec);
        winrt_sys_free(it);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE nvi_iids(void *This, ULONG *c, IID **i) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    return winrt_sys_get_iids_copy(&it->vec->it_iid, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE nvi_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1", n);
}
static HRESULT STDMETHODCALLTYPE nvi_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE nvi_cur(void *This, void **cur) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    if (!cur) return E_POINTER;
    *cur = NULL;
    if (!it->vec->item || it->idx > 0u) return E_BOUNDS;
    *cur = it->vec->item;
    it->vec->item->lpVtbl->AddRef(it->vec->item);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nvi_has(void *This, boolean *has) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    if (!has) return E_POINTER;
    *has = (it->vec->item && it->idx == 0u) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nvi_next(void *This, boolean *moved) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    if (!moved) return E_POINTER;
    if (it->vec->item && it->idx == 0u) it->idx = 1u;
    *moved = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nvi_many(void *This, uint32_t cap, void **items, uint32_t *actual) {
    NetVecIterObj *it = (NetVecIterObj *)This;
    if (!actual) return E_POINTER;
    *actual = 0u;
    if (cap > 0u && items && it->vec->item && it->idx == 0u) {
        items[0] = it->vec->item;
        it->vec->item->lpVtbl->AddRef(it->vec->item);
        it->idx = 1u;
        *actual = 1u;
    }
    return S_OK;
}
static const void *g_nvi_vtbl[] = {
    nvi_qi, nvi_ar, nvi_rel, nvi_iids, nvi_rcn, nvi_tl,
    nvi_cur, nvi_has, nvi_next, nvi_many
};

static HRESULT nv_qi_impl(WinrtNetVecObj *v, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &v->view_iid)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblView;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &v->iter_iid)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE nv_qi(void *This, REFIID r, void **p) {
    return nv_qi_impl(WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This), r, p);
}
static ULONG STDMETHODCALLTYPE nv_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE nv_rel(void *This) {
    return nv_rel_impl(WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This));
}
static HRESULT STDMETHODCALLTYPE nv_iids(void *This, ULONG *c, IID **i) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This);
    const GUID list[2] = { v->view_iid, v->iter_iid };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE nv_rcn(void *This, HSTRING *n) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This);
    return winrt_sys_make_hstring(v->rcn, n);
}
static HRESULT STDMETHODCALLTYPE nv_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE nv_getat(void *This, uint32_t idx, void **val) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This);
    if (!val) return E_POINTER;
    *val = NULL;
    if (!v->item || idx != 0u) return E_BOUNDS;
    *val = v->item;
    v->item->lpVtbl->AddRef(v->item);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nv_size(void *This, uint32_t *val) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This);
    if (!val) return E_POINTER;
    *val = v->item ? 1u : 0u;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nv_indexof(void *This, void *elem, uint32_t *idx, boolean *found) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This);
    if (!idx || !found) return E_POINTER;
    *idx = 0u;
    *found = (v->item && (void *)v->item == elem) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nv_getmany(void *This, uint32_t start, uint32_t cap, void **items, uint32_t *actual) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblView, This);
    if (!actual) return E_POINTER;
    *actual = 0u;
    if (start == 0u && cap > 0u && items && v->item) {
        items[0] = v->item;
        v->item->lpVtbl->AddRef(v->item);
        *actual = 1u;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE nvr_qi(void *This, REFIID r, void **p) {
    return nv_qi_impl(WINRT_SYS_FROM(WinrtNetVecObj, lpVtblIter, This), r, p);
}
static ULONG STDMETHODCALLTYPE nvr_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtNetVecObj, lpVtblIter, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE nvr_rel(void *This) {
    return nv_rel_impl(WINRT_SYS_FROM(WinrtNetVecObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE nvr_iids(void *This, ULONG *c, IID **i) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblIter, This);
    const GUID list[2] = { v->view_iid, v->iter_iid };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE nvr_rcn(void *This, HSTRING *n) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblIter, This);
    return winrt_sys_make_hstring(v->rcn, n);
}
static HRESULT STDMETHODCALLTYPE nvr_first(void *This, void **iter) {
    WinrtNetVecObj *v = WINRT_SYS_FROM(WinrtNetVecObj, lpVtblIter, This);
    if (!iter) return E_POINTER;
    *iter = NULL;
    NetVecIterObj *it = (NetVecIterObj *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = (const void *)&g_nvi_vtbl;
    it->ref_count = 1;
    it->vec = v;
    InterlockedIncrement(&v->ref_count);
    it->idx = 0u;
    *iter = it;
    return S_OK;
}
static const void *g_nv_view_vtbl[] = {
    nv_qi, nv_ar, nv_rel, nv_iids, nv_rcn, nv_tl,
    nv_getat, nv_size, nv_indexof, nv_getmany
};
static const void *g_nv_iter_vtbl[] = {
    nvr_qi, nvr_ar, nvr_rel, nvr_iids, nvr_rcn, nv_tl, nvr_first
};

static WinrtNetVecObj *create_net_vec(const GUID *view_iid, const GUID *iter_iid, const GUID *it_iid,
                                      const wchar_t *rcn, IUnknown *item) {
    WinrtNetVecObj *v = (WinrtNetVecObj *)winrt_sys_alloc(sizeof(*v));
    if (!v) return NULL;
    v->lpVtblView = (const void *)&g_nv_view_vtbl;
    v->lpVtblIter = (const void *)&g_nv_iter_vtbl;
    v->ref_count = 1;
    v->view_iid = *view_iid;
    v->iter_iid = *iter_iid;
    v->it_iid = *it_iid;
    v->rcn = rcn;
    v->item = item;
    if (item) item->lpVtbl->AddRef(item);
    return v;
}

/* NetworkAdapter & ConnectionProfile */
typedef struct WinrtNetAdapterObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_OutboundMaxBitsPerSecond)(void *This, uint64_t *val);
        HRESULT (STDMETHODCALLTYPE *get_InboundMaxBitsPerSecond)(void *This, uint64_t *val);
        HRESULT (STDMETHODCALLTYPE *get_IanaInterfaceType)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *get_NetworkItem)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *get_NetworkAdapterId)(void *This, GUID *val);
        HRESULT (STDMETHODCALLTYPE *GetConnectedProfileAsync)(void *This, void **op);
    } *lpVtbl;
    volatile LONG ref_count;
} WinrtNetAdapterObj;

static HRESULT STDMETHODCALLTYPE na_qi(void *This, REFIID riid, void **ppv) {
    WinrtNetAdapterObj *o = (WinrtNetAdapterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_INetworkAdapter)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE na_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtNetAdapterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE na_rel(void *This) {
    WinrtNetAdapterObj *o = (WinrtNetAdapterObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE na_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_INetworkAdapter, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE na_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Networking.Connectivity.NetworkAdapter", n);
}
static HRESULT STDMETHODCALLTYPE na_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE na_speed(void *This, uint64_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 1000000000ULL; return S_OK;
}
static HRESULT STDMETHODCALLTYPE na_iana(void *This, uint32_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 6u; /* Ethernet */ return S_OK;
}
static HRESULT STDMETHODCALLTYPE na_item(void *This, void **val) {
    (void)This; if (!val) return E_POINTER; *val = NULL; return S_OK;
}
static HRESULT STDMETHODCALLTYPE na_id(void *This, GUID *val) {
    (void)This;
    if (!val) return E_POINTER;
    static const GUID k_id = { 0x11223344u, 0x5566u, 0x7788u, { 0x99u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu, 0x01u } };
    *val = k_id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE na_profile(void *This, void **op) {
    if (op) *op = NULL;
    return winrt_sys_unimplemented("INetworkAdapter::GetConnectedProfileAsync", This);
}
static const void *g_na_vtbl[] = {
    na_qi, na_ar, na_rel, na_iids, na_rcn, na_tl,
    na_speed, na_speed, na_iana, na_item, na_id, na_profile
};

typedef struct WinrtConnProfileObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_ProfileName)(void *This, HSTRING *val);
        HRESULT (STDMETHODCALLTYPE *GetNetworkConnectivityLevel)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *GetNetworkNames)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetConnectionCost)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetDataPlanStatus)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *get_NetworkAdapter)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetLocalUsage1)(void *This, int64_t s, int64_t e, void **val);
        HRESULT (STDMETHODCALLTYPE *GetLocalUsage2)(void *This, int64_t s, int64_t e, uint32_t states, void **val);
        HRESULT (STDMETHODCALLTYPE *get_NetworkSecuritySettings)(void *This, void **val);
    } *lpVtbl;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_IsWwanConnectionProfile)(void *This, boolean *val);
        HRESULT (STDMETHODCALLTYPE *get_IsWlanConnectionProfile)(void *This, boolean *val);
        HRESULT (STDMETHODCALLTYPE *get_WwanConnectionProfileDetails)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *get_WlanConnectionProfileDetails)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *get_ServiceProviderGuid)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetSignalBars)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetDomainConnectivityLevel)(void *This, uint32_t *val);
        HRESULT (STDMETHODCALLTYPE *GetNetworkUsageAsync)(void *This, int64_t s, int64_t e, uint32_t g, void *st, void **op);
        HRESULT (STDMETHODCALLTYPE *GetConnectivityIntervalsAsync)(void *This, int64_t s, int64_t e, void *st, void **op);
    } *lpVtbl2;
    volatile LONG ref_count;
} WinrtConnProfileObj;

static HRESULT cp_qi_impl(WinrtConnProfileObj *o, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IConnectionProfile)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IConnectionProfile2)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtbl2;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG cp_rel_impl(WinrtConnProfileObj *o) {
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) winrt_sys_free(o);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE cp_qi(void *This, REFIID r, void **p) {
    return cp_qi_impl(WINRT_SYS_FROM(WinrtConnProfileObj, lpVtbl, This), r, p);
}
static ULONG STDMETHODCALLTYPE cp_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtConnProfileObj, lpVtbl, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE cp_rel(void *This) {
    return cp_rel_impl(WINRT_SYS_FROM(WinrtConnProfileObj, lpVtbl, This));
}
static HRESULT STDMETHODCALLTYPE cp_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IConnectionProfile, WINRT_IID_IConnectionProfile2 };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE cp_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Networking.Connectivity.ConnectionProfile", n);
}
static HRESULT STDMETHODCALLTYPE cp_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_pname(void *This, HSTRING *val) {
    (void)This;
    return winrt_sys_make_hstring(L"Ethernet", val);
}
static HRESULT STDMETHODCALLTYPE cp_level(void *This, uint32_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 3u; /* NetworkConnectivityLevel_InternetAccess */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_names(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtNetVecObj *v = create_net_vec(
        &WINRT_IID_IVectorView_HSTRING, &WINRT_IID_IIterable_HSTRING, &WINRT_IID_IIterator_HSTRING,
        L"Windows.Foundation.Collections.IVectorView`1<String>", NULL);
    if (!v) return E_OUTOFMEMORY;
    *val = &v->lpVtblView;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_null_prop(void *This, void **val) {
    (void)This; if (!val) return E_POINTER; *val = NULL; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_adapter(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtNetAdapterObj *a = (WinrtNetAdapterObj *)winrt_sys_alloc(sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    a->lpVtbl = (const void *)&g_na_vtbl;
    a->ref_count = 1;
    *val = a;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp_usage1(void *This, int64_t s, int64_t e, void **val) {
    (void)s; (void)e;
    if (val) *val = NULL;
    return winrt_sys_unimplemented("IConnectionProfile::GetLocalUsage", This);
}
static HRESULT STDMETHODCALLTYPE cp_usage2(void *This, int64_t s, int64_t e, uint32_t st, void **val) {
    (void)s; (void)e; (void)st;
    if (val) *val = NULL;
    return winrt_sys_unimplemented("IConnectionProfile::GetLocalUsage2", This);
}
static HRESULT STDMETHODCALLTYPE cp2_qi(void *This, REFIID r, void **p) {
    return cp_qi_impl(WINRT_SYS_FROM(WinrtConnProfileObj, lpVtbl2, This), r, p);
}
static ULONG STDMETHODCALLTYPE cp2_ar(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtConnProfileObj, lpVtbl2, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE cp2_rel(void *This) {
    return cp_rel_impl(WINRT_SYS_FROM(WinrtConnProfileObj, lpVtbl2, This));
}
static HRESULT STDMETHODCALLTYPE cp2_false(void *This, boolean *val) {
    (void)This; if (!val) return E_POINTER; *val = 0; return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp2_domain(void *This, uint32_t *val) {
    (void)This; if (!val) return E_POINTER; *val = 0u; /* DomainConnectivityLevel_None */ return S_OK;
}
static HRESULT STDMETHODCALLTYPE cp2_netusage(void *This, int64_t s, int64_t e, uint32_t g, void *st, void **op) {
    (void)s; (void)e; (void)g; (void)st;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IConnectionProfile2::GetNetworkUsageAsync", This);
}
static HRESULT STDMETHODCALLTYPE cp2_connint(void *This, int64_t s, int64_t e, void *st, void **op) {
    (void)s; (void)e; (void)st;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IConnectionProfile2::GetConnectivityIntervalsAsync", This);
}
static const void *g_cp_vtbl[] = {
    cp_qi, cp_ar, cp_rel, cp_iids, cp_rcn, cp_tl,
    cp_pname, cp_level, cp_names, cp_null_prop, cp_null_prop,
    cp_adapter, cp_usage1, cp_usage2, cp_null_prop
};
static const void *g_cp2_vtbl[] = {
    cp2_qi, cp2_ar, cp2_rel, cp_iids, cp_rcn, cp_tl,
    cp2_false, cp2_false, cp_null_prop, cp_null_prop, cp_null_prop, cp_null_prop,
    cp2_domain, cp2_netusage, cp2_connint
};

static WinrtConnProfileObj *create_conn_profile(void) {
    WinrtConnProfileObj *o = (WinrtConnProfileObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return NULL;
    o->lpVtbl = (const void *)&g_cp_vtbl;
    o->lpVtbl2 = (const void *)&g_cp2_vtbl;
    o->ref_count = 1;
    return o;
}

/* NetworkInformation Factory */
typedef struct WinrtNetworkInfoFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetConnectionProfiles)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetInternetConnectionProfile)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetLanIdentifiers)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetHostNames)(void *This, void **val);
        HRESULT (STDMETHODCALLTYPE *GetProxyConfigurationAsync)(void *This, void *uri, void **op);
        HRESULT (STDMETHODCALLTYPE *GetSortedEndpointPairs)(void *This, void *ep, uint32_t opts, void **val);
        HRESULT (STDMETHODCALLTYPE *add_NetworkStatusChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_NetworkStatusChanged)(void *This, EventRegistrationToken tok);
    } *lpVtblStatics;
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *FindConnectionProfilesAsync)(void *This, void *filter, void **op);
    } *lpVtblStatics2;
} WinrtNetworkInfoFactory;

static WinrtNetworkInfoFactory g_net_factory;

static HRESULT STDMETHODCALLTYPE net_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_net_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_INetworkInformationStatics)) {
        *ppv = &g_net_factory.lpVtblStatics;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_INetworkInformationStatics2)) {
        *ppv = &g_net_factory.lpVtblStatics2;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE net_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[3] = {
        WINRT_IID_IActivationFactory,
        WINRT_IID_INetworkInformationStatics,
        WINRT_IID_INetworkInformationStatics2
    };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE net_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("NetworkInformation::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE net_get_profiles(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    WinrtConnProfileObj *cp = create_conn_profile();
    if (!cp) return E_OUTOFMEMORY;
    WinrtNetVecObj *v = create_net_vec(
        &WINRT_IID_IVectorView_ConnectionProfile,
        &WINRT_IID_IIterable_ConnectionProfile,
        &WINRT_IID_IIterator_ConnectionProfile,
        L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.Connectivity.ConnectionProfile>",
        (IUnknown *)&cp->lpVtbl);
    cp_rel_impl(cp);
    if (!v) return E_OUTOFMEMORY;
    *val = &v->lpVtblView;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_get_inet_profile(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    WinrtConnProfileObj *cp = create_conn_profile();
    if (!cp) return E_OUTOFMEMORY;
    *val = &cp->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_get_lan(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    static const GUID k_lan_vv = { 0xd067d814u, 0x0f02u, 0x5f4au, { 0x89u, 0xd8u, 0xf2u, 0x3bu, 0xd3u, 0x8du, 0x45u, 0x05u } };
    static const GUID k_lan_it = { 0x5c386545u, 0x058au, 0x50f1u, { 0xb5u, 0xc0u, 0x8au, 0x82u, 0x62u, 0x4cu, 0x0eu, 0x9du } };
    static const GUID k_lan_ir = { 0x6245102cu, 0x0934u, 0x506bu, { 0xa3u, 0x16u, 0xc3u, 0x67u, 0x2eu, 0x84u, 0xc6u, 0x43u } };
    WinrtNetVecObj *v = create_net_vec(&k_lan_vv, &k_lan_it, &k_lan_ir,
                                       L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.Connectivity.LanIdentifier>", NULL);
    if (!v) return E_OUTOFMEMORY;
    *val = &v->lpVtblView;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_get_hostnames(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    WinrtHostNameObj *hn = create_host_name_obj(L"localhost");
    if (!hn) return E_OUTOFMEMORY;
    WinrtNetVecObj *v = create_net_vec(
        &WINRT_IID_IVectorView_HostName,
        &WINRT_IID_IIterable_HostName,
        &WINRT_IID_IIterator_HostName,
        L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.HostName>",
        (IUnknown *)hn);
    hn_rel(hn);
    if (!v) return E_OUTOFMEMORY;
    *val = &v->lpVtblView;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_get_proxy(void *This, void *uri, void **op) {
    (void)uri;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("INetworkInformationStatics::GetProxyConfigurationAsync", This);
}
static HRESULT STDMETHODCALLTYPE net_get_ep(void *This, void *ep, uint32_t opts, void **val) {
    (void)This; (void)ep; (void)opts;
    if (!val) return E_POINTER;
    static const GUID k_ep_vv = { 0xd7ec83c4u, 0xa17bu, 0x51bfu, { 0x89u, 0x97u, 0xaau, 0x33u, 0xb9u, 0x10u, 0x2du, 0xc9u } };
    static const GUID k_ep_it = { 0xc899ff9fu, 0x26f5u, 0x5b64u, { 0xb5u, 0x8bu, 0xfbu, 0xc7u, 0xa2u, 0x87u, 0x2eu, 0xf2u } };
    static const GUID k_ep_ir = { 0xb1b4b289u, 0xa206u, 0x5c36u, { 0x8eu, 0x3bu, 0x02u, 0xb9u, 0x09u, 0x7cu, 0xdau, 0x41u } };
    WinrtNetVecObj *v = create_net_vec(&k_ep_vv, &k_ep_it, &k_ep_ir,
                                       L"Windows.Foundation.Collections.IVectorView`1<Windows.Networking.EndpointPair>", NULL);
    if (!v) return E_OUTOFMEMORY;
    *val = &v->lpVtblView;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE net_find_profiles(void *This, void *filter, void **op) {
    (void)filter;
    if (op) *op = NULL;
    void *profs = NULL;
    HRESULT hr = net_get_profiles(This, &profs);
    static const GUID k_aop_cp = { 0xbc88635bu, 0x052bu, 0x56f5u, { 0x93u, 0xfau, 0xc7u, 0x6cu, 0x3au, 0x51u, 0x6au, 0x0eu } };
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &k_aop_cp,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Networking.Connectivity.ConnectionProfile>>",
        (IUnknown *)profs, hr, op);
    if (profs) ((IUnknown *)profs)->lpVtbl->Release((IUnknown *)profs);
    return out_hr;
}
static const WinrtActivationFactoryVtbl g_net_act_vtbl = {
    net_fac_qi, pnp_fac_ar, pnp_fac_rel, net_fac_iids, pnp_fac_rcn, pnp_fac_tl, net_fac_act
};
static const void *g_net_stat_vtbl[] = {
    net_fac_qi, pnp_fac_ar, pnp_fac_rel, net_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    net_get_profiles, net_get_inet_profile, net_get_lan, net_get_hostnames,
    net_get_proxy, net_get_ep, pnp_w_add_noop, pnp_w_rem_noop
};
static const void *g_net_stat2_vtbl[] = {
    net_fac_qi, pnp_fac_ar, pnp_fac_rel, net_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    net_find_profiles
};
static WinrtNetworkInfoFactory g_net_factory = {
    &g_net_act_vtbl, (const void *)&g_net_stat_vtbl, (const void *)&g_net_stat2_vtbl
};

HRESULT winrt_sys_get_network_information_factory(REFIID iid, void **out_factory) {
    return net_fac_qi(&g_net_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 8. Windows.Media.Devices.MediaDevice
 * ============================================================================ */

static const GUID WINRT_IID_IMediaDeviceStatics =
    { 0xaa2d9a40u, 0x909fu, 0x4bbau, { 0xbfu, 0x8bu, 0x0cu, 0x0du, 0x29u, 0x6fu, 0x14u, 0xf0u } };

typedef struct WinrtMediaDeviceStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
    HRESULT (STDMETHODCALLTYPE *GetAudioCaptureSelector)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetAudioRenderSelector)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetVideoCaptureSelector)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetDefaultAudioCaptureId)(void *This, uint32_t role, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *GetDefaultAudioRenderId)(void *This, uint32_t role, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *add_DefaultAudioCaptureDeviceChanged)(void *This, void *h, EventRegistrationToken *tok);
    HRESULT (STDMETHODCALLTYPE *remove_DefaultAudioCaptureDeviceChanged)(void *This, EventRegistrationToken tok);
    HRESULT (STDMETHODCALLTYPE *add_DefaultAudioRenderDeviceChanged)(void *This, void *h, EventRegistrationToken *tok);
    HRESULT (STDMETHODCALLTYPE *remove_DefaultAudioRenderDeviceChanged)(void *This, EventRegistrationToken tok);
} WinrtMediaDeviceStaticsVtbl;

typedef struct WinrtMediaDeviceFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtMediaDeviceStaticsVtbl *lpVtblStatics;
} WinrtMediaDeviceFactory;

static WinrtMediaDeviceFactory g_md_factory;

static HRESULT STDMETHODCALLTYPE md_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_md_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IMediaDeviceStatics)) {
        *ppv = &g_md_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE md_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IActivationFactory, WINRT_IID_IMediaDeviceStatics };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE md_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("MediaDevice::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE md_audio_cap_sel(void *This, HSTRING *value) {
    (void)This;
    return winrt_sys_make_hstring(
        L"System.Devices.InterfaceClassGuid:=\"{2EEF81BE-33FA-4800-9670-1CD474972C3F}\" "
        L"AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True",
        value);
}
static HRESULT STDMETHODCALLTYPE md_audio_ren_sel(void *This, HSTRING *value) {
    (void)This;
    return winrt_sys_make_hstring(
        L"System.Devices.InterfaceClassGuid:=\"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}\" "
        L"AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True",
        value);
}
static HRESULT STDMETHODCALLTYPE md_video_cap_sel(void *This, HSTRING *value) {
    (void)This;
    return winrt_sys_make_hstring(
        L"System.Devices.InterfaceClassGuid:=\"{E5323777-F976-4F5B-9B55-B94699C46E44}\" "
        L"AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True",
        value);
}
static HRESULT md_query_wine_default_id(int is_render, uint32_t role, HSTRING *value) {
    if (!value) return E_POINTER;
    *value = NULL;
    HMODULE hMod = LoadLibraryA("windows.media.devices.dll");
    if (hMod) {
        typedef HRESULT (WINAPI *DllGetActFacFn)(HSTRING, void **);
        DllGetActFacFn pfn = (DllGetActFacFn)(void *)GetProcAddress(hMod, "DllGetActivationFactory");
        if (pfn) {
            HSTRING hcls = NULL;
            if (SUCCEEDED(winrt_sys_make_hstring(L"Windows.Media.Devices.MediaDevice", &hcls)) && hcls) {
                void *af = NULL;
                if (SUCCEEDED(pfn(hcls, &af)) && af) {
                    struct { const WinrtMediaDeviceStaticsVtbl *lpVtbl; } *ms = NULL;
                    if (SUCCEEDED(((IUnknown *)af)->lpVtbl->QueryInterface(
                            (IUnknown *)af, &WINRT_IID_IMediaDeviceStatics, (void **)&ms)) && ms) {
                        HRESULT hr = is_render
                            ? ms->lpVtbl->GetDefaultAudioRenderId(ms, role, value)
                            : ms->lpVtbl->GetDefaultAudioCaptureId(ms, role, value);
                        ms->lpVtbl->Release(ms);
                        if (SUCCEEDED(hr) && *value != NULL) {
                            WindowsDeleteString(hcls);
                            ((IUnknown *)af)->lpVtbl->Release((IUnknown *)af);
                            FreeLibrary(hMod);
                            return S_OK;
                        }
                        if (*value) {
                            WindowsDeleteString(*value);
                            *value = NULL;
                        }
                    }
                    ((IUnknown *)af)->lpVtbl->Release((IUnknown *)af);
                }
                WindowsDeleteString(hcls);
            }
        }
        FreeLibrary(hMod);
    }
    return winrt_sys_make_hstring(
        is_render ? L"{0.0.0.00000000}.{default}" : L"{0.0.1.00000000}.{default}",
        value);
}
static HRESULT STDMETHODCALLTYPE md_def_audio_cap(void *This, uint32_t role, HSTRING *value) {
    (void)This;
    return md_query_wine_default_id(0, role, value);
}
static HRESULT STDMETHODCALLTYPE md_def_audio_ren(void *This, uint32_t role, HSTRING *value) {
    (void)This;
    return md_query_wine_default_id(1, role, value);
}
static const WinrtActivationFactoryVtbl g_md_act_vtbl = {
    md_fac_qi, pnp_fac_ar, pnp_fac_rel, md_fac_iids, pnp_fac_rcn, pnp_fac_tl, md_fac_act
};
static const WinrtMediaDeviceStaticsVtbl g_md_stat_vtbl = {
    md_fac_qi, pnp_fac_ar, pnp_fac_rel, md_fac_iids, pnp_fac_rcn, pnp_fac_tl,
    md_audio_cap_sel, md_audio_ren_sel, md_video_cap_sel,
    md_def_audio_cap, md_def_audio_ren,
    pnp_w_add_noop, pnp_w_rem_noop,
    pnp_w_add_noop, pnp_w_rem_noop
};
static WinrtMediaDeviceFactory g_md_factory = { &g_md_act_vtbl, &g_md_stat_vtbl };

HRESULT winrt_sys_get_media_device_factory(REFIID iid, void **out_factory) {
    return md_fac_qi(&g_md_factory.lpVtblAct, iid, out_factory);
}
