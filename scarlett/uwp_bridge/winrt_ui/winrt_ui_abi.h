/*
 * Proton-Scarlett — UWP Bridge WinRT UI ABI Definitions
 *
 * Clean-room C11 / C++20 compatible COM/WinRT ABI contract for:
 *   - Windows.Graphics.Display.DisplayInformation
 *   - Windows.UI.ViewManagement.ApplicationView (+ ApplicationViewTitleBar)
 *   - Windows.UI.Core.SystemNavigationManager (+ BackRequestedEventArgs)
 *   - Windows.UI.Core.CoreCursor
 *   - Windows.Devices.Input.MouseDevice (+ MouseEventArgs)
 *   - Windows.System.Display.DisplayRequest
 *   - Windows.Foundation.Metadata.ApiInformation
 *   - Windows.Graphics.Display.Core.HdmiDisplayInformation (+ HdmiDisplayMode)
 *
 * Derived cleanly from Wine IDL definitions and documented WinRT ABI layouts.
 * Compiles identically under x86_64-w64-mingw32 (PE64) and host C11/C++20 tests.
 */

#ifndef SCARLETT_UWP_WINRT_UI_ABI_H
#define SCARLETT_UWP_WINRT_UI_ABI_H

#include "../include/uwp_bridge_abi.h"

#ifdef _WIN32
#include <inspectable.h>
#include <hstring.h>
#include <eventtoken.h>
#else
#include <string.h>
#include <wchar.h>

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef WINAPI
#define WINAPI
#endif

typedef uint8_t  BYTE;
typedef uint16_t UINT16;
typedef int64_t  INT64;
typedef uint64_t UINT64;
typedef float    FLOAT;
typedef double   DOUBLE;
typedef uint8_t  boolean;
typedef wchar_t  WCHAR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR *LPWSTR;
typedef GUID IID;

#ifndef S_OK
#define S_OK                       ((HRESULT)0x00000000L)
#define S_FALSE                    ((HRESULT)0x00000001L)
#define E_NOTIMPL                  ((HRESULT)0x80004001L)
#define E_NOINTERFACE              ((HRESULT)0x80004002L)
#define E_POINTER                  ((HRESULT)0x80004003L)
#define E_FAIL                     ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY              ((HRESULT)0x8007000EL)
#define E_INVALIDARG               ((HRESULT)0x80070057L)
#define CLASS_E_CLASSNOTAVAILABLE  ((HRESULT)0x80040111L)
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

static inline int IsEqualGUID(const GUID *a, const GUID *b) {
    return a && b && memcmp(a, b, sizeof(GUID)) == 0;
}

typedef struct HSTRING__ {
    int unused;
} HSTRING__;
typedef HSTRING__ *HSTRING;

typedef struct EventRegistrationToken {
    INT64 value;
} EventRegistrationToken;

typedef enum TrustLevel {
    BaseTrust = 0,
    PartialTrust = 1,
    FullTrust = 2
} TrustLevel;

typedef struct IUnknown IUnknown;
typedef struct IUnknownVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IUnknown *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IUnknown *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IUnknown *This);
} IUnknownVtbl;
struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};

typedef struct IInspectable IInspectable;
typedef struct IInspectableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IInspectable *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IInspectable *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IInspectable *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IInspectable *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IInspectable *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IInspectable *This, TrustLevel *trustLevel);
} IInspectableVtbl;
struct IInspectable {
    const IInspectableVtbl *lpVtbl;
};
#endif /* !_WIN32 */

#ifndef __IActivationFactory_INTERFACE_DEFINED__
#define __IActivationFactory_INTERFACE_DEFINED__
typedef struct IActivationFactory IActivationFactory;
typedef struct IActivationFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IActivationFactory *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IActivationFactory *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IActivationFactory *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IActivationFactory *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IActivationFactory *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IActivationFactory *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *ActivateInstance)(IActivationFactory *This, IInspectable **instance);
} IActivationFactoryVtbl;
struct IActivationFactory {
    const IActivationFactoryVtbl *lpVtbl;
};
#endif /* !__IActivationFactory_INTERFACE_DEFINED__ */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Standard COM / WinRT Base GUIDs
 * ============================================================================ */

static const GUID SCARLETT_IID_IUnknown =
    { 0x00000000u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const GUID SCARLETT_IID_IInspectable = SCARLETT_IID_IINSPECTABLE_INIT;
static const GUID SCARLETT_IID_IActivationFactory = SCARLETT_IID_IACTIVATIONFACTORY_INIT;
static const GUID SCARLETT_IID_IAgileObject = SCARLETT_IID_IAGILEOBJECT_INIT;

/* ============================================================================
 * Foundation Value Structs & Generic Event Handler Interfaces
 * ============================================================================ */

typedef struct UwpSize {
    FLOAT Width;
    FLOAT Height;
} UwpSize;

typedef struct UwpRect {
    FLOAT X;
    FLOAT Y;
    FLOAT Width;
    FLOAT Height;
} UwpRect;

typedef struct UwpColor {
    uint8_t A;
    uint8_t R;
    uint8_t G;
    uint8_t B;
} UwpColor;

typedef struct UwpMouseDelta {
    INT32 X;
    INT32 Y;
} UwpMouseDelta;

typedef struct UwpHdmiDisplayHdr2086Metadata {
    UINT16 RedPrimaryX;
    UINT16 RedPrimaryY;
    UINT16 GreenPrimaryX;
    UINT16 GreenPrimaryY;
    UINT16 BluePrimaryX;
    UINT16 BluePrimaryY;
    UINT16 WhitePointX;
    UINT16 WhitePointY;
    UINT16 MaxMasteringLuminance;
    UINT16 MinMasteringLuminance;
    UINT16 MaxContentLightLevel;
    UINT16 MaxFrameAverageLightLevel;
} UwpHdmiDisplayHdr2086Metadata;

typedef struct UwpEventHandler UwpEventHandler;
typedef struct UwpEventHandlerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpEventHandler *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpEventHandler *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpEventHandler *This);
    HRESULT (STDMETHODCALLTYPE *Invoke)(UwpEventHandler *This, void *sender, void *args);
} UwpEventHandlerVtbl;
struct UwpEventHandler {
    const UwpEventHandlerVtbl *lpVtbl;
};

/* ============================================================================
 * 1. Windows.Graphics.Display.DisplayInformation
 * ============================================================================ */

typedef enum UwpDisplayOrientations {
    UwpDisplayOrientations_None             = 0,
    UwpDisplayOrientations_Landscape        = 1,
    UwpDisplayOrientations_Portrait         = 2,
    UwpDisplayOrientations_LandscapeFlipped = 4,
    UwpDisplayOrientations_PortraitFlipped  = 8
} UwpDisplayOrientations;

typedef enum UwpResolutionScale {
    UwpResolutionScale_Invalid         = 0,
    UwpResolutionScale_Scale100Percent = 100,
    UwpResolutionScale_Scale120Percent = 120,
    UwpResolutionScale_Scale125Percent = 125,
    UwpResolutionScale_Scale140Percent = 140,
    UwpResolutionScale_Scale150Percent = 150,
    UwpResolutionScale_Scale160Percent = 160,
    UwpResolutionScale_Scale175Percent = 175,
    UwpResolutionScale_Scale180Percent = 180,
    UwpResolutionScale_Scale200Percent = 200,
    UwpResolutionScale_Scale225Percent = 225,
    UwpResolutionScale_Scale250Percent = 250,
    UwpResolutionScale_Scale300Percent = 300,
    UwpResolutionScale_Scale350Percent = 350,
    UwpResolutionScale_Scale400Percent = 400,
    UwpResolutionScale_Scale450Percent = 450,
    UwpResolutionScale_Scale500Percent = 500
} UwpResolutionScale;

/* IDisplayInformationStatics: {C6A02A6C-D452-44DC-BA07-96F3C6ADF9D1} */
static const GUID SCARLETT_IID_IDisplayInformationStatics =
    { 0xC6A02A6Cu, 0xD452u, 0x44DCu, { 0xBAu, 0x07u, 0x96u, 0xF3u, 0xC6u, 0xADu, 0xF9u, 0xD1u } };

/* IDisplayInformation: {BED112AE-ADC3-4DC9-AE65-851F4D7D4799} */
static const GUID SCARLETT_IID_IDisplayInformation =
    { 0xBED112AEu, 0xADC3u, 0x4DC9u, { 0xAEu, 0x65u, 0x85u, 0x1Fu, 0x4Du, 0x7Du, 0x47u, 0x99u } };

/* IDisplayInformation2: {4DCD0021-FAD1-4B8E-8EDF-775887B8BF19} */
static const GUID SCARLETT_IID_IDisplayInformation2 =
    { 0x4DCD0021u, 0xFAD1u, 0x4B8Eu, { 0x8Eu, 0xDFu, 0x77u, 0x58u, 0x87u, 0xB8u, 0xBFu, 0x19u } };

/* IDisplayInformation3: {DB15011D-0F09-4466-8FF3-11DE9A3C929A} */
static const GUID SCARLETT_IID_IDisplayInformation3 =
    { 0xDB15011Du, 0x0F09u, 0x4466u, { 0x8Fu, 0xF3u, 0x11u, 0xDEu, 0x9Au, 0x3Cu, 0x92u, 0x9Au } };

/* IDisplayInformation4: {C972CE2F-1242-46BE-B536-E1AAFE9E7ACF} */
static const GUID SCARLETT_IID_IDisplayInformation4 =
    { 0xC972CE2Fu, 0x1242u, 0x46BEu, { 0xB5u, 0x36u, 0xE1u, 0xAAu, 0xFEu, 0x9Eu, 0x7Au, 0xCFu } };

/* IDisplayInformation5: {3A5442DC-2CDE-4A8D-80D1-21DC5ADCC1AA} */
static const GUID SCARLETT_IID_IDisplayInformation5 =
    { 0x3A5442DCu, 0x2CDEu, 0x4A8Du, { 0x80u, 0xD1u, 0x21u, 0xDCu, 0x5Au, 0xDCu, 0xC1u, 0xAAu } };

typedef struct UwpIDisplayInformation UwpIDisplayInformation;
typedef struct UwpIDisplayInformationVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayInformation *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayInformation *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayInformation *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayInformation *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayInformation *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayInformation *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_CurrentOrientation)(UwpIDisplayInformation *This, UwpDisplayOrientations *value);
    HRESULT (STDMETHODCALLTYPE *get_NativeOrientation)(UwpIDisplayInformation *This, UwpDisplayOrientations *value);
    HRESULT (STDMETHODCALLTYPE *add_OrientationChanged)(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_OrientationChanged)(UwpIDisplayInformation *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *get_ResolutionScale)(UwpIDisplayInformation *This, UwpResolutionScale *value);
    HRESULT (STDMETHODCALLTYPE *get_LogicalDpi)(UwpIDisplayInformation *This, FLOAT *value);
    HRESULT (STDMETHODCALLTYPE *get_RawDpiX)(UwpIDisplayInformation *This, FLOAT *value);
    HRESULT (STDMETHODCALLTYPE *get_RawDpiY)(UwpIDisplayInformation *This, FLOAT *value);
    HRESULT (STDMETHODCALLTYPE *add_DpiChanged)(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_DpiChanged)(UwpIDisplayInformation *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *get_StereoEnabled)(UwpIDisplayInformation *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *add_StereoEnabledChanged)(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_StereoEnabledChanged)(UwpIDisplayInformation *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *GetColorProfileAsync)(UwpIDisplayInformation *This, void **asyncInfo);
    HRESULT (STDMETHODCALLTYPE *add_ColorProfileChanged)(UwpIDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_ColorProfileChanged)(UwpIDisplayInformation *This, EventRegistrationToken token);
} UwpIDisplayInformationVtbl;
struct UwpIDisplayInformation {
    const UwpIDisplayInformationVtbl *lpVtbl;
};

typedef struct UwpIDisplayInformation2 UwpIDisplayInformation2;
typedef struct UwpIDisplayInformation2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayInformation2 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayInformation2 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayInformation2 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayInformation2 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayInformation2 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayInformation2 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_RawPixelsPerViewPixel)(UwpIDisplayInformation2 *This, DOUBLE *value);
} UwpIDisplayInformation2Vtbl;
struct UwpIDisplayInformation2 {
    const UwpIDisplayInformation2Vtbl *lpVtbl;
};

typedef struct UwpIDisplayInformation3 UwpIDisplayInformation3;
typedef struct UwpIDisplayInformation3Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayInformation3 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayInformation3 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayInformation3 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayInformation3 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayInformation3 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayInformation3 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_DiagonalSizeInInches)(UwpIDisplayInformation3 *This, void **value);
} UwpIDisplayInformation3Vtbl;
struct UwpIDisplayInformation3 {
    const UwpIDisplayInformation3Vtbl *lpVtbl;
};

typedef struct UwpIDisplayInformation4 UwpIDisplayInformation4;
typedef struct UwpIDisplayInformation4Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayInformation4 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayInformation4 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayInformation4 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayInformation4 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayInformation4 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayInformation4 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_ScreenWidthInRawPixels)(UwpIDisplayInformation4 *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_ScreenHeightInRawPixels)(UwpIDisplayInformation4 *This, UINT32 *value);
} UwpIDisplayInformation4Vtbl;
struct UwpIDisplayInformation4 {
    const UwpIDisplayInformation4Vtbl *lpVtbl;
};

typedef struct UwpIDisplayInformation5 UwpIDisplayInformation5;
typedef struct UwpIDisplayInformation5Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayInformation5 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayInformation5 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayInformation5 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayInformation5 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayInformation5 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayInformation5 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAdvancedColorInfo)(UwpIDisplayInformation5 *This, void **value);
    HRESULT (STDMETHODCALLTYPE *add_AdvancedColorInfoChanged)(UwpIDisplayInformation5 *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_AdvancedColorInfoChanged)(UwpIDisplayInformation5 *This, EventRegistrationToken token);
} UwpIDisplayInformation5Vtbl;
struct UwpIDisplayInformation5 {
    const UwpIDisplayInformation5Vtbl *lpVtbl;
};

typedef struct UwpIDisplayInformationStatics UwpIDisplayInformationStatics;
typedef struct UwpIDisplayInformationStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayInformationStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayInformationStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayInformationStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayInformationStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayInformationStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayInformationStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(UwpIDisplayInformationStatics *This, UwpIDisplayInformation **current);
    HRESULT (STDMETHODCALLTYPE *get_AutoRotationPreferences)(UwpIDisplayInformationStatics *This, UwpDisplayOrientations *value);
    HRESULT (STDMETHODCALLTYPE *put_AutoRotationPreferences)(UwpIDisplayInformationStatics *This, UwpDisplayOrientations value);
    HRESULT (STDMETHODCALLTYPE *add_DisplayContentsInvalidated)(UwpIDisplayInformationStatics *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_DisplayContentsInvalidated)(UwpIDisplayInformationStatics *This, EventRegistrationToken token);
} UwpIDisplayInformationStaticsVtbl;
struct UwpIDisplayInformationStatics {
    const UwpIDisplayInformationStaticsVtbl *lpVtbl;
};

/* ============================================================================
 * 2. Windows.UI.ViewManagement.ApplicationView
 * ============================================================================ */

typedef enum UwpApplicationViewBoundsMode {
    UwpApplicationViewBoundsMode_UseVisible    = 0,
    UwpApplicationViewBoundsMode_UseCoreWindow = 1
} UwpApplicationViewBoundsMode;

typedef enum UwpApplicationViewMode {
    UwpApplicationViewMode_Default        = 0,
    UwpApplicationViewMode_CompactOverlay = 1
} UwpApplicationViewMode;

typedef enum UwpApplicationViewOrientation {
    UwpApplicationViewOrientation_Landscape = 0,
    UwpApplicationViewOrientation_Portrait  = 1
} UwpApplicationViewOrientation;

typedef enum UwpApplicationViewState {
    UwpApplicationViewState_FullScreenLandscape = 0,
    UwpApplicationViewState_Filled              = 1,
    UwpApplicationViewState_Snapped             = 2,
    UwpApplicationViewState_FullScreenPortrait  = 3
} UwpApplicationViewState;

typedef enum UwpApplicationViewWindowingMode {
    UwpApplicationViewWindowingMode_Auto                    = 0,
    UwpApplicationViewWindowingMode_PreferredLaunchViewSize = 1,
    UwpApplicationViewWindowingMode_FullScreen              = 2,
    UwpApplicationViewWindowingMode_CompactOverlay          = 3,
    UwpApplicationViewWindowingMode_Maximized               = 4
} UwpApplicationViewWindowingMode;

typedef enum UwpFullScreenSystemOverlayMode {
    UwpFullScreenSystemOverlayMode_Standard = 0,
    UwpFullScreenSystemOverlayMode_Minimal  = 1
} UwpFullScreenSystemOverlayMode;

/* Wine IDL IIDs for ApplicationView */
static const GUID SCARLETT_IID_IApplicationView =
    { 0xD222D519u, 0x4361u, 0x451Eu, { 0x96u, 0xC4u, 0x60u, 0xF4u, 0xF9u, 0x74u, 0x2Du, 0xB0u } };
static const GUID SCARLETT_IID_IApplicationView2 =
    { 0xE876B196u, 0xA545u, 0x40DCu, { 0xB5u, 0x94u, 0x45u, 0x0Cu, 0xBAu, 0x68u, 0xCCu, 0x00u } };
static const GUID SCARLETT_IID_IApplicationView3 =
    { 0x903C9CE5u, 0x793Au, 0x4FDFu, { 0xA2u, 0xB2u, 0xAFu, 0x1Au, 0xC2u, 0x1Eu, 0x31u, 0x08u } };
static const GUID SCARLETT_IID_IApplicationView4 =
    { 0x15E5CBECu, 0x9E0Fu, 0x46B5u, { 0xBCu, 0x3Fu, 0x9Bu, 0xF6u, 0x53u, 0xE7u, 0x4Bu, 0x5Eu } };
static const GUID SCARLETT_IID_IApplicationViewStatics =
    { 0x010A6306u, 0xC433u, 0x44E5u, { 0xA9u, 0xF2u, 0xBDu, 0x84u, 0xD4u, 0x03u, 0x0Au, 0x95u } };
static const GUID SCARLETT_IID_IApplicationViewStatics2 =
    { 0xAF338AE5u, 0xCF64u, 0x423Cu, { 0x85u, 0xE5u, 0xF3u, 0xE7u, 0x24u, 0x48u, 0xFBu, 0x23u } };
static const GUID SCARLETT_IID_IApplicationViewStatics3 =
    { 0xA28D7594u, 0x8C41u, 0x4E13u, { 0x97u, 0x19u, 0x51u, 0x64u, 0x79u, 0x6Fu, 0xE4u, 0xC7u } };
static const GUID SCARLETT_IID_IApplicationViewFullscreenStatics =
    { 0xBC792EBDu, 0x64FEu, 0x4B65u, { 0xA0u, 0xC0u, 0x90u, 0x1Cu, 0xE2u, 0xB6u, 0x86u, 0x36u } };
static const GUID SCARLETT_IID_IApplicationViewInteropStatics =
    { 0xC446FB5Du, 0x4793u, 0x4896u, { 0xA8u, 0xE2u, 0xBEu, 0x57u, 0xA8u, 0xBBu, 0x0Fu, 0x50u } };
static const GUID SCARLETT_IID_IApplicationViewTitleBar =
    { 0x00924AC0u, 0x932Bu, 0x4A6Bu, { 0x9Cu, 0x4Bu, 0xDCu, 0x38u, 0xC8u, 0x24u, 0x78u, 0xCEu } };

typedef struct UwpIApplicationViewTitleBar UwpIApplicationViewTitleBar;
typedef struct UwpIApplicationViewTitleBarVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationViewTitleBar *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationViewTitleBar *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationViewTitleBar *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationViewTitleBar *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationViewTitleBar *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationViewTitleBar *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *put_ForegroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ForegroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_BackgroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_BackgroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonForegroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonForegroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonBackgroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonBackgroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonHoverForegroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonHoverForegroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonHoverBackgroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonHoverBackgroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonPressedForegroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonPressedForegroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonPressedBackgroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonPressedBackgroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_InactiveForegroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_InactiveForegroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_InactiveBackgroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_InactiveBackgroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonInactiveForegroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonInactiveForegroundColor)(UwpIApplicationViewTitleBar *This, void **value);
    HRESULT (STDMETHODCALLTYPE *put_ButtonInactiveBackgroundColor)(UwpIApplicationViewTitleBar *This, void *value);
    HRESULT (STDMETHODCALLTYPE *get_ButtonInactiveBackgroundColor)(UwpIApplicationViewTitleBar *This, void **value);
} UwpIApplicationViewTitleBarVtbl;
struct UwpIApplicationViewTitleBar {
    const UwpIApplicationViewTitleBarVtbl *lpVtbl;
};

typedef struct UwpIApplicationView UwpIApplicationView;
typedef struct UwpIApplicationViewVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationView *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationView *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationView *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationView *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationView *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationView *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Orientation)(UwpIApplicationView *This, UwpApplicationViewOrientation *value);
    HRESULT (STDMETHODCALLTYPE *get_AdjacentToLeftDisplayEdge)(UwpIApplicationView *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_AdjacentToRightDisplayEdge)(UwpIApplicationView *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsFullScreen)(UwpIApplicationView *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsOnLockScreen)(UwpIApplicationView *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsScreenCaptureEnabled)(UwpIApplicationView *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *put_IsScreenCaptureEnabled)(UwpIApplicationView *This, boolean value);
    HRESULT (STDMETHODCALLTYPE *put_Title)(UwpIApplicationView *This, HSTRING value);
    HRESULT (STDMETHODCALLTYPE *get_Title)(UwpIApplicationView *This, HSTRING *value);
    HRESULT (STDMETHODCALLTYPE *get_Id)(UwpIApplicationView *This, INT32 *value);
    HRESULT (STDMETHODCALLTYPE *add_Consolidated)(UwpIApplicationView *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_Consolidated)(UwpIApplicationView *This, EventRegistrationToken token);
} UwpIApplicationViewVtbl;
struct UwpIApplicationView {
    const UwpIApplicationViewVtbl *lpVtbl;
};

typedef struct UwpIApplicationView2 UwpIApplicationView2;
typedef struct UwpIApplicationView2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationView2 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationView2 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationView2 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationView2 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationView2 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationView2 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_SuppressSystemOverlays)(UwpIApplicationView2 *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *put_SuppressSystemOverlays)(UwpIApplicationView2 *This, boolean value);
    HRESULT (STDMETHODCALLTYPE *get_VisibleBounds)(UwpIApplicationView2 *This, UwpRect *value);
    HRESULT (STDMETHODCALLTYPE *add_VisibleBoundsChanged)(UwpIApplicationView2 *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_VisibleBoundsChanged)(UwpIApplicationView2 *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *SetDesiredBoundsMode)(UwpIApplicationView2 *This, UwpApplicationViewBoundsMode mode, boolean *success);
    HRESULT (STDMETHODCALLTYPE *get_DesiredBoundsMode)(UwpIApplicationView2 *This, UwpApplicationViewBoundsMode *value);
} UwpIApplicationView2Vtbl;
struct UwpIApplicationView2 {
    const UwpIApplicationView2Vtbl *lpVtbl;
};

typedef struct UwpIApplicationView3 UwpIApplicationView3;
typedef struct UwpIApplicationView3Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationView3 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationView3 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationView3 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationView3 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationView3 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationView3 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_TitleBar)(UwpIApplicationView3 *This, UwpIApplicationViewTitleBar **value);
    HRESULT (STDMETHODCALLTYPE *get_FullScreenSystemOverlayMode)(UwpIApplicationView3 *This, UwpFullScreenSystemOverlayMode *value);
    HRESULT (STDMETHODCALLTYPE *put_FullScreenSystemOverlayMode)(UwpIApplicationView3 *This, UwpFullScreenSystemOverlayMode value);
    HRESULT (STDMETHODCALLTYPE *get_IsFullScreenMode)(UwpIApplicationView3 *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *TryEnterFullScreenMode)(UwpIApplicationView3 *This, boolean *success);
    HRESULT (STDMETHODCALLTYPE *ExitFullScreenMode)(UwpIApplicationView3 *This);
    HRESULT (STDMETHODCALLTYPE *ShowStandardSystemOverlays)(UwpIApplicationView3 *This);
    HRESULT (STDMETHODCALLTYPE *TryResizeView)(UwpIApplicationView3 *This, UwpSize value, boolean *success);
    HRESULT (STDMETHODCALLTYPE *SetPreferredMinSize)(UwpIApplicationView3 *This, UwpSize size);
} UwpIApplicationView3Vtbl;
struct UwpIApplicationView3 {
    const UwpIApplicationView3Vtbl *lpVtbl;
};

typedef struct UwpIApplicationView4 UwpIApplicationView4;
typedef struct UwpIApplicationView4Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationView4 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationView4 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationView4 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationView4 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationView4 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationView4 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_ViewMode)(UwpIApplicationView4 *This, UwpApplicationViewMode *value);
    HRESULT (STDMETHODCALLTYPE *IsViewModeSupported)(UwpIApplicationView4 *This, UwpApplicationViewMode mode, boolean *is_mode_supported);
    HRESULT (STDMETHODCALLTYPE *TryEnterViewModeAsync)(UwpIApplicationView4 *This, UwpApplicationViewMode mode, void **operation);
    HRESULT (STDMETHODCALLTYPE *TryEnterViewModeWithPreferencesAsync)(UwpIApplicationView4 *This, UwpApplicationViewMode mode, void *preferences, void **operation);
    HRESULT (STDMETHODCALLTYPE *TryConsolidateAsync)(UwpIApplicationView4 *This, void **operation);
} UwpIApplicationView4Vtbl;
struct UwpIApplicationView4 {
    const UwpIApplicationView4Vtbl *lpVtbl;
};

typedef struct UwpIApplicationViewStatics UwpIApplicationViewStatics;
typedef struct UwpIApplicationViewStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationViewStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationViewStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationViewStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationViewStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationViewStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationViewStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Value)(UwpIApplicationViewStatics *This, UwpApplicationViewState *value);
    HRESULT (STDMETHODCALLTYPE *TryUnsnap)(UwpIApplicationViewStatics *This, boolean *success);
} UwpIApplicationViewStaticsVtbl;
struct UwpIApplicationViewStatics {
    const UwpIApplicationViewStaticsVtbl *lpVtbl;
};

typedef struct UwpIApplicationViewStatics2 UwpIApplicationViewStatics2;
typedef struct UwpIApplicationViewStatics2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationViewStatics2 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationViewStatics2 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationViewStatics2 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationViewStatics2 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationViewStatics2 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationViewStatics2 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(UwpIApplicationViewStatics2 *This, UwpIApplicationView **current);
    HRESULT (STDMETHODCALLTYPE *get_TerminateAppOnFinalViewClose)(UwpIApplicationViewStatics2 *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *put_TerminateAppOnFinalViewClose)(UwpIApplicationViewStatics2 *This, boolean value);
} UwpIApplicationViewStatics2Vtbl;
struct UwpIApplicationViewStatics2 {
    const UwpIApplicationViewStatics2Vtbl *lpVtbl;
};

typedef struct UwpIApplicationViewStatics3 UwpIApplicationViewStatics3;
typedef struct UwpIApplicationViewStatics3Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationViewStatics3 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationViewStatics3 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationViewStatics3 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationViewStatics3 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationViewStatics3 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationViewStatics3 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_PreferredLaunchWindowingMode)(UwpIApplicationViewStatics3 *This, UwpApplicationViewWindowingMode *value);
    HRESULT (STDMETHODCALLTYPE *put_PreferredLaunchWindowingMode)(UwpIApplicationViewStatics3 *This, UwpApplicationViewWindowingMode value);
    HRESULT (STDMETHODCALLTYPE *get_PreferredLaunchViewSize)(UwpIApplicationViewStatics3 *This, UwpSize *value);
    HRESULT (STDMETHODCALLTYPE *put_PreferredLaunchViewSize)(UwpIApplicationViewStatics3 *This, UwpSize value);
} UwpIApplicationViewStatics3Vtbl;
struct UwpIApplicationViewStatics3 {
    const UwpIApplicationViewStatics3Vtbl *lpVtbl;
};

typedef struct UwpIApplicationViewFullscreenStatics UwpIApplicationViewFullscreenStatics;
typedef struct UwpIApplicationViewFullscreenStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationViewFullscreenStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationViewFullscreenStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationViewFullscreenStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationViewFullscreenStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationViewFullscreenStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationViewFullscreenStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *TryUnsnapToFullscreen)(UwpIApplicationViewFullscreenStatics *This, boolean *success);
} UwpIApplicationViewFullscreenStaticsVtbl;
struct UwpIApplicationViewFullscreenStatics {
    const UwpIApplicationViewFullscreenStaticsVtbl *lpVtbl;
};

typedef struct UwpIApplicationViewInteropStatics UwpIApplicationViewInteropStatics;
typedef struct UwpIApplicationViewInteropStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApplicationViewInteropStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApplicationViewInteropStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApplicationViewInteropStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApplicationViewInteropStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApplicationViewInteropStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApplicationViewInteropStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetApplicationViewIdForWindow)(UwpIApplicationViewInteropStatics *This, void *window, INT32 *id);
} UwpIApplicationViewInteropStaticsVtbl;
struct UwpIApplicationViewInteropStatics {
    const UwpIApplicationViewInteropStaticsVtbl *lpVtbl;
};

/* ============================================================================
 * 3. Windows.UI.Core.SystemNavigationManager
 * ============================================================================ */

typedef enum UwpAppViewBackButtonVisibility {
    UwpAppViewBackButtonVisibility_Visible   = 0,
    UwpAppViewBackButtonVisibility_Collapsed = 1,
    UwpAppViewBackButtonVisibility_Disabled  = 2
} UwpAppViewBackButtonVisibility;

/* ISystemNavigationManagerStatics: {DC52B5CE-BEE0-4305-8C54-68228ED683B5} */
static const GUID SCARLETT_IID_ISystemNavigationManagerStatics =
    { 0xDC52B5CEu, 0xBEE0u, 0x4305u, { 0x8Cu, 0x54u, 0x68u, 0x22u, 0x8Eu, 0xD6u, 0x83u, 0xB5u } };

/* ISystemNavigationManager: {93023118-CF50-42A6-9706-69107FA122E1} */
static const GUID SCARLETT_IID_ISystemNavigationManager =
    { 0x93023118u, 0xCF50u, 0x42A6u, { 0x97u, 0x06u, 0x69u, 0x10u, 0x7Fu, 0xA1u, 0x22u, 0xE1u } };

/* ISystemNavigationManager2: {8C510401-67BE-49AE-9509-671C1E54A389} */
static const GUID SCARLETT_IID_ISystemNavigationManager2 =
    { 0x8C510401u, 0x67BEu, 0x49AEu, { 0x95u, 0x09u, 0x67u, 0x1Cu, 0x1Eu, 0x54u, 0xA3u, 0x89u } };

/* IBackRequestedEventArgs: {D603D28A-E411-4A4E-BA41-6A327A8675BC} */
static const GUID SCARLETT_IID_IBackRequestedEventArgs =
    { 0xD603D28Au, 0xE411u, 0x4A4Eu, { 0xBAu, 0x41u, 0x6Au, 0x32u, 0x7Au, 0x86u, 0x75u, 0xBCu } };

typedef struct UwpIBackRequestedEventArgs UwpIBackRequestedEventArgs;
typedef struct UwpIBackRequestedEventArgsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIBackRequestedEventArgs *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIBackRequestedEventArgs *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIBackRequestedEventArgs *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIBackRequestedEventArgs *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIBackRequestedEventArgs *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIBackRequestedEventArgs *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Handled)(UwpIBackRequestedEventArgs *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *put_Handled)(UwpIBackRequestedEventArgs *This, boolean value);
} UwpIBackRequestedEventArgsVtbl;
struct UwpIBackRequestedEventArgs {
    const UwpIBackRequestedEventArgsVtbl *lpVtbl;
};

typedef struct UwpISystemNavigationManager UwpISystemNavigationManager;
typedef struct UwpISystemNavigationManagerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpISystemNavigationManager *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpISystemNavigationManager *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpISystemNavigationManager *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpISystemNavigationManager *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpISystemNavigationManager *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpISystemNavigationManager *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_BackRequested)(UwpISystemNavigationManager *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_BackRequested)(UwpISystemNavigationManager *This, EventRegistrationToken token);
} UwpISystemNavigationManagerVtbl;
struct UwpISystemNavigationManager {
    const UwpISystemNavigationManagerVtbl *lpVtbl;
};

typedef struct UwpISystemNavigationManager2 UwpISystemNavigationManager2;
typedef struct UwpISystemNavigationManager2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpISystemNavigationManager2 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpISystemNavigationManager2 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpISystemNavigationManager2 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpISystemNavigationManager2 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpISystemNavigationManager2 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpISystemNavigationManager2 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_AppViewBackButtonVisibility)(UwpISystemNavigationManager2 *This, UwpAppViewBackButtonVisibility *value);
    HRESULT (STDMETHODCALLTYPE *put_AppViewBackButtonVisibility)(UwpISystemNavigationManager2 *This, UwpAppViewBackButtonVisibility value);
} UwpISystemNavigationManager2Vtbl;
struct UwpISystemNavigationManager2 {
    const UwpISystemNavigationManager2Vtbl *lpVtbl;
};

typedef struct UwpISystemNavigationManagerStatics UwpISystemNavigationManagerStatics;
typedef struct UwpISystemNavigationManagerStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpISystemNavigationManagerStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpISystemNavigationManagerStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpISystemNavigationManagerStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpISystemNavigationManagerStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpISystemNavigationManagerStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpISystemNavigationManagerStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(UwpISystemNavigationManagerStatics *This, UwpISystemNavigationManager **current);
} UwpISystemNavigationManagerStaticsVtbl;
struct UwpISystemNavigationManagerStatics {
    const UwpISystemNavigationManagerStaticsVtbl *lpVtbl;
};

/* ============================================================================
 * 4. Windows.UI.Core.CoreCursor
 * ============================================================================ */

typedef enum UwpCoreCursorType {
    UwpCoreCursorType_Arrow                  = 0,
    UwpCoreCursorType_Cross                  = 1,
    UwpCoreCursorType_Custom                 = 2,
    UwpCoreCursorType_Hand                   = 3,
    UwpCoreCursorType_Help                   = 4,
    UwpCoreCursorType_IBeam                  = 5,
    UwpCoreCursorType_SizeAll                = 6,
    UwpCoreCursorType_SizeNortheastSouthwest = 7,
    UwpCoreCursorType_SizeNorthSouth         = 8,
    UwpCoreCursorType_SizeNorthwestSoutheast = 9,
    UwpCoreCursorType_SizeWestEast           = 10,
    UwpCoreCursorType_UniversalNo            = 11,
    UwpCoreCursorType_UpArrow                = 12,
    UwpCoreCursorType_Wait                   = 13,
    UwpCoreCursorType_Pin                    = 14,
    UwpCoreCursorType_Person                 = 15
} UwpCoreCursorType;

/* Wine IDL IIDs for CoreCursor */
static const GUID SCARLETT_IID_ICoreCursor =
    { 0x96893ACFu, 0x111Du, 0x442Cu, { 0x8Au, 0x77u, 0xB8u, 0x79u, 0x92u, 0xF8u, 0xE2u, 0xD6u } };
static const GUID SCARLETT_IID_ICoreCursorFactory =
    { 0xF6359621u, 0xA79Du, 0x4ED3u, { 0x8Cu, 0x32u, 0xA9u, 0xEFu, 0x9Du, 0x6Bu, 0x76u, 0xA4u } };

typedef struct UwpICoreCursor UwpICoreCursor;
typedef struct UwpICoreCursorVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpICoreCursor *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpICoreCursor *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpICoreCursor *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpICoreCursor *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpICoreCursor *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpICoreCursor *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Id)(UwpICoreCursor *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_Type)(UwpICoreCursor *This, UwpCoreCursorType *value);
} UwpICoreCursorVtbl;
struct UwpICoreCursor {
    const UwpICoreCursorVtbl *lpVtbl;
};

typedef struct UwpICoreCursorFactory UwpICoreCursorFactory;
typedef struct UwpICoreCursorFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpICoreCursorFactory *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpICoreCursorFactory *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpICoreCursorFactory *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpICoreCursorFactory *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpICoreCursorFactory *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpICoreCursorFactory *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *CreateCursor)(UwpICoreCursorFactory *This, UwpCoreCursorType type, UINT32 id, UwpICoreCursor **cursor);
} UwpICoreCursorFactoryVtbl;
struct UwpICoreCursorFactory {
    const UwpICoreCursorFactoryVtbl *lpVtbl;
};

/* ============================================================================
 * 5. Windows.Devices.Input.MouseDevice
 * ============================================================================ */

/* IMouseDeviceStatics: {484A9045-6D70-49DB-8E68-46FF8DD317BD} */
static const GUID SCARLETT_IID_IMouseDeviceStatics =
    { 0x484A9045u, 0x6D70u, 0x49DBu, { 0x8Eu, 0x68u, 0x46u, 0xFFu, 0x8Du, 0xD3u, 0x17u, 0xBDu } };

/* IMouseDevice: {88EDF458-F2C8-49F4-BE1F-C256B388BC11} */
static const GUID SCARLETT_IID_IMouseDevice =
    { 0x88EDF458u, 0xF2C8u, 0x49F4u, { 0xBEu, 0x1Fu, 0xC2u, 0x56u, 0xB3u, 0x88u, 0xBCu, 0x11u } };

/* IMouseEventArgs: {F625AA5D-2354-4CC7-9230-96941C969FDE} */
static const GUID SCARLETT_IID_IMouseEventArgs =
    { 0xF625AA5Du, 0x2354u, 0x4CC7u, { 0x92u, 0x30u, 0x96u, 0x94u, 0x1Cu, 0x96u, 0x9Fu, 0xDEu } };

typedef struct UwpIMouseEventArgs UwpIMouseEventArgs;
typedef struct UwpIMouseEventArgsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIMouseEventArgs *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIMouseEventArgs *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIMouseEventArgs *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIMouseEventArgs *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIMouseEventArgs *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIMouseEventArgs *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_MouseDelta)(UwpIMouseEventArgs *This, UwpMouseDelta *value);
} UwpIMouseEventArgsVtbl;
struct UwpIMouseEventArgs {
    const UwpIMouseEventArgsVtbl *lpVtbl;
};

typedef struct UwpIMouseDevice UwpIMouseDevice;
typedef struct UwpIMouseDeviceVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIMouseDevice *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIMouseDevice *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIMouseDevice *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIMouseDevice *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIMouseDevice *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIMouseDevice *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_MouseMoved)(UwpIMouseDevice *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_MouseMoved)(UwpIMouseDevice *This, EventRegistrationToken token);
} UwpIMouseDeviceVtbl;
struct UwpIMouseDevice {
    const UwpIMouseDeviceVtbl *lpVtbl;
};

typedef struct UwpIMouseDeviceStatics UwpIMouseDeviceStatics;
typedef struct UwpIMouseDeviceStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIMouseDeviceStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIMouseDeviceStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIMouseDeviceStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIMouseDeviceStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIMouseDeviceStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIMouseDeviceStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(UwpIMouseDeviceStatics *This, UwpIMouseDevice **mouseDevice);
} UwpIMouseDeviceStaticsVtbl;
struct UwpIMouseDeviceStatics {
    const UwpIMouseDeviceStaticsVtbl *lpVtbl;
};

/* ============================================================================
 * 6. Windows.System.Display.DisplayRequest
 * ============================================================================ */

/* IDisplayRequest: {E5732044-F49F-4B60-8DD4-5E7E3A632AC0} */
static const GUID SCARLETT_IID_IDisplayRequest =
    { 0xE5732044u, 0xF49Fu, 0x4B60u, { 0x8Du, 0xD4u, 0x5Eu, 0x7Eu, 0x3Au, 0x63u, 0x2Au, 0xC0u } };

typedef struct UwpIDisplayRequest UwpIDisplayRequest;
typedef struct UwpIDisplayRequestVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIDisplayRequest *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIDisplayRequest *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIDisplayRequest *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIDisplayRequest *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIDisplayRequest *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIDisplayRequest *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *RequestActive)(UwpIDisplayRequest *This);
    HRESULT (STDMETHODCALLTYPE *RequestRelease)(UwpIDisplayRequest *This);
} UwpIDisplayRequestVtbl;
struct UwpIDisplayRequest {
    const UwpIDisplayRequestVtbl *lpVtbl;
};

/* ============================================================================
 * 7. Windows.Foundation.Metadata.ApiInformation
 * ============================================================================ */

/* Wine IDL IID for IApiInformationStatics: {997439FE-F681-4A11-B416-C13A47E8BA36} */
static const GUID SCARLETT_IID_IApiInformationStatics =
    { 0x997439FEu, 0xF681u, 0x4A11u, { 0xB4u, 0x16u, 0xC1u, 0x3Au, 0x47u, 0xE8u, 0xBAu, 0x36u } };

typedef struct UwpIApiInformationStatics UwpIApiInformationStatics;
typedef struct UwpIApiInformationStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIApiInformationStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIApiInformationStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIApiInformationStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIApiInformationStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIApiInformationStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIApiInformationStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *IsTypePresent)(UwpIApiInformationStatics *This, HSTRING typeName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsMethodPresent)(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING methodName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsMethodPresentWithArity)(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING methodName, UINT32 inputParameterCount, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsEventPresent)(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING eventName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsPropertyPresent)(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING propertyName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsReadOnlyPropertyPresent)(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING propertyName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsWriteablePropertyPresent)(UwpIApiInformationStatics *This, HSTRING typeName, HSTRING propertyName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsEnumNamedValuePresent)(UwpIApiInformationStatics *This, HSTRING enumTypeName, HSTRING valueName, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsApiContractPresentByMajor)(UwpIApiInformationStatics *This, HSTRING contractName, UINT16 majorVersion, boolean *value);
    HRESULT (STDMETHODCALLTYPE *IsApiContractPresentByMajorAndMinor)(UwpIApiInformationStatics *This, HSTRING contractName, UINT16 majorVersion, UINT16 minorVersion, boolean *value);
} UwpIApiInformationStaticsVtbl;
struct UwpIApiInformationStatics {
    const UwpIApiInformationStaticsVtbl *lpVtbl;
};

/* ============================================================================
 * 8. Windows.Graphics.Display.Core.HdmiDisplayInformation
 * ============================================================================ */

typedef enum UwpHdmiDisplayColorSpace {
    UwpHdmiDisplayColorSpace_RgbLimited = 0,
    UwpHdmiDisplayColorSpace_RgbFull    = 1,
    UwpHdmiDisplayColorSpace_BT2020     = 2,
    UwpHdmiDisplayColorSpace_BT709      = 3
} UwpHdmiDisplayColorSpace;

typedef enum UwpHdmiDisplayPixelEncoding {
    UwpHdmiDisplayPixelEncoding_Rgb444   = 0,
    UwpHdmiDisplayPixelEncoding_Ycc444   = 1,
    UwpHdmiDisplayPixelEncoding_Ycc422   = 2,
    UwpHdmiDisplayPixelEncoding_Ycc420   = 3
} UwpHdmiDisplayPixelEncoding;

typedef enum UwpHdmiDisplayHdrOption {
    UwpHdmiDisplayHdrOption_None                  = 0,
    UwpHdmiDisplayHdrOption_EotfSdr               = 1,
    UwpHdmiDisplayHdrOption_Eotf2084              = 2,
    UwpHdmiDisplayHdrOption_DolbyVisionLowLatency = 3
} UwpHdmiDisplayHdrOption;

/* IHdmiDisplayInformationStatics: {6CE6B260-F42A-4A15-914C-7B8E2A5A65DF} */
static const GUID SCARLETT_IID_IHdmiDisplayInformationStatics =
    { 0x6CE6B260u, 0xF42Au, 0x4A15u, { 0x91u, 0x4Cu, 0x7Bu, 0x8Eu, 0x2Au, 0x5Au, 0x65u, 0xDFu } };

/* IHdmiDisplayInformation: {130B3C0A-F565-476E-ABD5-EA05AEE74C69} */
static const GUID SCARLETT_IID_IHdmiDisplayInformation =
    { 0x130B3C0Au, 0xF565u, 0x476Eu, { 0xABu, 0xD5u, 0xEAu, 0x05u, 0xAEu, 0xE7u, 0x4Cu, 0x69u } };

/* IHdmiDisplayMode: {0C06D5AD-1B90-4F51-9981-EF5A1C0DDF66} */
static const GUID SCARLETT_IID_IHdmiDisplayMode =
    { 0x0C06D5ADu, 0x1B90u, 0x4F51u, { 0x99u, 0x81u, 0xEFu, 0x5Au, 0x1Cu, 0x0Du, 0xDFu, 0x66u } };

/* IHdmiDisplayMode2: {07CD4E9F-4B3C-42B8-84E7-895368718AF2} */
static const GUID SCARLETT_IID_IHdmiDisplayMode2 =
    { 0x07CD4E9Fu, 0x4B3Cu, 0x42B8u, { 0x84u, 0xE7u, 0x89u, 0x53u, 0x68u, 0x71u, 0x8Au, 0xF2u } };

typedef struct UwpIHdmiDisplayMode UwpIHdmiDisplayMode;
typedef struct UwpIHdmiDisplayModeVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIHdmiDisplayMode *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIHdmiDisplayMode *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIHdmiDisplayMode *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIHdmiDisplayMode *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIHdmiDisplayMode *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIHdmiDisplayMode *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_ResolutionWidthInRawPixels)(UwpIHdmiDisplayMode *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_ResolutionHeightInRawPixels)(UwpIHdmiDisplayMode *This, UINT32 *value);
    HRESULT (STDMETHODCALLTYPE *get_RefreshRate)(UwpIHdmiDisplayMode *This, DOUBLE *value);
    HRESULT (STDMETHODCALLTYPE *get_StereoEnabled)(UwpIHdmiDisplayMode *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_BitsPerPixel)(UwpIHdmiDisplayMode *This, UINT16 *value);
    HRESULT (STDMETHODCALLTYPE *IsEqual)(UwpIHdmiDisplayMode *This, UwpIHdmiDisplayMode *mode, boolean *result);
    HRESULT (STDMETHODCALLTYPE *get_ColorSpace)(UwpIHdmiDisplayMode *This, UwpHdmiDisplayColorSpace *value);
    HRESULT (STDMETHODCALLTYPE *get_PixelEncoding)(UwpIHdmiDisplayMode *This, UwpHdmiDisplayPixelEncoding *value);
    HRESULT (STDMETHODCALLTYPE *get_IsSdrLuminanceSupported)(UwpIHdmiDisplayMode *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_IsSmpte2084Supported)(UwpIHdmiDisplayMode *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_Is2086MetadataSupported)(UwpIHdmiDisplayMode *This, boolean *value);
} UwpIHdmiDisplayModeVtbl;
struct UwpIHdmiDisplayMode {
    const UwpIHdmiDisplayModeVtbl *lpVtbl;
};

typedef struct UwpIHdmiDisplayMode2 UwpIHdmiDisplayMode2;
typedef struct UwpIHdmiDisplayMode2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIHdmiDisplayMode2 *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIHdmiDisplayMode2 *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIHdmiDisplayMode2 *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIHdmiDisplayMode2 *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIHdmiDisplayMode2 *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIHdmiDisplayMode2 *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_IsDolbyVisionLowLatencySupported)(UwpIHdmiDisplayMode2 *This, boolean *value);
} UwpIHdmiDisplayMode2Vtbl;
struct UwpIHdmiDisplayMode2 {
    const UwpIHdmiDisplayMode2Vtbl *lpVtbl;
};

typedef struct UwpIVectorViewHdmiDisplayMode UwpIVectorViewHdmiDisplayMode;
typedef struct UwpIVectorViewHdmiDisplayModeVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIVectorViewHdmiDisplayMode *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIVectorViewHdmiDisplayMode *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIVectorViewHdmiDisplayMode *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIVectorViewHdmiDisplayMode *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIVectorViewHdmiDisplayMode *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIVectorViewHdmiDisplayMode *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(UwpIVectorViewHdmiDisplayMode *This, UINT32 index, UwpIHdmiDisplayMode **item);
    HRESULT (STDMETHODCALLTYPE *get_Size)(UwpIVectorViewHdmiDisplayMode *This, UINT32 *size);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(UwpIVectorViewHdmiDisplayMode *This, UwpIHdmiDisplayMode *value, UINT32 *index, boolean *found);
    HRESULT (STDMETHODCALLTYPE *GetMany)(UwpIVectorViewHdmiDisplayMode *This, UINT32 startIndex, UINT32 capacity, UwpIHdmiDisplayMode **items, UINT32 *actual);
} UwpIVectorViewHdmiDisplayModeVtbl;
struct UwpIVectorViewHdmiDisplayMode {
    const UwpIVectorViewHdmiDisplayModeVtbl *lpVtbl;
};

typedef struct UwpIHdmiDisplayInformation UwpIHdmiDisplayInformation;
typedef struct UwpIHdmiDisplayInformationVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIHdmiDisplayInformation *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIHdmiDisplayInformation *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIHdmiDisplayInformation *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIHdmiDisplayInformation *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIHdmiDisplayInformation *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIHdmiDisplayInformation *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetSupportedDisplayModes)(UwpIHdmiDisplayInformation *This, UwpIVectorViewHdmiDisplayMode **result);
    HRESULT (STDMETHODCALLTYPE *GetCurrentDisplayMode)(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode **result);
    HRESULT (STDMETHODCALLTYPE *SetDefaultDisplayModeAsync)(UwpIHdmiDisplayInformation *This, void **operation);
    HRESULT (STDMETHODCALLTYPE *RequestSetCurrentDisplayModeAsync)(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode *mode, void **operation);
    HRESULT (STDMETHODCALLTYPE *RequestSetCurrentDisplayModeWithHdrAsync)(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode *mode, UwpHdmiDisplayHdrOption hdrOption, void **operation);
    HRESULT (STDMETHODCALLTYPE *RequestSetCurrentDisplayModeWithHdrAndMetadataAsync)(UwpIHdmiDisplayInformation *This, UwpIHdmiDisplayMode *mode, UwpHdmiDisplayHdrOption hdrOption, UwpHdmiDisplayHdr2086Metadata hdrMetadata, void **operation);
    HRESULT (STDMETHODCALLTYPE *add_DisplayModesChanged)(UwpIHdmiDisplayInformation *This, UwpEventHandler *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_DisplayModesChanged)(UwpIHdmiDisplayInformation *This, EventRegistrationToken token);
} UwpIHdmiDisplayInformationVtbl;
struct UwpIHdmiDisplayInformation {
    const UwpIHdmiDisplayInformationVtbl *lpVtbl;
};

typedef struct UwpIHdmiDisplayInformationStatics UwpIHdmiDisplayInformationStatics;
typedef struct UwpIHdmiDisplayInformationStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(UwpIHdmiDisplayInformationStatics *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(UwpIHdmiDisplayInformationStatics *This);
    ULONG   (STDMETHODCALLTYPE *Release)(UwpIHdmiDisplayInformationStatics *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(UwpIHdmiDisplayInformationStatics *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(UwpIHdmiDisplayInformationStatics *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(UwpIHdmiDisplayInformationStatics *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(UwpIHdmiDisplayInformationStatics *This, UwpIHdmiDisplayInformation **result);
} UwpIHdmiDisplayInformationStaticsVtbl;
struct UwpIHdmiDisplayInformationStatics {
    const UwpIHdmiDisplayInformationStaticsVtbl *lpVtbl;
};

/* ============================================================================
 * Internal WinRT UI Runtime Notification Helpers
 * ============================================================================ */

HRESULT uwp_winrt_ui_trigger_back_requested(int32_t *out_handled);
HRESULT uwp_winrt_ui_notify_mouse_moved(int32_t dx, int32_t dy);
HRESULT uwp_winrt_ui_notify_dpi_changed(void);
HRESULT uwp_winrt_ui_notify_orientation_changed(void);
HRESULT uwp_winrt_ui_notify_visible_bounds_changed(void);
const wchar_t *uwp_winrt_ui_cursor_type_to_win32_resource(UwpCoreCursorType type);

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_UWP_WINRT_UI_ABI_H */
