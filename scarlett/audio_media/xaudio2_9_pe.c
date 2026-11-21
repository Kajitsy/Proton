/*
 * Proton-Scarlett — xaudio2_9.dll PE64 Freestanding Shim
 *
 * Strictly freestanding C11 (zero libc, zero <windows.h>, zero POSIX).
 * Crosses PE <-> audio_scarlett_unix.so ONLY via WINE_UNIX_CALL.
 */

#include "audio_pe_common.h"

#if !defined(WINE_UNIX_CALL) && !defined(_WIN32) && !defined(__MINGW32__)
static scarlett_audio_unix_bridge_fn_t g_audio_host_bridge = NULL;

void scarlett_audio_pe_set_unix_bridge(scarlett_audio_unix_bridge_fn_t bridge_fn) {
    g_audio_host_bridge = bridge_fn;
}

scarlett_audio_unix_bridge_fn_t scarlett_audio_pe_get_unix_bridge(void) {
    return g_audio_host_bridge;
}
#endif

/* ============================================================================
 * Freestanding Win64 COM Types matching xaudio2_9_xs.xdl
 * ============================================================================ */

typedef struct scarlett_xa2_voice_details_s {
    uint32_t CreationFlags;
    uint32_t ActiveFlags;
    uint32_t InputChannels;
    uint32_t InputSampleRate;
} scarlett_xa2_voice_details_t;

typedef struct scarlett_xa2_buffer_s {
    uint32_t Flags;
    uint32_t AudioBytes;
    const uint8_t *pAudioData;
    uint32_t PlayBegin;
    uint32_t PlayLength;
    uint32_t LoopBegin;
    uint32_t LoopLength;
    uint32_t LoopCount;
    uint32_t Reserved0;
    void *pContext;
} scarlett_xa2_buffer_t;

typedef struct scarlett_xa2_perf_data_s {
    uint64_t AudioCyclesSinceLastQuery;
    uint64_t TotalCyclesSinceLastQuery;
    uint32_t MinimumCyclesPerQuantum;
    uint32_t MaximumCyclesPerQuantum;
    uint32_t MemoryUsageInBytes;
    uint32_t CurrentLatencyInSamples;
    uint32_t GlitchesSinceEngineStarted;
    uint32_t ActiveSourceVoiceCount;
    uint32_t TotalSourceVoiceCount;
    uint32_t ActiveSubmixVoiceCount;
    uint32_t ActiveResamplerCount;
    uint32_t ActiveMatrixMixCount;
    uint32_t ActiveXmaSourceVoices;
    uint32_t ActiveXmaStreams;
} scarlett_xa2_perf_data_t;

typedef struct scarlett_waveformatex_s {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
} scarlett_waveformatex_t;

typedef struct scarlett_xa2_voice_obj_s scarlett_xa2_voice_obj_t;
typedef struct scarlett_xa2_engine_obj_s scarlett_xa2_engine_obj_t;

typedef struct scarlett_xa2_voice_vtbl_s {
    void (SCARLETT_WIN64_API *GetVoiceDetails)(scarlett_xa2_voice_obj_t *self,
                                               scarlett_xa2_voice_details_t *pVoiceDetails);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetVolume)(scarlett_xa2_voice_obj_t *self,
                                                       float Volume,
                                                       uint32_t OperationSet);
    void (SCARLETT_WIN64_API *GetVolume)(scarlett_xa2_voice_obj_t *self, float *pVolume);
    scarlett_hresult_t (SCARLETT_WIN64_API *SubmitSourceBuffer)(scarlett_xa2_voice_obj_t *self,
                                                                const scarlett_xa2_buffer_t *pBuffer,
                                                                const void *pBufferWMA);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetOutputVoices)(scarlett_xa2_voice_obj_t *self,
                                                             const void *pSendList);
    scarlett_hresult_t (SCARLETT_WIN64_API *Start)(scarlett_xa2_voice_obj_t *self,
                                                   uint32_t Flags,
                                                   uint32_t OperationSet);
    scarlett_hresult_t (SCARLETT_WIN64_API *Stop)(scarlett_xa2_voice_obj_t *self,
                                                  uint32_t Flags,
                                                  uint32_t OperationSet);
    scarlett_hresult_t (SCARLETT_WIN64_API *FlushSourceBuffers)(scarlett_xa2_voice_obj_t *self);
    void (SCARLETT_WIN64_API *DestroyVoice)(scarlett_xa2_voice_obj_t *self);
} scarlett_xa2_voice_vtbl_t;

struct scarlett_xa2_voice_obj_s {
    const scarlett_xa2_voice_vtbl_t *lpVtbl;
    uint64_t unix_voice_handle;
    uint32_t in_use;
    uint32_t creation_flags;
    uint32_t channels;
    uint32_t sample_rate;
};

typedef struct scarlett_xa2_engine_vtbl_s {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(scarlett_xa2_engine_obj_t *self,
                                                            const scarlett_guid_t *riid,
                                                            void **ppvObject);
    uint32_t (SCARLETT_WIN64_API *AddRef)(scarlett_xa2_engine_obj_t *self);
    uint32_t (SCARLETT_WIN64_API *Release)(scarlett_xa2_engine_obj_t *self);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterForCallbacks)(scarlett_xa2_engine_obj_t *self,
                                                                  void *pCallback);
    void (SCARLETT_WIN64_API *UnregisterForCallbacks)(scarlett_xa2_engine_obj_t *self,
                                                      void *pCallback);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateSourceVoice)(scarlett_xa2_engine_obj_t *self,
                                                               void **ppSourceVoice,
                                                               const void *pSourceFormat,
                                                               uint32_t Flags,
                                                               float MaxFrequencyRatio,
                                                               void *pCallback,
                                                               const void *pSendList,
                                                               const void *pEffectChain);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateMasteringVoice)(scarlett_xa2_engine_obj_t *self,
                                                                  void **ppMasteringVoice,
                                                                  uint32_t InputChannels,
                                                                  uint32_t InputSampleRate,
                                                                  uint32_t Flags,
                                                                  const void *szDeviceId,
                                                                  const void *pEffectChain,
                                                                  uint32_t StreamCategory);
    scarlett_hresult_t (SCARLETT_WIN64_API *StartEngine)(scarlett_xa2_engine_obj_t *self);
    void (SCARLETT_WIN64_API *StopEngine)(scarlett_xa2_engine_obj_t *self);
    scarlett_hresult_t (SCARLETT_WIN64_API *CommitChanges)(scarlett_xa2_engine_obj_t *self,
                                                           uint32_t OperationSet);
    void (SCARLETT_WIN64_API *GetPerformanceData)(scarlett_xa2_engine_obj_t *self,
                                                  scarlett_xa2_perf_data_t *pPerfData);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetDebugConfiguration)(scarlett_xa2_engine_obj_t *self,
                                                                   const void *pDebugConfiguration,
                                                                   void *pReserved);
} scarlett_xa2_engine_vtbl_t;

struct scarlett_xa2_engine_obj_s {
    const scarlett_xa2_engine_vtbl_t *lpVtbl;
    uint64_t unix_engine_handle;
    uint32_t ref_count;
    uint32_t in_use;
    void *callbacks[8];
};

#define SCARLETT_XA2_MAX_ENGINES 8u
#define SCARLETT_XA2_MAX_VOICES  128u

static scarlett_xa2_engine_obj_t g_engines[SCARLETT_XA2_MAX_ENGINES];
static scarlett_xa2_voice_obj_t  g_voices[SCARLETT_XA2_MAX_VOICES];

/* IID_IXAudio2 = {2B02E3CF-2E0B-4EC3-BE45-1B2A3FE7210D} */
static const scarlett_guid_t kIidIXAudio2 = {
    UINT32_C(0x2B02E3CF), UINT16_C(0x2E0B), UINT16_C(0x4EC3),
    { 0xBEu, 0x45u, 0x1Bu, 0x2Au, 0x3Fu, 0xE7u, 0x21u, 0x0Du }
};

/* IID_IUnknown = {00000000-0000-0000-C000-000000000046} */
static const scarlett_guid_t kIidIUnknown = {
    UINT32_C(0x00000000), UINT16_C(0x0000), UINT16_C(0x0000),
    { 0xC0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u }
};

static bool guid_eq(const scarlett_guid_t *a, const scarlett_guid_t *b) {
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

/* ============================================================================
 * IXAudio2Voice Methods
 * ============================================================================ */

static void SCARLETT_WIN64_API voice_get_details(
    scarlett_xa2_voice_obj_t *self,
    scarlett_xa2_voice_details_t *pVoiceDetails) {
    if (self == NULL || pVoiceDetails == NULL) {
        return;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.ctrl_op = (uint32_t)SCARLETT_VOICE_CTRL_GET_DETAILS;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &p);
    pVoiceDetails->CreationFlags = self->creation_flags;
    pVoiceDetails->ActiveFlags = self->creation_flags;
    pVoiceDetails->InputChannels = p.out_channels != 0u ? p.out_channels : self->channels;
    pVoiceDetails->InputSampleRate = p.out_sample_rate != 0u ? p.out_sample_rate : self->sample_rate;
}

static scarlett_hresult_t SCARLETT_WIN64_API voice_set_volume(
    scarlett_xa2_voice_obj_t *self,
    float Volume,
    uint32_t OperationSet) {
    (void)OperationSet;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.ctrl_op = (uint32_t)SCARLETT_VOICE_CTRL_SET_VOLUME;
    p.param_f0 = Volume;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &p);
}

static void SCARLETT_WIN64_API voice_get_volume(
    scarlett_xa2_voice_obj_t *self,
    float *pVolume) {
    if (self == NULL || pVolume == NULL) {
        return;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.ctrl_op = (uint32_t)SCARLETT_VOICE_CTRL_GET_VOLUME;
    if (scarlett_succeeded(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &p))) {
        *pVolume = p.param_f0;
    } else {
        *pVolume = 0.0f;
    }
}

static scarlett_hresult_t SCARLETT_WIN64_API voice_submit_source_buffer(
    scarlett_xa2_voice_obj_t *self,
    const scarlett_xa2_buffer_t *pBuffer,
    const void *pBufferWMA) {
    (void)pBufferWMA;
    if (self == NULL || pBuffer == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_voice_buffer_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.buffer_addr = (uint64_t)(uintptr_t)pBuffer->pAudioData;
    p.buffer_bytes = pBuffer->AudioBytes;
    p.flags = pBuffer->Flags;
    p.play_begin = pBuffer->PlayBegin;
    p.play_length = pBuffer->PlayLength;
    p.loop_count = pBuffer->LoopCount;
    p.packet_count = 1u;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_SUBMIT_VOICE_BUFFER, &p);
}

static const scarlett_stub_descriptor_t g_stub_voice_set_output_voices = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_XDL_VTABLE,
    SCARLETT_STUB_ORDINAL_NONE,
    4u,
    SCARLETT_E_NOTIMPL,
    0u,
    "xaudio2_9.dll",
    "IXAudio2Voice::SetOutputVoices"
};

static scarlett_hresult_t SCARLETT_WIN64_API voice_set_output_voices(
    scarlett_xa2_voice_obj_t *self,
    const void *pSendList) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_voice_set_output_voices,
        ret,
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pSendList,
        0u,
        0u);
}

static scarlett_hresult_t SCARLETT_WIN64_API voice_start(
    scarlett_xa2_voice_obj_t *self,
    uint32_t Flags,
    uint32_t OperationSet) {
    (void)Flags;
    (void)OperationSet;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.ctrl_op = (uint32_t)SCARLETT_VOICE_CTRL_START;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &p);
}

static scarlett_hresult_t SCARLETT_WIN64_API voice_stop(
    scarlett_xa2_voice_obj_t *self,
    uint32_t Flags,
    uint32_t OperationSet) {
    (void)Flags;
    (void)OperationSet;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.ctrl_op = (uint32_t)SCARLETT_VOICE_CTRL_STOP;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &p);
}

static scarlett_hresult_t SCARLETT_WIN64_API voice_flush(scarlett_xa2_voice_obj_t *self) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    p.ctrl_op = (uint32_t)SCARLETT_VOICE_CTRL_FLUSH;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &p);
}

static void SCARLETT_WIN64_API voice_destroy(scarlett_xa2_voice_obj_t *self) {
    if (self == NULL || self->in_use == 0u) {
        return;
    }
    scarlett_audio_voice_ctrl_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.voice_handle = self->unix_voice_handle;
    (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_DESTROY_VOICE, &p);
    self->unix_voice_handle = 0u;
    self->in_use = 0u;
}

static const scarlett_xa2_voice_vtbl_t g_voice_vtbl = {
    voice_get_details,
    voice_set_volume,
    voice_get_volume,
    voice_submit_source_buffer,
    voice_set_output_voices,
    voice_start,
    voice_stop,
    voice_flush,
    voice_destroy
};

/* ============================================================================
 * IXAudio2 Methods
 * ============================================================================ */

static scarlett_hresult_t SCARLETT_WIN64_API xa2_query_interface(
    scarlett_xa2_engine_obj_t *self,
    const scarlett_guid_t *riid,
    void **ppvObject) {
    if (self == NULL || ppvObject == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (guid_eq(riid, &kIidIXAudio2) || guid_eq(riid, &kIidIUnknown)) {
        self->ref_count += 1u;
        *ppvObject = self;
        return SCARLETT_OK;
    }
    *ppvObject = NULL;
    return SCARLETT_E_NOINTERFACE;
}

static uint32_t SCARLETT_WIN64_API xa2_add_ref(scarlett_xa2_engine_obj_t *self) {
    if (self == NULL) {
        return 0u;
    }
    self->ref_count += 1u;
    return self->ref_count;
}

static uint32_t SCARLETT_WIN64_API xa2_release(scarlett_xa2_engine_obj_t *self) {
    if (self == NULL || self->ref_count == 0u) {
        return 0u;
    }
    self->ref_count -= 1u;
    if (self->ref_count == 0u) {
        scarlett_audio_engine_create_params_t p;
        audio_pe_zero_mem(&p, sizeof(p));
        p.out_engine_handle = self->unix_engine_handle;
        (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_DESTROY_ENGINE, &p);
        self->unix_engine_handle = 0u;
        self->in_use = 0u;
        return 0u;
    }
    return self->ref_count;
}

static scarlett_hresult_t SCARLETT_WIN64_API xa2_register_callbacks(
    scarlett_xa2_engine_obj_t *self,
    void *pCallback) {
    if (self == NULL || pCallback == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    for (size_t i = 0u; i < 8u; ++i) {
        if (self->callbacks[i] == NULL || self->callbacks[i] == pCallback) {
            self->callbacks[i] = pCallback;
            return SCARLETT_OK;
        }
    }
    return SCARLETT_E_OUTOFMEMORY;
}

static void SCARLETT_WIN64_API xa2_unregister_callbacks(
    scarlett_xa2_engine_obj_t *self,
    void *pCallback) {
    if (self == NULL || pCallback == NULL) {
        return;
    }
    for (size_t i = 0u; i < 8u; ++i) {
        if (self->callbacks[i] == pCallback) {
            self->callbacks[i] = NULL;
        }
    }
}

static scarlett_hresult_t SCARLETT_WIN64_API xa2_create_source_voice(
    scarlett_xa2_engine_obj_t *self,
    void **ppSourceVoice,
    const void *pSourceFormat,
    uint32_t Flags,
    float MaxFrequencyRatio,
    void *pCallback,
    const void *pSendList,
    const void *pEffectChain) {
    (void)MaxFrequencyRatio;
    (void)pCallback;
    (void)pSendList;
    (void)pEffectChain;
    if (self == NULL || ppSourceVoice == NULL || pSourceFormat == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    *ppSourceVoice = NULL;

    scarlett_xa2_voice_obj_t *slot = NULL;
    for (size_t i = 0u; i < SCARLETT_XA2_MAX_VOICES; ++i) {
        if (g_voices[i].in_use == 0u) {
            slot = &g_voices[i];
            break;
        }
    }
    if (slot == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    const scarlett_waveformatex_t *wfx = (const scarlett_waveformatex_t *)pSourceFormat;
    scarlett_audio_voice_create_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.engine_handle = self->unix_engine_handle;
    p.voice_kind = (uint32_t)SCARLETT_VOICE_SOURCE;
    p.format_tag = wfx->wFormatTag;
    p.channels = wfx->nChannels;
    p.sample_rate_hz = wfx->nSamplesPerSec;
    p.block_align = wfx->nBlockAlign;
    p.bits_per_sample = wfx->wBitsPerSample;
    p.flags = Flags;
    if (wfx->cbSize > 0u) {
        p.extradata_addr = (uint64_t)(uintptr_t)((const uint8_t *)pSourceFormat + sizeof(*wfx));
        p.extradata_bytes = wfx->cbSize;
    }

    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE, &p);
    if (scarlett_failed(hr)) {
        return hr;
    }

    audio_pe_zero_mem(slot, sizeof(*slot));
    slot->lpVtbl = &g_voice_vtbl;
    slot->unix_voice_handle = p.out_voice_handle;
    slot->in_use = 1u;
    slot->creation_flags = Flags;
    slot->channels = wfx->nChannels;
    slot->sample_rate = wfx->nSamplesPerSec;
    *ppSourceVoice = slot;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API xa2_create_mastering_voice(
    scarlett_xa2_engine_obj_t *self,
    void **ppMasteringVoice,
    uint32_t InputChannels,
    uint32_t InputSampleRate,
    uint32_t Flags,
    const void *szDeviceId,
    const void *pEffectChain,
    uint32_t StreamCategory) {
    (void)szDeviceId;
    (void)pEffectChain;
    (void)StreamCategory;
    if (self == NULL || ppMasteringVoice == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    *ppMasteringVoice = NULL;

    scarlett_xa2_voice_obj_t *slot = NULL;
    for (size_t i = 0u; i < SCARLETT_XA2_MAX_VOICES; ++i) {
        if (g_voices[i].in_use == 0u) {
            slot = &g_voices[i];
            break;
        }
    }
    if (slot == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_audio_voice_create_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.engine_handle = self->unix_engine_handle;
    p.voice_kind = (uint32_t)SCARLETT_VOICE_MASTERING;
    p.format_tag = SCARLETT_AUDIO_FORMAT_IEEE_FLOAT;
    p.channels = InputChannels == 0u ? 2u : InputChannels;
    p.sample_rate_hz = InputSampleRate == 0u ? 48000u : InputSampleRate;
    p.flags = Flags;

    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE, &p);
    if (scarlett_failed(hr)) {
        return hr;
    }

    audio_pe_zero_mem(slot, sizeof(*slot));
    slot->lpVtbl = &g_voice_vtbl;
    slot->unix_voice_handle = p.out_voice_handle;
    slot->in_use = 1u;
    slot->creation_flags = Flags;
    slot->channels = p.channels;
    slot->sample_rate = p.sample_rate_hz;
    *ppMasteringVoice = slot;
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API xa2_start_engine(scarlett_xa2_engine_obj_t *self) {
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    return SCARLETT_OK;
}

static void SCARLETT_WIN64_API xa2_stop_engine(scarlett_xa2_engine_obj_t *self) {
    (void)self;
}

static scarlett_hresult_t SCARLETT_WIN64_API xa2_commit_changes(
    scarlett_xa2_engine_obj_t *self,
    uint32_t OperationSet) {
    (void)OperationSet;
    if (self == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_render_quantum_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.engine_handle = self->unix_engine_handle;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM, &p);
}

static void SCARLETT_WIN64_API xa2_get_performance_data(
    scarlett_xa2_engine_obj_t *self,
    scarlett_xa2_perf_data_t *pPerfData) {
    if (self == NULL || pPerfData == NULL) {
        return;
    }
    audio_pe_zero_mem(pPerfData, sizeof(*pPerfData));
    scarlett_audio_render_quantum_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.engine_handle = self->unix_engine_handle;
    if (scarlett_succeeded(audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM, &p))) {
        pPerfData->TotalCyclesSinceLastQuery = p.out_total_cycles;
        pPerfData->AudioCyclesSinceLastQuery = p.out_total_cycles;
        pPerfData->GlitchesSinceEngineStarted = p.out_xruns;
        pPerfData->ActiveSourceVoiceCount = p.out_active_voices;
        pPerfData->TotalSourceVoiceCount = p.out_active_voices;
        pPerfData->ActiveResamplerCount = p.out_resamplers;
        pPerfData->ActiveXmaSourceVoices = p.out_xma_voices;
        pPerfData->ActiveXmaStreams = p.out_xma_voices;
        pPerfData->CurrentLatencyInSamples = SCARLETT_AUDIO_QUANTUM_FRAMES;
    }
}

static const scarlett_stub_descriptor_t g_stub_xa2_set_debug_cfg = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_XDL_VTABLE,
    SCARLETT_STUB_ORDINAL_NONE,
    11u,
    SCARLETT_E_NOTIMPL,
    0u,
    "xaudio2_9.dll",
    "IXAudio2::SetDebugConfiguration"
};

static scarlett_hresult_t SCARLETT_WIN64_API xa2_set_debug_configuration(
    scarlett_xa2_engine_obj_t *self,
    const void *pDebugConfiguration,
    void *pReserved) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_xa2_set_debug_cfg,
        ret,
        (uint64_t)(uintptr_t)self,
        (uint64_t)(uintptr_t)pDebugConfiguration,
        (uint64_t)(uintptr_t)pReserved,
        0u);
}

static const scarlett_xa2_engine_vtbl_t g_engine_vtbl = {
    xa2_query_interface,
    xa2_add_ref,
    xa2_release,
    xa2_register_callbacks,
    xa2_unregister_callbacks,
    xa2_create_source_voice,
    xa2_create_mastering_voice,
    xa2_start_engine,
    xa2_stop_engine,
    xa2_commit_changes,
    xa2_get_performance_data,
    xa2_set_debug_configuration
};

/* ============================================================================
 * Exported DLL Functions (Ordinal #1 XAudio2Create + Diagnostic Auto-Stubs)
 * ============================================================================ */

scarlett_hresult_t SCARLETT_WIN64_API XAudio2Create(
    void **ppXAudio2,
    uint32_t Flags,
    uint32_t XAudio2Processor) {
    (void)Flags;
    (void)XAudio2Processor;
    if (ppXAudio2 == NULL) {
        return SCARLETT_E_POINTER;
    }
    *ppXAudio2 = NULL;

    scarlett_xa2_engine_obj_t *eng = NULL;
    for (size_t i = 0u; i < SCARLETT_XA2_MAX_ENGINES; ++i) {
        if (g_engines[i].in_use == 0u) {
            eng = &g_engines[i];
            break;
        }
    }
    if (eng == NULL) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    scarlett_audio_engine_create_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.sink_mode = (uint32_t)SCARLETT_AUDIO_SINK_AUTO;
    p.sample_rate_hz = 48000u;
    p.channels = 2u;

    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_CREATE_ENGINE, &p);
    if (scarlett_failed(hr)) {
        return hr;
    }

    audio_pe_zero_mem(eng, sizeof(*eng));
    eng->lpVtbl = &g_engine_vtbl;
    eng->unix_engine_handle = p.out_engine_handle;
    eng->ref_count = 1u;
    eng->in_use = 1u;
    *ppXAudio2 = eng;
    return SCARLETT_OK;
}

static const scarlett_stub_descriptor_t g_stub_create_vol_meter = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_ORDINAL_EXPORT,
    2u,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_E_NOTIMPL,
    0u,
    "xaudio2_9.dll",
    "CreateAudioVolumeMeter"
};

scarlett_hresult_t SCARLETT_WIN64_API CreateAudioVolumeMeter(void **ppApo) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_create_vol_meter, ret, (uint64_t)(uintptr_t)ppApo, 0u, 0u, 0u);
}

static const scarlett_stub_descriptor_t g_stub_create_reverb = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_ORDINAL_EXPORT,
    3u,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_E_NOTIMPL,
    0u,
    "xaudio2_9.dll",
    "CreateAudioReverb"
};

scarlett_hresult_t SCARLETT_WIN64_API CreateAudioReverb(void **ppApo) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_create_reverb, ret, (uint64_t)(uintptr_t)ppApo, 0u, 0u, 0u);
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
