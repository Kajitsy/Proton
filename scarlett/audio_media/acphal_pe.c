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

typedef scarlett_hresult_t (SCARLETT_WIN64_API *scarlett_acphal_slot_fn_t)(
    void *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9);

typedef struct scarlett_acphal_obj_s {
    const scarlett_acphal_slot_fn_t *lpVtbl;
    uint64_t unix_handle;
} scarlett_acphal_obj_t;

static uint32_t g_acphal_slot_hits[32];

static scarlett_hresult_t SCARLETT_WIN64_API acphal_vtbl_connect(
    void *self,
    uint64_t cmd_queue_len,
    uint64_t msg_queue_len,
    uint64_t r9) {
    (void)r9;
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_ods_printf(
        "[scarlett-acphal] IAcpHal::Connect(self=0x%llx, cmd_q=%llu, msg_q=%llu) ret=0x%llx OK\n",
        (unsigned long long)(uintptr_t)self,
        (unsigned long long)cmd_queue_len,
        (unsigned long long)msg_queue_len,
        (unsigned long long)ret);
    return SCARLETT_OK;
}

static scarlett_hresult_t SCARLETT_WIN64_API acphal_vtbl_disconnect(
    void *self,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    (void)rdx;
    (void)r8;
    (void)r9;
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_ods_printf(
        "[scarlett-acphal] IAcpHal::Disconnect(self=0x%llx) ret=0x%llx OK\n",
        (unsigned long long)(uintptr_t)self,
        (unsigned long long)ret);
    return SCARLETT_OK;
}

#define DEFINE_ACPHAL_SLOT(N) \
    static scarlett_hresult_t SCARLETT_WIN64_API acphal_vtbl_slot_##N( \
        void *self, \
        uint64_t rdx, \
        uint64_t r8, \
        uint64_t r9) { \
        uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0); \
        uint32_t hit = __atomic_add_fetch(&g_acphal_slot_hits[N], 1u, __ATOMIC_RELAXED); \
        if (hit <= 4u) { \
            audio_pe_ods_printf( \
                "[scarlett-acphal] IAcpHal::Slot%u(self=0x%llx, rdx=0x%llx, r8=0x%llx, r9=0x%llx) ret=0x%llx hit=%u OK\n", \
                (unsigned)(N), \
                (unsigned long long)(uintptr_t)self, \
                (unsigned long long)rdx, \
                (unsigned long long)r8, \
                (unsigned long long)r9, \
                (unsigned long long)ret, \
                (unsigned)hit); \
        } \
        return SCARLETT_OK; \
    }

DEFINE_ACPHAL_SLOT(2)
DEFINE_ACPHAL_SLOT(3)
DEFINE_ACPHAL_SLOT(4)
DEFINE_ACPHAL_SLOT(5)
DEFINE_ACPHAL_SLOT(6)
DEFINE_ACPHAL_SLOT(7)
DEFINE_ACPHAL_SLOT(8)
DEFINE_ACPHAL_SLOT(9)
DEFINE_ACPHAL_SLOT(10)
DEFINE_ACPHAL_SLOT(11)
DEFINE_ACPHAL_SLOT(12)
DEFINE_ACPHAL_SLOT(13)
DEFINE_ACPHAL_SLOT(14)
DEFINE_ACPHAL_SLOT(15)
DEFINE_ACPHAL_SLOT(16)
DEFINE_ACPHAL_SLOT(17)
DEFINE_ACPHAL_SLOT(18)
DEFINE_ACPHAL_SLOT(19)
DEFINE_ACPHAL_SLOT(20)
DEFINE_ACPHAL_SLOT(21)
DEFINE_ACPHAL_SLOT(22)
DEFINE_ACPHAL_SLOT(23)
DEFINE_ACPHAL_SLOT(24)
DEFINE_ACPHAL_SLOT(25)
DEFINE_ACPHAL_SLOT(26)
DEFINE_ACPHAL_SLOT(27)
DEFINE_ACPHAL_SLOT(28)
DEFINE_ACPHAL_SLOT(29)
DEFINE_ACPHAL_SLOT(30)
DEFINE_ACPHAL_SLOT(31)

static const scarlett_acphal_slot_fn_t g_acphal_vtbl[32] = {
    acphal_vtbl_connect,
    acphal_vtbl_disconnect,
    acphal_vtbl_slot_2,
    acphal_vtbl_slot_3,
    acphal_vtbl_slot_4,
    acphal_vtbl_slot_5,
    acphal_vtbl_slot_6,
    acphal_vtbl_slot_7,
    acphal_vtbl_slot_8,
    acphal_vtbl_slot_9,
    acphal_vtbl_slot_10,
    acphal_vtbl_slot_11,
    acphal_vtbl_slot_12,
    acphal_vtbl_slot_13,
    acphal_vtbl_slot_14,
    acphal_vtbl_slot_15,
    acphal_vtbl_slot_16,
    acphal_vtbl_slot_17,
    acphal_vtbl_slot_18,
    acphal_vtbl_slot_19,
    acphal_vtbl_slot_20,
    acphal_vtbl_slot_21,
    acphal_vtbl_slot_22,
    acphal_vtbl_slot_23,
    acphal_vtbl_slot_24,
    acphal_vtbl_slot_25,
    acphal_vtbl_slot_26,
    acphal_vtbl_slot_27,
    acphal_vtbl_slot_28,
    acphal_vtbl_slot_29,
    acphal_vtbl_slot_30,
    acphal_vtbl_slot_31
};

static scarlett_acphal_obj_t g_acphal_obj = {
    g_acphal_vtbl,
    0u
};

scarlett_hresult_t SCARLETT_WIN64_API AcpHalCreate(void **out_hal_handle) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    if (out_hal_handle == NULL) {
        return SCARLETT_E_POINTER;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ACPHAL_CREATE;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr)) {
        g_acphal_obj.unix_handle = p.out_ptr_or_handle;
    }
    *out_hal_handle = &g_acphal_obj;
    audio_pe_ods_printf(
        "[scarlett-acphal] AcpHalCreate(out=0x%llx) -> obj=0x%llx unix=0x%llx ret=0x%llx OK\n",
        (unsigned long long)(uintptr_t)out_hal_handle,
        (unsigned long long)(uintptr_t)&g_acphal_obj,
        (unsigned long long)g_acphal_obj.unix_handle,
        (unsigned long long)ret);
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalRelease(void *hal_handle) {
    uint64_t unix_handle = (uint64_t)(uintptr_t)hal_handle;
    if (hal_handle == (void *)&g_acphal_obj) {
        unix_handle = g_acphal_obj.unix_handle;
    }
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ACPHAL_RELEASE;
    p.handle_or_ptr = unix_handle;
    return audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
}

static uint8_t g_shape_ctx_fallback_pool[262144];

scarlett_hresult_t SCARLETT_WIN64_API AcpHalAllocateShapeContexts(void *pShapeContexts) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    if (pShapeContexts == NULL) {
        audio_pe_ods_printf(
            "[scarlett-acphal] AcpHalAllocateShapeContexts(NULL) ret=0x%llx -> E_POINTER\n",
            (unsigned long long)ret);
        return SCARLETT_E_POINTER;
    }

    uint8_t *base = (uint8_t *)pShapeContexts;
    const uint64_t context_count = *(const uint64_t *)(base + 0x10u);
    uint64_t alloc_bytes = context_count * UINT64_C(64);
    if (alloc_bytes < UINT64_C(65536)) {
        alloc_bytes = UINT64_C(65536);
    }

    void *buf = NULL;
    scarlett_audio_apu_mem_params_t p;
    audio_pe_zero_mem(&p, sizeof(p));
    p.op = (uint32_t)SCARLETT_APU_OP_ALLOC;
    p.size_or_count = alloc_bytes;
    p.alignment = 64u;
    const scarlett_hresult_t hr = audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
    if (scarlett_succeeded(hr) && p.out_ptr_or_handle != 0u) {
        buf = (void *)(uintptr_t)p.out_ptr_or_handle;
    } else if (alloc_bytes <= sizeof(g_shape_ctx_fallback_pool)) {
        buf = g_shape_ctx_fallback_pool;
    } else {
        audio_pe_ods_printf(
            "[scarlett-acphal] AcpHalAllocateShapeContexts(p=0x%llx, count=%llu, bytes=%llu) ret=0x%llx -> E_OUTOFMEMORY\n",
            (unsigned long long)(uintptr_t)pShapeContexts,
            (unsigned long long)context_count,
            (unsigned long long)alloc_bytes,
            (unsigned long long)ret);
        return SCARLETT_E_OUTOFMEMORY;
    }

    audio_pe_zero_mem(buf, (size_t)alloc_bytes);
    *(void **)(base + 0x38u) = buf;
    audio_pe_ods_printf(
        "[scarlett-acphal] AcpHalAllocateShapeContexts(p=0x%llx, count=%llu, bytes=%llu) -> buf=0x%llx ret=0x%llx OK\n",
        (unsigned long long)(uintptr_t)pShapeContexts,
        (unsigned long long)context_count,
        (unsigned long long)alloc_bytes,
        (unsigned long long)(uintptr_t)buf,
        (unsigned long long)ret);
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API AcpHalReleaseShapeContexts(void *pShapeContexts) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    if (pShapeContexts != NULL) {
        uint8_t *base = (uint8_t *)pShapeContexts;
        void *buf = *(void **)(base + 0x38u);
        if (buf != NULL && buf != (void *)g_shape_ctx_fallback_pool) {
            scarlett_audio_apu_mem_params_t p;
            audio_pe_zero_mem(&p, sizeof(p));
            p.op = (uint32_t)SCARLETT_APU_OP_FREE;
            p.handle_or_ptr = (uint64_t)(uintptr_t)buf;
            (void)audio_pe_unix_call(SCARLETT_AUDIO_UNIX_EX_APU_MEM, &p);
        }
        *(void **)(base + 0x38u) = NULL;
    }
    audio_pe_ods_printf(
        "[scarlett-acphal] AcpHalReleaseShapeContexts(p=0x%llx) ret=0x%llx OK\n",
        (unsigned long long)(uintptr_t)pShapeContexts,
        (unsigned long long)ret);
    return SCARLETT_OK;
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

#define SCARLETT_APU_MAP_SLOTS 64u
typedef struct scarlett_apu_map_entry_s {
    void *virtual_address;
    uint32_t apu_address;
    uint32_t in_use;
} scarlett_apu_map_entry_t;

static scarlett_apu_map_entry_t g_apu_addr_map[SCARLETT_APU_MAP_SLOTS];

int32_t SCARLETT_WIN64_API ApuIsVirtualAddressValid(const void *virtual_address) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "AcpHal.dll",
        "ApuIsVirtualAddressValid",
        ret,
        (uint64_t)(uintptr_t)virtual_address,
        0u,
        0u,
        0u);
    return virtual_address != NULL ? 1 : 0;
}

scarlett_hresult_t SCARLETT_WIN64_API ApuMapVirtualAddress(
    const void *virtual_address,
    uint32_t *apu_address) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "AcpHal.dll",
        "ApuMapVirtualAddress",
        ret,
        (uint64_t)(uintptr_t)virtual_address,
        (uint64_t)(uintptr_t)apu_address,
        0u,
        0u);
    if (apu_address == NULL) {
        return SCARLETT_E_POINTER;
    }
    if (virtual_address == NULL) {
        *apu_address = 0u;
        return SCARLETT_E_INVALIDARG;
    }
    const uintptr_t va = (uintptr_t)virtual_address;
    if (va <= UINT32_MAX) {
        *apu_address = (uint32_t)va;
        return SCARLETT_OK;
    }
    for (size_t i = 0u; i < SCARLETT_APU_MAP_SLOTS; ++i) {
        if (g_apu_addr_map[i].in_use != 0u && g_apu_addr_map[i].virtual_address == virtual_address) {
            *apu_address = g_apu_addr_map[i].apu_address;
            return SCARLETT_OK;
        }
    }
    for (size_t i = 0u; i < SCARLETT_APU_MAP_SLOTS; ++i) {
        if (g_apu_addr_map[i].in_use == 0u) {
            const uint32_t synthetic = UINT32_C(0x80000000) + (uint32_t)(i * 0x1000u);
            g_apu_addr_map[i].virtual_address = (void *)(uintptr_t)va;
            g_apu_addr_map[i].apu_address = synthetic;
            g_apu_addr_map[i].in_use = 1u;
            *apu_address = synthetic;
            return SCARLETT_OK;
        }
    }
    *apu_address = (uint32_t)va;
    return SCARLETT_OK;
}

scarlett_hresult_t SCARLETT_WIN64_API ApuMapApuAddress(
    uint32_t apu_address,
    void **virtual_address) {
    uint64_t ret = (uint64_t)(uintptr_t)__builtin_return_address(0);
    audio_pe_log_stub_call(
        "AcpHal.dll",
        "ApuMapApuAddress",
        ret,
        (uint64_t)apu_address,
        (uint64_t)(uintptr_t)virtual_address,
        0u,
        0u);
    if (virtual_address == NULL) {
        return SCARLETT_E_POINTER;
    }
    *virtual_address = NULL;
    if (apu_address == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    for (size_t i = 0u; i < SCARLETT_APU_MAP_SLOTS; ++i) {
        if (g_apu_addr_map[i].in_use != 0u && g_apu_addr_map[i].apu_address == apu_address) {
            *virtual_address = g_apu_addr_map[i].virtual_address;
            return SCARLETT_OK;
        }
    }
    *virtual_address = (void *)(uintptr_t)apu_address;
    return SCARLETT_OK;
}

#if defined(_WIN32) || defined(__MINGW32__)
uint32_t SCARLETT_WIN64_API DllMain(void *inst, uint32_t reason, void *reserved) {
    (void)inst;
    (void)reserved;
    return audio_pe_dll_main(reason);
}
#endif
