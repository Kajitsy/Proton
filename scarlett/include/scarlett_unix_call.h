/*
 * Proton-Scarlett — Unified Wine 11.0 WINE_UNIX_CALL Bridge Contract
 *
 * Enforces strict Wine 11.0 PE (x86_64-windows) vs unixlib (x86_64-unix)
 * separation for all Scarlett native backends:
 *   - xmem_unix.so          (virtual ranges, physical aliasing, UMA Tier A/B/C)
 *   - dstorage_xs_unix.so   (io_uring, GPU/CPU decompression, IBcpackDecoder)
 *   - audio_scarlett_unix.so(FAudio/PipeWire, AcpHal XMA2, GameInput 4-motor, Bink2)
 *   - loader_unix.so        (MicrosoftGame.Config, GAME: CoW, XVD/XVC, XGameSave, Net)
 *
 * ABI Rules:
 *   1. PE64 DLLs initialize the bridge once via `__wine_init_unix_call()` in
 *      DllMain(DLL_PROCESS_ATTACH) and cross the boundary ONLY via WINE_UNIX_CALL.
 *   2. PE64 DLLs MUST NEVER link libc, libvulkan, liburing, libdrm, or POSIX APIs.
 *   3. Every parameter struct uses explicit fixed-width types (no `long`, `size_t`,
 *      or `bool`), explicit padding, and compile-time size assertions so MS x64 ABI
 *      and System V AMD64 ABI observe identical memory layouts.
 */

#ifndef SCARLETT_UNIX_CALL_H
#define SCARLETT_UNIX_CALL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCARLETT_UNIX_CALL_ABI_VERSION UINT32_C(1)

/* Canonical unixlib module identifiers for handshake validation */
typedef enum scarlett_unix_module_id_e {
    SCARLETT_UNIX_MOD_XMEM     = 1, /* xmem_unix.so */
    SCARLETT_UNIX_MOD_DSTORAGE = 2, /* dstorage_xs_unix.so */
    SCARLETT_UNIX_MOD_AUDIO    = 3, /* audio_scarlett_unix.so */
    SCARLETT_UNIX_MOD_LOADER   = 4  /* loader_unix.so */
} scarlett_unix_module_id_t;

/*
 * Portable dispatch wrapper:
 *   - Inside Wine PE builds (after including <wine/unixlib.h>), routes directly
 *     to `WINE_UNIX_CALL(opcode, params_ptr)`.
 *   - In standalone host tests, a mock dispatcher function can be injected via
 *     `scarlett_unix_call_fn_t`.
 */
#if defined(WINE_UNIX_CALL)
#define SCARLETT_UNIX_CALL(opcode, params_ptr) WINE_UNIX_CALL((opcode), (params_ptr))
#endif

typedef int32_t (*scarlett_unix_call_fn_t)(uint32_t opcode, void *params);

/* ============================================================================
 * Common Handshake Payload (Opcode 0 in every unixlib module)
 * ============================================================================ */

typedef struct scarlett_unix_init_params_s {
    uint32_t abi_version;          /* Must equal SCARLETT_UNIX_CALL_ABI_VERSION */
    uint32_t module_id;            /* scarlett_unix_module_id_t */
    uint32_t console_model;        /* scarlett_console_model_t */
    uint32_t strict_mode;          /* 0 or 1 (PROTON_XBOX_STRICT) */
    uint32_t shader_mode;          /* scarlett_shader_mode_t */
    uint32_t net_mode;             /* scarlett_net_mode_t */
    uint32_t out_unix_abi_version; /* Reported by unixlib */
    scarlett_hresult_t out_status;
} scarlett_unix_init_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_unix_init_params_t) == 32,
                       "scarlett_unix_init_params_t must be 32 bytes");

/* ============================================================================
 * Module 1: xmem_unix.so Opcodes & Parameter Structs
 * ============================================================================ */

typedef enum scarlett_xmem_unix_op_e {
    SCARLETT_XMEM_UNIX_INIT              = 0,
    SCARLETT_XMEM_UNIX_RESERVE_RANGE     = 1,
    SCARLETT_XMEM_UNIX_ALLOC_PHYS_PAGES  = 2,
    SCARLETT_XMEM_UNIX_FREE_PHYS_PAGES   = 3,
    SCARLETT_XMEM_UNIX_MAP_PHYS_PAGES    = 4,
    SCARLETT_XMEM_UNIX_UNMAP_PHYS_PAGES  = 5,
    SCARLETT_XMEM_UNIX_UMA_EXPORT        = 6,
    SCARLETT_XMEM_UNIX_UFFD_FLUSH_DIRTY  = 7,
    SCARLETT_XMEM_UNIX_QUERY_BUDGET      = 8,
    SCARLETT_XMEM_UNIX_OP_COUNT          = 9
} scarlett_xmem_unix_op_t;

typedef struct scarlett_xmem_reserve_params_s {
    uint64_t requested_base;
    uint64_t size_bytes;
    uint32_t region;               /* scarlett_mem_region_t */
    uint32_t flags;
    uint64_t out_base;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_reserve_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_reserve_params_t) == 40,
                       "scarlett_xmem_reserve_params_t must be 40 bytes");

typedef struct scarlett_xmem_alloc_pages_params_s {
    uint64_t page_count;
    uint64_t page_size_bytes;      /* 4KB, 64KB, 2MB, or 4MB */
    uint64_t out_pfn_array_addr;   /* Guest uint64_t[page_count] pointer */
    uint32_t cache_attr;           /* scarlett_mem_cache_attr_t */
    uint32_t flags;
    uint64_t out_alloc_id;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_alloc_pages_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_alloc_pages_params_t) == 48,
                       "scarlett_xmem_alloc_pages_params_t must be 48 bytes");

typedef struct scarlett_xmem_map_pages_params_s {
    uint64_t virtual_addr;
    uint64_t page_count;
    uint64_t page_size_bytes;
    uint64_t pfn_array_addr;       /* Guest const uint64_t[page_count] pointer */
    uint32_t access;               /* scarlett_mem_access_t (RO/RW aliasing OK) */
    uint32_t cache_attr;           /* Must match PFN cache attr else CACHE_CONFLICT */
    uint32_t unmap_flag;           /* 0 = map, 1 = unmap */
    scarlett_hresult_t out_status;
} scarlett_xmem_map_pages_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_map_pages_params_t) == 48,
                       "scarlett_xmem_map_pages_params_t must be 48 bytes");

typedef struct scarlett_xmem_uma_export_params_s {
    uint64_t virtual_addr;
    uint64_t size_bytes;
    uint32_t preferred_tier;       /* scarlett_uma_tier_t */
    uint32_t out_active_tier;      /* scarlett_uma_tier_t */
    int32_t  out_dmabuf_fd;        /* Tier A DMA-BUF fd, or -1 */
    scarlett_hresult_t out_status;
    uint64_t out_host_ptr;         /* Tier B/C host pointer for Vulkan import/shadow */
} scarlett_xmem_uma_export_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_uma_export_params_t) == 40,
                       "scarlett_xmem_uma_export_params_t must be 40 bytes");

typedef struct scarlett_xmem_flush_dirty_params_s {
    uint64_t command_list_id;      /* Invoked prior to every ExecuteCommandLists on Tier C */
    uint64_t region_addr;          /* 0 to flush all dirty MEM_GRAPHICS pages */
    uint64_t region_size;
    uint64_t out_flushed_pages;
    uint64_t out_flushed_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xmem_flush_dirty_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_flush_dirty_params_t) == 48,
                       "scarlett_xmem_flush_dirty_params_t must be 48 bytes");

typedef struct scarlett_xmem_query_budget_params_s {
    uint64_t out_total_budget_bytes;
    uint64_t out_committed_bytes;
    uint64_t out_graphics_committed_bytes;
    uint64_t out_title_committed_bytes;
    uint32_t out_active_uma_tier;
    scarlett_hresult_t out_status;
} scarlett_xmem_query_budget_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xmem_query_budget_params_t) == 40,
                       "scarlett_xmem_query_budget_params_t must be 40 bytes");

/* ============================================================================
 * Module 2: dstorage_xs_unix.so Opcodes & Parameter Structs
 * ============================================================================ */

typedef enum scarlett_dstorage_unix_op_e {
    SCARLETT_DSTORAGE_UNIX_INIT               = 0,
    SCARLETT_DSTORAGE_UNIX_OPEN_FILE          = 1,
    SCARLETT_DSTORAGE_UNIX_CLOSE_FILE         = 2,
    SCARLETT_DSTORAGE_UNIX_CREATE_QUEUE       = 3,
    SCARLETT_DSTORAGE_UNIX_DESTROY_QUEUE      = 4,
    SCARLETT_DSTORAGE_UNIX_SUBMIT_REQUEST     = 5,
    SCARLETT_DSTORAGE_UNIX_POLL_COMPLETION    = 6,
    SCARLETT_DSTORAGE_UNIX_LOAD_BCPACK_PLUGIN = 7,
    SCARLETT_DSTORAGE_UNIX_OP_COUNT           = 8
} scarlett_dstorage_unix_op_t;

typedef struct scarlett_dstorage_submit_params_s {
    uint64_t queue_handle;
    uint64_t file_handle;
    uint64_t file_offset;
    uint64_t src_addr;
    uint64_t src_size;
    uint64_t dst_addr;
    uint64_t dst_size;
    scarlett_dstorage_request_options_t options;
    uint64_t request_tag;
    uint32_t priority;
    scarlett_hresult_t out_status;
} scarlett_dstorage_submit_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_submit_params_t) == 80,
                       "scarlett_dstorage_submit_params_t must be 80 bytes");

typedef struct scarlett_dstorage_poll_params_s {
    uint64_t queue_handle;
    uint64_t timeout_ns;
    uint64_t out_completed_tag;
    uint64_t out_bytes_transferred;
    uint32_t out_has_completion;
    scarlett_hresult_t out_request_status;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_dstorage_poll_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_poll_params_t) == 48,
                       "scarlett_dstorage_poll_params_t must be 48 bytes");

typedef struct scarlett_dstorage_bcpack_plugin_params_s {
    uint64_t plugin_path_utf8_addr;
    uint32_t plugin_path_len;
    uint32_t out_plugin_abi_version;
    uint32_t out_loaded;
    scarlett_hresult_t out_status;
} scarlett_dstorage_bcpack_plugin_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_dstorage_bcpack_plugin_params_t) == 24,
                       "scarlett_dstorage_bcpack_plugin_params_t must be 24 bytes");

/* ============================================================================
 * Module 3: audio_scarlett_unix.so Opcodes & Parameter Structs
 * ============================================================================ */

typedef enum scarlett_audio_unix_op_e {
    SCARLETT_AUDIO_UNIX_INIT              = 0,
    SCARLETT_AUDIO_UNIX_OPEN_STREAM       = 1,
    SCARLETT_AUDIO_UNIX_CLOSE_STREAM      = 2,
    SCARLETT_AUDIO_UNIX_SUBMIT_PCM        = 3,
    SCARLETT_AUDIO_UNIX_ACPHAL_DECODE     = 4,
    SCARLETT_AUDIO_UNIX_INPUT_POLL        = 5,
    SCARLETT_AUDIO_UNIX_INPUT_SET_RUMBLE  = 6,
    SCARLETT_AUDIO_UNIX_BINK2_DECODE      = 7,
    SCARLETT_AUDIO_UNIX_OP_COUNT          = 8
} scarlett_audio_unix_op_t;

typedef struct scarlett_audio_pcm_submit_params_s {
    uint64_t stream_handle;
    uint64_t buffer_addr;
    uint32_t frame_count;
    uint32_t channels;
    uint32_t sample_rate_hz;
    uint32_t format_tag;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_audio_pcm_submit_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_pcm_submit_params_t) == 40,
                       "scarlett_audio_pcm_submit_params_t must be 40 bytes");

typedef struct scarlett_audio_acphal_decode_params_s {
    uint64_t context_handle;
    uint64_t src_xma2_addr;
    uint64_t dst_pcm_addr;
    uint32_t src_bytes;
    uint32_t dst_capacity_bytes;
    uint32_t out_decoded_bytes;
    scarlett_hresult_t out_status;
} scarlett_audio_acphal_decode_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_audio_acphal_decode_params_t) == 40,
                       "scarlett_audio_acphal_decode_params_t must be 40 bytes");

/* GameInput 4-motor rumble: low/high frequency + left/right impulse triggers */
typedef struct scarlett_input_rumble_params_s {
    uint64_t device_id;
    float low_frequency;
    float high_frequency;
    float left_trigger;
    float right_trigger;
    uint32_t duration_ms;
    scarlett_hresult_t out_status;
} scarlett_input_rumble_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_input_rumble_params_t) == 32,
                       "scarlett_input_rumble_params_t must be 32 bytes");

typedef struct scarlett_media_bink2_frame_params_s {
    uint64_t decoder_handle;
    uint64_t dst_y_addr;
    uint64_t dst_uv_addr;
    uint32_t width;
    uint32_t height;
    uint32_t out_frame_index;
    scarlett_hresult_t out_status;
} scarlett_media_bink2_frame_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_media_bink2_frame_params_t) == 40,
                       "scarlett_media_bink2_frame_params_t must be 40 bytes");

/* ============================================================================
 * Module 4: loader_unix.so Opcodes & Parameter Structs
 * ============================================================================ */

typedef enum scarlett_loader_unix_op_e {
    SCARLETT_LOADER_UNIX_INIT                 = 0,
    SCARLETT_LOADER_UNIX_MOUNT_PACKAGE        = 1,
    SCARLETT_LOADER_UNIX_UNMOUNT_PACKAGE      = 2,
    SCARLETT_LOADER_UNIX_RESOLVE_GAME_PATH    = 3,
    SCARLETT_LOADER_UNIX_REPORT_STUB          = 4,
    SCARLETT_LOADER_UNIX_SET_TIME_SENSITIVE   = 5,
    SCARLETT_LOADER_UNIX_CHECK_NET_POLICY     = 6,
    SCARLETT_LOADER_UNIX_GAMESAVE_COMMIT_BLOB = 7,
    SCARLETT_LOADER_UNIX_OP_COUNT             = 8
} scarlett_loader_unix_op_t;

typedef struct scarlett_loader_mount_pkg_params_s {
    uint64_t package_path_utf8_addr;
    uint64_t overlay_dir_utf8_addr;
    uint32_t package_path_len;
    uint32_t overlay_dir_len;
    uint32_t out_volume_flags;
    uint32_t out_is_encrypted; /* 1 => out_status MUST be ENCRYPTED_PACKAGE_UNSUPPORTED */
    uint64_t out_mount_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_mount_pkg_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_mount_pkg_params_t) == 48,
                       "scarlett_loader_mount_pkg_params_t must be 48 bytes");

typedef struct scarlett_loader_report_stub_params_s {
    scarlett_stub_context_t ctx;
    uint32_t out_action; /* scarlett_stub_action_t */
    scarlett_hresult_t out_status;
} scarlett_loader_report_stub_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_report_stub_params_t) == 480,
                       "scarlett_loader_report_stub_params_t must be 480 bytes");

typedef struct scarlett_loader_thread_rt_params_s {
    uint32_t is_time_sensitive;
    uint32_t out_previous_state;
    uint32_t out_os_priority_applied;
    scarlett_hresult_t out_status;
} scarlett_loader_thread_rt_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_thread_rt_params_t) == 16,
                       "scarlett_loader_thread_rt_params_t must be 16 bytes");

typedef struct scarlett_loader_net_policy_params_s {
    uint64_t host_utf8_addr;
    uint32_t host_len;
    uint32_t port;
    uint32_t out_allowed;
    scarlett_hresult_t out_status;
} scarlett_loader_net_policy_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_net_policy_params_t) == 24,
                       "scarlett_loader_net_policy_params_t must be 24 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_UNIX_CALL_H */
