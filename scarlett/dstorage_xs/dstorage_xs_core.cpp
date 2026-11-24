#include "dstorage_xs_core.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace scarlett::dstorage_xs::unixlib {

namespace {

constexpr uint32_t kPriorityWeights[SCARLETT_DSTORAGE_PRIORITY_COUNT] = {
    1u,    /* LOW */
    10u,   /* NORMAL */
    100u,  /* HIGH */
    1000u  /* REALTIME */
};

scarlett_hresult_t validate_request_basic(const DSTORAGE_REQUEST &req) noexcept {
    const scarlett_dstorage_request_options_t opt{req.Options.AsUInt64};
    if (scarlett_dstorage_opt_reserved(opt) != 0u) {
        return SCARLETT_ERR_DSTORAGE_INVALID_OPTIONS;
    }
    if (req.SourceSize > SCARLETT_DSTORAGE_MAX_REQUEST_BYTES) {
        return SCARLETT_E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE;
    }
    if (req.DestinationSize > SCARLETT_DSTORAGE_MAX_REQUEST_BYTES) {
        return SCARLETT_E_DSTORAGE_INVALID_DESTINATION_SIZE;
    }
    if (req.DestinationAddress == 0u && req.DestinationSize > 0u) {
        return SCARLETT_E_POINTER;
    }
    if (req.Options.SourceType == DSTORAGE_REQUEST_SOURCE_MEMORY) {
        if (req.SourceAddress == 0u && req.SourceSize > 0u) {
            return SCARLETT_E_POINTER;
        }
    } else {
        if (req.FileHandle == 0u) {
            return SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE;
        }
    }
    if (req.Options.ZlibDecompress == 0u && req.Options.BcpackMode == 0u &&
        req.SourceSize > req.DestinationSize) {
        return SCARLETT_E_DSTORAGE_INVALID_DESTINATION_SIZE;
    }
    return SCARLETT_OK;
}

bool read_bcpack_header_telemetry(
    const uint8_t *src,
    uint32_t src_size,
    uint32_t bcpack_mode,
    BcpackPluginState &state,
    scarlett_bcpack_header_t &out_hdr) noexcept {
    out_hdr = scarlett_bcpack_header_t{};
    if (src == nullptr || src_size < sizeof(scarlett_bcpack_header_t)) {
        return false;
    }
    std::memcpy(&out_hdr, src, sizeof(out_hdr));
    if (out_hdr.magic == SCARLETT_DSTORAGE_BCPACK_MAGIC) {
        ++state.header_reads;
        if (out_hdr.bcpack_mode == 0u) {
            out_hdr.bcpack_mode = static_cast<uint16_t>(bcpack_mode);
        }
        return true;
    }
    return false;
}

bool iequals_ascii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

bool split_normalized_components(
    std::string_view rel,
    std::vector<std::string> &out) noexcept {
    out.clear();
    size_t pos = 0;
    while (pos < rel.size()) {
        while (pos < rel.size() && (rel[pos] == '\\' || rel[pos] == '/')) {
            ++pos;
        }
        if (pos >= rel.size()) {
            break;
        }
        size_t next = pos;
        while (next < rel.size() && rel[next] != '\\' && rel[next] != '/') {
            ++next;
        }
        std::string_view comp = rel.substr(pos, next - pos);
        pos = next;
        if (comp == ".") {
            continue;
        }
        if (comp == "..") {
            if (out.empty()) {
                return false;
            }
            out.pop_back();
            continue;
        }
        out.emplace_back(comp);
    }
    return true;
}

bool resolve_case_insensitive_under(
    const std::filesystem::path &base,
    const std::vector<std::string> &components,
    std::filesystem::path &out_path) noexcept {
    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) {
        return false;
    }
    std::filesystem::path cur = base;
    for (const std::string &comp : components) {
        std::filesystem::path direct = cur / comp;
        if (std::filesystem::exists(direct, ec)) {
            cur = std::move(direct);
            continue;
        }
        bool matched = false;
        std::filesystem::directory_iterator it(cur, ec);
        if (ec) {
            return false;
        }
        for (const auto &entry : it) {
            const std::string name = entry.path().filename().string();
            if (iequals_ascii(name, comp)) {
                cur = entry.path();
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    if (!std::filesystem::is_regular_file(cur, ec)) {
        return false;
    }
    out_path = std::move(cur);
    return true;
}

std::string resolve_dstorage_path(const std::string &input) noexcept {
    if (input.empty()) {
        return {};
    }

    std::string_view sv(input);
    bool is_xbox_vfs = false;
    char drive_letter = '\0';
    if (sv.size() >= 4 && (sv.substr(0, 4) == "\\\\?\\" || sv.substr(0, 4) == "\\\\.\\")) {
        sv.remove_prefix(4);
    }
    if (sv.size() >= 5 && iequals_ascii(sv.substr(0, 5), "GAME:")) {
        sv.remove_prefix(5);
        is_xbox_vfs = true;
    } else if (sv.size() >= 2 &&
               (sv[0] == 'D' || sv[0] == 'd' || sv[0] == 'G' || sv[0] == 'g') &&
               sv[1] == ':') {
        drive_letter = static_cast<char>(std::tolower(static_cast<unsigned char>(sv[0])));
        sv.remove_prefix(2);
        is_xbox_vfs = true;
    }

    if (is_xbox_vfs) {
        std::vector<std::string> comps;
        if (!split_normalized_components(sv, comps) || comps.empty()) {
            return {};
        }

        std::vector<std::filesystem::path> roots;
        if (const char *ov = std::getenv("PROTON_XBOX_OVERLAY_ROOT"); ov != nullptr && ov[0] != '\0') {
            roots.emplace_back(ov);
        }
        if (const char *ov2 = std::getenv("SCARLETT_VFS_OVERLAY_DIR"); ov2 != nullptr && ov2[0] != '\0') {
            roots.emplace_back(ov2);
        }
        if (const char *pfx = std::getenv("WINEPREFIX"); pfx != nullptr && pfx[0] != '\0') {
            if (drive_letter == 'g') {
                roots.emplace_back(std::filesystem::path(pfx) / "dosdevices" / "g:");
            }
        }
        if (const char *gr = std::getenv("PROTON_XBOX_GAME_ROOT"); gr != nullptr && gr[0] != '\0') {
            roots.emplace_back(gr);
        }
        if (const char *gr2 = std::getenv("SCARLETT_GAME_ROOT"); gr2 != nullptr && gr2[0] != '\0') {
            roots.emplace_back(gr2);
        }
        if (const char *pfx = std::getenv("WINEPREFIX"); pfx != nullptr && pfx[0] != '\0') {
            roots.emplace_back(std::filesystem::path(pfx) / "dosdevices" / "d:");
            if (drive_letter != 'g') {
                roots.emplace_back(std::filesystem::path(pfx) / "dosdevices" / "g:");
            }
        }
        std::error_code ec;
        std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec && !cwd.empty()) {
            roots.push_back(std::move(cwd));
        }

        for (const auto &root : roots) {
            std::filesystem::path resolved;
            if (resolve_case_insensitive_under(root, comps, resolved)) {
                return resolved.string();
            }
        }
        return {};
    }

    std::string normalized = input;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    return normalized;
}

} // namespace

DStorageManager &DStorageManager::instance() {
    /* Intentional immortal singleton: Wine NtTerminateProcess/exit() kills threads
     * before __cxa_finalize, which would otherwise std::terminate() in ~thread(). */
    static DStorageManager *inst = new DStorageManager();
    return *inst;
}

DStorageManager::DStorageManager() = default;

DStorageManager::~DStorageManager() {
    reset_all();
}

void DStorageManager::start_scheduler_locked() {
    if (sched_running_) {
        return;
    }
    sched_stop_ = false;
    sched_running_ = true;
    sched_thread_ = std::thread(&DStorageManager::scheduler_loop, this);
}

void DStorageManager::stop_scheduler() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!sched_running_) {
            return;
        }
        sched_stop_ = true;
        pe_signals_stop_ = true;
        cv_sched_.notify_all();
        cv_pe_signal_.notify_all();
    }
    if (sched_thread_.joinable()) {
        sched_thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sched_running_ = false;
}

void DStorageManager::reset_all() {
    stop_scheduler();
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t p = 0; p < SCARLETT_DSTORAGE_PRIORITY_COUNT; ++p) {
        priority_queues_[p].clear();
        drr_deficit_[p] = 0u;
    }
    for (auto &kv : queues_) {
        delete kv.second;
    }
    queues_.clear();
    for (auto &kv : status_arrays_) {
        delete kv.second;
    }
    status_arrays_.clear();
    files_.clear();
    pending_pe_signals_.clear();
    pe_signals_stop_ = false;
    seen_opt_combos_mask_ = 0u;
    unload_bcpack_plugin_locked();
    io_engine_.shutdown();
    decomp_pool_.shutdown();
    staging_buffer_bytes_ = SCARLETT_DSTORAGE_DEFAULT_STAGING_BYTES;
    next_handle_ = 1u;
}

scarlett_hresult_t DStorageManager::init_module(scarlett_unix_init_params_t &params) {
    params.out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    if (params.abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        params.module_id != SCARLETT_UNIX_MOD_DSTORAGE) {
        params.out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return params.out_status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pe_signals_stop_ = false;
    const char *env_strict = std::getenv(SCARLETT_ENV_STRICT);
    if (env_strict != nullptr && env_strict[0] == '1') {
        strict_mode_ = 1u;
    } else {
        strict_mode_ = params.strict_mode != 0u ? 1u : 0u;
    }

    io_engine_.initialize();
    decomp_pool_.initialize();
    start_scheduler_locked();

    const char *plugin_env = std::getenv(SCARLETT_ENV_BCPACK_PLUGIN);
    if (plugin_env != nullptr && plugin_env[0] != '\0') {
        uint32_t abi_ver = 0;
        (void)load_bcpack_plugin_locked(plugin_env, &abi_ver);
    }

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

void DStorageManager::ensure_shaders_probed_locked() {
    if (shaders_probed_) {
        return;
    }
    vk_probe_ = probe_vulkan_decompression();

    std::vector<uint32_t> spv;
    std::string diag;
    vk_probe_.vkdeflate_spirv_valid = compile_and_validate_spirv_v14(
        generate_vkdeflate_compute_glsl(), "vkdeflate", spv, &diag);
    vk_probe_.swizzle_spirv_valid = compile_and_validate_spirv_v14(
        generate_swizzle_compute_glsl(), "swizzle", spv, &diag);
    shaders_probed_ = true;
}

void DStorageManager::unload_bcpack_plugin_locked() {
    if (bcpack_.decoder != nullptr && bcpack_.decoder->destroy != nullptr) {
        bcpack_.decoder->destroy(bcpack_.decoder);
        bcpack_.decoder = nullptr;
    }
    if (bcpack_.so_handle != nullptr) {
        ::dlclose(bcpack_.so_handle);
        bcpack_.so_handle = nullptr;
    }
    bcpack_.abi_version = 0u;
    bcpack_.plugin_path.clear();
}

scarlett_hresult_t DStorageManager::load_bcpack_plugin_locked(const char *path, uint32_t *out_abi) {
    if (out_abi != nullptr) {
        *out_abi = 0u;
    }
    unload_bcpack_plugin_locked();

    if (path == nullptr || path[0] == '\0') {
        return SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING;
    }

    void *so = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (so == nullptr) {
        return SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING;
    }

    auto create_fn = reinterpret_cast<scarlett_bcpack_create_decoder_fn_t>(
        ::dlsym(so, SCARLETT_BCPACK_CREATE_SYMBOL));
    if (create_fn == nullptr) {
        ::dlclose(so);
        return SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH;
    }

    scarlett_bcpack_decoder_t *dec = nullptr;
    const scarlett_hresult_t hr = create_fn(SCARLETT_BCPACK_PLUGIN_ABI_VERSION, &dec);
    if (scarlett_failed(hr) || dec == nullptr) {
        ::dlclose(so);
        return scarlett_failed(hr) ? hr : SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH;
    }
    if (dec->abi_version != SCARLETT_BCPACK_PLUGIN_ABI_VERSION || dec->decode == nullptr) {
        if (dec->destroy != nullptr) {
            dec->destroy(dec);
        }
        ::dlclose(so);
        return SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH;
    }

    bcpack_.so_handle = so;
    bcpack_.decoder = dec;
    bcpack_.abi_version = dec->abi_version;
    bcpack_.plugin_path = path;
    if (out_abi != nullptr) {
        *out_abi = dec->abi_version;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::load_bcpack_plugin(
    scarlett_dstorage_bcpack_plugin_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params.out_plugin_abi_version = 0u;
    params.out_loaded = 0u;

    std::string path_str;
    if (params.plugin_path_utf8_addr != 0u && params.plugin_path_len > 0u) {
        const char *p = reinterpret_cast<const char *>(
            static_cast<uintptr_t>(params.plugin_path_utf8_addr));
        path_str.assign(p, params.plugin_path_len);
    } else {
        const char *env = std::getenv(SCARLETT_ENV_BCPACK_PLUGIN);
        if (env != nullptr) {
            path_str = env;
        }
    }

    uint32_t abi = 0u;
    scarlett_hresult_t hr = load_bcpack_plugin_locked(
        path_str.empty() ? nullptr : path_str.c_str(), &abi);
    params.out_plugin_abi_version = abi;
    params.out_loaded = scarlett_succeeded(hr) ? 1u : 0u;
    params.out_status = hr;
    return hr;
}

scarlett_hresult_t DStorageManager::open_file(scarlett_dstorage_open_file_params_t &params) {
    params.out_file_handle = 0u;
    params.out_file_size = 0u;
    if (params.path_utf8_addr == 0u || params.path_utf8_len == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    const char *raw = reinterpret_cast<const char *>(
        static_cast<uintptr_t>(params.path_utf8_addr));
    std::string raw_path(raw, params.path_utf8_len);
    std::string path = resolve_dstorage_path(raw_path);
    if (path.empty()) {
        params.out_status = SCARLETT_E_DSTORAGE_FILE_NOT_OPEN;
        return params.out_status;
    }

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        params.out_status = SCARLETT_E_DSTORAGE_FILE_NOT_OPEN;
        return params.out_status;
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        params.out_status = SCARLETT_E_FAIL;
        return params.out_status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t h = next_handle_++;
    auto f = std::make_shared<OpenFile>();
    f->handle = h;
    f->fd = fd;
    f->size_bytes = static_cast<uint64_t>(st.st_size);
    f->open_flags = params.open_flags;
    files_.emplace(h, f);

    params.out_file_handle = h;
    params.out_file_size = f->size_bytes;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::close_file(scarlett_dstorage_close_file_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(params.file_handle);
    if (it == files_.end()) {
        params.out_status = SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE;
        return params.out_status;
    }
    files_.erase(it);
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::get_file_info(scarlett_dstorage_file_info_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = files_.find(params.file_handle);
    if (it == files_.end()) {
        params.out_status = SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE;
        return params.out_status;
    }
    params.out_file_size_bytes = it->second->size_bytes;
    params.out_open_flags = it->second->open_flags;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::create_status_array(
    scarlett_dstorage_create_status_params_t &params) {
    params.out_status_array_handle = 0u;
    if (params.capacity == 0u || params.capacity > SCARLETT_DSTORAGE_MAX_QUEUE_CAPACITY) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t h = next_handle_++;
    auto *arr = new StatusArray();
    arr->handle = h;
    arr->capacity = params.capacity;
    arr->entries = std::vector<StatusEntry>(params.capacity);
    status_arrays_.emplace(h, arr);
    params.out_status_array_handle = h;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::destroy_status_array(
    scarlett_dstorage_destroy_status_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = status_arrays_.find(params.status_array_handle);
    if (it == status_arrays_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    delete it->second;
    status_arrays_.erase(it);
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::query_status(scarlett_dstorage_query_status_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = status_arrays_.find(params.status_array_handle);
    if (it == status_arrays_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    StatusArray *arr = it->second;
    if (params.index >= arr->capacity) {
        params.out_status = SCARLETT_E_DSTORAGE_INDEX_BOUND;
        return params.out_status;
    }
    params.out_is_complete = arr->entries[params.index].complete.load(std::memory_order_acquire);
    params.out_entry_hresult = arr->entries[params.index].hr.load(std::memory_order_acquire);
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::create_queue(scarlett_dstorage_create_queue_params_t &params) {
    params.out_queue_handle = 0u;
    if (params.capacity < SCARLETT_DSTORAGE_MIN_QUEUE_CAPACITY ||
        params.capacity > SCARLETT_DSTORAGE_MAX_QUEUE_CAPACITY) {
        params.out_status = SCARLETT_E_DSTORAGE_INVALID_QUEUE_CAPACITY;
        return params.out_status;
    }
    if (params.priority >= SCARLETT_DSTORAGE_PRIORITY_COUNT) {
        params.out_status = SCARLETT_E_DSTORAGE_INVALID_MEMORY_QUEUE_PRIORITY;
        return params.out_status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    io_engine_.initialize();
    decomp_pool_.initialize();
    start_scheduler_locked();

    const uint64_t h = next_handle_++;
    auto *q = new QueueState();
    q->handle = h;
    q->source_type = params.source_type;
    q->capacity = params.capacity;
    q->priority = params.priority;
    q->auto_submit_threshold = std::max(1u, params.capacity / 2u);
    queues_.emplace(h, q);

    params.out_queue_handle = h;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::destroy_queue(scarlett_dstorage_destroy_queue_params_t &params) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;
    q->closed = true;
    /* Wait for any in-flight scheduled items of this queue to drain */
    cv_idle_.wait(lock, [&]() { return q->inflight_count == 0u; });
    delete q;
    queues_.erase(it);
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

void DStorageManager::record_queue_error_locked(
    QueueState &q,
    scarlett_hresult_t hr,
    DSTORAGE_COMMAND_TYPE cmd_type,
    const DSTORAGE_REQUEST &req) {
    if (q.error_record.FailureCount == 0u) {
        q.error_record.FirstFailure.HResult = hr;
        q.error_record.FirstFailure.CommandType = static_cast<uint32_t>(cmd_type);
        q.error_record.FirstFailure.Request = req;
    }
    ++q.error_record.FailureCount;
    q.batch_last_hr = hr;
    std::fprintf(
        stderr,
        "[scarlett:dstorage_xs] queue error: q=0x%llx cmd=%u hr=0x%08x Zlib=%u Bcpack=%u Swizzle=%u src=%u dst=%u offset=0x%llx\n",
        static_cast<unsigned long long>(q.handle),
        static_cast<unsigned>(cmd_type),
        static_cast<unsigned>(hr),
        static_cast<unsigned>(req.Options.ZlibDecompress),
        static_cast<unsigned>(req.Options.BcpackMode),
        static_cast<unsigned>(req.Options.SwizzleMode),
        req.SourceSize,
        req.DestinationSize,
        static_cast<unsigned long long>(req.FileOffset));
    std::fflush(stderr);
}

void DStorageManager::flush_queue_pending_locked(QueueState &q, bool is_auto_submit) {
    if (q.pending.empty()) {
        return;
    }
    if (is_auto_submit) {
        ++q.auto_submit_count;
    }

    auto &pq = priority_queues_[q.priority];
    for (auto &cmd : q.pending) {
        if (cmd.cancelled) {
            continue;
        }
        ScheduledItem item{};
        item.queue_handle = q.handle;
        item.priority = q.priority;
        item.seq = q.next_seq++;
        item.cmd = cmd;
        pq.push_back(item);
        ++q.inflight_count;
        if (cmd.type == DSTORAGE_COMMAND_TYPE_REQUEST) {
            ++q.total_submitted;
        }
    }
    q.pending.clear();
    q.pending_requests = 0u;
    cv_sched_.notify_one();
}

scarlett_hresult_t DStorageManager::enqueue_request(
    scarlett_dstorage_enqueue_request_params_t &params) {
    params.out_auto_submitted = 0u;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;
    if (q->closed) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (q->pending_requests >= q->capacity) {
        params.out_status = SCARLETT_E_FAIL;
        return params.out_status;
    }

    DSTORAGE_REQUEST req{};
    req.Options.AsUInt64 = params.options.raw;
    req.FileHandle = params.file_handle;
    req.FileOffset = params.file_offset;
    req.SourceAddress = params.src_addr;
    req.SourceSize = params.src_size;
    req.DestinationSize = params.dst_size;
    req.DestinationAddress = params.dst_addr;
    req.CancellationTag = params.cancellation_tag;
    req.Name = reinterpret_cast<const char *>(static_cast<uintptr_t>(params.name_utf8_addr));
    req.SwizzleSubresourceIndex = params.swizzle_subresource_index;
    req.SwizzleReserved = params.swizzle_reserved;

    const uint32_t opt_key =
        (static_cast<uint32_t>(req.Options.ZlibDecompress) << 4) |
        static_cast<uint32_t>(req.Options.BcpackMode);
    const uint32_t opt_bit = 1u << opt_key;
    if ((seen_opt_combos_mask_ & opt_bit) == 0u) {
        seen_opt_combos_mask_ |= opt_bit;
        std::fprintf(
            stderr,
            "[scarlett:dstorage_xs] first EnqueueRequest options: ZlibDecompress=%u BcpackMode=%u SwizzleMode=%u SourceType=%u src_size=%u dst_size=%u\n",
            static_cast<unsigned>(req.Options.ZlibDecompress),
            static_cast<unsigned>(req.Options.BcpackMode),
            static_cast<unsigned>(req.Options.SwizzleMode),
            static_cast<unsigned>(req.Options.SourceType),
            req.SourceSize,
            req.DestinationSize);
        std::fflush(stderr);
    }

    PendingCmd cmd{};
    cmd.type = DSTORAGE_COMMAND_TYPE_REQUEST;
    cmd.request = req;
    q->pending.push_back(cmd);
    ++q->pending_requests;

    if (q->pending_requests >= q->auto_submit_threshold) {
        flush_queue_pending_locked(*q, true);
        params.out_auto_submitted = 1u;
    }

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::enqueue_status(
    scarlett_dstorage_enqueue_status_params_t &params) {
    params.out_auto_submitted = 0u;
    std::lock_guard<std::mutex> lock(mutex_);
    auto qit = queues_.find(params.queue_handle);
    auto sit = status_arrays_.find(params.status_array_handle);
    if (qit == queues_.end() || sit == status_arrays_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = qit->second;
    if (q->closed) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (q->pending_requests >= q->capacity) {
        params.out_status = SCARLETT_E_FAIL;
        return params.out_status;
    }
    if (params.index >= sit->second->capacity) {
        params.out_status = SCARLETT_E_DSTORAGE_INDEX_BOUND;
        return params.out_status;
    }

    sit->second->entries[params.index].complete.store(0u, std::memory_order_release);
    sit->second->entries[params.index].hr.store(SCARLETT_OK, std::memory_order_release);

    PendingCmd cmd{};
    cmd.type = DSTORAGE_COMMAND_TYPE_STATUS;
    cmd.status_array_handle = params.status_array_handle;
    cmd.status_index = params.index;
    q->pending.push_back(cmd);
    ++q->pending_requests;

    if (q->pending_requests >= q->auto_submit_threshold) {
        flush_queue_pending_locked(*q, true);
        params.out_auto_submitted = 1u;
    }

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::enqueue_signal(
    scarlett_dstorage_enqueue_signal_params_t &params) {
    params.out_auto_submitted = 0u;
    std::lock_guard<std::mutex> lock(mutex_);
    auto qit = queues_.find(params.queue_handle);
    if (qit == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = qit->second;
    if (q->closed) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (q->pending_requests >= q->capacity) {
        params.out_status = SCARLETT_E_FAIL;
        return params.out_status;
    }

    PendingCmd cmd{};
    cmd.type = DSTORAGE_COMMAND_TYPE_SIGNAL;
    cmd.signal_addr = params.fence_or_event_addr;
    cmd.signal_value = params.value;
    cmd.signal_mode = params.is_event;
    q->pending.push_back(cmd);
    ++q->pending_requests;

    if (q->pending_requests >= q->auto_submit_threshold) {
        flush_queue_pending_locked(*q, true);
        params.out_auto_submitted = 1u;
    }

    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::submit_queue(
    scarlett_dstorage_submit_queue_params_t &params) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;
    flush_queue_pending_locked(*q, false);

    if (params.wait_for_idle != 0u) {
        cv_idle_.wait(lock, [&]() { return q->inflight_count == 0u; });
    }
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::cancel_tag(scarlett_dstorage_cancel_tag_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;
    uint32_t cancelled = 0u;

    for (auto &cmd : q->pending) {
        if (!cmd.cancelled && cmd.type == DSTORAGE_COMMAND_TYPE_REQUEST &&
            (cmd.request.CancellationTag & params.mask) == params.value) {
            cmd.cancelled = true;
            ++cancelled;
            if (q->pending_requests > 0u) {
                --q->pending_requests;
            }
        }
    }

    auto &pq = priority_queues_[q->priority];
    for (auto &item : pq) {
        if (!item.cmd.cancelled && item.queue_handle == q->handle &&
            item.cmd.type == DSTORAGE_COMMAND_TYPE_REQUEST &&
            (item.cmd.request.CancellationTag & params.mask) == params.value) {
            item.cmd.cancelled = true;
            ++cancelled;
        }
    }

    q->total_cancelled += cancelled;
    params.out_cancelled_count = cancelled;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::query_queue(scarlett_dstorage_query_queue_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;
    params.out_source_type = q->source_type;
    params.out_capacity = q->capacity;
    params.out_priority = q->priority;
    params.out_pending_count = q->pending_requests;
    params.out_empty_slots = (q->capacity > q->pending_requests)
                                 ? (q->capacity - q->pending_requests)
                                 : 0u;
    params.out_until_auto_submit = (q->auto_submit_threshold > q->pending_requests)
                                       ? (q->auto_submit_threshold - q->pending_requests)
                                       : 0u;
    params.out_auto_submit_count = q->auto_submit_count;
    params.out_submitted_requests = q->total_submitted;
    params.out_completed_requests = q->total_completed;
    params.out_cancelled_requests = q->total_cancelled;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::retrieve_error_record(
    scarlett_dstorage_error_record_params_t &params) {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;
    /* Drain inflight work so error record reflects completed commands */
    cv_idle_.wait(lock, [&]() { return q->inflight_count == 0u; });

    if (params.out_record_addr != 0u) {
        auto *dst = reinterpret_cast<DSTORAGE_ERROR_RECORD *>(
            static_cast<uintptr_t>(params.out_record_addr));
        *dst = q->error_record;
    }
    q->error_record = DSTORAGE_ERROR_RECORD{};
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::set_staging_size(
    scarlett_dstorage_staging_size_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (params.size_bytes == 0u || params.size_bytes > SCARLETT_DSTORAGE_MAX_STAGING_BYTES) {
        params.out_active_size_bytes = staging_buffer_bytes_;
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    staging_buffer_bytes_ = params.size_bytes;
    params.out_active_size_bytes = staging_buffer_bytes_;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::execute_single_request(
    const DSTORAGE_REQUEST &req,
    uint64_t &out_bytes) {
    out_bytes = 0u;
    scarlett_hresult_t hr = validate_request_basic(req);
    if (scarlett_failed(hr)) {
        return hr;
    }

    /* Resolve file descriptor if SourceType == File (hold shared_ptr across I/O to prevent close_file race) */
    std::shared_ptr<OpenFile> file_ref;
    int fd = -1;
    uint64_t file_size = 0u;
    if (req.Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto fit = files_.find(req.FileHandle);
        if (fit == files_.end()) {
            return SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE;
        }
        file_ref = fit->second;
        fd = file_ref->fd;
        file_size = file_ref->size_bytes;
        if (req.FileOffset > file_size ||
            static_cast<uint64_t>(req.SourceSize) > file_size - req.FileOffset) {
            return SCARLETT_E_DSTORAGE_END_OF_FILE;
        }
    }

    const bool need_decompress = (req.Options.ZlibDecompress != 0u);
    const bool need_bcpack = (req.Options.BcpackMode != 0u);
    const bool need_swizzle = (req.Options.SwizzleMode != DSTORAGE_SWIZZLE_NONE);
    const bool dst_phys = (req.Options.DestinationIsPhysicalPages != 0u);
    const bool src_phys = (req.Options.SourceIsPhysicalPages != 0u);

    /* Fast direct scatter path: uncompressed file -> xmem 64 KiB physical pages without swizzle */
    if (!need_decompress && !need_bcpack && !need_swizzle && dst_phys &&
        req.Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE) {
        hr = io_engine_.scatter_to_xmem_physical_pages(
            req.DestinationAddress, req.DestinationSize, nullptr, fd, req.FileOffset);
        if (scarlett_succeeded(hr)) {
            out_bytes = req.DestinationSize;
        }
        return hr;
    }

    /* Fast direct linear path: uncompressed file -> linear memory without swizzle */
    if (!need_decompress && !need_bcpack && !need_swizzle && !dst_phys &&
        req.Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE) {
        uint8_t *dst_ptr = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(req.DestinationAddress));
        hr = io_engine_.read_sync(fd, req.FileOffset, req.SourceSize, dst_ptr);
        if (scarlett_succeeded(hr)) {
            out_bytes = req.SourceSize;
        }
        return hr;
    }

    /* Acquire source bytes into src_ptr */
    std::vector<uint8_t> src_storage;
    const uint8_t *src_ptr = nullptr;
    if (req.Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE) {
        src_storage.resize(req.SourceSize);
        hr = io_engine_.read_sync(fd, req.FileOffset, req.SourceSize, src_storage.data());
        if (scarlett_failed(hr)) {
            return hr;
        }
        src_ptr = src_storage.data();
    } else if (src_phys) {
        src_storage.resize(req.SourceSize);
        hr = io_engine_.gather_from_xmem_physical_pages(
            req.SourceAddress, req.SourceSize, src_storage.data());
        if (scarlett_failed(hr)) {
            return hr;
        }
        src_ptr = src_storage.data();
    } else {
        src_ptr = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(req.SourceAddress));
    }

    /* Stage intermediate buffer if we need BCPack, swizzle, or physical scatter */
    std::vector<uint8_t> work_buf;
    uint8_t *dst_linear = (!dst_phys && !need_swizzle)
                              ? reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(req.DestinationAddress))
                              : nullptr;
    if (dst_linear == nullptr) {
        work_buf.resize(req.DestinationSize);
        dst_linear = work_buf.data();
    }

    uint64_t written = req.DestinationSize;
    if (need_bcpack) {
        scarlett_bcpack_decoder_t *dec = nullptr;
        scarlett_bcpack_header_t bchdr{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            (void)read_bcpack_header_telemetry(
                src_ptr, req.SourceSize, req.Options.BcpackMode, bcpack_, bchdr);
            dec = bcpack_.decoder;
            if (dec == nullptr) {
                ++bcpack_.missing_plugin_errors;
            }
        }
        if (dec == nullptr) {
            uint32_t magic = 0u;
            if (src_ptr != nullptr && req.SourceSize >= 4u) {
                std::memcpy(&magic, src_ptr, 4u);
            }
            std::fprintf(
                stderr,
                "[scarlett:dstorage_xs] BCPack request without plugin: BcpackMode=%u src_size=%u dst_size=%u magic=0x%08x\n",
                static_cast<unsigned>(req.Options.BcpackMode),
                req.SourceSize,
                req.DestinationSize,
                magic);
            std::fflush(stderr);
            return SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING;
        }
        hr = dec->decode(
            dec,
            req.Options.BcpackMode,
            src_ptr,
            req.SourceSize,
            dst_linear,
            req.DestinationSize,
            &written);
        if (scarlett_failed(hr)) {
            return (hr == SCARLETT_ERR_DSTORAGE_BCPACK_ABI_MISMATCH)
                       ? hr
                       : SCARLETT_ERR_DSTORAGE_BCPACK_DECODE_FAILED;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        bcpack_.decoded_bytes += written;
    } else if (need_decompress) {
        hr = decomp_pool_.decompress(
            src_ptr, req.SourceSize, dst_linear, req.DestinationSize, &written);
        if (scarlett_failed(hr)) {
            return hr;
        }
    } else {
        if (dst_linear != src_ptr) {
            std::memcpy(dst_linear, src_ptr, req.SourceSize);
        }
        written = req.SourceSize;
    }

    if (need_swizzle) {
        std::vector<uint8_t> swiz_buf;
        uint8_t *swiz_dst = nullptr;
        if (!dst_phys) {
            swiz_dst = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(req.DestinationAddress));
        } else {
            swiz_buf.resize(written);
            swiz_dst = swiz_buf.data();
        }
        hr = apply_swizzle_cpu(
            req.Options.SwizzleMode, false, dst_linear, swiz_dst, static_cast<size_t>(written));
        if (scarlett_failed(hr)) {
            return hr;
        }
        if (dst_phys) {
            hr = io_engine_.scatter_to_xmem_physical_pages(
                req.DestinationAddress, written, swiz_dst, -1, 0u);
            if (scarlett_failed(hr)) {
                return hr;
            }
        }
    } else if (dst_phys) {
        hr = io_engine_.scatter_to_xmem_physical_pages(
            req.DestinationAddress, written, dst_linear, -1, 0u);
        if (scarlett_failed(hr)) {
            return hr;
        }
    }

    out_bytes = written;
    return SCARLETT_OK;
}

void DStorageManager::scheduler_loop() {
    while (true) {
        ScheduledItem item{};
        bool have_item = false;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_sched_.wait(lock, [&]() {
                if (sched_stop_) {
                    return true;
                }
                for (uint32_t p = 0; p < SCARLETT_DSTORAGE_PRIORITY_COUNT; ++p) {
                    if (!priority_queues_[p].empty()) {
                        return true;
                    }
                }
                return false;
            });

            if (sched_stop_) {
                return;
            }

            /* Weighted Deficit Round-Robin across REALTIME(1000), HIGH(100), NORMAL(10), LOW(1) */
            auto refill_deficits = [&]() {
                for (uint32_t p = 0; p < SCARLETT_DSTORAGE_PRIORITY_COUNT; ++p) {
                    if (!priority_queues_[p].empty() && drr_deficit_[p] == 0u) {
                        drr_deficit_[p] = kPriorityWeights[p];
                    }
                }
            };

            refill_deficits();
            for (int pass = 0; pass < 2 && !have_item; ++pass) {
                for (int p = static_cast<int>(SCARLETT_DSTORAGE_PRIORITY_COUNT) - 1; p >= 0; --p) {
                    if (!priority_queues_[p].empty() && drr_deficit_[p] > 0u) {
                        item = priority_queues_[p].front();
                        priority_queues_[p].pop_front();
                        --drr_deficit_[p];
                        have_item = true;
                        break;
                    }
                }
                if (!have_item) {
                    refill_deficits();
                }
            }
        }

        if (!have_item) {
            continue;
        }

        scarlett_hresult_t cmd_hr = SCARLETT_OK;
        uint64_t bytes = 0u;
        if (!item.cmd.cancelled) {
            if (item.cmd.type == DSTORAGE_COMMAND_TYPE_REQUEST) {
                cmd_hr = execute_single_request(item.cmd.request, bytes);
            } else if (item.cmd.type == DSTORAGE_COMMAND_TYPE_SIGNAL &&
                       item.cmd.signal_addr != 0u) {
                if (item.cmd.signal_mode == SCARLETT_DSTORAGE_SIGNAL_MODE_HOST_EVENT) {
                    auto *ev = reinterpret_cast<std::atomic<uint32_t> *>(
                        static_cast<uintptr_t>(item.cmd.signal_addr));
                    ev->store(1u, std::memory_order_release);
                    ev->notify_all();
                } else if (item.cmd.signal_mode == SCARLETT_DSTORAGE_SIGNAL_MODE_HOST_FENCE) {
                    auto *fence = reinterpret_cast<std::atomic<uint64_t> *>(
                        static_cast<uintptr_t>(item.cmd.signal_addr));
                    fence->store(item.cmd.signal_value, std::memory_order_release);
                    fence->notify_all();
                } else {
                    /* PE_FENCE / PE_EVENT: NEVER dereference ID3D12Fence* or Win32 HANDLE in Unix!
                     * Hand off to PE completion thread via pending_pe_signals_. */
                    std::lock_guard<std::mutex> sig_lock(mutex_);
                    scarlett_dstorage_pe_signal_item_t sig{};
                    sig.target_addr = item.cmd.signal_addr;
                    sig.value = item.cmd.signal_value;
                    sig.mode = item.cmd.signal_mode;
                    pending_pe_signals_.push_back(sig);
                    cv_pe_signal_.notify_one();
                }
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto qit = queues_.find(item.queue_handle);
        if (qit != queues_.end()) {
            QueueState *q = qit->second;
            if (!item.cmd.cancelled) {
                if (item.cmd.type == DSTORAGE_COMMAND_TYPE_REQUEST) {
                    ++q->total_completed;
                    if (scarlett_failed(cmd_hr)) {
                        record_queue_error_locked(
                            *q, cmd_hr, DSTORAGE_COMMAND_TYPE_REQUEST, item.cmd.request);
                    }
                    q->completions.push_back(CompletionRecord{
                        item.cmd.request.CancellationTag, bytes, cmd_hr});
                } else if (item.cmd.type == DSTORAGE_COMMAND_TYPE_STATUS) {
                    auto sit = status_arrays_.find(item.cmd.status_array_handle);
                    if (sit != status_arrays_.end() &&
                        item.cmd.status_index < sit->second->capacity) {
                        sit->second->entries[item.cmd.status_index].hr.store(
                            q->batch_last_hr, std::memory_order_release);
                        sit->second->entries[item.cmd.status_index].complete.store(
                            1u, std::memory_order_release);
                    }
                    q->batch_last_hr = SCARLETT_OK;
                }
            }
            if (q->inflight_count > 0u) {
                --q->inflight_count;
            }
            if (q->inflight_count == 0u) {
                cv_idle_.notify_all();
            }
        }
    }
}

scarlett_hresult_t DStorageManager::submit_request_direct(
    scarlett_dstorage_submit_params_t &params) {
    DSTORAGE_REQUEST req{};
    req.Options.AsUInt64 = params.options.raw;
    req.FileHandle = params.file_handle;
    req.FileOffset = params.file_offset;
    req.SourceAddress = params.src_addr;
    req.SourceSize = static_cast<uint32_t>(params.src_size);
    req.DestinationSize = static_cast<uint32_t>(params.dst_size);
    req.DestinationAddress = params.dst_addr;
    req.CancellationTag = params.request_tag;

    if (params.src_size > SCARLETT_DSTORAGE_MAX_REQUEST_BYTES) {
        params.out_status = SCARLETT_E_DSTORAGE_COMPRESSED_DATA_TOO_LARGE;
        return params.out_status;
    }
    if (params.dst_size > SCARLETT_DSTORAGE_MAX_REQUEST_BYTES) {
        params.out_status = SCARLETT_E_DSTORAGE_INVALID_DESTINATION_SIZE;
        return params.out_status;
    }

    uint64_t bytes = 0u;
    scarlett_hresult_t hr = execute_single_request(req, bytes);
    params.out_status = hr;

    if (params.queue_handle != 0u) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queues_.find(params.queue_handle);
        if (it != queues_.end()) {
            ++it->second->total_submitted;
            ++it->second->total_completed;
            if (scarlett_failed(hr)) {
                record_queue_error_locked(*it->second, hr, DSTORAGE_COMMAND_TYPE_REQUEST, req);
            }
            it->second->completions.push_back(CompletionRecord{params.request_tag, bytes, hr});
        }
    }
    return hr;
}

scarlett_hresult_t DStorageManager::poll_completion(scarlett_dstorage_poll_params_t &params) {
    params.out_has_completion = 0u;
    params.out_completed_tag = 0u;
    params.out_bytes_transferred = 0u;
    params.out_request_status = SCARLETT_OK;

    std::unique_lock<std::mutex> lock(mutex_);
    auto it = queues_.find(params.queue_handle);
    if (it == queues_.end()) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    QueueState *q = it->second;

    if (q->completions.empty() && q->inflight_count > 0u && params.timeout_ns > 0u) {
        cv_idle_.wait_for(
            lock,
            std::chrono::nanoseconds(params.timeout_ns),
            [&]() { return !q->completions.empty() || q->inflight_count == 0u; });
    }

    if (!q->completions.empty()) {
        const CompletionRecord rec = q->completions.front();
        q->completions.pop_front();
        params.out_has_completion = 1u;
        params.out_completed_tag = rec.tag;
        params.out_bytes_transferred = rec.bytes;
        params.out_request_status = rec.hr;
    }
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::submit_batch(scarlett_dstorage_submit_batch_params_t &params) {
    params.out_bytes_decompressed = 0u;
    params.out_elapsed_ns = 0u;
    if (params.requests_addr == 0u && params.request_count > 0u) {
        params.out_status = SCARLETT_E_POINTER;
        return params.out_status;
    }
    if (params.request_count == 0u) {
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    const auto *reqs = reinterpret_cast<const DSTORAGE_REQUEST *>(
        static_cast<uintptr_t>(params.requests_addr));

    /* Validate all requests & resolve file descriptors */
    struct BatchSrcSlice {
        int fd = -1;
        uint64_t offset = 0;
        uint32_t size = 0;
        uint64_t staging_offset = 0;
        const uint8_t *mem_ptr = nullptr;
    };
    std::vector<BatchSrcSlice> slices(params.request_count);
    std::vector<std::shared_ptr<OpenFile>> held_files;
    uint64_t total_file_read_bytes = 0u;
    uint64_t total_dst_bytes = 0u;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        io_engine_.initialize();
        decomp_pool_.initialize();

        for (uint32_t i = 0; i < params.request_count; ++i) {
            scarlett_hresult_t vhr = validate_request_basic(reqs[i]);
            if (scarlett_failed(vhr)) {
                params.out_status = vhr;
                return vhr;
            }
            if (reqs[i].Options.BcpackMode != 0u && bcpack_.decoder == nullptr) {
                ++bcpack_.missing_plugin_errors;
                params.out_status = SCARLETT_ERR_DSTORAGE_BCPACK_PLUGIN_MISSING;
                return params.out_status;
            }
            if (reqs[i].Options.SourceType == DSTORAGE_REQUEST_SOURCE_FILE) {
                auto fit = files_.find(reqs[i].FileHandle);
                if (fit == files_.end()) {
                    params.out_status = SCARLETT_E_DSTORAGE_INVALID_FILE_HANDLE;
                    return params.out_status;
                }
                if (reqs[i].FileOffset > fit->second->size_bytes ||
                    reqs[i].SourceSize > fit->second->size_bytes - reqs[i].FileOffset) {
                    params.out_status = SCARLETT_E_DSTORAGE_END_OF_FILE;
                    return params.out_status;
                }
                held_files.push_back(fit->second);
                slices[i].fd = fit->second->fd;
                slices[i].offset = reqs[i].FileOffset;
                slices[i].size = reqs[i].SourceSize;
                slices[i].staging_offset = total_file_read_bytes;
                total_file_read_bytes += reqs[i].SourceSize;
            } else {
                slices[i].mem_ptr = reinterpret_cast<const uint8_t *>(
                    static_cast<uintptr_t>(reqs[i].SourceAddress));
                slices[i].size = reqs[i].SourceSize;
            }
            total_dst_bytes += reqs[i].DestinationSize;
        }
    }

    std::lock_guard<std::mutex> scratch_lock(batch_scratch_mutex_);
    if (batch_read_buf_.size() < total_file_read_bytes) {
        batch_read_buf_.resize(total_file_read_bytes);
    }

    std::vector<IoBatchItem> io_items;
    io_items.reserve(params.request_count);
    for (uint32_t i = 0; i < params.request_count; ++i) {
        if (slices[i].fd >= 0 && slices[i].size > 0u) {
            IoBatchItem it{};
            it.fd = slices[i].fd;
            it.offset = slices[i].offset;
            it.size = slices[i].size;
            it.dst = batch_read_buf_.data() + slices[i].staging_offset;
            io_items.push_back(it);
        }
    }

    const auto t0 = std::chrono::steady_clock::now();

    auto collect_tiles = [&](uint32_t begin_idx, uint32_t end_idx,
                             std::vector<DecompTileJob> &jobs,
                             std::vector<uint32_t> &fallbacks) -> scarlett_hresult_t {
        jobs.clear();
        fallbacks.clear();
        size_t total_tiles = 0u;
        for (uint32_t i = begin_idx; i < end_idx; ++i) {
            const DSTORAGE_REQUEST &req = reqs[i];
            const uint8_t *src_ptr = (slices[i].fd >= 0)
                                         ? (batch_read_buf_.data() + slices[i].staging_offset)
                                         : slices[i].mem_ptr;
            if (req.Options.ZlibDecompress != 0u &&
                req.Options.BcpackMode == 0u &&
                req.Options.DestinationIsPhysicalPages == 0u &&
                req.Options.SwizzleMode == DSTORAGE_SWIZZLE_NONE &&
                req.SourceSize >= sizeof(scarlett_gdeflate_header_t)) {
                scarlett_gdeflate_header_t hdr{};
                std::memcpy(&hdr, src_ptr, sizeof(hdr));
                if (hdr.magic == SCARLETT_DSTORAGE_GDEFLATE_MAGIC) {
                    if (hdr.tile_count == 0u ||
                        hdr.total_uncompressed_bytes > req.DestinationSize ||
                        req.SourceSize < sizeof(hdr) +
                            static_cast<size_t>(hdr.tile_count) * sizeof(scarlett_gdeflate_tile_desc_t)) {
                        return SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR;
                    }
                    total_tiles += hdr.tile_count;
                }
            }
        }
        jobs.resize(total_tiles);
        size_t write_idx = 0u;
        for (uint32_t i = begin_idx; i < end_idx; ++i) {
            const DSTORAGE_REQUEST &req = reqs[i];
            const uint8_t *src_ptr = (slices[i].fd >= 0)
                                         ? (batch_read_buf_.data() + slices[i].staging_offset)
                                         : slices[i].mem_ptr;
            uint8_t *dst_ptr = reinterpret_cast<uint8_t *>(
                static_cast<uintptr_t>(req.DestinationAddress));

            if (req.Options.ZlibDecompress != 0u &&
                req.Options.BcpackMode == 0u &&
                req.Options.DestinationIsPhysicalPages == 0u &&
                req.Options.SwizzleMode == DSTORAGE_SWIZZLE_NONE &&
                req.SourceSize >= sizeof(scarlett_gdeflate_header_t)) {
                scarlett_gdeflate_header_t hdr{};
                std::memcpy(&hdr, src_ptr, sizeof(hdr));
                if (hdr.magic == SCARLETT_DSTORAGE_GDEFLATE_MAGIC) {
                    const auto *descs = reinterpret_cast<const scarlett_gdeflate_tile_desc_t *>(
                        src_ptr + sizeof(hdr));
                    for (uint32_t t = 0; t < hdr.tile_count; ++t) {
                        const scarlett_gdeflate_tile_desc_t &d = descs[t];
                        if (static_cast<uint64_t>(d.compressed_offset) + d.compressed_size > req.SourceSize ||
                            static_cast<uint64_t>(d.uncompressed_offset) + d.uncompressed_size > req.DestinationSize) {
                            return SCARLETT_E_DSTORAGE_DECOMPRESSION_ERROR;
                        }
                        DecompTileJob &job = jobs[write_idx++];
                        job.src = src_ptr + d.compressed_offset;
                        job.src_size = d.compressed_size;
                        job.dst = dst_ptr + d.uncompressed_offset;
                        job.dst_size = d.uncompressed_size;
                        job.is_zlib_wrapper = false;
                    }
                    continue;
                }
            }
            fallbacks.push_back(i);
        }
        return SCARLETT_OK;
    };

    auto run_fallbacks = [&](const std::vector<uint32_t> &fallbacks) -> scarlett_hresult_t {
        for (uint32_t idx : fallbacks) {
            uint64_t out_b = 0;
            scarlett_hresult_t hr = execute_single_request(reqs[idx], out_b);
            if (scarlett_failed(hr)) {
                return hr;
            }
        }
        return SCARLETT_OK;
    };

    /* Overlap io_uring read of second wave with parallel decompression only for huge multi-chunk
     * batches (> 256 MiB read) where I/O exceeds thread-pool barrier + thread spawn overhead. */
    std::vector<uint32_t> fallback_indices;
    if (io_items.size() >= 4u && io_items.size() == params.request_count &&
        total_file_read_bytes > (UINT64_C(256) * 1024u * 1024u)) {
        const uint32_t split = static_cast<uint32_t>(io_items.size() / 4u);
        scarlett_hresult_t io1_hr = io_engine_.read_batch_sync(io_items.data(), split);
        if (scarlett_failed(io1_hr)) {
            params.out_status = io1_hr;
            return io1_hr;
        }

        scarlett_hresult_t io2_hr = SCARLETT_OK;
        std::thread io_reader([&]() {
            io2_hr = io_engine_.read_batch_sync(
                io_items.data() + split, io_items.size() - split);
        });

        scarlett_hresult_t d1_hr = collect_tiles(0u, split, batch_tile_jobs_, fallback_indices);
        if (scarlett_succeeded(d1_hr) && !batch_tile_jobs_.empty()) {
            d1_hr = decomp_pool_.decompress_tiles_parallel(
                batch_tile_jobs_.data(), batch_tile_jobs_.size());
        }
        if (scarlett_succeeded(d1_hr) && !fallback_indices.empty()) {
            d1_hr = run_fallbacks(fallback_indices);
        }

        io_reader.join();
        if (scarlett_failed(io2_hr)) {
            params.out_status = io2_hr;
            return io2_hr;
        }
        if (scarlett_failed(d1_hr)) {
            params.out_status = d1_hr;
            return d1_hr;
        }

        scarlett_hresult_t d2_hr = collect_tiles(split, params.request_count, batch_tile_jobs_, fallback_indices);
        if (scarlett_failed(d2_hr)) {
            params.out_status = d2_hr;
            return d2_hr;
        }
        if (!batch_tile_jobs_.empty()) {
            d2_hr = decomp_pool_.decompress_tiles_parallel(
                batch_tile_jobs_.data(), batch_tile_jobs_.size());
            if (scarlett_failed(d2_hr)) {
                params.out_status = d2_hr;
                return d2_hr;
            }
        }
        if (!fallback_indices.empty()) {
            scarlett_hresult_t f_hr = run_fallbacks(fallback_indices);
            if (scarlett_failed(f_hr)) {
                params.out_status = f_hr;
                return f_hr;
            }
        }
    } else {
        if (!io_items.empty()) {
            scarlett_hresult_t io_hr = io_engine_.read_batch_sync(io_items.data(), io_items.size());
            if (scarlett_failed(io_hr)) {
                params.out_status = io_hr;
                return io_hr;
            }
        }
        scarlett_hresult_t c_hr = collect_tiles(0u, params.request_count, batch_tile_jobs_, fallback_indices);
        if (scarlett_failed(c_hr)) {
            params.out_status = c_hr;
            return c_hr;
        }
        if (!batch_tile_jobs_.empty()) {
            scarlett_hresult_t decomp_hr = decomp_pool_.decompress_tiles_parallel(
                batch_tile_jobs_.data(), batch_tile_jobs_.size());
            if (scarlett_failed(decomp_hr)) {
                params.out_status = decomp_hr;
                return decomp_hr;
            }
        }
        if (!fallback_indices.empty()) {
            scarlett_hresult_t f_hr = run_fallbacks(fallback_indices);
            if (scarlett_failed(f_hr)) {
                params.out_status = f_hr;
                return f_hr;
            }
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const uint64_t elapsed_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

    params.out_bytes_decompressed = total_dst_bytes;
    params.out_elapsed_ns = elapsed_ns;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::query_caps(scarlett_dstorage_caps_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    io_engine_.initialize();
    decomp_pool_.initialize();
    ensure_shaders_probed_locked();

    params.out_io_uring_active = io_engine_.is_io_uring_active() ? 1u : 0u;
    params.out_io_uring_sqpoll = io_engine_.is_sqpoll_active() ? 1u : 0u;
    params.out_io_uring_fixed_buffers = io_engine_.is_fixed_buffers_active() ? 1u : 0u;
    params.out_vk_memory_decompression = vk_probe_.vk_ext_memory_decompression ? 1u : 0u;
    params.out_vkdeflate_spirv_valid = vk_probe_.vkdeflate_spirv_valid ? 1u : 0u;
    params.out_swizzle_spirv_valid = vk_probe_.swizzle_spirv_valid ? 1u : 0u;
    params.out_bcpack_plugin_loaded = (bcpack_.decoder != nullptr) ? 1u : 0u;
    params.out_bcpack_header_reads = bcpack_.header_reads;
    params.out_bcpack_decoded_bytes = bcpack_.decoded_bytes;
    params.out_staging_buffer_bytes = staging_buffer_bytes_;
    params.out_total_bytes_read = io_engine_.total_bytes_read();
    params.out_total_bytes_decompressed = decomp_pool_.total_bytes_decompressed();
    params.out_worker_threads = decomp_pool_.worker_count();
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::report_stub(scarlett_dstorage_report_stub_params_t &params) {
    params.ctx.strict_mode = strict_mode_;
    const scarlett_stub_action_t action = scarlett_stub_resolve_action(&params.ctx);
    params.out_action = static_cast<uint32_t>(action);

    std::lock_guard<std::mutex> lock(stub_mutex_);
    if (stub_sink_ != nullptr) {
        const scarlett_stub_action_t sink_action = stub_sink_(&params.ctx, stub_sink_user_data_);
        params.out_action = static_cast<uint32_t>(sink_action);
    } else {
        std::fprintf(
            stderr,
            "[scarlett:dstorage_xs:stub] dll=%s fn=%s caller=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx\n",
            params.ctx.dll_name,
            params.ctx.symbol_name,
            static_cast<unsigned long long>(params.ctx.return_address),
            static_cast<unsigned long long>(params.ctx.rcx),
            static_cast<unsigned long long>(params.ctx.rdx),
            static_cast<unsigned long long>(params.ctx.r8),
            static_cast<unsigned long long>(params.ctx.r9));
    }
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t DStorageManager::wait_signals(scarlett_dstorage_wait_signals_params_t &params) {
    params.out_count = 0u;
    params.out_stop = 0u;
    std::unique_lock<std::mutex> lock(mutex_);
    if (params.blocking != 0u) {
        cv_pe_signal_.wait(lock, [&]() {
            return !pending_pe_signals_.empty() || pe_signals_stop_;
        });
    }
    if (pe_signals_stop_ && pending_pe_signals_.empty()) {
        params.out_stop = 1u;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }
    uint32_t count = 0u;
    while (!pending_pe_signals_.empty() && count < SCARLETT_DSTORAGE_PE_SIGNAL_BATCH_MAX) {
        params.items[count++] = pending_pe_signals_.front();
        pending_pe_signals_.pop_front();
    }
    params.out_count = count;
    params.out_stop = pe_signals_stop_ ? 1u : 0u;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

void DStorageManager::set_stub_sink(scarlett_stub_sink_fn_t sink, void *user_data) {
    std::lock_guard<std::mutex> lock(stub_mutex_);
    stub_sink_ = sink;
    stub_sink_user_data_ = user_data;
}

} // namespace scarlett::dstorage_xs::unixlib
