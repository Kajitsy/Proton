/*
 * Proton-Scarlett — Thread Topology, Time-Sensitive & /dev/ntsync Subsystem
 *
 * Implements:
 *   - 7-core / 14-thread Xbox title CPU topology mapping onto host P-cores
 *     via /sys/devices/system/cpu + /sys/devices/cpu_core/cpus detection
 *   - XThreadSetTimeSensitive / XThreadIsTimeSensitive / XThreadAssertNotTimeSensitive
 *     (strict per-thread TLS state + best-effort OS nice boost without faking status)
 *   - Native Linux /dev/ntsync integration (NTSYNC_IOC_*) with clean, explicitly
 *     diagnosable Linux futex + eventfd fallback when /dev/ntsync is unavailable.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xmem_internal.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <linux/futex.h>
#include <linux/ntsync.h>
#ifndef NTSYNC_IOC_CREATE_EVENT
struct ntsync_event_args {
    __u32 manual;
    __u32 signaled;
};
#define NTSYNC_IOC_CREATE_EVENT _IOW('N', 0x87, struct ntsync_event_args)
#define NTSYNC_IOC_EVENT_SET    _IOR('N', 0x88, __u32)
#define NTSYNC_IOC_EVENT_RESET  _IOR('N', 0x89, __u32)
#define NTSYNC_IOC_EVENT_PULSE  _IOR('N', 0x8a, __u32)
#endif
#ifndef NTSYNC_IOC_CREATE_MUTEX
struct ntsync_mutex_args {
    __u32 owner;
    __u32 count;
};
#define NTSYNC_IOC_CREATE_MUTEX _IOW ('N', 0x84, struct ntsync_mutex_args)
#define NTSYNC_IOC_MUTEX_UNLOCK _IOWR('N', 0x85, struct ntsync_mutex_args)
#define NTSYNC_IOC_MUTEX_KILL   _IOW ('N', 0x86, __u32)
#endif
#ifndef NTSYNC_IOC_SEM_READ
#define NTSYNC_IOC_SEM_READ     _IOR ('N', 0x8b, struct ntsync_sem_args)
#define NTSYNC_IOC_MUTEX_READ   _IOR ('N', 0x8c, struct ntsync_mutex_args)
#define NTSYNC_IOC_EVENT_READ   _IOR ('N', 0x8d, struct ntsync_event_args)
#endif
#ifndef NTSYNC_IOC_SEM_RELEASE
#define NTSYNC_IOC_SEM_RELEASE  _IOWR('N', 0x81, __u32)
#endif
#ifndef NTSYNC_WAIT_REALTIME
#define NTSYNC_WAIT_REALTIME    0x1
struct ntsync_wait_args {
    __u64 timeout;
    __u64 objs;
    __u32 count;
    __u32 index;
    __u32 flags;
    __u32 owner;
    __u32 alert;
    __u32 pad;
};
#endif
#ifndef NTSYNC_IOC_WAIT_ANY
#define NTSYNC_IOC_WAIT_ANY     _IOWR('N', 0x82, struct ntsync_wait_args)
#endif
#ifndef NTSYNC_IOC_WAIT_ALL
#define NTSYNC_IOC_WAIT_ALL     _IOWR('N', 0x83, struct ntsync_wait_args)
#endif
#include <map>
#include <sched.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace scarlett::xmem::unixlib {

namespace {

uint32_t current_tid_u32() noexcept {
    long tid = ::syscall(SYS_gettid);
    return (tid > 0) ? static_cast<uint32_t>(tid) : 1u;
}

uint64_t monotonic_now_ns() noexcept {
    struct timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * UINT64_C(1000000000) +
           static_cast<uint64_t>(ts.tv_nsec);
}

bool read_sysfs_string(const std::string &path, std::string &out) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
    }
    std::getline(f, out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return !out.empty();
}

bool read_sysfs_u64(const std::string &path, uint64_t &out) {
    std::string s;
    if (!read_sysfs_string(path, s)) {
        return false;
    }
    char *endptr = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &endptr, 10);
    if (endptr == s.c_str()) {
        return false;
    }
    out = static_cast<uint64_t>(v);
    return true;
}

std::vector<uint32_t> parse_cpu_list(const std::string &list_str) {
    std::vector<uint32_t> cpus;
    std::stringstream ss(list_str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) {
            continue;
        }
        auto dash = token.find('-');
        if (dash == std::string::npos) {
            cpus.push_back(static_cast<uint32_t>(std::strtoul(token.c_str(), nullptr, 10)));
        } else {
            uint32_t start = static_cast<uint32_t>(
                std::strtoul(token.substr(0, dash).c_str(), nullptr, 10));
            uint32_t end = static_cast<uint32_t>(
                std::strtoul(token.substr(dash + 1).c_str(), nullptr, 10));
            for (uint32_t c = start; c <= end && c < 4096u; ++c) {
                cpus.push_back(c);
            }
        }
    }
    return cpus;
}

thread_local bool g_tls_time_sensitive = false;
thread_local bool g_tls_os_boosted = false;
thread_local int g_tls_saved_nice = 0;
std::atomic<uint32_t> g_ts_assertion_trips{0};

} // namespace

ThreadSyncManager &ThreadSyncManager::instance() {
    static ThreadSyncManager mgr;
    return mgr;
}

ThreadSyncManager::ThreadSyncManager() {
    initialize();
}

ThreadSyncManager::~ThreadSyncManager() {
    reset_sync_handles();
    if (ntsync_dev_fd_ >= 0) {
        ::close(ntsync_dev_fd_);
        ntsync_dev_fd_ = -1;
    }
}

void ThreadSyncManager::initialize() {
    {
        std::lock_guard<std::mutex> lock(topo_mutex_);
        if (!topo_detected_) {
            detect_cpu_topology_locked();
        }
    }
    {
        std::lock_guard<std::mutex> lock(sync_mutex_);
        if (!ntsync_probed_) {
            probe_ntsync_locked();
        }
    }
}

void ThreadSyncManager::reset_sync_handles() {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    for (auto &kv : sync_objects_) {
        if (kv.second.ntsync_obj_fd >= 0) {
            ::close(kv.second.ntsync_obj_fd);
            kv.second.ntsync_obj_fd = -1;
        }
        if (kv.second.notify_efd >= 0) {
            ::close(kv.second.notify_efd);
            kv.second.notify_efd = -1;
        }
    }
    sync_objects_.clear();
}

void ThreadSyncManager::detect_cpu_topology_locked() {
    std::vector<uint32_t> online_cpus;
    std::string online_str;
    if (read_sysfs_string("/sys/devices/system/cpu/online", online_str)) {
        online_cpus = parse_cpu_list(online_str);
    }
    if (online_cpus.empty()) {
        long nprocs = ::sysconf(_SC_NPROCESSORS_ONLN);
        uint32_t count = (nprocs > 0) ? static_cast<uint32_t>(nprocs) : 1u;
        for (uint32_t i = 0; i < count; ++i) {
            online_cpus.push_back(i);
        }
    }
    std::sort(online_cpus.begin(), online_cpus.end());
    online_cpus.erase(std::unique(online_cpus.begin(), online_cpus.end()), online_cpus.end());

    struct HostCpuInfo {
        uint32_t cpu_id = 0;
        uint64_t pkg_id = 0;
        uint64_t core_id = 0;
        uint64_t max_freq_khz = 0;
        bool is_pcore = true;
    };

    /* Check Intel hybrid P-core sysfs mask if exposed by kernel */
    std::set<uint32_t> explicit_pcores;
    std::string pcore_str;
    if (read_sysfs_string("/sys/devices/cpu_core/cpus", pcore_str)) {
        auto parsed = parse_cpu_list(pcore_str);
        explicit_pcores.insert(parsed.begin(), parsed.end());
    }

    std::vector<HostCpuInfo> infos;
    infos.reserve(online_cpus.size());
    uint64_t global_max_freq = 0;
    uint64_t global_min_freq = UINT64_MAX;

    for (uint32_t cpu : online_cpus) {
        HostCpuInfo info{};
        info.cpu_id = cpu;
        const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        (void)read_sysfs_u64(base + "/topology/physical_package_id", info.pkg_id);
        if (!read_sysfs_u64(base + "/topology/core_id", info.core_id)) {
            info.core_id = cpu;
        }
        if (!read_sysfs_u64(base + "/cpufreq/cpuinfo_max_freq", info.max_freq_khz)) {
            (void)read_sysfs_u64(base + "/cpufreq/base_frequency", info.max_freq_khz);
        }
        if (info.max_freq_khz > 0u) {
            global_max_freq = std::max(global_max_freq, info.max_freq_khz);
            global_min_freq = std::min(global_min_freq, info.max_freq_khz);
        }
        infos.push_back(info);
    }

    bool hybrid = false;
    uint32_t pcore_cpus = 0;
    uint32_t ecore_cpus = 0;
    for (auto &info : infos) {
        if (!explicit_pcores.empty()) {
            info.is_pcore = (explicit_pcores.count(info.cpu_id) > 0u);
        } else if (global_max_freq > 0u && global_min_freq < global_max_freq &&
                   (global_min_freq * 100u) < (global_max_freq * 90u)) {
            info.is_pcore = (info.max_freq_khz * 100u) >= (global_max_freq * 90u);
        } else {
            info.is_pcore = true;
        }
        if (info.is_pcore) {
            pcore_cpus++;
        } else {
            ecore_cpus++;
        }
    }
    hybrid = (ecore_cpus > 0u && pcore_cpus > 0u);

    /* Group logical CPUs into physical core groups, P-cores first */
    using CoreKey = std::pair<uint64_t, uint64_t>;
    std::map<CoreKey, std::vector<uint32_t>> pcore_groups;
    std::map<CoreKey, std::vector<uint32_t>> ecore_groups;

    for (const auto &info : infos) {
        CoreKey key{info.pkg_id, info.core_id};
        if (info.is_pcore) {
            pcore_groups[key].push_back(info.cpu_id);
        } else {
            ecore_groups[key].push_back(info.cpu_id);
        }
    }

    std::vector<std::vector<uint32_t>> ordered_cores;
    for (auto &kv : pcore_groups) {
        std::sort(kv.second.begin(), kv.second.end());
        ordered_cores.push_back(std::move(kv.second));
    }
    for (auto &kv : ecore_groups) {
        std::sort(kv.second.begin(), kv.second.end());
        ordered_cores.push_back(std::move(kv.second));
    }
    if (ordered_cores.empty()) {
        ordered_cores.push_back({0u});
    }

    /* Map Xbox 7 title cores (14 SMT threads) onto P-cores first */
    const size_t num_phys_cores = ordered_cores.size();
    for (uint32_t xbox_core = 0; xbox_core < XMEM_XBOX_TITLE_CORES; ++xbox_core) {
        const auto &group = ordered_cores[xbox_core % num_phys_cores];
        const uint32_t primary_cpu = group[0];
        uint32_t secondary_cpu = primary_cpu;
        if (group.size() >= 2u) {
            /* True SMT sibling on the same physical P-core */
            secondary_cpu = group[1];
        } else if (num_phys_cores > 1u) {
            /* Spread second logical thread across distinct core on non-SMT CPUs */
            secondary_cpu = ordered_cores[(xbox_core + (num_phys_cores / 2u)) % num_phys_cores][0];
        }
        topology_.xbox_thread_to_host_cpu[xbox_core * 2u] = primary_cpu;
        topology_.xbox_thread_to_host_cpu[xbox_core * 2u + 1u] = secondary_cpu;
    }

    topology_.host_total_cpus = static_cast<uint32_t>(infos.size());
    topology_.host_pcore_cpus = pcore_cpus;
    topology_.host_ecore_cpus = ecore_cpus;
    topology_.is_hybrid = hybrid ? 1u : 0u;
    topo_detected_ = true;
}

scarlett_hresult_t
ThreadSyncManager::handle_topology(scarlett_xmem_thread_topology_params_t &params) {
    CpuTopology snap{};
    {
        std::lock_guard<std::mutex> lock(topo_mutex_);
        if (!topo_detected_) {
            detect_cpu_topology_locked();
        }
        snap = topology_;
    }

    params.out_host_total_cpus = snap.host_total_cpus;
    params.out_host_pcore_cpus = snap.host_pcore_cpus;
    params.out_host_ecore_cpus = snap.host_ecore_cpus;
    params.out_is_hybrid = snap.is_hybrid;
    params.out_xbox_core_count = XMEM_XBOX_TITLE_CORES;
    params.out_xbox_thread_count = XMEM_XBOX_TITLE_THREADS;
    for (uint32_t i = 0; i < XMEM_XBOX_TITLE_THREADS; ++i) {
        params.out_host_cpu_map[i] = snap.xbox_thread_to_host_cpu[i];
    }
    params.out_affinity_applied = 0u;

    if (params.action == 0u) {
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (params.action == 1u) {
        const uint32_t masked = params.xbox_thread_mask & ((1u << XMEM_XBOX_TITLE_THREADS) - 1u);
        if (masked == 0u) {
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
        }
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (uint32_t t = 0; t < XMEM_XBOX_TITLE_THREADS; ++t) {
            if ((masked & (1u << t)) != 0u) {
                uint32_t host_cpu = snap.xbox_thread_to_host_cpu[t];
                if (host_cpu < CPU_SETSIZE) {
                    CPU_SET(host_cpu, &cpuset);
                }
            }
        }
        if (::sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
            params.out_affinity_applied = 0u;
            params.out_status = SCARLETT_E_ACCESSDENIED;
            return params.out_status;
        }
        params.out_affinity_applied = 1u;
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    params.out_status = SCARLETT_E_INVALIDARG;
    return params.out_status;
}

scarlett_hresult_t
ThreadSyncManager::handle_time_sensitive(scarlett_xmem_thread_ts_params_t &params,
                                         uint32_t strict_mode) {
    params.out_previous_state = g_tls_time_sensitive ? 1u : 0u;
    params.out_os_priority_applied = 0u;

    if (params.action == 0u) {
        /* XThreadIsTimeSensitive */
        params.out_current_state = g_tls_time_sensitive ? 1u : 0u;
        params.out_assertion_trips = g_ts_assertion_trips.load(std::memory_order_acquire);
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (params.action == 1u) {
        /* XThreadSetTimeSensitive */
        const bool enable = (params.is_time_sensitive != 0u);
        const id_t tid = static_cast<id_t>(current_tid_u32());
        if (enable && !g_tls_time_sensitive) {
            errno = 0;
            int cur_nice = ::getpriority(PRIO_PROCESS, tid);
            if (errno == 0) {
                g_tls_saved_nice = cur_nice;
            }
            if (::setpriority(PRIO_PROCESS, tid, -5) == 0) {
                g_tls_os_boosted = true;
                params.out_os_priority_applied = 1u;
            } else {
                /* Unprivileged: keep logical TLS flag accurate, report OS boost honestly as 0 */
                g_tls_os_boosted = false;
                params.out_os_priority_applied = 0u;
            }
        } else if (!enable && g_tls_os_boosted) {
            if (::setpriority(PRIO_PROCESS, tid, g_tls_saved_nice) == 0) {
                params.out_os_priority_applied = 1u;
            }
            g_tls_os_boosted = false;
        } else if (enable && g_tls_os_boosted) {
            params.out_os_priority_applied = 1u;
        }

        g_tls_time_sensitive = enable;
        params.out_current_state = g_tls_time_sensitive ? 1u : 0u;
        params.out_assertion_trips = g_ts_assertion_trips.load(std::memory_order_acquire);
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (params.action == 2u) {
        /* XThreadAssertNotTimeSensitive */
        params.out_current_state = g_tls_time_sensitive ? 1u : 0u;
        if (!g_tls_time_sensitive) {
            params.out_assertion_trips = g_ts_assertion_trips.load(std::memory_order_acquire);
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        const uint32_t trips =
            g_ts_assertion_trips.fetch_add(1u, std::memory_order_acq_rel) + 1u;
        params.out_assertion_trips = trips;
        std::fprintf(stderr,
                     "[scarlett-xmem] XThreadAssertNotTimeSensitive FAILED on tid=%u "
                     "(trips=%u, strict=%u) -> E_GS_ASYNC_FUNCTION_REQUIRED\n",
                     current_tid_u32(), trips, strict_mode);
        params.out_status = SCARLETT_E_GS_ASYNC_FUNCTION_REQUIRED;
        return params.out_status;
    }

    params.out_status = SCARLETT_E_INVALIDARG;
    return params.out_status;
}

void ThreadSyncManager::probe_ntsync_locked() {
    ntsync_probed_ = true;
    const char *no_ntsync = std::getenv("PROTON_NO_NTSYNC");
    if (no_ntsync != nullptr && (no_ntsync[0] == '1' || strcasecmp(no_ntsync, "true") == 0)) {
        ntsync_dev_fd_ = -1;
        ntsync_errno_ = ENODEV;
        active_backend_ = XMEM_SYNC_BACKEND_FUTEX_FALLBACK;
        return;
    }

    int fd = ::open("/dev/ntsync", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        ntsync_dev_fd_ = fd;
        ntsync_errno_ = 0;
        active_backend_ = XMEM_SYNC_BACKEND_NTSYNC_KERNEL;
    } else {
        ntsync_dev_fd_ = -1;
        ntsync_errno_ = (errno != 0) ? errno : ENOENT;
        active_backend_ = XMEM_SYNC_BACKEND_FUTEX_FALLBACK;
    }
}

void ThreadSyncManager::wake_fallback_waiters_locked() {
    fallback_wait_gen_.fetch_add(1u, std::memory_order_acq_rel);
    (void)::syscall(SYS_futex,
                    reinterpret_cast<uint32_t *>(&fallback_wait_gen_),
                    FUTEX_WAKE_PRIVATE,
                    INT_MAX,
                    nullptr, nullptr, 0);
}

bool ThreadSyncManager::is_object_signaled_locked(const SyncObject &obj,
                                                  uint32_t caller_tid) const noexcept {
    switch (obj.type) {
        case XMEM_SYNC_TYPE_EVENT:
            return obj.signaled != 0u;
        case XMEM_SYNC_TYPE_SEMAPHORE:
            return obj.sem_count > 0u;
        case XMEM_SYNC_TYPE_MUTEX:
            return obj.mutex_count == 0u || obj.mutex_owner_tid == caller_tid;
        default:
            return false;
    }
}

void ThreadSyncManager::consume_object_locked(SyncObject &obj,
                                              uint32_t caller_tid) noexcept {
    switch (obj.type) {
        case XMEM_SYNC_TYPE_EVENT:
            if (obj.manual_reset == 0u) {
                obj.signaled = 0u;
            }
            break;
        case XMEM_SYNC_TYPE_SEMAPHORE:
            if (obj.sem_count > 0u) {
                obj.sem_count--;
            }
            break;
        case XMEM_SYNC_TYPE_MUTEX:
            obj.mutex_owner_tid = caller_tid;
            obj.mutex_count++;
            break;
        default:
            break;
    }
}

scarlett_hresult_t
ThreadSyncManager::sync_create(scarlett_xmem_sync_create_params_t &params) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    if (!ntsync_probed_) {
        probe_ntsync_locked();
    }

    params.out_handle = 0;
    const bool force_fallback = (params.flags & XMEM_SYNC_CREATE_FLAG_FORCE_FALLBACK) != 0u;
    const bool use_ntsync = (!force_fallback && ntsync_dev_fd_ >= 0);

    params.out_backend = use_ntsync
                             ? static_cast<uint32_t>(XMEM_SYNC_BACKEND_NTSYNC_KERNEL)
                             : static_cast<uint32_t>(XMEM_SYNC_BACKEND_FUTEX_FALLBACK);
    params.out_ntsync_errno = use_ntsync ? 0 : ((ntsync_errno_ != 0) ? ntsync_errno_ : ENODEV);

    SyncObject obj{};
    obj.handle = next_sync_handle_++;
    obj.type = static_cast<scarlett_xmem_sync_type_t>(params.sync_type);
    obj.backend = static_cast<scarlett_xmem_sync_backend_t>(params.out_backend);

    if (obj.type == XMEM_SYNC_TYPE_EVENT) {
        const uint32_t manual = (params.arg0 != 0u) ? 1u : 0u;
        const uint32_t init_sig = (params.arg1 != 0u) ? 1u : 0u;
        if (use_ntsync) {
            struct ntsync_event_args args {};
            args.manual = manual;
            args.signaled = init_sig;
            int obj_fd = ::ioctl(ntsync_dev_fd_, NTSYNC_IOC_CREATE_EVENT, &args);
            if (obj_fd < 0) {
                params.out_ntsync_errno = errno;
                params.out_status = SCARLETT_E_FAIL;
                return params.out_status;
            }
            obj.ntsync_obj_fd = obj_fd;
        } else {
            obj.notify_efd = ::eventfd(init_sig, EFD_NONBLOCK | EFD_CLOEXEC);
            obj.manual_reset = manual;
            obj.signaled = init_sig;
        }
    } else if (obj.type == XMEM_SYNC_TYPE_SEMAPHORE) {
        const uint32_t init_cnt = params.arg0;
        const uint32_t max_cnt = params.arg1;
        if (max_cnt == 0u || init_cnt > max_cnt) {
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
        }
        if (use_ntsync) {
            struct ntsync_sem_args args {};
            args.count = init_cnt;
            args.max = max_cnt;
            int obj_fd = ::ioctl(ntsync_dev_fd_, NTSYNC_IOC_CREATE_SEM, &args);
            if (obj_fd < 0) {
                params.out_ntsync_errno = errno;
                params.out_status = SCARLETT_E_FAIL;
                return params.out_status;
            }
            obj.ntsync_obj_fd = obj_fd;
        } else {
            obj.notify_efd = ::eventfd(init_cnt, EFD_NONBLOCK | EFD_CLOEXEC);
            obj.sem_count = init_cnt;
            obj.sem_max = max_cnt;
        }
    } else if (obj.type == XMEM_SYNC_TYPE_MUTEX) {
        const uint32_t init_owner = (params.arg0 != 0u) ? current_tid_u32() : 0u;
        const uint32_t init_count = (params.arg0 != 0u) ? 1u : 0u;
        if (use_ntsync) {
            struct ntsync_mutex_args args {};
            args.owner = init_owner;
            args.count = init_count;
            int obj_fd = ::ioctl(ntsync_dev_fd_, NTSYNC_IOC_CREATE_MUTEX, &args);
            if (obj_fd < 0) {
                params.out_ntsync_errno = errno;
                params.out_status = SCARLETT_E_FAIL;
                return params.out_status;
            }
            obj.ntsync_obj_fd = obj_fd;
        } else {
            obj.notify_efd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            obj.mutex_owner_tid = init_owner;
            obj.mutex_count = init_count;
        }
    } else {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    params.out_handle = obj.handle;
    sync_objects_.emplace(obj.handle, obj);
    params.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t
ThreadSyncManager::sync_op(scarlett_xmem_sync_op_params_t &params) {
    std::lock_guard<std::mutex> lock(sync_mutex_);
    auto it = sync_objects_.find(params.handle);
    if (it == sync_objects_.end()) {
        params.out_status = SCARLETT_E_HANDLE;
        return params.out_status;
    }

    SyncObject &obj = it->second;
    params.out_backend = static_cast<uint32_t>(obj.backend);
    params.out_prev_val = 0;
    params.out_aux_val = 0;

    if (params.op == XMEM_SYNC_OP_CLOSE) {
        if (obj.ntsync_obj_fd >= 0) {
            ::close(obj.ntsync_obj_fd);
            obj.ntsync_obj_fd = -1;
        }
        if (obj.notify_efd >= 0) {
            ::close(obj.notify_efd);
            obj.notify_efd = -1;
        }
        sync_objects_.erase(it);
        params.out_status = SCARLETT_OK;
        return SCARLETT_OK;
    }

    if (obj.backend == XMEM_SYNC_BACKEND_NTSYNC_KERNEL) {
        switch (static_cast<scarlett_xmem_sync_op_t>(params.op)) {
            case XMEM_SYNC_OP_EVENT_SET: {
                if (obj.type != XMEM_SYNC_TYPE_EVENT) {
                    params.out_status = SCARLETT_E_INVALIDARG;
                    return params.out_status;
                }
                __u32 prev = 0;
                if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_EVENT_SET, &prev) != 0) {
                    params.out_status = SCARLETT_E_FAIL;
                    return params.out_status;
                }
                params.out_prev_val = prev;
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            case XMEM_SYNC_OP_EVENT_RESET: {
                if (obj.type != XMEM_SYNC_TYPE_EVENT) {
                    params.out_status = SCARLETT_E_INVALIDARG;
                    return params.out_status;
                }
                __u32 prev = 0;
                if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_EVENT_RESET, &prev) != 0) {
                    params.out_status = SCARLETT_E_FAIL;
                    return params.out_status;
                }
                params.out_prev_val = prev;
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            case XMEM_SYNC_OP_EVENT_PULSE: {
                if (obj.type != XMEM_SYNC_TYPE_EVENT) {
                    params.out_status = SCARLETT_E_INVALIDARG;
                    return params.out_status;
                }
                __u32 prev = 0;
                if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_EVENT_PULSE, &prev) != 0) {
                    params.out_status = SCARLETT_E_FAIL;
                    return params.out_status;
                }
                params.out_prev_val = prev;
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            case XMEM_SYNC_OP_SEM_RELEASE: {
                if (obj.type != XMEM_SYNC_TYPE_SEMAPHORE || params.arg0 == 0u) {
                    params.out_status = SCARLETT_E_INVALIDARG;
                    return params.out_status;
                }
                __u32 count = params.arg0;
                if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_SEM_RELEASE, &count) != 0) {
                    params.out_status = (errno == EOVERFLOW) ? SCARLETT_E_INVALIDARG
                                                             : SCARLETT_E_FAIL;
                    return params.out_status;
                }
                params.out_prev_val = count;
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            case XMEM_SYNC_OP_MUTEX_UNLOCK: {
                if (obj.type != XMEM_SYNC_TYPE_MUTEX) {
                    params.out_status = SCARLETT_E_INVALIDARG;
                    return params.out_status;
                }
                struct ntsync_mutex_args args {};
                args.owner = current_tid_u32();
                if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_MUTEX_UNLOCK, &args) != 0) {
                    params.out_status = (errno == EPERM) ? SCARLETT_E_ACCESSDENIED
                                                         : SCARLETT_E_FAIL;
                    return params.out_status;
                }
                params.out_prev_val = args.count;
                params.out_aux_val = args.owner;
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            case XMEM_SYNC_OP_READ_STATE: {
                if (obj.type == XMEM_SYNC_TYPE_EVENT) {
                    struct ntsync_event_args args {};
                    if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_EVENT_READ, &args) != 0) {
                        params.out_status = SCARLETT_E_FAIL;
                        return params.out_status;
                    }
                    params.out_prev_val = args.signaled;
                    params.out_aux_val = args.manual;
                } else if (obj.type == XMEM_SYNC_TYPE_SEMAPHORE) {
                    struct ntsync_sem_args args {};
                    if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_SEM_READ, &args) != 0) {
                        params.out_status = SCARLETT_E_FAIL;
                        return params.out_status;
                    }
                    params.out_prev_val = args.count;
                    params.out_aux_val = args.max;
                } else {
                    struct ntsync_mutex_args args {};
                    if (::ioctl(obj.ntsync_obj_fd, NTSYNC_IOC_MUTEX_READ, &args) != 0) {
                        params.out_status = SCARLETT_E_FAIL;
                        return params.out_status;
                    }
                    params.out_prev_val = args.count;
                    params.out_aux_val = args.owner;
                }
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            default:
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
        }
    }

    /* Futex + eventfd fallback path */
    switch (static_cast<scarlett_xmem_sync_op_t>(params.op)) {
        case XMEM_SYNC_OP_EVENT_SET: {
            if (obj.type != XMEM_SYNC_TYPE_EVENT) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            params.out_prev_val = obj.signaled;
            obj.signaled = 1u;
            if (obj.notify_efd >= 0) {
                uint64_t one = 1;
                ssize_t ignored = ::write(obj.notify_efd, &one, sizeof(one));
                (void)ignored;
            }
            wake_fallback_waiters_locked();
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        case XMEM_SYNC_OP_EVENT_RESET: {
            if (obj.type != XMEM_SYNC_TYPE_EVENT) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            params.out_prev_val = obj.signaled;
            obj.signaled = 0u;
            if (obj.notify_efd >= 0) {
                uint64_t drain = 0;
                ssize_t ignored = ::read(obj.notify_efd, &drain, sizeof(drain));
                (void)ignored;
            }
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        case XMEM_SYNC_OP_EVENT_PULSE: {
            if (obj.type != XMEM_SYNC_TYPE_EVENT) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            params.out_prev_val = obj.signaled;
            obj.pulse_seq++;
            if (obj.manual_reset == 0u) {
                obj.pulse_auto_budget = 1u;
            }
            obj.signaled = 0u;
            wake_fallback_waiters_locked();
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        case XMEM_SYNC_OP_SEM_RELEASE: {
            if (obj.type != XMEM_SYNC_TYPE_SEMAPHORE || params.arg0 == 0u) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            if (params.arg0 > obj.sem_max - obj.sem_count) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            params.out_prev_val = obj.sem_count;
            obj.sem_count += params.arg0;
            if (obj.notify_efd >= 0) {
                uint64_t add = params.arg0;
                ssize_t ignored = ::write(obj.notify_efd, &add, sizeof(add));
                (void)ignored;
            }
            wake_fallback_waiters_locked();
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        case XMEM_SYNC_OP_MUTEX_UNLOCK: {
            if (obj.type != XMEM_SYNC_TYPE_MUTEX) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            const uint32_t caller = current_tid_u32();
            if (obj.mutex_count == 0u || obj.mutex_owner_tid != caller) {
                params.out_status = SCARLETT_E_ACCESSDENIED;
                return params.out_status;
            }
            params.out_prev_val = obj.mutex_count;
            obj.mutex_count--;
            if (obj.mutex_count == 0u) {
                obj.mutex_owner_tid = 0u;
                wake_fallback_waiters_locked();
            }
            params.out_aux_val = obj.mutex_owner_tid;
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        case XMEM_SYNC_OP_READ_STATE: {
            if (obj.type == XMEM_SYNC_TYPE_EVENT) {
                params.out_prev_val = obj.signaled;
                params.out_aux_val = obj.manual_reset;
            } else if (obj.type == XMEM_SYNC_TYPE_SEMAPHORE) {
                params.out_prev_val = obj.sem_count;
                params.out_aux_val = obj.sem_max;
            } else {
                params.out_prev_val = obj.mutex_count;
                params.out_aux_val = obj.mutex_owner_tid;
            }
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        default:
            params.out_status = SCARLETT_E_INVALIDARG;
            return params.out_status;
    }
}

scarlett_hresult_t
ThreadSyncManager::sync_wait(scarlett_xmem_sync_wait_params_t &params) {
    params.out_signaled_index = XMEM_SYNC_WAIT_TIMEOUT_INDEX;
    params.out_timed_out = 0u;

    if (params.handles_addr == 0u || params.handle_count == 0u ||
        params.handle_count > XMEM_SYNC_MAX_WAIT_HANDLES) {
        params.out_status = SCARLETT_E_INVALIDARG;
        return params.out_status;
    }

    const auto *handles = reinterpret_cast<const uint64_t *>(params.handles_addr);
    const uint32_t caller_tid = current_tid_u32();

    /* Validate handles & check backend uniformity */
    int ntsync_fds[XMEM_SYNC_MAX_WAIT_HANDLES];
    scarlett_xmem_sync_backend_t backend = XMEM_SYNC_BACKEND_UNINITIALIZED;
    {
        std::lock_guard<std::mutex> lock(sync_mutex_);
        std::unordered_set<uint64_t> seen;
        for (uint32_t i = 0; i < params.handle_count; ++i) {
            if (params.wait_all != 0u && !seen.insert(handles[i]).second) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            auto it = sync_objects_.find(handles[i]);
            if (it == sync_objects_.end()) {
                params.out_status = SCARLETT_E_HANDLE;
                return params.out_status;
            }
            if (i == 0u) {
                backend = it->second.backend;
            } else if (it->second.backend != backend) {
                params.out_status = SCARLETT_E_INVALIDARG;
                return params.out_status;
            }
            ntsync_fds[i] = it->second.ntsync_obj_fd;
        }
    }
    params.out_backend = static_cast<uint32_t>(backend);

    /* Path 1: Native Linux /dev/ntsync kernel wait */
    if (backend == XMEM_SYNC_BACKEND_NTSYNC_KERNEL) {
        struct ntsync_wait_args wargs {};
        if (params.timeout_ns == XMEM_SYNC_TIMEOUT_INFINITE) {
            wargs.timeout = UINT64_MAX;
        } else {
            const uint64_t now = monotonic_now_ns();
            wargs.timeout = (params.timeout_ns > UINT64_MAX - now)
                                ? UINT64_MAX
                                : (now + params.timeout_ns);
        }
        wargs.objs = reinterpret_cast<uint64_t>(ntsync_fds);
        wargs.count = params.handle_count;
        wargs.owner = caller_tid;
        wargs.flags = 0; /* CLOCK_MONOTONIC */

        const unsigned long req = (params.wait_all != 0u) ? NTSYNC_IOC_WAIT_ALL
                                                          : NTSYNC_IOC_WAIT_ANY;
        int rc = 0;
        do {
            rc = ::ioctl(ntsync_dev_fd_, req, &wargs);
        } while (rc < 0 && errno == EINTR);

        if (rc == 0) {
            params.out_signaled_index = (params.wait_all != 0u) ? 0u : wargs.index;
            params.out_timed_out = 0u;
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        if (errno == ETIMEDOUT) {
            params.out_signaled_index = XMEM_SYNC_WAIT_TIMEOUT_INDEX;
            params.out_timed_out = 1u;
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
        params.out_status = SCARLETT_E_FAIL;
        return params.out_status;
    }

    /* Path 2: Clean, atomic futex + eventfd fallback */
    const bool infinite = (params.timeout_ns == XMEM_SYNC_TIMEOUT_INFINITE);
    const uint64_t deadline_ns = infinite ? UINT64_MAX : (monotonic_now_ns() + params.timeout_ns);

    /* Capture initial pulse sequences before sleeping so PulseEvent wakes waiting threads cleanly */
    uint64_t initial_pulse_seq[XMEM_SYNC_MAX_WAIT_HANDLES] = {};
    {
        std::lock_guard<std::mutex> lock(sync_mutex_);
        for (uint32_t i = 0; i < params.handle_count; ++i) {
            auto it = sync_objects_.find(handles[i]);
            if (it != sync_objects_.end()) {
                initial_pulse_seq[i] = it->second.pulse_seq;
            }
        }
    }

    auto is_ready_or_pulsed = [&](const SyncObject &obj, uint32_t idx) -> bool {
        if (is_object_signaled_locked(obj, caller_tid)) {
            return true;
        }
        if (obj.type == XMEM_SYNC_TYPE_EVENT && obj.pulse_seq != initial_pulse_seq[idx]) {
            if (obj.manual_reset != 0u || obj.pulse_auto_budget > 0u) {
                return true;
            }
        }
        return false;
    };

    auto consume_ready_or_pulsed = [&](SyncObject &obj, uint32_t idx) {
        if (obj.type == XMEM_SYNC_TYPE_EVENT && obj.signaled == 0u &&
            obj.pulse_seq != initial_pulse_seq[idx]) {
            if (obj.manual_reset == 0u && obj.pulse_auto_budget > 0u) {
                obj.pulse_auto_budget--;
            }
            return;
        }
        consume_object_locked(obj, caller_tid);
    };

    for (;;) {
        uint32_t observed_gen = 0;
        {
            std::lock_guard<std::mutex> lock(sync_mutex_);
            observed_gen = fallback_wait_gen_.load(std::memory_order_acquire);

            if (params.wait_all != 0u) {
                bool all_ready = true;
                for (uint32_t i = 0; i < params.handle_count; ++i) {
                    auto it = sync_objects_.find(handles[i]);
                    if (it == sync_objects_.end()) {
                        params.out_status = SCARLETT_E_HANDLE;
                        return params.out_status;
                    }
                    if (!is_ready_or_pulsed(it->second, i)) {
                        all_ready = false;
                        break;
                    }
                }
                if (all_ready) {
                    for (uint32_t i = 0; i < params.handle_count; ++i) {
                        consume_ready_or_pulsed(sync_objects_.at(handles[i]), i);
                    }
                    params.out_signaled_index = 0u;
                    params.out_timed_out = 0u;
                    params.out_status = SCARLETT_OK;
                    return SCARLETT_OK;
                }
            } else {
                for (uint32_t i = 0; i < params.handle_count; ++i) {
                    auto it = sync_objects_.find(handles[i]);
                    if (it == sync_objects_.end()) {
                        params.out_status = SCARLETT_E_HANDLE;
                        return params.out_status;
                    }
                    if (is_ready_or_pulsed(it->second, i)) {
                        consume_ready_or_pulsed(it->second, i);
                        params.out_signaled_index = i;
                        params.out_timed_out = 0u;
                        params.out_status = SCARLETT_OK;
                        return SCARLETT_OK;
                    }
                }
            }
        }

        if (params.timeout_ns == 0u) {
            params.out_signaled_index = XMEM_SYNC_WAIT_TIMEOUT_INDEX;
            params.out_timed_out = 1u;
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }

        struct timespec rel_ts {};
        const struct timespec *ts_ptr = nullptr;
        if (!infinite) {
            const uint64_t now = monotonic_now_ns();
            if (now >= deadline_ns) {
                params.out_signaled_index = XMEM_SYNC_WAIT_TIMEOUT_INDEX;
                params.out_timed_out = 1u;
                params.out_status = SCARLETT_OK;
                return SCARLETT_OK;
            }
            const uint64_t rem = deadline_ns - now;
            rel_ts.tv_sec = static_cast<time_t>(rem / UINT64_C(1000000000));
            rel_ts.tv_nsec = static_cast<long>(rem % UINT64_C(1000000000));
            ts_ptr = &rel_ts;
        }

        long rc = ::syscall(SYS_futex,
                            reinterpret_cast<uint32_t *>(&fallback_wait_gen_),
                            FUTEX_WAIT_PRIVATE,
                            observed_gen,
                            ts_ptr,
                            nullptr,
                            0);
        if (rc < 0 && errno == ETIMEDOUT) {
            params.out_signaled_index = XMEM_SYNC_WAIT_TIMEOUT_INDEX;
            params.out_timed_out = 1u;
            params.out_status = SCARLETT_OK;
            return SCARLETT_OK;
        }
    }
}

} // namespace scarlett::xmem::unixlib
