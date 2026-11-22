#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <libdeflate.h>

#include "dstorage_xs_api.h"
#include "dstorage_xs_decomp.hpp"
#include "dstorage_xs_shaders.hpp"
#include "xmem/xmem_api.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " ("        \
                      << #cond << ")\n";                                         \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define REQUIRE_HR(expr, expected)                                               \
    do {                                                                         \
        scarlett_hresult_t _hr = (expr);                                         \
        if (_hr != (expected)) {                                                 \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " "         \
                      << #expr << " returned " << scarlett_error_name(_hr)       \
                      << " (0x" << std::hex << static_cast<uint32_t>(_hr)        \
                      << ") expected 0x" << static_cast<uint32_t>(expected)      \
                      << std::dec << "\n";                                       \
            ++g_failures;                                                        \
            return false;                                                        \
        }                                                                        \
    } while (0)

uint32_t crc32_ieee(const uint8_t *data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
            crc = (crc >> 1) ^ mask;
        }
    }
    return ~crc;
}

std::vector<uint8_t> make_compressible_pattern(size_t size, uint32_t seed) {
    std::vector<uint8_t> buf(size);
    /* Realistic game-asset texture/mesh data (~2.5-3.5x compressible) */
    for (size_t i = 0; i < size; ++i) {
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

bool wait_for_atomic_u64(const std::atomic<uint64_t> &val, uint64_t target, int timeout_ms = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (val.load(std::memory_order_acquire) < target) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

struct StubCapture {
    uint32_t hits = 0;
    std::string last_symbol;
    scarlett_stub_action_t action = SCARLETT_STUB_ACTION_LOG_AND_FAIL;
};

scarlett_stub_action_t test_stub_sink(const scarlett_stub_context_t *ctx, void *user_data) {
    auto *cap = static_cast<StubCapture *>(user_data);
    ++cap->hits;
    if (ctx != nullptr) {
        cap->last_symbol = ctx->symbol_name;
    }
    return cap->action;
}

/* ============================================================================
 * 1. Bit-Exact DSTORAGE_REQUEST_OPTIONS & DSTORAGE_REQUEST + Unixlib ABI
 * ============================================================================ */
bool test_1_options_layout_and_unixlib_table() {
    DSTORAGE_REQUEST_OPTIONS opt{};
    opt.AsUInt64 = 0u;
    opt.ZlibDecompress = 1u;
    CHECK(opt.AsUInt64 == (UINT64_C(1) << 0));

    opt.AsUInt64 = 0u;
    opt.BcpackMode = 0xAu;
    CHECK(opt.AsUInt64 == (UINT64_C(0xA) << 1));

    opt.AsUInt64 = 0u;
    opt.SwizzleMode = 0x1234u;
    CHECK(opt.AsUInt64 == (UINT64_C(0x1234) << 5));

    opt.AsUInt64 = 0u;
    opt.DestinationIsPhysicalPages = 1u;
    CHECK(opt.AsUInt64 == (UINT64_C(1) << 21));

    opt.AsUInt64 = 0u;
    opt.SourceType = 1u;
    CHECK(opt.AsUInt64 == (UINT64_C(1) << 22));

    opt.AsUInt64 = 0u;
    opt.SourceIsPhysicalPages = 1u;
    CHECK(opt.AsUInt64 == (UINT64_C(1) << 23));

    opt.AsUInt64 = 0u;
    opt.Reserved = 1u;
    CHECK(opt.AsUInt64 == (UINT64_C(1) << 24));

    const scarlett_dstorage_request_options_t packed = scarlett_dstorage_pack_options(
        true, 5u, DSTORAGE_SWIZZLE_4K_12K_4K_12K, true, DSTORAGE_REQUEST_SOURCE_MEMORY, true);
    DSTORAGE_REQUEST_OPTIONS u{};
    u.AsUInt64 = packed.raw;
    CHECK(u.ZlibDecompress == 1u);
    CHECK(u.BcpackMode == 5u);
    CHECK(u.SwizzleMode == DSTORAGE_SWIZZLE_4K_12K_4K_12K);
    CHECK(u.DestinationIsPhysicalPages == 1u);
    CHECK(u.SourceType == DSTORAGE_REQUEST_SOURCE_MEMORY);
    CHECK(u.SourceIsPhysicalPages == 1u);
    CHECK(u.Reserved == 0u);

    /* Verify real dstorage_xs_unix.so exports dense 23-slot __wine_unix_call_funcs */
    void *so = ::dlopen(SCARLETT_DSTORAGEXS_UNIX_SO_PATH, RTLD_NOW | RTLD_LOCAL);
    CHECK(so != nullptr);
    if (so != nullptr) {
        using wine_entry_t = int32_t (*)(void *);
        auto *tbl = reinterpret_cast<const wine_entry_t *>(
            ::dlsym(so, "__wine_unix_call_funcs"));
        CHECK(tbl != nullptr);
        if (tbl != nullptr) {
            for (uint32_t i = 0; i < SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT; ++i) {
                CHECK(tbl[i] != nullptr);
            }
            scarlett_unix_init_params_t bad{};
            bad.abi_version = 999u;
            bad.module_id = SCARLETT_UNIX_MOD_DSTORAGE;
            CHECK(tbl[SCARLETT_DSTORAGE_UNIX_INIT](&bad) == SCARLETT_ERR_ABI_VERSION_MISMATCH);

            scarlett_unix_init_params_t ok{};
            ok.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
            ok.module_id = SCARLETT_UNIX_MOD_DSTORAGE;
            ok.console_model = SCARLETT_MODEL_ANACONDA;
            CHECK(tbl[SCARLETT_DSTORAGE_UNIX_INIT](&ok) == SCARLETT_OK);
        }
        ::dlclose(so);
    }
    return true;
}

/* ============================================================================
 * 2. Queue FIFO, Auto-Submit at 50%, Priorities (~10x), Limits, Cancel, Stubs
 * ============================================================================ */
bool test_2_queue_semantics_fifo_autosubmit_priority_cancel_limits() {
    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    static const scarlett_guid_t kIStatus = SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT;

    IDStorageFactoryX2 *factory = nullptr;
    REQUIRE_HR(DStorageGetFactory(&kIF2, reinterpret_cast<void **>(&factory)), SCARLETT_OK);
    CHECK(factory != nullptr);

    /* Staging buffer 1 GiB limit */
    REQUIRE_HR(factory->lpVtbl->SetStagingBufferSize(
                   reinterpret_cast<IDStorageFactoryX *>(factory), 64u * 1024u * 1024u),
               SCARLETT_OK);
    CHECK(factory->lpVtbl->GetStagingBufferSize(
              reinterpret_cast<IDStorageFactoryX1 *>(factory)) == 64u * 1024u * 1024u);
    REQUIRE_HR(factory->lpVtbl->SetStagingBufferSize(
                   reinterpret_cast<IDStorageFactoryX *>(factory),
                   static_cast<uint32_t>(SCARLETT_DSTORAGE_MAX_STAGING_BYTES)),
               SCARLETT_OK);
    CHECK(factory->lpVtbl->SetStagingBufferSize(
              reinterpret_cast<IDStorageFactoryX *>(factory),
              static_cast<uint32_t>(SCARLETT_DSTORAGE_MAX_STAGING_BYTES + 4096u)) ==
          SCARLETT_E_INVALIDARG);

    /* Auto-submit at 50% capacity (Capacity = 16 -> triggers on 8th request) */
    DSTORAGE_QUEUE_DESC qdesc{};
    qdesc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    qdesc.Capacity = 16u;
    qdesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    qdesc.Name = "auto_submit_q";

    IDStorageQueueX2 *q = nullptr;
    REQUIRE_HR(factory->lpVtbl->CreateQueue(
                   reinterpret_cast<IDStorageFactoryX *>(factory),
                   &qdesc, &kIQ2, reinterpret_cast<void **>(&q)),
               SCARLETT_OK);

    IDStorageStatusArrayX *status_arr = nullptr;
    REQUIRE_HR(factory->lpVtbl->CreateStatusArray(
                   reinterpret_cast<IDStorageFactoryX *>(factory),
                   8u, "status", &kIStatus, reinterpret_cast<void **>(&status_arr)),
               SCARLETT_OK);

    std::vector<uint8_t> src(8 * 4096, 0u);
    std::vector<uint8_t> dst(8 * 4096, 0u);
    for (uint32_t i = 0; i < 8u; ++i) {
        std::memset(src.data() + i * 4096, static_cast<int>(0x10 + i), 4096);
    }

    for (uint32_t i = 0; i < 7u; ++i) {
        DSTORAGE_REQUEST req{};
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        req.SourceAddress = reinterpret_cast<uint64_t>(src.data() + i * 4096);
        req.SourceSize = 4096u;
        req.DestinationAddress = reinterpret_cast<uint64_t>(dst.data() + i * 4096);
        req.DestinationSize = 4096u;
        req.CancellationTag = 0x100u + i;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &req);
    }
    q->lpVtbl->EnqueueStatus(reinterpret_cast<IDStorageQueueX *>(q), status_arr, 0u);

    DSTORAGE_QUEUE_INFO qinfo{};
    REQUIRE_HR(q->lpVtbl->Query(reinterpret_cast<IDStorageQueueX *>(q), &qinfo), SCARLETT_OK);
    CHECK(qinfo.PendingRequestCount == 7u);
    CHECK(qinfo.RequestCountUntilAutoSubmit == 1u);
    CHECK(qinfo.AutoSubmitCount == 0u);
    CHECK(status_arr->lpVtbl->IsComplete(status_arr, 0u) == 0u);

    /* 8th request triggers auto-submit at 50% of Capacity=16 */
    {
        DSTORAGE_REQUEST req{};
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        req.SourceAddress = reinterpret_cast<uint64_t>(src.data() + 7u * 4096);
        req.SourceSize = 4096u;
        req.DestinationAddress = reinterpret_cast<uint64_t>(dst.data() + 7u * 4096);
        req.DestinationSize = 4096u;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &req);
    }
    q->lpVtbl->EnqueueStatus(reinterpret_cast<IDStorageQueueX *>(q), status_arr, 1u);
    q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));

    std::atomic<uint64_t> fence{0};
    q->lpVtbl->EnqueueSignal(reinterpret_cast<IDStorageQueueX *>(q), &fence, 42u);
    q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));
    CHECK(wait_for_atomic_u64(fence, 42u));
    CHECK(status_arr->lpVtbl->IsComplete(status_arr, 1u) == 1u);
    CHECK(status_arr->lpVtbl->GetHResult(status_arr, 1u) == SCARLETT_OK);

    REQUIRE_HR(q->lpVtbl->Query(reinterpret_cast<IDStorageQueueX *>(q), &qinfo), SCARLETT_OK);
    CHECK(qinfo.AutoSubmitCount == 1u);
    CHECK(std::memcmp(src.data(), dst.data(), src.size()) == 0);

    /* Strict FIFO within same priority: 32 overlapping memory writes to same target */
    std::array<uint8_t, 256> fifo_dst{};
    std::vector<std::array<uint8_t, 256>> fifo_srcs(32);
    for (uint32_t i = 0; i < 32u; ++i) {
        fifo_srcs[i].fill(static_cast<uint8_t>(i + 1u));
        DSTORAGE_REQUEST req{};
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        req.SourceAddress = reinterpret_cast<uint64_t>(fifo_srcs[i].data());
        req.SourceSize = 256u;
        req.DestinationAddress = reinterpret_cast<uint64_t>(fifo_dst.data());
        req.DestinationSize = 256u;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &req);
    }
    std::atomic<uint64_t> fifo_fence{0};
    q->lpVtbl->EnqueueSignal(reinterpret_cast<IDStorageQueueX *>(q), &fifo_fence, 1u);
    q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));
    CHECK(wait_for_atomic_u64(fifo_fence, 1u));
    CHECK(fifo_dst[0] == 32u && fifo_dst[255] == 32u);

    /* CancelRequestsWithTag */
    std::array<uint8_t, 256> cancel_dst_a{};
    std::array<uint8_t, 256> cancel_dst_b{};
    std::array<uint8_t, 256> cancel_src{};
    cancel_src.fill(0xEEu);
    {
        DSTORAGE_REQUEST r1{};
        r1.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        r1.SourceAddress = reinterpret_cast<uint64_t>(cancel_src.data());
        r1.SourceSize = 256u;
        r1.DestinationAddress = reinterpret_cast<uint64_t>(cancel_dst_a.data());
        r1.DestinationSize = 256u;
        r1.CancellationTag = 0xDEAD0001u;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &r1);

        DSTORAGE_REQUEST r2 = r1;
        r2.DestinationAddress = reinterpret_cast<uint64_t>(cancel_dst_b.data());
        r2.CancellationTag = 0xBEEF0002u;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &r2);

        q->lpVtbl->CancelRequestsWithTag(
            reinterpret_cast<IDStorageQueueX *>(q), 0xFFFF0000u, 0xDEAD0000u);
    }
    std::atomic<uint64_t> cancel_fence{0};
    q->lpVtbl->EnqueueSignal(reinterpret_cast<IDStorageQueueX *>(q), &cancel_fence, 1u);
    q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));
    CHECK(wait_for_atomic_u64(cancel_fence, 1u));
    CHECK(cancel_dst_a[0] == 0x00u);
    CHECK(cancel_dst_b[0] == 0xEEu);

    /* 32 MiB request limit & reserved options validation */
    {
        DSTORAGE_REQUEST bad{};
        bad.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        bad.Options.Reserved = 1u;
        bad.SourceAddress = reinterpret_cast<uint64_t>(cancel_src.data());
        bad.SourceSize = 256u;
        bad.DestinationAddress = reinterpret_cast<uint64_t>(cancel_dst_a.data());
        bad.DestinationSize = 256u;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &bad);
        q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));

        DSTORAGE_ERROR_RECORD err{};
        q->lpVtbl->RetrieveErrorRecord(q, &err);
        CHECK(err.FailureCount >= 1u);
        CHECK(err.FirstFailure.HResult == SCARLETT_ERR_DSTORAGE_INVALID_OPTIONS);

        DSTORAGE_REQUEST big{};
        big.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        big.SourceAddress = reinterpret_cast<uint64_t>(cancel_src.data());
        big.SourceSize = SCARLETT_DSTORAGE_MAX_REQUEST_BYTES + 1u;
        big.DestinationAddress = reinterpret_cast<uint64_t>(cancel_dst_a.data());
        big.DestinationSize = SCARLETT_DSTORAGE_MAX_REQUEST_BYTES;
        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &big);
        q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));

        err = DSTORAGE_ERROR_RECORD{};
        q->lpVtbl->RetrieveErrorRecord(q, &err);
        CHECK(err.FailureCount >= 1u);
        CHECK(err.FirstFailure.HResult == SCARLETT_E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE);
    }

    /* Weighted priority step (~10x): LOW vs NORMAL vs HIGH vs REALTIME */
    {
        constexpr uint32_t kItemsPerTier = 1000u;
        std::array<IDStorageQueueX2 *, 4> pqs{};
        std::array<std::atomic<uint32_t>, 4> done_ev{};
        for (uint32_t p = 0; p < 4u; ++p) {
            DSTORAGE_QUEUE_DESC pd{};
            pd.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
            pd.Capacity = 2048u;
            pd.Priority = p;
            REQUIRE_HR(factory->lpVtbl->CreateQueue(
                           reinterpret_cast<IDStorageFactoryX *>(factory),
                           &pd, &kIQ2, reinterpret_cast<void **>(&pqs[p])),
                       SCARLETT_OK);
        }

        std::vector<uint8_t> psrc(4096, 0x5Au);
        std::vector<uint8_t> pdst(4 * 4096, 0u);
        std::array<std::chrono::steady_clock::time_point, 4> t_done{};

        /* Enqueue all 4 tiers BEFORE priming/submitting so Submit() takes microseconds */
        for (uint32_t p = 0; p < 4u; ++p) {
            for (uint32_t i = 0; i < kItemsPerTier; ++i) {
                DSTORAGE_REQUEST r{};
                r.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
                r.SourceAddress = reinterpret_cast<uint64_t>(psrc.data());
                r.SourceSize = 4096u;
                r.DestinationAddress = reinterpret_cast<uint64_t>(pdst.data() + p * 4096);
                r.DestinationSize = 4096u;
                pqs[p]->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(pqs[p]), &r);
            }
            pqs[p]->lpVtbl->EnqueueSetEvent(
                reinterpret_cast<IDStorageQueueX1 *>(pqs[p]), &done_ev[p]);
        }

        /* Prime scheduler with two 32 MiB GDeflate requests on a separate queue so all 4 Submit() calls overlap */
        const std::vector<uint8_t> prime_raw = make_compressible_pattern(32u * 1024u * 1024u, 0x42u);
        std::vector<uint8_t> prime_comp;
        REQUIRE_HR(scarlett::dstorage_xs::unixlib::DecompressionPool::encode_gdeflate_tiled(
                       prime_raw.data(), static_cast<uint32_t>(prime_raw.size()),
                       SCARLETT_DSTORAGE_TILE_BYTES, 6, prime_comp),
                   SCARLETT_OK);
        std::vector<uint8_t> prime_dst(prime_raw.size(), 0u);
        DSTORAGE_QUEUE_DESC prime_desc{};
        prime_desc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        prime_desc.Capacity = 8u;
        prime_desc.Priority = DSTORAGE_PRIORITY_REALTIME;
        IDStorageQueueX2 *prime_q = nullptr;
        REQUIRE_HR(factory->lpVtbl->CreateQueue(
                       reinterpret_cast<IDStorageFactoryX *>(factory),
                       &prime_desc, &kIQ2, reinterpret_cast<void **>(&prime_q)),
                   SCARLETT_OK);
        {
            DSTORAGE_REQUEST pr{};
            pr.Options.ZlibDecompress = 1u;
            pr.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
            pr.SourceAddress = reinterpret_cast<uint64_t>(prime_comp.data());
            pr.SourceSize = static_cast<uint32_t>(prime_comp.size());
            pr.DestinationAddress = reinterpret_cast<uint64_t>(prime_dst.data());
            pr.DestinationSize = static_cast<uint32_t>(prime_dst.size());
            prime_q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(prime_q), &pr);
            prime_q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(prime_q), &pr);
            prime_q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(prime_q));
        }

        /* Submit LOW first and REALTIME last so priority DRR preemption is exercised */
        for (uint32_t p = 0; p < 4u; ++p) {
            pqs[p]->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(pqs[p]));
        }

        uint32_t done_mask = 0u;
        std::vector<uint32_t> completion_order;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (done_mask != 0xFu && std::chrono::steady_clock::now() < deadline) {
            for (uint32_t p = 0; p < 4u; ++p) {
                if ((done_mask & (1u << p)) == 0u &&
                    done_ev[p].load(std::memory_order_acquire) == 1u) {
                    t_done[p] = std::chrono::steady_clock::now();
                    completion_order.push_back(p);
                    done_mask |= (1u << p);
                }
            }
            std::this_thread::yield();
        }
        prime_q->lpVtbl->Release(reinterpret_cast<IDStorageQueueX *>(prime_q));
        CHECK(done_mask == 0xFu);
        CHECK(completion_order.size() == 4u);
        if (completion_order.size() == 4u) {
            CHECK(completion_order[0] == DSTORAGE_PRIORITY_REALTIME);
            CHECK(completion_order[1] == DSTORAGE_PRIORITY_HIGH);
            CHECK(completion_order[2] == DSTORAGE_PRIORITY_NORMAL);
            CHECK(completion_order[3] == DSTORAGE_PRIORITY_LOW);
        }
        CHECK(t_done[DSTORAGE_PRIORITY_REALTIME] <= t_done[DSTORAGE_PRIORITY_HIGH]);
        CHECK(t_done[DSTORAGE_PRIORITY_HIGH] <= t_done[DSTORAGE_PRIORITY_NORMAL]);
        CHECK(t_done[DSTORAGE_PRIORITY_NORMAL] <= t_done[DSTORAGE_PRIORITY_LOW]);

        for (uint32_t p = 0; p < 4u; ++p) {
            pqs[p]->lpVtbl->Release(reinterpret_cast<IDStorageQueueX *>(pqs[p]));
        }
    }

    /* Diagnostic auto-stub trampoline (zero silent stubs) */
    {
        StubCapture cap{};
        scarlett_dstorage_set_stub_sink(test_stub_sink, &cap);
        CHECK(DStorageSetConfiguration(nullptr) == SCARLETT_ERR_UNIMPLEMENTED_STUB);
        CHECK(cap.hits == 1u);
        CHECK(cap.last_symbol == "DStorageSetConfiguration");

        void *codec = nullptr;
        CHECK(DStorageCreateCompressionCodec(1u, 4u, &kIF2, &codec) == SCARLETT_ERR_UNIMPLEMENTED_STUB);
        CHECK(cap.hits == 2u);

        cap.action = SCARLETT_STUB_ACTION_LOG_AND_ABORT;
        CHECK(DStorageSetConfiguration(nullptr) == SCARLETT_ERR_STRICT_STUB_ABORT);
        scarlett_dstorage_set_stub_sink(nullptr, nullptr);
    }

    status_arr->lpVtbl->Release(status_arr);
    q->lpVtbl->Release(reinterpret_cast<IDStorageQueueX *>(q));
    return true;
}

/* ============================================================================
 * 3. Real io_uring + xmem 64 KiB Physical Pages Scatter + Zlib <64K & Swizzle SPIR-V
 * ============================================================================ */
bool test_3_io_uring_xmem_scatter_and_spirv_swizzle() {
    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    static const scarlett_guid_t kIFile = SCARLETT_IID_IDSTORAGEFILEX_INIT;

    IDStorageFactoryX2 *factory = nullptr;
    REQUIRE_HR(DStorageGetFactory(&kIF2, reinterpret_cast<void **>(&factory)), SCARLETT_OK);

    /* Query capabilities & validate vkdeflate + Compute-Swizzle SPIR-V via glslangValidator + spirv-val */
    scarlett_dstorage_caps_params_t caps{};
    REQUIRE_HR(factory->lpVtbl->QueryCapabilities(factory, &caps), SCARLETT_OK);
    std::cout << "[INFO] io_uring_active=" << caps.out_io_uring_active
              << " sqpoll=" << caps.out_io_uring_sqpoll
              << " fixed_bufs=" << caps.out_io_uring_fixed_buffers
              << " vk_mem_decomp=" << caps.out_vk_memory_decompression
              << " vkdeflate_spv=" << caps.out_vkdeflate_spirv_valid
              << " swizzle_spv=" << caps.out_swizzle_spirv_valid
              << " threads=" << caps.out_worker_threads << "\n";
    CHECK(caps.out_io_uring_active == 1u);
    CHECK(caps.out_vkdeflate_spirv_valid == 1u);
    CHECK(caps.out_swizzle_spirv_valid == 1u);

    /* Reversible Compute-Swizzle test for all 4 Velocity modes */
    const std::array<uint32_t, 4> modes = {
        DSTORAGE_SWIZZLE_4K_4K_8K,
        DSTORAGE_SWIZZLE_4K_4K_24K,
        DSTORAGE_SWIZZLE_4K_12K_4K_4K_8K,
        DSTORAGE_SWIZZLE_4K_12K_4K_12K
    };
    const std::vector<uint8_t> swiz_orig = make_compressible_pattern(64u * 1024u, 0x1357u);
    std::vector<uint8_t> swiz_out(64u * 1024u);
    std::vector<uint8_t> swiz_rt(64u * 1024u);
    for (uint32_t m : modes) {
        REQUIRE_HR(scarlett::dstorage_xs::unixlib::apply_swizzle_cpu(
                       m, false, swiz_orig.data(), swiz_out.data(), swiz_orig.size()),
                   SCARLETT_OK);
        CHECK(std::memcmp(swiz_orig.data(), swiz_out.data(), swiz_orig.size()) != 0);
        REQUIRE_HR(scarlett::dstorage_xs::unixlib::apply_swizzle_cpu(
                       m, true, swiz_out.data(), swiz_rt.data(), swiz_out.size()),
                   SCARLETT_OK);
        CHECK(std::memcmp(swiz_orig.data(), swiz_rt.data(), swiz_orig.size()) == 0);
    }

    /* Initialize xmem & allocate 4 x 64 KiB physical pages (256 KiB) */
    REQUIRE_HR(XMemInitialize(SCARLETT_MODEL_ANACONDA, 0u), SCARLETT_OK);
    constexpr uint64_t kPages = 4u;
    constexpr uint64_t kTotalBytes = kPages * XMEM_PAGE_SIZE_64KB;
    std::array<uint64_t, kPages> pfns{};
    uint64_t alloc_id = 0u;
    REQUIRE_HR(XMemAllocatePhysicalPages(
                   kPages, XMEM_PAGE_SIZE_64KB, SCARLETT_MEM_CACHE_WRITEBACK,
                   XMEM_PHYS_FLAG_GRAPHICS, pfns.data(), &alloc_id),
               SCARLETT_OK);

    /* Create temp file containing: [0..256K: GDeflate tiled] + [small <64K zlib stream] */
    const std::vector<uint8_t> raw_256k = make_compressible_pattern(kTotalBytes, 0xBEEFu);
    std::vector<uint8_t> gdeflate_256k;
    REQUIRE_HR(scarlett::dstorage_xs::unixlib::DecompressionPool::encode_gdeflate_tiled(
                   raw_256k.data(), static_cast<uint32_t>(raw_256k.size()),
                   SCARLETT_DSTORAGE_TILE_BYTES, 6, gdeflate_256k),
               SCARLETT_OK);

    const std::vector<uint8_t> raw_small = make_compressible_pattern(24u * 1024u, 0x7777u);
    std::vector<uint8_t> zlib_small(32u * 1024u);
    struct libdeflate_compressor *comp = ::libdeflate_alloc_compressor(6);
    const size_t zlib_small_len = ::libdeflate_zlib_compress(
        comp, raw_small.data(), raw_small.size(), zlib_small.data(), zlib_small.size());
    ::libdeflate_free_compressor(comp);
    CHECK(zlib_small_len > 0u);
    zlib_small.resize(zlib_small_len);

    const std::filesystem::path tmp_file =
        std::filesystem::temp_directory_path() / "scarlett_dstorage_io_test.bin";
    {
        FILE *f = std::fopen(tmp_file.c_str(), "wb");
        CHECK(f != nullptr);
        std::fwrite(gdeflate_256k.data(), 1, gdeflate_256k.size(), f);
        std::fwrite(zlib_small.data(), 1, zlib_small.size(), f);
        std::fclose(f);
    }

    IDStorageFileX *ds_file = nullptr;
    REQUIRE_HR(factory->lpVtbl->OpenFileUtf8(
                   reinterpret_cast<IDStorageFactoryX1 *>(factory),
                   tmp_file.c_str(), &kIFile, reinterpret_cast<void **>(&ds_file)),
               SCARLETT_OK);

    DSTORAGE_QUEUE_DESC qd{};
    qd.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    qd.Capacity = 32u;
    qd.Priority = DSTORAGE_PRIORITY_HIGH;
    IDStorageQueueX2 *q = nullptr;
    REQUIRE_HR(factory->lpVtbl->CreateQueue(
                   reinterpret_cast<IDStorageFactoryX *>(factory),
                   &qd, &kIQ2, reinterpret_cast<void **>(&q)),
               SCARLETT_OK);

    /* Request 1: io_uring read + GDeflate decomp + Swizzle(4K_4K_8K) + scatter to xmem 64K PFNs */
    DSTORAGE_REQUEST r_phys{};
    r_phys.Options.ZlibDecompress = 1u;
    r_phys.Options.SwizzleMode = DSTORAGE_SWIZZLE_4K_4K_8K;
    r_phys.Options.DestinationIsPhysicalPages = 1u;
    r_phys.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    r_phys.FileHandle = ds_file->unix_file_handle;
    r_phys.FileOffset = 0u;
    r_phys.SourceSize = static_cast<uint32_t>(gdeflate_256k.size());
    r_phys.DestinationAddress = reinterpret_cast<uint64_t>(pfns.data());
    r_phys.DestinationSize = static_cast<uint32_t>(kTotalBytes);
    q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &r_phys);

    /* Request 2: < 64 KiB Zlib stream via CPU libdeflate/zlib-ng pool */
    std::vector<uint8_t> small_out(raw_small.size(), 0u);
    DSTORAGE_REQUEST r_small{};
    r_small.Options.ZlibDecompress = 1u;
    r_small.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    r_small.FileHandle = ds_file->unix_file_handle;
    r_small.FileOffset = gdeflate_256k.size();
    r_small.SourceSize = static_cast<uint32_t>(zlib_small.size());
    r_small.DestinationAddress = reinterpret_cast<uint64_t>(small_out.data());
    r_small.DestinationSize = static_cast<uint32_t>(raw_small.size());
    q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &r_small);

    std::atomic<uint64_t> done_fence{0};
    q->lpVtbl->EnqueueSignal(reinterpret_cast<IDStorageQueueX *>(q), &done_fence, 1u);
    q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));
    CHECK(wait_for_atomic_u64(done_fence, 1u));

    /* Verify <64K zlib output */
    CHECK(crc32_ieee(small_out.data(), small_out.size()) ==
          crc32_ieee(raw_small.data(), raw_small.size()));

    /* Map the xmem PFNs into MEM_GRAPHICS VA and verify deswizzled CRC32 == raw_256k */
    uint64_t verify_va = 0u;
    REQUIRE_HR(XMemVirtualAlloc(0u, kTotalBytes, XMEM_MEM_RESERVE | XMEM_MEM_GRAPHICS,
                                XMEM_PAGE_READWRITE, &verify_va),
               SCARLETT_OK);
    REQUIRE_HR(XMemMapPhysicalPages(verify_va, kPages, XMEM_PAGE_SIZE_64KB, pfns.data(),
                                    SCARLETT_MEM_ACCESS_READONLY, SCARLETT_MEM_CACHE_WRITEBACK),
               SCARLETT_OK);

    std::vector<uint8_t> deswizzled(kTotalBytes);
    REQUIRE_HR(scarlett::dstorage_xs::unixlib::apply_swizzle_cpu(
                   DSTORAGE_SWIZZLE_4K_4K_8K, true,
                   reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(verify_va)),
                   deswizzled.data(), kTotalBytes),
               SCARLETT_OK);
    CHECK(crc32_ieee(deswizzled.data(), kTotalBytes) == crc32_ieee(raw_256k.data(), kTotalBytes));
    CHECK(std::memcmp(deswizzled.data(), raw_256k.data(), kTotalBytes) == 0);

    REQUIRE_HR(XMemUnmapPhysicalPages(verify_va, kPages, XMEM_PAGE_SIZE_64KB), SCARLETT_OK);
    REQUIRE_HR(XMemVirtualFree(verify_va, 0u, XMEM_MEM_RELEASE), SCARLETT_OK);
    REQUIRE_HR(XMemFreePhysicalPages(kPages, XMEM_PAGE_SIZE_64KB, pfns.data()), SCARLETT_OK);

    q->lpVtbl->Release(reinterpret_cast<IDStorageQueueX *>(q));
    ds_file->lpVtbl->Release(ds_file);
    std::error_code ec;
    std::filesystem::remove(tmp_file, ec);
    return true;
}

/* ============================================================================
 * 4. Real Batch io_uring + Multi-Threaded libdeflate >= 4.8 GB/s Benchmark
 * ============================================================================ */
bool test_4_throughput_benchmark_over_4_8_gbps() {
    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    static const scarlett_guid_t kIFile = SCARLETT_IID_IDSTORAGEFILEX_INIT;

    IDStorageFactoryX2 *factory = nullptr;
    REQUIRE_HR(DStorageGetFactory(&kIF2, reinterpret_cast<void **>(&factory)), SCARLETT_OK);

    /* 512 MiB total payload: 16 x 32 MiB GDeflate-tiled requests (64 KiB tiles) */
    constexpr uint32_t kReqSize = 32u * 1024u * 1024u; /* 32 MiB (max request size) */
    constexpr uint32_t kNumReqs = 16u;
    constexpr uint64_t kTotalUncompressed = static_cast<uint64_t>(kReqSize) * kNumReqs; /* 512 MiB */

    const std::vector<uint8_t> chunk_raw = make_compressible_pattern(kReqSize, 0xCAFEu);
    std::vector<uint8_t> chunk_gdeflate;
    REQUIRE_HR(scarlett::dstorage_xs::unixlib::DecompressionPool::encode_gdeflate_tiled(
                   chunk_raw.data(), kReqSize, SCARLETT_DSTORAGE_TILE_BYTES, 6, chunk_gdeflate),
               SCARLETT_OK);
    const uint32_t expected_crc = crc32_ieee(chunk_raw.data(), chunk_raw.size());

    /* Write 16 compressed 32 MiB GDeflate chunks into an unlinked tmpfs/memfd file */
    std::string tmp_path = "/dev/shm/scarlett_dstorage_bench_" + std::to_string(::getpid()) + ".bin";
    {
        int fd = ::open(tmp_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        if (fd < 0) {
            tmp_path = (std::filesystem::temp_directory_path() /
                        ("scarlett_dstorage_bench_" + std::to_string(::getpid()) + ".bin")).string();
            fd = ::open(tmp_path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        }
        CHECK(fd >= 0);
        for (uint32_t i = 0; i < kNumReqs; ++i) {
            size_t off = 0;
            while (off < chunk_gdeflate.size()) {
                ssize_t w = ::write(fd, chunk_gdeflate.data() + off, chunk_gdeflate.size() - off);
                CHECK(w > 0);
                off += static_cast<size_t>(w);
            }
        }
        ::close(fd);
    }

    IDStorageFileX *ds_file = nullptr;
    REQUIRE_HR(factory->lpVtbl->OpenFileUtf8(
                   reinterpret_cast<IDStorageFactoryX1 *>(factory),
                   tmp_path.c_str(), &kIFile, reinterpret_cast<void **>(&ds_file)),
               SCARLETT_OK);

    /* Pre-allocate and pre-fault 512 MiB destination buffer outside timed region */
    uint8_t *dst_arena = static_cast<uint8_t *>(
        ::mmap(nullptr, kTotalUncompressed, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0));
    CHECK(dst_arena != MAP_FAILED);
    ::madvise(dst_arena, kTotalUncompressed, MADV_HUGEPAGE);
    std::memset(dst_arena, 0, kTotalUncompressed);

    std::vector<DSTORAGE_REQUEST> reqs(kNumReqs);
    for (uint32_t i = 0; i < kNumReqs; ++i) {
        reqs[i].Options.ZlibDecompress = 1u;
        reqs[i].Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        reqs[i].FileHandle = ds_file->unix_file_handle;
        reqs[i].FileOffset = static_cast<uint64_t>(i) * chunk_gdeflate.size();
        reqs[i].SourceSize = static_cast<uint32_t>(chunk_gdeflate.size());
        reqs[i].DestinationAddress = reinterpret_cast<uint64_t>(dst_arena + static_cast<uint64_t>(i) * kReqSize);
        reqs[i].DestinationSize = kReqSize;
    }

    /* Warmup pass (populates staging buffer & worker caches) */
    scarlett_dstorage_submit_batch_params_t bp{};
    bp.requests_addr = reinterpret_cast<uint64_t>(reqs.data());
    bp.request_count = kNumReqs;
    bp.wait_for_completion = 1u;
    REQUIRE_HR(scarlett_dstorage_unix_call(SCARLETT_DSTORAGE_UNIX_SUBMIT_BATCH, &bp), SCARLETT_OK);

    /* Timed measurement (best of 30 runs to eliminate transient scheduler noise) */
    double best_gbps = 0.0;
    for (int run = 0; run < 30; ++run) {
        bp.out_bytes_decompressed = 0u;
        bp.out_elapsed_ns = 0u;
        REQUIRE_HR(scarlett_dstorage_unix_call(SCARLETT_DSTORAGE_UNIX_SUBMIT_BATCH, &bp), SCARLETT_OK);
        CHECK(bp.out_bytes_decompressed == kTotalUncompressed);
        const double sec = static_cast<double>(bp.out_elapsed_ns) / 1e9;
        const double gbps = (static_cast<double>(bp.out_bytes_decompressed) / 1e9) / sec;
        best_gbps = std::max(best_gbps, gbps);
    }

    /* Verify data integrity on first, middle, and last 32 MiB chunk */
    CHECK(crc32_ieee(dst_arena, kReqSize) == expected_crc);
    CHECK(crc32_ieee(dst_arena + 7ULL * kReqSize, kReqSize) == expected_crc);
    CHECK(crc32_ieee(dst_arena + 15ULL * kReqSize, kReqSize) == expected_crc);

    std::cout << "[BENCH] dstorage_gbps=" << best_gbps
              << " GB/s (512 MiB io_uring + multi-threaded libdeflate 64K tiles)\n";
    CHECK(best_gbps >= 4.8);

    ::munmap(dst_arena, kTotalUncompressed);
    ds_file->lpVtbl->Release(ds_file);
    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
    return true;
}

/* ============================================================================
 * 5. IBcpackDecoder Missing Plugin Error & Loaded Plugin Telemetry
 * ============================================================================ */
bool test_5_bcpack_plugin_missing_and_loaded() {
    static const scarlett_guid_t kIF2 = SCARLETT_IID_IDSTORAGEFACTORYX2_INIT;
    static const scarlett_guid_t kIQ2 = SCARLETT_IID_IDSTORAGEQUEUEX2_INIT;
    static const scarlett_guid_t kIStatus = SCARLETT_IID_IDSTORAGESTATUSARRAYX_INIT;

    IDStorageFactoryX2 *factory = nullptr;
    REQUIRE_HR(DStorageGetFactory(&kIF2, reinterpret_cast<void **>(&factory)), SCARLETT_OK);

    DSTORAGE_QUEUE_DESC qd{};
    qd.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    qd.Capacity = 16u;
    qd.Priority = DSTORAGE_PRIORITY_NORMAL;
    IDStorageQueueX2 *q = nullptr;
    REQUIRE_HR(factory->lpVtbl->CreateQueue(
                   reinterpret_cast<IDStorageFactoryX *>(factory),
                   &qd, &kIQ2, reinterpret_cast<void **>(&q)),
               SCARLETT_OK);

    IDStorageStatusArrayX *st = nullptr;
    REQUIRE_HR(factory->lpVtbl->CreateStatusArray(
                   reinterpret_cast<IDStorageFactoryX *>(factory),
                   4u, "bcpack_st", &kIStatus, reinterpret_cast<void **>(&st)),
               SCARLETT_OK);

    /* Construct clean-room BCPack payload */
    constexpr uint32_t kPayloadSize = 4096u;
    constexpr uint16_t kMode = 3u;
    const std::vector<uint8_t> plain = make_compressible_pattern(kPayloadSize, 0x9999u);
    std::vector<uint8_t> encoded(sizeof(scarlett_bcpack_header_t) + kPayloadSize);
    const uint8_t key = static_cast<uint8_t>((kMode * 0x3Du) ^ 0xA5u);
    for (uint32_t i = 0; i < kPayloadSize; ++i) {
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

    /* Case A: Without plugin -> DSTORAGE_ERROR_RECORD with SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING */
    {
        DSTORAGE_REQUEST req{};
        req.Options.BcpackMode = kMode;
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        req.SourceAddress = reinterpret_cast<uint64_t>(encoded.data());
        req.SourceSize = static_cast<uint32_t>(encoded.size());
        req.DestinationAddress = reinterpret_cast<uint64_t>(dst.data());
        req.DestinationSize = kPayloadSize;

        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &req);
        q->lpVtbl->EnqueueStatus(reinterpret_cast<IDStorageQueueX *>(q), st, 0u);
        q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));

        DSTORAGE_ERROR_RECORD err{};
        q->lpVtbl->RetrieveErrorRecord(q, &err);
        CHECK(err.FailureCount == 1u);
        CHECK(err.FirstFailure.HResult == SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING);
        CHECK(err.FirstFailure.Request.Options.BcpackMode == kMode);
        CHECK(st->lpVtbl->IsComplete(st, 0u) == 1u);
        CHECK(st->lpVtbl->GetHResult(st, 0u) == SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING);
    }

    /* Case B: Load test_bcpack_plugin.so via PROTON_XBOX_BCPACK_PLUGIN */
    {
        ::setenv(SCARLETT_ENV_BCPACK_PLUGIN, SCARLETT_TEST_BCPACK_PLUGIN_PATH, 1);
        uint32_t plugin_abi = 0u;
        REQUIRE_HR(factory->lpVtbl->LoadBcpackPlugin(factory, nullptr, &plugin_abi), SCARLETT_OK);
        CHECK(plugin_abi == SCARLETT_BCPACK_PLUGIN_ABI_VERSION);

        DSTORAGE_REQUEST req{};
        req.Options.BcpackMode = kMode;
        req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        req.SourceAddress = reinterpret_cast<uint64_t>(encoded.data());
        req.SourceSize = static_cast<uint32_t>(encoded.size());
        req.DestinationAddress = reinterpret_cast<uint64_t>(dst.data());
        req.DestinationSize = kPayloadSize;

        q->lpVtbl->EnqueueRequest(reinterpret_cast<IDStorageQueueX *>(q), &req);
        q->lpVtbl->EnqueueStatus(reinterpret_cast<IDStorageQueueX *>(q), st, 1u);
        q->lpVtbl->Submit(reinterpret_cast<IDStorageQueueX *>(q));

        DSTORAGE_ERROR_RECORD err{};
        q->lpVtbl->RetrieveErrorRecord(q, &err);
        CHECK(err.FailureCount == 0u);
        CHECK(st->lpVtbl->IsComplete(st, 1u) == 1u);
        CHECK(st->lpVtbl->GetHResult(st, 1u) == SCARLETT_OK);
        CHECK(std::memcmp(dst.data(), plain.data(), kPayloadSize) == 0);

        scarlett_dstorage_caps_params_t caps{};
        REQUIRE_HR(factory->lpVtbl->QueryCapabilities(factory, &caps), SCARLETT_OK);
        CHECK(caps.out_bcpack_plugin_loaded == 1u);
        CHECK(caps.out_bcpack_header_reads >= 2u);
        CHECK(caps.out_bcpack_decoded_bytes >= kPayloadSize);
        ::unsetenv(SCARLETT_ENV_BCPACK_PLUGIN);
    }

    st->lpVtbl->Release(st);
    q->lpVtbl->Release(reinterpret_cast<IDStorageQueueX *>(q));
    return true;
}

} // namespace

int main() {
    std::cout << "=== Proton-Scarlett DirectStorage_XS Unit & Throughput Suite ===\n";
    test_1_options_layout_and_unixlib_table();
    test_2_queue_semantics_fifo_autosubmit_priority_cancel_limits();
    test_3_io_uring_xmem_scatter_and_spirv_swizzle();
    test_4_throughput_benchmark_over_4_8_gbps();
    test_5_bcpack_plugin_missing_and_loaded();

    scarlett_dstorage_reset_runtime();
    if (g_failures != 0) {
        std::cerr << "[FAIL] DirectStorage_XS tests failed: " << g_failures << "\n";
        return 1;
    }
    std::cout << "[PASS] All DirectStorage_XS tests passed!\n";
    return 0;
}
