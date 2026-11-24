/*
 * Proton-Scarlett — xmem.dll PE64 Layer
 *
 * Strict Wine 11.0 PE64 isolation:
 *   - ZERO direct references to libc.so, libvulkan.so, or POSIX syscalls.
 *   - All native Linux memory & UMA operations cross into xmem_unix.so
 *     exclusively via WINE_UNIX_CALL (or the host test bridge on Linux).
 *   - Compiles cleanly with `clang --target=x86_64-w64-mingw32 -ffreestanding -c`.
 */

#include "xmem_pe_bridge.h"

#if !defined(_WIN32)
static scarlett_unix_call_fn_t g_xmem_unix_bridge = scarlett_xmem_unix_call;

SCARLETT_XMEM_API void scarlett_xmem_set_unix_call_bridge(scarlett_unix_call_fn_t fn) {
    g_xmem_unix_bridge = fn;
}

SCARLETT_XMEM_API scarlett_unix_call_fn_t scarlett_xmem_get_unix_call_bridge(void) {
    return g_xmem_unix_bridge;
}
#endif

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemInitialize(
    scarlett_console_model_t model,
    uint32_t strict_mode) {
    scarlett_unix_init_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    params.module_id = (uint32_t)SCARLETT_UNIX_MOD_XMEM;
    params.console_model = (uint32_t)model;
    params.strict_mode = strict_mode != 0u ? 1u : 0u;
    params.shader_mode = (uint32_t)SCARLETT_SHADER_MODE_ADAPTIVE;
    params.net_mode = (uint32_t)SCARLETT_NET_MODE_OFFLINE;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_INIT, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemSetConsoleModel(
    scarlett_console_model_t model,
    uint32_t reset_allocations) {
    scarlett_xmem_set_model_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.console_model = (uint32_t)model;
    params.reset_allocations = reset_allocations != 0u ? 1u : 0u;
    params.strict_mode = UINT32_C(0xFFFFFFFF);

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_SET_MODEL, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemAllocatePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    scarlett_mem_cache_attr_t cache_attr,
    uint32_t flags,
    uint64_t *out_pfn_array,
    uint64_t *out_alloc_id) {
    if (out_pfn_array == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_alloc_pages_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.page_count = page_count;
    params.page_size_bytes = page_size_bytes;
    params.out_pfn_array_addr = (uint64_t)(uintptr_t)out_pfn_array;
    params.cache_attr = (uint32_t)cache_attr;
    params.flags = flags;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_ALLOC_PHYS_PAGES, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_alloc_id != NULL) {
        *out_alloc_id = params.out_alloc_id;
    }
    {
        static uint32_t g_phys_alloc_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_phys_alloc_seq, 1u, __ATOMIC_RELAXED);
        uint64_t total_bytes = page_count * page_size_bytes;
        if (total_bytes >= (UINT64_C(4) << 20) || seq <= 16u || (seq % 100u) == 0u ||
            scarlett_failed(params.out_status)) {
            xmem_pe_log_diag("[scarlett-xmem-phys-alloc]",
                             "xmem.dll",
                             "XMemAllocatePhysicalPages",
                             (uint64_t)(uintptr_t)__builtin_return_address(0),
                             total_bytes,
                             (uint64_t)cache_attr,
                             (uint64_t)flags,
                             params.out_alloc_id,
                             params.out_status);
        }
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemFreePhysicalPages(
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
    {
        static uint32_t g_phys_free_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_phys_free_seq, 1u, __ATOMIC_RELAXED);
        uint64_t total_bytes = page_count * page_size_bytes;
        if (total_bytes >= (UINT64_C(4) << 20) || seq <= 16u || (seq % 100u) == 0u ||
            scarlett_failed(params.out_status)) {
            xmem_pe_log_diag("[scarlett-xmem-phys-free]",
                             "xmem.dll",
                             "XMemFreePhysicalPages",
                             (uint64_t)(uintptr_t)__builtin_return_address(0),
                             total_bytes,
                             page_count,
                             page_size_bytes,
                             (uint64_t)seq,
                             params.out_status);
        }
    }
    return params.out_status;
}

SCARLETT_XMEM_API void * SCARLETT_XMEM_CALL XMemVirtualAlloc(
    void *requested_base,
    uint64_t size_bytes,
    uint32_t alloc_type,
    uint32_t arg4,
    uint64_t arg5) {
    if ((alloc_type & (XMEM_MEM_RESERVE_PLACEHOLDER | XMEM_MEM_REPLACE_PLACEHOLDER)) != 0u) {
        uint32_t win32_alloc = 0u;
        uint32_t win32_prot = XMEM_PAGE_NOACCESS;
        if ((alloc_type & XMEM_MEM_RESERVE_PLACEHOLDER) != 0u) {
            win32_alloc = XMEM_MEM_RESERVE | XMEM_MEM_RESERVE_PLACEHOLDER;
            win32_prot = XMEM_PAGE_NOACCESS;
        } else {
            win32_alloc = XMEM_MEM_RESERVE | XMEM_MEM_COMMIT | XMEM_MEM_REPLACE_PLACEHOLDER;
            win32_prot = XMEM_PAGE_READWRITE;
        }
        void *va2_res = xmem_pe_virtual_alloc2(
            requested_base, (size_t)size_bytes, win32_alloc, win32_prot);
        int32_t va2_status = (va2_res != NULL) ? 0 : (int32_t)xmem_pe_get_last_error();
        xmem_pe_log_diag(va2_res != NULL ? "[scarlett-xmem-placeholder]" : "[scarlett-xmem-fail]",
                         "xmem.dll",
                         "XMemVirtualAlloc",
                         (uint64_t)(uintptr_t)__builtin_return_address(0),
                         (uint64_t)(uintptr_t)requested_base,
                         size_bytes,
                         (uint64_t)alloc_type,
                         (uint64_t)(uintptr_t)va2_res,
                         va2_status);
        if (va2_res == NULL) {
            return NULL;
        }
        if (arg5 > UINT64_C(0x10000) && (arg5 & 7u) == 0u) {
            *(uint64_t *)(uintptr_t)arg5 = (uint64_t)(uintptr_t)va2_res;
        }
        return va2_res;
    }

    uint32_t combined_prot = arg4 | (uint32_t)arg5;
    uint32_t cache_bits = combined_prot & (XMEM_PAGE_NOCACHE | XMEM_PAGE_WRITECOMBINE);
    uint32_t low_prot4 = arg4 & UINT32_C(0xF);
    uint32_t low_prot5 = (uint32_t)arg5 & UINT32_C(0xF);

    uint32_t protect = XMEM_PAGE_READWRITE;
    if ((arg5 & XMEM_PAGE_READWRITE) != 0u || (arg4 & XMEM_PAGE_READWRITE) != 0u ||
        low_prot5 == UINT32_C(0x3) || low_prot4 == UINT32_C(0x3)) {
        protect = XMEM_PAGE_READWRITE | cache_bits;
    } else if (low_prot5 == XMEM_PAGE_READONLY || low_prot4 == XMEM_PAGE_READONLY) {
        protect = XMEM_PAGE_READONLY | cache_bits;
    } else {
        protect = XMEM_PAGE_READWRITE | cache_bits;
    }

    scarlett_mem_region_t region = SCARLETT_MEM_REGION_UNKNOWN;
    if ((alloc_type & XMEM_MEM_GRAPHICS) != 0u) {
        region = SCARLETT_MEM_REGION_GRAPHICS;
    } else if ((alloc_type & XMEM_MEM_TITLE) != 0u) {
        region = SCARLETT_MEM_REGION_TITLE;
    } else if (requested_base == NULL) {
        if ((alloc_type & XMEM_MEM_PHYSICAL) != 0u ||
            (combined_prot & (XMEM_PAGE_GPU_COHERENT | XMEM_PAGE_WRITECOMBINE)) != 0u) {
            region = SCARLETT_MEM_REGION_GRAPHICS;
        } else {
            region = SCARLETT_MEM_REGION_TITLE;
        }
    }

    scarlett_xmem_reserve_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.requested_base = (uint64_t)(uintptr_t)requested_base;
    params.size_bytes = size_bytes;
    params.region = (uint32_t)region;
    params.flags = (alloc_type & UINT32_C(0xFFFFF000)) |
                   (protect & UINT32_C(0x00000FFF));

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_RESERVE_RANGE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        int32_t fail_hr = scarlett_failed(rc) ? rc : params.out_status;
        xmem_pe_log_diag("[scarlett-xmem-fail]",
                         "xmem.dll",
                         "XMemVirtualAlloc",
                         (uint64_t)(uintptr_t)__builtin_return_address(0),
                         (uint64_t)(uintptr_t)requested_base,
                         size_bytes,
                         (uint64_t)alloc_type,
                         (uint64_t)arg4,
                         fail_hr);
        return NULL;
    }

    /* Support legacy callers/tests passing an out_base pointer (>64KB and 8-byte aligned) */
    if (arg5 > UINT64_C(0x10000) && (arg5 & 7u) == 0u) {
        *(uint64_t *)(uintptr_t)arg5 = params.out_base;
    }

    {
        static uint32_t g_valloc_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_valloc_seq, 1u, __ATOMIC_RELAXED);
        if (size_bytes >= (UINT64_C(4) << 20) || seq <= 16u || (seq % 100u) == 0u) {
            xmem_pe_log_diag("[scarlett-xmem-alloc]",
                             "xmem.dll",
                             "XMemVirtualAlloc",
                             (uint64_t)(uintptr_t)__builtin_return_address(0),
                             params.out_base,
                             size_bytes,
                             (uint64_t)alloc_type,
                             (uint64_t)protect,
                             params.out_status);
        }
    }

    return (void *)(uintptr_t)params.out_base;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemVirtualFree(
    uint64_t base_addr,
    uint64_t size_bytes,
    uint32_t free_type) {
    if ((free_type & (XMEM_MEM_PRESERVE_PLACEHOLDER | XMEM_MEM_COALESCE_PLACEHOLDERS)) != 0u) {
        int32_t vf_resolved = 0;
        int32_t vf_ok = xmem_pe_virtual_free(
            (void *)(uintptr_t)base_addr, (size_t)size_bytes, free_type, &vf_resolved);
        if (vf_resolved != 0) {
            int32_t vf_status = (vf_ok != 0) ? 0 : (int32_t)xmem_pe_get_last_error();
            xmem_pe_log_diag(vf_ok != 0 ? "[scarlett-xmem-placeholder]" : "[scarlett-xmem-fail]",
                             "xmem.dll",
                             "XMemVirtualFree",
                             (uint64_t)(uintptr_t)__builtin_return_address(0),
                             base_addr,
                             size_bytes,
                             (uint64_t)free_type,
                             (uint64_t)vf_ok,
                             vf_status);
            return (vf_ok != 0) ? SCARLETT_OK : SCARLETT_E_FAIL;
        }
    }

    scarlett_xmem_free_range_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.base_addr = base_addr;
    params.size_bytes = size_bytes;
    params.free_type = free_type;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_FREE_RANGE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    {
        static uint32_t g_vfree_seq = 0u;
        uint32_t seq = __atomic_add_fetch(&g_vfree_seq, 1u, __ATOMIC_RELAXED);
        if (size_bytes >= (UINT64_C(4) << 20) || seq <= 24u || (seq % 100u) == 0u ||
            scarlett_failed(params.out_status)) {
            xmem_pe_log_diag("[scarlett-xmem-free]",
                             "xmem.dll",
                             "XMemVirtualFree",
                             (uint64_t)(uintptr_t)__builtin_return_address(0),
                             base_addr,
                             size_bytes,
                             (uint64_t)free_type,
                             (uint64_t)seq,
                             params.out_status);
        }
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemMapPhysicalPages(
    uint64_t virtual_addr,
    uint64_t page_count,
    uint64_t page_size_bytes,
    const uint64_t *pfn_array,
    scarlett_mem_access_t access,
    scarlett_mem_cache_attr_t cache_attr) {
    if (pfn_array == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_map_pages_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.virtual_addr = virtual_addr;
    params.page_count = page_count;
    params.page_size_bytes = page_size_bytes;
    params.pfn_array_addr = (uint64_t)(uintptr_t)pfn_array;
    params.access = (uint32_t)access;
    params.cache_attr = (uint32_t)cache_attr;
    params.unmap_flag = 0u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_MAP_PHYS_PAGES, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemMapPhysicalPagesScatter(
    uint64_t base_virtual_addr,
    const uint64_t *byte_offsets,
    const uint64_t *pfn_array,
    uint64_t entry_count,
    uint64_t page_size_bytes,
    scarlett_mem_access_t access,
    scarlett_mem_cache_attr_t cache_attr,
    uint32_t scatter_flags) {
    if (pfn_array == NULL) {
        return SCARLETT_E_POINTER;
    }

    scarlett_xmem_scatter_map_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.base_virtual_addr = base_virtual_addr;
    params.offsets_array_addr = (uint64_t)(uintptr_t)byte_offsets;
    params.pfn_array_addr = (uint64_t)(uintptr_t)pfn_array;
    params.entry_count = entry_count;
    params.page_size_bytes = page_size_bytes;
    params.access = (uint32_t)access;
    params.cache_attr = (uint32_t)cache_attr;
    params.flags = scatter_flags;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_MAP_SCATTER, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUnmapPhysicalPages(
    uint64_t virtual_addr,
    uint64_t page_count,
    uint64_t page_size_bytes) {
    scarlett_xmem_map_pages_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.virtual_addr = virtual_addr;
    params.page_count = page_count;
    params.page_size_bytes = page_size_bytes;
    params.unmap_flag = 1u;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_UNMAP_PHYS_PAGES, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemVirtualQuery(
    uint64_t query_addr,
    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION *out_info) {
    if (out_info == NULL) {
        return SCARLETT_E_POINTER;
    }
    xmem_pe_zero(out_info, sizeof(*out_info));

    scarlett_xmem_virtual_query_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.query_addr = query_addr;

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

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemGetWorkingSetStatistics(
    XMEM_WORKING_SET_STATISTICS *out_stats) {
    if (out_stats == NULL) {
        return SCARLETT_E_POINTER;
    }
    xmem_pe_zero(out_stats, sizeof(*out_stats));

    scarlett_xmem_working_set_stats_params_t params;
    xmem_pe_zero(&params, sizeof(params));

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_WORKING_SET_STATS, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }

    out_stats->TotalBudgetBytes = params.out_total_budget_bytes;
    out_stats->CommittedBytes = params.out_committed_bytes;
    out_stats->ReservedBytes = params.out_reserved_bytes;
    out_stats->PhysicalAllocatedBytes = params.out_physical_allocated_bytes;
    out_stats->PhysicalMappedBytes = params.out_physical_mapped_bytes;
    out_stats->GraphicsCommittedBytes = params.out_graphics_committed_bytes;
    out_stats->TitleCommittedBytes = params.out_title_committed_bytes;
    out_stats->HeapAllocatedBytes = params.out_heap_allocated_bytes;
    out_stats->PeakCommittedBytes = params.out_peak_committed_bytes;
    out_stats->UmaDirtyFlushes = params.out_uma_dirty_flushes;
    out_stats->UmaDirtyPagesFlushed = params.out_uma_dirty_pages_flushed;
    out_stats->ActivePhysicalPages = params.out_active_physical_pages;
    out_stats->ActiveAliasMappings = params.out_active_alias_mappings;
    out_stats->ActiveUmaTier = params.out_active_uma_tier;
    return params.out_status;
}

SCARLETT_XMEM_API uint64_t SCARLETT_XMEM_CALL XMemMakeTag(
    uint8_t object_type,
    uint8_t allocator_id,
    uint8_t alignment_pow2,
    uint8_t page_size_code,
    uint8_t cache_attr,
    uint8_t zero_init,
    uint8_t graphics_hint) {
    return scarlett_xmem_make_tag_inline(
        object_type, allocator_id, alignment_pow2,
        page_size_code, cache_attr, zero_init, graphics_hint);
}

SCARLETT_XMEM_API void * SCARLETT_XMEM_CALL XMemAllocDefault(
    uint64_t size_bytes,
    uint64_t attributes_tag) {
    scarlett_xmem_heap_alloc_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.size_bytes = size_bytes;
    params.attributes_tag = attributes_tag;
    params.alignment_bytes = scarlett_xmem_tag_alignment(attributes_tag);

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_HEAP_ALLOC, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return NULL;
    }
    return (void *)(uintptr_t)params.out_ptr;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemFreeDefault(
    void *ptr,
    uint64_t attributes_tag) {
    scarlett_xmem_heap_free_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.ptr = (uint64_t)(uintptr_t)ptr;
    params.attributes_tag = attributes_tag;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_HEAP_FREE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    return params.out_status;
}

SCARLETT_XMEM_API uint64_t SCARLETT_XMEM_CALL XMemSizeDefault(
    const void *ptr,
    uint64_t attributes_tag) {
    scarlett_xmem_heap_free_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.ptr = (uint64_t)(uintptr_t)ptr;
    params.attributes_tag = attributes_tag;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_HEAP_SIZE, &params);
    if (scarlett_failed(rc) || scarlett_failed(params.out_status)) {
        return 0u;
    }
    return params.out_size_bytes;
}

SCARLETT_XMEM_API void * SCARLETT_XMEM_CALL XMemAlloc(
    uint64_t size_bytes,
    uint64_t attributes) {
    return XMemAllocDefault(size_bytes, attributes);
}

SCARLETT_XMEM_API void SCARLETT_XMEM_CALL XMemFree(
    void *ptr,
    uint64_t attributes) {
    (void)XMemFreeDefault(ptr, attributes);
}

SCARLETT_XMEM_API int32_t SCARLETT_XMEM_CALL XMemIsToolsMemAvailable(void) {
    return 0;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemTransferMemory(
    void *dst,
    const void *src,
    uint64_t size_bytes,
    uint32_t flags) {
    if (dst == NULL || src == NULL) {
        return SCARLETT_E_INVALIDARG;
    }
    (void)flags;
    xmem_pe_copy(dst, src, (size_t)size_bytes);
    return SCARLETT_OK;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaProbe(
    scarlett_xmem_uma_probe_action_t action,
    scarlett_uma_tier_t forced_tier,
    uint32_t simulated_caps,
    XMEM_UMA_TIER_INFO *out_info) {
    scarlett_xmem_uma_probe_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.action = (uint32_t)action;
    params.forced_tier = (uint32_t)forced_tier;
    params.cap_flags = simulated_caps;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_UMA_PROBE, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_info != NULL) {
        xmem_pe_zero(out_info, sizeof(*out_info));
        out_info->SelectedTier = params.out_selected_tier;
        out_info->WatchMode = params.out_watch_mode;
        out_info->DetectedCaps = params.out_detected_caps;
        out_info->UffdErrno = params.out_uffd_errno;
        out_info->WatchedRegionCount = params.out_watched_region_count;
        out_info->MinHostPtrAlignment = params.out_min_host_ptr_alignment;
        out_info->RebarHeapBytes = params.out_rebar_heap_bytes;
        out_info->TotalWriteFaults = params.out_total_write_faults;
        out_info->TotalSubmitFlushes = params.out_total_submit_flushes;
        out_info->TotalPresentFlushes = params.out_total_present_flushes;
        out_info->TotalMigratedHotRegions = params.out_total_migrated_hot_regions;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaExport(
    uint64_t virtual_addr,
    uint64_t size_bytes,
    scarlett_uma_tier_t preferred_tier,
    scarlett_uma_tier_t *out_active_tier,
    int32_t *out_dmabuf_fd,
    uint64_t *out_host_ptr) {
    scarlett_xmem_uma_export_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.virtual_addr = virtual_addr;
    params.size_bytes = size_bytes;
    params.preferred_tier = (uint32_t)preferred_tier;
    params.out_dmabuf_fd = -1;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_UMA_EXPORT, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_active_tier != NULL) {
        *out_active_tier = (scarlett_uma_tier_t)params.out_active_tier;
    }
    if (out_dmabuf_fd != NULL) {
        *out_dmabuf_fd = params.out_dmabuf_fd;
    }
    if (out_host_ptr != NULL) {
        *out_host_ptr = params.out_host_ptr;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaFlushBeforeSubmit(
    uint64_t command_list_id,
    uint64_t region_addr,
    uint64_t region_size,
    uint64_t *out_flushed_pages,
    uint64_t *out_flushed_bytes) {
    scarlett_xmem_flush_dirty_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.command_list_id = command_list_id;
    params.region_addr = region_addr;
    params.region_size = region_size;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_UFFD_FLUSH_DIRTY, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_flushed_pages != NULL) {
        *out_flushed_pages = params.out_flushed_pages;
    }
    if (out_flushed_bytes != NULL) {
        *out_flushed_bytes = params.out_flushed_bytes;
    }
    return params.out_status;
}

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaTrackHotResource(
    uint64_t virtual_addr,
    uint64_t size_bytes,
    scarlett_xmem_uma_hot_action_t action,
    uint32_t touch_weight,
    uint32_t *out_is_migrated,
    uint32_t *out_migration_count) {
    scarlett_xmem_uma_hot_params_t params;
    xmem_pe_zero(&params, sizeof(params));
    params.virtual_addr = virtual_addr;
    params.size_bytes = size_bytes;
    params.access_kind = (uint32_t)action;
    params.touch_weight = touch_weight;

    int32_t rc = xmem_pe_unix_call(SCARLETT_XMEM_UNIX_UMA_MIGRATE_HOT, &params);
    if (scarlett_failed(rc)) {
        return rc;
    }
    if (out_is_migrated != NULL) {
        *out_is_migrated = params.out_is_migrated;
    }
    if (out_migration_count != NULL) {
        *out_migration_count = params.out_migration_count;
    }
    return params.out_status;
}

#if defined(_WIN32)
uint32_t SCARLETT_XMEM_CALL DllMain(void *hinst, uint32_t reason, void *reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == 1u) { /* DLL_PROCESS_ATTACH */
        if (xmem_pe_init_unix_bridge() != 0) {
            return 0u;
        }
        if (scarlett_failed(XMemInitialize(SCARLETT_MODEL_ANACONDA, 0u))) {
            return 0u;
        }
    }
    return 1u;
}
#endif
