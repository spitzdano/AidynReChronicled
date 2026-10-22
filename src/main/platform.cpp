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
#include <execinfo.h>
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

void aidyn::platform::restart(const std::vector<std::string>& args) {
    // Windows has no exec: start the new copy, then end this one.
    std::wstring cmd;
    for (size_t i = 0; i < args.size(); i++) {
        std::wstring a = i == 0 ? exe_path().wstring() : fs::path(args[i]).wstring();
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

void aidyn::platform::restart(const std::vector<std::string>& args) {
    std::vector<std::string> copy = args;
    std::vector<char*> argv;
    for (std::string& a : copy) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    // The executable's own path, not /proc/self/exe: exec takes the process's name from the path ("exe" otherwise).
    execv(exe_path().c_str(), argv.data());
    std::fprintf(stderr, "Could not restart: %s\n", std::strerror(errno));
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

void aidyn::platform::start_log(const fs::path& dir, const std::vector<std::string>& args) {
#ifdef _WIN32
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path log = dir / "last-run.log";
    if (fs::exists(log, ec)) {
        fs::rename(log, dir / "previous-run.log", ec);
    }
    if (std::freopen(log.string().c_str(), "w", stdout) == nullptr) {
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

