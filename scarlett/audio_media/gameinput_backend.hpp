#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "audio_media_unix_abi.h"

namespace scarlett::audio_media {

/*
 * Captured 4-motor rumble output packet for Linux hidraw (GIP/xpadneo)
 * and automatic evdev FF_RUMBLE fallback.
 */
struct ScarlettRumbleTransportPacket {
    uint32_t transport_used;     /* scarlett_input_transport_mode_t */
    uint32_t gip_packet_len;     /* 13 bytes Xbox GIP 0x09 command */
    uint8_t  gip_packet[16];
    uint32_t hidraw_report_len;  /* 9 bytes xpadneo HID report 0x03 */
    uint8_t  hidraw_report[16];
    uint16_t evdev_ff_type;      /* FF_RUMBLE (0x50) */
    uint16_t evdev_strong_mag;   /* 0..65535 */
    uint16_t evdev_weak_mag;     /* 0..65535 (blends highFrequency + trigger impulse) */
    uint16_t evdev_duration_ms;
};

static_assert(sizeof(ScarlettRumbleTransportPacket) == 52,
              "ScarlettRumbleTransportPacket must be 52 bytes");

class GameInputBackend {
public:
    static constexpr size_t kRingCapacity = 4096u;

    scarlett_hresult_t create_instance(uint64_t& out_handle) noexcept;
    scarlett_hresult_t destroy_instance(uint64_t handle) noexcept;

    scarlett_hresult_t push_reading(uint64_t handle,
                                    const scarlett_input_reading_record_t& rec) noexcept;
    scarlett_hresult_t get_current_reading(uint64_t handle,
                                           uint32_t input_kind,
                                           uint64_t device_id,
                                           scarlett_input_reading_record_t& out_rec) noexcept;
    scarlett_hresult_t get_next_reading(uint64_t handle,
                                        uint64_t ref_sequence,
                                        uint32_t input_kind,
                                        uint64_t device_id,
                                        scarlett_input_reading_record_t& out_rec) noexcept;
    scarlett_hresult_t get_previous_reading(uint64_t handle,
                                            uint64_t ref_sequence,
                                            uint32_t input_kind,
                                            uint64_t device_id,
                                            scarlett_input_reading_record_t& out_rec) noexcept;

    scarlett_hresult_t set_rumble_state(const scarlett_input_rumble_params_t& req) noexcept;
    scarlett_hresult_t query_last_rumble_packet(uint64_t device_id,
                                                ScarlettRumbleTransportPacket& out_pkt) noexcept;
    scarlett_hresult_t set_transport_mode(uint32_t mode) noexcept;

    uint64_t current_timestamp_us() const noexcept;

private:
    struct RingSlot {
        std::atomic<uint64_t> gen{0}; /* Even = stable, odd = writing */
        scarlett_input_reading_record_t data{};
    };

    struct Instance {
        uint64_t handle = 0;
        std::atomic<uint64_t> write_seq{0};
        std::array<RingSlot, kRingCapacity> ring{};
    };

    static bool matches_filter(const scarlett_input_reading_record_t& rec,
                               uint32_t input_kind,
                               uint64_t device_id) noexcept;
    static bool read_slot_consistent(const RingSlot& slot,
                                     scarlett_input_reading_record_t& out_rec) noexcept;

    std::mutex mutex_;
    uint64_t next_handle_ = 1;
    std::unordered_map<uint64_t, std::unique_ptr<Instance>> instances_;
    std::atomic<uint32_t> transport_mode_{SCARLETT_INPUT_TRANSPORT_AUTO_MOCK};
    std::atomic<uint8_t> gip_seq_{1};
    std::unordered_map<uint64_t, ScarlettRumbleTransportPacket> last_rumble_;
};

} // namespace scarlett::audio_media
