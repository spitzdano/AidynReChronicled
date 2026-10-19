// Game functions replaced by hand-written C (listed in aidyn.us.toml's `ignored` and in README.md).
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

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
gpr aidyn_rc_page(uint8_t* rdram, recomp_context* ctx, gpr g);  // "ReChronicled page", below

namespace aidyn {
    void set_gap_fill_horizon(int row);  // src/main/rt64_context.cpp
}
extern int aidyn_sky_horizon;  // "Below the horizon", below: this frame's horizon row, or -1

extern "C" void Graphics__EndList(uint8_t* rdram, recomp_context* ctx) {
    aidyn::set_gap_fill_horizon(aidyn_sky_horizon);
    aidyn_sky_horizon = -1;
    gpr g = ctx->r4;
    g = aidyn_ff_marker(rdram, g);
    g = aidyn_prompts(rdram, g);
    g = aidyn_rc_page(rdram, ctx, g);
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
#include <mutex>
#include <set>
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

extern "C" void aidyn_ui_image_seen(uint8_t* rdram, recomp_context* ctx) {
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
        return;
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
        return;
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
        return;
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
#include "input.h"
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
    if (rx != 0.0f) {
        const float a = rx * kTurnSpeed * dt * kDegToRad;  // stick right: the view turns right
        const float c = std::cos(a), s = std::sin(a);
        const float ax = read_f32(rdram, cam + 0x0C), az = read_f32(rdram, cam + 0x14);
        for (int point : { 0x00, 0x18 }) {  // pos, posTarget
            const float dx = read_f32(rdram, cam + point) - ax, dz = read_f32(rdram, cam + point + 8) - az;
            write_f32(rdram, cam + point, ax + dx * c - dz * s);
            write_f32(rdram, cam + point + 8, az + dx * s + dz * c);
        }
        free_camera = true;
    }
    if (ry != 0.0f) {
        if (std::isnan(rest_angle)) {
            rest_angle = read_f32(rdram, cam + 0x4C);
        }
        const float lo = rest_angle - kTiltRange, hi = rest_angle + kTiltRange;
        float angle = read_f32(rdram, cam + 0x48) - ry * kTurnSpeed * dt;  // stick up: the camera dips, looking up
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
static void log_far(uint8_t* rdram, recomp_context* ctx, float far, float edge) {
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
            far, std::min(edge, 9999.0f), free_bytes / 1024, props_tried / 60, props_original / 60, props_scaled / 60);
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
    const float far = std::clamp(current, original, original * scale);
    far_clip_now = far;
    const float half = half_view_degrees();
    write_f32_at(rdram, 0x800CD6F4, far * (200.0f / 180.0f));                  // cell wedge length (200 at 180)
    write_f32_at(rdram, 0x800CD6FC, std::max(42.5f, half + 13.6f));            // cell wedge angle, degrees
    write_f32_at(rdram, 0x800CD6A0, std::max(37.5f, half + 8.6f) * kPi / 180.0f);   // props: fading from
    write_f32_at(rdram, 0x800CD6A4, std::max(42.5f, half + 13.6f) * kPi / 180.0f);  // props: gone at
    log_far(rdram, ctx, far, edge);
    uint32_t bits;
    std::memcpy(&bits, &far, sizeof(far));
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
        const float own = std::min(size, read_f32(rdram, prop + 0x28 + 12 * (models - 1)));
        const float nearer = models >= 2 ? read_f32(rdram, prop + 0x28 + 12 * (models - 2)) : 0.0f;
        items_seen++;
        items_before += prox / scale < own;
        if (prox >= own && prox < reach(own)) {
            prox = (nearer + own) * 0.5f;
        }
        items_now += prox < own;
    } else {
        prox /= scale;
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
// (their ground only: no props, people or collision), out to radius R (5x5 at R = 2 ... 9x9 at R = 4, so the far
// clip, which follows the loaded edge, can reach 180 x draw_distance):
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

    void free_far_cell(uint8_t* rdram, recomp_context* ctx, int k) {
        if (!far_cells[k].used) {
            return;
        }
        far_cells[k].used = false;
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
    bool far_cell_visible(uint8_t* rdram, int dx, int dz, float far) {
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
                if ((x * fx + z * fz) / d >= cosv && d * ((x * fx + z * fz) / d) <= far + 10.0f) {
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
// slightly out of place: a one-frame flicker each time a boundary is crossed (Dan). So on the frame the centre cell
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

extern "C" void aidyn_scene_draw_begin(uint8_t* rdram, recomp_context* ctx) {
    if (!interpolating()) {
        return;
    }
    GfxCommand c[1];
    gEXEnable(&c[0]);
    ctx->r4 = put_cmds(rdram, ctx->r4, c, 1);  // a0 = the display list pointer, before the function copies it
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

// DCM::VoiceHandler's entry (audio thread).
extern "C" void aidyn_dcm_tick(uint8_t* rdram, recomp_context* ctx) {
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
        return g;
    }
}

// The version list, bottom left of the title screen: Original, Aidyn Patch and Aidyn Plus, one a line. The running one
// (aidyn::settings::game().rom_patch) has a copper marker and full brightness. Of the other two the upper one gets X
// (N64 C-down; keyboard K) and the lower one Y (C-up; keyboard I): pressing it restarts into that version
// (src/main/main.cpp aidyn::switch_version). One whose ROM isn't stored (each is the player's own, checked by its
// hash) is grey with "not found" and no button. The pictures are make_branding.py's ver_*, tinted with the
// primitive colour and faded with the card.
#include "version.h"

namespace {
    uint32_t ink_width(int index);  // "Button prompts", below

    using aidyn::settings::RomPatch;
    constexpr RomPatch kVersions[3] = { RomPatch::None, RomPatch::AidynPatch, RomPatch::AidynPlus };
    constexpr const char* kVersionImages[3] = { "ver_original", "ver_patch", "ver_plus" };

    int title_image(const char* name) {
        for (size_t i = 0; i < std::size(aidyn::branding::kImages); i++) {
            if (std::strcmp(aidyn::branding::kImages[i].name, name) == 0) {
                return (int)i;
            }
        }
        return -1;
    }

    // Stored ROMs don't change while the game runs: looked up once.
    bool version_here(int i) {
        static const std::array<bool, 3> here = { aidyn::version_available(kVersions[0]),
                                                  aidyn::version_available(kVersions[1]),
                                                  aidyn::version_available(kVersions[2]) };
        return here[i];
    }

    // The two versions not running, top to bottom (X, Y), as indices into kVersions.
    std::array<int, 2> other_versions() {
        std::array<int, 2> o{};
        int n = 0;
        for (int i = 0; i < 3; i++) {
            if (kVersions[i] != aidyn::settings::game().rom_patch) {
                o[n++] = i;
            }
        }
        return o;
    }

    void title_version_switch() {
        static bool first = true, x_was = false, y_was = false;
        const bool x = aidyn::input::c_down_held(), y = aidyn::input::c_up_held();
        if (first) {
            first = false;  // still held from the press that switched: wait for the next one
            x_was = x;
            y_was = y;
        }
        const std::array<int, 2> o = other_versions();
        if (x && !x_was && version_here(o[0])) {
            aidyn::switch_version(kVersions[o[0]]);
        }
        if (y && !y_was && version_here(o[1])) {
            aidyn::switch_version(kVersions[o[1]]);
        }
        x_was = x;
        y_was = y;
    }

    // The three lines from (10, 194), 12 texels apart; hs, vs pixels a texel; card_rgba the card's colour (its alpha
    // fades them in with it).
    gpr version_list(uint8_t* rdram, gpr g, float hs, float vs, uint32_t card_rgba) {
        const float fade = (card_rgba & 0xFF) / 255.0f;
        auto tint = [&](uint32_t rgb, float alpha) {
            put(rdram, g, 0xFA000000, rgb << 8 | (uint32_t)std::lround(255.0f * alpha * fade));  // gDPSetPrimColor
        };
        const std::array<int, 2> o = other_versions();
        const int note = title_image("ver_missing");
        for (int i = 0; i < 3; i++) {
            const float y = 194.0f + 12.0f * i;
            const bool running = kVersions[i] == aidyn::settings::game().rom_patch, here = version_here(i);
            const int label = title_image(kVersionImages[i]);
            const int icon = running ? title_image("ver_mark") : here ? title_image(i == o[0] ? "ver_x" : "ver_y") : -1;
            if (running) {
                tint(0xFFFFFF, 1.0f);
            } else if (here) {
                tint(0xD2CDC3, 0.95f);
            } else {
                tint(0x7D7870, 0.8f);
            }
            if (icon >= 0) {
                g = draw_rgba16(rdram, g, icon, 10.0f * hs, y * vs, hs, vs);
            }
            if (label >= 0) {
                g = draw_rgba16(rdram, g, label, 22.0f * hs, y * vs, hs, vs);
                if (!here && note >= 0) {
                    g = draw_rgba16(rdram, g, note, (22.0f + ink_width(label) + 4.0f) * hs, y * vs, hs, vs);
                }
            }
        }
        put(rdram, g, 0xFA000000, card_rgba);  // the card's own again
        return g;
    }
}

// The version number, bottom right above the credit: the build's (aidyn_version.h, written by tools/version.cmake:
// "v0.4.0" for a release, "v0.4.0 dev" for a build after its tag, "dev" without either) put together from
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
            } else if (text[i] == ' ') {
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
    bool redrawn_art();  // src/main/rt64_context.cpp: false while F2 has the redrawn art pack off
}

// The redrawn title card (art/titles, tools/branding/make_logo.py: Dan's redesign) has RECHRONICLED in it: on screen
// while textures are on, the art pack is listed and F2 hasn't turned it off.
bool redesigned_card() {
    const auto& gfx = aidyn::settings::graphics();
    return gfx.textures && aidyn::redrawn_art()
        && std::find(gfx.texture_packs.begin(), gfx.texture_packs.end(), "rechronicled-art") != gfx.texture_packs.end();
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
        return version_list(rdram, g, hs, vs, rgba);
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
    return g;
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

// Fast-forward. Every mode of the game (walking, combat, menus, cutscenes) measures its frame's time step by counting
// the controller samples queued since the last frame: Controller::GetInput pops one and returns 1, and the frame calls
// it until it returns 0, then steps that many ticks of 1/60 s (capped at 6: made for the N64's 10 to 20 frames a
// second). aidyn.us.toml hooks GetInput's entry (the port) and its return: while fast-forward is on, every real sample
// is followed by (speed - 1) quiet ones, copies of it without its new presses (same stick, held buttons and hold
// times), so the game runs that many ticks a frame and no press is handled twice. Fast-forward is L3 or a held Tab
// (src/main/input.cpp), at game.fast_forward_speed; enemy turns in combat get game.combat_enemy_speed on their own:
// combat_controls (the combat frame's sample loop) is hooked on entry and looks at gCombatP->current_Ent->aiP (set for
// enemies and the AI-led Sholeh). While the player's fast-forward is on, a marker shows at the top right.
namespace {
    constexpr gpr kQuietSample = (gpr)(int32_t)0x80F81000;  // one ControllerFull (0x34 bytes), above the game's 15 MB
    constexpr int kControllerFull = 0x34;
    constexpr int kPressed = 0x8;                            // controller_aidyn.pressed
    constexpr gpr kCombatP = (gpr)(int32_t)0x800E0BF0;       // gCombatP
    constexpr int kCurrentEnt = 0x2C, kAiP = 0x44;           // CombatStruct.current_Ent, CombatEntity.aiP
    int ff_port = 0;
    int ff_pending = 0;   // quiet samples still to hand out this frame
    int ff_combat = 1;    // speed for the combat_controls call under way (an enemy's turn)
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
    int speed = 1;
    const gpr combat = (gpr)(int32_t)MEM_W(0, kCombatP);
    if (combat != 0) {
        const gpr ent = (gpr)(int32_t)MEM_W(kCurrentEnt, combat);
        if (ent != 0 && MEM_W(kAiP, ent) != 0) {
            speed = aidyn::settings::game().combat_enemy_speed;
        }
    }
    ff_combat = speed;
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
        const int list = (uint8_t)(MEM_BU(0, kCombatState) - 10) < 2 ? kTargetsSpell : kTargetsAttack;
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

namespace {
    bool hd_text() {
        static const bool on = [] {
            const auto& packs = aidyn::settings::graphics().texture_packs;
            std::error_code ec;
            const std::filesystem::path beside =
                std::filesystem::read_symlink("/proc/self/exe", ec).parent_path() / "texture-packs";
            for (auto name = packs.rbegin(); name != packs.rend(); ++name) {
                if (*name != "rechronicled-text" && *name != "hd-ui") {
                    continue;
                }
                for (const std::filesystem::path& dir : { aidyn::settings::folder() / "texture-packs", beside }) {
                    if (std::filesystem::is_directory(dir / *name, ec)) {
                        std::ifstream in(dir / *name / aidyn::font::kPackMarker);
                        std::string id;
                        in >> id;
                        if (id == aidyn::font::kWidthsId) {
                            return true;
                        }
                        fprintf(stderr, "HD text: %s was drawn for other letter widths (%s): the game keeps its own. "
                                "rechronicled-text from this release (or a newer hd-ui pack) gives the HD text its "
                                "spacing.\n", (dir / *name).c_str(), id.empty() ? "an older pack" : id.c_str());
                        return false;
                    }
                    if (std::filesystem::exists(dir / (*name + ".rtz"), ec)) {
                        return false;  // a packed pack can't be checked: the original widths fit any letters
                    }
                }
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
// View (N64 C-left; keyboard J) shows and hides it in those two modes and isn't passed to the game there (it only
// cycled combat targets, which the stick and RT still do); src/main/input.cpp, saved as settings.json
// graphics.controls_card. Page prompts: the pause pages' arrows (SliderArrowL / LR / R, told apart by their pixels,
// at the bottom of the page, and the one painted into the map page's background) get LB and RB at the ends they
// point to. Both are drawn at the end of the frame (Graphics__EndList) with their own
// render state, from the pictures in src/game/branding_images.h (tools/branding/make_branding.py, which also puts
// their full-resolution versions in the rechronicled pack).
namespace {
    constexpr uint32_t kGameStateA = 0x800D80C8;  // gGlobals.gameStateA (u8)

    int branding_index(const char* name) {
        for (size_t i = 0; i < std::size(aidyn::branding::kImages); i++) {
            if (std::strcmp(aidyn::branding::kImages[i].name, name) == 0) {
                return (int)i;
            }
        }
        return -1;
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
        return w;
    }

    // One-cycle, filtered, blended; texture colour, texture alpha times the primitive colour's alpha.
    gpr prompt_state(uint8_t* rdram, gpr g, uint8_t alpha) {
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
        for (int k = 0; k < n; k++) width += row[k] < 0 ? 0.0f : ink_width(row[k]) + 6.0f;
        if (centred) x -= (width - 6.0f) / 2.0f;
        g = prompt_state(rdram, g, origin.alpha);
        for (int k = 0; k < n; k++) {
            if (row[k] < 0) continue;
            g = draw_rgba16(rdram, g, row[k], origin.x + x * origin.sx, origin.y + y * origin.sy, origin.sx, origin.sy);
            x += ink_width(row[k]) + 6.0f;
        }
        return g;
    }

    struct Card {
        const char* rows[8];
        bool bottom;
    };
    const Card kFieldCard = { { "ctl_walk", "ctl_camera", "ctl_talk", "ctl_party", "ctl_sneak", "ctl_ff", "ctl_menu", nullptr },
                              false };
    const Card kCombatCard = { { "ctl_move", "ctl_attack", "ctl_magic", "ctl_endturn", "ctl_combatcam", "ctl_ff",
                                 "ctl_hide", nullptr }, true };

    gpr draw_card(uint8_t* rdram, gpr g, const Card& card) {
        constexpr float kMargin = 6.0f, kPad = 2.0f, kMessagePortraitX = 25.0f, kMessageTop = 178.0f;
        int rows[8], n = 0;
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
        g = hud_begin(rdram, g, false);
        g = prompt_state(rdram, g, 150);
        g = draw_rgba16(rdram, g, panel, x0 * hs, y0 * vs, (width + 2 * kPad) / p.w * hs, (height + 2 * kPad) / p.h * vs);
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
// func_80084BA0, a0 = the sample, pressed at +8; RT is C-right, and View, the C-left, shows and hides the controls
// card in combat).
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

gpr aidyn_prompts(uint8_t* rdram, gpr g) {
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
    if (mode == 0 && page_arrows.empty() && !treasure_title.seen && !inventory_page.seen && !combat_menu_box.seen) {
        return g;
    }
    copy_branding(rdram);
    // Not while Menu is held: pausing keeps a picture of the screen as the pause menu's background.
    if (mode != 0 && aidyn::input::controls_card() && !aidyn::input::start_held()) {
        g = draw_card(rdram, g, mode == 1 ? kFieldCard : kCombatCard);
    }
    if (!aidyn::settings::graphics().button_prompts) {  // the ReChronicled page's Button prompts: off, only the card
        page_arrows.clear();
        treasure_title.seen = false;
        inventory_page.seen = false;
        combat_menu_box.seen = false;
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
        // A shop: A buys from its list and sells from the party's; Y appraises (the shop's list only). Its row leaves
        // B out (four don't fit the panel, and how a shop is left isn't in the barter widget).
        const bool loot_list = barter_widget == 0 || MEM_W(0x88, barter_widget) == MEM_W(0x80, barter_widget);
        const bool shop = treasure_title.shop;
        const int row[] = { branding_index(shop ? (loot_list ? "hint_buy" : "hint_sell")
                                                : (loot_list ? "hint_take" : "hint_putback")),
                            shop && loot_list ? branding_index("hint_appraise") : -1,
                            shop ? -1 : branding_index("hint_leave"), branding_index("hint_switch") };
        float width = 0.0f;
        for (int i : row) {
            width += i < 0 ? 0.0f : ink_width(i) + 6.0f;
        }
        const TitleSeen& t = treasure_title;
        float x = t.x + (80.0f - 48.0f - (width - 6.0f) / 2.0f) * t.sx;
        const float y = t.y + (192.0f - 68.0f) * t.sy;
        g = prompt_state(rdram, g, t.alpha);
        for (int i : row) {
            if (i >= 0) {
                g = draw_rgba16(rdram, g, i, x, y, t.sx, t.sy);
                x += (ink_width(i) + 6.0f) * t.sx;
            }
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
            // On the row of the page arrow (y 207): A at the bottom left, under the paper doll and clear of the LB
            // prompt (x 114 on); the lists between the RB prompt (to x 185) and DETAILS (x 242 on).
            const int a = shown != 0 && shown == inventory && heap_pointer(inventory) ? inventory_a_hint(rdram, inventory) : -1;
            g = draw_hint_row(rdram, g, inventory_page, &a, 1, 8.0f, 208.0f, false);
            g = draw_hint_row(rdram, g, inventory_page, &lists_hint, 1, 193.0f, 208.0f, false);
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
    treasure_title.seen = false;
    inventory_page.seen = false;
    combat_menu_box.seen = false;
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
// The strip is an ornate bar of pieces cut from Dan's concept (tools/branding/turn_order.py; branding_images.h has
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

    // The strip's pieces, cut from Dan's concept (tools/branding/turn_order.py): their pictures (branding_images.h)
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
    // Where each piece goes (units, shift included), and a slot's portrait: filling its window, centred (the gold
    // frame's window is a little taller than wide: the portrait's sides go under the frame).
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
        q.ps = std::max(q.wx1 - q.wx0, q.wy1 - q.wy0);
        q.targeted = targeted;
        placed.push_back(q);
    }

    // A slot without a picture: its window backed dark.
    g = rect_state(rdram, g);
    for (const Placed& q : placed) {
        if (q.it->kind == StripItem::Slot && q.it->e->image == 0) {
            g = rect(rdram, g, q.wx0, q.wy0, q.wx1, q.wy1, q.it->dim ? 0x141414FF : 0x1C1C1CFF, hs, vs);
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
// change is applied at once and written into settings.json: textures (every pack, HD save pictures, redrawn page
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

namespace {
    // Our entries' ids (the game's are 0xA0..0x17A and 9999), their texts and their pages' titles.
    constexpr uint16_t kRcId = 0x7A11, kDisplayId = 0x7A12;
    constexpr gpr kRcText = (gpr)(int32_t)0x80F81100;           // the entries' texts (above the game's 15 MB), 0x20 each
    enum RcKind { kRcGameplay = 0, kRcDisplay = 1 };
    constexpr const char* kRcEntryText[] = { "ReChronicled", "Display" };
    constexpr uint16_t kRcEntryId[] = { kRcId, kDisplayId };
    constexpr gpr kBigAssMenu = (gpr)(int32_t)0x800D9558;       // gGlobals.BigAssMenu (the pause menu)
    constexpr gpr kSfx = (gpr)(int32_t)0x800D9380;              // gGlobals.SFXStruct
    // The list's colours (MakeScrollList: SetColor(COLOR_RED1), SetScrollMenuColors(COLOR_BROWN2, COLOR_PURPLE1, 0x14)):
    // red-brown, the highlighted entry pulsing from dark brown to purple and back over 20 frames each way.
    constexpr uint32_t kMenuInk = 0x825050, kMenuHighlight0 = 0x442A22, kMenuHighlight1 = 0x978DBF;
    constexpr float kRowsLeft = 76.0f, kRowsRight = 286.0f, kRowsTop = 54.0f, kRowStep = 16.0f;  // N64 pixels, 4:3

    struct RcRow {
        const char* label;
        bool* flag;           // a switch; nullptr: the draw distance
        const char* key;      // in settings.json's "graphics"
        void (*applied)(uint8_t* rdram);
        bool (*shown)() = nullptr;  // what the row shows when not just *flag (Widescreen: the picture really wide)
    };

    constexpr float kDistances[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
    constexpr const char* kDistanceWords[] = { "rc_dd_1", "rc_dd_15", "rc_dd_2", "rc_dd_25", "rc_dd_3" };

    std::vector<RcRow> rc_rows(int kind) {
        auto& gfx = aidyn::settings::graphics_rw();
        if (kind == kRcDisplay) {
            return {
                { "rc_widescreen", &gfx.widescreen, "widescreen",
                  [](uint8_t*) { aidyn::settings::apply_widescreen(); aidyn::refresh_aspect(); },
                  [] { return aidyn::settings::graphics().aspect != aidyn::settings::Aspect::Original; } },
                { "rc_textures", &gfx.textures, "textures", [](uint8_t* rdram) { aidyn::refresh_textures(); apply_font_widths(rdram); } },
                { "rc_vsync", &gfx.vsync, "vsync", [](uint8_t*) { aidyn::refresh_vsync(); } },
                { "rc_hud_edges", &gfx.hud_at_edges, "hud_at_edges", nullptr },
                { "rc_draw_distance", nullptr, "draw_distance", nullptr },
            };
        }
        return {
            { "rc_turn_order", &gfx.turn_order, "turn_order", nullptr },
            { "rc_controls_card", &gfx.controls_card, "controls_card", nullptr },
            { "rc_button_prompts", &gfx.button_prompts, "button_prompts", nullptr },
            { "rc_smooth_loops", &gfx.smooth_animation_loops, "smooth_animation_loops", nullptr },
            { "rc_stick_camera", &gfx.right_stick_camera, "right_stick_camera", nullptr },
            { "rc_invert_x", &gfx.camera_invert_x, "camera_invert_x", nullptr },
            { "rc_invert_y", &gfx.camera_invert_y, "camera_invert_y", nullptr },
        };
    }

    bool rc_on(const RcRow& r) {
        return r.shown != nullptr ? r.shown() : *r.flag;
    }

    int distance_index() {
        const float dd = aidyn::settings::graphics().draw_distance;
        int best = 0;
        for (int i = 0; i < (int)std::size(kDistances); i++) {
            if (std::fabs(kDistances[i] - dd) < std::fabs(kDistances[best] - dd)) best = i;
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
    int rc_row[2] = { 0, 0 };             // each page's highlighted row, kept between visits

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
            } else {
                aidyn::settings::save_bool("graphics", r.key, *r.flag);
            }
        } else {
            const int n = (int)std::size(kDistances);
            gfx.draw_distance = kDistances[(distance_index() + step + n) % n];
            aidyn::settings::save_number("graphics", r.key, gfx.draw_distance);
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

extern "C" void aidyn_rc_entry_end(uint8_t* rdram, recomp_context* ctx) {
    if (!rc_entry_added) {
        aidyn_rc_entry(rdram, ctx);  // a battle's list: Theater isn't in it
    }
    rc_entry_added = false;
}

// PauseMenuSelectCallback, the Menu page's entry chosen: v1 its id, s0 the substruct, s2 the WidgetOptionsMenu.
extern "C" int aidyn_rc_select(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = ctx->r3 & 0xFFFF;
    if ((id != kRcId && id != kDisplayId) || rc.open) {
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
    return 1;
}

// Graphics__EndList: the page, over everything.
gpr aidyn_rc_page(uint8_t* rdram, recomp_context* ctx, gpr g) {
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
    const std::vector<RcRow> rows = rc_rows(rc.kind);
    int& row = rc_row[rc.kind];
    row = std::min(row, (int)rows.size() - 1);
    if (!rc.closing && rc.fade == 255) {
        if (act & 0x0800) { row = (row + (int)rows.size() - 1) % (int)rows.size(); rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x0400) { row = (row + 1) % (int)rows.size(); rc_sound(rdram, ctx, 0x73B, 30); }
        if (act & 0x8000) { rc_change(rdram, rows[row], 1); rc_sound(rdram, ctx, 0x740, 60); }
        else if (act & 0x0300) { rc_change(rdram, rows[row], (act & 0x0200) ? -1 : 1); rc_sound(rdram, ctx, 0x740, 60); }
        if (pressed & (0x4000 | 0x1000)) {  // B, Start: back to the list
            rc.closing = true;
            rc_sound(rdram, ctx, 0x741, 60);
        }
    }
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
    for (int k = 0; k < (int)rows.size(); k++) {
        const RcRow& r = rows[k];
        const float y = kRowsTop + kRowStep * k;
        ink(k == row ? rc_highlight() : kMenuInk);
        word(r.label, kRowsLeft, y, false);
        word(r.flag != nullptr ? (rc_on(r) ? "rc_on" : "rc_off") : kDistanceWords[distance_index()], kRowsRight, y, true);
    }
    g = prompt_state(rdram, g, (uint8_t)rc.fade);
    const int change = branding_index("rc_hint_change"), back = branding_index("rc_hint_back");
    if (change >= 0 && back >= 0) {
        const float gap = 24.0f, x = centre - (ink_width(change) + gap + ink_width(back)) / 2.0f;
        const float y = kRowsTop + kRowStep * rows.size() + 6.0f;
        word("rc_hint_change", x, y, false);
        word("rc_hint_back", x + ink_width(change) + gap, y, false);
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
// C-left to pick a target (View shows / hides the controls card here: RT does it). The L and R icons (\x88, \x89)
// don't show in the book's text, so LB and RB are written as words. get_borg13 is hooked as it returns
// the loaded book: its commands and text are copied above the game's 15 MB with those words changed and a
// ReChronicled topic added before Exit Help (this edition's camera, fast-forward, controls card, menus, combat and
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
            "again to go back to normal speed. Two chevrons at the top right of the screen show when it is on.",
            "In combat, the enemies' turns also run twice as fast on their own, so you spend less time waiting for "
            "your next turn." } },
        { "Controls Card", {
            "While you walk and in combat, a card at the left edge of the screen lists what the buttons do there. "
            "Press \\x82 to hide it or show it again; your choice is remembered.",
            "The menus also show button hints, such as \\x7F Equip or Lists. The card and the hints can each be "
            "turned off on the ReChronicled page of the Menu." } },
        { "Menus", {
            "In the pause menu, LB and RB turn the scroll to the previous or next page. On the equipment page, "
            "\\x82 and \\x81 choose the previous or next member of your party.",
            "The Menu page's Display and ReChronicled entries hold this edition's options: widescreen, the new "
            "textures, the draw distance, the turn order and more. Each change takes effect at once." } },
        { "Combat", {
            "A strip across the top of the screen shows the order of the round's turns, from whoever acts now, then "
            "the next round, dimmed. Quick fighters act two or three times a round. Whoever you are targeting flashes "
            "on their portrait and in the strip.",
            "Hold \\x84 for spells and skills, or \\x83 for items, choose with the left stick and let go to use it; a "
            "quick tap uses the last one again. \\x87 or LB ends a character's turn.",
            "With a bow or a thrown weapon, \\x86 left and right step through the targets in reach, and the aiming "
            "marker follows how far you push the left stick, so a light push aims finely." } },
        { "Keyboard and PC", {
            "On a keyboard the arrows move; Z and X are \\x7F and \\x80; K and I are \\x84 and \\x83; J and L are "
            "\\x82 and \\x81; C, Q and E are \\x87, LB and RB; Enter is \\x85. Hold Tab to fast-forward.",
            "F1 opens this edition's manual in your web browser. F11, or Alt and Enter, switches between a window and "
            "the full screen." } },
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
            const int p = add({ page });
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
    if (total > 255) {
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
