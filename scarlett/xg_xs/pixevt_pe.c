/*
 * Proton-Scarlett — pixevt.dll PE64 Shim
 *
 * Strictly freestanding C11:
 *   - ZERO references to libc, POSIX, <windows.h>, or libvulkan.
 *   - Crosses PE <-> xg_xs_unix.so boundary ONLY via WINE_UNIX_CALL.
 *   - Routes PIXBeginEvent / PIXEndEvent / PIXSetMarker (+ OnCommandList) to
 *     VK_EXT_debug_utils with stack nesting & ARGB->RGBA conversion.
 *   - All unimplemented exports route through scarlett_stub_thunk.h.
 */

#include "xg_xs_pe_common.h"

static void pix_emit_event(
    uint32_t op_kind,
    uint64_t cmd_list,
    uint64_t argb_color,
    const char *label) {
    scarlett_pix_event_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.op_kind = op_kind;
    params.command_list = cmd_list;
    params.argb_color = argb_color;
    params.label_utf8_addr = (uint64_t)(uintptr_t)label;
    (void)xg_pe_unix_call(SCARLETT_XG_XS_UNIX_PIX_EVENT, &params);
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXBeginEvent(
    uint64_t color,
    const char *formatString,
    ...) {
    char buf[SCARLETT_PIX_MAX_LABEL_LEN];
    va_list ap;
    va_start(ap, formatString);
    xg_pe_format_label_va(buf, sizeof(buf), formatString, ap);
    va_end(ap);
    pix_emit_event(0u, 0u, color, buf);
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXEndEvent(void) {
    pix_emit_event(1u, 0u, 0u, NULL);
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXSetMarker(
    uint64_t color,
    const char *formatString,
    ...) {
    char buf[SCARLETT_PIX_MAX_LABEL_LEN];
    va_list ap;
    va_start(ap, formatString);
    xg_pe_format_label_va(buf, sizeof(buf), formatString, ap);
    va_end(ap);
    pix_emit_event(2u, 0u, color, buf);
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXBeginEventOnCommandList(
    void *commandList,
    uint64_t color,
    const char *formatString,
    ...) {
    char buf[SCARLETT_PIX_MAX_LABEL_LEN];
    va_list ap;
    va_start(ap, formatString);
    xg_pe_format_label_va(buf, sizeof(buf), formatString, ap);
    va_end(ap);
    pix_emit_event(0u, (uint64_t)(uintptr_t)commandList, color, buf);
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXEndEventOnCommandList(void *commandList) {
    pix_emit_event(1u, (uint64_t)(uintptr_t)commandList, 0u, NULL);
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXSetMarkerOnCommandList(
    void *commandList,
    uint64_t color,
    const char *formatString,
    ...) {
    char buf[SCARLETT_PIX_MAX_LABEL_LEN];
    va_list ap;
    va_start(ap, formatString);
    xg_pe_format_label_va(buf, sizeof(buf), formatString, ap);
    va_end(ap);
    pix_emit_event(2u, (uint64_t)(uintptr_t)commandList, color, buf);
}

SCARLETT_XG_API uint32_t SCARLETT_XG_CALL PIXGetEventStackDepth(
    void *commandList,
    uint32_t *outUnderflowCount) {
    scarlett_pix_event_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.op_kind = 3u;
    params.command_list = (uint64_t)(uintptr_t)commandList;
    if (scarlett_failed(xg_pe_unix_call(SCARLETT_XG_XS_UNIX_PIX_EVENT, &params))) {
        return 0u;
    }
    if (outUnderflowCount != NULL) {
        *outUnderflowCount = params.out_underflow_count;
    }
    return params.out_depth;
}

SCARLETT_XG_API void SCARLETT_XG_CALL PIXSetEventsEnabled(uint32_t enabled) {
    scarlett_pix_event_params_t params;
    xg_pe_zero_mem(&params, sizeof(params));
    params.op_kind = 4u;
    params.enabled_flag = enabled != 0u ? 1u : 0u;
    (void)xg_pe_unix_call(SCARLETT_XG_XS_UNIX_PIX_EVENT, &params);
}

static const scarlett_stub_descriptor_t g_stub_pix_gpu_capture = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "pixevt.dll",
    "PIXGpuCaptureNextFrames",
};

SCARLETT_XG_API scarlett_hresult_t SCARLETT_XG_CALL PIXGpuCaptureNextFrames(
    const void *fileName,
    uint32_t numFrames) {
    return xg_pe_stub_dispatch_local(
        &g_stub_pix_gpu_capture,
        (uint64_t)(uintptr_t)__builtin_return_address(0),
        (uint64_t)(uintptr_t)fileName,
        (uint64_t)numFrames,
        0u,
        0u);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_XG_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    return xg_pe_dll_main(reason);
}
#endif
