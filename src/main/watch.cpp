// Watching for a session that goes wrong without crashing (a freeze; the game closed by force), and Linux crashes.
//
// A tester whose game froze and was closed by force had nothing to send: a report was only made for a crash. Now:
//  - The session marker (session-running in the data folder) is made at start and removed when the program ends
//    properly. Found at the next start, the last session froze or was ended by force: a problem report is made then,
//    with the watchdog's notes, and the player is told (a notice at the top of the screen).
//  - The freeze watchdog: the game thread checks in once a frame (watch::frame, from Graphics::EndList); if no frame
//    comes for kFreezeSeconds after the first, the watchdog writes what the game was doing, whether the sound is still
//    running (a frozen picture with the game still going, or the whole game stopped) and where the game thread is: its
//    call stack on Linux (it is asked for it with SIGUSR2), a minidump on Windows (dbghelp, all threads' stacks, read
//    against the release's .pdb), into the log and logs/hang.txt (and hang.dmp). It does nothing else: the game may
//    still recover (then the log says so, and the next start reports the freeze as one that passed).
//  - Linux crash signals write game-crash.txt and a report, as Windows' crash handler does (src/main/report.cpp), and
//    end the program by the same signal, so the core dump is kept and the launcher's own report is skipped (it skips
//    when the game made one).
#include "watch.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include "platform.h"
#include "report.h"
#include "settings.h"

extern std::atomic_bool aidyn_quitting;

namespace {
    namespace fs = std::filesystem;
    using clock_type = std::chrono::steady_clock;
    constexpr int kFreezeSeconds = 15;

    fs::path g_data, g_marker;
    std::atomic<int64_t> g_last_frame{ 0 };   // steady clock, ms; 0 before the first frame
    std::atomic<uint64_t> g_sound_ticks{ 0 };
    std::atomic_bool g_ended{ false };          // session_end: the watchdog stops (a crash's message box isn't a freeze)
    const auto g_started = clock_type::now();
    std::mutex g_lock;
    std::string g_state = "starting", g_notice;

    int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now().time_since_epoch()).count();
    }
    std::string clock_text() {
        char buf[32];
        const std::time_t t = std::time(nullptr);
        std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        return buf;
    }
    std::string session_age() {
        const long s = (long)std::chrono::duration_cast<std::chrono::seconds>(clock_type::now() - g_started).count();
        char buf[32];
        std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
        return buf;
    }
    std::vector<uint8_t> read_file(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    fs::path hang_txt() { return g_data / "logs" / "hang.txt"; }
    fs::path hang_dmp() { return g_data / "logs" / "hang.dmp"; }

#ifdef _WIN32
    HANDLE g_game_thread = nullptr;

    // A minidump of the whole program (every thread's stack) through dbghelp.dll, loaded when needed.
    bool write_minidump(const fs::path& path) {
        using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
        HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
        if (dbghelp == nullptr) return false;
        const auto write = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
        bool ok = false;
        HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write != nullptr && file != INVALID_HANDLE_VALUE) {
            constexpr int kNormalWithThreadInfo = 0x00001000;  // MiniDumpNormal | MiniDumpWithThreadInfo
            ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, kNormalWithThreadInfo, nullptr, nullptr, nullptr) != 0;
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        FreeLibrary(dbghelp);
        return ok;
    }
#else
    pthread_t g_game_thread{};
    std::atomic_bool g_have_game_thread{ false };
    void* g_stack_frames[48];
    volatile sig_atomic_t g_stack_count = 0, g_stack_done = 0;

    void on_stack_request(int) {
        g_stack_count = ::backtrace(g_stack_frames, 48);
        g_stack_done = 1;
    }

    // The game thread's call stack: it is interrupted to take it (SIGUSR2), wherever it waits.
    std::vector<std::string> game_thread_stack() {
        if (!g_have_game_thread) return {};
        g_stack_done = 0;
        if (pthread_kill(g_game_thread, SIGUSR2) != 0) return {};
        for (int i = 0; i < 100 && !g_stack_done; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!g_stack_done) return { "(it didn't answer)" };
        const int skip = g_stack_count > 2 ? 2 : 0;  // the handler and the signal trampoline
        return aidyn::platform::describe_stack(g_stack_frames + skip, g_stack_count - skip);
    }
#endif

    void note_freeze(int seconds) {
        std::string state;
        {
            std::lock_guard g(g_lock);
            state = g_state;
        }
        const uint64_t ticks0 = g_sound_ticks.load();
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const bool sound = g_sound_ticks.load() != ticks0;
        std::string text = "FREEZE at " + clock_text() + " (" + session_age() + " into the session): no game frame for " +
                           std::to_string(seconds) + " s\n";
        text += "Last state: " + state + "\n";
        text += sound ? "Sound: still running (the game goes on; the picture stopped)\n"
                      : "Sound: stopped too (the whole game stopped)\n";
#ifdef _WIN32
        { std::error_code ec; fs::create_directories(hang_dmp().parent_path(), ec); }
        text += write_minidump(hang_dmp()) ? "Minidump: logs/hang.dmp (every thread's stack; read with the release's .pdb)\n"
                                           : "Minidump: could not be written\n";
#else
        text += "Game thread's stack:\n";
        for (const std::string& line : game_thread_stack()) text += "    " + line + "\n";
#endif
        std::fprintf(stderr, "%s", text.c_str());
        std::fflush(stderr);
        std::error_code ec;
        fs::create_directories(hang_txt().parent_path(), ec);  // (no launcher made it: the Linux game run directly)
        std::ofstream(hang_txt(), std::ios::app) << text << "\n";
    }

    void watchdog() {
        bool frozen = false;
        int64_t frozen_since = 0;
        while (!aidyn_quitting.load() && !g_ended.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (g_ended.load()) break;
            const int64_t last = g_last_frame.load();
            if (last == 0) continue;  // not started yet (the ROM loading, the first frame)
            const int64_t age = now_ms() - last;
            if (!frozen && age >= kFreezeSeconds * 1000) {
                frozen = true;
                frozen_since = last;
                note_freeze((int)(age / 1000));
            } else if (frozen && age < 1000) {
                frozen = false;
                const std::string line = "FREEZE over at " + clock_text() + ": the game went on after " +
                                         std::to_string((now_ms() - frozen_since) / 1000) + " s\n";
                std::fprintf(stderr, "%s", line.c_str());
                std::fflush(stderr);
                std::ofstream(hang_txt(), std::ios::app) << line << "\n";
            }
        }
    }

#ifndef _WIN32
    volatile sig_atomic_t g_crashing = 0;

    void on_crash(int sig, siginfo_t* info, void*) {
        if (g_crashing) {  // a crash while reporting one: end by it
            signal(sig, SIG_DFL);
            raise(sig);
            return;
        }
        g_crashing = 1;
        char line[160];
        std::snprintf(line, sizeof line, "CRASH (signal %d, %s) at address %p\n", sig, strsignal(sig), info ? info->si_addr : nullptr);
        std::string out = line;
        for (const std::string& frame : aidyn::platform::backtrace_lines(1, 32)) out += "    " + frame + "\n";
        std::fputs(out.c_str(), stderr);
        std::fflush(stderr);
        if (FILE* f = std::fopen((aidyn::settings::folder() / "game-crash.txt").string().c_str(), "w")) {
            std::fputs(out.c_str(), f);
            std::fclose(f);
        }
        char reason[96];
        std::snprintf(reason, sizeof reason, "The game crashed (signal %d, %s; game-crash.txt)", sig, strsignal(sig));
        aidyn::report::make(reason);
        aidyn::watch::session_end();
        signal(sig, SIG_DFL);  // ended by the same signal: the system keeps its core dump
        raise(sig);
    }
#endif
}

void aidyn::watch::session_start(const fs::path& data, bool report_only) {
    g_data = data;
    g_marker = data / "session-running";
    std::error_code ec;
    if (report_only) {
        fs::remove(g_marker, ec);  // the launcher's report after a crash covers the session that left it
        return;
    }
    const bool unclean = fs::exists(g_marker, ec), freeze = fs::exists(hang_txt(), ec);
    if (unclean || freeze) {
        std::string reason = unclean ? "The last session didn't close properly (it froze, or was closed by force)"
                                     : "The game froze for a while in the last session and went on";
        std::vector<std::pair<std::string, std::vector<uint8_t>>> extra;
        if (freeze) {
            reason += unclean ? "; the freeze watchdog's notes are in hang.txt" : " (hang.txt)";
            extra.emplace_back("hang.txt", read_file(hang_txt()));
        }
        if (fs::exists(hang_dmp(), ec)) extra.emplace_back("hang.dmp", read_file(hang_dmp()));
        const fs::path made = aidyn::report::make(reason, extra);
        std::fprintf(stdout, "Session: %s: %s\n", reason.c_str(), made.empty() ? "no report could be written" : "report written");
        std::fflush(stdout);
        if (!made.empty()) {
            std::lock_guard g(g_lock);
            g_notice = unclean ? "The last session didn't close properly: report saved" : "A freeze last session: report saved";
        }
        fs::remove(hang_txt(), ec);
        fs::remove(hang_dmp(), ec);
    }
    std::ofstream(g_marker) << clock_text() << "\n";
}

void aidyn::watch::session_end() {
    g_ended = true;
    if (g_marker.empty()) return;
    std::error_code ec;
    fs::remove(g_marker, ec);
}

void aidyn::watch::start_watchdog() {
#ifndef _WIN32
    struct sigaction sa {};
    sa.sa_handler = on_stack_request;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, nullptr);
#endif
    std::thread(watchdog).detach();
}

void aidyn::watch::frame(const std::string& state) {
    if (g_last_frame.load() == 0) {
#ifdef _WIN32
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_game_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
        g_game_thread = pthread_self();
        g_have_game_thread = true;
#endif
    }
    g_last_frame = now_ms();
    std::lock_guard g(g_lock);
    if (state != g_state) g_state = state;
}

void aidyn::watch::sound_tick() {
    g_sound_ticks.fetch_add(1, std::memory_order_relaxed);
}

std::string aidyn::watch::take_notice() {
    std::lock_guard g(g_lock);
    std::string n;
    n.swap(g_notice);
    return n;
}

void aidyn::watch::install_crash_signals() {
#ifndef _WIN32
    struct sigaction sa {};
    sa.sa_sigaction = on_crash;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT }) sigaction(sig, &sa, nullptr);
#endif
}
