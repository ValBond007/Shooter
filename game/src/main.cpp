// =============================================================================
//  Arena Shooter - a small top-down shooter used as a target for an external
//  memory reader (DMA) + aimbot project.
//
//  Usage:  shooter [--mode dm|tdm|survival|training] [--map N] [--bots N]
//                  [--difficulty easy|normal|hard] [--no-fog] [--skip-menu]
//                  [--seed N] [--width W --height H]
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
#include <vector>

#include "audio.h"
#include "maps.h"
#include "game.h"
#include "memory_export.h"
#include "raylib.h"
#include "render.h"
#include "ui.h"

namespace {

struct Options {
    MatchSettings match;
    bool     fog = true;
    bool     skipMenu = false;
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
        if (a == "--bots") o.match.bots = std::atoi(next());
        else if (a == "--map") o.match.map = std::atoi(next());
        else if (a == "--mode") {
            std::string m = next();
            o.match.mode = m == "tdm" ? gm::MODE_TEAM_DEATHMATCH
                         : m == "survival" ? gm::MODE_SURVIVAL
                         : m == "training" ? gm::MODE_TRAINING : gm::MODE_DEATHMATCH;
        } else if (a == "--difficulty") {
            std::string d = next();
            o.match.difficulty = d == "easy" ? gm::DIFFICULTY_EASY : d == "hard" ? gm::DIFFICULTY_HARD : gm::DIFFICULTY_NORMAL;
        }
        else if (a == "--no-fog") o.fog = false;
        else if (a == "--skip-menu") o.skipMenu = true;
        else if (a == "--seed") o.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (a == "--width") o.width = std::atoi(next());
        else if (a == "--height") o.height = std::atoi(next());
        else if (a == "--print-offsets") o.printOffsets = true;
        else if (a == "--headless") o.headless = true;
        else if (a == "--seconds") o.seconds = static_cast<float>(std::atof(next()));
        else if (a == "--help" || a == "-h") {
            std::printf("usage: shooter [--mode dm|tdm|survival|training] [--map N] [--bots N]\n"
                        "               [--difficulty easy|normal|hard] [--no-fog] [--skip-menu]\n"
                        "               [--seed N] [--width W --height H]\n"
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
    std::printf("[headless] %s on %s, running %s (seed %u)\n", ModeName(opt.match.mode), GetMap(opt.match.map).name,
                opt.seconds > 0 ? "for a fixed time" : "until killed", opt.seed);
    Game game(g_game, opt.seed);
    game.StartMatch(opt.match);
    g_game.fogOfWar = opt.fog ? 1u : 0u;
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
            if (!e.active || !e.alive || !e.visible || e.team == p.team) continue;
            float d = Distance(e.pos, p.pos);
            if (d < best) { best = d; in.aimWorld = e.pos; }
        }
        in.shoot = best < 700.0f;
        in.reload = p.ammo == 0;
        game.Tick(in);
        if (game.MatchOver()) {
            std::printf("[headless] %s: %s\n", game.Result().title.c_str(), game.Result().csvLine.c_str());
            game.Reset();
        }

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

// One line per finished match. Handy for comparing runs (e.g. with / without aimbot).
void AppendResultCsv(const std::string& line) {
    const char* path = "shooter_results.csv";
    bool exists = false;
    if (FILE* f = std::fopen(path, "r")) {
        exists = true;
        std::fclose(f);
    }
    FILE* f = std::fopen(path, "a");
    if (!f) return;
    if (!exists)
        std::fputs("date,mode,map,difficulty,bots,duration_s,kills,deaths,shots,hits,accuracy,"
                   "avg_ttk_ms,avg_first_hit_ms,wave,result\n", f);
    std::fputs(line.c_str(), f);
    std::fputc('\n', f);
    std::fclose(f);
}

enum class Screen { Menu, Playing, Results };

// Camera for the menu: the whole map, slowly drifting.
void MenuCamera(Camera2D& cam, const gm::GameMemory& mem, float time) {
    const float sw = static_cast<float>(mem.screenWidth), sh = static_cast<float>(mem.screenHeight);
    cam.zoom = std::min(sw / mem.arenaSize.x, sh / mem.arenaSize.y) * 1.05f;
    cam.offset = Vector2{sw * 0.5f, sh * 0.5f};
    cam.target = Vector2{mem.arenaSize.x * 0.5f + 60.0f * std::sin(time * 0.15f),
                         mem.arenaSize.y * 0.5f + 40.0f * std::cos(time * 0.11f)};
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = ParseArgs(argc, argv);
    PublishLayout();
    if (opt.printOffsets) return 0;
    if (opt.headless) return RunHeadless(opt);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(opt.width, opt.height, "Arena Shooter");
    SetWindowMinSize(800, 600);
    SetExitKey(KEY_NULL);  // Esc = pause menu, not quit
    HideCursor();          // we draw our own crosshair / pointer

    Game game(g_game, opt.seed);
    Audio audio;
    if (!audio.Init()) std::printf("[audio] no audio device - running without sound\n");

    MenuState menu;
    menu.settings = opt.match;
    menu.fog = opt.fog;
    menu.selected = 5;  // "PLAY"

    Screen screen = Screen::Menu;
    UiState ui;
    Camera2D cam{};
    cam.zoom = 1.0f;
    float accumulator = 0.0f;
    float shake = 0.0f;           // screen shake strength (pixels)
    float resultDelay = 0.0f;     // short pause between the last kill and the result screen
    float menuTime = 0.0f;
    int   pendingWeapon = -1;     // one-shot inputs wait for the next simulation tick
    bool  pendingReload = false;
    bool  pendingDash = false;
    int   previousWeapon = gm::WEAPON_SHOTGUN;
    int   menuMap = -1;
    bool  quit = false;

    auto startMatch = [&]() {
        game.StartMatch(menu.settings);
        g_game.fogOfWar = menu.fog ? 1u : 0u;
        cam.zoom = 1.0f;
        cam.target = Vector2{game.Player().pos.x, game.Player().pos.y};
        accumulator = 0.0f;
        ui.paused = ui.showHelp = false;
        ui.helpHintTimer = 8.0f;
        screen = Screen::Playing;
    };
    auto toMenu = [&]() {
        menuMap = -1;
        menu.selected = 5;
        ui.paused = ui.showHelp = false;
        screen = Screen::Menu;
    };

    if (opt.skipMenu) startMatch();

    while (!WindowShouldClose() && !quit) {
        const float frameDt = std::min(GetFrameTime(), 0.1f);
        g_game.screenWidth  = GetScreenWidth();
        g_game.screenHeight = GetScreenHeight();
        Vector2 mouse = GetMousePosition();
        uint32_t buttons = 0;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))   buttons |= gm::BUTTON_LEFT;
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))  buttons |= gm::BUTTON_RIGHT;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) buttons |= gm::BUTTON_MIDDLE;
        if (IsKeyPressed(KEY_M)) audio.muted = ui.muted = !ui.muted;

        // =====================================================================
        //  Main menu
        // =====================================================================
        if (screen == Screen::Menu) {
            if (menu.settings.map != menuMap) {
                menuMap = menu.settings.map;
                game.EnterMenu(menuMap);
            }
            menuTime += frameDt;
            MenuCamera(cam, g_game, menuTime);
            PublishCameraAndInput(cam, mouse, buttons, true);

            BeginDrawing();
            DrawMenuBackground(game, g_game, cam);
            MenuAction action = DoMainMenu(menu);
            DrawPointer();
            EndDrawing();

            if (menu.click) audio.PlayClick();
            if (action == MenuAction::Play) startMatch();
            if (action == MenuAction::Quit) quit = true;
            continue;
        }

        // =====================================================================
        //  Playing / result screen
        // =====================================================================
        const bool playing = screen == Screen::Playing;
        if (playing) {
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) {
                ui.paused = !ui.paused;
                menu.selected = 0;
            }
            if (!ui.paused) {
                if (IsKeyPressed(KEY_F1)) ui.debugOverlay = !ui.debugOverlay;
                if (IsKeyPressed(KEY_F2)) game.godMode = !game.godMode;
                if (IsKeyPressed(KEY_F3)) game.botsFrozen = !game.botsFrozen;
                if (IsKeyPressed(KEY_F4)) game.botsPeaceful = !game.botsPeaceful;
                if (IsKeyPressed(KEY_F5)) startMatch();
                if (IsKeyPressed(KEY_F6)) game.SetDifficulty((game.Difficulty() + 1) % 3);
                if (IsKeyPressed(KEY_F7)) g_game.fogOfWar = !g_game.fogOfWar;
                if (IsKeyPressed(KEY_H)) ui.showHelp = !ui.showHelp;
                if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) game.SetBotCount(game.BotCount() + 1);
                if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) game.SetBotCount(game.BotCount() - 1);
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) cam.zoom = Clampf(cam.zoom * (1.0f + 0.1f * wheel), 0.5f, 1.6f);
                if (IsKeyPressed(KEY_R)) pendingReload = true;
                if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_SPACE)) pendingDash = true;
                int current = static_cast<int>(game.Player().weapon);
                for (int w = 0; w < gm::kWeaponCount; ++w)
                    if (IsKeyPressed(static_cast<KeyboardKey>(KEY_ONE + w)) && w != current) pendingWeapon = w;
                if (IsKeyPressed(KEY_Q)) pendingWeapon = previousWeapon;
                if (pendingWeapon >= 0 && pendingWeapon != current) previousWeapon = current;
            }
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            toMenu();
            continue;
        }
        ui.showScoreboard = IsKeyDown(KEY_TAB);
        ui.helpHintTimer -= frameDt;

        // ---- input ------------------------------------------------------------------
        PlayerInput in;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    in.move.y -= 1.0f;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  in.move.y += 1.0f;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  in.move.x -= 1.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) in.move.x += 1.0f;
        Vector2 aim = GetScreenToWorld2D(mouse, cam);
        in.aimWorld = {aim.x, aim.y};
        in.shoot = (buttons & gm::BUTTON_LEFT) && !ui.showHelp;
        in.reload = pendingReload;
        in.selectWeapon = pendingWeapon;
        in.dash = pendingDash;

        // ---- simulation (fixed time step) ------------------------------------------
        if (!ui.paused && !ui.showHelp) {
            accumulator += frameDt;
            while (accumulator >= Game::kTickDt) {
                game.Tick(in);
                accumulator -= Game::kTickDt;
                // one-shot inputs only for the first tick
                in.reload = pendingReload = false;
                in.selectWeapon = pendingWeapon = -1;
                in.dash = pendingDash = false;
            }
        }

        // ---- sound + screen shake from this frame's events ---------------------------
        std::vector<GameEvent> events = game.TakeEvents();
        audio.PlayEvents(events, game.Player().pos);
        for (const GameEvent& e : events) {
            if (e.type == GameEventType::PlayerShot) shake = std::max(shake, GetWeaponDef(e.weapon).shake);
            if (e.type == GameEventType::PlayerHurt) shake = std::max(shake, 5.0f);
            if (e.type == GameEventType::PlayerDied) shake = std::max(shake, 10.0f);
            if (e.type == GameEventType::MatchEnd) {
                AppendResultCsv(game.Result().csvLine);
                std::printf("[match] %s  %s\n", game.Result().title.c_str(), game.Result().csvLine.c_str());
                resultDelay = 1.2f;
            }
        }
        shake *= std::exp(-14.0f * frameDt);
        if (playing && game.MatchOver()) {
            resultDelay -= frameDt;
            if (resultDelay <= 0.0f) {
                screen = Screen::Results;
                menu.selected = 0;
            }
        }

        UpdateCamera(cam, g_game, game.Player().pos, frameDt);
        if (shake > 0.2f) {
            // The shaken camera is also what gets published to memory, so an
            // external tool always sees exactly the camera that was drawn.
            cam.offset.x += static_cast<float>(GetRandomValue(-100, 100)) / 100.0f * shake;
            cam.offset.y += static_cast<float>(GetRandomValue(-100, 100)) / 100.0f * shake;
        }
        PublishCameraAndInput(cam, mouse, buttons, ui.paused || ui.showHelp || !game.Playing());

        // ---- draw ---------------------------------------------------------------------
        BeginDrawing();
        DrawFrame(game, g_game, cam, ui);
        MenuAction action = MenuAction::None;
        if (screen == Screen::Results) action = DoResultScreen(game, menu);
        else if (ui.paused) action = DoPauseMenu(menu);
        if (screen == Screen::Results || ui.paused || ui.showHelp) DrawPointer();
        EndDrawing();

        if (menu.click && (ui.paused || screen == Screen::Results)) audio.PlayClick();
        switch (action) {
            case MenuAction::Resume:   ui.paused = false; break;
            case MenuAction::Restart:  startMatch(); break;
            case MenuAction::MainMenu: toMenu(); break;
            case MenuAction::Quit:     quit = true; break;
            default: break;
        }
    }

    ShutdownRenderer();
    CloseWindow();
    return 0;
}
