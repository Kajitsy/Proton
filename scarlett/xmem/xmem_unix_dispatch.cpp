/*
 * Proton-Scarlett — xmem_unix.so WINE_UNIX_CALL Dispatcher & Direct Hooks
 *
 * Exports:
 *   - __wine_unix_call_funcs[] (Wine 11.0 unixlib ABI table)
 *   - scarlett_xmem_unix_call(uint32_t opcode, void *params)
 *   - scarlett_uma_flush_before_submit(...) (pre-ExecuteCommandLists hook)
 *   - scarlett_uma_set_gpu_upload_callback(...)
 *   - scarlett_uma_read_gpu_shadow(...)
 *   - scarlett_xmem_set_stub_sink(...)
 */

#include "xmem_internal.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace scarlett::xmem::unixlib {

namespace {

std::mutex g_stub_sink_mutex;
scarlett_stub_sink_fn_t g_stub_sink = nullptr;
void *g_stub_sink_user_data = nullptr;

int32_t op_init(void *args) {
    if (args == nullptr) {
        return SCARLETT_E_POINTER;
    }
    auto *p = static_cast<scarlett_unix_init_params_t *>(args);
    p->out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;

    if (p->abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        p->module_id != SCARLETT_UNIX_MOD_XMEM) {
        p->out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return p->out_status;
    }

    scarlett_hresult_t hr = MemoryManager::instance().initialize(
        static_cast<scarlett_console_model_t>(p->console_model),
        p->strict_mode);
    if (scarlett_succeeded(hr)) {
        UmaBridge::instance().initialize_from_host();
        ThreadSyncManager::instance().initialize();
    }
    p->out_status = hr;
    return hr;
}

int32_t op_reserve_range(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().reserve_or_commit_range(
        *static_cast<scarlett_xmem_reserve_params_t *>(args));
}

int32_t op_alloc_phys_pages(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().alloc_phys_pages(
        *static_cast<scarlett_xmem_alloc_pages_params_t *>(args));
}

int32_t op_free_phys_pages(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().free_phys_pages(
        *static_cast<scarlett_xmem_alloc_pages_params_t *>(args));
}

int32_t op_map_phys_pages(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().map_phys_pages(
        *static_cast<scarlett_xmem_map_pages_params_t *>(args));
}

int32_t op_unmap_phys_pages(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().unmap_phys_pages(
        *static_cast<scarlett_xmem_map_pages_params_t *>(args));
}

int32_t op_uma_export(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return UmaBridge::instance().export_region(
        *static_cast<scarlett_xmem_uma_export_params_t *>(args));
}

int32_t op_uffd_flush_dirty(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return UmaBridge::instance().flush_before_submit(
        *static_cast<scarlett_xmem_flush_dirty_params_t *>(args));
}

int32_t op_query_budget(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().query_budget(
        *static_cast<scarlett_xmem_query_budget_params_t *>(args));
}

int32_t op_free_range(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().free_range(
        *static_cast<scarlett_xmem_free_range_params_t *>(args));
}

int32_t op_virtual_query(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().virtual_query(
        *static_cast<scarlett_xmem_virtual_query_params_t *>(args));
}

int32_t op_working_set_stats(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().working_set_stats(
        *static_cast<scarlett_xmem_working_set_stats_params_t *>(args));
}

int32_t op_heap_alloc(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().heap_alloc(
        *static_cast<scarlett_xmem_heap_alloc_params_t *>(args));
}

int32_t op_heap_free(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().heap_free(
        *static_cast<scarlett_xmem_heap_free_params_t *>(args));
}

int32_t op_heap_size(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().heap_size(
        *static_cast<scarlett_xmem_heap_free_params_t *>(args));
}

int32_t op_title_mem_status(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().title_memory_status(
        *static_cast<scarlett_xmem_title_mem_status_params_t *>(args));
}

int32_t op_uma_probe(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return UmaBridge::instance().probe(
        *static_cast<scarlett_xmem_uma_probe_params_t *>(args));
}

int32_t op_uma_migrate_hot(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return UmaBridge::instance().track_hot_resource(
        *static_cast<scarlett_xmem_uma_hot_params_t *>(args));
}

int32_t op_thread_topology(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return ThreadSyncManager::instance().handle_topology(
        *static_cast<scarlett_xmem_thread_topology_params_t *>(args));
}

int32_t op_thread_time_sensitive(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return ThreadSyncManager::instance().handle_time_sensitive(
        *static_cast<scarlett_xmem_thread_ts_params_t *>(args),
        MemoryManager::instance().strict_mode());
}

int32_t op_sync_create(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return ThreadSyncManager::instance().sync_create(
        *static_cast<scarlett_xmem_sync_create_params_t *>(args));
}

int32_t op_sync_op(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return ThreadSyncManager::instance().sync_op(
        *static_cast<scarlett_xmem_sync_op_params_t *>(args));
}

int32_t op_sync_wait(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return ThreadSyncManager::instance().sync_wait(
        *static_cast<scarlett_xmem_sync_wait_params_t *>(args));
}

int32_t op_map_scatter(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return MemoryManager::instance().map_phys_pages_scatter(
        *static_cast<scarlett_xmem_scatter_map_params_t *>(args));
}

int32_t op_set_model(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    auto *p = static_cast<scarlett_xmem_set_model_params_t *>(args);
    p->out_status = MemoryManager::instance().set_model(
        static_cast<scarlett_console_model_t>(p->console_model),
        p->reset_allocations != 0u,
        p->strict_mode);
    return p->out_status;
}

int32_t op_report_stub(void *args) {
    if (args == nullptr) return SCARLETT_E_POINTER;
    return handle_report_stub(*static_cast<scarlett_loader_report_stub_params_t *>(args));
}

using unixlib_entry_t = int32_t (*)(void *);

} // namespace

scarlett_hresult_t handle_report_stub(scarlett_loader_report_stub_params_t &params) {
    params.ctx.return_hr = scarlett_stub_enforce_error(params.ctx.return_hr);
    if (params.ctx.strict_mode == 0u) {
        params.ctx.strict_mode = MemoryManager::instance().strict_mode();
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
                 "[scarlett-stub] dll=%s sym=%s ord=%u slot=%u rva=0x%" PRIx64
                 " rcx=0x%" PRIx64 " rdx=0x%" PRIx64 " r8=0x%" PRIx64
                 " r9=0x%" PRIx64 " strict=%u hr=0x%08x\n",
                 params.ctx.dll_name,
                 params.ctx.symbol_name,
                 params.ctx.ordinal,
                 params.ctx.vtable_slot,
                 params.ctx.caller_rva,
                 params.ctx.rcx,
                 params.ctx.rdx,
                 params.ctx.r8,
                 params.ctx.r9,
                 params.ctx.strict_mode,
                 static_cast<uint32_t>(params.ctx.return_hr));

    if (sink != nullptr) {
        action = sink(&params.ctx, ud);
    }

    params.out_action = static_cast<uint32_t>(action);
    params.out_status = (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT)
                            ? SCARLETT_ERR_STRICT_STUB_ABORT
                            : params.ctx.return_hr;
    return params.out_status;
}

} // namespace scarlett::xmem::unixlib

extern "C" {

using wine_unixlib_entry_t = int32_t (*)(void *);

/* Wine 11.0 unixlib export table (exact opcode order 0..25) */
extern const wine_unixlib_entry_t __wine_unix_call_funcs[] SCARLETT_XMEM_API;
const wine_unixlib_entry_t __wine_unix_call_funcs[] = {
    scarlett::xmem::unixlib::op_init,                  /*  0: SCARLETT_XMEM_UNIX_INIT */
    scarlett::xmem::unixlib::op_reserve_range,         /*  1: SCARLETT_XMEM_UNIX_RESERVE_RANGE */
    scarlett::xmem::unixlib::op_alloc_phys_pages,      /*  2: SCARLETT_XMEM_UNIX_ALLOC_PHYS_PAGES */
    scarlett::xmem::unixlib::op_free_phys_pages,       /*  3: SCARLETT_XMEM_UNIX_FREE_PHYS_PAGES */
    scarlett::xmem::unixlib::op_map_phys_pages,        /*  4: SCARLETT_XMEM_UNIX_MAP_PHYS_PAGES */
    scarlett::xmem::unixlib::op_unmap_phys_pages,      /*  5: SCARLETT_XMEM_UNIX_UNMAP_PHYS_PAGES */
    scarlett::xmem::unixlib::op_uma_export,            /*  6: SCARLETT_XMEM_UNIX_UMA_EXPORT */
    scarlett::xmem::unixlib::op_uffd_flush_dirty,      /*  7: SCARLETT_XMEM_UNIX_UFFD_FLUSH_DIRTY */
    scarlett::xmem::unixlib::op_query_budget,          /*  8: SCARLETT_XMEM_UNIX_QUERY_BUDGET */
    scarlett::xmem::unixlib::op_free_range,            /*  9: SCARLETT_XMEM_UNIX_FREE_RANGE */
    scarlett::xmem::unixlib::op_virtual_query,         /* 10: SCARLETT_XMEM_UNIX_VIRTUAL_QUERY */
    scarlett::xmem::unixlib::op_working_set_stats,     /* 11: SCARLETT_XMEM_UNIX_WORKING_SET_STATS */
    scarlett::xmem::unixlib::op_heap_alloc,            /* 12: SCARLETT_XMEM_UNIX_HEAP_ALLOC */
    scarlett::xmem::unixlib::op_heap_free,             /* 13: SCARLETT_XMEM_UNIX_HEAP_FREE */
    scarlett::xmem::unixlib::op_heap_size,             /* 14: SCARLETT_XMEM_UNIX_HEAP_SIZE */
    scarlett::xmem::unixlib::op_title_mem_status,      /* 15: SCARLETT_XMEM_UNIX_TITLE_MEM_STATUS */
    scarlett::xmem::unixlib::op_uma_probe,             /* 16: SCARLETT_XMEM_UNIX_UMA_PROBE */
    scarlett::xmem::unixlib::op_uma_migrate_hot,       /* 17: SCARLETT_XMEM_UNIX_UMA_MIGRATE_HOT */
    scarlett::xmem::unixlib::op_thread_topology,       /* 18: SCARLETT_XMEM_UNIX_THREAD_TOPOLOGY */
    scarlett::xmem::unixlib::op_thread_time_sensitive, /* 19: SCARLETT_XMEM_UNIX_THREAD_TIME_SENSITIVE */
    scarlett::xmem::unixlib::op_sync_create,           /* 20: SCARLETT_XMEM_UNIX_SYNC_CREATE */
    scarlett::xmem::unixlib::op_sync_op,               /* 21: SCARLETT_XMEM_UNIX_SYNC_OP */
    scarlett::xmem::unixlib::op_sync_wait,             /* 22: SCARLETT_XMEM_UNIX_SYNC_WAIT */
    scarlett::xmem::unixlib::op_map_scatter,           /* 23: SCARLETT_XMEM_UNIX_MAP_SCATTER */
    scarlett::xmem::unixlib::op_set_model,             /* 24: SCARLETT_XMEM_UNIX_SET_MODEL */
    scarlett::xmem::unixlib::op_report_stub,           /* 25: SCARLETT_XMEM_UNIX_REPORT_STUB */
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_XMEM_UNIX_EXT_OP_COUNT,
    "__wine_unix_call_funcs table length must match SCARLETT_XMEM_UNIX_EXT_OP_COUNT");

SCARLETT_XMEM_API int32_t scarlett_xmem_unix_call(uint32_t opcode, void *params) {
    if (opcode >= SCARLETT_XMEM_UNIX_EXT_OP_COUNT) {
        return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
    return __wine_unix_call_funcs[opcode](params);
}

SCARLETT_XMEM_API scarlett_hresult_t scarlett_uma_flush_before_submit(
    uint64_t command_list_id,
    uint64_t region_addr,
    uint64_t region_size,
    uint64_t *out_flushed_pages,
    uint64_t *out_flushed_bytes) {
    scarlett_xmem_flush_dirty_params_t params{};
    params.command_list_id = command_list_id;
    params.region_addr = region_addr;
    params.region_size = region_size;
    scarlett_hresult_t hr =
        scarlett::xmem::unixlib::UmaBridge::instance().flush_before_submit(params);
    if (out_flushed_pages != nullptr) {
        *out_flushed_pages = params.out_flushed_pages;
    }
    if (out_flushed_bytes != nullptr) {
        *out_flushed_bytes = params.out_flushed_bytes;
    }
    return hr;
}

SCARLETT_XMEM_API void scarlett_uma_set_gpu_upload_callback(
    scarlett_uma_gpu_upload_cb_t cb,
    void *user_data) {
    scarlett::xmem::unixlib::UmaBridge::instance().set_gpu_upload_callback(cb, user_data);
}

SCARLETT_XMEM_API scarlett_hresult_t scarlett_uma_read_gpu_shadow(
    uint64_t region_base_va,
    uint64_t byte_offset,
    void *dst_buffer,
    uint64_t byte_length) {
    return scarlett::xmem::unixlib::UmaBridge::instance().read_gpu_shadow(
        region_base_va, byte_offset, dst_buffer, byte_length);
}

SCARLETT_XMEM_API void scarlett_xmem_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data) {
    std::lock_guard<std::mutex> lock(scarlett::xmem::unixlib::g_stub_sink_mutex);
    scarlett::xmem::unixlib::g_stub_sink = sink;
    scarlett::xmem::unixlib::g_stub_sink_user_data = user_data;
}

} /* extern "C" */
