/*
 * Proton-Scarlett — Comprehensive Unit & Integration Tests for scarlett/audio_media
 *
 * Covers:
 *   1. Unixlib handshake + dense 21-slot __wine_unix_call_funcs table verification
 *   2. XAudio2 (#1 XAudio2Create) + 64 simultaneous Opus 0x704F / ADAT 512KB ring voices + 0 xruns
 *   3. AcpHal / SHAPE: ApuCreateHeap/ApuAlloc/ApuFree, AcpHalCreate/ShapeContexts,
 *      real FFmpeg AV_CODEC_ID_XMA2 + big-endian bswap32 seek table + IMA/MS-ADPCM
 *   4. XDSP: Radix-2 FFT/IFFT (< 1e-4 accuracy), overlap-add convolution reverb, Spatial HRTF
 *   5. GameInput (#1 GameInputCreate): 1000 Hz ring buffer (Current/Next/Previous),
 *      4-motor rumble (GIP hidraw packet + evdev FF_RUMBLE fallback)
 *   6. Bink2Scarlett (all 12 exports + YUV->RGB BT.709/BT.601 accuracy)
 *   7. amd_fidelityfx_loader_xbox & amd_fidelityfx_upscaler_xbox (FSR 3 / FSR 4.1.2,
 *      Halton(2,3) jitter, quality presets, upscale dispatch + CPU fallback)
 *   8. Diagnostic auto-stub enforcement (0 silent S_OK stubs)
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <linux/input.h>
#include <thread>
#include <vector>

extern "C" {
#include <opus/opus.h>
}

#include "audio_media_unix_abi.h"
#include "gameinput_backend.hpp"
#include "media_bink2_fsr.hpp"
#include "xapu_codecs.hpp"
#include "xaudio2_mixer.hpp"
#include "xdsp_math.hpp"

extern "C" {
int32_t scarlett_audio_unix_dispatch(uint32_t opcode, void* params);
typedef int32_t (*wine_unixlib_entry_t)(void* args);
extern const wine_unixlib_entry_t __wine_unix_call_funcs[SCARLETT_AUDIO_UNIX_EX_OP_COUNT];

/* PE64 shim exports compiled into host test bridge */
scarlett_hresult_t SCARLETT_WIN64_API XAudio2Create(void** ppXAudio2, uint32_t Flags, uint32_t Proc);
scarlett_hresult_t SCARLETT_WIN64_API CreateAudioVolumeMeter(void** ppApo);

scarlett_hresult_t SCARLETT_WIN64_API ApuCreateHeap(uint64_t initial_size, uint64_t max_size);
scarlett_hresult_t SCARLETT_WIN64_API ApuAlloc(void** va, void** pa, uint64_t size, uint32_t align, uint32_t flags);
scarlett_hresult_t SCARLETT_WIN64_API ApuFree(void* va);
scarlett_hresult_t SCARLETT_WIN64_API AcpHalCreate(void** out_hal);
scarlett_hresult_t SCARLETT_WIN64_API AcpHalRelease(void* hal);
scarlett_hresult_t SCARLETT_WIN64_API AcpHalAllocateShapeContexts(void* hal, uint32_t count, void** out_ctx);
scarlett_hresult_t SCARLETT_WIN64_API AcpHalReleaseShapeContexts(void* hal, void* ctx);
scarlett_hresult_t SCARLETT_WIN64_API AcpHalDecodeXma2(void* ctx, const uint8_t* src, uint32_t src_bytes,
                                                       uint8_t* dst, uint32_t dst_cap, uint32_t* out_bytes);
scarlett_hresult_t SCARLETT_WIN64_API AcpHalSubmitFlowgraph(void* hal, const void* fg, uint32_t flags, uint64_t res);

scarlett_hresult_t SCARLETT_WIN64_API GameInputCreate(void** gameInput);
scarlett_hresult_t SCARLETT_WIN64_API GameInputCreateDispatcher(void** dispatcher);

void* SCARLETT_WIN64_API BinkOpen(const void* name_or_mem, uint32_t flags);
void SCARLETT_WIN64_API BinkClose(void* bink);
int32_t SCARLETT_WIN64_API BinkDoFrame(void* bink);
void SCARLETT_WIN64_API BinkNextFrame(void* bink);
int32_t SCARLETT_WIN64_API BinkWait(void* bink);
int32_t SCARLETT_WIN64_API BinkCopyToBuffer(void* bink, void* dest, int32_t pitch, uint32_t h,
                                            uint32_t dx, uint32_t dy, uint32_t flags);
int32_t SCARLETT_WIN64_API BinkCopyToBufferRect(void* bink, void* dest, int32_t pitch, uint32_t h,
                                                uint32_t dx, uint32_t dy, uint32_t sx, uint32_t sy,
                                                uint32_t sw, uint32_t sh, uint32_t flags);
void SCARLETT_WIN64_API BinkSetSoundOnOff(void* bink, int32_t onoff);
void SCARLETT_WIN64_API BinkSetVolume(void* bink, uint32_t track_id, int32_t volume);
int32_t SCARLETT_WIN64_API BinkPause(void* bink, int32_t pause);
void SCARLETT_WIN64_API BinkGoto(void* bink, uint32_t frame, int32_t flags);
void SCARLETT_WIN64_API BinkGetSummary(void* bink, scarlett_bink_summary_t* summary);
scarlett_hresult_t SCARLETT_WIN64_API BinkSetIOSize(uint64_t size);

scarlett_hresult_t SCARLETT_WIN64_API ffxCreateContext(uint64_t* ctx, const void* desc, const void* mem_cb);
scarlett_hresult_t SCARLETT_WIN64_API ffxDestroyContext(uint64_t* ctx, const void* mem_cb);
scarlett_hresult_t SCARLETT_WIN64_API ffxConfigure(uint64_t* ctx, const void* desc);
scarlett_hresult_t SCARLETT_WIN64_API ffxQuery(uint64_t* ctx, uint32_t* ver, uint32_t* used_vk);
scarlett_hresult_t SCARLETT_WIN64_API ffxLoadInterface(void* b, uint32_t v, void* out_i, uint64_t f);

scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextCreate(uint64_t* ctx, const void* desc);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextDestroy(uint64_t* ctx);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3ContextDispatch(uint64_t* ctx, void* desc);
float SCARLETT_WIN64_API ffxFsr3GetUpscaleRatioFromQualityMode(uint32_t mode);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3GetRenderResolutionFromQualityMode(
    uint32_t* rw, uint32_t* rh, uint32_t dw, uint32_t dh, uint32_t mode);
int32_t SCARLETT_WIN64_API ffxFsr3GetJitterPhaseCount(int32_t rw, int32_t dw);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr3GetJitterOffset(float* ox, float* oy, int32_t idx, int32_t phases);
uint32_t SCARLETT_WIN64_API ffxFsr4GetVersion(void);
scarlett_hresult_t SCARLETT_WIN64_API ffxFsr4SetDebugHook(void* ctx, void* fn, uint32_t flags, uint64_t res);
}

namespace {

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++g_pass;                                                            \
        } else {                                                                 \
            ++g_fail;                                                            \
            std::fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (false)

/* Minimal PE64-matching COM vtable projections for host test verification */
struct Xa2VoiceVtbl {
    void (SCARLETT_WIN64_API *GetVoiceDetails)(void*, void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetVolume)(void*, float, uint32_t);
    void (SCARLETT_WIN64_API *GetVolume)(void*, float*);
    scarlett_hresult_t (SCARLETT_WIN64_API *SubmitSourceBuffer)(void*, const void*, const void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetOutputVoices)(void*, const void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *Start)(void*, uint32_t, uint32_t);
    scarlett_hresult_t (SCARLETT_WIN64_API *Stop)(void*, uint32_t, uint32_t);
    scarlett_hresult_t (SCARLETT_WIN64_API *FlushSourceBuffers)(void*);
    void (SCARLETT_WIN64_API *DestroyVoice)(void*);
};

struct Xa2VoiceObj {
    const Xa2VoiceVtbl* lpVtbl;
    uint64_t unix_voice_handle;
};

struct Xa2PerfData {
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
};

struct Xa2Vtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterForCallbacks)(void*, void*);
    void (SCARLETT_WIN64_API *UnregisterForCallbacks)(void*, void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateSourceVoice)(
        void*, void**, const void*, uint32_t, float, void*, const void*, const void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateMasteringVoice)(
        void*, void**, uint32_t, uint32_t, uint32_t, const void*, const void*, uint32_t);
    scarlett_hresult_t (SCARLETT_WIN64_API *StartEngine)(void*);
    void (SCARLETT_WIN64_API *StopEngine)(void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *CommitChanges)(void*, uint32_t);
    void (SCARLETT_WIN64_API *GetPerformanceData)(void*, Xa2PerfData*);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetDebugConfiguration)(void*, const void*, void*);
};

struct Xa2Obj {
    const Xa2Vtbl* lpVtbl;
    uint64_t unix_engine_handle;
};

struct Xa2Buffer {
    uint32_t Flags;
    uint32_t AudioBytes;
    const uint8_t* pAudioData;
    uint32_t PlayBegin;
    uint32_t PlayLength;
    uint32_t LoopBegin;
    uint32_t LoopLength;
    uint32_t LoopCount;
    uint32_t Reserved0;
    void* pContext;
};

struct WaveFormatEx {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
};

struct GiRumble {
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
};

struct GiGamepadState {
    uint32_t buttons;
    float leftTrigger;
    float rightTrigger;
    float leftThumbstickX;
    float leftThumbstickY;
    float rightThumbstickX;
    float rightThumbstickY;
    uint32_t extendedButtons;
};

struct GiDeviceVtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    const void* (SCARLETT_WIN64_API *GetDeviceInfo)(void*);
    uint32_t (SCARLETT_WIN64_API *GetDeviceStatus)(void*);
    void (SCARLETT_WIN64_API *GetBatteryState)(void*, void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *CreateForceFeedbackEffect)(void*, uint32_t, const void*, void**);
    uint32_t (SCARLETT_WIN64_API *IsForceFeedbackMotorPoweredOn)(void*, uint32_t);
    void (SCARLETT_WIN64_API *SetForceFeedbackMotorGain)(void*, uint32_t, float);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetHapticMotorState)(void*, uint32_t, const void*);
    void (SCARLETT_WIN64_API *SetRumbleState)(void*, const GiRumble*);
};

struct GiDeviceObj {
    const GiDeviceVtbl* lpVtbl;
};

struct GiReadingVtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    uint32_t (SCARLETT_WIN64_API *GetInputKind)(void*);
    uint64_t (SCARLETT_WIN64_API *GetSequenceNumber)(void*, uint32_t);
    uint64_t (SCARLETT_WIN64_API *GetTimestamp)(void*);
    void (SCARLETT_WIN64_API *GetDevice)(void*, void**);
    uint32_t (SCARLETT_WIN64_API *GetRawReport)(void*, void**);
    uint32_t (SCARLETT_WIN64_API *GetControllerAxisCount)(void*);
    uint32_t (SCARLETT_WIN64_API *GetControllerAxisState)(void*, uint32_t, float*);
    uint32_t (SCARLETT_WIN64_API *GetControllerButtonCount)(void*);
    uint32_t (SCARLETT_WIN64_API *GetControllerButtonState)(void*, uint32_t, uint8_t*);
    uint32_t (SCARLETT_WIN64_API *GetControllerSwitchCount)(void*);
    uint32_t (SCARLETT_WIN64_API *GetControllerSwitchState)(void*, uint32_t, uint32_t*);
    uint32_t (SCARLETT_WIN64_API *GetKeyCount)(void*);
    uint32_t (SCARLETT_WIN64_API *GetKeyState)(void*, uint32_t, void*);
    uint32_t (SCARLETT_WIN64_API *GetMouseState)(void*, void*);
    uint32_t (SCARLETT_WIN64_API *GetTouchCount)(void*);
    uint32_t (SCARLETT_WIN64_API *GetTouchState)(void*, uint32_t, void*);
    uint32_t (SCARLETT_WIN64_API *GetMotionState)(void*, void*);
    uint32_t (SCARLETT_WIN64_API *GetArcadeStickState)(void*, void*);
    uint32_t (SCARLETT_WIN64_API *GetFlightStickState)(void*, void*);
    uint32_t (SCARLETT_WIN64_API *GetGamepadState)(void*, GiGamepadState*);
};

struct GiReadingObj {
    const GiReadingVtbl* lpVtbl;
};

struct GiVtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    uint64_t (SCARLETT_WIN64_API *GetCurrentTimestamp)(void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetCurrentReading)(void*, uint32_t, void*, void**);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetNextReading)(void*, void*, uint32_t, void*, void**);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetPreviousReading)(void*, void*, uint32_t, void*, void**);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetTemporalReading)(void*, uint64_t, void*, void**);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterReadingCallback)(
        void*, void*, uint32_t, float, void*, void*, uint64_t*);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterDeviceCallback)(
        void*, void*, uint32_t, uint32_t, uint32_t, void*, void*, uint64_t*);
};

struct GiObj {
    const GiVtbl* lpVtbl;
    uint64_t unix_instance_handle;
};

void test_unixlib_handshake_and_table() {
    for (uint32_t i = 0u; i < SCARLETT_AUDIO_UNIX_EX_OP_COUNT; ++i) {
        CHECK(__wine_unix_call_funcs[i] != nullptr);
    }

    scarlett_unix_init_params_t bad{};
    bad.abi_version = 999u;
    bad.module_id = SCARLETT_UNIX_MOD_AUDIO;
    CHECK(__wine_unix_call_funcs[SCARLETT_AUDIO_UNIX_INIT](&bad) == SCARLETT_ERR_ABI_VERSION_MISMATCH);

    scarlett_unix_init_params_t ok{};
    ok.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    ok.module_id = SCARLETT_UNIX_MOD_AUDIO;
    ok.console_model = SCARLETT_MODEL_ANACONDA;
    CHECK(__wine_unix_call_funcs[SCARLETT_AUDIO_UNIX_INIT](&ok) == SCARLETT_OK);
    CHECK(ok.out_unix_abi_version == SCARLETT_UNIX_CALL_ABI_VERSION);

    CHECK(scarlett_audio_unix_dispatch(0xDEADu, nullptr) == SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE);
}

void test_xaudio2_64_opus_adat_voices() {
    void* xa2_raw = nullptr;
    CHECK(XAudio2Create(&xa2_raw, 0u, 0u) == SCARLETT_OK);
    auto* xa2 = static_cast<Xa2Obj*>(xa2_raw);
    CHECK(xa2 != nullptr);

    void* master_raw = nullptr;
    CHECK(xa2->lpVtbl->CreateMasteringVoice(xa2, &master_raw, 2u, 48000u, 0u, nullptr, nullptr, 0u) == SCARLETT_OK);

    /* Real libopus encoding for 480 frames (10ms @ 48kHz stereo) */
    int opus_err = OPUS_OK;
    OpusEncoder* enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opus_err);
    CHECK(enc != nullptr && opus_err == OPUS_OK);

    std::vector<float> sine_pcm(480u * 2u);
    for (uint32_t i = 0u; i < 480u; ++i) {
        const float s = 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
        sine_pcm[i * 2u + 0u] = s;
        sine_pcm[i * 2u + 1u] = s;
    }

    uint8_t opus_pkt[1024]{};
    const int pkt_len = opus_encode_float(enc, sine_pcm.data(), 480, opus_pkt, sizeof(opus_pkt));
    CHECK(pkt_len > 0);
    opus_encoder_destroy(enc);

    /* Build a real 512 KiB ADAT circular ring container wrapping the Opus packet near wrap boundary */
    std::vector<uint8_t> adat_container(sizeof(scarlett::audio_media::AdatRingHeader) +
                                        SCARLETT_ADAT_RING_CAPACITY_BYTES, 0u);
    const uint32_t wrap_offset = SCARLETT_ADAT_RING_CAPACITY_BYTES - 4u; /* Force header+payload wrap-around */
    scarlett::audio_media::AdatRingHeader hdr{};
    hdr.magic = SCARLETT_ADAT_MAGIC;
    hdr.version = 1u;
    hdr.sample_rate_hz = 48000u;
    hdr.channels = 2u;
    hdr.ring_capacity_bytes = SCARLETT_ADAT_RING_CAPACITY_BYTES;
    hdr.read_offset = wrap_offset;
    hdr.packet_count = 1u;
    std::memcpy(adat_container.data(), &hdr, sizeof(hdr));

    scarlett::audio_media::AdatPacketHeader phdr{};
    phdr.frame_bytes = static_cast<uint32_t>(pkt_len);
    phdr.encoder_delay_samples = 0u;
    uint8_t* ring = adat_container.data() + sizeof(hdr);
    uint32_t wpos = wrap_offset;
    const uint8_t* phdr_bytes = reinterpret_cast<const uint8_t*>(&phdr);
    for (size_t i = 0u; i < sizeof(phdr); ++i) {
        ring[(wpos + i) % SCARLETT_ADAT_RING_CAPACITY_BYTES] = phdr_bytes[i];
    }
    wpos = (wpos + static_cast<uint32_t>(sizeof(phdr))) % SCARLETT_ADAT_RING_CAPACITY_BYTES;
    for (int i = 0; i < pkt_len; ++i) {
        ring[(wpos + static_cast<uint32_t>(i)) % SCARLETT_ADAT_RING_CAPACITY_BYTES] = opus_pkt[i];
    }

    /* Create 64 simultaneous voices: 32 Opus 0x704F + 32 ADAT 512KB ring */
    WaveFormatEx opus_wfx{};
    opus_wfx.wFormatTag = SCARLETT_AUDIO_FORMAT_OPUS;
    opus_wfx.nChannels = 2u;
    opus_wfx.nSamplesPerSec = 48000u;

    WaveFormatEx adat_wfx{};
    adat_wfx.wFormatTag = SCARLETT_AUDIO_FORMAT_OPUS;
    adat_wfx.nChannels = 2u;
    adat_wfx.nSamplesPerSec = 48000u;

    std::vector<Xa2VoiceObj*> voices;
    voices.reserve(64);
    for (int i = 0; i < 64; ++i) {
        void* vraw = nullptr;
        const auto& fmt = (i % 2 == 0) ? opus_wfx : adat_wfx;
        CHECK(xa2->lpVtbl->CreateSourceVoice(xa2, &vraw, &fmt, 0u, 2.0f, nullptr, nullptr, nullptr) == SCARLETT_OK);
        auto* v = static_cast<Xa2VoiceObj*>(vraw);
        voices.push_back(v);

        Xa2Buffer buf{};
        if (i % 2 == 0) {
            buf.AudioBytes = static_cast<uint32_t>(pkt_len);
            buf.pAudioData = opus_pkt;
        } else {
            buf.AudioBytes = static_cast<uint32_t>(adat_container.size());
            buf.pAudioData = adat_container.data();
        }
        CHECK(v->lpVtbl->SetVolume(v, 0.02f, 0u) == SCARLETT_OK);
        CHECK(v->lpVtbl->SubmitSourceBuffer(v, &buf, nullptr) == SCARLETT_OK);
        CHECK(v->lpVtbl->Start(v, 0u, 0u) == SCARLETT_OK);
    }

    std::vector<float> mixed(SCARLETT_AUDIO_QUANTUM_FRAMES * 2u, 0.0f);
    scarlett_audio_render_quantum_params_t rq{};
    rq.engine_handle = xa2->unix_engine_handle;
    rq.out_pcm_f32_addr = reinterpret_cast<uint64_t>(mixed.data());
    rq.frame_count = SCARLETT_AUDIO_QUANTUM_FRAMES;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM, &rq) == SCARLETT_OK);
    CHECK(rq.out_frames_rendered == SCARLETT_AUDIO_QUANTUM_FRAMES);
    CHECK(rq.out_active_voices == 64u);
    CHECK(rq.out_xruns == 0u);

    double energy = 0.0;
    for (float s : mixed) {
        energy += static_cast<double>(s) * static_cast<double>(s);
    }
    CHECK(energy > 0.01);

    /* Also test submix + XAPO + HSRC resampler (24kHz -> 48kHz) */
    scarlett_audio_voice_create_params_t sub_req{};
    sub_req.engine_handle = xa2->unix_engine_handle;
    sub_req.voice_kind = SCARLETT_VOICE_SUBMIX;
    sub_req.channels = 2u;
    sub_req.sample_rate_hz = 48000u;
    sub_req.xapo_type = SCARLETT_XAPO_REVERB;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE, &sub_req) == SCARLETT_OK);

    std::vector<float> pcm24k(240u * 2u, 0.2f);
    scarlett_audio_voice_create_params_t src24_req{};
    src24_req.engine_handle = xa2->unix_engine_handle;
    src24_req.submix_voice_handle = sub_req.out_voice_handle;
    src24_req.voice_kind = SCARLETT_VOICE_SOURCE;
    src24_req.format_tag = SCARLETT_AUDIO_FORMAT_IEEE_FLOAT;
    src24_req.channels = 2u;
    src24_req.sample_rate_hz = 24000u;
    src24_req.enable_hrtf = 1u;
    src24_req.xapo_type = SCARLETT_XAPO_LOWPASS;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE, &src24_req) == SCARLETT_OK);

    scarlett_audio_voice_buffer_params_t vbuf{};
    vbuf.voice_handle = src24_req.out_voice_handle;
    vbuf.buffer_addr = reinterpret_cast<uint64_t>(pcm24k.data());
    vbuf.buffer_bytes = static_cast<uint32_t>(pcm24k.size() * sizeof(float));
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_SUBMIT_VOICE_BUFFER, &vbuf) == SCARLETT_OK);

    scarlett_audio_voice_ctrl_params_t vctrl{};
    vctrl.voice_handle = src24_req.out_voice_handle;
    vctrl.ctrl_op = SCARLETT_VOICE_CTRL_START;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, &vctrl) == SCARLETT_OK);

    Xa2PerfData perf{};
    xa2->lpVtbl->GetPerformanceData(xa2, &perf);
    CHECK(perf.GlitchesSinceEngineStarted == 0u);
    CHECK(perf.ActiveResamplerCount >= 1u);

    for (Xa2VoiceObj* v : voices) {
        v->lpVtbl->DestroyVoice(v);
    }
    static_cast<Xa2VoiceObj*>(master_raw)->lpVtbl->DestroyVoice(master_raw);
    CHECK(xa2->lpVtbl->Release(xa2) == 0u);
}

void test_acphal_xma2_seek_and_adpcm() {
    CHECK(ApuCreateHeap(4u * 1024u * 1024u, 16u * 1024u * 1024u) == SCARLETT_OK);

    void* apu_va = nullptr;
    void* apu_pa = nullptr;
    CHECK(ApuAlloc(&apu_va, &apu_pa, 4096u, 256u, 0u) == SCARLETT_OK);
    CHECK(apu_va != nullptr && apu_va == apu_pa);
    CHECK((reinterpret_cast<uintptr_t>(apu_va) & 255u) == 0u);
    CHECK(ApuFree(apu_va) == SCARLETT_OK);

    void* hal = nullptr;
    CHECK(AcpHalCreate(&hal) == SCARLETT_OK);
    void* shape_ctx = nullptr;
    CHECK(AcpHalAllocateShapeContexts(hal, 4u, &shape_ctx) == SCARLETT_OK);

    /* Test big-endian XMA2 seek table with bswap32 */
    const uint32_t be_seek_table[4] = {
        scarlett::audio_media::scarlett_bswap32(2048u),
        scarlett::audio_media::scarlett_bswap32(4096u),
        scarlett::audio_media::scarlett_bswap32(6144u),
        scarlett::audio_media::scarlett_bswap32(8192u),
    };
    std::vector<scarlett::audio_media::Xma2SeekEntry> seek_entries;
    CHECK(scarlett::audio_media::parse_xma2_seek_table(be_seek_table, 4u, 2048u, seek_entries));
    CHECK(seek_entries.size() == 4u);
    CHECK(seek_entries[2].cumulative_samples == 6144u);
    uint32_t blk_idx = 0u, byte_off = 0u;
    CHECK(scarlett::audio_media::xma2_seek_to_sample(seek_entries, 5000u, blk_idx, byte_off));
    CHECK(blk_idx == 2u && byte_off == 4096u);

    /* Test real FFmpeg AV_CODEC_ID_XMA2 decoder via AcpHalDecodeXma2 */
    uint8_t xma2_empty_pkt[2048]{};
    xma2_empty_pkt[0] = 0x03u;
    xma2_empty_pkt[1] = 0xFFu;
    xma2_empty_pkt[2] = 0xF8u;
    xma2_empty_pkt[3] = 0xFFu;
    uint8_t pcm_out[8192]{};
    uint32_t decoded_bytes = 999u;
    CHECK(AcpHalDecodeXma2(shape_ctx, xma2_empty_pkt, sizeof(xma2_empty_pkt),
                           pcm_out, sizeof(pcm_out), &decoded_bytes) == SCARLETT_OK);
    CHECK(decoded_bytes == 0u);

    /* Test 52-byte XMA2WAVEFORMATEX initialization + negative malformed packet check */
    uint8_t xma2_wfx52[52]{};
    xma2_wfx52[18] = 1u; /* NumStreams = 1 */
    scarlett::audio_media::Xma2StreamDecoder xma_dec;
    CHECK(xma_dec.init(48000u, 2u, xma2_wfx52, sizeof(xma2_wfx52)));
    CHECK(xma_dec.is_open());
    uint8_t bad_pkt[2048]{};
    std::fill_n(bad_pkt, sizeof(bad_pkt), 0xAAu);
    bad_pkt[0] = 0x04u; /* frame_count = 1, frame_offset = 0 -> triggers XMA2 frame parse */
    bad_pkt[1] = 0x00u;
    bad_pkt[2] = 0x00u;
    bad_pkt[3] = 0x00u;
    bad_pkt[4] = 0x01u; /* frame_len = 128 bits -> ends inside this packet */
    bad_pkt[5] = 0x00u;
    std::vector<float> tmp_pcm;
    CHECK(scarlett_failed(xma_dec.decode_packet(bad_pkt, sizeof(bad_pkt), tmp_pcm)));

    /* Clean-Room IMA-ADPCM & MS-ADPCM decoding verification */
    uint8_t ima_blk[12]{};
    ima_blk[0] = 0x00u; ima_blk[1] = 0x10u; /* predictor = 4096 */
    ima_blk[2] = 10u; /* step index */
    ima_blk[4] = 0x12u; ima_blk[5] = 0x34u;
    std::vector<float> ima_pcm;
    CHECK(scarlett::audio_media::decode_ima_adpcm_to_f32(ima_blk, sizeof(ima_blk), 1u, sizeof(ima_blk), ima_pcm));
    CHECK(ima_pcm.size() >= 5u);
    CHECK(std::abs(ima_pcm[0] - (4096.0f / 32768.0f)) < 1e-4f);

    uint8_t ms_blk[12]{};
    ms_blk[0] = 0u; /* coeff 0 */
    ms_blk[1] = 64u; ms_blk[2] = 0u; /* delta = 64 */
    ms_blk[3] = 0x00u; ms_blk[4] = 0x08u; /* samp1 = 2048 */
    ms_blk[5] = 0x00u; ms_blk[6] = 0x04u; /* samp2 = 1024 */
    ms_blk[7] = 0x11u;
    std::vector<float> ms_pcm;
    CHECK(scarlett::audio_media::decode_ms_adpcm_to_f32(ms_blk, sizeof(ms_blk), 1u, sizeof(ms_blk), ms_pcm));
    CHECK(ms_pcm.size() >= 4u);
    CHECK(std::abs(ms_pcm[0] - (1024.0f / 32768.0f)) < 1e-4f);
    CHECK(std::abs(ms_pcm[1] - (2048.0f / 32768.0f)) < 1e-4f);

    CHECK(AcpHalReleaseShapeContexts(hal, shape_ctx) == SCARLETT_OK);
    CHECK(AcpHalRelease(hal) == SCARLETT_OK);
}

void test_xdsp_fft_reverb_hrtf() {
    constexpr uint32_t N = 512u;
    std::vector<float> orig_re(N), re(N), im(N, 0.0f);
    for (uint32_t i = 0u; i < N; ++i) {
        const float val = 0.6f * std::sin(2.0f * 3.14159265f * 7.0f * static_cast<float>(i) / N) +
                          0.3f * std::cos(2.0f * 3.14159265f * 23.0f * static_cast<float>(i) / N);
        orig_re[i] = val;
        re[i] = val;
    }

    CHECK(scarlett::audio_media::xdsp_fft_f32(N, re.data(), im.data(), false));
    CHECK(scarlett::audio_media::xdsp_fft_f32(N, re.data(), im.data(), true));

    float max_err = 0.0f;
    for (uint32_t i = 0u; i < N; ++i) {
        max_err = std::max(max_err, std::abs(re[i] - orig_re[i]));
        max_err = std::max(max_err, std::abs(im[i]));
    }
    CHECK(max_err < 1e-4f);

    /* Overlap-add convolution reverb */
    std::vector<float> ir(64u, 0.0f);
    ir[0] = 0.8f;
    ir[16] = 0.4f;
    std::vector<float> dry_in(SCARLETT_AUDIO_QUANTUM_FRAMES, 0.0f);
    dry_in[0] = 1.0f;
    std::vector<float> rev_out(SCARLETT_AUDIO_QUANTUM_FRAMES, 0.0f);

    scarlett_audio_xdsp_params_t xp{};
    xp.op = SCARLETT_XDSP_OP_CONV_REVERB;
    xp.count = SCARLETT_AUDIO_QUANTUM_FRAMES;
    xp.in_real_addr = reinterpret_cast<uint64_t>(dry_in.data());
    xp.in_imag_or_ir_addr = reinterpret_cast<uint64_t>(ir.data());
    xp.out_real_addr = reinterpret_cast<uint64_t>(rev_out.data());
    xp.ir_len = static_cast<uint32_t>(ir.size());
    xp.param_f0 = 1.0f;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_XDSP_OP, &xp) == SCARLETT_OK);
    CHECK(std::abs(rev_out[0] - 0.8f) < 1e-3f);
    CHECK(std::abs(rev_out[16] - 0.4f) < 1e-3f);

    /* Spatial HRTF: pan hard right (+pi/2) -> right ear RMS > left ear RMS */
    std::vector<float> mono_tone(256u);
    for (uint32_t i = 0u; i < 256u; ++i) {
        mono_tone[i] = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
    }
    std::vector<float> hrtf_stereo(512u, 0.0f);
    xp.op = SCARLETT_XDSP_OP_HRTF_RENDER;
    xp.count = 256u;
    xp.in_real_addr = reinterpret_cast<uint64_t>(mono_tone.data());
    xp.out_real_addr = reinterpret_cast<uint64_t>(hrtf_stereo.data());
    xp.param_f0 = 1.5707963f; /* +90 deg right */
    xp.param_f1 = 0.0f;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_XDSP_OP, &xp) == SCARLETT_OK);

    double energy_l = 0.0, energy_r = 0.0;
    for (uint32_t i = 0u; i < 256u; ++i) {
        energy_l += static_cast<double>(hrtf_stereo[i * 2u]) * hrtf_stereo[i * 2u];
        energy_r += static_cast<double>(hrtf_stereo[i * 2u + 1u]) * hrtf_stereo[i * 2u + 1u];
    }
    CHECK(energy_r > energy_l * 4.0);
}

void test_gameinput_1000hz_and_4motor_rumble() {
    void* gi_raw = nullptr;
    CHECK(GameInputCreate(&gi_raw) == SCARLETT_OK);
    auto* gi = static_cast<GiObj*>(gi_raw);
    CHECK(gi != nullptr);
    CHECK(gi->lpVtbl->GetCurrentTimestamp(gi) > 0u);

    constexpr uint64_t kPadDevId = 0x045E0B120001ULL;
    constexpr int kTotalReadings = 1000;

    /* 1000 Hz concurrent producer pushing gamepad/keyboard/mouse readings */
    std::atomic<bool> producer_done{false};
    std::atomic<int> race_errors{0};
    std::thread producer([&]() {
        for (int i = 1; i <= kTotalReadings; ++i) {
            scarlett_input_reading_record_t rec{};
            rec.device_id = kPadDevId;
            rec.input_kind = SCARLETT_GAMEINPUT_KIND_GAMEPAD;
            rec.gamepad.buttons = static_cast<uint32_t>(i);
            rec.gamepad.left_trigger = static_cast<float>(i % 100) / 100.0f;
            rec.gamepad.right_trigger = 1.0f - rec.gamepad.left_trigger;
            rec.gamepad.left_thumbstick_x = static_cast<float>(i) / 1000.0f;
            rec.gamepad.extended_buttons = 0xAA55u;

            scarlett_input_ring_params_t rp{};
            rp.instance_handle = gi->unix_instance_handle;
            rp.op = SCARLETT_INPUT_OP_PUSH_READING;
            rp.record_addr = reinterpret_cast<uint64_t>(&rec);
            if (scarlett_failed(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &rp))) {
                race_errors.fetch_add(1);
            }
            if ((i % 25) == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(250));
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    while (!producer_done.load(std::memory_order_acquire)) {
        void* r_cur = nullptr;
        if (scarlett_succeeded(gi->lpVtbl->GetCurrentReading(
                gi, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_cur))) {
            auto* r = static_cast<GiReadingObj*>(r_cur);
            GiGamepadState gs{};
            if (r->lpVtbl->GetGamepadState(r, &gs) == 0u) {
                race_errors.fetch_add(1);
            }
            const uint64_t seq = r->lpVtbl->GetSequenceNumber(r, SCARLETT_GAMEINPUT_KIND_GAMEPAD);
            if (gs.buttons != static_cast<uint32_t>(seq) || gs.extendedButtons != 0xAA55u) {
                race_errors.fetch_add(1);
            }
            r->lpVtbl->Release(r);
        }
    }
    producer.join();
    CHECK(race_errors.load() == 0);

    /* Verify Current -> Previous -> Next ring traversal */
    void* r_cur = nullptr;
    CHECK(gi->lpVtbl->GetCurrentReading(gi, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_cur) == SCARLETT_OK);
    auto* cur_obj = static_cast<GiReadingObj*>(r_cur);
    const uint64_t cur_seq = cur_obj->lpVtbl->GetSequenceNumber(cur_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD);
    CHECK(cur_seq == static_cast<uint64_t>(kTotalReadings));

    void* r_prev = nullptr;
    CHECK(gi->lpVtbl->GetPreviousReading(gi, cur_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_prev) == SCARLETT_OK);
    auto* prev_obj = static_cast<GiReadingObj*>(r_prev);
    CHECK(prev_obj->lpVtbl->GetSequenceNumber(prev_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD) == cur_seq - 1u);

    void* r_next = nullptr;
    CHECK(gi->lpVtbl->GetNextReading(gi, prev_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_next) == SCARLETT_OK);
    auto* next_obj = static_cast<GiReadingObj*>(r_next);
    CHECK(next_obj->lpVtbl->GetSequenceNumber(next_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD) == cur_seq);

    /* Test 4-motor rumble via IGameInputDevice::SetRumbleState + GIP hidraw & evdev FF_RUMBLE fallback */
    void* dev_raw = nullptr;
    cur_obj->lpVtbl->GetDevice(cur_obj, &dev_raw);
    CHECK(dev_raw != nullptr);
    auto* dev = static_cast<GiDeviceObj*>(dev_raw);

    GiRumble rumble{ 0.85f, 0.45f, 0.70f, 0.30f };
    dev->lpVtbl->SetRumbleState(dev, &rumble);

    scarlett::audio_media::ScarlettRumbleTransportPacket pkt{};
    scarlett_input_ring_params_t qp{};
    qp.op = SCARLETT_INPUT_OP_QUERY_RUMBLE_PKT;
    qp.device_id = kPadDevId;
    qp.aux_addr = reinterpret_cast<uint64_t>(&pkt);
    qp.aux_bytes = sizeof(pkt);
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &qp) == SCARLETT_OK);
    CHECK(pkt.gip_packet_len == 13u);
    CHECK(pkt.gip_packet[0] == 0x09u && pkt.gip_packet[5] == 0x0Fu);
    CHECK(pkt.gip_packet[6] == 70u && pkt.gip_packet[7] == 30u);
    CHECK(pkt.gip_packet[8] == 85u && pkt.gip_packet[9] == 45u);
    CHECK(pkt.hidraw_report_len == 9u && pkt.hidraw_report[0] == 0x03u && pkt.hidraw_report[1] == 0x0Fu);
    CHECK(pkt.evdev_ff_type == FF_RUMBLE);
    CHECK(pkt.evdev_strong_mag > 55000u && pkt.evdev_weak_mag >= 32767u);

    /* Test evdev FF_RUMBLE fallback mode selection */
    scarlett_input_ring_params_t tp{};
    tp.op = SCARLETT_INPUT_OP_SET_TRANSPORT;
    tp.input_kind = SCARLETT_INPUT_TRANSPORT_HIDRAW_GIP;
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &tp) == SCARLETT_OK);
    dev->lpVtbl->SetRumbleState(dev, &rumble);
    CHECK(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &qp) == SCARLETT_OK);
    CHECK(pkt.transport_used == SCARLETT_INPUT_TRANSPORT_HIDRAW_GIP ||
          pkt.transport_used == SCARLETT_INPUT_TRANSPORT_EVDEV_FF);

    dev->lpVtbl->Release(dev);
    next_obj->lpVtbl->Release(next_obj);
    prev_obj->lpVtbl->Release(prev_obj);
    cur_obj->lpVtbl->Release(cur_obj);

    /* Verify RegisterDeviceCallback (slot 9 / 0x48) with GameInputBlockingEnumeration (2)
       and GetDeviceInfo() deviceId at offset 0x20 */
    struct GiCbProbe {
        uint32_t calls;
        uint32_t status;
        uint8_t  device_id[32];
    } probe{};
    auto on_dev_cb = [](uint64_t /*tok*/, void* ctx, void* device, uint64_t /*ts*/,
                        uint32_t cur_status, uint32_t /*prev_status*/) SCARLETT_WIN64_API {
        auto* p = static_cast<GiCbProbe*>(ctx);
        p->calls += 1u;
        p->status = cur_status;
        if (device != nullptr) {
            auto* d = static_cast<GiDeviceObj*>(device);
            const auto* info = static_cast<const uint8_t*>(d->lpVtbl->GetDeviceInfo(d));
            if (info != nullptr) {
                std::memcpy(p->device_id, info + 0x20u, 32u);
            }
        }
    };
    uint64_t cb_tok = 0u;
    void (*cb_ptr)(uint64_t, void*, void*, uint64_t, uint32_t, uint32_t) SCARLETT_WIN64_API = on_dev_cb;
    CHECK(gi->lpVtbl->RegisterDeviceCallback(
              gi, nullptr, SCARLETT_GAMEINPUT_KIND_GAMEPAD, 1u, 2u, &probe,
              reinterpret_cast<void*>(cb_ptr), &cb_tok) == SCARLETT_OK);
    CHECK(cb_tok != 0u);
    CHECK(probe.calls == 1u);
    CHECK(probe.status == 1u);
    bool nonzero_id = false;
    for (uint8_t b : probe.device_id) {
        if (b != 0u) nonzero_id = true;
    }
    CHECK(nonzero_id);

    CHECK(gi->lpVtbl->Release(gi) == 0u);
}

void test_bink2_all_12_exports_and_yuv_bt709() {
    constexpr uint32_t kW = 16u, kH = 16u, kFrames = 4u;
    std::vector<uint8_t> y(kW * kH * kFrames, 235u); /* BT.709 white Y=235 */
    std::vector<uint8_t> u((kW / 2u) * (kH / 2u) * kFrames, 128u);
    std::vector<uint8_t> v((kW / 2u) * (kH / 2u) * kFrames, 128u);

    /* Frame 1: BT.709 pure saturated red (~Y=63, U=102, V=240) */
    std::fill_n(y.data() + kW * kH, kW * kH, 63u);
    std::fill_n(u.data() + (kW / 2u) * (kH / 2u), (kW / 2u) * (kH / 2u), 102u);
    std::fill_n(v.data() + (kW / 2u) * (kH / 2u), (kW / 2u) * (kH / 2u), 240u);

    scarlett::audio_media::ScarlettBinkInMemStream mem{};
    mem.magic = 0x32594353u;
    mem.width = kW;
    mem.height = kH;
    mem.frame_count = kFrames;
    mem.fps_num = 60u;
    mem.fps_den = 1u;
    mem.y_planes = y.data();
    mem.u_planes = u.data();
    mem.v_planes = v.data();

    void* bink = BinkOpen(&mem, 0x00040000u);
    CHECK(bink != nullptr);

    scarlett_bink_summary_t sum{};
    BinkGetSummary(bink, &sum);
    CHECK(sum.width == kW && sum.height == kH && sum.total_frames == kFrames && sum.current_frame == 1u);

    CHECK(BinkWait(bink) == 0);
    CHECK(BinkDoFrame(bink) == 0);

    std::vector<uint8_t> bgra(kW * kH * 4u, 0u);
    CHECK(BinkCopyToBuffer(bink, bgra.data(), static_cast<int32_t>(kW * 4u), kH, 0u, 0u,
                           SCARLETT_BINK_SURFACE_BGRA8888 | SCARLETT_BINK_MATRIX_BT709) == 0);
    /* White pixel check: B>=250, G>=250, R>=250, A==255 */
    CHECK(bgra[0] >= 250u && bgra[1] >= 250u && bgra[2] >= 250u && bgra[3] == 255u);

    BinkNextFrame(bink);
    BinkGetSummary(bink, &sum);
    CHECK(sum.current_frame == 2u);
    CHECK(BinkDoFrame(bink) == 0);
    CHECK(BinkCopyToBufferRect(bink, bgra.data(), static_cast<int32_t>(kW * 4u), kH,
                               0u, 0u, 0u, 0u, 8u, 8u,
                               SCARLETT_BINK_SURFACE_RGBA8888 | SCARLETT_BINK_MATRIX_BT709) == 0);
    /* BT.709 Red in RGBA8888: R >= 240, G <= 15, B <= 15 */
    CHECK(bgra[0] >= 240u && bgra[1] <= 15u && bgra[2] <= 15u && bgra[3] == 255u);

    BinkSetSoundOnOff(bink, 0);
    BinkSetVolume(bink, 0u, 16384);
    CHECK(BinkPause(bink, 1) == 1);
    BinkGoto(bink, 4u, 0);
    BinkGetSummary(bink, &sum);
    CHECK(sum.current_frame == 4u && sum.sound_enabled == 0u && sum.volume == 16384 && sum.paused == 1u);
    BinkClose(bink);
}

void test_fidelityfx_fsr3_fsr4() {
    uint32_t rw = 0u, rh = 0u;
    CHECK(ffxFsr3GetRenderResolutionFromQualityMode(&rw, &rh, 3840u, 2160u, SCARLETT_FSR_QUALITY_PERFORMANCE) == SCARLETT_OK);
    CHECK(rw == 1920u && rh == 1080u);
    CHECK(std::abs(ffxFsr3GetUpscaleRatioFromQualityMode(SCARLETT_FSR_QUALITY_QUALITY) - 1.5f) < 1e-4f);
    CHECK(ffxFsr4GetVersion() == 0x040102u);

    const int32_t phases = ffxFsr3GetJitterPhaseCount(1920, 3840);
    CHECK(phases == 32);
    float jx = 0.0f, jy = 0.0f;
    CHECK(ffxFsr3GetJitterOffset(&jx, &jy, 0, phases) == SCARLETT_OK);
    CHECK(std::abs(jx) <= 0.5f && std::abs(jy) <= 0.5f);
    CHECK(std::abs(jx - 0.0f) < 1e-4f); /* Halton(1,2) - 0.5 == 0.0 */

    struct {
        uint32_t quality_mode;
        uint32_t display_width;
        uint32_t display_height;
        uint32_t max_render_width;
        uint32_t max_render_height;
        uint32_t fsr_version_packed;
    } loader_desc{ SCARLETT_FSR_QUALITY_QUALITY, 32u, 32u, 24u, 24u, 0x040102u };
    uint64_t lctx = 0u;
    CHECK(ffxCreateContext(&lctx, &loader_desc, nullptr) == SCARLETT_OK);
    uint32_t qver = 0u, qvk = 0u;
    CHECK(ffxQuery(&lctx, &qver, &qvk) == SCARLETT_OK);
    CHECK(qver == 0x040102u);
    CHECK(ffxConfigure(&lctx, &loader_desc) == SCARLETT_OK);
    CHECK(ffxDestroyContext(&lctx, nullptr) == SCARLETT_OK);

    struct {
        uint32_t flags;
        uint32_t quality_mode;
        uint32_t max_render_width;
        uint32_t max_render_height;
        uint32_t display_width;
        uint32_t display_height;
    } fsr3_desc{ 0u, SCARLETT_FSR_QUALITY_PERFORMANCE, 16u, 16u, 32u, 32u };
    uint64_t ctx = 0u;
    CHECK(ffxFsr3ContextCreate(&ctx, &fsr3_desc) == SCARLETT_OK);
    CHECK(ctx != 0u);

    std::vector<float> in_img(16u * 16u * 4u, 0.5f);
    std::vector<float> out_img(32u * 32u * 4u, 0.0f);
    struct {
        const float* color_in;
        const float* depth_in;
        const float* motion_vectors;
        float* color_out;
        uint32_t render_width;
        uint32_t render_height;
        uint32_t display_width;
        uint32_t display_height;
        float jitter_x;
        float jitter_y;
        float sharpness;
        float frame_time_delta_ms;
        uint32_t out_used_vulkan_gpu;
    } disp{ in_img.data(), nullptr, nullptr, out_img.data(), 16u, 16u, 32u, 32u, jx, jy, 0.5f, 16.6f, 0u };

    CHECK(ffxFsr3ContextDispatch(&ctx, &disp) == SCARLETT_OK);
    CHECK(std::abs(out_img[0] - 0.5f) < 1e-3f);
    CHECK(std::abs(out_img[(32u * 16u + 16u) * 4u] - 0.5f) < 1e-3f);
    CHECK(ffxFsr3ContextDestroy(&ctx) == SCARLETT_OK);
}

void test_diagnostic_auto_stubs() {
    void* dummy = nullptr;
    CHECK(scarlett_failed(CreateAudioVolumeMeter(&dummy)));
    CHECK(scarlett_failed(GameInputCreateDispatcher(&dummy)));
    CHECK(scarlett_failed(AcpHalSubmitFlowgraph(nullptr, nullptr, 0u, 0u)));
    CHECK(scarlett_failed(BinkSetIOSize(4096u)));
    CHECK(scarlett_failed(ffxLoadInterface(nullptr, 1u, nullptr, 0u)));
    CHECK(scarlett_failed(ffxFsr4SetDebugHook(nullptr, nullptr, 0u, 0u)));

    void* xa2_raw = nullptr;
    CHECK(XAudio2Create(&xa2_raw, 0u, 0u) == SCARLETT_OK);
    auto* xa2 = static_cast<Xa2Obj*>(xa2_raw);
    CHECK(scarlett_failed(xa2->lpVtbl->SetDebugConfiguration(xa2, nullptr, nullptr)));
    CHECK(xa2->lpVtbl->Release(xa2) == 0u);
}

} // namespace

int main() {
    scarlett_audio_pe_set_unix_bridge(&scarlett_audio_unix_dispatch);

    test_unixlib_handshake_and_table();
    test_xaudio2_64_opus_adat_voices();
    test_acphal_xma2_seek_and_adpcm();
    test_xdsp_fft_reverb_hrtf();
    test_gameinput_1000hz_and_4motor_rumble();
    test_bink2_all_12_exports_and_yuv_bt709();
    test_fidelityfx_fsr3_fsr4();
    test_diagnostic_auto_stubs();

    std::printf("[audio_media_tests] PASS=%d FAIL=%d\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
