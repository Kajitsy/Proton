/*
 * Proton-Scarlett — XMem / KernelX Physical & Virtual Memory Core (xmem_unix.so)
 *
 * Implements:
 *   - Physical 4KB / 64KB / 2MB / 4MB page pool backed by DMA-BUF / hugetlb / memfd
 *   - Fixed 64-bit MEM_GRAPHICS (0x100000000..0xFFFFFFFFFF) &
 *     MEM_TITLE (0x40000000000..0x7FFFFFFFFFF) virtual reservations via mmap(MAP_FIXED_NOREPLACE)
 *   - Physical page mapping & scatter-gather (64KB pages & 2MB 32-page groups) via mmap(MAP_FIXED)
 *   - Strict invariants:
 *       * RO/RW aliasing of identical physical pages -> ALLOWED (zero-copy hardware coherent)
 *       * Cache-attribute mismatch across aliases -> SCARLETT_ERR_XMEM_CACHE_CONFLICT
 *       * Cross-allocation virtual/physical map or unmap -> SCARLETT_ERR_XMEM_CROSS_ALLOCATION
 *       * Freeing still-mapped physical pages -> SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED
 *   - Console profile budgets: Anaconda (13.5 GiB), Lockhart (8.5 GiB), DevKit (40.0 GiB)
 *   - XMemVirtualQuery, XMemGetWorkingSetStatistics, TitleMemoryStatus, JobTitleMemoryStatus
 *   - XMemAllocDefault / Free / Size (alignment >= 16 bytes)
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xmem_internal.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <linux/memfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_set>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#ifndef MFD_HUGETLB
#define MFD_HUGETLB 0x0004U
#endif

#ifndef MFD_HUGE_2MB
#define MFD_HUGE_2MB (21U << 26)
#endif

namespace scarlett::xmem::unixlib {

namespace {

bool is_valid_page_size(uint64_t page_size) noexcept {
    return page_size == XMEM_PAGE_SIZE_4KB ||
           page_size == XMEM_PAGE_SIZE_64KB ||
           page_size == XMEM_PAGE_SIZE_2MB ||
           page_size == XMEM_PAGE_SIZE_4MB;
}

bool is_valid_cache_attr(uint32_t attr) noexcept {
    return attr == SCARLETT_MEM_CACHE_WRITEBACK ||
           attr == SCARLETT_MEM_CACHE_WRITECOMBINE ||
           attr == SCARLETT_MEM_CACHE_UNCACHED ||
           attr == SCARLETT_MEM_CACHE_GPU_COHERENT;
}

bool is_valid_access(uint32_t access) noexcept {
    return access == SCARLETT_MEM_ACCESS_NOACCESS ||
           access == SCARLETT_MEM_ACCESS_READONLY ||
           access == SCARLETT_MEM_ACCESS_READWRITE;
}

int access_to_mmap_prot(scarlett_mem_access_t access) noexcept {
    switch (access) {
        case SCARLETT_MEM_ACCESS_READONLY:  return PROT_READ;
        case SCARLETT_MEM_ACCESS_READWRITE: return PROT_READ | PROT_WRITE;
        case SCARLETT_MEM_ACCESS_NOACCESS:
        default:                            return PROT_NONE;
    }
}

uint32_t access_and_cache_to_xmem_protect(scarlett_mem_access_t access,
                                          scarlett_mem_cache_attr_t cache_attr) noexcept {
    uint32_t prot = XMEM_PAGE_NOACCESS;
    if (access == SCARLETT_MEM_ACCESS_READONLY) {
        prot = XMEM_PAGE_READONLY;
    } else if (access == SCARLETT_MEM_ACCESS_READWRITE) {
        prot = XMEM_PAGE_READWRITE;
    }

    switch (cache_attr) {
        case SCARLETT_MEM_CACHE_WRITECOMBINE:
            prot |= XMEM_PAGE_WRITECOMBINE;
            break;
        case SCARLETT_MEM_CACHE_UNCACHED:
            prot |= XMEM_PAGE_NOCACHE;
            break;
        case SCARLETT_MEM_CACHE_GPU_COHERENT:
            prot |= XMEM_PAGE_GPU_COHERENT;
            break;
        case SCARLETT_MEM_CACHE_WRITEBACK:
        default:
            break;
    }
    return prot;
}

scarlett_mem_access_t xmem_protect_to_access(uint32_t protect) noexcept {
    if ((protect & XMEM_PAGE_READWRITE) != 0u) {
        return SCARLETT_MEM_ACCESS_READWRITE;
    }
    if ((protect & XMEM_PAGE_READONLY) != 0u) {
        return SCARLETT_MEM_ACCESS_READONLY;
    }
    return SCARLETT_MEM_ACCESS_NOACCESS;
}

scarlett_mem_cache_attr_t xmem_protect_to_cache_attr(uint32_t protect) noexcept {
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

uint64_t align_up_u64(uint64_t val, uint64_t align) noexcept {
    if (align <= 1u) {
        return val;
    }
    uint64_t rem = val % align;
    if (rem == 0u) {
        return val;
    }
    if (val > UINT64_MAX - (align - rem)) {
        return 0u; /* overflow sentinel */
    }
    return val + (align - rem);
}

bool ranges_overlap(uint64_t a_start, uint64_t a_len,
                    uint64_t b_start, uint64_t b_len) noexcept {
    if (a_len == 0u || b_len == 0u) {
        return false;
    }
    uint64_t a_end = a_start + a_len;
    uint64_t b_end = b_start + b_len;
    return a_start < b_end && b_start < a_end;
}

scarlett_console_model_t parse_env_console_model() noexcept {
    const char *env = std::getenv(SCARLETT_ENV_MODEL);
    if (env == nullptr || env[0] == '\0') {
        return SCARLETT_MODEL_ANACONDA;
    }
    if (strcasecmp(env, "lockhart") == 0 || strcasecmp(env, "xss") == 0) {
        return SCARLETT_MODEL_LOCKHART;
    }
    if (strcasecmp(env, "devkit") == 0) {
        return SCARLETT_MODEL_DEVKIT;
    }
    return SCARLETT_MODEL_ANACONDA;
}

uint32_t parse_env_strict_mode() noexcept {
    const char *env = std::getenv(SCARLETT_ENV_STRICT);
    if (env != nullptr && (env[0] == '1' || strcasecmp(env, "true") == 0)) {
        return 1u;
    }
    return 0u;
}

} // namespace

MemoryManager &MemoryManager::instance() {
    static MemoryManager mgr;
    return mgr;
}

MemoryManager::MemoryManager()
    : strict_mode_(parse_env_strict_mode()),
      console_model_(parse_env_console_model()) {
    /* Ensure UmaBridge is constructed before MemoryManager so it outlives ~MemoryManager(). */
    (void)UmaBridge::instance();
}

MemoryManager::~MemoryManager() {
    reset_all();
}

scarlett_console_model_t MemoryManager::console_model() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return console_model_;
}

uint32_t MemoryManager::strict_mode() const noexcept {
    return strict_mode_.load(std::memory_order_acquire);
}

uint64_t MemoryManager::budget_bytes_locked() const noexcept {
    return scarlett_model_memory_budget_bytes(console_model_);
}

void MemoryManager::model_pools_locked(uint64_t &fast_bytes,
                                       uint64_t &std_bytes) const noexcept {
    switch (console_model_) {
        case SCARLETT_MODEL_LOCKHART:
            fast_bytes = SCARLETT_BUDGET_LOCKHART_FAST_BYTES;
            std_bytes = SCARLETT_BUDGET_LOCKHART_STD_BYTES;
            break;
        case SCARLETT_MODEL_DEVKIT:
            fast_bytes = SCARLETT_BUDGET_DEVKIT_FAST_BYTES;
            std_bytes = SCARLETT_BUDGET_DEVKIT_STD_BYTES;
            break;
        case SCARLETT_MODEL_ANACONDA:
        default:
            fast_bytes = SCARLETT_BUDGET_ANACONDA_FAST_BYTES;
            std_bytes = SCARLETT_BUDGET_ANACONDA_STD_BYTES;
            break;
    }
}

void MemoryManager::record_peak_locked() noexcept {
    if (total_committed_bytes_ > peak_committed_bytes_) {
        peak_committed_bytes_ = total_committed_bytes_;
    }
    if (title_committed_bytes_ > peak_legacy_bytes_) {
        peak_legacy_bytes_ = title_committed_bytes_;
    }
    if (total_committed_bytes_ > peak_title_bytes_) {
        peak_title_bytes_ = total_committed_bytes_;
    }
}

scarlett_hresult_t MemoryManager::initialize(scarlett_console_model_t model,
                                             uint32_t strict_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (model != SCARLETT_MODEL_ANACONDA &&
        model != SCARLETT_MODEL_LOCKHART &&
        model != SCARLETT_MODEL_DEVKIT) {
        return SCARLETT_E_INVALIDARG;
    }
    console_model_ = model;
    strict_mode_.store(strict_mode != 0u ? 1u : 0u, std::memory_order_release);
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::set_model(scarlett_console_model_t model,
                                            bool reset_allocations,
                                            uint32_t strict_mode) {
    if (model != SCARLETT_MODEL_ANACONDA &&
        model != SCARLETT_MODEL_LOCKHART &&
        model != SCARLETT_MODEL_DEVKIT) {
        return SCARLETT_E_INVALIDARG;
    }

    if (reset_allocations) {
        reset_all();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    console_model_ = model;
    if (strict_mode != UINT32_C(0xFFFFFFFF)) {
        strict_mode_.store(strict_mode != 0u ? 1u : 0u, std::memory_order_release);
    }
    if (total_committed_bytes_ > budget_bytes_locked()) {
        return SCARLETT_ERR_XMEM_BUDGET_EXCEEDED;
    }
    return SCARLETT_OK;
}

void MemoryManager::release_reservation_locked(VirtualReservation &res) {
    if (UmaBridge::is_alive()) {
        UmaBridge::instance().unregister_range(res.base_va, res.size_bytes);
    }

    for (const auto &span : res.mapped_spans) {
        if (span.is_external_pfn) {
            auto it = phys_pages_.find(span.pfn);
            if (it != phys_pages_.end() && it->second.active_map_count > 0u) {
                it->second.active_map_count--;
            }
            if (active_alias_mappings_ > 0u) {
                active_alias_mappings_--;
            }
            if (physical_mapped_bytes_ >= span.size_bytes) {
                physical_mapped_bytes_ -= span.size_bytes;
            }
        } else {
            if (total_committed_bytes_ >= span.size_bytes) {
                total_committed_bytes_ -= span.size_bytes;
            }
            if (res.region == SCARLETT_MEM_REGION_GRAPHICS) {
                if (graphics_committed_bytes_ >= span.size_bytes) {
                    graphics_committed_bytes_ -= span.size_bytes;
                }
            } else {
                if (title_committed_bytes_ >= span.size_bytes) {
                    title_committed_bytes_ -= span.size_bytes;
                }
            }
        }
    }
    res.mapped_spans.clear();

    if (res.owned_commit_fd >= 0) {
        ::close(res.owned_commit_fd);
        res.owned_commit_fd = -1;
    }
    if (res.base_va != 0u && res.size_bytes > 0u) {
        ::munmap(reinterpret_cast<void *>(res.base_va),
                 static_cast<size_t>(res.size_bytes));
    }
    if (total_reserved_bytes_ >= res.size_bytes) {
        total_reserved_bytes_ -= res.size_bytes;
    }
}

void MemoryManager::reset_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (UmaBridge::is_alive()) {
        UmaBridge::instance().reset_all();
    }

    for (auto &kv : reservations_) {
        release_reservation_locked(kv.second);
    }
    reservations_.clear();

    for (auto &kv : phys_allocs_) {
        if (kv.second.backing_fd >= 0) {
            ::close(kv.second.backing_fd);
            kv.second.backing_fd = -1;
        }
    }
    phys_allocs_.clear();
    phys_pages_.clear();

    for (auto &kv : heap_allocs_) {
        std::free(reinterpret_cast<void *>(kv.second.base_va));
    }
    heap_allocs_.clear();

    total_reserved_bytes_ = 0;
    total_committed_bytes_ = 0;
    physical_allocated_bytes_ = 0;
    physical_mapped_bytes_ = 0;
    graphics_committed_bytes_ = 0;
    title_committed_bytes_ = 0;
    heap_allocated_bytes_ = 0;
    peak_committed_bytes_ = 0;
    peak_legacy_bytes_ = 0;
    peak_title_bytes_ = 0;
    active_alias_mappings_ = 0;
    graphics_cursor_ = SCARLETT_MEM_GRAPHICS_BASE;
    title_cursor_ = SCARLETT_MEM_TITLE_BASE;
}

MemoryManager::VirtualReservation *
MemoryManager::find_reservation_containing_locked(uint64_t addr) {
    for (auto &kv : reservations_) {
        auto &res = kv.second;
        if (addr >= res.base_va && (addr - res.base_va) < res.size_bytes) {
            return &res;
        }
    }
    return nullptr;
}

int MemoryManager::create_backing_fd_locked(uint64_t total_bytes,
                                            uint64_t page_size_bytes,
                                            uint32_t flags,
                                            bool &out_is_dmabuf,
                                            bool &out_is_hugetlb) {
    out_is_dmabuf = false;
    out_is_hugetlb = false;

    /* 1. Try Linux DMA-BUF system heap if graphics/dmabuf hinted and accessible */
    if ((flags & (XMEM_PHYS_FLAG_GRAPHICS | XMEM_PHYS_FLAG_DMABUF_HINT)) != 0u) {
        int heap_fd = ::open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
        if (heap_fd >= 0) {
            struct dma_heap_allocation_data alloc_data {};
            alloc_data.len = total_bytes;
            alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
            alloc_data.heap_flags = 0;
            if (::ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) == 0) {
                ::close(heap_fd);
                out_is_dmabuf = true;
                return static_cast<int>(alloc_data.fd);
            }
            ::close(heap_fd);
        }
    }

    /* 2. Try 2 MiB hugetlb memfd ONLY when page_size_bytes >= 2 MiB (since hugetlbfs
     *    requires 2 MiB-aligned offsets/lengths and cannot scatter-map 64 KiB pages).
     *    Also verify hugepages are actually reservable via a quick mmap probe, since
     *    ftruncate() on hugetlbfs succeeds even when HugePages_Free == 0. */
    if (page_size_bytes >= XMEM_PAGE_SIZE_2MB &&
        (total_bytes % XMEM_PAGE_SIZE_2MB) == 0u) {
        int hfd = ::memfd_create("scarlett_xmem_2m",
                                 MFD_CLOEXEC | MFD_HUGETLB | MFD_HUGE_2MB);
        if (hfd >= 0) {
            if (::ftruncate(hfd, static_cast<off_t>(total_bytes)) == 0) {
                void *probe = ::mmap(nullptr, static_cast<size_t>(total_bytes),
                                     PROT_NONE, MAP_SHARED, hfd, 0);
                if (probe != MAP_FAILED) {
                    ::munmap(probe, static_cast<size_t>(total_bytes));
                    out_is_hugetlb = true;
                    return hfd;
                }
            }
            ::close(hfd);
        }
    }

    /* 3. Standard anonymous memfd_create (zero-copy aliasable + VK_KHR_external_memory_fd) */
    int fd = ::memfd_create("scarlett_xmem_phys", MFD_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    if (::ftruncate(fd, static_cast<off_t>(total_bytes)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

scarlett_hresult_t MemoryManager::reserve_va_locked(uint64_t requested_base,
                                                    uint64_t size_bytes,
                                                    uint64_t align_bytes,
                                                    scarlett_mem_region_t region,
                                                    uint64_t &out_base) {
    const uint64_t win_base = (region == SCARLETT_MEM_REGION_GRAPHICS)
                                  ? SCARLETT_MEM_GRAPHICS_BASE
                                  : SCARLETT_MEM_TITLE_BASE;
    const uint64_t win_end = (region == SCARLETT_MEM_REGION_GRAPHICS)
                                 ? SCARLETT_MEM_GRAPHICS_END
                                 : SCARLETT_MEM_TITLE_END;

    if (size_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    if (requested_base != 0u) {
        if ((requested_base % align_bytes) != 0u) {
            return SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        }
        bool in_win = (region == SCARLETT_MEM_REGION_GRAPHICS)
                          ? scarlett_addr_is_graphics(requested_base, size_bytes)
                          : scarlett_addr_is_title(requested_base, size_bytes);
        if (!in_win) {
            return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        }
        for (const auto &kv : reservations_) {
            if (ranges_overlap(requested_base, size_bytes,
                               kv.second.base_va, kv.second.size_bytes)) {
                return SCARLETT_E_OUTOFMEMORY;
            }
        }
        void *mapped = ::mmap(reinterpret_cast<void *>(requested_base),
                              static_cast<size_t>(size_bytes),
                              PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
                              -1, 0);
        if (mapped == MAP_FAILED) {
            return SCARLETT_E_OUTOFMEMORY;
        }
        if (reinterpret_cast<uint64_t>(mapped) != requested_base) {
            ::munmap(mapped, static_cast<size_t>(size_bytes));
            return SCARLETT_E_OUTOFMEMORY;
        }
        out_base = requested_base;
        return SCARLETT_OK;
    }

    uint64_t &cursor = (region == SCARLETT_MEM_REGION_GRAPHICS)
                           ? graphics_cursor_
                           : title_cursor_;
    uint64_t candidate = align_up_u64(cursor, align_bytes);
    if (candidate == 0u || candidate < win_base ||
        candidate > win_end || (size_bytes - 1u) > (win_end - candidate)) {
        candidate = align_up_u64(win_base, align_bytes);
    }

    for (uint32_t attempt = 0; attempt < 4096u; ++attempt) {
        if (candidate < win_base || candidate > win_end ||
            (size_bytes - 1u) > (win_end - candidate)) {
            candidate = align_up_u64(win_base, align_bytes);
            if (candidate == 0u || (size_bytes - 1u) > (win_end - candidate)) {
                return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
            }
        }

        bool overlaps = false;
        uint64_t jump_to = candidate + align_bytes;
        for (const auto &kv : reservations_) {
            if (ranges_overlap(candidate, size_bytes,
                               kv.second.base_va, kv.second.size_bytes)) {
                overlaps = true;
                uint64_t after = align_up_u64(kv.second.base_va + kv.second.size_bytes,
                                              align_bytes);
                if (after > jump_to) {
                    jump_to = after;
                }
            }
        }
        if (overlaps) {
            candidate = jump_to;
            continue;
        }

        void *mapped = ::mmap(reinterpret_cast<void *>(candidate),
                              static_cast<size_t>(size_bytes),
                              PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
                              -1, 0);
        if (mapped != MAP_FAILED && reinterpret_cast<uint64_t>(mapped) == candidate) {
            cursor = align_up_u64(candidate + size_bytes, XMEM_PAGE_SIZE_64KB);
            out_base = candidate;
            return SCARLETT_OK;
        }
        if (mapped != MAP_FAILED) {
            ::munmap(mapped, static_cast<size_t>(size_bytes));
        }
        candidate = align_up_u64(candidate + std::max(align_bytes, size_bytes), align_bytes);
    }

    return SCARLETT_E_OUTOFMEMORY;
}

scarlett_hresult_t
MemoryManager::reserve_or_commit_range(scarlett_xmem_reserve_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params.out_base = 0;

    if (params.size_bytes == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    /* Decode flags layout:
     * low 16..31 bits may hold XMEM_MEM_* + protect passed via high bits or standard flags.
     * In scarlett_xmem_reserve_params_t, `flags` carries XMEM_MEM_* bits in bits [12..31]
     * and protection bits in bits [0..11] (note: XMEM_PAGE_NOACCESS..WRITECOMBINE fit in 0x0FFF!).
     */
    const uint32_t alloc_flags = params.flags & UINT32_C(0xFFFFF000);
    uint32_t protect_bits = params.flags & UINT32_C(0x00000FFF);
    if ((params.flags & XMEM_PAGE_GPU_COHERENT) != 0u &&
        (params.flags & XMEM_MEM_4MB_PAGES) == 0u) {
        /* 0x80000000 is XMEM_MEM_4MB_PAGES in alloc_type; protect defaults below */
    }
    const bool do_reserve = (alloc_flags & XMEM_MEM_RESERVE) != 0u || alloc_flags == 0u;
    const bool do_commit = (alloc_flags & XMEM_MEM_COMMIT) != 0u;
    if (protect_bits == 0u) {
        protect_bits = do_commit ? XMEM_PAGE_READWRITE : XMEM_PAGE_NOACCESS;
    }

    uint64_t granularity = XMEM_PAGE_SIZE_64KB;
    if ((alloc_flags & XMEM_MEM_4MB_PAGES) != 0u) {
        granularity = XMEM_PAGE_SIZE_4MB;
    } else if ((alloc_flags & XMEM_MEM_LARGE_PAGES) != 0u) {
        granularity = XMEM_PAGE_SIZE_2MB;
    }

    const uint64_t aligned_size = align_up_u64(params.size_bytes, granularity);
    if (aligned_size == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    /* Determine region (MEM_GRAPHICS vs MEM_TITLE) */
    scarlett_mem_region_t region = static_cast<scarlett_mem_region_t>(params.region);
    if (region == SCARLETT_MEM_REGION_UNKNOWN) {
        if ((alloc_flags & XMEM_MEM_GRAPHICS) != 0u) {
            region = SCARLETT_MEM_REGION_GRAPHICS;
        } else if ((alloc_flags & XMEM_MEM_TITLE) != 0u) {
            region = SCARLETT_MEM_REGION_TITLE;
        } else if (params.requested_base != 0u) {
            if (scarlett_addr_is_graphics(params.requested_base, aligned_size)) {
                region = SCARLETT_MEM_REGION_GRAPHICS;
            } else if (scarlett_addr_is_title(params.requested_base, aligned_size)) {
                region = SCARLETT_MEM_REGION_TITLE;
            } else {
                params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
                return params.out_status;
            }
        } else {
            region = SCARLETT_MEM_REGION_TITLE;
        }
    }

    /* Commit-only inside an existing reservation */
    if (!do_reserve && do_commit) {
        if (params.requested_base == 0u) {
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
        }
        VirtualReservation *res = find_reservation_containing_locked(params.requested_base);
        if (res == nullptr) {
            params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
            return params.out_status;
        }
        if ((params.requested_base % res->granularity) != 0u) {
            params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
            return params.out_status;
        }
        if (params.requested_base + aligned_size > res->base_va + res->size_bytes) {
            for (const auto &kv : reservations_) {
                if (kv.second.res_id != res->res_id &&
                    ranges_overlap(params.requested_base, aligned_size,
                                   kv.second.base_va, kv.second.size_bytes)) {
                    params.out_status = SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
                    return params.out_status;
                }
            }
            params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
            return params.out_status;
        }

        if (total_committed_bytes_ + aligned_size > budget_bytes_locked()) {
            params.out_status = SCARLETT_ERR_XMEM_BUDGET_EXCEEDED;
            return params.out_status;
        }

        for (const auto &sp : res->mapped_spans) {
            if (ranges_overlap(params.requested_base, aligned_size, sp.va, sp.size_bytes)) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
        }

        if (res->owned_commit_fd < 0) {
            bool is_dmabuf = false;
            bool is_huge = false;
            uint32_t phys_flags = (res->region == SCARLETT_MEM_REGION_GRAPHICS)
                                      ? XMEM_PHYS_FLAG_GRAPHICS
                                      : XMEM_PHYS_FLAG_TITLE;
            res->owned_commit_fd = create_backing_fd_locked(
                res->size_bytes, res->granularity, phys_flags, is_dmabuf, is_huge);
            if (res->owned_commit_fd < 0) {
                params.out_status = SCARLETT_E_OUTOFMEMORY;
                return params.out_status;
            }
            res->owned_commit_alloc_id = next_alloc_id_++;
        }

        const scarlett_mem_access_t acc = xmem_protect_to_access(protect_bits);
        const scarlett_mem_cache_attr_t cattr = xmem_protect_to_cache_attr(protect_bits);
        const uint64_t fd_off = params.requested_base - res->base_va;
        void *mapped = ::mmap(reinterpret_cast<void *>(params.requested_base),
                              static_cast<size_t>(aligned_size),
                              access_to_mmap_prot(acc),
                              MAP_SHARED | MAP_FIXED,
                              res->owned_commit_fd,
                              static_cast<off_t>(fd_off));
        if (mapped == MAP_FAILED) {
            params.out_status = SCARLETT_E_OUTOFMEMORY;
            return params.out_status;
        }

        MappedSpan span{};
        span.va = params.requested_base;
        span.size_bytes = aligned_size;
        span.pfn = 0;
        span.phys_alloc_id = res->owned_commit_alloc_id;
        span.backing_fd = res->owned_commit_fd;
        span.fd_offset = fd_off;
        span.protect = protect_bits;
        span.access = acc;
        span.cache_attr = cattr;
        span.is_external_pfn = false;
        res->mapped_spans.push_back(span);
        std::sort(res->mapped_spans.begin(), res->mapped_spans.end(),
                  [](const MappedSpan &a, const MappedSpan &b) { return a.va < b.va; });

        total_committed_bytes_ += aligned_size;
        if (res->region == SCARLETT_MEM_REGION_GRAPHICS) {
            graphics_committed_bytes_ += aligned_size;
        } else {
            title_committed_bytes_ += aligned_size;
        }
        record_peak_locked();

        params.out_base = params.requested_base;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    /* Check console memory budget BEFORE reserving VA if MEM_COMMIT is requested */
    if (do_commit && (total_committed_bytes_ + aligned_size > budget_bytes_locked())) {
        params.out_status = SCARLETT_ERR_XMEM_BUDGET_EXCEEDED;
        return params.out_status;
    }

    uint64_t reserve_align = granularity;
    if (params.requested_base == 0u &&
        aligned_size >= XMEM_PAGE_SIZE_2MB &&
        (aligned_size % XMEM_PAGE_SIZE_2MB) == 0u &&
        reserve_align < XMEM_PAGE_SIZE_2MB) {
        reserve_align = XMEM_PAGE_SIZE_2MB;
    }

    uint64_t chosen_base = 0;
    scarlett_hresult_t hr = reserve_va_locked(params.requested_base,
                                              aligned_size,
                                              reserve_align,
                                              region,
                                              chosen_base);
    if (scarlett_failed(hr)) {
        params.out_status = hr;
        return hr;
    }

    VirtualReservation res{};
    res.res_id = next_res_id_++;
    res.base_va = chosen_base;
    res.size_bytes = aligned_size;
    res.granularity = granularity;
    res.region = region;
    res.alloc_protect = protect_bits;
    res.type = ((alloc_flags & XMEM_MEM_PHYSICAL) != 0u) ? XMEM_MEM_PHYSICAL : XMEM_MEM_PRIVATE;

    if (do_commit) {
        bool is_dmabuf = false;
        bool is_huge = false;
        uint32_t phys_flags = (region == SCARLETT_MEM_REGION_GRAPHICS)
                                  ? XMEM_PHYS_FLAG_GRAPHICS
                                  : XMEM_PHYS_FLAG_TITLE;
        if (granularity >= XMEM_PAGE_SIZE_2MB) {
            phys_flags |= XMEM_PHYS_FLAG_HUGEPAGE_HINT;
        }
        int fd = create_backing_fd_locked(aligned_size, granularity, phys_flags,
                                          is_dmabuf, is_huge);
        if (fd < 0) {
            ::munmap(reinterpret_cast<void *>(chosen_base),
                     static_cast<size_t>(aligned_size));
            params.out_status = SCARLETT_E_OUTOFMEMORY;
            return params.out_status;
        }

        const scarlett_mem_access_t acc = xmem_protect_to_access(protect_bits);
        const scarlett_mem_cache_attr_t cattr = xmem_protect_to_cache_attr(protect_bits);
        void *mapped = ::mmap(reinterpret_cast<void *>(chosen_base),
                              static_cast<size_t>(aligned_size),
                              access_to_mmap_prot(acc),
                              MAP_SHARED | MAP_FIXED,
                              fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            ::munmap(reinterpret_cast<void *>(chosen_base),
                     static_cast<size_t>(aligned_size));
            params.out_status = SCARLETT_E_OUTOFMEMORY;
            return params.out_status;
        }
        if (granularity >= XMEM_PAGE_SIZE_2MB) {
            ::madvise(mapped, static_cast<size_t>(aligned_size), MADV_HUGEPAGE);
        }

        res.owned_commit_fd = fd;
        res.owned_commit_alloc_id = next_alloc_id_++;

        MappedSpan span{};
        span.va = chosen_base;
        span.size_bytes = aligned_size;
        span.pfn = 0;
        span.phys_alloc_id = res.owned_commit_alloc_id;
        span.backing_fd = fd;
        span.fd_offset = 0;
        span.protect = protect_bits;
        span.access = acc;
        span.cache_attr = cattr;
        span.is_external_pfn = false;
        span.is_dmabuf = is_dmabuf;
        res.mapped_spans.push_back(span);

        total_committed_bytes_ += aligned_size;
        if (region == SCARLETT_MEM_REGION_GRAPHICS) {
            graphics_committed_bytes_ += aligned_size;
        } else {
            title_committed_bytes_ += aligned_size;
        }
        record_peak_locked();
    }

    total_reserved_bytes_ += aligned_size;
    reservations_.emplace(chosen_base, std::move(res));

    params.out_base = chosen_base;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::free_range(scarlett_xmem_free_range_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (params.base_addr == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    if (params.free_type == XMEM_MEM_RELEASE) {
        auto it = reservations_.find(params.base_addr);
        if (it == reservations_.end()) {
            params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
            return params.out_status;
        }
        if (params.size_bytes != 0u && params.size_bytes != it->second.size_bytes) {
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
        }
        release_reservation_locked(it->second);
        reservations_.erase(it);
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (params.free_type == XMEM_MEM_DECOMMIT) {
        VirtualReservation *res = find_reservation_containing_locked(params.base_addr);
        if (res == nullptr) {
            params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
            return params.out_status;
        }
        const uint64_t decommit_size = (params.size_bytes == 0u)
                                           ? (res->base_va + res->size_bytes - params.base_addr)
                                           : align_up_u64(params.size_bytes, res->granularity);
        if (decommit_size == 0u ||
            params.base_addr + decommit_size > res->base_va + res->size_bytes) {
            for (const auto &kv : reservations_) {
                if (kv.second.res_id != res->res_id &&
                    ranges_overlap(params.base_addr, decommit_size,
                                   kv.second.base_va, kv.second.size_bytes)) {
                    params.out_status = SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
                    return params.out_status;
                }
            }
            params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
            return params.out_status;
        }

        UmaBridge::instance().unregister_range(params.base_addr, decommit_size);

        void *replaced = ::mmap(reinterpret_cast<void *>(params.base_addr),
                                static_cast<size_t>(decommit_size),
                                PROT_NONE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                                -1, 0);
        if (replaced == MAP_FAILED) {
            params.out_status = SCARLETT_E_FAIL;
            return params.out_status;
        }

        std::vector<MappedSpan> remaining;
        remaining.reserve(res->mapped_spans.size());
        for (const auto &sp : res->mapped_spans) {
            if (!ranges_overlap(params.base_addr, decommit_size, sp.va, sp.size_bytes)) {
                remaining.push_back(sp);
                continue;
            }
            if (sp.is_external_pfn) {
                auto pit = phys_pages_.find(sp.pfn);
                if (pit != phys_pages_.end() && pit->second.active_map_count > 0u) {
                    pit->second.active_map_count--;
                }
                if (active_alias_mappings_ > 0u) {
                    active_alias_mappings_--;
                }
                if (physical_mapped_bytes_ >= sp.size_bytes) {
                    physical_mapped_bytes_ -= sp.size_bytes;
                }
            } else {
                if (total_committed_bytes_ >= sp.size_bytes) {
                    total_committed_bytes_ -= sp.size_bytes;
                }
                if (res->region == SCARLETT_MEM_REGION_GRAPHICS) {
                    if (graphics_committed_bytes_ >= sp.size_bytes) {
                        graphics_committed_bytes_ -= sp.size_bytes;
                    }
                } else {
                    if (title_committed_bytes_ >= sp.size_bytes) {
                        title_committed_bytes_ -= sp.size_bytes;
                    }
                }
            }
        }
        res->mapped_spans = std::move(remaining);
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    params.out_status = SCARLETT_E_INVALIDARG;
    return params.out_status;
}

scarlett_hresult_t
MemoryManager::alloc_phys_pages(scarlett_xmem_alloc_pages_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params.out_alloc_id = 0;

    if (params.page_count == 0u || params.out_pfn_array_addr == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (!is_valid_page_size(params.page_size_bytes)) {
        params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        return params.out_status;
    }
    if (!is_valid_cache_attr(params.cache_attr)) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (params.page_count > UINT64_MAX / params.page_size_bytes) {
        params.out_status = SCARLETT_ERR_XMEM_BUDGET_EXCEEDED;
        return params.out_status;
    }

    const uint64_t total_bytes = params.page_count * params.page_size_bytes;
    if (total_committed_bytes_ + total_bytes > budget_bytes_locked()) {
        params.out_status = SCARLETT_ERR_XMEM_BUDGET_EXCEEDED;
        return params.out_status;
    }

    bool is_dmabuf = false;
    bool is_hugetlb = false;
    int fd = create_backing_fd_locked(total_bytes, params.page_size_bytes,
                                      params.flags, is_dmabuf, is_hugetlb);
    if (fd < 0) {
        params.out_status = SCARLETT_E_OUTOFMEMORY;
        return params.out_status;
    }

    const uint64_t alloc_id = next_alloc_id_++;
    const auto cache_attr = static_cast<scarlett_mem_cache_attr_t>(params.cache_attr);
    const scarlett_mem_region_t acct_region =
        ((params.flags & XMEM_PHYS_FLAG_GRAPHICS) != 0u)
            ? SCARLETT_MEM_REGION_GRAPHICS
            : SCARLETT_MEM_REGION_TITLE;

    PhysicalAllocation alloc{};
    alloc.alloc_id = alloc_id;
    alloc.backing_fd = fd;
    alloc.page_size_bytes = params.page_size_bytes;
    alloc.total_bytes = total_bytes;
    alloc.remaining_pages = params.page_count;
    alloc.cache_attr = cache_attr;
    alloc.accounting_region = acct_region;
    alloc.is_dmabuf = is_dmabuf;
    alloc.is_hugetlb = is_hugetlb;

    auto *out_pfns = reinterpret_cast<uint64_t *>(params.out_pfn_array_addr);
    for (uint64_t i = 0; i < params.page_count; ++i) {
        const uint64_t pfn = next_pfn_++;
        PhysicalPage page{};
        page.pfn = pfn;
        page.alloc_id = alloc_id;
        page.page_size_bytes = params.page_size_bytes;
        page.fd_offset = i * params.page_size_bytes;
        page.backing_fd = fd;
        page.cache_attr = cache_attr;
        page.active_map_count = 0;
        page.is_dmabuf = is_dmabuf;
        phys_pages_.emplace(pfn, page);
        out_pfns[i] = pfn;
    }

    phys_allocs_.emplace(alloc_id, alloc);
    physical_allocated_bytes_ += total_bytes;
    total_committed_bytes_ += total_bytes;
    if (acct_region == SCARLETT_MEM_REGION_GRAPHICS) {
        graphics_committed_bytes_ += total_bytes;
    } else {
        title_committed_bytes_ += total_bytes;
    }
    record_peak_locked();

    params.out_alloc_id = alloc_id;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
MemoryManager::free_phys_pages(scarlett_xmem_alloc_pages_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (params.page_count == 0u || params.out_pfn_array_addr == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (!is_valid_page_size(params.page_size_bytes)) {
        params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        return params.out_status;
    }

    const auto *pfns = reinterpret_cast<const uint64_t *>(params.out_pfn_array_addr);
    std::unordered_set<uint64_t> seen;
    seen.reserve(static_cast<size_t>(params.page_count));

    /* Phase 1: Validate every PFN atomically before freeing anything */
    for (uint64_t i = 0; i < params.page_count; ++i) {
        if (!seen.insert(pfns[i]).second) {
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
        }
        auto it = phys_pages_.find(pfns[i]);
        if (it == phys_pages_.end()) {
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
        }
        if (it->second.page_size_bytes != params.page_size_bytes) {
            params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
            return params.out_status;
        }
        if (it->second.active_map_count > 0u) {
            params.out_status = SCARLETT_ERR_XMEM_PAGES_STILL_MAPPED;
            return params.out_status;
        }
    }

    /* Phase 2: Commit free */
    for (uint64_t i = 0; i < params.page_count; ++i) {
        auto pit = phys_pages_.find(pfns[i]);
        const uint64_t alloc_id = pit->second.alloc_id;
        const uint64_t page_sz = pit->second.page_size_bytes;
        phys_pages_.erase(pit);

        auto ait = phys_allocs_.find(alloc_id);
        if (ait != phys_allocs_.end()) {
            if (physical_allocated_bytes_ >= page_sz) {
                physical_allocated_bytes_ -= page_sz;
            }
            if (total_committed_bytes_ >= page_sz) {
                total_committed_bytes_ -= page_sz;
            }
            if (ait->second.accounting_region == SCARLETT_MEM_REGION_GRAPHICS) {
                if (graphics_committed_bytes_ >= page_sz) {
                    graphics_committed_bytes_ -= page_sz;
                }
            } else {
                if (title_committed_bytes_ >= page_sz) {
                    title_committed_bytes_ -= page_sz;
                }
            }

            if (ait->second.remaining_pages > 0u) {
                ait->second.remaining_pages--;
            }
            if (ait->second.remaining_pages == 0u) {
                if (ait->second.backing_fd >= 0) {
                    ::close(ait->second.backing_fd);
                    ait->second.backing_fd = -1;
                }
                phys_allocs_.erase(ait);
            }
        }
    }

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::map_entries_locked(
    uint64_t base_va,
    const uint64_t *byte_offsets,
    const uint64_t *pfns,
    uint64_t count,
    uint64_t page_size_bytes,
    scarlett_mem_access_t access,
    scarlett_mem_cache_attr_t cache_attr,
    bool require_2mb_group_alignment) {
    if (base_va == 0u || pfns == nullptr || count == 0u) {
        return SCARLETT_E_INVALIDARG;
    }
    if (!is_valid_page_size(page_size_bytes)) {
        return SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
    }
    if (!is_valid_access(static_cast<uint32_t>(access)) ||
        !is_valid_cache_attr(static_cast<uint32_t>(cache_attr))) {
        return SCARLETT_E_INVALIDARG;
    }
    if ((base_va % page_size_bytes) != 0u) {
        return SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
    }
    if (count > UINT64_MAX / page_size_bytes) {
        return SCARLETT_E_INVALIDARG;
    }
    if (require_2mb_group_alignment) {
        if ((base_va % XMEM_PAGE_SIZE_2MB) != 0u ||
            ((count * page_size_bytes) % XMEM_PAGE_SIZE_2MB) != 0u) {
            return SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        }
    }

    struct ResolvedEntry {
        uint64_t va;
        uint64_t pfn;
        uint64_t alloc_id;
        int backing_fd;
        uint64_t fd_offset;
        bool is_dmabuf;
    };

    std::vector<ResolvedEntry> entries;
    entries.reserve(static_cast<size_t>(count));

    uint64_t min_va = UINT64_MAX;
    uint64_t max_end_va = 0u;
    uint64_t first_phys_alloc_id = 0u;
    std::unordered_set<uint64_t> target_vas;
    target_vas.reserve(static_cast<size_t>(count));

    /* Phase 1: Validate ALL invariants atomically before any mmap() */
    for (uint64_t i = 0; i < count; ++i) {
        const uint64_t off = (byte_offsets != nullptr)
                                 ? byte_offsets[i]
                                 : (i * page_size_bytes);
        if ((off % page_size_bytes) != 0u || off > UINT64_MAX - base_va) {
            return SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        }
        const uint64_t entry_va = base_va + off;
        if (entry_va > UINT64_MAX - page_size_bytes) {
            return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        }
        if (!target_vas.insert(entry_va).second) {
            return SCARLETT_E_INVALIDARG;
        }
        if (entry_va < min_va) {
            min_va = entry_va;
        }
        if (entry_va + page_size_bytes > max_end_va) {
            max_end_va = entry_va + page_size_bytes;
        }

        auto pit = phys_pages_.find(pfns[i]);
        if (pit == phys_pages_.end()) {
            return SCARLETT_E_INVALIDARG;
        }
        const PhysicalPage &page = pit->second;
        if (page.page_size_bytes != page_size_bytes) {
            return SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        }

        /* STRICT INVARIANT 1: Cache-attribute conflict across aliases -> ERROR */
        if (page.cache_attr != cache_attr) {
            return SCARLETT_ERR_XMEM_CACHE_CONFLICT;
        }

        /* STRICT INVARIANT 2: Mixing PFNs from two distinct physical allocations -> ERROR */
        if (i == 0u) {
            first_phys_alloc_id = page.alloc_id;
        } else if (page.alloc_id != first_phys_alloc_id) {
            return SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
        }

        entries.push_back(ResolvedEntry{
            entry_va,
            page.pfn,
            page.alloc_id,
            page.backing_fd,
            page.fd_offset,
            page.is_dmabuf,
        });
    }

    /* STRICT INVARIANT 3: Entire target span MUST lie inside ONE virtual reservation */
    VirtualReservation *res = find_reservation_containing_locked(min_va);
    if (res == nullptr) {
        return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
    }

    const uint64_t span_len = max_end_va - min_va;
    if (max_end_va > res->base_va + res->size_bytes) {
        for (const auto &kv : reservations_) {
            if (kv.second.res_id != res->res_id &&
                ranges_overlap(min_va, span_len,
                               kv.second.base_va, kv.second.size_bytes)) {
                return SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
            }
        }
        return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
    }

    /* Sort by target VA so we can check overlap and coalesce contiguous runs */
    std::sort(entries.begin(), entries.end(),
              [](const ResolvedEntry &a, const ResolvedEntry &b) {
                  return a.va < b.va;
              });

    for (const auto &entry : entries) {
        for (const auto &existing : res->mapped_spans) {
            if (ranges_overlap(entry.va, page_size_bytes,
                               existing.va, existing.size_bytes)) {
                return SCARLETT_E_INVALIDARG;
            }
        }
    }

    /* Phase 2: Coalesce contiguous (va, fd, fd_offset) runs and mmap(MAP_FIXED) */
    const int prot = access_to_mmap_prot(access);
    const uint32_t xmem_prot = access_and_cache_to_xmem_protect(access, cache_attr);
    size_t idx = 0;
    size_t mapped_entries_so_far = 0;

    while (idx < entries.size()) {
        size_t run_end = idx + 1;
        while (run_end < entries.size() &&
               entries[run_end].backing_fd == entries[idx].backing_fd &&
               entries[run_end].va == entries[run_end - 1].va + page_size_bytes &&
               entries[run_end].fd_offset == entries[run_end - 1].fd_offset + page_size_bytes) {
            ++run_end;
        }

        const uint64_t run_va = entries[idx].va;
        const uint64_t run_len = (run_end - idx) * page_size_bytes;
        void *mapped = ::mmap(reinterpret_cast<void *>(run_va),
                              static_cast<size_t>(run_len),
                              prot,
                              MAP_SHARED | MAP_FIXED,
                              entries[idx].backing_fd,
                              static_cast<off_t>(entries[idx].fd_offset));
        if (mapped == MAP_FAILED) {
            /* Rollback previously mapped entries to preserve atomic semantics */
            for (size_t r = 0; r < mapped_entries_so_far; ++r) {
                ::mmap(reinterpret_cast<void *>(entries[r].va),
                       static_cast<size_t>(page_size_bytes),
                       PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                       -1, 0);
            }
            return SCARLETT_E_OUTOFMEMORY;
        }
        if (run_len >= XMEM_PAGE_SIZE_2MB && (run_va % XMEM_PAGE_SIZE_2MB) == 0u) {
            ::madvise(mapped, static_cast<size_t>(run_len), MADV_HUGEPAGE);
        }

        mapped_entries_so_far = run_end;
        idx = run_end;
    }

    /* Phase 3: Commit metadata and refcounts */
    for (const auto &entry : entries) {
        MappedSpan span{};
        span.va = entry.va;
        span.size_bytes = page_size_bytes;
        span.pfn = entry.pfn;
        span.phys_alloc_id = entry.alloc_id;
        span.backing_fd = entry.backing_fd;
        span.fd_offset = entry.fd_offset;
        span.protect = xmem_prot;
        span.access = access;
        span.cache_attr = cache_attr;
        span.is_external_pfn = true;
        span.is_dmabuf = entry.is_dmabuf;
        res->mapped_spans.push_back(span);

        auto &page = phys_pages_.at(entry.pfn);
        page.active_map_count++;
        active_alias_mappings_++;
        physical_mapped_bytes_ += page_size_bytes;
    }

    std::sort(res->mapped_spans.begin(), res->mapped_spans.end(),
              [](const MappedSpan &a, const MappedSpan &b) { return a.va < b.va; });
    return SCARLETT_OK;
}

scarlett_hresult_t
MemoryManager::map_phys_pages(scarlett_xmem_map_pages_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (params.unmap_flag != 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    const auto *pfns = reinterpret_cast<const uint64_t *>(params.pfn_array_addr);
    params.out_status = map_entries_locked(
        params.virtual_addr,
        nullptr,
        pfns,
        params.page_count,
        params.page_size_bytes,
        static_cast<scarlett_mem_access_t>(params.access),
        static_cast<scarlett_mem_cache_attr_t>(params.cache_attr),
        false);
    return params.out_status;
}

scarlett_hresult_t
MemoryManager::map_phys_pages_scatter(scarlett_xmem_scatter_map_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto *offsets = (params.offsets_array_addr != 0u)
                              ? reinterpret_cast<const uint64_t *>(params.offsets_array_addr)
                              : nullptr;
    const auto *pfns = reinterpret_cast<const uint64_t *>(params.pfn_array_addr);
    const bool require_2mb = (params.flags & XMEM_SCATTER_FLAG_2MB_GROUP) != 0u;

    params.out_status = map_entries_locked(
        params.base_virtual_addr,
        offsets,
        pfns,
        params.entry_count,
        params.page_size_bytes,
        static_cast<scarlett_mem_access_t>(params.access),
        static_cast<scarlett_mem_cache_attr_t>(params.cache_attr),
        require_2mb);
    return params.out_status;
}

scarlett_hresult_t
MemoryManager::unmap_phys_pages(scarlett_xmem_map_pages_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (params.virtual_addr == 0u || params.page_count == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (!is_valid_page_size(params.page_size_bytes) ||
        (params.virtual_addr % params.page_size_bytes) != 0u) {
        params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        return params.out_status;
    }
    if (params.page_count > UINT64_MAX / params.page_size_bytes) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    const uint64_t total_bytes = params.page_count * params.page_size_bytes;
    if (params.virtual_addr > UINT64_MAX - total_bytes) {
        params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return params.out_status;
    }

    /* STRICT INVARIANT 1: Must lie inside a single virtual reservation */
    VirtualReservation *res = find_reservation_containing_locked(params.virtual_addr);
    if (res == nullptr) {
        params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return params.out_status;
    }

    if (params.virtual_addr + total_bytes > res->base_va + res->size_bytes) {
        for (const auto &kv : reservations_) {
            if (kv.second.res_id != res->res_id &&
                ranges_overlap(params.virtual_addr, total_bytes,
                               kv.second.base_va, kv.second.size_bytes)) {
                params.out_status = SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
                return params.out_status;
            }
        }
        params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return params.out_status;
    }

    /* STRICT INVARIANT 2: Cannot unmap across two distinct physical allocations */
    uint64_t first_phys_alloc_id = 0u;
    size_t matched_count = 0;
    for (const auto &sp : res->mapped_spans) {
        if (!ranges_overlap(params.virtual_addr, total_bytes, sp.va, sp.size_bytes)) {
            continue;
        }
        if (sp.va < params.virtual_addr ||
            sp.va + sp.size_bytes > params.virtual_addr + total_bytes) {
            params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
            return params.out_status;
        }
        if (matched_count == 0u) {
            first_phys_alloc_id = sp.phys_alloc_id;
        } else if (sp.phys_alloc_id != first_phys_alloc_id) {
            params.out_status = SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
            return params.out_status;
        }
        ++matched_count;
    }

    if (matched_count == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    /* Unregister from Tier C write-watch BEFORE replacing PTEs with PROT_NONE */
    UmaBridge::instance().unregister_range(params.virtual_addr, total_bytes);

    void *replaced = ::mmap(reinterpret_cast<void *>(params.virtual_addr),
                            static_cast<size_t>(total_bytes),
                            PROT_NONE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
                            -1, 0);
    if (replaced == MAP_FAILED) {
        params.out_status = SCARLETT_E_FAIL;
        return params.out_status;
    }

    std::vector<MappedSpan> remaining;
    remaining.reserve(res->mapped_spans.size());
    for (const auto &sp : res->mapped_spans) {
        if (!ranges_overlap(params.virtual_addr, total_bytes, sp.va, sp.size_bytes)) {
            remaining.push_back(sp);
            continue;
        }
        if (sp.is_external_pfn) {
            auto pit = phys_pages_.find(sp.pfn);
            if (pit != phys_pages_.end() && pit->second.active_map_count > 0u) {
                pit->second.active_map_count--;
            }
            if (active_alias_mappings_ > 0u) {
                active_alias_mappings_--;
            }
            if (physical_mapped_bytes_ >= sp.size_bytes) {
                physical_mapped_bytes_ -= sp.size_bytes;
            }
        }
    }
    res->mapped_spans = std::move(remaining);

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
MemoryManager::virtual_query(scarlett_xmem_virtual_query_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    params.out_base_address = 0;
    params.out_allocation_base = 0;
    params.out_region_size = 0;
    params.out_alloc_id = 0;
    params.out_allocation_protect = XMEM_PAGE_NOACCESS;
    params.out_state = XMEM_MEM_FREE;
    params.out_protect = XMEM_PAGE_NOACCESS;
    params.out_type = 0;
    params.out_region = SCARLETT_MEM_REGION_UNKNOWN;
    params.out_cache_attr = SCARLETT_MEM_CACHE_WRITEBACK;

    VirtualReservation *res = find_reservation_containing_locked(params.query_addr);
    if (res == nullptr) {
        if (scarlett_addr_is_graphics(params.query_addr, 1u)) {
            params.out_region = SCARLETT_MEM_REGION_GRAPHICS;
        } else if (scarlett_addr_is_title(params.query_addr, 1u)) {
            params.out_region = SCARLETT_MEM_REGION_TITLE;
        }
        params.out_base_address = params.query_addr & ~(XMEM_PAGE_SIZE_64KB - 1u);
        params.out_state = XMEM_MEM_FREE;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    params.out_allocation_base = res->base_va;
    params.out_allocation_protect = res->alloc_protect;
    params.out_type = res->type;
    params.out_region = static_cast<uint32_t>(res->region);

    /* Find if query_addr falls inside a committed/mapped span */
    for (size_t i = 0; i < res->mapped_spans.size(); ++i) {
        const auto &sp = res->mapped_spans[i];
        if (params.query_addr >= sp.va &&
            params.query_addr < sp.va + sp.size_bytes) {
            /* Coalesce adjacent spans sharing identical protect, cache_attr, and phys_alloc_id */
            uint64_t run_end = sp.va + sp.size_bytes;
            for (size_t j = i + 1; j < res->mapped_spans.size(); ++j) {
                const auto &nxt = res->mapped_spans[j];
                if (nxt.va == run_end &&
                    nxt.protect == sp.protect &&
                    nxt.cache_attr == sp.cache_attr &&
                    nxt.phys_alloc_id == sp.phys_alloc_id) {
                    run_end = nxt.va + nxt.size_bytes;
                } else {
                    break;
                }
            }
            const uint64_t aligned_addr = params.query_addr & ~(XMEM_PAGE_SIZE_4KB - 1u);
            params.out_base_address = aligned_addr;
            params.out_region_size = run_end - aligned_addr;
            params.out_state = XMEM_MEM_COMMIT;
            params.out_protect = sp.protect;
            params.out_cache_attr = static_cast<uint32_t>(sp.cache_attr);
            params.out_alloc_id = sp.phys_alloc_id;
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
    }

    /* Reserved but uncommitted hole inside reservation */
    const uint64_t aligned_addr = params.query_addr & ~(res->granularity - 1u);
    uint64_t next_bound = res->base_va + res->size_bytes;
    for (const auto &sp : res->mapped_spans) {
        if (sp.va > aligned_addr && sp.va < next_bound) {
            next_bound = sp.va;
        }
    }
    params.out_base_address = aligned_addr;
    params.out_region_size = next_bound - aligned_addr;
    params.out_state = XMEM_MEM_RESERVE;
    params.out_protect = XMEM_PAGE_NOACCESS;
    params.out_alloc_id = res->res_id;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
MemoryManager::query_budget(scarlett_xmem_query_budget_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params.out_total_budget_bytes = budget_bytes_locked();
    params.out_committed_bytes = total_committed_bytes_;
    params.out_graphics_committed_bytes = graphics_committed_bytes_;
    params.out_title_committed_bytes = title_committed_bytes_;
    params.out_active_uma_tier = static_cast<uint32_t>(UmaBridge::instance().active_tier());
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
MemoryManager::working_set_stats(scarlett_xmem_working_set_stats_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t uma_flushes = 0;
    uint64_t uma_pages = 0;
    UmaBridge::instance().get_flush_stats(uma_flushes, uma_pages);

    params.out_total_budget_bytes = budget_bytes_locked();
    params.out_committed_bytes = total_committed_bytes_;
    params.out_reserved_bytes = total_reserved_bytes_;
    params.out_physical_allocated_bytes = physical_allocated_bytes_;
    params.out_physical_mapped_bytes = physical_mapped_bytes_;
    params.out_graphics_committed_bytes = graphics_committed_bytes_;
    params.out_title_committed_bytes = title_committed_bytes_;
    params.out_heap_allocated_bytes = heap_allocated_bytes_;
    params.out_peak_committed_bytes = peak_committed_bytes_;
    params.out_uma_dirty_flushes = uma_flushes;
    params.out_uma_dirty_pages_flushed = uma_pages;
    params.out_active_physical_pages = static_cast<uint32_t>(phys_pages_.size());
    params.out_active_alias_mappings = active_alias_mappings_;
    params.out_active_uma_tier = static_cast<uint32_t>(UmaBridge::instance().active_tier());
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
MemoryManager::title_memory_status(scarlett_xmem_title_mem_status_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    const uint64_t total_budget = budget_bytes_locked();
    uint64_t fast_bytes = 0;
    uint64_t std_bytes = 0;
    model_pools_locked(fast_bytes, std_bytes);

    const uint64_t used = total_committed_bytes_;
    const uint64_t avail = (used < total_budget) ? (total_budget - used) : 0u;
    const uint64_t legacy_used = title_committed_bytes_;
    const uint64_t legacy_avail = (legacy_used < total_budget) ? (total_budget - legacy_used) : 0u;

    params.out_console_model = static_cast<uint32_t>(console_model_);
    params.out_total_mem = total_budget;
    params.out_avail_mem = avail;
    params.out_legacy_used = legacy_used;
    params.out_legacy_peak = peak_legacy_bytes_;
    params.out_legacy_avail = legacy_avail;
    params.out_legacy_limit = total_budget;
    params.out_title_used = used;
    params.out_title_peak = peak_title_bytes_;
    params.out_title_avail = avail;
    params.out_title_limit = total_budget;
    params.out_fast_pool_bytes = fast_bytes;
    params.out_standard_pool_bytes = std_bytes;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::heap_alloc(scarlett_xmem_heap_alloc_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params.out_ptr = 0;
    params.out_actual_size = 0;

    if (params.size_bytes == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    uint32_t align = params.alignment_bytes;
    if (align < XMEM_DEFAULT_HEAP_ALIGNMENT) {
        align = XMEM_DEFAULT_HEAP_ALIGNMENT;
    }
    /* Ensure power-of-two alignment */
    if ((align & (align - 1u)) != 0u) {
        uint32_t p = XMEM_DEFAULT_HEAP_ALIGNMENT;
        while (p < align && p < XMEM_PAGE_SIZE_64KB) {
            p <<= 1;
        }
        align = p;
    }
    if (align > XMEM_PAGE_SIZE_64KB) {
        align = static_cast<uint32_t>(XMEM_PAGE_SIZE_64KB);
    }

    const uint64_t actual_size = align_up_u64(params.size_bytes, align);
    if (actual_size == 0u ||
        total_committed_bytes_ + actual_size > budget_bytes_locked()) {
        params.out_status = SCARLETT_ERR_XMEM_BUDGET_EXCEEDED;
        return params.out_status;
    }

    void *raw_ptr = nullptr;
    if (::posix_memalign(&raw_ptr, static_cast<size_t>(align),
                         static_cast<size_t>(actual_size)) != 0 ||
        raw_ptr == nullptr) {
        params.out_status = SCARLETT_E_OUTOFMEMORY;
        return params.out_status;
    }

    if (scarlett_xmem_tag_zero_init(params.attributes_tag)) {
        std::memset(raw_ptr, 0, static_cast<size_t>(actual_size));
    }

    const uint64_t ptr_val = reinterpret_cast<uint64_t>(raw_ptr);
    HeapAllocation alloc{};
    alloc.base_va = ptr_val;
    alloc.raw_size = actual_size;
    alloc.requested_size = params.size_bytes;
    alloc.tag = params.attributes_tag;
    alloc.alignment = align;

    heap_allocs_.emplace(ptr_val, alloc);
    heap_allocated_bytes_ += actual_size;
    title_committed_bytes_ += actual_size;
    total_committed_bytes_ += actual_size;
    record_peak_locked();

    params.out_ptr = ptr_val;
    params.out_actual_size = actual_size;
    params.alignment_bytes = align;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::heap_free(scarlett_xmem_heap_free_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (params.ptr == 0u) {
        params.out_size_bytes = 0;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    auto it = heap_allocs_.find(params.ptr);
    if (it == heap_allocs_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    const uint64_t raw_size = it->second.raw_size;
    params.out_size_bytes = it->second.requested_size;
    std::free(reinterpret_cast<void *>(it->second.base_va));
    heap_allocs_.erase(it);

    if (heap_allocated_bytes_ >= raw_size) {
        heap_allocated_bytes_ -= raw_size;
    }
    if (title_committed_bytes_ >= raw_size) {
        title_committed_bytes_ -= raw_size;
    }
    if (total_committed_bytes_ >= raw_size) {
        total_committed_bytes_ -= raw_size;
    }

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::heap_size(scarlett_xmem_heap_free_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params.out_size_bytes = 0;
    if (params.ptr == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    auto it = heap_allocs_.find(params.ptr);
    if (it == heap_allocs_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    params.out_size_bytes = it->second.requested_size;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t MemoryManager::inspect_mapped_range(uint64_t va,
                                                       uint64_t size_bytes,
                                                       BackingRangeInfo &out_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    out_info = BackingRangeInfo{};

    if (va == 0u || size_bytes == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    VirtualReservation *res = find_reservation_containing_locked(va);
    if (res == nullptr) {
        return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
    }
    if (va + size_bytes > res->base_va + res->size_bytes) {
        return SCARLETT_ERR_XMEM_CROSS_ALLOCATION;
    }

    uint64_t covered = 0u;
    uint64_t expected_va = va;
    int first_fd = -1;
    uint64_t first_fd_off = 0u;
    uint64_t expected_fd_off = 0u;
    bool contiguous_fd = true;
    bool is_dmabuf = false;
    scarlett_mem_cache_attr_t cache_attr = SCARLETT_MEM_CACHE_WRITEBACK;

    for (const auto &sp : res->mapped_spans) {
        if (!ranges_overlap(va, size_bytes, sp.va, sp.size_bytes)) {
            continue;
        }
        if (sp.va > expected_va) {
            return SCARLETT_E_INVALIDARG; /* Unmapped hole in requested range */
        }
        const uint64_t slice_start = std::max(va, sp.va);
        const uint64_t slice_end = std::min(va + size_bytes, sp.va + sp.size_bytes);
        const uint64_t slice_len = slice_end - slice_start;
        const uint64_t slice_fd_off = sp.fd_offset + (slice_start - sp.va);

        if (covered == 0u) {
            first_fd = sp.backing_fd;
            first_fd_off = slice_fd_off;
            expected_fd_off = slice_fd_off + slice_len;
            is_dmabuf = sp.is_dmabuf;
            cache_attr = sp.cache_attr;
        } else {
            if (sp.backing_fd != first_fd || slice_fd_off != expected_fd_off) {
                contiguous_fd = false;
            }
            expected_fd_off = slice_fd_off + slice_len;
        }

        covered += slice_len;
        expected_va = slice_end;
        if (covered >= size_bytes) {
            break;
        }
    }

    if (covered < size_bytes) {
        return SCARLETT_E_INVALIDARG;
    }

    out_info.backing_fd = first_fd;
    out_info.fd_offset = first_fd_off;
    out_info.mapped_size = size_bytes;
    out_info.region = res->region;
    out_info.cache_attr = cache_attr;
    out_info.is_dmabuf = is_dmabuf;
    out_info.is_contiguous_fd = contiguous_fd;
    return SCARLETT_OK;
}

} // namespace scarlett::xmem::unixlib
