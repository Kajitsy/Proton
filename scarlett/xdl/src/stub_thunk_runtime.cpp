#include "scarlett_xdl_runtime.hpp"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#if defined(__linux__)
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace {

struct StubRuntimeState {
    std::mutex mutex;
    scarlett_stub_sink_fn_t sink = nullptr;
    void* sink_user_data = nullptr;
    scarlett_stub_context_t last_context{};
    std::atomic<uint32_t> hit_counter{0};
    std::atomic<int> strict_override{-1};
};

StubRuntimeState& stub_state() {
    static StubRuntimeState state;
    return state;
}

struct RegisteredApiEntry {
    scarlett_guid_t iid;
    void* table_proxy;
};

struct ApiRegistryState {
    std::mutex mutex;
    std::vector<RegisteredApiEntry> entries;
};

ApiRegistryState& api_registry() {
    static ApiRegistryState reg;
    return reg;
}

void copy_bounded(char* dst, size_t dst_cap, const char* src) noexcept {
    if (dst == nullptr || dst_cap == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

uint32_t current_thread_id() noexcept {
#if defined(__linux__)
    return static_cast<uint32_t>(syscall(SYS_gettid));
#elif defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentThreadId());
#else
    return 1u;
#endif
}

void resolve_caller_module(uint64_t return_address,
                           uint64_t& out_base,
                           uint64_t& out_rva,
                           char* out_name,
                           size_t out_name_cap) noexcept {
    out_base = 0;
    out_rva = return_address;
    copy_bounded(out_name, out_name_cap, "<unknown>");

#if defined(__linux__)
    Dl_info info{};
    if (return_address != 0 &&
        dladdr(reinterpret_cast<const void*>(return_address), &info) != 0) {
        if (info.dli_fbase != nullptr) {
            out_base = reinterpret_cast<uint64_t>(info.dli_fbase);
            if (return_address >= out_base) {
                out_rva = return_address - out_base;
            }
        }
        if (info.dli_fname != nullptr && info.dli_fname[0] != '\0') {
            const char* slash = std::strrchr(info.dli_fname, '/');
            copy_bounded(out_name, out_name_cap, slash ? (slash + 1) : info.dli_fname);
        }
    }
#elif defined(_WIN32)
    HMODULE mod = nullptr;
    if (return_address != 0 &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(return_address),
                           &mod)) {
        out_base = reinterpret_cast<uint64_t>(mod);
        if (return_address >= out_base) {
            out_rva = return_address - out_base;
        }
        char mod_path[260] = {};
        if (GetModuleFileNameA(mod, mod_path, sizeof(mod_path)) > 0) {
            const char* slash = std::strrchr(mod_path, '\\');
            copy_bounded(out_name, out_name_cap, slash ? (slash + 1) : mod_path);
        }
    }
#endif
}

bool env_strict_enabled() noexcept {
    const int ov = stub_state().strict_override.load(std::memory_order_acquire);
    if (ov == 0) return false;
    if (ov > 0) return true;
    const char* env = std::getenv(SCARLETT_ENV_STRICT);
    if (env == nullptr || env[0] == '\0') {
        return false;
    }
    return (std::strcmp(env, "1") == 0 ||
            std::strcmp(env, "true") == 0 ||
            std::strcmp(env, "TRUE") == 0 ||
            std::strcmp(env, "on") == 0);
}

void log_stub_context_stderr(const scarlett_stub_context_t& ctx) noexcept {
    std::fprintf(
        stderr,
        "[SCARLETT-STUB][%s] dll=%s symbol=%s ordinal=%u vtable_slot=%u "
        "flags=0x%08" PRIX32 " caller=%s caller_rva=0x%" PRIX64 " (ret=0x%" PRIX64 " base=0x%" PRIX64 ") "
        "RCX=0x%016" PRIX64 " RDX=0x%016" PRIX64 " R8=0x%016" PRIX64 " R9=0x%016" PRIX64
        " tid=%u hit=%u hr=0x%08" PRIX32 " (%s)\n",
        ctx.strict_mode ? "STRICT" : "FAIL",
        ctx.dll_name,
        ctx.symbol_name,
        ctx.ordinal,
        ctx.vtable_slot,
        ctx.flags,
        ctx.caller_module_name,
        ctx.caller_rva,
        ctx.return_address,
        ctx.caller_module_base,
        ctx.rcx,
        ctx.rdx,
        ctx.r8,
        ctx.r9,
        ctx.thread_id,
        ctx.hit_count,
        static_cast<uint32_t>(ctx.return_hr),
        scarlett_error_name(ctx.return_hr));
    std::fflush(stderr);
}

} /* namespace */

extern "C" {

void scarlett_stub_set_sink(scarlett_stub_sink_fn_t sink, void* user_data) {
    auto& st = stub_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.sink = sink;
    st.sink_user_data = user_data;
}

void scarlett_stub_get_last_context(scarlett_stub_context_t* out_ctx) {
    if (out_ctx == nullptr) return;
    auto& st = stub_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    *out_ctx = st.last_context;
}

void scarlett_stub_reset_stats(void) {
    auto& st = stub_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.hit_counter.store(0u, std::memory_order_release);
    std::memset(&st.last_context, 0, sizeof(st.last_context));
}

uint32_t scarlett_stub_total_hits(void) {
    return stub_state().hit_counter.load(std::memory_order_acquire);
}

void scarlett_stub_set_strict_override(int strict_override) {
    stub_state().strict_override.store(strict_override, std::memory_order_release);
}

int scarlett_stub_is_strict_active(void) {
    return env_strict_enabled() ? 1 : 0;
}

scarlett_hresult_t scarlett_stub_dispatch_win64(
    const scarlett_stub_descriptor_t* desc,
    uint64_t return_address,
    uint64_t rcx,
    uint64_t rdx,
    uint64_t r8,
    uint64_t r9) {
    auto& st = stub_state();
    const bool strict = env_strict_enabled();
    const uint32_t hit = st.hit_counter.fetch_add(1u, std::memory_order_acq_rel) + 1u;

    scarlett_stub_context_t ctx{};
    ctx.abi_version = SCARLETT_STUB_THUNK_ABI_VERSION;
    ctx.flags = (desc != nullptr) ? desc->flags : static_cast<uint32_t>(SCARLETT_STUB_FLAG_NONE);
    if (strict) {
        ctx.flags |= static_cast<uint32_t>(SCARLETT_STUB_FLAG_STRICT_MODE);
    }
    ctx.ordinal = (desc != nullptr) ? desc->ordinal : SCARLETT_STUB_ORDINAL_NONE;
    ctx.vtable_slot = (desc != nullptr) ? desc->vtable_slot : SCARLETT_STUB_SLOT_NONE;
    ctx.thread_id = current_thread_id();
    ctx.strict_mode = strict ? 1u : 0u;
    ctx.return_address = return_address;
    resolve_caller_module(return_address,
                          ctx.caller_module_base,
                          ctx.caller_rva,
                          ctx.caller_module_name,
                          sizeof(ctx.caller_module_name));
    ctx.rcx = rcx;
    ctx.rdx = rdx;
    ctx.r8 = r8;
    ctx.r9 = r9;

    const scarlett_hresult_t raw_hr =
        (desc != nullptr) ? desc->default_error : SCARLETT_ERR_UNIMPLEMENTED_STUB;
    ctx.return_hr = strict ? SCARLETT_ERR_STRICT_STUB_ABORT
                           : scarlett_stub_enforce_error(raw_hr);
    ctx.hit_count = hit;

    copy_bounded(ctx.dll_name, sizeof(ctx.dll_name),
                 (desc != nullptr && desc->dll_name != nullptr) ? desc->dll_name : "<unknown.dll>");
    copy_bounded(ctx.symbol_name, sizeof(ctx.symbol_name),
                 (desc != nullptr && desc->symbol_name != nullptr) ? desc->symbol_name : "<unknown>");

    scarlett_stub_sink_fn_t sink_fn = nullptr;
    void* sink_ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        st.last_context = ctx;
        sink_fn = st.sink;
        sink_ud = st.sink_user_data;
    }

    log_stub_context_stderr(ctx);

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&ctx);
    if (sink_fn != nullptr) {
        action = sink_fn(&ctx, sink_ud);
    }

    if (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        std::fprintf(stderr,
                     "[SCARLETT-STUB][STRICT-ABORT] terminating process with %s (0x%08" PRIX32 ")\n",
                     scarlett_error_name(SCARLETT_ERR_STRICT_STUB_ABORT),
                     static_cast<uint32_t>(SCARLETT_ERR_STRICT_STUB_ABORT));
        std::fflush(stderr);
#if defined(_WIN32)
        ExitProcess(static_cast<UINT>(SCARLETT_ERR_STRICT_STUB_ABORT));
#else
        std::_Exit(static_cast<int>(static_cast<uint32_t>(SCARLETT_ERR_STRICT_STUB_ABORT) & 0xFFu));
#endif
    }

    /* Final belt-and-suspenders guard: guarantee negative HRESULT (never success/0) */
    return scarlett_stub_enforce_error(ctx.return_hr);
}

void scarlett_xdl_register_api_table(const scarlett_guid_t* iid, void* table_proxy) {
    if (iid == nullptr || table_proxy == nullptr) return;
    auto& reg = api_registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    for (auto& entry : reg.entries) {
        if (scarlett::xdl::guid_equal(entry.iid, *iid)) {
            entry.table_proxy = table_proxy;
            return;
        }
    }
    reg.entries.push_back(RegisteredApiEntry{*iid, table_proxy});
}

void scarlett_xdl_clear_api_tables(void) {
    auto& reg = api_registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    reg.entries.clear();
}

scarlett_hresult_t SCARLETT_MS_ABI scarlett_xdl_query_api_impl(
    uint64_t api_id,
    const scarlett_guid_t* riid,
    void** out_impl) {
    (void)api_id;
    if (riid == nullptr || out_impl == nullptr) {
        return SCARLETT_E_INVALIDARG;
    }
    *out_impl = nullptr;

    auto& reg = api_registry();
    std::lock_guard<std::mutex> lock(reg.mutex);
    for (const auto& entry : reg.entries) {
        if (scarlett::xdl::guid_equal(entry.iid, *riid)) {
            *out_impl = entry.table_proxy;
            return SCARLETT_OK;
        }
    }
    return SCARLETT_E_NOINTERFACE;
}

} /* extern "C" */
