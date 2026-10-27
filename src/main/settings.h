// Player settings, read from settings.json in the data folder (written with defaults on first run).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aidyn::settings {
    enum class Aspect {
        Original,   // "4:3": black bars at the sides
        Ratio,      // "W:H" wider than 4:3 ("16:9", "21:9", ...): the 3D view is widened, menus and HUD stay 4:3 in the middle
        Window,     // "window": as wide as the window
    };

    enum class RomPatch {
        None,        // "none": the NTSC-U 1.0 ROM as released
        AidynPatch,  // "aidyn-patch": the community Aidyn Patch (u/halibabica), applied to a 1.0 ROM by the player
        AidynPlus,   // "aidyn-plus": Aidyn Plus (u/halibabica, a bigger rebalance), applied to a 1.0 ROM by the player
    };

    struct Game {
        RomPatch rom_patch = RomPatch::None;
        bool skip_intro = true;      // start at the title screen, without the Nintendo, THQ and H2O logos
        int fast_forward_speed = 3;  // L3 (keyboard: hold Tab) toggles fast-forward: the game runs this many times faster, 2..6
        float combat_enemy_speed = 2.0f;  // enemy turns in combat run this many times faster on their own; 1 = off, up to 6
                                          // (the ReChronicled page's Enemy turns)
        float combat_party_speed = 1.0f;  // the party's turns in combat while they move and act (not while choosing):
                                          // 1 = as the game, up to 6 (the ReChronicled page's Combat speed)
        int save_backups = 20;       // copies of the Controller Pak kept in <data>/backups (before saving, and at start); 0 = none
    };

    // The sound as the player hears it (src/main/audio.cpp). The game's own mix is very soft (-35 to -39 LUFS; most
    // games sit around -16 to -23) and dull (its instrument samples are low-rate): "enhanced" plays it louder through a
    // limiter, with a gentle lift of the treble. Nothing in the music or sounds themselves changes.
    struct Audio {
        bool enhanced = true;        // the pause menu's ReChronicled page: Sound, Enhanced / Original
        float loudness_db = 18.0f;   // enhanced: the gain, 0..24 dB (peaks held at -1 dBFS by the limiter)
        float brightness_db = 6.0f;  // enhanced: the treble lift above about 3 kHz, 0..12 dB
        bool restored_samples = true; // the music's instrument samples restored (the rechronicled-samples pack, when
                                      // there): the ReChronicled page's Instruments, Restored / Original
    };

    struct Graphics {
        int render_height = 1080;    // internal resolution in lines (240 = N64); 0 = largest integer scale that fits the window
        Aspect aspect = Aspect::Ratio;   // in effect: Original while widescreen is off
        bool widescreen = true;          // false: 4:3, the N64's picture, whatever "aspect" says (the Display page)
        std::string aspect_text = "16:9"; // as written in settings.json
        double aspect_ratio = 16.0 / 9.0; // width / height, for Aspect::Ratio
        int msaa = 4;                // 0, 2, 4 or 8 (capped to what the GPU supports)
        float draw_distance = 3.0f; // field view: multiplies the far clip (and the fog with it); 1 = original, up to 3
        float walk_speed = 1.5f;    // field: Alaron's walk and run cycles (and so his pace) this many times faster; 1 = original, up to 2
        int supersampling = 2;       // 1 (off), 2, 3 or 4: render that many times render_height and scale down (smoother edges)
        int fps_limit = 240;         // frames shown a second (RT64 interpolates between the game's 60), never above the display's rate; 0 = the display's rate
        bool fullscreen = false;
        bool menus_4_3 = true;       // with 16:9/window: show the pause menu, inventory and shops in 4:3 (their layouts break widened)
        bool right_stick_camera = true; // walking: right stick turns the camera (x) and raises/lowers it (y)
        bool camera_invert_x = false; // right stick left/right turns the camera the other way
        bool camera_invert_y = true;  // right stick up/down tilts the camera the other way (up: look down)
        bool overscan_borders = false; // the black frame the game draws for CRT overscan (Graphics::EndList)
        bool hud_at_edges = true;    // widescreen: the time-of-day dial at the left edge, the compass at the right (portraits stay centred)
        bool smooth_animation_loops = true; // no repeated pose and no stop in the stride when a walk/run cycle loops
        bool controls_card = true;   // the card of button prompts in the field and in combat (View shows / hides it)
        bool button_prompts = true;  // the port's other prompts: menu hints (Equip, Lists, Take...), LB / RB at page arrows, party switch
        bool turn_order = true;      // combat: the round's turn order across the top of the screen, then the next round's
        bool combat_log = true;      // combat: the log of the fight's messages at the bottom left
        bool fps_counter = false;    // the frames shown a second, at the top left (the Display page)
        bool textures = true;        // every custom texture: the packs below, HD save pictures, redrawn page words, the HD letters' spacing (false: as the N64)
        bool vsync = true;           // wait for the display's refresh (no tearing); false: show each frame as soon as it's drawn (G-Sync / FreeSync)
        std::vector<std::string> texture_packs{ "hd-ui", "hd-world", "rechronicled-text", "rechronicled-art", "rechronicled" }; // RT64 packs in <data>/texture-packs (folders or .rtz), later ones win
    };

    // Loads (or creates) <folder>/settings.json. A file that can't be parsed stops the program with the reason,
    // rather than silently using defaults (C9).
    void load(const std::filesystem::path& folder);
    const std::filesystem::path& folder();
    const Graphics& graphics();
    const Game& game();
    const Audio& audio();
    void use_rom_patch(RomPatch patch);   // for this run only (--patch / --original)
    void save_rom_patch(RomPatch patch);  // into settings.json (the title screen's version switch), keeping the rest
    void save_controls_card(bool shown);  // View in the field or combat: remembered in settings.json
    // The pause menu's ReChronicled page (src/game/patches.cpp) changes settings while the game runs: change the field
    // through these, then save it (written into settings.json at once, keeping the rest of the file).
    Graphics& graphics_rw();
    Game& game_rw();
    Audio& audio_rw();
    void save_bool(const char* section, const char* key, bool value);
    void save_number(const char* section, const char* key, double value);
    // Replaces a whole section of settings.json with JSON text (the Controls page's bindings: src/main/controls.cpp).
    void save_section(const char* section, const std::string& json_text);
    // The Display page's Widescreen switch: graphics().aspect again from "aspect" and widescreen (turning it on with
    // "aspect": "4:3" makes that "16:9", saved). Saving widescreen itself is the caller's.
    void apply_widescreen();
}
