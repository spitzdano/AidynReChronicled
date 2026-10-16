#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "json/json.hpp"

namespace {
    aidyn::settings::Graphics g_graphics;
    aidyn::settings::Game g_game;

    const char* rom_patch_name(aidyn::settings::RomPatch p) {
        return p == aidyn::settings::RomPatch::AidynPatch ? "aidyn-patch" : "none";
    }
    std::filesystem::path g_folder;

    [[noreturn]] void bad(const std::filesystem::path& path, const std::string& why) {
        fprintf(stderr, "%s: %s\n", path.c_str(), why.c_str());
        std::exit(EXIT_FAILURE);
    }

    // "W:H" -> W / H, or 0 when it isn't two positive numbers.
    double parse_ratio(const std::string& text) {
        const size_t colon = text.find(':');
        if (colon == std::string::npos) {
            return 0.0;
        }
        try {
            size_t used_w = 0, used_h = 0;
            const double w = std::stod(text.substr(0, colon), &used_w);
            const double h = std::stod(text.substr(colon + 1), &used_h);
            if (used_w != colon || used_h != text.size() - colon - 1 || !(w > 0.0) || !(h > 0.0)) {
                return 0.0;
            }
            return w / h;
        }
        catch (const std::exception&) {
            return 0.0;
        }
    }
}

void aidyn::settings::load(const std::filesystem::path& folder) {
    g_folder = folder;
    const std::filesystem::path path = folder / "settings.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        nlohmann::ordered_json j;
        j["game"]["rom_patch"] = rom_patch_name(g_game.rom_patch);
        j["game"]["skip_intro"] = g_game.skip_intro;
        j["graphics"]["render_height"] = g_graphics.render_height;
        j["graphics"]["aspect"] = g_graphics.aspect_text;
        j["graphics"]["msaa"] = g_graphics.msaa;
        j["graphics"]["supersampling"] = g_graphics.supersampling;
        j["graphics"]["fps_limit"] = g_graphics.fps_limit;
        j["graphics"]["draw_distance"] = g_graphics.draw_distance;
        j["graphics"]["fullscreen"] = g_graphics.fullscreen;
        j["graphics"]["menus_4_3"] = g_graphics.menus_4_3;
        j["graphics"]["right_stick_camera"] = g_graphics.right_stick_camera;
        j["graphics"]["overscan_borders"] = g_graphics.overscan_borders;
        j["graphics"]["smooth_animation_loops"] = g_graphics.smooth_animation_loops;
        j["graphics"]["texture_packs"] = g_graphics.texture_packs;
        std::ofstream out(path);
        out << j.dump(4) << "\n";
        if (!out) {
            bad(path, "cannot write the default settings");
        }
        return;
    }

    nlohmann::json j;
    try {
        std::ifstream in(path);
        j = nlohmann::json::parse(in);
        const auto& game = j.value("game", nlohmann::json::object());
        const std::string patch = game.value("rom_patch", std::string(rom_patch_name(g_game.rom_patch)));
        if (patch == "none") g_game.rom_patch = RomPatch::None;
        else if (patch == "aidyn-patch") g_game.rom_patch = RomPatch::AidynPatch;
        else bad(path, "game.rom_patch must be \"none\" or \"aidyn-patch\", not \"" + patch + "\"");
        g_game.skip_intro = game.value("skip_intro", g_game.skip_intro);
        const auto& gfx = j.value("graphics", nlohmann::json::object());
        g_graphics.render_height = gfx.value("render_height", g_graphics.render_height);
        g_graphics.msaa = gfx.value("msaa", g_graphics.msaa);
        g_graphics.supersampling = gfx.value("supersampling", g_graphics.supersampling);
        g_graphics.fps_limit = gfx.value("fps_limit", g_graphics.fps_limit);
        g_graphics.draw_distance = gfx.value("draw_distance", g_graphics.draw_distance);
        g_graphics.fullscreen = gfx.value("fullscreen", g_graphics.fullscreen);
        g_graphics.menus_4_3 = gfx.value("menus_4_3", g_graphics.menus_4_3);
        g_graphics.right_stick_camera = gfx.value("right_stick_camera", g_graphics.right_stick_camera);
        g_graphics.overscan_borders = gfx.value("overscan_borders", g_graphics.overscan_borders);
        g_graphics.smooth_animation_loops = gfx.value("smooth_animation_loops", g_graphics.smooth_animation_loops);
        g_graphics.texture_packs = gfx.value("texture_packs", g_graphics.texture_packs);
        const std::string aspect = gfx.value("aspect", g_graphics.aspect_text);
        g_graphics.aspect_text = aspect;
        if (aspect == "4:3") {
            g_graphics.aspect = Aspect::Original;
        }
        else if (aspect == "window") {
            g_graphics.aspect = Aspect::Window;
        }
        else {
            const double ratio = parse_ratio(aspect);
            if (ratio < 4.0 / 3.0 || ratio > 4.0) {
                bad(path, "graphics.aspect must be \"4:3\", \"window\" or a width:height from 4:3 to 4:1 (\"16:9\", "
                          "\"21:9\", \"43:18\"...), not \"" + aspect + "\"");
            }
            g_graphics.aspect = Aspect::Ratio;
            g_graphics.aspect_ratio = ratio;
        }
    }
    catch (const nlohmann::json::exception& e) {
        bad(path, e.what());
    }
    if (g_graphics.render_height != 0 && (g_graphics.render_height < 240 || g_graphics.render_height > 4320)) {
        bad(path, "graphics.render_height must be 0 (fit the window) or 240..4320");
    }
    if (g_graphics.msaa != 0 && g_graphics.msaa != 2 && g_graphics.msaa != 4 && g_graphics.msaa != 8) {
        bad(path, "graphics.msaa must be 0, 2, 4 or 8");
    }
    if (g_graphics.fps_limit != 0 && (g_graphics.fps_limit < 30 || g_graphics.fps_limit > 1000)) {
        bad(path, "graphics.fps_limit must be 0 (the display's rate) or 30..1000");
    }
    if (g_graphics.supersampling < 1 || g_graphics.supersampling > 4) {
        bad(path, "graphics.supersampling must be 1 (off), 2, 3 or 4");
    }
    if (!(g_graphics.draw_distance >= 1.0f && g_graphics.draw_distance <= 3.0f)) {
        bad(path, "graphics.draw_distance must be 1 (original) to 3");
    }
}

const std::filesystem::path& aidyn::settings::folder() {
    return g_folder;
}

const aidyn::settings::Graphics& aidyn::settings::graphics() {
    return g_graphics;
}

const aidyn::settings::Game& aidyn::settings::game() {
    return g_game;
}

void aidyn::settings::use_rom_patch(RomPatch patch) {
    g_game.rom_patch = patch;
}

void aidyn::settings::save_rom_patch(RomPatch patch) {
    g_game.rom_patch = patch;
    const std::filesystem::path path = g_folder / "settings.json";
    nlohmann::ordered_json j = nlohmann::ordered_json::object();
    try {
        std::ifstream in(path);
        if (in) {
            j = nlohmann::ordered_json::parse(in);
        }
    }
    catch (const nlohmann::json::exception& e) {
        fprintf(stderr, "%s: %s (not updated)\n", path.c_str(), e.what());
        return;
    }
    j["game"]["rom_patch"] = rom_patch_name(patch);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp);
        out << j.dump(4) << "\n";
        if (!out) {
            fprintf(stderr, "%s: cannot write (not updated)\n", tmp.c_str());
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        fprintf(stderr, "%s: %s (not updated)\n", path.c_str(), ec.message().c_str());
    }
}
