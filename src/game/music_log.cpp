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

#include "recomp.h"

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
}

extern "C" void aidyn_music_tick(uint8_t* rdram, recomp_context* ctx) {
    ms++;
}

extern "C" void aidyn_music_add(uint8_t* rdram, recomp_context* ctx) {
    if (music_log() == nullptr) return;
    const gpr sp = ctx->r29, sample = ctx->r6;
    add_index = ctx->r4;
    add_id = ctx->r5;
    std::snprintf(add_line, sizeof add_line, "%08X %u %u %u %u %u %u %u %u %u", (uint32_t)sample,
                  (uint32_t)MEM_W(4, sample), (uint32_t)MEM_W(8, sample), (uint32_t)MEM_W(0, sample),
                  (uint32_t)MEM_W(0x10, sp), (uint32_t)MEM_W(0x14, sp), MEM_BU(0x1B, sp), MEM_HU(0x1E, sp),
                  MEM_BU(0x23, sp), MEM_BU(0x27, sp));
}

extern "C" void aidyn_music_added(uint8_t* rdram, recomp_context* ctx) {
    if (music_log() == nullptr) return;
    std::fprintf(music_log(), "%llu add %u %u %s\n", (unsigned long long)ms, MEM_BU(0, add_index),
                 (uint32_t)MEM_W(0, add_id), add_line);
}

extern "C" void aidyn_music_set(uint8_t* rdram, recomp_context* ctx, const char* what) {
    if (music_log() == nullptr) return;
    std::fprintf(music_log(), "%llu %s %u %u %u\n", (unsigned long long)ms, what, (uint32_t)(ctx->r4 & 0xFF),
                 (uint32_t)ctx->r5, (uint32_t)(ctx->r6 & 0xFFFF));
}

extern "C" void aidyn_music_stop(uint8_t* rdram, recomp_context* ctx) {
    if (music_log() == nullptr) return;
    std::fprintf(music_log(), "%llu stop %u %u\n", (unsigned long long)ms, (uint32_t)(ctx->r4 & 0xFF), (uint32_t)ctx->r5);
}
