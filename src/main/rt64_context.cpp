#include "platform.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "hle/rt64_application.h"
#include "hle/rt64_present_queue.h"
#include "render/rt64_texture_cache.h"
#include "settings.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"

#include "rt64_context.h"

static RT64::UserConfiguration::Antialiasing device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
static bool sample_positions_supported = false;
static bool high_precision_fb_enabled = false;

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

unsigned int MI_INTR_REG = 0;

unsigned int DPC_START_REG = 0;
unsigned int DPC_END_REG = 0;
unsigned int DPC_CURRENT_REG = 0;
unsigned int DPC_STATUS_REG = 0;
unsigned int DPC_CLOCK_REG = 0;
unsigned int DPC_BUFBUSY_REG = 0;
unsigned int DPC_PIPEBUSY_REG = 0;
unsigned int DPC_TMEM_REG = 0;

void dummy_check_interrupts() {}

RT64::UserConfiguration::Antialiasing compute_max_supported_aa(plume::RenderSampleCounts bits) {
    if (bits & plume::RenderSampleCount::Bits::COUNT_2) {
        if (bits & plume::RenderSampleCount::Bits::COUNT_4) {
            if (bits & plume::RenderSampleCount::Bits::COUNT_8) {
                return RT64::UserConfiguration::Antialiasing::MSAA8X;
            }
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    };
    return RT64::UserConfiguration::Antialiasing::None;
}

RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:
            return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:
            return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:
            return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount:
            return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
}

RT64::UserConfiguration::Antialiasing to_rt64(ultramodern::renderer::Antialiasing option) {
    switch (option) {
        case ultramodern::renderer::Antialiasing::None:
            return RT64::UserConfiguration::Antialiasing::None;
        case ultramodern::renderer::Antialiasing::MSAA2X:
            return RT64::UserConfiguration::Antialiasing::MSAA2X;
        case ultramodern::renderer::Antialiasing::MSAA4X:
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        case ultramodern::renderer::Antialiasing::MSAA8X:
            return RT64::UserConfiguration::Antialiasing::MSAA8X;
        case ultramodern::renderer::Antialiasing::OptionCount:
            return RT64::UserConfiguration::Antialiasing::OptionCount;
    }
}

RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:
            return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:
            return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:
            return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount:
            return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
}

RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:
            return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:
            return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:
            return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount:
            return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
}

RT64::EnhancementConfiguration::Presentation::Mode to_rt64(ultramodern::renderer::PresentationMode mode) {
    switch (mode) {
        case ultramodern::renderer::PresentationMode::Console:
            return RT64::EnhancementConfiguration::Presentation::Mode::Console;
        case ultramodern::renderer::PresentationMode::SkipBuffering:
            return RT64::EnhancementConfiguration::Presentation::Mode::SkipBuffering;
        case ultramodern::renderer::PresentationMode::PresentEarly:
            return RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
    }
}

// True while a full-screen menu is up and settings.json's menus_4_3 asks for 4:3 there (see send_dl).
static bool menu_4_3 = false;

void set_application_user_config(RT64::Application* application, const ultramodern::renderer::GraphicsConfig& config) {
    // Resolution and aspect ratio come from settings.json (src/main/settings.h), not the runtime's presets.
    const aidyn::settings::Graphics& player = aidyn::settings::graphics();
    // Supersampling: RT64 draws at resolutionMultiplier and shrinks the result by downsampleMultiplier, so both grow by
    // the factor. It smooths what MSAA leaves (edges cut by texture alpha, such as hair and leaves, and thin geometry).
    // With render_height 0 the window's integer scale is used as is (RT64 picks it per frame), without supersampling.
    if (player.render_height == 0) {
        application->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        application->userConfig.downsampleMultiplier = 1;
    }
    else {
        application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
        application->userConfig.resolutionMultiplier = player.render_height / 240.0 * player.supersampling;
        application->userConfig.downsampleMultiplier = player.supersampling;
    }

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0/9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    switch (menu_4_3 ? aidyn::settings::Aspect::Original : player.aspect) {
        case aidyn::settings::Aspect::Ratio:
            application->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.aspectTarget = player.aspect_ratio;
            break;
        case aidyn::settings::Aspect::Window:
            application->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
        case aidyn::settings::Aspect::Original:
        default:
            application->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
    }
    application->userConfig.antialiasing = to_rt64(config.msaa_option);
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
}

ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:
            return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound:
            return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:
            return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:
            return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:
            return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }

    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:
            return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:
            return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:
            return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic:
            return ultramodern::renderer::GraphicsApi::Auto;
        default:
            break;
    }

    fprintf(stderr, "Unhandled `RT64::UserConfiguration::GraphicsAPI` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

// Texture packs from settings.json, looked up in <data>/texture-packs, then in texture-packs next to the executable
// (a release ships its own packs there). One that isn't in either is reported and skipped: the default list names
// the HD packs, which only exist once they have been built or copied to this machine. Without `art`, the redrawn
// art pack (rechronicled-art) is left out: F2 switches it to compare (aidyn::toggle_redrawn_art).
static std::vector<RT64::ReplacementDirectory> texture_packs(bool art, bool report) {
    std::vector<RT64::ReplacementDirectory> packs;
    if (!aidyn::settings::graphics().textures) {
        return packs;  // textures off (the pause menu's ReChronicled page): the game's own, as on the N64
    }
    std::error_code ec;
    const std::filesystem::path beside = aidyn::platform::exe_path().parent_path() / "texture-packs";
    for (const std::string& name : aidyn::settings::graphics().texture_packs) {
        if (!art && name == "rechronicled-art") {
            continue;
        }
        std::filesystem::path pack;
        for (const std::filesystem::path& dir : { aidyn::settings::folder() / "texture-packs", beside }) {
            for (const std::filesystem::path& candidate : { dir / name, dir / (name + ".rtz") }) {
                if (pack.empty() && !dir.empty() && std::filesystem::exists(candidate, ec)) {
                    pack = candidate;
                }
            }
        }
        if (pack.empty()) {
            if (report) {
                fprintf(stderr, "Texture pack \"%s\" not found in %s or %s (skipped)\n", name.c_str(),
                        (aidyn::settings::folder() / "texture-packs").string().c_str(), beside.string().c_str());
            }
            continue;
        }
        packs.emplace_back(pack);
        if (report) {
            fprintf(stdout, "Texture pack: %s\n", pack.string().c_str());
        }
    }
    return packs;
}

static std::atomic_bool redrawn_art_on{ true }, redrawn_art_reload{ false };

// The renderer that takes save pictures' copies (aidyn::capture_framebuffer).
static std::atomic<RT64::Application*> capture_app{ nullptr };

void aidyn::toggle_redrawn_art() {
    const auto& packs = aidyn::settings::graphics().texture_packs;
    if (std::find(packs.begin(), packs.end(), "rechronicled-art") == packs.end()) {
        fprintf(stdout, "Redrawn art: rechronicled-art isn't in settings.json's texture_packs\n");
        return;
    }
    redrawn_art_on = !redrawn_art_on;
    redrawn_art_reload = true;
}

bool aidyn::redrawn_art() {
    return redrawn_art_on;
}

aidyn::RT64Context::RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, ultramodern::renderer::PresentationMode presentation_mode, bool debug) {
    static unsigned char dummy_rom_header[0x40];

    // Set up the RT64 application core fields.
    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    appCore.window = window_handle;
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    appCore.DPC_START_REG = &DPC_START_REG;
    appCore.DPC_END_REG = &DPC_END_REG;
    appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
    appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
    appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
    appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
    appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
    appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

    ultramodern::renderer::ViRegs *vi_regs = ultramodern::renderer::get_vi_regs();
    appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
    appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
    appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
    appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
    appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
    appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
    appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
    appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

    // Set up the RT64 application configuration fields.
    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;
    // No RT64 data folder: it would only make an empty ~/.config/.rt64 (no config file, no log).
    appConfig.detectDataPath = false;

    // Create the RT64 application.
    app = std::make_unique<RT64::Application>(appCore, appConfig);

    // Set initial user config settings based on the current settings.
    auto& cur_config = ultramodern::renderer::get_graphics_config();
    set_application_user_config(app.get(), cur_config);
    app->userConfig.developerMode = debug;
    // Force gbi depth branches to prevent LODs from kicking in.
    app->enhancementConfig.f3dex.forceBranch = true;
    // Scale LODs based on the output resolution.
    app->enhancementConfig.textureLOD.scale = true;
    // Set the target presentation mode.
    app->enhancementConfig.presentation.mode = to_rt64(presentation_mode);
    // Pick an API if the user has set an override.
    switch (cur_config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
            break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
            break;
        case ultramodern::renderer::GraphicsApi::Metal:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal;
            break;
        case ultramodern::renderer::GraphicsApi::Auto:
        default:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
            break;
    }

    // Set up the RT64 application.
    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    aidyn::platform::startup_mark("renderer setting up");
    setup_result = map_setup_result(app->setup(thread_id));
    aidyn::platform::startup_mark("renderer ready");
    // Get the API that RT64 chose.
    chosen_api = map_graphics_api(app->chosenGraphicsAPI);
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        app = nullptr;
        return;
    }

    // Set the application's fullscreen state.
    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);

    // Check if the selected device actually supports MSAA sample positions and MSAA for for the formats that will be used
    // and downgrade the configuration accordingly.
    if (app->device->getCapabilities().sampleLocations) {
        plume::RenderSampleCounts color_sample_counts = app->device->getSampleCountsSupported(plume::RenderFormat::R8G8B8A8_UNORM);
        plume::RenderSampleCounts depth_sample_counts = app->device->getSampleCountsSupported(plume::RenderFormat::D32_FLOAT);
        plume::RenderSampleCounts common_sample_counts = color_sample_counts & depth_sample_counts;
        device_max_msaa = compute_max_supported_aa(common_sample_counts);
        sample_positions_supported = true;
    }
    else {
        device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
        sample_positions_supported = false;
    }

    // Cap the requested MSAA to what the GPU supports.
    if (app->userConfig.antialiasing > device_max_msaa) {
        fprintf(stderr, "MSAA: %ux not supported here, using %ux\n", app->userConfig.msaaSampleCount(), RT64::UserConfiguration::msaaSampleCount(device_max_msaa));
        app->userConfig.antialiasing = device_max_msaa;
        app->updateMultisampling();
    }

    high_precision_fb_enabled = app->shaderLibrary->usesHDR;

    const std::vector<RT64::ReplacementDirectory> packs = texture_packs(true, true);
    if (!packs.empty() && !app->textureCache->loadReplacementDirectories(packs)) {
        fprintf(stderr, "Texture packs failed to load (see above)\n");
    }
    aidyn::platform::startup_mark("texture packs loaded");

    // Textures made while the game runs: save pictures in full resolution (src/game/save_pictures.cpp) and the
    // inventory page with its words redrawn (src/game/page_words.cpp).
    // (Not while textures are off: then the game's own pictures, as on the N64.)
    app->state->dynamicReplacementProvider = [](uint64_t hash, uint32_t address, uint16_t width, uint16_t height,
                                                uint8_t fmt, uint8_t siz, RT64::TextureCache::DynamicReplacement& replacement) {
        if (!aidyn::settings::graphics().textures) {
            return false;
        }
        return aidyn::save_picture_for_texture(address, width, height, fmt, siz, replacement.rgba, replacement.width,
                                               replacement.height)
            || aidyn::page_words_for_texture(hash, address, width, height, fmt, siz, replacement.rgba, replacement.width,
                                             replacement.height);
    };
    capture_app = app.get();

    // Hairline gaps between separately drawn geometry (map cells meeting, a wall and the ground) filled by RT64 after
    // each 3D pass (tools/patches/rt64-save-pictures.patch, RenderTarget::fillGaps); wider ones below the horizon
    // (aidyn::set_gap_fill_horizon, from each frame's sky). AIDYN_GAP_FILL=0 turns it off.
    if (const char* v = std::getenv("AIDYN_GAP_FILL"); v != nullptr && std::strcmp(v, "0") == 0) {
        app->sharedQueueResources->gapFill = false;
    }

    // AIDYN_DUMP_TEXTURES=<folder>: RT64 writes every texture the game loads there (raw TMEM plus tile info), for
    // building texture packs (tools/hdui). Off unless set.
    if (const char* dump = std::getenv("AIDYN_DUMP_TEXTURES"); dump != nullptr && *dump != '\0') {
        std::filesystem::create_directories(dump);
        app->state->dumpingTexturesDirectory = dump;
        fprintf(stdout, "Dumping textures to %s\n", dump);
    }
}

aidyn::RT64Context::~RT64Context() {
    capture_app = nullptr;
}

void aidyn::capture_framebuffer(uint32_t address, float left, float top, float right, float bottom,
                                std::function<void(std::vector<uint8_t>&& rgba, uint32_t width, uint32_t height)> done) {
    RT64::Application* app = capture_app;
    if (app == nullptr) {
        done({}, 0, 0);
        return;
    }
    RT64::SharedQueueResources& shared = *app->sharedQueueResources;
    std::scoped_lock lock(shared.framebufferCaptureMutex);
    shared.framebufferCaptures.push_back({ address, left, top, right, bottom, std::move(done) });
}

// The frames shown a second, for the FPS counter (settings.json graphics.fps_counter, the Display page): the present
// thread's own timing, read twice a second.
static std::atomic<float> shown_fps_now{ 0.0f };

float aidyn::shown_fps() {
    return shown_fps_now.load(std::memory_order_relaxed);
}

// AIDYN_FPS=1 prints how many frames the game submits per second (one graphics task per game frame), how many are
// shown, and how many game ticks pass (60 a second, more under fast-forward).
static void count_game_frame(RT64::Application* app, const OSTask* task) {
    static bool first = true;
    if (first) {
        first = false;
        aidyn::platform::startup_mark("first game frame");
    }
    {
        using clock = std::chrono::steady_clock;
        static clock::time_point last = clock::now();
        const auto now = clock::now();
        if (now - last >= std::chrono::milliseconds(500)) {
            last = now;
            const double present_ms = app->presentQueue->presentProfiler.average();
            shown_fps_now.store(present_ms > 0.0 ? (float)(1000.0 / present_ms) : 0.0f, std::memory_order_relaxed);
        }
    }
    static const bool enabled = [] { const char* v = std::getenv("AIDYN_FPS"); return v != nullptr && *v != '\0' && *v != '0'; }();
    if (!enabled) {
        return;
    }
    using clock = std::chrono::steady_clock;
    static clock::time_point window_start = clock::now();
    static int frames = 0;
    static uint32_t max_dl = 0;  // the largest display list in the window (the game's buffer: 0x6400 commands)
    frames++;
    max_dl = std::max(max_dl, (uint32_t)task->t.data_size);
    auto now = clock::now();
    double seconds = std::chrono::duration<double>(now - window_start).count();
    if (seconds >= 2.0) {
        // Shown: the present thread's own timing (RT64 draws in-between frames above the game's rate).
        const double present_ms = app->presentQueue->presentProfiler.average();
        extern std::atomic_uint32_t aidyn_game_ticks;  // src/game/patches.cpp, "Fast-forward"
        fprintf(stdout, "fps: %.1f (shown %.1f), game ticks %.0f/s, display list up to %u commands\n", frames / seconds,
                present_ms > 0.0 ? 1000.0 / present_ms : 0.0, aidyn_game_ticks.exchange(0) / seconds, max_dl / 8);
        max_dl = 0;
        fflush(stdout);
        frames = 0;
        window_start = now;
    }
}

extern std::atomic_bool aidyn_menu_frame; // src/game/patches.cpp

// Widescreen and 4:3 per frame: frames that draw a full-screen menu go 4:3 at once; the picture widens again after
// 10 frames without one, so a menu's fade in and out doesn't flicker between the two.
void aidyn::RT64Context::update_menu_aspect() {
    const aidyn::settings::Graphics& player = aidyn::settings::graphics();
    const bool menu = aidyn_menu_frame.exchange(false, std::memory_order_relaxed);
    if (!player.menus_4_3 || player.aspect == aidyn::settings::Aspect::Original) {
        return;
    }
    frames_without_menu_ = menu ? 0 : frames_without_menu_ + 1;
    const bool want = menu || (menu_4_3 && frames_without_menu_ < 10);
    if (want != menu_4_3) {
        menu_4_3 = want;
        set_application_user_config(app.get(), ultramodern::renderer::get_graphics_config());
        app->updateUserConfig(true);
    }
}

extern std::atomic_uint32_t aidyn_page_log_image_start, aidyn_page_log_image_end; // src/game/patches.cpp

// AIDYN_PAGE_LOG=1 also prints the RDRAM ranges RT64 tracks as framebuffers whenever they change, and says when the
// last pause page background lies in one: RT64 then reads that picture through the framebuffer path, which skips
// texture replacement.
static void log_framebuffers(RT64::State* state) {
    static const bool enabled = std::getenv("AIDYN_PAGE_LOG") != nullptr;
    if (!enabled) {
        return;
    }
    static std::string last;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (const auto& [address, fb] : state->framebufferManager.framebuffers) {
        ranges.emplace_back(fb.addressStart, fb.addressEnd);
    }
    std::sort(ranges.begin(), ranges.end());
    const uint32_t start = aidyn_page_log_image_start, end = aidyn_page_log_image_end;
    std::string line = "framebuffers:";
    bool overlap = false;
    for (const auto& [s, e] : ranges) {
        char item[40];
        snprintf(item, sizeof(item), " %06X-%06X", s, e);
        line += item;
        overlap |= (s < end) && (e > start);
    }
    char image[64];
    snprintf(image, sizeof(image), " | page %06X-%06X%s", start, end, overlap ? " OVERLAP" : "");
    line += image;
    if (line != last) {
        fprintf(stdout, "%s\n", line.c_str());
        fflush(stdout);
        last = line;
    }
}

static std::atomic_int gap_fill_horizon{ -1 };

void aidyn::set_gap_fill_horizon(int row) {
    gap_fill_horizon.store(row, std::memory_order_relaxed);
}

// The pause menu's ReChronicled page: textures on / off (the packs again, or none) and vsync, applied on the next frame.
static std::atomic_bool vsync_dirty{ true }, aspect_dirty{ false };

void aidyn::refresh_textures() {
    redrawn_art_reload = true;
}

void aidyn::refresh_vsync() {
    vsync_dirty = true;
}

void aidyn::refresh_aspect() {
    aspect_dirty = true;
}

void aidyn::RT64Context::send_dl(const OSTask* task) {
    if (vsync_dirty.exchange(false) && app->swapChain != nullptr) {
        // FIFO (vsync) or immediate; plume rebuilds the swap chain with it on the next present.
        app->swapChain->setVsyncEnabled(aidyn::settings::graphics().vsync);
    }
    if (aspect_dirty.exchange(false)) {  // the Display page's Widescreen: the picture's shape again
        set_application_user_config(app.get(), ultramodern::renderer::get_graphics_config());
        app->updateUserConfig(true);
    }
    app->sharedQueueResources->gapFillHorizon = gap_fill_horizon.load(std::memory_order_relaxed);
    if (redrawn_art_reload.exchange(false)) {  // F2 or the textures switch: the packs again (a few seconds), or none
        app->textureCache->loadReplacementDirectories(texture_packs(redrawn_art_on, false));
        if (aidyn::settings::graphics().textures) {
            fprintf(stdout, "Textures: on, redrawn art %s\n", redrawn_art_on ? "on" : "off");
        } else {
            fprintf(stdout, "Textures: off (the game's own, as on the N64)\n");
        }
    }
    count_game_frame(app.get(), task);
    log_framebuffers(app->state.get());
    update_menu_aspect();
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
    app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
}

void aidyn::RT64Context::send_dummy_workload(uint32_t fb_address) {
    app->state->listProcessBegin();
    app->state->rdp->setColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, fb_address);
    // G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_FILL | G_PM_NPRIMITIVE
    // G_AC_NONE | G_ZS_PIXEL | G_RM_NOOP | G_RM_NOOP2
    app->state->rdp->setOtherMode(0x382C30, 0);
    app->state->rdp->fillRect(0, 0, 320 << 2, 240 << 2);
    app->state->fullSync();
    app->state->listProcessEnd();
}

void aidyn::RT64Context::update_screen() {
    app->updateScreen();
}

void aidyn::RT64Context::shutdown() {
    if (app != nullptr) {
        app->end();
    }
}

bool aidyn::RT64Context::update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }

    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }

    set_application_user_config(app.get(), new_config);

    // When updating the user configuration, only discard framebuffers if an option was changed that affects the resolution.
    bool resolution_changed = new_config.res_option != old_config.res_option;
    bool aspect_ratio_changed = new_config.ar_option != old_config.ar_option;
    bool downsampling_changed = new_config.ds_option != old_config.ds_option;
    bool msaa_changed = new_config.msaa_option != old_config.msaa_option;
    app->updateUserConfig(resolution_changed || aspect_ratio_changed || downsampling_changed || msaa_changed);

    if (msaa_changed) {
        app->updateMultisampling();
    }
    return true;
}

void aidyn::RT64Context::enable_instant_present() {
    // Enable the present early presentation mode for minimal latency.
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;

    app->updateEnhancementConfig();
}

uint32_t aidyn::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float aidyn::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
            }
            else {
                return 1.0f;
            }
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        default:
            return 1.0f;
    }
}


std::unique_ptr<ultramodern::renderer::RendererContext> aidyn::create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<aidyn::RT64Context>(rdram, window_handle, ultramodern::renderer::PresentationMode::PresentEarly, developer_mode);
}
