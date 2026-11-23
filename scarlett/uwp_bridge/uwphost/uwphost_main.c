/*
 * Proton-Scarlett — UWPHOST.dll Entry Point, Dispatch Chain & ExecuteAssembly
 *
 * Implements:
 *   - DllMain (loader-lock safe: zero LoadLibrary / file I/O inside DllMain)
 *   - UwpHostInitialize / UwpHostRun
 *   - DllGetActivationFactory / UwpHostTryGetActivationFactory dispatch chain:
 *       (a) uwphost core classes (CoreApplication, CoreWindow, CoreDispatcher, PropertySet)
 *       (b) uwp_winrt_ui_get_activation_factory()
 *       (c) uwp_winrt_sys_get_activation_factory()
 *       (d) AppxManifest.xml <Extension Category="windows.activatableClass.inProcessServer">
 *           DLLs via LoadLibraryW + DllGetActivationFactory (with self-forward guard!)
 *       (e) uwp_bridge_log_unknown_class (logs classId, IID, caller RVA, RCX/RDX/R8/R9,
 *           aborts under PROTON_XBOX_STRICT=1, else returns CLASS_E_CLASSNOTAVAILABLE)
 *   - ExecuteAssembly (directly imported by HedgePhysics.exe / UWPShim.exe!)
 *
 * Strict rule: Under _WIN32 (PE64), uses Win32 APIs ONLY (zero POSIX calls).
 *              ZERO silent return S_OK stubs.
 */

#include "uwphost_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Initialization Guard & In-Process Server Module Cache
 * ============================================================================ */

static volatile int32_t g_host_initialized = 0;
static uwp_mutex_t g_inproc_lock;
static int g_inproc_lock_ready = 0;

#define UWP_MAX_CACHED_MODS 32

typedef struct uwp_cached_mod_s {
    wchar_t path[UWP_MAX_PATH_CHARS];
    HMODULE module;
    HRESULT (UWP_API *pfn_get_factory)(UWP_HSTRING, UwpIActivationFactory **);
} uwp_cached_mod_t;

static uwp_cached_mod_t g_cached_mods[UWP_MAX_CACHED_MODS];
static uint32_t g_cached_mod_count = 0u;

#ifdef _WIN32
static HMODULE g_active_coreclr_mod = NULL;
static DWORD   g_active_coreclr_domain_id = 0u;
static HRESULT uwp_try_coreclr_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory);
#endif

/* Per-thread reentrancy guard (prevents infinite loops on self-forwarded EXEs like HedgePhysics.exe) */
#ifdef _WIN32
static DWORD g_reentry_tls = TLS_OUT_OF_INDEXES;
#else
static __thread int g_reentry_depth = 0;
#endif

static int uwp_reentry_enter(void) {
#ifdef _WIN32
    if (g_reentry_tls == TLS_OUT_OF_INDEXES) return 1;
    uintptr_t depth = (uintptr_t)TlsGetValue(g_reentry_tls);
    if (depth >= 8u) return 0;
    TlsSetValue(g_reentry_tls, (LPVOID)(depth + 1u));
    return 1;
#else
    if (g_reentry_depth >= 8) return 0;
    ++g_reentry_depth;
    return 1;
#endif
}

static void uwp_reentry_leave(void) {
#ifdef _WIN32
    if (g_reentry_tls == TLS_OUT_OF_INDEXES) return;
    uintptr_t depth = (uintptr_t)TlsGetValue(g_reentry_tls);
    if (depth > 0u) TlsSetValue(g_reentry_tls, (LPVOID)(depth - 1u));
#else
    if (g_reentry_depth > 0) --g_reentry_depth;
#endif
}

void uwp_host_ensure_initialized(void) {
    if (g_host_initialized) return;
    if (!g_inproc_lock_ready) {
        uwp_mutex_init(&g_inproc_lock);
        g_inproc_lock_ready = 1;
    }
#ifdef _WIN32
    if (g_reentry_tls == TLS_OUT_OF_INDEXES) {
        g_reentry_tls = TlsAlloc();
    }
#endif
    uwp_winstring_init();
    uwp_corewindow_subsystem_init();
    uwp_coreapp_subsystem_init();
    (void)uwp_core_get_package_identity();
    g_host_initialized = 1;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            /* Loader-lock safe init ONLY: no LoadLibrary or file I/O here */
            if (!g_inproc_lock_ready) {
                uwp_mutex_init(&g_inproc_lock);
                g_inproc_lock_ready = 1;
            }
            if (g_reentry_tls == TLS_OUT_OF_INDEXES) {
                g_reentry_tls = TlsAlloc();
            }
            uwp_corewindow_subsystem_init();
            uwp_coreapp_subsystem_init();
            break;

        case DLL_PROCESS_DETACH:
            if (lpvReserved == NULL) {
                uwp_coreapp_subsystem_shutdown();
                uwp_corewindow_subsystem_shutdown();
                if (g_inproc_lock_ready) {
                    uwp_mutex_destroy(&g_inproc_lock);
                    g_inproc_lock_ready = 0;
                }
                if (g_reentry_tls != TLS_OUT_OF_INDEXES) {
                    TlsFree(g_reentry_tls);
                    g_reentry_tls = TLS_OUT_OF_INDEXES;
                }
            }
            break;
        default:
            break;
    }
    return TRUE;
}
#endif

/* ============================================================================
 * UwpHostInitialize & UwpHostRun
 * ============================================================================ */

HRESULT UWP_API UwpHostInitialize(const uwp_host_config_t *cfg) {
    if (cfg && cfg->abi_version != 0u && cfg->abi_version != SCARLETT_UWP_BRIDGE_ABI_VERSION) {
        uwp_bridge_log("uwphost", "UwpHostInitialize ABI mismatch got=%u expected=%u",
                       (unsigned)cfg->abi_version, (unsigned)SCARLETT_UWP_BRIDGE_ABI_VERSION);
        return SCARLETT_ERR_ABI_VERSION_MISMATCH;
    }

    if (!g_inproc_lock_ready) {
        uwp_mutex_init(&g_inproc_lock);
        g_inproc_lock_ready = 1;
    }
#ifdef _WIN32
    if (g_reentry_tls == TLS_OUT_OF_INDEXES) {
        g_reentry_tls = TlsAlloc();
    }
#endif
    uwp_winstring_init();
    uwp_corewindow_subsystem_init();
    uwp_coreapp_subsystem_init();

    if (cfg) {
        if (cfg->default_width > 0u && cfg->default_height > 0u) {
            uwp_corewindow_set_default_size(cfg->default_width, cfg->default_height);
        }
        if (cfg->strict_mode != 0u) {
            uwp_log_set_strict_mode(1u);
        }
        uwp_manifest_initialize(cfg->manifest_path);
    } else {
        uwp_manifest_initialize(NULL);
    }

    g_host_initialized = 1;
    uwp_bridge_log("uwphost", "UwpHostInitialize complete (abi_version=%u)",
                   (unsigned)SCARLETT_UWP_BRIDGE_ABI_VERSION);
    return S_OK;
}

HRESULT UWP_API UwpHostRun(IUnknown *viewSource) {
    if (!viewSource) {
        return E_INVALIDARG;
    }
    uwp_host_ensure_initialized();
    UwpIFrameworkViewSource *vs = NULL;
    HRESULT hr = viewSource->lpVtbl->QueryInterface(viewSource, &UWP_IID_IFrameworkViewSource, (void **)&vs);
    if (FAILED(hr) || !vs) {
        uwp_bridge_log("uwphost", "UwpHostRun: viewSource does not implement IFrameworkViewSource (hr=0x%08X)",
                       (unsigned)hr);
        return FAILED(hr) ? hr : E_NOINTERFACE;
    }
    hr = uwp_coreapp_run_view_source(vs);
    vs->lpVtbl->Release(vs);
    return hr;
}

/* ============================================================================
 * PropertySet Activation Factory (Windows.Foundation.Collections.PropertySet)
 * ============================================================================ */

static HRESULT UWP_CALL psf_QI(UwpIActivationFactory *T, REFIID r, void **p) {
    if (!r || !p) return E_INVALIDARG;
    *p = NULL;
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) ||
        uwp_guid_equal(r, &UWP_IID_IInspectable) ||
        uwp_guid_equal(r, &UWP_IID_IAgileObject) ||
        uwp_guid_equal(r, &UWP_IID_IActivationFactory)) {
        *p = T;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG UWP_CALL psf_AddRef(UwpIActivationFactory *T) { (void)T; return 1u; }
static ULONG UWP_CALL psf_Release(UwpIActivationFactory *T) { (void)T; return 1u; }
static HRESULT UWP_CALL psf_GetIids(UwpIActivationFactory *T, ULONG *c, IID **i) {
    (void)T;
    const GUID list[1] = { UWP_IID_IActivationFactory };
    return uwp_alloc_iids(list, 1u, c, i);
}
static HRESULT UWP_CALL psf_GetName(UwpIActivationFactory *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Windows.Foundation.Collections.PropertySet", n);
}
static HRESULT UWP_CALL psf_GetTrust(UwpIActivationFactory *T, UwpTrustLevel *t) {
    (void)T; if (!t) return E_INVALIDARG; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL psf_ActivateInstance(UwpIActivationFactory *T, UwpIInspectable **inst) {
    (void)T;
    if (!inst) return E_INVALIDARG;
    UwpIMap *ps = uwp_create_property_set();
    if (!ps) {
        *inst = NULL;
        return E_OUTOFMEMORY;
    }
    *inst = (UwpIInspectable *)ps;
    return S_OK;
}

static const UwpIActivationFactoryVtbl g_psf_vtbl = {
    psf_QI, psf_AddRef, psf_Release,
    psf_GetIids, psf_GetName, psf_GetTrust,
    psf_ActivateInstance
};
static UwpIActivationFactory g_propset_factory = { &g_psf_vtbl };

/* ============================================================================
 * Step (d): AppxManifest InProcessServer DLL Loader & Factory Forwarder
 * ============================================================================ */

static int uwp_is_abs_path(const wchar_t *p) {
    if (!p || p[0] == L'\0') return 0;
    if (p[0] == L'\\' || p[0] == L'/') return 1;
    if (((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) && p[1] == L':') {
        return 1;
    }
    return 0;
}

static HRESULT uwp_try_inproc_server_factory(UWP_HSTRING class_id,
                                             const wchar_t *class_name,
                                             REFIID iid,
                                             void **out_factory) {
    uwp_inproc_class_entry_t entry;
    if (!uwp_manifest_find_inproc_class(class_name, &entry)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    char cls_u8[UWP_MAX_IDENTITY_CHARS];
    char dll_u8[UWP_MAX_PATH_CHARS];
    uwp_wstr_to_utf8(class_name, cls_u8, sizeof(cls_u8));
    uwp_wstr_to_utf8(entry.dll_path, dll_u8, sizeof(dll_u8));

#ifdef _WIN32
    wchar_t full_path[UWP_MAX_PATH_CHARS];
    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    if (!uwp_is_abs_path(entry.dll_path) && id && id->install_path[0] != L'\0') {
        size_t ilen = wcslen(id->install_path);
        size_t dlen = wcslen(entry.dll_path);
        if (ilen + 1 + dlen < UWP_MAX_PATH_CHARS) {
            wcscpy(full_path, id->install_path);
            if (ilen > 0 && full_path[ilen - 1] != L'\\' && full_path[ilen - 1] != L'/') {
                wcscat(full_path, L"\\");
            }
            wcscat(full_path, entry.dll_path);
        } else {
            wcsncpy(full_path, entry.dll_path, UWP_MAX_PATH_CHARS - 1);
            full_path[UWP_MAX_PATH_CHARS - 1] = L'\0';
        }
    } else {
        wcsncpy(full_path, entry.dll_path, UWP_MAX_PATH_CHARS - 1);
        full_path[UWP_MAX_PATH_CHARS - 1] = L'\0';
    }

    HRESULT (UWP_API *pfn)(UWP_HSTRING, UwpIActivationFactory **) = NULL;

    uwp_mutex_lock(&g_inproc_lock);
    for (uint32_t i = 0; i < g_cached_mod_count; ++i) {
        if (wcscmp(g_cached_mods[i].path, full_path) == 0) {
            pfn = g_cached_mods[i].pfn_get_factory;
            break;
        }
    }
    uwp_mutex_unlock(&g_inproc_lock);

    if (!pfn) {
        HMODULE mod = GetModuleHandleW(full_path);
        if (!mod) {
            mod = LoadLibraryW(full_path);
        }
        if (!mod && !uwp_is_abs_path(entry.dll_path)) {
            mod = LoadLibraryW(entry.dll_path);
        }
        if (!mod) {
            uwp_bridge_log("inproc", "Failed to LoadLibraryW('%s') for class '%s' err=%lu",
                           dll_u8, cls_u8, (unsigned long)GetLastError());
            return CLASS_E_CLASSNOTAVAILABLE;
        }

        void *raw_pfn = (void *)GetProcAddress(mod, "DllGetActivationFactory");
        if (!raw_pfn) {
            uwp_bridge_log("inproc", "Module '%s' has no DllGetActivationFactory for class '%s'",
                           dll_u8, cls_u8);
            return CLASS_E_CLASSNOTAVAILABLE;
        }

        /*
         * CRITICAL SELF-FORWARD GUARD:
         * Managed UWP shims such as HedgePhysics.exe (UWPShim.exe) export
         * DllGetActivationFactory forwarded directly to uwphost.DllGetActivationFactory!
         * Invoking it here would infinitely recurse and overflow the stack.
         */
        if (raw_pfn == (void *)&DllGetActivationFactory) {
            HRESULT hr_cf = uwp_coreapp_try_custom_factory(class_id, iid, out_factory);
            if (hr_cf != CLASS_E_CLASSNOTAVAILABLE && hr_cf != REGDB_E_CLASSNOTREG) {
                return hr_cf;
            }
            hr_cf = uwp_try_coreclr_activation_factory(class_name, iid, out_factory);
            if (hr_cf != CLASS_E_CLASSNOTAVAILABLE && hr_cf != REGDB_E_CLASSNOTREG) {
                uwp_bridge_log("inproc", "Activated managed class '%s' via CoreCLR -> hr=0x%08X",
                               cls_u8, (unsigned)hr_cf);
                return hr_cf;
            }
            uwp_bridge_log("inproc",
                           "Class '%s' in '%s' self-forwards to uwphost.DllGetActivationFactory "
                           "(managed CLR WinRT class; custom/CoreCLR activation unavailable)",
                           cls_u8, dll_u8);
            return CLASS_E_CLASSNOTAVAILABLE;
        }

        pfn = (HRESULT (UWP_API *)(UWP_HSTRING, UwpIActivationFactory **))raw_pfn;

        uwp_mutex_lock(&g_inproc_lock);
        if (g_cached_mod_count < UWP_MAX_CACHED_MODS) {
            uwp_cached_mod_t *slot = &g_cached_mods[g_cached_mod_count++];
            wcsncpy(slot->path, full_path, UWP_MAX_PATH_CHARS - 1);
            slot->path[UWP_MAX_PATH_CHARS - 1] = L'\0';
            slot->module = mod;
            slot->pfn_get_factory = pfn;
        }
        uwp_mutex_unlock(&g_inproc_lock);
    }

    if (!uwp_reentry_enter()) {
        uwp_bridge_log("inproc", "Reentrancy limit hit activating '%s' from '%s'", cls_u8, dll_u8);
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    UwpIActivationFactory *af = NULL;
    HRESULT hr = pfn(class_id, &af);
    uwp_reentry_leave();

    if (FAILED(hr) || !af) {
        uwp_bridge_log("inproc", "InProcessServer '%s'::DllGetActivationFactory('%s') failed hr=0x%08X",
                       dll_u8, cls_u8, (unsigned)hr);
        return FAILED(hr) ? hr : CLASS_E_CLASSNOTAVAILABLE;
    }

    hr = af->lpVtbl->QueryInterface(af, iid, out_factory);
    af->lpVtbl->Release(af);
    uwp_bridge_log("inproc", "Activated class '%s' via '%s' -> hr=0x%08X", cls_u8, dll_u8, (unsigned)hr);
    return hr;
#else
    (void)class_id; (void)iid; (void)out_factory; (void)uwp_is_abs_path;
    (void)g_cached_mods; (void)g_cached_mod_count;
    (void)uwp_reentry_enter; (void)uwp_reentry_leave;
    uwp_bridge_log("inproc", "Host test: matched manifest inProcessServer class='%s' -> dll='%s'",
                   cls_u8, dll_u8);
    return uwp_coreapp_try_custom_factory(class_id, iid, out_factory);
#endif
}

/* ============================================================================
 * Unified Dispatch Chain: UwpHostTryGetActivationFactory & DllGetActivationFactory
 * ============================================================================ */

static int uwp_caller_is_combase(void *caller_ret_addr) {
#ifdef _WIN32
    if (!caller_ret_addr) return 0;
    HMODULE h_cb = GetModuleHandleA("combase.dll");
    if (!h_cb) return 0;
    HMODULE h_caller = NULL;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)caller_ret_addr,
            &h_caller)) {
        return h_caller == h_cb;
    }
    return 0;
#else
    (void)caller_ret_addr;
    return 0;
#endif
}

static HRESULT uwp_dispatch_activation_factory(
    UWP_HSTRING classId,
    REFIID iid,
    void **factory,
    void *caller_ret_addr,
    int log_unknown_on_miss) {
    if (!classId || !iid || !factory) {
        return E_INVALIDARG;
    }
    *factory = NULL;
    uwp_host_ensure_initialized();

    const wchar_t *class_name = WindowsGetStringRawBuffer(classId, NULL);
    if (!class_name || class_name[0] == L'\0') {
        return E_INVALIDARG;
    }

    /* (a) uwphost core classes */
    if (wcscmp(class_name, L"Windows.ApplicationModel.Core.CoreApplication") == 0) {
        return uwp_coreapp_get_activation_factory(iid, factory);
    }
    if (wcscmp(class_name, L"Windows.UI.Core.CoreWindow") == 0) {
        return uwp_corewindow_get_activation_factory(iid, factory);
    }
    if (wcscmp(class_name, L"Windows.UI.Core.CoreDispatcher") == 0) {
        return uwp_coredispatcher_get_activation_factory(iid, factory);
    }
    if (wcscmp(class_name, L"Windows.Foundation.Collections.PropertySet") == 0 ||
        wcscmp(class_name, L"Windows.Foundation.Collections.ValueSet") == 0) {
        return psf_QI(&g_propset_factory, iid, factory);
    }

    /* (b) winrt_ui sub-module */
    HRESULT hr = uwp_winrt_ui_get_activation_factory(class_name, iid, factory);
    if (hr != CLASS_E_CLASSNOTAVAILABLE && hr != REGDB_E_CLASSNOTREG) {
        return hr;
    }

    /* (c) winrt_sys sub-module */
    hr = uwp_winrt_sys_get_activation_factory(class_name, iid, factory);
    if (hr != CLASS_E_CLASSNOTAVAILABLE && hr != REGDB_E_CLASSNOTREG) {
        return hr;
    }

    /* (d) AppxManifest.xml inProcessServer DLLs via LoadLibraryW + DllGetActivationFactory */
    hr = uwp_try_inproc_server_factory(classId, class_name, iid, factory);
    if (hr != CLASS_E_CLASSNOTAVAILABLE && hr != REGDB_E_CLASSNOTREG) {
        return hr;
    }

    /* (e) Unknown class diagnostic (and strict abort if PROTON_XBOX_STRICT=1) */
    if (log_unknown_on_miss) {
        uwp_bridge_log_unknown_class(
            class_name,
            iid,
            caller_ret_addr,
            (uint64_t)(uintptr_t)classId,
            (uint64_t)(uintptr_t)iid,
            (uint64_t)(uintptr_t)factory,
            0u);
    } else {
        char cls_u8[UWP_MAX_IDENTITY_CHARS];
        uwp_wstr_to_utf8(class_name, cls_u8, sizeof(cls_u8));
        uwp_bridge_log("dispatch",
                       "Class '%s' not in uwphost/winrt_ui/winrt_sys/manifest; deferring to combase registry",
                       cls_u8);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT UWP_API UwpHostTryGetActivationFactory(UWP_HSTRING classId, REFIID iid, void **factory) {
    void *ret = NULL;
#if defined(__GNUC__) || defined(__clang__)
    ret = __builtin_return_address(0);
#endif
    /*
     * When called by Wine's combase.dll!RoGetActivationFactory, combase will
     * fall back to Wine's registry lookup on CLASS_E_CLASSNOTAVAILABLE and
     * invoke UwpHostLogUnknownClass if the registry lookup ALSO fails.
     * For all direct callers, log unknown class immediately in step (e).
     */
    int log_now = uwp_caller_is_combase(ret) ? 0 : 1;
    return uwp_dispatch_activation_factory(classId, iid, factory, ret, log_now);
}

HRESULT UWP_API DllGetActivationFactory(UWP_HSTRING activatableClassId,
                                        UwpIActivationFactory **factory) {
    void *ret = NULL;
#if defined(__GNUC__) || defined(__clang__)
    ret = __builtin_return_address(0);
#endif
    return uwp_dispatch_activation_factory(
        activatableClassId,
        &UWP_IID_IActivationFactory,
        (void **)factory,
        ret,
        1);
}

/* ============================================================================
 * ExecuteAssembly — Manifest-Driven Bootstrap for UWPShim.exe (HedgePhysics.exe)
 *
 * Verified ABI from HedgePhysics.exe (UWPShim.exe @ 0x14000109f-0x1400010b6):
 *   RCX: LPCWSTR assemblyPath   (e.g. "<pkg_dir>\entrypoint\HedgePhysics.exe")
 *   RDX: int argc               (remaining argument count)
 *   R8 : LPCWSTR *argv          (argument vector starting at orig_argv[1])
 *   R9 : int *pExitCode         (receives process exit code returned by wmain)
 * ============================================================================ */

#ifdef _WIN32
typedef int (*pfn_coreclr_initialize)(
    const char *exePath,
    const char *appDomainFriendlyName,
    int propertyCount,
    const char **propertyKeys,
    const char **propertyValues,
    void **hostHandle,
    unsigned int *domainId);

typedef int (*pfn_coreclr_execute_assembly)(
    void *hostHandle,
    unsigned int domainId,
    int argc,
    const char **argv,
    const char *managedAssemblyPath,
    unsigned int *exitCode);

typedef int (*pfn_coreclr_shutdown)(void *hostHandle, unsigned int domainId);

typedef HRESULT (WINAPI *pfn_GetCLRRuntimeHost)(REFIID riid, void **ppUnk);

/* Standard CoreCLR ICLRRuntimeHost2 ({712AB73F-2C22-4807-AD7E-F501D7B72C2D}) */
static const GUID SCARLETT_IID_ICLRRuntimeHost2 = {
    0x712AB73Fu, 0x2C22u, 0x4807u, { 0xADu, 0x7Eu, 0xF5u, 0x01u, 0xD7u, 0xB7u, 0x2Cu, 0x2Du }
};

typedef struct ICLRRuntimeHost2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Start)(void *This);
    HRESULT (STDMETHODCALLTYPE *Stop)(void *This);
    HRESULT (STDMETHODCALLTYPE *SetHostControl)(void *This, void *pHostControl);
    HRESULT (STDMETHODCALLTYPE *GetCLRControl)(void *This, void **pCLRControl);
    HRESULT (STDMETHODCALLTYPE *UnloadAppDomain)(void *This, DWORD dwAppDomainId, BOOL fWaitUntilDone);
    HRESULT (STDMETHODCALLTYPE *ExecuteInAppDomain)(void *This, DWORD dwAppDomainId, void *pCallback, void *cookie);
    HRESULT (STDMETHODCALLTYPE *GetCurrentAppDomainId)(void *This, DWORD *pdwAppDomainId);
    HRESULT (STDMETHODCALLTYPE *ExecuteApplication)(void *This, LPCWSTR pwzAppFullName, DWORD dwManifestPaths, LPCWSTR *ppwzManifestPaths, DWORD dwActivationData, LPCWSTR *ppwzActivationData, int *pReturnValue);
    HRESULT (STDMETHODCALLTYPE *ExecuteInDefaultAppDomain)(void *This, LPCWSTR pwzAssemblyPath, LPCWSTR pwzTypeName, LPCWSTR pwzMethodName, LPCWSTR pwzArgument, DWORD *pReturnValue);
    HRESULT (STDMETHODCALLTYPE *CreateAppDomainWithManager)(void *This, LPCWSTR wszFriendlyName, DWORD dwFlags, LPCWSTR wszAppDomainManagerAssemblyName, LPCWSTR wszAppDomainManagerTypeName, int nProperties, LPCWSTR *pPropertyNames, LPCWSTR *pPropertyValues, DWORD *pAppDomainID);
    HRESULT (STDMETHODCALLTYPE *CreateDelegate)(void *This, DWORD appDomainID, LPCWSTR wszAssemblyName, LPCWSTR wszClassName, LPCWSTR wszMethodName, INT_PTR *fnPtr);
    HRESULT (STDMETHODCALLTYPE *Authenticate)(void *This, ULONGLONG authKey);
    HRESULT (STDMETHODCALLTYPE *RegisterMacEHPort)(void *This);
    HRESULT (STDMETHODCALLTYPE *SetStartupFlags)(void *This, DWORD dwFlags);
    HRESULT (STDMETHODCALLTYPE *DllGetActivationFactory)(void *This, DWORD appDomainID, LPCWSTR wszTypeName, void **factory);
    HRESULT (STDMETHODCALLTYPE *ExecuteAssembly)(void *This, DWORD appDomainID, LPCWSTR pwzAssemblyPath, int argc, LPCWSTR *argv, DWORD *pReturnValue);
} ICLRRuntimeHost2Vtbl;

typedef struct ICLRRuntimeHost2 {
    const ICLRRuntimeHost2Vtbl *lpVtbl;
} ICLRRuntimeHost2;

static HRESULT uwp_try_coreclr_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory) {
    if (!class_name || !iid || !out_factory) return E_INVALIDARG;
    *out_factory = NULL;

    uwp_mutex_lock(&g_inproc_lock);
    HMODULE h_clr = g_active_coreclr_mod;
    DWORD dom_id = g_active_coreclr_domain_id;
    uwp_mutex_unlock(&g_inproc_lock);

    if (!h_clr || dom_id == 0u) return CLASS_E_CLASSNOTAVAILABLE;

    pfn_GetCLRRuntimeHost p_get_host =
        (pfn_GetCLRRuntimeHost)(void *)GetProcAddress(h_clr, "GetCLRRuntimeHost");
    if (!p_get_host) return CLASS_E_CLASSNOTAVAILABLE;

    if (!uwp_reentry_enter()) return CLASS_E_CLASSNOTAVAILABLE;

    ICLRRuntimeHost2 *host2 = NULL;
    HRESULT hr = p_get_host(&SCARLETT_IID_ICLRRuntimeHost2, (void **)&host2);
    if (SUCCEEDED(hr) && host2) {
        void *af_raw = NULL;
        hr = host2->lpVtbl->DllGetActivationFactory(host2, dom_id, class_name, &af_raw);
        host2->lpVtbl->Release(host2);
        if (SUCCEEDED(hr) && af_raw) {
            UwpIActivationFactory *af = (UwpIActivationFactory *)af_raw;
            hr = af->lpVtbl->QueryInterface(af, iid, out_factory);
            af->lpVtbl->Release(af);
            uwp_reentry_leave();
            return hr;
        }
    }

    uwp_reentry_leave();
    return CLASS_E_CLASSNOTAVAILABLE;
}

typedef struct uwp_cor20_header_s {
    uint32_t cb;
    uint16_t major_runtime_version;
    uint16_t minor_runtime_version;
    uint32_t metadata_rva;
    uint32_t metadata_size;
    uint32_t flags;
    uint32_t entry_point_token;
} uwp_cor20_header_t;

/*
 * Inspect a PE binary for an IMAGE_COR20_HEADER (DataDirectory[14]).
 * Returns 1 if managed (CLR header present), 0 if native-only, -1 on I/O error.
 */
static int uwp_pe_inspect_clr(const wchar_t *path,
                               uint16_t *out_magic,
                               uint16_t *out_maj,
                               uint16_t *out_min,
                               uint32_t *out_flags,
                               uint32_t *out_ep_token) {
    if (out_magic) *out_magic = 0;
    if (out_maj) *out_maj = 0;
    if (out_min) *out_min = 0;
    if (out_flags) *out_flags = 0;
    if (out_ep_token) *out_ep_token = 0;
    if (!path || !path[0]) return -1;

    HANDLE hf = CreateFileW(path, GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return -1;

    IMAGE_DOS_HEADER dos;
    DWORD rd = 0;
    if (!ReadFile(hf, &dos, sizeof(dos), &rd, NULL) || rd != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < (LONG)sizeof(dos)) {
        CloseHandle(hf);
        return -1;
    }

    if (SetFilePointer(hf, dos.e_lfanew, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        CloseHandle(hf);
        return -1;
    }

    DWORD pe_sig = 0;
    IMAGE_FILE_HEADER fh;
    if (!ReadFile(hf, &pe_sig, sizeof(pe_sig), &rd, NULL) || rd != sizeof(pe_sig) ||
        pe_sig != IMAGE_NT_SIGNATURE ||
        !ReadFile(hf, &fh, sizeof(fh), &rd, NULL) || rd != sizeof(fh)) {
        CloseHandle(hf);
        return -1;
    }

    uint8_t opt_buf[256];
    DWORD opt_to_read = fh.SizeOfOptionalHeader;
    if (opt_to_read > sizeof(opt_buf)) opt_to_read = sizeof(opt_buf);
    if (opt_to_read < 2 || !ReadFile(hf, opt_buf, opt_to_read, &rd, NULL) || rd != opt_to_read) {
        CloseHandle(hf);
        return -1;
    }

    uint16_t magic = 0;
    memcpy(&magic, opt_buf, sizeof(magic));
    if (out_magic) *out_magic = magic;

    DWORD dd_off = (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) ? 112u :
                   (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) ? 96u : 0u;
    if (dd_off == 0u || opt_to_read < dd_off + 15u * sizeof(IMAGE_DATA_DIRECTORY)) {
        CloseHandle(hf);
        return 0;
    }

    IMAGE_DATA_DIRECTORY com_dd;
    memcpy(&com_dd, opt_buf + dd_off + 14u * sizeof(IMAGE_DATA_DIRECTORY), sizeof(com_dd));
    if (com_dd.VirtualAddress == 0u || com_dd.Size < sizeof(uwp_cor20_header_t)) {
        CloseHandle(hf);
        return 0;
    }

    /* Map com_dd.VirtualAddress to file offset via section table */
    LONG sec_table_pos = dos.e_lfanew + 4 + (LONG)sizeof(IMAGE_FILE_HEADER) + (LONG)fh.SizeOfOptionalHeader;
    if (SetFilePointer(hf, sec_table_pos, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        CloseHandle(hf);
        return 1;
    }

    DWORD cor_off = 0u;
    WORD nsec = fh.NumberOfSections;
    if (nsec > 96u) nsec = 96u;
    for (WORD i = 0; i < nsec; ++i) {
        IMAGE_SECTION_HEADER sh;
        if (!ReadFile(hf, &sh, sizeof(sh), &rd, NULL) || rd != sizeof(sh)) break;
        DWORD span = (sh.Misc.VirtualSize > sh.SizeOfRawData) ? sh.Misc.VirtualSize : sh.SizeOfRawData;
        if (com_dd.VirtualAddress >= sh.VirtualAddress &&
            com_dd.VirtualAddress < sh.VirtualAddress + span) {
            cor_off = sh.PointerToRawData + (com_dd.VirtualAddress - sh.VirtualAddress);
            break;
        }
    }

    if (cor_off != 0u &&
        SetFilePointer(hf, (LONG)cor_off, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
        uwp_cor20_header_t cor;
        if (ReadFile(hf, &cor, sizeof(cor), &rd, NULL) && rd == sizeof(cor)) {
            if (out_maj) *out_maj = cor.major_runtime_version;
            if (out_min) *out_min = cor.minor_runtime_version;
            if (out_flags) *out_flags = cor.flags;
            if (out_ep_token) *out_ep_token = cor.entry_point_token;
        }
    }

    CloseHandle(hf);
    return 1;
}

#define UWP_TPA_MAX_SEEN 512

typedef struct uwp_tpa_seen_s {
    wchar_t names[UWP_TPA_MAX_SEEN][128];
    uint32_t count;
} uwp_tpa_seen_t;

static void uwp_extract_asm_simple_name(const wchar_t *file_name, wchar_t *out, size_t out_chars) {
    out[0] = L'\0';
    if (!file_name) return;
    const wchar_t *base = file_name;
    for (const wchar_t *p = file_name; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    wcsncpy(out, base, out_chars - 1);
    out[out_chars - 1] = L'\0';

    size_t len = wcslen(out);
    if (len > 7 && _wcsicmp(out + len - 7, L".ni.dll") == 0) {
        out[len - 7] = L'\0';
    } else if (len > 4 && (_wcsicmp(out + len - 4, L".dll") == 0 ||
                           _wcsicmp(out + len - 4, L".exe") == 0)) {
        out[len - 4] = L'\0';
    } else if (len > 6 && _wcsicmp(out + len - 6, L".winmd") == 0) {
        out[len - 6] = L'\0';
    }
}

static int uwp_tpa_seen_add(uwp_tpa_seen_t *seen, const wchar_t *file_name) {
    wchar_t simple[128];
    uwp_extract_asm_simple_name(file_name, simple, 128);
    if (!simple[0]) return 0;
    for (uint32_t i = 0; i < seen->count; ++i) {
        if (_wcsicmp(seen->names[i], simple) == 0) {
            return 0; /* duplicate */
        }
    }
    if (seen->count < UWP_TPA_MAX_SEEN) {
        wcsncpy(seen->names[seen->count], simple, 127);
        seen->names[seen->count][127] = L'\0';
        seen->count++;
    }
    return 1;
}

static void uwp_append_wstr(wchar_t **buf, size_t *len, size_t *cap, const wchar_t *s) {
    if (!s || !s[0]) return;
    size_t slen = wcslen(s);
    if (*len + slen + 2 > *cap) {
        size_t ncap = (*cap == 0) ? 8192 : (*cap * 2);
        while (ncap < *len + slen + 2) ncap *= 2;
        wchar_t *nbuf = (wchar_t *)realloc(*buf, ncap * sizeof(wchar_t));
        if (!nbuf) return;
        *buf = nbuf;
        *cap = ncap;
    }
    memcpy(*buf + *len, s, slen * sizeof(wchar_t));
    *len += slen;
    (*buf)[*len] = L'\0';
}

static void uwp_append_str(char **buf, size_t *len, size_t *cap, const char *s) {
    if (!s || !s[0]) return;
    size_t slen = strlen(s);
    if (*len + slen + 2 > *cap) {
        size_t ncap = (*cap == 0) ? 8192 : (*cap * 2);
        while (ncap < *len + slen + 2) ncap *= 2;
        char *nbuf = (char *)realloc(*buf, ncap);
        if (!nbuf) return;
        *buf = nbuf;
        *cap = ncap;
    }
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

/*
 * Collect managed assemblies from `dir_w` matching `pattern` into both
 * UTF-8 and UTF-16 TPA lists, skipping native-only binaries and duplicates.
 */
static void uwp_collect_tpa_dir_both(const wchar_t *dir_w,
                                     const wchar_t *pattern,
                                     uwp_tpa_seen_t *seen,
                                     char **tpa_u8, size_t *len_u8, size_t *cap_u8,
                                     wchar_t **tpa_w, size_t *len_w, size_t *cap_w) {
    if (!dir_w || !dir_w[0]) return;
    wchar_t search_w[UWP_MAX_PATH_CHARS];
    if (wcslen(dir_w) + wcslen(pattern) + 4 >= UWP_MAX_PATH_CHARS) return;
    wcscpy(search_w, dir_w);
    size_t dlen = wcslen(search_w);
    if (dlen > 0 && search_w[dlen - 1] != L'\\' && search_w[dlen - 1] != L'/') {
        wcscat(search_w, L"\\");
    }
    wcscat(search_w, pattern);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_w, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        wchar_t full_w[UWP_MAX_PATH_CHARS];
        wcscpy(full_w, dir_w);
        size_t fl = wcslen(full_w);
        if (fl > 0 && full_w[fl - 1] != L'\\' && full_w[fl - 1] != L'/') {
            wcscat(full_w, L"\\");
        }
        if (wcslen(full_w) + wcslen(fd.cFileName) + 1 >= UWP_MAX_PATH_CHARS) continue;
        wcscat(full_w, fd.cFileName);

        /* Skip native-only DLLs/EXEs (only include managed CLR assemblies) */
        if (uwp_pe_inspect_clr(full_w, NULL, NULL, NULL, NULL, NULL) != 1) {
            continue;
        }
        /* Deduplicate by assembly simple name */
        if (!uwp_tpa_seen_add(seen, fd.cFileName)) {
            continue;
        }

        if (*len_w > 0) uwp_append_wstr(tpa_w, len_w, cap_w, L";");
        uwp_append_wstr(tpa_w, len_w, cap_w, full_w);

        char full_u8[UWP_MAX_PATH_CHARS * 2];
        uwp_wstr_to_utf8(full_w, full_u8, sizeof(full_u8));
        if (*len_u8 > 0) uwp_append_str(tpa_u8, len_u8, cap_u8, ";");
        uwp_append_str(tpa_u8, len_u8, cap_u8, full_u8);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

static void uwp_log_coreclr_props(const char *const *keys,
                                  const char *const *vals,
                                  int count,
                                  uint32_t tpa_count) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(keys[i], "TRUSTED_PLATFORM_ASSEMBLIES") == 0) {
            size_t len = vals[i] ? strlen(vals[i]) : 0;
            uwp_bridge_log("execute_assembly",
                           "  prop[%d] %s = <%u assemblies, %u bytes>",
                           i, keys[i], (unsigned)tpa_count, (unsigned)len);
            /* Log TPA entries in bounded chunks so nothing is truncated */
            if (vals[i] && len > 0) {
                const char *p = vals[i];
                size_t chunk_idx = 0;
                while (*p) {
                    char chunk[420];
                    size_t avail = strlen(p);
                    size_t take = avail;
                    if (take > sizeof(chunk) - 1) {
                        take = sizeof(chunk) - 1;
                        while (take > 0 && p[take] != ';') --take;
                        if (take == 0) take = sizeof(chunk) - 1;
                    }
                    memcpy(chunk, p, take);
                    chunk[take] = '\0';
                    uwp_bridge_log("execute_assembly", "    TPA[%u]: %s",
                                   (unsigned)chunk_idx++, chunk);
                    p += take;
                    if (*p == ';') ++p;
                }
            }
        } else {
            uwp_bridge_log("execute_assembly", "  prop[%d] %s = '%s'",
                           i, keys[i], vals[i] ? vals[i] : "");
        }
    }
}

static int uwp_try_coreclr_execute(const wchar_t *assembly_path,
                                   const wchar_t *pkg_dir,
                                   int argc,
                                   LPCWSTR *argv,
                                   int *out_exit_code) {
    wchar_t clr_candidates[3][UWP_MAX_PATH_CHARS];
    int cand_count = 0;

    wchar_t raw_clr[UWP_MAX_PATH_CHARS];
    if (uwp_get_env_wstr(L"PROTON_UWP_CORECLR_PATH", raw_clr, UWP_MAX_PATH_CHARS)) {
        uwp_manifest_normalize_path(raw_clr, clr_candidates[cand_count], UWP_MAX_PATH_CHARS);
        ++cand_count;
    }
    if (pkg_dir && pkg_dir[0] != L'\0' && wcslen(pkg_dir) + 16 < UWP_MAX_PATH_CHARS) {
        wcscpy(clr_candidates[cand_count], pkg_dir);
        wcscat(clr_candidates[cand_count], L"\\coreclr.dll");
        ++cand_count;
    }
    wcscpy(clr_candidates[cand_count++], L"coreclr.dll");

    HMODULE h_clr = NULL;
    char loaded_clr_u8[UWP_MAX_PATH_CHARS] = { 0 };
    for (int i = 0; i < cand_count; ++i) {
        h_clr = LoadLibraryExW(clr_candidates[i], NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h_clr) {
            h_clr = LoadLibraryW(clr_candidates[i]);
        }
        if (h_clr) {
            uwp_wstr_to_utf8(clr_candidates[i], loaded_clr_u8, sizeof(loaded_clr_u8));
            break;
        }
    }
    if (!h_clr) {
        return 0;
    }

    /* Inspect CLR header of the target assembly */
    uint16_t pe_magic = 0, clr_maj = 0, clr_min = 0;
    uint32_t clr_flags = 0, clr_ep = 0;
    int is_managed = uwp_pe_inspect_clr(assembly_path, &pe_magic, &clr_maj, &clr_min, &clr_flags, &clr_ep);
    uwp_bridge_log("execute_assembly",
                   "Target PE CLR header: managed=%d magic=0x%04X clr=v%u.%u flags=0x%08X entry_token=0x%08X",
                   is_managed, (unsigned)pe_magic, (unsigned)clr_maj, (unsigned)clr_min,
                   (unsigned)clr_flags, (unsigned)clr_ep);

    wchar_t clr_dir_w[UWP_MAX_PATH_CHARS] = { 0 };
    if (GetModuleFileNameW(h_clr, clr_dir_w, UWP_MAX_PATH_CHARS) > 0) {
        wchar_t *sep = wcsrchr(clr_dir_w, L'\\');
        if (!sep) sep = wcsrchr(clr_dir_w, L'/');
        if (sep) *sep = L'\0';
    }

    wchar_t entry_dir_w[UWP_MAX_PATH_CHARS] = { 0 };
    if (assembly_path) {
        wcsncpy(entry_dir_w, assembly_path, UWP_MAX_PATH_CHARS - 1);
        wchar_t *sep = wcsrchr(entry_dir_w, L'\\');
        if (!sep) sep = wcsrchr(entry_dir_w, L'/');
        if (sep) *sep = L'\0';
    }

    wchar_t winmd_dir_w[UWP_MAX_PATH_CHARS] = { 0 };
    if (pkg_dir && pkg_dir[0] && wcslen(pkg_dir) + 16 < UWP_MAX_PATH_CHARS) {
        wcscpy(winmd_dir_w, pkg_dir);
        wcscat(winmd_dir_w, L"\\WinMetadata");
        DWORD attr = GetFileAttributesW(winmd_dir_w);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            wcsncpy(winmd_dir_w, pkg_dir, UWP_MAX_PATH_CHARS - 1);
        }
    }

    uwp_tpa_seen_t seen;
    memset(&seen, 0, sizeof(seen));
    char *tpa_u8 = NULL;
    size_t tpa_u8_len = 0, tpa_u8_cap = 0;
    wchar_t *tpa_w = NULL;
    size_t tpa_w_len = 0, tpa_w_cap = 0;

    /* Priority order: CoreCLR runtime dir -> entrypoint dir -> package root DLLs (no root native EXEs) */
    uwp_collect_tpa_dir_both(clr_dir_w, L"*.dll", &seen, &tpa_u8, &tpa_u8_len, &tpa_u8_cap, &tpa_w, &tpa_w_len, &tpa_w_cap);
    uwp_collect_tpa_dir_both(entry_dir_w, L"*.exe", &seen, &tpa_u8, &tpa_u8_len, &tpa_u8_cap, &tpa_w, &tpa_w_len, &tpa_w_cap);
    uwp_collect_tpa_dir_both(entry_dir_w, L"*.dll", &seen, &tpa_u8, &tpa_u8_len, &tpa_u8_cap, &tpa_w, &tpa_w_len, &tpa_w_cap);
    uwp_collect_tpa_dir_both(pkg_dir, L"*.dll", &seen, &tpa_u8, &tpa_u8_len, &tpa_u8_cap, &tpa_w, &tpa_w_len, &tpa_w_cap);

    char asm_u8[UWP_MAX_PATH_CHARS * 2];
    char pkg_dir_u8[UWP_MAX_PATH_CHARS * 2];
    char clr_dir_u8[UWP_MAX_PATH_CHARS * 2];
    char entry_dir_u8[UWP_MAX_PATH_CHARS * 2];
    char winmd_dir_u8[UWP_MAX_PATH_CHARS * 2];
    uwp_wstr_to_utf8(assembly_path, asm_u8, sizeof(asm_u8));
    uwp_wstr_to_utf8(pkg_dir ? pkg_dir : L".", pkg_dir_u8, sizeof(pkg_dir_u8));
    uwp_wstr_to_utf8(clr_dir_w[0] ? clr_dir_w : L".", clr_dir_u8, sizeof(clr_dir_u8));
    uwp_wstr_to_utf8(entry_dir_w[0] ? entry_dir_w : L".", entry_dir_u8, sizeof(entry_dir_u8));
    uwp_wstr_to_utf8(winmd_dir_w[0] ? winmd_dir_w : (pkg_dir ? pkg_dir : L"."), winmd_dir_u8, sizeof(winmd_dir_u8));

    char app_paths_u8[UWP_MAX_PATH_CHARS * 6];
    char ni_paths_u8[UWP_MAX_PATH_CHARS * 6];
    char res_roots_u8[UWP_MAX_PATH_CHARS * 6];
    snprintf(app_paths_u8, sizeof(app_paths_u8), "%s;%s;%s", entry_dir_u8, pkg_dir_u8, clr_dir_u8);
    snprintf(ni_paths_u8, sizeof(ni_paths_u8), "%s;%s;%s", clr_dir_u8, pkg_dir_u8, entry_dir_u8);
    snprintf(res_roots_u8, sizeof(res_roots_u8), "%s;%s;%s", pkg_dir_u8, entry_dir_u8, clr_dir_u8);

    const char *keys[7] = {
        "TRUSTED_PLATFORM_ASSEMBLIES",
        "APP_PATHS",
        "APP_NI_PATHS",
        "NATIVE_DLL_SEARCH_DIRECTORIES",
        "PLATFORM_RESOURCE_ROOTS",
        "APP_LOCAL_WINMETADATA",
        "AppDomainCompatSwitch"
    };
    const char *vals[7] = {
        tpa_u8 ? tpa_u8 : "",
        app_paths_u8,
        ni_paths_u8,
        app_paths_u8,
        res_roots_u8,
        winmd_dir_u8,
        "UseLatestBehaviorWhenAvailable"
    };

    uwp_bridge_log("execute_assembly",
                   "CoreCLR Domain Properties (runtime='%s'):", loaded_clr_u8);
    uwp_log_coreclr_props(keys, vals, 7, seen.count);

    /* Path A: coreclr_initialize + coreclr_execute_assembly (uses DefaultADID=1 internally) */
    pfn_coreclr_initialize p_init =
        (pfn_coreclr_initialize)(void *)GetProcAddress(h_clr, "coreclr_initialize");
    pfn_coreclr_execute_assembly p_exec =
        (pfn_coreclr_execute_assembly)(void *)GetProcAddress(h_clr, "coreclr_execute_assembly");
    pfn_coreclr_shutdown p_shut =
        (pfn_coreclr_shutdown)(void *)GetProcAddress(h_clr, "coreclr_shutdown");

    if (p_init && p_exec) {
        void *host_handle = NULL;
        unsigned int domain_id = 0u;

        uwp_bridge_log("execute_assembly",
                       "Hosting managed assembly '%s' via coreclr_initialize '%s'",
                       asm_u8, loaded_clr_u8);

        int rc = p_init(asm_u8, "ProtonScarlettUwpDomain", 7, keys, vals, &host_handle, &domain_id);
        free(tpa_u8);
        free(tpa_w);
        if (rc < 0) {
            uwp_bridge_log("execute_assembly", "coreclr_initialize failed hr=0x%08X", (unsigned)rc);
            if (out_exit_code) *out_exit_code = rc;
            return -1;
        }

        const char **argv_u8 = NULL;
        char **argv_alloc = NULL;
        if (argc > 0 && argv) {
            argv_u8 = (const char **)calloc((size_t)argc, sizeof(const char *));
            argv_alloc = (char **)calloc((size_t)argc, sizeof(char *));
            for (int i = 0; i < argc; ++i) {
                argv_alloc[i] = (char *)malloc(512);
                if (argv_alloc[i]) {
                    uwp_wstr_to_utf8(argv[i], argv_alloc[i], 512);
                    argv_u8[i] = argv_alloc[i];
                }
            }
        }

        unsigned int exit_code = 0u;
        uwp_mutex_lock(&g_inproc_lock);
        g_active_coreclr_mod = h_clr;
        g_active_coreclr_domain_id = (DWORD)domain_id;
        uwp_mutex_unlock(&g_inproc_lock);

        uwp_bridge_log("execute_assembly",
                       "coreclr_execute_assembly(domain=%u, argc=%d)", domain_id, argc);
        rc = p_exec(host_handle, domain_id, argc, argv_u8, asm_u8, &exit_code);

        uwp_mutex_lock(&g_inproc_lock);
        g_active_coreclr_mod = NULL;
        g_active_coreclr_domain_id = 0u;
        uwp_mutex_unlock(&g_inproc_lock);
        if (p_shut) {
            p_shut(host_handle, domain_id);
        }

        if (argv_alloc) {
            for (int i = 0; i < argc; ++i) free(argv_alloc[i]);
            free(argv_alloc);
            free(argv_u8);
        }

        uwp_bridge_log("execute_assembly", "coreclr_execute_assembly finished rc=0x%08X exit_code=%u",
                       (unsigned)rc, exit_code);
        if (out_exit_code) *out_exit_code = (rc < 0) ? rc : (int)exit_code;
        return (rc < 0) ? -1 : 1;
    }

    /* Path B: Fallback via GetCLRRuntimeHost (ICLRRuntimeHost2) */
    pfn_GetCLRRuntimeHost p_get_host =
        (pfn_GetCLRRuntimeHost)(void *)GetProcAddress(h_clr, "GetCLRRuntimeHost");
    if (p_get_host) {
        ICLRRuntimeHost2 *host2 = NULL;
        HRESULT hr = p_get_host(&SCARLETT_IID_ICLRRuntimeHost2, (void **)&host2);
        if (SUCCEEDED(hr) && host2) {
            /* STARTUP_CONCURRENT_GC (0x1) | STARTUP_LOADER_OPTIMIZATION_SINGLE_DOMAIN (0x2) | STARTUP_SINGLE_APPDOMAIN (0x10000) */
            hr = host2->lpVtbl->SetStartupFlags(host2, 0x00010003u);
            uwp_bridge_log("execute_assembly", "ICLRRuntimeHost2::SetStartupFlags(0x10003) -> hr=0x%08X", (unsigned)hr);
            hr = host2->lpVtbl->Start(host2);
            if (FAILED(hr)) {
                uwp_bridge_log("execute_assembly", "ICLRRuntimeHost2::Start failed hr=0x%08X", (unsigned)hr);
                free(tpa_u8);
                free(tpa_w);
                host2->lpVtbl->Release(host2);
                if (out_exit_code) *out_exit_code = (int)hr;
                return -1;
            }

            wchar_t app_paths_w[UWP_MAX_PATH_CHARS * 4];
            wchar_t ni_paths_w[UWP_MAX_PATH_CHARS * 4];
            wchar_t res_roots_w[UWP_MAX_PATH_CHARS * 4];
            swprintf(app_paths_w, UWP_MAX_PATH_CHARS * 4, L"%ls;%ls;%ls",
                     entry_dir_w[0] ? entry_dir_w : L".",
                     (pkg_dir && pkg_dir[0]) ? pkg_dir : L".",
                     clr_dir_w[0] ? clr_dir_w : L".");
            swprintf(ni_paths_w, UWP_MAX_PATH_CHARS * 4, L"%ls;%ls;%ls",
                     clr_dir_w[0] ? clr_dir_w : L".",
                     (pkg_dir && pkg_dir[0]) ? pkg_dir : L".",
                     entry_dir_w[0] ? entry_dir_w : L".");
            swprintf(res_roots_w, UWP_MAX_PATH_CHARS * 4, L"%ls;%ls;%ls",
                     (pkg_dir && pkg_dir[0]) ? pkg_dir : L".",
                     entry_dir_w[0] ? entry_dir_w : L".",
                     clr_dir_w[0] ? clr_dir_w : L".");

            LPCWSTR prop_names[7] = {
                L"TRUSTED_PLATFORM_ASSEMBLIES",
                L"APP_PATHS",
                L"APP_NI_PATHS",
                L"NATIVE_DLL_SEARCH_DIRECTORIES",
                L"PLATFORM_RESOURCE_ROOTS",
                L"APP_LOCAL_WINMETADATA",
                L"AppDomainCompatSwitch"
            };
            LPCWSTR prop_vals[7] = {
                tpa_w ? tpa_w : L"",
                app_paths_w,
                ni_paths_w,
                app_paths_w,
                res_roots_w,
                winmd_dir_w[0] ? winmd_dir_w : ((pkg_dir && pkg_dir[0]) ? pkg_dir : L"."),
                L"UseLatestBehaviorWhenAvailable"
            };

            DWORD domain_id = 0;
            /*
             * APPDOMAIN_ENABLE_PINVOKE_AND_CLASSIC_COMINTEROP (0x10) |
             * APPDOMAIN_ENABLE_PLATFORM_SPECIFIC_APPS (0x40) |
             * APPDOMAIN_ENABLE_ASSEMBLY_LOADFILE (0x80) |
             * APPDOMAIN_DISABLE_TRANSPARENCY_ENFORCEMENT (0x100)
             */
            hr = host2->lpVtbl->CreateAppDomainWithManager(
                host2, L"ProtonScarlettUwpDomain", 0x1D0u, NULL, NULL,
                7, prop_names, prop_vals, &domain_id);
            free(tpa_u8);
            free(tpa_w);

            if (FAILED(hr)) {
                uwp_bridge_log("execute_assembly",
                               "ICLRRuntimeHost2::CreateAppDomainWithManager failed hr=0x%08X",
                               (unsigned)hr);
                host2->lpVtbl->Stop(host2);
                host2->lpVtbl->Release(host2);
                if (out_exit_code) *out_exit_code = (int)hr;
                return -1;
            }

            DWORD ret_val = 0;
            uwp_bridge_log("execute_assembly",
                           "ICLRRuntimeHost2::ExecuteAssembly(domain=%u, argc=%d) via '%s'",
                           (unsigned)domain_id, argc, loaded_clr_u8);
            hr = host2->lpVtbl->ExecuteAssembly(host2, domain_id, assembly_path, argc, argv, &ret_val);
            uwp_bridge_log("execute_assembly",
                           "ICLRRuntimeHost2::ExecuteAssembly finished hr=0x%08X ret=%u",
                           (unsigned)hr, (unsigned)ret_val);
            host2->lpVtbl->UnloadAppDomain(host2, domain_id, TRUE);
            host2->lpVtbl->Stop(host2);
            host2->lpVtbl->Release(host2);
            if (out_exit_code) *out_exit_code = FAILED(hr) ? (int)hr : (int)ret_val;
            return FAILED(hr) ? -1 : 1;
        }
    }

    free(tpa_u8);
    free(tpa_w);
    uwp_bridge_log("execute_assembly",
                   "Loaded '%s' but missing coreclr_initialize/GetCLRRuntimeHost exports",
                   loaded_clr_u8);
    return 0;
}
#endif

static void uwp_derive_pkg_dir_from_assembly(const wchar_t *assembly_path,
                                             wchar_t *out_dir, size_t out_chars) {
    out_dir[0] = L'\0';
    if (!assembly_path || assembly_path[0] == L'\0') return;
    const wchar_t *last_sep = NULL;
    for (const wchar_t *p = assembly_path; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') last_sep = p;
    }
    if (!last_sep) return;
    size_t len = (size_t)(last_sep - assembly_path);
    if (len >= out_chars) len = out_chars - 1;
    memcpy(out_dir, assembly_path, len * sizeof(wchar_t));
    out_dir[len] = L'\0';

    /* Strip trailing "\entrypoint" or "/entrypoint" if present (UWPShim layout) */
    const wchar_t *prev_sep = NULL;
    for (wchar_t *p = out_dir; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') prev_sep = p;
    }
    if (prev_sep && (wcscmp(prev_sep + 1, L"entrypoint") == 0 ||
                     wcscmp(prev_sep + 1, L"EntryPoint") == 0)) {
        out_dir[prev_sep - out_dir] = L'\0';
    }
}

HRESULT UWP_API ExecuteAssembly(LPCWSTR assemblyPath, int argc, LPCWSTR *argv, int *pExitCode) {
    if (pExitCode) *pExitCode = -1;

    wchar_t derived_pkg_dir[UWP_MAX_PATH_CHARS];
    uwp_derive_pkg_dir_from_assembly(assemblyPath, derived_pkg_dir, UWP_MAX_PATH_CHARS);

    /* Ensure manifest is loaded from package root if not already loaded */
    if (!g_host_initialized) {
        wchar_t manifest_candidate[UWP_MAX_PATH_CHARS];
        manifest_candidate[0] = L'\0';
        if (derived_pkg_dir[0] != L'\0' && wcslen(derived_pkg_dir) + 18 < UWP_MAX_PATH_CHARS) {
            wcscpy(manifest_candidate, derived_pkg_dir);
            wcscat(manifest_candidate, L"\\AppxManifest.xml");
        }
        uwp_host_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.abi_version = SCARLETT_UWP_BRIDGE_ABI_VERSION;
        cfg.manifest_path = manifest_candidate[0] ? manifest_candidate : NULL;
        UwpHostInitialize(&cfg);
    }

    const uwp_package_identity_t *id = uwp_core_get_package_identity();
    char asm_u8[UWP_MAX_PATH_CHARS * 2];
    char name_u8[UWP_MAX_IDENTITY_CHARS];
    char ep_u8[UWP_MAX_IDENTITY_CHARS];
    uwp_wstr_to_utf8(assemblyPath ? assemblyPath : L"<null>", asm_u8, sizeof(asm_u8));
    uwp_wstr_to_utf8(id->name, name_u8, sizeof(name_u8));
    uwp_wstr_to_utf8(id->entry_point, ep_u8, sizeof(ep_u8));

    uwp_bridge_log("execute_assembly",
                   "ExecuteAssembly(path='%s', argc=%d) pkg='%s' ver=%u.%u.%u.%u entry='%s' inproc=%u",
                   asm_u8,
                   argc,
                   name_u8,
                   (unsigned)id->ver_major, (unsigned)id->ver_minor,
                   (unsigned)id->ver_build, (unsigned)id->ver_revision,
                   ep_u8,
                   (unsigned)id->inproc_class_count);

    for (int i = 0; i < argc && argv && i < 8; ++i) {
        char arg_u8[256];
        uwp_wstr_to_utf8(argv[i], arg_u8, sizeof(arg_u8));
        uwp_bridge_log("execute_assembly", "  argv[%d] = '%s'", i, arg_u8);
    }

    /* Path 1: Try CoreCLR / .NET Core UWP runtime hosting if coreclr.dll is available */
#ifdef _WIN32
    const wchar_t *pkg_root = (id->install_path[0] != L'\0') ? id->install_path : derived_pkg_dir;
    int clr_exit = -1;
    int clr_res = uwp_try_coreclr_execute(assemblyPath, pkg_root, argc, argv, &clr_exit);
    if (clr_res > 0) {
        if (pExitCode) *pExitCode = clr_exit;
        return S_OK;
    }
    if (clr_res < 0) {
        if (pExitCode) *pExitCode = clr_exit;
        return (HRESULT)clr_exit;
    }
#else
    (void)argv;
#endif

    /* Path 2: Manifest-driven native/UnityPlayer InProcessServer bootstrap */
    uwp_inproc_class_entry_t entry;
    if (id->entry_point[0] != L'\0' && uwp_manifest_find_inproc_class(id->entry_point, &entry)) {
        UWP_HSTRING ep_hs = NULL;
        if (SUCCEEDED(uwp_hstring_create_from_wstr(id->entry_point, &ep_hs)) && ep_hs) {
            UwpIActivationFactory *af = NULL;
            HRESULT hr = UwpHostTryGetActivationFactory(ep_hs, &UWP_IID_IActivationFactory, (void **)&af);
            WindowsDeleteString(ep_hs);
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
                        if (pExitCode) *pExitCode = SUCCEEDED(hr) ? 0 : (int)hr;
                        return hr;
                    }
                }
            }
        }
    }

    if (uwp_manifest_find_inproc_class(L"UnityPlayer.AppCallbacks", &entry)) {
        uwp_bridge_log("execute_assembly",
                       "Detected Unity UWP package ('UnityPlayer.AppCallbacks' in manifest). "
                       "Attempting native activation of UnityPlayer.dll...");
        RoInitialize(UWP_RO_INIT_MULTITHREADED);

        UWP_HSTRING up_hs = NULL;
        HRESULT hr = uwp_hstring_create_from_wstr(L"UnityPlayer.AppCallbacks", &up_hs);
        if (SUCCEEDED(hr) && up_hs) {
            UwpIActivationFactory *af = NULL;
            hr = UwpHostTryGetActivationFactory(up_hs, &UWP_IID_IActivationFactory, (void **)&af);
            WindowsDeleteString(up_hs);
            if (SUCCEEDED(hr) && af) {
                UwpIInspectable *inst = NULL;
                hr = af->lpVtbl->ActivateInstance(af, &inst);
                af->lpVtbl->Release(af);
                if (SUCCEEDED(hr) && inst) {
                    UwpIFrameworkViewSource *vs = NULL;
                    HRESULT qi_hr = inst->lpVtbl->QueryInterface(inst, &UWP_IID_IFrameworkViewSource, (void **)&vs);
                    if (SUCCEEDED(qi_hr) && vs) {
                        inst->lpVtbl->Release(inst);
                        hr = uwp_coreapp_run_view_source(vs);
                        vs->lpVtbl->Release(vs);
                        if (pExitCode) *pExitCode = SUCCEEDED(hr) ? 0 : (int)hr;
                        return hr;
                    }
                    inst->lpVtbl->Release(inst);
                    uwp_bridge_log("execute_assembly",
                                   "UnityPlayer.AppCallbacks activated cleanly, but does not expose "
                                   "IFrameworkViewSource directly (requires managed IL wrapper '%s' + "
                                   "WinRTBridge.WinRTBridge via CoreCLR / PROTON_UWP_CORECLR_PATH)",
                                   ep_u8);
                } else {
                    uwp_bridge_log("execute_assembly",
                                   "UnityPlayer.AppCallbacks ActivateInstance failed hr=0x%08X",
                                   (unsigned)hr);
                }
            } else {
                uwp_bridge_log("execute_assembly",
                               "UnityPlayer.AppCallbacks activation factory load failed hr=0x%08X",
                               (unsigned)hr);
            }
        }
    }

    /* Zero silent return S_OK: report exact missing managed runtime requirement */
    uwp_bridge_log("execute_assembly",
                   "FAILED to execute managed UWP assembly '%s' (EntryPoint='%s'): "
                   "no CoreCLR runtime (coreclr.dll / PROTON_UWP_CORECLR_PATH) available and "
                   "manifest entry point is not a standalone native IFrameworkViewSource.",
                   asm_u8, ep_u8);
    if (pExitCode) *pExitCode = (int)SCARLETT_ERR_UNIMPLEMENTED_STUB;
    return SCARLETT_ERR_UNIMPLEMENTED_STUB;
}
