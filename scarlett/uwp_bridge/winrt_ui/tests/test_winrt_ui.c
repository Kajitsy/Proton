/*
 * Proton-Scarlett — WinRT UI Unit Tests
 *
 * Host-runnable (Linux C11) and PE64-compilable unit test suite verifying:
 *   - COM/WinRT vtable slot offsets matching SDL2.dll / UnityPlayer.dll
 *   - QueryInterface, AddRef/Release, GetIids, GetRuntimeClassName, GetTrustLevel
 *   - DisplayInformation (DPI, scale, raw pixels, orientation, events, E_NOTIMPL logging)
 *   - ApplicationView (Title, Fullscreen, PreferredLaunch*, VisibleBounds, TitleBar, etc.)
 *   - SystemNavigationManager (BackRequested listener list, trigger & Handled flag)
 *   - CoreCursor, MouseDevice, DisplayRequest, HdmiDisplayInformation
 *   - ApiInformation (honest TRUE for implemented APIs, FALSE for unimplemented)
 */

#include "../winrt_ui_internal.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ============================================================================
 * Mock uwp_core_* & uwp_bridge_log() State
 * ============================================================================ */

static uint32_t g_mock_width = 1920;
static uint32_t g_mock_height = 1080;
static int32_t  g_mock_fullscreen = 0;
static wchar_t  g_mock_title[256] = L"ScarlettTestApp";
static int32_t  g_log_count = 0;
static int32_t  g_unimpl_log_count = 0;
static char     g_last_log[512] = "";

static const uwp_package_identity_t g_mock_pkg = {
    .valid = 1,
    .ver_major = 1,
    .ver_minor = 0,
    .ver_build = 0,
    .ver_revision = 0,
    .architecture = 9u, /* ProcessorArchitecture x64 */
    .name = L"ScarlettTest",
    .publisher = L"ScarlettPublisher",
    .publisher_id = L"8wekyb3d8bbwe",
    .full_name = L"ScarlettTest_1.0.0.0_x64__8wekyb3d8bbwe",
    .family_name = L"ScarlettTest_8wekyb3d8bbwe"
};

HWND uwp_core_get_main_hwnd(void) {
    return NULL;
}

void uwp_core_get_client_size(uint32_t *out_width, uint32_t *out_height) {
    if (out_width) *out_width = g_mock_width;
    if (out_height) *out_height = g_mock_height;
}

int32_t uwp_core_is_fullscreen(void) {
    return g_mock_fullscreen;
}

int32_t uwp_core_set_fullscreen(int32_t fullscreen) {
    g_mock_fullscreen = fullscreen ? 1 : 0;
    return 1;
}

void uwp_core_set_window_title(const wchar_t *title) {
    if (!title) {
        g_mock_title[0] = L'\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < (sizeof(g_mock_title) / sizeof(g_mock_title[0])) && title[i] != L'\0'; ++i) {
        g_mock_title[i] = title[i];
    }
    g_mock_title[i] = L'\0';
}

void uwp_core_get_window_title(wchar_t *buf, uint32_t buf_chars) {
    if (!buf || buf_chars == 0) return;
    uint32_t i = 0;
    for (; i + 1 < buf_chars && g_mock_title[i] != L'\0'; ++i) {
        buf[i] = g_mock_title[i];
    }
    buf[i] = L'\0';
}

const uwp_package_identity_t *uwp_core_get_package_identity(void) {
    return &g_mock_pkg;
}

void uwp_bridge_log(const char *channel, const char *fmt, ...) {
    char msg[440];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);

    snprintf(g_last_log, sizeof(g_last_log), "[%s] %s", channel ? channel : "?", msg);
    g_log_count++;
    if (strstr(g_last_log, "E_NOTIMPL") != NULL || strstr(g_last_log, "unimplemented") != NULL) {
        g_unimpl_log_count++;
    }
}

/* ============================================================================
 * Test Helpers & Mock Event Delegate
 * ============================================================================ */

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define VTABLE_SLOT(vtbl_type, member) (offsetof(vtbl_type, member) / sizeof(void *))

static void free_iids(IID *iids) {
#ifdef _WIN32
    CoTaskMemFree(iids);
#else
    free(iids);
#endif
}

typedef struct TestEventDelegate {
    UwpEventHandler iface;
    volatile int32_t ref_count;
    int32_t call_count;
    void *last_sender;
    void *last_args;
    int32_t handle_back_request;
    UwpMouseDelta last_mouse_delta;
} TestEventDelegate;

static HRESULT STDMETHODCALLTYPE test_delegate_qi(UwpEventHandler *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) || IsEqualGUID(riid, &SCARLETT_IID_IAgileObject)) {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE test_delegate_addref(UwpEventHandler *This) {
    TestEventDelegate *self = CONTAINING_RECORD(This, TestEventDelegate, iface);
    return uwp_ui_atomic_inc(&self->ref_count);
}

static ULONG STDMETHODCALLTYPE test_delegate_release(UwpEventHandler *This) {
    TestEventDelegate *self = CONTAINING_RECORD(This, TestEventDelegate, iface);
    return uwp_ui_atomic_dec(&self->ref_count);
}

static HRESULT STDMETHODCALLTYPE test_delegate_invoke(UwpEventHandler *This, void *sender, void *args) {
    TestEventDelegate *self = CONTAINING_RECORD(This, TestEventDelegate, iface);
    self->call_count++;
    self->last_sender = sender;
    self->last_args = args;

    if (self->handle_back_request && args) {
        IUnknown *unk = (IUnknown *)args;
        UwpIBackRequestedEventArgs *back_args = NULL;
        if (SUCCEEDED(unk->lpVtbl->QueryInterface(unk, &SCARLETT_IID_IBackRequestedEventArgs, (void **)&back_args))) {
            back_args->lpVtbl->put_Handled(back_args, TRUE);
            back_args->lpVtbl->Release(back_args);
        }
    }
    if (args) {
        IUnknown *unk = (IUnknown *)args;
        UwpIMouseEventArgs *mouse_args = NULL;
        if (SUCCEEDED(unk->lpVtbl->QueryInterface(unk, &SCARLETT_IID_IMouseEventArgs, (void **)&mouse_args))) {
            mouse_args->lpVtbl->get_MouseDelta(mouse_args, &self->last_mouse_delta);
            mouse_args->lpVtbl->Release(mouse_args);
        }
    }
    return S_OK;
}

static const UwpEventHandlerVtbl g_test_delegate_vtbl = {
    test_delegate_qi,
    test_delegate_addref,
    test_delegate_release,
    test_delegate_invoke
};

static void test_delegate_init(TestEventDelegate *d, int32_t handle_back) {
    memset(d, 0, sizeof(*d));
    d->iface.lpVtbl = &g_test_delegate_vtbl;
    d->ref_count = 1;
    d->handle_back_request = handle_back;
}

/* ============================================================================
 * 1. Vtable Slot Offset Verification
 * ============================================================================ */

static int test_vtable_layouts(void) {
    /* IDisplayInformationStatics */
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, QueryInterface) == 0);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, GetTrustLevel) == 5);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, GetForCurrentView) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, get_AutoRotationPreferences) == 7);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, put_AutoRotationPreferences) == 8);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, add_DisplayContentsInvalidated) == 9);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationStaticsVtbl, remove_DisplayContentsInvalidated) == 10);

    /* IDisplayInformation .. IDisplayInformation5 */
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, get_CurrentOrientation) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, get_NativeOrientation) == 7);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, add_OrientationChanged) == 8);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, remove_OrientationChanged) == 9);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, get_ResolutionScale) == 10);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, get_LogicalDpi) == 11);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, get_RawDpiX) == 12);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, get_RawDpiY) == 13);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, add_DpiChanged) == 14);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, remove_DpiChanged) == 15);
    CHECK(VTABLE_SLOT(UwpIDisplayInformationVtbl, GetColorProfileAsync) == 19);
    CHECK(VTABLE_SLOT(UwpIDisplayInformation2Vtbl, get_RawPixelsPerViewPixel) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayInformation3Vtbl, get_DiagonalSizeInInches) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayInformation4Vtbl, get_ScreenWidthInRawPixels) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayInformation4Vtbl, get_ScreenHeightInRawPixels) == 7);
    CHECK(VTABLE_SLOT(UwpIDisplayInformation5Vtbl, GetAdvancedColorInfo) == 6);

    /* IApplicationView* */
    CHECK(VTABLE_SLOT(UwpIApplicationViewStatics2Vtbl, GetForCurrentView) == 6);
    CHECK(VTABLE_SLOT(UwpIApplicationViewStatics3Vtbl, get_PreferredLaunchWindowingMode) == 6);
    CHECK(VTABLE_SLOT(UwpIApplicationViewStatics3Vtbl, put_PreferredLaunchWindowingMode) == 7);
    CHECK(VTABLE_SLOT(UwpIApplicationViewStatics3Vtbl, get_PreferredLaunchViewSize) == 8);
    CHECK(VTABLE_SLOT(UwpIApplicationViewStatics3Vtbl, put_PreferredLaunchViewSize) == 9);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, get_Orientation) == 6);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, get_AdjacentToLeftDisplayEdge) == 7);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, get_AdjacentToRightDisplayEdge) == 8);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, get_IsFullScreen) == 9);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, put_Title) == 13);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, get_Title) == 14);
    CHECK(VTABLE_SLOT(UwpIApplicationViewVtbl, get_Id) == 15);
    CHECK(VTABLE_SLOT(UwpIApplicationView2Vtbl, get_VisibleBounds) == 8);
    CHECK(VTABLE_SLOT(UwpIApplicationView2Vtbl, add_VisibleBoundsChanged) == 9);
    CHECK(VTABLE_SLOT(UwpIApplicationView2Vtbl, SetDesiredBoundsMode) == 11);
    CHECK(VTABLE_SLOT(UwpIApplicationView2Vtbl, get_DesiredBoundsMode) == 12);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, get_TitleBar) == 6);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, get_FullScreenSystemOverlayMode) == 7);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, put_FullScreenSystemOverlayMode) == 8);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, get_IsFullScreenMode) == 9);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, TryEnterFullScreenMode) == 10);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, ExitFullScreenMode) == 11);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, ShowStandardSystemOverlays) == 12);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, TryResizeView) == 13);
    CHECK(VTABLE_SLOT(UwpIApplicationView3Vtbl, SetPreferredMinSize) == 14);
    CHECK(VTABLE_SLOT(UwpIApplicationView4Vtbl, get_ViewMode) == 6);

    /* SystemNavigationManager (verified vs UnityPlayer.dll: add_BackRequested +0x30, put_Handled +0x38) */
    CHECK(VTABLE_SLOT(UwpISystemNavigationManagerStaticsVtbl, GetForCurrentView) == 6);
    CHECK(VTABLE_SLOT(UwpISystemNavigationManagerVtbl, add_BackRequested) == 6);
    CHECK(VTABLE_SLOT(UwpISystemNavigationManagerVtbl, remove_BackRequested) == 7);
    CHECK(VTABLE_SLOT(UwpIBackRequestedEventArgsVtbl, get_Handled) == 6);
    CHECK(VTABLE_SLOT(UwpIBackRequestedEventArgsVtbl, put_Handled) == 7);
    CHECK(VTABLE_SLOT(UwpISystemNavigationManager2Vtbl, get_AppViewBackButtonVisibility) == 6);
    CHECK(VTABLE_SLOT(UwpISystemNavigationManager2Vtbl, put_AppViewBackButtonVisibility) == 7);

    /* CoreCursor & MouseDevice & DisplayRequest (verified vs SDL2.dll) */
    CHECK(VTABLE_SLOT(UwpICoreCursorFactoryVtbl, CreateCursor) == 6);
    CHECK(VTABLE_SLOT(UwpICoreCursorVtbl, get_Id) == 6);
    CHECK(VTABLE_SLOT(UwpICoreCursorVtbl, get_Type) == 7);
    CHECK(VTABLE_SLOT(UwpIMouseDeviceStaticsVtbl, GetForCurrentView) == 6);
    CHECK(VTABLE_SLOT(UwpIMouseDeviceVtbl, add_MouseMoved) == 6);
    CHECK(VTABLE_SLOT(UwpIMouseDeviceVtbl, remove_MouseMoved) == 7);
    CHECK(VTABLE_SLOT(UwpIMouseEventArgsVtbl, get_MouseDelta) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayRequestVtbl, RequestActive) == 6);
    CHECK(VTABLE_SLOT(UwpIDisplayRequestVtbl, RequestRelease) == 7);

    /* ApiInformation */
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsTypePresent) == 6);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsMethodPresent) == 7);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsMethodPresentWithArity) == 8);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsEventPresent) == 9);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsPropertyPresent) == 10);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsReadOnlyPropertyPresent) == 11);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsWriteablePropertyPresent) == 12);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsEnumNamedValuePresent) == 13);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsApiContractPresentByMajor) == 14);
    CHECK(VTABLE_SLOT(UwpIApiInformationStaticsVtbl, IsApiContractPresentByMajorAndMinor) == 15);

    /* HdmiDisplayInformation */
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayInformationStaticsVtbl, GetForCurrentView) == 6);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayInformationVtbl, GetSupportedDisplayModes) == 6);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayInformationVtbl, GetCurrentDisplayMode) == 7);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayInformationVtbl, SetDefaultDisplayModeAsync) == 8);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayModeVtbl, get_ResolutionWidthInRawPixels) == 6);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayModeVtbl, get_ResolutionHeightInRawPixels) == 7);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayModeVtbl, get_RefreshRate) == 8);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayModeVtbl, get_BitsPerPixel) == 10);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayModeVtbl, IsEqual) == 11);
    CHECK(VTABLE_SLOT(UwpIHdmiDisplayMode2Vtbl, get_IsDolbyVisionLowLatencySupported) == 6);

    return 0;
}

/* ============================================================================
 * 2. Factory Dispatch & IInspectable / QI Tests
 * ============================================================================ */

static int test_factory_dispatch_and_qi(void) {
    void *ptr = NULL;
    HRESULT hr;

    CHECK(uwp_winrt_ui_get_activation_factory(NULL, &SCARLETT_IID_IInspectable, &ptr) == E_INVALIDARG);
    CHECK(uwp_winrt_ui_get_activation_factory(L"Windows.Graphics.Display.DisplayInformation", NULL, &ptr) == E_INVALIDARG);
    CHECK(uwp_winrt_ui_get_activation_factory(L"Windows.Graphics.Display.DisplayInformation", &SCARLETT_IID_IInspectable, NULL) == E_POINTER);

    hr = uwp_winrt_ui_get_activation_factory(L"Windows.NonExistent.FakeClass", &SCARLETT_IID_IInspectable, &ptr);
    CHECK(hr == CLASS_E_CLASSNOTAVAILABLE);
    CHECK(ptr == NULL);

    /* Unsupported IID on valid class must return E_NOINTERFACE */
    static const GUID dummy_iid = { 0x11111111u, 0x2222u, 0x3333u, { 0x44u, 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0xAAu, 0xBBu } };
    hr = uwp_winrt_ui_get_activation_factory(L"Windows.Graphics.Display.DisplayInformation", &dummy_iid, &ptr);
    CHECK(hr == E_NOINTERFACE);
    CHECK(ptr == NULL);

    return 0;
}

/* ============================================================================
 * 3. DisplayInformation Tests
 * ============================================================================ */

static int test_display_information(void) {
    UwpIDisplayInformationStatics *statics = NULL;
    UwpIDisplayInformation *di = NULL;
    UwpIDisplayInformation2 *di2 = NULL;
    UwpIDisplayInformation3 *di3 = NULL;
    UwpIDisplayInformation4 *di4 = NULL;
    UwpIDisplayInformation5 *di5 = NULL;
    IUnknown *bad = NULL;

    g_mock_width = 2560;
    g_mock_height = 1440;

    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.Graphics.Display.DisplayInformation",
        &SCARLETT_IID_IDisplayInformationStatics, (void **)&statics)));
    CHECK(statics != NULL);

    /* GetIids / GetRuntimeClassName / GetTrustLevel */
    ULONG iid_count = 0;
    IID *iids = NULL;
    CHECK(SUCCEEDED(statics->lpVtbl->GetIids(statics, &iid_count, &iids)));
    CHECK(iid_count >= 1 && iids != NULL);
    free_iids(iids);

    HSTRING cls_name = NULL;
    CHECK(SUCCEEDED(statics->lpVtbl->GetRuntimeClassName(statics, &cls_name)));
    const wchar_t *raw_cls = uwp_ui_hstring_raw(cls_name, NULL);
    CHECK(raw_cls && wcscmp(raw_cls, L"Windows.Graphics.Display.DisplayInformation") == 0);
    uwp_ui_hstring_delete(cls_name);

    TrustLevel tl = FullTrust;
    CHECK(SUCCEEDED(statics->lpVtbl->GetTrustLevel(statics, &tl)));
    CHECK(tl == BaseTrust);

    CHECK(SUCCEEDED(statics->lpVtbl->GetForCurrentView(statics, &di)));
    CHECK(di != NULL);

    CHECK(SUCCEEDED(di->lpVtbl->QueryInterface(di, &SCARLETT_IID_IDisplayInformation2, (void **)&di2)));
    CHECK(SUCCEEDED(di->lpVtbl->QueryInterface(di, &SCARLETT_IID_IDisplayInformation3, (void **)&di3)));
    CHECK(SUCCEEDED(di->lpVtbl->QueryInterface(di, &SCARLETT_IID_IDisplayInformation4, (void **)&di4)));
    CHECK(SUCCEEDED(di->lpVtbl->QueryInterface(di, &SCARLETT_IID_IDisplayInformation5, (void **)&di5)));
    CHECK(di->lpVtbl->QueryInterface(di, &SCARLETT_IID_IApplicationView, (void **)&bad) == E_NOINTERFACE);

    FLOAT dpi = 0.0f, raw_x = 0.0f, raw_y = 0.0f;
    DOUBLE scale_factor = 0.0;
    UINT32 w = 0, h = 0;
    UwpDisplayOrientations cur_ori = UwpDisplayOrientations_None, nat_ori = UwpDisplayOrientations_None;
    UwpResolutionScale res_scale = 0;

    CHECK(SUCCEEDED(di->lpVtbl->get_LogicalDpi(di, &dpi)));
    CHECK(dpi >= 96.0f);
    CHECK(SUCCEEDED(di->lpVtbl->get_RawDpiX(di, &raw_x)));
    CHECK(SUCCEEDED(di->lpVtbl->get_RawDpiY(di, &raw_y)));
    CHECK(raw_x >= 96.0f && raw_y >= 96.0f);
    CHECK(SUCCEEDED(di2->lpVtbl->get_RawPixelsPerViewPixel(di2, &scale_factor)));
    CHECK(scale_factor >= 1.0);
    CHECK(SUCCEEDED(di->lpVtbl->get_ResolutionScale(di, &res_scale)));
    CHECK(res_scale >= UwpResolutionScale_Scale100Percent);
    CHECK(SUCCEEDED(di->lpVtbl->get_CurrentOrientation(di, &cur_ori)));
    CHECK(SUCCEEDED(di->lpVtbl->get_NativeOrientation(di, &nat_ori)));
    CHECK(cur_ori == UwpDisplayOrientations_Landscape);
    CHECK(nat_ori == UwpDisplayOrientations_Landscape);
    CHECK(SUCCEEDED(di4->lpVtbl->get_ScreenWidthInRawPixels(di4, &w)));
    CHECK(SUCCEEDED(di4->lpVtbl->get_ScreenHeightInRawPixels(di4, &h)));
    CHECK(w == 2560 && h == 1440);

    /* AutoRotationPreferences */
    CHECK(SUCCEEDED(statics->lpVtbl->put_AutoRotationPreferences(statics, UwpDisplayOrientations_Landscape | UwpDisplayOrientations_LandscapeFlipped)));
    UwpDisplayOrientations pref = UwpDisplayOrientations_None;
    CHECK(SUCCEEDED(statics->lpVtbl->get_AutoRotationPreferences(statics, &pref)));
    CHECK(pref == (UwpDisplayOrientations_Landscape | UwpDisplayOrientations_LandscapeFlipped));

    /* Event registration + notification */
    TestEventDelegate dpi_del, ori_del;
    test_delegate_init(&dpi_del, 0);
    test_delegate_init(&ori_del, 0);
    EventRegistrationToken tok_dpi = { 0 }, tok_ori = { 0 };

    CHECK(SUCCEEDED(di->lpVtbl->add_DpiChanged(di, &dpi_del.iface, &tok_dpi)));
    CHECK(tok_dpi.value != 0);
    CHECK(SUCCEEDED(di->lpVtbl->add_OrientationChanged(di, &ori_del.iface, &tok_ori)));
    CHECK(tok_ori.value != 0);

    CHECK(SUCCEEDED(uwp_winrt_ui_notify_dpi_changed()));
    CHECK(dpi_del.call_count == 1);
    CHECK(ori_del.call_count == 0);

    CHECK(SUCCEEDED(uwp_winrt_ui_notify_orientation_changed()));
    CHECK(ori_del.call_count == 1);

    CHECK(SUCCEEDED(di->lpVtbl->remove_DpiChanged(di, tok_dpi)));
    CHECK(SUCCEEDED(di->lpVtbl->remove_OrientationChanged(di, tok_ori)));
    CHECK(SUCCEEDED(uwp_winrt_ui_notify_dpi_changed()));
    CHECK(dpi_del.call_count == 1);

    /* Unimplemented methods MUST log via uwp_bridge_log() and return E_NOTIMPL */
    int32_t prev_unimpl = g_unimpl_log_count;
    void *dummy_out = (void *)0x1234;
    CHECK(di->lpVtbl->GetColorProfileAsync(di, &dummy_out) == E_NOTIMPL);
    CHECK(dummy_out == NULL);
    CHECK(g_unimpl_log_count > prev_unimpl);

    prev_unimpl = g_unimpl_log_count;
    dummy_out = (void *)0x1234;
    CHECK(di5->lpVtbl->GetAdvancedColorInfo(di5, &dummy_out) == E_NOTIMPL);
    CHECK(dummy_out == NULL);
    CHECK(g_unimpl_log_count > prev_unimpl);

    di5->lpVtbl->Release(di5);
    di4->lpVtbl->Release(di4);
    di3->lpVtbl->Release(di3);
    di2->lpVtbl->Release(di2);
    di->lpVtbl->Release(di);
    statics->lpVtbl->Release(statics);
    return 0;
}

/* ============================================================================
 * 4. ApplicationView Tests
 * ============================================================================ */

static int test_application_view(void) {
    UwpIApplicationViewStatics *s1 = NULL;
    UwpIApplicationViewStatics2 *s2 = NULL;
    UwpIApplicationViewStatics3 *s3 = NULL;
    UwpIApplicationView *av = NULL;
    UwpIApplicationView2 *av2 = NULL;
    UwpIApplicationView3 *av3 = NULL;
    UwpIApplicationView4 *av4 = NULL;

    g_mock_width = 1280;
    g_mock_height = 720;
    g_mock_fullscreen = 0;

    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.UI.ViewManagement.ApplicationView",
        &SCARLETT_IID_IApplicationViewStatics2, (void **)&s2)));
    CHECK(SUCCEEDED(s2->lpVtbl->QueryInterface(s2, &SCARLETT_IID_IApplicationViewStatics, (void **)&s1)));
    CHECK(SUCCEEDED(s2->lpVtbl->QueryInterface(s2, &SCARLETT_IID_IApplicationViewStatics3, (void **)&s3)));

    CHECK(SUCCEEDED(s2->lpVtbl->GetForCurrentView(s2, &av)));
    CHECK(av != NULL);
    CHECK(SUCCEEDED(av->lpVtbl->QueryInterface(av, &SCARLETT_IID_IApplicationView2, (void **)&av2)));
    CHECK(SUCCEEDED(av->lpVtbl->QueryInterface(av, &SCARLETT_IID_IApplicationView3, (void **)&av3)));
    CHECK(SUCCEEDED(av->lpVtbl->QueryInterface(av, &SCARLETT_IID_IApplicationView4, (void **)&av4)));

    /* Statics3: PreferredLaunchViewSize & PreferredLaunchWindowingMode */
    UwpSize launch_size = { 1600.0f, 900.0f };
    CHECK(SUCCEEDED(s3->lpVtbl->put_PreferredLaunchViewSize(s3, launch_size)));
    UwpSize got_size = { 0.0f, 0.0f };
    CHECK(SUCCEEDED(s3->lpVtbl->get_PreferredLaunchViewSize(s3, &got_size)));
    CHECK(fabsf(got_size.Width - 1600.0f) < 0.01f && fabsf(got_size.Height - 900.0f) < 0.01f);

    CHECK(SUCCEEDED(s3->lpVtbl->put_PreferredLaunchWindowingMode(s3, UwpApplicationViewWindowingMode_FullScreen)));
    UwpApplicationViewWindowingMode wmode = UwpApplicationViewWindowingMode_Auto;
    CHECK(SUCCEEDED(s3->lpVtbl->get_PreferredLaunchWindowingMode(s3, &wmode)));
    CHECK(wmode == UwpApplicationViewWindowingMode_FullScreen);

    /* Title get/put via uwp_core_* */
    HSTRING new_title = NULL;
    CHECK(SUCCEEDED(uwp_ui_hstring_create(L"GZDoom-Scarlett", 15, &new_title)));
    CHECK(SUCCEEDED(av->lpVtbl->put_Title(av, new_title)));
    uwp_ui_hstring_delete(new_title);
    CHECK(wcscmp(g_mock_title, L"GZDoom-Scarlett") == 0);

    HSTRING read_title = NULL;
    CHECK(SUCCEEDED(av->lpVtbl->get_Title(av, &read_title)));
    const wchar_t *raw_title = uwp_ui_hstring_raw(read_title, NULL);
    CHECK(raw_title && wcscmp(raw_title, L"GZDoom-Scarlett") == 0);
    uwp_ui_hstring_delete(read_title);

    /* FullScreen mode */
    boolean is_fs = TRUE;
    CHECK(SUCCEEDED(av3->lpVtbl->get_IsFullScreenMode(av3, &is_fs)));
    CHECK(is_fs == FALSE);

    boolean entered = FALSE;
    CHECK(SUCCEEDED(av3->lpVtbl->TryEnterFullScreenMode(av3, &entered)));
    CHECK(entered == TRUE && g_mock_fullscreen == 1);
    CHECK(SUCCEEDED(av3->lpVtbl->get_IsFullScreenMode(av3, &is_fs)));
    CHECK(is_fs == TRUE);

    CHECK(SUCCEEDED(av3->lpVtbl->ExitFullScreenMode(av3)));
    CHECK(g_mock_fullscreen == 0);
    CHECK(SUCCEEDED(av3->lpVtbl->get_IsFullScreenMode(av3, &is_fs)));
    CHECK(is_fs == FALSE);

    /* VisibleBounds & DesiredBoundsMode & VisibleBoundsChanged */
    UwpRect bounds = { 0 };
    CHECK(SUCCEEDED(av2->lpVtbl->get_VisibleBounds(av2, &bounds)));
    CHECK(fabsf(bounds.Width - 1280.0f) < 0.01f && fabsf(bounds.Height - 720.0f) < 0.01f);

    boolean bounds_ok = FALSE;
    CHECK(SUCCEEDED(av2->lpVtbl->SetDesiredBoundsMode(av2, UwpApplicationViewBoundsMode_UseCoreWindow, &bounds_ok)));
    CHECK(bounds_ok == TRUE);
    UwpApplicationViewBoundsMode bmode = UwpApplicationViewBoundsMode_UseVisible;
    CHECK(SUCCEEDED(av2->lpVtbl->get_DesiredBoundsMode(av2, &bmode)));
    CHECK(bmode == UwpApplicationViewBoundsMode_UseCoreWindow);

    TestEventDelegate vb_del;
    test_delegate_init(&vb_del, 0);
    EventRegistrationToken vb_tok = { 0 };
    CHECK(SUCCEEDED(av2->lpVtbl->add_VisibleBoundsChanged(av2, &vb_del.iface, &vb_tok)));
    CHECK(SUCCEEDED(uwp_winrt_ui_notify_visible_bounds_changed()));
    CHECK(vb_del.call_count == 1);
    CHECK(SUCCEEDED(av2->lpVtbl->remove_VisibleBoundsChanged(av2, vb_tok)));

    /* Adjacent edges, Orientation, Id, TryResizeView */
    boolean left_adj = FALSE, right_adj = FALSE;
    CHECK(SUCCEEDED(av->lpVtbl->get_AdjacentToLeftDisplayEdge(av, &left_adj)));
    CHECK(SUCCEEDED(av->lpVtbl->get_AdjacentToRightDisplayEdge(av, &right_adj)));
    CHECK(left_adj == TRUE && right_adj == TRUE);

    UwpApplicationViewOrientation av_ori = UwpApplicationViewOrientation_Portrait;
    CHECK(SUCCEEDED(av->lpVtbl->get_Orientation(av, &av_ori)));
    CHECK(av_ori == UwpApplicationViewOrientation_Landscape);

    INT32 view_id = 0;
    CHECK(SUCCEEDED(av->lpVtbl->get_Id(av, &view_id)));
    CHECK(view_id == 1);

    boolean resized = FALSE;
    UwpSize req_size = { 1024.0f, 768.0f };
    CHECK(SUCCEEDED(av3->lpVtbl->TryResizeView(av3, req_size, &resized)));
    CHECK(resized == TRUE);

    /* TitleBar */
    UwpIApplicationViewTitleBar *tb = NULL;
    CHECK(SUCCEEDED(av3->lpVtbl->get_TitleBar(av3, &tb)));
    CHECK(tb != NULL);
    void *bg_color = (void *)0x1234;
    CHECK(SUCCEEDED(tb->lpVtbl->get_BackgroundColor(tb, &bg_color)));
    CHECK(bg_color == NULL);
    CHECK(SUCCEEDED(tb->lpVtbl->put_BackgroundColor(tb, NULL)));
    tb->lpVtbl->Release(tb);

    /* ApplicationView4 view mode + E_NOTIMPL on ShowStandardSystemOverlays */
    UwpApplicationViewMode vm = UwpApplicationViewMode_CompactOverlay;
    CHECK(SUCCEEDED(av4->lpVtbl->get_ViewMode(av4, &vm)));
    CHECK(vm == UwpApplicationViewMode_Default);

    int32_t prev_unimpl = g_unimpl_log_count;
    CHECK(av3->lpVtbl->ShowStandardSystemOverlays(av3) == E_NOTIMPL);
    CHECK(g_unimpl_log_count > prev_unimpl);

    av4->lpVtbl->Release(av4);
    av3->lpVtbl->Release(av3);
    av2->lpVtbl->Release(av2);
    av->lpVtbl->Release(av);
    s3->lpVtbl->Release(s3);
    s2->lpVtbl->Release(s2);
    s1->lpVtbl->Release(s1);
    return 0;
}

/* ============================================================================
 * 5. SystemNavigationManager Tests
 * ============================================================================ */

static int test_system_navigation_manager(void) {
    UwpISystemNavigationManagerStatics *statics = NULL;
    UwpISystemNavigationManager *snm = NULL;
    UwpISystemNavigationManager2 *snm2 = NULL;

    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.UI.Core.SystemNavigationManager",
        &SCARLETT_IID_ISystemNavigationManagerStatics, (void **)&statics)));
    CHECK(SUCCEEDED(statics->lpVtbl->GetForCurrentView(statics, &snm)));
    CHECK(snm != NULL);
    CHECK(SUCCEEDED(snm->lpVtbl->QueryInterface(snm, &SCARLETT_IID_ISystemNavigationManager2, (void **)&snm2)));

    CHECK(SUCCEEDED(snm2->lpVtbl->put_AppViewBackButtonVisibility(snm2, UwpAppViewBackButtonVisibility_Visible)));
    UwpAppViewBackButtonVisibility vis = UwpAppViewBackButtonVisibility_Collapsed;
    CHECK(SUCCEEDED(snm2->lpVtbl->get_AppViewBackButtonVisibility(snm2, &vis)));
    CHECK(vis == UwpAppViewBackButtonVisibility_Visible);

    TestEventDelegate back_del;
    test_delegate_init(&back_del, 1); /* handler sets Handled = TRUE */
    EventRegistrationToken tok = { 0 };
    CHECK(SUCCEEDED(snm->lpVtbl->add_BackRequested(snm, &back_del.iface, &tok)));
    CHECK(tok.value != 0);

    int32_t handled = 0;
    CHECK(SUCCEEDED(uwp_winrt_ui_trigger_back_requested(&handled)));
    CHECK(back_del.call_count == 1);
    CHECK(handled == 1);

    CHECK(SUCCEEDED(snm->lpVtbl->remove_BackRequested(snm, tok)));
    handled = 1;
    CHECK(SUCCEEDED(uwp_winrt_ui_trigger_back_requested(&handled)));
    CHECK(back_del.call_count == 1);
    CHECK(handled == 0);

    snm2->lpVtbl->Release(snm2);
    snm->lpVtbl->Release(snm);
    statics->lpVtbl->Release(statics);
    return 0;
}

/* ============================================================================
 * 6. CoreCursor, MouseDevice, DisplayRequest, HdmiDisplayInformation Tests
 * ============================================================================ */

static int test_cursor_mouse_request_hdmi(void) {
    /* CoreCursor */
    UwpICoreCursorFactory *ccf = NULL;
    UwpICoreCursor *cursor = NULL;
    IActivationFactory *af = NULL;
    IInspectable *dummy_inst = NULL;

    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.UI.Core.CoreCursor",
        &SCARLETT_IID_ICoreCursorFactory, (void **)&ccf)));
    CHECK(SUCCEEDED(ccf->lpVtbl->QueryInterface(ccf, &SCARLETT_IID_IActivationFactory, (void **)&af)));
    int32_t prev_unimpl = g_unimpl_log_count;
    CHECK(af->lpVtbl->ActivateInstance(af, &dummy_inst) == E_NOTIMPL);
    CHECK(g_unimpl_log_count > prev_unimpl);

    CHECK(SUCCEEDED(ccf->lpVtbl->CreateCursor(ccf, UwpCoreCursorType_Hand, 42u, &cursor)));
    CHECK(cursor != NULL);
    UINT32 cid = 0;
    UwpCoreCursorType ctype = UwpCoreCursorType_Arrow;
    CHECK(SUCCEEDED(cursor->lpVtbl->get_Id(cursor, &cid)));
    CHECK(SUCCEEDED(cursor->lpVtbl->get_Type(cursor, &ctype)));
    CHECK(cid == 42u && ctype == UwpCoreCursorType_Hand);
    CHECK(uwp_winrt_ui_cursor_type_to_win32_resource(UwpCoreCursorType_Hand) != NULL);
    CHECK(uwp_winrt_ui_cursor_type_to_win32_resource(UwpCoreCursorType_Custom) == NULL);

    cursor->lpVtbl->Release(cursor);
    af->lpVtbl->Release(af);
    ccf->lpVtbl->Release(ccf);

    /* MouseDevice */
    UwpIMouseDeviceStatics *mds = NULL;
    UwpIMouseDevice *md = NULL;
    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.Devices.Input.MouseDevice",
        &SCARLETT_IID_IMouseDeviceStatics, (void **)&mds)));
    CHECK(SUCCEEDED(mds->lpVtbl->GetForCurrentView(mds, &md)));
    CHECK(md != NULL);

    TestEventDelegate mouse_del;
    test_delegate_init(&mouse_del, 0);
    EventRegistrationToken mtok = { 0 };
    CHECK(SUCCEEDED(md->lpVtbl->add_MouseMoved(md, &mouse_del.iface, &mtok)));
    CHECK(SUCCEEDED(uwp_winrt_ui_notify_mouse_moved(17, -23)));
    CHECK(mouse_del.call_count == 1);
    CHECK(mouse_del.last_mouse_delta.X == 17 && mouse_del.last_mouse_delta.Y == -23);
    CHECK(SUCCEEDED(md->lpVtbl->remove_MouseMoved(md, mtok)));

    md->lpVtbl->Release(md);
    mds->lpVtbl->Release(mds);

    /* DisplayRequest */
    IActivationFactory *draf = NULL;
    IInspectable *dr_insp = NULL;
    UwpIDisplayRequest *dr = NULL;
    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.System.Display.DisplayRequest",
        &SCARLETT_IID_IActivationFactory, (void **)&draf)));
    CHECK(SUCCEEDED(draf->lpVtbl->ActivateInstance(draf, &dr_insp)));
    CHECK(SUCCEEDED(dr_insp->lpVtbl->QueryInterface(dr_insp, &SCARLETT_IID_IDisplayRequest, (void **)&dr)));
    dr_insp->lpVtbl->Release(dr_insp);

    CHECK(SUCCEEDED(dr->lpVtbl->RequestActive(dr)));
    CHECK(SUCCEEDED(dr->lpVtbl->RequestActive(dr)));
    CHECK(SUCCEEDED(dr->lpVtbl->RequestRelease(dr)));
    CHECK(SUCCEEDED(dr->lpVtbl->RequestRelease(dr)));
    CHECK(dr->lpVtbl->RequestRelease(dr) == (HRESULT)0x8000000EL); /* E_ILLEGAL_METHOD_CALL */

    dr->lpVtbl->Release(dr);
    draf->lpVtbl->Release(draf);

    /* HdmiDisplayInformation */
    g_mock_width = 3840;
    g_mock_height = 2160;
    UwpIHdmiDisplayInformationStatics *hstatics = NULL;
    UwpIHdmiDisplayInformation *hdi = NULL;
    UwpIHdmiDisplayMode *mode = NULL;
    UwpIHdmiDisplayMode2 *mode2 = NULL;
    UwpIVectorViewHdmiDisplayMode *modes = NULL;

    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.Graphics.Display.Core.HdmiDisplayInformation",
        &SCARLETT_IID_IHdmiDisplayInformationStatics, (void **)&hstatics)));
    CHECK(SUCCEEDED(hstatics->lpVtbl->GetForCurrentView(hstatics, &hdi)));
    CHECK(SUCCEEDED(hdi->lpVtbl->GetCurrentDisplayMode(hdi, &mode)));
    CHECK(mode != NULL);

    UINT32 mw = 0, mh = 0;
    DOUBLE hz = 0.0;
    UINT16 bpp = 0;
    boolean eq = FALSE, dv = TRUE;
    CHECK(SUCCEEDED(mode->lpVtbl->get_ResolutionWidthInRawPixels(mode, &mw)));
    CHECK(SUCCEEDED(mode->lpVtbl->get_ResolutionHeightInRawPixels(mode, &mh)));
    CHECK(SUCCEEDED(mode->lpVtbl->get_RefreshRate(mode, &hz)));
    CHECK(SUCCEEDED(mode->lpVtbl->get_BitsPerPixel(mode, &bpp)));
    CHECK(SUCCEEDED(mode->lpVtbl->IsEqual(mode, mode, &eq)));
    CHECK(mw == 3840 && mh == 2160 && hz >= 59.0 && bpp >= 24 && eq == TRUE);

    CHECK(SUCCEEDED(mode->lpVtbl->QueryInterface(mode, &SCARLETT_IID_IHdmiDisplayMode2, (void **)&mode2)));
    CHECK(SUCCEEDED(mode2->lpVtbl->get_IsDolbyVisionLowLatencySupported(mode2, &dv)));
    CHECK(dv == FALSE);

    CHECK(SUCCEEDED(hdi->lpVtbl->GetSupportedDisplayModes(hdi, &modes)));
    UINT32 mcount = 0;
    CHECK(SUCCEEDED(modes->lpVtbl->get_Size(modes, &mcount)));
    CHECK(mcount >= 1);
    UwpIHdmiDisplayMode *mode_at0 = NULL, *mode_oob = (UwpIHdmiDisplayMode *)0x1234;
    CHECK(SUCCEEDED(modes->lpVtbl->GetAt(modes, 0, &mode_at0)));
    CHECK(mode_at0 != NULL);
    CHECK(modes->lpVtbl->GetAt(modes, mcount, &mode_oob) == (HRESULT)0x8000000BL); /* E_BOUNDS */
    CHECK(mode_oob == NULL);

    prev_unimpl = g_unimpl_log_count;
    void *async_op = (void *)0x1234;
    CHECK(hdi->lpVtbl->SetDefaultDisplayModeAsync(hdi, &async_op) == E_NOTIMPL);
    CHECK(async_op == NULL && g_unimpl_log_count > prev_unimpl);

    mode_at0->lpVtbl->Release(mode_at0);
    modes->lpVtbl->Release(modes);
    mode2->lpVtbl->Release(mode2);
    mode->lpVtbl->Release(mode);
    hdi->lpVtbl->Release(hdi);
    hstatics->lpVtbl->Release(hstatics);

    return 0;
}

/* ============================================================================
 * 7. ApiInformation Tests
 * ============================================================================ */

static int check_api_type(UwpIApiInformationStatics *ai, const wchar_t *name, boolean expected) {
    HSTRING hs = NULL;
    boolean actual = (boolean)!expected;
    if (FAILED(uwp_ui_hstring_create(name, (uint32_t)wcslen(name), &hs))) return 1;
    HRESULT hr = ai->lpVtbl->IsTypePresent(ai, hs, &actual);
    uwp_ui_hstring_delete(hs);
    return (SUCCEEDED(hr) && actual == expected) ? 0 : 1;
}

static int check_api_member(UwpIApiInformationStatics *ai,
                            int kind,
                            const wchar_t *type_name,
                            const wchar_t *member_name,
                            UINT32 arity,
                            boolean expected) {
    HSTRING ht = NULL, hm = NULL;
    boolean actual = (boolean)!expected;
    if (FAILED(uwp_ui_hstring_create(type_name, (uint32_t)wcslen(type_name), &ht))) return 1;
    if (FAILED(uwp_ui_hstring_create(member_name, (uint32_t)wcslen(member_name), &hm))) {
        uwp_ui_hstring_delete(ht);
        return 1;
    }

    HRESULT hr = E_FAIL;
    switch (kind) {
    case 0: hr = ai->lpVtbl->IsMethodPresent(ai, ht, hm, &actual); break;
    case 1: hr = ai->lpVtbl->IsMethodPresentWithArity(ai, ht, hm, arity, &actual); break;
    case 2: hr = ai->lpVtbl->IsPropertyPresent(ai, ht, hm, &actual); break;
    case 3: hr = ai->lpVtbl->IsReadOnlyPropertyPresent(ai, ht, hm, &actual); break;
    case 4: hr = ai->lpVtbl->IsWriteablePropertyPresent(ai, ht, hm, &actual); break;
    case 5: hr = ai->lpVtbl->IsEventPresent(ai, ht, hm, &actual); break;
    case 6: hr = ai->lpVtbl->IsEnumNamedValuePresent(ai, ht, hm, &actual); break;
    default: break;
    }

    uwp_ui_hstring_delete(hm);
    uwp_ui_hstring_delete(ht);
    return (SUCCEEDED(hr) && actual == expected) ? 0 : 1;
}

static int test_api_information(void) {
    UwpIApiInformationStatics *ai = NULL;
    CHECK(SUCCEEDED(uwp_winrt_ui_get_activation_factory(
        L"Windows.Foundation.Metadata.ApiInformation",
        &SCARLETT_IID_IApiInformationStatics, (void **)&ai)));

    /* Types: honest TRUE for implemented, FALSE for unimplemented */
    CHECK(check_api_type(ai, L"Windows.Graphics.Display.DisplayInformation", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.UI.ViewManagement.ApplicationView", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.UI.Core.SystemNavigationManager", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.UI.Core.CoreCursor", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.Devices.Input.MouseDevice", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.System.Display.DisplayRequest", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.Foundation.Metadata.ApiInformation", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.Graphics.Display.Core.HdmiDisplayInformation", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.ApplicationModel.Core.CoreApplication", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.UI.Core.CoreWindow", TRUE) == 0);
    CHECK(check_api_type(ai, L"Windows.Devices.Bluetooth.BluetoothAdapter", FALSE) == 0);
    CHECK(check_api_type(ai, L"Windows.Media.Capture.MediaCapture", FALSE) == 0);

    /* Methods & Arity */
    CHECK(check_api_member(ai, 0, L"Windows.UI.ViewManagement.ApplicationView", L"TryEnterFullScreenMode", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 1, L"Windows.UI.ViewManagement.ApplicationView", L"TryResizeView", 1, TRUE) == 0);
    CHECK(check_api_member(ai, 1, L"Windows.UI.ViewManagement.ApplicationView", L"TryResizeView", 3, FALSE) == 0);
    CHECK(check_api_member(ai, 0, L"Windows.Graphics.Display.DisplayInformation", L"GetColorProfileAsync", 0, FALSE) == 0);

    /* Properties (readonly vs writeable) */
    CHECK(check_api_member(ai, 2, L"Windows.Graphics.Display.DisplayInformation", L"LogicalDpi", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 3, L"Windows.Graphics.Display.DisplayInformation", L"LogicalDpi", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 4, L"Windows.Graphics.Display.DisplayInformation", L"LogicalDpi", 0, FALSE) == 0);
    CHECK(check_api_member(ai, 2, L"Windows.UI.ViewManagement.ApplicationView", L"Title", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 4, L"Windows.UI.ViewManagement.ApplicationView", L"Title", 0, TRUE) == 0);

    /* Events & Enums */
    CHECK(check_api_member(ai, 5, L"Windows.UI.Core.SystemNavigationManager", L"BackRequested", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 5, L"Windows.Graphics.Display.DisplayInformation", L"ColorProfileChanged", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 5, L"Windows.Graphics.Display.DisplayInformation", L"BogusEvent", 0, FALSE) == 0);
    CHECK(check_api_member(ai, 6, L"Windows.Graphics.Display.DisplayOrientations", L"Landscape", 0, TRUE) == 0);
    CHECK(check_api_member(ai, 6, L"Windows.Graphics.Display.DisplayOrientations", L"BogusOrientation", 0, FALSE) == 0);

    /* Contracts */
    HSTRING hc = NULL;
    boolean present = FALSE;
    CHECK(SUCCEEDED(uwp_ui_hstring_create(L"Windows.Foundation.UniversalApiContract", 39, &hc)));
    CHECK(SUCCEEDED(ai->lpVtbl->IsApiContractPresentByMajor(ai, hc, 5, &present)) && present == TRUE);
    CHECK(SUCCEEDED(ai->lpVtbl->IsApiContractPresentByMajor(ai, hc, 99, &present)) && present == FALSE);
    CHECK(SUCCEEDED(ai->lpVtbl->IsApiContractPresentByMajorAndMinor(ai, hc, 10, 0, &present)) && present == TRUE);
    uwp_ui_hstring_delete(hc);

    CHECK(SUCCEEDED(uwp_ui_hstring_create(L"Windows.Phone.PhoneContract", 27, &hc)));
    CHECK(SUCCEEDED(ai->lpVtbl->IsApiContractPresentByMajor(ai, hc, 1, &present)) && present == FALSE);
    uwp_ui_hstring_delete(hc);

    ai->lpVtbl->Release(ai);
    return 0;
}

/* ============================================================================
 * Entry Point
 * ============================================================================ */

int main(void) {
    printf("[winrt_ui] running unit tests...\n");

    CHECK(test_vtable_layouts() == 0);
    printf("  PASS: vtable layouts\n");

    CHECK(test_factory_dispatch_and_qi() == 0);
    printf("  PASS: factory dispatch & QI\n");

    CHECK(test_display_information() == 0);
    printf("  PASS: DisplayInformation\n");

    CHECK(test_application_view() == 0);
    printf("  PASS: ApplicationView\n");

    CHECK(test_system_navigation_manager() == 0);
    printf("  PASS: SystemNavigationManager\n");

    CHECK(test_cursor_mouse_request_hdmi() == 0);
    printf("  PASS: CoreCursor, MouseDevice, DisplayRequest, HdmiDisplayInformation\n");

    CHECK(test_api_information() == 0);
    printf("  PASS: ApiInformation\n");

    printf("[winrt_ui] ALL TESTS PASSED (%d log entries observed)\n", g_log_count);
    return 0;
}
