// In-game updates (src/main/updater.cpp): the title menu's Updates (src/game/patches.cpp, "Updates") asks here.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aidyn::updater {
    enum class Phase {
        Off,          // not a release build (a development build checks nothing), or not checked yet
        Checking,     // asking GitHub
        UpToDate,     // nothing newer
        Available,    // a newer release: `latest`, its notes
        Downloading,  // `done` of `total` bytes
        Ready,        // downloaded and checked: applied at the next start (or Restart now)
        Failed,       // `error` says why
    };

    struct Status {
        Phase phase = Phase::Off;
        std::string current;      // this build's version ("0.9.1-patch2"), empty for a development build
        std::string latest;       // the newer release's version
        bool latest_test = false; // it's a pre-release (a test build)
        std::string notes;        // its release notes, as plain text (paragraphs and "- " lines)
        std::string page;         // its page on GitHub
        bool can_download = false;  // it has a signed manifest and this computer has curl (else: the page)
        std::string error;
        uint64_t done = 0, total = 0;  // Downloading: bytes
        int files_done = 0, files_total = 0;
        std::string undo_to;      // the version Undo update goes back to (empty: nothing to undo)
    };

    // main(), first thing: applies a downloaded update (or finishes one) before any file of the game is opened, and
    // starts the new program in place of this one. Returns only if there was nothing to do (or it failed: noted for
    // the Updates panel).
    void apply_pending(const std::vector<std::string>& args);
    // main(), once settings are loaded: what the last run left, and the check at start (settings.json updates.check).
    void init();
    Status status();
    std::string take_just_updated();  // once after an update or undo: "Updated to 0.9.2" / "Back to 0.9.1"; then ""
    void check();          // look again now (the panel's Check again)
    void download();       // fetch the update (Phase Available with can_download)
    void cancel();         // stop a download
    void restart_now();    // apply a Ready update and start the new program (returns only if it failed)
    void undo_now();       // put the last update's files back and restart (returns only if it failed)
    bool open_page();      // the release page in the system's browser
    // Developers (AIDYN_UPDATE_SELFTEST, with AIDYN_UPDATE_URL): check, download and restart into the update without a
    // window (=undo: undo the last update and restart), printing each step; the restarted program does it again and
    // stops once there is nothing left to do. For a computer that can't draw the game (the Windows VM). Its exit code.
    int selftest(const std::string& mode);
}
