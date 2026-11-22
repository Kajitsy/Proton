#ifndef SCARLETT_DSTORAGE_XS_DECOMP_HPP
#define SCARLETT_DSTORAGE_XS_DECOMP_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <libdeflate.h>
#include <mutex>
#include <thread>
#include <vector>

#include "dstorage_xs_unix_abi.h"

namespace scarlett::dstorage_xs::unixlib {

struct DecompTileJob {
    const uint8_t *src = nullptr;
    uint32_t src_size = 0;
    uint8_t *dst = nullptr;
    uint32_t dst_size = 0;
    bool is_zlib_wrapper = false;
};

class DecompressionPool {
public:
    DecompressionPool();
    ~DecompressionPool();

    DecompressionPool(const DecompressionPool &) = delete;
    DecompressionPool &operator=(const DecompressionPool &) = delete;

    void initialize(uint32_t thread_count = 0);
    void shutdown();

    uint32_t worker_count() const noexcept { return worker_count_; }

    /* Decompresses a single request payload (GDeflate tiled container, zlib, or raw deflate) */
    scarlett_hresult_t decompress(
        const uint8_t *src,
        uint32_t src_size,
        uint8_t *dst,
        uint32_t dst_size,
        uint64_t *out_written = nullptr);

    /* Parallel decompression of pre-parsed tile jobs across all worker threads */
    scarlett_hresult_t decompress_tiles_parallel(
        const DecompTileJob *jobs,
        size_t job_count);

    /* Helper: encodes a buffer into 64 KiB GDeflate-tiled container for tests/streaming */
    static scarlett_hresult_t encode_gdeflate_tiled(
        const uint8_t *src,
        uint32_t src_size,
        uint32_t tile_size,
        int compression_level,
        std::vector<uint8_t> &out_container);

    uint64_t total_bytes_decompressed() const noexcept {
        return total_bytes_decompressed_.load(std::memory_order_relaxed);
    }

private:
    struct ActiveBatch {
        const DecompTileJob *jobs = nullptr;
        size_t job_count = 0;
        std::atomic<size_t> next_idx{0};
        std::atomic<uint32_t> remaining_workers{0};
        std::atomic<int32_t> first_error{SCARLETT_OK};
    };

    void worker_loop(uint32_t worker_idx);
    void run_batch_slice(uint32_t worker_idx, ActiveBatch &batch);

    std::mutex dispatch_mutex_;
    std::condition_variable cv_start_;
    std::condition_variable cv_done_;
    bool initialized_ = false;
    bool stop_ = false;
    uint32_t worker_count_ = 1;
    uint64_t generation_ = 0;
    ActiveBatch *active_batch_ = nullptr;

    std::vector<std::thread> threads_;
    std::vector<struct libdeflate_decompressor *> decompressors_;
    std::vector<uint32_t> sort_scratch_;
    std::atomic<uint64_t> total_bytes_decompressed_{0};
};

} // namespace scarlett::dstorage_xs::unixlib

#endif /* SCARLETT_DSTORAGE_XS_DECOMP_HPP */
