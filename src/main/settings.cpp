#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <algorithm>

#include "json/json.hpp"

namespace {
    aidyn::settings::Graphics g_graphics;
    aidyn::settings::Game g_game;
    aidyn::settings::Audio g_audio;

    const char* rom_patch_name(aidyn::settings::RomPatch p) {
        return p == aidyn::settings::RomPatch::AidynPatch ? "aidyn-patch"
               : p == aidyn::settings::RomPatch::AidynPlus ? "aidyn-plus"
                                                           : "none";
    }
    std::filesystem::path g_folder;

    [[noreturn]] void bad(const std::filesystem::path& path, const std::string& why) {
        fprintf(stderr, "%s: %s\n", path.string().c_str(), why.c_str());
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

namespace {
    // Changes some keys of settings.json in place, keeping the rest as the player wrote it.
    void edit_file(const std::filesystem::path& path, const std::function<void(nlohmann::ordered_json&)>& change) {
        nlohmann::ordered_json j = nlohmann::ordered_json::object();
        try {
            std::ifstream in(path);
            if (in) {
                j = nlohmann::ordered_json::parse(in);
            }
        }
        catch (const nlohmann::json::exception& e) {
            fprintf(stderr, "%s: %s (not updated)\n", path.string().c_str(), e.what());
            return;
        }
        change(j);
        const std::filesystem::path tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp);
            out << j.dump(4) << "\n";
            if (!out) {
                fprintf(stderr, "%s: cannot write (not updated)\n", tmp.string().c_str());
                return;
            }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            fprintf(stderr, "%s: %s (not updated)\n", path.string().c_str(), ec.message().c_str());
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
        j["game"]["fast_forward_speed"] = g_game.fast_forward_speed;
        j["game"]["combat_enemy_speed"] = g_game.combat_enemy_speed;
        j["game"]["combat_party_speed"] = g_game.combat_party_speed;
        j["game"]["save_backups"] = g_game.save_backups;
        j["audio"]["enhanced"] = g_audio.enhanced;
        j["audio"]["loudness_db"] = g_audio.loudness_db;
        j["audio"]["brightness_db"] = g_audio.brightness_db;
        j["audio"]["restored_samples"] = g_audio.restored_samples;
        j["graphics"]["render_height"] = g_graphics.render_height;
        j["graphics"]["aspect"] = g_graphics.aspect_text;
        j["graphics"]["widescreen"] = g_graphics.widescreen;
        j["graphics"]["msaa"] = g_graphics.msaa;
        j["graphics"]["supersampling"] = g_graphics.supersampling;
        j["graphics"]["fps_limit"] = g_graphics.fps_limit;
        j["graphics"]["draw_distance"] = g_graphics.draw_distance;
        j["graphics"]["walk_speed"] = g_graphics.walk_speed;
        j["graphics"]["fullscreen"] = g_graphics.fullscreen;
        j["graphics"]["menus_4_3"] = g_graphics.menus_4_3;
        j["graphics"]["right_stick_camera"] = g_graphics.right_stick_camera;
        j["graphics"]["camera_invert_x"] = g_graphics.camera_invert_x;
        j["graphics"]["camera_invert_y"] = g_graphics.camera_invert_y;
        j["graphics"]["overscan_borders"] = g_graphics.overscan_borders;
        j["graphics"]["smooth_animation_loops"] = g_graphics.smooth_animation_loops;
        j["graphics"]["hud_at_edges"] = g_graphics.hud_at_edges;
        j["graphics"]["controls_card"] = g_graphics.controls_card;
        j["graphics"]["button_prompts"] = g_graphics.button_prompts;
        j["graphics"]["turn_order"] = g_graphics.turn_order;
        j["graphics"]["combat_log"] = g_graphics.combat_log;
        j["graphics"]["fps_counter"] = g_graphics.fps_counter;
        j["graphics"]["textures"] = g_graphics.textures;
        j["graphics"]["vsync"] = g_graphics.vsync;
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
        else if (patch == "aidyn-plus") g_game.rom_patch = RomPatch::AidynPlus;
        else bad(path, "game.rom_patch must be \"none\", \"aidyn-patch\" or \"aidyn-plus\", not \"" + patch + "\"");
        g_game.skip_intro = game.value("skip_intro", g_game.skip_intro);
        g_game.fast_forward_speed = game.value("fast_forward_speed", g_game.fast_forward_speed);
        g_game.combat_enemy_speed = game.value("combat_enemy_speed", g_game.combat_enemy_speed);
        g_game.combat_party_speed = game.value("combat_party_speed", g_game.combat_party_speed);
        g_game.save_backups = game.value("save_backups", g_game.save_backups);
        const auto& audio = j.value("audio", nlohmann::json::object());
        g_audio.enhanced = audio.value("enhanced", g_audio.enhanced);
        g_audio.loudness_db = audio.value("loudness_db", g_audio.loudness_db);
        g_audio.brightness_db = audio.value("brightness_db", g_audio.brightness_db);
        g_audio.restored_samples = audio.value("restored_samples", g_audio.restored_samples);
        const auto& gfx = j.value("graphics", nlohmann::json::object());
        g_graphics.render_height = gfx.value("render_height", g_graphics.render_height);
        g_graphics.msaa = gfx.value("msaa", g_graphics.msaa);
        g_graphics.supersampling = gfx.value("supersampling", g_graphics.supersampling);
        g_graphics.fps_limit = gfx.value("fps_limit", g_graphics.fps_limit);
        g_graphics.draw_distance = gfx.value("draw_distance", g_graphics.draw_distance);
        g_graphics.walk_speed = gfx.value("walk_speed", g_graphics.walk_speed);
        g_graphics.fullscreen = gfx.value("fullscreen", g_graphics.fullscreen);
        g_graphics.menus_4_3 = gfx.value("menus_4_3", g_graphics.menus_4_3);
        g_graphics.right_stick_camera = gfx.value("right_stick_camera", g_graphics.right_stick_camera);
        g_graphics.camera_invert_x = gfx.value("camera_invert_x", g_graphics.camera_invert_x);
        g_graphics.camera_invert_y = gfx.value("camera_invert_y", g_graphics.camera_invert_y);
        g_graphics.overscan_borders = gfx.value("overscan_borders", g_graphics.overscan_borders);
        g_graphics.smooth_animation_loops = gfx.value("smooth_animation_loops", g_graphics.smooth_animation_loops);
        g_graphics.hud_at_edges = gfx.value("hud_at_edges", g_graphics.hud_at_edges);
        g_graphics.controls_card = gfx.value("controls_card", g_graphics.controls_card);
        g_graphics.button_prompts = gfx.value("button_prompts", g_graphics.button_prompts);
        g_graphics.turn_order = gfx.value("turn_order", g_graphics.turn_order);
        g_graphics.combat_log = gfx.value("combat_log", g_graphics.combat_log);
        g_graphics.fps_counter = gfx.value("fps_counter", g_graphics.fps_counter);
        g_graphics.textures = gfx.value("textures", g_graphics.textures);
        g_graphics.vsync = gfx.value("vsync", g_graphics.vsync);
        g_graphics.texture_packs = gfx.value("texture_packs", g_graphics.texture_packs);
        // rechronicled-art (the redrawn portraits and icons) came after the first settings files were written: put it
        // in once, before "rechronicled" (later packs win).
        auto& packs = g_graphics.texture_packs;
        if (gfx.contains("texture_packs") && std::find(packs.begin(), packs.end(), "rechronicled-art") == packs.end()
            && !game.value("art_pack_added", false)) {
            packs.insert(std::find(packs.begin(), packs.end(), "rechronicled"), "rechronicled-art");
            edit_file(path, [&packs](nlohmann::ordered_json& f) {
                f["graphics"]["texture_packs"] = packs;
                f["game"]["art_pack_added"] = true;
            });
        }
        // rechronicled-text (the HD letters, shipped with releases so they match the game's letter widths) came later
        // still: in once, after hd-ui (its letters win over that pack's).
        if (gfx.contains("texture_packs") && std::find(packs.begin(), packs.end(), "rechronicled-text") == packs.end()
            && !game.value("text_pack_added", false)) {
            auto at = std::find(packs.begin(), packs.end(), "rechronicled-art");
            if (at == packs.end()) {
                at = std::find(packs.begin(), packs.end(), "rechronicled");
            }
            packs.insert(at, "rechronicled-text");
            edit_file(path, [&packs](nlohmann::ordered_json& f) {
                f["graphics"]["texture_packs"] = packs;
                f["game"]["text_pack_added"] = true;
            });
        }
        g_graphics.widescreen = gfx.value("widescreen", g_graphics.widescreen);
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
        if (!g_graphics.widescreen) {
            g_graphics.aspect = Aspect::Original;
        }
    }
    catch (const nlohmann::json::exception& e) {
        bad(path, e.what());
    }
    if (g_game.fast_forward_speed < 2 || g_game.fast_forward_speed > 6) {
        bad(path, "game.fast_forward_speed must be 2..6");
    }
    if (g_game.combat_party_speed < 1 || g_game.combat_party_speed > 6) {
        bad(path, "game.combat_party_speed must be 1 (as the game) to 6");
    }
    if (g_game.combat_enemy_speed < 1 || g_game.combat_enemy_speed > 6) {
        bad(path, "game.combat_enemy_speed must be 1 (off) to 6");
    }
    if (g_game.save_backups < 0 || g_game.save_backups > 1000) {
        bad(path, "game.save_backups must be 0 (none) to 1000");
    }
    if (!(g_audio.loudness_db >= 0.0f && g_audio.loudness_db <= 24.0f)) {
        bad(path, "audio.loudness_db must be 0..24");
    }
    if (!(g_audio.brightness_db >= 0.0f && g_audio.brightness_db <= 12.0f)) {
        bad(path, "audio.brightness_db must be 0..12");
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
    if (!(g_graphics.walk_speed >= 1.0f && g_graphics.walk_speed <= 2.0f)) {
        bad(path, "graphics.walk_speed must be 1 (original) to 2");
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

const aidyn::settings::Audio& aidyn::settings::audio() {
    return g_audio;
}

aidyn::settings::Audio& aidyn::settings::audio_rw() {
    return g_audio;
}

void aidyn::settings::use_rom_patch(RomPatch patch) {
    g_game.rom_patch = patch;
}

void aidyn::settings::save_rom_patch(RomPatch patch) {
    g_game.rom_patch = patch;
    edit_file(g_folder / "settings.json", [patch](nlohmann::ordered_json& j) { j["game"]["rom_patch"] = rom_patch_name(patch); });
}

aidyn::settings::Graphics& aidyn::settings::graphics_rw() {
    return g_graphics;
}

aidyn::settings::Game& aidyn::settings::game_rw() {
    return g_game;
}

void aidyn::settings::save_bool(const char* section, const char* key, bool value) {
    edit_file(g_folder / "settings.json", [=](nlohmann::ordered_json& j) { j[section][key] = value; });
}

void aidyn::settings::save_number(const char* section, const char* key, double value) {
    edit_file(g_folder / "settings.json", [=](nlohmann::ordered_json& j) {
        if (value == (double)(int64_t)value) {
            j[section][key] = (int64_t)value;
        } else {
            j[section][key] = value;
        }
    });
}

void aidyn::settings::save_section(const char* section, const std::string& json_text) {
    edit_file(g_folder / "settings.json", [&](nlohmann::ordered_json& j) {
        j[section] = nlohmann::ordered_json::parse(json_text);
    });
}

void aidyn::settings::apply_widescreen() {
    if (g_graphics.widescreen && g_graphics.aspect_text == "4:3") {
        g_graphics.aspect_text = "16:9";
        edit_file(g_folder / "settings.json", [](nlohmann::ordered_json& j) { j["graphics"]["aspect"] = "16:9"; });
    }
    const std::string& a = g_graphics.aspect_text;
    if (!g_graphics.widescreen || a == "4:3") {
        g_graphics.aspect = Aspect::Original;
    }
    else if (a == "window") {
        g_graphics.aspect = Aspect::Window;
    }
    else {
        g_graphics.aspect = Aspect::Ratio;
        g_graphics.aspect_ratio = parse_ratio(a);
    }
}

void aidyn::settings::save_controls_card(bool shown) {
    g_graphics.controls_card = shown;
    edit_file(g_folder / "settings.json", [shown](nlohmann::ordered_json& j) { j["graphics"]["controls_card"] = shown; });
}
