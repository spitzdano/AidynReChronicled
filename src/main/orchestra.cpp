// The orchestral music (#12): see orchestra.h.
//
// Each sample the game's music plays has an entry in the instrument map (tools/orchestra/make_map.py, keyed by the
// sample's own data): a General MIDI program (or a drum note on the orchestral kit), the sample's fundamental f0 and an
// octave shift. A note the game starts (its pitch is a playback rate: pitch / 44100 of the sample's own speed) becomes
// the MIDI note nearest f0 x pitch / 44100, shifted; its velocity follows the game's volume, its pan the game's pan,
// and later volume changes (fades between areas) the channel's volume. Each program gets a channel of its own (the
// drums channel 9, the orchestral kit). The notes arrive in the game's music player's 1 ms steps; render() spreads a
// chunk's steps over its frames, so they sound at their own time within it.
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
    // sit there too, so Enhanced and Original treat it as they treat the game's own.
    constexpr float kGainDb = -22.0f;
    constexpr int kVoices = 64;

    struct Instrument {
        int channel = 0, note_shift = 0, drum_note = -1;
        float f0 = 440.0f, velocity = 1.0f;
    };
    struct Event {
        uint64_t tick;
        enum { On, Off, Volume, Pan } kind;
        int channel, note;
        float value;
    };
    struct Voice {
        bool on = false;
        uint32_t id = 0;
        int channel = 0, note = 0;
        uint32_t volume_at_start = 1;
        float last_ratio = 1.0f;
    };

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
        std::unordered_map<std::string, Instrument> map_of;
        std::unordered_map<int, int> channel_of;  // program -> channel
        int next = 0;
        for (const auto& [key, e] : map.items()) {
            Instrument in;
            in.velocity = e.value("vel", 1.0f);
            if (e.contains("drum")) {
                in.channel = 9;
                in.drum_note = e["drum"].get<int>();
            } else {
                const int program = e.value("program", 48);
                auto it = channel_of.find(program);
                if (it == channel_of.end()) {
                    if (next == 9) next++;
                    if (next > 15) continue;  // more programs than channels: left to the game
                    it = channel_of.emplace(program, next++).first;
                    tsf_channel_set_presetnumber(loaded_synth, it->second, program, 0);
                }
                in.channel = it->second;
                in.f0 = e.value("f0", 440.0f);
                in.note_shift = e.value("shift", 0);
            }
            map_of.emplace(key, in);
        }
        tsf_channel_set_presetnumber(loaded_synth, 9, 48, 1);  // the orchestral kit
        {
            std::lock_guard g(lock);
            synth = loaded_synth;
            instruments = std::move(map_of);
        }
        std::fprintf(stderr, "Orchestra: %s, %zu instruments on %d channels\n", sf2.string().c_str(),
                     instruments.size(), next);
        state = instruments.empty() ? kFailed : kReady;
    }

    // Switched off: the notes it plays are let go (render() goes on while they fade), new ones are the game's again.
    void release_all() {
        std::lock_guard g(lock);
        for (Voice& v : voices) {
            if (v.on && v.channel != 9) pending.push_back({ now, Event::Off, v.channel, v.note, 0 });
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
    if (v.on && v.channel != 9) pending.push_back({ now, Event::Off, v.channel, v.note, 0 });
    int note = in.drum_note;
    if (note < 0) {
        note = (int)std::lround(69 + 12 * std::log2(std::max(1.0f, in.f0 * (float)pitch / 44100.0f) / 440.0f)) + in.note_shift;
        note = std::clamp(note, 0, 127);
    }
    const float velocity = std::clamp((float)volume / 200.0f * in.velocity, 0.01f, 1.0f);
    pending.push_back({ now, Event::Pan, in.channel, 0, std::clamp((float)pan / 255.0f, 0.0f, 1.0f) });
    pending.push_back({ now, Event::Volume, in.channel, 0, 1.0f });
    pending.push_back({ now, Event::On, in.channel, note, velocity });
    v = { true, id, in.channel, note, std::max<uint32_t>(volume, 1), 1.0f };
}

bool aidyn::orchestra::playing(int voice, uint32_t id) {
    std::lock_guard g(lock);
    return voice >= 0 && voice < kVoices && voices[voice].on && voices[voice].id == id;
}

void aidyn::orchestra::voice_volume(int voice, uint32_t id, uint32_t volume) {
    std::lock_guard g(lock);
    if (voice < 0 || voice >= kVoices || !voices[voice].on || voices[voice].id != id) return;
    Voice& v = voices[voice];
    const float ratio = std::clamp((float)volume / (float)v.volume_at_start, 0.0f, 2.0f);
    if (std::fabs(ratio - v.last_ratio) > 0.01f) {
        v.last_ratio = ratio;
        pending.push_back({ now, Event::Volume, v.channel, 0, ratio });
    }
}

void aidyn::orchestra::voice_stop(int voice, uint32_t id) {
    std::lock_guard g(lock);
    if (voice < 0 || voice >= kVoices || !voices[voice].on || voices[voice].id != id) return;
    Voice& v = voices[voice];
    if (v.channel != 9) pending.push_back({ now, Event::Off, v.channel, v.note, 0 });
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
        }
    }
    if (done < frames) {
        tsf_render_float(synth, out + 2 * done, (int)(frames - done), 1);
    }
    hall.process(out, frames);  // out holds only the orchestra (audio.cpp mixes it into the game's sound after)
    pending.clear();
    rendered_until = now;
}
