// The orchestral music (#12): see orchestra.h.
//
// Each sample the game's music plays has an entry in the instrument map (tools/orchestra/make_map.py, keyed by the
// sample's own data): a General MIDI program (or a drum note on the orchestral kit), the sample's fundamental f0, an
// octave shift and a level (gain, matched to the original part: tools/orchestra). A note the game starts (its pitch is
// a playback rate: pitch / 44100 of the sample's own speed) becomes the MIDI note nearest f0 x pitch / 44100, shifted;
// its velocity follows the game's volume, its pan the game's pan. Every game voice plays on channels of its own (two,
// in turn, so a note's release keeps its own settings while the next starts), so the game's later changes to that
// voice (volume swells and fades, pitch bends) reach only its note, as they do in the game. The notes arrive in the
// game's music player's 1 ms steps; render() spreads a chunk's steps over its frames, so they sound at their own time.
#define TSF_IMPLEMENTATION
#include "tsf.h"

#include "orchestra.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "json/json.hpp"
#include "settings.h"

namespace {
    // The game's music plays at half scale before Sound: Enhanced (about -36 LUFS); the SoundFont's level is set to
    // sit there too, so Enhanced and Original treat it as they treat the game's own. Each instrument's "gain" matches
    // its part to the original part rendered from the samples at the game's volumes; the game's mixer puts those 27 dB
    // lower (measured on the title and a battle).
    constexpr float kGainDb = -27.0f;
    constexpr int kVoices = 64;

    struct Instrument {
        int program = 48, note_shift = 0, drum_note = -1;
        float f0 = 440.0f, velocity = 1.0f, gain = 1.0f;
    };
    struct Event {
        uint64_t tick;
        enum { On, Off, Volume, Pan, Preset, Tuning } kind;
        int channel, note;  // Preset: note the program, value 1 for the drum kit
        float value;
    };
    struct Voice {
        bool on = false;
        uint32_t id = 0;
        int channel = 0, note = 0, turn = 0;
        uint32_t volume_at_start = 1;
        float gain = 1.0f, last_volume = 1.0f, f0 = 440.0f, exact = 0.0f;  // exact: the note before rounding
        int note_shift = 0;
        bool drum = false;
    };

    // The MIDI note (fractional) a sample of fundamental f0 sounds at, played at this rate.
    float exact_note(float f0, uint32_t rate, int shift) {
        return 69.0f + 12.0f * std::log2(std::max(1.0f, f0 * (float)rate / 44100.0f) / 440.0f) + (float)shift;
    }

    // A hall for the orchestra (it plays dry otherwise): Freeverb (Jezar's public-domain design), its 8 comb and 4
    // all-pass filters per channel at their 44.1 kHz lengths, right a little longer than left for width.
    struct Reverb {
        static constexpr int kCombs[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr int kPasses[4] = { 556, 441, 341, 225 };
        static constexpr int kSpread = 23;
        static constexpr float kRoom = 0.80f, kDamp = 0.40f, kWet = 0.22f, kInput = 0.015f;
        std::vector<float> comb[2][8], pass[2][4];
        int comb_at[2][8] = {}, pass_at[2][4] = {};
        float low[2][8] = {};

        Reverb() {
            for (int c = 0; c < 2; c++) {
                for (int i = 0; i < 8; i++) comb[c][i].assign(kCombs[i] + c * kSpread, 0.0f);
                for (int i = 0; i < 4; i++) pass[c][i].assign(kPasses[i] + c * kSpread, 0.0f);
            }
        }

        void process(float* buf, size_t frames) {
            for (size_t f = 0; f < frames; f++) {
                const float in = (buf[2 * f] + buf[2 * f + 1]) * kInput;
                for (int c = 0; c < 2; c++) {
                    float sum = 0;
                    for (int i = 0; i < 8; i++) {
                        std::vector<float>& d = comb[c][i];
                        const float y = d[comb_at[c][i]];
                        low[c][i] = y * (1 - kDamp) + low[c][i] * kDamp;
                        d[comb_at[c][i]] = in + low[c][i] * kRoom;
                        comb_at[c][i] = (comb_at[c][i] + 1) % (int)d.size();
                        sum += y;
                    }
                    for (int i = 0; i < 4; i++) {
                        std::vector<float>& d = pass[c][i];
                        const float b = d[pass_at[c][i]];
                        d[pass_at[c][i]] = sum + b * 0.5f;
                        pass_at[c][i] = (pass_at[c][i] + 1) % (int)d.size();
                        sum = b - sum;
                    }
                    buf[2 * f + c] += sum * kWet;
                }
            }
        }
    };

    enum { kNotLoaded, kLoading, kReady, kFailed };
    std::atomic_int state{ kNotLoaded };
    std::mutex lock;
    Reverb hall;
    tsf* synth = nullptr;
    uint32_t synth_rate = 0;
    std::unordered_map<std::string, Instrument> instruments;
    std::vector<Event> pending;
    Voice voices[kVoices];
    uint64_t now = 0, rendered_until = 0;

    // The SoundFont: AIDYN_ORCHESTRA, else the first .sf2 in <data folder>/orchestra.
    std::filesystem::path soundfont() {
        const char* env = std::getenv("AIDYN_ORCHESTRA");
        if (env != nullptr && *env != '\0') return env;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(aidyn::settings::folder() / "orchestra", ec)) {
            if (e.path().extension() == ".sf2") return e.path();
        }
        return {};
    }

    bool wanted() {
        const char* env = std::getenv("AIDYN_ORCHESTRA");
        return (env != nullptr && *env != '\0') || aidyn::settings::audio().orchestral;
    }

    // Loads the map and the SoundFont (a couple of seconds for a big one) on a thread of its own; the game's own music
    // plays until it's ready.
    void load() {
        const std::filesystem::path sf2 = soundfont();
        const auto map_path = aidyn::settings::folder() / "orchestra/orchestra-map.json";
        nlohmann::json map;
        try {
            std::ifstream in(map_path);
            map = nlohmann::json::parse(in);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Orchestra: no instrument map at %s (tools/orchestra/make_map.py): %s\n",
                         map_path.string().c_str(), e.what());
            state = kFailed;
            return;
        }
        tsf* loaded_synth = sf2.empty() ? nullptr : tsf_load_filename(sf2.string().c_str());
        if (loaded_synth == nullptr) {
            std::fprintf(stderr, "Orchestra: no SoundFont (%s)\n", sf2.empty() ? "none in the data folder's orchestra" : sf2.string().c_str());
            state = kFailed;
            return;
        }
        tsf_set_max_voices(loaded_synth, 256);
        tsf_channel_set_presetnumber(loaded_synth, 2 * kVoices - 1, 0, 0);  // all the voices' channels, made at once
        std::unordered_map<std::string, Instrument> map_of;
        for (const auto& [key, e] : map.items()) {
            Instrument in;
            in.velocity = e.value("vel", 1.0f);
            in.gain = e.value("gain", 1.0f);
            if (e.contains("drum")) {
                in.drum_note = e["drum"].get<int>();
            } else {
                in.program = e.value("program", 48);
                in.f0 = e.value("f0", 440.0f);
                in.note_shift = e.value("shift", 0);
            }
            map_of.emplace(key, in);
        }
        {
            std::lock_guard g(lock);
            synth = loaded_synth;
            instruments = std::move(map_of);
        }
        std::fprintf(stderr, "Orchestra: %s, %zu instruments\n", sf2.string().c_str(), instruments.size());
        state = instruments.empty() ? kFailed : kReady;
    }

    // Switched off: the notes it plays are let go (render() goes on while they fade), new ones are the game's again.
    void release_all() {
        std::lock_guard g(lock);
        for (Voice& v : voices) {
            if (v.on && !v.drum) pending.push_back({ now, Event::Off, v.channel, v.note, 0 });
            v.on = false;
        }
    }
}

bool aidyn::orchestra::active() {
    static bool was_on = false;
    if (!wanted()) {
        if (was_on) {
            was_on = false;
            release_all();
        }
        return false;
    }
    int expected = kNotLoaded;
    if (state.compare_exchange_strong(expected, kLoading)) {
        std::thread(load).detach();
    }
    was_on = state == kReady;
    return was_on;
}

bool aidyn::orchestra::loaded() {
    return state == kReady;
}

bool aidyn::orchestra::available() {
    return !soundfont().empty();
}

bool aidyn::orchestra::handles(const std::string& sample_key) {
    std::lock_guard g(lock);
    return state == kReady && instruments.count(sample_key) != 0;
}

void aidyn::orchestra::note_on(int voice, uint32_t id, const std::string& sample_key, uint32_t pitch, uint32_t volume,
                               uint32_t pan) {
    std::lock_guard g(lock);
    if (state != kReady || voice < 0 || voice >= kVoices) return;
    const auto it = instruments.find(sample_key);
    if (it == instruments.end()) return;
    const Instrument& in = it->second;
    Voice& v = voices[voice];
    if (v.on && !v.drum) pending.push_back({ now, Event::Off, v.channel, v.note, 0 });
    const bool drum = in.drum_note >= 0;
    const float exact = drum ? (float)in.drum_note : exact_note(in.f0, pitch, in.note_shift);
    const int note = std::clamp((int)std::lround(exact), 0, 127);
    const int turn = v.turn ^ 1, channel = 2 * voice + turn;
    const float velocity = std::clamp((float)volume / 200.0f * in.velocity, 0.01f, 1.0f);
    pending.push_back({ now, Event::Preset, channel, drum ? 0 : in.program, drum ? 1.0f : 0.0f });
    pending.push_back({ now, Event::Tuning, channel, 0, 0.0f });  // in tune: the nearest note (the samples' own
                                                                   // tunings differ by up to a third of a semitone)
    pending.push_back({ now, Event::Pan, channel, 0, std::clamp((float)pan / 255.0f, 0.0f, 1.0f) });
    pending.push_back({ now, Event::Volume, channel, 0, in.gain });
    pending.push_back({ now, Event::On, channel, note, velocity });
    v = Voice{};
    v.on = true;
    v.id = id;
    v.channel = channel;
    v.note = note;
    v.turn = turn;
    v.volume_at_start = std::max<uint32_t>(volume, 1);
    v.gain = v.last_volume = in.gain;
    v.f0 = in.f0;
    v.note_shift = in.note_shift;
    v.exact = exact;
    v.drum = drum;
}

bool aidyn::orchestra::playing(int voice, uint32_t id) {
    std::lock_guard g(lock);
    return voice >= 0 && voice < kVoices && voices[voice].on && voices[voice].id == id;
}

void aidyn::orchestra::voice_volume(int voice, uint32_t id, uint32_t volume) {
    std::lock_guard g(lock);
    if (voice < 0 || voice >= kVoices || !voices[voice].on || voices[voice].id != id) return;
    Voice& v = voices[voice];
    const float level = v.gain * std::clamp((float)volume / (float)v.volume_at_start, 0.0f, 2.0f);
    if (std::fabs(level - v.last_volume) > 0.005f * v.gain) {
        v.last_volume = level;
        pending.push_back({ now, Event::Volume, v.channel, 0, level });
    }
}

void aidyn::orchestra::voice_pitch(int voice, uint32_t id, uint32_t pitch) {
    std::lock_guard g(lock);
    if (voice < 0 || voice >= kVoices || !voices[voice].on || voices[voice].id != id || voices[voice].drum) return;
    Voice& v = voices[voice];
    // A bend from where the note started (it was rounded into tune: the bend keeps that).
    pending.push_back({ now, Event::Tuning, v.channel, 0, exact_note(v.f0, pitch, v.note_shift) - v.exact });
}

void aidyn::orchestra::voice_stop(int voice, uint32_t id) {
    std::lock_guard g(lock);
    if (voice < 0 || voice >= kVoices || !voices[voice].on || voices[voice].id != id) return;
    Voice& v = voices[voice];
    if (!v.drum) pending.push_back({ now, Event::Off, v.channel, v.note, 0 });
    v.on = false;
}

void aidyn::orchestra::tick() {
    std::lock_guard g(lock);
    now++;
}

void aidyn::orchestra::render(float* out, size_t frames, uint32_t rate) {
    std::lock_guard g(lock);
    if (state != kReady || frames == 0) return;
    if (rate != synth_rate) {
        tsf_set_output(synth, TSF_STEREO_INTERLEAVED, (int)rate, kGainDb);
        synth_rate = rate;
    }
    const uint64_t steps = std::max<uint64_t>(1, now - rendered_until);
    size_t done = 0;
    for (const Event& e : pending) {
        const uint64_t step = e.tick > rendered_until ? e.tick - rendered_until : 0;
        const size_t at = std::min(frames, (size_t)(step * frames / steps));
        if (at > done) {
            tsf_render_float(synth, out + 2 * done, (int)(at - done), 1);
            done = at;
        }
        switch (e.kind) {
            case Event::On: tsf_channel_note_on(synth, e.channel, e.note, e.value); break;
            case Event::Off: tsf_channel_note_off(synth, e.channel, e.note); break;
            case Event::Volume: tsf_channel_set_volume(synth, e.channel, e.value); break;
            case Event::Pan: tsf_channel_set_pan(synth, e.channel, e.value); break;
            case Event::Preset: tsf_channel_set_presetnumber(synth, e.channel, e.note, e.value != 0.0f); break;
            case Event::Tuning: tsf_channel_set_tuning(synth, e.channel, e.value); break;
        }
    }
    if (done < frames) {
        tsf_render_float(synth, out + 2 * done, (int)(frames - done), 1);
    }
    hall.process(out, frames);  // out holds only the orchestra (audio.cpp mixes it into the game's sound after)
    pending.clear();
    rendered_until = now;
}
