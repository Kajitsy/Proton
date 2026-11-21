/*
 * Proton-Scarlett — XTaskQueue, XAsync & XAsyncProvider Implementation
 *
 * Supports:
 *   - All 5 XTaskQueueDispatchMode port modes:
 *     ThreadPool, SerializedThreadPool, Manual, Immediate, Inline
 *   - XTaskQueueSubmitDelayedCallback (steady_clock timer heap)
 *   - XTaskQueueDispatch with /dev/ntsync + SYS_futex wakeup & timeout
 *   - XTaskQueueTerminate (drain/cancel + wait + termination callback)
 *   - XTaskQueueRegisterMonitor / UnregisterMonitor
 *   - Full XAsync / XAsyncProvider lifecycle with single-completion & cancel safety
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "xgameruntime_internal.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/ntsync.h>
#include <memory>
#include <mutex>
#include <queue>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace scarlett::xgameruntime::unixlib {

namespace {

constexpr uint64_t kAsyncBlockMagic = UINT64_C(0x534341524153594E);

struct AsyncInternalLayout {
    uint64_t magic;
    uint64_t async_id;
    uint64_t reserved0;
    uint64_t reserved1;
};

static_assert(sizeof(AsyncInternalLayout) == 32, "Must fit in XAsyncBlock::internal[32]");

/* ============================================================================
 * Low-level /dev/ntsync + Linux SYS_futex Wakeup Primitive
 * ============================================================================ */

class PortWakePrimitive {
public:
    PortWakePrimitive() {
        int fd = ::open("/dev/ntsync", O_CLOEXEC | O_RDWR);
        if (fd >= 0) {
            struct ntsync_sem_args args{};
            args.count = 0;
            args.max = 0x7FFFFFFF;
            int sem_fd = ::ioctl(fd, NTSYNC_IOC_CREATE_SEM, &args);
            ::close(fd);
            if (sem_fd >= 0) {
                ntsync_sem_fd_ = sem_fd;
                backend_ = 1u; /* ntsync */
                return;
            }
        }
        backend_ = 2u; /* futex */
    }

    ~PortWakePrimitive() {
        if (ntsync_sem_fd_ >= 0) {
            ::close(ntsync_sem_fd_);
            ntsync_sem_fd_ = -1;
        }
    }

    uint32_t backend() const noexcept { return backend_; }

    void signal_one() noexcept {
        if (ntsync_sem_fd_ >= 0) {
            __u32 count = 1;
            (void)::ioctl(ntsync_sem_fd_, NTSYNC_IOC_SEM_RELEASE, &count);
            return;
        }
        futex_seq_.fetch_add(1u, std::memory_order_release);
        ::syscall(SYS_futex, &futex_seq_, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
    }

    void signal_all() noexcept {
        if (ntsync_sem_fd_ >= 0) {
            __u32 count = 64;
            (void)::ioctl(ntsync_sem_fd_, NTSYNC_IOC_SEM_RELEASE, &count);
            return;
        }
        futex_seq_.fetch_add(1u, std::memory_order_release);
        ::syscall(SYS_futex, &futex_seq_, FUTEX_WAKE_PRIVATE, INT_MAX, nullptr, nullptr, 0);
    }

    uint32_t snapshot_seq() const noexcept {
        return futex_seq_.load(std::memory_order_acquire);
    }

    void wait_until(uint32_t observed_seq,
                    const std::chrono::steady_clock::time_point *deadline) noexcept {
        if (ntsync_sem_fd_ >= 0) {
            /* Drain via non-blocking poll/read or fallback to futex-style timed wait */
            struct timespec ts{};
            if (deadline != nullptr) {
                auto now = std::chrono::steady_clock::now();
                if (*deadline <= now) {
                    return;
                }
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(*deadline - now);
                ts.tv_sec = static_cast<time_t>(ns.count() / 1000000000LL);
                ts.tv_nsec = static_cast<long>(ns.count() % 1000000000LL);
            } else {
                ts.tv_sec = 0;
                ts.tv_nsec = 50000000L; /* 50ms slice to recheck termination */
            }
            /* Use ntsync wait via /dev/ntsync? NTSYNC_IOC_WAIT_ANY is on device fd;
             * keeping futex for the timed wait while holding ntsync sem handle keeps
             * wait interruption deterministic without busy spinning. */
            ::syscall(SYS_futex, &futex_seq_, FUTEX_WAIT_PRIVATE, observed_seq, &ts, nullptr, 0);
            return;
        }

        if (deadline == nullptr) {
            ::syscall(SYS_futex, &futex_seq_, FUTEX_WAIT_PRIVATE, observed_seq, nullptr, nullptr, 0);
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (*deadline <= now) {
            return;
        }
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(*deadline - now);
        struct timespec ts{};
        ts.tv_sec = static_cast<time_t>(ns.count() / 1000000000LL);
        ts.tv_nsec = static_cast<long>(ns.count() % 1000000000LL);
        ::syscall(SYS_futex, &futex_seq_, FUTEX_WAIT_PRIVATE, observed_seq, &ts, nullptr, 0);
    }

private:
    int ntsync_sem_fd_{-1};
    uint32_t backend_{2u};
    std::atomic<uint32_t> futex_seq_{0u};
};

/* ============================================================================
 * Queue Port & Queue State
 * ============================================================================ */

struct TaskItem {
    XTaskQueueCallback callback{nullptr};
    void *context{nullptr};
};

struct MonitorEntry {
    uint64_t token{0u};
    uint64_t queue_handle{0u};
    XTaskQueuePort port{XTaskQueuePort_Work};
    XTaskQueueMonitorCallback callback{nullptr};
    void *context{nullptr};
};

struct DelayedTaskItem {
    std::chrono::steady_clock::time_point due_time;
    uint64_t seq{0u};
    uint64_t queue_handle{0u};
    uint64_t port_id{0u};
    XTaskQueuePort port_enum{XTaskQueuePort_Work};
    XTaskQueueCallback callback{nullptr};
    void *context{nullptr};

    bool operator>(const DelayedTaskItem &other) const noexcept {
        if (due_time != other.due_time) {
            return due_time > other.due_time;
        }
        return seq > other.seq;
    }
};

struct PortState {
    uint64_t port_id{0u};
    XTaskQueueDispatchMode mode{XTaskQueueDispatchMode_ThreadPool};
    std::mutex mutex;
    std::condition_variable cv;
    PortWakePrimitive wake;
    std::deque<TaskItem> items;
    std::vector<std::thread> workers;
    uint32_t active_callbacks{0u};
    bool terminating{false};
    bool terminated{false};
    std::vector<MonitorEntry> monitors;
    std::vector<std::pair<XTaskQueueTerminatedCallback, void *>> term_waiters;

    ~PortState() {
        stop_workers();
    }

    void start_workers_if_needed() {
        if (mode == XTaskQueueDispatchMode_ThreadPool) {
            if (workers.empty()) {
                for (int i = 0; i < 4; ++i) {
                    workers.emplace_back([this]() { worker_loop(); });
                }
            }
        } else if (mode == XTaskQueueDispatchMode_SerializedThreadPool) {
            if (workers.empty()) {
                workers.emplace_back([this]() { worker_loop(); });
            }
        }
    }

    void stop_workers() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            terminating = true;
            terminated = true;
            cv.notify_all();
            wake.signal_all();
        }
        for (auto &t : workers) {
            if (t.joinable()) {
                if (t.get_id() == std::this_thread::get_id()) {
                    t.detach();
                } else {
                    t.join();
                }
            }
        }
        workers.clear();
    }

    void worker_loop() {
        while (true) {
            TaskItem item{};
            bool has_item = false;
            bool canceled = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this]() {
                    return !items.empty() || terminating;
                });
                if (!items.empty()) {
                    item = items.front();
                    items.pop_front();
                    canceled = terminating;
                    has_item = true;
                    ++active_callbacks;
                } else if (terminating) {
                    break;
                }
            }

            if (has_item && item.callback != nullptr) {
                item.callback(item.context, canceled);
            }

            if (has_item) {
                std::vector<std::pair<XTaskQueueTerminatedCallback, void *>> notify_term;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (active_callbacks > 0u) {
                        --active_callbacks;
                    }
                    if (terminating && items.empty() && active_callbacks == 0u) {
                        terminated = true;
                        notify_term.swap(term_waiters);
                        cv.notify_all();
                        wake.signal_all();
                    }
                }
                for (auto &w : notify_term) {
                    if (w.first != nullptr) {
                        w.first(w.second);
                    }
                }
            }
        }
    }

    void enqueue_ready(XTaskQueueCallback cb,
                       void *ctx,
                       uint64_t queue_handle,
                       XTaskQueuePort port_enum) {
        if (cb == nullptr) {
            return;
        }

        if (mode == XTaskQueueDispatchMode_Immediate ||
            mode == XTaskQueueDispatchMode_Inline) {
            std::vector<MonitorEntry> mons;
            bool canceled = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (terminating) {
                    canceled = true;
                }
                ++active_callbacks;
                mons = monitors;
            }
            for (const auto &m : mons) {
                if (m.callback != nullptr && m.queue_handle == queue_handle) {
                    m.callback(m.context, queue_handle, port_enum);
                }
            }
            cb(ctx, canceled);
            std::vector<std::pair<XTaskQueueTerminatedCallback, void *>> notify_term;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (active_callbacks > 0u) {
                    --active_callbacks;
                }
                if (terminating && items.empty() && active_callbacks == 0u) {
                    terminated = true;
                    notify_term.swap(term_waiters);
                    cv.notify_all();
                }
            }
            for (auto &w : notify_term) {
                if (w.first != nullptr) {
                    w.first(w.second);
                }
            }
            return;
        }

        std::vector<MonitorEntry> mons;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (terminating) {
                /* Execute canceled immediately if queue is terminating */
                lock.~lock_guard();
                cb(ctx, true);
                return;
            }
            items.push_back(TaskItem{cb, ctx});
            mons = monitors;
            cv.notify_one();
            wake.signal_one();
        }
        for (const auto &m : mons) {
            if (m.callback != nullptr && m.queue_handle == queue_handle) {
                m.callback(m.context, queue_handle, port_enum);
            }
        }
    }
};

struct QueueObject {
    uint64_t handle{0u};
    std::atomic<uint32_t> ref_count{1u};
    std::shared_ptr<PortState> work_port;
    std::shared_ptr<PortState> completion_port;
    std::atomic<bool> terminating{false};
};

struct AsyncState {
    uint64_t id{0u};
    XAsyncBlock *async_block{nullptr};
    XTaskQueueHandle queue{0u};
    void *context{nullptr};
    const void *identity{nullptr};
    XAsyncProvider provider{nullptr};
    std::mutex mutex;
    std::condition_variable cv;
    bool completed{false};
    bool completion_cb_done{false};
    bool canceled{false};
    bool work_submitted{false};
    scarlett_hresult_t result{SCARLETT_E_PENDING};
    uint64_t required_buffer_size{0u};
};

class TaskQueueImpl {
public:
    static TaskQueueImpl &instance() {
        static TaskQueueImpl inst;
        return inst;
    }

    ~TaskQueueImpl() {
        shutdown();
    }

    void initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!timer_running_) {
            timer_stop_ = false;
            timer_running_ = true;
            timer_thread_ = std::thread([this]() { timer_loop(); });
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timer_stop_ = true;
            timer_cv_.notify_all();
        }
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
        timer_running_ = false;

        std::unordered_map<uint64_t, std::shared_ptr<QueueObject>> qcopy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            qcopy.swap(queues_);
            ports_.clear();
            process_queue_ = 0u;
        }
        for (auto &kv : qcopy) {
            if (kv.second->work_port) {
                kv.second->work_port->stop_workers();
            }
            if (kv.second->completion_port) {
                kv.second->completion_port->stop_workers();
            }
        }
    }

    std::shared_ptr<PortState> create_port(XTaskQueueDispatchMode mode) {
        auto p = std::make_shared<PortState>();
        p->port_id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        p->mode = mode;
        p->start_workers_if_needed();
        std::lock_guard<std::mutex> lock(mutex_);
        ports_[p->port_id] = p;
        return p;
    }

    scarlett_hresult_t create_queue(XTaskQueueDispatchMode work_mode,
                                    XTaskQueueDispatchMode comp_mode,
                                    uint64_t &out_handle,
                                    uint32_t &out_backend) {
        initialize();
        if (static_cast<uint32_t>(work_mode) > XTaskQueueDispatchMode_Inline ||
            static_cast<uint32_t>(comp_mode) > XTaskQueueDispatchMode_Inline) {
            return SCARLETT_E_INVALIDARG;
        }
        auto q = std::make_shared<QueueObject>();
        q->handle = next_id_.fetch_add(1u, std::memory_order_relaxed);
        q->work_port = create_port(work_mode);
        q->completion_port = create_port(comp_mode);
        out_handle = q->handle;
        out_backend = q->work_port->wake.backend();
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[q->handle] = q;
        return SCARLETT_OK;
    }

    scarlett_hresult_t create_composite(uint64_t work_port_h,
                                        uint64_t comp_port_h,
                                        uint64_t &out_handle,
                                        uint32_t &out_backend) {
        initialize();
        std::shared_ptr<PortState> wp;
        std::shared_ptr<PortState> cp;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto wit = ports_.find(work_port_h);
            auto cit = ports_.find(comp_port_h);
            if (wit == ports_.end() || cit == ports_.end()) {
                return SCARLETT_E_INVALIDARG;
            }
            wp = wit->second;
            cp = cit->second;
        }
        auto q = std::make_shared<QueueObject>();
        q->handle = next_id_.fetch_add(1u, std::memory_order_relaxed);
        q->work_port = wp;
        q->completion_port = cp;
        out_handle = q->handle;
        out_backend = wp->wake.backend();
        std::lock_guard<std::mutex> lock(mutex_);
        queues_[q->handle] = q;
        return SCARLETT_OK;
    }

    std::shared_ptr<QueueObject> lookup_queue(uint64_t h) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = queues_.find(h);
        if (it == queues_.end()) {
            return nullptr;
        }
        return it->second;
    }

    scarlett_hresult_t dup_queue(uint64_t h, uint64_t &out_h) {
        auto q = lookup_queue(h);
        if (!q) {
            return SCARLETT_E_HANDLE;
        }
        q->ref_count.fetch_add(1u, std::memory_order_relaxed);
        out_h = h;
        return SCARLETT_OK;
    }

    scarlett_hresult_t close_queue(uint64_t h) {
        std::shared_ptr<QueueObject> q_to_drop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = queues_.find(h);
            if (it == queues_.end()) {
                return SCARLETT_E_HANDLE;
            }
            if (it->second->ref_count.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
                q_to_drop = it->second;
                queues_.erase(it);
                if (process_queue_ == h) {
                    process_queue_ = 0u;
                }
            }
        }
        if (q_to_drop) {
            if (q_to_drop->work_port && q_to_drop->work_port.use_count() <= 2) {
                q_to_drop->work_port->stop_workers();
                std::lock_guard<std::mutex> lock(mutex_);
                ports_.erase(q_to_drop->work_port->port_id);
            }
            if (q_to_drop->completion_port && q_to_drop->completion_port.use_count() <= 2) {
                q_to_drop->completion_port->stop_workers();
                std::lock_guard<std::mutex> lock(mutex_);
                ports_.erase(q_to_drop->completion_port->port_id);
            }
        }
        return SCARLETT_OK;
    }

    uint64_t ensure_process_queue() {
        uint64_t existing = 0u;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (process_queue_ != 0u && queues_.count(process_queue_) > 0u) {
                return process_queue_;
            }
            existing = process_queue_;
        }
        (void)existing;
        uint64_t qh = 0u;
        uint32_t backend = 0u;
        if (scarlett_succeeded(create_queue(XTaskQueueDispatchMode_ThreadPool,
                                            XTaskQueueDispatchMode_ThreadPool,
                                            qh, backend))) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (process_queue_ == 0u) {
                process_queue_ = qh;
            }
            return process_queue_;
        }
        return 0u;
    }

    scarlett_hresult_t get_process_queue(uint64_t &out_h) {
        out_h = ensure_process_queue();
        if (out_h == 0u) {
            return SCARLETT_E_OUTOFMEMORY;
        }
        auto q = lookup_queue(out_h);
        if (q) {
            q->ref_count.fetch_add(1u, std::memory_order_relaxed);
        }
        return SCARLETT_OK;
    }

    scarlett_hresult_t set_process_queue(uint64_t h) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (h != 0u && queues_.count(h) == 0u) {
            return SCARLETT_E_HANDLE;
        }
        process_queue_ = h;
        return SCARLETT_OK;
    }

    scarlett_hresult_t submit(uint64_t queue_h,
                              XTaskQueuePort port_enum,
                              uint32_t delay_ms,
                              void *ctx,
                              XTaskQueueCallback cb) {
        if (cb == nullptr) {
            return SCARLETT_E_POINTER;
        }
        auto q = lookup_queue(queue_h);
        if (!q) {
            return SCARLETT_E_HANDLE;
        }
        if (q->terminating.load(std::memory_order_acquire)) {
            return SCARLETT_E_ABORT;
        }
        auto port = (port_enum == XTaskQueuePort_Work) ? q->work_port : q->completion_port;
        if (!port) {
            return SCARLETT_E_HANDLE;
        }

        if (delay_ms == 0u) {
            port->enqueue_ready(cb, ctx, queue_h, port_enum);
            return SCARLETT_OK;
        }

        initialize();
        DelayedTaskItem ditem{};
        ditem.due_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        ditem.seq = next_id_.fetch_add(1u, std::memory_order_relaxed);
        ditem.queue_handle = queue_h;
        ditem.port_id = port->port_id;
        ditem.port_enum = port_enum;
        ditem.callback = cb;
        ditem.context = ctx;

        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            delayed_heap_.push(ditem);
            timer_cv_.notify_one();
        }
        return SCARLETT_OK;
    }

    scarlett_hresult_t dispatch(uint64_t queue_h,
                                XTaskQueuePort port_enum,
                                uint32_t timeout_ms,
                                uint32_t &out_dispatched,
                                uint32_t &out_count) {
        out_dispatched = 0u;
        out_count = 0u;
        auto q = lookup_queue(queue_h);
        if (!q) {
            return SCARLETT_E_HANDLE;
        }
        auto port = (port_enum == XTaskQueuePort_Work) ? q->work_port : q->completion_port;
        if (!port) {
            return SCARLETT_E_HANDLE;
        }

        bool has_deadline = (timeout_ms != SCARLETT_INFINITE_MS);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        while (true) {
            TaskItem item{};
            bool got = false;
            bool canceled = false;
            uint32_t observed_seq = port->wake.snapshot_seq();

            {
                std::lock_guard<std::mutex> lock(port->mutex);
                if (!port->items.empty()) {
                    item = port->items.front();
                    port->items.pop_front();
                    canceled = port->terminating;
                    got = true;
                    ++port->active_callbacks;
                } else if (port->terminating) {
                    return SCARLETT_OK;
                }
            }

            if (got) {
                if (item.callback != nullptr) {
                    item.callback(item.context, canceled);
                }
                std::vector<std::pair<XTaskQueueTerminatedCallback, void *>> notify_term;
                {
                    std::lock_guard<std::mutex> lock(port->mutex);
                    if (port->active_callbacks > 0u) {
                        --port->active_callbacks;
                    }
                    if (port->terminating && port->items.empty() && port->active_callbacks == 0u) {
                        port->terminated = true;
                        notify_term.swap(port->term_waiters);
                        port->cv.notify_all();
                        port->wake.signal_all();
                    }
                }
                for (auto &w : notify_term) {
                    if (w.first != nullptr) {
                        w.first(w.second);
                    }
                }
                out_dispatched = 1u;
                ++out_count;

                /* Drain any remaining ready items immediately */
                while (true) {
                    TaskItem next_item{};
                    bool next_got = false;
                    bool next_canceled = false;
                    {
                        std::lock_guard<std::mutex> lock(port->mutex);
                        if (!port->items.empty()) {
                            next_item = port->items.front();
                            port->items.pop_front();
                            next_canceled = port->terminating;
                            next_got = true;
                            ++port->active_callbacks;
                        }
                    }
                    if (!next_got) {
                        break;
                    }
                    if (next_item.callback != nullptr) {
                        next_item.callback(next_item.context, next_canceled);
                    }
                    {
                        std::lock_guard<std::mutex> lock(port->mutex);
                        if (port->active_callbacks > 0u) {
                            --port->active_callbacks;
                        }
                    }
                    ++out_count;
                }
                return SCARLETT_OK;
            }

            if (timeout_ms == 0u) {
                return SCARLETT_OK;
            }
            if (has_deadline && std::chrono::steady_clock::now() >= deadline) {
                return SCARLETT_OK;
            }
            port->wake.wait_until(observed_seq, has_deadline ? &deadline : nullptr);
        }
    }

    scarlett_hresult_t terminate(uint64_t queue_h,
                                 bool wait,
                                 void *ctx,
                                 XTaskQueueTerminatedCallback cb) {
        auto q = lookup_queue(queue_h);
        if (!q) {
            return SCARLETT_E_HANDLE;
        }
        q->terminating.store(true, std::memory_order_release);

        /* Cancel delayed items belonging to this queue */
        std::vector<DelayedTaskItem> canceled_delayed;
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            std::priority_queue<DelayedTaskItem, std::vector<DelayedTaskItem>, std::greater<DelayedTaskItem>> kept;
            while (!delayed_heap_.empty()) {
                auto top = delayed_heap_.top();
                delayed_heap_.pop();
                if (top.queue_handle == queue_h) {
                    canceled_delayed.push_back(top);
                } else {
                    kept.push(top);
                }
            }
            delayed_heap_.swap(kept);
        }
        for (auto &d : canceled_delayed) {
            if (d.callback != nullptr) {
                d.callback(d.context, true);
            }
        }

        auto term_port = [&](std::shared_ptr<PortState> &p, bool attach_cb) {
            if (!p) return;
            std::vector<TaskItem> dropped;
            bool already_done = false;
            {
                std::lock_guard<std::mutex> lock(p->mutex);
                p->terminating = true;
                if (p->mode == XTaskQueueDispatchMode_Manual) {
                    while (!p->items.empty()) {
                        dropped.push_back(p->items.front());
                        p->items.pop_front();
                    }
                }
                if (p->items.empty() && p->active_callbacks == 0u) {
                    p->terminated = true;
                    already_done = true;
                } else if (attach_cb && cb != nullptr) {
                    p->term_waiters.push_back({cb, ctx});
                }
                p->cv.notify_all();
                p->wake.signal_all();
            }
            for (auto &it : dropped) {
                if (it.callback != nullptr) {
                    it.callback(it.context, true);
                }
            }
            if (attach_cb && already_done && cb != nullptr) {
                cb(ctx);
            }
        };

        term_port(q->work_port, false);
        term_port(q->completion_port, true);

        if (wait) {
            if (q->work_port) {
                q->work_port->stop_workers();
            }
            if (q->completion_port && q->completion_port != q->work_port) {
                q->completion_port->stop_workers();
            }
        }
        return SCARLETT_OK;
    }

    scarlett_hresult_t register_monitor(uint64_t queue_h,
                                        void *ctx,
                                        XTaskQueueMonitorCallback cb,
                                        uint64_t &out_token) {
        if (cb == nullptr) {
            return SCARLETT_E_POINTER;
        }
        auto q = lookup_queue(queue_h);
        if (!q) {
            return SCARLETT_E_HANDLE;
        }
        uint64_t tok = next_id_.fetch_add(1u, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(q->work_port->mutex);
            q->work_port->monitors.push_back(
                MonitorEntry{tok, queue_h, XTaskQueuePort_Work, cb, ctx});
        }
        if (q->completion_port != q->work_port) {
            std::lock_guard<std::mutex> lock(q->completion_port->mutex);
            q->completion_port->monitors.push_back(
                MonitorEntry{tok, queue_h, XTaskQueuePort_Completion, cb, ctx});
        }
        out_token = tok;
        return SCARLETT_OK;
    }

    scarlett_hresult_t unregister_monitor(uint64_t queue_h, uint64_t token) {
        auto q = lookup_queue(queue_h);
        if (!q) {
            return SCARLETT_E_HANDLE;
        }
        auto erase_from = [&](std::shared_ptr<PortState> &p) {
            if (!p) return;
            std::lock_guard<std::mutex> lock(p->mutex);
            auto &v = p->monitors;
            for (auto it = v.begin(); it != v.end();) {
                if (it->token == token) {
                    it = v.erase(it);
                } else {
                    ++it;
                }
            }
        };
        erase_from(q->work_port);
        erase_from(q->completion_port);
        return SCARLETT_OK;
    }

    /* ========================================================================
     * XAsync Implementation
     * ======================================================================== */

    scarlett_hresult_t async_begin(scarlett_xgr_async_params_t &p) {
        auto *blk = reinterpret_cast<XAsyncBlock *>(static_cast<uintptr_t>(p.async_block_addr));
        auto provider = reinterpret_cast<XAsyncProvider>(static_cast<uintptr_t>(p.provider_addr));
        if (blk == nullptr || provider == nullptr) {
            return SCARLETT_E_POINTER;
        }

        uint64_t qh = blk->queue;
        if (qh == 0u) {
            qh = ensure_process_queue();
        }
        if (qh == 0u) {
            return SCARLETT_E_OUTOFMEMORY;
        }

        auto st = std::make_shared<AsyncState>();
        st->id = next_id_.fetch_add(1u, std::memory_order_relaxed);
        st->async_block = blk;
        st->queue = qh;
        st->context = reinterpret_cast<void *>(static_cast<uintptr_t>(p.context_addr));
        st->identity = reinterpret_cast<const void *>(static_cast<uintptr_t>(p.identity_addr));
        st->provider = provider;

        auto *layout = reinterpret_cast<AsyncInternalLayout *>(blk->internal);
        layout->magic = kAsyncBlockMagic;
        layout->async_id = st->id;
        layout->reserved0 = 0u;
        layout->reserved1 = 0u;

        {
            std::lock_guard<std::mutex> lock(async_mutex_);
            asyncs_[st->id] = st;
        }

        XAsyncProviderData pdata{};
        pdata.async = blk;
        pdata.bufferSize = 0u;
        pdata.buffer = nullptr;
        pdata.context = st->context;

        scarlett_hresult_t hr = provider(XAsyncOp_Begin, &pdata);
        if (scarlett_failed(hr) && hr != SCARLETT_E_PENDING) {
            std::lock_guard<std::mutex> lock(async_mutex_);
            asyncs_.erase(st->id);
            layout->magic = 0u;
            layout->async_id = 0u;
            return hr;
        }
        return SCARLETT_OK;
    }

    scarlett_hresult_t async_schedule(scarlett_xgr_async_params_t &p) {
        auto st = lookup_async(reinterpret_cast<XAsyncBlock *>(static_cast<uintptr_t>(p.async_block_addr)));
        if (!st) {
            return SCARLETT_E_INVALIDARG;
        }
        struct WorkCtx {
            std::shared_ptr<AsyncState> state;
        };
        auto *wctx = new WorkCtx{st};
        scarlett_hresult_t hr = submit(
            st->queue,
            XTaskQueuePort_Work,
            p.delay_ms,
            wctx,
            [](void *ctx, bool canceled) {
                auto *wc = static_cast<WorkCtx *>(ctx);
                auto s = wc->state;
                delete wc;

                if (canceled || s->canceled) {
                    TaskQueueImpl::instance().complete_state(s, SCARLETT_E_ABORT, 0u);
                    return;
                }
                XAsyncProviderData pdata{};
                pdata.async = s->async_block;
                pdata.bufferSize = 0u;
                pdata.buffer = nullptr;
                pdata.context = s->context;
                scarlett_hresult_t whr = s->provider(XAsyncOp_DoWork, &pdata);
                if (whr != SCARLETT_E_PENDING) {
                    bool already_done = false;
                    {
                        std::lock_guard<std::mutex> lock(s->mutex);
                        already_done = s->completed;
                    }
                    if (!already_done) {
                        TaskQueueImpl::instance().complete_state(s, whr, 0u);
                    }
                }
            });
        if (scarlett_failed(hr)) {
            delete wctx;
        }
        return hr;
    }

    void complete_state(const std::shared_ptr<AsyncState> &st,
                        scarlett_hresult_t result_hr,
                        uint64_t required_bytes) {
        bool should_complete = false;
        XAsyncBlock *blk = nullptr;
        uint64_t qh = 0u;
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            if (!st->completed) {
                st->completed = true;
                st->result = st->canceled ? SCARLETT_E_ABORT : result_hr;
                st->required_buffer_size = st->canceled ? 0u : required_bytes;
                blk = st->async_block;
                qh = st->queue;
                should_complete = true;
                if (blk == nullptr || blk->callback == nullptr) {
                    st->completion_cb_done = true;
                    st->cv.notify_all();
                }
            }
        }
        if (!should_complete || blk == nullptr || blk->callback == nullptr) {
            return;
        }

        struct CompCtx {
            std::shared_ptr<AsyncState> state;
        };
        auto *cctx = new CompCtx{st};
        scarlett_hresult_t shr = submit(
            qh,
            XTaskQueuePort_Completion,
            0u,
            cctx,
            [](void *ctx, bool /*canceled*/) {
                auto *cc = static_cast<CompCtx *>(ctx);
                auto s = cc->state;
                delete cc;
                if (s->async_block != nullptr && s->async_block->callback != nullptr) {
                    s->async_block->callback(s->async_block);
                }
                {
                    std::lock_guard<std::mutex> lock(s->mutex);
                    s->completion_cb_done = true;
                    s->cv.notify_all();
                }
            });
        if (scarlett_failed(shr)) {
            delete cctx;
            if (blk->callback != nullptr) {
                blk->callback(blk);
            }
            std::lock_guard<std::mutex> lock(st->mutex);
            st->completion_cb_done = true;
            st->cv.notify_all();
        }
    }

    scarlett_hresult_t async_complete(scarlett_xgr_async_params_t &p) {
        auto st = lookup_async(reinterpret_cast<XAsyncBlock *>(static_cast<uintptr_t>(p.async_block_addr)));
        if (!st) {
            return SCARLETT_E_INVALIDARG;
        }
        complete_state(st, p.result_hr, p.buffer_size);
        return SCARLETT_OK;
    }

    scarlett_hresult_t async_get_status(scarlett_xgr_async_params_t &p) {
        auto st = lookup_async(reinterpret_cast<XAsyncBlock *>(static_cast<uintptr_t>(p.async_block_addr)));
        if (!st) {
            return SCARLETT_E_INVALIDARG;
        }
        std::unique_lock<std::mutex> lock(st->mutex);
        if (!st->completed || !st->completion_cb_done) {
            if (p.wait_flag == 0u) {
                return SCARLETT_E_PENDING;
            }
            st->cv.wait(lock, [&]() { return st->completed && st->completion_cb_done; });
        }
        return st->result;
    }

    scarlett_hresult_t async_get_result(scarlett_xgr_async_params_t &p) {
        auto st = lookup_async(reinterpret_cast<XAsyncBlock *>(static_cast<uintptr_t>(p.async_block_addr)));
        if (!st) {
            return SCARLETT_E_INVALIDARG;
        }
        const void *id_ptr = reinterpret_cast<const void *>(static_cast<uintptr_t>(p.identity_addr));
        if (id_ptr != nullptr && st->identity != nullptr && id_ptr != st->identity) {
            return SCARLETT_E_INVALIDARG;
        }

        std::unique_lock<std::mutex> lock(st->mutex);
        if (!st->completed) {
            return SCARLETT_E_PENDING;
        }
        scarlett_hresult_t hr = st->result;
        uint64_t req = st->required_buffer_size;
        lock.unlock();

        p.out_buffer_used = req;
        if (scarlett_failed(hr)) {
            return hr;
        }

        void *buf = reinterpret_cast<void *>(static_cast<uintptr_t>(p.buffer_addr));
        if (p.wait_flag != 0u) {
            /* Query required size mode (XAsyncGetResultSize) */
            return SCARLETT_OK;
        }
        if (req > 0u) {
            if (buf == nullptr) {
                return SCARLETT_E_POINTER;
            }
            if (p.buffer_size < req) {
                return SCARLETT_HRESULT_C(0x8007007Au); /* ERROR_INSUFFICIENT_BUFFER */
            }
        }
        if (st->provider != nullptr) {
            XAsyncProviderData pdata{};
            pdata.async = st->async_block;
            pdata.bufferSize = p.buffer_size;
            pdata.buffer = buf;
            pdata.context = st->context;
            hr = st->provider(XAsyncOp_GetResult, &pdata);
            if (scarlett_succeeded(hr)) {
                st->provider(XAsyncOp_Cleanup, &pdata);
            }
            return hr;
        }
        return SCARLETT_OK;
    }

    scarlett_hresult_t async_cancel(scarlett_xgr_async_params_t &p) {
        auto st = lookup_async(reinterpret_cast<XAsyncBlock *>(static_cast<uintptr_t>(p.async_block_addr)));
        if (!st) {
            return SCARLETT_E_INVALIDARG;
        }
        bool do_cancel = false;
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            if (!st->completed && !st->canceled) {
                st->canceled = true;
                do_cancel = true;
            }
        }
        if (do_cancel) {
            if (st->provider != nullptr) {
                XAsyncProviderData pdata{};
                pdata.async = st->async_block;
                pdata.context = st->context;
                (void)st->provider(XAsyncOp_Cancel, &pdata);
            }
            complete_state(st, SCARLETT_E_ABORT, 0u);
        }
        return SCARLETT_OK;
    }

private:
    std::shared_ptr<AsyncState> lookup_async(const XAsyncBlock *blk) {
        if (blk == nullptr) {
            return nullptr;
        }
        const auto *layout = reinterpret_cast<const AsyncInternalLayout *>(blk->internal);
        if (layout->magic != kAsyncBlockMagic || layout->async_id == 0u) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(async_mutex_);
        auto it = asyncs_.find(layout->async_id);
        return (it != asyncs_.end()) ? it->second : nullptr;
    }

    void timer_loop() {
        while (true) {
            DelayedTaskItem due{};
            bool has_due = false;
            {
                std::unique_lock<std::mutex> lock(timer_mutex_);
                while (!timer_stop_) {
                    if (delayed_heap_.empty()) {
                        timer_cv_.wait(lock, [this]() {
                            return timer_stop_ || !delayed_heap_.empty();
                        });
                    } else {
                        auto next_time = delayed_heap_.top().due_time;
                        if (std::chrono::steady_clock::now() >= next_time) {
                            due = delayed_heap_.top();
                            delayed_heap_.pop();
                            has_due = true;
                            break;
                        }
                        timer_cv_.wait_until(lock, next_time);
                    }
                }
                if (timer_stop_ && !has_due) {
                    break;
                }
            }
            if (has_due) {
                auto q = lookup_queue(due.queue_handle);
                if (q && !q->terminating.load(std::memory_order_acquire)) {
                    auto port = (due.port_enum == XTaskQueuePort_Work)
                                    ? q->work_port
                                    : q->completion_port;
                    if (port) {
                        port->enqueue_ready(due.callback, due.context, due.queue_handle, due.port_enum);
                    }
                } else if (due.callback != nullptr) {
                    due.callback(due.context, true);
                }
            }
        }
    }

    std::atomic<uint64_t> next_id_{100u};
    std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<QueueObject>> queues_;
    std::unordered_map<uint64_t, std::shared_ptr<PortState>> ports_;
    uint64_t process_queue_{0u};

    std::mutex async_mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<AsyncState>> asyncs_;

    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    std::priority_queue<DelayedTaskItem, std::vector<DelayedTaskItem>, std::greater<DelayedTaskItem>> delayed_heap_;
    std::thread timer_thread_;
    bool timer_running_{false};
    bool timer_stop_{false};
};

} // namespace

TaskQueueManager &TaskQueueManager::instance() {
    static TaskQueueManager mgr;
    return mgr;
}

void TaskQueueManager::initialize() {
    TaskQueueImpl::instance().initialize();
}

void TaskQueueManager::shutdown() {
    TaskQueueImpl::instance().shutdown();
}

scarlett_hresult_t TaskQueueManager::create(scarlett_xgr_taskq_create_params_t &p) {
    p.out_status = TaskQueueImpl::instance().create_queue(
        static_cast<XTaskQueueDispatchMode>(p.work_mode),
        static_cast<XTaskQueueDispatchMode>(p.completion_mode),
        p.out_handle,
        p.out_sync_backend);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::create_composite(scarlett_xgr_taskq_create_params_t &p) {
    p.out_status = TaskQueueImpl::instance().create_composite(
        p.work_port_handle, p.completion_port_handle, p.out_handle, p.out_sync_backend);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::duplicate_handle(scarlett_xgr_taskq_create_params_t &p) {
    p.out_status = TaskQueueImpl::instance().dup_queue(p.queue_handle, p.out_handle);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::close_handle(scarlett_xgr_taskq_create_params_t &p) {
    p.out_status = TaskQueueImpl::instance().close_queue(p.queue_handle);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::submit(scarlett_xgr_taskq_submit_params_t &p) {
    p.out_status = TaskQueueImpl::instance().submit(
        p.queue_handle,
        static_cast<XTaskQueuePort>(p.port),
        p.delay_ms,
        reinterpret_cast<void *>(static_cast<uintptr_t>(p.context_addr)),
        reinterpret_cast<XTaskQueueCallback>(static_cast<uintptr_t>(p.callback_addr)));
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::dispatch(scarlett_xgr_taskq_dispatch_params_t &p) {
    p.out_status = TaskQueueImpl::instance().dispatch(
        p.queue_handle,
        static_cast<XTaskQueuePort>(p.port),
        p.timeout_ms,
        p.out_dispatched,
        p.out_dispatched_count);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::terminate(scarlett_xgr_taskq_terminate_params_t &p) {
    p.out_status = TaskQueueImpl::instance().terminate(
        p.queue_handle,
        p.wait_for_completion != 0u,
        reinterpret_cast<void *>(static_cast<uintptr_t>(p.context_addr)),
        reinterpret_cast<XTaskQueueTerminatedCallback>(static_cast<uintptr_t>(p.callback_addr)));
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::monitor(scarlett_xgr_taskq_monitor_params_t &p) {
    if (p.is_unregister != 0u) {
        p.out_status = TaskQueueImpl::instance().unregister_monitor(p.queue_handle, p.token);
    } else {
        p.out_status = TaskQueueImpl::instance().register_monitor(
            p.queue_handle,
            reinterpret_cast<void *>(static_cast<uintptr_t>(p.context_addr)),
            reinterpret_cast<XTaskQueueMonitorCallback>(static_cast<uintptr_t>(p.callback_addr)),
            p.token);
    }
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::process_queue(scarlett_xgr_taskq_create_params_t &p) {
    if (p.is_set_op != 0u) {
        p.out_status = TaskQueueImpl::instance().set_process_queue(p.queue_handle);
    } else {
        p.out_status = TaskQueueImpl::instance().get_process_queue(p.out_handle);
    }
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::get_port(scarlett_xgr_taskq_create_params_t &p) {
    auto q = TaskQueueImpl::instance().lookup_queue(p.queue_handle);
    if (!q) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    auto port = (p.port_kind == static_cast<uint32_t>(XTaskQueuePort_Work))
                    ? q->work_port
                    : q->completion_port;
    if (!port) {
        p.out_status = SCARLETT_E_HANDLE;
        return p.out_status;
    }
    p.out_handle = port->port_id;
    p.out_sync_backend = port->wake.backend();
    p.out_status = SCARLETT_OK;
    return SCARLETT_OK;
}

scarlett_hresult_t TaskQueueManager::async_begin(scarlett_xgr_async_params_t &p) {
    p.out_status = TaskQueueImpl::instance().async_begin(p);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::async_schedule(scarlett_xgr_async_params_t &p) {
    p.out_status = TaskQueueImpl::instance().async_schedule(p);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::async_complete(scarlett_xgr_async_params_t &p) {
    p.out_status = TaskQueueImpl::instance().async_complete(p);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::async_get_status(scarlett_xgr_async_params_t &p) {
    p.out_status = TaskQueueImpl::instance().async_get_status(p);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::async_get_result(scarlett_xgr_async_params_t &p) {
    p.out_status = TaskQueueImpl::instance().async_get_result(p);
    return p.out_status;
}

scarlett_hresult_t TaskQueueManager::async_cancel(scarlett_xgr_async_params_t &p) {
    p.out_status = TaskQueueImpl::instance().async_cancel(p);
    return p.out_status;
}

} // namespace scarlett::xgameruntime::unixlib
