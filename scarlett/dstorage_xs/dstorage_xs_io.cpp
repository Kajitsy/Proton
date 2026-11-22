#include "dstorage_xs_io.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>

#include "xmem/xmem_api.h"

namespace scarlett::dstorage_xs::unixlib {

namespace {

scarlett_hresult_t map_xmem_pfn_auto_cache(
    uint64_t target_va,
    uint64_t pfn_count,
    const uint64_t *pfns) {
    static constexpr scarlett_mem_cache_attr_t kCacheAttrs[] = {
        SCARLETT_MEM_CACHE_WRITEBACK,
        SCARLETT_MEM_CACHE_WRITECOMBINE,
        SCARLETT_MEM_CACHE_GPU_COHERENT,
        SCARLETT_MEM_CACHE_UNCACHED
    };

    scarlett_hresult_t last_hr = SCARLETT_E_FAIL;
    for (scarlett_mem_cache_attr_t attr : kCacheAttrs) {
        scarlett_xmem_map_pages_params_t mp{};
        mp.virtual_addr = target_va;
        mp.page_count = pfn_count;
        mp.page_size_bytes = SCARLETT_PAGE_SIZE_64KB;
        mp.pfn_array_addr = reinterpret_cast<uint64_t>(pfns);
        mp.access = static_cast<uint32_t>(SCARLETT_MEM_ACCESS_READWRITE);
        mp.cache_attr = static_cast<uint32_t>(attr);
        mp.unmap_flag = 0u;
        const int32_t rc = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_MAP_PHYS_PAGES, &mp);
        last_hr = scarlett_failed(rc) ? rc : mp.out_status;
        if (scarlett_succeeded(last_hr)) {
            return SCARLETT_OK;
        }
        if (last_hr != SCARLETT_ERR_XMEM_CACHE_CONFLICT) {
            break;
        }
    }
    return last_hr;
}

} // namespace

IoUringEngine::IoUringEngine() = default;

IoUringEngine::~IoUringEngine() {
    shutdown();
}

scarlett_hresult_t IoUringEngine::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return SCARLETT_OK;
    }

    struct io_uring_params params{};
    params.flags = IORING_SETUP_SQPOLL;
    params.sq_thread_idle = 2000u;

    int rc = ::io_uring_queue_init_params(kRingEntries, &ring_, &params);
    if (rc == 0) {
        io_uring_active_ = true;
        sqpoll_active_ = true;
    } else if (rc == -EPERM || rc == -ENOSYS || rc == -EACCES || rc == -EINVAL) {
        /* Clean fallback to standard io_uring without SQPOLL */
        std::memset(&params, 0, sizeof(params));
        rc = ::io_uring_queue_init_params(kRingEntries, &ring_, &params);
        if (rc == 0) {
            io_uring_active_ = true;
            sqpoll_active_ = false;
        }
    }

    if (io_uring_active_) {
        fixed_pool_.assign(kFixedBufCount * kFixedBufSize, 0u);
        fixed_iovecs_.resize(kFixedBufCount);
        for (size_t i = 0; i < kFixedBufCount; ++i) {
            fixed_iovecs_[i].iov_base = fixed_pool_.data() + i * kFixedBufSize;
            fixed_iovecs_[i].iov_len = kFixedBufSize;
        }
        int reg_rc = ::io_uring_register_buffers(
            &ring_, fixed_iovecs_.data(), static_cast<unsigned>(fixed_iovecs_.size()));
        if (reg_rc == 0) {
            fixed_buffers_active_ = true;
        } else {
            /* Fallback gracefully if RLIMIT_MEMLOCK prevents fixed buffer registration */
            fixed_buffers_active_ = false;
        }
    }

    initialized_ = true;
    return SCARLETT_OK;
}

void IoUringEngine::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }
    if (io_uring_active_) {
        if (fixed_buffers_active_) {
            ::io_uring_unregister_buffers(&ring_);
            fixed_buffers_active_ = false;
        }
        ::io_uring_queue_exit(&ring_);
        io_uring_active_ = false;
        sqpoll_active_ = false;
    }
    fixed_pool_.clear();
    fixed_iovecs_.clear();
    initialized_ = false;
}

scarlett_hresult_t IoUringEngine::read_pread_fallback(
    int fd, uint64_t offset, uint32_t size, uint8_t *dst) {
    uint32_t done = 0;
    while (done < size) {
        const ssize_t n = ::pread(
            fd, dst + done, static_cast<size_t>(size - done),
            static_cast<off_t>(offset + done));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return SCARLETT_ERR_DSTORAGE_IO_URING_FAILED;
        }
        if (n == 0) {
            return SCARLETT_E_DSTORAGE_END_OF_FILE;
        }
        done += static_cast<uint32_t>(n);
    }
    total_bytes_read_.fetch_add(size, std::memory_order_relaxed);
    return SCARLETT_OK;
}

scarlett_hresult_t IoUringEngine::read_chunk_uring_locked(
    int fd, uint64_t offset, uint32_t size, uint8_t *dst) {
    if (size == 0u) {
        return SCARLETT_OK;
    }
    if (!io_uring_active_) {
        return read_pread_fallback(fd, offset, size, dst);
    }

    /* Fast path: small/medium reads <= kFixedBufSize use registered fixed buffers if available */
    if (fixed_buffers_active_ && size <= kFixedBufSize) {
        struct io_uring_sqe *sqe = ::io_uring_get_sqe(&ring_);
        if (sqe != nullptr) {
            ::io_uring_prep_read_fixed(sqe, fd, fixed_iovecs_[0].iov_base, size, offset, 0);
            ::io_uring_sqe_set_data64(sqe, 1u);
            const int sub = ::io_uring_submit_and_wait(&ring_, 1);
            if (sub >= 0) {
                struct io_uring_cqe *cqe = nullptr;
                const int wrc = ::io_uring_wait_cqe(&ring_, &cqe);
                if (wrc == 0 && cqe != nullptr) {
                    const int32_t res = cqe->res;
                    ::io_uring_cqe_seen(&ring_, cqe);
                    if (res == static_cast<int32_t>(size)) {
                        std::memcpy(dst, fixed_iovecs_[0].iov_base, size);
                        total_bytes_read_.fetch_add(size, std::memory_order_relaxed);
                        return SCARLETT_OK;
                    }
                    if (res >= 0 && res < static_cast<int32_t>(size)) {
                        std::memcpy(dst, fixed_iovecs_[0].iov_base, static_cast<size_t>(res));
                        return read_pread_fallback(
                            fd, offset + static_cast<uint64_t>(res),
                            size - static_cast<uint32_t>(res),
                            dst + res);
                    }
                }
            }
        }
    }

    /* Direct io_uring read into destination buffer */
    uint32_t done = 0;
    while (done < size) {
        struct io_uring_sqe *sqe = ::io_uring_get_sqe(&ring_);
        if (sqe == nullptr) {
            return read_pread_fallback(fd, offset + done, size - done, dst + done);
        }
        const uint32_t req_len = size - done;
        ::io_uring_prep_read(sqe, fd, dst + done, req_len, offset + done);
        const int sub = ::io_uring_submit_and_wait(&ring_, 1);
        if (sub < 0) {
            return read_pread_fallback(fd, offset + done, req_len, dst + done);
        }
        struct io_uring_cqe *cqe = nullptr;
        const int wrc = ::io_uring_wait_cqe(&ring_, &cqe);
        if (wrc != 0 || cqe == nullptr) {
            return read_pread_fallback(fd, offset + done, req_len, dst + done);
        }
        const int32_t res = cqe->res;
        ::io_uring_cqe_seen(&ring_, cqe);
        if (res < 0) {
            if (res == -EINTR || res == -EAGAIN) {
                continue;
            }
            return SCARLETT_ERR_DSTORAGE_IO_URING_FAILED;
        }
        if (res == 0) {
            return SCARLETT_E_DSTORAGE_END_OF_FILE;
        }
        done += static_cast<uint32_t>(res);
    }

    total_bytes_read_.fetch_add(size, std::memory_order_relaxed);
    return SCARLETT_OK;
}

scarlett_hresult_t IoUringEngine::readv_uring_locked(
    int fd, uint64_t offset, const struct iovec *iov, unsigned iov_cnt, uint64_t total_bytes) {
    if (total_bytes == 0u || iov_cnt == 0u) {
        return SCARLETT_OK;
    }
    if (!io_uring_active_) {
        uint64_t cur_off = offset;
        for (unsigned i = 0; i < iov_cnt; ++i) {
            scarlett_hresult_t hr = read_pread_fallback(
                fd, cur_off, static_cast<uint32_t>(iov[i].iov_len),
                static_cast<uint8_t *>(iov[i].iov_base));
            if (scarlett_failed(hr)) {
                return hr;
            }
            cur_off += iov[i].iov_len;
        }
        return SCARLETT_OK;
    }

    struct io_uring_sqe *sqe = ::io_uring_get_sqe(&ring_);
    if (sqe != nullptr) {
        ::io_uring_prep_readv(sqe, fd, iov, iov_cnt, offset);
        const int sub = ::io_uring_submit_and_wait(&ring_, 1);
        if (sub >= 0) {
            struct io_uring_cqe *cqe = nullptr;
            const int wrc = ::io_uring_wait_cqe(&ring_, &cqe);
            if (wrc == 0 && cqe != nullptr) {
                const int32_t res = cqe->res;
                ::io_uring_cqe_seen(&ring_, cqe);
                if (res >= 0 && static_cast<uint64_t>(res) == total_bytes) {
                    total_bytes_read_.fetch_add(total_bytes, std::memory_order_relaxed);
                    return SCARLETT_OK;
                }
            }
        }
    }

    uint64_t cur_off = offset;
    for (unsigned i = 0; i < iov_cnt; ++i) {
        scarlett_hresult_t hr = read_chunk_uring_locked(
            fd, cur_off, static_cast<uint32_t>(iov[i].iov_len),
            static_cast<uint8_t *>(iov[i].iov_base));
        if (scarlett_failed(hr)) {
            return hr;
        }
        cur_off += iov[i].iov_len;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t IoUringEngine::read_sync(int fd, uint64_t offset, uint32_t size, uint8_t *dst) {
    if (fd < 0) {
        return SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE;
    }
    if (dst == nullptr && size > 0u) {
        return SCARLETT_E_POINTER;
    }
    if (size == 0u) {
        return SCARLETT_OK;
    }

    if (!initialized_) {
        initialize();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return read_chunk_uring_locked(fd, offset, size, dst);
}

scarlett_hresult_t IoUringEngine::read_batch_sync(const IoBatchItem *items, size_t count) {
    if (count == 0u) {
        return SCARLETT_OK;
    }
    if (items == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (!initialized_) {
        initialize();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!io_uring_active_) {
        for (size_t i = 0; i < count; ++i) {
            scarlett_hresult_t hr = read_pread_fallback(
                items[i].fd, items[i].offset, items[i].size, items[i].dst);
            if (scarlett_failed(hr)) {
                return hr;
            }
        }
        return SCARLETT_OK;
    }

    size_t idx = 0;
    while (idx < count) {
        const size_t batch_end = std::min(count, idx + static_cast<size_t>(kRingEntries / 2u));
        const size_t batch_len = batch_end - idx;

        bool use_fixed = fixed_buffers_active_ && (batch_len <= kFixedBufCount);
        if (use_fixed) {
            for (size_t b = 0; b < batch_len; ++b) {
                if (items[idx + b].size > kFixedBufSize) {
                    use_fixed = false;
                    break;
                }
            }
        }

        unsigned submitted_here = 0;
        for (size_t b = 0; b < batch_len; ++b) {
            const IoBatchItem &it = items[idx + b];
            if (it.size == 0u) {
                continue;
            }
            struct io_uring_sqe *sqe = ::io_uring_get_sqe(&ring_);
            if (sqe == nullptr) {
                break;
            }
            if (use_fixed) {
                ::io_uring_prep_read_fixed(
                    sqe, it.fd, fixed_iovecs_[b].iov_base, it.size, it.offset,
                    static_cast<int>(b));
            } else {
                ::io_uring_prep_read(sqe, it.fd, it.dst, it.size, it.offset);
            }
            ::io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(b));
            ++submitted_here;
        }

        if (submitted_here == 0u) {
            scarlett_hresult_t hr = read_chunk_uring_locked(
                items[idx].fd, items[idx].offset, items[idx].size, items[idx].dst);
            if (scarlett_failed(hr)) {
                return hr;
            }
            ++idx;
            continue;
        }

        const int sub = ::io_uring_submit_and_wait(&ring_, submitted_here);
        if (sub < 0) {
            return SCARLETT_ERR_DSTORAGE_IO_URING_FAILED;
        }

        unsigned seen = 0;
        while (seen < submitted_here) {
            struct io_uring_cqe *cqe = nullptr;
            const int wrc = ::io_uring_wait_cqe(&ring_, &cqe);
            if (wrc != 0 || cqe == nullptr) {
                return SCARLETT_ERR_DSTORAGE_IO_URING_FAILED;
            }
            const uint64_t slot = ::io_uring_cqe_get_data64(cqe);
            const int32_t res = cqe->res;
            ::io_uring_cqe_seen(&ring_, cqe);
            ++seen;

            if (slot >= batch_len) {
                return SCARLETT_ERR_DSTORAGE_IO_URING_FAILED;
            }
            const IoBatchItem &it = items[idx + static_cast<size_t>(slot)];
            if (res < 0) {
                return SCARLETT_ERR_DSTORAGE_IO_URING_FAILED;
            }
            if (use_fixed) {
                std::memcpy(it.dst, fixed_iovecs_[static_cast<size_t>(slot)].iov_base,
                            static_cast<size_t>(res));
            }
            if (static_cast<uint32_t>(res) < it.size) {
                scarlett_hresult_t hr = read_pread_fallback(
                    it.fd, it.offset + static_cast<uint64_t>(res),
                    it.size - static_cast<uint32_t>(res),
                    it.dst + res);
                if (scarlett_failed(hr)) {
                    return hr;
                }
            } else {
                total_bytes_read_.fetch_add(it.size, std::memory_order_relaxed);
            }
        }

        idx += submitted_here;
    }

    return SCARLETT_OK;
}

scarlett_hresult_t IoUringEngine::scatter_to_xmem_physical_pages(
    uint64_t dst_addr,
    uint64_t dst_size,
    const uint8_t *data_ptr,
    int src_fd,
    uint64_t src_offset) {
    if (dst_addr == 0u) {
        return SCARLETT_E_POINTER;
    }
    if (dst_size == 0u) {
        return SCARLETT_OK;
    }

    /* Case 1: dst_addr is already an xmem-mapped MEM_GRAPHICS or MEM_TITLE VA */
    if (scarlett_addr_is_graphics(dst_addr, dst_size) ||
        scarlett_addr_is_title(dst_addr, dst_size)) {
        scarlett_xmem_virtual_query_params_t vq{};
        vq.query_addr = dst_addr;
        const int32_t rc = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_VIRTUAL_QUERY, &vq);
        if (scarlett_succeeded(rc) && scarlett_succeeded(vq.out_status) &&
            vq.out_state == XMEM_MEM_COMMIT &&
            dst_addr + dst_size <= vq.out_base_address + vq.out_region_size) {
            uint8_t *target = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(dst_addr));
            if (data_ptr != nullptr) {
                std::memcpy(target, data_ptr, static_cast<size_t>(dst_size));
                return SCARLETT_OK;
            }
            return read_sync(src_fd, src_offset, static_cast<uint32_t>(dst_size), target);
        }
    }

    /* Case 2: dst_addr is a pointer to an array of 64 KiB xmem PFNs */
    const uint64_t page_size = SCARLETT_PAGE_SIZE_64KB;
    const uint64_t page_count = (dst_size + page_size - 1u) / page_size;
    const uint64_t *pfns = reinterpret_cast<const uint64_t *>(static_cast<uintptr_t>(dst_addr));

    scarlett_xmem_reserve_params_t rp{};
    rp.requested_base = 0u;
    rp.size_bytes = page_count * page_size;
    rp.region = static_cast<uint32_t>(SCARLETT_MEM_REGION_GRAPHICS);
    rp.flags = XMEM_MEM_RESERVE | XMEM_PAGE_READWRITE;
    int32_t rc = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_RESERVE_RANGE, &rp);
    if (scarlett_failed(rc) || scarlett_failed(rp.out_status)) {
        return scarlett_failed(rc) ? rc : rp.out_status;
    }
    const uint64_t scratch_va = rp.out_base;

    auto cleanup_va = [&]() {
        scarlett_xmem_map_pages_params_t ump{};
        ump.virtual_addr = scratch_va;
        ump.page_count = page_count;
        ump.page_size_bytes = page_size;
        ump.unmap_flag = 1u;
        (void)scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_UNMAP_PHYS_PAGES, &ump);

        scarlett_xmem_free_range_params_t fp{};
        fp.base_addr = scratch_va;
        fp.size_bytes = 0u;
        fp.free_type = XMEM_MEM_RELEASE;
        (void)scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_FREE_RANGE, &fp);
    };

    scarlett_hresult_t hr = map_xmem_pfn_auto_cache(scratch_va, page_count, pfns);
    if (hr == SCARLETT_ERR_XMEM_CROSS_ALLOCATION) {
        /* PFNs span multiple physical allocations: map page-by-page cleanly */
        hr = SCARLETT_OK;
        for (uint64_t i = 0; i < page_count; ++i) {
            hr = map_xmem_pfn_auto_cache(scratch_va + i * page_size, 1u, &pfns[i]);
            if (scarlett_failed(hr)) {
                break;
            }
        }
    }
    if (scarlett_failed(hr)) {
        cleanup_va();
        return hr;
    }

    uint8_t *mapped_base = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(scratch_va));
    if (data_ptr != nullptr) {
        std::memcpy(mapped_base, data_ptr, static_cast<size_t>(dst_size));
        hr = SCARLETT_OK;
    } else {
        if (!initialized_) {
            initialize();
        }
        std::vector<struct iovec> iovs(static_cast<size_t>(page_count));
        uint64_t remaining = dst_size;
        for (uint64_t i = 0; i < page_count; ++i) {
            const uint64_t chunk = std::min(page_size, remaining);
            iovs[static_cast<size_t>(i)].iov_base = mapped_base + i * page_size;
            iovs[static_cast<size_t>(i)].iov_len = static_cast<size_t>(chunk);
            remaining -= chunk;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        hr = readv_uring_locked(
            src_fd, src_offset, iovs.data(), static_cast<unsigned>(iovs.size()), dst_size);
    }

    cleanup_va();
    return hr;
}

scarlett_hresult_t IoUringEngine::gather_from_xmem_physical_pages(
    uint64_t src_addr,
    uint64_t src_size,
    uint8_t *out_dst) {
    if (src_addr == 0u || out_dst == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (src_size == 0u) {
        return SCARLETT_OK;
    }

    if (scarlett_addr_is_graphics(src_addr, src_size) ||
        scarlett_addr_is_title(src_addr, src_size)) {
        scarlett_xmem_virtual_query_params_t vq{};
        vq.query_addr = src_addr;
        const int32_t rc = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_VIRTUAL_QUERY, &vq);
        if (scarlett_succeeded(rc) && scarlett_succeeded(vq.out_status) &&
            vq.out_state == XMEM_MEM_COMMIT &&
            src_addr + src_size <= vq.out_base_address + vq.out_region_size) {
            std::memcpy(out_dst,
                        reinterpret_cast<const void *>(static_cast<uintptr_t>(src_addr)),
                        static_cast<size_t>(src_size));
            return SCARLETT_OK;
        }
    }

    const uint64_t page_size = SCARLETT_PAGE_SIZE_64KB;
    const uint64_t page_count = (src_size + page_size - 1u) / page_size;
    const uint64_t *pfns = reinterpret_cast<const uint64_t *>(static_cast<uintptr_t>(src_addr));

    scarlett_xmem_reserve_params_t rp{};
    rp.requested_base = 0u;
    rp.size_bytes = page_count * page_size;
    rp.region = static_cast<uint32_t>(SCARLETT_MEM_REGION_GRAPHICS);
    rp.flags = XMEM_MEM_RESERVE | XMEM_PAGE_READWRITE;
    int32_t rc = scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_RESERVE_RANGE, &rp);
    if (scarlett_failed(rc) || scarlett_failed(rp.out_status)) {
        return scarlett_failed(rc) ? rc : rp.out_status;
    }
    const uint64_t scratch_va = rp.out_base;

    scarlett_hresult_t hr = map_xmem_pfn_auto_cache(scratch_va, page_count, pfns);
    if (hr == SCARLETT_ERR_XMEM_CROSS_ALLOCATION) {
        hr = SCARLETT_OK;
        for (uint64_t i = 0; i < page_count; ++i) {
            hr = map_xmem_pfn_auto_cache(scratch_va + i * page_size, 1u, &pfns[i]);
            if (scarlett_failed(hr)) {
                break;
            }
        }
    }
    if (scarlett_succeeded(hr)) {
        std::memcpy(out_dst,
                    reinterpret_cast<const void *>(static_cast<uintptr_t>(scratch_va)),
                    static_cast<size_t>(src_size));
    }

    scarlett_xmem_map_pages_params_t ump{};
    ump.virtual_addr = scratch_va;
    ump.page_count = page_count;
    ump.page_size_bytes = page_size;
    ump.unmap_flag = 1u;
    (void)scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_UNMAP_PHYS_PAGES, &ump);

    scarlett_xmem_free_range_params_t fp{};
    fp.base_addr = scratch_va;
    fp.size_bytes = 0u;
    fp.free_type = XMEM_MEM_RELEASE;
    (void)scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_FREE_RANGE, &fp);

    return hr;
}

} // namespace scarlett::dstorage_xs::unixlib
