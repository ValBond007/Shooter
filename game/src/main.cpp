// =============================================================================
//  Arena Shooter - a small top-down shooter used as a target for an external
//  memory reader (DMA) + aimbot project.
//
//  Usage:  shooter [--bots N] [--seed N] [--width W --height H]
//                  [--print-offsets] [--headless [--seconds S]]
// =============================================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "game.h"
#include "memory_export.h"
#include "raylib.h"
#include "render.h"

namespace {

struct Options {
    int      bots = 8;
    uint32_t seed = 0;
    int      width = 1280;
    int      height = 720;
    bool     printOffsets = false;
    bool     headless = false;
    float    seconds = 0.0f;  // headless: 0 = run forever
};

Options ParseArgs(int argc, char** argv) {
    Options o;
    o.seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if (a == "--bots") o.bots = std::atoi(next());
        else if (a == "--seed") o.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (a == "--width") o.width = std::atoi(next());
        else if (a == "--height") o.height = std::atoi(next());
        else if (a == "--print-offsets") o.printOffsets = true;
        else if (a == "--headless") o.headless = true;
        else if (a == "--seconds") o.seconds = static_cast<float>(std::atof(next()));
        else if (a == "--help" || a == "-h") {
            std::printf("usage: shooter [--bots N] [--seed N] [--width W --height H]\n"
                        "               [--print-offsets] [--headless [--seconds S]]\n");
            std::exit(0);
        }
    }
    return o;
}

void PublishLayout() {
    // The game writes the real address of g_game into the struct itself.
    // External tools use it to verify that they found the right thing.
    g_game.selfAddress = reinterpret_cast<uint64_t>(&g_game);

    std::string text = DescribeMemoryLayout();
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout);
    if (WriteOffsetsFile("shooter_offsets.txt"))
        std::printf("(also written to shooter_offsets.txt)\n");
    std::fflush(stdout);
}

// Clamp the camera so it does not show too much outside of the arena.
void UpdateCamera(Camera2D& cam, const gm::GameMemory& mem, Vec2f focus, float dt) {
    const float sw = static_cast<float>(mem.screenWidth), sh = static_cast<float>(mem.screenHeight);
    cam.offset = Vector2{sw * 0.5f, sh * 0.5f};

    float k = 1.0f - std::exp(-12.0f * dt);  // smooth follow
    Vec2f target{cam.target.x, cam.target.y};
    target = Lerp(target, focus, k);

    float halfW = sw * 0.5f / cam.zoom, halfH = sh * 0.5f / cam.zoom;
    const float pad = 80.0f;
    float minX = halfW - pad, maxX = mem.arenaSize.x - halfW + pad;
    float minY = halfH - pad, maxY = mem.arenaSize.y - halfH + pad;
    target.x = minX < maxX ? Clampf(target.x, minX, maxX) : mem.arenaSize.x * 0.5f;
    target.y = minY < maxY ? Clampf(target.y, minY, maxY) : mem.arenaSize.y * 0.5f;
    cam.target = Vector2{target.x, target.y};
}

void PublishCameraAndInput(const Camera2D& cam, Vector2 mouse, uint32_t buttons, bool paused) {
    g_game.camTarget    = {cam.target.x, cam.target.y};
    g_game.camOffset    = {cam.offset.x, cam.offset.y};
    g_game.camZoom      = cam.zoom;
    g_game.camRotation  = cam.rotation;
    g_game.mouseScreen  = {mouse.x, mouse.y};
    g_game.mouseWorld   = gm::ScreenToWorld(g_game, g_game.mouseScreen);
    g_game.buttons      = buttons;
    g_game.paused       = paused ? 1u : 0u;
    g_game.frameCount++;
}

// -----------------------------------------------------------------------------
//  Headless mode: no window, the player is controlled by a simple script.
//  Useful to test an external reader on a machine without a GPU/display.
// -----------------------------------------------------------------------------
int RunHeadless(const Options& opt) {
    std::printf("[headless] running %s (seed %u, %d bots)\n",
                opt.seconds > 0 ? "for a fixed time" : "until killed", opt.seed, opt.bots);
    Game game(g_game, opt.seed);
    game.SetBotCount(opt.bots);
    g_game.screenWidth = opt.width;
    g_game.screenHeight = opt.height;

    Camera2D cam{};
    cam.zoom = 1.0f;
    auto start = std::chrono::steady_clock::now();
    auto nextTick = start;
    float t = 0.0f;
    while (opt.seconds <= 0.0f || t < opt.seconds) {
        const gm::Entity& p = game.Player();
        PlayerInput in;
        in.move = {std::cos(t * 0.7f), std::sin(t * 1.3f)};
        // Aim at the closest visible bot.
        float best = 1e9f;
        in.aimWorld = p.pos + Vec2f{100, 0};
        for (int i = 1; i < gm::kMaxEntities; ++i) {
            const gm::Entity& e = g_game.entities[i];
            if (!e.active || !e.alive || !e.visible) continue;
            float d = Distance(e.pos, p.pos);
            if (d < best) { best = d; in.aimWorld = e.pos; }
        }
        in.shoot = best < 700.0f;
        game.Tick(in);

        UpdateCamera(cam, g_game, p.pos, Game::kTickDt);
        Vec2f mouse = gm::WorldToScreen(g_game, in.aimWorld);
        PublishCameraAndInput(cam, Vector2{mouse.x, mouse.y}, 0, false);

        t += Game::kTickDt;
        nextTick += std::chrono::microseconds(static_cast<int64_t>(Game::kTickDt * 1e6f));
        std::this_thread::sleep_until(nextTick);
    }
    std::printf("[headless] done. player kills %d deaths %d\n", game.Player().kills,
                game.Player().deaths);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = ParseArgs(argc, argv);
    PublishLayout();
    if (opt.printOffsets) return 0;
    if (opt.headless) return RunHeadless(opt);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(opt.width, opt.height, "Arena Shooter");
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);  // Esc = pause, not quit
    HideCursor();          // we draw our own crosshair

    Game game(g_game, opt.seed);
    game.SetBotCount(opt.bots);

    UiState ui;
    Camera2D cam{};
    cam.zoom = 1.0f;
    cam.target = Vector2{game.Player().pos.x, game.Player().pos.y};
    float accumulator = 0.0f;

    while (!WindowShouldClose()) {
        const float frameDt = std::min(GetFrameTime(), 0.1f);
        g_game.screenWidth  = GetScreenWidth();
        g_game.screenHeight = GetScreenHeight();

        // ---- keys --------------------------------------------------------------
        if (IsKeyPressed(KEY_F1)) ui.debugOverlay = !ui.debugOverlay;
        if (IsKeyPressed(KEY_F2)) game.godMode = !game.godMode;
        if (IsKeyPressed(KEY_F3)) game.botsFrozen = !game.botsFrozen;
        if (IsKeyPressed(KEY_F4)) game.botsPeaceful = !game.botsPeaceful;
        if (IsKeyPressed(KEY_H)) ui.showHelp = !ui.showHelp;
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) ui.paused = !ui.paused;
        if (IsKeyPressed(KEY_R)) game.Reset();
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) game.SetBotCount(game.BotCount() + 1);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) game.SetBotCount(game.BotCount() - 1);
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) cam.zoom = Clampf(cam.zoom * (1.0f + 0.1f * wheel), 0.5f, 1.6f);
        ui.helpHintTimer -= frameDt;

        // ---- input --------------------------------------------------------------
        Vector2 mouse = GetMousePosition();
        uint32_t buttons = 0;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))   buttons |= gm::BUTTON_LEFT;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))  buttons |= gm::BUTTON_RIGHT;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) buttons |= gm::BUTTON_MIDDLE;

        PlayerInput in;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    in.move.y -= 1.0f;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  in.move.y += 1.0f;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  in.move.x -= 1.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) in.move.x += 1.0f;
        Vector2 aim = GetScreenToWorld2D(mouse, cam);
        in.aimWorld = {aim.x, aim.y};
        in.shoot = (buttons & gm::BUTTON_LEFT) && !ui.showHelp;

        // ---- simulation (fixed time step) ------------------------------------------
        if (!ui.paused && !ui.showHelp) {
            accumulator += frameDt;
            while (accumulator >= Game::kTickDt) {
                game.Tick(in);
                accumulator -= Game::kTickDt;
            }
        }

        UpdateCamera(cam, g_game, game.Player().pos, frameDt);
        PublishCameraAndInput(cam, mouse, buttons, ui.paused || ui.showHelp);

        // ---- draw ---------------------------------------------------------------------
        BeginDrawing();
        DrawFrame(game, g_game, cam, ui);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
