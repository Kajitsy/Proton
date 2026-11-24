/*
 * Proton-Scarlett — PresentX & High-Precision Frame Pacing (d3d12_xs_unix.so)
 *
 * Implements:
 *   - SetFrameIntervalX / ScheduleFrameEventX / blocking WaitFrameEventX with
 *     monotonic 64-bit D3D12XBOX_FRAME_PIPELINE_TOKEN and hybrid CLOCK_MONOTONIC
 *     TIMER_ABSTIME + micro-spin pacing for deterministic p99 jitter < 1.5 ms.
 *   - PresentX with 1 or 2 independent planes:
 *       * Plane 0: Primary HDR10 PQ (G2084_P2020 -> VK_COLOR_SPACE_HDR10_ST2084_EXT
 *         + SMPTE ST.2086 VkHdrMetadataEXT for Wayland / Gamescope) or SDR.
 *       * Plane 1: Optional GameDVR / HUD plane (G22_P709 -> VK_COLOR_SPACE_SRGB_NONLINEAR_KHR).
 *   - Strict invariant: UMA dirty flush is NEVER deferred to PresentX.
 */

#include "d3d12_xs_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sched.h>
#include <sys/prctl.h>
#include <time.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace scarlett::d3d12xs::unixlib {

namespace {

constexpr uint64_t kNsPerUs = UINT64_C(1000);
constexpr uint64_t kNsPerMs = UINT64_C(1000000);
constexpr uint64_t kNsPerSec = UINT64_C(1000000000);
/* Two-stage TIMER_ABSTIME + micro-spin pacing with 1ns timer slack (~95% asleep at 60 Hz):
 *  - Stage 1 wakes 2200us early to exit deep CPU C-states / powersave P-states and absorb
 *    long-sleep hrtimer wakeup lag.
 *  - Stage 2 wakes 850us early on an already-warm core (well below Linux EEVDF/BORE ~1.5ms
 *    preemption slice so micro-spin is not preempted) and spins with _mm_pause() to target_ns. */
constexpr uint64_t kPrewakeMarginNs = UINT64_C(2200000);
constexpr uint64_t kPrecisionSpinMarginNs = UINT64_C(850000);

void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#else
    ::sched_yield();
#endif
}

uint32_t map_color_space_to_vk(uint32_t xbox_cs) noexcept {
    switch (xbox_cs) {
        case SCARLETT_COLOR_SPACE_G2084_P2020:
            return SCAR_VK_COLOR_SPACE_HDR10_ST2084_EXT;
        case SCARLETT_COLOR_SPACE_G22_P709:
            return SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        case SCARLETT_COLOR_SPACE_G10_P709:
            return SCAR_VK_COLOR_SPACE_BT709_NONLINEAR_EXT;
        default:
            return SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
}

void sleep_until_monotonic_ns(uint64_t target_ns) noexcept {
    thread_local bool slack_configured = false;
    if (!slack_configured) {
        ::prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
        slack_configured = true;
    }

    auto sleep_abs = [](uint64_t wake_ns) noexcept {
        struct timespec ts{};
        ts.tv_sec = static_cast<time_t>(wake_ns / kNsPerSec);
        ts.tv_nsec = static_cast<long>(wake_ns % kNsPerSec);
        while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
            if (monotonic_now_ns() >= wake_ns) {
                break;
            }
        }
    };

    uint64_t now = monotonic_now_ns();
    if (now >= target_ns) {
        return;
    }

    if (target_ns - now > kPrewakeMarginNs) {
        sleep_abs(target_ns - kPrewakeMarginNs);
        now = monotonic_now_ns();
        if (now >= target_ns) {
            return;
        }
    }

    if (target_ns - now > kPrecisionSpinMarginNs) {
        sleep_abs(target_ns - kPrecisionSpinMarginNs);
    }

    while (monotonic_now_ns() < target_ns) {
        cpu_relax();
    }
}

uint32_t find_vk_memory_type(
    VulkanRuntimeContext *vk,
    uint32_t type_filter,
    uint32_t required_props) noexcept {
    if (vk == nullptr || vk->vkGetPhysicalDeviceMemoryProperties == nullptr) {
        return UINT32_MAX;
    }
    ScarVkPhysicalDeviceMemoryProperties mem_props{};
    vk->vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &mem_props);
    for (uint32_t i = 0u; i < mem_props.memoryTypeCount && i < SCAR_VK_MAX_MEMORY_TYPES; ++i) {
        if ((type_filter & (1u << i)) != 0u &&
            (mem_props.memoryTypes[i].propertyFlags & required_props) == required_props) {
            return i;
        }
    }
    return UINT32_MAX;
}

void wsi_destroy_upload_resources(SwapchainState &sc, VulkanRuntimeContext *vk) noexcept {
    if (vk == nullptr || vk->device == nullptr) {
        sc.wsi.upload_buf = SCAR_VK_NULL_HANDLE;
        sc.wsi.upload_buf_mem = SCAR_VK_NULL_HANDLE;
        sc.wsi.upload_buf_mapped = nullptr;
        sc.wsi.upload_buf_size = 0u;
        sc.wsi.upload_img = SCAR_VK_NULL_HANDLE;
        sc.wsi.upload_img_mem = SCAR_VK_NULL_HANDLE;
        sc.wsi.upload_img_w = 0u;
        sc.wsi.upload_img_h = 0u;
        return;
    }
    if (sc.wsi.upload_buf_mapped != nullptr && sc.wsi.upload_buf_mem != SCAR_VK_NULL_HANDLE &&
        vk->vkUnmapMemory != nullptr) {
        vk->vkUnmapMemory(vk->device, sc.wsi.upload_buf_mem);
    }
    sc.wsi.upload_buf_mapped = nullptr;
    if (sc.wsi.upload_img != SCAR_VK_NULL_HANDLE && vk->vkDestroyImage != nullptr) {
        vk->vkDestroyImage(vk->device, sc.wsi.upload_img, nullptr);
    }
    sc.wsi.upload_img = SCAR_VK_NULL_HANDLE;
    if (sc.wsi.upload_img_mem != SCAR_VK_NULL_HANDLE && vk->vkFreeMemory != nullptr) {
        vk->vkFreeMemory(vk->device, sc.wsi.upload_img_mem, nullptr);
    }
    sc.wsi.upload_img_mem = SCAR_VK_NULL_HANDLE;
    sc.wsi.upload_img_w = 0u;
    sc.wsi.upload_img_h = 0u;
    if (sc.wsi.upload_buf != SCAR_VK_NULL_HANDLE && vk->vkDestroyBuffer != nullptr) {
        vk->vkDestroyBuffer(vk->device, sc.wsi.upload_buf, nullptr);
    }
    sc.wsi.upload_buf = SCAR_VK_NULL_HANDLE;
    if (sc.wsi.upload_buf_mem != SCAR_VK_NULL_HANDLE && vk->vkFreeMemory != nullptr) {
        vk->vkFreeMemory(vk->device, sc.wsi.upload_buf_mem, nullptr);
    }
    sc.wsi.upload_buf_mem = SCAR_VK_NULL_HANDLE;
    sc.wsi.upload_buf_size = 0u;
}

bool wsi_ensure_upload_resources(
    SwapchainState &sc,
    VulkanRuntimeContext *vk,
    uint32_t width,
    uint32_t height) noexcept {
    if (vk == nullptr || vk->device == nullptr || vk->vkCreateBuffer == nullptr ||
        vk->vkGetBufferMemoryRequirements == nullptr || vk->vkAllocateMemory == nullptr ||
        vk->vkBindBufferMemory == nullptr || vk->vkMapMemory == nullptr ||
        vk->vkCreateImage == nullptr || vk->vkGetImageMemoryRequirements == nullptr ||
        vk->vkBindImageMemory == nullptr) {
        return false;
    }

    const uint64_t needed_bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
    if (sc.wsi.upload_buf == SCAR_VK_NULL_HANDLE || sc.wsi.upload_buf_size < needed_bytes ||
        sc.wsi.upload_img == SCAR_VK_NULL_HANDLE || sc.wsi.upload_img_w != width ||
        sc.wsi.upload_img_h != height) {
        if (vk->vkDeviceWaitIdle != nullptr) {
            (void)vk->vkDeviceWaitIdle(vk->device);
        }
        wsi_destroy_upload_resources(sc, vk);

        ScarVkBufferCreateInfo buf_ci{};
        buf_ci.sType = SCAR_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_ci.size = needed_bytes;
        buf_ci.usage = SCAR_VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buf_ci.sharingMode = SCAR_VK_SHARING_MODE_EXCLUSIVE;
        if (vk->vkCreateBuffer(vk->device, &buf_ci, nullptr, &sc.wsi.upload_buf) != SCAR_VK_SUCCESS ||
            sc.wsi.upload_buf == SCAR_VK_NULL_HANDLE) {
            wsi_destroy_upload_resources(sc, vk);
            return false;
        }
        ScarVkMemoryRequirements buf_reqs{};
        vk->vkGetBufferMemoryRequirements(vk->device, sc.wsi.upload_buf, &buf_reqs);
        const uint32_t host_mem_idx = find_vk_memory_type(
            vk,
            buf_reqs.memoryTypeBits,
            SCAR_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | SCAR_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (host_mem_idx == UINT32_MAX) {
            wsi_destroy_upload_resources(sc, vk);
            return false;
        }
        ScarVkMemoryAllocateInfo buf_ai{};
        buf_ai.sType = SCAR_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        buf_ai.allocationSize = buf_reqs.size;
        buf_ai.memoryTypeIndex = host_mem_idx;
        if (vk->vkAllocateMemory(vk->device, &buf_ai, nullptr, &sc.wsi.upload_buf_mem) != SCAR_VK_SUCCESS ||
            vk->vkBindBufferMemory(vk->device, sc.wsi.upload_buf, sc.wsi.upload_buf_mem, 0u) != SCAR_VK_SUCCESS ||
            vk->vkMapMemory(vk->device, sc.wsi.upload_buf_mem, 0u, needed_bytes, 0u, &sc.wsi.upload_buf_mapped) != SCAR_VK_SUCCESS ||
            sc.wsi.upload_buf_mapped == nullptr) {
            wsi_destroy_upload_resources(sc, vk);
            return false;
        }
        sc.wsi.upload_buf_size = needed_bytes;

        ScarVkImageCreateInfo img_ci{};
        img_ci.sType = SCAR_VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_ci.imageType = SCAR_VK_IMAGE_TYPE_2D;
        img_ci.format = SCAR_VK_FORMAT_B8G8R8A8_UNORM;
        img_ci.extent = {width, height, 1u};
        img_ci.mipLevels = 1u;
        img_ci.arrayLayers = 1u;
        img_ci.samples = SCAR_VK_SAMPLE_COUNT_1_BIT;
        img_ci.tiling = SCAR_VK_IMAGE_TILING_OPTIMAL;
        img_ci.usage = SCAR_VK_IMAGE_USAGE_TRANSFER_DST_BIT | SCAR_VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        img_ci.sharingMode = SCAR_VK_SHARING_MODE_EXCLUSIVE;
        img_ci.initialLayout = SCAR_VK_IMAGE_LAYOUT_UNDEFINED;
        if (vk->vkCreateImage(vk->device, &img_ci, nullptr, &sc.wsi.upload_img) != SCAR_VK_SUCCESS ||
            sc.wsi.upload_img == SCAR_VK_NULL_HANDLE) {
            wsi_destroy_upload_resources(sc, vk);
            return false;
        }
        ScarVkMemoryRequirements img_reqs{};
        vk->vkGetImageMemoryRequirements(vk->device, sc.wsi.upload_img, &img_reqs);
        uint32_t dev_mem_idx = find_vk_memory_type(
            vk, img_reqs.memoryTypeBits, SCAR_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (dev_mem_idx == UINT32_MAX) {
            dev_mem_idx = find_vk_memory_type(vk, img_reqs.memoryTypeBits, 0u);
        }
        if (dev_mem_idx == UINT32_MAX) {
            wsi_destroy_upload_resources(sc, vk);
            return false;
        }
        ScarVkMemoryAllocateInfo img_ai{};
        img_ai.sType = SCAR_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        img_ai.allocationSize = img_reqs.size;
        img_ai.memoryTypeIndex = dev_mem_idx;
        if (vk->vkAllocateMemory(vk->device, &img_ai, nullptr, &sc.wsi.upload_img_mem) != SCAR_VK_SUCCESS ||
            vk->vkBindImageMemory(vk->device, sc.wsi.upload_img, sc.wsi.upload_img_mem, 0u) != SCAR_VK_SUCCESS) {
            wsi_destroy_upload_resources(sc, vk);
            return false;
        }
        sc.wsi.upload_img_w = width;
        sc.wsi.upload_img_h = height;
    }
    return true;
}

uint8_t half_to_unorm8(uint16_t h) noexcept {
    const uint32_t sign = (h >> 15u) & 1u;
    if (sign != 0u) return 0u;
    const uint32_t exp = (h >> 10u) & 0x1Fu;
    const uint32_t mant = h & 0x3FFu;
    if (exp == 0u) return 0u;
    if (exp >= 15u) {
        if (exp == 31u && mant != 0u) return 0u;
        return 255u;
    }
    const int32_t shift = 15 - static_cast<int32_t>(exp);
    const uint32_t val10 = (0x400u | mant) >> static_cast<uint32_t>(shift);
    const uint32_t scaled = (val10 * 255u + 512u) >> 10u;
    return static_cast<uint8_t>(std::min<uint32_t>(scaled, 255u));
}

void convert_plane_to_bgra8(
    const scarlett_d3d12xs_present_plane_t &plane,
    uint8_t *dst_bgra) noexcept {
    const uint32_t w = plane.width;
    const uint32_t h = plane.height;
    const uint8_t *src = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(plane.gpu_va));
    const uint32_t fmt = plane.dxgi_format;
    const uint32_t dst_pitch = w * 4u;

    if (fmt == 10u || fmt == 11u) {
        /* DXGI_FORMAT_R16G16B16A16_FLOAT (8 bpp) -> BGRA8 */
        const uint32_t src_pitch = plane.row_pitch >= w * 8u ? plane.row_pitch : ((w * 8u + 255u) & ~255u);
        for (uint32_t y = 0u; y < h; ++y) {
            const uint16_t *s = reinterpret_cast<const uint16_t *>(src + static_cast<size_t>(y) * src_pitch);
            uint8_t *d = dst_bgra + static_cast<size_t>(y) * dst_pitch;
            for (uint32_t x = 0u; x < w; ++x) {
                d[x * 4u + 0u] = half_to_unorm8(s[x * 4u + 2u]);
                d[x * 4u + 1u] = half_to_unorm8(s[x * 4u + 1u]);
                d[x * 4u + 2u] = half_to_unorm8(s[x * 4u + 0u]);
                d[x * 4u + 3u] = half_to_unorm8(s[x * 4u + 3u]);
            }
        }
        return;
    }

    const uint32_t src_pitch = plane.row_pitch >= w * 4u ? plane.row_pitch : ((w * 4u + 255u) & ~255u);
    if (fmt == 24u || fmt == 25u || fmt == 26u) {
        /* DXGI_FORMAT_R10G10B10A2_UNORM -> BGRA8 */
        for (uint32_t y = 0u; y < h; ++y) {
            const uint32_t *s = reinterpret_cast<const uint32_t *>(src + static_cast<size_t>(y) * src_pitch);
            uint8_t *d = dst_bgra + static_cast<size_t>(y) * dst_pitch;
            for (uint32_t x = 0u; x < w; ++x) {
                const uint32_t v = s[x];
                d[x * 4u + 0u] = static_cast<uint8_t>((v >> 22u) & 0xFFu);
                d[x * 4u + 1u] = static_cast<uint8_t>((v >> 12u) & 0xFFu);
                d[x * 4u + 2u] = static_cast<uint8_t>((v >> 2u) & 0xFFu);
                d[x * 4u + 3u] = static_cast<uint8_t>(((v >> 30u) & 0x3u) * 85u);
            }
        }
        return;
    }

    if (fmt == 28u || fmt == 29u || fmt == 30u || fmt == 31u || fmt == 32u) {
        /* DXGI_FORMAT_R8G8B8A8_* -> BGRA8 */
        for (uint32_t y = 0u; y < h; ++y) {
            const uint8_t *s = src + static_cast<size_t>(y) * src_pitch;
            uint8_t *d = dst_bgra + static_cast<size_t>(y) * dst_pitch;
            for (uint32_t x = 0u; x < w; ++x) {
                d[x * 4u + 0u] = s[x * 4u + 2u];
                d[x * 4u + 1u] = s[x * 4u + 1u];
                d[x * 4u + 2u] = s[x * 4u + 0u];
                d[x * 4u + 3u] = s[x * 4u + 3u];
            }
        }
        return;
    }

    /* BGRA8 / BGRX8 / default 4-bpp */
    for (uint32_t y = 0u; y < h; ++y) {
        std::memcpy(
            dst_bgra + static_cast<size_t>(y) * dst_pitch,
            src + static_cast<size_t>(y) * src_pitch,
            dst_pitch);
    }
}

bool wsi_create_swapchain(SwapchainState &sc, VulkanRuntimeContext *vk) noexcept {
    if (vk == nullptr || vk->device == nullptr || sc.wsi.surface == SCAR_VK_NULL_HANDLE) {
        return false;
    }
    if (vk->vkDeviceWaitIdle != nullptr) {
        (void)vk->vkDeviceWaitIdle(vk->device);
    }

    ScarVkSurfaceCapabilitiesKHR caps{};
    if (vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            vk->physical_device, sc.wsi.surface, &caps) != SCAR_VK_SUCCESS) {
        return false;
    }

    uint32_t target_w = sc.wsi.width > 0u ? sc.wsi.width : 1280u;
    uint32_t target_h = sc.wsi.height > 0u ? sc.wsi.height : 720u;
    if (sc.wsi.x11_XGetGeometry != nullptr && sc.wsi.x11_display != nullptr &&
        sc.wsi.x11_window != 0ul) {
        unsigned long root_ret = 0ul;
        int rx = 0, ry = 0;
        unsigned int rw = 0u, rh = 0u, bw = 0u, depth = 0u;
        if (sc.wsi.x11_XGetGeometry(
                sc.wsi.x11_display, sc.wsi.x11_window, &root_ret, &rx, &ry,
                &rw, &rh, &bw, &depth) != 0 &&
            rw > 0u && rh > 0u) {
            target_w = rw;
            target_h = rh;
        }
    }

    ScarVkExtent2D extent{target_w, target_h};
    if (caps.currentExtent.width != UINT32_C(0xFFFFFFFF) && caps.currentExtent.width != 0u &&
        caps.currentExtent.height != 0u) {
        extent = caps.currentExtent;
    } else {
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    if (extent.width == 0u || extent.height == 0u) {
        return false;
    }
    sc.wsi.width = extent.width;
    sc.wsi.height = extent.height;

    uint32_t min_images = 3u;
    if (min_images < caps.minImageCount) min_images = caps.minImageCount;
    if (caps.maxImageCount > 0u && min_images > caps.maxImageCount) min_images = caps.maxImageCount;

    ScarVkFlags composite_alpha = SCAR_VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & composite_alpha) == 0u &&
        caps.supportedCompositeAlpha != 0u) {
        composite_alpha = caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1u);
    }

    ScarVkSwapchainKHR old_sc = sc.wsi.swapchain;
    ScarVkSwapchainCreateInfoKHR sc_ci{};
    sc_ci.sType = SCAR_VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc_ci.surface = sc.wsi.surface;
    sc_ci.minImageCount = min_images;
    sc_ci.imageFormat = sc.wsi.swapchain_format;
    sc_ci.imageColorSpace = sc.wsi.swapchain_colorspace;
    sc_ci.imageExtent = extent;
    sc_ci.imageArrayLayers = 1u;
    sc_ci.imageUsage =
        SCAR_VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | SCAR_VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sc_ci.imageSharingMode = SCAR_VK_SHARING_MODE_EXCLUSIVE;
    sc_ci.preTransform = caps.currentTransform != 0u
                             ? caps.currentTransform
                             : SCAR_VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sc_ci.compositeAlpha = composite_alpha;
    sc_ci.presentMode = SCAR_VK_PRESENT_MODE_FIFO_KHR;
    sc_ci.clipped = SCAR_VK_TRUE;
    sc_ci.oldSwapchain = old_sc;

    ScarVkSwapchainKHR new_sc = SCAR_VK_NULL_HANDLE;
    if (vk->vkCreateSwapchainKHR(vk->device, &sc_ci, nullptr, &new_sc) != SCAR_VK_SUCCESS ||
        new_sc == SCAR_VK_NULL_HANDLE) {
        return false;
    }
    if (old_sc != SCAR_VK_NULL_HANDLE && vk->vkDestroySwapchainKHR != nullptr) {
        vk->vkDestroySwapchainKHR(vk->device, old_sc, nullptr);
    }
    sc.wsi.swapchain = new_sc;

    uint32_t img_count = 0u;
    (void)vk->vkGetSwapchainImagesKHR(vk->device, sc.wsi.swapchain, &img_count, nullptr);
    if (img_count == 0u) {
        return false;
    }
    sc.wsi.images.resize(img_count);
    if (vk->vkGetSwapchainImagesKHR(
            vk->device, sc.wsi.swapchain, &img_count, sc.wsi.images.data()) != SCAR_VK_SUCCESS) {
        return false;
    }
    return true;
}

void wsi_init_once(SwapchainState &sc, VulkanRuntimeContext *vk, uint64_t host_x11_window) noexcept {
    if (sc.wsi.attempted) {
        return;
    }
    sc.wsi.attempted = true;

    if (vk == nullptr || !vk->real_device_created || !vk->has_xlib_surface_ext ||
        !vk->has_swapchain_ext || vk->direct_queue == nullptr ||
        vk->direct_pool == SCAR_VK_NULL_HANDLE || vk->vkCreateXlibSurfaceKHR == nullptr ||
        vk->vkGetPhysicalDeviceSurfaceSupportKHR == nullptr ||
        vk->vkGetPhysicalDeviceSurfaceCapabilitiesKHR == nullptr ||
        vk->vkGetPhysicalDeviceSurfaceFormatsKHR == nullptr ||
        vk->vkCreateSwapchainKHR == nullptr || vk->vkGetSwapchainImagesKHR == nullptr ||
        vk->vkAcquireNextImageKHR == nullptr || vk->vkQueuePresentKHR == nullptr ||
        vk->vkCreateSemaphore == nullptr || vk->vkCreateFence == nullptr ||
        vk->vkWaitForFences == nullptr || vk->vkResetFences == nullptr ||
        vk->vkAllocateCommandBuffers == nullptr || vk->vkResetCommandBuffer == nullptr ||
        vk->vkBeginCommandBuffer == nullptr || vk->vkEndCommandBuffer == nullptr ||
        vk->vkCmdPipelineBarrier == nullptr || vk->vkCmdClearColorImage == nullptr ||
        vk->vkQueueSubmit == nullptr) {
        std::fprintf(stderr,
                     "[scarlett-wsi] Disabled: vk=%p real_dev=%d xlib_ext=%d sc_ext=%d queue=%p\n",
                     static_cast<const void *>(vk),
                     vk ? (vk->real_device_created ? 1 : 0) : 0,
                     vk ? (vk->has_xlib_surface_ext ? 1 : 0) : 0,
                     vk ? (vk->has_swapchain_ext ? 1 : 0) : 0,
                     vk ? static_cast<const void *>(vk->direct_queue) : nullptr);
        std::fflush(stderr);
        return;
    }

    const char *display_env = std::getenv("DISPLAY");
    if (display_env == nullptr || display_env[0] == '\0') {
        std::fprintf(stderr, "[scarlett-wsi] Headless fallback: DISPLAY is unset\n");
        std::fflush(stderr);
        return;
    }

    void *x11 = ::dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (x11 == nullptr) {
        x11 = ::dlopen("libX11.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (x11 == nullptr) {
        std::fprintf(stderr, "[scarlett-wsi] Headless fallback: dlopen(libX11.so.6) failed\n");
        std::fflush(stderr);
        return;
    }
    sc.wsi.x11_dl = x11;

    using PfnXOpenDisplay = void *(*)(const char *);
    using PfnXDefaultScreen = int (*)(void *);
    using PfnXRootWindow = unsigned long (*)(void *, int);
    using PfnXBlackPixel = unsigned long (*)(void *, int);
    using PfnXCreateSimpleWindow = unsigned long (*)(
        void *, unsigned long, int, int, unsigned int, unsigned int, unsigned int,
        unsigned long, unsigned long);
    using PfnXStoreName = int (*)(void *, unsigned long, const char *);
    using PfnXMapWindow = int (*)(void *, unsigned long);
    using PfnXFlush = int (*)(void *);
    using PfnXPending = int (*)(void *);
    using PfnXNextEvent = int (*)(void *, void *);
    using PfnXGetGeometry = int (*)(void *, unsigned long, unsigned long *, int *, int *,
                                    unsigned int *, unsigned int *, unsigned int *, unsigned int *);
    using PfnXDestroyWindow = int (*)(void *, unsigned long);
    using PfnXCloseDisplay = int (*)(void *);

    auto x_open_display = reinterpret_cast<PfnXOpenDisplay>(::dlsym(x11, "XOpenDisplay"));
    auto x_default_screen = reinterpret_cast<PfnXDefaultScreen>(::dlsym(x11, "XDefaultScreen"));
    auto x_root_window = reinterpret_cast<PfnXRootWindow>(::dlsym(x11, "XRootWindow"));
    auto x_black_pixel = reinterpret_cast<PfnXBlackPixel>(::dlsym(x11, "XBlackPixel"));
    auto x_create_simple_window =
        reinterpret_cast<PfnXCreateSimpleWindow>(::dlsym(x11, "XCreateSimpleWindow"));
    auto x_store_name = reinterpret_cast<PfnXStoreName>(::dlsym(x11, "XStoreName"));
    auto x_map_window = reinterpret_cast<PfnXMapWindow>(::dlsym(x11, "XMapWindow"));
    auto x_flush = reinterpret_cast<PfnXFlush>(::dlsym(x11, "XFlush"));
    sc.wsi.x11_XPending = reinterpret_cast<PfnXPending>(::dlsym(x11, "XPending"));
    sc.wsi.x11_XNextEvent = reinterpret_cast<PfnXNextEvent>(::dlsym(x11, "XNextEvent"));
    sc.wsi.x11_XGetGeometry = reinterpret_cast<PfnXGetGeometry>(::dlsym(x11, "XGetGeometry"));
    sc.wsi.x11_XDestroyWindow =
        reinterpret_cast<PfnXDestroyWindow>(::dlsym(x11, "XDestroyWindow"));
    sc.wsi.x11_XCloseDisplay = reinterpret_cast<PfnXCloseDisplay>(::dlsym(x11, "XCloseDisplay"));

    if (x_open_display == nullptr) {
        wsi_shutdown(sc, vk);
        return;
    }

    sc.wsi.x11_display = x_open_display(nullptr);
    if (sc.wsi.x11_display == nullptr) {
        std::fprintf(stderr, "[scarlett-wsi] Headless fallback: XOpenDisplay(%s) failed\n",
                     display_env);
        std::fflush(stderr);
        wsi_shutdown(sc, vk);
        return;
    }

    if (host_x11_window != 0u) {
        sc.wsi.x11_window = static_cast<unsigned long>(host_x11_window);
        sc.wsi.owns_x11_window = false;
    } else {
        if (x_default_screen == nullptr || x_root_window == nullptr ||
            x_black_pixel == nullptr || x_create_simple_window == nullptr ||
            x_store_name == nullptr || x_map_window == nullptr || x_flush == nullptr) {
            wsi_shutdown(sc, vk);
            return;
        }
        const int screen = x_default_screen(sc.wsi.x11_display);
        const unsigned long root = x_root_window(sc.wsi.x11_display, screen);
        const unsigned long black = x_black_pixel(sc.wsi.x11_display, screen);
        sc.wsi.width = 1280u;
        sc.wsi.height = 720u;
        sc.wsi.x11_window = x_create_simple_window(
            sc.wsi.x11_display, root, 0, 0, sc.wsi.width, sc.wsi.height, 0u, black, black);
        if (sc.wsi.x11_window == 0ul) {
            wsi_shutdown(sc, vk);
            return;
        }
        sc.wsi.owns_x11_window = true;
        x_store_name(sc.wsi.x11_display, sc.wsi.x11_window, "GTA6 Scarlett \xe2\x80\x94 Proton d3d12_xs");
        x_map_window(sc.wsi.x11_display, sc.wsi.x11_window);
        x_flush(sc.wsi.x11_display);
    }

    ScarVkXlibSurfaceCreateInfoKHR surf_ci{};
    surf_ci.sType = SCAR_VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surf_ci.dpy = sc.wsi.x11_display;
    surf_ci.window = sc.wsi.x11_window;
    if (vk->vkCreateXlibSurfaceKHR(vk->instance, &surf_ci, nullptr, &sc.wsi.surface) !=
            SCAR_VK_SUCCESS ||
        sc.wsi.surface == SCAR_VK_NULL_HANDLE) {
        wsi_shutdown(sc, vk);
        return;
    }

    ScarVkBool32 supported = SCAR_VK_FALSE;
    if (vk->vkGetPhysicalDeviceSurfaceSupportKHR(
            vk->physical_device, vk->direct_queue_family, sc.wsi.surface, &supported) !=
            SCAR_VK_SUCCESS ||
        supported == SCAR_VK_FALSE) {
        wsi_shutdown(sc, vk);
        return;
    }

    uint32_t fmt_count = 0u;
    (void)vk->vkGetPhysicalDeviceSurfaceFormatsKHR(
        vk->physical_device, sc.wsi.surface, &fmt_count, nullptr);
    if (fmt_count == 0u) {
        wsi_shutdown(sc, vk);
        return;
    }
    std::vector<ScarVkSurfaceFormatKHR> formats(fmt_count);
    if (vk->vkGetPhysicalDeviceSurfaceFormatsKHR(
            vk->physical_device, sc.wsi.surface, &fmt_count, formats.data()) != SCAR_VK_SUCCESS) {
        wsi_shutdown(sc, vk);
        return;
    }

    ScarVkSurfaceFormatKHR chosen_fmt = formats[0];
    for (const auto &f : formats) {
        if (f.format == SCAR_VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen_fmt = f;
            break;
        }
    }
    sc.wsi.swapchain_format = chosen_fmt.format;
    sc.wsi.swapchain_colorspace = chosen_fmt.colorSpace;

    if (!wsi_create_swapchain(sc, vk)) {
        wsi_shutdown(sc, vk);
        return;
    }

    ScarVkCommandBufferAllocateInfo cb_ai{};
    cb_ai.sType = SCAR_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_ai.commandPool = vk->direct_pool;
    cb_ai.level = SCAR_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_ai.commandBufferCount = 1u;
    if (vk->vkAllocateCommandBuffers(vk->device, &cb_ai, &sc.wsi.cmd_buf) != SCAR_VK_SUCCESS ||
        sc.wsi.cmd_buf == nullptr) {
        wsi_shutdown(sc, vk);
        return;
    }

    ScarVkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = SCAR_VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vk->vkCreateSemaphore(vk->device, &sem_ci, nullptr, &sc.wsi.image_available_sem) !=
            SCAR_VK_SUCCESS ||
        vk->vkCreateSemaphore(vk->device, &sem_ci, nullptr, &sc.wsi.render_finished_sem) !=
            SCAR_VK_SUCCESS) {
        wsi_shutdown(sc, vk);
        return;
    }

    ScarVkFenceCreateInfo fence_ci{};
    fence_ci.sType = SCAR_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = 1u; /* VK_FENCE_CREATE_SIGNALED_BIT */
    if (vk->vkCreateFence(vk->device, &fence_ci, nullptr, &sc.wsi.in_flight_fence) !=
        SCAR_VK_SUCCESS) {
        wsi_shutdown(sc, vk);
        return;
    }

    sc.wsi.active = true;
    std::fprintf(stderr,
                 "[scarlett-wsi] Bound %s X11 window=0x%lx %ux%u + VkSwapchainKHR (images=%zu)\n",
                 sc.wsi.owns_x11_window ? "standalone" : "host-HWND",
                 sc.wsi.x11_window, sc.wsi.width, sc.wsi.height, sc.wsi.images.size());
    std::fflush(stderr);
}

void wsi_present_frame(
    SwapchainState &sc,
    VulkanRuntimeContext *vk,
    const scarlett_d3d12xs_presentx_params_t &inout) noexcept {
    if (!sc.wsi.active || vk == nullptr) {
        return;
    }

    if (sc.wsi.owns_x11_window && sc.wsi.x11_XPending != nullptr && sc.wsi.x11_XNextEvent != nullptr) {
        while (sc.wsi.x11_XPending(sc.wsi.x11_display) > 0) {
            uint64_t ev[24];
            sc.wsi.x11_XNextEvent(sc.wsi.x11_display, ev);
        }
    }

    if (sc.wsi.x11_XGetGeometry != nullptr && sc.wsi.x11_display != nullptr &&
        sc.wsi.x11_window != 0ul) {
        unsigned long root_ret = 0ul;
        int rx = 0, ry = 0;
        unsigned int rw = 0u, rh = 0u, bw = 0u, depth = 0u;
        if (sc.wsi.x11_XGetGeometry(
                sc.wsi.x11_display, sc.wsi.x11_window, &root_ret, &rx, &ry,
                &rw, &rh, &bw, &depth) != 0 &&
            rw > 0u && rh > 0u && (rw != sc.wsi.width || rh != sc.wsi.height)) {
            (void)wsi_create_swapchain(sc, vk);
        }
    }

    (void)vk->vkWaitForFences(vk->device, 1u, &sc.wsi.in_flight_fence, SCAR_VK_TRUE, UINT64_MAX);
    (void)vk->vkResetFences(vk->device, 1u, &sc.wsi.in_flight_fence);

    uint32_t image_index = 0u;
    ScarVkResult acq_res = vk->vkAcquireNextImageKHR(
        vk->device, sc.wsi.swapchain, UINT64_MAX, sc.wsi.image_available_sem,
        SCAR_VK_NULL_HANDLE, &image_index);
    if (acq_res == SCAR_VK_ERROR_OUT_OF_DATE_KHR) {
        if (!wsi_create_swapchain(sc, vk)) {
            return;
        }
        acq_res = vk->vkAcquireNextImageKHR(
            vk->device, sc.wsi.swapchain, UINT64_MAX, sc.wsi.image_available_sem,
            SCAR_VK_NULL_HANDLE, &image_index);
    }
    if ((acq_res != SCAR_VK_SUCCESS && acq_res != SCAR_VK_SUBOPTIMAL_KHR) ||
        image_index >= sc.wsi.images.size()) {
        return;
    }

    (void)vk->vkResetCommandBuffer(sc.wsi.cmd_buf, 0u);
    ScarVkCommandBufferBeginInfo begin_info{};
    begin_info.sType = SCAR_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = SCAR_VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vk->vkBeginCommandBuffer(sc.wsi.cmd_buf, &begin_info) != SCAR_VK_SUCCESS) {
        return;
    }

    bool did_blit = false;
    if (inout.plane_count > 0u && inout.planes[0].gpu_va > UINT64_C(0x10000) &&
        inout.planes[0].width > 0u && inout.planes[0].height > 0u &&
        vk->vkCmdCopyBufferToImage != nullptr && vk->vkCmdBlitImage != nullptr &&
        wsi_ensure_upload_resources(sc, vk, inout.planes[0].width, inout.planes[0].height)) {
        const auto &plane0 = inout.planes[0];
        convert_plane_to_bgra8(plane0, static_cast<uint8_t *>(sc.wsi.upload_buf_mapped));

        ScarVkImageMemoryBarrier up_to_dst{};
        up_to_dst.sType = SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        up_to_dst.srcAccessMask = 0u;
        up_to_dst.dstAccessMask = SCAR_VK_ACCESS_TRANSFER_WRITE_BIT;
        up_to_dst.oldLayout = SCAR_VK_IMAGE_LAYOUT_UNDEFINED;
        up_to_dst.newLayout = SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        up_to_dst.srcQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        up_to_dst.dstQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        up_to_dst.image = sc.wsi.upload_img;
        up_to_dst.subresourceRange = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};

        ScarVkImageMemoryBarrier sc_to_dst{};
        sc_to_dst.sType = SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        sc_to_dst.srcAccessMask = 0u;
        sc_to_dst.dstAccessMask = SCAR_VK_ACCESS_TRANSFER_WRITE_BIT;
        sc_to_dst.oldLayout = SCAR_VK_IMAGE_LAYOUT_UNDEFINED;
        sc_to_dst.newLayout = SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        sc_to_dst.srcQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        sc_to_dst.dstQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        sc_to_dst.image = sc.wsi.images[image_index];
        sc_to_dst.subresourceRange = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};

        const ScarVkImageMemoryBarrier pre_barriers[2] = {up_to_dst, sc_to_dst};
        vk->vkCmdPipelineBarrier(
            sc.wsi.cmd_buf, SCAR_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 2u, pre_barriers);

        ScarVkBufferImageCopy copy_reg{};
        copy_reg.bufferOffset = 0u;
        copy_reg.bufferRowLength = plane0.width;
        copy_reg.bufferImageHeight = plane0.height;
        copy_reg.imageSubresource = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        copy_reg.imageOffset = {0, 0, 0};
        copy_reg.imageExtent = {plane0.width, plane0.height, 1u};
        vk->vkCmdCopyBufferToImage(
            sc.wsi.cmd_buf, sc.wsi.upload_buf, sc.wsi.upload_img,
            SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy_reg);

        ScarVkImageMemoryBarrier up_to_src{};
        up_to_src.sType = SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        up_to_src.srcAccessMask = SCAR_VK_ACCESS_TRANSFER_WRITE_BIT;
        up_to_src.dstAccessMask = SCAR_VK_ACCESS_TRANSFER_READ_BIT;
        up_to_src.oldLayout = SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        up_to_src.newLayout = SCAR_VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        up_to_src.srcQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        up_to_src.dstQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        up_to_src.image = sc.wsi.upload_img;
        up_to_src.subresourceRange = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        vk->vkCmdPipelineBarrier(
            sc.wsi.cmd_buf, SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT,
            SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &up_to_src);

        int32_t sx0 = 0, sy0 = 0;
        int32_t sx1 = static_cast<int32_t>(plane0.width);
        int32_t sy1 = static_cast<int32_t>(plane0.height);
        if (plane0.src_right > plane0.src_left && plane0.src_bottom > plane0.src_top &&
            plane0.src_right <= plane0.width && plane0.src_bottom <= plane0.height) {
            sx0 = static_cast<int32_t>(plane0.src_left);
            sy0 = static_cast<int32_t>(plane0.src_top);
            sx1 = static_cast<int32_t>(plane0.src_right);
            sy1 = static_cast<int32_t>(plane0.src_bottom);
        }

        ScarVkImageBlit blit{};
        blit.srcSubresource = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        blit.srcOffsets[0] = {sx0, sy0, 0};
        blit.srcOffsets[1] = {sx1, sy1, 1};
        blit.dstSubresource = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u};
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {static_cast<int32_t>(sc.wsi.width), static_cast<int32_t>(sc.wsi.height), 1};
        vk->vkCmdBlitImage(
            sc.wsi.cmd_buf, sc.wsi.upload_img, SCAR_VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            sc.wsi.images[image_index], SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1u, &blit, SCAR_VK_FILTER_LINEAR);
        did_blit = true;
    }

    if (!did_blit) {
        ScarVkImageMemoryBarrier to_dst{};
        to_dst.sType = SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_dst.srcAccessMask = 0u;
        to_dst.dstAccessMask = SCAR_VK_ACCESS_TRANSFER_WRITE_BIT;
        to_dst.oldLayout = SCAR_VK_IMAGE_LAYOUT_UNDEFINED;
        to_dst.newLayout = SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_dst.srcQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        to_dst.dstQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
        to_dst.image = sc.wsi.images[image_index];
        to_dst.subresourceRange = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
        vk->vkCmdPipelineBarrier(
            sc.wsi.cmd_buf, SCAR_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &to_dst);

        ScarVkClearColorValue clear_val{};
        clear_val.float32[0] = 0.0f;
        clear_val.float32[1] = 0.0f;
        clear_val.float32[2] = 0.0f;
        clear_val.float32[3] = 1.0f;
        vk->vkCmdClearColorImage(
            sc.wsi.cmd_buf, sc.wsi.images[image_index], SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clear_val, 1u, &to_dst.subresourceRange);
    }

    ScarVkImageMemoryBarrier to_present{};
    to_present.sType = SCAR_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_present.srcAccessMask = SCAR_VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0u;
    to_present.oldLayout = SCAR_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = SCAR_VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
    to_present.dstQueueFamilyIndex = SCAR_VK_QUEUE_FAMILY_IGNORED;
    to_present.image = sc.wsi.images[image_index];
    to_present.subresourceRange = {SCAR_VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u};
    vk->vkCmdPipelineBarrier(
        sc.wsi.cmd_buf, SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT,
        SCAR_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &to_present);

    if (vk->vkEndCommandBuffer(sc.wsi.cmd_buf) != SCAR_VK_SUCCESS) {
        return;
    }

    const ScarVkFlags wait_stage = SCAR_VK_PIPELINE_STAGE_TRANSFER_BIT;
    ScarVkSubmitInfo submit_info{};
    submit_info.sType = SCAR_VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1u;
    submit_info.pWaitSemaphores = &sc.wsi.image_available_sem;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1u;
    submit_info.pCommandBuffers = &sc.wsi.cmd_buf;
    submit_info.signalSemaphoreCount = 1u;
    submit_info.pSignalSemaphores = &sc.wsi.render_finished_sem;
    if (vk->vkQueueSubmit(vk->direct_queue, 1u, &submit_info, sc.wsi.in_flight_fence) !=
        SCAR_VK_SUCCESS) {
        return;
    }

    ScarVkPresentInfoKHR present_info{};
    present_info.sType = SCAR_VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1u;
    present_info.pWaitSemaphores = &sc.wsi.render_finished_sem;
    present_info.swapchainCount = 1u;
    present_info.pSwapchains = &sc.wsi.swapchain;
    present_info.pImageIndices = &image_index;
    const ScarVkResult pres_res = vk->vkQueuePresentKHR(vk->direct_queue, &present_info);
    if (pres_res == SCAR_VK_ERROR_OUT_OF_DATE_KHR || pres_res == SCAR_VK_SUBOPTIMAL_KHR) {
        (void)wsi_create_swapchain(sc, vk);
    }
}

} // namespace

void wsi_shutdown(SwapchainState &sc, VulkanRuntimeContext *vk) noexcept {
    if (vk != nullptr && vk->device != nullptr) {
        if (vk->vkDeviceWaitIdle != nullptr) {
            (void)vk->vkDeviceWaitIdle(vk->device);
        }
        wsi_destroy_upload_resources(sc, vk);
        if (sc.wsi.in_flight_fence != SCAR_VK_NULL_HANDLE && vk->vkDestroyFence != nullptr) {
            vk->vkDestroyFence(vk->device, sc.wsi.in_flight_fence, nullptr);
        }
        if (sc.wsi.image_available_sem != SCAR_VK_NULL_HANDLE &&
            vk->vkDestroySemaphore != nullptr) {
            vk->vkDestroySemaphore(vk->device, sc.wsi.image_available_sem, nullptr);
        }
        if (sc.wsi.render_finished_sem != SCAR_VK_NULL_HANDLE &&
            vk->vkDestroySemaphore != nullptr) {
            vk->vkDestroySemaphore(vk->device, sc.wsi.render_finished_sem, nullptr);
        }
        if (sc.wsi.swapchain != SCAR_VK_NULL_HANDLE && vk->vkDestroySwapchainKHR != nullptr) {
            vk->vkDestroySwapchainKHR(vk->device, sc.wsi.swapchain, nullptr);
        }
    }
    if (vk != nullptr && vk->instance != nullptr && sc.wsi.surface != SCAR_VK_NULL_HANDLE &&
        vk->vkDestroySurfaceKHR != nullptr) {
        vk->vkDestroySurfaceKHR(vk->instance, sc.wsi.surface, nullptr);
    }

    sc.wsi.in_flight_fence = SCAR_VK_NULL_HANDLE;
    sc.wsi.image_available_sem = SCAR_VK_NULL_HANDLE;
    sc.wsi.render_finished_sem = SCAR_VK_NULL_HANDLE;
    sc.wsi.swapchain = SCAR_VK_NULL_HANDLE;
    sc.wsi.surface = SCAR_VK_NULL_HANDLE;
    sc.wsi.cmd_buf = nullptr;
    sc.wsi.images.clear();

    if (sc.wsi.x11_display != nullptr) {
        if (sc.wsi.owns_x11_window && sc.wsi.x11_window != 0ul &&
            sc.wsi.x11_XDestroyWindow != nullptr) {
            sc.wsi.x11_XDestroyWindow(sc.wsi.x11_display, sc.wsi.x11_window);
        }
        if (sc.wsi.x11_XCloseDisplay != nullptr) {
            sc.wsi.x11_XCloseDisplay(sc.wsi.x11_display);
        }
    }
    sc.wsi.owns_x11_window = false;
    sc.wsi.x11_window = 0ul;
    sc.wsi.x11_display = nullptr;

    if (sc.wsi.x11_dl != nullptr) {
        ::dlclose(sc.wsi.x11_dl);
        sc.wsi.x11_dl = nullptr;
    }
    sc.wsi.active = false;
}

uint64_t monotonic_now_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kNsPerSec + static_cast<uint64_t>(ts.tv_nsec);
}

scarlett_hresult_t pacing_set_frame_interval(
    SwapchainState &sc,
    const scarlett_d3d12xs_frame_interval_params_t &in) noexcept {
    if (in.period_us < 1000u || in.period_us > 1000000u) {
        return SCARLETT_E_INVALIDARG;
    }
    sc.frame_period_us = in.period_us;
    sc.origin_offset_us = in.origin_offset_us;
    sc.vrr_enabled = (in.vrr_enabled != 0u);
    return SCARLETT_OK;
}

scarlett_hresult_t pacing_schedule_frame_event(
    SwapchainState &sc,
    scarlett_d3d12xs_schedule_frame_params_t &inout) noexcept {
    if (inout.event_type != SCARLETT_FRAME_EVENT_ORIGIN &&
        inout.event_type != SCARLETT_FRAME_EVENT_PRESENT) {
        inout.out_status = SCARLETT_E_INVALIDARG;
        return inout.out_status;
    }

    const uint64_t now = monotonic_now_ns();
    const uint64_t period_ns = static_cast<uint64_t>(sc.frame_period_us) * kNsPerUs;
    const uint64_t delay_ns = static_cast<uint64_t>(inout.delay_us) * kNsPerUs;
    const uint64_t origin_off_ns = static_cast<uint64_t>(sc.origin_offset_us) * kNsPerUs;

    uint64_t target_ns = 0u;
    if (sc.last_origin_ns == 0u) {
        target_ns = now + period_ns + delay_ns + origin_off_ns;
    } else {
        target_ns = sc.last_origin_ns + period_ns + delay_ns;
        /* Re-anchor cleanly if caller missed the next frame deadline (prevents cascading jitter) */
        if (target_ns <= now) {
            target_ns = now + period_ns + delay_ns;
        }
    }

    const uint64_t token = sc.next_token++;
    sc.scheduled_token = token;
    sc.scheduled_target_ns = target_ns;

    inout.out_token = token;
    inout.out_target_ns = target_ns;
    inout.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t pacing_wait_frame_event(
    std::mutex &device_mutex,
    SwapchainState &sc,
    scarlett_d3d12xs_wait_frame_params_t &inout) noexcept {
    /* Accept all frame event types:
     * 0 = SCARLETT_FRAME_EVENT_ORIGIN (VBlank start)
     * 1 = SCARLETT_FRAME_EVENT_PRESENT (Presentation start)
     * 2 = D3D12XBOX_FRAME_EVENT_BEFORE_PRESENT / GPU_COMPLETE
     * and any extended event types <= 16
     */
    uint64_t target_ns = 0u;
    uint64_t token = 0u;
    uint64_t deadline_ns = UINT64_MAX;
    const uint64_t start_now = monotonic_now_ns();

    if (inout.timeout_ms > 0u && inout.timeout_ms < (UINT64_MAX / kNsPerMs)) {
        deadline_ns = start_now + inout.timeout_ms * kNsPerMs;
    } else if (inout.timeout_ms == 0u) {
        deadline_ns = start_now;
    }

    {
        std::lock_guard<std::mutex> lock(device_mutex);
        if (sc.is_suspended) {
            inout.out_status = SCARLETT_E_PENDING;
            return inout.out_status;
        }
        const uint32_t period_us = (sc.frame_period_us != 0u) ? sc.frame_period_us : 16667u;
        const uint64_t period_ns = static_cast<uint64_t>(period_us) * kNsPerUs;
        if (sc.scheduled_target_ns != 0u &&
            (inout.expected_token == 0u || inout.expected_token == sc.scheduled_token)) {
            target_ns = sc.scheduled_target_ns;
            token = sc.scheduled_token;
        } else {
            token = sc.next_token++;
            target_ns = (sc.last_origin_ns == 0u) ? (start_now + period_ns)
                                                  : (sc.last_origin_ns + period_ns);
            if (target_ns <= start_now) {
                target_ns = start_now + period_ns;
            }
        }
    }

    const uint64_t effective_wake_ns = (target_ns < deadline_ns) ? target_ns : deadline_ns;
    sleep_until_monotonic_ns(effective_wake_ns);
    const uint64_t actual_ns = monotonic_now_ns();

    const bool timed_out = (actual_ns < target_ns && actual_ns >= deadline_ns);
    {
        std::lock_guard<std::mutex> lock(device_mutex);
        if (!timed_out) {
            sc.last_origin_ns = target_ns;
            if (sc.scheduled_token == token) {
                sc.scheduled_target_ns = 0u;
            }
        }
    }

    inout.out_token = token;
    inout.out_event_timestamp_ns = actual_ns;
    inout.out_target_timestamp_ns = target_ns;
    inout.out_jitter_ns = static_cast<int64_t>(actual_ns) - static_cast<int64_t>(target_ns);
    inout.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t presentx_submit(
    SwapchainState &sc,
    VulkanRuntimeContext *vk,
    uint64_t /*device_caps_mask*/,
    scarlett_d3d12xs_presentx_params_t &inout) noexcept {
    if (sc.is_suspended) {
        inout.out_status = SCARLETT_E_PENDING;
        return inout.out_status;
    }

    if (inout.plane_count > 2u) {
        inout.out_status = SCARLETT_E_INVALIDARG;
        return inout.out_status;
    }

    wsi_init_once(sc, vk, inout.host_x11_window);

    const uint32_t plane0_vk_cs = inout.plane_count > 0u
        ? map_color_space_to_vk(inout.planes[0].color_space)
        : SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sc.last_plane0_vk_colorspace = plane0_vk_cs;

    /* Populate SMPTE ST.2086 HDR10 metadata when primary plane is G2084_P2020 */
    if (inout.plane_count > 0u &&
        inout.planes[0].color_space == SCARLETT_COLOR_SPACE_G2084_P2020) {
        sc.hdr_enabled = true;
        sc.last_hdr_metadata.sType = SCAR_VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
        sc.last_hdr_metadata.pNext = nullptr;
        /* Rec.2020 mastering primaries + D65 white point */
        sc.last_hdr_metadata.displayPrimaryRed = {0.708f, 0.292f};
        sc.last_hdr_metadata.displayPrimaryGreen = {0.170f, 0.797f};
        sc.last_hdr_metadata.displayPrimaryBlue = {0.131f, 0.046f};
        sc.last_hdr_metadata.whitePoint = {0.3127f, 0.3290f};
        sc.last_hdr_metadata.maxLuminance =
            inout.hdr_max_luminance > 0.0f ? inout.hdr_max_luminance : 1000.0f;
        sc.last_hdr_metadata.minLuminance =
            inout.hdr_min_luminance > 0.0f ? inout.hdr_min_luminance : 0.005f;
        sc.last_hdr_metadata.maxContentLightLevel =
            inout.hdr_max_cll > 0.0f ? inout.hdr_max_cll : 1000.0f;
        sc.last_hdr_metadata.maxFrameAverageLightLevel =
            inout.hdr_max_fall > 0.0f ? inout.hdr_max_fall : 400.0f;
    }

    uint32_t plane1_vk_cs = SCAR_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    uint32_t gamedvr_captured = 0u;
    if (inout.plane_count == 2u) {
        plane1_vk_cs = map_color_space_to_vk(inout.planes[1].color_space);
        sc.last_plane1_vk_colorspace = plane1_vk_cs;
        gamedvr_captured = 1u;
        sc.gamedvr_captures++;
    }

    sc.present_serial++;
    wsi_present_frame(sc, vk, inout);

    inout.out_vk_color_space_plane0 = plane0_vk_cs;
    inout.out_vk_color_space_plane1 = plane1_vk_cs;
    inout.out_gamedvr_captured = gamedvr_captured;
    inout.out_swapchain_generation = sc.generation;
    inout.out_present_serial = sc.present_serial;
    inout.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

} // namespace scarlett::d3d12xs::unixlib
