/*
 * Proton-Scarlett — UWP Bridge System WinRT: AnalyticsInfo & GameBar
 *
 * Implements:
 *   - Windows.System.Profile.AnalyticsInfo (IAnalyticsInfoStatics, AnalyticsVersionInfo)
 *   - Windows.Gaming.UI.GameBar (IGameBarStatics: Visible=FALSE, IsInputRedirected=FALSE,
 *     VisibilityChanged & IsInputRedirectedChanged event registration)
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls).
 */

#include "winrt_sys_internal.h"

#include <string.h>

/* ============================================================================
 * Windows.System.Profile.AnalyticsInfo & AnalyticsVersionInfo
 * ============================================================================ */

typedef struct WinrtAnalyticsInfoStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_VersionInfo)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_DeviceForm)(void *This, HSTRING *value);
} WinrtAnalyticsInfoStaticsVtbl;

typedef struct WinrtAnalyticsVersionInfoVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_DeviceFamily)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_DeviceFamilyVersion)(void *This, HSTRING *value);
} WinrtAnalyticsVersionInfoVtbl;

typedef struct WinrtAnalyticsVersionInfoObj {
    const WinrtAnalyticsVersionInfoVtbl *lpVtbl;
    volatile LONG ref_count;
} WinrtAnalyticsVersionInfoObj;

static HRESULT STDMETHODCALLTYPE avi_qi(void *This, REFIID riid, void **ppv) {
    WinrtAnalyticsVersionInfoObj *obj = (WinrtAnalyticsVersionInfoObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAnalyticsVersionInfo)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE avi_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtAnalyticsVersionInfoObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE avi_release(void *This) {
    WinrtAnalyticsVersionInfoObj *obj = (WinrtAnalyticsVersionInfoObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE avi_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IAnalyticsVersionInfo, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE avi_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.System.Profile.AnalyticsVersionInfo", n);
}
static HRESULT STDMETHODCALLTYPE avi_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE avi_get_family(void *This, HSTRING *val) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Desktop", val);
}
static HRESULT STDMETHODCALLTYPE avi_get_version(void *This, HSTRING *val) {
    (void)This;
    /* Encodes 10.0.19041.0: ((10ULL<<48)|(0ULL<<32)|(19041ULL<<16)|0ULL) = 2814751014977536 */
    return winrt_sys_make_hstring(L"2814751014977536", val);
}

static const WinrtAnalyticsVersionInfoVtbl g_avi_vtbl = {
    avi_qi, avi_addref, avi_release, avi_getiids, avi_rcn, avi_tl,
    avi_get_family, avi_get_version
};

typedef struct WinrtAnalyticsInfoFactory {
    const WinrtActivationFactoryVtbl     *lpVtblAct;
    const WinrtAnalyticsInfoStaticsVtbl  *lpVtblStatics;
} WinrtAnalyticsInfoFactory;

static WinrtAnalyticsInfoFactory g_ai_factory;

static HRESULT STDMETHODCALLTYPE ai_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_ai_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IAnalyticsInfoStatics)) {
        *ppv = &g_ai_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ai_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE ai_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE ai_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x1d5ee066u, 0x188du, 0x5ba9u, { 0x43u, 0x87u, 0xacu, 0xaeu, 0xb0u, 0xe7u, 0xe3u, 0x05u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE ai_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE ai_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ai_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("AnalyticsInfo::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE ai_fac_get_version_info(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtAnalyticsVersionInfoObj *obj = (WinrtAnalyticsVersionInfoObj *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    obj->lpVtbl = &g_avi_vtbl;
    obj->ref_count = 1;
    *val = obj;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ai_fac_get_device_form(void *This, HSTRING *val) {
    (void)This;
    return winrt_sys_make_hstring(L"Desktop", val);
}

static const WinrtActivationFactoryVtbl g_ai_act_vtbl = {
    ai_fac_qi, ai_fac_addref, ai_fac_release, ai_fac_getiids, ai_fac_rcn, ai_fac_tl, ai_fac_act
};
static const WinrtAnalyticsInfoStaticsVtbl g_ai_stat_vtbl = {
    ai_fac_qi, ai_fac_addref, ai_fac_release, ai_fac_getiids, ai_fac_rcn, ai_fac_tl,
    ai_fac_get_version_info, ai_fac_get_device_form
};
static WinrtAnalyticsInfoFactory g_ai_factory = { &g_ai_act_vtbl, &g_ai_stat_vtbl };

HRESULT winrt_sys_get_analytics_info_factory(REFIID iid, void **out_factory) {
    return ai_fac_qi(&g_ai_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * Windows.Gaming.UI.GameBar
 * ============================================================================ */

typedef struct WinrtGameBarStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_VisibilityChanged)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_VisibilityChanged)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *add_IsInputRedirectedChanged)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_IsInputRedirectedChanged)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *get_Visible)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsInputRedirected)(void *This, boolean *value);
} WinrtGameBarStaticsVtbl;

typedef struct GameBarListener {
    EventRegistrationToken token;
    IUnknown *handler;
} GameBarListener;

typedef struct WinrtGameBarFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtGameBarStaticsVtbl    *lpVtblStatics;
    CRITICAL_SECTION lock;
    LONG init;
    int64_t next_token;
    uint32_t vis_count;
    GameBarListener vis_listeners[16];
    uint32_t redir_count;
    GameBarListener redir_listeners[16];
} WinrtGameBarFactory;

static WinrtGameBarFactory g_gb_factory;

static void ensure_gamebar_lock(void) {
    if (InterlockedCompareExchange(&g_gb_factory.init, 1, 0) == 0) {
        InitializeCriticalSection(&g_gb_factory.lock);
    }
}

static HRESULT STDMETHODCALLTYPE gb_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    ensure_gamebar_lock();
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_gb_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IGameBarStatics)) {
        *ppv = &g_gb_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE gb_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE gb_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE gb_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x1db9a292u, 0xcc78u, 0x4173u, { 0xbeu, 0x45u, 0xb6u, 0x1eu, 0x67u, 0x28u, 0x3eu, 0xa7u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE gb_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE gb_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gb_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("GameBar::ActivateInstance", This);
}

static HRESULT gb_add_listener(GameBarListener *arr, uint32_t *cnt, void *handler, EventRegistrationToken *tok) {
    if (!handler || !tok) return E_POINTER;
    ensure_gamebar_lock();
    EnterCriticalSection(&g_gb_factory.lock);
    if (*cnt >= 16u) {
        LeaveCriticalSection(&g_gb_factory.lock);
        return E_OUTOFMEMORY;
    }
    IUnknown *h = (IUnknown *)handler;
    h->lpVtbl->AddRef(h);
    tok->value = ++g_gb_factory.next_token;
    arr[*cnt].token = *tok;
    arr[*cnt].handler = h;
    (*cnt)++;
    LeaveCriticalSection(&g_gb_factory.lock);
    return S_OK;
}

static HRESULT gb_rem_listener(GameBarListener *arr, uint32_t *cnt, EventRegistrationToken tok) {
    ensure_gamebar_lock();
    EnterCriticalSection(&g_gb_factory.lock);
    for (uint32_t i = 0; i < *cnt; ++i) {
        if (arr[i].token.value == tok.value) {
            if (arr[i].handler) arr[i].handler->lpVtbl->Release(arr[i].handler);
            for (uint32_t j = i + 1u; j < *cnt; ++j) arr[j - 1u] = arr[j];
            (*cnt)--;
            break;
        }
    }
    LeaveCriticalSection(&g_gb_factory.lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gb_add_vis(void *This, void *h, EventRegistrationToken *tok) {
    (void)This;
    return gb_add_listener(g_gb_factory.vis_listeners, &g_gb_factory.vis_count, h, tok);
}
static HRESULT STDMETHODCALLTYPE gb_rem_vis(void *This, EventRegistrationToken tok) {
    (void)This;
    return gb_rem_listener(g_gb_factory.vis_listeners, &g_gb_factory.vis_count, tok);
}
static HRESULT STDMETHODCALLTYPE gb_add_redir(void *This, void *h, EventRegistrationToken *tok) {
    (void)This;
    return gb_add_listener(g_gb_factory.redir_listeners, &g_gb_factory.redir_count, h, tok);
}
static HRESULT STDMETHODCALLTYPE gb_rem_redir(void *This, EventRegistrationToken tok) {
    (void)This;
    return gb_rem_listener(g_gb_factory.redir_listeners, &g_gb_factory.redir_count, tok);
}
static HRESULT STDMETHODCALLTYPE gb_get_visible(void *This, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gb_get_redir(void *This, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0;
    return S_OK;
}

static const WinrtActivationFactoryVtbl g_gb_act_vtbl = {
    gb_fac_qi, gb_fac_addref, gb_fac_release, gb_fac_getiids, gb_fac_rcn, gb_fac_tl, gb_fac_act
};
static const WinrtGameBarStaticsVtbl g_gb_stat_vtbl = {
    gb_fac_qi, gb_fac_addref, gb_fac_release, gb_fac_getiids, gb_fac_rcn, gb_fac_tl,
    gb_add_vis, gb_rem_vis, gb_add_redir, gb_rem_redir, gb_get_visible, gb_get_redir
};
static WinrtGameBarFactory g_gb_factory = {
    &g_gb_act_vtbl, &g_gb_stat_vtbl, { 0 }, 0, 0, 0, { { { 0 }, NULL } }, 0, { { { 0 }, NULL } }
};

HRESULT winrt_sys_get_game_bar_factory(REFIID iid, void **out_factory) {
    return gb_fac_qi(&g_gb_factory.lpVtblAct, iid, out_factory);
}
