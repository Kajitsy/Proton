/*
 * Proton-Scarlett — Public API for d3d12_xs.dll & d3d12_xs_unix.so
 *
 * Clean-room freestanding C11 / C++20 header matching scarlett/xdl/schemas/d3d12_xs.xdl
 * and scarlett/docs/ABI_CONTRACTS.md.
 *   - Under PE64 compilation (_WIN32): implemented in d3d12_xs_pe.c strictly via
 *     WINE_UNIX_CALL (zero libc / POSIX / libvulkan dependencies).
 *   - Under Linux host tests: d3d12_xs_pe.c links via scarlett_d3d12xs_set_unix_call_bridge()
 *     directly to d3d12_xs_unix.so.
 */

#ifndef SCARLETT_D3D12_XS_API_H
#define SCARLETT_D3D12_XS_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "d3d12_xs_unix_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__MINGW32__)
#define SCARLETT_D3D12XS_API __declspec(dllexport)
#define SCARLETT_D3D12XS_CALL __stdcall
#else
#define SCARLETT_D3D12XS_API __attribute__((visibility("default")))
#define SCARLETT_D3D12XS_CALL
#endif

/* ============================================================================
 * Xbox Series X|S D3D12_XS Public Structs (Matching d3d12_xs.xdl)
 * ============================================================================ */

/* IID_ID3D12DeviceX = {189819F1-1DB6-4B57-BE54-1821339B85F7} */
#define SCARLETT_IID_ID3D12DEVICEX_INIT                                        \
    {                                                                          \
        UINT32_C(0x189819F1), UINT16_C(0x1DB6), UINT16_C(0x4B57),              \
        { 0xBEu, 0x54u, 0x18u, 0x21u, 0x33u, 0x9Bu, 0x85u, 0xF7u }             \
    }

/* Xbox GDK IID_ID3D12DeviceX = {5405C344-D457-444E-B4DD-2366E45AEE39} */
#define SCARLETT_IID_ID3D12DEVICEX_GDK_INIT                                    \
    {                                                                          \
        UINT32_C(0x5405C344), UINT16_C(0xD457), UINT16_C(0x444E),              \
        { 0xB4u, 0xDDu, 0x23u, 0x66u, 0xE4u, 0x5Au, 0xEEu, 0x39u }             \
    }

typedef struct D3D12XBOX_CREATE_DEVICE_PARAMETERS_S {
    uint32_t Version;
    uint32_t ProcessDebugFlags;
    uint32_t GraphicsCommandBufferSizeBytes;
    uint32_t GraphicsScratchMemorySizeBytes;
    uint32_t ComputeCommandBufferSizeBytes;
    uint32_t ComputeScratchMemorySizeBytes;
    uint32_t DmaCommandBufferSizeBytes;
    uint32_t Reserved0;
    uint64_t AutoBreadcrumbNodeCount;
    uint32_t AmplificationShaderIndirectArgsBufferSize; /* GXDK >= 0x585D070E */
    uint32_t AmplificationShaderPayloadBufferSize;      /* GXDK >= 0x585D070E */
    uint64_t DevKitDiagnosticBufferSize;
} D3D12XBOX_CREATE_DEVICE_PARAMETERS;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_CREATE_DEVICE_PARAMETERS) == 56,
                       "D3D12XBOX_CREATE_DEVICE_PARAMETERS must be 56 bytes");

typedef struct D3D12XBOX_VIEW_RECT_S {
    float Left;
    float Top;
    float Right;
    float Bottom;
} D3D12XBOX_VIEW_RECT;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_VIEW_RECT) == 16,
                       "D3D12XBOX_VIEW_RECT must be 16 bytes");

typedef struct D3D12XBOX_RECT_S {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} D3D12XBOX_RECT;

typedef struct D3D12XBOX_PRESENT_PLANE_PARAMETERS_S {
    uint64_t Token;                               /* +0x00 */
    uint32_t ResourceCount;                       /* +0x08 */
    uint32_t _pad0;                               /* +0x0C */
    void * const *ppResources;                    /* +0x10 */
    const D3D12XBOX_RECT *pSrcViewRects;          /* +0x18 */
    const D3D12XBOX_VIEW_RECT *pDestPlacementBase;/* +0x20 */
    uint32_t ColorSpace;                          /* +0x28: scarlett_d3d12xs_color_space_t */
    uint32_t ScaleFilter;                         /* +0x2C */
    uint32_t ExtendedDescCount;                   /* +0x30 */
    uint32_t _pad1;                               /* +0x34 */
    const void *pExtendedDescs;                   /* +0x38 */
    uint32_t Flags;                               /* +0x40 */
    uint32_t _pad2;                               /* +0x44 */
} D3D12XBOX_PRESENT_PLANE_PARAMETERS;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_PRESENT_PLANE_PARAMETERS) == 72,
                       "D3D12XBOX_PRESENT_PLANE_PARAMETERS must be 72 bytes");

typedef struct D3D12XBOX_PRESENT_PARAMETERS_S {
    uint64_t Token;
    uint32_t Flags;
    uint32_t Reserved0;
    float HdrMaxLuminance;
    float HdrMinLuminance;
    float HdrMaxContentLightLevel;
    float HdrMaxFrameAverageLightLevel;
} D3D12XBOX_PRESENT_PARAMETERS;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_PRESENT_PARAMETERS) == 32,
                       "D3D12XBOX_PRESENT_PARAMETERS must be 32 bytes");

typedef struct D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC_S {
    uint64_t Token;
    uint64_t TargetOriginTimestampNs;
    uint32_t FrameIndex;
    uint32_t Flags;
    uint64_t VrrTargetFrameTimeNs;
} D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC) == 32,
                       "D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC must be 32 bytes");

typedef struct D3D12XBOX_RESOURCE_DESC_S {
    uint32_t Dimension;       /* 1=Buffer, 2=Tex1D, 3=Tex2D, 4=Tex3D */
    uint32_t Width;
    uint32_t Height;
    uint32_t DepthOrArraySize;
    uint32_t MipLevels;
    uint32_t Format;
    uint32_t SwizzleMode;
    uint32_t Flags;
    uint64_t ByteSize;
} D3D12XBOX_RESOURCE_DESC;

typedef struct D3D12XBOX_DRAW_INDEXED_ARGUMENTS_S {
    uint32_t IndexCountPerInstance;
    uint32_t InstanceCount;
    uint32_t StartIndexLocation;
    int32_t  BaseVertexLocation;
    uint32_t StartInstanceLocation;
} D3D12XBOX_DRAW_INDEXED_ARGUMENTS;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_DRAW_INDEXED_ARGUMENTS) == 20,
                       "D3D12XBOX_DRAW_INDEXED_ARGUMENTS must be 20 bytes");

typedef struct D3D12XBOX_DISPATCH_ARGUMENTS_S {
    uint32_t ThreadGroupCountX;
    uint32_t ThreadGroupCountY;
    uint32_t ThreadGroupCountZ;
} D3D12XBOX_DISPATCH_ARGUMENTS;

SCARLETT_STATIC_ASSERT(sizeof(D3D12XBOX_DISPATCH_ARGUMENTS) == 12,
                       "D3D12XBOX_DISPATCH_ARGUMENTS must be 12 bytes");

/* ============================================================================
 * COM Interface ID3D12DeviceX (Matching scarlett/xdl/schemas/d3d12_xs.xdl)
 * ============================================================================ */

typedef struct ID3D12DeviceX_S ID3D12DeviceX;

typedef struct ID3D12DeviceXVtbl_S {
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *QueryInterface)(
        ID3D12DeviceX *self,
        const scarlett_guid_t *riid,
        void **ppvObject);
    uint32_t (SCARLETT_D3D12XS_CALL *AddRef)(ID3D12DeviceX *self);
    uint32_t (SCARLETT_D3D12XS_CALL *Release)(ID3D12DeviceX *self);
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *CreatePlacedResourceX)(
        ID3D12DeviceX *self,
        uint64_t gpuAddress,
        const void *pDesc,
        uint32_t InitialResourceState,
        const void *pOptimizedClearValue,
        const scarlett_guid_t *riid,
        void **ppvResource);
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *PresentX)(
        ID3D12DeviceX *self,
        uint32_t PlaneCount,
        const D3D12XBOX_PRESENT_PLANE_PARAMETERS *pPlaneParameters,
        const void *pPresentParameters);
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *WaitFrameEventX)(
        ID3D12DeviceX *self,
        uint32_t FrameEventType,
        uint64_t TimeoutInMilliseconds,
        const D3D12XBOX_FRAME_PIPELINE_TOKEN_DESC *pTokenDesc,
        uint64_t *pOutEventTimestamp);
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *SetAmplificationShaderLimits)(
        ID3D12DeviceX *self,
        uint32_t IndirectArgsBufferSize,
        uint32_t PayloadBufferSize);
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *LegacyPresentSyncX)(
        ID3D12DeviceX *self,
        uint64_t FrameToken,
        uint32_t TimeoutMs);
    scarlett_hresult_t (SCARLETT_D3D12XS_CALL *CreateReservedResourceDebugX)(
        ID3D12DeviceX *self,
        const void *pDesc,
        uint32_t InitialState,
        const scarlett_guid_t *riid,
        void **ppvResource);
    void *extra_slots[247]; /* Slots 9..255 (e.g. slot 26, slot 43, slot 48, slot 73, slot 133) */
} ID3D12DeviceXVtbl;

struct ID3D12DeviceX_S {
    const ID3D12DeviceXVtbl *lpVtbl;
    uint64_t unix_device_handle;
    uint32_t ref_count;
    uint32_t in_use;
    void *real_device;
};

/* ============================================================================
 * Host Unixlib Bridge Hooks (d3d12_xs_unix.so)
 * ============================================================================ */

#if !defined(_WIN32) && !defined(__MINGW32__)
SCARLETT_D3D12XS_API int32_t scarlett_d3d12xs_unix_call(uint32_t opcode, void *params);
SCARLETT_D3D12XS_API void scarlett_d3d12xs_set_unix_call_bridge(scarlett_unix_call_fn_t fn);
SCARLETT_D3D12XS_API scarlett_unix_call_fn_t scarlett_d3d12xs_get_unix_call_bridge(void);
SCARLETT_D3D12XS_API void scarlett_d3d12xs_set_stub_sink(
    scarlett_stub_sink_fn_t sink,
    void *user_data);
#endif

/* ============================================================================
 * PE64 Exported Functions (d3d12_xs.dll)
 * ============================================================================ */

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateDevice(
    void *pAdapter,
    const D3D12XBOX_CREATE_DEVICE_PARAMETERS *pParameters,
    const scarlett_guid_t *riid,
    void **ppDevice);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxDestroyDeviceX(
    uint64_t device_handle);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxQueryDeviceCapsX(
    uint64_t device_handle,
    scarlett_d3d12xs_query_caps_params_t *out_caps);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateCommandQueueX(
    uint64_t device_handle,
    uint32_t queue_type,
    uint32_t priority,
    scarlett_d3d12xs_create_queue_params_t *out_queue_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateCommandListX(
    uint64_t device_handle,
    uint32_t queue_type,
    uint32_t flags,
    uint64_t *out_cmd_list_handle);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreatePlacedResourceX(
    uint64_t device_handle,
    uint64_t gpu_va,
    const D3D12XBOX_RESOURCE_DESC *pDesc,
    uint32_t initial_state,
    uint64_t *out_resource_handle,
    uint32_t *out_uma_tier);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxDestroyResourceX(
    uint64_t device_handle,
    uint64_t resource_handle);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateDescriptorHeapX(
    uint64_t device_handle,
    uint32_t heap_type,
    uint32_t num_descriptors,
    uint32_t flags,
    scarlett_d3d12xs_create_desc_heap_params_t *out_heap_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxWriteDescriptorX(
    scarlett_d3d12xs_write_desc_params_t *params);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxEvaluateDescriptorPolicyX(
    scarlett_d3d12xs_eval_desc_policy_params_t *params);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxRecordEnhancedBarrierX(
    scarlett_d3d12xs_record_barrier_params_t *params);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxExecuteIndirectX(
    scarlett_d3d12xs_record_indirect_params_t *params);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxExecuteCommandListsX(
    uint64_t device_handle,
    uint64_t queue_handle,
    const uint64_t *cmd_list_handles,
    uint32_t cmd_list_count,
    uint64_t uma_region_addr,
    uint64_t uma_region_size,
    scarlett_d3d12xs_execute_cmd_lists_params_t *out_exec_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxStripDxilContainerX(
    const void *src_bytes,
    uint64_t src_size,
    void *dst_bytes,
    uint64_t dst_capacity,
    scarlett_d3d12xs_strip_dxil_params_t *out_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxSetFrameIntervalX(
    uint64_t device_handle,
    uint32_t period_us,
    uint32_t origin_offset_us,
    uint32_t vrr_enabled,
    uint32_t flags);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxScheduleFrameEventX(
    uint64_t device_handle,
    uint32_t event_type,
    uint32_t delay_us,
    uint64_t *out_token,
    uint64_t *out_target_ns);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxWaitFrameEventX(
    uint64_t device_handle,
    uint32_t event_type,
    uint64_t timeout_ms,
    uint64_t expected_token,
    scarlett_d3d12xs_wait_frame_params_t *out_wait_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxPresentX(
    uint64_t device_handle,
    uint32_t plane_count,
    const D3D12XBOX_PRESENT_PLANE_PARAMETERS *pPlaneParameters,
    const D3D12XBOX_PRESENT_PARAMETERS *pPresentParameters,
    scarlett_d3d12xs_presentx_params_t *out_present_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxResizeSwapChainX(
    uint64_t device_handle,
    uint32_t width,
    uint32_t height,
    uint32_t fullscreen,
    uint32_t hdr_enabled,
    uint32_t buffer_count,
    scarlett_d3d12xs_resize_swapchain_params_t *out_resize_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxSuspendX(
    uint64_t device_handle,
    scarlett_d3d12xs_power_state_params_t *out_power_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxResumeX(
    uint64_t device_handle,
    scarlett_d3d12xs_power_state_params_t *out_power_info);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxDumpDredFaultX(
    uint64_t device_handle,
    char *out_buffer,
    uint32_t out_buffer_capacity,
    uint32_t simulate_fault,
    scarlett_d3d12xs_dred_dump_params_t *out_dump_info);

/* Unimplemented exports routed strictly to diagnostic auto-stub trampoline */
SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12GetDebugInterface(
    const scarlett_guid_t *riid,
    void **ppvDebug);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateMeshPipelineState(
    void *pDevice,
    const void *pDesc,
    const scarlett_guid_t *riid,
    void **ppState);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12ConfigureVirtualMemoryX(
    const void *pConfiguration);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12SerializeVersionedRootSignature(
    const void *pRootSignatureDesc,
    void **ppBlob,
    void **ppErrorBlob);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12SerializeRootSignature(
    const void *pRootSignature,
    uint32_t Version,
    void **ppBlob,
    void **ppErrorBlob);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12CreateRootSignatureDeserializer(
    const void *pSrcData,
    uint64_t SrcDataSizeInBytes,
    const scarlett_guid_t *pRootSignatureDeserializerInterface,
    void **ppRootSignatureDeserializer);

SCARLETT_D3D12XS_API scarlett_hresult_t SCARLETT_D3D12XS_CALL D3D12XboxCreateReservedResourceDebugX(
    void *pDevice,
    const void *pDesc,
    uint32_t InitialState,
    const scarlett_guid_t *riid,
    void **ppvResource);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_D3D12_XS_API_H */
