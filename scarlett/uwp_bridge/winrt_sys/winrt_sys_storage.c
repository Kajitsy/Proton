/*
 * Proton-Scarlett — UWP Bridge System WinRT: Storage & ApplicationData
 *
 * Implements:
 *   - Windows.Storage.ApplicationData (IApplicationDataStatics, IApplicationDataStatics2,
 *     IApplicationData, IApplicationData2, IApplicationData3)
 *   - Windows.Storage.ApplicationDataContainer (IApplicationDataContainer + subcontainers)
 *   - Windows.Foundation.Collections.PropertySet (IPropertySet, IMap, IObservableMap,
 *     IIterable, IMapView) backed by real in-memory + INI persistence under LocalState/RoamingState
 *   - Windows.Storage.StorageFolder (IStorageFolderStatics, IStorageFolder, IStorageItem)
 *   - Windows.Storage.StorageFile (IStorageFile, IStorageItem)
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls).
 */

#include "winrt_sys_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Vtable Definitions (Windows.Storage & Collections)
 * ============================================================================ */

typedef struct WinrtStorageFolderStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetFolderFromPathAsync)(void *This, HSTRING path, void **operation);
} WinrtStorageFolderStaticsVtbl;

typedef struct WinrtStorageItemVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *RenameAsyncOverloadDefaultOptions)(void *This, HSTRING desiredName, void **operation);
    HRESULT (STDMETHODCALLTYPE *RenameAsync)(void *This, HSTRING desiredName, uint32_t option, void **operation);
    HRESULT (STDMETHODCALLTYPE *DeleteAsyncOverloadDefaultOptions)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *DeleteAsync)(void *This, uint32_t option, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetBasicPropertiesAsync)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Path)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Attributes)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *get_DateCreated)(void *This, int64_t *value);
    HRESULT (STDMETHODCALLTYPE *IsOfType)(void *This, uint32_t type, boolean *value);
} WinrtStorageItemVtbl;

typedef struct WinrtStorageFolderVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *CreateFileAsyncOverloadDefaultOptions)(void *This, HSTRING desiredName, void **operation);
    HRESULT (STDMETHODCALLTYPE *CreateFileAsync)(void *This, HSTRING desiredName, uint32_t options, void **operation);
    HRESULT (STDMETHODCALLTYPE *CreateFolderAsyncOverloadDefaultOptions)(void *This, HSTRING desiredName, void **operation);
    HRESULT (STDMETHODCALLTYPE *CreateFolderAsync)(void *This, HSTRING desiredName, uint32_t options, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFileAsync)(void *This, HSTRING name, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFolderAsync)(void *This, HSTRING name, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetItemAsync)(void *This, HSTRING name, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFilesAsyncOverloadDefaultOptionsStartAndCount)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetFoldersAsyncOverloadDefaultOptionsStartAndCount)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *GetItemsAsyncOverloadDefaultStartAndCount)(void *This, void **operation);
} WinrtStorageFolderVtbl;

typedef struct WinrtStorageFileVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_FileType)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_ContentType)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *OpenAsync)(void *This, uint32_t accessMode, void **operation);
    HRESULT (STDMETHODCALLTYPE *OpenTransactedWriteAsync)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *CopyOverloadDefaultNameAndOptions)(void *This, void *destinationFolder, void **operation);
    HRESULT (STDMETHODCALLTYPE *CopyOverloadDefaultOptions)(void *This, void *destinationFolder, HSTRING desiredNewName, void **operation);
    HRESULT (STDMETHODCALLTYPE *CopyOverload)(void *This, void *destinationFolder, HSTRING desiredNewName, uint32_t option, void **operation);
    HRESULT (STDMETHODCALLTYPE *CopyAndReplaceAsync)(void *This, void *fileToReplace, void **operation);
    HRESULT (STDMETHODCALLTYPE *MoveOverloadDefaultNameAndOptions)(void *This, void *destinationFolder, void **operation);
    HRESULT (STDMETHODCALLTYPE *MoveOverloadDefaultOptions)(void *This, void *destinationFolder, HSTRING desiredNewName, void **operation);
    HRESULT (STDMETHODCALLTYPE *MoveOverload)(void *This, void *destinationFolder, HSTRING desiredNewName, uint32_t option, void **operation);
    HRESULT (STDMETHODCALLTYPE *MoveAndReplaceAsync)(void *This, void *fileToReplace, void **operation);
} WinrtStorageFileVtbl;

typedef struct WinrtVectorViewPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t index, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, void *element, uint32_t *index, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t startIndex, uint32_t itemsSize, void **items, uint32_t *value);
} WinrtVectorViewPtrVtbl;

typedef struct WinrtApplicationDataStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **value);
} WinrtApplicationDataStaticsVtbl;

typedef struct WinrtApplicationDataStatics2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetForUserAsync)(void *This, void *user, void **operation);
} WinrtApplicationDataStatics2Vtbl;

typedef struct WinrtApplicationDataVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Version)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *SetVersionAsync)(void *This, uint32_t desiredVersion, void *handler, void **operation);
    HRESULT (STDMETHODCALLTYPE *ClearAllAsync)(void *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *ClearAsync)(void *This, uint32_t locality, void **operation);
    HRESULT (STDMETHODCALLTYPE *get_LocalSettings)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_RoamingSettings)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_LocalFolder)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_RoamingFolder)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_TemporaryFolder)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *add_DataChanged)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_DataChanged)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *SignalDataChanged)(void *This);
    HRESULT (STDMETHODCALLTYPE *get_RoamingStorageQuota)(void *This, uint64_t *value);
} WinrtApplicationDataVtbl;

typedef struct WinrtApplicationData2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_LocalCacheFolder)(void *This, void **value);
} WinrtApplicationData2Vtbl;

typedef struct WinrtApplicationData3Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetPublisherCacheFolder)(void *This, HSTRING folderName, void **value);
    HRESULT (STDMETHODCALLTYPE *ClearPublisherCacheFolderAsync)(void *This, HSTRING folderName, void **operation);
    HRESULT (STDMETHODCALLTYPE *get_SharedLocalFolder)(void *This, void **value);
} WinrtApplicationData3Vtbl;

typedef struct WinrtApplicationDataContainerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Locality)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *get_Values)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Containers)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *CreateContainer)(void *This, HSTRING name, uint32_t disposition, void **container);
    HRESULT (STDMETHODCALLTYPE *DeleteContainer)(void *This, HSTRING name);
} WinrtApplicationDataContainerVtbl;

typedef struct WinrtPropertySetVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
} WinrtPropertySetVtbl;

typedef struct WinrtObservableMapVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_MapChanged)(void *This, void *vhnd, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_MapChanged)(void *This, EventRegistrationToken token);
} WinrtObservableMapVtbl;

typedef struct WinrtMapHstringPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *Lookup)(void *This, HSTRING key, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
    HRESULT (STDMETHODCALLTYPE *HasKey)(void *This, HSTRING key, boolean *found);
    HRESULT (STDMETHODCALLTYPE *GetView)(void *This, void **view);
    HRESULT (STDMETHODCALLTYPE *Insert)(void *This, HSTRING key, void *value, boolean *replaced);
    HRESULT (STDMETHODCALLTYPE *Remove)(void *This, HSTRING key);
    HRESULT (STDMETHODCALLTYPE *Clear)(void *This);
} WinrtMapHstringPtrVtbl;

typedef struct WinrtMapViewHstringPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *Lookup)(void *This, HSTRING key, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *size);
    HRESULT (STDMETHODCALLTYPE *HasKey)(void *This, HSTRING key, boolean *found);
    HRESULT (STDMETHODCALLTYPE *Split)(void *This, void **first, void **second);
} WinrtMapViewHstringPtrVtbl;

typedef struct WinrtIterableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *First)(void *This, void **first);
} WinrtIterableVtbl;

typedef struct WinrtIteratorPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t itemsSize, void **items, uint32_t *value);
} WinrtIteratorPtrVtbl;

typedef struct WinrtKvpHstringPtrVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Key)(void *This, HSTRING *key);
    HRESULT (STDMETHODCALLTYPE *get_Value)(void *This, void **value);
} WinrtKvpHstringPtrVtbl;

typedef struct WinrtMapChangedArgsHstringVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_CollectionChanged)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *get_Key)(void *This, HSTRING *value);
} WinrtMapChangedArgsHstringVtbl;

typedef struct WinrtMapChangedHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *sender, void *args);
} WinrtMapChangedHandlerVtbl;

/* ============================================================================
 * Generic Ptr VectorView + KVP + Iterator Helpers
 * ============================================================================ */

typedef struct WinrtStoragePtrList {
    const WinrtVectorViewPtrVtbl *lpVtbl;
    volatile LONG ref_count;
    GUID view_iid;
    wchar_t runtime_class_name[160];
    uint32_t count;
    IUnknown **items;
} WinrtStoragePtrList;

static HRESULT STDMETHODCALLTYPE sp_list_qi(void *This, REFIID riid, void **ppv) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &obj->view_iid)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sp_list_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtStoragePtrList *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE sp_list_release(void *This) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < obj->count; ++i) {
            if (obj->items[i]) obj->items[i]->lpVtbl->Release(obj->items[i]);
        }
        winrt_sys_free(obj->items);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE sp_list_getiids(void *This, ULONG *c, IID **i) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    return winrt_sys_get_iids_copy(&obj->view_iid, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE sp_list_rcn(void *This, HSTRING *n) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    return winrt_sys_make_hstring(obj->runtime_class_name, n);
}
static HRESULT STDMETHODCALLTYPE sp_list_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sp_list_getat(void *This, uint32_t index, void **val) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    if (!val) return E_POINTER;
    *val = NULL;
    if (index >= obj->count) return E_BOUNDS;
    obj->items[index]->lpVtbl->AddRef(obj->items[index]);
    *val = obj->items[index];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sp_list_size(void *This, uint32_t *val) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    if (!val) return E_POINTER;
    *val = obj->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sp_list_indexof(void *This, void *elem, uint32_t *idx, boolean *found) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    if (!idx || !found) return E_POINTER;
    *idx = 0;
    *found = 0;
    for (uint32_t i = 0; i < obj->count; ++i) {
        if ((void *)obj->items[i] == elem) {
            *idx = i;
            *found = 1;
            break;
        }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sp_list_getmany(void *This, uint32_t start, uint32_t cap, void **out, uint32_t *actual) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (start >= obj->count || cap == 0 || !out) return S_OK;
    uint32_t n = obj->count - start;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; ++i) {
        obj->items[start + i]->lpVtbl->AddRef(obj->items[start + i]);
        out[i] = obj->items[start + i];
    }
    *actual = n;
    return S_OK;
}

static const WinrtVectorViewPtrVtbl g_sp_list_vtbl = {
    sp_list_qi, sp_list_addref, sp_list_release, sp_list_getiids, sp_list_rcn, sp_list_tl,
    sp_list_getat, sp_list_size, sp_list_indexof, sp_list_getmany
};

static HRESULT create_storage_ptr_list(
    const GUID *view_iid,
    const wchar_t *rcn,
    IUnknown **items,
    uint32_t count,
    void **out_view) {
    WinrtStoragePtrList *obj = (WinrtStoragePtrList *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    obj->lpVtbl = &g_sp_list_vtbl;
    obj->ref_count = 1;
    obj->view_iid = *view_iid;
    winrt_sys_copy_wstr(obj->runtime_class_name, 160, rcn);
    obj->count = count;
    if (count > 0) {
        obj->items = (IUnknown **)winrt_sys_alloc((size_t)count * sizeof(IUnknown *));
        if (!obj->items) {
            winrt_sys_free(obj);
            return E_OUTOFMEMORY;
        }
        for (uint32_t i = 0; i < count; ++i) {
            obj->items[i] = items[i];
            if (items[i]) items[i]->lpVtbl->AddRef(items[i]);
        }
    }
    *out_view = obj;
    return S_OK;
}

/* ============================================================================
 * Windows.Storage.StorageFile & StorageFolder
 * ============================================================================ */

HRESULT winrt_sys_create_storage_file(const wchar_t *path, void **out_file);

typedef struct WinrtStorageFileObj {
    const WinrtStorageFileVtbl *lpVtbl;
    const WinrtStorageItemVtbl *lpVtblItem;
    volatile LONG ref_count;
    wchar_t path[UWP_MAX_PATH_CHARS];
} WinrtStorageFileObj;

static void extract_leaf_name(const wchar_t *path, wchar_t *out, size_t cap) {
    if (!path || path[0] == L'\0') {
        winrt_sys_copy_wstr(out, cap, L"");
        return;
    }
    const wchar_t *last = wcsrchr(path, L'\\');
    if (last == NULL) {
        winrt_sys_copy_wstr(out, cap, path);
    } else if (last[1] == L'\0') {
        /* Root like C:\ */
        winrt_sys_copy_wstr(out, cap, path);
    } else {
        winrt_sys_copy_wstr(out, cap, last + 1);
    }
}

static uint32_t map_win32_attrs(DWORD attrs) {
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0u;
    uint32_t out = 0u;
    if (attrs & FILE_ATTRIBUTE_READONLY)  out |= 0x1u;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) out |= 0x10u;
    if (attrs & FILE_ATTRIBUTE_ARCHIVE)   out |= 0x20u;
    return out;
}

static int64_t query_creation_time(const wchar_t *path) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    memset(&fad, 0, sizeof(fad));
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        return 0;
    }
    return ((int64_t)fad.ftCreationTime.dwHighDateTime << 32) |
           (int64_t)fad.ftCreationTime.dwLowDateTime;
}

static HRESULT STDMETHODCALLTYPE sf_file_qi(void *This, REFIID riid, void **ppv) {
    WinrtStorageFileObj *obj = (WinrtStorageFileObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IStorageFile)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IStorageItem)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblItem;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sf_file_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtStorageFileObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE sf_file_release(void *This) {
    WinrtStorageFileObj *obj = (WinrtStorageFileObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE sf_file_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0xfa3f6186u, 0x4214u, 0x428cu, { 0xa6u, 0x4cu, 0x14u, 0xc9u, 0xacu, 0x73u, 0x15u, 0xeau } },
        { 0x4207a996u, 0xca2fu, 0x42f7u, { 0xbdu, 0xe8u, 0x8bu, 0x10u, 0x45u, 0x7au, 0x7fu, 0x30u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE sf_file_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.StorageFile", n);
}
static HRESULT STDMETHODCALLTYPE sf_file_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sf_file_get_filetype(void *This, HSTRING *val) {
    WinrtStorageFileObj *obj = (WinrtStorageFileObj *)This;
    const wchar_t *dot = wcsrchr(obj->path, L'.');
    const wchar_t *slash = wcsrchr(obj->path, L'\\');
    if (dot != NULL && (slash == NULL || dot > slash)) {
        return winrt_sys_make_hstring(dot, val);
    }
    return winrt_sys_make_hstring(L"", val);
}
static HRESULT STDMETHODCALLTYPE sf_file_get_contenttype(void *This, HSTRING *val) {
    (void)This;
    return winrt_sys_make_hstring(L"application/octet-stream", val);
}
static HRESULT STDMETHODCALLTYPE sf_file_unimpl_1(void *This, uint32_t a, void **op) {
    (void)a;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IStorageFile::OpenAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_unimpl_0(void *This, void **op) {
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IStorageFile::OpenTransactedWriteAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_unimpl_copy1(void *This, void *a, void **op) {
    (void)a;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IStorageFile::CopyMove", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_unimpl_copy2(void *This, void *a, HSTRING b, void **op) {
    (void)a; (void)b;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IStorageFile::CopyMove", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_unimpl_copy3(void *This, void *a, HSTRING b, uint32_t c, void **op) {
    (void)a; (void)b; (void)c;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IStorageFile::CopyMove", This);
}

static HRESULT STDMETHODCALLTYPE sf_file_item_qi(void *This, REFIID riid, void **ppv) {
    return sf_file_qi(WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE sf_file_item_addref(void *This) {
    return sf_file_addref(WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This));
}
static ULONG STDMETHODCALLTYPE sf_file_item_release(void *This) {
    return sf_file_release(WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This));
}
static HRESULT STDMETHODCALLTYPE sf_file_item_getiids(void *This, ULONG *c, IID **i) {
    return sf_file_getiids(WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This), c, i);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_rcn(void *This, HSTRING *n) {
    return sf_file_rcn(WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This), n);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_tl(void *This, TrustLevel *tl) {
    return sf_file_tl(WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This), tl);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_rename1(void *This, HSTRING n, void **op) {
    (void)n;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("StorageFile::RenameAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_rename2(void *This, HSTRING n, uint32_t o, void **op) {
    (void)n; (void)o;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("StorageFile::RenameAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_del1(void *This, void **op) {
    WinrtStorageFileObj *obj = WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This);
    if (!op) return E_POINTER;
    HRESULT hr = DeleteFileW(obj->path) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    return winrt_sys_create_completed_async_action(hr, op);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_del2(void *This, uint32_t opt, void **op) {
    (void)opt;
    return sf_file_item_del1(This, op);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_props(void *This, void **op) {
    if (op) *op = NULL;
    return winrt_sys_unimplemented("StorageFile::GetBasicPropertiesAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_name(void *This, HSTRING *val) {
    WinrtStorageFileObj *obj = WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This);
    wchar_t leaf[UWP_MAX_PATH_CHARS];
    extract_leaf_name(obj->path, leaf, UWP_MAX_PATH_CHARS);
    return winrt_sys_make_hstring(leaf, val);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_path(void *This, HSTRING *val) {
    WinrtStorageFileObj *obj = WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This);
    return winrt_sys_make_hstring(obj->path, val);
}
static HRESULT STDMETHODCALLTYPE sf_file_item_attrs(void *This, uint32_t *val) {
    WinrtStorageFileObj *obj = WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This);
    if (!val) return E_POINTER;
    *val = map_win32_attrs(GetFileAttributesW(obj->path));
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sf_file_item_date(void *This, int64_t *val) {
    WinrtStorageFileObj *obj = WINRT_SYS_FROM(WinrtStorageFileObj, lpVtblItem, This);
    if (!val) return E_POINTER;
    *val = query_creation_time(obj->path);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sf_file_item_isoftype(void *This, uint32_t type, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = ((type & 1u) != 0u) ? 1 : 0; /* StorageItemTypes_File = 1 */
    return S_OK;
}

static const WinrtStorageFileVtbl g_storage_file_vtbl = {
    sf_file_qi, sf_file_addref, sf_file_release, sf_file_getiids, sf_file_rcn, sf_file_tl,
    sf_file_get_filetype, sf_file_get_contenttype,
    sf_file_unimpl_1, sf_file_unimpl_0,
    sf_file_unimpl_copy1, sf_file_unimpl_copy2, sf_file_unimpl_copy3, sf_file_unimpl_copy1,
    sf_file_unimpl_copy1, sf_file_unimpl_copy2, sf_file_unimpl_copy3, sf_file_unimpl_copy1
};

static const WinrtStorageItemVtbl g_storage_file_item_vtbl = {
    sf_file_item_qi, sf_file_item_addref, sf_file_item_release,
    sf_file_item_getiids, sf_file_item_rcn, sf_file_item_tl,
    sf_file_item_rename1, sf_file_item_rename2,
    sf_file_item_del1, sf_file_item_del2,
    sf_file_item_props,
    sf_file_item_name, sf_file_item_path, sf_file_item_attrs,
    sf_file_item_date, sf_file_item_isoftype
};

HRESULT winrt_sys_create_storage_file(const wchar_t *path, void **out_file) {
    if (!out_file) return E_POINTER;
    *out_file = NULL;
    WinrtStorageFileObj *obj = (WinrtStorageFileObj *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    obj->lpVtbl = &g_storage_file_vtbl;
    obj->lpVtblItem = &g_storage_file_item_vtbl;
    obj->ref_count = 1;
    winrt_sys_normalize_path(path, obj->path, UWP_MAX_PATH_CHARS);
    *out_file = &obj->lpVtbl;
    return S_OK;
}

typedef struct WinrtStorageFolderObj {
    const WinrtStorageFolderVtbl *lpVtbl;
    const WinrtStorageItemVtbl   *lpVtblItem;
    volatile LONG ref_count;
    wchar_t path[UWP_MAX_PATH_CHARS];
} WinrtStorageFolderObj;

static void join_child_path(const wchar_t *base, const wchar_t *child, wchar_t *out, size_t out_chars) {
    wchar_t combined[UWP_MAX_PATH_CHARS];
    size_t blen = wcslen(base);
    if (blen > 0 && base[blen - 1] == L'\\') {
        swprintf(combined, UWP_MAX_PATH_CHARS, L"%ls%ls", base, child);
    } else {
        swprintf(combined, UWP_MAX_PATH_CHARS, L"%ls\\%ls", base, child);
    }
    combined[UWP_MAX_PATH_CHARS - 1u] = L'\0';
    winrt_sys_normalize_path(combined, out, out_chars);
}

static HRESULT STDMETHODCALLTYPE sf_qi(void *This, REFIID riid, void **ppv) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IStorageFolder)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IStorageItem)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblItem;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sf_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtStorageFolderObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE sf_release(void *This) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE sf_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x72d1cb78u, 0xb3efu, 0x4f75u, { 0xa8u, 0x0bu, 0x6fu, 0xd9u, 0xdau, 0xe2u, 0x94u, 0x4bu } },
        { 0x4207a996u, 0xca2fu, 0x42f7u, { 0xbdu, 0xe8u, 0x8bu, 0x10u, 0x45u, 0x7au, 0x7fu, 0x30u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE sf_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.StorageFolder", n);
}
static HRESULT STDMETHODCALLTYPE sf_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sf_create_file(void *This, HSTRING desiredName, uint32_t options, void **op) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    if (!op) return E_POINTER;
    *op = NULL;
    const wchar_t *name = winrt_sys_hstring_buf(desiredName, NULL);
    if (!name || name[0] == L'\0') return E_INVALIDARG;

    wchar_t target[UWP_MAX_PATH_CHARS];
    join_child_path(obj->path, name, target, UWP_MAX_PATH_CHARS);

    DWORD disp = CREATE_NEW;
    if (options == 1u /* ReplaceExisting */) disp = CREATE_ALWAYS;
    else if (options == 3u /* OpenIfExists */) disp = OPEN_ALWAYS;
    else if (options == 2u /* FailIfExists */) disp = CREATE_NEW;
    else if (options == 0u /* GenerateUniqueName */) {
        for (unsigned n = 2; n < 1000 && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES; ++n) {
            wchar_t alt[UWP_MAX_PATH_CHARS];
            swprintf(alt, UWP_MAX_PATH_CHARS, L"%ls (%u)", name, n);
            alt[UWP_MAX_PATH_CHARS - 1u] = L'\0';
            join_child_path(obj->path, alt, target, UWP_MAX_PATH_CHARS);
        }
        disp = CREATE_NEW;
    }

    HANDLE fh = CreateFileW(target, GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        HRESULT err = HRESULT_FROM_WIN32(GetLastError());
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_StorageFile,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            NULL, err, op);
    }
    CloseHandle(fh);

    void *file_obj = NULL;
    HRESULT hr = winrt_sys_create_storage_file(target, &file_obj);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFile,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
        (IUnknown *)file_obj, hr, op);
    if (file_obj) ((IUnknown *)file_obj)->lpVtbl->Release((IUnknown *)file_obj);
    return out_hr;
}

static HRESULT STDMETHODCALLTYPE sf_create_file_def(void *This, HSTRING desiredName, void **op) {
    return sf_create_file(This, desiredName, 2u /* FailIfExists */, op);
}

static HRESULT STDMETHODCALLTYPE sf_create_folder(void *This, HSTRING desiredName, uint32_t options, void **op) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    if (!op) return E_POINTER;
    *op = NULL;
    const wchar_t *name = winrt_sys_hstring_buf(desiredName, NULL);
    if (!name || name[0] == L'\0') return E_INVALIDARG;

    wchar_t target[UWP_MAX_PATH_CHARS];
    join_child_path(obj->path, name, target, UWP_MAX_PATH_CHARS);

    if (options == 0u /* GenerateUniqueName */) {
        for (unsigned n = 2; n < 1000 && GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES; ++n) {
            wchar_t alt[UWP_MAX_PATH_CHARS];
            swprintf(alt, UWP_MAX_PATH_CHARS, L"%ls (%u)", name, n);
            alt[UWP_MAX_PATH_CHARS - 1u] = L'\0';
            join_child_path(obj->path, alt, target, UWP_MAX_PATH_CHARS);
        }
    }

    DWORD existing = GetFileAttributesW(target);
    if (existing != INVALID_FILE_ATTRIBUTES) {
        if ((existing & FILE_ATTRIBUTE_DIRECTORY) == 0 || options == 2u /* FailIfExists */) {
            return winrt_sys_create_completed_async_op_ptr(
                &WINRT_IID_IAsyncOperation_StorageFolder,
                L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                NULL, HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), op);
        }
    } else {
        if (!winrt_sys_ensure_dir_recursive(target)) {
            return winrt_sys_create_completed_async_op_ptr(
                &WINRT_IID_IAsyncOperation_StorageFolder,
                L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
                NULL, HRESULT_FROM_WIN32(GetLastError()), op);
        }
    }

    void *folder_obj = NULL;
    HRESULT hr = winrt_sys_create_storage_folder(target, &folder_obj);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFolder,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
        (IUnknown *)folder_obj, hr, op);
    if (folder_obj) ((IUnknown *)folder_obj)->lpVtbl->Release((IUnknown *)folder_obj);
    return out_hr;
}

static HRESULT STDMETHODCALLTYPE sf_create_folder_def(void *This, HSTRING desiredName, void **op) {
    return sf_create_folder(This, desiredName, 2u /* FailIfExists */, op);
}

static HRESULT STDMETHODCALLTYPE sf_get_file(void *This, HSTRING nameH, void **op) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    if (!op) return E_POINTER;
    *op = NULL;
    const wchar_t *name = winrt_sys_hstring_buf(nameH, NULL);
    wchar_t target[UWP_MAX_PATH_CHARS];
    join_child_path(obj->path, name, target, UWP_MAX_PATH_CHARS);

    DWORD attrs = GetFileAttributesW(target);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_StorageFile,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }

    void *file_obj = NULL;
    HRESULT hr = winrt_sys_create_storage_file(target, &file_obj);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFile,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFile>",
        (IUnknown *)file_obj, hr, op);
    if (file_obj) ((IUnknown *)file_obj)->lpVtbl->Release((IUnknown *)file_obj);
    return out_hr;
}

static HRESULT STDMETHODCALLTYPE sf_get_folder(void *This, HSTRING nameH, void **op) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    if (!op) return E_POINTER;
    *op = NULL;
    const wchar_t *name = winrt_sys_hstring_buf(nameH, NULL);
    wchar_t target[UWP_MAX_PATH_CHARS];
    join_child_path(obj->path, name, target, UWP_MAX_PATH_CHARS);

    DWORD attrs = GetFileAttributesW(target);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_StorageFolder,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }

    void *folder_obj = NULL;
    HRESULT hr = winrt_sys_create_storage_folder(target, &folder_obj);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFolder,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
        (IUnknown *)folder_obj, hr, op);
    if (folder_obj) ((IUnknown *)folder_obj)->lpVtbl->Release((IUnknown *)folder_obj);
    return out_hr;
}

static HRESULT STDMETHODCALLTYPE sf_get_item(void *This, HSTRING nameH, void **op) {
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)This;
    if (!op) return E_POINTER;
    *op = NULL;
    const wchar_t *name = winrt_sys_hstring_buf(nameH, NULL);
    wchar_t target[UWP_MAX_PATH_CHARS];
    join_child_path(obj->path, name, target, UWP_MAX_PATH_CHARS);

    DWORD attrs = GetFileAttributesW(target);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_IStorageItem,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }

    void *raw_obj = NULL;
    HRESULT hr = (attrs & FILE_ATTRIBUTE_DIRECTORY)
        ? winrt_sys_create_storage_folder(target, &raw_obj)
        : winrt_sys_create_storage_file(target, &raw_obj);
    IUnknown *item_iface = NULL;
    if (SUCCEEDED(hr) && raw_obj != NULL) {
        hr = ((IUnknown *)raw_obj)->lpVtbl->QueryInterface(
            (IUnknown *)raw_obj, &WINRT_IID_IStorageItem, (void **)&item_iface);
        ((IUnknown *)raw_obj)->lpVtbl->Release((IUnknown *)raw_obj);
    }
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_IStorageItem,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.IStorageItem>",
        item_iface, hr, op);
    if (item_iface) item_iface->lpVtbl->Release(item_iface);
    return out_hr;
}

static HRESULT sf_enumerate_children(
    WinrtStorageFolderObj *obj,
    int want_files,
    int want_folders,
    const GUID *vec_iid,
    const GUID *op_iid,
    const wchar_t *vec_rcn,
    const wchar_t *op_rcn,
    void **op) {
    if (!op) return E_POINTER;
    *op = NULL;

    wchar_t pattern[UWP_MAX_PATH_CHARS];
    join_child_path(obj->path, L"*", pattern, UWP_MAX_PATH_CHARS);

    uint32_t cap = 16u;
    uint32_t count = 0u;
    IUnknown **list = (IUnknown **)winrt_sys_alloc((size_t)cap * sizeof(IUnknown *));
    if (!list) return E_OUTOFMEMORY;

    WIN32_FIND_DATAW fd;
    memset(&fd, 0, sizeof(fd));
    HANDLE hf = FindFirstFileW(pattern, &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                continue;
            }
            int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if ((is_dir && !want_folders) || (!is_dir && !want_files)) {
                continue;
            }
            wchar_t child[UWP_MAX_PATH_CHARS];
            join_child_path(obj->path, fd.cFileName, child, UWP_MAX_PATH_CHARS);
            void *created = NULL;
            HRESULT chr = is_dir
                ? winrt_sys_create_storage_folder(child, &created)
                : winrt_sys_create_storage_file(child, &created);
            if (SUCCEEDED(chr) && created != NULL) {
                IUnknown *elem = (IUnknown *)created;
                if (want_files && want_folders) {
                    IUnknown *si = NULL;
                    if (SUCCEEDED(elem->lpVtbl->QueryInterface(elem, &WINRT_IID_IStorageItem, (void **)&si))) {
                        elem->lpVtbl->Release(elem);
                        elem = si;
                    }
                }
                if (count == cap) {
                    uint32_t new_cap = cap * 2u;
                    IUnknown **nl = (IUnknown **)winrt_sys_alloc((size_t)new_cap * sizeof(IUnknown *));
                    if (nl) {
                        memcpy(nl, list, (size_t)count * sizeof(IUnknown *));
                        winrt_sys_free(list);
                        list = nl;
                        cap = new_cap;
                    } else {
                        elem->lpVtbl->Release(elem);
                        break;
                    }
                }
                list[count++] = elem;
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    void *view = NULL;
    HRESULT hr = create_storage_ptr_list(vec_iid, vec_rcn, list, count, &view);
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i]) list[i]->lpVtbl->Release(list[i]);
    }
    winrt_sys_free(list);

    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        op_iid, op_rcn, (IUnknown *)view, hr, op);
    if (view) ((IUnknown *)view)->lpVtbl->Release((IUnknown *)view);
    return out_hr;
}

static const GUID WINRT_IID_IAsyncOp_Vec_StorageFile =
    { 0x03362e33u, 0xe413u, 0x5f29u, { 0x97u, 0xd0u, 0x48u, 0xa4u, 0x78u, 0x09u, 0x35u, 0xf9u } };
static const GUID WINRT_IID_IAsyncOp_Vec_StorageFolder =
    { 0xca40b21bu, 0xaeb1u, 0x5a61u, { 0x9eu, 0x08u, 0x3bu, 0xd5u, 0xd9u, 0x59u, 0x40u, 0x23u } };
static const GUID WINRT_IID_IAsyncOp_Vec_IStorageItem =
    { 0x4b1c0fd7u, 0x7a01u, 0x5e7au, { 0xa6u, 0xfeu, 0xbeu, 0x45u, 0x00u, 0x28u, 0x3fu, 0x23u } };

static HRESULT STDMETHODCALLTYPE sf_get_files(void *This, void **op) {
    return sf_enumerate_children((WinrtStorageFolderObj *)This, 1, 0,
        &WINRT_IID_IVectorView_StorageFile, &WINRT_IID_IAsyncOp_Vec_StorageFile,
        L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFile>",
        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFile>>",
        op);
}
static HRESULT STDMETHODCALLTYPE sf_get_folders(void *This, void **op) {
    return sf_enumerate_children((WinrtStorageFolderObj *)This, 0, 1,
        &WINRT_IID_IVectorView_StorageFolder, &WINRT_IID_IAsyncOp_Vec_StorageFolder,
        L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFolder>",
        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.StorageFolder>>",
        op);
}
static HRESULT STDMETHODCALLTYPE sf_get_items(void *This, void **op) {
    return sf_enumerate_children((WinrtStorageFolderObj *)This, 1, 1,
        &WINRT_IID_IVectorView_IStorageItem, &WINRT_IID_IAsyncOp_Vec_IStorageItem,
        L"Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>",
        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.Storage.IStorageItem>>",
        op);
}

static HRESULT STDMETHODCALLTYPE sf_item_qi(void *This, REFIID riid, void **ppv) {
    return sf_qi(WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE sf_item_addref(void *This) {
    return sf_addref(WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This));
}
static ULONG STDMETHODCALLTYPE sf_item_release(void *This) {
    return sf_release(WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This));
}
static HRESULT STDMETHODCALLTYPE sf_item_getiids(void *This, ULONG *c, IID **i) {
    return sf_getiids(WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This), c, i);
}
static HRESULT STDMETHODCALLTYPE sf_item_rcn(void *This, HSTRING *n) {
    return sf_rcn(WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This), n);
}
static HRESULT STDMETHODCALLTYPE sf_item_tl(void *This, TrustLevel *tl) {
    return sf_tl(WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This), tl);
}
static HRESULT STDMETHODCALLTYPE sf_item_rename1(void *This, HSTRING n, void **op) {
    (void)n;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("StorageFolder::RenameAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_item_rename2(void *This, HSTRING n, uint32_t o, void **op) {
    (void)n; (void)o;
    if (op) *op = NULL;
    return winrt_sys_unimplemented("StorageFolder::RenameAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_item_del1(void *This, void **op) {
    WinrtStorageFolderObj *obj = WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This);
    if (!op) return E_POINTER;
    HRESULT hr = RemoveDirectoryW(obj->path) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    return winrt_sys_create_completed_async_action(hr, op);
}
static HRESULT STDMETHODCALLTYPE sf_item_del2(void *This, uint32_t opt, void **op) {
    (void)opt;
    return sf_item_del1(This, op);
}
static HRESULT STDMETHODCALLTYPE sf_item_props(void *This, void **op) {
    if (op) *op = NULL;
    return winrt_sys_unimplemented("StorageFolder::GetBasicPropertiesAsync", This);
}
static HRESULT STDMETHODCALLTYPE sf_item_name(void *This, HSTRING *val) {
    WinrtStorageFolderObj *obj = WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This);
    wchar_t leaf[UWP_MAX_PATH_CHARS];
    extract_leaf_name(obj->path, leaf, UWP_MAX_PATH_CHARS);
    return winrt_sys_make_hstring(leaf, val);
}
static HRESULT STDMETHODCALLTYPE sf_item_path(void *This, HSTRING *val) {
    WinrtStorageFolderObj *obj = WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This);
    return winrt_sys_make_hstring(obj->path, val);
}
static HRESULT STDMETHODCALLTYPE sf_item_attrs(void *This, uint32_t *val) {
    WinrtStorageFolderObj *obj = WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This);
    if (!val) return E_POINTER;
    *val = map_win32_attrs(GetFileAttributesW(obj->path));
    if (*val == 0u) *val = 0x10u; /* Directory */
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sf_item_date(void *This, int64_t *val) {
    WinrtStorageFolderObj *obj = WINRT_SYS_FROM(WinrtStorageFolderObj, lpVtblItem, This);
    if (!val) return E_POINTER;
    *val = query_creation_time(obj->path);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sf_item_isoftype(void *This, uint32_t type, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = ((type & 2u) != 0u) ? 1 : 0; /* StorageItemTypes_Folder = 2 */
    return S_OK;
}

static const WinrtStorageFolderVtbl g_storage_folder_vtbl = {
    sf_qi, sf_addref, sf_release, sf_getiids, sf_rcn, sf_tl,
    sf_create_file_def, sf_create_file,
    sf_create_folder_def, sf_create_folder,
    sf_get_file, sf_get_folder, sf_get_item,
    sf_get_files, sf_get_folders, sf_get_items
};

static const WinrtStorageItemVtbl g_storage_folder_item_vtbl = {
    sf_item_qi, sf_item_addref, sf_item_release,
    sf_item_getiids, sf_item_rcn, sf_item_tl,
    sf_item_rename1, sf_item_rename2,
    sf_item_del1, sf_item_del2,
    sf_item_props,
    sf_item_name, sf_item_path, sf_item_attrs,
    sf_item_date, sf_item_isoftype
};

HRESULT winrt_sys_create_storage_folder(const wchar_t *path, void **out_folder) {
    if (!out_folder) return E_POINTER;
    *out_folder = NULL;
    WinrtStorageFolderObj *obj = (WinrtStorageFolderObj *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    obj->lpVtbl = &g_storage_folder_vtbl;
    obj->lpVtblItem = &g_storage_folder_item_vtbl;
    obj->ref_count = 1;
    winrt_sys_normalize_path(path, obj->path, UWP_MAX_PATH_CHARS);
    *out_folder = &obj->lpVtbl;
    return S_OK;
}

/* StorageFolder Activation Factory */
typedef struct WinrtStorageFolderFactory {
    const WinrtActivationFactoryVtbl     *lpVtblAct;
    const WinrtStorageFolderStaticsVtbl  *lpVtblStatics;
} WinrtStorageFolderFactory;

static WinrtStorageFolderFactory g_sf_factory;

static HRESULT STDMETHODCALLTYPE sf_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_sf_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IStorageFolderStatics)) {
        *ppv = &g_sf_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE sf_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE sf_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE sf_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x08f327ffu, 0x85d5u, 0x48b9u, { 0xaeu, 0xe9u, 0x28u, 0x51u, 0x1eu, 0x33u, 0x9fu, 0x9fu } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE sf_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE sf_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE sf_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("StorageFolder::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE sf_fac_get_from_path(void *This, HSTRING pathH, void **op) {
    (void)This;
    if (!op) return E_POINTER;
    *op = NULL;
    const wchar_t *raw = winrt_sys_hstring_buf(pathH, NULL);
    if (!raw || raw[0] == L'\0') return E_INVALIDARG;

    wchar_t norm[UWP_MAX_PATH_CHARS];
    winrt_sys_normalize_path(raw, norm, UWP_MAX_PATH_CHARS);
    DWORD attrs = GetFileAttributesW(norm);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return winrt_sys_create_completed_async_op_ptr(
            &WINRT_IID_IAsyncOperation_StorageFolder,
            L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
            NULL, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), op);
    }

    void *folder_obj = NULL;
    HRESULT hr = winrt_sys_create_storage_folder(norm, &folder_obj);
    HRESULT out_hr = winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_StorageFolder,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.StorageFolder>",
        (IUnknown *)folder_obj, hr, op);
    if (folder_obj) ((IUnknown *)folder_obj)->lpVtbl->Release((IUnknown *)folder_obj);
    return out_hr;
}

static const WinrtActivationFactoryVtbl g_sf_act_vtbl = {
    sf_fac_qi, sf_fac_addref, sf_fac_release, sf_fac_getiids, sf_fac_rcn, sf_fac_tl, sf_fac_act
};
static const WinrtStorageFolderStaticsVtbl g_sf_stat_vtbl = {
    sf_fac_qi, sf_fac_addref, sf_fac_release, sf_fac_getiids, sf_fac_rcn, sf_fac_tl, sf_fac_get_from_path
};
static WinrtStorageFolderFactory g_sf_factory = { &g_sf_act_vtbl, &g_sf_stat_vtbl };

HRESULT winrt_sys_get_storage_folder_factory(REFIID iid, void **out_factory) {
    return sf_fac_qi(&g_sf_factory.lpVtblAct, iid, out_factory);
}

/* ============================================================================
 * ApplicationDataContainer + Persistent PropertySet (INI-backed)
 * ============================================================================ */

typedef struct SettingsEntry {
    wchar_t *key;
    IUnknown *value;
} SettingsEntry;

typedef struct SettingsListener {
    EventRegistrationToken token;
    IUnknown *handler;
} SettingsListener;

typedef struct WinrtAppDataContainerObj WinrtAppDataContainerObj;

typedef struct WinrtPropertySetObj {
    const WinrtPropertySetVtbl     *lpVtbl;
    const WinrtObservableMapVtbl   *lpVtblObs;
    const WinrtMapHstringPtrVtbl   *lpVtblMap;
    const WinrtIterableVtbl        *lpVtblIter;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    uint32_t count;
    uint32_t capacity;
    SettingsEntry *entries;
    uint32_t listener_count;
    SettingsListener listeners[16];
    int64_t next_token;
    WinrtAppDataContainerObj *owner_container; /* non-owning backref */
} WinrtPropertySetObj;

struct WinrtAppDataContainerObj {
    const WinrtApplicationDataContainerVtbl *lpVtbl;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    wchar_t name[UWP_MAX_IDENTITY_CHARS];
    wchar_t section[UWP_MAX_IDENTITY_CHARS];
    uint32_t locality;
    wchar_t ini_path[UWP_MAX_PATH_CHARS];
    WinrtPropertySetObj *values;
    uint32_t sub_count;
    uint32_t sub_cap;
    WinrtAppDataContainerObj **subs;
    int is_root;
};

static void settings_save_root(WinrtAppDataContainerObj *root);

static WinrtAppDataContainerObj *find_root_container(WinrtAppDataContainerObj *c) {
    return c; /* Each root saves its entire tree; subcontainers hold same ini_path */
}

/* Serialize PropertyValue -> UTF-8 "type:payload\r\n" */
static void append_utf8_bytes(char **buf, size_t *len, size_t *cap, const char *s, size_t slen) {
    if (*len + slen + 1u > *cap) {
        size_t ncap = (*cap == 0u) ? 512u : (*cap * 2u);
        while (ncap < *len + slen + 1u) ncap *= 2u;
        char *nb = (char *)winrt_sys_alloc(ncap);
        if (!nb) return;
        if (*buf && *len > 0) memcpy(nb, *buf, *len);
        winrt_sys_free(*buf);
        *buf = nb;
        *cap = ncap;
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void append_wide_as_utf8_escaped(char **buf, size_t *len, size_t *cap, const wchar_t *ws, int escape) {
    if (!ws || ws[0] == L'\0') return;
    int need = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (need <= 1) return;
    char *u8 = (char *)winrt_sys_alloc((size_t)need);
    if (!u8) return;
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, u8, need, NULL, NULL);
    for (int i = 0; i < need - 1; ++i) {
        char c = u8[i];
        if (escape) {
            if (c == '\\') { append_utf8_bytes(buf, len, cap, "\\\\", 2); continue; }
            if (c == '\n') { append_utf8_bytes(buf, len, cap, "\\n", 2); continue; }
            if (c == '\r') { append_utf8_bytes(buf, len, cap, "\\r", 2); continue; }
            if (c == '=')  { append_utf8_bytes(buf, len, cap, "\\e", 2); continue; }
        }
        append_utf8_bytes(buf, len, cap, &c, 1);
    }
    winrt_sys_free(u8);
}

static void serialize_container_tree(WinrtAppDataContainerObj *c, char **buf, size_t *len, size_t *cap) {
    append_utf8_bytes(buf, len, cap, "[", 1);
    append_wide_as_utf8_escaped(buf, len, cap, c->section[0] ? c->section : L"root", 0);
    append_utf8_bytes(buf, len, cap, "]\n", 2);

    if (c->values) {
        EnterCriticalSection(&c->values->lock);
        for (uint32_t i = 0; i < c->values->count; ++i) {
            SettingsEntry *e = &c->values->entries[i];
            if (!e->key || !e->value) continue;
            typedef struct { const WinrtPropertyValueVtbl *lpVtbl; } PvIf;
            PvIf *pv = NULL;
            if (FAILED(e->value->lpVtbl->QueryInterface(e->value, &WINRT_IID_IPropertyValue, (void **)&pv)) || !pv) {
                continue;
            }
            WinrtPropertyType pt = WinrtPropertyType_Empty;
            pv->lpVtbl->get_Type(pv, &pt);

            char val_buf[128];
            val_buf[0] = '\0';
            int ok = 1;
            HSTRING hs = NULL;

            append_wide_as_utf8_escaped(buf, len, cap, e->key, 1);
            append_utf8_bytes(buf, len, cap, "=", 1);

            switch (pt) {
            case WinrtPropertyType_Int32: {
                int32_t v = 0; pv->lpVtbl->GetInt32(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "i32:%d", v);
                break;
            }
            case WinrtPropertyType_UInt32: {
                uint32_t v = 0; pv->lpVtbl->GetUInt32(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "u32:%u", v);
                break;
            }
            case WinrtPropertyType_Int64: {
                int64_t v = 0; pv->lpVtbl->GetInt64(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "i64:%lld", (long long)v);
                break;
            }
            case WinrtPropertyType_UInt64: {
                uint64_t v = 0; pv->lpVtbl->GetUInt64(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "u64:%llu", (unsigned long long)v);
                break;
            }
            case WinrtPropertyType_Single: {
                float v = 0.0f; pv->lpVtbl->GetSingle(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "f32:%.9g", (double)v);
                break;
            }
            case WinrtPropertyType_Double: {
                double v = 0.0; pv->lpVtbl->GetDouble(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "f64:%.17g", v);
                break;
            }
            case WinrtPropertyType_Boolean: {
                boolean v = 0; pv->lpVtbl->GetBoolean(pv, &v);
                snprintf(val_buf, sizeof(val_buf), "bool:%u", v ? 1u : 0u);
                break;
            }
            case WinrtPropertyType_String: {
                pv->lpVtbl->GetString(pv, &hs);
                append_utf8_bytes(buf, len, cap, "str:", 4);
                append_wide_as_utf8_escaped(buf, len, cap, winrt_sys_hstring_buf(hs, NULL), 1);
                WindowsDeleteString(hs);
                ok = 0; /* already appended */
                break;
            }
            default:
                snprintf(val_buf, sizeof(val_buf), "empty:");
                break;
            }
            if (ok && val_buf[0] != '\0') {
                append_utf8_bytes(buf, len, cap, val_buf, strlen(val_buf));
            }
            append_utf8_bytes(buf, len, cap, "\n", 1);
            pv->lpVtbl->Release(pv);
        }
        LeaveCriticalSection(&c->values->lock);
    }

    EnterCriticalSection(&c->lock);
    for (uint32_t i = 0; i < c->sub_count; ++i) {
        if (c->subs[i]) {
            serialize_container_tree(c->subs[i], buf, len, cap);
        }
    }
    LeaveCriticalSection(&c->lock);
}

static WinrtAppDataContainerObj *g_local_settings_root = NULL;
static WinrtAppDataContainerObj *g_roaming_settings_root = NULL;

static void settings_save_root(WinrtAppDataContainerObj *c) {
    WinrtAppDataContainerObj *root = c;
    if (c != NULL) {
        if (c->locality == 0 && g_local_settings_root != NULL) root = g_local_settings_root;
        else if (c->locality == 1 && g_roaming_settings_root != NULL) root = g_roaming_settings_root;
    }
    if (!root || root->ini_path[0] == L'\0') return;

    char *buf = NULL;
    size_t len = 0, cap = 0;
    serialize_container_tree(root, &buf, &len, &cap);
    if (!buf) return;

    HANDLE fh = CreateFileW(root->ini_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(fh, buf, (DWORD)len, &written, NULL);
        CloseHandle(fh);
    }
    winrt_sys_free(buf);
}

static wchar_t *utf8_unescaped_to_wstr(const char *s, size_t slen) {
    char *tmp = (char *)winrt_sys_alloc(slen + 1u);
    if (!tmp) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < slen; ++i) {
        if (s[i] == '\\' && i + 1u < slen) {
            char n = s[i + 1u];
            if (n == '\\') { tmp[w++] = '\\'; ++i; continue; }
            if (n == 'n')  { tmp[w++] = '\n'; ++i; continue; }
            if (n == 'r')  { tmp[w++] = '\r'; ++i; continue; }
            if (n == 'e')  { tmp[w++] = '=';  ++i; continue; }
        }
        tmp[w++] = s[i];
    }
    tmp[w] = '\0';

    int wneed = MultiByteToWideChar(CP_UTF8, 0, tmp, -1, NULL, 0);
    wchar_t *ws = (wchar_t *)winrt_sys_alloc((size_t)(wneed > 0 ? wneed : 1) * sizeof(wchar_t));
    if (ws && wneed > 0) {
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, ws, wneed);
    }
    winrt_sys_free(tmp);
    return ws;
}

static HRESULT create_container_obj(
    const wchar_t *name,
    const wchar_t *section,
    uint32_t locality,
    const wchar_t *ini_path,
    int is_root,
    WinrtAppDataContainerObj **out_c);

static WinrtAppDataContainerObj *ensure_container_path(
    WinrtAppDataContainerObj *root,
    const wchar_t *section) {
    if (!section || wcscmp(section, L"root") == 0 || section[0] == L'\0') {
        return root;
    }
    WinrtAppDataContainerObj *cur = root;
    wchar_t tmp[UWP_MAX_IDENTITY_CHARS];
    winrt_sys_copy_wstr(tmp, UWP_MAX_IDENTITY_CHARS, section);

    wchar_t *p = tmp;
    while (*p != L'\0') {
        wchar_t *slash = wcschr(p, L'\\');
        if (slash) *slash = L'\0';

        WinrtAppDataContainerObj *found = NULL;
        EnterCriticalSection(&cur->lock);
        for (uint32_t i = 0; i < cur->sub_count; ++i) {
            if (wcscmp(cur->subs[i]->name, p) == 0) {
                found = cur->subs[i];
                break;
            }
        }
        if (!found) {
            wchar_t child_sec[UWP_MAX_IDENTITY_CHARS];
            if (cur->is_root) {
                winrt_sys_copy_wstr(child_sec, UWP_MAX_IDENTITY_CHARS, p);
            } else {
                swprintf(child_sec, UWP_MAX_IDENTITY_CHARS, L"%ls\\%ls", cur->section, p);
                child_sec[UWP_MAX_IDENTITY_CHARS - 1u] = L'\0';
            }
            if (SUCCEEDED(create_container_obj(p, child_sec, cur->locality, cur->ini_path, 0, &found))) {
                WinrtAppDataContainerObj **ns = (WinrtAppDataContainerObj **)winrt_sys_alloc(
                    (size_t)(cur->sub_count + 1u) * sizeof(WinrtAppDataContainerObj *));
                if (ns) {
                    if (cur->subs && cur->sub_count > 0) {
                        memcpy(ns, cur->subs, (size_t)cur->sub_count * sizeof(WinrtAppDataContainerObj *));
                    }
                    winrt_sys_free(cur->subs);
                    cur->subs = ns;
                    cur->subs[cur->sub_count++] = found;
                }
            }
        }
        LeaveCriticalSection(&cur->lock);
        if (!found) break;
        cur = found;
        if (!slash) break;
        p = slash + 1;
    }
    return cur;
}

static void settings_insert_raw(WinrtPropertySetObj *ps, const wchar_t *key, IUnknown *val);

static void settings_load_root(WinrtAppDataContainerObj *root) {
    if (!root || root->ini_path[0] == L'\0') return;
    HANDLE fh = CreateFileW(root->ini_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) return;

    size_t cap = 4096u, len = 0u;
    char *buf = (char *)winrt_sys_alloc(cap);
    if (!buf) {
        CloseHandle(fh);
        return;
    }
    for (;;) {
        DWORD rd = 0;
        if (len + 1024u > cap) {
            size_t ncap = cap * 2u;
            char *nb = (char *)winrt_sys_alloc(ncap);
            if (!nb) break;
            memcpy(nb, buf, len);
            winrt_sys_free(buf);
            buf = nb;
            cap = ncap;
        }
        if (!ReadFile(fh, buf + len, 1024u, &rd, NULL) || rd == 0) break;
        len += rd;
    }
    CloseHandle(fh);
    buf[len] = '\0';

    WinrtAppDataContainerObj *cur = root;
    size_t pos = 0;
    while (pos < len) {
        size_t line_end = pos;
        while (line_end < len && buf[line_end] != '\n' && buf[line_end] != '\r') ++line_end;
        size_t llen = line_end - pos;
        if (llen > 0) {
            const char *line = buf + pos;
            if (line[0] == '[' && line[llen - 1u] == ']') {
                wchar_t *sec = utf8_unescaped_to_wstr(line + 1, llen - 2u);
                if (sec) {
                    cur = ensure_container_path(root, sec);
                    winrt_sys_free(sec);
                }
            } else {
                const char *eq = (const char *)memchr(line, '=', llen);
                if (eq && eq > line && cur && cur->values) {
                    wchar_t *k = utf8_unescaped_to_wstr(line, (size_t)(eq - line));
                    const char *vp = eq + 1;
                    size_t vlen = llen - (size_t)(vp - line);
                    char *vstr = (char *)winrt_sys_alloc(vlen + 1u);
                    if (k && vstr) {
                        memcpy(vstr, vp, vlen);
                        vstr[vlen] = '\0';
                        void *pv = NULL;
                        if (strncmp(vstr, "i32:", 4) == 0) {
                            winrt_sys_create_property_value_int32((int32_t)strtol(vstr + 4, NULL, 10), &pv);
                        } else if (strncmp(vstr, "u32:", 4) == 0) {
                            winrt_sys_create_property_value_uint32((uint32_t)strtoul(vstr + 4, NULL, 10), &pv);
                        } else if (strncmp(vstr, "i64:", 4) == 0) {
                            winrt_sys_create_property_value_int64((int64_t)strtoll(vstr + 4, NULL, 10), &pv);
                        } else if (strncmp(vstr, "u64:", 4) == 0) {
                            winrt_sys_create_property_value_uint64((uint64_t)strtoull(vstr + 4, NULL, 10), &pv);
                        } else if (strncmp(vstr, "f32:", 4) == 0) {
                            winrt_sys_create_property_value_single(strtof(vstr + 4, NULL), &pv);
                        } else if (strncmp(vstr, "f64:", 4) == 0) {
                            winrt_sys_create_property_value_double(strtod(vstr + 4, NULL), &pv);
                        } else if (strncmp(vstr, "bool:", 5) == 0) {
                            winrt_sys_create_property_value_boolean(atoi(vstr + 5) != 0 ? 1 : 0, &pv);
                        } else if (strncmp(vstr, "str:", 4) == 0) {
                            wchar_t *ws = utf8_unescaped_to_wstr(vstr + 4, vlen - 4u);
                            if (ws) {
                                winrt_sys_create_property_value_string(ws, &pv);
                                winrt_sys_free(ws);
                            }
                        }
                        if (pv) {
                            settings_insert_raw(cur->values, k, (IUnknown *)pv);
                            ((IUnknown *)pv)->lpVtbl->Release((IUnknown *)pv);
                        }
                    }
                    winrt_sys_free(k);
                    winrt_sys_free(vstr);
                }
            }
        }
        pos = line_end + 1u;
    }
    winrt_sys_free(buf);
}

/* KVP<HSTRING, IInspectable*> */
typedef struct WinrtKvpObj {
    const WinrtKvpHstringPtrVtbl *lpVtbl;
    volatile LONG ref_count;
    HSTRING key;
    IUnknown *value;
} WinrtKvpObj;

static HRESULT STDMETHODCALLTYPE kvp_qi(void *This, REFIID riid, void **ppv) {
    WinrtKvpObj *obj = (WinrtKvpObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IKeyValuePair_HSTRING_IInspectable)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE kvp_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtKvpObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE kvp_release(void *This) {
    WinrtKvpObj *obj = (WinrtKvpObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->key) WindowsDeleteString(obj->key);
        if (obj->value) obj->value->lpVtbl->Release(obj->value);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE kvp_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IKeyValuePair_HSTRING_IInspectable, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE kvp_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IKeyValuePair`2<String, Object>", n);
}
static HRESULT STDMETHODCALLTYPE kvp_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE kvp_get_key(void *This, HSTRING *k) {
    return winrt_sys_dup_hstring(((WinrtKvpObj *)This)->key, k);
}
static HRESULT STDMETHODCALLTYPE kvp_get_val(void *This, void **v) {
    WinrtKvpObj *obj = (WinrtKvpObj *)This;
    if (!v) return E_POINTER;
    *v = obj->value;
    if (obj->value) obj->value->lpVtbl->AddRef(obj->value);
    return S_OK;
}
static const WinrtKvpHstringPtrVtbl g_kvp_vtbl = {
    kvp_qi, kvp_addref, kvp_release, kvp_getiids, kvp_rcn, kvp_tl, kvp_get_key, kvp_get_val
};

static IUnknown *create_kvp_obj(const wchar_t *key, IUnknown *val, const GUID *kvp_iid) {
    (void)kvp_iid;
    WinrtKvpObj *o = (WinrtKvpObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return NULL;
    o->lpVtbl = &g_kvp_vtbl;
    o->ref_count = 1;
    winrt_sys_make_hstring(key, &o->key);
    o->value = val;
    if (val) val->lpVtbl->AddRef(val);
    return (IUnknown *)o;
}

/* Snapshot MapView + Iterator for PropertySet */
typedef struct WinrtMapSnapshot {
    const WinrtMapViewHstringPtrVtbl *lpVtbl;
    const WinrtIterableVtbl          *lpVtblIter;
    volatile LONG ref_count;
    GUID view_iid;
    GUID iter_iid;
    GUID kvp_iid;
    uint32_t count;
    SettingsEntry *entries;
} WinrtMapSnapshot;

typedef struct WinrtMapIterator {
    const WinrtIteratorPtrVtbl *lpVtbl;
    volatile LONG ref_count;
    GUID iter_iid;
    GUID kvp_iid;
    uint32_t count;
    uint32_t index;
    IUnknown **kvps;
} WinrtMapIterator;

static HRESULT STDMETHODCALLTYPE map_it_qi(void *This, REFIID riid, void **ppv) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &obj->iter_iid)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE map_it_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtMapIterator *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE map_it_release(void *This) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < obj->count; ++i) {
            if (obj->kvps[i]) obj->kvps[i]->lpVtbl->Release(obj->kvps[i]);
        }
        winrt_sys_free(obj->kvps);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE map_it_getiids(void *This, ULONG *c, IID **i) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    return winrt_sys_get_iids_copy(&obj->iter_iid, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE map_it_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator", n);
}
static HRESULT STDMETHODCALLTYPE map_it_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_it_current(void *This, void **val) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    if (!val) return E_POINTER;
    *val = NULL;
    if (obj->index >= obj->count) return E_BOUNDS;
    obj->kvps[obj->index]->lpVtbl->AddRef(obj->kvps[obj->index]);
    *val = obj->kvps[obj->index];
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_it_has(void *This, boolean *val) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    if (!val) return E_POINTER;
    *val = (obj->index < obj->count) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_it_next(void *This, boolean *val) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    if (!val) return E_POINTER;
    if (obj->index < obj->count) obj->index++;
    *val = (obj->index < obj->count) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_it_getmany(void *This, uint32_t cap, void **out, uint32_t *actual) {
    WinrtMapIterator *obj = (WinrtMapIterator *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (obj->index >= obj->count || cap == 0 || !out) return S_OK;
    uint32_t n = obj->count - obj->index;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; ++i) {
        obj->kvps[obj->index + i]->lpVtbl->AddRef(obj->kvps[obj->index + i]);
        out[i] = obj->kvps[obj->index + i];
    }
    obj->index += n;
    *actual = n;
    return S_OK;
}

static const WinrtIteratorPtrVtbl g_map_it_vtbl = {
    map_it_qi, map_it_addref, map_it_release, map_it_getiids, map_it_rcn, map_it_tl,
    map_it_current, map_it_has, map_it_next, map_it_getmany
};

static HRESULT STDMETHODCALLTYPE map_snap_qi(void *This, REFIID riid, void **ppv) {
    WinrtMapSnapshot *obj = (WinrtMapSnapshot *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &obj->view_iid)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &obj->iter_iid)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE map_snap_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtMapSnapshot *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE map_snap_release(void *This) {
    WinrtMapSnapshot *obj = (WinrtMapSnapshot *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < obj->count; ++i) {
            winrt_sys_free(obj->entries[i].key);
            if (obj->entries[i].value) obj->entries[i].value->lpVtbl->Release(obj->entries[i].value);
        }
        winrt_sys_free(obj->entries);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE map_snap_getiids(void *This, ULONG *c, IID **i) {
    WinrtMapSnapshot *obj = (WinrtMapSnapshot *)This;
    GUID list[2] = { obj->view_iid, obj->iter_iid };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE map_snap_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IMapView", n);
}
static HRESULT STDMETHODCALLTYPE map_snap_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_snap_lookup(void *This, HSTRING keyH, void **val) {
    WinrtMapSnapshot *obj = (WinrtMapSnapshot *)This;
    if (!val) return E_POINTER;
    *val = NULL;
    const wchar_t *k = winrt_sys_hstring_buf(keyH, NULL);
    for (uint32_t i = 0; i < obj->count; ++i) {
        if (wcscmp(obj->entries[i].key, k) == 0) {
            *val = obj->entries[i].value;
            if (obj->entries[i].value) obj->entries[i].value->lpVtbl->AddRef(obj->entries[i].value);
            return S_OK;
        }
    }
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE map_snap_size(void *This, uint32_t *sz) {
    if (!sz) return E_POINTER;
    *sz = ((WinrtMapSnapshot *)This)->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_snap_haskey(void *This, HSTRING keyH, boolean *found) {
    WinrtMapSnapshot *obj = (WinrtMapSnapshot *)This;
    if (!found) return E_POINTER;
    *found = 0;
    const wchar_t *k = winrt_sys_hstring_buf(keyH, NULL);
    for (uint32_t i = 0; i < obj->count; ++i) {
        if (wcscmp(obj->entries[i].key, k) == 0) {
            *found = 1;
            break;
        }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE map_snap_split(void *This, void **first, void **second) {
    if (first) *first = NULL;
    if (second) *second = NULL;
    return winrt_sys_unimplemented("IMapView::Split", This);
}
static HRESULT STDMETHODCALLTYPE map_snap_iter_qi(void *This, REFIID riid, void **ppv) {
    return map_snap_qi(WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE map_snap_iter_addref(void *This) {
    return map_snap_addref(WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This));
}
static ULONG STDMETHODCALLTYPE map_snap_iter_release(void *This) {
    return map_snap_release(WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE map_snap_iter_getiids(void *This, ULONG *c, IID **i) {
    return map_snap_getiids(WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This), c, i);
}
static HRESULT STDMETHODCALLTYPE map_snap_iter_rcn(void *This, HSTRING *n) {
    return map_snap_rcn(WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This), n);
}
static HRESULT STDMETHODCALLTYPE map_snap_iter_tl(void *This, TrustLevel *tl) {
    return map_snap_tl(WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This), tl);
}
static HRESULT STDMETHODCALLTYPE map_snap_first(void *This, void **first) {
    WinrtMapSnapshot *obj = WINRT_SYS_FROM(WinrtMapSnapshot, lpVtblIter, This);
    if (!first) return E_POINTER;
    *first = NULL;
    WinrtMapIterator *it = (WinrtMapIterator *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = &g_map_it_vtbl;
    it->ref_count = 1;
    it->iter_iid = obj->iter_iid;
    it->kvp_iid = obj->kvp_iid;
    it->count = obj->count;
    if (obj->count > 0) {
        it->kvps = (IUnknown **)winrt_sys_alloc((size_t)obj->count * sizeof(IUnknown *));
        if (!it->kvps) { winrt_sys_free(it); return E_OUTOFMEMORY; }
        for (uint32_t i = 0; i < obj->count; ++i) {
            it->kvps[i] = create_kvp_obj(obj->entries[i].key, obj->entries[i].value, &obj->kvp_iid);
        }
    }
    *first = it;
    return S_OK;
}

static const WinrtMapViewHstringPtrVtbl g_map_snap_vtbl = {
    map_snap_qi, map_snap_addref, map_snap_release, map_snap_getiids, map_snap_rcn, map_snap_tl,
    map_snap_lookup, map_snap_size, map_snap_haskey, map_snap_split
};
static const WinrtIterableVtbl g_map_snap_iter_vtbl = {
    map_snap_iter_qi, map_snap_iter_addref, map_snap_iter_release,
    map_snap_iter_getiids, map_snap_iter_rcn, map_snap_iter_tl, map_snap_first
};

/* MapChangedEventArgs */
typedef struct WinrtMapChangedArgsObj {
    const WinrtMapChangedArgsHstringVtbl *lpVtbl;
    volatile LONG ref_count;
    uint32_t change;
    HSTRING key;
} WinrtMapChangedArgsObj;

static HRESULT STDMETHODCALLTYPE mc_args_qi(void *This, REFIID riid, void **ppv) {
    WinrtMapChangedArgsObj *obj = (WinrtMapChangedArgsObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IMapChangedEventArgs_HSTRING)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE mc_args_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtMapChangedArgsObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE mc_args_release(void *This) {
    WinrtMapChangedArgsObj *obj = (WinrtMapChangedArgsObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->key) WindowsDeleteString(obj->key);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE mc_args_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IMapChangedEventArgs_HSTRING, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE mc_args_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IMapChangedEventArgs`1<String>", n);
}
static HRESULT STDMETHODCALLTYPE mc_args_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mc_args_get_change(void *This, uint32_t *val) {
    if (!val) return E_POINTER;
    *val = ((WinrtMapChangedArgsObj *)This)->change;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE mc_args_get_key(void *This, HSTRING *val) {
    return winrt_sys_dup_hstring(((WinrtMapChangedArgsObj *)This)->key, val);
}
static const WinrtMapChangedArgsHstringVtbl g_mc_args_vtbl = {
    mc_args_qi, mc_args_addref, mc_args_release, mc_args_getiids, mc_args_rcn, mc_args_tl,
    mc_args_get_change, mc_args_get_key
};

static void ps_notify_changed(WinrtPropertySetObj *ps, uint32_t change, const wchar_t *key) {
    IUnknown *handlers[16];
    uint32_t cnt = 0;
    EnterCriticalSection(&ps->lock);
    for (uint32_t i = 0; i < ps->listener_count && i < 16; ++i) {
        if (ps->listeners[i].handler) {
            ps->listeners[i].handler->lpVtbl->AddRef(ps->listeners[i].handler);
            handlers[cnt++] = ps->listeners[i].handler;
        }
    }
    LeaveCriticalSection(&ps->lock);
    if (cnt == 0) return;

    WinrtMapChangedArgsObj *args = (WinrtMapChangedArgsObj *)winrt_sys_alloc(sizeof(*args));
    if (args) {
        args->lpVtbl = &g_mc_args_vtbl;
        args->ref_count = 1;
        args->change = change;
        winrt_sys_make_hstring(key, &args->key);
    }
    for (uint32_t i = 0; i < cnt; ++i) {
        typedef struct { const WinrtMapChangedHandlerVtbl *lpVtbl; } HIf;
        ((HIf *)handlers[i])->lpVtbl->Invoke(handlers[i], &ps->lpVtblObs, args);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
    if (args) mc_args_release(args);
}

static void settings_insert_raw(WinrtPropertySetObj *ps, const wchar_t *key, IUnknown *val) {
    size_t klen = wcslen(key);
    EnterCriticalSection(&ps->lock);
    for (uint32_t i = 0; i < ps->count; ++i) {
        if (wcscmp(ps->entries[i].key, key) == 0) {
            if (ps->entries[i].value) ps->entries[i].value->lpVtbl->Release(ps->entries[i].value);
            ps->entries[i].value = val;
            if (val) val->lpVtbl->AddRef(val);
            LeaveCriticalSection(&ps->lock);
            return;
        }
    }
    if (ps->count == ps->capacity) {
        uint32_t ncap = ps->capacity ? ps->capacity * 2u : 16u;
        SettingsEntry *ne = (SettingsEntry *)winrt_sys_alloc((size_t)ncap * sizeof(SettingsEntry));
        if (!ne) {
            LeaveCriticalSection(&ps->lock);
            return;
        }
        if (ps->entries && ps->count > 0) memcpy(ne, ps->entries, (size_t)ps->count * sizeof(SettingsEntry));
        winrt_sys_free(ps->entries);
        ps->entries = ne;
        ps->capacity = ncap;
    }
    wchar_t *kcopy = (wchar_t *)winrt_sys_alloc((klen + 1u) * sizeof(wchar_t));
    if (kcopy) {
        memcpy(kcopy, key, (klen + 1u) * sizeof(wchar_t));
        ps->entries[ps->count].key = kcopy;
        ps->entries[ps->count].value = val;
        if (val) val->lpVtbl->AddRef(val);
        ps->count++;
    }
    LeaveCriticalSection(&ps->lock);
}

static HRESULT STDMETHODCALLTYPE ps_qi(void *This, REFIID riid, void **ppv) {
    WinrtPropertySetObj *obj = (WinrtPropertySetObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPropertySet)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IObservableMap_HSTRING_IInspectable)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblObs;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IMap_HSTRING_IInspectable)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblMap;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_KVP_HSTRING_IInspectable)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ps_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPropertySetObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE ps_release(void *This) {
    WinrtPropertySetObj *obj = (WinrtPropertySetObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < obj->count; ++i) {
            winrt_sys_free(obj->entries[i].key);
            if (obj->entries[i].value) obj->entries[i].value->lpVtbl->Release(obj->entries[i].value);
        }
        winrt_sys_free(obj->entries);
        for (uint32_t i = 0; i < obj->listener_count; ++i) {
            if (obj->listeners[i].handler) obj->listeners[i].handler->lpVtbl->Release(obj->listeners[i].handler);
        }
        DeleteCriticalSection(&obj->lock);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE ps_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x8a43ed9fu, 0xf4e6u, 0x4421u, { 0xacu, 0xf9u, 0x1du, 0xabu, 0x29u, 0x86u, 0x82u, 0x0cu } },
        { 0x236aac9du, 0xfb12u, 0x5c4du, { 0xa4u, 0x1cu, 0x9eu, 0x44u, 0x5fu, 0xb4u, 0xd7u, 0xecu } },
        { 0x1b0d3570u, 0x0877u, 0x5ec2u, { 0x8au, 0x2cu, 0x3bu, 0x95u, 0x39u, 0x50u, 0x6au, 0xcau } },
        { 0xfe2f3d47u, 0x5d47u, 0x5499u, { 0x83u, 0x74u, 0x43u, 0x0cu, 0x7cu, 0xdau, 0x02u, 0x04u } }
    };
    return winrt_sys_get_iids_copy(list, 4, c, i);
}
static HRESULT STDMETHODCALLTYPE ps_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.PropertySet", n);
}
static HRESULT STDMETHODCALLTYPE ps_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_obs_qi(void *This, REFIID r, void **p) {
    return ps_qi(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This), r, p);
}
static ULONG STDMETHODCALLTYPE ps_obs_addref(void *This) {
    return ps_addref(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This));
}
static ULONG STDMETHODCALLTYPE ps_obs_release(void *This) {
    return ps_release(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This));
}
static HRESULT STDMETHODCALLTYPE ps_obs_getiids(void *This, ULONG *c, IID **i) {
    return ps_getiids(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This), c, i);
}
static HRESULT STDMETHODCALLTYPE ps_obs_rcn(void *This, HSTRING *n) {
    return ps_rcn(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This), n);
}
static HRESULT STDMETHODCALLTYPE ps_obs_tl(void *This, TrustLevel *tl) {
    return ps_tl(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This), tl);
}
static HRESULT STDMETHODCALLTYPE ps_obs_add_mc(void *This, void *vhnd, EventRegistrationToken *tok) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This);
    if (!vhnd || !tok) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    if (obj->listener_count >= 16u) {
        LeaveCriticalSection(&obj->lock);
        return E_OUTOFMEMORY;
    }
    IUnknown *h = (IUnknown *)vhnd;
    h->lpVtbl->AddRef(h);
    tok->value = ++obj->next_token;
    obj->listeners[obj->listener_count].token = *tok;
    obj->listeners[obj->listener_count].handler = h;
    obj->listener_count++;
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_obs_rem_mc(void *This, EventRegistrationToken tok) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblObs, This);
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->listener_count; ++i) {
        if (obj->listeners[i].token.value == tok.value) {
            if (obj->listeners[i].handler) obj->listeners[i].handler->lpVtbl->Release(obj->listeners[i].handler);
            for (uint32_t j = i + 1u; j < obj->listener_count; ++j) obj->listeners[j - 1u] = obj->listeners[j];
            obj->listener_count--;
            break;
        }
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_map_qi(void *This, REFIID r, void **p) {
    return ps_qi(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This), r, p);
}
static ULONG STDMETHODCALLTYPE ps_map_addref(void *This) {
    return ps_addref(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This));
}
static ULONG STDMETHODCALLTYPE ps_map_release(void *This) {
    return ps_release(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This));
}
static HRESULT STDMETHODCALLTYPE ps_map_getiids(void *This, ULONG *c, IID **i) {
    return ps_getiids(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This), c, i);
}
static HRESULT STDMETHODCALLTYPE ps_map_rcn(void *This, HSTRING *n) {
    return ps_rcn(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This), n);
}
static HRESULT STDMETHODCALLTYPE ps_map_tl(void *This, TrustLevel *tl) {
    return ps_tl(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This), tl);
}
static HRESULT STDMETHODCALLTYPE ps_map_lookup(void *This, HSTRING keyH, void **val) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    if (!val) return E_POINTER;
    *val = NULL;
    const wchar_t *k = winrt_sys_hstring_buf(keyH, NULL);
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->count; ++i) {
        if (wcscmp(obj->entries[i].key, k) == 0) {
            *val = obj->entries[i].value;
            if (obj->entries[i].value) obj->entries[i].value->lpVtbl->AddRef(obj->entries[i].value);
            LeaveCriticalSection(&obj->lock);
            return S_OK;
        }
    }
    LeaveCriticalSection(&obj->lock);
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE ps_map_size(void *This, uint32_t *sz) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    if (!sz) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    *sz = obj->count;
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_map_haskey(void *This, HSTRING keyH, boolean *found) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    if (!found) return E_POINTER;
    *found = 0;
    const wchar_t *k = winrt_sys_hstring_buf(keyH, NULL);
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->count; ++i) {
        if (wcscmp(obj->entries[i].key, k) == 0) {
            *found = 1;
            break;
        }
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_map_getview(void *This, void **view) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    if (!view) return E_POINTER;
    *view = NULL;

    WinrtMapSnapshot *snap = (WinrtMapSnapshot *)winrt_sys_alloc(sizeof(*snap));
    if (!snap) return E_OUTOFMEMORY;
    snap->lpVtbl = &g_map_snap_vtbl;
    snap->lpVtblIter = &g_map_snap_iter_vtbl;
    snap->ref_count = 1;
    snap->view_iid = WINRT_IID_IMapView_HSTRING_IInspectable;
    snap->iter_iid = WINRT_IID_IIterable_KVP_HSTRING_IInspectable;
    snap->kvp_iid = WINRT_IID_IKeyValuePair_HSTRING_IInspectable;

    EnterCriticalSection(&obj->lock);
    snap->count = obj->count;
    if (obj->count > 0) {
        snap->entries = (SettingsEntry *)winrt_sys_alloc((size_t)obj->count * sizeof(SettingsEntry));
        if (!snap->entries) {
            LeaveCriticalSection(&obj->lock);
            winrt_sys_free(snap);
            return E_OUTOFMEMORY;
        }
        for (uint32_t i = 0; i < obj->count; ++i) {
            size_t kl = wcslen(obj->entries[i].key);
            snap->entries[i].key = (wchar_t *)winrt_sys_alloc((kl + 1u) * sizeof(wchar_t));
            if (snap->entries[i].key) memcpy(snap->entries[i].key, obj->entries[i].key, (kl + 1u) * sizeof(wchar_t));
            snap->entries[i].value = obj->entries[i].value;
            if (snap->entries[i].value) snap->entries[i].value->lpVtbl->AddRef(snap->entries[i].value);
        }
    }
    LeaveCriticalSection(&obj->lock);
    *view = &snap->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_map_insert(void *This, HSTRING keyH, void *value, boolean *replaced) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    const wchar_t *k = winrt_sys_hstring_buf(keyH, NULL);
    boolean had = 0;
    ps_map_haskey(This, keyH, &had);
    settings_insert_raw(obj, k, (IUnknown *)value);
    if (replaced) *replaced = had;
    if (obj->owner_container) settings_save_root(find_root_container(obj->owner_container));
    ps_notify_changed(obj, had ? 3u /* ItemChanged */ : 1u /* ItemInserted */, k);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_map_remove(void *This, HSTRING keyH) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    const wchar_t *k = winrt_sys_hstring_buf(keyH, NULL);
    EnterCriticalSection(&obj->lock);
    int found = 0;
    for (uint32_t i = 0; i < obj->count; ++i) {
        if (wcscmp(obj->entries[i].key, k) == 0) {
            winrt_sys_free(obj->entries[i].key);
            if (obj->entries[i].value) obj->entries[i].value->lpVtbl->Release(obj->entries[i].value);
            for (uint32_t j = i + 1u; j < obj->count; ++j) obj->entries[j - 1u] = obj->entries[j];
            obj->count--;
            found = 1;
            break;
        }
    }
    LeaveCriticalSection(&obj->lock);
    if (!found) return E_BOUNDS;
    if (obj->owner_container) settings_save_root(find_root_container(obj->owner_container));
    ps_notify_changed(obj, 2u /* ItemRemoved */, k);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ps_map_clear(void *This) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblMap, This);
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->count; ++i) {
        winrt_sys_free(obj->entries[i].key);
        if (obj->entries[i].value) obj->entries[i].value->lpVtbl->Release(obj->entries[i].value);
    }
    obj->count = 0;
    LeaveCriticalSection(&obj->lock);
    if (obj->owner_container) settings_save_root(find_root_container(obj->owner_container));
    ps_notify_changed(obj, 0u /* Reset */, L"");
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ps_iter_qi(void *This, REFIID r, void **p) {
    return ps_qi(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This), r, p);
}
static ULONG STDMETHODCALLTYPE ps_iter_addref(void *This) {
    return ps_addref(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This));
}
static ULONG STDMETHODCALLTYPE ps_iter_release(void *This) {
    return ps_release(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE ps_iter_getiids(void *This, ULONG *c, IID **i) {
    return ps_getiids(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This), c, i);
}
static HRESULT STDMETHODCALLTYPE ps_iter_rcn(void *This, HSTRING *n) {
    return ps_rcn(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This), n);
}
static HRESULT STDMETHODCALLTYPE ps_iter_tl(void *This, TrustLevel *tl) {
    return ps_tl(WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This), tl);
}
static HRESULT STDMETHODCALLTYPE ps_iter_first(void *This, void **first) {
    WinrtPropertySetObj *obj = WINRT_SYS_FROM(WinrtPropertySetObj, lpVtblIter, This);
    void *view = NULL;
    HRESULT hr = ps_map_getview(&obj->lpVtblMap, &view);
    if (FAILED(hr)) return hr;
    WinrtMapSnapshot *snap = (WinrtMapSnapshot *)view;
    hr = map_snap_first(&snap->lpVtblIter, first);
    map_snap_release(snap);
    return hr;
}

static const WinrtPropertySetVtbl g_ps_vtbl = {
    ps_qi, ps_addref, ps_release, ps_getiids, ps_rcn, ps_tl
};
static const WinrtObservableMapVtbl g_ps_obs_vtbl = {
    ps_obs_qi, ps_obs_addref, ps_obs_release, ps_obs_getiids, ps_obs_rcn, ps_obs_tl,
    ps_obs_add_mc, ps_obs_rem_mc
};
static const WinrtMapHstringPtrVtbl g_ps_map_vtbl = {
    ps_map_qi, ps_map_addref, ps_map_release, ps_map_getiids, ps_map_rcn, ps_map_tl,
    ps_map_lookup, ps_map_size, ps_map_haskey, ps_map_getview, ps_map_insert, ps_map_remove, ps_map_clear
};
static const WinrtIterableVtbl g_ps_iter_vtbl = {
    ps_iter_qi, ps_iter_addref, ps_iter_release, ps_iter_getiids, ps_iter_rcn, ps_iter_tl, ps_iter_first
};

/* ApplicationDataContainer methods */
static HRESULT STDMETHODCALLTYPE adc_qi(void *This, REFIID riid, void **ppv) {
    WinrtAppDataContainerObj *obj = (WinrtAppDataContainerObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IApplicationDataContainer)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE adc_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtAppDataContainerObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE adc_release(void *This) {
    WinrtAppDataContainerObj *obj = (WinrtAppDataContainerObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->values) {
            obj->values->owner_container = NULL;
            ps_release(&obj->values->lpVtbl);
        }
        for (uint32_t i = 0; i < obj->sub_count; ++i) {
            if (obj->subs[i]) adc_release(obj->subs[i]);
        }
        winrt_sys_free(obj->subs);
        DeleteCriticalSection(&obj->lock);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE adc_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IApplicationDataContainer, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE adc_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.ApplicationDataContainer", n);
}
static HRESULT STDMETHODCALLTYPE adc_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE adc_get_name(void *This, HSTRING *val) {
    return winrt_sys_make_hstring(((WinrtAppDataContainerObj *)This)->name, val);
}
static HRESULT STDMETHODCALLTYPE adc_get_locality(void *This, uint32_t *val) {
    if (!val) return E_POINTER;
    *val = ((WinrtAppDataContainerObj *)This)->locality;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE adc_get_values(void *This, void **val) {
    WinrtAppDataContainerObj *obj = (WinrtAppDataContainerObj *)This;
    if (!val) return E_POINTER;
    *val = NULL;
    if (!obj->values) return E_UNEXPECTED;
    ps_addref(&obj->values->lpVtbl);
    *val = &obj->values->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE adc_get_containers(void *This, void **val) {
    WinrtAppDataContainerObj *obj = (WinrtAppDataContainerObj *)This;
    if (!val) return E_POINTER;
    *val = NULL;

    WinrtMapSnapshot *snap = (WinrtMapSnapshot *)winrt_sys_alloc(sizeof(*snap));
    if (!snap) return E_OUTOFMEMORY;
    snap->lpVtbl = &g_map_snap_vtbl;
    snap->lpVtblIter = &g_map_snap_iter_vtbl;
    snap->ref_count = 1;
    snap->view_iid = WINRT_IID_IMapView_HSTRING_AppDataContainer;
    snap->iter_iid = WINRT_IID_IIterable_KVP_HSTRING_AppDataContainer;
    snap->kvp_iid = WINRT_IID_IKeyValuePair_HSTRING_AppDataContainer;

    EnterCriticalSection(&obj->lock);
    snap->count = obj->sub_count;
    if (obj->sub_count > 0) {
        snap->entries = (SettingsEntry *)winrt_sys_alloc((size_t)obj->sub_count * sizeof(SettingsEntry));
        if (!snap->entries) {
            LeaveCriticalSection(&obj->lock);
            winrt_sys_free(snap);
            return E_OUTOFMEMORY;
        }
        for (uint32_t i = 0; i < obj->sub_count; ++i) {
            size_t kl = wcslen(obj->subs[i]->name);
            snap->entries[i].key = (wchar_t *)winrt_sys_alloc((kl + 1u) * sizeof(wchar_t));
            if (snap->entries[i].key) memcpy(snap->entries[i].key, obj->subs[i]->name, (kl + 1u) * sizeof(wchar_t));
            snap->entries[i].value = (IUnknown *)obj->subs[i];
            adc_addref(obj->subs[i]);
        }
    }
    LeaveCriticalSection(&obj->lock);
    *val = &snap->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE adc_create_container(
    void *This,
    HSTRING nameH,
    uint32_t disposition,
    void **container) {
    WinrtAppDataContainerObj *obj = (WinrtAppDataContainerObj *)This;
    if (!container) return E_POINTER;
    *container = NULL;
    const wchar_t *name = winrt_sys_hstring_buf(nameH, NULL);
    if (!name || name[0] == L'\0') return E_INVALIDARG;

    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->sub_count; ++i) {
        if (wcscmp(obj->subs[i]->name, name) == 0) {
            adc_addref(obj->subs[i]);
            *container = obj->subs[i];
            LeaveCriticalSection(&obj->lock);
            return S_OK;
        }
    }
    if (disposition == 1u /* Existing */) {
        LeaveCriticalSection(&obj->lock);
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    wchar_t child_sec[UWP_MAX_IDENTITY_CHARS];
    if (obj->is_root) {
        winrt_sys_copy_wstr(child_sec, UWP_MAX_IDENTITY_CHARS, name);
    } else {
        swprintf(child_sec, UWP_MAX_IDENTITY_CHARS, L"%ls\\%ls", obj->section, name);
        child_sec[UWP_MAX_IDENTITY_CHARS - 1u] = L'\0';
    }
    WinrtAppDataContainerObj *sub = NULL;
    HRESULT hr = create_container_obj(name, child_sec, obj->locality, obj->ini_path, 0, &sub);
    if (SUCCEEDED(hr) && sub) {
        WinrtAppDataContainerObj **ns = (WinrtAppDataContainerObj **)winrt_sys_alloc(
            (size_t)(obj->sub_count + 1u) * sizeof(WinrtAppDataContainerObj *));
        if (ns) {
            if (obj->subs && obj->sub_count > 0) {
                memcpy(ns, obj->subs, (size_t)obj->sub_count * sizeof(WinrtAppDataContainerObj *));
            }
            winrt_sys_free(obj->subs);
            obj->subs = ns;
            obj->subs[obj->sub_count++] = sub;
            adc_addref(sub);
            *container = sub;
        } else {
            adc_release(sub);
            hr = E_OUTOFMEMORY;
        }
    }
    LeaveCriticalSection(&obj->lock);
    if (SUCCEEDED(hr)) settings_save_root(obj);
    return hr;
}
static HRESULT STDMETHODCALLTYPE adc_delete_container(void *This, HSTRING nameH) {
    WinrtAppDataContainerObj *obj = (WinrtAppDataContainerObj *)This;
    const wchar_t *name = winrt_sys_hstring_buf(nameH, NULL);
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->sub_count; ++i) {
        if (wcscmp(obj->subs[i]->name, name) == 0) {
            adc_release(obj->subs[i]);
            for (uint32_t j = i + 1u; j < obj->sub_count; ++j) obj->subs[j - 1u] = obj->subs[j];
            obj->sub_count--;
            LeaveCriticalSection(&obj->lock);
            settings_save_root(obj);
            return S_OK;
        }
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static const WinrtApplicationDataContainerVtbl g_adc_vtbl = {
    adc_qi, adc_addref, adc_release, adc_getiids, adc_rcn, adc_tl,
    adc_get_name, adc_get_locality, adc_get_values, adc_get_containers,
    adc_create_container, adc_delete_container
};

static HRESULT create_container_obj(
    const wchar_t *name,
    const wchar_t *section,
    uint32_t locality,
    const wchar_t *ini_path,
    int is_root,
    WinrtAppDataContainerObj **out_c) {
    WinrtAppDataContainerObj *c = (WinrtAppDataContainerObj *)winrt_sys_alloc(sizeof(*c));
    if (!c) return E_OUTOFMEMORY;
    c->lpVtbl = &g_adc_vtbl;
    c->ref_count = 1;
    InitializeCriticalSection(&c->lock);
    winrt_sys_copy_wstr(c->name, UWP_MAX_IDENTITY_CHARS, name);
    winrt_sys_copy_wstr(c->section, UWP_MAX_IDENTITY_CHARS, section);
    c->locality = locality;
    winrt_sys_copy_wstr(c->ini_path, UWP_MAX_PATH_CHARS, ini_path);
    c->is_root = is_root;

    WinrtPropertySetObj *ps = (WinrtPropertySetObj *)winrt_sys_alloc(sizeof(*ps));
    if (!ps) {
        DeleteCriticalSection(&c->lock);
        winrt_sys_free(c);
        return E_OUTOFMEMORY;
    }
    ps->lpVtbl = &g_ps_vtbl;
    ps->lpVtblObs = &g_ps_obs_vtbl;
    ps->lpVtblMap = &g_ps_map_vtbl;
    ps->lpVtblIter = &g_ps_iter_vtbl;
    ps->ref_count = 1;
    ps->owner_container = c;
    InitializeCriticalSection(&ps->lock);
    c->values = ps;
    *out_c = c;
    return S_OK;
}

/* ============================================================================
 * Windows.Storage.ApplicationData
 * ============================================================================ */

typedef struct DataChangedListener {
    EventRegistrationToken token;
    IUnknown *handler;
} DataChangedListener;

typedef struct WinrtApplicationDataObj {
    const WinrtApplicationDataVtbl  *lpVtbl;
    const WinrtApplicationData2Vtbl *lpVtbl2;
    const WinrtApplicationData3Vtbl *lpVtbl3;
    volatile LONG ref_count;
    uint32_t version;
    CRITICAL_SECTION lock;
    uint32_t dc_count;
    DataChangedListener dc_listeners[16];
    int64_t next_token;
} WinrtApplicationDataObj;

static WinrtApplicationDataObj g_appdata_singleton;
static LONG g_appdata_init = 0;

static void ensure_appdata_init(void) {
    if (InterlockedCompareExchange(&g_appdata_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_appdata_singleton.lock);
        g_appdata_singleton.ref_count = 1;
        g_appdata_singleton.version = 0;
    }
}

static WinrtAppDataContainerObj *get_or_create_settings_root(uint32_t locality) {
    WinrtAppDataContainerObj **slot = (locality == 0) ? &g_local_settings_root : &g_roaming_settings_root;
    EnterCriticalSection(&g_appdata_singleton.lock);
    if (*slot == NULL) {
        wchar_t dir[UWP_MAX_PATH_CHARS];
        winrt_sys_get_state_dir(locality == 0 ? WINRT_SYS_STATE_LOCAL : WINRT_SYS_STATE_ROAMING,
                                dir, UWP_MAX_PATH_CHARS);
        wchar_t ini[UWP_MAX_PATH_CHARS];
        join_child_path(dir,
                        locality == 0 ? L"uwp_local_settings.ini" : L"uwp_roaming_settings.ini",
                        ini, UWP_MAX_PATH_CHARS);
        WinrtAppDataContainerObj *root = NULL;
        if (SUCCEEDED(create_container_obj(L"", L"root", locality, ini, 1, &root))) {
            *slot = root;
            settings_load_root(root);
        }
    }
    WinrtAppDataContainerObj *res = *slot;
    if (res) adc_addref(res);
    LeaveCriticalSection(&g_appdata_singleton.lock);
    return res;
}

static HRESULT STDMETHODCALLTYPE ad_qi(void *This, REFIID riid, void **ppv) {
    WinrtApplicationDataObj *obj = (WinrtApplicationDataObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IApplicationData)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IApplicationData2)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl2;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IApplicationData3)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl3;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ad_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtApplicationDataObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE ad_release(void *This) {
    LONG rc = InterlockedDecrement(&((WinrtApplicationDataObj *)This)->ref_count);
    return rc > 0 ? (ULONG)rc : 1u;
}
static HRESULT STDMETHODCALLTYPE ad_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0xc3da6fb7u, 0xb744u, 0x4b45u, { 0xb0u, 0xb8u, 0x22u, 0x3au, 0x09u, 0x38u, 0xd0u, 0xdcu } },
        { 0x9e65cd69u, 0x0ba3u, 0x4e32u, { 0xbeu, 0x29u, 0xb0u, 0x2du, 0xe6u, 0x60u, 0x76u, 0x38u } },
        { 0xdc222cf4u, 0x2772u, 0x4c1du, { 0xaau, 0x2cu, 0xc9u, 0xf7u, 0x43u, 0xadu, 0xe8u, 0xd1u } }
    };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE ad_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Storage.ApplicationData", n);
}
static HRESULT STDMETHODCALLTYPE ad_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_get_version(void *This, uint32_t *val) {
    WinrtApplicationDataObj *obj = (WinrtApplicationDataObj *)This;
    if (!val) return E_POINTER;
    *val = obj->version;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_set_version(void *This, uint32_t desired, void *handler, void **op) {
    WinrtApplicationDataObj *obj = (WinrtApplicationDataObj *)This;
    (void)handler;
    if (!op) return E_POINTER;
    obj->version = desired;
    return winrt_sys_create_completed_async_action(S_OK, op);
}
static HRESULT STDMETHODCALLTYPE ad_clear_async(void *This, uint32_t locality, void **op) {
    (void)This;
    if (!op) return E_POINTER;
    if (locality == 0u || locality == 1u) {
        WinrtAppDataContainerObj *root = get_or_create_settings_root(locality);
        if (root) {
            if (root->values) ps_map_clear(&root->values->lpVtblMap);
            adc_release(root);
        }
    }
    return winrt_sys_create_completed_async_action(S_OK, op);
}
static HRESULT STDMETHODCALLTYPE ad_clear_all(void *This, void **op) {
    if (!op) return E_POINTER;
    void *dummy = NULL;
    ad_clear_async(This, 0u, &dummy);
    if (dummy) ((IUnknown *)dummy)->lpVtbl->Release((IUnknown *)dummy);
    dummy = NULL;
    ad_clear_async(This, 1u, &dummy);
    if (dummy) ((IUnknown *)dummy)->lpVtbl->Release((IUnknown *)dummy);
    return winrt_sys_create_completed_async_action(S_OK, op);
}
static HRESULT STDMETHODCALLTYPE ad_get_local_settings(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = get_or_create_settings_root(0u);
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT STDMETHODCALLTYPE ad_get_roaming_settings(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = get_or_create_settings_root(1u);
    return *val ? S_OK : E_OUTOFMEMORY;
}
static HRESULT ad_get_state_folder(winrt_sys_state_kind_t kind, void **val) {
    if (!val) return E_POINTER;
    *val = NULL;
    wchar_t dir[UWP_MAX_PATH_CHARS];
    winrt_sys_get_state_dir(kind, dir, UWP_MAX_PATH_CHARS);
    return winrt_sys_create_storage_folder(dir, val);
}
static HRESULT STDMETHODCALLTYPE ad_get_local_folder(void *This, void **val) {
    (void)This;
    return ad_get_state_folder(WINRT_SYS_STATE_LOCAL, val);
}
static HRESULT STDMETHODCALLTYPE ad_get_roaming_folder(void *This, void **val) {
    (void)This;
    return ad_get_state_folder(WINRT_SYS_STATE_ROAMING, val);
}
static HRESULT STDMETHODCALLTYPE ad_get_temp_folder(void *This, void **val) {
    (void)This;
    return ad_get_state_folder(WINRT_SYS_STATE_TEMP, val);
}
static HRESULT STDMETHODCALLTYPE ad_add_dc(void *This, void *handler, EventRegistrationToken *tok) {
    WinrtApplicationDataObj *obj = (WinrtApplicationDataObj *)This;
    if (!handler || !tok) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    if (obj->dc_count >= 16u) {
        LeaveCriticalSection(&obj->lock);
        return E_OUTOFMEMORY;
    }
    IUnknown *h = (IUnknown *)handler;
    h->lpVtbl->AddRef(h);
    tok->value = ++obj->next_token;
    obj->dc_listeners[obj->dc_count].token = *tok;
    obj->dc_listeners[obj->dc_count].handler = h;
    obj->dc_count++;
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_rem_dc(void *This, EventRegistrationToken tok) {
    WinrtApplicationDataObj *obj = (WinrtApplicationDataObj *)This;
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->dc_count; ++i) {
        if (obj->dc_listeners[i].token.value == tok.value) {
            if (obj->dc_listeners[i].handler) obj->dc_listeners[i].handler->lpVtbl->Release(obj->dc_listeners[i].handler);
            for (uint32_t j = i + 1u; j < obj->dc_count; ++j) obj->dc_listeners[j - 1u] = obj->dc_listeners[j];
            obj->dc_count--;
            break;
        }
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_signal_dc(void *This) {
    WinrtApplicationDataObj *obj = (WinrtApplicationDataObj *)This;
    IUnknown *handlers[16];
    uint32_t cnt = 0;
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < obj->dc_count && i < 16u; ++i) {
        if (obj->dc_listeners[i].handler) {
            obj->dc_listeners[i].handler->lpVtbl->AddRef(obj->dc_listeners[i].handler);
            handlers[cnt++] = obj->dc_listeners[i].handler;
        }
    }
    LeaveCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < cnt; ++i) {
        typedef struct { const WinrtEventHandlerPtrVtbl *lpVtbl; } HIf;
        ((HIf *)handlers[i])->lpVtbl->Invoke(handlers[i], (IInspectable *)&obj->lpVtbl, NULL);
        handlers[i]->lpVtbl->Release(handlers[i]);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_get_quota(void *This, uint64_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 102400ULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ad2_qi(void *This, REFIID r, void **p) {
    return ad_qi(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl2, This), r, p);
}
static ULONG STDMETHODCALLTYPE ad2_addref(void *This) {
    return ad_addref(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl2, This));
}
static ULONG STDMETHODCALLTYPE ad2_release(void *This) {
    return ad_release(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl2, This));
}
static HRESULT STDMETHODCALLTYPE ad2_getiids(void *This, ULONG *c, IID **i) {
    return ad_getiids(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl2, This), c, i);
}
static HRESULT STDMETHODCALLTYPE ad2_rcn(void *This, HSTRING *n) {
    return ad_rcn(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl2, This), n);
}
static HRESULT STDMETHODCALLTYPE ad2_tl(void *This, TrustLevel *tl) {
    return ad_tl(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl2, This), tl);
}
static HRESULT STDMETHODCALLTYPE ad2_get_local_cache(void *This, void **val) {
    (void)This;
    return ad_get_state_folder(WINRT_SYS_STATE_LOCAL_CACHE, val);
}

static HRESULT STDMETHODCALLTYPE ad3_qi(void *This, REFIID r, void **p) {
    return ad_qi(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl3, This), r, p);
}
static ULONG STDMETHODCALLTYPE ad3_addref(void *This) {
    return ad_addref(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl3, This));
}
static ULONG STDMETHODCALLTYPE ad3_release(void *This) {
    return ad_release(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl3, This));
}
static HRESULT STDMETHODCALLTYPE ad3_getiids(void *This, ULONG *c, IID **i) {
    return ad_getiids(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl3, This), c, i);
}
static HRESULT STDMETHODCALLTYPE ad3_rcn(void *This, HSTRING *n) {
    return ad_rcn(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl3, This), n);
}
static HRESULT STDMETHODCALLTYPE ad3_tl(void *This, TrustLevel *tl) {
    return ad_tl(WINRT_SYS_FROM(WinrtApplicationDataObj, lpVtbl3, This), tl);
}
static HRESULT STDMETHODCALLTYPE ad3_get_pub_cache(void *This, HSTRING folderH, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    const wchar_t *sub = winrt_sys_hstring_buf(folderH, NULL);
    wchar_t base[UWP_MAX_PATH_CHARS];
    winrt_sys_get_state_dir(WINRT_SYS_STATE_LOCAL_CACHE, base, UWP_MAX_PATH_CHARS);
    wchar_t target[UWP_MAX_PATH_CHARS];
    join_child_path(base, (sub && sub[0]) ? sub : L"Publisher", target, UWP_MAX_PATH_CHARS);
    winrt_sys_ensure_dir_recursive(target);
    return winrt_sys_create_storage_folder(target, val);
}
static HRESULT STDMETHODCALLTYPE ad3_clear_pub_cache(void *This, HSTRING folderH, void **op) {
    (void)This; (void)folderH;
    if (!op) return E_POINTER;
    return winrt_sys_create_completed_async_action(S_OK, op);
}
static HRESULT STDMETHODCALLTYPE ad3_get_shared_local(void *This, void **val) {
    (void)This;
    return ad_get_state_folder(WINRT_SYS_STATE_SHARED_LOCAL, val);
}

static const WinrtApplicationDataVtbl g_ad_vtbl = {
    ad_qi, ad_addref, ad_release, ad_getiids, ad_rcn, ad_tl,
    ad_get_version, ad_set_version, ad_clear_all, ad_clear_async,
    ad_get_local_settings, ad_get_roaming_settings,
    ad_get_local_folder, ad_get_roaming_folder, ad_get_temp_folder,
    ad_add_dc, ad_rem_dc, ad_signal_dc, ad_get_quota
};
static const WinrtApplicationData2Vtbl g_ad2_vtbl = {
    ad2_qi, ad2_addref, ad2_release, ad2_getiids, ad2_rcn, ad2_tl,
    ad2_get_local_cache
};
static const WinrtApplicationData3Vtbl g_ad3_vtbl = {
    ad3_qi, ad3_addref, ad3_release, ad3_getiids, ad3_rcn, ad3_tl,
    ad3_get_pub_cache, ad3_clear_pub_cache, ad3_get_shared_local
};

static WinrtApplicationDataObj g_appdata_singleton = {
    &g_ad_vtbl, &g_ad2_vtbl, &g_ad3_vtbl, 1, 0, { 0 }, 0, { { { 0 }, NULL } }, 0
};

/* ApplicationData Activation Factory */
typedef struct WinrtApplicationDataFactory {
    const WinrtActivationFactoryVtbl     *lpVtblAct;
    const WinrtApplicationDataStaticsVtbl *lpVtblStatics;
    const WinrtApplicationDataStatics2Vtbl *lpVtblStatics2;
} WinrtApplicationDataFactory;

static WinrtApplicationDataFactory g_ad_factory;

static HRESULT STDMETHODCALLTYPE ad_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_ad_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IApplicationDataStatics)) {
        *ppv = &g_ad_factory.lpVtblStatics;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IApplicationDataStatics2)) {
        *ppv = &g_ad_factory.lpVtblStatics2;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ad_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE ad_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE ad_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x5612147bu, 0xe843u, 0x45e3u, { 0x94u, 0xd8u, 0x06u, 0x16u, 0x9eu, 0x3cu, 0x8eu, 0x17u } },
        { 0xcd606211u, 0xcf49u, 0x40a4u, { 0xa4u, 0x7cu, 0xc7u, 0xf0u, 0xdbu, 0xbau, 0x81u, 0x07u } }
    };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE ad_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE ad_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("ApplicationData::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE ad_fac_get_current(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    ensure_appdata_init();
    InterlockedIncrement(&g_appdata_singleton.ref_count);
    *val = &g_appdata_singleton.lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ad_fac_get_for_user(void *This, void *user, void **op) {
    (void)This; (void)user;
    if (!op) return E_POINTER;
    ensure_appdata_init();
    return winrt_sys_create_completed_async_op_ptr(
        &WINRT_IID_IAsyncOperation_ApplicationData,
        L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.ApplicationData>",
        (IUnknown *)&g_appdata_singleton.lpVtbl, S_OK, op);
}

static const WinrtActivationFactoryVtbl g_ad_act_vtbl = {
    ad_fac_qi, ad_fac_addref, ad_fac_release, ad_fac_getiids, ad_fac_rcn, ad_fac_tl, ad_fac_act
};
static const WinrtApplicationDataStaticsVtbl g_ad_stat_vtbl = {
    ad_fac_qi, ad_fac_addref, ad_fac_release, ad_fac_getiids, ad_fac_rcn, ad_fac_tl, ad_fac_get_current
};
static const WinrtApplicationDataStatics2Vtbl g_ad_stat2_vtbl = {
    ad_fac_qi, ad_fac_addref, ad_fac_release, ad_fac_getiids, ad_fac_rcn, ad_fac_tl, ad_fac_get_for_user
};
static WinrtApplicationDataFactory g_ad_factory = {
    &g_ad_act_vtbl, &g_ad_stat_vtbl, &g_ad_stat2_vtbl
};

HRESULT winrt_sys_get_application_data_factory(REFIID iid, void **out_factory) {
    return ad_fac_qi(&g_ad_factory.lpVtblAct, iid, out_factory);
}
