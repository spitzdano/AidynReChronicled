#pragma once
#include <cstddef>
#include <cstdint>

namespace aidyn::audio {
    void init(uint32_t output_frequency);
    void queue_samples(int16_t* audio_data, size_t sample_count);
    size_t get_frames_remaining();
    void set_frequency(uint32_t freq);
}
