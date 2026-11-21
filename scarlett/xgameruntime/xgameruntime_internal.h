/*
 * Proton-Scarlett — Internal C++20 Declarations for xgameruntime_unix.so
 */

#ifndef SCARLETT_XGAMERUNTIME_INTERNAL_H
#define SCARLETT_XGAMERUNTIME_INTERNAL_H

#include "xgameruntime_api.h"
#include "xgameruntime_unix_abi.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace scarlett::xgameruntime::unixlib {

class TaskQueueManager {
public:
    static TaskQueueManager &instance();

    void initialize();
    void shutdown();

    scarlett_hresult_t create(scarlett_xgr_taskq_create_params_t &p);
    scarlett_hresult_t create_composite(scarlett_xgr_taskq_create_params_t &p);
    scarlett_hresult_t duplicate_handle(scarlett_xgr_taskq_create_params_t &p);
    scarlett_hresult_t close_handle(scarlett_xgr_taskq_create_params_t &p);
    scarlett_hresult_t submit(scarlett_xgr_taskq_submit_params_t &p);
    scarlett_hresult_t dispatch(scarlett_xgr_taskq_dispatch_params_t &p);
    scarlett_hresult_t terminate(scarlett_xgr_taskq_terminate_params_t &p);
    scarlett_hresult_t monitor(scarlett_xgr_taskq_monitor_params_t &p);
    scarlett_hresult_t process_queue(scarlett_xgr_taskq_create_params_t &p);
    scarlett_hresult_t get_port(scarlett_xgr_taskq_create_params_t &p);

    scarlett_hresult_t async_begin(scarlett_xgr_async_params_t &p);
    scarlett_hresult_t async_schedule(scarlett_xgr_async_params_t &p);
    scarlett_hresult_t async_complete(scarlett_xgr_async_params_t &p);
    scarlett_hresult_t async_get_status(scarlett_xgr_async_params_t &p);
    scarlett_hresult_t async_get_result(scarlett_xgr_async_params_t &p);
    scarlett_hresult_t async_cancel(scarlett_xgr_async_params_t &p);
};

class PlatformManager {
public:
    static PlatformManager &instance();

    void initialize(scarlett_console_model_t model, uint32_t strict_mode);
    void shutdown();

    scarlett_console_model_t active_console_model() const noexcept;
    bool is_strict_mode() const noexcept;

    scarlett_hresult_t xsystem_query(scarlett_xgr_xsystem_params_t &p);
    scarlett_hresult_t xthread_time_sensitive(scarlett_loader_thread_rt_params_t &p);
    bool current_thread_is_time_sensitive() const noexcept;

    scarlett_hresult_t xuser_op(scarlett_xgr_xuser_params_t &p);
    bool is_user_valid_and_registered(uint64_t user_handle) const;
    uint64_t get_user_xuid(uint64_t user_handle) const;

    scarlett_hresult_t xpackage_op(scarlett_xgr_xpackage_params_t &p);
    scarlett_hresult_t xpls_op(scarlett_xgr_xpls_params_t &p);
    scarlett_hresult_t xstore_op(scarlett_xgr_xstore_params_t &p);
    scarlett_hresult_t xgameui_op(scarlett_xgr_xgameui_params_t &p);

private:
    mutable std::mutex mutex_;
    scarlett_console_model_t default_model_{SCARLETT_MODEL_ANACONDA};
    uint32_t strict_mode_{0u};
};

class GameSaveManager {
public:
    static GameSaveManager &instance();

    void initialize();
    void shutdown();

    scarlett_hresult_t provider_op(scarlett_xgr_gs_provider_params_t &p);
    scarlett_hresult_t container_op(scarlett_xgr_gs_container_params_t &p);
    scarlett_hresult_t files_op(scarlett_xgr_gs_files_params_t &p);
};

class NetworkServicesManager {
public:
    static NetworkServicesManager &instance();

    void initialize(scarlett_net_mode_t net_mode);
    void shutdown();

    scarlett_hresult_t net_config(scarlett_xgr_net_config_params_t &p);
    scarlett_hresult_t hc_op(scarlett_xgr_hc_params_t &p);
    scarlett_hresult_t xcurl_op(scarlett_xgr_xcurl_params_t &p);
    scarlett_hresult_t xsapi_op(scarlett_xgr_xsapi_params_t &p);
    scarlett_hresult_t playfab_op(scarlett_xgr_playfab_params_t &p);
};

scarlett_hresult_t dispatch_report_stub(scarlett_loader_report_stub_params_t &p);

} // namespace scarlett::xgameruntime::unixlib

#endif /* SCARLETT_XGAMERUNTIME_INTERNAL_H */
