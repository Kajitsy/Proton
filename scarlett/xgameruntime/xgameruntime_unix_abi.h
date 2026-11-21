/*
 * Proton-Scarlett — GDK Runtime (xgameruntime / libHttpClient / XCurl) Unixlib ABI Contract
 *
 * Clean-room, freestanding C11 / C++20 header shared identically across:
 *   - PE64 DLLs (xgameruntime.dll, libHttpClient.dll, XCurl.dll)
 *   - xgameruntime_unix.so (x86_64-unix)
 *   - Host C++20 unit tests
 *
 * Strictly zero dependencies on <windows.h>, libc, libcurl, or POSIX headers.
 * Every cross-boundary struct uses explicit fixed-width types (no `long`,
 * `size_t`, or `bool`), explicit padding, and compile-time size assertions.
 */

#ifndef SCARLETT_XGAMERUNTIME_UNIX_ABI_H
#define SCARLETT_XGAMERUNTIME_UNIX_ABI_H

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
 * Module Identity & Portable Calling Conventions
 * ============================================================================ */

#define SCARLETT_XGR_UNIX_MODULE_ID UINT32_C(5)

#if defined(_WIN32)
#define SCARLETT_XGR_API __declspec(dllexport)
#define SCARLETT_XGR_CALL __stdcall
#else
#define SCARLETT_XGR_API __attribute__((visibility("default")))
#define SCARLETT_XGR_CALL
#endif

#if defined(_WIN32)
#define SCARLETT_XGR_WIN64_ABI __cdecl
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define SCARLETT_XGR_WIN64_ABI __attribute__((ms_abi))
#else
#define SCARLETT_XGR_WIN64_ABI
#endif

#define SCARLETT_XGR_MAX_NAME       UINT32_C(128)
#define SCARLETT_XGR_MAX_PATH       UINT32_C(512)
#define SCARLETT_XGR_MAX_URL        UINT32_C(1024)
#define SCARLETT_XGR_MAX_TEXT       UINT32_C(2048)
#define SCARLETT_XGR_GS_MAX_UPDATE  UINT64_C(16777216) /* 16 MiB hard transaction limit */

/* ============================================================================
 * Dense WINE_UNIX_CALL Opcode Table (0 .. SCARLETT_XGR_UNIX_OP_COUNT - 1)
 * ============================================================================ */

typedef enum scarlett_xgr_unix_op_e {
    SCARLETT_XGR_UNIX_INIT                   = 0,
    SCARLETT_XGR_UNIX_SHUTDOWN               = 1,
    SCARLETT_XGR_UNIX_IS_FEATURE_AVAILABLE   = 2,
    SCARLETT_XGR_UNIX_TASKQ_CREATE           = 3,
    SCARLETT_XGR_UNIX_TASKQ_CREATE_COMPOSITE = 4,
    SCARLETT_XGR_UNIX_TASKQ_DUP              = 5,
    SCARLETT_XGR_UNIX_TASKQ_CLOSE            = 6,
    SCARLETT_XGR_UNIX_TASKQ_SUBMIT           = 7,
    SCARLETT_XGR_UNIX_TASKQ_DISPATCH         = 8,
    SCARLETT_XGR_UNIX_TASKQ_TERMINATE        = 9,
    SCARLETT_XGR_UNIX_TASKQ_MONITOR          = 10,
    SCARLETT_XGR_UNIX_TASKQ_PROCESS_QUEUE    = 11,
    SCARLETT_XGR_UNIX_TASKQ_GET_PORT         = 12,
    SCARLETT_XGR_UNIX_ASYNC_BEGIN            = 13,
    SCARLETT_XGR_UNIX_ASYNC_SCHEDULE         = 14,
    SCARLETT_XGR_UNIX_ASYNC_COMPLETE         = 15,
    SCARLETT_XGR_UNIX_ASYNC_GET_STATUS       = 16,
    SCARLETT_XGR_UNIX_ASYNC_GET_RESULT       = 17,
    SCARLETT_XGR_UNIX_ASYNC_CANCEL           = 18,
    SCARLETT_XGR_UNIX_XSYSTEM_QUERY          = 19,
    SCARLETT_XGR_UNIX_XTHREAD_TIME_SENSITIVE = 20,
    SCARLETT_XGR_UNIX_XUSER_OP               = 21,
    SCARLETT_XGR_UNIX_XPACKAGE_OP            = 22,
    SCARLETT_XGR_UNIX_XPLS_OP                = 23,
    SCARLETT_XGR_UNIX_XSTORE_OP              = 24,
    SCARLETT_XGR_UNIX_XGAMEUI_OP             = 25,
    SCARLETT_XGR_UNIX_GAMESAVE_PROVIDER_OP   = 26,
    SCARLETT_XGR_UNIX_GAMESAVE_CONTAINER_OP  = 27,
    SCARLETT_XGR_UNIX_GAMESAVE_FILES_OP      = 28,
    SCARLETT_XGR_UNIX_NET_CONFIG             = 29,
    SCARLETT_XGR_UNIX_HC_OP                  = 30,
    SCARLETT_XGR_UNIX_XCURL_OP               = 31,
    SCARLETT_XGR_UNIX_XSAPI_OP               = 32,
    SCARLETT_XGR_UNIX_PLAYFAB_OP             = 33,
    SCARLETT_XGR_UNIX_REPORT_STUB            = 34,
    SCARLETT_XGR_UNIX_OP_COUNT               = 35
} scarlett_xgr_unix_op_t;

/* ============================================================================
 * Runtime Feature & Lifecycle Structs
 * ============================================================================ */

typedef struct scarlett_xgr_feature_params_s {
    uint32_t feature;
    uint32_t out_available;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_feature_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_feature_params_t) == 16,
                       "scarlett_xgr_feature_params_t must be 16 bytes");

/* ============================================================================
 * XTaskQueue & XAsync Structs
 * ============================================================================ */

typedef struct scarlett_xgr_taskq_create_params_s {
    uint32_t work_mode;
    uint32_t completion_mode;
    uint64_t work_port_handle;
    uint64_t completion_port_handle;
    uint64_t queue_handle;
    uint32_t port_kind;
    uint32_t is_set_op;
    uint64_t out_handle;
    uint32_t out_sync_backend; /* 1=ntsync, 2=futex/condvar */
    scarlett_hresult_t out_status;
} scarlett_xgr_taskq_create_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_taskq_create_params_t) == 56,
                       "scarlett_xgr_taskq_create_params_t must be 56 bytes");

typedef struct scarlett_xgr_taskq_submit_params_s {
    uint64_t queue_handle;
    uint32_t port;
    uint32_t delay_ms;
    uint64_t callback_addr;
    uint64_t context_addr;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_taskq_submit_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_taskq_submit_params_t) == 40,
                       "scarlett_xgr_taskq_submit_params_t must be 40 bytes");

typedef struct scarlett_xgr_taskq_dispatch_params_s {
    uint64_t queue_handle;
    uint32_t port;
    uint32_t timeout_ms;
    uint32_t out_dispatched;
    uint32_t out_dispatched_count;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_taskq_dispatch_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_taskq_dispatch_params_t) == 32,
                       "scarlett_xgr_taskq_dispatch_params_t must be 32 bytes");

typedef struct scarlett_xgr_taskq_terminate_params_s {
    uint64_t queue_handle;
    uint64_t callback_addr;
    uint64_t context_addr;
    uint32_t wait_for_completion;
    scarlett_hresult_t out_status;
} scarlett_xgr_taskq_terminate_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_taskq_terminate_params_t) == 32,
                       "scarlett_xgr_taskq_terminate_params_t must be 32 bytes");

typedef struct scarlett_xgr_taskq_monitor_params_s {
    uint64_t queue_handle;
    uint64_t callback_addr;
    uint64_t context_addr;
    uint64_t token;
    uint32_t is_unregister;
    scarlett_hresult_t out_status;
} scarlett_xgr_taskq_monitor_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_taskq_monitor_params_t) == 40,
                       "scarlett_xgr_taskq_monitor_params_t must be 40 bytes");

typedef struct scarlett_xgr_async_params_s {
    uint64_t async_block_addr;
    uint64_t queue_handle;
    uint64_t context_addr;
    uint64_t identity_addr;
    uint64_t identity_name_addr;
    uint64_t provider_addr;
    uint64_t completion_cb_addr;
    uint64_t caller_ctx_addr;
    uint64_t buffer_addr;
    uint64_t buffer_size;
    uint64_t out_buffer_used;
    uint32_t delay_ms;
    uint32_t wait_flag;
    scarlett_hresult_t result_hr;
    scarlett_hresult_t out_status;
} scarlett_xgr_async_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_async_params_t) == 104,
                       "scarlett_xgr_async_params_t must be 104 bytes");

/* ============================================================================
 * XSystem & XThread Structs
 * ============================================================================ */

typedef enum scarlett_xgr_xsystem_subop_e {
    SCARLETT_XGR_XSYS_GET_DEVICE_TYPE     = 1,
    SCARLETT_XGR_XSYS_GET_CONSOLE_ID      = 2,
    SCARLETT_XGR_XSYS_GET_SANDBOX_ID      = 3,
    SCARLETT_XGR_XSYS_GET_ANALYTICS_INFO  = 4,
    SCARLETT_XGR_XSYS_GET_DEVICE_ID       = 5,
    SCARLETT_XGR_XSYS_GET_GAMEOS_VERSION  = 6,
    SCARLETT_XGR_XSYS_GET_MEMORY_BUDGET   = 7
} scarlett_xgr_xsystem_subop_t;

typedef struct scarlett_xgr_xsystem_params_s {
    uint32_t subop;
    uint32_t out_device_type;
    uint32_t out_console_model;
    uint32_t buf_cap;
    uint64_t buf_addr;
    uint64_t out_used;
    uint64_t out_u64_a;
    uint64_t out_u64_b;
    uint64_t out_u64_c;
    uint64_t out_u64_d;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_xsystem_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xsystem_params_t) == 72,
                       "scarlett_xgr_xsystem_params_t must be 72 bytes");

/* ============================================================================
 * XUser, XPackage, XPersistentLocalStorage, XStore, XGameUI Structs
 * ============================================================================ */

typedef enum scarlett_xgr_xuser_subop_e {
    SCARLETT_XGR_XUSER_ADD              = 1,
    SCARLETT_XGR_XUSER_DUPLICATE        = 2,
    SCARLETT_XGR_XUSER_CLOSE            = 3,
    SCARLETT_XGR_XUSER_GET_ID           = 4,
    SCARLETT_XGR_XUSER_GET_LOCAL_ID     = 5,
    SCARLETT_XGR_XUSER_GET_GAMERTAG     = 6,
    SCARLETT_XGR_XUSER_GET_AGE_GROUP    = 7,
    SCARLETT_XGR_XUSER_GET_STATE        = 8,
    SCARLETT_XGR_XUSER_CHECK_PRIVILEGE  = 9,
    SCARLETT_XGR_XUSER_SET_SERVICE_REG  = 10
} scarlett_xgr_xuser_subop_t;

typedef struct scarlett_xgr_xuser_params_s {
    uint32_t subop;
    uint32_t options_or_arg;
    uint64_t user_handle;
    uint64_t out_handle_or_u64;
    uint64_t buf_addr;
    uint32_t buf_cap;
    uint32_t out_u32;
    uint32_t out_u32_b;
    scarlett_hresult_t out_status;
} scarlett_xgr_xuser_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xuser_params_t) == 48,
                       "scarlett_xgr_xuser_params_t must be 48 bytes");

typedef enum scarlett_xgr_xpackage_subop_e {
    SCARLETT_XGR_XPKG_GET_CUR_ID       = 1,
    SCARLETT_XGR_XPKG_ENUM_PACKAGES    = 2,
    SCARLETT_XGR_XPKG_ENUM_CHUNKS      = 3,
    SCARLETT_XGR_XPKG_MOUNT            = 4,
    SCARLETT_XGR_XPKG_CLOSE_MOUNT      = 5,
    SCARLETT_XGR_XPKG_GET_MOUNT_PATH   = 6,
    SCARLETT_XGR_XPKG_FIND_CHUNK_AVAIL = 7,
    SCARLETT_XGR_XPKG_REGISTER_CHUNK   = 8
} scarlett_xgr_xpackage_subop_t;

typedef struct scarlett_xgr_xpackage_params_s {
    uint32_t subop;
    uint32_t chunk_id;
    uint64_t handle;
    uint64_t str_addr;
    uint64_t aux_addr;
    uint32_t buf_cap;
    uint32_t out_count;
    uint32_t out_avail;
    scarlett_hresult_t out_status;
} scarlett_xgr_xpackage_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xpackage_params_t) == 48,
                       "scarlett_xgr_xpackage_params_t must be 48 bytes");

typedef enum scarlett_xgr_xpls_subop_e {
    SCARLETT_XGR_XPLS_GET_PATH       = 1,
    SCARLETT_XGR_XPLS_GET_SPACE_INFO = 2,
    SCARLETT_XGR_XPLS_PROMPT_SPACE   = 3,
    SCARLETT_XGR_XPLS_SET_QUOTA      = 4
} scarlett_xgr_xpls_subop_t;

typedef struct scarlett_xgr_xpls_params_s {
    uint32_t subop;
    uint32_t buf_cap;
    uint64_t buf_addr;
    uint64_t out_u64_a;
    uint64_t out_u64_b;
    uint64_t req_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_xpls_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xpls_params_t) == 48,
                       "scarlett_xgr_xpls_params_t must be 48 bytes");

typedef enum scarlett_xgr_xstore_subop_e {
    SCARLETT_XGR_XSTORE_CREATE_CTX       = 1,
    SCARLETT_XGR_XSTORE_CLOSE_CTX        = 2,
    SCARLETT_XGR_XSTORE_ACQUIRE_PKG_LIC  = 3,
    SCARLETT_XGR_XSTORE_ACQUIRE_DUR_LIC  = 4,
    SCARLETT_XGR_XSTORE_IS_LIC_VALID     = 5,
    SCARLETT_XGR_XSTORE_CLOSE_LIC        = 6,
    SCARLETT_XGR_XSTORE_QUERY_ASSOC      = 7,
    SCARLETT_XGR_XSTORE_QUERY_CUR_GAME   = 8,
    SCARLETT_XGR_XSTORE_ENUM_PRODUCTS    = 9,
    SCARLETT_XGR_XSTORE_CLOSE_QUERY      = 10,
    SCARLETT_XGR_XSTORE_QUERY_CONSUMABLE = 11
} scarlett_xgr_xstore_subop_t;

typedef struct scarlett_xgr_xstore_params_s {
    uint32_t subop;
    uint32_t out_bool;
    uint64_t handle;
    uint64_t aux_handle;
    uint64_t str_addr;
    uint64_t cb_addr;
    uint64_t ctx_addr;
    uint32_t out_u32;
    scarlett_hresult_t out_status;
} scarlett_xgr_xstore_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xstore_params_t) == 56,
                       "scarlett_xgr_xstore_params_t must be 56 bytes");

typedef enum scarlett_xgr_xgameui_subop_e {
    SCARLETT_XGR_UI_MSG_DIALOG    = 1,
    SCARLETT_XGR_UI_TEXT_ENTRY    = 2,
    SCARLETT_XGR_UI_ERROR_DIALOG  = 3,
    SCARLETT_XGR_UI_PROFILE_CARD  = 4,
    SCARLETT_XGR_UI_NOTIF_HINT    = 5
} scarlett_xgr_xgameui_subop_t;

typedef struct scarlett_xgr_xgameui_params_s {
    uint32_t subop;
    uint32_t arg_u32;
    uint64_t str_a_addr;
    uint64_t str_b_addr;
    uint64_t out_buf_addr;
    uint32_t out_buf_cap;
    uint32_t out_u32;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_xgameui_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xgameui_params_t) == 48,
                       "scarlett_xgr_xgameui_params_t must be 48 bytes");

/* ============================================================================
 * XGameSave & XGameSaveFiles Structs
 * ============================================================================ */

typedef enum scarlett_xgr_gs_provider_subop_e {
    SCARLETT_XGR_GS_INIT_PROVIDER     = 1,
    SCARLETT_XGR_GS_CLOSE_PROVIDER    = 2,
    SCARLETT_XGR_GS_GET_QUOTA         = 3,
    SCARLETT_XGR_GS_DELETE_CONTAINER  = 4,
    SCARLETT_XGR_GS_ENUM_CONTAINERS   = 5,
    SCARLETT_XGR_GS_SET_POLICY        = 6
} scarlett_xgr_gs_provider_subop_t;

typedef struct scarlett_xgr_gs_provider_params_s {
    uint32_t subop;
    uint32_t flags_or_bool;
    uint64_t provider_handle;
    uint64_t user_handle;
    uint64_t str_addr;
    uint64_t aux_addr;
    uint64_t cb_addr;
    uint64_t ctx_addr;
    uint64_t quota_val;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_gs_provider_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_gs_provider_params_t) == 72,
                       "scarlett_xgr_gs_provider_params_t must be 72 bytes");

typedef enum scarlett_xgr_gs_container_subop_e {
    SCARLETT_XGR_GS_CREATE_CONTAINER  = 1,
    SCARLETT_XGR_GS_CLOSE_CONTAINER   = 2,
    SCARLETT_XGR_GS_CREATE_UPDATE     = 3,
    SCARLETT_XGR_GS_SUBMIT_UPDATE     = 4,
    SCARLETT_XGR_GS_CANCEL_UPDATE     = 5,
    SCARLETT_XGR_GS_CLOSE_UPDATE      = 6,
    SCARLETT_XGR_GS_UPDATE_WRITE_BLOB = 7,
    SCARLETT_XGR_GS_UPDATE_DELETE_BLOB= 8,
    SCARLETT_XGR_GS_ENUM_BLOBS        = 9,
    SCARLETT_XGR_GS_READ_BLOB         = 10
} scarlett_xgr_gs_container_subop_t;

typedef struct scarlett_xgr_gs_container_params_s {
    uint32_t subop;
    uint32_t sync_flag;
    uint64_t provider_or_container_handle;
    uint64_t update_handle;
    uint64_t name_addr;
    uint64_t data_addr;
    uint64_t data_size;
    uint64_t out_count_or_size;
    uint64_t aux_addr;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_gs_container_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_gs_container_params_t) == 72,
                       "scarlett_xgr_gs_container_params_t must be 72 bytes");

typedef enum scarlett_xgr_gs_files_subop_e {
    SCARLETT_XGR_GSF_GET_FOLDER       = 1,
    SCARLETT_XGR_GSF_GET_QUOTA        = 2,
    SCARLETT_XGR_GSF_CLOSE_PROVIDER   = 3
} scarlett_xgr_gs_files_subop_t;

typedef struct scarlett_xgr_gs_files_params_s {
    uint32_t subop;
    uint32_t buf_cap;
    uint64_t user_handle;
    uint64_t scid_addr;
    uint64_t out_path_addr;
    uint64_t out_u64;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_gs_files_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_gs_files_params_t) == 48,
                       "scarlett_xgr_gs_files_params_t must be 48 bytes");

/* ============================================================================
 * Network, libHttpClient, XCurl, XSAPI, PlayFab Structs
 * ============================================================================ */

typedef enum scarlett_xgr_net_config_subop_e {
    SCARLETT_XGR_NET_GET_POLICY       = 1,
    SCARLETT_XGR_NET_CHECK_HOST       = 2,
    SCARLETT_XGR_NET_GET_CA_PATHS     = 3,
    SCARLETT_XGR_NET_REFRESH_ENV      = 4
} scarlett_xgr_net_config_subop_t;

typedef struct scarlett_xgr_net_config_params_s {
    uint32_t subop;
    uint32_t out_net_mode;
    uint64_t host_addr;
    uint32_t port;
    uint32_t out_allowed;
    uint64_t cert_file_addr;
    uint32_t cert_file_cap;
    uint32_t cert_dir_cap;
    uint64_t cert_dir_addr;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_xgr_net_config_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_net_config_params_t) == 56,
                       "scarlett_xgr_net_config_params_t must be 56 bytes");

typedef enum scarlett_xgr_hc_subop_e {
    SCARLETT_XGR_HC_INIT             = 1,
    SCARLETT_XGR_HC_CLEANUP          = 2,
    SCARLETT_XGR_HC_CALL_CREATE      = 3,
    SCARLETT_XGR_HC_CALL_DUP         = 4,
    SCARLETT_XGR_HC_CALL_CLOSE       = 5,
    SCARLETT_XGR_HC_SET_URL          = 6,
    SCARLETT_XGR_HC_SET_BODY         = 7,
    SCARLETT_XGR_HC_SET_HEADER       = 8,
    SCARLETT_XGR_HC_SET_TIMEOUT      = 9,
    SCARLETT_XGR_HC_PERFORM          = 10,
    SCARLETT_XGR_HC_GET_STATUS_CODE  = 11,
    SCARLETT_XGR_HC_GET_NET_ERR      = 12,
    SCARLETT_XGR_HC_GET_RESP_STR     = 13,
    SCARLETT_XGR_HC_GET_RESP_BYTES   = 14,
    SCARLETT_XGR_HC_GET_RESP_HEADER  = 15,
    SCARLETT_XGR_HC_WS_CREATE        = 16,
    SCARLETT_XGR_HC_WS_CONNECT       = 17,
    SCARLETT_XGR_HC_WS_SEND          = 18,
    SCARLETT_XGR_HC_WS_DISCONNECT    = 19,
    SCARLETT_XGR_HC_WS_CLOSE         = 20
} scarlett_xgr_hc_subop_t;

typedef struct scarlett_xgr_hc_params_s {
    uint32_t subop;
    uint32_t u32_a;
    uint64_t handle;
    uint64_t str_a_addr;
    uint64_t str_b_addr;
    uint64_t buf_addr;
    uint64_t buf_size;
    uint64_t out_u64;
    uint32_t out_u32;
    scarlett_hresult_t out_status;
} scarlett_xgr_hc_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_hc_params_t) == 64,
                       "scarlett_xgr_hc_params_t must be 64 bytes");

typedef enum scarlett_xgr_xcurl_subop_e {
    SCARLETT_XGR_XCURL_GLOBAL_INIT     = 1,
    SCARLETT_XGR_XCURL_GLOBAL_CLEANUP  = 2,
    SCARLETT_XGR_XCURL_EASY_INIT       = 3,
    SCARLETT_XGR_XCURL_EASY_CLEANUP    = 4,
    SCARLETT_XGR_XCURL_EASY_SETOPT_STR = 5,
    SCARLETT_XGR_XCURL_EASY_SETOPT_U64 = 6,
    SCARLETT_XGR_XCURL_EASY_PERFORM    = 7,
    SCARLETT_XGR_XCURL_EASY_GETINFO_U64= 8,
    SCARLETT_XGR_XCURL_EASY_GETINFO_STR= 9,
    SCARLETT_XGR_XCURL_MULTI_INIT      = 10,
    SCARLETT_XGR_XCURL_MULTI_CLEANUP   = 11,
    SCARLETT_XGR_XCURL_MULTI_ADD       = 12,
    SCARLETT_XGR_XCURL_MULTI_REMOVE    = 13,
    SCARLETT_XGR_XCURL_MULTI_PERFORM   = 14,
    SCARLETT_XGR_XCURL_MULTI_POLL      = 15,
    SCARLETT_XGR_XCURL_SLIST_APPEND    = 16,
    SCARLETT_XGR_XCURL_SLIST_FREE      = 17
} scarlett_xgr_xcurl_subop_t;

typedef struct scarlett_xgr_xcurl_params_s {
    uint32_t subop;
    uint32_t option_id;
    uint64_t handle;
    uint64_t aux_handle;
    uint64_t value_u64;
    uint64_t str_addr;
    uint32_t buf_cap;
    uint32_t out_curl_code;
    scarlett_hresult_t out_policy_hr;
    scarlett_hresult_t out_status;
} scarlett_xgr_xcurl_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xcurl_params_t) == 56,
                       "scarlett_xgr_xcurl_params_t must be 56 bytes");

typedef enum scarlett_xgr_xsapi_subop_e {
    SCARLETT_XGR_XSAPI_INIT          = 1,
    SCARLETT_XGR_XSAPI_CLEANUP       = 2,
    SCARLETT_XGR_XSAPI_CTX_CREATE    = 3,
    SCARLETT_XGR_XSAPI_CTX_CLOSE     = 4,
    SCARLETT_XGR_XSAPI_CTX_DUP       = 5,
    SCARLETT_XGR_XSAPI_CTX_GET_USER  = 6,
    SCARLETT_XGR_XSAPI_ACH_GET       = 7,
    SCARLETT_XGR_XSAPI_ACH_UPDATE    = 8,
    SCARLETT_XGR_XSAPI_ACH_READ      = 9,
    SCARLETT_XGR_XSAPI_ACH_CLOSE     = 10,
    SCARLETT_XGR_XSAPI_STATS_GET     = 11,
    SCARLETT_XGR_XSAPI_STATS_SET     = 12,
    SCARLETT_XGR_XSAPI_SOCIAL_GET    = 13
} scarlett_xgr_xsapi_subop_t;

typedef struct scarlett_xgr_xsapi_params_s {
    uint32_t subop;
    uint32_t u32_a;
    uint64_t handle;
    uint64_t aux_handle;
    uint64_t str_a_addr;
    uint64_t str_b_addr;
    uint64_t out_buf_addr;
    uint64_t out_u64;
    uint32_t out_u32;
    scarlett_hresult_t out_status;
} scarlett_xgr_xsapi_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_xsapi_params_t) == 64,
                       "scarlett_xgr_xsapi_params_t must be 64 bytes");

typedef enum scarlett_xgr_playfab_subop_e {
    SCARLETT_XGR_PF_INIT             = 1,
    SCARLETT_XGR_PF_UNINIT           = 2,
    SCARLETT_XGR_PF_CFG_CREATE       = 3,
    SCARLETT_XGR_PF_CFG_CLOSE        = 4,
    SCARLETT_XGR_PF_LOGIN_XBOX       = 5,
    SCARLETT_XGR_PF_ENTITY_CLOSE     = 6,
    SCARLETT_XGR_PF_TITLE_DATA_GET   = 7,
    SCARLETT_XGR_PF_TITLE_DATA_SET   = 8,
    SCARLETT_XGR_PF_PLAYER_DATA_GET  = 9,
    SCARLETT_XGR_PF_PLAYER_DATA_SET  = 10
} scarlett_xgr_playfab_subop_t;

typedef struct scarlett_xgr_playfab_params_s {
    uint32_t subop;
    uint32_t buf_cap;
    uint64_t handle;
    uint64_t aux_handle;
    uint64_t str_a_addr;
    uint64_t str_b_addr;
    uint64_t out_buf_addr;
    uint64_t out_u64;
    uint32_t out_used;
    scarlett_hresult_t out_status;
} scarlett_xgr_playfab_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_xgr_playfab_params_t) == 64,
                       "scarlett_xgr_playfab_params_t must be 64 bytes");

/* ============================================================================
 * Host Bridge & Unixlib Dispatch Entry
 * ============================================================================ */

#if !defined(_WIN32)
SCARLETT_XGR_API int32_t scarlett_xgr_unix_call(uint32_t opcode, void *params);
SCARLETT_XGR_API void scarlett_xgr_set_unix_call_bridge(scarlett_unix_call_fn_t fn);
SCARLETT_XGR_API scarlett_unix_call_fn_t scarlett_xgr_get_unix_call_bridge(void);
SCARLETT_XGR_API void scarlett_xgr_set_stub_sink(scarlett_stub_sink_fn_t sink, void *user_data);
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_XGAMERUNTIME_UNIX_ABI_H */
