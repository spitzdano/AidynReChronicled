// tmemhash: RT64's texture hash (RT64::TMEMHasher, the key of a texture-pack replacement) for textures described on
// stdin, so tools/hdui can name replacements for images the game hasn't drawn yet.
//
// Input, one texture per line: <tmem file: 4096 bytes> fmt siz line tmem palette width height tlut
// (tlut: the other-mode TLUT bits RT64 passes: 0 none, G_TT_RGBA16 = 0x8000, G_TT_IA16 = 0xC000).
// Output: one 16-digit hex hash per line, in the same order.
#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "xxHash/xxh3.h"
#include "common/rt64_load_types.h"
#include "shared/rt64_f3d_defines.h"
#include "common/rt64_tmem_hasher.h"

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream in(line);
        std::string path;
        unsigned fmt, siz, lineWords, tmem, palette, width, height, tlut;
        if (!(in >> path >> fmt >> siz >> lineWords >> tmem >> palette >> width >> height >> tlut)) {
            fprintf(stderr, "tmemhash: bad line: %s\n", line.c_str());
            return 1;
        }
        std::vector<uint8_t> TMEM(4096);
        std::ifstream f(path, std::ios::binary);
        if (!f.read(reinterpret_cast<char*>(TMEM.data()), TMEM.size())) {
            fprintf(stderr, "tmemhash: cannot read 4096 bytes from %s\n", path.c_str());
            return 1;
        }
        RT64::LoadTile tile{};
        tile.fmt = uint8_t(fmt);
        tile.siz = uint8_t(siz);
        tile.line = uint16_t(lineWords);
        tile.tmem = uint16_t(tmem);
        tile.palette = uint8_t(palette);
        uint64_t hash = RT64::TMEMHasher::hash(TMEM.data(), tile, uint16_t(width), uint16_t(height), tlut,
                                               RT64::TMEMHasher::CurrentHashVersion);
        printf("%016" PRIx64 "\n", hash);
    }
    return 0;
}
