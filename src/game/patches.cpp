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
extern "C" void Graphics__EndList(uint8_t* rdram, recomp_context* ctx) {
    gpr g = ctx->r4;
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

extern "C" void aidyn_ui_image_seen(uint8_t* rdram, recomp_context* ctx) {
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
// within the camera's own limits (+0x50, +0x54), by game ticks (not the host clock, so each shown frame turns
// evenly). Once the camera has been turned, func_800A68E0 (replaced below) keeps
// the camera's own direction as its "behind" point, so the camera stays where it was put and follows at the same
// distance; pressing the right stick (R3) puts it back behind the player, as the original. Movement is relative to
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

    constexpr float kTurnSpeed = 150.0f;   // degrees a second at full tilt
    constexpr float kHeightSpeed = 60.0f;  // degrees a second at full tilt
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
    }
    was_clicked = clicked;

    float rx, ry;
    aidyn::input::right_stick(&rx, &ry);
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
        const float lo = read_f32(rdram, cam + 0x50), hi = read_f32(rdram, cam + 0x54);
        float angle = read_f32(rdram, cam + 0x48) - ry * kHeightSpeed * dt;  // stick up: the camera dips, looking up
        if (lo < hi) {
            angle = std::clamp(angle, lo, hi);
        }
        write_f32(rdram, cam + 0x48, angle);
        write_f32(rdram, cam + 0x4C, angle);
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

gpr draw_title_branding(uint8_t* rdram, gpr g, float ox, float oy, float sx, float sy);  // below, "Title screen"

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
        ctx->r2 = draw_title_branding(rdram, ctx->r2, d.ox, d.oy, d.sx, d.sy);
    }
}

// Draw distance. The field is drawn with a far clip of 180 units (RenderZones, aidyn.us.toml hook), about the edge of
// the 3x3 block of map cells the game keeps loaded around the player (100 units each outdoors). settings.json's
// draw_distance multiplies it; the game's fog sits at the far end of the depth range, so it moves out with it.
extern "C" uint32_t aidyn_field_far_plane(uint32_t far_bits) {
    float far;
    std::memcpy(&far, &far_bits, sizeof(far));
    far *= aidyn::settings::graphics().draw_distance;
    std::memcpy(&far_bits, &far, sizeof(far));
    return far_bits;
}

// Widescreen clouds. Clouds::Draw positions each cloud (a 2D image) from the 3D camera, so in 16:9 some belong outside
// the 4:3 picture, but the game's scissor (0..320) and gSPScisTextureRectangle's clamp at x = 0 cut them off at the
// 4:3 edges. Three aidyn.us.toml hooks: on entry to the outdoor branch, aidyn_clouds_begin writes RT64 extended-GBI
// commands into the display list (scissor to the full window; every rectangle moved left by kCloudShift);
// before each cloud's Borg8_DrawSimple, aidyn_cloud_x moves the cloud right by kCloudShift (so its left edge is never
// below 0 and nothing gets clamped); on exit, aidyn_clouds_end restores the scissor and the rectangle offset.
#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

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

extern "C" void aidyn_anim_loop_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!aidyn::settings::graphics().smooth_animation_loops) {
        return;
    }
    ctx->r4 = MEM_W(8, ctx->r17);  // the model's scene
    Scene__Tick(rdram, ctx);
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
extern "C" void aidyn_splash_loaded(uint8_t* rdram) {
    if (aidyn::settings::game().skip_intro) {
        MEM_B(0, (gpr)(int32_t)0x800D80A8) = 0;
    }
}

// Title screen. The project is Aidyn ReChronicled: "Rechronicled" goes on the title card under THE FIRST MAGE, and
// "Enhanced by SPITZDANO" at the bottom right. Both are drawn right after the card (aidyn_image_draw_done), with the
// card's colour and fade still set. The pictures (src/game/branding_images.h, generated by tools/branding/
// make_branding.py, which also makes the "rechronicled" texture pack with their full-resolution versions) are copied
// once to RDRAM above the 8 MB the game uses, and each is loaded with one RGBA16 LoadTile and drawn 1:1.
#include "branding_images.h"

namespace {
    constexpr uint32_t kBrandingBase = 0x80800000;  // RDRAM the game never touches (it is told it has 8 MB)
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

// The version switch: the running version is aidyn::settings::game().rom_patch; View (N64 C-left) on the title screen
// restarts into the other one (src/main/main.cpp aidyn::switch_version), if its ROM is stored.
#include "version.h"

namespace {
    void title_version_switch() {
        const bool patch = aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch;
        static bool first = true, was_held = false;
        const bool held = aidyn::input::c_left_held();
        if (first) {
            first = false;
            was_held = held;  // still held from the press that switched: wait for the next one
        }
        if (held && !was_held && aidyn::version_available(!patch)) {
            aidyn::switch_version(!patch);
        }
        was_held = held;
    }
}

gpr draw_title_branding(uint8_t* rdram, gpr g, float ox, float oy, float sx, float sy) {
    copy_branding(rdram);
    title_version_switch();
    // "Rechronicled": centred under THE FIRST MAGE (card texels 26..172, rows 120..138) on the card's lower parchment.
    g = draw_rgba16(rdram, g, 0, ox + (97 - 48) * sx, oy + 139 * sy, sx, sy);
    // "Enhanced by SPITZDANO": bottom right of the whole picture, 10 texels in from the edge and the bottom. RT64
    // extended GBI: with the right edge as origin RT64 adds the frame width (320) to x, so the rectangle, drawn where it
    // would sit in 320x240, is moved back 320 by the alignment's offsets (texture rectangles can't take negative x).
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    // The game's scissor ends at the 4:3 edge, so it is widened to the whole window meanwhile (as for the clouds).
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Original) {
        g = draw_rgba16(rdram, g, 1, (310 - 140) * hs, (230 - 14) * vs, hs, vs);
        if (aidyn::version_available(aidyn::settings::game().rom_patch != aidyn::settings::RomPatch::AidynPatch)) {
            g = draw_rgba16(rdram, g, aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch ? 3 : 2,
                            10 * hs, (230 - 12) * vs, hs, vs);
        }
        return g;
    }
    GfxCommand c[5];  // gEXEnable: 1 command; gEXSetScissor, gEXSetRectAlign: 2 each
    gEXEnable(&c[0]);
    gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
    gEXSetRectAlign(&c[3], G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -320 * 4, 0, -320 * 4, 0);
    g = put_cmds(rdram, g, c, 5);
    g = draw_rgba16(rdram, g, 1, (310 - 140) * hs, (230 - 14) * vs, hs, vs);
    // "[View] Original · Aidyn Patch", the running one bright: bottom left, from the left edge of the whole picture.
    const bool patch = aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch;
    const bool both = aidyn::version_available(!patch);
    if (both) {
        gEXSetRectAlign(&c[0], G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
        g = put_cmds(rdram, g, c, 2);
        g = draw_rgba16(rdram, g, patch ? 3 : 2, 10 * hs, (230 - 12) * vs, hs, vs);
    }
    const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager), v = MEM_HU(kVres1, (gpr)gGfxManager);
    gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
    c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
    g = put_cmds(rdram, g, c, 3);
    return g;
}
