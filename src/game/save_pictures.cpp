// Save pictures in full resolution.
//
// The game keeps a 64x32 picture with each save: QuestData::TakeSnapshot point-samples the middle half of the last
// frame (x 80..240, y 60..180 of 320x240) when the pause menu opens, and CopySnapshot puts those 4 KB into the save on
// the Controller Pak. The Load and Save screens stretch it into a 4:3 box, so it was always a smudge.
//
// Here, as the game takes its picture, RT64 copies the same area out of the full-resolution frame it rendered (see
// tools/patches/rt64-save-pictures.patch; square pixels around the same centre, so a widened 21:9 frame isn't
// squeezed). Saving writes that copy beside the pack as save-pictures/<key>.jpg, the key being an FNV-1a hash of the
// game's own 4 KB picture, which travels inside the save. When a Load or Save screen draws a 64x32 picture whose key
// has a copy, RT64 is told where in RDRAM it is; the texture RT64 loads from there gets the copy as its replacement,
// like a texture pack's, so the menus draw, place and fade it as before. Saves without a copy (made before this, or
// on another machine) keep the small picture.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

#include "rt64_context.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb/stb_image_write.h"
#include "stb/stb_image.h"  // decoding comes with RT64 (render/rt64_texture_cache.cpp)

namespace {

constexpr gpr kSnapshotPointer = (gpr)(int32_t)0x800DA4F0;  // gQuestdataSnapshot
constexpr gpr kGfxManager = (gpr)(int32_t)0x800D7ED8;       // gGfxManager: FrameBuffers +0x18, bufferChoice +0x19A
constexpr uint32_t kWidth = 64, kHeight = 32, kBytes = kWidth * kHeight * 2;  // RGBA16
constexpr uint32_t kMaxLines = 720;  // copies are averaged down to at most this many lines
constexpr size_t kKeptInMemory = 6;
constexpr size_t kKeptOnDisk = 64;

struct Picture {
    std::vector<uint8_t> rgba;
    uint32_t width = 0, height = 0;
};

// A 64x32 picture drawn lately: where its pixels are (physical) and its key.
struct Shown {
    uint32_t address;
    uint64_t key;
};

std::mutex g_mutex;
std::filesystem::path g_dir;
uint8_t* g_rdram = nullptr;
std::map<uint64_t, std::shared_ptr<const Picture>> g_pictures;
std::deque<uint64_t> g_recent;          // g_pictures' keys, oldest first
std::set<uint64_t> g_missing;           // keys looked for on disk without a file
std::deque<Shown> g_shown;

bool log_on() {
    static const bool on = std::getenv("AIDYN_PICTURE_LOG") != nullptr;
    return on;
}

uint64_t picture_key(uint8_t* rdram, gpr pixels) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < kBytes; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    return hash;
}

std::filesystem::path file_for(uint64_t key) {
    char name[32];
    snprintf(name, sizeof(name), "%016llx.jpg", (unsigned long long)key);
    return g_dir / name;
}

void keep_locked(uint64_t key, std::shared_ptr<const Picture> picture) {
    g_pictures[key] = std::move(picture);
    g_missing.erase(key);
    for (auto it = g_recent.begin(); it != g_recent.end(); ++it) {
        if (*it == key) {
            g_recent.erase(it);
            break;
        }
    }
    g_recent.push_back(key);
    while (g_recent.size() > kKeptInMemory) {
        g_pictures.erase(g_recent.front());
        g_recent.pop_front();
    }
}

// The copy for a key, from memory or from disk (decoded once). Null if there is none.
std::shared_ptr<const Picture> find_locked(uint64_t key) {
    if (auto it = g_pictures.find(key); it != g_pictures.end()) {
        return it->second;
    }
    if (g_dir.empty() || g_missing.count(key) != 0) {
        return nullptr;
    }
    const std::string path = file_for(key).string();
    int w = 0, h = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, nullptr, 4);
    if (data == nullptr) {
        g_missing.insert(key);
        return nullptr;
    }
    auto picture = std::make_shared<Picture>();
    picture->rgba.assign(data, data + size_t(w) * h * 4);
    picture->width = (uint32_t)w;
    picture->height = (uint32_t)h;
    stbi_image_free(data);
    keep_locked(key, picture);
    if (log_on()) {
        fprintf(stdout, "Save picture %016llx: read %s (%dx%d)\n", (unsigned long long)key, path.c_str(), w, h);
    }
    return picture;
}

// The copy averaged down by a whole factor to at most kMaxLines lines (RT64 may render at twice the window's height:
// at the menus' size a full-size copy would shimmer, and it costs memory and disk for nothing).
std::shared_ptr<Picture> shrink(std::vector<uint8_t>&& rgba, uint32_t width, uint32_t height) {
    auto picture = std::make_shared<Picture>();
    const uint32_t f = (height + kMaxLines - 1) / kMaxLines;
    if (f <= 1) {
        picture->rgba = std::move(rgba);
        picture->width = width;
        picture->height = height;
        return picture;
    }
    picture->width = width / f;
    picture->height = height / f;
    picture->rgba.resize(size_t(picture->width) * picture->height * 4);
    for (uint32_t y = 0; y < picture->height; y++) {
        for (uint32_t x = 0; x < picture->width; x++) {
            uint32_t sum[3] = {};
            for (uint32_t dy = 0; dy < f; dy++) {
                const uint8_t* src = &rgba[(size_t(y * f + dy) * width + x * f) * 4];
                for (uint32_t dx = 0; dx < f; dx++) {
                    sum[0] += src[dx * 4 + 0];
                    sum[1] += src[dx * 4 + 1];
                    sum[2] += src[dx * 4 + 2];
                }
            }
            uint8_t* dst = &picture->rgba[(size_t(y) * picture->width + x) * 4];
            for (int c = 0; c < 3; c++) {
                dst[c] = (uint8_t)((sum[c] + f * f / 2) / (f * f));
            }
            dst[3] = 255;
        }
    }
    return picture;
}

// Writes the copy (on its own thread: a JPEG takes a few tens of milliseconds), then keeps the folder to the newest
// kKeptOnDisk pictures.
void write_picture(uint64_t key, std::shared_ptr<const Picture> picture, std::filesystem::path path) {
    std::thread([key, picture, path]() {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        const std::filesystem::path tmp = path.string() + ".tmp";
        if (!stbi_write_jpg(tmp.c_str(), (int)picture->width, (int)picture->height, 4, picture->rgba.data(), 95)) {
            fprintf(stderr, "Save picture: could not write %s\n", tmp.c_str());
            return;
        }
        std::filesystem::rename(tmp, path, ec);
        if (log_on()) {
            fprintf(stdout, "Save picture %016llx: wrote %s\n", (unsigned long long)key, path.c_str());
        }
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path(), ec)) {
            if (entry.path().extension() == ".jpg") {
                files.emplace_back(entry.last_write_time(ec), entry.path());
            }
        }
        if (files.size() > kKeptOnDisk) {
            std::sort(files.begin(), files.end());
            for (size_t i = 0; i + kKeptOnDisk < files.size(); i++) {
                std::filesystem::remove(files[i].second, ec);
            }
        }
    }).detach();
}

} // namespace

// Where the copies go: save-pictures beside the pack. Called once by main() before the game starts.
void aidyn_save_pictures_set_dir(const std::filesystem::path& dir) {
    std::lock_guard lock(g_mutex);
    g_dir = dir;
}

// QuestData::TakeSnapshot right after getGfxLastFrame (0x8001A400): the game's picture is in gQuestdataSnapshot, the
// area it was taken from in the call's stack arguments (sp + 0x10..0x1C: left, top, right, bottom), from the
// framebuffer gGfxManager.FrameBuffers[bufferChoice]. RT64 copies the same area before it draws the next frame.
extern "C" void aidyn_save_picture_taken(uint8_t* rdram, recomp_context* ctx) {
    const gpr snapshot = MEM_W(0, kSnapshotPointer);
    if (snapshot == 0) {
        return;
    }
    const uint64_t key = picture_key(rdram, snapshot);
    const gpr sp = ctx->r29;
    const float left = (float)(MEM_W(0x10, sp) & 0xFFFF), top = (float)(MEM_W(0x14, sp) & 0xFFFF);
    const float right = (float)(MEM_W(0x18, sp) & 0xFFFF), bottom = (float)(MEM_W(0x1C, sp) & 0xFFFF);
    const uint32_t fb = (uint32_t)MEM_W(0x18 + 4 * MEM_BU(0x19A, kGfxManager), kGfxManager) & 0x1FFFFFFF;
    {
        std::lock_guard lock(g_mutex);
        g_rdram = rdram;
    }
    aidyn::capture_framebuffer(fb, left, top, right, bottom,
                               [key](std::vector<uint8_t>&& rgba, uint32_t width, uint32_t height) {
        if (rgba.empty()) {
            if (log_on()) {
                fprintf(stdout, "Save picture %016llx: nothing to copy\n", (unsigned long long)key);
            }
            return;
        }
        std::shared_ptr<Picture> picture = shrink(std::move(rgba), width, height);
        if (log_on()) {
            fprintf(stdout, "Save picture %016llx: copied %ux%u, kept %ux%u\n", (unsigned long long)key, width, height,
                    picture->width, picture->height);
        }
        std::lock_guard lock(g_mutex);
        keep_locked(key, std::move(picture));
    });
    if (log_on()) {
        fprintf(stdout, "Save picture %016llx: taken from %06X (%.0f,%.0f to %.0f,%.0f)\n", (unsigned long long)key, fb,
                left, top, right, bottom);
    }
}

// CopySnapshot's entry (func_80019EF0): the picture goes into a save, so its copy goes to disk.
extern "C" void aidyn_save_picture_saved(uint8_t* rdram, recomp_context* ctx) {
    const gpr snapshot = MEM_W(0, kSnapshotPointer);
    if (snapshot == 0) {
        return;
    }
    const uint64_t key = picture_key(rdram, snapshot);
    std::lock_guard lock(g_mutex);
    auto it = g_pictures.find(key);
    if (it == g_pictures.end() || g_dir.empty()) {
        if (log_on()) {
            fprintf(stdout, "Save picture %016llx: saved without a copy\n", (unsigned long long)key);
        }
        return;
    }
    write_picture(key, it->second, file_for(key));
}

// N64BorgImageDraw's entry (src/game/patches.cpp aidyn_ui_image_seen): a 64x32 RGBA16 picture with a copy is noted,
// so the texture RT64 loads from it gets the copy.
void aidyn_save_picture_drawn(uint8_t* rdram, gpr img) {
    if (MEM_HU(0x08, img) != 2 || MEM_HU(0x0A, img) != kWidth || MEM_HU(0x0C, img) != kHeight) {
        return;
    }
    const gpr pixels = MEM_W(0x14, img);
    if (pixels == 0) {
        return;
    }
    const uint64_t key = picture_key(rdram, pixels);
    const uint32_t address = (uint32_t)pixels & 0x1FFFFFFF;
    std::lock_guard lock(g_mutex);
    g_rdram = rdram;
    for (const Shown& shown : g_shown) {
        if (shown.address == address && shown.key == key) {
            return;
        }
    }
    if (find_locked(key) == nullptr) {
        return;
    }
    for (auto it = g_shown.begin(); it != g_shown.end(); ++it) {
        if (it->address == address) {
            g_shown.erase(it);
            break;
        }
    }
    g_shown.push_back({ address, key });
    while (g_shown.size() > 8) {
        g_shown.pop_front();
    }
    if (log_on()) {
        fprintf(stdout, "Save picture %016llx: drawn from %06X\n", (unsigned long long)key, address);
    }
}

// RT64, for each new texture (src/main/rt64_context.cpp): when its data comes from a noted picture that still holds
// the same pixels, the matching part of the copy (all of it for a whole 64x32 load).
bool aidyn::save_picture_for_texture(uint32_t address, uint16_t width, uint16_t height, uint8_t fmt, uint8_t siz,
                                     std::vector<uint8_t>& rgba, uint32_t& out_width, uint32_t& out_height) {
    if (fmt != 0 || siz != 2 || width == 0 || height == 0) {  // G_IM_FMT_RGBA, G_IM_SIZ_16b
        return false;
    }
    std::lock_guard lock(g_mutex);
    uint8_t* rdram = g_rdram;
    if (rdram == nullptr) {
        return false;
    }
    for (const Shown& shown : g_shown) {
        if (address < shown.address || address >= shown.address + kBytes) {
            continue;
        }
        if (picture_key(rdram, (gpr)(int32_t)(shown.address | 0x80000000)) != shown.key) {
            return false;
        }
        auto it = g_pictures.find(shown.key);
        if (it == g_pictures.end()) {
            return false;
        }
        const Picture& picture = *it->second;
        const uint32_t offset = address - shown.address;
        const uint32_t row = offset / (kWidth * 2), column = (offset % (kWidth * 2)) / 2;
        const uint32_t x0 = column * picture.width / kWidth;
        const uint32_t y0 = row * picture.height / kHeight;
        const uint32_t x1 = std::min<uint32_t>((column + width) * picture.width / kWidth, picture.width);
        const uint32_t y1 = std::min<uint32_t>((row + height) * picture.height / kHeight, picture.height);
        if (x1 <= x0 || y1 <= y0) {
            return false;
        }
        out_width = x1 - x0;
        out_height = y1 - y0;
        rgba.resize(size_t(out_width) * out_height * 4);
        for (uint32_t y = 0; y < out_height; y++) {
            std::memcpy(&rgba[size_t(y) * out_width * 4], &picture.rgba[(size_t(y0 + y) * picture.width + x0) * 4],
                        size_t(out_width) * 4);
        }
        if (log_on()) {
            fprintf(stdout, "Save picture %016llx: replaces a %ux%u texture at %06X with %ux%u\n",
                    (unsigned long long)shown.key, width, height, address, out_width, out_height);
        }
        return true;
    }
    return false;
}

// Problem reports (src/game/patches.cpp, "Report a problem"): the full-resolution copy of the picture
// gQuestdataSnapshot holds (the pause menu's, of the screen as it opened) as a JPEG. False if there is none.
bool aidyn_save_picture_jpeg(uint8_t* rdram, std::vector<uint8_t>& jpeg) {
    const gpr snapshot = MEM_W(0, kSnapshotPointer);
    if (snapshot == 0) {
        return false;
    }
    const uint64_t key = picture_key(rdram, snapshot);
    std::shared_ptr<const Picture> picture;
    {
        std::lock_guard lock(g_mutex);
        picture = find_locked(key);
    }
    if (picture == nullptr) {
        return false;
    }
    jpeg.clear();
    return stbi_write_jpg_to_func([](void* out, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(out);
        v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }, &jpeg, (int)picture->width, (int)picture->height, 4, picture->rgba.data(), 90) != 0;
}
