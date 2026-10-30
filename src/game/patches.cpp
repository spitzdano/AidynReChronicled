// Game functions replaced by hand-written C (listed in aidyn.us.toml's `ignored` and in README.md).
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

#include "platform.h"
#include "settings.h"

namespace {
    constexpr int32_t gGfxManager = (int32_t)0x800D7ED8;
    constexpr int kHres1 = 0x192;   // gGfxManager.Hres[1]: width of the frame being built
    constexpr int kVres1 = 0x196;   // gGfxManager.Vres[1]
    constexpr int kUnk19C = 0x19C;  // gGfxManager.unk0x19c, cleared at the end of every frame

    void put(uint8_t* rdram, gpr& g, uint32_t w0, uint32_t w1) {
        MEM_W(0, g) = w0;
        MEM_W(4, g) = w1;
        g += 8;
    }

    // gDPFillRectangle(ulx, uly, lrx, lry)
    void fill_rect(uint8_t* rdram, gpr& g, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
        put(rdram, g, 0xF6000000 | ((lrx & 0x3FF) << 14) | ((lry & 0x3FF) << 2), ((ulx & 0x3FF) << 14) | ((uly & 0x3FF) << 2));
    }
}

// Graphics::EndList(Gfx* g) -> Gfx*: ends each frame's display list. The original first paints black bars over
// the TV overscan area (16 px at the sides, 12 at the top and bottom; 20 at the sides in 640-wide mode), which a
// CRT hid but a modern display shows as a frame, and which widescreen moves into the middle of the picture.
// They are drawn only when settings.json has "overscan_borders": true; everything else is as the original.
gpr aidyn_ff_marker(uint8_t* rdram, gpr g);  // "Fast-forward", below
gpr aidyn_prompts(uint8_t* rdram, gpr g);    // "Button prompts", below
std::string help_keys_text();               // "Keys on the prompts", below
gpr draw_key_slots(uint8_t* rdram, gpr g, size_t index, float x, float y, float sx, float sy);  // "Keys on the prompts", below
gpr draw_button_cap(uint8_t* rdram, gpr g, const char* button, float x, float y, float sx, float sy, float w, float h, bool cover, bool text_state = false);
gpr draw_dialogue_hint(uint8_t* rdram, recomp_context* ctx, gpr g);  // "Conversation hint", below
gpr aidyn_rc_page(uint8_t* rdram, recomp_context* ctx, gpr g);  // "ReChronicled page", below
gpr draw_combat_log(uint8_t* rdram, recomp_context* ctx, gpr g, float card_top, bool menu_open);  // "Combat log"
gpr draw_spell_info(uint8_t* rdram, recomp_context* ctx, gpr g);  // "Spell descriptions in combat", below
bool aidyn_restored_samples_available();  // "Restored instrument samples", below
gpr draw_fps_counter(uint8_t* rdram, recomp_context* ctx, gpr g);  // "FPS counter", below
gpr draw_save_notice(uint8_t* rdram, recomp_context* ctx, gpr g);  // "Quick and auto saves", below
gpr treasure_sparkle(uint8_t* rdram, recomp_context* ctx, gpr g);   // "Treasure sparkle", below
void all_props_frame_end();  // "Every prop considered", below
void heap_trace_frame(uint8_t* rdram);  // "Heap trace", below
void free_spill_frame(uint8_t* rdram, recomp_context* ctx);  // "Free queue overflow", below
void combat_log_new_battle();
void combat_move_log(uint8_t* rdram);  // "Stuck checks per tick", below: AIDYN_COMBAT_MOVE_LOG
bool combat_log_frame = false;     // aidyn_prompts: in combat this frame, the log to be drawn
float combat_card_top = 240.0f;    // the combat controls card's top this frame (240: not shown)
bool combat_menu_open = false;     // a held X / Y menu was drawn this frame (aidyn_combat_menu_drawn)

namespace aidyn {
    void set_gap_fill_horizon(int row);  // src/main/rt64_context.cpp
}
extern int aidyn_sky_horizon;  // "Below the horizon", below: this frame's horizon row, or -1

// The display lists' size. The game gives each of its two lists 0x32000 bytes with the Expansion Pak (0x19000
// without), from its heap. The widescreen picture (more characters and props in view), the far map cells at draw
// distance 2-3 and this edition's HUD already fill a forest frame to three quarters of that, and a frame that runs
// past the end stops the game ("GLIST OVERWRITE!!": appProc checks after Graphics::EndList). Graphics::initGfx is
// hooked where the size is chosen ($s3, before the two HALLOCs) for twice the room, and appProc's check (its limit
// in commands, $a3, 0x6400) follows it.
constexpr uint32_t kDisplayListBytes = 0x64000;

extern "C" void aidyn_display_list_size(uint8_t* rdram, recomp_context* ctx) {
    fprintf(stderr, "Display lists: 2 x %u bytes (the game's own: %u)\n", kDisplayListBytes, (uint32_t)ctx->r19);
    ctx->r19 = kDisplayListBytes;
}

extern "C" void aidyn_display_list_limit(uint8_t* rdram, recomp_context* ctx) {
    ctx->r7 = kDisplayListBytes / 8;
}

void update_glyph_widths(uint8_t* rdram, recomp_context* ctx);  // "Keys in the game's text", below
recomp_context* frame_ctx = nullptr;  // this frame's context, for the key caps drawn inside picture draws

extern "C" void Graphics__EndList(uint8_t* rdram, recomp_context* ctx) {
    frame_ctx = ctx;
    update_glyph_widths(rdram, ctx);
    aidyn::set_gap_fill_horizon(aidyn_sky_horizon);
    aidyn_sky_horizon = -1;
    gpr g = ctx->r4;
    g = aidyn_ff_marker(rdram, g);
    all_props_frame_end();
    heap_trace_frame(rdram);
    free_spill_frame(rdram, ctx);
    g = treasure_sparkle(rdram, ctx, g);
    g = aidyn_prompts(rdram, g);
    g = draw_dialogue_hint(rdram, ctx, g);
    if (combat_log_frame) {
        combat_move_log(rdram);
        g = draw_combat_log(rdram, ctx, g, combat_card_top, combat_menu_open);
    }
    combat_menu_open = false;
    g = draw_spell_info(rdram, ctx, g);
    g = aidyn_rc_page(rdram, ctx, g);
    g = draw_fps_counter(rdram, ctx, g);
    g = draw_save_notice(rdram, ctx, g);
    if (aidyn::settings::graphics().overscan_borders) {
        const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager);
        const uint32_t v = MEM_HU(kVres1, (gpr)gGfxManager);
        const uint32_t side = (h == 320) ? 16 : 20;
        put(rdram, g, 0xE7000000, 0);          // gDPPipeSync
        put(rdram, g, 0xE3000A01, 0x00300000); // gDPSetCycleType(G_CYC_FILL)
        put(rdram, g, 0xE200001C, 0);          // gDPSetRenderMode(0, 0)
        put(rdram, g, 0xF7000000, 0);          // gDPSetFillColor(0)
        fill_rect(rdram, g, 0, 0, side - 1, v - 1);
        fill_rect(rdram, g, 0, 0, h - 1, 11);
        fill_rect(rdram, g, h - side, 0, h - 1, v - 1);
        fill_rect(rdram, g, 0, v - 12, h - 1, v - 1);
    }
    put(rdram, g, 0xE9000000, 0);              // gDPFullSync
    put(rdram, g, 0xDF000000, 0);              // gSPEndDisplayList
    // How full the frame's display list got (aidyn_display_list_size; gGfxManager.GfxLists[bufferChoice] at +4 /
    // +0x19A). Each new most is logged from three quarters of the game's own size on (past 100% there, the game's own
    // list would have run out).
    {
        constexpr uint32_t kGameListBytes = 0x32000;
        static uint32_t most = 0;
        const gpr start = MEM_W(4 + 4 * MEM_BU(0x19A, (gpr)gGfxManager), (gpr)gGfxManager);
        const uint32_t used = (uint32_t)(g - start);
        if (used > most && used < 0x1000000) {
            most = used;
            if (used > kGameListBytes * 3 / 4) {
                fprintf(stderr, "Display list: %u bytes, %u%% of the game's own size, %u%% of this one's%s\n", used,
                        used * 100 / kGameListBytes, used * 100 / kDisplayListBytes,
                        used > kDisplayListBytes ? ": PAST ITS END" : "");
            }
        }
    }
    MEM_B(kUnk19C, (gpr)gGfxManager) = 0;
    // osWritebackDCacheAll: nothing to write back on the runtime.
    ctx->r2 = g;
}

// AIDYN_UI_LOG=<folder>: every 2D image the game draws goes through N64BorgImageDraw(g, Borg8Header* img, ...)
// (menus, HUD, fonts, portraits). aidyn.us.toml hooks its first instruction to call this, which saves each image
// once, whole, as <folder>/<pixel address>.bin (+ .pal for palettes) with a line in <folder>/images.jsonl.
// tools/hdui uses these to know which RT64 textures are UI, and to upscale each image in one piece. Off unless set.
#include <atomic>
#include <cstdio>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

// AIDYN_PAGE_LOG=1: every full-screen picture's draw (pause page backgrounds) with its position and clip, printed
// when they change, and its pixels' address for src/main/rt64_context.cpp, which reports when they lie in memory
// RT64 tracks as a framebuffer. For finding why a page sometimes shows its original picture. Off unless set.
std::atomic_uint32_t aidyn_page_log_image_start{0}, aidyn_page_log_image_end{0};

static void log_page_draw(uint8_t* rdram, recomp_context* ctx) {
    const gpr img = ctx->r5;
    if (img == 0 || MEM_HU(0x0A, img) != 320 || MEM_HU(0x0C, img) != 240) {
        return;
    }
    static const uint32_t bpp[] = { 0, 32, 16, 16, 8, 8, 8, 4, 4, 4 };
    const uint32_t format = MEM_HU(0x08, img);
    const uint32_t pixels = (uint32_t)MEM_W(0x14, img) & 0x3FFFFFF;
    if (format < 1 || format > 9) {
        return;
    }
    aidyn_page_log_image_start = pixels;
    aidyn_page_log_image_end = pixels + 320 * 240 * bpp[format] / 8;
    // N64BorgImageDraw(g, img, float x, float y, u16 xOff, u16 yOff, u16 h, u16 v, float xScale, float yScale, ...):
    // x and y arrive in a2 and a3, the rest on the stack from sp+0x10.
    const gpr sp = ctx->r29;
    float x, y, xs, ys;
    uint32_t w;
    w = (uint32_t)ctx->r6; std::memcpy(&x, &w, 4);
    w = (uint32_t)ctx->r7; std::memcpy(&y, &w, 4);
    w = (uint32_t)MEM_W(0x20, sp); std::memcpy(&xs, &w, 4);
    w = (uint32_t)MEM_W(0x24, sp); std::memcpy(&ys, &w, 4);
    char line[160];
    snprintf(line, sizeof(line), "page draw: fmt %u pixels %06X at %.2f,%.2f off %u,%u to %u,%u scale %.3f,%.3f",
             format, pixels, x, y, MEM_W(0x10, sp) & 0xFFFF, MEM_W(0x14, sp) & 0xFFFF, MEM_W(0x18, sp) & 0xFFFF,
             MEM_W(0x1C, sp) & 0xFFFF, xs, ys);
    static std::mutex mutex;
    static std::set<std::string> printed;
    std::lock_guard lock(mutex);
    if (printed.insert(line).second) {
        fprintf(stdout, "%s\n", line);
        fflush(stdout);
    }
}

void normalize_big_draw(uint8_t* rdram, recomp_context* ctx);  // below, "Big pictures"

void note_page_arrow(uint8_t* rdram, recomp_context* ctx);  // "Button prompts", below

void aidyn_save_picture_drawn(uint8_t* rdram, gpr img);  // src/game/save_pictures.cpp
void aidyn_page_words_drawn(uint8_t* rdram, gpr img);    // src/game/page_words.cpp

void aidyn_hud_frames_image(uint8_t* rdram, recomp_context* ctx);  // "Portrait frames", near the end

bool glyph_as_key(uint8_t* rdram, recomp_context* ctx);  // "Keys in the game's text", below

extern "C" int aidyn_ui_image_seen(uint8_t* rdram, recomp_context* ctx) {
    if (glyph_as_key(rdram, ctx)) {
        return 1;  // a button symbol in the game's text, drawn as a key cap: the font's own is skipped
    }
    aidyn_hud_frames_image(rdram, ctx);
    note_page_arrow(rdram, ctx);
    if (ctx->r5 != 0) {
        aidyn_save_picture_drawn(rdram, ctx->r5);
        aidyn_page_words_drawn(rdram, ctx->r5);
    }
    static const bool page_log = std::getenv("AIDYN_PAGE_LOG") != nullptr;
    if (page_log) {
        log_page_draw(rdram, ctx);
    }
    normalize_big_draw(rdram, ctx);
    const gpr img = ctx->r5;
    static const char* dir = std::getenv("AIDYN_UI_LOG");
    if (dir == nullptr || *dir == '\0' || img == 0) {
        return 0;
    }
    static std::mutex mutex;
    static std::set<uint64_t> seen;
    const uint32_t format = MEM_HU(0x08, img);
    const uint32_t width = MEM_HU(0x0A, img);
    const uint32_t height = MEM_HU(0x0C, img);
    const uint32_t palette = (uint32_t)MEM_W(0x10, img);
    const uint32_t pixels = (uint32_t)MEM_W(0x14, img);
    // Bits per pixel for Borg8Format 1..9: RGBA32 RGBA16 IA16 CI8 IA8 I8 CI4 IA4 I4.
    static const uint32_t bpp[] = { 0, 32, 16, 16, 8, 8, 8, 4, 4, 4 };
    if (format < 1 || format > 9 || width == 0 || height == 0 || width > 1024 || height > 1024) {
        return 0;
    }
    // The game reuses memory: a page's background can later load where another picture used to be. Images are told
    // apart by address and content (FNV-1a over pixels and palette), so each distinct picture is saved once.
    const uint32_t bytes = width * height * bpp[format] / 8;
    const uint32_t palette_entries = (format == 4) ? 256 : (format == 7) ? 16 : 0;
    uint64_t hash = 0xCBF29CE484222325ULL;
    auto mix = [&](uint32_t address, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            hash = (hash ^ MEM_BU(i, (gpr)(int32_t)address)) * 0x100000001B3ULL;
        }
    };
    mix(pixels, bytes);
    if (palette_entries != 0 && palette != 0) {
        mix(palette, palette_entries * 2);
    }
    std::lock_guard lock(mutex);
    if (!seen.insert(hash ^ pixels).second) {
        return 0;
    }
    std::filesystem::create_directories(dir);
    char name[48];
    snprintf(name, sizeof(name), "%08X_%016llX", pixels, (unsigned long long)hash);
    auto save = [&](const std::string& path, uint32_t address, uint32_t count) {
        FILE* f = fopen(path.c_str(), "wb");
        if (f == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            fputc(MEM_BU(i, (gpr)(int32_t)address), f);
        }
        fclose(f);
    };
    save(std::string(dir) + "/" + name + ".bin", pixels, bytes);
    if (palette_entries != 0 && palette != 0) {
        save(std::string(dir) + "/" + name + ".pal", palette, palette_entries * 2);
    }
    if (FILE* log = fopen((std::string(dir) + "/images.jsonl").c_str(), "a")) {
        fprintf(log, "{\"pixels\": \"%08X\", \"file\": \"%s\", \"format\": %u, \"width\": %u, \"height\": %u, \"palette\": \"%08X\", \"borg\": %d}\n",
                pixels, name, format, width, height, palette, (int)MEM_W(0x00, img));
        fclose(log);
    }
    return 0;
}

// Menus drawn this frame. RT64's widescreen keeps some 2D draws centred and stretches others, which pulls apart menus
// that mix both (the equipment page's slot frames slid away from their icons; Level/Gold went missing). The pause menu
// (PauseWidget::Render, every page, also the combat inventory) and the equipment doll in shops (WidgetDollMenu's
// Render, 0x80030430 from its vtable at 0x800CF3D0) call this through aidyn.us.toml hooks; rt64_context.cpp shows
// those frames in 4:3.
#include <atomic>
std::atomic_bool aidyn_menu_frame{ false };

extern "C" void aidyn_menu_drawn(void) {
    aidyn_menu_frame.store(true, std::memory_order_relaxed);
}

// The pause menu opens with a reveal: bigAssOpenCallback_1 widens gGlobals.scrollLocation, the clip every page is
// drawn through, towards 0..320 in step with the 3D frame animation, and marks the menu open when that animation ends.
// It ends a few steps early, leaving the clip at 17..303 when the menu opens on the equipment page (other pages
// similar) until the menu closes. A TV's overscan and the game's black borders hid those edges. Here they showed as a
// narrower page, and the journal slid in under that clip was cut into texture strips the HD pack doesn't have, so it
// fell back to the original parchment. aidyn.us.toml hooks the "menu open" store to finish the reveal at 0..320.
extern "C" void aidyn_menu_opened(uint8_t* rdram, gpr scroll) {
    MEM_H(0x18, scroll) = 0;    // gGlobals.scrollLocation[0]
    MEM_H(0x1A, scroll) = 320;  // gGlobals.scrollLocation[1]
}

// Right-stick camera. Walking uses Camera::Mode0 (ProcessGameCamera_mode0, 0x800A6E64). Each frame it puts the
// camera's target position (Camera_struct.posTarget, +0x18) on the line from the player (aim, +0xC) through the
// camera (pos, +0x0) at the follow distance (+0x5C) and the height angle (+0x48, degrees), with func_800A681C; then
// eases that target towards a second point, behind the player's facing turned by +0x58 degrees (func_800A68E0); and
// eases the camera towards the target. So turning the target alone was undone the next frame, and the game resets
// +0x58 whenever the player moves (func_800A67FC).
//
// Actor::Move (aidyn.us.toml hook at 0x8001532C) calls aidyn_camera_stick for every actor with s3 = the player handler
// (camera pointer at +4) and s2 = the actor. For the player, the right stick's x turns the camera itself (pos and
// posTarget) around the player, and y changes the height angle (+0x48 and the resting angle +0x4C it is reset to),
// both at the same rate, by game ticks (not the host clock, so each shown frame turns evenly). The height angle goes
// the same distance up and down from where the camera rests (walking: 15 degrees, so -15 to 45; the game itself only
// allowed 0 to 45, a third of the way up and two thirds down). graphics.camera_invert_x / camera_invert_y flip them. Once the camera has been turned, func_800A68E0 (replaced below) keeps
// the camera's own direction as its "behind" point, so the camera stays where it was put and follows at the same
// distance; pressing the right stick (R3) puts it back behind the player at its resting height, as the original. Movement is relative to
// the camera, so an angle relative to the player's facing would make the camera chase itself round in circles.
#include "controls.h"
#include "input.h"
#include "watch.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {
    float read_f32(uint8_t* rdram, gpr addr) {
        uint32_t bits = (uint32_t)MEM_W(0, addr);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }
    void write_f32(uint8_t* rdram, gpr addr, float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(f));
        MEM_W(0, addr) = (int32_t)bits;
    }

    constexpr float kTurnSpeed = 150.0f;   // degrees a second at full tilt, both axes
    constexpr float kTiltRange = 30.0f;    // degrees up and down from the resting height angle
    float rest_angle = NAN;                // the camera's resting height angle before the stick moved it
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    std::atomic_bool free_camera{ false };  // turned by the right stick since the last recentre
}

extern "C" void aidyn_camera_stick(uint8_t* rdram, gpr handler, gpr actor) {
    if (!aidyn::settings::graphics().right_stick_camera || (MEM_HU(0x1E, actor) & 1) == 0) {
        return;  // not the player character
    }
    gpr cam = (gpr)(int32_t)MEM_W(4, handler);
    if (cam == 0) {
        return;
    }
    const float dt = 1.0f / 60.0f;  // called once per game tick, which is 1/60 s of game time

    static bool was_clicked = false;
    const bool clicked = aidyn::input::right_stick_click();
    if (clicked && !was_clicked) {
        free_camera = false;  // back behind the player (the original follow camera)
        if (!std::isnan(rest_angle)) {
            write_f32(rdram, cam + 0x48, rest_angle);
            write_f32(rdram, cam + 0x4C, rest_angle);
            rest_angle = NAN;
        }
    }
    was_clicked = clicked;

    float rx, ry;
    aidyn::input::right_stick(&rx, &ry);
    if (aidyn::settings::graphics().camera_invert_x) {
        rx = -rx;
    }
    if (aidyn::settings::graphics().camera_invert_y) {
        ry = -ry;
    }
    // The mouse (src/main/controls.cpp): degrees turned and tilted since the last tick, on top of the stick's.
    float mouse_turn = 0.0f, mouse_tilt = 0.0f;
    aidyn::controls::look(&mouse_turn, &mouse_tilt);
    if (aidyn::settings::graphics().camera_invert_x) {
        mouse_turn = -mouse_turn;
    }
    const float turn = rx * kTurnSpeed * dt + mouse_turn;   // right positive
    const float tilt = ry * kTurnSpeed * dt + mouse_tilt;   // up positive
    if (turn != 0.0f) {
        const float a = turn * kDegToRad;  // stick right: the view turns right
        const float c = std::cos(a), s = std::sin(a);
        const float ax = read_f32(rdram, cam + 0x0C), az = read_f32(rdram, cam + 0x14);
        for (int point : { 0x00, 0x18 }) {  // pos, posTarget
            const float dx = read_f32(rdram, cam + point) - ax, dz = read_f32(rdram, cam + point + 8) - az;
            write_f32(rdram, cam + point, ax + dx * c - dz * s);
            write_f32(rdram, cam + point + 8, az + dx * s + dz * c);
        }
        free_camera = true;
    }
    if (tilt != 0.0f) {
        if (std::isnan(rest_angle)) {
            rest_angle = read_f32(rdram, cam + 0x4C);
        }
        const float lo = rest_angle - kTiltRange, hi = rest_angle + kTiltRange;
        float angle = read_f32(rdram, cam + 0x48) - tilt;  // stick up: the camera dips, looking up
        angle = std::clamp(angle, lo, hi);
        write_f32(rdram, cam + 0x48, angle);
        write_f32(rdram, cam + 0x4C, angle);
        static const bool log = std::getenv("AIDYN_CAMERA_LOG") != nullptr;
        if (log) {
            fprintf(stderr, "camera: height angle %.1f (limits %.1f..%.1f), distance %.1f\n", angle, lo, hi,
                    read_f32(rdram, cam + 0x5C));
        }
    }
}

// func_800A68E0(Camera_struct* cam, vec3f* out, vec3f* facing): Mode0's "behind the player" point, the player's facing
// turned by cam+0x58 degrees at the follow distance and height angle. Same, except after the right stick has turned the
// camera: then the direction is the camera's own (Camera_struct.rotationXZ, +0x30, player minus camera), as
// func_800A681C uses, so Mode0 has nothing to swing back to.
extern "C" void func_800A68E0(uint8_t* rdram, recomp_context* ctx) {
    const gpr cam = ctx->r4, out = ctx->r5, facing = ctx->r6;
    const float dtor = read_f32(rdram, (gpr)(int32_t)0x800D5828);   // the game's degrees-to-radians constant
    const float deg180 = read_f32(rdram, (gpr)(int32_t)0x800D582C); // 180.0
    const float dist = read_f32(rdram, cam + 0x5C);
    float dx, dz;
    if (free_camera && aidyn::settings::graphics().right_stick_camera) {
        dx = -read_f32(rdram, cam + 0x30);
        dz = -read_f32(rdram, cam + 0x34);
    } else {
        const float a = read_f32(rdram, cam + 0x58) * dtor;
        const float s = std::sin(a), c = std::cos(a);
        const float fx = read_f32(rdram, facing + 0), fz = read_f32(rdram, facing + 8);
        dx = c * fx + s * fz;
        dz = c * fz - s * fx;
    }
    write_f32(rdram, out + 0, read_f32(rdram, cam + 0x0C) + dx * dist);
    write_f32(rdram, out + 4, read_f32(rdram, cam + 0x10) +
                              std::sin((deg180 - read_f32(rdram, cam + 0x48)) * dtor) * dist);
    write_f32(rdram, out + 8, read_f32(rdram, cam + 0x14) + dz * dist);
}

// Big pictures. N64BorgImageDraw(g, img, x, y, xOff, yOff, h, v, xScale, yScale, ...) draws the part [xOff, h) x
// [yOff, v) of an image. One that doesn't fit TMEM in one load (page backgrounds, title cards, the Controller Pak
// screen, map pieces, clouds) goes in strips of whole rows of that part, so every crop (a page sliding in, a reveal,
// a list redrawing behind a line of text) loads differently shaped strips, and a texture pack, which matches strips
// by their exact contents, only had the crops someone happened to capture. Here, on entry, such a draw is widened to
// the image's full width and the rows from the strip grid line at or above yOff to the image's bottom, moved so the
// requested part stays where it was; on exit (aidyn.us.toml hook before the return), each texture rectangle the
// game wrote is clipped back to the requested area, its texture coordinates moved to match, and rectangles left
// with nothing are turned into no-ops. Every draw of an image then loads the same few strips, which tools/hdui
// makes for every such image in the ROM. The two font sheets (36x414) are left alone: they are drawn a glyph at a
// time and the pack covers every glyph crop.
namespace {
    struct BigDraw {
        bool active = false;
        gpr g_in = 0, sp = 0;
        uint16_t xoff = 0, yoff = 0, h = 0, v = 0;
        int32_t cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // requested area, screen pixels in 10.2 fixed point
        bool title_card = false;                      // the title screen's card (188x174 RGBA16): add the branding
        float ox = 0, oy = 0, sx = 1, sy = 1;         // where the image's texel (0, 0) lands, and its scale (pixels)
    };
    BigDraw big_draw;

    // Rows between strips for a full-width load: N64BorgImageDraw's own rule (TMEM size over the padded row size, less
    // the extra row each strip loads for filtering).
    int strip_step(uint32_t format, uint32_t w) {
        auto pad = [](uint32_t n, uint32_t m) { return (n + m - 1) / m * m; };
        switch (format) {
            case 1: return (int)(0x1000 / (pad(w, 2) * 4)) - 1;                       // RGBA32
            case 2: case 3: return (int)(0x1000 / (pad(w, 4) * 2)) - 1;               // RGBA16, IA16
            case 4: return (int)(0x800 / pad(w, 8)) - 1;                              // CI8
            case 5: case 6: return (int)(0x1000 / pad(w, 8)) - 1;                     // IA8, I8
            case 7: return (int)(0x800 / (pad(w, 16) / 2)) - 1;                       // CI4
            default: return (int)(0x1000 / (pad(w, 16) / 2)) - 1;                    // IA4, I4
        }
    }
}

void normalize_big_draw(uint8_t* rdram, recomp_context* ctx) {
    big_draw.active = false;
    const gpr img = ctx->r5;
    if (img == 0) {
        return;
    }
    static const uint32_t bpp[] = { 0, 32, 16, 16, 8, 8, 8, 4, 4, 4 };
    const uint32_t format = MEM_HU(0x08, img), w = MEM_HU(0x0A, img), h_img = MEM_HU(0x0C, img);
    if (format < 1 || format > 9 || w == 0 || h_img == 0 || (w == 36 && h_img == 414)) {
        return;
    }
    const uint32_t capacity = (format == 4 || format == 7) ? 0x800 : 0x1000;
    if (w * h_img * bpp[format] / 8 <= capacity) {
        return;
    }
    const gpr sp = ctx->r29;
    const uint16_t xoff = MEM_HU(0x12, sp), yoff = MEM_HU(0x16, sp), h = MEM_HU(0x1A, sp), v = MEM_HU(0x1E, sp);
    const int step = strip_step(format, w);
    if (h <= xoff || v <= yoff || h > w || v > h_img || step < 1) {
        return;
    }
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    // The requested area, worked out as N64BorgImageDraw does.
    const int32_t left = (int32_t)(x * hs * 4.0f);
    const int32_t top = (int32_t)(y * vs * 4.0f);
    BigDraw d;
    d.active = true;
    d.g_in = ctx->r4;
    d.sp = sp;
    d.xoff = xoff; d.yoff = yoff; d.h = h; d.v = v;
    d.cx0 = left;
    d.cx1 = left + (int32_t)((float)(int)(h - xoff) * xs * hs * 4.0f);
    d.cy0 = top;
    d.cy1 = (int32_t)(y * vs * 4.0f + (float)(int)(v - yoff) * ys * vs * 4.0f);
    d.title_card = format == 2 && w == 188 && h_img == 174;
    d.ox = (x - (float)xoff * xs) * hs;
    d.oy = (y - (float)yoff * ys) * vs;
    d.sx = xs * hs;
    d.sy = ys * vs;
    // Widen to the full width, from the strip grid line at or above yOff to the bottom.
    const uint16_t yoff2 = (uint16_t)(yoff / step * step);
    x -= (float)xoff * xs;
    y -= (float)(yoff - yoff2) * ys;
    std::memcpy(&bits, &x, 4);
    ctx->r6 = (int32_t)bits;
    std::memcpy(&bits, &y, 4);
    ctx->r7 = (int32_t)bits;
    MEM_H(0x12, sp) = 0;
    MEM_H(0x16, sp) = yoff2;
    MEM_H(0x1A, sp) = (uint16_t)w;
    MEM_H(0x1E, sp) = (uint16_t)h_img;
    big_draw = d;
}

gpr draw_title_branding(uint8_t* rdram, gpr g, float ox, float oy, float sx, float sy, uint32_t rgba);  // "Title screen"
gpr title_versions_panel(uint8_t* rdram, gpr g);  // the list of versions Y opens (the title screen)

extern "C" void aidyn_image_draw_done(uint8_t* rdram, recomp_context* ctx) {
    if (!big_draw.active) {
        return;
    }
    const BigDraw d = big_draw;
    big_draw.active = false;
    // The caller's stack arguments go back as they were (a caller may draw again with the same ones).
    MEM_H(0x12, d.sp) = d.xoff;
    MEM_H(0x16, d.sp) = d.yoff;
    MEM_H(0x1A, d.sp) = d.h;
    MEM_H(0x1E, d.sp) = d.v;
    const gpr end = ctx->r2;
    for (gpr g = d.g_in; g + 16 < end; g += 8) {
        const uint32_t w0 = (uint32_t)MEM_W(0, g);
        if ((w0 >> 24) != 0xE4 || ((uint32_t)MEM_W(8, g) >> 24) != 0xE1 || ((uint32_t)MEM_W(16, g) >> 24) != 0xF1) {
            continue;
        }
        // gSPTextureRectangle: E4 xh yh | tile xl yl, E1 s t, F1 dsdx dtdy (screen 10.2, s/t S10.5, dsdx/dtdy S5.10).
        const uint32_t w1 = (uint32_t)MEM_W(4, g), st = (uint32_t)MEM_W(12, g), dd = (uint32_t)MEM_W(20, g);
        const int32_t xh = (w0 >> 12) & 0xFFF, yh = w0 & 0xFFF, xl = (w1 >> 12) & 0xFFF, yl = w1 & 0xFFF;
        int32_t s = (int16_t)(st >> 16), t = (int16_t)(st & 0xFFFF);
        const int32_t dsdx = (int16_t)(dd >> 16), dtdy = (int16_t)(dd & 0xFFFF);
        const int32_t nxl = std::max(xl, d.cx0), nyl = std::max(yl, d.cy0);
        const int32_t nxh = std::min(xh, d.cx1), nyh = std::min(yh, d.cy1);
        if (nxl >= nxh || nyl >= nyh) {
            for (int i = 0; i < 3; i++) {  // G_SPNOOP
                MEM_W(i * 8, g) = 0;
                MEM_W(i * 8 + 4, g) = 0;
            }
        } else {
            // (pixels / 4) * (texels per pixel / 1024) * 32 = 10.2 pixels * dsdx / 128.
            s += ((nxl - xl) * dsdx + 64) >> 7;
            t += ((nyl - yl) * dtdy + 64) >> 7;
            MEM_W(0, g) = (int32_t)(0xE4000000u | ((uint32_t)nxh << 12) | (uint32_t)nyh);
            MEM_W(4, g) = (int32_t)((w1 & 0x07000000u) | ((uint32_t)nxl << 12) | (uint32_t)nyl);
            MEM_W(12, g) = (int32_t)(((uint32_t)(s & 0xFFFF) << 16) | (uint32_t)(t & 0xFFFF));
        }
        g += 16;
    }
    if (d.title_card) {
        // The card's colour and fade: N64BorgImageDraw's r, g, b, a (stack words at +0x28..+0x34).
        const uint32_t rgba = (uint32_t)MEM_BU(0x2B, d.sp) << 24 | (uint32_t)MEM_BU(0x2F, d.sp) << 16 |
                              (uint32_t)MEM_BU(0x33, d.sp) << 8 | MEM_BU(0x37, d.sp);
        ctx->r2 = draw_title_branding(rdram, ctx->r2, d.ox, d.oy, d.sx, d.sy, rgba);
    }
}

// Draw distance. The field keeps a 3x3 block of map cells loaded around the player (100 units each outdoors; world
// space has its origin at the centre cell's corner) and draws them with a far clip of 180 units (RenderZones,
// aidyn.us.toml hook), so with the fog at the far end of the depth range the loaded ground ends 100 to 280 units away.
// settings.json draw_distance (1..3) is used three ways (and a fourth further down: monsters, people and items):
// - The far clip follows the loaded ground: each frame it goes to where the view (the whole picture's width) leaves
//   the 3x3 block, never under the original 180 nor over 180 x draw_distance, eased so it doesn't jump when turning.
//   The game's fog still thickens into it, so the edge of the loaded world never shows.
// - Props (trees, rocks, objects: RenderVoxelScenes) measure their distance from the player as 1/draw_distance of it
//   (aidyn.us.toml hook after its Vec3Dist), so their cut-off and level-of-detail bands move out together.
// - The views the game culls with were made for 4:3: map cells outside a 42.5-degree wedge 200 units long
//   (func_8000D270: 0x800CD6F4 length, 0x800CD6F8 margin, 0x800CD6FC angle) and props outside a cone fading from
//   37.5 to 42.5 degrees (RenderVoxelScenes: 0x800CD6A0, 0x800CD6A4, radians). Each constant is read only there; they
//   are rewritten each frame for the picture's width (the field's vertical field of view is 45 degrees) and the far
//   clip, so nothing is cut at the sides of a 16:9 or 21:9 picture.
namespace {
    constexpr int32_t kGlobals = 0x800D8088;
    constexpr float kPi = 3.14159265f;

    float view_aspect() {
        const auto& gfx = aidyn::settings::graphics();
        switch (gfx.aspect) {
            case aidyn::settings::Aspect::Ratio: return (float)gfx.aspect_ratio;
            case aidyn::settings::Aspect::Window: return 2.4f;  // assume an ultrawide window
            default: return 4.0f / 3.0f;
        }
    }

    // Half the horizontal field of view, degrees.
    float half_view_degrees() {
        return std::atan(std::tan(22.5f * kPi / 180.0f) * view_aspect()) * 180.0f / kPi;
    }

    int far_ring_radius();  // below, "Far map cells": 1 = only the game's 3x3 block
    float far_clip_now = 180.0f;  // the field's far clip this frame (aidyn_field_far_plane)

    // Depth (along the view) at which the picture first leaves the loaded block of cells.
    float loaded_edge_depth(uint8_t* rdram) {
        const gpr g = (gpr)kGlobals;
        const float px = read_f32(rdram, g + 0x214), pz = read_f32(rdram, g + 0x21C);  // gCamera.pos
        const float ax = read_f32(rdram, g + 0x220), az = read_f32(rdram, g + 0x228);  // gCamera.aim
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        float fx = ax - px, fz = az - pz;
        const float len = std::sqrt(fx * fx + fz * fz);
        if (len < 1e-4f || cx <= 0.0f || cz <= 0.0f) {
            return 1e9f;
        }
        fx /= len;
        fz /= len;
        const float half = half_view_degrees() * kPi / 180.0f;
        const float r = (float)far_ring_radius();
        float depth = 1e9f;
        for (int k = -4; k <= 4; k++) {
            const float a = half * (float)k / 4.0f, c = std::cos(a), sn = std::sin(a);
            const float dx = fx * c - fz * sn, dz = fx * sn + fz * c;
            float t = 1e9f;
            if (dx > 1e-5f) t = std::min(t, ((r + 1.0f) * cx - px) / dx);
            if (dx < -1e-5f) t = std::min(t, (-r * cx - px) / dx);
            if (dz > 1e-5f) t = std::min(t, ((r + 1.0f) * cz - pz) / dz);
            if (dz < -1e-5f) t = std::min(t, (-r * cz - pz) / dz);
            depth = std::min(depth, std::max(t, 0.0f) * c);
        }
        return depth;
    }

    void write_f32_at(uint8_t* rdram, int32_t address, float f) {
        write_f32(rdram, (gpr)address, f);
    }
}

extern "C" void get_memFree(uint8_t* rdram, recomp_context* ctx);  // the game's free heap bytes (0x8008E980)

// AIDYN_FAR_LOG also counts, per second, the props tried and how many are near enough to draw by the original rule
// and by the scaled one.
static int props_tried = 0, props_original = 0, props_scaled = 0;

// AIDYN_FAR_LOG=1: once a second, the far clip, the loaded ground's edge and the game's free heap.
static void log_far(uint8_t* rdram, recomp_context* ctx, float clip, float edge) {
    static const bool enabled = std::getenv("AIDYN_FAR_LOG") != nullptr;
    static int frames = 0;
    if (!enabled || ++frames % 60 != 0) {
        return;
    }
    recomp_context saved = *ctx;  // mid-function: every register is someone's
    get_memFree(rdram, ctx);
    const uint32_t free_bytes = (uint32_t)ctx->r2;
    *ctx = saved;
    fprintf(stdout, "far %.0f edge %.0f free heap %u KB, props a frame: tried %d, near enough %d originally, %d now\n",
            clip, std::min(edge, 9999.0f), free_bytes / 1024, props_tried / 60, props_original / 60, props_scaled / 60);
    props_tried = props_original = props_scaled = 0;
    fflush(stdout);
}

extern "C" uint32_t aidyn_field_far_plane(uint8_t* rdram, recomp_context* ctx, uint32_t far_bits) {
    float original;
    std::memcpy(&original, &far_bits, sizeof(original));
    const float scale = aidyn::settings::graphics().draw_distance;
    const float edge = loaded_edge_depth(rdram);
    const float target = std::clamp(edge, original, original * scale);
    static float current = 0.0f;
    current = current <= 0.0f ? target : current + (target - current) * 0.12f;
    const float clip = std::clamp(current, original, original * scale);
    far_clip_now = clip;
    const float half = half_view_degrees();
    write_f32_at(rdram, 0x800CD6F4, clip * (200.0f / 180.0f));                  // cell wedge length (200 at 180)
    write_f32_at(rdram, 0x800CD6FC, std::max(42.5f, half + 13.6f));            // cell wedge angle, degrees
    write_f32_at(rdram, 0x800CD6A0, std::max(37.5f, half + 8.6f) * kPi / 180.0f);   // props: fading from
    write_f32_at(rdram, 0x800CD6A4, std::max(42.5f, half + 13.6f) * kPi / 180.0f);  // props: gone at
    log_far(rdram, ctx, clip, edge);
    uint32_t bits;
    std::memcpy(&bits, &clip, sizeof(clip));
    return bits;
}

// Monsters, people and items farther away. Props are drawn to about 40 units (their typical cut-off) x draw_distance,
// but the field shows characters only within 20 units of the camera's aim and moves them only within 30, and the
// items lying about (the models loot containers show: chests, bags, herb patches, gem veins...) have cut-offs of 5 to
// 30. With draw_distance above 1 they reach twice as far as the props: the original x draw_distance at least, and
// 80 x draw_distance from draw_distance 2 up (160 units at 2, 240 at 3; the far clip there is 180 or more, the fog
// clear), so one in view doesn't vanish as you back away. They exist only in the game's own 3x3 map cells (100 units
// each, the party in the middle one), so past those there are none to show. Their footsteps and other animation
// sounds still fade out at 15 units ("Sound falloff"): the game skips a sound at volume 0, so far ones cost nothing.
namespace {
    float reach(float original) {
        const float dd = aidyn::settings::graphics().draw_distance;
        const float scaled = original * dd;
        return std::max(scaled, scaled + (80.0f * dd - scaled) * std::clamp(dd - 1.0f, 0.0f, 1.0f));
    }

    // Every model a loot container's linked scene object shows, in any map (Borg9 Container voxel objects -> their
    // Scene link -> its Borg5 models): LootChestClosed, LootBagClosed, LootCloverPatch, LootPepperPlant, LootGemVein,
    // LootKendallsHat, crates and barrels and the odd one-off; the chest and bag once opened or blown open
    // (LootChestOpened 0x909, LootChestExploded 0x905, LootBagOpened 0xBAC, LootBagExploded 0xBA8: the game swaps the
    // model); and the teleporters (BORG5_Teleporter 0xC54). Not the glowing lights you follow through the first forest
    // (0x1910, unnamed in the decomp): drawn farther they vanished before you reached them, so they stay props. Where
    // the same model stands as plain decoration it reaches as far too, so seeing one from afar doesn't tell it holds
    // loot.
    bool item_model(uint32_t model) {
        static constexpr uint32_t kItems[] = { 1969, 2032, 2072, 2218, 2246, 2309, 2311, 2313, 2339, 2508, 2546, 2598,
                                               2947, 2952, 2984, 2986, 2988, 3007, 3033, 3156, 3219, 3221, 3223, 14342 };
        return std::find(std::begin(kItems), std::end(kItems), model) != std::end(kItems);
    }

    // PlayerHandler (gGlobals.gameVars.PlayerHandler, 0x800D8440): characters are drawn within shadowDist (+0x64) of
    // the camera's aim and moved, animated and kept loaded within +0x68. ZoneEngineInit sets them to 20 and 30 for the
    // field (128 in combat, 0 on the title screen).
    constexpr int32_t kActorDraw = (int32_t)0x800D84A4;
    constexpr int32_t kActorMove = (int32_t)0x800D84A8;

    // AIDYN_ACTOR_LOG=1: every 2 s, how many characters a frame are drawn (fade checks) and the farthest.
    bool actor_log() {
        static const bool on = std::getenv("AIDYN_ACTOR_LOG") != nullptr;
        return on;
    }
    int actor_checks = 0, actor_frames = 0, items_seen = 0, items_before = 0, items_now = 0;
    float actor_farthest = 0.0f;
}

// A pathlight: a scene object whose second model is BORG5_OrianaLight (RenderVoxelScenes' own test).
static bool pathlight(uint8_t* rdram, gpr voxel) {
    return MEM_HU(0x68, voxel) >= 2 && (uint32_t)MEM_W(0x28 + 12 + 4, voxel) == 0x374A;
}

extern "C" void aidyn_prop_distance(uint8_t* rdram, recomp_context* ctx) {
    const float scale = aidyn::settings::graphics().draw_distance;
    float prox;
    uint32_t bits = ctx->f0.u32l;
    std::memcpy(&prox, &bits, sizeof(prox));
    const gpr prop = ctx->r19;  // s3: the prop's voxel object (header.size +0xC, models from +0x28, count +0x68)
    const float size = read_f32(rdram, prop + 0xC);  // the prop's cut-off distance
    props_tried++;
    props_original += prox < size;
    const uint16_t models = MEM_HU(0x68, prop);
    if ((MEM_HU(0x6A, prop) & 2) == 0 && models >= 1 && models <= 3 && item_model((uint32_t)MEM_W(0x2C, prop))) {
        // An item: its cut-off (the nearer of size and its farthest level of detail) moves out to reach(). Within its
        // own cut-off the game gets the real distance, so its levels of detail switch where they always did; beyond
        // it, out to reach(), a distance inside its farthest level of detail.
        const float far_lod = read_f32(rdram, prop + 0x28 + 12 * (models - 1));
        const float own = std::min(size, far_lod);
        const float nearer = models >= 2 ? read_f32(rdram, prop + 0x28 + 12 * (models - 2)) : 0.0f;
        items_seen++;
        items_before += prox / scale < own;
        // The game fades its farthest level of detail out over the last unit before far_lod (RenderVoxelScenes); with
        // the reach drawn past it, that unit would show the item half see-through between two solid stretches (the
        // chests in Gwernia's barracks, #22), so the reach takes over from the start of that unit.
        const float start = (far_lod <= size && reach(own) > own + 1.0f) ? own - 1.0f : own;
        if (prox >= start && prox < reach(own)) {
            // Fading in over the last 15% of the reach (it popped in at full strength): the game fades a model in
            // over the last unit of its farthest level of detail, so the band is mapped there.
            const float r = reach(own), band = 0.15f * r;
            prox = (prox > r - band && far_lod <= size) ? far_lod - 1.0f + (prox - (r - band)) / band
                                                       : (nearer + own) * 0.5f;
        }
        items_now += prox < own;
    } else if (pathlight(rdram, prop)) {
        // Oriana's pathlights (the glowing lights through the first forest): the game turns one off for good (and
        // notes it in the save's voxel chart) once its nearer model is chosen, at 5 units. Scaled like the other
        // props that happened at 5 x draw_distance, so they went out while still far off. Within 23 units they get
        // the real distance; beyond, out to 24 x draw_distance, a distance in the unit where the light fades.
        const float far_lod = read_f32(rdram, prop + 0x28 + 12);  // 24
        if (prox >= far_lod - 1.0f) {
            const float r = far_lod * scale;
            prox = prox >= r ? r : far_lod - 1.0f + (prox - (far_lod - 1.0f)) / std::max(r - (far_lod - 1.0f), 1.0f);
        }
    } else {
        prox /= scale;
        // Fading in over the last 20% of the range instead of the game's last unit, which at draw_distance's longer
        // ranges, out of the fog, showed as trees popping in whole (the title flyover's valley, spitzdano): that band of
        // the distance is mapped onto the unit where the game fades the farthest level of detail. Kept inside that
        // level's own band (above the nearer level's fade) and only where the level, not size, is the cut-off.
        if (models >= 1 && models <= 3) {
            const float far_lod = read_f32(rdram, prop + 0x28 + 12 * (models - 1));
            const float nearer = models >= 2 ? read_f32(rdram, prop + 0x28 + 12 * (models - 2)) : 0.0f;
            const float band = std::min(0.2f * far_lod, far_lod - nearer - 3.0f);
            if (far_lod <= size && band > 1.0f && prox > far_lod - band && prox < far_lod) {
                prox = far_lod - 1.0f + (prox - (far_lod - band)) / band;
            }
        }
    }
    props_scaled += prox < size;
    std::memcpy(&bits, &prox, sizeof(prox));
    ctx->f0.u32l = bits;
}

// ProcessPlayers' entry, each frame: the field's own character distances (20 and 30) become reach(20) and 10 more.
extern "C" void aidyn_actor_reach(uint8_t* rdram) {
    if (read_f32(rdram, kActorDraw) == 20.0f && read_f32(rdram, kActorMove) == 30.0f) {
        const float draw = reach(20.0f);
        if (draw != 20.0f) {
            write_f32_at(rdram, kActorDraw, draw);
            write_f32_at(rdram, kActorMove, std::max(30.0f, draw + 10.0f));
            fprintf(stderr, "Characters: drawn within %.0f units, moving within %.0f (draw_distance %.1f)\n", draw,
                    std::max(30.0f, draw + 10.0f), aidyn::settings::graphics().draw_distance);
        }
    }
    if (actor_log() && ++actor_frames >= 120) {
        fprintf(stderr, "actors: %.1f drawn a frame, farthest %.0f units (limit %.0f); items in view a frame: %.1f, "
                "near enough %.1f (was %.1f)\n", actor_checks / 120.0f, actor_farthest, read_f32(rdram, kActorDraw),
                items_seen / 120.0f, items_now / 120.0f, items_before / 120.0f);
        actor_checks = actor_frames = items_seen = items_before = items_now = 0;
        actor_farthest = 0.0f;
    }
}

// PlayerShadowAlpha(handler, actor, dist, alpha), replaced: the game fades a character out over the last 2 units
// before shadowDist; farther out that is a pop, so the fade takes the last tenth of it once that is wider.
extern "C" uint32_t aidyn_actor_alpha(uint8_t* rdram, recomp_context* ctx) {
    const float limit = read_f32(rdram, ctx->r4 + 0x64);
    float dist;
    const uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&dist, &bits, sizeof(dist));
    const float fade = std::max(2.0f, limit * 0.1f);
    actor_checks++;
    actor_farthest = std::max(actor_farthest, dist);
    uint32_t a = (uint32_t)ctx->r7 & 0xFF;
    if (limit - fade < dist) {
        a = (uint32_t)std::clamp((limit - dist) / fade * 255.0f, 0.0f, 255.0f);
    }
    const uint32_t own = MEM_BU(0x70C, ctx->r5);  // the actor's shadowAlpha
    return std::min(a, own);
}

// RenderVoxelScenes puts props that are fading (or drawn see-through) in a 32-entry table drawn after the rest
// (0x800E6710, count at 0x800DA38E), without checking it is full. With props drawn farther more can fade at once; when
// it is full the prop is drawn at once instead (aidyn.us.toml hook where an entry is taken).
extern "C" int aidyn_prop_table_full(uint8_t* rdram) {
    return MEM_HU(0, (gpr)(int32_t)0x800DA38E) >= 32;
}

// Far map cells. Outdoors the game keeps only the 3x3 block of map cells around the player, so the ground ends 100 to
// 280 units away. With settings.json draw_distance above 1, rings of cells around that block are loaded and drawn too
// (their ground; within three cells their props too, "Far props" below; no people or collision), out to radius R
// (5x5 at R = 2 ... 9x9 at R = 4, so the far clip, which follows the loaded edge, can reach 180 x draw_distance):
// - Each frame (aidyn.us.toml hook in RenderZones after the game's own cells, display list at sp+0x64): cells that left
//   the radius are freed through the game's free queue; missing cells are loaded, nearest first (two a frame, six
//   while more than 12 are missing: an area just loaded), with the game's BorgAnimLoadScene, while its heap's largest
//   free block stays over 512 KB;
//   each is placed as the game places its neighbours (ZoneMoveSceneDat: flags 0x10, fog, 0x8, 0x4, identity matrices,
//   matrixA at (dx, 0, dz) cells from the centre cell's origin) and drawn, when in view, with SetSceneColors +
//   BorgAnimDrawScene, as the game draws a cell.
// - The 3x3's own neighbours get a copy too, drawn whenever the game has none there. The centre cell's map hides some
//   neighbours on purpose (its Borg9 byte 0x1a, a bit each, cleared by loadGameBorgScenes: the N64's short view mostly
//   couldn't see them), and the game loads a new neighbour over a few frames. Without the copies a cell vanished when
//   the centre moved (the castle across the bay in the title flyover) and nothing past a hidden neighbour showed.
//   Hidden neighbours still have no props or people (the game doesn't load their map).
// - In view (far_cell_visible): a corner or edge midpoint within the picture's half-angle plus 15 degrees, widened for
//   a tilted camera, whose corners reach wider (the flyover's cliffs popped in at the picture's edge), and the far clip.
// - The game is told it has 15 MB of RAM, not 8 (aidyn.us.toml hook in MemoryCheck): its heap grows from 6.2 to 13 MB
//   and its framebuffers move up to just under 15 MB. Nothing in the game assumes 8 MB; RT64 sees 16 MB of addresses.
// - FreeZoneEngine and FreeZoneEngineMemory (leaving the field) free every ring cell.
// The ring cells' scene pointers live in RDRAM (kFarTable) so the game's free queue can clear them.
extern "C" void BorgAnimLoadScene(uint8_t* rdram, recomp_context* ctx);
extern "C" void BorgAnimDrawScene(uint8_t* rdram, recomp_context* ctx);
extern "C" void SetSceneColors(uint8_t* rdram, recomp_context* ctx);
extern "C" void get_memFree_2(uint8_t* rdram, recomp_context* ctx);
extern "C" void AllocFreeQueueItem(uint8_t* rdram, recomp_context* ctx);
extern "C" void loadBorg9(uint8_t* rdram, recomp_context* ctx);
extern "C" void remove_borg_9(uint8_t* rdram, recomp_context* ctx);
extern "C" void RenderVoxelScenes(uint8_t* rdram, recomp_context* ctx);

namespace {
    constexpr uint32_t kGameMemory = 15u << 20;
    constexpr int32_t kFarTable = 0x80F80000;     // RDRAM above the game's 15 MB, above the branding pictures
    constexpr int32_t kQueueA = 0x800D96B0;       // gGlobals.QueueA, the free queue
    constexpr int32_t kMapIndices = 0x800DCBD0;   // gLoadedMapIndecies[22][30][3]: borg5, borg9, borg5 ALT
    constexpr int32_t kZoneDatMtx = 0x800D8168;   // ZoneDat[3][3], 0x20 each, rows of 0x60
    constexpr int kMaxRing = 4, kSide = 2 * kMaxRing + 1;

    struct FarCell {
        int gx = 0, gy = 0;       // map grid cell
        uint16_t borg5 = 0;
        bool used = false;
    };
    FarCell far_cells[kSide * kSide];  // slot k's scene pointer is at kFarTable + 4k
    gpr far_maps[kSide * kSide];       // slot k's map (Borg9Header) for its props, near cells only (see "Far props")
    constexpr int kPropRing = 3;       // props are drawn out to their own reach x draw_distance: big trees to ~240 units
    int far_center_x = -1000, far_center_y = -1000;
    int far_radius_now = 1;

    uint32_t call_game(uint8_t* rdram, recomp_context* ctx, void (*fn)(uint8_t*, recomp_context*),
                       uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
        recomp_context saved = *ctx;  // mid-function in the game: every register is someone's
        ctx->r4 = (int32_t)a0;
        ctx->r5 = (int32_t)a1;
        ctx->r6 = (int32_t)a2;
        ctx->r7 = (int32_t)a3;
        fn(rdram, ctx);
        const uint32_t v0 = (uint32_t)ctx->r2;
        *ctx = saved;
        return v0;
    }

    gpr far_slot(int k) {
        return (gpr)(kFarTable + 4 * k);
    }

    bool outdoors(uint8_t* rdram) {
        return MEM_HU(0, (gpr)(int32_t)0x800D8414) == 0  // gGlobals.gameVars.mapDatA: MAPA_Overworld
               && read_f32(rdram, (gpr)(int32_t)0x800D8404) == 100.0f;
    }

    int wanted_radius() {
        const float dd = aidyn::settings::graphics().draw_distance;
        if (dd <= 1.0f) {
            return 1;
        }
        return std::clamp((int)std::ceil(180.0f * dd / 100.0f), 2, kMaxRing);
    }

    uint16_t map_index(uint8_t* rdram, int gx, int gy, int k) {
        if (gx < 1 || gx > 21 || gy < 1 || gy > 29) {
            return 0;
        }
        return MEM_HU(((gx * 30 + gy) * 3 + k) * 2, (gpr)kMapIndices);
    }

    void free_far_map(uint8_t* rdram, recomp_context* ctx, int k);  // "Far props", below

    void free_far_cell(uint8_t* rdram, recomp_context* ctx, int k) {
        if (!far_cells[k].used) {
            return;
        }
        far_cells[k].used = false;
        free_far_map(rdram, ctx, k);
        const uint32_t scene = (uint32_t)MEM_W(0, far_slot(k));
        if (scene == 0) {
            return;
        }
        // Through the game's free queue, which frees at the start of the next frame (the renderer may still be
        // reading this frame's display list) and clears the slot now.
        call_game(rdram, ctx, AllocFreeQueueItem, (uint32_t)kQueueA, (uint32_t)far_slot(k), 1 /* QueueType_Scene */, 0);
    }

    void place_far_cell(uint8_t* rdram, int k, int dx, int dz) {
        const gpr scene = (gpr)(int32_t)MEM_W(0, far_slot(k));
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        MEM_W(0x1F4, scene) = 0x10 | 0x100 | 0x8 | 0x4;  // ZoneMoveSceneDat: SetFlag10, SetFogFlag, SetFlag8, SetFlag4
        for (int m : { 0x88, 0xC8 }) {                    // matrixA, matrixB = identity
            for (int i = 0; i < 16; i++) {
                write_f32(rdram, scene + m + i * 4, (i % 5 == 0) ? 1.0f : 0.0f);
            }
        }
        write_f32(rdram, scene + 0x88 + 48, dx * cx);     // MatrixASetPos: matrixA[3][0..2]
        write_f32(rdram, scene + 0x88 + 56, dz * cz);
    }

    // In view: some corner of the cell within the picture's width (plus a margin) and the far clip.
    bool far_cell_visible(uint8_t* rdram, int dx, int dz, float clip) {
        const gpr g = (gpr)kGlobals;
        const float px = read_f32(rdram, g + 0x214), pz = read_f32(rdram, g + 0x21C);
        float fx = read_f32(rdram, g + 0x220) - px, fz = read_f32(rdram, g + 0x228) - pz;
        const float len = std::sqrt(fx * fx + fz * fz);
        if (len < 1e-4f) {
            return true;
        }
        fx /= len;
        fz /= len;
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        // The picture's corners reach wider than its horizontal half-angle when the camera is tilted (up or down): a
        // corner ray (tan h, tan v, 1) pitched by p has horizontal angle atan(tan h / (cos p - sin |p| tan v)); past
        // straight down (the denominator 0) it covers every direction. The field's vertical half-angle is 22.5.
        const float dy = read_f32(rdram, g + 0x224) - read_f32(rdram, g + 0x218);
        const float pitch = std::atan2(std::fabs(dy), len);
        const float denom = std::cos(pitch) - std::sin(pitch) * std::tan(22.5f * kPi / 180.0f);
        if (denom <= 0.05f) {
            return true;
        }
        const float half = std::atan2(std::tan(half_view_degrees() * kPi / 180.0f), denom);
        const float cosv = std::cos(std::min(half + 15.0f * kPi / 180.0f, kPi));
        for (int i = 0; i <= 2; i++) {
            for (int j = 0; j <= 2; j++) {  // corners and edge midpoints
                const float x = (dx + i * 0.5f) * cx - px, z = (dz + j * 0.5f) * cz - pz;
                const float d = std::sqrt(x * x + z * z);
                if (d < 1.0f) {
                    return true;
                }
                if ((x * fx + z * fz) / d >= cosv && d * ((x * fx + z * fz) / d) <= clip + 10.0f) {
                    return true;
                }
            }
        }
        return false;
    }

}

namespace {
    int far_ring_radius() {
        return far_radius_now;
    }
}

// Far props. The trees, rocks, buildings and chests' models of a map cell (its Borg9 map's scene objects) are drawn by
// RenderVoxelScenes only for the game's 3x3 block; the far cells above are ground only. With draw_distance above 1,
// props reach past the 3x3 (to about 40 x draw_distance), so whole groups of trees popped in each time the camera's
// cell changed and the next cells' maps arrived (the title flyover's valley before the castle, spitzdano). So the cells
// within kPropRing of the centre get their map too, loaded with loadBorg9 alone (no collision, wanderers, sounds or
// containers: loading_map_data's other steps), their scene objects marked active as VoxelObjects_Activate would with
// the Expansion Pak (all but VOXEL_JumperPak ones; the voxel chart's few changed objects aren't looked up), and drawn
// with the game's RenderVoxelScenes, as it draws its own neighbours: from the camera's place in that cell's
// coordinates, with the collision section the camera is in or the edge nearest it (its lists say which objects to
// consider) and the cell's offset. A cell in the 3x3 keeps its own copy only while the game has none (a neighbour the
// centre hides); otherwise the game draws it and ours is freed. Freeing a map frees its props' models through the
// game's free queue first (they may be in this frame's display list).
namespace {
    bool heap_pointer(gpr p);  // defined further down
    uint32_t f32_bits(float f);
    uint32_t call_game_stack(uint8_t* rdram, recomp_context* ctx, void (*fn)(uint8_t*, recomp_context*), uint32_t a0,
                             uint32_t a1, uint32_t a2, uint32_t a3, std::initializer_list<uint32_t> stack);

    // voxelObject: 0x6C bytes (RenderVoxelScenes indexes them x 108), its header 0x28 (Bitfeild +0x14, type +0x16),
    // then a scene's three models (renderProx, borgIndex, scene / Borg7 pointer: 12 bytes each), BorgCount, sceneflags.
    constexpr int kVoxelSize = 0x6C, kVoxelBits = 0x14, kVoxelType = 0x16, kSceneModels = 0x28, kSceneCount = 0x68,
                  kSceneFlags = 0x6A;
    constexpr int kB9VoxelCount = 0x28, kB9Voxels = 0x44;  // Borg9Data, at Borg9Header +8

    // The game's free queue holds 256 entries (QueueA, its count at +0x800; more is a crash), and a map's props can
    // need hundreds: maps to free wait in far_maps_pending and are freed a few a frame, keeping the queue under 200.
    std::vector<gpr> far_maps_pending;

    int far_map_models(uint8_t* rdram, gpr map, bool queue, recomp_context* ctx) {
        const gpr dat = map + 8, voxels = MEM_W(kB9Voxels, dat);
        const int count = MEM_HU(kB9VoxelCount, dat);
        int n = 0;
        for (int i = 0; heap_pointer(voxels) && i < count; i++) {
            const gpr v = voxels + kVoxelSize * i;
            if (MEM_HU(kVoxelType, v) != 0) {  // VOXEL_Scene
                continue;
            }
            const uint32_t type = (MEM_HU(kSceneFlags, v) & 2) ? 0 /* QueueType_Borg7 */ : 1 /* QueueType_Scene */;
            for (int j = 0; j < 3 && j < MEM_HU(kSceneCount, v); j++) {
                const gpr slot = v + kSceneModels + 12 * j + 8;
                if (MEM_W(0, slot) != 0) {
                    n++;
                    if (queue) {
                        call_game(rdram, ctx, AllocFreeQueueItem, (uint32_t)kQueueA, (uint32_t)slot, type, 0);
                    }
                }
            }
        }
        return n;
    }

    void free_far_map(uint8_t* rdram, recomp_context* ctx, int k) {
        if (far_maps[k] != 0) {
            far_maps_pending.push_back(far_maps[k]);
            far_maps[k] = 0;
        }
    }

    void free_pending_far_maps(uint8_t* rdram, recomp_context* ctx) {
        while (!far_maps_pending.empty()) {
            const gpr map = far_maps_pending.back();
            const int used = MEM_HU(0x800, (gpr)kQueueA);
            if (used + far_map_models(rdram, map, false, ctx) > 200) {
                if (used > 0) {
                    return;  // next frame, once the queue has emptied
                }
            }
            far_map_models(rdram, map, true, ctx);  // (a map with more than 200 models alone still goes: never seen)
            call_game(rdram, ctx, remove_borg_9, (uint32_t)map);
            far_maps_pending.pop_back();
        }
    }

    gpr load_far_map(uint8_t* rdram, recomp_context* ctx, uint16_t borg9) {
        const gpr map = (gpr)(int32_t)call_game(rdram, ctx, loadBorg9, borg9);
        if (!heap_pointer(map)) {
            return 0;
        }
        const gpr dat = map + 8, voxels = MEM_W(kB9Voxels, dat);
        const int count = MEM_HU(kB9VoxelCount, dat);
        for (int i = 0; heap_pointer(voxels) && i < count; i++) {
            const gpr v = voxels + kVoxelSize * i;
            uint16_t bits = MEM_HU(kVoxelBits, v);
            // a scene, not "only without the pak", and not a pathlight: one turned off here would be noted in the
            // voxel chart against the map the party is on (RenderVoxelScenes uses the current map's ids)
            const bool active = MEM_HU(kVoxelType, v) == 0 && !(bits & 0x20) && !pathlight(rdram, v);
            bits = active ? (bits | 0x8000) : (bits & ~0x8000);
            MEM_H(kVoxelBits, v) = (int16_t)bits;
        }
        return map;
    }

    // Whether the game has its own map in this 3x3 place.
    bool game_has_map(uint8_t* rdram, int dx, int dz) {
        return MEM_W(0xC, (gpr)(kZoneDatMtx + (dx + 1) * 0x60 + (dz + 1) * 0x20)) != 0;
    }

    bool wants_props(uint8_t* rdram, int dx, int dz) {
        if (std::abs(dx) > kPropRing || std::abs(dz) > kPropRing || (dx == 0 && dz == 0)) {
            return false;
        }
        return std::abs(dx) > 1 || std::abs(dz) > 1 || !game_has_map(rdram, dx, dz);
    }

    gpr draw_far_props(uint8_t* rdram, recomp_context* ctx, gpr g, int k, int dx, int dz, gpr cam) {
        const gpr dat = far_maps[k] + 8;
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        const float ox = -dx * cx, oz = -dz * cz;  // the camera's place in that cell = its place here + (ox, oz)
        const float fx = read_f32(rdram, dat + 0xC), fz = read_f32(rdram, dat + 0x10);
        const float lx = (read_f32(rdram, cam) + ox - read_f32(rdram, dat)) / std::max(fx, 1.0f);
        const float lz = (read_f32(rdram, cam + 8) + oz - read_f32(rdram, dat + 8)) / std::max(fz, 1.0f);
        const int sx = (int)std::clamp(std::floor(lx), -1000.0f, 1000.0f), sz = (int)std::clamp(std::floor(lz), -1000.0f, 1000.0f);
        return (gpr)(int32_t)call_game_stack(rdram, ctx, RenderVoxelScenes, (uint32_t)g, (uint32_t)dat, (uint32_t)cam,
                                             (uint32_t)(int32_t)sx, { (uint32_t)(int32_t)sz, f32_bits(ox), f32_bits(oz) });
    }
}

// Every prop considered. A map's props are listed per collision section (about 5 x 5 units, some 20 x 20 to a cell):
// each section lists the scene objects to consider from there (20 to 40 of a cell's 30 to 80), made for the N64's
// short view. With draw_distance above 1 trees beyond those lists were in range, and whole groups appeared the moment
// the camera crossed into the next section (the title flyover, spitzdano). Outdoors, with draw_distance above 1, every
// RenderVoxelScenes (the game's own cells' and the far cells') is handed a section listing all of the map's scene
// objects instead (aidyn.us.toml hook right after its getCollideSection): each object still checks its own distance
// and view, and is drawn once a frame (its timestamp). The lists are made each frame in RDRAM above the game's 15 MB
// (kAllPropsBase on, reset at the end of the frame).
namespace {
    constexpr uint32_t kAllPropsBase = 0x80FA0000, kAllPropsEnd = 0x81000000;
    uint32_t all_props_next = kAllPropsBase;
}

void all_props_frame_end() {
    all_props_next = kAllPropsBase;
}

extern "C" void aidyn_all_props(uint8_t* rdram, recomp_context* ctx) {
    const gpr section = ctx->r2;
    if (aidyn::settings::graphics().draw_distance <= 1.0f || !outdoors(rdram) || !heap_pointer(section)) {
        return;
    }
    const gpr dat = MEM_W(0x1DC, ctx->r29);  // RenderVoxelScenes' Borg9Data argument
    if (!heap_pointer(dat)) {
        return;
    }
    const gpr voxels = MEM_W(kB9Voxels, dat);
    const int count = MEM_HU(kB9VoxelCount, dat);
    if (!heap_pointer(voxels) || count == 0) {
        return;
    }
    const uint32_t need = 0x14 + 2 * (uint32_t)count + 4;
    if (all_props_next + need > kAllPropsEnd) {
        return;  // (never: some 20 KB a frame)
    }
    const gpr out = (gpr)(int32_t)all_props_next, list = out + 0x14;
    int n = 0;
    for (int i = 0; i < count; i++) {
        if (MEM_HU(kVoxelType, voxels + kVoxelSize * i) == 0) {  // VOXEL_Scene
            MEM_H(2 * n++, list) = (int16_t)i;
        }
    }
    for (int k = 0; k < 0x14; k += 4) {
        MEM_W(k, out) = MEM_W(k, section);
    }
    MEM_W(8, out) = (int32_t)list;     // voxelIndecies
    MEM_H(0x10, out) = (int16_t)n;     // voxelSceneCount
    all_props_next += (need + 7) & ~7u;
    ctx->r2 = out;
}

extern "C" uint32_t aidyn_game_memory(uint32_t reported) {
    return std::max(reported, kGameMemory);
}

extern "C" void aidyn_far_cells_free_all(uint8_t* rdram, recomp_context* ctx) {
    for (int k = 0; k < kSide * kSide; k++) {
        free_far_cell(rdram, ctx, k);
    }
    far_center_x = far_center_y = -1000;
    far_radius_now = 1;
}

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

// Crossing a map cell. The field's coordinates are relative to the centre cell, so when the party walks into the next
// cell the game moves everything (camera, cells, characters) by 100 units in one frame. RT64 blends each transform
// between game frames to show 100 Hz from the game's 60, and blending across that jump draws one in-between frame
// slightly out of place: a one-frame flicker each time a boundary is crossed (spitzdano). So on the frame the centre cell
// changes, the display list tells RT64 not to interpolate into it (gEXSkipInterpolation, tools/patches/
// rt64-save-pictures.patch): that frame is shown as it is. AIDYN_RECENTRE_LOG=1 prints each one.
namespace {
    gpr put_cmds(uint8_t* rdram, gpr g, const GfxCommand* cmds, int n);  // "Widescreen clouds", below

    gpr skip_interpolation_on_recentre(uint8_t* rdram, gpr g, int sx, int sy) {
        static int last_x = INT32_MIN, last_y = INT32_MIN;
        const bool moved = (last_x != INT32_MIN) && (sx != last_x || sy != last_y);
        last_x = sx;
        last_y = sy;
        if (!moved) {
            return g;
        }
        static const bool log = std::getenv("AIDYN_RECENTRE_LOG") != nullptr;
        if (log) {
            fprintf(stderr, "recentre: cell %d,%d, no interpolation into this frame\n", sx, sy);
        }
        GfxCommand c[2];
        gEXEnable(&c[0]);
        gEXSkipInterpolation(&c[1]);
        return put_cmds(rdram, g, c, 2);
    }
}

extern "C" void aidyn_far_cells(uint8_t* rdram, recomp_context* ctx) {
    free_pending_far_maps(rdram, ctx);
    const int radius = outdoors(rdram) ? wanted_radius() : 1;
    const int sx = MEM_H(0, (gpr)(int32_t)0x800D83FA), sy = MEM_H(0, (gpr)(int32_t)0x800D83FC);  // mapShort1/2
    // Free cells no longer within the radius (and the centre cell: the game always has its own).
    for (int k = 0; k < kSide * kSide; k++) {
        if (!far_cells[k].used) {
            continue;
        }
        const int dx = far_cells[k].gx - sx, dz = far_cells[k].gy - sy;
        if (std::abs(dx) > radius || std::abs(dz) > radius || (dx == 0 && dz == 0) || radius <= 1) {
            free_far_cell(rdram, ctx, k);
        }
    }
    const bool moved = sx != far_center_x || sy != far_center_y;
    far_center_x = sx;
    far_center_y = sy;
    if (radius <= 1) {
        far_radius_now = 1;
        return;
    }
    // Place the loaded ones again after the centre moved, and find the missing ones, nearest first.
    struct Want { int gx, gy, dx, dz; };
    Want missing[kSide * kSide];
    int n_missing = 0;
    bool complete = true;
    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const int gx = sx + dx, gy = sy + dz;
            const uint16_t borg5 = map_index(rdram, gx, gy, 0);
            if (borg5 == 0) {
                continue;
            }
            int found = -1;
            for (int k = 0; k < kSide * kSide; k++) {
                if (far_cells[k].used && far_cells[k].gx == gx && far_cells[k].gy == gy) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                if (moved) {
                    place_far_cell(rdram, found, dx, dz);
                }
            } else {
                missing[n_missing++] = { gx, gy, dx, dz };
                complete = false;
            }
        }
    }
    far_radius_now = complete ? radius : std::max(1, radius - 1);
    std::sort(missing, missing + n_missing, [](const Want& a, const Want& b) {
        return std::max(std::abs(a.dx), std::abs(a.dz)) * 100 + std::abs(a.dx) + std::abs(a.dz) <
               std::max(std::abs(b.dx), std::abs(b.dz)) * 100 + std::abs(b.dx) + std::abs(b.dz);
    });
    // Two a frame; more when many are missing (an area just loaded, its first frames fading in).
    const int max_loads = n_missing > 12 ? 6 : 2;
    for (int i = 0, loads = 0; i < n_missing && loads < max_loads; i++) {
        if (call_game(rdram, ctx, get_memFree_2) < 0x80000) {
            break;  // keep room for the game's own loads
        }
        int k = 0;
        while (k < kSide * kSide && far_cells[k].used) {
            k++;
        }
        if (k == kSide * kSide || MEM_W(0, far_slot(k)) != 0) {
            break;  // (a slot still waiting in the free queue)
        }
        const uint16_t borg5 = map_index(rdram, missing[i].gx, missing[i].gy, 0);
        const uint32_t scene = call_game(rdram, ctx, BorgAnimLoadScene, borg5);
        if (scene == 0) {
            break;
        }
        MEM_W(0, far_slot(k)) = (int32_t)scene;
        far_cells[k] = { missing[i].gx, missing[i].gy, borg5, true };
        place_far_cell(rdram, k, missing[i].dx, missing[i].dz);
        loads++;
    }
    // Far props: the near cells' maps, one a frame (and ours freed where the game has its own).
    bool map_loaded = false;
    for (int k = 0; k < kSide * kSide; k++) {
        if (!far_cells[k].used) {
            continue;
        }
        const int dx = far_cells[k].gx - sx, dz = far_cells[k].gy - sy;
        const bool want = wants_props(rdram, dx, dz);
        if (!want && far_maps[k] != 0) {
            free_far_map(rdram, ctx, k);
        }
        else if (want && far_maps[k] == 0 && !map_loaded && call_game(rdram, ctx, get_memFree_2) >= 0x100000) {
            const uint16_t borg9 = map_index(rdram, far_cells[k].gx, far_cells[k].gy, 1);
            if (borg9 != 0) {
                far_maps[k] = load_far_map(rdram, ctx, borg9);
                map_loaded = true;
            }
        }
    }
    // Draw the ones in view, after the game's own cells (display list pointer at sp+0x64 in RenderZones).
    const gpr sp = ctx->r29;
    uint32_t g = (uint32_t)MEM_W(0x64, sp);
    g = (uint32_t)skip_interpolation_on_recentre(rdram, (gpr)(int32_t)g, sx, sy);
    for (int k = 0; k < kSide * kSide; k++) {
        if (!far_cells[k].used || MEM_W(0, far_slot(k)) == 0) {
            continue;
        }
        const int dx = far_cells[k].gx - sx, dz = far_cells[k].gy - sy;
        if (std::abs(dx) <= 1 && std::abs(dz) <= 1 && MEM_W(4, (gpr)(kZoneDatMtx + (dx + 1) * 0x60 + (dz + 1) * 0x20)) != 0) {
            continue;  // in the 3x3 and the game has it: drawn by the game
        }
        if (!far_cell_visible(rdram, dx, dz, far_clip_now)) {
            continue;
        }
        const uint32_t scene = (uint32_t)MEM_W(0, far_slot(k));
        call_game(rdram, ctx, SetSceneColors, scene, 0xFF, 1, 0);
        g = call_game(rdram, ctx, BorgAnimDrawScene, g, scene);
    }
    // Their props (each object checks its own distance and view, as the game's do), from RenderZones' camera place
    // (its pos argument, at sp+0xB4).
    const gpr cam = MEM_W(0xB4, sp);
    for (int k = 0; heap_pointer(cam) && k < kSide * kSide; k++) {
        if (!far_cells[k].used || far_maps[k] == 0) {
            continue;
        }
        const int dx = far_cells[k].gx - sx, dz = far_cells[k].gy - sy;
        if (wants_props(rdram, dx, dz)) {  // (each object checks its own distance and view)
            g = (uint32_t)draw_far_props(rdram, ctx, (gpr)(int32_t)g, k, dx, dz, cam);
        }
    }
    MEM_W(0x64, sp) = (int32_t)g;
}

// Widescreen clouds. Clouds::Draw positions each cloud (a 2D image) from the 3D camera, so in 16:9 some belong outside
// the 4:3 picture, but the game's scissor (0..320) and gSPScisTextureRectangle's clamp at x = 0 cut them off at the
// 4:3 edges. Three aidyn.us.toml hooks: on entry to the outdoor branch, aidyn_clouds_begin writes RT64 extended-GBI
// commands into the display list (scissor to the full window; every rectangle moved left by kCloudShift);
// before each cloud's Borg8_DrawSimple, aidyn_cloud_x moves the cloud right by kCloudShift (so its left edge is never
// below 0 and nothing gets clamped); on exit, aidyn_clouds_end restores the scissor and the rectangle offset.
// (F3DEX_GBI_2 and rt64_extended_gbi.h are included above, at "Crossing a map cell".)
namespace {
    // Texels. At 21:9 a cloud (up to 173 wide) can start ~293 left of the 4:3 edge; rectangle x stays under 1024 (12-bit
    // 10.2 coordinates), which leaves room up to 639 on the right (4:3 ends at 320).
    constexpr int kCloudShift = 384;
    bool clouds_open = false;

    gpr put_cmds(uint8_t* rdram, gpr g, const GfxCommand* cmds, int n) {
        for (int i = 0; i < n; i++) {
            MEM_W(0, g) = (int32_t)cmds[i].values.word0;
            MEM_W(4, g) = (int32_t)cmds[i].values.word1;
            g += 8;
        }
        return g;
    }
}

extern "C" gpr aidyn_clouds_begin(uint8_t* rdram, gpr g) {
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Original) {
        return g;
    }
    GfxCommand c[5];
    gEXEnable(&c[0]);
    gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
    gEXSetRectAlign(&c[3], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -kCloudShift * 4, 0, -kCloudShift * 4, 0);
    clouds_open = true;
    return put_cmds(rdram, g, c, 5);
}

extern "C" void aidyn_cloud_x(recomp_context* ctx) {
    if (!clouds_open) {
        return;
    }
    uint32_t bits = (uint32_t)ctx->r6;  // Borg8_DrawSimple's x (a float, passed in a2)
    float x;
    std::memcpy(&x, &bits, sizeof(x));
    x += kCloudShift;
    std::memcpy(&bits, &x, sizeof(x));
    ctx->r6 = (gpr)(int32_t)bits;
}

extern "C" gpr aidyn_clouds_end(uint8_t* rdram, gpr g) {
    if (!clouds_open) {
        return g;
    }
    clouds_open = false;
    const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager);
    const uint32_t v = MEM_HU(kVres1, (gpr)gGfxManager);
    GfxCommand c[3];
    gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
    c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
    return put_cmds(rdram, g, c, 3);
}

// Font::GetButtonColor(u8 button, u8* r, u8* g, u8* b) (func_800AA2B0, ignored in aidyn.us.toml): the tint of the
// controller-button icons in text (A blue, B green, C yellow, Start red, the rest grey; table D_800E4B2C, 3 bytes
// each). With the HD UI pack the icons are drawn as full-colour Xbox buttons (tools/hdui/buttons.py), so the tint is
// white; without it, the game's colours.
#include <algorithm>

extern "C" void func_800AA2B0(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t button = (uint32_t)ctx->r4 & 0xFF;
    const auto& packs = aidyn::settings::graphics().texture_packs;
    const bool xbox = std::find(packs.begin(), packs.end(), "hd-ui") != packs.end();
    for (int i = 0; i < 3; i++) {
        const gpr out = i == 0 ? ctx->r5 : (i == 1 ? ctx->r6 : ctx->r7);
        MEM_B(0, out) = xbox ? 0xFF : MEM_BU(button * 3 + i, (gpr)(int32_t)0x800E4B2C);
    }
}

// Frame interpolation. Above 60 frames a second (settings.json fps_limit) RT64 draws in-between frames by pairing each
// matrix of one game frame with one of the next and interpolating. Left to itself it pairs them by guesswork, and an
// occasional wrong pair stretched a polygon across the screen for a frame. Every model node the game draws goes through
// BorgAnimDrawSceneRaw, which loads the node's world matrix with gSPMatrix; aidyn.us.toml hooks it so each node's
// matrix is tagged with an RT64 matrix group whose ID is the scene and node (stable while the model is loaded), matched
// exactly between frames. On entry the extended GBI is switched on (RT64 switches it off after every frame); before
// each node's matrix the group (2 commands, the display list has room: it peaks around 6400 of its 25600); on return,
// RT64's automatic group again for whatever is drawn after.
namespace {
    bool interpolating() {
        const int limit = aidyn::settings::graphics().fps_limit;
        return limit == 0 || limit > 60;
    }
}

bool scene_intact(uint8_t* rdram, gpr scene);  // "Heap trace", below
void skinned_draw(uint8_t* rdram, gpr scene);   // "Skinned models", below

// Returns false to skip the model (the hook returns the display list unchanged): one whose memory was overwritten
// (its Borg5 pointer no longer one), which the function would read through and crash on (see "Heap trace").
extern "C" int aidyn_scene_draw_begin(uint8_t* rdram, recomp_context* ctx) {
    if (!scene_intact(rdram, ctx->r5)) {
        return 0;
    }
    skinned_draw(rdram, ctx->r5);
    if (!interpolating()) {
        return 1;
    }
    GfxCommand c[1];
    gEXEnable(&c[0]);
    ctx->r4 = put_cmds(rdram, ctx->r4, c, 1);  // a0 = the display list pointer, before the function copies it
    return 1;
}

extern "C" void aidyn_scene_node_matrix(uint8_t* rdram, recomp_context* ctx) {
    if (!interpolating()) {
        return;
    }
    const uint32_t scene = (uint32_t)MEM_W(0, (gpr)(int32_t)0x800E6A40);  // the scene being drawn
    const uint32_t node = (uint32_t)MEM_W(8, (gpr)(int32_t)0x800E6A40);   // its current node
    uint32_t id = (scene * 0x9E3779B1u) ^ node;
    if (id == G_EX_ID_IGNORE || id == G_EX_ID_AUTO) {
        id = 1;
    }
    GfxCommand c[2];
    gEXMatrixGroupDecomposed(&c[0], id, G_EX_NOPUSH, 0, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_AUTO, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_AUTO);
    // s4 = where the gSPMatrix goes, s7 = s4 + 8 (the pointer after it): both move past the group.
    ctx->r20 = put_cmds(rdram, ctx->r20, c, 2);
    ctx->r23 += 16;
}

extern "C" void aidyn_scene_draw_end(uint8_t* rdram, recomp_context* ctx) {
    if (!interpolating()) {
        return;
    }
    // RT64's default group (rt64_transform_group.h): automatic ID, decomposed, components automatic.
    GfxCommand c[2];
    gEXMatrixGroup(&c[0], G_EX_ID_AUTO, G_EX_INTERPOLATE_DECOMPOSE, G_EX_NOPUSH, 0, G_EX_COMPONENT_AUTO,
                   G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                   G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO, G_EX_ORDER_AUTO, G_EX_EDIT_NONE, G_EX_ASPECT_AUTO,
                   G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
    ctx->r2 = put_cmds(rdram, ctx->r2, c, 2);  // v0 = the display list pointer returned
}

// Animation loops. func_80096570 advances a model's animation one tick (1/60 s): Actor animations call it once per
// tick. When an animation ends it picks the next one (a walk cycle picks itself again: takeBranch 0x800962B4, set up by
// 0x80096A38) and skips Scene::Tick for that tick, so the last pose is shown twice; and the root motion that moves the
// character (+0x2C..+0x34, root position now minus last tick's +0x20) is zeroed that tick and, through the counter
// +0x12, the next two. On the N64, at 15-20 frames a second, that vanished inside one frame; at 60 a walking character
// stopped dead for 50 ms every stride. With settings.json's smooth_animation_loops (default), aidyn.us.toml hooks:
// after the new animation is set up, Scene::Tick runs in the same tick (aidyn_anim_loop_tick); then the motion keeps
// last tick's value instead of being zeroed, with no countdown (aidyn_anim_loop_motion, which skips to the end where
// the new root position is stored).
// AIDYN_ANIM_LOG=1 prints each animation tick's switch flag and z motion, for measuring.
extern "C" void Scene__Tick(uint8_t* rdram, recomp_context* ctx);  // the recompiled Scene::Tick (0x80097098)

// Ticking the new animation at once skips its first frame (aniTime 1) for everything that reads the frame number.
// ProcessPlayers notes which animation an actor is playing for its sounds only on frame 1 (pDat->unk16 = ani_type), and
// player_audiokey plays footsteps, swings, hits and spell sounds on given frames of that animation, so without frame 1
// every sound keyed to an animation was silent. early_switch remembers the models ticked early (Borg7 -> aniTime after
// the tick); aidyn_anim_first_frame (ProcessPlayers' aniTime == 1 check) and aidyn_anim_first_frame_sound (its
// player_audiokey call) give the actor its frame 1 back: the animation is noted, and frame 1's sounds play just before
// frame 2's, in the same tick.
namespace {
    std::unordered_map<uint32_t, uint16_t> early_switch;
    uint32_t first_frame_actor = 0;
}

extern "C" void aidyn_anim_loop_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!aidyn::settings::graphics().smooth_animation_loops) {
        return;
    }
    const gpr borg7 = ctx->r17;
    ctx->r4 = MEM_W(8, borg7);  // the model's scene
    Scene__Tick(rdram, ctx);
    early_switch[(uint32_t)borg7] = MEM_HU(0x1F8, MEM_W(8, borg7));  // SceneData.aniTime
}

// ProcessPlayers before 0x800132A4 (bne a0, v0): s1 = the actor, a0 = its aniTime, v0 = 1. Returns the aniTime to test.
extern "C" gpr aidyn_anim_first_frame(uint8_t* rdram, gpr actor, gpr ani_time) {
    const uint32_t borg7 = (uint32_t)MEM_W(4, actor);
    auto it = early_switch.find(borg7);
    if (it == early_switch.end()) {
        return ani_time;
    }
    const bool now = it->second == (uint16_t)ani_time;
    early_switch.erase(it);
    if (!now) {
        return ani_time;
    }
    first_frame_actor = (uint32_t)actor;
    return 1;
}

extern "C" void player_audiokey(uint8_t* rdram, recomp_context* ctx);  // the recompiled player_audiokey (0x80053560)

// ProcessPlayers before 0x8001341C (jal player_audiokey; its delay slot sets a0 = s1): a1 animation, a2 frame, a3 length.
extern "C" void aidyn_anim_first_frame_sound(uint8_t* rdram, recomp_context* ctx) {
    if (first_frame_actor == 0) {
        return;
    }
    const bool mine = first_frame_actor == (uint32_t)ctx->r17;
    first_frame_actor = 0;
    if (!mine || (uint32_t)(ctx->r6 & 0xFFFF) <= 1) {
        return;
    }
    recomp_context saved = *ctx;
    ctx->r4 = ctx->r17;
    ctx->r6 = 1;
    player_audiokey(rdram, ctx);
    *ctx = saved;
}

extern "C" int aidyn_anim_loop_motion(uint8_t* rdram, gpr anim, int branched) {
    if (!aidyn::settings::graphics().smooth_animation_loops || !branched) {
        return 0;
    }
    MEM_H(0x12, anim) = 0;  // no countdown; aidyn_anim_loop_speed puts last tick's motion back
    return 1;
}

// The two fixes move a walking character on 2 more ticks a cycle and make the cycle a tick shorter (18 ticks for the
// walk, not 19), about 18% faster. To keep the original speed, each animation instance measures its last cycle (a cycle
// ends when it switches animation): D = the motion the fixes give over its n ticks, x = what they added (the switch tick
// and the one after, which the original zeroed), and scales every tick's motion by the original speed over the new one:
// ((D - x) / (n + 1)) / (D / n). Runs at the end of every tick (aidyn.us.toml hook at 0x8009676C).
namespace {
    struct LoopState {
        float raw[3] = { 0.0f, 0.0f, 0.0f };  // last tick's motion before scaling
        float scale = 1.0f;
        float distance = 0.0f, added = 0.0f;
        int ticks = 0;
    };
    std::unordered_map<uint32_t, LoopState> loop_states;
}

extern "C" void aidyn_anim_loop_speed(uint8_t* rdram, gpr anim, int branched) {
    static const bool log = std::getenv("AIDYN_ANIM_LOG") != nullptr;
    if (aidyn::settings::graphics().smooth_animation_loops) {
        LoopState& st = loop_states[(uint32_t)anim];
        float raw[3];
        for (int i = 0; i < 3; i++) {
            raw[i] = branched ? st.raw[i] : read_f32(rdram, anim + 0x2C + i * 4);
        }
        if (branched) {
            if (st.ticks > 2 && st.distance > 0.05f) {
                const float n = (float)st.ticks;
                st.scale = std::clamp((st.distance - st.added) / (n + 1.0f) * n / st.distance, 0.7f, 1.0f);
            }
            st.distance = st.added = 0.0f;
            st.ticks = 0;
        }
        const float step = std::sqrt(raw[0] * raw[0] + raw[2] * raw[2]);
        st.distance += step;
        if (st.ticks < 2) {
            st.added += step;
        }
        st.ticks++;
        for (int i = 0; i < 3; i++) {
            st.raw[i] = raw[i];
            write_f32(rdram, anim + 0x2C + i * 4, raw[i] * st.scale);
        }
    }
    if (log) {
        fprintf(stdout, "anim %08X branch %d countdown %d motion z %.4f\n", (uint32_t)anim, branched,
                (int)MEM_H(0x12, anim), read_f32(rdram, anim + 0x34));
    }
}

// Startup logos. load_splashscreen_data (TitleSplash::Load) starts the boot sequence at state 5 (gGlobals.
// titleSplashVars.state, 0x800D80A8): Expansion Pak notice, copyright, Nintendo logo, THQ, H2O, then 0 = done, on to
// the title screen. With settings.json game.skip_intro (default) an aidyn.us.toml hook at its return sets 0.
//
// The intro music starts with the logos (load_intro_music) and plays on into the title screen, so with the logos
// skipped the title screen opened on the part written for them. It is moved on to where it was when the title screen
// used to appear (aidyn_dcm_tick).
namespace {
    std::atomic_bool intro_seek{ false };
}

extern "C" void aidyn_splash_loaded(uint8_t* rdram) {
    if (aidyn::settings::game().skip_intro) {
        MEM_B(0, (gpr)(int32_t)0x800D80A8) = 0;
        intro_seek = true;
    }
}

// The game's music player (decomp dcm.cpp, DCM::VoiceHandler 0x80090C94, run by the audio thread once per tick) plays
// up to 32 tracks (gDCMManager 0x800E41F0: DCM_struct* at +0x14, 0x20 each: channel states +0x00, Borg12Sub* +0x04,
// end tick +0x08, ticks played +0x0C, read offset +0x10, id +0x14, active +0x18 (2: playing), tempo divider +0x19 and
// its count +0x1A, loops +0x1D). A track is one command stream for all its channels (Borg12Sub: channel count low byte
// at +0x03, length +0x08, loop offset +0x0C, data +0x14); each channel (0x10 bytes: voice id +0x00, unk4 +0x04, pitch
// +0x08, voice +0x0A, instrument +0x0B, volume +0x0C, pan +0x0D, wait +0x0E) reads its next command when its wait is
// 0. DcmTrack is a copy of one track's state, and tick() is one handler call on it with the sound left out: reading
// the stream ahead moves the music on without playing it.
namespace {
    constexpr uint32_t kDcmManager = 0x800E41F0;
    constexpr uint32_t kIntroIndex = 0x800D80A0, kIntroId = 0x800D80A4, kSplashState = 0x800D80A8;
    // Ticks of the intro music played before the title screen appears when the logos run: measured with
    // AIDYN_INTRO_LOG=1 and game.skip_intro false.
    constexpr uint32_t kIntroTitleTick = 12947;

    struct DcmTrack {
        struct Channel {
            uint32_t unk4 = 0;
            uint16_t pitch = 0;
            uint8_t instrument = 0, volume = 0, pan = 0, wait = 0;
        };
        uint32_t played = 0, offset = 0;
        uint8_t divider = 0, count = 0;
        Channel ch[32];
        bool ended = false;

        static gpr address(uint8_t* rdram, uint32_t index) {
            return (gpr)(int32_t)((uint32_t)MEM_W(0x14, (gpr)(int32_t)kDcmManager) + index * 0x20);
        }
        void load(uint8_t* rdram, gpr t) {
            played = (uint32_t)MEM_W(0xC, t);
            offset = (uint32_t)MEM_W(0x10, t);
            divider = MEM_BU(0x19, t);
            count = MEM_BU(0x1A, t);
            const gpr c = MEM_W(0, t);
            for (uint32_t i = 0; i < 32; i++) {
                Channel& k = ch[i];
                k.unk4 = (uint32_t)MEM_W(4 + i * 0x10, c);
                k.pitch = (uint16_t)MEM_HU(8 + i * 0x10, c);
                k.instrument = MEM_BU(0xB + i * 0x10, c);
                k.volume = MEM_BU(0xC + i * 0x10, c);
                k.pan = MEM_BU(0xD + i * 0x10, c);
                k.wait = MEM_BU(0xE + i * 0x10, c);
            }
        }
        void store(uint8_t* rdram, gpr t) const {
            MEM_W(0xC, t) = (int32_t)played;
            MEM_W(0x10, t) = (int32_t)offset;
            MEM_B(0x19, t) = divider;
            MEM_B(0x1A, t) = count;
            const gpr c = MEM_W(0, t);
            for (uint32_t i = 0; i < 32; i++) {
                const Channel& k = ch[i];
                MEM_W(4 + i * 0x10, c) = (int32_t)k.unk4;
                MEM_H(8 + i * 0x10, c) = (int16_t)k.pitch;
                MEM_B(0xB + i * 0x10, c) = k.instrument;
                MEM_B(0xC + i * 0x10, c) = k.volume;
                MEM_B(0xD + i * 0x10, c) = k.pan;
                MEM_B(0xE + i * 0x10, c) = k.wait;
            }
        }
        bool same(const DcmTrack& o, uint32_t channels) const {
            if (played != o.played || offset != o.offset || divider != o.divider || count != o.count) {
                return false;
            }
            for (uint32_t i = 0; i < channels; i++) {
                const Channel &a = ch[i], &b = o.ch[i];
                if (a.unk4 != b.unk4 || a.pitch != b.pitch || a.instrument != b.instrument || a.volume != b.volume
                    || a.pan != b.pan || a.wait != b.wait) {
                    return false;
                }
            }
            return true;
        }
        void tick(uint8_t* rdram, gpr sub, uint8_t loops) {
            const uint32_t channels = MEM_BU(3, sub) & 31, length = (uint32_t)MEM_W(8, sub);
            const gpr data = MEM_W(0x14, sub);
            auto byte = [&]() { return (uint8_t)MEM_BU(offset++, data); };
            if (count < divider) {
                count++;
            }
            else {
                for (uint32_t i = 0; i < channels; i++) {
                    Channel& k = ch[i];
                    if (k.wait != 0) {
                        k.wait--;
                        continue;
                    }
                    const uint8_t cmd = byte();
                    if (cmd & 0x80) {
                        k.wait = (cmd & 0x7F) + 1;
                        continue;
                    }
                    if (cmd & 0x40) {
                        const uint16_t lo = byte(), hi = byte(), raw = lo | (hi << 8), shift = raw >> 14;
                        static constexpr uint16_t base[4] = { 0, 0x4000, 0xC000, 0xC000 };
                        k.pitch = (uint16_t)(base[shift] + ((raw & 0x3FFF) << shift));
                    }
                    if (cmd & 0x20) {
                        k.volume = (uint8_t)std::sqrt((float)byte());
                    }
                    if (cmd & 0x10) {
                        k.pan = byte();
                    }
                    if (cmd & 0x08) {
                        k.instrument = byte();
                    }
                    k.unk4 = (cmd & 0x04) ? (uint32_t)byte() << 8 : 0;
                    if (cmd & 0x01) {
                        const uint8_t sub_cmd = byte();
                        if (sub_cmd & 0x40) {
                            uint8_t t = byte();
                            if (t != 0) {
                                t = (uint8_t)(125000u / ((uint32_t)t * 50u)) - 1;
                            }
                            divider = t;
                        }
                        if (sub_cmd & 0x20) {
                            offset++;
                        }
                    }
                }
                if (offset >= length) {
                    if (loops == 0) {
                        ended = true;
                    }
                    offset = (uint32_t)MEM_W(0xC, sub);
                    for (Channel& k : ch) {
                        k.wait = 0;
                    }
                }
                count = 0;
            }
            played++;
        }
    };
    DcmTrack intro_start;  // AIDYN_INTRO_LOG: the intro music's state on its first tick
    bool intro_start_seen = false, intro_logged = false;
}

extern "C" void func_8008FAB0(uint8_t* rdram, recomp_context* ctx);  // the recompiled DCM::StopVoice(voice, id)

extern "C" void aidyn_music_tick(uint8_t* rdram, recomp_context* ctx);  // src/game/music_log.cpp

// DCM::VoiceHandler's entry (audio thread).
extern "C" void aidyn_dcm_tick(uint8_t* rdram, recomp_context* ctx) {
    aidyn_music_tick(rdram, ctx);
    static const bool log = std::getenv("AIDYN_INTRO_LOG") != nullptr;
    if (!log && !intro_seek) {
        return;
    }
    const uint32_t index = (uint32_t)MEM_W(0, (gpr)(int32_t)kIntroIndex), id = (uint32_t)MEM_W(0, (gpr)(int32_t)kIntroId);
    if (index >= 32) {
        return;
    }
    const gpr t = DcmTrack::address(rdram, index);
    if (MEM_BU(0x18, t) != 2 || (uint32_t)MEM_W(0x14, t) != id) {
        return;  // not playing (yet)
    }
    const gpr sub = MEM_W(4, t);
    if (log) {
        if (!intro_start_seen && MEM_W(0xC, t) == 0) {
            intro_start.load(rdram, t);
            intro_start_seen = true;
        }
        if (intro_start_seen && !intro_logged && MEM_BU(0, (gpr)(int32_t)kSplashState) == 0) {
            intro_logged = true;
            DcmTrack now, sim = intro_start;
            now.load(rdram, t);
            while (sim.played < now.played) {
                sim.tick(rdram, sub, MEM_BU(0x1D, t));
            }
            fprintf(stderr, "intro music: title screen at tick %u (offset %u); reading ahead %s\n", now.played,
                    now.offset, sim.same(now, MEM_BU(3, sub) & 31) ? "matches" : "DIFFERS");
        }
    }
    if (intro_seek.exchange(false)) {
        DcmTrack track;
        track.load(rdram, t);
        while (track.played < kIntroTitleTick && !track.ended) {
            track.tick(rdram, sub, MEM_BU(0x1D, t));
        }
        track.store(rdram, t);
        const recomp_context saved = *ctx;
        const gpr channels = MEM_W(0, t);
        for (uint32_t i = 0; i < (MEM_BU(3, sub) & 31u); i++) {  // notes started before the jump
            ctx->r4 = MEM_BU(0xA + i * 0x10, channels);
            ctx->r5 = MEM_W(i * 0x10, channels);
            func_8008FAB0(rdram, ctx);
        }
        *ctx = saved;
    }
}

// Title screen. The project is Aidyn ReChronicled: "Rechronicled" goes on the title card under THE FIRST MAGE, and
// "Enhanced by SPITZDANO" at the bottom right. Both are drawn right after the card (aidyn_image_draw_done), with the
// card's colour and fade still set. The pictures (src/game/branding_images.h, generated by tools/branding/
// make_branding.py, which also makes the "rechronicled" texture pack with their full-resolution versions) are copied
// once to RDRAM above the 8 MB the game uses, and each is loaded with one RGBA16 LoadTile and drawn 1:1.
#include "branding_images.h"

namespace {
    constexpr uint32_t kBrandingBase = 0x80F00000;  // RDRAM the game never touches (it is told it has 15 MB, "Far map cells")
    constexpr uint32_t kBrandingEnd = 0x80F80000;   // where the small fixed slots start (far cells, quiet sample, page entry)
    gpr branding_address[std::size(aidyn::branding::kImages)];

    void copy_branding(uint8_t* rdram) {
        static bool done = false;
        if (done) {
            return;
        }
        done = true;
        uint32_t address = kBrandingBase;
        for (size_t i = 0; i < std::size(aidyn::branding::kImages); i++) {
            const auto& img = aidyn::branding::kImages[i];
            if (address + (uint32_t)img.w * img.h * 2 > kBrandingEnd) {
                fprintf(stderr, "The branding pictures don't fit below the fixed slots (patches.cpp kBrandingEnd)\n");
                std::abort();
            }
            branding_address[i] = (gpr)(int32_t)address;
            for (uint32_t k = 0; k < (uint32_t)img.w * img.h; k++) {
                MEM_H(k * 2, (gpr)(int32_t)address) = img.rgba16[k];
            }
            address += ((uint32_t)img.w * img.h * 2 + 7) & ~7u;
        }
    }

    // The image's texel (0, 0) at pixel (x, y), sx and sy pixels a texel.
    gpr draw_rgba16(uint8_t* rdram, gpr g, size_t index, float x, float y, float sx, float sy) {
        const auto& img = aidyn::branding::kImages[index];
        const uint32_t w = img.w, h = img.h, line = (w * 2 + 7) / 8;
        auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
        const uint32_t clamp = (2u << 18) | (2u << 8);                                  // cmt, cms = G_TX_CLAMP
        cmd(0xE7000000, 0);                                                              // gDPPipeSync
        cmd(0xFD000000 | (0u << 21) | (2u << 19) | (w - 1), (uint32_t)branding_address[index]); // SetTextureImage RGBA16
        cmd(0xF5000000 | (0u << 21) | (2u << 19) | (line << 9), (7u << 24) | clamp);      // SetTile G_TX_LOADTILE
        cmd(0xE6000000, 0);                                                              // gDPLoadSync
        cmd(0xF4000000, (7u << 24) | (((w - 1) << 2) << 12) | ((h - 1) << 2));          // LoadTile
        cmd(0xE7000000, 0);                                                              // gDPPipeSync
        cmd(0xF5000000 | (0u << 21) | (2u << 19) | (line << 9), (0u << 24) | clamp);      // SetTile G_TX_RENDERTILE
        cmd(0xF2000000, (0u << 24) | (((w - 1) << 2) << 12) | ((h - 1) << 2));          // SetTileSize
        const uint32_t xl = (uint32_t)std::max(0, (int)(x * 4.0f)), yl = (uint32_t)std::max(0, (int)(y * 4.0f));
        const uint32_t xh = (uint32_t)(int)((x + w * sx) * 4.0f), yh = (uint32_t)(int)((y + h * sy) * 4.0f);
        const uint32_t dsdx = (uint32_t)(int)(1024.0f / sx) & 0xFFFF, dtdy = (uint32_t)(int)(1024.0f / sy) & 0xFFFF;
        cmd(0xE4000000 | ((xh & 0xFFF) << 12) | (yh & 0xFFF), (0u << 24) | ((xl & 0xFFF) << 12) | (yl & 0xFFF));
        cmd(0xE1000000, 0);                                                              // s, t = 0
        cmd(0xF1000000, (dsdx << 16) | dtdy);
        const size_t n = std::strlen(img.name);
        if (n > 2 && img.name[n - 2] == '_' && img.name[n - 1] == 'k') {  // a keyboard twin: its slots get key caps
            g = draw_key_slots(rdram, g, index, x, y, sx, sy);
        }
        return g;
    }

    // Button prompts (a word and its button: Unequip A, Lists, Change, Back, the conversations' Continue...) are drawn at
    // 85% (spitzdano, 2026-10-28: a bit smaller all round), each kept centred on the row the full-size one filled.
    constexpr float kPromptScale = 0.85f;
    gpr draw_prompt(uint8_t* rdram, gpr g, size_t index, float x, float y, float sx, float sy) {
        const float drop = aidyn::branding::kImages[index].h * (1.0f - kPromptScale) / 2.0f;
        // At whole pixels: RT64 snaps a rectangle's top and left edges to them, and a picture starting between pixels
        // lost its top row (the button's rim, cut flat).
        return draw_rgba16(rdram, g, index, std::round(x), std::round(y + drop * sy), sx * kPromptScale, sy * kPromptScale);
    }
}

// The version, bottom left of the title screen: the one running (aidyn::settings::game().rom_patch) with a copper
// marker, and under it "Versions" with Y (N64 C-up; keyboard I): Y opens the list of versions (title_versions_panel,
// further down: Original first, then the mods), where the player picks one and the game restarts into it
// (src/main/main.cpp aidyn::switch_version). A version whose ROM isn't stored (each is the player's own, checked by its
// hash) is dimmed and says "not found". The pictures are make_branding.py's ver_*, tinted with the primitive colour
// and faded with the card.
#include "version.h"

namespace {
    uint32_t ink_width(int index);  // "Button prompts", below

    using aidyn::settings::RomPatch;
    constexpr int kVersionCount = 4;
    constexpr RomPatch kVersions[kVersionCount] = { RomPatch::None, RomPatch::AidynPatch, RomPatch::AidynPlus,
                                                    RomPatch::ActionEdition };
    constexpr const char* kVersionImages[kVersionCount] = { "ver_original", "ver_patch", "ver_plus", "ver_action" };

    // A picture by its name, from a table made once.
    int find_image(const std::string& name) {
        static const std::unordered_map<std::string, int> by_name = [] {
            std::unordered_map<std::string, int> m;
            for (size_t i = 0; i < std::size(aidyn::branding::kImages); i++) m[aidyn::branding::kImages[i].name] = (int)i;
            return m;
        }();
        const auto it = by_name.find(name);
        return it == by_name.end() ? -1 : it->second;
    }

    // The picture to show: a prompt's keyboard twin (its button icons left as blank slots, which draw_rgba16 fills with
    // key caps, "Keys on the prompts") while the keyboard or mouse was used last, else the picture itself.
    int title_image(const char* name) {
        if (aidyn::input::keyboard_last()) {
            const int twin = find_image(std::string(name) + "_k");
            if (twin >= 0) return twin;
        }
        return find_image(name);
    }

    // Stored ROMs don't change while the game runs: looked up once.
    bool version_here(int i) {
        static const std::array<bool, kVersionCount> here = [] {
            std::array<bool, kVersionCount> h{};
            for (int k = 0; k < kVersionCount; k++) h[k] = aidyn::version_available(kVersions[k]);
            return h;
        }();
        return here[i];
    }

    int running_version() {
        for (int i = 0; i < kVersionCount; i++) {
            if (kVersions[i] == aidyn::settings::game().rom_patch) return i;
        }
        return 0;
    }

    // Y on the card: opens the list (title_versions_panel, below).
    void title_versions_open();

    void title_version_switch() {
        static bool first = true, y_was = false;
        const bool y = aidyn::input::c_up_held();
        if (first) {
            first = false;  // still held from the press that got us here: wait for the next one
            y_was = y;
        }
        if (y && !y_was) {
            title_versions_open();
        }
        y_was = y;
    }

    // The two lines from (10, 194), 12 texels apart; hs, vs pixels a texel; card_rgba the card's colour (its alpha
    // fades them in with it).
    gpr version_list(uint8_t* rdram, gpr g, float hs, float vs, uint32_t card_rgba) {
        const float fade = (card_rgba & 0xFF) / 255.0f;
        auto tint = [&](uint32_t rgb, float alpha) {
            put(rdram, g, 0xFA000000, rgb << 8 | (uint32_t)std::lround(255.0f * alpha * fade));  // gDPSetPrimColor
        };
        const int run = running_version();
        const int mark = title_image("ver_mark"), label = title_image(kVersionImages[run]);
        tint(0xFFFFFF, 1.0f);
        if (mark >= 0) {
            g = draw_rgba16(rdram, g, mark, 10.0f * hs, 194.0f * vs, hs, vs);
        }
        if (label >= 0) {
            g = draw_rgba16(rdram, g, label, 22.0f * hs, 194.0f * vs, hs, vs);
        }
        tint(0xD2CDC3, 0.95f);
        const int icon = title_image("ver_y"), word = title_image("ver_versions");
        if (icon >= 0) {
            g = draw_rgba16(rdram, g, icon, 10.0f * hs, 206.0f * vs, hs, vs);
        }
        if (word >= 0) {
            g = draw_rgba16(rdram, g, word, 22.0f * hs, 206.0f * vs, hs, vs);
        }
        put(rdram, g, 0xFA000000, card_rgba);  // the card's own again
        return g;
    }
}

// The version number, bottom right above the credit: the build's (aidyn_version.h, written by tools/version.cmake:
// "v0.4.0" for a release, "v0.4.0 dev" for a build after its tag, "v0.8.2 beta.1" for a test build, "dev" without either) put together from
// make_branding.py's verno_* pictures (a character each, and the word "dev"), right-aligned at x 309 (320x240), dimmed
// and faded with the card like the version list.
#include "aidyn_version.h"

namespace {
    gpr version_number(uint8_t* rdram, gpr g, float hs, float vs, uint32_t card_rgba) {
        std::vector<int> pieces;  // picture indices, -1 for a space
        const std::string text = AIDYN_VERSION_TEXT;
        for (size_t i = 0; i < text.size(); i++) {
            if (text.compare(i, 3, "dev") == 0) {
                pieces.push_back(title_image("verno_dev"));
                i += 2;
            } else if (text.compare(i, 4, "beta") == 0) {  // a test build: "0.8.2-beta.1"
                pieces.push_back(title_image("verno_beta"));
                i += 3;
            } else if (text[i] == ' ' || text[i] == '-') {
                pieces.push_back(-1);
            } else {
                const char name[] = { 'v', 'e', 'r', 'n', 'o', '_', text[i], '\0' };
                pieces.push_back(text[i] == '.' ? title_image("verno_dot") : title_image(name));
            }
        }
        // Each picture's ink includes its dark edge and shadow (about a texel past the letter): the next one overlaps it.
        auto advance = [](int i) { return i < 0 ? 2.5f : ink_width(i) - 1.0f; };
        float width = 0.0f;
        for (int i : pieces) {
            width += advance(i);
        }
        float x = 309.0f - width;
        put(rdram, g, 0xFA000000, 0xD2CDC300u | (uint32_t)std::lround(0.85f * (card_rgba & 0xFF)));  // gDPSetPrimColor
        for (int i : pieces) {
            if (i >= 0) {
                g = draw_rgba16(rdram, g, i, x * hs, 206.0f * vs, hs, vs);
            }
            x += advance(i);
        }
        put(rdram, g, 0xFA000000, card_rgba);  // the card's own again
        return g;
    }
}

// Back to the title card. The title screen's menu (New Game, Load Game, Options) had no way back: its B handler
// (IntroMenu_BFunc, func_800440D8) only plays the back sound there, and the version list is on the card. So B in that
// menu goes back to the card, as Start came from it: aidyn.us.toml hooks the handler after the sound (0x80044134, before
// its switch on the menu's state: s0 the IntroMenu's substruct, s2 the IntroMenu), and in that state calls the game's
// IntroMenu::InitTitleCard (func_800432B4; made for the first showing, it reuses the card and its Press Start). It
// leaves the menu's dark backing panel both as the one fading out (+0x80) and as the current one (+0x84), and frees the
// first once faded: the current one is cleared. Then the menu fades out over the card fading in (alpha1 +0x7F 255, the
// menu's alpha 255), as the game's other menu changes do. The handler then returns as from its switch.
extern "C" void func_800432B4(uint8_t* rdram, recomp_context* ctx);  // IntroMenu::InitTitleCard
extern "C" void Utilities__SetAlpha(uint8_t* rdram, recomp_context* ctx);

extern "C" int aidyn_title_back(uint8_t* rdram, recomp_context* ctx) {
    const gpr sub = ctx->r16, menu = ctx->r18;
    if (MEM_BU(0x24, sub) != 1) {  // IntroM_processIntoMenu: the New Game / Load Game / Options menu
        return 0;
    }
    call_game(rdram, ctx, func_800432B4, (uint32_t)menu);
    MEM_W(0x84, menu) = 0;
    MEM_B(0x7F, menu) = (int8_t)0xFF;
    const gpr leaving = MEM_W(0x20, sub);
    if (leaving != 0) {
        call_game(rdram, ctx, Utilities__SetAlpha, (uint32_t)leaving, 0xFF);
    }
    return 1;
}

namespace aidyn {
    bool redrawn_art();            // src/main/rt64_context.cpp: false while F2 has the redrawn art pack off
    bool redrawn_art_available();  // ... and whether the pack is there at all (listed and found)
}

// The redrawn title card (art/titles, tools/branding/make_logo.py: spitzdano's redesign) has RECHRONICLED in it: on screen
// while textures are on, the art pack was really loaded (listed in settings.json and found) and F2 hasn't turned it off.
bool redesigned_card() {
    return aidyn::settings::graphics().textures && aidyn::redrawn_art() && aidyn::redrawn_art_available();
}

gpr draw_title_branding(uint8_t* rdram, gpr g, float ox, float oy, float sx, float sy, uint32_t rgba) {
    copy_branding(rdram);
    title_version_switch();
    // "Rechronicled": centred under THE FIRST MAGE (card texels 26..172, rows 120..138) on the card's lower parchment
    // (the redesigned card has its own).
    if (!redesigned_card()) {
        g = draw_rgba16(rdram, g, 0, ox + (97 - 48) * sx, oy + 139 * sy, sx, sy);
    }
    // "Enhanced by SPITZDANO": bottom right of the whole picture, 10 texels in from the edge and the bottom. RT64
    // extended GBI: with the right edge as origin RT64 adds the frame width (320) to x, so the rectangle, drawn where it
    // would sit in 320x240, is moved back 320 by the alignment's offsets (texture rectangles can't take negative x).
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    // The game's scissor ends at the 4:3 edge, so it is widened to the whole window meanwhile (as for the clouds).
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Original) {
        g = draw_rgba16(rdram, g, 1, (310 - 140) * hs, (230 - 14) * vs, hs, vs);
        g = version_number(rdram, g, hs, vs, rgba);
        g = version_list(rdram, g, hs, vs, rgba);
        return title_versions_panel(rdram, g);
    }
    GfxCommand c[5];  // gEXEnable: 1 command; gEXSetScissor, gEXSetRectAlign: 2 each
    gEXEnable(&c[0]);
    gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
    gEXSetRectAlign(&c[3], G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -320 * 4, 0, -320 * 4, 0);
    g = put_cmds(rdram, g, c, 5);
    g = draw_rgba16(rdram, g, 1, (310 - 140) * hs, (230 - 14) * vs, hs, vs);
    g = version_number(rdram, g, hs, vs, rgba);
    // The version list: bottom left, from the left edge of the whole picture.
    gEXSetRectAlign(&c[0], G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
    g = put_cmds(rdram, g, c, 2);
    g = version_list(rdram, g, hs, vs, rgba);
    const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager), v = MEM_HU(kVres1, (gpr)gGfxManager);
    gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
    c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
    g = put_cmds(rdram, g, c, 3);
    return title_versions_panel(rdram, g);
}

// Field HUD at the screen edges. draw_hud_elements draws the time-of-day dial (Sundial::Draw) at the bottom left, the
// party portraits (Portraits::Draw) centred and the compass (Compass::Draw) at the bottom right, in 320x240
// coordinates, which RT64 keeps in the middle 4:3 of a widescreen picture. With settings.json hud_at_edges (default)
// the dial and the compass are drawn with RT64's rectangle alignment set to the picture's left or right edge (aidyn.us.toml hooks on entry, display list
// in a0, and before the return, display list in v0), so they keep their place relative to that edge: the dial 19
// texels from the left, the compass ring 19 from the right. The scissor is widened meanwhile and put back after.
namespace {
    bool hud_wide() {
        const auto& gfx = aidyn::settings::graphics();
        return gfx.hud_at_edges && gfx.aspect != aidyn::settings::Aspect::Original;
    }

    gpr hud_begin(uint8_t* rdram, gpr g, bool right) {
        GfxCommand c[5];
        gEXEnable(&c[0]);
        gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
        const int off = right ? -320 * 4 : 0;  // RIGHT adds the frame width to x
        gEXSetRectAlign(&c[3], right ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT, right ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT,
                        off, 0, off, 0);
        return put_cmds(rdram, g, c, 5);
    }

    gpr hud_end(uint8_t* rdram, gpr g) {
        const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager), v = MEM_HU(kVres1, (gpr)gGfxManager);
        GfxCommand c[3];
        gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
        c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
        c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
        return put_cmds(rdram, g, c, 3);
    }
}

extern "C" void aidyn_hud_begin(uint8_t* rdram, recomp_context* ctx, int right) {
    if (hud_wide()) {
        ctx->r4 = hud_begin(rdram, ctx->r4, right != 0);
    }
}

extern "C" void aidyn_hud_end(uint8_t* rdram, recomp_context* ctx) {
    if (hud_wide()) {
        ctx->r2 = hud_end(rdram, ctx->r2);
    }
}

// Skill icons for the weapon skills and Shield (#24). The skills lists (WidgetSkillInfo's constructor, func_80036E10:
// this in s3, the subtype in s4, the kind in s6: 0 a skill, 1 a weapon skill, 2 Shield) give only the 12 skills an
// icon (the game's table at 0x800DEF7C); the weapon rows and Shield had none, a gap at the left of the row. Before
// the icon is linked (0x80037028, aidyn.us.toml hook) those rows get one of the game's own item icons (painted in the
// redrawn art pack) for their kind of weapon, shown at the skill icons' 10x10.
extern "C" void new_(uint8_t* rdram, recomp_context* ctx);
extern "C" void loadBorg8(uint8_t* rdram, recomp_context* ctx);
extern "C" void WidgetBorg8__WidgetBorg8(uint8_t* rdram, recomp_context* ctx);

extern "C" void aidyn_weapon_skill_icon(uint8_t* rdram, recomp_context* ctx) {
    const gpr self = ctx->r19;
    const int kind = (int)(ctx->r22 & 0xFF), sub = (int)(ctx->r20 & 0xFFFF);
    if (!heap_pointer(self) || MEM_W(0x80, self) != 0 || (kind != 1 && kind != 2)) {
        return;
    }
    // WeaponClassEnum: 3 Hafted, 4 Missile, 5 Pole, 8 Sword, 9 Thrown (the rest are monsters' attacks).
    uint32_t icon = 0;
    if (kind == 2) {
        icon = 0x35EB;  // IconItemShield1
    } else {
        switch (sub) {
            case 3: icon = 0x35C5; break;  // IconItemAxe (axes, maces, clubs)
            case 4: icon = 0x35CA; break;  // IconItemBowArrow
            case 5: icon = 0x35E3; break;  // IconItemPike
            case 8: icon = 0x35EE; break;  // IconItemSword2
            case 9: icon = 0x35F0; break;  // IconItemThrowIron
            default: return;
        }
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr borg = (gpr)(int32_t)call_game(rdram, ctx, loadBorg8, icon);
    const gpr widget = (gpr)(int32_t)call_game(rdram, ctx, WidgetBorg8__WidgetBorg8, (uint32_t)memory, (uint32_t)borg);
    if (!heap_pointer(widget)) {
        return;
    }
    MEM_H(0x74, widget) = 10;  // width, height: the skill icons' size
    MEM_H(0x72, widget) = 10;
    MEM_W(0x80, self) = (int32_t)widget;
}

// Potion colours (#25). Every potion has the same icon (IconItemPotion 0x35E5, 16x16 CI8: a bottle of red liquid).
// Each potion but Healing gets a copy of it with its palette's red recoloured to the potion's own colour
// (tools/hdui/potions.py has the colours and the same recolour; the redrawn art pack replaces each copy with the
// painted bottle recoloured alike). Which potion an icon is for is noted where the game asks for one:
// GetItemImage's potion case (0x80044F50, the item ID in a2: inventory, shops, treasure, details) and the combat item
// menu's rows (0x800892BC, the potion in s1); the loadBorg8 that follows (hooked at its entry) recolours the palette of
// the picture it loads. Every loadBorg8 gives its caller a fresh copy on the heap (getBorgItem with the borg flag set:
// index -1, freed with the widget), so changing it touches no other icon.
namespace {
    constexpr int kPotionCount = 17, kHealing = 4;
    constexpr uint32_t kPotionIcon = 0x35E5;
    constexpr uint8_t kPotionColours[kPotionCount][3] = {
        { 255, 140, 30 }, { 255, 60, 20 }, { 190, 150, 255 }, { 170, 255, 40 }, { 0, 0, 0 }, { 255, 230, 60 },
        { 255, 110, 190 }, { 60, 220, 90 }, { 225, 235, 255 }, { 205, 110, 45 }, { 40, 230, 230 }, { 255, 200, 80 },
        { 150, 255, 150 }, { 110, 180, 255 }, { 240, 80, 255 }, { 120, 140, 210 }, { 95, 75, 140 } };
    int potion_pending = -1;
    // Scrolls and wands (#25): their spell's school colour on the icon's blue (tools/hdui/potions.py's "blue" mode).
    constexpr uint32_t kScrollIcon = 0x35E9, kWandIcon = 0x35F1;
    constexpr uint8_t kSchoolColours[6][3] = { { 235, 45, 45 }, { 255, 150, 30 }, { 60, 210, 90 }, { 150, 60, 210 },
                                               { 0, 0, 0 }, { 70, 120, 255 } };  // Chaos, Elemental, Naming, Necro, -, Star
    int school_pending = -1;
    // Item icons (#25): an item that shares its icon with others (item_icons.h, from tools/hdui/item_icons.py) gets a
    // copy of it that draws the same but is a texture of its own, which the redrawn art pack replaces with its own
    // painted icon: for its variant j, the j-th palette entry the picture doesn't use takes the colour of its middle
    // texel, and the middle texel points at that entry (item_icons.py's variant(), the same change).
    int item_icon_variant = -1;
    uint32_t item_icon_base = 0;
}

#include "item_icons.h"

// GetItemImage's entry (a0 the item ID): a scroll (type 17) or wand (13) notes its spell's school. The item database
// (gItemDBp 0x800E2980: total +0, Gear_RAM* +0x18, 0x30 bytes each, ID +0, spell index +0x25) and the spell database
// (gSpellDBp 0x800E2B30: SpellRAM* +0x10, 0x26 bytes each, school +0x1A).
extern "C" void aidyn_item_image(uint8_t* rdram, recomp_context* ctx) {
    school_pending = -1;
    potion_pending = -1;  // a call that never loaded its icon leaves no colour for the next bottle
    item_icon_variant = -1;
    const uint32_t id = (uint32_t)ctx->r4 & 0xFFFF, type = id >> 8;
    for (const ItemIcon& e : kItemIcons) {
        if (e.item == id) {
            item_icon_variant = e.variant;
            item_icon_base = e.icon;
        }
    }
    if (type != 13 && type != 17) {
        return;
    }
    const gpr items = MEM_W(0, (gpr)(int32_t)0x800E2980), spells_db = MEM_W(0, (gpr)(int32_t)0x800E2B30);
    if (!heap_pointer(items) || !heap_pointer(spells_db)) return;
    const gpr gear = MEM_W(0x18, items), spells = MEM_W(0x10, spells_db);
    if (!heap_pointer(gear) || !heap_pointer(spells)) return;
    const uint32_t total = MEM_BU(0, items);
    for (uint32_t i = 0; i < total; i++) {
        const gpr g = gear + 0x30 * i;
        if (MEM_HU(0, g) != id) continue;
        const uint32_t spell = MEM_BU(0x25, g), count = MEM_BU(0, spells_db);
        if (spell >= count) return;
        const uint32_t school = MEM_BU(0x1A, spells + 0x26 * spell);
        if (school < 6 && school != 4) school_pending = (int)school;
        return;
    }
}

extern "C" void aidyn_potion_from_id(uint8_t* rdram, recomp_context* ctx) {
    potion_pending = (int)(ctx->r6 & 0xFF);
}

extern "C" void aidyn_potion_from_combat(uint8_t* rdram, recomp_context* ctx) {
    potion_pending = (int)(ctx->r17 & 0xFF);
}

extern "C" int aidyn_potion_borg(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t icon = (uint32_t)ctx->r4 & 0xFFFF;
    const int k = potion_pending, school = school_pending;
    const bool item = item_icon_variant >= 0 && icon == item_icon_base;
    const int variant = item_icon_variant;
    potion_pending = -1;
    school_pending = -1;
    item_icon_variant = -1;
    const bool blue = school >= 0 && (icon == kScrollIcon || icon == kWandIcon);
    if (!item && !blue && (k < 0 || k >= kPotionCount || k == kHealing || icon != kPotionIcon)) {
        return 0;
    }
    const gpr real = (gpr)(int32_t)call_game(rdram, ctx, loadBorg8, icon);
    ctx->r2 = real;
    if (!heap_pointer(real) || MEM_W(0, real) != -1 || MEM_HU(0x08, real) != 4 || !heap_pointer(MEM_W(0x10, real))) {
        return 1;  // not a private copy: handed out as it is
    }
    const gpr palette = MEM_W(0x10, real), src = palette;
    if (item) {
        // The variant's change (item_icons.py's variant()): CI8 pixels at +0x14, width +0x0A, height +0x0C.
        const gpr pixels = MEM_W(0x14, real);
        const uint32_t w = MEM_HU(0x0A, real), h = MEM_HU(0x0C, real);
        if (!heap_pointer(pixels) || w * h > 4096 || w * h == 0) {
            return 1;
        }
        bool used[256] = {};
        for (uint32_t i = 0; i < w * h; i++) used[MEM_BU(i, pixels)] = true;
        int left = variant;
        for (int e = 0; e < 256; e++) {
            if (used[e]) continue;
            if (left-- == 0) {
                const uint32_t mid = (h / 2) * w + w / 2;
                MEM_H(2 * e, palette) = (int16_t)MEM_HU(2 * MEM_BU(mid, pixels), palette);
                MEM_B(mid, pixels) = (int8_t)e;
                break;
            }
        }
        return 1;
    }
    const uint8_t* t = blue ? kSchoolColours[school] : kPotionColours[k];
    for (int i = 0; i < 256; i++) {
        uint32_t v = MEM_HU(2 * i, src);
        const int r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31, a = v & 1;
        const int grey = blue ? std::min(r, g) : std::min(g, b), amt = (blue ? b : r) - grey;
        if (amt > 0) {
            const int nr = std::min(31, (blue ? r : grey) + (amt * (t[0] >> 3) + 15) / 31);
            const int ng = std::min(31, g + (amt * (t[1] >> 3) + 15) / 31);
            const int nb = std::min(31, (blue ? grey : b) + (amt * (t[2] >> 3) + 15) / 31);
            v = (uint32_t)(nr << 11 | ng << 6 | nb << 1 | a);
        }
        MEM_H(2 * i, palette) = (int16_t)v;
    }
    return 1;
}

// What's in view, for a widescreen picture. renderPlayers draws a character only within 5 units of the camera's aim
// or within 42.5 degrees of its direction (the cosine of that angle, kept in f24); RenderVoxelScenes draws a scene
// prop within 37.5 degrees and fades it out by 42.5 (f24, f22). Those angles fit the N64's 4:3 picture: a 16:9
// one reaches about 51 degrees to the side and a 21:9 one about 58, so characters and props at its edges vanished
// (#29). Both are widened by the picture's aspect, atan(tan(angle) * aspect / (4/3)), right after the game takes the
// cosines (aidyn.us.toml hooks); at 4:3 they stay the game's.
namespace {
    float in_view_cos(float radians) {
        float aspect = view_aspect();
        if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Window && aidyn::window_aspect() > 0.0f) {
            aspect = aidyn::window_aspect();
        }
        const float k = std::max(aspect / (4.0f / 3.0f), 1.0f);
        return std::cos(std::atan(std::tan(radians) * k));
    }
}

extern "C" void aidyn_actors_in_view(uint8_t* rdram, recomp_context* ctx) {
    ctx->f24.fl = in_view_cos(0.7417525f);
}

// The ground cells too (#10): the zone engine draws a map cell only if it touches a triangle from the camera out
// 200 units at 42.5 degrees either side of its direction (func_8000D270, FUN_80010598 in the decomp: the angle in
// f20 for its two Vec2Rot calls), so at 21:9 the picture's sides showed no ground, the dark below-horizon fill
// in its place. Widened the same way, in degrees.
extern "C" void aidyn_cells_in_view(uint8_t* rdram, recomp_context* ctx) {
    ctx->f20.fl = std::acos(in_view_cos(0.7417525f)) * 57.29578f;
}

extern "C" void aidyn_props_in_view(uint8_t* rdram, recomp_context* ctx) {
    ctx->f24.fl = in_view_cos(0.6544875f);
    ctx->f22.fl = in_view_cos(0.7417525f);
}

// The indoor minimap (MiniMap::Render: this in a0, the display list in a1; returns it in v0) is drawn in the corner
// above the compass (x 237 to 297 of 320) or, with B, big in the middle. With hud_at_edges the corner one moves with the
// compass to the picture's right edge, as the dial and compass do (RT64's rectangle alignment); the big one stays
// centred. Its left edge (mapX, +0x78) tells them apart: 237 in the corner, 52 when big (MiniMap::LoadData).
namespace {
    bool minimap_wide = false;  // this Render call is aligned right
}

extern "C" void aidyn_minimap_begin(uint8_t* rdram, recomp_context* ctx) {
    const gpr map = ctx->r4;
    minimap_wide = hud_wide() && MEM_W(0, map) != 0 && read_f32(rdram, map + 0x78) > 160.0f;
    if (minimap_wide) {
        ctx->r5 = hud_begin(rdram, ctx->r5, true);
    }
}

extern "C" void aidyn_minimap_end(uint8_t* rdram, recomp_context* ctx) {
    if (minimap_wide) {
        ctx->r2 = hud_end(rdram, ctx->r2);
        minimap_wide = false;
    }
}

// Fast-forward. Every mode of the game (walking, combat, menus, cutscenes) measures its frame's time step by counting
// the controller samples queued since the last frame: Controller::GetInput pops one and returns 1, and the frame calls
// it until it returns 0, then steps that many ticks of 1/60 s (capped at 6: made for the N64's 10 to 20 frames a
// second). aidyn.us.toml hooks GetInput's entry (the port) and its return: while fast-forward is on, every real sample
// is followed by (speed - 1) quiet ones, copies of it without its new presses (same stick, held buttons and hold
// times), so the game runs that many ticks a frame and no press is handled twice. Fast-forward is L3 or a held Tab
// (src/main/input.cpp), at game.fast_forward_speed; turns in combat get their own: combat_controls (the combat frame's
// sample loop) is hooked on entry and looks at gCombatP->current_Ent->aiP (set for enemies and the AI-led Sholeh):
// enemies' turns at game.combat_enemy_speed, the party's at game.combat_party_speed while they move and act (not while
// the player chooses: aiming (9, 10, 11, 18), the X / Y menus (22, 23), Perform's (25) and the pause menu (14, 27),
// where the cursors would race). A fraction (1.5) alternates ticks between frames. While the player's fast-forward is
// on, a marker shows at the top right.
namespace {
    constexpr gpr kQuietSample = (gpr)(int32_t)0x80F81000;  // one ControllerFull (0x34 bytes), above the game's 15 MB
    constexpr int kControllerFull = 0x34;
    constexpr int kPressed = 0x8;                            // controller_aidyn.pressed
    constexpr gpr kCombatP = (gpr)(int32_t)0x800E0BF0;       // gCombatP
    constexpr int kCurrentEnt = 0x2C, kAiP = 0x44;           // CombatStruct.current_Ent, CombatEntity.aiP
    int ff_port = 0;
    int ff_pending = 0;   // quiet samples still to hand out this frame
    int ff_combat = 1;    // speed for the combat_controls call under way (an enemy's turn)
    float ff_combat_carry = 0.0f;  // a fractional speed's ticks carried to the next frame
}

std::atomic_uint32_t aidyn_game_ticks{ 0 };  // samples handed out on port 1: game ticks (AIDYN_FPS prints the rate)

extern "C" void aidyn_ff_input_begin(int port) {
    ff_port = port;
}

// a0 (the caller's controller_aidyn**) was kept in s3; popped is GetInput's result.
extern "C" uint32_t aidyn_ff_input_end(uint8_t* rdram, gpr input_ptr, uint32_t popped) {
    if (ff_port != 0) {
        return popped;
    }
    const int speed = std::max(aidyn::input::fast_forward() ? aidyn::settings::game().fast_forward_speed : 1, ff_combat);
    if (popped) {
        if (speed > 1) {
            const gpr sample = (gpr)(int32_t)MEM_W(0, input_ptr);
            for (int i = 0; i < kControllerFull; i += 2) {
                MEM_H(i, kQuietSample) = MEM_H(i, sample);
            }
            MEM_W(kPressed, kQuietSample) = 0;
            ff_pending += speed - 1;
        }
        aidyn_game_ticks.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    if (ff_pending > 0) {
        ff_pending--;
        MEM_W(0, input_ptr) = (int32_t)kQuietSample;
        aidyn_game_ticks.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    ff_combat = 1;  // the queue is empty: the combat frame's sample loop (if that is where we are) is over
    return 0;
}

extern "C" void aidyn_ff_combat(uint8_t* rdram) {
    float speed = 1.0f;
    const gpr combat = (gpr)(int32_t)MEM_W(0, kCombatP);
    if (combat != 0) {
        const gpr ent = (gpr)(int32_t)MEM_W(kCurrentEnt, combat);
        if (ent != 0 && MEM_W(kAiP, ent) != 0) {
            speed = aidyn::settings::game().combat_enemy_speed;
        } else if (ent != 0) {
            switch (MEM_BU(0, (gpr)(int32_t)0x800D9454)) {  // gGlobals.combatBytes[0]
                case 9: case 10: case 11: case 14: case 18: case 22: case 23: case 25: case 27: break;  // choosing
                default: speed = aidyn::settings::game().combat_party_speed; break;
            }
        }
    }
    speed = std::clamp(speed, 1.0f, 6.0f) + ff_combat_carry;
    ff_combat = (int)speed;
    ff_combat_carry = speed - (float)ff_combat;
}

// Graphics__EndList (the end of every frame's display list) draws this: the chevrons (branding image 4), 8 texels in
// from the top right of the picture, while the player's fast-forward is on.
gpr aidyn_ff_marker(uint8_t* rdram, gpr g) {
    if (!aidyn::input::fast_forward()) {
        return g;
    }
    copy_branding(rdram);
    const auto& img = aidyn::branding::kImages[4];
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
    g = hud_begin(rdram, g, true);
    cmd(0xE7000000, 0);                  // gDPPipeSync
    cmd(0xE3000A01, 0x00000000);         // gDPSetCycleType(G_CYC_1CYCLE)
    cmd(0xE3000C00, 0x00000000);         // gDPSetTexturePersp(G_TP_NONE)
    cmd(0xE3001001, 0x00000000);         // gDPSetTextureLUT(G_TT_NONE)
    cmd(0xE3001201, 0x00002000);         // gDPSetTextureFilter(G_TF_BILERP)
    cmd(0xE2001E01, 0x00000000);         // gDPSetAlphaCompare(G_AC_NONE)
    cmd(0xE200001C, 0x00504240);         // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2)
    cmd(0xFCFFFFFF, 0xFFFCF279);         // gDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA)
    g = draw_rgba16(rdram, g, 4, (320 - 8 - img.w) * hs, 8 * vs, hs, vs);
    return hud_end(rdram, g);
}

// Combat HUD at the screen edges. The combat screen draws the enemies' column at the left (slots at x 21 to 47) and the
// party's at the right (x 270 to 298) in 320x240 coordinates, which RT64 keeps in the middle 4:3 of a widescreen
// picture: each slot a WidgetBorg8Combat frame, each fighter's portrait a WidgetBorg8Combat that flies in from the
// centre to its slot (crossbones are its child), the health bars drawn beside the slots by CombatDrawHealthbar, and
// the radar (WidgetCombatRadar, func_8008DF04) at the bottom right. With hud_at_edges, aidyn.us.toml hooks the entry
// and return of the two Render functions and the bar drawing, and each piece is drawn with RT64's rectangle alignment
// on its side: a slot frame by where it stands, a portrait by the slot it goes to (so its fly-in ends at the edge
// instead of jumping sides halfway), a bar by its x, the radar on the right. The message box and menus stay centred.
// Target flash: the fighters the game marks as targeted (the ones whose models it pulses, CombatTargetVisuals::Render:
// gCombatP->substruct2[1].arrayB while a spell is aimed, combat state 10 or 11, else substruct2[0].arrayB; an int per
// combatEnts index) also pulse on their portraits: a white frame and a light flash over the picture, on the portrait
// in its column (the outermost WidgetBorg8Combat render of a portrait, gCombatPortraits' cEntIndex = the fighter) and
// in the turn order (below).
namespace {
    constexpr gpr kCombatPortraitCount = (gpr)(int32_t)0x800E3144;  // u8
    constexpr gpr kCombatPortraits = (gpr)(int32_t)0x800E3148;      // CombatPortrait* (0x18 each: widget, ..., blipIndex at 0x10)
    constexpr gpr kCombatRadarBlips = (gpr)(int32_t)0x800E314C;     // CombatRadarBlip* (0x18 each: widget, s16 x at 4)
    constexpr int kWidgetPosX = 0x60;                               // BaseWidget.posX
    int combat_depth = 0;
    bool combat_aligned[64];
    bool radar_aligned = false, bar_aligned = false;
    gpr combat_widget[64];  // the widget each nested render draws

    constexpr gpr kCombatState = (gpr)(int32_t)0x800D9454;          // gGlobals.combatBytes[0]
    constexpr int kTargetsAttack = 0x4EAC, kTargetsSpell = 0x4F54;  // gCombatP->substruct2[0 / 1].arrayB, int[18]

    bool combat_targeted(uint8_t* rdram, gpr combat, int index) {
        if (index < 0 || index >= 18) {
            return false;
        }
        // Not while the player moves (1) or browses the X / Y menus (22, 23): the game leaves a cancelled spell's
        // targets marked (cancel_combat_action doesn't clear them; only the next spell's aim does).
        const uint8_t state = MEM_BU(0, kCombatState);
        if (state == 1 || state == 22 || state == 23) {
            return false;
        }
        const int list = (uint8_t)(state - 10) < 2 ? kTargetsSpell : kTargetsAttack;
        return MEM_W(list + 4 * index, combat) != 0;
    }

    // 0 to 1 and back, about once a second, like the game's pulse on the models.
    float target_pulse() {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        return 0.5f - 0.5f * (float)std::cos(t * 2.0 * 3.14159265 / 0.9);
    }

    // Flat colour rectangles: one-cycle, blended, the primitive colour.
    gpr rect_state(uint8_t* rdram, gpr g) {
        put(rdram, g, 0xE7000000, 0);           // gDPPipeSync
        put(rdram, g, 0xE3000A01, 0x00000000);  // gDPSetCycleType(G_CYC_1CYCLE)
        put(rdram, g, 0xE200001C, 0x00504240);  // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2)
        put(rdram, g, 0xFCFFFFFF, 0xFFFDF6FB);  // gDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE)
        return g;
    }

    // x0..x1, y0..y1 in units of the 320x240 screen; hs, vs pixels a unit.
    gpr rect(uint8_t* rdram, gpr g, float x0, float y0, float x1, float y1, uint32_t rgba, float hs, float vs) {
        put(rdram, g, 0xFA000000, rgba);  // gDPSetPrimColor
        const uint32_t ulx = (uint32_t)std::max(0, (int)(x0 * hs * 4.0f)), uly = (uint32_t)std::max(0, (int)(y0 * vs * 4.0f));
        const uint32_t lrx = (uint32_t)(int)(x1 * hs * 4.0f), lry = (uint32_t)(int)(y1 * vs * 4.0f);
        put(rdram, g, 0xF6000000 | ((lrx & 0xFFF) << 12) | (lry & 0xFFF), ((ulx & 0xFFF) << 12) | (uly & 0xFFF));
        return g;
    }

    // The flash on a targeted fighter's portrait at x0..x1, y0..y1 (after the portrait is drawn).
    gpr target_flash(uint8_t* rdram, gpr g, float x0, float y0, float x1, float y1, float hs, float vs) {
        const float pulse = target_pulse();
        const uint32_t frame = 0xFFFFFF00u | (uint32_t)(90 + 165 * pulse), flash = 0xFFFFFF00u | (uint32_t)(80 * pulse);
        g = rect_state(rdram, g);
        g = rect(rdram, g, x0, y0, x1, y1, flash, hs, vs);
        g = rect(rdram, g, x0 - 1.0f, y0 - 1.0f, x1 + 1.0f, y0, frame, hs, vs);
        g = rect(rdram, g, x0 - 1.0f, y1, x1 + 1.0f, y1 + 1.0f, frame, hs, vs);
        g = rect(rdram, g, x0 - 1.0f, y0, x0, y1, frame, hs, vs);
        g = rect(rdram, g, x1, y0, x1 + 1.0f, y1, frame, hs, vs);
        return g;
    }

    // Which edge a combat widget belongs to: false left, true right.
    bool combat_right(uint8_t* rdram, gpr widget) {
        const gpr portraits = (gpr)(int32_t)MEM_W(0, kCombatPortraits);
        const gpr blips = (gpr)(int32_t)MEM_W(0, kCombatRadarBlips);
        const int count = MEM_BU(0, kCombatPortraitCount);
        for (int i = 0; portraits != 0 && i < count; i++) {
            const gpr p = portraits + i * 0x18;
            if ((gpr)(int32_t)MEM_W(0, p) == widget) {
                const int blip = MEM_BU(0x10, p);
                if (blip != 0xFF && blips != 0) {
                    return (int16_t)MEM_H(4, blips + blip * 0x18) >= 160;
                }
                break;
            }
        }
        return (int16_t)MEM_H(kWidgetPosX, widget) >= 160;
    }
}

extern "C" void borg8DlistInit(uint8_t* rdram, recomp_context* ctx);  // (g, flags, hres, vres): the 2D images' state

// Portrait frames (combat columns): spitzdano's ornate frames over each portrait in the combat columns, gold for the party
// and red and silver for enemies (branding images frame_party / frame_enemy, tools/branding/hud_frames.py), drawn
// after the portrait (its widget's render, below) while the redrawn art shows. The game's own slot frame under it
// (image 0x1C6) is hidden then (aidyn_hud_frames_image, below); its colour said something (in reach: green, the
// spell's: red, targeted: brighter), which shows instead as a soft band of that colour round the frame (frame_glow).
namespace {
    int branding_index(const char* name);  // "Button prompts", below

    gpr tinted_image_state(uint8_t* rdram, gpr g, uint32_t rgba) {
        auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
        cmd(0xE7000000, 0);                  // gDPPipeSync
        cmd(0xE3000A01, 0x00000000);         // gDPSetCycleType(G_CYC_1CYCLE)
        cmd(0xE3000C00, 0x00000000);         // gDPSetTexturePersp(G_TP_NONE)
        cmd(0xE3001001, 0x00000000);         // gDPSetTextureLUT(G_TT_NONE)
        cmd(0xE3001201, 0x00002000);         // gDPSetTextureFilter(G_TF_BILERP)
        cmd(0xE2001E01, 0x00000000);         // gDPSetAlphaCompare(G_AC_NONE)
        cmd(0xE200001C, 0x00504240);         // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2)
        cmd(0xFC119623, 0xFF2FFFFF);         // gDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM)
        cmd(0xFA000000, rgba);               // gDPSetPrimColor
        return g;
    }

    // The frame (and the band, if the slot is coloured) round a column portrait's widget; p its CombatPortrait.
    gpr column_frame(uint8_t* rdram, gpr g, gpr widget, gpr p) {
        const int frame = branding_index(MEM_W(0x0C, p) != 0 ? "frame_party" : "frame_enemy");
        const int glow = branding_index("frame_glow");
        if (frame < 0 || glow < 0) {
            return g;
        }
        copy_branding(rdram);
        const float x = (int16_t)MEM_HU(kWidgetPosX, widget), y = (int16_t)MEM_HU(kWidgetPosX + 2, widget);
        const float fx = MEM_HU(0x74, widget) / 23.0f, fy = MEM_HU(0x72, widget) / 23.0f;  // the portrait is 23x23
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        const gpr blips = MEM_W(0, kCombatRadarBlips);
        const int blip = MEM_BU(0x10, p);
        if (heap_pointer(blips) && blip != 0xFF) {
            const gpr slot = MEM_W(0, blips + blip * 0x18);
            if (heap_pointer(slot)) {
                const uint32_t r = MEM_BU(0x58, slot), gr = MEM_BU(0x59, slot), b = MEM_BU(0x5A, slot), a = MEM_BU(0x5B, slot);
                const bool plain = r < 40 && gr < 40;                                     // the usual black
                const bool gold = r > 150 && gr > 120 && b < 60 && std::abs((int)r - (int)gr) < 60;  // an ally's
                // The spell's red (in its range) stays on the slot after a cancelled spell (the game clears the
                // range list only at the next spell's aim): not while moving or in the X / Y menus, as the
                // target flash ("Target flash" above).
                const uint8_t state = MEM_BU(0, kCombatState);
                const bool stale_red = r > 150 && gr < 60 && (state == 1 || state == 22 || state == 23);
                if (!plain && !gold && !stale_red && a != 0) {
                    g = tinted_image_state(rdram, g, r << 24 | gr << 16 | b << 8 | std::min(255u, a + 60));
                    g = draw_rgba16(rdram, g, glow, (x - 6.0f * fx) * hs, (y - 6.0f * fy) * vs, fx * hs, fy * vs);
                }
            }
        }
        g = tinted_image_state(rdram, g, 0xFFFFFFFFu);
        return draw_rgba16(rdram, g, frame, (x - 4.0f * fx) * hs, (y - 4.0f * fy) * vs, fx * hs, fy * vs);
    }
}

// HP and stamina inside the portrait frames (spitzdano's pick over the game's bars hanging off the frames' sides):
// across the bottom of the picture, HP (the game's own colour for it, getHPColor) and a thin blue stamina bar under it,
// on a dark backing. HP is Entity::getHPCurrent / getHPMax (Endurance, Stamina and the level stat together); stamina
// CharStats::getModded(STAT_STAM) of its most, Base[STAT_STAM] (the stats' byte 5; checked: 25 of 30, 6 of 20). The
// game's own bars (CombatDrawHealthbar) aren't drawn while the frames show (aidyn_combat_bars_hidden).
extern "C" void Entity__getHPCurrent(uint8_t* rdram, recomp_context* ctx);
extern "C" void Entity__getHPMax(uint8_t* rdram, recomp_context* ctx);
extern "C" void getHPColor(uint8_t* rdram, recomp_context* ctx);
extern "C" void CharStats__getModded(uint8_t* rdram, recomp_context* ctx);

extern "C" int aidyn_combat_bars_hidden() {
    return redesigned_card() ? 1 : 0;
}

gpr tired_mark(uint8_t* rdram, recomp_context* ctx, gpr g, float x, float y, float w, float fx, float fy);  // below

namespace {
    constexpr gpr kHpColorSlot = (gpr)(int32_t)0x80F81780;  // getHPColor's Color32 out (scratch above the game's 15 MB)

    gpr column_bars(uint8_t* rdram, recomp_context* ctx, gpr g, gpr widget, gpr p) {
        const gpr combat = MEM_W(0, kCombatP);
        const int ent_index = MEM_BU(0x0A, p);  // cEntIndex
        if (!heap_pointer(combat) || ent_index >= 18) return g;
        const gpr ent = MEM_W(0x52D0 + 4 * ent_index, combat);
        const gpr sheet = heap_pointer(ent) ? MEM_W(0x40, ent) : 0;
        const gpr stats = heap_pointer(sheet) ? MEM_W(0x18, sheet) : 0;
        if (!heap_pointer(stats)) return g;
        const uint32_t hp = (uint32_t)call_game(rdram, ctx, Entity__getHPCurrent, (uint32_t)sheet);
        const uint32_t hp_max = (uint32_t)call_game(rdram, ctx, Entity__getHPMax, (uint32_t)sheet) & 0xFF;
        if (hp_max == 0) return g;
        call_game(rdram, ctx, getHPColor, (uint32_t)kHpColorSlot, (uint32_t)sheet);
        const uint32_t hp_rgb = (uint32_t)MEM_BU(0, kHpColorSlot) << 24 | (uint32_t)MEM_BU(1, kHpColorSlot) << 16 |
                                (uint32_t)MEM_BU(2, kHpColorSlot) << 8;
        const int stam = (int)(int32_t)call_game(rdram, ctx, CharStats__getModded, (uint32_t)stats, 5);  // STAT_STAM
        const int stam_max = std::max<int>(MEM_BU(5, stats), stam);  // Base[STAT_STAM]: its most
        const float hpf = std::clamp((float)hp / (float)hp_max, 0.0f, 1.0f);
        const float stf = stam_max > 0 ? std::clamp((float)stam / (float)stam_max, 0.0f, 1.0f) : 0.0f;
        const float x = (int16_t)MEM_HU(kWidgetPosX, widget), y = (int16_t)MEM_HU(kWidgetPosX + 2, widget);
        const float w = MEM_HU(0x74, widget), h = MEM_HU(0x72, widget), fx = w / 23.0f, fy = h / 23.0f;
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        const float x0 = x + 1.5f * fx, x1 = x + w - 1.5f * fx;
        const float hy0 = y + h - 4.7f * fy, hy1 = y + h - 2.6f * fy, sy0 = y + h - 2.2f * fy, sy1 = y + h - 1.3f * fy;
        g = rect_state(rdram, g);
        g = rect(rdram, g, x0 - 0.6f * fx, hy0 - 0.6f * fy, x1 + 0.6f * fx, sy1 + 0.6f * fy, 0x000000B4, hs, vs);  // backing
        g = rect(rdram, g, x0, hy0, x1, hy1, 0x262626FF, hs, vs);
        if (hpf > 0.0f) g = rect(rdram, g, x0, hy0, x0 + (x1 - x0) * hpf, hy1, hp_rgb | 0xFF, hs, vs);
        g = rect(rdram, g, x0, sy0, x1, sy1, 0x262626FF, hs, vs);
        if (stf > 0.0f) g = rect(rdram, g, x0, sy0, x0 + (x1 - x0) * stf, sy1, 0x4A9CFFFF, hs, vs);
        static const bool tired_test = std::getenv("AIDYN_TIRED_TEST") != nullptr;  // the mark on everyone, to look at it
        if ((stam <= 0 || tired_test) && hp > 0) {
            g = tired_mark(rdram, ctx, g, x, y, w, fx, fy);
        }
        return g;
    }
}

// WidgetBorg8Combat::Render(this, g, ...): this in a0, g in a1; returns g in v0. Only the outermost of nested renders
// (a portrait and its crossbones) sets the alignment.
extern "C" void aidyn_combat_widget_begin(uint8_t* rdram, recomp_context* ctx) {
    const int d = combat_depth++;
    if (d >= (int)std::size(combat_aligned)) {
        return;
    }
    combat_widget[d] = ctx->r4;
    combat_aligned[d] = d == 0 && hud_wide();
    if (combat_aligned[d]) {
        ctx->r5 = hud_begin(rdram, ctx->r5, combat_right(rdram, ctx->r4));
    }
}

extern "C" void aidyn_combat_widget_end(uint8_t* rdram, recomp_context* ctx) {
    const int d = --combat_depth;
    const gpr combat = MEM_W(0, kCombatP), portraits = MEM_W(0, kCombatPortraits);
    if (d == 0 && combat != 0 && portraits != 0 && MEM_W(0x7C, combat_widget[0]) != 0) {  // WidgetBorg8Combat visible
        const gpr widget = combat_widget[0];
        for (int i = 0; i < MEM_BU(0, kCombatPortraitCount); i++) {
            const gpr p = portraits + i * 0x18;
            if (MEM_W(0, p) == widget) {
                if (redesigned_card() && MEM_HU(0x74, widget) != 0 && heap_pointer(MEM_W(0x6C, widget))) {  // (a spare slot has no picture)
                    gpr g = column_frame(rdram, ctx->r2, widget, p);
                    g = column_bars(rdram, ctx, g, widget, p);
                    ctx->r2 = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6,
                                                      MEM_HU(kHres1, (gpr)gGfxManager), MEM_HU(kVres1, (gpr)gGfxManager));
                }
                if (combat_targeted(rdram, combat, MEM_BU(0x0A, p))) {  // cEntIndex
                    const float x = (int16_t)MEM_HU(kWidgetPosX, widget), y = (int16_t)MEM_HU(kWidgetPosX + 2, widget);
                    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
                    gpr g = target_flash(rdram, ctx->r2, x, y, x + MEM_HU(0x74, widget), y + MEM_HU(0x72, widget), hs, vs);
                    // Back to the combat HUD's own state (its RSPFUNC6 at the start): the widgets after this one
                    // (the other portraits, and the pause menu's pages in combat) draw with it, and would come out
                    // in flat colour.
                    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                                MEM_HU(kVres1, (gpr)gGfxManager));
                    ctx->r2 = g;
                }
                break;
            }
        }
    }
    if (d >= 0 && d < (int)std::size(combat_aligned) && combat_aligned[d]) {
        ctx->r2 = hud_end(rdram, ctx->r2);
    }
    if (combat_depth < 0) {
        combat_depth = 0;
    }
}

extern "C" void aidyn_combat_radar_begin(uint8_t* rdram, recomp_context* ctx) {
    radar_aligned = hud_wide();
    if (radar_aligned) {
        ctx->r5 = hud_begin(rdram, ctx->r5, true);
    }
}

extern "C" void aidyn_combat_radar_end(uint8_t* rdram, recomp_context* ctx) {
    if (radar_aligned) {
        ctx->r2 = hud_end(rdram, ctx->r2);
        radar_aligned = false;
    }
}

// CombatDrawHealthbar: each bar is two DrawRectangle calls; g in s6 and the bar's x in s4 before them, g in v0 after.
extern "C" gpr aidyn_combat_bar_begin(uint8_t* rdram, gpr g, int x) {
    bar_aligned = hud_wide();
    return bar_aligned ? hud_begin(rdram, g, x >= 160) : g;
}

extern "C" gpr aidyn_combat_bar_end(uint8_t* rdram, gpr g) {
    if (bar_aligned) {
        bar_aligned = false;
        return hud_end(rdram, g);
    }
    return g;
}

// HD text widths. The game measures every letter's width from the original font sheet when it loads a face
// (Font::LoadFace, func_800AA474, through Font::SetupBorg8) and both draws that many columns of the letter's cell
// and advances by it. The original pixel font is wide and loosely spaced, so the HD pack's letters (tools/hdui/font.py)
// have their own proportions and these widths (src/game/font_widths.h, generated with them). aidyn.us.toml hooks
// LoadFace right after SetupBorg8 (s4 = the face's Borg8 index, s0 = its FontSubstruct, widths at +0x10) and the two
// font sheets (BORG8_MainFont 28, BORG8_SlimFont 29) get them, only when the letters in use were drawn for them: the
// pack they come from holds the id of these widths in aidyn-text-widths.txt. That is rechronicled-text (letters and
// buttons only, tools/hdui/text_pack.py, shipped with every release so it always matches) or else the HD UI pack
// (build_pack.py), whichever comes later in texture_packs (later packs win). Letters drawn for other widths (an older
// pack) or the original letters would be cut off or spread out, so the original widths stay.
#include "font_widths.h"
#include "packs.h"

namespace {
    bool hd_text() {
        static const bool on = [] {
            const auto& packs = aidyn::settings::graphics().texture_packs;
            for (auto name = packs.rbegin(); name != packs.rend(); ++name) {
                if (*name != "rechronicled-text" && *name != "hd-ui") {
                    continue;
                }
                const std::filesystem::path pack = aidyn::packs::find(*name);  // its folder or its .rtz
                if (pack.empty()) {
                    continue;
                }
                const std::string id = aidyn::packs::first_word(*name, aidyn::font::kPackMarker);
                if (id == aidyn::font::kWidthsId) {
                    return true;
                }
                fprintf(stderr, "HD text: %s was drawn for other letter widths (%s): the game keeps its own. "
                        "rechronicled-text from this release (or a newer hd-ui pack) gives the HD text its "
                        "spacing.\n", pack.string().c_str(), id.empty() ? "an older pack" : id.c_str());
                return false;
            }
            return false;
        }();
        return on;
    }
}

// The two faces are loaded once, when the game starts (App's init), and kept: their widths are remembered as loaded,
// so textures off (the pause menu's ReChronicled page) can give the original letters their original widths back.
namespace {
    struct FaceWidths {
        gpr sizes;
        std::vector<uint16_t> original;  // per kWidths entry
    };
    std::vector<FaceWidths> font_faces;

    void apply_font_widths(uint8_t* rdram) {
        const bool hd = hd_text() && aidyn::settings::graphics().textures;
        for (const FaceWidths& f : font_faces) {
            size_t k = 0;
            for (const auto& w : aidyn::font::kWidths) {
                MEM_H(w.cell * 2, f.sizes) = hd ? w.width : f.original[k];
                k++;
            }
        }
    }
}

extern "C" void aidyn_font_widths(uint8_t* rdram, uint32_t borg, gpr face) {
    if ((borg != 28 && borg != 29) || !hd_text()) {
        return;
    }
    FaceWidths f{ (gpr)(int32_t)MEM_W(0x10, face), {} };
    for (const auto& w : aidyn::font::kWidths) {
        f.original.push_back(MEM_HU(w.cell * 2, f.sizes));
    }
    font_faces.push_back(std::move(f));
    apply_font_widths(rdram);
}

// ---- Sound log (AIDYN_SFX_LOG) ----
// What the game asks to play: DCM::Add (every sound voice: footsteps, swings, hits, spells), PlayAudioSound (menu
// and dialogue sounds, by Borg12 index) and player_audiokey's per-frame check with the actor's proximity volume.
namespace {
bool sfx_log() {
    static const bool on = std::getenv("AIDYN_SFX_LOG") != nullptr;
    return on;
}
}

// Sound falloff. A character's animation sounds (footsteps, swings, hits: player_audiokey) take their volume from
// func_8004EB90: 1 - distance / 15 from the camera's aim (Sound_Volume_proximity), times 255, stored as a byte with
// no clamp. Past 15 units that is negative and the byte wraps round: 20 units away gives 171 of 255, loud. The game
// skips a sound at volume 0, so 15 units was meant as the edge; it went unnoticed while characters only moved within
// 30 units, but with them moving farther (draw_distance) distant footsteps were loud. Outside combat the volume is
// clamped to 0..1 right after the call, so they fade out at 15 units as intended; combat keeps the old behaviour
// (its sounds were all heard as they are).
// Below the horizon. Outdoors the sky is a gradient drawn row by row down to the horizon, and everything below it is
// filled with the last row's colour (RenderSky's DrawRectangle at 0x8001DD50), meant to meet the fogged distant
// ground. Where two pieces of ground or a cliff and the ground don't quite meet, that fill shows through: hairline
// gaps the N64's own anti-aliasing blended away at 320x240 show as light blue lines at high resolution, and a hole
// near the camera as a blue triangle. So the fill keeps the horizon colour only in a band just below the horizon
// (where the distant ground meets it) and fades over 12 rows to a quarter of it: gaps nearer the camera read as dark
// creases. The horizon row also goes to RT64's gap filler (Graphics__EndList, aidyn::set_gap_fill_horizon): below it
// any background showing is a hole, so wider ones are filled there (tools/patches/rt64-save-pictures.patch,
// GapFillCS.hlsl). AIDYN_SKY_FILL=rrggbb (testing) fills it all with one colour instead (magenta shows every gap),
// =off keeps the game's own;
// AIDYN_SKY_LOG=1 prints the horizon row and colour.
extern "C" void DrawRectangle(uint8_t* rdram, recomp_context* ctx);

int aidyn_sky_horizon = -1;

extern "C" void aidyn_sky_fill(uint8_t* rdram, recomp_context* ctx) {
    static const char* fill = std::getenv("AIDYN_SKY_FILL");
    static const bool log = std::getenv("AIDYN_SKY_LOG") != nullptr;
    const gpr sp = ctx->r29;  // DrawRectangle(gfx, x0, y0, x1, y1, r, g, b, a): y1 and the colour on the stack
    const uint32_t horizon[3] = { (uint32_t)MEM_W(0x14, sp) & 0xFF, (uint32_t)MEM_W(0x18, sp) & 0xFF,
                                  (uint32_t)MEM_W(0x1C, sp) & 0xFF };
    if (ctx->r6 == 0 && (horizon[0] | horizon[1] | horizon[2]) == 0) {
        return;  // the black full-screen fill (indoors, or no sky), not the horizon's (which starts at row 0 too when
                 // the camera looks down past it)
    }
    aidyn_sky_horizon = (int)ctx->r6;  // for the gap filler (Graphics__EndList hands it to RT64)
    if (log) {
        static int frames = 0;
        if (++frames % 60 == 0) {
            fprintf(stderr, "sky: horizon row %u colour %02X%02X%02X\n", (uint32_t)ctx->r6, horizon[0], horizon[1],
                    horizon[2]);
        }
    }
    auto colour = [&](float t) {  // t 0: the horizon's, 1: a quarter of it
        for (int c = 0; c < 3; c++) {
            MEM_W(0x14 + 4 * c, sp) = (uint32_t)std::lround(horizon[c] * (1.0f - 0.75f * t));
        }
    };
    if (fill != nullptr && std::strcmp(fill, "off") == 0) {
        return;  // the game's own fill, for comparing
    }
    if (fill != nullptr) {
        const uint32_t rgb = (uint32_t)std::strtoul(fill, nullptr, 16);
        MEM_W(0x14, sp) = (rgb >> 16) & 0xFF;
        MEM_W(0x18, sp) = (rgb >> 8) & 0xFF;
        MEM_W(0x1C, sp) = rgb & 0xFF;
        return;
    }
    constexpr int kBands = 6, kBandRows = 2;
    const int bottom = (int)MEM_W(0x10, sp);
    int y = (int)ctx->r6;
    uint32_t gfx = (uint32_t)ctx->r4;
    for (int k = 0; k < kBands && y + kBandRows < bottom; k++, y += kBandRows) {
        MEM_W(0x10, sp) = y + kBandRows;
        colour((k + 0.5f) / kBands);
        gfx = call_game(rdram, ctx, DrawRectangle, gfx, 0, (uint32_t)y, (uint32_t)ctx->r7);  // a3: x1 (320)
    }
    MEM_W(0x10, sp) = bottom;  // the game's own call: the rest, dark
    colour(1.0f);
    ctx->r4 = (int32_t)gfx;
    ctx->r6 = y;
}

// Lightning. In a storm RenderSky counts a timer up each frame, modulo 20, and when it comes round to 0 a flash may
// come (a chance of the storm's strength / 4): the whole screen white for a frame, the sky white, and a thunderclap.
// The N64 drew about 20 frames a second, so that was a chance about once a second; the port draws 60, so storms
// flashed three times as often (a player reported "the sky flashes a lot"). The timer now comes round to 0 every
// third time and starts again at 1 otherwise: a chance every 58 frames, about once a second again.
// AIDYN_STORM_LOG=1 logs each chance with the storm's strength; AIDYN_STORM_TEST=<0..1> holds a storm (testing).
extern "C" void aidyn_storm_timer(uint8_t* rdram, recomp_context* ctx) {
    static const bool log = std::getenv("AIDYN_STORM_LOG") != nullptr;
    static const char* test = std::getenv("AIDYN_STORM_TEST");  // testing: the storm's strength held at this
    static uint32_t rounds = 0;
    const gpr terrain = (gpr)MEM_W(0, (gpr)(int32_t)0x800E303C);  // TerrainPointer
    if (test != nullptr && terrain != 0) {
        write_f32(rdram, terrain + 0x10, (float)std::atof(test));
    }
    if ((ctx->r6 + 1) % 20 != 0) {
        return;
    }
    if (++rounds % 3 != 0) {
        ctx->r6 = 0;  // counted up to 1, not 0
        return;
    }
    if (log) {
        fprintf(stderr, "storm: a lightning chance, thunder %.3f\n", terrain != 0 ? read_f32(rdram, terrain + 0x10) : 0.0f);
    }
}

extern "C" void aidyn_sound_falloff(uint8_t* rdram, recomp_context* ctx) {
    if (MEM_BU(0, (gpr)(int32_t)0x800D80C8) == 2) {  // gGlobals.gameStateA: 2 = combat
        return;
    }
    ctx->f0.fl = std::clamp(ctx->f0.fl, 0.0f, 1.0f);
}

extern "C" void aidyn_sfx_key(uint8_t* rdram, recomp_context* ctx) {
    if (!sfx_log()) {
        return;
    }
    uint8_t prox = MEM_BU(0x68, ctx->r29);
    fprintf(stderr, "sfx key: actor %08X anim %u frame %u/%u prox %u\n", (uint32_t)ctx->r16, (uint32_t)ctx->r17,
            (uint32_t)ctx->r18, (uint32_t)ctx->r19, prox);
}

// Sound test (AIDYN_SOUND_TEST=<Borg12 index>, for exporting the game's music and sounds with AIDYN_AUDIO_DUMP): the
// title screen plays that Borg12 in place of its theme (load_intro_music hook), and nothing else gets a voice: the
// title's flying camera loads map areas whose own music and ambient sounds would otherwise mix in.
namespace {
    int sound_test_track() {
        static const int track = [] {
            const char* v = std::getenv("AIDYN_SOUND_TEST");
            return (v != nullptr && *v != '\0') ? (int)std::strtol(v, nullptr, 0) : -1;
        }();
        return track;
    }
}

extern "C" gpr aidyn_sound_test_track(gpr index) {
    const int track = sound_test_track();
    if (track < 0) {
        return index;
    }
    fprintf(stderr, "sound test: Borg12 %d (0x%X) in place of the title theme %u\n", track, track, (uint32_t)index);
    return (gpr)track;
}

// DCM::Add's entry; returns 1 to refuse the sound (the hook returns 0 from DCM::Add).
extern "C" int aidyn_sfx_dcm(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        fprintf(stderr, "sfx dcm add: sub %08X vol %u pan %d\n", (uint32_t)ctx->r6, (uint32_t)(ctx->r7 & 0xFF),
                (int8_t)MEM_BU(0x10, ctx->r29));
    }
    if (sound_test_track() < 0) {
        return 0;
    }
    const gpr music = MEM_W(0, (gpr)(int32_t)0x800D809C);  // gGlobals.titleSplashVars.introMusic (Borg12Header*)
    const uint32_t mine = music != 0 ? (uint32_t)MEM_W(8, music) + 8 : 0;  // &header->dat->sub
    return (uint32_t)ctx->r6 != mine;
}

extern "C" void aidyn_sfx_play(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        fprintf(stderr, "sfx play: borg12 %u\n", (uint32_t)ctx->r5);
    }
}

extern "C" void aidyn_sfx_attack_vol(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        uint32_t bits = (uint32_t)MEM_W(0, (gpr)(int32_t)0x800DA0E0);  // gGlobals.VolSFX (gGlobals + 0x2058)
        float vol;
        memcpy(&vol, &bits, 4);
        fprintf(stderr, "sfx attack: actor %08X anim %u frame %u/%u vol8 %u VolSFX %.2f\n", (uint32_t)ctx->r18,
                (uint32_t)(ctx->r12 & 0xFFFF), (uint32_t)(ctx->r11 & 0xFFFF), (uint32_t)(ctx->r10 & 0xFFFF),
                (uint32_t)(ctx->r17 & 0xFF), vol);
    }
}

extern "C" void aidyn_sfx_attack_pick(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        fprintf(stderr, "sfx attack match: actor %08X sound %u\n", (uint32_t)ctx->r18, (uint32_t)ctx->r2);
    }
}

// ---- Button prompts ----
// Controls card: while walking the field (gGlobals.gameStateA 1) or in combat (2), a see-through dark card at the left
// edge lists what the buttons do there (top left in the field; bottom left in combat, clear of the enemies' column).
// LB (N64 L; keyboard Q) shows and hides it in those two modes and isn't passed to the game there (it only turns the
// pause menu's pages; in combat it was a second End turn), nor is View (C-left: there it only cycled combat targets,
// which the stick and RT still do; in the field it quicksaves); src/main/input.cpp, saved as settings.json
// graphics.controls_card. Page prompts: the pause pages' arrows (SliderArrowL / LR / R, told apart by their pixels,
// at the bottom of the page, and the one painted into the map page's background) get LB and RB at the ends they
// point to. Both are drawn at the end of the frame (Graphics__EndList) with their own
// render state, from the pictures in src/game/branding_images.h (tools/branding/make_branding.py, which also puts
// their full-resolution versions in the rechronicled pack).
namespace {
    constexpr uint32_t kGameStateA = 0x800D80C8;  // gGlobals.gameStateA (u8)

    int branding_index(const char* name) {
        return title_image(name);
    }

    // Texels from the left edge to the last column with any opaque texel.
    uint32_t ink_width(int index) {
        const auto& img = aidyn::branding::kImages[index];
        uint32_t w = 0;
        for (uint32_t y = 0; y < img.h; y++) {
            for (uint32_t x = 0; x < img.w; x++) {
                if (img.rgba16[y * img.w + x] & 1) {
                    w = std::max(w, x + 1);
                }
            }
        }
        // A keyboard twin's blank slots are part of it (a cap is drawn in each): to the last slot's end.
        for (const auto& k : aidyn::branding::kKeyed) {
            if (std::strcmp(k.name, img.name) == 0) {
                for (int n = 0; n < k.n; n++) w = std::max<uint32_t>(w, k.slots[n].x + k.slots[n].w);
            }
        }
        return w;
    }

    // One-cycle, filtered, blended; texture colour, texture alpha times the primitive colour's alpha.
    uint8_t prompt_alpha_now = 255;  // the last prompt_state's alpha: the key caps drawn into a picture's slots use it
    gpr prompt_state(uint8_t* rdram, gpr g, uint8_t alpha) {
        prompt_alpha_now = alpha;
        auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
        cmd(0xE7000000, 0);                  // gDPPipeSync
        cmd(0xE3000A01, 0x00000000);         // gDPSetCycleType(G_CYC_1CYCLE)
        cmd(0xE3000C00, 0x00000000);         // gDPSetTexturePersp(G_TP_NONE)
        cmd(0xE3001001, 0x00000000);         // gDPSetTextureLUT(G_TT_NONE)
        cmd(0xE3001201, 0x00002000);         // gDPSetTextureFilter(G_TF_BILERP)
        cmd(0xE2001E01, 0x00000000);         // gDPSetAlphaCompare(G_AC_NONE)
        cmd(0xE200001C, 0x00504240);         // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2)
        cmd(0xFC119623, 0xFF2FFFFF);         // gDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM)
        cmd(0xFA000000, 0xFFFFFF00u | alpha); // gDPSetPrimColor(0, 0, 255, 255, 255, alpha)
        return g;
    }

    struct PageArrow {
        bool left, right;
        float x, y, w, h, scale;  // screen pixels; pixels per texel
        uint8_t alpha;
    };
    std::vector<PageArrow> page_arrows;  // drawn this frame (game thread)

    // Treasure hints: the treasure screen (a chest, sack or pile: WidgetBarter with invType 1) shows what A and B do
    // and how to switch lists, under its "Found: ... gold" line, like the inventory page's own "DETAILS [X]". The
    // screen is up while its title (BORG8_TitleTreasure) is drawn; the hints follow the title's place, scale and fade.
    // A takes the item from the treasure list and puts one back from the party's inventory: which list is active is
    // the barter widget's description pointer (+0x88) being its item list (+0x80); the widget is the one most
    // recently made (constructor func_8003CB68, aidyn.us.toml hook).
    struct TitleSeen {
        bool seen = false;
        bool shop = false;  // the barter title: a shop's (Buy) rather than treasure
        float x = 0, y = 0, sx = 0, sy = 0;  // screen pixels; pixels per texel
        uint8_t alpha = 0;
    };
    TitleSeen treasure_title;  // this frame (game thread)
    gpr barter_widget = 0;

    // Inventory page hints: the pause menu's inventory page (its background, PauseBGInventory, told by the texels of its
    // painted DETAILS) gets a row at its bottom left: what A does to the highlighted item (Equip, Unequip or Use, read
    // from the inventory list) and the D-pad switching the right-hand list (Inventory, Spells, Skills, Stats); and,
    // with more than one in the party, View and RT beside the portraits (previous / next member). After a battle the
    // page trains with experience instead: "Train [A]" and the lists inside the Train panel. The lists widget
    // (DollMenuLists) is the one most recently set up (func_80030C50, aidyn.us.toml hook): menus[4] +0x7C, menuIndex
    // +0x90, menuCount +0x92, invMenu +0x94, expCost +0xA8 (only after a battle). An inventory list's highlighted row:
    // scrollMenu +0x80 -> substruct +0x40 -> items +0x18, highlight +0x1E, numChoices +0x20; a row (SMIItem): item ID
    // +0x5E, the portrait of whoever has it equipped +0x84; the item's type is the ID's upper byte (itemIDList.h).
    TitleSeen inventory_page;  // the page's texel (0, 0) on screen, this frame
    gpr doll_lists = 0;

    // Combat menu hints. In combat the items (Y) and spells and skills (X) are menus held open: the game keeps one
    // while its button is held (combat_control_case_0x16 / 0x17) and, when it is let go, uses the highlighted entry
    // (the menu's destructor calls its A function); A itself does nothing there. So under the box: "Choose" (D-pad or
    // stick) and "Release to use [Y]" or "[X]". The menu is gCombatCVertMenu (0x800E3170); a real list, not the "No
    // potions..." message stored there too, is the one Combat_menu_C_Up / combat_menu_C_down just made (aidyn.us.toml
    // hooks where they store it). Its place is the widget's: posX +0x60, posY +0x62, width +0x74, height +0x72.
    gpr combat_menu = 0;
    int combat_menu_kind = 0;  // 1 items (Y), 0 spells and skills (X)
    struct { bool seen; int x, y, w, h; } combat_menu_box{};
    struct { bool seen; int x, y, w, h; } spell_info_box{};  // the held menu's box this frame, for draw_spell_info

    bool heap_pointer(gpr p) {
        const uint32_t u = (uint32_t)p;
        return u >= 0x80000400u && u < 0x80E6A000u && (u & 3) == 0;
    }

    // The A hint for the inventory list's highlighted item, or -1 (nothing A does to it: misc and key items).
    int inventory_a_hint(uint8_t* rdram, gpr list) {
        const gpr menu = MEM_W(0x80, list);
        if (!heap_pointer(menu)) return -1;
        const gpr sub = MEM_W(0x40, menu);
        if (!heap_pointer(sub)) return -1;
        const gpr items = MEM_W(0x18, sub);
        const uint32_t highlight = MEM_HU(0x1E, sub), count = MEM_HU(0x20, sub);
        if (!heap_pointer(items) || highlight >= count) return -1;
        const gpr row = MEM_W(4 * highlight, items);
        if (!heap_pointer(row)) return -1;
        if (MEM_W(0x84, row) != 0) return branding_index("hint_unequip");
        switch (MEM_HU(0x5E, row) >> 8) {
            case 16: return branding_index("hint_use");  // DB_POTION
            case 5: case 6: case 7: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 17: case 19:
                return branding_index("hint_equip");      // armour, shield, weapon, gear, scroll, amulet
            default: return -1;
        }
    }

    // Draws a row of hint pictures from page texel (x, y), 6 texels apart; returns the screen x after it.
    gpr draw_hint_row(uint8_t* rdram, gpr g, const TitleSeen& origin, const int* row, int n, float x, float y,
                      bool centred) {
        float width = 0.0f;
        for (int k = 0; k < n; k++) width += row[k] < 0 ? 0.0f : ink_width(row[k]) * kPromptScale + 6.0f;
        if (centred) x -= (width - 6.0f) / 2.0f;
        g = prompt_state(rdram, g, origin.alpha);
        for (int k = 0; k < n; k++) {
            if (row[k] < 0) continue;
            g = draw_prompt(rdram, g, row[k], origin.x + x * origin.sx, origin.y + y * origin.sy, origin.sx, origin.sy);
            x += ink_width(row[k]) * kPromptScale + 6.0f;
        }
        return g;
    }

    struct Card {
        const char* rows[9];
        bool bottom;
    };
    const Card kFieldCard = { { "ctl_walk", "ctl_camera", "ctl_talk", "ctl_party", "ctl_sneak", "ctl_ff", "ctl_quicksave",
                              "ctl_menu", nullptr },
                              false };
    const Card kCombatCard = { { "ctl_move", "ctl_attack", "ctl_magic", "ctl_endturn", "ctl_combatcam", "ctl_ff",
                                 "ctl_hide", nullptr }, true };

    gpr draw_card(uint8_t* rdram, gpr g, const Card& card) {
        constexpr float kMargin = 6.0f, kPad = 2.0f, kMessagePortraitX = 25.0f, kMessageTop = 178.0f;
        int rows[9], n = 0;
        uint32_t width = 0, height = 0;
        for (const char* name : card.rows) {
            if (name == nullptr) {
                break;
            }
            const int i = branding_index(name);
            if (i < 0) {
                continue;
            }
            rows[n++] = i;
            width = std::max(width, ink_width(i));
            height += aidyn::branding::kImages[i].h;
        }
        const int panel = branding_index("ctl_panel");
        if (n == 0 || panel < 0) {
            return g;
        }
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        const float x0 = kMargin;
        float y0 = card.bottom ? 240.0f - kMargin - height - 2 * kPad : kMargin;
        // Combat's message box (centred, its portrait from x 25 to y 179 of the middle 4:3) leaves the bottom left
        // corner free on a wide picture only; on a narrower one the card goes above the box.
        const float room = kMessagePortraitX + (240.0f * view_aspect() - 320.0f) / 2.0f;
        if (card.bottom && x0 + width + 2 * kPad + 3.0f > room) {
            y0 = kMessageTop - 3.0f - height - 2 * kPad;
        }
        const auto& p = aidyn::branding::kImages[panel];
        if (card.bottom) {
            combat_card_top = y0;  // the combat log goes above it
        }
        (void)p;  // no backing (spitzdano): the rows' letters have their own dark edge
        g = hud_begin(rdram, g, false);
        g = prompt_state(rdram, g, 255);
        float y = y0 + kPad;
        for (int k = 0; k < n; k++) {
            g = draw_rgba16(rdram, g, rows[k], (x0 + kPad) * hs, y * vs, hs, vs);
            y += aidyn::branding::kImages[rows[k]].h;
        }
        return hud_end(rdram, g);
    }

    int play_mode(uint8_t* rdram) {
        const uint8_t state = MEM_BU(0, (gpr)(int32_t)kGameStateA);
        static const bool log = std::getenv("AIDYN_MODE_LOG") != nullptr;
        static uint8_t last = 0xFF;
        if (log && state != last) {
            fprintf(stderr, "game state %u\n", state);
            last = state;
        }
        return state == 1 ? 1 : state == 2 ? 2 : 0;
    }
}

// N64BorgImageDraw's entry: note a pause page arrow (a1 the image, a2/a3 x/y, sp+0x10.. xOff yOff h v xScale yScale r
// g b a).
// The map page's arrow is painted into its background (PauseBGMap, 320x240 CI8: a left arrow at texels 142..177,
// 196..203, told by those texels): noted at that place when the visible part of the picture includes it.
// The journal (Camp > Journal on the map page) is a book over the map page, whose painted arrow is still drawn under
// it: told by its own background (JournalBG, 320x240 CI8, the FNV hash of its rows 100..103), so that arrow gets no LB.
static bool journal_frame = false;
// The journal's book (the Help book too: a conversation drawn on the same page) was drawn in the last few frames.
static int book_frames = 0;
// The map page's and the journal's texel (0, 0) on screen, scale and fade, this frame (their button hints, #11).
// ---- World map pieces ----
// The map page's land (#26): 18 grey drawings, one per region whose map the party has found, that the game tints brown
// (WidgetMenuWorldMap::ShowMap: SetColor(130, 80, 80)). The redrawn art pack paints them in colour, in place on the
// painted page (tools/hdui/map_pieces.py; it lists MapPieces in the pack's aidyn-pages.txt): while that pack is on,
// they're drawn untinted. Hooked at WidgetMenuWorldMap::Tick's entry (a0 the page: its pieces +0x8C, 8 bytes each,
// the WidgetBorg8 first; their count +0x90; a widget's colour at +0x58), every frame the page shows, so F2 brings the
// game's own tint back at once.
extern "C" void aidyn_world_map_tick(uint8_t* rdram, recomp_context* ctx) {
    static const bool painted = [] {
        std::vector<uint8_t> bytes;
        return aidyn::packs::read("rechronicled-art", "aidyn-pages.txt", bytes) &&
               std::string(bytes.begin(), bytes.end()).find("MapPieces") != std::string::npos;
    }();
    const gpr map = ctx->r4;
    const gpr pieces = heap_pointer(map) ? MEM_W(0x8C, map) : 0;
    if (!heap_pointer(pieces)) {
        return;
    }
    const bool on = painted && redesigned_card();
    const uint32_t count = std::min<uint32_t>(MEM_HU(0x90, map), 32);
    for (uint32_t i = 0; i < count; i++) {
        const gpr img = MEM_W(8 * i, pieces);
        if (heap_pointer(img)) {
            MEM_B(0x58, img) = (int8_t)(on ? 255 : 130);
            MEM_B(0x59, img) = (int8_t)(on ? 255 : 80);
            MEM_B(0x5A, img) = (int8_t)(on ? 255 : 80);
        }
    }
}

static TitleSeen map_page, journal_page;

// A 320x240 page's texel (0, 0) on screen, scale and fade, from N64BorgImageDraw's arguments.
static TitleSeen page_origin(uint8_t* rdram, recomp_context* ctx) {
    const gpr sp = ctx->r29;
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    TitleSeen o;
    o.seen = true;
    o.x = (x - (float)(int)MEM_HU(0x12, sp) * xs) * hs;
    o.y = (y - (float)(int)MEM_HU(0x16, sp) * ys) * vs;
    o.sx = xs * hs;
    o.sy = ys * vs;
    o.alpha = MEM_BU(0x37, sp);
    return o;
}

static void note_journal(uint8_t* rdram, recomp_context* ctx, gpr img) {
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 100 * 320; i < 104 * 320; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    if (hash == 0x5E583A6EAD9F819CULL) {
        journal_frame = true;
        if (!journal_page.seen) {
            journal_page = page_origin(rdram, ctx);  // the first (whole) draw: the game redraws strips of it after
        }
    }
}

static void note_map_page_arrow(uint8_t* rdram, recomp_context* ctx, gpr img) {
    constexpr uint32_t kX = 142, kY = 196, kW = 36, kH = 8;
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t y = kY; y < kY + kH; y++) {
        for (uint32_t x = kX; x < kX + kW; x++) {
            hash = (hash ^ MEM_BU(y * 320 + x, pixels)) * 0x100000001B3ULL;
        }
    }
    const gpr sp = ctx->r29;
    const uint32_t xoff = MEM_HU(0x12, sp), yoff = MEM_HU(0x16, sp), h = MEM_HU(0x1A, sp), v = MEM_HU(0x1E, sp);
    if (hash != 0x0D50127AE284254EULL || xoff > kX || h < kX + kW || yoff > kY || v < kY + kH) {
        return;
    }
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    PageArrow a{};
    a.left = true;
    a.x = (x + (float)(int)(kX - xoff) * xs) * hs;
    a.y = (y + (float)(int)(kY - yoff) * ys) * vs;
    a.w = kW * xs * hs;
    a.h = kH * ys * vs;
    a.scale = xs * hs;
    a.alpha = MEM_BU(0x37, sp);
    if (a.alpha != 0 && page_arrows.size() < 8) {
        page_arrows.push_back(a);
    }
    map_page = page_origin(rdram, ctx);
}

void note_treasure_title(uint8_t* rdram, recomp_context* ctx, gpr img) {
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < 84 * 17; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    if (hash != 0xB8766FCFAC94A81BULL) {  // BORG8_TitleTreasure (CI8, 84x17)
        return;
    }
    treasure_title.shop = false;
    const gpr sp = ctx->r29;
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    treasure_title.seen = true;
    treasure_title.x = x * hs;
    treasure_title.y = y * vs;
    treasure_title.sx = read_f32(rdram, sp + 0x20) * hs;
    treasure_title.sy = read_f32(rdram, sp + 0x24) * vs;
    treasure_title.alpha = MEM_BU(0x37, sp);
}

// PauseBGInventory being drawn (CI8 320x240, told by its painted DETAILS, texels 242..274 x 211..217): the page's
// texel (0, 0) on screen, its scale and fade.
void note_inventory_page(uint8_t* rdram, recomp_context* ctx, gpr img) {
    constexpr uint32_t kX = 242, kY = 211, kW = 33, kH = 7;
    const gpr sp = ctx->r29;
    const uint32_t xoff = MEM_HU(0x12, sp), yoff = MEM_HU(0x16, sp), h = MEM_HU(0x1A, sp), v = MEM_HU(0x1E, sp);
    if (xoff > kX || h < kX + kW || yoff > kY || v < kY + kH) {
        return;  // only the part of the picture with the word in it identifies it
    }
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t y = kY; y < kY + kH; y++) {
        for (uint32_t x = kX; x < kX + kW; x++) {
            hash = (hash ^ MEM_BU(y * 320 + x, pixels)) * 0x100000001B3ULL;
        }
    }
    if (hash != 0x2E5ED04143174A58ULL) {
        return;
    }
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    inventory_page.seen = true;
    inventory_page.x = (x - (float)(int)xoff * xs) * hs;
    inventory_page.y = (y - (float)(int)yoff * ys) * vs;
    inventory_page.sx = xs * hs;
    inventory_page.sy = ys * vs;
    inventory_page.alpha = MEM_BU(0x37, sp);
}

// Combat_menu_C_Up / combat_menu_C_down: 0 at their entry, then the list they made (v0) as they store it in
// gCombatCVertMenu (the "No potions..." path never stores one, so its message gets no hints).
extern "C" void aidyn_combat_menu_made(gpr menu, int items) {
    combat_menu = menu;
    combat_menu_kind = items;
}

// Combat_RenderCVertMenu's entry: the held menu is drawn this frame. Its own place says nothing (0 by 0): the box
// is the menu's nine frame pieces (WidgetChoiceDia: WidgetBorg8 pointers +0x80..+0xA0, each placed on screen), their
// union; failing those, its scroll list (+0x7C: posX +0x60, posY +0x62, boundX1 +0x66, boundY1 +0x6A).
extern "C" void aidyn_combat_menu_drawn(uint8_t* rdram) {
    const gpr menu = MEM_W(0, (gpr)(int32_t)0x800E3170);
    if (menu == 0 || menu != combat_menu) {
        return;
    }
    int x0 = 10000, y0 = 10000, x1 = -10000, y1 = -10000;
    for (int k = 0; k < 9; k++) {
        const gpr piece = MEM_W(0x80 + 4 * k, menu);
        if (!heap_pointer(piece)) continue;
        const int px = (int16_t)MEM_HU(0x60, piece), py = (int16_t)MEM_HU(0x62, piece);
        const int pw = MEM_HU(0x74, piece), ph = MEM_HU(0x72, piece);
        if (pw == 0 || ph == 0) continue;
        x0 = std::min(x0, px); y0 = std::min(y0, py); x1 = std::max(x1, px + pw); y1 = std::max(y1, py + ph);
    }
    const gpr list = MEM_W(0x7C, menu);
    if (x1 <= x0 && heap_pointer(list)) {
        x0 = (int16_t)MEM_HU(0x60, list); y0 = (int16_t)MEM_HU(0x62, list);
        x1 = (int16_t)MEM_HU(0x66, list); y1 = (int16_t)MEM_HU(0x6A, list);
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    combat_menu_box = { true, x0, y0, x1 - x0, y1 - y0 };
    combat_menu_open = true;
    spell_info_box = { true, x0, y0, x1 - x0, y1 - y0 };
    static const bool log = std::getenv("AIDYN_HINT_LOG") != nullptr;
    static int last[4] = {};
    const int now[4] = { x0, y0, x1 - x0, y1 - y0 };
    if (log && std::memcmp(now, last, sizeof(now)) != 0) {
        fprintf(stderr, "combat menu (%s): box x %d y %d w %d h %d; list", combat_menu_kind ? "items" : "spells", now[0],
                now[1], now[2], now[3]);
        if (heap_pointer(list)) {
            fprintf(stderr, " x %d y %d to %d %d", (int16_t)MEM_HU(0x60, list), (int16_t)MEM_HU(0x62, list),
                    (int16_t)MEM_HU(0x66, list), (int16_t)MEM_HU(0x6A, list));
        }
        fprintf(stderr, "\n");
        std::memcpy(last, now, sizeof(now));
    }
}

// Aiming marker. Aiming a bow or a thrown weapon (combat state 18) or an area spell (11), the stick moves a marker on
// the ground (gCombatP->SpellMarkerPos) and whoever stands within 1 unit of it is the target. Its step (func_8008465C,
// FUN_8008dcfc in the decomp: the stick's direction scaled by 0.8, once a frame, however far the stick is pushed) was
// paced for the N64's combat at about 20 frames a second; at 60 it crossed a target in two or three frames.
// aidyn.us.toml hooks the function's entry (the controller sample, a0) and its Vec2Scale call (the step, a1): a third
// of it, times how far the stick is pushed (at least a fifth), so a light push aims finely. In state 18 the D-pad's
// left / right also jump to the previous / next target, as C-left / C-right do there (combat_control_case_0x12,
// func_80084BA0, a0 = the sample, pressed at +8; RT is C-right, and View, the C-left, doesn't reach the game in
// combat).
namespace {
    gpr marker_input = 0;
}

extern "C" void aidyn_marker_input(gpr cont) {
    marker_input = cont;
}

extern "C" uint32_t aidyn_marker_step(uint8_t* rdram, uint32_t step_bits) {
    float step;
    std::memcpy(&step, &step_bits, 4);
    float push = 1.0f;
    if (heap_pointer(marker_input)) {
        const float x = read_f32(rdram, marker_input), y = read_f32(rdram, marker_input + 4);  // joy_x, joy_y
        push = std::clamp(std::sqrt(x * x + y * y), 0.2f, 1.0f);
    }
    step *= push / 3.0f;
    std::memcpy(&step_bits, &step, 4);
    return step_bits;
}

extern "C" void aidyn_ranged_dpad(uint8_t* rdram, gpr cont) {
    uint32_t pressed = (uint32_t)MEM_W(8, cont);
    if (pressed & 0x0200) pressed |= 0x0002;  // D-left: C-left
    if (pressed & 0x0100) pressed |= 0x0001;  // D-right: C-right
    MEM_W(8, cont) = (int32_t)pressed;
}

// DollMenuLists::InitMenu (func_80030C50; a0 = this): the inventory page's lists, just set up.
extern "C" void aidyn_doll_lists(gpr lists) {
    doll_lists = lists;
}

// WidgetBarter's constructor (func_8003CB68; s2 = this): the treasure or shop screen just made.
extern "C" void aidyn_barter_made(gpr widget) {
    barter_widget = widget;
}

// A shop's title (BORG8_TitleBuy, CI8 33x17) over the same panel: the barter hints as a shop's. Placed like the
// treasure title (centred on x 90, its bottom at y 85), so the row is laid out from the treasure title's place.
void note_shop_title(uint8_t* rdram, recomp_context* ctx, gpr img) {
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < 33 * 17; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    if (hash != 0x79F2EBA0EF7D2C2AULL) {
        return;
    }
    const gpr sp = ctx->r29;
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float sx = read_f32(rdram, sp + 0x20) * hs;
    treasure_title.seen = true;
    treasure_title.shop = true;
    treasure_title.x = x * hs - (84.0f - 33.0f) / 2.0f * sx;  // where an 84-wide treasure title would start
    treasure_title.y = y * vs;
    treasure_title.sx = sx;
    treasure_title.sy = read_f32(rdram, sp + 0x24) * vs;
    treasure_title.alpha = MEM_BU(0x37, sp);
}

void note_page_arrow(uint8_t* rdram, recomp_context* ctx) {
    const gpr img = ctx->r5;
    if (img != 0 && MEM_HU(0x08, img) == 4 && MEM_HU(0x0A, img) == 84 && MEM_HU(0x0C, img) == 17) {
        note_treasure_title(rdram, ctx, img);
        return;
    }
    if (img != 0 && MEM_HU(0x08, img) == 4 && MEM_HU(0x0A, img) == 33 && MEM_HU(0x0C, img) == 17) {
        note_shop_title(rdram, ctx, img);
        return;
    }
    if (img != 0 && MEM_HU(0x08, img) == 4 && MEM_HU(0x0A, img) == 320 && MEM_HU(0x0C, img) == 240) {
        note_map_page_arrow(rdram, ctx, img);
        note_inventory_page(rdram, ctx, img);
        note_journal(rdram, ctx, img);
        return;
    }
    if (img == 0 || MEM_HU(0x08, img) != 5 || MEM_HU(0x0C, img) != 8) {
        return;
    }
    const uint32_t w = MEM_HU(0x0A, img);
    if (w != 36 && w != 47) {
        return;
    }
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < w * 8; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    PageArrow a{};
    if (hash == 0xB250FD36B7D00719ULL) a.left = true;                          // SliderArrowL
    else if (hash == 0x46AC41CADC3C3186ULL) a.left = a.right = true;           // SliderArrowLR
    else if (hash == 0x30B0CB2D5BAD17CDULL) a.right = true;                    // SliderArrowR
    else return;
    const gpr sp = ctx->r29;
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    a.x = x * hs;
    a.y = y * vs;
    a.w = (float)(int)(MEM_HU(0x1A, sp) - MEM_HU(0x12, sp)) * xs * hs;
    a.h = (float)(int)(MEM_HU(0x1E, sp) - MEM_HU(0x16, sp)) * ys * vs;
    a.scale = xs * hs;
    a.alpha = MEM_BU(0x37, sp);
    // Only a page's own arrow, at its bottom: the Controller Pak screens use the same arrows on their save slots
    // (y 77), which aren't turned with LB / RB.
    if (y < 180.0f) {
        return;
    }
    if (a.alpha != 0 && page_arrows.size() < 8) {
        page_arrows.push_back(a);
    }
}

namespace aidyn {
    bool redrawn_art();  // src/main/rt64_context.cpp: false while F2 has the redrawn art pack off
}

int conversation_state(uint8_t* rdram);  // "Conversation hint", below

gpr aidyn_prompts(uint8_t* rdram, gpr g) {
    book_frames = journal_frame ? 10 : std::max(0, book_frames - 1);
    aidyn::input::set_dialogue(book_frames > 0 ? 0 : conversation_state(rdram));  // the Help book has its own hints
    if (!aidyn::redrawn_art()) {  // say so, top right under the fast-forward marker
        copy_branding(rdram);
        const int label = branding_index("art_off");
        if (label >= 0) {
            const auto& img = aidyn::branding::kImages[label];
            const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
            g = hud_begin(rdram, g, true);
            g = prompt_state(rdram, g, 255);
            g = draw_rgba16(rdram, g, label, (320 - 8 - img.w) * hs, 22 * vs, hs, vs);
            g = hud_end(rdram, g);
        }
    }
    const int mode = play_mode(rdram);
    aidyn::input::set_card_mode(mode);
    const uint8_t state_a = MEM_BU(0, (gpr)(int32_t)kGameStateA);
    const bool paused = heap_pointer(MEM_W(0, (gpr)(int32_t)0x800D9558));  // gGlobals.BigAssMenu: the pause menu is up
    aidyn::input::set_menu_mode(paused || state_a == 5 || state_a == 7);    // the pause menu, a trainer, a shop
    combat_card_top = 240.0f;
    combat_log_frame = mode == 2 && !aidyn::input::start_held();
    const bool journal = journal_frame;
    if (journal_frame) {
        journal_frame = false;
        page_arrows.clear();  // the map page's arrow, under the journal
        map_page.seen = false;
    }
    if (mode == 0 && page_arrows.empty() && !treasure_title.seen && !inventory_page.seen && !combat_menu_box.seen &&
        !map_page.seen && !journal_page.seen) {
        return g;
    }
    copy_branding(rdram);
    // Not while Menu is held: pausing keeps a picture of the screen as the pause menu's background. Nor while a
    // panel covers the screen: after a fight the game is still in combat while its Train and treasure screens are up,
    // and the combat card showed at the bottom left under them.
    if (mode != 0 && aidyn::input::controls_card() && !aidyn::input::start_held() && !inventory_page.seen &&
        !treasure_title.seen) {
        g = draw_card(rdram, g, mode == 1 ? kFieldCard : kCombatCard);
    }
    if (!aidyn::settings::graphics().button_prompts) {  // the ReChronicled page's Button prompts: off, only the card
        page_arrows.clear();
        treasure_title.seen = false;
        inventory_page.seen = false;
        combat_menu_box.seen = false;
        map_page.seen = false;
        journal_page.seen = false;
        return g;
    }
    const int lb = branding_index("prompt_lb"), rb = branding_index("prompt_rb");
    for (const PageArrow& a : page_arrows) {
        if (lb < 0 || rb < 0) {
            break;
        }
        const auto& icon = aidyn::branding::kImages[lb];
        const float iw = icon.w * a.scale, ih = icon.h * a.scale, y = a.y + (a.h - ih) / 2.0f, gap = a.scale;
        g = prompt_state(rdram, g, a.alpha);
        if (a.left) {
            g = draw_rgba16(rdram, g, lb, a.x - iw - gap, y, a.scale, a.scale);
        }
        if (a.right) {
            g = draw_rgba16(rdram, g, rb, a.x + a.w + gap, y, a.scale, a.scale);
        }
    }
    page_arrows.clear();
    if (treasure_title.seen && treasure_title.alpha != 0) {
        // Under "Found: ... gold" (y 169): the row centred on the treasure panel (x 80; its title sits at 48, 68),
        // 6 texels between hints.
        // A shop: A buys from its list and sells from the party's; Y appraises (the shop's list only); B leaves. Four
        // don't fit the panel's width, so a shop has two rows: what A (and Y) do, then Switch and Leave.
        const bool loot_list = barter_widget == 0 || MEM_W(0x88, barter_widget) == MEM_W(0x80, barter_widget);
        const bool shop = treasure_title.shop;
        const TitleSeen& t = treasure_title;
        auto draw_row = [&](std::initializer_list<int> row, float row_y) {
            float width = 0.0f;
            for (int i : row) {
                width += i < 0 ? 0.0f : ink_width(i) * kPromptScale + 6.0f;
            }
            float x = t.x + (80.0f - 48.0f - (width - 6.0f) / 2.0f) * t.sx;
            const float y = t.y + (row_y - 68.0f) * t.sy;
            for (int i : row) {
                if (i >= 0) {
                    g = draw_prompt(rdram, g, i, x, y, t.sx, t.sy);
                    x += (ink_width(i) * kPromptScale + 6.0f) * t.sx;
                }
            }
        };
        g = prompt_state(rdram, g, t.alpha);
        if (shop) {
            draw_row({ branding_index(loot_list ? "hint_buy" : "hint_sell"),
                       loot_list ? branding_index("hint_appraise") : -1 }, 184.0f);
            draw_row({ branding_index("hint_switch"), branding_index("hint_leave") }, 197.0f);
        } else {
            draw_row({ branding_index(loot_list ? "hint_take" : "hint_putback"), branding_index("hint_leave"),
                       branding_index("hint_switch") }, 192.0f);
        }
    }
    if (inventory_page.seen && inventory_page.alpha != 0 && !treasure_title.seen && heap_pointer(doll_lists)) {
        const gpr lists = doll_lists;
        const uint32_t index = MEM_BU(0x90, lists), count = MEM_BU(0x92, lists);
        const gpr shown = index < 4 ? MEM_W(0x7C + 4 * index, lists) : 0, inventory = MEM_W(0x94, lists);
        const int lists_hint = count > 1 ? branding_index("hint_lists") : -1;
        if (MEM_W(0xA8, lists) != 0) {
            // After a battle: inside the Train panel (x 0..160), above its experience lines (y 187 on).
            const int row[] = { branding_index("hint_train"), lists_hint };
            g = draw_hint_row(rdram, g, inventory_page, row, 2, 80.0f, 174.0f, true);
        }
        else {
            // Near the row of the page arrow (y 207), 4 texels higher, with the repainted page's DETAILS (art/pages):
            // at y 208 they hung off its torn bottom edge. A at the bottom left, under the paper doll and clear of the
            // LB prompt (x 114 on); the lists between the RB prompt (to x 185) and DETAILS (x 242 on).
            int a = shown != 0 && shown == inventory && heap_pointer(inventory) ? inventory_a_hint(rdram, inventory) : -1;
            // The skills list: A on Alchemist, Healer or Mechanic opens its menu (potions, healing, armour), in the
            // pause menu only (gGlobals.pauseMenuState 7; a trainer's A trains). Those rows' value (WidgetSkillInfo
            // skillVal +0x84) has the A function (get_crafting_menu).
            const gpr skills = MEM_W(0x9C, lists);
            if (a < 0 && shown != 0 && shown == skills && MEM_BU(0, (gpr)(int32_t)0x800D9550) == 7) {
                const gpr scroll = MEM_W(0x80, skills);
                const gpr sub = heap_pointer(scroll) ? MEM_W(0x40, scroll) : 0;
                const gpr rows = heap_pointer(sub) ? MEM_W(0x18, sub) : 0;
                const uint32_t hl = heap_pointer(sub) ? MEM_HU(0x1E, sub) : 0, n = heap_pointer(sub) ? MEM_HU(0x20, sub) : 0;
                const gpr row = heap_pointer(rows) && hl < n ? MEM_W(4 * hl, rows) : 0;
                const gpr value = heap_pointer(row) ? MEM_W(0x84, row) : 0;
                if (heap_pointer(value) && MEM_W(0x18, value) != 0) {
                    a = branding_index("hint_use");
                }
            }
            g = draw_hint_row(rdram, g, inventory_page, &a, 1, 8.0f, 204.0f, false);
            g = draw_hint_row(rdram, g, inventory_page, &lists_hint, 1, 193.0f, 204.0f, false);
        }
        // DETAILS and its X are painted into the page (art/pages, tools/hdui/scroll_pages.py: the button 9.4 texels square at
        // 282.6, 205.75, 85% of the game's): with the keyboard in use a cap covers it, centred on it.
        if (aidyn::input::keyboard_last()) {
            const TitleSeen& o = inventory_page;
            g = prompt_state(rdram, g, o.alpha);
            g = draw_button_cap(rdram, g, "CD", std::round(o.x + 281.8f * o.sx), std::round(o.y + 205.75f * o.sy), o.sx, o.sy, 13.0f * kPromptScale,
                                11.0f * kPromptScale, true);
        }
        // The party switch beside the portraits (their frames span x 21..150, y 25..66).
        const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684);  // gGlobals.party
        if (heap_pointer(party) && MEM_BU(0x24, party) > 1) {     // PartySize
            const int prev = branding_index("party_prev"), next = branding_index("party_next");
            const TitleSeen& o = inventory_page;
            if (prev >= 0 && next >= 0) {
                g = draw_rgba16(rdram, g, prev, o.x + 8.0f * o.sx, o.y + 40.0f * o.sy, o.sx, o.sy);
                g = draw_rgba16(rdram, g, next, o.x + 153.0f * o.sx, o.y + 40.0f * o.sy, o.sx, o.sy);
            }
        }
    }
    if (combat_menu_box.seen) {
        // Under the box, centred on it.
        const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
        const TitleSeen origin{ true, false, 0.0f, 0.0f, hs, vs, 255 };
        const int row[] = { branding_index("chint_choose"),
                            branding_index(combat_menu_kind ? "chint_use_items" : "chint_use_spells") };
        g = draw_hint_row(rdram, g, origin, row, 2, combat_menu_box.x + combat_menu_box.w / 2.0f,
                          (float)(combat_menu_box.y + combat_menu_box.h + 3), true);
    }
    // The map page: what A does with Camp and Journal (page texels 191..257, y 160..188), under them, clear of the LB
    // arrow (x 142..177). The journal: up / down scroll it, B closes it; at the foot of the book's page (its text
    // ends at y 210, the page spans x 62..245).
    if (map_page.seen && map_page.alpha != 0 && !journal) {
        const int row[] = { branding_index("hint_open") };
        g = draw_hint_row(rdram, g, map_page, row, 1, 224.0f, 193.0f, true);
    }
    if (journal && journal_page.seen && journal_page.alpha != 0) {
        const int row[] = { branding_index("hint_scroll"), branding_index("hint_close") };
        g = draw_hint_row(rdram, g, journal_page, row, 2, 153.0f, 222.0f, true);
    }
    treasure_title.seen = false;
    inventory_page.seen = false;
    combat_menu_box.seen = false;
    map_page.seen = false;
    journal_page.seen = false;
    return g;
}

// ---- Turn order ----
// Combat's turn order across the top of the screen, like Divinity: Original Sin's and Baldur's Gate 3's. The game
// lists each round's turns when the round starts (CombatTurn::Init, func_8006A16C) in gCombatP->turn (+0x30: count,
// the turn under way, the round's alternate flag at +4, the combatEnts indices at +8; combatEnts at +0x52D0). Every
// living fighter able to act gets turns by base DEX (CombatEntity::DEXCheck, func_8005F4D0, which reads the alternate
// flag): 0 or less one turn every other round; 1 to 17 one; 18 to 24 one, or two every other round; 25 to 29 two;
// 30 to 34 two, or three every other round; 35 and up three. The list is four groups in turn: the DEX 0 fighters, the
// three-turn fighters' first turns, the two- and three-turn fighters' (next) turns, then everyone's last turn; within a
// group the order is rolled. CombatTurn::FUN_8007381c (func_8006A62C) starts the next round when the last turn ends.
// The strip is an ornate bar of pieces cut from spitzdano's concept (tools/branding/turn_order.py; branding_images.h has
// each piece's picture, the bar's middle row and a slot's portrait window): the left end with this round's plate
// ("Round" and its number written on it), the rest of the round from the fighter whose turn it is (a glowing gold
// frame, the others bronze; anyone dead or fled since is left out), the divider, the next round's plate and turns
// (a little darker), the right end. The fighters targeted get a silver frame and flash ("Target flash", above). The next round's order
// is shown exactly: the order within each group is
// rolled a round ahead (turn_rank) and, right after Init lists that round, its groups are put in that order (Init's
// own rolls still happen, so the game's random numbers are unchanged). A crowded round shrinks the bar, then
// ends with "+N" (on a plate) for the turns that still don't fit. aidyn.us.toml hooks Init's two calls (the battle's start in
// Combat_InitEncounter: round 1; FUN_8007381c: the next) and the end of the combat HUD (func_800863E4 after
// CombatDrawHealthbar, so menus and the pause menu cover it). The portraits are the combat portraits' images
// (gCombatPortraits) drawn with the game's Borg8_DrawSimple, so the redrawn art replaces them. In widescreen with
// hud_at_edges the strip may span the picture between the two columns at its edges, else the middle 4:3 between them.
// settings.json graphics.turn_order (off: no strip and Init's own order). AIDYN_TURN_LOG=1 prints each round's list,
// the turn, the combat state and the targets; AIDYN_TURN_TEST=n repeats the shown turns up to n (to look at a crowded strip).
extern "C" void Entity__isDead(uint8_t* rdram, recomp_context* ctx);
extern "C" void CombatEntity__Flag6(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8005F024(uint8_t* rdram, recomp_context* ctx);  // CombatEntity::Flag1: takes turns
extern "C" void func_8005F4D0(uint8_t* rdram, recomp_context* ctx);  // CombatEntity::DEXCheck
extern "C" void Borg8_DrawSimple(uint8_t* rdram, recomp_context* ctx);

#include <numeric>
#include <random>

namespace {
    constexpr int kEntCount = 0x01, kPartyCount = 0x03, kTurn = 0x30, kCombatEnts = 0x52D0;  // CombatStruct
    constexpr int kCharSheet = 0x40;  // CombatEntity.charSheetP
    constexpr int kMaxEnts = 32;
    int turn_round = 0;               // this battle's round, from 1
    uint8_t turn_rank[kMaxEnts];      // the next round's order within each group: lower first

    // The groups (0..3) a DEXCheck type's turns go in, in order: -1 ends.
    constexpr int kTurnGroups[4][4] = { { 0, -1 }, { 3, -1 }, { 2, 3, -1 }, { 1, 2, 3, -1 } };

    // Units of the 320x240 screen: the bar's middle row; the pieces are drawn kScale units a texel (a portrait slot 18
    // wide), down to kSmallestScale for a crowded round.
    constexpr float kCentreY = 16.0f, kScale = 1.0f, kSmallestScale = 0.72f;

    struct TurnEntry {
        gpr ent, image;
        bool party, current, next;
        int index;  // into combatEnts
    };

    void roll_turn_rank() {
        static std::mt19937 rng{ std::random_device{}() };
        std::iota(std::begin(turn_rank), std::end(turn_rank), 0);
        std::shuffle(std::begin(turn_rank), std::end(turn_rank), rng);
    }

    uint32_t f32_bits(float f) {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    }

    // A game function with arguments past a3: those go at sp+0x10 on, below the hooked function's frame.
    uint32_t call_game_stack(uint8_t* rdram, recomp_context* ctx, void (*fn)(uint8_t*, recomp_context*), uint32_t a0,
                             uint32_t a1, uint32_t a2, uint32_t a3, std::initializer_list<uint32_t> stack) {
        const gpr sp = ctx->r29;
        ctx->r29 = sp - 0x40;
        int k = 0;
        for (uint32_t v : stack) {
            MEM_W(0x10 + 4 * k++, ctx->r29) = (int32_t)v;
        }
        const uint32_t v0 = call_game(rdram, ctx, fn, a0, a1, a2, a3);
        ctx->r29 = sp;
        return v0;
    }

    bool fighting(uint8_t* rdram, recomp_context* ctx, gpr ent) {
        const gpr sheet = MEM_W(kCharSheet, ent);
        return heap_pointer(sheet) && (call_game(rdram, ctx, Entity__isDead, (uint32_t)sheet) & 0xFF) == 0 &&
               (call_game(rdram, ctx, CombatEntity__Flag6, (uint32_t)ent) & 0xFF) == 0;
    }

    int dex_type(uint8_t* rdram, recomp_context* ctx, gpr ent) {
        return std::min(3, (int)(call_game(rdram, ctx, func_8005F4D0, (uint32_t)ent) & 0xFF));
    }

    // The combat portrait image of combatEnts[index]: the one made for its entity ID, else (a reinforcement, given
    // one of the two spare portraits, whose owner stays 0) the one given its index.
    gpr turn_image(uint8_t* rdram, int index, gpr ent) {
        const gpr portraits = MEM_W(0, kCombatPortraits);
        const int count = MEM_BU(0, kCombatPortraitCount);
        const gpr sheet = MEM_W(kCharSheet, ent);
        const uint32_t id = heap_pointer(sheet) ? MEM_HU(0, sheet) : 0;
        gpr by_index = 0;
        for (int i = 0; heap_pointer(portraits) && i < count; i++) {
            const gpr p = portraits + i * 0x18, widget = MEM_W(0, p);
            const gpr img = heap_pointer(widget) ? MEM_W(0x6C, widget) : 0;  // BaseWidget.borg8
            if (!heap_pointer(img) || MEM_HU(0x0A, img) == 0 || MEM_HU(0x0C, img) == 0) {
                continue;
            }
            if (id != 0 && MEM_HU(0x08, p) == id) {
                return img;
            }
            if (by_index == 0 && MEM_BU(0x0A, p) == index) {
                by_index = img;
            }
        }
        return by_index;
    }

    // The next round's turns as Init will list them (in turn_rank's order within each group): the alternate flag
    // flipped, which DEXCheck reads from gCombatP.
    void next_round(uint8_t* rdram, recomp_context* ctx, gpr combat, std::vector<TurnEntry>& out) {
        const int ents = std::min<int>(MEM_BU(kEntCount, combat), kMaxEnts), party = MEM_BU(kPartyCount, combat);
        const int32_t alternate = MEM_W(kTurn + 4, combat);
        MEM_W(kTurn + 4, combat) = alternate ? 0 : 1;
        std::vector<int> groups[4];
        for (int i = 0; i < ents; i++) {
            const gpr ent = MEM_W(kCombatEnts + 4 * i, combat);
            if (!heap_pointer(ent) || !fighting(rdram, ctx, ent) || (call_game(rdram, ctx, func_8005F024, (uint32_t)ent) & 0xFF) == 0) {
                continue;
            }
            const int type = dex_type(rdram, ctx, ent);
            if (type == 0 && alternate != 0) {
                continue;  // DEX 0 or less: every other round
            }
            for (const int* g = kTurnGroups[type]; *g >= 0; g++) {
                groups[*g].push_back(i);
            }
        }
        MEM_W(kTurn + 4, combat) = alternate;
        for (auto& group : groups) {
            std::sort(group.begin(), group.end(), [](int a, int b) { return turn_rank[a] < turn_rank[b]; });
            for (int i : group) {
                const gpr ent = MEM_W(kCombatEnts + 4 * i, combat);
                out.push_back({ ent, turn_image(rdram, i, ent), i < party, false, true, i });
            }
        }
    }

    int digit_image(char c) {
        const char name[] = { 't', 'u', 'r', 'n', '_', 'd', c, '\0' };
        return branding_index(name);
    }

    int label_image(char c) {
        return c == '+' ? branding_index("turn_plus") : digit_image(c);
    }

    // A number (or "+N") in texels: each figure's ink, a little closer than its picture's margin.
    float label_texels(const std::string& text) {
        float w = 0.0f;
        for (char c : text) {
            const int i = label_image(c);
            w += i < 0 ? 0.0f : ink_width(i) - 0.5f;
        }
        return w;
    }

    // The figures from x (units, the first one's ink at x), their pictures' tops at y; s units a texel.
    gpr draw_label(uint8_t* rdram, gpr g, const std::string& text, float x, float y, float s, float hs, float vs) {
        x -= 0.5f * s;  // the pictures' left margin
        for (char c : text) {
            const int i = label_image(c);
            if (i < 0) {
                continue;
            }
            g = draw_rgba16(rdram, g, i, x * hs, y * vs, s * hs, s * vs);
            x += (ink_width(i) - 0.5f) * s;
        }
        return g;
    }

    // The strip's pieces, cut from spitzdano's concept (tools/branding/turn_order.py): their pictures (branding_images.h)
    // and, in texels, the bar's middle row and a slot's portrait window.
    struct Piece {
        int image = -1;
        float w = 0.0f, h = 0.0f, mid = 0.0f, x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    };

    const Piece& piece(const char* name) {
        static std::unordered_map<std::string, Piece> pieces;
        if (auto it = pieces.find(name); it != pieces.end()) {
            return it->second;
        }
        Piece p;
        p.image = branding_index(name);
        if (p.image >= 0) {
            p.w = aidyn::branding::kImages[p.image].w;
            p.h = aidyn::branding::kImages[p.image].h;
        }
        for (const auto& t : aidyn::branding::kTurnPieces) {
            if (std::strcmp(t.name, name) == 0) {
                p.mid = t.bar_mid;
                p.x0 = t.x0, p.y0 = t.y0, p.x1 = t.x1, p.y1 = t.y1;
            }
        }
        return pieces.emplace(name, p).first->second;
    }

    // One piece of the strip, left to right: the left end with this round's plate, a portrait slot per turn, "+N" on
    // a plate for the turns that don't fit, the divider, the next round's plate and slots, the right end.
    struct StripItem {
        enum Kind { EndL, Slot, More, Divider, Plate, EndR } kind;
        const TurnEntry* e = nullptr;
        int number = 0;      // EndL, Plate: the round; More: the turns left out
        bool dim = false;    // the next round
        float x = 0.0f;      // left edge, texels from the strip's left
    };

    const Piece& item_piece(const StripItem& it, bool targeted) {
        switch (it.kind) {
            case StripItem::EndL: return piece("to_end_l");
            case StripItem::More:
            case StripItem::Plate: return piece("to_plate");
            case StripItem::Divider: return piece("to_divider");
            case StripItem::EndR: return piece("to_end_r");
            case StripItem::Slot:
            default: return it.e->current ? piece("to_gold") : piece(targeted ? "to_silver" : "to_slot");
        }
    }

    float strip_texels(std::vector<StripItem>& items) {
        float w = 0.0f;
        for (StripItem& it : items) {
            it.x = w;
            w += item_piece(it, false).w;
        }
        return w;
    }

    // The strip within `room` texels; false if not every turn of this round fits (it then has "+N"). The next round
    // follows only if this one fits, with as many of its turns as fit.
    bool lay_out_strip(std::vector<StripItem>& items, const std::vector<TurnEntry>& now,
                       const std::vector<TurnEntry>& later, float room) {
        auto this_round = [&](size_t n) {
            items.clear();
            items.push_back({ StripItem::EndL, nullptr, turn_round });
            for (size_t k = 0; k < n; k++) {
                items.push_back({ StripItem::Slot, &now[k] });
            }
            if (n < now.size()) {
                items.push_back({ StripItem::More, nullptr, (int)(now.size() - n) });
            }
        };
        auto with_end = [&]() {
            items.push_back({ StripItem::EndR });
            const float w = strip_texels(items);
            items.pop_back();
            return w;
        };
        size_t n = now.size();
        this_round(n);
        while (with_end() > room && n > 1) {
            this_round(--n);
        }
        const bool all = n == now.size();
        if (all) {
            const size_t keep = items.size();
            auto next_round_part = [&](size_t m) {
                items.resize(keep);
                items.push_back({ StripItem::Divider });
                items.push_back({ StripItem::Plate, nullptr, turn_round + 1, true });
                for (size_t k = 0; k < m; k++) {
                    items.push_back({ StripItem::Slot, &later[k], 0, true });
                }
                if (m < later.size()) {
                    items.push_back({ StripItem::More, nullptr, (int)(later.size() - m), true });
                }
            };
            size_t m = later.size();
            next_round_part(m);
            while (with_end() > room && m > 0) {
                next_round_part(--m);
            }
            if (m == 0) {
                items.resize(keep);  // no room for any of the next round
            }
        }
        items.push_back({ StripItem::EndR });
        strip_texels(items);
        return all;
    }
}

extern "C" void aidyn_turn_round(int next) {
    turn_round = next ? turn_round + 1 : 1;
    if (!next) {
        roll_turn_rank();
        combat_log_new_battle();
    }
}

// Right after Init listed the next round (func_8006A62C, 0x8006A758): its groups in turn_rank's order, as the strip
// showed them, then the following round rolled.
extern "C" void aidyn_turn_listed(uint8_t* rdram, recomp_context* ctx) {
    const gpr combat = MEM_W(0, kCombatP);
    const gpr list = heap_pointer(combat) ? MEM_W(kTurn + 8, combat) : 0;
    const int count = heap_pointer(list) ? MEM_BU(kTurn, combat) : 0;
    if (aidyn::settings::graphics().turn_order && count > 1 && count <= 3 * kMaxEnts) {
        const int ents = std::min<int>(MEM_BU(kEntCount, combat), kMaxEnts);
        std::vector<int> index(count), group(count);
        int seen[kMaxEnts] = {};
        bool ok = true;
        for (int p = 0; p < count && ok; p++) {
            index[p] = MEM_BU(p, list);
            const gpr ent = index[p] < ents ? MEM_W(kCombatEnts + 4 * index[p], combat) : 0;
            ok = heap_pointer(ent);
            if (ok) {
                const int type = dex_type(rdram, ctx, ent), n = seen[index[p]]++;
                ok = n < 3 && kTurnGroups[type][n] >= 0;
                group[p] = ok ? kTurnGroups[type][n] : 0;
                ok = ok && (p == 0 || group[p] >= group[p - 1]);
            }
        }
        if (ok) {
            for (int a = 0; a < count;) {
                int b = a + 1;
                while (b < count && group[b] == group[a]) b++;
                std::sort(index.begin() + a, index.begin() + b, [](int x, int y) { return turn_rank[x] < turn_rank[y]; });
                a = b;
            }
            for (int p = 0; p < count; p++) {
                MEM_B(p, list) = (int8_t)index[p];
            }
            MEM_W(kCurrentEnt, combat) = MEM_W(kCombatEnts + 4 * index[0], combat);
        }
        else {
            static bool told = false;
            if (!told) {
                fprintf(stderr, "turn order: round %d's list isn't in the groups expected; left in the game's order\n",
                        turn_round);
                told = true;
            }
        }
    }
    roll_turn_rank();
}

extern "C" gpr aidyn_turn_strip(uint8_t* rdram, recomp_context* ctx, gpr g) {
    const gpr combat = MEM_W(0, kCombatP);
    if (!aidyn::settings::graphics().turn_order || !heap_pointer(combat) || turn_round < 1) {
        return g;
    }
    const int count = MEM_BU(kTurn, combat), turn = MEM_BU(kTurn + 1, combat), ents = MEM_BU(kEntCount, combat);
    const gpr list = MEM_W(kTurn + 8, combat), current = MEM_W(kCurrentEnt, combat);
    if (!heap_pointer(list) || turn >= count) {
        return g;
    }
    copy_branding(rdram);
    std::vector<TurnEntry> now;
    for (int k = turn; k < count; k++) {
        const int i = MEM_BU(k, list);
        const gpr ent = i < ents ? MEM_W(kCombatEnts + 4 * i, combat) : 0;
        if (!heap_pointer(ent) || (k > turn && !fighting(rdram, ctx, ent))) {
            continue;
        }
        now.push_back({ ent, turn_image(rdram, i, ent), i < MEM_BU(kPartyCount, combat), k == turn, false, i });
    }
    std::vector<TurnEntry> later;
    next_round(rdram, ctx, combat, later);
    static const bool log = std::getenv("AIDYN_TURN_LOG") != nullptr;
    if (log) {
        static std::string last;
        std::string text = "turn order: round " + std::to_string(turn_round) + ", turn " + std::to_string(turn + 1) +
                           " of " + std::to_string(count) + (MEM_W(kTurn + 4, combat) ? " (alternate)" : "") + ":";
        for (int k = 0; k < count; k++) {
            const int i = MEM_BU(k, list);
            const gpr ent = i < ents ? MEM_W(kCombatEnts + 4 * i, combat) : 0;
            const gpr sheet = heap_pointer(ent) ? MEM_W(kCharSheet, ent) : 0;
            char item[48];
            snprintf(item, sizeof(item), " %s%d:%04X", k == turn ? (ent == current ? "*" : "?") : "", i,
                     heap_pointer(sheet) ? MEM_HU(0, sheet) : 0);
            text += item;
        }
        text += " | state " + std::to_string(MEM_BU(0, kCombatState)) + ", targets";
        for (int list : { kTargetsAttack, kTargetsSpell }) {
            text += list == kTargetsAttack ? " [" : " spell [";
            for (int i = 0; i < 18; i++) {
                if (MEM_W(list + 4 * i, combat) != 0) text += " " + std::to_string(i);
            }
            text += " ]";
        }
        text += " | next:";
        for (const TurnEntry& e : later) {
            char item[16];
            snprintf(item, sizeof(item), " %04X", MEM_HU(0, MEM_W(kCharSheet, e.ent)));
            text += item;
        }
        if (text != last) {
            fprintf(stderr, "%s\n", text.c_str());
            last = text;
        }
    }
    if (now.empty()) {
        return g;
    }
    static const int test = std::getenv("AIDYN_TURN_TEST") ? std::atoi(std::getenv("AIDYN_TURN_TEST")) : 0;
    for (auto* part : { &now, &later }) {
        for (size_t k = 1; !part->empty() && (int)part->size() < test; k++) {
            TurnEntry copy = (*part)[k % part->size()];
            copy.current = false;
            part->push_back(copy);
        }
    }

    // What fits: the room between the two columns (enemies x 21 to 47, the party 270 to 298 of the 4:3; at the
    // picture's edges with hud_at_edges). A crowded round first gets a smaller strip.
    const bool wide = hud_wide();
    float aspect = view_aspect();
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Window && aidyn::window_aspect() > 0.0f) {
        aspect = aidyn::window_aspect();
    }
    const float room = wide ? std::max(216.0f, 240.0f * aspect - 2.0f * 56.0f) : 216.0f;
    std::vector<StripItem> items;
    float sc = kScale;
    while (!lay_out_strip(items, now, later, room / sc) && sc > kSmallestScale + 0.001f) {
        sc = std::max(kSmallestScale, sc - 0.04f);
    }
    const float width = strip_texels(items) * sc;
    const float left = 160.0f - width / 2.0f;
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    // Widescreen: drawn from the picture's centre, every x moved right by shift (rectangle x can't be negative).
    const float shift = wide ? std::max(0.0f, std::ceil(width / 2.0f - 160.0f) + 8.0f) : 0.0f;
    if (wide) {
        GfxCommand c[5];
        gEXEnable(&c[0]);
        gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
        const int off = -(int)std::lround((160.0f + shift) * hs * 4.0f);
        gEXSetRectAlign(&c[3], G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, off, 0, off, 0);
        g = put_cmds(rdram, g, c, 5);
    }
    // Where each piece goes (units, shift included), and a slot's portrait: filling its window, centred, and a texel
    // larger all round so its soft edges go under the frame (the gold frame's window is a little taller than wide: the
    // portrait's sides go under it too).
    struct Placed {
        const StripItem* it;
        const Piece* p;
        float x, y;            // the piece's top left
        float wx0, wy0, wx1, wy1, ps;
        bool targeted;
    };
    std::vector<Placed> placed;
    for (const StripItem& it : items) {
        const bool targeted = it.kind == StripItem::Slot && combat_targeted(rdram, combat, it.e->index);
        const Piece& pc = item_piece(it, targeted);
        Placed q{ &it, &pc, left + it.x * sc + shift, kCentreY - pc.mid * sc };
        q.wx0 = q.x + pc.x0 * sc, q.wy0 = q.y + pc.y0 * sc, q.wx1 = q.x + pc.x1 * sc, q.wy1 = q.y + pc.y1 * sc;
        q.ps = std::max(q.wx1 - q.wx0, q.wy1 - q.wy0) + 1.0f * sc;
        q.targeted = targeted;
        placed.push_back(q);
    }

    // Every window backed solid dark (the portraits' see-through edges, a slot without a picture), a texel into the
    // frame all round.
    g = rect_state(rdram, g);
    for (const Placed& q : placed) {
        if (q.it->kind == StripItem::Slot) {
            g = rect(rdram, g, q.wx0 - sc, q.wy0 - sc, q.wx1 + sc, q.wy1 + sc, 0x141414FF, hs, vs);
        }
    }
    // The portraits, in their windows (the game's Borg8_DrawSimple, so the redrawn art replaces them).
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    for (const Placed& q : placed) {
        if (q.it->kind != StripItem::Slot || q.it->e->image == 0) {
            continue;
        }
        const gpr img = q.it->e->image;
        const float sx = q.ps / MEM_HU(0x0A, img), sy = q.ps / MEM_HU(0x0C, img);
        const float px = q.wx0 + (q.wx1 - q.wx0 - q.ps) / 2.0f, py = q.wy0 + (q.wy1 - q.wy0 - q.ps) / 2.0f;
        const uint32_t shade = q.it->dim ? 175 : 255;
        g = (gpr)(int32_t)call_game_stack(rdram, ctx, Borg8_DrawSimple, (uint32_t)g, (uint32_t)img, f32_bits(px),
                                          f32_bits(py), { f32_bits(sx), f32_bits(sy), shade, shade, shade, 255 });
    }
    // The bar itself: every piece over the portraits (their windows are clear), the next round's a little darker.
    copy_branding(rdram);
    for (const Placed& q : placed) {
        if (q.p->image < 0) {
            continue;
        }
        g = prompt_state(rdram, g, 255);
        put(rdram, g, 0xFA000000, q.it->dim ? 0xBEBEBEFF : 0xFFFFFFFF);
        g = draw_rgba16(rdram, g, q.p->image, q.x * hs, q.y * vs, sc * hs, sc * vs);
    }
    // The targets' pulse.
    g = rect_state(rdram, g);
    for (const Placed& q : placed) {
        if (q.it->kind == StripItem::Slot && q.targeted) {
            g = rect(rdram, g, q.wx0, q.wy0, q.wx1, q.wy1, 0xFFFFFF00u | (uint32_t)(70 * target_pulse()), hs, vs);
        }
    }
    // The words: "Round" over its number on each plate, "+N" on a plate of its own.
    const int word = branding_index("turn_round");
    for (const Placed& q : placed) {
        float centre = 0.0f;
        if (q.it->kind == StripItem::EndL) {
            centre = q.x + 12.4f * sc;
        }
        else if (q.it->kind == StripItem::Plate || q.it->kind == StripItem::More) {
            centre = q.x + 7.2f * sc;
        }
        else {
            continue;
        }
        g = prompt_state(rdram, g, 255);
        put(rdram, g, 0xFA000000, q.it->dim ? 0xC4C4C4FF : 0xFFFFFFFF);
        if (q.it->kind == StripItem::More) {
            const std::string text = "+" + std::to_string(q.it->number);
            g = draw_label(rdram, g, text, centre - label_texels(text) * sc / 2.0f, kCentreY - 5.4f * sc, sc, hs, vs);
            continue;
        }
        if (word >= 0) {
            const float ww = (ink_width(word) - 0.5f) * sc;
            g = draw_rgba16(rdram, g, word, (centre - ww / 2.0f - 0.5f * sc) * hs, (kCentreY - 8.4f * sc) * vs,
                            sc * hs, sc * vs);
        }
        const std::string number = std::to_string(q.it->number);
        g = draw_label(rdram, g, number, centre - label_texels(number) * sc / 2.0f, kCentreY - 3.9f * sc, sc, hs, vs);
    }
    if (wide) {
        g = hud_end(rdram, g);
    }
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}

// ---- Party portraits (field) ----
// While walking about the game draws the party's portraits in a row at the bottom (Portraits::DrawParty, func_8002583C:
// a white glow frame at x 84, 122, 160, 198 and each portrait 30 units square 6 inside it, half see-through; the row
// slides up into place, screen_bottom 0x800DB180 going from 240 to 185 by 4 a frame, and fades with the screen,
// gGlobals.brightness +0x2040). With the redrawn art (spitzdano: like the turn order bar) the row is drawn here instead:
// the turn bar's style at field size (tools/branding/turn_order.py pr_*: Codex's painted ends and slots, art/hud/source/
// party-row, else the bar's own pieces cut larger; a slot per member, 38 units each, so each portrait keeps its place,
// the leader's gold), the portraits opaque in their windows on a dark backing, an empty place an empty slot. gGlobals.party (0x800D9684) Members[0..3], each CharSheet's portrait at +0x44.
namespace {
    constexpr gpr kScreenBottom = (gpr)(int32_t)0x800DB180;  // u16
}

extern "C" int aidyn_party_row(uint8_t* rdram, recomp_context* ctx) {
    if (!redesigned_card()) return 0;
    const Piece& slot = piece("pr_slot");
    const Piece& end_l = piece("pr_end_l");
    const Piece& end_r = piece("pr_end_r");
    if (slot.image < 0 || end_l.image < 0 || end_r.image < 0) return 0;
    // The game's slide (its a1: 1 rising, 0 going down).
    uint16_t bottom = MEM_HU(0, kScreenBottom);
    if (ctx->r5 == 1 && bottom >= 0xBA) bottom -= 4;
    else if (ctx->r5 == 0 && bottom < 0xF0) bottom += 4;
    MEM_H(0, kScreenBottom) = (int16_t)bottom;
    gpr g = ctx->r4;
    const float bright = std::clamp(read_f32(rdram, (gpr)kGlobals + 0x2040), 0.0f, 1.0f);
    const uint32_t shade = (uint32_t)std::lround(255.0f * bright);
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    // Each slot centred where the game's portrait is: x 90 + 38 i, y bottom + 6, 30 square.
    const float wcx = (slot.x0 + slot.x1) / 2.0f, wcy = (slot.y0 + slot.y1) / 2.0f;
    const float cy = bottom + 6.0f + 15.0f, top = cy - wcy;
    auto slot_x = [&](int i) { return 90.0f + 38.0f * i + 15.0f - wcx; };
    const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684);
    g = rect_state(rdram, g);
    for (int i = 0; i < 4; i++) {
        const float x = slot_x(i);
        g = rect(rdram, g, x + slot.x0 - 1.0f, top + slot.y0 - 1.0f, x + slot.x1 + 1.0f, top + slot.y1 + 1.0f,
                 0x14141400u | shade, hs, vs);
    }
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    for (int i = 0; i < 4 && heap_pointer(party); i++) {
        const gpr member = MEM_W(4 * i, party);
        const gpr img = heap_pointer(member) ? MEM_W(0x44, member) : 0;
        if (!heap_pointer(img) || MEM_HU(0x0A, img) == 0 || MEM_HU(0x0C, img) == 0) continue;
        // filling the window and a unit more all round, so its soft edges go under the frame
        const float ps = std::max(slot.x1 - slot.x0, slot.y1 - slot.y0) + 1.0f;
        const float x = slot_x(i), px = x + wcx - ps / 2.0f, py = top + wcy - ps / 2.0f;
        g = (gpr)(int32_t)call_game_stack(rdram, ctx, Borg8_DrawSimple, (uint32_t)g, (uint32_t)img, f32_bits(px), f32_bits(py),
                                          { f32_bits(ps / MEM_HU(0x0A, img)), f32_bits(ps / MEM_HU(0x0C, img)), shade, shade,
                                            shade, 255 });
    }
    copy_branding(rdram);
    auto draw_piece = [&](const Piece& pc, float x, float y) {
        g = prompt_state(rdram, g, 255);
        put(rdram, g, 0xFA000000, shade << 24 | shade << 16 | shade << 8 | 0xFF);
        g = draw_rgba16(rdram, g, pc.image, x * hs, y * vs, hs, vs);
    };
    const float bar_y = top + slot.mid;  // the bar's middle row, for the ends
    draw_piece(end_l, slot_x(0) - end_l.w, bar_y - end_l.mid);
    const Piece& leader = piece("pr_slot_leader");  // the party's leader in gold, if painted (Codex)
    for (int i = 0; i < 4; i++) {
        draw_piece(i == 0 && leader.image >= 0 ? leader : slot, slot_x(i), top);
    }
    draw_piece(end_r, slot_x(3) + slot.w, bar_y - end_r.mid);
    ctx->r2 = g;
    return 1;
}

// ---- ReChronicled page ----
// The pause menu's Menu page (WidgetOptionsMenu::MakeScrollList, func_8003F8C4: Load Game, Save Game, Options, Theater,
// Credits, Help; in a battle Options and Flee) gets one more entry, "ReChronicled", below Theater (in a battle at the
// end): aidyn.us.toml hooks the list's making after Theater is appended (0x8003FB0C) and where every list ends
// (0x8003FBC8, added there unless it already is; the list is made with room for eight instead of six), where it is
// made the game's way (new, WidgetClipText(text, 400), its id at +0x5E, then
// WidgetScrollMenu::Append; the list's colours are set after). A on it reaches PauseMenuSelectCallback
// (func_800403AC), which would stop the game on an id it doesn't know: hooked where it reads the id (0x80040438), ours
// opens the page and returns through the callback's own exit. The page is ours, drawn at the end of the frame on the
// pause menu's parchment: the list stays the menu's page but fades out under it (Utilities::SetAlpha) and back in when
// it closes, and the game gets no input meanwhile (aidyn::input::set_capture), so nothing it does with a button can
// reach a list that isn't there. Up / down choose a row, A or left / right change it, B (or Start) goes back. Each
// change is applied at once and written into settings.json: the sound (audio.enhanced), textures (every pack, HD save pictures, redrawn page
// words, the HD letters' widths; off: as the N64), vsync, the turn order, the controls card, the other button prompts
// (menu hints, LB / RB at the page arrows, the party switch), the HUD at the screen
// edges, the draw distance, smooth walk cycles and the camera's right stick and its directions. The words are
// make_branding.py's rc_* pictures, tinted with the list's own colours (red-brown; the highlight pulsing brown to
// purple); the title is two halves (one picture would be more than a load of TMEM).
extern "C" void new_(uint8_t* rdram, recomp_context* ctx);  // the game's operator new (0x8008EEDC)
extern "C" void WidgetClipText__WidgetClipText(uint8_t* rdram, recomp_context* ctx);
extern "C" void WidgetScrollMenu__Apppend(uint8_t* rdram, recomp_context* ctx);
extern "C" void PlayAudioSound(uint8_t* rdram, recomp_context* ctx);

namespace aidyn {
    void refresh_textures();  // src/main/rt64_context.cpp
    void refresh_vsync();
    void refresh_aspect();
}

void report_problem(uint8_t* rdram, recomp_context* ctx);  // "Report a problem", below
void controls_open(uint8_t* rdram, recomp_context* ctx);     // "Controls page", below
void ach_open(uint8_t* rdram, recomp_context* ctx);          // "Achievements page and toast", below
gpr achievements_page(uint8_t* rdram, recomp_context* ctx, gpr g, uint32_t b, uint32_t pressed, uint32_t act);
bool ach_shown();
gpr controls_page(uint8_t* rdram, recomp_context* ctx, gpr g, uint32_t b, uint32_t pressed, uint32_t act);
void title_controls_open(uint8_t* rdram, recomp_context* ctx);
gpr title_controls_page(uint8_t* rdram, recomp_context* ctx, gpr g);
void title_ach_open(uint8_t* rdram, recomp_context* ctx);      // "Achievements page and toast", below
gpr title_ach_page(uint8_t* rdram, recomp_context* ctx, gpr g);
bool fair_panel_open(uint8_t* rdram, recomp_context* ctx);       // "Fair play", below
gpr fair_panel(uint8_t* rdram, recomp_context* ctx, gpr g);
void fair_show(uint8_t* rdram, recomp_context* ctx);
void fair_fix_stats(uint8_t* rdram, recomp_context* ctx);
bool fair_rule_kept(int i);
void better_shops_tick(uint8_t* rdram);                            // "Better shops"
bool sparkle_on();                                                // "Treasure sparkle": this playthrough has it
void sparkle_turn_on(uint8_t* rdram, recomp_context* ctx);

namespace {
    // The mouse pointer over one of the port's own lists (#44): when it has moved since this list last looked
    // (seen), the row under it (rows `step` apart from `top`, between `left` and `right`), or -1; x is where it is.
    struct PointerPick { bool moved = false; int row = -1; float x = 0.0f; };
    PointerPick pointer_pick(float top, float step, int count, float left, float right, uint32_t& seen) {
        PointerPick p;
        float x = 0.0f, y = 0.0f;
        uint32_t moves = 0;
        if (!aidyn::controls::pointer(&x, &y, &moves) || moves == seen) return p;
        seen = moves;
        p.moved = true;
        p.x = x;
        if (x < left || x > right) return p;
        const int i = (int)std::floor((y - top + 2.0f) / step);
        if (i >= 0 && i < count) p.row = i;
        return p;
    }
}

namespace {
    // Our entries' ids (the game's are 0xA0..0x17A and 9999), their texts and their pages' titles.
    constexpr uint16_t kRcId = 0x7A11, kDisplayId = 0x7A12, kAchMenuId = 0x7A16;
    constexpr gpr kRcText = (gpr)(int32_t)0x80F81100;           // the entries' texts (above the game's 15 MB), 0x20 each
    enum RcKind { kRcGameplay = 0, kRcDisplay = 1, kRcControls = 2, kRcAchievements = 3 };
    constexpr const char* kRcEntryText[] = { "ReChronicled", "Display" };
    constexpr uint16_t kRcEntryId[] = { kRcId, kDisplayId };
    constexpr gpr kBigAssMenu = (gpr)(int32_t)0x800D9558;       // gGlobals.BigAssMenu (the pause menu)
    constexpr gpr kSfx = (gpr)(int32_t)0x800D9380;              // gGlobals.SFXStruct
    // The list's colours (MakeScrollList: SetColor(COLOR_RED1), SetScrollMenuColors(COLOR_BROWN2, COLOR_PURPLE1, 0x14)):
    // red-brown, the highlighted entry pulsing from dark brown to purple and back over 20 frames each way.
    constexpr uint32_t kMenuInk = 0x825050, kMenuHighlight0 = 0x442A22, kMenuHighlight1 = 0x978DBF;
    constexpr float kRowsLeft = 76.0f, kRowsRight = 286.0f, kRowsTop = 54.0f, kRowStep = 16.0f;  // N64 pixels, 4:3

    // A row of steps (the draw distance, the walk speed): the setting, its steps and their words.
    struct RcSteps {
        float* value;
        const float* steps;
        const char* const* words;
        int n;
    };

    struct RcRow {
        const char* label;
        bool* flag;           // a switch; nullptr: steps
        const char* key;      // in settings.json's section (graphics unless the row says)
        void (*applied)(uint8_t* rdram);
        bool (*shown)() = nullptr;  // what the row shows when not just *flag (Widescreen: the picture really wide)
        const RcSteps* steps = nullptr;
        void (*action)(uint8_t* rdram, recomp_context* ctx) = nullptr;  // a row that does something (A), no value
        const char* on_word = "rc_on";    // a switch's values
        const char* off_word = "rc_off";
        const char* section = "graphics";
    };

    constexpr float kDistances[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
    constexpr const char* kDistanceWords[] = { "rc_dd_1", "rc_dd_15", "rc_dd_2", "rc_dd_25", "rc_dd_3" };
    // Anti-aliasing's steps: MSAA and supersampling together (src/main/main.cpp set_antialiasing). No step above 8x
    // and 2x: 3x supersampling (an "Ultra" 8x / 3) took an RTX 3090 at 3440x1440 to 16-20 frames a second in the pause
    // menu (the game's picture behind the scroll is copied at the full supersampled size), High holds 100.
    constexpr float kAaSteps[] = { 0.0f, 1.0f, 2.0f, 3.0f };
    constexpr const char* kAaWords[] = { "rc_off", "rc_aa_low", "rc_aa_medium", "rc_aa_high" };
    constexpr int kAaMsaa[] = { 0, 2, 4, 8 }, kAaSupersampling[] = { 1, 1, 2, 2 };
    float aa_step = 2.0f;  // the row's value, from settings.json's msaa and supersampling when the page lists its rows
    constexpr float kWalkSpeeds[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    constexpr float kPartySpeeds[] = { 1.0f, 1.5f, 2.0f, 3.0f };
    constexpr const char* kPartySpeedWords[] = { "rc_dd_1", "rc_dd_15", "rc_dd_2", "rc_dd_3" };
    constexpr float kEnemySpeeds[] = { 1.0f, 2.0f, 3.0f, 4.0f };
    constexpr const char* kEnemySpeedWords[] = { "rc_dd_1", "rc_dd_2", "rc_dd_3", "rc_x4" };
    constexpr const char* kWalkWords[] = { "rc_dd_1", "rc_ws_125", "rc_dd_15", "rc_ws_175", "rc_dd_2" };

    std::vector<RcRow> rc_rows(int kind) {
        auto& gfx = aidyn::settings::graphics_rw();
        static const RcSteps distance = { &gfx.draw_distance, kDistances, kDistanceWords, (int)std::size(kDistances) };
        static const RcSteps walk = { &gfx.walk_speed, kWalkSpeeds, kWalkWords, (int)std::size(kWalkSpeeds) };
        auto& game = aidyn::settings::game_rw();
        static const RcSteps party = { &game.combat_party_speed, kPartySpeeds, kPartySpeedWords, (int)std::size(kPartySpeeds) };
        static const RcSteps enemy = { &game.combat_enemy_speed, kEnemySpeeds, kEnemySpeedWords, (int)std::size(kEnemySpeeds) };
        if (kind == kRcDisplay) {
            static const RcSteps aa = { &aa_step, kAaSteps, kAaWords, (int)std::size(kAaSteps) };
            int best = 0;  // the step nearest what settings.json has (it may hold any mix)
            for (int i = 0; i < (int)std::size(kAaSteps); i++) {
                const auto off = [&](int k) { return std::abs(kAaMsaa[k] - gfx.msaa) + 4 * std::abs(kAaSupersampling[k] - gfx.supersampling); };
                if (off(i) < off(best)) best = i;
            }
            aa_step = kAaSteps[best];
            return {
                { "rc_widescreen", &gfx.widescreen, "widescreen",
                  [](uint8_t*) { aidyn::settings::apply_widescreen(); aidyn::refresh_aspect(); },
                  [] { return aidyn::settings::graphics().aspect != aidyn::settings::Aspect::Original; } },
                { "rc_textures", &gfx.textures, "textures", [](uint8_t* rdram) { aidyn::refresh_textures(); apply_font_widths(rdram); } },
                { "rc_vsync", &gfx.vsync, "vsync", [](uint8_t*) { aidyn::refresh_vsync(); } },
                { "rc_window", &gfx.fullscreen, "fullscreen", nullptr, nullptr, nullptr, nullptr, "rc_borderless", "rc_windowed" },
                { "rc_fps", &gfx.fps_counter, "fps_counter", nullptr },
                { "rc_hud_edges", &gfx.hud_at_edges, "hud_at_edges", nullptr },
                { "rc_draw_distance", nullptr, "draw_distance", nullptr, nullptr, &distance },
                { "rc_anti_aliasing", nullptr, "", [](uint8_t*) {
                      const int i = (int)aa_step;
                      aidyn::set_antialiasing(kAaMsaa[i], kAaSupersampling[i]);
                  }, nullptr, &aa },
            };
        }
        auto& sound = aidyn::settings::audio_rw();
        std::vector<RcRow> rows = {
            { "rc_sound", &sound.enhanced, "enhanced", nullptr, nullptr, nullptr, nullptr, "rc_enhanced", "rc_dd_1", "audio" },
            { "rc_turn_order", &gfx.turn_order, "turn_order", nullptr },
            { "rc_walk_speed", nullptr, "walk_speed", nullptr, nullptr, &walk },
            { "rc_combat_speed", nullptr, "combat_party_speed", nullptr, nullptr, &party, nullptr, "rc_on", "rc_off", "game" },
            { "rc_enemy_turns", nullptr, "combat_enemy_speed", nullptr, nullptr, &enemy, nullptr, "rc_on", "rc_off", "game" },
            { "rc_controls_card", &gfx.controls_card, "controls_card", nullptr },
            { "rc_button_prompts", &gfx.button_prompts, "button_prompts", nullptr },
            { "rc_combat_log", &gfx.combat_log, "combat_log", nullptr },
            { "rc_combat_info", &gfx.combat_descriptions, "combat_descriptions", nullptr },
            { "rc_smooth_loops", &gfx.smooth_animation_loops, "smooth_animation_loops", nullptr },
            { "rc_stick_camera", &gfx.right_stick_camera, "right_stick_camera", nullptr },
            { "rc_invert_x", &gfx.camera_invert_x, "camera_invert_x", nullptr },
            { "rc_invert_y", &gfx.camera_invert_y, "camera_invert_y", nullptr },
            { "rc_controls", nullptr, "", nullptr, nullptr, nullptr, controls_open },
            { "rc_fair_play", nullptr, "", nullptr, nullptr, nullptr, fair_show },
            { "rc_sparkle", nullptr, "", nullptr, sparkle_on, nullptr, sparkle_turn_on },
            { "rc_report", nullptr, "", nullptr, nullptr, nullptr, report_problem },
        };
        if (aidyn_restored_samples_available()) {  // the rechronicled-samples pack ("Restored instrument samples")
            rows.insert(rows.begin() + 1, { "rc_instruments", &sound.restored_samples, "restored_samples", nullptr,
                                            nullptr, nullptr, nullptr, "rc_restored", "rc_dd_1", "audio" });
        }
        return rows;
    }

    bool rc_on(const RcRow& r) {
        return r.shown != nullptr ? r.shown() : *r.flag;
    }

    int step_index(const RcSteps& s) {
        int best = 0;
        for (int i = 0; i < s.n; i++) {
            if (std::fabs(s.steps[i] - *s.value) < std::fabs(s.steps[best] - *s.value)) best = i;
        }
        return best;
    }

    struct RcPage {
        bool open = false, closing = false;
        gpr menu = 0, sub = 0, list = 0;  // the WidgetOptionsMenu, its substruct and its list
        int fade = 0;                     // 0..255, like the menu's own fades (60 a frame)
        int kind = kRcGameplay;
        uint32_t held = 0;                // last frame's buttons (N64 bits; the stick as D-pad bits)
        int repeat = 0;                   // frames up / down / left / right have been held
        int pulse = 0;                    // frames since opening: the highlight's colour
    } rc;
    int rc_row[3] = { 0, 0, 0 };             // each page's highlighted row, kept between visits
    int rc_top[3] = { 0, 0, 0 };             // and its first row shown
    // A page shows ten rows, 14 apart (spitzdano: the page had filled up, 8 apart); more scroll, with a bar at the right.
    constexpr int kRcVisible = 10;
    constexpr float kRcStep = 14.0f;

    uint32_t rc_highlight() {
        const int k = rc.pulse % 40;
        const float t = (k < 20 ? k : 40 - k) / 20.0f;
        uint32_t out = 0;
        for (int shift = 0; shift < 24; shift += 8) {
            const float a = (float)((kMenuHighlight0 >> shift) & 0xFF), b = (float)((kMenuHighlight1 >> shift) & 0xFF);
            out |= (uint32_t)(a + (b - a) * t + 0.5f) << shift;
        }
        return out;
    }

    void rc_sound(uint8_t* rdram, recomp_context* ctx, uint32_t id, uint32_t volume) {
        call_game_stack(rdram, ctx, PlayAudioSound, (uint32_t)kSfx, id, 0, 0x3F800000u /* 1.0f */, { volume, 0 });
    }

    void rc_change(uint8_t* rdram, const RcRow& r, int step) {
        auto& gfx = aidyn::settings::graphics_rw();
        if (r.flag != nullptr) {
            *r.flag = !rc_on(r);
            if (r.flag == &gfx.controls_card) {
                aidyn::input::set_controls_card(*r.flag);  // its own state in input.cpp, and saved
            } else if (r.flag == &gfx.fullscreen) {
                aidyn::set_borderless(*r.flag);  // through RT64, and saved (src/main/main.cpp)
            } else {
                aidyn::settings::save_bool(r.section, r.key, *r.flag);
            }
        } else {
            const RcSteps& s = *r.steps;
            *s.value = s.steps[(step_index(s) + step + s.n) % s.n];
            if (*r.key != '\0') {  // (Anti-aliasing saves its own two settings)
                aidyn::settings::save_number(r.section, r.key, *s.value);
            }
        }
        if (r.applied != nullptr) {
            r.applied(rdram);
        }
    }

    // Still on the pause menu's Menu page, with the list we hid?
    bool rc_still_there(uint8_t* rdram) {
        const gpr big = MEM_W(0, kBigAssMenu);
        const gpr pause = heap_pointer(big) ? MEM_W(0x40, big) : 0;
        return heap_pointer(pause) && MEM_W(0, pause) == rc.menu && heap_pointer(rc.sub) && MEM_W(0x10, rc.sub) == rc.list;
    }

    void rc_end() {
        rc = RcPage{};
        aidyn::input::set_capture(false);
    }
}

namespace {
    bool rc_entry_added = false;  // this MakeScrollList has added ours (after Theater)
}

// MakeScrollList: after Theater is appended, and where the list ends (s1 the substruct, list at +0): Display, then
// ReChronicled.
extern "C" void aidyn_rc_entry(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = MEM_W(0, ctx->r17);
    if (!heap_pointer(list)) {
        return;
    }
    rc_entry_added = true;
    for (int kind : { kRcDisplay, kRcGameplay }) {
        const gpr text = kRcText + 0x20 * kind;
        for (size_t i = 0; i <= std::strlen(kRcEntryText[kind]); i++) {
            MEM_B(i, text) = (int8_t)kRcEntryText[kind][i];
        }
        const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
        if (!heap_pointer(memory)) {
            return;
        }
        const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetClipText__WidgetClipText, (uint32_t)memory, (uint32_t)text, 400);
        MEM_H(0x5E, entry) = (int16_t)kRcEntryId[kind];
        call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
    }
}

void quit_entry(uint8_t* rdram, recomp_context* ctx, gpr list);      // "Quit to Title", below
void ach_menu_entry(uint8_t* rdram, recomp_context* ctx, gpr list);  // the same place: Achievements, after Help

extern "C" void aidyn_rc_entry_end(uint8_t* rdram, recomp_context* ctx) {
    if (!rc_entry_added) {
        aidyn_rc_entry(rdram, ctx);  // a battle's list: Theater isn't in it
    }
    rc_entry_added = false;
    ach_menu_entry(rdram, ctx, MEM_W(0, ctx->r17));
    quit_entry(rdram, ctx, MEM_W(0, ctx->r17));
}

// PauseMenuSelectCallback, the Menu page's entry chosen: v1 its id, s0 the substruct, s2 the WidgetOptionsMenu.
// 1: ours, the menu stays; 2: ours, the menu closes (the hook returns s2, as Help and Flee do).
bool quick_load_chosen(uint8_t* rdram, recomp_context* ctx, uint32_t id);  // "Quick and auto saves", below
int quit_chosen(uint8_t* rdram, recomp_context* ctx, uint32_t id);         // "Quit to Title", below

extern "C" int aidyn_rc_select(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = ctx->r3 & 0xFFFF;
    if (!rc.open && quick_load_chosen(rdram, ctx, id)) {
        return 1;
    }
    if (!rc.open) {
        if (const int quit = quit_chosen(rdram, ctx, id)) {
            return quit;
        }
    }
    if ((id != kRcId && id != kDisplayId && id != kAchMenuId) || rc.open) {
        return 0;
    }
    rc.open = true;
    rc.kind = id == kDisplayId ? kRcDisplay : kRcGameplay;
    rc.closing = false;
    rc.menu = ctx->r18;
    rc.sub = ctx->r16;
    rc.list = MEM_W(0x10, rc.sub);
    rc.fade = 0;
    rc.held = 0xFFFFFFFF;  // the A that chose it is still down: wait for it to come up
    aidyn::input::set_capture(true);
    if (id == kAchMenuId) ach_open(rdram, ctx);  // the list, straight from the Menu page
    return 1;
}

// Graphics__EndList: the page, over everything.
gpr aidyn_rc_page(uint8_t* rdram, recomp_context* ctx, gpr g) {
    g = title_controls_page(rdram, ctx, g);
    g = title_ach_page(rdram, ctx, g);
    g = fair_panel(rdram, ctx, g);
    if (!rc.open) {
        return g;
    }
    if (!rc_still_there(rdram)) {
        rc_end();
        return g;
    }
    // Input (the game gets none while capturing).
    uint32_t b = 0;
    float sx = 0.0f, sy = 0.0f;
    aidyn::input::captured(&b, &sx, &sy);
    if (sy > 0.5f) b |= 0x0800;
    if (sy < -0.5f) b |= 0x0400;
    if (sx < -0.5f) b |= 0x0200;
    if (sx > 0.5f) b |= 0x0100;
    const uint32_t pressed = rc.held == 0xFFFFFFFF ? 0 : (b & ~rc.held);
    rc.held = (rc.held == 0xFFFFFFFF && (b & 0x8000)) ? 0xFFFFFFFF : b;
    const uint32_t dirs = b & 0x0F00;
    rc.repeat = dirs != 0 ? rc.repeat + 1 : 0;
    const uint32_t repeated = (rc.repeat > 24 && rc.repeat % 6 == 0) ? dirs : 0;
    const uint32_t act = pressed | repeated;
    if (rc.kind == kRcControls) {
        return controls_page(rdram, ctx, g, b, pressed, act);
    }
    if (rc.kind == kRcAchievements) {
        return achievements_page(rdram, ctx, g, b, pressed, act);
    }
    const std::vector<RcRow> rows = rc_rows(rc.kind);
    const int n = (int)rows.size(), shown = std::min(n, kRcVisible);
    int& row = rc_row[rc.kind];
    int& top = rc_top[rc.kind];
    row = std::min(row, n - 1);
    top = std::clamp(top, 0, n - shown);
    if (!rc.closing && rc.fade == 255) {  // the pointer over a row highlights it (A, or a click, then acts on it)
        static uint32_t seen = 0;
        const PointerPick p = pointer_pick(kRowsTop, kRcStep, shown, kRowsLeft - 8.0f, kRowsRight + 4.0f, seen);
        if (p.row >= 0 && top + p.row != row) { row = top + p.row; rc_sound(rdram, ctx, 0x73B, 30); }
    }
    if (!rc.closing && rc.fade == 255) {
        if (act & 0x0800) { row = (row + (int)rows.size() - 1) % (int)rows.size(); rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0400) { row = (row + 1) % (int)rows.size(); rc_sound(rdram, ctx, 0x73B, 30); }
        if (rows[row].action != nullptr) {
            if (pressed & 0x8000) rows[row].action(rdram, ctx);
        }
        else if (act & 0x8000) { rc_change(rdram, rows[row], 1); rc_sound(rdram, ctx, 0x740, 60); }
        else if (act & 0x0300) { rc_change(rdram, rows[row], (act & 0x0200) ? -1 : 1); rc_sound(rdram, ctx, 0x740, 60); }
        if (pressed & (0x4000 | 0x1000)) {  // B, Start: back to the list
            rc.closing = true;
            rc_sound(rdram, ctx, 0x741, 60);
        }
    }
    if (row < top) top = row;
    if (row >= top + shown) top = row - shown + 1;
    // Fades: the page in over the list, or out.
    rc.fade = std::clamp(rc.fade + (rc.closing ? -60 : 60), 0, 255);
    call_game(rdram, ctx, Utilities__SetAlpha, (uint32_t)rc.list, (uint32_t)(255 - rc.fade));
    if (rc.closing && rc.fade == 0) {
        // The game gets input back once every button is up: the B still held would be a new press to it (and leave
        // the pause menu).
        if (b == 0) {
            rc_end();
        }
        return g;
    }
    // Draw: the title, a row each (its name at the left, its value at the right), the hints.
    copy_branding(rdram);
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    auto ink = [&](uint32_t rgb) {
        g = prompt_state(rdram, g, (uint8_t)rc.fade);
        put(rdram, g, 0xFA000000, rgb << 8 | (uint32_t)rc.fade);  // gDPSetPrimColor: the tint
    };
    auto word = [&](const char* name, float x, float y, bool right) {
        const int i = branding_index(name);
        if (i >= 0) {
            g = draw_rgba16(rdram, g, i, (right ? x - ink_width(i) : x) * hs, y * vs, hs, vs);
        }
    };
    // Centred on the parchment right of the scroll's roller (the rows), like the Menu page's title and list.
    const float centre = (kRowsLeft + kRowsRight) / 2.0f;
    rc.pulse++;
    g = prompt_state(rdram, g, (uint8_t)rc.fade);
    if (rc.kind == kRcDisplay) {
        const int title = branding_index("rc_title_display");
        if (title >= 0) {
            word("rc_title_display", centre - aidyn::branding::kImages[title].w / 2.0f, kRowsTop - 31.0f, false);
        }
    } else {
        const int title_l = branding_index("rc_title_l"), title_r = branding_index("rc_title_r");  // halves: 4 KB of TMEM each
        if (title_l >= 0 && title_r >= 0) {
            const float w = aidyn::branding::kImages[title_l].w, x = centre - w;
            word("rc_title_l", x, kRowsTop - 31.0f, false);
            word("rc_title_r", x + w, kRowsTop - 31.0f, false);
        }
    }
    for (int k = top; k < top + shown; k++) {
        const RcRow& r = rows[k];
        const float y = kRowsTop + kRcStep * (k - top);
        ink(k == row ? rc_highlight() : kMenuInk);
        word(r.label, kRowsLeft, y, false);
        if (r.action == nullptr || r.shown != nullptr) {  // (an action row with a value: Treasure sparkle)
            word(r.flag != nullptr || r.steps == nullptr ? (rc_on(r) ? r.on_word : r.off_word) : r.steps->words[step_index(*r.steps)],
                 kRowsRight, y, true);
        }
    }
    if (n > shown) {  // the scroll bar: the track the rows' height, the thumb where the shown rows are
        const int bar = branding_index("ach_bar");
        if (bar >= 0) {
            const auto& img = aidyn::branding::kImages[bar];
            const float x = kRowsRight + 6.0f, y0 = kRowsTop + 1.0f, h = kRcStep * shown - 4.0f, w = 2.0f;
            const float th = std::max(8.0f, h * shown / n), ty = y0 + (h - th) * top / (float)(n - shown);
            g = tinted_image_state(rdram, g, 0xC8B494u << 8 | (uint32_t)(rc.fade * 7 / 8));
            g = draw_rgba16(rdram, g, bar, x * hs, y0 * vs, w / img.w * hs, h / img.h * vs);
            g = tinted_image_state(rdram, g, (uint32_t)kMenuInk << 8 | (uint32_t)rc.fade);
            g = draw_rgba16(rdram, g, bar, x * hs, ty * vs, w / img.w * hs, th / img.h * vs);
        }
    }
    g = prompt_state(rdram, g, (uint8_t)rc.fade);
    const int change = branding_index("rc_hint_change"), back = branding_index("rc_hint_back");
    if (change >= 0 && back >= 0) {
        const float gap = 24.0f, x = centre - (ink_width(change) + ink_width(back)) * kPromptScale / 2.0f - gap / 2.0f;
        const float y = kRowsTop + kRcStep * shown + 6.0f;
        g = draw_prompt(rdram, g, change, x * hs, y * vs, hs, vs);
        g = draw_prompt(rdram, g, back, (x + ink_width(change) * kPromptScale + gap) * hs, y * vs, hs, vs);
    }
    return g;
}

// ---- Graphics option ----
// The game's Options screen (title and pause menu) had a Graphics row: Normal Resolution or, with the Expansion Pak,
// High Resolution (a 512-wide frame; a third mode, 32-bit colour, is never offered). Here the renderer already draws
// at render_height in full colour, and the wide frame only stretched the HUD and menus off the picture, so the row is
// gone and the mode is always normal (aidyn.us.toml: Graphics::SetGfxMode and ResolutionSelect, and up / down skip
// the row as the game does without an Expansion Pak). The constructor is hooked after it links its rows: the Graphics
// title and its value move below the screen, and the Game title and Rename Character (pause menu only) move up into
// their place. OptionsConfigSubstruct: AudioTitle +0, GraphicsTitle +4, GameTitle +8, optionTitles[4] +0xC.
extern "C" void BaseWidget__SetCoords(uint8_t* rdram, recomp_context* ctx);

extern "C" void aidyn_options_no_graphics(uint8_t* rdram, recomp_context* ctx) {
    const gpr widget = ctx->r20;  // s4: the WidgetOptionsConfig
    const gpr sub = heap_pointer(widget) ? MEM_W(0x40, widget) : 0;
    if (!heap_pointer(sub)) {
        return;
    }
    const gpr graphics = MEM_W(0x4, sub), value = MEM_W(0x14, sub), game = MEM_W(0x8, sub), rename = MEM_W(0x18, sub);
    if (!heap_pointer(graphics) || !heap_pointer(value)) {
        return;
    }
    auto move = [&](gpr w, int dy) {
        call_game(rdram, ctx, BaseWidget__SetCoords, (uint32_t)w, (uint32_t)(int32_t)MEM_H(0x60, w),
                  (uint32_t)(int32_t)(MEM_H(0x62, w) + dy));
    };
    const int up = heap_pointer(game) ? MEM_H(0x62, game) - MEM_H(0x62, graphics) : 0;
    move(graphics, 600);
    move(value, 600);
    if (heap_pointer(game)) {
        move(game, -up);
    }
    if (heap_pointer(rename)) {
        move(rename, -up);
    }
}

// ---- Pause scroll ----
// The pause pages are one long parchment (PauseBGOptions, PauseBGStats, PauseBGMap) drawn through that clip, and the
// scroll's wooden rollers are a 3D model (the pause substruct's borg7) moved sideways per page (scrollSpeed, +0x2C:
// 27 on the Menu page, 0 on the equipment page, -26 on the map; scroll_floats). On the Menu page the left roller sits
// at x 22..64 and the parchment still ran on to the screen's edge behind it; on the map page, the same past the right
// roller. PauseWidget::Render is hooked before it draws its children (aidyn.us.toml) to keep the clip between the
// rollers' middles, measured in game: x 42.9 for the left one at 27, 292.3 for the right one at -26, moving 11 px a
// unit (so they follow the page turns; on the equipment page both are off the screen). Only with the pause menu at
// 4:3 (menus_4_3, the default), where the 3D and the pages share a frame. Past the rollers the screen's picture shows:
// draw_screenshot_background drew only its top and bottom 40 rows once the menu was open (black between, where the
// parchment covered it), and now always draws it whole (aidyn.us.toml).
extern "C" void aidyn_pause_clip(uint8_t* rdram, recomp_context* ctx) {
    const auto& gfx = aidyn::settings::graphics();
    if (!gfx.menus_4_3 && gfx.aspect != aidyn::settings::Aspect::Original) {
        return;
    }
    const gpr sub = MEM_W(0x40, ctx->r16);  // s0: the PauseWidget
    if (!heap_pointer(sub)) {
        return;
    }
    const float s = read_f32(rdram, sub + 0x2C);
    const int left = (int)std::lround(42.9f + (s - 27.0f) * 11.0f), right = (int)std::lround(292.3f + (s + 26.0f) * 11.0f);
    const int x0 = (int16_t)ctx->r6, x1 = (int16_t)MEM_W(0x10, ctx->r29);
    ctx->r6 = std::max(x0, left);
    MEM_W(0x10, ctx->r29) = std::min(x1, right);
}

// ---- Help book ----
// The pause menu's Help is a dialogue (BORG13_HelpMenu, asset 0x54B): commands of 0x38 bytes (text at +0x20, an s16
// offset into the text block, its length a u16 at +0x22; next command +0x28; a menu's other choices +0x29..+0x2F
// with its own index at +0x30; +0x31 0x8 for a menu, 0x1 otherwise; ops at +0: {u16 value, u8 command}, 2 = go to
// command `value`, 34 = end). Its words were written for the N64: "the \x7F BUTTON" (the text's escapes become the
// button icons, now the Xbox ones: the BUTTON after them is dropped), "the Control Stick" (the left stick here), and
// C-left to pick a target (View quicksaves here: RT does it). The L and R icons (\x88, \x89)
// don't show in the book's text, so LB and RB are written as words. get_borg13 is hooked as it returns
// the loaded book: its commands and text are copied above the game's 15 MB with those words changed and a
// ReChronicled topic added before Exit Help (this edition's camera, fast-forward, controls card, menus, saving, combat and
// keyboard), and the book points at the copies. Only the book as the 1.0 ROM has it (229 commands) is changed.
namespace {
    constexpr gpr kHelpCmds = (gpr)(int32_t)0x80F90000;  // up to 255 commands
    constexpr gpr kHelpText = (gpr)(int32_t)0x80F94000;  // up to 0xC000 bytes
    constexpr int kCmdSize = 0x38;

    struct HelpNode {
        std::string text;
        int next = 255;           // +0x28
        int choices[7] = { 255, 255, 255, 255, 255, 255, 255 };
        bool menu = false;
        int go_to = -1;           // an op "go to" (Back)
    };

    std::string replace_all(std::string s, const std::string& from, const std::string& to) {
        for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) {
            s.replace(at, from.size(), to);
        }
        return s;
    }

    std::string help_words(std::string s) {
        s = replace_all(s, "\\x82 BUTTON", "\\x81");  // pick a target: RT (C-right), not View
        for (const char* code : { "7F", "80", "81", "83", "84", "85", "86", "87", "88", "89" }) {
            s = replace_all(s, std::string("\\x") + code + " BUTTON", std::string("\\x") + code);
        }
        s = replace_all(s, "the Control Stick", "the left stick");
        return replace_all(s, "Control Stick", "left stick");
    }

    // The ReChronicled topic: a menu of subjects, each one or more pages, then Back to the topics.
    struct HelpSubject {
        const char* name;
        std::vector<const char*> pages;
    };
    const HelpSubject kHelpSubjects[] = {
        { "Camera", {
            "Turn the camera around the party with the right stick, and tilt it up or down. Once turned, the camera "
            "keeps that direction while you walk. Click the right stick to bring it back behind the party.",
            "Holding RB and moving the left stick still works the game's own camera. The right-stick camera can be "
            "turned off, and either of its directions inverted, on the ReChronicled page of the Menu." } },
        { "Fast-Forward", {
            "Click the left stick to make everything run three times faster: long walks, fights and scenes. Click it "
            "again to go back to normal speed. Two chevrons at the top right of the screen show when it is on. "
            "In a conversation, \\x85 skips it until you are asked something.",
            "In combat, the enemies' turns also run twice as fast on their own, so you spend less time waiting for "
            "your next turn. Combat speed and Enemy turns on the ReChronicled page set how fast each side goes." } },
        { "Controls Card", {
            "While you walk and in combat, a card at the left edge of the screen lists what the buttons do there. "
            "Press LB to hide it or show it again; your choice is remembered.",
            "The menus also show button hints, such as \\x7F Equip or Lists. The card and the hints can each be "
            "turned off on the ReChronicled page of the Menu." } },
        { "Menus", {
            "In the pause menu, LB and RB turn the scroll to the previous or next page. On the equipment page, "
            "in shops and with trainers, \\x87 and \\x81 choose the previous or next member of your party.",
            "The Menu page's Display and ReChronicled entries hold this edition's options: widescreen, the new "
            "textures, the draw distance, the turn order and more. Each change takes effect at once." } },
        { "Saving", {
            "Press \\x82 while walking about to quicksave. The game also saves by itself when you arrive in another "
            "area and after every ten minutes of walking about, keeping the last two of these autosaves.",
            "Quick Load on the Menu page loads the newest quicksave or autosave; Continue on the title screen loads "
            "your newest save of any kind. They are kept apart from the Controller Pak, which still holds the saves "
            "made with Save Game.",
            "Load Game and Quick Load work during a fight too. Quit to Title, last on the Menu page, ends the game "
            "without closing it: choose it twice, as anything not saved is lost. Quit Game on the title screen "
            "closes it." } },
        { "Combat", {
            "A strip across the top of the screen shows the order of the round's turns, from whoever acts now, then "
            "the next round, dimmed. Quick fighters act two or three times a round. Whoever you are targeting flashes "
            "on their portrait and in the strip.",
            "Hold \\x84 for spells and skills, or \\x83 for items, choose with the left stick and let go to use it; a "
            "quick tap uses the last one again. \\x87 ends a character's turn.",
            "With a bow or a thrown weapon, \\x86 left and right step through the targets in reach, and the aiming "
            "marker follows how far you push the left stick. A blue zZ on a portrait means that fighter is out of "
            "stamina: they take more damage, hit more weakly and can't cast until they rest." } },
        { "Keyboard and PC", {
            "@KEYS@",
            "F1 opens this edition's manual in your web browser. F11, or Alt and Enter, switches between a window and "
            "the full screen (borderless), as does Window on the Display page; it's remembered." } },
    };
}

extern "C" void aidyn_help_book(uint8_t* rdram, uint32_t index, gpr header) {
    if (index != 0x54B || !heap_pointer(header)) {
        return;
    }
    const gpr book = MEM_W(0x10, header);  // Borg13Data
    if (!heap_pointer(book) || MEM_W(0, book) == kHelpCmds || MEM_BU(0x12, book) != 229) {
        return;
    }
    const gpr cmds = MEM_W(0, book), text = MEM_W(8, book);
    auto text_of = [&](gpr cmd) {
        std::string s;
        const int at = MEM_H(0x20, cmd), len = MEM_HU(0x22, cmd);
        for (int i = 0; at >= 0 && i < len; i++) {
            s += (char)MEM_B(at + i, text);
        }
        return s;
    };
    if (text_of(cmds) != "Select a help topic:") {
        return;
    }
    std::vector<std::string> texts;
    for (int i = 0; i < 229; i++) {
        const gpr from = cmds + kCmdSize * i, to = kHelpCmds + kCmdSize * i;
        for (int k = 0; k < kCmdSize; k += 4) {
            MEM_W(k, to) = MEM_W(k, from);
        }
        texts.push_back(help_words(text_of(from)));
    }
    // The topic: its choice in the topics (before Exit Help), its menu, each subject's choice and pages, Back.
    std::vector<HelpNode> added;
    const int first = 229;
    auto add = [&](HelpNode n) { added.push_back(n); return first + (int)added.size() - 1; };
    const int choice = add({ "ReChronicled" });
    const int menu = add({ "ReChronicled Help:" });
    added[menu - first].menu = true;
    added[choice - first].next = menu;
    std::vector<int> subject_choices;
    for (const HelpSubject& s : kHelpSubjects) {
        const int c = add({ s.name });
        subject_choices.push_back(c);
        int prev = c;
        for (const char* page : s.pages) {
            const int p = add({ std::strcmp(page, "@KEYS@") == 0 ? help_keys_text() : std::string(page) });
            added[prev - first].next = p;
            prev = p;
        }
    }
    const int back = add({ "Back" });
    added[back - first].next = 0;
    added[back - first].go_to = 0;
    added[menu - first].next = subject_choices[0];
    for (size_t k = 1; k < subject_choices.size(); k++) {
        added[menu - first].choices[k - 1] = subject_choices[k];
    }
    added[menu - first].choices[subject_choices.size() - 1] = back;
    const int total = first + (int)added.size();
    if (total > 255) {  // the book's links are bytes: a page too many and the whole topic is left out
        fprintf(stderr, "Help book: %d commands, more than the 255 a link can reach: the ReChronicled topic is left out\n", total);
        return;
    }
    for (int k = 0; k < (int)added.size(); k++) {
        const HelpNode& n = added[k];
        const gpr c = kHelpCmds + kCmdSize * (first + k);
        for (int b = 0; b < kCmdSize; b += 4) {
            MEM_W(b, c) = 0;
        }
        if (n.go_to >= 0) {
            MEM_H(0x0, c) = (int16_t)n.go_to;
            MEM_B(0x2, c) = 2;
        }
        MEM_B(0x28, c) = (int8_t)n.next;
        for (int i = 0; i < 7; i++) {
            MEM_B(0x29 + i, c) = (int8_t)n.choices[i];
        }
        MEM_B(0x30, c) = (int8_t)(first + k);
        MEM_B(0x31, c) = n.menu ? 0x8 : 0x1;
        texts.push_back(n.text);
    }
    // The topics menu (command 0): Standard Controls (next), Trek, Combat, ReChronicled, Exit Help.
    const gpr topics = kHelpCmds;
    MEM_B(0x29 + 2, topics) = (int8_t)choice;
    MEM_B(0x29 + 3, topics) = (int8_t)227;
    // The text block: every command's text again, one after another (NUL after each, as the game's).
    int at = 0;
    for (int i = 0; i < total; i++) {
        const gpr c = kHelpCmds + kCmdSize * i;
        const std::string& s = texts[i];
        if (at + (int)s.size() + 1 > 0xC000) {
            return;  // never: the book points at the game's own still
        }
        if (i == 228) {  // the end marker: no text
            MEM_H(0x20, c) = 0;
            MEM_H(0x22, c) = 0;
            continue;
        }
        MEM_H(0x20, c) = (int16_t)at;
        MEM_H(0x22, c) = (int16_t)s.size();
        for (char ch : s) {
            MEM_B(at++, kHelpText) = (int8_t)ch;
        }
        MEM_B(at++, kHelpText) = 0;
    }
    MEM_W(0, book) = (int32_t)kHelpCmds;
    MEM_W(8, book) = (int32_t)kHelpText;
    MEM_H(0x10, book) = (int16_t)at;
    MEM_B(0x12, book) = (int8_t)total;
}

// ---- Combat log ----
// A log of the fight at the bottom left, in the game's own letters: every message the game writes in combat's
// message box (turns, attacks, hits, misses, damage, kills, spells, healing, potions, fleeing), many of which it shows
// only for a moment or replaces at once, and "Round N" when a round begins. They all go through
// copy_string_to_textbox (0x8005E604: a0 gCombatP, a1 the text; aidyn.us.toml hooks its entry). "X begins action" is
// written more than once a turn: a line the same as the last one, in the same turn of the same round, is left out.
// Each message is split into coloured parts (the party's names blue, enemies' red, hit points lost orange and gained
// green, the rest parchment) and wrapped to the log's width, measured with the game's font (Font::GetWidthScaled);
// the last kLogShown lines are drawn at the end of the frame with Font::printSimple (func_800AA6E8), with a soft
// shadow and no backing, older lines dimmer: in the corner when the picture is wide enough to clear the
// message box, else above it right of the enemies' column (hidden there while a held X / Y menu covers it), and above
// the controls card when that is shown. The latest fight is also written to
// combat-log.txt in the data folder. settings.json graphics.combat_log (the ReChronicled page). AIDYN_COMBAT_LOG_DEBUG=1
// prints each message.
extern "C" void func_800AA6E8(uint8_t* rdram, recomp_context* ctx);  // Font::printSimple(font, Gfx**, txt, x, y, sx, sy)
extern "C" void Font__GetWidthScaled(uint8_t* rdram, recomp_context* ctx);
extern "C" void N64BorgImageDraw(uint8_t* rdram, recomp_context* ctx);  // (g, img, x, y, xOff, yOff, h, v, sx, sy, r, g, b, a)
extern "C" void Font__SetFace(uint8_t* rdram, recomp_context* ctx);

#include <deque>

namespace {
    constexpr gpr kFontPtr = (gpr)(int32_t)0x800D968C;  // gGlobals.font
    constexpr gpr kLogText = (gpr)(int32_t)0x80F81200;  // a part's text for the font (256 bytes)
    constexpr gpr kLogGfx = (gpr)(int32_t)0x80F81300;   // printSimple's Gfx**
    constexpr uint32_t kMainFont = 28;
    constexpr float kLogScale = 0.62f, kLogLine = 7.0f, kLogPad = 2.5f;
    constexpr int kLogShown = 7;
    constexpr uint32_t kLogInk = 0xEADFC8, kLogParty = 0x8CBCFF, kLogEnemy = 0xFF8A78, kLogHurt = 0xFFA040,
                       kLogHeal = 0x86DC86, kLogRound = 0xD8B070;

    struct LogPart {
        std::string text;
        uint32_t rgb;
    };
    struct LogLine {
        std::vector<LogPart> parts;
    };
    std::deque<LogLine> combat_log;
    std::string log_last_text;
    int log_last_turn = -1, log_last_round = -1, log_round_shown = 0;
    float log_width = 140.0f;  // units: the log's text width (set by the draw from the room there is)
    FILE* log_file = nullptr;

    // The font set to the main face and the log's scale meanwhile; put back after.
    struct FontState {
        uint32_t w[8];
    };
    FontState font_save(uint8_t* rdram, gpr font) {
        FontState st;
        for (int k = 0; k < 8; k++) st.w[k] = (uint32_t)MEM_W(4 + 4 * k, font);
        return st;
    }
    void font_restore(uint8_t* rdram, gpr font, const FontState& st) {
        for (int k = 0; k < 8; k++) MEM_W(4 + 4 * k, font) = (int32_t)st.w[k];
    }

    void put_text(uint8_t* rdram, const std::string& text) {
        const size_t n = std::min<size_t>(text.size(), 250);
        for (size_t i = 0; i < n; i++) MEM_B(i, kLogText) = (int8_t)text[i];
        MEM_B(n, kLogText) = 0;
    }

    // Font::printSimple advances by each letter's width scaled and cut to whole units: at these small scales every
    // letter loses up to a unit and the words come out crammed. print_text places the letters one by one, each moving
    // on by its own width at this scale and kTracking, rounded to a whole unit (a letter's own: the same letter always
    // leaves the same gap). Whole units: the game's text routine draws at whole units, and an HD letter drawn between
    // them comes out cut (RT64 shifts its picture by the fraction). Letter widths: Font::GetWidthScaled of each alone.
    constexpr float kTracking = 0.3f;  // units of the 320x240 screen
    float letter_width(uint8_t* rdram, recomp_context* ctx, gpr font, char c) {
        put_text(rdram, std::string(1, c));
        return (float)(int32_t)call_game(rdram, ctx, Font__GetWidthScaled, (uint32_t)font, (uint32_t)kLogText,
                                         f32_bits(1.0f));
    }

    // A letter's advance at this scale: whole units.
    float letter_advance(uint8_t* rdram, recomp_context* ctx, gpr font, char c, float scale) {
        return std::max(1.0f, std::round(letter_width(rdram, ctx, font, c) * scale + kTracking));
    }

    float text_width(uint8_t* rdram, recomp_context* ctx, gpr font, const std::string& text) {
        float w = 0.0f;
        for (char c : text) w += letter_advance(rdram, ctx, font, c, kLogScale);
        return w;
    }

    // Draws text at (x, y) at this scale in the font's colour; returns where the next text goes.
    float print_text(uint8_t* rdram, recomp_context* ctx, gpr font, gpr& g, const std::string& text, float x, float y,
                     float scale) {
        x = std::round(x);
        for (char c : text) {
            const float advance = letter_advance(rdram, ctx, font, c, scale);
            put_text(rdram, std::string(1, c));
            MEM_W(0, kLogGfx) = (int32_t)g;
            call_game_stack(rdram, ctx, func_800AA6E8, (uint32_t)font, (uint32_t)kLogGfx, (uint32_t)kLogText,
                            (uint32_t)(int)x, { (uint32_t)(int)std::lround(y), f32_bits(scale), f32_bits(scale) });
            g = (gpr)(int32_t)MEM_W(0, kLogGfx);
            x += advance;
        }
        return x;
    }

    // The fighters' names, longest first, with their side.
    std::vector<std::pair<std::string, bool>> fighter_names(uint8_t* rdram, gpr combat) {
        std::vector<std::pair<std::string, bool>> names;
        const int ents = std::min<int>(MEM_BU(kEntCount, combat), kMaxEnts), party = MEM_BU(kPartyCount, combat);
        for (int i = 0; i < ents; i++) {
            const gpr ent = MEM_W(kCombatEnts + 4 * i, combat);
            const gpr sheet = heap_pointer(ent) ? MEM_W(kCharSheet, ent) : 0;
            const gpr name = heap_pointer(sheet) ? MEM_W(4, sheet) : 0;
            if (!heap_pointer(name & ~(gpr)3)) continue;  // a string: any byte
            std::string s;
            for (int k = 0; k < 40; k++) {
                const char c = (char)MEM_B(k, name);
                if (c == 0) break;
                s += c;
            }
            if (!s.empty()) names.push_back({ s, i < party });
        }
        std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
        return names;
    }

    // A message in coloured parts.
    std::vector<LogPart> colour_parts(const std::string& text, const std::vector<std::pair<std::string, bool>>& names) {
        const bool hurt = text.find(" loses ") != std::string::npos, heal = text.find(" gains ") != std::string::npos;
        std::vector<LogPart> parts;
        auto add = [&](const std::string& t, uint32_t rgb) {
            if (t.empty()) return;
            if (!parts.empty() && parts.back().rgb == rgb) parts.back().text += t;
            else parts.push_back({ t, rgb });
        };
        size_t i = 0;
        while (i < text.size()) {
            bool matched = false;
            for (const auto& [name, party] : names) {
                if (text.compare(i, name.size(), name) == 0) {
                    add(name, party ? kLogParty : kLogEnemy);
                    i += name.size();
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            if (std::isdigit((unsigned char)text[i])) {
                size_t j = i;
                while (j < text.size() && std::isdigit((unsigned char)text[j])) j++;
                add(text.substr(i, j - i), hurt ? kLogHurt : heal ? kLogHeal : kLogRound);
                i = j;
                continue;
            }
            add(std::string(1, text[i]), kLogInk);
            i++;
        }
        return parts;
    }

    // Wrapped to log_width: words kept whole (a part may end mid-line and the next start there).
    void push_wrapped(uint8_t* rdram, recomp_context* ctx, gpr font, const std::vector<LogPart>& parts) {
        struct Word { std::string text; uint32_t rgb; bool space_before; };
        std::vector<Word> words;
        bool space = false;
        for (const LogPart& p : parts) {
            std::string cur;
            for (char c : p.text) {
                if (c == ' ') {
                    if (!cur.empty()) { words.push_back({ cur, p.rgb, space }); cur.clear(); }
                    space = true;
                }
                else {
                    if (cur.empty() && !words.empty() && !space && words.back().rgb != p.rgb) {
                        // a new colour right after the last word (no space): the same word, another part
                    }
                    cur += c;
                }
            }
            if (!cur.empty()) { words.push_back({ cur, p.rgb, space }); space = false; }
        }
        const float space_w = std::max(1.0f, text_width(rdram, ctx, font, "a a") - text_width(rdram, ctx, font, "aa"));
        LogLine line;
        float x = 0.0f;
        for (const Word& w : words) {
            const float ww = text_width(rdram, ctx, font, w.text);
            const float gap = (w.space_before && x > 0.0f) ? space_w : 0.0f;
            if (x > 0.0f && x + gap + ww > log_width) {
                combat_log.push_back(line);
                line = LogLine{};
                x = 0.0f;
            }
            const std::string t = (w.space_before && x > 0.0f) ? " " + w.text : w.text;
            if (!line.parts.empty() && line.parts.back().rgb == w.rgb) line.parts.back().text += t;
            else line.parts.push_back({ t, w.rgb });
            x += (x > 0.0f ? gap : 0.0f) + ww;
        }
        if (!line.parts.empty()) combat_log.push_back(line);
        while (combat_log.size() > 64) combat_log.pop_front();
    }

    void log_to_file(const std::string& text) {
        if (log_file != nullptr) {
            fprintf(log_file, "%s\n", text.c_str());
            fflush(log_file);
        }
    }
}

// A new battle (aidyn_turn_round with next 0): the log starts empty, and so does combat-log.txt.
void combat_log_new_battle() {
    combat_log.clear();
    log_last_text.clear();
    log_last_turn = log_last_round = -1;
    log_round_shown = 0;
    if (log_file != nullptr) fclose(log_file);
    std::error_code ec;
    log_file = std::fopen((aidyn::settings::folder() / "combat-log.txt").string().c_str(), "w");
}

extern "C" void aidyn_combat_message(uint8_t* rdram, recomp_context* ctx) {
    std::string text;
    for (int i = 0; i < 200; i++) {
        const char c = (char)MEM_B(i, ctx->r5);
        if (c == 0) break;
        text += c;
    }
    static const bool debug = std::getenv("AIDYN_COMBAT_LOG_DEBUG") != nullptr;
    if (debug) {
        static int n = 0;
        fprintf(stderr, "combat message %d: \"%s\"\n", n++, text.c_str());
    }
    const gpr combat = ctx->r4, font = MEM_W(0, kFontPtr);
    if (text.find_first_not_of(' ') == std::string::npos || !heap_pointer(combat) || !heap_pointer(font)) {
        return;
    }
    const int turn = MEM_BU(kTurn + 1, combat);
    if (text == log_last_text && turn == log_last_turn && turn_round == log_last_round) {
        return;  // the same message again in the same turn
    }
    log_last_text = text;
    log_last_turn = turn;
    log_last_round = turn_round;
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    if (turn_round > 0 && turn_round != log_round_shown) {
        log_round_shown = turn_round;
        const std::string r = "Round " + std::to_string(turn_round);
        push_wrapped(rdram, ctx, font, { { r, kLogRound } });
        log_to_file("-- " + r + " --");
    }
    const auto names = fighter_names(rdram, combat);
    const auto parts = colour_parts(text, names);
    if (debug) {
        std::string d = "  names:";
        for (const auto& [n, party] : names) d += " [" + n + (party ? " party]" : " enemy]");
        d += " | parts:";
        for (const auto& pt : parts) { char c[16]; snprintf(c, sizeof(c), " %06X:", pt.rgb); d += c + ("\"" + pt.text + "\""); }
        fprintf(stderr, "%s\n", d.c_str());
    }
    push_wrapped(rdram, ctx, font, parts);
    font_restore(rdram, font, st);
    log_to_file(text);
}

// Drawn after the controls card (aidyn_prompts), in combat; card_top: the card's top (240 if not shown).
gpr draw_combat_log(uint8_t* rdram, recomp_context* ctx, gpr g, float card_top, bool menu_open) {
    const gpr font = MEM_W(0, kFontPtr);
    if (!aidyn::settings::graphics().combat_log || combat_log.empty() || !heap_pointer(font)) {
        return g;
    }
    constexpr float kMargin = 6.0f, kMessagePortraitX = 25.0f, kMessageTop = 178.0f;
    // The room left of the message box's portrait (its x 25 of the middle 4:3, plus the picture's extra width).
    const float room = kMessagePortraitX + (hud_wide() ? (240.0f * view_aspect() - 320.0f) / 2.0f : 0.0f);
    const bool corner = room - kMargin - 3.0f >= 120.0f;
    if (!corner && menu_open) {
        return g;  // above the message box is where the held X / Y menu opens: out of its way while it's held
    }
    log_width = corner ? std::min(150.0f, room - kMargin - 3.0f - 2 * kLogPad) : 140.0f;
    const int shown = std::min<int>(kLogShown, (int)combat_log.size());
    const float height = shown * kLogLine + 2 * kLogPad;
    float bottom = corner ? 240.0f - kMargin : kMessageTop - 3.0f;
    bottom = std::min(bottom, card_top - 3.0f);
    // Above the message box it starts right of the enemies' column (portraits and bars, x 21 to 50), clear of it.
    const float x0 = corner ? kMargin : 53.0f, y0 = bottom - height;
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    g = hud_begin(rdram, g, false);
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    const int first = (int)combat_log.size() - shown;
    // No backing: each line has a soft dark shadow, the same text drawn first a little down and right (RT64's rectangle
    // alignment offsets, in quarter units).
    for (int pass = 0; pass < 2; pass++) {
        GfxCommand a[2];
        const int d = pass == 0 ? 2 : 0;
        gEXSetRectAlign(&a[0], G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, d, d, d, d);
        g = put_cmds(rdram, g, a, 2);
        for (int k = 0; k < shown; k++) {
            const LogLine& line = combat_log[first + k];
            const int age = shown - 1 - k;  // 0 the newest
            const uint32_t alpha = age < 4 ? 255 : (uint32_t)std::max(90, 255 - (age - 3) * 50);
            float x = x0 + kLogPad;
            const float y = y0 + kLogPad + k * kLogLine;
            for (const LogPart& part : line.parts) {
                MEM_W(0x1C, font) = (int32_t)(pass == 0 ? (alpha * 7 / 8) : (part.rgb << 8 | alpha));
                x = print_text(rdram, ctx, font, g, part.text, x, y, kLogScale);
            }
        }
    }
    font_restore(rdram, font, st);
    g = hud_end(rdram, g);
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}
// ---- HP parts on the character page ----
// A character's HP is three parts (Entity::getHPCurrent / getHPMax): Endurance, Stamina and a pool that grows with
// their level, which the game shows nowhere. Damage takes the level pool first, then Stamina, then Endurance; healing
// refills them the other way round. The character page (the pause menu's doll page) shows the total under HitPoints
// (WidgetHealthGold); these three lines go beside its Level and Gold, in the game's red and lettering a little smaller:
// END, STA and VIG (Vigour: the level pool; spitzdano's short names, the words crowded the page), each its current of its most (CharStats::getModded / getBase
// of STAT_END 3, STAT_STAM 5, STAT_LV 6: the three add up to the total). The character is the one the page shows
// (gPartyPicker, 0x800DE9F0, of gGlobals.party's Members); the lines follow the Gold text's place and fade (+0x80).
// They are clipped like the page's own text (Font::PrintWrapperA with the bounds WidgetHealthGold::Render was given),
// so they close with the scroll instead of staying on the screen as it rolls up.
extern "C" void CharStats__getBase(uint8_t* rdram, recomp_context* ctx);
extern "C" void Font__PrintWrapperA(uint8_t* rdram, recomp_context* ctx);
namespace {
    gpr hp_parts_widget = 0;
    uint32_t hp_parts_clip[4] = { 0, 0, 320, 240 };  // x0, y0, x1, y1
}

// The rope (the HP bar, HPBar +0x8C) 3 units higher than the game puts it (y 27 to 37): the redrawn page keeps a faint
// trace of its painted rope at y 25 to 32 (art/pages/PauseBGStats.png), which showed above the game's.
extern "C" void aidyn_hp_parts_begin(uint8_t* rdram, recomp_context* ctx) {
    const gpr widget = ctx->r4;
    hp_parts_widget = widget;
    hp_parts_clip[0] = (uint32_t)ctx->r6 & 0xFFFF;
    hp_parts_clip[1] = (uint32_t)ctx->r7 & 0xFFFF;
    hp_parts_clip[2] = (uint32_t)MEM_W(0x10, ctx->r29) & 0xFFFF;
    hp_parts_clip[3] = (uint32_t)MEM_W(0x14, ctx->r29) & 0xFFFF;
    const gpr bar = heap_pointer(widget) ? MEM_W(0x8C, widget) : 0, gold = heap_pointer(widget) ? MEM_W(0x80, widget) : 0;
    if (heap_pointer(bar) && heap_pointer(gold)) {
        const int16_t y = (int16_t)(MEM_H(0x62, gold) - 69 + 24);  // the Gold text's y (69) tells the page's own place
        MEM_H(0x62, bar) = y;          // posY
        MEM_H(0x68, bar) = y;          // boundY0
        MEM_H(0x6A, bar) = y + 10;     // boundY1
    }
}

extern "C" gpr aidyn_hp_parts_end(uint8_t* rdram, recomp_context* ctx, gpr g) {
    const gpr widget = hp_parts_widget;
    hp_parts_widget = 0;
    const gpr font = MEM_W(0, kFontPtr);
    if (!heap_pointer(widget) || !heap_pointer(font)) return g;
    const gpr gold = MEM_W(0x80, widget);
    const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684);
    const uint32_t picker = MEM_BU(0, (gpr)(int32_t)0x800DE9F0);
    if (!heap_pointer(gold) || !heap_pointer(party) || picker >= 4) return g;
    const gpr sheet = MEM_W(4 * picker, party);
    const gpr stats = heap_pointer(sheet) ? MEM_W(0x18, sheet) : 0;
    const uint32_t alpha = MEM_BU(0x5B, gold);
    if (!heap_pointer(stats) || alpha == 0) return g;
    constexpr float kScale = 0.75f, kRow = 7.5f;
    const float x = (int16_t)MEM_HU(kWidgetPosX, gold), y = (int16_t)MEM_HU(kWidgetPosX + 2, gold);
    const float label_x = x + 32.0f, right = x + 93.0f, top = y - 15.0f;  // beside Level (gold y - 14) and Gold
    static constexpr struct { const char* name; uint32_t stat; } kParts[] = { { "END", 3 }, { "STA", 5 }, { "VIG", 6 } };
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    MEM_W(0x1C, font) = (int32_t)(0x82505000u | alpha);  // COLOR_RED1, as the page's values
    for (int k = 0; k < 3; k++) {
        const int now = (int)(int32_t)call_game(rdram, ctx, CharStats__getModded, (uint32_t)stats, kParts[k].stat);
        const int most = (int)(int32_t)call_game(rdram, ctx, CharStats__getBase, (uint32_t)stats, kParts[k].stat);
        const float row = top + k * kRow;
        const auto print = [&](const std::string& text, float tx) {
            tx = std::round(tx);
            for (char c : text) {
                put_text(rdram, std::string(1, c));
                MEM_W(0, kLogGfx) = (int32_t)g;
                call_game_stack(rdram, ctx, Font__PrintWrapperA, (uint32_t)font, (uint32_t)kLogGfx, (uint32_t)kLogText,
                                (uint32_t)(int)tx,
                                { (uint32_t)(int)std::lround(row), hp_parts_clip[0], hp_parts_clip[1], hp_parts_clip[2],
                                  hp_parts_clip[3], f32_bits(kScale), f32_bits(kScale) });
                g = (gpr)(int32_t)MEM_W(0, kLogGfx);
                tx += letter_advance(rdram, ctx, font, c, kScale);
            }
        };
        print(kParts[k].name, label_x);
        const std::string value = std::to_string(std::max(0, now)) + " / " + std::to_string(std::max(0, most));
        float w = 0.0f;
        for (char c : value) w += letter_advance(rdram, ctx, font, c, kScale);
        print(value, right - w);
    }
    font_restore(rdram, font, st);
    return g;
}

// The tired mark (#35; spitzdano picked a zZ over a TIRED label and a blue tint): a fighter with no stamina left takes
// 15% more damage, hits 10 to 20% weaker (CombatEntity's damage formulas) and can't cast, so its portrait gets a small
// light-blue "zZ" at its top right (the game's letters, small capitals, with the combat log's soft shadow) while its
// stamina is 0 and it is still standing. Drawn with the portrait's own HUD alignment, after its bars.
gpr tired_mark(uint8_t* rdram, recomp_context* ctx, gpr g, float x, float y, float w, float fx, float fy) {
    const gpr font = MEM_W(0, kFontPtr);
    if (!heap_pointer(font)) return g;
    const float scale = 0.5f * fy;
    const std::string mark = "zZ";
    // The bars leave the flat-colour state on: back to the textured one the letters need.
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    float tw = 0.0f;
    for (char c : mark) tw += letter_advance(rdram, ctx, font, c, scale);
    const float tx = x + w - tw - 2.0f * fx, ty = y + 1.2f * fy;
    MEM_W(0x1C, font) = (int32_t)0x101830C8u;  // the shadow, a little down and right
    print_text(rdram, ctx, font, g, mark, tx + 0.7f, ty + 0.7f, scale);
    MEM_W(0x1C, font) = (int32_t)0x9CCBFFFFu;
    print_text(rdram, ctx, font, g, mark, tx, ty, scale);
    font_restore(rdram, font, st);
    return g;
}

// ---- Spell and item descriptions in combat ----
// While an entry is highlighted in combat's held menus, what it does shows beside the menu (#23): a spell (X), and in
// the item menu (Y) a potion or the spell an item holds (a wand, a ring...). The text is the pause menu's Details
// (WidgetItemDetail): the spells' romstrings at ROM 0xB1FCFE30 (0x13A0 bytes, indexed through the spell ID table), the
// potions' at 0xB1FD1960 (0xA80 bytes, without their crafting recipe), wrapped to 140 units in the game's letters,
// with the combat log's soft shadow. The menu (gCombatCVertMenu, its box from aidyn_combat_menu_drawn): its scroll
// list +0x7C, that list's substruct +0x40 (rows +0x18, highlight +0x1E, count +0x20); each row's kind and what it
// holds: combat_entry_key. Aidyn Patch and Aidyn Plus keep the 1.0's texts (the same bytes, even for the spells
// they change). settings.json graphics.combat_descriptions (the ReChronicled page's Combat descriptions) off hides it;
// it used to follow the combat log, which hid the descriptions for anyone who turned the log off.
extern "C" void func_romstrings(uint8_t* rdram, recomp_context* ctx);  // RomString::Load(rom, size)
extern "C" void Entity__CheckSpellWizard(uint8_t* rdram, recomp_context* ctx);
extern "C" void CharSkills__getModdedSkill(uint8_t* rdram, recomp_context* ctx);
extern "C" void RomString__Free(uint8_t* rdram, recomp_context* ctx);

namespace {
    constexpr uint32_t kSpellStrings = 0xB1FCFE30, kSpellStringsSize = 0x13A0, kSpellAFunc = 0x80088958;
    constexpr uint16_t kSpellText[][2] = {
        {0x0300, 0x00},{0x0301, 0x01},{0x0302, 0x02},{0x0303, 0x03},{0x0304, 0x04},{0x0305, 0x05},{0x0306, 0x06},
        {0x0308, 0x07},{0x0309, 0x08},{0x030a, 0x09},{0x030c, 0x0a},{0x030d, 0x0b},{0x0310, 0x0c},{0x0312, 0x0d},
        {0x0313, 0x0e},{0x0314, 0x0f},{0x0315, 0x10},{0x0316, 0x11},{0x0317, 0x12},{0x0318, 0x13},{0x0319, 0x14},
        {0x031a, 0x15},{0x031b, 0x16},{0x031c, 0x17},{0x031d, 0x18},{0x031e, 0x19},{0x031f, 0x1a},{0x0320, 0x1b},
        {0x0321, 0x1c},{0x0322, 0x1d},{0x0323, 0x1e},{0x0324, 0x1f},{0x0326, 0x20},{0x0329, 0x21},{0x032a, 0x22},
        {0x032b, 0x23},{0x032d, 0x24},{0x032e, 0x25},{0x032f, 0x26},{0x0330, 0x27},{0x0331, 0x28},{0x0332, 0x29},
        {0x0333, 0x2a},{0x0334, 0x2b},{0x0335, 0x2c},{0x0336, 0x2d},{0x0337, 0x2e},{0x0338, 0x2f},{0x0339, 0x30},
        {0x033a, 0x31},{0x033b, 0x32},{0x033c, 0x33},{0x033d, 0x34},{0x033e, 0x35},{0x033f, 0x36} };
    // The potions' Details texts (String/PotionDetails), in PotionEnum order without Aspect and Jumping (never in the
    // game): Fire, Inferno, Sleep, Acid, Healing, Stamina, Curing, Antidote, Restore, Strength, Dexterity, Clarity,
    // Charisma, Defence, Stealth (as the game's own table at 0x800CF760 maps them). Each ends with the Alchemist's
    // recipe ("Creating a Fire Flask requires..."), left out in combat.
    constexpr uint32_t kPotionStrings = 0xB1FD1960, kPotionStringsSize = 0xA80;
    constexpr int kPotionText[17] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1, -1, 11, 12, 13, 14 };
    constexpr uint32_t kPotionAFunc = 0x80089114, kItemSpellAFunc = 0x80089178;  // the item menu's two row kinds
    // The X menu's skill rows (WidgetCombatSkills): Lay On Hands, Heal With Herbs, Perform. Their texts are the Details
    // of their skills (String/SkillDetails, 0x790 bytes, between the spells' and the potions': indexed by SkillEnum,
    // Healer 2, Troubador 9).
    constexpr uint32_t kLayOnHandsAFunc = 0x800889E8, kHerbsAFunc = 0x80088A58, kPerformAFunc = 0x800889B0;
    constexpr uint32_t kSkillStrings = 0xB1FD11D0, kSkillStringsSize = 0x790;
    std::vector<std::string> skill_texts;   // by SkillEnum, read once
    std::vector<std::string> spell_texts;   // by romstring index, read once
    std::vector<std::string> potion_texts;  // likewise
    uint32_t text_shown = 0;                // the entry whose lines are below (combat_entry_key)
    float spell_width = 0.0f;               // and the width they're wrapped to
    std::vector<std::string> spell_lines;

    // A romstring table's first count strings: each is its length (the byte before it), then its decoded letters
    // (func_romstrings): not aligned, so read byte by byte.
    std::vector<std::string> load_romstrings(uint8_t* rdram, recomp_context* ctx, uint32_t rom, uint32_t size,
                                             int count) {
        std::vector<std::string> out;
        const gpr strings = (gpr)(int32_t)call_game(rdram, ctx, func_romstrings, rom, size);
        if (!heap_pointer(strings)) {
            return out;
        }
        for (int i = 0; i < count; i++) {
            const gpr str = MEM_W(4 * i, strings);
            const uint32_t u = (uint32_t)str;
            std::string text;
            if (u >= 0x80000400u && u < 0x80E6A000u) {
                const int len = MEM_BU(-1, str);
                for (int k = 0; k < len; k++) {
                    const char c = (char)MEM_BU(k, str);
                    if (c == 0) break;
                    text += c;
                }
            }
            out.push_back(text);
        }
        call_game(rdram, ctx, RomString__Free, (uint32_t)strings);
        return out;
    }

    // The highlighted entry of the held X or Y menu: 0x10000 | a spell's ID, 0x30000 | the spell an item holds (a wand,
    // a ring...: cast from its charges), 0x20000 | a potion (PotionEnum), 0x40000 | a skill row (0 Lay On Hands,
    // 1 Heal With Herbs, 2 Perform), else 0. Those are all the rows the two menus make. The menu's rows: A function +0x18, varU8 +0x5C,
    // varU16 +0x5E. A spell row (func_80088958): its spellbook index in varU8. A potion row (WidgetCombatItems,
    // func_80089114): its item ID in varU16. An item's spell (func_80089178): varU16 where (2 armour, 3 shield,
    // 4 weapon, 5 gear) and varU8 the gear slot; the item (CharSheet +0x20 armour[2], +0x24 weapon, +0x28 gear list
    // -> pItem[]) -> spellCharge (+0x10) -> Spell (+0) -> its ID (+0).
    uint32_t combat_entry_key(uint8_t* rdram, gpr* spell_out = nullptr) {
        const gpr menu = combat_menu;
        if (!heap_pointer(menu) || MEM_W(0, (gpr)(int32_t)0x800E3170) != menu) return 0;
        const gpr list = MEM_W(0x7C, menu);
        const gpr sub = heap_pointer(list) ? MEM_W(0x40, list) : 0;
        if (!heap_pointer(sub)) return 0;
        const gpr rows = MEM_W(0x18, sub);
        const uint32_t hl = MEM_HU(0x1E, sub), count = MEM_HU(0x20, sub);
        if (!heap_pointer(rows) || hl >= count) return 0;
        const gpr row = MEM_W(4 * hl, rows);
        if (!heap_pointer(row)) return 0;
        const uint32_t afunc = (uint32_t)MEM_W(0x18, row), var8 = MEM_BU(0x5C, row), var16 = MEM_HU(0x5E, row);
        const gpr combat = MEM_W(0, kCombatP);
        const gpr ent = heap_pointer(combat) ? MEM_W(kCurrentEnt, combat) : 0;
        const gpr sheet = heap_pointer(ent) ? MEM_W(0x40, ent) : 0;
        if (!heap_pointer(sheet)) return 0;
        auto spell_key = [&](gpr spell) -> uint32_t { return heap_pointer(spell) ? 0x10000u | MEM_HU(0, spell) : 0; };
        if (afunc == kSpellAFunc) {
            const gpr book = MEM_W(0x2C, sheet);
            if (!heap_pointer(book) || var8 >= MEM_BU(4, book)) return 0;
            const gpr spells = MEM_W(0, book);
            if (spell_out != nullptr && heap_pointer(spells)) *spell_out = MEM_W(4 * var8, spells);
            return heap_pointer(spells) ? spell_key(MEM_W(4 * var8, spells)) : 0;
        }
        if (afunc == kLayOnHandsAFunc) return 0x40000u;
        if (afunc == kHerbsAFunc) return 0x40001u;
        if (afunc == kPerformAFunc) return 0x40002u;
        if (afunc == kPotionAFunc) {
            return (var16 >> 8) == 16 && (var16 & 0xFF) < 17 ? 0x20000u | (var16 & 0xFF) : 0;
        }
        if (afunc == kItemSpellAFunc) {
            gpr item = 0;
            if (var16 == 2 || var16 == 3) {
                const gpr armour = MEM_W(0x20, sheet);
                item = heap_pointer(armour) ? MEM_W(4 * (var16 - 2), armour) : 0;
            } else if (var16 == 4) {
                item = MEM_W(0x24, sheet);
            } else if (var16 == 5) {
                const gpr gear = MEM_W(0x28, sheet);
                const gpr items = heap_pointer(gear) ? MEM_W(0, gear) : 0;
                item = heap_pointer(items) && var8 < 12 ? MEM_W(4 * var8, items) : 0;
            }
            const gpr charge = heap_pointer(item) ? MEM_W(0x10, item) : 0;
            const uint32_t k = heap_pointer(charge) ? spell_key(MEM_W(0, charge)) : 0;
            return k != 0 ? (k | 0x20000u) : 0;  // 0x30000 | the spell: cast from the item's charges
        }
        return 0;
    }

    // That entry's description, or "".
    std::string combat_entry_text(uint8_t* rdram, recomp_context* ctx, uint32_t key) {
        if (key >> 16 == 1 || key >> 16 == 3) {
            if (spell_texts.empty()) {
                spell_texts = load_romstrings(rdram, ctx, kSpellStrings, kSpellStringsSize, (int)std::size(kSpellText));
            }
            for (const auto& [id, at] : kSpellText) {
                if (id == (key & 0xFFFF)) return at < spell_texts.size() ? spell_texts[at] : "";
            }
        } else if (key >> 16 == 4) {
            if (skill_texts.empty()) {
                skill_texts = load_romstrings(rdram, ctx, kSkillStrings, kSkillStringsSize, 18);
            }
            const size_t at = (key & 0xFF) == 2 ? 9 : 2;  // Troubador for Perform, Healer for the heals
            return at < skill_texts.size() ? skill_texts[at] : "";
        } else if (key >> 16 == 2) {
            if (potion_texts.empty()) {
                potion_texts = load_romstrings(rdram, ctx, kPotionStrings, kPotionStringsSize, 15);
            }
            const int at = kPotionText[key & 0xFF];
            std::string text = at >= 0 && at < (int)potion_texts.size() ? potion_texts[at] : "";
            const size_t recipe = text.find(" Creating ");  // what it does, not how an Alchemist makes it
            return recipe == std::string::npos ? text : text.substr(0, recipe);
        }
        return "";
    }
}

// Under the description, what the entry takes from the party's pack, with its icon: a spell its ingredient (the spell
// database's SpellRAM +0x23: none, spice, herb or gemstone; a cast uses one, combatEntity.cpp's TakeItem(..., 1)) as
// "1 / <in the pack>", a potion how many there are. A spell cast from an item's charges takes nothing. The count is the
// party's inventory's own (party 0x800D9684 +0x10, its vtable's GetItemIndex +0x50 / +0x54 and GetItemQuantity +0x68 /
// +0x6C, called through get_function), read each frame (it changes as spells are cast). The icon is the game's
// (GetItemImage, then loadBorg8: the item icon hooks give it its own, which the art pack paints), loaded once per item and
// drawn with N64BorgImageDraw (g, img, x, y, xOff, yOff, w, h, xScale, yScale, r, g, b, a).
extern "C" void GetItemImage(uint8_t* rdram, recomp_context* ctx);
extern "C" void borg8_free(uint8_t* rdram, recomp_context* ctx);
extern "C" void N64BorgImageDraw(uint8_t* rdram, recomp_context* ctx);
extern "C" recomp_func_t* get_function(int32_t addr);

namespace {
    constexpr gpr kIconIndexSlot = (gpr)(int32_t)0x80F81700;  // GetItemImage's u32* out (scratch above the game's 15 MB)
    uint16_t footer_item = 0;
    gpr footer_icon = 0;

    // The party's inventory: how many of item there are (0 if none).
    int pack_count(uint8_t* rdram, recomp_context* ctx, uint16_t item) {
        const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684);
        const gpr inv = heap_pointer(party) ? MEM_W(0x10, party) : 0;
        const gpr vt = heap_pointer(inv) ? MEM_W(0, inv) : 0;
        if (vt == 0) return 0;
        recomp_func_t* index_of = get_function(MEM_W(0x54, vt));
        recomp_func_t* quantity = get_function(MEM_W(0x6C, vt));
        if (index_of == nullptr || quantity == nullptr) return 0;
        const int32_t index = (int32_t)call_game(rdram, ctx, index_of, (uint32_t)(inv + (int16_t)MEM_HU(0x50, vt)), item);
        if (index < 0) return 0;
        return (int32_t)call_game(rdram, ctx, quantity, (uint32_t)(inv + (int16_t)MEM_HU(0x68, vt)), (uint32_t)index);
    }

    // The item's icon, loaded once (the last one freed).
    gpr item_icon(uint8_t* rdram, recomp_context* ctx, uint16_t item) {
        if (item == footer_item && heap_pointer(footer_icon)) return footer_icon;
        if (heap_pointer(footer_icon)) call_game(rdram, ctx, borg8_free, (uint32_t)footer_icon);
        footer_icon = 0;
        footer_item = item;
        MEM_W(0, kIconIndexSlot) = 0;
        call_game(rdram, ctx, GetItemImage, item, (uint32_t)kIconIndexSlot);
        const uint32_t index = (uint32_t)MEM_W(0, kIconIndexSlot);
        if (index != 0) footer_icon = (gpr)(int32_t)call_game(rdram, ctx, loadBorg8, index);
        return footer_icon;
    }
}

gpr draw_spell_info(uint8_t* rdram, recomp_context* ctx, gpr g) {
    const auto box = spell_info_box;
    spell_info_box.seen = false;
    const gpr font = MEM_W(0, kFontPtr);
    if (!box.seen || !aidyn::settings::graphics().combat_descriptions || !heap_pointer(font)) {
        return g;
    }
    gpr spell = 0;
    const uint32_t key = combat_entry_key(rdram, &spell);
    if (key == 0) {
        return g;
    }
    constexpr float kScale = 0.72f, kLine = 8.0f, kWidth = 140.0f;
    // Right of the menu's box, centred on its height; in 4:3 (no room right, and the combat log and the controls list
    // on the left) above it instead, under the turn-order bar, wider (clear of the corner portraits). The text is
    // placed from the picture's left edge (hud_begin), the box in the middle 4:3: shift by the picture's extra width.
    float aspect = view_aspect();
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Window && aidyn::window_aspect() > 0.0f) {
        aspect = aidyn::window_aspect();
    }
    const float shift = std::max(0.0f, (240.0f * aspect - 320.0f) / 2.0f);
    constexpr float kAboveWidth = 200.0f, kBarBottom = 28.0f;
    const float right_x = box.x + box.w + 3.0f, right_room = 320.0f + shift - 4.0f - right_x;
    const bool right = right_room >= kWidth;
    const float width = right ? kWidth : kAboveWidth;
    if (key != text_shown || width != spell_width) {
        text_shown = key;
        spell_width = width;
        spell_lines.clear();
        const std::string text = combat_entry_text(rdram, ctx, key);
        const FontState st = font_save(rdram, font);
        call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
        std::string line, word;
        auto flush_word = [&] {
            if (word.empty()) return;
            const std::string tried = line.empty() ? word : line + " " + word;
            if (!line.empty() && text_width(rdram, ctx, font, tried) * (kScale / kLogScale) > width) {
                spell_lines.push_back(line);
                line = word;
            } else {
                line = tried;
            }
            word.clear();
        };
        for (char c : text) {
            if (c == ' ' || c == '\n') {
                flush_word();
                if (c == '\n' && !line.empty()) { spell_lines.push_back(line); line.clear(); }
            } else {
                word += c;
            }
        }
        flush_word();
        if (!line.empty()) spell_lines.push_back(line);
        font_restore(rdram, font, st);
    }
    if (spell_lines.empty()) {
        return g;
    }
    // The pack line: the item, how many it uses (spells) and how many there are.
    uint16_t item = 0;
    std::string pack;
    if (key >> 16 == 1) {
        const gpr db = MEM_W(0, (gpr)(int32_t)0x800E2B30);
        const gpr spells = heap_pointer(db) ? MEM_W(0x10, db) : 0;
        const uint32_t total = heap_pointer(db) ? MEM_BU(0, db) : 0;
        for (uint32_t i = 0; heap_pointer(spells) && i < total; i++) {
            if (MEM_HU(0x26 * i, spells) != (key & 0xFFFF)) continue;
            static constexpr uint16_t kIngredient[] = { 0, 0x111, 0x112, 0x113 };  // none, spice, herb, gemstone
            static constexpr const char* kName[] = { "", "Spice", "Herb", "Gemstone" };
            const uint32_t use = MEM_BU(0x26 * i + 0x23, spells);
            if (use >= 1 && use <= 3) {
                item = kIngredient[use];
                pack = std::string(kName[use]) + "   1 / " + std::to_string(pack_count(rdram, ctx, item));
            }
            break;
        }
    } else if (key == 0x40001u) {  // Heal With Herbs takes a herb (Party::HerbHeal)
        item = 0x112;
        pack = std::string("Herb   1 / ") + std::to_string(pack_count(rdram, ctx, item));
    } else if (key >> 16 == 2) {
        item = (uint16_t)(0x1000 | (key & 0xFF));
        pack = "In your pack: " + std::to_string(pack_count(rdram, ctx, item));
    }
    // The stamina line (#39): what it costs the one acting, and how much stamina they have. A spell: its ST less the
    // caster's rank in it (Entity::CheckSpellWizard), at least 1, as the game charges it (gear with a stamina bonus pays
    // first); Lay On Hands 3 (Party::PrintHeal); Heal With Herbs 5 less the Healer rank, at least 1 (Party::HerbHeal);
    // Perform 1 each round it goes on (CombatTurn). Not for potions or an item's spell.
    std::string stamina;
    {
        const gpr combat = MEM_W(0, kCombatP);
        const gpr ent = heap_pointer(combat) ? MEM_W(kCurrentEnt, combat) : 0;
        const gpr sheet = heap_pointer(ent) ? MEM_W(0x40, ent) : 0;
        const gpr stats = heap_pointer(sheet) ? MEM_W(0x18, sheet) : 0, skills = heap_pointer(sheet) ? MEM_W(0x1C, sheet) : 0;
        int cost = -1;
        const char* each = "";
        if (key >> 16 == 1 && heap_pointer(spell) && heap_pointer(sheet)) {
            const int rank = (int)(call_game(rdram, ctx, Entity__CheckSpellWizard, (uint32_t)sheet, (uint32_t)spell) & 0xFF);
            cost = std::max(1, (int)MEM_BU(0x17, spell) - rank);  // SpellInstance stamina (after the 0x14-byte ItemInstance)
        } else if (key == 0x40000u) {
            cost = 3;
        } else if (key == 0x40001u && heap_pointer(skills)) {
            const int healer = (int)(int8_t)(call_game(rdram, ctx, CharSkills__getModdedSkill, (uint32_t)skills, 2) & 0xFF);
            cost = std::max(1, 5 - healer);
        } else if (key == 0x40002u) {
            cost = 1;
            each = " a round";
        }
        if (cost > 0 && heap_pointer(stats)) {
            const int have = (int)(int32_t)call_game(rdram, ctx, CharStats__getModded, (uint32_t)stats, 5);  // STAT_STAM
            stamina = "Stamina   " + std::to_string(cost) + each + " / " + std::to_string(std::max(0, have));
        }
    }
    constexpr float kIcon = 9.0f;  // the icon's size (its 16x16 texels drawn this many units across)
    const float footer_h = (pack.empty() ? 0.0f : kIcon + 3.0f) + (stamina.empty() ? 0.0f : kLine + (pack.empty() ? 3.0f : 0.0f));
    const float lines_h = spell_lines.size() * kLine + footer_h;
    const float x0 = right ? right_x
                           : std::clamp(box.x + box.w / 2.0f - width / 2.0f, 4.0f - shift, 316.0f + shift - width);
    const float y0 = right ? box.y + std::max(0.0f, (box.h - lines_h) / 2.0f)
                           : std::max(kBarBottom, box.y - 3.0f - lines_h);
    g = hud_begin(rdram, g, false);
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    for (int pass = 0; pass < 2; pass++) {
        GfxCommand a[2];
        const int d = pass == 0 ? 2 : 0;
        gEXSetRectAlign(&a[0], G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, d, d, d, d);
        g = put_cmds(rdram, g, a, 2);
        for (size_t k = 0; k < spell_lines.size(); k++) {
            MEM_W(0x1C, font) = (int32_t)(pass == 0 ? 0x000000E0u : 0xF2E6C8FFu);
            print_text(rdram, ctx, font, g, spell_lines[k], x0 + shift, y0 + k * kLine, kScale);
        }
        if (!pack.empty()) {
            MEM_W(0x1C, font) = (int32_t)(pass == 0 ? 0x000000E0u : 0xD8B070FFu);  // the combat log's gold
            print_text(rdram, ctx, font, g, pack, x0 + shift + kIcon + 3.0f,
                       y0 + spell_lines.size() * kLine + 3.0f + (kIcon - kLine) / 2, kScale);
        }
        if (!stamina.empty()) {  // under the pack line, in the stamina bar's blue
            MEM_W(0x1C, font) = (int32_t)(pass == 0 ? 0x000000E0u : 0x8EC5FFFFu);
            const float sy = y0 + spell_lines.size() * kLine + 3.0f + (pack.empty() ? 0.0f : kIcon + 1.0f);
            print_text(rdram, ctx, font, g, stamina, x0 + shift + kIcon + 3.0f, sy, kScale);
        }
    }
    font_restore(rdram, font, st);
    const gpr icon = pack.empty() ? 0 : item_icon(rdram, ctx, item);
    if (heap_pointer(icon)) {
        const uint32_t iw = MEM_HU(0x0A, icon), ih = MEM_HU(0x0C, icon);
        if (iw != 0 && ih != 0) {
            g = (gpr)(int32_t)call_game_stack(rdram, ctx, N64BorgImageDraw, (uint32_t)g, (uint32_t)icon,
                                              f32_bits(x0 + shift), f32_bits(y0 + spell_lines.size() * kLine + 2.0f),
                                              { 0, 0, iw, ih, f32_bits(kIcon / iw), f32_bits(kIcon / ih), 255, 255, 255, 255 });
        }
    }
    g = hud_end(rdram, g);
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}

// ---- Asset files ----
// Any of the game's assets (a Borg: a mesh, a texture, a model...) can come from a file in place of the ROM's: getBorgItem
// reads the asset's listing (type, compression, compressed and uncompressed sizes, offset) to its stack (+0x18),
// allocates the uncompressed size and decompresses the ROM's data into it, then sets it up (its pointers) as usual. With
// a file for the asset, the listing's size becomes the file's (0x80098D98, after the listing is read: s3 the index) and
// decompressBorg (its entry: a2 the buffer, a3 the size) copies the file in instead of decompressing. Everything after
// is the game's own, so a file in the asset's own format works as the original would. A first test (tools/models/
// borg2_mesh.py: a new mesh for a model from an OBJ); developers only for now: AIDYN_ASSET_DIR=<folder> with
// <index in hex>.bin files (3533.bin: the Small Shield's mesh); AIDYN_ASSET_LOG=1 lists every asset as it loads.
//
// A model's own copy of a mesh: <model>-<mesh>.bin (25cb-203d.bin: Alaron's torso) replaces that mesh in that model only.
// Meshes are shared (Alaron's 17 body parts are in at least six other characters' models), and getBorgItem hands every
// model the same loaded copy unless borgFlag (0x800E4850) is set, when it makes a private one (freed with its model:
// the header's index -1, borg_2_free). InitBorgScene (0x80099B6C, a model's setup: a0 the Borg5Header, its mesh count
// at +0x18 and mesh indices at +0x3C; its own index still in getBorgItem's s3, the header's written after) notes the meshes the model has its own files for; when it asks for
// one, getBorgItem's entry sets borgFlag and the file is that one. The private path also clears the mesh's shared cache
// entry (gBorgpointers[index], gBorgBytes[index]: the arrays at 0x800E4984 / 0x800E4988), which the other models still
// use: they are saved first and put back, with borgFlag, as getBorgItem returns.
namespace {
    struct AssetFile {
        bool checked = false;
        std::vector<uint8_t> bytes;
    };
    std::map<uint32_t, AssetFile> asset_files;
    std::map<std::pair<uint32_t, uint32_t>, AssetFile> model_files;  // (model, mesh) -> its own copy
    uint32_t asset_pending = 0xFFFFFFFF;  // the index getBorgItem is loading from a file, until decompressBorg copies it
    const std::vector<uint8_t>* asset_pending_file = nullptr;

    constexpr gpr kBorgFlag = (gpr)(int32_t)0x800E4850, kBorgPointers = (gpr)(int32_t)0x800E4984,
                  kBorgBytes = (gpr)(int32_t)0x800E4988;
    std::vector<uint32_t> model_pending;  // meshes the model being set up has its own files for
    uint32_t model_current = 0xFFFFFFFF;
    std::map<uint32_t, uint32_t> model_of;  // Borg5Header -> its index, as set up (the header's own isn't kept)
    struct ModelOverride {
        bool active = false;
        uint32_t model = 0, mesh = 0;
        int32_t flag = 0, pointer = 0;
        uint8_t count = 0;
    } model_override;

    const std::vector<uint8_t>* read_asset(AssetFile& f, const char* name, uint32_t index) {
        static const char* dir = std::getenv("AIDYN_ASSET_DIR");
        if (!f.checked) {
            f.checked = true;
            std::ifstream in(std::filesystem::path(dir) / name, std::ios::binary);
            if (in) {
                f.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                fprintf(stdout, "Asset files: %s (%zu bytes) for asset %#x\n", name, f.bytes.size(), index);
                fflush(stdout);
            }
        }
        return f.bytes.empty() ? nullptr : &f.bytes;
    }

    bool assets_on() {
        static const char* dir = std::getenv("AIDYN_ASSET_DIR");
        return dir != nullptr && *dir != '\0';
    }

    const std::vector<uint8_t>* model_file(uint32_t model, uint32_t mesh) {
        if (!assets_on()) return nullptr;
        char name[24];
        snprintf(name, sizeof name, "%04x-%04x.bin", model, mesh);
        return read_asset(model_files[{ model, mesh }], name, mesh);
    }

    const std::vector<uint8_t>* asset_file(uint32_t index) {
        if (!assets_on()) return nullptr;
        char name[16];
        snprintf(name, sizeof name, "%04x.bin", index);
        return read_asset(asset_files[index], name, index);
    }
}

// InitBorgScene's entry: the meshes this model has its own copies of.
extern "C" void aidyn_model_init(uint8_t* rdram, recomp_context* ctx) {
    model_pending.clear();
    if (!assets_on()) return;
    const gpr b5 = ctx->r4;
    if (!heap_pointer(b5)) return;
    // The header's index is only written once the setup is done; getBorgItem, which calls it, still has the index in s3.
    model_current = (uint32_t)ctx->r19;
    model_of[(uint32_t)b5] = model_current;
    const int count = MEM_W(0x18, b5);
    const gpr list = MEM_W(0x3C, b5);
    static const bool log = std::getenv("AIDYN_ASSET_LOG") != nullptr;
    if (log) {
        fprintf(stdout, "Asset files: model %#x set up, %d meshes at %08x\n", model_current, count, (uint32_t)list);
        fflush(stdout);
    }
    if (count <= 0 || count > 256 || !heap_pointer(list)) return;
    for (int k = 0; k < count; k++) {
        const uint32_t mesh = (uint32_t)MEM_W(4 * k, list);
        if (model_file(model_current, mesh) != nullptr) model_pending.push_back(mesh);
    }
}

// getBorgItem's entry (a0 the index): a mesh the model being set up has its own copy of is loaded privately.
extern "C" void aidyn_asset_request(uint8_t* rdram, recomp_context* ctx) {
    if (model_pending.empty() || model_override.active) return;
    const uint32_t index = (uint32_t)ctx->r4;
    const auto it = std::find(model_pending.begin(), model_pending.end(), index);
    if (it == model_pending.end()) return;
    model_pending.erase(it);
    const gpr pointers = MEM_W(0, kBorgPointers), counts = MEM_W(0, kBorgBytes);
    if (!heap_pointer(pointers) || !heap_pointer(counts)) return;
    model_override = { true, model_current, index, MEM_W(0, kBorgFlag), MEM_W(4 * index, pointers),
                       MEM_BU(index, counts) };
    MEM_W(0, kBorgFlag) = 1;
}

// As getBorgItem returns (from aidyn_borg_loaded): the shared entry and borgFlag back as they were.
void asset_request_done(uint8_t* rdram, uint32_t index) {
    if (!model_override.active || index != model_override.mesh) return;
    const gpr pointers = MEM_W(0, kBorgPointers), counts = MEM_W(0, kBorgBytes);
    MEM_W(4 * index, pointers) = model_override.pointer;
    MEM_B(index, counts) = (int8_t)model_override.count;
    MEM_W(0, kBorgFlag) = model_override.flag;
    static const bool log = std::getenv("AIDYN_ASSET_LOG") != nullptr;
    if (log) {
        fprintf(stdout, "Asset files: model %#x has its own copy of mesh %#x\n", model_override.model, index);
        fflush(stdout);
    }
    model_override = {};
}

extern "C" void aidyn_asset_listing(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t index = (uint32_t)ctx->r19;
    static const bool log = std::getenv("AIDYN_ASSET_LOG") != nullptr;  // every asset as it loads: which to replace
    if (log) {
        fprintf(stdout, "Asset files: loading %#x (type %u, %u bytes)\n", index, MEM_HU(0x18, ctx->r29),
                (uint32_t)MEM_W(0x20, ctx->r29));
        fflush(stdout);
    }
    const std::vector<uint8_t>* file = model_override.active && index == model_override.mesh
                                           ? model_file(model_override.model, index) : asset_file(index);
    asset_pending = file != nullptr ? index : 0xFFFFFFFF;
    asset_pending_file = file;
    if (file != nullptr) {
        MEM_W(0x20, ctx->r29) = (int32_t)file->size();  // BorgListing.uncompressed
    }
}

extern "C" int aidyn_asset_decompress(uint8_t* rdram, recomp_context* ctx) {
    if (asset_pending == 0xFFFFFFFF) return 0;
    const std::vector<uint8_t>* file = asset_pending_file;
    if (file == nullptr || (uint32_t)ctx->r7 != file->size()) return 0;
    asset_pending = 0xFFFFFFFF;
    const gpr dest = ctx->r6;
    for (size_t i = 0; i < file->size(); i++) {
        MEM_B(i, dest) = (int8_t)(*file)[i];
    }
    return 1;
}

// ---- Skinned models ----
// A skinned character in place of a rigid model (a test, with the asset files above; tools/models/skinned_model.py
// writes both from a GLB). The game draws a character as rigid parts, each a mesh with its own matrix; a skinned mesh
// bends at the joints, which its formats can't hold. So the model's torso mesh (the carrier) holds the whole skinned
// mesh, the other parts are emptied, and every time the model is drawn (BorgAnimDrawSceneRaw's entry) its vertices
// are skinned here from <model>.skin: the frame of the animation the scene plays (SceneData.borg6's index, aniTime at
// +0x1F8) gives each joint's matrix (mesh rest -> model space, the root transform's own motion left out), each vertex
// blends its four joints, and positions (in the carrier's units: v * scale / 16 is metres) and normals go into the
// carrier's vertices (Vtx +6, unused by the RSP, holds which skinned vertex each is). Two vertex buffers in turn (the
// blob carries a second): Borg2Data.vertlist2 (+0x34) is pointed at the one just filled. The carrier's transform (6) is
// then given the root's matrix (transform 2, mfs[1]: the Borg5Struct2 at transform +0xC, mfs[1] at +0xC0), the space
// the skinned vertices are in. An animation the file lacks shows its first one's first frame.
namespace {
    struct Skin {
        uint32_t verts = 0, joints = 0, slot = 0, off_vtx = 0, off_vtx2 = 0, blob_verts = 0;
        struct Vert { float pos[3], nrm[3]; uint8_t joint[4]; float weight[4]; };
        std::vector<Vert> vert;
        std::map<uint32_t, std::pair<uint32_t, std::vector<float>>> anims;  // borg6 -> (frames, 12 floats per joint)
        uint32_t first = 0;
        bool flip = false;
    };
    std::map<uint32_t, std::unique_ptr<Skin>> skins;  // model -> its skin, or null

    Skin* skin_for(uint32_t model) {
        auto it = skins.find(model);
        if (it != skins.end()) return it->second.get();
        std::unique_ptr<Skin> s;
        char name[24];
        snprintf(name, sizeof name, "%04x.skin", model);
        std::ifstream in(std::filesystem::path(std::getenv("AIDYN_ASSET_DIR")) / name, std::ios::binary);
        if (in) {
            std::vector<char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            uint32_t h[10];
            if (b.size() >= sizeof h && std::memcmp(b.data(), "AKS1", 4) == 0) {
                std::memcpy(h, b.data(), sizeof h);
                s = std::make_unique<Skin>();
                s->verts = h[2], s->joints = h[3], s->slot = h[5], s->off_vtx = h[6], s->off_vtx2 = h[7],
                s->blob_verts = h[8];
                size_t o = sizeof h;
                s->vert.resize(s->verts);
                for (auto& v : s->vert) {
                    std::memcpy(v.pos, &b[o], 24), std::memcpy(v.joint, &b[o + 24], 4),
                        std::memcpy(v.weight, &b[o + 28], 16);
                    o += 44;
                }
                for (uint32_t a = 0; a < h[4] && o + 8 <= b.size(); a++) {
                    uint32_t idx, frames;
                    std::memcpy(&idx, &b[o], 4), std::memcpy(&frames, &b[o + 4], 4);
                    o += 8;
                    std::vector<float> k(size_t(frames) * s->joints * 12);
                    std::memcpy(k.data(), &b[o], k.size() * 4);
                    o += k.size() * 4;
                    if (a == 0) s->first = idx;
                    s->anims[idx] = { frames, std::move(k) };
                }
                fprintf(stdout, "Skinned models: %s, %u vertices, %u joints, %zu animations\n", name, s->verts,
                        s->joints, s->anims.size());
                fflush(stdout);
            }
        }
        return (skins[model] = std::move(s)).get();
    }
}

void skinned_draw(uint8_t* rdram, gpr scene) {
    if (!assets_on()) return;
    const gpr b5 = MEM_W(0, scene);
    if (!heap_pointer(b5)) return;
    const auto m = model_of.find((uint32_t)b5);
    if (m == model_of.end()) return;
    Skin* s = skin_for(m->second);
    if (s == nullptr) return;
    const gpr list = MEM_W(0x3C, b5);  // Borg5Data.borg2p
    if ((uint32_t)MEM_W(0x18, b5) <= s->slot || !heap_pointer(list)) return;
    const gpr b2 = MEM_W(4 * s->slot, list);
    if (!heap_pointer(b2)) return;
    const gpr dat = MEM_W(0x58, b2);  // Borg2Header.dat
    if (!heap_pointer(dat) || (uint32_t)MEM_W(0x38, dat) != s->blob_verts) return;  // not the carrier
    const gpr transforms = MEM_W(0x28, b5);
    if (!heap_pointer(transforms)) return;
    const gpr root = MEM_W(0xC, transforms + 2 * 0x40), carrier = MEM_W(0xC, transforms + 6 * 0x40);
    if (!heap_pointer(root) || !heap_pointer(carrier)) return;

    // The animation and frame.
    const gpr b6 = MEM_W(4, scene);
    auto it = heap_pointer(b6) ? s->anims.find((uint32_t)MEM_W(0, b6)) : s->anims.end();
    uint32_t frame = 0;
    if (it == s->anims.end()) {
        it = s->anims.find(s->first);
    } else {
        const uint32_t t = MEM_HU(0x1F8, scene);
        frame = std::min<uint32_t>(t > 0 ? t - 1 : 0, it->second.first - 1);
    }
    const float* k = it->second.second.data() + size_t(frame) * s->joints * 12;

    // Skin every vertex: position and normal, in model space.
    static std::vector<float> out;
    out.resize(size_t(s->verts) * 6);
    for (uint32_t i = 0; i < s->verts; i++) {
        const Skin::Vert& v = s->vert[i];
        float p[3] = {}, n[3] = {};
        for (int j = 0; j < 4; j++) {
            const float w = v.weight[j];
            if (w == 0.0f || v.joint[j] >= s->joints) continue;
            const float* m = k + v.joint[j] * 12;
            for (int r = 0; r < 3; r++) {
                p[r] += w * (m[4 * r] * v.pos[0] + m[4 * r + 1] * v.pos[1] + m[4 * r + 2] * v.pos[2] + m[4 * r + 3]);
                n[r] += w * (m[4 * r] * v.nrm[0] + m[4 * r + 1] * v.nrm[1] + m[4 * r + 2] * v.nrm[2]);
            }
        }
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int r = 0; r < 3; r++) out[size_t(i) * 6 + r] = p[r], out[size_t(i) * 6 + 3 + r] = l > 0 ? n[r] / l : 0;
    }

    // Into the buffer not drawn last time, in the carrier's units.
    const float unit = 16.0f / read_f32(rdram, dat + 8);  // Borg2Data.scale
    s->flip = !s->flip;
    const gpr vtx = dat + (s->flip ? s->off_vtx2 : s->off_vtx);
    for (uint32_t i = 0; i < s->blob_verts; i++) {
        const gpr a = vtx + 16 * i;
        const uint32_t id = MEM_HU(6, a);
        if (id >= s->verts) continue;
        const float* o = &out[size_t(id) * 6];
        for (int r = 0; r < 3; r++) {
            MEM_H(2 * r, a) = (int16_t)std::clamp(std::lround(o[r] * unit), -32768L, 32767L);
            MEM_B(12 + r, a) = (int8_t)std::clamp(std::lround(o[3 + r] * 127.0f), -127L, 127L);
        }
    }
    MEM_W(0x34, dat) = (int32_t)vtx;  // Borg2Data.vertlist2: segment A for this draw

    // The carrier draws with the root's matrix.
    for (int w = 0; w < 16; w++) MEM_W(0xC0 + 4 * w, carrier) = MEM_W(0xC0 + 4 * w, root);
}

// ---- Buckler ----
// The Buckler's model (BORG5_ShieldBuckler; its vertices and display list are BORG2 asset 0x3520) was exported with
// every texture coordinate inside a patch of about 5x5 of its texture's 32x32 texels, so in the game its face shows a
// flat dark red-brown, the N64's picture and the HD one alike: the red studded leather with its gold bands never
// shows (every other shield covers its texture at least half). getBorgItem is hooked as it returns the loaded asset:
// the 33 vertices of its display list (Borg2Data vertlist2 at +0x34, count at +0x38; Vtx s, t at +8, +10) are given
// coordinates projected flat on the shield (x across, y down), so the texture covers it once. Only the model as the 1.0 ROM has it is changed (checked
// by its first vertex); a model already done gets the same values again (the asset stays loaded between uses).
namespace {
    constexpr uint32_t kBucklerVerts = 0x3520;
    constexpr int kBorg2Dat = 0x58;  // Borg2Header: head 8, lookat[2] 8, MtxF 64, dlist, dlistSet, dat
}

void asset_request_done(uint8_t* rdram, uint32_t index);  // "Asset files", above

extern "C" void aidyn_borg_loaded(uint8_t* rdram, uint32_t index, gpr header) {
    asset_request_done(rdram, index);
    if (index != kBucklerVerts || !heap_pointer(header)) {
        return;
    }
    const gpr dat = MEM_W(kBorg2Dat, header);
    if (!heap_pointer(dat) || MEM_W(4, dat) != 1 || MEM_W(0x38, dat) != 33) {
        return;
    }
    const gpr verts = MEM_W(0x34, dat);
    if (!heap_pointer(verts) || MEM_H(0, verts) != -98 || MEM_H(2, verts) != 0) {
        return;
    }
    const int count = MEM_W(0x38, dat);
    int x0 = 0x7FFF, x1 = -0x8000, y0 = 0x7FFF, y1 = -0x8000;
    for (int i = 0; i < count; i++) {
        const gpr v = verts + 16 * i;
        x0 = std::min<int>(x0, MEM_H(0, v)), x1 = std::max<int>(x1, MEM_H(0, v));
        y0 = std::min<int>(y0, MEM_H(2, v)), y1 = std::max<int>(y1, MEM_H(2, v));
    }
    constexpr float kPerTexel = 64.0f;  // the vertex format's S10.5, under the game's gSPTexture scale of one half
    for (int i = 0; i < count; i++) {
        const gpr v = verts + 16 * i;
        const float u = (MEM_H(0, v) - x0) / (float)(x1 - x0), w = (y1 - MEM_H(2, v)) / (float)(y1 - y0);
        MEM_H(8, v) = (int16_t)std::lround(u * 32.0f * kPerTexel);
        MEM_H(10, v) = (int16_t)std::lround(w * 32.0f * kPerTexel);
    }
}
// ---- FPS counter ----
// settings.json graphics.fps_counter (the Display page): the frames shown a second at the picture's top right corner,
// over everything (drawn last in Graphics::EndList), in the game's font with its HD letters and the combat log's soft
// shadow. The number is RT64's present timing (aidyn::shown_fps, twice a second): the frames you see, above the game's
// own 60 when RT64 draws in-between frames.
namespace aidyn {
    float shown_fps();  // src/main/rt64_context.cpp
}

gpr draw_fps_counter(uint8_t* rdram, recomp_context* ctx, gpr g) {
    const gpr font = MEM_W(0, kFontPtr);
    if (!aidyn::settings::graphics().fps_counter || !heap_pointer(font)) {
        return g;
    }
    // Top right (the controls card is at the top left while walking): its right end 6 texels from the edge, or left
    // of the fast-forward marker (x 292..312) while that shows.
    constexpr float kY = 9.0f, kScale = 0.7f;
    const std::string text = std::to_string((int)std::lround(aidyn::shown_fps())) + " FPS";
    put_text(rdram, text);
    const float width = (float)(int32_t)call_game(rdram, ctx, Font__GetWidthScaled, (uint32_t)font, (uint32_t)kLogText,
                                                  f32_bits(kScale));
    const float kX = (aidyn::input::fast_forward() ? 288.0f : 314.0f) - width;
    g = hud_begin(rdram, g, true);
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    for (int pass = 0; pass < 2; pass++) {
        GfxCommand a[2];
        const int d = pass == 0 ? 2 : 0;  // the shadow: the same text a little down and right
        const int off = -320 * 4;          // from the picture's right edge, as hud_begin(right)
        gEXSetRectAlign(&a[0], G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, off + d, d, off + d, d);
        g = put_cmds(rdram, g, a, 2);
        MEM_W(0x1C, font) = (int32_t)(pass == 0 ? 0x000000E0u : 0xF2E6C8FFu);  // ivory, as the page words
        put_text(rdram, text);
        MEM_W(0, kLogGfx) = (int32_t)g;
        call_game_stack(rdram, ctx, func_800AA6E8, (uint32_t)font, (uint32_t)kLogGfx, (uint32_t)kLogText,
                        (uint32_t)(int)kX, { (uint32_t)(int)kY, f32_bits(kScale), f32_bits(kScale) });
        g = (gpr)(int32_t)MEM_W(0, kLogGfx);
    }
    font_restore(rdram, font, st);
    g = hud_end(rdram, g);
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}

// ---- Stuck checks per tick ----
// Two of the game's checks that a character is stuck measure progress per frame, and the N64 ran a frame every 3 or
// so game ticks (15-20 a second); at 60 frames a second a frame is 1 or 2 ticks, the same walk covers a third as much
// ground in one, and the checks fire on characters that are walking normally: in crowded fights enemies stop short
// and give up (spitzdano: "they get stuck and run into each other"). Both thresholds are scaled by the frame's ticks
// over 3, so a frame of 3 or more ticks is judged exactly as on the N64:
//   Actor::MoveTo (func_800159C8): it re-aims at its waypoint only once it has come 0.01 closer since it last did
//   (else aiTravelTime grows by the frame's ticks, and in combat the move is given up at 30), the double in f0 at
//   0x80015A34 (from 0x800CDCC8), against f2;
//   CombatStateFunc_5: the move's waitTimer (240) counts down every frame the mover went under 0.03 (its distance x
//   100 as a byte in v0, compared with 3 at 0x800220B0; the frame's ticks are the delta in s7); at 0 the move ends
//   where it is. The measured distance is scaled up instead (so the byte's own wrap above 2.55 stays as it was).
namespace {
    float frame_ticks(uint8_t* rdram) {
        return read_f32(rdram, (gpr)(int32_t)0x800D8094);  // gGlobals' frame delta (MoveTo's own aiTravelTime step)
    }
}

// AIDYN_COMBAT_MOVE_LOG=<file>: every frame of a fight, the combat state, whose turn it is and the move's waitTimer,
// and per fighter its position, collision radius, flags, aiTravelTime and destination: to see why characters stop.
void combat_move_log(uint8_t* rdram) {
    static FILE* f = [] {
        const char* path = std::getenv("AIDYN_COMBAT_MOVE_LOG");
        return (path != nullptr && *path != '\0') ? std::fopen(path, "w") : nullptr;
    }();
    static uint32_t frame = 0;
    if (f == nullptr) return;
    frame++;
    const gpr combat = MEM_W(0, (gpr)(int32_t)0x800E0BF0);
    if (!heap_pointer(combat)) return;
    const gpr current = MEM_W(0x2C, combat);
    std::fprintf(f, "%u state %u turn %d wait %d", frame, MEM_BU(0, (gpr)(int32_t)0x800D9454),
                 heap_pointer(current) ? (int)MEM_BU(0x20, current) : -1, (int)MEM_W(0x4FBC, combat));
    const int ents = std::min<int>(MEM_BU(0x01, combat), 18);
    for (int i = 0; i < ents; i++) {
        const gpr ent = MEM_W(0x52D0 + 4 * i, combat);
        if (!heap_pointer(ent)) continue;
        const int index = MEM_BU(0x20, ent);
        const gpr actor = index < 14 ? MEM_W(4 * index, (gpr)(int32_t)0x800D9488) : 0;
        if (!heap_pointer(actor)) continue;
        std::fprintf(f, " | %d %.3f %.3f r%.3f s%.2f c%.3f f%04x t%d d%.3f,%.3f", index, read_f32(rdram, actor + 0x68),
                     read_f32(rdram, actor + 0x70), read_f32(rdram, actor + 0x64), read_f32(rdram, actor + 0x30),
                     read_f32(rdram, actor + 0x38), MEM_HU(0x1E, actor), (int)(int16_t)MEM_HU(0x25C, actor), read_f32(rdram, actor + 0x260),
                     read_f32(rdram, actor + 0x264));
    }
    std::fprintf(f, "\n");
    if (frame % 60 == 0) std::fflush(f);
}

// ---- Crowded fights: moving around each other ----
// In a crowd the original AI walks every attacker straight at its waypoint and pushes into whoever is in the way
// (characters are spheres pushed apart, and a mover touching two is put back where it was): it stalls until MoveTo's
// 30 ticks without progress run out (its aiTravelTime, which the game never resets within a move) and gives up where
// it stands; and a move planned onto a cell the grid thinks free often ends against a body there (a character's grid
// cell is its position rounded down, up to a cell from where it stands). spitzdano's wolves fight (four wolves on
// Rheda) logged every wolf move ending that way. In a fight's move (state 5, the fighter whose turn it is), at
// MoveTo's progress check:
//   - a fighter pressed against the mover and ahead of it (within their radii and 0.15, in front): the mover is
//     turned to slide along its side (keeping the same side, so it doesn't swing between them), for up to 2 seconds;
//   - one standing on the mover's waypoint (it can't be reached): the mover takes the nearest free spot touching its
//     target instead (16 round it; up to twice a move), and if there's none, or the one in the way is its target
//     (it has arrived), the move gives up at once (aiTravelTime past its limit), as the game's own give-up would
//     after its 30 ticks, the AI acting from where it stands (attacking, in reach);
//   - progress again resets aiTravelTime, so a few stalled frames early in a long move no longer end it later.
namespace {
    constexpr gpr kCombatActors = (gpr)(int32_t)0x800D9488;  // gGlobals.combatActors[14]
    struct Slide { uint32_t actor = 0; int side = 0; int frames = 0, retargets = 0; };
    Slide slide;  // the move under way (a new one: another mover, or half a second since the last step)
    std::chrono::steady_clock::time_point slide_seen;

    bool combat_mover(uint8_t* rdram, gpr actor) {
        if (MEM_BU(0, (gpr)(int32_t)0x800D9454) != 5) return false;  // combatBytes[0]: a move
        const gpr combat = MEM_W(0, (gpr)(int32_t)0x800E0BF0);
        if (!heap_pointer(combat)) return false;
        const gpr current = MEM_W(0x2C, combat);
        if (!heap_pointer(current)) return false;
        const int index = MEM_BU(0x20, current);
        return index < 14 && MEM_W(4 * index, kCombatActors) == (int32_t)actor;
    }

    // The fighter whose turn it is: its target's actor, or 0. An AI's is its CombatAIInfo's entIndex (aiP +0x44,
    // the s8 at +5), a party member's CombatEntity.TargetIndex (+0x10).
    gpr mover_target(uint8_t* rdram) {
        const gpr combat = MEM_W(0, (gpr)(int32_t)0x800E0BF0);
        const gpr current = heap_pointer(combat) ? MEM_W(0x2C, combat) : 0;
        if (!heap_pointer(current)) return 0;
        const gpr ai = MEM_W(0x44, current);
        const int32_t t = heap_pointer(ai) ? (int32_t)MEM_B(5, ai) : MEM_W(0x10, current);
        if (t < 0 || t >= std::min<int>(MEM_BU(0x01, combat), 18)) return 0;
        const gpr ent = MEM_W(0x52D0 + 4 * t, combat);
        if (!heap_pointer(ent)) return 0;
        const int index = MEM_BU(0x20, ent);
        return index < 14 ? MEM_W(4 * index, kCombatActors) : 0;
    }

    float actor_radius(uint8_t* rdram, gpr a) {
        return read_f32(rdram, a + 0x64) * read_f32(rdram, a + 0x30);
    }

    // A free spot touching the target (nobody else within their radii and 0.05 of it), nearest the mover.
    bool free_spot_by(uint8_t* rdram, gpr actor, gpr target, float* sx, float* sz) {
        const float px = read_f32(rdram, actor + 0x68), pz = read_f32(rdram, actor + 0x70), rm = actor_radius(rdram, actor);
        const float tx = read_f32(rdram, target + 0x68), tz = read_f32(rdram, target + 0x70);
        const float ring = rm + actor_radius(rdram, target) + 0.15f;
        float best = 1e9f;
        for (int k = 0; k < 16; k++) {
            const float a = (float)k * 3.14159265f / 8.0f, cx = tx + ring * std::cos(a), cz = tz + ring * std::sin(a);
            bool free = true;
            for (int i = 0; i < 14 && free; i++) {
                const gpr o = MEM_W(4 * i, kCombatActors);
                if (!heap_pointer(o) || o == actor || o == target || MEM_H(2, o) == 0 || (MEM_HU(0x1E, o) & 0x400) != 0) continue;
                free = std::hypot(read_f32(rdram, o + 0x68) - cx, read_f32(rdram, o + 0x70) - cz) >= rm + actor_radius(rdram, o) + 0.05f;
            }
            const float d = std::hypot(cx - px, cz - pz);
            if (free && d < best) best = d, *sx = cx, *sz = cz;
        }
        return best < 1e9f;
    }
}

extern "C" void aidyn_moveto_progress(uint8_t* rdram, recomp_context* ctx) {
    const float ticks = std::clamp(frame_ticks(rdram), 1.0f, 3.0f);
    ctx->f0.d *= ticks / 3.0;
    const gpr actor = ctx->r16, sp = ctx->r29;
    if (!heap_pointer(actor) || !combat_mover(rdram, actor)) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (slide.actor != (uint32_t)actor || now - slide_seen > std::chrono::milliseconds(500)) {
        slide = { (uint32_t)actor, 0, 0, 0 };
    }
    slide_seen = now;
    const bool progress = ctx->f0.d < ctx->f2.d;
    if (progress) {
        MEM_H(0x25C, actor) = 0;  // aiTravelTime: counts stalls since the last progress
    }
    // The way to the waypoint, as MoveTo has it at sp+0x10: (x, -z).
    const float px = read_f32(rdram, actor + 0x68), pz = read_f32(rdram, actor + 0x70);
    const float wx = read_f32(rdram, actor + 0x260), wz = read_f32(rdram, actor + 0x264);
    float dx = read_f32(rdram, sp + 0x10), dy = read_f32(rdram, sp + 0x14);
    const float dl = std::sqrt(dx * dx + dy * dy);
    if (dl < 1e-4f) return;
    dx /= dl, dy /= dl;
    const float rm = read_f32(rdram, actor + 0x64) * read_f32(rdram, actor + 0x30);
    gpr blocker = 0;
    float bx = 0, by = 0, best = 1e9f;
    for (int i = 0; i < 14; i++) {
        const gpr other = MEM_W(4 * i, kCombatActors);
        if (!heap_pointer(other) || other == actor || MEM_H(2, other) == 0 || (MEM_HU(0x1E, other) & 0x400) != 0) continue;
        const float ox = read_f32(rdram, other + 0x68), oz = read_f32(rdram, other + 0x70);
        const float ro = read_f32(rdram, other + 0x64) * read_f32(rdram, other + 0x30);
        const float vx = ox - px, vy = -(oz - pz), d = std::sqrt(vx * vx + vy * vy);
        if (d > rm + ro + 0.15f || d < 1e-4f || (vx * dx + vy * dy) / d < 0.3f) continue;
        if (std::hypot(ox - wx, oz - wz) < rm + ro) {  // on the waypoint: a free spot by the target, or give up
            const gpr target = mover_target(rdram);
            float sx = 0, sz = 0;
            if (heap_pointer(target) && other != target && slide.retargets < 2 && free_spot_by(rdram, actor, target, &sx, &sz)) {
                slide.retargets++;
                slide.side = 0, slide.frames = 0;
                write_f32(rdram, actor + 0x260, sx);
                write_f32(rdram, actor + 0x264, sz);
                const float nx = sx - px, ny = -(sz - pz), nl = std::max(1e-4f, std::sqrt(nx * nx + ny * ny));
                write_f32(rdram, sp + 0x10, nx / nl);
                write_f32(rdram, sp + 0x14, ny / nl);
                MEM_H(0x25C, actor) = 0;
                ctx->f0.d = -1e30;  // re-aim at the new spot this frame
                return;
            }
            MEM_H(0x25C, actor) = 0x7000;  // past its limit (30, or 300), with room for the frame's ticks
            ctx->f0.d = 1e30;  // the no-progress branch, which gives the move up
            return;
        }
        if (d < best) best = d, blocker = other, bx = vx / d, by = vy / d;
    }
    if (blocker == 0) {
        slide.side = 0, slide.frames = 0;  // (the retargets count for the whole move)
        return;
    }
    if (++slide.frames > 120) return;  // 2 seconds: the game's own stall and give-up take over
    if (slide.side == 0) {  // the side nearer the waypoint, kept from then on
        slide.side = (-by * dx + bx * dy) >= 0.0f ? 1 : -1;
    }
    float tx = -by * (float)slide.side, ty = bx * (float)slide.side;  // along the blocker's side
    tx = tx * 0.85f + dx * 0.15f, ty = ty * 0.85f + dy * 0.15f;
    const float tl = std::sqrt(tx * tx + ty * ty);
    write_f32(rdram, sp + 0x10, tx / tl);
    write_f32(rdram, sp + 0x14, ty / tl);
    ctx->f0.d = -1e30;  // re-aim this frame (MoveTo turns toward sp+0x10)
}

extern "C" void aidyn_combat_move_watch(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t moved = (uint32_t)ctx->r2 & 0xFF, ticks = std::clamp<uint32_t>((uint32_t)ctx->r23 & 0xFFFF, 1, 3);
    if (moved < 3 && ticks < 3) {
        ctx->r2 = (gpr)(moved * 3 / ticks);
    }
}

// ---- Restored instrument samples (#12) ----
// The game's instrument samples (Borg11 records: flag +0, BORG11_8bit 0x4 or BORG11_16BE 0x8; len in bytes +4;
// samples +8; wav +0xC) were filtered when they were made: half of the notes the music plays have nothing above
// about 3.3 kHz. The rechronicled-samples pack (<data folder>/sample-packs/rechronicled-samples, or beside the
// executable; made by tools/audio/sample_pack.py, kept in the sound/ submodule) has each one the music plays restored: redrawn whole by AudioSR (the lost treble back,
// the 8-bit hiss gone), at its own length and loop points, as 16-bit mono WAVs named by the key the sample's own data
// gives, "<samples>-<flag>-<FNV-1a of its first 256 bytes>". When the music starts a note (DCM AddVoice,
// func_8008F778's entry, through src/game/music_log.cpp) on a sample the pack has, the sample's record is pointed at
// a block of the game's heap (operator new) holding the restored one, 16-bit big-endian, its format and length
// changed to match: the game's own mixer plays it, with the same loop points, volumes and pans. The block goes with
// the sample (HeapFree on the record: the "Heap trace" hook below); switched off (settings.json
// audio.restored_samples, the ReChronicled page's Instruments row), each record gets its own data back on its next
// note and the block is freed a few seconds later (a voice still playing reads it until then). The audio microcode
// reaches only the first 16 MB (its DMA masks to 24 bits), hence the game's heap and not the port's.
extern "C" void new_(uint8_t* rdram, recomp_context* ctx);
extern "C" void delete_(uint8_t* rdram, recomp_context* ctx);

namespace {
    struct PatchedSample {
        uint32_t flag, len, wav;  // the record's own
        gpr block;                // ours
    };
    std::recursive_mutex samples_lock;  // (freeing a block goes through HeapFree, whose hook takes it again)
    std::unordered_map<std::string, std::vector<int16_t>> restored_pcm;  // by key, loaded once
    std::unordered_map<uint32_t, PatchedSample> patched_samples;         // by the record's address
    std::vector<std::pair<gpr, std::chrono::steady_clock::time_point>> pending_frees;
    std::once_flag restored_loaded;

    // <data folder>/sample-packs/rechronicled-samples, else sample-packs next to the executable (a release's copy, as
    // its texture packs).
    std::filesystem::path restored_pack() {
        const std::filesystem::path data = aidyn::settings::folder() / "sample-packs" / "rechronicled-samples";
        std::error_code ec;
        if (std::filesystem::is_directory(data, ec)) return data;
        return aidyn::platform::exe_path().parent_path() / "sample-packs" / "rechronicled-samples";
    }

    // A pack WAV: 16-bit mono PCM, the "data" chunk.
    std::vector<int16_t> read_wav16(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        char riff[12];
        if (!in.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) return {};
        uint16_t channels = 0, bits = 0;
        for (;;) {
            char id[4];
            uint32_t size = 0;
            if (!in.read(id, 4) || !in.read(reinterpret_cast<char*>(&size), 4)) return {};
            if (std::memcmp(id, "fmt ", 4) == 0) {
                std::vector<char> fmt(size);
                if (!in.read(fmt.data(), size) || size < 16) return {};
                std::memcpy(&channels, fmt.data() + 2, 2);
                std::memcpy(&bits, fmt.data() + 14, 2);
            } else if (std::memcmp(id, "data", 4) == 0) {
                if (channels != 1 || bits != 16) return {};
                std::vector<int16_t> pcm(size / 2);
                in.read(reinterpret_cast<char*>(pcm.data()), (std::streamsize)(pcm.size() * 2));
                return pcm;
            } else {
                in.seekg(size + (size & 1), std::ios::cur);
            }
        }
    }

    void load_restored() {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(restored_pack(), ec)) {
            if (e.path().extension() != ".wav") continue;
            std::vector<int16_t> pcm = read_wav16(e.path());
            if (!pcm.empty()) restored_pcm.emplace(e.path().stem().string(), std::move(pcm));
        }
        if (!restored_pcm.empty()) {
            fprintf(stderr, "Restored samples: %zu from %s\n", restored_pcm.size(), restored_pack().string().c_str());
        }
    }

    std::string restored_key(uint8_t* rdram, gpr sample) {
        const uint32_t len = (uint32_t)MEM_W(4, sample);
        const gpr wav = MEM_W(0xC, sample);
        uint32_t h = 0x811C9DC5u;
        for (uint32_t i = 0; i < len && i < 256; i++) {
            h = (h ^ MEM_BU(i, wav)) * 0x01000193u;
        }
        char key[48];
        std::snprintf(key, sizeof key, "%u-%u-%08x", (uint32_t)MEM_W(8, sample), (uint32_t)MEM_W(0, sample), h);
        return key;
    }

    void free_due(uint8_t* rdram, recomp_context* ctx, bool all) {
        const auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < pending_frees.size();) {
            if (all || now - pending_frees[i].second > std::chrono::seconds(4)) {
                call_game(rdram, ctx, delete_, (uint32_t)pending_frees[i].first);
                pending_frees.erase(pending_frees.begin() + (long)i);
            } else {
                i++;
            }
        }
    }
}

bool aidyn_restored_samples_available() {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(restored_pack(), ec)) {
        if (e.path().extension() == ".wav") return true;
    }
    return false;
}

// The sample's own format, length and data (as the game made it, restored or not), for the music log.
void aidyn_sample_original(uint8_t* rdram, gpr sample, uint32_t* flag, uint32_t* len, uint32_t* wav) {
    std::lock_guard g(samples_lock);
    const auto it = patched_samples.find((uint32_t)sample);
    if (it != patched_samples.end() && (uint32_t)MEM_W(0xC, sample) == (uint32_t)it->second.block) {
        *flag = it->second.flag, *len = it->second.len, *wav = it->second.wav;
    } else {
        *flag = (uint32_t)MEM_W(0, sample), *len = (uint32_t)MEM_W(4, sample), *wav = (uint32_t)MEM_W(0xC, sample);
    }
}

// AddVoice's entry: the note's sample (a2) restored, or its own again.
extern "C" void aidyn_samples_voice(uint8_t* rdram, recomp_context* ctx) {
    const gpr sample = ctx->r6;
    if (!heap_pointer(sample)) return;
    std::call_once(restored_loaded, load_restored);
    std::lock_guard g(samples_lock);
    free_due(rdram, ctx, false);
    if (restored_pcm.empty()) return;
    auto it = patched_samples.find((uint32_t)sample);
    if (it != patched_samples.end() && (uint32_t)MEM_W(0xC, sample) != (uint32_t)it->second.block) {
        patched_samples.erase(it);  // a record at a freed one's address: not ours
        it = patched_samples.end();
    }
    const bool on = aidyn::settings::audio().restored_samples;
    if (it != patched_samples.end()) {
        if (!on) {  // switched off: its own data back, the block freed once no voice can still be reading it
            MEM_W(0, sample) = (int32_t)it->second.flag;
            MEM_W(4, sample) = (int32_t)it->second.len;
            MEM_W(0xC, sample) = (int32_t)it->second.wav;
            pending_frees.push_back({ it->second.block, std::chrono::steady_clock::now() });
            patched_samples.erase(it);
        }
        return;
    }
    if (!on) return;
    const uint32_t flag = (uint32_t)MEM_W(0, sample), frames = (uint32_t)MEM_W(8, sample);
    const auto r = restored_pcm.find(restored_key(rdram, sample));
    if (r == restored_pcm.end() || r->second.size() != frames || (flag & 0xC) == 0) return;
    const gpr block = (gpr)(int32_t)call_game(rdram, ctx, new_, frames * 2);
    if (!heap_pointer(block)) return;
    for (uint32_t i = 0; i < frames; i++) MEM_H(2 * i, block) = r->second[i];
    patched_samples[(uint32_t)sample] = { flag, (uint32_t)MEM_W(4, sample), (uint32_t)MEM_W(0xC, sample), block };
    MEM_W(0, sample) = (int32_t)((flag & ~0x4u) | 0x8u);  // 16-bit big-endian
    MEM_W(4, sample) = (int32_t)(frames * 2);
    MEM_W(0xC, sample) = (int32_t)block;
}

namespace {
    // HeapFree's entry: a restored sample's record going (its Borg11 freed): our block with it.
    void restored_sample_freed(uint8_t* rdram, recomp_context* ctx, gpr p) {
        std::lock_guard g(samples_lock);
        const auto it = patched_samples.find((uint32_t)p);
        if (it == patched_samples.end()) return;
        const gpr block = it->second.block;
        const bool ours = (uint32_t)MEM_W(0xC, p) == (uint32_t)block;
        patched_samples.erase(it);
        if (ours) call_game(rdram, ctx, delete_, (uint32_t)block);
    }
}

// ---- Heap trace ----
// spitzdano's game crashed at draw distance 3 near the hut by the first forest (2026-10-19): a prop's model (a SceneData
// one of the game's own cells still pointed at, its second detail level) had been overwritten with zeros, and
// BorgAnimDrawSceneRaw read through its zeroed Borg5 pointer. Such a model is now skipped (aidyn_scene_draw_begin)
// and reported once on stdout. AIDYN_HEAP_TRACE=1 also finds what did it: the game's HeapAlloc, HeapFree, _bzero,
// memset and AllocFreeQueueItem (models are freed through its free queue: who queued one) are recorded in a ring
// with the host call stack, and at the end of every frame every prop model the maps point at (the game's 3x3 cells,
// the far props' maps) is checked: its heap block still in use (the size tag before it has bit 0 set) and its Borg5
// pointer intact. The first one that isn't goes to heap-trace.txt in the settings folder with every recorded event
// that touched its block.

namespace {
    bool heap_trace_on() {
        static const bool on = [] {
            const char* v = std::getenv("AIDYN_HEAP_TRACE");
            return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
        }();
        return on;
    }

    struct HeapEvent {
        uint32_t frame;
        char kind;  // A alloc, F free, Z bzero, M memset, Q queued to be freed
        uint32_t addr, size;
        int depth;
        void* stack[10];
    };
    constexpr size_t kHeapRing = 1 << 18;
    std::vector<HeapEvent> heap_ring;
    size_t heap_ring_next = 0;
    uint32_t heap_frame = 0;
    bool heap_reported = false;
    HeapEvent alloc_pending;

    void heap_record(char kind, uint32_t addr, uint32_t size, const HeapEvent* stack_from = nullptr) {
        if (heap_ring.empty()) {
            heap_ring.resize(kHeapRing);
        }
        HeapEvent& e = heap_ring[heap_ring_next++ % kHeapRing];
        e.frame = heap_frame;
        e.kind = kind;
        e.addr = addr;
        e.size = size;
        if (stack_from != nullptr) {
            e.depth = stack_from->depth;
            std::memcpy(e.stack, stack_from->stack, sizeof(e.stack));
        }
        else {
            e.depth = aidyn::platform::capture_stack(e.stack, 10);
        }
    }

    void heap_report(uint8_t* rdram, const char* where, int voxel, int model, gpr scene) {
        heap_reported = true;
        const uint32_t s = (uint32_t)scene, block = s - 4;
        const uint32_t tag = (uint32_t)MEM_W(-4, scene);
        std::string out;
        char line[256];
        std::snprintf(line, sizeof line, "frame %u: %s, voxel %d model %d: model %08x (block %08x, size tag %08x, %s), "
                      "Borg5 pointer %08x\n", heap_frame, where, voxel, model, s, block, tag,
                      (tag & 1) ? "in use" : "FREE", (uint32_t)MEM_W(0, scene));
        out += line;
        const uint32_t lo = block - 0x40, hi = block + std::max<uint32_t>(tag & ~1u, 0x120) + 0x40;
        const size_t n = std::min(heap_ring_next, kHeapRing);
        for (size_t i = 0; i < n; i++) {
            const HeapEvent& e = heap_ring[(heap_ring_next - n + i) % kHeapRing];
            const uint32_t a = e.kind == 'Q' ? e.addr : e.addr, end = a + std::max<uint32_t>(e.size, 4);
            if (end <= lo || a >= hi) {
                continue;
            }
            std::snprintf(line, sizeof line, "  frame %u %c %08x size %x\n", e.frame, e.kind, a, e.size);
            out += line;
            const std::vector<std::string> names = aidyn::platform::describe_stack(e.stack, e.depth);
            for (size_t k = 1; k < names.size(); k++) {
                out += "      ";
                out += names[k];
                out += "\n";
            }
        }
        std::fputs(out.c_str(), stdout);
        std::fflush(stdout);
        if (FILE* f = std::fopen((aidyn::settings::folder() / "heap-trace.txt").string().c_str(), "w")) {
            std::fputs(out.c_str(), f);
            std::fclose(f);
        }
    }

    void heap_check_map(uint8_t* rdram, gpr map, const char* where) {
        if (heap_reported || !heap_pointer(map)) {
            return;
        }
        const gpr dat = map + 8, voxels = MEM_W(kB9Voxels, dat);
        const int count = MEM_HU(kB9VoxelCount, dat);
        for (int i = 0; heap_pointer(voxels) && i < count && !heap_reported; i++) {
            const gpr v = voxels + kVoxelSize * i;
            if (MEM_HU(kVoxelType, v) != 0) {
                continue;
            }
            const bool b7 = (MEM_HU(kSceneFlags, v) & 2) != 0;
            for (int j = 0; j < 3 && j < MEM_HU(kSceneCount, v); j++) {
                const gpr scene = MEM_W(0, v + kSceneModels + 12 * j + 8);
                if (scene == 0) {
                    continue;
                }
                const bool used = heap_pointer(scene) && (MEM_W(-4, scene) & 1);
                if (!used || (!b7 && !scene_intact(rdram, scene))) {
                    heap_report(rdram, where, i, j, scene);
                    return;
                }
            }
        }
    }
}

bool scene_intact(uint8_t* rdram, gpr scene) {
    if (heap_pointer(scene) && heap_pointer(MEM_W(0, scene))) {
        return true;
    }
    static std::set<uint32_t> told;
    if (told.insert((uint32_t)scene).second) {
        std::fprintf(stdout, "Skipped a damaged prop model at %08x (its Borg5 pointer %08x)\n", (uint32_t)scene,
                     heap_pointer(scene) ? (uint32_t)MEM_W(0, scene) : 0u);
        std::fflush(stdout);
    }
    return false;
}

// func_80093CAC's entry (a0 = display list, a1 = a model's mesh): it loads the mesh's texture, whose header (a1 +0x10)
// starts with its type (0..8), a size looked up by func_80093748, which stops the game (Crash::ManualCrash) on any
// other value. A texture whose memory was overwritten (spitzdano's game, the first forest at draw distance 3, after
// opening the pause menu, 2026-10-23: the damage the "Skipped a damaged prop model" check finds in a model's own
// pointer, here in its texture) is skipped instead: the function returns the display list unchanged, so the mesh draws
// with the texture before it for that frame. Reported once on stdout, and with AIDYN_HEAP_TRACE=1 its block's history
// goes to heap-trace.txt.
extern "C" int aidyn_texture_intact(uint8_t* rdram, recomp_context* ctx) {
    const gpr mesh = ctx->r5;
    const gpr texture = heap_pointer(mesh) ? MEM_W(0x10, mesh) : 0;
    if (heap_pointer(texture) && (uint16_t)MEM_HU(0, texture) <= 8) {
        return 1;
    }
    static std::set<uint32_t> told;
    if (told.insert((uint32_t)texture).second) {
        std::fprintf(stdout, "Skipped a damaged texture at %08x (mesh %08x, type %d)\n", (uint32_t)texture,
                     (uint32_t)mesh, heap_pointer(texture) ? (int)(int16_t)MEM_HU(0, texture) : -1);
        std::fflush(stdout);
    }
    if (heap_trace_on() && !heap_reported && heap_pointer(texture)) {
        heap_report(rdram, "a damaged texture", -1, -1, texture);
    }
    return 0;
}

void heap_trace_frame(uint8_t* rdram) {
    if (!heap_trace_on()) {
        return;
    }
    heap_frame++;
    char where[32];
    for (int i = 0; i < 9; i++) {
        std::snprintf(where, sizeof where, "game cell %d", i);
        heap_check_map(rdram, MEM_W(0xC, (gpr)(kZoneDatMtx + 0x20 * i)), where);
    }
    for (int k = 0; k < kSide * kSide; k++) {
        std::snprintf(where, sizeof where, "far props slot %d", k);
        heap_check_map(rdram, far_maps[k], where);
    }
}

extern "C" void aidyn_heap_alloc_begin(uint8_t* rdram, recomp_context* ctx) {
    if (heap_trace_on()) {
        alloc_pending.depth = aidyn::platform::capture_stack(alloc_pending.stack, 10);
    }
}

extern "C" void aidyn_heap_alloc_end(uint8_t* rdram, recomp_context* ctx) {
    if (heap_trace_on() && heap_pointer(ctx->r2)) {
        heap_record('A', (uint32_t)ctx->r2 - 4, (uint32_t)MEM_W(-4, ctx->r2) & ~1u, &alloc_pending);
    }
}

extern "C" void aidyn_heap_free(uint8_t* rdram, recomp_context* ctx) {
    restored_sample_freed(rdram, ctx, ctx->r4);
    if (heap_trace_on() && heap_pointer(ctx->r4)) {
        heap_record('F', (uint32_t)ctx->r4 - 4, (uint32_t)MEM_W(-4, ctx->r4) & ~1u);
    }
}

extern "C" void aidyn_heap_zero(uint8_t* rdram, recomp_context* ctx, int memset_call) {
    if (heap_trace_on()) {
        heap_record(memset_call ? 'M' : 'Z', (uint32_t)ctx->r4, (uint32_t)(memset_call ? ctx->r6 : ctx->r5));
    }
}

static void heap_queued(uint8_t* rdram, recomp_context* ctx) {
    if (heap_trace_on() && heap_pointer(ctx->r5)) {
        const gpr item = MEM_W(0, ctx->r5);
        if (heap_pointer(item)) {
            heap_record('Q', (uint32_t)item - 4, (uint32_t)MEM_W(-4, item) & ~1u);
        }
    }
}

// ---- Borg holder counts ----
// A Borg item's holders are counted in a signed byte per item (*(0x800E4988) + index). The game loads at most a few
// dozen copies of any prop on a console; with draw_distance above 1 a forest has well over 127 trees sharing one
// texture. At 128 the byte goes negative, the release (func_80099274) ignores a count that isn't above 0, and later
// loads count up through 0, where the item reads as not loaded: a second copy is made, and when the holders of the
// first copy are released they free the second under the trees still using it ("Skipped a damaged texture", then a
// crash in the renderer; the first forest after changing the draw distance, 2026-10-27). So the byte stops at 127 and
// the holders past it are counted here.
namespace {
    std::unordered_map<uint32_t, uint32_t> borg_extra_holders;
}

// getBorgItem is about to add a holder to an item loaded `old` times: true when it's counted here instead.
extern "C" int aidyn_borg_holder_up(uint8_t* rdram, uint32_t index, uint32_t old) {
    if ((old & 0xFF) != 127) {
        return 0;
    }
    if (borg_extra_holders[index]++ == 0) {
        static int told = 0;
        if (told++ < 5) {
            std::fprintf(stdout, "Borg item %u has over 127 holders (counted past the game's byte)\n", index);
            std::fflush(stdout);
        }
    }
    return 1;
}

// func_80099274(index) releases one: true when it's one of the extra holders (the item stays loaded; v0 = 0, as for
// any release that doesn't free it).
extern "C" int aidyn_borg_holder_down(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t index = (uint32_t)ctx->r4;
    const auto it = borg_extra_holders.find(index);
    if (it == borg_extra_holders.end()) {
        return 0;
    }
    const gpr table = MEM_W(0, (gpr)(int32_t)0x800E4988);
    if ((int8_t)MEM_B(0, table + index) != 127) {
        borg_extra_holders.erase(it);  // the count was reset (an unshared load): the extras went with it
        return 0;
    }
    if (--it->second == 0) {
        borg_extra_holders.erase(it);
    }
    ctx->r2 = 0;
    return 1;
}

// ---- Free queue overflow ----
// The game frees models through a queue of 256 entries (QueueA, its count at +0x800), a few frames late so a display
// list still in flight can use them; one more is a crash (AllocFreeQueueItem calls Crash::ManualCrash: the black,
// frozen screen). With draw_distance above 1 far more prop models are loaded (all props considered, far cells), and an
// area change (into the hut by the first forest, 2026-10-21) frees every one at once. So past 240 entries an item
// waits here instead, taken out of its slot as the game would (the slot is set to NULL), and goes into the queue at the
// end of each frame while it has room (kept under 200). Each overflow is reported once on stdout.
namespace {
    struct SpilledFree { uint32_t queue, item; uint16_t type, pri; };
    std::vector<SpilledFree> free_spill;
    constexpr gpr kSpillSlot = (gpr)(int32_t)0x80F81400;  // the void* the game's function takes the item from
    bool spilling = false;
}

extern "C" int aidyn_free_queued(uint8_t* rdram, recomp_context* ctx) {
    heap_queued(rdram, ctx);
    const gpr q = ctx->r4, slot = ctx->r5;
    // (the slot may be outside the game's heap: the far cells' scene pointers live above its 15 MB, kFarTable)
    const uint32_t us = (uint32_t)slot;
    const bool slot_ok = us >= 0x80000400u && us < 0x81000000u && (us & 3) == 0;
    if (spilling || !heap_pointer(q) || !slot_ok || MEM_HU(0x800, q) < 240) {
        return 0;
    }
    free_spill.push_back({ (uint32_t)q, (uint32_t)MEM_W(0, slot), (uint16_t)ctx->r6, (uint16_t)ctx->r7 });
    MEM_W(0, slot) = 0;
    static size_t told = 0;
    if (free_spill.size() > told + 50 || told == 0) {
        told = free_spill.size();
        std::fprintf(stdout, "Free queue full: %zu item(s) waiting to be freed\n", free_spill.size());
        std::fflush(stdout);
    }
    return 1;
}

void free_spill_frame(uint8_t* rdram, recomp_context* ctx) {
    spilling = true;
    while (!free_spill.empty()) {
        const SpilledFree f = free_spill.back();
        if (MEM_HU(0x800, (gpr)(int32_t)f.queue) >= 200) {
            break;
        }
        MEM_W(0, kSpillSlot) = (int32_t)f.item;
        call_game(rdram, ctx, AllocFreeQueueItem, f.queue, (uint32_t)kSpillSlot, f.type, f.pri);
        free_spill.pop_back();
    }
    spilling = false;
}

// ---- Game crash report ----
// Crash::ManualCrash is the game's own "this must not happen" (heap exhausted or corrupt, free queue overflow, a bad
// asset index...). The release build keeps no cause and shows a black, frozen screen. Its entry is hooked: the caller
// (ra), the host call stack (the recompiled functions' names), the free queue's count and the heap's free bytes go to
// stdout and game-crash.txt in the data folder; then a problem report is written (src/main/report.cpp), a message
// says where, and the program ends rather than sit frozen.
#include "report.h"
extern "C" void get_MemFree(uint8_t* rdram, recomp_context* ctx);

extern "C" void aidyn_game_crash(uint8_t* rdram, recomp_context* ctx) {
    std::string out;
    char line[256];
    std::snprintf(line, sizeof line, "GAME CRASH (Crash::ManualCrash), called from %08x; free queue %u/256, %zu waiting; "
                  "heap free %u bytes\n", (uint32_t)ctx->r31, (uint32_t)MEM_HU(0x800, (gpr)kQueueA), free_spill.size(),
                  (uint32_t)MEM_W(0, (gpr)(int32_t)0x800E3D90 + 0x14));
    out += line;
    for (const std::string& frame : aidyn::platform::backtrace_lines(0)) {
        out += "    ";
        out += frame;
        out += "\n";
    }
    std::fputs(out.c_str(), stdout);
    std::fflush(stdout);
    if (FILE* f = std::fopen((aidyn::settings::folder() / "game-crash.txt").string().c_str(), "w")) {
        std::fputs(out.c_str(), f);
        std::fclose(f);
    }
    aidyn::report::game_crashed("The game stopped at its own crash check (game-crash.txt)");
}

// ---- Portrait frames (images) ----
// While the redrawn art shows, three HUD pictures the art pack replaces with spitzdano's (art/hud, tools/branding/hud_frames.py)
// are drawn opaque at N64BorgImageDraw's entry (aidyn_ui_image_seen): CompassRing (the time-of-day dial's, the compass's
// and the combat radar's ring; the game draws it at 80 or 164 of 255, for a faint white ring) and PortraitHighlight
// (the white frame of the field's party portraits, the dialogue's and the combat message box's; 128 in the field,
// where it stays at 128 like the portraits inside it). On
// an enemy's turn the message box's frame is drawn from a copy with its middle texel changed (a texture of its own,
// art/hud/PortraitHighlightEnemy: red and silver). And the combat columns' slot frames (image 0x1C6) are hidden: the
// ornate frames are drawn over the portraits ("Portrait frames (combat columns)"). Told by their size and pixels.
namespace {
    enum HudPic { kHudOther, kHudRing, kHudHighlight, kHudSlot, kHudLetter };
    constexpr gpr kEnemyFrame = (gpr)(int32_t)0x80F81500;        // the copy's Borg8 header (0x20 bytes)
    constexpr gpr kEnemyFramePixels = (gpr)(int32_t)0x80F81600;  // its 52x52 IA8 pixels (2704 bytes)

    uint64_t fnv_bytes(uint8_t* rdram, gpr p, uint32_t n) {
        uint64_t hash = 0xCBF29CE484222325ULL;
        for (uint32_t i = 0; i < n; i++) {
            hash = (hash ^ MEM_BU(i, p)) * 0x100000001B3ULL;
        }
        return hash;
    }

    HudPic hud_pic(uint8_t* rdram, gpr img) {
        if (!heap_pointer(img)) {
            return kHudOther;
        }
        const uint32_t fmt = MEM_HU(0x08, img), w = MEM_HU(0x0A, img), h = MEM_HU(0x0C, img);
        const gpr pixels = MEM_W(0x14, img);
        if (!heap_pointer(pixels)) {
            return kHudOther;
        }
        if (fmt == 5 && w == 60 && h == 60) {
            return fnv_bytes(rdram, pixels, 3600) == 0xA0FBA05E1D07A869ULL ? kHudRing : kHudOther;
        }
        if (fmt == 5 && w == 52 && h == 52) {
            return fnv_bytes(rdram, pixels, 2704) == 0xD6E3E1DCCD1E5C9AULL ? kHudHighlight : kHudOther;
        }
        if (fmt == 5 && w == 16 && h == 16) {
            const uint64_t f = fnv_bytes(rdram, pixels, 256);
            return (f == 0x4D42E5B5A2318E1AULL || f == 0x446893E88652D64AULL || f == 0x3E72B8418DD7E999ULL
                    || f == 0x25FB4DA75816DAD7ULL) ? kHudLetter : kHudOther;  // CompassWest, North, South, East
        }
        if (fmt == 4 && w == 28 && h == 26) {
            return fnv_bytes(rdram, pixels, 728) == 0xDEAF4656D90F8425ULL ? kHudSlot : kHudOther;
        }
        return kHudOther;
    }

    // Whose turn the combat message box shows (CombatTextboxWidget_Tick: gCombatP->current_Ent's portrait): an enemy's?
    bool speaker_enemy(uint8_t* rdram) {
        const gpr combat = MEM_W(0, kCombatP);
        const gpr ent = heap_pointer(combat) ? MEM_W(kCurrentEnt, combat) : 0;
        const gpr sheet = heap_pointer(ent) ? MEM_W(kCharSheet, ent) : 0;
        if (!heap_pointer(sheet)) {
            return false;
        }
        const uint32_t id = MEM_HU(0, sheet);
        const gpr portraits = MEM_W(0, kCombatPortraits);
        for (int i = 0; heap_pointer(portraits) && i < MEM_BU(0, kCombatPortraitCount); i++) {
            const gpr p = portraits + i * 0x18;
            if (MEM_HU(8, p) == id) {
                return MEM_W(0x0C, p) == 0;  // made for the enemies' column
            }
        }
        return MEM_W(kAiP, ent) != 0;
    }
}

void aidyn_hud_frames_image(uint8_t* rdram, recomp_context* ctx) {
    if (!redesigned_card()) {
        return;
    }
    const HudPic pic = hud_pic(rdram, ctx->r5);
    if (pic == kHudOther) {
        return;
    }
    const gpr sp = ctx->r29;
    const uint32_t alpha = MEM_BU(0x37, sp);
    if (pic == kHudSlot) {
        MEM_W(0x34, sp) = 0;
        return;
    }
    if (pic == kHudLetter) {
        // Compass::Draw's letters, drawn after the ring (in front of it), 25 units from its middle (271, 199): on the
        // gold ring's line here (22), and white rather than its blue (160, 160, 220).
        float x, y;
        uint32_t bits = (uint32_t)ctx->r6;
        std::memcpy(&x, &bits, 4);
        bits = (uint32_t)ctx->r7;
        std::memcpy(&y, &bits, 4);
        x = 271.0f + (x + 8.0f - 271.0f) * 0.88f - 8.0f;
        y = 199.0f + (y + 8.0f - 199.0f) * 0.88f - 8.0f;
        std::memcpy(&bits, &x, 4);
        ctx->r6 = (gpr)(int32_t)bits;
        std::memcpy(&bits, &y, 4);
        ctx->r7 = (gpr)(int32_t)bits;
        const uint32_t m = std::max({ MEM_BU(0x2B, sp), MEM_BU(0x2F, sp), MEM_BU(0x33, sp) });
        const int32_t v = (int32_t)std::min(255u, m * 255 / 220);
        MEM_W(0x28, sp) = v;
        MEM_W(0x2C, sp) = v;
        MEM_W(0x30, sp) = v;
        return;
    }
    // The field's party frames (Portraits::DrawParty: at x 84, 122, 160, 198) keep the game's own half strength, as
    // the portraits inside them are drawn (spitzdano: frames and portraits see-through alike); the others, opaque.
    bool party_frame = false;
    if (pic == kHudHighlight) {
        float fx;
        const uint32_t xb = (uint32_t)ctx->r6;
        std::memcpy(&fx, &xb, 4);
        for (float px : { 84.0f, 122.0f, 160.0f, 198.0f }) {
            party_frame = party_frame || std::abs(fx - px) < 0.5f;
        }
    }
    MEM_W(0x34, sp) = (int32_t)std::min(255u, pic == kHudRing ? alpha * 16 / 5 : party_frame ? alpha : alpha * 2);
    if (pic == kHudHighlight && heap_pointer(MEM_W(0, kCombatP))) {
        float x, y;
        uint32_t bits = (uint32_t)ctx->r6;
        std::memcpy(&x, &bits, 4);
        bits = (uint32_t)ctx->r7;
        std::memcpy(&y, &bits, 4);
        if (std::abs(x - 20.0f) < 1.5f && std::abs(y - 174.0f) < 1.5f && speaker_enemy(rdram)) {  // the message box's
            const gpr img = ctx->r5, pixels = MEM_W(0x14, img);
            for (int k = 0; k < 0x20; k += 4) {
                MEM_W(k, kEnemyFrame) = MEM_W(k, img);
            }
            for (int i = 0; i < 52 * 52; i++) {
                MEM_B(i, kEnemyFramePixels) = MEM_B(i, pixels);
            }
            MEM_B(26 * 52 + 26, kEnemyFramePixels) ^= 1;  // tools/hdui/romimages.py COPIES
            MEM_W(0x14, kEnemyFrame) = (int32_t)kEnemyFramePixels;
            ctx->r5 = kEnemyFrame;
        }
    }
}

// ---- Camera and walls ----
// The walking camera (Camera::Mode0) never kept itself out of walls: it only used the map's collision to decide
// when to swing back behind the player, and the right-stick camera (above) leaves it where it was turned, so it went
// through walls and showed the outside of the level. Before Mode0's last Orient the line from the player's head (the
// camera's aim, +0xC) to the camera (pos, +0x0) is checked with the game's own CheckCollision against the map
// (borg_9, +0x88), as the game's other camera mode does for its own move (Camera::Mode1): a wall on it puts the
// camera on the player's side of the wall, a little off it. Not while the camera is held (cutscenes: holdCamera).
extern "C" void CheckCollision(uint8_t* rdram, recomp_context* ctx);  // (borg9, from, to, radius, outPos, outNormal, s16)

namespace {
    constexpr gpr kCamHit = (gpr)(int32_t)0x80F81480, kCamNormal = (gpr)(int32_t)0x80F8148C;  // vec3f each
}

extern "C" void aidyn_camera_walls(uint8_t* rdram, recomp_context* ctx) {
    const gpr cam = ctx->r19;
    if (!heap_pointer(cam) || MEM_HU(0x6A, cam) != 0) {
        return;
    }
    const gpr borg = MEM_W(0x88, cam);
    if (!heap_pointer(borg)) {
        return;
    }
    const uint32_t hit = call_game_stack(rdram, ctx, CheckCollision, (uint32_t)borg, (uint32_t)(cam + 0xC), (uint32_t)cam,
                                         f32_bits(0.25f), { (uint32_t)kCamHit, (uint32_t)kCamNormal, 1 }) & 0xFF;
    if (hit == 0) {
        return;
    }
    // A wall right behind the head (backed against it) would put the camera on the aim itself, which the game takes
    // for a crash (handleZoneEngineFrame: "Focus, and Camera at same Spot!", under 0.05 apart; spitzdano's game froze so,
    // 2026-10-21): then the camera stays where the game put it.
    float to[3], d2 = 0.0f;
    for (int k = 0; k < 3; k++) {
        to[k] = read_f32(rdram, kCamHit + 4 * k) + 0.3f * read_f32(rdram, kCamNormal + 4 * k);
        const float d = to[k] - read_f32(rdram, cam + 0xC + 4 * k);
        d2 += d * d;
    }
    if (!(d2 >= 0.5f * 0.5f)) {
        return;
    }
    for (int k = 0; k < 3; k++) {
        write_f32(rdram, cam + 4 * k, to[k]);
    }
}

// And whatever else might bring them together: handleZoneEngineFrame before that check (0x8000EA44; the camera's pos
// and aim at s1 + 0x134 and + 0x140) lifts a camera within 0.06 of its aim half a unit above it, rather than
// the game freezing.
extern "C" void aidyn_camera_apart(uint8_t* rdram, gpr pos) {
    const gpr aim = pos + 0xC;
    float d2 = 0.0f;
    for (int k = 0; k < 3; k++) {
        const float d = read_f32(rdram, pos + 4 * k) - read_f32(rdram, aim + 4 * k);
        d2 += d * d;
    }
    if (d2 < 0.06f * 0.06f) {
        fprintf(stderr, "Camera: on its aim, lifted (the game would crash)\n");
        write_f32(rdram, pos + 4, read_f32(rdram, aim + 4) + 0.5f);
    }
}

// ---- Quick and auto saves ----
// Saves of their own beside the Controller Pak (spitzdano, 2026-10-21), which keeps the game's manual saves: files next to
// the pak, named after it (aidyn-patch.mpk: aidyn-patch-quicksave.sav, -autosave-1.sav, -autosave-2.sav), each the
// 7 KB SaveDatStruct the game writes to the pak, byte for byte.
//  - View in the field quicksaves (src/main/input.cpp). Autosaves are made on arriving in another area (through a door
//    or any other map load; not walking from cell to cell outdoors) and after every 10 minutes of walking about, into
//    the older of the two files. Only while really walking about: gameStateA 1 (Trek), gameStateB 3, no screen fade,
//    no state change waiting (some_toggle), the walk set up (gTrekUninitted clear), a player. A door is only a screen
//    fade out and in with the map changed between (gameStateB stays 3): a map other than before after a fade (or
//    gameStateB 15, the game's teleport, or the walk set up again) is an arrival; mapLoadBool is no use (it stays
//    set), and walking from cell to cell outdoors changes the map's cell with no fade. Made as the pause menu's Save makes one:
//    QuestData::TakeSnapshot (the picture; save_pictures.cpp has RT64 copy it in full resolution, which it does
//    before the next frame, so the save is made a few frames later), then QuestData::InitSaveFile into a buffer above
//    the game's 15 MB (its checksum's table made for it: the pak screens keep one only while open), then the picture
//    freed again. Trek_GameState's entry (aidyn_trek_tick) runs all this.
//  - The pause menu's Menu page gets Quick Load after Load Game when there is a quicksave or an autosave: the newest
//    one, loaded as the Load screen loads (QuestData::LoadFromMemPak, then the pause menu's after-load callback,
//    func_80048E18: the menu closes and the game reloads).
//  - The title screen's menu gets Continue first when there is any: the newest quicksave, autosave or manual save
//    (every pak save is also written as -lastsave.sav), then the title's after-load callback (func_80043958: fade
//    out, start). A damaged file (its checksum, QuestData::VerifyChecksum) is passed over for the next newest.
// A short notice at the top of the screen says what happened. AIDYN_SAVE_LOG=1 prints each step.
extern "C" void QuestData__TakeSnapshot(uint8_t* rdram, recomp_context* ctx);
extern "C" void Crash__ManualCrash(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_8001A448(uint8_t* rdram, recomp_context* ctx);  // QuestData::FreeSnapshot
extern "C" void func_8001A08C(uint8_t* rdram, recomp_context* ctx);  // QuestData::InitSaveFile(SaveDatStruct*)
void aidyn_save_pictures_off_pak(bool on);  // src/game/save_pictures.cpp: a save that isn't for the Controller Pak
extern "C" void func_80019ED4(uint8_t* rdram, recomp_context* ctx);  // QuestData::VerifyChecksum(SaveDatStruct*)
extern "C" void func_8001A20C(uint8_t* rdram, recomp_context* ctx);  // QuestData::LoadFromMemPak(SaveDatStruct*)
extern "C" void func_80048E18(uint8_t* rdram, recomp_context* ctx);  // the pause menu's Load callback
extern "C" void func_80043958(uint8_t* rdram, recomp_context* ctx);  // the title's Load callback
extern "C" void func_8001FF04(uint8_t* rdram, recomp_context* ctx);  // the walk: fade out, then gameStateB a0
extern "C" void WidgetText__WidgetText(uint8_t* rdram, recomp_context* ctx);
extern "C" void func_800CBC94(uint8_t* rdram, recomp_context* ctx);  // CRC table: made (if not there)
extern "C" void func_800CBD28(uint8_t* rdram, recomp_context* ctx);  // CRC table: freed

void fair_note_save(uint64_t hash);  // "Fair play", below: the playthrough's rules go with each save the port writes
void fair_loaded_save(uint64_t hash);  // ... and come back with it

namespace {
    constexpr gpr kSaveBuffer = (gpr)(int32_t)0x80F84000;  // a SaveDatStruct (free up to 0x80F8FFFF)
    constexpr uint32_t kSaveSize = 0x1C00;
    constexpr gpr kPlayer = (gpr)(int32_t)0x800D8130;         // gGlobals.playerCharStruct.playerDat
    constexpr gpr kGameStateB = (gpr)(int32_t)0x800D8164;     // gGlobals.playerCharStruct.gameStateB
    constexpr gpr kTrekState = (gpr)(int32_t)0x800D80C8;      // gGlobals.gameStateA
    constexpr gpr kScreenFade = (gpr)(int32_t)0x800DA0D4;     // gGlobals.screenFadeMode
    constexpr gpr kSomeToggle = (gpr)(int32_t)0x800DB064;     // some_toggle: a state change after the fade, or -1
    constexpr gpr kTrekUninit = (gpr)(int32_t)0x800DB066;     // gTrekUninitted
    constexpr gpr kSnapshot = (gpr)(int32_t)0x800DA4F0;       // gQuestdataSnapshot
    constexpr gpr kCrcTable = (gpr)(int32_t)0x800E6560;       // the checksum's table: only while a pak screen is open
    constexpr gpr kMapShort1 = (gpr)(int32_t)0x800D83FC;      // gGlobals.gameVars.mapShort1, mapShort2 (the cell)
    constexpr gpr kMapDatA = (gpr)(int32_t)0x800D8414;        // gGlobals.gameVars.mapDatA (the map)
    constexpr uint16_t kQuickLoadId = 0x7A13;
    constexpr gpr kQuickLoadText = kRcText + 0x40, kContinueText = kRcText + 0x60;
    // Seconds of walking about between autosaves: 10 minutes (AIDYN_AUTOSAVE_EVERY=<seconds> for tests).
    const double kAutosaveEvery = [] {
        const char* v = std::getenv("AIDYN_AUTOSAVE_EVERY");
        return v != nullptr && std::atof(v) > 0.0 ? std::atof(v) : 600.0;
    }();
    constexpr double kArrived = 1.5;          // seconds walking about in a new area before its autosave

    enum SaveFile { kQuickFile, kAutoFile1, kAutoFile2, kLastFile };
    std::filesystem::path save_dir;
    std::string save_stem;

    struct SaveState {
        int pending = 0;            // 1 a quicksave, 2 an autosave, being made
        int wait = 0;               // frames until it is made
        bool own_snapshot = false;  // the picture is ours to free
        double explored = 0.0;      // seconds of walking about since the last save or load
        double safe_for = 0.0;      // seconds of walking about in a row
        std::chrono::steady_clock::time_point last{};
        int area[3] = { 0, 0, 0 };
        bool area_known = false;
        bool map_loaded = false;    // a map load since the last walking about
        bool save_loaded = false;   // a save loaded: its area is no new arrival
        bool continuing = false;    // Continue chosen on the title screen
    } saves;

    std::string notice_text;
    std::chrono::steady_clock::time_point notice_until{};

    // The game's checksum needs its table, which the Load and Save screens make and free: made for f if it isn't
    // there, then freed again.
    template <typename F>
    auto with_crc_table(uint8_t* rdram, recomp_context* ctx, F f) {
        const bool made = MEM_W(0, kCrcTable) == 0;
        if (made) {
            call_game(rdram, ctx, func_800CBC94);
        }
        auto result = f();
        if (made) {
            call_game(rdram, ctx, func_800CBD28);
        }
        return result;
    }

    bool save_log() {
        static const bool on = std::getenv("AIDYN_SAVE_LOG") != nullptr;
        return on;
    }

    std::filesystem::path save_path(SaveFile f) {
        static const char* const kSuffix[] = { "-quicksave.sav", "-autosave-1.sav", "-autosave-2.sav", "-lastsave.sav" };
        return save_dir / (save_stem + kSuffix[f]);
    }

    bool save_exists(SaveFile f) {
        std::error_code ec;
        return !save_dir.empty() && std::filesystem::file_size(save_path(f), ec) == kSaveSize && !ec;
    }

    void notice(const char* text, int ms = 2500) {
        notice_text = text;
        notice_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    }

    bool walking_about(uint8_t* rdram) {
        return MEM_BU(0, kTrekState) == 1 && MEM_BU(0, kGameStateB) == 3 && MEM_HU(0, kScreenFade) == 0 &&
               MEM_H(0, kSomeToggle) == -1 && MEM_BU(0, kTrekUninit) == 0 &&
               heap_pointer(MEM_W(0, kPlayer));
    }

    // Which version wrote each save (#31). Saves aren't interchangeable: Aidyn Plus has other spell and item databases,
    // and its loader reads a Patch save's bit-packed party wrong from the first difference on, then equips nonsense and
    // crashes. A save says nothing of its version, so the port keeps a record beside the saves (save-versions.txt:
    // FNV-1a of the 7 KB save, the version that wrote it): every save it writes (quick, auto, and each pak save's
    // -lastsave copy) is added, and on the first start the existing quick and auto saves by their file names (in the
    // order original, Aidyn Patch, Aidyn Plus, Action Edition, so a copy made under another version's name keeps its first). A save
    // recorded as another version's is refused (Continue and Quick Load pass over it, the Load screen says so); one
    // not in the record (older pak saves) loads as before.
    const char* version_name(aidyn::settings::RomPatch v) {
        return v == aidyn::settings::RomPatch::AidynPatch ? "aidyn-patch"
             : v == aidyn::settings::RomPatch::AidynPlus ? "aidyn-plus"
             : v == aidyn::settings::RomPatch::ActionEdition ? "action-edition" : "original";
    }
    const char* version_title(const std::string& v) {
        return v == "aidyn-patch" ? "Aidyn Patch" : v == "aidyn-plus" ? "Aidyn Plus"
               : v == "action-edition" ? "Action Edition" : "the original game";
    }
    std::map<uint64_t, std::string> save_versions;
    bool save_versions_loaded = false;
    std::string passed_over;  // read_newest: the version of a save it passed over, for the notice

    uint64_t save_hash(const uint8_t* bytes) {
        uint64_t h = 0xCBF29CE484222325ULL;
        for (uint32_t i = 0; i < kSaveSize; i++) h = (h ^ bytes[i]) * 0x100000001B3ULL;
        return h;
    }
    uint64_t save_hash(uint8_t* rdram, gpr buffer) {
        std::vector<uint8_t> b(kSaveSize);
        for (uint32_t i = 0; i < kSaveSize; i++) b[i] = MEM_BU(i, buffer);
        return save_hash(b.data());
    }
    std::filesystem::path save_versions_path() { return save_dir / "save-versions.txt"; }

    void note_version(uint64_t h, const std::string& v, bool keep_first) {
        if (keep_first && save_versions.count(h)) return;
        save_versions[h] = v;
        std::ofstream out(save_versions_path(), std::ios::app);
        char line[64];
        snprintf(line, sizeof(line), "%016llx %s\n", (unsigned long long)h, v.c_str());
        out << line;
    }

    void load_save_versions() {
        if (save_versions_loaded || save_dir.empty()) return;
        save_versions_loaded = true;
        std::ifstream in(save_versions_path());
        std::string hex, v;
        while (in >> hex >> v) save_versions[std::stoull(hex, nullptr, 16)] = v;  // later lines win
        if (std::filesystem::exists(save_versions_path())) return;
        for (const char* stem : { "aidyn", "aidyn-patch", "aidyn-plus", "action-edition" }) {  // the first start: what's there now
            const std::string v = std::string(stem) == "aidyn" ? "original" : stem;
            for (const char* suffix : { "-quicksave.sav", "-autosave-1.sav", "-autosave-2.sav", "-lastsave.sav" }) {
                std::ifstream f(save_dir / (std::string(stem) + suffix), std::ios::binary);
                std::vector<char> b(kSaveSize);
                if (f.read(b.data(), (std::streamsize)b.size())) {
                    note_version(save_hash(reinterpret_cast<uint8_t*>(b.data())), v, true);
                }
            }
        }
        if (!std::filesystem::exists(save_versions_path())) std::ofstream(save_versions_path()).flush();
    }

    // "" if the save may load here, else the version it belongs to.
    uint64_t read_hash = 0;  // the save about to load, hashed as read (the game changes its buffer before loading it)
    std::string foreign_save(uint8_t* rdram, gpr buffer) {
        load_save_versions();
        read_hash = save_hash(rdram, buffer);
        const auto it = save_versions.find(read_hash);
        const std::string here = version_name(aidyn::settings::game().rom_patch);
        return it == save_versions.end() || it->second == here ? std::string() : it->second;
    }

    bool write_save(uint8_t* rdram, gpr buffer, SaveFile f) {
        std::vector<char> bytes(kSaveSize);
        for (uint32_t i = 0; i < kSaveSize; i++) {
            bytes[i] = (char)MEM_BU(i, buffer);
        }
        const std::filesystem::path path = save_path(f), tmp = path.string() + ".tmp";
        std::error_code ec;
        std::filesystem::create_directories(save_dir, ec);
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), (std::streamsize)bytes.size());
            if (!out) {
                fprintf(stderr, "Saves: could not write %s\n", tmp.string().c_str());
                return false;
            }
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            fprintf(stderr, "Saves: could not replace %s: %s\n", path.string().c_str(), ec.message().c_str());
            return false;
        }
        if (save_log()) {
            fprintf(stdout, "Saves: wrote %s\n", path.string().c_str());
            fflush(stdout);
        }
        load_save_versions();
        note_version(save_hash(reinterpret_cast<const uint8_t*>(bytes.data())), version_name(aidyn::settings::game().rom_patch), false);
        fair_note_save(save_hash(reinterpret_cast<const uint8_t*>(bytes.data())));
        return true;
    }

    // The newest of these files whose save is whole, read into kSaveBuffer: false if none.
    bool read_newest(uint8_t* rdram, recomp_context* ctx, std::initializer_list<SaveFile> files) {
        passed_over.clear();
        std::vector<std::pair<std::filesystem::file_time_type, SaveFile>> found;
        for (SaveFile f : files) {
            std::error_code ec;
            if (save_exists(f)) {
                found.emplace_back(std::filesystem::last_write_time(save_path(f), ec), f);
            }
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [time, f] : found) {
            std::ifstream in(save_path(f), std::ios::binary);
            std::vector<char> bytes(kSaveSize);
            if (!in.read(bytes.data(), (std::streamsize)bytes.size())) {
                continue;
            }
            for (uint32_t i = 0; i < kSaveSize; i++) {
                MEM_B(i, kSaveBuffer) = (int8_t)bytes[i];
            }
            if (const std::string other = foreign_save(rdram, kSaveBuffer); !other.empty()) {
                fprintf(stderr, "Saves: %s is from %s, passed over\n", save_path(f).string().c_str(), version_title(other));
                passed_over = other;
                continue;
            }
            if (with_crc_table(rdram, ctx, [&] { return call_game(rdram, ctx, func_80019ED4, (uint32_t)kSaveBuffer) & 0xFF; }) != 0) {
                if (save_log()) {
                    fprintf(stdout, "Saves: loading %s\n", save_path(f).string().c_str());
                }
                return true;
            }
            fprintf(stderr, "Saves: %s is damaged (its checksum), passed over\n", save_path(f).string().c_str());
        }
        return false;
    }

    void drop_snapshot(uint8_t* rdram, recomp_context* ctx) {
        if (saves.own_snapshot) {
            call_game(rdram, ctx, func_8001A448);
            saves.own_snapshot = false;
        }
    }

    void start_save(uint8_t* rdram, recomp_context* ctx, int kind) {
        if (saves.pending != 0) {
            saves.pending = std::min(saves.pending, kind);  // a quicksave asked while an autosave is on its way
            return;
        }
        saves.pending = kind;
        saves.wait = 3;
        saves.own_snapshot = MEM_W(0, kSnapshot) == 0;
        if (saves.own_snapshot) {
            call_game(rdram, ctx, QuestData__TakeSnapshot);
        }
    }

    void finish_save(uint8_t* rdram, recomp_context* ctx) {
        const bool quick = saves.pending == 1;
        saves.pending = 0;
        aidyn_save_pictures_off_pak(true);  // not shown on the Load and Save screens: no full-size picture kept
        with_crc_table(rdram, ctx, [&] { return call_game(rdram, ctx, func_8001A08C, (uint32_t)kSaveBuffer); });
        aidyn_save_pictures_off_pak(false);
        drop_snapshot(rdram, ctx);
        SaveFile f = kQuickFile;
        if (!quick) {  // the older autosave (a missing one first)
            std::error_code e1, e2;
            const bool has1 = save_exists(kAutoFile1), has2 = save_exists(kAutoFile2);
            f = !has1 ? kAutoFile1 : !has2 ? kAutoFile2
                : std::filesystem::last_write_time(save_path(kAutoFile1), e1) <= std::filesystem::last_write_time(save_path(kAutoFile2), e2)
                    ? kAutoFile1 : kAutoFile2;
        }
        if (!write_save(rdram, kSaveBuffer, f)) {
            notice("Could not save");
            return;
        }
        saves.explored = 0.0;
        notice(quick ? "Quicksaved" : "Autosaved");
        if (quick) {
            rc_sound(rdram, ctx, 0x740, 60);
        }
    }
}

namespace {
    bool fight_load_pending = false;  // a save chosen in a fight's pause, in kSaveBuffer: loaded once the fight is gone
    bool quit_pending = false;        // Quit to Title chosen in the field's pause: the walk fades out to the title
}

void aidyn_quick_saves_set_pak(const std::filesystem::path& pak) {
    save_dir = pak.parent_path();
    save_stem = pak.stem().string();
}

// Trek_GameState's entry: the game walking about (or about to stop: a door, a fight, a menu), once a frame.
extern "C" void aidyn_trek_tick(uint8_t* rdram, recomp_context* ctx) {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::clamp(std::chrono::duration<double>(now - saves.last).count(), 0.0, 0.25);
    saves.last = now;
    saves.continuing = false;
    if (fight_load_pending) {  // the fight is cleared away: its save now, as the field's Load makes it (Quit to Title)
        fight_load_pending = false;
        call_game(rdram, ctx, func_8001A20C, (uint32_t)kSaveBuffer);
    }
    const bool asked = aidyn::input::quicksave_asked();
    if (save_log()) {  // each change of the walk's states, for finding what a door or a fight looks like
        static uint32_t last = 0xFFFFFFFF;
        const uint32_t now_states = (uint32_t)MEM_BU(0, kGameStateB) | (uint32_t)MEM_BU(0, kTrekUninit) << 8 |
                                    (uint32_t)(MEM_HU(0, kScreenFade) & 0xFF) << 16 | (uint32_t)(MEM_BU(0, kSomeToggle) << 24);
        if (now_states != last) {
            fprintf(stdout, "Saves: walk state B %u, set up %u, fade %u, toggle %d; map %u cell %d,%d\n",
                    MEM_BU(0, kGameStateB), MEM_BU(0, kTrekUninit), MEM_HU(0, kScreenFade), MEM_H(0, kSomeToggle),
                    MEM_HU(0, kMapDatA), MEM_H(0, kMapShort1), MEM_H(0, kMapShort1 + 2));
            fflush(stdout);
            last = now_states;
        }
    }
    if (!walking_about(rdram)) {
        if (MEM_HU(0, kScreenFade) != 0 || MEM_BU(0, kGameStateB) == 15 || MEM_BU(0, kTrekUninit) != 0) {
            saves.map_loaded = true;
        }
        saves.safe_for = 0.0;
        if (saves.pending != 0) {
            if (saves.pending == 1) {
                notice("Can't save now");
            }
            saves.pending = 0;
            drop_snapshot(rdram, ctx);
        }
        if (asked) {
            notice("Can't save now");
            if (save_log()) {
                fprintf(stdout, "Saves: not walking about: state %u/%u fade %u toggle %d trek init %u player %08x\n",
                        MEM_BU(0, kTrekState), MEM_BU(0, kGameStateB), MEM_HU(0, kScreenFade), MEM_H(0, kSomeToggle),
                        MEM_BU(0, kTrekUninit), (uint32_t)MEM_W(0, kPlayer));
                fflush(stdout);
            }
        }
        return;
    }
    if (quit_pending) {  // Quit to Title: the walk fades out and ends (gameStateB 0), the game's way to the title
        quit_pending = false;
        saves.pending = 0;
        drop_snapshot(rdram, ctx);
        call_game(rdram, ctx, func_8001FF04, 0);
        return;
    }
    saves.safe_for += dt;
    saves.explored += dt;
    // AIDYN_CRASH_TEST=1: after 10 seconds of walking about, the game's own crash check, to try the problem report's
    // way end to end (Steam launch options: AIDYN_CRASH_TEST=1 %command%); AIDYN_CRASH_TEST=native: a real crash
    // there instead (a write to address 0: the launcher's report on Linux, the crash handler's on Windows).
    static const char* crash_test_kind = std::getenv("AIDYN_CRASH_TEST");
    static const bool crash_test = crash_test_kind != nullptr;
    static double crash_test_time = 0.0;
    if (crash_test && std::strcmp(crash_test_kind, "native") == 0 && crash_test_time + dt >= 10.0) {
        *(volatile int*)nullptr = 1;
    }
    if (crash_test && (crash_test_time += dt) >= 10.0) {
        call_game(rdram, ctx, Crash__ManualCrash);
    }
    const int area[3] = { MEM_HU(0, kMapDatA), MEM_H(0, kMapShort1), MEM_H(0, kMapShort1 + 2) };
    const bool same = std::equal(area, area + 3, saves.area);
    if (!saves.area_known || saves.save_loaded) {
        std::copy(area, area + 3, saves.area);
        saves.area_known = true;
        saves.save_loaded = saves.map_loaded = false;
    }
    else if (!saves.map_loaded) {
        std::copy(area, area + 3, saves.area);  // outdoors, from cell to cell
    }
    else if (saves.safe_for >= kArrived) {
        saves.map_loaded = false;
        if (!same) {
            if (save_log()) {
                fprintf(stdout, "Saves: arrived in map %d cell %d,%d (from %d %d,%d)\n", area[0], area[1], area[2],
                        saves.area[0], saves.area[1], saves.area[2]);
                fflush(stdout);
            }
            std::copy(area, area + 3, saves.area);
            start_save(rdram, ctx, 2);
        }
    }
    if (saves.explored >= kAutosaveEvery) {
        start_save(rdram, ctx, 2);
    }
    if (asked) {
        start_save(rdram, ctx, 1);
    }
    if (saves.pending != 0 && --saves.wait <= 0) {
        finish_save(rdram, ctx);
    }
}

// QuestData::LoadFromMemPak's entry: a save is being loaded (the Load screens', Quick Load, Continue).
void ach_loaded_save();  // "Achievements", below
gpr ach_toast(uint8_t* rdram, recomp_context* ctx, gpr g);  // "Achievements page and toast", below

extern "C" int aidyn_save_loading(uint8_t* rdram, recomp_context* ctx) {
    if (MEM_BU(0, kTrekState) == 3 && (ctx->r4 == kSaveBuffer || heap_pointer(ctx->r4))) {  // a fight's pause ("Quit to Title")
        if (ctx->r4 != kSaveBuffer) {
            for (uint32_t i = 0; i < kSaveSize; i += 4) {
                MEM_W(i, kSaveBuffer) = MEM_W(i, ctx->r4);
            }
        }
        fight_load_pending = true;
        if (save_log()) {
            fprintf(stdout, "Saves: a load chosen in a fight, made once the fight is cleared away\n");
            fflush(stdout);
        }
        return 1;
    }
    quit_pending = false;
    ach_loaded_save();
    fair_loaded_save(read_hash);  // the hash of the bytes read (a file's, the pak's): its record in save-rules.txt
    read_hash = 0;
    saves.save_loaded = true;
    saves.explored = 0.0;
    saves.pending = 0;
    drop_snapshot(rdram, ctx);
    return 0;
}

// WidgetContPakDataLoad::LoadSaveFile (func_8007F220) before it loads the chosen save (s0 the widget, s1 its index:
// the save at +0x7C + 0x20 each): one recorded as another version's isn't loaded (#31). The screen stays, as after a
// pak error, with a notice and the game's error sound.
extern "C" int aidyn_pak_load_check(uint8_t* rdram, recomp_context* ctx) {
    const gpr save = MEM_W(0x7C + 0x20 * (ctx->r17 & 0xFF), ctx->r16);
    if (!heap_pointer(save)) return 0;
    const std::string other = foreign_save(rdram, save);
    if (other.empty()) return 0;
    notice((std::string("This save is from ") + version_title(other) + ": switch on the title screen").c_str());
    rc_sound(rdram, ctx, 0x741, 60);
    return 1;
}

// WidgetContPakDataSave::WriteSaveFile (func_8007EE34) once the pak took the save (s0 the widget, its buffer at +0x2C4):
// the newest manual save, for Continue.
extern "C" void aidyn_pak_saved(uint8_t* rdram, recomp_context* ctx) {
    const gpr buffer = MEM_W(0x2C4, ctx->r16);
    if (heap_pointer(buffer)) {
        write_save(rdram, buffer, kLastFile);
    }
    saves.explored = 0.0;
}

namespace {
    gpr put_menu_text(uint8_t* rdram, gpr at, const char* text) {
        for (size_t i = 0; i <= std::strlen(text); i++) {
            MEM_B(i, at) = (int8_t)text[i];
        }
        return at;
    }
}

// The pause menu's list (MakeScrollList) just after Load Game is appended (s1 the substruct, its list at +0): Quick Load.
extern "C" void aidyn_quick_load_entry(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = MEM_W(0, ctx->r17);
    if (!heap_pointer(list) || (!save_exists(kQuickFile) && !save_exists(kAutoFile1) && !save_exists(kAutoFile2))) {
        return;
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetClipText__WidgetClipText, (uint32_t)memory,
                                              (uint32_t)put_menu_text(rdram, kQuickLoadText, "Quick Load"), 400);
    MEM_H(0x5E, entry) = (int16_t)kQuickLoadId;
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
}

// aidyn_rc_select: Quick Load chosen (its id): true if it was.
bool quick_load_chosen(uint8_t* rdram, recomp_context* ctx, uint32_t id) {
    if (id != kQuickLoadId) {
        return false;
    }
    if (!read_newest(rdram, ctx, { kQuickFile, kAutoFile1, kAutoFile2 })) {
        notice(passed_over.empty() ? "No save to load"
                                   : (std::string("That save is from ") + version_title(passed_over) + ": switch on the title screen").c_str());
        rc_sound(rdram, ctx, 0x741, 60);
        return true;
    }
    call_game(rdram, ctx, func_8001A20C, (uint32_t)kSaveBuffer);
    call_game(rdram, ctx, func_80048E18);
    return true;
}

// ---- Quit to Title ----
// The pause menu's Menu page ends with Quit to Title (spitzdano, 2026-10-25), in the field and in a fight; the title
// screen's menu ends with Quit Game, which closes the game as the window's close button does.
//  - Quit to Title asks first: chosen once, it reads "Really quit?" and a notice says unsaved progress is lost;
//    chosen again it quits; moving off it puts it back. In the field the pause menu closes as after Help (its
//    WidgetOptionsMenu returned, id 0: back to walking about), then the walk fades out into gameStateB 0, the game's
//    own way out of the walk to the title (Trek_GameState returns GameStateA 0: the game's data freed, the title
//    splash). In a fight it closes as Flee does, with CombatState 0 next instead of 20: CombatStateFunc_0 clears the
//    fight away and returns GameStateA 0.
//  - A fight's Menu page also gets Load Game and Quick Load (the game offers neither there). The load screen and
//    Quick Load end in QuestData::LoadFromMemPak and the pause menu's after-load callback, as in the field; in a fight
//    the load itself waits (the fight's characters still point into the party it would replace): the save is kept in
//    kSaveBuffer, the pause menu closes for a load (gameStateB 4, back to the walk), which the game turns into
//    CombatState 16 (the fight cleared away, as after one is won), and the first frame of the walk loads it.
namespace {
    constexpr uint16_t kQuitId = 0x7A14;
    constexpr uint16_t kLoadGameId = 0xA0;                     // the game's Load Game (OptionN_LoadGame)
    constexpr gpr kQuitText = kRcText + 0x80, kQuitGameText = kRcText + 0xA0, kControlsText = kRcText + 0xC0,
                  kAchText = kRcText + 0xE0;
    constexpr gpr kCombatNext = (gpr)(int32_t)0x800D9455;      // gGlobals.combatBytes[1]: the fight's state after the pause
    constexpr gpr kCommonStrings = (gpr)(int32_t)0x800DA0D8;   // gGlobals.CommonStrings
    constexpr const char* kQuitLabel = "Quit to Title";
    constexpr const char* kQuitAsk = "Really Quit?";           // no longer than kQuitLabel: written over its copy

    struct QuitEntry {
        gpr list = 0;     // the Menu page's list, while the pause menu is up
        int index = -1;   // Quit to Title's place in it
        bool armed = false;
    } quit;

    gpr quit_widget(uint8_t* rdram) {
        const gpr sub = heap_pointer(quit.list) ? MEM_W(0x40, quit.list) : 0;
        const gpr items = heap_pointer(sub) ? MEM_W(0x18, sub) : 0;
        return heap_pointer(items) && quit.index >= 0 ? MEM_W(4 * quit.index, items) : 0;
    }

    void quit_label(uint8_t* rdram, const char* text) {
        const gpr w = quit_widget(rdram);
        const gpr sub = heap_pointer(w) ? MEM_W(0x40, w) : 0;
        const gpr str = heap_pointer(sub) ? MEM_W(0, sub) : 0;  // WidgetClipText's own copy of its text
        if (heap_pointer(str)) {
            put_menu_text(rdram, str, text);
        }
    }
}

void quit_entry(uint8_t* rdram, recomp_context* ctx, gpr list) {
    quit = QuitEntry{};
    const gpr sub = heap_pointer(list) ? MEM_W(0x40, list) : 0;
    if (!heap_pointer(sub)) {
        return;
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetClipText__WidgetClipText, (uint32_t)memory,
                                              (uint32_t)put_menu_text(rdram, kQuitText, kQuitLabel), 400);
    MEM_H(0x5E, entry) = (int16_t)kQuitId;
    quit.list = list;
    quit.index = MEM_HU(0x20, sub);  // numChoices: its place once appended
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
}

// The Menu page's Achievements (spitzdano: on the main pause page, beside Help), when achievements are on.
void ach_menu_entry(uint8_t* rdram, recomp_context* ctx, gpr list) {
    if (!ach_shown() || !heap_pointer(list)) {
        return;
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetClipText__WidgetClipText, (uint32_t)memory,
                                              (uint32_t)put_menu_text(rdram, kAchText, "Achievements"), 400);
    MEM_H(0x5E, entry) = (int16_t)kAchMenuId;
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
}

int quit_chosen(uint8_t* rdram, recomp_context* ctx, uint32_t id) {
    if (id != kQuitId) {
        return 0;
    }
    if (!quit.armed) {
        quit.armed = true;
        quit_label(rdram, kQuitAsk);
        notice("Unsaved progress will be lost");
        rc_sound(rdram, ctx, 0x741, 60);
        return 1;
    }
    quit.armed = false;
    rc_sound(rdram, ctx, 0x740, 60);
    if (MEM_BU(0, kTrekState) == 3) {
        MEM_B(0, kCombatNext) = 0;  // CombatState 0 once the pause menu is gone
    }
    else {
        MEM_H(0x5E, ctx->r18) = 0;  // back to walking about (Help leaves 3 here)
        quit_pending = true;
    }
    return 2;
}

// Each frame (Graphics::EndList): Quit to Title asks again once the cursor leaves it; the list is forgotten with the
// pause menu.
void quit_tick(uint8_t* rdram) {
    if (quit.list == 0) {
        return;
    }
    if (!heap_pointer(MEM_W(0, kBigAssMenu))) {
        quit = QuitEntry{};
        return;
    }
    const gpr sub = MEM_W(0x40, quit.list);
    if (quit.armed && heap_pointer(sub) && MEM_HU(0x1E, sub) != quit.index) {
        quit.armed = false;
        quit_label(rdram, kQuitLabel);
    }
}

// MakeScrollList where Options is made (s1 the substruct, its list at +0): in a fight, Load Game and Quick Load first.
extern "C" void aidyn_fight_load_entries(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = MEM_W(0, ctx->r17);
    const gpr strings = MEM_W(0, kCommonStrings);
    if (MEM_BU(0, kTrekState) != 3 || !heap_pointer(list) || !heap_pointer(strings)) {
        return;
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetClipText__WidgetClipText, (uint32_t)memory,
                                              (uint32_t)MEM_W(0x280, strings), 400);  // the game's "Load Game"
    MEM_H(0x5E, entry) = (int16_t)kLoadGameId;
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
    aidyn_quick_load_entry(rdram, ctx);
}

namespace {
    constexpr int kTitleRow = 19;  // the title screen's entries: one every 19 units (the font, vSpace 8)

    int list_entries(uint8_t* rdram, gpr list) {
        const gpr sub = heap_pointer(list) ? MEM_W(0x40, list) : 0;
        return heap_pointer(sub) ? MEM_HU(0x20, sub) : 0;  // WSMSub numChoices
    }
}

// The title screen's menu (StartGameMenu) after Options is appended (s1 the list): Quit Game. The game's menu had three
// entries; with Continue and Quit Game the list and its title (the list's first child) move up half a row for each
// entry past three, so the menu stays centred.
extern "C" void aidyn_quit_game_entry(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = ctx->r17;
    if (!heap_pointer(list)) {
        return;
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr controls = (gpr)(int32_t)call_game(rdram, ctx, WidgetText__WidgetText, (uint32_t)memory,
                                                 (uint32_t)put_menu_text(rdram, kControlsText, "Controls"), 400);
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)controls);
    if (ach_shown()) {
        const gpr memory_a = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
        if (heap_pointer(memory_a)) {
            const gpr ach_entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetText__WidgetText, (uint32_t)memory_a,
                                                          (uint32_t)put_menu_text(rdram, kAchText, "Achievements"), 400);
            call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)ach_entry);
        }
    }
    const gpr memory2 = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory2)) {
        return;
    }
    const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetText__WidgetText, (uint32_t)memory2,
                                              (uint32_t)put_menu_text(rdram, kQuitGameText, "Quit Game"), 400);
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
    const int up = std::max(0, list_entries(rdram, list) - 3) * kTitleRow / 2;
    MEM_H(0x62, list) = (int16_t)(MEM_H(0x62, list) - up);
    gpr child = MEM_W(0x50, list);
    for (int i = 0; i < 8 && heap_pointer(child); i++, child = MEM_W(0x4C, child)) {
        MEM_H(0x62, child) = (int16_t)(MEM_H(0x62, child) - up);
    }
}

// IntroMenu::ShowStartGameMenu's backing box (IntroMenu_ShadowBG: a3 its bottom, shifted left 16; v1 the list): made
// for three entries, a row taller for each one more.
extern "C" void aidyn_title_box_fit(uint8_t* rdram, recomp_context* ctx) {
    const int more = list_entries(rdram, ctx->r3) - 3;
    if (more > 0) {
        const int32_t bottom = ((int32_t)ctx->r7 >> 16) + more * kTitleRow;
        ctx->r7 = (gpr)(int32_t)((uint32_t)bottom << 16);
    }
}

// The title menu's backing box (#37): spitzdano's pick, in the combat turn order bar's style. Codex painted the pieces
// from the turn bar (art/hud/source/title-panel, tools/branding/title_panel.py): a bar across the top (end caps with
// their amber gems and the bar's middle repeated) behind the title "Menu", the bar's dark slate below it with bronze
// rails down its sides, and a slimmer bar closing it at the bottom. IntroMenu_ShadowBG's box (a stretched pixel with
// gradient edges and corners as children) isn't drawn: the panel takes its place and its size, and fades with it.
// Hooked at WidgetBorg8::Render's entry: the IntroMenu (gGlobals.titleScreen) keeps its box at +0x84 (and the one
// fading out at +0x80). The title's Options page gets the same panel.
namespace {
    constexpr gpr kTitleScreen = (gpr)(int32_t)0x800D9590;  // gGlobals.titleScreen (IntroMenu*)

    // A piece of the panel drawn w x h units at (x, y) (units of the 320x240 screen).
    gpr panel_piece(uint8_t* rdram, gpr g, const char* name, float x, float y, float w, float h, float hs, float vs) {
        const int i = branding_index(name);
        if (i < 0) return g;
        const auto& img = aidyn::branding::kImages[i];
        return draw_rgba16(rdram, g, (size_t)i, x * hs, y * vs, w / img.w * hs, h / img.h * vs);
    }

    // A bar from x0 to x1, h units tall, top at y: its end caps (the left one `left`) and its middle repeated between,
    // each repeat stretched a little so a whole number of them fill the gap (they join seamlessly).
    gpr panel_bar(uint8_t* rdram, gpr g, const char* left, float x0, float x1, float y, float h, float hs, float vs) {
        const float k = h / 30.0f;  // the pieces are 30 texels tall
        const int li = branding_index(left);
        const float lw = (li >= 0 ? aidyn::branding::kImages[li].w : 25) * k, rw = 25.0f * k;
        g = panel_piece(rdram, g, left, x0, y, lw, h, hs, vs);
        g = panel_piece(rdram, g, "tp_cap_r", x1 - rw, y, rw, h, hs, vs);
        const float mx0 = x0 + lw, mx1 = x1 - rw;
        const int n = std::max(1, (int)std::lround((mx1 - mx0) / (64.0f * k)));
        const float mw = (mx1 - mx0) / n;
        for (int i = 0; i < n; i++) g = panel_piece(rdram, g, "tp_bar_mid", mx0 + i * mw, y, mw, h, hs, vs);
        return g;
    }
}

// A title written over the panel's top bar (Fair Play, Controls): the letters' top this far down, so the 0.9 scale title
// sits in the bar's middle (at 26 it rode the bar's upper rail).
constexpr float kPanelTitleY = 33.0f;

// The panel from (x0, y0) to (x1, y1), faded by alpha: the slate body with its rails, the top bar (28 units, behind a
// title) and the closing bar (16).
gpr title_panel(uint8_t* rdram, recomp_context* ctx, gpr g, float x0, float y0, float x1, float y1, uint32_t alpha) {
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    g = tinted_image_state(rdram, g, 0xFFFFFF00u | alpha);
    // The bars: 28 units tall at the top (behind the title), 16 at the bottom; their painted part is rows 14..226
    // of 240, its rails at the top and bottom edges. The slate body runs from under the top bar's lower rail to
    // under the bottom bar's upper rail.
    constexpr float kTop = 28.0f, kFoot = 16.0f;
    const float ty = y0 + 7.0f, fy = y1 - kFoot + 1.0f;  // the top bar centred on the title
    const float bx0 = x0 + 3.0f, bx1 = x1 - 3.0f, by0 = ty + kTop * 207.0f / 240.0f, by1 = fy + kFoot * 30.0f / 240.0f;
    const int nx = std::max(1, (int)std::lround((bx1 - bx0) / 32.0f)), ny = std::max(1, (int)std::lround((by1 - by0) / 32.0f));
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            g = panel_piece(rdram, g, "tp_body", bx0 + (bx1 - bx0) * i / nx, by0 + (by1 - by0) * j / ny,
                            (bx1 - bx0) / nx, (by1 - by0) / ny, hs, vs);
        }
    }
    const int nr = std::max(1, (int)std::lround((by1 - by0) / 64.0f));
    for (int j = 0; j < nr; j++) {
        const float ry = by0 + (by1 - by0) * j / nr, rh = (by1 - by0) / nr;
        g = panel_piece(rdram, g, "tp_rail_l", bx0 - 2.0f, ry, 5.0f, rh, hs, vs);
        g = panel_piece(rdram, g, "tp_rail_r", bx1 - 3.0f, ry, 5.0f, rh, hs, vs);
    }
    g = panel_bar(rdram, g, "tp_cap_l", x0 - 10.0f, x1 + 10.0f, ty, kTop, hs, vs);
    g = panel_bar(rdram, g, "tp_cap_l", x0 - 2.0f, x1 + 2.0f, fy, kFoot, hs, vs);
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}

// The Options page over the title screen's panel (WidgetOptionsConfig::InitMenu's end): its title picture sat 1.3 units
// high in the panel's top bar, and its rows leaned left (AUDIO 14 units from the left rail, the bars ending 22 short of
// the right one). The title goes down a unit and the rows (AUDIO, Sound, Music, Graphics and their bars, which take the
// labels' x) 4 units right. Substruct: AudioTitle +0, GraphicsTitle +4, optionTitles[0..3] +0xC.., TitleImage +0x1C.
namespace {
    // The shifted page (s4) and its vtable word, to tell it is still the one open (the title screen's box, sized from
    // the page's rows, is moved back by the same 4 units: aidyn_title_box).
    gpr options_page = 0;
    uint32_t options_page_vtable = 0;
    bool options_box_shifted(uint8_t* rdram) {
        return heap_pointer(options_page) && (uint32_t)MEM_W(0x78, options_page) == options_page_vtable;
    }
}

extern "C" void aidyn_options_layout(uint8_t* rdram, recomp_context* ctx) {
    if (!heap_pointer(MEM_W(0, kTitleScreen))) return;  // the pause menu's page is on the parchment: left as it is
    const gpr sub = ctx->r19;
    if (!heap_pointer(sub)) return;
    options_page = ctx->r20;
    options_page_vtable = heap_pointer(options_page) ? (uint32_t)MEM_W(0x78, options_page) : 0;
    for (int off : { 0x0, 0x4, 0xC, 0x10, 0x14, 0x18 }) {
        const gpr w = MEM_W(off, sub);
        if (heap_pointer(w)) MEM_H(kWidgetPosX, w) = (int16_t)(MEM_H(kWidgetPosX, w) + 4);
    }
    const gpr title = MEM_W(0x1C, sub);
    if (heap_pointer(title)) MEM_H(kWidgetPosX + 2, title) = (int16_t)(MEM_H(kWidgetPosX + 2, title) + 1);
}

extern "C" int aidyn_title_box(uint8_t* rdram, recomp_context* ctx) {
    const gpr menu = MEM_W(0, kTitleScreen), w = ctx->r4;
    if (!heap_pointer(menu) || !heap_pointer(w) || (w != MEM_W(0x84, menu) && w != MEM_W(0x80, menu))) {
        return 0;
    }
    const uint32_t alpha = MEM_BU(0x5B, w);  // the box's colour: its alpha fades it in and out
    gpr g = ctx->r5;
    if (alpha != 0) {
        float x0 = (int16_t)MEM_HU(kWidgetPosX, w), y0 = (int16_t)MEM_HU(kWidgetPosX + 2, w);
        float x1 = x0 + MEM_HU(0x74, w), y1 = y0 + MEM_HU(0x72, w);
        static const bool box_log = std::getenv("AIDYN_BOX_LOG") != nullptr;  // each new box's size, for fitting
        static float logged[4] = {};
        if (box_log && (logged[0] != x0 || logged[1] != y0 || logged[2] != x1 || logged[3] != y1)) {
            fprintf(stdout, "Title box: %g,%g to %g,%g\n", x0, y0, x1, y1);
            fflush(stdout);
            logged[0] = x0, logged[1] = y0, logged[2] = x1, logged[3] = y1;
        }
        // The Options page's box is sized down to its Game row (Rename Character), which the title screen hides: it
        // ran off the bottom of the screen. A box that would leave the screen ends just under the Music slider
        // instead (the page's fixed layout: 116 units from the box's top, and room for the closing bar).
        // The Load Game page (a box of 51,24 to 270,216) has its picture and portrait up against the top bar and its
        // Load, Delete and Exit row on the closing one: the panel takes a little more room, up and down and at the sides.
        if (x0 == 51.0f && y0 == 24.0f && x1 == 270.0f && y1 == 216.0f) {
            x0 = 45.0f;
            y0 = 16.0f;  // the top bar behind the title picture (at 11 the picture hung over the bar's lower rail)
            x1 = 276.0f;
            y1 = 232.0f;
        }
        // The name entry (New Game: a box of 52,43 to 260,179) has its fourth row of letters where the closing bar goes:
        // the panel runs 13 units further down, and 4 wider each side for the grid's last column.
        if (x0 == 52.0f && y0 == 43.0f && x1 == 260.0f && y1 == 179.0f) {
            x0 = 48.0f;
            x1 = 264.0f;
            y1 = 192.0f;
        }
        if (y1 > 232.0f) y1 = std::min(y1, y0 + 140.0f);
        if (options_box_shifted(rdram) && x1 - x0 == 208.0f) {  // the Options page's box follows its rows: kept centred
            x0 -= 4.0f;
            x1 -= 4.0f;
        }
        g = title_panel(rdram, ctx, g, x0, y0, x1, y1, alpha);
    }
    ctx->r2 = g;
    return 1;
}

// The Menu page's list once placed (s2 its title, s1 the substruct with the list at +0): ten entries (the field's,
// with Quick Load and Quit to Title) reach the page arrow under them; the title and list move up 8 units.
extern "C" void aidyn_menu_list_fit(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = MEM_W(0, ctx->r17);
    if (list_entries(rdram, list) < 10 || !heap_pointer(ctx->r18)) {
        return;
    }
    MEM_H(0x62, list) = (int16_t)(MEM_H(0x62, list) - 8);
    MEM_H(0x62, ctx->r18) = (int16_t)(MEM_H(0x62, ctx->r18) - 8);
}

// The title screen's menu (IntroMenu::StartGameMenu, func_80043AE8) before its first entry is made (s1 the list; v0
// the first entry's memory, kept by call_game): Continue.
extern "C" void aidyn_continue_entry(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = ctx->r17;
    if (!heap_pointer(list) || (!save_exists(kQuickFile) && !save_exists(kAutoFile1) && !save_exists(kAutoFile2) &&
                                !save_exists(kLastFile))) {
        return;
    }
    const gpr memory = (gpr)(int32_t)call_game(rdram, ctx, new_, 0x7C);
    if (!heap_pointer(memory)) {
        return;
    }
    const gpr entry = (gpr)(int32_t)call_game(rdram, ctx, WidgetText__WidgetText, (uint32_t)memory,
                                              (uint32_t)put_menu_text(rdram, kContinueText, "Continue"), 400);
    call_game(rdram, ctx, WidgetScrollMenu__Apppend, (uint32_t)list, (uint32_t)entry);
}

// IntroMenu::ProcessIntroMenu's entry (processIntroMenu: a1 the chosen text widget, its substruct at +0x40 with the
// string at +0): Continue, and nothing else once it is on its way. True: the game's own is skipped.
extern "C" int aidyn_title_choice(uint8_t* rdram, recomp_context* ctx) {
    if (saves.continuing) {
        return 1;
    }
    const gpr sub = heap_pointer(ctx->r5) ? MEM_W(0x40, ctx->r5) : 0;
    const gpr str = heap_pointer(sub) ? MEM_W(0, sub) : 0;
    if (str == 0) {
        return 0;
    }
    std::string text;
    for (int i = 0; i < 16 && MEM_B(i, str) != 0; i++) {
        text += (char)MEM_B(i, str);
    }
    if (text == "Controls") {
        rc_sound(rdram, ctx, 0x740, 60);
        title_controls_open(rdram, ctx);
        return 1;
    }
    if (text == "Achievements") {
        rc_sound(rdram, ctx, 0x740, 60);
        title_ach_open(rdram, ctx);
        return 1;
    }
    if (text == "New Game" && fair_panel_open(rdram, ctx)) {  // the Fair play panel first ("Fair play", below)
        rc_sound(rdram, ctx, 0x740, 60);
        return 1;
    }
    if (text == "Quit Game") {
        rc_sound(rdram, ctx, 0x740, 60);
        aidyn::request_quit();
        return 1;
    }
    if (text != "Continue") {
        return 0;
    }
    if (!read_newest(rdram, ctx, { kQuickFile, kAutoFile1, kAutoFile2, kLastFile })) {
        notice(passed_over.empty() ? "No save to load"
                                   : (std::string("That save is from ") + version_title(passed_over) + ": switch on the title screen").c_str());
        rc_sound(rdram, ctx, 0x741, 60);
        return 1;
    }
    saves.continuing = true;
    call_game(rdram, ctx, func_8001A20C, (uint32_t)kSaveBuffer);
    call_game(rdram, ctx, func_80043958);
    rc_sound(rdram, ctx, 0x740, 60);
    return 1;
}

// ---- The trail and the freeze watchdog ----
// Once a frame (Graphics::EndList, here): the freeze watchdog's heartbeat with what the game is doing
// (src/main/watch.cpp), a line in the log each time that changes (the trail: the last lines before a freeze or a crash
// say what led to it), and the notice about the last session's report. What it is doing: the title screen or the
// game's state (gGlobals.gameStateA, with gameStateB while walking and the fight's state in a fight), the map and its
// cell. The fight's state changes with every move, so it is in the watchdog's notes but not the trail.
namespace {
    std::string state_name(uint32_t a) {
        switch (a) {
            case 0: return "to the title";
            case 1: case 9: return "walking";
            case 2: return "fight";
            case 3: return "fight, paused";
            case 5: return "trainer";
            case 6: return "after a fight";
            case 7: return "shop or treasure";
            case 8: return "pause menu";
            case 12: return "conversation";
            case 14: return "scene";
            case 15: return "starting";
            case 17: return "Controller Pak";
            default: return "state " + std::to_string(a);
        }
    }
    std::string trail_last;
    const auto trail_start = std::chrono::steady_clock::now();
}

void watch_tick(uint8_t* rdram) {
    const uint32_t a = MEM_BU(0, kTrekState), b = MEM_BU(0, kGameStateB);
    std::string trail;
    if (heap_pointer(MEM_W(0, kTitleScreen))) {
        trail = "title screen";
    } else {
        trail = state_name(a);
        if (a == 1) trail += " (" + std::to_string(b) + ")";
        trail += ", map " + std::to_string(MEM_HU(0, kMapDatA)) + " cell " + std::to_string(MEM_H(0, kMapShort1)) + "," +
                 std::to_string(MEM_H(0, kMapShort1 + 2));
    }
    if (trail != trail_last) {
        trail_last = trail;
        const long s = (long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - trail_start).count();
        fprintf(stdout, "Trail %ld:%02ld:%02ld %s\n", s / 3600, s / 60 % 60, s % 60, trail.c_str());
        fflush(stdout);
    }
    // The mouse looks around only while walking about (not in a menu, a conversation or a fight).
    aidyn::controls::want_capture(!heap_pointer(MEM_W(0, kTitleScreen)) && a == 1 && !quit_pending);
    aidyn::watch::frame(a == 2 || a == 3 ? trail + ", fight state " + std::to_string(MEM_BU(0, (gpr)(int32_t)0x800D9454)) : trail);
    // AIDYN_FREEZE_TEST=1: once, after 10 seconds of walking about, the game thread stops for 25 seconds, to try the
    // watchdog's notes and the next start's report end to end (Steam launch options: AIDYN_FREEZE_TEST=1 %command%);
    // =start: 20 seconds after the first frame, wherever it is (the title screen: no controller needed).
    static const char* freeze_test = std::getenv("AIDYN_FREEZE_TEST");
    static const bool at_start = freeze_test != nullptr && std::strcmp(freeze_test, "start") == 0;
    static double walked = 0.0;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (freeze_test != nullptr && (a == 1 || at_start) && walked >= 0.0) {
        walked += std::chrono::duration<double>(now - last).count();
        if (walked >= (at_start ? 20.0 : 10.0)) {
            walked = -1.0;
            fprintf(stdout, "Freeze test: the game thread stops for 25 s\n");
            fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(25));
        }
    }
    last = now;
    if (const std::string n = aidyn::watch::take_notice(); !n.empty()) {
        notice(n.c_str(), 8000);
    }
}

// ---- Achievements (#42) ----
// Milestones read from the event flags the game keeps in its saves (decomp Flagindecies.h): the story's journal
// entries, the bosses' "killed" flags, each companion's "joined" flag, maps, keys, stats and skills; and a few the port
// counts itself (fights won, a flawless fight, a camp ambush survived, a fight fled, gold, levels, a full party). Kept
// per version (achievements-<pak stem>.txt beside the saves: not in a save), so they add up across playthroughs:
// "Every Companion" needs all 13, which one playthrough can't recruit (the party holds four; at Gwernia only 2 of
// Godric, Rheda and Abrecan go along). Every locked one is hidden (the page shows ???). Checked once a second while a
// game is loaded (getEventFlag); nothing unlocks while the game's own Cheater flag (3693) is set.
// Only what is done in play counts: when a game starts (the title screen goes) or a save is loaded, the first 3 seconds
// are its starting point (every flag it already has, its level, gold and party size), and a flag unlocks only when it
// comes on after that (the last of an achievement's flags, when it has several), a level, gold or party size only when
// it crosses its threshold. A late save never unlocks the story so far. Flawless: a fight won with a full party of
// four against four or more enemies and nobody in the party hit (Entity::DamageToLevel on a party member, hooked:
// a spell's stamina cost doesn't go through it). AIDYN_ACH_LOG=1 prints each unlock and count; AIDYN_ACHIEVEMENTS=0
// turns them off. Each unlock shows a toast at the top right (ach_toast: the medal on the turn bar's pieces), and the
// ReChronicled page's Achievements row lists them (achievements_page).
extern "C" void getEventFlag(uint8_t* rdram, recomp_context* ctx);
extern "C" void setEventFlag(uint8_t* rdram, recomp_context* ctx);

namespace {
    enum AchKind : uint8_t { kAchAll, kAchAny, kAchCount };
    struct Achievement {
        const char* id;
        const char* title;
        AchKind kind;
        std::vector<uint16_t> flags;  // kAchAll: every one set; kAchAny: one set; kAchCount: see ach_count
    };
    // The groups the page lists them in (the side quests are optional ones among the story's journal entries), and each
    // group's medal (Codex's: kind names of art/hud/source/achievements/medal_<kind>.png).
    enum AchCat : uint8_t { kCatStory, kCatSide, kCatBoss, kCatBossOpt, kCatCompanion, kCatExplore, kCatGrowth, kCatFeat, kCatCount };
    constexpr const char* kCatNames[kCatCount] = { "Story", "Side quests", "Bosses", "Optional bosses", "Companions",
                                                   "Exploration", "Growth", "Feats" };
    constexpr const char* kCatMedals[kCatCount] = { "story", "side", "boss", "boss", "companion", "explore", "growth", "feat" };
    AchCat ach_cat(const char* id) {
        const std::string s = id;
        // The side quests: Trahern's sword (a favour, the journal's 85/19/87; not in Prima's route), Pandara's amulet (234:
        // only for a courteous player) and the three "side_" ones the journal notes (a letter, a wild man, Elisheva's
        // tomb). Mago's house and Batrin's tale of the knight on the bridge (Arturo: recruiting him is "Joined by
        // Arturo") are on the story's road, not side quests. The lighthouse
        // (Captain Amann's boat needs the port open) and Yeraza's dinner (the Stormbreaker for the voyage) are the story's.
        if (s == "story_sword" || s == "story_forest" || s.rfind("side_", 0) == 0) return kCatSide;
        if (s.rfind("story_", 0) == 0) return kCatStory;
        // the three story bosses; the other eight (the journal doesn't mention them) are optional
        if (s == "boss_kitarak" || s == "boss_shamsuk" || s == "boss_lugash") return kCatBoss;
        if (s.rfind("boss_", 0) == 0) return kCatBossOpt;
        if (s.rfind("join_", 0) == 0 || s == "full_party" || s == "every_companion") return kCatCompanion;
        if (s == "cartographer" || s == "keymaster") return kCatExplore;
        if (s == "first_spell" || s == "brewer" || s == "peak_form" || s == "master" || s == "veteran" || s == "legend") return kCatGrowth;
        return kCatFeat;
    }

    const std::vector<Achievement>& achievements() {
        static const std::vector<Achievement> list = {
            // the story (journal entries' flags)
            { "story_spirit", "A Strange Spirit", kAchAll, { 9 } },
            { "story_poisoned", "Poisoned", kAchAll, { 50 } },
            { "story_errand", "The King's Errand", kAchAll, { 68 } },
            { "story_sword", "The Missing Sword", kAchAll, { 87 } },
            { "story_erromon", "City Under the Hills", kAchAll, { 122 } },
            { "story_promise", "A Broken Promise", kAchAll, { 166 } },
            { "story_dinner", "Dinner Guest", kAchAll, { 192 } },
            { "story_school", "Wizard's School", kAchAll, { 216 } },
            { "story_amaranth", "The Amaranth", kAchAll, { 222 } },
            { "story_forest", "Friend of the Forest", kAchAll, { 234 } },
            { "story_nameless", "Nameless", kAchAll, { 240 } },
            { "story_mago", "A Cure for Mago", kAchAll, { 390 } },  // Mago's house in Terminor: Fyrsil, the Jundar wizard
            { "story_light", "Let There Be Light", kAchAll, { 5017 } },
            { "story_sail", "Set Sail", kAchAll, { 297 } },
            { "story_isle", "Cradawgh's Isle", kAchAll, { 309 } },
            { "story_bog", "Into the Bog", kAchAll, { 414 } },
            { "story_trolls", "Troll Toll", kAchAll, { 399 } },
            { "story_tower", "The Tower", kAchAll, { 438 } },
            { "story_gate", "The Desert Gate", kAchAll, { 480 } },
            { "story_emperor", "The Emperor", kAchAll, { 518 } },
            { "story_fate", "Fate", kAchAll, { 548 } },
            { "story_whole", "Whole Again", kAchAll, { 566 } },
            { "story_named", "Named", kAchAll, { 601 } },
            // the side quests the journal notes (its entries' flags; Prima's guide: Elisheva's tomb is its "Side Quest"):
            // the strange letter (Shamsuk's journal, in the Saiid cave that also holds the lighthouse scroll), the wild
            // man of the bog (he gives directions to Shamsuk's tower), Elisheva's tomb
            { "side_letter", "A Strange Letter", kAchAll, { 295 } },
            { "side_wildman", "The Wild Man of the Bog", kAchAll, { 425 } },
            { "side_tomb", "Elisheva's Tomb", kAchAll, { 429 } },
            // bosses (their killed flags)
            { "boss_kitarak", "The Goblin King", kAchAll, { 154 } },
            { "boss_shamsuk", "The Necromancer", kAchAll, { 458 } },
            { "boss_lugash", "The Lich", kAchAll, { 533 } },
            { "boss_pochanargat", "Pochanargat", kAchAll, { 3204 } },
            { "boss_behrooz", "Behrooz", kAchAll, { 3733 } },
            { "boss_nasim", "Nasim", kAchAll, { 3734 } },
            { "boss_golnar", "Golnar", kAchAll, { 3735 } },
            { "boss_mehrdad", "Mehrdad", kAchAll, { 3736 } },
            { "boss_shatrevar", "Shatrevar", kAchAll, { 3737 } },
            { "boss_ksathra", "Ksathra", kAchAll, { 3738 } },
            { "boss_assim", "Assim", kAchAll, { 3739 } },
            // companions (their joined flags, which stay set after they leave)
            { "join_brenna", "Joined by Brenna", kAchAll, { 92 } },
            { "join_abrecan", "Joined by Abrecan", kAchAll, { 89 } },
            { "join_rheda", "Joined by Rheda", kAchAll, { 91 } },
            { "join_godric", "Joined by Godric", kAchAll, { 90 } },
            { "join_arturo", "Joined by Arturo", kAchAll, { 247 } },
            { "join_keelin", "Joined by Keelin", kAchAll, { 265 } },
            { "join_becan", "Joined by Becan", kAchAll, { 375 } },
            { "join_baird", "Joined by Baird", kAchAll, { 380 } },
            { "join_donovan", "Joined by Donovan", kAchAll, { 382 } },
            { "join_niesen", "Joined by Niesen", kAchAll, { 412 } },
            { "join_farris", "Joined by Farris", kAchAll, { 455 } },
            { "join_sholeh", "Joined by Sholeh", kAchAll, { 523 } },
            { "join_dougal", "Joined by Dougal", kAchAll, { 574 } },
            { "full_party", "A Full Party", kAchCount, {} },
            { "every_companion", "Every Companion", kAchCount, {} },
            // exploration
            { "cartographer", "Cartographer", kAchAll, { 3406, 3407, 3408, 3409, 3410, 3411, 3412, 3413, 3414, 3415, 3416,
                                                          3418, 3419, 3420, 3421, 3422, 3423, 3424 } },
            { "keymaster", "Keymaster", kAchAll, { 3425, 3426, 3427, 3428, 3429, 3430, 3431, 3432 } },
            // growth
            { "first_spell", "First Spell", kAchAll, { 3611 } },  // FLAG_LearnedFirstSpell: a new spell from a trainer
            // a potion brewed (aidyn_potion_brewed; flag 3612 is FLAG_GotFirstPotion, any potion taken, a chest's too)
            { "brewer", "Brewer", kAchCount, {} },
            { "peak_form", "Peak Form", kAchAny, { 5290, 5294, 5298, 5302, 5306 } },
            { "master", "Master", kAchAny, { 5346, 5336, 5316, 5326 } },
            { "veteran", "Veteran", kAchCount, {} },
            { "legend", "Legend", kAchCount, {} },
            // feats
            { "seasoned", "Seasoned", kAchCount, {} },
            { "flawless", "Flawless", kAchCount, {} },
            { "night_watch", "Night Watch", kAchCount, {} },
            { "discretion", "Discretion", kAchCount, {} },
            { "treasure_hoard", "Treasure Hoard", kAchCount, {} },
            // fair play: the game finished with a rule on all the way ("Fair play")
            { "fair_healer", "Honest Healer", kAchCount, {} },
            { "fair_trade", "Fair Trader", kAchCount, {} },
            { "fair_alchemy", "True Alchemist", kAchCount, {} },
            { "fair_stay_won", "Settled Scores", kAchCount, {} },
            { "fair_chests", "Fair Finder", kAchCount, {} },
            { "fair_keen_eye", "Keen Eye", kAchCount, {} },
            { "fair_shops", "Old Stock", kAchCount, {} },
        };
        return list;
    }

    // How each is earned: shown under the list for the highlighted one (a secret one's only once it is earned).
    const char* ach_how(const std::string& id) {
        static const std::unordered_map<std::string, const char*> kHow = {
            { "story_spirit", "Meet the strange spirit in the forest." },
            { "story_poisoned", "Wake in Oriana's hut, poisoned by the goblins." },
            { "story_errand", "Take the King's errand to the Mirari of Erromon." },
            { "story_sword", "Find the sword Trahern lost (Bowden hid it in a storeroom off the lab)." },
            { "story_erromon", "Reach Erromon, the Mirari city under the hills." },
            { "story_promise", "Learn that Txomin won't keep his promise." },
            { "story_dinner", "Have dinner with Yeraza in Erromon." },
            { "story_school", "Be let into the Wizard's School in Talewok." },
            { "story_amaranth", "Find the amaranth Ardra needs to cure you." },
            { "story_forest", "Win the favour of Pandara, the dryad queen, and her amulet." },
            { "story_nameless", "Learn from Ardra that you have no True Name." },
            { "story_mago", "Hear of Fyrsil, the wizard who may have a cure for Mago." },
            { "story_light", "Light the lighthouse and open the port of Saiid." },
            { "story_sail", "Set sail for Cradawgh's Isle." },
            { "story_isle", "Reach Cradawgh's Isle." },
            { "story_bog", "Reach the Darkling Bog." },
            { "story_trolls", "Defeat the trolls of the bog." },
            { "story_tower", "Reach Shamsuk's tower." },
            { "story_gate", "Solve the riddle of the Desert Gate." },
            { "story_emperor", "Speak with Zaratas, the Jundar Emperor." },
            { "story_fate", "Recover Loden's sword, Fate." },
            { "story_whole", "Join with your darker self." },
            { "story_named", "Finish the game: be given your True Name." },
            { "side_letter", "Find the strange letter in the cave under Saiid." },
            { "side_wildman", "Meet the wild man of the Darkling Bog." },
            { "side_tomb", "Find Elisheva's tomb in the Darkling Bog." },
            { "boss_kitarak", "Defeat Kitarak, the goblin king." },
            { "boss_shamsuk", "Defeat Shamsuk, the necromancer, in his tower." },
            { "boss_lugash", "Defeat Lugash, the lich under the Barrow." },
            { "full_party", "Have four in the party." },
            { "every_companion", "Have all 13 companions join you." },
            { "cartographer", "Collect all 18 maps." },
            { "keymaster", "Collect all eight keys." },
            { "first_spell", "Learn a new spell from a trainer." },
            { "brewer", "Brew a potion (Alchemist skill, the Alchemy menu)." },
            { "peak_form", "Have a stat at 29: Intelligence, Willpower, Endurance, Dexterity or Strength." },
            { "master", "Reach rank 10 in Wizard, Lore, Mechanic or Diplomat." },
            { "veteran", "Bring someone in the party to level 10." },
            { "legend", "Bring someone in the party to level 20." },
            { "seasoned", "Win 100 fights." },
            { "flawless", "With four in the party, beat four or more enemies without a scratch." },
            { "night_watch", "Win a fight that ambushed your camp." },
            { "discretion", "Flee from a fight." },
            { "treasure_hoard", "Hold 10,000 gold at once." },
            { "fair_healer", "Finish the game with the Healer rule on (Fair Play, at New Game)." },
            { "fair_trade", "Finish the game with Fair trade on (Fair Play, at New Game)." },
            { "fair_alchemy", "Finish the game with Honest alchemy on (Fair Play, at New Game)." },
            { "fair_stay_won", "Finish the game with Fights stay won on (Fair Play, at New Game)." },
            { "fair_chests", "Finish the game with Chests stay empty on (Fair Play, at New Game)." },
            { "fair_keen_eye", "Finish the game without the treasure sparkle (Fair Play, at New Game)." },
            { "fair_shops", "Finish the Original with its own shops: Better shops off (Fair Play, at New Game)." },
        };
        const auto it = kHow.find(id);
        if (it != kHow.end()) return it->second;
        static std::map<std::string, std::string> made;  // the companions' and the optional bosses'
        auto& m = made[id];
        if (m.empty()) {
            for (const Achievement& a : achievements()) {
                if (a.id != id) continue;
                const std::string t = a.title;
                if (id.rfind("join_", 0) == 0) m = "Have " + t.substr(10) + " join the party.";  // "Joined by X"
                else if (id.rfind("boss_", 0) == 0) m = "Defeat " + t + ".";
            }
        }
        return m.c_str();
    }

    // The secret kinds (spitzdano): the story, the side quests, the companions and the optional bosses show ??? until
    // earned; the rest (the story's bosses, exploration, growth, feats, fair play) show their names and how from the start.
    bool ach_secret(int cat) { return cat == kCatStory || cat == kCatSide || cat == kCatCompanion || cat == kCatBossOpt; }

    struct AchState {
        bool loaded = false;
        std::map<std::string, std::string> unlocked;  // id -> when
        int fights_won = 0;
        bool brewed = false;  // a potion brewed since the last check (aidyn_potion_brewed)
        // the fight in progress
        bool in_fight = false, fight_hit = false, fight_full = false, fight_fled = false, fight_from_camp = false;
        int fight_enemies = 0;
        uint32_t last_a = 0xFF, last_walk_b = 0xFF;
        std::vector<std::pair<std::string, int>> earned;  // (title, category) in this check, for the toasts
        // The game's starting point (a new game, a save loaded): see the comment above.
        bool base_started = false;
        std::chrono::steady_clock::time_point base_until{};
        std::set<uint16_t> base_flags;
        int last_level = -1, last_party = -1;
        uint32_t last_gold = 0;
        struct Toast { std::string head, title, medal; };
        std::deque<Toast> toasts;
        bool toast_on = false;
        std::chrono::steady_clock::time_point toast_start{};
        std::chrono::steady_clock::time_point next_check{};
    } ach;

    // On unless AIDYN_ACHIEVEMENTS=0 (nothing tracked, saved or shown then).
    bool ach_enabled() {
        static const bool on = [] { const char* v = std::getenv("AIDYN_ACHIEVEMENTS"); return v == nullptr || *v == '\0' || *v != '0'; }();
        return on;
    }
    bool ach_log() {
        static const bool on = std::getenv("AIDYN_ACH_LOG") != nullptr;
        return on;
    }
    std::filesystem::path ach_path() { return save_dir / ("achievements-" + save_stem + ".txt"); }

    void ach_load() {
        if (ach.loaded || save_dir.empty()) return;
        ach.loaded = true;
        std::ifstream in(ach_path());
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("fights_won ", 0) == 0) ach.fights_won = std::atoi(line.c_str() + 11);
            else if (line.rfind("unlocked ", 0) == 0) {
                const std::string rest = line.substr(9);
                const size_t sp = rest.find(' ');
                ach.unlocked[rest.substr(0, sp)] = sp == std::string::npos ? "" : rest.substr(sp + 1);
            }
        }
    }
    void ach_save() {
        std::error_code ec;
        std::filesystem::create_directories(save_dir, ec);
        const std::filesystem::path tmp = ach_path().string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << "# Aidyn ReChronicled achievements (" << save_stem << ")\n";
            out << "fights_won " << ach.fights_won << "\n";
            for (const auto& [id, when] : ach.unlocked) out << "unlocked " << id << " " << when << "\n";
        }
        std::filesystem::rename(tmp, ach_path(), ec);
    }
    void ach_unlock(const std::string& id) {
        if (ach.unlocked.count(id)) return;
        char when[32];
        const std::time_t t = std::time(nullptr);
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&t));
        ach.unlocked[id] = when;
        if (id == "story_named") {  // the ending: the fair play rules kept all game
            static const char* kFair[7] = { "fair_healer", "fair_trade", "fair_alchemy", "fair_keen_eye", "fair_chests",
                                            "fair_stay_won", "fair_shops" };
            for (int i = 0; i < 7; i++) {
                if (fair_rule_kept(i)) ach_unlock(kFair[i]);
            }
        }
        for (const Achievement& a : achievements()) {
            if (a.id == id) ach.earned.push_back({ a.title, (int)ach_cat(a.id) });
        }
        if (ach_log()) {
            fprintf(stdout, "Achievement: %s\n", id.c_str());
            fflush(stdout);
        }
    }
    bool flag_set(uint8_t* rdram, recomp_context* ctx, uint16_t f) {
        return (call_game(rdram, ctx, getEventFlag, f) & 0xFF) != 0;
    }
}

void ach_loaded_save() {
    if (!ach_enabled()) return;
    ach.base_started = false;  // what the save holds is its starting point, not earned
    ach.in_fight = false;
    ach.brewed = false;
}

// WidgetPotionCraft::AFunc after Party::CraftPotion (v0: the potion made, 0 for none): Brewer.
extern "C" void aidyn_potion_brewed(uint8_t* rdram, recomp_context* ctx) {
    if (ach_enabled() && (uint32_t)ctx->r2 != 0) ach.brewed = true;
}

namespace {
    // The game's cheat codes (typed as a name) were used in this game: !cheater (experience), !bingo (gold), !slashing
    // and !darkside (weapons). No achievement is earned in it then.
    bool ach_cheated(uint8_t* rdram, recomp_context* ctx) {
        return flag_set(rdram, ctx, 3693) || flag_set(rdram, ctx, 3694) || flag_set(rdram, ctx, 3673) || flag_set(rdram, ctx, 3675);
    }
}

// Entity::DamageToLevel's entry (a0 the character, a1 the damage): a hit on a party member in a fight spoils Flawless.
extern "C" void aidyn_ach_damage(uint8_t* rdram, recomp_context* ctx) {
    if (!ach_enabled() || !ach.in_fight || (int16_t)ctx->r5 <= 0) return;
    const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684);
    for (int i = 0; heap_pointer(party) && i < 4; i++) {
        if (MEM_W(4 * i, party) == ctx->r4) ach.fight_hit = true;
    }
}

// Once a frame (Graphics::EndList): the fight events every frame, the flags once a second, the notices when quiet.
void achievements_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!ach_enabled()) return;
    const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684);
    const uint32_t a = MEM_BU(0, kTrekState), b = MEM_BU(0, kGameStateB);
    const bool playing = heap_pointer(party) && !heap_pointer(MEM_W(0, kTitleScreen)) && a != 0 && a != 15;
    if (heap_pointer(MEM_W(0, kTitleScreen))) ach.base_started = false;  // at the title: the next game starts afresh
    if (!playing) {
        ach.last_a = a;
        return;
    }
    ach_load();
    if (a == 1 && b != 0) ach.last_walk_b = b;
    // the fight: its start, a flight, its end
    if (a == 2 && !ach.in_fight) {
        const gpr combat = MEM_W(0, kCombatP);
        ach.in_fight = true;
        ach.fight_hit = ach.fight_fled = false;
        ach.fight_from_camp = ach.last_walk_b == 17;  // camping (gameStateB 17) when the fight began: an ambush
        ach.fight_full = MEM_BU(0x24, party) >= 4;     // Party::PartySize
        ach.fight_enemies = heap_pointer(combat) ? MEM_BU(kEntCount, combat) - MEM_BU(kPartyCount, combat) : 0;
    }
    if (ach.in_fight && (a == 2 || a == 3) && MEM_BU(0, (gpr)(int32_t)0x800D9454) == 20) ach.fight_fled = true;
    if (ach.in_fight && a != 2 && a != 3) {
        ach.in_fight = false;
        if (ach_cheated(rdram, ctx)) {
            // nothing counts
        } else if (a == 6) {  // after a fight: won
            ach.fights_won++;
            if (ach.fights_won >= 100) ach_unlock("seasoned");
            if (ach.fight_full && ach.fight_enemies >= 4 && !ach.fight_hit) ach_unlock("flawless");
            if (ach.fight_from_camp) ach_unlock("night_watch");
        }
        if (ach.fight_fled && !ach_cheated(rdram, ctx)) ach_unlock("discretion");
        ach_save();
    }
    ach.last_a = a;
    // the flags and the counts, once a second
    const auto now = std::chrono::steady_clock::now();
    if (now >= ach.next_check) {
        ach.next_check = now + std::chrono::seconds(1);
        if (!ach.base_started) {  // a game just began (or a save loaded): its first 3 seconds are its starting point
            ach.base_started = true;
            ach.base_until = now + std::chrono::seconds(3);
            ach.base_flags.clear();
            ach.last_level = ach.last_party = -1;
            ach.last_gold = 0;
            // developers: AIDYN_SET_FLAGS=f,f sets those event flags as a game begins (a cheat code's, to see the gate)
            if (const char* fl = std::getenv("AIDYN_SET_FLAGS")) {
                std::stringstream ids(fl);
                std::string id;
                while (std::getline(ids, id, ',')) call_game(rdram, ctx, setEventFlag, (uint32_t)std::atoi(id.c_str()), 1);
            }
        }
        const bool settling = now < ach.base_until;
        if (ach_cheated(rdram, ctx)) {
            ach.brewed = false;
        } else {
            const int party_size = MEM_BU(0x24, party);
            const uint32_t gold = (uint32_t)MEM_W(0x14, party);
            int top_level = 0;
            for (int i = 0; i < 4; i++) {
                const gpr sheet = MEM_W(4 * i, party);
                const gpr stats = heap_pointer(sheet) ? MEM_W(0x18, sheet) : 0;
                if (heap_pointer(stats)) {
                    top_level = std::max(top_level, (int)(int8_t)(call_game(rdram, ctx, CharStats__getBase, (uint32_t)stats, 6) & 0xFF));
                }
            }
            if (settling) {  // what the game starts with isn't earned
                for (const Achievement& x : achievements()) {
                    for (uint16_t f : x.flags) {
                        if (flag_set(rdram, ctx, f)) ach.base_flags.insert(f);
                    }
                }
            } else {
                for (const Achievement& x : achievements()) {
                    if (x.kind == kAchCount || ach.unlocked.count(x.id)) continue;
                    bool all = true, fresh = false;
                    for (uint16_t f : x.flags) {
                        const bool on = flag_set(rdram, ctx, f);
                        all = all && on;
                        fresh = fresh || (on && !ach.base_flags.count(f));  // came on during play
                    }
                    if (x.kind == kAchAll ? (all && fresh) : fresh) ach_unlock(x.id);
                }
                const auto crossed = [](int last, int now_value, int threshold) { return last >= 0 && last < threshold && now_value >= threshold; };
                if (crossed(ach.last_party, party_size, 4)) ach_unlock("full_party");
                if (ach.last_gold < 10000 && gold >= 10000) ach_unlock("treasure_hoard");
                if (crossed(ach.last_level, top_level, 10)) ach_unlock("veteran");
                if (crossed(ach.last_level, top_level, 20)) ach_unlock("legend");
                if (ach.brewed) ach_unlock("brewer");
                ach.brewed = false;
                int joined = 0;
                for (const Achievement& x : achievements()) joined += std::string(x.id).rfind("join_", 0) == 0 && ach.unlocked.count(x.id);
                if (joined >= 13) ach_unlock("every_companion");
                // developers: AIDYN_ACH_FORCE=id,id unlocks those once, to see their toasts and the list
                static bool forced = false;
                if (!forced) {
                    forced = true;
                    const char* f = std::getenv("AIDYN_ACH_FORCE");
                    std::stringstream ids(f != nullptr ? f : "");
                    std::string id;
                    while (std::getline(ids, id, ',')) ach_unlock(id);
                }
            }
            ach.last_party = party_size;
            ach.last_gold = gold;
            ach.last_level = top_level;
        }
        if (!ach.earned.empty()) {
            ach_save();
            for (const auto& [title, cat] : ach.earned) ach.toasts.push_back({ "Achievement unlocked", title, kCatMedals[cat] });
            ach.earned.clear();
        }
    }
    // one toast at a time, started while walking about (ach_toast draws it)
    if (!ach.toast_on && !ach.toasts.empty() && a == 1) {
        ach.toast_on = true;
        ach.toast_start = std::chrono::steady_clock::now();
        if (ach_log()) {
            fprintf(stdout, "Achievement toast: %s / %s\n", ach.toasts.front().head.c_str(), ach.toasts.front().title.c_str());
            fflush(stdout);
        }
    }
}

// Graphics::EndList: the notice, centred at the top, fading out over its last half second (not while Menu is held:
// the pause menu keeps a picture of the screen).
gpr draw_save_notice(uint8_t* rdram, recomp_context* ctx, gpr g) {
    quit_tick(rdram);
    watch_tick(rdram);
    achievements_tick(rdram, ctx);
    fair_fix_stats(rdram, ctx);
    better_shops_tick(rdram);
    g = ach_toast(rdram, ctx, g);
    const gpr font = MEM_W(0, kFontPtr);
    const auto now = std::chrono::steady_clock::now();
    if (notice_text.empty() || now >= notice_until || !heap_pointer(font) || aidyn::input::start_held()) {
        return g;
    }
    const double left = std::chrono::duration<double>(notice_until - now).count();
    const uint32_t alpha = (uint32_t)std::lround(255.0 * std::min(1.0, left / 0.5));
    constexpr float kY = 9.0f, kScale = 0.8f;
    put_text(rdram, notice_text);
    const float width = (float)(int32_t)call_game(rdram, ctx, Font__GetWidthScaled, (uint32_t)font, (uint32_t)kLogText,
                                                  f32_bits(kScale));
    const float x = 160.0f - width / 2.0f;
    g = hud_begin(rdram, g, false);
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    const FontState st = font_save(rdram, font);
    call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
    for (int pass = 0; pass < 2; pass++) {
        GfxCommand a[2];
        const int d = pass == 0 ? 2 : 0;  // the shadow: the same text a little down and right
        const int off = -160 * 4;          // CENTER adds half the frame width to x
        gEXSetRectAlign(&a[0], G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, off + d, d, off + d, d);
        g = put_cmds(rdram, g, a, 2);
        MEM_W(0x1C, font) = (int32_t)(pass == 0 ? (alpha * 7 / 8) : (0xF2E6C800u | alpha));  // ivory, as the page words
        put_text(rdram, notice_text);
        MEM_W(0, kLogGfx) = (int32_t)g;
        call_game_stack(rdram, ctx, func_800AA6E8, (uint32_t)font, (uint32_t)kLogGfx, (uint32_t)kLogText,
                        (uint32_t)(int)x, { (uint32_t)(int)kY, f32_bits(kScale), f32_bits(kScale) });
        g = (gpr)(int32_t)MEM_W(0, kLogGfx);
    }
    font_restore(rdram, font, st);
    g = hud_end(rdram, g);
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}
// ---- Walk speed ----
// settings.json graphics.walk_speed (the ReChronicled page): the party walks faster in the field. The game moves a
// character by its walk or run cycle itself: ProcessPlayers steps each one's animation once per game tick, moving it
// forward by what the cycle's root moves that step (playerData's borg7, +0x2C), so there is no speed to change. For
// Alaron (the party's leader, the only one shown) walking or running (ani_type +0x14: 2, 3; not sneaking) while
// walking about (gameStateA 1, gameStateB 3), the loop is given walk_speed times the frame's ticks (fp), the fraction
// carried to the next frame, and fp put back after it: legs and pace quicken together, as with fast-forward, and
// nothing else does (the day's clock, the monsters, the camera). aidyn.us.toml hooks the loop's start (0x800130E4,
// s1 the character) and its end (0x8001343C).
namespace {
    int walk_saved_ticks = -1;
    double walk_carry = 0.0;
}

extern "C" void aidyn_walk_steps(uint8_t* rdram, recomp_context* ctx) {
    walk_saved_ticks = -1;
    const double k = aidyn::settings::graphics().walk_speed;
    const gpr p = ctx->r17;
    const int ticks = (int)(int32_t)ctx->r30, type = MEM_H(0x14, p);
    if (k <= 1.0 || ticks <= 0 || p != MEM_W(0, kPlayer) || MEM_BU(0, kTrekState) != 1 || MEM_BU(0, kGameStateB) != 3 ||
        (type != 2 && type != 3)) {
        return;
    }
    walk_carry += (k - 1.0) * ticks;
    const int extra = (int)walk_carry;
    walk_carry -= extra;
    if (extra > 0) {
        walk_saved_ticks = ticks;
        ctx->r30 = (gpr)(ticks + extra);
    }
}

extern "C" void aidyn_walk_steps_end(uint8_t* rdram, recomp_context* ctx) {
    if (walk_saved_ticks >= 0) {
        ctx->r30 = (gpr)walk_saved_ticks;
        walk_saved_ticks = -1;
    }
}

// ---- Controls page (#44) ----
// The pause menu's ReChronicled page ends with Controls (and the title menu has an entry for it): one row for each
// action with its two bindings (a key, a mouse button or a wheel step; src/main/controls.cpp), then the mouse's rows
// (look on / off, sensitivity, tilt inverted) and Reset to defaults. Up / down choose a row, left / right a binding of
// it, A listens for the next key, mouse button or wheel step (Esc, or the pad's B, cancels), Y clears the binding, B or
// Start goes back. A binding used by another action moves to this one. The words are the game's own letters printed
// at runtime (print_text): this is the first version, with plain text where the cards will have key pictures.
namespace {
    using aidyn::controls::Action;

    struct CtlRow {
        enum Kind { kBinding, kMouseLook, kSensitivity, kInvertTilt, kReset } kind;
        Action action = aidyn::controls::kA;
    };

    const std::vector<CtlRow>& ctl_rows() {
        static const std::vector<CtlRow> rows = [] {
            using namespace aidyn::controls;
            std::vector<CtlRow> r;
            for (Action a : { kMoveUp, kMoveDown, kMoveLeft, kMoveRight, kA, kB, kX, kY, kView, kLT, kRT, kLB, kRB, kMenu,
                              kCamUp, kCamDown, kCamLeft, kCamRight, kCamReset, kDpadUp, kDpadDown, kDpadLeft,
                              kDpadRight, kFastForward }) {
                r.push_back({ CtlRow::kBinding, a });
            }
            r.push_back({ CtlRow::kMouseLook });
            r.push_back({ CtlRow::kSensitivity });
            r.push_back({ CtlRow::kInvertTilt });
            r.push_back({ CtlRow::kReset });
            return r;
        }();
        return rows;
    }

    constexpr float kSensitivities[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f };
    constexpr int kCtlVisible = 12;
    constexpr float kCtlRowStep = 9.6f, kCtlScale = 0.6f;

    struct CtlPage {
        int row = 0, slot = 0, top = 0;
        bool listening = false;
        bool wait_release = false;  // after a binding: the click or key that made it isn't an action
        bool confirm_reset = false;
        std::string note;
        int note_frames = 0;
    } ctl;

    float ctl_width(uint8_t* rdram, recomp_context* ctx, gpr font, const std::string& text, float scale) {
        float w = 0.0f;
        for (char c : text) w += letter_advance(rdram, ctx, font, c, scale);
        return w;
    }

    // Text in the game's letters at (x, y) in the menu's units; right: x is its right end.
    gpr ctl_text(uint8_t* rdram, recomp_context* ctx, gpr g, const std::string& text, float x, float y, float scale,
                 uint32_t rgb, int alpha, bool right = false, bool centre = false) {
        const gpr font = MEM_W(0, kFontPtr);
        if (!heap_pointer(font) || text.empty()) return g;
        const FontState st = font_save(rdram, font);
        call_game(rdram, ctx, Font__SetFace, (uint32_t)font, kMainFont);
        MEM_W(0x1C, font) = (int32_t)(rgb << 8 | (uint32_t)std::clamp(alpha, 0, 255));
        const float w = ctl_width(rdram, ctx, font, text, scale);
        print_text(rdram, ctx, font, g, text, right ? x - w : centre ? x - w / 2.0f : x, y, scale);
        font_restore(rdram, font, st);
        return g;
    }

    gpr ctl_state(uint8_t* rdram, recomp_context* ctx, gpr g) {
        return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                       MEM_HU(kVres1, (gpr)gGfxManager));
    }

    void ctl_note(const std::string& text) {
        ctl.note = text;
        ctl.note_frames = 180;
    }

    std::string ctl_label(const CtlRow& r) {
        switch (r.kind) {
        case CtlRow::kBinding: return aidyn::controls::info(r.action).label;
        case CtlRow::kMouseLook: return "Mouse look";
        case CtlRow::kSensitivity: return "Mouse sensitivity";
        case CtlRow::kInvertTilt: return "Mouse tilt inverted";
        default: return "Reset to defaults";
        }
    }

    int ctl_sens_index() {
        const float s = aidyn::controls::mouse_sensitivity();
        int best = 0;
        for (int i = 0; i < (int)std::size(kSensitivities); i++) {
            if (std::fabs(kSensitivities[i] - s) < std::fabs(kSensitivities[best] - s)) best = i;
        }
        return best;
    }

    std::string ctl_value(const CtlRow& r) {
        char buf[16];
        switch (r.kind) {
        case CtlRow::kMouseLook: return aidyn::controls::mouse_look() ? "On" : "Off";
        case CtlRow::kSensitivity:
            std::snprintf(buf, sizeof buf, "%.2gx", aidyn::controls::mouse_sensitivity());
            return buf;
        case CtlRow::kInvertTilt: return aidyn::controls::mouse_invert_y() ? "On" : "Off";
        case CtlRow::kReset: return ctl.confirm_reset ? "A again to reset" : "";
        default: return "";
        }
    }

    // A / left / right on a row that isn't a binding.
    void ctl_change(const CtlRow& r, int step) {
        switch (r.kind) {
        case CtlRow::kMouseLook: aidyn::controls::set_mouse_look(!aidyn::controls::mouse_look()); break;
        case CtlRow::kSensitivity: {
            const int n = (int)std::size(kSensitivities);
            aidyn::controls::set_mouse_sensitivity(kSensitivities[std::clamp(ctl_sens_index() + step, 0, n - 1)]);
            break;
        }
        case CtlRow::kInvertTilt: aidyn::controls::set_mouse_invert_y(!aidyn::controls::mouse_invert_y()); break;
        default: break;
        }
    }

    void ctl_stop_listening() {
        ctl.listening = false;
        aidyn::controls::listen(false);
    }
}

namespace {
    // Where the page is shown: the pause menu's parchment (red-brown letters, the page fading the list under it) or
    // the title screen (ivory letters on the title panel).
    struct CtlHost {
        bool title;
        int fade;
        bool closing;
        int pulse;
        gpr list;  // the pause menu's list to fade (0: the title)
    };

    uint32_t ctl_highlight(const CtlHost& h) {
        if (!h.title) return rc_highlight();
        const int k = h.pulse % 40;
        const float t = (k < 20 ? k : 40 - k) / 20.0f;  // gold to white and back
        const uint32_t r = 0xE8 + (uint32_t)((0xFF - 0xE8) * t), g = 0xB8 + (uint32_t)((0xFF - 0xB8) * t), b = 0x50 + (uint32_t)((0xFF - 0x50) * t);
        return r << 16 | g << 8 | b;
    }

    std::string keys_of(aidyn::controls::Action a, bool all);  // "Keys on the prompts", below

    // One frame of the page: its input, its fade and its drawing. Returns true once it is closed (and input given back).
    bool controls_frame(uint8_t* rdram, recomp_context* ctx, gpr& g, CtlHost& h, uint32_t b, uint32_t pressed, uint32_t act) {
        const std::vector<CtlRow>& rows = ctl_rows();
        const int n = (int)rows.size();
        ctl.row = std::clamp(ctl.row, 0, n - 1);
        const bool ready = !h.closing && h.fade == 255;
        if (ready && !ctl.listening) {  // the pointer: its row, and on a binding row its column (first or second key)
            static uint32_t seen = 0;
            const float pleft = h.title ? 54.0f : kRowsLeft, pright = h.title ? 266.0f : kRowsRight;
            const float ptop = (h.title ? 52.0f : kRowsTop) - 2.0f, pcol1 = h.title ? 212.0f : 230.0f;
            const PointerPick p = pointer_pick(ptop, kCtlRowStep, std::min(kCtlVisible, n - ctl.top), pleft - 8.0f, pright + 4.0f, seen);
            if (p.row >= 0) {
                const int r = ctl.top + p.row;
                const int slot = (rows[r].kind == CtlRow::kBinding && p.x >= pcol1 - 4.0f) ? 1 : 0;
                if (r != ctl.row || slot != ctl.slot) {
                    ctl.row = r;
                    ctl.slot = slot;
                    ctl.confirm_reset = false;
                    rc_sound(rdram, ctx, 0x73B, 30);
                }
            }
        }
        if (ctl.wait_release) {
            if (b == 0) ctl.wait_release = false;
        } else if (ready && ctl.listening) {
            aidyn::controls::Input in;
            if (aidyn::input::pad_b_held() && (pressed & 0x4000)) {
                ctl_stop_listening();  // the pad's B
                ctl.wait_release = true;
            } else if (aidyn::controls::take_pressed(&in)) {
                ctl_stop_listening();
                ctl.wait_release = true;  // the key or click that bound it isn't an action
                if (aidyn::controls::is_escape(in)) {
                    ctl_note("Cancelled");
                } else if (aidyn::controls::reserved(in)) {
                    ctl_note(aidyn::controls::input_name(in) + " is the game's own key");
                } else {
                    const int from = aidyn::controls::set_binding(rows[ctl.row].action, ctl.slot, in);
                    ctl_note(aidyn::controls::input_name(in) +
                             (from >= 0 && from != rows[ctl.row].action
                                  ? " moved here from " + std::string(aidyn::controls::info((Action)from).label) : " bound"));
                    rc_sound(rdram, ctx, 0x740, 60);
                }
            }
        } else if (ready) {
            if (act & 0x0800) { ctl.row = (ctl.row + n - 1) % n; ctl.confirm_reset = false; rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0400) { ctl.row = (ctl.row + 1) % n; ctl.confirm_reset = false; rc_sound(rdram, ctx, 0x73B, 30); }
            const CtlRow& cur = rows[ctl.row];
            if (cur.kind == CtlRow::kBinding) {
                if (pressed & 0x0200) { ctl.slot = 0; rc_sound(rdram, ctx, 0x73B, 30); }
                if (pressed & 0x0100) { ctl.slot = 1; rc_sound(rdram, ctx, 0x73B, 30); }
                if (pressed & 0x8000) {
                    ctl.listening = true;
                    aidyn::controls::listen(true);
                    ctl_note("");
                }
                if (pressed & 0x0008) {  // Y: clear
                    if (!aidyn::controls::binding(cur.action, ctl.slot).empty()) {
                        aidyn::controls::clear_binding(cur.action, ctl.slot);
                        ctl_note("Cleared");
                        rc_sound(rdram, ctx, 0x740, 60);
                    }
                }
            } else if (cur.kind == CtlRow::kReset) {
                if (pressed & 0x8000) {
                    if (ctl.confirm_reset) {
                        aidyn::controls::reset_defaults();
                        ctl.confirm_reset = false;
                        ctl_note("Back to the defaults");
                    } else {
                        ctl.confirm_reset = true;
                    }
                    rc_sound(rdram, ctx, 0x740, 60);
                }
            } else {
                if (pressed & 0x8000) { ctl_change(cur, 1); rc_sound(rdram, ctx, 0x740, 60); }
                else if (act & 0x0300) { ctl_change(cur, (act & 0x0200) ? -1 : 1); rc_sound(rdram, ctx, 0x740, 60); }
            }
            if (pressed & (0x4000 | 0x1000)) {  // B, Start: back
                h.closing = true;
                rc_sound(rdram, ctx, 0x741, 60);
            }
        }
        if (ctl.row < ctl.top) ctl.top = ctl.row;
        if (ctl.row >= ctl.top + kCtlVisible) ctl.top = ctl.row - kCtlVisible + 1;
        if (ctl.note_frames > 0) ctl.note_frames--;

        h.fade = std::clamp(h.fade + (h.closing ? -60 : 60), 0, 255);
        if (h.list != 0) call_game(rdram, ctx, Utilities__SetAlpha, (uint32_t)h.list, (uint32_t)(255 - h.fade));
        if (h.closing && h.fade == 0) {
            if (b == 0) {
                if (ctl.listening) ctl_stop_listening();
                return true;
            }
            return false;
        }
        h.pulse++;
        // Where things go.
        const float left = h.title ? 54.0f : kRowsLeft, right = h.title ? 266.0f : kRowsRight, top = h.title ? 58.0f : kRowsTop;
        const float col0 = h.title ? 152.0f : 168.0f, col1 = h.title ? 212.0f : 230.0f;
        const uint32_t ink_normal = h.title ? 0xF2E6C8 : kMenuInk, ink_empty = h.title ? 0x8C8470 : 0xA09090;
        const float centre = (left + right) / 2.0f;
        if (h.title) {
            g = title_panel(rdram, ctx, g, 40.0f, 20.0f, 280.0f, 200.0f, (uint32_t)h.fade);  // room for 12 rows and the hint
        }
        g = ctl_state(rdram, ctx, g);
        g = ctl_text(rdram, ctx, g, "Controls", centre, h.title ? kPanelTitleY : top - 30.0f, h.title ? 0.9f : 1.0f,
                     h.title ? 0xE8C060 : kMenuInk, h.fade, false, true);
        for (int i = 0; i < kCtlVisible && ctl.top + i < n; i++) {
            const int k = ctl.top + i;
            const CtlRow& r = rows[k];
            const float y = top - 2.0f + kCtlRowStep * i;
            const bool sel = k == ctl.row;
            const uint32_t ink = sel ? ctl_highlight(h) : ink_normal;
            g = ctl_text(rdram, ctx, g, ctl_label(r), left, y, kCtlScale, ink, h.fade);
            if (r.kind == CtlRow::kBinding) {
                for (int sl = 0; sl < aidyn::controls::kSlots; sl++) {
                    const aidyn::controls::Input in = aidyn::controls::binding(r.action, sl);
                    const bool here = sel && sl == ctl.slot;
                    const std::string text = (here && ctl.listening) ? "press a key" : aidyn::controls::input_name(in);
                    g = ctl_text(rdram, ctx, g, text, sl == 0 ? col0 : col1, y, kCtlScale,
                                 here ? ctl_highlight(h) : (in.empty() ? ink_empty : ink_normal), h.fade);
                }
            } else {
                g = ctl_text(rdram, ctx, g, ctl_value(r), right, y, kCtlScale, ink, h.fade, true);
            }
        }
        const float hint_y = top - 2.0f + kCtlRowStep * kCtlVisible + 3.0f;
        // The keys that do it while the keyboard or mouse is in use (the pad's buttons otherwise).
        const bool kb = aidyn::input::keyboard_last();
        const std::string a_key = kb ? keys_of(aidyn::controls::kA, false) : "A", y_key = kb ? keys_of(aidyn::controls::kY, false) : "Y",
                          b_key = kb ? keys_of(aidyn::controls::kB, false) : "B";
        const std::string hint = ctl.listening ? "Press a key, a mouse button or the wheel (Esc: cancel)"
                                 : ctl.note_frames > 0 && !ctl.note.empty() ? ctl.note
                                 : rows[ctl.row].kind == CtlRow::kBinding ? a_key + ": bind   " + y_key + ": clear   " + b_key + ": back"
                                                                          : a_key + " / left / right: change   " + b_key + ": back";
        g = ctl_text(rdram, ctx, g, hint, centre, hint_y, kCtlScale, ink_normal, h.fade, false, true);
        g = ctl_text(rdram, ctx, g, std::to_string(ctl.row + 1) + " / " + std::to_string(n), right, h.title ? kPanelTitleY + 2.0f : top - 30.0f,
                     0.55f, ink_normal, h.fade, true);
        return false;
    }

    // The title screen's host.
    struct TitleControls {
        bool open = false, closing = false;
        int fade = 0, pulse = 0, repeat = 0;
        uint32_t held = 0xFFFFFFFF;
    } tc;
}

void controls_open(uint8_t* rdram, recomp_context* ctx) {
    ctl = CtlPage{};
    rc.kind = kRcControls;
    rc.held = 0xFFFFFFFF;  // the A that opened it is still down
    ctl.wait_release = true;
}

gpr controls_page(uint8_t* rdram, recomp_context* ctx, gpr g, uint32_t b, uint32_t pressed, uint32_t act) {
    CtlHost h{ false, rc.fade, rc.closing, rc.pulse, rc.list };
    const bool done = controls_frame(rdram, ctx, g, h, b, pressed, act);
    rc.fade = h.fade;
    rc.closing = h.closing;
    rc.pulse = h.pulse;
    if (done) rc_end();
    return g;
}

// The title menu's Controls entry (aidyn_title_choice): the page over the title screen.
void title_controls_open(uint8_t* rdram, recomp_context* ctx) {
    ctl = CtlPage{};
    ctl.wait_release = true;
    tc = TitleControls{};
    tc.open = true;
    aidyn::input::set_capture(true);
}

gpr title_controls_page(uint8_t* rdram, recomp_context* ctx, gpr g) {
    if (!tc.open) return g;
    if (!heap_pointer(MEM_W(0, kTitleScreen))) {  // the title is gone (a version switch, a game starting)
        if (ctl.listening) ctl_stop_listening();
        tc = TitleControls{};
        aidyn::input::set_capture(false);
        return g;
    }
    uint32_t b = 0;
    float sx = 0.0f, sy = 0.0f;
    aidyn::input::captured(&b, &sx, &sy);
    if (sy > 0.5f) b |= 0x0800;
    if (sy < -0.5f) b |= 0x0400;
    if (sx < -0.5f) b |= 0x0200;
    if (sx > 0.5f) b |= 0x0100;
    const uint32_t pressed = tc.held == 0xFFFFFFFF ? 0 : (b & ~tc.held);
    tc.held = (tc.held == 0xFFFFFFFF && (b & 0x8000)) ? 0xFFFFFFFF : b;
    const uint32_t dirs = b & 0x0F00;
    tc.repeat = dirs != 0 ? tc.repeat + 1 : 0;
    const uint32_t act = pressed | ((tc.repeat > 24 && tc.repeat % 6 == 0) ? dirs : 0);
    CtlHost h{ true, tc.fade, tc.closing, tc.pulse, 0 };
    const bool done = controls_frame(rdram, ctx, g, h, b, pressed, act);
    tc.fade = h.fade;
    tc.closing = h.closing;
    tc.pulse = h.pulse;
    if (done) {
        tc = TitleControls{};
        aidyn::input::set_capture(false);
    }
    return g;
}

// ---- The pointer over the game's lists (#44) ----
// The game's scroll lists (WidgetScrollMenu: the pause menu's Menu page, the title menu, the Options pages' lists...):
// when the mouse pointer has moved over one of a visible list's entries, that entry becomes the highlighted one (WSMSub
// +0x1E), as Up and Down would make it; a click is A as usual and acts on it. Entries are placed by the list's own Tick
// (posX/posY, their width and height: BaseWidget +0x60, +0x62, +0x74, +0x72) in the 320 x 240 screen, as the pointer is.
extern "C" void aidyn_list_pointer(uint8_t* rdram, recomp_context* ctx) {
    const gpr list = ctx->r4;
    if (!heap_pointer(list) || MEM_BU(0x5B, list) == 0) return;  // not shown (faded out under one of our pages)
    const gpr sub = MEM_W(0x40, list);
    if (!heap_pointer(sub)) return;
    float x = 0.0f, y = 0.0f;
    uint32_t moves = 0;
    if (!aidyn::controls::pointer(&x, &y, &moves)) return;
    static std::unordered_map<uint32_t, uint32_t> seen;  // by list: the moves it has followed
    uint32_t& last = seen[(uint32_t)list];
    if (last == moves) return;
    last = moves;
    const gpr items = MEM_W(0x18, sub);
    const int n = MEM_HU(0x20, sub);
    if (std::getenv("AIDYN_LIST_LOG")) {
        fprintf(stdout, "List %08x: pointer %.1f,%.1f, %d entries, highlight %u\n", (uint32_t)list, x, y, n, MEM_HU(0x1E, sub));
        for (int i = 0; heap_pointer(items) && i < n; i++) {
            const gpr e = MEM_W(4 * i, items);
            if (heap_pointer(e)) fprintf(stdout, "  %d: %d,%d %ux%u\n", i, (int16_t)MEM_HU(kWidgetPosX, e),
                                         (int16_t)MEM_HU(kWidgetPosX + 2, e), MEM_HU(0x74, e), MEM_HU(0x72, e));
        }
        fflush(stdout);
    }
    for (int i = 0; heap_pointer(items) && i < n; i++) {
        const gpr e = MEM_W(4 * i, items);
        if (!heap_pointer(e)) continue;
        const float ex = (int16_t)MEM_HU(kWidgetPosX, e), ey = (int16_t)MEM_HU(kWidgetPosX + 2, e);
        const float ew = MEM_HU(0x74, e), eh = MEM_HU(0x72, e);
        // half the gap to the next entry above and below it (the title menu's are 8 apart), a little to the sides
        if (eh == 0 || y < ey - 4.0f || y >= ey + eh + 4.0f) continue;
        if (ew > 0 && (x < ex - 16.0f || x > ex + ew + 16.0f)) continue;
        if (MEM_HU(0x1E, sub) != i) {
            MEM_H(0x1E, sub) = (int16_t)i;
            rc_sound(rdram, ctx, 0x73B, 30);
        }
        return;
    }
}

// ---- Achievements page and toast (#42) ----
// The toast: at the top right while walking about, 5 seconds a toast, one at a time: the turn bar's left cap with its
// plate (the achievement's medal in it: Codex's, one per group, ach_medal_<kind>), its middle and right cap
// (title_panel.py's pieces, as the title menu's panel), "Achievement unlocked" in gold over the title in ivory. Not
// drawn while Menu is held (the pause menu keeps a picture of the screen).
// The list: the ReChronicled page's Achievements row (this version's) and the title menu's Achievements (all four
// versions: a progress bar each, then the chosen one's list) draw the same list: grouped (Story, Side quests, Bosses,
// Companions, Exploration, Growth, Feats), a medal and the title with the day it was earned, or the locked medal and ???
// (hidden until earned), a header with each group's count.
bool ach_shown() { return ach_enabled(); }

namespace {
    // 0 Original, 1 Aidyn Patch, 2 Aidyn Plus, 3 Action Edition: the version being played.
    int running_version_index() {
        switch (aidyn::settings::game().rom_patch) {
            case aidyn::settings::RomPatch::AidynPatch: return 1;
            case aidyn::settings::RomPatch::AidynPlus: return 2;
            case aidyn::settings::RomPatch::ActionEdition: return 3;
            default: return 0;
        }
    }

    // A medal's picture: the group's, in the version's metal (bronze, silver, gold, rose copper); a group whose medal
    // isn't painted yet (side quests) borrows the story's.
    std::string ach_medal_name(const std::string& kind, int version) {
        static const char* kMetals[4] = { "bronze", "silver", "gold", "rose" };
        std::string n = "ach_medal_" + kind + "_" + kMetals[version & 3];
        if (branding_index(n.c_str()) < 0) n = std::string("ach_medal_story_") + kMetals[version & 3];
        return n;
    }
}

gpr ach_toast(uint8_t* rdram, recomp_context* ctx, gpr g) {
    if (!ach.toast_on || ach.toasts.empty()) return g;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - ach.toast_start).count();
    constexpr double kShow = 5.0;
    if (t >= kShow) {
        ach.toasts.pop_front();
        ach.toast_on = false;
        return g;
    }
    const gpr font = MEM_W(0, kFontPtr);
    if (MEM_BU(0, kTrekState) != 1 || aidyn::input::start_held() || !heap_pointer(font)) return g;
    const uint32_t alpha = (uint32_t)std::lround(255.0 * std::clamp(std::min(t / 0.25, (kShow - t) / 0.5), 0.0, 1.0));
    const auto& toast = ach.toasts.front();
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    constexpr float kH = 26.0f, kHead = 0.5f, kTitle = 0.72f;
    const float k = kH / 30.0f, lw = 38.0f * k, rw = 25.0f * k;
    const float text_w = std::max(ctl_width(rdram, ctx, font, toast.head, kHead), ctl_width(rdram, ctx, font, toast.title, kTitle));
    const float w = std::max(130.0f, lw + text_w + 6.0f + rw);
    // under the FPS counter when it's shown (top right too)
    const float x1 = 314.0f, x0 = x1 - w, y0 = aidyn::settings::graphics().fps_counter ? 20.0f : 5.0f;
    // As the controls card and the FPS counter are drawn here: the pieces in the prompts' picture state, then the text
    // in the 2D state, its rectangles aligned to the picture's right edge again (borg8DlistInit resets that).
    copy_branding(rdram);
    const bool wide = hud_wide();
    g = hud_begin(rdram, g, wide);
    g = prompt_state(rdram, g, (uint8_t)alpha);
    g = panel_bar(rdram, g, "tp_cap_l_plate", x0, x1, y0, kH, hs, vs);
    g = panel_piece(rdram, g, ach_medal_name(toast.medal, running_version_index()).c_str(), x0 + 10.5f * k, y0 + 5.0f * k, 20.0f * k, 20.0f * k, hs, vs);
    g = (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                MEM_HU(kVres1, (gpr)gGfxManager));
    GfxCommand a[2];
    gEXSetRectAlign(&a[0], wide ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT, wide ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT,
                    wide ? -320 * 4 : 0, 0, wide ? -320 * 4 : 0, 0);
    g = put_cmds(rdram, g, a, 2);
    g = ctl_text(rdram, ctx, g, toast.head, x0 + lw + 2.0f, y0 + 6.5f, kHead, 0xE8C060, (int)alpha);
    g = ctl_text(rdram, ctx, g, toast.title, x0 + lw + 2.0f, y0 + 12.5f, kTitle, 0xF2E6C8, (int)alpha);
    g = hud_end(rdram, g);
    return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager),
                                   MEM_HU(kVres1, (gpr)gGfxManager));
}

namespace {
    // Each version keeps its own file (achievements-<its pak's name>.txt: aidyn.mpk, aidyn-patch.mpk, ...), all read for
    // the title screen's overview.
    constexpr const char* kAchStems[4] = { "aidyn", "aidyn-patch", "aidyn-plus", "action-edition" };
    constexpr const char* kAchVersions[4] = { "Original", "Aidyn Patch", "Aidyn Plus", "Action Edition" };
    using AchMap = std::map<std::string, std::string>;  // id -> when

    AchMap ach_read(const std::string& stem) {
        AchMap out;
        std::ifstream in(save_dir / ("achievements-" + stem + ".txt"));
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("unlocked ", 0) != 0) continue;
            const std::string rest = line.substr(9);
            const size_t sp = rest.find(' ');
            out[rest.substr(0, sp)] = sp == std::string::npos ? "" : rest.substr(sp + 1);
        }
        return out;
    }

    // A hidden achievement shows nothing until it is earned (no ??? row, not in the group's header or the totals). Only
    // ones that a version can't earn are: Fair Trader in Action Edition (spitzdano: its shops are its own fix, so Fair
    // play's Fair trade isn't offered there) and Old Stock in the mods (Better shops is the Original's only). The side quests and optional bosses show as ??? so they can be counted.
    bool ach_hidden(const Achievement& a, int version) {
        return (version == 3 && std::strcmp(a.id, "fair_trade") == 0) ||
               (version != 0 && std::strcmp(a.id, "fair_shops") == 0);  // Better shops is the Original's only
    }

    // What a version has earned, and the total it is out of (every achievement but the hidden ones not yet earned).
    struct AchCount { int earned, total; };
    AchCount ach_count(const AchMap& m, int version) {
        AchCount c{ 0, 0 };
        for (const Achievement& a : achievements()) {
            const bool got = m.count(a.id) != 0;
            c.earned += got ? 1 : 0;
            c.total += (got || !ach_hidden(a, version)) ? 1 : 0;
        }
        return c;
    }

    // The list's rows: each group's header (a == nullptr) and its achievements.
    struct AchRow { const Achievement* a; int cat, earned, total; };
    std::vector<AchRow> ach_rows(const AchMap& unlocked, int version) {
        std::vector<AchRow> rows;
        for (int c = 0; c < kCatCount; c++) {
            int total = 0, earned = 0;
            for (const Achievement& a : achievements()) {
                if (ach_cat(a.id) != c) continue;
                const bool got = unlocked.count(a.id) != 0;
                earned += got ? 1 : 0;
                total += (got || !ach_hidden(a, version)) ? 1 : 0;
            }
            if (total == 0) continue;  // a hidden group with nothing found yet isn't there
            rows.push_back({ nullptr, c, earned, total });
            for (const Achievement& a : achievements()) {
                if (ach_cat(a.id) == c && (unlocked.count(a.id) || !ach_hidden(a, version))) rows.push_back({ &a, c, 0, 0 });
            }
        }
        return rows;
    }

    // A bar of `frac` (0..1): a dark track, the fill over it (the tinted ach_bar picture).
    gpr ach_bar(uint8_t* rdram, gpr g, float x, float y, float w, float h, float frac, uint32_t fill, uint32_t track, int fade) {
        const int i = branding_index("ach_bar");
        if (i < 0) return g;
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        const auto& img = aidyn::branding::kImages[i];
        g = tinted_image_state(rdram, g, track << 8 | (uint32_t)(fade * 7 / 8));
        g = draw_rgba16(rdram, g, i, x * hs, y * vs, w / img.w * hs, h / img.h * vs);
        if (frac > 0.0f) {
            g = tinted_image_state(rdram, g, fill << 8 | (uint32_t)fade);
            g = draw_rgba16(rdram, g, i, x * hs, y * vs, std::max(1.0f, w * std::min(frac, 1.0f)) / img.w * hs, h / img.h * vs);
        }
        return g;
    }

    struct AchInk { uint32_t head, earned, locked; };

    // `visible` rows of the list from `top`, the highlighted `row` pulsing in `highlight`, from y0 in rows kCtlRowStep apart.
    gpr ach_draw_list(uint8_t* rdram, recomp_context* ctx, gpr g, const std::vector<AchRow>& rows, const AchMap& unlocked,
                      int version, int row, int top, int visible, float left, float right, float y0, const AchInk& ink,
                      uint32_t highlight, int fade) {
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        const int n = (int)rows.size();
        constexpr float kMedal = 9.0f;
        copy_branding(rdram);
        g = prompt_state(rdram, g, (uint8_t)fade);  // the medals
        for (int i = 0; i < visible && top + i < n; i++) {
            const AchRow& r = rows[top + i];
            if (r.a == nullptr) continue;
            const std::string name = unlocked.count(r.a->id) ? ach_medal_name(kCatMedals[r.cat], version) : "ach_medal_locked";
            const int m = branding_index(name.c_str());
            if (m >= 0) g = draw_rgba16(rdram, g, m, left * hs, (y0 + kCtlRowStep * i - 0.8f) * vs, kMedal / 20.0f * hs, kMedal / 20.0f * vs);
        }
        g = ctl_state(rdram, ctx, g);
        for (int i = 0; i < visible && top + i < n; i++) {
            const AchRow& r = rows[top + i];
            const float y = y0 + kCtlRowStep * i;
            if (r.a == nullptr) {
                g = ctl_text(rdram, ctx, g, kCatNames[r.cat], left, y, kCtlScale, top + i == row ? highlight : ink.head, fade);
                g = ctl_text(rdram, ctx, g, std::to_string(r.earned) + " / " + std::to_string(r.total), right, y, 0.5f,
                             ink.head, fade, true);
                continue;
            }
            const auto it = unlocked.find(r.a->id);
            const bool got = it != unlocked.end();
            g = ctl_text(rdram, ctx, g, got || !ach_secret(r.cat) ? r.a->title : "???", left + 12.0f, y, kCtlScale,
                         top + i == row ? highlight : got ? ink.earned : ink.locked, fade);
            if (got && it->second.size() >= 10) {
                g = ctl_text(rdram, ctx, g, it->second.substr(0, 10), right, y, 0.5f, ink.locked, fade, true);
            }
        }
        return g;
    }

    int ach_row_limit(int row, int n) { return std::clamp(row, 0, std::max(0, n - 1)); }

    // Under the list: how the highlighted one is earned (a secret one not yet earned: that it shows once it is), in up
    // to two lines of `width`, centred on `centre` from y.
    gpr ach_describe(uint8_t* rdram, recomp_context* ctx, gpr g, const std::vector<AchRow>& rows, const AchMap& unlocked,
                     int row, float centre, float y, float width, uint32_t ink, int fade) {
        if (row < 0 || row >= (int)rows.size()) return g;
        const AchRow& r = rows[row];
        std::string text;
        if (r.a == nullptr) {
            text = ach_secret(r.cat) ? "Each shows what it is once you have earned it." : "";
        } else if (!unlocked.count(r.a->id) && ach_secret(r.cat)) {
            text = "Not earned yet: it shows what it is once you have.";
        } else {
            text = ach_how(r.a->id);
        }
        if (text.empty()) return g;
        constexpr float kScale = 0.5f;
        const gpr font = MEM_W(0, kFontPtr);
        std::vector<std::string> lines(1);
        std::stringstream words(text);
        std::string w;
        while (words >> w) {
            const std::string tryline = lines.back().empty() ? w : lines.back() + " " + w;
            if (!lines.back().empty() && ctl_width(rdram, ctx, font, tryline, kScale) > width * 0.86f && lines.size() < 2) lines.push_back(w);
            else lines.back() = tryline;
        }
        for (size_t k = 0; k < lines.size(); k++) {
            g = ctl_text(rdram, ctx, g, lines[k], centre, y + 8.0f * k, kScale, ink, fade, false, true);
        }
        return g;
    }
}

namespace {
    int ach_row = 0, ach_top = 0;
}

void ach_open(uint8_t* rdram, recomp_context* ctx) {
    ach_load();
    rc.kind = kRcAchievements;
    rc.held = 0xFFFFFFFF;  // the A that opened it is still down
    ach_row = 0;
    ach_top = 0;
}

// The pause menu's page: this version's list.
gpr achievements_page(uint8_t* rdram, recomp_context* ctx, gpr g, uint32_t b, uint32_t pressed, uint32_t act) {
    const std::vector<AchRow> rows = ach_rows(ach.unlocked, running_version_index());
    const int n = (int)rows.size();
    constexpr int kVisible = 12;  // and two lines of the highlighted one's description under them
    if (!rc.closing && rc.fade == 255) {
        static uint32_t seen = 0;
        const PointerPick p = pointer_pick(kRowsTop - 2.0f, kCtlRowStep, std::min(kVisible, n - ach_top), kRowsLeft - 8.0f, kRowsRight + 4.0f, seen);
        if (p.row >= 0 && ach_top + p.row != ach_row) { ach_row = ach_top + p.row; rc_sound(rdram, ctx, 0x73B, 30); }
    }
    if (!rc.closing && rc.fade == 255) {
        if (act & 0x0800) { ach_row = (ach_row + n - 1) % n; rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0400) { ach_row = (ach_row + 1) % n; rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0200) { ach_row = std::max(0, ach_row - kVisible); rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0100) { ach_row = std::min(n - 1, ach_row + kVisible); rc_sound(rdram, ctx, 0x73B, 30); }
        if (pressed & (0x4000 | 0x1000)) {
            rc.closing = true;
            rc_sound(rdram, ctx, 0x741, 60);
        }
    }
    ach_row = ach_row_limit(ach_row, n);
    if (ach_row < ach_top) ach_top = ach_row;
    if (ach_row >= ach_top + kVisible) ach_top = ach_row - kVisible + 1;
    rc.fade = std::clamp(rc.fade + (rc.closing ? -60 : 60), 0, 255);
    call_game(rdram, ctx, Utilities__SetAlpha, (uint32_t)rc.list, (uint32_t)(255 - rc.fade));
    if (rc.closing && rc.fade == 0) {
        if (b == 0) rc_end();
        return g;
    }
    rc.pulse++;
    const float left = kRowsLeft, right = kRowsRight, top = kRowsTop, centre = (left + right) / 2.0f;
    {  // the title, a picture in halves like the ReChronicled page's (4 KB of TMEM each)
        copy_branding(rdram);
        const int tl = branding_index("ach_title_l"), tr = branding_index("ach_title_r");
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        if (tl >= 0 && tr >= 0) {
            const float w = aidyn::branding::kImages[tl].w;
            g = prompt_state(rdram, g, (uint8_t)rc.fade);
            g = draw_rgba16(rdram, g, tl, (centre - w) * hs, (top - 31.0f) * vs, hs, vs);
            g = draw_rgba16(rdram, g, tr, centre * hs, (top - 31.0f) * vs, hs, vs);
        }
    }
    const AchCount counts = ach_count(ach.unlocked, running_version_index());
    const int earned = counts.earned, total = counts.total;
    g = ach_bar(rdram, g, left, top - 6.0f, right - left, 3.0f, (float)earned / (float)total, 0x9A5A2A, 0xC8B494, rc.fade);
    g = ctl_state(rdram, ctx, g);
    g = ctl_text(rdram, ctx, g, std::to_string(earned) + " / " + std::to_string(total), right, top - 14.0f, 0.55f, kMenuInk,
                 rc.fade, true);
    g = ach_draw_list(rdram, ctx, g, rows, ach.unlocked, running_version_index(), ach_row, ach_top, kVisible, left, right, top - 2.0f,
                      { 0x442A22, kMenuInk, 0xA09090 }, rc_highlight(), rc.fade);
    g = ach_describe(rdram, ctx, g, rows, ach.unlocked, ach_row, centre, top - 2.0f + kCtlRowStep * kVisible + 2.0f, right - left,
                     0x442A22, rc.fade);
    const bool kb = aidyn::input::keyboard_last();
    const std::string back = kb ? keys_of(aidyn::controls::kB, false) : "B";
    g = ctl_text(rdram, ctx, g, "Up / down: scroll   " + back + ": back", centre, top - 2.0f + kCtlRowStep * (kVisible + 2) + 3.0f,
                 kCtlScale, kMenuInk, rc.fade, false, true);
    return g;
}

// The title menu's Achievements: a panel over the title screen, all four versions with their progress, A opens one's list.
namespace {
    struct TitleAch {
        bool open = false, closing = false;
        int fade = 0, pulse = 0, repeat = 0, level = 0, sel = 0, row = 0, top = 0;
        uint32_t held = 0xFFFFFFFF;
        AchMap views[4];
    } ta;

}

void title_ach_open(uint8_t* rdram, recomp_context* ctx) {
    ta = TitleAch{};
    ta.open = true;
    for (int i = 0; i < 4; i++) ta.views[i] = ach_read(kAchStems[i]);
    ta.sel = running_version_index();
    aidyn::input::set_capture(true);
}

gpr title_ach_page(uint8_t* rdram, recomp_context* ctx, gpr g) {
    if (!ta.open) return g;
    if (!heap_pointer(MEM_W(0, kTitleScreen))) {  // the title is gone (a version switch, a game starting)
        ta = TitleAch{};
        aidyn::input::set_capture(false);
        return g;
    }
    uint32_t b = 0;
    float sx = 0.0f, sy = 0.0f;
    aidyn::input::captured(&b, &sx, &sy);
    if (sy > 0.5f) b |= 0x0800;
    if (sy < -0.5f) b |= 0x0400;
    if (sx < -0.5f) b |= 0x0200;
    if (sx > 0.5f) b |= 0x0100;
    const uint32_t pressed = ta.held == 0xFFFFFFFF ? 0 : (b & ~ta.held);
    ta.held = (ta.held == 0xFFFFFFFF && (b & 0x8000)) ? 0xFFFFFFFF : b;
    const uint32_t dirs = b & 0x0F00;
    ta.repeat = dirs != 0 ? ta.repeat + 1 : 0;
    const uint32_t act = pressed | ((ta.repeat > 24 && ta.repeat % 6 == 0) ? dirs : 0);
    const AchMap& view = ta.views[ta.sel];
    const std::vector<AchRow> rows = ach_rows(view, ta.sel);
    const int n = (int)rows.size();
    constexpr int kVisible = 7;  // and two lines of the highlighted one's description under them
    constexpr float kLeft = 58.0f, kRight = 262.0f, kBodyTop = 59.0f, kVersionStep = 27.0f;
    if (!ta.closing && ta.fade == 255) {
        static uint32_t seen = 0;
        if (ta.level == 0) {
            const PointerPick p = pointer_pick(kBodyTop - 3.0f, kVersionStep, 4, 44.0f, 276.0f, seen);
            if (p.row >= 0 && p.row != ta.sel) { ta.sel = p.row; rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0800) { ta.sel = (ta.sel + 3) % 4; rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0400) { ta.sel = (ta.sel + 1) % 4; rc_sound(rdram, ctx, 0x73B, 30); }
            if (pressed & 0x8000) { ta.level = 1; ta.row = ta.top = 0; rc_sound(rdram, ctx, 0x740, 60); }
            else if (pressed & (0x4000 | 0x1000)) { ta.closing = true; rc_sound(rdram, ctx, 0x741, 60); }
        } else {
            const PointerPick p = pointer_pick(kBodyTop + 14.0f - 2.0f, kCtlRowStep, std::min(kVisible, n - ta.top), 44.0f, 276.0f, seen);
            if (p.row >= 0 && ta.top + p.row != ta.row) { ta.row = ta.top + p.row; rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0800) { ta.row = (ta.row + n - 1) % n; rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0400) { ta.row = (ta.row + 1) % n; rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0200) { ta.row = std::max(0, ta.row - kVisible); rc_sound(rdram, ctx, 0x73B, 30); }
            if (act & 0x0100) { ta.row = std::min(n - 1, ta.row + kVisible); rc_sound(rdram, ctx, 0x73B, 30); }
            if (pressed & (0x4000 | 0x1000)) { ta.level = 0; rc_sound(rdram, ctx, 0x741, 60); }
        }
    }
    ta.row = ach_row_limit(ta.row, n);
    if (ta.row < ta.top) ta.top = ta.row;
    if (ta.row >= ta.top + kVisible) ta.top = ta.row - kVisible + 1;
    ta.fade = std::clamp(ta.fade + (ta.closing ? -60 : 60), 0, 255);
    if (ta.closing && ta.fade == 0 && b == 0) {
        ta = TitleAch{};
        aidyn::input::set_capture(false);
        return g;
    }
    ta.pulse++;
    const int fade = ta.fade;
    const CtlHost h{ true, fade, ta.closing, ta.pulse, 0 };
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    const float centre = 160.0f;
    g = title_panel(rdram, ctx, g, 40.0f, 20.0f, 280.0f, 194.0f, (uint32_t)fade);
    copy_branding(rdram);
    {  // the title picture on the top bar, as the Versions panel's
        const int tl = branding_index("ach_title_l"), tr = branding_index("ach_title_r");
        if (tl >= 0 && tr >= 0) {
            const float w = aidyn::branding::kImages[tl].w;
            g = prompt_state(rdram, g, (uint8_t)fade);
            g = draw_rgba16(rdram, g, tl, (centre - w) * hs, 28.5f * vs, hs, vs);
            g = draw_rgba16(rdram, g, tr, centre * hs, 28.5f * vs, hs, vs);
        }
    }
    if (ta.level == 0) {
        for (int i = 0; i < 4; i++) {
            const float y = kBodyTop + kVersionStep * i;
            const int earned = ach_count(ta.views[i], i).earned, total = ach_count(ta.views[i], i).total;
            if (i == ta.sel) {  // the title's copper marker
                const int mark = branding_index("ver_mark");
                if (mark >= 0) {
                    g = prompt_state(rdram, g, (uint8_t)fade);
                    g = draw_rgba16(rdram, g, mark, 47.0f * hs, (y - 1.0f) * vs, hs, vs);
                }
            }
            g = ach_bar(rdram, g, kLeft + 4.0f, y + 12.0f, kRight - kLeft - 4.0f, 4.0f, (float)earned / (float)total, 0xE8B850, 0x20282E, fade);
        }
        g = ctl_state(rdram, ctx, g);
        for (int i = 0; i < 4; i++) {
            const float y = kBodyTop + kVersionStep * i;
            const int earned = ach_count(ta.views[i], i).earned, total = ach_count(ta.views[i], i).total;
            const uint32_t ink = i == ta.sel ? ctl_highlight(h) : 0xF2E6C8;
            g = ctl_text(rdram, ctx, g, kAchVersions[i], kLeft + 4.0f, y, 0.8f, ink, fade);
            if (i == running_version_index()) {
                g = ctl_text(rdram, ctx, g, "playing", kLeft + 12.0f + ctl_width(rdram, ctx, MEM_W(0, kFontPtr), kAchVersions[i], 0.8f),
                             y + 2.0f, 0.5f, 0xE8C060, fade);
            }
            g = ctl_text(rdram, ctx, g, std::to_string(earned) + " / " + std::to_string(total), kRight, y, 0.8f, 0xE8C060, fade, true);
        }
    } else {
        const int earned = ach_count(view, ta.sel).earned, total = ach_count(view, ta.sel).total;
        g = ach_bar(rdram, g, kLeft, kBodyTop + 10.0f, kRight - kLeft, 3.0f, (float)earned / (float)total, 0xE8B850, 0x20282E, fade);
        g = ctl_state(rdram, ctx, g);
        g = ctl_text(rdram, ctx, g, kAchVersions[ta.sel], kLeft, kBodyTop - 1.0f, 0.7f, 0xE8C060, fade);
        g = ctl_text(rdram, ctx, g, std::to_string(earned) + " / " + std::to_string(total), kRight, kBodyTop - 1.0f, 0.7f, 0xE8C060, fade, true);
        g = ach_draw_list(rdram, ctx, g, rows, view, ta.sel, ta.row, ta.top, kVisible, kLeft, kRight, kBodyTop + 14.0f,
                          { 0xE8C060, 0xF2E6C8, 0x8C8470 }, ctl_highlight(h), fade);
        g = ach_describe(rdram, ctx, g, rows, view, ta.row, centre, kBodyTop + 14.0f + kCtlRowStep * kVisible + 3.0f,
                         kRight - kLeft, 0xC8BFA8, fade);
    }
    const bool kb = aidyn::input::keyboard_last();
    const std::string a_key = kb ? keys_of(aidyn::controls::kA, false) : "A", b_key = kb ? keys_of(aidyn::controls::kB, false) : "B";
    g = ctl_text(rdram, ctx, g, ta.level == 0 ? a_key + ": open   " + b_key + ": back" : "Up / down: scroll   " + b_key + ": back",
                 centre, 167.0f, 0.55f, 0xC8BFA8, fade, false, true);  // above the closing bar's rail
    return g;
}

// ---- The versions list (title screen) ----
// Y on the title card opens a panel listing the versions: Original first, then the mods (Aidyn Patch, Aidyn Plus, Action
// Edition). Up and Down choose (the pad's D-pad or stick, or the keyboard's movement keys), A plays the chosen one (the
// game restarts into it: aidyn::switch_version; the running one just closes the list), B, Start or Y again close it.
// One whose ROM isn't stored says "not found" and can't be chosen. The game gets no input meanwhile
// (aidyn::input::set_capture), as under the Controls page. draw_title_branding calls the panel each frame the card is
// drawn; if that stops (the card goes away), the watchdog below lets the input go.
namespace {
    struct VersionsPanel {
        bool open = false, closing = false, locked = true;  // locked: the press that opened it is still down
        bool y_was = true;
        int sel = 0, fade = 0, pulse = 0, repeat = 0;
        uint32_t held = 0;
        std::chrono::steady_clock::time_point last{};
    } vers;

    void title_versions_open() {
        if (vers.open) return;
        vers = VersionsPanel{};
        vers.open = true;
        vers.sel = running_version();
        vers.last = std::chrono::steady_clock::now();
        aidyn::input::set_capture(true);
    }

    void versions_close_now() {
        vers = VersionsPanel{};
        aidyn::input::set_capture(false);
    }

}

gpr title_versions_panel(uint8_t* rdram, gpr g) {
    recomp_context* ctx = frame_ctx;
    if (!vers.open || ctx == nullptr) return g;
    const auto now = std::chrono::steady_clock::now();
    const bool step = now - vers.last >= std::chrono::milliseconds(8);  // the card may draw more than once a frame
    if (step) {
        vers.last = now;
        uint32_t b = 0;
        float sx = 0.0f, sy = 0.0f;
        aidyn::input::captured(&b, &sx, &sy);
        if (sy > 0.5f) b |= 0x0800;
        if (sy < -0.5f) b |= 0x0400;
        const bool y = aidyn::input::c_up_held();
        const bool y_pressed = y && !vers.y_was;
        vers.y_was = y;
        if (vers.locked && !y && !(b & 0x8000)) vers.locked = false;
        const uint32_t pressed = vers.locked ? 0 : (b & ~vers.held);
        vers.held = b;
        const uint32_t dirs = b & 0x0C00;
        vers.repeat = dirs != 0 ? vers.repeat + 1 : 0;
        const uint32_t act = pressed | ((vers.repeat > 24 && vers.repeat % 6 == 0) ? dirs : 0);
        if (!vers.closing) {
            static uint32_t seen = 0;
            const PointerPick p = pointer_pick(52.0f + 40.0f - 1.5f, 13.5f, kVersionCount, 70.0f, 250.0f, seen);
            if (p.row >= 0 && p.row != vers.sel) { vers.sel = p.row; rc_sound(rdram, ctx, 0x740, 60); }
        }
        if (!vers.closing) {
            if (act & 0x0800) {
                vers.sel = (vers.sel + kVersionCount - 1) % kVersionCount;
                rc_sound(rdram, ctx, 0x740, 60);
            } else if (act & 0x0400) {
                vers.sel = (vers.sel + 1) % kVersionCount;
                rc_sound(rdram, ctx, 0x740, 60);
            } else if (pressed & 0x8000) {  // A
                if (version_here(vers.sel)) {
                    rc_sound(rdram, ctx, 0x740, 60);
                    vers.closing = true;
                    if (vers.sel != running_version()) {
                        aidyn::switch_version(kVersions[vers.sel]);  // restarts: returns only if that fails
                    }
                }
            } else if ((pressed & (0x4000 | 0x1000)) || y_pressed) {  // B, Start, Y: back
                rc_sound(rdram, ctx, 0x741, 60);
                vers.closing = true;
            }
        }
        vers.fade = std::clamp(vers.fade + (vers.closing ? -60 : 60), 0, 255);
        vers.pulse++;
        if (vers.closing && vers.fade == 0) {
            versions_close_now();
            return g;
        }
    }
    const int fade = vers.fade;
    constexpr float kLeft = 70.0f, kRight = 250.0f, kTop = 52.0f, kBottom = 170.0f;
    g = title_panel(rdram, ctx, g, kLeft, kTop, kRight, kBottom, (uint32_t)fade);
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    const CtlHost h{ true, fade, vers.closing, vers.pulse, 0 };
    // Everything is pictures in the title menu's own looks (make_branding.py): the heading like Menu and Load Game, the
    // names in the weight of its words (tinted here), the hints with the button icons or their keys.
    auto pic = [&](const char* name, float x, float y, uint32_t rgb) {
        const int i = title_image(name);
        if (i < 0) return;
        g = tinted_image_state(rdram, g, rgb << 8 | (uint32_t)fade);
        g = draw_rgba16(rdram, g, i, x * hs, y * vs, hs, vs);
    };
    auto width = [&](const char* name) {
        const int i = title_image(name);
        return i < 0 ? 0.0f : (float)ink_width(i);
    };
    const float centre = (kLeft + kRight) / 2.0f;
    {
        const int i = find_image("rc_title_versions");
        if (i >= 0) {
            pic("rc_title_versions", centre - aidyn::branding::kImages[i].w / 2.0f, kTop + 8.5f, 0xFFFFFF);
        }
    }
    static const char* kNames[kVersionCount] = { "rc_dd_1", "vw_patch", "vw_plus", "vw_action" };
    const int run = running_version();
    for (int i = 0; i < kVersionCount; i++) {
        const float y = kTop + 38.0f + 13.5f * i;
        const bool here = version_here(i);
        const uint32_t ink = i == vers.sel ? ctl_highlight(h) : here ? 0xF2E6C8 : 0x8C8470;
        if (i == vers.sel) {  // the title's copper marker
            pic("ver_mark", kLeft + 8.0f, y - 0.5f, 0xFFFFFF);
        }
        pic(kNames[i], kLeft + 22.0f, y, ink);
        if (i == run) {
            pic("vw_playing", kRight - 22.0f - width("vw_playing"), y, 0xE8C060);
        } else if (!here) {
            pic("vw_missing", kRight - 22.0f - width("vw_missing"), y, 0x8C8470);
        }
    }
    // Who made the highlighted one, under the list (spitzdano: the mods' authors credited; the README says more).
    static const char* kMadeBy[kVersionCount] = { "By H2O Entertainment, published by THQ (2001)", "A rebalance by halibabica",
                                                  "An overhaul by halibabica", "A mod by MDoulos" };
    g = ctl_state(rdram, ctx, g);
    g = ctl_text(rdram, ctx, g, kMadeBy[vers.sel], centre, kTop + 38.0f + 13.5f * kVersionCount + 5.0f, 0.5f, 0xC8BFA8, fade,
                 false, true);
    // The hints sit inside the closing bar (16 units, its rails at the top and bottom edges), centred in it: the 11-unit
    // icons at 85% (at full size their rims touched both rails, spitzdano: the A and B looked cut off).
    constexpr float kHint = 0.85f;
    const float footer = (width("dlg_choose") + width("dlg_back")) * kHint + 12.0f;
    const float foot_y = kBottom - 7.0f - 11.0f * kHint / 2.0f;  // the space between the rails is centred 7 up
    auto hint = [&](const char* name, float x) {
        const int i = title_image(name);
        if (i < 0) return;
        g = tinted_image_state(rdram, g, 0xF2E6C800u | (uint32_t)fade);
        g = draw_rgba16(rdram, g, i, x * hs, foot_y * vs, hs * kHint, vs * kHint);
    };
    hint("dlg_choose", centre - footer / 2.0f);
    hint("dlg_back", centre - footer / 2.0f + width("dlg_choose") * kHint + 12.0f);
    return g;
}

// Each frame: lets the game have its input again if the card stopped drawing the panel (a state change under it).
void title_versions_watchdog() {
    if (vers.open && std::chrono::steady_clock::now() - vers.last > std::chrono::milliseconds(300)) {
        versions_close_now();
    }
}

// ---- Fair play ----
// Three fixes for the original's loopholes, chosen on a panel when a New Game begins and fixed for that playthrough (on
// by default; each has its own achievement, earned by finishing the game with it on the whole way):
//   Healer rule     a healer can't lay hands on someone who healed them since the party last rested (camping, an inn:
//                   Party::CampHeal), so two healers can't heal each other up for free (#33). Honest Healer.
//   Fair trade      a shop never sells an item for less than it would pay for it. The game prices with
//                   f = (keeper's Merchant - the party's best) x 0.05: you buy at price x markup x (1 + f) and sell at
//                   base x (1 - f) / 2, so a party out-ranking the keeper (by 7, or at all on half-price stock) could
//                   buy and sell back at a profit for ever. Fair Trader.
//   Honest alchemy  a potion sells for no more than its ingredients' base price (the original sold a Restore Potion
//                   brewed from 150 gold of reagents for about 1,200). True Alchemist.
// The rules travel with each save the port writes (save-rules.txt beside the saves: the save's hash, the three as 0/1);
// a save without a record (made before) plays with none of them, and its playthrough can't earn their achievements.
// Always on, outside the panel: Sholeh's intelligence (35 in the original and Aidyn Patch) is saved in 5 bits and came
// back as 3; any stat a save cut below the character's starting value gets its 32 back (fair_fix_stats).
// The ReChronicled page's Fair play row says which are on (it can't change them).
//   Fights stay won (Settled Scores) a group of monsters beaten stays beaten. The game brings back a group
//                   that has no "defeated" flag of its own (52 of its 1,085 groups, such as the Gwernia inn cellar's
//                   bats, and townsfolk who fight once spoken to) after three trips through a door or to another map
//                   (its voxel chart, 19 places, counts them down: WriteTo_VoxelChart's last argument 3, in
//                   CombatStateFunc_12). With the rule each such win of a group with no conversation (borg_13) is kept
//                   (its map and voxel), with the save, and some_monsterparty_checker finds it there: the group never
//                   comes back. Townsfolk keep the game's way.
// The panel's last row is a help rather than a rule: the treasure sparkle ("Treasure sparkle", below), off by default.
// Once on it stays on for the playthrough (the ReChronicled page can turn it on later, never off), and a playthrough
// that never had it earns Keen Eye at the end.
// A record is the save's hash, then the three rules, the sparkle and fights stay won as 0/1 (the rules x for a save from
// before), then the groups beaten for good (hex, comma-separated; - for none).
namespace {
    struct FairRules {
        bool known = true;  // chosen at New Game (false: a save from before, or a playthrough without a record)
        bool on[3] = { true, true, true };
        bool sparkle = false;
        bool stay_won = true;
        bool chests = true;
        bool shops = false;  // Better shops: Action Edition's stock (the Original only, off by default)
        std::set<uint32_t> beaten;  // groups beaten for good: voxel index << 24 | map (mapDatA, the cell's x, y)
        std::set<uint32_t> looted;  // containers emptied for good: the same keys
    };
    FairRules fair;             // this playthrough's
    FairRules fair_pending;     // the panel's choice, for the game about to begin
    std::map<uint64_t, FairRules> fair_records;
    bool fair_records_loaded = false;
    // The panel's rows: the three rules, fights stay won, chests stay empty, the sparkle, better shops.
    constexpr int kFairRows = 7, kStayWonRow = 3, kChestsRow = 4, kSparkleRow = 5, kShopsRow = 6;
    constexpr const char* kFairNames[kFairRows] = { "Healer rule", "Fair trade", "Honest alchemy", "Fights stay won",
                                                     "Chests stay empty", "Treasure sparkle", "Better shops" };
    constexpr const char* kFairAch[kFairRows] = { "Honest Healer", "Fair Trader", "True Alchemist", "Settled Scores",
                                                   "Fair Finder", "Keen Eye", "Old Stock" };
    constexpr const char* kFairWhy[kFairRows][2] = {
        { "A healer can't lay hands on someone who", "healed them since the party last rested." },
        { "A shop never sells an item for less than", "it would pay you for it." },
        { "A potion sells for no more than", "its ingredients cost." },
        { "Monsters you have beaten don't come back", "when you return (the original's few did)." },
        { "A chest you have emptied stays empty (the", "original refilled a few; plants regrow)." },
        { "A star shows the chests and hidden treasure", "near you that you can see, until opened." },
        { "Shops carry Action Edition's stock (by MDoulos):", "fuller, and fitting each trade." },
    };
    // Action Edition's shops are its own fix (spitzdano): Fair trade isn't offered there, nor Fair Trader.
    bool fair_trade_offered() {
        return aidyn::settings::game().rom_patch != aidyn::settings::RomPatch::ActionEdition;
    }
    // Better shops is the Original's only: each mod stocks its shops its own way (Aidyn Patch 34 of the 75, Plus 52,
    // Action Edition is where the stock comes from), and MDoulos asked that they keep theirs.
    bool fair_shops_offered() {
        return aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::None;
    }
    bool fair_offered(int row) {
        return row == 1 ? fair_trade_offered() : row == kShopsRow ? fair_shops_offered() : true;
    }
    constexpr gpr kFairText = (gpr)(int32_t)0x80F82000;  // the healer rule's message for the game (0x100 bytes)

    std::filesystem::path fair_path() { return save_dir / "save-rules.txt"; }

    void fair_load_records() {
        if (fair_records_loaded || save_dir.empty()) return;
        fair_records_loaded = true;
        std::ifstream in(fair_path());
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream words(line);
            std::string hex, bits;
            if (!(words >> hex >> bits)) continue;
            FairRules r;
            r.known = !bits.empty() && bits[0] != 'x';
            for (int i = 0; i < 3 && i < (int)bits.size(); i++) r.on[i] = bits[i] == '1';
            r.sparkle = bits.size() > 3 && bits[3] == '1';
            r.stay_won = bits.size() > 4 && bits[4] == '1';
            r.chests = bits.size() > 5 && bits[5] == '1';
            r.shops = bits.size() > 6 && bits[6] == '1';  // a record from before Better shops: the original's
            auto keys = [&](std::set<uint32_t>& into) {
                std::string list;
                if (!(words >> list) || list == "-") return;
                std::istringstream in(list);
                for (std::string k; std::getline(in, k, ',');) {
                    if (!k.empty()) into.insert((uint32_t)std::stoul(k, nullptr, 16));
                }
            };
            keys(r.beaten);
            keys(r.looted);
            fair_records[std::stoull(hex, nullptr, 16)] = r;
        }
    }

    // The record's 0/1s (also the log's): the rules, the sparkle, fights stay won, chests stay empty, better shops.
    std::string fair_bits(const FairRules& r) {
        std::string b;
        for (int i = 0; i < 3; i++) b += r.known ? "01"[r.on[i]] : 'x';
        b += "01"[r.sparkle];
        b += r.known ? "01"[r.stay_won] : 'x';
        b += r.known ? "01"[r.chests] : 'x';
        b += r.known ? "01"[r.shops] : 'x';
        return b;
    }

    // A rule in force now.
    bool fair_on(int i) { return fair.known && fair.on[i] && (i != 1 || fair_trade_offered()); }

    // (healer ID, target ID) pairs since the last rest.
    std::set<std::pair<uint16_t, uint16_t>> healed_since_rest;
}

// 0-2 the rules, 3 Keen Eye (never the sparkle), 4 chests stay empty, 5 fights stay won, 6 Old Stock (the Original
// with its own shops: Better shops off).
bool fair_rule_kept(int i) {
    return i < 3 ? fair_on(i) : i == 3 ? fair.known && !fair.sparkle : i == 4 ? fair.known && fair.chests
         : i == 5 ? fair.known && fair.stay_won : fair.known && !fair.shops && fair_shops_offered();
}
// Better shops in force now (Action Edition's stock in the Original's shops: "Better shops", below).
bool better_shops_on() {
    static const bool test = std::getenv("AIDYN_BETTER_SHOPS_TEST") != nullptr;  // developers: in any version, any game
    return test || (fair.known && fair.shops && fair_shops_offered());
}
bool sparkle_on() { return fair.sparkle; }

// The ReChronicled page's Treasure sparkle row (A): on for the rest of this playthrough, once A is pressed twice (the
// first press says what it costs).
void sparkle_turn_on(uint8_t* rdram, recomp_context* ctx) {
    static auto asked = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (fair.sparkle) {
        notice("The treasure sparkle stays on for this game", 3000);
        rc_sound(rdram, ctx, 0x741, 60);
        return;
    }
    if (now - asked > std::chrono::seconds(4)) {
        asked = now;
        notice("Choose it again: on for good, and no Keen Eye", 4000);
        rc_sound(rdram, ctx, 0x740, 60);
        return;
    }
    asked = {};
    fair.sparkle = true;
    fprintf(stdout, "Fair play: the treasure sparkle turned on\n");
    fflush(stdout);
    notice("The treasure sparkle is on for this game", 3000);
    rc_sound(rdram, ctx, 0x740, 60);
}

void fair_note_save(uint64_t hash) {
    fair_load_records();
    if (!fair.known && !fair.sparkle) return;  // nothing to record: it plays without the rules either way
    fair_records[hash] = fair;
    std::ofstream out(fair_path(), std::ios::app);
    char head[32];
    snprintf(head, sizeof head, "%016llx ", (unsigned long long)hash);
    auto list = [](const std::set<uint32_t>& keys) {
        std::string l;
        for (const uint32_t k : keys) {
            char key[16];
            snprintf(key, sizeof key, "%s%x", l.empty() ? "" : ",", k);
            l += key;
        }
        return l.empty() ? std::string("-") : l;
    };
    out << head << fair_bits(fair) << " " << list(fair.beaten) << " " << list(fair.looted) << "\n";
}

void fair_loaded_save(uint64_t hash) {
    fair_load_records();
    const auto it = fair_records.find(hash);
    fair = it != fair_records.end() ? it->second : FairRules{ false, { false, false, false }, false, false, false, false };
    healed_since_rest.clear();
    fprintf(stdout, "Fair play: loaded save %016llx, %s, %zu groups beaten and %zu containers emptied for good\n",
            (unsigned long long)hash, it != fair_records.end() ? ("rules " + fair_bits(fair)).c_str() : "no record (none on)",
            fair.beaten.size(), fair.looted.size());
    fflush(stdout);
}

// Fights stay won. battle_setup_func's entry (a0 the group's voxel): whether this fight is one to keep once won (a group
// with no "defeated" flag, +0x1E, and no conversation, monster borg_13 at +0x5C).
namespace {
    bool fight_to_keep = false;
    uint32_t beaten_key(uint32_t index, uint32_t map, uint32_t x, uint32_t y, uint32_t bcd) {
        x = (x + (bcd >> 4) - 1) & 0xFF;  // voxelChart_sub: the cell the group is in, from the party's
        y = (y + (bcd & 3) - 1) & 0xFF;
        return (index & 0xFF) << 24 | (map & 0xFF) << 16 | x << 8 | y;
    }
}

extern "C" void aidyn_battle_setup(uint8_t* rdram, recomp_context* ctx) {
    const gpr v = ctx->r4;
    fight_to_keep = heap_pointer(v) && MEM_HU(0x1E, v) == 0 && MEM_W(0x5C, v) == 0;
    static const bool log = std::getenv("AIDYN_FAIR_LOG") != nullptr;
    if (log && heap_pointer(v)) {
        fprintf(stdout, "Fair play: a fight, group flags %u %u %u, conversation %x, voxel %u%s\n", MEM_HU(0x1C, v), MEM_HU(0x1E, v),
                MEM_HU(0x20, v), (uint32_t)MEM_W(0x5C, v), (uint32_t)ctx->r6 & 0xFFFF, fight_to_keep ? " (kept once won)" : "");
        fflush(stdout);
    }
}

// Chests stay empty. loot_func's entry (a0 the container's voxel): whether what is being opened is a chest (not a herb,
// spice or gem patch, LootType 4-6 at +0x3A: plants regrow).
namespace {
    bool chest_to_keep = false;
    constexpr gpr kEmptiedEntry = (gpr)(int32_t)0x80F814C0;  // a stand-in chart entry (0x10 bytes) for lookups
}

extern "C" void aidyn_loot_open(uint8_t* rdram, recomp_context* ctx) {
    const gpr v = ctx->r4;
    const uint32_t type = heap_pointer(v) ? MEM_HU(0x3A, v) : 0;
    chest_to_keep = heap_pointer(v) && !(type >= 4 && type <= 6);
    static const bool log = std::getenv("AIDYN_FAIR_LOG") != nullptr;
    if (log && heap_pointer(v)) {
        fprintf(stdout, "Fair play: a container opened, opened flag %u, loot type %u%s\n", MEM_HU(0x30, v), type,
                chest_to_keep ? " (a chest)" : " (a plant)");
        fflush(stdout);
    }
}

// WriteTo_VoxelChart's entry (index, mapDatA, x, y; BCD, type, count on the stack): a monster group (5) beaten
// (CombatStateFunc_12's count, 3) is kept for good, and a chest (1) with no opened flag of its own, emptied (loot_func's
// count, 10).
extern "C" void aidyn_voxel_chart_write(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bcd = MEM_BU(0x13, ctx->r29), type = MEM_BU(0x17, ctx->r29), count = MEM_BU(0x1B, ctx->r29);
    static const bool log = std::getenv("AIDYN_FAIR_LOG") != nullptr;
    if (log) {
        fprintf(stdout, "Fair play: voxel chart: voxel %u map %u cell %u,%u bcd %02x type %u count %u\n", (uint32_t)ctx->r4 & 0xFFFF,
                (uint32_t)ctx->r5 & 0xFF, (uint32_t)ctx->r6 & 0xFF, (uint32_t)ctx->r7 & 0xFF, bcd, type, count);
        fflush(stdout);
    }
    if (type == 1 && count == 10 && chest_to_keep && fair.known && fair.chests) {
        chest_to_keep = false;
        const uint32_t key = beaten_key((uint32_t)ctx->r4 & 0xFFFF, (uint32_t)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r7, bcd);
        if (fair.looted.insert(key).second) {
            fprintf(stdout, "Fair play: container %08x emptied for good\n", key);
            fflush(stdout);
        }
        return;
    }
    if (type != 5 || count != 3 || !fight_to_keep || !(fair.known && fair.stay_won)) return;
    fight_to_keep = false;
    const uint32_t key = beaten_key((uint32_t)ctx->r4 & 0xFFFF, (uint32_t)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r7, bcd);
    if (fair.beaten.insert(key).second) {
        fprintf(stdout, "Fair play: group %08x beaten for good\n", key);
        fflush(stdout);
    }
}

// some_ref_obj_lookup_func's entry (the same arguments): for a container (1) emptied for good, a chart entry (a stand-in:
// its callers only test it), so loot_func and exploding_container_check leave it open and empty.
extern "C" int aidyn_emptied_for_good(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bcd = MEM_BU(0x13, ctx->r29), type = MEM_BU(0x17, ctx->r29);
    static const bool log = std::getenv("AIDYN_FAIR_LOG") != nullptr;
    if (log && type == 1) {
        static int shown = 0;
        if (shown++ < 40) {
            fprintf(stdout, "Fair play: a container still empty? voxel %u map %u cell %u,%u bcd %02x\n", (uint32_t)ctx->r4 & 0xFFFF,
                    (uint32_t)ctx->r5 & 0xFF, (uint32_t)ctx->r6 & 0xFF, (uint32_t)ctx->r7 & 0xFF, bcd);
            fflush(stdout);
        }
    }
    if (!(fair.known && fair.chests) || fair.looted.empty()) return 0;
    if (type != 1) return 0;
    if (!fair.looted.count(beaten_key((uint32_t)ctx->r4 & 0xFFFF, (uint32_t)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r7, bcd))) {
        return 0;
    }
    for (int k = 0; k < 0x10; k += 4) MEM_W(k, kEmptiedEntry) = 0;
    ctx->r2 = kEmptiedEntry;
    return 1;
}

// some_monsterparty_checker's entry (the same arguments, type for count): true for a group beaten for good, which
// look_for_monsterparties then doesn't bring back.
extern "C" int aidyn_beaten_for_good(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t bcd = MEM_BU(0x13, ctx->r29), type = MEM_BU(0x17, ctx->r29);
    static const bool log = std::getenv("AIDYN_FAIR_LOG") != nullptr;
    if (log) {
        fprintf(stdout, "Fair play: a group appears? voxel %u map %u cell %u,%u bcd %02x type %u\n", (uint32_t)ctx->r4 & 0xFFFF,
                (uint32_t)ctx->r5 & 0xFF, (uint32_t)ctx->r6 & 0xFF, (uint32_t)ctx->r7 & 0xFF, bcd, type);
        fflush(stdout);
    }
    if (!(fair.known && fair.stay_won) || fair.beaten.empty()) return 0;
    if (type != 5) return 0;
    if (!fair.beaten.count(beaten_key((uint32_t)ctx->r4 & 0xFFFF, (uint32_t)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r7, bcd))) {
        return 0;
    }
    ctx->r2 = 1;
    return 1;
}

// Party::PrintHeal's entry: refused (the message in v0) when the target healed the healer since the last rest.
extern "C" int aidyn_heal_rule(uint8_t* rdram, recomp_context* ctx) {
    if (!fair_on(0)) return 0;
    const gpr party = ctx->r4;
    const gpr healer = MEM_W(4 * (ctx->r5 & 0xFF), party), target = MEM_W(4 * (ctx->r6 & 0xFF), party);
    if (!heap_pointer(healer) || !heap_pointer(target)) return 0;
    const uint16_t hid = MEM_HU(0, healer), tid = MEM_HU(0, target);
    // developers: AIDYN_FAIR_TEST_HEAL also refuses healing the same one twice, to see the refusal with one healer
    static const bool test = std::getenv("AIDYN_FAIR_TEST_HEAL") != nullptr;
    if (healed_since_rest.count({ tid, hid }) || (test && healed_since_rest.count({ hid, tid }))) {
        auto name = [&](gpr sheet) {
            std::string n;
            const gpr str = MEM_W(4, sheet);
            for (int k = 0; heap_pointer(str & ~(gpr)3) && k < 24 && MEM_B(k, str) != 0; k++) n += (char)MEM_B(k, str);
            return n;
        };
        const std::string msg = name(target) + " healed " + name(healer) + " since you last rested: " + name(healer) +
                                " can't lay hands on " + name(target) + " until you rest.";
        for (size_t k = 0; k < msg.size() && k < 0xFF; k++) MEM_B(k, kFairText) = (int8_t)msg[k];
        MEM_B(std::min<size_t>(msg.size(), 0xFF), kFairText) = 0;
        ctx->r2 = kFairText;
        return 1;
    }
    healed_since_rest.insert({ hid, tid });
    return 0;
}

extern "C" void aidyn_party_rested() { healed_since_rest.clear(); }

extern "C" void getItemPrice(uint8_t* rdram, recomp_context* ctx);
extern "C" void processIntroMenu(uint8_t* rdram, recomp_context* ctx);
extern "C" void GetIDIndex(uint8_t* rdram, recomp_context* ctx);

namespace {
    // An item's base price (ItemInstance::GetPrice: the database's, before a shop's markup).
    uint32_t base_price(uint8_t* rdram, recomp_context* ctx, gpr item) {
        return call_game(rdram, ctx, getItemPrice, (uint32_t)item) & 0xFFFF;
    }

    // The ingredients of potion `potion` at their base prices (potionRecipies: potion, spices, herbs, gemstones, rank,
    // 2 more; 15 of 7 bytes), or 0 for one with no recipe.
    uint32_t potion_ingredients(uint8_t* rdram, recomp_context* ctx, int potion) {
        static uint32_t price[3] = { 0, 0, 0 };  // spice, herb, gemstone (item IDs 0x111..0x113)
        if (price[0] == 0) {  // ItemInstance::GetPrice on a stand-in holding just the ingredient's ID
            const gpr probe = kFairText + 0x100;
            for (int k = 0; k < 3; k++) {
                MEM_H(0, probe) = (int16_t)(0x111 + k);
                price[k] = base_price(rdram, ctx, probe);
            }
        }
        for (int r = 0; r < 15; r++) {
            const gpr rec = (gpr)(int32_t)0x800D7960 + 7 * r;
            if (MEM_BU(0, rec) == potion) {
                return MEM_BU(1, rec) * price[0] + MEM_BU(2, rec) * price[1] + MEM_BU(3, rec) * price[2];
            }
        }
        return 0;
    }
}

// GetGoldPrice with its price in v1 (a0 the item, s0 set while selling): fair trade's floor, honest alchemy's cap.
extern "C" void aidyn_shop_price(uint8_t* rdram, recomp_context* ctx) {
    const gpr item = ctx->r4;
    if (!heap_pointer(item)) return;
    const bool selling = ctx->r16 != 0;
    // developers: AIDYN_FAIR_TEST_F=<f> sets the barter factor the next prices are made with (a party out-ranking the
    // keeper is negative), to see the rules act without one
    static const char* test_f = std::getenv("AIDYN_FAIR_TEST_F");
    if (test_f != nullptr) MEM_W(0, (gpr)(int32_t)0x800DEA60) = (int32_t)f32_bits(std::strtof(test_f, nullptr));
    const float f = read_f32(rdram, (gpr)(int32_t)0x800DEA60);  // gBarterFloat
    uint32_t price = (uint32_t)ctx->r3 & 0xFFFF;
    if (!selling && fair_on(1)) {
        const uint32_t sell = (uint32_t)std::max(0.0f, (float)base_price(rdram, ctx, item) * (1.0f - f) * 0.5f);
        price = std::max(price, sell);
    }
    if (selling && fair_on(2) && (MEM_HU(0, item) >> 8) == 16) {  // DB_POTION
        const uint32_t cost = potion_ingredients(rdram, ctx, MEM_HU(0, item) & 0xFF);
        if (cost > 0) price = std::min(price, cost);
    }
    static const bool log = std::getenv("AIDYN_FAIR_LOG") != nullptr;  // developers: each price the shop asks
    if (log) {
        const bool potion = (MEM_HU(0, item) >> 8) == 16;
        fprintf(stdout, "Fair play: item %04x %s, f %.2f, base %u, game %u, now %u%s\n", MEM_HU(0, item), selling ? "sold" : "bought",
                f, base_price(rdram, ctx, item), (uint32_t)ctx->r3 & 0xFFFF, price,
                potion ? (", ingredients " + std::to_string(potion_ingredients(rdram, ctx, MEM_HU(0, item) & 0xFF))).c_str() : "");
        fflush(stdout);
    }
    ctx->r3 = price;
}

// Once a second while playing: stats a save cut to 5 bits back to their real value (see above).
void fair_fix_stats(uint8_t* rdram, recomp_context* ctx) {
    static auto next = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now < next) return;
    next = now + std::chrono::seconds(1);
    // only while a game is being played (the title keeps a party from before, half freed)
    if (heap_pointer(MEM_W(0, kTitleScreen)) || MEM_BU(0, kTrekState) != 1) return;
    const gpr party = MEM_W(0, (gpr)(int32_t)0x800D9684), edb = MEM_W(0, (gpr)(int32_t)0x800E2830);
    if (!heap_pointer(party) || !heap_pointer(edb) || !heap_pointer(MEM_W(0x10, edb))) return;
    for (int i = 0; i < 4; i++) {
        const gpr sheet = MEM_W(4 * i, party);
        const gpr stats = heap_pointer(sheet) ? MEM_W(0x18, sheet) : 0;
        if (!heap_pointer(stats)) continue;
        const uint16_t id = MEM_HU(0, sheet);
        if ((id >> 8) != 2) continue;  // an entity (DB_ENTITY), or GetIDIndex stops the game
        const uint32_t idx = call_game(rdram, ctx, GetIDIndex, id, 0, 0) & 0xFF;
        if (idx >= MEM_BU(0, edb)) continue;
        const gpr ent = (gpr)(int32_t)(MEM_W(0x10, edb) + 0x80 * (int32_t)idx);  // signed: a uint32 sum wouldn't sign-extend
        for (int st : { 0, 1, 2, 4 }) {  // INT, WIL, DEX, STR: the 5-bit ones (EntityRAM stats at +0x39, StatEnum order)
            const int start = MEM_BU(0x39 + st, ent), base = MEM_BU(st, stats);
            if (start < 32 || base >= 32) continue;  // only a value the save's 5 bits couldn't hold was cut
            MEM_B(st, stats) = (int8_t)(base + 32);
            MEM_B(14 + st, stats) = (int8_t)(MEM_BU(14 + st, stats) + 32);  // magicMod (a copy of Base when loaded)
            for (int arr : { 7, 21 }) {  // health, Modded: wrapped too when they were 32 or more
                const int v = MEM_BU(arr + st, stats);
                if (std::abs(v + 32 - (base + 32)) < std::abs(v - (base + 32))) MEM_B(arr + st, stats) = (int8_t)(v + 32);
            }
            static int told = 0;
            if (told++ < 8) {
                fprintf(stdout, "Stats: stat %d of party member %d was %d in the save (5 bits): %d again\n", st, i, base, base + 32);
                fflush(stdout);
            }
        }
    }
}

// The New Game panel (over the title screen): the three rules, left and right change the highlighted one, A begins (the
// game's own New Game, its name entry, with the choice kept for the game about to start), B goes back.
namespace {
    struct FairPanel {
        bool open = false, closing = false, begin = false;
        int fade = 0, pulse = 0, repeat = 0, row = 0;
        uint32_t held = 0xFFFFFFFF;
        uint32_t menu = 0, widget = 0;  // processIntroMenu's arguments, to call it once A is pressed
        bool passing = false;           // calling it ourselves: not ours to open
    } fp;
}

namespace {
    bool& fair_pick(int row) {
        return row < 3 ? fair_pending.on[row] : row == kStayWonRow ? fair_pending.stay_won
             : row == kChestsRow ? fair_pending.chests : row == kShopsRow ? fair_pending.shops : fair_pending.sparkle;
    }
}

bool fair_panel_open(uint8_t* rdram, recomp_context* ctx) {
    if (fp.passing) return false;
    fp = FairPanel{};
    fp.open = true;
    fp.menu = (uint32_t)ctx->r4;
    fp.widget = (uint32_t)ctx->r5;
    fair_pending = FairRules{};
    fair_pending.on[1] = fair_trade_offered();
    aidyn::input::set_capture(true);
    return true;
}

gpr fair_panel(uint8_t* rdram, recomp_context* ctx, gpr g) {
    if (!fp.open) return g;
    if (!heap_pointer(MEM_W(0, kTitleScreen))) {
        fp = FairPanel{};
        aidyn::input::set_capture(false);
        return g;
    }
    uint32_t b = 0;
    float sx = 0.0f, sy = 0.0f;
    aidyn::input::captured(&b, &sx, &sy);
    if (sy > 0.5f) b |= 0x0800;
    if (sy < -0.5f) b |= 0x0400;
    if (sx < -0.5f) b |= 0x0200;
    if (sx > 0.5f) b |= 0x0100;
    const uint32_t pressed = fp.held == 0xFFFFFFFF ? 0 : (b & ~fp.held);
    fp.held = (fp.held == 0xFFFFFFFF && (b & 0x8000)) ? 0xFFFFFFFF : b;
    const uint32_t dirs = b & 0x0F00;
    fp.repeat = dirs != 0 ? fp.repeat + 1 : 0;
    const uint32_t act = pressed | ((fp.repeat > 24 && fp.repeat % 6 == 0) ? dirs : 0);
    constexpr float kLeft = 50.0f, kRight = 270.0f, kTop = 58.0f, kStep = 11.0f;
    if (!fp.closing && fp.fade == 255) {
        static uint32_t seen = 0;
        const PointerPick p = pointer_pick(kTop - 2.0f, kStep, kFairRows, kLeft - 8.0f, kRight + 4.0f, seen);
        if (p.row >= 0 && p.row != fp.row) { fp.row = p.row; rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0800) { fp.row = (fp.row + kFairRows - 1) % kFairRows; rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0400) { fp.row = (fp.row + 1) % kFairRows; rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0300) {
            if (!fair_offered(fp.row)) {
                rc_sound(rdram, ctx, 0x741, 60);
            } else {
                bool& v = fair_pick(fp.row);
                v = !v;
                rc_sound(rdram, ctx, 0x740, 60);
            }
        }
        if (pressed & 0x8000) { fp.begin = true; fp.closing = true; rc_sound(rdram, ctx, 0x740, 60); }
        else if (pressed & (0x4000 | 0x1000)) { fp.closing = true; rc_sound(rdram, ctx, 0x741, 60); }
    }
    fp.fade = std::clamp(fp.fade + (fp.closing ? -60 : 60), 0, 255);
    if (fp.closing && fp.fade == 0 && b == 0) {
        const bool begin = fp.begin;
        const uint32_t menu = fp.menu, widget = fp.widget;
        fp = FairPanel{};
        aidyn::input::set_capture(false);
        if (begin) {  // the game's own New Game now (its name entry), this choice the new playthrough's
            fair = fair_pending;
            fair.known = true;
            // developers: AIDYN_FAIR_TEST_LOOTED=key,key (hex, as save-rules.txt has them) starts with those emptied
            if (const char* t = std::getenv("AIDYN_FAIR_TEST_LOOTED")) {
                std::istringstream in(t);
                for (std::string k; std::getline(in, k, ',');) {
                    if (!k.empty()) fair.looted.insert((uint32_t)std::stoul(k, nullptr, 16));
                }
            }
            healed_since_rest.clear();
            fprintf(stdout, "Fair play: new game, rules %s\n", fair_bits(fair).c_str());
            fflush(stdout);
            fp.passing = true;
            call_game(rdram, ctx, processIntroMenu, menu, widget);
            fp.passing = false;
        }
        return g;
    }
    fp.pulse++;
    const int fade = fp.fade;
    const CtlHost h{ true, fade, fp.closing, fp.pulse, 0 };
    const float centre = 160.0f;
    g = title_panel(rdram, ctx, g, 40.0f, 20.0f, 280.0f, 194.0f, (uint32_t)fade);
    g = ctl_state(rdram, ctx, g);
    g = ctl_text(rdram, ctx, g, "Fair Play", centre, kPanelTitleY, 0.9f, 0xE8C060, fade, false, true);
    g = ctl_text(rdram, ctx, g, "Fixes for the original's loopholes, and a help, set for the whole game.", centre, 46.0f, 0.5f,
                 0xC8BFA8, fade, false, true);
    for (int i = 0; i < kFairRows; i++) {
        const float y = kTop + kStep * i;
        const uint32_t ink = i == fp.row ? ctl_highlight(h) : 0xF2E6C8;
        const bool on = fair_pick(i), offered = fair_offered(i);
        g = ctl_text(rdram, ctx, g, kFairNames[i], kLeft, y, 0.7f, offered ? ink : 0x8C8470, fade);
        g = ctl_text(rdram, ctx, g, !offered ? "Not here" : on ? "On" : "Off", kRight, y, 0.7f, on ? 0xE8C060 : 0x8C8470, fade, true);
    }
    const float dy = kTop + kStep * kFairRows + 3.0f;
    g = ctl_text(rdram, ctx, g, kFairWhy[fp.row][0], centre, dy, 0.55f, 0xF2E6C8, fade, false, true);
    g = ctl_text(rdram, ctx, g, kFairWhy[fp.row][1], centre, dy + 9.0f, 0.55f, 0xF2E6C8, fade, false, true);
    // The sparkle and better shops: on loses their achievement (the sparkle's stays on for good).
    const bool help = fp.row == kSparkleRow || fp.row == kShopsRow;
    const bool mod_shops = !fair_offered(fp.row) && aidyn::settings::game().rom_patch != aidyn::settings::RomPatch::ActionEdition;
    const std::string warn = !fair_offered(fp.row) ? std::string(mod_shops ? "This version keeps its own shops." : "Action Edition's shops are its own fix.")
                           : fp.row == kSparkleRow ? std::string("On (for good): the achievement ") + kFairAch[kSparkleRow] + " can't be earned."
                           : fp.row == kShopsRow ? std::string("On: the achievement ") + kFairAch[kShopsRow] + " can't be earned in this game."
                           : std::string("Off: the achievement ") + kFairAch[fp.row] + " can't be earned in this game.";
    g = ctl_text(rdram, ctx, g, warn, centre, dy + 21.0f, 0.5f, fair_pick(fp.row) != help ? 0x8C8470 : 0xE8A050, fade, false, true);
    const bool kb = aidyn::input::keyboard_last();
    const std::string a_key = kb ? keys_of(aidyn::controls::kA, false) : "A", b_key = kb ? keys_of(aidyn::controls::kB, false) : "B";
    g = ctl_text(rdram, ctx, g, a_key + ": begin   left / right: change   " + b_key + ": back", centre, 167.0f, 0.55f, 0xC8BFA8,
                 fade, false, true);
    return g;
}

// The ReChronicled page's Fair play row (A): which rules this playthrough has.
void fair_show(uint8_t* rdram, recomp_context* ctx) {
    // Short: the notice is one line. The rules on, then the sparkle if it is.
    std::string t = "Fair play: none (an older game)";
    if (fair.known) {
        const char* names[6] = { "Healer", "Trade", "Alchemy", "Stay won", "Chests", "Shops" };
        const bool on[6] = { fair.on[0], fair_on(1), fair.on[2], fair.stay_won, fair.chests, better_shops_on() };
        std::string list;
        for (int i = 0; i < 6; i++) {
            if (on[i]) list += std::string(list.empty() ? "" : ", ") + names[i];
        }
        t = "Fair play: " + (list.empty() ? std::string("none") : list);
    }
    if (fair.sparkle) t += " + sparkle";
    notice(t.c_str(), 6000);
    rc_sound(rdram, ctx, 0x740, 60);
}

// ---- Better shops ----
// Fair play's Better shops (the Original only, off by default; finishing with it off earns Old Stock): the game's 75 shops stock what
// Action Edition (MDoulos's mod) stocks them with, as he agreed (2026-10-29), credited. The original's shops are thin;
// Action Edition's are fuller and fit each trade. The game reads its shop table from the ROM into gShopDBP (0x800E2B20:
// ShopDB {total, pad, shop_ram* shops}; shop_ram 108 bytes: the shopkeeper, stock[23] as ItemIDs, multi[3][20] the three
// price bytes of the first 20) once, and every shop opened reads from there. Each frame: while the rule is in force the
// table holds Action Edition's records (src/game/action_shops.h, tools/data/action_shops.py: the same shops, shopkeepers
// and only items the original has), converted as ShopDB::Load converts the ROM's; otherwise the game's own, put back.
// A new table (the game making it again) starts as the game's own.
#include "action_shops.h"

namespace {
    constexpr gpr kShopDBPtr = (gpr)(int32_t)0x800E2B20;  // gShopDBP
    constexpr int kShopRam = 108;
    struct ShopsState {
        gpr shops = 0;
        bool applied = false;
        std::vector<uint8_t> original;  // the game's own table, while Action Edition's is in it
    } shops_state;

    void shops_write(uint8_t* rdram, gpr shop, const uint8_t* rec) {
        int pos = 2;  // after the shopkeeper (kept: the same in both)
        for (int k = 0; k < 23; k++) {
            MEM_H(2 + 2 * k, shop) = (int16_t)(rec[pos] | rec[pos + 1] << 8);
            pos += 2;
            if (k < 20) {
                for (int m = 0; m < 3; m++) MEM_B(48 + 20 * m + k, shop) = (int8_t)rec[pos++];
            }
        }
    }
}

void better_shops_tick(uint8_t* rdram) {
    const gpr db = MEM_W(0, kShopDBPtr);
    if (!heap_pointer(db)) {
        shops_state = {};
        return;
    }
    const gpr shops = MEM_W(4, db);
    if (!heap_pointer(shops) || MEM_BU(0, db) != aidyn::action_shops::kShops) return;
    if (shops != shops_state.shops) {
        shops_state = {};
        shops_state.shops = shops;
    }
    const bool want = better_shops_on();
    if (want == shops_state.applied) return;
    const int n = aidyn::action_shops::kShops;
    if (want) {
        for (int i = 0; i < n; i++) {  // the same shops in the same order: else leave the game's alone
            const uint8_t* rec = aidyn::action_shops::kRecords[i];
            if (MEM_HU(kShopRam * i, shops) != (uint16_t)(rec[0] | rec[1] << 8)) return;
        }
        shops_state.original.resize((size_t)n * kShopRam);
        for (int k = 0; k < n * kShopRam; k++) shops_state.original[k] = (uint8_t)MEM_B(k, shops);
        for (int i = 0; i < n; i++) shops_write(rdram, shops + kShopRam * i, aidyn::action_shops::kRecords[i]);
    } else if (shops_state.original.size() == (size_t)n * kShopRam) {
        for (int k = 0; k < n * kShopRam; k++) MEM_B(k, shops) = (int8_t)shops_state.original[k];
    }
    shops_state.applied = want;
    fprintf(stdout, "Better shops: %s\n", want ? "Action Edition's stock" : "the game's own");
    fflush(stdout);
}

// ---- Keys on the prompts (#44) ----
// The controls card shows the keys and mouse buttons bound to each of its lines, in gold after the line, while the
// keyboard or the mouse is what was used last (aidyn::input::keyboard_last: a pad player sees the card as before);
// the Help book's Keyboard and PC page says the current keys. Plain text for now; key pictures come with the art.
namespace {
    std::string keys_of(aidyn::controls::Action a, bool all = true) {
        std::string out;
        for (int sl = 0; sl < aidyn::controls::kSlots; sl++) {
            const aidyn::controls::Input in = aidyn::controls::binding(a, sl);
            if (in.empty()) continue;
            if (!out.empty()) out += all ? " or " : "";
            if (!all && !out.empty()) break;
            out += aidyn::controls::input_name(in);
            if (!all) break;
        }
        return out.empty() ? "no key" : out;
    }

    std::string move_keys() {
        using namespace aidyn::controls;
        std::string out;
        for (Action a : { kMoveUp, kMoveLeft, kMoveDown, kMoveRight }) {
            if (!out.empty()) out += " ";
            out += keys_of(a, false);
        }
        return out;
    }

    }

// Key caps in a keyboard twin's slots (make_branding.py: KEYS; the header's kKeyed says where they are): each button the
// picture stands for becomes the key (or the mouse button) bound to what that button does, as a cap with its name on it
// or a mouse picture with the button lit. Placeholder art: spitzdano will have better made.
namespace {
    struct SlotFace {
        std::string text;            // a cap's name
        const char* mouse = nullptr; // or a mouse picture
        std::string cap;             // Codex's finished cap for it (kc_<cap>), if there is one
    };

    // The finished cap (make_branding.py kc_<name>, the header's kKeyCaps) for a key, by its name
    // (aidyn::controls::input_name, SDL's scancode names), or "" for one the set hasn't.
    std::string cap_for(const std::string& n) {
        if (n.size() == 1 && std::isalnum((unsigned char)n[0])) return std::string(1, (char)std::toupper((unsigned char)n[0]));
        if (n.size() >= 2 && n.size() <= 3 && n[0] == 'F' && std::isdigit((unsigned char)n[1])) return n;  // F1..F12
        static const std::unordered_map<std::string, std::string> kNames = {
            { "Up", "Up" }, { "Down", "Down" }, { "Left", "Left" }, { "Right", "Right" },
            { "Esc", "Esc" }, { "Tab", "Tab" }, { "Left Alt", "Alt" }, { "Right Alt", "Alt" }, { "End", "End" },
            { "Left GUI", "Win" }, { "Right GUI", "Win" }, { "CapsLock", "Caps" }, { "Left Ctrl", "Ctrl" },
            { "Right Ctrl", "Ctrl" }, { "Home", "Home" }, { "Menu", "Menu" }, { "Application", "Menu" }, { "Pause", "Pause" },
            { "Left Shift", "Shift" }, { "Right Shift", "Shift" }, { "Enter", "Enter" }, { "Insert", "Insert" },
            { "PrintScreen", "PrtSc" }, { "ScrollLock", "ScrLk" }, { "Delete", "Delete" }, { "PageUp", "PageUp" },
            { "PageDown", "PageDown" }, { "Space", "Space" }, { "Backspace", "Backspace" },
            { "Minus", "minus" }, { "Equals", "equals" }, { "Left Bracket", "lbracket" }, { "Right Bracket", "rbracket" },
            { "Semicolon", "semicolon" }, { "Apostrophe", "apostrophe" }, { "Comma", "comma" }, { "Period", "period" },
            { "Slash", "slash" }, { "Backslash", "backslash" }, { "Grave", "grave" },
            { "Keypad +", "NumAdd" }, { "Keypad -", "NumSub" }, { "Keypad *", "NumMul" }, { "Keypad /", "NumDiv" },
            { "Keypad .", "NumDot" }, { "Keypad Enter", "NumEnter" }, { "Numlock", "NumLock" },
        };
        if (n.rfind("Keypad ", 0) == 0 && n.size() == 8 && std::isdigit((unsigned char)n[7])) return "Num" + n.substr(7);
        const auto it = kNames.find(n);
        return it == kNames.end() ? "" : it->second;
    }

    // A finished cap's painted width in texels (0: not in the set).
    int cap_painted(const std::string& cap) {
        if (cap.empty()) return 0;
        for (const auto& k : aidyn::branding::kKeyCaps) {
            if (cap == k.name) return k.painted;
        }
        return 0;
    }

    std::string cap_name(aidyn::controls::Input in) {
        static const struct { const char* from; const char* to; } kShort[] = {
            { "Enter", "Ent" }, { "Space", "Spc" }, { "Backspace", "Bksp" }, { "Left Shift", "Shft" }, { "Right Shift", "Shft" },
            { "Left Ctrl", "Ctrl" }, { "Right Ctrl", "Ctrl" }, { "Left Alt", "Alt" }, { "Right Alt", "Alt" },
            { "Left Bracket", "[" }, { "Right Bracket", "]" }, { "Semicolon", ";" }, { "Apostrophe", "'" },
            { "Keypad 0", "N0" }, { "Keypad 1", "N1" }, { "Keypad 2", "N2" }, { "Keypad 3", "N3" }, { "Keypad 4", "N4" },
            { "Keypad 5", "N5" }, { "Keypad 6", "N6" }, { "Keypad 7", "N7" }, { "Keypad 8", "N8" }, { "Keypad 9", "N9" },
        };
        const std::string n = aidyn::controls::input_name(in);
        for (const auto& k : kShort) {
            if (n == k.from) return k.to;
        }
        return n;
    }

    SlotFace face_of(aidyn::controls::Action a) {
        using namespace aidyn::controls;
        Input in = binding(a, 0);
        if (in.empty()) in = binding(a, 1);
        if (in.empty()) return { "-" };
        if (in.kind == kMouse) {
            static const char* kNames[] = { "", "key_mouse_left", "key_mouse_middle", "key_mouse_right", "key_mouse_back",
                                            "key_mouse_forward" };
            return { "", in.code >= 1 && in.code <= 5 ? kNames[in.code] : "key_mouse_move" };
        }
        if (in.kind == kWheel) return { "", in.code == 0 ? "key_mouse_wheelup" : "key_mouse_wheeldown" };
        return { cap_name(in), nullptr, cap_for(input_name(in)) };
    }

    // Several keys on one cap: their names, run together when each is one character ("WASD"), else spaced.
    std::string keys_text(std::initializer_list<aidyn::controls::Action> list) {
        std::vector<std::string> names;
        bool single = true;
        for (aidyn::controls::Action a : list) {
            names.push_back(face_of(a).text.empty() ? "-" : face_of(a).text);
            single = single && names.back().size() == 1;
        }
        std::string out;
        for (const std::string& n : names) out += (single || out.empty() ? "" : " ") + n;
        return out;
    }

    SlotFace slot_face(const std::string& button) {
        using namespace aidyn::controls;
        if (button == "A") return face_of(kA);
        if (button == "B") return face_of(kB);
        if (button == "CD") return face_of(kX);
        if (button == "CU") return face_of(kY);
        if (button == "CL") return face_of(kView);
        if (button == "CR") return face_of(kRT);
        if (button == "START") return face_of(kMenu);
        if (button == "Z") return face_of(kLT);
        if (button == "L") return face_of(kLB);
        if (button == "R") return face_of(kRB);
        if (button == "L3") return face_of(kFastForward);
        if (button == "R3") return face_of(kCamReset);
        if (button == "LS") {  // W A S D or the arrows: the finished combined caps
            SlotFace f{ keys_text({ kMoveUp, kMoveLeft, kMoveDown, kMoveRight }) };
            const std::string names = face_of(kMoveUp).cap + face_of(kMoveLeft).cap + face_of(kMoveDown).cap + face_of(kMoveRight).cap;
            if (names == "WASD") f.cap = "WASD";
            if (names == "UpLeftDownRight") f.cap = "Arrows";
            return f;
        }
        if (button == "RS") {
            if (mouse_look()) return { "", "key_mouse_move" };
            return { keys_text({ kCamUp, kCamLeft, kCamDown, kCamRight }) };
        }
        if (button == "DPAD") return { keys_text({ kMoveLeft, kMoveRight }) };
        return { "?" };
    }
}

// The ink width (texels) of a character's picture on a cap, a space's 0, -1 for one with no picture.
static float key_glyph_ink(unsigned char c) {
    for (const auto& k : aidyn::branding::kKeyGlyphs) {
        if (k.ch == c) return k.ink;
    }
    return c == ' ' ? 0.0f : -1.0f;
}

// A name's width on a cap, texels: its letters' ink with a gap between and spaces of 1.4.
static float key_text_width(const std::string& text) {
    constexpr float kGap = 0.5f, kSpace = 1.4f;
    float w = 0.0f;
    int shown = 0;
    for (unsigned char c : text) {
        const float ink = key_glyph_ink(c);
        if (ink < 0.0f) continue;
        w += (c == ' ' ? kSpace : ink) + (shown++ > 0 ? kGap : 0.0f);
    }
    return w;
}

// A cap (or a mouse picture) for `button` in a slot w x h texels at (x, y) on screen (pixels, like draw_rgba16's), a texel
// being sx x sy pixels; cover: no narrower than the round icon it must hide (a page's painted button).
gpr draw_button_cap(uint8_t* rdram, gpr g, const char* button, float x, float y, float sx, float sy, float w, float h, bool cover, bool text_state) {
    recomp_context* ctx = frame_ctx;
    const gpr font = MEM_W(0, kFontPtr);
    if (ctx == nullptr || !heap_pointer(font)) return g;
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    const uint8_t alpha = prompt_alpha_now;
    const float hu = h * sy / vs;  // a slot and the cap in it, in units of the 320x240 screen
    auto piece = [&](const char* piece_name, float ux, float uy, float uw, float uh) {
        const int i = find_image(piece_name);
        if (i < 0) return;
        const auto& img = aidyn::branding::kImages[i];
        g = draw_rgba16(rdram, g, (size_t)i, ux * hs, uy * vs, uw * hs / img.w, uh * vs / img.h);
    };
    const SlotFace face = slot_face(button);
    const float slot_x = x / hs, slot_w = w * sx / hs, top = y / vs;
    constexpr float kFaceCentre = 5.95f;  // texels from the cap's top (make_branding.py: KEY_FACE_CENTRE)
    constexpr float kPad = 2.0f;  // the cap pieces' and mice's transparent texels at the right (make_branding.py: KEY_PAD)
    if (face.mouse != nullptr) {
        piece(face.mouse, slot_x + (slot_w - hu) / 2.0f, top, hu * (12.0f + kPad) / 12.0f, hu);
        if (text_state) {
            return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager), MEM_HU(kVres1, (gpr)gGfxManager));
        }
        return g;
    }
    // Codex's finished cap, when the key has one: drawn whole, as tall as the slot and as wide as painted (shrunk as a
    // whole if the slot can't hold it), centred in the slot.
    // A cap that would have to shrink below 85% of its size (ENTER in the controls card's narrow slot) isn't used: the
    // short name ("Ent") is lettered on a full-height cap instead, so every cap in a column stays the same height.
    if (const int painted = cap_painted(face.cap); painted != 0 &&
        std::max(slot_w, hu * (cover ? 1.4f : 1.15f)) >= 0.85f * painted * (hu / 12.0f)) {
        const float u = hu / 12.0f, room = std::max(slot_w, hu * (cover ? 1.4f : 1.15f));
        const float k = std::min(1.0f, room / (painted * u));
        const float cw = painted * u * k, ch = hu * k;
        const std::string name = "kc_" + face.cap;
        const int i = find_image(name);
        if (i >= 0) {
            const auto& img = aidyn::branding::kImages[i];
            piece(name.c_str(), slot_x + (slot_w - cw) / 2.0f, top + (hu - ch) / 2.0f, img.w * u * k, ch);
        }
        if (text_state) {
            return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager), MEM_HU(kVres1, (gpr)gGfxManager));
        }
        return g;
    }
    // The letters are pictures (make_branding.py, key_g_<code>: 12 texels high like the cap, the capitals' height centred
    // on the cap's face, as wide as their ink), so they are placed exactly: no whole-unit rounding of the game's text.
    // A texel is hu / 12 units; the caps' ends take 4 of 12 texels each, so the name needs that much room at each side.
    const float u = hu / 12.0f, pad = hu * 0.55f, gap = 0.5f, space = 1.4f;  // gap and space in texels (key_text_width's)
    const float text_w = key_text_width(face.text);  // texels
    float k = 1.0f;  // a long name: smaller letters, to fit the slot
    if (text_w * u > slot_w - pad - 0.5f) {
        k = (slot_w - pad - 0.5f) / (text_w * u);
    }
    const float cap_w = std::clamp(text_w * u * k + pad, hu * (cover ? 1.4f : 0.9f), std::max(slot_w, hu * (cover ? 1.4f : 1.15f)));
    const float cap_x = slot_x + (slot_w - cap_w) / 2.0f;
    const float end = 4.0f * hu / 12.0f;  // the cap's ends are 4 of its 12 texels
    // Each piece is drawn wider by its pad (the texels past its picture are transparent), the middle stretched to fit.
    // They overlap by a unit where they meet: drawn edge to edge (or by half a unit), the filtering at the edges left a
    // thin dark line.
    constexpr float kOverlap = 1.0f;
    const float mid_w = std::max(0.0f, cap_w - 2.0f * end) + 2.0f * kOverlap;
    piece("key_cap_l", cap_x, top, end * (4.0f + kPad) / 4.0f, hu);
    piece("key_cap_m", cap_x + end - kOverlap, top, mid_w * (2.0f + kPad) / 2.0f, hu);
    piece("key_cap_r", cap_x + cap_w - end - kOverlap, top, end * (4.0f + kPad) / 4.0f, hu);
    float pen = cap_x + (cap_w - text_w * u * k) / 2.0f;
    const float letter_top = top + (kFaceCentre / 12.0f) * hu * (1.0f - k);  // the face's centre stays where it is
    for (unsigned char c : face.text) {
        const float ink = key_glyph_ink(c);
        if (ink < 0.0f) continue;
        if (c != ' ') {
            char name[16];
            std::snprintf(name, sizeof name, "key_g_%02x", c);
            const int i = find_image(name);
            if (i >= 0) {
                const auto& img = aidyn::branding::kImages[i];
                // its ink centred in its first img.w - kPad texels (the last kPad are transparent)
                piece(name, pen - (img.w - kPad - ink) / 2.0f * u * k, letter_top, img.w * u * k, hu * k);
            }
        }
        pen += ((c == ' ' ? space : ink) + gap) * u * k;
    }
    if (text_state) {  // inside the game's own text: its next letter wants the textured state again
        return (gpr)(int32_t)call_game(rdram, ctx, borg8DlistInit, (uint32_t)g, 6, MEM_HU(kHres1, (gpr)gGfxManager), MEM_HU(kVres1, (gpr)gGfxManager));
    }
    return g;
}

gpr draw_key_slots(uint8_t* rdram, gpr g, size_t index, float x, float y, float sx, float sy) {
    const char* name = aidyn::branding::kImages[index].name;
    const aidyn::branding::KeyedImage* keyed = nullptr;
    for (const auto& k : aidyn::branding::kKeyed) {
        if (std::strcmp(k.name, name) == 0) keyed = &k;
    }
    if (keyed == nullptr) return g;
    for (int n = 0; n < keyed->n; n++) {
        const auto& slot = keyed->slots[n];
        g = draw_button_cap(rdram, g, slot.button, x + slot.x * sx, y, sx, sy, (float)slot.w, (float)keyed->h, false);
    }
    return g;
}

// ---- Keys in the game's text ----
// The game's own text carries button symbols (\x7F..\x89: the font's cells 0x5C..0x66, redrawn as the Xbox icons by the
// pack): menu words, the Help book, messages. While the keyboard or mouse was used last each one is drawn as a key cap
// (or a mouse picture) instead: N64BorgImageDraw's entry sees the font's cell being drawn and draws the cap in its place,
// and the cells' widths in the font's tables (kerning) are set to the caps' so the text lays out around them.
namespace {
    constexpr int kGlyphFirst = 0x5C, kGlyphCount = 11;
    // The cells' buttons, in order: A B RT View Y X Menu D-pad LT LB RB.
    constexpr const char* kGlyphButtons[kGlyphCount] = { "A", "B", "CR", "CL", "CU", "CD", "START", "DPAD", "Z", "L", "R" };
    uint16_t glyph_original[3][kGlyphCount];
    bool glyph_saved[3] = { false, false, false };

    // Face f of the font (gGlobals.font) if it is the 36 x 414 sheet with the button cells.
    gpr glyph_face(uint8_t* rdram, gpr font, uint32_t f) {
        const gpr subs = MEM_W(0, font);
        if (!heap_pointer(subs) || f >= MEM_BU(9, font) || f >= 3) return 0;
        const gpr sub = subs + 0x18 * f, img = (gpr)(int32_t)MEM_W(4, sub);
        if (!heap_pointer(img) || MEM_HU(0x0A, img) != 36 || MEM_HU(0x0C, img) != 414) return 0;
        return sub;
    }

    // A cap's width at the font's own scale (the cells are 9 units tall): its name plus a little, 9 at least.
    int glyph_cap_width(uint8_t* rdram, recomp_context* ctx, gpr font, const char* button) {
        const SlotFace face = slot_face(button);
        if (face.mouse != nullptr) return 9;
        if (const int painted = cap_painted(face.cap)) return (int)std::ceil(painted * 9.0f / 12.0f);
        return std::max(9, (int)std::ceil(key_text_width(face.text) * 9.0f / 12.0f + 0.55f * 9.0f));
    }
}

// Each frame: the cells' widths for the device in use.
void update_glyph_widths(uint8_t* rdram, recomp_context* ctx) {
    title_versions_watchdog();
    const gpr font = MEM_W(0, kFontPtr);
    if (!heap_pointer(font)) return;
    const bool keys = aidyn::input::keyboard_last();
    for (uint32_t f = 0; f < 3; f++) {
        const gpr sub = glyph_face(rdram, font, f);
        if (sub == 0) continue;
        const gpr kern = (gpr)(int32_t)MEM_W(0x10, sub);
        if (!heap_pointer(kern)) continue;
        if (!glyph_saved[f]) {
            for (int k = 0; k < kGlyphCount; k++) glyph_original[f][k] = MEM_HU(2 * (kGlyphFirst + k), kern);
            glyph_saved[f] = true;
        }
        for (int k = 0; k < kGlyphCount; k++) {
            MEM_H(2 * (kGlyphFirst + k), kern) = (int16_t)(keys ? glyph_cap_width(rdram, ctx, font, kGlyphButtons[k]) : glyph_original[f][k]);
        }
    }
}

// N64BorgImageDraw's entry (a0 the display list, a1 the picture, a2 and a3 the position as floats, the stack the part of
// the picture, its scale at +0x20 and +0x24): a button cell of the font's sheet, while keys are shown, is drawn as a cap.
bool glyph_as_key(uint8_t* rdram, recomp_context* ctx) {
    if (!aidyn::input::keyboard_last()) return false;
    const gpr font = MEM_W(0, (gpr)(int32_t)0x800D968C);
    if (font == 0 || (uint32_t)ctx->r5 == 0) return false;
    for (uint32_t f = 0; f < 3; f++) {
        const gpr sub = glyph_face(rdram, font, f);
        if (sub == 0 || (uint32_t)MEM_W(4, sub) != (uint32_t)ctx->r5) continue;
        const gpr sp = ctx->r29;
        const uint32_t cw = MEM_HU(8, sub), ch = MEM_HU(0xA, sub), rows = MEM_BU(0x14, sub);
        if (cw == 0 || ch == 0 || rows == 0) return false;
        const uint16_t xoff = MEM_HU(0x12, sp), yoff = MEM_HU(0x16, sp), right = MEM_HU(0x1A, sp), bottom = MEM_HU(0x1E, sp);
        // a whole cell (a clipped one is left to the font)
        if (xoff % cw != 0 || yoff % ch != 0 || right - xoff < (int)cw - 4 || bottom - yoff != (int)ch) return false;
        const int cell = (int)((xoff / cw) * rows + yoff / ch) - kGlyphFirst;
        if (cell < 0 || cell >= kGlyphCount) return false;
        const gpr kern = (gpr)(int32_t)MEM_W(0x10, sub);
        const float width = (float)MEM_HU(2 * (kGlyphFirst + cell), kern);
        uint32_t xb = (uint32_t)ctx->r6, yb = (uint32_t)ctx->r7, xsb = (uint32_t)MEM_W(0x20, sp), ysb = (uint32_t)MEM_W(0x24, sp);
        float x, y, xs, ys;
        std::memcpy(&x, &xb, 4);
        std::memcpy(&y, &yb, 4);
        std::memcpy(&xs, &xsb, 4);
        std::memcpy(&ys, &ysb, 4);
        const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
        gpr g = (gpr)(int32_t)ctx->r4;
        // The pack's picture is the same place: x, y in the game's units, the cell `width` and 9 units wide and tall, scaled.
        g = draw_button_cap(rdram, g, kGlyphButtons[cell], x * hs, y * vs, xs * hs, ys * vs, width, 9.0f, false, true);
        ctx->r2 = (int32_t)g;
        return true;
    }
    return false;
}

std::string help_keys_text() {
    using namespace aidyn::controls;
    auto k = [](Action a) { return keys_of(a, false); };
    // With the keyboard in use the buttons are named (the symbols would turn into the keys again): "Z and X are A and B".
    const bool kb = aidyn::input::keyboard_last();
    auto sym = [&](const char* code, const char* name) { return kb ? std::string(name) : std::string("\\x") + code; };
    std::string t = "On a keyboard, " + move_keys() + " move; " + k(kA) + " and " + k(kB) + " are " + sym("7F", "A") + " and " + sym("80", "B") + "; " +
                    k(kX) + " and " + k(kY) + " are " + sym("84", "X") + " and " + sym("83", "Y") + "; " + k(kView) + " and " + k(kRT) +
                    " are " + sym("82", "View") + " and " + sym("81", "RT") + "; " + k(kLT) + ", " + k(kLB) + " and " + k(kRB) + " are " +
                    sym("87", "LT") + ", LB and RB; " + k(kMenu) + " is " + sym("85", "Menu") + ". Hold " + k(kFastForward) + " to fast-forward.";
    if (mouse_look()) {
        t += " The mouse turns the camera; " + k(kCamReset) + " puts it behind the party.";
    }
    return t + " Controls, on the ReChronicled page or the title screen, changes them.";
}

// ---- Conversation hint ----
// A conversation says nothing about how to move it on. While one is up, the bottom right of its dark
// band shows "Continue [A]", or "Choose [A]" when it offers answers: the A button's picture, or a cap with the key bound
// to A while the keyboard or mouse was used last (the picture's keyboard twin). gGlobals.diaClass (0x800D964C) -> its menu (+4, substruct
// +0x40: DiaMenuSub, Background +0xC, the answers' scroll menu +0x28). AIDYN_DIALOGUE_LOG=1 prints what it finds.
//
// Start skips a conversation, as it skips the cinematics (src/main/input.cpp: A pressed for the player, line by line,
// until answers are offered or it ends): "Skip [Start]" beside Continue, "Stop [Start]" while it skips.
namespace {
    // The conversation's band (gGlobals.diaClass's menu's Background) and its answers, or no band. A conversation the
    // story starts is gameStateA 12; talking to someone in the field (a shopkeeper) stays 1: the game's own test for one
    // is the menu (DialougeClass::FreeWidgets clears it).
    struct Conversation { gpr bg = 0; int answers = 0; };
    Conversation conversation(uint8_t* rdram) {
        Conversation c;
        const uint32_t a = MEM_BU(0, kTrekState);
        if ((a != 12 && a != 1) || heap_pointer(MEM_W(0, kTitleScreen))) return c;
        const gpr dia = MEM_W(0, (gpr)(int32_t)0x800D964C);
        const gpr menu = heap_pointer(dia) ? MEM_W(4, dia) : 0;
        const gpr sub = heap_pointer(menu) ? MEM_W(0x40, menu) : 0;
        const gpr bg = heap_pointer(sub) ? MEM_W(0xC, sub) : 0;
        if (!heap_pointer(bg)) return c;
        const gpr scroll = MEM_W(0x28, sub);
        const gpr ssub = heap_pointer(scroll) ? MEM_W(0x40, scroll) : 0;
        c.bg = bg;
        c.answers = heap_pointer(ssub) ? MEM_HU(0x20, ssub) : 0;
        return c;
    }
}

// A shop or trainer opened from a conversation (gameStateA 7 or 5) is drawn over a picture of the screen taken as it
// opens (GetSnapshot_), with the conversation's band and its last line in it: they showed under the shop page's torn
// bottom edge. The conversation is gone by then, so its band's place is kept while it is up (draw_dialogue_hint, each
// frame); when the game turns to a shop or trainer within a few frames of one, draw_screenshot_background's end covers
// that place in black (the page's edges then show dark, as they do over the sky) until it leaves the shop.
namespace {
    struct BandSeen { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; uint32_t frame = 0; bool any = false; } band_seen;
    uint32_t band_frames = 0;  // draw_dialogue_hint's calls: one a frame
}

extern "C" void aidyn_snapshot_drawn(uint8_t* rdram, recomp_context* ctx) {
    static bool cover = false;
    static uint32_t was = 0;
    const uint32_t a = MEM_BU(0, kTrekState);
    const bool shop = a == 7 || a == 5;
    if (shop && was != a) cover = band_seen.any && band_frames - band_seen.frame < 20;
    was = a;
    if (!shop || !cover) return;
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    gpr g = ctx->r2;
    g = rect_state(rdram, g);
    g = rect(rdram, g, std::max(0.0f, band_seen.x0 - 1.0f), std::max(0.0f, band_seen.y0 - 1.0f),
             std::min(320.0f, band_seen.x1 + 1.0f), std::min(240.0f, band_seen.y1 + 1.0f), 0x000000FFu, hs, vs);
    ctx->r2 = g;
}

// For the input (aidyn::input::set_dialogue): 0 no conversation, 1 a line, 2 answers offered.
int conversation_state(uint8_t* rdram) {
    const Conversation c = conversation(rdram);
    return c.bg == 0 ? 0 : c.answers > 0 ? 2 : 1;
}

gpr draw_dialogue_hint(uint8_t* rdram, recomp_context* ctx, gpr g) {
    static bool was_skipping = false;
    const bool skipping = aidyn::input::dialogue_skipping();
    if (skipping && !was_skipping) notice("Skipping the conversation: Start stops", 2500);
    was_skipping = skipping;
    const Conversation c = conversation(rdram);
    band_frames++;
    if (c.bg != 0) {  // its place, for a shop opened from it (aidyn_snapshot_drawn)
        const float bx0 = (int16_t)MEM_HU(kWidgetPosX, c.bg), by0 = (int16_t)MEM_HU(kWidgetPosX + 2, c.bg);
        band_seen = { bx0, by0, bx0 + MEM_HU(0x74, c.bg), by0 + MEM_HU(0x72, c.bg), band_frames, true };
    }
    if (c.bg == 0 || book_frames > 0 || aidyn::input::start_held() || !aidyn::settings::graphics().button_prompts) {
        return g;
    }
    const gpr bg = c.bg;
    const uint32_t alpha = MEM_BU(0x5B, bg);
    const float x0 = (int16_t)MEM_HU(kWidgetPosX, bg), y0 = (int16_t)MEM_HU(kWidgetPosX + 2, bg);
    const float x1 = x0 + MEM_HU(0x74, bg), y1 = y0 + MEM_HU(0x72, bg);
    const int answers = c.answers;
    static const bool log = std::getenv("AIDYN_DIALOGUE_LOG") != nullptr;
    if (log) {
        static int frames = 0;
        if (++frames % 30 == 0) {
            fprintf(stdout, "Dialogue: band %.0f,%.0f to %.0f,%.0f alpha %u, %d answers\n", x0, y0, x1, y1, alpha, answers);
            fflush(stdout);
        }
    }
    if (alpha < 128) return g;
    // the keyboard twins while keys are shown
    const int image = branding_index(skipping ? "dlg_stop" : answers > 0 ? "dlg_choose" : "dlg_continue");
    const int skip = skipping || answers > 0 ? -1 : branding_index("dlg_skip");
    if (image < 0) return g;
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    const float w = ink_width(image) * kPromptScale;
    copy_branding(rdram);
    g = hud_begin(rdram, g, false);
    GfxCommand a[2];
    gEXSetRectAlign(&a[0], G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, -160 * 4, 0, -160 * 4, 0);  // the 4:3 picture's own units
    g = put_cmds(rdram, g, a, 2);
    g = prompt_state(rdram, g, (uint8_t)alpha);
    g = draw_prompt(rdram, g, image, (x1 - 8.0f - w) * hs, (y1 - 13.0f) * vs, hs, vs);
    if (skip >= 0) {
        g = draw_prompt(rdram, g, skip, (x1 - 8.0f - w - 10.0f - ink_width(skip) * kPromptScale) * hs, (y1 - 13.0f) * vs, hs, vs);
    }
    return hud_end(rdram, g);
}

// ---- Report a problem ----
// The ReChronicled page's last row (spitzdano, 2026-10-21, for playtesting): a problem report (src/main/report.cpp: the logs,
// settings and saves in one .zip in reports/) with the screen as the pause menu opened (its picture, full resolution:
// save_pictures.cpp) and, paused from walking about (not a fight: the game's own Save isn't offered there), a save of
// this moment made as Save Game makes one.
bool aidyn_save_picture_jpeg(uint8_t* rdram, std::vector<uint8_t>& jpeg);  // src/game/save_pictures.cpp

void report_problem(uint8_t* rdram, recomp_context* ctx) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> extra;
    std::vector<uint8_t> jpeg;
    if (aidyn_save_picture_jpeg(rdram, jpeg)) {
        extra.emplace_back("screen.jpg", std::move(jpeg));
    }
    if (MEM_BU(0, kTrekState) != 3 && heap_pointer(MEM_W(0, kPlayer))) {  // 3: a fight's pause
        aidyn_save_pictures_off_pak(true);
        with_crc_table(rdram, ctx, [&] { return call_game(rdram, ctx, func_8001A08C, (uint32_t)kSaveBuffer); });
        aidyn_save_pictures_off_pak(false);
        std::vector<uint8_t> save(kSaveSize);
        for (uint32_t i = 0; i < kSaveSize; i++) {
            save[i] = MEM_BU(i, kSaveBuffer);
        }
        extra.emplace_back("saves/this-moment.sav", std::move(save));
    }
    const std::filesystem::path made = aidyn::report::make("Report a problem (the pause menu)", extra);
    notice(made.empty() ? "Could not write the report" : "Report saved in the reports folder");
    rc_sound(rdram, ctx, made.empty() ? 0x741 : 0x740, 60);
}

// AIDYN_JOURNAL_DUMP=<file>: each journal entry as Journal::Load decrypts it (a1: the text, its flag first), appended
// once each, to name the achievements by the game's own words (#42; developer only).
extern "C" void aidyn_journal_dump(uint8_t* rdram, recomp_context* ctx) {
    static const char* path = std::getenv("AIDYN_JOURNAL_DUMP");
    static std::set<std::string> seen;
    if (path == nullptr || !heap_pointer(ctx->r5 & ~(gpr)3)) return;
    std::string text;
    for (int i = 0; i < 0x2000; i++) {
        const char c = (char)MEM_B(i, ctx->r5);
        if (c == 0) break;
        text += c;
    }
    if (!seen.insert(text).second) return;
    std::ofstream(path, std::ios::app) << text << "\n";
}

// ---- Treasure sparkle ----
// spitzdano asked for a sparkle where something can be picked up or opened, for containers that aren't obvious: chosen
// on the New Game panel or turned on from the ReChronicled page, on for the rest of the playthrough ("Fair play"; the
// developer's AIDYN_SPARKLE=1 shows it always). While
// walking about, every container of the camera's map (gCamera.borg_9, +0x88: the map cell it is in, or the interior)
// within kReach of the party that is still to be opened (its openFlag not set) and holds something (gold, a loot table,
// an item; or a herb, spice or gem patch) gets a pulsing star at its place, projected with the camera's position and
// aim (gCamera +0, +0xC) and the game's 45-degree vertical view, if nothing of the map stands between it and the
// camera: the game's CheckCollision (as the camera keeps out of walls, "Camera and walls") on the line from the camera to
// it, a hit within kNear of it being the container's own wall or floor. Each container is looked at every 8th frame.
// voxelObject: header (pos +0, Bitfield +0x14, type +0x16), container_Dat at +0x28 (openFlag +8, LootCat +0x10, LootType +0x12, Gold
// +0x16, item +0x18).
namespace {
    constexpr gpr kSparkleTo = (gpr)(int32_t)0x80F81498, kSparkleHit = (gpr)(int32_t)0x80F814A4,
                  kSparkleNormal = (gpr)(int32_t)0x80F814B0;  // vec3f each, after the camera's ("Camera and walls")

    // Can the camera see the container at x, y, z (voxel i of the map dat)? Looked at again every 8th frame.
    bool sparkle_seen(uint8_t* rdram, recomp_context* ctx, gpr dat, gpr cam, int i, float x, float y, float z, int frame) {
        struct Seen { gpr dat; int frame; bool seen; };
        static std::map<int, Seen> cache;
        auto it = cache.find(i);
        if (it != cache.end() && it->second.dat == dat && frame - it->second.frame < 8 && frame >= it->second.frame) {
            return it->second.seen;
        }
        write_f32(rdram, kSparkleTo, x);
        write_f32(rdram, kSparkleTo + 4, y);
        write_f32(rdram, kSparkleTo + 8, z);
        const uint32_t hit = call_game_stack(rdram, ctx, CheckCollision, (uint32_t)dat, (uint32_t)cam, (uint32_t)kSparkleTo,
                                             f32_bits(0.05f), { (uint32_t)kSparkleHit, (uint32_t)kSparkleNormal, 1 }) & 0xFF;
        bool seen = true;
        if (hit != 0) {
            constexpr float kNear = 1.0f;
            const float hx = read_f32(rdram, kSparkleHit) - x, hy = read_f32(rdram, kSparkleHit + 4) - y,
                        hz = read_f32(rdram, kSparkleHit + 8) - z;
            seen = hx * hx + hy * hy + hz * hz < kNear * kNear;
        }
        static const bool debug = std::getenv("AIDYN_SPARKLE_DEBUG") != nullptr;
        if (debug && (it == cache.end() || it->second.seen != seen)) {
            fprintf(stdout, "Sparkle %d: %s\n", i, seen ? "in sight" : "behind a wall");
            fflush(stdout);
        }
        cache[i] = { dat, frame, seen };
        return seen;
    }
}

gpr treasure_sparkle(uint8_t* rdram, recomp_context* ctx, gpr g) {
    static const bool always = std::getenv("AIDYN_SPARKLE") != nullptr;
    if (!(always || sparkle_on()) || MEM_BU(0, kTrekState) != 1 || heap_pointer(MEM_W(0, kTitleScreen)) ||
        heap_pointer(MEM_W(0, (gpr)(int32_t)0x800D9558))) {
        return g;
    }
    const gpr cam = (gpr)kGlobals + 0x214;
    const gpr dat = MEM_W(0x88, cam);
    if (!heap_pointer(dat)) return g;
    const int count = MEM_HU(0x28, dat);
    const gpr voxels = MEM_W(0x44, dat);
    if (!heap_pointer(voxels)) return g;
    const int image = branding_index("sparkle");
    if (image < 0) return g;
    const float px = read_f32(rdram, cam), py = read_f32(rdram, cam + 4), pz = read_f32(rdram, cam + 8);
    const float ax = read_f32(rdram, cam + 0xC), ay = read_f32(rdram, cam + 0x10), az = read_f32(rdram, cam + 0x14);
    float fx = ax - px, fy = ay - py, fz = az - pz;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-4f) return g;
    fx /= fl, fy /= fl, fz /= fl;
    float rx = -fz, rz = fx;  // right = forward x up (y up)
    const float rl = std::sqrt(rx * rx + rz * rz);
    if (rl < 1e-4f) return g;
    rx /= rl, rz /= rl;
    const float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;  // up = right x forward
    const float tv = std::tan(22.5f * kPi / 180.0f), aspect = view_aspect();
    constexpr float kReach = 20.0f;
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    bool begun = false;
    static int logged = -1;
    int shown = 0;
    static const bool debug = std::getenv("AIDYN_SPARKLE_DEBUG") != nullptr;  // a star at the camera's aim too
    static int frame = 0;
    frame++;
    for (int i = debug ? -1 : 0; i < count && i < 512; i++) {
        const gpr v = voxels + 0x6C * (i < 0 ? 0 : i);
        float x, y, z;
        if (i < 0) {
            x = ax, y = ay, z = az;
        } else {
        if (MEM_HU(0x16, v) != 1) continue;  // VOXEL_Container
        // Active (0x8000): the game keeps it so only while it can be opened (exploding_container_check: not opened, not
        // picked, not on the voxel chart, its flags say it is there); objects that aren't can't be touched.
        if ((MEM_HU(0x14, v) & 0x8000) == 0) continue;
        const gpr c = v + 0x28;
        const int type = MEM_HU(0x12, c);
        const bool patch = type >= 4 && type <= 6;  // Treasure_Herb, _Spice, _Gem
        if (!patch && MEM_HU(0x16, c) == 0 && MEM_HU(0x10, c) == 0 && MEM_HU(0x18, c) == 0) continue;
        const uint16_t open = MEM_HU(8, c);
        if (open > 1 && flag_set(rdram, ctx, open)) continue;
        x = read_f32(rdram, v), y = read_f32(rdram, v + 4) + 0.6f, z = read_f32(rdram, v + 8);
        }
        const float dx = x - ax, dz = z - az;
        if (dx * dx + dz * dz > kReach * kReach) continue;
        const float wx = x - px, wy = y - py, wz = z - pz;
        const float depth = wx * fx + wy * fy + wz * fz;
        if (depth < 0.5f) continue;
        const float sx = (wx * rx + wz * rz) / (depth * tv * aspect), sy = (wx * ux + wy * uy + wz * uz) / (depth * tv);
        if (std::fabs(sx) > 1.1f || std::fabs(sy) > 1.1f) continue;
        const float hx = 160.0f + sx * 120.0f * aspect, hy = 120.0f - sy * 120.0f;  // the 4:3 picture's units, centred
        if (i >= 0 && !sparkle_seen(rdram, ctx, dat, cam, i, x, y, z, frame)) continue;
        if (debug && frame % 60 == 0) {
            const gpr v = voxels + 0x6C * (i < 0 ? 0 : i);
            fprintf(stdout, "Sparkle %d: at %.2f %.2f %.2f, camera %.2f %.2f %.2f aim %.2f %.2f %.2f, depth %.2f, screen %.1f %.1f, bits %04x open %u\n",
                    i, x, y, z, px, py, pz, ax, ay, az, depth, hx, hy, MEM_HU(0x14, v), MEM_HU(0x28 + 8, v));
            fflush(stdout);
        }
        const float size = std::clamp(120.0f / depth, 8.0f, 24.0f) * (0.8f + 0.2f * (float)std::sin(t * 4.0 + i));
        const uint32_t alpha = (uint32_t)(195.0 + 60.0 * std::sin(t * 3.0 + i * 1.7));
        if (!begun) {
            copy_branding(rdram);
            g = hud_begin(rdram, g, false);
            GfxCommand a[2];
            gEXSetRectAlign(&a[0], G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, -160 * 4, 0, -160 * 4, 0);
            g = put_cmds(rdram, g, a, 2);
            begun = true;
        }
        g = tinted_image_state(rdram, g, 0xFFF0C0u << 8 | alpha);
        g = draw_rgba16(rdram, g, image, (hx - size / 2.0f) * hs, (hy - size / 2.0f) * vs, size / 16.0f * hs, size / 16.0f * vs);
        shown++;
    }
    if (shown != logged) {
        logged = shown;
        fprintf(stdout, "Sparkle: %d shown (map %d voxels)\n", shown, count);
        fflush(stdout);
    }
    return begun ? hud_end(rdram, g) : g;
}

// src/main/main.cpp graphics_memory_fallback, as the game starts (before its thread runs): a note on the title screen.
void aidyn_startup_notice(const std::string& text) {
    notice(text.c_str(), 15000);
}
