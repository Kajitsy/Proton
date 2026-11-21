/*
 * Proton-Scarlett — Clean-Room XVD/XVC Parser & Read-Only NTFS Reader
 */

#include "xvd_ntfs.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace scarlett::loader {

namespace {

bool is_power_of_two_u32(uint32_t v) noexcept {
    return v != 0u && (v & (v - 1u)) == 0u;
}

uint64_t align_up_u64(uint64_t val, uint64_t align) noexcept {
    if (align <= 1u) {
        return val;
    }
    uint64_t rem = val % align;
    if (rem == 0u) {
        return val;
    }
    if (val > UINT64_MAX - (align - rem)) {
        return UINT64_MAX;
    }
    return val + (align - rem);
}

bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    if (a != 0u && b > UINT64_MAX / a) {
        return false;
    }
    out = a * b;
    return true;
}

bool checked_add_u64(uint64_t a, uint64_t b, uint64_t& out) noexcept {
    if (a > UINT64_MAX - b) {
        return false;
    }
    out = a + b;
    return true;
}

bool all_bytes_zero(const uint8_t* ptr, size_t len) noexcept {
    for (size_t i = 0; i < len; ++i) {
        if (ptr[i] != 0u) {
            return false;
        }
    }
    return true;
}

bool iequals_ascii(std::string_view a, std::string_view b) noexcept {
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

std::string utf16le_to_utf8(const uint8_t* data, size_t code_units) {
    std::string out;
    out.reserve(code_units);
    for (size_t i = 0; i < code_units; ++i) {
        uint16_t w1 = static_cast<uint16_t>(data[i * 2]) |
                      (static_cast<uint16_t>(data[i * 2 + 1]) << 8);
        uint32_t cp = w1;
        if (w1 >= 0xD800u && w1 <= 0xDBFFu && (i + 1) < code_units) {
            uint16_t w2 = static_cast<uint16_t>(data[(i + 1) * 2]) |
                          (static_cast<uint16_t>(data[(i + 1) * 2 + 1]) << 8);
            if (w2 >= 0xDC00u && w2 <= 0xDFFFu) {
                cp = 0x10000u + (((static_cast<uint32_t>(w1) - 0xD800u) << 10) |
                                 (static_cast<uint32_t>(w2) - 0xDC00u));
                ++i;
            }
        }
        if (cp <= 0x7Fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FFu) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp <= 0xFFFFu) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
}

bool apply_ntfs_usa_fixup(
    std::vector<uint8_t>& buf,
    uint16_t usa_offset,
    uint16_t usa_count,
    uint32_t bytes_per_sector) noexcept {
    if (usa_count == 0u || bytes_per_sector < 512u) {
        return false;
    }
    if (usa_count == 1u) {
        return true;
    }
    size_t usa_bytes = static_cast<size_t>(usa_count) * 2u;
    if (static_cast<size_t>(usa_offset) + usa_bytes > buf.size()) {
        return false;
    }
    size_t expected_sectors = static_cast<size_t>(usa_count - 1u);
    if (expected_sectors * bytes_per_sector > buf.size()) {
        return false;
    }
    const uint8_t usn0 = buf[usa_offset];
    const uint8_t usn1 = buf[usa_offset + 1];
    for (size_t i = 1; i < usa_count; ++i) {
        size_t sector_tail = i * bytes_per_sector - 2u;
        if (buf[sector_tail] != usn0 || buf[sector_tail + 1] != usn1) {
            return false;
        }
        buf[sector_tail] = buf[usa_offset + i * 2u];
        buf[sector_tail + 1] = buf[usa_offset + i * 2u + 1u];
    }
    return true;
}

bool decode_ntfs_data_runs(
    const uint8_t* pairs,
    size_t max_len,
    uint64_t base_vcn,
    std::vector<NtfsDataRun>& out_runs) {
    out_runs.clear();
    size_t pos = 0;
    uint64_t vcn = base_vcn;
    int64_t cur_lcn = 0;

    while (pos < max_len) {
        uint8_t hdr = pairs[pos++];
        if (hdr == 0u) {
            return true;
        }
        uint8_t len_bytes = hdr & 0x0Fu;
        uint8_t off_bytes = (hdr >> 4) & 0x0Fu;
        if (len_bytes == 0u || len_bytes > 8u || off_bytes > 8u) {
            return false;
        }
        if (pos + len_bytes + off_bytes > max_len) {
            return false;
        }

        uint64_t run_len = 0;
        for (uint8_t i = 0; i < len_bytes; ++i) {
            run_len |= static_cast<uint64_t>(pairs[pos++]) << (8u * i);
        }
        if (run_len == 0u) {
            return false;
        }

        NtfsDataRun run;
        run.vcn_start = vcn;
        run.cluster_count = run_len;

        if (off_bytes == 0u) {
            run.is_sparse = true;
            run.lcn_start = 0;
        } else {
            uint64_t raw_off = 0;
            for (uint8_t i = 0; i < off_bytes; ++i) {
                raw_off |= static_cast<uint64_t>(pairs[pos++]) << (8u * i);
            }
            if (off_bytes < 8u && (pairs[pos - 1] & 0x80u) != 0u) {
                raw_off |= (~UINT64_C(0)) << (8u * off_bytes);
            }
            int64_t delta = static_cast<int64_t>(raw_off);
            cur_lcn += delta;
            if (cur_lcn < 0) {
                return false;
            }
            run.is_sparse = false;
            run.lcn_start = cur_lcn;
        }

        out_runs.push_back(run);
        if (!checked_add_u64(vcn, run_len, vcn)) {
            return false;
        }
    }
    return true;
}

class MemoryByteSource final : public XvdByteSource {
public:
    explicit MemoryByteSource(std::vector<uint8_t> data) : data_(std::move(data)) {}

    uint64_t size() const noexcept override {
        return static_cast<uint64_t>(data_.size());
    }

    bool read_at(uint64_t offset, void* dst, size_t size) const noexcept override {
        if (size == 0u) {
            return true;
        }
        if (!dst || offset > data_.size() || size > data_.size() - offset) {
            return false;
        }
        std::memcpy(dst, data_.data() + offset, size);
        return true;
    }

private:
    std::vector<uint8_t> data_;
};

class FileByteSource final : public XvdByteSource {
public:
    explicit FileByteSource(int fd, uint64_t size) noexcept : fd_(fd), size_(size) {}

    ~FileByteSource() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    uint64_t size() const noexcept override {
        return size_;
    }

    bool read_at(uint64_t offset, void* dst, size_t size) const noexcept override {
        if (size == 0u) {
            return true;
        }
        if (fd_ < 0 || !dst || offset > size_ || size > size_ - offset) {
            return false;
        }
        uint8_t* out = static_cast<uint8_t*>(dst);
        size_t total = 0;
        while (total < size) {
            ssize_t n = ::pread(fd_, out + total, size - total,
                                static_cast<off_t>(offset + total));
            if (n <= 0) {
                return false;
            }
            total += static_cast<size_t>(n);
        }
        return true;
    }

private:
    int fd_ = -1;
    uint64_t size_ = 0;
};

} // namespace

std::unique_ptr<XvdByteSource> make_memory_byte_source(std::vector<uint8_t> data) {
    return std::make_unique<MemoryByteSource>(std::move(data));
}

std::unique_ptr<XvdByteSource> make_file_byte_source(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return nullptr;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<FileByteSource>(fd, static_cast<uint64_t>(st.st_size));
}

scarlett_hresult_t NtfsReadOnlyReader::mount(
    const XvdByteSource* source,
    uint64_t volume_offset) noexcept {
    mounted_ = false;
    source_ = nullptr;
    mft_data_stream_ = StreamInfo{};

    if (!source) {
        return SCARLETT_E_INVALIDARG;
    }
    uint64_t vbr_end = 0;
    if (!checked_add_u64(volume_offset, sizeof(scarlett_ntfs_vbr_t), vbr_end) ||
        vbr_end > source->size()) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    scarlett_ntfs_vbr_t vbr{};
    if (!source->read_at(volume_offset, &vbr, sizeof(vbr))) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    if (vbr.oem_id != SCARLETT_NTFS_OEM_ID || vbr.end_of_sector_marker != 0xAA55u) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    if (!is_power_of_two_u32(vbr.bytes_per_sector) ||
        vbr.bytes_per_sector < 512u || vbr.bytes_per_sector > 4096u) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    if (!is_power_of_two_u32(vbr.sectors_per_cluster) || vbr.sectors_per_cluster > 128u) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    uint32_t cluster_sz = static_cast<uint32_t>(vbr.bytes_per_sector) *
                          static_cast<uint32_t>(vbr.sectors_per_cluster);
    if (cluster_sz == 0u || cluster_sz > 65536u) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    auto decode_record_size = [cluster_sz](int8_t raw) -> uint32_t {
        if (raw > 0) {
            return static_cast<uint32_t>(raw) * cluster_sz;
        }
        if (raw < 0 && raw >= -16) {
            return UINT32_C(1) << static_cast<uint32_t>(-static_cast<int>(raw));
        }
        return 0u;
    };

    uint32_t mft_rec_sz = decode_record_size(vbr.clusters_per_file_record);
    uint32_t idx_blk_sz = decode_record_size(vbr.clusters_per_index_block);
    if (mft_rec_sz < 512u || mft_rec_sz > 65536u ||
        idx_blk_sz < 512u || idx_blk_sz > 65536u) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    source_ = source;
    volume_offset_ = volume_offset;
    bytes_per_sector_ = vbr.bytes_per_sector;
    sectors_per_cluster_ = vbr.sectors_per_cluster;
    cluster_size_ = cluster_sz;
    mft_record_size_ = mft_rec_sz;
    index_block_size_ = idx_blk_sz;
    total_sectors_ = vbr.total_sectors;
    mft_lcn_ = vbr.mft_lcn;

    // Bootstrap $MFT record 0 directly from mft_lcn_
    uint64_t mft_byte_off = 0;
    uint64_t mft_abs_off = 0;
    if (!checked_mul_u64(mft_lcn_, cluster_size_, mft_byte_off) ||
        !checked_add_u64(volume_offset_, mft_byte_off, mft_abs_off)) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    std::vector<uint8_t> rec0(mft_record_size_);
    if (!source_->read_at(mft_abs_off, rec0.data(), rec0.size())) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    scarlett_ntfs_mft_record_hdr_t hdr0{};
    std::memcpy(&hdr0, rec0.data(), sizeof(hdr0));
    if (hdr0.magic != SCARLETT_NTFS_MFT_MAGIC ||
        !apply_ntfs_usa_fixup(rec0, hdr0.usa_offset, hdr0.usa_count, bytes_per_sector_)) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    // Parse unnamed $DATA attribute of $MFT record 0
    size_t pos = hdr0.attrs_offset;
    size_t limit = std::min<size_t>(hdr0.bytes_in_use, rec0.size());
    while (pos + sizeof(scarlett_ntfs_attr_hdr_t) <= limit) {
        scarlett_ntfs_attr_hdr_t ah{};
        std::memcpy(&ah, rec0.data() + pos, sizeof(ah));
        if (ah.type == SCARLETT_NTFS_ATTR_END) {
            break;
        }
        if (ah.length < sizeof(scarlett_ntfs_attr_hdr_t) || pos + ah.length > limit) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }
        if (ah.type == SCARLETT_NTFS_ATTR_DATA && ah.name_length == 0u) {
            if (ah.non_resident == 0u) {
                if (ah.length < sizeof(scarlett_ntfs_attr_resident_t)) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                scarlett_ntfs_attr_resident_t res{};
                std::memcpy(&res, rec0.data() + pos, sizeof(res));
                if (static_cast<size_t>(res.value_offset) + res.value_length > ah.length) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                mft_data_stream_.valid = true;
                mft_data_stream_.non_resident = false;
                mft_data_stream_.data_size = res.value_length;
                mft_data_stream_.resident_data.assign(
                    rec0.data() + pos + res.value_offset,
                    rec0.data() + pos + res.value_offset + res.value_length);
            } else {
                if (ah.length < sizeof(scarlett_ntfs_attr_nonresident_t)) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                scarlett_ntfs_attr_nonresident_t nr{};
                std::memcpy(&nr, rec0.data() + pos, sizeof(nr));
                if (nr.mapping_pairs_offset > ah.length) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                mft_data_stream_.valid = true;
                mft_data_stream_.non_resident = true;
                mft_data_stream_.data_size = nr.data_size;
                if (!decode_ntfs_data_runs(
                        rec0.data() + pos + nr.mapping_pairs_offset,
                        ah.length - nr.mapping_pairs_offset,
                        nr.lowest_vcn,
                        mft_data_stream_.runs)) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
            }
            break;
        }
        pos += ah.length;
    }

    if (!mft_data_stream_.valid) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    mounted_ = true;
    return SCARLETT_OK;
}

scarlett_hresult_t NtfsReadOnlyReader::read_mft_record(
    uint64_t record_index,
    std::vector<uint8_t>& out_rec) const noexcept {
    if (!mounted_) {
        return SCARLETT_E_FAIL;
    }
    uint64_t rec_off = 0;
    if (!checked_mul_u64(record_index, mft_record_size_, rec_off)) {
        return SCARLETT_E_INVALIDARG;
    }
    out_rec.assign(mft_record_size_, 0u);
    size_t bytes_read = 0;
    scarlett_hresult_t hr = read_stream(mft_data_stream_, rec_off, out_rec.data(),
                                        out_rec.size(), bytes_read);
    if (scarlett_failed(hr) || bytes_read != out_rec.size()) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    scarlett_ntfs_mft_record_hdr_t hdr{};
    std::memcpy(&hdr, out_rec.data(), sizeof(hdr));
    if (hdr.magic != SCARLETT_NTFS_MFT_MAGIC ||
        (hdr.flags & SCARLETT_NTFS_MFT_FLAG_IN_USE) == 0u ||
        hdr.bytes_in_use > out_rec.size() ||
        hdr.attrs_offset >= hdr.bytes_in_use) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    if (!apply_ntfs_usa_fixup(out_rec, hdr.usa_offset, hdr.usa_count, bytes_per_sector_)) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t NtfsReadOnlyReader::read_stream(
    const StreamInfo& stream,
    uint64_t offset,
    void* dst,
    size_t size,
    size_t& out_read) const noexcept {
    out_read = 0;
    if (!stream.valid) {
        return SCARLETT_E_FAIL;
    }
    if (offset >= stream.data_size || size == 0u) {
        return SCARLETT_OK;
    }
    uint64_t avail = stream.data_size - offset;
    size_t to_read = static_cast<size_t>(std::min<uint64_t>(avail, size));
    uint8_t* out = static_cast<uint8_t*>(dst);

    if (!stream.non_resident) {
        if (offset + to_read > stream.resident_data.size()) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }
        std::memcpy(out, stream.resident_data.data() + offset, to_read);
        out_read = to_read;
        return SCARLETT_OK;
    }

    size_t done = 0;
    while (done < to_read) {
        uint64_t cur_off = offset + done;
        uint64_t target_vcn = cur_off / cluster_size_;
        uint64_t in_cluster_off = cur_off % cluster_size_;

        const NtfsDataRun* found_run = nullptr;
        for (const auto& run : stream.runs) {
            if (target_vcn >= run.vcn_start &&
                target_vcn < run.vcn_start + run.cluster_count) {
                found_run = &run;
                break;
            }
        }
        if (!found_run) {
            // Unallocated / sparse tail
            size_t chunk = to_read - done;
            std::memset(out + done, 0, chunk);
            done += chunk;
            break;
        }

        uint64_t rel_cluster = target_vcn - found_run->vcn_start;
        uint64_t rem_clusters = found_run->cluster_count - rel_cluster;
        uint64_t run_avail_bytes = rem_clusters * cluster_size_ - in_cluster_off;
        size_t chunk = static_cast<size_t>(
            std::min<uint64_t>(to_read - done, run_avail_bytes));

        if (found_run->is_sparse) {
            std::memset(out + done, 0, chunk);
        } else {
            uint64_t lcn = static_cast<uint64_t>(found_run->lcn_start) + rel_cluster;
            uint64_t vol_off = 0;
            uint64_t abs_off = 0;
            if (!checked_mul_u64(lcn, cluster_size_, vol_off) ||
                !checked_add_u64(vol_off, in_cluster_off, vol_off) ||
                !checked_add_u64(volume_offset_, vol_off, abs_off)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            if (!source_->read_at(abs_off, out + done, chunk)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
        }
        done += chunk;
    }

    out_read = done;
    return SCARLETT_OK;
}

scarlett_hresult_t NtfsReadOnlyReader::read_directory_entries_from_record(
    uint64_t dir_mft_record,
    std::vector<NtfsDirEntry>& out_entries) const noexcept {
    out_entries.clear();

    std::vector<uint8_t> rec;
    scarlett_hresult_t hr = read_mft_record(dir_mft_record, rec);
    if (scarlett_failed(hr)) {
        return hr;
    }

    scarlett_ntfs_mft_record_hdr_t hdr{};
    std::memcpy(&hdr, rec.data(), sizeof(hdr));
    if ((hdr.flags & SCARLETT_NTFS_MFT_FLAG_DIRECTORY) == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    bool has_index_root = false;
    scarlett_ntfs_index_root_t idx_root{};
    std::vector<uint8_t> idx_root_payload;
    StreamInfo idx_alloc_stream;

    size_t pos = hdr.attrs_offset;
    size_t limit = std::min<size_t>(hdr.bytes_in_use, rec.size());
    while (pos + sizeof(scarlett_ntfs_attr_hdr_t) <= limit) {
        scarlett_ntfs_attr_hdr_t ah{};
        std::memcpy(&ah, rec.data() + pos, sizeof(ah));
        if (ah.type == SCARLETT_NTFS_ATTR_END) {
            break;
        }
        if (ah.length < sizeof(scarlett_ntfs_attr_hdr_t) || pos + ah.length > limit) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }

        if (ah.type == SCARLETT_NTFS_ATTR_INDEX_ROOT && ah.non_resident == 0u) {
            if (ah.length < sizeof(scarlett_ntfs_attr_resident_t)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            scarlett_ntfs_attr_resident_t res{};
            std::memcpy(&res, rec.data() + pos, sizeof(res));
            if (res.value_length < sizeof(scarlett_ntfs_index_root_t) ||
                static_cast<size_t>(res.value_offset) + res.value_length > ah.length) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            std::memcpy(&idx_root, rec.data() + pos + res.value_offset, sizeof(idx_root));
            idx_root_payload.assign(
                rec.data() + pos + res.value_offset,
                rec.data() + pos + res.value_offset + res.value_length);
            has_index_root = true;
        } else if (ah.type == SCARLETT_NTFS_ATTR_INDEX_ALLOCATION && ah.non_resident != 0u) {
            if (ah.length < sizeof(scarlett_ntfs_attr_nonresident_t)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            scarlett_ntfs_attr_nonresident_t nr{};
            std::memcpy(&nr, rec.data() + pos, sizeof(nr));
            if (nr.mapping_pairs_offset > ah.length) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            idx_alloc_stream.valid = true;
            idx_alloc_stream.non_resident = true;
            idx_alloc_stream.data_size = nr.data_size;
            if (!decode_ntfs_data_runs(
                    rec.data() + pos + nr.mapping_pairs_offset,
                    ah.length - nr.mapping_pairs_offset,
                    nr.lowest_vcn,
                    idx_alloc_stream.runs)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
        }
        pos += ah.length;
    }

    if (!has_index_root) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    // Collect entries by MFT record so Win32/POSIX names override DOS 8.3 names cleanly
    struct PendingEntry {
        std::string name;
        uint8_t name_type = 2;
        uint64_t mft_record = 0;
        uint64_t size_bytes = 0;
        bool is_dir = false;
    };
    std::unordered_map<uint64_t, PendingEntry> by_mft;

    auto collect_from_index_entries = [&](const uint8_t* base, size_t max_bytes,
                                          uint32_t entries_off, uint32_t index_len) -> bool {
        if (entries_off > index_len || index_len > max_bytes) {
            return false;
        }
        size_t cur = entries_off;
        while (cur + sizeof(scarlett_ntfs_index_entry_hdr_t) <= index_len) {
            scarlett_ntfs_index_entry_hdr_t ie{};
            std::memcpy(&ie, base + cur, sizeof(ie));
            if (ie.entry_length < sizeof(scarlett_ntfs_index_entry_hdr_t) ||
                cur + ie.entry_length > index_len) {
                return false;
            }
            if ((ie.flags & SCARLETT_NTFS_INDEX_ENTRY_LAST) != 0u) {
                break;
            }
            if (ie.key_length >= sizeof(scarlett_ntfs_file_name_attr_t) &&
                sizeof(scarlett_ntfs_index_entry_hdr_t) + ie.key_length <= ie.entry_length) {
                scarlett_ntfs_file_name_attr_t fn{};
                std::memcpy(&fn, base + cur + sizeof(scarlett_ntfs_index_entry_hdr_t), sizeof(fn));
                size_t name_bytes = static_cast<size_t>(fn.name_length) * 2u;
                if (sizeof(scarlett_ntfs_file_name_attr_t) + name_bytes <= ie.key_length) {
                    std::string utf8 = utf16le_to_utf8(
                        base + cur + sizeof(scarlett_ntfs_index_entry_hdr_t) +
                            sizeof(scarlett_ntfs_file_name_attr_t),
                        fn.name_length);
                    uint64_t child_mft = ie.mft_reference & UINT64_C(0x0000FFFFFFFFFFFF);
                    if (!utf8.empty() && utf8 != "." && utf8 != ".." && utf8[0] != '$') {
                        bool is_dir = (fn.file_attributes & UINT32_C(0x10000000)) != 0u;
                        auto it = by_mft.find(child_mft);
                        if (it == by_mft.end() ||
                            (it->second.name_type == 2u && fn.name_type != 2u)) {
                            by_mft[child_mft] = PendingEntry{
                                std::move(utf8),
                                fn.name_type,
                                child_mft,
                                fn.data_size,
                                is_dir,
                            };
                        }
                    }
                }
            }
            cur += ie.entry_length;
        }
        return true;
    };

    const size_t root_hdr_off = offsetof(scarlett_ntfs_index_root_t, header);
    if (!collect_from_index_entries(
            idx_root_payload.data() + root_hdr_off,
            idx_root_payload.size() - root_hdr_off,
            idx_root.header.entries_offset,
            idx_root.header.index_length)) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    if (idx_alloc_stream.valid && idx_alloc_stream.data_size > 0u) {
        uint64_t block_count = idx_alloc_stream.data_size / index_block_size_;
        std::vector<uint8_t> blk(index_block_size_);
        for (uint64_t bi = 0; bi < block_count; ++bi) {
            size_t got = 0;
            hr = read_stream(idx_alloc_stream, bi * index_block_size_, blk.data(), blk.size(), got);
            if (scarlett_failed(hr) || got != blk.size()) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            scarlett_ntfs_indx_block_hdr_t indxh{};
            std::memcpy(&indxh, blk.data(), sizeof(indxh));
            if (indxh.magic != SCARLETT_NTFS_INDX_MAGIC) {
                continue;
            }
            if (!apply_ntfs_usa_fixup(blk, indxh.usa_offset, indxh.usa_count, bytes_per_sector_)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
            const size_t indxh_hdr_off = offsetof(scarlett_ntfs_indx_block_hdr_t, header);
            if (!collect_from_index_entries(
                    blk.data() + indxh_hdr_off,
                    blk.size() - indxh_hdr_off,
                    indxh.header.entries_offset,
                    indxh.header.index_length)) {
                return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
            }
        }
    }

    // Refine directory / file size from each child's MFT record when available
    for (const auto& [mft_id, pending] : by_mft) {
        NtfsDirEntry entry;
        entry.name = pending.name;
        entry.mft_record = mft_id;
        entry.is_directory = pending.is_dir;
        entry.size_bytes = pending.size_bytes;

        std::vector<uint8_t> child_rec;
        if (scarlett_succeeded(read_mft_record(mft_id, child_rec))) {
            scarlett_ntfs_mft_record_hdr_t chdr{};
            std::memcpy(&chdr, child_rec.data(), sizeof(chdr));
            entry.is_directory = (chdr.flags & SCARLETT_NTFS_MFT_FLAG_DIRECTORY) != 0u;
            if (!entry.is_directory) {
                size_t cpos = chdr.attrs_offset;
                size_t climit = std::min<size_t>(chdr.bytes_in_use, child_rec.size());
                while (cpos + sizeof(scarlett_ntfs_attr_hdr_t) <= climit) {
                    scarlett_ntfs_attr_hdr_t cah{};
                    std::memcpy(&cah, child_rec.data() + cpos, sizeof(cah));
                    if (cah.type == SCARLETT_NTFS_ATTR_END) {
                        break;
                    }
                    if (cah.length < sizeof(scarlett_ntfs_attr_hdr_t) || cpos + cah.length > climit) {
                        break;
                    }
                    if (cah.type == SCARLETT_NTFS_ATTR_DATA && cah.name_length == 0u) {
                        if (cah.non_resident == 0u &&
                            cah.length >= sizeof(scarlett_ntfs_attr_resident_t)) {
                            scarlett_ntfs_attr_resident_t cres{};
                            std::memcpy(&cres, child_rec.data() + cpos, sizeof(cres));
                            entry.size_bytes = cres.value_length;
                        } else if (cah.non_resident != 0u &&
                                   cah.length >= sizeof(scarlett_ntfs_attr_nonresident_t)) {
                            scarlett_ntfs_attr_nonresident_t cnr{};
                            std::memcpy(&cnr, child_rec.data() + cpos, sizeof(cnr));
                            entry.size_bytes = cnr.data_size;
                        }
                        break;
                    }
                    cpos += cah.length;
                }
            } else {
                entry.size_bytes = 0;
            }
        }
        out_entries.push_back(std::move(entry));
    }

    std::sort(out_entries.begin(), out_entries.end(),
              [](const NtfsDirEntry& a, const NtfsDirEntry& b) {
                  return a.name < b.name;
              });
    return SCARLETT_OK;
}

scarlett_hresult_t NtfsReadOnlyReader::resolve_path_record(
    std::string_view rel_path,
    uint64_t& out_mft_record,
    bool& out_is_dir,
    uint64_t& out_size) const noexcept {
    if (!mounted_) {
        return SCARLETT_E_FAIL;
    }

    uint64_t cur_mft = SCARLETT_NTFS_ROOT_MFT_RECORD;
    bool cur_is_dir = true;
    uint64_t cur_size = 0;

    size_t pos = 0;
    while (pos < rel_path.size()) {
        while (pos < rel_path.size() && (rel_path[pos] == '/' || rel_path[pos] == '\\')) {
            ++pos;
        }
        if (pos >= rel_path.size()) {
            break;
        }
        size_t next = pos;
        while (next < rel_path.size() && rel_path[next] != '/' && rel_path[next] != '\\') {
            ++next;
        }
        std::string_view comp = rel_path.substr(pos, next - pos);
        pos = next;
        if (comp == ".") {
            continue;
        }
        if (comp == ".." || !cur_is_dir) {
            return SCARLETT_E_FAIL;
        }

        std::vector<NtfsDirEntry> entries;
        scarlett_hresult_t hr = read_directory_entries_from_record(cur_mft, entries);
        if (scarlett_failed(hr)) {
            return hr;
        }

        bool found = false;
        for (const auto& e : entries) {
            if (iequals_ascii(e.name, comp)) {
                cur_mft = e.mft_record;
                cur_is_dir = e.is_directory;
                cur_size = e.size_bytes;
                found = true;
                break;
            }
        }
        if (!found) {
            return SCARLETT_E_FAIL;
        }
    }

    out_mft_record = cur_mft;
    out_is_dir = cur_is_dir;
    out_size = cur_size;
    return SCARLETT_OK;
}

scarlett_hresult_t NtfsReadOnlyReader::stat_path(
    std::string_view rel_path,
    NtfsFileStat& out_stat) const noexcept {
    out_stat = NtfsFileStat{};
    uint64_t rec = 0;
    bool is_dir = false;
    uint64_t sz = 0;
    scarlett_hresult_t hr = resolve_path_record(rel_path, rec, is_dir, sz);
    if (scarlett_failed(hr)) {
        return hr;
    }
    out_stat.exists = true;
    out_stat.mft_record = rec;
    out_stat.is_directory = is_dir;
    out_stat.size_bytes = sz;
    return SCARLETT_OK;
}

scarlett_hresult_t NtfsReadOnlyReader::list_directory(
    std::string_view rel_path,
    std::vector<NtfsDirEntry>& out_entries) const noexcept {
    uint64_t rec = 0;
    bool is_dir = false;
    uint64_t sz = 0;
    scarlett_hresult_t hr = resolve_path_record(rel_path, rec, is_dir, sz);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (!is_dir) {
        return SCARLETT_E_INVALIDARG;
    }
    return read_directory_entries_from_record(rec, out_entries);
}

scarlett_hresult_t NtfsReadOnlyReader::read_file(
    std::string_view rel_path,
    uint64_t offset,
    void* dst,
    size_t bytes_to_read,
    size_t& out_bytes_read) const noexcept {
    out_bytes_read = 0;
    if (!dst && bytes_to_read > 0u) {
        return SCARLETT_E_POINTER;
    }

    uint64_t mft_id = 0;
    bool is_dir = false;
    uint64_t sz = 0;
    scarlett_hresult_t hr = resolve_path_record(rel_path, mft_id, is_dir, sz);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (is_dir) {
        return SCARLETT_E_INVALIDARG;
    }

    std::vector<uint8_t> rec;
    hr = read_mft_record(mft_id, rec);
    if (scarlett_failed(hr)) {
        return hr;
    }

    scarlett_ntfs_mft_record_hdr_t hdr{};
    std::memcpy(&hdr, rec.data(), sizeof(hdr));
    StreamInfo stream;
    size_t pos = hdr.attrs_offset;
    size_t limit = std::min<size_t>(hdr.bytes_in_use, rec.size());
    while (pos + sizeof(scarlett_ntfs_attr_hdr_t) <= limit) {
        scarlett_ntfs_attr_hdr_t ah{};
        std::memcpy(&ah, rec.data() + pos, sizeof(ah));
        if (ah.type == SCARLETT_NTFS_ATTR_END) {
            break;
        }
        if (ah.length < sizeof(scarlett_ntfs_attr_hdr_t) || pos + ah.length > limit) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }
        if (ah.type == SCARLETT_NTFS_ATTR_DATA && ah.name_length == 0u) {
            if (ah.non_resident == 0u) {
                if (ah.length < sizeof(scarlett_ntfs_attr_resident_t)) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                scarlett_ntfs_attr_resident_t res{};
                std::memcpy(&res, rec.data() + pos, sizeof(res));
                if (static_cast<size_t>(res.value_offset) + res.value_length > ah.length) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                stream.valid = true;
                stream.non_resident = false;
                stream.data_size = res.value_length;
                stream.resident_data.assign(
                    rec.data() + pos + res.value_offset,
                    rec.data() + pos + res.value_offset + res.value_length);
            } else {
                if (ah.length < sizeof(scarlett_ntfs_attr_nonresident_t)) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                scarlett_ntfs_attr_nonresident_t nr{};
                std::memcpy(&nr, rec.data() + pos, sizeof(nr));
                if (nr.mapping_pairs_offset > ah.length) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
                stream.valid = true;
                stream.non_resident = true;
                stream.data_size = nr.data_size;
                if (!decode_ntfs_data_runs(
                        rec.data() + pos + nr.mapping_pairs_offset,
                        ah.length - nr.mapping_pairs_offset,
                        nr.lowest_vcn,
                        stream.runs)) {
                    return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
                }
            }
            break;
        }
        pos += ah.length;
    }

    if (!stream.valid) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    return read_stream(stream, offset, dst, bytes_to_read, out_bytes_read);
}

scarlett_hresult_t NtfsReadOnlyReader::read_entire_file(
    std::string_view rel_path,
    std::vector<uint8_t>& out_data) const noexcept {
    out_data.clear();
    NtfsFileStat st{};
    scarlett_hresult_t hr = stat_path(rel_path, st);
    if (scarlett_failed(hr)) {
        return hr;
    }
    if (st.is_directory) {
        return SCARLETT_E_INVALIDARG;
    }
    if (st.size_bytes > UINT64_C(256) * 1024u * 1024u) {
        return SCARLETT_E_OUTOFMEMORY;
    }
    out_data.resize(static_cast<size_t>(st.size_bytes));
    if (out_data.empty()) {
        return SCARLETT_OK;
    }
    size_t read_bytes = 0;
    hr = read_file(rel_path, 0, out_data.data(), out_data.size(), read_bytes);
    if (scarlett_failed(hr)) {
        out_data.clear();
        return hr;
    }
    out_data.resize(read_bytes);
    return SCARLETT_OK;
}

scarlett_hresult_t XvdPackageReader::open_memory(
    std::vector<uint8_t> image_bytes,
    bool& out_is_encrypted) noexcept {
    out_is_encrypted = false;
    source_ = make_memory_byte_source(std::move(image_bytes));
    return parse_internal(out_is_encrypted);
}

scarlett_hresult_t XvdPackageReader::open_file(
    const std::filesystem::path& path,
    bool& out_is_encrypted) noexcept {
    out_is_encrypted = false;
    source_ = make_file_byte_source(path);
    if (!source_) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    return parse_internal(out_is_encrypted);
}

scarlett_hresult_t XvdPackageReader::parse_internal(bool& out_is_encrypted) noexcept {
    out_is_encrypted = false;
    header_ = XvdHeaderInfo{};
    xvc_ = XvcDescriptorInfo{};

    if (!source_ || source_->size() < SCARLETT_XVD_MIN_IMAGE_SIZE) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    // Validate signature preamble region 0x000..0x200 bounds without DRM bypass
    std::vector<uint8_t> sig_area(SCARLETT_XVD_SIGNATURE_SIZE);
    if (!source_->read_at(SCARLETT_XVD_SIGNATURE_OFFSET, sig_area.data(), sig_area.size())) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    scarlett_xvd_raw_header_t raw{};
    if (!source_->read_at(SCARLETT_XVD_HEADER_OFFSET, &raw, sizeof(raw))) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }
    if (raw.magic != SCARLETT_XVD_MAGIC_LE64) {
        return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
    }

    // Strict Clean-Room gate: reject encrypted volumes IMMEDIATELY
    if ((raw.volume_flags & SCARLETT_XVD_VOLUME_FLAG_ENCRYPTION_DISABLED) == 0u ||
        (raw.volume_flags & SCARLETT_XVD_VOLUME_FLAG_ENCRYPTED_EXPLICIT) != 0u) {
        out_is_encrypted = true;
        return SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED;
    }

    header_.volume_flags = raw.volume_flags;
    header_.format_version = raw.format_version;
    header_.file_time = raw.file_time;
    header_.drive_size = raw.drive_size;
    std::memcpy(header_.vd_uid, raw.vd_uid, sizeof(header_.vd_uid));
    std::memcpy(header_.content_id, raw.content_id, sizeof(header_.content_id));
    header_.sequence_number = raw.sequence_number;
    header_.xvd_type = raw.xvd_type;
    header_.content_type = raw.content_type;
    header_.embedded_xvd_length = raw.embedded_xvd_length;
    header_.user_data_length = raw.user_data_length;
    header_.xvc_data_length = raw.xvc_data_length;
    header_.dyn_header_length = raw.dyn_header_length;
    header_.block_size = raw.block_size;

    size_t sb_len = 0;
    while (sb_len < sizeof(raw.sandbox_id) && raw.sandbox_id[sb_len] != 0u) {
        ++sb_len;
    }
    header_.sandbox_id.assign(reinterpret_cast<const char*>(raw.sandbox_id), sb_len);

    uint64_t cursor = SCARLETT_XVD_PAGE_SIZE;
    cursor = align_up_u64(cursor + align_up_u64(raw.embedded_xvd_length, SCARLETT_XVD_PAGE_SIZE),
                          SCARLETT_XVD_PAGE_SIZE);
    cursor = align_up_u64(cursor + align_up_u64(raw.user_data_length, SCARLETT_XVD_PAGE_SIZE),
                          SCARLETT_XVD_PAGE_SIZE);

    if (raw.xvc_data_length > 0u) {
        uint64_t xvc_end = 0;
        if (raw.xvc_data_length < sizeof(scarlett_xvc_raw_block1_header_t) ||
            !checked_add_u64(cursor, raw.xvc_data_length, xvc_end) ||
            xvc_end > source_->size()) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }

        std::vector<uint8_t> xvc_buf(raw.xvc_data_length);
        if (!source_->read_at(cursor, xvc_buf.data(), xvc_buf.size())) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }

        // Block 1: XVC Header (Content ID + Key/Encryption flags + Table counts)
        scarlett_xvc_raw_block1_header_t b1{};
        std::memcpy(&b1, xvc_buf.data(), sizeof(b1));
        if (b1.magic != SCARLETT_XVC_INFO_MAGIC) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }
        if (b1.key_count != 0u || b1.encryption_flags != 0u ||
            !all_bytes_zero(b1.primary_key_id, sizeof(b1.primary_key_id))) {
            out_is_encrypted = true;
            return SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED;
        }

        xvc_.present = true;
        xvc_.version = b1.version;
        std::memcpy(xvc_.content_id, b1.content_id, sizeof(xvc_.content_id));
        xvc_.key_count = b1.key_count;
        xvc_.encryption_flags = b1.encryption_flags;
        xvc_.chunk_size_bytes = b1.chunk_size_bytes;

        uint64_t region_bytes = 0;
        uint64_t chunk_bytes = 0;
        uint64_t update_bytes = 0;
        if (!checked_mul_u64(b1.region_count, sizeof(scarlett_xvc_raw_region_entry_t), region_bytes) ||
            !checked_mul_u64(b1.chunk_count, sizeof(scarlett_xvc_raw_chunk_entry_t), chunk_bytes) ||
            !checked_mul_u64(b1.update_segment_count, sizeof(scarlett_xvc_raw_update_segment_t), update_bytes)) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }

        uint64_t total_xvc_needed = sizeof(scarlett_xvc_raw_block1_header_t);
        if (!checked_add_u64(total_xvc_needed, region_bytes, total_xvc_needed) ||
            !checked_add_u64(total_xvc_needed, chunk_bytes, total_xvc_needed) ||
            !checked_add_u64(total_xvc_needed, update_bytes, total_xvc_needed) ||
            total_xvc_needed > xvc_buf.size()) {
            return SCARLETT_ERR_INVALID_PACKAGE_HEADER;
        }

        size_t off = sizeof(scarlett_xvc_raw_block1_header_t);

        // Block 2a: Intelligent Delivery Region Table
        xvc_.regions.reserve(b1.region_count);
        for (uint32_t i = 0; i < b1.region_count; ++i) {
            scarlett_xvc_raw_region_entry_t re{};
            std::memcpy(&re, xvc_buf.data() + off, sizeof(re));
            off += sizeof(re);

            if ((re.flags & SCARLETT_XVC_REGION_FLAG_ENCRYPTED) != 0u ||
                (re.key_id_index != 0u && re.key_id_index != 0xFFFFu)) {
                out_is_encrypted = true;
                return SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED;
            }

            XvcRegionInfo info;
            info.region_id = re.region_id;
            info.flags = re.flags;
            info.first_chunk_index = re.first_chunk_index;
            info.chunk_count = re.chunk_count;
            info.offset = re.offset;
            info.length = re.length;
            info.hash = re.hash;
            size_t sp_len = 0;
            while (sp_len < sizeof(re.specifier) && re.specifier[sp_len] != '\0') {
                ++sp_len;
            }
            info.specifier.assign(re.specifier, sp_len);
            xvc_.regions.push_back(std::move(info));
        }

        // Block 2b: Intelligent Delivery Chunk Table
        xvc_.chunks.reserve(b1.chunk_count);
        for (uint32_t i = 0; i < b1.chunk_count; ++i) {
            scarlett_xvc_raw_chunk_entry_t ce{};
            std::memcpy(&ce, xvc_buf.data() + off, sizeof(ce));
            off += sizeof(ce);

            XvcChunkInfo cinfo;
            cinfo.chunk_id = ce.chunk_id;
            cinfo.region_id = ce.region_id;
            cinfo.offset = ce.offset;
            cinfo.length = ce.length;
            cinfo.flags = ce.flags;
            xvc_.chunks.push_back(cinfo);
        }

        // Block 3: Update Segment Metadata Table
        xvc_.update_segments.reserve(b1.update_segment_count);
        for (uint32_t i = 0; i < b1.update_segment_count; ++i) {
            scarlett_xvc_raw_update_segment_t us{};
            std::memcpy(&us, xvc_buf.data() + off, sizeof(us));
            off += sizeof(us);

            XvcUpdateSegmentInfo uinfo;
            uinfo.segment_id = us.segment_id;
            uinfo.flags = us.flags;
            uinfo.target_offset = us.target_offset;
            uinfo.length = us.length;
            uinfo.base_version = us.base_version;
            std::memcpy(uinfo.segment_hash, us.segment_hash, sizeof(uinfo.segment_hash));
            xvc_.update_segments.push_back(uinfo);
        }

        cursor = align_up_u64(cursor + raw.xvc_data_length, SCARLETT_XVD_PAGE_SIZE);
    }

    uint64_t vol_with_dyn = cursor + align_up_u64(raw.dyn_header_length, SCARLETT_XVD_PAGE_SIZE);
    if (scarlett_failed(ntfs_.mount(source_.get(), vol_with_dyn))) {
        if (vol_with_dyn != cursor) {
            (void)ntfs_.mount(source_.get(), cursor);
        }
    }

    return SCARLETT_OK;
}

} // namespace scarlett::loader
