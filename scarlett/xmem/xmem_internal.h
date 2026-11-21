/*
 * Proton-Scarlett — Internal C++20 Header for xmem_unix.so
 *
 * Linux-native unixlib implementation shared across:
 *   - xmem_phys_virt.cpp    (64KB/2MB physical pool, virtual ranges, aliasing, budgets)
 *   - xmem_uma_bridge.cpp   (Tier A/B/C UMA bridge, UFFD / mprotect write-watch, pre-submit flush)
 *   - xmem_thread_sync.cpp  (7c/14t P-core topology, XThread*TimeSensitive, /dev/ntsync + futex/eventfd)
 *   - xmem_unix_dispatch.cpp (WINE_UNIX_CALL dispatcher & exported C hooks)
 */

#ifndef SCARLETT_XMEM_INTERNAL_H
#define SCARLETT_XMEM_INTERNAL_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "xmem_api.h"
#include "xmem_unix_abi.h"

namespace scarlett::xmem::unixlib {

struct BackingRangeInfo {
    int backing_fd = -1;
    uint64_t fd_offset = 0;
    uint64_t mapped_size = 0;
    scarlett_mem_region_t region = SCARLETT_MEM_REGION_UNKNOWN;
    scarlett_mem_cache_attr_t cache_attr = SCARLETT_MEM_CACHE_WRITEBACK;
    bool is_dmabuf = false;
    bool is_contiguous_fd = false;
};

/* ============================================================================
 * Physical & Virtual Memory Subsystem (xmem_phys_virt.cpp)
 * ============================================================================ */

class MemoryManager {
public:
    static MemoryManager &instance();

    scarlett_hresult_t initialize(scarlett_console_model_t model, uint32_t strict_mode);
    scarlett_hresult_t set_model(scarlett_console_model_t model,
                                 bool reset_allocations,
                                 uint32_t strict_mode);
    void reset_all();

    scarlett_hresult_t reserve_or_commit_range(scarlett_xmem_reserve_params_t &params);
    scarlett_hresult_t free_range(scarlett_xmem_free_range_params_t &params);
    scarlett_hresult_t alloc_phys_pages(scarlett_xmem_alloc_pages_params_t &params);
    scarlett_hresult_t free_phys_pages(scarlett_xmem_alloc_pages_params_t &params);
    scarlett_hresult_t map_phys_pages(scarlett_xmem_map_pages_params_t &params);
    scarlett_hresult_t unmap_phys_pages(scarlett_xmem_map_pages_params_t &params);
    scarlett_hresult_t map_phys_pages_scatter(scarlett_xmem_scatter_map_params_t &params);
    scarlett_hresult_t virtual_query(scarlett_xmem_virtual_query_params_t &params);
    scarlett_hresult_t query_budget(scarlett_xmem_query_budget_params_t &params);
    scarlett_hresult_t working_set_stats(scarlett_xmem_working_set_stats_params_t &params);
    scarlett_hresult_t title_memory_status(scarlett_xmem_title_mem_status_params_t &params);

    scarlett_hresult_t heap_alloc(scarlett_xmem_heap_alloc_params_t &params);
    scarlett_hresult_t heap_free(scarlett_xmem_heap_free_params_t &params);
    scarlett_hresult_t heap_size(scarlett_xmem_heap_free_params_t &params);

    /* Used by UmaBridge to validate MEM_GRAPHICS mappings and inspect backing fds */
    scarlett_hresult_t inspect_mapped_range(uint64_t va,
                                            uint64_t size_bytes,
                                            BackingRangeInfo &out_info);

    scarlett_console_model_t console_model() const noexcept;
    uint32_t strict_mode() const noexcept;

private:
    MemoryManager();
    ~MemoryManager();

    struct PhysicalAllocation {
        uint64_t alloc_id = 0;
        int backing_fd = -1;
        uint64_t page_size_bytes = 0;
        uint64_t total_bytes = 0;
        uint64_t remaining_pages = 0;
        scarlett_mem_cache_attr_t cache_attr = SCARLETT_MEM_CACHE_WRITEBACK;
        scarlett_mem_region_t accounting_region = SCARLETT_MEM_REGION_TITLE;
        bool is_dmabuf = false;
        bool is_hugetlb = false;
    };

    struct PhysicalPage {
        uint64_t pfn = 0;
        uint64_t alloc_id = 0;
        uint64_t page_size_bytes = 0;
        uint64_t fd_offset = 0;
        int backing_fd = -1;
        scarlett_mem_cache_attr_t cache_attr = SCARLETT_MEM_CACHE_WRITEBACK;
        uint32_t active_map_count = 0;
        bool is_dmabuf = false;
    };

    struct MappedSpan {
        uint64_t va = 0;
        uint64_t size_bytes = 0;
        uint64_t pfn = 0;           /* 0 for direct VirtualAlloc MEM_COMMIT */
        uint64_t phys_alloc_id = 0; /* Owning physical allocation ID (never 0) */
        int backing_fd = -1;
        uint64_t fd_offset = 0;
        uint32_t protect = 0;
        scarlett_mem_access_t access = SCARLETT_MEM_ACCESS_NOACCESS;
        scarlett_mem_cache_attr_t cache_attr = SCARLETT_MEM_CACHE_WRITEBACK;
        bool is_external_pfn = false;
        bool is_dmabuf = false;
    };

    struct VirtualReservation {
        uint64_t res_id = 0;
        uint64_t base_va = 0;
        uint64_t size_bytes = 0;
        uint64_t granularity = XMEM_PAGE_SIZE_64KB;
        scarlett_mem_region_t region = SCARLETT_MEM_REGION_UNKNOWN;
        uint32_t alloc_protect = XMEM_PAGE_NOACCESS;
        uint32_t type = XMEM_MEM_PRIVATE;
        /* Direct-commit backing fd owned by this reservation (if VirtualAlloc MEM_COMMIT) */
        int owned_commit_fd = -1;
        uint64_t owned_commit_alloc_id = 0;
        /* Ordered non-overlapping mapped spans within [base_va, base_va + size_bytes) */
        std::vector<MappedSpan> mapped_spans;
    };

    struct HeapAllocation {
        uint64_t base_va = 0;
        uint64_t raw_size = 0;
        uint64_t requested_size = 0;
        uint64_t tag = 0;
        uint32_t alignment = XMEM_DEFAULT_HEAP_ALIGNMENT;
    };

    uint64_t budget_bytes_locked() const noexcept;
    void model_pools_locked(uint64_t &fast_bytes, uint64_t &std_bytes) const noexcept;
    void record_peak_locked() noexcept;
    VirtualReservation *find_reservation_containing_locked(uint64_t addr);
    int create_backing_fd_locked(uint64_t total_bytes,
                                 uint64_t page_size_bytes,
                                 uint32_t flags,
                                 bool &out_is_dmabuf,
                                 bool &out_is_hugetlb);
    scarlett_hresult_t reserve_va_locked(uint64_t requested_base,
                                         uint64_t size_bytes,
                                         uint64_t align_bytes,
                                         scarlett_mem_region_t region,
                                         uint64_t &out_base);
    scarlett_hresult_t map_entries_locked(uint64_t base_va,
                                          const uint64_t *byte_offsets,
                                          const uint64_t *pfns,
                                          uint64_t count,
                                          uint64_t page_size_bytes,
                                          scarlett_mem_access_t access,
                                          scarlett_mem_cache_attr_t cache_attr,
                                          bool require_2mb_group_alignment);
    void release_reservation_locked(VirtualReservation &res);

    mutable std::mutex mutex_;
    std::atomic<uint32_t> strict_mode_{0};
    scarlett_console_model_t console_model_ = SCARLETT_MODEL_ANACONDA;

    uint64_t next_alloc_id_ = 1;
    uint64_t next_res_id_ = 1;
    uint64_t next_pfn_ = 0x1000;
    uint64_t graphics_cursor_ = SCARLETT_MEM_GRAPHICS_BASE;
    uint64_t title_cursor_ = SCARLETT_MEM_TITLE_BASE;

    uint64_t total_reserved_bytes_ = 0;
    uint64_t total_committed_bytes_ = 0;
    uint64_t physical_allocated_bytes_ = 0;
    uint64_t physical_mapped_bytes_ = 0;
    uint64_t graphics_committed_bytes_ = 0;
    uint64_t title_committed_bytes_ = 0;
    uint64_t heap_allocated_bytes_ = 0;
    uint64_t peak_committed_bytes_ = 0;
    uint64_t peak_legacy_bytes_ = 0;
    uint64_t peak_title_bytes_ = 0;
    uint32_t active_alias_mappings_ = 0;

    std::unordered_map<uint64_t, PhysicalAllocation> phys_allocs_;
    std::unordered_map<uint64_t, PhysicalPage> phys_pages_;
    std::unordered_map<uint64_t, VirtualReservation> reservations_; /* keyed by base_va */
    std::unordered_map<uint64_t, HeapAllocation> heap_allocs_;      /* keyed by user ptr */
};

/* ============================================================================
 * 3-Tier UMA Bridge Subsystem (xmem_uma_bridge.cpp)
 * ============================================================================ */

class UmaBridge {
public:
    static UmaBridge &instance();

    void initialize_from_host();
    void reset_all();

    scarlett_hresult_t probe(scarlett_xmem_uma_probe_params_t &params);
    scarlett_hresult_t export_region(scarlett_xmem_uma_export_params_t &params);
    scarlett_hresult_t flush_before_submit(scarlett_xmem_flush_dirty_params_t &params);
    scarlett_hresult_t track_hot_resource(scarlett_xmem_uma_hot_params_t &params);

    void unregister_range(uint64_t va, uint64_t size_bytes);
    void set_gpu_upload_callback(scarlett_uma_gpu_upload_cb_t cb, void *user_data);
    scarlett_hresult_t read_gpu_shadow(uint64_t region_base_va,
                                       uint64_t byte_offset,
                                       void *dst_buffer,
                                       uint64_t byte_length);

    scarlett_uma_tier_t active_tier() const noexcept;
    void get_flush_stats(uint64_t &out_flushes, uint64_t &out_pages) const noexcept;

private:
    UmaBridge();
    ~UmaBridge();

    struct ExportedRegion {
        uint64_t base_va = 0;
        uint64_t size_bytes = 0;
        scarlett_uma_tier_t tier = SCARLETT_UMA_TIER_C_UFFD_WP;
        scarlett_xmem_uma_watch_mode_t watch_mode = XMEM_UMA_WATCH_NONE;
        int watch_slot_index = -1;
        bool uffd_registered = false;
        /* GPU-side shadow buffer (used by Tier C write-watch and Tier B hot migration) */
        std::vector<uint8_t> gpu_shadow;
        /* Tier B hot-resource tracking */
        uint64_t gpu_reads = 0;
        uint64_t cpu_writes = 0;
        bool is_migrated = false;
        uint32_t migration_count = 0;
    };

    void probe_host_vulkan_and_kernel_locked();
    scarlett_uma_tier_t select_tier_locked(scarlett_uma_tier_t preferred) const noexcept;
    bool ensure_uffd_thread_locked();
    void stop_uffd_thread();
    void uffd_worker_loop();
    void ensure_sigsegv_handler_installed();
    scarlett_hresult_t arm_tier_c_watch_locked(ExportedRegion &reg);
    void disarm_tier_c_watch_locked(ExportedRegion &reg);
    void unregister_range_locked(uint64_t va, uint64_t size_bytes);

    mutable std::mutex mutex_;
    uint64_t os_page_size_ = 4096;
    uint32_t detected_caps_ = XMEM_UMA_CAP_NONE;
    uint32_t active_caps_ = XMEM_UMA_CAP_NONE;
    scarlett_uma_tier_t forced_tier_ = SCARLETT_UMA_TIER_AUTO;
    scarlett_xmem_uma_watch_mode_t host_watch_mode_ = XMEM_UMA_WATCH_MPROTECT_SIGSEGV;
    uint32_t uffd_errno_ = 0;
    uint64_t min_host_ptr_alignment_ = 4096;
    uint64_t rebar_heap_bytes_ = 0;

    int uffd_fd_ = -1;
    int uffd_wake_efd_ = -1;
    bool uffd_thread_running_ = false;
    std::atomic<bool> uffd_stop_flag_{false};
    pthread_t uffd_thread_{};

    std::atomic<uint64_t> total_submit_flushes_{0};
    std::atomic<uint64_t> total_dirty_pages_flushed_{0};
    std::atomic<uint64_t> total_present_flushes_{0};
    std::atomic<uint64_t> total_migrated_hot_regions_{0};

    scarlett_uma_gpu_upload_cb_t gpu_upload_cb_ = nullptr;
    void *gpu_upload_user_data_ = nullptr;

    std::unordered_map<uint64_t, ExportedRegion> exported_regions_;
};

/* ============================================================================
 * Thread Topology, Time-Sensitive & /dev/ntsync Subsystem (xmem_thread_sync.cpp)
 * ============================================================================ */

class ThreadSyncManager {
public:
    static ThreadSyncManager &instance();

    void initialize();
    void reset_sync_handles();

    scarlett_hresult_t handle_topology(scarlett_xmem_thread_topology_params_t &params);
    scarlett_hresult_t handle_time_sensitive(scarlett_xmem_thread_ts_params_t &params,
                                             uint32_t strict_mode);

    scarlett_hresult_t sync_create(scarlett_xmem_sync_create_params_t &params);
    scarlett_hresult_t sync_op(scarlett_xmem_sync_op_params_t &params);
    scarlett_hresult_t sync_wait(scarlett_xmem_sync_wait_params_t &params);

private:
    ThreadSyncManager();
    ~ThreadSyncManager();

    struct CpuTopology {
        uint32_t host_total_cpus = 1;
        uint32_t host_pcore_cpus = 1;
        uint32_t host_ecore_cpus = 0;
        uint32_t is_hybrid = 0;
        uint32_t xbox_thread_to_host_cpu[XMEM_XBOX_TITLE_THREADS] = {};
    };

    struct SyncObject {
        uint64_t handle = 0;
        scarlett_xmem_sync_type_t type = XMEM_SYNC_TYPE_EVENT;
        scarlett_xmem_sync_backend_t backend = XMEM_SYNC_BACKEND_FUTEX_FALLBACK;
        int ntsync_obj_fd = -1;
        int notify_efd = -1;
        /* Fallback state (protected by sync_mutex_) */
        uint32_t manual_reset = 0;
        uint32_t signaled = 0;
        uint64_t pulse_seq = 0;
        uint32_t pulse_auto_budget = 0;
        uint32_t sem_count = 0;
        uint32_t sem_max = 1;
        uint32_t mutex_owner_tid = 0;
        uint32_t mutex_count = 0;
    };

    void detect_cpu_topology_locked();
    void probe_ntsync_locked();
    void wake_fallback_waiters_locked();
    bool is_object_signaled_locked(const SyncObject &obj, uint32_t caller_tid) const noexcept;
    void consume_object_locked(SyncObject &obj, uint32_t caller_tid) noexcept;

    mutable std::mutex topo_mutex_;
    bool topo_detected_ = false;
    CpuTopology topology_{};

    mutable std::mutex sync_mutex_;
    bool ntsync_probed_ = false;
    int ntsync_dev_fd_ = -1;
    int32_t ntsync_errno_ = 0;
    scarlett_xmem_sync_backend_t active_backend_ = XMEM_SYNC_BACKEND_FUTEX_FALLBACK;
    uint64_t next_sync_handle_ = 0x100;
    std::atomic<uint32_t> fallback_wait_gen_{0};
    std::unordered_map<uint64_t, SyncObject> sync_objects_;
};

/* Diagnostic stub report handler inside xmem_unix_dispatch.cpp */
scarlett_hresult_t handle_report_stub(scarlett_loader_report_stub_params_t &params);

} // namespace scarlett::xmem::unixlib

#endif /* SCARLETT_XMEM_INTERNAL_H */
