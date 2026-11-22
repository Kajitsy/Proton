/*
 * Proton-Scarlett — Internal C++20 Architecture for d3d12_xs_unix.so
 */

#ifndef SCARLETT_D3D12_XS_INTERNAL_HPP
#define SCARLETT_D3D12_XS_INTERNAL_HPP

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "d3d12_xs_api.h"
#include "d3d12_xs_vk_abi.h"

namespace scarlett::d3d12xs::unixlib {

/* ============================================================================
 * Parsed Driver Version & Descriptor Heap Policy (NVIDIA 610.xx / 1M Guard)
 * ============================================================================ */

struct DriverVersionParsed {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
    bool parsed_from_info = false;
};

struct DescriptorPolicyInput {
    uint32_t vendor_id = 0;
    uint32_t driver_id = 0;
    uint32_t raw_driver_version = 0;
    std::string driver_info;
    uint32_t requested_descriptors = 0;
    bool has_descriptor_heap_ext = false;
    bool has_descriptor_buffer_ext = false;
    bool has_descriptor_indexing_ext = false;
};

struct DescriptorPolicyDecision {
    DriverVersionParsed parsed_version{};
    bool is_nvidia = false;
    bool is_nvidia_610_guard = false;
    scarlett_descriptor_backend_t selected_backend = SCARLETT_DESC_BACKEND_DESCRIPTOR_INDEXING;
    scarlett_d3d12xs_desc_fallback_reason_t reason = SCARLETT_DESC_REASON_HEAP_EXT_MISSING;
};

DriverVersionParsed parse_driver_version(
    uint32_t vendor_id,
    uint32_t driver_id,
    uint32_t raw_driver_version,
    const char *driver_info) noexcept;

DescriptorPolicyDecision evaluate_descriptor_policy(const DescriptorPolicyInput &in) noexcept;

/* ============================================================================
 * Descriptor Heap (T# / V# / S#) Storage & Encoder
 * ============================================================================ */

struct DescriptorSlot {
    uint32_t kind = 0;
    uint64_t words[4] = {0, 0, 0, 0};
};

struct DescriptorHeapObject {
    uint64_t handle = 0;
    uint32_t heap_type = 0;
    uint32_t num_descriptors = 0;
    uint32_t flags = 0;
    uint32_t stride_bytes = 32;
    uint64_t gpu_va_base = 0;
    scarlett_descriptor_backend_t backend = SCARLETT_DESC_BACKEND_DESCRIPTOR_INDEXING;
    scarlett_d3d12xs_desc_fallback_reason_t reason = SCARLETT_DESC_REASON_NATIVE_HEAP;
    std::vector<DescriptorSlot> slots;
};

scarlett_hresult_t encode_rdna2_descriptor(
    const scarlett_d3d12xs_write_desc_params_t &req,
    DescriptorSlot &out_slot) noexcept;

/* ============================================================================
 * DXIL Fast-Path (Path A): DXBC Container Parser & XBOX/XS2M Stripper
 * ============================================================================ */

scarlett_hresult_t strip_xbox_dxil_container(
    const uint8_t *src,
    uint64_t src_size,
    uint8_t *dst,
    uint64_t dst_capacity,
    scarlett_d3d12xs_strip_dxil_params_t &out_info) noexcept;

/* ============================================================================
 * Dynamic Vulkan 1.4 Runtime Context (dlopen("libvulkan.so.1"))
 * ============================================================================ */

struct VulkanRuntimeContext {
    void *dl_handle = nullptr;
    bool available = false;
    bool real_device_created = false;
    bool validation_layer_enabled = false;

    ScarPFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    ScarPFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    ScarPFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr;
    ScarPFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
    ScarPFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
    ScarPFN_vkCreateInstance vkCreateInstance = nullptr;
    ScarPFN_vkDestroyInstance vkDestroyInstance = nullptr;

    ScarPFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
    ScarPFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    ScarPFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = nullptr;
    ScarPFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
    ScarPFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    ScarPFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;

    ScarPFN_vkCreateDevice vkCreateDevice = nullptr;
    ScarPFN_vkDestroyDevice vkDestroyDevice = nullptr;
    ScarPFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
    ScarPFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;
    ScarPFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;

    ScarPFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
    ScarPFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
    ScarPFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
    ScarPFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
    ScarPFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
    ScarPFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
    ScarPFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;

    ScarPFN_vkCreateFence vkCreateFence = nullptr;
    ScarPFN_vkDestroyFence vkDestroyFence = nullptr;
    ScarPFN_vkWaitForFences vkWaitForFences = nullptr;
    ScarPFN_vkResetFences vkResetFences = nullptr;
    ScarPFN_vkQueueSubmit vkQueueSubmit = nullptr;

    ScarPFN_vkCreateBuffer vkCreateBuffer = nullptr;
    ScarPFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
    ScarPFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
    ScarPFN_vkAllocateMemory vkAllocateMemory = nullptr;
    ScarPFN_vkFreeMemory vkFreeMemory = nullptr;
    ScarPFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
    ScarPFN_vkMapMemory vkMapMemory = nullptr;
    ScarPFN_vkUnmapMemory vkUnmapMemory = nullptr;

    ScarPFN_vkCmdPipelineBarrier2 vkCmdPipelineBarrier2 = nullptr;
    ScarPFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
    ScarPFN_vkCmdCopyBuffer vkCmdCopyBuffer = nullptr;
    ScarPFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect = nullptr;
    ScarPFN_vkCmdDispatchIndirect vkCmdDispatchIndirect = nullptr;

    ScarPFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
    ScarPFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    ScarPFN_vkGetDeviceFaultInfoEXT vkGetDeviceFaultInfoEXT = nullptr;

    ScarVkInstance instance = nullptr;
    ScarVkDebugUtilsMessengerEXT debug_messenger = SCAR_VK_NULL_HANDLE;
    ScarVkPhysicalDevice physical_device = nullptr;
    ScarVkDevice device = nullptr;

    uint32_t instance_api_version = SCAR_VK_API_VERSION_1_0;
    uint32_t device_api_version = SCAR_VK_API_VERSION_1_0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint32_t driver_id = 0;
    uint32_t raw_driver_version = 0;
    DriverVersionParsed driver_version{};
    std::string device_name;
    std::string driver_name;
    std::string driver_info;

    uint64_t caps_mask = 0;

    /* Separated Vulkan Queue Families (Direct=0x0, Async Compute=0x10, DMA=0x2) */
    uint32_t direct_queue_family = SCAR_VK_QUEUE_FAMILY_IGNORED;
    uint32_t compute_queue_family = SCAR_VK_QUEUE_FAMILY_IGNORED;
    uint32_t dma_queue_family = SCAR_VK_QUEUE_FAMILY_IGNORED;
    bool separate_compute_family = false;
    bool separate_dma_family = false;

    ScarVkQueue direct_queue = nullptr;
    ScarVkQueue compute_queue = nullptr;
    ScarVkQueue dma_queue = nullptr;

    ScarVkCommandPool direct_pool = SCAR_VK_NULL_HANDLE;
    ScarVkCommandPool compute_pool = SCAR_VK_NULL_HANDLE;
    ScarVkCommandPool dma_pool = SCAR_VK_NULL_HANDLE;

    ScarVkPhysicalDeviceMemoryProperties mem_props{};

    std::atomic<uint32_t> validation_error_count{0};
    std::atomic<uint32_t> validation_warning_count{0};
    std::mutex log_mutex;
    std::vector<std::string> validation_messages;

    VulkanRuntimeContext() = default;
    ~VulkanRuntimeContext();

    VulkanRuntimeContext(const VulkanRuntimeContext &) = delete;
    VulkanRuntimeContext &operator=(const VulkanRuntimeContext &) = delete;

    bool initialize(bool request_validation);
    void shutdown();

    ScarVkQueue queue_for_type(uint32_t queue_type, uint32_t *out_family, bool *out_dedicated) const;
    ScarVkCommandPool pool_for_type(uint32_t queue_type) const;
    bool submit_empty_or_recorded_cmd(
        uint32_t queue_type,
        const std::vector<ScarVkMemoryBarrier2> &barriers,
        uint32_t &out_vk_submitted);
    std::string query_device_fault_info(bool *out_queried) const;
};

/* ============================================================================
 * Frame Pacing & Swapchain / PresentX State Machine
 * ============================================================================ */

struct SwapchainState {
    uint32_t width = 3840;
    uint32_t height = 2160;
    uint32_t buffer_count = 3;
    uint32_t generation = 1;
    bool fullscreen = true;
    bool hdr_enabled = true;
    bool is_suspended = false;

    uint32_t frame_period_us = 16667; /* Default 60Hz (16.667 ms) */
    uint32_t origin_offset_us = 0;
    bool vrr_enabled = false;

    uint64_t next_token = 1;
    uint64_t last_origin_ns = 0;
    uint64_t scheduled_token = 0;
    uint64_t scheduled_target_ns = 0;
    uint64_t present_serial = 0;
    uint64_t gamedvr_captures = 0;

    ScarVkHdrMetadataEXT last_hdr_metadata{};
    uint32_t last_plane0_vk_colorspace = SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT;
    uint32_t last_plane1_vk_colorspace = SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
};

uint64_t monotonic_now_ns() noexcept;

scarlett_hresult_t pacing_set_frame_interval(
    SwapchainState &sc,
    const scarlett_d3d12xs_frame_interval_params_t &in) noexcept;

scarlett_hresult_t pacing_schedule_frame_event(
    SwapchainState &sc,
    scarlett_d3d12xs_schedule_frame_params_t &inout) noexcept;

scarlett_hresult_t pacing_wait_frame_event(
    std::mutex &device_mutex,
    SwapchainState &sc,
    scarlett_d3d12xs_wait_frame_params_t &inout) noexcept;

scarlett_hresult_t presentx_submit(
    SwapchainState &sc,
    uint64_t device_caps_mask,
    scarlett_d3d12xs_presentx_params_t &inout) noexcept;

/* ============================================================================
 * Device Objects & Central Manager
 * ============================================================================ */

struct QueueObject {
    uint64_t handle = 0;
    uint32_t queue_type = 0;
    uint32_t priority = 0;
    uint32_t vk_queue_family = 0;
    uint32_t vk_queue_index = 0;
    bool is_dedicated = false;
    uint64_t submit_serial = 0;
};

struct PlacedResourceObject {
    uint64_t handle = 0;
    uint64_t gpu_va = 0;
    uint64_t size_bytes = 0;
    uint32_t dimension = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth_or_array = 0;
    uint32_t mip_levels = 0;
    uint32_t dxgi_format = 0;
    uint32_t swizzle_mode = 0;
    uint32_t current_state = 0;
    uint32_t uma_tier = 0;
};

struct RecordedIndirectOp {
    uint32_t indirect_kind = 0;
    uint32_t commands_parsed = 0;
    uint64_t total_work = 0;
    bool used_dgc = false;
};

struct CommandListObject {
    uint64_t handle = 0;
    uint32_t queue_type = 0;
    std::vector<ScarVkMemoryBarrier2> recorded_barriers;
    std::vector<RecordedIndirectOp> recorded_indirects;
    uint64_t execution_count = 0;
};

struct DredBreadcrumb {
    uint64_t timestamp_ns = 0;
    uint64_t serial = 0;
    std::string op_name;
    std::string details;
};

struct DeviceObject {
    uint64_t handle = 0;
    uint32_t gxdk_version = SCARLETT_GXDK_MAGIC;
    uint32_t amp_indirect_args_bytes = 0;
    uint32_t amp_payload_bytes = 0;
    uint64_t auto_breadcrumb_limit = 256;
    uint64_t devkit_diag_bytes = 0;

    std::unique_ptr<VulkanRuntimeContext> vk;
    DescriptorPolicyDecision default_desc_policy{};
    SwapchainState swapchain{};

    uint64_t next_object_id = 1;
    std::unordered_map<uint64_t, QueueObject> queues;
    std::unordered_map<uint64_t, PlacedResourceObject> resources;
    std::unordered_map<uint64_t, DescriptorHeapObject> desc_heaps;
    std::unordered_map<uint64_t, CommandListObject> cmd_lists;

    uint64_t uma_pre_submit_flushes = 0;
    uint64_t uma_present_flushes = 0; /* Invariant: ALWAYS 0! Never deferred to PresentX */
    uint64_t total_submits = 0;

    std::deque<DredBreadcrumb> breadcrumbs;
    std::mutex mutex;

    void record_breadcrumb(const char *op, const std::string &details);
};

class D3D12XsManager {
public:
    static D3D12XsManager &instance();

    scarlett_hresult_t init_module(scarlett_unix_init_params_t &p);
    scarlett_hresult_t create_device(scarlett_d3d12xs_create_device_params_t &p);
    scarlett_hresult_t destroy_device(scarlett_d3d12xs_destroy_device_params_t &p);
    scarlett_hresult_t query_caps(scarlett_d3d12xs_query_caps_params_t &p);
    scarlett_hresult_t set_amp_limits(scarlett_d3d12xs_set_amp_limits_params_t &p);
    scarlett_hresult_t create_queue(scarlett_d3d12xs_create_queue_params_t &p);
    scarlett_hresult_t create_placed_resource(scarlett_d3d12xs_create_placed_resource_params_t &p);
    scarlett_hresult_t destroy_resource(scarlett_d3d12xs_destroy_resource_params_t &p);
    scarlett_hresult_t create_desc_heap(scarlett_d3d12xs_create_desc_heap_params_t &p);
    scarlett_hresult_t write_descriptor(scarlett_d3d12xs_write_desc_params_t &p);
    scarlett_hresult_t eval_desc_policy(scarlett_d3d12xs_eval_desc_policy_params_t &p);
    scarlett_hresult_t create_cmd_list(scarlett_d3d12xs_create_cmd_list_params_t &p);
    scarlett_hresult_t record_barrier(scarlett_d3d12xs_record_barrier_params_t &p);
    scarlett_hresult_t record_indirect(scarlett_d3d12xs_record_indirect_params_t &p);
    scarlett_hresult_t execute_cmd_lists(scarlett_d3d12xs_execute_cmd_lists_params_t &p);
    scarlett_hresult_t strip_dxil(scarlett_d3d12xs_strip_dxil_params_t &p);
    scarlett_hresult_t set_frame_interval(scarlett_d3d12xs_frame_interval_params_t &p);
    scarlett_hresult_t schedule_frame_event(scarlett_d3d12xs_schedule_frame_params_t &p);
    scarlett_hresult_t wait_frame_event(scarlett_d3d12xs_wait_frame_params_t &p);
    scarlett_hresult_t presentx(scarlett_d3d12xs_presentx_params_t &p);
    scarlett_hresult_t resize_swapchain(scarlett_d3d12xs_resize_swapchain_params_t &p);
    scarlett_hresult_t power_state(scarlett_d3d12xs_power_state_params_t &p);
    scarlett_hresult_t dump_dred_fault(scarlett_d3d12xs_dred_dump_params_t &p);
    scarlett_hresult_t report_stub(scarlett_d3d12xs_report_stub_params_t &p);

    void set_stub_sink(scarlett_stub_sink_fn_t sink, void *user_data);

private:
    D3D12XsManager() = default;
    std::shared_ptr<DeviceObject> find_device(uint64_t handle);

    std::mutex map_mutex_;
    uint64_t next_device_handle_ = 1;
    std::unordered_map<uint64_t, std::shared_ptr<DeviceObject>> devices_;

    std::atomic<uint32_t> strict_mode_{0};
    std::mutex stub_mutex_;
    scarlett_stub_sink_fn_t stub_sink_ = nullptr;
    void *stub_sink_ud_ = nullptr;
};

} // namespace scarlett::d3d12xs::unixlib

#endif /* SCARLETT_D3D12_XS_INTERNAL_HPP */
