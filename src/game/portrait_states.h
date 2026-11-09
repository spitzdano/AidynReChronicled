// portrait_states.h: written by tools/hdui/portrait_states.py (don't edit by hand).
// The party's portraits (by the FNV-1a hash of their CI8 pixels with the middle texel as 0) and which damage states have a
// picture painted (bit n of `states` for state n, 1 to 4): src/game/patches.cpp, "Portrait states".
#pragma once
#include <cstdint>

struct PortraitStateInfo {
    uint64_t hash;
    uint16_t w, h;
    uint8_t states;
    const char* name;
};

constexpr PortraitStateInfo kPortraitStates[] = {
    { 0x84F4294D132DBBC2ull, 40, 40, 0x00, "PortraitAbrecan" },
    { 0x368C0689F865FA67ull, 40, 40, 0x1E, "PortraitAlaron" },
    { 0x6A411BC6D00CA7DAull, 40, 40, 0x1E, "PortraitArturo" },
    { 0xB10A6F568C90D6ACull, 40, 40, 0x1E, "PortraitBaird" },
    { 0x3CFD7371B1B896E5ull, 40, 40, 0x1E, "PortraitBecan" },
    { 0xAA06A7635335A047ull, 40, 40, 0x1E, "PortraitBrenna" },
    { 0xAE5128208B405121ull, 40, 40, 0x1E, "PortraitDonovan" },
    { 0x12BD1FB4834CC007ull, 40, 40, 0x00, "PortraitDougal" },
    { 0x561629421038040Aull, 40, 40, 0x1E, "PortraitGodrik" },
    { 0x518BCE9CAF8C6320ull, 40, 40, 0x1E, "PortraitKeelin" },
    { 0x5719B259BADDDAF9ull, 40, 40, 0x1E, "PortraitNiesen" },
    { 0xAF3474A142CFD2D0ull, 40, 40, 0x1E, "PortraitRheda" },
    { 0xA7050C85F950EC9Aull, 40, 40, 0x1E, "PortraitSholeh" },
};
