#ifndef SCARLETT_DSTORAGE_XS_IO_HPP
#define SCARLETT_DSTORAGE_XS_IO_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <liburing.h>
#include <mutex>
#include <vector>

#include "dstorage_xs_unix_abi.h"

namespace scarlett::dstorage_xs::unixlib {

struct IoBatchItem {
    int fd = -1;
    uint64_t offset = 0;
    uint32_t size = 0;
    uint8_t *dst = nullptr;
};

class IoUringEngine {
public:
    IoUringEngine();
    ~IoUringEngine();

    IoUringEngine(const IoUringEngine &) = delete;
    IoUringEngine &operator=(const IoUringEngine &) = delete;

    scarlett_hresult_t initialize();
    void shutdown();

    bool is_io_uring_active() const noexcept { return io_uring_active_; }
    bool is_sqpoll_active() const noexcept { return sqpoll_active_; }
    bool is_fixed_buffers_active() const noexcept { return fixed_buffers_active_; }

    /* Reads [offset, offset + size) from fd into dst via io_uring (fixed-buf or direct) */
    scarlett_hresult_t read_sync(int fd, uint64_t offset, uint32_t size, uint8_t *dst);

    /* Submits a batch of reads in minimal io_uring_submit_and_wait syscalls */
    scarlett_hresult_t read_batch_sync(const IoBatchItem *items, size_t count);

    /*
     * Scatters data into xmem 64 KiB physical pages (DestinationIsPhysicalPages=1).
     * Supports both:
     *   - dst_addr inside MEM_GRAPHICS / MEM_TITLE (mapped xmem VA)
     *   - dst_addr pointing to a uint64_t pfn_array[ceil(dst_size / 65536)]
     * If data_ptr != nullptr, writes data_ptr[0..dst_size) into the 64 KiB pages;
     * otherwise reads directly from (src_fd, src_offset) via io_uring_prep_readv!
     */
    scarlett_hresult_t scatter_to_xmem_physical_pages(
        uint64_t dst_addr,
        uint64_t dst_size,
        const uint8_t *data_ptr,
        int src_fd,
        uint64_t src_offset);

    /* Reads from xmem 64 KiB physical pages (SourceIsPhysicalPages=1) into linear buffer */
    scarlett_hresult_t gather_from_xmem_physical_pages(
        uint64_t src_addr,
        uint64_t src_size,
        uint8_t *out_dst);

    uint64_t total_bytes_read() const noexcept {
        return total_bytes_read_.load(std::memory_order_relaxed);
    }

private:
    scarlett_hresult_t read_chunk_uring_locked(int fd, uint64_t offset, uint32_t size, uint8_t *dst);
    scarlett_hresult_t readv_uring_locked(int fd, uint64_t offset, const struct iovec *iov, unsigned iov_cnt, uint64_t total_bytes);
    scarlett_hresult_t read_pread_fallback(int fd, uint64_t offset, uint32_t size, uint8_t *dst);

    std::mutex mutex_;
    struct io_uring ring_{};
    bool initialized_ = false;
    bool io_uring_active_ = false;
    bool sqpoll_active_ = false;
    bool fixed_buffers_active_ = false;

    static constexpr unsigned kRingEntries = 256u;
    static constexpr size_t kFixedBufCount = 64u;
    static constexpr size_t kFixedBufSize = 65536u; /* 64 x 64 KiB = 4 MiB (< 8 MiB RLIMIT_MEMLOCK) */

    std::vector<uint8_t> fixed_pool_;
    std::vector<struct iovec> fixed_iovecs_;
    std::atomic<uint64_t> total_bytes_read_{0};
};

} // namespace scarlett::dstorage_xs::unixlib

#endif /* SCARLETT_DSTORAGE_XS_IO_HPP */
