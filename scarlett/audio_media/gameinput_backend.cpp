#include "gameinput_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace scarlett::audio_media {

namespace {

uint8_t float_to_u8_100(float v) noexcept {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(clamped * 100.0f));
}

uint16_t float_to_u16(float v) noexcept {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint16_t>(std::lround(clamped * 65535.0f));
}

bool try_send_hidraw_xbox_gip(const uint8_t* report, size_t len) noexcept {
    DIR* dir = opendir("/dev");
    if (dir == nullptr) {
        return false;
    }
    bool sent = false;
    while (struct dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "hidraw", 6) != 0) {
            continue;
        }
        char path[300];
        std::snprintf(path, sizeof(path), "/dev/%s", ent->d_name);
        const int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        hidraw_devinfo info{};
        if (ioctl(fd, HIDIOCGRAWINFO, &info) >= 0 && info.vendor == 0x045e) {
            const ssize_t wr = write(fd, report, len);
            if (wr == static_cast<ssize_t>(len)) {
                sent = true;
                close(fd);
                break;
            }
        }
        close(fd);
    }
    closedir(dir);
    return sent;
}

bool try_send_evdev_ff_rumble(uint16_t strong_mag,
                              uint16_t weak_mag,
                              uint16_t duration_ms) noexcept {
    DIR* dir = opendir("/dev/input");
    if (dir == nullptr) {
        return false;
    }
    bool sent = false;
    while (struct dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        char path[300];
        std::snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        const int fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        ff_effect eff{};
        eff.type = FF_RUMBLE;
        eff.id = -1;
        eff.replay.length = duration_ms == 0u ? 1000u : duration_ms;
        eff.replay.delay = 0;
        eff.u.rumble.strong_magnitude = strong_mag;
        eff.u.rumble.weak_magnitude = weak_mag;
        if (ioctl(fd, EVIOCSFF, &eff) >= 0) {
            input_event play{};
            play.type = EV_FF;
            play.code = static_cast<uint16_t>(eff.id);
            play.value = 1;
            if (write(fd, &play, sizeof(play)) == static_cast<ssize_t>(sizeof(play))) {
                sent = true;
                close(fd);
                break;
            }
        }
        close(fd);
    }
    closedir(dir);
    return sent;
}

} // namespace

uint64_t GameInputBackend::current_timestamp_us() const noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

scarlett_hresult_t GameInputBackend::create_instance(uint64_t& out_handle) noexcept {
    auto inst = std::make_unique<Instance>();
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t h = next_handle_++;
    inst->handle = h;
    out_handle = h;
    instances_.emplace(h, std::move(inst));
    return SCARLETT_OK;
}

scarlett_hresult_t GameInputBackend::destroy_instance(uint64_t handle) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (instances_.erase(handle) == 0u) {
        return SCARLETT_E_HANDLE;
    }
    return SCARLETT_OK;
}

bool GameInputBackend::matches_filter(const scarlett_input_reading_record_t& rec,
                                      uint32_t input_kind,
                                      uint64_t device_id) noexcept {
    if (input_kind != SCARLETT_GAMEINPUT_KIND_UNKNOWN &&
        (rec.input_kind & input_kind) == 0u) {
        return false;
    }
    if (device_id != 0u && rec.device_id != device_id) {
        return false;
    }
    return true;
}

bool GameInputBackend::read_slot_consistent(const RingSlot& slot,
                                            scarlett_input_reading_record_t& out_rec) noexcept {
    for (int retry = 0; retry < 8; ++retry) {
        const uint64_t g0 = slot.gen.load(std::memory_order_acquire);
        if ((g0 & 1u) != 0u || g0 == 0u) {
            continue;
        }
        const scarlett_input_reading_record_t copy = slot.data;
        const uint64_t g1 = slot.gen.load(std::memory_order_acquire);
        if (g0 == g1) {
            out_rec = copy;
            return true;
        }
    }
    return false;
}

scarlett_hresult_t GameInputBackend::push_reading(
    uint64_t handle,
    const scarlett_input_reading_record_t& rec) noexcept {
    Instance* inst = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(handle);
        if (it == instances_.end()) {
            return SCARLETT_E_HANDLE;
        }
        inst = it->second.get();
    }

    const uint64_t seq = inst->write_seq.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    RingSlot& slot = inst->ring[seq % kRingCapacity];

    const uint64_t g = slot.gen.load(std::memory_order_relaxed);
    slot.gen.store(g + 1u, std::memory_order_release); /* odd: write in progress */

    scarlett_input_reading_record_t stamped = rec;
    stamped.sequence_number = seq;
    if (stamped.timestamp_us == 0u) {
        stamped.timestamp_us = current_timestamp_us();
    }
    slot.data = stamped;

    slot.gen.store(g + 2u, std::memory_order_release); /* even: stable */
    return SCARLETT_OK;
}

scarlett_hresult_t GameInputBackend::get_current_reading(
    uint64_t handle,
    uint32_t input_kind,
    uint64_t device_id,
    scarlett_input_reading_record_t& out_rec) noexcept {
    Instance* inst = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(handle);
        if (it == instances_.end()) {
            return SCARLETT_E_HANDLE;
        }
        inst = it->second.get();
    }

    const uint64_t head = inst->write_seq.load(std::memory_order_acquire);
    if (head == 0u) {
        return SCARLETT_E_FAIL;
    }
    const uint64_t min_seq = (head > kRingCapacity) ? (head - kRingCapacity + 1u) : 1u;

    for (uint64_t seq = head; seq >= min_seq; --seq) {
        scarlett_input_reading_record_t candidate{};
        if (read_slot_consistent(inst->ring[seq % kRingCapacity], candidate) &&
            candidate.sequence_number == seq &&
            matches_filter(candidate, input_kind, device_id)) {
            out_rec = candidate;
            return SCARLETT_OK;
        }
        if (seq == min_seq) {
            break;
        }
    }
    return SCARLETT_E_FAIL;
}

scarlett_hresult_t GameInputBackend::get_next_reading(
    uint64_t handle,
    uint64_t ref_sequence,
    uint32_t input_kind,
    uint64_t device_id,
    scarlett_input_reading_record_t& out_rec) noexcept {
    Instance* inst = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(handle);
        if (it == instances_.end()) {
            return SCARLETT_E_HANDLE;
        }
        inst = it->second.get();
    }

    const uint64_t head = inst->write_seq.load(std::memory_order_acquire);
    if (ref_sequence >= head) {
        return SCARLETT_E_FAIL;
    }
    const uint64_t min_seq = (head > kRingCapacity) ? (head - kRingCapacity + 1u) : 1u;
    const uint64_t start_seq = std::max(ref_sequence + 1u, min_seq);

    for (uint64_t seq = start_seq; seq <= head; ++seq) {
        scarlett_input_reading_record_t candidate{};
        if (read_slot_consistent(inst->ring[seq % kRingCapacity], candidate) &&
            candidate.sequence_number == seq &&
            matches_filter(candidate, input_kind, device_id)) {
            out_rec = candidate;
            return SCARLETT_OK;
        }
    }
    return SCARLETT_E_FAIL;
}

scarlett_hresult_t GameInputBackend::get_previous_reading(
    uint64_t handle,
    uint64_t ref_sequence,
    uint32_t input_kind,
    uint64_t device_id,
    scarlett_input_reading_record_t& out_rec) noexcept {
    if (ref_sequence <= 1u) {
        return SCARLETT_E_FAIL;
    }
    Instance* inst = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(handle);
        if (it == instances_.end()) {
            return SCARLETT_E_HANDLE;
        }
        inst = it->second.get();
    }

    const uint64_t head = inst->write_seq.load(std::memory_order_acquire);
    const uint64_t min_seq = (head > kRingCapacity) ? (head - kRingCapacity + 1u) : 1u;
    if (ref_sequence - 1u < min_seq) {
        return SCARLETT_E_FAIL;
    }

    for (uint64_t seq = ref_sequence - 1u; seq >= min_seq; --seq) {
        scarlett_input_reading_record_t candidate{};
        if (read_slot_consistent(inst->ring[seq % kRingCapacity], candidate) &&
            candidate.sequence_number == seq &&
            matches_filter(candidate, input_kind, device_id)) {
            out_rec = candidate;
            return SCARLETT_OK;
        }
        if (seq == min_seq) {
            break;
        }
    }
    return SCARLETT_E_FAIL;
}

scarlett_hresult_t GameInputBackend::set_rumble_state(
    const scarlett_input_rumble_params_t& req) noexcept {
    ScarlettRumbleTransportPacket pkt{};

    const uint8_t left_trig = float_to_u8_100(req.left_trigger);
    const uint8_t right_trig = float_to_u8_100(req.right_trigger);
    const uint8_t low_freq = float_to_u8_100(req.low_frequency);
    const uint8_t high_freq = float_to_u8_100(req.high_frequency);
    const uint8_t dur_10ms = req.duration_ms == 0u
                                 ? 0xFFu
                                 : static_cast<uint8_t>(std::min<uint32_t>(255u, (req.duration_ms + 9u) / 10u));
    const uint8_t seq = gip_seq_.fetch_add(1u, std::memory_order_relaxed);

    /* 1. Xbox GIP 0x09 Actuator Command (13 bytes, xone / USB GIP) */
    pkt.gip_packet_len = 13u;
    pkt.gip_packet[0]  = 0x09u; /* GIP_CMD_RUMBLE */
    pkt.gip_packet[1]  = 0x00u; /* internal flags */
    pkt.gip_packet[2]  = seq;
    pkt.gip_packet[3]  = 0x09u; /* payload length */
    pkt.gip_packet[4]  = 0x00u; /* sub-command: standard 4-motor actuator */
    pkt.gip_packet[5]  = 0x0Fu; /* motor enable mask: LT(8)|RT(4)|Strong(2)|Weak(1) */
    pkt.gip_packet[6]  = left_trig;
    pkt.gip_packet[7]  = right_trig;
    pkt.gip_packet[8]  = low_freq;
    pkt.gip_packet[9]  = high_freq;
    pkt.gip_packet[10] = dur_10ms;
    pkt.gip_packet[11] = 0x00u; /* off period */
    pkt.gip_packet[12] = 0x01u; /* repeat count */

    /* 2. Linux hidraw / xpadneo HID Output Report 0x03 (9 bytes) */
    pkt.hidraw_report_len = 9u;
    pkt.hidraw_report[0] = 0x03u; /* HID report ID */
    pkt.hidraw_report[1] = 0x0Fu; /* enable all 4 motors */
    pkt.hidraw_report[2] = left_trig;
    pkt.hidraw_report[3] = right_trig;
    pkt.hidraw_report[4] = low_freq;
    pkt.hidraw_report[5] = high_freq;
    pkt.hidraw_report[6] = dur_10ms;
    pkt.hidraw_report[7] = 0x00u;
    pkt.hidraw_report[8] = 0x01u;

    /* 3. Linux evdev FF_RUMBLE fallback (blends trigger impulse into weak motor) */
    pkt.evdev_ff_type = FF_RUMBLE;
    pkt.evdev_strong_mag = float_to_u16(req.low_frequency);
    const float trigger_avg = 0.5f * (std::clamp(req.left_trigger, 0.0f, 1.0f) +
                                      std::clamp(req.right_trigger, 0.0f, 1.0f));
    const float blended_weak = std::max(std::clamp(req.high_frequency, 0.0f, 1.0f), trigger_avg);
    pkt.evdev_weak_mag = float_to_u16(blended_weak);
    pkt.evdev_duration_ms = static_cast<uint16_t>(std::min<uint32_t>(req.duration_ms, 65535u));

    const uint32_t mode = transport_mode_.load(std::memory_order_acquire);
    pkt.transport_used = SCARLETT_INPUT_TRANSPORT_AUTO_MOCK;

    if (mode == SCARLETT_INPUT_TRANSPORT_HIDRAW_GIP) {
        if (try_send_hidraw_xbox_gip(pkt.hidraw_report, pkt.hidraw_report_len)) {
            pkt.transport_used = SCARLETT_INPUT_TRANSPORT_HIDRAW_GIP;
        } else {
            (void)try_send_evdev_ff_rumble(pkt.evdev_strong_mag, pkt.evdev_weak_mag, pkt.evdev_duration_ms);
            pkt.transport_used = SCARLETT_INPUT_TRANSPORT_EVDEV_FF;
        }
    } else if (mode == SCARLETT_INPUT_TRANSPORT_EVDEV_FF) {
        (void)try_send_evdev_ff_rumble(pkt.evdev_strong_mag, pkt.evdev_weak_mag, pkt.evdev_duration_ms);
        pkt.transport_used = SCARLETT_INPUT_TRANSPORT_EVDEV_FF;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_rumble_[req.device_id] = pkt;
    }
    return SCARLETT_OK;
}

scarlett_hresult_t GameInputBackend::query_last_rumble_packet(
    uint64_t device_id,
    ScarlettRumbleTransportPacket& out_pkt) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = last_rumble_.find(device_id);
    if (it == last_rumble_.end()) {
        return SCARLETT_E_FAIL;
    }
    out_pkt = it->second;
    return SCARLETT_OK;
}

scarlett_hresult_t GameInputBackend::set_transport_mode(uint32_t mode) noexcept {
    if (mode > SCARLETT_INPUT_TRANSPORT_EVDEV_FF) {
        return SCARLETT_E_INVALIDARG;
    }
    transport_mode_.store(mode, std::memory_order_release);
    return SCARLETT_OK;
}

namespace {

struct LiveInputState {
    std::mutex mutex;
    uint64_t last_poll_us = 0;

    // Evdev gamepad
    int evdev_fd = -1;
    uint64_t last_evdev_scan_us = 0;
    scarlett_gamepad_state_t evdev_gp{};
    int abs_x_min = -32768, abs_x_max = 32767;
    int abs_y_min = -32768, abs_y_max = 32767;
    int abs_rx_min = -32768, abs_rx_max = 32767;
    int abs_ry_min = -32768, abs_ry_max = 32767;
    int abs_lt_max = 255, abs_rt_max = 255;

    // X11 dlopen
    bool x11_tried = false;
    void* x11_lib = nullptr;
    void* x11_dpy = nullptr;
    unsigned long x11_root = 0;
    int (*fn_XQueryKeymap)(void*, char[32]) = nullptr;
    int (*fn_XQueryPointer)(void*, unsigned long, unsigned long*, unsigned long*,
                            int*, int*, int*, int*, unsigned int*) = nullptr;
    unsigned long (*fn_XKeycodeToKeysym)(void*, unsigned int, int) = nullptr;
    unsigned int (*fn_XKeysymToKeycode)(void*, unsigned long) = nullptr;
};

LiveInputState& live_state() noexcept {
    static LiveInputState* s = new LiveInputState();
    return *s;
}

float normalize_axis(int val, int mn, int mx) noexcept {
    if (mx <= mn) {
        return 0.0f;
    }
    const double mid = 0.5 * (static_cast<double>(mn) + static_cast<double>(mx));
    const double half = 0.5 * (static_cast<double>(mx) - static_cast<double>(mn));
    float f = static_cast<float>((static_cast<double>(val) - mid) / half);
    if (std::fabs(f) < 0.08f) {
        return 0.0f;
    }
    return std::clamp(f, -1.0f, 1.0f);
}

void try_open_evdev_gamepad(LiveInputState& st, uint64_t now_us) noexcept {
    if (st.evdev_fd >= 0 || (now_us - st.last_evdev_scan_us) < 2000000u) {
        return;
    }
    st.last_evdev_scan_us = now_us;
    DIR* dir = opendir("/dev/input");
    if (dir == nullptr) {
        return;
    }
    while (struct dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        char path[300];
        std::snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        const int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        unsigned long keybits[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))]{};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
            const auto has_key = [&](int code) {
                return (keybits[code / (8 * sizeof(unsigned long))] >>
                        (code % (8 * sizeof(unsigned long)))) & 1UL;
            };
            if (has_key(BTN_SOUTH) || has_key(BTN_GAMEPAD)) {
                input_absinfo ai{};
                if (ioctl(fd, EVIOCGABS(ABS_X), &ai) >= 0) { st.abs_x_min = ai.minimum; st.abs_x_max = ai.maximum; }
                if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) >= 0) { st.abs_y_min = ai.minimum; st.abs_y_max = ai.maximum; }
                if (ioctl(fd, EVIOCGABS(ABS_RX), &ai) >= 0) { st.abs_rx_min = ai.minimum; st.abs_rx_max = ai.maximum; }
                if (ioctl(fd, EVIOCGABS(ABS_RY), &ai) >= 0) { st.abs_ry_min = ai.minimum; st.abs_ry_max = ai.maximum; }
                if (ioctl(fd, EVIOCGABS(ABS_Z), &ai) >= 0 && ai.maximum > 0) { st.abs_lt_max = ai.maximum; }
                if (ioctl(fd, EVIOCGABS(ABS_RZ), &ai) >= 0 && ai.maximum > 0) { st.abs_rt_max = ai.maximum; }
                st.evdev_fd = fd;
                st.evdev_gp = {};
                break;
            }
        }
        close(fd);
    }
    closedir(dir);
}

void poll_evdev_gamepad(LiveInputState& st) noexcept {
    if (st.evdev_fd < 0) {
        return;
    }
    input_event ev{};
    while (true) {
        const ssize_t rd = read(st.evdev_fd, &ev, sizeof(ev));
        if (rd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close(st.evdev_fd);
                st.evdev_fd = -1;
                st.evdev_gp = {};
            }
            break;
        }
        if (rd != static_cast<ssize_t>(sizeof(ev))) {
            break;
        }
        if (ev.type == EV_KEY) {
            uint32_t bit = 0;
            switch (ev.code) {
                case BTN_SOUTH:  bit = 0x0004u; break; /* A */
                case BTN_EAST:   bit = 0x0008u; break; /* B */
                case BTN_WEST:   bit = 0x0010u; break; /* X */
                case BTN_NORTH:  bit = 0x0020u; break; /* Y */
                case BTN_TL:     bit = 0x0400u; break; /* LeftShoulder */
                case BTN_TR:     bit = 0x0800u; break; /* RightShoulder */
                case BTN_SELECT: bit = 0x0002u; break; /* View */
                case BTN_START:  bit = 0x0001u; break; /* Menu */
                case BTN_THUMBL: bit = 0x1000u; break; /* LeftThumbstick */
                case BTN_THUMBR: bit = 0x2000u; break; /* RightThumbstick */
                default: break;
            }
            if (bit != 0u) {
                if (ev.value != 0) st.evdev_gp.buttons |= bit;
                else st.evdev_gp.buttons &= ~bit;
            }
        } else if (ev.type == EV_ABS) {
            switch (ev.code) {
                case ABS_X:  st.evdev_gp.left_thumbstick_x  =  normalize_axis(ev.value, st.abs_x_min, st.abs_x_max); break;
                case ABS_Y:  st.evdev_gp.left_thumbstick_y  = -normalize_axis(ev.value, st.abs_y_min, st.abs_y_max); break;
                case ABS_RX: st.evdev_gp.right_thumbstick_x =  normalize_axis(ev.value, st.abs_rx_min, st.abs_rx_max); break;
                case ABS_RY: st.evdev_gp.right_thumbstick_y = -normalize_axis(ev.value, st.abs_ry_min, st.abs_ry_max); break;
                case ABS_Z:  st.evdev_gp.left_trigger  = std::clamp(static_cast<float>(ev.value) / st.abs_lt_max, 0.0f, 1.0f); break;
                case ABS_RZ: st.evdev_gp.right_trigger = std::clamp(static_cast<float>(ev.value) / st.abs_rt_max, 0.0f, 1.0f); break;
                case ABS_HAT0X:
                    st.evdev_gp.buttons &= ~(0x0100u | 0x0200u);
                    if (ev.value < 0) st.evdev_gp.buttons |= 0x0100u;
                    else if (ev.value > 0) st.evdev_gp.buttons |= 0x0200u;
                    break;
                case ABS_HAT0Y:
                    st.evdev_gp.buttons &= ~(0x0040u | 0x0080u);
                    if (ev.value < 0) st.evdev_gp.buttons |= 0x0040u;
                    else if (ev.value > 0) st.evdev_gp.buttons |= 0x0080u;
                    break;
                default: break;
            }
        }
    }
}

void init_x11_if_needed(LiveInputState& st) noexcept {
    if (st.x11_tried) {
        return;
    }
    st.x11_tried = true;
    if (std::getenv("DISPLAY") == nullptr) {
        return;
    }
    st.x11_lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (st.x11_lib == nullptr) {
        return;
    }
    using open_display_fn = void* (*)(const char*);
    using default_root_fn = unsigned long (*)(void*);
    auto fn_open = reinterpret_cast<open_display_fn>(dlsym(st.x11_lib, "XOpenDisplay"));
    auto fn_root = reinterpret_cast<default_root_fn>(dlsym(st.x11_lib, "XDefaultRootWindow"));
    st.fn_XQueryKeymap = reinterpret_cast<decltype(st.fn_XQueryKeymap)>(dlsym(st.x11_lib, "XQueryKeymap"));
    st.fn_XQueryPointer = reinterpret_cast<decltype(st.fn_XQueryPointer)>(dlsym(st.x11_lib, "XQueryPointer"));
    st.fn_XKeycodeToKeysym = reinterpret_cast<decltype(st.fn_XKeycodeToKeysym)>(dlsym(st.x11_lib, "XKeycodeToKeysym"));
    st.fn_XKeysymToKeycode = reinterpret_cast<decltype(st.fn_XKeysymToKeycode)>(dlsym(st.x11_lib, "XKeysymToKeycode"));
    if (fn_open == nullptr || fn_root == nullptr || st.fn_XQueryKeymap == nullptr) {
        return;
    }
    st.x11_dpy = fn_open(nullptr);
    if (st.x11_dpy != nullptr) {
        st.x11_root = fn_root(st.x11_dpy);
    }
}

} // namespace

void GameInputBackend::poll_live_input(uint64_t handle) noexcept {
    LiveInputState& st = live_state();
    scarlett_input_reading_record_t rec{};
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        const uint64_t now_us = current_timestamp_us();
        if (st.last_poll_us != 0u && (now_us - st.last_poll_us) < 2000u) {
            return;
        }
        st.last_poll_us = now_us;

        try_open_evdev_gamepad(st, now_us);
        poll_evdev_gamepad(st);
        init_x11_if_needed(st);

        rec.timestamp_us = now_us;
        rec.device_id = 1u;
        rec.input_kind = SCARLETT_GAMEINPUT_KIND_GAMEPAD |
                         SCARLETT_GAMEINPUT_KIND_KEYBOARD |
                         SCARLETT_GAMEINPUT_KIND_MOUSE;
        rec.gamepad = st.evdev_gp;

        if (st.x11_dpy != nullptr && st.fn_XQueryKeymap != nullptr && st.fn_XKeysymToKeycode != nullptr) {
            char keymap[32]{};
            st.fn_XQueryKeymap(st.x11_dpy, keymap);
            auto is_ks_down = [&](unsigned long ks) -> bool {
                const unsigned int kc = st.fn_XKeysymToKeycode(st.x11_dpy, ks);
                if (kc == 0u || kc >= 256u) return false;
                return (static_cast<uint8_t>(keymap[kc >> 3]) & (1u << (kc & 7u))) != 0u;
            };

            struct KeyBind { unsigned long ks; uint8_t vk; };
            static constexpr KeyBind kBinds[] = {
                {0x0077u, 'W'}, {0x0061u, 'A'}, {0x0073u, 'S'}, {0x0064u, 'D'},
                {0x0065u, 'E'}, {0x0066u, 'F'}, {0x0071u, 'Q'}, {0x0072u, 'R'},
                {0x006du, 'M'}, {0xff0du, 0x0Du}, {0x0020u, 0x20u}, {0xff1bu, 0x1Bu},
                {0xff08u, 0x08u}, {0xff09u, 0x09u}, {0xff51u, 0x25u}, {0xff52u, 0x26u},
                {0xff53u, 0x27u}, {0xff54u, 0x28u}, {0xffe1u, 0x10u}, {0xffe3u, 0x11u}
            };
            for (const auto& b : kBinds) {
                if (is_ks_down(b.ks) && rec.keyboard.key_count < 16u) {
                    rec.keyboard.virtual_keys[rec.keyboard.key_count++] = b.vk;
                }
            }

            if (st.fn_XQueryPointer != nullptr && st.x11_root != 0ul) {
                unsigned long r = 0, c = 0;
                int rx = 0, ry = 0, wx = 0, wy = 0;
                unsigned int mask = 0;
                if (st.fn_XQueryPointer(st.x11_dpy, st.x11_root, &r, &c, &rx, &ry, &wx, &wy, &mask) != 0) {
                    rec.mouse.position_x = rx;
                    rec.mouse.position_y = ry;
                    if ((mask & (1u << 8)) != 0u) rec.mouse.buttons |= 0x01u; /* Left */
                    if ((mask & (1u << 10)) != 0u) rec.mouse.buttons |= 0x02u; /* Right */
                    if ((mask & (1u << 9)) != 0u) rec.mouse.buttons |= 0x04u; /* Middle */
                }
            }

            // Virtual Xbox gamepad fallback from keyboard+mouse when no physical gamepad is connected
            if (st.evdev_fd < 0) {
                if (is_ks_down(0x0077u)) rec.gamepad.left_thumbstick_y += 1.0f;  /* W */
                if (is_ks_down(0x0073u)) rec.gamepad.left_thumbstick_y -= 1.0f;  /* S */
                if (is_ks_down(0x0061u)) rec.gamepad.left_thumbstick_x -= 1.0f;  /* A */
                if (is_ks_down(0x0064u)) rec.gamepad.left_thumbstick_x += 1.0f;  /* D */
                if (is_ks_down(0xff52u)) rec.gamepad.buttons |= 0x0040u;         /* Up -> DPadUp */
                if (is_ks_down(0xff54u)) rec.gamepad.buttons |= 0x0080u;         /* Down -> DPadDown */
                if (is_ks_down(0xff51u)) rec.gamepad.buttons |= 0x0100u;         /* Left -> DPadLeft */
                if (is_ks_down(0xff53u)) rec.gamepad.buttons |= 0x0200u;         /* Right -> DPadRight */
                if (is_ks_down(0xff0du) || is_ks_down(0x0020u)) rec.gamepad.buttons |= 0x0004u; /* Enter/Space -> A */
                if (is_ks_down(0xff08u) || is_ks_down(0xff1bu)) rec.gamepad.buttons |= 0x0008u; /* Backspace/Esc -> B */
                if (is_ks_down(0x0065u)) rec.gamepad.buttons |= 0x0010u;         /* E -> X */
                if (is_ks_down(0x0066u)) rec.gamepad.buttons |= 0x0020u;         /* F -> Y */
                if (is_ks_down(0xff09u)) rec.gamepad.buttons |= 0x0002u;         /* Tab -> View */
                if (is_ks_down(0x006du)) rec.gamepad.buttons |= 0x0001u;         /* M -> Menu */
                if (is_ks_down(0x0071u)) rec.gamepad.buttons |= 0x0400u;         /* Q -> LB */
                if (is_ks_down(0x0072u)) rec.gamepad.buttons |= 0x0800u;         /* R -> RB */
                if ((rec.mouse.buttons & 0x01u) != 0u) rec.gamepad.right_trigger = 1.0f;
                if ((rec.mouse.buttons & 0x02u) != 0u) rec.gamepad.left_trigger = 1.0f;
            }
        }
    }
    (void)push_reading(handle, rec);
}

} // namespace scarlett::audio_media
