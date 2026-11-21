/*
 * Proton-Scarlett — XSystem, XThread, XUser, XPackage, XPersistentLocalStorage,
 *                   XStore, and XGameUI Implementation
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xgameruntime_internal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace scarlett::xgameruntime::unixlib {

namespace {

thread_local bool t_time_sensitive = false;

std::string to_lower_ascii(const char *s) {
    if (s == nullptr) return {};
    std::string out;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

scarlett_hresult_t copy_str_to_guest(uint64_t addr,
                                     uint32_t cap,
                                     const std::string &src,
                                     uint64_t *out_used = nullptr) {
    const uint64_t need = static_cast<uint64_t>(src.size()) + 1u;
    if (out_used != nullptr) {
        *out_used = need;
    }
    char *dst = reinterpret_cast<char *>(static_cast<uintptr_t>(addr));
    if (dst == nullptr) {
        return SCARLETT_E_POINTER;
    }
    if (cap < need) {
        if (cap > 0u) {
            std::memcpy(dst, src.data(), cap - 1u);
            dst[cap - 1u] = '\0';
        }
        return SCARLETT_HRESULT_C(0x8007007Au); /* ERROR_INSUFFICIENT_BUFFER */
    }
    std::memcpy(dst, src.c_str(), need);
    return SCARLETT_OK;
}

struct UserRecord {
    uint64_t handle{0u};
    uint32_t ref_count{1u};
    uint64_t xuid{UINT64_C(0x0009000000000001)};
    uint64_t local_id{1u};
    std::string gamertag{"ScarlettPlayer"};
    XUserAgeGroup age_group{XUserAgeGroup_Adult};
    XUserState state{XUserState_SignedIn};
    bool registered_in_service{true};
};

struct PackageChunkRecord {
    uint32_t chunk_id{0u};
    std::string tag;
    XPackageChunkAvailability availability{XPackageChunkAvailability_Ready};
};

struct MountRecord {
    uint64_t handle{0u};
    std::string package_id;
    std::string mount_path;
};

struct StoreContextRecord {
    uint64_t handle{0u};
    uint64_t user_handle{0u};
};

struct StoreLicenseRecord {
    uint64_t handle{0u};
    std::string id;
    bool valid{true};
};

struct StoreQueryRecord {
    uint64_t handle{0u};
    std::vector<XStoreProduct> products;
};

struct PlatformState {
    std::mutex mutex;
    uint64_t next_handle{0x2000u};
    std::unordered_map<uint64_t, UserRecord> users;
    std::unordered_map<uint64_t, MountRecord> mounts;
    std::vector<PackageChunkRecord> chunks;
    std::unordered_map<uint64_t, StoreContextRecord> store_ctxs;
    std::unordered_map<uint64_t, StoreLicenseRecord> store_licenses;
    std::unordered_map<uint64_t, StoreQueryRecord> store_queries;
    uint64_t pls_quota_bytes{UINT64_C(2147483648)}; /* 2 GiB */
    XGameUiNotificationPositionHint notif_hint{XGameUiNotificationPositionHint_BottomCenter};

    PlatformState() {
        chunks.push_back(PackageChunkRecord{1u, "Launch", XPackageChunkAvailability_Ready});
        chunks.push_back(PackageChunkRecord{2u, "Campaign", XPackageChunkAvailability_Ready});
        chunks.push_back(PackageChunkRecord{3u, "HighResTextures", XPackageChunkAvailability_Ready});
    }
};

PlatformState &pstate() {
    static PlatformState s;
    return s;
}

std::string resolve_prefix_root() {
    const char *pfx = std::getenv("PROTON_XBOX_PREFIX_DIR");
    if (pfx != nullptr && pfx[0] != '\0') {
        return std::string(pfx);
    }
    const char *winepfx = std::getenv("WINEPREFIX");
    if (winepfx != nullptr && winepfx[0] != '\0') {
        return std::string(winepfx);
    }
    return "/tmp/scarlett_pfx";
}

} // namespace

PlatformManager &PlatformManager::instance() {
    static PlatformManager mgr;
    return mgr;
}

void PlatformManager::initialize(scarlett_console_model_t model, uint32_t strict_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    default_model_ = model;
    strict_mode_ = strict_mode;
}

void PlatformManager::shutdown() {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    pstate().users.clear();
    pstate().mounts.clear();
    pstate().store_ctxs.clear();
    pstate().store_licenses.clear();
    pstate().store_queries.clear();
}

scarlett_console_model_t PlatformManager::active_console_model() const noexcept {
    const char *env = std::getenv(SCARLETT_ENV_MODEL);
    if (env != nullptr && env[0] != '\0') {
        std::string lower = to_lower_ascii(env);
        if (lower == "lockhart" || lower == "xss" || lower == "seriess") {
            return SCARLETT_MODEL_LOCKHART;
        }
        if (lower == "devkit" || lower == "xsx_devkit" || lower == "anaconda_devkit") {
            return SCARLETT_MODEL_DEVKIT;
        }
        if (lower == "anaconda" || lower == "xsx" || lower == "seriesx") {
            return SCARLETT_MODEL_ANACONDA;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return default_model_;
}

bool PlatformManager::is_strict_mode() const noexcept {
    const char *env = std::getenv(SCARLETT_ENV_STRICT);
    if (env != nullptr && env[0] != '\0') {
        return std::strcmp(env, "1") == 0 || std::strcmp(env, "true") == 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return strict_mode_ != 0u;
}

scarlett_hresult_t PlatformManager::xsystem_query(scarlett_xgr_xsystem_params_t &p) {
    const scarlett_console_model_t model = active_console_model();
    p.out_console_model = static_cast<uint32_t>(model);

    switch (model) {
        case SCARLETT_MODEL_LOCKHART:
            p.out_device_type = static_cast<uint32_t>(XSystemDeviceType_XboxScarlettLockhart);
            break;
        case SCARLETT_MODEL_DEVKIT:
            p.out_device_type = static_cast<uint32_t>(XSystemDeviceType_XboxScarlettDevkit);
            break;
        case SCARLETT_MODEL_ANACONDA:
        default:
            p.out_device_type = static_cast<uint32_t>(XSystemDeviceType_XboxScarlettAnaconda);
            break;
    }

    switch (static_cast<scarlett_xgr_xsystem_subop_t>(p.subop)) {
        case SCARLETT_XGR_XSYS_GET_DEVICE_TYPE:
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_XSYS_GET_CONSOLE_ID: {
            const char *env_cid = std::getenv("PROTON_XBOX_CONSOLE_ID");
            std::string cid = (env_cid != nullptr && env_cid[0] != '\0')
                                  ? std::string(env_cid)
                                  : "00000000.00000000.00000000.00000001";
            p.out_status = copy_str_to_guest(p.buf_addr, p.buf_cap, cid, &p.out_used);
            break;
        }
        case SCARLETT_XGR_XSYS_GET_SANDBOX_ID: {
            const char *env_sbx = std::getenv("PROTON_XBOX_SANDBOX_ID");
            std::string sbx = (env_sbx != nullptr && env_sbx[0] != '\0')
                                  ? std::string(env_sbx)
                                  : "RETAIL";
            p.out_status = copy_str_to_guest(p.buf_addr, p.buf_cap, sbx, &p.out_used);
            break;
        }
        case SCARLETT_XGR_XSYS_GET_DEVICE_ID: {
            std::string did = "{5C4A1E77-0001-4000-8000-000000000001}";
            p.out_status = copy_str_to_guest(p.buf_addr, p.buf_cap, did, &p.out_used);
            break;
        }
        case SCARLETT_XGR_XSYS_GET_ANALYTICS_INFO: {
            auto *info = reinterpret_cast<XSystemAnalyticsInfo *>(static_cast<uintptr_t>(p.buf_addr));
            if (info == nullptr || p.buf_cap < sizeof(XSystemAnalyticsInfo)) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            std::memset(info, 0, sizeof(*info));
            info->osVersion = XVersion{10u, 0u, 26200u, 0u};
            info->hostingOsVersion = XVersion{10u, 0u, 26200u, 0u};
            std::snprintf(info->family, sizeof(info->family), "Xbox");
            const char *form = "XboxScarlettAnaconda";
            if (model == SCARLETT_MODEL_LOCKHART) {
                form = "XboxScarlettLockhart";
            } else if (model == SCARLETT_MODEL_DEVKIT) {
                form = "XboxScarlettDevkit";
            }
            std::snprintf(info->form, sizeof(info->form), "%s", form);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSYS_GET_GAMEOS_VERSION: {
            scarlett_abi_version_t v{10u, 0u, 26200u, 0u, SCARLETT_GXDK_MAGIC, 0u};
            p.out_u64_a = scarlett_abi_pack_u64(v);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSYS_GET_MEMORY_BUDGET: {
            p.out_u64_a = scarlett_model_memory_budget_bytes(model);
            if (model == SCARLETT_MODEL_LOCKHART) {
                p.out_u64_b = SCARLETT_BUDGET_LOCKHART_FAST_BYTES;
            } else if (model == SCARLETT_MODEL_ANACONDA) {
                p.out_u64_b = SCARLETT_BUDGET_ANACONDA_FAST_BYTES;
            } else {
                p.out_u64_b = SCARLETT_BUDGET_DEVKIT_BYTES;
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

scarlett_hresult_t PlatformManager::xthread_time_sensitive(scarlett_loader_thread_rt_params_t &p) {
    bool prev = t_time_sensitive;
    p.out_previous_state = prev ? 1u : 0u;
    if (p.is_time_sensitive <= 1u) {
        bool next = (p.is_time_sensitive != 0u);
        t_time_sensitive = next;
        p.out_os_priority_applied = 0u;
        if (next) {
            if (::setpriority(PRIO_PROCESS, 0, -5) == 0) {
                p.out_os_priority_applied = 1u;
            }
        } else {
            if (::setpriority(PRIO_PROCESS, 0, 0) == 0) {
                p.out_os_priority_applied = 1u;
            }
        }
    }
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

bool PlatformManager::current_thread_is_time_sensitive() const noexcept {
    return t_time_sensitive;
}

bool PlatformManager::is_user_valid_and_registered(uint64_t user_handle) const {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    auto it = pstate().users.find(user_handle);
    if (it == pstate().users.end()) {
        return false;
    }
    return it->second.registered_in_service && it->second.state == XUserState_SignedIn;
}

uint64_t PlatformManager::get_user_xuid(uint64_t user_handle) const {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    auto it = pstate().users.find(user_handle);
    return (it != pstate().users.end()) ? it->second.xuid : 0u;
}

scarlett_hresult_t PlatformManager::xuser_op(scarlett_xgr_xuser_params_t &p) {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    switch (static_cast<scarlett_xgr_xuser_subop_t>(p.subop)) {
        case SCARLETT_XGR_XUSER_ADD: {
            UserRecord rec{};
            rec.handle = ++pstate().next_handle;
            rec.ref_count = 1u;
            rec.local_id = rec.handle;
            rec.xuid = UINT64_C(0x0009000000000000) | (rec.handle & 0xFFFFu);
            const char *gt = std::getenv("PROTON_XBOX_GAMERTAG");
            if (gt != nullptr && gt[0] != '\0') {
                rec.gamertag = gt;
            }
            pstate().users[rec.handle] = rec;
            p.out_handle_or_u64 = rec.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_DUPLICATE: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            ++it->second.ref_count;
            p.out_handle_or_u64 = it->second.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_CLOSE: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            if (--it->second.ref_count == 0u) {
                pstate().users.erase(it);
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_GET_ID: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_handle_or_u64 = it->second.xuid;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_GET_LOCAL_ID: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_handle_or_u64 = it->second.local_id;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_GET_GAMERTAG: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            std::string gt = it->second.gamertag;
            if (p.options_or_arg == static_cast<uint32_t>(XUserGamertagComponent_UniqueModern)) {
                gt += "#0001";
            }
            uint64_t used = 0u;
            p.out_status = copy_str_to_guest(p.buf_addr, p.buf_cap, gt, &used);
            p.out_u32 = static_cast<uint32_t>(used);
            break;
        }
        case SCARLETT_XGR_XUSER_GET_AGE_GROUP: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u32 = static_cast<uint32_t>(it->second.age_group);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_GET_STATE: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u32 = static_cast<uint32_t>(it->second.state);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_CHECK_PRIVILEGE: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            /* Offline/local grants non-cloud privileges; multiplayer depends on net mode */
            bool granted = true;
            uint32_t reason = 0u;
            if (p.options_or_arg == static_cast<uint32_t>(XUserPrivilege_Multiplayer) ||
                p.options_or_arg == static_cast<uint32_t>(XUserPrivilege_CrossPlay)) {
                const char *net = std::getenv(SCARLETT_ENV_NET_MODE);
                if (net == nullptr || std::strcmp(net, "offline") == 0) {
                    granted = false;
                    reason = 1u; /* Privilege check denied in offline mode */
                }
            }
            p.out_u32 = granted ? 1u : 0u;
            p.out_u32_b = reason;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XUSER_SET_SERVICE_REG: {
            auto it = pstate().users.find(p.user_handle);
            if (it == pstate().users.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            it->second.registered_in_service = (p.options_or_arg != 0u);
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t PlatformManager::xpackage_op(scarlett_xgr_xpackage_params_t &p) {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    const std::string pkg_id = "Scarlett.ProtonTitle_1.0.0.0_x64__8wekyb3d8bbwe";

    switch (static_cast<scarlett_xgr_xpackage_subop_t>(p.subop)) {
        case SCARLETT_XGR_XPKG_GET_CUR_ID: {
            p.out_status = copy_str_to_guest(p.str_addr, p.buf_cap, pkg_id);
            break;
        }
        case SCARLETT_XGR_XPKG_ENUM_PACKAGES: {
            auto cb = reinterpret_cast<XPackageEnumerationCallback>(static_cast<uintptr_t>(p.aux_addr));
            if (cb == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            XPackageDetails det{};
            std::snprintf(det.packageIdentifier, sizeof(det.packageIdentifier), "%s", pkg_id.c_str());
            det.version = XVersion{1u, 0u, 0u, 0u};
            det.kind = static_cast<XPackageKind>(p.chunk_id);
            std::snprintf(det.displayName, sizeof(det.displayName), "Proton-Scarlett Title");
            std::snprintf(det.description, sizeof(det.description), "Clean-Room Xbox Scarlett Package");
            std::snprintf(det.publisher, sizeof(det.publisher), "Proton-Scarlett");
            std::snprintf(det.storeId, sizeof(det.storeId), "9NBLGGH00001");
            det.installing = false;
            det.index = 0u;
            det.count = 1u;
            (void)cb(reinterpret_cast<void *>(static_cast<uintptr_t>(p.handle)), &det);
            p.out_count = 1u;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XPKG_ENUM_CHUNKS: {
            auto cb = reinterpret_cast<XPackageChunkEnumerationCallback>(static_cast<uintptr_t>(p.aux_addr));
            if (cb == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            uint32_t count = 0u;
            for (const auto &ch : pstate().chunks) {
                XPackageChunkSelector sel{};
                sel.type = 0u;
                sel.chunkId = ch.chunk_id;
                std::snprintf(sel.tag, sizeof(sel.tag), "%s", ch.tag.c_str());
                ++count;
                if (!cb(reinterpret_cast<void *>(static_cast<uintptr_t>(p.handle)), &sel, ch.availability)) {
                    break;
                }
            }
            p.out_count = count;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XPKG_MOUNT: {
            const char *req_pkg = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            std::string pid = (req_pkg != nullptr && req_pkg[0] != '\0') ? req_pkg : pkg_id;
            MountRecord m{};
            m.handle = ++pstate().next_handle;
            m.package_id = pid;
            m.mount_path = resolve_prefix_root() + "/drive_c/scarlett_pkg/" + pid;
            std::error_code ec;
            std::filesystem::create_directories(m.mount_path, ec);
            pstate().mounts[m.handle] = m;
            p.handle = m.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XPKG_CLOSE_MOUNT: {
            pstate().mounts.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XPKG_GET_MOUNT_PATH: {
            auto it = pstate().mounts.find(p.handle);
            if (it == pstate().mounts.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            uint64_t used = 0u;
            p.out_status = copy_str_to_guest(p.str_addr, p.buf_cap, it->second.mount_path, &used);
            p.out_count = static_cast<uint32_t>(used);
            break;
        }
        case SCARLETT_XGR_XPKG_FIND_CHUNK_AVAIL: {
            p.out_avail = static_cast<uint32_t>(XPackageChunkAvailability_Ready);
            const auto *sels = reinterpret_cast<const XPackageChunkSelector *>(static_cast<uintptr_t>(p.aux_addr));
            if (sels != nullptr && p.buf_cap > 0u) {
                for (uint32_t i = 0u; i < p.buf_cap; ++i) {
                    bool found = false;
                    for (const auto &ch : pstate().chunks) {
                        if (ch.chunk_id == sels[i].chunkId ||
                            (sels[i].tag[0] != '\0' && ch.tag == sels[i].tag)) {
                            found = true;
                            if (static_cast<uint32_t>(ch.availability) > p.out_avail) {
                                p.out_avail = static_cast<uint32_t>(ch.availability);
                            }
                            break;
                        }
                    }
                    if (!found) {
                        p.out_avail = static_cast<uint32_t>(XPackageChunkAvailability_Unavailable);
                    }
                }
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XPKG_REGISTER_CHUNK: {
            const char *tag = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            pstate().chunks.push_back(PackageChunkRecord{
                p.chunk_id,
                tag != nullptr ? tag : "Chunk",
                static_cast<XPackageChunkAvailability>(p.out_avail)});
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t PlatformManager::xpls_op(scarlett_xgr_xpls_params_t &p) {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    const std::string pls_dir = resolve_prefix_root() +
                                "/drive_c/users/steamuser/AppData/Local/ScarlettPLS/default";
    std::error_code ec;
    std::filesystem::create_directories(pls_dir, ec);

    auto calc_used_bytes = [&]() -> uint64_t {
        uint64_t total = 0u;
        std::error_code it_ec;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(pls_dir, it_ec)) {
            if (entry.is_regular_file(it_ec)) {
                total += entry.file_size(it_ec);
            }
        }
        return total;
    };

    switch (static_cast<scarlett_xgr_xpls_subop_t>(p.subop)) {
        case SCARLETT_XGR_XPLS_GET_PATH: {
            uint64_t used = 0u;
            p.out_status = copy_str_to_guest(p.buf_addr, p.buf_cap, pls_dir, &used);
            p.out_u64_a = used;
            break;
        }
        case SCARLETT_XGR_XPLS_GET_SPACE_INFO: {
            uint64_t used = calc_used_bytes();
            uint64_t quota = pstate().pls_quota_bytes;
            p.out_u64_b = quota;
            p.out_u64_a = (quota > used) ? (quota - used) : 0u;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XPLS_PROMPT_SPACE: {
            uint64_t used = calc_used_bytes();
            uint64_t avail = (pstate().pls_quota_bytes > used) ? (pstate().pls_quota_bytes - used) : 0u;
            p.out_status = (p.req_bytes <= avail) ? SCARLETT_OK : SCARLETT_E_GS_OUT_OF_LOCAL_STORAGE;
            break;
        }
        case SCARLETT_XGR_XPLS_SET_QUOTA: {
            pstate().pls_quota_bytes = p.req_bytes;
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t PlatformManager::xstore_op(scarlett_xgr_xstore_params_t &p) {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    switch (static_cast<scarlett_xgr_xstore_subop_t>(p.subop)) {
        case SCARLETT_XGR_XSTORE_CREATE_CTX: {
            StoreContextRecord rec{};
            rec.handle = ++pstate().next_handle;
            rec.user_handle = p.handle;
            pstate().store_ctxs[rec.handle] = rec;
            p.handle = rec.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_CLOSE_CTX: {
            pstate().store_ctxs.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_ACQUIRE_PKG_LIC:
        case SCARLETT_XGR_XSTORE_ACQUIRE_DUR_LIC: {
            if (pstate().store_ctxs.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            const char *id = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_addr));
            StoreLicenseRecord lic{};
            lic.handle = ++pstate().next_handle;
            lic.id = (id != nullptr) ? id : "9NBLGGH00001";
            lic.valid = true;
            pstate().store_licenses[lic.handle] = lic;
            p.aux_handle = lic.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_IS_LIC_VALID: {
            auto it = pstate().store_licenses.find(p.handle);
            p.out_bool = (it != pstate().store_licenses.end() && it->second.valid) ? 1u : 0u;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_CLOSE_LIC: {
            pstate().store_licenses.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_QUERY_ASSOC:
        case SCARLETT_XGR_XSTORE_QUERY_CUR_GAME: {
            if (pstate().store_ctxs.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            StoreQueryRecord q{};
            q.handle = ++pstate().next_handle;

            XStoreProduct main_game{};
            std::snprintf(main_game.storeId, sizeof(main_game.storeId), "9NBLGGH00001");
            std::snprintf(main_game.title, sizeof(main_game.title), "Proton-Scarlett Base Title");
            std::snprintf(main_game.description, sizeof(main_game.description), "Offline local game entitlement");
            std::snprintf(main_game.productKind, sizeof(main_game.productKind), "Game");
            main_game.isInUserCollection = true;
            main_game.hasDigitalDownload = true;
            q.products.push_back(main_game);

            if (static_cast<scarlett_xgr_xstore_subop_t>(p.subop) == SCARLETT_XGR_XSTORE_QUERY_ASSOC) {
                XStoreProduct dlc{};
                std::snprintf(dlc.storeId, sizeof(dlc.storeId), "9NBLGGH00002");
                std::snprintf(dlc.title, sizeof(dlc.title), "Expansion Pack");
                std::snprintf(dlc.description, sizeof(dlc.description), "Offline local DLC entitlement");
                std::snprintf(dlc.productKind, sizeof(dlc.productKind), "Durable");
                dlc.isInUserCollection = true;
                dlc.hasDigitalDownload = true;
                q.products.push_back(dlc);
            }
            pstate().store_queries[q.handle] = q;
            p.aux_handle = q.handle;
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_ENUM_PRODUCTS: {
            auto it = pstate().store_queries.find(p.handle);
            if (it == pstate().store_queries.end()) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            auto cb = reinterpret_cast<XStoreProductQueryCallback>(static_cast<uintptr_t>(p.cb_addr));
            if (cb == nullptr) {
                p.out_status = SCARLETT_E_POINTER;
                break;
            }
            for (const auto &prod : it->second.products) {
                if (!cb(&prod, reinterpret_cast<void *>(static_cast<uintptr_t>(p.ctx_addr)))) {
                    break;
                }
            }
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_CLOSE_QUERY: {
            pstate().store_queries.erase(p.handle);
            p.out_status = SCARLETT_OK;
            break;
        }
        case SCARLETT_XGR_XSTORE_QUERY_CONSUMABLE: {
            if (pstate().store_ctxs.count(p.handle) == 0u) {
                p.out_status = SCARLETT_E_HANDLE;
                break;
            }
            p.out_u32 = 100u; /* Deterministic offline local balance */
            p.out_status = SCARLETT_OK;
            break;
        }
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

scarlett_hresult_t PlatformManager::xgameui_op(scarlett_xgr_xgameui_params_t &p) {
    std::lock_guard<std::mutex> lock(pstate().mutex);
    switch (static_cast<scarlett_xgr_xgameui_subop_t>(p.subop)) {
        case SCARLETT_XGR_UI_MSG_DIALOG:
            p.out_u32 = p.arg_u32; /* Return configured defaultButton */
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_UI_TEXT_ENTRY: {
            const char *def_txt = reinterpret_cast<const char *>(static_cast<uintptr_t>(p.str_b_addr));
            std::string text = (def_txt != nullptr) ? def_txt : "ScarlettInput";
            uint64_t used = 0u;
            p.out_status = copy_str_to_guest(p.out_buf_addr, p.out_buf_cap, text, &used);
            p.out_u32 = static_cast<uint32_t>(used);
            break;
        }
        case SCARLETT_XGR_UI_ERROR_DIALOG:
        case SCARLETT_XGR_UI_PROFILE_CARD:
            p.out_status = SCARLETT_OK;
            break;
        case SCARLETT_XGR_UI_NOTIF_HINT:
            pstate().notif_hint = static_cast<XGameUiNotificationPositionHint>(p.arg_u32);
            p.out_status = SCARLETT_OK;
            break;
        default:
            p.out_status = SCARLETT_E_INVALIDARG;
            break;
    }
    return p.out_status;
}

} // namespace scarlett::xgameruntime::unixlib
