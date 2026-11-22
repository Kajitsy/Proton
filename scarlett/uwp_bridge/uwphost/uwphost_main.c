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
    (void)T; if (!c || !i) return E_INVALIDARG; *c = 0; *i = NULL; return S_OK;
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
            uwp_bridge_log("inproc",
                           "Class '%s' in '%s' self-forwards to uwphost.DllGetActivationFactory "
                           "(managed CLR WinRT class; requires managed host registration)",
                           cls_u8, dll_u8);
            return uwp_coreapp_try_custom_factory(class_id, iid, out_factory);
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

static int uwp_try_coreclr_execute(const wchar_t *assembly_path,
                                   const wchar_t *pkg_dir,
                                   int argc,
                                   LPCWSTR *argv,
                                   int *out_exit_code) {
    wchar_t clr_candidates[3][UWP_MAX_PATH_CHARS];
    int cand_count = 0;

    if (uwp_get_env_wstr(L"PROTON_UWP_CORECLR_PATH", clr_candidates[cand_count], UWP_MAX_PATH_CHARS)) {
        uwp_manifest_normalize_path(clr_candidates[cand_count], clr_candidates[cand_count], UWP_MAX_PATH_CHARS);
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
        h_clr = LoadLibraryW(clr_candidates[i]);
        if (h_clr) {
            uwp_wstr_to_utf8(clr_candidates[i], loaded_clr_u8, sizeof(loaded_clr_u8));
            break;
        }
    }
    if (!h_clr) {
        return 0;
    }

    pfn_coreclr_initialize p_init =
        (pfn_coreclr_initialize)(void *)GetProcAddress(h_clr, "coreclr_initialize");
    pfn_coreclr_execute_assembly p_exec =
        (pfn_coreclr_execute_assembly)(void *)GetProcAddress(h_clr, "coreclr_execute_assembly");
    pfn_coreclr_shutdown p_shut =
        (pfn_coreclr_shutdown)(void *)GetProcAddress(h_clr, "coreclr_shutdown");

    if (!p_init || !p_exec) {
        uwp_bridge_log("execute_assembly",
                       "Loaded '%s' but missing coreclr_initialize/coreclr_execute_assembly exports",
                       loaded_clr_u8);
        return 0;
    }

    char asm_u8[UWP_MAX_PATH_CHARS * 2];
    char dir_u8[UWP_MAX_PATH_CHARS * 2];
    uwp_wstr_to_utf8(assembly_path, asm_u8, sizeof(asm_u8));
    uwp_wstr_to_utf8(pkg_dir ? pkg_dir : L".", dir_u8, sizeof(dir_u8));

    const char *keys[2] = { "APP_PATHS", "NATIVE_DLL_SEARCH_DIRECTORIES" };
    const char *vals[2] = { dir_u8, dir_u8 };
    void *host_handle = NULL;
    unsigned int domain_id = 0u;

    uwp_bridge_log("execute_assembly", "Hosting managed assembly '%s' via CoreCLR '%s'",
                   asm_u8, loaded_clr_u8);

    int rc = p_init(asm_u8, "ProtonScarlettUwpDomain", 2, keys, vals, &host_handle, &domain_id);
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
    rc = p_exec(host_handle, domain_id, argc, argv_u8, asm_u8, &exit_code);
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
