#include "arena_scenes.hpp"
#include "dds_dx10.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <linux/input.h>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
#include <opus/opus.h>
}

#include "audio_media/audio_media_unix_abi.h"
#include "audio_media/gameinput_backend.hpp"
#include "audio_media/xapu_codecs.hpp"
#include "audio_media/xdsp_math.hpp"
#include "loader/game_config.hpp"
#include "loader/loader_unix_abi.h"
#include "loader/vfs_cow.hpp"
#include "loader/xvd_xvc_format.h"
#include "scarlett_xdl_runtime.hpp"
#include "xgameruntime/xgameruntime_api.h"

extern "C" {
int32_t scarlett_audio_unix_dispatch(uint32_t opcode, void* params);
scarlett_hresult_t SCARLETT_WIN64_API XAudio2Create(void** ppXAudio2, uint32_t Flags, uint32_t Proc);
scarlett_hresult_t SCARLETT_WIN64_API GameInputCreate(void** gameInput);
}

namespace scarlett::arena {

namespace {

/* Minimal PE64-matching COM vtable projections for xaudio2_9 & GameInput */
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

struct WaveFormatEx {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
};
static_assert(sizeof(WaveFormatEx) == 20, "Matches scarlett_waveformatex_t in xaudio2_9_pe.c");

struct Xma2WaveFormatEx {
    WaveFormatEx wfx;
    uint8_t extra[34];
};

#pragma pack(push, 1)
struct Xma2WaveFormatEx52 {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
    uint8_t extra[34];
};
#pragma pack(pop)
static_assert(sizeof(Xma2WaveFormatEx52) == 52, "XMA2WAVEFORMATEX must be 52 bytes");

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

struct GiRumble {
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
};

struct GiDeviceVtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetDeviceInfo)(void*, const void**);
    uint32_t (SCARLETT_WIN64_API *GetDeviceStatus)(void*);
    void (SCARLETT_WIN64_API *SetRumbleState)(void*, const GiRumble*);
    scarlett_hresult_t (SCARLETT_WIN64_API *SetHapticMotorState)(void*, uint32_t, const void*);
};
struct GiDeviceObj { const GiDeviceVtbl* lpVtbl; };

struct GiReadingVtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    uint32_t (SCARLETT_WIN64_API *GetInputKind)(void*);
    uint64_t (SCARLETT_WIN64_API *GetSequenceNumber)(void*, uint32_t);
    uint64_t (SCARLETT_WIN64_API *GetTimestamp)(void*);
    void (SCARLETT_WIN64_API *GetDevice)(void*, void**);
    uint32_t (SCARLETT_WIN64_API *GetGamepadState)(void*, GiGamepadState*);
};
struct GiReadingObj { const GiReadingVtbl* lpVtbl; };

struct GiVtbl {
    scarlett_hresult_t (SCARLETT_WIN64_API *QueryInterface)(void*, const scarlett_guid_t*, void**);
    uint32_t (SCARLETT_WIN64_API *AddRef)(void*);
    uint32_t (SCARLETT_WIN64_API *Release)(void*);
    uint64_t (SCARLETT_WIN64_API *GetCurrentTimestamp)(void*);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetCurrentReading)(void*, uint32_t, void*, void**);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetNextReading)(void*, void*, uint32_t, void*, void**);
    scarlett_hresult_t (SCARLETT_WIN64_API *RegisterDeviceCallback)(
        void*, void*, uint32_t, uint32_t, uint32_t, void*, void*, uint64_t*);
    scarlett_hresult_t (SCARLETT_WIN64_API *GetPreviousReading)(void*, void*, uint32_t, void*, void**);
};
struct GiObj {
    const GiVtbl* lpVtbl;
    uint64_t unix_instance_handle;
};

[[gnu::noinline]] scarlett_hresult_t call_query_api_impl_win64(
    uint64_t api_id,
    const scarlett_guid_t* riid,
    void** out_impl) noexcept {
    register uint64_t r_rcx __asm__("rcx") = api_id;
    register uint64_t r_rdx __asm__("rdx") = reinterpret_cast<uint64_t>(riid);
    register uint64_t r_r8  __asm__("r8")  = reinterpret_cast<uint64_t>(out_impl);
    register uint64_t r_fn  __asm__("r11") = reinterpret_cast<uint64_t>(&QueryApiImpl);
    scarlett_hresult_t hr = SCARLETT_E_FAIL;
    __asm__ volatile(
        "subq $128, %%rsp\n\t"
        "pushq %%rdi\n\t"
        "movq %%rsp, %%rdi\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $32, %%rsp\n\t"
        "callq *%%r11\n\t"
        "movq %%rdi, %%rsp\n\t"
        "popq %%rdi\n\t"
        "addq $128, %%rsp\n\t"
        : "=a"(hr), "+r"(r_rcx), "+r"(r_rdx), "+r"(r_r8), "+r"(r_fn)
        :
        : "r9", "r10", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc", "memory"
    );
    return hr;
}

uint32_t fnv1a_32(const uint8_t* p, size_t n) noexcept {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0u; i < n; ++i) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

} // namespace

bool run_scene6_runtime_audio_vfs(ArenaTelemetry& tel) {
    SceneChecker sc(tel, tel.s6.checks, "Scene6_Runtime_Audio_VFS");
    namespace fs = std::filesystem;

    /* 1. Audio: 64 simultaneous Opus 0x704F/ADAT + 32 XMA2 voices (0 xruns) + XDSP */
    {
        void* xa2_raw = nullptr;
        ARENA_CHECK_HR(sc, XAudio2Create(&xa2_raw, 0u, 0u), SCARLETT_OK);
        auto* xa2 = static_cast<Xa2Obj*>(xa2_raw);
        if (xa2 != nullptr) {
            void* master_raw = nullptr;
            ARENA_CHECK_HR(sc, xa2->lpVtbl->CreateMasteringVoice(xa2, &master_raw, 2u, 48000u, 0u, nullptr, nullptr, 0u), SCARLETT_OK);

            int opus_err = OPUS_OK;
            OpusEncoder* enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &opus_err);
            ARENA_CHECK(sc, enc != nullptr && opus_err == OPUS_OK);
            std::vector<float> sine_pcm(480u * 2u);
            for (uint32_t i = 0u; i < 480u; ++i) {
                const float s = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
                sine_pcm[i * 2u + 0u] = s;
                sine_pcm[i * 2u + 1u] = s;
            }
            uint8_t opus_pkt[1024]{};
            const int pkt_len = opus_encode_float(enc, sine_pcm.data(), 480, opus_pkt, sizeof(opus_pkt));
            ARENA_CHECK(sc, pkt_len > 0);
            opus_encoder_destroy(enc);

            std::vector<uint8_t> adat_container(
                sizeof(scarlett::audio_media::AdatRingHeader) + SCARLETT_ADAT_RING_CAPACITY_BYTES, 0u);
            const uint32_t wrap_offset = SCARLETT_ADAT_RING_CAPACITY_BYTES - 4u;
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
            uint8_t* ring = adat_container.data() + sizeof(hdr);
            const uint8_t* phdr_b = reinterpret_cast<const uint8_t*>(&phdr);
            for (size_t i = 0u; i < sizeof(phdr); ++i) {
                ring[(wrap_offset + i) % SCARLETT_ADAT_RING_CAPACITY_BYTES] = phdr_b[i];
            }
            const uint32_t pstart = (wrap_offset + static_cast<uint32_t>(sizeof(phdr))) %
                                    SCARLETT_ADAT_RING_CAPACITY_BYTES;
            for (int i = 0; i < pkt_len; ++i) {
                ring[(pstart + static_cast<uint32_t>(i)) % SCARLETT_ADAT_RING_CAPACITY_BYTES] = opus_pkt[i];
            }

            /* 64 simultaneous Opus 0x704F / ADAT voices */
            WaveFormatEx opus_wfx{};
            opus_wfx.wFormatTag = SCARLETT_AUDIO_FORMAT_OPUS;
            opus_wfx.nChannels = 2u;
            opus_wfx.nSamplesPerSec = 48000u;

            std::vector<Xa2VoiceObj*> voices;
            voices.reserve(96u);
            uint32_t opus_created = 0u;
            for (int i = 0; i < 64; ++i) {
                void* vraw = nullptr;
                ARENA_CHECK_HR(sc, xa2->lpVtbl->CreateSourceVoice(xa2, &vraw, &opus_wfx, 0u, 2.0f, nullptr, nullptr, nullptr), SCARLETT_OK);
                auto* v = static_cast<Xa2VoiceObj*>(vraw);
                if (v != nullptr) {
                    voices.push_back(v);
                    ++opus_created;
                    Xa2Buffer buf{};
                    if (i % 2 == 0) {
                        buf.AudioBytes = static_cast<uint32_t>(pkt_len);
                        buf.pAudioData = opus_pkt;
                    } else {
                        buf.AudioBytes = static_cast<uint32_t>(adat_container.size());
                        buf.pAudioData = adat_container.data();
                    }
                    ARENA_CHECK_HR(sc, v->lpVtbl->SetVolume(v, 0.01f, 0u), SCARLETT_OK);
                    ARENA_CHECK_HR(sc, v->lpVtbl->SubmitSourceBuffer(v, &buf, nullptr), SCARLETT_OK);
                    ARENA_CHECK_HR(sc, v->lpVtbl->Start(v, 0u, 0u), SCARLETT_OK);
                }
            }

            /* + 32 simultaneous XMA2 voices (AV_CODEC_ID_XMA2 + bswap32 seek table) */
            const uint32_t be_seek[4] = {
                scarlett::audio_media::scarlett_bswap32(2048u),
                scarlett::audio_media::scarlett_bswap32(4096u),
                scarlett::audio_media::scarlett_bswap32(6144u),
                scarlett::audio_media::scarlett_bswap32(8192u),
            };
            std::vector<scarlett::audio_media::Xma2SeekEntry> seek_entries;
            ARENA_CHECK(sc, scarlett::audio_media::parse_xma2_seek_table(be_seek, 4u, 2048u, seek_entries));
            uint32_t blk_idx = 0u, byte_off = 0u;
            ARENA_CHECK(sc, scarlett::audio_media::xma2_seek_to_sample(seek_entries, 5000u, blk_idx, byte_off));
            ARENA_CHECK(sc, blk_idx == 2u && byte_off == 4096u);

            Xma2WaveFormatEx xma_wfx{};
            xma_wfx.wfx.wFormatTag = SCARLETT_AUDIO_FORMAT_XMA2;
            xma_wfx.wfx.nChannels = 2u;
            xma_wfx.wfx.nSamplesPerSec = 48000u;
            xma_wfx.wfx.nBlockAlign = 2048u;
            xma_wfx.wfx.wBitsPerSample = 16u;
            xma_wfx.wfx.cbSize = 34u;
            xma_wfx.extra[0] = 1u; /* NumStreams = 1 */

            uint8_t xma2_pkt[2048]{};
            xma2_pkt[0] = 0x03u;
            xma2_pkt[1] = 0xFFu;
            xma2_pkt[2] = 0xF8u;
            xma2_pkt[3] = 0xFFu;

            uint32_t xma2_created = 0u;
            for (int i = 0; i < 32; ++i) {
                void* vraw = nullptr;
                ARENA_CHECK_HR(sc, xa2->lpVtbl->CreateSourceVoice(xa2, &vraw, &xma_wfx, 0u, 2.0f, nullptr, nullptr, nullptr), SCARLETT_OK);
                auto* v = static_cast<Xa2VoiceObj*>(vraw);
                if (v != nullptr) {
                    voices.push_back(v);
                    ++xma2_created;
                    Xa2Buffer buf{};
                    buf.AudioBytes = sizeof(xma2_pkt);
                    buf.pAudioData = xma2_pkt;
                    ARENA_CHECK_HR(sc, v->lpVtbl->SubmitSourceBuffer(v, &buf, nullptr), SCARLETT_OK);
                    ARENA_CHECK_HR(sc, v->lpVtbl->Start(v, 0u, 0u), SCARLETT_OK);
                }
            }

            std::vector<float> mixed(SCARLETT_AUDIO_QUANTUM_FRAMES * 2u, 0.0f);
            scarlett_audio_render_quantum_params_t rq{};
            rq.engine_handle = xa2->unix_engine_handle;
            rq.out_pcm_f32_addr = reinterpret_cast<uint64_t>(mixed.data());
            rq.frame_count = SCARLETT_AUDIO_QUANTUM_FRAMES;
            ARENA_CHECK_HR(sc, scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM, &rq), SCARLETT_OK);
            ARENA_CHECK(sc, rq.out_active_voices == 64u);
            ARENA_CHECK(sc, xma2_created == 32u);
            ARENA_CHECK(sc, rq.out_xruns == 0u);

            Xa2PerfData perf{};
            xa2->lpVtbl->GetPerformanceData(xa2, &perf);
            ARENA_CHECK(sc, perf.GlitchesSinceEngineStarted == 0u);

            tel.s6.opus_adat_voices = opus_created;
            tel.s6.xma2_voices = xma2_created;
            tel.s6.audio_xruns = rq.out_xruns;

            for (Xa2VoiceObj* v : voices) {
                v->lpVtbl->DestroyVoice(v);
            }
            static_cast<Xa2VoiceObj*>(master_raw)->lpVtbl->DestroyVoice(master_raw);
            ARENA_CHECK(sc, xa2->lpVtbl->Release(xa2) == 0u);
        }

        /* XDSP FFT/IFFT + Convolution Reverb + Spatial HRTF */
        constexpr uint32_t N = 512u;
        std::vector<float> orig_re(N), re(N), im(N, 0.0f);
        for (uint32_t i = 0u; i < N; ++i) {
            const float val = 0.6f * std::sin(2.0f * 3.14159265f * 7.0f * static_cast<float>(i) / N);
            orig_re[i] = val;
            re[i] = val;
        }
        ARENA_CHECK(sc, scarlett::audio_media::xdsp_fft_f32(N, re.data(), im.data(), false));
        ARENA_CHECK(sc, scarlett::audio_media::xdsp_fft_f32(N, re.data(), im.data(), true));
        double max_err = 0.0;
        for (uint32_t i = 0u; i < N; ++i) {
            max_err = std::max(max_err, static_cast<double>(std::abs(re[i] - orig_re[i])));
        }
        tel.s6.xdsp_fft_max_err = max_err;
        ARENA_CHECK(sc, max_err < 1e-4);

        std::vector<float> ir(64u, 0.0f);
        ir[0] = 0.8f;
        ir[16] = 0.4f;
        std::vector<float> dry(SCARLETT_AUDIO_QUANTUM_FRAMES, 0.0f);
        dry[0] = 1.0f;
        std::vector<float> rev(SCARLETT_AUDIO_QUANTUM_FRAMES, 0.0f);
        scarlett_audio_xdsp_params_t xp{};
        xp.op = SCARLETT_XDSP_OP_CONV_REVERB;
        xp.count = SCARLETT_AUDIO_QUANTUM_FRAMES;
        xp.in_real_addr = reinterpret_cast<uint64_t>(dry.data());
        xp.in_imag_or_ir_addr = reinterpret_cast<uint64_t>(ir.data());
        xp.out_real_addr = reinterpret_cast<uint64_t>(rev.data());
        xp.ir_len = 64u;
        xp.param_f0 = 1.0f;
        ARENA_CHECK_HR(sc, scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_XDSP_OP, &xp), SCARLETT_OK);
        tel.s6.xdsp_reverb_ok = (std::abs(rev[0] - 0.8f) < 1e-3f && std::abs(rev[16] - 0.4f) < 1e-3f);
        ARENA_CHECK(sc, tel.s6.xdsp_reverb_ok);

        std::vector<float> mono(256u);
        for (uint32_t i = 0u; i < 256u; ++i) {
            mono[i] = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
        }
        std::vector<float> hrtf(512u, 0.0f);
        xp.op = SCARLETT_XDSP_OP_HRTF_RENDER;
        xp.count = 256u;
        xp.in_real_addr = reinterpret_cast<uint64_t>(mono.data());
        xp.out_real_addr = reinterpret_cast<uint64_t>(hrtf.data());
        xp.param_f0 = 1.5707963f;
        xp.param_f1 = 0.0f;
        ARENA_CHECK_HR(sc, scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_XDSP_OP, &xp), SCARLETT_OK);
        double el = 0.0, er = 0.0;
        for (uint32_t i = 0u; i < 256u; ++i) {
            el += static_cast<double>(hrtf[i * 2u]) * hrtf[i * 2u];
            er += static_cast<double>(hrtf[i * 2u + 1u]) * hrtf[i * 2u + 1u];
        }
        tel.s6.xdsp_hrtf_ok = (er > el * 4.0);
        ARENA_CHECK(sc, tel.s6.xdsp_hrtf_ok);
    }

    /* 2. GameInput 1000 Hz ring history + 4-motor rumble */
    {
        void* gi_raw = nullptr;
        ARENA_CHECK_HR(sc, GameInputCreate(&gi_raw), SCARLETT_OK);
        auto* gi = static_cast<GiObj*>(gi_raw);
        if (gi != nullptr) {
            constexpr uint64_t kPadDevId = 0x045E0B120001ULL;
            constexpr int kReadings = 1000;
            std::atomic<bool> done{false};
            std::atomic<int> errs{0};
            std::thread prod([&]() {
                for (int i = 1; i <= kReadings; ++i) {
                    scarlett_input_reading_record_t rec{};
                    rec.device_id = kPadDevId;
                    rec.input_kind = SCARLETT_GAMEINPUT_KIND_GAMEPAD;
                    rec.gamepad.buttons = static_cast<uint32_t>(i);
                    rec.gamepad.left_trigger = static_cast<float>(i % 100) / 100.0f;
                    rec.gamepad.right_trigger = 1.0f - rec.gamepad.left_trigger;
                    rec.gamepad.extended_buttons = 0xAA55u;

                    scarlett_input_ring_params_t rp{};
                    rp.instance_handle = gi->unix_instance_handle;
                    rp.op = SCARLETT_INPUT_OP_PUSH_READING;
                    rp.record_addr = reinterpret_cast<uint64_t>(&rec);
                    if (scarlett_failed(scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &rp))) {
                        errs.fetch_add(1);
                    }
                }
                done.store(true, std::memory_order_release);
            });
            while (!done.load(std::memory_order_acquire)) {
                void* rcur = nullptr;
                if (scarlett_succeeded(gi->lpVtbl->GetCurrentReading(gi, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &rcur))) {
                    auto* r = static_cast<GiReadingObj*>(rcur);
                    GiGamepadState gs{};
                    if (r->lpVtbl->GetGamepadState(r, &gs) == 0u || gs.extendedButtons != 0xAA55u) {
                        errs.fetch_add(1);
                    }
                    r->lpVtbl->Release(r);
                }
            }
            prod.join();
            ARENA_CHECK(sc, errs.load() == 0);

            void* r_cur = nullptr;
            ARENA_CHECK_HR(sc, gi->lpVtbl->GetCurrentReading(gi, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_cur), SCARLETT_OK);
            auto* cur_obj = static_cast<GiReadingObj*>(r_cur);
            const uint64_t cur_seq = cur_obj->lpVtbl->GetSequenceNumber(cur_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD);
            ARENA_CHECK(sc, cur_seq == static_cast<uint64_t>(kReadings));
            tel.s6.gameinput_readings_1000hz = static_cast<uint32_t>(cur_seq);

            void* r_prev = nullptr;
            ARENA_CHECK_HR(sc, gi->lpVtbl->GetPreviousReading(gi, cur_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_prev), SCARLETT_OK);
            auto* prev_obj = static_cast<GiReadingObj*>(r_prev);
            void* r_next = nullptr;
            if (prev_obj != nullptr) {
                ARENA_CHECK_HR(sc, gi->lpVtbl->GetNextReading(gi, prev_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD, nullptr, &r_next), SCARLETT_OK);
            }
            auto* next_obj = static_cast<GiReadingObj*>(r_next);
            if (next_obj != nullptr) {
                ARENA_CHECK(sc, next_obj->lpVtbl->GetSequenceNumber(next_obj, SCARLETT_GAMEINPUT_KIND_GAMEPAD) == cur_seq);
            }

            void* dev_raw = nullptr;
            cur_obj->lpVtbl->GetDevice(cur_obj, &dev_raw);
            auto* gdev = static_cast<GiDeviceObj*>(dev_raw);
            if (gdev != nullptr) {
                GiRumble rumble{0.85f, 0.45f, 0.70f, 0.30f};
                gdev->lpVtbl->SetRumbleState(gdev, &rumble);

                scarlett::audio_media::ScarlettRumbleTransportPacket pkt{};
                scarlett_input_ring_params_t qp{};
                qp.op = SCARLETT_INPUT_OP_QUERY_RUMBLE_PKT;
                qp.device_id = kPadDevId;
                qp.aux_addr = reinterpret_cast<uint64_t>(&pkt);
                qp.aux_bytes = sizeof(pkt);
                ARENA_CHECK_HR(sc, scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, &qp), SCARLETT_OK);
                tel.s6.gameinput_4motor_rumble_ok =
                    (pkt.gip_packet_len == 13u && pkt.gip_packet[5] == 0x0Fu &&
                     pkt.evdev_ff_type == FF_RUMBLE && pkt.evdev_strong_mag > 55000u);
                ARENA_CHECK(sc, tel.s6.gameinput_4motor_rumble_ok);

                gdev->lpVtbl->Release(gdev);
            }
            if (next_obj != nullptr) {
                next_obj->lpVtbl->Release(next_obj);
            }
            if (prev_obj != nullptr) {
                prev_obj->lpVtbl->Release(prev_obj);
            }
            cur_obj->lpVtbl->Release(cur_obj);
            ARENA_CHECK(sc, gi->lpVtbl->Release(gi) == 0u);
        }
    }

    /* 3. xgameruntime: 22 domains, QueryApiImpl Win64, XSystem, XTaskQueue/XAsync, XGameSave 12 MiB + >16 MiB error */
    {
        const char* env_saves = std::getenv("PROTON_XBOX_SAVES_DIR");
        const std::string tmp_saves =
            (env_saves != nullptr && env_saves[0] != '\0')
                ? std::string(env_saves)
                : ("/tmp/scarlett_arena_saves_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(tmp_saves, ec);
        ::setenv("PROTON_XBOX_SAVES_DIR", tmp_saves.c_str(), 1);
        ::setenv(SCARLETT_ENV_MODEL, tel.model_name.c_str(), 1);

        ARENA_CHECK_HR(sc, XGameRuntimeInitialize(), SCARLETT_OK);

        uint32_t avail_cnt = 0u;
        for (int f = 0; f < SCARLETT_GRF_COUNT; ++f) {
            uint32_t av = 0u;
            if (XGameRuntimeIsFeatureAvailable(static_cast<scarlett_game_runtime_feature_t>(f), &av) == SCARLETT_OK && av == 1u) {
                ++avail_cnt;
            }
        }
        ARENA_CHECK(sc, avail_cnt == static_cast<uint32_t>(SCARLETT_GRF_COUNT));
        tel.s6.xdl_domains_available = avail_cnt;

        static const scarlett_guid_t kSysGuid = SCARLETT_GUID_SYSTEM_DISPATCH_INIT;
        static const scarlett_guid_t kPlatGuid = SCARLETT_GUID_PLATFORM_DISPATCH_INIT;
        void* sys_tbl = nullptr;
        void* plat_tbl = nullptr;
        ARENA_CHECK_HR(sc, call_query_api_impl_win64(0u, &kSysGuid, &sys_tbl), SCARLETT_OK);
        ARENA_CHECK_HR(sc, call_query_api_impl_win64(0u, &kPlatGuid, &plat_tbl), SCARLETT_OK);
        ARENA_CHECK(sc, sys_tbl != nullptr && plat_tbl != nullptr);

        const XSystemDeviceType expected_dev = (tel.model == SCARLETT_MODEL_LOCKHART)
                                                   ? XSystemDeviceType_XboxScarlettLockhart
                                                   : XSystemDeviceType_XboxScarlettAnaconda;
        const XSystemDeviceType actual_dev = XSystemGetDeviceType();
        ARENA_CHECK(sc, actual_dev == expected_dev);
        tel.s6.xsystem_device_type = static_cast<uint32_t>(actual_dev);

        /* XTaskQueue + XAsync */
        XTaskQueueHandle tq = 0u;
        ARENA_CHECK_HR(sc,
                       XTaskQueueCreate(XTaskQueueDispatchMode_ThreadPool,
                                        XTaskQueueDispatchMode_Manual, &tq),
                       SCARLETT_OK);
        struct AsyncCtx { std::atomic<uint32_t> done{0u}; } actx;
        XAsyncBlock ab{};
        ab.queue = tq;
        ab.context = &actx;
        ab.callback = [](XAsyncBlock* b) {
            static_cast<AsyncCtx*>(b->context)->done.fetch_add(1u, std::memory_order_acq_rel);
        };
        ARENA_CHECK_HR(sc, XAsyncRun(&ab, [](XAsyncBlock*) -> scarlett_hresult_t { return SCARLETT_OK; }),
                       SCARLETT_OK);
        ARENA_CHECK(sc, XTaskQueueDispatch(tq, XTaskQueuePort_Completion, 500u));
        ARENA_CHECK_HR(sc, XAsyncGetStatus(&ab, true), SCARLETT_OK);
        ARENA_CHECK(sc, actx.done.load(std::memory_order_acquire) == 1u);
        tel.s6.xtaskqueue_ok = (actx.done.load(std::memory_order_acquire) == 1u);
        XTaskQueueCloseHandle(tq);

        /* XGameSave 12 MiB atomic commit + >16 MiB E_GS_UPDATE_TOO_BIG */
        XAsyncBlock uab{};
        ARENA_CHECK_HR(sc, XUserAddAsync(XUserAddOptions_AddDefaultUserSilently, &uab), SCARLETT_OK);
        XUserHandle user = 0u;
        ARENA_CHECK_HR(sc, XUserAddResult(&uab, &user), SCARLETT_OK);

        XGameSaveProviderHandle prov = 0u;
        ARENA_CHECK_HR(sc, XGameSaveInitializeProvider(user, "5CA11E77-0000-0000-0000-00004C2B5CA1", false, &prov), SCARLETT_OK);
        XGameSaveContainerHandle cont = 0u;
        ARENA_CHECK_HR(sc, XGameSaveCreateContainer(prov, "arena_save_12m", &cont), SCARLETT_OK);

        constexpr size_t k12MiB = 12u * 1024u * 1024u;
        std::vector<uint8_t> save_12m(k12MiB);
        for (size_t i = 0u; i < k12MiB; ++i) {
            save_12m[i] = static_cast<uint8_t>((i * 17u + (i >> 12)) & 0xFFu);
        }
        const uint32_t expected_hash = fnv1a_32(save_12m.data(), save_12m.size());

        XGameSaveUpdateHandle upd = 0u;
        ARENA_CHECK_HR(sc, XGameSaveCreateUpdate(cont, "Arena 12MiB", &upd), SCARLETT_OK);
        ARENA_CHECK_HR(sc, XGameSaveSubmitBlobWrite(upd, "world_state.bin", save_12m.data(), save_12m.size()), SCARLETT_OK);
        ARENA_CHECK_HR(sc, XGameSaveSubmitUpdate(upd), SCARLETT_OK);
        XGameSaveCloseUpdate(upd);

        const fs::path save_path = fs::path(tmp_saves) / "5CA11E77-0000-0000-0000-00004C2B5CA1" /
                                   "arena_save_12m" / "world_state.bin";
        ARENA_CHECK(sc, fs::exists(save_path, ec));
        ARENA_CHECK(sc, !fs::exists(fs::path(save_path.string() + ".tmp"), ec));
        const uintmax_t saved_sz = fs::file_size(save_path, ec);
        ARENA_CHECK(sc, !ec && saved_sz == k12MiB);

        std::vector<uint8_t> read_buf(sizeof(XGameSaveBlob) + k12MiB);
        const char* blob_names[] = {"world_state.bin"};
        uint32_t cnt = 1u;
        ARENA_CHECK_HR(sc,
                       XGameSaveReadBlobData(cont, blob_names, &cnt,
                                             static_cast<uint32_t>(read_buf.size()),
                                             reinterpret_cast<XGameSaveBlob*>(read_buf.data())),
                       SCARLETT_OK);
        auto* rb = reinterpret_cast<XGameSaveBlob*>(read_buf.data());
        ARENA_CHECK(sc, rb->info.size == k12MiB);
        ARENA_CHECK(sc, fnv1a_32(rb->data, rb->info.size) == expected_hash);
        tel.s6.xgamesave_12mib_bytes = rb->info.size;

        /* > 16 MiB limit -> E_GS_UPDATE_TOO_BIG */
        XGameSaveUpdateHandle big_upd = 0u;
        ARENA_CHECK_HR(sc, XGameSaveCreateUpdate(cont, "TooBig", &big_upd), SCARLETT_OK);
        std::vector<uint8_t> over16m(16u * 1024u * 1024u + 1u, 0xCCu);
        const scarlett_hresult_t big_hr =
            XGameSaveSubmitBlobWrite(big_upd, "overflow.bin", over16m.data(), over16m.size());
        ARENA_CHECK_HR(sc, big_hr, SCARLETT_E_GS_UPDATE_TOO_BIG);
        tel.s6.xgamesave_over16mib_rejected = (big_hr == SCARLETT_E_GS_UPDATE_TOO_BIG);
        XGameSaveCloseUpdate(big_upd);

        XGameSaveCloseContainer(cont);
        XGameSaveCloseProvider(prov);
        XUserCloseHandle(user);
        XGameRuntimeUninitialize();
        fs::remove_all(tmp_saves, ec);
        ::unsetenv("PROTON_XBOX_SAVES_DIR");
    }

    /* 4. Loader: MicrosoftGame.Config + strict 148B DDS+DX10 via GAME: CoW VFS + encrypted XVC refusal */
    {
        const fs::path pkg_dir(tel.package_dir);
        const fs::path cfg_path = pkg_dir / "MicrosoftGame.Config";
        scarlett::loader::MicrosoftGameConfig cfg{};
        ARENA_CHECK_HR(sc, scarlett::loader::parse_microsoft_game_config_file(cfg_path, cfg), SCARLETT_OK);
        ARENA_CHECK(sc, cfg.is_scarlett);
        ARENA_CHECK(sc, cfg.title_id == 0x5CA11E77u);
        ARENA_CHECK(sc, cfg.msa_app_id == "000000004C2B5CA1");
        ARENA_CHECK(sc, cfg.virtual_machine.present && cfg.virtual_machine.enabled);
        ARENA_CHECK(sc, !cfg.executables.empty() && cfg.executables[0].name == "ScarlettArena.exe");
        tel.s6.game_config_title_id = cfg.title_id_raw;

        const fs::path overlay_dir =
            fs::temp_directory_path() / ("scarlett_arena_cow_overlay_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(overlay_dir, ec);
        fs::create_directories(overlay_dir, ec);

        scarlett::loader::GameCowVfs vfs;
        ARENA_CHECK_HR(sc, vfs.mount_directory(pkg_dir, overlay_dir), SCARLETT_OK);
        ARENA_CHECK_HR(sc, vfs.write_direct_to_base_forbidden(), SCARLETT_ERR_VFS_READONLY_VIOLATION);

        auto read_and_verify_dds = [&](std::string_view game_path, uint32_t exp_dxgi,
                                       uint32_t exp_w, uint32_t exp_h,
                                       std::vector<uint8_t>& out_bytes) -> bool {
            uint64_t fh = 0u, fsz = 0u;
            scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
            if (vfs.open_file(game_path, SCARLETT_VFS_ACCESS_READ, fh, fsz, layer) != SCARLETT_OK) {
                return false;
            }
            if (layer != SCARLETT_VFS_LAYER_BASE_DIR || fsz <= 148u) {
                vfs.close_file(fh);
                return false;
            }
            out_bytes.resize(static_cast<size_t>(fsz));
            uint32_t nread = 0u;
            const scarlett_hresult_t rhr =
                vfs.read_file(fh, 0u, out_bytes.data(), static_cast<uint32_t>(fsz), nread);
            vfs.close_file(fh);
            if (rhr != SCARLETT_OK || nread != fsz) {
                return false;
            }
            ParsedDdsDx10 parsed{};
            if (!parse_strict_dds_dx10(out_bytes.data(), out_bytes.size(), parsed)) {
                return false;
            }
            return parsed.dxgi_format == exp_dxgi && parsed.width == exp_w && parsed.height == exp_h;
        };

        std::vector<uint8_t> bc7_orig, rgba_orig;
        tel.s6.dds_148b_bc7_ok =
            read_and_verify_dds("GAME:\\assets\\arena_albedo_bc7.dds", kDxgiFormatBc7Unorm, 16u, 16u, bc7_orig);
        tel.s6.dds_148b_rgba8_ok =
            read_and_verify_dds("GAME:\\assets\\arena_ui_rgba8.dds", kDxgiFormatR8G8B8A8Unorm, 16u, 16u, rgba_orig);
        ARENA_CHECK(sc, tel.s6.dds_148b_bc7_ok);
        ARENA_CHECK(sc, tel.s6.dds_148b_rgba8_ok);

        const uint32_t base_bc7_hash = fnv1a_32(bc7_orig.data(), bc7_orig.size());

        /* Mutate via CoW VFS -> must go strictly to overlay, leaving read-only base untouched! */
        uint64_t wh = 0u, wsz = 0u;
        scarlett_vfs_layer_t wlayer = SCARLETT_VFS_LAYER_NONE;
        ARENA_CHECK_HR(sc,
                       vfs.open_file("GAME:\\assets\\arena_albedo_bc7.dds",
                                     SCARLETT_VFS_ACCESS_READ | SCARLETT_VFS_ACCESS_WRITE,
                                     wh, wsz, wlayer),
                       SCARLETT_OK);
        ARENA_CHECK(sc, wlayer == SCARLETT_VFS_LAYER_OVERLAY);
        const uint8_t patch_bytes[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
        uint32_t nwritten = 0u;
        ARENA_CHECK_HR(sc, vfs.write_file(wh, 148u, patch_bytes, 4u, nwritten), SCARLETT_OK);
        ARENA_CHECK(sc, nwritten == 4u);
        ARENA_CHECK_HR(sc, vfs.close_file(wh), SCARLETT_OK);

        /* Base file on disk must remain byte-for-byte identical */
        std::ifstream base_ifs(pkg_dir / "assets" / "arena_albedo_bc7.dds", std::ios::binary);
        std::vector<uint8_t> disk_bytes((std::istreambuf_iterator<char>(base_ifs)),
                                        std::istreambuf_iterator<char>());
        tel.s6.cow_base_untouched = (disk_bytes.size() == bc7_orig.size() &&
                                     fnv1a_32(disk_bytes.data(), disk_bytes.size()) == base_bc7_hash);
        ARENA_CHECK(sc, tel.s6.cow_base_untouched);
        vfs.unmount();

        /* Clean-Room refusal on encrypted XVD/XVC (no encryption-disabled flag) */
        std::vector<uint8_t> enc_xvd(0x4000u, 0u);
        const uint64_t magic = SCARLETT_XVD_MAGIC_LE64;
        std::memcpy(enc_xvd.data() + SCARLETT_XVD_HEADER_OFFSET, &magic, sizeof(magic));
        uint32_t vflags = 0u;
        bool is_enc = false;
        scarlett::loader::GameCowVfs enc_vfs;
        const scarlett_hresult_t enc_hr =
            enc_vfs.mount_xvd_memory(std::move(enc_xvd), overlay_dir, vflags, is_enc);
        ARENA_CHECK_HR(sc, enc_hr, SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED);
        ARENA_CHECK(sc, is_enc);
        tel.s6.encrypted_xvc_rejected = (enc_hr == SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED && is_enc);

        fs::remove_all(overlay_dir, ec);
    }

    tel.s6.passed = sc.ok();
    return tel.s6.passed;
}

} // namespace scarlett::arena
