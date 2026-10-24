// AIDYN_MUSIC_LOG=<file>: every voice the game's music and sound player (DCM, the decomp's dcm.cpp and audio.cpp)
// starts, changes and stops, with the time in its own 1 ms steps, for working out the orchestral music (#12) from
// what the game really plays. Off unless set.
//
// DCM::VoiceHandler (func_80090C94; its entry hook, aidyn_dcm_tick, counts here) runs once a millisecond of sound (it returns MTIME, 1000 us) and steps every
// playing track, calling the voice functions: AddVoice (func_8008F778: &index, &id, Borg11Data* sample, offset;
// on the stack start, end, loops, pitch, volume, pan), SetVoicePitch / Vol / Pan (func_8008F968 / 9D0 / A40: voice,
// id, value) and StopVoice (func_8008FAB0: voice, id). A line per call:
//   <ms> add <voice> <id> <sample addr> <len> <samples> <flag> <start> <end> <loops> <pitch> <vol> <pan>
//   <ms> pitch|vol|pan <voice> <id> <value>
//   <ms> stop <voice> <id>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "recomp.h"
#include "orchestra.h"

// The orchestral music (src/main/orchestra.cpp) takes its notes from the same calls: a voice AddVoice starts for a
// music track (the DCM_struct VoiceHandler is stepping, $s0, +4 its Borg12Sub: 4+ channels at +0 and a command stream
// of 3000+ bytes at +8, as tools/orchestra/make_map.py tells music from sounds) whose sample is in the instrument map
// is played by the orchestra, and the game's own voice is muted (its volume argument and later volume changes 0), so
// the player keeps its own bookkeeping. The sample's key: "<samples>:<flag>:<FNV-1a of its first 256 bytes>"
// (Borg11Data: flag +0, len +4, samples +8, PCM +0xC).
namespace {
    FILE* music_log() {
        static FILE* f = [] {
            const char* path = std::getenv("AIDYN_MUSIC_LOG");
            return (path != nullptr && *path != '\0') ? std::fopen(path, "w") : nullptr;
        }();
        return f;
    }
    uint64_t ms = 0;
    gpr add_index = 0, add_id = 0;
    char add_line[160];
    // The note AddVoice is starting, for its return (the voice slot is known only then).
    bool add_orchestral = false;
    std::string add_key;
    uint32_t add_pitch = 0, add_volume = 0, add_pan = 0;

    bool music_track(uint8_t* rdram, gpr dcm) {
        const uint32_t d = (uint32_t)dcm;
        if (d < 0x80000400u || d >= 0x80F00000u) return false;
        const gpr sub = MEM_W(4, dcm);
        if ((uint32_t)sub < 0x80000400u || (uint32_t)sub >= 0x80F00000u) return false;
        const uint32_t channels = (uint32_t)MEM_W(0, sub), size = (uint32_t)MEM_W(8, sub);
        return channels >= 4 && channels <= 32 && size >= 3000 && size < 0x100000;
    }

    std::string sample_key(uint8_t* rdram, gpr sample) {
        const uint32_t len = (uint32_t)MEM_W(4, sample);
        const gpr pcm = MEM_W(0xC, sample);
        uint32_t h = 0x811C9DC5u;
        for (uint32_t i = 0; i < len && i < 256; i++) {
            h = (h ^ MEM_BU(i, pcm)) * 0x01000193u;
        }
        char key[48];
        std::snprintf(key, sizeof key, "%u:%u:%08x", (uint32_t)MEM_W(8, sample), (uint32_t)MEM_W(0, sample), h);
        return key;
    }
}

extern "C" void aidyn_music_tick(uint8_t* rdram, recomp_context* ctx) {
    ms++;
    aidyn::orchestra::tick();
}

extern "C" void aidyn_music_add(uint8_t* rdram, recomp_context* ctx) {
    const gpr sp = ctx->r29, sample = ctx->r6;
    add_index = ctx->r4;
    add_id = ctx->r5;
    add_orchestral = false;
    if (aidyn::orchestra::active() && music_track(rdram, ctx->r16)) {
        add_key = sample_key(rdram, sample);
        if (aidyn::orchestra::handles(add_key)) {
            add_pitch = MEM_HU(0x1E, sp);
            add_volume = MEM_BU(0x23, sp);
            add_pan = MEM_BU(0x27, sp);
            add_orchestral = true;
            MEM_B(0x23, sp) = 0;  // the game's own voice starts silent
        }
    }
    if (music_log() == nullptr) return;
    std::snprintf(add_line, sizeof add_line, "%08X %u %u %u %u %u %u %u %u %u", (uint32_t)sample,
                  (uint32_t)MEM_W(4, sample), (uint32_t)MEM_W(8, sample), (uint32_t)MEM_W(0, sample),
                  (uint32_t)MEM_W(0x10, sp), (uint32_t)MEM_W(0x14, sp), MEM_BU(0x1B, sp), MEM_HU(0x1E, sp),
                  MEM_BU(0x23, sp), MEM_BU(0x27, sp));
}

extern "C" void aidyn_music_added(uint8_t* rdram, recomp_context* ctx) {
    if (add_orchestral) {
        add_orchestral = false;
        aidyn::orchestra::note_on(MEM_BU(0, add_index), (uint32_t)MEM_W(0, add_id), add_key, add_pitch, add_volume,
                                  add_pan);
    }
    if (music_log() == nullptr) return;
    std::fprintf(music_log(), "%llu add %u %u %s\n", (unsigned long long)ms, MEM_BU(0, add_index),
                 (uint32_t)MEM_W(0, add_id), add_line);
}

extern "C" void aidyn_music_set(uint8_t* rdram, recomp_context* ctx, const char* what) {
    const int voice = (int)(ctx->r4 & 0xFF);
    const uint32_t id = (uint32_t)ctx->r5;
    if (what[0] == 'v' && aidyn::orchestra::playing(voice, id)) {
        aidyn::orchestra::voice_volume(voice, id, (uint32_t)(ctx->r6 & 0xFF));
        ctx->r6 = 0;  // the game's own voice stays silent
    }
    if (music_log() == nullptr) return;
    std::fprintf(music_log(), "%llu %s %u %u %u\n", (unsigned long long)ms, what, (uint32_t)(ctx->r4 & 0xFF),
                 (uint32_t)ctx->r5, (uint32_t)(ctx->r6 & 0xFFFF));
}

extern "C" void aidyn_music_stop(uint8_t* rdram, recomp_context* ctx) {
    aidyn::orchestra::voice_stop((int)(ctx->r4 & 0xFF), (uint32_t)ctx->r5);
    if (music_log() == nullptr) return;
    std::fprintf(music_log(), "%llu stop %u %u\n", (unsigned long long)ms, (uint32_t)(ctx->r4 & 0xFF), (uint32_t)ctx->r5);
}
