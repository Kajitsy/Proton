/*
 * Proton-Scarlett — XGameSave & XGameSaveFiles POSIX Atomic Storage Backend
 *
 * Implements:
 *   - Atomic POSIX writes (<file>.tmp + fsync + rename + dir fsync)
 *   - All 15 official GDK XGameSave error codes (0x80830001..0x8083000F)
 *   - Hard 16 MiB transaction limit (E_GS_UPDATE_TOO_BIG)
 *   - Strict XGameSave vs XGameSaveFiles mutual exclusion (E_GS_PROVIDER_MISMATCH)
 *   - Mandatory XThreadIsTimeSensitive() check on all sync APIs (E_GS_ASYNC_FUNCTION_REQUIRED)
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xgameruntime_internal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace scarlett::xgameruntime::unixlib {

namespace {

namespace fs = std::filesystem;

constexpr uint64_t kDefaultSaveQuotaBytes = UINT64_C(256) * 1024u * 1024u; /* 256 MiB */

enum class ActiveProviderKind {
    None = 0,
    GameSave = 1,
    GameSaveFiles = 2
};

struct PendingBlobOp {
    bool is_delete{false};
    std::string blob_name;
    std::vector<uint8_t> data;
};

struct UpdateRecord {
    uint64_t handle{0u};
    uint64_t container_handle{0u};
    uint64_t provider_handle{0u};
    uint64_t pending_write_bytes{0u};
    std::vector<PendingBlobOp> ops;
    bool expired{false};
};

struct ContainerRecord {
    uint64_t handle{0u};
    uint64_t provider_handle{0u};
    std::string name;
    bool expired{false};
};

struct ProviderRecord {
    uint64_t handle{0u};
    uint64_t user_handle{0u};
    std::string scid;
    std::string root_dir;
    bool sync_on_demand{false};
    bool expired{false};
};

struct GameSavePolicy {
    uint64_t quota_bytes{kDefaultSaveQuotaBytes};
    uint64_t local_storage_limit_bytes{UINT64_C(1024) * 1024u * 1024u};
    bool force_user_canceled{false};
    bool force_sync_failed{false};
    bool force_no_access{false};
    std::set<std::string> unsynced_containers;
};

struct GameSaveState {
    std::mutex mutex;
    uint64_t next_handle{0x5000u};
    ActiveProviderKind active_kind{ActiveProviderKind::None};
    uint32_t active_files_providers{0u};
    std::set<std::string> files_sessions;
    std::unordered_map<uint64_t, ProviderRecord> providers;
    std::unordered_map<uint64_t, ContainerRecord> containers;
    std::unordered_map<uint64_t, UpdateRecord> updates;
    GameSavePolicy policy;
};

GameSaveState &gs_state() {
    static GameSaveState s;
    return s;
}

std::string resolve_saves_base_dir() {
    const char *override_dir = std::getenv("PROTON_XBOX_SAVES_DIR");
    if (override_dir != nullptr && override_dir[0] != '\0') {
        return std::string(override_dir);
    }
    const char *pfx = std::getenv("PROTON_XBOX_PREFIX_DIR");
    if (pfx == nullptr || pfx[0] == '\0') {
        pfx = std::getenv("WINEPREFIX");
    }
    std::string root = (pfx != nullptr && pfx[0] != '\0') ? std::string(pfx) : "/tmp/scarlett_pfx";
    return root + "/drive_c/users/steamuser/AppData/Local/ScarlettSaves";
}

bool is_valid_container_name(const char *name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    size_t len = std::strlen(name);
    if (len == 0u || len > 255u) {
        return false;
    }
    if (std::strstr(name, "..") != nullptr) {
        return false;
    }
    if (name[len - 1u] == '.' || name[len - 1u] == ' ') {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20u || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

bool is_valid_blob_name(const char *name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    size_t len = std::strlen(name);
    if (len > 127u) {
        return false;
    }
    if (std::strstr(name, "..") != nullptr) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20u || c == '/' || c == '\\') {
            return false;
        }
    }
    return true;
}

scarlett_hresult_t map_errno_to_gs(int err) {
    switch (err) {
        case EACCES:
        case EPERM:
        case EROFS:
            return SCARLETT_E_GS_NO_ACCESS;
        case ENOSPC:
        case EDQUOT:
            return SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE;
        case ENOENT:
            return SCARLETT_E_GS_BLOB_NOT_FOUND;
        default:
            return SCARLETT_E_FAIL;
    }
}

scarlett_hresult_t fsync_directory(const fs::path &dir) {
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) {
        return map_errno_to_gs(errno);
    }
    int rc = ::fsync(dfd);
    int saved_err = errno;
    ::close(dfd);
    if (rc != 0 && saved_err != EINVAL) {
        return map_errno_to_gs(saved_err);
    }
    return SCARLETT_OK;
}

scarlett_hresult_t atomic_write_file(const fs::path &target, const uint8_t *data, size_t size) {
    fs::path tmp = target;
    tmp += ".tmp";

    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return map_errno_to_gs(errno);
    }

    size_t written = 0u;
    while (written < size) {
        ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            int saved = errno;
            ::close(fd);
            ::unlink(tmp.c_str());
            return map_errno_to_gs(saved);
        }
        written += static_cast<size_t>(n);
    }

    if (::fsync(fd) != 0) {
        int saved = errno;
        ::close(fd);
        ::unlink(tmp.c_str());
        return map_errno_to_gs(saved);
    }
    ::close(fd);

    if (::rename(tmp.c_str(), target.c_str()) != 0) {
        int saved = errno;
        ::unlink(tmp.c_str());
        return map_errno_to_gs(saved);
    }

    return fsync_directory(target.parent_path());
}

uint64_t compute_dir_bytes(const fs::path &dir) {
    uint64_t total = 0u;
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return 0u;
    }
    for (const auto &entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec)) {
            std::string fn = entry.path().filename().string();
            if (fn.size() >= 4u && fn.compare(fn.size() - 4u, 4u, ".tmp") == 0) {
                continue;
            }
            total += entry.file_size(ec);
        }
    }
    return total;
}

bool check_sync_allowed() {
    return !PlatformManager::instance().current_thread_is_time_sensitive();
}

} // namespace

GameSaveManager &GameSaveManager::instance() {
    static GameSaveManager mgr;
    return mgr;
}

void GameSaveManager::initialize() {
    std::lock_guard<std::mutex> lock(gs_state().mutex);
    /* Preserve active state if re-initialized; policy remains intact */
}

void GameSaveManager::shutdown() {
    std::lock_guard<std::mutex> lock(gs_state().mutex);
    gs_state().providers.clear();
    gs_state().containers.clear();
    gs_state().updates.clear();
    gs_state().files_sessions.clear();
    gs_state().active_files_providers = 0u;
    gs_state().active_kind = ActiveProviderKind::None;
    gs_state().policy = GameSavePolicy{};
}

scarlett_hresult_t GameSaveManager::provider_op(scarlett_xgr_gs_provider_params_t &p) {
    const auto subop = static_cast<scarlett_xgr_gs_provider_subop_t>(p.subop);
    const bool is_async = (p.reserved != 0u);

    if (!is_async &&
        (subop == SCARLETT_XGR_GS_INIT_PROVIDER ||
         subop == SCARLETT_XGR_GS_GET_QUOTA ||
         subop == SCARLETT_XGR_GS_DELETE_CONTAINER ||
         subop == SCARLETT_XGR_GS_ENUM_CONTAINERS)) {
        if (!check_sync_allowed()) {
            p.out_status = SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED;
            return p.out_status;
        }
    }

    std::lock_guard<std::mutex> lock(gs_state().mutex);

    switch (subop) {
        case SCARLETT_XGR_GS_INIT_PROVIDER: {
            if (gs_state().policy.force_user_canceled) {
                p.out_status = SCARLETT_E_GS_USER_CANCELED;
                break;
            }
            if (gs_state().policy.force_no_access) {
                p.out_status = SCARLETT_E_GS_NO_ACCESS;
                break;
            }
            const char *scid = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            if (scid == nullptr || scid[0] == '\0') {
                p.out_status = SCARLETT_E_GS_NO_SERVICE_CONFIGURATION;
                break;
            }
            if (!PlatformManager::instance().is_user_valid_and_registered(p.user_handle)) {
                p.out_status = SCARLETT_E_GS_USER_NOT_REGISTERED_IN_SERVICE;
                break;
            }
            if (gs_state().active_kind == ActiveProviderKind::GameSaveFiles) {
                p.out_status = SCARLETT_E_GS_PROVIDER_MISMATCH;
                break;
            }
            std::string root = resolve_saves_base_dir() + "/" + std::string(scid);
            std::error_code ec;
            fs::create_directories(root, ec);
            if (ec) {
                p.out_status = map_errno_to_gs(ec.value());
                break;
            }

            ProviderRecord rec{};
            rec.handle = ++gs_state().next_handle;
            rec.user_handle = p.user_handle;
            rec.scid = scid;
            rec.root_dir = root;
            rec.sync_on_demand = (p.flags_or_bool != 0u);
            gs_state().providers[rec.handle] = rec;
            gs_state().active_kind = ActiveProviderKind::GameSave;
            p.provider_handle = rec.handle;
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_CLOSE_PROVIDER: {
            auto it = gs_state().providers.find(p.provider_handle);
            if (it == gs_state().providers.end()) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            it->second.expired = true;
            for (auto &ckv : gs_state().containers) {
                if (ckv.second.provider_handle == p.provider_handle) {
                    ckv.second.expired = true;
                }
            }
            for (auto &ukv : gs_state().updates) {
                if (ukv.second.provider_handle == p.provider_handle) {
                    ukv.second.expired = true;
                }
            }
            gs_state().providers.erase(it);
            if (gs_state().providers.empty() && gs_state().active_files_providers == 0u) {
                gs_state().active_kind = ActiveProviderKind::None;
            }
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_GET_QUOTA: {
            auto it = gs_state().providers.find(p.provider_handle);
            if (it == gs_state().providers.end() || it->second.expired) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            uint64_t used = compute_dir_bytes(it->second.root_dir);
            uint64_t quota = gs_state().policy.quota_bytes;
            int64_t rem = (quota >= used) ? static_cast<int64_t>(quota - used) : 0;
            p.quota_val = static_cast<uint64_t>(rem);
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_DELETE_CONTAINER: {
            auto it = gs_state().providers.find(p.provider_handle);
            if (it == gs_state().providers.end() || it->second.expired) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            const char *cname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            if (!is_valid_container_name(cname)) {
                p.out_status = SCARLETT_E_GS_INVALID_CONTAINER_NAME;
                break;
            }
            fs::path cdir = fs::path(it->second.root_dir) / cname;
            std::error_code ec;
            if (!fs::exists(cdir, ec)) {
                p.out_status = SCARLETT_E_GS_BLOB_NOT_FOUND;
                break;
            }
            fs::remove_all(cdir, ec);
            if (ec) {
                p.out_status = map_errno_to_gs(ec.value());
                break;
            }
            gs_state().policy.unsynced_containers.erase(cname);
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_ENUM_CONTAINERS: {
            auto it = gs_state().providers.find(p.provider_handle);
            if (it == gs_state().providers.end() || it->second.expired) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            auto cb = reinterpret_cast<XGameSaveContainerInfoCallback>(static_cast<uintptr_t>(p.cb_addr));
            if (cb == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            const char *prefix = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            std::string pfx_str = (prefix != nullptr) ? prefix : "";

            std::error_code ec;
            for (const auto &entry : fs::directory_iterator(it->second.root_dir, ec)) {
                if (!entry.is_directory(ec)) {
                    continue;
                }
                std::string name = entry.path().filename().string();
                if (!pfx_str.empty() && name.rfind(pfx_str, 0) != 0) {
                    continue;
                }
                XGameSaveContainerInfo info{};
                std::snprintf(info.name, sizeof(info.name), "%s", name.c_str());
                std::snprintf(info.displayName, sizeof(info.displayName), "%s", name.c_str());
                uint32_t bcnt = 0u;
                uint64_t bsz = 0u;
                for (const auto &b : fs::directory_iterator(entry.path(), ec)) {
                    if (b.is_regular_file(ec)) {
                        std::string bname = b.path().filename().string();
                        if (bname.size() >= 4u && bname.compare(bname.size() - 4u, 4u, ".tmp") == 0) {
                            continue;
                        }
                        ++bcnt;
                        bsz += b.file_size(ec);
                    }
                }
                info.blobCount = bcnt;
                info.totalSize = bsz;
                info.lastModifiedTime = static_cast<uint64_t>(std::time(nullptr));
                info.needsSync = gs_state().policy.unsynced_containers.count(name) > 0u;
                if (!cb(&info, reinterpret_cast<void *>(static_cast<uintptr_t>(p.ctx_addr)))) {
                    break;
                }
            }
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_SET_POLICY: {
            /*flags_or_bool:
             * 1 = set quota_bytes (quota_val)
             * 2 = set local_storage_limit_bytes (quota_val)
             * 3 = set force_user_canceled (quota_val != 0)
             * 4 = set force_sync_failed (quota_val != 0)
             * 5 = set force_no_access (quota_val != 0)
             * 6 = mark container unsynced (str_addr, quota_val != 0)
             * 7 = reset all test policy flags
             */
            const char *cname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            switch (p.flags_or_bool) {
                case 1u: gs_state().policy.quota_bytes = p.quota_val; break;
                case 2u: gs_state().policy.local_storage_limit_bytes = p.quota_val; break;
                case 3u: gs_state().policy.force_user_canceled = (p.quota_val != 0u); break;
                case 4u: gs_state().policy.force_sync_failed = (p.quota_val != 0u); break;
                case 5u: gs_state().policy.force_no_access = (p.quota_val != 0u); break;
                case 6u:
                    if (cname != nullptr) {
                        if (p.quota_val != 0u) {
                            gs_state().policy.unsynced_containers.insert(cname);
                        } else {
                            gs_state().policy.unsynced_containers.erase(cname);
                        }
                    }
                    break;
                case 7u:
                default:
                    gs_state().policy = GameSavePolicy{};
                    break;
            }
            p.out_status = SCARLETT_OK;
            break;
        }

        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }

    return p.out_status;
}

scarlett_hresult_t GameSaveManager::container_op(scarlett_xgr_gs_container_params_t &p) {
    const auto subop = static_cast<scarlett_xgr_gs_container_subop_t>(p.subop);
    const bool is_async = (p.reserved != 0u);

    if (!is_async &&
        (subop == SCARLETT_XGR_GS_ENUM_BLOBS ||
         subop == SCARLETT_XGR_GS_READ_BLOB ||
         subop == SCARLETT_XGR_GS_SUBMIT_UPDATE)) {
        if (!check_sync_allowed()) {
            p.out_status = SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED;
            return p.out_status;
        }
    }

    std::lock_guard<std::mutex> lock(gs_state().mutex);

    auto get_valid_provider = [&](uint64_t ph) -> ProviderRecord * {
        auto it = gs_state().providers.find(ph);
        if (it == gs_state().providers.end() || it->second.expired) {
            return nullptr;
        }
        return &it->second;
    };

    auto get_valid_container = [&](uint64_t ch) -> ContainerRecord * {
        auto it = gs_state().containers.find(ch);
        if (it == gs_state().containers.end() || it->second.expired) {
            return nullptr;
        }
        if (get_valid_provider(it->second.provider_handle) == nullptr) {
            return nullptr;
        }
        return &it->second;
    };

    switch (subop) {
        case SCARLETT_XGR_GS_CREATE_CONTAINER: {
            if (gs_state().active_kind == ActiveProviderKind::GameSaveFiles) {
                p.out_status = SCARLETT_E_GS_PROVIDER_MISMATCH;
                break;
            }
            auto *prov = get_valid_provider(p.provider_or_container_handle);
            if (prov == nullptr) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            const char *cname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.name_addr));
            if (!is_valid_container_name(cname)) {
                p.out_status = SCARLETT_E_GS_INVALID_CONTAINER_NAME;
                break;
            }
            if (gs_state().policy.force_no_access) {
                p.out_status = SCARLETT_E_GS_NO_ACCESS;
                break;
            }
            fs::path cdir = fs::path(prov->root_dir) / cname;
            std::error_code ec;
            fs::create_directories(cdir, ec);
            if (ec) {
                p.out_status = map_errno_to_gs(ec.value());
                break;
            }
            ContainerRecord rec{};
            rec.handle = ++gs_state().next_handle;
            rec.provider_handle = prov->handle;
            rec.name = cname;
            gs_state().containers[rec.handle] = rec;
            p.provider_or_container_handle = rec.handle;
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_CLOSE_CONTAINER: {
            auto it = gs_state().containers.find(p.provider_or_container_handle);
            if (it == gs_state().containers.end()) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            it->second.expired = true;
            gs_state().containers.erase(it);
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_CREATE_UPDATE: {
            auto *cont = get_valid_container(p.provider_or_container_handle);
            if (cont == nullptr) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            UpdateRecord upd{};
            upd.handle = ++gs_state().next_handle;
            upd.container_handle = cont->handle;
            upd.provider_handle = cont->provider_handle;
            gs_state().updates[upd.handle] = upd;
            p.update_handle = upd.handle;
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_UPDATE_WRITE_BLOB: {
            auto it = gs_state().updates.find(p.update_handle);
            if (it == gs_state().updates.end() || it->second.expired) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            const char *bname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.name_addr));
            if (!is_valid_blob_name(bname)) {
                p.out_status = SCARLETT_E_GS_INVALID_CONTAINER_NAME;
                break;
            }
            if (p.data_size > 0u && p.data_addr == 0u) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            if (p.data_size > SCARLETT_XGR_GS_MAX_UPDATE ||
                it->second.pending_write_bytes + p.data_size > SCARLETT_XGR_GS_MAX_UPDATE) {
                p.out_status = SCARLETT_E_GS_UPDATE_TOO_BIG;
                break;
            }
            PendingBlobOp op{};
            op.is_delete = false;
            op.blob_name = bname;
            if (p.data_size > 0u) {
                const auto *src = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(p.data_addr));
                op.data.assign(src, src + p.data_size);
            }
            it->second.pending_write_bytes += p.data_size;
            it->second.ops.push_back(std::move(op));
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_UPDATE_DELETE_BLOB: {
            auto it = gs_state().updates.find(p.update_handle);
            if (it == gs_state().updates.end() || it->second.expired) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            const char *bname = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.name_addr));
            if (!is_valid_blob_name(bname)) {
                p.out_status = SCARLETT_E_GS_INVALID_CONTAINER_NAME;
                break;
            }
            PendingBlobOp op{};
            op.is_delete = true;
            op.blob_name = bname;
            it->second.ops.push_back(std::move(op));
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_SUBMIT_UPDATE: {
            auto uit = gs_state().updates.find(p.update_handle);
            if (uit == gs_state().updates.end() || uit->second.expired) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            auto *cont = get_valid_container(uit->second.container_handle);
            auto *prov = (cont != nullptr) ? get_valid_provider(cont->provider_handle) : nullptr;
            if (cont == nullptr || prov == nullptr) {
                uit->second.expired = true;
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            if (gs_state().policy.force_user_canceled) {
                p.out_status = SCARLETT_E_GS_USER_CANCELED;
                break;
            }
            if (gs_state().policy.force_sync_failed) {
                p.out_status = SCARLETT_E_GS_CONTAINER_SYNC_FAILED;
                break;
            }
            if (gs_state().policy.force_no_access) {
                p.out_status = SCARLETT_E_GS_NO_ACCESS;
                break;
            }
            if (uit->second.pending_write_bytes > SCARLETT_XGR_GS_MAX_UPDATE) {
                p.out_status = SCARLETT_E_GS_UPDATE_TOO_BIG;
                break;
            }

            const uint64_t cur_used = compute_dir_bytes(prov->root_dir);
            if (cur_used + uit->second.pending_write_bytes > gs_state().policy.local_storage_limit_bytes) {
                p.out_status = SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE;
                break;
            }
            if (cur_used + uit->second.pending_write_bytes > gs_state().policy.quota_bytes) {
                p.out_status = SCARLETT_E_GS_QUOTA_EXCEEDED;
                break;
            }

            fs::path cdir = fs::path(prov->root_dir) / cont->name;
            std::error_code ec;
            fs::create_directories(cdir, ec);
            if (ec) {
                p.out_status = map_errno_to_gs(ec.value());
                break;
            }

            scarlett_hresult_t op_hr = SCARLETT_OK;
            for (const auto &op : uit->second.ops) {
                fs::path bpath = cdir / op.blob_name;
                if (op.is_delete) {
                    if (!fs::exists(bpath, ec)) {
                        op_hr = SCARLETT_E_GS_BLOB_NOT_FOUND;
                        break;
                    }
                    fs::remove(bpath, ec);
                    if (ec) {
                        op_hr = map_errno_to_gs(ec.value());
                        break;
                    }
                } else {
                    op_hr = atomic_write_file(bpath, op.data.data(), op.data.size());
                    if (scarlett_failed(op_hr)) {
                        break;
                    }
                }
            }
            /* Update handle expires upon submit */
            uit->second.expired = true;
            p.out_status = op_hr;
            break;
        }

        case SCARLETT_XGR_GS_CANCEL_UPDATE:
        case SCARLETT_XGR_GS_CLOSE_UPDATE: {
            auto uit = gs_state().updates.find(p.update_handle);
            if (uit == gs_state().updates.end()) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            uit->second.expired = true;
            gs_state().updates.erase(uit);
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_ENUM_BLOBS: {
            auto *cont = get_valid_container(p.provider_or_container_handle);
            if (cont == nullptr) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            auto *prov = get_valid_provider(cont->provider_handle);
            if (prov == nullptr) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            if (gs_state().policy.force_sync_failed) {
                p.out_status = SCARLETT_E_GS_CONTAINER_SYNC_FAILED;
                break;
            }
            if (gs_state().policy.unsynced_containers.count(cont->name) > 0u) {
                p.out_status = SCARLETT_E_GS_CONTAINER_NOT_IN_SYNC;
                break;
            }
            auto cb = reinterpret_cast<XGameSaveBlobInfoCallback>(static_cast<uintptr_t>(p.aux_addr));
            if (cb == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            const char *prefix = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.name_addr));
            std::string pfx_str = (prefix != nullptr) ? prefix : "";

            fs::path cdir = fs::path(prov->root_dir) / cont->name;
            std::error_code ec;
            for (const auto &entry : fs::directory_iterator(cdir, ec)) {
                if (!entry.is_regular_file(ec)) {
                    continue;
                }
                std::string bname = entry.path().filename().string();
                if (bname.size() >= 4u && bname.compare(bname.size() - 4u, 4u, ".tmp") == 0) {
                    continue;
                }
                if (!pfx_str.empty() && bname.rfind(pfx_str, 0) != 0) {
                    continue;
                }
                XGameSaveBlobInfo bi{};
                std::snprintf(bi.name, sizeof(bi.name), "%s", bname.c_str());
                bi.size = static_cast<uint32_t>(entry.file_size(ec));
                if (!cb(&bi, reinterpret_cast<void *>(static_cast<uintptr_t>(p.data_addr)))) {
                    break;
                }
            }
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GS_READ_BLOB: {
            auto *cont = get_valid_container(p.provider_or_container_handle);
            if (cont == nullptr) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            auto *prov = get_valid_provider(cont->provider_handle);
            if (prov == nullptr) {
                p.out_status = SCARLETT_E_GS_HANDLE_EXPIRED;
                break;
            }
            if (gs_state().policy.force_sync_failed) {
                p.out_status = SCARLETT_E_GS_CONTAINER_SYNC_FAILED;
                break;
            }
            if (gs_state().policy.unsynced_containers.count(cont->name) > 0u) {
                p.out_status = SCARLETT_E_GS_CONTAINER_NOT_IN_SYNC;
                break;
            }
            if (gs_state().policy.force_no_access) {
                p.out_status = SCARLETT_E_GS_NO_ACCESS;
                break;
            }

            const auto *names = reinterpret_cast<const char *const *>(static_cast<uintptr_t>(p.name_addr));
            uint32_t *count_ptr = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(p.aux_addr));
            if (names == nullptr || count_ptr == nullptr || *count_ptr == 0u) {
                p.out_status = SCARLETT_E_INVALIDARG;
                break;
            }

            const uint32_t nblobs = *count_ptr;
            fs::path cdir = fs::path(prov->root_dir) / cont->name;
            std::vector<std::pair<std::string, std::vector<uint8_t>>> loaded;
            loaded.reserve(nblobs);

            uint64_t required_bytes = static_cast<uint64_t>(nblobs) * sizeof(XGameSaveBlob);
            bool not_found = false;
            scarlett_hresult_t io_hr = SCARLETT_OK;

            for (uint32_t i = 0u; i < nblobs; ++i) {
                if (!is_valid_blob_name(names[i])) {
                    io_hr = SCARLETT_E_GS_INVALID_CONTAINER_NAME;
                    break;
                }
                fs::path bpath = cdir / names[i];
                std::ifstream in(bpath, std::ios::binary | std::ios::ate);
                if (!in.is_open()) {
                    if (errno == EACCES || errno == EPERM) {
                        io_hr = SCARLETT_E_GS_NO_ACCESS;
                    } else {
                        not_found = true;
                        io_hr = SCARLETT_E_GS_BLOB_NOT_FOUND;
                    }
                    break;
                }
                std::streamsize sz = in.tellg();
                in.seekg(0, std::ios::beg);
                std::vector<uint8_t> bytes(static_cast<size_t>(sz > 0 ? sz : 0));
                if (sz > 0 && !in.read(reinterpret_cast<char *>(bytes.data()), sz)) {
                    io_hr = SCARLETT_E_FAIL;
                    break;
                }
                required_bytes += bytes.size();
                loaded.emplace_back(names[i], std::move(bytes));
            }

            if (not_found || scarlett_failed(io_hr)) {
                p.out_status = io_hr;
                break;
            }

            p.out_count_or_size = required_bytes;
            auto *out_blobs = reinterpret_cast<XGameSaveBlob *>(static_cast<uintptr_t>(p.data_addr));
            if (out_blobs == nullptr || p.data_size < required_bytes) {
                p.out_status = SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL;
                break;
            }

            uint8_t *payload_ptr = reinterpret_cast<uint8_t *>(out_blobs + nblobs);
            for (uint32_t i = 0u; i < nblobs; ++i) {
                std::memset(&out_blobs[i], 0, sizeof(XGameSaveBlob));
                std::snprintf(out_blobs[i].info.name, sizeof(out_blobs[i].info.name),
                              "%s", loaded[i].first.c_str());
                out_blobs[i].info.size = static_cast<uint32_t>(loaded[i].second.size());
                out_blobs[i].data = payload_ptr;
                if (!loaded[i].second.empty()) {
                    std::memcpy(payload_ptr, loaded[i].second.data(), loaded[i].second.size());
                    payload_ptr += loaded[i].second.size();
                }
            }
            *count_ptr = nblobs;
            p.out_status = SCARLETT_OK;
            break;
        }

        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }

    return p.out_status;
}

scarlett_hresult_t GameSaveManager::files_op(scarlett_xgr_gs_files_params_t &p) {
    const auto subop = static_cast<scarlett_xgr_gs_files_subop_t>(p.subop);
    const bool is_async = (p.reserved != 0u);

    if (!is_async && subop == SCARLETT_XGR_GSF_GET_QUOTA) {
        if (!check_sync_allowed()) {
            p.out_status = SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED;
            return p.out_status;
        }
    }

    std::lock_guard<std::mutex> lock(gs_state().mutex);
    const char *scid = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.scid_addr));

    switch (subop) {
        case SCARLETT_XGR_GSF_GET_FOLDER: {
            if (gs_state().policy.force_user_canceled) {
                p.out_status = SCARLETT_E_GS_USER_CANCELED;
                break;
            }
            if (scid == nullptr || scid[0] == '\0') {
                p.out_status = SCARLETT_E_GS_NO_SERVICE_CONFIGURATION;
                break;
            }
            if (!PlatformManager::instance().is_user_valid_and_registered(p.user_handle)) {
                p.out_status = SCARLETT_E_GS_USER_NOT_REGISTERED_IN_SERVICE;
                break;
            }
            if (gs_state().active_kind == ActiveProviderKind::GameSave) {
                p.out_status = SCARLETT_E_GS_PROVIDER_MISMATCH;
                break;
            }

            std::string dir = resolve_saves_base_dir() + "/" + std::string(scid) + "/files";
            std::error_code ec;
            fs::create_directories(dir, ec);
            if (ec) {
                p.out_status = map_errno_to_gs(ec.value());
                break;
            }
            if (gs_state().files_sessions.insert(scid).second) {
                ++gs_state().active_files_providers;
            }
            gs_state().active_kind = ActiveProviderKind::GameSaveFiles;

            const uint64_t need = static_cast<uint64_t>(dir.size()) + 1u;
            p.out_u64 = need;
            char *dst = reinterpret_cast<char *>(static_cast<uintptr_t>(p.out_path_addr));
            if (dst == nullptr || p.buf_cap < need) {
                p.out_status = SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL;
                break;
            }
            std::memcpy(dst, dir.c_str(), need);
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GSF_GET_QUOTA: {
            if (scid == nullptr || scid[0] == '\0') {
                p.out_status = SCARLETT_E_GS_NO_SERVICE_CONFIGURATION;
                break;
            }
            if (gs_state().active_kind == ActiveProviderKind::GameSave) {
                p.out_status = SCARLETT_E_GS_PROVIDER_MISMATCH;
                break;
            }
            std::string dir = resolve_saves_base_dir() + "/" + std::string(scid) + "/files";
            uint64_t used = compute_dir_bytes(dir);
            uint64_t quota = gs_state().policy.quota_bytes;
            int64_t rem = (quota >= used) ? static_cast<int64_t>(quota - used) : 0;
            p.out_u64 = static_cast<uint64_t>(rem);
            p.out_status = SCARLETT_OK;
            break;
        }

        case SCARLETT_XGR_GSF_CLOSE_PROVIDER: {
            if (scid != nullptr && gs_state().files_sessions.erase(scid) > 0u) {
                if (gs_state().active_files_providers > 0u) {
                    --gs_state().active_files_providers;
                }
            }
            if (gs_state().providers.empty() && gs_state().active_files_providers == 0u) {
                gs_state().active_kind = ActiveProviderKind::None;
            }
            p.out_status = SCARLETT_OK;
            break;
        }

        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }

    return p.out_status;
}

} // namespace scarlett::xgameruntime::unixlib
