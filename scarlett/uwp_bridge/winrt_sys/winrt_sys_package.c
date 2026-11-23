/*
 * Proton-Scarlett — UWP Bridge System WinRT: Windows.ApplicationModel.Package
 *
 * Implements:
 *   - IPackageStatics::get_Current
 *   - IPackage (Id, InstalledLocation, IsFramework, Dependencies)
 *   - IPackage2 (DisplayName, PublisherDisplayName, Description, Logo -> E_NOTIMPL,
 *                IsResourcePackage, IsBundle, IsDevelopmentMode)
 *   - IPackage3 (Status, InstalledDate, GetAppListEntriesAsync -> E_NOTIMPL)
 *   - IPackageId & IPackageIdWithMetadata
 *   - IPackageStatus & empty IVectorView<Package*>
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls).
 */

#include "winrt_sys_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Vtable Definitions (Windows.ApplicationModel)
 * ============================================================================ */

typedef struct WinrtPackageStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **value);
} WinrtPackageStaticsVtbl;

typedef struct WinrtPackageVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_InstalledLocation)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_IsFramework)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Dependencies)(void *This, void **value);
} WinrtPackageVtbl;

typedef struct WinrtPackage2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_DisplayName)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_PublisherDisplayName)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Description)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Logo)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_IsResourcePackage)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsBundle)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsDevelopmentMode)(void *This, boolean *value);
} WinrtPackage2Vtbl;

typedef struct WinrtPackage3Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Status)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_InstalledDate)(void *This, int64_t *value);
    HRESULT (STDMETHODCALLTYPE *GetAppListEntriesAsync)(void *This, void **operation);
} WinrtPackage3Vtbl;

typedef struct WinrtPackageIdVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Version)(void *This, WinrtPackageVersion *value);
    HRESULT (STDMETHODCALLTYPE *get_Architecture)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *get_ResourceId)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Publisher)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_PublisherId)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_FullName)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_FamilyName)(void *This, HSTRING *value);
} WinrtPackageIdVtbl;

typedef struct WinrtPackageIdWithMetadataVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_ProductId)(void *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Author)(void *This, HSTRING *value);
} WinrtPackageIdWithMetadataVtbl;

typedef struct WinrtPackageStatusVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *VerifyIsOK)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_NotAvailable)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_PackageOffline)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_DataOffline)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Disabled)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_NeedsRemediation)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_LicenseIssue)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Modified)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Tampered)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_DependencyIssue)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Servicing)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_DeploymentInProgress)(void *This, boolean *value);
} WinrtPackageStatusVtbl;

typedef struct WinrtPackageVectorViewVtbl {
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
} WinrtPackageVectorViewVtbl;

typedef struct WinrtPackageIterableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *First)(void *This, void **first);
} WinrtPackageIterableVtbl;

typedef struct WinrtPackageIteratorVtbl {
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
} WinrtPackageIteratorVtbl;

/* ============================================================================
 * Identity Snapshot Helper (uwp_core_get_package_identity + env fallbacks)
 * ============================================================================ */

static void parse_version_str(const wchar_t *ver, WinrtPackageVersion *out) {
    unsigned maj = 1, min = 0, bld = 0, rev = 0;
    if (ver && ver[0] != L'\0') {
        if (swscanf(ver, L"%u.%u.%u.%u", &maj, &min, &bld, &rev) < 1) {
            maj = 1; min = 0; bld = 0; rev = 0;
        }
    }
    out->Major = (uint16_t)maj;
    out->Minor = (uint16_t)min;
    out->Build = (uint16_t)bld;
    out->Revision = (uint16_t)rev;
}

static void resolve_package_identity(uwp_package_identity_t *out) {
    memset(out, 0, sizeof(*out));
    const uwp_package_identity_t *core = uwp_core_get_package_identity();
    if (core != NULL && (core->valid != 0 || core->install_path[0] != L'\0')) {
        *out = *core;
    }

    wchar_t buf[UWP_MAX_IDENTITY_CHARS];
    if (out->name[0] == L'\0') {
        DWORD n = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_NAME", buf, UWP_MAX_IDENTITY_CHARS);
        if (n > 0 && n < UWP_MAX_IDENTITY_CHARS) {
            winrt_sys_copy_wstr(out->name, UWP_MAX_IDENTITY_CHARS, buf);
        } else {
            winrt_sys_copy_wstr(out->name, UWP_MAX_IDENTITY_CHARS, L"ProtonScarlett.UWPApp");
        }
    }
    if (out->publisher[0] == L'\0') {
        DWORD n = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_PUBLISHER", buf, UWP_MAX_IDENTITY_CHARS);
        if (n > 0 && n < UWP_MAX_IDENTITY_CHARS) {
            winrt_sys_copy_wstr(out->publisher, UWP_MAX_IDENTITY_CHARS, buf);
        } else {
            winrt_sys_copy_wstr(out->publisher, UWP_MAX_IDENTITY_CHARS, L"CN=ProtonScarlett");
        }
    }
    if (out->ver_major == 0 && out->ver_minor == 0 && out->ver_build == 0 && out->ver_revision == 0) {
        DWORD n = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_VERSION", buf, UWP_MAX_IDENTITY_CHARS);
        WinrtPackageVersion pv = { 1, 0, 0, 0 };
        if (n > 0 && n < UWP_MAX_IDENTITY_CHARS) {
            parse_version_str(buf, &pv);
        }
        out->ver_major = pv.Major;
        out->ver_minor = pv.Minor;
        out->ver_build = pv.Build;
        out->ver_revision = pv.Revision;
    }
    if (out->architecture == 0) {
        out->architecture = 9u; /* ProcessorArchitecture_X64 */
    }
    if (out->full_name[0] == L'\0') {
        DWORD n = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_FULL_NAME", buf, UWP_MAX_IDENTITY_CHARS);
        if (n > 0 && n < UWP_MAX_IDENTITY_CHARS) {
            winrt_sys_copy_wstr(out->full_name, UWP_MAX_IDENTITY_CHARS, buf);
        }
    }
    if (out->family_name[0] == L'\0') {
        DWORD n = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_FAMILY_NAME", buf, UWP_MAX_IDENTITY_CHARS);
        if (n > 0 && n < UWP_MAX_IDENTITY_CHARS) {
            winrt_sys_copy_wstr(out->family_name, UWP_MAX_IDENTITY_CHARS, buf);
        }
    }
    if (out->publisher_id[0] == L'\0') {
        const wchar_t *sep = wcsstr(out->full_name, L"__");
        if (sep != NULL && sep[2] != L'\0') {
            winrt_sys_copy_wstr(out->publisher_id, 64, sep + 2);
        } else {
            const wchar_t *us = wcsrchr(out->family_name, L'_');
            if (us != NULL && us[1] != L'\0') {
                winrt_sys_copy_wstr(out->publisher_id, 64, us + 1);
            } else {
                winrt_sys_copy_wstr(out->publisher_id, 64, L"0000000000000");
            }
        }
    }
    if (out->family_name[0] == L'\0') {
        swprintf(out->family_name, UWP_MAX_IDENTITY_CHARS, L"%ls_%ls", out->name, out->publisher_id);
        out->family_name[UWP_MAX_IDENTITY_CHARS - 1u] = L'\0';
    }
    if (out->full_name[0] == L'\0') {
        swprintf(out->full_name, UWP_MAX_IDENTITY_CHARS,
                 L"%ls_%u.%u.%u.%u_x64__%ls",
                 out->name,
                 (unsigned)out->ver_major,
                 (unsigned)out->ver_minor,
                 (unsigned)out->ver_build,
                 (unsigned)out->ver_revision,
                 out->publisher_id);
        out->full_name[UWP_MAX_IDENTITY_CHARS - 1u] = L'\0';
    }
    if (out->display_name[0] == L'\0') {
        winrt_sys_copy_wstr(out->display_name, UWP_MAX_IDENTITY_CHARS, out->name);
    }
    if (out->install_path[0] == L'\0') {
        winrt_sys_get_install_dir(out->install_path, UWP_MAX_PATH_CHARS);
    } else {
        wchar_t norm[UWP_MAX_PATH_CHARS];
        winrt_sys_normalize_path(out->install_path, norm, UWP_MAX_PATH_CHARS);
        winrt_sys_copy_wstr(out->install_path, UWP_MAX_PATH_CHARS, norm);
    }
}

/* ============================================================================
 * Empty IVectorView<Package*> for IPackage::get_Dependencies
 * ============================================================================ */

typedef struct WinrtEmptyPkgIter {
    const WinrtPackageIteratorVtbl *lpVtbl;
    volatile LONG ref_count;
} WinrtEmptyPkgIter;

static HRESULT STDMETHODCALLTYPE epkg_it_qi(void *This, REFIID riid, void **ppv) {
    WinrtEmptyPkgIter *obj = (WinrtEmptyPkgIter *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IIterator_Package)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE epkg_it_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtEmptyPkgIter *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE epkg_it_release(void *This) {
    WinrtEmptyPkgIter *obj = (WinrtEmptyPkgIter *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE epkg_it_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IIterator_Package, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE epkg_it_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1<Windows.ApplicationModel.Package>", n);
}
static HRESULT STDMETHODCALLTYPE epkg_it_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_it_cur(void *This, void **v) {
    (void)This;
    if (v) *v = NULL;
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE epkg_it_has(void *This, boolean *v) {
    (void)This;
    if (!v) return E_POINTER;
    *v = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_it_next(void *This, boolean *v) {
    (void)This;
    if (!v) return E_POINTER;
    *v = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_it_many(void *This, uint32_t sz, void **items, uint32_t *v) {
    (void)This; (void)sz; (void)items;
    if (!v) return E_POINTER;
    *v = 0;
    return S_OK;
}
static const WinrtPackageIteratorVtbl g_epkg_it_vtbl = {
    epkg_it_qi, epkg_it_addref, epkg_it_release, epkg_it_getiids, epkg_it_rcn, epkg_it_tl,
    epkg_it_cur, epkg_it_has, epkg_it_next, epkg_it_many
};

typedef struct WinrtEmptyPkgView {
    const WinrtPackageVectorViewVtbl *lpVtbl;
    const WinrtPackageIterableVtbl   *lpVtblIter;
    volatile LONG ref_count;
} WinrtEmptyPkgView;

static HRESULT STDMETHODCALLTYPE epkg_vw_qi(void *This, REFIID riid, void **ppv) {
    WinrtEmptyPkgView *obj = (WinrtEmptyPkgView *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IVectorView_Package)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_Package)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE epkg_vw_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtEmptyPkgView *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE epkg_vw_release(void *This) {
    WinrtEmptyPkgView *obj = (WinrtEmptyPkgView *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE epkg_vw_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x0263c4d4u, 0x195cu, 0x5dc5u, { 0xa7u, 0xcau, 0x68u, 0x06u, 0xceu, 0xcau, 0x42u, 0x0bu } },
        { 0x69ad6aa7u, 0x0c49u, 0x5f27u, { 0xa5u, 0xebu, 0xefu, 0x4du, 0x59u, 0x46u, 0x7bu, 0x6du } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE epkg_vw_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Package>", n);
}
static HRESULT STDMETHODCALLTYPE epkg_vw_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_vw_getat(void *This, uint32_t idx, void **v) {
    (void)This; (void)idx;
    if (v) *v = NULL;
    return E_BOUNDS;
}
static HRESULT STDMETHODCALLTYPE epkg_vw_size(void *This, uint32_t *v) {
    (void)This;
    if (!v) return E_POINTER;
    *v = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_vw_indexof(void *This, void *el, uint32_t *idx, boolean *found) {
    (void)This; (void)el;
    if (!idx || !found) return E_POINTER;
    *idx = 0; *found = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_vw_getmany(void *This, uint32_t s, uint32_t c, void **it, uint32_t *v) {
    (void)This; (void)s; (void)c; (void)it;
    if (!v) return E_POINTER;
    *v = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE epkg_vw_iter_qi(void *This, REFIID r, void **p) {
    return epkg_vw_qi(WINRT_SYS_FROM(WinrtEmptyPkgView, lpVtblIter, This), r, p);
}
static ULONG STDMETHODCALLTYPE epkg_vw_iter_addref(void *This) {
    return epkg_vw_addref(WINRT_SYS_FROM(WinrtEmptyPkgView, lpVtblIter, This));
}
static ULONG STDMETHODCALLTYPE epkg_vw_iter_release(void *This) {
    return epkg_vw_release(WINRT_SYS_FROM(WinrtEmptyPkgView, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE epkg_vw_iter_getiids(void *This, ULONG *c, IID **i) {
    return epkg_vw_getiids(WINRT_SYS_FROM(WinrtEmptyPkgView, lpVtblIter, This), c, i);
}
static HRESULT STDMETHODCALLTYPE epkg_vw_iter_rcn(void *This, HSTRING *n) {
    return epkg_vw_rcn(WINRT_SYS_FROM(WinrtEmptyPkgView, lpVtblIter, This), n);
}
static HRESULT STDMETHODCALLTYPE epkg_vw_iter_tl(void *This, TrustLevel *tl) {
    return epkg_vw_tl(WINRT_SYS_FROM(WinrtEmptyPkgView, lpVtblIter, This), tl);
}
static HRESULT STDMETHODCALLTYPE epkg_vw_first(void *This, void **first) {
    (void)This;
    if (!first) return E_POINTER;
    WinrtEmptyPkgIter *it = (WinrtEmptyPkgIter *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = &g_epkg_it_vtbl;
    it->ref_count = 1;
    *first = it;
    return S_OK;
}
static const WinrtPackageVectorViewVtbl g_epkg_vw_vtbl = {
    epkg_vw_qi, epkg_vw_addref, epkg_vw_release, epkg_vw_getiids, epkg_vw_rcn, epkg_vw_tl,
    epkg_vw_getat, epkg_vw_size, epkg_vw_indexof, epkg_vw_getmany
};
static const WinrtPackageIterableVtbl g_epkg_vw_iter_vtbl = {
    epkg_vw_iter_qi, epkg_vw_iter_addref, epkg_vw_iter_release,
    epkg_vw_iter_getiids, epkg_vw_iter_rcn, epkg_vw_iter_tl, epkg_vw_first
};

/* ============================================================================
 * PackageStatus
 * ============================================================================ */

typedef struct WinrtPackageStatusObj {
    const WinrtPackageStatusVtbl *lpVtbl;
    volatile LONG ref_count;
} WinrtPackageStatusObj;

static HRESULT STDMETHODCALLTYPE pstat_qi(void *This, REFIID riid, void **ppv) {
    WinrtPackageStatusObj *obj = (WinrtPackageStatusObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPackageStatus)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pstat_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPackageStatusObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pstat_release(void *This) {
    WinrtPackageStatusObj *obj = (WinrtPackageStatusObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pstat_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IPackageStatus, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE pstat_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.ApplicationModel.PackageStatus", n);
}
static HRESULT STDMETHODCALLTYPE pstat_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pstat_ok(void *This, boolean *v) {
    (void)This;
    if (!v) return E_POINTER;
    *v = 1;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pstat_false(void *This, boolean *v) {
    (void)This;
    if (!v) return E_POINTER;
    *v = 0;
    return S_OK;
}
static const WinrtPackageStatusVtbl g_pstat_vtbl = {
    pstat_qi, pstat_addref, pstat_release, pstat_getiids, pstat_rcn, pstat_tl,
    pstat_ok, pstat_false, pstat_false, pstat_false, pstat_false, pstat_false,
    pstat_false, pstat_false, pstat_false, pstat_false, pstat_false, pstat_false
};

/* ============================================================================
 * PackageId
 * ============================================================================ */

typedef struct WinrtPackageIdObj {
    const WinrtPackageIdVtbl             *lpVtbl;
    const WinrtPackageIdWithMetadataVtbl *lpVtblMeta;
    volatile LONG ref_count;
    uwp_package_identity_t id;
} WinrtPackageIdObj;

static HRESULT STDMETHODCALLTYPE pid_qi(void *This, REFIID riid, void **ppv) {
    WinrtPackageIdObj *obj = (WinrtPackageIdObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPackageId)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPackageIdWithMetadata)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblMeta;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pid_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPackageIdObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pid_release(void *This) {
    WinrtPackageIdObj *obj = (WinrtPackageIdObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pid_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x1adb665eu, 0x37c7u, 0x4790u, { 0x99u, 0x80u, 0xddu, 0x7au, 0xe7u, 0x4eu, 0x8bu, 0xb2u } },
        { 0x40577a7cu, 0x0c9eu, 0x443du, { 0x90u, 0x74u, 0x85u, 0x5fu, 0x5cu, 0xe0u, 0xa0u, 0x8du } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE pid_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.ApplicationModel.PackageId", n);
}
static HRESULT STDMETHODCALLTYPE pid_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pid_get_name(void *This, HSTRING *v) {
    return winrt_sys_make_hstring(((WinrtPackageIdObj *)This)->id.name, v);
}
static HRESULT STDMETHODCALLTYPE pid_get_version(void *This, WinrtPackageVersion *v) {
    WinrtPackageIdObj *obj = (WinrtPackageIdObj *)This;
    if (!v) return E_POINTER;
    v->Major = obj->id.ver_major;
    v->Minor = obj->id.ver_minor;
    v->Build = obj->id.ver_build;
    v->Revision = obj->id.ver_revision;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pid_get_arch(void *This, uint32_t *v) {
    WinrtPackageIdObj *obj = (WinrtPackageIdObj *)This;
    if (!v) return E_POINTER;
    *v = obj->id.architecture;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pid_get_resid(void *This, HSTRING *v) {
    (void)This;
    return winrt_sys_make_hstring(L"", v);
}
static HRESULT STDMETHODCALLTYPE pid_get_pub(void *This, HSTRING *v) {
    return winrt_sys_make_hstring(((WinrtPackageIdObj *)This)->id.publisher, v);
}
static HRESULT STDMETHODCALLTYPE pid_get_pubid(void *This, HSTRING *v) {
    return winrt_sys_make_hstring(((WinrtPackageIdObj *)This)->id.publisher_id, v);
}
static HRESULT STDMETHODCALLTYPE pid_get_fullname(void *This, HSTRING *v) {
    return winrt_sys_make_hstring(((WinrtPackageIdObj *)This)->id.full_name, v);
}
static HRESULT STDMETHODCALLTYPE pid_get_familyname(void *This, HSTRING *v) {
    return winrt_sys_make_hstring(((WinrtPackageIdObj *)This)->id.family_name, v);
}

static HRESULT STDMETHODCALLTYPE pid_m_qi(void *This, REFIID r, void **p) {
    return pid_qi(WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This), r, p);
}
static ULONG STDMETHODCALLTYPE pid_m_addref(void *This) {
    return pid_addref(WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This));
}
static ULONG STDMETHODCALLTYPE pid_m_release(void *This) {
    return pid_release(WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This));
}
static HRESULT STDMETHODCALLTYPE pid_m_getiids(void *This, ULONG *c, IID **i) {
    return pid_getiids(WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This), c, i);
}
static HRESULT STDMETHODCALLTYPE pid_m_rcn(void *This, HSTRING *n) {
    return pid_rcn(WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This), n);
}
static HRESULT STDMETHODCALLTYPE pid_m_tl(void *This, TrustLevel *tl) {
    return pid_tl(WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This), tl);
}
static HRESULT STDMETHODCALLTYPE pid_m_product(void *This, HSTRING *v) {
    WinrtPackageIdObj *obj = WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This);
    const wchar_t *src = obj->id.app_id[0] ? obj->id.app_id : L"{00000000-0000-0000-0000-000000000000}";
    return winrt_sys_make_hstring(src, v);
}
static HRESULT STDMETHODCALLTYPE pid_m_author(void *This, HSTRING *v) {
    WinrtPackageIdObj *obj = WINRT_SYS_FROM(WinrtPackageIdObj, lpVtblMeta, This);
    return winrt_sys_make_hstring(obj->id.publisher, v);
}

static const WinrtPackageIdVtbl g_pid_vtbl = {
    pid_qi, pid_addref, pid_release, pid_getiids, pid_rcn, pid_tl,
    pid_get_name, pid_get_version, pid_get_arch, pid_get_resid,
    pid_get_pub, pid_get_pubid, pid_get_fullname, pid_get_familyname
};
static const WinrtPackageIdWithMetadataVtbl g_pid_meta_vtbl = {
    pid_m_qi, pid_m_addref, pid_m_release, pid_m_getiids, pid_m_rcn, pid_m_tl,
    pid_m_product, pid_m_author
};

/* ============================================================================
 * Package
 * ============================================================================ */

typedef struct WinrtPackageObj {
    const WinrtPackageVtbl  *lpVtbl;
    const WinrtPackage2Vtbl *lpVtbl2;
    const WinrtPackage3Vtbl *lpVtbl3;
    volatile LONG ref_count;
} WinrtPackageObj;

static HRESULT STDMETHODCALLTYPE pkg_qi(void *This, REFIID riid, void **ppv) {
    WinrtPackageObj *obj = (WinrtPackageObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPackage)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPackage2)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl2;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPackage3)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl3;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pkg_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPackageObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE pkg_release(void *This) {
    WinrtPackageObj *obj = (WinrtPackageObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) winrt_sys_free(obj);
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE pkg_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x163c792fu, 0xbd75u, 0x413cu, { 0xbfu, 0x23u, 0xb1u, 0xfeu, 0x7bu, 0x95u, 0xd8u, 0x25u } },
        { 0xa6612fb6u, 0x7688u, 0x4aceu, { 0x95u, 0xfbu, 0x35u, 0x95u, 0x38u, 0xe7u, 0xaau, 0x01u } },
        { 0x5f738b61u, 0xf86au, 0x4917u, { 0x93u, 0xd1u, 0xf1u, 0xeeu, 0x9du, 0x3bu, 0x35u, 0xd9u } }
    };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE pkg_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.ApplicationModel.Package", n);
}
static HRESULT STDMETHODCALLTYPE pkg_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pkg_get_id(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtPackageIdObj *pid = (WinrtPackageIdObj *)winrt_sys_alloc(sizeof(*pid));
    if (!pid) return E_OUTOFMEMORY;
    pid->lpVtbl = &g_pid_vtbl;
    pid->lpVtblMeta = &g_pid_meta_vtbl;
    pid->ref_count = 1;
    resolve_package_identity(&pid->id);
    *val = &pid->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pkg_get_installed_loc(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    uwp_package_identity_t id;
    resolve_package_identity(&id);
    return winrt_sys_create_storage_folder(id.install_path, val);
}
static HRESULT STDMETHODCALLTYPE pkg_get_is_framework(void *This, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pkg_get_deps(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtEmptyPkgView *vw = (WinrtEmptyPkgView *)winrt_sys_alloc(sizeof(*vw));
    if (!vw) return E_OUTOFMEMORY;
    vw->lpVtbl = &g_epkg_vw_vtbl;
    vw->lpVtblIter = &g_epkg_vw_iter_vtbl;
    vw->ref_count = 1;
    *val = &vw->lpVtbl;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pkg2_qi(void *This, REFIID r, void **p) {
    return pkg_qi(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl2, This), r, p);
}
static ULONG STDMETHODCALLTYPE pkg2_addref(void *This) {
    return pkg_addref(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl2, This));
}
static ULONG STDMETHODCALLTYPE pkg2_release(void *This) {
    return pkg_release(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl2, This));
}
static HRESULT STDMETHODCALLTYPE pkg2_getiids(void *This, ULONG *c, IID **i) {
    return pkg_getiids(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl2, This), c, i);
}
static HRESULT STDMETHODCALLTYPE pkg2_rcn(void *This, HSTRING *n) {
    return pkg_rcn(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl2, This), n);
}
static HRESULT STDMETHODCALLTYPE pkg2_tl(void *This, TrustLevel *tl) {
    return pkg_tl(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl2, This), tl);
}
static HRESULT STDMETHODCALLTYPE pkg2_get_display_name(void *This, HSTRING *val) {
    (void)This;
    uwp_package_identity_t id;
    resolve_package_identity(&id);
    return winrt_sys_make_hstring(id.display_name, val);
}
static HRESULT STDMETHODCALLTYPE pkg2_get_pub_display_name(void *This, HSTRING *val) {
    (void)This;
    uwp_package_identity_t id;
    resolve_package_identity(&id);
    return winrt_sys_make_hstring(id.publisher, val);
}
static HRESULT STDMETHODCALLTYPE pkg2_get_desc(void *This, HSTRING *val) {
    (void)This;
    uwp_package_identity_t id;
    resolve_package_identity(&id);
    return winrt_sys_make_hstring(id.display_name, val);
}
static HRESULT STDMETHODCALLTYPE pkg2_get_logo(void *This, void **val) {
    if (val) *val = NULL;
    return winrt_sys_unimplemented("IPackage2::get_Logo", This);
}
static HRESULT STDMETHODCALLTYPE pkg2_get_false(void *This, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pkg3_qi(void *This, REFIID r, void **p) {
    return pkg_qi(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl3, This), r, p);
}
static ULONG STDMETHODCALLTYPE pkg3_addref(void *This) {
    return pkg_addref(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl3, This));
}
static ULONG STDMETHODCALLTYPE pkg3_release(void *This) {
    return pkg_release(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl3, This));
}
static HRESULT STDMETHODCALLTYPE pkg3_getiids(void *This, ULONG *c, IID **i) {
    return pkg_getiids(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl3, This), c, i);
}
static HRESULT STDMETHODCALLTYPE pkg3_rcn(void *This, HSTRING *n) {
    return pkg_rcn(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl3, This), n);
}
static HRESULT STDMETHODCALLTYPE pkg3_tl(void *This, TrustLevel *tl) {
    return pkg_tl(WINRT_SYS_FROM(WinrtPackageObj, lpVtbl3, This), tl);
}
static HRESULT STDMETHODCALLTYPE pkg3_get_status(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtPackageStatusObj *st = (WinrtPackageStatusObj *)winrt_sys_alloc(sizeof(*st));
    if (!st) return E_OUTOFMEMORY;
    st->lpVtbl = &g_pstat_vtbl;
    st->ref_count = 1;
    *val = st;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pkg3_get_installed_date(void *This, int64_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    uwp_package_identity_t id;
    resolve_package_identity(&id);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    memset(&fad, 0, sizeof(fad));
    if (GetFileAttributesExW(id.install_path, GetFileExInfoStandard, &fad)) {
        *val = ((int64_t)fad.ftCreationTime.dwHighDateTime << 32) |
               (int64_t)fad.ftCreationTime.dwLowDateTime;
    } else {
        *val = 132539328000000000LL; /* 2021-01-01T00:00:00Z FILETIME */
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pkg3_get_applist(void *This, void **op) {
    if (op) *op = NULL;
    return winrt_sys_unimplemented("IPackage3::GetAppListEntriesAsync", This);
}

static const WinrtPackageVtbl g_pkg_vtbl = {
    pkg_qi, pkg_addref, pkg_release, pkg_getiids, pkg_rcn, pkg_tl,
    pkg_get_id, pkg_get_installed_loc, pkg_get_is_framework, pkg_get_deps
};
static const WinrtPackage2Vtbl g_pkg2_vtbl = {
    pkg2_qi, pkg2_addref, pkg2_release, pkg2_getiids, pkg2_rcn, pkg2_tl,
    pkg2_get_display_name, pkg2_get_pub_display_name, pkg2_get_desc,
    pkg2_get_logo, pkg2_get_false, pkg2_get_false, pkg2_get_false
};
static const WinrtPackage3Vtbl g_pkg3_vtbl = {
    pkg3_qi, pkg3_addref, pkg3_release, pkg3_getiids, pkg3_rcn, pkg3_tl,
    pkg3_get_status, pkg3_get_installed_date, pkg3_get_applist
};

/* Package Activation Factory */
typedef struct WinrtPackageFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtPackageStaticsVtbl    *lpVtblStatics;
} WinrtPackageFactory;

static WinrtPackageFactory g_pkg_factory;

static HRESULT STDMETHODCALLTYPE pkg_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_pkg_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPackageStatics)) {
        *ppv = &g_pkg_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pkg_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE pkg_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE pkg_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x4e534bdfu, 0x2960u, 0x4878u, { 0x97u, 0xa4u, 0x96u, 0x24u, 0xdeu, 0xb7u, 0x2fu, 0x2du } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE pkg_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE pkg_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pkg_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("Package::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE pkg_fac_get_current(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtPackageObj *pkg = (WinrtPackageObj *)winrt_sys_alloc(sizeof(*pkg));
    if (!pkg) return E_OUTOFMEMORY;
    pkg->lpVtbl = &g_pkg_vtbl;
    pkg->lpVtbl2 = &g_pkg2_vtbl;
    pkg->lpVtbl3 = &g_pkg3_vtbl;
    pkg->ref_count = 1;
    *val = &pkg->lpVtbl;
    return S_OK;
}

static const WinrtActivationFactoryVtbl g_pkg_act_vtbl = {
    pkg_fac_qi, pkg_fac_addref, pkg_fac_release, pkg_fac_getiids, pkg_fac_rcn, pkg_fac_tl, pkg_fac_act
};
static const WinrtPackageStaticsVtbl g_pkg_stat_vtbl = {
    pkg_fac_qi, pkg_fac_addref, pkg_fac_release, pkg_fac_getiids, pkg_fac_rcn, pkg_fac_tl, pkg_fac_get_current
};
static WinrtPackageFactory g_pkg_factory = { &g_pkg_act_vtbl, &g_pkg_stat_vtbl };

HRESULT winrt_sys_get_package_factory(REFIID iid, void **out_factory) {
    return pkg_fac_qi(&g_pkg_factory.lpVtblAct, iid, out_factory);
}
