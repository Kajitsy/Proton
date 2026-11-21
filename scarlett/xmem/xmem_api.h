/*
 * Proton-Scarlett — Public API for xmem.dll, kernelx.dll & xmem_unix.so
 *
 * Clean-room freestanding C11 / C++20 header.
 *   - Under PE64 compilation (_WIN32): implemented in xmem_pe.c / kernelx_pe.c
 *     strictly via WINE_UNIX_CALL (zero libc / POSIX / libvulkan dependencies).
 *   - Under Linux host tests: the identical xmem_pe.c / kernelx_pe.c sources
 *     link via scarlett_xmem_set_unix_call_bridge() directly to xmem_unix.so.
 */

#ifndef SCARLETT_XMEM_API_H
#define SCARLETT_XMEM_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xmem_unix_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define SCARLETT_XMEM_API __declspec(dllexport)
#define SCARLETT_XMEM_CALL __stdcall
#else
#define SCARLETT_XMEM_API __attribute__((visibility("default")))
#define SCARLETT_XMEM_CALL
#endif

/* ============================================================================
 * Public High-Level Structs (Freestanding, Fixed-Width)
 * ============================================================================ */

typedef struct XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION_S {
    uint64_t BaseAddress;
    uint64_t AllocationBase;
    uint32_t AllocationProtect;
    uint32_t Region;          /* scarlett_mem_region_t */
    uint64_t RegionSize;
    uint32_t State;           /* XMEM_MEM_COMMIT / RESERVE / FREE */
    uint32_t Protect;
    uint32_t Type;            /* XMEM_MEM_PRIVATE / PHYSICAL */
    uint32_t CacheAttr;       /* scarlett_mem_cache_attr_t */
    uint64_t AllocationId;
} XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION;

typedef struct XMEM_WORKING_SET_STATISTICS_S {
    uint64_t TotalBudgetBytes;
    uint64_t CommittedBytes;
    uint64_t ReservedBytes;
    uint64_t PhysicalAllocatedBytes;
    uint64_t PhysicalMappedBytes;
    uint64_t GraphicsCommittedBytes;
    uint64_t TitleCommittedBytes;
    uint64_t HeapAllocatedBytes;
    uint64_t PeakCommittedBytes;
    uint64_t UmaDirtyFlushes;
    uint64_t UmaDirtyPagesFlushed;
    uint32_t ActivePhysicalPages;
    uint32_t ActiveAliasMappings;
    uint32_t ActiveUmaTier;
    uint32_t Reserved;
} XMEM_WORKING_SET_STATISTICS;

typedef struct XMEM_TITLE_MEMORY_STATUS_S {
    uint32_t dwLength;
    uint32_t ConsoleModel;    /* scarlett_console_model_t */
    uint64_t ullTotalMem;     /* Exact profile budget (13.5 / 8.5 / 40 GiB) */
    uint64_t ullAvailMem;
    uint64_t ullLegacyUsed;
    uint64_t ullLegacyPeak;
    uint64_t ullLegacyAvail;
    uint64_t ullLegacyLimit;
    uint64_t ullTitleUsed;
    uint64_t ullTitlePeak;
    uint64_t ullTitleAvail;
    uint64_t ullTitleLimit;
    uint64_t ullFastPoolBytes;
    uint64_t ullStandardPoolBytes;
} XMEM_TITLE_MEMORY_STATUS;

typedef struct XMEM_JOB_TITLE_MEMORY_STATUS_S {
    uint32_t dwLength;
    uint32_t ConsoleModel;
    uint64_t ullTotalMem;
    uint64_t ullAvailMem;
    uint64_t ullLegacyUsed;
    uint64_t ullLegacyPeak;
    uint64_t ullLegacyLimit;
    uint64_t ullTitleUsed;
    uint64_t ullTitlePeak;
    uint64_t ullTitleLimit;
    uint64_t ullFastPoolBytes;
    uint64_t ullStandardPoolBytes;
} XMEM_JOB_TITLE_MEMORY_STATUS;

typedef struct XMEM_UMA_TIER_INFO_S {
    uint32_t SelectedTier;          /* scarlett_uma_tier_t */
    uint32_t WatchMode;             /* scarlett_xmem_uma_watch_mode_t */
    uint32_t DetectedCaps;          /* XMEM_UMA_CAP_* bitmask */
    uint32_t UffdErrno;
    uint32_t WatchedRegionCount;
    uint32_t Reserved;
    uint64_t MinHostPtrAlignment;
    uint64_t RebarHeapBytes;
    uint64_t TotalWriteFaults;
    uint64_t TotalSubmitFlushes;
    uint64_t TotalPresentFlushes;
    uint64_t TotalMigratedHotRegions;
} XMEM_UMA_TIER_INFO;

typedef struct XMEM_PROCESSOR_TOPOLOGY_S {
    uint32_t HostTotalCpus;
    uint32_t HostPCoreCpus;
    uint32_t HostECoreCpus;
    uint32_t IsHybrid;
    uint32_t XboxCoreCount;
    uint32_t XboxThreadCount;
    uint32_t HostCpuForXboxThread[XMEM_XBOX_TITLE_THREADS];
} XMEM_PROCESSOR_TOPOLOGY;

typedef struct XMEM_SYNC_STATUS_S {
    uint32_t Backend;      /* scarlett_xmem_sync_backend_t */
    int32_t  NtsyncErrno;  /* 0 when /dev/ntsync active, else real errno */
    uint32_t PrevValue;
    uint32_t AuxValue;
} XMEM_SYNC_STATUS;

/* ============================================================================
 * Unixlib Bridge Entry & Direct Pre-Submit UMA Hook (xmem_unix.so)
 * ============================================================================ */

#if !defined(_WIN32)
/* Main WINE_UNIX_CALL dispatcher exported by xmem_unix.so */
SCARLETT_XMEM_API int32_t scarlett_xmem_unix_call(uint32_t opcode, void *params);

/* Allows host tests to bind or intercept PE -> unixlib calls */
SCARLETT_XMEM_API void scarlett_xmem_set_unix_call_bridge(scarlett_unix_call_fn_t fn);
SCARLETT_XMEM_API scarlett_unix_call_fn_t scarlett_xmem_get_unix_call_bridge(void);

/*
 * Direct C hook invoked by d3d12_xs BEFORE EVERY ExecuteCommandLists (NOT PresentX).
 * Flushes all dirty CPU-written pages from Tier C write-watched MEM_GRAPHICS
 * regions to GPU shadow / Vulkan staging and re-arms write protection.
 */
SCARLETT_XMEM_API scarlett_hresult_t scarlett_uma_flush_before_submit(
    uint64_t command_list_id,
    uint64_t region_addr,
    uint64_t region_size,
    uint64_t *out_flushed_pages,
    uint64_t *out_flushed_bytes);

/*
 * GPU Upload Callback invoked synchronously inside scarlett_uma_flush_before_submit
 * for every coalesced dirty page span (used by d3d12_xs and unit tests to verify
 * zero 1-frame lag delivery prior to ExecuteCommandLists).
 */
typedef void (*scarlett_uma_gpu_upload_cb_t)(
    uint64_t command_list_id,
    uint64_t region_base_va,
    uint64_t byte_offset,
    const void *cpu_data,
    uint64_t byte_length,
    void *user_data);

SCARLETT_XMEM_API void scarlett_uma_set_gpu_upload_callback(
    scarlett_uma_gpu_upload_cb_t cb,
    void *user_data);

/*
 * Reads bytes back from the GPU-side shadow copy of an exported MEM_GRAPHICS
 * region (Tier C shadow / Tier B migrated buffer / Tier A zero-copy mapping).
 */
SCARLETT_XMEM_API scarlett_hresult_t scarlett_uma_read_gpu_shadow(
    uint64_t region_base_va,
    uint64_t byte_offset,
    void *dst_buffer,
    uint64_t byte_length);

/* Installs a diagnostic stub sink callback for testing PE auto-stub traps */
SCARLETT_XMEM_API void scarlett_xmem_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data);
#endif /* !defined(_WIN32) */

/* ============================================================================
 * xmem.dll PE64 Exports (Strictly routed via WINE_UNIX_CALL)
 * ============================================================================ */

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemInitialize(
    scarlett_console_model_t model,
    uint32_t strict_mode);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemSetConsoleModel(
    scarlett_console_model_t model,
    uint32_t reset_allocations);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemAllocatePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    scarlett_mem_cache_attr_t cache_attr,
    uint32_t flags,
    uint64_t *out_pfn_array,
    uint64_t *out_alloc_id);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemFreePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    const uint64_t *pfn_array);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemVirtualAlloc(
    uint64_t requested_base,
    uint64_t size_bytes,
    uint32_t alloc_type,
    uint32_t protect,
    uint64_t *out_base);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemVirtualFree(
    uint64_t base_addr,
    uint64_t size_bytes,
    uint32_t free_type);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemMapPhysicalPages(
    uint64_t virtual_addr,
    uint64_t page_count,
    uint64_t page_size_bytes,
    const uint64_t *pfn_array,
    scarlett_mem_access_t access,
    scarlett_mem_cache_attr_t cache_attr);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemMapPhysicalPagesScatter(
    uint64_t base_virtual_addr,
    const uint64_t *byte_offsets,
    const uint64_t *pfn_array,
    uint64_t entry_count,
    uint64_t page_size_bytes,
    scarlett_mem_access_t access,
    scarlett_mem_cache_attr_t cache_attr,
    uint32_t scatter_flags);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUnmapPhysicalPages(
    uint64_t virtual_addr,
    uint64_t page_count,
    uint64_t page_size_bytes);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemVirtualQuery(
    uint64_t query_addr,
    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION *out_info);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemGetWorkingSetStatistics(
    XMEM_WORKING_SET_STATISTICS *out_stats);

SCARLETT_XMEM_API uint64_t SCARLETT_XMEM_CALL XMemMakeTag(
    uint8_t object_type,
    uint8_t allocator_id,
    uint8_t alignment_pow2,
    uint8_t page_size_code,
    uint8_t cache_attr,
    uint8_t zero_init,
    uint8_t graphics_hint);

SCARLETT_XMEM_API void * SCARLETT_XMEM_CALL XMemAllocDefault(
    uint64_t size_bytes,
    uint64_t attributes_tag);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemFreeDefault(
    void *ptr,
    uint64_t attributes_tag);

SCARLETT_XMEM_API uint64_t SCARLETT_XMEM_CALL XMemSizeDefault(
    const void *ptr,
    uint64_t attributes_tag);

/* UMA Bridge Control & Pre-Submit Flush PE64 Exports */
SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaProbe(
    scarlett_xmem_uma_probe_action_t action,
    scarlett_uma_tier_t forced_tier,
    uint32_t simulated_caps,
    XMEM_UMA_TIER_INFO *out_info);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaExport(
    uint64_t virtual_addr,
    uint64_t size_bytes,
    scarlett_uma_tier_t preferred_tier,
    scarlett_uma_tier_t *out_active_tier,
    int32_t *out_dmabuf_fd,
    uint64_t *out_host_ptr);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaFlushBeforeSubmit(
    uint64_t command_list_id,
    uint64_t region_addr,
    uint64_t region_size,
    uint64_t *out_flushed_pages,
    uint64_t *out_flushed_bytes);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XMemUmaTrackHotResource(
    uint64_t virtual_addr,
    uint64_t size_bytes,
    scarlett_xmem_uma_hot_action_t action,
    uint32_t touch_weight,
    uint32_t *out_is_migrated,
    uint32_t *out_migration_count);

/* ============================================================================
 * kernelx.dll PE64 Exports (Strictly routed via WINE_UNIX_CALL)
 * ============================================================================ */

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL TitleMemoryStatus(
    XMEM_TITLE_MEMORY_STATUS *out_status);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL JobTitleMemoryStatus(
    XMEM_JOB_TITLE_MEMORY_STATUS *out_status);

SCARLETT_XMEM_API void * SCARLETT_XMEM_CALL KernelXVirtualAlloc(
    void *address,
    uint64_t size_bytes,
    uint32_t alloc_type,
    uint32_t protect);

SCARLETT_XMEM_API uint32_t SCARLETT_XMEM_CALL KernelXVirtualFree(
    void *address,
    uint64_t size_bytes,
    uint32_t free_type);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXVirtualQuery(
    const void *address,
    XMEM_VIRTUAL_MEMORY_BASIC_INFORMATION *out_info);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL AllocateTitlePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    uint32_t protect,
    uint64_t *out_pfn_array);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL FreeTitlePhysicalPages(
    uint64_t page_count,
    uint64_t page_size_bytes,
    const uint64_t *pfn_array);

/* Legacy Xbox One ESRAM API: explicitly routed to diagnostic auto-stub thunk */
SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL MapTitleEsramPages(
    void *virtual_addr,
    uint64_t page_count,
    uint32_t flags,
    const uint32_t *esram_page_array);

/* Threading: 7-core / 14-thread P-core Topology & Time-Sensitive API */
SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadGetProcessorTopology(
    XMEM_PROCESSOR_TOPOLOGY *out_topology);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadSetXboxCoreAffinity(
    uint32_t xbox_thread_mask,
    uint32_t *out_applied);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadSetTimeSensitive(
    uint32_t is_time_sensitive,
    uint32_t *out_previous_state,
    uint32_t *out_os_priority_applied);

SCARLETT_XMEM_API uint32_t SCARLETT_XMEM_CALL XThreadIsTimeSensitive(void);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL XThreadAssertNotTimeSensitive(void);

/* KernelX Synchronization Primitives (/dev/ntsync + Diagnosable Futex/Eventfd Fallback) */
SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCreateEvent(
    uint32_t manual_reset,
    uint32_t initial_state,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXSetEvent(
    uint64_t handle,
    uint32_t *out_prev_state);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXResetEvent(
    uint64_t handle,
    uint32_t *out_prev_state);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXPulseEvent(
    uint64_t handle,
    uint32_t *out_prev_state);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCreateSemaphore(
    uint32_t initial_count,
    uint32_t max_count,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXReleaseSemaphore(
    uint64_t handle,
    uint32_t release_count,
    uint32_t *out_prev_count);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCreateMutex(
    uint32_t initial_owner,
    uint32_t flags,
    uint64_t *out_handle,
    XMEM_SYNC_STATUS *out_sync_status);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXReleaseMutex(
    uint64_t handle,
    uint32_t *out_prev_count);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXWaitForMultipleObjects(
    uint32_t handle_count,
    const uint64_t *handles,
    uint32_t wait_all,
    uint64_t timeout_ns,
    uint32_t *out_signaled_index,
    uint32_t *out_timed_out);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXReadSyncState(
    uint64_t handle,
    XMEM_SYNC_STATUS *out_sync_status);

SCARLETT_XMEM_API scarlett_hresult_t SCARLETT_XMEM_CALL KernelXCloseSyncHandle(
    uint64_t handle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_XMEM_API_H */
