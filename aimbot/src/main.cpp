// =============================================================================
//  aimbot - external memory reader + aimbot for Arena Shooter
//
//  usage: aimbot [mode] [--config config.ini] [--memory dma|winapi|linux]
//                [--mouse kmbox_b|kmbox_net|sendinput|x11|none] [--seconds N]
//
//  modes:
//    aim        (default) read the game and move the mouse onto enemies
//    dump       live table of everything read from the game's memory
//    radar      ASCII radar of the arena (memory reading only)
//    info       find the game, print addresses once, exit
//    bench      measure how fast memory reads are (useful for DMA)
//    calibrate  measure mouse counts per pixel -> value for mouse_scale
// =============================================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "aimbot.h"
#include "config.h"
#include "game_reader.h"
#include "input/mouse.h"
#include "memory/memory.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <timeapi.h>
#endif

namespace {

std::atomic<bool> g_running{true};

void OnSignal(int) { g_running = false; }

using Clock = std::chrono::steady_clock;

double NowSeconds() {
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void SleepMicros(int us) {
    if (us > 0) std::this_thread::sleep_for(std::chrono::microseconds(us));
    else std::this_thread::yield();
}

void SetupConsole() {
#if defined(_WIN32)
    // Enable ANSI escape codes (colors / cursor movement) in the Windows console.
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    // 1 ms timer resolution so short sleeps are really short.
    timeBeginPeriod(1);
#endif
}

void ClearScreen() { std::printf("\x1b[H\x1b[2J"); }
void CursorHome() { std::printf("\x1b[H"); }

struct Args {
    std::string mode = "aim";
    std::string configPath = "config.ini";
    std::string memoryOverride;
    std::string mouseOverride;
    double      seconds = 0.0;  // 0 = run until Ctrl+C
};

Args ParseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (s == "--config") a.configPath = next();
        else if (s == "--memory") a.memoryOverride = next();
        else if (s == "--mouse") a.mouseOverride = next();
        else if (s == "--seconds") a.seconds = std::atof(next().c_str());
        else if (s == "-h" || s == "--help") a.mode = "help";
        else if (!s.empty() && s[0] != '-') a.mode = s;
        else std::fprintf(stderr, "unknown argument '%s'\n", s.c_str());
    }
    return a;
}

void PrintHelp() {
    std::printf(
        "usage: aimbot [mode] [--config config.ini] [--memory dma|winapi|linux]\n"
        "              [--mouse kmbox_b|kmbox_net|sendinput|x11|none] [--seconds N]\n\n"
        "modes:\n"
        "  aim        (default) read the game and move the mouse onto enemies\n"
        "  dump       live table of everything read from the game's memory\n"
        "  radar      ASCII radar of the arena\n"
        "  info       find the game, print addresses once, exit\n"
        "  bench      measure memory read speed\n"
        "  calibrate  measure mouse counts per pixel (for mouse_scale)\n");
}

bool TimeUp(const Args& args, double start) {
    return !g_running || (args.seconds > 0 && NowSeconds() - start > args.seconds);
}

// Attach to the process and find g_game. Retries until it works (or Ctrl+C),
// so you can start the aimbot before the game.
bool Connect(MemoryReader& mem, GameReader& reader, const Config& cfg) {
    bool first = true;
    while (g_running) {
        if (mem.Attach(cfg.process)) {
            std::printf("[memory] %s: pid %u, module base 0x%" PRIX64 ", size 0x%" PRIX64 "\n", mem.Name(),
                        mem.Pid(), mem.ModuleBase(), mem.ModuleSize());
            if (reader.Locate()) return true;
        }
        if (first) std::printf("waiting for the game... (Ctrl+C to quit)\n");
        first = false;
        for (int i = 0; i < 20 && g_running; ++i) SleepMicros(100000);
    }
    return false;
}

// Read a snapshot; if it fails several times in a row (game closed or
// restarted) find the game again.
bool ReadOrReconnect(MemoryReader& mem, GameReader& reader, const Config& cfg, gm::GameMemory& g) {
    static int failures = 0;
    if (reader.ReadSnapshot(g)) {
        failures = 0;
        return true;
    }
    if (++failures >= 50) {
        std::printf("\n[reader] lost the game, reconnecting...\n");
        failures = 0;
        Connect(mem, reader, cfg);
    } else {
        SleepMicros(2000);
    }
    return false;
}

std::string EntityName(const gm::Entity& e) {
    char name[gm::kNameLength + 1] = {};
    std::memcpy(name, e.name, gm::kNameLength);  // may not be zero terminated if memory is garbage
    return name;
}

// =============================================================================
//  mode: info
// =============================================================================
int RunInfo(MemoryReader& mem, GameReader& reader, const Config& cfg) {
    if (!Connect(mem, reader, cfg)) return 1;
    gm::GameMemory g{};
    if (!reader.ReadSnapshot(g)) {
        std::fprintf(stderr, "read failed\n");
        return 1;
    }
    std::printf("\n");
    std::printf("module base     0x%016" PRIX64 "\n", mem.ModuleBase());
    std::printf("g_game          0x%016" PRIX64 "  (RVA 0x%" PRIX64 ")\n", reader.Address(),
                reader.Address() - mem.ModuleBase());
    std::printf("entities[0]     0x%016" PRIX64 "\n", reader.Address() + offsetof(gm::GameMemory, entities));
    std::printf("layout version  %u, size 0x%X\n", g.layoutVersion, g.structSize);
    std::printf("frame %u, time %.1f s, %u entities, screen %dx%d\n", g.frameCount, g.gameTime,
                g.entityCount, g.screenWidth, g.screenHeight);
    std::printf("\ntip: put  game_rva = 0x%" PRIX64 "  into config.ini to skip the scan\n",
                reader.Address() - mem.ModuleBase());
    return 0;
}

// =============================================================================
//  mode: dump
// =============================================================================
int RunDump(MemoryReader& mem, GameReader& reader, const Config& cfg, const Args& args) {
    if (!Connect(mem, reader, cfg)) return 1;
    ClearScreen();
    gm::GameMemory g{};
    double start = NowSeconds(), lastPrint = 0;
    int reads = 0;
    double readTime = 0;

    while (!TimeUp(args, start)) {
        double t0 = NowSeconds();
        if (!ReadOrReconnect(mem, reader, cfg, g)) continue;
        readTime += NowSeconds() - t0;
        ++reads;

        if (NowSeconds() - lastPrint < 0.1) {
            SleepMicros(cfg.pollIntervalUs);
            continue;
        }
        double elapsed = NowSeconds() - lastPrint;
        lastPrint = NowSeconds();

        CursorHome();
        std::printf("Arena Shooter - live memory dump            (%s)  Ctrl+C to quit\x1b[K\n", mem.Name());
        std::printf("g_game 0x%" PRIX64 "   %d reads/s   avg read %.1f us   frame %u   time %.1f s\x1b[K\n",
                    reader.Address(), static_cast<int>(reads / elapsed), readTime / std::max(reads, 1) * 1e6,
                    g.frameCount, g.gameTime);
        reads = 0;
        readTime = 0;
        std::printf("camera target (%.1f, %.1f) offset (%.1f, %.1f) zoom %.2f   screen %dx%d\x1b[K\n",
                    g.camTarget.x, g.camTarget.y, g.camOffset.x, g.camOffset.y, g.camZoom, g.screenWidth,
                    g.screenHeight);
        std::printf("mouse screen (%.0f, %.0f) world (%.0f, %.0f)   buttons [%s%s%s]   %s\x1b[K\n",
                    g.mouseScreen.x, g.mouseScreen.y, g.mouseWorld.x, g.mouseWorld.y,
                    (g.buttons & gm::BUTTON_LEFT) ? "L" : "-", (g.buttons & gm::BUTTON_MIDDLE) ? "M" : "-",
                    (g.buttons & gm::BUTTON_RIGHT) ? "R" : "-", g.paused ? "PAUSED" : "");
        std::printf("\x1b[K\n");
        std::printf(" idx  name            team alive  hp    pos.x   pos.y   vel.x  vel.y  angle  vis  K/D    screen\x1b[K\n");
        for (int i = 0; i < gm::kMaxEntities; ++i) {
            const gm::Entity& e = g.entities[i];
            if (!e.active) continue;
            gm::Vec2f s = gm::WorldToScreen(g, e.pos);
            std::printf(" %3d  %-15s %4u %5u %4d  %7.1f %7.1f %6.0f %6.0f %6.2f %4u %3d/%-3d (%5.0f,%5.0f)\x1b[K\n", i,
                        EntityName(e).c_str(), e.team, e.alive, e.health, e.pos.x, e.pos.y, e.vel.x, e.vel.y,
                        e.aimAngle, e.visible, e.kills, e.deaths, s.x, s.y);
        }
        std::printf("\x1b[J");  // clear the rest of the screen
        std::fflush(stdout);
    }
    return 0;
}

// =============================================================================
//  mode: radar
// =============================================================================
int RunRadar(MemoryReader& mem, GameReader& reader, const Config& cfg, const Args& args) {
    if (!Connect(mem, reader, cfg)) return 1;
    ClearScreen();
    const int W = 80, H = 30;
    gm::GameMemory g{};
    double start = NowSeconds();

    while (!TimeUp(args, start)) {
        if (!ReadOrReconnect(mem, reader, cfg, g)) continue;
        if (g.arenaSize.x <= 0 || g.arenaSize.y <= 0) continue;

        std::vector<std::string> grid(H, std::string(W, ' '));
        auto cell = [&](float x, float y, int& cx, int& cy) {
            cx = std::clamp(static_cast<int>(x / g.arenaSize.x * W), 0, W - 1);
            cy = std::clamp(static_cast<int>(y / g.arenaSize.y * H), 0, H - 1);
        };
        for (uint32_t i = 0; i < std::min<uint32_t>(g.obstacleCount, gm::kMaxObstacles); ++i) {
            const gm::Obstacle& o = g.obstacles[i];
            int x0, y0, x1, y1;
            cell(o.x, o.y, x0, y0);
            cell(o.x + o.w - 1, o.y + o.h - 1, x1, y1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) grid[y][x] = '#';
        }
        const gm::Entity& me = g.entities[g.localPlayerIndex % gm::kMaxEntities];
        for (int i = 0; i < gm::kMaxEntities; ++i) {
            const gm::Entity& e = g.entities[i];
            if (!e.active || !e.alive) continue;
            int x, y;
            cell(e.pos.x, e.pos.y, x, y);
            grid[y][x] = (i == static_cast<int>(g.localPlayerIndex)) ? '@' : (e.visible ? 'E' : 'e');
        }

        CursorHome();
        std::printf("Arena Shooter - radar (from memory)   @ you   E enemy (visible)   e enemy (hidden)   # wall\x1b[K\n");
        std::printf("+%s+\n", std::string(W, '-').c_str());
        for (const std::string& row : grid) std::printf("|%s|\n", row.c_str());
        std::printf("+%s+\n", std::string(W, '-').c_str());
        std::printf("you: hp %d  pos (%.0f, %.0f)  kills %d  deaths %d\x1b[K\n", me.health, me.pos.x, me.pos.y,
                    me.kills, me.deaths);
        std::fflush(stdout);
        for (int i = 0; i < 5 && g_running; ++i) SleepMicros(10000);
    }
    return 0;
}

// =============================================================================
//  mode: bench
// =============================================================================
int RunBench(MemoryReader& mem, GameReader& reader, const Config& cfg) {
    if (!Connect(mem, reader, cfg)) return 1;

    auto bench = [&](const char* label, size_t size, int count) {
        std::vector<uint8_t> buf(size);
        double minT = 1e9, maxT = 0, total = 0;
        int ok = 0;
        for (int i = 0; i < count && g_running; ++i) {
            double t0 = NowSeconds();
            bool success = mem.Read(reader.Address(), buf.data(), size);
            double dt = NowSeconds() - t0;
            if (!success) continue;
            ++ok;
            total += dt;
            minT = std::min(minT, dt);
            maxT = std::max(maxT, dt);
        }
        if (!ok) { std::printf("%-28s all reads failed\n", label); return; }
        double avg = total / ok;
        std::printf("%-28s %5d reads  avg %8.1f us  min %8.1f us  max %8.1f us  -> %7.0f reads/s, %6.2f MB/s\n",
                    label, ok, avg * 1e6, minT * 1e6, maxT * 1e6, 1.0 / avg, size / avg / 1e6);
    };

    std::printf("\nmemory backend: %s\n\n", mem.Name());
    bench("8 bytes (one value)", 8, 2000);
    bench("0x60 bytes (one entity)", sizeof(gm::Entity), 2000);
    bench("0xE80 bytes (whole game)", sizeof(gm::GameMemory), 2000);
    std::printf("\nReading the whole struct at once is much cheaper than reading every\n"
                "field separately (%zu single reads would be needed for all entities).\n",
                static_cast<size_t>(gm::kMaxEntities) * 15);
    return 0;
}

// =============================================================================
//  mode: calibrate
// =============================================================================
bool WaitFrames(MemoryReader& mem, GameReader& reader, const Config& cfg, gm::GameMemory& g, int frames) {
    gm::GameMemory tmp{};
    if (!reader.ReadSnapshot(tmp)) return false;
    uint32_t target = tmp.frameCount + static_cast<uint32_t>(frames);
    double start = NowSeconds();
    while (g_running && NowSeconds() - start < 2.0) {
        if (ReadOrReconnect(mem, reader, cfg, g) && static_cast<int32_t>(g.frameCount - target) >= 0) return true;
        SleepMicros(1000);
    }
    return false;
}

int RunCalibrate(MemoryReader& mem, GameReader& reader, MouseOutput& mouse, const Config& cfg) {
    if (!Connect(mem, reader, cfg)) return 1;
    std::printf("\nCalibration: moves the mouse by a known number of counts and reads\n"
                "how far the game's cursor moved (mouseScreen in memory).\n"
                "Put the game window in focus, cursor near the CENTER of the window.\n"
                "Starting in 3 seconds...\n");
    for (int i = 0; i < 30 && g_running; ++i) SleepMicros(100000);

    const int counts = 100;
    float results[2] = {0, 0};
    for (int axis = 0; axis < 2; ++axis) {
        gm::GameMemory before{}, after{};
        if (!WaitFrames(mem, reader, cfg, before, 3)) return 1;
        mouse.Move(axis == 0 ? counts : 0, axis == 1 ? counts : 0);
        if (!WaitFrames(mem, reader, cfg, after, 10)) return 1;
        float moved = axis == 0 ? after.mouseScreen.x - before.mouseScreen.x
                                : after.mouseScreen.y - before.mouseScreen.y;
        results[axis] = moved;
        std::printf("  %s: sent %d counts -> cursor moved %.1f pixels\n", axis == 0 ? "x" : "y", counts, moved);
        mouse.Move(axis == 0 ? -counts : 0, axis == 1 ? -counts : 0);  // move back
    }

    float avgPixels = (results[0] + results[1]) * 0.5f;
    if (std::fabs(avgPixels) < 5.0f) {
        std::printf("\nThe cursor did not move. Is the game window focused / the mouse output working?\n");
        return 1;
    }
    std::printf("\n=> mouse_scale = %.3f   (counts per pixel, put this into config.ini)\n", counts / avgPixels);
    std::printf("   Tip: with Windows pointer speed 10/20 and \"Enhance pointer precision\" OFF\n"
                "   this should be about 1.0 (divided by your display scaling, e.g. 1.25 at 125%%).\n");
    return 0;
}

// =============================================================================
//  mode: aim
// =============================================================================
int RunAim(MemoryReader& mem, GameReader& reader, MouseOutput& mouse, const Config& cfg, const Args& args) {
    if (!Connect(mem, reader, cfg)) return 1;
    std::printf("\naimbot running - hold %s in the game. Ctrl+C to quit.\n\n",
                cfg.aimKey == "always" ? "nothing (always on)" : cfg.aimKey.c_str());

    Aimbot aimbot(cfg);
    gm::GameMemory g{};
    uint32_t lastFrame = 0;
    double start = NowSeconds(), lastStatus = 0;
    int reads = 0, frames = 0, moves = 0;

    while (!TimeUp(args, start)) {
        if (!ReadOrReconnect(mem, reader, cfg, g)) continue;
        ++reads;

        // Only react to NEW frames: the game samples the cursor once per frame,
        // so moving more often than that would use stale data (overshooting).
        if (g.frameCount != lastFrame) {
            lastFrame = g.frameCount;
            ++frames;
            AimResult r = aimbot.Update(g);
            if (r.moveX != 0 || r.moveY != 0) {
                mouse.Move(r.moveX, r.moveY);
                ++moves;
            }

            double now = NowSeconds();
            if (now - lastStatus >= 0.1) {
                double dt = now - lastStatus;
                lastStatus = now;
                const gm::Entity& me = g.entities[g.localPlayerIndex % gm::kMaxEntities];
                char target[96] = "-";
                if (r.hasTarget) {
                    const gm::Entity& t = g.entities[r.targetIndex];
                    std::snprintf(target, sizeof(target), "%s (hp %d) err %.0f px", EntityName(t).c_str(),
                                  t.health, std::hypot(r.error.x, r.error.y));
                }
                std::printf("\r[%s] reads/s %5d  frames/s %4d  moves/s %4d | hp %3d  K %d D %d | target %-34s",
                            r.keyHeld ? "AIM" : "   ", static_cast<int>(reads / dt), static_cast<int>(frames / dt),
                            static_cast<int>(moves / dt), me.health, me.kills, me.deaths, target);
                std::fflush(stdout);
                reads = frames = moves = 0;
            }
        }
        SleepMicros(cfg.pollIntervalUs);
    }
    std::printf("\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Args args = ParseArgs(argc, argv);
    if (args.mode == "help") {
        PrintHelp();
        return 0;
    }
    SetupConsole();
    std::signal(SIGINT, OnSignal);

    Config cfg;
    if (cfg.Load(args.configPath)) std::printf("[config] loaded %s\n", args.configPath.c_str());
    else std::printf("[config] %s not found, using defaults\n", args.configPath.c_str());
    if (!args.memoryOverride.empty()) cfg.memory = args.memoryOverride;
    if (!args.mouseOverride.empty()) cfg.mouse = args.mouseOverride;
    cfg.Print();

    auto mem = CreateMemoryReader(cfg.memory, cfg.dmaArgs);
    if (!mem) return 1;
    GameReader reader(*mem, cfg.gameRva);

    if (args.mode == "info") return RunInfo(*mem, reader, cfg);
    if (args.mode == "dump") return RunDump(*mem, reader, cfg, args);
    if (args.mode == "radar") return RunRadar(*mem, reader, cfg, args);
    if (args.mode == "bench") return RunBench(*mem, reader, cfg);

    if (args.mode == "aim" || args.mode == "calibrate") {
        auto mouse = CreateMouseOutput(cfg);
        if (!mouse || !mouse->Connect()) return 1;
        std::printf("[mouse] %s\n", mouse->Name());
        if (args.mode == "calibrate") return RunCalibrate(*mem, reader, *mouse, cfg);
        return RunAim(*mem, reader, *mouse, cfg, args);
    }

    std::fprintf(stderr, "unknown mode '%s'\n\n", args.mode.c_str());
    PrintHelp();
    return 1;
}
