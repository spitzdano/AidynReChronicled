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
        AidynPlus,   // "aidyn-plus": the Aidyn Plus romhack (a bigger rebalance), applied to a 1.0 ROM by the player
    };

    struct Game {
        RomPatch rom_patch = RomPatch::None;
        bool skip_intro = true;      // start at the title screen, without the Nintendo, THQ and H2O logos
        int fast_forward_speed = 3;  // L3 (keyboard: hold Tab) toggles fast-forward: the game runs this many times faster, 2..6
        int combat_enemy_speed = 2;  // enemy turns in combat run this many times faster on their own; 1 = off, up to 6
        int save_backups = 20;       // copies of the Controller Pak kept in <data>/backups (before saving, and at start); 0 = none
    };

    struct Graphics {
        int render_height = 1080;    // internal resolution in lines (240 = N64); 0 = largest integer scale that fits the window
        Aspect aspect = Aspect::Ratio;
        std::string aspect_text = "16:9"; // as written in settings.json
        double aspect_ratio = 16.0 / 9.0; // width / height, for Aspect::Ratio
        int msaa = 4;                // 0, 2, 4 or 8 (capped to what the GPU supports)
        float draw_distance = 2.0f; // field view: multiplies the far clip (and the fog with it); 1 = original, up to 3
        int supersampling = 2;       // 1 (off), 2, 3 or 4: render that many times render_height and scale down (smoother edges)
        int fps_limit = 120;         // frames shown a second (RT64 interpolates between the game's 60); 0 = the display's rate
        bool fullscreen = false;
        bool menus_4_3 = true;       // with 16:9/window: show the pause menu, inventory and shops in 4:3 (their layouts break widened)
        bool right_stick_camera = true; // walking: right stick turns the camera (x) and raises/lowers it (y)
        bool camera_invert_x = false; // right stick left/right turns the camera the other way
        bool camera_invert_y = false; // right stick up/down tilts the camera the other way (up: look down)
        bool overscan_borders = false; // the black frame the game draws for CRT overscan (Graphics::EndList)
        bool hud_at_edges = true;    // widescreen: the time-of-day dial at the left edge, the compass at the right (portraits stay centred)
        bool smooth_animation_loops = true; // no repeated pose and no stop in the stride when a walk/run cycle loops
        bool controls_card = true;   // the card of button prompts in the field and in combat (View shows / hides it)
        bool turn_order = true;      // combat: the round's turn order across the top of the screen, then the next round's
        std::vector<std::string> texture_packs{ "hd-ui", "hd-world", "rechronicled-text", "rechronicled-art", "rechronicled" }; // RT64 packs in <data>/texture-packs (folders or .rtz), later ones win
    };

    // Loads (or creates) <folder>/settings.json. A file that can't be parsed stops the program with the reason,
    // rather than silently using defaults (C9).
    void load(const std::filesystem::path& folder);
    const std::filesystem::path& folder();
    const Graphics& graphics();
    const Game& game();
    void use_rom_patch(RomPatch patch);   // for this run only (--patch / --original)
    void save_rom_patch(RomPatch patch);  // into settings.json (the title screen's version switch), keeping the rest
    void save_controls_card(bool shown);  // View in the field or combat: remembered in settings.json
}
