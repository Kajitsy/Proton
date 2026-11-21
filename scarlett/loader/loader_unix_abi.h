/*
 * Proton-Scarlett — Loader, VFS, XGamePlatform & PSM AppNotify ABI Contract
 *
 * Freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLLs (xgameplatform.dll, api-ms-win-core-psm-appnotify-l1-1-0.dll)
 *   - loader_unix.so (x86_64-unix)
 *   - Host C++20 unit tests
 *
 * Zero dependencies on <windows.h>, libc, or POSIX headers.
 */

#ifndef SCARLETT_LOADER_UNIX_ABI_H
#define SCARLETT_LOADER_UNIX_ABI_H

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

/* ============================================================================
 * Portable Calling Convention & WINE_UNIX_CALL Bridge Helpers
 * ============================================================================ */

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
#define SCARLETT_WIN64_API __attribute__((ms_abi))
#elif defined(_MSC_VER)
#define SCARLETT_WIN64_API __stdcall
#else
#define SCARLETT_WIN64_API
#endif
#else
#define SCARLETT_WIN64_API
#endif

#if defined(WINE_UNIX_CALL)
/* Wine PE build: __wine_unix_call_dispatcher provided by wine/unixlib.h */
#else
typedef int32_t (*scarlett_loader_unix_bridge_fn_t)(uint32_t opcode, void *params);
void scarlett_loader_pe_set_unix_bridge(scarlett_loader_unix_bridge_fn_t bridge_fn);
scarlett_loader_unix_bridge_fn_t scarlett_loader_pe_get_unix_bridge(void);
#endif

/* ============================================================================
 * VFS Access Flags, Layer Identifiers & Path Normalization Constants
 * ============================================================================ */

#define SCARLETT_LOADER_MAX_PATH UINT32_C(512)
#define SCARLETT_LOADER_MAX_NAME UINT32_C(128)

typedef enum scarlett_vfs_access_flags_e {
    SCARLETT_VFS_ACCESS_READ      = (1u << 0),
    SCARLETT_VFS_ACCESS_WRITE     = (1u << 1),
    SCARLETT_VFS_ACCESS_CREATE    = (1u << 2),
    SCARLETT_VFS_ACCESS_TRUNCATE  = (1u << 3),
    SCARLETT_VFS_ACCESS_APPEND    = (1u << 4),
    SCARLETT_VFS_ACCESS_DIRECTORY = (1u << 5)
} scarlett_vfs_access_flags_t;

typedef enum scarlett_vfs_layer_e {
    SCARLETT_VFS_LAYER_NONE     = 0,
    SCARLETT_VFS_LAYER_BASE_DIR = 1,
    SCARLETT_VFS_LAYER_OVERLAY  = 2,
    SCARLETT_VFS_LAYER_XVD_NTFS = 3
} scarlett_vfs_layer_t;

/* ============================================================================
 * Loader Unixlib Extended Opcodes (0x100+) & Core Opcode Payload Structs
 * ============================================================================ */

typedef enum scarlett_loader_unix_ex_op_e {
    SCARLETT_LOADER_UNIX_EX_LOAD_CONFIG       = 0x100,
    SCARLETT_LOADER_UNIX_EX_GET_METADATA      = 0x101,
    SCARLETT_LOADER_UNIX_EX_VFS_OPEN          = 0x102,
    SCARLETT_LOADER_UNIX_EX_VFS_READ          = 0x103,
    SCARLETT_LOADER_UNIX_EX_VFS_WRITE         = 0x104,
    SCARLETT_LOADER_UNIX_EX_VFS_CLOSE         = 0x105,
    SCARLETT_LOADER_UNIX_EX_VFS_DELETE        = 0x106,
    SCARLETT_LOADER_UNIX_EX_VFS_MKDIR         = 0x107,
    SCARLETT_LOADER_UNIX_EX_VFS_STAT          = 0x108,
    SCARLETT_LOADER_UNIX_EX_PSM_REGISTER      = 0x109,
    SCARLETT_LOADER_UNIX_EX_PSM_UNREGISTER    = 0x10A,
    SCARLETT_LOADER_UNIX_EX_PSM_SNAPSHOT_CB   = 0x10B,
    SCARLETT_LOADER_UNIX_EX_GET_LAST_STUB     = 0x10C,
    SCARLETT_LOADER_UNIX_EX_QUERY_XVD_INFO    = 0x10D
} scarlett_loader_unix_ex_op_t;

/* Payload for SCARLETT_LOADER_UNIX_UNMOUNT_PACKAGE (opcode 2) */
typedef struct scarlett_loader_unmount_pkg_params_s {
    uint64_t mount_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_unmount_pkg_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_unmount_pkg_params_t) == 16,
                       "scarlett_loader_unmount_pkg_params_t must be 16 bytes");

/* Payload for SCARLETT_LOADER_UNIX_RESOLVE_GAME_PATH (opcode 3) */
typedef struct scarlett_loader_resolve_path_params_s {
    uint64_t mount_handle;
    uint64_t game_path_utf8_addr;
    uint64_t out_host_path_utf8_addr;
    uint32_t game_path_len;
    uint32_t out_host_path_cap;
    uint32_t access_flags;
    uint32_t out_resolved_layer; /* scarlett_vfs_layer_t */
    uint32_t out_host_path_len;
    scarlett_hresult_t out_status;
} scarlett_loader_resolve_path_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_resolve_path_params_t) == 48,
                       "scarlett_loader_resolve_path_params_t must be 48 bytes");

/* Payload for SCARLETT_LOADER_UNIX_GAMESAVE_COMMIT_BLOB (opcode 7) */
typedef struct scarlett_loader_gamesave_commit_params_s {
    uint64_t dest_path_utf8_addr;
    uint64_t data_addr;
    uint32_t dest_path_len;
    uint32_t data_size;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_gamesave_commit_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_gamesave_commit_params_t) == 32,
                       "scarlett_loader_gamesave_commit_params_t must be 32 bytes");

/* ============================================================================
 * MicrosoftGame.Config -> XSystem / XPackage Metadata Contract
 * ============================================================================ */

typedef struct scarlett_loader_title_metadata_s {
    uint32_t abi_version;
    uint32_t is_scarlett;
    uint32_t title_id;
    uint32_t advanced_user_model;
    uint32_t vm_enabled;
    uint32_t desktop_reg_present;
    uint32_t console_model; /* scarlett_console_model_t */
    uint32_t reserved;
    char identity_name[SCARLETT_LOADER_MAX_NAME];
    char publisher[SCARLETT_LOADER_MAX_NAME];
    char version[64];
    char msa_app_id[64];
    char display_name[SCARLETT_LOADER_MAX_NAME];
    char selected_exe[SCARLETT_LOADER_MAX_PATH];
    char target_device_family[64];
    char package_full_name[256];
} scarlett_loader_title_metadata_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_title_metadata_t) == 1376,
                       "scarlett_loader_title_metadata_t size check");

typedef struct scarlett_loader_config_params_s {
    uint64_t xml_or_path_utf8_addr;
    uint32_t input_len;
    uint32_t is_xml_buffer; /* 0 = path to MicrosoftGame.Config, 1 = XML text */
    uint64_t game_dir_utf8_addr;
    uint32_t game_dir_len;
    scarlett_hresult_t out_status;
    uint64_t out_metadata_addr; /* scarlett_loader_title_metadata_t* */
} scarlett_loader_config_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_config_params_t) == 40,
                       "scarlett_loader_config_params_t must be 40 bytes");

/* ============================================================================
 * VFS File I/O Extended Payloads
 * ============================================================================ */

typedef struct scarlett_loader_vfs_open_params_s {
    uint64_t mount_handle;
    uint64_t game_path_utf8_addr;
    uint32_t game_path_len;
    uint32_t access_flags; /* scarlett_vfs_access_flags_t */
    uint64_t out_file_handle;
    uint64_t out_file_size;
    uint32_t out_layer;
    scarlett_hresult_t out_status;
} scarlett_loader_vfs_open_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_vfs_open_params_t) == 48,
                       "scarlett_loader_vfs_open_params_t must be 48 bytes");

typedef struct scarlett_loader_vfs_rw_params_s {
    uint64_t file_handle;
    uint64_t buffer_addr;
    uint64_t offset;
    uint32_t byte_count;
    uint32_t out_bytes_transferred;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_vfs_rw_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_vfs_rw_params_t) == 40,
                       "scarlett_loader_vfs_rw_params_t must be 40 bytes");

typedef struct scarlett_loader_vfs_close_params_s {
    uint64_t file_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_vfs_close_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_vfs_close_params_t) == 16,
                       "scarlett_loader_vfs_close_params_t must be 16 bytes");

typedef struct scarlett_loader_vfs_path_op_params_s {
    uint64_t mount_handle;
    uint64_t game_path_utf8_addr;
    uint32_t game_path_len;
    uint32_t out_exists;
    uint32_t out_is_directory;
    uint32_t out_layer;
    uint64_t out_size_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_vfs_path_op_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_vfs_path_op_params_t) == 48,
                       "scarlett_loader_vfs_path_op_params_t must be 48 bytes");

/* ============================================================================
 * PSM AppNotify Contract (api-ms-win-core-psm-appnotify-l1-1-0)
 * ============================================================================ */

typedef enum scarlett_psm_notify_kind_e {
    SCARLETT_PSM_NOTIFY_APP_STATE   = 1,
    SCARLETT_PSM_NOTIFY_CONSTRAINED = 2
} scarlett_psm_notify_kind_t;

typedef enum scarlett_psm_event_type_e {
    SCARLETT_PSM_EVENT_RESUME        = 0, /* Quiesced = 0 */
    SCARLETT_PSM_EVENT_SUSPEND       = 1, /* Quiesced = 1 */
    SCARLETT_PSM_EVENT_UNCONSTRAINED = 2, /* Constrained = 0 */
    SCARLETT_PSM_EVENT_CONSTRAINED   = 3  /* Constrained = 1 */
} scarlett_psm_event_type_t;

typedef void (SCARLETT_WIN64_API *scarlett_psm_callback_fn_t)(uint8_t state_flag, void *context);

#define SCARLETT_PSM_MAX_SNAPSHOT_CALLBACKS UINT32_C(64)

typedef struct scarlett_psm_register_params_s {
    uint64_t callback_addr;
    uint64_t context_addr;
    uint32_t notify_kind; /* scarlett_psm_notify_kind_t */
    uint32_t reserved;
    uint64_t out_registration_handle;
    scarlett_hresult_t out_status;
    uint32_t pad;
} scarlett_psm_register_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_psm_register_params_t) == 40,
                       "scarlett_psm_register_params_t must be 40 bytes");

typedef struct scarlett_psm_unregister_params_s {
    uint64_t registration_handle;
    uint32_t notify_kind;
    scarlett_hresult_t out_status;
} scarlett_psm_unregister_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_psm_unregister_params_t) == 16,
                       "scarlett_psm_unregister_params_t must be 16 bytes");

typedef struct scarlett_psm_cb_entry_s {
    uint64_t callback_addr;
    uint64_t context_addr;
} scarlett_psm_cb_entry_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_psm_cb_entry_t) == 16,
                       "scarlett_psm_cb_entry_t must be 16 bytes");

typedef struct scarlett_psm_snapshot_params_s {
    uint32_t event_type; /* scarlett_psm_event_type_t */
    uint32_t out_count;
    uint64_t out_entries_addr; /* scarlett_psm_cb_entry_t[capacity] */
    uint32_t capacity;
    scarlett_hresult_t out_status;
} scarlett_psm_snapshot_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_psm_snapshot_params_t) == 24,
                       "scarlett_psm_snapshot_params_t must be 24 bytes");

/* ============================================================================
 * Last Diagnostic Stub Inspection & XVD Summary Query
 * ============================================================================ */

typedef struct scarlett_loader_last_stub_params_s {
    scarlett_stub_context_t out_ctx;
    uint32_t out_has_stub;
    uint32_t out_total_hits;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_last_stub_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_last_stub_params_t) == 488,
                       "scarlett_loader_last_stub_params_t must be 488 bytes");

typedef struct scarlett_loader_xvd_info_params_s {
    uint64_t mount_handle;
    uint32_t out_format_version;
    uint32_t out_volume_flags;
    uint64_t out_drive_size;
    uint32_t out_user_data_length;
    uint32_t out_xvc_data_length;
    uint32_t out_dyn_header_length;
    uint32_t out_xvc_region_count;
    uint32_t out_xvc_chunk_count;
    uint32_t out_xvc_update_segment_count;
    uint8_t  out_content_id[16];
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_loader_xvd_info_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_loader_xvd_info_params_t) == 72,
                       "scarlett_loader_xvd_info_params_t must be 72 bytes");

/* ============================================================================
 * Public C Entrypoints for xgameplatform / PSM AppNotify & Host Dispatch
 * ============================================================================ */

int32_t scarlett_loader_unix_dispatch(uint32_t opcode, void *params);

/* xgameplatform API-set resolver & metadata query APIs */
scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformResolveApiSet(
    const char *api_set_name,
    char *out_target_dll,
    uint32_t out_target_cap);

scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformGetTitleMetadata(
    scarlett_loader_title_metadata_t *out_metadata);

scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformGetPackageFullName(
    char *out_buffer,
    uint32_t out_cap);

uint32_t SCARLETT_WIN64_API XGamePlatformGetConsoleModel(void);

/* Unimplemented xgameplatform export routed via diagnostic auto-stub thunk */
scarlett_hresult_t SCARLETT_WIN64_API XGamePlatformCreateActivationContext(
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3);

/* PSM AppNotify APIs (api-ms-win-core-psm-appnotify-l1-1-0) */
uint32_t SCARLETT_WIN64_API RegisterAppStateChangeNotification(
    scarlett_psm_callback_fn_t routine,
    void *context,
    void **registration);

void SCARLETT_WIN64_API UnregisterAppStateChangeNotification(
    void *registration);

uint32_t SCARLETT_WIN64_API RegisterAppConstrainedChangeNotification(
    scarlett_psm_callback_fn_t routine,
    void *context,
    void **registration);

void SCARLETT_WIN64_API UnregisterAppConstrainedChangeNotification(
    void *registration);

scarlett_hresult_t SCARLETT_WIN64_API ScarlettPsmDispatchEvent(
    uint32_t event_type);

scarlett_hresult_t SCARLETT_WIN64_API RegisterAppStateChangeNotificationEx(
    uint64_t arg0,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_LOADER_UNIX_ABI_H */
