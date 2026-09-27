#include "render.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>

#include "memory_export.h"

namespace {

constexpr float kPi = 3.14159265358979f;

// ---- palette ------------------------------------------------------------------
const Color kBackground   = {18, 20, 26, 255};
const Color kFloor        = {28, 31, 39, 255};
const Color kGrid         = {36, 40, 50, 255};
const Color kArenaBorder  = {90, 98, 120, 255};
const Color kWallFill     = {62, 67, 82, 255};
const Color kWallTop      = {84, 91, 110, 255};
const Color kWallEdge     = {24, 26, 32, 255};
const Color kShadow       = {0, 0, 0, 70};
const Color kPlayerColor  = {70, 160, 255, 255};
const Color kBotColor     = {235, 85, 80, 255};
const Color kPlayerBullet = {255, 230, 120, 255};
const Color kBotBullet    = {255, 140, 70, 255};
const Color kText         = {230, 232, 240, 255};
const Color kTextDim      = {150, 155, 170, 255};
const Color kPanel        = {10, 12, 16, 210};
const Color kAccent       = {120, 220, 160, 255};

Color TeamColor(uint32_t team) { return team == gm::TEAM_PLAYER ? kPlayerColor : kBotColor; }

Vector2 V(Vec2f v) { return Vector2{v.x, v.y}; }

Color WithAlpha(Color c, float a) {
    c.a = static_cast<unsigned char>(Clampf(a, 0.0f, 1.0f) * 255.0f);
    return c;
}

Color Darker(Color c, float f) {
    return Color{static_cast<unsigned char>(c.r * f), static_cast<unsigned char>(c.g * f),
                 static_cast<unsigned char>(c.b * f), c.a};
}

// Deterministic pseudo random number in [0,1) for effect particles.
float Hash01(int a, int b) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
}

void DrawTextShadow(const char* text, int x, int y, int size, Color color) {
    DrawText(text, x + 1, y + 1, size, Color{0, 0, 0, 160});
    DrawText(text, x, y, size, color);
}

void DrawTextCentered(const char* text, int cx, int y, int size, Color color) {
    DrawTextShadow(text, cx - MeasureText(text, size) / 2, y, size, color);
}

// ---- world ---------------------------------------------------------------------

void DrawFloor(const gm::GameMemory& mem) {
    const float w = mem.arenaSize.x, h = mem.arenaSize.y;
    DrawRectangleRec(Rectangle{0, 0, w, h}, kFloor);
    for (float x = 0; x <= w; x += 100.0f) DrawLineV(Vector2{x, 0}, Vector2{x, h}, kGrid);
    for (float y = 0; y <= h; y += 100.0f) DrawLineV(Vector2{0, y}, Vector2{w, y}, kGrid);
    DrawRectangleLinesEx(Rectangle{-6, -6, w + 12, h + 12}, 6.0f, kArenaBorder);
}

void DrawObstacles(const gm::GameMemory& mem) {
    for (uint32_t i = 0; i < mem.obstacleCount; ++i) {
        const gm::Obstacle& o = mem.obstacles[i];
        DrawRectangleRec(Rectangle{o.x + 8, o.y + 10, o.w, o.h}, kShadow);
    }
    for (uint32_t i = 0; i < mem.obstacleCount; ++i) {
        const gm::Obstacle& o = mem.obstacles[i];
        Rectangle r{o.x, o.y, o.w, o.h};
        DrawRectangleRec(r, kWallFill);
        DrawRectangleRec(Rectangle{o.x + 4, o.y + 4, o.w - 8, o.h - 8}, kWallTop);
        DrawRectangleLinesEx(r, 2.0f, kWallEdge);
    }
}

void DrawEntity(const gm::Entity& e) {
    Color body = TeamColor(e.team);
    if (!e.visible) body = WithAlpha(Darker(body, 0.8f), 0.55f);  // no line of sight

    Vector2 pos = V(e.pos);
    Vec2f dir = FromAngle(e.aimAngle);
    Vector2 gunStart = V(e.pos + dir * (e.radius * 0.3f));
    Vector2 gunEnd   = V(e.pos + dir * (e.radius + 14.0f));

    DrawCircleV(Vector2{pos.x + 5, pos.y + 7}, e.radius, kShadow);
    DrawLineEx(gunStart, gunEnd, 9.0f, WithAlpha(Color{20, 22, 28, 255}, body.a / 255.0f));
    DrawLineEx(gunStart, gunEnd, 5.0f, WithAlpha(Color{90, 95, 110, 255}, body.a / 255.0f));
    DrawCircleV(pos, e.radius, WithAlpha(Darker(body, 0.45f), body.a / 255.0f));
    DrawCircleV(pos, e.radius - 3.0f, body);
    // small highlight
    DrawCircleV(Vector2{pos.x - e.radius * 0.3f, pos.y - e.radius * 0.35f}, e.radius * 0.3f,
                WithAlpha(WHITE, 0.18f * body.a / 255.0f));
}

void DrawBullets(const Game& game) {
    for (const Bullet& b : game.Bullets()) {
        if (!b.active) continue;
        Color c = b.team == gm::TEAM_PLAYER ? kPlayerBullet : kBotBullet;
        Vec2f tail = b.pos - Normalize(b.vel) * 16.0f;
        DrawLineEx(V(tail), V(b.pos), 3.0f, WithAlpha(c, 0.5f));
        DrawCircleV(V(b.pos), 3.0f, c);
    }
}

void DrawEffects(const Game& game) {
    int index = 0;
    for (const Effect& fx : game.Effects()) {
        ++index;
        float t = fx.time / fx.duration;  // 0..1
        switch (fx.type) {
            case EffectType::Muzzle: {
                Color c = fx.team == gm::TEAM_PLAYER ? kPlayerBullet : kBotBullet;
                DrawCircleV(V(fx.pos), 9.0f * (1.0f - t) + 2.0f, WithAlpha(c, 1.0f - t));
                break;
            }
            case EffectType::Spark: {
                for (int i = 0; i < 6; ++i) {
                    float a = fx.angle + kPi - 1.2f + 2.4f * Hash01(index, i);
                    float len = (10.0f + 14.0f * Hash01(index, i + 7)) * t + 3.0f;
                    Vec2f d = FromAngle(a);
                    DrawLineEx(V(fx.pos + d * (len * 0.5f)), V(fx.pos + d * len), 2.0f,
                               WithAlpha(Color{255, 220, 150, 255}, 1.0f - t));
                }
                break;
            }
            case EffectType::Blood: {
                Color c = Darker(TeamColor(fx.team), 0.8f);
                for (int i = 0; i < 7; ++i) {
                    float a = fx.angle - 0.7f + 1.4f * Hash01(index, i);
                    float dist = (8.0f + 26.0f * Hash01(index, i + 3)) * t;
                    DrawCircleV(V(fx.pos + FromAngle(a) * dist), 3.0f * (1.0f - t) + 1.0f,
                                WithAlpha(c, 1.0f - t));
                }
                break;
            }
            case EffectType::Death: {
                Color c = TeamColor(fx.team);
                DrawRing(V(fx.pos), 10.0f + 50.0f * t, 14.0f + 52.0f * t, 0, 360, 36,
                         WithAlpha(c, 0.8f * (1.0f - t)));
                for (int i = 0; i < 12; ++i) {
                    float a = 6.2831853f * i / 12.0f + Hash01(index, i);
                    DrawCircleV(V(fx.pos + FromAngle(a) * (60.0f * t)), 4.0f * (1.0f - t),
                                WithAlpha(c, 1.0f - t));
                }
                break;
            }
        }
    }
}

// ---- screen space UI -------------------------------------------------------------

void DrawEntityOverlays(const Game& game, const gm::GameMemory& mem, const Camera2D& cam,
                        bool debug) {
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem.entities[i];
        if (!e.active || !e.alive) continue;
        Vector2 s = GetWorldToScreen2D(V(e.pos), cam);
        float r = e.radius * cam.zoom;

        if (i != static_cast<int>(mem.localPlayerIndex)) {
            // health bar + name
            float frac = static_cast<float>(e.health) / static_cast<float>(e.maxHealth);
            float w = 44.0f;
            Rectangle bg{s.x - w / 2, s.y - r - 16, w, 6};
            DrawRectangleRec(bg, Color{0, 0, 0, 170});
            DrawRectangleRec(Rectangle{bg.x + 1, bg.y + 1, (w - 2) * frac, 4},
                             frac > 0.5f ? kAccent : (frac > 0.25f ? ORANGE : RED));
            int tw = MeasureText(e.name, 10);
            DrawText(e.name, static_cast<int>(s.x) - tw / 2, static_cast<int>(bg.y) - 12, 10,
                     e.visible ? kText : kTextDim);
        }

        if (debug) {
            uint64_t addr = reinterpret_cast<uint64_t>(&mem.entities[i]);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "[%d] 0x%" PRIX64, i, addr);
            DrawText(buf, static_cast<int>(s.x + r + 6), static_cast<int>(s.y - 10), 10, YELLOW);
            std::snprintf(buf, sizeof(buf), "hp %d  (%.0f, %.0f)", e.health, e.pos.x, e.pos.y);
            DrawText(buf, static_cast<int>(s.x + r + 6), static_cast<int>(s.y + 2), 10, YELLOW);
            // velocity vector (0.25 s ahead)
            Vector2 v = GetWorldToScreen2D(V(e.pos + e.vel * 0.25f), cam);
            DrawLineEx(s, v, 2.0f, SKYBLUE);
        }
    }

    // Floating damage numbers.
    for (const FloatingText& t : game.Texts()) {
        Vector2 s = GetWorldToScreen2D(V(t.pos), cam);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", t.value);
        DrawTextCentered(buf, static_cast<int>(s.x), static_cast<int>(s.y) - 20, 20,
                         WithAlpha(kPlayerBullet, 1.0f - t.time / 0.8f));
    }
}

void DrawCrosshair(const Game& game, const gm::GameMemory& mem) {
    Vector2 m{mem.mouseScreen.x, mem.mouseScreen.y};
    Color c = Color{255, 255, 255, 230};
    DrawCircleLinesV(m, 11.0f, c);
    DrawLineEx(Vector2{m.x - 18, m.y}, Vector2{m.x - 6, m.y}, 2.0f, c);
    DrawLineEx(Vector2{m.x + 6, m.y}, Vector2{m.x + 18, m.y}, 2.0f, c);
    DrawLineEx(Vector2{m.x, m.y - 18}, Vector2{m.x, m.y - 6}, 2.0f, c);
    DrawLineEx(Vector2{m.x, m.y + 6}, Vector2{m.x, m.y + 18}, 2.0f, c);
    DrawCircleV(m, 1.5f, c);

    if (game.PlayerHitMarker() > 0.0f) {
        Color hc = WithAlpha(Color{255, 80, 80, 255}, game.PlayerHitMarker() / 0.15f);
        DrawLineEx(Vector2{m.x - 12, m.y - 12}, Vector2{m.x - 5, m.y - 5}, 3.0f, hc);
        DrawLineEx(Vector2{m.x + 12, m.y - 12}, Vector2{m.x + 5, m.y - 5}, 3.0f, hc);
        DrawLineEx(Vector2{m.x - 12, m.y + 12}, Vector2{m.x - 5, m.y + 5}, 3.0f, hc);
        DrawLineEx(Vector2{m.x + 12, m.y + 12}, Vector2{m.x + 5, m.y + 5}, 3.0f, hc);
    }
}

void DrawHud(const Game& game, const gm::GameMemory& mem, const UiState& ui) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const gm::Entity& p = game.Player();
    char buf[128];

    // Health bar (bottom left)
    float frac = static_cast<float>(p.health) / static_cast<float>(p.maxHealth);
    Rectangle bar{20, static_cast<float>(sh - 48), 280, 26};
    DrawRectangleRec(bar, kPanel);
    DrawRectangleRec(Rectangle{bar.x + 3, bar.y + 3, (bar.width - 6) * frac, bar.height - 6},
                     frac > 0.5f ? kAccent : (frac > 0.25f ? ORANGE : RED));
    std::snprintf(buf, sizeof(buf), "HP %d / %d", p.health, p.maxHealth);
    DrawTextShadow(buf, static_cast<int>(bar.x) + 10, static_cast<int>(bar.y) + 4, 20, kText);

    float acc = game.shotsFired > 0 ? 100.0f * game.shotsHit / game.shotsFired : 0.0f;
    std::snprintf(buf, sizeof(buf), "Kills %d   Deaths %d   Accuracy %.0f%%", p.kills, p.deaths, acc);
    DrawTextShadow(buf, 20, sh - 74, 20, kText);

    // Top left: status
    std::snprintf(buf, sizeof(buf), "FPS %d   Bots %d (+/-)   Zoom %.2f", GetFPS(), game.BotCount(),
                  mem.camZoom);
    DrawTextShadow(buf, 12, 10, 20, kTextDim);
    int y = 34;
    if (game.godMode)      { DrawTextShadow("GOD MODE (F2)", 12, y, 20, GOLD); y += 22; }
    if (game.botsFrozen)   { DrawTextShadow("BOTS FROZEN (F3)", 12, y, 20, SKYBLUE); y += 22; }
    if (game.botsPeaceful) { DrawTextShadow("BOTS PEACEFUL (F4)", 12, y, 20, kAccent); y += 22; }
    if (mem.buttons & gm::BUTTON_RIGHT) { DrawTextShadow("RMB held", 12, y, 20, kTextDim); y += 22; }
    if (ui.helpHintTimer > 0.0f && !ui.showHelp) {
        DrawTextShadow("H = help / controls    F1 = memory debug", 12, y,
                       20, WithAlpha(kText, Clampf(ui.helpHintTimer, 0.0f, 1.0f)));
    }

    // Kill feed (top right)
    int ky = 10;
    for (const KillFeedEntry& k : game.KillFeed()) {
        int w = MeasureText(k.text.c_str(), 20);
        float alpha = Clampf(6.0f - k.time, 0.0f, 1.0f);
        DrawRectangle(sw - w - 28, ky, w + 16, 26, WithAlpha(kPanel, 0.8f * alpha));
        DrawText(k.text.c_str(), sw - w - 20, ky + 3, 20,
                 WithAlpha(k.involvesPlayer ? GOLD : kText, alpha));
        ky += 30;
    }

    // Damage flash
    if (game.PlayerDamageFlash() > 0.0f) {
        float a = game.PlayerDamageFlash() / 0.25f * 0.35f;
        DrawRectangleGradientV(0, 0, sw, sh / 4, WithAlpha(RED, a), WithAlpha(RED, 0));
        DrawRectangleGradientV(0, sh - sh / 4, sw, sh / 4, WithAlpha(RED, 0), WithAlpha(RED, a));
    }

    // Death screen
    if (!p.alive) {
        DrawRectangle(0, 0, sw, sh, Color{60, 0, 0, 90});
        DrawTextCentered("YOU DIED", sw / 2, sh / 2 - 50, 60, Color{255, 90, 90, 255});
        std::snprintf(buf, sizeof(buf), "respawning in %.1f", p.respawnTimer);
        DrawTextCentered(buf, sw / 2, sh / 2 + 20, 20, kText);
    }
}

void DrawMinimap(const gm::GameMemory& mem, const Camera2D& cam) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const float scale = 0.09f;
    const float w = mem.arenaSize.x * scale, h = mem.arenaSize.y * scale;
    const float x0 = sw - w - 16, y0 = sh - h - 16;

    DrawRectangleRec(Rectangle{x0 - 4, y0 - 4, w + 8, h + 8}, kPanel);
    DrawRectangleRec(Rectangle{x0, y0, w, h}, kFloor);
    for (uint32_t i = 0; i < mem.obstacleCount; ++i) {
        const gm::Obstacle& o = mem.obstacles[i];
        DrawRectangleRec(Rectangle{x0 + o.x * scale, y0 + o.y * scale, o.w * scale, o.h * scale},
                         kWallTop);
    }
    // Current view rectangle
    Vector2 tl = GetScreenToWorld2D(Vector2{0, 0}, cam);
    Vector2 br = GetScreenToWorld2D(Vector2{static_cast<float>(sw), static_cast<float>(sh)}, cam);
    DrawRectangleLinesEx(Rectangle{x0 + tl.x * scale, y0 + tl.y * scale, (br.x - tl.x) * scale,
                                   (br.y - tl.y) * scale},
                         1.0f, WithAlpha(WHITE, 0.4f));

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem.entities[i];
        if (!e.active || !e.alive) continue;
        Color c = TeamColor(e.team);
        if (!e.visible) c = WithAlpha(c, 0.35f);
        DrawCircleV(Vector2{x0 + e.pos.x * scale, y0 + e.pos.y * scale},
                    i == static_cast<int>(mem.localPlayerIndex) ? 4.0f : 3.0f, c);
    }
}

void DrawDebugPanel(const gm::GameMemory& mem) {
    const uint64_t base = GetModuleBaseAddress();
    const uint64_t addr = reinterpret_cast<uint64_t>(&mem);
    const int x = 12, lh = 12;
    int y = 110;
    char buf[256];

    int rows = 0;
    for (int i = 0; i < gm::kMaxEntities; ++i) rows += mem.entities[i].active ? 1 : 0;
    DrawRectangle(x - 6, y - 6, 560, 24 + 10 * lh + rows * lh + 10, kPanel);

    DrawText("MEMORY DEBUG (F1)", x, y, 20, YELLOW);
    y += 24;
    auto line = [&](Color c, const char* fmt, auto... args) {
        if constexpr (sizeof...(args) == 0) std::snprintf(buf, sizeof(buf), "%s", fmt);
        else std::snprintf(buf, sizeof(buf), fmt, args...);
        DrawText(buf, x, y, 10, c);
        y += lh;
    };
    line(kText, "process      %s  pid %u", GetExecutableName().c_str(), GetProcessIdSelf());
    line(kText, "module base  0x%016" PRIX64, base);
    line(GOLD,  "g_game       0x%016" PRIX64 "   RVA 0x%" PRIX64, addr, addr - base);
    line(kText, "entities     0x%016" PRIX64 "   (g_game + 0x%zX, stride 0x%zX)",
         reinterpret_cast<uint64_t>(&mem.entities[0]), offsetof(gm::GameMemory, entities),
         sizeof(gm::Entity));
    line(kTextDim, "frame %u   time %.2f   entityCount %u   paused %u", mem.frameCount,
         mem.gameTime, mem.entityCount, mem.paused);
    line(kTextDim, "camTarget (%.1f, %.1f)  camOffset (%.1f, %.1f)  zoom %.3f", mem.camTarget.x,
         mem.camTarget.y, mem.camOffset.x, mem.camOffset.y, mem.camZoom);
    line(kTextDim, "mouseScreen (%.1f, %.1f)  mouseWorld (%.1f, %.1f)  buttons 0x%X",
         mem.mouseScreen.x, mem.mouseScreen.y, mem.mouseWorld.x, mem.mouseWorld.y, mem.buttons);
    line(kTextDim, "full layout: shooter_offsets.txt / console window");
    y += 4;

    const int cols[] = {x, x + 26, x + 150, x + 190, x + 222, x + 256, x + 326, x + 396, x + 456, x + 516};
    const char* head[] = {"idx", "address", "id", "alive", "hp", "pos.x", "pos.y", "vel.x", "vel.y", "vis"};
    for (int c = 0; c < 10; ++c) DrawText(head[c], cols[c], y, 10, YELLOW);
    y += lh;

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem.entities[i];
        if (!e.active) continue;
        Color c = e.alive ? (e.team == gm::TEAM_PLAYER ? kPlayerColor : kText) : kTextDim;
        char cell[10][40];
        std::snprintf(cell[0], 40, "%d", i);
        std::snprintf(cell[1], 40, "0x%" PRIX64, reinterpret_cast<uint64_t>(&e));
        std::snprintf(cell[2], 40, "%u", e.id);
        std::snprintf(cell[3], 40, "%u", e.alive);
        std::snprintf(cell[4], 40, "%d", e.health);
        std::snprintf(cell[5], 40, "%.1f", e.pos.x);
        std::snprintf(cell[6], 40, "%.1f", e.pos.y);
        std::snprintf(cell[7], 40, "%.0f", e.vel.x);
        std::snprintf(cell[8], 40, "%.0f", e.vel.y);
        std::snprintf(cell[9], 40, "%u", e.visible);
        for (int col = 0; col < 10; ++col) DrawText(cell[col], cols[col], y, 10, c);
        y += lh;
    }
}

void DrawHelp() {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    // {key, description}; empty key + empty text = spacer
    const char* lines[][2] = {
        {"WASD / arrows", "move"},
        {"Mouse", "aim"},
        {"Left mouse", "shoot"},
        {"Mouse wheel", "zoom"},
        {"+ / -", "more / fewer bots"},
        {"R", "restart round"},
        {"Esc / P", "pause"},
        {"", ""},
        {"F1", "memory debug overlay (addresses)"},
        {"F2", "god mode"},
        {"F3", "freeze bots (static targets)"},
        {"F4", "peaceful bots (no shooting)"},
        {"H", "close this help"},
    };
    const int n = sizeof(lines) / sizeof(lines[0]);
    const int w = 640, h = n * 26 + 80;
    const int x = sw / 2 - w / 2, y = sh / 2 - h / 2;
    DrawRectangle(x, y, w, h, kPanel);
    DrawRectangleLines(x, y, w, h, kArenaBorder);
    DrawText("CONTROLS  (game paused)", x + 30, y + 20, 20, YELLOW);
    for (int i = 0; i < n; ++i) {
        int ly = y + 60 + i * 26;
        DrawText(lines[i][0], x + 30, ly, 20, GOLD);
        DrawText(lines[i][1], x + 220, ly, 20, kText);
    }
}

}  // namespace

void DrawFrame(const Game& game, const gm::GameMemory& mem, const Camera2D& camera,
               const UiState& ui) {
    ClearBackground(kBackground);

    BeginMode2D(camera);
    DrawFloor(mem);
    DrawObstacles(mem);
    for (int i = gm::kMaxEntities - 1; i >= 0; --i) {  // player (slot 0) drawn last = on top
        const gm::Entity& e = mem.entities[i];
        if (e.active && e.alive) DrawEntity(e);
    }
    DrawBullets(game);
    DrawEffects(game);
    EndMode2D();

    DrawEntityOverlays(game, mem, camera, ui.debugOverlay);
    DrawMinimap(mem, camera);
    DrawHud(game, mem, ui);
    if (ui.debugOverlay) DrawDebugPanel(mem);
    if (ui.showHelp) DrawHelp();
    if (ui.paused && !ui.showHelp) {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 120});
        DrawTextCentered("PAUSED", GetScreenWidth() / 2, GetScreenHeight() / 2 - 30, 60, kText);
        DrawTextCentered("Esc / P to continue", GetScreenWidth() / 2, GetScreenHeight() / 2 + 40,
                         20, kTextDim);
    }
    DrawCrosshair(game, mem);
}
