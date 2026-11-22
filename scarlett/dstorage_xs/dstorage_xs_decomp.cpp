#include "dstorage_xs_decomp.hpp"

#include <algorithm>
#include <cstring>
#include <zlib-ng.h>

namespace scarlett::dstorage_xs::unixlib {

namespace {

scarlett_hresult_t decompress_single_with_libdeflate(
    struct libdeflate_decompressor *decomp,
    const uint8_t *src,
    uint32_t src_size,
    uint8_t *dst,
    uint32_t dst_size,
    bool prefer_zlib,
    uint64_t *out_written) noexcept {
    if (src == nullptr || dst == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (dst_size == 0u) {
        if (out_written) {
            *out_written = 0u;
        }
        return SCARLETT_OK;
    }

    size_t actual_out = 0;
    enum libdeflate_result rc = LIBDEFLATE_BAD_DATA;
    if (prefer_zlib) {
        rc = ::libdeflate_zlib_decompress(decomp, src, src_size, dst, dst_size, &actual_out);
        if (rc != LIBDEFLATE_SUCCESS) {
            rc = ::libdeflate_deflate_decompress(decomp, src, src_size, dst, dst_size, &actual_out);
        }
    } else {
        rc = ::libdeflate_deflate_decompress(decomp, src, src_size, dst, dst_size, &actual_out);
        if (rc != LIBDEFLATE_SUCCESS) {
            rc = ::libdeflate_zlib_decompress(decomp, src, src_size, dst, dst_size, &actual_out);
        }
    }

    if (rc == LIBDEFLATE_SUCCESS && actual_out == dst_size) {
        if (out_written) {
            *out_written = actual_out;
        }
        return SCARLETT_OK;
    }

    /* Fallback to zlib-ng streaming / uncompress */
    size_t zng_dst_len = dst_size;
    size_t zng_src_len = src_size;
    if (::zng_uncompress2(dst, &zng_dst_len, src, &zng_src_len) == Z_OK &&
        zng_dst_len == dst_size) {
        if (out_written) {
            *out_written = zng_dst_len;
        }
        return SCARLETT_OK;
    }

    /* Try raw deflate via zng_inflateInit2(-15) */
    zng_stream strm{};
    strm.next_in = const_cast<uint8_t *>(src);
    strm.avail_in = src_size;
    strm.next_out = dst;
    strm.avail_out = dst_size;
    if (::zng_inflateInit2(&strm, -15) == Z_OK) {
        const int inf_rc = ::zng_inflate(&strm, Z_FINISH);
        const size_t written = strm.total_out;
        ::zng_inflateEnd(&strm);
        if (inf_rc == Z_STREAM_END && written == dst_size) {
            if (out_written) {
                *out_written = written;
            }
            return SCARLETT_OK;
        }
    }

    return SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR;
}

} // namespace

DecompressionPool::DecompressionPool() = default;

DecompressionPool::~DecompressionPool() {
    shutdown();
}

void DecompressionPool::initialize(uint32_t thread_count) {
    std::lock_guard<std::mutex> lock(dispatch_mutex_);
    if (initialized_) {
        return;
    }
    uint32_t hw = std::thread::hardware_concurrency();
    if (hw == 0u) {
        hw = 4u;
    }
    worker_count_ = (thread_count > 0u) ? thread_count : hw;
    stop_ = false;

    /* Worker 0 is the caller thread itself; spawn worker_count_ - 1 background workers */
    decompressors_.resize(worker_count_, nullptr);
    for (uint32_t i = 0; i < worker_count_; ++i) {
        decompressors_[i] = ::libdeflate_alloc_decompressor();
    }

    threads_.reserve(worker_count_ > 1u ? worker_count_ - 1u : 0u);
    for (uint32_t i = 1u; i < worker_count_; ++i) {
        threads_.emplace_back(&DecompressionPool::worker_loop, this, i);
    }

    initialized_ = true;
}

void DecompressionPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        if (!initialized_) {
            return;
        }
        stop_ = true;
        ++generation_;
        cv_start_.notify_all();
    }
    for (auto &t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    threads_.clear();
    for (auto *d : decompressors_) {
        if (d != nullptr) {
            ::libdeflate_free_decompressor(d);
        }
    }
    decompressors_.clear();
    initialized_ = false;
}

void DecompressionPool::run_batch_slice(uint32_t worker_idx, ActiveBatch &batch) {
    struct libdeflate_decompressor *decomp = decompressors_[worker_idx];
    const size_t total = batch.job_count;
    constexpr size_t kGrain = 16u;

    while (true) {
        const size_t start = batch.next_idx.fetch_add(kGrain, std::memory_order_relaxed);
        if (start >= total) {
            break;
        }
        const size_t end = std::min(total, start + kGrain);
        for (size_t i = start; i < end; ++i) {
            const DecompTileJob &job = batch.jobs[i];
            const scarlett_hresult_t hr = decompress_single_with_libdeflate(
                decomp, job.src, job.src_size, job.dst, job.dst_size,
                job.is_zlib_wrapper, nullptr);
            if (scarlett_failed(hr)) {
                int32_t expected = SCARLETT_OK;
                batch.first_error.compare_exchange_strong(
                    expected, hr, std::memory_order_acq_rel);
            }
        }
    }
}

void DecompressionPool::worker_loop(uint32_t worker_idx) {
    uint64_t seen_gen = 0;
    while (true) {
        ActiveBatch *batch_ptr = nullptr;
        {
            std::unique_lock<std::mutex> lock(dispatch_mutex_);
            cv_start_.wait(lock, [&]() { return stop_ || generation_ != seen_gen; });
            if (stop_) {
                return;
            }
            seen_gen = generation_;
            batch_ptr = active_batch_;
        }

        if (batch_ptr != nullptr) {
            run_batch_slice(worker_idx, *batch_ptr);
            if (batch_ptr->remaining_workers.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
                std::lock_guard<std::mutex> lock(dispatch_mutex_);
                cv_done_.notify_one();
            }
        }
    }
}

scarlett_hresult_t DecompressionPool::decompress_tiles_parallel(
    const DecompTileJob *jobs,
    size_t job_count) {
    if (job_count == 0u) {
        return SCARLETT_OK;
    }
    if (jobs == nullptr) {
        return SCARLETT_E_POINTER;
    }
    std::lock_guard<std::mutex> call_lock(call_mutex_);
    if (!initialized_) {
        initialize();
    }

    if (job_count < 4u || worker_count_ <= 1u) {
        for (size_t i = 0; i < job_count; ++i) {
            const DecompTileJob &job = jobs[i];
            const scarlett_hresult_t hr = decompress_single_with_libdeflate(
                decompressors_[0], job.src, job.src_size, job.dst, job.dst_size,
                job.is_zlib_wrapper, nullptr);
            if (scarlett_failed(hr)) {
                return hr;
            }
        }
        uint64_t bytes = 0;
        for (size_t i = 0; i < job_count; ++i) {
            bytes += jobs[i].dst_size;
        }
        total_bytes_decompressed_.fetch_add(bytes, std::memory_order_relaxed);
        return SCARLETT_OK;
    }

    ActiveBatch batch{};
    batch.jobs = jobs;
    batch.job_count = job_count;
    batch.next_idx.store(0u, std::memory_order_relaxed);
    batch.remaining_workers.store(worker_count_, std::memory_order_release);
    batch.first_error.store(SCARLETT_OK, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        active_batch_ = &batch;
        ++generation_;
        cv_start_.notify_all();
    }

    /* Caller participates as worker 0 */
    run_batch_slice(0u, batch);
    if (batch.remaining_workers.fetch_sub(1u, std::memory_order_acq_rel) > 1u) {
        std::unique_lock<std::mutex> lock(dispatch_mutex_);
        cv_done_.wait(lock, [&]() {
            return batch.remaining_workers.load(std::memory_order_acquire) == 0u;
        });
    }

    {
        std::lock_guard<std::mutex> lock(dispatch_mutex_);
        active_batch_ = nullptr;
    }

    const int32_t err = batch.first_error.load(std::memory_order_acquire);
    if (scarlett_failed(err)) {
        return err;
    }

    uint64_t bytes = 0;
    for (size_t i = 0; i < job_count; ++i) {
        bytes += jobs[i].dst_size;
    }
    total_bytes_decompressed_.fetch_add(bytes, std::memory_order_relaxed);
    return SCARLETT_OK;
}

scarlett_hresult_t DecompressionPool::decompress(
    const uint8_t *src,
    uint32_t src_size,
    uint8_t *dst,
    uint32_t dst_size,
    uint64_t *out_written) {
    if (src == nullptr || dst == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (!initialized_) {
        initialize();
    }

    /* Check for Scarlett 64 KiB Tiled GDeflate container ("SGD1") */
    if (src_size >= sizeof(scarlett_gdeflate_header_t)) {
        scarlett_gdeflate_header_t hdr{};
        std::memcpy(&hdr, src, sizeof(hdr));
        if (hdr.magic == SCARLETT_DSTORAGE_GDEFLATE_MAGIC) {
            if (hdr.tile_count == 0u ||
                hdr.total_uncompressed_bytes > dst_size ||
                src_size < sizeof(hdr) + static_cast<size_t>(hdr.tile_count) * sizeof(scarlett_gdeflate_tile_desc_t)) {
                return SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR;
            }

            const auto *descs = reinterpret_cast<const scarlett_gdeflate_tile_desc_t *>(src + sizeof(hdr));
            std::vector<DecompTileJob> jobs(hdr.tile_count);
            for (uint32_t i = 0; i < hdr.tile_count; ++i) {
                const scarlett_gdeflate_tile_desc_t &d = descs[i];
                if (static_cast<uint64_t>(d.compressed_offset) + d.compressed_size > src_size ||
                    static_cast<uint64_t>(d.uncompressed_offset) + d.uncompressed_size > dst_size) {
                    return SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR;
                }
                jobs[i].src = src + d.compressed_offset;
                jobs[i].src_size = d.compressed_size;
                jobs[i].dst = dst + d.uncompressed_offset;
                jobs[i].dst_size = d.uncompressed_size;
                jobs[i].is_zlib_wrapper = false;
            }

            /* Sort tiles by compressed size (descending) to minimize warp/thread divergence */
            std::sort(jobs.begin(), jobs.end(), [](const DecompTileJob &a, const DecompTileJob &b) {
                return a.src_size > b.src_size;
            });

            scarlett_hresult_t hr = decompress_tiles_parallel(jobs.data(), jobs.size());
            if (scarlett_succeeded(hr) && out_written != nullptr) {
                *out_written = hdr.total_uncompressed_bytes;
            }
            return hr;
        }
    }

    /* Single-stream zlib / raw deflate (< 64 KiB or >= 64 KiB single stream) */
    const bool looks_zlib = (src_size >= 2u && (src[0] & 0x0Fu) == 0x08u);
    uint64_t written = 0;
    scarlett_hresult_t hr = SCARLETT_OK;
    {
        std::lock_guard<std::mutex> call_lock(call_mutex_);
        if (!initialized_) {
            initialize();
        }
        hr = decompress_single_with_libdeflate(
            decompressors_[0], src, src_size, dst, dst_size, looks_zlib, &written);
    }
    if (scarlett_succeeded(hr)) {
        total_bytes_decompressed_.fetch_add(written, std::memory_order_relaxed);
        if (out_written != nullptr) {
            *out_written = written;
        }
    }
    return hr;
}

scarlett_hresult_t DecompressionPool::encode_gdeflate_tiled(
    const uint8_t *src,
    uint32_t src_size,
    uint32_t tile_size,
    int compression_level,
    std::vector<uint8_t> &out_container) {
    if (src == nullptr && src_size > 0u) {
        return SCARLETT_E_POINTER;
    }
    if (tile_size == 0u) {
        tile_size = SCARLETT_DSTORAGE_TILE_BYTES;
    }

    const uint32_t tile_count = (src_size == 0u) ? 1u : ((src_size + tile_size - 1u) / tile_size);
    const size_t header_bytes = sizeof(scarlett_gdeflate_header_t) +
                                static_cast<size_t>(tile_count) * sizeof(scarlett_gdeflate_tile_desc_t);

    struct libdeflate_compressor *comp = ::libdeflate_alloc_compressor(compression_level);
    if (comp == nullptr) {
        return SCARLETT_E_OUTOFMEMORY;
    }

    std::vector<scarlett_gdeflate_tile_desc_t> descs(tile_count);
    std::vector<uint8_t> payload;
    payload.reserve(src_size);

    for (uint32_t i = 0; i < tile_count; ++i) {
        const uint32_t u_off = i * tile_size;
        const uint32_t u_len = (src_size > u_off) ? std::min(tile_size, src_size - u_off) : 0u;
        const size_t max_comp = ::libdeflate_deflate_compress_bound(comp, u_len);
        const size_t old_sz = payload.size();
        payload.resize(old_sz + max_comp);
        const size_t c_len = ::libdeflate_deflate_compress(
            comp, src + u_off, u_len, payload.data() + old_sz, max_comp);
        if (c_len == 0u && u_len > 0u) {
            ::libdeflate_free_compressor(comp);
            return SCARLETT_E_FAIL;
        }
        payload.resize(old_sz + c_len);
        descs[i].compressed_offset = static_cast<uint32_t>(header_bytes + old_sz);
        descs[i].compressed_size = static_cast<uint32_t>(c_len);
        descs[i].uncompressed_offset = u_off;
        descs[i].uncompressed_size = u_len;
    }
    ::libdeflate_free_compressor(comp);

    scarlett_gdeflate_header_t hdr{};
    hdr.magic = SCARLETT_DSTORAGE_GDEFLATE_MAGIC;
    hdr.tile_count = tile_count;
    hdr.tile_size = tile_size;
    hdr.total_uncompressed_bytes = src_size;

    out_container.resize(header_bytes + payload.size());
    std::memcpy(out_container.data(), &hdr, sizeof(hdr));
    std::memcpy(out_container.data() + sizeof(hdr), descs.data(),
                descs.size() * sizeof(scarlett_gdeflate_tile_desc_t));
    if (!payload.empty()) {
        std::memcpy(out_container.data() + header_bytes, payload.data(), payload.size());
    }
    return SCARLETT_OK;
}

} // namespace scarlett::dstorage_xs::unixlib
