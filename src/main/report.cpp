// Problem reports: everything needed to look into a problem in one .zip, for a tester to attach to an issue.
//
// Made three ways: by the game when it hits its own crash check (src/game/patches.cpp, "Game crash report": then it
// says where the report is and closes, where it used to freeze); by the launcher when the game ended without closing
// properly (`AidynReChronicled --report "<why>"` makes one and exits, game/aidyn-rechronicled.sh); and by the player,
// Report a problem on the pause menu's ReChronicled page (with a save of that moment and the screen's picture).
#include "report.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include "SDL2/SDL.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "miniz.h"

#include "aidyn_build.h"
#include "platform.h"
#include "settings.h"

namespace {
    std::filesystem::path g_data, g_pak;
    constexpr size_t kKept = 10;

    std::vector<uint8_t> read_file(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // The home folder's path (which holds the user's name) as ~.
    std::string redact(std::string s) {
        const std::string h = aidyn::platform::home_dir();
        if (h.size() < 2) {
            return s;
        }
        for (size_t at = 0; (at = s.find(h, at)) != std::string::npos; at += 1) {
            s.replace(at, h.size(), "~");
        }
        return s;
    }

    std::string when(std::filesystem::file_time_type t) {
        const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        char buf[32];
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
        return buf;
    }

    const char* game_name() {
        switch (aidyn::settings::game().rom_patch) {
            case aidyn::settings::RomPatch::AidynPatch: return "Aidyn Patch";
            case aidyn::settings::RomPatch::AidynPlus: return "Aidyn Plus";
            default: return "the original";
        }
    }
}

void aidyn::report::set_paths(const std::filesystem::path& data, const std::filesystem::path& pak) {
    g_data = data;
    g_pak = pak;
}

std::filesystem::path aidyn::report::make(const std::string& reason,
                                          const std::vector<std::pair<std::string, std::vector<uint8_t>>>& extra) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = aidyn::settings::folder() / "reports";
    fs::create_directories(dir, ec);
    const std::time_t now = std::time(nullptr);
    char stamp[32], made[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    std::strftime(made, sizeof made, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    const fs::path path = dir / (std::string("report-") + stamp + ".zip");

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.string().c_str(), 0)) {
        fprintf(stderr, "Report: could not write %s\n", path.string().c_str());
        return {};
    }
    auto add = [&](const std::string& name, const void* data, size_t size) {
        mz_zip_writer_add_mem(&zip, name.c_str(), data, size, MZ_DEFAULT_COMPRESSION);
    };
    auto add_text = [&](const std::string& name, const std::string& text) {
        const std::string t = redact(text);
        add(name, t.data(), t.size());
    };
    auto add_file = [&](const std::string& name, const fs::path& from, bool text) -> bool {
        if (!fs::is_regular_file(from, ec)) {
            return false;
        }
        const std::vector<uint8_t> bytes = read_file(from);
        if (text) {
            add_text(name, std::string(bytes.begin(), bytes.end()));
        } else {
            add(name, bytes.data(), bytes.size());
        }
        return true;
    };

    std::ostringstream info;
    info << "Aidyn ReChronicled problem report\n\n"
         << "Why:         " << reason << "\n"
         << "Made:        " << made << "\n"
         << "Build:       " << AIDYN_BUILD_TEXT << "\n"
         << "Playing:     " << game_name() << "\n"
         << "System:      " << aidyn::platform::os_description() << "\n";
    const fs::path crash = aidyn::settings::folder() / "game-crash.txt";
    if (fs::is_regular_file(crash, ec)) {
        info << "Game crash:  game-crash.txt, written " << when(fs::last_write_time(crash, ec)) << "\n";
    }
    info << "\nThe GPU and driver are in logs/last-run.log. When you send this, say in a few words what you were doing\n"
            "and what went wrong.\n";
    add_text("report.txt", info.str());

    add_file("game-crash.txt", crash, true);
    add_file("logs/last-run.log", g_data / "logs" / "last-run.log", true);
    add_file("logs/previous-run.log", g_data / "logs" / "previous-run.log", true);
    add_file("settings.json", aidyn::settings::folder() / "settings.json", true);
    if (!g_pak.empty()) {
        add_file("saves/" + g_pak.filename().string(), g_pak, false);
        const std::string stem = g_pak.stem().string();
        for (const char* suffix : { "-quicksave.sav", "-autosave-1.sav", "-autosave-2.sav", "-lastsave.sav" }) {
            add_file("saves/" + stem + suffix, g_pak.parent_path() / (stem + suffix), false);
        }
    }
    for (const auto& [name, bytes] : extra) {
        add(name, bytes.data(), bytes.size());
    }
    const bool ok = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    if (!ok) {
        fprintf(stderr, "Report: could not finish %s\n", path.string().c_str());
        fs::remove(path, ec);
        return {};
    }

    // The newest kKept.
    std::vector<std::pair<fs::file_time_type, fs::path>> reports;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".zip" && entry.path().filename().string().rfind("report-", 0) == 0) {
            reports.emplace_back(entry.last_write_time(ec), entry.path());
        }
    }
    std::sort(reports.begin(), reports.end());
    for (size_t i = 0; i + kKept < reports.size(); i++) {
        fs::remove(reports[i].second, ec);
    }
    fprintf(stdout, "Report: wrote %s\n", path.string().c_str());
    fflush(stdout);
    return path;
}

#ifdef _WIN32
namespace {
    LONG WINAPI unhandled(EXCEPTION_POINTERS* info) {
        static volatile LONG entered = 0;
        if (InterlockedExchange(&entered, 1) != 0) {
            return EXCEPTION_EXECUTE_HANDLER;  // a crash while reporting one: just end
        }
        const EXCEPTION_RECORD* rec = info->ExceptionRecord;
        std::string out;
        char line[256];
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)rec->ExceptionAddress, &mod)) {
            GetModuleFileNameA(mod, name, sizeof name);
        }
        const char* base = std::strrchr(name, '\\');
        std::snprintf(line, sizeof line, "CRASH (exception 0x%08lX) at %s(+0x%llx)", rec->ExceptionCode, base ? base + 1 : name,
                      (unsigned long long)((const char*)rec->ExceptionAddress - (const char*)mod));
        out += line;
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
            std::snprintf(line, sizeof line, ", %s address 0x%llx", rec->ExceptionInformation[0] == 1 ? "writing" : "reading",
                          (unsigned long long)rec->ExceptionInformation[1]);
            out += line;
        }
        out += "\n";
        for (const std::string& frame : aidyn::platform::backtrace_lines(0)) {
            out += "    " + frame + "\n";
        }
        std::fputs(out.c_str(), stderr);
        if (FILE* f = std::fopen((aidyn::settings::folder() / "game-crash.txt").string().c_str(), "w")) {
            std::fputs(out.c_str(), f);
            std::fclose(f);
        }
        char reason[96];
        std::snprintf(reason, sizeof reason, "The game crashed (exception 0x%08lX, game-crash.txt)", rec->ExceptionCode);
        aidyn::report::game_crashed(reason);
    }
}
#endif

void aidyn::report::install_crash_handler() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandled);
#endif
}

void aidyn::report::game_crashed(const std::string& reason) {
    const std::filesystem::path made = make(reason);
    std::string text = "Aidyn ReChronicled ran into a problem and has to close.\n\n";
    if (!made.empty()) {
#ifdef _WIN32
        const std::string where = made.string();  // in full: "~" means nothing on Windows (and only the player sees this)
#else
        const std::string where = redact(made.string());
#endif
        text += "A problem report was saved:\n" + where +
                "\n\nTo help fix it, send that file with a few words on what you were doing (the manual's "
                "\"Reporting a problem\" says how).";
    } else {
        text += "The details are in game-crash.txt in the data folder.";
    }
    fprintf(stderr, "%s\n", text.c_str());
    fflush(stderr);
#ifdef _WIN32
    // Windows' own message box: it works from inside the crash handler, where SDL's may not.
    MessageBoxA(nullptr, text.c_str(), "Aidyn ReChronicled", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Aidyn ReChronicled", text.c_str(), nullptr);
#endif
    std::_Exit(1);
}
