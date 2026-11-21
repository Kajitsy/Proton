#include <cstdint>

namespace scarlett::audio_media {

struct AudioInputSpecification {
    uint32_t xaudio2_create_ordinal;
    uint32_t gameinput_create_ordinal;
    uint32_t impulse_rumble_motor_count;
};

constexpr AudioInputSpecification kAudioInputSpec = {
    1U,
    1U,
    4U,
};

const AudioInputSpecification& query_audio_input_specification() noexcept {
    return kAudioInputSpec;
}

} // namespace scarlett::audio_media
