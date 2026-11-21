/*
 * Proton-Scarlett — audio_scarlett_unix.so Native Linux Unixlib Implementation
 *
 * System V AMD64 ELF64 unixlib backend for:
 *   - xaudio2_9.dll (multi-threaded mixer, XAPO, HRTF, Opus 0x704F, ADAT 512KB ring, HSRC)
 *   - AcpHal.dll (ApuAlloc/ApuFree/ApuCreateHeap, SHAPE contexts, FFmpeg AV_CODEC_ID_XMA2)
 *   - GameInput.dll (v4.0 1000Hz ring buffer, 4-motor GIP hidraw + evdev FF_RUMBLE fallback)
 *   - Bink2Scarlett.dll (12 exports + YUV->RGB BT.709/BT.601)
 *   - amd_fidelityfx_loader_xbox.dll & amd_fidelityfx_upscaler_xbox.dll (FSR 3 / FSR 4.1.2)
 *   - Diagnostic auto-stub reporting (PROTON_XBOX_STRICT=0|1)
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <vector>

#include "audio_media_unix_abi.h"
#include "gameinput_backend.hpp"
#include "media_bink2_fsr.hpp"
#include "xapu_codecs.hpp"
#include "xaudio2_mixer.hpp"
#include "xdsp_math.hpp"

namespace {

struct AudioUnixState {
    std::mutex mutex;
    bool initialized = false;
    uint32_t console_model = SCARLETT_MODEL_ANACONDA;
    uint32_t strict_mode = 0;

    scarlett::audio_media::XAudio2MixerManager mixer;
    scarlett::audio_media::ApuAndShapeManager apu_shape;
    scarlett::audio_media::GameInputBackend gameinput;
    scarlett::audio_media::Bink2Manager bink2;
    scarlett::audio_media::FidelityFxManager fsr;

    bool has_last_stub = false;
    uint32_t total_stub_hits = 0;
    scarlett_stub_context_t last_stub{};
};

AudioUnixState& g_state() {
    static AudioUnixState state;
    return state;
}

bool read_env_strict_mode() noexcept {
    const char* v = std::getenv(SCARLETT_ENV_STRICT);
    return (v != nullptr && std::strcmp(v, "1") == 0);
}

scarlett_hresult_t handle_init(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_unix_init_params_t*>(args);
    p->out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    if (p->abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        p->module_id != SCARLETT_UNIX_MOD_AUDIO) {
        p->out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return SCARLETT_ERR_ABI_VERSION_MISMATCH;
    }

    auto& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.console_model = p->console_model;
    st.strict_mode = (p->strict_mode != 0u || read_env_strict_mode()) ? 1u : 0u;
    st.initialized = true;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t handle_open_stream(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_engine_create_params_t*>(args);
    uint64_t handle = 0u;
    uint32_t active_sink = 0u;
    const scarlett_hresult_t hr = g_state().mixer.create_engine(*p, handle, active_sink);
    p->out_engine_handle = handle;
    p->out_active_sink = active_sink;
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_close_stream(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_engine_create_params_t*>(args);
    const scarlett_hresult_t hr = g_state().mixer.destroy_engine(p->out_engine_handle);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_submit_pcm(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_pcm_submit_params_t*>(args);
    scarlett_audio_voice_buffer_params_t vbuf{};
    vbuf.voice_handle = p->stream_handle;
    vbuf.buffer_addr = p->buffer_addr;
    const uint32_t bytes_per_sample =
        (p->format_tag == SCARLETT_AUDIO_FORMAT_IEEE_FLOAT) ? 4u : 2u;
    vbuf.buffer_bytes = p->frame_count * std::max(1u, p->channels) * bytes_per_sample;
    const scarlett_hresult_t hr = g_state().mixer.submit_voice_buffer(vbuf);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_acphal_decode(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_acphal_decode_params_t*>(args);
    const uint8_t* src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(p->src_xma2_addr));
    uint8_t* dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(p->dst_pcm_addr));
    uint32_t decoded = 0u;
    const scarlett_hresult_t hr = g_state().apu_shape.acphal_decode_xma2(
        p->context_handle, src, p->src_bytes, dst, p->dst_capacity_bytes, decoded);
    p->out_decoded_bytes = decoded;
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_input_poll(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_input_ring_params_t*>(args);
    if (p->record_addr == 0u) {
        p->out_status = SCARLETT_E_POINTER;
        return SCARLETT_E_POINTER;
    }
    auto* out = reinterpret_cast<scarlett_input_reading_record_t*>(
        static_cast<uintptr_t>(p->record_addr));
    const scarlett_hresult_t hr = g_state().gameinput.get_current_reading(
        p->instance_handle, p->input_kind, p->device_id, *out);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_input_set_rumble(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_input_rumble_params_t*>(args);
    const scarlett_hresult_t hr = g_state().gameinput.set_rumble_state(*p);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_bink2_decode(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_media_bink2_frame_params_t*>(args);
    scarlett_bink2_op_params_t op{};
    op.bink_handle = p->decoder_handle;
    op.op = SCARLETT_BINK2_OP_DO_FRAME;
    scarlett_hresult_t hr = g_state().bink2.handle_op(op);
    if (scarlett_succeeded(hr) && p->dst_y_addr != 0u) {
        op.op = SCARLETT_BINK2_OP_COPY_TO_BUFFER;
        op.path_or_buf_addr = p->dst_y_addr;
        op.dest_pitch = static_cast<int32_t>(p->width * 4u);
        op.dest_height = p->height;
        hr = g_state().bink2.handle_op(op);
    }
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_create_voice(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_voice_create_params_t*>(args);
    uint64_t handle = 0u;
    const scarlett_hresult_t hr = g_state().mixer.create_voice(*p, handle);
    p->out_voice_handle = handle;
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_destroy_voice(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_voice_ctrl_params_t*>(args);
    const scarlett_hresult_t hr = g_state().mixer.destroy_voice(p->voice_handle);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_voice_control(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_voice_ctrl_params_t*>(args);
    const scarlett_hresult_t hr = g_state().mixer.voice_control(*p);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_submit_voice_buffer(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_voice_buffer_params_t*>(args);
    const scarlett_hresult_t hr = g_state().mixer.submit_voice_buffer(*p);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_render_quantum(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_render_quantum_params_t*>(args);
    const scarlett_hresult_t hr = g_state().mixer.render_quantum(*p);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_apu_mem(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_apu_mem_params_t*>(args);
    auto& mgr = g_state().apu_shape;
    scarlett_hresult_t hr = SCARLETT_E_INVALIDARG;
    switch (p->op) {
        case SCARLETT_APU_OP_CREATE_HEAP:
            hr = mgr.create_heap(p->size_or_count);
            break;
        case SCARLETT_APU_OP_ALLOC:
            hr = mgr.apu_alloc(p->size_or_count, p->alignment, p->out_ptr_or_handle);
            break;
        case SCARLETT_APU_OP_FREE:
            hr = mgr.apu_free(p->handle_or_ptr);
            break;
        case SCARLETT_APU_OP_ACPHAL_CREATE:
            hr = mgr.acphal_create(p->out_ptr_or_handle);
            break;
        case SCARLETT_APU_OP_ACPHAL_RELEASE:
            hr = mgr.acphal_release(p->handle_or_ptr);
            break;
        case SCARLETT_APU_OP_ALLOC_SHAPE_CTX:
            hr = mgr.alloc_shape_contexts(p->handle_or_ptr,
                                          static_cast<uint32_t>(p->size_or_count),
                                          p->out_ptr_or_handle);
            break;
        case SCARLETT_APU_OP_FREE_SHAPE_CTX:
            hr = mgr.free_shape_contexts(p->handle_or_ptr);
            break;
        default:
            hr = SCARLETT_E_INVALIDARG;
            break;
    }
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_xdsp_op(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_xdsp_params_t*>(args);
    scarlett_hresult_t hr = SCARLETT_E_INVALIDARG;
    switch (p->op) {
        case SCARLETT_XDSP_OP_FFT:
        case SCARLETT_XDSP_OP_IFFT: {
            float* re = reinterpret_cast<float*>(static_cast<uintptr_t>(p->in_real_addr));
            float* im = reinterpret_cast<float*>(static_cast<uintptr_t>(p->in_imag_or_ir_addr));
            const bool inv = (p->op == SCARLETT_XDSP_OP_IFFT);
            hr = scarlett::audio_media::xdsp_fft_f32(p->count, re, im, inv)
                     ? SCARLETT_OK
                     : SCARLETT_E_INVALIDARG;
            break;
        }
        case SCARLETT_XDSP_OP_CONV_REVERB: {
            const float* in = reinterpret_cast<const float*>(static_cast<uintptr_t>(p->in_real_addr));
            const float* ir = reinterpret_cast<const float*>(static_cast<uintptr_t>(p->in_imag_or_ir_addr));
            float* out = reinterpret_cast<float*>(static_cast<uintptr_t>(p->out_real_addr));
            scarlett::audio_media::ConvolutionReverb rev;
            if (!rev.configure(ir, p->ir_len, p->param_f0) || in == nullptr || out == nullptr || p->count == 0u) {
                hr = SCARLETT_E_INVALIDARG;
            } else {
                rev.process_block(in, out, p->count);
                hr = SCARLETT_OK;
            }
            break;
        }
        case SCARLETT_XDSP_OP_HRTF_RENDER: {
            const float* mono = reinterpret_cast<const float*>(static_cast<uintptr_t>(p->in_real_addr));
            float* stereo = reinterpret_cast<float*>(static_cast<uintptr_t>(p->out_real_addr));
            if (mono == nullptr || stereo == nullptr || p->count == 0u) {
                hr = SCARLETT_E_INVALIDARG;
            } else {
                scarlett::audio_media::HrtfBinauralRenderer hrtf;
                hrtf.configure(48000u);
                hrtf.set_direction(p->param_f0, p->param_f1, 1.0f);
                hrtf.render_mono_to_stereo(mono, stereo, p->count);
                hr = SCARLETT_OK;
            }
            break;
        }
        default:
            hr = SCARLETT_E_INVALIDARG;
            break;
    }
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_input_ring_op(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_input_ring_params_t*>(args);
    auto& gi = g_state().gameinput;
    scarlett_hresult_t hr = SCARLETT_E_INVALIDARG;

    switch (p->op) {
        case SCARLETT_INPUT_OP_CREATE_INSTANCE:
            hr = gi.create_instance(p->instance_handle);
            break;
        case SCARLETT_INPUT_OP_DESTROY_INSTANCE:
            hr = gi.destroy_instance(p->instance_handle);
            break;
        case SCARLETT_INPUT_OP_PUSH_READING: {
            if (p->record_addr == 0u) {
                hr = SCARLETT_E_POINTER;
                break;
            }
            const auto* rec = reinterpret_cast<const scarlett_input_reading_record_t*>(
                static_cast<uintptr_t>(p->record_addr));
            hr = gi.push_reading(p->instance_handle, *rec);
            break;
        }
        case SCARLETT_INPUT_OP_GET_CURRENT:
        case SCARLETT_INPUT_OP_GET_NEXT:
        case SCARLETT_INPUT_OP_GET_PREVIOUS: {
            if (p->record_addr == 0u) {
                hr = SCARLETT_E_POINTER;
                break;
            }
            auto* rec = reinterpret_cast<scarlett_input_reading_record_t*>(
                static_cast<uintptr_t>(p->record_addr));
            if (p->op == SCARLETT_INPUT_OP_GET_CURRENT) {
                hr = gi.get_current_reading(p->instance_handle, p->input_kind, p->device_id, *rec);
            } else if (p->op == SCARLETT_INPUT_OP_GET_NEXT) {
                hr = gi.get_next_reading(p->instance_handle, p->ref_sequence, p->input_kind, p->device_id, *rec);
            } else {
                hr = gi.get_previous_reading(p->instance_handle, p->ref_sequence, p->input_kind, p->device_id, *rec);
            }
            break;
        }
        case SCARLETT_INPUT_OP_GET_TIMESTAMP:
            p->out_timestamp_us = gi.current_timestamp_us();
            hr = SCARLETT_OK;
            break;
        case SCARLETT_INPUT_OP_QUERY_RUMBLE_PKT: {
            if (p->aux_addr == 0u ||
                p->aux_bytes < sizeof(scarlett::audio_media::ScarlettRumbleTransportPacket)) {
                hr = SCARLETT_E_INVALIDARG;
                break;
            }
            scarlett::audio_media::ScarlettRumbleTransportPacket pkt{};
            hr = gi.query_last_rumble_packet(p->device_id, pkt);
            if (scarlett_succeeded(hr)) {
                std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(p->aux_addr)),
                            &pkt, sizeof(pkt));
                p->out_aux_written = static_cast<uint32_t>(sizeof(pkt));
            }
            break;
        }
        case SCARLETT_INPUT_OP_SET_TRANSPORT:
            hr = gi.set_transport_mode(p->input_kind);
            break;
        default:
            hr = SCARLETT_E_INVALIDARG;
            break;
    }

    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_bink2_op(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_bink2_op_params_t*>(args);
    const scarlett_hresult_t hr = g_state().bink2.handle_op(*p);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_fsr_op(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_fsr_op_params_t*>(args);
    const scarlett_hresult_t hr = g_state().fsr.handle_op(*p);
    p->out_status = hr;
    return hr;
}

scarlett_hresult_t handle_report_stub(void* args) noexcept {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_audio_report_stub_params_t*>(args);
    auto& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);

    const uint32_t strict = (st.strict_mode != 0u || read_env_strict_mode()) ? 1u : 0u;
    p->ctx.strict_mode = strict;
    if (strict != 0u) {
        p->ctx.flags |= SCARLETT_STUB_FLAG_STRICT_MODE;
    }
    p->ctx.return_hr = scarlett_stub_enforce_error(p->ctx.return_hr);
    p->ctx.hit_count = ++st.total_stub_hits;
    if (p->ctx.thread_id == 0u) {
        p->ctx.thread_id = static_cast<uint32_t>(getpid());
    }

    st.last_stub = p->ctx;
    st.has_last_stub = true;

    const scarlett_stub_action_t action = scarlett_stub_resolve_action(&p->ctx);
    p->out_action = static_cast<uint32_t>(action);
    p->out_status = SCARLETT_OK;

    std::fprintf(stderr,
                 "[scarlett-audio-stub] dll=%s sym=%s ord=%u slot=%u rva=0x%llx "
                 "rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx strict=%u hr=0x%08x\n",
                 p->ctx.dll_name,
                 p->ctx.symbol_name,
                 p->ctx.ordinal,
                 p->ctx.vtable_slot,
                 static_cast<unsigned long long>(p->ctx.caller_rva),
                 static_cast<unsigned long long>(p->ctx.rcx),
                 static_cast<unsigned long long>(p->ctx.rdx),
                 static_cast<unsigned long long>(p->ctx.r8),
                 static_cast<unsigned long long>(p->ctx.r9),
                 strict,
                 static_cast<unsigned int>(p->ctx.return_hr));
    return SCARLETT_OK;
}

} // namespace

extern "C" {

int32_t scarlett_audio_unix_dispatch(uint32_t opcode, void* params) {
    switch (opcode) {
        case SCARLETT_AUDIO_UNIX_INIT:
            return handle_init(params);
        case SCARLETT_AUDIO_UNIX_OPEN_STREAM:
            return handle_open_stream(params);
        case SCARLETT_AUDIO_UNIX_CLOSE_STREAM:
            return handle_close_stream(params);
        case SCARLETT_AUDIO_UNIX_SUBMIT_PCM:
            return handle_submit_pcm(params);
        case SCARLETT_AUDIO_UNIX_ACPHAL_DECODE:
            return handle_acphal_decode(params);
        case SCARLETT_AUDIO_UNIX_INPUT_POLL:
            return handle_input_poll(params);
        case SCARLETT_AUDIO_UNIX_INPUT_SET_RUMBLE:
            return handle_input_set_rumble(params);
        case SCARLETT_AUDIO_UNIX_BINK2_DECODE:
            return handle_bink2_decode(params);
        case SCARLETT_AUDIO_UNIX_EX_CREATE_ENGINE:
            return handle_open_stream(params);
        case SCARLETT_AUDIO_UNIX_EX_DESTROY_ENGINE:
            return handle_close_stream(params);
        case SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE:
            return handle_create_voice(params);
        case SCARLETT_AUDIO_UNIX_EX_DESTROY_VOICE:
            return handle_destroy_voice(params);
        case SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL:
            return handle_voice_control(params);
        case SCARLETT_AUDIO_UNIX_EX_SUBMIT_VOICE_BUFFER:
            return handle_submit_voice_buffer(params);
        case SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM:
            return handle_render_quantum(params);
        case SCARLETT_AUDIO_UNIX_EX_APU_MEM:
            return handle_apu_mem(params);
        case SCARLETT_AUDIO_UNIX_EX_XDSP_OP:
            return handle_xdsp_op(params);
        case SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP:
            return handle_input_ring_op(params);
        case SCARLETT_AUDIO_UNIX_EX_BINK2_OP:
            return handle_bink2_op(params);
        case SCARLETT_AUDIO_UNIX_EX_FSR_OP:
            return handle_fsr_op(params);
        case SCARLETT_AUDIO_UNIX_EX_REPORT_STUB:
            return handle_report_stub(params);
        default:
            return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
}

typedef int32_t (*wine_unixlib_entry_t)(void* args);

static int32_t unix_op_init(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_INIT, args); }
static int32_t unix_op_open_stream(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_OPEN_STREAM, args); }
static int32_t unix_op_close_stream(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_CLOSE_STREAM, args); }
static int32_t unix_op_submit_pcm(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_SUBMIT_PCM, args); }
static int32_t unix_op_acphal_decode(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_ACPHAL_DECODE, args); }
static int32_t unix_op_input_poll(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_INPUT_POLL, args); }
static int32_t unix_op_input_set_rumble(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_INPUT_SET_RUMBLE, args); }
static int32_t unix_op_bink2_decode(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_BINK2_DECODE, args); }
static int32_t unix_op_ex_create_engine(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_CREATE_ENGINE, args); }
static int32_t unix_op_ex_destroy_engine(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_DESTROY_ENGINE, args); }
static int32_t unix_op_ex_create_voice(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_CREATE_VOICE, args); }
static int32_t unix_op_ex_destroy_voice(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_DESTROY_VOICE, args); }
static int32_t unix_op_ex_voice_control(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_VOICE_CONTROL, args); }
static int32_t unix_op_ex_submit_voice_buf(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_SUBMIT_VOICE_BUFFER, args); }
static int32_t unix_op_ex_render_quantum(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_RENDER_QUANTUM, args); }
static int32_t unix_op_ex_apu_mem(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_APU_MEM, args); }
static int32_t unix_op_ex_xdsp_op(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_XDSP_OP, args); }
static int32_t unix_op_ex_input_ring_op(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_INPUT_RING_OP, args); }
static int32_t unix_op_ex_bink2_op(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_BINK2_OP, args); }
static int32_t unix_op_ex_fsr_op(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_FSR_OP, args); }
static int32_t unix_op_ex_report_stub(void* args) { return scarlett_audio_unix_dispatch(SCARLETT_AUDIO_UNIX_EX_REPORT_STUB, args); }

__attribute__((visibility("default")))
extern const wine_unixlib_entry_t __wine_unix_call_funcs[SCARLETT_AUDIO_UNIX_EX_OP_COUNT];

const wine_unixlib_entry_t __wine_unix_call_funcs[SCARLETT_AUDIO_UNIX_EX_OP_COUNT] = {
    unix_op_init,
    unix_op_open_stream,
    unix_op_close_stream,
    unix_op_submit_pcm,
    unix_op_acphal_decode,
    unix_op_input_poll,
    unix_op_input_set_rumble,
    unix_op_bink2_decode,
    unix_op_ex_create_engine,
    unix_op_ex_destroy_engine,
    unix_op_ex_create_voice,
    unix_op_ex_destroy_voice,
    unix_op_ex_voice_control,
    unix_op_ex_submit_voice_buf,
    unix_op_ex_render_quantum,
    unix_op_ex_apu_mem,
    unix_op_ex_xdsp_op,
    unix_op_ex_input_ring_op,
    unix_op_ex_bink2_op,
    unix_op_ex_fsr_op,
    unix_op_ex_report_stub,
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_AUDIO_UNIX_EX_OP_COUNT,
    "__wine_unix_call_funcs must cover all 21 audio_media unixlib opcodes");

} /* extern "C" */
