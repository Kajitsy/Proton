#include "arena_scenes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <libdeflate.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "dstorage_xs/dstorage_xs_api.h"
#include "dstorage_xs/dstorage_xs_decomp.hpp"
#include "dstorage_xs/dstorage_xs_shaders.hpp"
#include "xmem/xmem_api.h"

namespace scarlett::arena {

namespace {

uint32_t crc32_ieee(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
            crc = (crc >> 1) ^ mask;
        }
    }
    return ~crc;
}

std::vector<uint8_t> make_asset_pattern(size_t size, uint32_t seed) {
    std::vector<uint8_t> buf(size);
    for (size_t i = 0u; i < size; ++i) {
        const size_t block = i >> 8;
        const size_t intra = i & 0xFFu;
        if ((intra & 0x0Fu) < 12u) {
            buf[i] = static_cast<uint8_t>((block * 7u + seed + (intra & 0x3u)) & 0xFFu);
        } else {
            buf[i] = static_cast<uint8_t>(((i * 131u) ^ (block * 17u) ^ seed) & 0xFFu);
        }
    }
    return buf;
}

bool wait_atomic_u64(const std::atomic<uint64_t>& val, uint64_t target, int timeout_ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (val.load(std::memory_order_acquire) < target) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

} // namespace

bool run_scene3_dstorage_velocity(ArenaTelemetry& tel) {
    SceneChecker sc(tel, tel.s3.checks, "Scene3_DirectStorage");

    struct DsResetGuard {
        ~DsResetGuard() {
            ::unsetenv(SCARLETT_ENV_BCPACK_PLUGIN);
            scarlett_dstorage_reset_runtime();
        }
    } ds_guard;

    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    static const scarlett_guid_t kIFile = SCARLETT_IID_IDSTORAGEFILEX_INIT;
    static const scarlett_guid_t kIStatus = SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT;

    IDStorageFactoryX2* factory = nullptr;
    ARENA_CHECK_HR(sc, DStorageGetFactory(&kIF2, reinterpret_cast<void**>(&factory)), SCARLETT_OK);
    if (factory == nullptr) {
        tel.s3.passed = false;
        return false;
    }

    scarlett_dstorage_caps_params_t caps{};
    ARENA_CHECK_HR(sc, factory->lpVtbl->QueryCapabilities(factory, &caps), SCARLETT_OK);
    ARENA_CHECK(sc, caps.out_io_uring_active == 1u);
    ARENA_CHECK(sc, caps.out_vkdeflate_spirv_valid == 1u);
    ARENA_CHECK(sc, caps.out_swizzle_spirv_valid == 1u);
    tel.s3.io_uring_active = (caps.out_io_uring_active == 1u);
    tel.s3.vkdeflate_spirv_valid = (caps.out_vkdeflate_spirv_valid == 1u);
    tel.s3.swizzle_spirv_valid = (caps.out_swizzle_spirv_valid == 1u);

    /* 1. IBcpackDecoder missing-plugin error & test_bcpack_plugin.so verification */
    {
        DSTORAGE_QUEUE_DESC qd{};
        qd.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        qd.Capacity = 16u;
        qd.Priority = DSTORAGE_PRIORITY_NORMAL;
        IDStorageQueueX2* q = nullptr;
        ARENA_CHECK_HR(sc,
                       factory->lpVtbl->CreateQueue(reinterpret_cast<IDStorageFactoryX*>(factory),
                                                    &qd, &kIQ2, reinterpret_cast<void**>(&q)),
                       SCARLETT_OK);

        IDStorageStatusArrayX* st = nullptr;
        ARENA_CHECK_HR(sc,
                       factory->lpVtbl->CreateStatusArray(reinterpret_cast<IDStorageFactoryX*>(factory),
                                                          4u, "arena_bcp_st", &kIStatus,
                                                          reinterpret_cast<void**>(&st)),
                       SCARLETT_OK);

        constexpr uint32_t kPayloadSize = 4096u;
        constexpr uint16_t kMode = 3u;
        const std::vector<uint8_t> plain = make_asset_pattern(kPayloadSize, 0x9999u);
        std::vector<uint8_t> encoded(sizeof(scarlett_bcpack_header_t) + kPayloadSize);
        const uint8_t key = static_cast<uint8_t>((kMode * 0x3Du) ^ 0xA5u);
        for (uint32_t i = 0u; i < kPayloadSize; ++i) {
            encoded[sizeof(scarlett_bcpack_header_t) + i] = static_cast<uint8_t>(plain[i] ^ key);
        }
        scarlett_bcpack_header_t hdr{};
        hdr.magic = SCARLETT_DSTORAGE_BCPACK_MAGIC;
        hdr.version = 1u;
        hdr.bcpack_mode = kMode;
        hdr.uncompressed_size = kPayloadSize;
        hdr.compressed_payload_size = kPayloadSize;
        hdr.payload_crc32 = crc32_ieee(encoded.data() + sizeof(hdr), kPayloadSize);
        std::memcpy(encoded.data(), &hdr, sizeof(hdr));

        std::vector<uint8_t> dst(kPayloadSize, 0u);
        DSTORAGE_REQUEST req{};
        req.Options.BcpackMode = kMode;
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        req.SourceAddress = reinterpret_cast<uint64_t>(encoded.data());
        req.SourceSize = static_cast<uint32_t>(encoded.size());
        req.DestinationAddress = reinterpret_cast<uint64_t>(dst.data());
        req.DestinationSize = kPayloadSize;

        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX*>(q), &req);
        q->lpVtbl->EnqueueStatus(reinterpret_cast<IDStorageQueueX*>(q), st, 0u);
        q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX*>(q));

        DSTORAGE_ERROR_RECORD err{};
        q->lpVtbl->RetrieveErrorRecord(q, &err);
        ARENA_CHECK(sc, err.FailureCount == 1u);
        ARENA_CHECK(sc, err.FirstFailure.HResult == SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING);
        ARENA_CHECK(sc, st->lpVtbl->IsComplete(st, 0u) == 1u);
        ARENA_CHECK(sc, st->lpVtbl->GetHResult(st, 0u) == SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING);
        tel.s3.bcpack_missing_plugin_rejected =
            (err.FirstFailure.HResult == SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING);

        /* Load clean-room test_bcpack_plugin.so */
        ::setenv(SCARLETT_ENV_BCPACK_PLUGIN, tel.bcpack_plugin_path.c_str(), 1);
        uint32_t plugin_abi = 0u;
        ARENA_CHECK_HR(sc, factory->lpVtbl->LoadBcpackPlugin(factory, nullptr, &plugin_abi), SCARLETT_OK);
        ARENA_CHECK(sc, plugin_abi == SCARLETT_BCPACK_PLUGIN_ABI_VERSION);

        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX*>(q), &req);
        q->lpVtbl->EnqueueStatus(reinterpret_cast<IDStorageQueueX*>(q), st, 1u);
        q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX*>(q));

        DSTORAGE_ERROR_RECORD err2{};
        q->lpVtbl->RetrieveErrorRecord(q, &err2);
        ARENA_CHECK(sc, err2.FailureCount == 0u);
        ARENA_CHECK(sc, st->lpVtbl->GetHResult(st, 1u) == SCARLETT_OK);
        ARENA_CHECK(sc, std::memcmp(dst.data(), plain.data(), kPayloadSize) == 0);

        scarlett_dstorage_caps_params_t bcp_caps{};
        ARENA_CHECK_HR(sc, factory->lpVtbl->QueryCapabilities(factory, &bcp_caps), SCARLETT_OK);
        ARENA_CHECK(sc, bcp_caps.out_bcpack_plugin_loaded == 1u);
        tel.s3.bcpack_plugin_decoded_bytes = bcp_caps.out_bcpack_decoded_bytes;

        st->lpVtbl->Release(st);
        q->lpVtbl->Release(reinterpret_cast<IDStorageQueueX*>(q));
    }

    /* 2. Velocity Swizzle directly into xmem 64 KiB Physical Pages (DestinationIsPhysicalPages=1) */
    {
        constexpr uint64_t kPages = 4u;
        constexpr uint64_t kTotalBytes = kPages * XMEM_PAGE_SIZE_64KB;
        const std::vector<uint8_t> raw_256k = make_asset_pattern(kTotalBytes, 0xBEEFu);
        std::vector<uint8_t> gdeflate_256k;
        ARENA_CHECK_HR(sc,
                       scarlett::dstorage_xs::unixlib::DecompressionPool::encode_gdeflate_tiled(
                           raw_256k.data(), static_cast<uint32_t>(raw_256k.size()),
                           SCARLETT_DSTORAGE_TILE_BYTES, 6, gdeflate_256k),
                       SCARLETT_OK);

        const std::filesystem::path tmp_file =
            std::filesystem::temp_directory_path() /
            ("scarlett_arena_swiz_" + std::to_string(::getpid()) + ".bin");
        {
            FILE* f = std::fopen(tmp_file.c_str(), "wb");
            ARENA_CHECK(sc, f != nullptr);
            if (f != nullptr) {
                std::fwrite(gdeflate_256k.data(), 1, gdeflate_256k.size(), f);
                std::fclose(f);
            }
        }

        IDStorageFileX* ds_file = nullptr;
        ARENA_CHECK_HR(sc,
                       factory->lpVtbl->OpenFileUtf8(
                           reinterpret_cast<IDStorageFactoryX1*>(factory),
                           tmp_file.c_str(), &kIFile, reinterpret_cast<void**>(&ds_file)),
                       SCARLETT_OK);

        DSTORAGE_QUEUE_DESC qd{};
        qd.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        qd.Capacity = 16u;
        qd.Priority = DSTORAGE_PRIORITY_HIGH;
        IDStorageQueueX2* q = nullptr;
        ARENA_CHECK_HR(sc,
                       factory->lpVtbl->CreateQueue(reinterpret_cast<IDStorageFactoryX*>(factory),
                                                    &qd, &kIQ2, reinterpret_cast<void**>(&q)),
                       SCARLETT_OK);

        const std::array<uint32_t, 4> swizzle_modes = {
            DSTORAGE_SWIZZLE_4K_4K_8K,
            DSTORAGE_SWIZZLE_4K_4K_24K,
            DSTORAGE_SWIZZLE_4K_12K_4K_4K_8K,
            DSTORAGE_SWIZZLE_4K_12K_4K_12K,
        };

        bool all_swiz_ok = true;
        uint32_t swiz_verified = 0u;
        for (uint32_t mode : swizzle_modes) {
            std::array<uint64_t, kPages> pfns{};
            uint64_t alloc_id = 0u;
            ARENA_CHECK_HR(sc,
                           XMemAllocatePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB,
                                                     SCARLETT_MEM_CACHE_WRITEBACK,
                                                     XMEM_PHYS_FLAG_GRAPHICS,
                                                     pfns.data(), &alloc_id),
                           SCARLETT_OK);

            DSTORAGE_REQUEST req{};
            req.Options.ZlibDecompress = 1u;
            req.Options.SwizzleMode = mode;
            req.Options.DestinationIsPhysicalPages = 1u;
            req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
            req.FileHandle = ds_file->unix_file_handle;
            req.FileOffset = 0u;
            req.SourceSize = static_cast<uint32_t>(gdeflate_256k.size());
            req.DestinationAddress = reinterpret_cast<uint64_t>(pfns.data());
            req.DestinationSize = static_cast<uint32_t>(kTotalBytes);

            std::atomic<uint64_t> done_fence{0u};
            q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX*>(q), &req);
            q->lpVtbl->EnqueueSignal(reinterpret_cast<IDStorageQueueX*>(q), &done_fence, 1u);
            q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX*>(q));
            ARENA_CHECK(sc, wait_atomic_u64(done_fence, 1u));

            uint64_t verify_va = 0u;
            ARENA_CHECK_HR(sc,
                           XMemVirtualAlloc(0u, kTotalBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                            XMEM_PAGE_READWRITE, &verify_va),
                           SCARLETT_OK);
            ARENA_CHECK_HR(sc,
                           XMemMapPhysicalPages(verify_va, kPages, XMEM_PAGE_SIZE_64KB,
                                                pfns.data(), SCARLETT_MEM_ACCESS_READONLY,
                                                SCARLETT_MEM_CACHE_WRITEBACK),
                           SCARLETT_OK);

            std::vector<uint8_t> deswizzled(kTotalBytes);
            ARENA_CHECK_HR(sc,
                           scarlett::dstorage_xs::unixlib::apply_swizzle_cpu(
                               mode, true,
                               reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(verify_va)),
                               deswizzled.data(), kTotalBytes),
                           SCARLETT_OK);
            const bool crc_ok = (crc32_ieee(deswizzled.data(), kTotalBytes) ==
                                 crc32_ieee(raw_256k.data(), kTotalBytes)) &&
                                (std::memcmp(deswizzled.data(), raw_256k.data(), kTotalBytes) == 0);
            ARENA_CHECK(sc, crc_ok);
            if (!crc_ok) all_swiz_ok = false;
            else ++swiz_verified;

            ARENA_CHECK_HR(sc, XMemUnmapPhysicalPages(verify_va, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
            ARENA_CHECK_HR(sc, XMemVirtualFree(verify_va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
            ARENA_CHECK_HR(sc, XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);
        }
        tel.s3.pfn_swizzle_modes_verified = swiz_verified;
        tel.s3.pfn_swizzle_crc_ok = all_swiz_ok && (swiz_verified == 4u);

        q->lpVtbl->Release(reinterpret_cast<IDStorageQueueX*>(q));
        ds_file->lpVtbl->Release(ds_file);
        std::error_code ec;
        std::filesystem::remove(tmp_file, ec);
    }

    /* 3. 512 MiB Zlib/GDeflate 64 KiB Tiled io_uring + libdeflate Streaming Throughput */
    {
        constexpr uint32_t kReqSize = 32u * 1024u * 1024u;
        constexpr uint32_t kNumReqs = 16u;
        constexpr uint64_t kTotalUncompressed = static_cast<uint64_t>(kReqSize) * kNumReqs;

        const std::vector<uint8_t> chunk_raw = make_asset_pattern(kReqSize, 0xCAFEu);
        std::vector<uint8_t> chunk_gdeflate;
        ARENA_CHECK_HR(sc,
                       scarlett::dstorage_xs::unixlib::DecompressionPool::encode_gdeflate_tiled(
                           chunk_raw.data(), kReqSize, SCARLETT_DSTORAGE_TILE_BYTES, 6, chunk_gdeflate),
                       SCARLETT_OK);
        const uint32_t expected_crc = crc32_ieee(chunk_raw.data(), chunk_raw.size());

        std::string tmp_path = "/dev/shm/scarlett_arena_512m_" + std::to_string(::getpid()) + ".bin";
        {
            int fd = ::open(tmp_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
            if (fd < 0) {
                tmp_path = (std::filesystem::temp_directory_path() /
                            ("scarlett_arena_512m_" + std::to_string(::getpid()) + ".bin")).string();
                fd = ::open(tmp_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
            }
            ARENA_CHECK(sc, fd >= 0);
            if (fd >= 0) {
                for (uint32_t i = 0u; i < kNumReqs; ++i) {
                    size_t off = 0u;
                    while (off < chunk_gdeflate.size()) {
                        ssize_t w = ::write(fd, chunk_gdeflate.data() + off, chunk_gdeflate.size() - off);
                        if (w <= 0) break;
                        off += static_cast<size_t>(w);
                    }
                }
                ::close(fd);
            }
        }

        IDStorageFileX* ds_file = nullptr;
        ARENA_CHECK_HR(sc,
                       factory->lpVtbl->OpenFileUtf8(
                           reinterpret_cast<IDStorageFactoryX1*>(factory),
                           tmp_path.c_str(), &kIFile, reinterpret_cast<void**>(&ds_file)),
                       SCARLETT_OK);

        uint8_t* dst_arena = static_cast<uint8_t*>(
            ::mmap(nullptr, kTotalUncompressed, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0));
        ARENA_CHECK(sc, dst_arena != MAP_FAILED);
        if (dst_arena != MAP_FAILED) {
            ::madvise(dst_arena, kTotalUncompressed, MADV_HUGEPAGE);
            std::memset(dst_arena, 0, kTotalUncompressed);

            std::vector<DSTORAGE_REQUEST> reqs(kNumReqs);
            for (uint32_t i = 0u; i < kNumReqs; ++i) {
                reqs[i].Options.ZlibDecompress = 1u;
                reqs[i].Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
                reqs[i].FileHandle = ds_file->unix_file_handle;
                reqs[i].FileOffset = static_cast<uint64_t>(i) * chunk_gdeflate.size();
                reqs[i].SourceSize = static_cast<uint32_t>(chunk_gdeflate.size());
                reqs[i].DestinationAddress =
                    reinterpret_cast<uint64_t>(dst_arena + static_cast<uint64_t>(i) * kReqSize);
                reqs[i].DestinationSize = kReqSize;
            }

            scarlett_dstorage_submit_batch_params_t bp{};
            bp.requests_addr = reinterpret_cast<uint64_t>(reqs.data());
            bp.request_count = kNumReqs;
            bp.wait_for_completion = 1u;
            ARENA_CHECK_HR(sc, scarlett_dstorage_unix_call(SCARLETT_DSTORAGE_UNIX_SUBMIT_BATCH, &bp),
                           SCARLETT_OK);

            double best_gbps = 0.0;
            for (int run = 0; run < 3; ++run) {
                bp.out_bytes_decompressed = 0u;
                bp.out_elapsed_ns = 0u;
                ARENA_CHECK_HR(sc, scarlett_dstorage_unix_call(SCARLETT_DSTORAGE_UNIX_SUBMIT_BATCH, &bp),
                               SCARLETT_OK);
                ARENA_CHECK(sc, bp.out_bytes_decompressed == kTotalUncompressed);
                const double sec = static_cast<double>(bp.out_elapsed_ns) / 1e9;
                if (sec > 0.0) {
                    const double gbps = (static_cast<double>(bp.out_bytes_decompressed) / 1e9) / sec;
                    best_gbps = std::max(best_gbps, gbps);
                }
            }

            ARENA_CHECK(sc, crc32_ieee(dst_arena, kReqSize) == expected_crc);
            ARENA_CHECK(sc, crc32_ieee(dst_arena + 7ULL * kReqSize, kReqSize) == expected_crc);
            ARENA_CHECK(sc, crc32_ieee(dst_arena + 15ULL * kReqSize, kReqSize) == expected_crc);

            tel.s3.dstorage_gbps = best_gbps;
            ARENA_CHECK(sc, best_gbps >= 4.8);

            ::munmap(dst_arena, kTotalUncompressed);
        }

        if (ds_file != nullptr) {
            ds_file->lpVtbl->Release(ds_file);
        }
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
    }

    tel.s3.passed = sc.ok();
    return tel.s3.passed;
}

} // namespace scarlett::arena
