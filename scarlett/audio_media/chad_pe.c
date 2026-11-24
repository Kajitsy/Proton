/*
 * Proton-Scarlett — chad.dll PE64 Freestanding Shim
 *
 * Implements Xbox Console Hardware Audio DSP (CHAD / XDsp):
 * XDspConnectWithMaximumStreamLimit, XDspActivate, XDspDeactivate,
 * XDspDisconnect, XDspSubmitCommand, and console hardware queries
 * (ChadGetConsoleModel, ChadGetHardwareState, ChadQueryCapabilities).
 *
 * Fully compliant with Wine 11.0 PE isolation:
 * Strictly freestanding C11 (zero libc, zero <windows.h>, zero POSIX headers).
 */

#include "audio_pe_common.h"

scarlett_hresult_t SCARLETT_WIN64_API XDspConnectWithMaximumStreamLimit(
    void *p0,
    uint32_t p1,
    uint32_t p2,
    uint32_t max_streams,
    uint32_t flags,
    void **out_handle) {
    (void)p0;
    (void)p1;
    (void)p2;
    (void)max_streams;
    (void)flags;

    if (out_handle == NULL) {
        return SCARLETT_E_POINTER;
    }

    *out_handle = (void *)(uintptr_t)0xCD500001ULL;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API XDspActivate(
    void *p0,
    void *p1,
    void *p2,
    void *p3) {
    (void)p0;
    (void)p1;
    (void)p2;
    (void)p3;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API XDspDeactivate(void *handle) {
    (void)handle;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API XDspDisconnect(void *handle) {
    (void)handle;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API XDspSubmitCommand(
    void *handle,
    const void *cmd,
    uint32_t cmd_size,
    void *result) {
    (void)handle;
    (void)cmd;
    (void)cmd_size;
    (void)result;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API ChadGetConsoleModel(uint32_t *out_model) {
    if (out_model == NULL) {
        return SCARLETT_E_POINTER;
    }
    /* Default to XboxScarlettAnaconda (SCARLETT_MODEL_ANACONDA = 1) */
    *out_model = 1u;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API ChadGetHardwareState(void *out_state) {
    if (out_state == NULL) {
        return SCARLETT_E_POINTER;
    }
    audio_pe_zero_mem(out_state, 64u);
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API ChadQueryCapabilities(uint32_t cap_id, void *out_value) {
    (void)cap_id;
    if (out_value != NULL) {
        audio_pe_zero_mem(out_value, 32u);
    }
    return SCARLETT_OK;
}
