// Aidyn ReChronicled (Aidyn Chronicles: The First Mage, recompiled for PC): runtime shell.
//
// Modelled on Zelda64Recomp's main.cpp, stripped to what the runtime needs to boot a game:
// an SDL window for RT64, SDL audio, SDL input, and the recompiled audio microcode. No launcher
// UI, no mods, no patches. The player's own ROM is given with --rom (or remembered from the
// last run, since librecomp stores a copy of a validated ROM in the config folder).
#include "settings.h"
#include "version.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"

#define SDL_MAIN_HANDLED
#include "SDL2/SDL.h"

#include "rt64_context.h"
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
    return {};
}

static ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    // The game renders 4:3 at 320x240 (high-res 640x480 with the Expansion Pak).
    window = SDL_CreateWindow("Aidyn ReChronicled", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
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
    return ultramodern::renderer::WindowHandle{ window };
}

static std::filesystem::path data_folder();

// F1: the manual (tools/manual/build_manual.py writes it into the data folder) in the default browser. Fullscreen is
// left first, so the browser's window isn't hidden behind the game.
static void open_manual() {
    const std::filesystem::path page = data_folder() / "manual" / "index.html";
    std::error_code ec;
    if (!std::filesystem::exists(page, ec)) {
        fprintf(stderr, "No manual in %s yet: build it with tools/manual/build_manual.py\n", page.parent_path().c_str());
        return;
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(window, 0);
    }
    const std::string url = "file://" + page.string();
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
                bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
                SDL_SetWindowFullscreen(window, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
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
    }
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
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base = (xdg != nullptr && *xdg != '\0') ? std::filesystem::path(xdg) : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".local" / "share";
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
            printf("Moved %s to %s\n", old.c_str(), folder.c_str());
        }
    }
    return folder;
}

void aidyn_pak_set_path(const std::filesystem::path& path); // src/game/pak.cpp

// The two ROMs the port runs. Aidyn Patch (u/halibabica's community patch, applied by the player to their own 1.0 ROM)
// changes only data (potion recipes and prices, skill and weapon XP rates, the item/spell/enemy database), no code, so
// the same recompiled code runs both. settings.json "game.rom_patch" picks one; each keeps its own Controller Pak.
static const char8_t game_id[] = u8"aidyn.n64.us.1.0";
static const char8_t game_id_aidyn_patch[] = u8"aidyn.n64.us.1.0.aidyn-patch";

// The command line, for restarting into the other version (aidyn::switch_version).
static std::vector<std::string> g_args;

// The title screen's version switch (src/game/patches.cpp): the other version's ROM must be stored.
bool aidyn::version_available(bool aidyn_patch) {
    return recomp::is_rom_valid(aidyn_patch ? game_id_aidyn_patch : game_id);
}

// Saves the choice in settings.json and starts this program again in its place, with the same arguments but the
// version flag replaced. Game data comes from the ROM at start-up, so a version is only changed by a restart; the
// Controller Pak is written through at once, so nothing is lost.
void aidyn::switch_version(bool aidyn_patch) {
    aidyn::settings::save_rom_patch(aidyn_patch ? aidyn::settings::RomPatch::AidynPatch : aidyn::settings::RomPatch::None);
    std::vector<std::string> args;
    for (size_t i = 0; i < g_args.size(); i++) {
        if (g_args[i] == "--patch" || g_args[i] == "--original") {
            continue;
        }
        if (g_args[i] == "--rom" && i + 1 < g_args.size()) {
            i++;  // stored after the first run
            continue;
        }
        args.push_back(g_args[i]);
    }
    args.push_back(aidyn_patch ? "--patch" : "--original");
    std::vector<char*> argv;
    for (std::string& a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    printf("Switching to %s\n", aidyn_patch ? "Aidyn Patch" : "the original");
    fflush(stdout);
    execv("/proc/self/exe", argv.data());
    fprintf(stderr, "Could not restart: %s\n", strerror(errno));
}

// Checks a ROM the player gave and stores it in the data folder (librecomp keeps a copy): the 1.0 release or Aidyn
// Patch, whichever it is. Stops with the reason when it is neither.
static void store_rom(const std::filesystem::path& rom_path) {
    recomp::RomValidationError result = recomp::select_rom(rom_path, game_id);
    const char* stored = "the NTSC-U 1.0 ROM";
    if (result == recomp::RomValidationError::IncorrectVersion || result == recomp::RomValidationError::OtherError) {
        if (recomp::select_rom(rom_path, game_id_aidyn_patch) == recomp::RomValidationError::Good) {
            result = recomp::RomValidationError::Good;
            stored = "the Aidyn Patch ROM";
        }
    }
    switch (result) {
    case recomp::RomValidationError::Good:
        printf("Stored %s.\n", stored);
        fflush(stdout);
        break;
    case recomp::RomValidationError::FailedToOpen:
        fatal("Could not open ROM: %s", rom_path.c_str());
    case recomp::RomValidationError::NotARom:
        fatal("Not an N64 ROM (a zipped ROM must be unzipped first): %s", rom_path.c_str());
    case recomp::RomValidationError::IncorrectVersion:
        fatal("This is Aidyn Chronicles, but neither the NTSC-U 1.0 release (version byte 0) nor Aidyn Patch applied "
              "to it: %s", rom_path.c_str());
    default:
        fatal("This is not Aidyn Chronicles (NTSC-U 1.0): %s", rom_path.c_str());
    }
}

// The first start: explains what is wanted, then a file picker. Empty when cancelled or when no picker works here.
static std::filesystem::path pick_rom(bool aidyn_patch) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Aidyn ReChronicled",
                             aidyn_patch ? "Choose your Aidyn Chronicles ROM with Aidyn Patch applied (.z64).\n\n"
                                           "It is checked and kept, so this is only asked once."
                                         : "Choose your Aidyn Chronicles ROM: the North American 1.0 cartridge (.z64).\n\n"
                                           "It is checked and kept, so this is only asked once.",
                             nullptr);
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

int main(int argc, char** argv) {
    g_args.assign(argv, argv + argc);
    std::filesystem::path rom_path;
    int version_flag = -1;  // --original 0, --patch 1: this run only (a launcher can start either)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            rom_path = argv[++i];
        }
        else if (strcmp(argv[i], "--patch") == 0) {
            version_flag = 1;
        }
        else if (strcmp(argv[i], "--original") == 0) {
            version_flag = 0;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: %s [--rom <ROM>] [--patch | --original]\n"
                   "  --rom       your NTSC-U 1.0 Aidyn Chronicles ROM, or that ROM with Aidyn Patch applied: checked and\n"
                   "              remembered after the first run\n"
                   "  --patch     play Aidyn Patch this time; --original: play the 1.0 release this time\n"
                   "              (otherwise settings.json game.rom_patch; View on the title screen switches and remembers)\n"
                   "  F1: the manual   F2: redrawn art off / on   F11 or Alt+Enter: fullscreen   Shift+Esc: quit\n", argv[0]);
            return 0;
        }
        else {
            fprintf(stderr, "Unknown argument: %s (try --help)\n", argv[i]);
            return 2;
        }
    }

    std::filesystem::create_directories(data_folder());
    recomp::register_config_path(data_folder());
    // settings.json: AIDYN_SETTINGS_DIR points at another folder (tests).
    const char* settings_dir = std::getenv("AIDYN_SETTINGS_DIR");
    aidyn::settings::load(settings_dir != nullptr && *settings_dir != '\0' ? std::filesystem::path(settings_dir) : data_folder());

    if (version_flag >= 0) {
        aidyn::settings::use_rom_patch(version_flag ? aidyn::settings::RomPatch::AidynPatch : aidyn::settings::RomPatch::None);
    }
    const bool aidyn_patch = aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch;
    const char8_t* chosen_id = aidyn_patch ? game_id_aidyn_patch : game_id;

    // The Controller Pak in port 1. AIDYN_MPK points it elsewhere (an emulator's .mpk, or a scratch copy for tests).
    const char* mpk = std::getenv("AIDYN_MPK");
    aidyn_pak_set_path(mpk != nullptr && *mpk != '\0' ? std::filesystem::path(mpk)
                       : data_folder() / (aidyn_patch ? "aidyn-patch.mpk" : "aidyn.mpk"));

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
    aidyn_register_overlays();

    if (!rom_path.empty()) {
        store_rom(rom_path);
    }
    recomp::check_all_stored_roms();
    if (!recomp::is_rom_valid(chosen_id) && rom_path.empty()) {
        // First start without --rom: ask for the ROM with a file picker (RT64's nativefiledialog: the desktop portal in
        // the portable build, GTK otherwise).
        rom_path = pick_rom(aidyn_patch);
        if (!rom_path.empty()) {
            store_rom(rom_path);
            recomp::check_all_stored_roms();
        }
    }
    if (!recomp::is_rom_valid(chosen_id)) {
        if (aidyn_patch) {
            fatal("settings.json picks Aidyn Patch (game.rom_patch), but no Aidyn Patch ROM is stored yet. Start the "
                  "game with --rom <your 1.0 ROM with Aidyn Patch applied>, or set game.rom_patch to \"none\".");
        }
        fatal("No ROM yet. Start the game again and choose your NTSC-U 1.0 Aidyn Chronicles ROM (.z64), or start it "
              "with --rom <path to the ROM>.");
    }
    printf("Game: %s\n", aidyn_patch ? "Aidyn Patch" : "Aidyn Chronicles (NTSC-U 1.0)");
    fflush(stdout);

    aidyn::audio::init(48000);

    ultramodern::renderer::GraphicsConfig graphics{};
    graphics.developer_mode = false;
    graphics.res_option = ultramodern::renderer::Resolution::Auto;
    // AIDYN_WINDOWED=1 (tools/dev/pad.sh's test runs): a window whatever settings.json says.
    const bool fullscreen = aidyn::settings::graphics().fullscreen && std::getenv("AIDYN_WINDOWED") == nullptr;
    graphics.wm_option = fullscreen ? ultramodern::renderer::WindowMode::Fullscreen : ultramodern::renderer::WindowMode::Windowed;
    graphics.hr_option = ultramodern::renderer::HUDRatioMode::Full; // extended-GBI origins span the window (clouds)
    graphics.api_option = ultramodern::renderer::GraphicsApi::Auto;
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
    recomp::start(cfg);
    return EXIT_SUCCESS;
}
