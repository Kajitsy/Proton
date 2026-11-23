/*
 * Proton-Scarlett — Windows.Foundation.Metadata.ApiInformation
 *
 * Clean-room implementation of IApiInformationStatics returning honest metadata:
 *   - TRUE for WinRT types, methods, properties, events, enums, and contracts
 *     implemented by UWPHOST.dll (uwphost + winrt_ui + winrt_sys).
 *   - FALSE for unimplemented types/members/contracts (e.g. XAML, Holographic, Phone).
 */

#include "winrt_ui_internal.h"

#include <wchar.h>

typedef struct api_info_statics_s {
    UwpIApiInformationStatics statics_iface;
    IActivationFactory        act_iface;
    volatile int32_t          ref_count;
} api_info_statics_t;

typedef struct api_member_entry_s {
    const wchar_t *type_name;
    const wchar_t *member_name;
    uint32_t       arity;
    uint8_t        writable; /* for properties: 1 = read/write, 0 = read-only */
} api_member_entry_t;

typedef struct api_contract_entry_s {
    const wchar_t *contract_name;
    uint16_t       max_major;
    uint16_t       max_minor;
} api_contract_entry_t;

static const wchar_t *const g_present_types[] = {
    /* Core (uwphost) */
    L"Windows.ApplicationModel.Core.CoreApplication",
    L"Windows.ApplicationModel.Core.CoreApplicationView",
    L"Windows.ApplicationModel.Core.IFrameworkViewSource",
    L"Windows.ApplicationModel.Core.IFrameworkView",
    L"Windows.UI.Core.CoreWindow",
    L"Windows.UI.Core.CoreDispatcher",
    L"Windows.UI.Core.CoreProcessEventsOption",
    L"Windows.UI.Core.CoreDispatcherPriority",
    /* UI (winrt_ui) */
    L"Windows.Graphics.Display.DisplayInformation",
    L"Windows.Graphics.Display.DisplayOrientations",
    L"Windows.Graphics.Display.ResolutionScale",
    L"Windows.Graphics.Display.Core.HdmiDisplayInformation",
    L"Windows.Graphics.Display.Core.HdmiDisplayMode",
    L"Windows.Graphics.Display.Core.HdmiDisplayColorSpace",
    L"Windows.Graphics.Display.Core.HdmiDisplayPixelEncoding",
    L"Windows.Graphics.Display.Core.HdmiDisplayHdrOption",
    L"Windows.UI.ViewManagement.ApplicationView",
    L"Windows.UI.ViewManagement.ApplicationViewTitleBar",
    L"Windows.UI.ViewManagement.ApplicationViewBoundsMode",
    L"Windows.UI.ViewManagement.ApplicationViewWindowingMode",
    L"Windows.UI.ViewManagement.ApplicationViewOrientation",
    L"Windows.UI.ViewManagement.ApplicationViewState",
    L"Windows.UI.ViewManagement.ApplicationViewMode",
    L"Windows.UI.ViewManagement.FullScreenSystemOverlayMode",
    L"Windows.UI.ViewManagement.UIViewSettings",
    L"Windows.UI.ViewManagement.UserInteractionMode",
    L"Windows.UI.Core.SystemNavigationManager",
    L"Windows.UI.Core.BackRequestedEventArgs",
    L"Windows.UI.Core.AppViewBackButtonVisibility",
    L"Windows.UI.Core.CoreCursor",
    L"Windows.UI.Core.CoreCursorType",
    L"Windows.Devices.Input.MouseDevice",
    L"Windows.Devices.Input.MouseEventArgs",
    L"Windows.Devices.Input.MouseDelta",
    L"Windows.UI.Input.PointerVisualizationSettings",
    L"Windows.System.Display.DisplayRequest",
    L"Windows.Foundation.Metadata.ApiInformation",
    /* System / Storage / Input (winrt_sys) */
    L"Windows.Storage.ApplicationData",
    L"Windows.Storage.ApplicationDataContainer",
    L"Windows.Storage.StorageFolder",
    L"Windows.ApplicationModel.Package",
    L"Windows.ApplicationModel.PackageId",
    L"Windows.ApplicationModel.PackageVersion",
    L"Windows.Gaming.Input.Gamepad",
    L"Windows.Gaming.Input.GamepadReading",
    L"Windows.Gaming.Input.GamepadVibration",
    L"Windows.Gaming.Input.GamepadButtons",
    L"Windows.Gaming.UI.GameBar",
    L"Windows.System.Profile.AnalyticsInfo",
    L"Windows.System.Profile.AnalyticsVersionInfo"
};

static const api_member_entry_t g_present_methods[] = {
    { L"Windows.Graphics.Display.DisplayInformation", L"GetForCurrentView", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"GetForCurrentView", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"SetDesiredBoundsMode", 1, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"TryEnterFullScreenMode", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"ExitFullScreenMode", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"TryResizeView", 1, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"SetPreferredMinSize", 1, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"IsViewModeSupported", 1, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"TryUnsnap", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"TryUnsnapToFullscreen", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"GetApplicationViewIdForWindow", 1, 0 },
    { L"Windows.UI.ViewManagement.UIViewSettings", L"GetForCurrentView", 0, 0 },
    { L"Windows.UI.Core.SystemNavigationManager", L"GetForCurrentView", 0, 0 },
    { L"Windows.Devices.Input.MouseDevice", L"GetForCurrentView", 0, 0 },
    { L"Windows.UI.Input.PointerVisualizationSettings", L"GetForCurrentView", 0, 0 },
    { L"Windows.System.Display.DisplayRequest", L"RequestActive", 0, 0 },
    { L"Windows.System.Display.DisplayRequest", L"RequestRelease", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayInformation", L"GetForCurrentView", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayInformation", L"GetSupportedDisplayModes", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayInformation", L"GetCurrentDisplayMode", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"IsEqual", 1, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsTypePresent", 1, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsMethodPresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsMethodPresent", 3, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsEventPresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsPropertyPresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsReadOnlyPropertyPresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsWriteablePropertyPresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsEnumNamedValuePresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsApiContractPresent", 2, 0 },
    { L"Windows.Foundation.Metadata.ApiInformation", L"IsApiContractPresent", 3, 0 },
    { L"Windows.ApplicationModel.Core.CoreApplication", L"GetCurrentView", 0, 0 },
    { L"Windows.ApplicationModel.Core.CoreApplication", L"Run", 1, 0 },
    { L"Windows.ApplicationModel.Core.CoreApplication", L"Exit", 0, 0 },
    { L"Windows.UI.Core.CoreWindow", L"GetForCurrentThread", 0, 0 },
    { L"Windows.UI.Core.CoreWindow", L"Activate", 0, 0 },
    { L"Windows.UI.Core.CoreWindow", L"Close", 0, 0 },
    { L"Windows.UI.Core.CoreDispatcher", L"ProcessEvents", 1, 0 },
    { L"Windows.UI.Core.CoreDispatcher", L"RunAsync", 2, 0 },
    { L"Windows.Gaming.Input.Gamepad", L"GetCurrentReading", 0, 0 }
};

static const api_member_entry_t g_present_properties[] = {
    { L"Windows.Graphics.Display.DisplayInformation", L"CurrentOrientation", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"NativeOrientation", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"ResolutionScale", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"LogicalDpi", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"RawDpiX", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"RawDpiY", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"StereoEnabled", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"RawPixelsPerViewPixel", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"DiagonalSizeInInches", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"ScreenWidthInRawPixels", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"ScreenHeightInRawPixels", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"AutoRotationPreferences", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"Orientation", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"AdjacentToLeftDisplayEdge", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"AdjacentToRightDisplayEdge", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"IsFullScreen", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"IsFullScreenMode", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"IsOnLockScreen", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"IsScreenCaptureEnabled", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"Title", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"Id", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"SuppressSystemOverlays", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"VisibleBounds", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"DesiredBoundsMode", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"TitleBar", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"FullScreenSystemOverlayMode", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"ViewMode", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"TerminateAppOnFinalViewClose", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"PreferredLaunchWindowingMode", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"PreferredLaunchViewSize", 0, 1 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"Value", 0, 0 },
    { L"Windows.UI.ViewManagement.UIViewSettings", L"UserInteractionMode", 0, 0 },
    { L"Windows.UI.Input.PointerVisualizationSettings", L"IsContactFeedbackEnabled", 0, 1 },
    { L"Windows.UI.Input.PointerVisualizationSettings", L"IsBarrelButtonFeedbackEnabled", 0, 1 },
    { L"Windows.UI.Core.SystemNavigationManager", L"AppViewBackButtonVisibility", 0, 1 },
    { L"Windows.UI.Core.BackRequestedEventArgs", L"Handled", 0, 1 },
    { L"Windows.UI.Core.CoreCursor", L"Id", 0, 0 },
    { L"Windows.UI.Core.CoreCursor", L"Type", 0, 0 },
    { L"Windows.Devices.Input.MouseEventArgs", L"MouseDelta", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"ResolutionWidthInRawPixels", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"ResolutionHeightInRawPixels", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"RefreshRate", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"StereoEnabled", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"BitsPerPixel", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"ColorSpace", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"PixelEncoding", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"IsSdrLuminanceSupported", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"IsSmpte2084Supported", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"Is2086MetadataSupported", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayMode", L"IsDolbyVisionLowLatencySupported", 0, 0 },
    { L"Windows.Storage.ApplicationData", L"Current", 0, 0 },
    { L"Windows.Storage.ApplicationData", L"LocalFolder", 0, 0 },
    { L"Windows.Storage.ApplicationData", L"RoamingFolder", 0, 0 },
    { L"Windows.Storage.ApplicationData", L"TemporaryFolder", 0, 0 },
    { L"Windows.Storage.ApplicationData", L"LocalSettings", 0, 0 },
    { L"Windows.ApplicationModel.Package", L"Current", 0, 0 },
    { L"Windows.ApplicationModel.Package", L"Id", 0, 0 },
    { L"Windows.ApplicationModel.Package", L"InstalledLocation", 0, 0 },
    { L"Windows.Gaming.Input.Gamepad", L"Gamepads", 0, 0 },
    { L"Windows.Gaming.Input.Gamepad", L"Vibration", 0, 1 }
};

static const api_member_entry_t g_present_events[] = {
    { L"Windows.Graphics.Display.DisplayInformation", L"OrientationChanged", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"DpiChanged", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"StereoEnabledChanged", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"ColorProfileChanged", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"DisplayContentsInvalidated", 0, 0 },
    { L"Windows.Graphics.Display.DisplayInformation", L"AdvancedColorInfoChanged", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"Consolidated", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationView", L"VisibleBoundsChanged", 0, 0 },
    { L"Windows.UI.Core.SystemNavigationManager", L"BackRequested", 0, 0 },
    { L"Windows.Devices.Input.MouseDevice", L"MouseMoved", 0, 0 },
    { L"Windows.Graphics.Display.Core.HdmiDisplayInformation", L"DisplayModesChanged", 0, 0 },
    { L"Windows.ApplicationModel.Core.CoreApplication", L"Suspending", 0, 0 },
    { L"Windows.ApplicationModel.Core.CoreApplication", L"Resuming", 0, 0 },
    { L"Windows.Gaming.Input.Gamepad", L"GamepadAdded", 0, 0 },
    { L"Windows.Gaming.Input.Gamepad", L"GamepadRemoved", 0, 0 }
};

static const api_member_entry_t g_present_enum_values[] = {
    { L"Windows.Graphics.Display.DisplayOrientations", L"None", 0, 0 },
    { L"Windows.Graphics.Display.DisplayOrientations", L"Landscape", 0, 0 },
    { L"Windows.Graphics.Display.DisplayOrientations", L"Portrait", 0, 0 },
    { L"Windows.Graphics.Display.DisplayOrientations", L"LandscapeFlipped", 0, 0 },
    { L"Windows.Graphics.Display.DisplayOrientations", L"PortraitFlipped", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewWindowingMode", L"Auto", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewWindowingMode", L"PreferredLaunchViewSize", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewWindowingMode", L"FullScreen", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewWindowingMode", L"CompactOverlay", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewWindowingMode", L"Maximized", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewBoundsMode", L"UseVisible", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewBoundsMode", L"UseCoreWindow", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewOrientation", L"Landscape", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewOrientation", L"Portrait", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewMode", L"Default", 0, 0 },
    { L"Windows.UI.ViewManagement.ApplicationViewMode", L"CompactOverlay", 0, 0 },
    { L"Windows.UI.ViewManagement.FullScreenSystemOverlayMode", L"Standard", 0, 0 },
    { L"Windows.UI.ViewManagement.FullScreenSystemOverlayMode", L"Minimal", 0, 0 },
    { L"Windows.UI.ViewManagement.UserInteractionMode", L"Mouse", 0, 0 },
    { L"Windows.UI.ViewManagement.UserInteractionMode", L"Touch", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Arrow", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Cross", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Custom", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Hand", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Help", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"IBeam", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"SizeAll", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"SizeNortheastSouthwest", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"SizeNorthSouth", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"SizeNorthwestSoutheast", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"SizeWestEast", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"UniversalNo", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"UpArrow", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Wait", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Pin", 0, 0 },
    { L"Windows.UI.Core.CoreCursorType", L"Person", 0, 0 },
    { L"Windows.UI.Core.AppViewBackButtonVisibility", L"Visible", 0, 0 },
    { L"Windows.UI.Core.AppViewBackButtonVisibility", L"Collapsed", 0, 0 },
    { L"Windows.UI.Core.AppViewBackButtonVisibility", L"Disabled", 0, 0 }
};

static const api_contract_entry_t g_present_contracts[] = {
    { L"Windows.Foundation.FoundationContract",   4, 0 },
    { L"Windows.Foundation.UniversalApiContract", 10, 0 }
};

static boolean api_lookup_type(const wchar_t *type_name) {
    if (!type_name || !*type_name) return FALSE;
    for (size_t i = 0; i < sizeof(g_present_types) / sizeof(g_present_types[0]); ++i) {
        if (wcscmp(type_name, g_present_types[i]) == 0) return TRUE;
    }
    return FALSE;
}

static boolean api_lookup_member(const api_member_entry_t *table, size_t count,
                                 const wchar_t *type_name, const wchar_t *member_name,
                                 int check_arity, uint32_t arity,
                                 int check_writable, uint8_t writable) {
    if (!type_name || !member_name) return FALSE;
    for (size_t i = 0; i < count; ++i) {
        if (wcscmp(type_name, table[i].type_name) == 0 &&
            wcscmp(member_name, table[i].member_name) == 0) {
            if (check_arity && table[i].arity != arity) continue;
            if (check_writable && table[i].writable != writable) continue;
            return TRUE;
        }
    }
    return FALSE;
}

static HRESULT ai_qi(api_info_statics_t *fac, REFIID riid, void **ppv) {
    if (!riid || !ppv) return E_INVALIDARG;
    *ppv = NULL;
    if (IsEqualGUID(riid, &SCARLETT_IID_IUnknown) ||
        IsEqualGUID(riid, &SCARLETT_IID_IInspectable) ||
        IsEqualGUID(riid, &SCARLETT_IID_IAgileObject) ||
        IsEqualGUID(riid, &SCARLETT_IID_IApiInformationStatics)) {
        *ppv = &fac->statics_iface;
    } else if (IsEqualGUID(riid, &SCARLETT_IID_IActivationFactory)) {
        *ppv = &fac->act_iface;
    } else {
        return E_NOINTERFACE;
    }
    uwp_ui_atomic_inc(&fac->ref_count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ais_QueryInterface(UwpIApiInformationStatics *This, REFIID riid, void **ppv) {
    return ai_qi(CONTAINING_RECORD(This, api_info_statics_t, statics_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE ais_AddRef(UwpIApiInformationStatics *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, api_info_statics_t, statics_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE ais_Release(UwpIApiInformationStatics *This) {
    api_info_statics_t *fac = CONTAINING_RECORD(This, api_info_statics_t, statics_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE ais_GetIids(UwpIApiInformationStatics *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IApiInformationStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE ais_GetRuntimeClassName(UwpIApiInformationStatics *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Foundation.Metadata.ApiInformation", name);
}
static HRESULT STDMETHODCALLTYPE ais_GetTrustLevel(UwpIApiInformationStatics *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsTypePresent(UwpIApiInformationStatics *This, HSTRING typeName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_type(t);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsMethodPresent(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING methodName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    const wchar_t *m = uwp_ui_hstring_raw(methodName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_methods, sizeof(g_present_methods) / sizeof(g_present_methods[0]),
                               t, m, 0, 0, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsMethodPresentWithArity(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING methodName, UINT32 arity, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    const wchar_t *m = uwp_ui_hstring_raw(methodName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_methods, sizeof(g_present_methods) / sizeof(g_present_methods[0]),
                               t, m, 1, arity, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsEventPresent(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING eventName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    const wchar_t *e = uwp_ui_hstring_raw(eventName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_events, sizeof(g_present_events) / sizeof(g_present_events[0]),
                               t, e, 0, 0, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsPropertyPresent(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING propertyName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    const wchar_t *p = uwp_ui_hstring_raw(propertyName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_properties, sizeof(g_present_properties) / sizeof(g_present_properties[0]),
                               t, p, 0, 0, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsReadOnlyPropertyPresent(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING propertyName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    const wchar_t *p = uwp_ui_hstring_raw(propertyName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_properties, sizeof(g_present_properties) / sizeof(g_present_properties[0]),
                               t, p, 0, 0, 1, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsWriteablePropertyPresent(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING propertyName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(typeName, NULL);
    const wchar_t *p = uwp_ui_hstring_raw(propertyName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_properties, sizeof(g_present_properties) / sizeof(g_present_properties[0]),
                               t, p, 0, 0, 1, 1);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsEnumNamedValuePresent(UwpIApiInformationStatics *This, HSTRING enumTypeName, HSTRING valueName, boolean *value) {
    const wchar_t *t = uwp_ui_hstring_raw(enumTypeName, NULL);
    const wchar_t *v = uwp_ui_hstring_raw(valueName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = api_lookup_member(g_present_enum_values, sizeof(g_present_enum_values) / sizeof(g_present_enum_values[0]),
                               t, v, 0, 0, 0, 0);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsApiContractPresentByMajorAndMinor(UwpIApiInformationStatics *This, HSTRING contractName, UINT16 major, UINT16 minor, boolean *value) {
    const wchar_t *c = uwp_ui_hstring_raw(contractName, NULL);
    (void)This;
    if (!value) return E_POINTER;
    *value = FALSE;
    if (!c) return S_OK;
    for (size_t i = 0; i < sizeof(g_present_contracts) / sizeof(g_present_contracts[0]); ++i) {
        if (wcscmp(c, g_present_contracts[i].contract_name) == 0) {
            if (major < g_present_contracts[i].max_major ||
                (major == g_present_contracts[i].max_major && minor <= g_present_contracts[i].max_minor)) {
                *value = TRUE;
            }
            break;
        }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ais_IsApiContractPresentByMajor(UwpIApiInformationStatics *This, HSTRING contractName, UINT16 major, boolean *value) {
    return ais_IsApiContractPresentByMajorAndMinor(This, contractName, major, 0, value);
}

static const UwpIApiInformationStaticsVtbl g_ais_vtbl = {
    ais_QueryInterface, ais_AddRef, ais_Release,
    ais_GetIids, ais_GetRuntimeClassName, ais_GetTrustLevel,
    ais_IsTypePresent,
    ais_IsMethodPresent,
    ais_IsMethodPresentWithArity,
    ais_IsEventPresent,
    ais_IsPropertyPresent,
    ais_IsReadOnlyPropertyPresent,
    ais_IsWriteablePropertyPresent,
    ais_IsEnumNamedValuePresent,
    ais_IsApiContractPresentByMajor,
    ais_IsApiContractPresentByMajorAndMinor
};

static HRESULT STDMETHODCALLTYPE aia_QueryInterface(IActivationFactory *This, REFIID riid, void **ppv) {
    return ai_qi(CONTAINING_RECORD(This, api_info_statics_t, act_iface), riid, ppv);
}
static ULONG STDMETHODCALLTYPE aia_AddRef(IActivationFactory *This) {
    return uwp_ui_atomic_inc(&CONTAINING_RECORD(This, api_info_statics_t, act_iface)->ref_count);
}
static ULONG STDMETHODCALLTYPE aia_Release(IActivationFactory *This) {
    api_info_statics_t *fac = CONTAINING_RECORD(This, api_info_statics_t, act_iface);
    ULONG ref = uwp_ui_atomic_dec(&fac->ref_count);
    if (ref == 0) uwp_ui_free(fac);
    return ref;
}
static HRESULT STDMETHODCALLTYPE aia_GetIids(IActivationFactory *This, ULONG *c, IID **iids) {
    (void)This;
    const GUID list[] = { SCARLETT_IID_IApiInformationStatics, SCARLETT_IID_IActivationFactory };
    return uwp_ui_get_iids(list, 2, c, iids);
}
static HRESULT STDMETHODCALLTYPE aia_GetRuntimeClassName(IActivationFactory *This, HSTRING *name) {
    (void)This; return uwp_ui_get_runtime_class_name(L"Windows.Foundation.Metadata.ApiInformation", name);
}
static HRESULT STDMETHODCALLTYPE aia_GetTrustLevel(IActivationFactory *This, TrustLevel *tl) {
    (void)This; if (!tl) return E_POINTER; *tl = BaseTrust; return S_OK;
}
static HRESULT STDMETHODCALLTYPE aia_ActivateInstance(IActivationFactory *This, IInspectable **instance) {
    (void)This;
    if (instance) *instance = NULL;
    return uwp_ui_unimpl("ApiInformationFactory", "ActivateInstance");
}

static const IActivationFactoryVtbl g_aia_vtbl = {
    aia_QueryInterface, aia_AddRef, aia_Release,
    aia_GetIids, aia_GetRuntimeClassName, aia_GetTrustLevel,
    aia_ActivateInstance
};

HRESULT uwp_ui_api_info_get_factory(REFIID iid, void **out_factory) {
    api_info_statics_t *fac = (api_info_statics_t *)uwp_ui_alloc(sizeof(api_info_statics_t));
    if (!fac) return E_OUTOFMEMORY;
    fac->statics_iface.lpVtbl = &g_ais_vtbl;
    fac->act_iface.lpVtbl = &g_aia_vtbl;
    fac->ref_count = 1;
    HRESULT hr = ai_qi(fac, iid, out_factory);
    uwp_ui_atomic_dec(&fac->ref_count);
    if (FAILED(hr)) uwp_ui_free(fac);
    return hr;
}
