/*
 * Proton-Scarlett — Host Unit Tests for winrt_sys (UWPHOST.dll System WinRT)
 */

#define _POSIX_C_SOURCE 200809L

#include "../winrt_sys_internal.h"

#include <assert.h>
#include <math.h>
#include <pthread.h>
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

/* ============================================================================
 * Extended Tests: StorageFile, Streams, IStorageFolder2/IStorageItem2, ThreadPool
 * ============================================================================ */

typedef struct StorageFolderFullIf {
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
        HRESULT (STDMETHODCALLTYPE *GetFiles)(void *This, void **op);
        HRESULT (STDMETHODCALLTYPE *GetFolders)(void *This, void **op);
        HRESULT (STDMETHODCALLTYPE *GetItems)(void *This, void **op);
    } *lpVtbl;
} StorageFolderFullIf;

typedef struct StorageFolder2If {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *TryGetItemAsync)(void *This, HSTRING n, void **op);
    } *lpVtbl;
} StorageFolder2If;

typedef struct StorageItem2If {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetParentAsync)(void *This, void **op);
        HRESULT (STDMETHODCALLTYPE *IsEqual)(void *This, void *item, boolean *val);
    } *lpVtbl;
} StorageItem2If;

typedef struct StorageFileStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *GetFileFromPathAsync)(void *This, HSTRING path, void **op);
        HRESULT (STDMETHODCALLTYPE *GetFileFromApplicationUriAsync)(void *This, void *uri, void **op);
    } *lpVtbl;
} StorageFileStaticsIf;

typedef struct StorageFileIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_FileType)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *get_ContentType)(void *This, HSTRING *v);
        HRESULT (STDMETHODCALLTYPE *OpenAsync)(void *This, uint32_t accessMode, void **op);
    } *lpVtbl;
} StorageFileIf;

typedef struct RasRefIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *OpenReadAsync)(void *This, void **op);
    } *lpVtbl;
} RasRefIf;

typedef struct RasIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint64_t *v);
        HRESULT (STDMETHODCALLTYPE *put_Size)(void *This, uint64_t v);
        HRESULT (STDMETHODCALLTYPE *GetInputStreamAt)(void *This, uint64_t pos, void **s);
        HRESULT (STDMETHODCALLTYPE *GetOutputStreamAt)(void *This, uint64_t pos, void **s);
        HRESULT (STDMETHODCALLTYPE *get_Position)(void *This, uint64_t *v);
        HRESULT (STDMETHODCALLTYPE *Seek)(void *This, uint64_t pos);
        HRESULT (STDMETHODCALLTYPE *CloneStream)(void *This, void **s);
        HRESULT (STDMETHODCALLTYPE *get_CanRead)(void *This, boolean *v);
        HRESULT (STDMETHODCALLTYPE *get_CanWrite)(void *This, boolean *v);
    } *lpVtbl;
} RasIf;

typedef struct InputStreamIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *ReadAsync)(void *This, void *buf, uint32_t cnt, uint32_t opt, void **op);
    } *lpVtbl;
} InputStreamIf;

typedef struct OutputStreamIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *WriteAsync)(void *This, void *buf, void **op);
        HRESULT (STDMETHODCALLTYPE *FlushAsync)(void *This, void **op);
    } *lpVtbl;
} OutputStreamIf;

typedef struct ClosableIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *Close)(void *This);
    } *lpVtbl;
} ClosableIf;

typedef struct BasicPropsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint64_t *v);
        HRESULT (STDMETHODCALLTYPE *get_DateModified)(void *This, int64_t *v);
        HRESULT (STDMETHODCALLTYPE *get_ItemDate)(void *This, int64_t *v);
    } *lpVtbl;
} BasicPropsIf;

typedef struct IterableIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *First)(void *This, void **iter);
    } *lpVtbl;
} IterableIf;

typedef struct IteratorIf {
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
} IteratorIf;

typedef struct KvpIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
        HRESULT (STDMETHODCALLTYPE *get_Key)(void *This, HSTRING *k);
        HRESULT (STDMETHODCALLTYPE *get_Value)(void *This, void **v);
    } *lpVtbl;
} KvpIf;

typedef struct ThreadPoolStaticsIf {
    const struct {
        HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
        ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
        ULONG   (STDMETHODCALLTYPE *Release)(void *This);
        HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
        HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
        HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
        HRESULT (STDMETHODCALLTYPE *RunAsync)(void *This, void *handler, void **operation);
        HRESULT (STDMETHODCALLTYPE *RunWithPriorityAsync)(void *This, void *handler, int32_t priority, void **operation);
        HRESULT (STDMETHODCALLTYPE *RunWithPriorityAndOptionsAsync)(void *This, void *handler, int32_t priority, uint32_t options, void **operation);
    } *lpVtbl;
} ThreadPoolStaticsIf;

static void test_storage_file_streams_and_extended_apis(void) {
    AppDataStaticsIf *ad_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Storage.ApplicationData", &WINRT_IID_IApplicationDataStatics, (void **)&ad_stat)));
    AppDataIf *ad = NULL;
    assert(SUCCEEDED(ad_stat->lpVtbl->get_Current(ad_stat, (void **)&ad)));
    StorageFolderFullIf *local_folder = NULL;
    assert(SUCCEEDED(ad->lpVtbl->get_LocalFolder(ad, (void **)&local_folder)));

    /* IStorageFolder2::TryGetItemAsync on nonexistent file returns NULL */
    StorageFolder2If *lf2 = NULL;
    assert(SUCCEEDED(local_folder->lpVtbl->QueryInterface(local_folder, &WINRT_IID_IStorageFolder2, (void **)&lf2)));
    HSTRING h_miss = NULL;
    assert(SUCCEEDED(WindowsCreateString(L"no_such_item.txt", 16, &h_miss)));
    AsyncOpIf *op = NULL;
    assert(SUCCEEDED(lf2->lpVtbl->TryGetItemAsync(lf2, h_miss, (void **)&op)));
    void *miss_item = (void *)(uintptr_t)0x1;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, &miss_item)));
    assert(miss_item == NULL);
    op->lpVtbl->Release(op);
    WindowsDeleteString(h_miss);

    /* Create SubDir and data.bin */
    HSTRING h_sub = NULL, h_file = NULL;
    assert(SUCCEEDED(WindowsCreateString(L"StreamSub", 9, &h_sub)));
    assert(SUCCEEDED(WindowsCreateString(L"data.bin", 8, &h_file)));
    assert(SUCCEEDED(local_folder->lpVtbl->CreateFolder(local_folder, h_sub, 1 /* ReplaceExisting */, (void **)&op)));
    StorageFolderFullIf *sub_folder = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&sub_folder)));
    op->lpVtbl->Release(op);

    assert(SUCCEEDED(sub_folder->lpVtbl->CreateFile(sub_folder, h_file, 1 /* ReplaceExisting */, (void **)&op)));
    StorageFileIf *sfile = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&sfile)));
    op->lpVtbl->Release(op);

    /* TryGetItemAsync on existing data.bin */
    StorageFolder2If *sub2 = NULL;
    assert(SUCCEEDED(sub_folder->lpVtbl->QueryInterface(sub_folder, &WINRT_IID_IStorageFolder2, (void **)&sub2)));
    assert(SUCCEEDED(sub2->lpVtbl->TryGetItemAsync(sub2, h_file, (void **)&op)));
    StorageItemIf *found_item = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&found_item)));
    op->lpVtbl->Release(op);
    assert(found_item != NULL);

    /* IStorageItem2::IsEqual & GetParentAsync */
    StorageItem2If *item2 = NULL;
    assert(SUCCEEDED(found_item->lpVtbl->QueryInterface(found_item, &WINRT_IID_IStorageItem2, (void **)&item2)));
    boolean eq = 0;
    assert(SUCCEEDED(item2->lpVtbl->IsEqual(item2, sfile, &eq)));
    assert(eq == 1);
    assert(SUCCEEDED(item2->lpVtbl->IsEqual(item2, sub_folder, &eq)));
    assert(eq == 0);
    assert(SUCCEEDED(item2->lpVtbl->GetParentAsync(item2, (void **)&op)));
    StorageFolderFullIf *parent_folder = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&parent_folder)));
    op->lpVtbl->Release(op);
    assert(parent_folder != NULL);
    StorageItem2If *sub_i2 = NULL;
    assert(SUCCEEDED(sub_folder->lpVtbl->QueryInterface(sub_folder, &WINRT_IID_IStorageItem2, (void **)&sub_i2)));
    assert(SUCCEEDED(sub_i2->lpVtbl->IsEqual(sub_i2, parent_folder, &eq)));
    assert(eq == 1);
    sub_i2->lpVtbl->Release(sub_i2);
    parent_folder->lpVtbl->Release(parent_folder);

    /* Windows.Storage.StorageFile::GetFileFromPathAsync */
    StorageItemIf *file_item = NULL;
    assert(SUCCEEDED(sfile->lpVtbl->QueryInterface(sfile, &WINRT_IID_IStorageItem, (void **)&file_item)));
    HSTRING h_fpath = NULL;
    assert(SUCCEEDED(file_item->lpVtbl->get_Path(file_item, &h_fpath)));
    StorageFileStaticsIf *sf_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Storage.StorageFile", &WINRT_IID_IStorageFileStatics, (void **)&sf_stat)));
    assert(SUCCEEDED(sf_stat->lpVtbl->GetFileFromPathAsync(sf_stat, h_fpath, (void **)&op)));
    StorageFileIf *sfile_by_path = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&sfile_by_path)));
    op->lpVtbl->Release(op);
    assert(SUCCEEDED(item2->lpVtbl->IsEqual(item2, sfile_by_path, &eq)));
    assert(eq == 1);
    sfile_by_path->lpVtbl->Release(sfile_by_path);
    sf_stat->lpVtbl->Release(sf_stat);
    WindowsDeleteString(h_fpath);

    /* OpenAsync(ReadWrite=1) -> WriteAsync, FlushAsync, Seek, ReadAsync, Close */
    assert(SUCCEEDED(sfile->lpVtbl->OpenAsync(sfile, 1 /* ReadWrite */, (void **)&op)));
    RasIf *ras = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&ras)));
    op->lpVtbl->Release(op);

    void *wbuf = NULL;
    const uint8_t payload[] = "hello stream";
    assert(SUCCEEDED(winrt_sys_create_buffer(payload, 12u, &wbuf)));
    OutputStreamIf *os = NULL;
    assert(SUCCEEDED(ras->lpVtbl->QueryInterface(ras, &WINRT_IID_IOutputStream, (void **)&os)));
    assert(SUCCEEDED(os->lpVtbl->WriteAsync(os, wbuf, (void **)&op)));
    struct { const WinrtAsyncOpWithProgressU32Vtbl *lpVtbl; } *wop = (void *)op;
    uint32_t written = 0;
    assert(SUCCEEDED(wop->lpVtbl->GetResults(wop, &written)));
    assert(written == 12u);
    wop->lpVtbl->Release(wop);
    ((IUnknown *)wbuf)->lpVtbl->Release((IUnknown *)wbuf);

    void *fop = NULL;
    assert(SUCCEEDED(os->lpVtbl->FlushAsync(os, &fop)));
    struct { const WinrtAsyncOperationBoolVtbl *lpVtbl; } *fbop = fop;
    boolean flushed = 0;
    assert(SUCCEEDED(fbop->lpVtbl->GetResults(fbop, &flushed)));
    assert(flushed == 1);
    fbop->lpVtbl->Release(fbop);
    os->lpVtbl->Release(os);

    uint64_t sz = 0, pos = 99;
    assert(SUCCEEDED(ras->lpVtbl->get_Size(ras, &sz)));
    assert(sz == 12u);
    assert(SUCCEEDED(ras->lpVtbl->Seek(ras, 0u)));
    assert(SUCCEEDED(ras->lpVtbl->get_Position(ras, &pos)));
    assert(pos == 0u);

    uint8_t zeros[32] = {0};
    void *rbuf = NULL;
    assert(SUCCEEDED(winrt_sys_create_buffer(zeros, 32u, &rbuf)));
    InputStreamIf *is = NULL;
    assert(SUCCEEDED(ras->lpVtbl->QueryInterface(ras, &WINRT_IID_IInputStream, (void **)&is)));
    assert(SUCCEEDED(is->lpVtbl->ReadAsync(is, rbuf, 32u, 0u, (void **)&op)));
    void *res_buf = NULL;
    struct { const WinrtAsyncOpWithProgressPtrVtbl *lpVtbl; } *rop = (void *)op;
    assert(SUCCEEDED(rop->lpVtbl->GetResults(rop, &res_buf)));
    rop->lpVtbl->Release(rop);
    uint8_t *rptr = NULL;
    uint32_t rlen = 0;
    assert(SUCCEEDED(winrt_sys_read_buffer(res_buf, &rptr, &rlen)));
    assert(rlen == 12u && memcmp(rptr, "hello stream", 12u) == 0);
    ((IUnknown *)res_buf)->lpVtbl->Release((IUnknown *)res_buf);
    ((IUnknown *)rbuf)->lpVtbl->Release((IUnknown *)rbuf);
    is->lpVtbl->Release(is);

    ClosableIf *clos = NULL;
    assert(SUCCEEDED(ras->lpVtbl->QueryInterface(ras, &WINRT_IID_IClosable, (void **)&clos)));
    assert(SUCCEEDED(clos->lpVtbl->Close(clos)));
    clos->lpVtbl->Release(clos);
    ras->lpVtbl->Release(ras);

    /* IRandomAccessStreamReference::OpenReadAsync */
    RasRefIf *rref = NULL;
    assert(SUCCEEDED(sfile->lpVtbl->QueryInterface(sfile, &WINRT_IID_IRandomAccessStreamReference, (void **)&rref)));
    assert(SUCCEEDED(rref->lpVtbl->OpenReadAsync(rref, (void **)&op)));
    RasIf *ras2 = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&ras2)));
    op->lpVtbl->Release(op);
    assert(SUCCEEDED(ras2->lpVtbl->get_Size(ras2, &sz)));
    assert(sz == 12u);
    ras2->lpVtbl->Release(ras2);
    rref->lpVtbl->Release(rref);

    /* GetBasicPropertiesAsync */
    assert(SUCCEEDED(file_item->lpVtbl->Props(file_item, (void **)&op)));
    BasicPropsIf *bp = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, (void **)&bp)));
    op->lpVtbl->Release(op);
    uint64_t bsz = 0;
    int64_t bdm = 0;
    assert(SUCCEEDED(bp->lpVtbl->get_Size(bp, &bsz)));
    assert(SUCCEEDED(bp->lpVtbl->get_DateModified(bp, &bdm)));
    assert(bsz == 12u && bdm != 0);
    bp->lpVtbl->Release(bp);

    /* GetFilesAsync + IIterable<StorageFile*>::First */
    assert(SUCCEEDED(sub_folder->lpVtbl->GetFiles(sub_folder, (void **)&op)));
    void *fvec = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, &fvec)));
    op->lpVtbl->Release(op);
    IterableIf *fit = NULL;
    assert(SUCCEEDED(((IUnknown *)fvec)->lpVtbl->QueryInterface((IUnknown *)fvec, &WINRT_IID_IIterable_StorageFile, (void **)&fit)));
    IteratorIf *fiter = NULL;
    assert(SUCCEEDED(fit->lpVtbl->First(fit, (void **)&fiter)));
    boolean has = 0;
    assert(SUCCEEDED(fiter->lpVtbl->get_HasCurrent(fiter, &has)));
    assert(has == 1);
    void *listed_raw = NULL;
    assert(SUCCEEDED(fiter->lpVtbl->get_Current(fiter, &listed_raw)));
    StorageItemIf *listed = NULL;
    assert(SUCCEEDED(((IUnknown *)listed_raw)->lpVtbl->QueryInterface((IUnknown *)listed_raw, &WINRT_IID_IStorageItem, (void **)&listed)));
    HSTRING lname = NULL;
    assert(SUCCEEDED(listed->lpVtbl->get_Name(listed, &lname)));
    assert(wcscmp(WindowsGetStringRawBuffer(lname, NULL), L"data.bin") == 0);
    WindowsDeleteString(lname);
    listed->lpVtbl->Release(listed);
    ((IUnknown *)listed_raw)->lpVtbl->Release((IUnknown *)listed_raw);
    fiter->lpVtbl->Release(fiter);
    fit->lpVtbl->Release(fit);
    ((IUnknown *)fvec)->lpVtbl->Release((IUnknown *)fvec);

    /* RenameAsync + recursive DeleteAsync */
    HSTRING h_newname = NULL;
    assert(SUCCEEDED(WindowsCreateString(L"renamed.bin", 11, &h_newname)));
    void *ren_act = NULL;
    assert(SUCCEEDED(file_item->lpVtbl->Rename2(file_item, h_newname, 1 /* ReplaceExisting */, &ren_act)));
    ((IUnknown *)ren_act)->lpVtbl->Release((IUnknown *)ren_act);

    assert(SUCCEEDED(sub2->lpVtbl->TryGetItemAsync(sub2, h_file, (void **)&op)));
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, &miss_item)));
    op->lpVtbl->Release(op);
    assert(miss_item == NULL);

    assert(SUCCEEDED(sub2->lpVtbl->TryGetItemAsync(sub2, h_newname, (void **)&op)));
    void *ren_item = NULL;
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, &ren_item)));
    op->lpVtbl->Release(op);
    assert(ren_item != NULL);
    ((IUnknown *)ren_item)->lpVtbl->Release((IUnknown *)ren_item);
    WindowsDeleteString(h_newname);

    StorageItemIf *sub_item = NULL;
    assert(SUCCEEDED(sub_folder->lpVtbl->QueryInterface(sub_folder, &WINRT_IID_IStorageItem, (void **)&sub_item)));
    void *del_act = NULL;
    assert(SUCCEEDED(sub_item->lpVtbl->Del1(sub_item, &del_act)));
    ((IUnknown *)del_act)->lpVtbl->Release((IUnknown *)del_act);
    sub_item->lpVtbl->Release(sub_item);

    assert(SUCCEEDED(lf2->lpVtbl->TryGetItemAsync(lf2, h_sub, (void **)&op)));
    assert(SUCCEEDED(op->lpVtbl->GetResults(op, &miss_item)));
    op->lpVtbl->Release(op);
    assert(miss_item == NULL);

    item2->lpVtbl->Release(item2);
    file_item->lpVtbl->Release(file_item);
    found_item->lpVtbl->Release(found_item);
    sfile->lpVtbl->Release(sfile);
    sub2->lpVtbl->Release(sub2);
    sub_folder->lpVtbl->Release(sub_folder);
    WindowsDeleteString(h_file);
    WindowsDeleteString(h_sub);
    lf2->lpVtbl->Release(lf2);
    local_folder->lpVtbl->Release(local_folder);
    ad->lpVtbl->Release(ad);
    ad_stat->lpVtbl->Release(ad_stat);
}

typedef struct TestWorkHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *operation);
} TestWorkHandlerVtbl;

typedef struct TestWorkHandler {
    const TestWorkHandlerVtbl *lpVtbl;
    volatile LONG ref_count;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int invoked;
    HRESULT result_to_return;
} TestWorkHandler;

static HRESULT STDMETHODCALLTYPE twh_qi(void *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) || winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject)) {
        InterlockedIncrement(&((TestWorkHandler *)This)->ref_count);
        *ppv = This;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE twh_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((TestWorkHandler *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE twh_release(void *This) {
    return (ULONG)InterlockedDecrement(&((TestWorkHandler *)This)->ref_count);
}
static HRESULT STDMETHODCALLTYPE twh_invoke(void *This, void *operation) {
    (void)operation;
    TestWorkHandler *w = (TestWorkHandler *)This;
    pthread_mutex_lock(&w->mu);
    w->invoked++;
    pthread_cond_broadcast(&w->cv);
    pthread_mutex_unlock(&w->mu);
    return w->result_to_return;
}

typedef struct TestActDoneHandler {
    const WinrtAsyncActionCompletedHandlerVtbl *lpVtbl;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int called;
    AsyncStatus status;
} TestActDoneHandler;

static HRESULT STDMETHODCALLTYPE tadh_qi(void *This, REFIID riid, void **ppv) {
    (void)riid; *ppv = This; return S_OK;
}
static ULONG STDMETHODCALLTYPE tadh_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE tadh_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE tadh_invoke(void *This, void *action, AsyncStatus status) {
    (void)action;
    TestActDoneHandler *h = (TestActDoneHandler *)This;
    pthread_mutex_lock(&h->mu);
    h->called++;
    h->status = status;
    pthread_cond_broadcast(&h->cv);
    pthread_mutex_unlock(&h->mu);
    return S_OK;
}

static void test_extended_settings_and_threadpool(void) {
    /* 1. ApplicationDataContainer::get_Containers KVP QI + extended PropertyValue types */
    AppDataStaticsIf *ad_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Storage.ApplicationData", &WINRT_IID_IApplicationDataStatics, (void **)&ad_stat)));
    AppDataIf *ad = NULL;
    assert(SUCCEEDED(ad_stat->lpVtbl->get_Current(ad_stat, (void **)&ad)));
    AppDataContainerIf *ls = NULL;
    assert(SUCCEEDED(ad->lpVtbl->get_LocalSettings(ad, (void **)&ls)));

    HSTRING h_cname = NULL;
    assert(SUCCEEDED(WindowsCreateString(L"TypedProps", 10, &h_cname)));
    AppDataContainerIf *subc = NULL;
    assert(SUCCEEDED(ls->lpVtbl->CreateContainer(ls, h_cname, 0 /* Always */, (void **)&subc)));
    void *subvals_raw = NULL;
    assert(SUCCEEDED(subc->lpVtbl->get_Values(subc, &subvals_raw)));
    MapIf *subvals = NULL;
    assert(SUCCEEDED(((IUnknown *)subvals_raw)->lpVtbl->QueryInterface((IUnknown *)subvals_raw, &WINRT_IID_IMap_HSTRING_IInspectable, (void **)&subvals)));

    void *pv_u8 = NULL, *pv_i16 = NULL, *pv_guid = NULL, *pv_dt = NULL;
    GUID g_sample = { 0x12345678u, 0x9abcu, 0xdef0u, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    assert(SUCCEEDED(winrt_sys_create_property_value_uint8(42u, &pv_u8)));
    assert(SUCCEEDED(winrt_sys_create_property_value_int16(-321, &pv_i16)));
    assert(SUCCEEDED(winrt_sys_create_property_value_guid(&g_sample, &pv_guid)));
    assert(SUCCEEDED(winrt_sys_create_property_value_datetime(9876543210LL, &pv_dt)));
    HSTRING k1 = NULL, k2 = NULL, k3 = NULL, k4 = NULL;
    assert(SUCCEEDED(WindowsCreateString(L"k_u8", 4, &k1)));
    assert(SUCCEEDED(WindowsCreateString(L"k_i16", 5, &k2)));
    assert(SUCCEEDED(WindowsCreateString(L"k_guid", 6, &k3)));
    assert(SUCCEEDED(WindowsCreateString(L"k_dt", 4, &k4)));
    boolean rep = 0;
    assert(SUCCEEDED(subvals->lpVtbl->Insert(subvals, k1, pv_u8, &rep)));
    assert(SUCCEEDED(subvals->lpVtbl->Insert(subvals, k2, pv_i16, &rep)));
    assert(SUCCEEDED(subvals->lpVtbl->Insert(subvals, k3, pv_guid, &rep)));
    assert(SUCCEEDED(subvals->lpVtbl->Insert(subvals, k4, pv_dt, &rep)));
    ((IUnknown *)pv_u8)->lpVtbl->Release((IUnknown *)pv_u8);
    ((IUnknown *)pv_i16)->lpVtbl->Release((IUnknown *)pv_i16);
    ((IUnknown *)pv_guid)->lpVtbl->Release((IUnknown *)pv_guid);
    ((IUnknown *)pv_dt)->lpVtbl->Release((IUnknown *)pv_dt);
    WindowsDeleteString(k1);
    WindowsDeleteString(k2);
    WindowsDeleteString(k3);
    WindowsDeleteString(k4);

    void *cview = NULL;
    assert(SUCCEEDED(ls->lpVtbl->get_Containers(ls, &cview)));
    IterableIf *cit = NULL;
    assert(SUCCEEDED(((IUnknown *)cview)->lpVtbl->QueryInterface((IUnknown *)cview, &WINRT_IID_IIterable_KVP_HSTRING_AppDataContainer, (void **)&cit)));
    IteratorIf *citer = NULL;
    assert(SUCCEEDED(cit->lpVtbl->First(cit, (void **)&citer)));
    boolean chas = 0;
    assert(SUCCEEDED(citer->lpVtbl->get_HasCurrent(citer, &chas)));
    assert(chas == 1);
    void *kvp_raw = NULL;
    assert(SUCCEEDED(citer->lpVtbl->get_Current(citer, &kvp_raw)));
    KvpIf *kvp = NULL;
    assert(SUCCEEDED(((IUnknown *)kvp_raw)->lpVtbl->QueryInterface((IUnknown *)kvp_raw, &WINRT_IID_IKeyValuePair_HSTRING_AppDataContainer, (void **)&kvp)));
    kvp->lpVtbl->Release(kvp);
    ((IUnknown *)kvp_raw)->lpVtbl->Release((IUnknown *)kvp_raw);
    citer->lpVtbl->Release(citer);
    cit->lpVtbl->Release(cit);
    ((IUnknown *)cview)->lpVtbl->Release((IUnknown *)cview);

    subvals->lpVtbl->Release(subvals);
    ((IUnknown *)subvals_raw)->lpVtbl->Release((IUnknown *)subvals_raw);
    subc->lpVtbl->Release(subc);
    WindowsDeleteString(h_cname);
    ls->lpVtbl->Release(ls);
    ad->lpVtbl->Release(ad);
    ad_stat->lpVtbl->Release(ad_stat);

    /* 2. Windows.System.Threading.ThreadPool::RunAsync */
    static const GUID iid_tp_statics =
        { 0xb6bf67ddu, 0x84bdu, 0x44f8u, { 0xacu, 0x1cu, 0x93u, 0xebu, 0xcbu, 0x9du, 0xbau, 0x91u } };
    ThreadPoolStaticsIf *tp = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.System.Threading.ThreadPool", &iid_tp_statics, (void **)&tp)));

    static const TestWorkHandlerVtbl work_vtbl = { twh_qi, twh_addref, twh_release, twh_invoke };
    TestWorkHandler wh = { &work_vtbl, 1, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, S_OK };

    static const WinrtAsyncActionCompletedHandlerVtbl done_vtbl = {
        tadh_qi, tadh_addref, tadh_release, tadh_invoke
    };
    TestActDoneHandler dh = { &done_vtbl, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, Started };

    struct { const WinrtAsyncActionVtbl *lpVtbl; } *act = NULL;
    assert(SUCCEEDED(tp->lpVtbl->RunAsync(tp, &wh, (void **)&act)));
    assert(act != NULL);
    assert(SUCCEEDED(act->lpVtbl->put_Completed(act, &dh)));

    pthread_mutex_lock(&dh.mu);
    while (dh.called == 0) pthread_cond_wait(&dh.cv, &dh.mu);
    pthread_mutex_unlock(&dh.mu);

    assert(wh.invoked == 1);
    assert(dh.status == Completed);
    assert(act->lpVtbl->GetResults(act) == S_OK);
    act->lpVtbl->Release(act);
    tp->lpVtbl->Release(tp);
}

typedef struct TestTimerHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(void *This, void *timer);
} TestTimerHandlerVtbl;

typedef struct TestTimerHandler {
    const TestTimerHandlerVtbl *lpVtbl;
    volatile LONG ref_count;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int fired;
} TestTimerHandler;

static HRESULT STDMETHODCALLTYPE tth_qi(void *This, REFIID riid, void **ppv) {
    (void)riid; *ppv = This; InterlockedIncrement(&((TestTimerHandler *)This)->ref_count); return S_OK;
}
static ULONG STDMETHODCALLTYPE tth_ar(void *This) {
    return (ULONG)InterlockedIncrement(&((TestTimerHandler *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE tth_rel(void *This) {
    return (ULONG)InterlockedDecrement(&((TestTimerHandler *)This)->ref_count);
}
static HRESULT STDMETHODCALLTYPE tth_invoke(void *This, void *timer) {
    (void)timer;
    TestTimerHandler *h = (TestTimerHandler *)This;
    pthread_mutex_lock(&h->mu);
    h->fired++;
    pthread_cond_broadcast(&h->cv);
    pthread_mutex_unlock(&h->mu);
    return S_OK;
}

static void test_devices_and_system_status(void) {
    /* 1. Windows.Devices.Enumeration.Pnp.PnpObject */
    static const GUID iid_pnp_statics =
        { 0xb3c32a3du, 0xd168u, 0x4660u, { 0xbbu, 0xf3u, 0xa7u, 0x33u, 0xb1u, 0x4bu, 0x6eu, 0x01u } };
    static const GUID iid_pnp_coll_vv =
        { 0xcce5a798u, 0xd269u, 0x5fceu, { 0x99u, 0xceu, 0xefu, 0x0au, 0xe3u, 0xcdu, 0x05u, 0x69u } };
    static const GUID iid_pnp_coll_it =
        { 0x30b50092u, 0x36eeu, 0x53ffu, { 0x94u, 0x50u, 0x02u, 0x90u, 0x04u, 0x43u, 0x6cu, 0x60u } };
    static const GUID iid_pnp_aop =
        { 0x0659eab6u, 0x97a1u, 0x54fcu, { 0x93u, 0x56u, 0xffu, 0x15u, 0xe3u, 0xb0u, 0xa8u, 0xa8u } };

    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *CreateFromIdAsync)(void *This, uint32_t t, HSTRING id, void *rp, void **op);
            HRESULT (STDMETHODCALLTYPE *FindAllAsync)(void *This, uint32_t t, void *rp, void **op);
            HRESULT (STDMETHODCALLTYPE *FindAllAsyncAqs)(void *This, uint32_t t, void *rp, HSTRING aqs, void **op);
            HRESULT (STDMETHODCALLTYPE *CreateWatcher)(void *This, uint32_t t, void *rp, void **w);
        } *lpVtbl;
    } *pnp_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Devices.Enumeration.Pnp.PnpObject", &iid_pnp_statics, (void **)&pnp_stat)));

    AsyncOpIf *pnp_op = NULL;
    assert(SUCCEEDED(pnp_stat->lpVtbl->FindAllAsync(pnp_stat, 3 /* DeviceContainer */, NULL, (void **)&pnp_op)));
    AsyncOpIf *pnp_op_qi = NULL;
    assert(SUCCEEDED(pnp_op->lpVtbl->QueryInterface(pnp_op, &iid_pnp_aop, (void **)&pnp_op_qi)));
    pnp_op_qi->lpVtbl->Release(pnp_op_qi);

    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t i, void **v);
            HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *sz);
        } *lpVtbl;
    } *pnp_coll = NULL;
    assert(SUCCEEDED(pnp_op->lpVtbl->GetResults(pnp_op, (void **)&pnp_coll)));
    pnp_op->lpVtbl->Release(pnp_op);
    assert(pnp_coll != NULL);
    void *pnp_vv_qi = NULL;
    assert(SUCCEEDED(pnp_coll->lpVtbl->QueryInterface(pnp_coll, &iid_pnp_coll_vv, &pnp_vv_qi)));
    ((IUnknown *)pnp_vv_qi)->lpVtbl->Release((IUnknown *)pnp_vv_qi);
    uint32_t pnp_sz = 99u;
    assert(SUCCEEDED(pnp_coll->lpVtbl->get_Size(pnp_coll, &pnp_sz)));
    assert(pnp_sz == 0u);
    IterableIf *pnp_it = NULL;
    assert(SUCCEEDED(pnp_coll->lpVtbl->QueryInterface(pnp_coll, &iid_pnp_coll_it, (void **)&pnp_it)));
    IteratorIf *pnp_iter = NULL;
    assert(SUCCEEDED(pnp_it->lpVtbl->First(pnp_it, (void **)&pnp_iter)));
    boolean pnp_has = 1;
    assert(SUCCEEDED(pnp_iter->lpVtbl->get_HasCurrent(pnp_iter, &pnp_has)));
    assert(pnp_has == 0);
    pnp_iter->lpVtbl->Release(pnp_iter);
    pnp_it->lpVtbl->Release(pnp_it);
    pnp_coll->lpVtbl->Release(pnp_coll);
    pnp_stat->lpVtbl->Release(pnp_stat);

    /* 2. HidDevice */
    static const GUID iid_hid_stat =
        { 0x9e5981e4u, 0x9856u, 0x418cu, { 0x9fu, 0x73u, 0x77u, 0xdeu, 0x0cu, 0xd8u, 0x57u, 0x54u } };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *GetDeviceSelector)(void *This, uint16_t up, uint16_t ui, HSTRING *s);
            HRESULT (STDMETHODCALLTYPE *GetDeviceSelectorVP)(void *This, uint16_t up, uint16_t ui, uint16_t v, uint16_t p, HSTRING *s);
            HRESULT (STDMETHODCALLTYPE *FromIdAsync)(void *This, HSTRING id, uint32_t mode, void **op);
        } *lpVtbl;
    } *hid_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Devices.HumanInterfaceDevice.HidDevice", &iid_hid_stat, (void **)&hid_stat)));
    HSTRING hsel = NULL;
    assert(SUCCEEDED(hid_stat->lpVtbl->GetDeviceSelector(hid_stat, 1u, 4u, &hsel)));
    assert(wcsstr(WindowsGetStringRawBuffer(hsel, NULL), L"4D1E55B2") != NULL);
    AsyncOpIf *hid_op = NULL;
    assert(SUCCEEDED(hid_stat->lpVtbl->FromIdAsync(hid_stat, hsel, 0u, (void **)&hid_op)));
    void *hid_res = (void *)(uintptr_t)0x1;
    assert(SUCCEEDED(hid_op->lpVtbl->GetResults(hid_op, &hid_res)));
    assert(hid_res == NULL);
    hid_op->lpVtbl->Release(hid_op);
    WindowsDeleteString(hsel);
    hid_stat->lpVtbl->Release(hid_stat);

    /* 3. TouchCapabilities / MouseCapabilities / KeyboardCapabilities */
    static const GUID iid_touch =
        { 0x20dd55f9u, 0x13f1u, 0x46c8u, { 0x92u, 0x85u, 0x2cu, 0x05u, 0xfau, 0x3eu, 0xdau, 0x6fu } };
    static const GUID iid_mouse =
        { 0xbca5e023u, 0x7dd9u, 0x4b6bu, { 0x9au, 0x92u, 0x55u, 0xd4u, 0x3cu, 0xb3u, 0x8fu, 0x73u } };
    static const GUID iid_kbd =
        { 0x3a3f9b56u, 0x6798u, 0x4bbcu, { 0x83u, 0x3eu, 0x0fu, 0x34u, 0xb1u, 0x7cu, 0x65u, 0xffu } };
    struct { const WinrtActivationFactoryVtbl *lpVtbl; } *fac = NULL;
    IInspectable *inst = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Devices.Input.TouchCapabilities", &WINRT_IID_IActivationFactory, (void **)&fac)));
    assert(SUCCEEDED(fac->lpVtbl->ActivateInstance(fac, &inst)));
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_TouchPresent)(void *This, int32_t *v);
            HRESULT (STDMETHODCALLTYPE *get_Contacts)(void *This, uint32_t *v);
        } *lpVtbl;
    } *touch = NULL;
    assert(SUCCEEDED(inst->lpVtbl->QueryInterface(inst, &iid_touch, (void **)&touch)));
    int32_t present = -1; uint32_t contacts = 99u;
    assert(SUCCEEDED(touch->lpVtbl->get_TouchPresent(touch, &present)));
    assert(SUCCEEDED(touch->lpVtbl->get_Contacts(touch, &contacts)));
    assert(present == 0 && contacts == 0u);
    touch->lpVtbl->Release(touch);
    inst->lpVtbl->Release(inst);
    fac->lpVtbl->Release(fac);

    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Devices.Input.MouseCapabilities", &WINRT_IID_IActivationFactory, (void **)&fac)));
    assert(SUCCEEDED(fac->lpVtbl->ActivateInstance(fac, &inst)));
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_MousePresent)(void *This, int32_t *v);
        } *lpVtbl;
    } *mouse = NULL;
    assert(SUCCEEDED(inst->lpVtbl->QueryInterface(inst, &iid_mouse, (void **)&mouse)));
    assert(SUCCEEDED(mouse->lpVtbl->get_MousePresent(mouse, &present)));
    assert(present == 1);
    mouse->lpVtbl->Release(mouse);
    inst->lpVtbl->Release(inst);
    fac->lpVtbl->Release(fac);

    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Devices.Input.KeyboardCapabilities", &WINRT_IID_IActivationFactory, (void **)&fac)));
    assert(SUCCEEDED(fac->lpVtbl->ActivateInstance(fac, &inst)));
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_KeyboardPresent)(void *This, int32_t *v);
        } *lpVtbl;
    } *kbd = NULL;
    assert(SUCCEEDED(inst->lpVtbl->QueryInterface(inst, &iid_kbd, (void **)&kbd)));
    assert(SUCCEEDED(kbd->lpVtbl->get_KeyboardPresent(kbd, &present)));
    assert(present == 1);
    kbd->lpVtbl->Release(kbd);
    inst->lpVtbl->Release(inst);
    fac->lpVtbl->Release(fac);

    /* 4. HardwareIdentification */
    static const GUID iid_hwid_stat =
        { 0x971260e0u, 0xf170u, 0x4a42u, { 0xbdu, 0x55u, 0xa9u, 0x00u, 0xb2u, 0x12u, 0xdau, 0xe2u } };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *GetPackageSpecificToken)(void *This, void *nonce, void **tok);
        } *lpVtbl;
    } *hwid = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.System.Profile.HardwareIdentification", &iid_hwid_stat, (void **)&hwid)));
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_Id)(void *This, void **b);
            HRESULT (STDMETHODCALLTYPE *get_Signature)(void *This, void **b);
            HRESULT (STDMETHODCALLTYPE *get_Certificate)(void *This, void **b);
        } *lpVtbl;
    } *hwt = NULL;
    assert(SUCCEEDED(hwid->lpVtbl->GetPackageSpecificToken(hwid, NULL, (void **)&hwt)));
    void *id_buf = NULL;
    assert(SUCCEEDED(hwt->lpVtbl->get_Id(hwt, &id_buf)));
    uint8_t *id_ptr = NULL; uint32_t id_len = 0;
    assert(SUCCEEDED(winrt_sys_read_buffer(id_buf, &id_ptr, &id_len)));
    assert(id_len == 16u);
    ((IUnknown *)id_buf)->lpVtbl->Release((IUnknown *)id_buf);
    hwt->lpVtbl->Release(hwt);
    hwid->lpVtbl->Release(hwid);

    /* 5. MemoryManager & PowerManager */
    static const GUID iid_mem_stat =
        { 0x5c6c279cu, 0xd7cau, 0x4779u, { 0x91u, 0x88u, 0x40u, 0x57u, 0x21u, 0x9cu, 0xe6u, 0x4cu } };
    static const GUID iid_pwr_stat =
        { 0x1394825du, 0x62ceu, 0x4364u, { 0x98u, 0xd5u, 0xaau, 0x28u, 0xc7u, 0xfbu, 0xd1u, 0x5bu } };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_AppMemoryUsage)(void *This, uint64_t *v);
            HRESULT (STDMETHODCALLTYPE *get_AppMemoryUsageLimit)(void *This, uint64_t *v);
        } *lpVtbl;
    } *mem_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.System.MemoryManager", &iid_mem_stat, (void **)&mem_stat)));
    uint64_t usage = 0, limit = 0;
    assert(SUCCEEDED(mem_stat->lpVtbl->get_AppMemoryUsage(mem_stat, &usage)));
    assert(SUCCEEDED(mem_stat->lpVtbl->get_AppMemoryUsageLimit(mem_stat, &limit)));
    assert(usage > 0 && limit > usage);
    mem_stat->lpVtbl->Release(mem_stat);

    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_EnergySaverStatus)(void *This, uint32_t *v);
            HRESULT (STDMETHODCALLTYPE *add_ES)(void *This, void *h, EventRegistrationToken *t);
            HRESULT (STDMETHODCALLTYPE *rem_ES)(void *This, EventRegistrationToken t);
            HRESULT (STDMETHODCALLTYPE *get_BatteryStatus)(void *This, uint32_t *v);
            HRESULT (STDMETHODCALLTYPE *add_BS)(void *This, void *h, EventRegistrationToken *t);
            HRESULT (STDMETHODCALLTYPE *rem_BS)(void *This, EventRegistrationToken t);
            HRESULT (STDMETHODCALLTYPE *get_PowerSupplyStatus)(void *This, uint32_t *v);
        } *lpVtbl;
    } *pwr_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.System.Power.PowerManager", &iid_pwr_stat, (void **)&pwr_stat)));
    uint32_t bat = 99u, pwr = 99u;
    assert(SUCCEEDED(pwr_stat->lpVtbl->get_BatteryStatus(pwr_stat, &bat)));
    assert(SUCCEEDED(pwr_stat->lpVtbl->get_PowerSupplyStatus(pwr_stat, &pwr)));
    assert(bat == 0u && pwr == 2u);
    pwr_stat->lpVtbl->Release(pwr_stat);

    /* 6. ThreadPoolTimer */
    static const GUID iid_tpt_stat =
        { 0x1a8a9d02u, 0xe482u, 0x461bu, { 0xb8u, 0xc7u, 0x8eu, 0xfau, 0xd1u, 0xccu, 0xe5u, 0x90u } };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *CreatePeriodicTimer)(void *This, void *h, int64_t p, void **t);
            HRESULT (STDMETHODCALLTYPE *CreateTimer)(void *This, void *h, int64_t d, void **t);
        } *lpVtbl;
    } *tpt_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.System.Threading.ThreadPoolTimer", &iid_tpt_stat, (void **)&tpt_stat)));
    static const TestTimerHandlerVtbl th_vtbl = { tth_qi, tth_ar, tth_rel, tth_invoke };
    TestTimerHandler th = { &th_vtbl, 1, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_Period)(void *This, int64_t *v);
            HRESULT (STDMETHODCALLTYPE *get_Delay)(void *This, int64_t *v);
            HRESULT (STDMETHODCALLTYPE *Cancel)(void *This);
        } *lpVtbl;
    } *tmr = NULL;
    assert(SUCCEEDED(tpt_stat->lpVtbl->CreateTimer(tpt_stat, &th, 10000LL /* 1 ms */, (void **)&tmr)));
    pthread_mutex_lock(&th.mu);
    while (th.fired == 0) pthread_cond_wait(&th.cv, &th.mu);
    pthread_mutex_unlock(&th.mu);
    assert(th.fired == 1);
    assert(SUCCEEDED(tmr->lpVtbl->Cancel(tmr)));
    tmr->lpVtbl->Release(tmr);
    tpt_stat->lpVtbl->Release(tpt_stat);

    /* 7. Windows.Networking.Connectivity.NetworkInformation & Windows.Networking.HostName */
    static const GUID iid_net_stat =
        { 0x5074f851u, 0x950du, 0x4165u, { 0x9cu, 0x15u, 0x36u, 0x56u, 0x19u, 0x48u, 0x1eu, 0xeau } };
    static const GUID iid_hn_vec =
        { 0xf4706ab1u, 0x55a3u, 0x5270u, { 0xafu, 0xb2u, 0x73u, 0x29u, 0x88u, 0xfeu, 0x82u, 0x27u } };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *GetConnectionProfiles)(void *This, void **v);
            HRESULT (STDMETHODCALLTYPE *GetInternetConnectionProfile)(void *This, void **v);
            HRESULT (STDMETHODCALLTYPE *GetLanIdentifiers)(void *This, void **v);
            HRESULT (STDMETHODCALLTYPE *GetHostNames)(void *This, void **v);
        } *lpVtbl;
    } *net_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Networking.Connectivity.NetworkInformation", &iid_net_stat, (void **)&net_stat)));

    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_ProfileName)(void *This, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *GetNetworkConnectivityLevel)(void *This, uint32_t *v);
        } *lpVtbl;
    } *cp = NULL;
    assert(SUCCEEDED(net_stat->lpVtbl->GetInternetConnectionProfile(net_stat, (void **)&cp)));
    uint32_t lvl = 0;
    assert(SUCCEEDED(cp->lpVtbl->GetNetworkConnectivityLevel(cp, &lvl)));
    assert(lvl == 3u);
    cp->lpVtbl->Release(cp);

    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t idx, void **v);
            HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *sz);
        } *lpVtbl;
    } *hvec = NULL;
    assert(SUCCEEDED(net_stat->lpVtbl->GetHostNames(net_stat, (void **)&hvec)));
    void *hvec_qi = NULL;
    assert(SUCCEEDED(hvec->lpVtbl->QueryInterface(hvec, &iid_hn_vec, &hvec_qi)));
    ((IUnknown *)hvec_qi)->lpVtbl->Release((IUnknown *)hvec_qi);
    uint32_t hsz = 0;
    assert(SUCCEEDED(hvec->lpVtbl->get_Size(hvec, &hsz)));
    assert(hsz == 1u);
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *get_IPInformation)(void *This, void **v);
            HRESULT (STDMETHODCALLTYPE *get_RawName)(void *This, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *get_DisplayName)(void *This, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *get_CanonicalName)(void *This, HSTRING *v);
        } *lpVtbl;
    } *hn = NULL;
    assert(SUCCEEDED(hvec->lpVtbl->GetAt(hvec, 0u, (void **)&hn)));
    void *ipinfo = (void *)(uintptr_t)0x1;
    assert(SUCCEEDED(hn->lpVtbl->get_IPInformation(hn, &ipinfo)));
    assert(ipinfo == NULL);
    HSTRING dname = NULL;
    assert(SUCCEEDED(hn->lpVtbl->get_DisplayName(hn, &dname)));
    assert(wcscmp(WindowsGetStringRawBuffer(dname, NULL), L"localhost") == 0);
    WindowsDeleteString(dname);
    hn->lpVtbl->Release(hn);
    hvec->lpVtbl->Release(hvec);
    net_stat->lpVtbl->Release(net_stat);

    /* 8. Windows.Media.Devices.MediaDevice */
    static const GUID iid_md_stat =
        { 0xaa2d9a40u, 0x909fu, 0x4bbau, { 0xbfu, 0x8bu, 0x0cu, 0x0du, 0x29u, 0x6fu, 0x14u, 0xf0u } };
    struct {
        const struct {
            HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppv);
            ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
            ULONG   (STDMETHODCALLTYPE *Release)(void *This);
            HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *c, IID **i);
            HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *n);
            HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *tl);
            HRESULT (STDMETHODCALLTYPE *GetAudioCaptureSelector)(void *This, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *GetAudioRenderSelector)(void *This, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *GetVideoCaptureSelector)(void *This, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *GetDefaultAudioCaptureId)(void *This, uint32_t role, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *GetDefaultAudioRenderId)(void *This, uint32_t role, HSTRING *v);
            HRESULT (STDMETHODCALLTYPE *add_Cap)(void *This, void *h, EventRegistrationToken *tok);
            HRESULT (STDMETHODCALLTYPE *rem_Cap)(void *This, EventRegistrationToken tok);
            HRESULT (STDMETHODCALLTYPE *add_Ren)(void *This, void *h, EventRegistrationToken *tok);
            HRESULT (STDMETHODCALLTYPE *rem_Ren)(void *This, EventRegistrationToken tok);
        } *lpVtbl;
    } *md_stat = NULL;
    assert(SUCCEEDED(uwp_winrt_sys_get_activation_factory(
        L"Windows.Media.Devices.MediaDevice", &iid_md_stat, (void **)&md_stat)));
    HSTRING ms1 = NULL, ms2 = NULL, ms3 = NULL, md_ren = NULL, md_cap = NULL;
    assert(SUCCEEDED(md_stat->lpVtbl->GetAudioCaptureSelector(md_stat, &ms1)));
    assert(SUCCEEDED(md_stat->lpVtbl->GetAudioRenderSelector(md_stat, &ms2)));
    assert(SUCCEEDED(md_stat->lpVtbl->GetVideoCaptureSelector(md_stat, &ms3)));
    assert(wcsstr(WindowsGetStringRawBuffer(ms1, NULL), L"2EEF81BE") != NULL);
    assert(wcsstr(WindowsGetStringRawBuffer(ms2, NULL), L"E6327CAD") != NULL);
    assert(wcsstr(WindowsGetStringRawBuffer(ms3, NULL), L"E5323777") != NULL);
    assert(SUCCEEDED(md_stat->lpVtbl->GetDefaultAudioRenderId(md_stat, 0u, &md_ren)));
    assert(SUCCEEDED(md_stat->lpVtbl->GetDefaultAudioCaptureId(md_stat, 0u, &md_cap)));
    assert(wcslen(WindowsGetStringRawBuffer(md_ren, NULL)) > 0u);
    assert(wcslen(WindowsGetStringRawBuffer(md_cap, NULL)) > 0u);
    EventRegistrationToken md_tok = { 0 };
    assert(SUCCEEDED(md_stat->lpVtbl->add_Ren(md_stat, &g_test_gp_added, &md_tok)));
    assert(md_tok.value != 0);
    assert(SUCCEEDED(md_stat->lpVtbl->rem_Ren(md_stat, md_tok)));
    WindowsDeleteString(ms1);
    WindowsDeleteString(ms2);
    WindowsDeleteString(ms3);
    WindowsDeleteString(md_ren);
    WindowsDeleteString(md_cap);
    md_stat->lpVtbl->Release(md_stat);
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
    test_storage_file_streams_and_extended_apis();
    test_extended_settings_and_threadpool();
    test_devices_and_system_status();

    printf("ALL winrt_sys host unit tests PASSED (sandbox=%s)\n", sandbox);
    return 0;
}
