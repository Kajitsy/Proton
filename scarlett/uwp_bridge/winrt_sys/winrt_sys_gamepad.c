/*
 * Proton-Scarlett — UWP Bridge System WinRT: Windows.Gaming.Input.Gamepad
 *
 * Implements:
 *   - IGamepadStatics (GamepadAdded, GamepadRemoved, Gamepads -> IVectorView<Gamepad*>)
 *   - IGamepadStatics2 (FromGameController)
 *   - IGamepad (Vibration get/put, GetCurrentReading)
 *   - IGamepad2 (GetButtonLabel)
 *   - IGameController (HeadsetConnected/Disconnected/UserChanged, Headset, IsWireless, User)
 *   - IVectorView<Gamepad*>, IIterable<Gamepad*>, IIterator<Gamepad*>
 *
 * Bridges dynamically via LoadLibraryA/GetProcAddress to:
 *   1. Proton-Scarlett GameInput.dll (IGameInput / IGameInputReading / IGameInputDevice)
 *   2. Fallback xinput1_4.dll (XInputGetState / XInputSetState)
 *
 * Clean-room PE64/Win32 C11 (zero POSIX/Linux calls).
 */

#include "winrt_sys_internal.h"

#include <string.h>

/* ============================================================================
 * WinRT Gaming.Input Vtable Definitions
 * ============================================================================ */

typedef struct WinrtGamepadStaticsVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_GamepadAdded)(void *This, void *value, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_GamepadAdded)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *add_GamepadRemoved)(void *This, void *value, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_GamepadRemoved)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *get_Gamepads)(void *This, void **value);
} WinrtGamepadStaticsVtbl;

typedef struct WinrtGamepadStatics2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *FromGameController)(void *This, void *controller, void **value);
} WinrtGamepadStatics2Vtbl;

typedef struct WinrtGamepadVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Vibration)(void *This, WinrtGamepadVibration *value);
    HRESULT (STDMETHODCALLTYPE *put_Vibration)(void *This, WinrtGamepadVibration value);
    HRESULT (STDMETHODCALLTYPE *GetCurrentReading)(void *This, WinrtGamepadReading *value);
} WinrtGamepadVtbl;

typedef struct WinrtGamepad2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetButtonLabel)(void *This, uint32_t button, uint32_t *value);
} WinrtGamepad2Vtbl;

typedef struct WinrtGameControllerVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *add_HeadsetConnected)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_HeadsetConnected)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *add_HeadsetDisconnected)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_HeadsetDisconnected)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *add_UserChanged)(void *This, void *handler, EventRegistrationToken *token);
    HRESULT (STDMETHODCALLTYPE *remove_UserChanged)(void *This, EventRegistrationToken token);
    HRESULT (STDMETHODCALLTYPE *get_Headset)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_IsWireless)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *get_User)(void *This, void **value);
} WinrtGameControllerVtbl;

typedef struct WinrtGamepadVectorViewVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *GetAt)(void *This, uint32_t index, void **value);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *This, uint32_t *value);
    HRESULT (STDMETHODCALLTYPE *IndexOf)(void *This, void *element, uint32_t *index, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t startIndex, uint32_t itemsSize, void **items, uint32_t *value);
} WinrtGamepadVectorViewVtbl;

typedef struct WinrtGamepadIterableVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *First)(void *This, void **first);
} WinrtGamepadIterableVtbl;

typedef struct WinrtGamepadIteratorVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **ppvObject);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *This);
    ULONG   (STDMETHODCALLTYPE *Release)(void *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *This, ULONG *iidCount, IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *This, HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *This, TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_Current)(void *This, void **value);
    HRESULT (STDMETHODCALLTYPE *get_HasCurrent)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *MoveNext)(void *This, boolean *value);
    HRESULT (STDMETHODCALLTYPE *GetMany)(void *This, uint32_t itemsSize, void **items, uint32_t *value);
} WinrtGamepadIteratorVtbl;

/* ============================================================================
 * Dynamic GameInput.dll & xinput1_4.dll ABI Contracts
 * ============================================================================ */

#define GI_KIND_GAMEPAD 0x00000008u

typedef struct GiRumbleParams {
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
} GiRumbleParams;

typedef struct GiGamepadState {
    uint32_t buttons;
    float    leftTrigger;
    float    rightTrigger;
    float    leftThumbstickX;
    float    leftThumbstickY;
    float    rightThumbstickX;
    float    rightThumbstickY;
    uint32_t extendedButtons;
} GiGamepadState;

typedef struct GiDeviceVtbl {
    HRESULT  (WINAPI *QueryInterface)(void *self, REFIID riid, void **ppv);
    uint32_t (WINAPI *AddRef)(void *self);
    uint32_t (WINAPI *Release)(void *self);
    HRESULT  (WINAPI *GetDeviceInfo)(void *self, const void **info);
    uint32_t (WINAPI *GetDeviceStatus)(void *self);
    void     (WINAPI *SetRumbleState)(void *self, const GiRumbleParams *params);
    HRESULT  (WINAPI *SetHapticMotorState)(void *self, uint32_t motorIndex, const void *params);
} GiDeviceVtbl;

typedef struct GiDeviceIf {
    const GiDeviceVtbl *lpVtbl;
} GiDeviceIf;

typedef struct GiReadingVtbl {
    HRESULT  (WINAPI *QueryInterface)(void *self, REFIID riid, void **ppv);
    uint32_t (WINAPI *AddRef)(void *self);
    uint32_t (WINAPI *Release)(void *self);
    uint32_t (WINAPI *GetInputKind)(void *self);
    uint64_t (WINAPI *GetSequenceNumber)(void *self, uint32_t inputKind);
    uint64_t (WINAPI *GetTimestamp)(void *self);
    void     (WINAPI *GetDevice)(void *self, void **device);
    uint32_t (WINAPI *GetGamepadState)(void *self, GiGamepadState *state);
} GiReadingVtbl;

typedef struct GiReadingIf {
    const GiReadingVtbl *lpVtbl;
} GiReadingIf;

typedef struct GiVtbl {
    HRESULT  (WINAPI *QueryInterface)(void *self, REFIID riid, void **ppv);
    uint32_t (WINAPI *AddRef)(void *self);
    uint32_t (WINAPI *Release)(void *self);
    uint64_t (WINAPI *GetCurrentTimestamp)(void *self);
    HRESULT  (WINAPI *GetCurrentReading)(void *self, uint32_t inputKind, void *device, void **reading);
    HRESULT  (WINAPI *GetNextReading)(void *self, void *refReading, uint32_t inputKind, void *device, void **reading);
    HRESULT  (WINAPI *RegisterDeviceCallback)(void *self, void *dev, uint32_t kind, uint32_t status, uint32_t enumK, void *ctx, void *cb, uint64_t *tok);
    HRESULT  (WINAPI *GetPreviousReading)(void *self, void *refReading, uint32_t inputKind, void *device, void **reading);
} GiVtbl;

typedef struct GiIf {
    const GiVtbl *lpVtbl;
} GiIf;

typedef HRESULT (WINAPI *pfn_gameinput_create_t)(void **gameInput);

typedef struct XiGamepad {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} XiGamepad;

typedef struct XiState {
    DWORD     dwPacketNumber;
    XiGamepad Gamepad;
} XiState;

typedef struct XiVibration {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XiVibration;

typedef DWORD (WINAPI *pfn_xinput_get_state_t)(DWORD dwUserIndex, XiState *pState);
typedef DWORD (WINAPI *pfn_xinput_set_state_t)(DWORD dwUserIndex, XiVibration *pVibration);

/* ============================================================================
 * Backend Loader & Conversion Helpers
 * ============================================================================ */

static CRITICAL_SECTION g_gp_lock;
static LONG g_gp_lock_init = 0;

static HMODULE g_gi_mod = NULL;
static int     g_gi_probed = 0;
static GiIf   *g_gi_instance = NULL;

static HMODULE g_xi_mod = NULL;
static int     g_xi_probed = 0;
static pfn_xinput_get_state_t g_xi_get_state = NULL;
static pfn_xinput_set_state_t g_xi_set_state = NULL;

static void ensure_gp_lock(void) {
    if (InterlockedCompareExchange(&g_gp_lock_init, 1, 0) == 0) {
        InitializeCriticalSection(&g_gp_lock);
    }
}

static void probe_backends_locked(void) {
    if (!g_gi_probed) {
        g_gi_probed = 1;
        g_gi_mod = LoadLibraryA("GameInput.dll");
        if (g_gi_mod != NULL) {
            pfn_gameinput_create_t create_fn =
                (pfn_gameinput_create_t)(void *)GetProcAddress(g_gi_mod, "GameInputCreate");
            if (create_fn != NULL) {
                void *gi = NULL;
                if (SUCCEEDED(create_fn(&gi)) && gi != NULL) {
                    g_gi_instance = (GiIf *)gi;
                }
            }
        }
    }

    if (!g_xi_probed) {
        g_xi_probed = 1;
        g_xi_mod = LoadLibraryA("xinput1_4.dll");
        if (g_xi_mod != NULL) {
            g_xi_get_state =
                (pfn_xinput_get_state_t)(void *)GetProcAddress(g_xi_mod, "XInputGetState");
            g_xi_set_state =
                (pfn_xinput_set_state_t)(void *)GetProcAddress(g_xi_mod, "XInputSetState");
        }
    }
}

static double clamp_unit(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static double clamp_axis(double v) {
    if (v < -1.0) return -1.0;
    if (v > 1.0) return 1.0;
    return v;
}

static double norm_thumb(int16_t raw) {
    if (raw >= 0) {
        return clamp_axis((double)raw / 32767.0);
    }
    return clamp_axis((double)raw / 32768.0);
}

static uint32_t convert_xinput_buttons(WORD w) {
    uint32_t out = 0u;
    if (w & 0x0001u) out |= 0x40u;   /* DPadUp */
    if (w & 0x0002u) out |= 0x80u;   /* DPadDown */
    if (w & 0x0004u) out |= 0x100u;  /* DPadLeft */
    if (w & 0x0008u) out |= 0x200u;  /* DPadRight */
    if (w & 0x0010u) out |= 0x1u;    /* Start -> Menu */
    if (w & 0x0020u) out |= 0x2u;    /* Back -> View */
    if (w & 0x0040u) out |= 0x1000u; /* LeftThumbstick */
    if (w & 0x0080u) out |= 0x2000u; /* RightThumbstick */
    if (w & 0x0100u) out |= 0x400u;  /* LeftShoulder */
    if (w & 0x0200u) out |= 0x800u;  /* RightShoulder */
    if (w & 0x1000u) out |= 0x4u;    /* A */
    if (w & 0x2000u) out |= 0x8u;    /* B */
    if (w & 0x4000u) out |= 0x10u;   /* X */
    if (w & 0x8000u) out |= 0x20u;   /* Y */
    return out;
}

static void convert_gi_to_winrt(const GiGamepadState *gs, uint64_t ts, WinrtGamepadReading *out) {
    out->Timestamp = ts;
    /* GameInputGamepadButtons bit values match Windows.Gaming.Input.GamepadButtons 1:1 */
    out->Buttons = gs->buttons & 0x00003FFFu;
    out->LeftTrigger = clamp_unit((double)gs->leftTrigger);
    out->RightTrigger = clamp_unit((double)gs->rightTrigger);
    out->LeftThumbstickX = clamp_axis((double)gs->leftThumbstickX);
    out->LeftThumbstickY = clamp_axis((double)gs->leftThumbstickY);
    out->RightThumbstickX = clamp_axis((double)gs->rightThumbstickX);
    out->RightThumbstickY = clamp_axis((double)gs->rightThumbstickY);
}

static void convert_xinput_to_winrt(const XiState *xs, WinrtGamepadReading *out) {
    out->Timestamp = (uint64_t)GetTickCount64() * 1000ULL;
    out->Buttons = convert_xinput_buttons(xs->Gamepad.wButtons);
    out->LeftTrigger = clamp_unit((double)xs->Gamepad.bLeftTrigger / 255.0);
    out->RightTrigger = clamp_unit((double)xs->Gamepad.bRightTrigger / 255.0);
    out->LeftThumbstickX = norm_thumb(xs->Gamepad.sThumbLX);
    out->LeftThumbstickY = norm_thumb(xs->Gamepad.sThumbLY);
    out->RightThumbstickX = norm_thumb(xs->Gamepad.sThumbRX);
    out->RightThumbstickY = norm_thumb(xs->Gamepad.sThumbRY);
}

/* ============================================================================
 * Gamepad Object
 * ============================================================================ */

typedef enum GamepadBackendKind {
    GP_BACKEND_NONE = 0,
    GP_BACKEND_GAMEINPUT = 1,
    GP_BACKEND_XINPUT = 2
} GamepadBackendKind;

typedef struct GamepadEventSlot {
    EventRegistrationToken token;
    IUnknown *handler;
} GamepadEventSlot;

typedef struct WinrtGamepadObj {
    const WinrtGamepadVtbl        *lpVtbl;
    const WinrtGamepad2Vtbl       *lpVtbl2;
    const WinrtGameControllerVtbl *lpVtblCtrl;
    volatile LONG ref_count;
    CRITICAL_SECTION lock;
    GamepadBackendKind backend;
    uint32_t index;
    int connected;
    GiDeviceIf *gi_device;
    WinrtGamepadVibration vibration;
    WinrtGamepadReading last_reading;
    int64_t next_token;
    uint32_t hc_cnt, hd_cnt, uc_cnt;
    GamepadEventSlot hc_list[8];
    GamepadEventSlot hd_list[8];
    GamepadEventSlot uc_list[8];
} WinrtGamepadObj;

#define MAX_WINRT_GAMEPADS 4u

static WinrtGamepadObj *g_active_pads[MAX_WINRT_GAMEPADS];
static GamepadEventSlot g_added_listeners[16];
static uint32_t g_added_count = 0;
static GamepadEventSlot g_removed_listeners[16];
static uint32_t g_removed_count = 0;
static int64_t g_stat_next_token = 0;

static HRESULT STDMETHODCALLTYPE gp_qi(void *This, REFIID riid, void **ppv) {
    WinrtGamepadObj *obj = (WinrtGamepadObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IGamepad)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IGamepad2)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl2;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IGameController)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblCtrl;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE gp_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtGamepadObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE gp_release(void *This) {
    WinrtGamepadObj *obj = (WinrtGamepadObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        if (obj->gi_device) obj->gi_device->lpVtbl->Release(obj->gi_device);
        for (uint32_t i = 0; i < obj->hc_cnt; ++i) {
            if (obj->hc_list[i].handler) obj->hc_list[i].handler->lpVtbl->Release(obj->hc_list[i].handler);
        }
        for (uint32_t i = 0; i < obj->hd_cnt; ++i) {
            if (obj->hd_list[i].handler) obj->hd_list[i].handler->lpVtbl->Release(obj->hd_list[i].handler);
        }
        for (uint32_t i = 0; i < obj->uc_cnt; ++i) {
            if (obj->uc_list[i].handler) obj->uc_list[i].handler->lpVtbl->Release(obj->uc_list[i].handler);
        }
        DeleteCriticalSection(&obj->lock);
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE gp_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0xbc7bb43cu, 0x0a69u, 0x3903u, { 0x9eu, 0x9du, 0xa5u, 0x0fu, 0x86u, 0xa4u, 0x5du, 0xe5u } },
        { 0x3c1689bdu, 0x5915u, 0x4245u, { 0xb0u, 0xc0u, 0xc8u, 0x9fu, 0xaeu, 0x03u, 0x08u, 0xffu } },
        { 0x1baf6522u, 0x5f64u, 0x42c5u, { 0x82u, 0x67u, 0xb9u, 0xfeu, 0x22u, 0x15u, 0xbfu, 0xbdu } }
    };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE gp_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Gaming.Input.Gamepad", n);
}
static HRESULT STDMETHODCALLTYPE gp_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gp_get_vibration(void *This, WinrtGamepadVibration *val) {
    WinrtGamepadObj *obj = (WinrtGamepadObj *)This;
    if (!val) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    *val = obj->vibration;
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gp_put_vibration(void *This, WinrtGamepadVibration val) {
    WinrtGamepadObj *obj = (WinrtGamepadObj *)This;
    val.LeftMotor = clamp_unit(val.LeftMotor);
    val.RightMotor = clamp_unit(val.RightMotor);
    val.LeftTrigger = clamp_unit(val.LeftTrigger);
    val.RightTrigger = clamp_unit(val.RightTrigger);

    EnterCriticalSection(&obj->lock);
    obj->vibration = val;
    GamepadBackendKind bk = obj->backend;
    uint32_t idx = obj->index;
    GiDeviceIf *gi_dev = obj->gi_device;
    if (gi_dev) gi_dev->lpVtbl->AddRef(gi_dev);
    LeaveCriticalSection(&obj->lock);

    if (bk == GP_BACKEND_GAMEINPUT) {
        if (!gi_dev) {
            ensure_gp_lock();
            EnterCriticalSection(&g_gp_lock);
            if (g_gi_instance != NULL) {
                void *reading_ptr = NULL;
                if (SUCCEEDED(g_gi_instance->lpVtbl->GetCurrentReading(
                        g_gi_instance, GI_KIND_GAMEPAD, NULL, &reading_ptr)) &&
                    reading_ptr != NULL) {
                    GiReadingIf *rd = (GiReadingIf *)reading_ptr;
                    void *dev_ptr = NULL;
                    rd->lpVtbl->GetDevice(rd, &dev_ptr);
                    gi_dev = (GiDeviceIf *)dev_ptr;
                    rd->lpVtbl->Release(rd);
                }
            }
            LeaveCriticalSection(&g_gp_lock);
        }
        if (gi_dev != NULL) {
            GiRumbleParams rp;
            rp.lowFrequency = (float)val.LeftMotor;
            rp.highFrequency = (float)val.RightMotor;
            rp.leftTrigger = (float)val.LeftTrigger;
            rp.rightTrigger = (float)val.RightTrigger;
            gi_dev->lpVtbl->SetRumbleState(gi_dev, &rp);
            gi_dev->lpVtbl->Release(gi_dev);
            return S_OK;
        }
    } else if (bk == GP_BACKEND_XINPUT) {
        ensure_gp_lock();
        EnterCriticalSection(&g_gp_lock);
        probe_backends_locked();
        pfn_xinput_set_state_t set_fn = g_xi_set_state;
        LeaveCriticalSection(&g_gp_lock);
        if (set_fn != NULL) {
            XiVibration xv;
            xv.wLeftMotorSpeed = (WORD)(val.LeftMotor * 65535.0 + 0.5);
            xv.wRightMotorSpeed = (WORD)(val.RightMotor * 65535.0 + 0.5);
            set_fn(idx, &xv);
        }
    }
    if (gi_dev) gi_dev->lpVtbl->Release(gi_dev);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gp_get_reading(void *This, WinrtGamepadReading *val) {
    WinrtGamepadObj *obj = (WinrtGamepadObj *)This;
    if (!val) return E_POINTER;
    memset(val, 0, sizeof(*val));

    EnterCriticalSection(&obj->lock);
    GamepadBackendKind bk = obj->backend;
    uint32_t idx = obj->index;
    GiDeviceIf *gi_dev = obj->gi_device;
    if (gi_dev) gi_dev->lpVtbl->AddRef(gi_dev);
    LeaveCriticalSection(&obj->lock);

    if (bk == GP_BACKEND_GAMEINPUT) {
        ensure_gp_lock();
        EnterCriticalSection(&g_gp_lock);
        probe_backends_locked();
        GiIf *gi = g_gi_instance;
        if (gi) gi->lpVtbl->AddRef(gi);
        LeaveCriticalSection(&g_gp_lock);

        if (gi != NULL) {
            void *reading_ptr = NULL;
            HRESULT hr = gi->lpVtbl->GetCurrentReading(gi, GI_KIND_GAMEPAD, gi_dev, &reading_ptr);
            if (FAILED(hr) && gi_dev != NULL) {
                hr = gi->lpVtbl->GetCurrentReading(gi, GI_KIND_GAMEPAD, NULL, &reading_ptr);
            }
            if (SUCCEEDED(hr) && reading_ptr != NULL) {
                GiReadingIf *rd = (GiReadingIf *)reading_ptr;
                GiGamepadState gs;
                memset(&gs, 0, sizeof(gs));
                if (rd->lpVtbl->GetGamepadState(rd, &gs) != 0u) {
                    uint64_t ts = rd->lpVtbl->GetTimestamp(rd);
                    convert_gi_to_winrt(&gs, ts, val);
                    EnterCriticalSection(&obj->lock);
                    obj->last_reading = *val;
                    if (!obj->gi_device) {
                        void *dp = NULL;
                        rd->lpVtbl->GetDevice(rd, &dp);
                        obj->gi_device = (GiDeviceIf *)dp;
                    }
                    LeaveCriticalSection(&obj->lock);
                    rd->lpVtbl->Release(rd);
                    gi->lpVtbl->Release(gi);
                    if (gi_dev) gi_dev->lpVtbl->Release(gi_dev);
                    return S_OK;
                }
                rd->lpVtbl->Release(rd);
            }
            gi->lpVtbl->Release(gi);
        }
    } else if (bk == GP_BACKEND_XINPUT) {
        ensure_gp_lock();
        EnterCriticalSection(&g_gp_lock);
        probe_backends_locked();
        pfn_xinput_get_state_t get_fn = g_xi_get_state;
        LeaveCriticalSection(&g_gp_lock);

        if (get_fn != NULL) {
            XiState xs;
            memset(&xs, 0, sizeof(xs));
            DWORD res = get_fn(idx, &xs);
            if (res == ERROR_SUCCESS) {
                convert_xinput_to_winrt(&xs, val);
                EnterCriticalSection(&obj->lock);
                obj->last_reading = *val;
                LeaveCriticalSection(&obj->lock);
                if (gi_dev) gi_dev->lpVtbl->Release(gi_dev);
                return S_OK;
            }
        }
    }

    if (gi_dev) gi_dev->lpVtbl->Release(gi_dev);
    EnterCriticalSection(&obj->lock);
    *val = obj->last_reading;
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gp2_qi(void *This, REFIID r, void **p) {
    return gp_qi(WINRT_SYS_FROM(WinrtGamepadObj, lpVtbl2, This), r, p);
}
static ULONG STDMETHODCALLTYPE gp2_addref(void *This) {
    return gp_addref(WINRT_SYS_FROM(WinrtGamepadObj, lpVtbl2, This));
}
static ULONG STDMETHODCALLTYPE gp2_release(void *This) {
    return gp_release(WINRT_SYS_FROM(WinrtGamepadObj, lpVtbl2, This));
}
static HRESULT STDMETHODCALLTYPE gp2_getiids(void *This, ULONG *c, IID **i) {
    return gp_getiids(WINRT_SYS_FROM(WinrtGamepadObj, lpVtbl2, This), c, i);
}
static HRESULT STDMETHODCALLTYPE gp2_rcn(void *This, HSTRING *n) {
    return gp_rcn(WINRT_SYS_FROM(WinrtGamepadObj, lpVtbl2, This), n);
}
static HRESULT STDMETHODCALLTYPE gp2_tl(void *This, TrustLevel *tl) {
    return gp_tl(WINRT_SYS_FROM(WinrtGamepadObj, lpVtbl2, This), tl);
}
static HRESULT STDMETHODCALLTYPE gp2_get_label(void *This, uint32_t btn, uint32_t *val) {
    (void)This;
    if (!val) return E_POINTER;
    switch (btn) {
    case 0x1u:    *val = 3u;  break; /* Menu -> XboxMenu */
    case 0x2u:    *val = 4u;  break; /* View -> XboxView */
    case 0x4u:    *val = 9u;  break; /* A -> XboxA */
    case 0x8u:    *val = 10u; break; /* B -> XboxB */
    case 0x10u:   *val = 11u; break; /* X -> XboxX */
    case 0x20u:   *val = 12u; break; /* Y -> XboxY */
    case 0x40u:   *val = 5u;  break; /* DPadUp -> XboxUp */
    case 0x80u:   *val = 6u;  break; /* DPadDown -> XboxDown */
    case 0x100u:  *val = 7u;  break; /* DPadLeft -> XboxLeft */
    case 0x200u:  *val = 8u;  break; /* DPadRight -> XboxRight */
    case 0x400u:  *val = 13u; break; /* LeftShoulder -> XboxLeftBumper */
    case 0x800u:  *val = 16u; break; /* RightShoulder -> XboxRightBumper */
    case 0x1000u: *val = 15u; break; /* LeftThumbstick -> XboxLeftStickButton */
    case 0x2000u: *val = 18u; break; /* RightThumbstick -> XboxRightStickButton */
    case 0x4000u: *val = 19u; break; /* Paddle1 -> XboxPaddle1 */
    case 0x8000u: *val = 20u; break; /* Paddle2 -> XboxPaddle2 */
    case 0x10000u:*val = 21u; break; /* Paddle3 -> XboxPaddle3 */
    case 0x20000u:*val = 22u; break; /* Paddle4 -> XboxPaddle4 */
    default:      *val = 0u;  break;
    }
    return S_OK;
}

static HRESULT gp_ctrl_add(WinrtGamepadObj *obj, GamepadEventSlot *arr, uint32_t *cnt, void *hnd, EventRegistrationToken *tok) {
    if (!hnd || !tok) return E_POINTER;
    EnterCriticalSection(&obj->lock);
    if (*cnt >= 8u) { LeaveCriticalSection(&obj->lock); return E_OUTOFMEMORY; }
    IUnknown *h = (IUnknown *)hnd;
    h->lpVtbl->AddRef(h);
    tok->value = ++obj->next_token;
    arr[*cnt].token = *tok;
    arr[*cnt].handler = h;
    (*cnt)++;
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}
static HRESULT gp_ctrl_rem(WinrtGamepadObj *obj, GamepadEventSlot *arr, uint32_t *cnt, EventRegistrationToken tok) {
    EnterCriticalSection(&obj->lock);
    for (uint32_t i = 0; i < *cnt; ++i) {
        if (arr[i].token.value == tok.value) {
            if (arr[i].handler) arr[i].handler->lpVtbl->Release(arr[i].handler);
            for (uint32_t j = i + 1u; j < *cnt; ++j) arr[j - 1u] = arr[j];
            (*cnt)--;
            break;
        }
    }
    LeaveCriticalSection(&obj->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gpc_qi(void *This, REFIID r, void **p) {
    return gp_qi(WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This), r, p);
}
static ULONG STDMETHODCALLTYPE gpc_addref(void *This) {
    return gp_addref(WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This));
}
static ULONG STDMETHODCALLTYPE gpc_release(void *This) {
    return gp_release(WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This));
}
static HRESULT STDMETHODCALLTYPE gpc_getiids(void *This, ULONG *c, IID **i) {
    return gp_getiids(WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This), c, i);
}
static HRESULT STDMETHODCALLTYPE gpc_rcn(void *This, HSTRING *n) {
    return gp_rcn(WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This), n);
}
static HRESULT STDMETHODCALLTYPE gpc_tl(void *This, TrustLevel *tl) {
    return gp_tl(WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This), tl);
}
static HRESULT STDMETHODCALLTYPE gpc_add_hc(void *This, void *h, EventRegistrationToken *t) {
    WinrtGamepadObj *o = WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This);
    return gp_ctrl_add(o, o->hc_list, &o->hc_cnt, h, t);
}
static HRESULT STDMETHODCALLTYPE gpc_rem_hc(void *This, EventRegistrationToken t) {
    WinrtGamepadObj *o = WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This);
    return gp_ctrl_rem(o, o->hc_list, &o->hc_cnt, t);
}
static HRESULT STDMETHODCALLTYPE gpc_add_hd(void *This, void *h, EventRegistrationToken *t) {
    WinrtGamepadObj *o = WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This);
    return gp_ctrl_add(o, o->hd_list, &o->hd_cnt, h, t);
}
static HRESULT STDMETHODCALLTYPE gpc_rem_hd(void *This, EventRegistrationToken t) {
    WinrtGamepadObj *o = WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This);
    return gp_ctrl_rem(o, o->hd_list, &o->hd_cnt, t);
}
static HRESULT STDMETHODCALLTYPE gpc_add_uc(void *This, void *h, EventRegistrationToken *t) {
    WinrtGamepadObj *o = WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This);
    return gp_ctrl_add(o, o->uc_list, &o->uc_cnt, h, t);
}
static HRESULT STDMETHODCALLTYPE gpc_rem_uc(void *This, EventRegistrationToken t) {
    WinrtGamepadObj *o = WINRT_SYS_FROM(WinrtGamepadObj, lpVtblCtrl, This);
    return gp_ctrl_rem(o, o->uc_list, &o->uc_cnt, t);
}
static HRESULT STDMETHODCALLTYPE gpc_get_headset(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpc_get_wireless(void *This, boolean *val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpc_get_user(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    return S_OK;
}

static const WinrtGamepadVtbl g_gp_vtbl = {
    gp_qi, gp_addref, gp_release, gp_getiids, gp_rcn, gp_tl,
    gp_get_vibration, gp_put_vibration, gp_get_reading
};
static const WinrtGamepad2Vtbl g_gp2_vtbl = {
    gp2_qi, gp2_addref, gp2_release, gp2_getiids, gp2_rcn, gp2_tl, gp2_get_label
};
static const WinrtGameControllerVtbl g_gpc_vtbl = {
    gpc_qi, gpc_addref, gpc_release, gpc_getiids, gpc_rcn, gpc_tl,
    gpc_add_hc, gpc_rem_hc, gpc_add_hd, gpc_rem_hd, gpc_add_uc, gpc_rem_uc,
    gpc_get_headset, gpc_get_wireless, gpc_get_user
};

static WinrtGamepadObj *create_gamepad_obj(GamepadBackendKind bk, uint32_t idx, GiDeviceIf *gi_dev) {
    WinrtGamepadObj *obj = (WinrtGamepadObj *)winrt_sys_alloc(sizeof(*obj));
    if (!obj) return NULL;
    obj->lpVtbl = &g_gp_vtbl;
    obj->lpVtbl2 = &g_gp2_vtbl;
    obj->lpVtblCtrl = &g_gpc_vtbl;
    obj->ref_count = 1;
    obj->backend = bk;
    obj->index = idx;
    obj->connected = 1;
    obj->gi_device = gi_dev;
    if (gi_dev) gi_dev->lpVtbl->AddRef(gi_dev);
    InitializeCriticalSection(&obj->lock);
    return obj;
}

/* ============================================================================
 * IVectorView<Gamepad*> / IIterable<Gamepad*> / IIterator<Gamepad*>
 * ============================================================================ */

typedef struct WinrtGamepadIterObj {
    const WinrtGamepadIteratorVtbl *lpVtbl;
    volatile LONG ref_count;
    uint32_t count;
    uint32_t index;
    WinrtGamepadObj *items[MAX_WINRT_GAMEPADS];
} WinrtGamepadIterObj;

static HRESULT STDMETHODCALLTYPE gpit_qi(void *This, REFIID riid, void **ppv) {
    WinrtGamepadIterObj *obj = (WinrtGamepadIterObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IIterator_Gamepad)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = obj;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE gpit_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtGamepadIterObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE gpit_release(void *This) {
    WinrtGamepadIterObj *obj = (WinrtGamepadIterObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < obj->count; ++i) {
            if (obj->items[i]) gp_release(&obj->items[i]->lpVtbl);
        }
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE gpit_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    return winrt_sys_get_iids_copy(&WINRT_IID_IIterator_Gamepad, 1, c, i);
}
static HRESULT STDMETHODCALLTYPE gpit_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IIterator`1<Windows.Gaming.Input.Gamepad>", n);
}
static HRESULT STDMETHODCALLTYPE gpit_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpit_cur(void *This, void **val) {
    WinrtGamepadIterObj *obj = (WinrtGamepadIterObj *)This;
    if (!val) return E_POINTER;
    *val = NULL;
    if (obj->index >= obj->count) return E_BOUNDS;
    gp_addref(&obj->items[obj->index]->lpVtbl);
    *val = &obj->items[obj->index]->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpit_has(void *This, boolean *val) {
    WinrtGamepadIterObj *obj = (WinrtGamepadIterObj *)This;
    if (!val) return E_POINTER;
    *val = (obj->index < obj->count) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpit_next(void *This, boolean *val) {
    WinrtGamepadIterObj *obj = (WinrtGamepadIterObj *)This;
    if (!val) return E_POINTER;
    if (obj->index < obj->count) obj->index++;
    *val = (obj->index < obj->count) ? 1 : 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpit_many(void *This, uint32_t cap, void **out, uint32_t *actual) {
    WinrtGamepadIterObj *obj = (WinrtGamepadIterObj *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (obj->index >= obj->count || cap == 0 || !out) return S_OK;
    uint32_t n = obj->count - obj->index;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; ++i) {
        gp_addref(&obj->items[obj->index + i]->lpVtbl);
        out[i] = &obj->items[obj->index + i]->lpVtbl;
    }
    obj->index += n;
    *actual = n;
    return S_OK;
}
static const WinrtGamepadIteratorVtbl g_gpit_vtbl = {
    gpit_qi, gpit_addref, gpit_release, gpit_getiids, gpit_rcn, gpit_tl,
    gpit_cur, gpit_has, gpit_next, gpit_many
};

typedef struct WinrtGamepadViewObj {
    const WinrtGamepadVectorViewVtbl *lpVtbl;
    const WinrtGamepadIterableVtbl   *lpVtblIter;
    volatile LONG ref_count;
    uint32_t count;
    WinrtGamepadObj *items[MAX_WINRT_GAMEPADS];
} WinrtGamepadViewObj;

static HRESULT STDMETHODCALLTYPE gpvw_qi(void *This, REFIID riid, void **ppv) {
    WinrtGamepadViewObj *obj = (WinrtGamepadViewObj *)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IVectorView_Gamepad)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtbl;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IIterable_Gamepad)) {
        InterlockedIncrement(&obj->ref_count);
        *ppv = &obj->lpVtblIter;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE gpvw_addref(void *This) {
    return (ULONG)InterlockedIncrement(&((WinrtGamepadViewObj *)This)->ref_count);
}
static ULONG STDMETHODCALLTYPE gpvw_release(void *This) {
    WinrtGamepadViewObj *obj = (WinrtGamepadViewObj *)This;
    LONG rc = InterlockedDecrement(&obj->ref_count);
    if (rc == 0) {
        for (uint32_t i = 0; i < obj->count; ++i) {
            if (obj->items[i]) gp_release(&obj->items[i]->lpVtbl);
        }
        winrt_sys_free(obj);
    }
    return (ULONG)rc;
}
static HRESULT STDMETHODCALLTYPE gpvw_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0xeb97bb69u, 0x09c9u, 0x5a99u, { 0x86u, 0xb2u, 0x3eu, 0x36u, 0x08u, 0x52u, 0x84u, 0xd4u } },
        { 0x47132ba0u, 0x6b17u, 0x5cd2u, { 0xa8u, 0xbdu, 0xb5u, 0xd3u, 0x44u, 0x3cu, 0xcbu, 0x13u } }
    };
    return winrt_sys_get_iids_copy(list, 2, c, i);
}
static HRESULT STDMETHODCALLTYPE gpvw_rcn(void *This, HSTRING *n) {
    (void)This;
    return winrt_sys_make_hstring(L"Windows.Foundation.Collections.IVectorView`1<Windows.Gaming.Input.Gamepad>", n);
}
static HRESULT STDMETHODCALLTYPE gpvw_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpvw_getat(void *This, uint32_t idx, void **val) {
    WinrtGamepadViewObj *obj = (WinrtGamepadViewObj *)This;
    if (!val) return E_POINTER;
    *val = NULL;
    if (idx >= obj->count) return E_BOUNDS;
    gp_addref(&obj->items[idx]->lpVtbl);
    *val = &obj->items[idx]->lpVtbl;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpvw_size(void *This, uint32_t *val) {
    WinrtGamepadViewObj *obj = (WinrtGamepadViewObj *)This;
    if (!val) return E_POINTER;
    *val = obj->count;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpvw_indexof(void *This, void *elem, uint32_t *idx, boolean *found) {
    WinrtGamepadViewObj *obj = (WinrtGamepadViewObj *)This;
    if (!idx || !found) return E_POINTER;
    *idx = 0; *found = 0;
    for (uint32_t i = 0; i < obj->count; ++i) {
        if ((void *)&obj->items[i]->lpVtbl == elem) {
            *idx = i;
            *found = 1;
            break;
        }
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpvw_getmany(void *This, uint32_t start, uint32_t cap, void **out, uint32_t *actual) {
    WinrtGamepadViewObj *obj = (WinrtGamepadViewObj *)This;
    if (!actual) return E_POINTER;
    *actual = 0;
    if (start >= obj->count || cap == 0 || !out) return S_OK;
    uint32_t n = obj->count - start;
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; ++i) {
        gp_addref(&obj->items[start + i]->lpVtbl);
        out[i] = &obj->items[start + i]->lpVtbl;
    }
    *actual = n;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gpvw_iter_qi(void *This, REFIID r, void **p) {
    return gpvw_qi(WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This), r, p);
}
static ULONG STDMETHODCALLTYPE gpvw_iter_addref(void *This) {
    return gpvw_addref(WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This));
}
static ULONG STDMETHODCALLTYPE gpvw_iter_release(void *This) {
    return gpvw_release(WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This));
}
static HRESULT STDMETHODCALLTYPE gpvw_iter_getiids(void *This, ULONG *c, IID **i) {
    return gpvw_getiids(WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This), c, i);
}
static HRESULT STDMETHODCALLTYPE gpvw_iter_rcn(void *This, HSTRING *n) {
    return gpvw_rcn(WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This), n);
}
static HRESULT STDMETHODCALLTYPE gpvw_iter_tl(void *This, TrustLevel *tl) {
    return gpvw_tl(WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This), tl);
}
static HRESULT STDMETHODCALLTYPE gpvw_first(void *This, void **first) {
    WinrtGamepadViewObj *obj = WINRT_SYS_FROM(WinrtGamepadViewObj, lpVtblIter, This);
    if (!first) return E_POINTER;
    WinrtGamepadIterObj *it = (WinrtGamepadIterObj *)winrt_sys_alloc(sizeof(*it));
    if (!it) return E_OUTOFMEMORY;
    it->lpVtbl = &g_gpit_vtbl;
    it->ref_count = 1;
    it->count = obj->count;
    for (uint32_t i = 0; i < obj->count; ++i) {
        it->items[i] = obj->items[i];
        gp_addref(&it->items[i]->lpVtbl);
    }
    *first = it;
    return S_OK;
}

static const WinrtGamepadVectorViewVtbl g_gpvw_vtbl = {
    gpvw_qi, gpvw_addref, gpvw_release, gpvw_getiids, gpvw_rcn, gpvw_tl,
    gpvw_getat, gpvw_size, gpvw_indexof, gpvw_getmany
};
static const WinrtGamepadIterableVtbl g_gpvw_iter_vtbl = {
    gpvw_iter_qi, gpvw_iter_addref, gpvw_iter_release,
    gpvw_iter_getiids, gpvw_iter_rcn, gpvw_iter_tl, gpvw_first
};

/* ============================================================================
 * Hotplug Refresh & Gamepad Statics Factory
 * ============================================================================ */

static void refresh_and_collect_gamepads(WinrtGamepadViewObj *view_out) {
    ensure_gp_lock();

    WinrtGamepadObj *added[MAX_WINRT_GAMEPADS];
    uint32_t added_n = 0;
    WinrtGamepadObj *removed[MAX_WINRT_GAMEPADS];
    uint32_t removed_n = 0;

    IUnknown *added_handlers[16];
    uint32_t added_h_n = 0;
    IUnknown *removed_handlers[16];
    uint32_t removed_h_n = 0;

    EnterCriticalSection(&g_gp_lock);
    probe_backends_locked();

    GamepadBackendKind desired_bk[MAX_WINRT_GAMEPADS];
    GiDeviceIf *desired_dev[MAX_WINRT_GAMEPADS];
    for (uint32_t i = 0; i < MAX_WINRT_GAMEPADS; ++i) {
        desired_bk[i] = GP_BACKEND_NONE;
        desired_dev[i] = NULL;
    }

    int gi_has_pad0 = 0;
    if (g_gi_instance != NULL) {
        void *reading_ptr = NULL;
        if (SUCCEEDED(g_gi_instance->lpVtbl->GetCurrentReading(
                g_gi_instance, GI_KIND_GAMEPAD, NULL, &reading_ptr)) &&
            reading_ptr != NULL) {
            GiReadingIf *rd = (GiReadingIf *)reading_ptr;
            GiGamepadState gs;
            memset(&gs, 0, sizeof(gs));
            if (rd->lpVtbl->GetGamepadState(rd, &gs) != 0u) {
                void *dev_ptr = NULL;
                rd->lpVtbl->GetDevice(rd, &dev_ptr);
                GiDeviceIf *dev = (GiDeviceIf *)dev_ptr;
                uint32_t st = dev ? dev->lpVtbl->GetDeviceStatus(dev) : 1u;
                if ((st & 1u) != 0u) {
                    gi_has_pad0 = 1;
                    desired_bk[0] = GP_BACKEND_GAMEINPUT;
                    desired_dev[0] = dev; /* already AddRef'd by GetDevice if non-NULL */
                    dev = NULL;
                }
                if (dev) dev->lpVtbl->Release(dev);
            }
            rd->lpVtbl->Release(rd);
        }
    }

    if (g_xi_get_state != NULL) {
        for (uint32_t i = 0; i < MAX_WINRT_GAMEPADS; ++i) {
            if (i == 0 && gi_has_pad0) continue;
            XiState xs;
            memset(&xs, 0, sizeof(xs));
            if (g_xi_get_state(i, &xs) == ERROR_SUCCESS) {
                desired_bk[i] = GP_BACKEND_XINPUT;
            }
        }
    }

    for (uint32_t i = 0; i < MAX_WINRT_GAMEPADS; ++i) {
        WinrtGamepadObj *cur = g_active_pads[i];
        if (desired_bk[i] != GP_BACKEND_NONE) {
            if (cur == NULL || cur->backend != desired_bk[i]) {
                if (cur != NULL) {
                    removed[removed_n++] = cur; /* transfer ownership */
                    g_active_pads[i] = NULL;
                }
                WinrtGamepadObj *np = create_gamepad_obj(desired_bk[i], i, desired_dev[i]);
                g_active_pads[i] = np;
                if (np != NULL) {
                    gp_addref(&np->lpVtbl);
                    added[added_n++] = np;
                }
            }
        } else if (cur != NULL) {
            removed[removed_n++] = cur; /* transfer ownership */
            g_active_pads[i] = NULL;
        }
        if (desired_dev[i]) desired_dev[i]->lpVtbl->Release(desired_dev[i]);
    }

    if (view_out != NULL) {
        view_out->count = 0;
        for (uint32_t i = 0; i < MAX_WINRT_GAMEPADS; ++i) {
            if (g_active_pads[i] != NULL) {
                gp_addref(&g_active_pads[i]->lpVtbl);
                view_out->items[view_out->count++] = g_active_pads[i];
            }
        }
    }

    if (added_n > 0) {
        for (uint32_t i = 0; i < g_added_count && i < 16u; ++i) {
            if (g_added_listeners[i].handler) {
                g_added_listeners[i].handler->lpVtbl->AddRef(g_added_listeners[i].handler);
                added_handlers[added_h_n++] = g_added_listeners[i].handler;
            }
        }
    }
    if (removed_n > 0) {
        for (uint32_t i = 0; i < g_removed_count && i < 16u; ++i) {
            if (g_removed_listeners[i].handler) {
                g_removed_listeners[i].handler->lpVtbl->AddRef(g_removed_listeners[i].handler);
                removed_handlers[removed_h_n++] = g_removed_listeners[i].handler;
            }
        }
    }

    LeaveCriticalSection(&g_gp_lock);

    for (uint32_t p = 0; p < removed_n; ++p) {
        for (uint32_t h = 0; h < removed_h_n; ++h) {
            typedef struct { const WinrtEventHandlerPtrVtbl *lpVtbl; } HIf;
            ((HIf *)removed_handlers[h])->lpVtbl->Invoke(removed_handlers[h], NULL, &removed[p]->lpVtbl);
        }
        gp_release(&removed[p]->lpVtbl);
    }
    for (uint32_t h = 0; h < removed_h_n; ++h) {
        removed_handlers[h]->lpVtbl->Release(removed_handlers[h]);
    }

    for (uint32_t p = 0; p < added_n; ++p) {
        for (uint32_t h = 0; h < added_h_n; ++h) {
            typedef struct { const WinrtEventHandlerPtrVtbl *lpVtbl; } HIf;
            ((HIf *)added_handlers[h])->lpVtbl->Invoke(added_handlers[h], NULL, &added[p]->lpVtbl);
        }
        gp_release(&added[p]->lpVtbl);
    }
    for (uint32_t h = 0; h < added_h_n; ++h) {
        added_handlers[h]->lpVtbl->Release(added_handlers[h]);
    }
}

typedef struct WinrtGamepadFactory {
    const WinrtActivationFactoryVtbl *lpVtblAct;
    const WinrtGamepadStaticsVtbl    *lpVtblStatics;
    const WinrtGamepadStatics2Vtbl   *lpVtblStatics2;
} WinrtGamepadFactory;

static WinrtGamepadFactory g_gp_factory;

static HRESULT STDMETHODCALLTYPE gp_fac_qi(void *This, REFIID riid, void **ppv) {
    (void)This;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (!riid) return E_INVALIDARG;
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IUnknown) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IInspectable) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IAgileObject) ||
        winrt_sys_guid_eq(riid, &WINRT_IID_IActivationFactory)) {
        *ppv = &g_gp_factory.lpVtblAct;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IGamepadStatics)) {
        *ppv = &g_gp_factory.lpVtblStatics;
        return S_OK;
    }
    if (winrt_sys_guid_eq(riid, &WINRT_IID_IGamepadStatics2)) {
        *ppv = &g_gp_factory.lpVtblStatics2;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE gp_fac_addref(void *This) { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE gp_fac_release(void *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE gp_fac_getiids(void *This, ULONG *c, IID **i) {
    (void)This;
    static const GUID list[] = {
        { 0x00000035u, 0x0000u, 0x0000u, { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } },
        { 0x8bbce529u, 0xd49cu, 0x39e9u, { 0x95u, 0x60u, 0xe4u, 0x7du, 0xdeu, 0x96u, 0xb7u, 0xc8u } },
        { 0x42676dc5u, 0x0856u, 0x47c4u, { 0x92u, 0x13u, 0xb3u, 0x95u, 0x50u, 0x4cu, 0x3au, 0x3cu } }
    };
    return winrt_sys_get_iids_copy(list, 3, c, i);
}
static HRESULT STDMETHODCALLTYPE gp_fac_rcn(void *This, HSTRING *n) {
    (void)This;
    if (n) *n = NULL;
    return E_ILLEGAL_METHOD_CALL;
}
static HRESULT STDMETHODCALLTYPE gp_fac_tl(void *This, TrustLevel *tl) {
    (void)This;
    if (!tl) return E_POINTER;
    *tl = BaseTrust;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE gp_fac_act(void *This, IInspectable **inst) {
    if (inst) *inst = NULL;
    return winrt_sys_unimplemented("Gamepad::ActivateInstance", This);
}

static HRESULT gp_stat_add_ev(GamepadEventSlot *arr, uint32_t *cnt, void *val, EventRegistrationToken *tok) {
    if (!val || !tok) return E_POINTER;
    ensure_gp_lock();
    EnterCriticalSection(&g_gp_lock);
    if (*cnt >= 16u) { LeaveCriticalSection(&g_gp_lock); return E_OUTOFMEMORY; }
    IUnknown *h = (IUnknown *)val;
    h->lpVtbl->AddRef(h);
    tok->value = ++g_stat_next_token;
    arr[*cnt].token = *tok;
    arr[*cnt].handler = h;
    (*cnt)++;
    LeaveCriticalSection(&g_gp_lock);
    return S_OK;
}

static HRESULT gp_stat_rem_ev(GamepadEventSlot *arr, uint32_t *cnt, EventRegistrationToken tok) {
    ensure_gp_lock();
    EnterCriticalSection(&g_gp_lock);
    for (uint32_t i = 0; i < *cnt; ++i) {
        if (arr[i].token.value == tok.value) {
            if (arr[i].handler) arr[i].handler->lpVtbl->Release(arr[i].handler);
            for (uint32_t j = i + 1u; j < *cnt; ++j) arr[j - 1u] = arr[j];
            (*cnt)--;
            break;
        }
    }
    LeaveCriticalSection(&g_gp_lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gp_stat_add_added(void *This, void *val, EventRegistrationToken *tok) {
    (void)This;
    return gp_stat_add_ev(g_added_listeners, &g_added_count, val, tok);
}
static HRESULT STDMETHODCALLTYPE gp_stat_rem_added(void *This, EventRegistrationToken tok) {
    (void)This;
    return gp_stat_rem_ev(g_added_listeners, &g_added_count, tok);
}
static HRESULT STDMETHODCALLTYPE gp_stat_add_removed(void *This, void *val, EventRegistrationToken *tok) {
    (void)This;
    return gp_stat_add_ev(g_removed_listeners, &g_removed_count, val, tok);
}
static HRESULT STDMETHODCALLTYPE gp_stat_rem_removed(void *This, EventRegistrationToken tok) {
    (void)This;
    return gp_stat_rem_ev(g_removed_listeners, &g_removed_count, tok);
}
static HRESULT STDMETHODCALLTYPE gp_stat_get_gamepads(void *This, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    WinrtGamepadViewObj *vw = (WinrtGamepadViewObj *)winrt_sys_alloc(sizeof(*vw));
    if (!vw) return E_OUTOFMEMORY;
    vw->lpVtbl = &g_gpvw_vtbl;
    vw->lpVtblIter = &g_gpvw_iter_vtbl;
    vw->ref_count = 1;
    refresh_and_collect_gamepads(vw);
    *val = &vw->lpVtbl;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE gp_stat2_from_ctrl(void *This, void *controller, void **val) {
    (void)This;
    if (!val) return E_POINTER;
    *val = NULL;
    if (!controller) return S_OK;
    IUnknown *unk = (IUnknown *)controller;
    HRESULT hr = unk->lpVtbl->QueryInterface(unk, &WINRT_IID_IGamepad, val);
    if (hr == E_NOINTERFACE) {
        *val = NULL;
        return S_OK;
    }
    return hr;
}

static const WinrtActivationFactoryVtbl g_gp_act_vtbl = {
    gp_fac_qi, gp_fac_addref, gp_fac_release, gp_fac_getiids, gp_fac_rcn, gp_fac_tl, gp_fac_act
};
static const WinrtGamepadStaticsVtbl g_gp_stat_vtbl = {
    gp_fac_qi, gp_fac_addref, gp_fac_release, gp_fac_getiids, gp_fac_rcn, gp_fac_tl,
    gp_stat_add_added, gp_stat_rem_added, gp_stat_add_removed, gp_stat_rem_removed, gp_stat_get_gamepads
};
static const WinrtGamepadStatics2Vtbl g_gp_stat2_vtbl = {
    gp_fac_qi, gp_fac_addref, gp_fac_release, gp_fac_getiids, gp_fac_rcn, gp_fac_tl,
    gp_stat2_from_ctrl
};
static WinrtGamepadFactory g_gp_factory = { &g_gp_act_vtbl, &g_gp_stat_vtbl, &g_gp_stat2_vtbl };

HRESULT winrt_sys_get_gamepad_factory(REFIID iid, void **out_factory) {
    return gp_fac_qi(&g_gp_factory.lpVtblAct, iid, out_factory);
}

#ifndef _WIN32
/* Host unit test helper to reset cached backend DLL state between test scenarios */
void winrt_sys_gamepad_test_reset(void) {
    ensure_gp_lock();
    EnterCriticalSection(&g_gp_lock);
    for (uint32_t i = 0; i < MAX_WINRT_GAMEPADS; ++i) {
        if (g_active_pads[i]) {
            gp_release(&g_active_pads[i]->lpVtbl);
            g_active_pads[i] = NULL;
        }
    }
    if (g_gi_instance) {
        g_gi_instance->lpVtbl->Release(g_gi_instance);
        g_gi_instance = NULL;
    }
    g_gi_mod = NULL;
    g_gi_probed = 0;
    g_xi_mod = NULL;
    g_xi_probed = 0;
    g_xi_get_state = NULL;
    g_xi_set_state = NULL;
    for (uint32_t i = 0; i < g_added_count; ++i) {
        if (g_added_listeners[i].handler) g_added_listeners[i].handler->lpVtbl->Release(g_added_listeners[i].handler);
    }
    g_added_count = 0;
    for (uint32_t i = 0; i < g_removed_count; ++i) {
        if (g_removed_listeners[i].handler) g_removed_listeners[i].handler->lpVtbl->Release(g_removed_listeners[i].handler);
    }
    g_removed_count = 0;
    LeaveCriticalSection(&g_gp_lock);
}
#endif
