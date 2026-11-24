/*
 * Proton-Scarlett — GameInput.dll PE64 Freestanding Shim (Ordinal #1 GameInputCreate)
 *
 * Parity v4.0:
 *   - IGameInput: GetCurrentTimestamp, GetCurrentReading, GetNextReading,
 *                 GetPreviousReading, RegisterDeviceCallback (auto-stub)
 *   - IGameInputReading: GetInputKind, GetSequenceNumber, GetTimestamp,
 *                        GetDevice, GetGamepadState
 *   - IGameInputDevice: GetDeviceInfo, GetDeviceStatus, SetRumbleState (4-motor),
 *                       SetHapticMotorState (auto-stub)
 *   - GameInputCreateDispatcher (@2 -> diagnostic auto-stub)
 */

#include "audio_pe_common.h"

typedef struct scarlett_gi_rumble_params_s {
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
} scarlett_gi_rumble_params_t;

typedef struct scarlett_gi_gamepad_state_s {
    uint32_t buttons;
    float leftTrigger;
    float rightTrigger;
    float leftThumbstickX;
    float leftThumbstickY;
    float rightThumbstickX;
    float rightThumbstickY;
    uint32_t extendedButtons;
} scarlett_gi_gamepad_state_t;

typedef struct scarlett_gi_key_state_s {
    uint32_t scanCode;
    uint32_t codePoint;
    uint8_t  virtualKey;
    uint8_t  isDeadKey;
    uint8_t  reserved[2];
} scarlett_gi_key_state_t;

typedef struct scarlett_gi_mouse_state_s {
    uint32_t buttons;
    uint32_t reserved;
    int64_t  positionX;
    int64_t  positionY;
    int64_t  wheelX;
    int64_t  wheelY;
} scarlett_gi_mouse_state_t;

typedef struct scarlett_gi_ui_nav_state_s {
    uint32_t buttons;
} scarlett_gi_ui_nav_state_t;

typedef struct scarlett_gi_device_info_s {
    uint32_t infoSize;
    uint16_t vendorId;
    uint16_t productId;
    uint16_t revisionNumber;
    uint8_t  interfaceNumber;
    uint8_t  collectionNumber;
    uint32_t usage;
    uint64_t hardwareVersion;
    uint64_t firmwareVersion;
    uint8_t  deviceId[32];
    uint8_t  deviceRootId[32];
    uint32_t deviceFamily;
    uint32_t capabilities;
    uint32_t supportedInput;
    uint32_t supportedRumbleMotors;
    uint32_t inputReportCount;
    uint32_t outputReportCount;
    uint32_t featureReportCount;
    uint32_t controllerAxisCount;
    uint32_t controllerButtonCount;
    uint32_t controllerSwitchCount;
    uint32_t keyboardInfoPtrPad[2];
    uint32_t mouseInfoPtrPad[2];
    uint8_t  reserved[112];
} scarlett_gi_device_info_t;

typedef struct scarlett_gi_device_obj_s  scarlett_gi_device_obj_t;
typedef struct scarlett_gi_reading_obj_s scarlett_gi_reading_obj_t;
typedef struct scarlett_gi_obj_s         scarlett_gi_obj_t;

typedef struct scarlett_gi_device_vtbl_s {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(scarlett_gi_device_obj_t *self,
                                                            const scarlett_guid_t *riid,
                                                            void **ppvObject);
    uint32_t (SCARLETT_WIN64_API *AddRef)(scarlett_gi_device_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *Release)(scarlett_gi_device_obj_t *self);
    const void * (SCARLETT_WIN64_API *GetDeviceInfo)(scarlett_gi_device_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetDeviceStatus)(scarlett_gi_device_obj_t *self);
    void (SCARLETT_WIN64_API *GetBatteryState)(scarlett_gi_device_obj_t *self, void *state);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateForceFeedbackEffect)(scarlett_gi_device_obj_t *self,
                                                                       uint32_t motorIndex,
                                                                       const void *params,
                                                                       void **effect);
    uint32_t (SCARLETT_WIN64_API *IsForceFeedbackMotorPoweredOn)(scarlett_gi_device_obj_t *self,
                                                                    uint32_t motorIndex);
    void (SCARLETT_WIN64_API *SetForceFeedbackMotorGain)(scarlett_gi_device_obj_t *self,
                                                         uint32_t motorIndex,
                                                         float masterGain);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetHapticMotorState)(scarlett_gi_device_obj_t *self,
                                                                 uint32_t motorIndex,
                                                                 const void *params);
    void (SCARLETT_WIN64_API *SetRumbleState)(scarlett_gi_device_obj_t *self,
                                              const scarlett_gi_rumble_params_t *params);
    void (SCARLETT_WIN64_API *SetInputSynchronizationState)(scarlett_gi_device_obj_t *self,
                                                            uint32_t enabled);
    void (SCARLETT_WIN64_API *SendInputSynchronizationHint)(scarlett_gi_device_obj_t *self);
    void (SCARLETT_WIN64_API *PowerOff)(scarlett_gi_device_obj_t *self);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateRawDeviceReport)(scarlett_gi_device_obj_t *self,
                                                                   uint32_t reportId,
                                                                   int32_t reportKind,
                                                                   void **report);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetRawDeviceFeature)(scarlett_gi_device_obj_t *self,
                                                                 uint32_t reportId,
                                                                 void **report);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetRawDeviceFeature)(scarlett_gi_device_obj_t *self,
                                                                 void *report);
    scarlett_hresult_t (SCARLETT_WIN64_API *SendRawDeviceOutput)(scarlett_gi_device_obj_t *self,
                                                                 void *report);
    scarlett_hresult_t (SCARLETT_WIN64_API *SendRawDeviceOutputWithResponse)(scarlett_gi_device_obj_t *self,
                                                                             void *requestReport,
                                                                             void **responseReport);
    scarlett_hresult_t (SCARLETT_WIN64_API *ExecuteRawDeviceIoControl)(scarlett_gi_device_obj_t *self,
                                                                       uint32_t controlCode,
                                                                       size_t inputBufferSize,
                                                                       const void *inputBuffer,
                                                                       size_t outputBufferSize,
                                                                       void *outputBuffer,
                                                                       size_t *outputSize);
    uint32_t (SCARLETT_WIN64_API *AcquireExclusiveRawDeviceAccess)(scarlett_gi_device_obj_t *self,
                                                                     uint64_t timeoutInMicroseconds);
    void (SCARLETT_WIN64_API *ReleaseExclusiveRawDeviceAccess)(scarlett_gi_device_obj_t *self);
} scarlett_gi_device_vtbl_t;

struct scarlett_gi_device_obj_s {
    const scarlett_gi_device_vtbl_t *lpVtbl;
    uint64_t device_id;
    uint32_t ref_count;
    uint32_t in_use;
    scarlett_gi_device_info_t info;
};

typedef struct scarlett_gi_reading_vtbl_s {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(scarlett_gi_reading_obj_t *self,
                                                            const scarlett_guid_t *riid,
                                                            void **ppvObject);
    uint32_t (SCARLETT_WIN64_API *AddRef)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *Release)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetInputKind)(scarlett_gi_reading_obj_t *self);
    uint64_t (SCARLETT_WIN64_API *GetSequenceNumber)(scarlett_gi_reading_obj_t *self,
                                                     uint32_t inputKind);
    uint64_t (SCARLETT_WIN64_API *GetTimestamp)(scarlett_gi_reading_obj_t *self);
    void (SCARLETT_WIN64_API *GetDevice)(scarlett_gi_reading_obj_t *self, void **device);
    uint32_t (SCARLETT_WIN64_API *GetRawReport)(scarlett_gi_reading_obj_t *self, void **report);
    uint32_t (SCARLETT_WIN64_API *GetControllerAxisCount)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetControllerAxisState)(scarlett_gi_reading_obj_t *self,
                                                          uint32_t stateArrayCount,
                                                          float *stateArray);
    uint32_t (SCARLETT_WIN64_API *GetControllerButtonCount)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetControllerButtonState)(scarlett_gi_reading_obj_t *self,
                                                            uint32_t stateArrayCount,
                                                            uint8_t *stateArray);
    uint32_t (SCARLETT_WIN64_API *GetControllerSwitchCount)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetControllerSwitchState)(scarlett_gi_reading_obj_t *self,
                                                            uint32_t stateArrayCount,
                                                            uint32_t *stateArray);
    uint32_t (SCARLETT_WIN64_API *GetKeyCount)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetKeyState)(scarlett_gi_reading_obj_t *self,
                                               uint32_t stateArrayCount,
                                               scarlett_gi_key_state_t *stateArray);
    uint32_t (SCARLETT_WIN64_API *GetMouseState)(scarlett_gi_reading_obj_t *self,
                                                 scarlett_gi_mouse_state_t *state);
    uint32_t (SCARLETT_WIN64_API *GetTouchCount)(scarlett_gi_reading_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *GetTouchState)(scarlett_gi_reading_obj_t *self,
                                                 uint32_t stateArrayCount,
                                                 void *stateArray);
    uint32_t (SCARLETT_WIN64_API *GetMotionState)(scarlett_gi_reading_obj_t *self, void *state);
    uint32_t (SCARLETT_WIN64_API *GetArcadeStickState)(scarlett_gi_reading_obj_t *self, void *state);
    uint32_t (SCARLETT_WIN64_API *GetFlightStickState)(scarlett_gi_reading_obj_t *self, void *state);
    uint32_t (SCARLETT_WIN64_API *GetGamepadState)(scarlett_gi_reading_obj_t *self,
                                                   scarlett_gi_gamepad_state_t *state);
    uint32_t (SCARLETT_WIN64_API *GetRacingWheelState)(scarlett_gi_reading_obj_t *self, void *state);
    uint32_t (SCARLETT_WIN64_API *GetUiNavigationState)(scarlett_gi_reading_obj_t *self,
                                                        scarlett_gi_ui_nav_state_t *state);
} scarlett_gi_reading_vtbl_t;

struct scarlett_gi_reading_obj_s {
    const scarlett_gi_reading_vtbl_t *lpVtbl;
    uint64_t instance_handle;
    uint32_t ref_count;
    uint32_t in_use;
    scarlett_input_reading_record_t record;
};

typedef struct scarlett_gi_vtbl_s {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(scarlett_gi_obj_t *self,
                                                            const scarlett_guid_t *riid,
                                                            void **ppvObject);
    uint32_t (SCARLETT_WIN64_API *AddRef)(scarlett_gi_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *Release)(scarlett_gi_obj_t *self);
    uint64_t (SCARLETT_WIN64_API *GetCurrentTimestamp)(scarlett_gi_obj_t *self);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetCurrentReading)(scarlett_gi_obj_t *self,
                                                               uint32_t inputKind,
                                                               void *device,
                                                               void **reading);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetNextReading)(scarlett_gi_obj_t *self,
                                                            void *referenceReading,
                                                            uint32_t inputKind,
                                                            void *device,
                                                            void **reading);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetPreviousReading)(scarlett_gi_obj_t *self,
                                                                void *referenceReading,
                                                                uint32_t inputKind,
                                                                void *device,
                                                                void **reading);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetTemporalReading)(scarlett_gi_obj_t *self,
                                                                uint64_t timestamp,
                                                                void *device,
                                                                void **reading);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterReadingCallback)(scarlett_gi_obj_t *self,
                                                                     void *device,
                                                                     uint32_t inputKind,
                                                                     float analogThreshold,
                                                                     void *context,
                                                                     void *callbackFunc,
                                                                     uint64_t *callbackToken);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterDeviceCallback)(scarlett_gi_obj_t *self,
                                                                    void *device,
                                                                    uint32_t inputKind,
                                                                    uint32_t statusFilter,
                                                                    uint32_t enumerationKind,
                                                                    void *context,
                                                                    void *callbackFunc,
                                                                    uint64_t *callbackToken);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterSystemButtonCallback)(scarlett_gi_obj_t *self,
                                                                          void *device,
                                                                          uint32_t buttonFilter,
                                                                          void *context,
                                                                          void *callbackFunc,
                                                                          uint64_t *callbackToken);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterKeyboardLayoutCallback)(scarlett_gi_obj_t *self,
                                                                            void *device,
                                                                            void *context,
                                                                            void *callbackFunc,
                                                                            uint64_t *callbackToken);
    void (SCARLETT_WIN64_API *StopCallback)(scarlett_gi_obj_t *self,
                                            uint64_t callbackToken);
    uint32_t (SCARLETT_WIN64_API *UnregisterCallback)(scarlett_gi_obj_t *self,
                                                      uint64_t callbackToken,
                                                      uint64_t timeoutInMicroseconds);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateDispatcher)(scarlett_gi_obj_t *self,
                                                              void **dispatcher);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateAggregateDevice)(scarlett_gi_obj_t *self,
                                                                   uint32_t inputKind,
                                                                   void **device);
    scarlett_hresult_t (SCARLETT_WIN64_API *FindDeviceFromId)(scarlett_gi_obj_t *self,
                                                              const void *deviceId,
                                                              void **device);
    scarlett_hresult_t (SCARLETT_WIN64_API *FindDeviceFromObject)(scarlett_gi_obj_t *self,
                                                                  void *value,
                                                                  void **device);
    scarlett_hresult_t (SCARLETT_WIN64_API *FindDeviceFromPlatformHandle)(scarlett_gi_obj_t *self,
                                                                          void *value,
                                                                          void **device);
    scarlett_hresult_t (SCARLETT_WIN64_API *FindDeviceFromPlatformString)(scarlett_gi_obj_t *self,
                                                                          const void *value,
                                                                          void **device);
    scarlett_hresult_t (SCARLETT_WIN64_API *EnableSystemButtonAggregation)(scarlett_gi_obj_t *self,
                                                                           uint32_t count,
                                                                           const void *buttonFilter);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetFocusPolicy)(scarlett_gi_obj_t *self,
                                                            uint32_t policy);
} scarlett_gi_vtbl_t;

struct scarlett_gi_obj_s {
    const scarlett_gi_vtbl_t *lpVtbl;
    uint64_t unix_instance_handle;
    uint32_t ref_count;
    uint32_t in_use;
};

#define SCARLETT_GI_MAX_INSTANCES 8u
#define SCARLETT_GI_MAX_READINGS  64u
#define SCARLETT_GI_MAX_DEVICES   16u

static scarlett_gi_obj_t         g_gi_instances[SCARLETT_GI_MAX_INSTANCES];
static scarlett_gi_reading_obj_t g_gi_readings[SCARLETT_GI_MAX_READINGS];
static scarlett_gi_device_obj_t  g_gi_devices[SCARLETT_GI_MAX_DEVICES];

static const scarlett_guid_t kIidIGameInput = {
    UINT32_C(0x11BE2A7E), UINT16_C(0x4254), UINT16_C(0x445A),
    { 0x9Cu, 0x09u, 0xFFu, 0xC4u, 0x0Fu, 0x00u, 0x69u, 0x18u }
};
static const scarlett_guid_t kIidIGameInputReading = {
    UINT32_C(0x2156947A), UINT16_C(0xE1FA), UINT16_C(0x4DE0),
    { 0xA3u, 0x0Bu, 0xD8u, 0x12u, 0x93u, 0x1Du, 0xBDu, 0x8Du }
};
static const scarlett_guid_t kIidIGameInputDevice = {
    UINT32_C(0x31DD86FB), UINT16_C(0x4C1B), UINT16_C(0x408A),
    { 0x86u, 0x8Fu, 0x43u, 0x9Bu, 0x3Cu, 0xD4u, 0x71u, 0x25u }
};
static const scarlett_guid_t kIidGiUnknown = {
    UINT32_C(0x00000000), UINT16_C(0x0000), UINT16_C(0x0000),
    { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }
};

/* GameInput SDK v0 GameInputKind bitmask constants (wine/include/gameinput.idl) */
#define GI_SDK_KIND_KEYBOARD       UINT32_C(0x00000010)
#define GI_SDK_KIND_MOUSE          UINT32_C(0x00000020)
#define GI_SDK_KIND_GAMEPAD        UINT32_C(0x00040000)
#define GI_SDK_KIND_UI_NAVIGATION  UINT32_C(0x01000000)

static uint32_t gi_sdk_kind_to_unix_kind(uint32_t sdk_kind) {
    if (sdk_kind == 0u) {
        return 0u;
    }
    uint32_t out = 0u;
    if ((sdk_kind & (GI_SDK_KIND_GAMEPAD | GI_SDK_KIND_UI_NAVIGATION | SCARLETT_GAMEINPUT_KIND_GAMEPAD)) != 0u) {
        out |= SCARLETT_GAMEINPUT_KIND_GAMEPAD;
    }
    if ((sdk_kind & (GI_SDK_KIND_KEYBOARD | SCARLETT_GAMEINPUT_KIND_KEYBOARD)) != 0u) {
        out |= SCARLETT_GAMEINPUT_KIND_KEYBOARD;
    }
    if ((sdk_kind & (GI_SDK_KIND_MOUSE | SCARLETT_GAMEINPUT_KIND_MOUSE)) != 0u) {
        out |= SCARLETT_GAMEINPUT_KIND_MOUSE;
    }
    return out;
}

static uint32_t gi_unix_kind_to_sdk_kind(uint32_t unix_kind) {
    uint32_t out = 0u;
    if ((unix_kind & SCARLETT_GAMEINPUT_KIND_GAMEPAD) != 0u) {
        out |= GI_SDK_KIND_GAMEPAD | GI_SDK_KIND_UI_NAVIGATION;
    }
    if ((unix_kind & SCARLETT_GAMEINPUT_KIND_KEYBOARD) != 0u) {
        out |= GI_SDK_KIND_KEYBOARD;
    }
    if ((unix_kind & SCARLETT_GAMEINPUT_KIND_MOUSE) != 0u) {
        out |= GI_SDK_KIND_MOUSE;
    }
    return out;
}

static bool gi_guid_eq(const scarlett_guid_t *a, const scarlett_guid_t *b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) {
        return false;
    }
    for (size_t i = 0u; i < 8u; ++i) {
        if (a->data4[i] != b->data4[i]) {
            return false;
        }
    }
    return true;
}

static scarlett_gi_device_obj_t *gi_get_or_alloc_device(uint64_t device_id);
static const scarlett_gi_reading_vtbl_t g_gi_reading_vtbl;

/* ============================================================================
 * IGameInputDevice
 * ============================================================================ */

static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_qi(
    scarlett_gi_device_obj_t *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (gi_guid_eq(riid, &kIidIGameInputDevice) || gi_guid_eq(riid, &kIidGiUnknown)) {
        self->ref_count += 1u;
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_WIN64_API gi_dev_add_ref(scarlett_gi_device_obj_t *self) {
    if (self == NULL) return 0u;
    return ++self->ref_count;
}

static uint32_t SCARLETT_WIN64_API gi_dev_release(scarlett_gi_device_obj_t *self) {
    if (self == NULL || self->ref_count == 0u) return 0u;
    if (--self->ref_count == 0u) {
        self->in_use = 0u;
        return 0u;
    }
    return self->ref_count;
}

static const void * SCARLETT_WIN64_API gi_dev_get_info(scarlett_gi_device_obj_t *self) {
    return self != NULL ? &self->info : NULL;
}

static uint32_t SCARLETT_WIN64_API gi_dev_get_status(scarlett_gi_device_obj_t *self) {
    return (self != NULL && self->in_use != 0u) ? 1u : 0u; /* GameInputDeviceConnected */
}

static void SCARLETT_WIN64_API gi_dev_get_battery_state(
    scarlett_gi_device_obj_t *self,
    void *state) {
    (void)self;
    if (state != NULL) {
        audio_pe_zero_mem(state, 16u);
    }
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_create_ff_effect(
    scarlett_gi_device_obj_t *self,
    uint32_t motorIndex,
    const void *params,
    void **effect) {
    (void)self;
    (void)motorIndex;
    (void)params;
    if (effect != NULL) {
        *effect = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static uint32_t SCARLETT_WIN64_API gi_dev_is_ff_powered_on(
    scarlett_gi_device_obj_t *self,
    uint32_t motorIndex) {
    (void)self;
    (void)motorIndex;
    return 1u;
}

static void SCARLETT_WIN64_API gi_dev_set_ff_gain(
    scarlett_gi_device_obj_t *self,
    uint32_t motorIndex,
    float masterGain) {
    (void)self;
    (void)motorIndex;
    (void)masterGain;
}

static void SCARLETT_WIN64_API gi_dev_set_rumble(
    scarlett_gi_device_obj_t *self,
    const scarlett_gi_rumble_params_t *params) {
    if (self == NULL || params == NULL) {
        return;
    }
    scarlett_input_rumble_params_t rp;
    audio_pe_zero_mem(&rp, sizeof(rp));
    rp.device_id = self->device_id;
    rp.low_frequency = params->lowFrequency;
    rp.high_frequency = params->highFrequency;
    rp.left_trigger = params->leftTrigger;
    rp.right_trigger = params->rightTrigger;
    rp.duration_ms = 250u;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_INPUT_SET_RUMBLE, &rp);
}

static const scarlett_stub_descriptor_t g_stub_gi_set_haptic = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_XDL_VTABLE,
    SCARLETT_STUB_ORDINAL_NONE,
    9u,
    SCARLETT_E_NOTIMPL,
    0u,
    "GameInput.dll",
    "IGameInputDevice::SetHapticMotorState"
};

static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_set_haptic(
    scarlett_gi_device_obj_t *self,
    uint32_t motorIndex,
    const void *params) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_gi_set_haptic,
        ret,
        (uint64_t)(uintptr_t)self,
        motorIndex,
        (uint64_t)(uintptr_t)params,
        0u);
}

static void SCARLETT_WIN64_API gi_dev_set_sync_state(scarlett_gi_device_obj_t *self, uint32_t enabled) {
    (void)self; (void)enabled;
}
static void SCARLETT_WIN64_API gi_dev_send_sync_hint(scarlett_gi_device_obj_t *self) {
    (void)self;
}
static void SCARLETT_WIN64_API gi_dev_power_off(scarlett_gi_device_obj_t *self) {
    (void)self;
}
static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_create_raw_report(
    scarlett_gi_device_obj_t *self, uint32_t reportId, int32_t reportKind, void **report) {
    (void)self; (void)reportId; (void)reportKind;
    if (report != NULL) *report = NULL;
    return SCARLETT_E_NOTIMPL;
}
static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_get_raw_feature(
    scarlett_gi_device_obj_t *self, uint32_t reportId, void **report) {
    (void)self; (void)reportId;
    if (report != NULL) *report = NULL;
    return SCARLETT_E_NOTIMPL;
}
static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_set_raw_feature(
    scarlett_gi_device_obj_t *self, void *report) {
    (void)self; (void)report;
    return SCARLETT_E_NOTIMPL;
}
static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_send_raw_output(
    scarlett_gi_device_obj_t *self, void *report) {
    (void)self; (void)report;
    return SCARLETT_E_NOTIMPL;
}
static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_send_raw_output_resp(
    scarlett_gi_device_obj_t *self, void *req, void **resp) {
    (void)self; (void)req;
    if (resp != NULL) *resp = NULL;
    return SCARLETT_E_NOTIMPL;
}
static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_exec_raw_ioctl(
    scarlett_gi_device_obj_t *self, uint32_t code, size_t in_sz, const void *in_buf,
    size_t out_sz, void *out_buf, size_t *out_written) {
    (void)self; (void)code; (void)in_sz; (void)in_buf; (void)out_sz; (void)out_buf;
    if (out_written != NULL) *out_written = 0u;
    return SCARLETT_E_NOTIMPL;
}
static uint32_t SCARLETT_WIN64_API gi_dev_acquire_excl(
    scarlett_gi_device_obj_t *self, uint64_t timeout_us) {
    (void)self; (void)timeout_us;
    return 0u;
}
static void SCARLETT_WIN64_API gi_dev_release_excl(scarlett_gi_device_obj_t *self) {
    (void)self;
}

static const scarlett_gi_device_vtbl_t g_gi_device_vtbl = {
    gi_dev_qi,
    gi_dev_add_ref,
    gi_dev_release,
    gi_dev_get_info,
    gi_dev_get_status,
    gi_dev_get_battery_state,
    gi_dev_create_ff_effect,
    gi_dev_is_ff_powered_on,
    gi_dev_set_ff_gain,
    gi_dev_set_haptic,
    gi_dev_set_rumble,
    gi_dev_set_sync_state,
    gi_dev_send_sync_hint,
    gi_dev_power_off,
    gi_dev_create_raw_report,
    gi_dev_get_raw_feature,
    gi_dev_set_raw_feature,
    gi_dev_send_raw_output,
    gi_dev_send_raw_output_resp,
    gi_dev_exec_raw_ioctl,
    gi_dev_acquire_excl,
    gi_dev_release_excl
};

static scarlett_gi_device_obj_t *gi_get_or_alloc_device(uint64_t device_id) {
    scarlett_gi_device_obj_t *free_slot = NULL;
    for (size_t i = 0u; i < SCARLETT_GI_MAX_DEVICES; ++i) {
        if (g_gi_devices[i].in_use != 0u && g_gi_devices[i].device_id == device_id) {
            g_gi_devices[i].ref_count += 1u;
            return &g_gi_devices[i];
        }
        if (g_gi_devices[i].in_use == 0u && free_slot == NULL) {
            free_slot = &g_gi_devices[i];
        }
    }
    if (free_slot != NULL) {
        audio_pe_zero_mem(free_slot, sizeof(*free_slot));
        free_slot->lpVtbl = &g_gi_device_vtbl;
        free_slot->device_id = device_id;
        free_slot->ref_count = 1u;
        free_slot->in_use = 1u;
        free_slot->info.infoSize = (uint32_t)sizeof(free_slot->info);
        free_slot->info.vendorId = 0x045Eu;
        free_slot->info.productId = 0x0B12u; /* Xbox Series X|S Controller */
        free_slot->info.deviceFamily = 1u;   /* GameInputFamilyXboxOne */
        free_slot->info.supportedInput = GI_SDK_KIND_GAMEPAD |
                                         GI_SDK_KIND_KEYBOARD |
                                         GI_SDK_KIND_MOUSE |
                                         GI_SDK_KIND_UI_NAVIGATION;
        free_slot->info.supportedRumbleMotors = 0x0Fu; /* 4-motor rumble */
        free_slot->info.controllerAxisCount = 6u;
        free_slot->info.controllerButtonCount = 14u;
        free_slot->info.controllerSwitchCount = 1u;
        static const char kDevTag[24] = "SCARLETT-GAMEINPUT-DEV!";
        audio_pe_copy_mem(free_slot->info.deviceId, kDevTag, 24u);
        for (size_t b = 0u; b < 8u; ++b) {
            free_slot->info.deviceId[24u + b] = (uint8_t)((device_id >> (b * 8u)) & 0xFFu);
        }
        audio_pe_copy_mem(free_slot->info.deviceRootId, free_slot->info.deviceId, 32u);
    }
    return free_slot;
}

/* ============================================================================
 * IGameInputReading
 * ============================================================================ */

static scarlett_hresult_t SCARLETT_WIN64_API gi_read_qi(
    scarlett_gi_reading_obj_t *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (gi_guid_eq(riid, &kIidIGameInputReading) || gi_guid_eq(riid, &kIidGiUnknown)) {
        self->ref_count += 1u;
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_WIN64_API gi_read_add_ref(scarlett_gi_reading_obj_t *self) {
    if (self == NULL) return 0u;
    return ++self->ref_count;
}

static uint32_t SCARLETT_WIN64_API gi_read_release(scarlett_gi_reading_obj_t *self) {
    if (self == NULL || self->ref_count == 0u) return 0u;
    if (--self->ref_count == 0u) {
        self->in_use = 0u;
        return 0u;
    }
    return self->ref_count;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_input_kind(scarlett_gi_reading_obj_t *self) {
    return self != NULL ? gi_unix_kind_to_sdk_kind(self->record.input_kind) : 0u;
}

static uint64_t SCARLETT_WIN64_API gi_read_get_seq(
    scarlett_gi_reading_obj_t *self,
    uint32_t inputKind) {
    (void)inputKind;
    return self != NULL ? self->record.sequence_number : 0u;
}

static uint64_t SCARLETT_WIN64_API gi_read_get_ts(scarlett_gi_reading_obj_t *self) {
    return self != NULL ? self->record.timestamp_us : 0u;
}

static void SCARLETT_WIN64_API gi_read_get_device(
    scarlett_gi_reading_obj_t *self,
    void **device) {
    if (device == NULL) {
        return;
    }
    if (self == NULL) {
        *device = NULL;
        return;
    }
    *device = gi_get_or_alloc_device(self->record.device_id);
}

static uint32_t SCARLETT_WIN64_API gi_read_get_raw_report(
    scarlett_gi_reading_obj_t *self,
    void **report) {
    (void)self;
    if (report != NULL) {
        *report = NULL;
    }
    return 0u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_ctrl_axis_count(scarlett_gi_reading_obj_t *self) {
    (void)self;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_ctrl_axis_state(
    scarlett_gi_reading_obj_t *self, uint32_t count, float *arr) {
    (void)self; (void)count; (void)arr;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_ctrl_btn_count(scarlett_gi_reading_obj_t *self) {
    (void)self;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_ctrl_btn_state(
    scarlett_gi_reading_obj_t *self, uint32_t count, uint8_t *arr) {
    (void)self; (void)count; (void)arr;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_ctrl_sw_count(scarlett_gi_reading_obj_t *self) {
    (void)self;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_ctrl_sw_state(
    scarlett_gi_reading_obj_t *self, uint32_t count, uint32_t *arr) {
    (void)self; (void)count; (void)arr;
    return 0u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_key_count(scarlett_gi_reading_obj_t *self) {
    if (self == NULL || (self->record.input_kind & SCARLETT_GAMEINPUT_KIND_KEYBOARD) == 0u) {
        return 0u;
    }
    return self->record.keyboard.key_count <= 16u ? self->record.keyboard.key_count : 16u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_key_state(
    scarlett_gi_reading_obj_t *self,
    uint32_t stateArrayCount,
    scarlett_gi_key_state_t *stateArray) {
    if (self == NULL || stateArray == NULL || stateArrayCount == 0u) {
        return 0u;
    }
    if ((self->record.input_kind & SCARLETT_GAMEINPUT_KIND_KEYBOARD) == 0u) {
        return 0u;
    }
    uint32_t n = self->record.keyboard.key_count;
    if (n > 16u) n = 16u;
    if (n > stateArrayCount) n = stateArrayCount;
    for (uint32_t i = 0u; i < n; ++i) {
        audio_pe_zero_mem(&stateArray[i], sizeof(stateArray[i]));
        stateArray[i].virtualKey = self->record.keyboard.virtual_keys[i];
        stateArray[i].scanCode = (i < 4u) ? self->record.keyboard.scan_codes[i] : 0u;
    }
    return n;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_mouse_state(
    scarlett_gi_reading_obj_t *self,
    scarlett_gi_mouse_state_t *state) {
    if (self == NULL || state == NULL) {
        return 0u;
    }
    if ((self->record.input_kind & SCARLETT_GAMEINPUT_KIND_MOUSE) == 0u) {
        return 0u;
    }
    state->buttons = self->record.mouse.buttons;
    state->reserved = 0u;
    state->positionX = self->record.mouse.position_x;
    state->positionY = self->record.mouse.position_y;
    state->wheelX = self->record.mouse.wheel_x;
    state->wheelY = self->record.mouse.wheel_y;
    return 1u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_touch_count(scarlett_gi_reading_obj_t *self) {
    (void)self;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_touch_state(
    scarlett_gi_reading_obj_t *self, uint32_t count, void *arr) {
    (void)self; (void)count; (void)arr;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_motion_state(
    scarlett_gi_reading_obj_t *self, void *state) {
    (void)self; (void)state;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_arcade_state(
    scarlett_gi_reading_obj_t *self, void *state) {
    (void)self; (void)state;
    return 0u;
}
static uint32_t SCARLETT_WIN64_API gi_read_get_flight_state(
    scarlett_gi_reading_obj_t *self, void *state) {
    (void)self; (void)state;
    return 0u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_gamepad_state(
    scarlett_gi_reading_obj_t *self,
    scarlett_gi_gamepad_state_t *state) {
    if (self == NULL || state == NULL) {
        return 0u;
    }
    if ((self->record.input_kind & SCARLETT_GAMEINPUT_KIND_GAMEPAD) == 0u) {
        return 0u;
    }
    state->buttons = self->record.gamepad.buttons;
    state->leftTrigger = self->record.gamepad.left_trigger;
    state->rightTrigger = self->record.gamepad.right_trigger;
    state->leftThumbstickX = self->record.gamepad.left_thumbstick_x;
    state->leftThumbstickY = self->record.gamepad.left_thumbstick_y;
    state->rightThumbstickX = self->record.gamepad.right_thumbstick_x;
    state->rightThumbstickY = self->record.gamepad.right_thumbstick_y;
    state->extendedButtons = self->record.gamepad.extended_buttons;
    return 1u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_racing_state(
    scarlett_gi_reading_obj_t *self, void *state) {
    (void)self; (void)state;
    return 0u;
}

static uint32_t SCARLETT_WIN64_API gi_read_get_ui_nav_state(
    scarlett_gi_reading_obj_t *self,
    scarlett_gi_ui_nav_state_t *state) {
    if (self == NULL || state == NULL) {
        return 0u;
    }
    uint32_t nav = 0u;
    const uint32_t gp = self->record.gamepad.buttons;
    if ((gp & 0x0001u) != 0u) nav |= 0x00000001u; /* Menu */
    if ((gp & 0x0002u) != 0u) nav |= 0x00000002u; /* View */
    if ((gp & 0x0004u) != 0u) nav |= 0x00000004u; /* Accept (A) */
    if ((gp & 0x0008u) != 0u) nav |= 0x00000008u; /* Cancel (B) */
    if ((gp & 0x0040u) != 0u) nav |= 0x00000010u; /* Up */
    if ((gp & 0x0080u) != 0u) nav |= 0x00000020u; /* Down */
    if ((gp & 0x0100u) != 0u) nav |= 0x00000040u; /* Left */
    if ((gp & 0x0200u) != 0u) nav |= 0x00000080u; /* Right */
    if ((gp & 0x0400u) != 0u) nav |= 0x00000100u; /* PageUp (LB) */
    if ((gp & 0x0800u) != 0u) nav |= 0x00000200u; /* PageDown (RB) */
    state->buttons = nav;
    return 1u;
}

static const scarlett_gi_reading_vtbl_t g_gi_reading_vtbl = {
    gi_read_qi,
    gi_read_add_ref,
    gi_read_release,
    gi_read_get_input_kind,
    gi_read_get_seq,
    gi_read_get_ts,
    gi_read_get_device,
    gi_read_get_raw_report,
    gi_read_get_ctrl_axis_count,
    gi_read_get_ctrl_axis_state,
    gi_read_get_ctrl_btn_count,
    gi_read_get_ctrl_btn_state,
    gi_read_get_ctrl_sw_count,
    gi_read_get_ctrl_sw_state,
    gi_read_get_key_count,
    gi_read_get_key_state,
    gi_read_get_mouse_state,
    gi_read_get_touch_count,
    gi_read_get_touch_state,
    gi_read_get_motion_state,
    gi_read_get_arcade_state,
    gi_read_get_flight_state,
    gi_read_get_gamepad_state,
    gi_read_get_racing_state,
    gi_read_get_ui_nav_state
};

static scarlett_gi_reading_obj_t *gi_alloc_reading_slot(void) {
    for (size_t i = 0u; i < SCARLETT_GI_MAX_READINGS; ++i) {
        if (g_gi_readings[i].in_use == 0u) {
            audio_pe_zero_mem(&g_gi_readings[i], sizeof(g_gi_readings[i]));
            g_gi_readings[i].lpVtbl = &g_gi_reading_vtbl;
            g_gi_readings[i].ref_count = 1u;
            g_gi_readings[i].in_use = 1u;
            return &g_gi_readings[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * IGameInput
 * ============================================================================ */

static scarlett_hresult_t SCARLETT_WIN64_API gi_qi(
    scarlett_gi_obj_t *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (gi_guid_eq(riid, &kIidIGameInput) || gi_guid_eq(riid, &kIidGiUnknown)) {
        self->ref_count += 1u;
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_WIN64_API gi_add_ref(scarlett_gi_obj_t *self) {
    if (self == NULL) return 0u;
    return ++self->ref_count;
}

static uint32_t SCARLETT_WIN64_API gi_release(scarlett_gi_obj_t *self) {
    if (self == NULL || self->ref_count == 0u) return 0u;
    if (--self->ref_count == 0u) {
        scarlett_input_ring_params_t p;
        audio_pe_zero_mem(&p, sizeof(p));
        p.instance_handle = self->unix_instance_handle;
        p.op = (uint32_t)SCARLETT_INPUT_OP_DESTROY_INSTANCE;
        (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &p);
        self->unix_instance_handle = 0u;
        self->in_use = 0u;
        return 0u;
    }
    return self->ref_count;
}

static uint64_t SCARLETT_WIN64_API gi_get_current_ts(scarlett_gi_obj_t *self) {
    (void)self;
    scarlett_input_ring_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_INPUT_OP_GET_TIMESTAMP;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &p);
    return p.out_timestamp_us;
}

static scarlett_hresult_t gi_fetch_reading_common(
    scarlett_gi_obj_t *self,
    uint32_t op,
    uint64_t ref_seq,
    uint32_t inputKind,
    void *device,
    void **reading) {
    if (self == NULL || reading == NULL) {
        return SCARLETT_E_POINTER;
    }
    *reading = NULL;
    const uint32_t unix_kind = gi_sdk_kind_to_unix_kind(inputKind);
    if (inputKind != 0u && unix_kind == 0u) {
        return (scarlett_hresult_t)UINT32_C(0x838A0003); /* GAMEINPUT_E_READING_NOT_FOUND */
    }

    scarlett_gi_reading_obj_t *slot = gi_alloc_reading_slot();
    if (slot == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_input_ring_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.instance_handle = self->unix_instance_handle;
    p.op = op;
    p.ref_sequence = ref_seq;
    p.input_kind = unix_kind;
    p.device_id = (device != NULL) ? ((const scarlett_gi_device_obj_t *)device)->device_id : 0u;
    p.record_addr = (uint64_t)(uintptr_t)&slot->record;

    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &p);
    if (scarlett_failed(hr)) {
        slot->in_use = 0u;
        return hr;
    }
    slot->instance_handle = self->unix_instance_handle;
    *reading = slot;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_get_current_reading(
    scarlett_gi_obj_t *self,
    uint32_t inputKind,
    void *device,
    void **reading) {
    return gi_fetch_reading_common(
        self, (uint32_t)SCARLETT_INPUT_OP_GET_CURRENT, 0u, inputKind, device, reading);
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_get_next_reading(
    scarlett_gi_obj_t *self,
    void *referenceReading,
    uint32_t inputKind,
    void *device,
    void **reading) {
    if (referenceReading == NULL) {
        return SCARLETT_E_POINTER;
    }
    const uint64_t ref_seq = ((const scarlett_gi_reading_obj_t *)referenceReading)->record.sequence_number;
    return gi_fetch_reading_common(
        self, (uint32_t)SCARLETT_INPUT_OP_GET_NEXT, ref_seq, inputKind, device, reading);
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_get_previous_reading(
    scarlett_gi_obj_t *self,
    void *referenceReading,
    uint32_t inputKind,
    void *device,
    void **reading) {
    if (referenceReading == NULL) {
        return SCARLETT_E_POINTER;
    }
    const uint64_t ref_seq = ((const scarlett_gi_reading_obj_t *)referenceReading)->record.sequence_number;
    return gi_fetch_reading_common(
        self, (uint32_t)SCARLETT_INPUT_OP_GET_PREVIOUS, ref_seq, inputKind, device, reading);
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_get_temporal_reading(
    scarlett_gi_obj_t *self,
    uint64_t timestamp,
    void *device,
    void **reading) {
    (void)self;
    (void)timestamp;
    (void)device;
    if (reading == NULL) {
        return SCARLETT_E_POINTER;
    }
    *reading = NULL;
    return (scarlett_hresult_t)UINT32_C(0x838A0003); /* GAMEINPUT_E_READING_NOT_FOUND */
}

typedef void (SCARLETT_WIN64_API *scarlett_gi_device_cb_t)(
    uint64_t callbackToken,
    void *context,
    void *device,
    uint64_t timestamp,
    uint32_t currentStatus,
    uint32_t previousStatus);

static scarlett_hresult_t SCARLETT_WIN64_API gi_register_device_callback(
    scarlett_gi_obj_t *self,
    void *device,
    uint32_t inputKind,
    uint32_t statusFilter,
    uint32_t enumerationKind,
    void *context,
    void *callbackFunc,
    uint64_t *callbackToken) {
    (void)device;
    const uint64_t tok = 1u;
    if (callbackToken != NULL) {
        *callbackToken = tok;
    }
    /* GameInputAsyncEnumeration (1) and GameInputBlockingEnumeration (2) */
    const bool kind_ok = (inputKind == 0u) || (gi_sdk_kind_to_unix_kind(inputKind) != 0u);
    const bool status_ok = (statusFilter == 0u) || ((statusFilter & 1u) != 0u);
    if ((enumerationKind == 1u || enumerationKind == 2u) &&
        callbackFunc != NULL && kind_ok && status_ok) {
        scarlett_gi_device_obj_t *dev = gi_get_or_alloc_device(1u);
        if (dev != NULL) {
            const uint64_t ts = gi_get_current_ts(self);
            ((scarlett_gi_device_cb_t)callbackFunc)(tok, context, dev, ts, 1u, 0u);
        }
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_register_reading_callback(
    scarlett_gi_obj_t *self,
    void *device,
    uint32_t inputKind,
    float analogThreshold,
    void *context,
    void *callbackFunc,
    uint64_t *callbackToken) {
    (void)self;
    (void)device;
    (void)inputKind;
    (void)analogThreshold;
    (void)context;
    (void)callbackFunc;
    if (callbackToken != NULL) {
        *callbackToken = 2u;
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_register_system_button_callback(
    scarlett_gi_obj_t *self,
    void *device,
    uint32_t buttonFilter,
    void *context,
    void *callbackFunc,
    uint64_t *callbackToken) {
    (void)self;
    (void)device;
    (void)buttonFilter;
    (void)context;
    (void)callbackFunc;
    if (callbackToken != NULL) {
        *callbackToken = 3u;
    }
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_register_keyboard_layout_callback(
    scarlett_gi_obj_t *self,
    void *device,
    void *context,
    void *callbackFunc,
    uint64_t *callbackToken) {
    (void)self;
    (void)device;
    (void)context;
    (void)callbackFunc;
    if (callbackToken != NULL) {
        *callbackToken = 4u;
    }
    return SCARLETT_OK;
}

static void SCARLETT_WIN64_API gi_stop_callback(
    scarlett_gi_obj_t *self,
    uint64_t callbackToken) {
    (void)self;
    (void)callbackToken;
}

static uint32_t SCARLETT_WIN64_API gi_unregister_callback(
    scarlett_gi_obj_t *self,
    uint64_t callbackToken,
    uint64_t timeoutInMicroseconds) {
    (void)self;
    (void)callbackToken;
    (void)timeoutInMicroseconds;
    return 1u;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_create_dispatcher(
    scarlett_gi_obj_t *self,
    void **dispatcher) {
    (void)self;
    if (dispatcher != NULL) {
        *dispatcher = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_create_aggregate_device(
    scarlett_gi_obj_t *self,
    uint32_t inputKind,
    void **device) {
    (void)self;
    (void)inputKind;
    if (device != NULL) {
        *device = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_find_device_from_id(
    scarlett_gi_obj_t *self,
    const void *deviceId,
    void **device) {
    (void)self;
    (void)deviceId;
    if (device != NULL) {
        *device = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_find_device_from_object(
    scarlett_gi_obj_t *self,
    void *value,
    void **device) {
    (void)self;
    (void)value;
    if (device != NULL) {
        *device = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_find_device_from_platform_handle(
    scarlett_gi_obj_t *self,
    void *value,
    void **device) {
    (void)self;
    (void)value;
    if (device != NULL) {
        *device = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_find_device_from_platform_string(
    scarlett_gi_obj_t *self,
    const void *value,
    void **device) {
    (void)self;
    (void)value;
    if (device != NULL) {
        *device = NULL;
    }
    return SCARLETT_E_NOTIMPL;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_enable_system_button_aggregation(
    scarlett_gi_obj_t *self,
    uint32_t count,
    const void *buttonFilter) {
    (void)self;
    (void)count;
    (void)buttonFilter;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API gi_set_focus_policy(
    scarlett_gi_obj_t *self,
    uint32_t policy) {
    (void)self;
    (void)policy;
    return SCARLETT_OK;
}

static const scarlett_gi_vtbl_t g_gi_vtbl = {
    gi_qi,                                /* 0 (0x00) */
    gi_add_ref,                           /* 1 (0x08) */
    gi_release,                           /* 2 (0x10) */
    gi_get_current_ts,                    /* 3 (0x18) */
    gi_get_current_reading,               /* 4 (0x20) */
    gi_get_next_reading,                  /* 5 (0x28) */
    gi_get_previous_reading,              /* 6 (0x30) */
    gi_get_temporal_reading,              /* 7 (0x38) */
    gi_register_reading_callback,         /* 8 (0x40) */
    gi_register_device_callback,          /* 9 (0x48) */
    gi_register_system_button_callback,   /* 10 (0x50) */
    gi_register_keyboard_layout_callback, /* 11 (0x58) */
    gi_stop_callback,                     /* 12 (0x60) */
    gi_unregister_callback,               /* 13 (0x68) */
    gi_create_dispatcher,                 /* 14 (0x70) */
    gi_create_aggregate_device,           /* 15 (0x78) */
    gi_find_device_from_id,               /* 16 (0x80) */
    gi_find_device_from_object,           /* 17 (0x88) */
    gi_find_device_from_platform_handle,  /* 18 (0x90) */
    gi_find_device_from_platform_string,  /* 19 (0x98) */
    gi_enable_system_button_aggregation,  /* 20 (0xa0) */
    gi_set_focus_policy                   /* 21 (0xa8) */
};

/* ============================================================================
 * Exported Functions (Ordinal #1 GameInputCreate + #2 Auto-Stub)
 * ============================================================================ */

scarlett_hresult_t SCARLETT_WIN64_API GameInputCreate(void **gameInput) {
    if (gameInput == NULL) {
        return SCARLETT_E_POINTER;
    }
    *gameInput = NULL;

    scarlett_gi_obj_t *slot = NULL;
    for (size_t i = 0u; i < SCARLETT_GI_MAX_INSTANCES; ++i) {
        if (g_gi_instances[i].in_use == 0u) {
            slot = &g_gi_instances[i];
            break;
        }
    }
    if (slot == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_input_ring_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_INPUT_OP_CREATE_INSTANCE;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &p);
    if (scarlett_failed(hr)) {
        return hr;
    }

    audio_pe_zero_mem(slot, sizeof(*slot));
    slot->lpVtbl = &g_gi_vtbl;
    slot->unix_instance_handle = p.instance_handle;
    slot->ref_count = 1u;
    slot->in_use = 1u;
    *gameInput = slot;
    return SCARLETT_OK;
}

static const scarlett_stub_descriptor_t g_stub_gi_create_dispatcher = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_ORDINAL_EXPORT,
    2u,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_E_NOTIMPL,
    0u,
    "GameInput.dll",
    "GameInputCreateDispatcher"
};

scarlett_hresult_t SCARLETT_WIN64_API GameInputCreateDispatcher(void **dispatcher) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_gi_create_dispatcher, ret, (uint64_t)(uintptr_t)dispatcher, 0u, 0u, 0u);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
