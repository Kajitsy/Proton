/*
 * Proton-Scarlett — AcpHal.dll PE64 Freestanding Shim
 *
 * Implements ApuCreateHeap, ApuAlloc, ApuFree, AcpHalCreate, AcpHalRelease,
 * AcpHalAllocateShapeContexts, AcpHalReleaseShapeContexts, AcpHalDecodeXma2,
 * and diagnostic auto-stub for AcpHalSubmitFlowgraph.
 */

#include "audio_pe_common.h"

scarlett_hresult_t SCARLETT_WIN64_API ApuCreateHeap(
    uint64_t initial_size_bytes,
    uint64_t max_size_bytes) {
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_CREATE_HEAP;
    p.size_or_count = max_size_bytes >= initial_size_bytes ? max_size_bytes : initial_size_bytes;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API ApuAlloc(
    void **virtual_address,
    void **physical_address,
    uint64_t size_bytes,
    uint32_t alignment_bytes,
    uint32_t flags) {
    (void)flags;
    if (virtual_address == NULL) {
        return SCARLETT_E_POINTER;
    }
    *virtual_address = NULL;
    if (physical_address != NULL) {
        *physical_address = NULL;
    }

    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ALLOC;
    p.size_or_count = size_bytes;
    p.alignment = alignment_bytes;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        void *ptr = (void *)(uintptr_t)p.out_ptr_or_handle;
        *virtual_address = ptr;
        if (physical_address != NULL) {
            *physical_address = ptr;
        }
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API ApuFree(void *virtual_address) {
    if (virtual_address == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_FREE;
    p.handle_or_ptr = (uint64_t)(uintptr_t)virtual_address;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalCreate(void **out_hal_handle) {
    if (out_hal_handle == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_hal_handle = NULL;
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ACPHAL_CREATE;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        *out_hal_handle = (void *)(uintptr_t)p.out_ptr_or_handle;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalRelease(void *hal_handle) {
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ACPHAL_RELEASE;
    p.handle_or_ptr = (uint64_t)(uintptr_t)hal_handle;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalAllocateShapeContexts(
    void *hal_handle,
    uint32_t context_count,
    void **out_contexts) {
    if (out_contexts == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_contexts = NULL;
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ALLOC_SHAPE_CTX;
    p.handle_or_ptr = (uint64_t)(uintptr_t)hal_handle;
    p.size_or_count = context_count;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        *out_contexts = (void *)(uintptr_t)p.out_ptr_or_handle;
    }
    return hr;
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalReleaseShapeContexts(
    void *hal_handle,
    void *contexts) {
    (void)hal_handle;
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_FREE_SHAPE_CTX;
    p.handle_or_ptr = (uint64_t)(uintptr_t)contexts;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalDecodeXma2(
    void *context_handle,
    const uint8_t *src_xma2,
    uint32_t src_bytes,
    uint8_t *dst_pcm,
    uint32_t dst_capacity_bytes,
    uint32_t *out_decoded_bytes) {
    if (out_decoded_bytes == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_decoded_bytes = 0u;
    scarlett_audio_acphal_decode_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.context_handle = (uint64_t)(uintptr_t)context_handle;
    p.src_xma2_addr = (uint64_t)(uintptr_t)src_xma2;
    p.dst_pcm_addr = (uint64_t)(uintptr_t)dst_pcm;
    p.src_bytes = src_bytes;
    p.dst_capacity_bytes = dst_capacity_bytes;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_ACPHAL_DECODE, &p);
    *out_decoded_bytes = p.out_decoded_bytes;
    return hr;
}

static const scarlett_stub_descriptor_t g_stub_acphal_submit_flowgraph = {
    SCARLETT_STUB_THUNK_ABI_VERSION,
    SCARLETT_STUB_FLAG_NONE,
    SCARLETT_STUB_ORDINAL_NONE,
    SCARLETT_STUB_SLOT_NONE,
    SCARLETT_ERR_UNIMPLEMENTED_STUB,
    0u,
    "AcpHal.dll",
    "AcpHalSubmitFlowgraph"
};

scarlett_hresult_t SCARLETT_WIN64_API AcpHalSubmitFlowgraph(
    void *hal_handle,
    const void *flowgraph,
    uint32_t flags,
    uint64_t reserved) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    return audio_pe_stub_dispatch_local(
        &g_stub_acphal_submit_flowgraph,
        ret,
        (uint64_t)(uintptr_t)hal_handle,
        (uint64_t)(uintptr_t)flowgraph,
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
