/*
 * Proton-Scarlett — xapu.dll PE64 Freestanding Shim
 *
 * Implements XApuConnect, XApuDisconnect, XApuEnqueueCommand,
 * XApuDequeueResult, XApuGetResultCount, XAPUCreateDecoder,
 * XAPUDestroyDecoder, XAPUProcess, XAPUFlush, and XAPUGetStatus.
 *
 * Fully compliant with Wine 11.0 PE isolation:
 * Strictly freestanding C11 (zero libc, zero <windows.h>, zero POSIX headers).
 */

#include "audio_pe_common.h"

scarlett_hresult_t SCARLETT_WIN64_API XApuConnect(
    const void *config,
    const void *client_info,
    void **out_handle) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    (void)config;
    (void)client_info;
    if (out_handle == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_handle = NULL;

    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_CONNECT;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        *out_handle = (void *)(uintptr_t)p.out_ptr_or_handle;
    }
    audio_pe_ods_printf(
        "[scarlett-xapu] XApuConnect(cfg=0x%llx, info=0x%llx, out=0x%llx) -> handle=0x%llx hr=0x%x ret=0x%llx\n",
        (unsigned long long)(uintptr_t)config,
        (unsigned long long)(uintptr_t)client_info,
        (unsigned long long)(uintptr_t)out_handle,
        (unsigned long long)(uintptr_t)*out_handle,
        (unsigned int)hr,
        (unsigned long long)ret);
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API XApuDisconnect(void *handle) {
    if (handle == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_DISCONNECT;
    p.handle_or_ptr = (uint64_t)(uintptr_t)handle;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API XApuEnqueueCommand(
    void *handle,
    const void *cmd_packet,
    uint32_t flags) {
    if (handle == NULL || cmd_packet == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_ENQUEUE;
    p.handle_or_ptr = (uint64_t)(uintptr_t)handle;
    p.out_ptr_or_handle = (uint64_t)(uintptr_t)cmd_packet;
    p.alignment = flags;
    p.size_or_count = 64u;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

uint32_t SCARLETT_WIN64_API XApuGetResultCount(
    void *handle,
    uint32_t queue_index) {
    if (handle == NULL) {
        return 0u;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_GET_COUNT;
    p.handle_or_ptr = (uint64_t)(uintptr_t)handle;
    p.alignment = queue_index;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        return (uint32_t)p.size_or_count;
    }
    return 0u;
}

scarlett_hresult_t SCARLETT_WIN64_API XApuDequeueResult(
    void *handle,
    void *out_result) {
    if (handle == NULL || out_result == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_DEQUEUE;
    p.handle_or_ptr = (uint64_t)(uintptr_t)handle;
    p.out_ptr_or_handle = (uint64_t)(uintptr_t)out_result;
    p.size_or_count = 64u;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API XAPUCreateDecoder(
    uint32_t codec_id,
    uint32_t sample_rate,
    uint32_t channels,
    const void *extradata,
    uint32_t extradata_size,
    void **out_decoder) {
    if (out_decoder == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_decoder = NULL;

    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_CREATE_DEC;
    p.alignment = codec_id;
    p.size_or_count = sample_rate;
    p.out_aux = (((uint64_t)channels) << 32) | (uint32_t)extradata_size;
    p.handle_or_ptr = (uint64_t)(uintptr_t)extradata;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        *out_decoder = (void *)(uintptr_t)p.out_ptr_or_handle;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API XAPUDestroyDecoder(void *decoder) {
    if (decoder == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_DESTROY_DEC;
    p.handle_or_ptr = (uint64_t)(uintptr_t)decoder;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API XAPUProcess(
    void *decoder,
    const void *input_buf,
    uint32_t input_size,
    void *output_pcm,
    uint32_t output_cap,
    uint32_t *out_written) {
    if (decoder == NULL || input_buf == NULL || output_pcm == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    if (out_written != NULL) {
        *out_written = 0u;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_PROCESS;
    p.handle_or_ptr = (uint64_t)(uintptr_t)decoder;
    p.alignment = input_size;
    p.size_or_count = output_cap;
    p.out_ptr_or_handle = (uint64_t)(uintptr_t)input_buf;
    p.out_aux = (uint64_t)(uintptr_t)output_pcm;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr) && out_written != NULL) {
        *out_written = (uint32_t)p.size_or_count;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API XAPUFlush(void *decoder) {
    if (decoder == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_XAPU_FLUSH;
    p.handle_or_ptr = (uint64_t)(uintptr_t)decoder;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API XAPUGetStatus(void *decoder, void *status_out) {
    (void)decoder;
    if (status_out != NULL) {
        audio_pe_zero_mem(status_out, 32);
    }
    return SCARLETT_OK;
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
