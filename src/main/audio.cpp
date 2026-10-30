// SDL audio output, from Zelda64Recomp's main.cpp: converts the game's 16-bit stereo samples (at the
// game's own sample rate) to float at the device rate and queues them.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <cmath>

#include "SDL2/SDL.h"

#include "audio.h"
#include "watch.h"
#include "settings.h"

static SDL_AudioCVT audio_convert;
static SDL_AudioDeviceID audio_device = 0;

// Samples per channel per second.
static uint32_t sample_rate = 48000;
static uint32_t output_sample_rate = 48000;
// Channel count.
constexpr uint32_t input_channels = 2;
static uint32_t output_channels = 2;

// Terminology: a frame is a collection of samples for each channel. e.g. 2 input samples is one input frame. This is unrelated to graphical frames.

// Number of frames to duplicate for fixing interpolation at the start and end of a chunk.
constexpr uint32_t duplicated_input_frames = 4;
// The number of output frames to skip for playback (to avoid playing duplicate inputs twice).
static uint32_t discarded_output_frames;

constexpr uint32_t bytes_per_frame = input_channels * sizeof(float);

// AIDYN_AUDIO_DUMP=<file>: everything the game plays is also written there as raw 16-bit stereo (left first) at the
// game's own rate (printed once), for checking sound changes without listening. Off unless set.
static void dump_samples(const int16_t* audio_data, size_t sample_count) {
    static FILE* f = [] {
        const char* path = std::getenv("AIDYN_AUDIO_DUMP");
        return (path != nullptr && *path != '\0') ? std::fopen(path, "wb") : nullptr;
    }();
    if (f == nullptr) {
        return;
    }
    static uint32_t rate = 0;
    if (rate != sample_rate) {
        rate = sample_rate;
        fprintf(stderr, "audio dump: %u Hz\n", rate);
    }
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        const int16_t frame[2] = { audio_data[i + 1], audio_data[i] };
        std::fwrite(frame, sizeof(frame), 1, f);
    }
    std::fflush(f);
}

// Sound: Enhanced (settings.json audio; the pause menu's ReChronicled page). The game's mix is very soft (-35 to -39
// LUFS measured on the title, battle, castle and tower music, where most games sit around -16 to -23) and dull (its
// instrument samples are low-rate: almost nothing above 4 kHz). Enhanced lifts the treble with a high shelf (RBJ
// cookbook, 3 kHz, audio.brightness_db), raises the level by audio.loudness_db and holds the peaks at -1 dBFS with a
// limiter that looks 1.5 ms ahead (the output is delayed that much), so the loud moments are turned down smoothly
// instead of clipping. Original is the port's plain path, the game's samples at half scale, as before.
namespace {
    constexpr float kShelfHz = 3000.0f, kCeiling = 0.891f;  // -1 dBFS
    constexpr int kLook = 64;                                  // frames of look-ahead (1.5 ms at 44.1 kHz)

    struct Enhancer {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;  // the shelf
        float z1[2] = {}, z2[2] = {};
        uint32_t rate = 0;
        float shelf_db = -1, gain = 1, attack = 1, release = 1;
        float delay[kLook][2] = {}, need[kLook] = {};
        int pos = 0;
        float env = 1;

        void setup(uint32_t r, const aidyn::settings::Audio& a) {
            if (r == rate && a.brightness_db == shelf_db && std::pow(10.0f, a.loudness_db / 20.0f) == gain) {
                return;
            }
            rate = r;
            shelf_db = a.brightness_db;
            gain = std::pow(10.0f, a.loudness_db / 20.0f);
            // RBJ high shelf, slope 1.
            const float A = std::pow(10.0f, shelf_db / 40.0f), w = 2.0f * 3.14159265f * kShelfHz / (float)rate;
            const float cw = std::cos(w), alpha = std::sin(w) / 2.0f * std::sqrt(2.0f), sa = 2.0f * std::sqrt(A) * alpha;
            const float a0 = (A + 1) - (A - 1) * cw + sa;
            b0 = A * ((A + 1) + (A - 1) * cw + sa) / a0;
            b1 = -2 * A * ((A - 1) + (A + 1) * cw) / a0;
            b2 = A * ((A + 1) + (A - 1) * cw - sa) / a0;
            a1 = 2 * ((A - 1) - (A + 1) * cw) / a0;
            a2 = ((A + 1) - (A - 1) * cw - sa) / a0;
            attack = 1.0f - std::exp(-4.0f / kLook);           // most of the way down within the look-ahead
            release = 1.0f - std::exp(-1.0f / (0.15f * rate));  // back up over about 150 ms
        }

        void reset() {
            *this = Enhancer{};
        }

        // One frame in (the game's sample at half scale, as the plain path has it), one frame out, 1.5 ms later.
        void process(float& l, float& r) {
            float in[2] = { l, r };
            for (int c = 0; c < 2; c++) {
                const float y = b0 * in[c] + z1[c];  // transposed direct form II
                z1[c] = b1 * in[c] - a1 * y + z2[c];
                z2[c] = b2 * in[c] - a2 * y;
                in[c] = y * gain;
            }
            const float peak = std::max(std::fabs(in[0]), std::fabs(in[1]));
            need[pos] = peak > kCeiling ? kCeiling / peak : 1.0f;
            float lowest = 1.0f;
            for (float n : need) lowest = std::min(lowest, n);
            env += (lowest - env) * (lowest < env ? attack : release);
            const float out_l = delay[pos][0], out_r = delay[pos][1];
            delay[pos][0] = in[0];
            delay[pos][1] = in[1];
            pos = (pos + 1) % kLook;
            l = std::clamp(out_l * env, -kCeiling, kCeiling);  // the limiter's last few hundredths, if any
            r = std::clamp(out_r * env, -kCeiling, kCeiling);
        }
    };
    Enhancer enhancer;
}

// Running dry. The game makes its sound a little at a time, on its own thread, topping the queue up to what
// get_frames_remaining asks for; when that thread is held up longer than the queue lasts (a slow frame, a map loading)
// the device plays silence until the next piece comes: a pop or a crackle. Each time the queue was found empty is
// counted and logged (at most every 10 seconds) with the longest wait between two pieces, so a report shows it.
static void note_dry() {
    using clock = std::chrono::steady_clock;
    static clock::time_point last_piece{}, last_log{};
    static uint32_t dry = 0, dry_logged = 0;
    static double longest_ms = 0.0;
    const auto now = clock::now();
    const bool started = last_piece != clock::time_point{};
    if (started && SDL_GetQueuedAudioSize(audio_device) == 0) {
        dry++;
        longest_ms = std::max(longest_ms, std::chrono::duration<double, std::milli>(now - last_piece).count());
    }
    last_piece = now;
    if (dry != dry_logged && now - last_log >= std::chrono::seconds(10)) {
        printf("Sound: ran dry %u time%s so far (the longest wait for the game's sound %.0f ms)\n", dry,
               dry == 1 ? "" : "s", longest_ms);
        fflush(stdout);
        dry_logged = dry;
        last_log = now;
    }
}

void aidyn::audio::queue_samples(int16_t* audio_data, size_t sample_count) {
    aidyn::watch::sound_tick();  // the freeze watchdog: the sound still going (src/main/watch.cpp)
    dump_samples(audio_data, sample_count);
    // Buffer for holding the output of swapping the audio channels. This is reused across
    // calls to reduce runtime allocations.
    static std::vector<float> swap_buffer;
    static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer;

    // Make sure the swap buffer is large enough to hold the audio data, including any extra space needed for resampling.
    size_t resampled_sample_count = sample_count + duplicated_input_frames * input_channels;
    size_t max_sample_count = std::max(resampled_sample_count, resampled_sample_count * audio_convert.len_mult);
    if (max_sample_count > swap_buffer.size()) {
        swap_buffer.resize(max_sample_count);
    }
    
    // Copy the duplicated frames from last chunk into this chunk
    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        swap_buffer[i] = duplicated_sample_buffer[i];
    }

    // Convert the audio from 16-bit values to floats and swap the audio channels into the
    // swap buffer to correct for the address xor caused by endianness handling.
    float cur_main_volume = 1.0f; // Get the current main volume, normalized to 0.0-1.0.
    const aidyn::settings::Audio& sound = aidyn::settings::audio();
    if (sound.enhanced) {
        enhancer.setup(sample_rate, sound);
    } else {
        enhancer.reset();
    }
    for (size_t i = 0; i < sample_count; i += input_channels) {
        float l = audio_data[i + 1] * (0.5f / 32768.0f) * cur_main_volume;
        float r = audio_data[i + 0] * (0.5f / 32768.0f) * cur_main_volume;
        if (sound.enhanced) {
            enhancer.process(l, r);
        }
        swap_buffer[i + 0 + duplicated_input_frames * input_channels] = l;
        swap_buffer[i + 1 + duplicated_input_frames * input_channels] = r;
    }
    // AIDYN_AUDIO_DUMP_OUTPUT=<file>: what goes to the device, after Sound: Enhanced (raw 32-bit float stereo at the
    // game's rate), to measure it. Off unless set.
    static FILE* out_dump = [] {
        const char* path = std::getenv("AIDYN_AUDIO_DUMP_OUTPUT");
        return (path != nullptr && *path != '\0') ? std::fopen(path, "wb") : nullptr;
    }();
    if (out_dump != nullptr) {
        std::fwrite(swap_buffer.data() + duplicated_input_frames * input_channels, sizeof(float), sample_count, out_dump);
        std::fflush(out_dump);
    }
    
    // TODO handle cases where a chunk is smaller than the duplicated frame count.
    assert(sample_count > duplicated_input_frames * input_channels);

    // Copy the last converted samples into the duplicated sample buffer to reuse in resampling the next queued chunk.
    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        duplicated_sample_buffer[i] = swap_buffer[i + sample_count];
    }
    
    audio_convert.buf = reinterpret_cast<Uint8*>(swap_buffer.data());
    audio_convert.len = (sample_count + duplicated_input_frames * input_channels) * sizeof(swap_buffer[0]);

    int ret = SDL_ConvertAudio(&audio_convert);

    if (ret < 0) {
        printf("Error using SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error using SDL audio converter");
    }

    uint64_t cur_queued_microseconds = uint64_t(SDL_GetQueuedAudioSize(audio_device)) / bytes_per_frame * 1000000 / sample_rate;
    uint32_t num_bytes_to_queue = audio_convert.len_cvt - output_channels * discarded_output_frames * sizeof(swap_buffer[0]);
    float* samples_to_queue = swap_buffer.data() + output_channels * discarded_output_frames / 2;

    // Prevent audio latency from building up by skipping samples in incoming audio when too many samples are already queued.
    // Skip samples based on how many microseconds of samples are queued already.
    uint32_t skip_factor = cur_queued_microseconds / 100000;
    if (skip_factor != 0) {
        uint32_t skip_ratio = 1 << skip_factor;
        num_bytes_to_queue /= skip_ratio;
        for (size_t i = 0; i < num_bytes_to_queue / (output_channels * sizeof(swap_buffer[0])); i++) {
            samples_to_queue[2 * i + 0] = samples_to_queue[2 * skip_ratio * i + 0];
            samples_to_queue[2 * i + 1] = samples_to_queue[2 * skip_ratio * i + 1];
        }
    }

    note_dry();

    // AIDYN_MUTE=1 (test runs: tools/dev/pad.sh, the Proton test): silence in place of the sound, through the same
    // device and queue, so the game's timing (paced by the queue) is as ever. SDL_AUDIODRIVER=dummy does the same on
    // Linux, but the Windows build's SDL under Proton still opened the real device.
    static const bool mute = [] { const char* m = std::getenv("AIDYN_MUTE"); return m != nullptr && *m != '\0' && *m != '0'; }();
    if (mute) std::memset(samples_to_queue, 0, num_bytes_to_queue);

    // Queue the swapped audio data.
    // Offset the data start by only half the discarded frame count as the other half of the discarded frames are at the end of the buffer.
    SDL_QueueAudio(audio_device, samples_to_queue, num_bytes_to_queue);
}

// How far ahead the game keeps its sound, in VIs (60ths of a second) on top of the one it is about to make. Zelda64Recomp
// (where this comes from) keeps one, so the queue held 18 to 35 ms and a hold-up of the game's thread longer than
// that ran it dry (pops and crackles on slower PCs, maps loading at a high draw distance). Three keep 52 to 68 ms, still
// below the 100 ms where queue_samples starts dropping audio. AIDYN_AUDIO_AHEAD=<VIs> (testing).
static float buffer_offset_frames() {
    static const float v = [] {
        const char* e = std::getenv("AIDYN_AUDIO_AHEAD");
        return e != nullptr ? std::clamp((float)std::atof(e), 0.0f, 4.0f) : 3.0f;
    }();
    return v;
}

size_t aidyn::audio::get_frames_remaining() {
    const float buffer_offset_frames = ::buffer_offset_frames();
    // Get the number of remaining buffered audio bytes.
    uint64_t buffered_byte_count = SDL_GetQueuedAudioSize(audio_device);

    // Scale the byte count based on the ratio of sample rates and channel counts.
    buffered_byte_count = buffered_byte_count * 2 * sample_rate / output_sample_rate / output_channels;

    // Adjust the reported count to be some number of refreshes in the future, which helps ensure that
    // there are enough samples even if the audio thread experiences a small amount of lag. This prevents
    // audio popping on games that use the buffered audio byte count to determine how many samples
    // to generate.
    uint32_t frames_per_vi = (sample_rate / 60);
    if (buffered_byte_count > (buffer_offset_frames * bytes_per_frame * frames_per_vi)) {
        buffered_byte_count -= (buffer_offset_frames * bytes_per_frame * frames_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    // Convert from byte count to sample count.
    uint32_t frames = static_cast<uint32_t>(buffered_byte_count / bytes_per_frame);

    // Aidyn sizes each audio buffer as (one VI of samples - frames still queued + 0x50), in unsigned
    // arithmetic, and "Audio list overrun!" crashes when that wraps. On a console the AI never holds more
    // than a VI or so, but a host audio device that starts late (or a game running ahead of real time)
    // can queue more. Never report more than one VI's worth: queue_samples already drops audio when
    // the real queue grows past 100 ms.
    return std::min(frames, sample_rate / 60);
}

static void update_audio_converter() {
    int ret = SDL_BuildAudioCVT(&audio_convert, AUDIO_F32, input_channels, sample_rate, AUDIO_F32, output_channels, output_sample_rate);

    if (ret < 0) {
        printf("Error creating SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error creating SDL audio converter");
    }

    // Calculate the number of samples to discard based on the sample rate ratio and the duplicate frame count.
    discarded_output_frames = duplicated_input_frames * output_sample_rate / sample_rate;
}

void aidyn::audio::set_frequency(uint32_t freq) {
    sample_rate = freq;
    
    update_audio_converter();
}

static void reset_audio(uint32_t output_freq) {
    SDL_AudioSpec spec_desired{
        .freq = (int)output_freq,
        .format = AUDIO_F32,
        .channels = (Uint8)output_channels,
        .silence = 0, // calculated
        .samples = 0x100, // Fairly small sample count to reduce the latency of internal buffering
        .padding = 0, // unused
        .size = 0, // calculated
        .callback = nullptr,
        .userdata = nullptr
    };


    audio_device = SDL_OpenAudioDevice(nullptr, false, &spec_desired, nullptr, 0);
    if (audio_device == 0) {
        fprintf(stderr, "SDL error opening audio device: %s\n", SDL_GetError());
        std::abort();
    }
    SDL_PauseAudioDevice(audio_device, 0);

    output_sample_rate = output_freq;
    update_audio_converter();
}


void aidyn::audio::init(uint32_t output_frequency) {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    reset_audio(output_frequency);
}
