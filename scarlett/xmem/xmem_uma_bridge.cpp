/*
 * Proton-Scarlett — 3-Tier UMA Memory Bridge (xmem_unix.so)
 *
 * Eliminates 1-frame CPU->GPU latency on discrete & integrated Linux GPUs:
 *   - Tier A: ReBAR / APU zero-copy via VK_KHR_external_memory_fd (DMA-BUF / memfd)
 *   - Tier B: VK_EXT_external_memory_host + hot-resource access tracking & migration
 *   - Tier C: userfaultfd (UFFDIO_WRITEPROTECT) with automatic async-signal-safe
 *             mprotect(PROT_READ) + SIGSEGV write-watch fallback when unprivileged
 *             userfaultfd is restricted by the host kernel.
 *
 * CRITICAL INVARIANT:
 *   Dirty pages are flushed CPU->GPU synchronously BEFORE EVERY ExecuteCommandLists
 *   via scarlett_uma_flush_before_submit() / SCARLETT_XMEM_UNIX_UFFD_FLUSH_DIRTY,
 *   NEVER deferred to PresentX.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xmem_internal.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace scarlett::xmem::unixlib {

namespace {

/* ============================================================================
 * Minimal Self-Contained Vulkan 1.1 ABI Definitions (No vulkan-headers req)
 * ============================================================================ */

constexpr uint32_t kVkSuccess = 0;
constexpr uint32_t kVkStructureTypeApplicationInfo = 0;
constexpr uint32_t kVkStructureTypeInstanceCreateInfo = 1;
constexpr uint32_t kVkPhysicalDeviceTypeIntegratedGpu = 1;
constexpr uint32_t kVkMemoryPropertyDeviceLocalBit = 0x00000001u;
constexpr uint32_t kVkMemoryPropertyHostVisibleBit = 0x00000002u;
constexpr uint64_t kRebarHeapThresholdBytes = UINT64_C(512) * 1024u * 1024u;
constexpr uint64_t kHotMigrationReadThreshold = 16u;

using VkInstanceHandle = void *;
using VkPhysicalDeviceHandle = void *;
using PFN_vkVoidFunction = void (*)(void);
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstanceHandle, const char *);

struct MinimalVkApplicationInfo {
    uint32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct MinimalVkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t flags;
    const MinimalVkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
};

struct MinimalVkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    /* Remaining limits/sparseProperties padding (large enough for Vulkan 1.x) */
    uint8_t rest[1024];
};

struct MinimalVkMemoryType {
    uint32_t propertyFlags;
    uint32_t heapIndex;
};

struct MinimalVkMemoryHeap {
    uint64_t size;
    uint32_t flags;
    uint32_t pad;
};

struct MinimalVkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    MinimalVkMemoryType memoryTypes[32];
    uint32_t memoryHeapCount;
    uint32_t pad;
    MinimalVkMemoryHeap memoryHeaps[16];
};

struct MinimalVkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
};

using PFN_vkCreateInstance = int32_t (*)(const MinimalVkInstanceCreateInfo *,
                                         const void *,
                                         VkInstanceHandle *);
using PFN_vkDestroyInstance = void (*)(VkInstanceHandle, const void *);
using PFN_vkEnumeratePhysicalDevices = int32_t (*)(VkInstanceHandle,
                                                   uint32_t *,
                                                   VkPhysicalDeviceHandle *);
using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDeviceHandle,
                                                   MinimalVkPhysicalDeviceProperties *);
using PFN_vkGetPhysicalDeviceMemoryProperties = void (*)(VkPhysicalDeviceHandle,
                                                         MinimalVkPhysicalDeviceMemoryProperties *);
using PFN_vkEnumerateDeviceExtensionProperties = int32_t (*)(VkPhysicalDeviceHandle,
                                                             const char *,
                                                             uint32_t *,
                                                             MinimalVkExtensionProperties *);

/* ============================================================================
 * Async-Signal-Safe Lock-Free Watch Registry (Shared by UFFD & SIGSEGV)
 * ============================================================================ */

constexpr size_t kMaxWatchSlots = 64;
constexpr size_t kDirtyWordsPerSlot = 512; /* 512 * 64 * 4 KiB = 128 MiB per region */

enum WatchSlotState : uint32_t {
    kSlotFree = 0,
    kSlotActive = 1,
    kSlotDeactivating = 2
};

struct WatchSlot {
    std::atomic<uint32_t> state{kSlotFree};
    uint64_t base_va = 0;
    uint64_t size_bytes = 0;
    uint64_t page_size = 4096;
    uint64_t page_count = 0;
    uint64_t word_count = 0;
    std::atomic<uint64_t> dirty_words[kDirtyWordsPerSlot];
};

WatchSlot g_watch_slots[kMaxWatchSlots];
std::atomic<uint64_t> g_total_write_faults{0};
struct sigaction g_prev_sigsegv_action {};
std::atomic<bool> g_sigsegv_installed{false};

bool mark_fault_page_dirty_signal_safe(uint64_t fault_addr,
                                       uint64_t &out_page_va,
                                       uint64_t &out_page_size) noexcept {
    for (size_t i = 0; i < kMaxWatchSlots; ++i) {
        WatchSlot &slot = g_watch_slots[i];
        if (slot.state.load(std::memory_order_acquire) != kSlotActive) {
            continue;
        }
        const uint64_t base = slot.base_va;
        const uint64_t len = slot.size_bytes;
        if (fault_addr < base || (fault_addr - base) >= len) {
            continue;
        }
        const uint64_t psz = slot.page_size;
        const uint64_t page_idx = (fault_addr - base) / psz;
        const uint64_t word_idx = page_idx >> 6;
        const uint64_t bit_mask = UINT64_C(1) << (page_idx & 63u);
        if (word_idx < slot.word_count) {
            slot.dirty_words[word_idx].fetch_or(bit_mask, std::memory_order_acq_rel);
        }
        g_total_write_faults.fetch_add(1u, std::memory_order_relaxed);
        out_page_va = base + (page_idx * psz);
        out_page_size = psz;
        return true;
    }
    return false;
}

void xmem_uma_sigsegv_handler(int sig, siginfo_t *info, void *ucontext) {
    if (sig == SIGSEGV && info != nullptr) {
        const uint64_t fault_va = reinterpret_cast<uint64_t>(info->si_addr);
        uint64_t page_va = 0;
        uint64_t page_size = 0;
        if (mark_fault_page_dirty_signal_safe(fault_va, page_va, page_size)) {
            /* mprotect is async-signal-safe on Linux (direct syscall) */
            if (::mprotect(reinterpret_cast<void *>(page_va),
                           static_cast<size_t>(page_size),
                           PROT_READ | PROT_WRITE) == 0) {
                return;
            }
        }
    }

    /* Not one of our write-watched pages: chain to previous SIGSEGV handler */
    if ((g_prev_sigsegv_action.sa_flags & SA_SIGINFO) != 0 &&
        g_prev_sigsegv_action.sa_sigaction != nullptr) {
        g_prev_sigsegv_action.sa_sigaction(sig, info, ucontext);
        return;
    }
    if (g_prev_sigsegv_action.sa_handler != SIG_DFL &&
        g_prev_sigsegv_action.sa_handler != SIG_IGN &&
        g_prev_sigsegv_action.sa_handler != nullptr) {
        g_prev_sigsegv_action.sa_handler(sig);
        return;
    }

    struct sigaction dfl {};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    ::sigaction(SIGSEGV, &dfl, nullptr);
    ::raise(SIGSEGV);
}

int open_userfaultfd_kernel(uint32_t &out_errno) noexcept {
    out_errno = 0;
    long fd = ::syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (fd >= 0) {
        return static_cast<int>(fd);
    }
    const int first_err = errno;

    /* Newer kernels expose /dev/userfaultfd */
    int dev_fd = ::open("/dev/userfaultfd", O_RDWR | O_CLOEXEC);
    if (dev_fd >= 0) {
        int uffd = ::ioctl(dev_fd, USERFAULTFD_IOC_NEW, O_CLOEXEC | O_NONBLOCK);
        const int ioctl_err = errno;
        ::close(dev_fd);
        if (uffd >= 0) {
            return uffd;
        }
        out_errno = static_cast<uint32_t>(ioctl_err);
        return -1;
    }

    out_errno = static_cast<uint32_t>(first_err);
    return -1;
}

scarlett_uma_tier_t parse_env_uma_tier() noexcept {
    const char *env = std::getenv("PROTON_XBOX_UMA_TIER");
    if (env == nullptr || env[0] == '\0') {
        return SCARLETT_UMA_TIER_AUTO;
    }
    if (strcasecmp(env, "tier_a") == 0 || strcasecmp(env, "a") == 0) {
        return SCARLETT_UMA_TIER_A_REBAR_DMABUF;
    }
    if (strcasecmp(env, "tier_b") == 0 || strcasecmp(env, "b") == 0) {
        return SCARLETT_UMA_TIER_B_EXT_HOST;
    }
    if (strcasecmp(env, "tier_c") == 0 || strcasecmp(env, "c") == 0) {
        return SCARLETT_UMA_TIER_C_UFFD_WP;
    }
    return SCARLETT_UMA_TIER_AUTO;
}

bool ranges_overlap_u64(uint64_t a_start, uint64_t a_len,
                        uint64_t b_start, uint64_t b_len) noexcept {
    if (a_len == 0u || b_len == 0u) {
        return false;
    }
    return a_start < (b_start + b_len) && b_start < (a_start + a_len);
}

} // namespace

UmaBridge &UmaBridge::instance() {
    static UmaBridge bridge;
    return bridge;
}

UmaBridge::UmaBridge() {
    long psz = ::sysconf(_SC_PAGESIZE);
    os_page_size_ = (psz > 0) ? static_cast<uint64_t>(psz) : UINT64_C(4096);
    forced_tier_ = parse_env_uma_tier();
    initialize_from_host();
}

UmaBridge::~UmaBridge() {
    reset_all();
    stop_uffd_thread();
    if (uffd_fd_ >= 0) {
        ::close(uffd_fd_);
        uffd_fd_ = -1;
    }
    if (uffd_wake_efd_ >= 0) {
        ::close(uffd_wake_efd_);
        uffd_wake_efd_ = -1;
    }
}

void UmaBridge::initialize_from_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    probe_host_vulkan_and_kernel_locked();
    active_caps_ = detected_caps_;
}

void UmaBridge::probe_host_vulkan_and_kernel_locked() {
    detected_caps_ = XMEM_UMA_CAP_NONE;
    rebar_heap_bytes_ = 0;
    min_host_ptr_alignment_ = os_page_size_;

    /* 1. Probe memfd_create capability */
    int test_mfd = ::memfd_create("scarlett_uma_probe", MFD_CLOEXEC);
    if (test_mfd >= 0) {
        detected_caps_ |= XMEM_UMA_CAP_MEMFD_EXPORT;
        ::close(test_mfd);
    }

    /* 2. Probe /dev/dma_heap/system capability */
    int heap_fd = ::open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
    if (heap_fd >= 0) {
        detected_caps_ |= XMEM_UMA_CAP_DMA_HEAP;
        ::close(heap_fd);
    }

    /* 3. Probe userfaultfd UFFDIO_WRITEPROTECT on shmem/memfd */
    if (uffd_fd_ >= 0) {
        detected_caps_ |= XMEM_UMA_CAP_UFFD_WP;
        host_watch_mode_ = XMEM_UMA_WATCH_UFFD_WP;
        uffd_errno_ = 0;
    } else {
        uint32_t probe_err = 0;
        int probe_fd = open_userfaultfd_kernel(probe_err);
        if (probe_fd >= 0) {
            struct uffdio_api api {};
            api.api = UFFD_API;
            api.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP |
                           UFFD_FEATURE_WP_HUGETLBFS_SHMEM;
            if (::ioctl(probe_fd, UFFDIO_API, &api) == 0 &&
                (api.features & UFFD_FEATURE_PAGEFAULT_FLAG_WP) != 0u &&
                (api.features & UFFD_FEATURE_WP_HUGETLBFS_SHMEM) != 0u) {
                uffd_fd_ = probe_fd;
                detected_caps_ |= XMEM_UMA_CAP_UFFD_WP;
                host_watch_mode_ = XMEM_UMA_WATCH_UFFD_WP;
                uffd_errno_ = 0;
            } else {
                uffd_errno_ = (errno != 0) ? static_cast<uint32_t>(errno)
                                           : static_cast<uint32_t>(EOPNOTSUPP);
                ::close(probe_fd);
                detected_caps_ |= XMEM_UMA_CAP_MPROTECT_FALLBACK;
                host_watch_mode_ = XMEM_UMA_WATCH_MPROTECT_SIGSEGV;
            }
        } else {
            uffd_errno_ = probe_err;
            detected_caps_ |= XMEM_UMA_CAP_MPROTECT_FALLBACK;
            host_watch_mode_ = XMEM_UMA_WATCH_MPROTECT_SIGSEGV;
        }
    }

    /* 4. Runtime probe libvulkan.so.1 for ReBAR/APU & external memory extensions */
    void *vk_lib = ::dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (vk_lib != nullptr) {
        auto get_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            ::dlsym(vk_lib, "vkGetInstanceProcAddr"));
        if (get_proc != nullptr) {
            auto create_inst = reinterpret_cast<PFN_vkCreateInstance>(
                get_proc(nullptr, "vkCreateInstance"));
            if (create_inst != nullptr) {
                MinimalVkApplicationInfo app_info{};
                app_info.sType = kVkStructureTypeApplicationInfo;
                app_info.pApplicationName = "ProtonScarlettXMemProbe";
                app_info.pEngineName = "ProtonScarlett";
                app_info.apiVersion = (1u << 22) | (1u << 12); /* VK_API_VERSION_1_1 */

                MinimalVkInstanceCreateInfo inst_info{};
                inst_info.sType = kVkStructureTypeInstanceCreateInfo;
                inst_info.pApplicationInfo = &app_info;

                VkInstanceHandle inst = nullptr;
                if (create_inst(&inst_info, nullptr, &inst) == static_cast<int32_t>(kVkSuccess) &&
                    inst != nullptr) {
                    auto destroy_inst = reinterpret_cast<PFN_vkDestroyInstance>(
                        get_proc(inst, "vkDestroyInstance"));
                    auto enum_devs = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
                        get_proc(inst, "vkEnumeratePhysicalDevices"));
                    auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
                        get_proc(inst, "vkGetPhysicalDeviceProperties"));
                    auto get_mem = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
                        get_proc(inst, "vkGetPhysicalDeviceMemoryProperties"));
                    auto enum_exts = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                        get_proc(inst, "vkEnumerateDeviceExtensionProperties"));

                    uint32_t dev_count = 0;
                    if (enum_devs && enum_devs(inst, &dev_count, nullptr) == 0 && dev_count > 0u) {
                        std::vector<VkPhysicalDeviceHandle> devs(dev_count);
                        if (enum_devs(inst, &dev_count, devs.data()) == 0) {
                            for (uint32_t d = 0; d < dev_count; ++d) {
                                if (get_props) {
                                    MinimalVkPhysicalDeviceProperties props{};
                                    get_props(devs[d], &props);
                                    if (props.deviceType == kVkPhysicalDeviceTypeIntegratedGpu) {
                                        detected_caps_ |= XMEM_UMA_CAP_APU;
                                    }
                                }
                                if (get_mem) {
                                    MinimalVkPhysicalDeviceMemoryProperties mem{};
                                    get_mem(devs[d], &mem);
                                    for (uint32_t t = 0; t < mem.memoryTypeCount && t < 32u; ++t) {
                                        const uint32_t flags = mem.memoryTypes[t].propertyFlags;
                                        const uint32_t hidx = mem.memoryTypes[t].heapIndex;
                                        if ((flags & (kVkMemoryPropertyDeviceLocalBit |
                                                      kVkMemoryPropertyHostVisibleBit)) ==
                                                (kVkMemoryPropertyDeviceLocalBit |
                                                 kVkMemoryPropertyHostVisibleBit) &&
                                            hidx < mem.memoryHeapCount && hidx < 16u) {
                                            const uint64_t hsz = mem.memoryHeaps[hidx].size;
                                            if (hsz > rebar_heap_bytes_) {
                                                rebar_heap_bytes_ = hsz;
                                            }
                                            if (hsz >= kRebarHeapThresholdBytes) {
                                                detected_caps_ |= XMEM_UMA_CAP_REBAR;
                                            }
                                        }
                                    }
                                }
                                if (enum_exts) {
                                    uint32_t ext_count = 0;
                                    if (enum_exts(devs[d], nullptr, &ext_count, nullptr) == 0 &&
                                        ext_count > 0u) {
                                        std::vector<MinimalVkExtensionProperties> exts(ext_count);
                                        if (enum_exts(devs[d], nullptr, &ext_count, exts.data()) == 0) {
                                            for (const auto &e : exts) {
                                                if (std::strcmp(e.extensionName,
                                                                "VK_KHR_external_memory_fd") == 0) {
                                                    detected_caps_ |= XMEM_UMA_CAP_VK_EXT_MEMORY_FD;
                                                } else if (std::strcmp(e.extensionName,
                                                                       "VK_EXT_external_memory_dma_buf") == 0) {
                                                    detected_caps_ |= XMEM_UMA_CAP_VK_EXT_DMABUF;
                                                } else if (std::strcmp(e.extensionName,
                                                                       "VK_EXT_external_memory_host") == 0) {
                                                    detected_caps_ |= XMEM_UMA_CAP_VK_EXT_HOST_MEM;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (destroy_inst) {
                        destroy_inst(inst, nullptr);
                    }
                }
            }
        }
        ::dlclose(vk_lib);
    }

    /* Always keep mprotect fallback capability flagged as baseline universal safety net */
    detected_caps_ |= XMEM_UMA_CAP_MPROTECT_FALLBACK;
}

scarlett_uma_tier_t
UmaBridge::select_tier_locked(scarlett_uma_tier_t preferred) const noexcept {
    if (preferred != SCARLETT_UMA_TIER_AUTO) {
        return preferred;
    }
    if (forced_tier_ != SCARLETT_UMA_TIER_AUTO) {
        return forced_tier_;
    }

    const bool has_tier_a =
        ((active_caps_ & (XMEM_UMA_CAP_REBAR | XMEM_UMA_CAP_APU)) != 0u) &&
        ((active_caps_ & XMEM_UMA_CAP_VK_EXT_MEMORY_FD) != 0u) &&
        ((active_caps_ & (XMEM_UMA_CAP_DMA_HEAP | XMEM_UMA_CAP_MEMFD_EXPORT |
                          XMEM_UMA_CAP_VK_EXT_DMABUF)) != 0u);
    if (has_tier_a) {
        return SCARLETT_UMA_TIER_A_REBAR_DMABUF;
    }

    const bool has_tier_b = (active_caps_ & XMEM_UMA_CAP_VK_EXT_HOST_MEM) != 0u;
    if (has_tier_b) {
        return SCARLETT_UMA_TIER_B_EXT_HOST;
    }

    return SCARLETT_UMA_TIER_C_UFFD_WP;
}

scarlett_uma_tier_t UmaBridge::active_tier() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return select_tier_locked(SCARLETT_UMA_TIER_AUTO);
}

void UmaBridge::get_flush_stats(uint64_t &out_flushes, uint64_t &out_pages) const noexcept {
    out_flushes = total_submit_flushes_.load(std::memory_order_acquire);
    out_pages = total_dirty_pages_flushed_.load(std::memory_order_acquire);
}

void UmaBridge::ensure_sigsegv_handler_installed() {
    bool expected = false;
    if (g_sigsegv_installed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        struct sigaction act {};
        act.sa_sigaction = xmem_uma_sigsegv_handler;
        act.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&act.sa_mask);
        ::sigaction(SIGSEGV, &act, &g_prev_sigsegv_action);
    }
}

bool UmaBridge::ensure_uffd_thread_locked() {
    if (uffd_fd_ < 0) {
        return false;
    }
    if (uffd_thread_running_) {
        return true;
    }
    if (uffd_wake_efd_ < 0) {
        uffd_wake_efd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (uffd_wake_efd_ < 0) {
            return false;
        }
    }
    uffd_stop_flag_.store(false, std::memory_order_release);
    auto entry = [](void *arg) -> void * {
        static_cast<UmaBridge *>(arg)->uffd_worker_loop();
        return nullptr;
    };
    if (::pthread_create(&uffd_thread_, nullptr, entry, this) != 0) {
        return false;
    }
    uffd_thread_running_ = true;
    return true;
}

void UmaBridge::stop_uffd_thread() {
    bool should_join = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (uffd_thread_running_) {
            uffd_stop_flag_.store(true, std::memory_order_release);
            if (uffd_wake_efd_ >= 0) {
                uint64_t one = 1;
                ssize_t ignored = ::write(uffd_wake_efd_, &one, sizeof(one));
                (void)ignored;
            }
            should_join = true;
            uffd_thread_running_ = false;
        }
    }
    if (should_join) {
        ::pthread_join(uffd_thread_, nullptr);
    }
}

void UmaBridge::uffd_worker_loop() {
    struct pollfd pfds[2] {};
    pfds[0].fd = uffd_fd_;
    pfds[0].events = POLLIN;
    pfds[1].fd = uffd_wake_efd_;
    pfds[1].events = POLLIN;

    while (!uffd_stop_flag_.load(std::memory_order_acquire)) {
        int rc = ::poll(pfds, 2, 100);
        if (rc <= 0) {
            continue;
        }
        if ((pfds[1].revents & POLLIN) != 0) {
            uint64_t val = 0;
            ssize_t ignored = ::read(uffd_wake_efd_, &val, sizeof(val));
            (void)ignored;
            if (uffd_stop_flag_.load(std::memory_order_acquire)) {
                break;
            }
        }
        if ((pfds[0].revents & POLLIN) != 0) {
            struct uffd_msg msg {};
            while (::read(uffd_fd_, &msg, sizeof(msg)) == static_cast<ssize_t>(sizeof(msg))) {
                if (msg.event == UFFD_EVENT_PAGEFAULT &&
                    (msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) != 0u) {
                    uint64_t page_va = 0;
                    uint64_t page_size = 0;
                    if (mark_fault_page_dirty_signal_safe(
                            msg.arg.pagefault.address, page_va, page_size)) {
                        struct uffdio_writeprotect wp {};
                        wp.range.start = page_va;
                        wp.range.len = page_size;
                        wp.mode = 0; /* Clear WP and wake faulting thread */
                        (void)::ioctl(uffd_fd_, UFFDIO_WRITEPROTECT, &wp);
                    }
                }
            }
        }
    }
}

scarlett_hresult_t UmaBridge::arm_tier_c_watch_locked(ExportedRegion &reg) {
    const uint64_t page_count = (reg.size_bytes + os_page_size_ - 1u) / os_page_size_;
    const uint64_t word_count = (page_count + 63u) / 64u;
    if (word_count > kDirtyWordsPerSlot) {
        return SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED;
    }

    int slot_idx = -1;
    for (size_t i = 0; i < kMaxWatchSlots; ++i) {
        if (g_watch_slots[i].state.load(std::memory_order_acquire) == kSlotFree) {
            slot_idx = static_cast<int>(i);
            break;
        }
    }
    if (slot_idx < 0) {
        return SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED;
    }

    WatchSlot &slot = g_watch_slots[slot_idx];
    slot.base_va = reg.base_va;
    slot.size_bytes = reg.size_bytes;
    slot.page_size = os_page_size_;
    slot.page_count = page_count;
    slot.word_count = word_count;
    for (uint64_t w = 0; w < word_count; ++w) {
        slot.dirty_words[w].store(0ULL, std::memory_order_relaxed);
    }

    /* Snapshot initial CPU contents into GPU shadow before arming WP */
    reg.gpu_shadow.resize(static_cast<size_t>(reg.size_bytes));
    std::memcpy(reg.gpu_shadow.data(),
                reinterpret_cast<const void *>(reg.base_va),
                static_cast<size_t>(reg.size_bytes));

    slot.state.store(kSlotActive, std::memory_order_release);
    reg.watch_slot_index = slot_idx;

    /* Path 1: Kernel userfaultfd UFFDIO_WRITEPROTECT */
    if (uffd_fd_ >= 0 && (active_caps_ & XMEM_UMA_CAP_UFFD_WP) != 0u &&
        ensure_uffd_thread_locked()) {
        struct uffdio_register reg_req {};
        reg_req.range.start = reg.base_va;
        reg_req.range.len = reg.size_bytes;
        reg_req.mode = UFFDIO_REGISTER_MODE_WP;
        if (::ioctl(uffd_fd_, UFFDIO_REGISTER, &reg_req) == 0) {
            struct uffdio_writeprotect wp {};
            wp.range.start = reg.base_va;
            wp.range.len = reg.size_bytes;
            wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
            if (::ioctl(uffd_fd_, UFFDIO_WRITEPROTECT, &wp) == 0) {
                reg.watch_mode = XMEM_UMA_WATCH_UFFD_WP;
                reg.uffd_registered = true;
                return SCARLETT_OK;
            }
            struct uffdio_range unreg { reg.base_va, reg.size_bytes };
            (void)::ioctl(uffd_fd_, UFFDIO_UNREGISTER, &unreg);
        }
    }

    /* Path 2: Automatic mprotect(PROT_READ) + async-signal-safe SIGSEGV fallback */
    ensure_sigsegv_handler_installed();
    if (::mprotect(reinterpret_cast<void *>(reg.base_va),
                   static_cast<size_t>(reg.size_bytes),
                   PROT_READ) != 0) {
        slot.state.store(kSlotFree, std::memory_order_release);
        reg.watch_slot_index = -1;
        return SCARLETT_ERR_XMEM_UFFD_FAULT;
    }

    reg.watch_mode = XMEM_UMA_WATCH_MPROTECT_SIGSEGV;
    reg.uffd_registered = false;
    return SCARLETT_OK;
}

void UmaBridge::disarm_tier_c_watch_locked(ExportedRegion &reg) {
    if (reg.watch_slot_index >= 0 &&
        static_cast<size_t>(reg.watch_slot_index) < kMaxWatchSlots) {
        WatchSlot &slot = g_watch_slots[reg.watch_slot_index];
        slot.state.store(kSlotDeactivating, std::memory_order_release);

        if (reg.uffd_registered && uffd_fd_ >= 0) {
            struct uffdio_writeprotect wp {};
            wp.range.start = reg.base_va;
            wp.range.len = reg.size_bytes;
            wp.mode = 0;
            (void)::ioctl(uffd_fd_, UFFDIO_WRITEPROTECT, &wp);

            struct uffdio_range unreg { reg.base_va, reg.size_bytes };
            (void)::ioctl(uffd_fd_, UFFDIO_UNREGISTER, &unreg);
            reg.uffd_registered = false;
        } else if (reg.watch_mode == XMEM_UMA_WATCH_MPROTECT_SIGSEGV) {
            (void)::mprotect(reinterpret_cast<void *>(reg.base_va),
                             static_cast<size_t>(reg.size_bytes),
                             PROT_READ | PROT_WRITE);
        }

        for (uint64_t w = 0; w < slot.word_count; ++w) {
            slot.dirty_words[w].store(0ULL, std::memory_order_relaxed);
        }
        slot.base_va = 0;
        slot.size_bytes = 0;
        slot.state.store(kSlotFree, std::memory_order_release);
        reg.watch_slot_index = -1;
    }
    reg.watch_mode = XMEM_UMA_WATCH_NONE;
}

void UmaBridge::unregister_range_locked(uint64_t va, uint64_t size_bytes) {
    for (auto it = exported_regions_.begin(); it != exported_regions_.end(); ) {
        if (ranges_overlap_u64(va, size_bytes, it->second.base_va, it->second.size_bytes)) {
            disarm_tier_c_watch_locked(it->second);
            it = exported_regions_.erase(it);
        } else {
            ++it;
        }
    }
}

void UmaBridge::unregister_range(uint64_t va, uint64_t size_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    unregister_range_locked(va, size_bytes);
}

void UmaBridge::reset_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &kv : exported_regions_) {
        disarm_tier_c_watch_locked(kv.second);
    }
    exported_regions_.clear();
    total_submit_flushes_.store(0u, std::memory_order_release);
    total_dirty_pages_flushed_.store(0u, std::memory_order_release);
    total_present_flushes_.store(0u, std::memory_order_release);
    total_migrated_hot_regions_.store(0u, std::memory_order_release);
    g_total_write_faults.store(0u, std::memory_order_release);
    forced_tier_ = parse_env_uma_tier();
    active_caps_ = detected_caps_;
}

scarlett_hresult_t UmaBridge::probe(scarlett_xmem_uma_probe_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    switch (static_cast<scarlett_xmem_uma_probe_action_t>(params.action)) {
        case XMEM_UMA_PROBE_QUERY:
            break;
        case XMEM_UMA_PROBE_SET_SIM_CAPS:
            active_caps_ = params.cap_flags;
            forced_tier_ = SCARLETT_UMA_TIER_AUTO;
            break;
        case XMEM_UMA_PROBE_FORCE_TIER:
            if (params.forced_tier > SCARLETT_UMA_TIER_C_UFFD_WP) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            forced_tier_ = static_cast<scarlett_uma_tier_t>(params.forced_tier);
            break;
        case XMEM_UMA_PROBE_RESET_HOST:
            forced_tier_ = parse_env_uma_tier();
            probe_host_vulkan_and_kernel_locked();
            active_caps_ = detected_caps_;
            break;
        default:
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
    }

    uint32_t watched_count = 0;
    for (const auto &kv : exported_regions_) {
        if (kv.second.watch_mode != XMEM_UMA_WATCH_NONE) {
            watched_count++;
        }
    }

    const scarlett_uma_tier_t sel = select_tier_locked(SCARLETT_UMA_TIER_AUTO);
    scarlett_xmem_uma_watch_mode_t effective_watch = host_watch_mode_;
    if ((active_caps_ & XMEM_UMA_CAP_UFFD_WP) != 0u && uffd_fd_ >= 0) {
        effective_watch = XMEM_UMA_WATCH_UFFD_WP;
    } else {
        effective_watch = XMEM_UMA_WATCH_MPROTECT_SIGSEGV;
    }

    params.out_selected_tier = static_cast<uint32_t>(sel);
    params.out_watch_mode = static_cast<uint32_t>(effective_watch);
    params.out_detected_caps = active_caps_;
    params.out_uffd_errno = uffd_errno_;
    params.out_watched_region_count = watched_count;
    params.out_min_host_ptr_alignment = min_host_ptr_alignment_;
    params.out_rebar_heap_bytes = rebar_heap_bytes_;
    params.out_total_write_faults = g_total_write_faults.load(std::memory_order_acquire);
    params.out_total_submit_flushes = total_submit_flushes_.load(std::memory_order_acquire);
    params.out_total_present_flushes = total_present_flushes_.load(std::memory_order_acquire);
    params.out_total_migrated_hot_regions =
        total_migrated_hot_regions_.load(std::memory_order_acquire);
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t UmaBridge::export_region(scarlett_xmem_uma_export_params_t &params) {
    params.out_active_tier = SCARLETT_UMA_TIER_AUTO;
    params.out_dmabuf_fd = -1;
    params.out_host_ptr = 0;

    if (params.virtual_addr == 0u || params.size_bytes == 0u) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }
    if (!scarlett_addr_is_graphics(params.virtual_addr, params.size_bytes)) {
        params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return params.out_status;
    }
    if ((params.virtual_addr % os_page_size_) != 0u ||
        (params.size_bytes % os_page_size_) != 0u) {
        params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
        return params.out_status;
    }

    /* Inspect MemoryManager BEFORE acquiring UmaBridge::mutex_ (strict lock order) */
    BackingRangeInfo backing{};
    scarlett_hresult_t hr = MemoryManager::instance().inspect_mapped_range(
        params.virtual_addr, params.size_bytes, backing);
    if (scarlett_failed(hr)) {
        params.out_status = hr;
        return hr;
    }
    if (backing.region != SCARLETT_MEM_REGION_GRAPHICS) {
        params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return params.out_status;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    /* Cleanly replace any prior export overlapping this range */
    unregister_range_locked(params.virtual_addr, params.size_bytes);

    const scarlett_uma_tier_t tier =
        select_tier_locked(static_cast<scarlett_uma_tier_t>(params.preferred_tier));

    ExportedRegion reg{};
    reg.base_va = params.virtual_addr;
    reg.size_bytes = params.size_bytes;
    reg.tier = tier;

    if (tier == SCARLETT_UMA_TIER_A_REBAR_DMABUF) {
        if (backing.backing_fd < 0) {
            params.out_status = SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED;
            return params.out_status;
        }
        int dup_fd = ::fcntl(backing.backing_fd, F_DUPFD_CLOEXEC, 0);
        if (dup_fd < 0) {
            params.out_status = SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED;
            return params.out_status;
        }
        params.out_dmabuf_fd = dup_fd;
        params.out_host_ptr = params.virtual_addr;
        params.out_active_tier = SCARLETT_UMA_TIER_A_REBAR_DMABUF;
        exported_regions_.emplace(reg.base_va, std::move(reg));
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (tier == SCARLETT_UMA_TIER_B_EXT_HOST) {
        if ((params.virtual_addr % min_host_ptr_alignment_) != 0u) {
            params.out_status = SCARLETT_ERR_XMEM_INVALID_ALIGNMENT;
            return params.out_status;
        }
        params.out_dmabuf_fd = -1;
        params.out_host_ptr = params.virtual_addr;
        params.out_active_tier = SCARLETT_UMA_TIER_B_EXT_HOST;
        exported_regions_.emplace(reg.base_va, std::move(reg));
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    /* Tier C: userfaultfd UFFDIO_WRITEPROTECT or automatic mprotect+SIGSEGV fallback */
    hr = arm_tier_c_watch_locked(reg);
    if (scarlett_failed(hr)) {
        params.out_status = hr;
        return hr;
    }

    params.out_dmabuf_fd = -1;
    params.out_host_ptr = params.virtual_addr;
    params.out_active_tier = SCARLETT_UMA_TIER_C_UFFD_WP;
    exported_regions_.emplace(reg.base_va, std::move(reg));
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
UmaBridge::flush_before_submit(scarlett_xmem_flush_dirty_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t total_pages = 0;
    uint64_t total_bytes = 0;

    for (auto &kv : exported_regions_) {
        ExportedRegion &reg = kv.second;
        if (params.region_addr != 0u) {
            const uint64_t filter_len = (params.region_size != 0u) ? params.region_size : 1u;
            if (!ranges_overlap_u64(params.region_addr, filter_len,
                                    reg.base_va, reg.size_bytes)) {
                continue;
            }
        }

        /* Tier B migrated hot resource synchronization if CPU touched it */
        if (reg.tier == SCARLETT_UMA_TIER_B_EXT_HOST && reg.is_migrated &&
            !reg.gpu_shadow.empty()) {
            if (std::memcmp(reg.gpu_shadow.data(),
                            reinterpret_cast<const void *>(reg.base_va),
                            static_cast<size_t>(reg.size_bytes)) != 0) {
                std::memcpy(reg.gpu_shadow.data(),
                            reinterpret_cast<const void *>(reg.base_va),
                            static_cast<size_t>(reg.size_bytes));
                if (gpu_upload_cb_ != nullptr) {
                    gpu_upload_cb_(params.command_list_id, reg.base_va, 0,
                                   reinterpret_cast<const void *>(reg.base_va),
                                   reg.size_bytes, gpu_upload_user_data_);
                }
                const uint64_t pages = reg.size_bytes / os_page_size_;
                total_pages += pages;
                total_bytes += reg.size_bytes;
            }
            continue;
        }

        /* Tier C write-watch dirty page flush + immediate WP re-arm */
        if (reg.tier != SCARLETT_UMA_TIER_C_UFFD_WP || reg.watch_slot_index < 0) {
            continue;
        }

        WatchSlot &slot = g_watch_slots[reg.watch_slot_index];
        uint64_t dirty_pages_in_region = 0;

        /* Collect dirty page runs while atomically clearing dirty bits BEFORE re-arming WP */
        uint64_t run_start_page = UINT64_MAX;
        auto flush_dirty_run = [&](uint64_t start_page, uint64_t end_page_excl) -> scarlett_hresult_t {
            const uint64_t run_pages = end_page_excl - start_page;
            const uint64_t run_off = start_page * slot.page_size;
            const uint64_t run_len = run_pages * slot.page_size;
            const uint64_t run_va = reg.base_va + run_off;

            /* 1. Re-arm write protection FIRST (issues TLB shootdown across CPUs) */
            if (reg.watch_mode == XMEM_UMA_WATCH_UFFD_WP && uffd_fd_ >= 0) {
                struct uffdio_writeprotect wp {};
                wp.range.start = run_va;
                wp.range.len = run_len;
                wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
                if (::ioctl(uffd_fd_, UFFDIO_WRITEPROTECT, &wp) != 0) {
                    return SCARLETT_ERR_XMEM_UFFD_FAULT;
                }
            } else if (reg.watch_mode == XMEM_UMA_WATCH_MPROTECT_SIGSEGV) {
                if (::mprotect(reinterpret_cast<void *>(run_va),
                               static_cast<size_t>(run_len),
                               PROT_READ) != 0) {
                    return SCARLETT_ERR_XMEM_UFFD_FAULT;
                }
            }

            /* 2. Copy updated CPU bytes into GPU shadow & notify ExecuteCommandLists sink */
            std::memcpy(reg.gpu_shadow.data() + run_off,
                        reinterpret_cast<const void *>(run_va),
                        static_cast<size_t>(run_len));
            if (gpu_upload_cb_ != nullptr) {
                gpu_upload_cb_(params.command_list_id, reg.base_va, run_off,
                               reinterpret_cast<const void *>(run_va),
                               run_len, gpu_upload_user_data_);
            }

            dirty_pages_in_region += run_pages;
            total_pages += run_pages;
            total_bytes += run_len;
            return SCARLETT_OK;
        };

        for (uint64_t w = 0; w < slot.word_count; ++w) {
            uint64_t bits = slot.dirty_words[w].exchange(0ULL, std::memory_order_acq_rel);
            if (bits == 0ULL) {
                if (run_start_page != UINT64_MAX) {
                    scarlett_hresult_t hr = flush_dirty_run(run_start_page, w * 64u);
                    if (scarlett_failed(hr)) {
                        params.out_status = hr;
                        return hr;
                    }
                    run_start_page = UINT64_MAX;
                }
                continue;
            }

            for (uint64_t b = 0; b < 64u; ++b) {
                const uint64_t p = (w * 64u) + b;
                if (p >= slot.page_count) {
                    break;
                }
                const bool is_dirty = ((bits >> b) & 1ULL) != 0ULL;
                if (is_dirty) {
                    if (run_start_page == UINT64_MAX) {
                        run_start_page = p;
                    }
                } else if (run_start_page != UINT64_MAX) {
                    scarlett_hresult_t hr = flush_dirty_run(run_start_page, p);
                    if (scarlett_failed(hr)) {
                        params.out_status = hr;
                        return hr;
                    }
                    run_start_page = UINT64_MAX;
                }
            }
        }

        if (run_start_page != UINT64_MAX) {
            scarlett_hresult_t hr = flush_dirty_run(run_start_page, slot.page_count);
            if (scarlett_failed(hr)) {
                params.out_status = hr;
                return hr;
            }
        }

        (void)dirty_pages_in_region;
    }

    total_submit_flushes_.fetch_add(1u, std::memory_order_acq_rel);
    total_dirty_pages_flushed_.fetch_add(total_pages, std::memory_order_acq_rel);

    params.out_flushed_pages = total_pages;
    params.out_flushed_bytes = total_bytes;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
UmaBridge::track_hot_resource(scarlett_xmem_uma_hot_params_t &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = exported_regions_.find(params.virtual_addr);
    if (it == exported_regions_.end()) {
        for (auto scan = exported_regions_.begin(); scan != exported_regions_.end(); ++scan) {
            if (params.virtual_addr >= scan->second.base_va &&
                params.virtual_addr < scan->second.base_va + scan->second.size_bytes) {
                it = scan;
                break;
            }
        }
    }
    if (it == exported_regions_.end()) {
        params.out_status = SCARLETT_ERR_XMEM_OUT_OF_RANGE;
        return params.out_status;
    }

    ExportedRegion &reg = it->second;
    const uint32_t weight = (params.touch_weight > 0u) ? params.touch_weight : 1u;

    auto perform_migration = [&]() {
        reg.gpu_shadow.resize(static_cast<size_t>(reg.size_bytes));
        std::memcpy(reg.gpu_shadow.data(),
                    reinterpret_cast<const void *>(reg.base_va),
                    static_cast<size_t>(reg.size_bytes));
        if (gpu_upload_cb_ != nullptr) {
            gpu_upload_cb_(0u, reg.base_va, 0u,
                           reinterpret_cast<const void *>(reg.base_va),
                           reg.size_bytes, gpu_upload_user_data_);
        }
        if (!reg.is_migrated) {
            reg.is_migrated = true;
            total_migrated_hot_regions_.fetch_add(1u, std::memory_order_acq_rel);
        }
        reg.migration_count++;
    };

    switch (static_cast<scarlett_xmem_uma_hot_action_t>(params.access_kind)) {
        case XMEM_UMA_HOT_QUERY:
            break;
        case XMEM_UMA_HOT_RECORD_GPU_READ:
            reg.gpu_reads += weight;
            if (reg.tier == SCARLETT_UMA_TIER_B_EXT_HOST &&
                !reg.is_migrated &&
                reg.gpu_reads >= kHotMigrationReadThreshold &&
                reg.gpu_reads >= 4u * (reg.cpu_writes + 1u)) {
                perform_migration();
            }
            break;
        case XMEM_UMA_HOT_RECORD_CPU_WRITE:
            reg.cpu_writes += weight;
            if (reg.tier == SCARLETT_UMA_TIER_B_EXT_HOST && reg.is_migrated) {
                /* Keep shadow coherent or demote if write-heavy */
                if (reg.cpu_writes > reg.gpu_reads) {
                    reg.is_migrated = false;
                } else {
                    std::memcpy(reg.gpu_shadow.data(),
                                reinterpret_cast<const void *>(reg.base_va),
                                static_cast<size_t>(reg.size_bytes));
                }
            }
            break;
        case XMEM_UMA_HOT_FORCE_MIGRATE:
            perform_migration();
            break;
        default:
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
    }

    params.out_is_migrated = reg.is_migrated ? 1u : 0u;
    params.out_migration_count = reg.migration_count;
    params.out_gpu_reads = reg.gpu_reads;
    params.out_cpu_writes = reg.cpu_writes;
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

void UmaBridge::set_gpu_upload_callback(scarlett_uma_gpu_upload_cb_t cb, void *user_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpu_upload_cb_ = cb;
    gpu_upload_user_data_ = user_data;
}

scarlett_hresult_t UmaBridge::read_gpu_shadow(uint64_t region_base_va,
                                              uint64_t byte_offset,
                                              void *dst_buffer,
                                              uint64_t byte_length) {
    if (dst_buffer == nullptr || byte_length == 0u) {
        return SCARLETT_E_INVALIDARG;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = exported_regions_.find(region_base_va);
    if (it == exported_regions_.end()) {
        return SCARLETT_ERR_XMEM_OUT_OF_RANGE;
    }

    const ExportedRegion &reg = it->second;
    if (byte_offset > reg.size_bytes || byte_length > (reg.size_bytes - byte_offset)) {
        return SCARLETT_E_INVALIDARG;
    }

    if (reg.tier == SCARLETT_UMA_TIER_C_UFFD_WP ||
        (reg.tier == SCARLETT_UMA_TIER_B_EXT_HOST && reg.is_migrated)) {
        if (reg.gpu_shadow.size() < byte_offset + byte_length) {
            return SCARLETT_ERR_XMEM_UMA_BRIDGE_FAILED;
        }
        std::memcpy(dst_buffer, reg.gpu_shadow.data() + byte_offset,
                    static_cast<size_t>(byte_length));
        return SCARLETT_OK;
    }

    /* Tier A zero-copy or non-migrated Tier B reads directly from CPU/host memory */
    std::memcpy(dst_buffer,
                reinterpret_cast<const uint8_t *>(reg.base_va) + byte_offset,
                static_cast<size_t>(byte_length));
    return SCARLETT_OK;
}

} // namespace scarlett::xmem::unixlib
