// Watching for a session that goes wrong without crashing (src/main/watch.cpp): a freeze, or the game closed by force.
#pragma once
#include <filesystem>
#include <string>

namespace aidyn::watch {
    // At start: a session that didn't close properly last time (its marker file still there: it froze, or was ended by
    // force) gets a problem report now, with the freeze watchdog's notes if it left any, and a notice for the player.
    // Then this session's marker is made. `report_only`: the --report run, which leaves the marker alone.
    void session_start(const std::filesystem::path& data, bool report_only);
    // The session ends properly (closed, restarted into another version, or after a reported crash): marker removed,
    // and the watchdog stops.
    void session_end();
    // The freeze watchdog: a thread that, once the game has drawn its first frame, notices when no frame comes for
    // kFreezeSeconds and writes what it can into the log and logs/hang.txt (the game's state, whether the sound still
    // runs, the game thread's stack on Linux, a minidump on Windows). The game is left alone: it may still recover.
    void start_watchdog();
    // The game thread, once a frame: the watchdog's heartbeat, and what the game is doing (for the notes).
    void frame(const std::string& state);
    // The sound thread, for each buffer it queues: to tell a frozen picture from a whole frozen game.
    void sound_tick();
    // A notice for the player about the last session's report, once (empty when there's none).
    std::string take_notice();
    // Linux: a crash signal (segfault, bus error, illegal instruction, arithmetic, abort) writes game-crash.txt (the
    // signal, the address, the call stack) and a problem report, as Windows' crash handler does, then ends the
    // program by the same signal so the system still keeps its core dump. Windows: nothing (report.cpp has its own).
    void install_crash_signals();
}
