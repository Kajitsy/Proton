/*
 * Proton-Scarlett — Core d3d12_xs_unix.so Device, Queue, UMA, Barrier,
 *                   ExecuteIndirect, Swapchain Lifecycle & DRED Engine
 */

#include "d3d12_xs_internal.hpp"
#include "rdna2_lifter/rdna2_lifter.hpp"
#include "xmem/xmem_api.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <zlib.h>

namespace scarlett::d3d12xs::unixlib {

namespace {

void copy_cstr(char *dst, size_t cap, const std::string &src) noexcept {
    if (dst == nullptr || cap == 0u) {
        return;
    }
    const size_t n = std::min(cap - 1u, src.size());
    if (n > 0u) {
        std::memcpy(dst, src.data(), n);
    }
    dst[n] = '\0';
}

bool env_is_strict() noexcept {
    const char *v = std::getenv(SCARLETT_ENV_STRICT);
    return (v != nullptr && v[0] == '1' && v[1] == '\0');
}

std::string resolve_proton_log_path() {
    const char *proton_log = std::getenv("PROTON_LOG");
    const char *proton_log_dir = std::getenv("PROTON_LOG_DIR");
    if (proton_log != nullptr && proton_log[0] != '\0' &&
        std::strcmp(proton_log, "0") != 0 && std::strcmp(proton_log, "1") != 0) {
        /* Caller provided an explicit file path in PROTON_LOG */
        return std::string(proton_log);
    }
    if (proton_log_dir != nullptr && proton_log_dir[0] != '\0') {
        std::string dir(proton_log_dir);
        if (!dir.empty() && dir.back() != '/') {
            dir.push_back('/');
        }
        return dir + "proton_scarlett_dred.log";
    }
    if (proton_log != nullptr && std::strcmp(proton_log, "1") == 0) {
        return "/tmp/proton_scarlett_dred.log";
    }
    return {};
}

} // namespace

void DeviceObject::record_breadcrumb(const char *op, const std::string &details) {
    DredBreadcrumb b{};
    b.timestamp_ns = monotonic_now_ns();
    b.serial = ++total_submits;
    b.op_name = op != nullptr ? op : "unknown";
    b.details = details;
    breadcrumbs.push_back(std::move(b));
    const size_t cap = auto_breadcrumb_limit > 0u
                           ? static_cast<size_t>(std::min<uint64_t>(auto_breadcrumb_limit, 4096u))
                           : 256u;
    while (breadcrumbs.size() > cap) {
        breadcrumbs.pop_front();
    }
}

D3D12XsManager &D3D12XsManager::instance() {
    static D3D12XsManager *mgr = new D3D12XsManager();
    return *mgr;
}

std::shared_ptr<DeviceObject> D3D12XsManager::find_device(uint64_t handle) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = devices_.find(handle);
    if (it == devices_.end()) {
        return nullptr;
    }
    return it->second;
}

scarlett_hresult_t D3D12XsManager::init_module(scarlett_unix_init_params_t &p) {
    p.out_unix_abi_version = SCARLETT_UNIX_CALL_ABI_VERSION;
    if (p.abi_version != SCARLETT_UNIX_CALL_ABI_VERSION ||
        p.module_id != SCARLETT_D3D12XS_UNIX_MODULE_ID) {
        p.out_status = SCARLETT_ERR_ABI_VERSION_MISMATCH;
        return p.out_status;
    }
    strict_mode_.store(p.strict_mode != 0u ? 1u : 0u, std::memory_order_relaxed);
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::create_device(scarlett_d3d12xs_create_device_params_t &p) {
    auto dev = std::make_shared<DeviceObject>();
    dev->gxdk_version = p.gxdk_version != 0u ? p.gxdk_version : SCARLETT_GXDK_MAGIC;
    dev->auto_breadcrumb_limit =
        p.auto_breadcrumb_node_count != 0u ? p.auto_breadcrumb_node_count : 256u;
    dev->devkit_diag_bytes = p.devkit_diagnostic_buffer_bytes;

    /* GXDK >= 0x585D070E Amplification Shader buffer limits */
    if (dev->gxdk_version >= SCARLETT_GXDK_MAGIC) {
        dev->amp_indirect_args_bytes = p.amp_shader_indirect_args_bytes != 0u
                                           ? p.amp_shader_indirect_args_bytes
                                           : (4u * 1024u * 1024u);
        dev->amp_payload_bytes = p.amp_shader_payload_bytes != 0u
                                     ? p.amp_shader_payload_bytes
                                     : (16u * 1024u * 1024u);
    }

    dev->vk = std::make_unique<VulkanRuntimeContext>();
    (void)dev->vk->initialize(p.request_validation != 0u);

    /* Evaluate default descriptor heap policy on the probed physical device */
    DescriptorPolicyInput pol_in{};
    pol_in.vendor_id = dev->vk->vendor_id;
    pol_in.driver_id = dev->vk->driver_id;
    pol_in.raw_driver_version = dev->vk->raw_driver_version;
    pol_in.driver_info = dev->vk->driver_info;
    pol_in.requested_descriptors = 65536u;
    pol_in.has_descriptor_heap_ext =
        (dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DESCRIPTOR_HEAP) != 0u;
    pol_in.has_descriptor_buffer_ext =
        (dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DESCRIPTOR_BUFFER) != 0u;
    pol_in.has_descriptor_indexing_ext =
        (dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DESCRIPTOR_INDEXING) != 0u;
    dev->default_desc_policy = evaluate_descriptor_policy(pol_in);

    {
        std::ostringstream oss;
        oss << "gxdk=0x" << std::hex << dev->gxdk_version << std::dec
            << " gpu=\"" << dev->vk->device_name << "\" driver="
            << dev->vk->driver_version.major << "." << dev->vk->driver_version.minor
            << "." << dev->vk->driver_version.patch
            << " desc_backend=" << static_cast<uint32_t>(dev->default_desc_policy.selected_backend)
            << " amp_indirect=" << dev->amp_indirect_args_bytes
            << " amp_payload=" << dev->amp_payload_bytes;
        dev->record_breadcrumb("D3D12XboxCreateDevice", oss.str());
    }

    uint64_t handle = 0u;
    {
        std::lock_guard<std::mutex> lock(map_mutex_);
        handle = next_device_handle_++;
        dev->handle = handle;
        devices_.emplace(handle, dev);
    }

    p.out_device_handle = handle;
    p.out_caps_mask = dev->vk->caps_mask;
    p.out_vk_api_version = dev->vk->device_api_version != SCAR_VK_API_VERSION_1_0
                               ? dev->vk->device_api_version
                               : dev->vk->instance_api_version;
    p.out_vendor_id = dev->vk->vendor_id;
    p.out_device_id = dev->vk->device_id;
    p.out_driver_id = dev->vk->driver_id;
    p.out_driver_major = dev->vk->driver_version.major;
    p.out_driver_minor = dev->vk->driver_version.minor;
    p.out_driver_patch = dev->vk->driver_version.patch;
    p.out_desc_backend = static_cast<uint32_t>(dev->default_desc_policy.selected_backend);
    p.out_desc_fallback_reason = static_cast<uint32_t>(dev->default_desc_policy.reason);
    p.out_validation_errors = dev->vk->validation_error_count.load(std::memory_order_relaxed);
    p.out_direct_queue_family = dev->vk->direct_queue_family;
    p.out_compute_queue_family = dev->vk->compute_queue_family;
    p.out_dma_queue_family = dev->vk->dma_queue_family;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::destroy_device(scarlett_d3d12xs_destroy_device_params_t &p) {
    std::shared_ptr<DeviceObject> dev;
    {
        std::lock_guard<std::mutex> lock(map_mutex_);
        auto it = devices_.find(p.device_handle);
        if (it == devices_.end()) {
            p.out_status = SCARLETT_E_HANDLE;
            return p.out_status;
        }
        dev = std::move(it->second);
        devices_.erase(it);
    }
    if (dev != nullptr) {
        std::lock_guard<std::mutex> dlock(dev->mutex);
        wsi_shutdown(dev->swapchain, dev->vk.get());
        if (dev->vk != nullptr) {
            dev->vk->shutdown();
        }
    }
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::query_caps(scarlett_d3d12xs_query_caps_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    p.out_caps_mask = dev->vk->caps_mask;
    p.out_vk_api_version = dev->vk->device_api_version != SCAR_VK_API_VERSION_1_0
                               ? dev->vk->device_api_version
                               : dev->vk->instance_api_version;
    p.out_vendor_id = dev->vk->vendor_id;
    p.out_device_id = dev->vk->device_id;
    p.out_driver_id = dev->vk->driver_id;
    p.out_driver_major = dev->vk->driver_version.major;
    p.out_driver_minor = dev->vk->driver_version.minor;
    p.out_driver_patch = dev->vk->driver_version.patch;
    p.out_desc_backend = static_cast<uint32_t>(dev->default_desc_policy.selected_backend);
    p.out_direct_queue_family = dev->vk->direct_queue_family;
    p.out_compute_queue_family = dev->vk->compute_queue_family;
    p.out_dma_queue_family = dev->vk->dma_queue_family;
    p.out_validation_errors = dev->vk->validation_error_count.load(std::memory_order_relaxed);
    p.out_amp_indirect_bytes = dev->amp_indirect_args_bytes;
    p.out_amp_payload_bytes = dev->amp_payload_bytes;
    p.out_swapchain_generation = dev->swapchain.generation;
    p.out_is_suspended = dev->swapchain.is_suspended ? 1u : 0u;
    p.out_uma_pre_submit_flushes = dev->uma_pre_submit_flushes;
    p.out_uma_present_flushes = dev->uma_present_flushes;
    p.out_total_presents = dev->swapchain.present_serial;
    copy_cstr(p.out_device_name, sizeof(p.out_device_name), dev->vk->device_name);
    copy_cstr(p.out_driver_info, sizeof(p.out_driver_info), dev->vk->driver_info);
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::set_amp_limits(scarlett_d3d12xs_set_amp_limits_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.indirect_args_buffer_bytes == 0u || p.payload_buffer_bytes == 0u) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    dev->amp_indirect_args_bytes = p.indirect_args_buffer_bytes;
    dev->amp_payload_bytes = p.payload_buffer_bytes;
    dev->record_breadcrumb("SetAmplificationShaderLimits",
                           "indirect=" + std::to_string(p.indirect_args_buffer_bytes) +
                               " payload=" + std::to_string(p.payload_buffer_bytes));
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::create_queue(scarlett_d3d12xs_create_queue_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    if (p.queue_type != SCARLETT_D3D12_QUEUE_TYPE_DIRECT &&
        p.queue_type != SCARLETT_D3D12_QUEUE_TYPE_DMA &&
        p.queue_type != SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
        p.out_status = SCARLETT_ERR_D3D12_UNSUPPORTED_QUEUE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    uint32_t vk_fam = 0u;
    bool dedicated = false;
    (void)dev->vk->queue_for_type(p.queue_type, &vk_fam, &dedicated);

    QueueObject q{};
    q.handle = dev->next_object_id++;
    q.queue_type = p.queue_type;
    q.priority = p.priority;
    q.vk_queue_family = vk_fam;
    q.vk_queue_index = 0u;
    q.is_dedicated = dedicated;
    dev->queues.emplace(q.handle, q);

    dev->record_breadcrumb("CreateCommandQueueX",
                           "type=0x" +
                               ([&]() {
                                   std::ostringstream s;
                                   s << std::hex << p.queue_type << std::dec
                                     << " vk_family=" << vk_fam
                                     << " dedicated=" << (dedicated ? 1 : 0);
                                   return s.str();
                               })());

    p.out_queue_handle = q.handle;
    p.out_vk_queue_family = q.vk_queue_family;
    p.out_vk_queue_index = q.vk_queue_index;
    p.out_is_dedicated_family = q.is_dedicated ? 1u : 0u;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::create_placed_resource(
    scarlett_d3d12xs_create_placed_resource_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    if (p.size_bytes == 0u || !scarlett_addr_is_graphics(p.gpu_va, p.size_bytes)) {
        p.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return p.out_status;
    }

    /* Query active UMA tier from xmem_unix (non-destructive probe) */
    uint32_t active_uma_tier = static_cast<uint32_t>(SCARLETT_UMA_TIER_C_UFFD_WP);
    scarlett_xmem_query_budget_params_t bq{};
    if (scarlett_succeeded(scarlett_xmem_unix_call(SCARLETT_XMEM_UNIX_QUERY_BUDGET, &bq)) &&
        scarlett_succeeded(bq.out_status) && bq.out_active_uma_tier != 0u) {
        active_uma_tier = bq.out_active_uma_tier;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    PlacedResourceObject r{};
    r.handle = dev->next_object_id++;
    r.gpu_va = p.gpu_va;
    r.size_bytes = p.size_bytes;
    r.dimension = p.dimension;
    r.width = p.width;
    r.height = p.height;
    r.depth_or_array = p.depth_or_array;
    r.mip_levels = p.mip_levels;
    r.dxgi_format = p.dxgi_format;
    r.swizzle_mode = p.swizzle_mode;
    r.current_state = p.initial_state;
    r.uma_tier = active_uma_tier;
    dev->resources.emplace(r.handle, r);

    std::ostringstream oss;
    oss << "res=" << r.handle << " va=0x" << std::hex << r.gpu_va << std::dec
        << " size=" << r.size_bytes << " uma_tier=" << r.uma_tier;
    dev->record_breadcrumb("CreatePlacedResourceX", oss.str());

    p.out_resource_handle = r.handle;
    p.out_uma_tier = r.uma_tier;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::destroy_resource(scarlett_d3d12xs_destroy_resource_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    auto it = dev->resources.find(p.resource_handle);
    if (it == dev->resources.end()) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    dev->resources.erase(it);
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::create_desc_heap(scarlett_d3d12xs_create_desc_heap_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.num_descriptors == 0u || p.num_descriptors > 4000000u) {
        p.out_status = SCARLETT_ERR_D3D12_DESCRIPTOR_LIMIT;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    DescriptorPolicyInput pol_in{};
    pol_in.vendor_id = dev->vk->vendor_id;
    pol_in.driver_id = dev->vk->driver_id;
    pol_in.raw_driver_version = dev->vk->raw_driver_version;
    pol_in.driver_info = dev->vk->driver_info;
    pol_in.requested_descriptors = p.num_descriptors;
    pol_in.has_descriptor_heap_ext =
        (dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DESCRIPTOR_HEAP) != 0u;
    pol_in.has_descriptor_buffer_ext =
        (dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DESCRIPTOR_BUFFER) != 0u;
    pol_in.has_descriptor_indexing_ext =
        (dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DESCRIPTOR_INDEXING) != 0u;

    const DescriptorPolicyDecision dec = evaluate_descriptor_policy(pol_in);

    DescriptorHeapObject heap{};
    heap.handle = dev->next_object_id++;
    heap.heap_type = p.heap_type;
    heap.num_descriptors = p.num_descriptors;
    heap.flags = p.flags;
    heap.stride_bytes = (p.heap_type == SCARLETT_DESC_HEAP_SAMPLER) ? 16u : 32u;
    heap.gpu_va_base = SCARLETT_MEM_GRAPHICS_BASE + (heap.handle << 24u);
    heap.backend = dec.selected_backend;
    heap.reason = dec.reason;
    /* Cap materialization vector size cleanly for massive (>1M) stress heaps */
    const size_t materialized = std::min<size_t>(p.num_descriptors, 65536u);
    heap.slots.resize(materialized);

    dev->desc_heaps.emplace(heap.handle, std::move(heap));

    const auto &stored = dev->desc_heaps.at(p.out_heap_handle = dev->next_object_id - 1u);
    p.out_gpu_va_base = stored.gpu_va_base;
    p.out_descriptor_stride = stored.stride_bytes;
    p.out_selected_backend = static_cast<uint32_t>(stored.backend);
    p.out_fallback_reason = static_cast<uint32_t>(stored.reason);
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::write_descriptor(scarlett_d3d12xs_write_desc_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    auto it = dev->desc_heaps.find(p.heap_handle);
    if (it == dev->desc_heaps.end()) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.slot_index >= it->second.num_descriptors) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    DescriptorSlot slot{};
    scarlett_hresult_t hr = encode_rdna2_descriptor(p, slot);
    if (scarlett_failed(hr)) {
        p.out_status = hr;
        return hr;
    }

    if (p.slot_index < it->second.slots.size()) {
        it->second.slots[p.slot_index] = slot;
    }

    p.out_encoded_word0 = slot.words[0];
    p.out_encoded_word1 = slot.words[1];
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::eval_desc_policy(scarlett_d3d12xs_eval_desc_policy_params_t &p) {
    DescriptorPolicyInput in{};
    in.vendor_id = p.vendor_id;
    in.driver_id = p.driver_id;
    in.raw_driver_version = p.raw_driver_version;
    char safe_info[sizeof(p.driver_info) + 1u];
    std::memcpy(safe_info, p.driver_info, sizeof(p.driver_info));
    safe_info[sizeof(p.driver_info)] = '\0';
    in.driver_info = safe_info;
    in.requested_descriptors = p.requested_descriptors;
    in.has_descriptor_heap_ext = (p.has_desc_heap_ext != 0u);
    in.has_descriptor_buffer_ext = (p.has_desc_buffer_ext != 0u);
    in.has_descriptor_indexing_ext = (p.has_desc_indexing_ext != 0u);

    const DescriptorPolicyDecision dec = evaluate_descriptor_policy(in);
    p.out_parsed_major = dec.parsed_version.major;
    p.out_parsed_minor = dec.parsed_version.minor;
    p.out_parsed_patch = dec.parsed_version.patch;
    p.out_is_nvidia_610 = dec.is_nvidia_610_guard ? 1u : 0u;
    p.out_selected_backend = static_cast<uint32_t>(dec.selected_backend);
    p.out_fallback_reason = static_cast<uint32_t>(dec.reason);
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::create_cmd_list(scarlett_d3d12xs_create_cmd_list_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.queue_type != SCARLETT_D3D12_QUEUE_TYPE_DIRECT &&
        p.queue_type != SCARLETT_D3D12_QUEUE_TYPE_DMA &&
        p.queue_type != SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
        p.out_status = SCARLETT_ERR_D3D12_UNSUPPORTED_QUEUE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    CommandListObject cl{};
    cl.handle = dev->next_object_id++;
    cl.queue_type = p.queue_type;
    dev->cmd_lists.emplace(cl.handle, std::move(cl));

    p.out_cmd_list_handle = dev->next_object_id - 1u;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::record_barrier(scarlett_d3d12xs_record_barrier_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.barrier_kind > SCARLETT_BARRIER_KIND_IMAGE) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    auto cl_it = dev->cmd_lists.find(p.cmd_list_handle);
    if (cl_it == dev->cmd_lists.end()) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    if (p.resource_handle != 0u) {
        auto res_it = dev->resources.find(p.resource_handle);
        if (res_it == dev->resources.end()) {
            p.out_status = SCARLETT_E_HANDLE;
            return p.out_status;
        }
        res_it->second.current_state = p.layout_after;
    }

    /* Translate Enhanced Barrier into Vulkan 1.3/1.4 VkMemoryBarrier2 / VkDependencyInfo */
    ScarVkMemoryBarrier2 mb{};
    mb.sType = SCAR_VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    mb.pNext = nullptr;
    mb.srcStageMask = p.sync_before != 0u ? p.sync_before : SCAR_VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = p.access_before != 0u ? p.access_before : SCAR_VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = p.sync_after != 0u ? p.sync_after : SCAR_VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = p.access_after != 0u ? p.access_after : SCAR_VK_ACCESS_2_MEMORY_READ_BIT;
    cl_it->second.recorded_barriers.push_back(mb);

    dev->record_breadcrumb("EnhancedBarrier",
                           "cmd=" + std::to_string(p.cmd_list_handle) +
                               " kind=" + std::to_string(p.barrier_kind) +
                               " layout=" + std::to_string(p.layout_before) + "->" +
                               std::to_string(p.layout_after));

    p.out_used_sync2 =
        ((dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_SYNCHRONIZATION2) != 0u) ? 1u : 0u;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::record_indirect(scarlett_d3d12xs_record_indirect_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.indirect_kind != SCARLETT_INDIRECT_DRAW_INDEXED &&
        p.indirect_kind != SCARLETT_INDIRECT_DISPATCH) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }
    if (p.arg_buffer_addr == 0u || p.max_command_count == 0u) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    const uint32_t min_stride =
        (p.indirect_kind == SCARLETT_INDIRECT_DRAW_INDEXED)
            ? static_cast<uint32_t>(sizeof(D3D12XBOX_DRAW_INDEXED_ARGUMENTS))
            : static_cast<uint32_t>(sizeof(D3D12XBOX_DISPATCH_ARGUMENTS));
    const uint32_t stride = p.arg_stride >= min_stride ? p.arg_stride : min_stride;

    std::lock_guard<std::mutex> lock(dev->mutex);
    auto cl_it = dev->cmd_lists.find(p.cmd_list_handle);
    if (cl_it == dev->cmd_lists.end()) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    /* Determine effective count (honors optional count buffer) */
    uint32_t effective_count = p.max_command_count;
    if (p.count_buffer_addr != 0u) {
        const auto *cnt_ptr = reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(p.count_buffer_addr + p.count_buffer_offset));
        uint32_t gpu_count = 0u;
        std::memcpy(&gpu_count, cnt_ptr, sizeof(gpu_count));
        if (gpu_count < effective_count) {
            effective_count = gpu_count;
        }
    }

    const bool has_dgc =
        ((dev->vk->caps_mask & SCARLETT_D3D12XS_CAP_DEVICE_GENERATED_CMDS) != 0u) &&
        (p.force_cpu_fallback == 0u);

    /*
     * Parse indirect argument stream for both validation and clean CPU-parsed
     * vkCmdDrawIndexedIndirect / vkCmdDispatchIndirect fallback when
     * VK_EXT_device_generated_commands is unavailable.
     */
    uint64_t total_work = 0u;
    const auto *arg_base = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(p.arg_buffer_addr + p.arg_buffer_offset));
    for (uint32_t i = 0u; i < effective_count; ++i) {
        const uint8_t *entry = arg_base + static_cast<size_t>(i) * stride;
        if (p.indirect_kind == SCARLETT_INDIRECT_DRAW_INDEXED) {
            D3D12XBOX_DRAW_INDEXED_ARGUMENTS draw{};
            std::memcpy(&draw, entry, sizeof(draw));
            total_work += static_cast<uint64_t>(draw.IndexCountPerInstance) *
                          static_cast<uint64_t>(draw.InstanceCount);
        } else {
            D3D12XBOX_DISPATCH_ARGUMENTS disp{};
            std::memcpy(&disp, entry, sizeof(disp));
            total_work += static_cast<uint64_t>(disp.ThreadGroupCountX) *
                          static_cast<uint64_t>(disp.ThreadGroupCountY) *
                          static_cast<uint64_t>(disp.ThreadGroupCountZ);
        }
    }

    RecordedIndirectOp op{};
    op.indirect_kind = p.indirect_kind;
    op.commands_parsed = effective_count;
    op.total_work = total_work;
    op.used_dgc = has_dgc;
    cl_it->second.recorded_indirects.push_back(op);

    dev->record_breadcrumb("ExecuteIndirect",
                           std::string(has_dgc ? "path=VK_EXT_DGC " : "path=CPU_vkCmdIndirect ") +
                               "kind=" + std::to_string(p.indirect_kind) +
                               " cmds=" + std::to_string(effective_count) +
                               " work=" + std::to_string(total_work));

    p.out_used_dgc = has_dgc ? 1u : 0u;
    p.out_commands_parsed = effective_count;
    p.out_total_indices_or_groups = total_work;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::execute_cmd_lists(
    scarlett_d3d12xs_execute_cmd_lists_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.cmd_list_count == 0u || p.cmd_list_handles_addr == 0u) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    const auto *handles =
        reinterpret_cast<const uint64_t *>(static_cast<uintptr_t>(p.cmd_list_handles_addr));

    std::lock_guard<std::mutex> lock(dev->mutex);
    if (dev->swapchain.is_suspended) {
        p.out_status = SCARLETT_E_PENDING;
        return p.out_status;
    }

    auto q_it = dev->queues.find(p.queue_handle);
    if (q_it == dev->queues.end()) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    std::vector<ScarVkMemoryBarrier2> merged_barriers;
    for (uint32_t i = 0u; i < p.cmd_list_count; ++i) {
        auto cl_it = dev->cmd_lists.find(handles[i]);
        if (cl_it == dev->cmd_lists.end()) {
            p.out_status = SCARLETT_E_HANDLE;
            return p.out_status;
        }
        merged_barriers.insert(merged_barriers.end(),
                               cl_it->second.recorded_barriers.begin(),
                               cl_it->second.recorded_barriers.end());
    }

    /*
     * MANDATORY 0-FRAME LAG INVARIANT:
     * Synchronously flush ALL dirty CPU-written MEM_GRAPHICS pages via
     * scarlett_uma_flush_before_submit() BEFORE submitting to the Vulkan queue!
     * NEVER deferred to PresentX.
     */
    const uint64_t submit_serial = ++q_it->second.submit_serial;
    uint64_t flushed_pages = 0u;
    uint64_t flushed_bytes = 0u;
    scarlett_hresult_t flush_hr = scarlett_uma_flush_before_submit(
        handles[0],
        p.uma_region_addr,
        p.uma_region_size,
        &flushed_pages,
        &flushed_bytes);
    if (scarlett_failed(flush_hr)) {
        p.out_status = flush_hr;
        return flush_hr;
    }
    dev->uma_pre_submit_flushes++;

    /* Submit recorded barriers onto the real Vulkan queue family */
    uint32_t vk_submitted = 0u;
    (void)dev->vk->submit_empty_or_recorded_cmd(
        q_it->second.queue_type, merged_barriers, vk_submitted);

    for (uint32_t i = 0u; i < p.cmd_list_count; ++i) {
        auto cl_it = dev->cmd_lists.find(handles[i]);
        if (cl_it != dev->cmd_lists.end()) {
            cl_it->second.execution_count++;
            cl_it->second.recorded_barriers.clear();
            cl_it->second.recorded_indirects.clear();
        }
    }

    std::ostringstream oss;
    oss << "queue=" << p.queue_handle << " type=0x" << std::hex << q_it->second.queue_type
        << std::dec << " lists=" << p.cmd_list_count
        << " uma_flushed_pages=" << flushed_pages
        << " uma_flushed_bytes=" << flushed_bytes
        << " vk_submitted=" << vk_submitted;
    dev->record_breadcrumb("ExecuteCommandLists", oss.str());

    p.out_flushed_pages = flushed_pages;
    p.out_flushed_bytes = flushed_bytes;
    p.out_submit_serial = submit_serial;
    p.out_uma_flushed_pre_submit = 1u;
    p.out_vk_submitted = vk_submitted;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::strip_dxil(scarlett_d3d12xs_strip_dxil_params_t &p) {
    const auto *src = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(p.src_addr));
    auto *dst = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(p.dst_addr));
    return strip_xbox_dxil_container(src, p.src_size, dst, p.dst_capacity, p);
}

namespace {

uint32_t read_le32_u(const uint8_t *b) noexcept {
    return static_cast<uint32_t>(b[0]) |
           (static_cast<uint32_t>(b[1]) << 8u) |
           (static_cast<uint32_t>(b[2]) << 16u) |
           (static_cast<uint32_t>(b[3]) << 24u);
}

uint64_t read_le64_u(const uint8_t *b) noexcept {
    return static_cast<uint64_t>(read_le32_u(b)) |
           (static_cast<uint64_t>(read_le32_u(b + 4u)) << 32u);
}

void write_le32_u(uint8_t *b, uint32_t v) noexcept {
    b[0] = static_cast<uint8_t>(v & 0xFFu);
    b[1] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
    b[2] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    b[3] = static_cast<uint8_t>((v >> 24u) & 0xFFu);
}

scarlett::rdna2::ShaderStage map_dxil_program_type(uint32_t prog_type) noexcept {
    using S = scarlett::rdna2::ShaderStage;
    switch (prog_type) {
    case 0u: return S::Pixel;
    case 1u: return S::Vertex;
    case 2u: return S::Geometry;
    case 3u: return S::Hull;
    case 4u: return S::Domain;
    case 5u: return S::Compute;
    default: return S::Pixel;
    }
}

bool try_lift_dxbc_xbch_to_spv0(
    const uint8_t *src,
    size_t src_size,
    scarlett_shader_mode_t mode,
    uint32_t prefer_ray_query,
    uint8_t *dst_bytes,
    uint64_t dst_capacity,
    scarlett_d3d12xs_lift_shader_params_t &p) noexcept {
    if (src == nullptr || src_size < 32u || read_le32_u(src) != 0x43425844u) {
        return false;
    }
    const uint32_t chunk_count = read_le32_u(src + 28u);
    if (chunk_count == 0u || chunk_count > 64u ||
        32u + static_cast<size_t>(chunk_count) * 4u > src_size) {
        return false;
    }

    struct ChunkSpan {
        uint32_t tag;
        const uint8_t *payload;
        uint32_t size;
    };
    std::vector<ChunkSpan> kept;
    kept.reserve(chunk_count + 1u);

    const uint8_t *dxil_payload = nullptr;
    uint32_t dxil_size = 0u;
    for (uint32_t i = 0; i < chunk_count; ++i) {
        const uint32_t off = read_le32_u(src + 32u + i * 4u);
        if (static_cast<size_t>(off) + 8u > src_size) {
            return false;
        }
        const uint32_t tag = read_le32_u(src + off);
        const uint32_t csz = read_le32_u(src + off + 4u);
        if (static_cast<size_t>(off) + 8u + static_cast<size_t>(csz) > src_size) {
            return false;
        }
        if (tag == 0x4C495844u /* 'DXIL' */) {
            dxil_payload = src + off + 8u;
            dxil_size = csz;
        } else {
            kept.push_back({tag, src + off + 8u, csz});
        }
    }
    if (dxil_payload == nullptr || dxil_size < 48u) {
        return false;
    }

    /* Locate embedded 'XBCH' (0x48434258) container inside Xbox 'DXIL' chunk */
    size_t xbch_off = SIZE_MAX;
    for (size_t k = 0; k + 24u <= dxil_size; ++k) {
        if (dxil_payload[k] == 'X' && dxil_payload[k + 1] == 'B' &&
            dxil_payload[k + 2] == 'C' && dxil_payload[k + 3] == 'H') {
            xbch_off = k;
            break;
        }
    }
    if (xbch_off == SIZE_MAX) {
        return false;
    }
    const uint32_t xbch_ver = read_le32_u(dxil_payload + xbch_off + 4u);
    const uint64_t comp_len = read_le64_u(dxil_payload + xbch_off + 8u);
    const uint64_t uncomp_len = read_le64_u(dxil_payload + xbch_off + 16u);
    if (xbch_ver != 3u || comp_len == 0u || uncomp_len < 16u ||
        uncomp_len > (16u * 1024u * 1024u) ||
        xbch_off + 24u + comp_len > dxil_size) {
        return false;
    }

    std::vector<uint8_t> fssm(static_cast<size_t>(uncomp_len));
    uLongf z_out = static_cast<uLongf>(uncomp_len);
    const int zrc = uncompress(
        fssm.data(),
        &z_out,
        dxil_payload + xbch_off + 24u,
        static_cast<uLong>(comp_len));
    if (zrc != Z_OK || z_out < 16u) {
        return false;
    }
    fssm.resize(static_cast<size_t>(z_out));

    auto unpacked = scarlett::rdna2::unpack_rdna2_container(fssm.data(), fssm.size());
    if (unpacked.status != SCARLETT_OK || unpacked.isa_words.empty()) {
        return false;
    }
    const uint32_t prog_type = (read_le32_u(dxil_payload) >> 16u) & 0xFFFFu;
    unpacked.meta.stage = map_dxil_program_type(prog_type);

    scarlett::rdna2::LiftOptions opts{};
    opts.mode = mode;
    opts.prefer_ray_query = (prefer_ray_query != 0u);
    opts.gpu_profile.probe_bitmask &= ~scarlett::rdna2::probe_bits::EXT_SPV_KHR_FMA;
    opts.graphics_no_rogue_descriptors = true;
    opts.enable_maximal_reconvergence = false;
    opts.enable_quad_control = false;
    opts.validate_with_spirv_tools = true;

    scarlett::rdna2::Rdna2Lifter lifter;
    auto res = lifter.lift_isa(
        unpacked.meta,
        unpacked.isa_words.data(),
        unpacked.isa_words.size(),
        opts);
    if (res.status != SCARLETT_OK || res.spirv_words.size() < 5u ||
        res.spirv_words[0] != 0x07230203u) {
        std::fprintf(stderr,
                     "[scarlett-shader] lift_isa failed stage=%u words=%zu hr=0x%08x diag='%s'\n",
                     static_cast<unsigned>(unpacked.meta.stage),
                     unpacked.isa_words.size(),
                     static_cast<uint32_t>(res.status),
                     res.diagnostics.c_str());
        std::fflush(stderr);
        return false;
    }

    const uint32_t spv_bytes_len = static_cast<uint32_t>(res.spirv_words.size() * 4u);
    const uint32_t out_chunks = static_cast<uint32_t>(kept.size() + 1u);
    uint64_t total_size = 32u + static_cast<uint64_t>(out_chunks) * 4u;
    for (const auto &c : kept) {
        total_size += 8u + static_cast<uint64_t>(c.size);
    }
    total_size += 8u + static_cast<uint64_t>(spv_bytes_len);
    if (dst_bytes == nullptr || total_size > dst_capacity) {
        p.out_dst_size = total_size;
        p.out_status = SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL;
        return true;
    }

    write_le32_u(dst_bytes + 0u, 0x43425844u); /* 'DXBC' */
    std::memset(dst_bytes + 4u, 0, 16u);
    write_le32_u(dst_bytes + 20u, 1u);
    write_le32_u(dst_bytes + 24u, static_cast<uint32_t>(total_size));
    write_le32_u(dst_bytes + 28u, out_chunks);

    uint32_t cursor = 32u + out_chunks * 4u;
    for (size_t i = 0; i < kept.size(); ++i) {
        write_le32_u(dst_bytes + 32u + i * 4u, cursor);
        write_le32_u(dst_bytes + cursor, kept[i].tag);
        write_le32_u(dst_bytes + cursor + 4u, kept[i].size);
        if (kept[i].size > 0u) {
            std::memcpy(dst_bytes + cursor + 8u, kept[i].payload, kept[i].size);
        }
        cursor += 8u + kept[i].size;
    }
    write_le32_u(dst_bytes + 32u + kept.size() * 4u, cursor);
    write_le32_u(dst_bytes + cursor, 0x30565053u); /* 'SPV0' */
    write_le32_u(dst_bytes + cursor + 4u, spv_bytes_len);
    std::memcpy(dst_bytes + cursor + 8u, res.spirv_words.data(), spv_bytes_len);

    compute_dxbc_digest(dst_bytes, static_cast<uint32_t>(total_size), dst_bytes + 4u);
    p.out_dst_size = total_size;
    p.out_used_dxil_fast_path = 1u;
    p.out_wave_strategy = static_cast<uint32_t>(res.wave_stats.chosen_strategy);
    p.out_status = SCARLETT_OK;
    return true;
}

} // namespace

scarlett_hresult_t D3D12XsManager::lift_shader(scarlett_d3d12xs_lift_shader_params_t &p) {
    p.out_dst_size = 0u;
    p.out_used_dxil_fast_path = 0u;
    p.out_container_kind = 0u;
    p.out_wave_strategy = 0u;

    const auto *src = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(p.src_addr));
    auto *dst = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(p.dst_addr));
    if (src == nullptr || p.src_size < 4u) {
        p.out_status = SCARLETT_ERR_SHADER_INVALID_CONTAINER;
        return p.out_status;
    }

    const uint32_t magic =
        static_cast<uint32_t>(src[0]) |
        (static_cast<uint32_t>(src[1]) << 8u) |
        (static_cast<uint32_t>(src[2]) << 16u) |
        (static_cast<uint32_t>(src[3]) << 24u);
    if (magic == 0x43425844u) { /* 'DXBC' */
        p.out_container_kind = 1u;
    } else if (magic == 0x00325358u || (src[0] == 'X' && src[1] == 'S' && src[2] == '2')) {
        p.out_container_kind = 2u; /* 'XS2' */
    } else if (magic == 0x4D535346u) { /* 'FSSM' */
        p.out_container_kind = 3u;
    } else if (magic == 0x09005350u) { /* "PS\x00\x09" */
        p.out_container_kind = 4u;
    } else if (magic == 0x35444753u) { /* 'SGD5' */
        p.out_container_kind = 5u;
    }

    scarlett_shader_mode_t mode = scarlett::rdna2::parse_shader_mode_env();
    if (p.shader_mode == 1u) {
        mode = SCARLETT_SHADER_MODE_EXACT;
    } else if (p.shader_mode == 2u) {
        mode = SCARLETT_SHADER_MODE_ADAPTIVE;
    } else if (p.shader_mode == 3u) {
        mode = SCARLETT_SHADER_MODE_FAST_F32;
    }

    auto *dst_bytes = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(p.dst_addr));
    scarlett_hresult_t hr = SCARLETT_OK;
    if (p.out_container_kind == 1u &&
        try_lift_dxbc_xbch_to_spv0(
            src,
            static_cast<size_t>(p.src_size),
            mode,
            p.prefer_ray_query,
            dst_bytes,
            p.dst_capacity,
            p)) {
        hr = p.out_status;
    } else if (p.out_container_kind >= 2u && p.out_container_kind <= 5u) {
        scarlett::rdna2::LiftOptions opts{};
        opts.mode = mode;
        opts.prefer_ray_query = (p.prefer_ray_query != 0u);
        opts.gpu_profile.probe_bitmask &= ~scarlett::rdna2::probe_bits::EXT_SPV_KHR_FMA;
        opts.graphics_no_rogue_descriptors = false;
        opts.enable_maximal_reconvergence = false;
        opts.enable_quad_control = false;
        opts.validate_with_spirv_tools = true;
        scarlett::rdna2::Rdna2Lifter lifter;
        auto res = lifter.lift_container(src, static_cast<size_t>(p.src_size), opts);
        if (res.status == SCARLETT_OK && !res.spirv_words.empty()) {
            const uint64_t bytes = static_cast<uint64_t>(res.spirv_words.size()) * 4u;
            if (dst == nullptr || bytes > p.dst_capacity) {
                p.out_dst_size = bytes;
                hr = SCARLETT_E_GS_PROVIDED_BUFFER_TOO_SMALL;
            } else {
                std::memcpy(dst, res.spirv_words.data(), static_cast<size_t>(bytes));
                p.out_dst_size = bytes;
                p.out_used_dxil_fast_path = 0u;
                p.out_wave_strategy = static_cast<uint32_t>(res.wave_stats.chosen_strategy);
                hr = SCARLETT_OK;
            }
        } else {
            hr = (res.status != SCARLETT_OK) ? res.status : SCARLETT_E_FAIL;
            static std::atomic<uint32_t> pathb_err_log{0u};
            if (pathb_err_log.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::fprintf(stderr,
                             "[scarlett-shader] lift_container failed kind=%u hr=0x%08x diag='%s'\n",
                             p.out_container_kind,
                             static_cast<uint32_t>(hr),
                             res.diagnostics.c_str());
                std::fflush(stderr);
            }
        }
        p.out_status = hr;
    } else {
        scarlett_rdna2_lift_c_result_t c_res{};
        const size_t cap_words = static_cast<size_t>(p.dst_capacity / 4u);
        hr = scarlett_rdna2_lift_shader_c(
            src,
            static_cast<size_t>(p.src_size),
            mode,
            p.prefer_ray_query,
            dst,
            cap_words,
            &c_res);

        p.out_dst_size = c_res.byte_count;
        p.out_used_dxil_fast_path = c_res.used_dxil_fast_path;
        p.out_wave_strategy = c_res.wave_strategy;
        p.out_status = hr;
    }

    static std::atomic<uint32_t> lift_log_count{0u};
    const uint32_t seq = lift_log_count.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (seq <= 16u || (seq % 100u) == 0u) {
        std::fprintf(stderr,
                     "[scarlett-shader] lift #%u magic='%c%c%c%c' kind=%u src=%" PRIu64
                     " dst=%" PRIu64 " fast_path=%u wave=%u hr=0x%08x\n",
                     seq,
                     (src[0] >= 32 && src[0] < 127) ? static_cast<char>(src[0]) : '.',
                     (src[1] >= 32 && src[1] < 127) ? static_cast<char>(src[1]) : '.',
                     (src[2] >= 32 && src[2] < 127) ? static_cast<char>(src[2]) : '.',
                     (src[3] >= 32 && src[3] < 127) ? static_cast<char>(src[3]) : '.',
                     p.out_container_kind,
                     p.src_size,
                     p.out_dst_size,
                     p.out_used_dxil_fast_path,
                     p.out_wave_strategy,
                     static_cast<uint32_t>(hr));
        std::fflush(stderr);
    }
    return hr;
}

scarlett_hresult_t D3D12XsManager::set_frame_interval(
    scarlett_d3d12xs_frame_interval_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    std::lock_guard<std::mutex> lock(dev->mutex);
    p.out_status = pacing_set_frame_interval(dev->swapchain, p);
    return p.out_status;
}

scarlett_hresult_t D3D12XsManager::schedule_frame_event(
    scarlett_d3d12xs_schedule_frame_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    std::lock_guard<std::mutex> lock(dev->mutex);
    return pacing_schedule_frame_event(dev->swapchain, p);
}

scarlett_hresult_t D3D12XsManager::wait_frame_event(scarlett_d3d12xs_wait_frame_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    return pacing_wait_frame_event(dev->mutex, dev->swapchain, p);
}

scarlett_hresult_t D3D12XsManager::presentx(scarlett_d3d12xs_presentx_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    /* Validate plane resource handles if provided */
    for (uint32_t i = 0u; i < p.plane_count && i < 2u; ++i) {
        if (p.planes[i].resource_handle != 0u &&
            dev->resources.find(p.planes[i].resource_handle) == dev->resources.end()) {
            p.out_status = SCARLETT_E_HANDLE;
            return p.out_status;
        }
    }

    /* Strictly NO UMA flush inside PresentX (dev->uma_present_flushes stays 0) */
    scarlett_hresult_t hr = presentx_submit(dev->swapchain, dev->vk.get(), dev->vk->caps_mask, p);
    if (scarlett_succeeded(hr)) {
        dev->record_breadcrumb("PresentX",
                               "planes=" + std::to_string(p.plane_count) +
                                   " vk_cs0=" + std::to_string(p.out_vk_color_space_plane0) +
                                   " gamedvr=" + std::to_string(p.out_gamedvr_captured) +
                                   " serial=" + std::to_string(p.out_present_serial));
    }
    return hr;
}

scarlett_hresult_t D3D12XsManager::resize_swapchain(
    scarlett_d3d12xs_resize_swapchain_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.width == 0u || p.height == 0u || p.width > 16384u || p.height > 16384u) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    /* 1. Drain all active Vulkan queues prior to rebuilding swapchain buffers */
    if (dev->vk->real_device_created && dev->vk->device != nullptr &&
        dev->vk->vkDeviceWaitIdle != nullptr) {
        (void)dev->vk->vkDeviceWaitIdle(dev->vk->device);
    }

    /* 2. Rebuild swapchain state while keeping all placed resources & heaps intact */
    dev->swapchain.width = p.width;
    dev->swapchain.height = p.height;
    dev->swapchain.fullscreen = (p.fullscreen != 0u);
    dev->swapchain.hdr_enabled = (p.hdr_enabled != 0u);
    if (p.buffer_count >= 2u && p.buffer_count <= 4u) {
        dev->swapchain.buffer_count = p.buffer_count;
    }
    dev->swapchain.generation++;
    dev->swapchain.scheduled_target_ns = 0u;

    dev->record_breadcrumb("ResizeSwapChainX",
                           std::to_string(p.width) + "x" + std::to_string(p.height) +
                               " fs=" + std::to_string(p.fullscreen) +
                               " gen=" + std::to_string(dev->swapchain.generation));

    p.out_generation = dev->swapchain.generation;
    p.out_drained_queues = static_cast<uint32_t>(dev->queues.size());
    p.out_preserved_resources = static_cast<uint32_t>(dev->resources.size());
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::power_state(scarlett_d3d12xs_power_state_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    if (p.power_op != SCARLETT_POWER_OP_SUSPEND && p.power_op != SCARLETT_POWER_OP_RESUME) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    if (dev->vk->real_device_created && dev->vk->device != nullptr &&
        dev->vk->vkDeviceWaitIdle != nullptr) {
        (void)dev->vk->vkDeviceWaitIdle(dev->vk->device);
    }

    if (p.power_op == SCARLETT_POWER_OP_SUSPEND) {
        dev->swapchain.is_suspended = true;
        dev->swapchain.scheduled_target_ns = 0u;
        dev->record_breadcrumb("SuspendX",
                               "drained_queues=" + std::to_string(dev->queues.size()) +
                                   " preserved_resources=" + std::to_string(dev->resources.size()));
    } else {
        dev->swapchain.is_suspended = false;
        dev->swapchain.last_origin_ns = 0u;
        dev->swapchain.scheduled_target_ns = 0u;
        dev->swapchain.generation++;
        dev->record_breadcrumb("ResumeX",
                               "rebuilt_swapchain_gen=" + std::to_string(dev->swapchain.generation) +
                                   " preserved_resources=" + std::to_string(dev->resources.size()));
    }

    p.out_is_suspended = dev->swapchain.is_suspended ? 1u : 0u;
    p.out_swapchain_generation = dev->swapchain.generation;
    p.out_preserved_resources = static_cast<uint32_t>(dev->resources.size());
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t D3D12XsManager::dump_dred_fault(scarlett_d3d12xs_dred_dump_params_t &p) {
    auto dev = find_device(p.device_handle);
    if (dev == nullptr) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }

    std::lock_guard<std::mutex> lock(dev->mutex);
    bool vk_queried = false;
    const std::string vk_fault_summary = dev->vk->query_device_fault_info(&vk_queried);

    std::ostringstream report;
    report << "=== PROTON-SCARLETT D3D12_XS DRED & VK_EXT_device_fault REPORT ===\n";
    report << "DeviceHandle: " << dev->handle << "\n";
    report << "GPU: " << dev->vk->device_name << " (vendor=0x" << std::hex << dev->vk->vendor_id
           << " device=0x" << dev->vk->device_id << std::dec << ")\n";
    report << "Driver: " << dev->vk->driver_name << " " << dev->vk->driver_version.major << "."
           << dev->vk->driver_version.minor << "." << dev->vk->driver_version.patch << " ("
           << dev->vk->driver_info << ")\n";
    report << "VulkanFault: " << vk_fault_summary << "\n";
    if (p.simulate_fault != 0u) {
        report << "SimulatedFaultContext: PAGE_FAULT_DIAGNOSTIC (synthetic test trigger)\n";
    }
    report << "UMAFlushes: pre_submit=" << dev->uma_pre_submit_flushes
           << " present_deferred=" << dev->uma_present_flushes << "\n";
    report << "Swapchain: " << dev->swapchain.width << "x" << dev->swapchain.height
           << " gen=" << dev->swapchain.generation
           << " suspended=" << (dev->swapchain.is_suspended ? 1 : 0)
           << " presents=" << dev->swapchain.present_serial << "\n";
    report << "DRED Auto-Breadcrumbs (" << dev->breadcrumbs.size() << "):\n";
    for (const auto &b : dev->breadcrumbs) {
        report << "  [#" << b.serial << " @" << b.timestamp_ns << "ns] " << b.op_name << ": "
               << b.details << "\n";
    }
    report << "=== END DRED REPORT ===\n";

    const std::string text = report.str();
    std::fprintf(stderr, "%s", text.c_str());

    uint32_t log_written = 0u;
    const std::string log_path = resolve_proton_log_path();
    if (!log_path.empty()) {
        std::ofstream ofs(log_path, std::ios::out | std::ios::app);
        if (ofs.is_open()) {
            ofs << text;
            ofs.flush();
            log_written = ofs.good() ? 1u : 0u;
        }
    }

    uint32_t copied = 0u;
    if (p.out_buf_addr != 0u && p.out_buf_capacity > 0u) {
        auto *dst = reinterpret_cast<char *>(static_cast<uintptr_t>(p.out_buf_addr));
        const size_t n = std::min<size_t>(text.size(), p.out_buf_capacity - 1u);
        if (n > 0u) {
            std::memcpy(dst, text.data(), n);
        }
        dst[n] = '\0';
        copied = static_cast<uint32_t>(n);
    }

    p.out_bytes_written = copied;
    p.out_breadcrumb_count = static_cast<uint32_t>(dev->breadcrumbs.size());
    p.out_vk_fault_queried = vk_queried ? 1u : 0u;
    p.out_proton_log_written = log_written;
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

void D3D12XsManager::set_stub_sink(scarlett_stub_sink_fn_t sink, void *user_data) {
    std::lock_guard<std::mutex> lock(stub_mutex_);
    stub_sink_ = sink;
    stub_sink_ud_ = user_data;
}

scarlett_hresult_t D3D12XsManager::report_stub(scarlett_d3d12xs_report_stub_params_t &p) {
    p.ctx.return_hr = scarlett_stub_enforce_error(p.ctx.return_hr);
    if (p.ctx.strict_mode == 0u) {
        p.ctx.strict_mode =
            (strict_mode_.load(std::memory_order_relaxed) != 0u || env_is_strict()) ? 1u : 0u;
    }

    scarlett_stub_action_t action = scarlett_stub_resolve_action(&p.ctx);

    scarlett_stub_sink_fn_t sink = nullptr;
    void *ud = nullptr;
    {
        std::lock_guard<std::mutex> lock(stub_mutex_);
        sink = stub_sink_;
        ud = stub_sink_ud_;
    }

    std::fprintf(stderr,
                 "[scarlett-stub] dll=%s sym=%s ord=%u slot=%u rva=0x%" PRIx64
                 " rcx=0x%" PRIx64 " rdx=0x%" PRIx64 " r8=0x%" PRIx64
                 " r9=0x%" PRIx64 " strict=%u hr=0x%08x\n",
                 p.ctx.dll_name,
                 p.ctx.symbol_name,
                 p.ctx.ordinal,
                 p.ctx.vtable_slot,
                 p.ctx.caller_rva,
                 p.ctx.rcx,
                 p.ctx.rdx,
                 p.ctx.r8,
                 p.ctx.r9,
                 p.ctx.strict_mode,
                 static_cast<uint32_t>(p.ctx.return_hr));

    if (sink != nullptr) {
        action = sink(&p.ctx, ud);
    }

    p.out_action = static_cast<uint32_t>(action);
    p.out_status = (action == SCARLETT_STUB_ACTION_LOG_AND_ABORT)
                       ? SCARLETT_ERR_STRICT_STUB_ABORT
                       : p.ctx.return_hr;
    return p.out_status;
}

scarlett_hresult_t D3D12XsManager::check_mem(scarlett_d3d12xs_check_mem_params_t &p) {
    p.out_ok = 0u;
    if (p.addr < UINT64_C(0x10000) || p.size == 0u || (p.addr + p.size) < p.addr) {
        p.out_status = SCARLETT_E_INVALIDARG;
        return p.out_status;
    }
    const uint64_t target_start = p.addr;
    const uint64_t target_end = p.addr + p.size;

    FILE *fp = std::fopen("/proc/self/maps", "r");
    if (fp == nullptr) {
        p.out_status = SCARLETT_E_FAIL;
        return p.out_status;
    }

    uint64_t covered_until = target_start;
    char line[512];
    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        unsigned long long s = 0, e = 0;
        char perms[8] = {};
        if (std::sscanf(line, "%llx-%llx %7s", &s, &e, perms) != 3) {
            continue;
        }
        const uint64_t seg_s = static_cast<uint64_t>(s);
        const uint64_t seg_e = static_cast<uint64_t>(e);
        if (seg_e <= covered_until) {
            continue;
        }
        if (seg_s > covered_until) {
            break;
        }
        const bool can_read = (perms[0] == 'r');
        const bool can_write = (perms[1] == 'w');
        if (!can_read || (p.check_write != 0u && !can_write)) {
            break;
        }
        covered_until = seg_e;
        if (covered_until >= target_end) {
            p.out_ok = 1u;
            break;
        }
    }
    std::fclose(fp);
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

} // namespace scarlett::d3d12xs::unixlib
