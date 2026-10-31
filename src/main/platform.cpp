// platform.cpp: the few things Linux and Windows do differently (src/main/platform.h).
#include "platform.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32

fs::path aidyn::platform::exe_path() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n < buf.size()) {
            buf.resize(n);
            return fs::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

fs::path aidyn::platform::data_home() {
    PWSTR path = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) {
        out = fs::path(path);
    }
    CoTaskMemFree(path);
    return out.empty() ? fs::current_path() : out;
}

bool aidyn::platform::under_gamescope() { return false; }  // Windows (under Proton, gamescope is seen as Linux's)

std::vector<std::string> aidyn::platform::home_dirs() {
    std::vector<std::string> out;
    if (const char* p = std::getenv("USERPROFILE")) {
        out.emplace_back(p);
    }
    if (const char* w = std::getenv("WINEHOMEDIR")) {  // Wine: "\\??\\Z:\\home\\<name>"
        std::string z = w;
        if (z.rfind("\\??\\", 0) == 0) {
            z.erase(0, 4);
        }
        out.push_back(z);
        if (z.size() > 2 && z[1] == ':') {
            std::string unix_home = z.substr(2);
            std::replace(unix_home.begin(), unix_home.end(), '\\', '/');
            out.push_back(unix_home);
        }
    }
    return out;
}

std::string aidyn::platform::os_description() {
    // RtlGetVersion tells the real version (GetVersionEx lies to programs without a manifest).
    using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto get = (RtlGetVersion_t)(void*)GetProcAddress(ntdll, "RtlGetVersion")) {
            get(&v);
        }
    }
    char text[96];
    std::snprintf(text, sizeof text, "Windows %lu.%lu.%lu (x86_64)%s", v.dwMajorVersion, v.dwMinorVersion,
                  v.dwBuildNumber, GetModuleHandleW(L"ntdll.dll") && GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                  "wine_get_version") ? ", under Wine/Proton" : "");
    return text;
}

void aidyn::platform::restart(const std::vector<std::string>& args, const fs::path& program) {
    // Windows has no exec: start the new copy, then end this one.
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); i++) {
        std::wstring a = i == 0 ? (program.empty() ? exe_path() : program).wstring() : fs::path(args[i]).wstring();
        cmd += (i ? L" \"" : L"\"") + a + L"\"";
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::fflush(nullptr);
        std::_Exit(0);
    }
    std::fprintf(stderr, "Could not restart: error %lu\n", GetLastError());
}

fs::path aidyn::platform::find_curl() {
    wchar_t dir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    const fs::path curl = fs::path(dir) / L"curl.exe";
    std::error_code ec;
    return fs::is_regular_file(curl, ec) ? curl : fs::path();
}

int aidyn::platform::run_process(const std::vector<std::string>& args, const fs::path& err_file,
                                 const std::function<bool()>& keep_going) {
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); i++) {
        cmd += (i ? L" \"" : L"\"") + fs::path(args[i]).wstring() + L"\"";
    }
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE err = CreateFileW(err_file.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = err;
    si.hStdError = err;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(fs::path(args[0]).wstring().c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    if (!ok) return -1;
    CloseHandle(pi.hThread);
    int result = -2;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 250) == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            result = (int)code;
            break;
        }
        if (!keep_going()) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            break;
        }
    }
    CloseHandle(pi.hProcess);
    return result;
}

int aidyn::platform::capture_stack(void** frames, int max_frames) {
    return CaptureStackBackTrace(0, (DWORD)max_frames, frames, nullptr);
}

std::vector<std::string> aidyn::platform::describe_stack(void* const* frames, int count) {
    // Addresses as offsets into their module, like Linux's backtrace_symbols ("AidynReChronicled.exe(+0x1234)"):
    // llvm-symbolizer reads them against the release's PDB.
    std::vector<std::string> out;
    for (int i = 0; i < count; i++) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        char line[MAX_PATH + 64];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)frames[i], &mod)) {
            GetModuleFileNameA(mod, name, sizeof name);
            const char* base = std::strrchr(name, '\\');
            std::snprintf(line, sizeof line, "%s(+0x%llx) [%p]", base ? base + 1 : name,
                          (unsigned long long)((const char*)frames[i] - (const char*)mod), frames[i]);
        } else {
            std::snprintf(line, sizeof line, "? [%p]", frames[i]);
        }
        out.push_back(line);
    }
    return out;
}

#else

fs::path aidyn::platform::exe_path() {
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path("/proc/self/exe") : p;
}

fs::path aidyn::platform::data_home() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return fs::path(xdg);
    }
    const char* home = std::getenv("HOME");
    return fs::path(home != nullptr ? home : ".") / ".local" / "share";
}

bool aidyn::platform::under_gamescope() {
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    return std::getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr || (desktop != nullptr && std::string(desktop) == "gamescope");
}

std::vector<std::string> aidyn::platform::home_dirs() {
    const char* p = std::getenv("HOME");
    return p != nullptr ? std::vector<std::string>{ p } : std::vector<std::string>{};
}

std::string aidyn::platform::os_description() {
    std::string name = "Linux";
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            name = line.substr(12);
            name.erase(std::remove(name.begin(), name.end(), '"'), name.end());
        }
    }
    utsname uts{};
    uname(&uts);
    return name + ", kernel " + uts.release + " (" + uts.machine + ")";
}

void aidyn::platform::restart(const std::vector<std::string>& args, const fs::path& program) {
    std::vector<std::string> copy = args;
    std::vector<char*> argv;
    for (std::string& a : copy) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    // The same process goes on (exec), and the log (the launcher's) with it: a line marks where the new program
    // starts, and its start-up times count from here (startup_mark), not from the first start.
    const fs::path target = program.empty() ? exe_path() : program;
    timespec boot{};
    clock_gettime(CLOCK_BOOTTIME, &boot);
    setenv("AIDYN_RESTARTED_AT_MS", std::to_string(boot.tv_sec * 1000.0 + boot.tv_nsec / 1e6).c_str(), 1);
    const std::time_t now = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(stdout, "== %s restart: %s\n", when, target.string().c_str());
    std::fflush(stdout);
    // The executable's own path, not /proc/self/exe: exec takes the process's name from the path ("exe" otherwise).
    execv(target.c_str(), argv.data());
    std::fprintf(stderr, "Could not restart: %s\n", std::strerror(errno));
}

fs::path aidyn::platform::find_curl() {
    const char* path = std::getenv("PATH");
    std::string dirs = path != nullptr && *path != '\0' ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        const size_t end = std::min(dirs.find(':', start), dirs.size());
        const fs::path curl = fs::path(dirs.substr(start, end - start)) / "curl";
        if (end > start && access(curl.c_str(), X_OK) == 0) return curl;
        start = end + 1;
    }
    return {};
}

int aidyn::platform::run_process(const std::vector<std::string>& args, const fs::path& err_file,
                                 const std::function<bool()>& keep_going) {
    std::vector<std::string> env;
    for (char** e = environ; *e != nullptr; e++) {
        if (std::strncmp(*e, "LD_PRELOAD=", 11) != 0 && std::strncmp(*e, "LD_LIBRARY_PATH=", 16) != 0) env.emplace_back(*e);
    }
    std::vector<char*> envp, argv;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<std::string> copy = args;
    for (std::string& a : copy) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, err_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    for (;;) {
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
        if (done < 0 && errno != EINTR) return -1;
        if (!keep_going()) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            return -2;
        }
        usleep(250 * 1000);
    }
}

int aidyn::platform::capture_stack(void** frames, int max_frames) {
    return ::backtrace(frames, max_frames);
}

std::vector<std::string> aidyn::platform::describe_stack(void* const* frames, int count) {
    std::vector<std::string> out;
    char** names = backtrace_symbols(frames, count);
    for (int i = 0; names != nullptr && i < count; i++) {
        out.push_back(names[i]);
    }
    std::free(names);
    return out;
}

#endif

void aidyn::platform::startup_mark(const char* what) {
#ifdef _WIN32
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimePreciseAsFileTime(&now);
    const auto ticks = [](FILETIME t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    const long long ms = static_cast<long long>((ticks(now) - ticks(created)) / 10000);  // 100 ns ticks
#else
    // The process's start in clock ticks since boot (/proc/self/stat, field 22), against the time since boot.
    long long ms = -1;
    if (std::FILE* f = std::fopen("/proc/self/stat", "r")) {
        char buf[1024];
        const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
        buf[n] = '\0';
        if (const char* p = std::strrchr(buf, ')')) {  // the name in parentheses may hold spaces
            unsigned long long start = 0;
            int field = 2;
            for (p++; *p != '\0' && field < 22; p++) {
                if (*p == ' ') {
                    field++;
                }
            }
            if (std::sscanf(p, "%llu", &start) == 1) {
                timespec boot{};
                clock_gettime(CLOCK_BOOTTIME, &boot);
                const double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
                double started = start * 1000.0 / hz;
                // After restart() the process is the same (exec): it started when the restart did.
                if (const char* r = std::getenv("AIDYN_RESTARTED_AT_MS"); r != nullptr && *r != '\0') {
                    started = std::max(started, std::atof(r));
                }
                ms = static_cast<long long>(boot.tv_sec * 1000.0 + boot.tv_nsec / 1e6 - started);
            }
        }
    }
#endif
    std::fprintf(stdout, "Start: %s at %lld ms\n", what, ms);
    std::fflush(stdout);
}

void aidyn::platform::start_log(const fs::path& dir, const std::vector<std::string>& args) {
#ifdef _WIN32
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path log = dir / "last-run.log";
    if (fs::exists(log, ec)) {
        fs::rename(log, dir / "previous-run.log", ec);
    }
    // Emptied, then both streams append (each write at the end): opened "w" and "a" they'd keep separate positions,
    // and stdout's lines would overwrite stderr's.
    std::fclose(std::fopen(log.string().c_str(), "w"));
    if (std::freopen(log.string().c_str(), "a", stdout) == nullptr) {
        return;
    }
    std::freopen(log.string().c_str(), "a", stderr);
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // the C runtime has no line buffering: _IOLBF would hold it all back
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    char when[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
    std::string line = std::string("== ") + when + " " + exe_path().string();
    for (size_t i = 1; i < args.size(); i++) {
        line += " " + args[i];
    }
    std::fprintf(stdout, "%s\n", line.c_str());
    std::fflush(stdout);
#else
    (void)dir;
    (void)args;
#endif
}

std::vector<std::string> aidyn::platform::backtrace_lines(int skip, int max_frames) {
    std::vector<void*> frames(max_frames + skip + 1);
    const int n = capture_stack(frames.data(), (int)frames.size());
    const int first = std::min(n, skip + 1);  // and this function's own frame
    return describe_stack(frames.data() + first, n - first);
}

