/*
 * Proton-Scarlett — UWP Bridge Frozen ABI Header
 *
 * Shared contract across:
 *   - scarlett/uwp_bridge/uwphost/   (CoreApplication, CoreWindow, CoreDispatcher, UWPHOST.dll)
 *   - scarlett/uwp_bridge/winrt_ui/  (DisplayInformation, ApplicationView, SystemNavigationManager, ...)
 *   - scarlett/uwp_bridge/winrt_sys/ (ApplicationData, Package, Gamepad, AnalyticsInfo)
 *   - scarlett/uwp_bridge/forwarders/ (*_APP.dll forwarders + diagnostic thunks)
 *   - dxvk/src/dxgi/                 (CreateSwapChainForCoreWindow)
 *   - wine/dlls/combase & kernelbase (UWP activation & package identity hooks)
 *
 * Clean-room C11 / C++20 compatible header.
 */

#ifndef SCARLETT_UWP_BRIDGE_ABI_H
#define SCARLETT_UWP_BRIDGE_ABI_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#include <unknwn.h>
#else
typedef int32_t HRESULT;
typedef uint32_t ULONG;
typedef uint32_t UINT32;
typedef int32_t INT32;
typedef int32_t BOOL;
typedef void *HWND;
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;
typedef const GUID *REFIID;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SCARLETT_UWP_BRIDGE_ABI_VERSION UINT32_C(1)

/* ============================================================================
 * Canonical Interop GUIDs (CoreWindow <-> DXVK / vkd3d-proton)
 * ============================================================================ */

/* Standard Windows ICoreWindowInterop: {45D64A29-A63E-4CB6-B498-5781D298CB4F} */
#define SCARLETT_IID_ICOREWINDOWINTEROP_INIT \
    { 0x45D64A29u, 0xA63Eu, 0x4CB6u, { 0xB4u, 0x98u, 0x57u, 0x81u, 0xD2u, 0x98u, 0xCBu, 0x4Fu } }

/* Proton-Scarlett IProtonScarlettCoreWindowBridge: {7A53F1D2-8C4B-4E91-9A2D-5B6E0C3F8A10} */
#define SCARLETT_IID_IPROTON_SCARLETT_COREWINDOW_BRIDGE_INIT \
    { 0x7A53F1D2u, 0x8C4Bu, 0x4E91u, { 0x9Au, 0x2Du, 0x5Bu, 0x6Eu, 0x0Cu, 0x3Fu, 0x8Au, 0x10u } }

static const GUID SCARLETT_IID_ICoreWindowInterop =
    SCARLETT_IID_ICOREWINDOWINTEROP_INIT;

static const GUID SCARLETT_IID_IProtonScarlettCoreWindowBridge =
    SCARLETT_IID_IPROTON_SCARLETT_COREWINDOW_BRIDGE_INIT;

/* ============================================================================
 * Core WinRT Base GUIDs
 * ============================================================================ */

/* IInspectable: {AF86E2E0-B12D-4C6A-9C5A-D7AA65101E90} */
#define SCARLETT_IID_IINSPECTABLE_INIT \
    { 0xAF86E2E0u, 0xB12Du, 0x4C6Au, { 0x9Cu, 0x5Au, 0xD7u, 0xAAu, 0x65u, 0x10u, 0x1Eu, 0x90u } }

/* IActivationFactory: {00000035-0000-0000-C000-000000000046} */
#define SCARLETT_IID_IACTIVATIONFACTORY_INIT \
    { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } }

/* IAgileObject: {94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90} */
#define SCARLETT_IID_IAGILEOBJECT_INIT \
    { 0x94EA2B94u, 0xE9CCu, 0x49E0u, { 0xC0u, 0xFFu, 0xEEu, 0x64u, 0xCAu, 0x8Fu, 0x5Bu, 0x90u } }

/* ============================================================================
 * C ABI Vtable Definitions for CoreWindow Interop
 * ============================================================================ */

#ifdef _WIN32
typedef struct ICoreWindowInteropBridgeVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *get_WindowHandle)(void *This, HWND *hwnd);
    HRESULT (STDMETHODCALLTYPE *put_MessageHandled)(void *This, uint8_t value);
} ICoreWindowInteropBridgeVtbl;

typedef struct IProtonScarlettCoreWindowBridgeVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetHwnd)(void *This, HWND *out_hwnd);
    HRESULT (STDMETHODCALLTYPE *GetClientSize)(void *This, uint32_t *out_width, uint32_t *out_height);
    HRESULT (STDMETHODCALLTYPE *IsFullscreen)(void *This, int32_t *out_is_fullscreen);
    HRESULT (STDMETHODCALLTYPE *NotifySwapChainCreated)(void *This, IUnknown *swapchain);
} IProtonScarlettCoreWindowBridgeVtbl;

typedef struct IProtonScarlettCoreWindowBridge {
    const IProtonScarlettCoreWindowBridgeVtbl *lpVtbl;
} IProtonScarlettCoreWindowBridge;
#endif

/* ============================================================================
 * Shared Runtime State & Sub-Module Hooks (winrt_ui / winrt_sys <-> uwphost)
 * ============================================================================ */

#define UWP_MAX_PATH_CHARS      512
#define UWP_MAX_IDENTITY_CHARS  256
#define UWP_MAX_INPROC_CLASSES  64

typedef struct uwp_inproc_class_entry_s {
    wchar_t class_id[UWP_MAX_IDENTITY_CHARS];
    wchar_t dll_path[UWP_MAX_PATH_CHARS];
    uint32_t threading_model; /* 0 = both, 1 = sta, 2 = mta */
} uwp_inproc_class_entry_t;

typedef struct uwp_package_identity_s {
    uint32_t valid;
    uint16_t ver_major;
    uint16_t ver_minor;
    uint16_t ver_build;
    uint16_t ver_revision;
    uint32_t architecture; /* 9 = x64 */
    wchar_t  name[UWP_MAX_IDENTITY_CHARS];
    wchar_t  publisher[UWP_MAX_IDENTITY_CHARS];
    wchar_t  publisher_id[64];
    wchar_t  full_name[UWP_MAX_IDENTITY_CHARS];
    wchar_t  family_name[UWP_MAX_IDENTITY_CHARS];
    wchar_t  aumid[UWP_MAX_IDENTITY_CHARS];
    wchar_t  app_id[128];
    wchar_t  executable[UWP_MAX_PATH_CHARS];
    wchar_t  entry_point[UWP_MAX_IDENTITY_CHARS];
    wchar_t  display_name[UWP_MAX_IDENTITY_CHARS];
    wchar_t  install_path[UWP_MAX_PATH_CHARS];
    wchar_t  local_folder[UWP_MAX_PATH_CHARS];
    wchar_t  roaming_folder[UWP_MAX_PATH_CHARS];
    wchar_t  temp_folder[UWP_MAX_PATH_CHARS];
    uint32_t inproc_class_count;
    uwp_inproc_class_entry_t inproc_classes[UWP_MAX_INPROC_CLASSES];
} uwp_package_identity_t;

typedef struct uwp_host_config_s {
    uint32_t abi_version;
    uint32_t default_width;
    uint32_t default_height;
    uint32_t strict_mode;
    const wchar_t *manifest_path;
} uwp_host_config_t;

/* CoreWindow accessors exported internally inside UWPHOST.dll for winrt_ui / winrt_sys */
HWND     uwp_core_get_main_hwnd(void);
void     uwp_core_get_client_size(uint32_t *out_width, uint32_t *out_height);
int32_t  uwp_core_is_fullscreen(void);
int32_t  uwp_core_set_fullscreen(int32_t fullscreen);
void     uwp_core_set_window_title(const wchar_t *title);
void     uwp_core_get_window_title(wchar_t *buf, uint32_t buf_chars);
const uwp_package_identity_t *uwp_core_get_package_identity(void);

/* Diagnostic logger shared by uwphost, winrt_ui, winrt_sys, and forwarders */
void uwp_bridge_log(const char *channel, const char *fmt, ...);
void uwp_bridge_log_unknown_class(
    const wchar_t *class_id,
    REFIID iid,
    void *caller_ret_addr,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9);

/* Sub-module factory hooks linked into UWPHOST.dll */
HRESULT uwp_winrt_ui_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory);
HRESULT uwp_winrt_sys_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory);

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_UWP_BRIDGE_ABI_H */
