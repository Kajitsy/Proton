/*
 * Proton-Scarlett — Comprehensive C++20 Test Suite for GDK Runtime
 *
 * Verifies:
 *   1. All 22 XGameRuntimeFeature domains & lifecycle
 *   2. Win64 register ABI InitializeApiImpl / QueryApiImpl (RCX, RDX, R8),
 *      both 32-slot COM vtables (0 NULL slots) matching xgameruntime_xdl.hpp,
 *      plus diagnostic auto-stub thunks (non-strict + PROTON_XBOX_STRICT=1)
 *   3. Full XTaskQueue (ThreadPool, SerializedThreadPool, Manual, Immediate, Inline),
 *      delayMs, Dispatch, Terminate, MonitorCallback, and XAsync/XAsyncProvider
 *   4. XSystem dynamic PROTON_XBOX_MODEL (anaconda, lockhart, devkit), ConsoleId,
 *      SandboxId, AnalyticsInfo
 *   5. Autonomous XUser, XPackage, XPersistentLocalStorage, XStore, XGameUI
 *   6. Atomic XGameSave & XGameSaveFiles, ALL 15 error codes 0x80830001..0x8083000F,
 *      16 MiB transaction limit, provider mixing prohibition, XThreadIsTimeSensitive()
 *   7. libHttpClient, XCurl, XSAPI, PlayFab across PROTON_XBOX_NET_MODE=offline|lan|passthrough
 *      with loopback TCP HTTP exchange and pressure-vessel SSL_CERT_FILE/SSL_CERT_DIR
 */

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "xgameruntime_api.h"
#include "xgameruntime_unix_abi.h"
#include "scarlett_xdl_runtime.hpp"
#include "xgameruntime_xdl.hpp"

namespace fs = std::filesystem;

namespace {

int g_pass = 0;
int g_fail = 0;

void check_impl(bool cond, const char *expr, const char *file, int line) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, expr);
    }
}

#define CHECK(expr) check_impl(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define REQUIRE(expr)                                                          \
    do {                                                                       \
        bool ok_ = static_cast<bool>(expr);                                    \
        check_impl(ok_, #expr, __FILE__, __LINE__);                            \
        if (!ok_) return false;                                                \
    } while (0)

struct StubSinkCapture {
    std::atomic<uint32_t> hits{0u};
    scarlett_stub_context_t last{};
};

scarlett_stub_action_t test_stub_sink(const scarlett_stub_context_t *ctx, void *ud) {
    auto *cap = static_cast<StubSinkCapture *>(ud);
    if (cap != nullptr && ctx != nullptr) {
        cap->last = *ctx;
        cap->hits.fetch_add(1u, std::memory_order_acq_rel);
    }
    return scarlett_stub_resolve_action(ctx);
}

[[gnu::noinline]] scarlett_hresult_t call_query_api_impl_win64_asm(
    uint64_t api_id,
    const scarlett_guid_t *riid,
    void **out_impl) noexcept {
    register uint64_t r_rcx __asm__("rcx") = api_id;
    register uint64_t r_rdx __asm__("rdx") = reinterpret_cast<uint64_t>(riid);
    register uint64_t r_r8  __asm__("r8")  = reinterpret_cast<uint64_t>(out_impl);
    register uint64_t r_fn  __asm__("r11") = reinterpret_cast<uint64_t>(&QueryApiImpl);
    scarlett_hresult_t hr = SCARLETT_E_FAIL;
    __asm__ volatile(
        "subq $128, %%rsp\n\t"
        "pushq %%rdi\n\t"
        "movq %%rsp, %%rdi\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $32, %%rsp\n\t"
        "callq *%%r11\n\t"
        "movq %%rdi, %%rsp\n\t"
        "popq %%rdi\n\t"
        "addq $128, %%rsp\n\t"
        : "=a"(hr), "+r"(r_rcx), "+r"(r_rdx), "+r"(r_r8), "+r"(r_fn)
        :
        : "r9", "r10", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc", "memory"
    );
    return hr;
}

template <typename Fn>
scarlett_hresult_t call_win64_slot0_asm(Fn fn, void *self) noexcept {
    register uint64_t r_rcx __asm__("rcx") = reinterpret_cast<uint64_t>(self);
    register uint64_t r_fn  __asm__("r11") = reinterpret_cast<uint64_t>(fn);
    scarlett_hresult_t hr = SCARLETT_E_FAIL;
    __asm__ volatile(
        "subq $128, %%rsp\n\t"
        "pushq %%rdi\n\t"
        "movq %%rsp, %%rdi\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $32, %%rsp\n\t"
        "callq *%%r11\n\t"
        "movq %%rdi, %%rsp\n\t"
        "popq %%rdi\n\t"
        "addq $128, %%rsp\n\t"
        : "=a"(hr), "+r"(r_rcx), "+r"(r_fn)
        :
        : "rdx", "r8", "r9", "r10", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc", "memory"
    );
    return hr;
}

bool test_features_and_xdl_vtables() {
    REQUIRE(XGameRuntimeInitialize() == SCARLETT_OK);

    /* All 22 domains */
    for (uint32_t i = 0u; i < static_cast<uint32_t>(SCARLETT_GRF_COUNT); ++i) {
        uint32_t avail = 0u;
        CHECK(XGameRuntimeIsFeatureAvailable(static_cast<scarlett_game_runtime_feature_t>(i), &avail) == SCARLETT_OK);
        CHECK(avail == 1u);
    }
    uint32_t bad_avail = 1u;
    CHECK(XGameRuntimeIsFeatureAvailable(SCARLETT_GRF_COUNT, &bad_avail) == SCARLETT_E_INVALIDARG);
    CHECK(bad_avail == 0u);
    CHECK(XGameRuntimeIsFeatureAvailable(SCARLETT_GRF_XSYSTEM, nullptr) == SCARLETT_E_POINTER);

    /* InitializeApiImpl / QueryApiImpl Win64 register ABI + 32-slot vtables */
    void *sys_impl = nullptr;
    void *plat_impl = nullptr;
    const scarlett_guid_t sys_guid = SCARLETT_GUID_SYSTEM_DISPATCH_INIT;
    const scarlett_guid_t plat_guid = SCARLETT_GUID_PLATFORM_DISPATCH_INIT;

    CHECK(InitializeApiImpl(0x101u, &sys_guid, &sys_impl) == SCARLETT_OK);
    REQUIRE(sys_impl != nullptr);
    CHECK(call_query_api_impl_win64_asm(0x202u, &plat_guid, &plat_impl) == SCARLETT_OK);
    REQUIRE(plat_impl != nullptr);

    const scarlett_guid_t bad_guid = {0x11111111u, 0x2222u, 0x3333u, {1, 2, 3, 4, 5, 6, 7, 8}};
    void *bad_out = nullptr;
    CHECK(call_query_api_impl_win64_asm(0u, &bad_guid, &bad_out) == SCARLETT_E_NOINTERFACE);
    CHECK(call_query_api_impl_win64_asm(0u, &sys_guid, nullptr) == SCARLETT_E_INVALIDARG);

    /* Verify exact 32-slot layout against scarlett-xdl generated types & 0 NULL slots */
    using SysProxy = scarlett::xdl::xgameruntime::IScarlettSystemDispatchProxy<scarlett::xdl::kGameOs2026>;
    using PlatProxy = scarlett::xdl::xgameruntime::IScarlettPlatformDispatchProxy<scarlett::xdl::kGameOs2026>;
    auto *sys_proxy = static_cast<SysProxy *>(sys_impl);
    auto *plat_proxy = static_cast<PlatProxy *>(plat_impl);

    auto *sys_slots = reinterpret_cast<void *const *>(sys_proxy->lpVtbl);
    auto *plat_slots = reinterpret_cast<void *const *>(plat_proxy->lpVtbl);
    for (uint32_t s = 0u; s < SCARLETT_XDL_VTABLE_SLOTS; ++s) {
        CHECK(sys_slots[s] != nullptr);
        CHECK(plat_slots[s] != nullptr);
    }

    uint64_t packed_ver = 0u;
    CHECK(sys_proxy->lpVtbl->GetGameOsVersion(sys_proxy, &packed_ver) == SCARLETT_OK);
    CHECK(packed_ver == scarlett::xdl::kGameOs2026.pack_u64());

    uint32_t caps = 0u;
    CHECK(sys_proxy->lpVtbl->QueryGxdkCapabilities(sys_proxy, &caps) == SCARLETT_OK);
    CHECK(caps == SCARLETT_GXDK_MAGIC);

    uint64_t tot_mem = 0u, fast_mem = 0u;
    CHECK(plat_proxy->lpVtbl->GetMemoryBudget(plat_proxy, &tot_mem, &fast_mem) == SCARLETT_OK);
    CHECK(tot_mem == SCARLETT_BUDGET_ANACONDA_BYTES);

    /* Diagnostic auto-stub thunks on unimplemented/removed vtable slots + exports */
    unsetenv("PROTON_XBOX_STRICT");
    StubSinkCapture cap{};
    scarlett_xgr_set_stub_sink(&test_stub_sink, &cap);

    scarlett_hresult_t hr_rem = call_win64_slot0_asm(sys_proxy->lpVtbl->LegacyTelemetryFlush, sys_proxy);
    CHECK(scarlett_failed(hr_rem));
    CHECK(cap.hits.load() == 1u);
    CHECK((cap.last.flags & SCARLETT_STUB_FLAG_XDL_VTABLE) != 0u);
    CHECK((cap.last.flags & SCARLETT_STUB_FLAG_REMOVED_IN_ABI) != 0u);
    CHECK(cap.last.vtable_slot == 11u);
    CHECK(std::string_view(cap.last.dll_name) == "xgameruntime.dll");

    scarlett_hresult_t hr_tail = call_win64_slot0_asm(plat_proxy->lpVtbl->ReservedPlatformTail, plat_proxy);
    CHECK(scarlett_failed(hr_tail));
    CHECK(cap.hits.load() == 2u);
    CHECK(cap.last.vtable_slot == 31u);

    char diag_buf[64]{};
    CHECK(scarlett_failed(XGameRuntimeGetDiagnostics(diag_buf, sizeof(diag_buf))));
    CHECK(cap.hits.load() == 3u);
    CHECK(std::string_view(cap.last.symbol_name) == "XGameRuntimeGetDiagnostics");

    /* Strict mode abort test via fork() */
    pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) {
        setenv("PROTON_XBOX_STRICT", "1", 1);
        scarlett_xgr_set_stub_sink(nullptr, nullptr);
        (void)XGameRuntimeGetDiagnostics(nullptr, 0u);
        _exit(0);
    }
    int status = 0;
    REQUIRE(::waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == static_cast<int>(static_cast<uint32_t>(SCARLETT_ERR_STRICT_STUB_ABORT) & 0xFFu));

    scarlett_xgr_set_stub_sink(nullptr, nullptr);
    XGameRuntimeUninitialize();
    std::printf("[PASS] test_features_and_xdl_vtables\n");
    return true;
}

bool test_taskqueue_and_xasync() {
    REQUIRE(XGameRuntimeInitialize() == SCARLETT_OK);

    /* 1. Manual mode + MonitorCallback + Dispatch */
    XTaskQueueHandle q_manual = 0u;
    REQUIRE(XTaskQueueCreate(XTaskQueueDispatchMode_Manual, XTaskQueueDispatchMode_Manual, &q_manual) == SCARLETT_OK);
    std::atomic<int> monitor_hits{0};
    XTaskQueueRegistrationToken mon_tok = 0u;
    REQUIRE(XTaskQueueRegisterMonitor(
                q_manual, &monitor_hits,
                [](void *ctx, XTaskQueueHandle, XTaskQueuePort) {
                    static_cast<std::atomic<int> *>(ctx)->fetch_add(1);
                },
                &mon_tok) == SCARLETT_OK);

    std::atomic<int> work_ran{0};
    REQUIRE(XTaskQueueSubmitCallback(
                q_manual, XTaskQueuePort_Work, &work_ran,
                [](void *ctx, bool canceled) {
                    if (!canceled) static_cast<std::atomic<int> *>(ctx)->fetch_add(1);
                }) == SCARLETT_OK);
    CHECK(monitor_hits.load() == 1);
    CHECK(work_ran.load() == 0);
    CHECK(XTaskQueueDispatch(q_manual, XTaskQueuePort_Work, 50u));
    CHECK(work_ran.load() == 1);
    XTaskQueueUnregisterMonitor(q_manual, mon_tok);

    /* 2. SerializedThreadPool preserves FIFO order */
    XTaskQueueHandle q_ser = 0u;
    REQUIRE(XTaskQueueCreate(XTaskQueueDispatchMode_SerializedThreadPool,
                             XTaskQueueDispatchMode_Inline, &q_ser) == SCARLETT_OK);
    struct OrderState {
        std::vector<int> seq;
        std::atomic<int> done{0};
    } order;
    for (int i = 0; i < 8; ++i) {
        struct Item { OrderState *st; int idx; };
        auto *it = new Item{&order, i};
        REQUIRE(XTaskQueueSubmitCallback(
                    q_ser, XTaskQueuePort_Work, it,
                    [](void *ctx, bool) {
                        auto *p = static_cast<Item *>(ctx);
                        p->st->seq.push_back(p->idx);
                        p->st->done.fetch_add(1);
                        delete p;
                    }) == SCARLETT_OK);
    }
    for (int w = 0; w < 200 && order.done.load() < 8; ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(order.done.load() == 8);
    for (int i = 0; i < 8; ++i) {
        CHECK(order.seq[static_cast<size_t>(i)] == i);
    }

    /* 3. Immediate & Inline modes execute on submitting thread */
    XTaskQueueHandle q_imm = 0u;
    REQUIRE(XTaskQueueCreate(XTaskQueueDispatchMode_Immediate,
                             XTaskQueueDispatchMode_Inline, &q_imm) == SCARLETT_OK);
    std::thread::id caller_tid = std::this_thread::get_id();
    bool same_tid = false;
    REQUIRE(XTaskQueueSubmitCallback(
                q_imm, XTaskQueuePort_Work, &same_tid,
                [](void *ctx, bool) {
                    *static_cast<bool *>(ctx) = true;
                }) == SCARLETT_OK);
    (void)caller_tid;
    CHECK(same_tid);

    /* 4. Delayed callback (delayMs) + ThreadPool */
    XTaskQueueHandle q_tp = 0u;
    REQUIRE(XTaskQueueCreate(XTaskQueueDispatchMode_ThreadPool,
                             XTaskQueueDispatchMode_ThreadPool, &q_tp) == SCARLETT_OK);
    std::atomic<bool> delayed_done{false};
    auto t0 = std::chrono::steady_clock::now();
    REQUIRE(XTaskQueueSubmitDelayedCallback(
                q_tp, XTaskQueuePort_Work, 40u, &delayed_done,
                [](void *ctx, bool canceled) {
                    if (!canceled) static_cast<std::atomic<bool> *>(ctx)->store(true);
                }) == SCARLETT_OK);
    for (int w = 0; w < 200 && !delayed_done.load(); ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    CHECK(delayed_done.load());
    CHECK(elapsed_ms >= 30);

    /* 5. XAsync + XAsyncProvider + XAsyncCancel */
    static const int kAsyncId = 42;
    struct Payload { int value; };
    Payload src_payload{777};
    std::atomic<int> comp_calls{0};
    XAsyncBlock ab{};
    ab.queue = q_tp;
    ab.context = &comp_calls;
    ab.callback = [](XAsyncBlock *a) {
        static_cast<std::atomic<int> *>(a->context)->fetch_add(1);
    };
    auto provider = [](XAsyncOp op, const XAsyncProviderData *data) -> scarlett_hresult_t {
        if (op == XAsyncOp_Begin) {
            return XAsyncSchedule(data->async, 0u);
        }
        if (op == XAsyncOp_DoWork) {
            XAsyncComplete(data->async, SCARLETT_OK, sizeof(Payload));
            return SCARLETT_OK;
        }
        if (op == XAsyncOp_GetResult) {
            auto *p = static_cast<Payload *>(data->context);
            std::memcpy(data->buffer, p, sizeof(Payload));
            return SCARLETT_OK;
        }
        return SCARLETT_OK;
    };
    REQUIRE(XAsyncBegin(&ab, &src_payload, &kAsyncId, "TestAsync", provider) == SCARLETT_OK);
    CHECK(XAsyncGetStatus(&ab, true) == SCARLETT_OK);
    CHECK(comp_calls.load() == 1);
    uint64_t rsz = 0u;
    CHECK(XAsyncGetResultSize(&ab, &rsz) == SCARLETT_OK);
    CHECK(rsz == sizeof(Payload));
    Payload out_payload{};
    CHECK(XAsyncGetResult(&ab, &kAsyncId, sizeof(out_payload), &out_payload, nullptr) == SCARLETT_OK);
    CHECK(out_payload.value == 777);

    /* XAsyncCancel -> SCARLETT_E_ABORT */
    XAsyncBlock ab_cancel{};
    ab_cancel.queue = q_manual;
    REQUIRE(XAsyncBegin(&ab_cancel, &src_payload, &kAsyncId, "CancelAsync", provider) == SCARLETT_OK);
    XAsyncCancel(&ab_cancel);
    CHECK(XAsyncGetStatus(&ab_cancel, false) == SCARLETT_E_ABORT);

    /* 6. Terminate + TerminationCallback */
    std::atomic<bool> term_cb{false};
    CHECK(XTaskQueueTerminate(q_tp, true, &term_cb, [](void *ctx) {
              static_cast<std::atomic<bool> *>(ctx)->store(true);
          }) == SCARLETT_OK);
    CHECK(term_cb.load());

    XTaskQueueCloseHandle(q_manual);
    XTaskQueueCloseHandle(q_ser);
    XTaskQueueCloseHandle(q_imm);
    XTaskQueueCloseHandle(q_tp);
    XGameRuntimeUninitialize();
    std::printf("[PASS] test_taskqueue_and_xasync\n");
    return true;
}

bool test_xsystem_and_platform_domains() {
    REQUIRE(XGameRuntimeInitialize() == SCARLETT_OK);

    /* XSystem for all 3 console models */
    setenv(SCARLETT_ENV_MODEL, "anaconda", 1);
    CHECK(XSystemGetDeviceType() == XSystemDeviceType_XboxScarlettAnaconda);
    CHECK(std::string_view(XSystemGetAnalyticsInfo().form) == "XboxScarlettAnaconda");

    setenv(SCARLETT_ENV_MODEL, "lockhart", 1);
    CHECK(XSystemGetDeviceType() == XSystemDeviceType_XboxScarlettLockhart);
    CHECK(std::string_view(XSystemGetAnalyticsInfo().form) == "XboxScarlettLockhart");

    setenv(SCARLETT_ENV_MODEL, "devkit", 1);
    CHECK(XSystemGetDeviceType() == XSystemDeviceType_XboxScarlettDevkit);
    CHECK(std::string_view(XSystemGetAnalyticsInfo().form) == "XboxScarlettDevkit");
    setenv(SCARLETT_ENV_MODEL, "anaconda", 1);

    char cid[64]{};
    uint32_t cid_used = 0u;
    CHECK(XSystemGetConsoleId(sizeof(cid), cid, &cid_used) == SCARLETT_OK);
    CHECK(cid_used > 1u);

    char sbx[32]{};
    CHECK(XSystemGetXboxLiveSandboxId(sizeof(sbx), sbx, nullptr) == SCARLETT_OK);
    CHECK(std::string_view(sbx) == "RETAIL");

    /* XUser */
    XAsyncBlock ab{};
    REQUIRE(XUserAddAsync(XUserAddOptions_AddDefaultUserSilently, &ab) == SCARLETT_OK);
    XUserHandle user = 0u;
    REQUIRE(XUserAddResult(&ab, &user) == SCARLETT_OK);
    uint64_t xuid = 0u;
    CHECK(XUserGetId(user, &xuid) == SCARLETT_OK);
    CHECK(xuid != 0u);
    char gt[64]{};
    CHECK(XUserGetGamertag(user, XUserGamertagComponent_Classic, sizeof(gt), gt, nullptr) == SCARLETT_OK);
    CHECK(std::strlen(gt) > 0u);
    XUserAgeGroup age = XUserAgeGroup_Unknown;
    CHECK(XUserGetAgeGroup(user, &age) == SCARLETT_OK);
    CHECK(age == XUserAgeGroup_Adult);

    /* XPackage */
    char pkg_id[128]{};
    CHECK(XPackageGetCurrentProcessPackageIdentifier(sizeof(pkg_id), pkg_id) == SCARLETT_OK);
    int chunk_cnt = 0;
    CHECK(XPackageEnumerateChunks(
              pkg_id, 0u, nullptr, &chunk_cnt,
              [](void *ctx, const XPackageChunkSelector *, XPackageChunkAvailability) {
                  ++(*static_cast<int *>(ctx));
                  return true;
              }) == SCARLETT_OK);
    CHECK(chunk_cnt >= 3);

    XAsyncBlock m_ab{};
    REQUIRE(XPackageMountWithUiAsync(pkg_id, &m_ab) == SCARLETT_OK);
    XPackageMountHandle mnt = 0u;
    REQUIRE(XPackageMountWithUiResult(&m_ab, &mnt) == SCARLETT_OK);
    char mpath[512]{};
    CHECK(XPackageGetMountPath(mnt, sizeof(mpath), mpath) == SCARLETT_OK);
    CHECK(std::strlen(mpath) > 0u);
    XPackageCloseMountHandle(mnt);

    /* XPersistentLocalStorage */
    uint32_t pls_sz = 0u;
    CHECK(XPersistentLocalStorageGetPathSize(&pls_sz) == SCARLETT_OK);
    std::string pls_path(pls_sz, '\0');
    CHECK(XPersistentLocalStorageGetPath(pls_sz, pls_path.data()) == SCARLETT_OK);
    uint64_t avail = 0u, total = 0u;
    CHECK(XPersistentLocalStorageGetSpaceInfo(&avail, &total) == SCARLETT_OK);
    CHECK(total > 0u && avail <= total);

    /* XStore */
    XStoreContextHandle sctx = 0u;
    REQUIRE(XStoreCreateContext(user, &sctx) == SCARLETT_OK);
    XAsyncBlock l_ab{};
    REQUIRE(XStoreAcquireLicenseForPackageAsync(sctx, pkg_id, &l_ab) == SCARLETT_OK);
    XStoreLicenseHandle lic = 0u;
    REQUIRE(XStoreAcquireLicenseForPackageResult(&l_ab, &lic) == SCARLETT_OK);
    CHECK(XStoreIsLicenseValid(lic));
    XStoreCloseLicenseHandle(lic);

    XAsyncBlock q_ab{};
    REQUIRE(XStoreQueryAssociatedProductsAsync(sctx, 0xFFu, 10u, &q_ab) == SCARLETT_OK);
    XStoreProductQueryHandle pq = 0u;
    REQUIRE(XStoreQueryAssociatedProductsResult(&q_ab, &pq) == SCARLETT_OK);
    int prod_cnt = 0;
    CHECK(XStoreEnumerateProductsQuery(
              pq, &prod_cnt,
              [](const XStoreProduct *, void *ctx) {
                  ++(*static_cast<int *>(ctx));
                  return true;
              }) == SCARLETT_OK);
    CHECK(prod_cnt >= 2);
    XStoreCloseProductsQueryHandle(pq);
    XStoreCloseContextHandle(sctx);

    /* XGameUI */
    XAsyncBlock ui_ab{};
    REQUIRE(XGameUiShowMessageDialogAsync(
                &ui_ab, "Title", "Body", "OK", "Cancel", nullptr,
                XGameUiMessageDialogButton_Second, XGameUiMessageDialogButton_Second) == SCARLETT_OK);
    XGameUiMessageDialogButton btn = XGameUiMessageDialogButton_First;
    CHECK(XGameUiShowMessageDialogResult(&ui_ab, &btn) == SCARLETT_OK);
    CHECK(btn == XGameUiMessageDialogButton_Second);

    XUserCloseHandle(user);
    XGameRuntimeUninitialize();
    std::printf("[PASS] test_xsystem_and_platform_domains\n");
    return true;
}

scarlett_hresult_t gs_set_policy(uint32_t flag, uint64_t val, const char *str = nullptr) {
    scarlett_xgr_gs_provider_params_t p{};
    p.subop = SCARLETT_XGR_GS_SET_POLICY;
    p.flags_or_bool = flag;
    p.quota_val = val;
    p.str_addr = reinterpret_cast<uint64_t>(str);
    return scarlett_xgr_unix_call(SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP, &p);
}

bool test_xgamesave_atomic_and_all_15_errors() {
    std::string tmp_saves = "/tmp/scarlett_gs_test_" + std::to_string(::getpid());
    fs::remove_all(tmp_saves);
    setenv("PROTON_XBOX_SAVES_DIR", tmp_saves.c_str(), 1);

    REQUIRE(XGameRuntimeInitialize() == SCARLETT_OK);

    XAsyncBlock uab{};
    REQUIRE(XUserAddAsync(XUserAddOptions_AddDefaultUserSilently, &uab) == SCARLETT_OK);
    XUserHandle user = 0u;
    REQUIRE(XUserAddResult(&uab, &user) == SCARLETT_OK);

    /* 1. Atomic write & persistence + no .tmp residue */
    XGameSaveProviderHandle prov = 0u;
    REQUIRE(XGameSaveInitializeProvider(user, "00000000-0000-0000-0000-000000001234", false, &prov) == SCARLETT_OK);

    XGameSaveContainerHandle cont = 0u;
    REQUIRE(XGameSaveCreateContainer(prov, "save_slot_0", &cont) == SCARLETT_OK);

    XGameSaveUpdateHandle upd = 0u;
    REQUIRE(XGameSaveCreateUpdate(cont, "Slot 0", &upd) == SCARLETT_OK);
    const std::string payload_str = "ScarlettAtomicSavePayload_v1";
    REQUIRE(XGameSaveSubmitBlobWrite(
                upd, "hero.dat",
                reinterpret_cast<const uint8_t *>(payload_str.data()),
                payload_str.size()) == SCARLETT_OK);
    REQUIRE(XGameSaveSubmitUpdate(upd) == SCARLETT_OK);

    /* Re-read and verify no .tmp file left on disk */
    fs::path save_file = fs::path(tmp_saves) / "00000000-0000-0000-0000-000000001234" / "save_slot_0" / "hero.dat";
    fs::path tmp_file = save_file;
    tmp_file += ".tmp";
    CHECK(fs::exists(save_file));
    CHECK(!fs::exists(tmp_file));

    std::vector<uint8_t> read_buf(sizeof(XGameSaveBlob) + 128u);
    const char *blob_names[] = {"hero.dat"};
    uint32_t cnt = 1u;
    REQUIRE(XGameSaveReadBlobData(
                cont, blob_names, &cnt,
                static_cast<uint32_t>(read_buf.size()),
                reinterpret_cast<XGameSaveBlob *>(read_buf.data())) == SCARLETT_OK);
    auto *rb = reinterpret_cast<XGameSaveBlob *>(read_buf.data());
    CHECK(rb->info.size == payload_str.size());
    CHECK(std::string_view(reinterpret_cast<const char *>(rb->data), rb->info.size) == payload_str);

    /* 2. All 15 official XGameSave error codes 0x80830001..0x8083000F */
    /* (1) 0x80830001 E_GS_INVALID_CONTAINER_NAME */
    XGameSaveContainerHandle bad_cont = 0u;
    CHECK(XGameSaveCreateContainer(prov, "../escape", &bad_cont) == SCARLETT_E_GS_INVALID_CONTAINER_NAME);
    CHECK(XGameSaveCreateContainer(prov, "", &bad_cont) == SCARLETT_E_GS_INVALID_CONTAINER_NAME);

    /* (2) 0x80830002 E_GS_NO_ACCESS */
    REQUIRE(gs_set_policy(5u, 1u) == SCARLETT_OK);
    CHECK(XGameSaveCreateContainer(prov, "locked", &bad_cont) == SCARLETT_E_GS_NO_ACCESS);
    REQUIRE(gs_set_policy(5u, 0u) == SCARLETT_OK);

    /* (3) 0x80830003 E_GS_OUT_OF_LOCAL_STORAGE */
    REQUIRE(gs_set_policy(2u, 10u) == SCARLETT_OK);
    {
        XGameSaveUpdateHandle u3 = 0u;
        REQUIRE(XGameSaveCreateUpdate(cont, "Slot 0", &u3) == SCARLETT_OK);
        uint8_t big16[32]{};
        REQUIRE(XGameSaveSubmitBlobWrite(u3, "b3", big16, sizeof(big16)) == SCARLETT_OK);
        CHECK(XGameSaveSubmitUpdate(u3) == SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE);
        XGameSaveCloseUpdate(u3);
    }
    REQUIRE(gs_set_policy(7u, 0u) == SCARLETT_OK);

    /* (4) 0x80830004 E_GS_USER_CANCELED */
    REQUIRE(gs_set_policy(3u, 1u) == SCARLETT_OK);
    {
        XGameSaveUpdateHandle u4 = 0u;
        REQUIRE(XGameSaveCreateUpdate(cont, "Slot 0", &u4) == SCARLETT_OK);
        CHECK(XGameSaveSubmitUpdate(u4) == SCARLETT_E_GS_USER_CANCELED);
        XGameSaveCloseUpdate(u4);
    }
    REQUIRE(gs_set_policy(3u, 0u) == SCARLETT_OK);

    /* (5) 0x80830005 E_GS_UPDATE_TOO_BIG (hard 16 MiB transaction limit) */
    {
        XGameSaveUpdateHandle u5 = 0u;
        REQUIRE(XGameSaveCreateUpdate(cont, "Slot 0", &u5) == SCARLETT_OK);
        std::vector<uint8_t> over16m(16u * 1024u * 1024u + 1u, 0xAAu);
        CHECK(XGameSaveSubmitBlobWrite(u5, "huge.bin", over16m.data(), over16m.size()) ==
              SCARLETT_E_GS_UPDATE_TOO_BIG);
        /* Also multi-blob sum > 16 MiB */
        std::vector<uint8_t> chunk9m(9u * 1024u * 1024u, 0xBBu);
        CHECK(XGameSaveSubmitBlobWrite(u5, "part1.bin", chunk9m.data(), chunk9m.size()) == SCARLETT_OK);
        CHECK(XGameSaveSubmitBlobWrite(u5, "part2.bin", chunk9m.data(), chunk9m.size()) ==
              SCARLETT_E_GS_UPDATE_TOO_BIG);
        XGameSaveCloseUpdate(u5);
    }

    /* (6) 0x80830006 E_GS_QUOTA_EXCEEDED */
    REQUIRE(gs_set_policy(1u, 20u) == SCARLETT_OK);
    {
        XGameSaveUpdateHandle u6 = 0u;
        REQUIRE(XGameSaveCreateUpdate(cont, "Slot 0", &u6) == SCARLETT_OK);
        uint8_t data30[30]{};
        REQUIRE(XGameSaveSubmitBlobWrite(u6, "q.bin", data30, sizeof(data30)) == SCARLETT_OK);
        CHECK(XGameSaveSubmitUpdate(u6) == SCARLETT_E_GS_QUOTA_EXCEEDED);
        XGameSaveCloseUpdate(u6);
    }
    REQUIRE(gs_set_policy(7u, 0u) == SCARLETT_OK);

    /* (7) 0x80830007 E_GS_PROVIDED_BUFFER_TOO_SMALL */
    {
        uint32_t small_cnt = 1u;
        XGameSaveBlob small_blob{};
        CHECK(XGameSaveReadBlobData(cont, blob_names, &small_cnt, 4u, &small_blob) ==
              SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL);
    }

    /* (8) 0x80830008 E_GS_BLOB_NOT_FOUND */
    {
        const char *missing[] = {"nonexistent.dat"};
        uint32_t mc = 1u;
        CHECK(XGameSaveReadBlobData(
                  cont, missing, &mc,
                  static_cast<uint32_t>(read_buf.size()),
                  reinterpret_cast<XGameSaveBlob *>(read_buf.data())) ==
              SCARLETT_E_GS_BLOB_NOT_FOUND);
    }

    /* (9) 0x80830009 E_GS_NO_SERVICE_CONFIGURATION */
    {
        XGameSaveProviderHandle p9 = 0u;
        CHECK(XGameSaveInitializeProvider(user, "", false, &p9) == SCARLETT_E_GS_NO_SERVICE_CONFIGURATION);
        CHECK(XGameSaveInitializeProvider(user, nullptr, false, &p9) == SCARLETT_E_GS_NO_SERVICE_CONFIGURATION);
    }

    /* (10) 0x8083000A E_GS_CONTAINER_NOT_IN_SYNC */
    REQUIRE(gs_set_policy(6u, 1u, "save_slot_0") == SCARLETT_OK);
    {
        uint32_t sc = 1u;
        CHECK(XGameSaveReadBlobData(
                  cont, blob_names, &sc,
                  static_cast<uint32_t>(read_buf.size()),
                  reinterpret_cast<XGameSaveBlob *>(read_buf.data())) ==
              SCARLETT_E_GS_CONTAINER_NOT_IN_SYNC);
    }
    REQUIRE(gs_set_policy(6u, 0u, "save_slot_0") == SCARLETT_OK);

    /* (11) 0x8083000B E_GS_CONTAINER_SYNC_FAILED */
    REQUIRE(gs_set_policy(4u, 1u) == SCARLETT_OK);
    {
        uint32_t sc = 1u;
        CHECK(XGameSaveReadBlobData(
                  cont, blob_names, &sc,
                  static_cast<uint32_t>(read_buf.size()),
                  reinterpret_cast<XGameSaveBlob *>(read_buf.data())) ==
              SCARLETT_E_GS_CONTAINER_SYNC_FAILED);
    }
    REQUIRE(gs_set_policy(4u, 0u) == SCARLETT_OK);

    /* (12) 0x8083000C E_GS_USER_NOT_REGISTERED_IN_SERVICE */
    {
        scarlett_xgr_xuser_params_t up{};
        up.subop = SCARLETT_XGR_XUSER_SET_SERVICE_REG;
        up.user_handle = user;
        up.options_or_arg = 0u;
        REQUIRE(scarlett_xgr_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &up) == SCARLETT_OK);

        XGameSaveProviderHandle p12 = 0u;
        CHECK(XGameSaveInitializeProvider(user, "00000000-0000-0000-0000-000000001234", false, &p12) ==
              SCARLETT_E_GS_USER_NOT_REGISTERED_IN_SERVICE);

        up.options_or_arg = 1u;
        REQUIRE(scarlett_xgr_unix_call(SCARLETT_XGR_UNIX_XUSER_OP, &up) == SCARLETT_OK);
    }

    /* (13) 0x8083000D E_GS_HANDLE_EXPIRED */
    CHECK(XGameSaveSubmitUpdate(upd) == SCARLETT_E_GS_HANDLE_EXPIRED);
    XGameSaveCloseUpdate(upd);

    /* (14) 0x8083000E E_GS_ASYNC_FUNCTION_REQUIRED */
    REQUIRE(XThreadSetTimeSensitive(1u) == SCARLETT_OK);
    CHECK(XThreadIsTimeSensitive() == 1u);
    {
        int64_t rem_q = 0;
        CHECK(XGameSaveGetRemainingQuota(prov, &rem_q) == SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED);
        uint32_t rc = 1u;
        CHECK(XGameSaveReadBlobData(
                  cont, blob_names, &rc,
                  static_cast<uint32_t>(read_buf.size()),
                  reinterpret_cast<XGameSaveBlob *>(read_buf.data())) ==
              SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED);
        /* Async counterpart works even when time-sensitive */
        XAsyncBlock qab{};
        REQUIRE(XGameSaveGetRemainingQuotaAsync(prov, &qab) == SCARLETT_OK);
        CHECK(XGameSaveGetRemainingQuotaResult(&qab, &rem_q) == SCARLETT_OK);
        CHECK(rem_q > 0);
    }
    REQUIRE(XThreadSetTimeSensitive(0u) == SCARLETT_OK);
    CHECK(XThreadIsTimeSensitive() == 0u);

    /* (15) 0x8083000F E_GS_PROVIDER_MISMATCH (no mixing XGameSave and XGameSaveFiles) */
    {
        XAsyncBlock fab{};
        REQUIRE(XGameSaveFilesGetFolderWithUiAsync(user, "00000000-0000-0000-0000-000000001234", &fab) == SCARLETT_OK);
        char fpath[512]{};
        CHECK(XGameSaveFilesGetFolderWithUiResult(&fab, sizeof(fpath), fpath) == SCARLETT_E_GS_PROVIDER_MISMATCH);
    }

    XGameSaveCloseContainer(cont);
    XGameSaveCloseProvider(prov);

    /* Now XGameSaveFiles works once XGameSave provider is closed, and blocks XGameSave */
    {
        XAsyncBlock fab{};
        REQUIRE(XGameSaveFilesGetFolderWithUiAsync(user, "00000000-0000-0000-0000-000000001234", &fab) == SCARLETT_OK);
        char fpath[512]{};
        CHECK(XGameSaveFilesGetFolderWithUiResult(&fab, sizeof(fpath), fpath) == SCARLETT_OK);
        CHECK(std::strlen(fpath) > 0u);

        XGameSaveProviderHandle mix_p = 0u;
        CHECK(XGameSaveInitializeProvider(user, "00000000-0000-0000-0000-000000001234", false, &mix_p) ==
              SCARLETT_E_GS_PROVIDER_MISMATCH);
        XGameSaveFilesCloseProvider(user, "00000000-0000-0000-0000-000000001234");
    }

    XUserCloseHandle(user);
    XGameRuntimeUninitialize();
    fs::remove_all(tmp_saves);
    std::printf("[PASS] test_xgamesave_atomic_and_all_15_errors\n");
    return true;
}

bool test_network_modes_httpclient_xcurl_xsapi_playfab() {
    REQUIRE(XGameRuntimeInitialize() == SCARLETT_OK);
    REQUIRE(HCInitialize(nullptr) == SCARLETT_OK);
    REQUIRE(curl_global_init(0) == CURLE_OK);

    /* 1. offline mode (default) */
    setenv(SCARLETT_ENV_NET_MODE, "offline", 1);
    {
        HCCallHandle call = 0u;
        REQUIRE(HCCallCreate(&call) == SCARLETT_OK);
        REQUIRE(HCCallRequestSetUrl(call, "GET", "http://127.0.0.1:8080/health") == SCARLETT_OK);
        XAsyncBlock ab{};
        REQUIRE(HCCallPerformAsync(call, &ab) == SCARLETT_OK);
        CHECK(XAsyncGetStatus(&ab, true) == SCARLETT_ERR_NET_OFFLINE_BLOCKED);
        const char *resp = nullptr;
        CHECK(HCCallResponseGetResponseString(call, &resp) == SCARLETT_OK);
        CHECK(resp != nullptr && std::string_view(resp).find("offline") != std::string_view::npos);
        HCCallCloseHandle(call);

        CURL *c = curl_easy_init();
        REQUIRE(c != nullptr);
        CHECK(curl_easy_setopt(c, CURLOPT_URL, "http://127.0.0.1:8080/test") == CURLE_OK);
        CHECK(curl_easy_perform(c) == CURLE_COULDNT_CONNECT);
        int64_t os_err = 0;
        CHECK(curl_easy_getinfo(c, CURLINFO_OS_ERRNO, &os_err) == CURLE_OK);
        CHECK(static_cast<scarlett_hresult_t>(static_cast<uint32_t>(os_err)) == SCARLETT_ERR_NET_OFFLINE_BLOCKED);
        curl_easy_cleanup(c);
    }

    /* 2. lan mode: blocks external 8.8.8.8, allows RFC1918 & real loopback TCP exchange */
    setenv(SCARLETT_ENV_NET_MODE, "lan", 1);
    {
        CURL *c = curl_easy_init();
        REQUIRE(c != nullptr);
        CHECK(curl_easy_setopt(c, CURLOPT_URL, "https://8.8.8.8/dns-query") == CURLE_OK);
        CHECK(curl_easy_perform(c) == CURLE_COULDNT_CONNECT);
        int64_t os_err = 0;
        CHECK(curl_easy_getinfo(c, CURLINFO_OS_ERRNO, &os_err) == CURLE_OK);
        CHECK(static_cast<scarlett_hresult_t>(static_cast<uint32_t>(os_err)) == SCARLETT_ERR_NET_LAN_POLICY_BLOCKED);
        curl_easy_cleanup(c);

        /* Spin up tiny loopback HTTP server on ephemeral port to verify real socket path */
        int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        REQUIRE(srv >= 0);
        int opt = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = 0;
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        REQUIRE(::bind(srv, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == 0);
        socklen_t alen = sizeof(addr);
        REQUIRE(::getsockname(srv, reinterpret_cast<struct sockaddr *>(&addr), &alen) == 0);
        uint16_t port = ntohs(addr.sin_port);
        REQUIRE(::listen(srv, 4) == 0);

        std::thread srv_thread([srv]() {
            for (int k = 0; k < 2; ++k) {
                int cli = ::accept(srv, nullptr, nullptr);
                if (cli >= 0) {
                    char req[1024];
                    (void)::recv(cli, req, sizeof(req), 0);
                    const char *http_resp =
                        "HTTP/1.1 200 OK\r\nContent-Length: 15\r\nConnection: close\r\n\r\n{\"lan_ok\":true}";
                    (void)::send(cli, http_resp, std::strlen(http_resp), 0);
                    ::close(cli);
                }
            }
        });

        std::string local_url = "http://127.0.0.1:" + std::to_string(port) + "/ping";
        HCCallHandle hcall = 0u;
        REQUIRE(HCCallCreate(&hcall) == SCARLETT_OK);
        REQUIRE(HCCallRequestSetUrl(hcall, "GET", local_url.c_str()) == SCARLETT_OK);
        XAsyncBlock ab{};
        REQUIRE(HCCallPerformAsync(hcall, &ab) == SCARLETT_OK);
        CHECK(XAsyncGetStatus(&ab, true) == SCARLETT_OK);
        uint32_t code = 0u;
        CHECK(HCCallResponseGetStatusCode(hcall, &code) == SCARLETT_OK);
        CHECK(code == 200u);
        const char *rstr = nullptr;
        CHECK(HCCallResponseGetResponseString(hcall, &rstr) == SCARLETT_OK);
        CHECK(rstr != nullptr && std::string_view(rstr) == "{\"lan_ok\":true}");
        HCCallCloseHandle(hcall);

        CURL *lc = curl_easy_init();
        REQUIRE(lc != nullptr);
        std::string curl_body;
        auto write_cb = +[](char *ptr, size_t size, size_t nmemb, void *ud) -> size_t {
            auto *out = static_cast<std::string *>(ud);
            out->append(ptr, size * nmemb);
            return size * nmemb;
        };
        CHECK(curl_easy_setopt(lc, CURLOPT_URL, local_url.c_str()) == CURLE_OK);
        CHECK(curl_easy_setopt(lc, CURLOPT_WRITEFUNCTION, reinterpret_cast<void *>(write_cb)) == CURLE_OK);
        CHECK(curl_easy_setopt(lc, CURLOPT_WRITEDATA, &curl_body) == CURLE_OK);
        CHECK(curl_easy_perform(lc) == CURLE_OK);
        int64_t http_code = 0;
        CHECK(curl_easy_getinfo(lc, CURLINFO_RESPONSE_CODE, &http_code) == CURLE_OK);
        CHECK(http_code == 200);
        CHECK(curl_body == "{\"lan_ok\":true}");
        curl_easy_cleanup(lc);

        srv_thread.join();
        ::close(srv);
    }

    /* 3. passthrough mode: pressure-vessel SSL_CERT_FILE / SSL_CERT_DIR */
    setenv(SCARLETT_ENV_NET_MODE, "passthrough", 1);
    {
        std::string ca_dir = "/tmp/scarlett_pv_certs_" + std::to_string(::getpid());
        fs::create_directories(ca_dir);
        std::string ca_file = ca_dir + "/ca-bundle.crt";
        {
            std::ofstream ofs(ca_file);
            ofs << "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n";
        }
        setenv("SSL_CERT_FILE", ca_file.c_str(), 1);
        setenv("SSL_CERT_DIR", ca_dir.c_str(), 1);

        CURL *pc = curl_easy_init();
        REQUIRE(pc != nullptr);
        const char *cainfo = nullptr;
        const char *capath = nullptr;
        CHECK(curl_easy_getinfo(pc, CURLINFO_CAINFO, &cainfo) == CURLE_OK);
        CHECK(curl_easy_getinfo(pc, CURLINFO_CAPATH, &capath) == CURLE_OK);
        CHECK(cainfo != nullptr && std::string_view(cainfo) == ca_file);
        CHECK(capath != nullptr && std::string_view(capath) == ca_dir);
        CHECK(curl_easy_setopt(pc, CURLOPT_URL, "https://example.invalid/api") == CURLE_OK);
        CHECK(curl_easy_perform(pc) == CURLE_OK);
        curl_easy_cleanup(pc);

        /* Missing SSL_CERT_FILE -> CURLE_SSL_CACERT_BADFILE / SCARLETT_ERR_NET_TLS_CERT_UNAVAILABLE */
        setenv("SSL_CERT_FILE", "/tmp/nonexistent_scarlett_ca.pem", 1);
        CURL *badc = curl_easy_init();
        REQUIRE(badc != nullptr);
        CHECK(curl_easy_setopt(badc, CURLOPT_URL, "https://example.invalid/api") == CURLE_OK);
        CHECK(curl_easy_perform(badc) == CURLE_SSL_CACERT_BADFILE);
        curl_easy_cleanup(badc);

        unsetenv("SSL_CERT_FILE");
        unsetenv("SSL_CERT_DIR");
        fs::remove_all(ca_dir);
    }
    setenv(SCARLETT_ENV_NET_MODE, "offline", 1);

    /* 4. XSAPI & PlayFab local stateful + auto-stubs */
    {
        XAsyncBlock uab{};
        REQUIRE(XUserAddAsync(XUserAddOptions_AddDefaultUserSilently, &uab) == SCARLETT_OK);
        XUserHandle user = 0u;
        REQUIRE(XUserAddResult(&uab, &user) == SCARLETT_OK);
        uint64_t xuid = 0u;
        REQUIRE(XUserGetId(user, &xuid) == SCARLETT_OK);

        XblInitArgs xargs{0u, "00000000-0000-0000-0000-000000001234"};
        REQUIRE(XblInitialize(&xargs) == SCARLETT_OK);
        XblContextHandle xctx = 0u;
        REQUIRE(XblContextCreateHandle(user, &xctx) == SCARLETT_OK);

        XAsyncBlock ach_upd{};
        REQUIRE(XblAchievementsUpdateAchievementAsync(xctx, xuid, "1", 100u, &ach_upd) == SCARLETT_OK);
        CHECK(XAsyncGetStatus(&ach_upd, true) == SCARLETT_OK);

        XAsyncBlock ach_get{};
        REQUIRE(XblAchievementsGetAchievementsForTitleIdAsync(
                    xctx, xuid, 1234u, 0u, true, 0u, 0u, 10u, &ach_get) == SCARLETT_OK);
        XblAchievementsResultHandle ares = 0u;
        REQUIRE(XblAchievementsGetAchievementsForTitleIdResult(&ach_get, &ares) == SCARLETT_OK);
        const XblAchievement *achs = nullptr;
        uint32_t ach_cnt = 0u;
        REQUIRE(XblAchievementsResultGetAchievements(ares, &achs, &ach_cnt) == SCARLETT_OK);
        CHECK(ach_cnt >= 1u);
        CHECK(achs[0].percentComplete == 100u);
        XblAchievementsResultCloseHandle(ares);

        StubSinkCapture cap{};
        scarlett_xgr_set_stub_sink(&test_stub_sink, &cap);
        CHECK(scarlett_failed(XblMatchmakingCreateMatchTicketAsync(xctx, "ranked", "{}", 30u, nullptr)));
        CHECK(cap.hits.load() == 1u);
        CHECK(std::string_view(cap.last.symbol_name) == "XblMatchmakingCreateMatchTicketAsync");

        /* PlayFab */
        REQUIRE(PFInitialize(0u) == SCARLETT_OK);
        PFServiceConfigHandle pcfg = 0u;
        REQUIRE(PFServiceConfigCreateHandle("https://scarlett.playfabapi.com", "ABCD1", &pcfg) == SCARLETT_OK);
        XAsyncBlock pfab{};
        REQUIRE(PFAuthenticationLoginWithXboxAsync(pcfg, user, &pfab) == SCARLETT_OK);
        PFEntityHandle pent = 0u;
        REQUIRE(PFAuthenticationLoginWithXboxResult(&pfab, &pent) == SCARLETT_OK);

        XAsyncBlock upd_pd{};
        REQUIRE(PFPlayerDataUpdateUserDataAsync(pent, "rank", "{\"tier\":5}", &upd_pd) == SCARLETT_OK);
        CHECK(PFPlayerDataUpdateUserDataResult(&upd_pd) == SCARLETT_OK);

        XAsyncBlock get_pd{};
        REQUIRE(PFPlayerDataGetUserDataAsync(pent, "rank", &get_pd) == SCARLETT_OK);
        char pdbuf[128]{};
        CHECK(PFPlayerDataGetUserDataResult(&get_pd, sizeof(pdbuf), pdbuf, nullptr) == SCARLETT_OK);
        CHECK(std::string_view(pdbuf) == "{\"tier\":5}");

        CHECK(scarlett_failed(PFMatchmakingCreateMatchmakingTicketAsync(pent, "q1", nullptr)));
        CHECK(cap.hits.load() == 2u);
        CHECK(std::string_view(cap.last.symbol_name) == "PFMatchmakingCreateMatchmakingTicketAsync");

        CHECK(scarlett_failed(HCGlobalSetHttpCallPerformFunction(nullptr, nullptr)));
        CHECK(cap.hits.load() == 3u);
        CHECK(curl_share_init() == nullptr);
        CHECK(cap.hits.load() == 4u);

        scarlett_xgr_set_stub_sink(nullptr, nullptr);
        PFEntityCloseHandle(pent);
        PFServiceConfigCloseHandle(pcfg);
        XAsyncBlock pfun{};
        (void)PFUninitializeAsync(&pfun);
        XblContextCloseHandle(xctx);
        XAsyncBlock xcl{};
        (void)XblCleanupAsync(&xcl);
        XUserCloseHandle(user);
    }

    curl_global_cleanup();
    HCCleanup();
    XGameRuntimeUninitialize();
    std::printf("[PASS] test_network_modes_httpclient_xcurl_xsapi_playfab\n");
    return true;
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_features_and_xdl_vtables();
    ok &= test_taskqueue_and_xasync();
    ok &= test_xsystem_and_platform_domains();
    ok &= test_xgamesave_atomic_and_all_15_errors();
    ok &= test_network_modes_httpclient_xcurl_xsapi_playfab();

    std::printf("Summary: %d passed, %d failed\n", g_pass, g_fail);
    return (ok && g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
