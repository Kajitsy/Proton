/*
 * Proton-Scarlett — UWP Bridge System WinRT Extras:
 *   - Windows.Storage.Pickers.FolderPicker (IFolderPicker, IFolderPicker2, IVector<HSTRING>)
 *   - Windows.Storage.AccessCache.StorageApplicationPermissions
 *     (IStorageApplicationPermissionsStatics, IStorageItemAccessList, IStorageItemMostRecentlyUsedList)
 *   - Windows.Security.Cryptography.CryptographicBuffer
 *     (ICryptographicBufferStatics, IBuffer, IBufferByteAccess)
 *   - Windows.UI.Popups.PopupMenu & Windows.UI.Popups.UICommand
 *     (IPopupMenu, IUICommand, IUICommandFactory, IVector<IUICommand*>)
 *   - Windows.System.Threading.ThreadPool
 *     (IThreadPoolStatics, real worker-thread IAsyncAction + IAsyncInfo)
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls on _WIN32).
 */

#include "winrt_sys_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <pthread.h>
#endif

/* ============================================================================
 * Extra WinRT IIDs (verified against Windows 10 SDK / Wine IDL / MinGW)
 * ============================================================================ */

/* FolderPicker */
static const GUID WINRT_IID_IFolderPicker =
    { 0x084f7799u, 0xf3fbu, 0x400au, { 0x99u, 0xb1u, 0x7bu, 0x4au, 0x77u, 0x2fu, 0xd6u, 0x0du } };
static const GUID WINRT_IID_IFolderPicker2 =
    { 0x8eb3ba97u, 0xdc85u, 0x4616u, { 0xbeu, 0x94u, 0x96u, 0x60u, 0x88u, 0x1fu, 0x2fu, 0x5du } };

/* IVector<HSTRING> / IVectorView<HSTRING> / IIterable<HSTRING> / IIterator<HSTRING> */
static const GUID WINRT_IID_IVector_HSTRING =
    { 0x98b9acc1u, 0x4b56u, 0x532eu, { 0xacu, 0x73u, 0x03u, 0xd5u, 0x29u, 0x1cu, 0xa9u, 0x02u } };
static const GUID WINRT_IID_IVectorView_HSTRING =
    { 0x2f13c006u, 0xa03au, 0x5f69u, { 0xb0u, 0x90u, 0x75u, 0xa4u, 0x3eu, 0x33u, 0x42u, 0x3eu } };
static const GUID WINRT_IID_IIterable_HSTRING =
    { 0xe2fcc7c1u, 0x3bfcu, 0x5a0bu, { 0xb2u, 0xb0u, 0x72u, 0xe7u, 0x69u, 0xd1u, 0xcbu, 0x7eu } };
static const GUID WINRT_IID_IIterator_HSTRING =
    { 0x8c304ebbu, 0x6615u, 0x50a4u, { 0x88u, 0x29u, 0x87u, 0x9du, 0xf9u, 0x6du, 0xfbu, 0x2bu } };

/* StorageApplicationPermissions & AccessCache */
static const GUID WINRT_IID_IStorageApplicationPermissionsStatics =
    { 0x4391dfaau, 0xd033u, 0x48f9u, { 0x80u, 0x60u, 0x3eu, 0xc8u, 0x47u, 0xd2u, 0xe3u, 0xf1u } };
static const GUID WINRT_IID_IStorageItemAccessList =
    { 0x2caff6adu, 0xde90u, 0x47f5u, { 0xb2u, 0xc3u, 0xddu, 0x36u, 0xc9u, 0xfdu, 0xd4u, 0x53u } };
static const GUID WINRT_IID_IStorageItemMostRecentlyUsedList =
    { 0x016239d5u, 0x510du, 0x411eu, { 0x8cu, 0xf1u, 0xc3u, 0xd1u, 0xefu, 0xf1u, 0x4cu, 0x33u } };
static const GUID WINRT_IID_IVectorView_AccessListEntry =
    { 0x458007e3u, 0x0580u, 0x5c36u, { 0x94u, 0x41u, 0x16u, 0x42u, 0x64u, 0x6du, 0x19u, 0xfbu } };
static const GUID WINRT_IID_IIterable_AccessListEntry =
    { 0x534c6c2bu, 0x6c30u, 0x5a73u, { 0x81u, 0x48u, 0x49u, 0x27u, 0x02u, 0x0eu, 0x27u, 0x5bu } };
static const GUID WINRT_IID_IIterator_AccessListEntry =
    { 0x3636826cu, 0x1149u, 0x5b6eu, { 0xb7u, 0xc7u, 0x07u, 0xbau, 0x14u, 0x0eu, 0x0du, 0x1au } };

/* CryptographicBuffer */
static const GUID WINRT_IID_ICryptographicBufferStatics =
    { 0x320b7e22u, 0x3cb0u, 0x4cdfu, { 0x86u, 0x63u, 0x1du, 0x28u, 0x91u, 0x00u, 0x65u, 0xebu } };

/* PopupMenu & UICommand */
static const GUID WINRT_IID_IPopupMenu =
    { 0x4e9bc6dcu, 0x880du, 0x47fcu, { 0xa0u, 0xa1u, 0x72u, 0xb6u, 0x39u, 0xe6u, 0x25u, 0x59u } };
static const GUID WINRT_IID_IUICommand =
    { 0x4ff93a75u, 0x4145u, 0x47ffu, { 0xacu, 0x7fu, 0xdfu, 0xf1u, 0xc1u, 0xfau, 0x5bu, 0x0fu } };
static const GUID WINRT_IID_IUICommandFactory =
    { 0xa21a8189u, 0x26b0u, 0x4676u, { 0xaeu, 0x94u, 0x54u, 0x04u, 0x1bu, 0xc1u, 0x25u, 0xe8u } };
static const GUID WINRT_IID_IVector_IUICommand =
    { 0x105139a1u, 0xdcb8u, 0x5f65u, { 0x97u, 0xefu, 0xcbu, 0x1bu, 0xf0u, 0xb7u, 0x5fu, 0x9du } };
static const GUID WINRT_IID_IAsyncOperation_IUICommand =
    { 0xb8770535u, 0x6a4bu, 0x52b1u, { 0xb5u, 0x78u, 0xf3u, 0xcdu, 0xc5u, 0x00u, 0x7au, 0x1fu } };

/* ThreadPool */
static const GUID WINRT_IID_IThreadPoolStatics =
    { 0xb6bf67ddu, 0x84bdu, 0x44f8u, { 0xacu, 0x1cu, 0x93u, 0xebu, 0xcbu, 0x9du, 0xbau, 0x91u } };

/* Helper to read path from any IStorageItem */
typedef struct StorageItemPathVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *RenameAsync0)(void *This, HSTRING desiredName, void **operation);
    HRESULT (STDMETHODCALLTYPE *RenameAsync1)(void *This, HSTRING desiredName, uint32_t option, void **operation);
    HRESULT (STDMETHODCALLTYPE *DeleteAsync0)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *DeleteAsync1)(void *This, uint32_t option, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetBasicPropertiesAsync)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Path)(void *This, HSTRING *value);
} StorageItemPathVtbl;

typedef struct StorageItemPathObj {
    const StorageItemPathVtbl *lpVtbl;
} StorageItemPathObj;

static void extras_wstr_to_utf8(const wchar_t *ws, char *out, size_t out_sz) {
    if (!out || out_sz == 0u) return;
    out[0] = '\0';
    if (!ws || ws[0] == L'\0') return;
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, (int)out_sz, NULL, NULL);
    if (n <= 0) out[0] = '\0';
}

/* ============================================================================
 * 1. IVector<HSTRING> / IVectorView<HSTRING> / IIterable<HSTRING> / IIterator<HSTRING>
 * ============================================================================ */

typedef struct WinrtIteratorHstringVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, HSTRING *current);
    HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *hasCurrent);
    HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *hasCurrent);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t capacity, HSTRING *items, uint32_t *actual);
} WinrtIteratorHstringVtbl;

typedef struct WinrtIterableHstringVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *First)(void *This, void **first);
} WinrtIterableHstringVtbl;

typedef struct WinrtVectorViewHstringVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t index, HSTRING *item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, HSTRING value, uint32_t *index, boolean *found);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t startIndex, uint32_t capacity, HSTRING *items, uint32_t *actual);
} WinrtVectorViewHstringVtbl;

typedef struct WinrtVectorHstringVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t index, HSTRING *item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
    HRESULT (STDMETHODCALLTYPE *GetView)(void *This, void **view);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, HSTRING value, uint32_t *index, boolean *found);
    HRESULT (STDMETHODCALLTYPE *SetAt)(void *This, uint32_t index, HSTRING item);
    HRESULT (STDMETHODCALLTYPE *InsertAt)(void *This, uint32_t index, HSTRING item);
    HRESULT (STDMETHODCALLTYPE *RemoveAt)(void *This, uint32_t index);
    HRESULT (STDMETHODCALLTYPE *Append)(void *This, HSTRING item);
    HRESULT (STDMETHODCALLTYPE *RemoveAtEnd)(void *This);
    HRESULT (STDMETHODCALLTYPE *Clear)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t startIndex, uint32_t capacity, HSTRING *items, uint32_t *actual);
    HRESULT (STDMETHODCALLTYPE *ReplaceAll)(void *This, uint32_t count, HSTRING *items);
} WinrtVectorHstringVtbl;

#define HSTR_VEC_MAX 64u

typedef struct HstrVecObj {
    const WinrtVectorHstringVtbl     *lpVtblVec;
    const WinrtVectorViewHstringVtbl *lpVtblView;
    const WinrtIterableHstringVtbl   *lpVtblIter;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    uint32_t count;
    HSTRING items[HSTR_VEC_MAX];
} HstrVecObj;

typedef struct HstrIterObj {
    const WinrtIteratorHstringVtbl *lpVtbl;
    volatile LONG ref_count;
    HstrVecObj *vec;
    uint32_t index;
} HstrIterObj;

static HRESULT STDMETHODCALLTYPE hstr_iter_qi(void *This, REFIID riid, void **ppv) {
    HstrIterObj *it = (HstrIterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IIterator_HSTRING)) {
        InterlockedIncrement(&it->ref_count);
        *ppv = it;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE hstr_iter_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((HstrIterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE hstr_iter_release(void *This) {
    HstrIterObj *it = (HstrIterObj *)This;
    LONG rc = InterlockedDecrement(&it->ref_count);
    if (rc == 0) {
        if (it->vec) it->vec->lpVtblVec->Release(&it->vec->lpVtblVec);
        winrt_sys_free(it);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE hstr_iter_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IIterator_HSTRING, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE hstr_iter_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1<String>", n);
}
static HRESULT STDMETHODCALLTYPE hstr_iter_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hstr_iter_cur(void *This, HSTRING *cur) {
    HstrIterObj *it = (HstrIterObj *)This;
    if (!cur) return E_POINTER;
    *cur = NULL;
    EnterCriticalSection(&it->vec->lock);
    HRESULT hr = E_BOUNDS;
    if (it->index < it->vec->count) {
        hr = winrt_sys_dup_hstring(it->vec->items[it->index], cur);
    }
    LeaveCriticalSection(&it->vec->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hstr_iter_has(void *This, boolean *has) {
    HstrIterObj *it = (HstrIterObj *)This;
    if (!has) return E_POINTER;
    EnterCriticalSection(&it->vec->lock);
    *has = (it->index < it->vec->count) ? 1 : 0;
    LeaveCriticalSection(&it->vec->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hstr_iter_next(void *This, boolean *has) {
    HstrIterObj *it = (HstrIterObj *)This;
    if (!has) return E_POINTER;
    EnterCriticalSection(&it->vec->lock);
    if (it->index < it->vec->count) it->index++;
    *has = (it->index < it->vec->count) ? 1 : 0;
    LeaveCriticalSection(&it->vec->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hstr_iter_many(void *This, uint32_t cap, HSTRING *items, uint32_t *actual) {
    HstrIterObj *it = (HstrIterObj *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (cap > 0 && !items) return E_POINTER;
    EnterCriticalSection(&it->vec->lock);
    uint32_t n = 0;
    while (n < cap && it->index < it->vec->count) {
        items[n] = NULL;
        winrt_sys_dup_hstring(it->vec->items[it->index], &items[n]);
        it->index++;
        n++;
    }
    LeaveCriticalSection(&it->vec->lock);
    *actual = n;
    return S_OK;
}

static const WinrtIteratorHstringVtbl g_hstr_iter_vtbl = {
    hstr_iter_qi, hstr_iter_addref, hstr_iter_release,
    hstr_iter_getiids, hstr_iter_rcn, hstr_iter_tl,
    hstr_iter_cur, hstr_iter_has, hstr_iter_next, hstr_iter_many
};

static HRESULT hstr_vec_qi_impl(HstrVecObj *v, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IVector_HSTRING)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblVec;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IVectorView_HSTRING)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblView;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_HSTRING)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG hstr_vec_release_impl(HstrVecObj *v) {
    LONG rc = InterlockedDecrement(&v->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < v->count; ++i) {
            if (v->items[i]) WindowsDeleteString(v->items[i]);
        }
        DeleteCriticalSection(&v->lock);
        winrt_sys_free(v);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE hvec_qi(void *This, REFIID riid, void **ppv) {
    return hstr_vec_qi_impl(WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hvec_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE hvec_release(void *This) {
    return hstr_vec_release_impl(WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This));
}
static HRESULT STDMETHODCALLTYPE hvec_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x98b9acc1u, 0x4b56u, 0x532eu, { 0xacu, 0x73u, 0x03u, 0xd5u, 0x29u, 0x1cu, 0xa9u, 0x02u } },
        { 0xe2fcc7c1u, 0x3bfcu, 0x5a0bu, { 0xb2u, 0xb0u, 0x72u, 0xe7u, 0x69u, 0xd1u, 0xcbu, 0x7eu } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE hvec_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IVector`1<String>", n);
}
static HRESULT STDMETHODCALLTYPE hvec_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_get_at(void *This, uint32_t idx, HSTRING *item) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    if (!item) return E_POINTER;
    *item = NULL;
    EnterCriticalSection(&v->lock);
    HRESULT hr = (idx < v->count) ? winrt_sys_dup_hstring(v->items[idx], item) : E_BOUNDS;
    LeaveCriticalSection(&v->lock);
    return hr;
}
static HRESULT STDMETHODCALLTYPE hvec_get_size(void *This, uint32_t *sz) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    if (!sz) return E_POINTER;
    EnterCriticalSection(&v->lock);
    *sz = v->count;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_get_view(void *This, void **view) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    return hstr_vec_qi_impl(v, &WINRT_IID_IVectorView_HSTRING, view);
}
static HRESULT STDMETHODCALLTYPE hvec_index_of(void *This, HSTRING val, uint32_t *idx, boolean *found) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    if (!idx || !found) return E_POINTER;
    *idx = 0;
    *found = 0;
    const wchar_t *needle = winrt_sys_hstring_buf(val, NULL);
    EnterCriticalSection(&v->lock);
    for (uint32_t i = 0; i < v->count; ++i) {
        if (wcscmp(winrt_sys_hstring_buf(v->items[i], NULL), needle) == 0) {
            *idx = i;
            *found = 1;
            break;
        }
    }
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_set_at(void *This, uint32_t idx, HSTRING item) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    HSTRING dup = NULL;
    HRESULT hr = winrt_sys_dup_hstring(item, &dup);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&v->lock);
    if (idx >= v->count) {
        LeaveCriticalSection(&v->lock);
        if (dup) WindowsDeleteString(dup);
        return E_BOUNDS;
    }
    if (v->items[idx]) WindowsDeleteString(v->items[idx]);
    v->items[idx] = dup;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_insert_at(void *This, uint32_t idx, HSTRING item) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    HSTRING dup = NULL;
    HRESULT hr = winrt_sys_dup_hstring(item, &dup);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&v->lock);
    if (idx > v->count) {
        LeaveCriticalSection(&v->lock);
        if (dup) WindowsDeleteString(dup);
        return E_BOUNDS;
    }
    if (v->count >= HSTR_VEC_MAX) {
        LeaveCriticalSection(&v->lock);
        if (dup) WindowsDeleteString(dup);
        return E_OUTOFMEMORY;
    }
    for (uint32_t i = v->count; i > idx; --i) v->items[i] = v->items[i - 1u];
    v->items[idx] = dup;
    v->count++;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_remove_at(void *This, uint32_t idx) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    EnterCriticalSection(&v->lock);
    if (idx >= v->count) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    if (v->items[idx]) WindowsDeleteString(v->items[idx]);
    for (uint32_t i = idx + 1u; i < v->count; ++i) v->items[i - 1u] = v->items[i];
    v->count--;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_append(void *This, HSTRING item) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    HSTRING dup = NULL;
    HRESULT hr = winrt_sys_dup_hstring(item, &dup);
    if (FAILED(hr)) return hr;
    EnterCriticalSection(&v->lock);
    if (v->count >= HSTR_VEC_MAX) {
        LeaveCriticalSection(&v->lock);
        if (dup) WindowsDeleteString(dup);
        return E_OUTOFMEMORY;
    }
    v->items[v->count++] = dup;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_remove_end(void *This) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    EnterCriticalSection(&v->lock);
    if (v->count == 0u) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    v->count--;
    if (v->items[v->count]) WindowsDeleteString(v->items[v->count]);
    v->items[v->count] = NULL;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_clear(void *This) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    EnterCriticalSection(&v->lock);
    for (uint32_t i = 0; i < v->count; ++i) {
        if (v->items[i]) WindowsDeleteString(v->items[i]);
        v->items[i] = NULL;
    }
    v->count = 0;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_get_many(void *This, uint32_t start, uint32_t cap, HSTRING *items, uint32_t *actual) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblVec, This);
    if (!actual) return E_POINTER;
    *actual = 0;
    if (cap > 0 && !items) return E_POINTER;
    EnterCriticalSection(&v->lock);
    uint32_t n = 0;
    for (uint32_t i = start; i < v->count && n < cap; ++i, ++n) {
        items[n] = NULL;
        winrt_sys_dup_hstring(v->items[i], &items[n]);
    }
    LeaveCriticalSection(&v->lock);
    *actual = n;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE hvec_replace_all(void *This, uint32_t count, HSTRING *items) {
    if (count > 0 && !items) return E_POINTER;
    if (count > HSTR_VEC_MAX) return E_OUTOFMEMORY;
    hvec_clear(This);
    for (uint32_t i = 0; i < count; ++i) {
        HRESULT hr = hvec_append(This, items[i]);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE hview_qi(void *This, REFIID riid, void **ppv) {
    return hstr_vec_qi_impl(WINRT_SYS_FROM(HstrVecObj, lpVtblView, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hview_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(HstrVecObj, lpVtblView, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE hview_release(void *This) {
    return hstr_vec_release_impl(WINRT_SYS_FROM(HstrVecObj, lpVtblView, This));
}
static HRESULT STDMETHODCALLTYPE hview_get_at(void *This, uint32_t idx, HSTRING *item) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblView, This);
    return hvec_get_at(&v->lpVtblVec, idx, item);
}
static HRESULT STDMETHODCALLTYPE hview_get_size(void *This, uint32_t *sz) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblView, This);
    return hvec_get_size(&v->lpVtblVec, sz);
}
static HRESULT STDMETHODCALLTYPE hview_index_of(void *This, HSTRING val, uint32_t *idx, boolean *found) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblView, This);
    return hvec_index_of(&v->lpVtblVec, val, idx, found);
}
static HRESULT STDMETHODCALLTYPE hview_get_many(void *This, uint32_t start, uint32_t cap, HSTRING *items, uint32_t *actual) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblView, This);
    return hvec_get_many(&v->lpVtblVec, start, cap, items, actual);
}

static HRESULT STDMETHODCALLTYPE hiter_qi(void *This, REFIID riid, void **ppv) {
    return hstr_vec_qi_impl(WINRT_SYS_FROM(HstrVecObj, lpVtblIter, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE hiter_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(HstrVecObj, lpVtblIter, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE hiter_release(void *This) {
    return hstr_vec_release_impl(WINRT_SYS_FROM(HstrVecObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE hiter_first(void *This, void **first) {
    HstrVecObj *v = WINRT_SYS_FROM(HstrVecObj, lpVtblIter, This);
    if (!first) return E_POINTER;
    *first = NULL;
    HstrIterObj *it = (HstrIterObj *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = &g_hstr_iter_vtbl;
    it->ref_count = 1;
    InterlockedIncrement(&v->ref_count);
    it->vec = v;
    it->index = 0;
    *first = it;
    return S_OK;
}

static const WinrtVectorHstringVtbl g_hstr_vec_vtbl = {
    hvec_qi, hvec_addref, hvec_release, hvec_getiids, hvec_rcn, hvec_tl,
    hvec_get_at, hvec_get_size, hvec_get_view, hvec_index_of,
    hvec_set_at, hvec_insert_at, hvec_remove_at, hvec_append,
    hvec_remove_end, hvec_clear, hvec_get_many, hvec_replace_all
};
static const WinrtVectorViewHstringVtbl g_hstr_view_vtbl = {
    hview_qi, hview_addref, hview_release, hvec_getiids, hvec_rcn, hvec_tl,
    hview_get_at, hview_get_size, hview_index_of, hview_get_many
};
static const WinrtIterableHstringVtbl g_hstr_iterable_vtbl = {
    hiter_qi, hiter_addref, hiter_release, hvec_getiids, hvec_rcn, hvec_tl,
    hiter_first
};

static HstrVecObj *hstr_vec_create(void) {
    HstrVecObj *v = (HstrVecObj *)winrt_sys_alloc(sizeof(*v));
    if (!v) return NULL;
    v->lpVtblVec = &g_hstr_vec_vtbl;
    v->lpVtblView = &g_hstr_view_vtbl;
    v->lpVtblIter = &g_hstr_iterable_vtbl;
    v->ref_count = 1;
    InitializeCriticalSection(&v->lock);
    return v;
}

/* ============================================================================
 * 2. Windows.Storage.Pickers.FolderPicker
 * ============================================================================ */

typedef struct WinrtFolderPickerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_ViewMode)(void *This, int32_t *value);
    HRESULT (STDMETHODCALLTYPE *put_ViewMode)(void *This, int32_t value);
    HRESULT (STDMETHODCALLTYPE *get_SettingsIdentifier)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *put_SettingsIdentifier)(void *This, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *get_SuggestedStartLocation)(void *This, int32_t *value);
    HRESULT (STDMETHODCALLTYPE *put_SuggestedStartLocation)(void *This, int32_t value);
    HRESULT (STDMETHODCALLTYPE *get_CommitButtonText)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *put_CommitButtonText)(void *This, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *get_FileTypeFilter)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *PickSingleFolderAsync)(void *This, void **operation);
} WinrtFolderPickerVtbl;

typedef struct WinrtFolderPicker2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_ContinuationData)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *PickFolderAndContinue)(void *This);
} WinrtFolderPicker2Vtbl;

typedef struct FolderPickerObj {
    const WinrtFolderPickerVtbl  *lpVtbl;
    const WinrtFolderPicker2Vtbl *lpVtbl2;
    volatile LONG ref_count;
    int32_t view_mode;
    int32_t suggested_location;
    HSTRING settings_id;
    HSTRING commit_text;
    HstrVecObj *file_types;
} FolderPickerObj;

static HRESULT fp_qi_impl(FolderPickerObj *o, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IFolderPicker)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IFolderPicker2)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = &o->lpVtbl2;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG fp_release_impl(FolderPickerObj *o) {
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) {
        if (o->settings_id) WindowsDeleteString(o->settings_id);
        if (o->commit_text) WindowsDeleteString(o->commit_text);
        if (o->file_types) o->file_types->lpVtblVec->Release(&o->file_types->lpVtblVec);
        winrt_sys_free(o);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE fp_qi(void *This, REFIID riid, void **ppv) {
    return fp_qi_impl(WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE fp_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE fp_release(void *This) {
    return fp_release_impl(WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This));
}
static HRESULT STDMETHODCALLTYPE fp_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x084f7799u, 0xf3fbu, 0x400au, { 0x99u, 0xb1u, 0x7bu, 0x4au, 0x77u, 0x2fu, 0xd6u, 0x0du } },
        { 0x8eb3ba97u, 0xdc85u, 0x4616u, { 0xbeu, 0x94u, 0x96u, 0x60u, 0x88u, 0x1fu, 0x2fu, 0x5du } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE fp_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.Pickers.FolderPicker", n);
}
static HRESULT STDMETHODCALLTYPE fp_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fp_get_vm(void *This, int32_t *val) {
    if (!val) return E_POINTER;
    *val = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This)->view_mode;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fp_put_vm(void *This, int32_t val) {
    WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This)->view_mode = val;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fp_get_sid(void *This, HSTRING *val) {
    FolderPickerObj *o = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This);
    return winrt_sys_dup_hstring(o->settings_id, val);
}
static HRESULT STDMETHODCALLTYPE fp_put_sid(void *This, HSTRING val) {
    FolderPickerObj *o = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This);
    if (o->settings_id) WindowsDeleteString(o->settings_id);
    o->settings_id = NULL;
    return winrt_sys_dup_hstring(val, &o->settings_id);
}
static HRESULT STDMETHODCALLTYPE fp_get_loc(void *This, int32_t *val) {
    if (!val) return E_POINTER;
    *val = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This)->suggested_location;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fp_put_loc(void *This, int32_t val) {
    WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This)->suggested_location = val;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fp_get_cbt(void *This, HSTRING *val) {
    FolderPickerObj *o = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This);
    return winrt_sys_dup_hstring(o->commit_text, val);
}
static HRESULT STDMETHODCALLTYPE fp_put_cbt(void *This, HSTRING val) {
    FolderPickerObj *o = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This);
    if (o->commit_text) WindowsDeleteString(o->commit_text);
    o->commit_text = NULL;
    return winrt_sys_dup_hstring(val, &o->commit_text);
}
static HRESULT STDMETHODCALLTYPE fp_get_filter(void *This, void **val) {
    FolderPickerObj *o = WINRT_SYS_FROM(FolderPickerObj, lpVtbl, This);
    if (!val) return E_POINTER;
    *val = NULL;
    if (!o->file_types) return E_OUTOFMEMORY;
    return hstr_vec_qi_impl(o->file_types, &WINRT_IID_IVector_HSTRING, val);
}
static HRESULT STDMETHODCALLTYPE fp_pick_single(void *This, void **op) {
    (void)This;
    if (!op) return E_POINTER;
    *op = NULL;

    wchar_t raw_dir[UWP_MAX_PATH_CHARS];
    wchar_t pick_dir[UWP_MAX_PATH_CHARS];
    raw_dir[0] = L'\0';
    pick_dir[0] = L'\0';

    DWORD env_len = GetEnvironmentVariableW(L"PROTON_UWP_PICKER_DIR", raw_dir, UWP_MAX_PATH_CHARS);
    if (env_len > 0 && env_len < UWP_MAX_PATH_CHARS) {
        winrt_sys_normalize_path(raw_dir, pick_dir, UWP_MAX_PATH_CHARS);
    } else {
        winrt_sys_get_state_dir(WINRT_SYS_STATE_LOCAL, pick_dir, UWP_MAX_PATH_CHARS);
        if (pick_dir[0] == L'\0') {
            winrt_sys_get_install_dir(pick_dir, UWP_MAX_PATH_CHARS);
        }
    }
    winrt_sys_ensure_dir_recursive(pick_dir);

    char u8[UWP_MAX_PATH_CHARS * 2];
    extras_wstr_to_utf8(pick_dir, u8, sizeof(u8));
    uwp_bridge_log("winrt_sys", "FolderPicker::PickSingleFolderAsync -> '%s'", u8);

    void *folder_obj = NULL;
    HRESULT hr = winrt_sys_create_storage_folder(pick_dir, &folder_obj);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFolder,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
        (IUnknown *)folder_obj,
        hr,
        op);
    if (folder_obj) {
        ((IUnknown *)folder_obj)->lpVtbl->Release((IUnknown *)folder_obj);
    }
    return out_hr;
}

static HRESULT STDMETHODCALLTYPE fp2_qi(void *This, REFIID riid, void **ppv) {
    return fp_qi_impl(WINRT_SYS_FROM(FolderPickerObj, lpVtbl2, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE fp2_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(FolderPickerObj, lpVtbl2, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE fp2_release(void *This) {
    return fp_release_impl(WINRT_SYS_FROM(FolderPickerObj, lpVtbl2, This));
}
static HRESULT STDMETHODCALLTYPE fp2_get_cont(void *This, void **val) {
    if (val) *val = NULL;
    return winrt_sys_unimplemented("IFolderPicker2::get_ContinuationData", This);
}
static HRESULT STDMETHODCALLTYPE fp2_pick_cont(void *This) {
    (void)This;
    return S_OK;
}

static const WinrtFolderPickerVtbl g_fp_vtbl = {
    fp_qi, fp_addref, fp_release, fp_getiids, fp_rcn, fp_tl,
    fp_get_vm, fp_put_vm, fp_get_sid, fp_put_sid,
    fp_get_loc, fp_put_loc, fp_get_cbt, fp_put_cbt,
    fp_get_filter, fp_pick_single
};
static const WinrtFolderPicker2Vtbl g_fp2_vtbl = {
    fp2_qi, fp2_addref, fp2_release, fp_getiids, fp_rcn, fp_tl,
    fp2_get_cont, fp2_pick_cont
};

typedef struct FolderPickerFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
} FolderPickerFactory;

static FolderPickerFactory g_fp_factory;

static HRESULT STDMETHODCALLTYPE fp_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_fp_factory.lpVtblAct;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE fp_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE fp_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE fp_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IActivationFactory, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE fp_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE fp_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE fp_fac_act(void *This, IInspectable **inst) {
    (void)This;
    if (!inst) return E_POINTER;
    *inst = NULL;
    FolderPickerObj *o = (FolderPickerObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return E_OUTOFMEMORY;
    o->lpVtbl = &g_fp_vtbl;
    o->lpVtbl2 = &g_fp2_vtbl;
    o->ref_count = 1;
    o->file_types = hstr_vec_create();
    if (!o->file_types) {
        winrt_sys_free(o);
        return E_OUTOFMEMORY;
    }
    *inst = (IInspectable *)&o->lpVtbl;
    return S_OK;
}

static const WinrtActivationFactoryVtbl g_fp_act_vtbl = {
    fp_fac_qi, fp_fac_addref, fp_fac_release, fp_fac_getiids, fp_fac_rcn, fp_fac_tl, fp_fac_act
};
static FolderPickerFactory g_fp_factory = { &g_fp_act_vtbl };

HRESULT winrt_sys_get_folder_picker_factory(REFIID iid, void **out_factory) {
    return fp_fac_qi(&g_fp_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 3. Windows.Storage.AccessCache.StorageApplicationPermissions
 * ============================================================================ */

typedef struct WinrtAccessListEntry {
    HSTRING Token;
    HSTRING Metadata;
} WinrtAccessListEntry;

typedef struct AccessCacheRecord {
    wchar_t token[128];
    wchar_t metadata[256];
    wchar_t path[UWP_MAX_PATH_CHARS];
} AccessCacheRecord;

#define ACCESS_CACHE_MAX 64u

typedef struct WinrtAccessListEntryIterVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, WinrtAccessListEntry *current);
    HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *hasCurrent);
    HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *hasCurrent);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t capacity, WinrtAccessListEntry *items, uint32_t *actual);
} WinrtAccessListEntryIterVtbl;

typedef struct WinrtAccessListEntryIterableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *First)(void *This, void **first);
} WinrtAccessListEntryIterableVtbl;

typedef struct WinrtAccessListEntryViewVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t index, WinrtAccessListEntry *item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, WinrtAccessListEntry value, uint32_t *index, boolean *found);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t startIndex, uint32_t capacity, WinrtAccessListEntry *items, uint32_t *actual);
} WinrtAccessListEntryViewVtbl;

typedef struct AccessEntryViewObj {
    const WinrtAccessListEntryViewVtbl     *lpVtblView;
    const WinrtAccessListEntryIterableVtbl *lpVtblIter;
    volatile LONG ref_count;
    uint32_t count;
    AccessCacheRecord *records;
} AccessEntryViewObj;

typedef struct AccessEntryIterObj {
    const WinrtAccessListEntryIterVtbl *lpVtbl;
    volatile LONG ref_count;
    AccessEntryViewObj *view;
    uint32_t index;
} AccessEntryIterObj;

static HRESULT STDMETHODCALLTYPE aei_qi(void *This, REFIID riid, void **ppv) {
    AccessEntryIterObj *it = (AccessEntryIterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IIterator_AccessListEntry)) {
        InterlockedIncrement(&it->ref_count);
        *ppv = it;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE aei_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((AccessEntryIterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE aei_release(void *This) {
    AccessEntryIterObj *it = (AccessEntryIterObj *)This;
    LONG rc = InterlockedDecrement(&it->ref_count);
    if (rc == 0) {
        if (it->view) it->view->lpVtblView->Release(&it->view->lpVtblView);
        winrt_sys_free(it);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE aei_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IIterator_AccessListEntry, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE aei_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1<Windows.Storage.AccessCache.AccessListEntry>", n);
}
static HRESULT STDMETHODCALLTYPE aei_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aei_cur(void *This, WinrtAccessListEntry *cur) {
    AccessEntryIterObj *it = (AccessEntryIterObj *)This;
    if (!cur) return E_POINTER;
    cur->Token = NULL;
    cur->Metadata = NULL;
    if (it->index >= it->view->count) return E_BOUNDS;
    winrt_sys_make_hstring(it->view->records[it->index].token, &cur->Token);
    winrt_sys_make_hstring(it->view->records[it->index].metadata, &cur->Metadata);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aei_has(void *This, boolean *has) {
    AccessEntryIterObj *it = (AccessEntryIterObj *)This;
    if (!has) return E_POINTER;
    *has = (it->index < it->view->count) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aei_next(void *This, boolean *has) {
    AccessEntryIterObj *it = (AccessEntryIterObj *)This;
    if (!has) return E_POINTER;
    if (it->index < it->view->count) it->index++;
    *has = (it->index < it->view->count) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aei_many(void *This, uint32_t cap, WinrtAccessListEntry *items, uint32_t *actual) {
    AccessEntryIterObj *it = (AccessEntryIterObj *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (cap > 0 && !items) return E_POINTER;
    uint32_t n = 0;
    while (n < cap && it->index < it->view->count) {
        items[n].Token = NULL;
        items[n].Metadata = NULL;
        winrt_sys_make_hstring(it->view->records[it->index].token, &items[n].Token);
        winrt_sys_make_hstring(it->view->records[it->index].metadata, &items[n].Metadata);
        it->index++;
        n++;
    }
    *actual = n;
    return S_OK;
}

static const WinrtAccessListEntryIterVtbl g_ae_iter_vtbl = {
    aei_qi, aei_addref, aei_release, aei_getiids, aei_rcn, aei_tl,
    aei_cur, aei_has, aei_next, aei_many
};

static HRESULT aev_qi_impl(AccessEntryViewObj *v, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IVectorView_AccessListEntry)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblView;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_AccessListEntry)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = &v->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG aev_release_impl(AccessEntryViewObj *v) {
    LONG rc = InterlockedDecrement(&v->ref_count);
    if (rc == 0) {
        if (v->records) winrt_sys_free(v->records);
        winrt_sys_free(v);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE aev_qi(void *This, REFIID riid, void **ppv) {
    return aev_qi_impl(WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE aev_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE aev_release(void *This) {
    return aev_release_impl(WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This));
}
static HRESULT STDMETHODCALLTYPE aev_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IVectorView_AccessListEntry, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE aev_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.AccessCache.AccessListEntryView", n);
}
static HRESULT STDMETHODCALLTYPE aev_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aev_get_at(void *This, uint32_t idx, WinrtAccessListEntry *item) {
    AccessEntryViewObj *v = WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This);
    if (!item) return E_POINTER;
    item->Token = NULL;
    item->Metadata = NULL;
    if (idx >= v->count) return E_BOUNDS;
    winrt_sys_make_hstring(v->records[idx].token, &item->Token);
    winrt_sys_make_hstring(v->records[idx].metadata, &item->Metadata);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aev_get_size(void *This, uint32_t *sz) {
    AccessEntryViewObj *v = WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This);
    if (!sz) return E_POINTER;
    *sz = v->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aev_index_of(void *This, WinrtAccessListEntry val, uint32_t *idx, boolean *found) {
    AccessEntryViewObj *v = WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This);
    if (!idx || !found) return E_POINTER;
    *idx = 0;
    *found = 0;
    const wchar_t *tok = winrt_sys_hstring_buf(val.Token, NULL);
    for (uint32_t i = 0; i < v->count; ++i) {
        if (wcscmp(v->records[i].token, tok) == 0) {
            *idx = i;
            *found = 1;
            break;
        }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE aev_get_many(void *This, uint32_t start, uint32_t cap, WinrtAccessListEntry *items, uint32_t *actual) {
    AccessEntryViewObj *v = WINRT_SYS_FROM(AccessEntryViewObj, lpVtblView, This);
    if (!actual) return E_POINTER;
    *actual = 0;
    if (cap > 0 && !items) return E_POINTER;
    uint32_t n = 0;
    for (uint32_t i = start; i < v->count && n < cap; ++i, ++n) {
        items[n].Token = NULL;
        items[n].Metadata = NULL;
        winrt_sys_make_hstring(v->records[i].token, &items[n].Token);
        winrt_sys_make_hstring(v->records[i].metadata, &items[n].Metadata);
    }
    *actual = n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE aev_iter_qi(void *This, REFIID riid, void **ppv) {
    return aev_qi_impl(WINRT_SYS_FROM(AccessEntryViewObj, lpVtblIter, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE aev_iter_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(AccessEntryViewObj, lpVtblIter, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE aev_iter_release(void *This) {
    return aev_release_impl(WINRT_SYS_FROM(AccessEntryViewObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE aev_iter_first(void *This, void **first) {
    AccessEntryViewObj *v = WINRT_SYS_FROM(AccessEntryViewObj, lpVtblIter, This);
    if (!first) return E_POINTER;
    *first = NULL;
    AccessEntryIterObj *it = (AccessEntryIterObj *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = &g_ae_iter_vtbl;
    it->ref_count = 1;
    InterlockedIncrement(&v->ref_count);
    it->view = v;
    it->index = 0;
    *first = it;
    return S_OK;
}

static const WinrtAccessListEntryViewVtbl g_aev_vtbl = {
    aev_qi, aev_addref, aev_release, aev_getiids, aev_rcn, aev_tl,
    aev_get_at, aev_get_size, aev_index_of, aev_get_many
};
static const WinrtAccessListEntryIterableVtbl g_aev_iter_vtbl = {
    aev_iter_qi, aev_iter_addref, aev_iter_release, aev_getiids, aev_rcn, aev_tl,
    aev_iter_first
};

typedef struct WinrtStorageItemAccessListVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *AddOverloadDefaultMetadata)(void *This, IUnknown *file, HSTRING *token);
    HRESULT (STDMETHODCALLTYPE *Add)(void *This, IUnknown *file, HSTRING metadata, HSTRING *token);
    HRESULT (STDMETHODCALLTYPE *AddOrReplaceOverloadDefaultMetadata)(void *This, HSTRING token, IUnknown *file);
    HRESULT (STDMETHODCALLTYPE *AddOrReplace)(void *This, HSTRING token, IUnknown *file, HSTRING metadata);
    HRESULT (STDMETHODCALLTYPE *GetItemAsync)(void *This, HSTRING token, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFileAsync)(void *This, HSTRING token, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFolderAsync)(void *This, HSTRING token, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetItemWithOptionsAsync)(void *This, HSTRING token, uint32_t options, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFileWithOptionsAsync)(void *This, HSTRING token, uint32_t options, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFolderWithOptionsAsync)(void *This, HSTRING token, uint32_t options, void **operation);
    HRESULT (STDMETHODCALLTYPE *Remove)(void *This, HSTRING token);
    HRESULT (STDMETHODCALLTYPE *ContainsItem)(void *This, HSTRING token, boolean *value);
    HRESULT (STDMETHODCALLTYPE *Clear)(void *This);
    HRESULT (STDMETHODCALLTYPE *CheckAccess)(void *This, IUnknown *file, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Entries)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_MaximumItemsAllowed)(void *This, uint32_t *value);
} WinrtStorageItemAccessListVtbl;

typedef struct WinrtMruListVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_ItemRemoved)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_ItemRemoved)(void *This, EventRegistrationToken token);
} WinrtMruListVtbl;

typedef struct AccessListObj {
    const WinrtStorageItemAccessListVtbl *lpVtblList;
    const WinrtMruListVtbl               *lpVtblMru;
    int is_mru;
    LONG init;
    CRITICAL_SECTION lock;
    uint32_t next_id;
    uint32_t count;
    AccessCacheRecord entries[ACCESS_CACHE_MAX];
} AccessListObj;

static AccessListObj g_future_access_list;
static AccessListObj g_mru_access_list;

static void access_list_init(AccessListObj *lst) {
    if (InterlockedCompareExchange(&lst->init, 1, 0) == 0) {
        InitializeCriticalSection(&lst->lock);
    }
}

static HRESULT extract_storage_item_path(IUnknown *item, wchar_t *out_path, size_t out_chars) {
    if (!item || !out_path || out_chars == 0u) return E_INVALIDARG;
    out_path[0] = L'\0';
    StorageItemPathObj *si = NULL;
    HRESULT hr = item->lpVtbl->QueryInterface(item, &WINRT_IID_IStorageItem, (void **)&si);
    if (FAILED(hr) || !si) return hr;
    HSTRING path_h = NULL;
    hr = si->lpVtbl->get_Path(si, &path_h);
    si->lpVtbl->Release(si);
    if (SUCCEEDED(hr) && path_h) {
        winrt_sys_copy_wstr(out_path, out_chars, winrt_sys_hstring_buf(path_h, NULL));
        WindowsDeleteString(path_h);
    }
    return hr;
}

static HRESULT al_qi_impl(AccessListObj *lst, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    access_list_init(lst);
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IStorageItemAccessList)) {
        *ppv = &lst->lpVtblList;
        return S_OK;
    }
    if (lst->is_mru && winrt_sys_guid_eq(riid, &WINRT_IID_IStorageItemMostRecentlyUsedList)) {
        *ppv = &lst->lpVtblMru;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE al_qi(void *This, REFIID riid, void **ppv) {
    return al_qi_impl(WINRT_SYS_FROM(AccessListObj, lpVtblList, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE al_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE al_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE al_getiids(void *This, ULONG *c, IID **i) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (lst->is_mru) {
        static const GUID list[] = {
            { 0x2caff6adu, 0xde90u, 0x47f5u, { 0xb2u, 0xc3u, 0xddu, 0x36u, 0xc9u, 0xfdu, 0xd4u, 0x53u } },
            { 0x016239d5u, 0x510du, 0x411eu, { 0x8cu, 0xf1u, 0xc3u, 0xd1u, 0xefu, 0xf1u, 0x4cu, 0x33u } }
        };
        return winrt_sys_get_iids_copy(list, 2, c, i);
    }
    return winrt_sys_get_iids_copy(&WINRT_IID_IStorageItemAccessList, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE al_rcn(void *This, HSTRING *n) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    return winrt_sys_make_hstring(
        lst->is_mru ? L"Windows.Storage.AccessCache.StorageItemMostRecentlyUsedList"
                    : L"Windows.Storage.AccessCache.StorageItemAccessList",
        n);
}
static HRESULT STDMETHODCALLTYPE al_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT al_put_entry(AccessListObj *lst, const wchar_t *tok, const wchar_t *path, const wchar_t *meta) {
    access_list_init(lst);
    EnterCriticalSection(&lst->lock);
    for (uint32_t i = 0; i < lst->count; ++i) {
        if (wcscmp(lst->entries[i].token, tok) == 0) {
            winrt_sys_copy_wstr(lst->entries[i].path, UWP_MAX_PATH_CHARS, path);
            winrt_sys_copy_wstr(lst->entries[i].metadata, 256, meta ? meta : L"");
            LeaveCriticalSection(&lst->lock);
            return S_OK;
        }
    }
    if (lst->count >= ACCESS_CACHE_MAX) {
        LeaveCriticalSection(&lst->lock);
        return E_OUTOFMEMORY;
    }
    winrt_sys_copy_wstr(lst->entries[lst->count].token, 128, tok);
    winrt_sys_copy_wstr(lst->entries[lst->count].path, UWP_MAX_PATH_CHARS, path);
    winrt_sys_copy_wstr(lst->entries[lst->count].metadata, 256, meta ? meta : L"");
    lst->count++;
    LeaveCriticalSection(&lst->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE al_add(void *This, IUnknown *file, HSTRING metadata, HSTRING *token) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (!token) return E_POINTER;
    *token = NULL;
    if (!file) return E_INVALIDARG;
    wchar_t path[UWP_MAX_PATH_CHARS];
    HRESULT hr = extract_storage_item_path(file, path, UWP_MAX_PATH_CHARS);
    if (FAILED(hr)) return hr;
    access_list_init(lst);
    uint32_t id = (uint32_t)InterlockedIncrement((volatile LONG *)&lst->next_id);
    wchar_t tok[64];
    swprintf(tok, 64, L"ScarlettToken_%u", id);
    hr = al_put_entry(lst, tok, path, winrt_sys_hstring_buf(metadata, NULL));
    if (FAILED(hr)) return hr;
    return winrt_sys_make_hstring(tok, token);
}
static HRESULT STDMETHODCALLTYPE al_add_def(void *This, IUnknown *file, HSTRING *token) {
    return al_add(This, file, NULL, token);
}
static HRESULT STDMETHODCALLTYPE al_add_or_replace(void *This, HSTRING token, IUnknown *file, HSTRING metadata) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (!file) return E_INVALIDARG;
    const wchar_t *tok = winrt_sys_hstring_buf(token, NULL);
    if (!tok || tok[0] == L'\0') return E_INVALIDARG;
    wchar_t path[UWP_MAX_PATH_CHARS];
    HRESULT hr = extract_storage_item_path(file, path, UWP_MAX_PATH_CHARS);
    if (FAILED(hr)) return hr;
    return al_put_entry(lst, tok, path, winrt_sys_hstring_buf(metadata, NULL));
}
static HRESULT STDMETHODCALLTYPE al_add_or_replace_def(void *This, HSTRING token, IUnknown *file) {
    return al_add_or_replace(This, token, file, NULL);
}

static int al_lookup_path(AccessListObj *lst, HSTRING token, wchar_t *out_path, size_t out_chars) {
    const wchar_t *tok = winrt_sys_hstring_buf(token, NULL);
    if (!tok || tok[0] == L'\0') return 0;
    access_list_init(lst);
    EnterCriticalSection(&lst->lock);
    for (uint32_t i = 0; i < lst->count; ++i) {
        if (wcscmp(lst->entries[i].token, tok) == 0) {
            winrt_sys_copy_wstr(out_path, out_chars, lst->entries[i].path);
            LeaveCriticalSection(&lst->lock);
            return 1;
        }
    }
    LeaveCriticalSection(&lst->lock);
    return 0;
}

static HRESULT STDMETHODCALLTYPE al_get_folder_opts(void *This, HSTRING token, uint32_t options, void **op) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    (void)options;
    if (!op) return E_POINTER;
    *op = NULL;
    wchar_t path[UWP_MAX_PATH_CHARS];
    if (!al_lookup_path(lst, token, path, UWP_MAX_PATH_CHARS)) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_StorageFolder,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }
    void *folder = NULL;
    HRESULT hr = winrt_sys_create_storage_folder(path, &folder);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFolder,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
        (IUnknown *)folder, hr, op);
    if (folder) ((IUnknown *)folder)->lpVtbl->Release((IUnknown *)folder);
    return out_hr;
}
static HRESULT STDMETHODCALLTYPE al_get_folder(void *This, HSTRING token, void **op) {
    return al_get_folder_opts(This, token, 0u, op);
}
static HRESULT STDMETHODCALLTYPE al_get_file_opts(void *This, HSTRING token, uint32_t options, void **op) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    (void)options;
    if (!op) return E_POINTER;
    *op = NULL;
    wchar_t path[UWP_MAX_PATH_CHARS];
    if (!al_lookup_path(lst, token, path, UWP_MAX_PATH_CHARS)) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_StorageFile,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }
    void *file = NULL;
    HRESULT hr = winrt_sys_create_storage_file(path, &file);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFile,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
        (IUnknown *)file, hr, op);
    if (file) ((IUnknown *)file)->lpVtbl->Release((IUnknown *)file);
    return out_hr;
}
static HRESULT STDMETHODCALLTYPE al_get_file(void *This, HSTRING token, void **op) {
    return al_get_file_opts(This, token, 0u, op);
}
static HRESULT STDMETHODCALLTYPE al_get_item_opts(void *This, HSTRING token, uint32_t options, void **op) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    (void)options;
    if (!op) return E_POINTER;
    *op = NULL;
    wchar_t path[UWP_MAX_PATH_CHARS];
    if (!al_lookup_path(lst, token, path, UWP_MAX_PATH_CHARS)) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_IStorageItem,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }
    DWORD attrs = GetFileAttributesW(path);
    void *raw = NULL;
    HRESULT hr = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        ? winrt_sys_create_storage_folder(path, &raw)
        : winrt_sys_create_storage_file(path, &raw);
    IUnknown *item_iface = NULL;
    if (SUCCEEDED(hr) && raw) {
        hr = ((IUnknown *)raw)->lpVtbl->QueryInterface((IUnknown *)raw, &WINRT_IID_IStorageItem, (void **)&item_iface);
        ((IUnknown *)raw)->lpVtbl->Release((IUnknown *)raw);
    }
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_IStorageItem,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>",
        item_iface, hr, op);
    if (item_iface) item_iface->lpVtbl->Release(item_iface);
    return out_hr;
}
static HRESULT STDMETHODCALLTYPE al_get_item(void *This, HSTRING token, void **op) {
    return al_get_item_opts(This, token, 0u, op);
}
static HRESULT STDMETHODCALLTYPE al_remove(void *This, HSTRING token) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    const wchar_t *tok = winrt_sys_hstring_buf(token, NULL);
    access_list_init(lst);
    EnterCriticalSection(&lst->lock);
    for (uint32_t i = 0; i < lst->count; ++i) {
        if (wcscmp(lst->entries[i].token, tok) == 0) {
            for (uint32_t j = i + 1u; j < lst->count; ++j) lst->entries[j - 1u] = lst->entries[j];
            lst->count--;
            break;
        }
    }
    LeaveCriticalSection(&lst->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE al_contains(void *This, HSTRING token, boolean *val) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (!val) return E_POINTER;
    wchar_t dummy[UWP_MAX_PATH_CHARS];
    *val = al_lookup_path(lst, token, dummy, UWP_MAX_PATH_CHARS) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE al_clear(void *This) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    access_list_init(lst);
    EnterCriticalSection(&lst->lock);
    lst->count = 0;
    LeaveCriticalSection(&lst->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE al_check_access(void *This, IUnknown *file, boolean *val) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (!val) return E_POINTER;
    *val = 0;
    if (!file) return E_INVALIDARG;
    wchar_t path[UWP_MAX_PATH_CHARS];
    if (FAILED(extract_storage_item_path(file, path, UWP_MAX_PATH_CHARS))) return S_OK;
    access_list_init(lst);
    EnterCriticalSection(&lst->lock);
    for (uint32_t i = 0; i < lst->count; ++i) {
        if (wcscmp(lst->entries[i].path, path) == 0) {
            *val = 1;
            break;
        }
    }
    LeaveCriticalSection(&lst->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE al_get_entries(void *This, void **val) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (!val) return E_POINTER;
    *val = NULL;
    access_list_init(lst);
    AccessEntryViewObj *view = (AccessEntryViewObj *)winrt_sys_alloc(sizeof(*view));
    if (!view) return E_OUTOFMEMORY;
    view->lpVtblView = &g_aev_vtbl;
    view->lpVtblIter = &g_aev_iter_vtbl;
    view->ref_count = 1;
    EnterCriticalSection(&lst->lock);
    view->count = lst->count;
    if (view->count > 0u) {
        view->records = (AccessCacheRecord *)winrt_sys_alloc(sizeof(AccessCacheRecord) * view->count);
        if (!view->records) {
            LeaveCriticalSection(&lst->lock);
            winrt_sys_free(view);
            return E_OUTOFMEMORY;
        }
        memcpy(view->records, lst->entries, sizeof(AccessCacheRecord) * view->count);
    }
    LeaveCriticalSection(&lst->lock);
    *val = &view->lpVtblView;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE al_get_max(void *This, uint32_t *val) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblList, This);
    if (!val) return E_POINTER;
    *val = lst->is_mru ? 25u : 1000u;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mru_qi(void *This, REFIID riid, void **ppv) {
    return al_qi_impl(WINRT_SYS_FROM(AccessListObj, lpVtblMru, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE mru_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE mru_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE mru_getiids(void *This, ULONG *c, IID **i) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblMru, This);
    return al_getiids(&lst->lpVtblList, c, i);
}
static HRESULT STDMETHODCALLTYPE mru_rcn(void *This, HSTRING *n) {
    AccessListObj *lst = WINRT_SYS_FROM(AccessListObj, lpVtblMru, This);
    return al_rcn(&lst->lpVtblList, n);
}
static HRESULT STDMETHODCALLTYPE mru_add_removed(void *This, void *h, EventRegistrationToken *tok) {
    (void)This; (void)h;
    if (!tok) return E_POINTER;
    tok->value = 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mru_rem_removed(void *This, EventRegistrationToken tok) {
    (void)This; (void)tok;
    return S_OK;
}

static const WinrtStorageItemAccessListVtbl g_al_vtbl = {
    al_qi, al_addref, al_release, al_getiids, al_rcn, al_tl,
    al_add_def, al_add, al_add_or_replace_def, al_add_or_replace,
    al_get_item, al_get_file, al_get_folder,
    al_get_item_opts, al_get_file_opts, al_get_folder_opts,
    al_remove, al_contains, al_clear, al_check_access,
    al_get_entries, al_get_max
};
static const WinrtMruListVtbl g_mru_vtbl = {
    mru_qi, mru_addref, mru_release, mru_getiids, mru_rcn, al_tl,
    mru_add_removed, mru_rem_removed
};

static AccessListObj g_future_access_list = {
    &g_al_vtbl, &g_mru_vtbl, 0, 0, { 0 }, 0, 0, { { { 0 }, { 0 }, { 0 } } }
};
static AccessListObj g_mru_access_list = {
    &g_al_vtbl, &g_mru_vtbl, 1, 0, { 0 }, 0, 0, { { { 0 }, { 0 }, { 0 } } }
};

typedef struct WinrtStorageAppPermsStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_FutureAccessList)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_MostRecentlyUsedList)(void *This, void **value);
} WinrtStorageAppPermsStaticsVtbl;

typedef struct StorageAppPermsFactory {
    const WinrtActivationFactoryVtbl       *lpVtblAct;
    const WinrtStorageAppPermsStaticsVtbl  *lpVtblStatics;
} StorageAppPermsFactory;

static StorageAppPermsFactory g_sap_factory;

static HRESULT STDMETHODCALLTYPE sap_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_sap_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IStorageApplicationPermissionsStatics)) {
        *ppv = &g_sap_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sap_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE sap_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE sap_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x4391dfaau, 0xd033u, 0x48f9u, { 0x80u, 0x60u, 0x3eu, 0xc8u, 0x47u, 0xd2u, 0xe3u, 0xf1u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE sap_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE sap_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sap_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("StorageApplicationPermissions::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE sap_get_fal(void *This, void **val) {
    (void)This;
    access_list_init(&g_future_access_list);
    return al_qi_impl(&g_future_access_list, &WINRT_IID_IStorageItemAccessList, val);
}
static HRESULT STDMETHODCALLTYPE sap_get_mru(void *This, void **val) {
    (void)This;
    access_list_init(&g_mru_access_list);
    return al_qi_impl(&g_mru_access_list, &WINRT_IID_IStorageItemMostRecentlyUsedList, val);
}

static const WinrtActivationFactoryVtbl g_sap_act_vtbl = {
    sap_fac_qi, sap_fac_addref, sap_fac_release, sap_fac_getiids, sap_fac_rcn, sap_fac_tl, sap_fac_act
};
static const WinrtStorageAppPermsStaticsVtbl g_sap_stat_vtbl = {
    sap_fac_qi, sap_fac_addref, sap_fac_release, sap_fac_getiids, sap_fac_rcn, sap_fac_tl,
    sap_get_fal, sap_get_mru
};
static StorageAppPermsFactory g_sap_factory = { &g_sap_act_vtbl, &g_sap_stat_vtbl };

HRESULT winrt_sys_get_storage_app_perms_factory(REFIID iid, void **out_factory) {
    return sap_fac_qi(&g_sap_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 4. Windows.Security.Cryptography.CryptographicBuffer + IBuffer
 * ============================================================================ */

typedef struct WinrtBufferVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Capacity)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *get_Length)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *put_Length)(void *This, uint32_t value);
} WinrtBufferVtbl;

typedef struct WinrtBufferByteAccessVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Buffer)(void *This, uint8_t **value);
} WinrtBufferByteAccessVtbl;

typedef struct WinrtBufferObj {
    const WinrtBufferVtbl           *lpVtblBuf;
    const WinrtBufferByteAccessVtbl *lpVtblAccess;
    volatile LONG ref_count;
    uint32_t capacity;
    uint32_t length;
    uint8_t *data;
} WinrtBufferObj;

static HRESULT buf_qi_impl(WinrtBufferObj *b, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IBuffer)) {
        InterlockedIncrement(&b->ref_count);
        *ppv = &b->lpVtblBuf;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IBufferByteAccess)) {
        InterlockedIncrement(&b->ref_count);
        *ppv = &b->lpVtblAccess;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG buf_release_impl(WinrtBufferObj *b) {
    LONG rc = InterlockedDecrement(&b->ref_count);
    if (rc == 0) {
        if (b->data) winrt_sys_free(b->data);
        winrt_sys_free(b);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE buf_qi(void *This, REFIID riid, void **ppv) {
    return buf_qi_impl(WINRT_SYS_FROM(WinrtBufferObj, lpVtblBuf, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE buf_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtBufferObj, lpVtblBuf, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE buf_release(void *This) {
    return buf_release_impl(WINRT_SYS_FROM(WinrtBufferObj, lpVtblBuf, This));
}
static HRESULT STDMETHODCALLTYPE buf_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IBuffer, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE buf_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.Streams.Buffer", n);
}
static HRESULT STDMETHODCALLTYPE buf_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE buf_get_cap(void *This, uint32_t *val) {
    if (!val) return E_POINTER;
    *val = WINRT_SYS_FROM(WinrtBufferObj, lpVtblBuf, This)->capacity;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE buf_get_len(void *This, uint32_t *val) {
    if (!val) return E_POINTER;
    *val = WINRT_SYS_FROM(WinrtBufferObj, lpVtblBuf, This)->length;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE buf_put_len(void *This, uint32_t val) {
    WinrtBufferObj *b = WINRT_SYS_FROM(WinrtBufferObj, lpVtblBuf, This);
    if (val > b->capacity) return E_INVALIDARG;
    b->length = val;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ba_qi(void *This, REFIID riid, void **ppv) {
    return buf_qi_impl(WINRT_SYS_FROM(WinrtBufferObj, lpVtblAccess, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE ba_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(WinrtBufferObj, lpVtblAccess, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE ba_release(void *This) {
    return buf_release_impl(WINRT_SYS_FROM(WinrtBufferObj, lpVtblAccess, This));
}
static HRESULT STDMETHODCALLTYPE ba_buffer(void *This, uint8_t **val) {
    WinrtBufferObj *b = WINRT_SYS_FROM(WinrtBufferObj, lpVtblAccess, This);
    if (!val) return E_POINTER;
    *val = b->data;
    return S_OK;
}

static const WinrtBufferVtbl g_buf_vtbl = {
    buf_qi, buf_addref, buf_release, buf_getiids, buf_rcn, buf_tl,
    buf_get_cap, buf_get_len, buf_put_len
};
static const WinrtBufferByteAccessVtbl g_ba_vtbl = {
    ba_qi, ba_addref, ba_release, ba_buffer
};

HRESULT winrt_sys_create_buffer(const uint8_t *bytes, uint32_t len, void **out_buf) {
    if (!out_buf) return E_POINTER;
    *out_buf = NULL;
    WinrtBufferObj *b = (WinrtBufferObj *)winrt_sys_alloc(sizeof(*b));
    if (!b) return E_OUTOFMEMORY;
    b->lpVtblBuf = &g_buf_vtbl;
    b->lpVtblAccess = &g_ba_vtbl;
    b->ref_count = 1;
    b->capacity = len;
    b->length = len;
    b->data = (uint8_t *)winrt_sys_alloc(len > 0u ? len : 1u);
    if (!b->data) {
        winrt_sys_free(b);
        return E_OUTOFMEMORY;
    }
    if (bytes && len > 0u) memcpy(b->data, bytes, len);
    *out_buf = &b->lpVtblBuf;
    return S_OK;
}

HRESULT winrt_sys_read_buffer(void *buf_iface, uint8_t **out_ptr, uint32_t *out_len) {
    if (!out_ptr || !out_len) return E_POINTER;
    *out_ptr = NULL;
    *out_len = 0;
    if (!buf_iface) return E_INVALIDARG;
    typedef struct { const WinrtBufferVtbl *lpVtbl; } BufProxy;
    typedef struct { const WinrtBufferByteAccessVtbl *lpVtbl; } BaProxy;
    BufProxy *bp = (BufProxy *)buf_iface;
    uint32_t len = 0;
    HRESULT hr = bp->lpVtbl->get_Length(bp, &len);
    if (FAILED(hr)) return hr;
    BaProxy *ba = NULL;
    hr = bp->lpVtbl->QueryInterface(bp, &WINRT_IID_IBufferByteAccess, (void **)&ba);
    if (FAILED(hr) || !ba) return hr;
    uint8_t *ptr = NULL;
    hr = ba->lpVtbl->Buffer(ba, &ptr);
    ba->lpVtbl->Release(ba);
    if (FAILED(hr)) return hr;
    *out_ptr = ptr;
    *out_len = len;
    return S_OK;
}

static HRESULT create_winrt_buffer(const uint8_t *bytes, uint32_t len, void **out_buf) {
    return winrt_sys_create_buffer(bytes, len, out_buf);
}

static HRESULT read_winrt_buffer(void *buf_iface, uint8_t **out_ptr, uint32_t *out_len) {
    return winrt_sys_read_buffer(buf_iface, out_ptr, out_len);
}

static uint64_t g_prng_state = 0x9e3779b97f4a7c15ULL;
static uint32_t next_u32_rand(void) {
    uint64_t x = g_prng_state ^ (uint64_t)GetTickCount64();
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_prng_state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

typedef struct WinrtCryptographicBufferStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *Compare)(void *This, void *obj1, void *obj2, boolean *is_equal);
    HRESULT (STDMETHODCALLTYPE *GenerateRandom)(void *This, uint32_t length, void **buffer);
    HRESULT (STDMETHODCALLTYPE *GenerateRandomNumber)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *CreateFromByteArray)(void *This, uint32_t value_size, uint8_t *value, void **buffer);
    HRESULT (STDMETHODCALLTYPE *CopyToByteArray)(void *This, void *buffer, uint32_t *value_size, uint8_t **value);
    HRESULT (STDMETHODCALLTYPE *DecodeFromHexString)(void *This, HSTRING value, void **buffer);
    HRESULT (STDMETHODCALLTYPE *EncodeToHexString)(void *This, void *buffer, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *DecodeFromBase64String)(void *This, HSTRING value, void **buffer);
    HRESULT (STDMETHODCALLTYPE *EncodeToBase64String)(void *This, void *buffer, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *ConvertStringToBinary)(void *This, HSTRING value, int32_t encoding, void **buffer);
    HRESULT (STDMETHODCALLTYPE *ConvertBinaryToString)(void *This, int32_t encoding, void *buffer, HSTRING *value);
} WinrtCryptographicBufferStaticsVtbl;

typedef struct CryptoBufFactory {
    const WinrtActivationFactoryVtbl          *lpVtblAct;
    const WinrtCryptographicBufferStaticsVtbl *lpVtblStatics;
} CryptoBufFactory;

static CryptoBufFactory g_cb_factory;

static HRESULT STDMETHODCALLTYPE cb_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_cb_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_ICryptographicBufferStatics)) {
        *ppv = &g_cb_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cb_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE cb_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE cb_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x320b7e22u, 0x3cb0u, 0x4cdfu, { 0x86u, 0x63u, 0x1du, 0x28u, 0x91u, 0x00u, 0x65u, 0xebu } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE cb_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE cb_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("CryptographicBuffer::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE cb_compare(void *This, void *o1, void *o2, boolean *eq) {
    (void)This;
    if (!eq) return E_POINTER;
    *eq = 0;
    uint8_t *p1 = NULL, *p2 = NULL;
    uint32_t l1 = 0, l2 = 0;
    HRESULT hr = read_winrt_buffer(o1, &p1, &l1);
    if (FAILED(hr)) return hr;
    hr = read_winrt_buffer(o2, &p2, &l2);
    if (FAILED(hr)) return hr;
    *eq = (l1 == l2 && (l1 == 0u || memcmp(p1, p2, l1) == 0)) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_gen_rand(void *This, uint32_t len, void **buf) {
    (void)This;
    if (!buf) return E_POINTER;
    uint8_t *tmp = (uint8_t *)winrt_sys_alloc(len > 0u ? len : 1u);
    if (!tmp) return E_OUTOFMEMORY;
    for (uint32_t i = 0; i < len; ++i) tmp[i] = (uint8_t)(next_u32_rand() & 0xFFu);
    HRESULT hr = create_winrt_buffer(tmp, len, buf);
    winrt_sys_free(tmp);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cb_gen_rand_num(void *This, uint32_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = next_u32_rand();
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_create_from_bytes(void *This, uint32_t sz, uint8_t *val, void **buf) {
    (void)This;
    if (!buf) return E_POINTER;
    if (sz > 0u && !val) return E_INVALIDARG;
    return create_winrt_buffer(val, sz, buf);
}
static HRESULT STDMETHODCALLTYPE cb_copy_to_bytes(void *This, void *buf, uint32_t *sz, uint8_t **val) {
    (void)This;
    if (!sz || !val) return E_POINTER;
    *sz = 0;
    *val = NULL;
    uint8_t *p = NULL;
    uint32_t len = 0;
    HRESULT hr = read_winrt_buffer(buf, &p, &len);
    if (FAILED(hr)) return hr;
    if (len == 0u) return S_OK;
    uint8_t *out = (uint8_t *)winrt_sys_cotask_alloc(len);
    if (!out) return E_OUTOFMEMORY;
    memcpy(out, p, len);
    *sz = len;
    *val = out;
    return S_OK;
}
static int hex_val(wchar_t c) {
    if (c >= L'0' && c <= L'9') return (int)(c - L'0');
    if (c >= L'a' && c <= L'f') return 10 + (int)(c - L'a');
    if (c >= L'A' && c <= L'F') return 10 + (int)(c - L'A');
    return -1;
}
static HRESULT STDMETHODCALLTYPE cb_decode_hex(void *This, HSTRING val, void **buf) {
    (void)This;
    if (!buf) return E_POINTER;
    uint32_t slen = 0;
    const wchar_t *s = winrt_sys_hstring_buf(val, &slen);
    if ((slen & 1u) != 0u) return E_INVALIDARG;
    uint32_t blen = slen / 2u;
    uint8_t *tmp = (uint8_t *)winrt_sys_alloc(blen > 0u ? blen : 1u);
    if (!tmp) return E_OUTOFMEMORY;
    for (uint32_t i = 0; i < blen; ++i) {
        int hi = hex_val(s[i * 2u]);
        int lo = hex_val(s[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            winrt_sys_free(tmp);
            return E_INVALIDARG;
        }
        tmp[i] = (uint8_t)((hi << 4) | lo);
    }
    HRESULT hr = create_winrt_buffer(tmp, blen, buf);
    winrt_sys_free(tmp);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cb_encode_hex(void *This, void *buf, HSTRING *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    uint8_t *p = NULL;
    uint32_t len = 0;
    HRESULT hr = read_winrt_buffer(buf, &p, &len);
    if (FAILED(hr)) return hr;
    static const wchar_t kHex[] = L"0123456789abcdef";
    wchar_t *ws = (wchar_t *)winrt_sys_alloc(((size_t)len * 2u + 1u) * sizeof(wchar_t));
    if (!ws) return E_OUTOFMEMORY;
    for (uint32_t i = 0; i < len; ++i) {
        ws[i * 2u] = kHex[(p[i] >> 4) & 0xFu];
        ws[i * 2u + 1u] = kHex[p[i] & 0xFu];
    }
    ws[len * 2u] = L'\0';
    hr = WindowsCreateString(ws, len * 2u, val);
    winrt_sys_free(ws);
    return hr;
}
static int b64_val(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return (int)(c - L'A');
    if (c >= L'a' && c <= L'z') return 26 + (int)(c - L'a');
    if (c >= L'0' && c <= L'9') return 52 + (int)(c - L'0');
    if (c == L'+') return 62;
    if (c == L'/') return 63;
    return -1;
}
static HRESULT STDMETHODCALLTYPE cb_decode_b64(void *This, HSTRING val, void **buf) {
    (void)This;
    if (!buf) return E_POINTER;
    uint32_t slen = 0;
    const wchar_t *s = winrt_sys_hstring_buf(val, &slen);
    uint8_t *tmp = (uint8_t *)winrt_sys_alloc((slen * 3u) / 4u + 4u);
    if (!tmp) return E_OUTOFMEMORY;
    uint32_t out = 0, acc = 0, bits = 0;
    for (uint32_t i = 0; i < slen; ++i) {
        if (s[i] == L'=' || s[i] == L' ' || s[i] == L'\r' || s[i] == L'\n') continue;
        int v = b64_val(s[i]);
        if (v < 0) {
            winrt_sys_free(tmp);
            return E_INVALIDARG;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6u;
        if (bits >= 8u) {
            bits -= 8u;
            tmp[out++] = (uint8_t)((acc >> bits) & 0xFFu);
        }
    }
    HRESULT hr = create_winrt_buffer(tmp, out, buf);
    winrt_sys_free(tmp);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cb_encode_b64(void *This, void *buf, HSTRING *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    uint8_t *p = NULL;
    uint32_t len = 0;
    HRESULT hr = read_winrt_buffer(buf, &p, &len);
    if (FAILED(hr)) return hr;
    static const wchar_t kB64[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t out_len = ((len + 2u) / 3u) * 4u;
    wchar_t *ws = (wchar_t *)winrt_sys_alloc(((size_t)out_len + 1u) * sizeof(wchar_t));
    if (!ws) return E_OUTOFMEMORY;
    uint32_t o = 0;
    for (uint32_t i = 0; i < len; i += 3u) {
        uint32_t v = ((uint32_t)p[i]) << 16;
        if (i + 1u < len) v |= ((uint32_t)p[i + 1u]) << 8;
        if (i + 2u < len) v |= (uint32_t)p[i + 2u];
        ws[o++] = kB64[(v >> 18) & 0x3Fu];
        ws[o++] = kB64[(v >> 12) & 0x3Fu];
        ws[o++] = (i + 1u < len) ? kB64[(v >> 6) & 0x3Fu] : L'=';
        ws[o++] = (i + 2u < len) ? kB64[v & 0x3Fu] : L'=';
    }
    ws[o] = L'\0';
    hr = WindowsCreateString(ws, o, val);
    winrt_sys_free(ws);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cb_str_to_bin(void *This, HSTRING val, int32_t enc, void **buf) {
    (void)This;
    if (!buf) return E_POINTER;
    uint32_t slen = 0;
    const wchar_t *s = winrt_sys_hstring_buf(val, &slen);
    if (enc == 0 /* Utf8 */) {
        if (slen == 0u) return create_winrt_buffer(NULL, 0u, buf);
        int need = WideCharToMultiByte(CP_UTF8, 0, s, (int)slen, NULL, 0, NULL, NULL);
        if (need <= 0) return create_winrt_buffer(NULL, 0u, buf);
        uint8_t *tmp = (uint8_t *)winrt_sys_alloc((size_t)need);
        if (!tmp) return E_OUTOFMEMORY;
        WideCharToMultiByte(CP_UTF8, 0, s, (int)slen, (char *)tmp, need, NULL, NULL);
        HRESULT hr = create_winrt_buffer(tmp, (uint32_t)need, buf);
        winrt_sys_free(tmp);
        return hr;
    }
    uint32_t blen = slen * 2u;
    uint8_t *tmp = (uint8_t *)winrt_sys_alloc(blen > 0u ? blen : 1u);
    if (!tmp) return E_OUTOFMEMORY;
    for (uint32_t i = 0; i < slen; ++i) {
        uint16_t ch = (uint16_t)s[i];
        if (enc == 2 /* Utf16BE */) {
            tmp[i * 2u] = (uint8_t)(ch >> 8);
            tmp[i * 2u + 1u] = (uint8_t)(ch & 0xFFu);
        } else {
            tmp[i * 2u] = (uint8_t)(ch & 0xFFu);
            tmp[i * 2u + 1u] = (uint8_t)(ch >> 8);
        }
    }
    HRESULT hr = create_winrt_buffer(tmp, blen, buf);
    winrt_sys_free(tmp);
    return hr;
}
static HRESULT STDMETHODCALLTYPE cb_bin_to_str(void *This, int32_t enc, void *buf, HSTRING *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    uint8_t *p = NULL;
    uint32_t len = 0;
    HRESULT hr = read_winrt_buffer(buf, &p, &len);
    if (FAILED(hr)) return hr;
    if (len == 0u) return WindowsCreateString(L"", 0u, val);
    if (enc == 0 /* Utf8 */) {
        int need = MultiByteToWideChar(CP_UTF8, 0, (const char *)p, (int)len, NULL, 0);
        if (need <= 0) return WindowsCreateString(L"", 0u, val);
        wchar_t *ws = (wchar_t *)winrt_sys_alloc(((size_t)need + 1u) * sizeof(wchar_t));
        if (!ws) return E_OUTOFMEMORY;
        MultiByteToWideChar(CP_UTF8, 0, (const char *)p, (int)len, ws, need);
        ws[need] = L'\0';
        hr = WindowsCreateString(ws, (uint32_t)need, val);
        winrt_sys_free(ws);
        return hr;
    }
    uint32_t wlen = len / 2u;
    wchar_t *ws = (wchar_t *)winrt_sys_alloc(((size_t)wlen + 1u) * sizeof(wchar_t));
    if (!ws) return E_OUTOFMEMORY;
    for (uint32_t i = 0; i < wlen; ++i) {
        ws[i] = (enc == 2)
            ? (wchar_t)(((uint16_t)p[i * 2u] << 8) | p[i * 2u + 1u])
            : (wchar_t)(((uint16_t)p[i * 2u + 1u] << 8) | p[i * 2u]);
    }
    ws[wlen] = L'\0';
    hr = WindowsCreateString(ws, wlen, val);
    winrt_sys_free(ws);
    return hr;
}

static const WinrtActivationFactoryVtbl g_cb_act_vtbl = {
    cb_fac_qi, cb_fac_addref, cb_fac_release, cb_fac_getiids, cb_fac_rcn, cb_fac_tl, cb_fac_act
};
static const WinrtCryptographicBufferStaticsVtbl g_cb_stat_vtbl = {
    cb_fac_qi, cb_fac_addref, cb_fac_release, cb_fac_getiids, cb_fac_rcn, cb_fac_tl,
    cb_compare, cb_gen_rand, cb_gen_rand_num, cb_create_from_bytes, cb_copy_to_bytes,
    cb_decode_hex, cb_encode_hex, cb_decode_b64, cb_encode_b64, cb_str_to_bin, cb_bin_to_str
};
static CryptoBufFactory g_cb_factory = { &g_cb_act_vtbl, &g_cb_stat_vtbl };

HRESULT winrt_sys_get_cryptographic_buffer_factory(REFIID iid, void **out_factory) {
    return cb_fac_qi(&g_cb_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 5. Windows.UI.Popups.UICommand & Windows.UI.Popups.PopupMenu
 * ============================================================================ */

typedef struct WinrtPoint {
    float X;
    float Y;
} WinrtPoint;

typedef struct WinrtRect {
    float X;
    float Y;
    float Width;
    float Height;
} WinrtRect;

typedef struct WinrtUICommandInvokedHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *command);
} WinrtUICommandInvokedHandlerVtbl;

typedef struct WinrtUICommandInvokedHandler {
    const WinrtUICommandInvokedHandlerVtbl *lpVtbl;
} WinrtUICommandInvokedHandler;

typedef struct WinrtUICommandVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Label)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *put_Label)(void *This, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *get_Invoked)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_Invoked)(void *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, IInspectable **value);
    HRESULT (STDMETHODCALLTYPE *put_Id)(void *This, IInspectable *value);
} WinrtUICommandVtbl;

typedef struct UICommandObj {
    const WinrtUICommandVtbl *lpVtbl;
    volatile LONG ref_count;
    HSTRING label;
    WinrtUICommandInvokedHandler *invoked;
    IInspectable *id;
} UICommandObj;

static HRESULT STDMETHODCALLTYPE uic_qi(void *This, REFIID riid, void **ppv) {
    UICommandObj *o = (UICommandObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IUICommand)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE uic_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((UICommandObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE uic_release(void *This) {
    UICommandObj *o = (UICommandObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) {
        if (o->label) WindowsDeleteString(o->label);
        if (o->invoked) o->invoked->lpVtbl->Release(o->invoked);
        if (o->id) o->id->lpVtbl->Release(o->id);
        winrt_sys_free(o);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE uic_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IUICommand, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE uic_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.UI.Popups.UICommand", n);
}
static HRESULT STDMETHODCALLTYPE uic_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uic_get_label(void *This, HSTRING *val) {
    return winrt_sys_dup_hstring(((UICommandObj *)This)->label, val);
}
static HRESULT STDMETHODCALLTYPE uic_put_label(void *This, HSTRING val) {
    UICommandObj *o = (UICommandObj *)This;
    if (o->label) WindowsDeleteString(o->label);
    o->label = NULL;
    return winrt_sys_dup_hstring(val, &o->label);
}
static HRESULT STDMETHODCALLTYPE uic_get_invoked(void *This, void **val) {
    UICommandObj *o = (UICommandObj *)This;
    if (!val) return E_POINTER;
    *val = o->invoked;
    if (o->invoked) o->invoked->lpVtbl->AddRef(o->invoked);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uic_put_invoked(void *This, void *val) {
    UICommandObj *o = (UICommandObj *)This;
    if (o->invoked) o->invoked->lpVtbl->Release(o->invoked);
    o->invoked = (WinrtUICommandInvokedHandler *)val;
    if (o->invoked) o->invoked->lpVtbl->AddRef(o->invoked);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uic_get_id(void *This, IInspectable **val) {
    UICommandObj *o = (UICommandObj *)This;
    if (!val) return E_POINTER;
    *val = o->id;
    if (o->id) o->id->lpVtbl->AddRef(o->id);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uic_put_id(void *This, IInspectable *val) {
    UICommandObj *o = (UICommandObj *)This;
    if (o->id) o->id->lpVtbl->Release(o->id);
    o->id = val;
    if (o->id) o->id->lpVtbl->AddRef(o->id);
    return S_OK;
}

static const WinrtUICommandVtbl g_uic_vtbl = {
    uic_qi, uic_addref, uic_release, uic_getiids, uic_rcn, uic_tl,
    uic_get_label, uic_put_label, uic_get_invoked, uic_put_invoked,
    uic_get_id, uic_put_id
};

static HRESULT create_uicommand_obj(HSTRING label, void *action, IInspectable *cmd_id, void **out_cmd) {
    if (!out_cmd) return E_POINTER;
    *out_cmd = NULL;
    UICommandObj *o = (UICommandObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return E_OUTOFMEMORY;
    o->lpVtbl = &g_uic_vtbl;
    o->ref_count = 1;
    if (label) winrt_sys_dup_hstring(label, &o->label);
    if (action) {
        o->invoked = (WinrtUICommandInvokedHandler *)action;
        o->invoked->lpVtbl->AddRef(o->invoked);
    }
    if (cmd_id) {
        o->id = cmd_id;
        o->id->lpVtbl->AddRef(o->id);
    }
    *out_cmd = o;
    return S_OK;
}

typedef struct WinrtUICommandFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *Create)(void *This, HSTRING label, void **instance);
    HRESULT (STDMETHODCALLTYPE *CreateWithHandler)(void *This, HSTRING label, void *action, void **instance);
    HRESULT (STDMETHODCALLTYPE *CreateWithHandlerAndId)(void *This, HSTRING label, void *action, IInspectable *id, void **instance);
} WinrtUICommandFactoryVtbl;

typedef struct UICommandFactoryObj {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtUICommandFactoryVtbl  *lpVtblFac;
} UICommandFactoryObj;

static UICommandFactoryObj g_uic_factory;

static HRESULT STDMETHODCALLTYPE uic_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_uic_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUICommandFactory)) {
        *ppv = &g_uic_factory.lpVtblFac;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE uic_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE uic_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE uic_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0xa21a8189u, 0x26b0u, 0x4676u, { 0xaeu, 0x94u, 0x54u, 0x04u, 0x1bu, 0xc1u, 0x25u, 0xe8u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE uic_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE uic_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE uic_fac_act(void *This, IInspectable **inst) {
    (void)This;
    return create_uicommand_obj(NULL, NULL, NULL, (void **)inst);
}
static HRESULT STDMETHODCALLTYPE uic_fac_create(void *This, HSTRING label, void **inst) {
    (void)This;
    return create_uicommand_obj(label, NULL, NULL, inst);
}
static HRESULT STDMETHODCALLTYPE uic_fac_create_h(void *This, HSTRING label, void *act, void **inst) {
    (void)This;
    return create_uicommand_obj(label, act, NULL, inst);
}
static HRESULT STDMETHODCALLTYPE uic_fac_create_hid(void *This, HSTRING label, void *act, IInspectable *id, void **inst) {
    (void)This;
    return create_uicommand_obj(label, act, id, inst);
}

static const WinrtActivationFactoryVtbl g_uic_act_vtbl = {
    uic_fac_qi, uic_fac_addref, uic_fac_release, uic_fac_getiids, uic_fac_rcn, uic_fac_tl, uic_fac_act
};
static const WinrtUICommandFactoryVtbl g_uic_fac_vtbl = {
    uic_fac_qi, uic_fac_addref, uic_fac_release, uic_fac_getiids, uic_fac_rcn, uic_fac_tl,
    uic_fac_create, uic_fac_create_h, uic_fac_create_hid
};
static UICommandFactoryObj g_uic_factory = { &g_uic_act_vtbl, &g_uic_fac_vtbl };

HRESULT winrt_sys_get_uicommand_factory(REFIID iid, void **out_factory) {
    return uic_fac_qi(&g_uic_factory.lpVtblAct, iid, out_factory);
}

/* IVector<IUICommand*> for PopupMenu::Commands */
typedef struct WinrtVectorPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t index, IUnknown **item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
    HRESULT (STDMETHODCALLTYPE *GetView)(void *This, void **view);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, IUnknown *value, uint32_t *index, boolean *found);
    HRESULT (STDMETHODCALLTYPE *SetAt)(void *This, uint32_t index, IUnknown *item);
    HRESULT (STDMETHODCALLTYPE *InsertAt)(void *This, uint32_t index, IUnknown *item);
    HRESULT (STDMETHODCALLTYPE *RemoveAt)(void *This, uint32_t index);
    HRESULT (STDMETHODCALLTYPE *Append)(void *This, IUnknown *item);
    HRESULT (STDMETHODCALLTYPE *RemoveAtEnd)(void *This);
    HRESULT (STDMETHODCALLTYPE *Clear)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t startIndex, uint32_t capacity, IUnknown **items, uint32_t *actual);
    HRESULT (STDMETHODCALLTYPE *ReplaceAll)(void *This, uint32_t count, IUnknown **items);
} WinrtVectorPtrVtbl;

#define CMD_VEC_MAX 32u

typedef struct CmdVecObj {
    const WinrtVectorPtrVtbl *lpVtbl;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    uint32_t count;
    IUnknown *items[CMD_VEC_MAX];
} CmdVecObj;

static HRESULT STDMETHODCALLTYPE cvec_qi(void *This, REFIID riid, void **ppv) {
    CmdVecObj *v = (CmdVecObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IVector_IUICommand)) {
        InterlockedIncrement(&v->ref_count);
        *ppv = v;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cvec_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((CmdVecObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE cvec_release(void *This) {
    CmdVecObj *v = (CmdVecObj *)This;
    LONG rc = InterlockedDecrement(&v->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < v->count; ++i) {
            if (v->items[i]) v->items[i]->lpVtbl->Release(v->items[i]);
        }
        DeleteCriticalSection(&v->lock);
        winrt_sys_free(v);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE cvec_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IVector_IUICommand, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE cvec_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IVector`1<Windows.UI.Popups.IUICommand>", n);
}
static HRESULT STDMETHODCALLTYPE cvec_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_get_at(void *This, uint32_t idx, IUnknown **item) {
    CmdVecObj *v = (CmdVecObj *)This;
    if (!item) return E_POINTER;
    *item = NULL;
    EnterCriticalSection(&v->lock);
    if (idx >= v->count) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    *item = v->items[idx];
    if (*item) (*item)->lpVtbl->AddRef(*item);
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_get_size(void *This, uint32_t *sz) {
    CmdVecObj *v = (CmdVecObj *)This;
    if (!sz) return E_POINTER;
    EnterCriticalSection(&v->lock);
    *sz = v->count;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_get_view(void *This, void **view) {
    if (view) *view = NULL;
    return winrt_sys_unimplemented("IVector<IUICommand>::GetView", This);
}
static HRESULT STDMETHODCALLTYPE cvec_index_of(void *This, IUnknown *val, uint32_t *idx, boolean *found) {
    CmdVecObj *v = (CmdVecObj *)This;
    if (!idx || !found) return E_POINTER;
    *idx = 0;
    *found = 0;
    EnterCriticalSection(&v->lock);
    for (uint32_t i = 0; i < v->count; ++i) {
        if (v->items[i] == val) {
            *idx = i;
            *found = 1;
            break;
        }
    }
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_set_at(void *This, uint32_t idx, IUnknown *item) {
    CmdVecObj *v = (CmdVecObj *)This;
    EnterCriticalSection(&v->lock);
    if (idx >= v->count) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    if (item) item->lpVtbl->AddRef(item);
    if (v->items[idx]) v->items[idx]->lpVtbl->Release(v->items[idx]);
    v->items[idx] = item;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_insert_at(void *This, uint32_t idx, IUnknown *item) {
    CmdVecObj *v = (CmdVecObj *)This;
    EnterCriticalSection(&v->lock);
    if (idx > v->count) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    if (v->count >= CMD_VEC_MAX) {
        LeaveCriticalSection(&v->lock);
        return E_OUTOFMEMORY;
    }
    if (item) item->lpVtbl->AddRef(item);
    for (uint32_t i = v->count; i > idx; --i) v->items[i] = v->items[i - 1u];
    v->items[idx] = item;
    v->count++;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_remove_at(void *This, uint32_t idx) {
    CmdVecObj *v = (CmdVecObj *)This;
    EnterCriticalSection(&v->lock);
    if (idx >= v->count) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    if (v->items[idx]) v->items[idx]->lpVtbl->Release(v->items[idx]);
    for (uint32_t i = idx + 1u; i < v->count; ++i) v->items[i - 1u] = v->items[i];
    v->count--;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_append(void *This, IUnknown *item) {
    CmdVecObj *v = (CmdVecObj *)This;
    EnterCriticalSection(&v->lock);
    if (v->count >= CMD_VEC_MAX) {
        LeaveCriticalSection(&v->lock);
        return E_OUTOFMEMORY;
    }
    if (item) item->lpVtbl->AddRef(item);
    v->items[v->count++] = item;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_remove_end(void *This) {
    CmdVecObj *v = (CmdVecObj *)This;
    EnterCriticalSection(&v->lock);
    if (v->count == 0u) {
        LeaveCriticalSection(&v->lock);
        return E_BOUNDS;
    }
    v->count--;
    if (v->items[v->count]) v->items[v->count]->lpVtbl->Release(v->items[v->count]);
    v->items[v->count] = NULL;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_clear(void *This) {
    CmdVecObj *v = (CmdVecObj *)This;
    EnterCriticalSection(&v->lock);
    for (uint32_t i = 0; i < v->count; ++i) {
        if (v->items[i]) v->items[i]->lpVtbl->Release(v->items[i]);
        v->items[i] = NULL;
    }
    v->count = 0;
    LeaveCriticalSection(&v->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_get_many(void *This, uint32_t start, uint32_t cap, IUnknown **items, uint32_t *actual) {
    CmdVecObj *v = (CmdVecObj *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (cap > 0 && !items) return E_POINTER;
    EnterCriticalSection(&v->lock);
    uint32_t n = 0;
    for (uint32_t i = start; i < v->count && n < cap; ++i, ++n) {
        items[n] = v->items[i];
        if (items[n]) items[n]->lpVtbl->AddRef(items[n]);
    }
    LeaveCriticalSection(&v->lock);
    *actual = n;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cvec_replace_all(void *This, uint32_t count, IUnknown **items) {
    if (count > 0 && !items) return E_POINTER;
    if (count > CMD_VEC_MAX) return E_OUTOFMEMORY;
    cvec_clear(This);
    for (uint32_t i = 0; i < count; ++i) {
        HRESULT hr = cvec_append(This, items[i]);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}

static const WinrtVectorPtrVtbl g_cvec_vtbl = {
    cvec_qi, cvec_addref, cvec_release, cvec_getiids, cvec_rcn, cvec_tl,
    cvec_get_at, cvec_get_size, cvec_get_view, cvec_index_of,
    cvec_set_at, cvec_insert_at, cvec_remove_at, cvec_append,
    cvec_remove_end, cvec_clear, cvec_get_many, cvec_replace_all
};

typedef struct WinrtPopupMenuVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Commands)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *ShowAsync)(void *This, WinrtPoint invocationPoint, void **asyncOp);
    HRESULT (STDMETHODCALLTYPE *ShowForSelectionAsync)(void *This, WinrtRect selection, void **asyncOp);
    HRESULT (STDMETHODCALLTYPE *ShowForSelectionWithPlacementAsync)(void *This, WinrtRect selection, int32_t placement, void **asyncOp);
} WinrtPopupMenuVtbl;

typedef struct PopupMenuObj {
    const WinrtPopupMenuVtbl *lpVtbl;
    volatile LONG ref_count;
    CmdVecObj *commands;
} PopupMenuObj;

static HRESULT STDMETHODCALLTYPE pm_qi(void *This, REFIID riid, void **ppv) {
    PopupMenuObj *o = (PopupMenuObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPopupMenu)) {
        InterlockedIncrement(&o->ref_count);
        *ppv = o;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pm_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((PopupMenuObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pm_release(void *This) {
    PopupMenuObj *o = (PopupMenuObj *)This;
    LONG rc = InterlockedDecrement(&o->ref_count);
    if (rc == 0) {
        if (o->commands) o->commands->lpVtbl->Release(o->commands);
        winrt_sys_free(o);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pm_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IPopupMenu, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE pm_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.UI.Popups.PopupMenu", n);
}
static HRESULT STDMETHODCALLTYPE pm_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pm_get_cmds(void *This, void **val) {
    PopupMenuObj *o = (PopupMenuObj *)This;
    if (!val) return E_POINTER;
    *val = o->commands;
    if (o->commands) o->commands->lpVtbl->AddRef(o->commands);
    return S_OK;
}
static HRESULT pm_show_common(PopupMenuObj *o, void **op) {
    if (!op) return E_POINTER;
    *op = NULL;
    IUnknown *selected = NULL;
    if (o->commands) {
        EnterCriticalSection(&o->commands->lock);
        if (o->commands->count > 0u && o->commands->items[0]) {
            selected = o->commands->items[0];
            selected->lpVtbl->AddRef(selected);
        }
        LeaveCriticalSection(&o->commands->lock);
    }
    if (selected) {
        typedef struct { const WinrtUICommandVtbl *lpVtbl; } CmdProxy;
        CmdProxy *cp = NULL;
        if (SUCCEEDED(selected->lpVtbl->QueryInterface(selected, &WINRT_IID_IUICommand, (void **)&cp)) && cp) {
            WinrtUICommandInvokedHandler *handler = NULL;
            if (SUCCEEDED(cp->lpVtbl->get_Invoked(cp, (void **)&handler)) && handler) {
                handler->lpVtbl->Invoke(handler, cp);
                handler->lpVtbl->Release(handler);
            }
            cp->lpVtbl->Release(cp);
        }
    }
    HRESULT hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_IUICommand,
        L"Windows.Foundation.IAsyncOperation`1<Windows.UI.Popups.IUICommand>",
        selected, S_OK, op);
    if (selected) selected->lpVtbl->Release(selected);
    return hr;
}
static HRESULT STDMETHODCALLTYPE pm_show(void *This, WinrtPoint pt, void **op) {
    (void)pt;
    return pm_show_common((PopupMenuObj *)This, op);
}
static HRESULT STDMETHODCALLTYPE pm_show_sel(void *This, WinrtRect rc, void **op) {
    (void)rc;
    return pm_show_common((PopupMenuObj *)This, op);
}
static HRESULT STDMETHODCALLTYPE pm_show_sel_p(void *This, WinrtRect rc, int32_t placement, void **op) {
    (void)rc; (void)placement;
    return pm_show_common((PopupMenuObj *)This, op);
}

static const WinrtPopupMenuVtbl g_pm_vtbl = {
    pm_qi, pm_addref, pm_release, pm_getiids, pm_rcn, pm_tl,
    pm_get_cmds, pm_show, pm_show_sel, pm_show_sel_p
};

typedef struct PopupMenuFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
} PopupMenuFactory;

static PopupMenuFactory g_pm_factory;

static HRESULT STDMETHODCALLTYPE pm_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_pm_factory.lpVtblAct;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pm_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE pm_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE pm_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IActivationFactory, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE pm_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE pm_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pm_fac_act(void *This, IInspectable **inst) {
    (void)This;
    if (!inst) return E_POINTER;
    *inst = NULL;
    PopupMenuObj *o = (PopupMenuObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return E_OUTOFMEMORY;
    CmdVecObj *cv = (CmdVecObj *)winrt_sys_alloc(sizeof(*cv));
    if (!cv) {
        winrt_sys_free(o);
        return E_OUTOFMEMORY;
    }
    cv->lpVtbl = &g_cvec_vtbl;
    cv->ref_count = 1;
    InitializeCriticalSection(&cv->lock);
    o->lpVtbl = &g_pm_vtbl;
    o->ref_count = 1;
    o->commands = cv;
    *inst = (IInspectable *)o;
    return S_OK;
}

static const WinrtActivationFactoryVtbl g_pm_act_vtbl = {
    pm_fac_qi, pm_fac_addref, pm_fac_release, pm_fac_getiids, pm_fac_rcn, pm_fac_tl, pm_fac_act
};
static PopupMenuFactory g_pm_factory = { &g_pm_act_vtbl };

HRESULT winrt_sys_get_popup_menu_factory(REFIID iid, void **out_factory) {
    return pm_fac_qi(&g_pm_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * 6. Windows.System.Threading.ThreadPool (Real Worker-Thread IAsyncAction)
 * ============================================================================ */

typedef struct WinrtWorkItemHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *operation);
} WinrtWorkItemHandlerVtbl;

typedef struct WinrtWorkItemHandler {
    const WinrtWorkItemHandlerVtbl *lpVtbl;
} WinrtWorkItemHandler;

typedef struct WinrtAsyncActionCompletedHandlerObj {
    const WinrtAsyncActionCompletedHandlerVtbl *lpVtbl;
} WinrtAsyncActionCompletedHandlerObj;

typedef struct ThreadPoolActionObj {
    const WinrtAsyncActionVtbl *lpVtblAction;
    const WinrtAsyncInfoVtbl   *lpVtblInfo;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    uint32_t id;
    AsyncStatus status;
    HRESULT err_hr;
    int completed_invoked;
    WinrtWorkItemHandler *work_handler;
    WinrtAsyncActionCompletedHandlerObj *completed_handler;
} ThreadPoolActionObj;

static volatile LONG g_tp_next_id = 1000;

static HRESULT tp_act_qi_impl(ThreadPoolActionObj *a, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAsyncAction)) {
        InterlockedIncrement(&a->ref_count);
        *ppv = &a->lpVtblAction;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IAsyncInfo)) {
        InterlockedIncrement(&a->ref_count);
        *ppv = &a->lpVtblInfo;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG tp_act_release_impl(ThreadPoolActionObj *a) {
    LONG rc = InterlockedDecrement(&a->ref_count);
    if (rc == 0) {
        if (a->work_handler) a->work_handler->lpVtbl->Release(a->work_handler);
        if (a->completed_handler) a->completed_handler->lpVtbl->Release(a->completed_handler);
        DeleteCriticalSection(&a->lock);
        winrt_sys_free(a);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE tpa_qi(void *This, REFIID riid, void **ppv) {
    return tp_act_qi_impl(WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblAction, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE tpa_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblAction, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE tpa_release(void *This) {
    return tp_act_release_impl(WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblAction, This));
}
static HRESULT STDMETHODCALLTYPE tpa_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x5a648006u, 0x843au, 0x4da9u, { 0x86u, 0x5bu, 0x9du, 0x26u, 0xe5u, 0xdfu, 0xadu, 0x7bu } },
        { 0x00000036u, 0x0000u, 0x0000u, { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE tpa_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.IAsyncAction", n);
}
static HRESULT STDMETHODCALLTYPE tpa_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpa_put_completed(void *This, void *handler) {
    ThreadPoolActionObj *a = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblAction, This);
    WinrtAsyncActionCompletedHandlerObj *to_call = NULL;
    AsyncStatus st = Started;
    EnterCriticalSection(&a->lock);
    if (a->completed_handler) {
        a->completed_handler->lpVtbl->Release(a->completed_handler);
        a->completed_handler = NULL;
    }
    if (handler) {
        a->completed_handler = (WinrtAsyncActionCompletedHandlerObj *)handler;
        a->completed_handler->lpVtbl->AddRef(a->completed_handler);
        if (a->status != Started && !a->completed_invoked) {
            a->completed_invoked = 1;
            to_call = a->completed_handler;
            to_call->lpVtbl->AddRef(to_call);
            st = a->status;
        }
    }
    LeaveCriticalSection(&a->lock);
    if (to_call) {
        tpa_addref(&a->lpVtblAction);
        to_call->lpVtbl->Invoke(to_call, &a->lpVtblAction, st);
        tpa_release(&a->lpVtblAction);
        to_call->lpVtbl->Release(to_call);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpa_get_completed(void *This, void **handler) {
    ThreadPoolActionObj *a = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblAction, This);
    if (!handler) return E_POINTER;
    EnterCriticalSection(&a->lock);
    *handler = a->completed_handler;
    if (a->completed_handler) a->completed_handler->lpVtbl->AddRef(a->completed_handler);
    LeaveCriticalSection(&a->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpa_get_results(void *This) {
    ThreadPoolActionObj *a = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblAction, This);
    EnterCriticalSection(&a->lock);
    HRESULT hr = a->err_hr;
    if (a->status == Canceled && SUCCEEDED(hr)) hr = E_ABORT;
    LeaveCriticalSection(&a->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE tpi_qi(void *This, REFIID riid, void **ppv) {
    return tp_act_qi_impl(WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE tpi_addref(void *This) {
    return (ULONG)InterlockedIncrement(&WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This)->ref_count);
}
static ULONG STDMETHODCALLTYPE tpi_release(void *This) {
    return tp_act_release_impl(WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This));
}
static HRESULT STDMETHODCALLTYPE tpi_get_id(void *This, uint32_t *id) {
    if (!id) return E_POINTER;
    *id = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This)->id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpi_get_status(void *This, AsyncStatus *st) {
    ThreadPoolActionObj *a = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This);
    if (!st) return E_POINTER;
    EnterCriticalSection(&a->lock);
    *st = a->status;
    LeaveCriticalSection(&a->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpi_get_error(void *This, HRESULT *err) {
    ThreadPoolActionObj *a = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This);
    if (!err) return E_POINTER;
    EnterCriticalSection(&a->lock);
    *err = a->err_hr;
    LeaveCriticalSection(&a->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpi_cancel(void *This) {
    ThreadPoolActionObj *a = WINRT_SYS_FROM(ThreadPoolActionObj, lpVtblInfo, This);
    EnterCriticalSection(&a->lock);
    if (a->status == Started) a->status = Canceled;
    LeaveCriticalSection(&a->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpi_close(void *This) {
    (void)This;
    return S_OK;
}

static const WinrtAsyncActionVtbl g_tp_action_vtbl = {
    tpa_qi, tpa_addref, tpa_release, tpa_getiids, tpa_rcn, tpa_tl,
    tpa_put_completed, tpa_get_completed, tpa_get_results
};
static const WinrtAsyncInfoVtbl g_tp_info_vtbl = {
    tpi_qi, tpi_addref, tpi_release, tpa_getiids, tpa_rcn, tpa_tl,
    tpi_get_id, tpi_get_status, tpi_get_error, tpi_cancel, tpi_close
};

static void tp_run_worker_body(ThreadPoolActionObj *a) {
    HRESULT hr = S_OK;
    if (a->work_handler) {
        hr = a->work_handler->lpVtbl->Invoke(a->work_handler, &a->lpVtblAction);
    }

    WinrtAsyncActionCompletedHandlerObj *to_call = NULL;
    AsyncStatus st = Completed;
    EnterCriticalSection(&a->lock);
    a->err_hr = hr;
    if (a->status == Started) {
        a->status = SUCCEEDED(hr) ? Completed : Error;
    }
    st = a->status;
    if (a->completed_handler && !a->completed_invoked) {
        a->completed_invoked = 1;
        to_call = a->completed_handler;
        to_call->lpVtbl->AddRef(to_call);
    }
    LeaveCriticalSection(&a->lock);

    if (to_call) {
        tpa_addref(&a->lpVtblAction);
        to_call->lpVtbl->Invoke(to_call, &a->lpVtblAction, st);
        tpa_release(&a->lpVtblAction);
        to_call->lpVtbl->Release(to_call);
    }
    tp_act_release_impl(a);
}

#ifdef _WIN32
static DWORD WINAPI tp_win32_thread(LPVOID param) {
    typedef HRESULT (WINAPI *RoInitFn)(int);
    typedef void (WINAPI *RoUninitFn)(void);
    typedef HRESULT (WINAPI *CoInitExFn)(LPVOID, DWORD);
    typedef void (WINAPI *CoUninitFn)(void);
    HMODULE hComb = LoadLibraryA("combase.dll");
    HMODULE hOle = NULL;
    int did_ro = 0;
    int did_co = 0;
    if (hComb) {
        RoInitFn pRoInit = (RoInitFn)(void *)GetProcAddress(hComb, "RoInitialize");
        if (pRoInit && SUCCEEDED(pRoInit(1 /* RO_INIT_MULTITHREADED */))) {
            did_ro = 1;
        }
    }
    if (!did_ro) {
        hOle = LoadLibraryA("ole32.dll");
        if (hOle) {
            CoInitExFn pCoInit = (CoInitExFn)(void *)GetProcAddress(hOle, "CoInitializeEx");
            if (pCoInit && SUCCEEDED(pCoInit(NULL, 0 /* COINIT_MULTITHREADED */))) {
                did_co = 1;
            }
        }
    }
    tp_run_worker_body((ThreadPoolActionObj *)param);
    if (did_ro && hComb) {
        RoUninitFn pRoUninit = (RoUninitFn)(void *)GetProcAddress(hComb, "RoUninitialize");
        if (pRoUninit) pRoUninit();
    } else if (did_co && hOle) {
        CoUninitFn pCoUninit = (CoUninitFn)(void *)GetProcAddress(hOle, "CoUninitialize");
        if (pCoUninit) pCoUninit();
    }
    if (hOle) FreeLibrary(hOle);
    if (hComb) FreeLibrary(hComb);
    return 0;
}
#else
static void *tp_posix_thread(void *param) {
    tp_run_worker_body((ThreadPoolActionObj *)param);
    return NULL;
}
#endif

typedef struct WinrtThreadPoolStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *RunAsync)(void *This, void *handler, void **operation);
    HRESULT (STDMETHODCALLTYPE *RunWithPriorityAsync)(void *This, void *handler, int32_t priority, void **operation);
    HRESULT (STDMETHODCALLTYPE *RunWithPriorityAndOptionsAsync)(void *This, void *handler, int32_t priority, uint32_t options, void **operation);
} WinrtThreadPoolStaticsVtbl;

typedef struct ThreadPoolFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtThreadPoolStaticsVtbl *lpVtblStatics;
} ThreadPoolFactory;

static ThreadPoolFactory g_tp_factory;

static HRESULT STDMETHODCALLTYPE tp_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_tp_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IThreadPoolStatics)) {
        *ppv = &g_tp_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE tp_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE tp_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE tp_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0xb6bf67ddu, 0x84bdu, 0x44f8u, { 0xacu, 0x1cu, 0x93u, 0xebu, 0xcbu, 0x9du, 0xbau, 0x91u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE tp_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE tp_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tp_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("ThreadPool::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE tp_run_opts(void *This, void *handler, int32_t priority, uint32_t options, void **op) {
    (void)This; (void)priority; (void)options;
    if (!op) return E_POINTER;
    *op = NULL;
    if (!handler) return E_INVALIDARG;

    ThreadPoolActionObj *a = (ThreadPoolActionObj *)winrt_sys_alloc(sizeof(*a));
    if (!a) return E_OUTOFMEMORY;
    a->lpVtblAction = &g_tp_action_vtbl;
    a->lpVtblInfo = &g_tp_info_vtbl;
    a->ref_count = 2; /* 1 for caller, 1 for worker thread */
    InitializeCriticalSection(&a->lock);
    a->id = (uint32_t)InterlockedIncrement(&g_tp_next_id);
    a->status = Started;
    a->err_hr = S_OK;
    a->work_handler = (WinrtWorkItemHandler *)handler;
    a->work_handler->lpVtbl->AddRef(a->work_handler);

#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, tp_win32_thread, a, 0, NULL);
    if (!th) {
        tp_act_release_impl(a);
        tp_act_release_impl(a);
        return E_OUTOFMEMORY;
    }
    CloseHandle(th);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, tp_posix_thread, a) != 0) {
        tp_act_release_impl(a);
        tp_act_release_impl(a);
        return E_OUTOFMEMORY;
    }
    pthread_detach(th);
#endif

    *op = &a->lpVtblAction;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tp_run_prio(void *This, void *handler, int32_t priority, void **op) {
    return tp_run_opts(This, handler, priority, 0u, op);
}
static HRESULT STDMETHODCALLTYPE tp_run(void *This, void *handler, void **op) {
    return tp_run_opts(This, handler, 0, 0u, op);
}

static const WinrtActivationFactoryVtbl g_tp_act_vtbl = {
    tp_fac_qi, tp_fac_addref, tp_fac_release, tp_fac_getiids, tp_fac_rcn, tp_fac_tl, tp_fac_act
};
static const WinrtThreadPoolStaticsVtbl g_tp_stat_vtbl = {
    tp_fac_qi, tp_fac_addref, tp_fac_release, tp_fac_getiids, tp_fac_rcn, tp_fac_tl,
    tp_run, tp_run_prio, tp_run_opts
};
static ThreadPoolFactory g_tp_factory = { &g_tp_act_vtbl, &g_tp_stat_vtbl };

HRESULT winrt_sys_get_thread_pool_factory(REFIID iid, void **out_factory) {
    return tp_fac_qi(&g_tp_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * Windows.System.Threading.ThreadPoolTimer
 * ============================================================================ */

static const GUID WINRT_IID_IThreadPoolTimerStatics =
    { 0x1a8a9d02u, 0xe482u, 0x461bu, { 0xb8u, 0xc7u, 0x8eu, 0xfau, 0xd1u, 0xccu, 0xe5u, 0x90u } };
static const GUID WINRT_IID_IThreadPoolTimer =
    { 0x594ebe78u, 0x55eau, 0x4a88u, { 0xa5u, 0x0du, 0x34u, 0x02u, 0xaeu, 0x1fu, 0x9cu, 0xf2u } };

typedef struct WinrtTimerHandlerObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *timer);
    } *lpVtbl;
} WinrtTimerHandlerObj;

typedef struct ThreadPoolTimerObj {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Period)(void *This, int64_t *val);
        HRESULT (STDMETHODCALLTYPE *get_Delay)(void *This, int64_t *val);
        HRESULT (STDMETHODCALLTYPE *Cancel)(void *This);
    } *lpVtbl;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    int periodic;
    int64_t period;
    int64_t delay;
    volatile LONG canceled;
    WinrtTimerHandlerObj *elapsed;
    WinrtTimerHandlerObj *destroyed;
#ifdef _WIN32
    HANDLE cancel_event;
#else
    pthread_mutex_t mu;
    pthread_cond_t cv;
#endif
} ThreadPoolTimerObj;

static HRESULT STDMETHODCALLTYPE tpt_qi(void *This, REFIID riid, void **ppv) {
    ThreadPoolTimerObj *t = (ThreadPoolTimerObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IThreadPoolTimer)) {
        InterlockedIncrement(&t->ref_count);
        *ppv = t;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE tpt_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((ThreadPoolTimerObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE tpt_rel(void *This) {
    ThreadPoolTimerObj *t = (ThreadPoolTimerObj *)This;
    LONG rc = InterlockedDecrement(&t->ref_count);
    if (rc == 0) {
        if (t->elapsed) t->elapsed->lpVtbl->Release(t->elapsed);
        if (t->destroyed) t->destroyed->lpVtbl->Release(t->destroyed);
#ifdef _WIN32
        if (t->cancel_event) CloseHandle(t->cancel_event);
#else
        pthread_cond_destroy(&t->cv);
        pthread_mutex_destroy(&t->mu);
#endif
        DeleteCriticalSection(&t->lock);
        winrt_sys_free(t);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE tpt_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IThreadPoolTimer, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE tpt_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.System.Threading.ThreadPoolTimer", n);
}
static HRESULT STDMETHODCALLTYPE tpt_tl(void *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpt_get_period(void *This, int64_t *val) {
    if (!val) return E_POINTER;
    *val = ((ThreadPoolTimerObj *)This)->period;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpt_get_delay(void *This, int64_t *val) {
    if (!val) return E_POINTER;
    *val = ((ThreadPoolTimerObj *)This)->delay;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpt_cancel(void *This) {
    ThreadPoolTimerObj *t = (ThreadPoolTimerObj *)This;
    InterlockedCompareExchange(&t->canceled, 1, 0);
#ifdef _WIN32
    if (t->cancel_event) SetEvent(t->cancel_event);
#else
    pthread_mutex_lock(&t->mu);
    pthread_cond_broadcast(&t->cv);
    pthread_mutex_unlock(&t->mu);
#endif
    return S_OK;
}
static const void *g_tpt_vtbl[] = {
    tpt_qi, tpt_ar, tpt_rel, tpt_iids, tpt_rcn, tpt_tl,
    tpt_get_period, tpt_get_delay, tpt_cancel
};

static int tpt_wait_100ns(ThreadPoolTimerObj *t, int64_t ticks) {
    if (InterlockedCompareExchange(&t->canceled, 0, 0) != 0) return 1;
    int64_t ms = (ticks > 0) ? (ticks / 10000LL) : 0;
#ifdef _WIN32
    if (t->cancel_event) {
        DWORD wr = WaitForSingleObject(t->cancel_event, (DWORD)(ms > 0x7FFFFFFELL ? 0x7FFFFFFELL : ms));
        if (wr == WAIT_OBJECT_0) return 1;
    } else if (ms > 0) {
        Sleep((DWORD)ms);
    }
#else
    if (ms > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)(ms / 1000LL);
        ts.tv_nsec += (long)((ms % 1000LL) * 1000000LL);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&t->mu);
        while (InterlockedCompareExchange(&t->canceled, 0, 0) == 0) {
            int rc = pthread_cond_timedwait(&t->cv, &t->mu, &ts);
            if (rc != 0) break;
        }
        pthread_mutex_unlock(&t->mu);
    }
#endif
    return InterlockedCompareExchange(&t->canceled, 0, 0) != 0;
}

static void tpt_worker_body(ThreadPoolTimerObj *t) {
    int64_t wait_ticks = t->periodic ? t->period : t->delay;
    for (;;) {
        if (tpt_wait_100ns(t, wait_ticks)) break;
        if (t->elapsed) {
            tpt_ar(t);
            t->elapsed->lpVtbl->Invoke(t->elapsed, t);
            tpt_rel(t);
        }
        if (!t->periodic) break;
    }
    if (t->destroyed) {
        tpt_ar(t);
        t->destroyed->lpVtbl->Invoke(t->destroyed, t);
        tpt_rel(t);
    }
    tpt_rel(t);
}

#ifdef _WIN32
static DWORD WINAPI tpt_win32_thread(LPVOID param) {
    typedef HRESULT (WINAPI *RoInitFn)(int);
    typedef void (WINAPI *RoUninitFn)(void);
    HMODULE hComb = LoadLibraryA("combase.dll");
    int did_ro = 0;
    if (hComb) {
        RoInitFn pRoInit = (RoInitFn)(void *)GetProcAddress(hComb, "RoInitialize");
        if (pRoInit && SUCCEEDED(pRoInit(1))) did_ro = 1;
    }
    tpt_worker_body((ThreadPoolTimerObj *)param);
    if (did_ro && hComb) {
        RoUninitFn pRoUninit = (RoUninitFn)(void *)GetProcAddress(hComb, "RoUninitialize");
        if (pRoUninit) pRoUninit();
    }
    if (hComb) FreeLibrary(hComb);
    return 0;
}
#else
static void *tpt_posix_thread(void *param) {
    tpt_worker_body((ThreadPoolTimerObj *)param);
    return NULL;
}
#endif

typedef struct ThreadPoolTimerFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const void *lpVtblStatics;
} ThreadPoolTimerFactory;

static ThreadPoolTimerFactory g_tpt_factory;

static HRESULT STDMETHODCALLTYPE tpt_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_tpt_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IThreadPoolTimerStatics)) {
        *ppv = &g_tpt_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE tpt_fac_iids(void *This, ULONG *c, IID **i) {
    (void)This;
    const GUID list[2] = { WINRT_IID_IActivationFactory, WINRT_IID_IThreadPoolTimerStatics };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE tpt_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("ThreadPoolTimer::ActivateInstance", This);
}
static HRESULT tpt_create_impl(int periodic, void *handler, int64_t ticks, void *destroyed, void **out_timer) {
    if (!out_timer) return E_POINTER;
    *out_timer = NULL;
    if (!handler) return E_INVALIDARG;
    ThreadPoolTimerObj *t = (ThreadPoolTimerObj *)winrt_sys_alloc(sizeof(*t));
    if (!t) return E_OUTOFMEMORY;
    t->lpVtbl = (const void *)&g_tpt_vtbl;
    t->ref_count = 2; /* caller + worker */
    InitializeCriticalSection(&t->lock);
    t->periodic = periodic;
    if (periodic) t->period = ticks;
    else t->delay = ticks;
    t->elapsed = (WinrtTimerHandlerObj *)handler;
    t->elapsed->lpVtbl->AddRef(t->elapsed);
    if (destroyed) {
        t->destroyed = (WinrtTimerHandlerObj *)destroyed;
        t->destroyed->lpVtbl->AddRef(t->destroyed);
    }
#ifdef _WIN32
    t->cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, tpt_win32_thread, t, 0, NULL);
    if (!th) {
        tpt_rel(t);
        tpt_rel(t);
        return E_OUTOFMEMORY;
    }
    CloseHandle(th);
#else
    pthread_mutex_init(&t->mu, NULL);
    pthread_cond_init(&t->cv, NULL);
    pthread_t th;
    if (pthread_create(&th, NULL, tpt_posix_thread, t) != 0) {
        tpt_rel(t);
        tpt_rel(t);
        return E_OUTOFMEMORY;
    }
    pthread_detach(th);
#endif
    *out_timer = t;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE tpt_create_periodic(void *This, void *h, int64_t period, void **out) {
    (void)This; return tpt_create_impl(1, h, period, NULL, out);
}
static HRESULT STDMETHODCALLTYPE tpt_create_timer(void *This, void *h, int64_t delay, void **out) {
    (void)This; return tpt_create_impl(0, h, delay, NULL, out);
}
static HRESULT STDMETHODCALLTYPE tpt_create_periodic_comp(void *This, void *h, int64_t period, void *dest, void **out) {
    (void)This; return tpt_create_impl(1, h, period, dest, out);
}
static HRESULT STDMETHODCALLTYPE tpt_create_timer_comp(void *This, void *h, int64_t delay, void *dest, void **out) {
    (void)This; return tpt_create_impl(0, h, delay, dest, out);
}
static const WinrtActivationFactoryVtbl g_tpt_act_vtbl = {
    tpt_fac_qi, tp_fac_addref, tp_fac_release, tpt_fac_iids, tp_fac_rcn, tp_fac_tl, tpt_fac_act
};
static const void *g_tpt_stat_vtbl[] = {
    tpt_fac_qi, tp_fac_addref, tp_fac_release, tpt_fac_iids, tp_fac_rcn, tp_fac_tl,
    tpt_create_periodic, tpt_create_timer, tpt_create_periodic_comp, tpt_create_timer_comp
};
static ThreadPoolTimerFactory g_tpt_factory = { &g_tpt_act_vtbl, (const void *)&g_tpt_stat_vtbl };

HRESULT winrt_sys_get_thread_pool_timer_factory(REFIID iid, void **out_factory) {
    return tpt_fac_qi(&g_tpt_factory.lpVtblAct, iid, out_factory);
}
