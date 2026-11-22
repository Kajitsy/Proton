#ifndef SCARLETT_DSTORAGE_XS_CORE_HPP
#define SCARLETT_DSTORAGE_XS_CORE_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "dstorage_xs_decomp.hpp"
#include "dstorage_xs_io.hpp"
#include "dstorage_xs_shaders.hpp"
#include "dstorage_xs_unix_abi.h"

namespace scarlett::dstorage_xs::unixlib {

struct BcpackPluginState {
    void *so_handle = nullptr;
    scarlett_bcpack_decoder_t *decoder = nullptr;
    uint32_t abi_version = 0;
    uint32_t header_reads = 0;
    uint64_t decoded_bytes = 0;
    uint32_t missing_plugin_errors = 0;
    std::string plugin_path;
};

class DStorageManager {
public:
    static DStorageManager &instance();

    scarlett_hresult_t init_module(scarlett_unix_init_params_t &params);
    void reset_all();

    scarlett_hresult_t open_file(scarlett_dstorage_open_file_params_t &params);
    scarlett_hresult_t close_file(scarlett_dstorage_close_file_params_t &params);
    scarlett_hresult_t get_file_info(scarlett_dstorage_file_info_params_t &params);

    scarlett_hresult_t create_queue(scarlett_dstorage_create_queue_params_t &params);
    scarlett_hresult_t destroy_queue(scarlett_dstorage_destroy_queue_params_t &params);
    scarlett_hresult_t enqueue_request(scarlett_dstorage_enqueue_request_params_t &params);
    scarlett_hresult_t enqueue_status(scarlett_dstorage_enqueue_status_params_t &params);
    scarlett_hresult_t enqueue_signal(scarlett_dstorage_enqueue_signal_params_t &params);
    scarlett_hresult_t submit_queue(scarlett_dstorage_submit_queue_params_t &params);
    scarlett_hresult_t submit_request_direct(scarlett_dstorage_submit_params_t &params);
    scarlett_hresult_t poll_completion(scarlett_dstorage_poll_params_t &params);
    scarlett_hresult_t cancel_tag(scarlett_dstorage_cancel_tag_params_t &params);
    scarlett_hresult_t query_queue(scarlett_dstorage_query_queue_params_t &params);
    scarlett_hresult_t retrieve_error_record(scarlett_dstorage_error_record_params_t &params);

    scarlett_hresult_t create_status_array(scarlett_dstorage_create_status_params_t &params);
    scarlett_hresult_t destroy_status_array(scarlett_dstorage_destroy_status_params_t &params);
    scarlett_hresult_t query_status(scarlett_dstorage_query_status_params_t &params);

    scarlett_hresult_t set_staging_size(scarlett_dstorage_staging_size_params_t &params);
    scarlett_hresult_t load_bcpack_plugin(scarlett_dstorage_bcpack_plugin_params_t &params);
    scarlett_hresult_t query_caps(scarlett_dstorage_caps_params_t &params);
    scarlett_hresult_t submit_batch(scarlett_dstorage_submit_batch_params_t &params);

    scarlett_hresult_t report_stub(scarlett_dstorage_report_stub_params_t &params);
    void set_stub_sink(scarlett_stub_sink_fn_t sink, void *user_data);

private:
    DStorageManager();
    ~DStorageManager();

    struct OpenFile {
        uint64_t handle = 0;
        int fd = -1;
        uint64_t size_bytes = 0;
        uint32_t open_flags = 0;
    };

    struct StatusEntry {
        std::atomic<uint32_t> complete{0};
        std::atomic<int32_t> hr{SCARLETT_OK};
    };

    struct StatusArray {
        uint64_t handle = 0;
        uint32_t capacity = 0;
        std::vector<StatusEntry> entries;
    };

    struct PendingCmd {
        DSTORAGE_COMMAND_TYPE type = DSTORAGE_COMMAND_TYPE_NONE;
        DSTORAGE_REQUEST request{};
        uint64_t status_array_handle = 0;
        uint32_t status_index = 0;
        uint64_t signal_addr = 0;
        uint64_t signal_value = 0;
        bool signal_is_event = false;
        bool cancelled = false;
    };

    struct ScheduledItem {
        uint64_t queue_handle = 0;
        uint32_t priority = DSTORAGE_PRIORITY_NORMAL;
        uint64_t seq = 0;
        PendingCmd cmd;
    };

    struct CompletionRecord {
        uint64_t tag = 0;
        uint64_t bytes = 0;
        scarlett_hresult_t hr = SCARLETT_OK;
    };

    struct QueueState {
        uint64_t handle = 0;
        uint64_t source_type = 0;
        uint32_t capacity = 0;
        uint32_t priority = DSTORAGE_PRIORITY_NORMAL;
        uint32_t auto_submit_threshold = 1;
        uint32_t pending_requests = 0;
        uint32_t auto_submit_count = 0;
        uint64_t total_submitted = 0;
        uint64_t total_completed = 0;
        uint64_t total_cancelled = 0;
        uint64_t next_seq = 1;
        uint64_t inflight_count = 0;
        scarlett_hresult_t batch_last_hr = SCARLETT_OK;
        std::vector<PendingCmd> pending;
        std::deque<CompletionRecord> completions;
        DSTORAGE_ERROR_RECORD error_record{};
        bool closed = false;
    };

    void start_scheduler_locked();
    void stop_scheduler();
    void scheduler_loop();

    void flush_queue_pending_locked(QueueState &q, bool is_auto_submit);
    scarlett_hresult_t execute_single_request(const DSTORAGE_REQUEST &req, uint64_t &out_bytes);
    void record_queue_error_locked(QueueState &q, scarlett_hresult_t hr, DSTORAGE_COMMAND_TYPE cmd_type, const DSTORAGE_REQUEST &req);
    scarlett_hresult_t load_bcpack_plugin_locked(const char *path, uint32_t *out_abi);
    void unload_bcpack_plugin_locked();
    void ensure_shaders_probed_locked();

    std::mutex mutex_;
    std::condition_variable cv_sched_;
    std::condition_variable cv_idle_;
    std::thread sched_thread_;
    bool sched_running_ = false;
    bool sched_stop_ = false;

    uint32_t strict_mode_ = 0;
    uint64_t staging_buffer_bytes_ = SCARLETT_DSTORAGE_DEFAULT_STAGING_BYTES;
    uint64_t next_handle_ = 1;

    std::unordered_map<uint64_t, OpenFile> files_;
    std::unordered_map<uint64_t, StatusArray *> status_arrays_;
    std::unordered_map<uint64_t, QueueState *> queues_;
    std::deque<ScheduledItem> priority_queues_[SCARLETT_DSTORAGE_PRIORITY_COUNT];
    uint32_t drr_deficit_[SCARLETT_DSTORAGE_PRIORITY_COUNT] = {0, 0, 0, 0};

    IoUringEngine io_engine_;
    DecompressionPool decomp_pool_;
    VulkanDecompProbeResult vk_probe_{};
    bool shaders_probed_ = false;
    BcpackPluginState bcpack_{};

    /* Reusable scratch buffers for batch execution (avoids hot-loop allocations) */
    std::mutex batch_scratch_mutex_;
    std::vector<uint8_t> batch_read_buf_;
    std::vector<DecompTileJob> batch_tile_jobs_;

    std::mutex stub_mutex_;
    scarlett_stub_sink_fn_t stub_sink_ = nullptr;
    void *stub_sink_user_data_ = nullptr;
};

} // namespace scarlett::dstorage_xs::unixlib

#endif /* SCARLETT_DSTORAGE_XS_CORE_HPP */
