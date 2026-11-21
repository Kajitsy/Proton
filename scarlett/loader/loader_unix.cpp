/*
 * Proton-Scarlett — loader_unix.so Native Linux Unixlib Implementation
 *
 * Implements the System V AMD64 native backend for:
 *   - MicrosoftGame.Config XML parsing & XSystem/XPackage metadata
 *   - GAME: / D:\ CoW overlay VFS (with whiteouts and case-insensitivity)
 *   - Clean-Room unencrypted XVD/XVC & read-only NTFS inspection/mounting
 *   - Diagnostic auto-stub reporting (PROTON_XBOX_STRICT=0|1)
 *   - PSM AppNotify callback registry
 *   - Time-sensitive thread state, net policy & atomic XGameSave commit
 */

#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "game_config.hpp"
#include "loader_unix_abi.h"
#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"
#include "vfs_cow.hpp"

namespace {

struct PsmRegistration {
    uint64_t handle = 0;
    uint32_t kind = 0;
    uint64_t callback_addr = 0;
    uint64_t context_addr = 0;
};

struct LoaderUnixState {
    std::mutex mutex;
    bool initialized = false;
    uint32_t console_model = SCARLETT_MODEL_ANACONDA;
    uint32_t strict_mode = 0;
    uint32_t shader_mode = SCARLETT_SHADER_MODE_ADAPTIVE;
    uint32_t net_mode = SCARLETT_NET_MODE_OFFLINE;

    uint64_t next_mount_handle = 1;
    std::unordered_map<uint64_t, std::unique_ptr<scarlett::loader::GameCowVfs>> mounts;

    bool has_metadata = false;
    scarlett_loader_title_metadata_t active_metadata{};

    uint64_t next_psm_handle = 1;
    std::vector<PsmRegistration> psm_registrations;

    bool has_last_stub = false;
    uint32_t total_stub_hits = 0;
    scarlett_stub_context_t last_stub{};
};

LoaderUnixState& g_state() {
    static LoaderUnixState state;
    return state;
}

thread_local uint32_t tls_time_sensitive = 0;

bool read_env_flag_01(const char* name, bool& out_val) noexcept {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return false;
    }
    if (std::strcmp(v, "1") == 0) {
        out_val = true;
        return true;
    }
    if (std::strcmp(v, "0") == 0) {
        out_val = false;
        return true;
    }
    return false;
}

std::string resolve_overlay_dir_from_params_or_env(
    uint64_t overlay_addr,
    uint32_t overlay_len) {
    if (overlay_addr != 0u && overlay_len > 0u) {
        const char* p = reinterpret_cast<const char*>(static_cast<uintptr_t>(overlay_addr));
        return std::string(p, strnlen(p, overlay_len));
    }
    const char* env_dir = std::getenv(SCARLETT_ENV_OVERLAY_DIR);
    if (env_dir && *env_dir) {
        return std::string(env_dir);
    }
    return {};
}

bool is_xvd_file_magic(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    uint64_t magic = 0;
    ssize_t r = ::pread(fd, &magic, sizeof(magic), SCARLETT_XVD_HEADER_OFFSET);
    ::close(fd);
    return r == static_cast<ssize_t>(sizeof(magic)) && magic == SCARLETT_XVD_MAGIC_LE64;
}

bool is_lan_ip_allowed(const std::string& host) noexcept {
    if (host == "localhost") {
        return true;
    }
    struct in_addr addr4{};
    if (::inet_pton(AF_INET, host.c_str(), &addr4) == 1) {
        uint32_t ip = ntohl(addr4.s_addr);
        if ((ip >> 24) == 127u) {
            return true; // 127.0.0.0/8
        }
        if ((ip >> 24) == 10u) {
            return true; // 10.0.0.0/8
        }
        if ((ip >> 20) == ((172u << 4) | 1u)) {
            return true; // 172.16.0.0/12
        }
        if ((ip >> 16) == ((192u << 8) | 168u)) {
            return true; // 192.168.0.0/16
        }
        if ((ip >> 16) == ((169u << 8) | 254u)) {
            return true; // 169.254.0.0/16
        }
        return false;
    }
    struct in6_addr addr6{};
    if (::inet_pton(AF_INET6, host.c_str(), &addr6) == 1) {
        // ::1
        bool is_loopback = true;
        for (size_t i = 0; i < 15; ++i) {
            if (addr6.s6_addr[i] != 0) {
                is_loopback = false;
                break;
            }
        }
        if (is_loopback && addr6.s6_addr[15] == 1u) {
            return true;
        }
        // fe80::/10 link-local
        if (addr6.s6_addr[0] == 0xFEu && (addr6.s6_addr[1] & 0xC0u) == 0x80u) {
            return true;
        }
        // fc00::/7 ULA
        if ((addr6.s6_addr[0] & 0xFEu) == 0xFCu) {
            return true;
        }
        return false;
    }
    return false;
}

scarlett::loader::GameCowVfs* find_mount_locked(LoaderUnixState& st, uint64_t handle) noexcept {
    if (handle == 0u && !st.mounts.empty()) {
        return st.mounts.begin()->second.get();
    }
    auto it = st.mounts.find(handle);
    if (it == st.mounts.end()) {
        return nullptr;
    }
    return it->second.get();
}

int32_t handle_init(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_unix_init_params_t*>(raw);
    p->out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;

    if (p->abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        p->module_id != SCARLETT_UNIX_MOD_LOADER) {
        p->out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return SCARLETT_ERR_ABI_VERSION_MISMATCH;
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.initialized = true;
    st.console_model = p->console_model;
    st.strict_mode = p->strict_mode ? 1u : 0u;
    bool env_strict = false;
    if (read_env_flag_01(SCARLETT_ENV_STRICT, env_strict)) {
        st.strict_mode = env_strict ? 1u : 0u;
    }
    st.shader_mode = p->shader_mode;
    st.net_mode = p->net_mode;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_mount_package(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_mount_pkg_params_t*>(raw);
    p->out_volume_flags = 0;
    p->out_is_encrypted = 0;
    p->out_mount_handle = 0;

    if (p->package_path_utf8_addr == 0u || p->package_path_len == 0u) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }

    const char* pkg_ptr =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(p->package_path_utf8_addr));
    std::string pkg_str(pkg_ptr, strnlen(pkg_ptr, p->package_path_len));
    std::string ov_str = resolve_overlay_dir_from_params_or_env(
        p->overlay_dir_utf8_addr, p->overlay_dir_len);

    if (ov_str.empty()) {
        p->out_status = SCARLETT_ERR_VFS_OVERLAY_FAILED;
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    std::filesystem::path pkg_path(pkg_str);
    std::filesystem::path ov_path(ov_str);
    auto vfs = std::make_unique<scarlett::loader::GameCowVfs>();

    std::error_code ec;
    scarlett_hresult_t hr = SCARLETT_OK;
    if (std::filesystem::is_directory(pkg_path, ec)) {
        hr = vfs->mount_directory(pkg_path, ov_path);
        if (scarlett_succeeded(hr)) {
            p->out_volume_flags = SCARLETT_XVD_VOLUME_FLAG_READ_ONLY |
                                  SCARLETT_XVD_VOLUME_FLAG_ENCRYPTION_DISABLED;
        }
    } else if (std::filesystem::is_regular_file(pkg_path, ec) || is_xvd_file_magic(pkg_path)) {
        uint32_t vol_flags = 0;
        bool is_enc = false;
        hr = vfs->mount_xvd_package(pkg_path, ov_path, vol_flags, is_enc);
        p->out_volume_flags = vol_flags;
        p->out_is_encrypted = is_enc ? 1u : 0u;
    } else {
        hr = SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    if (scarlett_failed(hr)) {
        p->out_status = hr;
        return hr;
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    uint64_t handle = st.next_mount_handle++;
    st.mounts.emplace(handle, std::move(vfs));
    p->out_mount_handle = handle;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_unmount_package(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_unmount_pkg_params_t*>(raw);
    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    auto it = st.mounts.find(p->mount_handle);
    if (it == st.mounts.end()) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }
    st.mounts.erase(it);
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_resolve_game_path(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_resolve_path_params_t*>(raw);
    p->out_resolved_layer = SCARLETT_VFS_LAYER_NONE;
    p->out_host_path_len = 0;

    if (p->game_path_utf8_addr == 0u || p->game_path_len == 0u) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }

    const char* gptr =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(p->game_path_utf8_addr));
    std::string gpath(gptr, strnlen(gptr, p->game_path_len));

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, p->mount_handle);
    if (!vfs) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }

    std::string resolved;
    scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
    scarlett_hresult_t hr = vfs->resolve_path(gpath, p->access_flags, resolved, layer);
    if (scarlett_failed(hr)) {
        p->out_status = hr;
        return hr;
    }

    p->out_resolved_layer = static_cast<uint32_t>(layer);
    p->out_host_path_len = static_cast<uint32_t>(resolved.size());
    if (p->out_host_path_utf8_addr != 0u && p->out_host_path_cap > 0u) {
        if (resolved.size() + 1u > p->out_host_path_cap) {
            p->out_status = SCARLETT_E_INVALIDARG;
            return SCARLETT_E_INVALIDARG;
        }
        char* dst = reinterpret_cast<char*>(static_cast<uintptr_t>(p->out_host_path_utf8_addr));
        std::memcpy(dst, resolved.data(), resolved.size());
        dst[resolved.size()] = '\0';
    }

    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_report_stub(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_report_stub_params_t*>(raw);

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);

    bool env_strict = false;
    uint32_t active_strict = st.strict_mode;
    if (read_env_flag_01(SCARLETT_ENV_STRICT, env_strict)) {
        active_strict = env_strict ? 1u : 0u;
    }
    if (p->ctx.strict_mode != 0u) {
        active_strict = 1u;
    }

    ++st.total_stub_hits;
    p->ctx.strict_mode = active_strict;
    p->ctx.hit_count = st.total_stub_hits;
    p->ctx.return_hr = scarlett_stub_enforce_error(p->ctx.return_hr);
    if (active_strict != 0u) {
        p->ctx.flags |= SCARLETT_STUB_FLAG_STRICT_MODE;
    }

    st.has_last_stub = true;
    st.last_stub = p->ctx;

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&p->ctx);
    p->out_action = static_cast<uint32_t>(action);

    std::fprintf(stderr,
                 "[scarlett-stub] hit=%u dll=%s sym=%s ord=%u slot=%u "
                 "caller=%s+0x%llx rc=(0x%llx,0x%llx,0x%llx,0x%llx) hr=0x%08x strict=%u\n",
                 p->ctx.hit_count,
                 p->ctx.dll_name,
                 p->ctx.symbol_name,
                 p->ctx.ordinal,
                 p->ctx.vtable_slot,
                 p->ctx.caller_module_name,
                 static_cast<unsigned long long>(p->ctx.caller_rva),
                 static_cast<unsigned long long>(p->ctx.rcx),
                 static_cast<unsigned long long>(p->ctx.rdx),
                 static_cast<unsigned long long>(p->ctx.r8),
                 static_cast<unsigned long long>(p->ctx.r9),
                 static_cast<uint32_t>(p->ctx.return_hr),
                 active_strict);

    p->out_status = (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT)
                        ? SCARLETT_ERR_STRICT_STUB_ABORT
                        : p->ctx.return_hr;
    return p->out_status;
}

int32_t handle_set_time_sensitive(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_thread_rt_params_t*>(raw);
    p->out_previous_state = tls_time_sensitive;
    tls_time_sensitive = p->is_time_sensitive ? 1u : 0u;
    int prio_rc = ::setpriority(PRIO_PROCESS, 0, tls_time_sensitive ? -5 : 0);
    p->out_os_priority_applied = (prio_rc == 0) ? 1u : 0u;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_check_net_policy(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_net_policy_params_t*>(raw);
    p->out_allowed = 0;

    std::string host;
    if (p->host_utf8_addr != 0u && p->host_len > 0u) {
        const char* hptr =
            reinterpret_cast<const char*>(static_cast<uintptr_t>(p->host_utf8_addr));
        host.assign(hptr, strnlen(hptr, p->host_len));
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.net_mode == SCARLETT_NET_MODE_OFFLINE) {
        p->out_status = SCARLETT_ERR_NET_OFFLINE_BLOCKED;
        return SCARLETT_ERR_NET_OFFLINE_BLOCKED;
    }
    if (st.net_mode == SCARLETT_NET_MODE_LAN) {
        if (is_lan_ip_allowed(host)) {
            p->out_allowed = 1;
            p->out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        p->out_status = SCARLETT_ERR_NET_LAN_POLICY_BLOCKED;
        return SCARLETT_ERR_NET_LAN_POLICY_BLOCKED;
    }

    p->out_allowed = 1;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_gamesave_commit_blob(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_gamesave_commit_params_t*>(raw);
    if (p->dest_path_utf8_addr == 0u || p->dest_path_len == 0u ||
        (p->data_size > 0u && p->data_addr == 0u)) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }
    const char* dptr =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(p->dest_path_utf8_addr));
    std::filesystem::path dest(std::string(dptr, strnlen(dptr, p->dest_path_len)));
    std::error_code ec;
    if (dest.has_parent_path()) {
        std::filesystem::create_directories(dest.parent_path(), ec);
        if (ec) {
            p->out_status = SCARLETT_E_FAIL;
            return SCARLETT_E_FAIL;
        }
    }

    std::filesystem::path tmp = dest;
    tmp += ".scarlett.tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        p->out_status = SCARLETT_E_FAIL;
        return SCARLETT_E_FAIL;
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(p->data_addr));
    uint32_t written = 0;
    while (written < p->data_size) {
        ssize_t w = ::write(fd, src + written, p->data_size - written);
        if (w <= 0) {
            ::close(fd);
            std::filesystem::remove(tmp, ec);
            p->out_status = SCARLETT_E_FAIL;
            return SCARLETT_E_FAIL;
        }
        written += static_cast<uint32_t>(w);
    }
    ::fsync(fd);
    ::close(fd);
    if (::rename(tmp.c_str(), dest.c_str()) != 0) {
        std::filesystem::remove(tmp, ec);
        p->out_status = SCARLETT_E_FAIL;
        return SCARLETT_E_FAIL;
    }
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_load_config(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_config_params_t*>(raw);
    if (p->xml_or_path_utf8_addr == 0u || p->input_len == 0u) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }
    const char* in_ptr =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(p->xml_or_path_utf8_addr));
    std::string input(in_ptr, strnlen(in_ptr, p->input_len));

    scarlett::loader::MicrosoftGameConfig cfg;
    scarlett_hresult_t hr = SCARLETT_OK;
    if (p->is_xml_buffer != 0u) {
        hr = scarlett::loader::parse_microsoft_game_config(input, cfg);
    } else {
        hr = scarlett::loader::parse_microsoft_game_config_file(input, cfg);
    }
    if (scarlett_failed(hr)) {
        p->out_status = hr;
        return hr;
    }

    if (p->game_dir_utf8_addr != 0u && p->game_dir_len > 0u) {
        const char* gd =
            reinterpret_cast<const char*>(static_cast<uintptr_t>(p->game_dir_utf8_addr));
        std::filesystem::path game_dir(std::string(gd, strnlen(gd, p->game_dir_len)));
        hr = scarlett::loader::select_scarlett_pe64_executable(cfg, game_dir);
        if (scarlett_failed(hr)) {
            p->out_status = hr;
            return hr;
        }
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::populate_loader_title_metadata(
        cfg,
        static_cast<scarlett_console_model_t>(st.console_model),
        st.active_metadata);
    st.has_metadata = true;

    if (p->out_metadata_addr != 0u) {
        auto* out_meta = reinterpret_cast<scarlett_loader_title_metadata_t*>(
            static_cast<uintptr_t>(p->out_metadata_addr));
        *out_meta = st.active_metadata;
    }
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_get_metadata(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_title_metadata_t*>(raw);
    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!st.has_metadata) {
        std::memset(p, 0, sizeof(*p));
        p->abi_version = SCARLETT_CONTRACT_ABI_VERSION;
        p->console_model = st.console_model;
        return SCARLETT_E_FAIL;
    }
    *p = st.active_metadata;
    return SCARLETT_OK;
}

int32_t handle_vfs_open(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_vfs_open_params_t*>(raw);
    p->out_file_handle = 0;
    p->out_file_size = 0;
    p->out_layer = SCARLETT_VFS_LAYER_NONE;

    if (p->game_path_utf8_addr == 0u || p->game_path_len == 0u) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }
    const char* gp =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(p->game_path_utf8_addr));
    std::string gpath(gp, strnlen(gp, p->game_path_len));

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, p->mount_handle);
    if (!vfs) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }

    uint64_t fh = 0;
    uint64_t fsz = 0;
    scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
    scarlett_hresult_t hr = vfs->open_file(gpath, p->access_flags, fh, fsz, layer);
    if (scarlett_failed(hr)) {
        p->out_status = hr;
        return hr;
    }
    p->out_file_handle = (p->mount_handle << 32) | (fh & UINT32_C(0xFFFFFFFF));
    p->out_file_size = fsz;
    p->out_layer = static_cast<uint32_t>(layer);
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_vfs_read(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_vfs_rw_params_t*>(raw);
    p->out_bytes_transferred = 0;
    uint64_t mhandle = p->file_handle >> 32;
    uint64_t fhandle = p->file_handle & UINT32_C(0xFFFFFFFF);

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, mhandle);
    if (!vfs) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }
    void* dst = reinterpret_cast<void*>(static_cast<uintptr_t>(p->buffer_addr));
    uint32_t got = 0;
    scarlett_hresult_t hr = vfs->read_file(fhandle, p->offset, dst, p->byte_count, got);
    p->out_bytes_transferred = got;
    p->out_status = hr;
    return hr;
}

int32_t handle_vfs_write(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_vfs_rw_params_t*>(raw);
    p->out_bytes_transferred = 0;
    uint64_t mhandle = p->file_handle >> 32;
    uint64_t fhandle = p->file_handle & UINT32_C(0xFFFFFFFF);

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, mhandle);
    if (!vfs) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }
    const void* src = reinterpret_cast<const void*>(static_cast<uintptr_t>(p->buffer_addr));
    uint32_t written = 0;
    scarlett_hresult_t hr = vfs->write_file(fhandle, p->offset, src, p->byte_count, written);
    p->out_bytes_transferred = written;
    p->out_status = hr;
    return hr;
}

int32_t handle_vfs_close(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_vfs_close_params_t*>(raw);
    uint64_t mhandle = p->file_handle >> 32;
    uint64_t fhandle = p->file_handle & UINT32_C(0xFFFFFFFF);

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, mhandle);
    if (!vfs) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }
    scarlett_hresult_t hr = vfs->close_file(fhandle);
    p->out_status = hr;
    return hr;
}

int32_t handle_vfs_path_op(uint32_t op, void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_vfs_path_op_params_t*>(raw);
    if (p->game_path_utf8_addr == 0u || p->game_path_len == 0u) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }
    const char* gp =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(p->game_path_utf8_addr));
    std::string gpath(gp, strnlen(gp, p->game_path_len));

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, p->mount_handle);
    if (!vfs) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }

    scarlett_hresult_t hr = SCARLETT_OK;
    if (op == SCARLETT_LOADER_UNIX_EX_VFS_DELETE) {
        hr = vfs->remove_path(gpath);
    } else if (op == SCARLETT_LOADER_UNIX_EX_VFS_MKDIR) {
        hr = vfs->create_directory(gpath);
    } else {
        scarlett::loader::VfsStatResult stat_res;
        hr = vfs->stat_path(gpath, stat_res);
        if (scarlett_succeeded(hr)) {
            p->out_exists = stat_res.exists ? 1u : 0u;
            p->out_is_directory = stat_res.is_directory ? 1u : 0u;
            p->out_layer = static_cast<uint32_t>(stat_res.layer);
            p->out_size_bytes = stat_res.size_bytes;
        }
    }
    p->out_status = hr;
    return hr;
}

int32_t handle_psm_register(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_psm_register_params_t*>(raw);
    p->out_registration_handle = 0;

    if (p->callback_addr == 0u ||
        (p->notify_kind != SCARLETT_PSM_NOTIFY_APP_STATE &&
         p->notify_kind != SCARLETT_PSM_NOTIFY_CONSTRAINED)) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    uint64_t handle = st.next_psm_handle++;
    st.psm_registrations.push_back(PsmRegistration{
        handle,
        p->notify_kind,
        p->callback_addr,
        p->context_addr,
    });
    p->out_registration_handle = handle;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_psm_unregister(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_psm_unregister_params_t*>(raw);
    if (p->registration_handle == 0u) {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    for (size_t i = 0; i < st.psm_registrations.size(); ++i) {
        if (st.psm_registrations[i].handle == p->registration_handle &&
            (p->notify_kind == 0u || st.psm_registrations[i].kind == p->notify_kind)) {
            st.psm_registrations.erase(st.psm_registrations.begin() +
                                       static_cast<std::ptrdiff_t>(i));
            p->out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
    }
    p->out_status = SCARLETT_E_HANDLE;
    return SCARLETT_E_HANDLE;
}

int32_t handle_psm_snapshot_cb(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_psm_snapshot_params_t*>(raw);
    p->out_count = 0;

    uint32_t target_kind = 0;
    if (p->event_type == SCARLETT_PSM_EVENT_RESUME ||
        p->event_type == SCARLETT_PSM_EVENT_SUSPEND) {
        target_kind = SCARLETT_PSM_NOTIFY_APP_STATE;
    } else if (p->event_type == SCARLETT_PSM_EVENT_UNCONSTRAINED ||
               p->event_type == SCARLETT_PSM_EVENT_CONSTRAINED) {
        target_kind = SCARLETT_PSM_NOTIFY_CONSTRAINED;
    } else {
        p->out_status = SCARLETT_E_INVALIDARG;
        return SCARLETT_E_INVALIDARG;
    }

    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);

    auto* out_entries = reinterpret_cast<scarlett_psm_cb_entry_t*>(
        static_cast<uintptr_t>(p->out_entries_addr));

    uint32_t count = 0;
    for (const auto& reg : st.psm_registrations) {
        if (reg.kind != target_kind) {
            continue;
        }
        if (out_entries && count < p->capacity) {
            out_entries[count].callback_addr = reg.callback_addr;
            out_entries[count].context_addr = reg.context_addr;
        }
        ++count;
    }
    p->out_count = count;
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_get_last_stub(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_last_stub_params_t*>(raw);
    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    p->out_has_stub = st.has_last_stub ? 1u : 0u;
    p->out_total_hits = st.total_stub_hits;
    if (st.has_last_stub) {
        p->out_ctx = st.last_stub;
    } else {
        std::memset(&p->out_ctx, 0, sizeof(p->out_ctx));
    }
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

int32_t handle_query_xvd_info(void* raw) {
    if (!raw) {
        return SCARLETT_E_POINTER;
    }
    auto* p = static_cast<scarlett_loader_xvd_info_params_t*>(raw);
    LoaderUnixState& st = g_state();
    std::lock_guard<std::mutex> lock(st.mutex);
    scarlett::loader::GameCowVfs* vfs = find_mount_locked(st, p->mount_handle);
    if (!vfs || !vfs->xvd_reader()) {
        p->out_status = SCARLETT_E_HANDLE;
        return SCARLETT_E_HANDLE;
    }
    const auto& hdr = vfs->xvd_reader()->header();
    const auto& xvc = vfs->xvd_reader()->xvc();
    p->out_format_version = hdr.format_version;
    p->out_volume_flags = hdr.volume_flags;
    p->out_drive_size = hdr.drive_size;
    p->out_user_data_length = hdr.user_data_length;
    p->out_xvc_data_length = hdr.xvc_data_length;
    p->out_dyn_header_length = hdr.dyn_header_length;
    p->out_xvc_region_count = static_cast<uint32_t>(xvc.regions.size());
    p->out_xvc_chunk_count = static_cast<uint32_t>(xvc.chunks.size());
    p->out_xvc_update_segment_count = static_cast<uint32_t>(xvc.update_segments.size());
    std::memcpy(p->out_content_id, hdr.content_id, sizeof(p->out_content_id));
    p->out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

} // namespace

extern "C" {

int32_t scarlett_loader_unix_dispatch(uint32_t opcode, void* params) {
    switch (opcode) {
        case SCARLETT_LOADER_UNIX_INIT:
            return handle_init(params);
        case SCARLETT_LOADER_UNIX_MOUNT_PACKAGE:
            return handle_mount_package(params);
        case SCARLETT_LOADER_UNIX_UNMOUNT_PACKAGE:
            return handle_unmount_package(params);
        case SCARLETT_LOADER_UNIX_RESOLVE_GAME_PATH:
            return handle_resolve_game_path(params);
        case SCARLETT_LOADER_UNIX_REPORT_STUB:
            return handle_report_stub(params);
        case SCARLETT_LOADER_UNIX_SET_TIME_SENSITIVE:
            return handle_set_time_sensitive(params);
        case SCARLETT_LOADER_UNIX_CHECK_NET_POLICY:
            return handle_check_net_policy(params);
        case SCARLETT_LOADER_UNIX_GAMESAVE_COMMIT_BLOB:
            return handle_gamesave_commit_blob(params);

        case SCARLETT_LOADER_UNIX_EX_LOAD_CONFIG:
            return handle_load_config(params);
        case SCARLETT_LOADER_UNIX_EX_GET_METADATA:
            return handle_get_metadata(params);
        case SCARLETT_LOADER_UNIX_EX_VFS_OPEN:
            return handle_vfs_open(params);
        case SCARLETT_LOADER_UNIX_EX_VFS_READ:
            return handle_vfs_read(params);
        case SCARLETT_LOADER_UNIX_EX_VFS_WRITE:
            return handle_vfs_write(params);
        case SCARLETT_LOADER_UNIX_EX_VFS_CLOSE:
            return handle_vfs_close(params);
        case SCARLETT_LOADER_UNIX_EX_VFS_DELETE:
        case SCARLETT_LOADER_UNIX_EX_VFS_MKDIR:
        case SCARLETT_LOADER_UNIX_EX_VFS_STAT:
            return handle_vfs_path_op(opcode, params);
        case SCARLETT_LOADER_UNIX_EX_PSM_REGISTER:
            return handle_psm_register(params);
        case SCARLETT_LOADER_UNIX_EX_PSM_UNREGISTER:
            return handle_psm_unregister(params);
        case SCARLETT_LOADER_UNIX_EX_PSM_SNAPSHOT_CB:
            return handle_psm_snapshot_cb(params);
        case SCARLETT_LOADER_UNIX_EX_GET_LAST_STUB:
            return handle_get_last_stub(params);
        case SCARLETT_LOADER_UNIX_EX_QUERY_XVD_INFO:
            return handle_query_xvd_info(params);

        default:
            return SCARLETT_ERR_UNIX_CALL_INVALID_OPCODE;
    }
}

/*
 * Wine 11.0 unixlib dispatch entrypoints (System V AMD64 ELF .so table).
 * Each slot takes `void *args` and returns NTSTATUS / scarlett_hresult_t.
 */
typedef int32_t (*wine_unixlib_entry_t)(void* args);

static int32_t unix_op_init(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_INIT, args);
}
static int32_t unix_op_mount(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_MOUNT_PACKAGE, args);
}
static int32_t unix_op_unmount(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_UNMOUNT_PACKAGE, args);
}
static int32_t unix_op_resolve_path(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_RESOLVE_GAME_PATH, args);
}
static int32_t unix_op_report_stub(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_REPORT_STUB, args);
}
static int32_t unix_op_set_time_sensitive(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_SET_TIME_SENSITIVE, args);
}
static int32_t unix_op_check_net_policy(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_CHECK_NET_POLICY, args);
}
static int32_t unix_op_gamesave_commit(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_GAMESAVE_COMMIT_BLOB, args);
}
static int32_t unix_op_ex_load_config(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_LOAD_CONFIG, args);
}
static int32_t unix_op_ex_get_metadata(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_GET_METADATA, args);
}
static int32_t unix_op_ex_vfs_open(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_OPEN, args);
}
static int32_t unix_op_ex_vfs_read(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_READ, args);
}
static int32_t unix_op_ex_vfs_write(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_WRITE, args);
}
static int32_t unix_op_ex_vfs_close(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_CLOSE, args);
}
static int32_t unix_op_ex_vfs_delete(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_DELETE, args);
}
static int32_t unix_op_ex_vfs_mkdir(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_MKDIR, args);
}
static int32_t unix_op_ex_vfs_stat(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_VFS_STAT, args);
}
static int32_t unix_op_ex_psm_register(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_PSM_REGISTER, args);
}
static int32_t unix_op_ex_psm_unregister(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_PSM_UNREGISTER, args);
}
static int32_t unix_op_ex_psm_snapshot(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_PSM_SNAPSHOT_CB, args);
}
static int32_t unix_op_ex_get_last_stub(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_GET_LAST_STUB, args);
}
static int32_t unix_op_ex_query_xvd_info(void* args) {
    return scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_QUERY_XVD_INFO, args);
}

__attribute__((visibility("default")))
extern const wine_unixlib_entry_t __wine_unix_call_funcs[SCARLETT_LOADER_UNIX_EX_OP_COUNT];

const wine_unixlib_entry_t __wine_unix_call_funcs[SCARLETT_LOADER_UNIX_EX_OP_COUNT] = {
    unix_op_init,
    unix_op_mount,
    unix_op_unmount,
    unix_op_resolve_path,
    unix_op_report_stub,
    unix_op_set_time_sensitive,
    unix_op_check_net_policy,
    unix_op_gamesave_commit,
    unix_op_ex_load_config,
    unix_op_ex_get_metadata,
    unix_op_ex_vfs_open,
    unix_op_ex_vfs_read,
    unix_op_ex_vfs_write,
    unix_op_ex_vfs_close,
    unix_op_ex_vfs_delete,
    unix_op_ex_vfs_mkdir,
    unix_op_ex_vfs_stat,
    unix_op_ex_psm_register,
    unix_op_ex_psm_unregister,
    unix_op_ex_psm_snapshot,
    unix_op_ex_get_last_stub,
    unix_op_ex_query_xvd_info,
};

SCARLETT_STATIC_ASSERT(
    sizeof(__wine_unix_call_funcs) / sizeof(__wine_unix_call_funcs[0]) ==
        SCARLETT_LOADER_UNIX_EX_OP_COUNT,
    "__wine_unix_call_funcs must cover all 22 loader unixlib opcodes");

} /* extern "C" */
