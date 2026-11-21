/*
 * Proton-Scarlett — xgameruntime_unix.so WINE_UNIX_CALL Dispatcher
 *
 * Exports:
 *   - __wine_unix_call_funcs[] (dense Wine 11.0 unixlib ABI table, opcodes 0..34)
 *   - scarlett_xgr_unix_call(uint32_t opcode, void *params)
 *   - scarlett_xgr_set_stub_sink(...)
 */

#include "xgameruntime_internal.h"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <sys/syscall.h>
#include <unistd.h>

namespace scarlett::xgameruntime::unixlib {

namespace {

std::mutex g_stub_sink_mutex;
scarlett_stub_sink_fn_t g_stub_sink = nullptr;
void *g_stub_sink_user_data = nullptr;
std::atomic<uint32_t> g_stub_hit_counter{0u};
std::atomic<uint32_t> g_init_refcount{0u};

int32_t op_init(void *args) {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto *p = static_cast<scarlett_unix_init_params_t *>(args);
    p->out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    if (p->abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        p->module_id != SCARLETT_XGR_UNIX_MODULE_ID) {
        p->out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return p->out_status;
    }

    PlatformManager::instance().initialize(
        static_cast<scarlett_console_model_t>(p->console_model),
        p->strict_mode);
    TaskQueueManager::instance().initialize();
    GameSaveManager::instance().initialize();
    NetworkServicesManager::instance().initialize(
        static_cast<scarlett_net_mode_t>(p->net_mode));

    g_init_refcount.fetch_add(1u, std::memory_order_acq_rel);
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t op_shutdown(void *args) {
    (void)args;
    uint32_t prev = g_init_refcount.load(std::memory_order_acquire);
    if (prev > 0u) {
        if (g_init_refcount.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            NetworkServicesManager::instance().shutdown();
            GameSaveManager::instance().shutdown();
            PlatformManager::instance().shutdown();
            TaskQueueManager::instance().shutdown();
        }
    }
    return SCARLETT_OK;
}

int32_t op_is_feature_available(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto *p = static_cast<scarlett_xgr_feature_params_t *>(args);
    if (p->feature < static_cast<uint32_t>(SCARLETT_GRF_COUNT)) {
        p->out_available = 1u;
        p->out_status = SCARLETT_OK;
    } else {
        p->out_available = 0u;
        p->out_status = SCARLETT_E_INVALIDARG;
    }
    return p->out_status;
}

int32_t op_taskq_create(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().create(*static_cast<scarlett_xgr_taskq_create_params_t *>(args));
}

int32_t op_taskq_create_composite(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().create_composite(*static_cast<scarlett_xgr_taskq_create_params_t *>(args));
}

int32_t op_taskq_dup(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().duplicate_handle(*static_cast<scarlett_xgr_taskq_create_params_t *>(args));
}

int32_t op_taskq_close(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().close_handle(*static_cast<scarlett_xgr_taskq_create_params_t *>(args));
}

int32_t op_taskq_submit(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().submit(*static_cast<scarlett_xgr_taskq_submit_params_t *>(args));
}

int32_t op_taskq_dispatch(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().dispatch(*static_cast<scarlett_xgr_taskq_dispatch_params_t *>(args));
}

int32_t op_taskq_terminate(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().terminate(*static_cast<scarlett_xgr_taskq_terminate_params_t *>(args));
}

int32_t op_taskq_monitor(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().monitor(*static_cast<scarlett_xgr_taskq_monitor_params_t *>(args));
}

int32_t op_taskq_process_queue(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().process_queue(*static_cast<scarlett_xgr_taskq_create_params_t *>(args));
}

int32_t op_taskq_get_port(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().get_port(*static_cast<scarlett_xgr_taskq_create_params_t *>(args));
}

int32_t op_async_begin(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().async_begin(*static_cast<scarlett_xgr_async_params_t *>(args));
}

int32_t op_async_schedule(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().async_schedule(*static_cast<scarlett_xgr_async_params_t *>(args));
}

int32_t op_async_complete(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().async_complete(*static_cast<scarlett_xgr_async_params_t *>(args));
}

int32_t op_async_get_status(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().async_get_status(*static_cast<scarlett_xgr_async_params_t *>(args));
}

int32_t op_async_get_result(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().async_get_result(*static_cast<scarlett_xgr_async_params_t *>(args));
}

int32_t op_async_cancel(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return TaskQueueManager::instance().async_cancel(*static_cast<scarlett_xgr_async_params_t *>(args));
}

int32_t op_xsystem_query(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xsystem_query(*static_cast<scarlett_xgr_xsystem_params_t *>(args));
}

int32_t op_xthread_time_sensitive(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xthread_time_sensitive(*static_cast<scarlett_loader_thread_rt_params_t *>(args));
}

int32_t op_xuser_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xuser_op(*static_cast<scarlett_xgr_xuser_params_t *>(args));
}

int32_t op_xpackage_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xpackage_op(*static_cast<scarlett_xgr_xpackage_params_t *>(args));
}

int32_t op_xpls_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xpls_op(*static_cast<scarlett_xgr_xpls_params_t *>(args));
}

int32_t op_xstore_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xstore_op(*static_cast<scarlett_xgr_xstore_params_t *>(args));
}

int32_t op_xgameui_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return PlatformManager::instance().xgameui_op(*static_cast<scarlett_xgr_xgameui_params_t *>(args));
}

int32_t op_gamesave_provider_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return GameSaveManager::instance().provider_op(*static_cast<scarlett_xgr_gs_provider_params_t *>(args));
}

int32_t op_gamesave_container_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return GameSaveManager::instance().container_op(*static_cast<scarlett_xgr_gs_container_params_t *>(args));
}

int32_t op_gamesave_files_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return GameSaveManager::instance().files_op(*static_cast<scarlett_xgr_gs_files_params_t *>(args));
}

int32_t op_net_config(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return NetworkServicesManager::instance().net_config(*static_cast<scarlett_xgr_net_config_params_t *>(args));
}

int32_t op_hc_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return NetworkServicesManager::instance().hc_op(*static_cast<scarlett_xgr_hc_params_t *>(args));
}

int32_t op_xcurl_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return NetworkServicesManager::instance().xcurl_op(*static_cast<scarlett_xgr_xcurl_params_t *>(args));
}

int32_t op_xsapi_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return NetworkServicesManager::instance().xsapi_op(*static_cast<scarlett_xgr_xsapi_params_t *>(args));
}

int32_t op_playfab_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return NetworkServicesManager::instance().playfab_op(*static_cast<scarlett_xgr_playfab_params_t *>(args));
}

int32_t op_report_stub(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return dispatch_report_stub(*static_cast<scarlett_loader_report_stub_params_t *>(args));
}

} // namespace

scarlett_hresult_t dispatch_report_stub(scarlett_loader_report_stub_params_t &params) {
    const bool strict = PlatformManager::instance().is_strict_mode();
    params.ctx.strict_mode = strict ? 1u : 0u;
    if (strict) {
        params.ctx.flags |= static_cast<uint32_t>(SCARLETT_STUB_FLAG_STRICT_MODE);
        params.ctx.return_hr = SCARLETT_ERR_STRICT_STUB_ABORT;
    } else {
        params.ctx.return_hr = scarlett_stub_enforce_error(params.ctx.return_hr);
    }
    params.ctx.thread_id = static_cast<uint32_t>(::syscall(SYS_gettid));
    params.ctx.hit_count = g_stub_hit_counter.fetch_add(1u, std::memory_order_acq_rel) + 1u;

    if (params.ctx.return_address != 0u) {
        Dl_info info{};
        if (dladdr(reinterpret_cast<const void *>(static_cast<uintptr_t>(params.ctx.return_address)),
                   &info) != 0) {
            if (info.dli_fbase != nullptr) {
                uint64_t base = reinterpret_cast<uint64_t>(info.dli_fbase);
                params.ctx.caller_module_base = base;
                if (params.ctx.return_address >= base) {
                    params.ctx.caller_rva = params.ctx.return_address - base;
                }
            }
            if (info.dli_fname != nullptr && info.dli_fname[0] != '\0') {
                const char *slash = std::strrchr(info.dli_fname, '/');
                std::snprintf(params.ctx.caller_module_name,
                              sizeof(params.ctx.caller_module_name),
                              "%s",
                              slash ? (slash + 1) : info.dli_fname);
            }
        }
    }

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&params.ctx);
    scarlett_stub_sink_fn_t sink = nullptr;
    void *ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_stub_sink_mutex);
        sink = g_stub_sink;
        ud = g_stub_sink_user_data;
    }

    std::fprintf(stderr,
                 "[scarlett-xgr-stub][%s] dll=%s sym=%s ord=%u slot=%u rva=0x%" PRIx64
                 " rcx=0x%" PRIx64 " rdx=0x%" PRIx64 " r8=0x%" PRIx64
                 " r9=0x%" PRIx64 " hit=%u hr=0x%08x\n",
                 strict ? "STRICT" : "FAIL",
                 params.ctx.dll_name,
                 params.ctx.symbol_name,
                 params.ctx.ordinal,
                 params.ctx.vtable_slot,
                 params.ctx.caller_rva,
                 params.ctx.rcx,
                 params.ctx.rdx,
                 params.ctx.r8,
                 params.ctx.r9,
                 params.ctx.hit_count,
                 static_cast<uint32_t>(params.ctx.return_hr));
    std::fflush(stderr);

    if (sink != nullptr) {
        action = sink(&params.ctx, ud);
    }

    params.out_action = static_cast<uint32_t>(action);
    params.out_status = (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT)
                            ? SCARLETT_ERR_STRICT_STUB_ABORT
                            : params.ctx.return_hr;

    if (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT) {
        std::_Exit(static_cast<int>(static_cast<uint32_t>(SCARLETT_ERR_STRICT_STUB_ABORT) & 0xFFu));
    }
    return params.out_status;
}

} // namespace scarlett::xgameruntime::unixlib

extern "C" {

using wine_unixlib_entry_t = int32_t (*)(void *);

extern const wine_unixlib_entry_t __wine_unix_call_funcs[] SCARLETT_XGR_API;
const wine_unixlib_entry_t __wine_unix_call_funcs[] = {
    scarlett::xgameruntime::unixlib::op_init,                   /*  0 */
    scarlett::xgameruntime::unixlib::op_shutdown,               /*  1 */
    scarlett::xgameruntime::unixlib::op_is_feature_available, /*  2 */
    scarlett::xgameruntime::unixlib::op_taskq_create,           /*  3 */
    scarlett::xgameruntime::unixlib::op_taskq_create_composite, /*  4 */
    scarlett::xgameruntime::unixlib::op_taskq_dup,              /*  5 */
    scarlett::xgameruntime::unixlib::op_taskq_close,            /*  6 */
    scarlett::xgameruntime::unixlib::op_taskq_submit,           /*  7 */
    scarlett::xgameruntime::unixlib::op_taskq_dispatch,         /*  8 */
    scarlett::xgameruntime::unixlib::op_taskq_terminate,        /*  9 */
    scarlett::xgameruntime::unixlib::op_taskq_monitor,          /* 10 */
    scarlett::xgameruntime::unixlib::op_taskq_process_queue,    /* 11 */
    scarlett::xgameruntime::unixlib::op_taskq_get_port,         /* 12 */
    scarlett::xgameruntime::unixlib::op_async_begin,            /* 13 */
    scarlett::xgameruntime::unixlib::op_async_schedule,         /* 14 */
    scarlett::xgameruntime::unixlib::op_async_complete,         /* 15 */
    scarlett::xgameruntime::unixlib::op_async_get_status,       /* 16 */
    scarlett::xgameruntime::unixlib::op_async_get_result,       /* 17 */
    scarlett::xgameruntime::unixlib::op_async_cancel,           /* 18 */
    scarlett::xgameruntime::unixlib::op_xsystem_query,          /* 19 */
    scarlett::xgameruntime::unixlib::op_xthread_time_sensitive, /* 20 */
    scarlett::xgameruntime::unixlib::op_xuser_op,               /* 21 */
    scarlett::xgameruntime::unixlib::op_xpackage_op,            /* 22 */
    scarlett::xgameruntime::unixlib::op_xpls_op,                /* 23 */
    scarlett::xgameruntime::unixlib::op_xstore_op,              /* 24 */
    scarlett::xgameruntime::unixlib::op_xgameui_op,             /* 25 */
    scarlett::xgameruntime::unixlib::op_gamesave_provider_op,   /* 26 */
    scarlett::xgameruntime::unixlib::op_gamesave_container_op,  /* 27 */
    scarlett::xgameruntime::unixlib::op_gamesave_files_op,      /* 28 */
    scarlett::xgameruntime::unixlib::op_net_config,             /* 29 */
    scarlett::xgameruntime::unixlib::op_hc_op,                  /* 30 */
    scarlett::xgameruntime::unixlib::op_xcurl_op,               /* 31 */
    scarlett::xgameruntime::unixlib::op_xsapi_op,               /* 32 */
    scarlett::xgameruntime::unixlib::op_playfab_op,             /* 33 */
    scarlett::xgameruntime::unixlib::op_report_stub,            /* 34 */
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_XGR_UNIX_OP_COUNT,
    "__wine_unix_call_funcs table length must match SCARLETT_XGR_UNIX_OP_COUNT");

SCARLETT_XGR_API int32_t scarlett_xgr_unix_call(uint32_t opcode, void *params) {
    if (opcode >= SCARLETT_XGR_UNIX_OP_COUNT) {
        return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
    return __wine_unix_call_funcs[opcode](params);
}

SCARLETT_XGR_API void scarlett_xgr_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data) {
    std::lock_guard<std::mutex> lock(scarlett::xgameruntime::unixlib::g_stub_sink_mutex);
    scarlett::xgameruntime::unixlib::g_stub_sink = sink;
    scarlett::xgameruntime::unixlib::g_stub_sink_user_data = user_data;
}

} /* extern "C" */
