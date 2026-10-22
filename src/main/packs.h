// Texture packs as the game finds them (src/main/packs.cpp): a folder or a .rtz (RT64's zipped pack, which releases
// ship: one file loads in a fraction of the time 40,000 do, on Windows above all), in <data>/texture-packs, then in
// texture-packs next to the executable.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aidyn::packs {
    // Where the pack of that name is (its folder or its .rtz), or empty.
    std::filesystem::path find(const std::string& name);
    // A file in the pack (a path inside it, with /), from its folder or its .rtz. False if the pack or file isn't there.
    bool read(const std::string& name, const std::string& file, std::vector<uint8_t>& out);
    // The first word of a text file in the pack (the packs' id markers), or empty.
    std::string first_word(const std::string& name, const std::string& file);
}
