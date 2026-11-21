/*
 * Proton-Scarlett — Comprehensive Loader, VFS CoW, XVD/XVC/NTFS & API-Set Tests
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "game_config.hpp"
#include "loader_unix_abi.h"
#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"
#include "vfs_cow.hpp"
#include "xvd_ntfs.hpp"
#include "xvd_xvc_format.h"

namespace {

int g_failures = 0;

void check(bool cond, std::string_view msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

std::filesystem::path make_temp_dir(std::string_view tag) {
    auto base = std::filesystem::temp_directory_path() /
                ("scarlett_loader_test_" + std::string(tag) + "_" +
                 std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

void write_bytes_file(const std::filesystem::path& p, const void* data, size_t len) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
}

void write_text_file(const std::filesystem::path& p, std::string_view text) {
    write_bytes_file(p, text.data(), text.size());
}

std::vector<uint8_t> read_bytes_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

std::vector<uint8_t> make_synthetic_pe(bool is_pe64) {
    std::vector<uint8_t> buf(0x200, 0u);
    buf[0] = 'M';
    buf[1] = 'Z';
    uint32_t pe_off = 0x80u;
    std::memcpy(buf.data() + 0x3C, &pe_off, sizeof(pe_off));
    buf[pe_off + 0] = 'P';
    buf[pe_off + 1] = 'E';
    uint16_t machine = is_pe64 ? 0x8664u : 0x014Cu;
    uint16_t opt_magic = is_pe64 ? 0x020Bu : 0x010Bu;
    std::memcpy(buf.data() + pe_off + 4, &machine, sizeof(machine));
    std::memcpy(buf.data() + pe_off + 24, &opt_magic, sizeof(opt_magic));
    return buf;
}

// ============================================================================
// Synthetic Clean-Room XVD + 3-Block XVC + NTFS Image Builder
// ============================================================================

void stamp_usa_fixup(std::vector<uint8_t>& blk, uint16_t usa_off, uint16_t usn, uint32_t bps) {
    uint16_t sector_count = static_cast<uint16_t>(blk.size() / bps);
    std::memcpy(blk.data() + usa_off, &usn, sizeof(usn));
    for (uint16_t i = 0; i < sector_count; ++i) {
        size_t tail = static_cast<size_t>(i + 1u) * bps - 2u;
        uint16_t orig = 0;
        std::memcpy(&orig, blk.data() + tail, sizeof(orig));
        std::memcpy(blk.data() + usa_off + 2u * (i + 1u), &orig, sizeof(orig));
        std::memcpy(blk.data() + tail, &usn, sizeof(usn));
    }
}

void append_utf16le_filename_entry(
    std::vector<uint8_t>& buf,
    uint64_t mft_ref,
    uint64_t parent_ref,
    std::string_view ascii_name,
    bool is_dir,
    uint64_t file_size) {
    scarlett_ntfs_file_name_attr_t fn{};
    fn.parent_directory = parent_ref;
    fn.allocated_size = file_size;
    fn.data_size = file_size;
    fn.file_attributes = is_dir ? UINT32_C(0x10000000) : UINT32_C(0x20);
    fn.name_length = static_cast<uint8_t>(ascii_name.size());
    fn.name_type = 1; // Win32

    uint16_t key_len = static_cast<uint16_t>(sizeof(fn) + ascii_name.size() * 2u);
    uint16_t entry_len = static_cast<uint16_t>(
        ((sizeof(scarlett_ntfs_index_entry_hdr_t) + key_len) + 7u) & ~7u);

    scarlett_ntfs_index_entry_hdr_t ie{};
    ie.mft_reference = mft_ref;
    ie.entry_length = entry_len;
    ie.key_length = key_len;
    ie.flags = 0u;

    size_t start = buf.size();
    buf.resize(start + entry_len, 0u);
    std::memcpy(buf.data() + start, &ie, sizeof(ie));
    std::memcpy(buf.data() + start + sizeof(ie), &fn, sizeof(fn));
    for (size_t i = 0; i < ascii_name.size(); ++i) {
        buf[start + sizeof(ie) + sizeof(fn) + i * 2u] = static_cast<uint8_t>(ascii_name[i]);
        buf[start + sizeof(ie) + sizeof(fn) + i * 2u + 1u] = 0u;
    }
}

void append_last_index_entry(std::vector<uint8_t>& buf) {
    scarlett_ntfs_index_entry_hdr_t ie{};
    ie.entry_length = sizeof(ie);
    ie.flags = SCARLETT_NTFS_INDEX_ENTRY_LAST;
    size_t start = buf.size();
    buf.resize(start + sizeof(ie), 0u);
    std::memcpy(buf.data() + start, &ie, sizeof(ie));
}

// Helper to append a resident attribute to an MFT record buffer
void append_resident_attr(
    std::vector<uint8_t>& rec,
    uint32_t type,
    const void* payload,
    uint32_t payload_len) {
    uint32_t val_off = sizeof(scarlett_ntfs_attr_resident_t);
    uint32_t total_len = ((val_off + payload_len) + 7u) & ~7u;

    scarlett_ntfs_attr_resident_t ar{};
    ar.common.type = type;
    ar.common.length = total_len;
    ar.common.non_resident = 0u;
    ar.value_length = payload_len;
    ar.value_offset = static_cast<uint16_t>(val_off);

    size_t pos = rec.size();
    rec.resize(pos + total_len, 0u);
    std::memcpy(rec.data() + pos, &ar, sizeof(ar));
    if (payload_len > 0u) {
        std::memcpy(rec.data() + pos + val_off, payload, payload_len);
    }
}

// Encode single or multiple contiguous/non-contiguous LCN runs
std::vector<uint8_t> encode_data_runs(const std::vector<std::pair<uint64_t, uint64_t>>& runs) {
    // runs: vector of {lcn, cluster_count}
    std::vector<uint8_t> out;
    int64_t prev_lcn = 0;
    for (const auto& [lcn, count] : runs) {
        int64_t delta = static_cast<int64_t>(lcn) - prev_lcn;
        prev_lcn = static_cast<int64_t>(lcn);

        uint8_t len_bytes = 2; // enough for <= 65535 clusters
        uint8_t off_bytes = 4; // signed 32-bit delta
        out.push_back(static_cast<uint8_t>((off_bytes << 4) | len_bytes));
        out.push_back(static_cast<uint8_t>(count & 0xFFu));
        out.push_back(static_cast<uint8_t>((count >> 8) & 0xFFu));
        int32_t d32 = static_cast<int32_t>(delta);
        uint32_t u32 = static_cast<uint32_t>(d32);
        out.push_back(static_cast<uint8_t>(u32 & 0xFFu));
        out.push_back(static_cast<uint8_t>((u32 >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>((u32 >> 16) & 0xFFu));
        out.push_back(static_cast<uint8_t>((u32 >> 24) & 0xFFu));
    }
    out.push_back(0u);
    return out;
}

void append_nonresident_attr(
    std::vector<uint8_t>& rec,
    uint32_t type,
    uint64_t total_clusters,
    uint64_t data_size,
    uint64_t cluster_size,
    const std::vector<uint8_t>& run_bytes) {
    uint16_t pairs_off = sizeof(scarlett_ntfs_attr_nonresident_t);
    uint32_t total_len = ((pairs_off + static_cast<uint32_t>(run_bytes.size())) + 7u) & ~7u;

    scarlett_ntfs_attr_nonresident_t nr{};
    nr.common.type = type;
    nr.common.length = total_len;
    nr.common.non_resident = 1u;
    nr.lowest_vcn = 0;
    nr.highest_vcn = total_clusters > 0u ? (total_clusters - 1u) : 0u;
    nr.mapping_pairs_offset = pairs_off;
    nr.allocated_size = total_clusters * cluster_size;
    nr.data_size = data_size;
    nr.initialized_size = data_size;

    size_t pos = rec.size();
    rec.resize(pos + total_len, 0u);
    std::memcpy(rec.data() + pos, &nr, sizeof(nr));
    std::memcpy(rec.data() + pos + pairs_off, run_bytes.data(), run_bytes.size());
}

std::vector<uint8_t> finalize_mft_record(
    std::vector<uint8_t> attrs_body,
    uint32_t rec_num,
    uint16_t flags,
    uint32_t rec_size,
    uint32_t bps) {
    // Append end marker 0xFFFFFFFF
    uint32_t end_marker = SCARLETT_NTFS_ATTR_END;
    size_t body_pos = attrs_body.size();
    attrs_body.resize(body_pos + 4u);
    std::memcpy(attrs_body.data() + body_pos, &end_marker, sizeof(end_marker));

    constexpr uint16_t kUsaOff = 48u;
    constexpr uint16_t kUsaCount = 3u; // 1 USN + 2 sectors for 1024-byte MFT record
    constexpr uint16_t kAttrsOff = 56u;

    std::vector<uint8_t> rec(rec_size, 0u);
    scarlett_ntfs_mft_record_hdr_t hdr{};
    hdr.magic = SCARLETT_NTFS_MFT_MAGIC;
    hdr.usa_offset = kUsaOff;
    hdr.usa_count = kUsaCount;
    hdr.sequence_number = 1u;
    hdr.link_count = 1u;
    hdr.attrs_offset = kAttrsOff;
    hdr.flags = flags;
    hdr.bytes_in_use = static_cast<uint32_t>(kAttrsOff + attrs_body.size());
    hdr.bytes_allocated = rec_size;
    hdr.mft_record_number = rec_num;

    std::memcpy(rec.data(), &hdr, sizeof(hdr));
    std::memcpy(rec.data() + kAttrsOff, attrs_body.data(), attrs_body.size());
    stamp_usa_fixup(rec, kUsaOff, kUsaCount, bps);
    return rec;
}

std::vector<uint8_t> build_synthetic_xvd_image(
    bool encrypted_xvd_flag,
    bool encrypted_xvc_key,
    bool encrypted_xvc_region,
    std::string_view small_text,
    const std::vector<uint8_t>& stream_bytes) {
    constexpr uint32_t kBps = 512u;
    constexpr uint32_t kSpc = 2u; // 1024-byte cluster
    constexpr uint32_t kClusterSize = kBps * kSpc;
    constexpr uint32_t kMftRecSize = 1024u;
    constexpr uint32_t kIdxBlockSize = 4096u;
    constexpr uint32_t kTotalClusters = 64u;

    // Layout inside XVD:
    // 0x0000..0x0200: signature
    // 0x0200..0x0400: XVD header
    // 0x1000..0x2000: user data (0x1000 bytes)
    // 0x2000..0x3000: 3-block XVC descriptor
    // 0x3000..      : NTFS volume (64 clusters * 1024 = 64 KiB)
    constexpr uint64_t kUserDataOffset = 0x1000u;
    constexpr uint32_t kUserDataLen = 0x1000u;
    constexpr uint64_t kXvcOffset = 0x2000u;
    constexpr uint64_t kNtfsOffset = 0x3000u;

    // Build 3-block XVC descriptor
    scarlett_xvc_raw_block1_header_t b1{};
    b1.magic = SCARLETT_XVC_INFO_MAGIC;
    b1.version = 1u;
    for (uint8_t i = 0; i < 16u; ++i) {
        b1.content_id[i] = static_cast<uint8_t>(0xA0u + i);
    }
    b1.key_count = encrypted_xvc_key ? 1u : 0u;
    b1.encryption_flags = 0u;
    b1.region_count = 2u;
    b1.chunk_count = 3u;
    b1.update_segment_count = 2u;
    b1.chunk_size_bytes = 65536u;
    if (encrypted_xvc_key) {
        b1.primary_key_id[0] = 0x42u;
    }

    scarlett_xvc_raw_region_entry_t reg0{};
    reg0.region_id = 1u;
    reg0.flags = SCARLETT_XVC_REGION_FLAG_INITIAL_PLAY | SCARLETT_XVC_REGION_FLAG_REQUIRED |
                 (encrypted_xvc_region ? SCARLETT_XVC_REGION_FLAG_ENCRYPTED : 0u);
    reg0.key_id_index = encrypted_xvc_region ? 1u : 0u;
    reg0.first_chunk_index = 0u;
    reg0.chunk_count = 2u;
    reg0.offset = 0u;
    reg0.length = 32768u;
    reg0.hash = UINT64_C(0x1122334455667788);
    std::snprintf(reg0.specifier, sizeof(reg0.specifier), "BaseGame");

    scarlett_xvc_raw_region_entry_t reg1{};
    reg1.region_id = 2u;
    reg1.flags = SCARLETT_XVC_REGION_FLAG_RESIDENT;
    reg1.key_id_index = 0u;
    reg1.first_chunk_index = 2u;
    reg1.chunk_count = 1u;
    reg1.offset = 32768u;
    reg1.length = 32768u;
    reg1.hash = UINT64_C(0x8877665544332211);
    std::snprintf(reg1.specifier, sizeof(reg1.specifier), "HighResTex");

    scarlett_xvc_raw_chunk_entry_t chunks[3]{};
    chunks[0] = { 0u, 1u, 0u, 16384u, 0u, 0u };
    chunks[1] = { 1u, 1u, 16384u, 16384u, 0u, 0u };
    chunks[2] = { 2u, 2u, 32768u, 32768u, 0u, 0u };

    scarlett_xvc_raw_update_segment_t segs[2]{};
    segs[0].segment_id = 10u;
    segs[0].flags = 1u;
    segs[0].target_offset = 0u;
    segs[0].length = 4096u;
    segs[0].base_version = 100u;
    segs[0].segment_hash[0] = 0xABu;
    segs[1].segment_id = 11u;
    segs[1].flags = 2u;
    segs[1].target_offset = 4096u;
    segs[1].length = 8192u;
    segs[1].base_version = 101u;
    segs[1].segment_hash[0] = 0xCDu;

    std::vector<uint8_t> xvc_blob;
    auto append_raw = [&xvc_blob](const void* p, size_t n) {
        size_t pos = xvc_blob.size();
        xvc_blob.resize(pos + n);
        std::memcpy(xvc_blob.data() + pos, p, n);
    };
    append_raw(&b1, sizeof(b1));
    append_raw(&reg0, sizeof(reg0));
    append_raw(&reg1, sizeof(reg1));
    append_raw(chunks, sizeof(chunks));
    append_raw(segs, sizeof(segs));

    uint64_t ntfs_bytes = static_cast<uint64_t>(kTotalClusters) * kClusterSize;
    std::vector<uint8_t> img(kNtfsOffset + ntfs_bytes, 0u);

    // Signature preamble 0x000..0x200 (Clean-room placeholder pattern)
    for (size_t i = 0; i < 16; ++i) {
        img[i] = static_cast<uint8_t>(0x5Au);
    }

    // XVD Header at 0x200
    scarlett_xvd_raw_header_t xvd{};
    xvd.magic = SCARLETT_XVD_MAGIC_LE64;
    xvd.volume_flags = SCARLETT_XVD_VOLUME_FLAG_READ_ONLY |
                       (encrypted_xvd_flag ? 0u : SCARLETT_XVD_VOLUME_FLAG_ENCRYPTION_DISABLED);
    xvd.format_version = 3u;
    xvd.file_time = UINT64_C(133500000000000000);
    xvd.drive_size = ntfs_bytes;
    std::memcpy(xvd.content_id, b1.content_id, sizeof(xvd.content_id));
    xvd.user_data_length = kUserDataLen;
    xvd.xvc_data_length = static_cast<uint32_t>(xvc_blob.size());
    xvd.dyn_header_length = 0u;
    xvd.block_size = 0x1000u;
    std::snprintf(reinterpret_cast<char*>(xvd.sandbox_id), sizeof(xvd.sandbox_id), "SCARLETT");
    std::memcpy(img.data() + SCARLETT_XVD_HEADER_OFFSET, &xvd, sizeof(xvd));
    std::memset(img.data() + kUserDataOffset, 0x33, kUserDataLen);
    std::memcpy(img.data() + kXvcOffset, xvc_blob.data(), xvc_blob.size());

    // Build NTFS volume starting at kNtfsOffset
    // Cluster 0: VBR
    // Clusters 4..15: $MFT (12 clusters = 12 MFT records: #0..#11)
    // Clusters 16..19: $INDEX_ALLOCATION INDX block for "Data" directory (4096 bytes = 4 clusters)
    // Clusters 24..25: Stream.bin run 1 (2 clusters = 2048 bytes)
    // Clusters 32..33: Stream.bin run 2 (2 clusters = 2048 bytes)
    scarlett_ntfs_vbr_t vbr{};
    vbr.oem_id = SCARLETT_NTFS_OEM_ID;
    vbr.bytes_per_sector = kBps;
    vbr.sectors_per_cluster = kSpc;
    vbr.total_sectors = kTotalClusters * kSpc;
    vbr.mft_lcn = 4u;
    vbr.mft_mirr_lcn = 4u;
    vbr.clusters_per_file_record = -10; // 2^10 = 1024 bytes
    vbr.clusters_per_index_block = -12; // 2^12 = 4096 bytes
    vbr.end_of_sector_marker = 0xAA55u;
    std::memcpy(img.data() + kNtfsOffset, &vbr, sizeof(vbr));

    auto write_mft = [&](uint64_t rec_idx, const std::vector<uint8_t>& rec_buf) {
        std::memcpy(img.data() + kNtfsOffset + 4u * kClusterSize + rec_idx * kMftRecSize,
                    rec_buf.data(), rec_buf.size());
    };

    // Record 0: $MFT (12 clusters at LCN 4..15)
    {
        std::vector<uint8_t> attrs;
        auto runs = encode_data_runs({{4u, 12u}});
        append_nonresident_attr(attrs, SCARLETT_NTFS_ATTR_DATA, 12u, 12u * kMftRecSize, kClusterSize, runs);
        write_mft(0u, finalize_mft_record(
                          std::move(attrs), 0u, SCARLETT_NTFS_MFT_FLAG_IN_USE, kMftRecSize, kBps));
    }

    // Record 5: Root directory "/" with $INDEX_ROOT containing "Data" -> MFT record 6
    {
        std::vector<uint8_t> entries;
        append_utf16le_filename_entry(entries, 6u, 5u, "Data", true, 0u);
        append_last_index_entry(entries);

        scarlett_ntfs_index_root_t ir{};
        ir.indexed_attr_type = SCARLETT_NTFS_ATTR_FILE_NAME;
        ir.collation_rule = 1u;
        ir.index_block_size = kIdxBlockSize;
        ir.clusters_per_index_block = 4u;
        ir.header.entries_offset = sizeof(scarlett_ntfs_index_header_t);
        ir.header.index_length = static_cast<uint32_t>(sizeof(scarlett_ntfs_index_header_t) + entries.size());
        ir.header.allocated_size = ir.header.index_length;

        std::vector<uint8_t> ir_payload(sizeof(ir) + entries.size());
        std::memcpy(ir_payload.data(), &ir, sizeof(ir));
        std::memcpy(ir_payload.data() + sizeof(ir), entries.data(), entries.size());

        std::vector<uint8_t> attrs;
        append_resident_attr(attrs, SCARLETT_NTFS_ATTR_INDEX_ROOT, ir_payload.data(),
                             static_cast<uint32_t>(ir_payload.size()));
        write_mft(5u, finalize_mft_record(
                          std::move(attrs), 5u,
                          SCARLETT_NTFS_MFT_FLAG_IN_USE | SCARLETT_NTFS_MFT_FLAG_DIRECTORY,
                          kMftRecSize, kBps));
    }

    // Record 6: "Data" directory with empty $INDEX_ROOT + non-resident $INDEX_ALLOCATION at LCN 16..19
    {
        std::vector<uint8_t> root_entries;
        append_last_index_entry(root_entries);

        scarlett_ntfs_index_root_t ir{};
        ir.indexed_attr_type = SCARLETT_NTFS_ATTR_FILE_NAME;
        ir.collation_rule = 1u;
        ir.index_block_size = kIdxBlockSize;
        ir.clusters_per_index_block = 4u;
        ir.header.entries_offset = sizeof(scarlett_ntfs_index_header_t);
        ir.header.index_length = static_cast<uint32_t>(sizeof(scarlett_ntfs_index_header_t) + root_entries.size());
        ir.header.allocated_size = ir.header.index_length;
        ir.header.flags = 1u; // has sub-nodes ($INDEX_ALLOCATION)

        std::vector<uint8_t> ir_payload(sizeof(ir) + root_entries.size());
        std::memcpy(ir_payload.data(), &ir, sizeof(ir));
        std::memcpy(ir_payload.data() + sizeof(ir), root_entries.data(), root_entries.size());

        // Build INDX block at LCN 16 (4 clusters = 4096 bytes)
        std::vector<uint8_t> indx_entries;
        append_utf16le_filename_entry(indx_entries, 7u, 6u, "Small.txt", false, small_text.size());
        append_utf16le_filename_entry(indx_entries, 8u, 6u, "Stream.bin", false, stream_bytes.size());
        append_last_index_entry(indx_entries);

        std::vector<uint8_t> indx(kIdxBlockSize, 0u);
        constexpr uint16_t kIndxUsaOff = 40u;
        constexpr uint16_t kIndxUsaCount = 9u; // 1 USN + 8 sectors (4096 / 512)
        constexpr uint32_t kEntriesRelToHeader = 40u;

        scarlett_ntfs_indx_block_hdr_t ih{};
        ih.magic = SCARLETT_NTFS_INDX_MAGIC;
        ih.usa_offset = kIndxUsaOff;
        ih.usa_count = kIndxUsaCount;
        ih.vcn = 0u;
        ih.header.entries_offset = kEntriesRelToHeader;
        ih.header.index_length = static_cast<uint32_t>(kEntriesRelToHeader + indx_entries.size());
        ih.header.allocated_size = kIdxBlockSize - 24u;

        std::memcpy(indx.data(), &ih, sizeof(ih));
        std::memcpy(indx.data() + offsetof(scarlett_ntfs_indx_block_hdr_t, header) + kEntriesRelToHeader,
                    indx_entries.data(), indx_entries.size());
        stamp_usa_fixup(indx, kIndxUsaOff, kIndxUsaCount, kBps);
        std::memcpy(img.data() + kNtfsOffset + 16u * kClusterSize, indx.data(), indx.size());

        std::vector<uint8_t> attrs;
        append_resident_attr(attrs, SCARLETT_NTFS_ATTR_INDEX_ROOT, ir_payload.data(),
                             static_cast<uint32_t>(ir_payload.size()));
        auto idx_runs = encode_data_runs({{16u, 4u}});
        append_nonresident_attr(attrs, SCARLETT_NTFS_ATTR_INDEX_ALLOCATION, 4u, kIdxBlockSize,
                                kClusterSize, idx_runs);
        write_mft(6u, finalize_mft_record(
                          std::move(attrs), 6u,
                          SCARLETT_NTFS_MFT_FLAG_IN_USE | SCARLETT_NTFS_MFT_FLAG_DIRECTORY,
                          kMftRecSize, kBps));
    }

    // Record 7: "Small.txt" with resident $DATA
    {
        std::vector<uint8_t> attrs;
        append_resident_attr(attrs, SCARLETT_NTFS_ATTR_DATA, small_text.data(),
                             static_cast<uint32_t>(small_text.size()));
        write_mft(7u, finalize_mft_record(
                          std::move(attrs), 7u, SCARLETT_NTFS_MFT_FLAG_IN_USE, kMftRecSize, kBps));
    }

    // Record 8: "Stream.bin" with non-resident $DATA split across 2 disjoint runs (LCN 24..25 and LCN 32..33)
    {
        size_t first_half = std::min<size_t>(stream_bytes.size(), 2u * kClusterSize);
        if (first_half > 0u) {
            std::memcpy(img.data() + kNtfsOffset + 24u * kClusterSize, stream_bytes.data(), first_half);
        }
        if (stream_bytes.size() > first_half) {
            std::memcpy(img.data() + kNtfsOffset + 32u * kClusterSize,
                        stream_bytes.data() + first_half, stream_bytes.size() - first_half);
        }

        std::vector<uint8_t> attrs;
        auto runs = encode_data_runs({{24u, 2u}, {32u, 2u}});
        append_nonresident_attr(attrs, SCARLETT_NTFS_ATTR_DATA, 4u, stream_bytes.size(),
                                kClusterSize, runs);
        write_mft(8u, finalize_mft_record(
                          std::move(attrs), 8u, SCARLETT_NTFS_MFT_FLAG_IN_USE, kMftRecSize, kBps));
    }

    return img;
}

// ============================================================================
// Test 1: MicrosoftGame.Config XML Parser
// ============================================================================

void test_microsoft_game_config() {
    const std::string scarlett_xml = R"XML(<?xml version="1.0" encoding="utf-8"?>
<Game configVersion="1">
  <!-- Scarlett Gen9 configuration -->
  <Identity Name="Contoso.ScarlettDemo"
            Publisher="CN=Contoso, O=Contoso Games"
            Version="2.4.18.0" />
  <ExecutableList>
    <Executable Name="ContosoPC.exe"
                Id="AppPC"
                TargetDeviceFamily="Windows.Desktop" />
    <Executable Name="ContosoDev.exe"
                Id="AppDev"
                TargetDeviceFamily="Gaming.Xbox.Scarlett.x64"
                IsDevOnly="true" />
    <Executable Name="ContosoScarlett.exe"
                Id="AppScarlett"
                TargetDeviceFamily="Gaming.Xbox.Scarlett.x64"
                Alias="MainGame"
                OverrideDisplayName="Contoso &amp; Arena" />
  </ExecutableList>
  <ShellVisuals DefaultDisplayName="Contoso Scarlett &lt;Gen9&gt;"
                PublisherDisplayName="Contoso Interactive"
                StoreLogo="Assets\StoreLogo.png"
                Square150x150Logo="Assets\150.png"
                Square44x44Logo="Assets\44.png"
                Description="Next-gen Scarlett Demo"
                BackgroundColor="#101010"
                SplashScreenImage="Assets\Splash.png" />
  <AdvancedUserModel>true</AdvancedUserModel>
  <TitleId>41335933</TitleId>
  <MSAAppId>00000000481A1234</MSAAppId>
  <VirtualMachine Enabled="true" VirtualMachineId="scarlett-vm-01"/>
  <DesktopRegistration DisableRegistryVirtualization="true" EnableReservedDriveLetters="false">
    <ModFolder Name="ContosoMods"/>
  </DesktopRegistration>
</Game>)XML";

    auto tmp_dir = make_temp_dir("cfg");
    auto pe64_bytes = make_synthetic_pe(true);
    auto pe32_bytes = make_synthetic_pe(false);
    write_bytes_file(tmp_dir / "ContosoScarlett.exe", pe64_bytes.data(), pe64_bytes.size());
    write_bytes_file(tmp_dir / "ContosoPC.exe", pe32_bytes.data(), pe32_bytes.size());

    scarlett::loader::MicrosoftGameConfig cfg;
    check(scarlett::loader::parse_microsoft_game_config(scarlett_xml, cfg) == SCARLETT_OK,
          "Valid Scarlett MicrosoftGame.Config must parse cleanly");
    check(cfg.is_scarlett, "Scarlett config must set is_scarlett == true");
    check(cfg.identity.name == "Contoso.ScarlettDemo", "Identity Name mismatch");
    check(cfg.identity.version == "2.4.18.0", "Identity Version mismatch");
    check(cfg.executables.size() == 3u, "ExecutableList count mismatch");
    check(cfg.selected_executable == "ContosoScarlett.exe",
          "Non-IsDevOnly Scarlett executable should be preferred");
    check(cfg.title_id == 0x41335933u, "TitleId hex parsing mismatch");
    check(cfg.msa_app_id == "00000000481A1234", "MSAAppId mismatch");
    check(cfg.advanced_user_model, "AdvancedUserModel should be true");
    check(cfg.virtual_machine.present && cfg.virtual_machine.enabled &&
              cfg.virtual_machine.virtual_machine_id == "scarlett-vm-01",
          "VirtualMachine fields mismatch");
    check(cfg.desktop_registration.present &&
              cfg.desktop_registration.disable_registry_virtualization &&
              cfg.desktop_registration.mod_folder == "ContosoMods",
          "DesktopRegistration fields mismatch");
    check(cfg.shell_visuals.default_display_name == "Contoso Scarlett <Gen9>",
          "ShellVisuals entity decoding mismatch");

    check(scarlett::loader::select_scarlett_pe64_executable(cfg, tmp_dir) == SCARLETT_OK,
          "select_scarlett_pe64_executable must validate PE64 binary on disk");

    scarlett::loader::XSystemMetadata xsys;
    scarlett::loader::XPackageMetadata xpkg;
    scarlett::loader::extract_xsystem_xpackage_metadata(cfg, SCARLETT_MODEL_ANACONDA, xsys, xpkg);
    check(xsys.title_id == 0x41335933u && xsys.is_scarlett, "XSystem metadata mismatch");
    check(xpkg.package_full_name.find("Contoso.ScarlettDemo_2.4.18.0_x64__") == 0u,
          "XPackage full name mismatch");

    // XboxGen9 variant
    const std::string gen9_xml = R"XML(<Game configVersion="1">
  <Identity Name="Gen9.Title" Publisher="CN=Test" Version="1.0.0.0"/>
  <ExecutableList>
    <Executable Name="Gen9.exe" TargetDeviceFamily="XboxGen9"/>
  </ExecutableList>
  <TitleId>0x1234ABCD</TitleId>
</Game>)XML";
    scarlett::loader::MicrosoftGameConfig gen9_cfg;
    check(scarlett::loader::parse_microsoft_game_config(gen9_xml, gen9_cfg) == SCARLETT_OK &&
              gen9_cfg.is_scarlett && gen9_cfg.title_id == 0x1234ABCDu,
          "XboxGen9 MicrosoftGame.Config must detect Scarlett and hex TitleId");

    // PC-only config
    const std::string pc_xml = R"XML(<Game configVersion="1">
  <Identity Name="DesktopOnly" Publisher="CN=PC" Version="1.0.0.0"/>
  <ExecutableList>
    <Executable Name="GamePC.exe" TargetDeviceFamily="Windows.Desktop"/>
  </ExecutableList>
</Game>)XML";
    scarlett::loader::MicrosoftGameConfig pc_cfg;
    check(scarlett::loader::parse_microsoft_game_config(pc_xml, pc_cfg) == SCARLETT_OK,
          "PC-only XML should parse structurally");
    check(!pc_cfg.is_scarlett, "PC-only config must have is_scarlett == false");
    check(scarlett::loader::select_scarlett_pe64_executable(pc_cfg, {}) ==
              SCARLETT_ERR_CONFIG_PARSE_FAILED,
          "Selecting Scarlett executable from PC-only config must fail");

    // Malformed XML variants
    const char* malformed_cases[] = {
        "",
        "<Game><Identity Name=\"x\"></Game>",
        "<NotAGame/>",
        "<Game><TitleId>NOT_HEX</TitleId></Game>",
        "<Game><Identity Name=\"unclosed</Game>",
    };
    for (const char* bad : malformed_cases) {
        scarlett::loader::MicrosoftGameConfig bad_cfg;
        check(scarlett::loader::parse_microsoft_game_config(bad, bad_cfg) ==
                  SCARLETT_ERR_CONFIG_PARSE_FAILED,
              "Malformed MicrosoftGame.Config must return SCARLETT_ERR_CONFIG_PARSE_FAILED");
    }

    std::filesystem::remove_all(tmp_dir);
}

// ============================================================================
// Test 2: VFS GAME: / D:\ CoW Overlay + Read-Only Integrity + Whiteout
// ============================================================================

void test_vfs_cow_overlay() {
    auto root = make_temp_dir("vfs");
    auto base_dir = root / "game_ro";
    auto overlay_dir = root / "overlay";

    const std::string orig_pak = "ORIG_PAK_PAYLOAD_0123456789";
    const std::string orig_ini = "[Graphics]\nHDR=1\n";
    const std::string orig_bin = "BINARY_BASE_BLOB";

    write_text_file(base_dir / "Content" / "Data.pak", orig_pak);
    write_text_file(base_dir / "Content" / "Sub" / "Config.ini", orig_ini);
    write_text_file(base_dir / "Bin" / "Asset.bin", orig_bin);

    scarlett::loader::GameCowVfs vfs;
    // Overlay inside base must be rejected as read-only violation
    check(vfs.mount_directory(base_dir, base_dir / "bad_ov") ==
              SCARLETT_ERR_VFS_READONLY_VIOLATION,
          "Mounting overlay inside read-only base must fail with READONLY_VIOLATION");

    check(vfs.mount_directory(base_dir, overlay_dir) == SCARLETT_OK,
          "VFS mount_directory must succeed");
    check(vfs.write_direct_to_base_forbidden() == SCARLETT_ERR_VFS_READONLY_VIOLATION,
          "Direct write to base must return SCARLETT_ERR_VFS_READONLY_VIOLATION");

    // Case-insensitive reads via both GAME:\ and D:\ prefixes
    uint64_t fh = 0;
    uint64_t fsz = 0;
    scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
    check(vfs.open_file("GAME:\\content\\DATA.PAK", SCARLETT_VFS_ACCESS_READ, fh, fsz, layer) ==
                  SCARLETT_OK &&
              layer == SCARLETT_VFS_LAYER_BASE_DIR && fsz == orig_pak.size(),
          "Case-insensitive read from GAME: base layer failed");

    std::string read_buf(fsz, '\0');
    uint32_t got = 0;
    check(vfs.read_file(fh, 0, read_buf.data(), static_cast<uint32_t>(fsz), got) == SCARLETT_OK &&
              read_buf == orig_pak,
          "Read contents mismatch from GAME: base");
    // Attempt write on read-only handle must fail
    uint32_t wr_dummy = 0;
    check(vfs.write_file(fh, 0, "X", 1, wr_dummy) == SCARLETT_ERR_VFS_READONLY_VIOLATION,
          "Writing to read-only handle must fail with READONLY_VIOLATION");
    check(vfs.close_file(fh) == SCARLETT_OK, "Close read handle failed");

    // Read via D:\ synonym
    check(vfs.open_file("d:/CONTENT/sub/CONFIG.INI", SCARLETT_VFS_ACCESS_READ, fh, fsz, layer) ==
                  SCARLETT_OK &&
              fsz == orig_ini.size(),
          "Case-insensitive read via D:\\ synonym failed");
    check(vfs.close_file(fh) == SCARLETT_OK, "Close D:\\ handle failed");

    // CoW overwrite of existing file (case-mismatched path!)
    check(vfs.open_file("GAME:\\content\\data.pak",
                        SCARLETT_VFS_ACCESS_READ | SCARLETT_VFS_ACCESS_WRITE,
                        fh, fsz, layer) == SCARLETT_OK &&
              layer == SCARLETT_VFS_LAYER_OVERLAY,
          "CoW open for write must materialize file in overlay");
    const std::string patch = "COW!";
    uint32_t wrote = 0;
    check(vfs.write_file(fh, 0, patch.data(), static_cast<uint32_t>(patch.size()), wrote) ==
                  SCARLETT_OK &&
              wrote == patch.size(),
          "CoW partial write failed");
    check(vfs.close_file(fh) == SCARLETT_OK, "Close CoW handle failed");

    // Re-read and verify overlay patched bytes while preserving untouched tail
    check(vfs.open_file("GAME:\\Content\\Data.pak", SCARLETT_VFS_ACCESS_READ, fh, fsz, layer) ==
                  SCARLETT_OK &&
              layer == SCARLETT_VFS_LAYER_OVERLAY,
          "Read after CoW write must hit overlay");
    std::string cow_read(fsz, '\0');
    check(vfs.read_file(fh, 0, cow_read.data(), static_cast<uint32_t>(fsz), got) == SCARLETT_OK,
          "Read after CoW failed");
    check(cow_read == "COW!_PAK_PAYLOAD_0123456789", "CoW partial overwrite content mismatch");
    check(vfs.close_file(fh) == SCARLETT_OK, "Close handle failed");

    // Create new file in new directory
    check(vfs.create_directory("GAME:\\Saves") == SCARLETT_OK, "mkdir GAME:\\Saves failed");
    check(vfs.open_file("GAME:\\Saves\\Save0.bin",
                        SCARLETT_VFS_ACCESS_WRITE | SCARLETT_VFS_ACCESS_CREATE,
                        fh, fsz, layer) == SCARLETT_OK,
          "Create new overlay file failed");
    const std::string save_data = "SAVE_V1";
    check(vfs.write_file(fh, 0, save_data.data(), static_cast<uint32_t>(save_data.size()), wrote) ==
              SCARLETT_OK,
          "Write new overlay file failed");
    check(vfs.close_file(fh) == SCARLETT_OK, "Close new overlay file failed");

    // Whiteout deletion of base file
    check(vfs.remove_path("GAME:\\bin\\asset.bin") == SCARLETT_OK,
          "Deleting base file via whiteout failed");
    scarlett::loader::VfsStatResult st{};
    check(scarlett_failed(vfs.stat_path("GAME:\\Bin\\Asset.bin", st)),
          "Whiteout-deleted file must not exist in VFS");
    std::vector<scarlett::loader::VfsDirEntry> bin_entries;
    check(vfs.list_directory("GAME:\\Bin", bin_entries) == SCARLETT_OK && bin_entries.empty(),
          "Whiteout-deleted entry and .wh.* marker must not appear in directory listing");

    // Recreate whiteout-deleted file
    check(vfs.open_file("GAME:\\Bin\\Asset.bin",
                        SCARLETT_VFS_ACCESS_WRITE | SCARLETT_VFS_ACCESS_CREATE,
                        fh, fsz, layer) == SCARLETT_OK,
          "Recreating whiteout-deleted file failed");
    check(vfs.write_file(fh, 0, "REBORN", 6, wrote) == SCARLETT_OK, "Write recreated file failed");
    check(vfs.close_file(fh) == SCARLETT_OK, "Close recreated file failed");
    check(vfs.stat_path("GAME:\\Bin\\Asset.bin", st) == SCARLETT_OK && st.size_bytes == 6u,
          "Recreated file must be visible again");

    // Byte-for-byte check that read-only source game directory was NEVER modified
    auto disk_pak = read_bytes_file(base_dir / "Content" / "Data.pak");
    auto disk_ini = read_bytes_file(base_dir / "Content" / "Sub" / "Config.ini");
    auto disk_bin = read_bytes_file(base_dir / "Bin" / "Asset.bin");
    check(std::string(disk_pak.begin(), disk_pak.end()) == orig_pak,
          "Read-only base Data.pak was modified!");
    check(std::string(disk_ini.begin(), disk_ini.end()) == orig_ini,
          "Read-only base Config.ini was modified!");
    check(std::string(disk_bin.begin(), disk_bin.end()) == orig_bin,
          "Read-only base Asset.bin was modified!");
    check(!std::filesystem::exists(base_dir / "Saves"),
          "Overlay directory Saves leaked into read-only base!");

    vfs.unmount();
    std::filesystem::remove_all(root);
}

// ============================================================================
// Test 3: Synthetic XVD + 3-Block XVC + NTFS + Encrypted XVC Refusal
// ============================================================================

void test_xvd_xvc_ntfs_and_cleanroom() {
    const std::string small_msg = "Hello Resident Scarlett NTFS!";
    std::vector<uint8_t> stream_payload(3500u);
    for (size_t i = 0; i < stream_payload.size(); ++i) {
        stream_payload[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xFFu);
    }

    auto xvd_img = build_synthetic_xvd_image(false, false, false, small_msg, stream_payload);

    // 3a. Parse synthetic unencrypted XVD + 3-Block XVC + NTFS in memory
    scarlett::loader::XvdPackageReader reader;
    bool is_enc = false;
    check(reader.open_memory(xvd_img, is_enc) == SCARLETT_OK && !is_enc,
          "Unencrypted XVD+XVC+NTFS image must open cleanly");
    check(reader.header().format_version == 3u &&
              reader.header().user_data_length == 0x1000u &&
              reader.header().xvc_data_length > 0u &&
              reader.header().sandbox_id == "SCARLETT",
          "XVD header parsed fields mismatch");
    check(reader.xvc().present &&
              reader.xvc().regions.size() == 2u &&
              reader.xvc().chunks.size() == 3u &&
              reader.xvc().update_segments.size() == 2u &&
              reader.xvc().regions[0].specifier == "BaseGame" &&
              reader.xvc().regions[1].specifier == "HighResTex",
          "3-block XVC Intelligent Delivery descriptor mismatch");
    check(reader.has_ntfs(), "NTFS volume inside XVD must mount cleanly");

    std::vector<scarlett::loader::NtfsDirEntry> data_entries;
    check(reader.ntfs().list_directory("data", data_entries) == SCARLETT_OK &&
              data_entries.size() == 2u,
          "NTFS $INDEX_ALLOCATION directory listing mismatch");

    std::vector<uint8_t> got_small;
    check(reader.ntfs().read_entire_file("DATA\\small.txt", got_small) == SCARLETT_OK &&
              std::string(got_small.begin(), got_small.end()) == small_msg,
          "NTFS resident $DATA read mismatch");

    std::vector<uint8_t> got_stream;
    check(reader.ntfs().read_entire_file("data/STREAM.BIN", got_stream) == SCARLETT_OK &&
              got_stream == stream_payload,
          "NTFS non-resident multi-run $DATA read mismatch");

    // 3b. Mount XVD file via VFS + CoW overlay and verify XVD stays untouched
    auto tmp = make_temp_dir("xvd");
    auto xvd_path = tmp / "title.xvd";
    auto ov_path = tmp / "overlay";
    write_bytes_file(xvd_path, xvd_img.data(), xvd_img.size());

    scarlett::loader::GameCowVfs xvd_vfs;
    uint32_t vol_flags = 0;
    check(xvd_vfs.mount_xvd_package(xvd_path, ov_path, vol_flags, is_enc) == SCARLETT_OK &&
              !is_enc,
          "Mounting unencrypted XVD package into VFS failed");

    uint64_t fh = 0, fsz = 0;
    scarlett_vfs_layer_t layer = SCARLETT_VFS_LAYER_NONE;
    check(xvd_vfs.open_file("GAME:\\Data\\Stream.bin", SCARLETT_VFS_ACCESS_READ, fh, fsz, layer) ==
                  SCARLETT_OK &&
              layer == SCARLETT_VFS_LAYER_XVD_NTFS && fsz == stream_payload.size(),
          "Opening NTFS file inside mounted XVD VFS failed");
    std::vector<uint8_t> vfs_stream(fsz);
    uint32_t got = 0;
    check(xvd_vfs.read_file(fh, 0, vfs_stream.data(), static_cast<uint32_t>(fsz), got) ==
                  SCARLETT_OK &&
              vfs_stream == stream_payload,
          "Reading NTFS file via mounted XVD VFS failed");
    check(xvd_vfs.close_file(fh) == SCARLETT_OK, "Close XVD VFS file failed");

    // CoW write over XVD NTFS file goes to overlay only
    check(xvd_vfs.open_file("GAME:\\Data\\Small.txt",
                            SCARLETT_VFS_ACCESS_WRITE | SCARLETT_VFS_ACCESS_TRUNCATE,
                            fh, fsz, layer) == SCARLETT_OK &&
              layer == SCARLETT_VFS_LAYER_OVERLAY,
          "CoW write on XVD VFS must materialize in overlay");
    uint32_t wrote = 0;
    check(xvd_vfs.write_file(fh, 0, "OVERLAY_XVD", 11, wrote) == SCARLETT_OK,
          "Write to XVD CoW overlay failed");
    check(xvd_vfs.close_file(fh) == SCARLETT_OK, "Close XVD overlay handle failed");

    // Verify original XVD on disk is byte-for-byte identical
    check(read_bytes_file(xvd_path) == xvd_img, "Source XVD image on disk was modified!");
    xvd_vfs.unmount();

    // 3c. Negative Clean-Room tests: encrypted XVD / XVC must immediately return ENCRYPTED_PACKAGE_UNSUPPORTED
    struct EncCase {
        bool xvd_flag;
        bool xvc_key;
        bool xvc_reg;
        const char* label;
    } enc_cases[] = {
        {true, false, false, "XVD missing ENCRYPTION_DISABLED flag"},
        {false, true, false, "XVC non-zero key_count / primary_key_id"},
        {false, false, true, "XVC region encrypted flag"},
    };
    for (const auto& ec : enc_cases) {
        auto enc_img = build_synthetic_xvd_image(
            ec.xvd_flag, ec.xvc_key, ec.xvc_reg, small_msg, stream_payload);
        auto enc_path = tmp / "enc.xvd";
        write_bytes_file(enc_path, enc_img.data(), enc_img.size());

        scarlett_loader_mount_pkg_params_t mp{};
        std::string enc_str = enc_path.string();
        std::string ov_str = (tmp / "enc_ov").string();
        mp.package_path_utf8_addr = reinterpret_cast<uint64_t>(enc_str.c_str());
        mp.package_path_len = static_cast<uint32_t>(enc_str.size());
        mp.overlay_dir_utf8_addr = reinterpret_cast<uint64_t>(ov_str.c_str());
        mp.overlay_dir_len = static_cast<uint32_t>(ov_str.size());

        int32_t rc = scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_MOUNT_PACKAGE, &mp);
        check(rc == SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED &&
                  mp.out_status == SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED &&
                  mp.out_is_encrypted == 1u &&
                  mp.out_mount_handle == 0u,
              std::string("Encrypted package refusal failed for: ") + ec.label);
    }

    std::filesystem::remove_all(tmp);
}

// ============================================================================
// Test 4: xgameplatform API-Set Resolver, PSM AppNotify & Auto-Stub Thunk
// ============================================================================

struct PsmTestTracker {
    int calls = 0;
    uint8_t last_flag = 255u;
    std::vector<uint8_t> history;
};

void SCARLETT_WIN64_API on_app_state_changed(uint8_t quiesced, void* ctx) {
    auto* tr = static_cast<PsmTestTracker*>(ctx);
    ++tr->calls;
    tr->last_flag = quiesced;
    tr->history.push_back(quiesced);
}

void SCARLETT_WIN64_API on_constrained_changed(uint8_t constrained, void* ctx) {
    auto* tr = static_cast<PsmTestTracker*>(ctx);
    ++tr->calls;
    tr->last_flag = constrained;
    tr->history.push_back(constrained);
}

void test_xgameplatform_psm_and_stubs() {
    scarlett_loader_pe_set_unix_bridge(scarlett_loader_unix_dispatch);

    scarlett_unix_init_params_t init{};
    init.abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    init.module_id = SCARLETT_UNIX_MOD_LOADER;
    init.console_model = SCARLETT_MODEL_ANACONDA;
    init.strict_mode = 0u;
    init.shader_mode = SCARLETT_SHADER_MODE_ADAPTIVE;
    init.net_mode = SCARLETT_NET_MODE_OFFLINE;
    check(scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_INIT, &init) == SCARLETT_OK,
          "loader_unix init failed");

    // Load config into unixlib state so XGamePlatformGetTitleMetadata / PackageFullName work
    const std::string cfg_xml = R"XML(<Game configVersion="1">
  <Identity Name="Scarlett.ApiSetDemo" Publisher="CN=Scarlett" Version="1.2.3.4"/>
  <ExecutableList>
    <Executable Name="Demo.exe" TargetDeviceFamily="Scarlett"/>
  </ExecutableList>
  <ShellVisuals DefaultDisplayName="ApiSet Demo"/>
  <TitleId>CAFEBABE</TitleId>
</Game>)XML";
    scarlett_loader_config_params_t cp{};
    cp.xml_or_path_utf8_addr = reinterpret_cast<uint64_t>(cfg_xml.c_str());
    cp.input_len = static_cast<uint32_t>(cfg_xml.size());
    cp.is_xml_buffer = 1u;
    check(scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_LOAD_CONFIG, &cp) == SCARLETT_OK,
          "LOAD_CONFIG via unix call failed");

    scarlett_loader_title_metadata_t meta{};
    check(XGamePlatformGetTitleMetadata(&meta) == SCARLETT_OK &&
              meta.title_id == 0xCAFEBABEu && meta.is_scarlett == 1u,
          "XGamePlatformGetTitleMetadata mismatch");

    char pkg_name[256]{};
    check(XGamePlatformGetPackageFullName(pkg_name, sizeof(pkg_name)) == SCARLETT_OK &&
              std::string_view(pkg_name).find("Scarlett.ApiSetDemo_1.2.3.4_x64__") == 0u,
          "XGamePlatformGetPackageFullName mismatch");

    // API-Set Resolver tests
    char target[128]{};
    check(XGamePlatformResolveApiSet("api-ms-win-core-psm-appnotify-l1-1-0.dll",
                                     target, sizeof(target)) == SCARLETT_OK &&
              std::string_view(target) == "api-ms-win-core-psm-appnotify-l1-1-0.dll",
          "API-set resolve for psm-appnotify failed");
    check(XGamePlatformResolveApiSet("API-MS-WIN-GAMING-EXPANDEDRESOURCES-L1-1-0",
                                     target, sizeof(target)) == SCARLETT_OK &&
              std::string_view(target) == "kernelx.dll",
          "API-set resolve for expandedresources failed");
    check(XGamePlatformResolveApiSet("ext-ms-win-gaming-xbl-l1-1-0",
                                     target, sizeof(target)) == SCARLETT_OK &&
              std::string_view(target) == "xgameruntime.dll",
          "API-set resolve for xbl failed");
    check(XGamePlatformResolveApiSet("api-ms-win-core-unknown-l9-9-9",
                                     target, sizeof(target)) == SCARLETT_E_NOINTERFACE,
          "Unknown API-set must return SCARLETT_E_NOINTERFACE");

    // PSM AppNotify tests: Suspend / Resume / Constrained
    PsmTestTracker state_tracker;
    void* state_reg = nullptr;
    check(RegisterAppStateChangeNotification(on_app_state_changed, &state_tracker, &state_reg) == 0u &&
              state_reg != nullptr,
          "RegisterAppStateChangeNotification failed");

    PsmTestTracker constr_tracker;
    void* constr_reg = nullptr;
    check(RegisterAppConstrainedChangeNotification(
              on_constrained_changed, &constr_tracker, &constr_reg) == 0u &&
              constr_reg != nullptr,
          "RegisterAppConstrainedChangeNotification failed");

    check(ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_SUSPEND) == SCARLETT_OK,
          "Dispatch SUSPEND failed");
    check(ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_CONSTRAINED) == SCARLETT_OK,
          "Dispatch CONSTRAINED failed");
    check(ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_UNCONSTRAINED) == SCARLETT_OK,
          "Dispatch UNCONSTRAINED failed");
    check(ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_RESUME) == SCARLETT_OK,
          "Dispatch RESUME failed");

    check(state_tracker.calls == 2 &&
              state_tracker.history == std::vector<uint8_t>({1u, 0u}),
          "AppState Suspend(1)->Resume(0) delivery mismatch");
    check(constr_tracker.calls == 2 &&
              constr_tracker.history == std::vector<uint8_t>({1u, 0u}),
          "AppConstrained Constrained(1)->Unconstrained(0) delivery mismatch");

    UnregisterAppStateChangeNotification(state_reg);
    UnregisterAppConstrainedChangeNotification(constr_reg);
    check(ScarlettPsmDispatchEvent(SCARLETT_PSM_EVENT_SUSPEND) == SCARLETT_OK,
          "Dispatch after unregister failed");
    check(state_tracker.calls == 2,
          "Unregistered AppState callback must not be invoked again");

    // Diagnostic auto-stub thunk tests (strict=0)
    scarlett_hresult_t stub_hr = XGamePlatformCreateActivationContext(0x11u, 0x22u, 0x33u, 0x44u);
    check(scarlett_failed(stub_hr) && stub_hr == SCARLETT_ERR_UNIMPLEMENTED_STUB,
          "Unimplemented xgameplatform export must fail loudly via auto-stub thunk");

    scarlett_loader_last_stub_params_t last_stub{};
    check(scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_EX_GET_LAST_STUB, &last_stub) ==
                  SCARLETT_OK &&
              last_stub.out_has_stub == 1u &&
              std::string_view(last_stub.out_ctx.dll_name) == "xgameplatform.dll" &&
              std::string_view(last_stub.out_ctx.symbol_name) ==
                  "XGamePlatformCreateActivationContext" &&
              last_stub.out_ctx.rcx == 0x11u && last_stub.out_ctx.rdx == 0x22u &&
              last_stub.out_ctx.r8 == 0x33u && last_stub.out_ctx.r9 == 0x44u &&
              last_stub.out_ctx.caller_rva != 0u,
          "Auto-stub context capture mismatch for xgameplatform.dll");

    scarlett_hresult_t psm_stub_hr = RegisterAppStateChangeNotificationEx(1u, 2u, 3u, 4u);
    check(scarlett_failed(psm_stub_hr) && psm_stub_hr == SCARLETT_ERR_UNIMPLEMENTED_STUB,
          "Unimplemented PSM export must fail loudly via auto-stub thunk");

    // Strict mode (PROTON_XBOX_STRICT=1) deterministic abort test in forked child
    pid_t pid = ::fork();
    if (pid == 0) {
        ::setenv(SCARLETT_ENV_STRICT, "1", 1);
        scarlett_unix_init_params_t strict_init = init;
        strict_init.strict_mode = 1u;
        scarlett_loader_unix_dispatch(SCARLETT_LOADER_UNIX_INIT, &strict_init);
        (void)XGamePlatformCreateActivationContext(0xDEADu, 0xBEEFu, 0u, 0u);
        ::_exit(0);
    }
    int wstatus = 0;
    check(::waitpid(pid, &wstatus, 0) == pid && WIFSIGNALED(wstatus),
          "PROTON_XBOX_STRICT=1 must deterministically trap/abort on stub invocation");
}

} // namespace

int main() {
    test_microsoft_game_config();
    test_vfs_cow_overlay();
    test_xvd_xvc_ntfs_and_cleanroom();
    test_xgameplatform_psm_and_stubs();

    if (g_failures != 0) {
        std::cerr << "[FAIL] test_loader_vfs failed with " << g_failures << " error(s).\n";
        return EXIT_FAILURE;
    }

    std::cout << "[PASS] All Scarlett Loader, VFS CoW, XVD/XVC/NTFS, API-set and PSM AppNotify tests passed.\n";
    return EXIT_SUCCESS;
}
