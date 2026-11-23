/*
 * Proton-Scarlett — UWPHOST.dll Internal WinRT ABI & Core Runtime Header
 *
 * Clean-room C11 WinRT COM vtable definitions and internal helpers for:
 *   - Windows.ApplicationModel.Core.CoreApplication
 *   - Windows.ApplicationModel.Core.CoreApplicationView
 *   - Windows.UI.Core.CoreWindow
 *   - Windows.UI.Core.CoreDispatcher
 *   - AppxManifest.xml parser & package identity
 *   - Structured diagnostics & strict-mode enforcement
 *
 * Compiles cleanly under both:
 *   - x86_64-w64-mingw32-gcc (PE64 / _WIN32, Win32 APIs ONLY, zero POSIX)
 *   - Host Linux gcc/clang (for host unit tests in tests/)
 */

#ifndef SCARLETT_UWPHOST_INTERNAL_H
#define SCARLETT_UWPHOST_INTERNAL_H

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#include "scarlett_errors.h"
#include "uwp_bridge_abi.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
#define UWP_CALL STDMETHODCALLTYPE
#define UWP_API  WINAPI
#else
/* Host unit-test Win32/COM type compatibility layer */
#define UWP_CALL
#define UWP_API

typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef uint64_t UINT64;
typedef int64_t  INT64;
typedef uint16_t WCHAR;
typedef const wchar_t *LPCWSTR;
typedef wchar_t *LPWSTR;
typedef const char *LPCSTR;
typedef uintptr_t WPARAM;
typedef intptr_t  LPARAM;
typedef intptr_t  LRESULT;
typedef void *HANDLE;
typedef void *HMODULE;
typedef void *HINSTANCE;
typedef void *LPVOID;
typedef GUID IID;

typedef struct IUnknown IUnknown;
typedef struct IUnknownVtbl {
    HRESULT (UWP_CALL *QueryInterface)(IUnknown *This, REFIID riid, void **ppvObject);
    ULONG   (UWP_CALL *AddRef)(IUnknown *This);
    ULONG   (UWP_CALL *Release)(IUnknown *This);
} IUnknownVtbl;
struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};

typedef struct ICoreWindowInteropBridgeVtbl {
    HRESULT (UWP_CALL *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (UWP_CALL *AddRef)(void *This);
    ULONG   (UWP_CALL *Release)(void *This);
    HRESULT (UWP_CALL *get_WindowHandle)(void *This, HWND *hwnd);
    HRESULT (UWP_CALL *put_MessageHandled)(void *This, uint8_t value);
} ICoreWindowInteropBridgeVtbl;

typedef struct IProtonScarlettCoreWindowBridgeVtbl {
    HRESULT (UWP_CALL *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (UWP_CALL *AddRef)(void *This);
    ULONG   (UWP_CALL *Release)(void *This);
    HRESULT (UWP_CALL *GetHwnd)(void *This, HWND *out_hwnd);
    HRESULT (UWP_CALL *GetClientSize)(void *This, uint32_t *out_width, uint32_t *out_height);
    HRESULT (UWP_CALL *IsFullscreen)(void *This, int32_t *out_is_fullscreen);
    HRESULT (UWP_CALL *NotifySwapChainCreated)(void *This, IUnknown *swapchain);
} IProtonScarlettCoreWindowBridgeVtbl;

typedef struct IProtonScarlettCoreWindowBridge {
    const IProtonScarlettCoreWindowBridgeVtbl *lpVtbl;
} IProtonScarlettCoreWindowBridge;

#ifndef S_OK
#define S_OK                     ((HRESULT)0x00000000L)
#define S_FALSE                  ((HRESULT)0x00000001L)
#define E_NOTIMPL                ((HRESULT)0x80004001L)
#define E_NOINTERFACE            ((HRESULT)0x80004002L)
#define E_POINTER                ((HRESULT)0x80004003L)
#define E_ABORT                  ((HRESULT)0x80004004L)
#define E_FAIL                   ((HRESULT)0x80004005L)
#define E_UNEXPECTED             ((HRESULT)0x8000FFFFL)
#define E_ACCESSDENIED           ((HRESULT)0x80070005L)
#define E_OUTOFMEMORY            ((HRESULT)0x8007000EL)
#define E_INVALIDARG             ((HRESULT)0x80070057L)
#define E_BOUNDS                 ((HRESULT)0x8000000BL)
#define E_CHANGED_STATE          ((HRESULT)0x8000000CL)
#define E_ILLEGAL_METHOD_CALL    ((HRESULT)0x8000000EL)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#define REGDB_E_CLASSNOTREG      ((HRESULT)0x80040154L)
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr)    (((HRESULT)(hr)) < 0)
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef LOWORD
#define LOWORD(l) ((uint16_t)(((uintptr_t)(l)) & 0xFFFFu))
#define HIWORD(l) ((uint16_t)((((uintptr_t)(l)) >> 16) & 0xFFFFu))
#endif

/* Win32 message constants needed for host dispatcher tests */
#ifndef WM_SIZE
#define WM_SIZE           0x0005u
#define WM_ACTIVATE       0x0006u
#define WM_CLOSE          0x0010u
#define WM_QUIT           0x0012u
#define WM_SHOWWINDOW     0x0018u
#define WM_KEYDOWN        0x0100u
#define WM_KEYUP          0x0101u
#define WM_CHAR           0x0102u
#define WM_SYSKEYDOWN     0x0104u
#define WM_SYSKEYUP       0x0105u
#define WM_SYSCHAR        0x0106u
#define WM_MOUSEMOVE      0x0200u
#define WM_LBUTTONDOWN    0x0201u
#define WM_LBUTTONUP      0x0202u
#define WM_RBUTTONDOWN    0x0204u
#define WM_RBUTTONUP      0x0205u
#define WM_MBUTTONDOWN    0x0207u
#define WM_MBUTTONUP      0x0208u
#define WM_MOUSEWHEEL     0x020Au
#define WM_XBUTTONDOWN    0x020Bu
#define WM_XBUTTONUP      0x020Cu
#define WM_MOUSEHWHEEL    0x020Eu
#define WM_ENTERSIZEMOVE  0x0231u
#define WM_EXITSIZEMOVE   0x0232u
#define WM_APP            0x8000u
#define WA_INACTIVE       0u
#define WA_ACTIVE         1u
#define WA_CLICKACTIVE    2u
#define SIZE_MINIMIZED    1u
#define MK_LBUTTON        0x0001u
#define MK_RBUTTON        0x0002u
#define MK_SHIFT          0x0004u
#define MK_CONTROL        0x0008u
#define MK_MBUTTON        0x0010u
#define MK_XBUTTON1       0x0020u
#define MK_XBUTTON2       0x0040u
#define XBUTTON1          0x0001u
#define XBUTTON2          0x0002u
#endif
#endif /* _WIN32 */

#ifndef E_BOUNDS
#define E_BOUNDS ((HRESULT)0x8000000BL)
#endif
#ifndef E_CHANGED_STATE
#define E_CHANGED_STATE ((HRESULT)0x8000000CL)
#endif
#ifndef E_ILLEGAL_METHOD_CALL
#define E_ILLEGAL_METHOD_CALL ((HRESULT)0x8000000EL)
#endif
#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#endif
#ifndef REGDB_E_CLASSNOTREG
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154L)
#endif
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020Eu
#endif

#define WM_UWP_DISPATCH_WORK (WM_APP + 0x101u)

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Portable Atomic & Mutex Primitives (Win32 CRITICAL_SECTION / Host pthread)
 * ============================================================================ */

#ifdef _WIN32
typedef CRITICAL_SECTION uwp_mutex_t;
static inline void uwp_mutex_init(uwp_mutex_t *m) { InitializeCriticalSection(m); }
static inline void uwp_mutex_destroy(uwp_mutex_t *m) { DeleteCriticalSection(m); }
static inline void uwp_mutex_lock(uwp_mutex_t *m) { EnterCriticalSection(m); }
static inline void uwp_mutex_unlock(uwp_mutex_t *m) { LeaveCriticalSection(m); }
static inline int32_t uwp_atomic_inc(volatile int32_t *v) {
    return (int32_t)InterlockedIncrement((volatile LONG *)v);
}
static inline int32_t uwp_atomic_dec(volatile int32_t *v) {
    return (int32_t)InterlockedDecrement((volatile LONG *)v);
}
static inline uint32_t uwp_current_thread_id(void) {
    return (uint32_t)GetCurrentThreadId();
}
#else
#include <pthread.h>
typedef pthread_mutex_t uwp_mutex_t;
static inline void uwp_mutex_init(uwp_mutex_t *m) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
}
static inline void uwp_mutex_destroy(uwp_mutex_t *m) { pthread_mutex_destroy(m); }
static inline void uwp_mutex_lock(uwp_mutex_t *m) { pthread_mutex_lock(m); }
static inline void uwp_mutex_unlock(uwp_mutex_t *m) { pthread_mutex_unlock(m); }
static inline int32_t uwp_atomic_inc(volatile int32_t *v) {
    return __atomic_add_fetch(v, 1, __ATOMIC_ACQ_REL);
}
static inline int32_t uwp_atomic_dec(volatile int32_t *v) {
    return __atomic_sub_fetch(v, 1, __ATOMIC_ACQ_REL);
}
static inline uint32_t uwp_current_thread_id(void) {
    return (uint32_t)(uintptr_t)pthread_self();
}
#endif

static inline int uwp_guid_equal(REFIID a, REFIID b) {
    if (!a || !b) return 0;
    return a->Data1 == b->Data1 &&
           a->Data2 == b->Data2 &&
           a->Data3 == b->Data3 &&
           a->Data4[0] == b->Data4[0] &&
           a->Data4[1] == b->Data4[1] &&
           a->Data4[2] == b->Data4[2] &&
           a->Data4[3] == b->Data4[3] &&
           a->Data4[4] == b->Data4[4] &&
           a->Data4[5] == b->Data4[5] &&
           a->Data4[6] == b->Data4[6] &&
           a->Data4[7] == b->Data4[7];
}

#define UWP_CONTAINING_RECORD(ptr, type, field) \
    ((type *)((uint8_t *)(ptr) - offsetof(type, field)))

/* ============================================================================
 * WinRT Basic Types (HSTRING, EventRegistrationToken, Geometry, Enums)
 * ============================================================================ */

typedef struct UwpHString__ *UWP_HSTRING;

typedef struct UwpHStringHeader {
    union {
        void *reserved1;
        char  reserved2[24];
    } Reserved;
} UWP_HSTRING_HEADER;

typedef enum UwpRoInitType {
    UWP_RO_INIT_SINGLETHREADED = 0,
    UWP_RO_INIT_MULTITHREADED  = 1
} UwpRoInitType;

typedef enum UwpTrustLevel {
    UWP_TRUST_BASE    = 0,
    UWP_TRUST_PARTIAL = 1,
    UWP_TRUST_FULL    = 2
} UwpTrustLevel;

typedef struct UwpEventRegistrationToken {
    int64_t value;
} UwpEventRegistrationToken;

typedef struct UwpPoint {
    float X;
    float Y;
} UwpPoint;

typedef struct UwpSize {
    float Width;
    float Height;
} UwpSize;

typedef struct UwpRect {
    float X;
    float Y;
    float Width;
    float Height;
} UwpRect;

typedef struct UwpDateTime {
    int64_t UniversalTime;
} UwpDateTime;

typedef enum UwpAsyncStatus {
    UWP_ASYNC_STARTED   = 0,
    UWP_ASYNC_COMPLETED = 1,
    UWP_ASYNC_CANCELED  = 2,
    UWP_ASYNC_ERROR     = 3
} UwpAsyncStatus;

typedef enum UwpCoreDispatcherPriority {
    UWP_PRIORITY_IDLE   = -2,
    UWP_PRIORITY_LOW    = -1,
    UWP_PRIORITY_NORMAL =  0,
    UWP_PRIORITY_HIGH   =  1
} UwpCoreDispatcherPriority;

typedef enum UwpCoreProcessEventsOption {
    UWP_PROCESS_ONE_AND_ALL_PENDING = 0,
    UWP_PROCESS_ONE_IF_PRESENT      = 1,
    UWP_PROCESS_UNTIL_QUIT          = 2,
    UWP_PROCESS_ALL_IF_PRESENT      = 3
} UwpCoreProcessEventsOption;

typedef enum UwpCoreVirtualKeyStates {
    UWP_KEYSTATE_NONE   = 0x0,
    UWP_KEYSTATE_DOWN   = 0x1,
    UWP_KEYSTATE_LOCKED = 0x2
} UwpCoreVirtualKeyStates;

typedef enum UwpCoreWindowActivationState {
    UWP_ACTIVATION_CODE_ACTIVATED    = 0,
    UWP_ACTIVATION_DEACTIVATED       = 1,
    UWP_ACTIVATION_POINTER_ACTIVATED = 2
} UwpCoreWindowActivationState;

typedef enum UwpCoreWindowActivationMode {
    UWP_ACTIVATION_MODE_NONE                     = 0,
    UWP_ACTIVATION_MODE_DEACTIVATED              = 1,
    UWP_ACTIVATION_MODE_ACTIVATED_NOT_FOREGROUND = 2,
    UWP_ACTIVATION_MODE_ACTIVATED_IN_FOREGROUND  = 3
} UwpCoreWindowActivationMode;

typedef enum UwpCoreWindowFlowDirection {
    UWP_FLOW_LEFT_TO_RIGHT = 0,
    UWP_FLOW_RIGHT_TO_LEFT = 1
} UwpCoreWindowFlowDirection;

typedef enum UwpCoreAcceleratorKeyEventType {
    UWP_ACCEL_KEY_DOWN              = 0,
    UWP_ACCEL_KEY_UP                = 1,
    UWP_ACCEL_CHARACTER             = 2,
    UWP_ACCEL_DEAD_CHARACTER        = 3,
    UWP_ACCEL_SYSTEM_KEY_DOWN       = 4,
    UWP_ACCEL_SYSTEM_KEY_UP         = 5,
    UWP_ACCEL_SYSTEM_CHARACTER      = 6,
    UWP_ACCEL_SYSTEM_DEAD_CHARACTER = 7,
    UWP_ACCEL_UNICODE_CHARACTER     = 8
} UwpCoreAcceleratorKeyEventType;

typedef struct UwpCorePhysicalKeyStatus {
    uint32_t RepeatCount;
    uint32_t ScanCode;
    uint8_t  IsExtendedKey;
    uint8_t  IsMenuKeyDown;
    uint8_t  WasKeyDown;
    uint8_t  IsKeyReleased;
} UwpCorePhysicalKeyStatus;

typedef enum UwpPointerDeviceType {
    UWP_POINTER_DEVICE_TOUCH = 0,
    UWP_POINTER_DEVICE_PEN   = 1,
    UWP_POINTER_DEVICE_MOUSE = 2
} UwpPointerDeviceType;

typedef enum UwpPointerUpdateKind {
    UWP_POINTER_UPDATE_OTHER                  = 0,
    UWP_POINTER_UPDATE_LEFT_BUTTON_PRESSED    = 1,
    UWP_POINTER_UPDATE_LEFT_BUTTON_RELEASED   = 2,
    UWP_POINTER_UPDATE_RIGHT_BUTTON_PRESSED   = 3,
    UWP_POINTER_UPDATE_RIGHT_BUTTON_RELEASED  = 4,
    UWP_POINTER_UPDATE_MIDDLE_BUTTON_PRESSED  = 5,
    UWP_POINTER_UPDATE_MIDDLE_BUTTON_RELEASED = 6,
    UWP_POINTER_UPDATE_XBUTTON1_PRESSED       = 7,
    UWP_POINTER_UPDATE_XBUTTON1_RELEASED      = 8,
    UWP_POINTER_UPDATE_XBUTTON2_PRESSED       = 9,
    UWP_POINTER_UPDATE_XBUTTON2_RELEASED      = 10
} UwpPointerUpdateKind;

typedef enum UwpVirtualKeyModifiers {
    UWP_MOD_NONE    = 0x0,
    UWP_MOD_CONTROL = 0x1,
    UWP_MOD_MENU    = 0x2,
    UWP_MOD_SHIFT   = 0x4,
    UWP_MOD_WINDOWS = 0x8
} UwpVirtualKeyModifiers;

typedef enum UwpActivationKind {
    UWP_ACTIVATION_KIND_LAUNCH = 0
} UwpActivationKind;

typedef enum UwpApplicationExecutionState {
    UWP_EXEC_STATE_NOT_RUNNING    = 0,
    UWP_EXEC_STATE_RUNNING        = 1,
    UWP_EXEC_STATE_SUSPENDED      = 2,
    UWP_EXEC_STATE_TERMINATED     = 3,
    UWP_EXEC_STATE_CLOSED_BY_USER = 4
} UwpApplicationExecutionState;

/* ============================================================================
 * Verified Clean-Room WinRT GUIDs (matching Wine 11.0 IDLs & uwp_bridge_abi.h)
 * ============================================================================ */

static const GUID UWP_IID_IUnknown =
    { 0x00000000u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const GUID UWP_IID_IInspectable = SCARLETT_IID_IINSPECTABLE_INIT;
static const GUID UWP_IID_IActivationFactory = SCARLETT_IID_IACTIVATIONFACTORY_INIT;
static const GUID UWP_IID_IAgileObject = SCARLETT_IID_IAGILEOBJECT_INIT;

/* Foundation / Async / Collections */
static const GUID UWP_IID_IAsyncInfo =
    { 0x00000036u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const GUID UWP_IID_IAsyncAction =
    { 0x5A648006u, 0x843Au, 0x4DA9u, { 0x86u, 0x5Bu, 0x9Du, 0x26u, 0xE5u, 0xDFu, 0xADu, 0x7Bu } };
static const GUID UWP_IID_IClosable =
    { 0x30D5A829u, 0x7FA4u, 0x4026u, { 0x83u, 0xBBu, 0xD7u, 0x5Bu, 0xAEu, 0x4Eu, 0xA9u, 0x9Eu } };
static const GUID UWP_IID_IDeferral =
    { 0xD6269732u, 0x3B7Fu, 0x46A7u, { 0xB4u, 0x0Bu, 0x4Fu, 0xDCu, 0xA2u, 0xA2u, 0xC6u, 0x93u } };
static const GUID UWP_IID_IPropertySet =
    { 0x8A43ED9Fu, 0xF4E6u, 0x4421u, { 0xACu, 0xF9u, 0x1Du, 0xABu, 0x29u, 0x86u, 0x82u, 0x0Cu } };
static const GUID UWP_IID_IMap_HSTRING_IInspectable =
    { 0x1B0D3570u, 0x0877u, 0x5EC2u, { 0x8Au, 0x2Cu, 0x3Bu, 0x95u, 0x39u, 0x50u, 0x6Au, 0xCAu } };
static const GUID UWP_IID_IObservableMap_HSTRING_IInspectable =
    { 0x236AAC9Du, 0xFB12u, 0x5C4Du, { 0xA4u, 0x1Cu, 0x9Eu, 0x44u, 0x5Fu, 0xB4u, 0xD7u, 0xECu } };

/* Windows.ApplicationModel.Core */
static const GUID UWP_IID_ICoreApplication =
    { 0x0AACF7A4u, 0x5E1Du, 0x49DFu, { 0x80u, 0x34u, 0xFBu, 0x6Au, 0x68u, 0xBCu, 0x5Eu, 0xD1u } };
static const GUID UWP_IID_ICoreApplication2 =
    { 0x998681FBu, 0x1AB6u, 0x4B7Fu, { 0xBEu, 0x4Au, 0x9Au, 0x06u, 0x45u, 0x22u, 0x4Cu, 0x04u } };
static const GUID UWP_IID_ICoreApplicationExit =
    { 0xCF86461Du, 0x261Eu, 0x4B72u, { 0x9Au, 0xCDu, 0x44u, 0xEDu, 0x2Au, 0xCEu, 0x6Au, 0x29u } };
static const GUID UWP_IID_ICoreApplicationUnhandledError =
    { 0xF0E24AB0u, 0xDD09u, 0x42E1u, { 0xB0u, 0xBCu, 0xE0u, 0xE1u, 0x31u, 0xF7u, 0x8Du, 0x7Eu } };
static const GUID UWP_IID_ICoreApplicationUseCount =
    { 0x518DC408u, 0xC077u, 0x475Bu, { 0x80u, 0x9Eu, 0x0Bu, 0xC0u, 0xC5u, 0x7Eu, 0x4Bu, 0x74u } };
static const GUID UWP_IID_ICoreImmersiveApplication =
    { 0x1ADA0E3Eu, 0xE4A2u, 0x4123u, { 0xB4u, 0x51u, 0xDCu, 0x96u, 0xBFu, 0x80u, 0x04u, 0x19u } };
static const GUID UWP_IID_ICoreApplicationView =
    { 0x638BB2DBu, 0x451Du, 0x4661u, { 0xB0u, 0x99u, 0x41u, 0x4Fu, 0x34u, 0xFFu, 0xB9u, 0xF1u } };
static const GUID UWP_IID_ICoreApplicationView2 =
    { 0x68EB7ADFu, 0x917Fu, 0x48EBu, { 0x9Au, 0xEBu, 0x7Du, 0xE5u, 0x3Eu, 0x08u, 0x6Au, 0xB1u } };
static const GUID UWP_IID_ICoreApplicationView3 =
    { 0x07EBE1B3u, 0xA4CFu, 0x4550u, { 0xABu, 0x70u, 0xB0u, 0x7Eu, 0x85u, 0x33u, 0x0Bu, 0xC8u } };
static const GUID UWP_IID_ICoreApplicationView5 =
    { 0x2BC095A8u, 0x8EF0u, 0x446Du, { 0x9Eu, 0x60u, 0x3Au, 0x3Eu, 0x04u, 0x28u, 0xC6u, 0x71u } };
static const GUID UWP_IID_ICoreApplicationViewTitleBar =
    { 0x006D35E3u, 0xE1F1u, 0x431Bu, { 0x95u, 0x08u, 0x29u, 0xB9u, 0x69u, 0x26u, 0xACu, 0x53u } };
static const GUID UWP_IID_IFrameworkView =
    { 0xFAAB5CD0u, 0x8924u, 0x45ACu, { 0xADu, 0x0Fu, 0xA0u, 0x8Fu, 0xAEu, 0x5Du, 0x03u, 0x24u } };
static const GUID UWP_IID_IFrameworkViewSource =
    { 0xCD770614u, 0x65C4u, 0x426Cu, { 0x94u, 0x94u, 0x34u, 0xFCu, 0x43u, 0x55u, 0x48u, 0x62u } };
static const GUID UWP_IID_IVectorView_CoreApplicationView =
    { 0xDE9E16C4u, 0x1B7Cu, 0x5126u, { 0xB1u, 0xD8u, 0x7Cu, 0xD0u, 0x4Fu, 0x13u, 0xBDu, 0x08u } };
static const GUID UWP_IID_IIterable_CoreApplicationView =
    { 0x32BC12D1u, 0x2653u, 0x5A41u, { 0xA5u, 0x5Eu, 0x88u, 0xA1u, 0x2Au, 0xF2u, 0x02u, 0x6Au } };
static const GUID UWP_IID_IIterator_CoreApplicationView =
    { 0x4F5F6944u, 0x264Bu, 0x5868u, { 0x80u, 0x9Eu, 0xC7u, 0xACu, 0x1Au, 0xC5u, 0xEDu, 0xADu } };

/* Windows.ApplicationModel / Activation */
static const GUID UWP_IID_IActivatedEventArgs =
    { 0xCF651713u, 0xCD08u, 0x4FD8u, { 0xB6u, 0x97u, 0xA2u, 0x81u, 0xB6u, 0x54u, 0x4Eu, 0x2Eu } };
static const GUID UWP_IID_ILaunchActivatedEventArgs =
    { 0xFBC93E26u, 0xA14Au, 0x4B4Fu, { 0x82u, 0xB0u, 0x33u, 0xBEu, 0xD9u, 0x20u, 0xAFu, 0x52u } };
static const GUID UWP_IID_IPrelaunchActivatedEventArgs =
    { 0x0C44717Bu, 0x19F7u, 0x48D6u, { 0xB0u, 0x46u, 0xCFu, 0x22u, 0x82u, 0x6Eu, 0xAAu, 0x74u } };
static const GUID UWP_IID_IApplicationViewActivatedEventArgs =
    { 0x930CEF4Bu, 0xB829u, 0x40FCu, { 0x88u, 0xF4u, 0x85u, 0x13u, 0xE8u, 0xA6u, 0x47u, 0x38u } };
static const GUID UWP_IID_ISplashScreen =
    { 0xCA4D975Cu, 0xD4D6u, 0x43F0u, { 0x97u, 0xC0u, 0x08u, 0x33u, 0xC6u, 0x39u, 0x1Cu, 0x24u } };
static const GUID UWP_IID_ISuspendingEventArgs =
    { 0x96061C05u, 0x2DBAu, 0x4D08u, { 0xB0u, 0xBDu, 0x2Bu, 0x30u, 0xA1u, 0x31u, 0xC6u, 0xAAu } };
static const GUID UWP_IID_ISuspendingOperation =
    { 0x9DA4CA41u, 0x20E1u, 0x4E9Bu, { 0x9Fu, 0x65u, 0xA9u, 0xF4u, 0x35u, 0x34u, 0x0Cu, 0x3Au } };
static const GUID UWP_IID_ISuspendingDeferral =
    { 0x59140509u, 0x8BC9u, 0x4EB4u, { 0xB6u, 0x36u, 0xDAu, 0xBDu, 0xC4u, 0xF4u, 0x6Fu, 0x66u } };
static const GUID UWP_IID_IEnteredBackgroundEventArgs =
    { 0xF722DCC2u, 0x9827u, 0x403Du, { 0xAAu, 0xEDu, 0xECu, 0xCAu, 0x9Au, 0xC1u, 0x73u, 0x98u } };
static const GUID UWP_IID_ILeavingBackgroundEventArgs =
    { 0x39C6EC9Au, 0xAE6Eu, 0x46F9u, { 0xA0u, 0x7Au, 0xCFu, 0xC2u, 0x3Fu, 0x88u, 0x73u, 0x3Eu } };

/* Windows.UI.Core */
static const GUID UWP_IID_ICoreWindow =
    { 0x79B9D5F2u, 0x879Eu, 0x4B89u, { 0xB7u, 0x98u, 0x79u, 0xE4u, 0x75u, 0x98u, 0x03u, 0x0Cu } };
static const GUID UWP_IID_ICoreWindow2 =
    { 0x7C2B1B85u, 0x6917u, 0x4361u, { 0x9Cu, 0x02u, 0x0Du, 0x9Eu, 0x3Au, 0x42u, 0x0Bu, 0x95u } };
static const GUID UWP_IID_ICoreWindow3 =
    { 0x32C20DD8u, 0xFAEFu, 0x4375u, { 0xA2u, 0xABu, 0x32u, 0x64u, 0x0Eu, 0x48u, 0x15u, 0xC7u } };
static const GUID UWP_IID_ICoreWindow4 =
    { 0x35CAF0D0u, 0x47F0u, 0x436Cu, { 0xAFu, 0x97u, 0x0Du, 0xD8u, 0x8Fu, 0x6Fu, 0x5Fu, 0x02u } };
static const GUID UWP_IID_ICoreWindow5 =
    { 0x4B4AE1E1u, 0x2E6Du, 0x4EAAu, { 0xBDu, 0xA1u, 0x1Cu, 0x5Cu, 0xC1u, 0xBEu, 0xE1u, 0x41u } };
static const GUID UWP_IID_ICoreWindowStatic =
    { 0x4D239005u, 0x3C2Au, 0x41B1u, { 0x90u, 0x22u, 0x53u, 0x6Bu, 0xB9u, 0xCFu, 0x93u, 0xB1u } };
static const GUID UWP_IID_ICoreDispatcher =
    { 0x60DB2FA8u, 0xB705u, 0x4FDEu, { 0xA7u, 0xD6u, 0xEBu, 0xBBu, 0x18u, 0x91u, 0xD3u, 0x9Eu } };
static const GUID UWP_IID_ICoreAcceleratorKeys =
    { 0x9FFDF7F5u, 0xB8C9u, 0x4EF0u, { 0xB7u, 0xD2u, 0x1Du, 0xE6u, 0x26u, 0x56u, 0x1Fu, 0xC8u } };
static const GUID UWP_IID_ICoreWindowEventArgs =
    { 0x272B1EF3u, 0xC633u, 0x4DA5u, { 0xA2u, 0x6Cu, 0xC6u, 0xD0u, 0xF5u, 0x6Bu, 0x29u, 0xDAu } };
static const GUID UWP_IID_IWindowSizeChangedEventArgs =
    { 0x5A200EC7u, 0x0426u, 0x47DCu, { 0xB8u, 0x6Cu, 0x6Fu, 0x47u, 0x59u, 0x15u, 0xE4u, 0x51u } };
static const GUID UWP_IID_IWindowActivatedEventArgs =
    { 0x179D65E7u, 0x4658u, 0x4CB6u, { 0xAAu, 0x13u, 0x41u, 0xD0u, 0x94u, 0xEAu, 0x25u, 0x5Eu } };
static const GUID UWP_IID_IVisibilityChangedEventArgs =
    { 0xBF9918EAu, 0xD801u, 0x4564u, { 0xA4u, 0x95u, 0xB1u, 0xE8u, 0x4Fu, 0x8Au, 0xD0u, 0x85u } };
static const GUID UWP_IID_IKeyEventArgs =
    { 0x5FF5E930u, 0x2544u, 0x4A17u, { 0xBDu, 0x78u, 0x1Fu, 0x2Fu, 0xDEu, 0xBBu, 0x10u, 0x6Bu } };
static const GUID UWP_IID_IKeyEventArgs2 =
    { 0x583ADD98u, 0x0790u, 0x4571u, { 0x9Bu, 0x12u, 0x64u, 0x5Eu, 0xF9u, 0xD7u, 0x9Eu, 0x42u } };
static const GUID UWP_IID_ICharacterReceivedEventArgs =
    { 0xC584659Fu, 0x99B2u, 0x4BCCu, { 0xBDu, 0x33u, 0x04u, 0xE6u, 0x3Fu, 0x42u, 0x90u, 0x2Eu } };
static const GUID UWP_IID_IPointerEventArgs =
    { 0x920D9CB1u, 0xA5FCu, 0x4A21u, { 0x8Cu, 0x09u, 0x49u, 0xDFu, 0xE6u, 0xFFu, 0xE2u, 0x5Fu } };
static const GUID UWP_IID_IAcceleratorKeyEventArgs =
    { 0xFF1C4C4Au, 0x9287u, 0x470Bu, { 0x83u, 0x6Eu, 0x90u, 0x86u, 0xE3u, 0x12u, 0x6Au, 0xDEu } };
static const GUID UWP_IID_IIdleDispatchedHandlerArgs =
    { 0x98BB6A24u, 0xDC1Cu, 0x43CBu, { 0xB4u, 0xEDu, 0xD1u, 0xC0u, 0xEBu, 0x23u, 0x91u, 0xF3u } };

/* Windows.UI.Input / Windows.Devices.Input */
static const GUID UWP_IID_IPointerPoint =
    { 0xE995317Du, 0x7296u, 0x42D9u, { 0x82u, 0x33u, 0xC5u, 0xBEu, 0x73u, 0xB7u, 0x4Au, 0x4Au } };
static const GUID UWP_IID_IPointerPointProperties =
    { 0xC79D8A4Bu, 0xC163u, 0x4EE7u, { 0x80u, 0x3Fu, 0x67u, 0xCEu, 0x79u, 0xF9u, 0x97u, 0x2Du } };
static const GUID UWP_IID_IPointerDevice =
    { 0x93C9BAFCu, 0xEBCBu, 0x467Eu, { 0x82u, 0xC6u, 0x27u, 0x6Fu, 0xEAu, 0xE3u, 0x6Bu, 0x5Au } };

/* ============================================================================
 * WinRT C Vtable Definitions (IInspectable + Exact IDL Slot Layouts)
 * ============================================================================ */

#define UWP_IINSPECTABLE_VTBL_HEAD(IfaceType)                                         \
    HRESULT (UWP_CALL *QueryInterface)(IfaceType *This, REFIID riid, void **ppv);     \
    ULONG   (UWP_CALL *AddRef)(IfaceType *This);                                      \
    ULONG   (UWP_CALL *Release)(IfaceType *This);                                     \
    HRESULT (UWP_CALL *GetIids)(IfaceType *This, ULONG *iidCount, IID **iids);        \
    HRESULT (UWP_CALL *GetRuntimeClassName)(IfaceType *This, UWP_HSTRING *className); \
    HRESULT (UWP_CALL *GetTrustLevel)(IfaceType *This, UwpTrustLevel *trustLevel)

typedef struct UwpIInspectable UwpIInspectable;
typedef struct UwpIInspectableVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIInspectable);
} UwpIInspectableVtbl;
struct UwpIInspectable {
    const UwpIInspectableVtbl *lpVtbl;
};

typedef struct UwpIActivationFactory UwpIActivationFactory;
typedef struct UwpIActivationFactoryVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIActivationFactory);
    HRESULT (UWP_CALL *ActivateInstance)(UwpIActivationFactory *This, UwpIInspectable **instance);
} UwpIActivationFactoryVtbl;
struct UwpIActivationFactory {
    const UwpIActivationFactoryVtbl *lpVtbl;
};

/* Generic WinRT Delegate Invoke signatures (all delegates inherit IUnknown + Invoke at slot 3) */
typedef struct UwpDispatchedHandler UwpDispatchedHandler;
typedef struct UwpDispatchedHandlerVtbl {
    HRESULT (UWP_CALL *QueryInterface)(UwpDispatchedHandler *This, REFIID riid, void **ppv);
    ULONG   (UWP_CALL *AddRef)(UwpDispatchedHandler *This);
    ULONG   (UWP_CALL *Release)(UwpDispatchedHandler *This);
    HRESULT (UWP_CALL *Invoke)(UwpDispatchedHandler *This);
} UwpDispatchedHandlerVtbl;
struct UwpDispatchedHandler {
    const UwpDispatchedHandlerVtbl *lpVtbl;
};

typedef struct UwpIdleDispatchedHandler UwpIdleDispatchedHandler;
typedef struct UwpIdleDispatchedHandlerVtbl {
    HRESULT (UWP_CALL *QueryInterface)(UwpIdleDispatchedHandler *This, REFIID riid, void **ppv);
    ULONG   (UWP_CALL *AddRef)(UwpIdleDispatchedHandler *This);
    ULONG   (UWP_CALL *Release)(UwpIdleDispatchedHandler *This);
    HRESULT (UWP_CALL *Invoke)(UwpIdleDispatchedHandler *This, void *args);
} UwpIdleDispatchedHandlerVtbl;
struct UwpIdleDispatchedHandler {
    const UwpIdleDispatchedHandlerVtbl *lpVtbl;
};

typedef struct UwpTypedEventHandler UwpTypedEventHandler;
typedef struct UwpTypedEventHandlerVtbl {
    HRESULT (UWP_CALL *QueryInterface)(UwpTypedEventHandler *This, REFIID riid, void **ppv);
    ULONG   (UWP_CALL *AddRef)(UwpTypedEventHandler *This);
    ULONG   (UWP_CALL *Release)(UwpTypedEventHandler *This);
    HRESULT (UWP_CALL *Invoke)(UwpTypedEventHandler *This, void *sender, void *args);
} UwpTypedEventHandlerVtbl;
struct UwpTypedEventHandler {
    const UwpTypedEventHandlerVtbl *lpVtbl;
};

typedef struct UwpAsyncActionCompletedHandler UwpAsyncActionCompletedHandler;
typedef struct UwpAsyncActionCompletedHandlerVtbl {
    HRESULT (UWP_CALL *QueryInterface)(UwpAsyncActionCompletedHandler *This, REFIID riid, void **ppv);
    ULONG   (UWP_CALL *AddRef)(UwpAsyncActionCompletedHandler *This);
    ULONG   (UWP_CALL *Release)(UwpAsyncActionCompletedHandler *This);
    HRESULT (UWP_CALL *Invoke)(UwpAsyncActionCompletedHandler *This, void *asyncAction, UwpAsyncStatus status);
} UwpAsyncActionCompletedHandlerVtbl;
struct UwpAsyncActionCompletedHandler {
    const UwpAsyncActionCompletedHandlerVtbl *lpVtbl;
};

/* IAsyncInfo */
typedef struct UwpIAsyncInfo UwpIAsyncInfo;
typedef struct UwpIAsyncInfoVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIAsyncInfo);
    HRESULT (UWP_CALL *get_Id)(UwpIAsyncInfo *This, uint32_t *id);
    HRESULT (UWP_CALL *get_Status)(UwpIAsyncInfo *This, UwpAsyncStatus *status);
    HRESULT (UWP_CALL *get_ErrorCode)(UwpIAsyncInfo *This, HRESULT *error_code);
    HRESULT (UWP_CALL *Cancel)(UwpIAsyncInfo *This);
    HRESULT (UWP_CALL *Close)(UwpIAsyncInfo *This);
} UwpIAsyncInfoVtbl;
struct UwpIAsyncInfo {
    const UwpIAsyncInfoVtbl *lpVtbl;
};

/* IAsyncAction */
typedef struct UwpIAsyncAction UwpIAsyncAction;
typedef struct UwpIAsyncActionVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIAsyncAction);
    HRESULT (UWP_CALL *put_Completed)(UwpIAsyncAction *This, UwpAsyncActionCompletedHandler *handler);
    HRESULT (UWP_CALL *get_Completed)(UwpIAsyncAction *This, UwpAsyncActionCompletedHandler **handler);
    HRESULT (UWP_CALL *GetResults)(UwpIAsyncAction *This);
} UwpIAsyncActionVtbl;
struct UwpIAsyncAction {
    const UwpIAsyncActionVtbl *lpVtbl;
};

/* IFrameworkView & IFrameworkViewSource (called by CoreApplication::Run) */
typedef struct UwpIFrameworkView UwpIFrameworkView;
typedef struct UwpIFrameworkViewVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIFrameworkView);
    HRESULT (UWP_CALL *Initialize)(UwpIFrameworkView *This, void *application_view);
    HRESULT (UWP_CALL *SetWindow)(UwpIFrameworkView *This, void *core_window);
    HRESULT (UWP_CALL *Load)(UwpIFrameworkView *This, UWP_HSTRING entry_point);
    HRESULT (UWP_CALL *Run)(UwpIFrameworkView *This);
    HRESULT (UWP_CALL *Uninitialize)(UwpIFrameworkView *This);
} UwpIFrameworkViewVtbl;
struct UwpIFrameworkView {
    const UwpIFrameworkViewVtbl *lpVtbl;
};

typedef struct UwpIFrameworkViewSource UwpIFrameworkViewSource;
typedef struct UwpIFrameworkViewSourceVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIFrameworkViewSource);
    HRESULT (UWP_CALL *CreateView)(UwpIFrameworkViewSource *This, UwpIFrameworkView **view);
} UwpIFrameworkViewSourceVtbl;
struct UwpIFrameworkViewSource {
    const UwpIFrameworkViewSourceVtbl *lpVtbl;
};

/* IGetActivationFactory */
typedef struct UwpIGetActivationFactory UwpIGetActivationFactory;
typedef struct UwpIGetActivationFactoryVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIGetActivationFactory);
    HRESULT (UWP_CALL *GetActivationFactory)(UwpIGetActivationFactory *This, UWP_HSTRING class_id, UwpIInspectable **factory);
} UwpIGetActivationFactoryVtbl;
struct UwpIGetActivationFactory {
    const UwpIGetActivationFactoryVtbl *lpVtbl;
};

/* ICoreApplication */
typedef struct UwpICoreApplication UwpICoreApplication;
typedef struct UwpICoreApplicationVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplication);
    HRESULT (UWP_CALL *get_Id)(UwpICoreApplication *This, UWP_HSTRING *value);
    HRESULT (UWP_CALL *add_Suspending)(UwpICoreApplication *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_Suspending)(UwpICoreApplication *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *add_Resuming)(UwpICoreApplication *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_Resuming)(UwpICoreApplication *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *get_Properties)(UwpICoreApplication *This, void **value);
    HRESULT (UWP_CALL *GetCurrentView)(UwpICoreApplication *This, void **value);
    HRESULT (UWP_CALL *Run)(UwpICoreApplication *This, UwpIFrameworkViewSource *view_source);
    HRESULT (UWP_CALL *RunWithActivationFactories)(UwpICoreApplication *This, UwpIGetActivationFactory *factory);
} UwpICoreApplicationVtbl;
struct UwpICoreApplication {
    const UwpICoreApplicationVtbl *lpVtbl;
};

/* ICoreApplication2 */
typedef struct UwpICoreApplication2 UwpICoreApplication2;
typedef struct UwpICoreApplication2Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplication2);
    HRESULT (UWP_CALL *add_BackgroundActivated)(UwpICoreApplication2 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_BackgroundActivated)(UwpICoreApplication2 *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *add_LeavingBackground)(UwpICoreApplication2 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_LeavingBackground)(UwpICoreApplication2 *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *add_EnteredBackground)(UwpICoreApplication2 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_EnteredBackground)(UwpICoreApplication2 *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *EnablePrelaunch)(UwpICoreApplication2 *This, uint8_t value);
} UwpICoreApplication2Vtbl;
struct UwpICoreApplication2 {
    const UwpICoreApplication2Vtbl *lpVtbl;
};

/* ICoreApplicationExit */
typedef struct UwpICoreApplicationExit UwpICoreApplicationExit;
typedef struct UwpICoreApplicationExitVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationExit);
    HRESULT (UWP_CALL *Exit)(UwpICoreApplicationExit *This);
    HRESULT (UWP_CALL *add_Exiting)(UwpICoreApplicationExit *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_Exiting)(UwpICoreApplicationExit *This, UwpEventRegistrationToken token);
} UwpICoreApplicationExitVtbl;
struct UwpICoreApplicationExit {
    const UwpICoreApplicationExitVtbl *lpVtbl;
};

/* ICoreImmersiveApplication (exposes MainView!) */
typedef struct UwpICoreImmersiveApplication UwpICoreImmersiveApplication;
typedef struct UwpICoreImmersiveApplicationVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreImmersiveApplication);
    HRESULT (UWP_CALL *get_Views)(UwpICoreImmersiveApplication *This, void **value);
    HRESULT (UWP_CALL *CreateNewView)(UwpICoreImmersiveApplication *This, UWP_HSTRING runtime_type, UWP_HSTRING entry_point, void **view);
    HRESULT (UWP_CALL *get_MainView)(UwpICoreImmersiveApplication *This, void **value);
} UwpICoreImmersiveApplicationVtbl;
struct UwpICoreImmersiveApplication {
    const UwpICoreImmersiveApplicationVtbl *lpVtbl;
};

/* ICoreApplicationUnhandledError */
typedef struct UwpICoreApplicationUnhandledError UwpICoreApplicationUnhandledError;
typedef struct UwpICoreApplicationUnhandledErrorVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationUnhandledError);
    HRESULT (UWP_CALL *add_UnhandledErrorDetected)(UwpICoreApplicationUnhandledError *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_UnhandledErrorDetected)(UwpICoreApplicationUnhandledError *This, UwpEventRegistrationToken token);
} UwpICoreApplicationUnhandledErrorVtbl;
struct UwpICoreApplicationUnhandledError {
    const UwpICoreApplicationUnhandledErrorVtbl *lpVtbl;
};

/* ICoreApplicationUseCount */
typedef struct UwpICoreApplicationUseCount UwpICoreApplicationUseCount;
typedef struct UwpICoreApplicationUseCountVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationUseCount);
    HRESULT (UWP_CALL *IncrementApplicationUseCount)(UwpICoreApplicationUseCount *This);
    HRESULT (UWP_CALL *DecrementApplicationUseCount)(UwpICoreApplicationUseCount *This);
} UwpICoreApplicationUseCountVtbl;
struct UwpICoreApplicationUseCount {
    const UwpICoreApplicationUseCountVtbl *lpVtbl;
};

/* ICoreApplicationView / View2 / View3 / View5 / TitleBar */
typedef struct UwpICoreApplicationView UwpICoreApplicationView;
typedef struct UwpICoreApplicationViewVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationView);
    HRESULT (UWP_CALL *get_CoreWindow)(UwpICoreApplicationView *This, void **value);
    HRESULT (UWP_CALL *add_Activated)(UwpICoreApplicationView *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_Activated)(UwpICoreApplicationView *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *get_IsMain)(UwpICoreApplicationView *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsHosted)(UwpICoreApplicationView *This, uint8_t *value);
} UwpICoreApplicationViewVtbl;
struct UwpICoreApplicationView {
    const UwpICoreApplicationViewVtbl *lpVtbl;
};

typedef struct UwpICoreApplicationView2 UwpICoreApplicationView2;
typedef struct UwpICoreApplicationView2Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationView2);
    HRESULT (UWP_CALL *get_Dispatcher)(UwpICoreApplicationView2 *This, void **value);
} UwpICoreApplicationView2Vtbl;
struct UwpICoreApplicationView2 {
    const UwpICoreApplicationView2Vtbl *lpVtbl;
};

typedef struct UwpICoreApplicationView3 UwpICoreApplicationView3;
typedef struct UwpICoreApplicationView3Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationView3);
    HRESULT (UWP_CALL *get_IsComponent)(UwpICoreApplicationView3 *This, uint8_t *value);
    HRESULT (UWP_CALL *get_TitleBar)(UwpICoreApplicationView3 *This, void **value);
    HRESULT (UWP_CALL *add_HostedViewClosing)(UwpICoreApplicationView3 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_HostedViewClosing)(UwpICoreApplicationView3 *This, UwpEventRegistrationToken token);
} UwpICoreApplicationView3Vtbl;
struct UwpICoreApplicationView3 {
    const UwpICoreApplicationView3Vtbl *lpVtbl;
};

typedef struct UwpICoreApplicationView5 UwpICoreApplicationView5;
typedef struct UwpICoreApplicationView5Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationView5);
    HRESULT (UWP_CALL *get_Properties)(UwpICoreApplicationView5 *This, void **value);
} UwpICoreApplicationView5Vtbl;
struct UwpICoreApplicationView5 {
    const UwpICoreApplicationView5Vtbl *lpVtbl;
};

typedef struct UwpICoreApplicationViewTitleBar UwpICoreApplicationViewTitleBar;
typedef struct UwpICoreApplicationViewTitleBarVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreApplicationViewTitleBar);
    HRESULT (UWP_CALL *put_ExtendViewIntoTitleBar)(UwpICoreApplicationViewTitleBar *This, uint8_t value);
    HRESULT (UWP_CALL *get_ExtendViewIntoTitleBar)(UwpICoreApplicationViewTitleBar *This, uint8_t *value);
    HRESULT (UWP_CALL *get_SystemOverlayLeftInset)(UwpICoreApplicationViewTitleBar *This, double *value);
    HRESULT (UWP_CALL *get_SystemOverlayRightInset)(UwpICoreApplicationViewTitleBar *This, double *value);
    HRESULT (UWP_CALL *get_Height)(UwpICoreApplicationViewTitleBar *This, double *value);
    HRESULT (UWP_CALL *add_LayoutMetricsChanged)(UwpICoreApplicationViewTitleBar *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_LayoutMetricsChanged)(UwpICoreApplicationViewTitleBar *This, UwpEventRegistrationToken token);
    HRESULT (UWP_CALL *get_IsVisible)(UwpICoreApplicationViewTitleBar *This, uint8_t *value);
    HRESULT (UWP_CALL *add_IsVisibleChanged)(UwpICoreApplicationViewTitleBar *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *token);
    HRESULT (UWP_CALL *remove_IsVisibleChanged)(UwpICoreApplicationViewTitleBar *This, UwpEventRegistrationToken token);
} UwpICoreApplicationViewTitleBarVtbl;
struct UwpICoreApplicationViewTitleBar {
    const UwpICoreApplicationViewTitleBarVtbl *lpVtbl;
};

/* ICoreWindow / ICoreWindow2..5 / ICoreWindowStatic / ICoreDispatcher */
typedef struct UwpICoreWindow UwpICoreWindow;
typedef struct UwpICoreWindowVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindow);
    HRESULT (UWP_CALL *get_AutomationHostProvider)(UwpICoreWindow *This, UwpIInspectable **value);
    HRESULT (UWP_CALL *get_Bounds)(UwpICoreWindow *This, UwpRect *value);
    HRESULT (UWP_CALL *get_CustomProperties)(UwpICoreWindow *This, void **value);
    HRESULT (UWP_CALL *get_Dispatcher)(UwpICoreWindow *This, void **value);
    HRESULT (UWP_CALL *get_FlowDirection)(UwpICoreWindow *This, UwpCoreWindowFlowDirection *value);
    HRESULT (UWP_CALL *put_FlowDirection)(UwpICoreWindow *This, UwpCoreWindowFlowDirection value);
    HRESULT (UWP_CALL *get_IsInputEnabled)(UwpICoreWindow *This, uint8_t *value);
    HRESULT (UWP_CALL *put_IsInputEnabled)(UwpICoreWindow *This, uint8_t value);
    HRESULT (UWP_CALL *get_PointerCursor)(UwpICoreWindow *This, void **value);
    HRESULT (UWP_CALL *put_PointerCursor)(UwpICoreWindow *This, void *value);
    HRESULT (UWP_CALL *get_PointerPosition)(UwpICoreWindow *This, UwpPoint *value);
    HRESULT (UWP_CALL *get_Visible)(UwpICoreWindow *This, uint8_t *value);
    HRESULT (UWP_CALL *Activate)(UwpICoreWindow *This);
    HRESULT (UWP_CALL *Close)(UwpICoreWindow *This);
    HRESULT (UWP_CALL *GetAsyncKeyState)(UwpICoreWindow *This, int32_t virtual_key, UwpCoreVirtualKeyStates *state);
    HRESULT (UWP_CALL *GetKeyState)(UwpICoreWindow *This, int32_t virtual_key, UwpCoreVirtualKeyStates *state);
    HRESULT (UWP_CALL *ReleasePointerCapture)(UwpICoreWindow *This);
    HRESULT (UWP_CALL *SetPointerCapture)(UwpICoreWindow *This);
    HRESULT (UWP_CALL *add_Activated)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_Activated)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_AutomationProviderRequested)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_AutomationProviderRequested)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_CharacterReceived)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_CharacterReceived)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_Closed)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_Closed)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_InputEnabled)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_InputEnabled)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_KeyDown)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_KeyDown)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_KeyUp)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_KeyUp)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerCaptureLost)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerCaptureLost)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerEntered)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerEntered)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerExited)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerExited)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerMoved)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerMoved)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerPressed)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerPressed)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerReleased)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerReleased)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_TouchHitTesting)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_TouchHitTesting)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_PointerWheelChanged)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_PointerWheelChanged)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_SizeChanged)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_SizeChanged)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_VisibilityChanged)(UwpICoreWindow *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_VisibilityChanged)(UwpICoreWindow *This, UwpEventRegistrationToken cookie);
} UwpICoreWindowVtbl;
struct UwpICoreWindow {
    const UwpICoreWindowVtbl *lpVtbl;
};

typedef struct UwpICoreWindow2 UwpICoreWindow2;
typedef struct UwpICoreWindow2Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindow2);
    HRESULT (UWP_CALL *put_PointerPosition)(UwpICoreWindow2 *This, UwpPoint value);
} UwpICoreWindow2Vtbl;
struct UwpICoreWindow2 {
    const UwpICoreWindow2Vtbl *lpVtbl;
};

typedef struct UwpICoreWindow3 UwpICoreWindow3;
typedef struct UwpICoreWindow3Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindow3);
    HRESULT (UWP_CALL *add_ClosestInteractiveBoundsRequested)(UwpICoreWindow3 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_ClosestInteractiveBoundsRequested)(UwpICoreWindow3 *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *GetCurrentKeyEventDeviceId)(UwpICoreWindow3 *This, UWP_HSTRING *value);
} UwpICoreWindow3Vtbl;
struct UwpICoreWindow3 {
    const UwpICoreWindow3Vtbl *lpVtbl;
};

typedef struct UwpICoreWindow4 UwpICoreWindow4;
typedef struct UwpICoreWindow4Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindow4);
    HRESULT (UWP_CALL *add_ResizeStarted)(UwpICoreWindow4 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_ResizeStarted)(UwpICoreWindow4 *This, UwpEventRegistrationToken cookie);
    HRESULT (UWP_CALL *add_ResizeCompleted)(UwpICoreWindow4 *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_ResizeCompleted)(UwpICoreWindow4 *This, UwpEventRegistrationToken cookie);
} UwpICoreWindow4Vtbl;
struct UwpICoreWindow4 {
    const UwpICoreWindow4Vtbl *lpVtbl;
};

typedef struct UwpICoreWindow5 UwpICoreWindow5;
typedef struct UwpICoreWindow5Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindow5);
    HRESULT (UWP_CALL *get_DispatcherQueue)(UwpICoreWindow5 *This, void **value);
    HRESULT (UWP_CALL *get_ActivationMode)(UwpICoreWindow5 *This, UwpCoreWindowActivationMode *value);
} UwpICoreWindow5Vtbl;
struct UwpICoreWindow5 {
    const UwpICoreWindow5Vtbl *lpVtbl;
};

typedef struct UwpICoreWindowStatic UwpICoreWindowStatic;
typedef struct UwpICoreWindowStaticVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindowStatic);
    HRESULT (UWP_CALL *GetForCurrentThread)(UwpICoreWindowStatic *This, UwpICoreWindow **window);
} UwpICoreWindowStaticVtbl;
struct UwpICoreWindowStatic {
    const UwpICoreWindowStaticVtbl *lpVtbl;
};

typedef struct UwpICoreWindowInterop {
    const ICoreWindowInteropBridgeVtbl *lpVtbl;
} UwpICoreWindowInterop;

typedef struct UwpICoreDispatcher UwpICoreDispatcher;
typedef struct UwpICoreDispatcherVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreDispatcher);
    HRESULT (UWP_CALL *get_HasThreadAccess)(UwpICoreDispatcher *This, uint8_t *value);
    HRESULT (UWP_CALL *ProcessEvents)(UwpICoreDispatcher *This, UwpCoreProcessEventsOption options);
    HRESULT (UWP_CALL *RunAsync)(UwpICoreDispatcher *This, UwpCoreDispatcherPriority priority,
                                 UwpDispatchedHandler *callback, UwpIAsyncAction **action);
    HRESULT (UWP_CALL *RunIdleAsync)(UwpICoreDispatcher *This, UwpIdleDispatchedHandler *callback,
                                     UwpIAsyncAction **action);
} UwpICoreDispatcherVtbl;
struct UwpICoreDispatcher {
    const UwpICoreDispatcherVtbl *lpVtbl;
};

typedef struct UwpICoreAcceleratorKeys UwpICoreAcceleratorKeys;
typedef struct UwpICoreAcceleratorKeysVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreAcceleratorKeys);
    HRESULT (UWP_CALL *add_AcceleratorKeyActivated)(UwpICoreAcceleratorKeys *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_AcceleratorKeyActivated)(UwpICoreAcceleratorKeys *This, UwpEventRegistrationToken cookie);
} UwpICoreAcceleratorKeysVtbl;
struct UwpICoreAcceleratorKeys {
    const UwpICoreAcceleratorKeysVtbl *lpVtbl;
};

/* Event Args Interfaces */
typedef struct UwpICoreWindowEventArgs UwpICoreWindowEventArgs;
typedef struct UwpICoreWindowEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICoreWindowEventArgs);
    HRESULT (UWP_CALL *get_Handled)(UwpICoreWindowEventArgs *This, uint8_t *value);
    HRESULT (UWP_CALL *put_Handled)(UwpICoreWindowEventArgs *This, uint8_t value);
} UwpICoreWindowEventArgsVtbl;
struct UwpICoreWindowEventArgs {
    const UwpICoreWindowEventArgsVtbl *lpVtbl;
};

typedef struct UwpIWindowSizeChangedEventArgs UwpIWindowSizeChangedEventArgs;
typedef struct UwpIWindowSizeChangedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIWindowSizeChangedEventArgs);
    HRESULT (UWP_CALL *get_Size)(UwpIWindowSizeChangedEventArgs *This, UwpSize *value);
} UwpIWindowSizeChangedEventArgsVtbl;
struct UwpIWindowSizeChangedEventArgs {
    const UwpIWindowSizeChangedEventArgsVtbl *lpVtbl;
};

typedef struct UwpIWindowActivatedEventArgs UwpIWindowActivatedEventArgs;
typedef struct UwpIWindowActivatedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIWindowActivatedEventArgs);
    HRESULT (UWP_CALL *get_WindowActivationState)(UwpIWindowActivatedEventArgs *This, UwpCoreWindowActivationState *value);
} UwpIWindowActivatedEventArgsVtbl;
struct UwpIWindowActivatedEventArgs {
    const UwpIWindowActivatedEventArgsVtbl *lpVtbl;
};

typedef struct UwpIVisibilityChangedEventArgs UwpIVisibilityChangedEventArgs;
typedef struct UwpIVisibilityChangedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIVisibilityChangedEventArgs);
    HRESULT (UWP_CALL *get_Visible)(UwpIVisibilityChangedEventArgs *This, uint8_t *value);
} UwpIVisibilityChangedEventArgsVtbl;
struct UwpIVisibilityChangedEventArgs {
    const UwpIVisibilityChangedEventArgsVtbl *lpVtbl;
};

typedef struct UwpIKeyEventArgs UwpIKeyEventArgs;
typedef struct UwpIKeyEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIKeyEventArgs);
    HRESULT (UWP_CALL *get_VirtualKey)(UwpIKeyEventArgs *This, int32_t *value);
    HRESULT (UWP_CALL *get_KeyStatus)(UwpIKeyEventArgs *This, UwpCorePhysicalKeyStatus *value);
} UwpIKeyEventArgsVtbl;
struct UwpIKeyEventArgs {
    const UwpIKeyEventArgsVtbl *lpVtbl;
};

typedef struct UwpIKeyEventArgs2 UwpIKeyEventArgs2;
typedef struct UwpIKeyEventArgs2Vtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIKeyEventArgs2);
    HRESULT (UWP_CALL *get_DeviceId)(UwpIKeyEventArgs2 *This, UWP_HSTRING *value);
} UwpIKeyEventArgs2Vtbl;
struct UwpIKeyEventArgs2 {
    const UwpIKeyEventArgs2Vtbl *lpVtbl;
};

typedef struct UwpICharacterReceivedEventArgs UwpICharacterReceivedEventArgs;
typedef struct UwpICharacterReceivedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpICharacterReceivedEventArgs);
    HRESULT (UWP_CALL *get_KeyCode)(UwpICharacterReceivedEventArgs *This, uint32_t *value);
    HRESULT (UWP_CALL *get_KeyStatus)(UwpICharacterReceivedEventArgs *This, UwpCorePhysicalKeyStatus *value);
} UwpICharacterReceivedEventArgsVtbl;
struct UwpICharacterReceivedEventArgs {
    const UwpICharacterReceivedEventArgsVtbl *lpVtbl;
};

typedef struct UwpIAcceleratorKeyEventArgs UwpIAcceleratorKeyEventArgs;
typedef struct UwpIAcceleratorKeyEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIAcceleratorKeyEventArgs);
    HRESULT (UWP_CALL *get_EventType)(UwpIAcceleratorKeyEventArgs *This, UwpCoreAcceleratorKeyEventType *value);
    HRESULT (UWP_CALL *get_VirtualKey)(UwpIAcceleratorKeyEventArgs *This, int32_t *value);
    HRESULT (UWP_CALL *get_KeyStatus)(UwpIAcceleratorKeyEventArgs *This, UwpCorePhysicalKeyStatus *value);
} UwpIAcceleratorKeyEventArgsVtbl;
struct UwpIAcceleratorKeyEventArgs {
    const UwpIAcceleratorKeyEventArgsVtbl *lpVtbl;
};

typedef struct UwpIIdleDispatchedHandlerArgs UwpIIdleDispatchedHandlerArgs;
typedef struct UwpIIdleDispatchedHandlerArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIIdleDispatchedHandlerArgs);
    HRESULT (UWP_CALL *get_IsDispatcherIdle)(UwpIIdleDispatchedHandlerArgs *This, uint8_t *value);
} UwpIIdleDispatchedHandlerArgsVtbl;
struct UwpIIdleDispatchedHandlerArgs {
    const UwpIIdleDispatchedHandlerArgsVtbl *lpVtbl;
};

typedef struct UwpIPointerDevice UwpIPointerDevice;
typedef struct UwpIPointerDeviceVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIPointerDevice);
    HRESULT (UWP_CALL *get_PointerDeviceType)(UwpIPointerDevice *This, UwpPointerDeviceType *value);
    HRESULT (UWP_CALL *get_IsIntegrated)(UwpIPointerDevice *This, uint8_t *value);
    HRESULT (UWP_CALL *get_MaxContacts)(UwpIPointerDevice *This, uint32_t *value);
    HRESULT (UWP_CALL *get_PhysicalDeviceRect)(UwpIPointerDevice *This, UwpRect *value);
    HRESULT (UWP_CALL *get_ScreenRect)(UwpIPointerDevice *This, UwpRect *value);
    HRESULT (UWP_CALL *get_SupportedUsages)(UwpIPointerDevice *This, void **value);
} UwpIPointerDeviceVtbl;
struct UwpIPointerDevice {
    const UwpIPointerDeviceVtbl *lpVtbl;
};

typedef struct UwpIPointerPointProperties UwpIPointerPointProperties;
typedef struct UwpIPointerPointPropertiesVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIPointerPointProperties);
    HRESULT (UWP_CALL *get_Pressure)(UwpIPointerPointProperties *This, float *value);
    HRESULT (UWP_CALL *get_IsInverted)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsEraser)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_Orientation)(UwpIPointerPointProperties *This, float *value);
    HRESULT (UWP_CALL *get_XTilt)(UwpIPointerPointProperties *This, float *value);
    HRESULT (UWP_CALL *get_YTilt)(UwpIPointerPointProperties *This, float *value);
    HRESULT (UWP_CALL *get_Twist)(UwpIPointerPointProperties *This, float *value);
    HRESULT (UWP_CALL *get_ContactRect)(UwpIPointerPointProperties *This, UwpRect *value);
    HRESULT (UWP_CALL *get_ContactRectRaw)(UwpIPointerPointProperties *This, UwpRect *value);
    HRESULT (UWP_CALL *get_TouchConfidence)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsLeftButtonPressed)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsRightButtonPressed)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsMiddleButtonPressed)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_MouseWheelDelta)(UwpIPointerPointProperties *This, int32_t *value);
    HRESULT (UWP_CALL *get_IsHorizontalMouseWheel)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsPrimary)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsInRange)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsCanceled)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsBarrelButtonPressed)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsXButton1Pressed)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_IsXButton2Pressed)(UwpIPointerPointProperties *This, uint8_t *value);
    HRESULT (UWP_CALL *get_PointerUpdateKind)(UwpIPointerPointProperties *This, UwpPointerUpdateKind *value);
    HRESULT (UWP_CALL *HasUsage)(UwpIPointerPointProperties *This, uint32_t usage_page, uint32_t usage_id, uint8_t *value);
    HRESULT (UWP_CALL *GetUsageValue)(UwpIPointerPointProperties *This, uint32_t usage_page, uint32_t usage_id, int32_t *value);
} UwpIPointerPointPropertiesVtbl;
struct UwpIPointerPointProperties {
    const UwpIPointerPointPropertiesVtbl *lpVtbl;
};

typedef struct UwpIPointerPoint UwpIPointerPoint;
typedef struct UwpIPointerPointVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIPointerPoint);
    HRESULT (UWP_CALL *get_PointerDevice)(UwpIPointerPoint *This, UwpIPointerDevice **value);
    HRESULT (UWP_CALL *get_Position)(UwpIPointerPoint *This, UwpPoint *value);
    HRESULT (UWP_CALL *get_RawPosition)(UwpIPointerPoint *This, UwpPoint *value);
    HRESULT (UWP_CALL *get_PointerId)(UwpIPointerPoint *This, uint32_t *value);
    HRESULT (UWP_CALL *get_FrameId)(UwpIPointerPoint *This, uint32_t *value);
    HRESULT (UWP_CALL *get_Timestamp)(UwpIPointerPoint *This, uint64_t *value);
    HRESULT (UWP_CALL *get_IsInContact)(UwpIPointerPoint *This, uint8_t *value);
    HRESULT (UWP_CALL *get_Properties)(UwpIPointerPoint *This, UwpIPointerPointProperties **value);
} UwpIPointerPointVtbl;
struct UwpIPointerPoint {
    const UwpIPointerPointVtbl *lpVtbl;
};

typedef struct UwpIPointerEventArgs UwpIPointerEventArgs;
typedef struct UwpIPointerEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIPointerEventArgs);
    HRESULT (UWP_CALL *get_CurrentPoint)(UwpIPointerEventArgs *This, UwpIPointerPoint **value);
    HRESULT (UWP_CALL *get_KeyModifiers)(UwpIPointerEventArgs *This, UwpVirtualKeyModifiers *value);
    HRESULT (UWP_CALL *GetIntermediatePoints)(UwpIPointerEventArgs *This, void **value);
} UwpIPointerEventArgsVtbl;
struct UwpIPointerEventArgs {
    const UwpIPointerEventArgsVtbl *lpVtbl;
};

typedef struct UwpISplashScreen UwpISplashScreen;
typedef struct UwpISplashScreenVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpISplashScreen);
    HRESULT (UWP_CALL *get_ImageLocation)(UwpISplashScreen *This, UwpRect *value);
    HRESULT (UWP_CALL *add_Dismissed)(UwpISplashScreen *This, UwpTypedEventHandler *handler, UwpEventRegistrationToken *cookie);
    HRESULT (UWP_CALL *remove_Dismissed)(UwpISplashScreen *This, UwpEventRegistrationToken cookie);
} UwpISplashScreenVtbl;
struct UwpISplashScreen {
    const UwpISplashScreenVtbl *lpVtbl;
};

typedef struct UwpIActivatedEventArgs UwpIActivatedEventArgs;
typedef struct UwpIActivatedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIActivatedEventArgs);
    HRESULT (UWP_CALL *get_Kind)(UwpIActivatedEventArgs *This, UwpActivationKind *value);
    HRESULT (UWP_CALL *get_PreviousExecutionState)(UwpIActivatedEventArgs *This, UwpApplicationExecutionState *value);
    HRESULT (UWP_CALL *get_SplashScreen)(UwpIActivatedEventArgs *This, UwpISplashScreen **value);
} UwpIActivatedEventArgsVtbl;
struct UwpIActivatedEventArgs {
    const UwpIActivatedEventArgsVtbl *lpVtbl;
};

typedef struct UwpILaunchActivatedEventArgs UwpILaunchActivatedEventArgs;
typedef struct UwpILaunchActivatedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpILaunchActivatedEventArgs);
    HRESULT (UWP_CALL *get_Arguments)(UwpILaunchActivatedEventArgs *This, UWP_HSTRING *value);
    HRESULT (UWP_CALL *get_TileId)(UwpILaunchActivatedEventArgs *This, UWP_HSTRING *value);
} UwpILaunchActivatedEventArgsVtbl;
struct UwpILaunchActivatedEventArgs {
    const UwpILaunchActivatedEventArgsVtbl *lpVtbl;
};

typedef struct UwpIPrelaunchActivatedEventArgs UwpIPrelaunchActivatedEventArgs;
typedef struct UwpIPrelaunchActivatedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIPrelaunchActivatedEventArgs);
    HRESULT (UWP_CALL *get_PrelaunchActivated)(UwpIPrelaunchActivatedEventArgs *This, uint8_t *value);
} UwpIPrelaunchActivatedEventArgsVtbl;
struct UwpIPrelaunchActivatedEventArgs {
    const UwpIPrelaunchActivatedEventArgsVtbl *lpVtbl;
};

typedef struct UwpIApplicationViewActivatedEventArgs UwpIApplicationViewActivatedEventArgs;
typedef struct UwpIApplicationViewActivatedEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIApplicationViewActivatedEventArgs);
    HRESULT (UWP_CALL *get_CurrentlyShownApplicationViewId)(UwpIApplicationViewActivatedEventArgs *This, int32_t *value);
} UwpIApplicationViewActivatedEventArgsVtbl;
struct UwpIApplicationViewActivatedEventArgs {
    const UwpIApplicationViewActivatedEventArgsVtbl *lpVtbl;
};

typedef struct UwpISuspendingDeferral UwpISuspendingDeferral;
typedef struct UwpISuspendingDeferralVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpISuspendingDeferral);
    HRESULT (UWP_CALL *Complete)(UwpISuspendingDeferral *This);
} UwpISuspendingDeferralVtbl;
struct UwpISuspendingDeferral {
    const UwpISuspendingDeferralVtbl *lpVtbl;
};

typedef struct UwpISuspendingOperation UwpISuspendingOperation;
typedef struct UwpISuspendingOperationVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpISuspendingOperation);
    HRESULT (UWP_CALL *GetDeferral)(UwpISuspendingOperation *This, UwpISuspendingDeferral **deferral);
    HRESULT (UWP_CALL *get_Deadline)(UwpISuspendingOperation *This, UwpDateTime *value);
} UwpISuspendingOperationVtbl;
struct UwpISuspendingOperation {
    const UwpISuspendingOperationVtbl *lpVtbl;
};

typedef struct UwpISuspendingEventArgs UwpISuspendingEventArgs;
typedef struct UwpISuspendingEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpISuspendingEventArgs);
    HRESULT (UWP_CALL *get_SuspendingOperation)(UwpISuspendingEventArgs *This, UwpISuspendingOperation **value);
} UwpISuspendingEventArgsVtbl;
struct UwpISuspendingEventArgs {
    const UwpISuspendingEventArgsVtbl *lpVtbl;
};

typedef struct UwpIDeferral UwpIDeferral;
typedef struct UwpIDeferralVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIDeferral);
    HRESULT (UWP_CALL *Complete)(UwpIDeferral *This);
} UwpIDeferralVtbl;
struct UwpIDeferral {
    const UwpIDeferralVtbl *lpVtbl;
};

typedef struct UwpIBackgroundEventArgs UwpIBackgroundEventArgs;
typedef struct UwpIBackgroundEventArgsVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIBackgroundEventArgs);
    HRESULT (UWP_CALL *GetDeferral)(UwpIBackgroundEventArgs *This, UwpIDeferral **value);
} UwpIBackgroundEventArgsVtbl;
struct UwpIBackgroundEventArgs {
    const UwpIBackgroundEventArgsVtbl *lpVtbl;
};

/* IMap<HSTRING, IInspectable*> + IPropertySet */
typedef struct UwpIMap UwpIMap;
typedef struct UwpIMapVtbl {
    UWP_IINSPECTABLE_VTBL_HEAD(UwpIMap);
    HRESULT (UWP_CALL *Lookup)(UwpIMap *This, UWP_HSTRING key, UwpIInspectable **value);
    HRESULT (UWP_CALL *get_Size)(UwpIMap *This, uint32_t *size);
    HRESULT (UWP_CALL *HasKey)(UwpIMap *This, UWP_HSTRING key, uint8_t *found);
    HRESULT (UWP_CALL *GetView)(UwpIMap *This, void **view);
    HRESULT (UWP_CALL *Insert)(UwpIMap *This, UWP_HSTRING key, UwpIInspectable *value, uint8_t *replaced);
    HRESULT (UWP_CALL *Remove)(UwpIMap *This, UWP_HSTRING key);
    HRESULT (UWP_CALL *Clear)(UwpIMap *This);
} UwpIMapVtbl;
struct UwpIMap {
    const UwpIMapVtbl *lpVtbl;
};

/* ============================================================================
 * Exported UWPHOST.dll C API Declarations
 * ============================================================================ */

HRESULT UWP_API DllGetActivationFactory(UWP_HSTRING activatableClassId, UwpIActivationFactory **factory);
HRESULT UWP_API ExecuteAssembly(LPCWSTR assemblyPath, int argc, LPCWSTR *argv, int *pExitCode);
HRESULT UWP_API UwpHostInitialize(const uwp_host_config_t *cfg);
HRESULT UWP_API UwpHostRun(IUnknown *viewSource);
HRESULT UWP_API UwpHostTryGetActivationFactory(UWP_HSTRING classId, REFIID iid, void **factory);
void    UWP_API UwpHostLogUnknownClass(UWP_HSTRING classId, REFIID iid, void *caller_ret_addr,
                                       uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9);

HRESULT UWP_API RoInitialize(UwpRoInitType initType);
void    UWP_API RoUninitialize(void);
HRESULT UWP_API RoGetActivationFactory(UWP_HSTRING activatableClassId, REFIID iid, void **factory);
HRESULT UWP_API RoActivateInstance(UWP_HSTRING activatableClassId, UwpIInspectable **instance);

HRESULT UWP_API WindowsCreateString(const wchar_t *sourceString, uint32_t length, UWP_HSTRING *string);
HRESULT UWP_API WindowsCreateStringReference(const wchar_t *sourceString, uint32_t length,
                                             UWP_HSTRING_HEADER *hstringHeader, UWP_HSTRING *string);
const wchar_t *UWP_API WindowsGetStringRawBuffer(UWP_HSTRING string, uint32_t *length);
HRESULT UWP_API WindowsDeleteString(UWP_HSTRING string);

/* ============================================================================
 * Internal Sub-Module Interfaces
 * ============================================================================ */

/* Logging & Strict Mode (uwphost_log.c) */
typedef void (*uwp_strict_abort_hook_fn)(const char *reason, scarlett_hresult_t code);
void uwp_log_set_strict_mode(uint32_t enabled);
uint32_t uwp_log_is_strict_mode(void);
void uwp_log_set_strict_abort_hook(uwp_strict_abort_hook_fn hook); /* host testing hook */
void uwp_format_guid(REFIID iid, char *buf, size_t buf_size);
void uwp_wstr_to_utf8(const wchar_t *src, char *dst, size_t dst_size);
void uwp_utf8_to_wstr(const char *src, wchar_t *dst, size_t dst_chars);
int  uwp_get_env_utf8(const char *name, char *buf, size_t buf_size);
int  uwp_get_env_wstr(const wchar_t *name, wchar_t *buf, size_t buf_chars);

/* HSTRING helpers (uwphost_winstring.c) */
HRESULT uwp_hstring_create_from_wstr(const wchar_t *str, UWP_HSTRING *out);
void    uwp_winstring_init(void);

/* AppxManifest parser & Package Identity (uwphost_manifest.c) */
void uwp_manifest_compute_publisher_id(const wchar_t *publisher, wchar_t out_pub_id[16]);
int  uwp_manifest_parse_buffer(const uint8_t *data, size_t len,
                               const wchar_t *install_dir,
                               uwp_package_identity_t *out);
int  uwp_manifest_load_file(const wchar_t *path, uwp_package_identity_t *out);
int  uwp_manifest_initialize(const wchar_t *explicit_manifest_path);
void uwp_manifest_apply_env_overrides(uwp_package_identity_t *id);
int  uwp_manifest_find_inproc_class(const wchar_t *class_id, uwp_inproc_class_entry_t *out);
void uwp_manifest_set_identity_for_test(const uwp_package_identity_t *id);
void uwp_manifest_normalize_path(const wchar_t *src, wchar_t *dst, size_t dst_chars);

/* Event Registration List & EventArgs Factories (uwphost_events.c) */
#define UWP_MAX_EVENT_LISTENERS 32

typedef struct uwp_event_entry_s {
    int64_t token;
    UwpTypedEventHandler *handler;
} uwp_event_entry_t;

typedef struct uwp_event_list_s {
    uwp_mutex_t lock;
    int64_t next_token;
    uwp_event_entry_t entries[UWP_MAX_EVENT_LISTENERS];
} uwp_event_list_t;

void    uwp_event_list_init(uwp_event_list_t *list);
void    uwp_event_list_destroy(uwp_event_list_t *list);
HRESULT uwp_event_list_add(uwp_event_list_t *list, UwpTypedEventHandler *handler, UwpEventRegistrationToken *out_token);
HRESULT uwp_event_list_remove(uwp_event_list_t *list, UwpEventRegistrationToken token);
void    uwp_event_list_invoke(uwp_event_list_t *list, void *sender, void *args);
size_t  uwp_event_list_count(uwp_event_list_t *list);
HRESULT uwp_alloc_iids(const GUID *src, ULONG count, ULONG *out_count, IID **out_iids);

typedef struct uwp_pointer_event_params_s {
    uint32_t pointer_id;
    uint32_t frame_id;
    uint64_t timestamp_us;
    float    x;
    float    y;
    uint8_t  in_contact;
    uint8_t  left_button;
    uint8_t  right_button;
    uint8_t  middle_button;
    uint8_t  xbutton1;
    uint8_t  xbutton2;
    uint8_t  horizontal_wheel;
    int32_t  wheel_delta;
    UwpPointerUpdateKind update_kind;
    UwpVirtualKeyModifiers key_modifiers;
} uwp_pointer_event_params_t;

UwpIWindowSizeChangedEventArgs *uwp_create_size_changed_args(float width, float height);
UwpIWindowActivatedEventArgs   *uwp_create_activated_args(UwpCoreWindowActivationState state);
UwpIVisibilityChangedEventArgs *uwp_create_visibility_changed_args(uint8_t visible);
UwpICoreWindowEventArgs        *uwp_create_core_window_event_args(void);
UwpIKeyEventArgs               *uwp_create_key_event_args(int32_t virtual_key, const UwpCorePhysicalKeyStatus *status);
UwpICharacterReceivedEventArgs *uwp_create_char_received_args(uint32_t key_code, const UwpCorePhysicalKeyStatus *status);
UwpIAcceleratorKeyEventArgs    *uwp_create_accel_key_args(UwpCoreAcceleratorKeyEventType type, int32_t vkey,
                                                          const UwpCorePhysicalKeyStatus *status);
UwpIPointerEventArgs           *uwp_create_pointer_event_args(const uwp_pointer_event_params_t *p);
UwpIIdleDispatchedHandlerArgs  *uwp_create_idle_dispatched_args(uint8_t is_idle);
UwpIActivatedEventArgs         *uwp_create_launch_activated_args(const wchar_t *arguments, const wchar_t *tile_id);
UwpISuspendingEventArgs        *uwp_create_suspending_args(void);
UwpIBackgroundEventArgs        *uwp_create_background_event_args(int is_entering);
UwpIMap                        *uwp_create_property_set(void);

typedef struct UwpAsyncActionObj UwpAsyncActionObj;
UwpAsyncActionObj *uwp_async_action_create(void);
UwpIAsyncAction   *uwp_async_action_iface(UwpAsyncActionObj *obj);
void               uwp_async_action_complete(UwpAsyncActionObj *obj, HRESULT result_hr);
void               uwp_async_action_release(UwpAsyncActionObj *obj);

/* CoreWindow & CoreDispatcher (uwphost_corewindow.c) */
typedef struct UwpCoreWindowObj UwpCoreWindowObj;
typedef struct UwpCoreDispatcherObj UwpCoreDispatcherObj;

void uwp_corewindow_subsystem_init(void);
void uwp_corewindow_subsystem_shutdown(void);
void uwp_corewindow_set_default_size(uint32_t width, uint32_t height);

UwpCoreWindowObj *uwp_corewindow_create(uint32_t width, uint32_t height, const wchar_t *title, int headless);
void              uwp_corewindow_bind_current_thread(UwpCoreWindowObj *win);
UwpCoreWindowObj *uwp_corewindow_get_for_current_thread(void);
UwpCoreWindowObj *uwp_corewindow_get_main(void);
UwpICoreWindow   *uwp_corewindow_iface(UwpCoreWindowObj *win);
UwpICoreDispatcher *uwp_corewindow_dispatcher_iface(UwpCoreWindowObj *win);
void              uwp_corewindow_request_close(UwpCoreWindowObj *win);
int               uwp_corewindow_is_closed(UwpCoreWindowObj *win);
int               uwp_corewindow_handle_message(UwpCoreWindowObj *win, uint32_t msg, WPARAM wParam, LPARAM lParam);
uint32_t          uwp_corewindow_drain_dispatched_work(UwpCoreWindowObj *win, uint32_t max_items);
HRESULT           uwp_corewindow_get_activation_factory(REFIID iid, void **out_factory);
HRESULT           uwp_coredispatcher_get_activation_factory(REFIID iid, void **out_factory);

/* CoreApplication & CoreApplicationView (uwphost_coreapp.c) */
void    uwp_coreapp_subsystem_init(void);
void    uwp_coreapp_subsystem_shutdown(void);
void    uwp_coreapp_set_headless(int headless); /* for host unit tests */
HRESULT uwp_coreapp_get_activation_factory(REFIID iid, void **out_factory);
HRESULT uwp_coreapp_run_view_source(UwpIFrameworkViewSource *view_source);
void    uwp_coreapp_notify_suspending(void);
void    uwp_coreapp_notify_resuming(void);
void    uwp_coreapp_notify_entered_background(void);
void    uwp_coreapp_notify_leaving_background(void);
void    uwp_coreapp_trigger_view_activated(const wchar_t *arguments);
HRESULT uwp_coreapp_try_custom_factory(UWP_HSTRING class_id, REFIID iid, void **out_factory);

/* Main initialization helper (uwphost_main.c) */
void uwp_host_ensure_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* SCARLETT_UWPHOST_INTERNAL_H */
