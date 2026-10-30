// What differs between Linux and Windows (src/main/platform.cpp): the rest of the program asks here.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace aidyn::platform {
    std::filesystem::path exe_path();   // this program's file
    std::filesystem::path data_home();  // where per-user data goes: $XDG_DATA_HOME or ~/.local/share; %APPDATA% on Windows
    // The user's home folder, as each path may spell it (problem reports write it as ~): on Windows %USERPROFILE%, and
    // under Wine also the Linux home, as Z:\home\<name> and /home/<name>.
    std::vector<std::string> home_dirs();
    std::string os_description();       // e.g. "Arch Linux, kernel 6.x (x86_64)" or "Windows 10.0.26100 (x86_64)"
    // Under gamescope (SteamOS / Bazzite Game Mode, or a gamescope session): its XWayland reports 60 Hz whatever the
    // screen runs at, so the display's rate it gives isn't the real one (src/main/rt64_context.cpp).
    bool under_gamescope();
    // Starts this program again with these arguments (args[0] the program) in place of this one. Returns only if it
    // couldn't.
    void restart(const std::vector<std::string>& args);
    // The call stack, one line per frame ("program(+0x1234) [0x...]" or a symbol), skipping the first `skip` frames.
    std::vector<std::string> backtrace_lines(int skip, int max_frames = 24);
    // Windows: the program's output goes to dir/last-run.log (the run before: previous-run.log), as the Linux launcher
    // (tools/ally/aidyn-rechronicled.sh) arranges there; a windowed program has nowhere else to write. Linux: nothing.
    void start_log(const std::filesystem::path& dir, const std::vector<std::string>& args);
    // "Start: <what> at <ms>" in the log: how long since the process began (on Windows from its creation, DLL loading
    // included), to see where a slow start goes.
    void startup_mark(const char* what);
    // The same in two steps, for keeping the frames now and describing them later (the heap tracer).
    int capture_stack(void** frames, int max_frames);
    std::vector<std::string> describe_stack(void* const* frames, int count);
}
