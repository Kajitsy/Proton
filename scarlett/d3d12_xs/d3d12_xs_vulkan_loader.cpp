/*
 * Proton-Scarlett — Dynamic Vulkan 1.4 Runtime Loader (d3d12_xs_unix.so)
 *
 * Loads libvulkan.so.1 strictly at runtime via dlopen/dlsym, probes Vulkan 1.4
 * instance & physical device properties, optional VK_LAYER_KHRONOS_validation
 * with VK_EXT_debug_utils zero-error counter, separated Vulkan queue families
 * (0x0 Direct, 0x2 DMA/Copy, 0x10 Async Compute XS), and live command submission.
 */

#include "d3d12_xs_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <sstream>
#include <unordered_set>

namespace scarlett::d3d12xs::unixlib {

namespace {

ScarVkBool32 debug_utils_callback(
    ScarVkFlags message_severity,
    ScarVkFlags message_types,
    const ScarVkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data) {
    auto *ctx = static_cast<VulkanRuntimeContext *>(user_data);
    if (ctx == nullptr || callback_data == nullptr) {
        return SCAR_VK_FALSE;
    }

    const char *msg = callback_data->pMessage != nullptr ? callback_data->pMessage : "<null>";
    if ((message_severity & SCAR_VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u &&
        (message_types & SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0u) {
        ctx->validation_error_count.fetch_add(1u, std::memory_order_relaxed);
    } else if ((message_severity & SCAR_VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0u) {
        ctx->validation_warning_count.fetch_add(1u, std::memory_order_relaxed);
    }

    std::lock_guard<std::mutex> lock(ctx->log_mutex);
    if (ctx->validation_messages.size() < 64u) {
        ctx->validation_messages.emplace_back(msg);
    }
    return SCAR_VK_FALSE;
}

template <typename Fn>
Fn load_instance_proc(ScarPFN_vkGetInstanceProcAddr gipa, ScarVkInstance inst, const char *name) {
    if (gipa == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(gipa(inst, name));
}

template <typename Fn>
Fn load_device_proc(ScarPFN_vkGetDeviceProcAddr gdpa, ScarVkDevice dev, const char *name) {
    if (gdpa == nullptr || dev == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(gdpa(dev, name));
}

} // namespace

VulkanRuntimeContext::~VulkanRuntimeContext() {
    shutdown();
}

void VulkanRuntimeContext::shutdown() {
    if (device != nullptr && vkDeviceWaitIdle != nullptr) {
        (void)vkDeviceWaitIdle(device);
    }

    if (device != nullptr && vkDestroyCommandPool != nullptr) {
        std::unordered_set<ScarVkCommandPool> destroyed_pools;
        auto destroy_pool = [&](ScarVkCommandPool &pool) {
            if (pool != SCAR_VK_NULL_HANDLE && destroyed_pools.insert(pool).second) {
                vkDestroyCommandPool(device, pool, nullptr);
            }
            pool = SCAR_VK_NULL_HANDLE;
        };
        destroy_pool(direct_pool);
        destroy_pool(compute_pool);
        destroy_pool(dma_pool);
    }

    if (device != nullptr && vkDestroyDevice != nullptr) {
        vkDestroyDevice(device, nullptr);
    }
    device = nullptr;
    direct_queue = nullptr;
    compute_queue = nullptr;
    dma_queue = nullptr;
    real_device_created = false;

    if (instance != nullptr && debug_messenger != SCAR_VK_NULL_HANDLE &&
        vkDestroyDebugUtilsMessengerEXT != nullptr) {
        vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
    }
    debug_messenger = SCAR_VK_NULL_HANDLE;

    if (instance != nullptr && vkDestroyInstance != nullptr) {
        vkDestroyInstance(instance, nullptr);
    }
    instance = nullptr;
    physical_device = nullptr;

    if (dl_handle != nullptr) {
        ::dlclose(dl_handle);
        dl_handle = nullptr;
    }
    available = false;
}

bool VulkanRuntimeContext::initialize(bool request_validation) {
    shutdown();

    dl_handle = ::dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (dl_handle == nullptr) {
        dl_handle = ::dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (dl_handle == nullptr) {
        device_name = "Headless Software Fallback (libvulkan.so.1 unavailable)";
        driver_info = "headless-ci";
        return false;
    }

    vkGetInstanceProcAddr = reinterpret_cast<ScarPFN_vkGetInstanceProcAddr>(
        ::dlsym(dl_handle, "vkGetInstanceProcAddr"));
    if (vkGetInstanceProcAddr == nullptr) {
        shutdown();
        return false;
    }

    vkEnumerateInstanceVersion = load_instance_proc<ScarPFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr, nullptr, "vkEnumerateInstanceVersion");
    vkEnumerateInstanceLayerProperties = load_instance_proc<ScarPFN_vkEnumerateInstanceLayerProperties>(
        vkGetInstanceProcAddr, nullptr, "vkEnumerateInstanceLayerProperties");
    vkEnumerateInstanceExtensionProperties =
        load_instance_proc<ScarPFN_vkEnumerateInstanceExtensionProperties>(
            vkGetInstanceProcAddr, nullptr, "vkEnumerateInstanceExtensionProperties");
    vkCreateInstance = load_instance_proc<ScarPFN_vkCreateInstance>(
        vkGetInstanceProcAddr, nullptr, "vkCreateInstance");

    if (vkCreateInstance == nullptr) {
        shutdown();
        return false;
    }

    instance_api_version = SCAR_VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion != nullptr) {
        uint32_t queried_ver = SCAR_VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion(&queried_ver) == SCAR_VK_SUCCESS) {
            instance_api_version = queried_ver;
        }
    }

    /* Probe instance layers for VK_LAYER_KHRONOS_validation */
    bool has_validation_layer = false;
    if (request_validation && vkEnumerateInstanceLayerProperties != nullptr) {
        uint32_t layer_count = 0;
        if (vkEnumerateInstanceLayerProperties(&layer_count, nullptr) == SCAR_VK_SUCCESS &&
            layer_count > 0u) {
            std::vector<ScarVkLayerProperties> layers(layer_count);
            if (vkEnumerateInstanceLayerProperties(&layer_count, layers.data()) == SCAR_VK_SUCCESS) {
                for (uint32_t i = 0; i < layer_count; ++i) {
                    if (std::strcmp(layers[i].layerName, SCAR_VK_LAYER_KHRONOS_VALIDATION_NAME) == 0) {
                        has_validation_layer = true;
                        break;
                    }
                }
            }
        }
    }

    /* Probe instance extensions */
    bool has_debug_utils = false;
    bool has_swapchain_colorspace = false;
    if (vkEnumerateInstanceExtensionProperties != nullptr) {
        uint32_t ext_count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr) == SCAR_VK_SUCCESS &&
            ext_count > 0u) {
            std::vector<ScarVkExtensionProperties> exts(ext_count);
            if (vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, exts.data()) ==
                SCAR_VK_SUCCESS) {
                for (uint32_t i = 0; i < ext_count; ++i) {
                    if (std::strcmp(exts[i].extensionName, SCAR_VK_EXT_DEBUG_UTILS_NAME) == 0) {
                        has_debug_utils = true;
                    } else if (std::strcmp(exts[i].extensionName,
                                           SCAR_VK_EXT_SWAPCHAIN_COLOR_SPACE_NAME) == 0) {
                        has_swapchain_colorspace = true;
                    }
                }
            }
        }
    }

    if (has_swapchain_colorspace) {
        caps_mask |= SCARLETT_D3D12XS_CAP_HDR10_PQ;
    }

    std::vector<const char *> enabled_layers;
    if (has_validation_layer) {
        enabled_layers.push_back(SCAR_VK_LAYER_KHRONOS_VALIDATION_NAME);
        validation_layer_enabled = true;
        caps_mask |= SCARLETT_D3D12XS_CAP_VALIDATION_LAYER;
    }

    std::vector<const char *> enabled_inst_exts;
    if (has_debug_utils && has_validation_layer) {
        enabled_inst_exts.push_back(SCAR_VK_EXT_DEBUG_UTILS_NAME);
    }

    uint32_t target_api = SCAR_VK_API_VERSION_1_4;
    if (instance_api_version < target_api) {
        target_api = instance_api_version;
    }

    ScarVkApplicationInfo app_info{};
    app_info.sType = SCAR_VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Proton-Scarlett d3d12_xs";
    app_info.applicationVersion = SCAR_VK_MAKE_API_VERSION(0u, 11u, 0u, 100u);
    app_info.pEngineName = "Scarlett-D3D12XS";
    app_info.engineVersion = SCAR_VK_MAKE_API_VERSION(0u, 3u, 1u, 0u);
    app_info.apiVersion = target_api;

    ScarVkDebugUtilsMessengerCreateInfoEXT dbg_ci{};
    dbg_ci.sType = SCAR_VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg_ci.messageSeverity =
        SCAR_VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        SCAR_VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg_ci.messageType =
        SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        SCAR_VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg_ci.pfnUserCallback = &debug_utils_callback;
    dbg_ci.pUserData = this;

    ScarVkInstanceCreateInfo inst_ci{};
    inst_ci.sType = SCAR_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pNext = (has_debug_utils && has_validation_layer) ? static_cast<const void *>(&dbg_ci) : nullptr;
    inst_ci.pApplicationInfo = &app_info;
    inst_ci.enabledLayerCount = static_cast<uint32_t>(enabled_layers.size());
    inst_ci.ppEnabledLayerNames = enabled_layers.empty() ? nullptr : enabled_layers.data();
    inst_ci.enabledExtensionCount = static_cast<uint32_t>(enabled_inst_exts.size());
    inst_ci.ppEnabledExtensionNames = enabled_inst_exts.empty() ? nullptr : enabled_inst_exts.data();

    if (vkCreateInstance(&inst_ci, nullptr, &instance) != SCAR_VK_SUCCESS || instance == nullptr) {
        /* Retry without layers if validation layer initialization failed */
        validation_layer_enabled = false;
        caps_mask &= ~SCARLETT_D3D12XS_CAP_VALIDATION_LAYER;
        inst_ci.pNext = nullptr;
        inst_ci.enabledLayerCount = 0u;
        inst_ci.ppEnabledLayerNames = nullptr;
        inst_ci.enabledExtensionCount = 0u;
        inst_ci.ppEnabledExtensionNames = nullptr;
        if (vkCreateInstance(&inst_ci, nullptr, &instance) != SCAR_VK_SUCCESS || instance == nullptr) {
            shutdown();
            return false;
        }
    }

    available = true;

    vkDestroyInstance = load_instance_proc<ScarPFN_vkDestroyInstance>(
        vkGetInstanceProcAddr, instance, "vkDestroyInstance");
    vkEnumeratePhysicalDevices = load_instance_proc<ScarPFN_vkEnumeratePhysicalDevices>(
        vkGetInstanceProcAddr, instance, "vkEnumeratePhysicalDevices");
    vkGetPhysicalDeviceProperties = load_instance_proc<ScarPFN_vkGetPhysicalDeviceProperties>(
        vkGetInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties");
    vkGetPhysicalDeviceProperties2 = load_instance_proc<ScarPFN_vkGetPhysicalDeviceProperties2>(
        vkGetInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties2");
    vkGetPhysicalDeviceMemoryProperties =
        load_instance_proc<ScarPFN_vkGetPhysicalDeviceMemoryProperties>(
            vkGetInstanceProcAddr, instance, "vkGetPhysicalDeviceMemoryProperties");
    vkGetPhysicalDeviceQueueFamilyProperties =
        load_instance_proc<ScarPFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            vkGetInstanceProcAddr, instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    vkEnumerateDeviceExtensionProperties =
        load_instance_proc<ScarPFN_vkEnumerateDeviceExtensionProperties>(
            vkGetInstanceProcAddr, instance, "vkEnumerateDeviceExtensionProperties");
    vkCreateDevice = load_instance_proc<ScarPFN_vkCreateDevice>(
        vkGetInstanceProcAddr, instance, "vkCreateDevice");
    vkGetDeviceProcAddr = load_instance_proc<ScarPFN_vkGetDeviceProcAddr>(
        vkGetInstanceProcAddr, instance, "vkGetDeviceProcAddr");

    if (validation_layer_enabled && has_debug_utils) {
        vkCreateDebugUtilsMessengerEXT = load_instance_proc<ScarPFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr, instance, "vkCreateDebugUtilsMessengerEXT");
        vkDestroyDebugUtilsMessengerEXT = load_instance_proc<ScarPFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr, instance, "vkDestroyDebugUtilsMessengerEXT");
        if (vkCreateDebugUtilsMessengerEXT != nullptr) {
            (void)vkCreateDebugUtilsMessengerEXT(instance, &dbg_ci, nullptr, &debug_messenger);
        }
    }

    if (vkEnumeratePhysicalDevices == nullptr || vkGetPhysicalDeviceProperties == nullptr ||
        vkGetPhysicalDeviceQueueFamilyProperties == nullptr ||
        vkEnumerateDeviceExtensionProperties == nullptr) {
        device_name = "Headless Vulkan Instance (Incomplete ICD)";
        return true;
    }

    uint32_t gpu_count = 0;
    if (vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr) != SCAR_VK_SUCCESS ||
        gpu_count == 0u) {
        device_name = "Headless Vulkan Instance (No Physical GPU)";
        return true;
    }

    std::vector<ScarVkPhysicalDevice> gpus(gpu_count);
    if (vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.data()) != SCAR_VK_SUCCESS ||
        gpu_count == 0u) {
        device_name = "Headless Vulkan Instance (Physical GPU Enum Failed)";
        return true;
    }

    /* Prefer discrete GPU, then integrated GPU, then first physical device */
    physical_device = gpus[0];
    int best_score = -1;
    for (uint32_t i = 0; i < gpu_count; ++i) {
        ScarVkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        int score = 1;
        if (props.deviceType == 2u) { /* DISCRETE_GPU */
            score = 100;
        } else if (props.deviceType == 1u) { /* INTEGRATED_GPU */
            score = 50;
        }
        if (score > best_score) {
            best_score = score;
            physical_device = gpus[i];
        }
    }

    ScarVkPhysicalDeviceProperties base_props{};
    vkGetPhysicalDeviceProperties(physical_device, &base_props);
    device_api_version = base_props.apiVersion;
    vendor_id = base_props.vendorID;
    device_id = base_props.deviceID;
    raw_driver_version = base_props.driverVersion;
    device_name = base_props.deviceName;

    if (device_api_version >= SCAR_VK_API_VERSION_1_4) {
        caps_mask |= SCARLETT_D3D12XS_CAP_VULKAN_1_4;
    }

    /* Query VkPhysicalDeviceDriverProperties when Vulkan >= 1.1 */
    if (vkGetPhysicalDeviceProperties2 != nullptr && device_api_version >= SCAR_VK_API_VERSION_1_1) {
        ScarVkPhysicalDeviceDriverProperties driver_props{};
        driver_props.sType = SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        driver_props.pNext = nullptr;

        ScarVkPhysicalDeviceProperties2 props2{};
        props2.sType = SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver_props;

        vkGetPhysicalDeviceProperties2(physical_device, &props2);
        driver_id = driver_props.driverID;
        driver_name = driver_props.driverName;
        driver_info = driver_props.driverInfo;
    }

    driver_version = parse_driver_version(
        vendor_id, driver_id, raw_driver_version,
        driver_info.empty() ? nullptr : driver_info.c_str());

    if (vkGetPhysicalDeviceMemoryProperties != nullptr) {
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    }

    /* Enumerate device extensions */
    std::unordered_set<std::string> dev_exts;
    uint32_t dev_ext_count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &dev_ext_count, nullptr) ==
            SCAR_VK_SUCCESS &&
        dev_ext_count > 0u) {
        std::vector<ScarVkExtensionProperties> ext_list(dev_ext_count);
        if (vkEnumerateDeviceExtensionProperties(
                physical_device, nullptr, &dev_ext_count, ext_list.data()) == SCAR_VK_SUCCESS) {
            for (uint32_t i = 0; i < dev_ext_count; ++i) {
                dev_exts.emplace(ext_list[i].extensionName);
            }
        }
    }

    auto has_ext = [&](const char *name) { return dev_exts.find(name) != dev_exts.end(); };

    if (has_ext(SCAR_VK_EXT_MESH_SHADER_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_MESH_SHADER;
    }
    if (has_ext(SCAR_VK_KHR_FRAGMENT_SHADING_RATE_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_FRAGMENT_SHADING_RATE;
    }
    if (has_ext(SCAR_VK_EXT_SHADER_IMAGE_ATOMIC_INT64_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_IMAGE_ATOMIC_INT64;
    }
    if (has_ext(SCAR_VK_EXT_DEVICE_GENERATED_COMMANDS_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_DEVICE_GENERATED_CMDS;
    }
    if (has_ext(SCAR_VK_KHR_RAY_QUERY_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_RAY_QUERY;
    }
    if (has_ext(SCAR_VK_KHR_OPACITY_MICROMAP_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_OPACITY_MICROMAP;
    }
    const bool ext_device_fault = has_ext(SCAR_VK_EXT_DEVICE_FAULT_NAME);
    if (ext_device_fault) {
        caps_mask |= SCARLETT_D3D12XS_CAP_DEVICE_FAULT;
    }
    if (has_ext(SCAR_VK_EXT_DESCRIPTOR_HEAP_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_DESCRIPTOR_HEAP;
    }
    if (has_ext(SCAR_VK_EXT_DESCRIPTOR_BUFFER_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_DESCRIPTOR_BUFFER;
    }
    if (has_ext(SCAR_VK_EXT_DESCRIPTOR_INDEXING_NAME) ||
        device_api_version >= SCAR_VK_API_VERSION_1_2) {
        caps_mask |= SCARLETT_D3D12XS_CAP_DESCRIPTOR_INDEXING;
    }
    const bool ext_sync2 = has_ext(SCAR_VK_KHR_SYNCHRONIZATION_2_NAME);
    const bool has_sync2 = ext_sync2 || (device_api_version >= SCAR_VK_API_VERSION_1_3);
    if (has_sync2) {
        caps_mask |= SCARLETT_D3D12XS_CAP_SYNCHRONIZATION2;
    }
    if (has_ext(SCAR_VK_EXT_HDR_METADATA_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_HDR_METADATA;
    }
    if (has_ext(SCAR_VK_KHR_PRESENT_WAIT_NAME) && has_ext(SCAR_VK_KHR_PRESENT_ID_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_PRESENT_WAIT;
    }
    if (has_ext(SCAR_VK_GOOGLE_DISPLAY_TIMING_NAME)) {
        caps_mask |= SCARLETT_D3D12XS_CAP_DISPLAY_TIMING;
    }

    /* Separate Vulkan queue families for Xbox Direct (0x0), Async Compute (0x10), DMA (0x2) */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, nullptr);
    std::vector<ScarVkQueueFamilyProperties> qfs(qf_count);
    if (qf_count > 0u) {
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &qf_count, qfs.data());
    }

    for (uint32_t i = 0; i < qf_count; ++i) {
        const ScarVkFlags f = qfs[i].queueFlags;
        if (qfs[i].queueCount == 0u) {
            continue;
        }
        if (direct_queue_family == SCAR_VK_QUEUE_FAMILY_IGNORED &&
            (f & (SCAR_VK_QUEUE_GRAPHICS_BIT | SCAR_VK_QUEUE_COMPUTE_BIT)) ==
                (SCAR_VK_QUEUE_GRAPHICS_BIT | SCAR_VK_QUEUE_COMPUTE_BIT)) {
            direct_queue_family = i;
        }
    }
    if (direct_queue_family == SCAR_VK_QUEUE_FAMILY_IGNORED) {
        for (uint32_t i = 0; i < qf_count; ++i) {
            if (qfs[i].queueCount > 0u && (qfs[i].queueFlags & SCAR_VK_QUEUE_GRAPHICS_BIT) != 0u) {
                direct_queue_family = i;
                break;
            }
        }
    }

    /* Dedicated Async Compute (Xbox 0x10): COMPUTE without GRAPHICS */
    for (uint32_t i = 0; i < qf_count; ++i) {
        const ScarVkFlags f = qfs[i].queueFlags;
        if (qfs[i].queueCount == 0u) {
            continue;
        }
        if ((f & SCAR_VK_QUEUE_COMPUTE_BIT) != 0u && (f & SCAR_VK_QUEUE_GRAPHICS_BIT) == 0u) {
            compute_queue_family = i;
            separate_compute_family = true;
            caps_mask |= SCARLETT_D3D12XS_CAP_SEPARATE_COMPUTE_QUEUE;
            break;
        }
    }
    if (compute_queue_family == SCAR_VK_QUEUE_FAMILY_IGNORED) {
        compute_queue_family = direct_queue_family;
    }

    /* Dedicated DMA/Copy (Xbox 0x2): TRANSFER without GRAPHICS and without COMPUTE */
    for (uint32_t i = 0; i < qf_count; ++i) {
        const ScarVkFlags f = qfs[i].queueFlags;
        if (qfs[i].queueCount == 0u) {
            continue;
        }
        if ((f & SCAR_VK_QUEUE_TRANSFER_BIT) != 0u &&
            (f & SCAR_VK_QUEUE_GRAPHICS_BIT) == 0u &&
            (f & SCAR_VK_QUEUE_COMPUTE_BIT) == 0u) {
            dma_queue_family = i;
            separate_dma_family = true;
            caps_mask |= SCARLETT_D3D12XS_CAP_SEPARATE_DMA_QUEUE;
            break;
        }
    }
    if (dma_queue_family == SCAR_VK_QUEUE_FAMILY_IGNORED) {
        dma_queue_family = compute_queue_family != SCAR_VK_QUEUE_FAMILY_IGNORED
                               ? compute_queue_family
                               : direct_queue_family;
    }

    if (direct_queue_family == SCAR_VK_QUEUE_FAMILY_IGNORED || vkCreateDevice == nullptr ||
        vkGetDeviceProcAddr == nullptr) {
        return true;
    }

    /* Build unique queue create infos */
    std::vector<uint32_t> unique_families;
    auto add_unique = [&](uint32_t fam) {
        if (fam == SCAR_VK_QUEUE_FAMILY_IGNORED) {
            return;
        }
        if (std::find(unique_families.begin(), unique_families.end(), fam) == unique_families.end()) {
            unique_families.push_back(fam);
        }
    };
    add_unique(direct_queue_family);
    add_unique(compute_queue_family);
    add_unique(dma_queue_family);

    const float queue_priority = 1.0f;
    std::vector<ScarVkDeviceQueueCreateInfo> queue_cis;
    queue_cis.reserve(unique_families.size());
    for (uint32_t fam : unique_families) {
        ScarVkDeviceQueueCreateInfo qci{};
        qci.sType = SCAR_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount = 1u;
        qci.pQueuePriorities = &queue_priority;
        queue_cis.push_back(qci);
    }

    std::vector<const char *> enabled_dev_exts;
    if (ext_sync2 && device_api_version < SCAR_VK_API_VERSION_1_3) {
        enabled_dev_exts.push_back(SCAR_VK_KHR_SYNCHRONIZATION_2_NAME);
    }
    if (ext_device_fault) {
        enabled_dev_exts.push_back(SCAR_VK_EXT_DEVICE_FAULT_NAME);
    }

    ScarVkPhysicalDeviceSynchronization2Features sync2_feat{};
    sync2_feat.sType = SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    sync2_feat.synchronization2 = has_sync2 ? SCAR_VK_TRUE : SCAR_VK_FALSE;

    ScarVkPhysicalDeviceDeviceFaultFeaturesEXT fault_feat{};
    fault_feat.sType = SCAR_VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEVICE_FAULT_FEATURES_EXT;
    fault_feat.deviceFault = ext_device_fault ? SCAR_VK_TRUE : SCAR_VK_FALSE;
    fault_feat.deviceFaultVendorBinary = SCAR_VK_FALSE;

    void *pnext_chain = nullptr;
    if (has_sync2) {
        sync2_feat.pNext = pnext_chain;
        pnext_chain = &sync2_feat;
    }
    if (ext_device_fault) {
        fault_feat.pNext = pnext_chain;
        pnext_chain = &fault_feat;
    }

    ScarVkDeviceCreateInfo dev_ci{};
    dev_ci.sType = SCAR_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.pNext = pnext_chain;
    dev_ci.queueCreateInfoCount = static_cast<uint32_t>(queue_cis.size());
    dev_ci.pQueueCreateInfos = queue_cis.data();
    dev_ci.enabledExtensionCount = static_cast<uint32_t>(enabled_dev_exts.size());
    dev_ci.ppEnabledExtensionNames = enabled_dev_exts.empty() ? nullptr : enabled_dev_exts.data();

    if (vkCreateDevice(physical_device, &dev_ci, nullptr, &device) != SCAR_VK_SUCCESS ||
        device == nullptr) {
        /* Safe fallback without optional feature chain if driver rejects a feature struct */
        dev_ci.pNext = nullptr;
        dev_ci.enabledExtensionCount = 0u;
        dev_ci.ppEnabledExtensionNames = nullptr;
        if (vkCreateDevice(physical_device, &dev_ci, nullptr, &device) != SCAR_VK_SUCCESS ||
            device == nullptr) {
            return true;
        }
    }

    real_device_created = true;
    caps_mask |= SCARLETT_D3D12XS_CAP_REAL_VK_DEVICE;

    /* Load device-level Vulkan functions */
    vkDestroyDevice = load_device_proc<ScarPFN_vkDestroyDevice>(vkGetDeviceProcAddr, device, "vkDestroyDevice");
    vkGetDeviceQueue = load_device_proc<ScarPFN_vkGetDeviceQueue>(vkGetDeviceProcAddr, device, "vkGetDeviceQueue");
    vkQueueWaitIdle = load_device_proc<ScarPFN_vkQueueWaitIdle>(vkGetDeviceProcAddr, device, "vkQueueWaitIdle");
    vkDeviceWaitIdle = load_device_proc<ScarPFN_vkDeviceWaitIdle>(vkGetDeviceProcAddr, device, "vkDeviceWaitIdle");
    vkCreateCommandPool = load_device_proc<ScarPFN_vkCreateCommandPool>(vkGetDeviceProcAddr, device, "vkCreateCommandPool");
    vkDestroyCommandPool = load_device_proc<ScarPFN_vkDestroyCommandPool>(vkGetDeviceProcAddr, device, "vkDestroyCommandPool");
    vkAllocateCommandBuffers = load_device_proc<ScarPFN_vkAllocateCommandBuffers>(vkGetDeviceProcAddr, device, "vkAllocateCommandBuffers");
    vkFreeCommandBuffers = load_device_proc<ScarPFN_vkFreeCommandBuffers>(vkGetDeviceProcAddr, device, "vkFreeCommandBuffers");
    vkBeginCommandBuffer = load_device_proc<ScarPFN_vkBeginCommandBuffer>(vkGetDeviceProcAddr, device, "vkBeginCommandBuffer");
    vkEndCommandBuffer = load_device_proc<ScarPFN_vkEndCommandBuffer>(vkGetDeviceProcAddr, device, "vkEndCommandBuffer");
    vkResetCommandBuffer = load_device_proc<ScarPFN_vkResetCommandBuffer>(vkGetDeviceProcAddr, device, "vkResetCommandBuffer");
    vkCreateFence = load_device_proc<ScarPFN_vkCreateFence>(vkGetDeviceProcAddr, device, "vkCreateFence");
    vkDestroyFence = load_device_proc<ScarPFN_vkDestroyFence>(vkGetDeviceProcAddr, device, "vkDestroyFence");
    vkWaitForFences = load_device_proc<ScarPFN_vkWaitForFences>(vkGetDeviceProcAddr, device, "vkWaitForFences");
    vkResetFences = load_device_proc<ScarPFN_vkResetFences>(vkGetDeviceProcAddr, device, "vkResetFences");
    vkQueueSubmit = load_device_proc<ScarPFN_vkQueueSubmit>(vkGetDeviceProcAddr, device, "vkQueueSubmit");
    vkCreateBuffer = load_device_proc<ScarPFN_vkCreateBuffer>(vkGetDeviceProcAddr, device, "vkCreateBuffer");
    vkDestroyBuffer = load_device_proc<ScarPFN_vkDestroyBuffer>(vkGetDeviceProcAddr, device, "vkDestroyBuffer");
    vkGetBufferMemoryRequirements = load_device_proc<ScarPFN_vkGetBufferMemoryRequirements>(vkGetDeviceProcAddr, device, "vkGetBufferMemoryRequirements");
    vkAllocateMemory = load_device_proc<ScarPFN_vkAllocateMemory>(vkGetDeviceProcAddr, device, "vkAllocateMemory");
    vkFreeMemory = load_device_proc<ScarPFN_vkFreeMemory>(vkGetDeviceProcAddr, device, "vkFreeMemory");
    vkBindBufferMemory = load_device_proc<ScarPFN_vkBindBufferMemory>(vkGetDeviceProcAddr, device, "vkBindBufferMemory");
    vkMapMemory = load_device_proc<ScarPFN_vkMapMemory>(vkGetDeviceProcAddr, device, "vkMapMemory");
    vkUnmapMemory = load_device_proc<ScarPFN_vkUnmapMemory>(vkGetDeviceProcAddr, device, "vkUnmapMemory");

    vkCmdPipelineBarrier2 = load_device_proc<ScarPFN_vkCmdPipelineBarrier2>(
        vkGetDeviceProcAddr, device, "vkCmdPipelineBarrier2");
    if (vkCmdPipelineBarrier2 == nullptr) {
        vkCmdPipelineBarrier2 = load_device_proc<ScarPFN_vkCmdPipelineBarrier2>(
            vkGetDeviceProcAddr, device, "vkCmdPipelineBarrier2KHR");
    }
    vkCmdPipelineBarrier = load_device_proc<ScarPFN_vkCmdPipelineBarrier>(
        vkGetDeviceProcAddr, device, "vkCmdPipelineBarrier");
    vkCmdCopyBuffer = load_device_proc<ScarPFN_vkCmdCopyBuffer>(
        vkGetDeviceProcAddr, device, "vkCmdCopyBuffer");
    vkCmdDrawIndexedIndirect = load_device_proc<ScarPFN_vkCmdDrawIndexedIndirect>(
        vkGetDeviceProcAddr, device, "vkCmdDrawIndexedIndirect");
    vkCmdDispatchIndirect = load_device_proc<ScarPFN_vkCmdDispatchIndirect>(
        vkGetDeviceProcAddr, device, "vkCmdDispatchIndirect");
    vkGetDeviceFaultInfoEXT = load_device_proc<ScarPFN_vkGetDeviceFaultInfoEXT>(
        vkGetDeviceProcAddr, device, "vkGetDeviceFaultInfoEXT");

    if (vkGetDeviceQueue != nullptr) {
        vkGetDeviceQueue(device, direct_queue_family, 0u, &direct_queue);
        vkGetDeviceQueue(device, compute_queue_family, 0u, &compute_queue);
        vkGetDeviceQueue(device, dma_queue_family, 0u, &dma_queue);
    }

    if (vkCreateCommandPool != nullptr) {
        auto make_pool = [&](uint32_t family) -> ScarVkCommandPool {
            ScarVkCommandPool pool = SCAR_VK_NULL_HANDLE;
            ScarVkCommandPoolCreateInfo pci{};
            pci.sType = SCAR_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pci.flags = SCAR_VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pci.queueFamilyIndex = family;
            if (vkCreateCommandPool(device, &pci, nullptr, &pool) != SCAR_VK_SUCCESS) {
                return SCAR_VK_NULL_HANDLE;
            }
            return pool;
        };

        direct_pool = make_pool(direct_queue_family);
        compute_pool = (compute_queue_family == direct_queue_family)
                           ? direct_pool
                           : make_pool(compute_queue_family);
        if (dma_queue_family == direct_queue_family) {
            dma_pool = direct_pool;
        } else if (dma_queue_family == compute_queue_family) {
            dma_pool = compute_pool;
        } else {
            dma_pool = make_pool(dma_queue_family);
        }
    }

    return true;
}

ScarVkQueue VulkanRuntimeContext::queue_for_type(
    uint32_t queue_type,
    uint32_t *out_family,
    bool *out_dedicated) const {
    if (queue_type == SCARLETT_D3D12_QUEUE_TYPE_DMA) {
        if (out_family != nullptr) *out_family = dma_queue_family;
        if (out_dedicated != nullptr) *out_dedicated = separate_dma_family;
        return dma_queue;
    }
    if (queue_type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
        if (out_family != nullptr) *out_family = compute_queue_family;
        if (out_dedicated != nullptr) *out_dedicated = separate_compute_family;
        return compute_queue;
    }
    if (out_family != nullptr) *out_family = direct_queue_family;
    if (out_dedicated != nullptr) *out_dedicated = true;
    return direct_queue;
}

ScarVkCommandPool VulkanRuntimeContext::pool_for_type(uint32_t queue_type) const {
    if (queue_type == SCARLETT_D3D12_QUEUE_TYPE_DMA) {
        return dma_pool;
    }
    if (queue_type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
        return compute_pool;
    }
    return direct_pool;
}

bool VulkanRuntimeContext::submit_empty_or_recorded_cmd(
    uint32_t queue_type,
    const std::vector<ScarVkMemoryBarrier2> &barriers,
    uint32_t &out_vk_submitted) {
    out_vk_submitted = 0u;
    if (!real_device_created || device == nullptr || vkAllocateCommandBuffers == nullptr ||
        vkBeginCommandBuffer == nullptr || vkEndCommandBuffer == nullptr ||
        vkQueueSubmit == nullptr || vkCreateFence == nullptr || vkWaitForFences == nullptr ||
        vkDestroyFence == nullptr || vkFreeCommandBuffers == nullptr) {
        return false;
    }

    ScarVkQueue queue = queue_for_type(queue_type, nullptr, nullptr);
    ScarVkCommandPool pool = pool_for_type(queue_type);
    if (queue == nullptr || pool == SCAR_VK_NULL_HANDLE) {
        return false;
    }

    ScarVkCommandBuffer cmd = nullptr;
    ScarVkCommandBufferAllocateInfo alloc_ci{};
    alloc_ci.sType = SCAR_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_ci.commandPool = pool;
    alloc_ci.level = SCAR_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = 1u;

    if (vkAllocateCommandBuffers(device, &alloc_ci, &cmd) != SCAR_VK_SUCCESS || cmd == nullptr) {
        return false;
    }

    ScarVkCommandBufferBeginInfo begin_ci{};
    begin_ci.sType = SCAR_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = SCAR_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    bool ok = false;
    if (vkBeginCommandBuffer(cmd, &begin_ci) == SCAR_VK_SUCCESS) {
        if (!barriers.empty()) {
            /* Clamp barrier stage masks to queue family capabilities for 0 validation errors */
            std::vector<ScarVkMemoryBarrier2> sanitized = barriers;
            for (auto &b : sanitized) {
                b.sType = SCAR_VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                b.pNext = nullptr;
                if (queue_type == SCARLETT_D3D12_QUEUE_TYPE_DMA) {
                    b.srcStageMask = SCAR_VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    b.dstStageMask = SCAR_VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    b.srcAccessMask = SCAR_VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    b.dstAccessMask = SCAR_VK_ACCESS_2_TRANSFER_READ_BIT;
                } else if (queue_type == SCARLETT_D3D12_QUEUE_TYPE_COMPUTE_XS) {
                    b.srcStageMask = SCAR_VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    b.dstStageMask = SCAR_VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                }
            }

            if (vkCmdPipelineBarrier2 != nullptr &&
                (caps_mask & SCARLETT_D3D12XS_CAP_SYNCHRONIZATION2) != 0u) {
                ScarVkDependencyInfo dep{};
                dep.sType = SCAR_VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.memoryBarrierCount = static_cast<uint32_t>(sanitized.size());
                dep.pMemoryBarriers = sanitized.data();
                vkCmdPipelineBarrier2(cmd, &dep);
            } else if (vkCmdPipelineBarrier != nullptr) {
                ScarVkMemoryBarrier mb{};
                mb.sType = SCAR_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                mb.srcAccessMask = 0x00010000u; /* MEMORY_WRITE */
                mb.dstAccessMask = 0x00008000u; /* MEMORY_READ */
                vkCmdPipelineBarrier(cmd, 0x00010000u, 0x00010000u, 0u, 1u, &mb, 0u, nullptr, 0u, nullptr);
            }
        }

        if (vkEndCommandBuffer(cmd) == SCAR_VK_SUCCESS) {
            ScarVkFence fence = SCAR_VK_NULL_HANDLE;
            ScarVkFenceCreateInfo fci{};
            fci.sType = SCAR_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(device, &fci, nullptr, &fence) == SCAR_VK_SUCCESS) {
                ScarVkSubmitInfo si{};
                si.sType = SCAR_VK_STRUCTURE_TYPE_SUBMIT_INFO;
                si.commandBufferCount = 1u;
                si.pCommandBuffers = &cmd;
                if (vkQueueSubmit(queue, 1u, &si, fence) == SCAR_VK_SUCCESS) {
                    (void)vkWaitForFences(device, 1u, &fence, SCAR_VK_TRUE, UINT64_C(5000000000));
                    out_vk_submitted = 1u;
                    ok = true;
                }
                vkDestroyFence(device, fence, nullptr);
            }
        }
    }

    vkFreeCommandBuffers(device, pool, 1u, &cmd);
    return ok;
}

std::string VulkanRuntimeContext::query_device_fault_info(bool *out_queried) const {
    if (out_queried != nullptr) {
        *out_queried = false;
    }
    if (!real_device_created || device == nullptr || vkGetDeviceFaultInfoEXT == nullptr ||
        (caps_mask & SCARLETT_D3D12XS_CAP_DEVICE_FAULT) == 0u) {
        return "VK_EXT_device_fault: unavailable on current device";
    }

    ScarVkDeviceFaultCountsEXT counts{};
    counts.sType = SCAR_VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
    ScarVkResult res = vkGetDeviceFaultInfoEXT(device, &counts, nullptr);
    if (out_queried != nullptr) {
        *out_queried = true;
    }
    if (res != SCAR_VK_SUCCESS && res != SCAR_VK_INCOMPLETE) {
        std::ostringstream oss;
        oss << "VK_EXT_device_fault: queried (VkResult=" << res << ", no active GPU fault)";
        return oss.str();
    }

    std::vector<ScarVkDeviceFaultAddressInfoEXT> addrs(counts.addressInfoCount);
    std::vector<ScarVkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
    ScarVkDeviceFaultInfoEXT info{};
    info.sType = SCAR_VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
    info.pAddressInfos = addrs.empty() ? nullptr : addrs.data();
    info.pVendorInfos = vendors.empty() ? nullptr : vendors.data();

    counts.vendorBinarySize = 0u;
    (void)vkGetDeviceFaultInfoEXT(device, &counts, &info);

    std::ostringstream oss;
    oss << "VK_EXT_device_fault: description=\""
        << (info.description[0] != '\0' ? info.description : "NONE")
        << "\" addressInfos=" << counts.addressInfoCount
        << " vendorInfos=" << counts.vendorInfoCount;
    for (uint32_t i = 0; i < counts.addressInfoCount; ++i) {
        oss << " [addr#" << i << ":type=" << addrs[i].addressType
            << ",va=0x" << std::hex << addrs[i].reportedAddress << std::dec << "]";
    }
    return oss.str();
}

} // namespace scarlett::d3d12xs::unixlib
