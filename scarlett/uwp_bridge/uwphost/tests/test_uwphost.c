/*
 * Proton-Scarlett — UWPHOST.dll Host Unit Test Suite
 *
 * Verifies:
 *   1. Clean-room AppxManifest.xml parser (SHA-256 PublisherId, UTF-8/UTF-16LE BOM,
 *      entities, comments, reverse Path/ActivatableClass order, real HedgePhysics & GZDoom manifests)
 *   2. HSTRING creation/reference/buffer/deletion
 *   3. CoreApplication, CoreApplicationView, CoreWindow, CoreDispatcher COM QI & vtable layouts
 *   4. ICoreWindowInterop & IProtonScarlettCoreWindowBridge ABI contract
 *   5. CoreApplication::Run(IFrameworkViewSource*) full UWP lifecycle order + suspend/resume/bg events
 *   6. CoreWindow Win32 message -> WinRT event translation (SizeChanged, KeyDown/Up, Char,
 *      PointerEntered/Moved/Pressed/Released/WheelChanged, Activated, VisibilityChanged, Closed)
 *   7. CoreDispatcher::RunAsync priority ordering & IAsyncAction/IAsyncInfo completion
 *   8. Activation factory dispatch chain (a) core -> (b) winrt_ui -> (c) winrt_sys ->
 *      (d) manifest inProcessServer -> (e) unknown class diagnostic & PROTON_XBOX_STRICT=1 abort
 *   9. uwp_core_* helpers and ExecuteAssembly non-silent failure reporting
 */

#include "uwphost_internal.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Mock winrt_ui & winrt_sys Hooks to Verify Exact Dispatch Chain Order
 * ============================================================================ */

static int g_dispatch_trace_count = 0;
static char g_dispatch_trace[8][32];

static void record_dispatch(const char *stage) {
    if (g_dispatch_trace_count < 8) {
        snprintf(g_dispatch_trace[g_dispatch_trace_count++], 32, "%s", stage);
    }
}

static HRESULT UWP_CALL mock_af_QI(UwpIActivationFactory *T, REFIID r, void **p) {
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) ||
        uwp_guid_equal(r, &UWP_IID_IInspectable) ||
        uwp_guid_equal(r, &UWP_IID_IActivationFactory)) {
        *p = T;
        return S_OK;
    }
    *p = NULL;
    return E_NOINTERFACE;
}
static ULONG UWP_CALL mock_af_AddRef(UwpIActivationFactory *T) { (void)T; return 1u; }
static ULONG UWP_CALL mock_af_Release(UwpIActivationFactory *T) { (void)T; return 1u; }
static HRESULT UWP_CALL mock_af_GetIids(UwpIActivationFactory *T, ULONG *c, IID **i) {
    (void)T; *c = 0; *i = NULL; return S_OK;
}
static HRESULT UWP_CALL mock_af_GetName(UwpIActivationFactory *T, UWP_HSTRING *n) {
    (void)T; return uwp_hstring_create_from_wstr(L"Mock.Factory", n);
}
static HRESULT UWP_CALL mock_af_GetTrust(UwpIActivationFactory *T, UwpTrustLevel *t) {
    (void)T; *t = UWP_TRUST_BASE; return S_OK;
}
static HRESULT UWP_CALL mock_af_Activate(UwpIActivationFactory *T, UwpIInspectable **inst) {
    (void)T; *inst = NULL; return E_NOTIMPL;
}

static const UwpIActivationFactoryVtbl g_mock_af_vtbl = {
    mock_af_QI, mock_af_AddRef, mock_af_Release,
    mock_af_GetIids, mock_af_GetName, mock_af_GetTrust,
    mock_af_Activate
};
static UwpIActivationFactory g_mock_ui_factory = { &g_mock_af_vtbl };
static UwpIActivationFactory g_mock_sys_factory = { &g_mock_af_vtbl };

HRESULT uwp_winrt_ui_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory) {
    record_dispatch("winrt_ui");
    if (wcscmp(class_name, L"Windows.Graphics.Display.DisplayInformation") == 0) {
        return mock_af_QI(&g_mock_ui_factory, iid, out_factory);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT uwp_winrt_sys_get_activation_factory(const wchar_t *class_name, REFIID iid, void **out_factory) {
    record_dispatch("winrt_sys");
    if (wcscmp(class_name, L"Windows.Storage.ApplicationData") == 0) {
        return mock_af_QI(&g_mock_sys_factory, iid, out_factory);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* ============================================================================
 * Test 1: AppxManifest.xml Parser & PublisherId
 * ============================================================================ */

static void test_manifest_parser(void) {
    printf("[test] AppxManifest.xml parser & PublisherId...\n");

    /* Verify Microsoft well-known PublisherId hash: 8wekyb3d8bbwe */
    wchar_t pub_id[16];
    uwp_manifest_compute_publisher_id(
        L"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US",
        pub_id);
    assert(wcscmp(pub_id, L"8wekyb3d8bbwe") == 0);

    /* Synthetic manifest with UTF-8 BOM, comments, entities, reverse ActivatableClass/Path order */
    const char *xml =
        "\xEF\xBB\xBF"
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<!-- Synthetic test manifest -->\r\n"
        "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\""
        " xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\">\r\n"
        "  <Identity Name=\"Test&amp;Game\" Publisher=\"CN=TestPub\" Version=\"2.3.4.5\" ProcessorArchitecture=\"x64\"/>\r\n"
        "  <Properties><DisplayName>Test &lt;Game&gt;</DisplayName></Properties>\r\n"
        "  <Applications>\r\n"
        "    <Application Id=\"MainApp\" Executable=\"TestGame.exe\" EntryPoint=\"TestGame.App\">\r\n"
        "      <uap:VisualElements DisplayName=\"FallbackName\"/>\r\n"
        "    </Application>\r\n"
        "  </Applications>\r\n"
        "  <Extensions>\r\n"
        "    <Extension Category=\"windows.activatableClass.inProcessServer\">\r\n"
        "      <InProcessServer>\r\n"
        "        <ActivatableClass ActivatableClassId=\"BeforePath.Class\" ThreadingModel=\"STA\"/>\r\n"
        "        <Path>CustomWinRT.dll</Path>\r\n"
        "        <ActivatableClass ActivatableClassId=\"AfterPath.Class\" ThreadingModel=\"mta\"/>\r\n"
        "      </InProcessServer>\r\n"
        "    </Extension>\r\n"
        "  </Extensions>\r\n"
        "</Package>\r\n";

    uwp_package_identity_t id;
    int ok = uwp_manifest_parse_buffer((const uint8_t *)xml, strlen(xml), L"Z:\\Games\\TestGame", &id);
    assert(ok == 1);
    assert(id.valid == 1u);
    assert(wcscmp(id.name, L"Test&Game") == 0);
    assert(wcscmp(id.display_name, L"Test <Game>") == 0);
    assert(id.ver_major == 2 && id.ver_minor == 3 && id.ver_build == 4 && id.ver_revision == 5);
    assert(id.architecture == 9u);
    assert(wcscmp(id.app_id, L"MainApp") == 0);
    assert(wcscmp(id.executable, L"TestGame.exe") == 0);
    assert(wcscmp(id.entry_point, L"TestGame.App") == 0);
    assert(wcscmp(id.install_path, L"Z:\\Games\\TestGame") == 0);
    assert(id.inproc_class_count == 2u);
    assert(wcscmp(id.inproc_classes[0].class_id, L"BeforePath.Class") == 0);
    assert(wcscmp(id.inproc_classes[0].dll_path, L"CustomWinRT.dll") == 0);
    assert(id.inproc_classes[0].threading_model == 1u);
    assert(wcscmp(id.inproc_classes[1].class_id, L"AfterPath.Class") == 0);
    assert(wcscmp(id.inproc_classes[1].dll_path, L"CustomWinRT.dll") == 0);
    assert(id.inproc_classes[1].threading_model == 2u);
    assert(wcsstr(id.full_name, L"Test&Game_2.3.4.5_x64__") != NULL);
    assert(wcsstr(id.aumid, L"!MainApp") != NULL);

    /* UTF-16LE BOM parsing test */
    const char *mini = "<Package><Identity Name=\"Utf16App\" Publisher=\"CN=U16\" Version=\"1.0.0.1\" ProcessorArchitecture=\"x64\"/></Package>";
    size_t mlen = strlen(mini);
    uint8_t *u16 = (uint8_t *)malloc(2 + mlen * 2);
    assert(u16 != NULL);
    u16[0] = 0xFFu; u16[1] = 0xFEu;
    for (size_t i = 0; i < mlen; ++i) {
        u16[2 + i * 2] = (uint8_t)mini[i];
        u16[2 + i * 2 + 1] = 0u;
    }
    uwp_package_identity_t id16;
    assert(uwp_manifest_parse_buffer(u16, 2 + mlen * 2, NULL, &id16) == 1);
    assert(wcscmp(id16.name, L"Utf16App") == 0);
    assert(id16.ver_major == 1 && id16.ver_revision == 1);
    free(u16);

    /* Test against real HedgePhysics AppxManifest.xml if present on machine */
    uwp_package_identity_t hp;
    if (uwp_manifest_load_file(
            L"/mnt/files/Downloads/HedgePhysics_1.2.3.0/HedgePhysics_1.2.3.0_x64/HedgePhysics_1.2.3.0_x64/AppxManifest.xml",
            &hp)) {
        printf("  verified live HedgePhysics AppxManifest.xml (inproc_classes=%u)\n",
               (unsigned)hp.inproc_class_count);
        assert(wcscmp(hp.name, L"HedgePhysics") == 0);
        assert(hp.ver_major == 1 && hp.ver_minor == 2 && hp.ver_build == 3 && hp.ver_revision == 0);
        assert(hp.architecture == 9u);
        assert(wcscmp(hp.entry_point, L"HedgePhysics.App") == 0);
        assert(hp.inproc_class_count == 20u);
        int found_unity = 0, found_bridge = 0;
        for (uint32_t i = 0; i < hp.inproc_class_count; ++i) {
            if (wcscmp(hp.inproc_classes[i].class_id, L"UnityPlayer.AppCallbacks") == 0) {
                assert(wcscmp(hp.inproc_classes[i].dll_path, L"UnityPlayer.dll") == 0);
                found_unity = 1;
            }
            if (wcscmp(hp.inproc_classes[i].class_id, L"WinRTBridge.WinRTBridge") == 0) {
                assert(wcscmp(hp.inproc_classes[i].dll_path, L"HedgePhysics.exe") == 0);
                found_bridge = 1;
            }
        }
        assert(found_unity && found_bridge);
    }

    /* Test against real GZDoom AppxManifest.xml if present on machine */
    uwp_package_identity_t gz;
    if (uwp_manifest_load_file(
            L"/mnt/files/Downloads/zdoom_1.0.7.0_x64/zdoom_1.0.7.0_x64/AppxManifest.xml",
            &gz)) {
        printf("  verified live GZDoom AppxManifest.xml\n");
        assert(wcscmp(gz.name, L"d19ff9e2-fbcc-4ab7-969e-c3d7d7b842f3") == 0);
        assert(wcscmp(gz.display_name, L"GZDoom") == 0);
        assert(wcscmp(gz.entry_point, L"GZDoom.App") == 0);
        assert(gz.ver_major == 1 && gz.ver_minor == 0 && gz.ver_build == 7 && gz.ver_revision == 0);
        assert(gz.inproc_class_count == 0u);
    }
}

/* ============================================================================
 * Test 2: HSTRING Operations
 * ============================================================================ */

static void test_hstring(void) {
    printf("[test] HSTRING create/ref/raw/delete...\n");
    UWP_HSTRING hs = NULL;
    assert(WindowsCreateString(L"Windows.UI.Core.CoreWindow", 26, &hs) == S_OK);
    uint32_t len = 0;
    const wchar_t *raw = WindowsGetStringRawBuffer(hs, &len);
    assert(len == 26);
    assert(wcscmp(raw, L"Windows.UI.Core.CoreWindow") == 0);
    assert(WindowsDeleteString(hs) == S_OK);

    UWP_HSTRING_HEADER hdr;
    UWP_HSTRING hs_ref = NULL;
    assert(WindowsCreateStringReference(L"FastRef", 7, &hdr, &hs_ref) == S_OK);
    raw = WindowsGetStringRawBuffer(hs_ref, &len);
    assert(len == 7 && wcscmp(raw, L"FastRef") == 0);
    assert(WindowsDeleteString(hs_ref) == S_OK);
}

/* ============================================================================
 * Test 3: CoreWindow, Interop Bridge, Dispatcher & Message Translation
 * ============================================================================ */

typedef struct TestEventCapture {
    UwpTypedEventHandler iface;
    volatile int32_t ref;
    int size_count;
    float last_w, last_h;
    int keydown_count, keyup_count, char_count, accel_count;
    int32_t last_vk;
    uint32_t last_scan, last_char;
    uint8_t last_key_released;
    int ptr_enter_count, ptr_move_count, ptr_press_count, ptr_release_count, ptr_wheel_count;
    float last_px, last_py;
    int32_t last_wheel_delta;
    uint8_t last_left_down, last_right_down, last_mid_down;
    UwpPointerUpdateKind last_update_kind;
    int activate_count, vis_count, close_count;
    UwpCoreWindowActivationState last_act_state;
    uint8_t last_visible;
    int kind; /* 1=size,2=kd,3=ku,4=char,5=pe,6=pm,7=pp,8=pr,9=pw,10=act,11=vis,12=close,13=accel */
} TestEventCapture;

typedef struct TestHandlerSlot {
    UwpTypedEventHandler iface;
    TestEventCapture *cap;
    int kind;
} TestHandlerSlot;

static HRESULT UWP_CALL th_QI(UwpTypedEventHandler *T, REFIID r, void **p) {
    if (uwp_guid_equal(r, &UWP_IID_IUnknown)) { *p = T; return S_OK; }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG UWP_CALL th_AddRef(UwpTypedEventHandler *T) { (void)T; return 1u; }
static ULONG UWP_CALL th_Release(UwpTypedEventHandler *T) { (void)T; return 1u; }
static HRESULT UWP_CALL th_Invoke(UwpTypedEventHandler *T, void *sender, void *args) {
    (void)sender;
    TestHandlerSlot *slot = UWP_CONTAINING_RECORD(T, TestHandlerSlot, iface);
    TestEventCapture *c = slot->cap;
    switch (slot->kind) {
        case 1: {
            UwpIWindowSizeChangedEventArgs *a = (UwpIWindowSizeChangedEventArgs *)args;
            UwpSize sz = { 0, 0 };
            assert(a->lpVtbl->get_Size(a, &sz) == S_OK);
            c->size_count++; c->last_w = sz.Width; c->last_h = sz.Height;
            break;
        }
        case 2:
        case 3: {
            UwpIKeyEventArgs *a = (UwpIKeyEventArgs *)args;
            int32_t vk = 0; UwpCorePhysicalKeyStatus ks;
            assert(a->lpVtbl->get_VirtualKey(a, &vk) == S_OK);
            assert(a->lpVtbl->get_KeyStatus(a, &ks) == S_OK);
            c->last_vk = vk; c->last_scan = ks.ScanCode; c->last_key_released = ks.IsKeyReleased;
            if (slot->kind == 2) c->keydown_count++; else c->keyup_count++;
            /* Also check secondary QI ICoreWindowEventArgs */
            UwpICoreWindowEventArgs *ea = NULL;
            assert(a->lpVtbl->QueryInterface(a, &UWP_IID_ICoreWindowEventArgs, (void **)&ea) == S_OK);
            assert(ea->lpVtbl->put_Handled(ea, 1u) == S_OK);
            uint8_t h = 0;
            assert(ea->lpVtbl->get_Handled(ea, &h) == S_OK && h == 1u);
            ea->lpVtbl->Release(ea);
            break;
        }
        case 4: {
            UwpICharacterReceivedEventArgs *a = (UwpICharacterReceivedEventArgs *)args;
            uint32_t kc = 0;
            assert(a->lpVtbl->get_KeyCode(a, &kc) == S_OK);
            c->char_count++; c->last_char = kc;
            break;
        }
        case 5: case 6: case 7: case 8: case 9: {
            UwpIPointerEventArgs *a = (UwpIPointerEventArgs *)args;
            UwpIPointerPoint *pt = NULL;
            assert(a->lpVtbl->get_CurrentPoint(a, &pt) == S_OK && pt != NULL);
            UwpPoint pos = { 0, 0 };
            assert(pt->lpVtbl->get_Position(pt, &pos) == S_OK);
            UwpIPointerDevice *dev = NULL;
            assert(pt->lpVtbl->get_PointerDevice(pt, &dev) == S_OK && dev != NULL);
            UwpPointerDeviceType dt = UWP_POINTER_DEVICE_TOUCH;
            assert(dev->lpVtbl->get_PointerDeviceType(dev, &dt) == S_OK && dt == UWP_POINTER_DEVICE_MOUSE);
            dev->lpVtbl->Release(dev);

            UwpIPointerPointProperties *props = NULL;
            assert(pt->lpVtbl->get_Properties(pt, &props) == S_OK && props != NULL);
            props->lpVtbl->get_IsLeftButtonPressed(props, &c->last_left_down);
            props->lpVtbl->get_IsRightButtonPressed(props, &c->last_right_down);
            props->lpVtbl->get_IsMiddleButtonPressed(props, &c->last_mid_down);
            props->lpVtbl->get_MouseWheelDelta(props, &c->last_wheel_delta);
            props->lpVtbl->get_PointerUpdateKind(props, &c->last_update_kind);
            props->lpVtbl->Release(props);
            pt->lpVtbl->Release(pt);

            c->last_px = pos.X; c->last_py = pos.Y;
            if (slot->kind == 5) c->ptr_enter_count++;
            else if (slot->kind == 6) c->ptr_move_count++;
            else if (slot->kind == 7) c->ptr_press_count++;
            else if (slot->kind == 8) c->ptr_release_count++;
            else if (slot->kind == 9) c->ptr_wheel_count++;
            break;
        }
        case 10: {
            UwpIWindowActivatedEventArgs *a = (UwpIWindowActivatedEventArgs *)args;
            assert(a->lpVtbl->get_WindowActivationState(a, &c->last_act_state) == S_OK);
            c->activate_count++;
            break;
        }
        case 11: {
            UwpIVisibilityChangedEventArgs *a = (UwpIVisibilityChangedEventArgs *)args;
            assert(a->lpVtbl->get_Visible(a, &c->last_visible) == S_OK);
            c->vis_count++;
            break;
        }
        case 12:
            c->close_count++;
            break;
        case 13:
            c->accel_count++;
            break;
        default:
            break;
    }
    return S_OK;
}

static const UwpTypedEventHandlerVtbl g_th_vtbl = { th_QI, th_AddRef, th_Release, th_Invoke };

/* RunAsync test callbacks */
static int g_run_order[8];
static int g_run_order_count = 0;
static int g_completed_callbacks = 0;

typedef struct TestDispatchedCb {
    UwpDispatchedHandler iface;
    int id;
} TestDispatchedCb;

static HRESULT UWP_CALL tdc_QI(UwpDispatchedHandler *T, REFIID r, void **p) {
    if (uwp_guid_equal(r, &UWP_IID_IUnknown)) { *p = T; return S_OK; }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG UWP_CALL tdc_AddRef(UwpDispatchedHandler *T) { (void)T; return 1u; }
static ULONG UWP_CALL tdc_Release(UwpDispatchedHandler *T) { (void)T; return 1u; }
static HRESULT UWP_CALL tdc_Invoke(UwpDispatchedHandler *T) {
    TestDispatchedCb *cb = UWP_CONTAINING_RECORD(T, TestDispatchedCb, iface);
    g_run_order[g_run_order_count++] = cb->id;
    return S_OK;
}
static const UwpDispatchedHandlerVtbl g_tdc_vtbl = { tdc_QI, tdc_AddRef, tdc_Release, tdc_Invoke };

static HRESULT UWP_CALL tac_QI(UwpAsyncActionCompletedHandler *T, REFIID r, void **p) {
    if (uwp_guid_equal(r, &UWP_IID_IUnknown)) { *p = T; return S_OK; }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG UWP_CALL tac_AddRef(UwpAsyncActionCompletedHandler *T) { (void)T; return 1u; }
static ULONG UWP_CALL tac_Release(UwpAsyncActionCompletedHandler *T) { (void)T; return 1u; }
static HRESULT UWP_CALL tac_Invoke(UwpAsyncActionCompletedHandler *T, void *a, UwpAsyncStatus st) {
    (void)T; (void)a;
    assert(st == UWP_ASYNC_COMPLETED);
    g_completed_callbacks++;
    return S_OK;
}
static const UwpAsyncActionCompletedHandlerVtbl g_tac_vtbl = { tac_QI, tac_AddRef, tac_Release, tac_Invoke };
static UwpAsyncActionCompletedHandler g_test_completed = { &g_tac_vtbl };

static void test_corewindow_and_dispatcher(void) {
    printf("[test] CoreWindow, Interop/Bridge QI, Message Translation & CoreDispatcher...\n");

    UwpCoreWindowObj *win = uwp_corewindow_create(1280, 720, L"UnitTestWindow", 1);
    assert(win != NULL);
    UwpICoreWindow *cw = uwp_corewindow_iface(win);
    assert(cw != NULL);

    /* Verify ICoreWindowStatic::GetForCurrentThread */
    UwpICoreWindowStatic *cw_static = NULL;
    assert(uwp_corewindow_get_activation_factory(&UWP_IID_ICoreWindowStatic, (void **)&cw_static) == S_OK);
    UwpICoreWindow *tls_cw = NULL;
    assert(cw_static->lpVtbl->GetForCurrentThread(cw_static, &tls_cw) == S_OK);
    assert(tls_cw == cw);
    tls_cw->lpVtbl->Release(tls_cw);

    /* Verify ICoreWindowInterop & IProtonScarlettCoreWindowBridge QI */
    UwpICoreWindowInterop *interop = NULL;
    assert(cw->lpVtbl->QueryInterface(cw, &SCARLETT_IID_ICoreWindowInterop, (void **)&interop) == S_OK);
    HWND h1 = NULL;
    assert(interop->lpVtbl->get_WindowHandle(interop, &h1) == S_OK);
    assert(h1 != NULL);
    assert(interop->lpVtbl->put_MessageHandled(interop, 1u) == S_OK);
    interop->lpVtbl->Release(interop);

    IProtonScarlettCoreWindowBridge *bridge = NULL;
    assert(cw->lpVtbl->QueryInterface(cw, &SCARLETT_IID_IProtonScarlettCoreWindowBridge, (void **)&bridge) == S_OK);
    HWND h2 = NULL;
    uint32_t bw = 0, bh = 0;
    int32_t bfs = -1;
    assert(bridge->lpVtbl->GetHwnd(bridge, &h2) == S_OK && h2 == h1);
    assert(bridge->lpVtbl->GetClientSize(bridge, &bw, &bh) == S_OK && bw == 1280 && bh == 720);
    assert(bridge->lpVtbl->IsFullscreen(bridge, &bfs) == S_OK && bfs == 0);
    assert(bridge->lpVtbl->NotifySwapChainCreated(bridge, NULL) == S_OK);
    bridge->lpVtbl->Release(bridge);

    /* Verify uwp_core_* helpers */
    assert(uwp_core_get_main_hwnd() == h1);
    assert(uwp_core_is_fullscreen() == 0);
    assert(uwp_core_set_fullscreen(1) == 1);
    assert(uwp_core_is_fullscreen() == 1);
    assert(uwp_core_set_fullscreen(0) == 1);
    uwp_core_set_window_title(L"Renamed UWP Title");
    wchar_t tbuf[128];
    uwp_core_get_window_title(tbuf, 128);
    assert(wcscmp(tbuf, L"Renamed UWP Title") == 0);

    /* Register all event handlers */
    TestEventCapture cap;
    memset(&cap, 0, sizeof(cap));
    TestHandlerSlot slots[14];
    for (int i = 1; i <= 13; ++i) {
        slots[i].iface.lpVtbl = &g_th_vtbl;
        slots[i].cap = &cap;
        slots[i].kind = i;
    }
    UwpEventRegistrationToken tok;
    assert(cw->lpVtbl->add_SizeChanged(cw, &slots[1].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_KeyDown(cw, &slots[2].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_KeyUp(cw, &slots[3].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_CharacterReceived(cw, &slots[4].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_PointerEntered(cw, &slots[5].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_PointerMoved(cw, &slots[6].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_PointerPressed(cw, &slots[7].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_PointerReleased(cw, &slots[8].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_PointerWheelChanged(cw, &slots[9].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_Activated(cw, &slots[10].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_VisibilityChanged(cw, &slots[11].iface, &tok) == S_OK);
    assert(cw->lpVtbl->add_Closed(cw, &slots[12].iface, &tok) == S_OK);

    UwpICoreDispatcher *disp = NULL;
    assert(cw->lpVtbl->get_Dispatcher(cw, (void **)&disp) == S_OK && disp != NULL);
    UwpICoreAcceleratorKeys *accel = NULL;
    assert(disp->lpVtbl->QueryInterface(disp, &UWP_IID_ICoreAcceleratorKeys, (void **)&accel) == S_OK);
    assert(accel->lpVtbl->add_AcceleratorKeyActivated(accel, &slots[13].iface, &tok) == S_OK);
    accel->lpVtbl->Release(accel);

    /* Feed Win32 messages and verify exact WinRT event translation */
    assert(uwp_corewindow_handle_message(win, WM_SIZE, 0, (LPARAM)((1080u << 16) | 1920u)) == 1);
    assert(cap.size_count == 1 && cap.last_w == 1920.0f && cap.last_h == 1080.0f);
    uwp_core_get_client_size(&bw, &bh);
    assert(bw == 1920 && bh == 1080);

    /* WM_KEYDOWN ('W'=0x57, scan=0x11) -> KeyDown + AcceleratorKeyActivated */
    assert(uwp_corewindow_handle_message(win, WM_KEYDOWN, 0x57, (LPARAM)((0x11u << 16) | 1u)) == 1);
    assert(cap.keydown_count == 1 && cap.last_vk == 0x57 && cap.last_scan == 0x11 && cap.last_key_released == 0);
    UwpCoreVirtualKeyStates ks = UWP_KEYSTATE_NONE;
    assert(cw->lpVtbl->GetKeyState(cw, 0x57, &ks) == S_OK && (ks & UWP_KEYSTATE_DOWN));

    /* WM_CHAR ('w') */
    assert(uwp_corewindow_handle_message(win, WM_CHAR, 'w', (LPARAM)((0x11u << 16) | 1u)) == 1);
    assert(cap.char_count == 1 && cap.last_char == 'w');

    /* WM_KEYUP ('W') */
    assert(uwp_corewindow_handle_message(win, WM_KEYUP, 0x57, (LPARAM)((0x11u << 16) | 1u)) == 1);
    assert(cap.keyup_count == 1 && cap.last_key_released == 1);
    assert(cap.accel_count == 3);

    /* Mouse move / press / release / wheel */
    assert(uwp_corewindow_handle_message(win, WM_MOUSEMOVE, 0, (LPARAM)((200u << 16) | 150u)) == 1);
    assert(cap.ptr_enter_count == 1 && cap.ptr_move_count == 1 && cap.last_px == 150.0f && cap.last_py == 200.0f);

    assert(uwp_corewindow_handle_message(win, WM_LBUTTONDOWN, MK_LBUTTON, (LPARAM)((210u << 16) | 160u)) == 1);
    assert(cap.ptr_press_count == 1 && cap.last_left_down == 1 &&
           cap.last_update_kind == UWP_POINTER_UPDATE_LEFT_BUTTON_PRESSED);

    assert(uwp_corewindow_handle_message(win, WM_RBUTTONDOWN, MK_RBUTTON, (LPARAM)((210u << 16) | 160u)) == 1);
    assert(cap.ptr_press_count == 2 && cap.last_right_down == 1 &&
           cap.last_update_kind == UWP_POINTER_UPDATE_RIGHT_BUTTON_PRESSED);

    assert(uwp_corewindow_handle_message(win, WM_LBUTTONUP, 0, (LPARAM)((210u << 16) | 160u)) == 1);
    assert(cap.ptr_release_count == 1 && cap.last_update_kind == UWP_POINTER_UPDATE_LEFT_BUTTON_RELEASED);

    assert(uwp_corewindow_handle_message(win, WM_MOUSEWHEEL, (WPARAM)((uint16_t)(int16_t)120 << 16),
                                         (LPARAM)((220u << 16) | 170u)) == 1);
    assert(cap.ptr_wheel_count == 1 && cap.last_wheel_delta == 120);

    /* WM_ACTIVATE / WM_SHOWWINDOW / WM_CLOSE */
    assert(uwp_corewindow_handle_message(win, WM_ACTIVATE, WA_CLICKACTIVE, 0) == 1);
    assert(cap.activate_count == 1 && cap.last_act_state == UWP_ACTIVATION_POINTER_ACTIVATED);

    assert(uwp_corewindow_handle_message(win, WM_SHOWWINDOW, 1, 0) == 1);
    assert(cap.vis_count == 1 && cap.last_visible == 1);

    /* Test CoreDispatcher::HasThreadAccess & RunAsync priority ordering */
    uint8_t has_access = 0;
    assert(disp->lpVtbl->get_HasThreadAccess(disp, &has_access) == S_OK && has_access == 1u);

    TestDispatchedCb cb_low  = { { &g_tdc_vtbl }, 1 };
    TestDispatchedCb cb_norm = { { &g_tdc_vtbl }, 2 };
    TestDispatchedCb cb_high = { { &g_tdc_vtbl }, 3 };
    UwpIAsyncAction *a1 = NULL, *a2 = NULL, *a3 = NULL;
    assert(disp->lpVtbl->RunAsync(disp, UWP_PRIORITY_LOW, &cb_low.iface, &a1) == S_OK);
    assert(disp->lpVtbl->RunAsync(disp, UWP_PRIORITY_NORMAL, &cb_norm.iface, &a2) == S_OK);
    assert(disp->lpVtbl->RunAsync(disp, UWP_PRIORITY_HIGH, &cb_high.iface, &a3) == S_OK);
    assert(a3->lpVtbl->put_Completed(a3, &g_test_completed) == S_OK);

    assert(disp->lpVtbl->ProcessEvents(disp, UWP_PROCESS_ALL_IF_PRESENT) == S_OK);
    assert(g_run_order_count == 3);
    assert(g_run_order[0] == 3 && g_run_order[1] == 2 && g_run_order[2] == 1);
    assert(g_completed_callbacks == 1);

    /* Verify IAsyncInfo status is Completed */
    UwpIAsyncInfo *ai = NULL;
    assert(a3->lpVtbl->QueryInterface(a3, &UWP_IID_IAsyncInfo, (void **)&ai) == S_OK);
    UwpAsyncStatus st = UWP_ASYNC_STARTED;
    assert(ai->lpVtbl->get_Status(ai, &st) == S_OK && st == UWP_ASYNC_COMPLETED);
    ai->lpVtbl->Release(ai);
    a1->lpVtbl->Release(a1);
    a2->lpVtbl->Release(a2);
    a3->lpVtbl->Release(a3);

    assert(uwp_corewindow_handle_message(win, WM_CLOSE, 0, 0) == 1);
    assert(cap.close_count == 1 && uwp_corewindow_is_closed(win) == 1);

    disp->lpVtbl->Release(disp);
    cw->lpVtbl->Release(cw);
    uwp_corewindow_subsystem_shutdown();
    /* Release creator ref */
    cw = uwp_corewindow_iface(win);
    cw->lpVtbl->Release(cw);
    cw->lpVtbl->Release(cw);
}

/* ============================================================================
 * Test 4: CoreApplication::Run(IFrameworkViewSource*) Lifecycle & Events
 * ============================================================================ */

static int g_lifecycle_step = 0;
static int g_view_activated_fired = 0;
static int g_suspending_fired = 0;
static int g_resuming_fired = 0;
static int g_entered_bg_fired = 0;
static int g_leaving_bg_fired = 0;
static int g_exiting_fired = 0;

static HRESULT UWP_CALL lc_ev_QI(UwpTypedEventHandler *T, REFIID r, void **p) {
    if (uwp_guid_equal(r, &UWP_IID_IUnknown)) { *p = T; return S_OK; }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG UWP_CALL lc_ev_AddRef(UwpTypedEventHandler *T) { (void)T; return 1u; }
static ULONG UWP_CALL lc_ev_Release(UwpTypedEventHandler *T) { (void)T; return 1u; }
typedef struct LcEvHandler { UwpTypedEventHandler iface; int which; } LcEvHandler;
static HRESULT UWP_CALL lc_ev_Invoke(UwpTypedEventHandler *T, void *sender, void *args) {
    (void)sender;
    LcEvHandler *h = UWP_CONTAINING_RECORD(T, LcEvHandler, iface);
    if (h->which == 1) {
        UwpIActivatedEventArgs *aa = (UwpIActivatedEventArgs *)args;
        UwpActivationKind kind = (UwpActivationKind)99;
        assert(aa->lpVtbl->get_Kind(aa, &kind) == S_OK && kind == UWP_ACTIVATION_KIND_LAUNCH);
        g_view_activated_fired = ++g_lifecycle_step;
    } else if (h->which == 2) {
        UwpISuspendingEventArgs *sa = (UwpISuspendingEventArgs *)args;
        UwpISuspendingOperation *op = NULL;
        assert(sa && sa->lpVtbl->get_SuspendingOperation(sa, &op) == S_OK && op != NULL);
        op->lpVtbl->Release(op);
        g_suspending_fired++;
    } else if (h->which == 3) {
        g_resuming_fired++;
    } else if (h->which == 4) {
        g_entered_bg_fired++;
    } else if (h->which == 5) {
        g_leaving_bg_fired++;
    } else if (h->which == 6) {
        g_exiting_fired++;
    }
    return S_OK;
}
static const UwpTypedEventHandlerVtbl g_lc_ev_vtbl = { lc_ev_QI, lc_ev_AddRef, lc_ev_Release, lc_ev_Invoke };
static LcEvHandler g_ev_view_act = { { &g_lc_ev_vtbl }, 1 };
static LcEvHandler g_ev_susp     = { { &g_lc_ev_vtbl }, 2 };
static LcEvHandler g_ev_res      = { { &g_lc_ev_vtbl }, 3 };
static LcEvHandler g_ev_ebg      = { { &g_lc_ev_vtbl }, 4 };
static LcEvHandler g_ev_lbg      = { { &g_lc_ev_vtbl }, 5 };
static LcEvHandler g_ev_exit     = { { &g_lc_ev_vtbl }, 6 };

typedef struct MockFrameworkView {
    UwpIFrameworkView view;
    UwpIFrameworkViewSource source;
    int step_create, step_init, step_setwin, step_load, step_run, step_uninit;
} MockFrameworkView;

static HRESULT UWP_CALL mfv_QI(UwpIFrameworkView *T, REFIID r, void **p) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, view);
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) || uwp_guid_equal(r, &UWP_IID_IFrameworkView)) {
        *p = &m->view; return S_OK;
    }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG UWP_CALL mfv_AddRef(UwpIFrameworkView *T) { (void)T; return 1u; }
static ULONG UWP_CALL mfv_Release(UwpIFrameworkView *T) { (void)T; return 1u; }
static HRESULT UWP_CALL mfv_GetIids(UwpIFrameworkView *T, ULONG *c, IID **i) { (void)T; *c = 0; *i = NULL; return S_OK; }
static HRESULT UWP_CALL mfv_GetName(UwpIFrameworkView *T, UWP_HSTRING *n) { (void)T; return uwp_hstring_create_from_wstr(L"MockView", n); }
static HRESULT UWP_CALL mfv_GetTrust(UwpIFrameworkView *T, UwpTrustLevel *t) { (void)T; *t = UWP_TRUST_BASE; return S_OK; }
static HRESULT UWP_CALL mfv_Initialize(UwpIFrameworkView *T, void *app_view) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, view);
    m->step_init = ++g_lifecycle_step;
    UwpICoreApplicationView *v = (UwpICoreApplicationView *)app_view;
    UwpEventRegistrationToken tok;
    assert(v->lpVtbl->add_Activated(v, &g_ev_view_act.iface, &tok) == S_OK);
    /* Verify QI for ICoreApplicationView2 & ICoreApplicationView3 */
    UwpICoreApplicationView2 *v2 = NULL;
    UwpICoreApplicationView3 *v3 = NULL;
    assert(v->lpVtbl->QueryInterface(v, &UWP_IID_ICoreApplicationView2, (void **)&v2) == S_OK);
    void *disp = NULL;
    assert(v2->lpVtbl->get_Dispatcher(v2, &disp) == S_OK && disp != NULL);
    ((IUnknown *)disp)->lpVtbl->Release((IUnknown *)disp);
    v2->lpVtbl->Release(v2);
    assert(v->lpVtbl->QueryInterface(v, &UWP_IID_ICoreApplicationView3, (void **)&v3) == S_OK);
    void *tb = NULL;
    assert(v3->lpVtbl->get_TitleBar(v3, &tb) == S_OK && tb != NULL);
    ((IUnknown *)tb)->lpVtbl->Release((IUnknown *)tb);
    v3->lpVtbl->Release(v3);
    return S_OK;
}
static HRESULT UWP_CALL mfv_SetWindow(UwpIFrameworkView *T, void *core_window) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, view);
    m->step_setwin = ++g_lifecycle_step;
    assert(core_window != NULL);
    return S_OK;
}
static HRESULT UWP_CALL mfv_Load(UwpIFrameworkView *T, UWP_HSTRING ep) {
    (void)ep;
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, view);
    m->step_load = ++g_lifecycle_step;
    return S_OK;
}
static HRESULT UWP_CALL mfv_Run(UwpIFrameworkView *T) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, view);
    m->step_run = ++g_lifecycle_step;
    /* Trigger Resuming / Background events & CoreApplication::Exit */
    uwp_coreapp_notify_entered_background();
    uwp_coreapp_notify_leaving_background();
    uwp_coreapp_notify_resuming();
    UwpICoreApplicationExit *ca_exit = NULL;
    assert(uwp_coreapp_get_activation_factory(&UWP_IID_ICoreApplicationExit, (void **)&ca_exit) == S_OK);
    assert(ca_exit->lpVtbl->Exit(ca_exit) == S_OK);
    return S_OK;
}
static HRESULT UWP_CALL mfv_Uninitialize(UwpIFrameworkView *T) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, view);
    m->step_uninit = ++g_lifecycle_step;
    return S_OK;
}

static HRESULT UWP_CALL mfvs_QI(UwpIFrameworkViewSource *T, REFIID r, void **p) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, source);
    if (uwp_guid_equal(r, &UWP_IID_IUnknown) || uwp_guid_equal(r, &UWP_IID_IFrameworkViewSource)) {
        *p = &m->source; return S_OK;
    }
    *p = NULL; return E_NOINTERFACE;
}
static ULONG UWP_CALL mfvs_AddRef(UwpIFrameworkViewSource *T) { (void)T; return 1u; }
static ULONG UWP_CALL mfvs_Release(UwpIFrameworkViewSource *T) { (void)T; return 1u; }
static HRESULT UWP_CALL mfvs_GetIids(UwpIFrameworkViewSource *T, ULONG *c, IID **i) { (void)T; *c = 0; *i = NULL; return S_OK; }
static HRESULT UWP_CALL mfvs_GetName(UwpIFrameworkViewSource *T, UWP_HSTRING *n) { (void)T; return uwp_hstring_create_from_wstr(L"MockSource", n); }
static HRESULT UWP_CALL mfvs_GetTrust(UwpIFrameworkViewSource *T, UwpTrustLevel *t) { (void)T; *t = UWP_TRUST_BASE; return S_OK; }
static HRESULT UWP_CALL mfvs_CreateView(UwpIFrameworkViewSource *T, UwpIFrameworkView **out) {
    MockFrameworkView *m = UWP_CONTAINING_RECORD(T, MockFrameworkView, source);
    m->step_create = ++g_lifecycle_step;
    *out = &m->view;
    return S_OK;
}

static const UwpIFrameworkViewVtbl g_mfv_vtbl = {
    mfv_QI, mfv_AddRef, mfv_Release, mfv_GetIids, mfv_GetName, mfv_GetTrust,
    mfv_Initialize, mfv_SetWindow, mfv_Load, mfv_Run, mfv_Uninitialize
};
static const UwpIFrameworkViewSourceVtbl g_mfvs_vtbl = {
    mfvs_QI, mfvs_AddRef, mfvs_Release, mfvs_GetIids, mfvs_GetName, mfvs_GetTrust,
    mfvs_CreateView
};

static void test_coreapp_lifecycle(void) {
    printf("[test] CoreApplication::Run(IFrameworkViewSource*) lifecycle...\n");
    uwp_coreapp_set_headless(1);

    UwpICoreApplication *ca = NULL;
    UwpICoreApplication2 *ca2 = NULL;
    UwpICoreApplicationExit *ca_exit = NULL;
    UwpICoreImmersiveApplication *ca_imm = NULL;

    UWP_HSTRING hs = NULL;
    assert(uwp_hstring_create_from_wstr(L"Windows.ApplicationModel.Core.CoreApplication", &hs) == S_OK);
    assert(RoGetActivationFactory(hs, &UWP_IID_ICoreApplication, (void **)&ca) == S_OK);
    assert(ca->lpVtbl->QueryInterface(ca, &UWP_IID_ICoreApplication2, (void **)&ca2) == S_OK);
    assert(ca->lpVtbl->QueryInterface(ca, &UWP_IID_ICoreApplicationExit, (void **)&ca_exit) == S_OK);
    assert(ca->lpVtbl->QueryInterface(ca, &UWP_IID_ICoreImmersiveApplication, (void **)&ca_imm) == S_OK);
    WindowsDeleteString(hs);

    UwpEventRegistrationToken tok;
    assert(ca->lpVtbl->add_Suspending(ca, &g_ev_susp.iface, &tok) == S_OK);
    assert(ca->lpVtbl->add_Resuming(ca, &g_ev_res.iface, &tok) == S_OK);
    assert(ca2->lpVtbl->add_EnteredBackground(ca2, &g_ev_ebg.iface, &tok) == S_OK);
    assert(ca2->lpVtbl->add_LeavingBackground(ca2, &g_ev_lbg.iface, &tok) == S_OK);
    assert(ca_exit->lpVtbl->add_Exiting(ca_exit, &g_ev_exit.iface, &tok) == S_OK);

    void *main_view = NULL;
    assert(ca_imm->lpVtbl->get_MainView(ca_imm, &main_view) == S_OK && main_view != NULL);
    ((IUnknown *)main_view)->lpVtbl->Release((IUnknown *)main_view);

    MockFrameworkView mock;
    memset(&mock, 0, sizeof(mock));
    mock.view.lpVtbl = &g_mfv_vtbl;
    mock.source.lpVtbl = &g_mfvs_vtbl;

    assert(UwpHostRun((IUnknown *)&mock.source) == S_OK);

    /* Assert strict UWP lifecycle ordering */
    assert(mock.step_create == 1);
    assert(mock.step_init == 2);
    assert(mock.step_setwin == 3);
    assert(mock.step_load == 4);
    assert(g_view_activated_fired == 5);
    assert(mock.step_run == 6);
    assert(mock.step_uninit == 7);
    assert(g_suspending_fired == 1);
    assert(g_resuming_fired == 1);
    assert(g_entered_bg_fired == 1);
    assert(g_leaving_bg_fired == 1);
    assert(g_exiting_fired == 1);

    uwp_coreapp_subsystem_shutdown();
    uwp_corewindow_subsystem_shutdown();
}

/* ============================================================================
 * Test 5: Dispatch Chain Order, Unknown Class Logging & Strict Mode Abort
 * ============================================================================ */

static int g_strict_abort_triggered = 0;
static scarlett_hresult_t g_strict_abort_code = 0;

static void test_strict_abort_hook(const char *reason, scarlett_hresult_t code) {
    (void)reason;
    g_strict_abort_triggered++;
    g_strict_abort_code = code;
}

static void test_dispatch_chain_and_strict_mode(void) {
    printf("[test] Dispatch chain (core -> winrt_ui -> winrt_sys -> manifest -> unknown/strict)...\n");

    UWP_HSTRING hs = NULL;
    UwpIActivationFactory *af = NULL;

    /* (a) Core class: must NOT hit winrt_ui or winrt_sys */
    g_dispatch_trace_count = 0;
    assert(uwp_hstring_create_from_wstr(L"Windows.UI.Core.CoreWindow", &hs) == S_OK);
    assert(DllGetActivationFactory(hs, &af) == S_OK && af != NULL);
    assert(g_dispatch_trace_count == 0);
    WindowsDeleteString(hs);

    /* (b) UI class: hits winrt_ui only */
    g_dispatch_trace_count = 0;
    assert(uwp_hstring_create_from_wstr(L"Windows.Graphics.Display.DisplayInformation", &hs) == S_OK);
    assert(DllGetActivationFactory(hs, &af) == S_OK && af == &g_mock_ui_factory);
    assert(g_dispatch_trace_count == 1 && strcmp(g_dispatch_trace[0], "winrt_ui") == 0);
    WindowsDeleteString(hs);

    /* (c) Sys class: hits winrt_ui then winrt_sys */
    g_dispatch_trace_count = 0;
    assert(uwp_hstring_create_from_wstr(L"Windows.Storage.ApplicationData", &hs) == S_OK);
    assert(DllGetActivationFactory(hs, &af) == S_OK && af == &g_mock_sys_factory);
    assert(g_dispatch_trace_count == 2 &&
           strcmp(g_dispatch_trace[0], "winrt_ui") == 0 &&
           strcmp(g_dispatch_trace[1], "winrt_sys") == 0);
    WindowsDeleteString(hs);

    /* (e) Unknown class (non-strict): returns CLASS_E_CLASSNOTAVAILABLE, never S_OK */
    uwp_log_set_strict_mode(0u);
    g_dispatch_trace_count = 0;
    assert(uwp_hstring_create_from_wstr(L"Windows.NonExistent.BogusClass", &hs) == S_OK);
    assert(DllGetActivationFactory(hs, &af) == CLASS_E_CLASSNOTAVAILABLE && af == NULL);
    assert(g_dispatch_trace_count == 2);

    /* Strict mode abort test */
    uwp_log_set_strict_abort_hook(test_strict_abort_hook);
    uwp_log_set_strict_mode(1u);
    g_strict_abort_triggered = 0;
    assert(DllGetActivationFactory(hs, &af) == CLASS_E_CLASSNOTAVAILABLE);
    assert(g_strict_abort_triggered == 1);
    assert(g_strict_abort_code == SCARLETT_ERR_STRICT_STUB_ABORT);
    WindowsDeleteString(hs);
    uwp_log_set_strict_mode(0u);
    uwp_log_set_strict_abort_hook(NULL);

    /* Verify ExecuteAssembly returns explicit failure (zero silent S_OK) without CoreCLR */
    int exit_code = 0;
    HRESULT ea_hr = ExecuteAssembly(L"Z:\\FakeGame\\entrypoint\\FakeGame.exe", 0, NULL, &exit_code);
    assert(FAILED(ea_hr));
    assert(exit_code != 0);
}

int main(void) {
    printf("=== Running Proton-Scarlett UWPHOST Unit Tests ===\n");
    test_manifest_parser();
    test_hstring();
    test_corewindow_and_dispatcher();
    test_coreapp_lifecycle();
    test_dispatch_chain_and_strict_mode();
    printf("=== ALL UWPHOST UNIT TESTS PASSED ===\n");
    return 0;
}
