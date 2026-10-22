// Problem reports (src/main/report.cpp): one .zip with what's needed to look into a problem, for a tester to send.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace aidyn::report {
    // Where things are: the data folder (logs/) and the Controller Pak (its saves are beside it). Set by main().
    void set_paths(const std::filesystem::path& data, const std::filesystem::path& pak);
    // Writes reports/report-<date>-<time>.zip in the settings folder: report.txt (why, when, the build, the game's
    // version, the system), game-crash.txt, the run logs, settings.json, the pak and the quick, auto and last saves,
    // plus `extra` (name, bytes). Home folder paths in the text files become ~. Keeps the newest 10. Returns its path,
    // empty if it couldn't be written.
    std::filesystem::path make(const std::string& reason,
                               const std::vector<std::pair<std::string, std::vector<uint8_t>>>& extra = {});
    // The game hit its own crash check (src/game/patches.cpp): a report, a message saying where it is, and the end of
    // the program (where the game would sit frozen).
    [[noreturn]] void game_crashed(const std::string& reason);
    // Windows: a crash the program doesn't handle itself (an access violation...) writes game-crash.txt (the exception,
    // where, the call stack) and a problem report, as the Linux launcher does after a crash signal. Linux: nothing.
    void install_crash_handler();
}
