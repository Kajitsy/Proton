/*
 * Proton-Scarlett — Bink2Scarlett.dll PE64 Freestanding Shim
 *
 * Implements all 12 core Bink2 exports:
 *   BinkOpen, BinkClose, BinkDoFrame, BinkNextFrame, BinkWait,
 *   BinkCopyToBuffer, BinkCopyToBufferRect, BinkSetSoundOnOff,
 *   BinkSetVolume, BinkPause, BinkGoto, BinkGetSummary
 * plus diagnostic auto-stub for BinkSetIOSize.
 */

#include "audio_pe_common.h"

typedef struct scarlett_bink_handle_s {
    uint32_t Width;
    uint32_t Height;
    uint32_t Frames;
    uint32_t FrameNum;
    uint32_t FrameRate;
    uint32_t FrameRateDiv;
    uint64_t unix_bink_handle;
    uint32_t in_use;
    uint32_t reserved;
} scarlett_bink_handle_t;

#define SCARLETT_BINK_MAX_SESSIONS 16u
static scarlett_bink_handle_t g_bink_sessions[SCARLETT_BINK_MAX_SESSIONS];

static void bink_sync_summary(scarlett_bink_handle_t *bink) {
    if (bink == NULL) {
        return;
    }
    scarlett_bink_summary_t sum;
    audio_pe_zero_mem(&sum, sizeof(sum));
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_GET_SUMMARY;
    p.summary_addr = (uint64_t)(uintptr_t)&sum;
    if (scarlett_succeeded(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        bink->Width = sum.width;
        bink->Height = sum.height;
        bink->Frames = sum.total_frames;
        bink->FrameNum = sum.current_frame;
        bink->FrameRate = sum.frame_rate_num;
        bink->FrameRateDiv = sum.frame_rate_den;
    }
}

void * SCARLETT_WIN64_API BinkOpen(const void *name_or_mem, uint32_t flags) {
    if (name_or_mem == NULL) {
        return NULL;
    }
    scarlett_bink_handle_t *slot = NULL;
    for (size_t i = 0u; i < SCARLETT_BINK_MAX_SESSIONS; ++i) {
        if (g_bink_sessions[i].in_use == 0u) {
            slot = &g_bink_sessions[i];
            break;
        }
    }
    if (slot == NULL) {
        return NULL;
    }

    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_BINK2_OP_OPEN;
    p.path_or_buf_addr = (uint64_t)(uintptr_t)name_or_mem;
    p.flags = flags;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        return NULL;
    }

    audio_pe_zero_mem(slot, sizeof(*slot));
    slot->unix_bink_handle = p.bink_handle;
    slot->in_use = 1u;
    bink_sync_summary(slot);
    return slot;
}

void SCARLETT_WIN64_API BinkClose(void *bink_ptr) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_CLOSE;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p);
    bink->unix_bink_handle = 0u;
    bink->in_use = 0u;
}

int32_t SCARLETT_WIN64_API BinkDoFrame(void *bink_ptr) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return -1;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_DO_FRAME;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        return -1;
    }
    return (int32_t)p.out_result_u32;
}

void SCARLETT_WIN64_API BinkNextFrame(void *bink_ptr) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_NEXT_FRAME;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p);
    bink_sync_summary(bink);
}

int32_t SCARLETT_WIN64_API BinkWait(void *bink_ptr) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return 0;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_WAIT;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        return 0;
    }
    return (int32_t)p.out_result_u32;
}

int32_t SCARLETT_WIN64_API BinkCopyToBuffer(
    void *bink_ptr,
    void *dest,
    int32_t dest_pitch,
    uint32_t dest_height,
    uint32_t dest_x,
    uint32_t dest_y,
    uint32_t flags) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u || dest == NULL) {
        return -1;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_COPY_TO_BUFFER;
    p.path_or_buf_addr = (uint64_t)(uintptr_t)dest;
    p.dest_pitch = dest_pitch;
    p.dest_height = dest_height;
    p.dest_x = dest_x;
    p.dest_y = dest_y;
    p.flags = flags;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        return -1;
    }
    return (int32_t)p.out_result_u32;
}

int32_t SCARLETT_WIN64_API BinkCopyToBufferRect(
    void *bink_ptr,
    void *dest,
    int32_t dest_pitch,
    uint32_t dest_height,
    uint32_t dest_x,
    uint32_t dest_y,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t src_w,
    uint32_t src_h,
    uint32_t flags) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u || dest == NULL) {
        return -1;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_COPY_TO_BUFFER_RECT;
    p.path_or_buf_addr = (uint64_t)(uintptr_t)dest;
    p.dest_pitch = dest_pitch;
    p.dest_height = dest_height;
    p.dest_x = dest_x;
    p.dest_y = dest_y;
    p.src_x = src_x;
    p.src_y = src_y;
    p.src_w = src_w;
    p.src_h = src_h;
    p.flags = flags;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        return -1;
    }
    return (int32_t)p.out_result_u32;
}

void SCARLETT_WIN64_API BinkSetSoundOnOff(void *bink_ptr, int32_t onoff) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_SET_SOUND_ON_OFF;
    p.arg_u32 = onoff != 0 ? 1u : 0u;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p);
}

void SCARLETT_WIN64_API BinkSetVolume(void *bink_ptr, uint32_t track_id, int32_t volume) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_SET_VOLUME;
    p.arg_u32 = track_id;
    p.arg_i32 = volume;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p);
}

int32_t SCARLETT_WIN64_API BinkPause(void *bink_ptr, int32_t pause) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return 0;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_PAUSE;
    p.arg_u32 = pause != 0 ? 1u : 0u;
    if (scarlett_failed(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p))) {
        return 0;
    }
    return (int32_t)p.out_result_u32;
}

void SCARLETT_WIN64_API BinkGoto(void *bink_ptr, uint32_t frame, int32_t flags) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u) {
        return;
    }
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_GOTO;
    p.arg_u32 = frame;
    p.flags = (uint32_t)flags;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p);
    bink_sync_summary(bink);
}

void SCARLETT_WIN64_API BinkGetSummary(void *bink_ptr, scarlett_bink_summary_t *summary) {
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink == NULL || bink->in_use == 0u || summary == NULL) {
        return;
    }
    audio_pe_zero_mem(summary, sizeof(*summary));
    scarlett_bink2_op_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.bink_handle = bink->unix_bink_handle;
    p.op = (uint32_t)SCARLETT_BINK2_OP_GET_SUMMARY;
    p.summary_addr = (uint64_t)(uintptr_t)summary;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, &p);
}

static const scarlett_stub_descriptor_t g_stub_bink_set_iosize = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "Bink2Scarlett.dll",
    "BinkSetIOSize"
};

scarlett_hresult_t SCARLETT_WIN64_API BinkSetIOSize(uint64_t size) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_bink_set_iosize, ret, size, 0u, 0u, 0u);
}

void SCARLETT_WIN64_API BinkSetOSFileCallbacks(
    void *open_fn,
    void *read_fn,
    void *seek_fn,
    void *close_fn) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkSetOSFileCallbacks",
        ret,
        (uint64_t)(uintptr_t)open_fn,
        (uint64_t)(uintptr_t)read_fn,
        (uint64_t)(uintptr_t)seek_fn,
        (uint64_t)(uintptr_t)close_fn);
}

void SCARLETT_WIN64_API BinkSetMemory(void *alloc_fn, void *free_fn) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkSetMemory",
        ret,
        (uint64_t)(uintptr_t)alloc_fn,
        (uint64_t)(uintptr_t)free_fn,
        0u,
        0u);
}

int32_t SCARLETT_WIN64_API BinkSetSoundSystem(void *open_fn, uint64_t param) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkSetSoundSystem",
        ret,
        (uint64_t)(uintptr_t)open_fn,
        param,
        0u,
        0u);
    return 1;
}

void * SCARLETT_WIN64_API BinkOpenWithOptions(
    const void *name_or_mem,
    const void *options,
    uint32_t flags) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkOpenWithOptions",
        ret,
        (uint64_t)(uintptr_t)name_or_mem,
        (uint64_t)(uintptr_t)options,
        (uint64_t)flags,
        0u);
    return BinkOpen(name_or_mem, flags);
}

void SCARLETT_WIN64_API BinkGetPlatformInfo(
    int32_t bink_cpu_feature_flags,
    void *platform_info) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkGetPlatformInfo",
        ret,
        (uint64_t)(uint32_t)bink_cpu_feature_flags,
        (uint64_t)(uintptr_t)platform_info,
        0u,
        0u);
}

typedef struct scarlett_bink_framebuffers_hdr_s {
    int32_t TotalFrames;
    uint32_t YABufferWidth;
    uint32_t YABufferHeight;
    uint32_t cRcBBufferWidth;
    uint32_t cRcBBufferHeight;
    uint32_t FrameNum;
} scarlett_bink_framebuffers_hdr_t;

void SCARLETT_WIN64_API BinkGetFrameBuffersInfo(void *bink_ptr, void *fbset) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkGetFrameBuffersInfo",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        (uint64_t)(uintptr_t)fbset,
        0u,
        0u);
    if (fbset == NULL) {
        return;
    }
    scarlett_bink_framebuffers_hdr_t *hdr = (scarlett_bink_framebuffers_hdr_t *)fbset;
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    if (bink != NULL && bink->in_use != 0u && bink->Width > 0u && bink->Height > 0u) {
        hdr->TotalFrames = 1;
        hdr->YABufferWidth = bink->Width;
        hdr->YABufferHeight = bink->Height;
        hdr->cRcBBufferWidth = bink->Width / 2u;
        hdr->cRcBBufferHeight = bink->Height / 2u;
        hdr->FrameNum = bink->FrameNum;
    } else {
        audio_pe_zero_mem(hdr, sizeof(*hdr));
    }
}

void SCARLETT_WIN64_API BinkRegisterFrameBuffers(void *bink_ptr, void *fbset) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkRegisterFrameBuffers",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        (uint64_t)(uintptr_t)fbset,
        0u,
        0u);
}

int32_t SCARLETT_WIN64_API BinkDoFrameAsyncMulti(
    void *bink_ptr,
    uint32_t *threads,
    int32_t thread_count) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkDoFrameAsyncMulti",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        (uint64_t)(uintptr_t)threads,
        (uint64_t)(uint32_t)thread_count,
        0u);
    (void)BinkDoFrame(bink_ptr);
    return 1;
}

int32_t SCARLETT_WIN64_API BinkDoFrameAsyncWait(void *bink_ptr, int32_t us) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkDoFrameAsyncWait",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        (uint64_t)(uint32_t)us,
        0u,
        0u);
    return 1;
}

int32_t SCARLETT_WIN64_API BinkRequestStopAsyncThreadsMulti(
    uint32_t *threads,
    int32_t thread_count) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkRequestStopAsyncThreadsMulti",
        ret,
        (uint64_t)(uintptr_t)threads,
        (uint64_t)(uint32_t)thread_count,
        0u,
        0u);
    return 1;
}

int32_t SCARLETT_WIN64_API BinkWaitStopAsyncThreadsMulti(
    uint32_t *threads,
    int32_t thread_count) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkWaitStopAsyncThreadsMulti",
        ret,
        (uint64_t)(uintptr_t)threads,
        (uint64_t)(uint32_t)thread_count,
        0u,
        0u);
    return 1;
}

int32_t SCARLETT_WIN64_API BinkStartAsyncThread(int32_t thread_num, const void *param) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkStartAsyncThread",
        ret,
        (uint64_t)(uint32_t)thread_num,
        (uint64_t)(uintptr_t)param,
        0u,
        0u);
    return 1;
}

uint32_t SCARLETT_WIN64_API BinkGetKeyFrame(
    void *bink_ptr,
    uint32_t frame,
    int32_t flags) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkGetKeyFrame",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        (uint64_t)frame,
        (uint64_t)(uint32_t)flags,
        0u);
    scarlett_bink_handle_t *bink = (scarlett_bink_handle_t *)bink_ptr;
    uint32_t kf = frame > 0u ? frame : 1u;
    if (bink != NULL && bink->in_use != 0u && bink->Frames > 0u && kf > bink->Frames) {
        kf = bink->Frames;
    }
    return kf;
}

void SCARLETT_WIN64_API BinkSetWillLoop(void *bink_ptr, int32_t onoff) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkSetWillLoop",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        (uint64_t)(uint32_t)onoff,
        0u,
        0u);
}

int32_t SCARLETT_WIN64_API BinkShouldSkip(void *bink_ptr) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkShouldSkip",
        ret,
        (uint64_t)(uintptr_t)bink_ptr,
        0u,
        0u,
        0u);
    return 0;
}

void SCARLETT_WIN64_API BinkFreeGlobals(void) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "Bink2Scarlett.dll",
        "BinkFreeGlobals",
        ret,
        0u,
        0u,
        0u,
        0u);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
