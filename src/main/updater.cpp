// updater.cpp: in-game updates (spitzdano, 2026-10-30; the plan: updates come through the game itself, on Linux and
// Windows alike, controller only on the Ally's Game Mode).
//
// Checking: at start (settings.json updates.check) a thread runs curl on GitHub's list of releases
// (api.github.com/repos/spitzdano/AidynReChronicled/releases) and keeps the newest one newer than this build: full
// releases, and pre-releases too with updates.test_builds. A development build ("dev" on the title screen) checks
// nothing. curl is a separate program (Linux's own; System32's curl.exe on Windows 10 and later), so the game has no
// network or TLS code of its own; without it the panel offers the release page.
//
// The manifest: each release made by tools/release/make_release.sh carries update.json (every installed file of each
// platform: its path, size, BLAKE2b-256 and the URL of the release asset holding exactly that file; a file unchanged
// since an earlier release points at that release's copy) and update.json.sig (Ed25519 of update.json's bytes,
// base64, made with spitzdano's private key; kPublicKey below is its other half). A manifest whose signature doesn't
// check, or that names a URL outside this repository's releases, or a path outside the install, is refused.
//
// Downloading (the panel's Update): the installed files are hashed (the hashes kept in .update/hashes.json by size and
// time, so the 900 MB art pack is read once) and only the files that differ are fetched into .update/new/, each checked
// against its hash; then .update/ready.json lists them. Nothing in the install changes until then.
//
// Applying, at the next start (apply_pending, first thing in main) or Restart now: each old file moves into
// .update/previous/ and the new one into its place (a running program can be renamed on both systems, not
// overwritten), then the new program starts in place of this one (Linux: exec, the same process, so Steam and Game Mode
// see nothing; Windows: a new process). A failure part way puts back what was moved and the old version keeps running.
// Undo update moves .update/previous/ back the same way. Saves, settings and ROMs live in the data folder and are
// never touched; files the manifest doesn't list are left alone.
//
// Testing (developers): AIDYN_UPDATE_URL=<base> reads <base>/releases.json instead of GitHub and accepts asset URLs
// under <base> (http too), AIDYN_UPDATE_KEY=<hex> the public key of a test key pair (only with AIDYN_UPDATE_URL), and
// AIDYN_UPDATE_VERSION=<version> makes a development build act as that release, AIDYN_UPDATE_RATE=<curl rate, e.g. 2M> slows
// the download (with AIDYN_UPDATE_URL) to watch its progress.
#include "updater.h"
#include "platform.h"
#include "settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <tuple>

#include "SDL2/SDL.h"
#include "aidyn_version.h"
#include "json/json.hpp"

extern "C" {
#include "monocypher.h"
#include "monocypher-ed25519.h"
}

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
    constexpr const char* kReleasesApi = "https://api.github.com/repos/spitzdano/AidynReChronicled/releases?per_page=30";
    constexpr const char* kAssetPrefix = "https://github.com/spitzdano/AidynReChronicled/releases/download/";
    // The release signing key's public half (tools/release/update_manifest.py keygen; the private half is spitzdano's).
    constexpr const char* kPublicKey = "0000000000000000000000000000000000000000000000000000000000000000";
#ifdef _WIN32
    constexpr const char* kPlatform = "windows-x86_64";
#else
    constexpr const char* kPlatform = "linux-x86_64";
#endif

    std::mutex g_lock;
    aidyn::updater::Status g_status;
    std::vector<std::string> g_args;
    std::atomic_bool g_busy{ false };   // a check or download thread is running
    std::atomic_bool g_stop{ false };   // cancel (or the program is closing)
    std::string g_manifest_url, g_sig_url, g_latest_tag;
    std::string g_just_updated;  // what the last run's update did (take_just_updated)

    // ---- small things ----

    std::string env(const char* name) {
        const char* v = std::getenv(name);
        return v != nullptr ? v : "";
    }
    std::string test_base() { return env("AIDYN_UPDATE_URL"); }

    // The install: the program's folder, noted once at start (an update moves the running program into .update/previous,
    // and the system then reports it there).
    fs::path g_root, g_exe_name;
    void note_install() {
        if (g_root.empty()) {
            const fs::path exe = aidyn::platform::exe_path();
            g_root = exe.parent_path();
            g_exe_name = exe.filename();
        }
    }
    fs::path install_dir() {
        note_install();
        return g_root;
    }
    fs::path update_dir() { return install_dir() / ".update"; }

    void log(const std::string& line) {
        std::fprintf(stdout, "Updates: %s\n", line.c_str());
        std::fflush(stdout);
    }

    std::string read_file(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    bool write_file(const fs::path& p, const std::string& text) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        const fs::path tmp = p.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << text;
            if (!out) return false;
        }
        fs::rename(tmp, p, ec);
        return !ec;
    }

    std::string hex(const uint8_t* b, size_t n) {
        static const char* d = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; i++) {
            s += d[b[i] >> 4];
            s += d[b[i] & 15];
        }
        return s;
    }

    bool unhex(const std::string& s, uint8_t* out, size_t n) {
        if (s.size() != 2 * n) return false;
        for (size_t i = 0; i < n; i++) {
            const auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            const int hi = v(s[2 * i]), lo = v(s[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            out[i] = (uint8_t)(hi << 4 | lo);
        }
        return true;
    }

    std::vector<uint8_t> unbase64(const std::string& s) {
        std::vector<uint8_t> out;
        uint32_t acc = 0;
        int bits = 0;
        for (char c : s) {
            int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
                  : c == '+' ? 62 : c == '/' ? 63 : -1;
            if (v < 0) continue;  // padding, line ends
            acc = acc << 6 | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back((uint8_t)(acc >> bits));
            }
        }
        return out;
    }

    // BLAKE2b-256 of a file, hex; empty if it can't be read. Stops (empty) when g_stop is set.
    std::string hash_file(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return "";
        crypto_blake2b_ctx ctx;
        crypto_blake2b_init(&ctx, 32);
        std::vector<char> buf(1 << 20);
        while (in) {
            in.read(buf.data(), (std::streamsize)buf.size());
            const std::streamsize n = in.gcount();
            if (n > 0) crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(buf.data()), (size_t)n);
            if (g_stop) return "";
        }
        uint8_t h[32];
        crypto_blake2b_final(&ctx, h);
        return hex(h, sizeof h);
    }

    // ---- versions ----

    // "0.9.1", "0.9.1-patch2", "0.8.2-beta.1": the number, then a patch above it, a beta / rc below it.
    struct Version {
        int a = 0, b = 0, c = 0, rank = 1, n = 0;
        bool ok = false;
    };

    Version parse_version(std::string s) {
        Version v;
        if (!s.empty() && s[0] == 'v') s.erase(0, 1);
        static const std::regex re(R"(^(\d+)\.(\d+)\.(\d+)(?:-([a-z]+)\.?(\d+)?)?$)");
        std::smatch m;
        if (!std::regex_match(s, m, re)) return v;
        v.a = std::stoi(m[1]);
        v.b = std::stoi(m[2]);
        v.c = std::stoi(m[3]);
        if (m[4].matched) {
            v.rank = m[4] == "patch" ? 2 : 0;
            v.n = m[5].matched ? std::stoi(m[5]) : 0;
        }
        v.ok = true;
        return v;
    }

    bool newer(const Version& x, const Version& y) {
        return std::tie(x.a, x.b, x.c, x.rank, x.n) > std::tie(y.a, y.b, y.c, y.rank, y.n);
    }

    // This build's version: the release's (make_release.sh sets it: "v0.9.1-patch2"), "" for a development build.
    std::string current_version() {
        const std::string forced = env("AIDYN_UPDATE_VERSION");
        if (!forced.empty()) return forced;
        std::string t = AIDYN_VERSION_TEXT;
        if (!t.empty() && t[0] == 'v') t.erase(0, 1);
        return parse_version(t).ok ? t : "";
    }

    // ---- curl ----

    std::string curl_error(int code, const fs::path& err) {
        switch (code) {
        case -1: return "couldn't run curl";
        case -2: return "stopped";
        case 6: return "no connection (couldn't find the server)";
        case 7: return "no connection (couldn't reach the server)";
        case 22: {
            std::string e = read_file(err);
            const size_t at = e.find("error: ");
            e = at != std::string::npos ? e.substr(at + 7) : "the server refused";
            e.erase(std::remove(e.begin(), e.end(), '\n'), e.end());
            return e;
        }
        case 28: return "the connection timed out";
        case 35: case 60: return "a secure connection couldn't be made";
        default: return "curl failed (" + std::to_string(code) + ")";
        }
    }

    // Fetches url into out. progress (optional): called with out's size so far, about four times a second.
    std::string fetch(const std::string& url, const fs::path& out, bool api, const std::function<void(uint64_t)>& progress = {}) {
        const fs::path curl = aidyn::platform::find_curl();
        if (curl.empty()) return "this computer has no curl";
        std::vector<std::string> args = { curl.string(), "-fsSL", "--connect-timeout", "15", "--retry", "2",
                                          "-A", "AidynReChronicled/" + (current_version().empty() ? std::string("dev") : current_version()) };
        if (test_base().empty()) {
            args.insert(args.end(), { "--proto", "=https", "--proto-redir", "=https" });
        } else if (!env("AIDYN_UPDATE_RATE").empty()) {  // testing: a slow download, to see its progress
            args.insert(args.end(), { "--limit-rate", env("AIDYN_UPDATE_RATE") });
        }
        if (api) {
            args.insert(args.end(), { "--max-time", "60", "-H", "Accept: application/vnd.github+json" });
        }
        args.insert(args.end(), { "-o", out.string(), url });
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        const fs::path err = update_dir() / "curl-error.txt";
        const int code = aidyn::platform::run_process(args, err, [&] {
            if (progress) progress(fs::exists(out, ec) ? fs::file_size(out, ec) : 0);
            return !g_stop.load();
        });
        if (code == 0) return "";
        fs::remove(out, ec);
        return curl_error(code, err);
    }

    // ---- release notes: Markdown to plain lines ----

    std::string plain_notes(std::string md) {
        md = std::regex_replace(md, std::regex(R"(\[([^\]]*)\]\([^)]*\))"), "$1");  // links: their words
        std::string out, line;
        std::istringstream in(md);
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t h = 0;
            while (h < line.size() && line[h] == '#') h++;
            if (h > 0 && h < line.size() && line[h] == ' ') line = line.substr(h + 1);
            if (line.rfind("* ", 0) == 0) line = "- " + line.substr(2);
            std::string clean;
            for (size_t i = 0; i < line.size(); i++) {
                if (line[i] == '`' || (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '*')) {
                    if (line[i] == '*') i++;
                    continue;
                }
                clean += line[i];
            }
            out += clean + "\n";
        }
        return out;
    }

    // ---- the check ----

    void set_phase(aidyn::updater::Phase p, const std::string& error = "") {
        std::lock_guard<std::mutex> l(g_lock);
        g_status.phase = p;
        g_status.error = error;
    }

    void run_check() {
        using aidyn::updater::Phase;
        const std::string cur = current_version();
        const std::string base = test_base();
        const fs::path list = update_dir() / "releases.json";
        const std::string err = fetch(base.empty() ? kReleasesApi : base + "/releases.json", list, true);
        if (!err.empty()) {
            log("couldn't check: " + err);
            set_phase(Phase::Failed, "Couldn't check: " + err);
            return;
        }
        json releases;
        try {
            releases = json::parse(read_file(list));
        } catch (...) {
            set_phase(Phase::Failed, "Couldn't check: GitHub's answer wasn't readable");
            return;
        }
        std::error_code ec;
        fs::remove(list, ec);
        const bool tests = aidyn::settings::updates().test_builds;
        const Version have = parse_version(cur);
        const json* best = nullptr;
        Version best_v;
        for (const json& r : releases) {
            if (!r.is_object() || r.value("draft", false)) continue;
            if (r.value("prerelease", false) && !tests) continue;
            const Version v = parse_version(r.value("tag_name", ""));
            if (!v.ok || !newer(v, have)) continue;
            if (best == nullptr || newer(v, best_v)) {
                best = &r;
                best_v = v;
            }
        }
        if (best == nullptr) {
            log("up to date (" + cur + ")");
            set_phase(Phase::UpToDate);
            return;
        }
        const json& r = *best;
        std::string manifest, sig;
        for (const json& a : r.value("assets", json::array())) {
            const std::string name = a.value("name", ""), url = a.value("browser_download_url", "");
            if (name == "update.json") manifest = url;
            if (name == "update.json.sig") sig = url;
        }
        std::string tag = r.value("tag_name", "");
        log("newer release " + tag + (manifest.empty() ? " (no update manifest: the page only)" : ""));
        std::lock_guard<std::mutex> l(g_lock);
        g_manifest_url = manifest;
        g_sig_url = sig;
        g_latest_tag = tag;
        if (!tag.empty() && tag[0] == 'v') tag.erase(0, 1);
        g_status.latest = tag;
        g_status.latest_test = r.value("prerelease", false);
        g_status.notes = plain_notes(r.value("body", ""));
        g_status.page = r.value("html_url", "");
        g_status.can_download = !manifest.empty() && !sig.empty() && !aidyn::platform::find_curl().empty();
        g_status.phase = Phase::Available;
        g_status.error.clear();
    }

    // ---- the download ----

    bool safe_path(const std::string& p) {
        if (p.empty() || p[0] == '/' || p[0] == '\\' || p.find(':') != std::string::npos || p.find('\\') != std::string::npos) return false;
        if (p.rfind(".update", 0) == 0) return false;
        std::string part;
        std::istringstream in(p);
        while (std::getline(in, part, '/')) {
            if (part.empty() || part == "." || part == "..") return false;
        }
        return true;
    }

    bool allowed_url(const std::string& url) {
        const std::string base = test_base();
        if (!base.empty()) return url.rfind(base + "/", 0) == 0;
        return url.rfind(kAssetPrefix, 0) == 0;
    }

    bool signature_ok(const std::string& manifest, const std::string& sig_text) {
        uint8_t key[32];
        const std::string test_key = env("AIDYN_UPDATE_KEY");
        if (!unhex(!test_base().empty() && !test_key.empty() ? test_key : kPublicKey, key, sizeof key)) return false;
        const std::vector<uint8_t> sig = unbase64(sig_text);
        if (sig.size() != 64) return false;
        return crypto_ed25519_check(sig.data(), key, reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()) == 0;
    }

    // The installed file's hash, from the cache when its size and time are unchanged.
    std::string installed_hash(json& cache, const std::string& path) {
        const fs::path f = install_dir() / fs::path(path);
        std::error_code ec;
        if (!fs::is_regular_file(f, ec)) return "";
        const uint64_t size = fs::file_size(f, ec);
        const int64_t time = (int64_t)fs::last_write_time(f, ec).time_since_epoch().count();
        const json& c = cache.contains(path) ? cache[path] : json();
        if (c.is_object() && c.value("size", (uint64_t)0) == size && c.value("time", (int64_t)0) == time) {
            return c.value("hash", "");
        }
        const std::string h = hash_file(f);
        if (!h.empty()) cache[path] = { { "size", size }, { "time", time }, { "hash", h } };
        return h;
    }

    void run_download() {
        using aidyn::updater::Phase;
        std::string manifest_url, sig_url, want;
        {
            std::lock_guard<std::mutex> l(g_lock);
            manifest_url = g_manifest_url;
            sig_url = g_sig_url;
            want = g_status.latest;
            g_status.phase = Phase::Downloading;
            g_status.done = g_status.total = 0;
            g_status.files_done = g_status.files_total = 0;
            g_status.error.clear();
        }
        const fs::path dir = update_dir();
        std::error_code ec;
        fs::remove_all(dir / "new", ec);
        fs::remove(dir / "ready.json", ec);
        const auto fail = [&](const std::string& why) {
            log("download failed: " + why);
            set_phase(g_stop ? Phase::Available : Phase::Failed, g_stop ? "" : "Download failed: " + why);
        };
        std::string err = fetch(manifest_url, dir / "download" / "update.json", false);
        if (err.empty()) err = fetch(sig_url, dir / "download" / "update.json.sig", false);
        if (!err.empty()) return fail("couldn't fetch the update's list: " + err);
        const std::string manifest = read_file(dir / "download" / "update.json");
        if (!signature_ok(manifest, read_file(dir / "download" / "update.json.sig"))) {
            return fail("the update's list isn't signed by this game's key (refused)");
        }
        json m;
        try {
            m = json::parse(manifest);
        } catch (...) {
            return fail("the update's list isn't readable");
        }
        if (m.value("format", 0) != 1 || m.value("version", "") != want) return fail("the update's list is for another version");
        json files = json::array();
        try {
            files = m.value("platforms", json::object()).value(kPlatform, json::object()).value("files", json::array());
        } catch (...) {
        }
        if (files.empty()) return fail(std::string("the update has nothing for ") + kPlatform);
        json cache;
        try {
            cache = json::parse(read_file(dir / "hashes.json"));
        } catch (...) {
            cache = json::object();
        }
        struct Need { std::string path, url, hash; uint64_t size; bool exec; };
        std::vector<Need> need;
        json ready_files = json::array();
        uint64_t total = 0;
        for (const json& f : files) {
            const std::string path = f.value("path", ""), url = f.value("url", ""), h = f.value("blake2b", "");
            const uint64_t size = f.value("size", (uint64_t)0);
            if (!safe_path(path) || !allowed_url(url) || h.size() != 64) return fail("the update's list has a bad entry (" + path + ")");
            if (installed_hash(cache, path) == h) continue;
            if (g_stop) return fail("stopped");
            need.push_back({ path, url, h, size, f.value("exec", false) });
            ready_files.push_back({ { "path", path }, { "exec", f.value("exec", false) } });
            total += size;
        }
        write_file(dir / "hashes.json", cache.dump());
        {
            std::lock_guard<std::mutex> l(g_lock);
            g_status.total = total;
            g_status.files_total = (int)need.size();
        }
        log("downloading " + std::to_string(need.size()) + " files, " + std::to_string(total) + " bytes");
        uint64_t done = 0;
        for (const Need& n : need) {
            const fs::path dst = dir / "new" / fs::path(n.path);
            const fs::path part = dst.string() + ".part";
            std::string why;
            for (int attempt = 0; attempt < 2; attempt++) {
                why = fetch(n.url, part, false, [&](uint64_t got) {
                    std::lock_guard<std::mutex> l(g_lock);
                    g_status.done = done + std::min(got, n.size);
                });
                if (!why.empty()) break;  // curl retries a broken connection itself
                if (hash_file(part) == n.hash) break;
                why = n.path + " didn't match its checksum";
                fs::remove(part, ec);
            }
            if (!why.empty()) return fail(why);
            fs::rename(part, dst, ec);
            if (ec) return fail("couldn't keep " + n.path);
            done += n.size;
            std::lock_guard<std::mutex> l(g_lock);
            g_status.done = done;
            g_status.files_done++;
        }
        const json ready = { { "version", want }, { "from", current_version() }, { "files", ready_files } };
        if (!write_file(dir / "ready.json", ready.dump(1))) return fail("couldn't write the update's list");
        fs::remove_all(dir / "download", ec);
        log("ready: " + want);
        set_phase(Phase::Ready);
    }

    void start_thread(void (*work)()) {
        bool expected = false;
        if (!g_busy.compare_exchange_strong(expected, true)) return;
        g_stop = false;
        std::thread([work] {
            work();
            g_busy = false;
        }).detach();
    }

    // ---- moving files ----

    // Moves from -> to, making to's folder. Over an existing file only where the system allows it (never a running
    // program: those are moved away first).
    bool move(const fs::path& from, const fs::path& to) {
        std::error_code ec;
        fs::create_directories(to.parent_path(), ec);
        fs::rename(from, to, ec);
        return !ec;
    }

    void make_executable(const fs::path& p) {
        std::error_code ec;
        fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read |
                               fs::perms::others_exec, fs::perm_options::replace, ec);
    }

    // Applies .update/ready.json. True when the install now holds the new version.
    bool apply_ready(std::string* why) {
        const fs::path dir = update_dir(), root = install_dir();
        std::error_code ec;
        json ready;
        try {
            ready = json::parse(read_file(dir / "ready.json"));
        } catch (...) {
            fs::remove(dir / "ready.json", ec);
            *why = "the downloaded update's list was unreadable";
            return false;
        }
        const std::string from = ready.value("from", ""), to = ready.value("version", "");
        if (from != current_version()) {  // another version was put in by hand since: that download is stale
            fs::remove(dir / "ready.json", ec);
            fs::remove_all(dir / "new", ec);
            *why = "the downloaded update was for " + from + ", not this version";
            return false;
        }
        fs::remove_all(dir / "previous", ec);
        struct Done { fs::path dst, prev; bool replaced; };
        std::vector<Done> done;
        json replaced = json::array(), added = json::array();
        bool ok = true;
        for (const json& f : ready.value("files", json::array())) {
            const std::string path = f.value("path", "");
            if (!safe_path(path)) continue;
            const fs::path src = dir / "new" / fs::path(path), dst = root / fs::path(path), prev = dir / "previous" / fs::path(path);
            const bool existed = fs::exists(dst, ec);
            if (existed && !move(dst, prev)) { *why = "couldn't move " + path + " aside"; ok = false; break; }
            if (!move(src, dst)) {
                if (existed) move(prev, dst);
                *why = "couldn't put " + path + " in place";
                ok = false;
                break;
            }
            if (f.value("exec", false)) make_executable(dst);
            done.push_back({ dst, prev, existed });
            (existed ? replaced : added).push_back(path);
        }
        if (!ok) {  // put back what was moved: the new files go to .update/failed (a running one can't be deleted)
            for (auto it = done.rbegin(); it != done.rend(); ++it) {
                move(it->dst, dir / "failed" / fs::relative(it->dst, root, ec));
                if (it->replaced) move(it->prev, it->dst);
            }
            fs::remove(dir / "ready.json", ec);
            fs::remove_all(dir / "new", ec);
            write_file(dir / "result.json", json({ { "error", *why } }).dump());
            return false;
        }
        write_file(dir / "previous" / "info.json", json({ { "version", from }, { "replaced", replaced }, { "added", added } }).dump(1));
        write_file(dir / "result.json", json({ { "done", "Updated to " + to } }).dump());
        fs::remove(dir / "ready.json", ec);
        fs::remove_all(dir / "new", ec);
        fs::remove(dir / "hashes.json", ec);  // the files changed: hashed again next time
        return true;
    }

    void restart_installed() {
        std::fflush(nullptr);
        const fs::path exe = install_dir() / g_exe_name;
        aidyn::platform::restart(g_args.empty() ? std::vector<std::string>{ exe.string() } : g_args, exe);
    }
}

void aidyn::updater::apply_pending(const std::vector<std::string>& args) {
    g_args = args;
    note_install();
    const fs::path dir = update_dir();
    std::error_code ec;
    fs::remove_all(dir / "failed", ec);  // what an earlier run couldn't delete (no longer running now)
    fs::remove_all(dir / "undone", ec);
    if (!fs::exists(dir / "ready.json", ec)) return;
    std::string why;
    if (apply_ready(&why)) {
        restart_installed();
    }
    std::fprintf(stderr, "Updates: not applied: %s\n", why.c_str());
}

void aidyn::updater::init() {
    static bool registered = false;
    if (!registered) {
        registered = true;
        std::atexit([] { g_stop = true; });  // a download's curl is stopped as the game closes
    }
    const fs::path dir = update_dir();
    std::error_code ec;
    std::lock_guard<std::mutex> l(g_lock);
    g_status.current = current_version();
    if (fs::exists(dir / "result.json", ec)) {
        try {
            const json r = json::parse(read_file(dir / "result.json"));
            if (r.contains("done")) {
                g_just_updated = r.value("done", "");
                log(g_just_updated);
            } else {
                g_status.phase = Phase::Failed;
                g_status.error = "The update wasn't applied: " + r.value("error", "");
                log(g_status.error);
            }
        } catch (...) {
        }
        fs::remove(dir / "result.json", ec);
    }
    if (fs::exists(dir / "previous" / "info.json", ec)) {
        try {
            g_status.undo_to = json::parse(read_file(dir / "previous" / "info.json")).value("version", "");
        } catch (...) {
        }
    }
    if (g_status.current.empty()) {
        log("a development build: no checks");
        return;
    }
    if (fs::exists(dir / "ready.json", ec)) {  // downloaded but not applied (Restart now couldn't, or apply failed)
        try {
            g_status.latest = json::parse(read_file(dir / "ready.json")).value("version", "");
            g_status.phase = Phase::Ready;
        } catch (...) {
        }
        return;
    }
    if (aidyn::settings::updates().check && g_status.phase != Phase::Failed) {
        g_status.phase = Phase::Checking;
        start_thread(run_check);
    }
}

aidyn::updater::Status aidyn::updater::status() {
    std::lock_guard<std::mutex> l(g_lock);
    return g_status;
}

std::string aidyn::updater::take_just_updated() {
    std::lock_guard<std::mutex> l(g_lock);
    std::string s;
    s.swap(g_just_updated);
    return s;
}

void aidyn::updater::check() {
    if (current_version().empty() || g_busy) return;
    set_phase(Phase::Checking);
    start_thread(run_check);
}

void aidyn::updater::download() {
    {
        std::lock_guard<std::mutex> l(g_lock);
        if (g_status.phase != Phase::Available || !g_status.can_download) return;
    }
    start_thread(run_download);
}

void aidyn::updater::cancel() {
    g_stop = true;
}

void aidyn::updater::restart_now() {
    std::string why;
    if (apply_ready(&why)) {
        restart_installed();
        why = "couldn't start the new version";
    }
    log("not applied: " + why);
    set_phase(Phase::Failed, "The update wasn't applied: " + why);
}

void aidyn::updater::undo_now() {
    const fs::path dir = update_dir(), root = install_dir();
    std::error_code ec;
    json info;
    try {
        info = json::parse(read_file(dir / "previous" / "info.json"));
    } catch (...) {
        set_phase(Phase::Failed, "There's nothing to undo");
        return;
    }
    // The current files move to .update/undone (deleted at the next start), the earlier ones back into place.
    for (const json& p : info.value("added", json::array())) {
        const std::string path = p.get<std::string>();
        if (safe_path(path)) move(root / fs::path(path), dir / "undone" / fs::path(path));
    }
    for (const json& p : info.value("replaced", json::array())) {
        const std::string path = p.get<std::string>();
        if (!safe_path(path)) continue;
        move(root / fs::path(path), dir / "undone" / fs::path(path));
        if (!move(dir / "previous" / fs::path(path), root / fs::path(path))) {
            set_phase(Phase::Failed, "Undo couldn't put back " + path);
            return;
        }
    }
    const std::string to = info.value("version", "");
    fs::remove_all(dir / "previous", ec);
    fs::remove(dir / "hashes.json", ec);
    write_file(dir / "result.json", json({ { "done", "Back to " + to } }).dump());
    log("undone: back to " + to);
    restart_installed();
    set_phase(Phase::Failed, "Undo put the files back, but couldn't restart: start the game again");
}

bool aidyn::updater::open_page() {
    std::string page;
    {
        std::lock_guard<std::mutex> l(g_lock);
        page = g_status.page;
    }
    if (page.empty()) page = "https://github.com/spitzdano/AidynReChronicled/releases";
    return SDL_OpenURL(page.c_str()) == 0;
}

int aidyn::updater::selftest(const std::string& mode) {
    init();
    std::printf("SELFTEST: this is %s; %s\n", current_version().c_str(), mode.c_str());
    if (mode == "undo") {
        const Status s = status();
        if (s.undo_to.empty()) {
            std::printf("SELFTEST: nothing to undo: done\n");
            return 0;
        }
        std::printf("SELFTEST: undoing to %s\n", s.undo_to.c_str());
        std::fflush(stdout);
        undo_now();
        std::printf("SELFTEST: undo failed: %s\n", status().error.c_str());
        return 1;
    }
    if (status().phase == Phase::Off) check();
    Phase last = Phase::Off;
    for (int i = 0; i < 1200; i++) {  // two minutes
        const Status s = status();
        if (s.phase != last || s.phase == Phase::Downloading) {
            std::printf("SELFTEST: phase %d %s%s (%llu of %llu bytes)\n", (int)s.phase, s.latest.c_str(),
                        s.error.empty() ? "" : (": " + s.error).c_str(), (unsigned long long)s.done, (unsigned long long)s.total);
            std::fflush(stdout);
            last = s.phase;
        }
        switch (s.phase) {
        case Phase::UpToDate: std::printf("SELFTEST: up to date: done\n"); return 0;
        case Phase::Failed: return 1;
        case Phase::Available:
            if (!s.can_download) { std::printf("SELFTEST: can't download (no curl or no manifest)\n"); return 1; }
            download();
            break;
        case Phase::Ready:
            std::fflush(stdout);
            restart_now();
            std::printf("SELFTEST: restart failed: %s\n", status().error.c_str());
            return 1;
        default: break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::printf("SELFTEST: timed out\n");
    return 1;
}
