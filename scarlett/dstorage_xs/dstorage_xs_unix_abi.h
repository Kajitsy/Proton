/*
 * Proton-Scarlett — DirectStorage_XS ABI Contract (dstorage_xs.dll <-> dstorage_xs_unix.so)
 *
 * Clean-room, freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLL: dstorage_xs.dll (x86_64-windows, Microsoft x64 ABI)
 *   - Linux native unixlib: dstorage_xs_unix.so (x86_64-unix, System V AMD64 ABI)
 *   - Host C++20 verification tests
 *
 * Strictly zero dependencies on <windows.h>, libc, POSIX, <liburing.h>,
 * <libdeflate.h>, <zlib-ng.h>, or <vulkan/vulkan.h>.
 */

#ifndef SCARLETT_DSTORAGE_XS_UNIX_ABI_H
#define SCARLETT_DSTORAGE_XS_UNIX_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__MINGW32__)
#define SCARLETT_DSTORAGEXS_CALL __stdcall
#if defined(__GNUC__) || defined(__clang__)
#define SCARLETT_DSTORAGEXS_API __attribute__((visibility("default")))
#else
#define SCARLETT_DSTORAGEXS_API __declspec(dllexport)
#endif
#else
#define SCARLETT_DSTORAGEXS_CALL
#if defined(__GNUC__) || defined(__clang__)
#define SCARLETT_DSTORAGEXS_API __attribute__((visibility("default")))
#else
#define SCARLETT_DSTORAGEXS_API
#endif
#endif

/* ============================================================================
 * DirectStorage_XS Constants & Limits
 * ============================================================================ */

#define SCARLETT_DSTORAGE_MAX_REQUEST_BYTES       (UINT32_C(32) * UINT32_C(1024) * UINT32_C(1024)) /* 32 MiB */
#define SCARLETT_DSTORAGE_MAX_STAGING_BYTES       (UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024)) /* 1 GiB */
#define SCARLETT_DSTORAGE_DEFAULT_STAGING_BYTES   (UINT64_C(64) * UINT64_C(1024) * UINT64_C(1024))   /* 64 MiB */
#define SCARLETT_DSTORAGE_TILE_BYTES              UINT32_C(65536) /* 64 KiB physical page / tile */
#define SCARLETT_DSTORAGE_MIN_QUEUE_CAPACITY      UINT32_C(2)
#define SCARLETT_DSTORAGE_MAX_QUEUE_CAPACITY      UINT32_C(16384)
#define SCARLETT_DSTORAGE_PRIORITY_COUNT          UINT32_C(4)

/* Tiled Zlib / GDeflate container header magic ("SGD1" little-endian) */
#define SCARLETT_DSTORAGE_GDEFLATE_MAGIC          UINT32_C(0x31444753)

/* Clean-room BCPack container header magic ("BCPK" little-endian) */
#define SCARLETT_DSTORAGE_BCPACK_MAGIC            UINT32_C(0x4B504342)

/* ============================================================================
 * Bit-Exact 64-bit DSTORAGE_REQUEST_OPTIONS & DSTORAGE_REQUEST
 * ============================================================================ */

typedef enum DSTORAGE_REQUEST_SOURCE_TYPE_E {
    DSTORAGE_REQUEST_SOURCE_FILE   = 0,
    DSTORAGE_REQUEST_SOURCE_MEMORY = 1
} DSTORAGE_REQUEST_SOURCE_TYPE;

typedef enum DSTORAGE_PRIORITY_E {
    DSTORAGE_PRIORITY_LOW      = 0,
    DSTORAGE_PRIORITY_NORMAL   = 1,
    DSTORAGE_PRIORITY_HIGH     = 2,
    DSTORAGE_PRIORITY_REALTIME = 3
} DSTORAGE_PRIORITY;

typedef enum DSTORAGE_COMMAND_TYPE_E {
    DSTORAGE_COMMAND_TYPE_NONE    = 0,
    DSTORAGE_COMMAND_TYPE_REQUEST = 1,
    DSTORAGE_COMMAND_TYPE_STATUS  = 2,
    DSTORAGE_COMMAND_TYPE_SIGNAL  = 3
} DSTORAGE_COMMAND_TYPE;

/*
 * Xbox Velocity Compute-Swizzle Modes (16-bit field in DSTORAGE_REQUEST_OPTIONS)
 */
typedef enum DSTORAGE_SWIZZLE_MODE_E {
    DSTORAGE_SWIZZLE_NONE              = 0,
    DSTORAGE_SWIZZLE_4K_4K_8K          = 1,
    DSTORAGE_SWIZZLE_4K_4K_24K         = 2,
    DSTORAGE_SWIZZLE_4K_12K_4K_4K_8K   = 3,
    DSTORAGE_SWIZZLE_4K_12K_4K_12K     = 4
} DSTORAGE_SWIZZLE_MODE;

/*
 * Exact 64-bit layout matching dstorage_xs.xdl & scarlett_abi_common.h:
 *   Bit  0      (1 bit)  : ZlibDecompress
 *   Bits 1..4   (4 bits) : BcpackMode
 *   Bits 5..20  (16 bits): SwizzleMode
 *   Bit  21     (1 bit)  : DestinationIsPhysicalPages
 *   Bit  22     (1 bit)  : SourceType (0 = File, 1 = Memory)
 *   Bit  23     (1 bit)  : SourceIsPhysicalPages
 *   Bits 24..63 (40 bits): Reserved (must be 0)
 */
typedef union DSTORAGE_REQUEST_OPTIONS_U {
    struct {
        uint64_t ZlibDecompress             : 1;
        uint64_t BcpackMode                 : 4;
        uint64_t SwizzleMode                : 16;
        uint64_t DestinationIsPhysicalPages : 1;
        uint64_t SourceType                 : 1;
        uint64_t SourceIsPhysicalPages      : 1;
        uint64_t Reserved                   : 40;
    };
    uint64_t AsUInt64;
} DSTORAGE_REQUEST_OPTIONS;

SCARLETT_STATIC_ASSERT(sizeof(DSTORAGE_REQUEST_OPTIONS) == 8,
                       "DSTORAGE_REQUEST_OPTIONS must be exactly 64 bits");

typedef struct DSTORAGE_QUEUE_DESC_S {
    uint64_t SourceType;  /* offset  0, size 8 */
    uint16_t Capacity;    /* offset  8, size 2 */
    int8_t   Priority;    /* offset 10, size 1 (DSTORAGE_PRIORITY) */
    uint8_t  Reserved[5]; /* offset 11, size 5 */
    const char *Name;     /* offset 16, size 8 */
    void *Device;         /* offset 24, size 8 */
} DSTORAGE_QUEUE_DESC;

SCARLETT_STATIC_ASSERT(sizeof(DSTORAGE_QUEUE_DESC) == 32,
                       "DSTORAGE_QUEUE_DESC must be 32 bytes");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_QUEUE_DESC, Capacity) == 8,
                       "DSTORAGE_QUEUE_DESC::Capacity offset must be 8");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_QUEUE_DESC, Priority) == 10,
                       "DSTORAGE_QUEUE_DESC::Priority offset must be 10");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_QUEUE_DESC, Name) == 16,
                       "DSTORAGE_QUEUE_DESC::Name offset must be 16");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_QUEUE_DESC, Device) == 24,
                       "DSTORAGE_QUEUE_DESC::Device offset must be 24");

typedef struct DSTORAGE_REQUEST_S {
    DSTORAGE_REQUEST_OPTIONS Options;   /* offset  0, size 8 */
    uint64_t FileHandle;                /* offset  8, size 8 */
    uint64_t FileOffset;                /* offset 16, size 8 */
    uint64_t SourceAddress;             /* offset 24, size 8 */
    uint32_t SourceSize;                /* offset 32, size 4 */
    uint32_t DestinationSize;           /* offset 36, size 4 */
    uint64_t DestinationAddress;        /* offset 40, size 8 */
    uint64_t CancellationTag;           /* offset 48, size 8 */
    const char *Name;                   /* offset 56, size 8 */
    uint32_t SwizzleSubresourceIndex;   /* offset 64, size 4 */
    uint32_t SwizzleReserved;           /* offset 68, size 4 */
} DSTORAGE_REQUEST;

SCARLETT_STATIC_ASSERT(sizeof(DSTORAGE_REQUEST) == 72,
                       "DSTORAGE_REQUEST must be exactly 72 bytes");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, Options) == 0, "Options offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, FileHandle) == 8, "FileHandle offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, FileOffset) == 16, "FileOffset offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, SourceAddress) == 24, "SourceAddress offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, SourceSize) == 32, "SourceSize offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, DestinationSize) == 36, "DestinationSize offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, DestinationAddress) == 40, "DestinationAddress offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, CancellationTag) == 48, "CancellationTag offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, Name) == 56, "Name offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, SwizzleSubresourceIndex) == 64, "SwizzleSubresourceIndex offset");
SCARLETT_STATIC_ASSERT(offsetof(DSTORAGE_REQUEST, SwizzleReserved) == 68, "SwizzleReserved offset");

typedef struct DSTORAGE_QUEUE_INFO_S {
    DSTORAGE_QUEUE_DESC Desc;
    uint32_t EmptySlotCount;
    uint32_t RequestCountUntilAutoSubmit;
    uint32_t PendingRequestCount;
    uint32_t AutoSubmitCount;
    uint64_t TotalSubmittedRequests;
    uint64_t TotalCompletedRequests;
    uint64_t TotalCancelledRequests;
} DSTORAGE_QUEUE_INFO;

typedef struct DSTORAGE_FILE_INFO_S {
    uint64_t FileSizeBytes;
    uint32_t OpenFlags;
    uint32_t Reserved;
} DSTORAGE_FILE_INFO;

SCARLETT_STATIC_ASSERT(sizeof(DSTORAGE_FILE_INFO) == 16,
                       "DSTORAGE_FILE_INFO must be 16 bytes");

typedef struct DSTORAGE_ERROR_FIRST_FAILURE_S {
    scarlett_hresult_t HResult;
    uint32_t CommandType;
    DSTORAGE_REQUEST Request;
} DSTORAGE_ERROR_FIRST_FAILURE;

SCARLETT_STATIC_ASSERT(sizeof(DSTORAGE_ERROR_FIRST_FAILURE) == 80,
                       "DSTORAGE_ERROR_FIRST_FAILURE must be 80 bytes");

typedef struct DSTORAGE_ERROR_RECORD_S {
    uint32_t FailureCount;
    uint32_t Reserved;
    DSTORAGE_ERROR_FIRST_FAILURE FirstFailure;
} DSTORAGE_ERROR_RECORD;

SCARLETT_STATIC_ASSERT(sizeof(DSTORAGE_ERROR_RECORD) == 88,
                       "DSTORAGE_ERROR_RECORD must be 88 bytes");

/* ============================================================================
 * Tiled GDeflate & Clean-Room BCPack Header Structs
 * ============================================================================ */

typedef struct scarlett_gdeflate_tile_desc_s {
    uint32_t compressed_offset;
    uint32_t compressed_size;
    uint32_t uncompressed_offset;
    uint32_t uncompressed_size;
} scarlett_gdeflate_tile_desc_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_gdeflate_tile_desc_t) == 16,
                       "scarlett_gdeflate_tile_desc_t must be 16 bytes");

typedef struct scarlett_gdeflate_header_s {
    uint32_t magic;                 /* SCARLETT_DSTORAGE_GDEFLATE_MAGIC */
    uint32_t tile_count;
    uint32_t tile_size;             /* 65536 */
    uint32_t total_uncompressed_bytes;
} scarlett_gdeflate_header_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_gdeflate_header_t) == 16,
                       "scarlett_gdeflate_header_t must be 16 bytes");

typedef struct scarlett_bcpack_header_s {
    uint32_t magic;                 /* SCARLETT_DSTORAGE_BCPACK_MAGIC */
    uint16_t version;
    uint16_t bcpack_mode;
    uint32_t uncompressed_size;
    uint32_t compressed_payload_size;
    uint32_t payload_crc32;
    uint32_t flags;
} scarlett_bcpack_header_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_bcpack_header_t) == 24,
                       "scarlett_bcpack_header_t must be 24 bytes");

/* ============================================================================
 * Extended dstorage_xs_unix.so Opcodes (0..7 match scarlett_unix_call.h verbatim)
 * ============================================================================ */

typedef enum scarlett_dstorage_unix_ext_op_e {
    SCARLETT_DSTORAGE_UNIX_ENQUEUE_REQUEST       = 8,
    SCARLETT_DSTORAGE_UNIX_ENQUEUE_STATUS        = 9,
    SCARLETT_DSTORAGE_UNIX_ENQUEUE_SIGNAL        = 10,
    SCARLETT_DSTORAGE_UNIX_SUBMIT_QUEUE          = 11,
    SCARLETT_DSTORAGE_UNIX_CANCEL_TAG            = 12,
    SCARLETT_DSTORAGE_UNIX_QUERY_QUEUE           = 13,
    SCARLETT_DSTORAGE_UNIX_CREATE_STATUS_ARRAY   = 14,
    SCARLETT_DSTORAGE_UNIX_DESTROY_STATUS_ARRAY  = 15,
    SCARLETT_DSTORAGE_UNIX_QUERY_STATUS          = 16,
    SCARLETT_DSTORAGE_UNIX_GET_FILE_INFO         = 17,
    SCARLETT_DSTORAGE_UNIX_RETRIEVE_ERROR_RECORD = 18,
    SCARLETT_DSTORAGE_UNIX_SET_STAGING_SIZE      = 19,
    SCARLETT_DSTORAGE_UNIX_QUERY_CAPS            = 20,
    SCARLETT_DSTORAGE_UNIX_SUBMIT_BATCH          = 21,
    SCARLETT_DSTORAGE_UNIX_REPORT_STUB           = 22,
    SCARLETT_DSTORAGE_UNIX_WAIT_SIGNALS          = 23,
    SCARLETT_DSTORAGE_UNIX_EXT_OP_COUNT          = 24
} scarlett_dstorage_unix_ext_op_t;

/* ============================================================================
 * Cross-Boundary Parameter Structs (Fixed-Width, MS x64 == SysV AMD64)
 * ============================================================================ */

/* Opcode 1: SCARLETT_DSTORAGE_UNIX_OPEN_FILE */
typedef struct scarlett_dstorage_open_file_params_s {
    uint64_t path_utf8_addr;
    uint32_t path_utf8_len;
    uint32_t open_flags;
    uint64_t out_file_handle;
    uint64_t out_file_size;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_open_file_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_open_file_params_t) == 40,
                       "scarlett_dstorage_open_file_params_t must be 40 bytes");

/* Opcode 2: SCARLETT_DSTORAGE_UNIX_CLOSE_FILE */
typedef struct scarlett_dstorage_close_file_params_s {
    uint64_t file_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_close_file_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_close_file_params_t) == 16,
                       "scarlett_dstorage_close_file_params_t must be 16 bytes");

/* Opcode 3: SCARLETT_DSTORAGE_UNIX_CREATE_QUEUE */
typedef struct scarlett_dstorage_create_queue_params_s {
    uint64_t source_type;
    uint32_t capacity;
    uint32_t priority;
    uint64_t name_utf8_addr;
    uint32_t name_utf8_len;
    uint32_t reserved0;
    uint64_t out_queue_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved1;
} scarlett_dstorage_create_queue_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_create_queue_params_t) == 48,
                       "scarlett_dstorage_create_queue_params_t must be 48 bytes");

/* Opcode 4: SCARLETT_DSTORAGE_UNIX_DESTROY_QUEUE */
typedef struct scarlett_dstorage_destroy_queue_params_s {
    uint64_t queue_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_destroy_queue_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_destroy_queue_params_t) == 16,
                       "scarlett_dstorage_destroy_queue_params_t must be 16 bytes");

/* Opcode 8: SCARLETT_DSTORAGE_UNIX_ENQUEUE_REQUEST */
typedef struct scarlett_dstorage_enqueue_request_params_s {
    uint64_t queue_handle;
    uint64_t file_handle;
    uint64_t file_offset;
    uint64_t src_addr;
    uint64_t dst_addr;
    uint64_t cancellation_tag;
    uint64_t name_utf8_addr;
    scarlett_dstorage_request_options_t options;
    uint32_t src_size;
    uint32_t dst_size;
    uint32_t swizzle_subresource_index;
    uint32_t swizzle_reserved;
    uint32_t out_auto_submitted;
    scarlett_hresult_t out_status;
} scarlett_dstorage_enqueue_request_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_enqueue_request_params_t) == 88,
                       "scarlett_dstorage_enqueue_request_params_t must be 88 bytes");

/* Opcode 9: SCARLETT_DSTORAGE_UNIX_ENQUEUE_STATUS */
typedef struct scarlett_dstorage_enqueue_status_params_s {
    uint64_t queue_handle;
    uint64_t status_array_handle;
    uint32_t index;
    uint32_t out_auto_submitted;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_enqueue_status_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_enqueue_status_params_t) == 32,
                       "scarlett_dstorage_enqueue_status_params_t must be 32 bytes");

/* Opcode 10: SCARLETT_DSTORAGE_UNIX_ENQUEUE_SIGNAL */
typedef struct scarlett_dstorage_enqueue_signal_params_s {
    uint64_t queue_handle;
    uint64_t fence_or_event_addr;
    uint64_t value;
    uint32_t is_event; /* 0 = uint64_t* fence/atomic counter, 1 = atomic uint32_t event */
    uint32_t out_auto_submitted;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_enqueue_signal_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_enqueue_signal_params_t) == 40,
                       "scarlett_dstorage_enqueue_signal_params_t must be 40 bytes");

/* Opcode 11: SCARLETT_DSTORAGE_UNIX_SUBMIT_QUEUE */
typedef struct scarlett_dstorage_submit_queue_params_s {
    uint64_t queue_handle;
    uint32_t wait_for_idle; /* 0 = async dispatch, 1 = drain until completed */
    scarlett_hresult_t out_status;
} scarlett_dstorage_submit_queue_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_submit_queue_params_t) == 16,
                       "scarlett_dstorage_submit_queue_params_t must be 16 bytes");

/* Opcode 12: SCARLETT_DSTORAGE_UNIX_CANCEL_TAG */
typedef struct scarlett_dstorage_cancel_tag_params_s {
    uint64_t queue_handle;
    uint64_t mask;
    uint64_t value;
    uint32_t out_cancelled_count;
    scarlett_hresult_t out_status;
} scarlett_dstorage_cancel_tag_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_cancel_tag_params_t) == 32,
                       "scarlett_dstorage_cancel_tag_params_t must be 32 bytes");

/* Opcode 13: SCARLETT_DSTORAGE_UNIX_QUERY_QUEUE */
typedef struct scarlett_dstorage_query_queue_params_s {
    uint64_t queue_handle;
    uint64_t out_source_type;
    uint32_t out_capacity;
    uint32_t out_priority;
    uint32_t out_empty_slots;
    uint32_t out_until_auto_submit;
    uint32_t out_pending_count;
    uint32_t out_auto_submit_count;
    uint64_t out_submitted_requests;
    uint64_t out_completed_requests;
    uint64_t out_cancelled_requests;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_query_queue_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_query_queue_params_t) == 72,
                       "scarlett_dstorage_query_queue_params_t must be 72 bytes");

/* Opcode 14: SCARLETT_DSTORAGE_UNIX_CREATE_STATUS_ARRAY */
typedef struct scarlett_dstorage_create_status_params_s {
    uint32_t capacity;
    uint32_t reserved;
    uint64_t out_status_array_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved1;
} scarlett_dstorage_create_status_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_create_status_params_t) == 24,
                       "scarlett_dstorage_create_status_params_t must be 24 bytes");

/* Opcode 15: SCARLETT_DSTORAGE_UNIX_DESTROY_STATUS_ARRAY */
typedef struct scarlett_dstorage_destroy_status_params_s {
    uint64_t status_array_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_destroy_status_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_destroy_status_params_t) == 16,
                       "scarlett_dstorage_destroy_status_params_t must be 16 bytes");

/* Opcode 16: SCARLETT_DSTORAGE_UNIX_QUERY_STATUS */
typedef struct scarlett_dstorage_query_status_params_s {
    uint64_t status_array_handle;
    uint32_t index;
    uint32_t out_is_complete;
    scarlett_hresult_t out_entry_hresult;
    scarlett_hresult_t out_status;
} scarlett_dstorage_query_status_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_query_status_params_t) == 24,
                       "scarlett_dstorage_query_status_params_t must be 24 bytes");

/* Opcode 17: SCARLETT_DSTORAGE_UNIX_GET_FILE_INFO */
typedef struct scarlett_dstorage_file_info_params_s {
    uint64_t file_handle;
    uint64_t out_file_size_bytes;
    uint32_t out_open_flags;
    scarlett_hresult_t out_status;
} scarlett_dstorage_file_info_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_file_info_params_t) == 24,
                       "scarlett_dstorage_file_info_params_t must be 24 bytes");

/* Opcode 18: SCARLETT_DSTORAGE_UNIX_RETRIEVE_ERROR_RECORD */
typedef struct scarlett_dstorage_error_record_params_s {
    uint64_t queue_handle;
    uint64_t out_record_addr; /* DSTORAGE_ERROR_RECORD* */
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_error_record_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_error_record_params_t) == 24,
                       "scarlett_dstorage_error_record_params_t must be 24 bytes");

/* Opcode 19: SCARLETT_DSTORAGE_UNIX_SET_STAGING_SIZE */
typedef struct scarlett_dstorage_staging_size_params_s {
    uint64_t size_bytes;
    uint64_t out_active_size_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_staging_size_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_staging_size_params_t) == 24,
                       "scarlett_dstorage_staging_size_params_t must be 24 bytes");

/* Opcode 20: SCARLETT_DSTORAGE_UNIX_QUERY_CAPS */
typedef struct scarlett_dstorage_caps_params_s {
    uint32_t out_io_uring_active;
    uint32_t out_io_uring_sqpoll;
    uint32_t out_io_uring_fixed_buffers;
    uint32_t out_vk_memory_decompression;
    uint32_t out_vkdeflate_spirv_valid;
    uint32_t out_swizzle_spirv_valid;
    uint32_t out_bcpack_plugin_loaded;
    uint32_t out_bcpack_header_reads;
    uint64_t out_bcpack_decoded_bytes;
    uint64_t out_staging_buffer_bytes;
    uint64_t out_total_bytes_read;
    uint64_t out_total_bytes_decompressed;
    uint32_t out_worker_threads;
    scarlett_hresult_t out_status;
} scarlett_dstorage_caps_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_caps_params_t) == 72,
                       "scarlett_dstorage_caps_params_t must be 72 bytes");

/* Opcode 21: SCARLETT_DSTORAGE_UNIX_SUBMIT_BATCH */
typedef struct scarlett_dstorage_submit_batch_params_s {
    uint64_t queue_handle;
    uint64_t requests_addr; /* const DSTORAGE_REQUEST* */
    uint32_t request_count;
    uint32_t wait_for_completion;
    uint64_t out_bytes_decompressed;
    uint64_t out_elapsed_ns;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_submit_batch_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_submit_batch_params_t) == 48,
                       "scarlett_dstorage_submit_batch_params_t must be 48 bytes");

#define SCARLETT_DSTORAGE_SIGNAL_MODE_HOST_FENCE  UINT32_C(0)
#define SCARLETT_DSTORAGE_SIGNAL_MODE_HOST_EVENT  UINT32_C(1)
#define SCARLETT_DSTORAGE_SIGNAL_MODE_PE_FENCE    UINT32_C(2)
#define SCARLETT_DSTORAGE_SIGNAL_MODE_PE_EVENT    UINT32_C(3)
#define SCARLETT_DSTORAGE_PE_SIGNAL_BATCH_MAX     UINT32_C(32)

typedef struct scarlett_dstorage_pe_signal_item_s {
    uint64_t target_addr;
    uint64_t value;
    uint32_t mode;
    uint32_t reserved;
} scarlett_dstorage_pe_signal_item_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_pe_signal_item_t) == 24,
                       "scarlett_dstorage_pe_signal_item_t must be 24 bytes");

/* Opcode 23: SCARLETT_DSTORAGE_UNIX_WAIT_SIGNALS */
typedef struct scarlett_dstorage_wait_signals_params_s {
    uint32_t blocking;
    uint32_t out_count;
    uint32_t out_stop;
    scarlett_hresult_t out_status;
    scarlett_dstorage_pe_signal_item_t items[SCARLETT_DSTORAGE_PE_SIGNAL_BATCH_MAX];
} scarlett_dstorage_wait_signals_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_wait_signals_params_t) == 16 + 32 * 24,
                       "scarlett_dstorage_wait_signals_params_t must be 784 bytes");

/* Opcode 22: SCARLETT_DSTORAGE_UNIX_REPORT_STUB */
typedef struct scarlett_dstorage_report_stub_params_s {
    scarlett_stub_context_t ctx;
    uint32_t out_action; /* scarlett_stub_action_t */
    scarlett_hresult_t out_status;
} scarlett_dstorage_report_stub_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_report_stub_params_t) == 480,
                       "scarlett_dstorage_report_stub_params_t must be 480 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_DSTORAGE_XS_UNIX_ABI_H */
