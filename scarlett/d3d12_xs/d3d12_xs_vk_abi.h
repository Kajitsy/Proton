/*
 * Proton-Scarlett — Self-Contained Vulkan 1.4 C ABI Declarations
 *
 * Clean-room binary-compatible Vulkan 1.0..1.4 & extension ABI declarations
 * matching the official Khronos x86_64 C ABI, loaded strictly at runtime via
 * dlopen("libvulkan.so.1") + vkGetInstanceProcAddr / vkGetDeviceProcAddr.
 *
 * Allows d3d12_xs_unix.so to compile and run across AMD RADV, NVIDIA 595+/610+,
 * Intel ANV, and headless CI environments without requiring system vulkan-headers.
 */

#ifndef SCARLETT_D3D12_XS_VK_ABI_H
#define SCARLETT_D3D12_XS_VK_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "scarlett_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Vulkan Scalar & Handle Types (x86_64 LP64 / Vulkan 1.4)
 * ============================================================================ */

typedef uint32_t ScarVkBool32;
typedef uint32_t ScarVkFlags;
typedef uint64_t ScarVkFlags64;
typedef uint64_t ScarVkDeviceSize;
typedef uint32_t ScarVkSampleMask;

typedef void *ScarVkInstance;
typedef void *ScarVkPhysicalDevice;
typedef void *ScarVkDevice;
typedef void *ScarVkQueue;
typedef void *ScarVkCommandBuffer;

typedef uint64_t ScarVkCommandPool;
typedef uint64_t ScarVkBuffer;
typedef uint64_t ScarVkImage;
typedef uint64_t ScarVkDeviceMemory;
typedef uint64_t ScarVkFence;
typedef uint64_t ScarVkSemaphore;
typedef uint64_t ScarVkDebugUtilsMessengerEXT;
typedef uint64_t ScarVkSurfaceKHR;
typedef uint64_t ScarVkSwapchainKHR;

typedef void (*ScarPFN_vkVoidFunction)(void);

#define SCAR_VK_TRUE  UINT32_C(1)
#define SCAR_VK_FALSE UINT32_C(0)
#define SCAR_VK_NULL_HANDLE UINT64_C(0)
#define SCAR_VK_QUEUE_FAMILY_IGNORED UINT32_C(0xFFFFFFFF)
#define SCAR_VK_WHOLE_SIZE UINT64_C(0xFFFFFFFFFFFFFFFF)
#define SCAR_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256u
#define SCAR_VK_UUID_SIZE 16u
#define SCAR_VK_MAX_EXTENSION_NAME_SIZE 256u
#define SCAR_VK_MAX_DESCRIPTION_SIZE 256u
#define SCAR_VK_MAX_MEMORY_TYPES 32u
#define SCAR_VK_MAX_MEMORY_HEAPS 16u
#define SCAR_VK_MAX_DRIVER_NAME_SIZE 256u
#define SCAR_VK_MAX_DRIVER_INFO_SIZE 256u

#define SCAR_VK_MAKE_API_VERSION(variant, major, minor, patch)                 \
    ((((uint32_t)(variant)) << 29u) | (((uint32_t)(major)) << 22u) |           \
     (((uint32_t)(minor)) << 12u) | ((uint32_t)(patch)))

#define SCAR_VK_API_VERSION_MAJOR(v) (((uint32_t)(v) >> 22u) & 0x7Fu)
#define SCAR_VK_API_VERSION_MINOR(v) (((uint32_t)(v) >> 12u) & 0x3FFu)
#define SCAR_VK_API_VERSION_PATCH(v) ((uint32_t)(v) & 0xFFFu)

#define SCAR_VK_API_VERSION_1_0 SCAR_VK_MAKE_API_VERSION(0u, 1u, 0u, 0u)
#define SCAR_VK_API_VERSION_1_1 SCAR_VK_MAKE_API_VERSION(0u, 1u, 1u, 0u)
#define SCAR_VK_API_VERSION_1_2 SCAR_VK_MAKE_API_VERSION(0u, 1u, 2u, 0u)
#define SCAR_VK_API_VERSION_1_3 SCAR_VK_MAKE_API_VERSION(0u, 1u, 3u, 0u)
#define SCAR_VK_API_VERSION_1_4 SCAR_VK_MAKE_API_VERSION(0u, 1u, 4u, 0u)

/* ============================================================================
 * VkResult Constants
 * ============================================================================ */

typedef int32_t ScarVkResult;

#define SCAR_VK_SUCCESS                       INT32_C(0)
#define SCAR_VK_NOT_READY                     INT32_C(1)
#define SCAR_VK_TIMEOUT                       INT32_C(2)
#define SCAR_VK_INCOMPLETE                    INT32_C(5)
#define SCAR_VK_ERROR_OUT_OF_HOST_MEMORY      INT32_C(-1)
#define SCAR_VK_ERROR_OUT_OF_DEVICE_MEMORY    INT32_C(-2)
#define SCAR_VK_ERROR_INITIALIZATION_FAILED   INT32_C(-3)
#define SCAR_VK_ERROR_DEVICE_LOST             INT32_C(-4)
#define SCAR_VK_ERROR_LAYER_NOT_PRESENT       INT32_C(-6)
#define SCAR_VK_ERROR_EXTENSION_NOT_PRESENT   INT32_C(-7)
#define SCAR_VK_ERROR_INCOMPATIBLE_DRIVER     INT32_C(-9)
#define SCAR_VK_SUBOPTIMAL_KHR                INT32_C(1000001003)
#define SCAR_VK_ERROR_OUT_OF_DATE_KHR         INT32_C(-1000001004)

/* ============================================================================
 * VkStructureType Constants (Exact Khronos Values)
 * ============================================================================ */

#define SCAR_VK_STRUCTURE_TYPE_APPLICATION_INFO                          UINT32_C(0)
#define SCAR_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO                      UINT32_C(1)
#define SCAR_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO                  UINT32_C(2)
#define SCAR_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO                        UINT32_C(3)
#define SCAR_VK_STRUCTURE_TYPE_SUBMIT_INFO                               UINT32_C(4)
#define SCAR_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO                      UINT32_C(5)
#define SCAR_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO                         UINT32_C(8)
#define SCAR_VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO                     UINT32_C(9)
#define SCAR_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO                        UINT32_C(12)
#define SCAR_VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO                         UINT32_C(14)
#define SCAR_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO                  UINT32_C(39)
#define SCAR_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO              UINT32_C(40)
#define SCAR_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO                 UINT32_C(42)
#define SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER                      UINT32_C(45)
#define SCAR_VK_STRUCTURE_TYPE_MEMORY_BARRIER                            UINT32_C(46)

#define SCAR_VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR                 UINT32_C(1000001000)
#define SCAR_VK_STRUCTURE_TYPE_PRESENT_INFO_KHR                          UINT32_C(1000001001)
#define SCAR_VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR              UINT32_C(1000004000)

#define SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2              UINT32_C(1000059001)
#define SCAR_VK_STRUCTURE_TYPE_HDR_METADATA_EXT                          UINT32_C(1000105000)
#define SCAR_VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT     UINT32_C(1000128004)
#define SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES         UINT32_C(1000196000)

/* VK_KHR_synchronization2 / Vulkan 1.3+ Core Enhanced Barriers */
#define SCAR_VK_STRUCTURE_TYPE_MEMORY_BARRIER_2                          UINT32_C(1000314000)
#define SCAR_VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2                   UINT32_C(1000314001)
#define SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2                    UINT32_C(1000314002)
#define SCAR_VK_STRUCTURE_TYPE_DEPENDENCY_INFO                           UINT32_C(1000314003)
#define SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES UINT32_C(1000314007)

/* VK_EXT_device_fault */
#define SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_FAULT_FEATURES_EXT UINT32_C(1000341000)
#define SCAR_VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT                   UINT32_C(1000341001)
#define SCAR_VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT                     UINT32_C(1000341002)

/* ============================================================================
 * Vendor / Driver / Queue / ColorSpace / Barrier Constants
 * ============================================================================ */

#define SCAR_VK_VENDOR_ID_AMD    UINT32_C(0x1002)
#define SCAR_VK_VENDOR_ID_NVIDIA UINT32_C(0x10DE)
#define SCAR_VK_VENDOR_ID_INTEL  UINT32_C(0x8086)

#define SCAR_VK_DRIVER_ID_AMD_PROPRIETARY           UINT32_C(1)
#define SCAR_VK_DRIVER_ID_AMD_OPEN_SOURCE           UINT32_C(2)
#define SCAR_VK_DRIVER_ID_MESA_RADV                 UINT32_C(3)
#define SCAR_VK_DRIVER_ID_NVIDIA_PROPRIETARY        UINT32_C(4)
#define SCAR_VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS UINT32_C(5)
#define SCAR_VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA    UINT32_C(6)
#define SCAR_VK_DRIVER_ID_MESA_LLVMPIPE             UINT32_C(13)
#define SCAR_VK_DRIVER_ID_MESA_NVK                  UINT32_C(25)

#define SCAR_VK_QUEUE_GRAPHICS_BIT       UINT32_C(0x00000001)
#define SCAR_VK_QUEUE_COMPUTE_BIT        UINT32_C(0x00000002)
#define SCAR_VK_QUEUE_TRANSFER_BIT       UINT32_C(0x00000004)
#define SCAR_VK_QUEUE_SPARSE_BINDING_BIT UINT32_C(0x00000008)

#define SCAR_VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT UINT32_C(0x00000002)
#define SCAR_VK_COMMAND_BUFFER_LEVEL_PRIMARY                 UINT32_C(0)
#define SCAR_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT     UINT32_C(0x00000001)

#define SCAR_VK_BUFFER_USAGE_TRANSFER_SRC_BIT                UINT32_C(0x00000001)
#define SCAR_VK_BUFFER_USAGE_TRANSFER_DST_BIT                UINT32_C(0x00000002)
#define SCAR_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT              UINT32_C(0x00000020)
#define SCAR_VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT             UINT32_C(0x00000100)

#define SCAR_VK_SHARING_MODE_EXCLUSIVE                       UINT32_C(0)

#define SCAR_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT             UINT32_C(0x00000001)
#define SCAR_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT             UINT32_C(0x00000002)
#define SCAR_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT            UINT32_C(0x00000004)

#define SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR               UINT32_C(0)
#define SCAR_VK_COLOR_SPACE_BT709_NONLINEAR_EXT              UINT32_C(1000104005)
#define SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT                 UINT32_C(1000104008)

#define SCAR_VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT UINT32_C(0x00000100)
#define SCAR_VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT   UINT32_C(0x00001000)
#define SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     UINT32_C(0x00000001)
#define SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  UINT32_C(0x00000002)
#define SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT UINT32_C(0x00000004)

/* Vulkan 1.3/1.4 Synchronization2 Pipeline Stage & Access Flags */
#define SCAR_VK_PIPELINE_STAGE_2_NONE                        UINT64_C(0)
#define SCAR_VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT             UINT64_C(0x00000001)
#define SCAR_VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT           UINT64_C(0x00000002)
#define SCAR_VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT           UINT64_C(0x00000008)
#define SCAR_VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT         UINT64_C(0x00000080)
#define SCAR_VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT UINT64_C(0x00000400)
#define SCAR_VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT          UINT64_C(0x00000800)
#define SCAR_VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT            UINT64_C(0x00001000)
#define SCAR_VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT          UINT64_C(0x00002000)
#define SCAR_VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT            UINT64_C(0x00008000)
#define SCAR_VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT            UINT64_C(0x00010000)
#define SCAR_VK_PIPELINE_STAGE_2_COPY_BIT                    UINT64_C(0x100000000)

#define SCAR_VK_ACCESS_2_NONE                                UINT64_C(0)
#define SCAR_VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT           UINT64_C(0x00000001)
#define SCAR_VK_ACCESS_2_SHADER_READ_BIT                     UINT64_C(0x00000020)
#define SCAR_VK_ACCESS_2_SHADER_WRITE_BIT                    UINT64_C(0x00000040)
#define SCAR_VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT           UINT64_C(0x00000080)
#define SCAR_VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT          UINT64_C(0x00000100)
#define SCAR_VK_ACCESS_2_TRANSFER_READ_BIT                   UINT64_C(0x00000800)
#define SCAR_VK_ACCESS_2_TRANSFER_WRITE_BIT                  UINT64_C(0x00001000)
#define SCAR_VK_ACCESS_2_MEMORY_READ_BIT                     UINT64_C(0x00008000)
#define SCAR_VK_ACCESS_2_MEMORY_WRITE_BIT                    UINT64_C(0x00010000)

#define SCAR_VK_IMAGE_LAYOUT_UNDEFINED                       UINT32_C(0)
#define SCAR_VK_IMAGE_LAYOUT_GENERAL                         UINT32_C(1)
#define SCAR_VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL        UINT32_C(2)
#define SCAR_VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL UINT32_C(3)
#define SCAR_VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL        UINT32_C(5)
#define SCAR_VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL            UINT32_C(6)
#define SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL            UINT32_C(7)
#define SCAR_VK_IMAGE_LAYOUT_PRESENT_SRC_KHR                 UINT32_C(1000001002)

#define SCAR_VK_IMAGE_ASPECT_COLOR_BIT                       UINT32_C(0x00000001)

#define SCAR_VK_FORMAT_B8G8R8A8_UNORM                        UINT32_C(44)
#define SCAR_VK_FORMAT_B8G8R8A8_SRGB                         UINT32_C(50)

#define SCAR_VK_IMAGE_USAGE_TRANSFER_SRC_BIT                 UINT32_C(0x00000001)
#define SCAR_VK_IMAGE_USAGE_TRANSFER_DST_BIT                 UINT32_C(0x00000002)
#define SCAR_VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT             UINT32_C(0x00000010)
#define SCAR_VK_IMAGE_TYPE_2D                                UINT32_C(1)
#define SCAR_VK_SAMPLE_COUNT_1_BIT                           UINT32_C(1)
#define SCAR_VK_IMAGE_TILING_OPTIMAL                         UINT32_C(0)
#define SCAR_VK_FILTER_LINEAR                                UINT32_C(1)

#define SCAR_VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR           UINT32_C(0x00000001)
#define SCAR_VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR               UINT32_C(0x00000001)
#define SCAR_VK_PRESENT_MODE_IMMEDIATE_KHR                   UINT32_C(0)
#define SCAR_VK_PRESENT_MODE_FIFO_KHR                        UINT32_C(2)

#define SCAR_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT               UINT32_C(0x00000001)
#define SCAR_VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT   UINT32_C(0x00000400)
#define SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT                  UINT32_C(0x00001000)
#define SCAR_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT            UINT32_C(0x00002000)

#define SCAR_VK_ACCESS_TRANSFER_READ_BIT                     UINT32_C(0x00000800)
#define SCAR_VK_ACCESS_TRANSFER_WRITE_BIT                    UINT32_C(0x00001000)

/* Official Extension Names */
#define SCAR_VK_KHR_SURFACE_NAME                             "VK_KHR_surface"
#define SCAR_VK_KHR_XLIB_SURFACE_NAME                        "VK_KHR_xlib_surface"
#define SCAR_VK_KHR_SWAPCHAIN_NAME                           "VK_KHR_swapchain"
#define SCAR_VK_EXT_MESH_SHADER_NAME                         "VK_EXT_mesh_shader"
#define SCAR_VK_KHR_FRAGMENT_SHADING_RATE_NAME               "VK_KHR_fragment_shading_rate"
#define SCAR_VK_EXT_SHADER_IMAGE_ATOMIC_INT64_NAME           "VK_EXT_shader_image_atomic_int64"
#define SCAR_VK_EXT_DEVICE_GENERATED_COMMANDS_NAME           "VK_EXT_device_generated_commands"
#define SCAR_VK_KHR_RAY_QUERY_NAME                           "VK_KHR_ray_query"
#define SCAR_VK_KHR_OPACITY_MICROMAP_NAME                    "VK_KHR_opacity_micromap"
#define SCAR_VK_EXT_DEVICE_FAULT_NAME                        "VK_EXT_device_fault"
#define SCAR_VK_EXT_DESCRIPTOR_HEAP_NAME                     "VK_EXT_descriptor_heap"
#define SCAR_VK_EXT_DESCRIPTOR_BUFFER_NAME                   "VK_EXT_descriptor_buffer"
#define SCAR_VK_EXT_DESCRIPTOR_INDEXING_NAME                 "VK_EXT_descriptor_indexing"
#define SCAR_VK_KHR_SYNCHRONIZATION_2_NAME                   "VK_KHR_synchronization2"
#define SCAR_VK_EXT_SWAPCHAIN_COLOR_SPACE_NAME               "VK_EXT_swapchain_colorspace"
#define SCAR_VK_EXT_HDR_METADATA_NAME                        "VK_EXT_hdr_metadata"
#define SCAR_VK_KHR_PRESENT_WAIT_NAME                        "VK_KHR_present_wait"
#define SCAR_VK_KHR_PRESENT_ID_NAME                          "VK_KHR_present_id"
#define SCAR_VK_GOOGLE_DISPLAY_TIMING_NAME                   "VK_GOOGLE_display_timing"
#define SCAR_VK_EXT_DEBUG_UTILS_NAME                         "VK_EXT_debug_utils"
#define SCAR_VK_LAYER_KHRONOS_VALIDATION_NAME                "VK_LAYER_KHRONOS_validation"

/* ============================================================================
 * Vulkan 1.4 C ABI Structs (Byte-for-Byte Verified Against Khronos Spec)
 * ============================================================================ */

typedef struct ScarVkApplicationInfo {
    uint32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} ScarVkApplicationInfo;

typedef struct ScarVkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    const ScarVkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} ScarVkInstanceCreateInfo;

typedef struct ScarVkExtensionProperties {
    char extensionName[SCAR_VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
} ScarVkExtensionProperties;

typedef struct ScarVkLayerProperties {
    char layerName[SCAR_VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
    uint32_t implementationVersion;
    char description[SCAR_VK_MAX_DESCRIPTION_SIZE];
} ScarVkLayerProperties;

typedef struct ScarVkPhysicalDeviceLimits {
    uint32_t maxImageDimension1D;
    uint32_t maxImageDimension2D;
    uint32_t maxImageDimension3D;
    uint32_t maxImageDimensionCube;
    uint32_t maxImageArrayLayers;
    uint32_t maxTexelBufferElements;
    uint32_t maxUniformBufferRange;
    uint32_t maxStorageBufferRange;
    uint32_t maxPushConstantsSize;
    uint32_t maxMemoryAllocationCount;
    uint32_t maxSamplerAllocationCount;
    ScarVkDeviceSize bufferImageGranularity;
    ScarVkDeviceSize sparseAddressSpaceSize;
    uint32_t maxBoundDescriptorSets;
    uint32_t maxPerStageDescriptorSamplers;
    uint32_t maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageDescriptorStorageBuffers;
    uint32_t maxPerStageDescriptorSampledImages;
    uint32_t maxPerStageDescriptorStorageImages;
    uint32_t maxPerStageDescriptorInputAttachments;
    uint32_t maxPerStageResources;
    uint32_t maxDescriptorSetSamplers;
    uint32_t maxDescriptorSetUniformBuffers;
    uint32_t maxDescriptorSetUniformBuffersDynamic;
    uint32_t maxDescriptorSetStorageBuffers;
    uint32_t maxDescriptorSetStorageBuffersDynamic;
    uint32_t maxDescriptorSetSampledImages;
    uint32_t maxDescriptorSetStorageImages;
    uint32_t maxDescriptorSetInputAttachments;
    uint32_t maxVertexInputAttributes;
    uint32_t maxVertexInputBindings;
    uint32_t maxVertexInputAttributeOffset;
    uint32_t maxVertexInputBindingStride;
    uint32_t maxVertexOutputComponents;
    uint32_t maxTessellationGenerationLevel;
    uint32_t maxTessellationPatchSize;
    uint32_t maxTessellationControlPerVertexInputComponents;
    uint32_t maxTessellationControlPerVertexOutputComponents;
    uint32_t maxTessellationControlPerPatchOutputComponents;
    uint32_t maxTessellationControlTotalOutputComponents;
    uint32_t maxTessellationEvaluationInputComponents;
    uint32_t maxTessellationEvaluationOutputComponents;
    uint32_t maxGeometryShaderInvocations;
    uint32_t maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents;
    uint32_t maxGeometryOutputVertices;
    uint32_t maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents;
    uint32_t maxFragmentOutputAttachments;
    uint32_t maxFragmentDualSrcAttachments;
    uint32_t maxFragmentCombinedOutputResources;
    uint32_t maxComputeSharedMemorySize;
    uint32_t maxComputeWorkGroupCount[3];
    uint32_t maxComputeWorkGroupInvocations;
    uint32_t maxComputeWorkGroupSize[3];
    uint32_t subPixelPrecisionBits;
    uint32_t subTexelPrecisionBits;
    uint32_t mipmapPrecisionBits;
    uint32_t maxDrawIndexedIndexValue;
    uint32_t maxDrawIndirectCount;
    float maxSamplerLodBias;
    float maxSamplerAnisotropy;
    uint32_t maxViewports;
    uint32_t maxViewportDimensions[2];
    float viewportBoundsRange[2];
    uint32_t viewportSubPixelBits;
    size_t minMemoryMapAlignment;
    ScarVkDeviceSize minTexelBufferOffsetAlignment;
    ScarVkDeviceSize minUniformBufferOffsetAlignment;
    ScarVkDeviceSize minStorageBufferOffsetAlignment;
    int32_t minTexelOffset;
    uint32_t maxTexelOffset;
    int32_t minTexelGatherOffset;
    uint32_t maxTexelGatherOffset;
    float minInterpolationOffset;
    float maxInterpolationOffset;
    uint32_t subPixelInterpolationOffsetBits;
    uint32_t maxFramebufferWidth;
    uint32_t maxFramebufferHeight;
    uint32_t maxFramebufferLayers;
    ScarVkFlags framebufferColorSampleCounts;
    ScarVkFlags framebufferDepthSampleCounts;
    ScarVkFlags framebufferStencilSampleCounts;
    ScarVkFlags framebufferNoAttachmentsSampleCounts;
    uint32_t maxColorAttachments;
    ScarVkFlags sampledImageColorSampleCounts;
    ScarVkFlags sampledImageIntegerSampleCounts;
    ScarVkFlags sampledImageDepthSampleCounts;
    ScarVkFlags sampledImageStencilSampleCounts;
    ScarVkFlags storageImageSampleCounts;
    uint32_t maxSampleMaskWords;
    ScarVkBool32 timestampComputeAndGraphics;
    float timestampPeriod;
    uint32_t maxClipDistances;
    uint32_t maxCullDistances;
    uint32_t maxCombinedClipAndCullDistances;
    uint32_t discreteQueuePriorities;
    float pointSizeRange[2];
    float lineWidthRange[2];
    float pointSizeGranularity;
    float lineWidthGranularity;
    ScarVkBool32 strictLines;
    ScarVkBool32 standardSampleLocations;
    ScarVkDeviceSize optimalBufferCopyOffsetAlignment;
    ScarVkDeviceSize optimalBufferCopyRowPitchAlignment;
    ScarVkDeviceSize nonCoherentAtomSize;
} ScarVkPhysicalDeviceLimits;

SCARLETT_STATIC_ASSERT(sizeof(ScarVkPhysicalDeviceLimits) == 504,
                       "VkPhysicalDeviceLimits must be 504 bytes on x86_64");

typedef struct ScarVkPhysicalDeviceSparseProperties {
    ScarVkBool32 residencyStandard2DBlockShape;
    ScarVkBool32 residencyStandard2DMultisampleBlockShape;
    ScarVkBool32 residencyStandard3DBlockShape;
    ScarVkBool32 residencyAlignedMipSize;
    ScarVkBool32 residencyNonResidentStrict;
} ScarVkPhysicalDeviceSparseProperties;

typedef struct ScarVkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[SCAR_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t pipelineCacheUUID[SCAR_VK_UUID_SIZE];
    ScarVkPhysicalDeviceLimits limits;
    ScarVkPhysicalDeviceSparseProperties sparseProperties;
} ScarVkPhysicalDeviceProperties;

SCARLETT_STATIC_ASSERT(sizeof(ScarVkPhysicalDeviceProperties) == 824,
                       "VkPhysicalDeviceProperties must be 824 bytes on x86_64");

typedef struct ScarVkPhysicalDeviceProperties2 {
    uint32_t sType;
    void *pNext;
    ScarVkPhysicalDeviceProperties properties;
} ScarVkPhysicalDeviceProperties2;

SCARLETT_STATIC_ASSERT(sizeof(ScarVkPhysicalDeviceProperties2) == 840,
                       "VkPhysicalDeviceProperties2 must be 840 bytes on x86_64");

typedef struct ScarVkConformanceVersion {
    uint8_t major;
    uint8_t minor;
    uint8_t subminor;
    uint8_t patch;
} ScarVkConformanceVersion;

typedef struct ScarVkPhysicalDeviceDriverProperties {
    uint32_t sType;
    void *pNext;
    uint32_t driverID;
    char driverName[SCAR_VK_MAX_DRIVER_NAME_SIZE];
    char driverInfo[SCAR_VK_MAX_DRIVER_INFO_SIZE];
    ScarVkConformanceVersion conformanceVersion;
} ScarVkPhysicalDeviceDriverProperties;

SCARLETT_STATIC_ASSERT(sizeof(ScarVkPhysicalDeviceDriverProperties) == 536,
                       "VkPhysicalDeviceDriverProperties must be 536 bytes on x86_64");

typedef struct ScarVkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} ScarVkExtent3D;

typedef struct ScarVkQueueFamilyProperties {
    ScarVkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    ScarVkExtent3D minImageTransferGranularity;
} ScarVkQueueFamilyProperties;

typedef struct ScarVkMemoryType {
    ScarVkFlags propertyFlags;
    uint32_t heapIndex;
} ScarVkMemoryType;

typedef struct ScarVkMemoryHeap {
    ScarVkDeviceSize size;
    ScarVkFlags flags;
} ScarVkMemoryHeap;

typedef struct ScarVkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    ScarVkMemoryType memoryTypes[SCAR_VK_MAX_MEMORY_TYPES];
    uint32_t memoryHeapCount;
    ScarVkMemoryHeap memoryHeaps[SCAR_VK_MAX_MEMORY_HEAPS];
} ScarVkPhysicalDeviceMemoryProperties;

typedef struct ScarVkDeviceQueueCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float *pQueuePriorities;
} ScarVkDeviceQueueCreateInfo;

typedef struct ScarVkPhysicalDeviceSynchronization2Features {
    uint32_t sType;
    void *pNext;
    ScarVkBool32 synchronization2;
} ScarVkPhysicalDeviceSynchronization2Features;

typedef struct ScarVkPhysicalDeviceDeviceFaultFeaturesEXT {
    uint32_t sType;
    void *pNext;
    ScarVkBool32 deviceFault;
    ScarVkBool32 deviceFaultVendorBinary;
} ScarVkPhysicalDeviceDeviceFaultFeaturesEXT;

typedef struct ScarVkDeviceCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    uint32_t queueCreateInfoCount;
    const ScarVkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
    const void *pEnabledFeatures;
} ScarVkDeviceCreateInfo;

typedef struct ScarVkCommandPoolCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    uint32_t queueFamilyIndex;
} ScarVkCommandPoolCreateInfo;

typedef struct ScarVkCommandBufferAllocateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkCommandPool commandPool;
    uint32_t level;
    uint32_t commandBufferCount;
} ScarVkCommandBufferAllocateInfo;

typedef struct ScarVkCommandBufferBeginInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    const void *pInheritanceInfo;
} ScarVkCommandBufferBeginInfo;

typedef struct ScarVkSubmitInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const ScarVkSemaphore *pWaitSemaphores;
    const ScarVkFlags *pWaitDstStageMask;
    uint32_t commandBufferCount;
    const ScarVkCommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const ScarVkSemaphore *pSignalSemaphores;
} ScarVkSubmitInfo;

typedef struct ScarVkFenceCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
} ScarVkFenceCreateInfo;

typedef struct ScarVkSemaphoreCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
} ScarVkSemaphoreCreateInfo;

typedef struct ScarVkExtent2D {
    uint32_t width;
    uint32_t height;
} ScarVkExtent2D;

typedef struct ScarVkXlibSurfaceCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    void *dpy;
    unsigned long window;
} ScarVkXlibSurfaceCreateInfoKHR;

typedef struct ScarVkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    ScarVkExtent2D currentExtent;
    ScarVkExtent2D minImageExtent;
    ScarVkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    ScarVkFlags supportedTransforms;
    uint32_t currentTransform;
    ScarVkFlags supportedCompositeAlpha;
    ScarVkFlags supportedUsageFlags;
} ScarVkSurfaceCapabilitiesKHR;

typedef struct ScarVkSurfaceFormatKHR {
    uint32_t format;
    uint32_t colorSpace;
} ScarVkSurfaceFormatKHR;

typedef struct ScarVkSwapchainCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    ScarVkSurfaceKHR surface;
    uint32_t minImageCount;
    uint32_t imageFormat;
    uint32_t imageColorSpace;
    ScarVkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    ScarVkFlags imageUsage;
    uint32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    uint32_t preTransform;
    ScarVkFlags compositeAlpha;
    uint32_t presentMode;
    ScarVkBool32 clipped;
    ScarVkSwapchainKHR oldSwapchain;
} ScarVkSwapchainCreateInfoKHR;

typedef struct ScarVkPresentInfoKHR {
    uint32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const ScarVkSemaphore *pWaitSemaphores;
    uint32_t swapchainCount;
    const ScarVkSwapchainKHR *pSwapchains;
    const uint32_t *pImageIndices;
    ScarVkResult *pResults;
} ScarVkPresentInfoKHR;

typedef union ScarVkClearColorValue {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} ScarVkClearColorValue;

typedef struct ScarVkBufferCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    ScarVkDeviceSize size;
    ScarVkFlags usage;
    uint32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
} ScarVkBufferCreateInfo;

typedef struct ScarVkMemoryRequirements {
    ScarVkDeviceSize size;
    ScarVkDeviceSize alignment;
    uint32_t memoryTypeBits;
} ScarVkMemoryRequirements;

typedef struct ScarVkMemoryAllocateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
} ScarVkMemoryAllocateInfo;

typedef struct ScarVkBufferCopy {
    ScarVkDeviceSize srcOffset;
    ScarVkDeviceSize dstOffset;
    ScarVkDeviceSize size;
} ScarVkBufferCopy;

/* Synchronization2 / Enhanced Barriers */
typedef struct ScarVkMemoryBarrier2 {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags64 srcStageMask;
    ScarVkFlags64 srcAccessMask;
    ScarVkFlags64 dstStageMask;
    ScarVkFlags64 dstAccessMask;
} ScarVkMemoryBarrier2;

typedef struct ScarVkBufferMemoryBarrier2 {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags64 srcStageMask;
    ScarVkFlags64 srcAccessMask;
    ScarVkFlags64 dstStageMask;
    ScarVkFlags64 dstAccessMask;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    ScarVkBuffer buffer;
    ScarVkDeviceSize offset;
    ScarVkDeviceSize size;
} ScarVkBufferMemoryBarrier2;

typedef struct ScarVkImageSubresourceRange {
    ScarVkFlags aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} ScarVkImageSubresourceRange;

typedef struct ScarVkImageMemoryBarrier2 {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags64 srcStageMask;
    ScarVkFlags64 srcAccessMask;
    ScarVkFlags64 dstStageMask;
    ScarVkFlags64 dstAccessMask;
    uint32_t oldLayout;
    uint32_t newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    ScarVkImage image;
    ScarVkImageSubresourceRange subresourceRange;
} ScarVkImageMemoryBarrier2;

typedef struct ScarVkDependencyInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags dependencyFlags;
    uint32_t memoryBarrierCount;
    const ScarVkMemoryBarrier2 *pMemoryBarriers;
    uint32_t bufferMemoryBarrierCount;
    const ScarVkBufferMemoryBarrier2 *pBufferMemoryBarriers;
    uint32_t imageMemoryBarrierCount;
    const ScarVkImageMemoryBarrier2 *pImageMemoryBarriers;
} ScarVkDependencyInfo;

typedef struct ScarVkMemoryBarrier {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags srcAccessMask;
    ScarVkFlags dstAccessMask;
} ScarVkMemoryBarrier;

typedef struct ScarVkImageMemoryBarrier {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags srcAccessMask;
    ScarVkFlags dstAccessMask;
    uint32_t oldLayout;
    uint32_t newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    ScarVkImage image;
    ScarVkImageSubresourceRange subresourceRange;
} ScarVkImageMemoryBarrier;

/* VK_EXT_hdr_metadata (SMPTE ST.2086 HDR10 PQ) */
typedef struct ScarVkXYColorEXT {
    float x;
    float y;
} ScarVkXYColorEXT;

typedef struct ScarVkHdrMetadataEXT {
    uint32_t sType;
    const void *pNext;
    ScarVkXYColorEXT displayPrimaryRed;
    ScarVkXYColorEXT displayPrimaryGreen;
    ScarVkXYColorEXT displayPrimaryBlue;
    ScarVkXYColorEXT whitePoint;
    float maxLuminance;
    float minLuminance;
    float maxContentLightLevel;
    float maxFrameAverageLightLevel;
} ScarVkHdrMetadataEXT;

/* VK_EXT_device_fault */
typedef struct ScarVkDeviceFaultAddressInfoEXT {
    uint32_t addressType;
    uint64_t reportedAddress;
    ScarVkDeviceSize addressPrecision;
} ScarVkDeviceFaultAddressInfoEXT;

typedef struct ScarVkDeviceFaultVendorInfoEXT {
    char description[SCAR_VK_MAX_DESCRIPTION_SIZE];
    uint64_t vendorFaultCode;
    uint64_t vendorFaultData;
} ScarVkDeviceFaultVendorInfoEXT;

typedef struct ScarVkDeviceFaultCountsEXT {
    uint32_t sType;
    void *pNext;
    uint32_t addressInfoCount;
    uint32_t vendorInfoCount;
    ScarVkDeviceSize vendorBinarySize;
} ScarVkDeviceFaultCountsEXT;

typedef struct ScarVkDeviceFaultInfoEXT {
    uint32_t sType;
    void *pNext;
    char description[SCAR_VK_MAX_DESCRIPTION_SIZE];
    ScarVkDeviceFaultAddressInfoEXT *pAddressInfos;
    ScarVkDeviceFaultVendorInfoEXT *pVendorInfos;
    void *pVendorBinaryData;
} ScarVkDeviceFaultInfoEXT;

/* VK_EXT_debug_utils */
typedef struct ScarVkDebugUtilsMessengerCallbackDataEXT {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    const char *pMessageIdName;
    int32_t messageIdNumber;
    const char *pMessage;
    uint32_t queueLabelCount;
    const void *pQueueLabels;
    uint32_t cmdBufLabelCount;
    const void *pCmdBufLabels;
    uint32_t objectCount;
    const void *pObjects;
} ScarVkDebugUtilsMessengerCallbackDataEXT;

typedef ScarVkBool32 (*ScarPFN_vkDebugUtilsMessengerCallbackEXT)(
    ScarVkFlags messageSeverity,
    ScarVkFlags messageTypes,
    const ScarVkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData);

typedef struct ScarVkDebugUtilsMessengerCreateInfoEXT {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    ScarVkFlags messageSeverity;
    ScarVkFlags messageType;
    ScarPFN_vkDebugUtilsMessengerCallbackEXT pfnUserCallback;
    void *pUserData;
} ScarVkDebugUtilsMessengerCreateInfoEXT;

/* ============================================================================
 * Function Pointer Typedefs
 * ============================================================================ */

typedef ScarPFN_vkVoidFunction (*ScarPFN_vkGetInstanceProcAddr)(
    ScarVkInstance instance, const char *pName);
typedef ScarPFN_vkVoidFunction (*ScarPFN_vkGetDeviceProcAddr)(
    ScarVkDevice device, const char *pName);
typedef ScarVkResult (*ScarPFN_vkEnumerateInstanceVersion)(uint32_t *pApiVersion);
typedef ScarVkResult (*ScarPFN_vkEnumerateInstanceLayerProperties)(
    uint32_t *pPropertyCount, ScarVkLayerProperties *pProperties);
typedef ScarVkResult (*ScarPFN_vkEnumerateInstanceExtensionProperties)(
    const char *pLayerName, uint32_t *pPropertyCount, ScarVkExtensionProperties *pProperties);
typedef ScarVkResult (*ScarPFN_vkCreateInstance)(
    const ScarVkInstanceCreateInfo *pCreateInfo, const void *pAllocator, ScarVkInstance *pInstance);
typedef void (*ScarPFN_vkDestroyInstance)(ScarVkInstance instance, const void *pAllocator);

typedef ScarVkResult (*ScarPFN_vkEnumeratePhysicalDevices)(
    ScarVkInstance instance, uint32_t *pPhysicalDeviceCount, ScarVkPhysicalDevice *pPhysicalDevices);
typedef void (*ScarPFN_vkGetPhysicalDeviceProperties)(
    ScarVkPhysicalDevice physicalDevice, ScarVkPhysicalDeviceProperties *pProperties);
typedef void (*ScarPFN_vkGetPhysicalDeviceProperties2)(
    ScarVkPhysicalDevice physicalDevice, ScarVkPhysicalDeviceProperties2 *pProperties);
typedef void (*ScarPFN_vkGetPhysicalDeviceMemoryProperties)(
    ScarVkPhysicalDevice physicalDevice, ScarVkPhysicalDeviceMemoryProperties *pMemoryProperties);
typedef void (*ScarPFN_vkGetPhysicalDeviceQueueFamilyProperties)(
    ScarVkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    ScarVkQueueFamilyProperties *pQueueFamilyProperties);
typedef ScarVkResult (*ScarPFN_vkEnumerateDeviceExtensionProperties)(
    ScarVkPhysicalDevice physicalDevice, const char *pLayerName,
    uint32_t *pPropertyCount, ScarVkExtensionProperties *pProperties);

typedef ScarVkResult (*ScarPFN_vkCreateDevice)(
    ScarVkPhysicalDevice physicalDevice, const ScarVkDeviceCreateInfo *pCreateInfo,
    const void *pAllocator, ScarVkDevice *pDevice);
typedef void (*ScarPFN_vkDestroyDevice)(ScarVkDevice device, const void *pAllocator);
typedef void (*ScarPFN_vkGetDeviceQueue)(
    ScarVkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, ScarVkQueue *pQueue);
typedef ScarVkResult (*ScarPFN_vkQueueWaitIdle)(ScarVkQueue queue);
typedef ScarVkResult (*ScarPFN_vkDeviceWaitIdle)(ScarVkDevice device);

typedef ScarVkResult (*ScarPFN_vkCreateCommandPool)(
    ScarVkDevice device, const ScarVkCommandPoolCreateInfo *pCreateInfo,
    const void *pAllocator, ScarVkCommandPool *pCommandPool);
typedef void (*ScarPFN_vkDestroyCommandPool)(
    ScarVkDevice device, ScarVkCommandPool commandPool, const void *pAllocator);
typedef ScarVkResult (*ScarPFN_vkAllocateCommandBuffers)(
    ScarVkDevice device, const ScarVkCommandBufferAllocateInfo *pAllocateInfo,
    ScarVkCommandBuffer *pCommandBuffers);
typedef void (*ScarPFN_vkFreeCommandBuffers)(
    ScarVkDevice device, ScarVkCommandPool commandPool, uint32_t commandBufferCount,
    const ScarVkCommandBuffer *pCommandBuffers);
typedef ScarVkResult (*ScarPFN_vkBeginCommandBuffer)(
    ScarVkCommandBuffer commandBuffer, const ScarVkCommandBufferBeginInfo *pBeginInfo);
typedef ScarVkResult (*ScarPFN_vkEndCommandBuffer)(ScarVkCommandBuffer commandBuffer);
typedef ScarVkResult (*ScarPFN_vkResetCommandBuffer)(
    ScarVkCommandBuffer commandBuffer, ScarVkFlags flags);

typedef ScarVkResult (*ScarPFN_vkCreateFence)(
    ScarVkDevice device, const ScarVkFenceCreateInfo *pCreateInfo,
    const void *pAllocator, ScarVkFence *pFence);
typedef void (*ScarPFN_vkDestroyFence)(
    ScarVkDevice device, ScarVkFence fence, const void *pAllocator);
typedef ScarVkResult (*ScarPFN_vkWaitForFences)(
    ScarVkDevice device, uint32_t fenceCount, const ScarVkFence *pFences,
    ScarVkBool32 waitAll, uint64_t timeout);
typedef ScarVkResult (*ScarPFN_vkResetFences)(
    ScarVkDevice device, uint32_t fenceCount, const ScarVkFence *pFences);
typedef ScarVkResult (*ScarPFN_vkQueueSubmit)(
    ScarVkQueue queue, uint32_t submitCount, const ScarVkSubmitInfo *pSubmits, ScarVkFence fence);

typedef ScarVkResult (*ScarPFN_vkCreateBuffer)(
    ScarVkDevice device, const ScarVkBufferCreateInfo *pCreateInfo,
    const void *pAllocator, ScarVkBuffer *pBuffer);
typedef void (*ScarPFN_vkDestroyBuffer)(
    ScarVkDevice device, ScarVkBuffer buffer, const void *pAllocator);
typedef void (*ScarPFN_vkGetBufferMemoryRequirements)(
    ScarVkDevice device, ScarVkBuffer buffer, ScarVkMemoryRequirements *pMemoryRequirements);
typedef ScarVkResult (*ScarPFN_vkAllocateMemory)(
    ScarVkDevice device, const ScarVkMemoryAllocateInfo *pAllocateInfo,
    const void *pAllocator, ScarVkDeviceMemory *pMemory);
typedef void (*ScarPFN_vkFreeMemory)(
    ScarVkDevice device, ScarVkDeviceMemory memory, const void *pAllocator);
typedef ScarVkResult (*ScarPFN_vkBindBufferMemory)(
    ScarVkDevice device, ScarVkBuffer buffer, ScarVkDeviceMemory memory,
    ScarVkDeviceSize memoryOffset);
typedef ScarVkResult (*ScarPFN_vkMapMemory)(
    ScarVkDevice device, ScarVkDeviceMemory memory, ScarVkDeviceSize offset,
    ScarVkDeviceSize size, ScarVkFlags flags, void **ppData);
typedef void (*ScarPFN_vkUnmapMemory)(ScarVkDevice device, ScarVkDeviceMemory memory);

typedef void (*ScarPFN_vkCmdPipelineBarrier2)(
    ScarVkCommandBuffer commandBuffer, const ScarVkDependencyInfo *pDependencyInfo);
typedef void (*ScarPFN_vkCmdPipelineBarrier)(
    ScarVkCommandBuffer commandBuffer, ScarVkFlags srcStageMask, ScarVkFlags dstStageMask,
    ScarVkFlags dependencyFlags, uint32_t memoryBarrierCount,
    const ScarVkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount,
    const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
    const void *pImageMemoryBarriers);
typedef void (*ScarPFN_vkCmdCopyBuffer)(
    ScarVkCommandBuffer commandBuffer, ScarVkBuffer srcBuffer, ScarVkBuffer dstBuffer,
    uint32_t regionCount, const ScarVkBufferCopy *pRegions);
typedef void (*ScarPFN_vkCmdDrawIndexedIndirect)(
    ScarVkCommandBuffer commandBuffer, ScarVkBuffer buffer, ScarVkDeviceSize offset,
    uint32_t drawCount, uint32_t stride);
typedef void (*ScarPFN_vkCmdDispatchIndirect)(
    ScarVkCommandBuffer commandBuffer, ScarVkBuffer buffer, ScarVkDeviceSize offset);

typedef ScarVkResult (*ScarPFN_vkCreateDebugUtilsMessengerEXT)(
    ScarVkInstance instance, const ScarVkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const void *pAllocator, ScarVkDebugUtilsMessengerEXT *pMessenger);
typedef void (*ScarPFN_vkDestroyDebugUtilsMessengerEXT)(
    ScarVkInstance instance, ScarVkDebugUtilsMessengerEXT messenger, const void *pAllocator);

typedef ScarVkResult (*ScarPFN_vkGetDeviceFaultInfoEXT)(
    ScarVkDevice device, ScarVkDeviceFaultCountsEXT *pFaultCounts,
    ScarVkDeviceFaultInfoEXT *pFaultInfo);

/* WSI / X11 / Swapchain / Semaphores / Clear */
typedef ScarVkResult (*ScarPFN_vkCreateXlibSurfaceKHR)(
    ScarVkInstance instance, const ScarVkXlibSurfaceCreateInfoKHR *pCreateInfo,
    const void *pAllocator, ScarVkSurfaceKHR *pSurface);
typedef void (*ScarPFN_vkDestroySurfaceKHR)(
    ScarVkInstance instance, ScarVkSurfaceKHR surface, const void *pAllocator);
typedef ScarVkResult (*ScarPFN_vkGetPhysicalDeviceSurfaceSupportKHR)(
    ScarVkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    ScarVkSurfaceKHR surface, ScarVkBool32 *pSupported);
typedef ScarVkResult (*ScarPFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(
    ScarVkPhysicalDevice physicalDevice, ScarVkSurfaceKHR surface,
    ScarVkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
typedef ScarVkResult (*ScarPFN_vkGetPhysicalDeviceSurfaceFormatsKHR)(
    ScarVkPhysicalDevice physicalDevice, ScarVkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount, ScarVkSurfaceFormatKHR *pSurfaceFormats);

typedef ScarVkResult (*ScarPFN_vkCreateSwapchainKHR)(
    ScarVkDevice device, const ScarVkSwapchainCreateInfoKHR *pCreateInfo,
    const void *pAllocator, ScarVkSwapchainKHR *pSwapchain);
typedef void (*ScarPFN_vkDestroySwapchainKHR)(
    ScarVkDevice device, ScarVkSwapchainKHR swapchain, const void *pAllocator);
typedef ScarVkResult (*ScarPFN_vkGetSwapchainImagesKHR)(
    ScarVkDevice device, ScarVkSwapchainKHR swapchain,
    uint32_t *pSwapchainImageCount, ScarVkImage *pSwapchainImages);
typedef ScarVkResult (*ScarPFN_vkAcquireNextImageKHR)(
    ScarVkDevice device, ScarVkSwapchainKHR swapchain, uint64_t timeout,
    ScarVkSemaphore semaphore, ScarVkFence fence, uint32_t *pImageIndex);
typedef ScarVkResult (*ScarPFN_vkQueuePresentKHR)(
    ScarVkQueue queue, const ScarVkPresentInfoKHR *pPresentInfo);

typedef ScarVkResult (*ScarPFN_vkCreateSemaphore)(
    ScarVkDevice device, const ScarVkSemaphoreCreateInfo *pCreateInfo,
    const void *pAllocator, ScarVkSemaphore *pSemaphore);
typedef void (*ScarPFN_vkDestroySemaphore)(
    ScarVkDevice device, ScarVkSemaphore semaphore, const void *pAllocator);
typedef void (*ScarPFN_vkCmdClearColorImage)(
    ScarVkCommandBuffer commandBuffer, ScarVkImage image, uint32_t imageLayout,
    const ScarVkClearColorValue *pColor, uint32_t rangeCount,
    const ScarVkImageSubresourceRange *pRanges);

typedef struct ScarVkOffset3D {
    int32_t x;
    int32_t y;
    int32_t z;
} ScarVkOffset3D;

typedef struct ScarVkImageCreateInfo {
    uint32_t sType;
    const void *pNext;
    ScarVkFlags flags;
    uint32_t imageType;
    uint32_t format;
    ScarVkExtent3D extent;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    uint32_t samples;
    uint32_t tiling;
    ScarVkFlags usage;
    uint32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    uint32_t initialLayout;
} ScarVkImageCreateInfo;

typedef struct ScarVkImageSubresourceLayers {
    ScarVkFlags aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} ScarVkImageSubresourceLayers;

typedef struct ScarVkBufferImageCopy {
    ScarVkDeviceSize bufferOffset;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    ScarVkImageSubresourceLayers imageSubresource;
    ScarVkOffset3D imageOffset;
    ScarVkExtent3D imageExtent;
} ScarVkBufferImageCopy;

typedef struct ScarVkImageBlit {
    ScarVkImageSubresourceLayers srcSubresource;
    ScarVkOffset3D srcOffsets[2];
    ScarVkImageSubresourceLayers dstSubresource;
    ScarVkOffset3D dstOffsets[2];
} ScarVkImageBlit;

typedef ScarVkResult (*ScarPFN_vkCreateImage)(
    ScarVkDevice device, const ScarVkImageCreateInfo *pCreateInfo,
    const void *pAllocator, ScarVkImage *pImage);
typedef void (*ScarPFN_vkDestroyImage)(
    ScarVkDevice device, ScarVkImage image, const void *pAllocator);
typedef void (*ScarPFN_vkGetImageMemoryRequirements)(
    ScarVkDevice device, ScarVkImage image, ScarVkMemoryRequirements *pMemoryRequirements);
typedef ScarVkResult (*ScarPFN_vkBindImageMemory)(
    ScarVkDevice device, ScarVkImage image, ScarVkDeviceMemory memory,
    ScarVkDeviceSize memoryOffset);
typedef void (*ScarPFN_vkCmdCopyBufferToImage)(
    ScarVkCommandBuffer commandBuffer, ScarVkBuffer srcBuffer, ScarVkImage dstImage,
    uint32_t dstImageLayout, uint32_t regionCount, const ScarVkBufferImageCopy *pRegions);
typedef void (*ScarPFN_vkCmdBlitImage)(
    ScarVkCommandBuffer commandBuffer, ScarVkImage srcImage, uint32_t srcImageLayout,
    ScarVkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount,
    const ScarVkImageBlit *pRegions, uint32_t filter);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SCARLETT_D3D12_XS_VK_ABI_H */
