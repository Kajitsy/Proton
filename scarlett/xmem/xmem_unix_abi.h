/*
 * Proton-Scarlett — XMem / KernelX / xmem_unix.so Freestanding ABI Contract
 *
 * Strictly freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLLs: xmem.dll, kernelx.dll (x86_64-windows, Microsoft x64 ABI)
 *   - Native unixlib: xmem_unix.so (x86_64-unix, System V AMD64 ABI)
 *   - Host C++20 verification tests
 *
 * ZERO dependencies on <windows.h>, libc, POSIX, or <vulkan/vulkan.h>.
 * Every cross-boundary struct uses fixed-width integer types only (no long,
 * size_t, or bool) with explicit padding and compile-time size assertions.
 */

#ifndef SCARLETT_XMEM_UNIX_ABI_H
#define SCARLETT_XMEM_UNIX_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Error Code Aliases & NTSTATUS Equivalents
 * ============================================================================ */

#ifndef SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT
#define SCARLETT_ERR_XMEM_CACHE_ATTR_CONFLICT SCARLETT_ERR_XMEM_CACHE_CONFLICT
#endif

#ifndef SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN
#define SCARLETT_ERR_XMEM_CROSS_ALLOC_SPAN SCARLETT_ERR_XMEM_CROSS_ALLOCATION
#endif

#define SCARLETT_NTSTATUS_INVALID_PARAMETER UINT32_C(0xC000000D)

/* ============================================================================
 * XMem / KernelX Memory Allocation & Page Protection Constants
 * ============================================================================ */

#define XMEM_PAGE_SIZE_4KB            SCARLETT_PAGE_SIZE_4KB
#define XMEM_PAGE_SIZE_64KB           SCARLETT_PAGE_SIZE_64KB
#define XMEM_PAGE_SIZE_2MB            SCARLETT_PAGE_SIZE_2MB
#define XMEM_PAGE_SIZE_4MB            SCARLETT_PAGE_SIZE_4MB
#define XMEM_2MB_GROUP_PAGE_COUNT     UINT64_C(32) /* 32 * 64 KiB = 2 MiB */
#define XMEM_DEFAULT_HEAP_ALIGNMENT   UINT32_C(16) /* XMemAllocDefault >= 16 bytes */

/* Allocation Type Flags (compatible with Win32 / Xbox KernelX VirtualAlloc) */
#define XMEM_MEM_COMMIT               UINT32_C(0x00001000)
#define XMEM_MEM_RESERVE              UINT32_C(0x00002000)
#define XMEM_MEM_DECOMMIT             UINT32_C(0x00004000)
#define XMEM_MEM_RELEASE              UINT32_C(0x00008000)
#define XMEM_MEM_FREE                 UINT32_C(0x00010000)
#define XMEM_MEM_PRIVATE              UINT32_C(0x00020000)
#define XMEM_MEM_PHYSICAL             UINT32_C(0x00400000)
#define XMEM_MEM_GRAPHICS             UINT32_C(0x10000000)
#define XMEM_MEM_LARGE_PAGES          UINT32_C(0x20000000)
#define XMEM_MEM_TITLE                UINT32_C(0x40000000)
#define XMEM_MEM_4MB_PAGES            UINT32_C(0x80000000)

/* Page Protection & Cache Attribute Flags */
#define XMEM_PAGE_NOACCESS            UINT32_C(0x00000001)
#define XMEM_PAGE_READONLY            UINT32_C(0x00000002)
#define XMEM_PAGE_READWRITE           UINT32_C(0x00000004)
#define XMEM_PAGE_NOCACHE             UINT32_C(0x00000200)
#define XMEM_PAGE_WRITECOMBINE        UINT32_C(0x00000400)
#define XMEM_PAGE_GPU_COHERENT        UINT32_C(0x80000000)

/* Physical Page Allocation Flags */
#define XMEM_PHYS_FLAG_NONE           UINT32_C(0x00000000)
#define XMEM_PHYS_FLAG_GRAPHICS       UINT32_C(0x00000001)
#define XMEM_PHYS_FLAG_TITLE          UINT32_C(0x00000002)
#define XMEM_PHYS_FLAG_HUGEPAGE_HINT  UINT32_C(0x00000004)
#define XMEM_PHYS_FLAG_DMABUF_HINT    UINT32_C(0x00000008)

/* Scatter Mapping Flags */
#define XMEM_SCATTER_FLAG_NONE        UINT32_C(0x00000000)
#define XMEM_SCATTER_FLAG_2MB_GROUP   UINT32_C(0x00000001)

/* Console Fast/Standard Memory Pool Breakdown (bytes) */
#define SCARLETT_BUDGET_ANACONDA_STD_BYTES  (SCARLETT_BUDGET_ANACONDA_BYTES - SCARLETT_BUDGET_ANACONDA_FAST_BYTES)
#define SCARLETT_BUDGET_LOCKHART_STD_BYTES  (SCARLETT_BUDGET_LOCKHART_BYTES - SCARLETT_BUDGET_LOCKHART_FAST_BYTES)
#define SCARLETT_BUDGET_DEVKIT_FAST_BYTES   UINT64_C(32212254720) /* 30.0 GiB */
#define SCARLETT_BUDGET_DEVKIT_STD_BYTES    (SCARLETT_BUDGET_DEVKIT_BYTES - SCARLETT_BUDGET_DEVKIT_FAST_BYTES)

/* ============================================================================
 * XMemMakeTag 64-bit Allocation Attribute Encoding
 *
 * Bits  0..7  : object_type     (8 bits)
 * Bits  8..15 : allocator_id    (8 bits)
 * Bits 16..19 : alignment_pow2  (4 bits: 0 => default 16B, else 1 << N, clamped >= 16B)
 * Bits 20..23 : page_size_code  (4 bits: 0=4KB, 1=64KB, 2=2MB, 3=4MB)
 * Bits 24..27 : cache_attr      (4 bits: scarlett_mem_cache_attr_t)
 * Bit  28     : zero_initialize (1 bit)
 * Bit  29     : graphics_hint   (1 bit)
 * Bits 30..63 : user_tag_ext    (34 bits)
 * ============================================================================ */

#define XMEM_TAG_PAGE_4KB   UINT8_C(0)
#define XMEM_TAG_PAGE_64KB  UINT8_C(1)
#define XMEM_TAG_PAGE_2MB   UINT8_C(2)
#define XMEM_TAG_PAGE_4MB   UINT8_C(3)

static inline uint64_t scarlett_xmem_make_tag_inline(
    uint8_t object_type,
    uint8_t allocator_id,
    uint8_t alignment_pow2,
    uint8_t page_size_code,
    uint8_t cache_attr,
    uint8_t zero_init,
    uint8_t graphics_hint) {
    return ((uint64_t)object_type) |
           ((uint64_t)allocator_id << 8) |
           (((uint64_t)alignment_pow2 & UINT64_C(0xF)) << 16) |
           (((uint64_t)page_size_code & UINT64_C(0xF)) << 20) |
           (((uint64_t)cache_attr & UINT64_C(0xF)) << 24) |
           (((uint64_t)(zero_init ? 1u : 0u)) << 28) |
           (((uint64_t)(graphics_hint ? 1u : 0u)) << 29);
}

static inline uint8_t scarlett_xmem_tag_object_type(uint64_t tag) {
    return (uint8_t)(tag & UINT64_C(0xFF));
}

static inline uint8_t scarlett_xmem_tag_allocator_id(uint64_t tag) {
    return (uint8_t)((tag >> 8) & UINT64_C(0xFF));
}

static inline uint32_t scarlett_xmem_tag_alignment(uint64_t tag) {
    uint32_t pow2 = (uint32_t)((tag >> 16) & UINT64_C(0xF));
    if (pow2 < 4u) {
        return XMEM_DEFAULT_HEAP_ALIGNMENT; /* Always >= 16 bytes */
    }
    return UINT32_C(1) << pow2;
}

static inline uint8_t scarlett_xmem_tag_cache_attr(uint64_t tag) {
    return (uint8_t)((tag >> 24) & UINT64_C(0xF));
}

static inline bool scarlett_xmem_tag_zero_init(uint64_t tag) {
    return ((tag >> 28) & UINT64_C(1)) != 0u;
}

static inline bool scarlett_xmem_tag_graphics_hint(uint64_t tag) {
    return ((tag >> 29) & UINT64_C(1)) != 0u;
}

/* ============================================================================
 * UMA Bridge Capability Flags & Write-Watch Modes
 * ============================================================================ */

#define XMEM_UMA_CAP_NONE               UINT32_C(0x00000000)
#define XMEM_UMA_CAP_REBAR              UINT32_C(0x00000001) /* ReBAR / large host-visible VRAM */
#define XMEM_UMA_CAP_APU                UINT32_C(0x00000002) /* Integrated unified GPU */
#define XMEM_UMA_CAP_VK_EXT_MEMORY_FD   UINT32_C(0x00000004) /* VK_KHR_external_memory_fd */
#define XMEM_UMA_CAP_VK_EXT_DMABUF      UINT32_C(0x00000008) /* VK_EXT_external_memory_dma_buf */
#define XMEM_UMA_CAP_VK_EXT_HOST_MEM    UINT32_C(0x00000010) /* VK_EXT_external_memory_host */
#define XMEM_UMA_CAP_DMA_HEAP           UINT32_C(0x00000020) /* /dev/dma_heap/system accessible */
#define XMEM_UMA_CAP_MEMFD_EXPORT       UINT32_C(0x00000040) /* memfd fd exportable */
#define XMEM_UMA_CAP_UFFD_WP            UINT32_C(0x00000080) /* userfaultfd UFFDIO_WRITEPROTECT */
#define XMEM_UMA_CAP_MPROTECT_FALLBACK  UINT32_C(0x00000100) /* SIGSEGV + mprotect write-watch */

typedef enum scarlett_xmem_uma_watch_mode_e {
    XMEM_UMA_WATCH_NONE             = 0,
    XMEM_UMA_WATCH_UFFD_WP          = 1, /* Kernel userfaultfd UFFDIO_WRITEPROTECT */
    XMEM_UMA_WATCH_MPROTECT_SIGSEGV = 2  /* Fallback mprotect(PROT_READ) + SIGSEGV */
} scarlett_xmem_uma_watch_mode_t;

typedef enum scarlett_xmem_uma_probe_action_e {
    XMEM_UMA_PROBE_QUERY         = 0,
    XMEM_UMA_PROBE_SET_SIM_CAPS  = 1, /* Inject capability flags (testing/overrides) */
    XMEM_UMA_PROBE_FORCE_TIER    = 2, /* Force Tier A/B/C selection */
    XMEM_UMA_PROBE_RESET_HOST    = 3  /* Re-probe real host hardware/kernel */
} scarlett_xmem_uma_probe_action_t;

typedef enum scarlett_xmem_uma_hot_action_e {
    XMEM_UMA_HOT_QUERY           = 0,
    XMEM_UMA_HOT_RECORD_GPU_READ = 1,
    XMEM_UMA_HOT_RECORD_CPU_WRITE = 2,
    XMEM_UMA_HOT_FORCE_MIGRATE   = 3
} scarlett_xmem_uma_hot_action_t;

/* ============================================================================
 * Thread Topology, Time-Sensitive & Sync Primitive Constants
 * ============================================================================ */

#define XMEM_XBOX_TITLE_CORES         UINT32_C(7)
#define XMEM_XBOX_TITLE_THREADS       UINT32_C(14)
#define XMEM_SYNC_MAX_WAIT_HANDLES    UINT32_C(64)
#define XMEM_SYNC_TIMEOUT_INFINITE    UINT64_C(0xFFFFFFFFFFFFFFFF)
#define XMEM_SYNC_WAIT_TIMEOUT_INDEX  UINT32_C(0xFFFFFFFF)

typedef enum scarlett_xmem_sync_type_e {
    XMEM_SYNC_TYPE_EVENT     = 1,
    XMEM_SYNC_TYPE_SEMAPHORE = 2,
    XMEM_SYNC_TYPE_MUTEX     = 3
} scarlett_xmem_sync_type_t;

typedef enum scarlett_xmem_sync_backend_e {
    XMEM_SYNC_BACKEND_UNINITIALIZED  = 0,
    XMEM_SYNC_BACKEND_NTSYNC_KERNEL  = 1, /* Native Linux /dev/ntsync ioctls */
    XMEM_SYNC_BACKEND_FUTEX_FALLBACK = 2  /* Diagnosable futex + eventfd fallback */
} scarlett_xmem_sync_backend_t;

typedef enum scarlett_xmem_sync_op_e {
    XMEM_SYNC_OP_EVENT_SET    = 1,
    XMEM_SYNC_OP_EVENT_RESET  = 2,
    XMEM_SYNC_OP_EVENT_PULSE  = 3,
    XMEM_SYNC_OP_SEM_RELEASE  = 4,
    XMEM_SYNC_OP_MUTEX_UNLOCK = 5,
    XMEM_SYNC_OP_READ_STATE   = 6,
    XMEM_SYNC_OP_CLOSE        = 7
} scarlett_xmem_sync_op_t;

#define XMEM_SYNC_CREATE_FLAG_NONE           UINT32_C(0)
#define XMEM_SYNC_CREATE_FLAG_FORCE_FALLBACK UINT32_C(1)

/* ============================================================================
 * Extended xmem_unix.so Opcodes (0..8 match scarlett_unix_call.h verbatim)
 * ============================================================================ */

typedef enum scarlett_xmem_unix_ext_op_e {
    SCARLETT_XMEM_UNIX_FREE_RANGE            = 9,
    SCARLETT_XMEM_UNIX_VIRTUAL_QUERY         = 10,
    SCARLETT_XMEM_UNIX_WORKING_SET_STATS     = 11,
    SCARLETT_XMEM_UNIX_HEAP_ALLOC            = 12,
    SCARLETT_XMEM_UNIX_HEAP_FREE             = 13,
    SCARLETT_XMEM_UNIX_HEAP_SIZE             = 14,
    SCARLETT_XMEM_UNIX_TITLE_MEM_STATUS      = 15,
    SCARLETT_XMEM_UNIX_UMA_PROBE             = 16,
    SCARLETT_XMEM_UNIX_UMA_MIGRATE_HOT       = 17,
    SCARLETT_XMEM_UNIX_THREAD_TOPOLOGY       = 18,
    SCARLETT_XMEM_UNIX_THREAD_TIME_SENSITIVE = 19,
    SCARLETT_XMEM_UNIX_SYNC_CREATE           = 20,
    SCARLETT_XMEM_UNIX_SYNC_OP               = 21,
    SCARLETT_XMEM_UNIX_SYNC_WAIT             = 22,
    SCARLETT_XMEM_UNIX_MAP_SCATTER           = 23,
    SCARLETT_XMEM_UNIX_SET_MODEL             = 24,
    SCARLETT_XMEM_UNIX_REPORT_STUB           = 25,
    SCARLETT_XMEM_UNIX_EXT_OP_COUNT          = 26
} scarlett_xmem_unix_ext_op_t;

/* ============================================================================
 * Extended WINE_UNIX_CALL Parameter Payloads (Fixed-Width ABI)
 * ============================================================================ */

typedef struct scarlett_xmem_free_range_params_s {
    uint64_t base_addr;
    uint64_t size_bytes;
    uint32_t free_type; /* XMEM_MEM_DECOMMIT or XMEM_MEM_RELEASE */
    scarlett_hresult_t out_status;
} scarlett_xmem_free_range_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_free_range_params_t) == 24,
                       "scarlett_xmem_free_range_params_t must be 24 bytes");

typedef struct scarlett_xmem_virtual_query_params_s {
    uint64_t query_addr;
    uint64_t out_base_address;
    uint64_t out_allocation_base;
    uint64_t out_region_size;
    uint64_t out_alloc_id;
    uint32_t out_allocation_protect;
    uint32_t out_state;      /* XMEM_MEM_COMMIT / RESERVE / FREE */
    uint32_t out_protect;
    uint32_t out_type;       /* XMEM_MEM_PRIVATE / PHYSICAL */
    uint32_t out_region;     /* scarlett_mem_region_t */
    uint32_t out_cache_attr; /* scarlett_mem_cache_attr_t */
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_virtual_query_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_virtual_query_params_t) == 72,
                       "scarlett_xmem_virtual_query_params_t must be 72 bytes");

typedef struct scarlett_xmem_working_set_stats_params_s {
    uint64_t out_total_budget_bytes;
    uint64_t out_committed_bytes;
    uint64_t out_reserved_bytes;
    uint64_t out_physical_allocated_bytes;
    uint64_t out_physical_mapped_bytes;
    uint64_t out_graphics_committed_bytes;
    uint64_t out_title_committed_bytes;
    uint64_t out_heap_allocated_bytes;
    uint64_t out_peak_committed_bytes;
    uint64_t out_uma_dirty_flushes;
    uint64_t out_uma_dirty_pages_flushed;
    uint32_t out_active_physical_pages;
    uint32_t out_active_alias_mappings;
    uint32_t out_active_uma_tier;
    scarlett_hresult_t out_status;
} scarlett_xmem_working_set_stats_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_working_set_stats_params_t) == 104,
                       "scarlett_xmem_working_set_stats_params_t must be 104 bytes");

typedef struct scarlett_xmem_heap_alloc_params_s {
    uint64_t size_bytes;
    uint64_t attributes_tag;
    uint64_t out_ptr;
    uint64_t out_actual_size;
    uint32_t alignment_bytes;
    scarlett_hresult_t out_status;
} scarlett_xmem_heap_alloc_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_heap_alloc_params_t) == 40,
                       "scarlett_xmem_heap_alloc_params_t must be 40 bytes");

typedef struct scarlett_xmem_heap_free_params_s {
    uint64_t ptr;
    uint64_t attributes_tag;
    uint64_t out_size_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_heap_free_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_heap_free_params_t) == 32,
                       "scarlett_xmem_heap_free_params_t must be 32 bytes");

typedef struct scarlett_xmem_title_mem_status_params_s {
    uint32_t is_job_query;
    uint32_t out_console_model;
    uint64_t out_total_mem;
    uint64_t out_avail_mem;
    uint64_t out_legacy_used;
    uint64_t out_legacy_peak;
    uint64_t out_legacy_avail;
    uint64_t out_legacy_limit;
    uint64_t out_title_used;
    uint64_t out_title_peak;
    uint64_t out_title_avail;
    uint64_t out_title_limit;
    uint64_t out_fast_pool_bytes;
    uint64_t out_standard_pool_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_title_mem_status_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_title_mem_status_params_t) == 112,
                       "scarlett_xmem_title_mem_status_params_t must be 112 bytes");

typedef struct scarlett_xmem_uma_probe_params_s {
    uint32_t action;          /* scarlett_xmem_uma_probe_action_t */
    uint32_t forced_tier;     /* scarlett_uma_tier_t */
    uint32_t cap_flags;       /* XMEM_UMA_CAP_* bitmask */
    uint32_t out_selected_tier;
    uint32_t out_watch_mode;  /* scarlett_xmem_uma_watch_mode_t */
    uint32_t out_detected_caps;
    uint32_t out_uffd_errno;
    uint32_t out_watched_region_count;
    uint64_t out_min_host_ptr_alignment;
    uint64_t out_rebar_heap_bytes;
    uint64_t out_total_write_faults;
    uint64_t out_total_submit_flushes;
    uint64_t out_total_present_flushes;
    uint64_t out_total_migrated_hot_regions;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_uma_probe_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_uma_probe_params_t) == 88,
                       "scarlett_xmem_uma_probe_params_t must be 88 bytes");

typedef struct scarlett_xmem_uma_hot_params_s {
    uint64_t virtual_addr;
    uint64_t size_bytes;
    uint32_t access_kind; /* scarlett_xmem_uma_hot_action_t */
    uint32_t touch_weight;
    uint32_t out_is_migrated;
    uint32_t out_migration_count;
    uint64_t out_gpu_reads;
    uint64_t out_cpu_writes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_uma_hot_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_uma_hot_params_t) == 56,
                       "scarlett_xmem_uma_hot_params_t must be 56 bytes");

typedef struct scarlett_xmem_thread_topology_params_s {
    uint32_t action; /* 0 = query, 1 = apply xbox_thread_mask to current thread */
    uint32_t xbox_thread_mask;
    uint32_t out_host_total_cpus;
    uint32_t out_host_pcore_cpus;
    uint32_t out_host_ecore_cpus;
    uint32_t out_is_hybrid;
    uint32_t out_xbox_core_count;
    uint32_t out_xbox_thread_count;
    uint32_t out_host_cpu_map[XMEM_XBOX_TITLE_THREADS];
    uint32_t out_affinity_applied;
    scarlett_hresult_t out_status;
} scarlett_xmem_thread_topology_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_thread_topology_params_t) == 96,
                       "scarlett_xmem_thread_topology_params_t must be 96 bytes");

typedef struct scarlett_xmem_thread_ts_params_s {
    uint32_t action; /* 0 = query, 1 = set, 2 = assert_not_time_sensitive */
    uint32_t is_time_sensitive;
    uint32_t out_previous_state;
    uint32_t out_current_state;
    uint32_t out_os_priority_applied;
    uint32_t out_assertion_trips;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_thread_ts_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_thread_ts_params_t) == 32,
                       "scarlett_xmem_thread_ts_params_t must be 32 bytes");

typedef struct scarlett_xmem_sync_create_params_s {
    uint32_t sync_type; /* scarlett_xmem_sync_type_t */
    uint32_t arg0;
    uint32_t arg1;
    uint32_t flags;
    uint64_t out_handle;
    uint32_t out_backend; /* scarlett_xmem_sync_backend_t */
    int32_t  out_ntsync_errno;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_sync_create_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_sync_create_params_t) == 40,
                       "scarlett_xmem_sync_create_params_t must be 40 bytes");

typedef struct scarlett_xmem_sync_op_params_s {
    uint64_t handle;
    uint32_t op; /* scarlett_xmem_sync_op_t */
    uint32_t arg0;
    uint32_t out_prev_val;
    uint32_t out_aux_val;
    uint32_t out_backend;
    scarlett_hresult_t out_status;
} scarlett_xmem_sync_op_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_sync_op_params_t) == 32,
                       "scarlett_xmem_sync_op_params_t must be 32 bytes");

typedef struct scarlett_xmem_sync_wait_params_s {
    uint64_t handles_addr; /* Guest const uint64_t[handle_count] */
    uint64_t timeout_ns;   /* 0 = poll, XMEM_SYNC_TIMEOUT_INFINITE = infinite */
    uint32_t handle_count; /* 1..XMEM_SYNC_MAX_WAIT_HANDLES */
    uint32_t wait_all;     /* 0 = WaitAny, 1 = WaitAll */
    uint32_t out_signaled_index;
    uint32_t out_timed_out;
    uint32_t out_backend;
    scarlett_hresult_t out_status;
} scarlett_xmem_sync_wait_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_sync_wait_params_t) == 40,
                       "scarlett_xmem_sync_wait_params_t must be 40 bytes");

typedef struct scarlett_xmem_scatter_map_params_s {
    uint64_t base_virtual_addr;
    uint64_t offsets_array_addr; /* Optional byte offsets, or 0 for contiguous */
    uint64_t pfn_array_addr;     /* Guest const uint64_t[entry_count] */
    uint64_t entry_count;
    uint64_t page_size_bytes;
    uint32_t access;             /* scarlett_mem_access_t */
    uint32_t cache_attr;         /* scarlett_mem_cache_attr_t */
    uint32_t flags;              /* XMEM_SCATTER_FLAG_* */
    scarlett_hresult_t out_status;
} scarlett_xmem_scatter_map_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_scatter_map_params_t) == 56,
                       "scarlett_xmem_scatter_map_params_t must be 56 bytes");

typedef struct scarlett_xmem_set_model_params_s {
    uint32_t console_model;     /* scarlett_console_model_t */
    uint32_t reset_allocations; /* 0 = keep state, 1 = clean reset */
    uint32_t strict_mode;       /* 0, 1, or 0xFFFFFFFF = unchanged */
    scarlett_hresult_t out_status;
} scarlett_xmem_set_model_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_set_model_params_t) == 16,
                       "scarlett_xmem_set_model_params_t must be 16 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_XMEM_UNIX_ABI_H */
