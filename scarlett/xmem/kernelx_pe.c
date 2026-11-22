/*
 * Proton-Scarlett — kernelx.dll PE64 Layer
 *
 * Strict Wine 11.0 PE64 isolation:
 *   - ZERO direct references to libc.so, libvulkan.so, or POSIX syscalls.
 *   - All memory, topology, time-sensitive, and /dev/ntsync operations route
 *     strictly through WINE_UNIX_CALL into xmem_unix.so.
 *   - Legacy unsupported Xbox One ESRAM API (MapTitleEsramPages) routes through
 *     the diagnostic auto-stub contract (Zero Silent Stubs policy).
 *   - Compiles cleanly with `clang --target=x86_64-w64-mingw32 -ffreestanding -c`.
 */

#include "xmem_pe_bridge.h"
#include "uwp_bridge/forwarders/sys/uwp_pkg_identity.h"

static uint32_t g_esram_stub_hits = 0u;

static scarlett_mem_cache_attr_t kernelx_protect_to_cache(uint32_t protect) {
    if ((protect & XMEM_PAGE_GPU_COHERENT) != 0u) {
        return SCARLETT_MEM_CACHE_GPU_COHERENT;
    }
    if ((protect & XMEM_PAGE_WRITECOMBINE) != 0u) {
        return SCARLETT_MEM_CACHE_WRITECOMBINE;
    }
    if ((protect & XMEM_PAGE_NOCACHE) != 0u) {
        return SCARLETT_MEM_CACHE_UNCACHED;
    }
    return SCARLETT_MEM_CACHE_WRITEBACK;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL TitleMemoryStatus(
    XMEM_TITLE_MEMORY_STATUS *out_status) {
    if (out_status == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_title_mem_status_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.is_job_query = 0u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_TITLE_MEM_STATUS, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }

    xmem_pe_zero(out_status, sizeof(*out_status));
    out_status->dwLength = (uint32_t)sizeof(XMEM_TITLE_MEMORY_STATUS);
    out_status->ConsoleModel = params.out_console_model;
    out_status->ullTotalMem = params.out_total_mem;
    out_status->ullAvailMem = params.out_avail_mem;
    out_status->ullLegacyUsed = params.out_legacy_used;
    out_status->ullLegacyPeak = params.out_legacy_peak;
    out_status->ullLegacyAvail = params.out_legacy_avail;
    out_status->ullLegacyLimit = params.out_legacy_limit;
    out_status->ullTitleUsed = params.out_title_used;
    out_status->ullTitlePeak = params.out_title_peak;
    out_status->ullTitleAvail = params.out_title_avail;
    out_status->ullTitleLimit = params.out_title_limit;
    out_status->ullFastPoolBytes = params.out_fast_pool_bytes;
    out_status->ullStandardPoolBytes = params.out_standard_pool_bytes;
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL JobTitleMemoryStatus(
    XMEM_JOB_TITLE_MEMORY_STATUS *out_status) {
    if (out_status == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_title_mem_status_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.is_job_query = 1u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_TITLE_MEM_STATUS, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }

    xmem_pe_zero(out_status, sizeof(*out_status));
    out_status->dwLength = (uint32_t)sizeof(XMEM_JOB_TITLE_MEMORY_STATUS);
    out_status->ConsoleModel = params.out_console_model;
    out_status->ullTotalMem = params.out_total_mem;
    out_status->ullAvailMem = params.out_avail_mem;
    out_status->ullLegacyUsed = params.out_legacy_used;
    out_status->ullLegacyPeak = params.out_legacy_peak;
    out_status->ullLegacyLimit = params.out_legacy_limit;
    out_status->ullTitleUsed = params.out_title_used;
    out_status->ullTitlePeak = params.out_title_peak;
    out_status->ullTitleLimit = params.out_title_limit;
    out_status->ullFastPoolBytes = params.out_fast_pool_bytes;
    out_status->ullStandardPoolBytes = params.out_standard_pool_bytes;
    return params.out_status;
}

SCARLETT_XMEM_API void * SCARLETT_XMEM_CALL KernelXVirtualAlloc(
    void *address,
    uint64_t size_bytes,
    uint32_t alloc_type,
    uint32_t protect) {
    uint64_t out_base = 0u;
    scarlett_mem_region_t region = SCARLETT_MEM_REGION_UNKNOWN;
    if ((alloc_type & XMEM_MEM_GRAPHICS) != 0u) {
        region = SCARLETT_MEM_REGION_GRAPHICS;
    } else if ((alloc_type & XMEM_MEM_TITLE) != 0u) {
        region = SCARLETT_MEM_REGION_TITLE;
    }

    scarlett_xmem_reserve_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.requested_base = (uint64_t)(uintptr_t)address;
    params.size_bytes = size_bytes;
    params.region = (uint32_t)region;
    params.flags = (alloc_type & UINT32_C(0xFFFFF000)) |
                   (protect & UINT32_C(0x00000FFF));

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_RESERVE_RANGE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return NULL;
    }
    out_base = params.out_base;
    return (void *)(uintptr_t)out_base;
}

SCARLETT_XMEM_API uint32_t SCARLETT_XMEM_CALL KernelXVirtualFree(
    void *address,
    uint64_t size_bytes,
    uint32_t free_type) {
    scarlett_xmem_free_range_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.base_addr = (uint64_t)(uintptr_t)address;
    params.size_bytes = size_bytes;
    params.free_type = free_type;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_FREE_RANGE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return 0u;
    }
    return 1u;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXVirtualQuery(
    const void *address,
    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION *out_info) {
    if (out_info == NULL) {
        return SCARLETT_E_POINTER;
    }
    xmem_pe_zero(out_info, sizeof(*out_info));

    scarlett_xmem_virtual_query_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.query_addr = (uint64_t)(uintptr_t)address;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_VIRTUAL_QUERY, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }

    out_info->BaseAddress = params.out_base_address;
    out_info->AllocationBase = params.out_allocation_base;
    out_info->AllocationProtect = params.out_allocation_protect;
    out_info->Region = params.out_region;
    out_info->RegionSize = params.out_region_size;
    out_info->State = params.out_state;
    out_info->Protect = params.out_protect;
    out_info->Type = params.out_type;
    out_info->CacheAttr = params.out_cache_attr;
    out_info->AllocationId = params.out_alloc_id;
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL AllocateTitlePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    uint32_t protect,
    uint64_t *out_pfn_array) {
    if (out_pfn_array == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_alloc_pages_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.page_count = page_count;
    params.page_size_bytes = page_size_bytes;
    params.out_pfn_array_addr = (uint64_t)(uintptr_t)out_pfn_array;
    params.cache_attr = (uint32_t)kernelx_protect_to_cache(protect);
    params.flags = XMEM_PHYS_FLAG_TITLE;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_ALLOC_PHYS_PAGES, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL FreeTitlePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    const uint64_t *pfn_array) {
    if (pfn_array == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_alloc_pages_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.page_count = page_count;
    params.page_size_bytes = page_size_bytes;
    params.out_pfn_array_addr = (uint64_t)(uintptr_t)pfn_array;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_FREE_PHYS_PAGES, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL MapTitleEsramPages(
    void *virtual_addr,
    uint64_t page_count,
    uint32_t flags,
    const uint32_t *esram_page_array) {
    scarlett_loader_report_stub_params_t params;
    xmem_pe_zero(&params, sizeof(params));

    params.ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    params.ctx.flags = SCARLETT_STUB_FLAG_REMOVED_IN_ABI;
    params.ctx.ordinal = SCARLETT_STUB_ORDINAL_NONE;
    params.ctx.vtable_slot = SCARLETT_STUB_SLOT_NONE;
    params.ctx.return_address = (uint64_t)(uintptr_t)__builtin_return_address(0);
    params.ctx.caller_rva = params.ctx.return_address;
    params.ctx.rcx = (uint64_t)(uintptr_t)virtual_addr;
    params.ctx.rdx = page_count;
    params.ctx.r8 = (uint64_t)flags;
    params.ctx.r9 = (uint64_t)(uintptr_t)esram_page_array;
    params.ctx.return_hr = SCARLETT_E_NOTIMPL;
    params.ctx.hit_count = ++g_esram_stub_hits;

    xmem_pe_copy_cstr(params.ctx.dll_name, sizeof(params.ctx.dll_name), "kernelx.dll");
    xmem_pe_copy_cstr(params.ctx.symbol_name, sizeof(params.ctx.symbol_name), "MapTitleEsramPages");
    xmem_pe_copy_cstr(params.ctx.caller_module_name,
                      sizeof(params.ctx.caller_module_name),
                      "<scarlett_caller>");

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_REPORT_STUB, &params);
    return scarlett_stub_enforce_error(scarlett_failed(rc) ? rc : params.out_status);
}

/* ============================================================================
 * Thread Topology & Time-Sensitive Exports
 * ============================================================================ */

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadGetProcessorTopology(
    XMEM_PROCESSOR_TOPOLOGY *out_topology) {
    if (out_topology == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_thread_topology_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.action = 0u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_THREAD_TOPOLOGY, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }

    xmem_pe_zero(out_topology, sizeof(*out_topology));
    out_topology->HostTotalCpus = params.out_host_total_cpus;
    out_topology->HostPCoreCpus = params.out_host_pcore_cpus;
    out_topology->HostECoreCpus = params.out_host_ecore_cpus;
    out_topology->IsHybrid = params.out_is_hybrid;
    out_topology->XboxCoreCount = params.out_xbox_core_count;
    out_topology->XboxThreadCount = params.out_xbox_thread_count;
    for (uint32_t i = 0u; i < XMEM_XBOX_TITLE_THREADS; ++i) {
        out_topology->HostCpuForXboxThread[i] = params.out_host_cpu_map[i];
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadSetXboxCoreAffinity(
    uint32_t xbox_thread_mask,
    uint32_t *out_applied) {
    scarlett_xmem_thread_topology_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.action = 1u;
    params.xbox_thread_mask = xbox_thread_mask;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_THREAD_TOPOLOGY, &params);
    if (out_applied != NULL) {
        *out_applied = params.out_affinity_applied;
    }
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadSetTimeSensitive(
    uint32_t is_time_sensitive,
    uint32_t *out_previous_state,
    uint32_t *out_os_priority_applied) {
    scarlett_xmem_thread_ts_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.action = 1u;
    params.is_time_sensitive = is_time_sensitive != 0u ? 1u : 0u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_THREAD_TIME_SENSITIVE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_previous_state != NULL) {
        *out_previous_state = params.out_previous_state;
    }
    if (out_os_priority_applied != NULL) {
        *out_os_priority_applied = params.out_os_priority_applied;
    }
    return params.out_status;
}

SCARLETT_XMEM_API uint32_t SCARLETT_XMEM_CALL XThreadIsTimeSensitive(void) {
    scarlett_xmem_thread_ts_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.action = 0u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_THREAD_TIME_SENSITIVE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return 0u;
    }
    return params.out_current_state;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadAssertNotTimeSensitive(void) {
    scarlett_xmem_thread_ts_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.action = 2u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_THREAD_TIME_SENSITIVE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

/* ============================================================================
 * KernelX Synchronization Primitives (/dev/ntsync + Futex/Eventfd Fallback)
 * ============================================================================ */

static scarlett_hresult_t kernelx_sync_create_common(
    scarlett_xmem_sync_type_t type,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status) {
    if (out_handle == NULL) {
        return SCARLETT_E_POINTER;
    }
    *out_handle = 0u;

    scarlett_xmem_sync_create_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.sync_type = (uint32_t)type;
    params.arg0 = arg0;
    params.arg1 = arg1;
    params.flags = flags;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_CREATE, &params);
    if (out_sync_status != NULL) {
        xmem_pe_zero(out_sync_status, sizeof(*out_sync_status));
        out_sync_status->Backend = params.out_backend;
        out_sync_status->NtsyncErrno = params.out_ntsync_errno;
    }
    if (scarlett_failed(rc)) {
        return rc;
    }
    *out_handle = params.out_handle;
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCreateEvent(
    uint32_t manual_reset,
    uint32_t initial_state,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status) {
    return kernelx_sync_create_common(
        XMEM_SYNC_TYPE_EVENT, manual_reset, initial_state, flags,
        out_handle, out_sync_status);
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXSetEvent(
    uint64_t handle,
    uint32_t *out_prev_state) {
    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_EVENT_SET;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    if (out_prev_state != NULL) {
        *out_prev_state = params.out_prev_val;
    }
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXResetEvent(
    uint64_t handle,
    uint32_t *out_prev_state) {
    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_EVENT_RESET;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    if (out_prev_state != NULL) {
        *out_prev_state = params.out_prev_val;
    }
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXPulseEvent(
    uint64_t handle,
    uint32_t *out_prev_state) {
    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_EVENT_PULSE;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    if (out_prev_state != NULL) {
        *out_prev_state = params.out_prev_val;
    }
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCreateSemaphore(
    uint32_t initial_count,
    uint32_t max_count,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status) {
    return kernelx_sync_create_common(
        XMEM_SYNC_TYPE_SEMAPHORE, initial_count, max_count, flags,
        out_handle, out_sync_status);
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXReleaseSemaphore(
    uint64_t handle,
    uint32_t release_count,
    uint32_t *out_prev_count) {
    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_SEM_RELEASE;
    params.arg0 = release_count;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    if (out_prev_count != NULL) {
        *out_prev_count = params.out_prev_val;
    }
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCreateMutex(
    uint32_t initial_owner,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status) {
    return kernelx_sync_create_common(
        XMEM_SYNC_TYPE_MUTEX, initial_owner, 0u, flags,
        out_handle, out_sync_status);
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXReleaseMutex(
    uint64_t handle,
    uint32_t *out_prev_count) {
    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_MUTEX_UNLOCK;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    if (out_prev_count != NULL) {
        *out_prev_count = params.out_prev_val;
    }
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXWaitForMultipleObjects(
    uint32_t handle_count,
    const uint64_t *handles,
    uint32_t wait_all,
    uint64_t timeout_ns,
    uint32_t *out_signaled_index,
    uint32_t *out_timed_out) {
    if (handles == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_sync_wait_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handles_addr = (uint64_t)(uintptr_t)handles;
    params.timeout_ns = timeout_ns;
    params.handle_count = handle_count;
    params.wait_all = wait_all != 0u ? 1u : 0u;
    params.out_signaled_index = XMEM_SYNC_WAIT_TIMEOUT_INDEX;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_WAIT, &params);
    if (out_signaled_index != NULL) {
        *out_signaled_index = params.out_signaled_index;
    }
    if (out_timed_out != NULL) {
        *out_timed_out = params.out_timed_out;
    }
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXReadSyncState(
    uint64_t handle,
    XMEM_SYNC_STATUS *out_sync_status) {
    if (out_sync_status == NULL) {
        return SCARLETT_E_POINTER;
    }
    xmem_pe_zero(out_sync_status, sizeof(*out_sync_status));

    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_READ_STATE;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    out_sync_status->Backend = params.out_backend;
    out_sync_status->PrevValue = params.out_prev_val;
    out_sync_status->AuxValue = params.out_aux_val;
    return scarlett_failed(rc) ? rc : params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCloseSyncHandle(
    uint64_t handle) {
    scarlett_xmem_sync_op_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.handle = handle;
    params.op = (uint32_t)XMEM_SYNC_OP_CLOSE;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SYNC_OP, &params);
    return scarlett_failed(rc) ? rc : params.out_status;
}

/* ============================================================================
 * UWP / Scarlett Package Identity & AppPolicy Exports
 * ============================================================================ */

#if !defined(_WIN32)
static const uwp_pkg_provider_t *g_kernelx_host_pkg_provider = NULL;

void kernelx_set_host_pkg_provider(const uwp_pkg_provider_t *provider) {
    g_kernelx_host_pkg_provider = provider;
}
#endif

static void kernelx_resolve_pkg(uwp_pkg_resolved_t *out) {
#if defined(_WIN32)
    uwp_pkg_resolve_win32(0, out);
#else
    if (g_kernelx_host_pkg_provider != NULL) {
        uwp_pkg_resolve_with_provider(g_kernelx_host_pkg_provider, 0, out);
    } else {
        uwp_pkg_zero(out, sizeof(*out));
    }
#endif
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL GetCurrentPackageFullName(
    uint32_t *package_full_name_length,
    uint16_t *package_full_name) {
    uwp_pkg_resolved_t pkg;
    kernelx_resolve_pkg(&pkg);
    if (!pkg.valid) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    return uwp_pkg_copy_out_u16(pkg.full_name, package_full_name_length, package_full_name);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL GetCurrentPackageId(
    uint32_t *buffer_length,
    uint8_t *buffer) {
    uwp_pkg_resolved_t pkg;
    kernelx_resolve_pkg(&pkg);
    if (!pkg.valid) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    return uwp_pkg_copy_out_package_id(&pkg, buffer_length, buffer);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL GetCurrentPackageFamilyName(
    uint32_t *package_family_name_length,
    uint16_t *package_family_name) {
    uwp_pkg_resolved_t pkg;
    kernelx_resolve_pkg(&pkg);
    if (!pkg.valid) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    return uwp_pkg_copy_out_u16(
        pkg.family_name, package_family_name_length, package_family_name);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL GetCurrentApplicationUserModelId(
    uint32_t *application_user_model_id_length,
    uint16_t *application_user_model_id) {
    uwp_pkg_resolved_t pkg;
    kernelx_resolve_pkg(&pkg);
    if (!pkg.valid || pkg.aumid[0] == 0u) {
        return UWP_APPMODEL_ERROR_NO_APPLICATION;
    }
    return uwp_pkg_copy_out_u16(
        pkg.aumid, application_user_model_id_length, application_user_model_id);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL GetPackagePathByFullName(
    const uint16_t *package_full_name,
    uint32_t *path_length,
    uint16_t *path) {
    if (path_length == NULL || package_full_name == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    uwp_pkg_resolved_t pkg;
    kernelx_resolve_pkg(&pkg);
    return uwp_pkg_get_path_by_full_name(&pkg, package_full_name, path_length, path);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL GetCurrentPackagePath(
    uint32_t *path_length,
    uint16_t *path) {
    uwp_pkg_resolved_t pkg;
    kernelx_resolve_pkg(&pkg);
    if (!pkg.valid || pkg.package_dir[0] == 0u) {
        return UWP_APPMODEL_ERROR_NO_PACKAGE;
    }
    return uwp_pkg_copy_out_u16(pkg.package_dir, path_length, path);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL AppPolicyGetProcessTerminationMethod(
    void *process_token,
    uint32_t *policy) {
    (void)process_token;
    if (policy == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    *policy = UWP_APPPOLICY_TERM_TERMINATE_PROCESS;
    return UWP_ERROR_SUCCESS;
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL AppPolicyGetWindowingModel(
    void *process_token,
    uint32_t *policy) {
    (void)process_token;
    if (policy == NULL) {
        return UWP_ERROR_INVALID_PARAMETER;
    }
    *policy = UWP_APPPOLICY_WIN_UNIVERSAL;
    return UWP_ERROR_SUCCESS;
}

#if defined(_WIN32)
uint32_t SCARLETT_XMEM_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == 1u) { /* DLL_PROCESS_ATTACH */
        if (xmem_pe_init_unix_bridge() != 0) {
            return 0u;
        }
        scarlett_unix_init_params_t params;
        xmem_pe_zero(&params, sizeof(params));
        params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
        params.module_id = (uint32_t)SCARLETT_UNIX_MOD_XMEM;
        params.console_model = (uint32_t)SCARLETT_MODEL_ANACONDA;
        if (scarlett_failed(xmem_pe_unix_call(SCARLETT_XMEM_UNIX_INIT, &params))) {
            return 0u;
        }
    }
    return 1u;
}
#endif
