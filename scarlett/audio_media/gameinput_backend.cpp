#include "gameinput_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dirent.h>
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

} // namespace scarlett::audio_media
