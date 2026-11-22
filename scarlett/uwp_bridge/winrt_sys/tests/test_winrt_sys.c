/*
 * Proton-Scarlett — Host Unit Tests for winrt_sys (UWPHOST.dll System WinRT)
 */

#define _POSIX_C_SOURCE 200809L

#include "../winrt_sys_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test shim controls from win32_host_shim.c */
void win32_host_shim_set_root(const char *unix_root);
void win32_host_shim_clear_env(void);
void win32_host_shim_set_env(const wchar_t *name, const wchar_t *value);
void win32_host_shim_set_pkg_identity(const uwp_package_identity_t *id);
void win32_host_shim_reset_logs(void);
uint32_t win32_host_shim_get_log_count(void);
const char *win32_host_shim_get_last_log(void);

typedef struct MockGiGamepadState {
    uint32_t buttons;
    float    leftTrigger;
    float    rightTrigger;
    float    leftThumbstickX;
    float    leftThumbstickY;
    float    rightThumbstickX;
    float    rightThumbstickY;
    uint32_t extendedButtons;
} MockGiGamepadState;

void win32_host_shim_reset_controllers(void);
void win32_host_shim_set_gameinput(int present, int connected, const MockGiGamepadState *st, uint64_t ts);
void win32_host_shim_get_gameinput_rumble(int *called, float *lf, float *hf, float *lt, float *rt);
void win32_host_shim_set_xinput(int present, uint32_t idx, int connected,
                                WORD buttons, BYTE lt, BYTE rt,
                                int16_t lx, int16_t ly, int16_t rx, int16_t ry);
void win32_host_shim_get_xinput_vib(uint32_t idx, int *called, WORD *left, WORD *right);
void winrt_sys_gamepad_test_reset(void);

/* Helper interface wrappers */
typedef struct GenericIf {
    const IInspectableVtbl *lpVtbl;
} GenericIf;

typedef struct StorageItemIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *Rename1)(void *This, HSTRING n, void **op);
        HRESULT (STDMETHODCALLTYPE *Rename2)(void *This, HSTRING n, uint32_t o, void **op);
        HRESULT (STDMETHODCALLTYPE *Del1)(void *This, void **op);
        HRESULT (STDMETHODCALLTYPE *Del2)(void *This, uint32_t o, void **op);
        HRESULT (STDMETHODCALLTYPE *Props)(void *This, void **op);
        HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Path)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Attributes)(void *This, uint32_t *v);
        HRESULT (STDMETHODCALLTYPE *get_DateCreated)(void *This, int64_t *v);
        HRESULT (STDMETHODCALLTYPE *IsOfType)(void *This, uint32_t t, boolean *v);
    } *lpVtbl;
} StorageItemIf;

typedef struct StorageFolderIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *CreateFileDef)(void *This, HSTRING n, void **op);
        HRESULT (STDMETHODCALLTYPE *CreateFile)(void *This, HSTRING n, uint32_t opt, void **op);
        HRESULT (STDMETHODCALLTYPE *CreateFolderDef)(void *This, HSTRING n, void **op);
        HRESULT (STDMETHODCALLTYPE *CreateFolder)(void *This, HSTRING n, uint32_t opt, void **op);
        HRESULT (STDMETHODCALLTYPE *GetFile)(void *This, HSTRING n, void **op);
        HRESULT (STDMETHODCALLTYPE *GetFolder)(void *This, HSTRING n, void **op);
        HRESULT (STDMETHODCALLTYPE *GetItem)(void *This, HSTRING n, void **op);
    } *lpVtbl;
} StorageFolderIf;

typedef struct StorageFolderStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetFolderFromPathAsync)(void *This, HSTRING path, void **op);
    } *lpVtbl;
} StorageFolderStaticsIf;

typedef struct AppDataStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **val);
    } *lpVtbl;
} AppDataStaticsIf;

typedef struct AppDataIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Version)(void *This, uint32_t *v);
        HRESULT (STDMETHODCALLTYPE *SetVersionAsync)(void *This, uint32_t v, void *h, void **op);
        HRESULT (STDMETHODCALLTYPE *ClearAllAsync)(void *This, void **op);
        HRESULT (STDMETHODCALLTYPE *ClearAsync)(void *This, uint32_t loc, void **op);
        HRESULT (STDMETHODCALLTYPE *get_LocalSettings)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_RoamingSettings)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_LocalFolder)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_RoamingFolder)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_TemporaryFolder)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *add_DataChanged)(void *This, void *h, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_DataChanged)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *SignalDataChanged)(void *This);
        HRESULT (STDMETHODCALLTYPE *get_RoamingStorageQuota)(void *This, uint64_t *v);
    } *lpVtbl;
} AppDataIf;

typedef struct AppDataContainerIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Locality)(void *This, uint32_t *v);
        HRESULT (STDMETHODCALLTYPE *get_Values)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_Containers)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *CreateContainer)(void *This, HSTRING n, uint32_t disp, void **c);
        HRESULT (STDMETHODCALLTYPE *DeleteContainer)(void *This, HSTRING n);
    } *lpVtbl;
} AppDataContainerIf;

typedef struct MapIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *Lookup)(void *This, HSTRING k, void **v);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *sz);
        HRESULT (STDMETHODCALLTYPE *HasKey)(void *This, HSTRING k, boolean *f);
        HRESULT (STDMETHODCALLTYPE *GetView)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *Insert)(void *This, HSTRING k, void *val, boolean *rep);
        HRESULT (STDMETHODCALLTYPE *Remove)(void *This, HSTRING k);
        HRESULT (STDMETHODCALLTYPE *Clear)(void *This);
    } *lpVtbl;
} MapIf;

typedef struct AsyncOpIf {
    const WinrtAsyncOperationPtrVtbl *lpVtbl;
} AsyncOpIf;

typedef struct PackageStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **v);
    } *lpVtbl;
} PackageStaticsIf;

typedef struct PackageIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_InstalledLocation)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_IsFramework)(void *This, boolean *v);
        HRESULT (STDMETHODCALLTYPE *get_Dependencies)(void *This, void **v);
    } *lpVtbl;
} PackageIf;

typedef struct Package2If {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_DisplayName)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_PublisherDisplayName)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Description)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Logo)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_IsResourcePackage)(void *This, boolean *v);
        HRESULT (STDMETHODCALLTYPE *get_IsBundle)(void *This, boolean *v);
        HRESULT (STDMETHODCALLTYPE *get_IsDevelopmentMode)(void *This, boolean *v);
    } *lpVtbl;
} Package2If;

typedef struct Package3If {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Status)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_InstalledDate)(void *This, int64_t *v);
        HRESULT (STDMETHODCALLTYPE *GetAppListEntriesAsync)(void *This, void **op);
    } *lpVtbl;
} Package3If;

typedef struct PackageIdIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Name)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Version)(void *This, WinrtPackageVersion *v);
        HRESULT (STDMETHODCALLTYPE *get_Architecture)(void *This, uint32_t *v);
        HRESULT (STDMETHODCALLTYPE *get_ResourceId)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_Publisher)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_PublisherId)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_FullName)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_FamilyName)(void *This, HSTRING *v);
    } *lpVtbl;
} PackageIdIf;

typedef struct GamepadStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *add_GamepadAdded)(void *This, void *v, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_GamepadAdded)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *add_GamepadRemoved)(void *This, void *v, EventRegistrationToken *tok);
        HRESULT (STDMETHODCALLTYPE *remove_GamepadRemoved)(void *This, EventRegistrationToken tok);
        HRESULT (STDMETHODCALLTYPE *get_Gamepads)(void *This, void **v);
    } *lpVtbl;
} GamepadStaticsIf;

typedef struct GamepadStatics2If {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *FromGameController)(void *This, void *ctrl, void **v);
    } *lpVtbl;
} GamepadStatics2If;

typedef struct GamepadIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Vibration)(void *This, WinrtGamepadVibration *v);
        HRESULT (STDMETHODCALLTYPE *put_Vibration)(void *This, WinrtGamepadVibration v);
        HRESULT (STDMETHODCALLTYPE *GetCurrentReading)(void *This, WinrtGamepadReading *v);
    } *lpVtbl;
} GamepadIf;

typedef struct VectorViewIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t idx, void **v);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *v);
    } *lpVtbl;
} VectorViewIf;

typedef struct AnalyticsInfoStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_VersionInfo)(void *This, void **v);
        HRESULT (STDMETHODCALLTYPE *get_DeviceForm)(void *This, HSTRING *v);
    } *lpVtbl;
} AnalyticsInfoStaticsIf;

typedef struct AnalyticsVersionInfoIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_DeviceFamily)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_DeviceFamilyVersion)(void *This, HSTRING *v);
    } *lpVtbl;
} AnalyticsVersionInfoIf;

typedef struct GameBarStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *add_VisibilityChanged)(void *This, void *h, EventRegistrationToken *t);
        HRESULT (STDMETHODCALLTYPE *remove_VisibilityChanged)(void *This, EventRegistrationToken t);
        HRESULT (STDMETHODCALLTYPE *add_IsInputRedirectedChanged)(void *This, void *h, EventRegistrationToken *t);
        HRESULT (STDMETHODCALLTYPE *remove_IsInputRedirectedChanged)(void *This, EventRegistrationToken t);
        HRESULT (STDMETHODCALLTYPE *get_Visible)(void *This, boolean *v);
        HRESULT (STDMETHODCALLTYPE *get_IsInputRedirected)(void *This, boolean *v);
    } *lpVtbl;
} GameBarStaticsIf;

static HSTRING make_hs(const wchar_t *s) {
    HSTRING h = NULL;
    assert(SUCCEEDED(WindowsCreateString(s, (UINT32)wcslen(s), &h)));
    return h;
}

static int g_async_cb_fired = 0;
static AsyncStatus g_async_cb_status = Started;

static HRESULT STDMETHODCALLTYPE test_async_cb_qi(void *This, REFIID r, void **p) {
    (void)r; *p = This; return S_OK;
}
static ULONG STDMETHODCALLTYPE test_async_cb_ar(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE test_async_cb_invoke(void *This, void *info, AsyncStatus st) {
    (void)This; (void)info;
    g_async_cb_fired++;
    g_async_cb_status = st;
    return S_OK;
}
static const WinrtAsyncCompletedHandlerPtrVtbl g_test_async_cb_vtbl = {
    test_async_cb_qi, test_async_cb_ar, test_async_cb_ar, test_async_cb_invoke
};
static struct { const WinrtAsyncCompletedHandlerPtrVtbl *lpVtbl; } g_test_async_cb = { &g_test_async_cb_vtbl };

static int g_gp_added_events = 0;
static int g_gp_removed_events = 0;

static HRESULT STDMETHODCALLTYPE test_gp_added_invoke(void *This, IInspectable *sender, void *args) {
    (void)This; (void)sender; (void)args;
    g_gp_added_events++;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE test_gp_removed_invoke(void *This, IInspectable *sender, void *args) {
    (void)This; (void)sender; (void)args;
    g_gp_removed_events++;
    return S_OK;
}
static const WinrtEventHandlerPtrVtbl g_test_gp_added_vtbl = {
    test_async_cb_qi, test_async_cb_ar, test_async_cb_ar, test_gp_added_invoke
};
static const WinrtEventHandlerPtrVtbl g_test_gp_removed_vtbl = {
    test_async_cb_qi, test_async_cb_ar, test_async_cb_ar, test_gp_removed_invoke
};
static struct { const WinrtEventHandlerPtrVtbl *lpVtbl; } g_test_gp_added = { &g_test_gp_added_vtbl };
static struct { const WinrtEventHandlerPtrVtbl *lpVtbl; } g_test_gp_removed = { &g_test_gp_removed_vtbl };

/* ============================================================================
 * Test Cases
 * ============================================================================ */

static void test_factory_dispatch_and_no_silent_stubs(void) {
    void *fac = NULL;
    assert(uwp_winrt_sys_get_activation_factory(L"Windows.Storage.ApplicationData", &WINRT_IID_IApplicationDataStatics, NULL) == E_POINTER);
    assert(uwp_winrt_sys_get_activation_factory(NULL, &WINRT_IID_IApplicationDataStatics, &fac) == E_INVALIDARG);
    assert(uwp_winrt_sys_get_activation_factory(L"Windows.NonExistent.Class", &WINRT_IID_IInspectable, &fac) == CLASS_E_CLASSNOTAVAILABLE);

    /* ActivateInstance on static-only class must log & return E_NOTIMPL, never S_OK */
    win32_host_shim_reset_logs();
    typedef struct ActFacIf { const WinrtActivationFactoryVtbl *lpVtbl; } ActFacIf;
    ActFacIf *af = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(L"Windows.ApplicationModel.Package", &WINRT_IID_IActivationFactory, (void **)&af)));
    IInspectable *inst = NULL;
    assert(af->lpVtbl->ActivateInstance(af, &inst) == E_NOTIMPL);
    assert(inst == NULL);
    assert(win32_host_shim_get_log_count() >= 1u);
    assert(strstr(win32_host_shim_get_last_log(), "E_NOTIMPL") != NULL);
    af->lpVtbl->Release(af);
}

static void test_application_data_and_storage_folder(const char *sandbox) {
    win32_host_shim_clear_env();
    win32_host_shim_set_env(L"PROTON_UWP_LOCAL_STATE", L"C:\\AppData\\LocalState");
    win32_host_shim_set_env(L"PROTON_UWP_ROAMING_STATE", L"C:\\AppData\\RoamingState");
    win32_host_shim_set_env(L"PROTON_UWP_TEMP_STATE", L"C:\\AppData\\TempState");

    AppDataStaticsIf *ad_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Storage.ApplicationData", &WINRT_IID_IApplicationDataStatics, (void **)&ad_stat)));
    AppDataIf *ad = NULL;
    assert(SUCCEEDED(ad_stat->lpVtbl->get_Current(ad_stat, (void **)&ad)));
    assert(ad != NULL);

    /* Folders exist on disk & implement IStorageFolder + IStorageItem */
    StorageFolderIf *lf = NULL, *rf = NULL, *tf = NULL;
    assert(SUCCEEDED(ad->lpVtbl->get_LocalFolder(ad, (void **)&lf)));
    assert(SUCCEEDED(ad->lpVtbl->get_RoamingFolder(ad, (void **)&rf)));
    assert(SUCCEEDED(ad->lpVtbl->get_TemporaryFolder(ad, (void **)&tf)));

    StorageItemIf *li = NULL;
    assert(SUCCEEDED(lf->lpVtbl->QueryInterface(lf, &WINRT_IID_IStorageItem, (void **)&li)));
    HSTRING path_hs = NULL, name_hs = NULL;
    assert(SUCCEEDED(li->lpVtbl->get_Path(li, &path_hs)));
    assert(SUCCEEDED(li->lpVtbl->get_Name(li, &name_hs)));
    assert(wcscmp(WindowsGetStringRawBuffer(path_hs, NULL), L"C:\\AppData\\LocalState") == 0);
    assert(wcscmp(WindowsGetStringRawBuffer(name_hs, NULL), L"LocalState") == 0);
    WindowsDeleteString(path_hs);
    WindowsDeleteString(name_hs);

    boolean is_dir = 0;
    assert(SUCCEEDED(li->lpVtbl->IsOfType(li, 2u /* Folder */, &is_dir)));
    assert(is_dir == 1);
    li->lpVtbl->Release(li);

    char check_dir[1024];
    snprintf(check_dir, sizeof(check_dir), "%s/drive_c/AppData/LocalState", sandbox);
    struct stat st;
    assert(stat(check_dir, &st) == 0 && S_ISDIR(st.st_mode));

    /* CreateFolderAsync inside LocalFolder */
    HSTRING sub_hs = make_hs(L"Saves");
    AsyncOpIf *cf_op = NULL;
    assert(SUCCEEDED(lf->lpVtbl->CreateFolder(lf, sub_hs, 3u /* OpenIfExists */, (void **)&cf_op)));
    WindowsDeleteString(sub_hs);
    g_async_cb_fired = 0;
    assert(SUCCEEDED(cf_op->lpVtbl->put_Completed(cf_op, &g_test_async_cb)));
    assert(g_async_cb_fired == 1 && g_async_cb_status == Completed);
    StorageFolderIf *saves_f = NULL;
    assert(SUCCEEDED(cf_op->lpVtbl->GetResults(cf_op, (void **)&saves_f)));
    assert(saves_f != NULL);
    cf_op->lpVtbl->Release(cf_op);

    /* StorageFolder.GetFolderFromPathAsync */
    StorageFolderStaticsIf *sf_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Storage.StorageFolder", &WINRT_IID_IStorageFolderStatics, (void **)&sf_stat)));
    HSTRING saves_path = make_hs(L"C:\\AppData\\LocalState\\Saves");
    AsyncOpIf *gf_op = NULL;
    assert(SUCCEEDED(sf_stat->lpVtbl->GetFolderFromPathAsync(sf_stat, saves_path, (void **)&gf_op)));
    WindowsDeleteString(saves_path);
    StorageFolderIf *saves_f2 = NULL;
    assert(SUCCEEDED(gf_op->lpVtbl->GetResults(gf_op, (void **)&saves_f2)));
    assert(saves_f2 != NULL);
    StorageItemIf *saves_i2 = NULL;
    assert(SUCCEEDED(saves_f2->lpVtbl->QueryInterface(saves_f2, &WINRT_IID_IStorageItem, (void **)&saves_i2)));
    HSTRING sn = NULL;
    assert(SUCCEEDED(saves_i2->lpVtbl->get_Name(saves_i2, &sn)));
    assert(wcscmp(WindowsGetStringRawBuffer(sn, NULL), L"Saves") == 0);
    WindowsDeleteString(sn);
    saves_i2->lpVtbl->Release(saves_i2);
    saves_f2->lpVtbl->Release(saves_f2);
    gf_op->lpVtbl->Release(gf_op);

    /* Nonexistent path returns Error AsyncStatus */
    HSTRING missing_path = make_hs(L"C:\\DoesNotExist\\Missing");
    AsyncOpIf *miss_op = NULL;
    assert(SUCCEEDED(sf_stat->lpVtbl->GetFolderFromPathAsync(sf_stat, missing_path, (void **)&miss_op)));
    WindowsDeleteString(missing_path);
    void *miss_res = NULL;
    assert(FAILED(miss_op->lpVtbl->GetResults(miss_op, &miss_res)));
    assert(miss_res == NULL);
    miss_op->lpVtbl->Release(miss_op);
    sf_stat->lpVtbl->Release(sf_stat);

    saves_f->lpVtbl->Release(saves_f);
    lf->lpVtbl->Release(lf);
    rf->lpVtbl->Release(rf);
    tf->lpVtbl->Release(tf);

    /* LocalSettings + INI persistence */
    AppDataContainerIf *ls = NULL;
    assert(SUCCEEDED(ad->lpVtbl->get_LocalSettings(ad, (void **)&ls)));
    GenericIf *vals_ps = NULL;
    assert(SUCCEEDED(ls->lpVtbl->get_Values(ls, (void **)&vals_ps)));
    MapIf *vals_map = NULL;
    assert(SUCCEEDED(vals_ps->lpVtbl->QueryInterface((IInspectable *)vals_ps, &WINRT_IID_IMap_HSTRING_IInspectable, (void **)&vals_map)));

    void *pv_str = NULL, *pv_int = NULL;
    assert(SUCCEEDED(winrt_sys_create_property_value_string(L"Hello\\World\nLine2", &pv_str)));
    assert(SUCCEEDED(winrt_sys_create_property_value_int32(1337, &pv_int)));

    HSTRING k1 = make_hs(L"greeting"), k2 = make_hs(L"high_score");
    boolean replaced = 1;
    assert(SUCCEEDED(vals_map->lpVtbl->Insert(vals_map, k1, pv_str, &replaced)));
    assert(replaced == 0);
    assert(SUCCEEDED(vals_map->lpVtbl->Insert(vals_map, k2, pv_int, &replaced)));
    assert(replaced == 0);
    ((IUnknown *)pv_str)->lpVtbl->Release((IUnknown *)pv_str);
    ((IUnknown *)pv_int)->lpVtbl->Release((IUnknown *)pv_int);

    uint32_t sz = 0;
    assert(SUCCEEDED(vals_map->lpVtbl->get_Size(vals_map, &sz)));
    assert(sz == 2u);

    void *got_int_obj = NULL;
    assert(SUCCEEDED(vals_map->lpVtbl->Lookup(vals_map, k2, &got_int_obj)));
    typedef struct { const WinrtPropertyValueVtbl *lpVtbl; } PvIf;
    PvIf *got_pv = NULL;
    assert(SUCCEEDED(((IUnknown *)got_int_obj)->lpVtbl->QueryInterface((IUnknown *)got_int_obj, &WINRT_IID_IPropertyValue, (void **)&got_pv)));
    int32_t score = 0;
    assert(SUCCEEDED(got_pv->lpVtbl->GetInt32(got_pv, &score)));
    assert(score == 1337);
    got_pv->lpVtbl->Release(got_pv);
    ((IUnknown *)got_int_obj)->lpVtbl->Release((IUnknown *)got_int_obj);

    /* Verify INI file was actually persisted on disk */
    char ini_file[1024];
    snprintf(ini_file, sizeof(ini_file), "%s/drive_c/AppData/LocalState/uwp_local_settings.ini", sandbox);
    FILE *fp = fopen(ini_file, "rb");
    assert(fp != NULL);
    char ini_buf[1024];
    size_t rd = fread(ini_buf, 1, sizeof(ini_buf) - 1, fp);
    fclose(fp);
    ini_buf[rd] = '\0';
    assert(strstr(ini_buf, "high_score=i32:1337") != NULL);
    assert(strstr(ini_buf, "greeting=str:Hello\\\\World\\nLine2") != NULL);

    /* Version check */
    uint32_t ver = 99;
    assert(SUCCEEDED(ad->lpVtbl->get_Version(ad, &ver)));
    assert(ver == 0u);

    WindowsDeleteString(k1);
    WindowsDeleteString(k2);
    vals_map->lpVtbl->Release(vals_map);
    vals_ps->lpVtbl->Release((IInspectable *)vals_ps);
    ls->lpVtbl->Release(ls);
    ad->lpVtbl->Release(ad);
    ad_stat->lpVtbl->Release(ad_stat);
}

static void test_package(void) {
    uwp_package_identity_t id;
    memset(&id, 0, sizeof(id));
    id.valid = 1;
    id.ver_major = 1; id.ver_minor = 2; id.ver_build = 3; id.ver_revision = 4;
    id.architecture = 9;
    winrt_sys_copy_wstr(id.name, UWP_MAX_IDENTITY_CHARS, L"HedgePhysics.UWP");
    winrt_sys_copy_wstr(id.publisher, UWP_MAX_IDENTITY_CHARS, L"CN=SonicDev");
    winrt_sys_copy_wstr(id.publisher_id, 64, L"8wekyb3d8bbwe");
    winrt_sys_copy_wstr(id.full_name, UWP_MAX_IDENTITY_CHARS, L"HedgePhysics.UWP_1.2.3.4_x64__8wekyb3d8bbwe");
    winrt_sys_copy_wstr(id.family_name, UWP_MAX_IDENTITY_CHARS, L"HedgePhysics.UWP_8wekyb3d8bbwe");
    winrt_sys_copy_wstr(id.display_name, UWP_MAX_IDENTITY_CHARS, L"Hedge Physics");
    winrt_sys_copy_wstr(id.install_path, UWP_MAX_PATH_CHARS, L"GAME:\\");
    win32_host_shim_set_pkg_identity(&id);

    PackageStaticsIf *pstat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.ApplicationModel.Package", &WINRT_IID_IPackageStatics, (void **)&pstat)));
    PackageIf *pkg = NULL;
    assert(SUCCEEDED(pstat->lpVtbl->get_Current(pstat, (void **)&pkg)));

    PackageIdIf *pid = NULL;
    assert(SUCCEEDED(pkg->lpVtbl->get_Id(pkg, (void **)&pid)));
    HSTRING h = NULL;
    assert(SUCCEEDED(pid->lpVtbl->get_Name(pid, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"HedgePhysics.UWP") == 0);
    WindowsDeleteString(h);

    WinrtPackageVersion pv = { 0, 0, 0, 0 };
    assert(SUCCEEDED(pid->lpVtbl->get_Version(pid, &pv)));
    assert(pv.Major == 1 && pv.Minor == 2 && pv.Build == 3 && pv.Revision == 4);

    uint32_t arch = 0;
    assert(SUCCEEDED(pid->lpVtbl->get_Architecture(pid, &arch)));
    assert(arch == 9u);

    assert(SUCCEEDED(pid->lpVtbl->get_FullName(pid, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"HedgePhysics.UWP_1.2.3.4_x64__8wekyb3d8bbwe") == 0);
    WindowsDeleteString(h);

    assert(SUCCEEDED(pid->lpVtbl->get_FamilyName(pid, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"HedgePhysics.UWP_8wekyb3d8bbwe") == 0);
    WindowsDeleteString(h);

    assert(SUCCEEDED(pid->lpVtbl->get_Publisher(pid, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"CN=SonicDev") == 0);
    WindowsDeleteString(h);

    assert(SUCCEEDED(pid->lpVtbl->get_PublisherId(pid, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"8wekyb3d8bbwe") == 0);
    WindowsDeleteString(h);
    pid->lpVtbl->Release(pid);

    StorageFolderIf *inst_loc = NULL;
    assert(SUCCEEDED(pkg->lpVtbl->get_InstalledLocation(pkg, (void **)&inst_loc)));
    StorageItemIf *inst_item = NULL;
    assert(SUCCEEDED(inst_loc->lpVtbl->QueryInterface(inst_loc, &WINRT_IID_IStorageItem, (void **)&inst_item)));
    assert(SUCCEEDED(inst_item->lpVtbl->get_Path(inst_item, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"GAME:\\") == 0);
    WindowsDeleteString(h);
    inst_item->lpVtbl->Release(inst_item);
    inst_loc->lpVtbl->Release(inst_loc);

    Package2If *pkg2 = NULL;
    assert(SUCCEEDED(pkg->lpVtbl->QueryInterface(pkg, &WINRT_IID_IPackage2, (void **)&pkg2)));
    assert(SUCCEEDED(pkg2->lpVtbl->get_DisplayName(pkg2, &h)));
    assert(wcscmp(WindowsGetStringRawBuffer(h, NULL), L"Hedge Physics") == 0);
    WindowsDeleteString(h);

    /* Unimplemented get_Logo logs & returns E_NOTIMPL */
    win32_host_shim_reset_logs();
    void *logo = NULL;
    assert(pkg2->lpVtbl->get_Logo(pkg2, &logo) == E_NOTIMPL);
    assert(win32_host_shim_get_log_count() >= 1u);
    pkg2->lpVtbl->Release(pkg2);

    Package3If *pkg3 = NULL;
    assert(SUCCEEDED(pkg->lpVtbl->QueryInterface(pkg, &WINRT_IID_IPackage3, (void **)&pkg3)));
    void *pstatus = NULL;
    assert(SUCCEEDED(pkg3->lpVtbl->get_Status(pkg3, &pstatus)));
    assert(pstatus != NULL);
    ((IUnknown *)pstatus)->lpVtbl->Release((IUnknown *)pstatus);
    pkg3->lpVtbl->Release(pkg3);

    pkg->lpVtbl->Release(pkg);
    pstat->lpVtbl->Release(pstat);
    win32_host_shim_set_pkg_identity(NULL);
}

static void test_gamepad_backends(void) {
    GamepadStaticsIf *gp_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Gaming.Input.Gamepad", &WINRT_IID_IGamepadStatics, (void **)&gp_stat)));

    /* 1. Honest empty list when no controller is connected */
    winrt_sys_gamepad_test_reset();
    win32_host_shim_reset_controllers();
    VectorViewIf *vw = NULL;
    assert(SUCCEEDED(gp_stat->lpVtbl->get_Gamepads(gp_stat, (void **)&vw)));
    uint32_t cnt = 99;
    assert(SUCCEEDED(vw->lpVtbl->get_Size(vw, &cnt)));
    assert(cnt == 0u);
    vw->lpVtbl->Release(vw);

    /* Register hotplug callbacks */
    g_gp_added_events = 0;
    g_gp_removed_events = 0;
    EventRegistrationToken tok_add = { 0 }, tok_rem = { 0 };
    assert(SUCCEEDED(gp_stat->lpVtbl->add_GamepadAdded(gp_stat, &g_test_gp_added, &tok_add)));
    assert(SUCCEEDED(gp_stat->lpVtbl->add_GamepadRemoved(gp_stat, &g_test_gp_removed, &tok_rem)));

    /* 2. GameInput.dll connected controller */
    winrt_sys_gamepad_test_reset();
    win32_host_shim_reset_controllers();
    MockGiGamepadState gi_st;
    gi_st.buttons = 0x4u | 0x200u | 0x1u; /* A | DPadRight | Menu */
    gi_st.leftTrigger = 0.75f;
    gi_st.rightTrigger = 0.25f;
    gi_st.leftThumbstickX = -0.5f;
    gi_st.leftThumbstickY = 0.8f;
    gi_st.rightThumbstickX = 0.3f;
    gi_st.rightThumbstickY = -0.9f;
    gi_st.extendedButtons = 0;
    win32_host_shim_set_gameinput(1, 1, &gi_st, 987654321ULL);

    /* Re-register listeners after reset */
    assert(SUCCEEDED(gp_stat->lpVtbl->add_GamepadAdded(gp_stat, &g_test_gp_added, &tok_add)));
    assert(SUCCEEDED(gp_stat->lpVtbl->add_GamepadRemoved(gp_stat, &g_test_gp_removed, &tok_rem)));

    assert(SUCCEEDED(gp_stat->lpVtbl->get_Gamepads(gp_stat, (void **)&vw)));
    assert(SUCCEEDED(vw->lpVtbl->get_Size(vw, &cnt)));
    assert(cnt == 1u);
    assert(g_gp_added_events == 1);

    GamepadIf *pad0 = NULL;
    assert(SUCCEEDED(vw->lpVtbl->GetAt(vw, 0, (void **)&pad0)));
    WinrtGamepadReading reading;
    assert(SUCCEEDED(pad0->lpVtbl->GetCurrentReading(pad0, &reading)));
    assert(reading.Timestamp == 987654321ULL);
    assert(reading.Buttons == (0x4u | 0x200u | 0x1u));
    assert(fabs(reading.LeftTrigger - 0.75) < 0.001);
    assert(fabs(reading.RightTrigger - 0.25) < 0.001);
    assert(fabs(reading.LeftThumbstickX - (-0.5)) < 0.001);
    assert(fabs(reading.LeftThumbstickY - 0.8) < 0.001);

    WinrtGamepadVibration vib = { 0.6, 0.4, 0.2, 0.8 };
    assert(SUCCEEDED(pad0->lpVtbl->put_Vibration(pad0, vib)));
    int gi_rcalled = 0; float lf = 0, hf = 0, lt = 0, rt = 0;
    win32_host_shim_get_gameinput_rumble(&gi_rcalled, &lf, &hf, &lt, &rt);
    assert(gi_rcalled == 1);
    assert(fabs(lf - 0.6) < 0.01 && fabs(hf - 0.4) < 0.01 && fabs(lt - 0.2) < 0.01 && fabs(rt - 0.8) < 0.01);

    /* IGamepadStatics2::FromGameController */
    GamepadStatics2If *gp_stat2 = NULL;
    assert(SUCCEEDED(gp_stat->lpVtbl->QueryInterface(gp_stat, &WINRT_IID_IGamepadStatics2, (void **)&gp_stat2)));
    IUnknown *ctrl = NULL;
    assert(SUCCEEDED(pad0->lpVtbl->QueryInterface(pad0, &WINRT_IID_IGameController, (void **)&ctrl)));
    GamepadIf *from_ctrl = NULL;
    assert(SUCCEEDED(gp_stat2->lpVtbl->FromGameController(gp_stat2, ctrl, (void **)&from_ctrl)));
    assert(from_ctrl != NULL);
    from_ctrl->lpVtbl->Release(from_ctrl);
    ctrl->lpVtbl->Release(ctrl);
    gp_stat2->lpVtbl->Release(gp_stat2);

    pad0->lpVtbl->Release(pad0);
    vw->lpVtbl->Release(vw);

    /* Disconnect GameInput -> GamepadRemoved fires */
    win32_host_shim_set_gameinput(1, 0, &gi_st, 0);
    assert(SUCCEEDED(gp_stat->lpVtbl->get_Gamepads(gp_stat, (void **)&vw)));
    assert(SUCCEEDED(vw->lpVtbl->get_Size(vw, &cnt)));
    assert(cnt == 0u);
    assert(g_gp_removed_events == 1);
    vw->lpVtbl->Release(vw);

    /* 3. Fallback xinput1_4.dll with slots 0 and 2 connected */
    winrt_sys_gamepad_test_reset();
    win32_host_shim_reset_controllers();
    win32_host_shim_set_xinput(1, 0, 1, 0x1000u | 0x0100u /* A | LB */, 255, 128, 32767, -32768, 0, 16384);
    win32_host_shim_set_xinput(1, 2, 1, 0x8000u /* Y */, 0, 255, -16384, 0, 0, 0);

    assert(SUCCEEDED(gp_stat->lpVtbl->get_Gamepads(gp_stat, (void **)&vw)));
    assert(SUCCEEDED(vw->lpVtbl->get_Size(vw, &cnt)));
    assert(cnt == 2u);

    GamepadIf *xpad0 = NULL;
    assert(SUCCEEDED(vw->lpVtbl->GetAt(vw, 0, (void **)&xpad0)));
    assert(SUCCEEDED(xpad0->lpVtbl->GetCurrentReading(xpad0, &reading)));
    assert(reading.Buttons == (0x4u | 0x400u)); /* A | LeftShoulder */
    assert(fabs(reading.LeftTrigger - 1.0) < 0.001);
    assert(fabs(reading.RightTrigger - (128.0 / 255.0)) < 0.001);
    assert(fabs(reading.LeftThumbstickX - 1.0) < 0.001);
    assert(fabs(reading.LeftThumbstickY - (-1.0)) < 0.001);

    WinrtGamepadVibration xvib = { 1.0, 0.5, 0.0, 0.0 };
    assert(SUCCEEDED(xpad0->lpVtbl->put_Vibration(xpad0, xvib)));
    int xi_called = 0; WORD x_l = 0, x_r = 0;
    win32_host_shim_get_xinput_vib(0, &xi_called, &x_l, &x_r);
    assert(xi_called == 1 && x_l == 65535u && x_r >= 32767u && x_r <= 32768u);

    xpad0->lpVtbl->Release(xpad0);
    vw->lpVtbl->Release(vw);
    gp_stat->lpVtbl->Release(gp_stat);
    winrt_sys_gamepad_test_reset();
}

static void test_analytics_and_gamebar(void) {
    AnalyticsInfoStaticsIf *ai = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.System.Profile.AnalyticsInfo", &WINRT_IID_IAnalyticsInfoStatics, (void **)&ai)));
    AnalyticsVersionInfoIf *vi = NULL;
    assert(SUCCEEDED(ai->lpVtbl->get_VersionInfo(ai, (void **)&vi)));
    HSTRING df = NULL, dfv = NULL, form = NULL;
    assert(SUCCEEDED(vi->lpVtbl->get_DeviceFamily(vi, &df)));
    assert(SUCCEEDED(vi->lpVtbl->get_DeviceFamilyVersion(vi, &dfv)));
    assert(SUCCEEDED(ai->lpVtbl->get_DeviceForm(ai, &form)));
    assert(wcscmp(WindowsGetStringRawBuffer(df, NULL), L"Windows.Desktop") == 0);
    assert(wcscmp(WindowsGetStringRawBuffer(dfv, NULL), L"2814751014977536") == 0);
    assert(wcscmp(WindowsGetStringRawBuffer(form, NULL), L"Desktop") == 0);
    WindowsDeleteString(df);
    WindowsDeleteString(dfv);
    WindowsDeleteString(form);
    vi->lpVtbl->Release(vi);
    ai->lpVtbl->Release(ai);

    GameBarStaticsIf *gb = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Gaming.UI.GameBar", &WINRT_IID_IGameBarStatics, (void **)&gb)));
    boolean vis = 1, redir = 1;
    assert(SUCCEEDED(gb->lpVtbl->get_Visible(gb, &vis)));
    assert(SUCCEEDED(gb->lpVtbl->get_IsInputRedirected(gb, &redir)));
    assert(vis == 0 && redir == 0);

    EventRegistrationToken t1 = { 0 }, t2 = { 0 };
    assert(SUCCEEDED(gb->lpVtbl->add_VisibilityChanged(gb, &g_test_gp_added, &t1)));
    assert(t1.value != 0);
    assert(SUCCEEDED(gb->lpVtbl->add_IsInputRedirectedChanged(gb, &g_test_gp_removed, &t2)));
    assert(t2.value != 0 && t2.value != t1.value);
    assert(SUCCEEDED(gb->lpVtbl->remove_VisibilityChanged(gb, t1)));
    assert(SUCCEEDED(gb->lpVtbl->remove_IsInputRedirectedChanged(gb, t2)));
    gb->lpVtbl->Release(gb);
}

int main(void) {
    char tmp_tmpl[] = "/tmp/scarlett_winrt_sys_XXXXXX";
    char *sandbox = mkdtemp(tmp_tmpl);
    assert(sandbox != NULL);
    win32_host_shim_set_root(sandbox);

    test_factory_dispatch_and_no_silent_stubs();
    test_application_data_and_storage_folder(sandbox);
    test_package();
    test_gamepad_backends();
    test_analytics_and_gamebar();

    printf("ALL winrt_sys host unit tests PASSED (sandbox=%s)\n", sandbox);
    return 0;
}
