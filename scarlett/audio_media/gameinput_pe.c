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

typedef struct scarlett_gi_device_info_s {
    uint32_t infoSize;
    uint16_t vendorId;
    uint16_t productId;
    uint32_t supportedInput;
    uint32_t supportedRumbleMotors;
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
    scarlett_hresult_t (SCARLETT_WIN64_API *GetDeviceInfo)(scarlett_gi_device_obj_t *self,
                                                           const void **info);
    uint32_t (SCARLETT_WIN64_API *GetDeviceStatus)(scarlett_gi_device_obj_t *self);
    void (SCARLETT_WIN64_API *SetRumbleState)(scarlett_gi_device_obj_t *self,
                                              const scarlett_gi_rumble_params_t *params);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetHapticMotorState)(scarlett_gi_device_obj_t *self,
                                                                 uint32_t motorIndex,
                                                                 const void *params);
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
    uint32_t (SCARLETT_WIN64_API *GetGamepadState)(scarlett_gi_reading_obj_t *self,
                                                   scarlett_gi_gamepad_state_t *state);
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
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterDeviceCallback)(scarlett_gi_obj_t *self,
                                                                    void *device,
                                                                    uint32_t inputKind,
                                                                    uint32_t statusFilter,
                                                                    uint32_t enumerationKind,
                                                                    void *context,
                                                                    void *callbackFunc,
                                                                    uint64_t *callbackToken);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetPreviousReading)(scarlett_gi_obj_t *self,
                                                                void *referenceReading,
                                                                uint32_t inputKind,
                                                                void *device,
                                                                void **reading);
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

static scarlett_hresult_t SCARLETT_WIN64_API gi_dev_get_info(
    scarlett_gi_device_obj_t *self,
    const void **info) {
    if (self == NULL || info == NULL) {
        return SCARLETT_E_POINTER;
    }
    *info = &self->info;
    return SCARLETT_OK;
}

static uint32_t SCARLETT_WIN64_API gi_dev_get_status(scarlett_gi_device_obj_t *self) {
    return (self != NULL && self->in_use != 0u) ? 1u : 0u; /* GameInputDeviceConnected */
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
    6u,
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

static const scarlett_gi_device_vtbl_t g_gi_device_vtbl = {
    gi_dev_qi,
    gi_dev_add_ref,
    gi_dev_release,
    gi_dev_get_info,
    gi_dev_get_status,
    gi_dev_set_rumble,
    gi_dev_set_haptic
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
        free_slot->info.supportedInput = SCARLETT_GAMEINPUT_KIND_GAMEPAD |
                                         SCARLETT_GAMEINPUT_KIND_KEYBOARD |
                                         SCARLETT_GAMEINPUT_KIND_MOUSE;
        free_slot->info.supportedRumbleMotors = 0x0Fu; /* 4-motor rumble */
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
    return self != NULL ? self->record.input_kind : 0u;
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

static const scarlett_gi_reading_vtbl_t g_gi_reading_vtbl = {
    gi_read_qi,
    gi_read_add_ref,
    gi_read_release,
    gi_read_get_input_kind,
    gi_read_get_seq,
    gi_read_get_ts,
    gi_read_get_device,
    gi_read_get_gamepad_state
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
    scarlett_gi_reading_obj_t *slot = gi_alloc_reading_slot();
    if (slot == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_input_ring_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.instance_handle = self->unix_instance_handle;
    p.op = op;
    p.ref_sequence = ref_seq;
    p.input_kind = inputKind;
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

static const scarlett_stub_descriptor_t g_stub_gi_reg_cb = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_XDL_VTABLE,
    SCARLETT_STUB_ORDINAL_NONE,
    6u,
    SCARLETT_E_NOTIMPL,
    0u,
    "GameInput.dll",
    "IGameInput::RegisterDeviceCallback"
};

static scarlett_hresult_t SCARLETT_WIN64_API gi_register_device_callback(
    scarlett_gi_obj_t *self,
    void *device,
    uint32_t inputKind,
    uint32_t statusFilter,
    uint32_t enumerationKind,
    void *context,
    void *callbackFunc,
    uint64_t *callbackToken) {
    (void)enumerationKind;
    (void)context;
    (void)callbackFunc;
    (void)callbackToken;
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_gi_reg_cb,
        ret,
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)device,
        inputKind,
        statusFilter);
}

static const scarlett_gi_vtbl_t g_gi_vtbl = {
    gi_qi,
    gi_add_ref,
    gi_release,
    gi_get_current_ts,
    gi_get_current_reading,
    gi_get_next_reading,
    gi_register_device_callback,
    gi_get_previous_reading
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
