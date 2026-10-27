// Aidyn ReChronicled (Aidyn Chronicles: The First Mage, recompiled for PC): runtime shell.
//
// Modelled on Zelda64Recomp's main.cpp, stripped to what the runtime needs to boot a game:
// an SDL window for RT64, SDL audio, SDL input, and the recompiled audio microcode. No launcher
// UI, no mods, no patches. The player's own ROM is given with --rom (or remembered from the
// last run, since librecomp stores a copy of a validated ROM in the config folder).
#include "platform.h"
#include "report.h"
#include "watch.h"
#include "settings.h"
#include "version.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"

#ifndef _WIN32  // on Windows SDL2main's WinMain starts the program and calls main() (as SDL_main)
#define SDL_MAIN_HANDLED
#endif
#include "SDL2/SDL.h"
#ifdef _WIN32
#include "SDL2/SDL_syswm.h"
#endif

#include "rt64_context.h"
#include "controls.h"
#include "input.h"
#include "audio.h"
#include "nfd.h"

extern RspUcodeFunc aspMain;
extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();
void aidyn_register_overlays();

extern std::atomic_bool aidyn_quitting;

static SDL_Window* window = nullptr;

static void quit() {
    aidyn_quitting.store(true);
    ultramodern::quit();
}

// Stops the program with a message: on stderr, and in a message box, since a game started from a launcher has no
// terminal to show it.
[[noreturn]] static void fatal(const char* fmt, const char* arg = "") {
    char text[1024];
    snprintf(text, sizeof(text), fmt, arg);
    fprintf(stderr, "%s\n", text);
    std::fflush(stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Aidyn ReChronicled", text, window);
    std::exit(EXIT_FAILURE);
}

static void message_box(const char* msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Aidyn ReChronicled", msg, window);
}

static ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        fatal("Failed to initialize SDL2: %s", SDL_GetError());
    }
    fprintf(stdout, "SDL video driver: %s\n", SDL_GetCurrentVideoDriver());
    aidyn::platform::startup_mark("SDL ready");
    return {};
}

static ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    // The game renders 4:3 at 320x240 (high-res 640x480 with the Expansion Pak).
#ifdef _WIN32
    constexpr Uint32 kWindowFlags = SDL_WINDOW_RESIZABLE;  // RT64 makes its own surface from the HWND
#else
    constexpr Uint32 kWindowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;
#endif
    window = SDL_CreateWindow("Aidyn ReChronicled", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1280, 960, kWindowFlags);
    if (window == nullptr) {
        fatal("Failed to create window: %s", SDL_GetError());
    }
    // On Wayland a window only learns its display (and can only get a Vulkan surface) once the
    // compositor has configured it, which needs the event loop to run. RT64 is created right after this
    // returns, so wait here until the window is ready (a few frames at most).
    SDL_ShowWindow(window);
    for (int i = 0; i < 300 && SDL_GetWindowDisplayIndex(window) < 0; i++) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    if (SDL_GetWindowDisplayIndex(window) < 0) {
        fprintf(stderr, "Warning: the window still has no display after 3 s (%s); continuing\n", SDL_GetError());
    }
#ifdef _WIN32
    // On Windows RT64 takes the native window and the thread that pumps its messages (as in Zelda64Recomp).
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        fatal("Failed to get the window's handle: %s", SDL_GetError());
    }
    return ultramodern::renderer::WindowHandle{ info.info.win.window, GetCurrentThreadId() };
#else
    return ultramodern::renderer::WindowHandle{ window };
#endif
}

static std::filesystem::path data_folder();

// F1: the manual (tools/manual/build_manual.py writes it into the data folder) in the default browser. Fullscreen is
// left first, so the browser's window isn't hidden behind the game.
static void open_manual() {
    const std::filesystem::path page = data_folder() / "manual" / "index.html";
    std::error_code ec;
    if (!std::filesystem::exists(page, ec)) {
        fprintf(stderr, "No manual in %s yet: build it with tools/manual/build_manual.py\n", page.parent_path().string().c_str());
        return;
    }
    if (aidyn::settings::graphics().fullscreen) {
        aidyn::set_borderless(false, false);  // for now: the next start is borderless again
    }
#ifdef _WIN32
    const std::string url = "file:///" + page.generic_string();  // file:///C:/Users/...
#else
    const std::string url = "file://" + page.string();
#endif
    if (SDL_OpenURL(url.c_str()) != 0) {
        fprintf(stderr, "Could not open %s: %s\n", url.c_str(), SDL_GetError());
    }
}

static void update_gfx(void*) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            quit();
            return;
        case SDL_KEYDOWN:
            if (event.key.keysym.scancode == SDL_SCANCODE_F11 || (event.key.keysym.scancode == SDL_SCANCODE_RETURN && (event.key.keysym.mod & KMOD_ALT))) {
                aidyn::set_borderless(!aidyn::settings::graphics().fullscreen);
            }
            else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE && (event.key.keysym.mod & KMOD_SHIFT)) {
                quit();
                return;
            }
            else if (event.key.keysym.scancode == SDL_SCANCODE_F1 && !event.key.repeat) {
                open_manual();
            }
            else if (event.key.keysym.scancode == SDL_SCANCODE_F2 && !event.key.repeat) {
                aidyn::toggle_redrawn_art();
            }
            break;
        default:
            break;
        }
        aidyn::input::handle_event(event);
        aidyn::controls::handle_event(event);
    }
    aidyn::controls::apply_mouse_mode();  // the field captures the mouse for the camera, anything else lets it go
}

// Audio tasks run the recompiled aspMain. Anything else is a bug in the port: graphics tasks go to RT64
// (never here), and no other microcode is known (C9: fail visibly, with the task's details).
static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) {
        return aspMain;
    }
    fprintf(stderr, "Unknown RSP task type %" PRIu32 ": ucode 0x%08" PRIX32 ", ucode_data 0x%08" PRIX32 ", data_ptr 0x%08" PRIX32 "\n",
            task->t.type, task->t.ucode, task->t.ucode_data, task->t.data_ptr);
    std::abort();
}

static ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    if (controller_num == 0) {
        return { ultramodern::input::Device::Controller, ultramodern::input::Pak::RumblePak };
    }
    return { ultramodern::input::Device::None, ultramodern::input::Pak::None };
}

static std::filesystem::path data_folder() {
    const std::filesystem::path base = aidyn::platform::data_home();
    std::filesystem::path folder = base / "AidynReChronicled";
    // The project was called AidynRecompiled: its folder (saves, settings, the stored ROMs, texture packs) moves over once.
    static bool checked = false;
    if (!checked) {
        checked = true;
        std::error_code ec;
        const std::filesystem::path old = base / "AidynRecompiled";
        if (!std::filesystem::exists(folder, ec) && std::filesystem::is_directory(old, ec)) {
            std::filesystem::rename(old, folder, ec);
            if (ec) {
                const std::string why = old.string() + " to " + folder.string() + ": " + ec.message();
                fatal("Could not move %s", why.c_str());
            }
            printf("Moved %s to %s\n", old.string().c_str(), folder.string().c_str());
        }
    }
    return folder;
}

void aidyn_pak_set_path(const std::filesystem::path& path); // src/game/pak.cpp
void aidyn_save_pictures_set_dir(const std::filesystem::path& dir); // src/game/save_pictures.cpp
void aidyn_quick_saves_set_pak(const std::filesystem::path& pak); // src/game/patches.cpp

// The three ROMs the port runs, each the player's own: the 1.0 release, or it with halibabica's (u/halibabica) community
// Aidyn Patch or Aidyn Plus applied by the player. Nothing of either is in this project: a ROM given with
// --rom (or chosen at the first start) is checked by its XXH3-64 against the three below and stored. Both patches change
// only data (potion recipes and prices, skill and weapon XP rates, the entity, weapon, armour, spell, item and chest
// databases; measured byte by byte against the 1.0 ROM), no code, so the same recompiled code runs all three.
// settings.json "game.rom_patch" picks one; each keeps its own Controller Pak.
static const char8_t game_id[] = u8"aidyn.n64.us.1.0";
static const char8_t game_id_aidyn_patch[] = u8"aidyn.n64.us.1.0.aidyn-patch";
static const char8_t game_id_aidyn_plus[] = u8"aidyn.n64.us.1.0.aidyn-plus";

static const char8_t* game_id_for(aidyn::settings::RomPatch version) {
    switch (version) {
        case aidyn::settings::RomPatch::AidynPatch: return game_id_aidyn_patch;
        case aidyn::settings::RomPatch::AidynPlus: return game_id_aidyn_plus;
        default: return game_id;
    }
}

static const char* version_name(aidyn::settings::RomPatch version) {
    switch (version) {
        case aidyn::settings::RomPatch::AidynPatch: return "Aidyn Patch";
        case aidyn::settings::RomPatch::AidynPlus: return "Aidyn Plus";
        default: return "the original";
    }
}

static const char* version_flag_arg(aidyn::settings::RomPatch version) {
    switch (version) {
        case aidyn::settings::RomPatch::AidynPatch: return "--patch";
        case aidyn::settings::RomPatch::AidynPlus: return "--plus";
        default: return "--original";
    }
}

// The command line, for restarting into the other version (aidyn::switch_version).
static std::vector<std::string> g_args;

// The title screen's version list (src/game/patches.cpp): a version is there when its ROM is stored.
bool aidyn::version_available(aidyn::settings::RomPatch version) {
    return recomp::is_rom_valid(game_id_for(version));
}

// Windowed or borderless: through RT64, as its own settings change it (RT64Context::update_config ->
// ApplicationWindow::setFullScreen on the renderer's thread: a borderless window over the whole screen on Windows,
// SDL's desktop fullscreen elsewhere), so its idea of the window always matches. settings.json graphics.fullscreen.
void aidyn::set_borderless(bool on, bool save) {
    aidyn::settings::graphics_rw().fullscreen = on;
    if (save) {
        aidyn::settings::save_bool("graphics", "fullscreen", on);
    }
    ultramodern::renderer::GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    config.wm_option = on ? ultramodern::renderer::WindowMode::Fullscreen : ultramodern::renderer::WindowMode::Windowed;
    ultramodern::renderer::set_graphics_config(config);
}

// Anti-aliasing: MSAA through RT64's own settings (RT64Context::update_config rebuilds its targets with the new sample
// count), supersampling through the resolution it draws at (set_application_user_config, on refresh_aspect's path).
void aidyn::set_antialiasing(int msaa, int supersampling) {
    aidyn::settings::graphics_rw().msaa = msaa;
    aidyn::settings::graphics_rw().supersampling = supersampling;
    aidyn::settings::save_number("graphics", "msaa", msaa);
    aidyn::settings::save_number("graphics", "supersampling", supersampling);
    ultramodern::renderer::GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    config.msaa_option = msaa == 8 ? ultramodern::renderer::Antialiasing::MSAA8X
                       : msaa == 4 ? ultramodern::renderer::Antialiasing::MSAA4X
                       : msaa == 2 ? ultramodern::renderer::Antialiasing::MSAA2X
                                   : ultramodern::renderer::Antialiasing::None;
    ultramodern::renderer::set_graphics_config(config);
    aidyn::refresh_aspect();
}

// The title screen's Quit Game (game thread): an SDL_QUIT for update_gfx, the way the window's close button ends it.
void aidyn::request_quit() {
    SDL_Event event{};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}

// The window's width / height (the picture's shape with aspect "window"), for the game thread's layout (the combat
// turn order, src/game/patches.cpp); 0 before the window exists.
float aidyn::window_aspect() {
    int w = 0, h = 0;
    if (window != nullptr) {
        SDL_GetWindowSize(window, &w, &h);
    }
    return h > 0 ? (float)w / (float)h : 0.0f;
}

// Saves the choice in settings.json and starts this program again in its place, with the same arguments but the
// version flag replaced. Game data comes from the ROM at start-up, so a version is only changed by a restart; the
// Controller Pak is written through at once, so nothing is lost.
void aidyn::switch_version(aidyn::settings::RomPatch version) {
    aidyn::settings::save_rom_patch(version);
    std::vector<std::string> args;
    for (size_t i = 0; i < g_args.size(); i++) {
        if (g_args[i] == "--patch" || g_args[i] == "--plus" || g_args[i] == "--original") {
            continue;
        }
        if (g_args[i] == "--rom" && i + 1 < g_args.size()) {
            i++;  // stored after the first run
            continue;
        }
        args.push_back(g_args[i]);
    }
    args.push_back(version_flag_arg(version));
    printf("Switching to %s\n", version_name(version));
    fflush(stdout);
    aidyn::watch::session_end();  // not an unclean end: the next process starts its own session
    aidyn::platform::restart(args);
}

// The ROMs folder: the player's ROMs, put in ROMs beside the game (the Windows download, the installed Linux game) or one
// level up (the unpacked Linux download, which keeps the game in game/). At every start, each version (the 1.0 release,
// Aidyn Patch, Aidyn Plus) whose ROM isn't stored yet is looked for there and stored when found, whatever the files
// are called: they're told apart by their hash. Only 32 MiB files with "AIDYN" in the cartridge header (in any of the
// three byte orders) are read in full, so a folder of other games costs next to nothing.
static void store_roms_from_folders() {
    const std::pair<const char8_t*, const char*> versions[] = { { game_id, "the NTSC-U 1.0 ROM" },
                                                               { game_id_aidyn_patch, "the Aidyn Patch ROM" },
                                                               { game_id_aidyn_plus, "the Aidyn Plus ROM" } };
    const auto missing = [&] {
        for (const auto& [id, name] : versions) {
            if (!recomp::is_rom_valid(id)) {
                return true;
            }
        }
        return false;
    };
    if (!missing()) {
        return;
    }
    const std::filesystem::path here = aidyn::platform::exe_path().parent_path();
    std::error_code ec;
    for (const std::filesystem::path& dir : { here / "ROMs", here.parent_path() / "ROMs" }) {
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec) || entry.file_size(ec) != 32 * 1024 * 1024) {
                continue;
            }
            // The header's name at 0x20, put in the .z64 (big-endian) order by the first word's order.
            unsigned char head[0x40]{};
            std::ifstream in(entry.path(), std::ios::binary);
            if (!in.read(reinterpret_cast<char*>(head), sizeof head)) {
                continue;
            }
            if (head[0] == 0x37 && head[1] == 0x80) {  // .v64: 16-bit words swapped
                for (size_t i = 0; i + 1 < sizeof head; i += 2) {
                    std::swap(head[i], head[i + 1]);
                }
            }
            else if (head[0] == 0x40 && head[1] == 0x12) {  // .n64: 32-bit words little-endian
                for (size_t i = 0; i + 3 < sizeof head; i += 4) {
                    std::swap(head[i], head[i + 3]);
                    std::swap(head[i + 1], head[i + 2]);
                }
            }
            if (std::memcmp(head + 0x20, "AIDYN", 5) != 0) {
                continue;
            }
            for (const auto& [id, name] : versions) {
                if (!recomp::is_rom_valid(id) && recomp::select_rom(entry.path(), id) == recomp::RomValidationError::Good) {
                    printf("Stored %s from %s\n", name, entry.path().string().c_str());
                    fflush(stdout);
                    recomp::check_all_stored_roms();
                    break;
                }
            }
            if (!missing()) {
                return;
            }
        }
    }
}

// Checks a ROM the player gave and stores it in the data folder (librecomp keeps a copy): the 1.0 release, or it with
// Aidyn Patch or Aidyn Plus applied, whichever its hash says it is. Stops with the reason when it is none of them.
static void store_rom(const std::filesystem::path& rom_path) {
    recomp::RomValidationError result = recomp::select_rom(rom_path, game_id);
    const char* stored = "the NTSC-U 1.0 ROM";
    for (auto [id, name] : { std::pair{ game_id_aidyn_patch, "the Aidyn Patch ROM" },
                             std::pair{ game_id_aidyn_plus, "the Aidyn Plus ROM" } }) {
        if (result == recomp::RomValidationError::IncorrectVersion || result == recomp::RomValidationError::OtherError) {
            if (recomp::select_rom(rom_path, id) == recomp::RomValidationError::Good) {
                result = recomp::RomValidationError::Good;
                stored = name;
            }
        }
    }
    switch (result) {
    case recomp::RomValidationError::Good:
        printf("Stored %s.\n", stored);
        fflush(stdout);
        break;
    case recomp::RomValidationError::FailedToOpen:
        fatal("Could not open ROM: %s", rom_path.string().c_str());
    case recomp::RomValidationError::NotARom:
        fatal("Not an N64 ROM (a zipped ROM must be unzipped first): %s", rom_path.string().c_str());
    case recomp::RomValidationError::IncorrectVersion:
        fatal("This is Aidyn Chronicles, but not the NTSC-U 1.0 release (version byte 0), nor it with Aidyn Patch or "
              "Aidyn Plus applied (the patch must be applied to the 1.0 ROM, unchanged): %s", rom_path.string().c_str());
    default:
        fatal("This is not Aidyn Chronicles (NTSC-U 1.0): %s", rom_path.string().c_str());
    }
}

// The first start: explains what is wanted, then a file picker. Empty when cancelled or when no picker works here.
static std::filesystem::path pick_rom(aidyn::settings::RomPatch version) {
    const char* text =
        version == aidyn::settings::RomPatch::AidynPatch
            ? "Choose your Aidyn Chronicles ROM with Aidyn Patch applied (.z64).\n\nIt is checked and kept, so this is only asked once."
        : version == aidyn::settings::RomPatch::AidynPlus
            ? "Choose your Aidyn Chronicles ROM with Aidyn Plus applied (.z64).\n\nIt is checked and kept, so this is only asked once."
            : "Choose your Aidyn Chronicles ROM: the North American 1.0 cartridge (.z64).\n\nIt is checked and kept, so this is only asked once. (Or put your ROMs in the ROMs folder next to the game: they're found by themselves.)";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Aidyn ReChronicled", text, nullptr);
    if (NFD_Init() != NFD_OKAY) {
        fprintf(stderr, "No file picker: %s\n", NFD_GetError());
        return {};
    }
    std::filesystem::path chosen;
    nfdu8char_t* out = nullptr;
    const nfdu8filteritem_t filters[] = { { "N64 ROM", "z64,n64,v64" } };
    const nfdresult_t r = NFD_OpenDialogU8(&out, filters, 1, nullptr);
    if (r == NFD_OKAY) {
        chosen = std::filesystem::path(reinterpret_cast<const char8_t*>(out));
        NFD_FreePathU8(out);
    }
    else if (r == NFD_ERROR) {
        fprintf(stderr, "File picker failed: %s\n", NFD_GetError());
    }
    NFD_Quit();
    return chosen;
}

// The version this run plays, once the stored ROMs and the ROMs folder have been looked at: settings.json's (or
// --original / --patch / --plus's) when its ROM is there. If it isn't, and the version came from settings.json, any
// version whose ROM is there (the original first), for this run only (the title screen lights the one playing); a
// version asked for on the command line (the Linux app menu's entries) asks for its ROM first. With no ROM at all the
// file dialog asks, and whichever of the three is chosen is played. Without a dialog (--report) the asked version.
static aidyn::settings::RomPatch choose_version(bool asked, bool interactive) {
    using aidyn::settings::RomPatch;
    const RomPatch wanted = aidyn::settings::game().rom_patch;
    if (aidyn::version_available(wanted)) {
        return wanted;
    }
    const auto other = [wanted] {
        for (RomPatch v : { RomPatch::None, RomPatch::AidynPatch, RomPatch::AidynPlus }) {
            if (aidyn::version_available(v)) {
                printf("No ROM for %s: playing %s this time\n", version_name(wanted), version_name(v));
                fflush(stdout);
                aidyn::settings::use_rom_patch(v);
                return v;
            }
        }
        return wanted;
    };
    if (!asked && other() != wanted) {
        return aidyn::settings::game().rom_patch;
    }
    if (!interactive) {
        return wanted;
    }
    // Ask with a file picker (RT64's nativefiledialog: the desktop portal in the portable build, GTK otherwise).
    const std::filesystem::path chosen = pick_rom(wanted);
    if (!chosen.empty()) {
        store_rom(chosen);
        recomp::check_all_stored_roms();
    }
    if (aidyn::version_available(wanted)) {
        return wanted;
    }
    if (other() != wanted) {
        return aidyn::settings::game().rom_patch;
    }
    if (wanted == RomPatch::None) {
        fatal("No ROM yet. Put your NTSC-U 1.0 Aidyn Chronicles ROM (.z64) in the ROMs folder next to the game, or "
              "choose it when the game asks (or start it with --rom <path to the ROM>).");
    }
    const std::string name = version_name(wanted);
    fatal("%s", ("No " + name + " ROM yet. Put your 1.0 ROM with " + name + " applied in the ROMs folder next to the "
                 "game, or choose it when the game asks (or start it with --rom <path to it>).").c_str());
}

int main(int argc, char** argv) {
    g_args.assign(argv, argv + argc);
    std::filesystem::path rom_path;
    int version_flag = -1;  // --original 0, --patch 1, --plus 2: this run only (a launcher can start any)
    std::string report_reason;  // --report <why>: make a problem report and stop (src/main/report.cpp)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom_path = argv[++i];
        }
        else if (strcmp(argv[i], "--patch") == 0) {
            version_flag = 1;
        }
        else if (strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            report_reason = argv[++i];
        }
        else if (strcmp(argv[i], "--plus") == 0) {
            version_flag = 2;
        }
        else if (strcmp(argv[i], "--original") == 0) {
            version_flag = 0;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: %s [--rom <ROM>] [--original | --patch | --plus] [--report <why>]\n"
                   "  --rom       your NTSC-U 1.0 Aidyn Chronicles ROM, or that ROM with Aidyn Patch or Aidyn Plus applied\n"
                   "              (by you): checked by its hash and remembered; give each one once\n"
                   "  --original, --patch, --plus   play that version this time (otherwise settings.json game.rom_patch;\n"
                   "              on the title screen X and Y switch to the other two and remember)\n"
                   "  --report    don't play: write a problem report (reports/ in the data folder) giving <why>, and\n"
                   "              print where (the launcher does this when the game ended without closing properly)\n"
                   "  F1: the manual   F2: redrawn art off / on   F11 or Alt+Enter: fullscreen   Shift+Esc: quit\n", argv[0]);
            return 0;
        }
        else {
            fprintf(stderr, "Unknown argument: %s (try --help)\n", argv[i]);
            return 2;
        }
    }

    std::filesystem::create_directories(data_folder());
    aidyn::platform::start_log(data_folder() / "logs", g_args);
    recomp::register_config_path(data_folder());
    // settings.json: AIDYN_SETTINGS_DIR points at another folder (tests).
    const char* settings_dir = std::getenv("AIDYN_SETTINGS_DIR");
    aidyn::settings::load(settings_dir != nullptr && *settings_dir != '\0' ? std::filesystem::path(settings_dir) : data_folder());

    if (version_flag >= 0) {
        aidyn::settings::use_rom_patch(version_flag == 1   ? aidyn::settings::RomPatch::AidynPatch
                                       : version_flag == 2 ? aidyn::settings::RomPatch::AidynPlus
                                                           : aidyn::settings::RomPatch::None);
    }
    recomp::GameEntry entry{
        .rom_hash = 0x79127D0E9CAF38BCULL, // XXH3-64 of the big-endian NTSC-U 1.0 ROM (SHA-1 47ae795a...)
        .internal_name = "AIDYN_CHRONICLES",
        .display_name = "Aidyn Chronicles: The First Mage",
        .game_id = game_id,
        .mod_game_id = "aidyn",
        .save_type = recomp::SaveType::None, // saves are Controller Pak only (Phase 6)
        .is_enabled = true,
        .has_compressed_code = false,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
    };
    recomp::register_game(entry);
    recomp::GameEntry patched = entry;
    patched.rom_hash = 0xAEE228C7FB42F968ULL; // XXH3-64 of Aidyn Patch applied to the 1.0 ROM (SHA-1 1f95ad78...)
    patched.display_name = "Aidyn Chronicles: The First Mage (Aidyn Patch)";
    patched.game_id = game_id_aidyn_patch;
    recomp::register_game(patched);
    recomp::GameEntry plus = entry;
    plus.rom_hash = 0xA72109E848AFF75EULL; // XXH3-64 of Aidyn Plus (AidynPlus.bps) applied to the 1.0 ROM (SHA-1 d57f9459...)
    plus.display_name = "Aidyn Chronicles: The First Mage (Aidyn Plus)";
    plus.game_id = game_id_aidyn_plus;
    recomp::register_game(plus);
    aidyn_register_overlays();

    if (!rom_path.empty()) {
        store_rom(rom_path);
    }
    recomp::check_all_stored_roms();
    store_roms_from_folders();
    const aidyn::settings::RomPatch version = choose_version(version_flag >= 0, report_reason.empty());
    const char8_t* chosen_id = game_id_for(version);

    // The Controller Pak in port 1. AIDYN_MPK points it elsewhere (an emulator's .mpk, or a scratch copy for tests).
    const char* mpk = std::getenv("AIDYN_MPK");
    const std::filesystem::path pak = mpk != nullptr && *mpk != '\0' ? std::filesystem::path(mpk)
                                      : data_folder() / (version == aidyn::settings::RomPatch::AidynPatch  ? "aidyn-patch.mpk"
                                                         : version == aidyn::settings::RomPatch::AidynPlus ? "aidyn-plus.mpk"
                                                                                                           : "aidyn.mpk");
    aidyn_pak_set_path(pak);
    aidyn_save_pictures_set_dir(pak.parent_path() / "save-pictures");
    aidyn_quick_saves_set_pak(pak);  // quick and auto saves: beside it, named after it
    aidyn::report::set_paths(data_folder(), pak);
    aidyn::report::install_crash_handler();
    aidyn::watch::install_crash_signals();
    // A session that didn't close properly last time gets its report now (src/main/watch.cpp); the --report run (the
    // launcher's, after a crash) clears the marker, its own report covering it.
    aidyn::watch::session_start(data_folder(), !report_reason.empty());
    std::atexit(aidyn::watch::session_end);
    if (!report_reason.empty()) {
        const std::filesystem::path made = aidyn::report::make(report_reason);
        if (made.empty()) {
            return 1;
        }
        printf("%s\n", made.string().c_str());
        return 0;
    }

    printf("Game: %s\n", version == aidyn::settings::RomPatch::None ? "Aidyn Chronicles (NTSC-U 1.0)" : version_name(version));
    fflush(stdout);

    // The device at the game's own rate (Aidyn's audio thread runs at 44.1 kHz): SDL then has nothing to resample, and
    // the system's mixer (PipeWire, WASAPI) converts to the hardware's rate with its own, better resampler.
    aidyn::audio::init(44100);

    ultramodern::renderer::GraphicsConfig graphics{};
    graphics.developer_mode = false;
    graphics.res_option = ultramodern::renderer::Resolution::Auto;
    // AIDYN_WINDOWED=1 (tools/dev/pad.sh's test runs): a window whatever settings.json says.
    const bool fullscreen = aidyn::settings::graphics().fullscreen && std::getenv("AIDYN_WINDOWED") == nullptr;
    graphics.wm_option = fullscreen ? ultramodern::renderer::WindowMode::Fullscreen : ultramodern::renderer::WindowMode::Windowed;
    graphics.hr_option = ultramodern::renderer::HUDRatioMode::Full; // extended-GBI origins span the window (clouds)
#ifdef _WIN32
    // Vulkan on Windows too (RT64 would pick D3D12): the save pictures' full-resolution copy (tools/patches) is Vulkan's.
    graphics.api_option = ultramodern::renderer::GraphicsApi::Vulkan;
#else
    graphics.api_option = ultramodern::renderer::GraphicsApi::Auto;
#endif
    graphics.ar_option = ultramodern::renderer::AspectRatio::Original;
    switch (aidyn::settings::graphics().msaa) {
        case 2: graphics.msaa_option = ultramodern::renderer::Antialiasing::MSAA2X; break;
        case 4: graphics.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X; break;
        case 8: graphics.msaa_option = ultramodern::renderer::Antialiasing::MSAA8X; break;
        default: graphics.msaa_option = ultramodern::renderer::Antialiasing::None; break;
    }
    // Above the game's 60 frames a second RT64 draws in-between frames, matching the camera and each object's matrices
    // between two game frames and interpolating them. fps_limit 0 = as fast as the display refreshes.
    const int fps_limit = aidyn::settings::graphics().fps_limit;
    graphics.rr_option = fps_limit == 0 ? ultramodern::renderer::RefreshRate::Display : ultramodern::renderer::RefreshRate::Manual;
    graphics.hpfb_option = ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    graphics.rr_manual_value = fps_limit == 0 ? 60 : fps_limit;
    graphics.ds_option = 1;
    ultramodern::renderer::set_graphics_config(graphics);

    recomp::Configuration cfg;
    cfg.project_version = recomp::Version(0, 1, 0, "-dev");
    cfg.window_handle = {}; // not default-initialised by Configuration: garbage here is used as the window
    cfg.rsp_callbacks = { .get_rsp_microcode = get_rsp_microcode };
    cfg.renderer_callbacks = { .create_render_context = aidyn::create_render_context };
    cfg.audio_callbacks = { .queue_samples = aidyn::audio::queue_samples, .get_frames_remaining = aidyn::audio::get_frames_remaining, .set_frequency = aidyn::audio::set_frequency };
    cfg.input_callbacks = { .poll_input = aidyn::input::poll, .get_input = aidyn::input::get, .set_rumble = aidyn::input::set_rumble, .get_connected_device_info = get_connected_device_info };
    cfg.gfx_callbacks = { .create_gfx = create_gfx, .create_window = create_window, .update_gfx = update_gfx };
    cfg.events_callbacks = { .vi_callback = nullptr, .gfx_init_callback = nullptr };
    cfg.error_handling_callbacks = { .message_box = message_box };
    cfg.threads_callbacks = { .get_game_thread_name = nullptr };
    cfg.message_queue_control.requeue_pi = true;
    cfg.message_queue_control.requeue_ai = true;
    cfg.message_queue_control.requeue_sp = true;
    cfg.message_queue_control.requeue_dp = true;
    cfg.message_queue_control.requeue_timer = true;

    // Start the game right away; the runtime loads the stored ROM when its game thread sees this.
    recomp::start_game(chosen_id, "");
    aidyn::watch::start_watchdog();
    recomp::start(cfg);
    aidyn::watch::session_end();
    return EXIT_SUCCESS;
}
