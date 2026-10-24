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
#include <cmath>
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

    std::mutex lock;
    tsf* synth = nullptr;
    bool tried = false, loaded = false;
    uint32_t synth_rate = 0;
    std::unordered_map<std::string, Instrument> instruments;
    std::vector<Event> pending;
    Voice voices[kVoices];
    uint64_t now = 0, rendered_until = 0;

    void load() {
        tried = true;
        const char* sf2 = std::getenv("AIDYN_ORCHESTRA");
        if (sf2 == nullptr || *sf2 == '\0') {
            return;
        }
        const auto map_path = aidyn::settings::folder() / "orchestra/orchestra-map.json";
        nlohmann::json map;
        try {
            std::ifstream in(map_path);
            map = nlohmann::json::parse(in);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Orchestra: no instrument map at %s (tools/orchestra/make_map.py): %s\n",
                         map_path.string().c_str(), e.what());
            return;
        }
        synth = tsf_load_filename(sf2);
        if (synth == nullptr) {
            std::fprintf(stderr, "Orchestra: can't load the SoundFont %s\n", sf2);
            return;
        }
        tsf_set_max_voices(synth, 256);
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
                    tsf_channel_set_presetnumber(synth, it->second, program, 0);
                }
                in.channel = it->second;
                in.f0 = e.value("f0", 440.0f);
                in.note_shift = e.value("shift", 0);
            }
            instruments.emplace(key, in);
        }
        tsf_channel_set_presetnumber(synth, 9, 48, 1);  // the orchestral kit
        loaded = !instruments.empty();
        std::fprintf(stderr, "Orchestra: %s, %zu instruments on %d channels\n", sf2, instruments.size(), next);
    }
}

bool aidyn::orchestra::active() {
    std::lock_guard g(lock);
    if (!tried) load();
    return loaded;
}

bool aidyn::orchestra::handles(const std::string& sample_key) {
    std::lock_guard g(lock);
    if (!tried) load();
    return loaded && instruments.count(sample_key) != 0;
}

void aidyn::orchestra::note_on(int voice, uint32_t id, const std::string& sample_key, uint32_t pitch, uint32_t volume,
                               uint32_t pan) {
    std::lock_guard g(lock);
    if (!loaded || voice < 0 || voice >= kVoices) return;
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
    if (!loaded || frames == 0) return;
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
    pending.clear();
    rendered_until = now;
}
