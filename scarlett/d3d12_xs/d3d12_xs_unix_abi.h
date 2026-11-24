/*
 * Proton-Scarlett — d3d12_xs.dll <-> d3d12_xs_unix.so WINE_UNIX_CALL ABI
 *
 * Clean-room freestanding C11 / C++20 header (zero libc, zero <windows.h>,
 * zero Vulkan headers, zero POSIX dependencies).
 *
 * All parameter structures use fixed-width integer/float types and explicit
 * SCARLETT_STATIC_ASSERT size invariants so Microsoft x64 (PE64) and
 * System V AMD64 (Linux ELF64) observe identical layouts.
 */

#ifndef SCARLETT_D3D12_XS_UNIX_ABI_H
#define SCARLETT_D3D12_XS_UNIX_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "scarlett_abi_common.h"
#include "scarlett_errors.h"
#include "scarlett_stub_thunk.h"
#include "scarlett_unix_call.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCARLETT_D3D12XS_UNIX_MODULE_ID UINT32_C(6)

/* ============================================================================
 * Dense WINE_UNIX_CALL Opcode Table (0 .. SCARLETT_D3D12XS_UNIX_OP_COUNT - 1)
 * ============================================================================ */

typedef enum scarlett_d3d12xs_unix_op_e {
    SCARLETT_D3D12XS_UNIX_INIT                    = 0,
    SCARLETT_D3D12XS_UNIX_CREATE_DEVICE           = 1,
    SCARLETT_D3D12XS_UNIX_DESTROY_DEVICE          = 2,
    SCARLETT_D3D12XS_UNIX_QUERY_CAPS              = 3,
    SCARLETT_D3D12XS_UNIX_SET_AMP_SHADER_LIMITS   = 4,
    SCARLETT_D3D12XS_UNIX_CREATE_QUEUE            = 5,
    SCARLETT_D3D12XS_UNIX_CREATE_PLACED_RESOURCE  = 6,
    SCARLETT_D3D12XS_UNIX_DESTROY_RESOURCE        = 7,
    SCARLETT_D3D12XS_UNIX_CREATE_DESC_HEAP        = 8,
    SCARLETT_D3D12XS_UNIX_WRITE_DESCRIPTOR        = 9,
    SCARLETT_D3D12XS_UNIX_EVAL_DESC_POLICY        = 10,
    SCARLETT_D3D12XS_UNIX_CREATE_CMD_LIST         = 11,
    SCARLETT_D3D12XS_UNIX_RECORD_BARRIER          = 12,
    SCARLETT_D3D12XS_UNIX_RECORD_INDIRECT         = 13,
    SCARLETT_D3D12XS_UNIX_EXECUTE_CMD_LISTS       = 14,
    SCARLETT_D3D12XS_UNIX_STRIP_DXIL              = 15,
    SCARLETT_D3D12XS_UNIX_SET_FRAME_INTERVAL      = 16,
    SCARLETT_D3D12XS_UNIX_SCHEDULE_FRAME_EVENT    = 17,
    SCARLETT_D3D12XS_UNIX_WAIT_FRAME_EVENT        = 18,
    SCARLETT_D3D12XS_UNIX_PRESENTX                = 19,
    SCARLETT_D3D12XS_UNIX_RESIZE_SWAPCHAIN        = 20,
    SCARLETT_D3D12XS_UNIX_POWER_STATE             = 21,
    SCARLETT_D3D12XS_UNIX_DUMP_DRED_FAULT         = 22,
    SCARLETT_D3D12XS_UNIX_REPORT_STUB             = 23,
    SCARLETT_D3D12XS_UNIX_LIFT_SHADER             = 24,
    SCARLETT_D3D12XS_UNIX_OP_COUNT                = 25
} scarlett_d3d12xs_unix_op_t;

/* ============================================================================
 * Feature Capability Bitmask & Domain Enums
 * ============================================================================ */

#define SCARLETT_D3D12XS_CAP_VULKAN_1_4               (UINT64_C(1) << 0)
#define SCARLETT_D3D12XS_CAP_MESH_SHADER              (UINT64_C(1) << 1)
#define SCARLETT_D3D12XS_CAP_FRAGMENT_SHADING_RATE    (UINT64_C(1) << 2)
#define SCARLETT_D3D12XS_CAP_IMAGE_ATOMIC_INT64       (UINT64_C(1) << 3) /* Nanite */
#define SCARLETT_D3D12XS_CAP_DEVICE_GENERATED_CMDS    (UINT64_C(1) << 4)
#define SCARLETT_D3D12XS_CAP_RAY_QUERY                (UINT64_C(1) << 5)
#define SCARLETT_D3D12XS_CAP_OPACITY_MICROMAP         (UINT64_C(1) << 6)
#define SCARLETT_D3D12XS_CAP_DEVICE_FAULT             (UINT64_C(1) << 7)
#define SCARLETT_D3D12XS_CAP_DESCRIPTOR_HEAP          (UINT64_C(1) << 8)
#define SCARLETT_D3D12XS_CAP_DESCRIPTOR_BUFFER        (UINT64_C(1) << 9)
#define SCARLETT_D3D12XS_CAP_DESCRIPTOR_INDEXING      (UINT64_C(1) << 10)
#define SCARLETT_D3D12XS_CAP_SYNCHRONIZATION2         (UINT64_C(1) << 11)
#define SCARLETT_D3D12XS_CAP_HDR10_PQ                 (UINT64_C(1) << 12)
#define SCARLETT_D3D12XS_CAP_HDR_METADATA             (UINT64_C(1) << 13)
#define SCARLETT_D3D12XS_CAP_PRESENT_WAIT             (UINT64_C(1) << 14)
#define SCARLETT_D3D12XS_CAP_DISPLAY_TIMING           (UINT64_C(1) << 15)
#define SCARLETT_D3D12XS_CAP_VALIDATION_LAYER         (UINT64_C(1) << 16)
#define SCARLETT_D3D12XS_CAP_REAL_VK_DEVICE           (UINT64_C(1) << 17)
#define SCARLETT_D3D12XS_CAP_SEPARATE_DMA_QUEUE       (UINT64_C(1) << 18)
#define SCARLETT_D3D12XS_CAP_SEPARATE_COMPUTE_QUEUE   (UINT64_C(1) << 19)

/* Xbox D3D12_XS queue types (0x0 Direct, 0x2 DMA/Copy, 0x10 Async Compute XS) */
#define SCARLETT_D3D12_QUEUE_TYPE_DIRECT              UINT32_C(0x00)
/* SCARLETT_D3D12_QUEUE_TYPE_DMA        = 0x02 (from scarlett_abi_common.h) */
/* SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS = 0x10 (from scarlett_abi_common.h) */

/* Descriptor heap types */
typedef enum scarlett_d3d12xs_desc_heap_type_e {
    SCARLETT_DESC_HEAP_CBV_SRV_UAV = 0, /* T# (texture) / V# (buffer) descriptors */
    SCARLETT_DESC_HEAP_SAMPLER     = 1, /* S# (sampler) descriptors */
    SCARLETT_DESC_HEAP_RTV         = 2,
    SCARLETT_DESC_HEAP_DSV         = 3
} scarlett_d3d12xs_desc_heap_type_t;

/* Descriptor slot kinds (RDNA2 T# / V# / S#) */
typedef enum scarlett_d3d12xs_desc_kind_e {
    SCARLETT_DESC_KIND_TEXTURE_T_SHARP = 1, /* 32-byte RDNA2 T# image descriptor */
    SCARLETT_DESC_KIND_BUFFER_V_SHARP  = 2, /* 16-byte RDNA2 V# buffer descriptor */
    SCARLETT_DESC_KIND_SAMPLER_S_SHARP = 3  /* 16-byte RDNA2 S# sampler descriptor */
} scarlett_d3d12xs_desc_kind_t;

/* Reason why descriptor backend policy chose a specific backend */
typedef enum scarlett_d3d12xs_desc_fallback_reason_e {
    SCARLETT_DESC_REASON_NATIVE_HEAP          = 0, /* VK_EXT_descriptor_heap selected */
    SCARLETT_DESC_REASON_NVIDIA_610_GUARD     = 1, /* Mandatory NVIDIA 610.xx driver guard */
    SCARLETT_DESC_REASON_NVIDIA_1M_LIMIT      = 2, /* >1,000,000 descriptors on NVIDIA */
    SCARLETT_DESC_REASON_GLOBAL_1M_LIMIT      = 3, /* >1,000,000 descriptors safe cap */
    SCARLETT_DESC_REASON_HEAP_EXT_MISSING     = 4, /* VK_EXT_descriptor_heap absent */
    SCARLETT_DESC_REASON_BUFFER_EXT_MISSING   = 5  /* Fell back to VK_EXT_descriptor_indexing */
} scarlett_d3d12xs_desc_fallback_reason_t;

/* PresentX color spaces */
typedef enum scarlett_d3d12xs_color_space_e {
    SCARLETT_COLOR_SPACE_G22_P709    = 0, /* SDR Rec.709 (GameDVR / UI plane) */
    SCARLETT_COLOR_SPACE_G2084_P2020 = 1, /* HDR10 PQ Rec.2020 (Primary game plane) */
    SCARLETT_COLOR_SPACE_G10_P709    = 2  /* Linear scRGB */
} scarlett_d3d12xs_color_space_t;

/* Frame event types for ScheduleFrameEventX / WaitFrameEventX */
typedef enum scarlett_d3d12xs_frame_event_e {
    SCARLETT_FRAME_EVENT_ORIGIN         = 0,
    SCARLETT_FRAME_EVENT_PRESENT        = 1,
    SCARLETT_FRAME_EVENT_BEFORE_PRESENT = 2,
    SCARLETT_FRAME_EVENT_AFTER_PRESENT  = 3
} scarlett_d3d12xs_frame_event_t;

/* Enhanced Barrier kinds */
typedef enum scarlett_d3d12xs_barrier_kind_e {
    SCARLETT_BARRIER_KIND_GLOBAL = 0, /* VkMemoryBarrier2 */
    SCARLETT_BARRIER_KIND_BUFFER = 1, /* VkBufferMemoryBarrier2 */
    SCARLETT_BARRIER_KIND_IMAGE  = 2  /* VkImageMemoryBarrier2 */
} scarlett_d3d12xs_barrier_kind_t;

/* ExecuteIndirect command kinds */
typedef enum scarlett_d3d12xs_indirect_kind_e {
    SCARLETT_INDIRECT_DRAW_INDEXED = 0,
    SCARLETT_INDIRECT_DISPATCH     = 1
} scarlett_d3d12xs_indirect_kind_t;

/* Power state transitions */
typedef enum scarlett_d3d12xs_power_op_e {
    SCARLETT_POWER_OP_SUSPEND = 0,
    SCARLETT_POWER_OP_RESUME  = 1
} scarlett_d3d12xs_power_op_t;

/* ============================================================================
 * Fixed-Width Parameter Structures (PE64 <-> Unixlib)
 * ============================================================================ */

typedef struct scarlett_d3d12xs_create_device_params_s {
    uint32_t gxdk_version; /* SCARLETT_GXDK_MAGIC = 0x585D070E or higher */
    uint32_t process_debug_flags;
    uint32_t graphics_cmd_buf_bytes;
    uint32_t graphics_scratch_bytes;
    uint32_t compute_cmd_buf_bytes;
    uint32_t compute_scratch_bytes;
    uint32_t dma_cmd_buf_bytes;
    uint32_t amp_shader_indirect_args_bytes; /* GXDK >= 0x585D070E */
    uint32_t amp_shader_payload_bytes;       /* GXDK >= 0x585D070E */
    uint32_t request_validation;             /* 1 to enable VK_LAYER_KHRONOS_validation if present */
    uint64_t auto_breadcrumb_node_count;
    uint64_t devkit_diagnostic_buffer_bytes;
    uint64_t out_device_handle;
    uint64_t out_caps_mask;
    uint32_t out_vk_api_version;
    uint32_t out_vendor_id;
    uint32_t out_device_id;
    uint32_t out_driver_id;
    uint32_t out_driver_major;
    uint32_t out_driver_minor;
    uint32_t out_driver_patch;
    uint32_t out_desc_backend;               /* scarlett_descriptor_backend_t */
    uint32_t out_desc_fallback_reason;       /* scarlett_d3d12xs_desc_fallback_reason_t */
    uint32_t out_validation_errors;
    uint32_t out_direct_queue_family;
    uint32_t out_compute_queue_family;
    uint32_t out_dma_queue_family;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_create_device_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_create_device_params_t) == 128,
                       "scarlett_d3d12xs_create_device_params_t must be 128 bytes");

typedef struct scarlett_d3d12xs_destroy_device_params_s {
    uint64_t device_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_destroy_device_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_destroy_device_params_t) == 16,
                       "scarlett_d3d12xs_destroy_device_params_t must be 16 bytes");

typedef struct scarlett_d3d12xs_query_caps_params_s {
    uint64_t device_handle;
    uint64_t out_caps_mask;
    uint32_t out_vk_api_version;
    uint32_t out_vendor_id;
    uint32_t out_device_id;
    uint32_t out_driver_id;
    uint32_t out_driver_major;
    uint32_t out_driver_minor;
    uint32_t out_driver_patch;
    uint32_t out_desc_backend;
    uint32_t out_direct_queue_family;
    uint32_t out_compute_queue_family;
    uint32_t out_dma_queue_family;
    uint32_t out_validation_errors;
    uint32_t out_amp_indirect_bytes;
    uint32_t out_amp_payload_bytes;
    uint32_t out_swapchain_generation;
    uint32_t out_is_suspended;
    uint64_t out_uma_pre_submit_flushes;
    uint64_t out_uma_present_flushes;
    uint64_t out_total_presents;
    char out_device_name[128];
    char out_driver_info[128];
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_query_caps_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_query_caps_params_t) == 368,
                       "scarlett_d3d12xs_query_caps_params_t must be 368 bytes");

typedef struct scarlett_d3d12xs_set_amp_limits_params_s {
    uint64_t device_handle;
    uint32_t indirect_args_buffer_bytes;
    uint32_t payload_buffer_bytes;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_set_amp_limits_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_set_amp_limits_params_t) == 24,
                       "scarlett_d3d12xs_set_amp_limits_params_t must be 24 bytes");

typedef struct scarlett_d3d12xs_create_queue_params_s {
    uint64_t device_handle;
    uint32_t queue_type; /* 0x0 Direct, 0x2 DMA/Copy, 0x10 Async Compute XS */
    uint32_t priority;
    uint64_t out_queue_handle;
    uint32_t out_vk_queue_family;
    uint32_t out_vk_queue_index;
    uint32_t out_is_dedicated_family;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_create_queue_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_create_queue_params_t) == 40,
                       "scarlett_d3d12xs_create_queue_params_t must be 40 bytes");

typedef struct scarlett_d3d12xs_create_placed_resource_params_s {
    uint64_t device_handle;
    uint64_t gpu_va;           /* Must lie within MEM_GRAPHICS [0x100000000..0xFFFFFFFFFF] */
    uint64_t size_bytes;
    uint32_t dimension;        /* 1=Buffer, 2=Tex1D, 3=Tex2D, 4=Tex3D */
    uint32_t width;
    uint32_t height;
    uint32_t depth_or_array;
    uint32_t mip_levels;
    uint32_t dxgi_format;
    uint32_t swizzle_mode;
    uint32_t initial_state;
    uint64_t out_resource_handle;
    uint32_t out_uma_tier;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_create_placed_resource_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_create_placed_resource_params_t) == 72,
                       "scarlett_d3d12xs_create_placed_resource_params_t must be 72 bytes");

typedef struct scarlett_d3d12xs_destroy_resource_params_s {
    uint64_t device_handle;
    uint64_t resource_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_destroy_resource_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_destroy_resource_params_t) == 24,
                       "scarlett_d3d12xs_destroy_resource_params_t must be 24 bytes");

typedef struct scarlett_d3d12xs_create_desc_heap_params_s {
    uint64_t device_handle;
    uint32_t heap_type;          /* scarlett_d3d12xs_desc_heap_type_t */
    uint32_t num_descriptors;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t out_heap_handle;
    uint64_t out_gpu_va_base;
    uint32_t out_descriptor_stride;
    uint32_t out_selected_backend;   /* scarlett_descriptor_backend_t */
    uint32_t out_fallback_reason;    /* scarlett_d3d12xs_desc_fallback_reason_t */
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_create_desc_heap_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_create_desc_heap_params_t) == 56,
                       "scarlett_d3d12xs_create_desc_heap_params_t must be 56 bytes");

typedef struct scarlett_d3d12xs_write_desc_params_s {
    uint64_t device_handle;
    uint64_t heap_handle;
    uint32_t slot_index;
    uint32_t desc_kind;      /* scarlett_d3d12xs_desc_kind_t (T#, V#, S#) */
    uint64_t resource_va;
    uint64_t resource_size;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t mip_or_filter;
    uint64_t out_encoded_word0;
    uint64_t out_encoded_word1;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_write_desc_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_write_desc_params_t) == 80,
                       "scarlett_d3d12xs_write_desc_params_t must be 80 bytes");

/* Deterministic policy matrix evaluation (calls exact production policy engine) */
typedef struct scarlett_d3d12xs_eval_desc_policy_params_s {
    uint32_t vendor_id;
    uint32_t driver_id;
    uint32_t raw_driver_version;
    uint32_t requested_descriptors;
    uint32_t has_desc_heap_ext;
    uint32_t has_desc_buffer_ext;
    uint32_t has_desc_indexing_ext;
    uint32_t reserved0;
    char driver_info[64];
    uint32_t out_parsed_major;
    uint32_t out_parsed_minor;
    uint32_t out_parsed_patch;
    uint32_t out_is_nvidia_610;
    uint32_t out_selected_backend;
    uint32_t out_fallback_reason;
    scarlett_hresult_t out_status;
    uint32_t reserved1;
} scarlett_d3d12xs_eval_desc_policy_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_eval_desc_policy_params_t) == 128,
                       "scarlett_d3d12xs_eval_desc_policy_params_t must be 128 bytes");

typedef struct scarlett_d3d12xs_create_cmd_list_params_s {
    uint64_t device_handle;
    uint32_t queue_type;
    uint32_t flags;
    uint64_t out_cmd_list_handle;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_create_cmd_list_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_create_cmd_list_params_t) == 32,
                       "scarlett_d3d12xs_create_cmd_list_params_t must be 32 bytes");

typedef struct scarlett_d3d12xs_record_barrier_params_s {
    uint64_t device_handle;
    uint64_t cmd_list_handle;
    uint64_t resource_handle;
    uint32_t barrier_kind;    /* scarlett_d3d12xs_barrier_kind_t */
    uint32_t layout_before;
    uint32_t layout_after;
    uint32_t reserved0;
    uint64_t sync_before;
    uint64_t sync_after;
    uint64_t access_before;
    uint64_t access_after;
    uint64_t offset;
    uint64_t size;
    uint32_t out_used_sync2;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_record_barrier_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_record_barrier_params_t) == 96,
                       "scarlett_d3d12xs_record_barrier_params_t must be 96 bytes");

typedef struct scarlett_d3d12xs_record_indirect_params_s {
    uint64_t device_handle;
    uint64_t cmd_list_handle;
    uint32_t indirect_kind;     /* scarlett_d3d12xs_indirect_kind_t */
    uint32_t max_command_count;
    uint32_t arg_stride;
    uint32_t force_cpu_fallback;
    uint64_t arg_buffer_addr;   /* Host/UMA address of indirect arguments */
    uint64_t arg_buffer_offset;
    uint64_t count_buffer_addr; /* Optional host/UMA uint32_t count buffer */
    uint64_t count_buffer_offset;
    uint32_t out_used_dgc;      /* 1 = VK_EXT_device_generated_commands, 0 = CPU indirect fallback */
    uint32_t out_commands_parsed;
    uint64_t out_total_indices_or_groups;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_record_indirect_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_record_indirect_params_t) == 88,
                       "scarlett_d3d12xs_record_indirect_params_t must be 88 bytes");

typedef struct scarlett_d3d12xs_execute_cmd_lists_params_s {
    uint64_t device_handle;
    uint64_t queue_handle;
    uint64_t cmd_list_handles_addr; /* const uint64_t[cmd_list_count] */
    uint32_t cmd_list_count;
    uint32_t flags;
    uint64_t uma_region_addr;       /* 0 to flush all dirty MEM_GRAPHICS pages */
    uint64_t uma_region_size;
    uint64_t out_flushed_pages;
    uint64_t out_flushed_bytes;
    uint64_t out_submit_serial;
    uint32_t out_uma_flushed_pre_submit;
    uint32_t out_vk_submitted;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_execute_cmd_lists_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_execute_cmd_lists_params_t) == 88,
                       "scarlett_d3d12xs_execute_cmd_lists_params_t must be 88 bytes");

typedef struct scarlett_d3d12xs_strip_dxil_params_s {
    uint64_t src_addr;
    uint64_t src_size;
    uint64_t dst_addr;
    uint64_t dst_capacity;
    uint64_t out_dst_size;
    uint32_t out_input_chunks;
    uint32_t out_output_chunks;
    uint32_t out_stripped_chunks;
    uint32_t out_has_valid_dxil;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_strip_dxil_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_strip_dxil_params_t) == 64,
                       "scarlett_d3d12xs_strip_dxil_params_t must be 64 bytes");

typedef struct scarlett_d3d12xs_frame_interval_params_s {
    uint64_t device_handle;
    uint32_t period_us;         /* e.g. 16667us for 60Hz, 8333us for 120Hz */
    uint32_t origin_offset_us;
    uint32_t vrr_enabled;
    uint32_t flags;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_frame_interval_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_frame_interval_params_t) == 32,
                       "scarlett_d3d12xs_frame_interval_params_t must be 32 bytes");

typedef struct scarlett_d3d12xs_schedule_frame_params_s {
    uint64_t device_handle;
    uint32_t event_type;        /* scarlett_d3d12xs_frame_event_t */
    uint32_t delay_us;
    uint64_t out_token;         /* Monotonic 64-bit D3D12XBOX_FRAME_PIPELINE_TOKEN */
    uint64_t out_target_ns;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_schedule_frame_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_schedule_frame_params_t) == 40,
                       "scarlett_d3d12xs_schedule_frame_params_t must be 40 bytes");

typedef struct scarlett_d3d12xs_wait_frame_params_s {
    uint64_t device_handle;
    uint32_t event_type;
    uint32_t flags;
    uint64_t timeout_ms;
    uint64_t expected_token;    /* 0 = wait next scheduled/vblank slot */
    uint64_t out_token;
    uint64_t out_event_timestamp_ns;
    uint64_t out_target_timestamp_ns;
    int64_t  out_jitter_ns;     /* actual - target (signed nanoseconds) */
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_wait_frame_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_wait_frame_params_t) == 72,
                       "scarlett_d3d12xs_wait_frame_params_t must be 72 bytes");

typedef struct scarlett_d3d12xs_present_plane_s {
    uint64_t resource_handle;
    uint64_t gpu_va;
    uint32_t subresource_index;
    uint32_t flags;
    uint32_t src_left;
    uint32_t src_top;
    uint32_t src_right;
    uint32_t src_bottom;
    uint32_t color_space;       /* scarlett_d3d12xs_color_space_t */
    uint32_t width;
    uint32_t height;
    uint32_t row_pitch;
    uint32_t dxgi_format;
    uint32_t reserved;
} scarlett_d3d12xs_present_plane_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_present_plane_t) == 64,
                       "scarlett_d3d12xs_present_plane_t must be 64 bytes");

typedef struct scarlett_d3d12xs_presentx_params_s {
    uint64_t device_handle;
    uint64_t frame_token;
    uint64_t host_x11_window;
    uint32_t plane_count;       /* 0, 1, or 2 */
    uint32_t flags;
    scarlett_d3d12xs_present_plane_t planes[2];
    float hdr_max_luminance;    /* SMPTE ST.2086 nits (e.g. 1000.0f) */
    float hdr_min_luminance;    /* e.g. 0.005f */
    float hdr_max_cll;
    float hdr_max_fall;
    uint32_t out_vk_color_space_plane0;
    uint32_t out_vk_color_space_plane1;
    uint32_t out_gamedvr_captured;
    uint32_t out_swapchain_generation;
    uint64_t out_present_serial;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_presentx_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_presentx_params_t) == 208,
                       "scarlett_d3d12xs_presentx_params_t must be 208 bytes");

typedef struct scarlett_d3d12xs_resize_swapchain_params_s {
    uint64_t device_handle;
    uint32_t width;
    uint32_t height;
    uint32_t fullscreen;        /* Alt+Enter toggle */
    uint32_t hdr_enabled;
    uint32_t buffer_count;
    uint32_t out_generation;
    uint32_t out_drained_queues;
    uint32_t out_preserved_resources;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_resize_swapchain_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_resize_swapchain_params_t) == 48,
                       "scarlett_d3d12xs_resize_swapchain_params_t must be 48 bytes");

typedef struct scarlett_d3d12xs_power_state_params_s {
    uint64_t device_handle;
    uint32_t power_op;          /* scarlett_d3d12xs_power_op_t */
    uint32_t flags;
    uint32_t out_is_suspended;
    uint32_t out_swapchain_generation;
    uint32_t out_preserved_resources;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_power_state_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_power_state_params_t) == 32,
                       "scarlett_d3d12xs_power_state_params_t must be 32 bytes");

typedef struct scarlett_d3d12xs_dred_dump_params_s {
    uint64_t device_handle;
    uint64_t out_buf_addr;      /* Optional caller buffer for formatted DRED text */
    uint32_t out_buf_capacity;
    uint32_t simulate_fault;    /* 1 to emit synthetic fault context when GPU healthy */
    uint32_t out_bytes_written;
    uint32_t out_breadcrumb_count;
    uint32_t out_vk_fault_queried;
    uint32_t out_proton_log_written;
    scarlett_hresult_t out_status;
    uint32_t reserved;
} scarlett_d3d12xs_dred_dump_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_dred_dump_params_t) == 48,
                       "scarlett_d3d12xs_dred_dump_params_t must be 48 bytes");

typedef struct scarlett_d3d12xs_report_stub_params_s {
    scarlett_stub_context_t ctx;
    uint32_t out_action;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_report_stub_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_report_stub_params_t) == 480,
                       "scarlett_d3d12xs_report_stub_params_t must be 480 bytes");

typedef struct scarlett_d3d12xs_lift_shader_params_s {
    uint64_t src_addr;
    uint64_t src_size;
    uint64_t dst_addr;
    uint64_t dst_capacity;
    uint64_t out_dst_size;
    uint32_t shader_mode;        /* 0=env/adaptive, 1=exact, 2=adaptive, 3=fast_f32 */
    uint32_t prefer_ray_query;
    uint32_t out_used_dxil_fast_path;
    uint32_t out_container_kind; /* 0=unknown, 1=DXBC, 2=XS2, 3=FSSM, 4=PS09, 5=SGD5 */
    uint32_t out_wave_strategy;
    scarlett_hresult_t out_status;
} scarlett_d3d12xs_lift_shader_params_t;

SCARLETT_STATIC_ASSERT(sizeof(scarlett_d3d12xs_lift_shader_params_t) == 64,
                       "scarlett_d3d12xs_lift_shader_params_t must be 64 bytes");

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_D3D12_XS_UNIX_ABI_H */
