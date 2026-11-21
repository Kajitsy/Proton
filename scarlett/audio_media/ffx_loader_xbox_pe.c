/*
 * Proton-Scarlett — amd_fidelityfx_loader_xbox.dll PE64 Freestanding Shim
 *
 * Clean-Room FFX Loader ABI (ffxCreateContext, ffxDestroyContext, ffxConfigure, ffxQuery)
 * + diagnostic auto-stub for ffxLoadInterface.
 */

#include "audio_pe_common.h"

typedef struct scarlett_ffx_create_desc_s {
    uint32_t quality_mode;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t max_render_width;
    uint32_t max_render_height;
    uint32_t fsr_version_packed;
} scarlett_ffx_create_desc_t;

scarlett_hresult_t SCARLETT_WIN64_API ffxCreateContext(
    uint64_t *context,
    const scarlett_ffx_create_desc_t *desc,
    const void *mem_cb) {
    (void)mem_cb;
    if (context == NULL || desc == NULL) {
        return SCARLETT_E_POINTER;
    }
    *context = 0u;
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_CREATE_CONTEXT;
    p.quality_mode = desc->quality_mode;
    p.display_width = desc->display_width;
    p.display_height = desc->display_height;
    p.render_width = desc->max_render_width;
    p.render_height = desc->max_render_height;
    p.fsr_version_packed = desc->fsr_version_packed;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    if (scarlett_succeeded(hr)) {
        *context = p.context_handle;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API ffxDestroyContext(
    uint64_t *context,
    const void *mem_cb) {
    (void)mem_cb;
    if (context == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_DESTROY_CONTEXT;
    p.context_handle = *context;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    if (scarlett_succeeded(hr)) {
        *context = 0u;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API ffxConfigure(
    uint64_t *context,
    const scarlett_ffx_create_desc_t *desc) {
    if (context == NULL || desc == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_CONFIGURE;
    p.context_handle = *context;
    p.display_width = desc->display_width;
    p.display_height = desc->display_height;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API ffxQuery(
    uint64_t *context,
    uint32_t *out_version_packed,
    uint32_t *out_used_vulkan) {
    (void)context;
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_QUERY;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    if (scarlett_succeeded(hr)) {
        if (out_version_packed != NULL) {
            *out_version_packed = p.fsr_version_packed;
        }
        if (out_used_vulkan != NULL) {
            *out_used_vulkan = p.out_used_vulkan_gpu;
        }
    }
    return hr;
}

static const scarlett_stub_descriptor_t g_stub_ffx_load_iface = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "amd_fidelityfx_loader_xbox.dll",
    "ffxLoadInterface"
};

scarlett_hresult_t SCARLETT_WIN64_API ffxLoadInterface(
    void *backend,
    uint32_t version,
    void *out_iface,
    uint64_t flags) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_ffx_load_iface,
        ret,
        (uint64_t)(uintptr_t)backend,
        version,
        (uint64_t)(uintptr_t)out_iface,
        flags);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
