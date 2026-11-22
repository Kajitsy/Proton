/*
 * Proton-Scarlett — UWP Bridge System WinRT Common Infrastructure
 *
 * Implements:
 *   - uwp_winrt_sys_get_activation_factory() dispatcher
 *   - Win32 path normalization, recursive CreateDirectoryW, state/install dir lookup
 *   - Completed IAsyncAction & generic completed IAsyncOperation<T*> (with IAsyncInfo)
 *   - Windows.Foundation.PropertyValue (IPropertyValue + IPropertyValueStatics)
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls).
 */

#include "winrt_sys_internal.h"

#include <stdio.h>
#include <string.h>

/* ============================================================================
 * Activation Factory Dispatcher
 * ============================================================================ */

HRESULT uwp_winrt_sys_get_activation_factory(
    const wchar_t *class_name,
    REFIID iid,
    void **out_factory) {
    if (out_factory == NULL) {
        return E_POINTER;
    }
    *out_factory = NULL;
    if (class_name == NULL || iid == NULL) {
        return E_INVALIDARG;
    }

    if (wcscmp(class_name, L"Windows.Storage.ApplicationData") == 0) {
        return winrt_sys_get_application_data_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Storage.StorageFolder") == 0) {
        return winrt_sys_get_storage_folder_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.ApplicationModel.Package") == 0) {
        return winrt_sys_get_package_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Gaming.Input.Gamepad") == 0) {
        return winrt_sys_get_gamepad_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.System.Profile.AnalyticsInfo") == 0) {
        return winrt_sys_get_analytics_info_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Gaming.UI.GameBar") == 0) {
        return winrt_sys_get_game_bar_factory(iid, out_factory);
    }
    if (wcscmp(class_name, L"Windows.Foundation.PropertyValue") == 0) {
        return winrt_sys_get_property_value_factory(iid, out_factory);
    }

    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ============================================================================
 * Memory, HSTRING, and Diagnostic Helpers
 * ============================================================================ */

void *winrt_sys_alloc(size_t bytes) {
    if (bytes == 0u) bytes = 1u;
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

void winrt_sys_free(void *ptr) {
    if (ptr != NULL) {
        HeapFree(GetProcessHeap(), 0, ptr);
    }
}

void *winrt_sys_cotask_alloc(size_t bytes) {
    if (bytes == 0u) bytes = 1u;
#ifdef _WIN32
    typedef LPVOID (WINAPI *pfn_cotaskmemalloc_t)(SIZE_T);
    static pfn_cotaskmemalloc_t s_alloc = NULL;
    static LONG s_init = 0;
    if (s_init == 0) {
        HMODULE ole32 = GetModuleHandleA("ole32.dll");
        if (!ole32) {
            ole32 = LoadLibraryA("ole32.dll");
        }
        if (ole32) {
            s_alloc = (pfn_cotaskmemalloc_t)(void *)GetProcAddress(ole32, "CoTaskMemAlloc");
        }
        InterlockedCompareExchange(&s_init, 1, 0);
    }
    if (s_alloc != NULL) {
        return s_alloc((SIZE_T)bytes);
    }
    return winrt_sys_alloc(bytes);
#else
    return CoTaskMemAlloc(bytes);
#endif
}

HRESULT winrt_sys_make_hstring(const wchar_t *str, HSTRING *out_hstr) {
    if (out_hstr == NULL) {
        return E_POINTER;
    }
    *out_hstr = NULL;
    if (str == NULL || str[0] == L'\0') {
        return S_OK;
    }
    size_t len = wcslen(str);
    return WindowsCreateString(str, (UINT32)len, out_hstr);
}

HRESULT winrt_sys_dup_hstring(HSTRING src, HSTRING *out_hstr) {
    if (out_hstr == NULL) {
        return E_POINTER;
    }
    *out_hstr = NULL;
    if (src == NULL) {
        return S_OK;
    }
    return WindowsDuplicateString(src, out_hstr);
}

const wchar_t *winrt_sys_hstring_buf(HSTRING hstr, uint32_t *out_len) {
    UINT32 len = 0u;
    const wchar_t *raw = WindowsGetStringRawBuffer(hstr, &len);
    if (out_len != NULL) {
        *out_len = (uint32_t)len;
    }
    return raw ? raw : L"";
}

HRESULT winrt_sys_get_iids_copy(
    const GUID *iids,
    ULONG count,
    ULONG *out_count,
    IID **out_iids) {
    if (out_count == NULL || out_iids == NULL) {
        return E_POINTER;
    }
    *out_count = 0;
    *out_iids = NULL;
    if (count == 0 || iids == NULL) {
        return S_OK;
    }
    IID *buf = (IID *)winrt_sys_cotask_alloc((size_t)count * sizeof(IID));
    if (buf == NULL) {
        return E_OUTOFMEMORY;
    }
    memcpy(buf, iids, (size_t)count * sizeof(IID));
    *out_count = count;
    *out_iids = buf;
    return S_OK;
}

HRESULT winrt_sys_unimplemented(const char *iface_method, void *self) {
    uwp_bridge_log("winrt_sys",
                   "UNIMPLEMENTED %s (this=%p) -> E_NOTIMPL",
                   iface_method ? iface_method : "unknown",
                   self);
    return E_NOTIMPL;
}

/* ============================================================================
 * Path Normalization & Directory Creation (Win32 Only)
 * ============================================================================ */

void winrt_sys_copy_wstr(wchar_t *dst, size_t dst_chars, const wchar_t *src) {
    if (dst == NULL || dst_chars == 0u) return;
    if (src == NULL) {
        dst[0] = L'\0';
        return;
    }
    size_t i = 0u;
    while (i + 1u < dst_chars && src[i] != L'\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = L'\0';
}

void winrt_sys_normalize_path(const wchar_t *in_path, wchar_t *out_path, size_t out_chars) {
    if (out_path == NULL || out_chars == 0u) return;
    out_path[0] = L'\0';
    if (in_path == NULL || in_path[0] == L'\0') return;

    wchar_t tmp[UWP_MAX_PATH_CHARS];
    size_t w = 0u;

    /* Convert leading Unix '/' to 'Z:\' for Wine DOS path compatibility */
    if (in_path[0] == L'/') {
        tmp[w++] = L'Z';
        tmp[w++] = L':';
    }

    for (size_t i = 0u; in_path[i] != L'\0' && w + 1u < UWP_MAX_PATH_CHARS; ++i) {
        wchar_t ch = (in_path[i] == L'/') ? L'\\' : in_path[i];
        /* Collapse duplicate backslashes after prefix (preserve UNC \\ at start) */
        if (ch == L'\\' && w > 1u && tmp[w - 1u] == L'\\') {
            continue;
        }
        tmp[w++] = ch;
    }
    tmp[w] = L'\0';

    /* Strip trailing backslash unless drive/volume root like "C:\" or "GAME:\" */
    while (w > 1u && tmp[w - 1u] == L'\\') {
        if (w >= 3u && tmp[w - 2u] == L':') {
            break;
        }
        tmp[--w] = L'\0';
    }

    winrt_sys_copy_wstr(out_path, out_chars, tmp);
}

BOOL winrt_sys_ensure_dir_recursive(const wchar_t *dir_path) {
    if (dir_path == NULL || dir_path[0] == L'\0') return FALSE;

    wchar_t norm[UWP_MAX_PATH_CHARS];
    winrt_sys_normalize_path(dir_path, norm, UWP_MAX_PATH_CHARS);
    if (norm[0] == L'\0') return FALSE;

    DWORD attrs = GetFileAttributesW(norm);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    wchar_t cur[UWP_MAX_PATH_CHARS];
    winrt_sys_copy_wstr(cur, UWP_MAX_PATH_CHARS, norm);
    size_t len = wcslen(cur);

    size_t start = 0u;
    const wchar_t *colon = wcschr(cur, L':');
    if (colon != NULL && colon[1] == L'\\') {
        start = (size_t)((colon - cur) + 2);
    } else if (len >= 2u && cur[0] == L'\\' && cur[1] == L'\\') {
        start = 2u;
    }

    for (size_t i = start; i <= len; ++i) {
        if (cur[i] == L'\\' || cur[i] == L'\0') {
            wchar_t saved = cur[i];
            if (i > start) {
                cur[i] = L'\0';
                DWORD a = GetFileAttributesW(cur);
                if (a == INVALID_FILE_ATTRIBUTES) {
                    if (!CreateDirectoryW(cur, NULL)) {
                        DWORD err = GetLastError();
                        if (err != ERROR_ALREADY_EXISTS) {
                            DWORD recheck = GetFileAttributesW(cur);
                            if (recheck == INVALID_FILE_ATTRIBUTES ||
                                (recheck & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                                return FALSE;
                            }
                        }
                    }
                } else if ((a & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    return FALSE;
                }
                cur[i] = saved;
            }
        }
    }

    attrs = GetFileAttributesW(norm);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

void winrt_sys_get_install_dir(wchar_t *out_path, size_t out_chars) {
    if (out_path == NULL || out_chars == 0u) return;
    out_path[0] = L'\0';

    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    if (id != NULL && id->install_path[0] != L'\0') {
        winrt_sys_normalize_path(id->install_path, out_path, out_chars);
        return;
    }

    wchar_t env_buf[UWP_MAX_PATH_CHARS];
    DWORD n = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_DIR", env_buf, UWP_MAX_PATH_CHARS);
    if (n > 0u && n < UWP_MAX_PATH_CHARS) {
        winrt_sys_normalize_path(env_buf, out_path, out_chars);
        return;
    }

    DWORD game_attr = GetFileAttributesW(L"GAME:\\");
    if (game_attr != INVALID_FILE_ATTRIBUTES && (game_attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        winrt_sys_copy_wstr(out_path, out_chars, L"GAME:\\");
        return;
    }

    wchar_t exe_path[UWP_MAX_PATH_CHARS];
    DWORD exe_len = GetModuleFileNameW(NULL, exe_path, UWP_MAX_PATH_CHARS);
    if (exe_len > 0u && exe_len < UWP_MAX_PATH_CHARS) {
        wchar_t *last_slash = wcsrchr(exe_path, L'\\');
        wchar_t *last_fwd = wcsrchr(exe_path, L'/');
        if (last_fwd != NULL && (last_slash == NULL || last_fwd > last_slash)) {
            last_slash = last_fwd;
        }
        if (last_slash != NULL) {
            *last_slash = L'\0';
            winrt_sys_normalize_path(exe_path, out_path, out_chars);
            return;
        }
    }

    winrt_sys_copy_wstr(out_path, out_chars, L"C:\\");
}

void winrt_sys_get_state_dir(winrt_sys_state_kind_t kind, wchar_t *out_path, size_t out_chars) {
    if (out_path == NULL || out_chars == 0u) return;
    out_path[0] = L'\0';

    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    const wchar_t *id_folder = NULL;
    const wchar_t *env_name = NULL;
    const wchar_t *sub_name = L"LocalState";

    switch (kind) {
    case WINRT_SYS_STATE_LOCAL:
        id_folder = (id != NULL && id->local_folder[0] != L'\0') ? id->local_folder : NULL;
        env_name = L"PROTON_UWP_LOCAL_STATE";
        sub_name = L"LocalState";
        break;
    case WINRT_SYS_STATE_ROAMING:
        id_folder = (id != NULL && id->roaming_folder[0] != L'\0') ? id->roaming_folder : NULL;
        env_name = L"PROTON_UWP_ROAMING_STATE";
        sub_name = L"RoamingState";
        break;
    case WINRT_SYS_STATE_TEMP:
        id_folder = (id != NULL && id->temp_folder[0] != L'\0') ? id->temp_folder : NULL;
        env_name = L"PROTON_UWP_TEMP_STATE";
        sub_name = L"TempState";
        break;
    case WINRT_SYS_STATE_LOCAL_CACHE:
        sub_name = L"LocalCache";
        break;
    case WINRT_SYS_STATE_SHARED_LOCAL:
        sub_name = L"SharedLocal";
        break;
    }

    wchar_t raw[UWP_MAX_PATH_CHARS];
    raw[0] = L'\0';

    if (id_folder != NULL) {
        winrt_sys_copy_wstr(raw, UWP_MAX_PATH_CHARS, id_folder);
    } else if (env_name != NULL) {
        DWORD n = GetEnvironmentVariableW(env_name, raw, UWP_MAX_PATH_CHARS);
        if (n == 0u || n >= UWP_MAX_PATH_CHARS) {
            raw[0] = L'\0';
        }
    }

    if (raw[0] == L'\0') {
        /* If LocalState is available and we need LocalCache/SharedLocal, derive sibling */
        if (kind == WINRT_SYS_STATE_LOCAL_CACHE || kind == WINRT_SYS_STATE_SHARED_LOCAL) {
            wchar_t local_dir[UWP_MAX_PATH_CHARS];
            winrt_sys_get_state_dir(WINRT_SYS_STATE_LOCAL, local_dir, UWP_MAX_PATH_CHARS);
            wchar_t *slash = wcsrchr(local_dir, L'\\');
            if (slash != NULL && wcscmp(slash + 1, L"LocalState") == 0) {
                *slash = L'\0';
                swprintf(raw, UWP_MAX_PATH_CHARS, L"%ls\\%ls", local_dir, sub_name);
                raw[UWP_MAX_PATH_CHARS - 1u] = L'\0';
            }
        }
    }

    if (raw[0] == L'\0') {
        wchar_t family[UWP_MAX_IDENTITY_CHARS];
        family[0] = L'\0';
        if (id != NULL && id->family_name[0] != L'\0') {
            winrt_sys_copy_wstr(family, UWP_MAX_IDENTITY_CHARS, id->family_name);
        } else {
            DWORD fn = GetEnvironmentVariableW(L"PROTON_UWP_PACKAGE_FAMILY_NAME",
                                               family,
                                               UWP_MAX_IDENTITY_CHARS);
            if (fn == 0u || fn >= UWP_MAX_IDENTITY_CHARS) {
                winrt_sys_copy_wstr(family, UWP_MAX_IDENTITY_CHARS, L"ProtonScarlett.UWPApp");
            }
        }

        wchar_t appdata[UWP_MAX_PATH_CHARS];
        DWORD ad_len = GetEnvironmentVariableW(L"LOCALAPPDATA", appdata, UWP_MAX_PATH_CHARS);
        if (ad_len == 0u || ad_len >= UWP_MAX_PATH_CHARS) {
            ad_len = GetEnvironmentVariableW(L"APPDATA", appdata, UWP_MAX_PATH_CHARS);
        }
        if (ad_len > 0u && ad_len < UWP_MAX_PATH_CHARS) {
            wchar_t norm_ad[UWP_MAX_PATH_CHARS];
            winrt_sys_normalize_path(appdata, norm_ad, UWP_MAX_PATH_CHARS);
            swprintf(raw, UWP_MAX_PATH_CHARS, L"%ls\\Packages\\%ls\\%ls",
                     norm_ad, family, sub_name);
            raw[UWP_MAX_PATH_CHARS - 1u] = L'\0';
        } else {
            wchar_t install_dir[UWP_MAX_PATH_CHARS];
            winrt_sys_get_install_dir(install_dir, UWP_MAX_PATH_CHARS);
            size_t ilen = wcslen(install_dir);
            if (ilen > 0u && install_dir[ilen - 1u] == L'\\') {
                swprintf(raw, UWP_MAX_PATH_CHARS, L"%lsUserData\\%ls", install_dir, sub_name);
            } else {
                swprintf(raw, UWP_MAX_PATH_CHARS, L"%ls\\UserData\\%ls", install_dir, sub_name);
            }
            raw[UWP_MAX_PATH_CHARS - 1u] = L'\0';
        }
    }

    winrt_sys_normalize_path(raw, out_path, out_chars);
    (void)winrt_sys_ensure_dir_recursive(out_path);
}

/* ============================================================================
 * Completed IAsyncAction & IAsyncOperation<T*> (with IAsyncInfo tear-off)
 * ============================================================================ */

static volatile LONG g_next_async_id = 1;

typedef struct WinrtAsyncActionObj {
    const WinrtAsyncActionVtbl *lpVtbl;
    const WinrtAsyncInfoVtbl   *lpVtblInfo;
    volatile LONG ref_count;
    uint32_t id;
    HRESULT err_hr;
    AsyncStatus status;
    IUnknown *completed_handler;
    CRITICAL_SECTION lock;
} WinrtAsyncActionObj;

static HRESULT STDMETHODCALLTYPE async_action_qi(void *This, REFIID riid, void **ppv) {
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)This;
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (riid == NULL) return E_INVALIDARG;

    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAsyncAction)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IAsyncInfo)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblInfo;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE async_action_addref(void *This) {
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)This;
    return (ULONG)InterlockedIncrement(&obj->ref_count);
}

static ULONG STDMETHODCALLTYPE async_action_release(void *This) {
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->completed_handler != NULL) {
            obj->completed_handler->lpVtbl->Release(obj->completed_handler);
        }
        DeleteCriticalSection(&obj->lock);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE async_action_get_iids(void *This, ULONG *cnt, IID **iids) {
    (void)This;
    static const GUID list[] = {
        { 0x5a648006u, 0x843au, 0x4da9u, { 0x86u, 0x5bu, 0x9du, 0x26u, 0xe5u, 0xdfu, 0xadu, 0x7bu } },
        { 0x00000036u, 0x0000u, 0x0000u, { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } }
    };
    return winrt_sys_get_iids_copy(list, 2, cnt, iids);
}

static HRESULT STDMETHODCALLTYPE async_action_get_rcn(void *This, HSTRING *name) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.IAsyncAction", name);
}

static HRESULT STDMETHODCALLTYPE async_action_get_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_action_put_completed(void *This, void *handler) {
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)This;
    IUnknown *h = (IUnknown *)handler;
    EnterCriticalSection(&obj->lock);
    if (obj->completed_handler != NULL) {
        obj->completed_handler->lpVtbl->Release(obj->completed_handler);
        obj->completed_handler = NULL;
    }
    if (h != NULL) {
        h->lpVtbl->AddRef(h);
        obj->completed_handler = h;
    }
    AsyncStatus st = obj->status;
    LeaveCriticalSection(&obj->lock);

    if (h != NULL) {
        typedef struct { const WinrtAsyncActionCompletedHandlerVtbl *lpVtbl; } HandlerIf;
        ((HandlerIf *)h)->lpVtbl->Invoke(h, &obj->lpVtbl, st);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_action_get_completed(void *This, void **handler) {
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)This;
    if (!handler) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    *handler = obj->completed_handler;
    if (obj->completed_handler != NULL) {
        obj->completed_handler->lpVtbl->AddRef(obj->completed_handler);
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_action_get_results(void *This) {
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)This;
    return obj->err_hr;
}

static HRESULT STDMETHODCALLTYPE async_action_info_qi(void *This, REFIID riid, void **ppv) {
    return async_action_qi(WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE async_action_info_addref(void *This) {
    return async_action_addref(WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This));
}
static ULONG STDMETHODCALLTYPE async_action_info_release(void *This) {
    return async_action_release(WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This));
}
static HRESULT STDMETHODCALLTYPE async_action_info_get_iids(void *This, ULONG *c, IID **i) {
    return async_action_get_iids(WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This), c, i);
}
static HRESULT STDMETHODCALLTYPE async_action_info_get_rcn(void *This, HSTRING *n) {
    return async_action_get_rcn(WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This), n);
}
static HRESULT STDMETHODCALLTYPE async_action_info_get_tl(void *This, TrustLevel *tl) {
    return async_action_get_tl(WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This), tl);
}
static HRESULT STDMETHODCALLTYPE async_action_info_get_id(void *This, uint32_t *id) {
    WinrtAsyncActionObj *obj = WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This);
    if (!id) return E_POINTER;
    *id = obj->id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_action_info_get_status(void *This, AsyncStatus *st) {
    WinrtAsyncActionObj *obj = WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This);
    if (!st) return E_POINTER;
    *st = obj->status;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_action_info_get_err(void *This, HRESULT *err) {
    WinrtAsyncActionObj *obj = WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This);
    if (!err) return E_POINTER;
    *err = obj->err_hr;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_action_info_cancel(void *This) {
    (void)This;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_action_info_close(void *This) {
    WinrtAsyncActionObj *obj = WINRT_SYS_FROM(WinrtAsyncActionObj, lpVtblInfo, This);
    EnterCriticalSection(&obj->lock);
    if (obj->completed_handler != NULL) {
        obj->completed_handler->lpVtbl->Release(obj->completed_handler);
        obj->completed_handler = NULL;
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static const WinrtAsyncActionVtbl g_async_action_vtbl = {
    async_action_qi,
    async_action_addref,
    async_action_release,
    async_action_get_iids,
    async_action_get_rcn,
    async_action_get_tl,
    async_action_put_completed,
    async_action_get_completed,
    async_action_get_results
};

static const WinrtAsyncInfoVtbl g_async_action_info_vtbl = {
    async_action_info_qi,
    async_action_info_addref,
    async_action_info_release,
    async_action_info_get_iids,
    async_action_info_get_rcn,
    async_action_info_get_tl,
    async_action_info_get_id,
    async_action_info_get_status,
    async_action_info_get_err,
    async_action_info_cancel,
    async_action_info_close
};

HRESULT winrt_sys_create_completed_async_action(HRESULT err_hr, void **out_action) {
    if (!out_action) return E_POINTER;
    *out_action = NULL;
    WinrtAsyncActionObj *obj = (WinrtAsyncActionObj *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    obj->lpVtbl = &g_async_action_vtbl;
    obj->lpVtblInfo = &g_async_action_info_vtbl;
    obj->ref_count = 1;
    obj->id = (uint32_t)InterlockedIncrement(&g_next_async_id);
    obj->err_hr = err_hr;
    obj->status = SUCCEEDED(err_hr) ? Completed : Error;
    InitializeCriticalSection(&obj->lock);
    *out_action = &obj->lpVtbl;
    return S_OK;
}

typedef struct WinrtAsyncOpPtrObj {
    const WinrtAsyncOperationPtrVtbl *lpVtbl;
    const WinrtAsyncInfoVtbl         *lpVtblInfo;
    volatile LONG ref_count;
    GUID op_iid;
    wchar_t runtime_class_name[160];
    uint32_t id;
    HRESULT err_hr;
    AsyncStatus status;
    IUnknown *result_obj;
    IUnknown *completed_handler;
    CRITICAL_SECTION lock;
} WinrtAsyncOpPtrObj;

static HRESULT STDMETHODCALLTYPE async_op_qi(void *This, REFIID riid, void **ppv) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;

    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &obj->op_iid)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IAsyncInfo)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblInfo;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE async_op_addref(void *This) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    return (ULONG)InterlockedIncrement(&obj->ref_count);
}

static ULONG STDMETHODCALLTYPE async_op_release(void *This) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->result_obj) obj->result_obj->lpVtbl->Release(obj->result_obj);
        if (obj->completed_handler) obj->completed_handler->lpVtbl->Release(obj->completed_handler);
        DeleteCriticalSection(&obj->lock);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE async_op_get_iids(void *This, ULONG *cnt, IID **iids) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    GUID list[2] = { obj->op_iid, WINRT_IID_IAsyncInfo };
    return winrt_sys_get_iids_copy(list, 2, cnt, iids);
}

static HRESULT STDMETHODCALLTYPE async_op_get_rcn(void *This, HSTRING *name) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    return winrt_sys_make_hstring(obj->runtime_class_name, name);
}

static HRESULT STDMETHODCALLTYPE async_op_get_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_op_put_completed(void *This, void *handler) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    IUnknown *h = (IUnknown *)handler;
    EnterCriticalSection(&obj->lock);
    if (obj->completed_handler) {
        obj->completed_handler->lpVtbl->Release(obj->completed_handler);
        obj->completed_handler = NULL;
    }
    if (h) {
        h->lpVtbl->AddRef(h);
        obj->completed_handler = h;
    }
    AsyncStatus st = obj->status;
    LeaveCriticalSection(&obj->lock);

    if (h) {
        typedef struct { const WinrtAsyncCompletedHandlerPtrVtbl *lpVtbl; } HandlerIf;
        ((HandlerIf *)h)->lpVtbl->Invoke(h, &obj->lpVtbl, st);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_op_get_completed(void *This, void **handler) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    if (!handler) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    *handler = obj->completed_handler;
    if (obj->completed_handler) obj->completed_handler->lpVtbl->AddRef(obj->completed_handler);
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_op_get_results(void *This, void **results) {
    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)This;
    if (!results) return E_POINTER;
    *results = NULL;
    if (FAILED(obj->err_hr)) {
        return obj->err_hr;
    }
    if (obj->result_obj != NULL) {
        obj->result_obj->lpVtbl->AddRef(obj->result_obj);
        *results = obj->result_obj;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE async_op_info_qi(void *This, REFIID riid, void **ppv) {
    return async_op_qi(WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This), riid, ppv);
}
static ULONG STDMETHODCALLTYPE async_op_info_addref(void *This) {
    return async_op_addref(WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This));
}
static ULONG STDMETHODCALLTYPE async_op_info_release(void *This) {
    return async_op_release(WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This));
}
static HRESULT STDMETHODCALLTYPE async_op_info_get_iids(void *This, ULONG *c, IID **i) {
    return async_op_get_iids(WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This), c, i);
}
static HRESULT STDMETHODCALLTYPE async_op_info_get_rcn(void *This, HSTRING *n) {
    return async_op_get_rcn(WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This), n);
}
static HRESULT STDMETHODCALLTYPE async_op_info_get_tl(void *This, TrustLevel *tl) {
    return async_op_get_tl(WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This), tl);
}
static HRESULT STDMETHODCALLTYPE async_op_info_get_id(void *This, uint32_t *id) {
    WinrtAsyncOpPtrObj *obj = WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This);
    if (!id) return E_POINTER;
    *id = obj->id;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_op_info_get_status(void *This, AsyncStatus *st) {
    WinrtAsyncOpPtrObj *obj = WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This);
    if (!st) return E_POINTER;
    *st = obj->status;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_op_info_get_err(void *This, HRESULT *err) {
    WinrtAsyncOpPtrObj *obj = WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This);
    if (!err) return E_POINTER;
    *err = obj->err_hr;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_op_info_cancel(void *This) {
    (void)This;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE async_op_info_close(void *This) {
    WinrtAsyncOpPtrObj *obj = WINRT_SYS_FROM(WinrtAsyncOpPtrObj, lpVtblInfo, This);
    EnterCriticalSection(&obj->lock);
    if (obj->completed_handler) {
        obj->completed_handler->lpVtbl->Release(obj->completed_handler);
        obj->completed_handler = NULL;
    }
    if (obj->result_obj) {
        obj->result_obj->lpVtbl->Release(obj->result_obj);
        obj->result_obj = NULL;
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static const WinrtAsyncOperationPtrVtbl g_async_op_vtbl = {
    async_op_qi,
    async_op_addref,
    async_op_release,
    async_op_get_iids,
    async_op_get_rcn,
    async_op_get_tl,
    async_op_put_completed,
    async_op_get_completed,
    async_op_get_results
};

static const WinrtAsyncInfoVtbl g_async_op_info_vtbl = {
    async_op_info_qi,
    async_op_info_addref,
    async_op_info_release,
    async_op_info_get_iids,
    async_op_info_get_rcn,
    async_op_info_get_tl,
    async_op_info_get_id,
    async_op_info_get_status,
    async_op_info_get_err,
    async_op_info_cancel,
    async_op_info_close
};

HRESULT winrt_sys_create_completed_async_op_ptr(
    const GUID *op_iid,
    const wchar_t *runtime_class_name,
    IUnknown *result_obj,
    HRESULT err_hr,
    void **out_op) {
    if (!out_op) return E_POINTER;
    *out_op = NULL;
    if (!op_iid) return E_INVALIDARG;

    WinrtAsyncOpPtrObj *obj = (WinrtAsyncOpPtrObj *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return E_OUTOFMEMORY;
    obj->lpVtbl = &g_async_op_vtbl;
    obj->lpVtblInfo = &g_async_op_info_vtbl;
    obj->ref_count = 1;
    obj->op_iid = *op_iid;
    winrt_sys_copy_wstr(obj->runtime_class_name, 160,
                        runtime_class_name ? runtime_class_name : L"Windows.Foundation.IAsyncOperation");
    obj->id = (uint32_t)InterlockedIncrement(&g_next_async_id);
    obj->err_hr = err_hr;
    obj->status = SUCCEEDED(err_hr) ? Completed : Error;
    if (result_obj != NULL) {
        result_obj->lpVtbl->AddRef(result_obj);
        obj->result_obj = result_obj;
    }
    InitializeCriticalSection(&obj->lock);
    *out_op = &obj->lpVtbl;
    return S_OK;
}

/* ============================================================================
 * Windows.Foundation.PropertyValue (IPropertyValue + IPropertyValueStatics)
 * ============================================================================ */

typedef struct WinrtPropertyValueObj {
    const WinrtPropertyValueVtbl *lpVtbl;
    volatile LONG ref_count;
    WinrtPropertyType type;
    union {
        uint8_t  u8;
        int16_t  i16;
        uint16_t u16;
        int32_t  i32;
        uint32_t u32;
        int64_t  i64;
        uint64_t u64;
        float    f32;
        double   f64;
        wchar_t  ch;
        boolean  b;
        HSTRING  hstr;
        GUID     guid;
    } v;
} WinrtPropertyValueObj;

static HRESULT STDMETHODCALLTYPE pv_qi(void *This, REFIID riid, void **ppv) {
    WinrtPropertyValueObj *obj = (WinrtPropertyValueObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IPropertyValue)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE pv_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtPropertyValueObj *)This)->ref_count);
}

static ULONG STDMETHODCALLTYPE pv_release(void *This) {
    WinrtPropertyValueObj *obj = (WinrtPropertyValueObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->type == WinrtPropertyType_String && obj->v.hstr != NULL) {
            WindowsDeleteString(obj->v.hstr);
        }
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}

static HRESULT STDMETHODCALLTYPE pv_get_iids(void *This, ULONG *c, IID **iids) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IPropertyValue, 1, c, iids);
}

static HRESULT STDMETHODCALLTYPE pv_get_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.PropertyValue", n);
}

static HRESULT STDMETHODCALLTYPE pv_get_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_get_type(void *This, WinrtPropertyType *val) {
    if (!val) return E_POINTER;
    *val = ((WinrtPropertyValueObj *)This)->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_get_is_numeric(void *This, boolean *val) {
    if (!val) return E_POINTER;
    WinrtPropertyType t = ((WinrtPropertyValueObj *)This)->type;
    *val = (t >= WinrtPropertyType_UInt8 && t <= WinrtPropertyType_Double) ? 1 : 0;
    return S_OK;
}

#define PV_TYPE_MISMATCH ((HRESULT)0x80028CA0L) /* TYPE_E_TYPEMISMATCH */

static HRESULT STDMETHODCALLTYPE pv_get_u8(void *This, uint8_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_UInt8) return PV_TYPE_MISMATCH;
    *v = o->v.u8;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_i16(void *This, int16_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Int16) return PV_TYPE_MISMATCH;
    *v = o->v.i16;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_u16(void *This, uint16_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_UInt16) return PV_TYPE_MISMATCH;
    *v = o->v.u16;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_i32(void *This, int32_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Int32) return PV_TYPE_MISMATCH;
    *v = o->v.i32;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_u32(void *This, uint32_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_UInt32) return PV_TYPE_MISMATCH;
    *v = o->v.u32;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_i64(void *This, int64_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Int64) return PV_TYPE_MISMATCH;
    *v = o->v.i64;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_u64(void *This, uint64_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_UInt64) return PV_TYPE_MISMATCH;
    *v = o->v.u64;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_f32(void *This, float *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Single) return PV_TYPE_MISMATCH;
    *v = o->v.f32;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_f64(void *This, double *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Double) return PV_TYPE_MISMATCH;
    *v = o->v.f64;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_char16(void *This, wchar_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Char16) return PV_TYPE_MISMATCH;
    *v = o->v.ch;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_bool(void *This, boolean *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Boolean) return PV_TYPE_MISMATCH;
    *v = o->v.b;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_str(void *This, HSTRING *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_String) return PV_TYPE_MISMATCH;
    return winrt_sys_dup_hstring(o->v.hstr, v);
}
static HRESULT STDMETHODCALLTYPE pv_get_guid(void *This, GUID *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_Guid) return PV_TYPE_MISMATCH;
    *v = o->v.guid;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_dt(void *This, int64_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_DateTime) return PV_TYPE_MISMATCH;
    *v = o->v.i64;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_get_ts(void *This, int64_t *v) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)This;
    if (!v) return E_POINTER;
    if (o->type != WinrtPropertyType_TimeSpan) return PV_TYPE_MISMATCH;
    *v = o->v.i64;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pv_unimpl_ptr(void *This, void *v) {
    (void)v;
    return winrt_sys_unimplemented("IPropertyValue::GetStruct", This);
}
static HRESULT STDMETHODCALLTYPE pv_unimpl_arr(void *This, uint32_t *sz, void **v) {
    (void)sz;
    (void)v;
    return winrt_sys_unimplemented("IPropertyValue::GetArray", This);
}

static const WinrtPropertyValueVtbl g_pv_vtbl = {
    pv_qi, pv_addref, pv_release, pv_get_iids, pv_get_rcn, pv_get_tl,
    pv_get_type, pv_get_is_numeric,
    pv_get_u8, pv_get_i16, pv_get_u16, pv_get_i32, pv_get_u32,
    pv_get_i64, pv_get_u64, pv_get_f32, pv_get_f64, pv_get_char16,
    pv_get_bool, pv_get_str, pv_get_guid, pv_get_dt, pv_get_ts,
    pv_unimpl_ptr, pv_unimpl_ptr, pv_unimpl_ptr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, uint8_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, int16_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, uint16_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, int32_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, uint32_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, int64_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, uint64_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, float **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, double **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, wchar_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, boolean **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, HSTRING **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, IInspectable ***))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, GUID **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, int64_t **))pv_unimpl_arr,
    (HRESULT (STDMETHODCALLTYPE *)(void *, uint32_t *, int64_t **))pv_unimpl_arr,
    pv_unimpl_arr, pv_unimpl_arr, pv_unimpl_arr
};

static WinrtPropertyValueObj *pv_alloc(WinrtPropertyType type) {
    WinrtPropertyValueObj *o = (WinrtPropertyValueObj *)winrt_sys_alloc(sizeof(*o));
    if (!o) return NULL;
    o->lpVtbl = &g_pv_vtbl;
    o->ref_count = 1;
    o->type = type;
    return o;
}

HRESULT winrt_sys_create_property_value_string(const wchar_t *str, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_String);
    if (!o) return E_OUTOFMEMORY;
    HRESULT hr = winrt_sys_make_hstring(str, &o->v.hstr);
    if (FAILED(hr)) {
        winrt_sys_free(o);
        return hr;
    }
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_int32(int32_t val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Int32);
    if (!o) return E_OUTOFMEMORY;
    o->v.i32 = val;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_uint32(uint32_t val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_UInt32);
    if (!o) return E_OUTOFMEMORY;
    o->v.u32 = val;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_int64(int64_t val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Int64);
    if (!o) return E_OUTOFMEMORY;
    o->v.i64 = val;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_uint64(uint64_t val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_UInt64);
    if (!o) return E_OUTOFMEMORY;
    o->v.u64 = val;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_single(float val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Single);
    if (!o) return E_OUTOFMEMORY;
    o->v.f32 = val;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_double(double val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Double);
    if (!o) return E_OUTOFMEMORY;
    o->v.f64 = val;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_boolean(boolean val, void **out_pv) {
    if (!out_pv) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Boolean);
    if (!o) return E_OUTOFMEMORY;
    o->v.b = val ? 1 : 0;
    *out_pv = o;
    return S_OK;
}

HRESULT winrt_sys_create_property_value_guid(const GUID *val, void **out_pv) {
    if (!out_pv || !val) return E_POINTER;
    *out_pv = NULL;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Guid);
    if (!o) return E_OUTOFMEMORY;
    o->v.guid = *val;
    *out_pv = o;
    return S_OK;
}

/* IPropertyValueStatics factory */
typedef struct WinrtPropertyValueStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *CreateEmpty)(void *This, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateUInt8)(void *This, uint8_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateInt16)(void *This, int16_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateUInt16)(void *This, uint16_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateInt32)(void *This, int32_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateUInt32)(void *This, uint32_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateInt64)(void *This, int64_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateUInt64)(void *This, uint64_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateSingle)(void *This, float v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateDouble)(void *This, double v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateChar16)(void *This, wchar_t v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateBoolean)(void *This, boolean v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateString)(void *This, HSTRING v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateInspectable)(void *This, IInspectable *v, void **pv);
    HRESULT (STDMETHODCALLTYPE *CreateGuid)(void *This, GUID v, void **pv);
} WinrtPropertyValueStaticsVtbl;

typedef struct WinrtPropertyValueFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtPropertyValueStaticsVtbl *lpVtblStatics;
} WinrtPropertyValueFactory;

static WinrtPropertyValueFactory g_pv_factory;

static HRESULT STDMETHODCALLTYPE pv_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_pv_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IPropertyValueStatics)) {
        *ppv = &g_pv_factory.lpVtblStatics;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE pv_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE pv_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE pv_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x629bdbc8u, 0xd932u, 0x4ff4u, { 0x96u, 0xb9u, 0x8du, 0x96u, 0xc5u, 0xc1u, 0xe8u, 0x58u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE pv_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE pv_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("PropertyValue::ActivateInstance", This);
}
static HRESULT STDMETHODCALLTYPE pv_st_empty(void *This, void **pv) {
    (void)This;
    if (!pv) return E_POINTER;
    *pv = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_st_u8(void *This, uint8_t v, void **pv) {
    (void)This;
    if (!pv) return E_POINTER;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_UInt8);
    if (!o) return E_OUTOFMEMORY;
    o->v.u8 = v;
    *pv = o;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_st_i16(void *This, int16_t v, void **pv) {
    (void)This;
    if (!pv) return E_POINTER;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Int16);
    if (!o) return E_OUTOFMEMORY;
    o->v.i16 = v;
    *pv = o;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_st_u16(void *This, uint16_t v, void **pv) {
    (void)This;
    if (!pv) return E_POINTER;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_UInt16);
    if (!o) return E_OUTOFMEMORY;
    o->v.u16 = v;
    *pv = o;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_st_i32(void *This, int32_t v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_int32(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_u32(void *This, uint32_t v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_uint32(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_i64(void *This, int64_t v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_int64(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_u64(void *This, uint64_t v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_uint64(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_f32(void *This, float v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_single(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_f64(void *This, double v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_double(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_c16(void *This, wchar_t v, void **pv) {
    (void)This;
    if (!pv) return E_POINTER;
    WinrtPropertyValueObj *o = pv_alloc(WinrtPropertyType_Char16);
    if (!o) return E_OUTOFMEMORY;
    o->v.ch = v;
    *pv = o;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_st_bool(void *This, boolean v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_boolean(v, pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_str(void *This, HSTRING v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_string(winrt_sys_hstring_buf(v, NULL), pv);
}
static HRESULT STDMETHODCALLTYPE pv_st_insp(void *This, IInspectable *v, void **pv) {
    (void)This;
    if (!pv) return E_POINTER;
    *pv = v;
    if (v) v->lpVtbl->AddRef(v);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE pv_st_guid(void *This, GUID v, void **pv) {
    (void)This;
    return winrt_sys_create_property_value_guid(&v, pv);
}

static const WinrtActivationFactoryVtbl g_pv_act_vtbl = {
    pv_fac_qi, pv_fac_addref, pv_fac_release, pv_fac_getiids, pv_fac_rcn, pv_fac_tl, pv_fac_act
};
static const WinrtPropertyValueStaticsVtbl g_pv_stat_vtbl = {
    pv_fac_qi, pv_fac_addref, pv_fac_release, pv_fac_getiids, pv_fac_rcn, pv_fac_tl,
    pv_st_empty, pv_st_u8, pv_st_i16, pv_st_u16, pv_st_i32, pv_st_u32,
    pv_st_i64, pv_st_u64, pv_st_f32, pv_st_f64, pv_st_c16, pv_st_bool,
    pv_st_str, pv_st_insp, pv_st_guid
};
static WinrtPropertyValueFactory g_pv_factory = { &g_pv_act_vtbl, &g_pv_stat_vtbl };

HRESULT winrt_sys_get_property_value_factory(REFIID iid, void **out_factory) {
    return pv_fac_qi(&g_pv_factory.lpVtblAct, iid, out_factory);
}
