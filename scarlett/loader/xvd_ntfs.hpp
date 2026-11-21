/*
 * Proton-Scarlett — Clean-Room XVD/XVC Parser & Read-Only NTFS Reader
 */

#ifndef SCARLETT_LOADER_XVD_NTFS_HPP
#define SCARLETT_LOADER_XVD_NTFS_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "scarlett_errors.h"
#include "xvd_xvc_format.h"

namespace scarlett::loader {

struct XvdHeaderInfo {
    uint32_t volume_flags = 0;
    uint32_t format_version = 0;
    uint64_t file_time = 0;
    uint64_t drive_size = 0;
    uint8_t  vd_uid[16]{};
    uint8_t  content_id[16]{};
    uint32_t sequence_number = 0;
    uint32_t xvd_type = 0;
    uint32_t content_type = 0;
    uint32_t embedded_xvd_length = 0;
    uint32_t user_data_length = 0;
    uint32_t xvc_data_length = 0;
    uint32_t dyn_header_length = 0;
    uint32_t block_size = 0;
    std::string sandbox_id;
};

struct XvcRegionInfo {
    uint32_t region_id = 0;
    uint32_t flags = 0;
    uint32_t first_chunk_index = 0;
    uint32_t chunk_count = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t hash = 0;
    std::string specifier;
};

struct XvcChunkInfo {
    uint32_t chunk_id = 0;
    uint32_t region_id = 0;
    uint64_t offset = 0;
    uint64_t length = 0;
    uint32_t flags = 0;
};

struct XvcUpdateSegmentInfo {
    uint32_t segment_id = 0;
    uint32_t flags = 0;
    uint64_t target_offset = 0;
    uint64_t length = 0;
    uint64_t base_version = 0;
    uint8_t  segment_hash[32]{};
};

struct XvcDescriptorInfo {
    bool present = false;
    uint32_t version = 0;
    uint8_t  content_id[16]{};
    uint32_t key_count = 0;
    uint32_t encryption_flags = 0;
    uint32_t chunk_size_bytes = 0;
    std::vector<XvcRegionInfo> regions;
    std::vector<XvcChunkInfo> chunks;
    std::vector<XvcUpdateSegmentInfo> update_segments;
};

struct NtfsDataRun {
    uint64_t vcn_start = 0;
    uint64_t cluster_count = 0;
    int64_t  lcn_start = 0;
    bool     is_sparse = false;
};

struct NtfsFileStat {
    uint64_t mft_record = 0;
    uint64_t size_bytes = 0;
    bool     is_directory = false;
    bool     exists = false;
};

struct NtfsDirEntry {
    std::string name;
    uint64_t mft_record = 0;
    uint64_t size_bytes = 0;
    bool     is_directory = false;
};

class XvdByteSource {
public:
    virtual ~XvdByteSource() = default;
    virtual uint64_t size() const noexcept = 0;
    virtual bool read_at(uint64_t offset, void* dst, size_t size) const noexcept = 0;
};

std::unique_ptr<XvdByteSource> make_memory_byte_source(std::vector<uint8_t> data);
std::unique_ptr<XvdByteSource> make_file_byte_source(const std::filesystem::path& path);

class NtfsReadOnlyReader {
public:
    scarlett_hresult_t mount(const XvdByteSource* source, uint64_t volume_offset) noexcept;

    bool is_mounted() const noexcept { return mounted_; }

    scarlett_hresult_t stat_path(std::string_view rel_path, NtfsFileStat& out_stat) const noexcept;

    scarlett_hresult_t list_directory(
        std::string_view rel_path,
        std::vector<NtfsDirEntry>& out_entries) const noexcept;

    scarlett_hresult_t read_file(
        std::string_view rel_path,
        uint64_t offset,
        void* dst,
        size_t bytes_to_read,
        size_t& out_bytes_read) const noexcept;

    scarlett_hresult_t read_entire_file(
        std::string_view rel_path,
        std::vector<uint8_t>& out_data) const noexcept;

private:
    struct StreamInfo {
        bool valid = false;
        bool non_resident = false;
        uint64_t data_size = 0;
        std::vector<uint8_t> resident_data;
        std::vector<NtfsDataRun> runs;
    };

    scarlett_hresult_t read_mft_record(uint64_t record_index, std::vector<uint8_t>& out_rec) const noexcept;
    scarlett_hresult_t read_stream(
        const StreamInfo& stream,
        uint64_t offset,
        void* dst,
        size_t size,
        size_t& out_read) const noexcept;

    scarlett_hresult_t read_directory_entries_from_record(
        uint64_t dir_mft_record,
        std::vector<NtfsDirEntry>& out_entries) const noexcept;

    scarlett_hresult_t resolve_path_record(
        std::string_view rel_path,
        uint64_t& out_mft_record,
        bool& out_is_dir,
        uint64_t& out_size) const noexcept;

    const XvdByteSource* source_ = nullptr;
    uint64_t volume_offset_ = 0;
    uint32_t bytes_per_sector_ = 0;
    uint32_t sectors_per_cluster_ = 0;
    uint32_t cluster_size_ = 0;
    uint32_t mft_record_size_ = 0;
    uint32_t index_block_size_ = 0;
    uint64_t total_sectors_ = 0;
    uint64_t mft_lcn_ = 0;
    StreamInfo mft_data_stream_;
    bool mounted_ = false;
};

class XvdPackageReader {
public:
    scarlett_hresult_t open_memory(std::vector<uint8_t> image_bytes, bool& out_is_encrypted) noexcept;
    scarlett_hresult_t open_file(const std::filesystem::path& path, bool& out_is_encrypted) noexcept;

    const XvdHeaderInfo& header() const noexcept { return header_; }
    const XvcDescriptorInfo& xvc() const noexcept { return xvc_; }
    const NtfsReadOnlyReader& ntfs() const noexcept { return ntfs_; }
    bool has_ntfs() const noexcept { return ntfs_.is_mounted(); }

private:
    scarlett_hresult_t parse_internal(bool& out_is_encrypted) noexcept;

    std::unique_ptr<XvdByteSource> source_;
    XvdHeaderInfo header_;
    XvcDescriptorInfo xvc_;
    NtfsReadOnlyReader ntfs_;
};

} // namespace scarlett::loader

#endif /* SCARLETT_LOADER_XVD_NTFS_HPP */
