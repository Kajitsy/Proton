/*
 * Proton-Scarlett — XDL Win64 Register ABI QueryApiImpl & Auto-Stub Trampoline Tests
 *
 * Verifies:
 *   1. Win64 MS_ABI `scarlett_xdl_query_api_impl` via C++ MS_ABI pointer and
 *      direct x86-64 inline assembly (`RCX=api_id, RDX=riid, R8=out_impl`).
 *   2. Diagnostic x86-64 auto-stub trampoline in non-strict mode (`PROTON_XBOX_STRICT=0`):
 *      - Captures RCX/RDX/R8/R9, return_address, caller_rva, dll_name, symbol_name,
 *        vtable_slot, ordinal, and flags (`XDL_VTABLE`, `REMOVED_IN_ABI`, `ORDINAL_EXPORT`).
 *      - Enforces negative HRESULT (`scarlett_stub_enforce_error`, never `S_OK`).
 *   3. Strict mode (`PROTON_XBOX_STRICT=1`) via `fork()` + `pipe()`:
 *      - Verifies deterministic halt (`SCARLETT_ERR_STRICT_STUB_ABORT & 0xFF` or signal)
 *        and full diagnostic dump on stderr (DLL, symbol, caller_rva, RCX..R9, STRICT).
 */

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "scarlett_xdl_runtime.hpp"
#include "xgameruntime_xdl.hpp"
#include "xaudio2_9_xs_xdl.hpp"
#include "gameinput_xs_xdl.hpp"
#include "d3d12_xs_xdl.hpp"

#if !defined(__x86_64__)
#error "test_xdl_stub_thunk.cpp requires x86-64 target"
#endif

namespace {

using namespace scarlett::xdl;

int g_pass_count = 0;
int g_fail_count = 0;

void report_check(bool ok, const char* expr, const char* file, int line) {
    if (ok) {
        ++g_pass_count;
    } else {
        ++g_fail_count;
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, expr);
    }
}

#define CHECK(expr) report_check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define REQUIRE(expr)                                                           \
    do {                                                                        \
        const bool ok_ = static_cast<bool>(expr);                               \
        report_check(ok_, #expr, __FILE__, __LINE__);                           \
        if (!ok_) {                                                             \
            return false;                                                       \
        }                                                                       \
    } while (0)

template <typename FnPtr>
uint64_t fn_to_u64(FnPtr fn) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __extension__ reinterpret_cast<uint64_t>(fn);
#else
    return reinterpret_cast<uint64_t>(fn);
#endif
}

[[gnu::noinline]] scarlett_hresult_t call_win64_4args_asm(
    uint64_t fn_addr,
    uint64_t arg_rcx,
    uint64_t arg_rdx,
    uint64_t arg_r8,
    uint64_t arg_r9) noexcept {
    register uint64_t r_rcx __asm__("rcx") = arg_rcx;
    register uint64_t r_rdx __asm__("rdx") = arg_rdx;
    register uint64_t r_r8  __asm__("r8")  = arg_r8;
    register uint64_t r_r9  __asm__("r9")  = arg_r9;
    register uint64_t r_fn  __asm__("r11") = fn_addr;
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
        : "=a"(hr), "+r"(r_rcx), "+r"(r_rdx), "+r"(r_r8), "+r"(r_r9), "+r"(r_fn)
        :
        : "r10", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc", "memory"
    );
    return hr;
}

[[gnu::noinline]] scarlett_hresult_t call_query_api_impl_asm(
    uint64_t api_id,
    const scarlett_guid_t* riid,
    void** out_impl) noexcept {
    register uint64_t r_rcx __asm__("rcx") = api_id;
    register uint64_t r_rdx __asm__("rdx") = reinterpret_cast<uint64_t>(riid);
    register uint64_t r_r8  __asm__("r8")  = reinterpret_cast<uint64_t>(out_impl);
    register uint64_t r_fn  __asm__("r11") = fn_to_u64(&scarlett_xdl_query_api_impl);
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

struct SinkCaptureState {
    uint32_t calls = 0;
    scarlett_stub_context_t last_ctx{};
};

scarlett_stub_action_t test_stub_sink(const scarlett_stub_context_t* ctx, void* user_data) {
    auto* state = static_cast<SinkCaptureState*>(user_data);
    if (state != nullptr && ctx != nullptr) {
        ++state->calls;
        state->last_ctx = *ctx;
    }
    return scarlett_stub_resolve_action(ctx);
}

std::string to_lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

bool contains_ci(const std::string& haystack_lower, std::string_view needle_lower) {
    return haystack_lower.find(needle_lower) != std::string::npos;
}

bool test_query_api_impl_win64_abi() {
    scarlett_xdl_clear_api_tables();

    xgameruntime::IScarlettSystemDispatchProxy<kGameOs2026> sys_proxy{
        &xgameruntime::IScarlettSystemDispatchProxy<kGameOs2026>::default_vtable()
    };
    xgameruntime::IScarlettPlatformDispatchProxy<kGameOs2026> plat_proxy{
        &xgameruntime::IScarlettPlatformDispatchProxy<kGameOs2026>::default_vtable()
    };

    scarlett_xdl_register_api_table(&xgameruntime::kIID_IScarlettSystemDispatch, &sys_proxy);
    scarlett_xdl_register_api_table(&xgameruntime::kIID_IScarlettPlatformDispatch, &plat_proxy);

    const scarlett_query_api_impl_fn_t query_fn = &scarlett_xdl_query_api_impl;

    /* 1. Direct SCARLETT_MS_ABI call for System Dispatch GUID {073B7DCB-...} */
    void* sys_out_ms = nullptr;
    const scarlett_hresult_t hr_sys_ms =
        query_fn(0x1001ULL, &xgameruntime::kIID_IScarlettSystemDispatch, &sys_out_ms);
    CHECK(hr_sys_ms == SCARLETT_OK);
    CHECK(sys_out_ms == &sys_proxy);

    /* 2. Direct x86-64 inline asm call (RCX, RDX, R8) for System Dispatch GUID */
    void* sys_out_asm = nullptr;
    const scarlett_hresult_t hr_sys_asm =
        call_query_api_impl_asm(0x1002ULL, &xgameruntime::kIID_IScarlettSystemDispatch, &sys_out_asm);
    CHECK(hr_sys_asm == SCARLETT_OK);
    CHECK(sys_out_asm == &sys_proxy);

    /* 3. Direct SCARLETT_MS_ABI and inline asm calls for Platform Dispatch GUID {37E56907-...} */
    void* plat_out_ms = nullptr;
    const scarlett_hresult_t hr_plat_ms =
        query_fn(0x2001ULL, &xgameruntime::kIID_IScarlettPlatformDispatch, &plat_out_ms);
    CHECK(hr_plat_ms == SCARLETT_OK);
    CHECK(plat_out_ms == &plat_proxy);

    void* plat_out_asm = nullptr;
    const scarlett_hresult_t hr_plat_asm =
        call_query_api_impl_asm(0x2002ULL, &xgameruntime::kIID_IScarlettPlatformDispatch, &plat_out_asm);
    CHECK(hr_plat_asm == SCARLETT_OK);
    CHECK(plat_out_asm == &plat_proxy);

    /* 4. Unknown GUID -> SCARLETT_E_NOINTERFACE */
    const scarlett_guid_t unknown_guid = {
        0xDEADBEEFu, 0x1111u, 0x2222u, {0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0xAAu}
    };
    void* unknown_out = reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234));
    const scarlett_hresult_t hr_noif = call_query_api_impl_asm(0ULL, &unknown_guid, &unknown_out);
    CHECK(hr_noif == SCARLETT_E_NOINTERFACE);

    /* 5. NULL out_impl -> SCARLETT_E_INVALIDARG */
    const scarlett_hresult_t hr_null_out =
        call_query_api_impl_asm(0ULL, &xgameruntime::kIID_IScarlettSystemDispatch, nullptr);
    CHECK(hr_null_out == SCARLETT_E_INVALIDARG);

    scarlett_xdl_clear_api_tables();
    std::printf("[PASS] query_api_impl_win64_abi (MS_ABI + inline asm RCX/RDX/R8)\n");
    return true;
}

bool test_auto_stub_trampoline_non_strict() {
    scarlett_stub_set_strict_override(0);
    scarlett_stub_reset_stats();

    SinkCaptureState sink_state{};
    scarlett_stub_set_sink(&test_stub_sink, &sink_state);

    constexpr uint64_t kRcx = 0x1111222233334444ULL;
    constexpr uint64_t kRdx = 0x5555666677778888ULL;
    constexpr uint64_t kR8  = 0x9999AAAABBBBCCCCULL;
    constexpr uint64_t kR9  = 0xDDDDEEEEFFFF0001ULL;

    const auto& sys_vt = xgameruntime::IScarlettSystemDispatchProxy<kGameOs2026>::default_vtable();

    /* 1. [unimplemented] vtable slot in IScarlettSystemDispatch */
    const uint32_t expected_unimpl_slot = static_cast<uint32_t>(
        offsetof(xgameruntime::IScarlettSystemDispatchVTable<kGameOs2026>, RegisterCrashDumpCallback) /
        sizeof(void*));
    const scarlett_hresult_t hr_slot = call_win64_4args_asm(
        fn_to_u64(sys_vt.RegisterCrashDumpCallback), kRcx, kRdx, kR8, kR9);

    CHECK(scarlett_failed(hr_slot));
    CHECK(hr_slot < 0);
    CHECK(hr_slot == SCARLETT_ERR_UNIMPLEMENTED_STUB || hr_slot == SCARLETT_E_NOTIMPL);
    CHECK(sink_state.calls == 1u);
    CHECK(scarlett_stub_total_hits() == 1u);

    scarlett_stub_context_t last_ctx{};
    scarlett_stub_get_last_context(&last_ctx);

    for (const scarlett_stub_context_t* ctx : {&sink_state.last_ctx, &last_ctx}) {
        CHECK(ctx->abi_version == SCARLETT_STUB_THUNK_ABI_VERSION);
        CHECK(ctx->strict_mode == 0u);
        CHECK(ctx->return_hr == hr_slot);
        CHECK(ctx->return_hr < 0);
        CHECK(ctx->rcx == kRcx);
        CHECK(ctx->rdx == kRdx);
        CHECK(ctx->r8 == kR8);
        CHECK(ctx->r9 == kR9);
        CHECK(ctx->return_address != 0u);
        CHECK(ctx->caller_rva != 0u);
        CHECK(ctx->caller_module_base == 0u ||
              ctx->caller_rva == (ctx->return_address - ctx->caller_module_base));
        CHECK(std::string_view(ctx->dll_name) == "xgameruntime.dll");
        CHECK(std::string_view(ctx->symbol_name).find("RegisterCrashDumpCallback") !=
              std::string_view::npos);
        CHECK(ctx->vtable_slot == expected_unimpl_slot);
        CHECK((ctx->flags & SCARLETT_STUB_FLAG_XDL_VTABLE) != 0u);
    }

    /* 2. [removed(10.0.25398.0)] vtable slot in kGameOs2026 */
    const uint32_t expected_removed_slot = static_cast<uint32_t>(
        offsetof(xgameruntime::IScarlettSystemDispatchVTable<kGameOs2026>, LegacyTelemetryFlush) /
        sizeof(void*));
    const scarlett_hresult_t hr_removed = call_win64_4args_asm(
        fn_to_u64(sys_vt.LegacyTelemetryFlush), 0xAAA1ULL, 0xBBB2ULL, 0xCCC3ULL, 0xDDD4ULL);

    CHECK(scarlett_failed(hr_removed));
    CHECK(sink_state.calls == 2u);
    scarlett_stub_get_last_context(&last_ctx);
    CHECK((last_ctx.flags & SCARLETT_STUB_FLAG_REMOVED_IN_ABI) != 0u);
    CHECK((last_ctx.flags & SCARLETT_STUB_FLAG_XDL_VTABLE) != 0u);
    CHECK(std::string_view(last_ctx.dll_name) == "xgameruntime.dll");
    CHECK(std::string_view(last_ctx.symbol_name).find("LegacyTelemetryFlush") !=
          std::string_view::npos);
    CHECK(last_ctx.vtable_slot == expected_removed_slot);
    CHECK(last_ctx.rcx == 0xAAA1ULL);
    CHECK(last_ctx.rdx == 0xBBB2ULL);
    CHECK(last_ctx.r8 == 0xCCC3ULL);
    CHECK(last_ctx.r9 == 0xDDD4ULL);

    /* 3. [ordinal(2), unimplemented] export CreateAudioVolumeMeter from xaudio2_9_xs */
    const scarlett_hresult_t hr_ord2 = call_win64_4args_asm(
        fn_to_u64(&xaudio2_9::Export_CreateAudioVolumeMeter::stub_thunk),
        0x1010101010101010ULL,
        0x2020202020202020ULL,
        0x3030303030303030ULL,
        0x4040404040404040ULL);

    CHECK(scarlett_failed(hr_ord2));
    CHECK(sink_state.calls == 3u);
    scarlett_stub_get_last_context(&last_ctx);
    CHECK(last_ctx.ordinal == 2u);
    CHECK(std::string_view(last_ctx.dll_name) == "xaudio2_9.dll");
    CHECK(std::string_view(last_ctx.symbol_name).find("CreateAudioVolumeMeter") !=
          std::string_view::npos);
    CHECK((last_ctx.flags & SCARLETT_STUB_FLAG_ORDINAL_EXPORT) != 0u);
    CHECK(last_ctx.rcx == 0x1010101010101010ULL);
    CHECK(last_ctx.rdx == 0x2020202020202020ULL);

    /* 4. Unimplemented exports from gameinput_xs and d3d12_xs */
    const scarlett_hresult_t hr_gi = call_win64_4args_asm(
        fn_to_u64(&gameinput::Export_GameInputCreateDispatcher::stub_thunk),
        0x11ULL, 0x22ULL, 0x33ULL, 0x44ULL);
    CHECK(scarlett_failed(hr_gi));
    scarlett_stub_get_last_context(&last_ctx);
    CHECK(std::string_view(last_ctx.dll_name) == "GameInput.dll");

    const scarlett_hresult_t hr_d3d = call_win64_4args_asm(
        fn_to_u64(&d3d12_xs::Export_D3D12GetDebugInterface::stub_thunk),
        0x55ULL, 0x66ULL, 0x77ULL, 0x88ULL);
    CHECK(scarlett_failed(hr_d3d));
    scarlett_stub_get_last_context(&last_ctx);
    CHECK(std::string_view(last_ctx.dll_name) == "d3d12_xs.dll");

    /* 5. scarlett_stub_enforce_error never allows S_OK / SCARLETT_OK */
    CHECK(scarlett_stub_enforce_error(SCARLETT_OK) == SCARLETT_ERR_UNIMPLEMENTED_STUB);
    CHECK(scarlett_stub_enforce_error(SCARLETT_S_FALSE) == SCARLETT_ERR_UNIMPLEMENTED_STUB);
    CHECK(scarlett_stub_enforce_error(SCARLETT_E_NOTIMPL) == SCARLETT_E_NOTIMPL);
    CHECK(scarlett_stub_enforce_error(SCARLETT_ERR_UNIMPLEMENTED_STUB) ==
          SCARLETT_ERR_UNIMPLEMENTED_STUB);

    scarlett_stub_set_sink(nullptr, nullptr);
    std::printf("[PASS] auto_stub_trampoline_non_strict (RCX/RDX/R8/R9, RVA, slots, ordinal #2)\n");
    return true;
}

bool test_auto_stub_strict_mode_fork() {
    int pipefd[2] = {-1, -1};
    REQUIRE(pipe(pipefd) == 0);

    std::fflush(nullptr);
    const pid_t pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (setenv("PROTON_XBOX_STRICT", "1", 1) != 0) {
            _exit(98);
        }
        scarlett_stub_set_sink(nullptr, nullptr);
        scarlett_stub_set_strict_override(-1);
        if (scarlett_stub_is_strict_active() != 1) {
            _exit(99);
        }

        const auto& sys_vt =
            xgameruntime::IScarlettSystemDispatchProxy<kGameOs2026>::default_vtable();
        (void)call_win64_4args_asm(
            fn_to_u64(sys_vt.RegisterCrashDumpCallback),
            0x1111222233334444ULL,
            0x5555666677778888ULL,
            0x9999AAAABBBBCCCCULL,
            0xDDDDEEEEFFFF0001ULL);

        /* Must never return under PROTON_XBOX_STRICT=1 */
        _exit(0);
    }

    close(pipefd[1]);
    std::string child_stderr;
    char buf[4096];
    for (;;) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            child_stderr.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            CHECK(false);
            return false;
        }
    }

    const int expected_exit = static_cast<int>(static_cast<uint32_t>(SCARLETT_ERR_STRICT_STUB_ABORT) & 0xFFu);
    const bool halted_strictly =
        (WIFEXITED(status) && WEXITSTATUS(status) == expected_exit) ||
        WIFSIGNALED(status);
    CHECK(halted_strictly);
    CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));

    const std::string lower_err = to_lower_ascii(child_stderr);
    CHECK(contains_ci(lower_err, "xgameruntime.dll"));
    CHECK(contains_ci(lower_err, "registercrashdumpcallback"));
    CHECK(contains_ci(lower_err, "rva"));
    CHECK(contains_ci(lower_err, "rcx"));
    CHECK(contains_ci(lower_err, "rdx"));
    CHECK(contains_ci(lower_err, "r8"));
    CHECK(contains_ci(lower_err, "r9"));
    CHECK(contains_ci(lower_err, "1111222233334444"));
    CHECK(contains_ci(lower_err, "5555666677778888"));
    CHECK(contains_ci(lower_err, "9999aaaabbbbcccc"));
    CHECK(contains_ci(lower_err, "ddddeeeeffff0001"));
    CHECK(contains_ci(lower_err, "strict"));

    std::printf("[PASS] auto_stub_strict_mode_fork (PROTON_XBOX_STRICT=1 halt & stderr diagnostics)\n");
    return true;
}

} /* namespace */

int main() {
    test_query_api_impl_win64_abi();
    test_auto_stub_trampoline_non_strict();
    test_auto_stub_strict_mode_fork();

    std::printf("test_xdl_stub_thunk summary: %d passed, %d failed\n",
                g_pass_count, g_fail_count);
    return g_fail_count == 0 ? 0 : 1;
}
