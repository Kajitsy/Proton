/*
 * Proton-Scarlett — Clean-Room Unencrypted XVD, 3-Block XVC & NTFS Format Definitions
 *
 * Strictly Clean-Room inspection layout:
 *   - No cryptographic keys, no PlayReady/CIK material, no DRM bypass.
 *   - Any encrypted volume or non-zero key/encryption flag triggers immediate
 *     refusal with SCARLETT_ERR_ENCRYPTED_PACKAGE_UNSUPPORTED.
 */

#ifndef SCARLETT_XVD_XVC_FORMAT_H
#define SCARLETT_XVD_XVC_FORMAT_H

#include <stdint.h>

#include "scarlett_abi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * XVD Volume Flags & Header Layout (0x000..0x200 signature, 0x200..0x400 header)
 * ============================================================================ */

#define SCARLETT_XVD_PAGE_SIZE                  UINT32_C(0x1000)
#define SCARLETT_XVD_HEADER_SIZE                UINT32_C(0x0200)
#define SCARLETT_XVD_MIN_IMAGE_SIZE             (SCARLETT_XVD_SIGNATURE_SIZE + SCARLETT_XVD_HEADER_SIZE)

/* VolumeFlags bits (public XVD container specification) */
#define SCARLETT_XVD_VOLUME_FLAG_DATA_INTEGRITY_DISABLED UINT32_C(0x00000004)
#define SCARLETT_XVD_VOLUME_FLAG_LEGACY_SECTOR_MODE      UINT32_C(0x00000008)
#define SCARLETT_XVD_VOLUME_FLAG_ENCRYPTED_EXPLICIT      UINT32_C(0x00000010)
#define SCARLETT_XVD_VOLUME_FLAG_RESILIENCY_ENABLED      UINT32_C(0x00000020)

/* XVC Magic & Flags */
#define SCARLETT_XVC_INFO_MAGIC                 UINT32_C(0x49435658) /* "XVCI" */
#define SCARLETT_XVC_REGION_FLAG_ENCRYPTED      UINT32_C(0x00000001)
#define SCARLETT_XVC_REGION_FLAG_RESIDENT       UINT32_C(0x00000002)
#define SCARLETT_XVC_REGION_FLAG_INITIAL_PLAY   UINT32_C(0x00000004)
#define SCARLETT_XVC_REGION_FLAG_REQUIRED       UINT32_C(0x00000008)

/* ============================================================================
 * XVD Header (512 bytes at offset SCARLETT_XVD_HEADER_OFFSET == 0x200)
 * ============================================================================ */

typedef struct __attribute__((packed)) scarlett_xvd_raw_header_s {
    uint64_t magic;                  /* 0x000: "msft-xvd" (0x6476782D7466736D LE) */
    uint32_t volume_flags;           /* 0x008: VolumeFlags */
    uint32_t format_version;         /* 0x00C: FormatVersion (typically 3) */
    uint64_t file_time;              /* 0x010: Creation FILETIME */
    uint64_t drive_size;             /* 0x018: Virtual drive size in bytes */
    uint8_t  vd_uid[16];             /* 0x020: Drive GUID */
    uint8_t  content_id[16];         /* 0x030: Content ID GUID */
    uint8_t  user_data_hash[32];     /* 0x040: User data SHA-256 */
    uint8_t  xvc_data_hash[32];      /* 0x060: XVC data SHA-256 */
    uint32_t sequence_number;        /* 0x080: Sequence / unknown */
    uint32_t xvd_type;               /* 0x084: 0 = Fixed, 1 = Dynamic */
    uint32_t content_type;           /* 0x088: ContentType */
    uint32_t embedded_xvd_length;    /* 0x08C: Embedded XVD bytes */
    uint32_t user_data_length;       /* 0x090: UserData bytes */
    uint32_t xvc_data_length;        /* 0x094: XVC metadata bytes */
    uint32_t dyn_header_length;      /* 0x098: Dynamic header bytes */
    uint32_t block_size;             /* 0x09C: Block size */
    uint8_t  sandbox_id[16];         /* 0x0A0: Sandbox ID */
    uint8_t  reserved[0x150];        /* 0x0B0..0x1FF */
} scarlett_xvd_raw_header_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xvd_raw_header_t) == 0x200,
                       "scarlett_xvd_raw_header_t must be exactly 512 bytes");

/* ============================================================================
 * 3-Block XVC Intelligent Delivery Descriptor
 *   Block 1: XVC Header (Content ID, key count / key IDs, block counts)
 *   Block 2: Region Table & Chunk Table
 *   Block 3: Update Segment Metadata Table
 * ============================================================================ */

typedef struct __attribute__((packed)) scarlett_xvc_raw_block1_header_s {
    uint32_t magic;                  /* SCARLETT_XVC_INFO_MAGIC ("XVCI") */
    uint32_t version;                /* XVC format version */
    uint8_t  content_id[16];         /* Content ID GUID */
    uint32_t key_count;              /* Must be 0 for clean-room unencrypted XVC */
    uint32_t encryption_flags;       /* Must be 0 for clean-room unencrypted XVC */
    uint32_t region_count;           /* Number of entries in Block 2 Region Table */
    uint32_t chunk_count;            /* Number of entries in Block 2 Chunk Table */
    uint32_t update_segment_count;   /* Number of entries in Block 3 Update Segments */
    uint32_t chunk_size_bytes;       /* Default Intelligent Delivery chunk size */
    uint8_t  primary_key_id[16];     /* Must be all-zero for unencrypted XVC */
    uint8_t  reserved[16];
} scarlett_xvc_raw_block1_header_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xvc_raw_block1_header_t) == 80,
                       "scarlett_xvc_raw_block1_header_t must be 80 bytes");

typedef struct __attribute__((packed)) scarlett_xvc_raw_region_entry_s {
    uint32_t region_id;
    uint32_t flags;                  /* SCARLETT_XVC_REGION_FLAG_* */
    uint16_t key_id_index;           /* Must be 0 / 0xFFFF (unencrypted) when unencrypted */
    uint16_t padding;
    uint32_t first_chunk_index;
    uint32_t chunk_count;
    uint32_t reserved;
    uint64_t offset;                 /* Byte offset in backing volume */
    uint64_t length;                 /* Byte length of region */
    uint64_t hash;                   /* Region digest / tag */
    char     specifier[64];          /* Language / feature / tag specifier */
} scarlett_xvc_raw_region_entry_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xvc_raw_region_entry_t) == 112,
                       "scarlett_xvc_raw_region_entry_t must be 112 bytes");

typedef struct __attribute__((packed)) scarlett_xvc_raw_chunk_entry_s {
    uint32_t chunk_id;
    uint32_t region_id;
    uint64_t offset;
    uint64_t length;
    uint32_t flags;
    uint32_t reserved;
} scarlett_xvc_raw_chunk_entry_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xvc_raw_chunk_entry_t) == 32,
                       "scarlett_xvc_raw_chunk_entry_t must be 32 bytes");

typedef struct __attribute__((packed)) scarlett_xvc_raw_update_segment_s {
    uint32_t segment_id;
    uint32_t flags;
    uint64_t target_offset;
    uint64_t length;
    uint64_t base_version;
    uint8_t  segment_hash[32];
} scarlett_xvc_raw_update_segment_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xvc_raw_update_segment_t) == 64,
                       "scarlett_xvc_raw_update_segment_t must be 64 bytes");

/* ============================================================================
 * Read-Only NTFS On-Disk Structures (VBR/BPB, MFT Record, Attributes, INDX)
 * ============================================================================ */

#define SCARLETT_NTFS_OEM_ID            UINT64_C(0x202020205346544E) /* "NTFS    " */
#define SCARLETT_NTFS_MFT_MAGIC         UINT32_C(0x454C4946)         /* "FILE" */
#define SCARLETT_NTFS_INDX_MAGIC        UINT32_C(0x58444E49)         /* "INDX" */

#define SCARLETT_NTFS_ATTR_STANDARD_INFO    UINT32_C(0x10)
#define SCARLETT_NTFS_ATTR_ATTRIBUTE_LIST   UINT32_C(0x20)
#define SCARLETT_NTFS_ATTR_FILE_NAME        UINT32_C(0x30)
#define SCARLETT_NTFS_ATTR_DATA             UINT32_C(0x80)
#define SCARLETT_NTFS_ATTR_INDEX_ROOT       UINT32_C(0x90)
#define SCARLETT_NTFS_ATTR_INDEX_ALLOCATION UINT32_C(0xA0)
#define SCARLETT_NTFS_ATTR_END              UINT32_C(0xFFFFFFFF)

#define SCARLETT_NTFS_MFT_FLAG_IN_USE       UINT16_C(0x0001)
#define SCARLETT_NTFS_MFT_FLAG_DIRECTORY    UINT16_C(0x0002)

#define SCARLETT_NTFS_INDEX_ENTRY_SUBNODE   UINT16_C(0x0001)
#define SCARLETT_NTFS_INDEX_ENTRY_LAST      UINT16_C(0x0002)

#define SCARLETT_NTFS_ROOT_MFT_RECORD       UINT64_C(5)

typedef struct __attribute__((packed)) scarlett_ntfs_vbr_s {
    uint8_t  jump_boot[3];
    uint64_t oem_id;                 /* "NTFS    " */
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  unused0[3];
    uint16_t unused1;
    uint8_t  media_descriptor;
    uint16_t unused2;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_sectors;
    uint32_t unused3;
    uint32_t unused4;
    uint64_t total_sectors;
    uint64_t mft_lcn;
    uint64_t mft_mirr_lcn;
    int8_t   clusters_per_file_record;
    uint8_t  unused5[3];
    int8_t   clusters_per_index_block;
    uint8_t  unused6[3];
    uint64_t volume_serial;
    uint32_t checksum;
    uint8_t  boot_code[426];
    uint16_t end_of_sector_marker;   /* 0xAA55 */
} scarlett_ntfs_vbr_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_vbr_t) == 512,
                       "scarlett_ntfs_vbr_t must be 512 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_mft_record_hdr_s {
    uint32_t magic;                  /* "FILE" */
    uint16_t usa_offset;             /* Update Sequence Array offset */
    uint16_t usa_count;              /* Number of 16-bit entries in USA */
    uint64_t lsn;
    uint16_t sequence_number;
    uint16_t link_count;
    uint16_t attrs_offset;
    uint16_t flags;
    uint32_t bytes_in_use;
    uint32_t bytes_allocated;
    uint64_t base_mft_record;
    uint16_t next_attr_id;
    uint16_t align_pad;
    uint32_t mft_record_number;
} scarlett_ntfs_mft_record_hdr_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_mft_record_hdr_t) == 48,
                       "scarlett_ntfs_mft_record_hdr_t must be 48 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_attr_hdr_s {
    uint32_t type;
    uint32_t length;
    uint8_t  non_resident;
    uint8_t  name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t attr_id;
} scarlett_ntfs_attr_hdr_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_attr_hdr_t) == 16,
                       "scarlett_ntfs_attr_hdr_t must be 16 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_attr_resident_s {
    scarlett_ntfs_attr_hdr_t common;
    uint32_t value_length;
    uint16_t value_offset;
    uint8_t  resident_flags;
    uint8_t  reserved;
} scarlett_ntfs_attr_resident_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_attr_resident_s) == 24,
                       "scarlett_ntfs_attr_resident_t must be 24 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_attr_nonresident_s {
    scarlett_ntfs_attr_hdr_t common;
    uint64_t lowest_vcn;
    uint64_t highest_vcn;
    uint16_t mapping_pairs_offset;
    uint8_t  compression_unit;
    uint8_t  reserved[5];
    uint64_t allocated_size;
    uint64_t data_size;
    uint64_t initialized_size;
} scarlett_ntfs_attr_nonresident_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_attr_nonresident_s) == 64,
                       "scarlett_ntfs_attr_nonresident_t must be 64 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_index_header_s {
    uint32_t entries_offset;
    uint32_t index_length;
    uint32_t allocated_size;
    uint8_t  flags;
    uint8_t  reserved[3];
} scarlett_ntfs_index_header_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_index_header_s) == 16,
                       "scarlett_ntfs_index_header_t must be 16 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_index_root_s {
    uint32_t indexed_attr_type;
    uint32_t collation_rule;
    uint32_t index_block_size;
    uint8_t  clusters_per_index_block;
    uint8_t  reserved[3];
    scarlett_ntfs_index_header_t header;
} scarlett_ntfs_index_root_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_index_root_s) == 32,
                       "scarlett_ntfs_index_root_t must be 32 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_indx_block_hdr_s {
    uint32_t magic;                  /* "INDX" */
    uint16_t usa_offset;
    uint16_t usa_count;
    uint64_t lsn;
    uint64_t vcn;
    scarlett_ntfs_index_header_t header;
} scarlett_ntfs_indx_block_hdr_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_indx_block_hdr_t) == 40,
                       "scarlett_ntfs_indx_block_hdr_t must be 40 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_index_entry_hdr_s {
    uint64_t mft_reference;          /* Low 48 bits = MFT record index */
    uint16_t entry_length;
    uint16_t key_length;
    uint16_t flags;                  /* SCARLETT_NTFS_INDEX_ENTRY_* */
    uint16_t reserved;
} scarlett_ntfs_index_entry_hdr_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_index_entry_hdr_t) == 16,
                       "scarlett_ntfs_index_entry_hdr_t must be 16 bytes");

typedef struct __attribute__((packed)) scarlett_ntfs_file_name_attr_s {
    uint64_t parent_directory;       /* Low 48 bits = parent MFT record */
    uint64_t creation_time;
    uint64_t modification_time;
    uint64_t mft_change_time;
    uint64_t last_access_time;
    uint64_t allocated_size;
    uint64_t data_size;
    uint32_t file_attributes;
    uint32_t reparse_point_tag;
    uint8_t  name_length;            /* In UTF-16 code units */
    uint8_t  name_type;              /* 0=POSIX, 1=Win32, 2=DOS, 3=Win32&DOS */
    /* Followed by name_length UTF-16LE uint16_t characters */
} scarlett_ntfs_file_name_attr_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_ntfs_file_name_attr_t) == 66,
                       "scarlett_ntfs_file_name_attr_t must be 66 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_XVD_XVC_FORMAT_H */
