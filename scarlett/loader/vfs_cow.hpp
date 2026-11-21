/*
 * Proton-Scarlett — GAME: / D:\ CoW Overlay Virtual File System
 */

#ifndef SCARLETT_LOADER_VFS_COW_HPP
#define SCARLETT_LOADER_VFS_COW_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "loader_unix_abi.h"
#include "scarlett_errors.h"
#include "xvd_ntfs.hpp"

namespace scarlett::loader {

struct VfsDirEntry {
    std::string name;
    bool is_directory = false;
    uint64_t size_bytes = 0;
    scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
};

struct VfsStatResult {
    bool exists = false;
    bool is_directory = false;
    uint64_t size_bytes = 0;
    scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
};

class GameCowVfs {
public:
    GameCowVfs() = default;
    ~GameCowVfs();

    GameCowVfs(const GameCowVfs&) = delete;
    GameCowVfs& operator=(const GameCowVfs&) = delete;

    scarlett_hresult_t mount_directory(
        const std::filesystem::path& base_dir,
        const std::filesystem::path& overlay_dir) noexcept;

    scarlett_hresult_t mount_xvd_package(
        const std::filesystem::path& package_path,
        const std::filesystem::path& overlay_dir,
        uint32_t& out_volume_flags,
        bool& out_is_encrypted) noexcept;

    scarlett_hresult_t mount_xvd_memory(
        std::vector<uint8_t> package_bytes,
        const std::filesystem::path& overlay_dir,
        uint32_t& out_volume_flags,
        bool& out_is_encrypted) noexcept;

    void unmount() noexcept;

    bool is_mounted() const noexcept;

    static scarlett_hresult_t normalize_game_path(
        std::string_view input_path,
        std::vector<std::string>& out_components) noexcept;

    scarlett_hresult_t resolve_path(
        std::string_view game_path,
        uint32_t access_flags,
        std::string& out_host_path,
        scarlett_vfs_layer_t& out_layer) noexcept;

    scarlett_hresult_t stat_path(
        std::string_view game_path,
        VfsStatResult& out_stat) noexcept;

    scarlett_hresult_t open_file(
        std::string_view game_path,
        uint32_t access_flags,
        uint64_t& out_file_handle,
        uint64_t& out_file_size,
        scarlett_vfs_layer_t& out_layer) noexcept;

    scarlett_hresult_t read_file(
        uint64_t file_handle,
        uint64_t offset,
        void* dst,
        uint32_t byte_count,
        uint32_t& out_bytes_read) noexcept;

    scarlett_hresult_t write_file(
        uint64_t file_handle,
        uint64_t offset,
        const void* src,
        uint32_t byte_count,
        uint32_t& out_bytes_written) noexcept;

    scarlett_hresult_t close_file(uint64_t file_handle) noexcept;

    scarlett_hresult_t create_directory(std::string_view game_path) noexcept;

    scarlett_hresult_t remove_path(std::string_view game_path) noexcept;

    scarlett_hresult_t list_directory(
        std::string_view game_path,
        std::vector<VfsDirEntry>& out_entries) noexcept;

    scarlett_hresult_t write_direct_to_base_forbidden() noexcept {
        return SCARLETT_ERR_VFS_READONLY_VIOLATION;
    }

    const XvdPackageReader* xvd_reader() const noexcept { return xvd_reader_.get(); }

private:
    struct OpenFileState {
        int fd = -1;
        bool writable = false;
        bool append = false;
        bool is_xvd_ntfs = false;
        std::string xvd_rel_path;
        uint64_t size_bytes = 0;
        scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
    };

    struct ResolvedTarget {
        bool exists = false;
        bool is_directory = false;
        bool whiteout = false;
        bool base_opaque = false;
        uint64_t size_bytes = 0;
        scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
        std::filesystem::path overlay_path;
        std::filesystem::path base_path;
        std::string canonical_rel_path;
        std::vector<std::string> canonical_components;
    };

    scarlett_hresult_t resolve_internal(
        const std::vector<std::string>& components,
        ResolvedTarget& out_target) const noexcept;

    scarlett_hresult_t ensure_overlay_parent_dirs(
        const std::vector<std::string>& components,
        std::filesystem::path& out_parent_overlay_dir,
        std::vector<std::string>& out_canonical_parent) const noexcept;

    scarlett_hresult_t cow_materialize_file(
        const ResolvedTarget& target,
        bool truncate_only,
        std::filesystem::path& out_overlay_file) const noexcept;

    mutable std::mutex mutex_;
    bool mounted_ = false;
    bool use_xvd_ = false;
    std::filesystem::path base_dir_;
    std::filesystem::path overlay_dir_;
    std::unique_ptr<XvdPackageReader> xvd_reader_;
    uint64_t next_file_handle_ = 1;
    std::unordered_map<uint64_t, OpenFileState> open_files_;
};

} // namespace scarlett::loader

#endif /* SCARLETT_LOADER_VFS_COW_HPP */
