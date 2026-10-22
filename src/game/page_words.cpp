// Words painted into page pictures, redrawn whatever HD menu pack is installed (or none).
//
// The inventory page's HitPoints, Level, Gold and DETAILS (and its C-down button) are part of the page picture, not
// text. The HD menu pack (hd-ui) redraws them in the text font, but it is made from the player's own ROM and never
// ships, so an older copy, or none, kept the old lettering. The release's rechronicled-text pack holds what goes on
// top (tools/hdui/text_pack.py: page-<name>.png, the new words and the Xbox button at 4x on nothing) and
// page-words.json (each page's FNV-1a hash of pixels and palette, and the boxes to erase). When such a page is drawn
// it is noted (N64BorgImageDraw's entry, aidyn_page_words_drawn); then, for each of its strips RT64 loads that
// crosses a box, this makes the replacement: the hd-ui pack's picture for that strip if there is one, else the
// game's own pixels upscaled; the boxes filled in from the parchment around them in the game's own picture (as
// tools/hdui/repaint.py erase does); the new words laid over. RT64 uses it in place of the pack's (save_pictures.cpp
// has the mechanism). An hd-ui pack whose aidyn-labels.txt matches page-words.json's id has these words already, and
// its pages are left alone. AIDYN_PAGE_WORDS_LOG=1 prints each step.
#include "platform.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "json/json.hpp"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

#include "packs.h"
#include "rt64_context.h"
#include "settings.h"

#include "stb/stb_image.h"  // decoding comes with RT64 (render/rt64_texture_cache.cpp)

namespace {

struct Page {
    std::string name;
    uint32_t format = 0, width = 0, height = 0;
    uint64_t fnv = 0;
    std::vector<std::array<int, 4>> erase;  // x, y, w, h in texels
    std::vector<uint8_t> words;            // RGBA at `scale` pixels per texel, width*scale x height*scale
    bool painted = false;                  // the redrawn art pack paints the whole page, words and all
};

struct Setup {
    bool on = false;
    int scale = 4;
    std::vector<Page> pages;
    bool hd_ui = false;  // the hd-ui pack is there: its pictures are what the strips start from
};

// A page drawn lately: where its pixels and palette are, and its picture decoded, with the boxes erased.
struct Shown {
    uint32_t pixels = 0, palette = 0;
    size_t page = 0;
    std::vector<uint8_t> original, erased;  // RGBA, width x height
};

std::mutex g_mutex;
uint8_t* g_rdram = nullptr;
std::deque<Shown> g_shown;

bool log_on() {
    static const bool on = std::getenv("AIDYN_PAGE_WORDS_LOG") != nullptr;
    return on;
}

// A pack named in texture_packs (folder or .rtz, src/main/packs.cpp).
bool listed(const std::string& name) {
    const auto& packs = aidyn::settings::graphics().texture_packs;
    return std::find(packs.begin(), packs.end(), name) != packs.end() && !aidyn::packs::find(name).empty();
}

const Setup& setup() {
    static const Setup s = [] {
        Setup s;
        std::vector<uint8_t> bytes;
        if (!listed("rechronicled-text") || !aidyn::packs::read("rechronicled-text", "page-words.json", bytes)) {
            return s;
        }
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(bytes.begin(), bytes.end());
        } catch (const std::exception& e) {
            fprintf(stderr, "Page words: rechronicled-text's page-words.json: %s\n", e.what());
            return s;
        }
        const std::string id = j.value("labels", "");
        s.scale = j.value("scale", 4);
        s.hd_ui = listed("hd-ui");
        if (s.hd_ui && aidyn::packs::first_word("hd-ui", "aidyn-labels.txt") == id) {
            if (log_on()) {
                fprintf(stdout, "Page words: hd-ui has them already\n");
            }
            return s;  // the pack's pages have these words
        }
        // The pages the redrawn art pack paints whole, its words drawn in (tools/hdui/scroll_pages.py; art_pack.py
        // lists them in aidyn-pages.txt): left to it while it's on (F2 turns it off: then they're redrawn here).
        std::vector<std::string> painted;
        if (listed("rechronicled-art") && aidyn::packs::read("rechronicled-art", "aidyn-pages.txt", bytes)) {
            std::string word;
            for (uint8_t c : bytes) {
                if (std::isspace(c)) {
                    if (!word.empty()) {
                        painted.push_back(word);
                    }
                    word.clear();
                } else {
                    word += (char)c;
                }
            }
            if (!word.empty()) {
                painted.push_back(word);
            }
        }
        for (const auto& p : j.value("pages", nlohmann::json::array())) {
            Page page;
            page.name = p.value("name", "");
            page.format = p.value("format", 0u);
            page.width = p.value("width", 0u);
            page.height = p.value("height", 0u);
            page.fnv = std::strtoull(p.value("fnv", "0").c_str(), nullptr, 16);
            for (const auto& b : p.value("erase", nlohmann::json::array())) {
                page.erase.push_back({ b[0].get<int>(), b[1].get<int>(), b[2].get<int>(), b[3].get<int>() });
            }
            page.painted = std::find(painted.begin(), painted.end(), page.name) != painted.end();
            const std::string words = p.value("words", "");
            int w = 0, h = 0;
            stbi_uc* data = aidyn::packs::read("rechronicled-text", words, bytes)
                ? stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, nullptr, 4) : nullptr;
            if (data == nullptr || page.format != 4 || (uint32_t)w != page.width * s.scale
                || (uint32_t)h != page.height * s.scale) {
                fprintf(stderr, "Page words: %s doesn't fit its page (skipped)\n", words.c_str());
                if (data != nullptr) {
                    stbi_image_free(data);
                }
                continue;
            }
            page.words.assign(data, data + size_t(w) * h * 4);
            stbi_image_free(data);
            s.pages.push_back(std::move(page));
        }
        s.on = !s.pages.empty();
        fprintf(stdout, "Page words: %zu page(s) redrawn over %s\n", s.pages.size(),
                s.hd_ui ? "hd-ui's pictures" : "the game's own pictures");
        return s;
    }();
    return s;
}

// FNV-1a over a CI8 page's pixels and its 256-colour palette, as text_pack.py hashes them.
uint64_t page_fnv(uint8_t* rdram, gpr pixels, gpr palette, uint32_t count) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < count; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    for (uint32_t i = 0; i < 512; i++) {
        hash = (hash ^ MEM_BU(i, palette)) * 0x100000001B3ULL;
    }
    return hash;
}

// RGBA16 (5551) to RGBA8, as RT64 decodes it (and tools/hdui/pngio.py).
void rgba16(uint16_t v, uint8_t* out) {
    const uint8_t r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31;
    out[0] = (uint8_t)((r << 3) | (r >> 2));
    out[1] = (uint8_t)((g << 3) | (g >> 2));
    out[2] = (uint8_t)((b << 3) | (b >> 2));
    out[3] = (v & 1) ? 255 : 0;
}

// The boxes filled in from the picture around them, edges inwards (tools/hdui/repaint.py erase).
void erase(std::vector<uint8_t>& rgba, int w, int h, const std::vector<std::array<int, 4>>& boxes) {
    std::vector<uint8_t> todo(size_t(w) * h, 0);
    for (const auto& b : boxes) {
        for (int y = std::max(0, b[1]); y < std::min(h, b[1] + b[3]); y++) {
            for (int x = std::max(0, b[0]); x < std::min(w, b[0] + b[2]); x++) {
                todo[size_t(y) * w + x] = 1;
            }
        }
    }
    for (;;) {
        std::vector<std::pair<size_t, std::array<uint8_t, 3>>> done;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (!todo[size_t(y) * w + x]) {
                    continue;
                }
                int acc[3] = {}, k = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int qx = x + dx, qy = y + dy;
                        if ((dx || dy) && qx >= 0 && qx < w && qy >= 0 && qy < h && !todo[size_t(qy) * w + qx]) {
                            const uint8_t* q = &rgba[(size_t(qy) * w + qx) * 4];
                            acc[0] += q[0];
                            acc[1] += q[1];
                            acc[2] += q[2];
                            k++;
                        }
                    }
                }
                if (k >= 2) {
                    done.push_back({ size_t(y) * w + x, { (uint8_t)(acc[0] / k), (uint8_t)(acc[1] / k), (uint8_t)(acc[2] / k) } });
                }
            }
        }
        if (done.empty()) {
            break;
        }
        for (const auto& [i, c] : done) {
            std::memcpy(&rgba[i * 4], c.data(), 3);
            todo[i] = 0;
        }
    }
}

// Bilinear sample of a w x h RGBA picture at texel coordinates (texel centres at +0.5), clamped at the edges.
void sample(const std::vector<uint8_t>& rgba, int w, int h, float fx, float fy, float* out) {
    fx = std::clamp(fx - 0.5f, 0.0f, (float)(w - 1));
    fy = std::clamp(fy - 0.5f, 0.0f, (float)(h - 1));
    const int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float ax = fx - x0, ay = fy - y0;
    for (int c = 0; c < 4; c++) {
        const float top = rgba[(size_t(y0) * w + x0) * 4 + c] * (1 - ax) + rgba[(size_t(y0) * w + x1) * 4 + c] * ax;
        const float bottom = rgba[(size_t(y1) * w + x0) * 4 + c] * (1 - ax) + rgba[(size_t(y1) * w + x1) * 4 + c] * ax;
        out[c] = top * (1 - ay) + bottom * ay;
    }
}

} // namespace

// N64BorgImageDraw's entry (src/game/patches.cpp aidyn_ui_image_seen): a page picture with words to redraw is noted
// (and decoded, its boxes erased, once per place it is loaded at).
void aidyn_page_words_drawn(uint8_t* rdram, gpr img) {
    const Setup& s = setup();
    if (!s.on) {
        return;
    }
    const uint32_t format = MEM_HU(0x08, img), w = MEM_HU(0x0A, img), h = MEM_HU(0x0C, img);
    for (size_t i = 0; i < s.pages.size(); i++) {
        const Page& page = s.pages[i];
        if (format != page.format || w != page.width || h != page.height || (page.painted && aidyn::redrawn_art())) {
            continue;
        }
        const gpr pixels = MEM_W(0x14, img), palette = MEM_W(0x10, img);
        if (pixels == 0 || palette == 0 || page_fnv(rdram, pixels, palette, w * h) != page.fnv) {
            continue;
        }
        std::lock_guard lock(g_mutex);
        g_rdram = rdram;
        const uint32_t address = (uint32_t)pixels & 0x1FFFFFFF, pal = (uint32_t)palette & 0x1FFFFFFF;
        for (const Shown& shown : g_shown) {
            if (shown.pixels == address && shown.palette == pal && shown.page == i) {
                return;
            }
        }
        Shown shown{ address, pal, i, {}, {} };
        shown.original.resize(size_t(w) * h * 4);
        for (uint32_t t = 0; t < w * h; t++) {
            rgba16(MEM_HU(MEM_BU(t, pixels) * 2, palette), &shown.original[size_t(t) * 4]);
        }
        shown.erased = shown.original;
        erase(shown.erased, (int)w, (int)h, page.erase);
        g_shown.push_back(std::move(shown));
        while (g_shown.size() > 4) {
            g_shown.pop_front();
        }
        if (log_on()) {
            fprintf(stdout, "Page words: %s drawn from %06X\n", page.name.c_str(), address);
        }
        return;
    }
}

// RT64, for each new texture (src/main/rt64_context.cpp): a strip of a noted page that crosses a box gets the page's
// picture (hd-ui's for this strip, else the game's own upscaled), the boxes erased and the new words over them.
bool aidyn::page_words_for_texture(uint64_t hash, uint32_t address, uint16_t width, uint16_t height, uint8_t fmt,
                                   uint8_t siz, std::vector<uint8_t>& rgba, uint32_t& out_width, uint32_t& out_height) {
    if (fmt != 2 || siz != 1 || width == 0 || height == 0) {  // G_IM_FMT_CI, G_IM_SIZ_8b
        return false;
    }
    const Setup& s = setup();
    if (!s.on) {
        return false;
    }
    std::lock_guard lock(g_mutex);
    uint8_t* rdram = g_rdram;
    for (const Shown& shown : g_shown) {
        const Page& page = s.pages[shown.page];
        if (page.painted && aidyn::redrawn_art()) {
            continue;  // the redrawn art pack's painting of the page, words and all
        }
        const int pw = (int)page.width, ph = (int)page.height, S = s.scale;
        if (address < shown.pixels || address >= shown.pixels + uint32_t(pw * ph)) {
            continue;
        }
        if (rdram == nullptr || page_fnv(rdram, (gpr)(int32_t)(shown.pixels | 0x80000000),
                                         (gpr)(int32_t)(shown.palette | 0x80000000), uint32_t(pw * ph)) != page.fnv) {
            return false;
        }
        const int offset = int(address - shown.pixels), row0 = offset / pw, col0 = offset % pw;
        if (col0 + width > pw) {
            return false;
        }
        bool crosses = false;
        for (const auto& b : page.erase) {  // with the 2 texels around them (below)
            crosses |= b[1] - 2 < row0 + height && b[1] + b[3] + 2 > row0 && b[0] - 2 < col0 + width && b[0] + b[2] + 2 > col0;
        }
        if (!crosses) {
            return false;
        }
        const int OW = width * S, OH = height * S;
        rgba.assign(size_t(OW) * OH * 4, 0);
        // The picture to start from: hd-ui's for this strip, else the game's own, upscaled.
        bool from_pack = false;
        std::vector<uint8_t> png;
        if (s.hd_ui) {
            char name[32];
            snprintf(name, sizeof(name), "%016llx.png", (unsigned long long)hash);
            int w = 0, h = 0;
            stbi_uc* data = aidyn::packs::read("hd-ui", name, png)
                ? stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, nullptr, 4) : nullptr;
            if (data != nullptr && w == OW && h == OH) {
                std::memcpy(rgba.data(), data, rgba.size());
                from_pack = true;
            }
            if (data != nullptr) {
                stbi_image_free(data);
            }
        }
        float px[4];
        if (!from_pack) {
            for (int y = 0; y < OH; y++) {
                for (int x = 0; x < OW; x++) {
                    sample(shown.original, pw, ph, col0 + (x + 0.5f) / S, row0 + (y + 0.5f) / S, px);
                    for (int c = 0; c < 4; c++) {
                        rgba[(size_t(y) * OW + x) * 4 + c] = (uint8_t)std::lround(px[c]);
                    }
                }
            }
        }
        // The boxes from the erased picture, 2 texels wider all round (an upscaled pack's old letters glow a little past
        // them) and blended in over those 2 texels.
        constexpr int kMargin = 2;
        for (const auto& b : page.erase) {
            const int bx0 = (b[0] - kMargin - col0) * S, by0 = (b[1] - kMargin - row0) * S;
            const int bx1 = bx0 + (b[2] + 2 * kMargin) * S, by1 = by0 + (b[3] + 2 * kMargin) * S;
            for (int y = std::max(0, by0); y < std::min(OH, by1); y++) {
                for (int x = std::max(0, bx0); x < std::min(OW, bx1); x++) {
                    const int d = std::min({ x - bx0, bx1 - 1 - x, y - by0, by1 - 1 - y });
                    const float a = std::clamp((d + 1) / float(kMargin * S), 0.0f, 1.0f);
                    sample(shown.erased, pw, ph, col0 + (x + 0.5f) / S, row0 + (y + 0.5f) / S, px);
                    uint8_t* o = &rgba[(size_t(y) * OW + x) * 4];
                    for (int c = 0; c < 3; c++) {
                        o[c] = (uint8_t)std::lround(o[c] * (1 - a) + px[c] * a);
                    }
                }
            }
        }
        // The new words over them.
        const int LW = pw * S, LH = ph * S;
        for (int y = 0; y < OH; y++) {
            const int ly = row0 * S + y;
            if (ly >= LH) {
                break;
            }
            for (int x = 0; x < OW; x++) {
                const uint8_t* src = &page.words[(size_t(ly) * LW + col0 * S + x) * 4];
                const float a = src[3] / 255.0f;
                if (a == 0.0f) {
                    continue;
                }
                uint8_t* o = &rgba[(size_t(y) * OW + x) * 4];
                for (int c = 0; c < 3; c++) {
                    o[c] = (uint8_t)std::lround(src[c] * a + o[c] * (1 - a));
                }
            }
        }
        out_width = (uint32_t)OW;
        out_height = (uint32_t)OH;
        if (log_on()) {
            fprintf(stdout, "Page words: %s rows %d..%d (%016llx) from %s\n", page.name.c_str(), row0, row0 + height,
                    (unsigned long long)hash, from_pack ? "hd-ui" : "the game's picture");
        }
        return true;
    }
    return false;
}
