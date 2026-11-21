/*
 * Proton-Scarlett — amd_fidelityfx_upscaler_xbox.dll PE64 Freestanding Shim
 *
 * Clean-Room Vulkan FidelityFX FSR 3 / FSR 4.1.2 Xbox upscaler bridge.
 */

#include "audio_pe_common.h"

typedef struct scarlett_fsr3_context_desc_s {
    uint32_t flags;
    uint32_t quality_mode;
    uint32_t max_render_width;
    uint32_t max_render_height;
    uint32_t display_width;
    uint32_t display_height;
} scarlett_fsr3_context_desc_t;

typedef struct scarlett_fsr3_dispatch_desc_s {
    const float *color_in;
    const float *depth_in;
    const float *motion_vectors;
    float *color_out;
    uint32_t render_width;
    uint32_t render_height;
    uint32_t display_width;
    uint32_t display_height;
    float jitter_x;
    float jitter_y;
    float sharpness;
    float frame_time_delta_ms;
    uint32_t out_used_vulkan_gpu;
} scarlett_fsr3_dispatch_desc_t;

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextCreate(
    uint64_t *context,
    const scarlett_fsr3_context_desc_t *desc) {
    if (context == NULL || desc == NULL) {
        return SCARLETT_E_POINTER;
    }
    *context = 0u;
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_CREATE_CONTEXT;
    p.quality_mode = desc->quality_mode;
    p.render_width = desc->max_render_width;
    p.render_height = desc->max_render_height;
    p.display_width = desc->display_width;
    p.display_height = desc->display_height;
    p.fsr_version_packed = 0x040102u; /* FSR 4.1.2 */
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    if (scarlett_succeeded(hr)) {
        *context = p.context_handle;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextDestroy(uint64_t *context) {
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

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextDispatch(
    uint64_t *context,
    scarlett_fsr3_dispatch_desc_t *desc) {
    if (context == NULL || desc == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_DISPATCH_UPSCALE;
    p.context_handle = *context;
    p.color_in_addr = (uint64_t)(uintptr_t)desc->color_in;
    p.depth_in_addr = (uint64_t)(uintptr_t)desc->depth_in;
    p.motion_in_addr = (uint64_t)(uintptr_t)desc->motion_vectors;
    p.color_out_addr = (uint64_t)(uintptr_t)desc->color_out;
    p.render_width = desc->render_width;
    p.render_height = desc->render_height;
    p.display_width = desc->display_width;
    p.display_height = desc->display_height;
    p.jitter_x = desc->jitter_x;
    p.jitter_y = desc->jitter_y;
    p.sharpness = desc->sharpness;
    p.frame_time_delta_ms = desc->frame_time_delta_ms;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    desc->out_used_vulkan_gpu = p.out_used_vulkan_gpu;
    return hr;
}

float SCARLETT_WIN64_API ffxFsr3GetUpscaleRatioFromQualityMode(uint32_t qualityMode) {
    switch (qualityMode) {
        case SCARLETT_FSR_QUALITY_NATIVEAA:          return 1.0f;
        case SCARLETT_FSR_QUALITY_QUALITY:           return 1.5f;
        case SCARLETT_FSR_QUALITY_BALANCED:          return 1.7f;
        case SCARLETT_FSR_QUALITY_PERFORMANCE:       return 2.0f;
        case SCARLETT_FSR_QUALITY_ULTRA_PERFORMANCE: return 3.0f;
        default:                                     return 1.5f;
    }
}

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3GetRenderResolutionFromQualityMode(
    uint32_t *renderWidth,
    uint32_t *renderHeight,
    uint32_t displayWidth,
    uint32_t displayHeight,
    uint32_t qualityMode) {
    if (renderWidth == NULL || renderHeight == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_GET_RENDER_RES;
    p.display_width = displayWidth;
    p.display_height = displayHeight;
    p.quality_mode = qualityMode;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    if (scarlett_succeeded(hr)) {
        *renderWidth = p.render_width;
        *renderHeight = p.render_height;
    }
    return hr;
}

int32_t SCARLETT_WIN64_API ffxFsr3GetJitterPhaseCount(
    int32_t renderWidth,
    int32_t displayWidth) {
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_GET_JITTER_PHASES;
    p.render_width = renderWidth > 0 ? (uint32_t)renderWidth : 1u;
    p.display_width = displayWidth > 0 ? (uint32_t)displayWidth : 1u;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p))) {
        return 8;
    }
    return (int32_t)p.jitter_phase_count;
}

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3GetJitterOffset(
    float *outX,
    float *outY,
    int32_t index,
    int32_t phaseCount) {
    if (outX == NULL || outY == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_fsr_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_FSR_OP_GET_JITTER_OFFSET;
    p.jitter_index = index >= 0 ? (uint32_t)index : 0u;
    p.jitter_phase_count = phaseCount > 0 ? (uint32_t)phaseCount : 1u;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_FSR_OP, &p);
    if (scarlett_succeeded(hr)) {
        *outX = p.jitter_x;
        *outY = p.jitter_y;
    }
    return hr;
}

uint32_t SCARLETT_WIN64_API ffxFsr4GetVersion(void) {
    return 0x040102u; /* FSR 4.1.2 */
}

static const scarlett_stub_descriptor_t g_stub_fsr4_debug_hook = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "amd_fidelityfx_upscaler_xbox.dll",
    "ffxFsr4SetDebugHook"
};

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr4SetDebugHook(
    void *context,
    void *hook_fn,
    uint32_t flags,
    uint64_t reserved) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_fsr4_debug_hook,
        ret,
        (uint64_t)(uintptr_t)context,
        (uint64_t)(uintptr_t)hook_fn,
        flags,
        reserved);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
