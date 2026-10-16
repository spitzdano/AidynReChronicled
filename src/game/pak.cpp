// Controller Pak (Phase 6): a 32 KiB memory pack in port 1, stored as aidyn.mpk in the save folder.
//
// Only the two raw block accesses are replaced. The game's own copies of libultra's file-system code
// (osPfsInitPak, osPfsAllocateFile, osPfsReadWriteFile, ... renamed aidyn_osPfs* by tools/symbol_renames.txt so
// N64Recomp recompiles them instead of using the runtime's "no pak" versions) run on top, so the on-pack format,
// the free-space checks and the error codes are the game's own. The image is the standard libultra layout:
// 1024 blocks of 32 bytes; block 1 (and copies at 3, 4, 6) is the ID, blocks 8..15 and 16..23 the inode table
// and its backup, blocks 24..39 the 16-entry note directory, pages 5..127 the data.
//
// Every write is flushed to disk at once (temp file + rename), so a crash never leaves a torn file.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <random>
#include <regex>
#include <string>
#include <vector>

#include "ultramodern/ultra64.h"
#include "ultramodern/error_handling.hpp"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

#include "settings.h"

namespace {

constexpr size_t kBlock = 32;
constexpr size_t kBlocks = 1024;
constexpr size_t kImageSize = kBlock * kBlocks; // 32 KiB: one bank
constexpr int kErrNoPack = 1;                    // PFS_ERR_NOPACK

std::mutex g_mutex;
std::filesystem::path g_path;
std::array<uint8_t, kImageSize> g_image{};
bool g_loaded = false;

[[noreturn]] void fatal(const std::string& message) {
    std::string text = "Controller Pak: " + message;
    fprintf(stderr, "%s\n", text.c_str());
    fflush(stderr);
    ultramodern::error_handling::message_box(text.c_str()); // shows a dialog, then ends the process
    std::_Exit(1);
}

void put16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
void put32(uint8_t* p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xFFFF); }

// libultra's __osIdCheckSum: sum and inverted sum of the first 14 big-endian halfwords of an ID block.
void id_checksum(const uint8_t* id, uint16_t& sum, uint16_t& inverted) {
    sum = 0;
    inverted = 0;
    for (int i = 0; i < 28; i += 2) {
        uint16_t w = (uint16_t)((id[i] << 8) | id[i + 1]);
        sum += w;
        inverted += (uint16_t)~w;
    }
}

// A freshly formatted pack, as libultra's osPfsReFormat leaves one: valid ID blocks, empty directory, every
// data page free.
void format_image() {
    g_image.fill(0);
    std::random_device rd;
    uint8_t id[kBlock] = {};
    put32(id + 0x00, 0);                  // repaired
    put32(id + 0x04, rd());               // random
    for (int i = 0x08; i < 0x18; i += 4) {
        put32(id + i, rd());              // serial
    }
    put16(id + 0x18, 0x0001);             // device id: bit 0 = memory pack
    id[0x1A] = 1;                         // banks
    id[0x1B] = 1;                         // version
    uint16_t sum, inverted;
    id_checksum(id, sum, inverted);
    put16(id + 0x1C, sum);
    put16(id + 0x1E, inverted);
    for (size_t block : { 1, 3, 4, 6 }) {
        std::memcpy(&g_image[block * kBlock], id, kBlock);
    }

    constexpr int inode_start_page = 5;   // banks * 2 + 3
    uint8_t inode[256] = {};
    uint32_t pages = 0;
    for (int i = inode_start_page; i < 128; i++) {
        put16(inode + i * 2, 0x0003);     // free page
        pages += 0x03;
    }
    put16(inode, (uint16_t)pages);        // entry 0: checksum of the entries from inode_start_page on
    std::memcpy(&g_image[8 * kBlock], inode, sizeof(inode));   // inode table
    std::memcpy(&g_image[16 * kBlock], inode, sizeof(inode));  // its backup
}

void flush_locked() {
    std::filesystem::path tmp = g_path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(g_image.data()), g_image.size());
        out.flush();
        if (!out) {
            fatal("cannot write " + tmp.string());
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, g_path, ec);
    if (ec) {
        fatal("cannot replace " + g_path.string() + ": " + ec.message());
    }
}

// Backups (settings.json game.save_backups, default 20): a copy of the pack in <data>/backups/<name>-<date>-<time>.mpk
// when the game starts and before each save (the first block written after 30 s without writes copies the pack as it
// was before it). A copy the same as the newest one is skipped. Only the newest save_backups copies of this pack are
// kept. A failed copy is reported and the game goes on: the pack itself is still written.
std::chrono::steady_clock::time_point g_last_write{};
bool g_written = false;

void backup_locked(const char* why) {
    const int keep = aidyn::settings::game().save_backups;
    if (keep <= 0) {
        return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = g_path.parent_path() / "backups";
    fs::create_directories(dir, ec);
    const std::string stem = g_path.stem().string();
    const std::regex mine(stem + R"(-\d{8}-\d{6}(-\d+)?\.mpk)");
    std::vector<fs::path> copies;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (std::regex_match(e.path().filename().string(), mine)) {
            copies.push_back(e.path());
        }
    }
    std::sort(copies.begin(), copies.end());  // the names sort by time
    if (!copies.empty()) {
        std::ifstream in(copies.back(), std::ios::binary);
        std::array<uint8_t, kImageSize> last{};
        if (in.read(reinterpret_cast<char*>(last.data()), last.size()) && last == g_image) {
            return;
        }
    }
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    fs::path file = dir / (stem + "-" + stamp + ".mpk");
    for (int n = 2; fs::exists(file, ec); n++) {
        file = dir / (stem + "-" + stamp + "-" + std::to_string(n) + ".mpk");
    }
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(g_image.data()), g_image.size());
        if (!out) {
            fprintf(stderr, "Controller Pak: could not write the backup %s (the save goes ahead)\n", file.c_str());
            return;
        }
    }
    fprintf(stderr, "Controller Pak: backup %s (%s)\n", file.c_str(), why);
    copies.push_back(file);
    while ((int)copies.size() > keep) {
        fs::remove(copies.front(), ec);
        copies.erase(copies.begin());
    }
}

void load_locked() {
    if (g_loaded) {
        return;
    }
    if (g_path.empty()) {
        fatal("aidyn_pak_set_path was never called");
    }
    std::error_code ec;
    if (std::filesystem::exists(g_path, ec)) {
        // A pack from an emulator (.mpk) is the same raw 32 KiB image. Anything else is refused untouched.
        if (std::filesystem::file_size(g_path, ec) != kImageSize) {
            fatal(g_path.string() + " is not a 32 KiB Controller Pak image; move it away or fix it");
        }
        std::ifstream in(g_path, std::ios::binary);
        in.read(reinterpret_cast<char*>(g_image.data()), g_image.size());
        if (!in) {
            fatal("cannot read " + g_path.string());
        }
        backup_locked("game start");
    } else {
        format_image();
        flush_locked();
        fprintf(stderr, "Controller Pak: created %s\n", g_path.c_str());
    }
    g_loaded = true;
}

} // namespace

// Where the pack lives. Called once by main() before the game starts.
void aidyn_pak_set_path(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
}

// osPfsIsPlug(OSMesgQueue* mq, u8* pattern): bit n of *pattern is set when port n has a pack. Port 1 always does.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    PTR(uint8_t) pattern = _arg<1, PTR(uint8_t)>(rdram, ctx);
    MEM_B(0, pattern) = 1;
    ctx->r2 = 0;
}

// __osPfsGetStatus(OSMesgQueue* mq, int channel): the status query libultra runs before every pack access.
// 0 for port 1, PFS_ERR_NOPACK for the others.
extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    int channel = _arg<1, int32_t>(rdram, ctx);
    ctx->r2 = channel == 0 ? 0 : kErrNoPack;
}

// osPfsChecker(OSPfs* pfs): libultra's consistency check, which talks to the PIF directly. The image is only
// ever changed by the game's own file-system code (and a pack from an emulator was written by one), so: consistent.
extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

// __osContRamRead(OSMesgQueue* mq, int channel, u16 block, u8* buffer): read one 32-byte block.
extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    int channel = _arg<1, int32_t>(rdram, ctx);
    uint32_t block = _arg<2, uint32_t>(rdram, ctx) & 0xFFFF;
    PTR(uint8_t) buffer = _arg<3, PTR(uint8_t)>(rdram, ctx);
    if (channel != 0) {
        ctx->r2 = kErrNoPack;
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (block >= kBlocks) {
        // Beyond the 32 KiB of bank 0 (a pack with more banks, which we don't have): reads as zeroes.
        for (size_t i = 0; i < kBlock; i++) {
            MEM_B(i, buffer) = 0;
        }
    } else {
        for (size_t i = 0; i < kBlock; i++) {
            MEM_B(i, buffer) = g_image[block * kBlock + i];
        }
    }
    ctx->r2 = 0;
}

// __osContRamWrite(OSMesgQueue* mq, int channel, u16 block, u8* buffer, int force): write one 32-byte block.
// Block 1024 (0x8000) is libultra's bank-select register; there is only one bank, so it is accepted and ignored.
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    int channel = _arg<1, int32_t>(rdram, ctx);
    uint32_t block = _arg<2, uint32_t>(rdram, ctx) & 0xFFFF;
    PTR(uint8_t) buffer = _arg<3, PTR(uint8_t)>(rdram, ctx);
    if (channel != 0) {
        ctx->r2 = kErrNoPack;
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    if (block >= kBlocks) {
        ctx->r2 = 0;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!g_written || now - g_last_write > std::chrono::seconds(30)) {
        backup_locked("before saving");
    }
    g_written = true;
    g_last_write = now;
    for (size_t i = 0; i < kBlock; i++) {
        g_image[block * kBlock + i] = MEM_B(i, buffer);
    }
    flush_locked();
    ctx->r2 = 0;
}
