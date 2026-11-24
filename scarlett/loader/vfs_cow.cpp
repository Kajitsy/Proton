/*
 * Proton-Scarlett — GAME: / D:\ CoW Overlay Virtual File System
 */

#include "vfs_cow.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>

namespace scarlett::loader {

namespace {

constexpr std::string_view kWhiteoutPrefix = ".wh.";
constexpr std::string_view kOpaqueMarker = ".wh..wh..opq";

bool iequals_str(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string to_lower_ascii(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool is_whiteout_name(std::string_view name) noexcept {
    return name.size() >= kWhiteoutPrefix.size() &&
           name.substr(0, kWhiteoutPrefix.size()) == kWhiteoutPrefix;
}

bool find_child_ci(
    const std::filesystem::path& dir,
    std::string_view name,
    std::string& out_actual_name,
    bool& out_is_dir,
    uint64_t& out_size) noexcept {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) {
        return false;
    }

    // Exact match fast path
    std::filesystem::path exact = dir / std::string(name);
    if (std::filesystem::exists(exact, ec)) {
        out_actual_name = std::string(name);
        out_is_dir = std::filesystem::is_directory(exact, ec);
        out_size = out_is_dir ? 0u : static_cast<uint64_t>(std::filesystem::file_size(exact, ec));
        return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string fname = entry.path().filename().string();
        if (iequals_str(fname, name)) {
            out_actual_name = std::move(fname);
            out_is_dir = entry.is_directory(ec);
            out_size = out_is_dir ? 0u : static_cast<uint64_t>(entry.file_size(ec));
            return true;
        }
    }
    return false;
}

bool find_whiteout_ci(
    const std::filesystem::path& overlay_dir,
    std::string_view name,
    std::filesystem::path& out_wh_path) noexcept {
    std::error_code ec;
    if (overlay_dir.empty() || !std::filesystem::is_directory(overlay_dir, ec)) {
        return false;
    }
    std::string target = std::string(kWhiteoutPrefix) + std::string(name);
    for (const auto& entry : std::filesystem::directory_iterator(overlay_dir, ec)) {
        std::string fname = entry.path().filename().string();
        if (iequals_str(fname, target)) {
            out_wh_path = entry.path();
            return true;
        }
    }
    return false;
}

void remove_matching_whiteouts(
    const std::filesystem::path& overlay_dir,
    std::string_view name) noexcept {
    std::error_code ec;
    if (overlay_dir.empty() || !std::filesystem::is_directory(overlay_dir, ec)) {
        return;
    }
    std::string target = std::string(kWhiteoutPrefix) + std::string(name);
    std::vector<std::filesystem::path> to_remove;
    for (const auto& entry : std::filesystem::directory_iterator(overlay_dir, ec)) {
        std::string fname = entry.path().filename().string();
        if (iequals_str(fname, target)) {
            to_remove.push_back(entry.path());
        }
    }
    for (const auto& p : to_remove) {
        std::filesystem::remove(p, ec);
    }
}

bool create_marker_file(const std::filesystem::path& marker_path) noexcept {
    int fd = ::open(marker_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    ::close(fd);
    return true;
}

bool copy_fd_contents(int src_fd, const std::filesystem::path& dst_path) noexcept {
    int dst_fd = ::open(dst_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        return false;
    }
    char buf[65536];
    for (;;) {
        ssize_t r = ::read(src_fd, buf, sizeof(buf));
        if (r < 0) {
            ::close(dst_fd);
            return false;
        }
        if (r == 0) {
            break;
        }
        ssize_t written = 0;
        while (written < r) {
            ssize_t w = ::write(dst_fd, buf + written, static_cast<size_t>(r - written));
            if (w <= 0) {
                ::close(dst_fd);
                return false;
            }
            written += w;
        }
    }
    ::close(dst_fd);
    return true;
}

bool is_path_same_or_inside(
    const std::filesystem::path& candidate,
    const std::filesystem::path& parent) noexcept {
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(candidate, ec);
    auto p = std::filesystem::weakly_canonical(parent, ec);
    if (ec) {
        return candidate == parent;
    }
    if (c == p) {
        return true;
    }
    auto pit = p.begin();
    auto cit = c.begin();
    while (pit != p.end() && cit != c.end()) {
        if (*pit != *cit) {
            return false;
        }
        ++pit;
        ++cit;
    }
    return pit == p.end();
}

} // namespace

GameCowVfs::~GameCowVfs() {
    unmount();
}

scarlett_hresult_t GameCowVfs::mount_directory(
    const std::filesystem::path& base_dir,
    const std::filesystem::path& overlay_dir) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    unmount();

    if (base_dir.empty() || overlay_dir.empty()) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(base_dir, ec)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    if (is_path_same_or_inside(overlay_dir, base_dir)) {
        return SCARLETT_ERR_VFS_READONLY_VIOLATION;
    }

    std::filesystem::create_directories(overlay_dir, ec);
    if (ec || !std::filesystem::is_directory(overlay_dir, ec)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    base_dir_ = std::filesystem::weakly_canonical(base_dir, ec);
    overlay_dir_ = std::filesystem::weakly_canonical(overlay_dir, ec);
    use_xvd_ = false;
    mounted_ = true;
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::mount_xvd_package(
    const std::filesystem::path& package_path,
    const std::filesystem::path& overlay_dir,
    uint32_t& out_volume_flags,
    bool& out_is_encrypted) noexcept {
    out_volume_flags = 0;
    out_is_encrypted = false;

    std::lock_guard<std::mutex> lock(mutex_);
    unmount();

    if (package_path.empty() || overlay_dir.empty()) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    auto reader = std::make_unique<XvdPackageReader>();
    scarlett_hresult_t hr = reader->open_file(package_path, out_is_encrypted);
    if (scarlett_failed(hr)) {
        return hr;
    }
    out_volume_flags = reader->header().volume_flags;
    if (!reader->has_ntfs()) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    std::error_code ec;
    std::filesystem::create_directories(overlay_dir, ec);
    if (ec || !std::filesystem::is_directory(overlay_dir, ec)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    xvd_reader_ = std::move(reader);
    overlay_dir_ = std::filesystem::weakly_canonical(overlay_dir, ec);
    base_dir_.clear();
    use_xvd_ = true;
    mounted_ = true;
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::mount_xvd_memory(
    std::vector<uint8_t> package_bytes,
    const std::filesystem::path& overlay_dir,
    uint32_t& out_volume_flags,
    bool& out_is_encrypted) noexcept {
    out_volume_flags = 0;
    out_is_encrypted = false;

    std::lock_guard<std::mutex> lock(mutex_);
    unmount();

    if (overlay_dir.empty()) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    auto reader = std::make_unique<XvdPackageReader>();
    scarlett_hresult_t hr = reader->open_memory(std::move(package_bytes), out_is_encrypted);
    if (scarlett_failed(hr)) {
        return hr;
    }
    out_volume_flags = reader->header().volume_flags;
    if (!reader->has_ntfs()) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    std::error_code ec;
    std::filesystem::create_directories(overlay_dir, ec);
    if (ec || !std::filesystem::is_directory(overlay_dir, ec)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    xvd_reader_ = std::move(reader);
    overlay_dir_ = std::filesystem::weakly_canonical(overlay_dir, ec);
    base_dir_.clear();
    use_xvd_ = true;
    mounted_ = true;
    return SCARLETT_OK;
}

void GameCowVfs::unmount() noexcept {
    for (auto& [id, st] : open_files_) {
        if (st.fd >= 0) {
            ::close(st.fd);
        }
    }
    open_files_.clear();
    xvd_reader_.reset();
    base_dir_.clear();
    overlay_dir_.clear();
    use_xvd_ = false;
    mounted_ = false;
}

bool GameCowVfs::is_mounted() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return mounted_;
}

scarlett_hresult_t GameCowVfs::normalize_game_path(
    std::string_view input_path,
    std::vector<std::string>& out_components) noexcept {
    out_components.clear();
    if (input_path.empty()) {
        return SCARLETT_E_INVALIDARG;
    }
    for (char ch : input_path) {
        if (ch == '\0') {
            return SCARLETT_E_INVALIDARG;
        }
    }

    std::string_view rest = input_path;
    if (rest.size() >= 4 &&
        (rest.substr(0, 4) == "\\\\?\\" || rest.substr(0, 4) == "\\\\.\\")) {
        rest.remove_prefix(4);
    }
    if (rest.size() >= 5 && iequals_str(rest.substr(0, 5), "GAME:")) {
        rest.remove_prefix(5);
    } else if (rest.size() >= 2 &&
               std::isalpha(static_cast<unsigned char>(rest[0])) &&
               rest[1] == ':') {
        if (rest[0] != 'D' && rest[0] != 'd' &&
            rest[0] != 'G' && rest[0] != 'g') {
            return SCARLETT_E_INVALIDARG;
        }
        rest.remove_prefix(2);
    } else if (rest.find(':') != std::string_view::npos) {
        return SCARLETT_E_INVALIDARG;
    }

    size_t pos = 0;
    while (pos < rest.size()) {
        while (pos < rest.size() && (rest[pos] == '\\' || rest[pos] == '/')) {
            ++pos;
        }
        if (pos >= rest.size()) {
            break;
        }
        size_t next = pos;
        while (next < rest.size() && rest[next] != '\\' && rest[next] != '/') {
            ++next;
        }
        std::string_view comp = rest.substr(pos, next - pos);
        pos = next;
        if (comp == ".") {
            continue;
        }
        if (comp == "..") {
            if (out_components.empty()) {
                return SCARLETT_E_INVALIDARG;
            }
            out_components.pop_back();
            continue;
        }
        if (is_whiteout_name(comp)) {
            return SCARLETT_E_INVALIDARG;
        }
        out_components.emplace_back(comp);
    }

    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::resolve_internal(
    const std::vector<std::string>& components,
    ResolvedTarget& out_target) const noexcept {
    out_target = ResolvedTarget{};
    if (!mounted_) {
        return SCARLETT_E_FAIL;
    }

    if (components.empty()) {
        out_target.exists = true;
        out_target.is_directory = true;
        out_target.layer = use_xvd_ ? SCARLETT_VFS_LAYER_XVD_NTFS : SCARLETT_VFS_LAYER_BASE_DIR;
        out_target.overlay_path = overlay_dir_;
        out_target.base_path = base_dir_;
        std::error_code ec;
        if (std::filesystem::exists(overlay_dir_ / std::string(kOpaqueMarker), ec)) {
            out_target.base_opaque = true;
        }
        return SCARLETT_OK;
    }

    std::filesystem::path ov_cur = overlay_dir_;
    bool ov_alive = true;
    std::filesystem::path base_cur = base_dir_;
    bool base_alive = true;
    bool opaque_stop = false;
    std::string xvd_rel;

    std::error_code ec;
    if (std::filesystem::exists(ov_cur / std::string(kOpaqueMarker), ec)) {
        opaque_stop = true;
        base_alive = false;
    }

    for (size_t i = 0; i < components.size(); ++i) {
        const std::string& comp = components[i];
        bool is_last = (i + 1 == components.size());

        // Check whiteout in overlay
        if (ov_alive) {
            std::filesystem::path wh;
            if (find_whiteout_ci(ov_cur, comp, wh)) {
                out_target.exists = false;
                out_target.whiteout = true;
                out_target.canonical_components.push_back(comp);
                for (size_t j = 0; j < out_target.canonical_components.size(); ++j) {
                    if (j > 0) {
                        out_target.canonical_rel_path.push_back('/');
                    }
                    out_target.canonical_rel_path.append(out_target.canonical_components[j]);
                }
                return SCARLETT_OK;
            }
        }

        std::string ov_actual;
        bool ov_is_dir = false;
        uint64_t ov_size = 0;
        bool ov_found = ov_alive && find_child_ci(ov_cur, comp, ov_actual, ov_is_dir, ov_size);

        std::string base_actual;
        bool base_is_dir = false;
        uint64_t base_size = 0;
        bool base_found = false;

        if (!opaque_stop && base_alive) {
            if (!use_xvd_) {
                base_found = find_child_ci(base_cur, comp, base_actual, base_is_dir, base_size);
            } else {
                std::string candidate_rel = xvd_rel.empty() ? comp : (xvd_rel + "/" + comp);
                std::vector<NtfsDirEntry> entries;
                if (scarlett_succeeded(xvd_reader_->ntfs().list_directory(xvd_rel, entries))) {
                    for (const auto& e : entries) {
                        if (iequals_str(e.name, comp)) {
                            base_actual = e.name;
                            base_is_dir = e.is_directory;
                            base_size = e.size_bytes;
                            base_found = true;
                            break;
                        }
                    }
                }
            }
        }

        std::string chosen_name = ov_found ? ov_actual : (base_found ? base_actual : comp);
        out_target.canonical_components.push_back(chosen_name);
        if (!out_target.canonical_rel_path.empty()) {
            out_target.canonical_rel_path.push_back('/');
        }
        out_target.canonical_rel_path.append(chosen_name);

        if (is_last) {
            if (ov_found) {
                out_target.exists = true;
                out_target.is_directory = ov_is_dir;
                out_target.size_bytes = ov_size;
                out_target.layer = SCARLETT_VFS_LAYER_OVERLAY;
                out_target.overlay_path = ov_cur / ov_actual;
                if (base_found && !use_xvd_) {
                    out_target.base_path = base_cur / base_actual;
                }
            } else if (base_found) {
                out_target.exists = true;
                out_target.is_directory = base_is_dir;
                out_target.size_bytes = base_size;
                out_target.layer =
                    use_xvd_ ? SCARLETT_VFS_LAYER_XVD_NTFS : SCARLETT_VFS_LAYER_BASE_DIR;
                out_target.overlay_path = ov_cur / chosen_name;
                if (!use_xvd_) {
                    out_target.base_path = base_cur / base_actual;
                }
            } else {
                out_target.exists = false;
                out_target.overlay_path = ov_cur / chosen_name;
            }
            return SCARLETT_OK;
        }

        // Intermediate directory traversal
        if (ov_found && !ov_is_dir) {
            // Overlay file shadows base directory
            out_target.exists = false;
            return SCARLETT_OK;
        }
        if (!ov_found && !base_found) {
            out_target.exists = false;
            return SCARLETT_OK;
        }
        if (!ov_found && base_found && !base_is_dir) {
            out_target.exists = false;
            return SCARLETT_OK;
        }

        if (ov_found) {
            ov_cur /= ov_actual;
            if (std::filesystem::exists(ov_cur / std::string(kOpaqueMarker), ec)) {
                opaque_stop = true;
                base_alive = false;
            }
        } else {
            ov_alive = false;
            ov_cur /= chosen_name;
        }

        if (base_found && base_is_dir && !opaque_stop) {
            if (!use_xvd_) {
                base_cur /= base_actual;
            } else {
                xvd_rel = xvd_rel.empty() ? base_actual : (xvd_rel + "/" + base_actual);
            }
        } else {
            base_alive = false;
        }
    }

    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::ensure_overlay_parent_dirs(
    const std::vector<std::string>& components,
    std::filesystem::path& out_parent_overlay_dir,
    std::vector<std::string>& out_canonical_parent) const noexcept {
    out_canonical_parent.clear();
    std::filesystem::path ov_cur = overlay_dir_;
    std::filesystem::path base_cur = base_dir_;
    bool base_alive = true;
    bool opaque_stop = false;
    std::string xvd_rel;
    std::error_code ec;

    if (std::filesystem::exists(ov_cur / std::string(kOpaqueMarker), ec)) {
        opaque_stop = true;
        base_alive = false;
    }

    for (size_t i = 0; i + 1 < components.size(); ++i) {
        const std::string& comp = components[i];
        std::filesystem::path wh;
        if (find_whiteout_ci(ov_cur, comp, wh)) {
            return SCARLETT_E_FAIL;
        }

        std::string ov_actual;
        bool ov_is_dir = false;
        uint64_t ov_size = 0;
        bool ov_found = find_child_ci(ov_cur, comp, ov_actual, ov_is_dir, ov_size);
        if (ov_found && !ov_is_dir) {
            return SCARLETT_E_INVALIDARG;
        }

        std::string base_actual;
        bool base_is_dir = false;
        uint64_t base_size = 0;
        bool base_found = false;
        if (!opaque_stop && base_alive) {
            if (!use_xvd_) {
                base_found = find_child_ci(base_cur, comp, base_actual, base_is_dir, base_size);
            } else {
                std::vector<NtfsDirEntry> entries;
                if (scarlett_succeeded(xvd_reader_->ntfs().list_directory(xvd_rel, entries))) {
                    for (const auto& e : entries) {
                        if (iequals_str(e.name, comp)) {
                            base_actual = e.name;
                            base_is_dir = e.is_directory;
                            base_size = e.size_bytes;
                            base_found = true;
                            break;
                        }
                    }
                }
            }
        }

        if (!ov_found && (!base_found || !base_is_dir)) {
            return SCARLETT_E_FAIL;
        }

        std::string chosen = ov_found ? ov_actual : base_actual;
        out_canonical_parent.push_back(chosen);
        ov_cur /= chosen;
        if (!ov_found) {
            std::filesystem::create_directories(ov_cur, ec);
            if (ec) {
                return SCARLETT_ERR_VFS_OVERLAY_FAILED;
            }
        } else if (std::filesystem::exists(ov_cur / std::string(kOpaqueMarker), ec)) {
            opaque_stop = true;
            base_alive = false;
        }

        if (!opaque_stop && base_found && base_is_dir) {
            if (!use_xvd_) {
                base_cur /= base_actual;
            } else {
                xvd_rel = xvd_rel.empty() ? base_actual : (xvd_rel + "/" + base_actual);
            }
        } else {
            base_alive = false;
        }
    }

    out_parent_overlay_dir = ov_cur;
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::cow_materialize_file(
    const ResolvedTarget& target,
    bool truncate_only,
    std::filesystem::path& out_overlay_file) const noexcept {
    std::filesystem::path parent_ov;
    std::vector<std::string> canonical_parent;
    scarlett_hresult_t hr = ensure_overlay_parent_dirs(
        target.canonical_components, parent_ov, canonical_parent);
    if (scarlett_failed(hr)) {
        return hr;
    }

    const std::string& leaf = target.canonical_components.back();
    remove_matching_whiteouts(parent_ov, leaf);

    out_overlay_file = parent_ov / leaf;
    std::error_code ec;
    if (std::filesystem::exists(out_overlay_file, ec)) {
        if (std::filesystem::is_directory(out_overlay_file, ec)) {
            return SCARLETT_E_INVALIDARG;
        }
        return SCARLETT_OK;
    }

    if (truncate_only || !target.exists) {
        if (!create_marker_file(out_overlay_file)) {
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
        return SCARLETT_OK;
    }

    // Copy-on-write from read-only base layer into overlay
    if (!use_xvd_) {
        int src_fd = ::open(target.base_path.c_str(), O_RDONLY);
        if (src_fd < 0) {
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
        bool ok = copy_fd_contents(src_fd, out_overlay_file);
        ::close(src_fd);
        return ok ? SCARLETT_OK : SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    std::vector<uint8_t> data;
    hr = xvd_reader_->ntfs().read_entire_file(target.canonical_rel_path, data);
    if (scarlett_failed(hr)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }
    int dst_fd = ::open(out_overlay_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }
    size_t written = 0;
    while (written < data.size()) {
        ssize_t w = ::write(dst_fd, data.data() + written, data.size() - written);
        if (w <= 0) {
            ::close(dst_fd);
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
        written += static_cast<size_t>(w);
    }
    ::close(dst_fd);
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::resolve_path(
    std::string_view game_path,
    uint32_t access_flags,
    std::string& out_host_path,
    scarlett_vfs_layer_t& out_layer) noexcept {
    out_host_path.clear();
    out_layer = SCARLETT_VFS_LAYER_NONE;

    std::vector<std::string> comps;
    scarlett_hresult_t hr = normalize_game_path(game_path, comps);
    if (scarlett_failed(hr)) {
        return hr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ResolvedTarget target;
    hr = resolve_internal(comps, target);
    if (scarlett_failed(hr)) {
        return hr;
    }

    bool wants_write = (access_flags &
                        (SCARLETT_VFS_ACCESS_WRITE | SCARLETT_VFS_ACCESS_CREATE |
                         SCARLETT_VFS_ACCESS_TRUNCATE | SCARLETT_VFS_ACCESS_APPEND)) != 0u;
    if (!wants_write) {
        if (!target.exists) {
            return SCARLETT_E_FAIL;
        }
        out_layer = target.layer;
        if (target.layer == SCARLETT_VFS_LAYER_OVERLAY) {
            out_host_path = target.overlay_path.string();
        } else if (target.layer == SCARLETT_VFS_LAYER_BASE_DIR) {
            out_host_path = target.base_path.string();
        } else {
            out_host_path = "xvd://" + target.canonical_rel_path;
        }
        return SCARLETT_OK;
    }

    if (comps.empty() || target.is_directory) {
        return SCARLETT_E_INVALIDARG;
    }
    if (!target.exists && (access_flags & SCARLETT_VFS_ACCESS_CREATE) == 0u) {
        return SCARLETT_E_FAIL;
    }

    std::filesystem::path ov_file;
    bool trunc = (access_flags & SCARLETT_VFS_ACCESS_TRUNCATE) != 0u;
    hr = cow_materialize_file(target, trunc, ov_file);
    if (scarlett_failed(hr)) {
        return hr;
    }
    out_host_path = ov_file.string();
    out_layer = SCARLETT_VFS_LAYER_OVERLAY;
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::stat_path(
    std::string_view game_path,
    VfsStatResult& out_stat) noexcept {
    out_stat = VfsStatResult{};
    std::vector<std::string> comps;
    scarlett_hresult_t hr = normalize_game_path(game_path, comps);
    if (scarlett_failed(hr)) {
        return hr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ResolvedTarget target;
    hr = resolve_internal(comps, target);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (!target.exists) {
        return SCARLETT_E_FAIL;
    }
    out_stat.exists = true;
    out_stat.is_directory = target.is_directory;
    out_stat.size_bytes = target.size_bytes;
    out_stat.layer = target.layer;
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::open_file(
    std::string_view game_path,
    uint32_t access_flags,
    uint64_t& out_file_handle,
    uint64_t& out_file_size,
    scarlett_vfs_layer_t& out_layer) noexcept {
    out_file_handle = 0;
    out_file_size = 0;
    out_layer = SCARLETT_VFS_LAYER_NONE;

    std::vector<std::string> comps;
    scarlett_hresult_t hr = normalize_game_path(game_path, comps);
    if (scarlett_failed(hr)) {
        return hr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ResolvedTarget target;
    hr = resolve_internal(comps, target);
    if (scarlett_failed(hr)) {
        return hr;
    }

    bool wants_write = (access_flags &
                        (SCARLETT_VFS_ACCESS_WRITE | SCARLETT_VFS_ACCESS_CREATE |
                         SCARLETT_VFS_ACCESS_TRUNCATE | SCARLETT_VFS_ACCESS_APPEND)) != 0u;
    if (comps.empty() || target.is_directory) {
        return SCARLETT_E_INVALIDARG;
    }

    OpenFileState st;
    if (!wants_write) {
        if (!target.exists) {
            return SCARLETT_E_FAIL;
        }
        st.writable = false;
        st.layer = target.layer;
        st.size_bytes = target.size_bytes;
        if (target.layer == SCARLETT_VFS_LAYER_XVD_NTFS) {
            st.is_xvd_ntfs = true;
            st.xvd_rel_path = target.canonical_rel_path;
        } else {
            const auto& p = (target.layer == SCARLETT_VFS_LAYER_OVERLAY)
                                ? target.overlay_path
                                : target.base_path;
            st.fd = ::open(p.c_str(), O_RDONLY);
            if (st.fd < 0) {
                return SCARLETT_E_FAIL;
            }
        }
    } else {
        if (!target.exists && (access_flags & SCARLETT_VFS_ACCESS_CREATE) == 0u) {
            return SCARLETT_E_FAIL;
        }
        std::filesystem::path ov_file;
        bool trunc = (access_flags & SCARLETT_VFS_ACCESS_TRUNCATE) != 0u;
        hr = cow_materialize_file(target, trunc, ov_file);
        if (scarlett_failed(hr)) {
            return hr;
        }
        int oflags = O_RDWR;
        if (trunc) {
            oflags |= O_TRUNC;
        }
        st.fd = ::open(ov_file.c_str(), oflags);
        if (st.fd < 0) {
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
        struct stat sb{};
        if (::fstat(st.fd, &sb) == 0) {
            st.size_bytes = static_cast<uint64_t>(sb.st_size);
        }
        st.writable = true;
        st.append = (access_flags & SCARLETT_VFS_ACCESS_APPEND) != 0u;
        st.layer = SCARLETT_VFS_LAYER_OVERLAY;
    }

    uint64_t handle = next_file_handle_++;
    out_file_handle = handle;
    out_file_size = st.size_bytes;
    out_layer = st.layer;
    open_files_.emplace(handle, std::move(st));
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::read_file(
    uint64_t file_handle,
    uint64_t offset,
    void* dst,
    uint32_t byte_count,
    uint32_t& out_bytes_read) noexcept {
    out_bytes_read = 0;
    if (!dst && byte_count > 0u) {
        return SCARLETT_E_POINTER;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(file_handle);
    if (it == open_files_.end()) {
        return SCARLETT_E_HANDLE;
    }
    const OpenFileState& st = it->second;
    if (st.is_xvd_ntfs) {
        size_t got = 0;
        scarlett_hresult_t hr = xvd_reader_->ntfs().read_file(
            st.xvd_rel_path, offset, dst, byte_count, got);
        if (scarlett_succeeded(hr)) {
            out_bytes_read = static_cast<uint32_t>(got);
        }
        return hr;
    }

    if (st.fd < 0) {
        return SCARLETT_E_HANDLE;
    }
    ssize_t r = ::pread(st.fd, dst, byte_count, static_cast<off_t>(offset));
    if (r < 0) {
        return SCARLETT_E_FAIL;
    }
    out_bytes_read = static_cast<uint32_t>(r);
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::write_file(
    uint64_t file_handle,
    uint64_t offset,
    const void* src,
    uint32_t byte_count,
    uint32_t& out_bytes_written) noexcept {
    out_bytes_written = 0;
    if (!src && byte_count > 0u) {
        return SCARLETT_E_POINTER;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(file_handle);
    if (it == open_files_.end()) {
        return SCARLETT_E_HANDLE;
    }
    OpenFileState& st = it->second;
    if (!st.writable || st.layer != SCARLETT_VFS_LAYER_OVERLAY || st.fd < 0) {
        return SCARLETT_ERR_VFS_READONLY_VIOLATION;
    }

    uint64_t write_off = offset;
    if (st.append) {
        struct stat sb{};
        if (::fstat(st.fd, &sb) == 0) {
            write_off = static_cast<uint64_t>(sb.st_size);
        }
    }

    const uint8_t* ptr = static_cast<const uint8_t*>(src);
    uint32_t total = 0;
    while (total < byte_count) {
        ssize_t w = ::pwrite(st.fd, ptr + total, byte_count - total,
                             static_cast<off_t>(write_off + total));
        if (w <= 0) {
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
        total += static_cast<uint32_t>(w);
    }
    if (write_off + total > st.size_bytes) {
        st.size_bytes = write_off + total;
    }
    out_bytes_written = total;
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::close_file(uint64_t file_handle) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(file_handle);
    if (it == open_files_.end()) {
        return SCARLETT_E_HANDLE;
    }
    if (it->second.fd >= 0) {
        ::close(it->second.fd);
    }
    open_files_.erase(it);
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::create_directory(std::string_view game_path) noexcept {
    std::vector<std::string> comps;
    scarlett_hresult_t hr = normalize_game_path(game_path, comps);
    if (scarlett_failed(hr) || comps.empty()) {
        return SCARLETT_E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ResolvedTarget target;
    hr = resolve_internal(comps, target);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (target.exists) {
        return target.is_directory ? SCARLETT_OK : SCARLETT_E_INVALIDARG;
    }

    std::filesystem::path parent_ov;
    std::vector<std::string> canonical_parent;
    hr = ensure_overlay_parent_dirs(comps, parent_ov, canonical_parent);
    if (scarlett_failed(hr)) {
        return hr;
    }

    const std::string& leaf = comps.back();
    std::filesystem::path wh;
    bool had_whiteout = find_whiteout_ci(parent_ov, leaf, wh);
    remove_matching_whiteouts(parent_ov, leaf);

    std::filesystem::path new_dir = parent_ov / leaf;
    std::error_code ec;
    std::filesystem::create_directory(new_dir, ec);
    if (ec || !std::filesystem::is_directory(new_dir, ec)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }

    if (had_whiteout) {
        if (!create_marker_file(new_dir / std::string(kOpaqueMarker))) {
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
    }
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::remove_path(std::string_view game_path) noexcept {
    std::vector<std::string> comps;
    scarlett_hresult_t hr = normalize_game_path(game_path, comps);
    if (scarlett_failed(hr) || comps.empty()) {
        return SCARLETT_E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ResolvedTarget target;
    hr = resolve_internal(comps, target);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (!target.exists) {
        return SCARLETT_E_FAIL;
    }

    if (target.is_directory) {
        // Ensure logical directory is empty before removing
        // Unlock not needed if we compute inline, so check via helper:
        std::filesystem::path ov_dir = target.overlay_path;
        std::error_code ec;
        if (std::filesystem::is_directory(ov_dir, ec)) {
            for (const auto& e : std::filesystem::directory_iterator(ov_dir, ec)) {
                std::string fn = e.path().filename().string();
                if (!is_whiteout_name(fn)) {
                    return SCARLETT_E_INVALIDARG;
                }
            }
        }
    }

    std::filesystem::path parent_ov;
    std::vector<std::string> canonical_parent;
    hr = ensure_overlay_parent_dirs(target.canonical_components, parent_ov, canonical_parent);
    if (scarlett_failed(hr)) {
        return hr;
    }

    const std::string& leaf = target.canonical_components.back();
    std::error_code ec;
    if (std::filesystem::exists(parent_ov / leaf, ec)) {
        std::filesystem::remove_all(parent_ov / leaf, ec);
        if (ec) {
            return SCARLETT_ERR_VFS_OVERLAY_FAILED;
        }
    }

    // Create whiteout in overlay so base entry stays hidden
    std::filesystem::path wh_path = parent_ov / (std::string(kWhiteoutPrefix) + leaf);
    if (!create_marker_file(wh_path)) {
        return SCARLETT_ERR_VFS_OVERLAY_FAILED;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t GameCowVfs::list_directory(
    std::string_view game_path,
    std::vector<VfsDirEntry>& out_entries) noexcept {
    out_entries.clear();
    std::vector<std::string> comps;
    scarlett_hresult_t hr = normalize_game_path(game_path, comps);
    if (scarlett_failed(hr)) {
        return hr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ResolvedTarget target;
    hr = resolve_internal(comps, target);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (!target.exists || !target.is_directory) {
        return SCARLETT_E_FAIL;
    }

    // Walk overlay + base path levels to find opaque boundary
    std::filesystem::path ov_dir = overlay_dir_;
    bool ov_exists = true;
    bool opaque = false;
    std::filesystem::path base_dir = base_dir_;
    bool base_exists = true;
    std::string xvd_rel;
    std::error_code ec;

    if (std::filesystem::exists(ov_dir / std::string(kOpaqueMarker), ec)) {
        opaque = true;
        base_exists = false;
    }

    for (const auto& c : target.canonical_components) {
        if (ov_exists) {
            std::string act;
            bool isd = false;
            uint64_t sz = 0;
            if (find_child_ci(ov_dir, c, act, isd, sz) && isd) {
                ov_dir /= act;
                if (std::filesystem::exists(ov_dir / std::string(kOpaqueMarker), ec)) {
                    opaque = true;
                    base_exists = false;
                }
            } else {
                ov_exists = false;
            }
        }
        if (!opaque && base_exists) {
            if (!use_xvd_) {
                std::string act;
                bool isd = false;
                uint64_t sz = 0;
                if (find_child_ci(base_dir, c, act, isd, sz) && isd) {
                    base_dir /= act;
                } else {
                    base_exists = false;
                }
            } else {
                xvd_rel = xvd_rel.empty() ? c : (xvd_rel + "/" + c);
            }
        }
    }

    std::unordered_set<std::string> hidden_lower;
    std::unordered_map<std::string, VfsDirEntry> merged;

    if (ov_exists && std::filesystem::is_directory(ov_dir, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(ov_dir, ec)) {
            std::string fname = e.path().filename().string();
            if (is_whiteout_name(fname)) {
                if (fname != kOpaqueMarker) {
                    hidden_lower.insert(to_lower_ascii(fname.substr(kWhiteoutPrefix.size())));
                }
                continue;
            }
            VfsDirEntry ve;
            ve.name = fname;
            ve.is_directory = e.is_directory(ec);
            ve.size_bytes = ve.is_directory ? 0u : static_cast<uint64_t>(e.file_size(ec));
            ve.layer = SCARLETT_VFS_LAYER_OVERLAY;
            merged[to_lower_ascii(fname)] = std::move(ve);
        }
    }

    if (!opaque && base_exists) {
        if (!use_xvd_) {
            if (std::filesystem::is_directory(base_dir, ec)) {
                for (const auto& e : std::filesystem::directory_iterator(base_dir, ec)) {
                    std::string fname = e.path().filename().string();
                    std::string low = to_lower_ascii(fname);
                    if (hidden_lower.count(low) || merged.count(low)) {
                        continue;
                    }
                    VfsDirEntry ve;
                    ve.name = std::move(fname);
                    ve.is_directory = e.is_directory(ec);
                    ve.size_bytes = ve.is_directory ? 0u : static_cast<uint64_t>(e.file_size(ec));
                    ve.layer = SCARLETT_VFS_LAYER_BASE_DIR;
                    merged[low] = std::move(ve);
                }
            }
        } else {
            std::vector<NtfsDirEntry> ntfs_entries;
            if (scarlett_succeeded(xvd_reader_->ntfs().list_directory(xvd_rel, ntfs_entries))) {
                for (const auto& ne : ntfs_entries) {
                    std::string low = to_lower_ascii(ne.name);
                    if (hidden_lower.count(low) || merged.count(low)) {
                        continue;
                    }
                    VfsDirEntry ve;
                    ve.name = ne.name;
                    ve.is_directory = ne.is_directory;
                    ve.size_bytes = ne.size_bytes;
                    ve.layer = SCARLETT_VFS_LAYER_XVD_NTFS;
                    merged[low] = std::move(ve);
                }
            }
        }
    }

    out_entries.reserve(merged.size());
    for (auto& [k, v] : merged) {
        out_entries.push_back(std::move(v));
    }
    std::sort(out_entries.begin(), out_entries.end(),
              [](const VfsDirEntry& a, const VfsDirEntry& b) {
                  return a.name < b.name;
              });
    return SCARLETT_OK;
}

} // namespace scarlett::loader
