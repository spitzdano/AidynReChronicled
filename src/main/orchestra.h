// The orchestral music (#12): the game's own music, note for note, played through orchestral instruments (a
// SoundFont, TinySoundFont) instead of its small N64 samples. src/game/music_log.cpp catches the notes the game's
// music player plays; src/main/audio.cpp mixes what this renders in. A prototype for now: on with
// AIDYN_ORCHESTRA=<SoundFont .sf2>, the instrument map in <data folder>/orchestra/orchestra-map.json
// (tools/orchestra/make_map.py).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace aidyn::orchestra {
    bool active();  // loaded and on
    bool handles(const std::string& sample_key);  // a sample in the instrument map: the game's voice is muted
    // A music voice the game started (the voice slot and id it got, its sample's map key, its pitch as a playback rate,
    // its volume and pan as the game gave them).
    void note_on(int voice, uint32_t id, const std::string& sample_key, uint32_t pitch, uint32_t volume, uint32_t pan);
    bool playing(int voice, uint32_t id);  // a voice the orchestra plays
    void voice_volume(int voice, uint32_t id, uint32_t volume);
    void voice_stop(int voice, uint32_t id);
    void tick();  // a step of the game's music player (1 ms)
    // Adds the orchestra's sound for this many frames (stereo, at the game's rate) into out.
    void render(float* out, size_t frames, uint32_t rate);
}
