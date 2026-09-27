#include "render.h"

#include <algorithm>
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
const Color kSoldierColor = {235, 85, 80, 255};
const Color kRunnerColor  = {255, 150, 60, 255};
const Color kHeavyColor   = {170, 45, 75, 255};
const Color kPlayerBullet = {255, 230, 120, 255};
const Color kBotBullet    = {255, 140, 70, 255};
const Color kText         = {230, 232, 240, 255};
const Color kTextDim      = {150, 155, 170, 255};
const Color kPanel        = {10, 12, 16, 210};
const Color kAccent       = {120, 220, 160, 255};
const Color kHealthGreen  = {90, 230, 120, 255};

// Fog of war overlay (rendered into its own texture, see DrawFog).
RenderTexture2D g_fogTexture{};
bool g_fogTextureLoaded = false;

Color TeamColor(uint32_t team) { return team == gm::TEAM_PLAYER ? kPlayerColor : kSoldierColor; }

Color KindColor(uint32_t kind) {
    switch (kind) {
        case gm::KIND_PLAYER: return kPlayerColor;
        case gm::KIND_RUNNER: return kRunnerColor;
        case gm::KIND_HEAVY:  return kHeavyColor;
        default:              return kSoldierColor;
    }
}

Vector2 V(Vec2f v) { return Vector2{v.x, v.y}; }

Color WithAlpha(Color c, float a) {
    c.a = static_cast<unsigned char>(Clampf(a, 0.0f, 1.0f) * 255.0f);
    return c;
}

Color Darker(Color c, float f) {
    return Color{static_cast<unsigned char>(c.r * f), static_cast<unsigned char>(c.g * f),
                 static_cast<unsigned char>(c.b * f), c.a};
}

Color Mix(Color a, Color b, float t) {
    t = Clampf(t, 0.0f, 1.0f);
    return Color{static_cast<unsigned char>(a.r + (b.r - a.r) * t), static_cast<unsigned char>(a.g + (b.g - a.g) * t),
                 static_cast<unsigned char>(a.b + (b.b - a.b) * t), a.a};
}

// Deterministic pseudo random number in [0,1) for effect particles.
float Hash01(int a, int b) {
    uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
}

void DrawTextShadow(const char* text, int x, int y, int size, Color color) {
    DrawText(text, x + 1, y + 1, size, Color{0, 0, 0, static_cast<unsigned char>(160 * color.a / 255)});
    DrawText(text, x, y, size, color);
}

void DrawTextCentered(const char* text, int cx, int y, int size, Color color) {
    DrawTextShadow(text, cx - MeasureText(text, size) / 2, y, size, color);
}

// raylib wants counter-clockwise triangles; this accepts any order.
void DrawTriangleAny(Vector2 a, Vector2 b, Vector2 c, Color color) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0) DrawTriangle(a, b, c, color);
    else DrawTriangle(a, c, b, color);
}

// Should this entity be drawn? (fog of war hides enemies without line of sight)
bool IsShown(const gm::GameMemory& mem, int i) {
    const gm::Entity& e = mem.entities[i];
    if (!e.active || !e.alive) return false;
    if (i == static_cast<int>(mem.localPlayerIndex)) return true;
    bool playerAlive = mem.entities[mem.localPlayerIndex].alive != 0;
    return !mem.fogOfWar || !playerAlive || e.visible;
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

void DrawPickups(const Game& game, const gm::GameMemory& mem) {
    float t = mem.gameTime;
    for (int i = 0; i < gm::kMaxPickups; ++i) {
        const gm::Pickup& p = mem.pickups[i];
        if (p.pos.x == 0.0f && p.pos.y == 0.0f) continue;
        Vector2 c = V(p.pos);
        if (!p.available) {
            // respawning: faint outline + timer ring
            float frac = 1.0f - game.PickupRespawn(i) / 20.0f;
            DrawCircleLinesV(c, 14.0f, WithAlpha(kHealthGreen, 0.25f));
            DrawRing(c, 15.0f, 17.0f, -90.0f, -90.0f + 360.0f * frac, 24, WithAlpha(kHealthGreen, 0.35f));
            continue;
        }
        float pulse = 0.5f + 0.5f * std::sin(t * 4.0f + i);
        DrawCircleV(c, 20.0f + 4.0f * pulse, WithAlpha(kHealthGreen, 0.12f + 0.08f * pulse));
        DrawRectangleRounded(Rectangle{c.x - 13, c.y - 13, 26, 26}, 0.3f, 6, Color{235, 240, 235, 255});
        DrawRectangle(static_cast<int>(c.x - 3), static_cast<int>(c.y - 9), 6, 18, Color{220, 50, 60, 255});
        DrawRectangle(static_cast<int>(c.x - 9), static_cast<int>(c.y - 3), 18, 6, Color{220, 50, 60, 255});
    }
}

// Darkens everything the player can NOT see. Each wall casts a "shadow"
// polygon away from the player. The shadows are drawn opaque into a texture
// first and then blended once, so overlapping shadows don't get darker.
void DrawFog(const gm::GameMemory& mem, const Camera2D& cam) {
    const gm::Entity& p = mem.entities[mem.localPlayerIndex];
    if (!mem.fogOfWar || !p.alive) return;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (!g_fogTextureLoaded || g_fogTexture.texture.width != sw || g_fogTexture.texture.height != sh) {
        if (g_fogTextureLoaded) UnloadRenderTexture(g_fogTexture);
        g_fogTexture = LoadRenderTexture(sw, sh);
        g_fogTextureLoaded = true;
    }

    EndMode2D();
    BeginTextureMode(g_fogTexture);
    ClearBackground(BLANK);
    BeginMode2D(cam);
    const Vec2f eye = p.pos;
    const float far = 5000.0f;
    for (uint32_t i = 0; i < mem.obstacleCount; ++i) {
        const gm::Obstacle& o = mem.obstacles[i];
        Vec2f corners[4] = {{o.x, o.y}, {o.x + o.w, o.y}, {o.x + o.w, o.y + o.h}, {o.x, o.y + o.h}};
        float base = AngleOf(Vec2f{o.x + o.w * 0.5f, o.y + o.h * 0.5f} - eye);
        int lo = 0, hi = 0;
        float aLo = 1e9f, aHi = -1e9f;
        for (int c = 0; c < 4; ++c) {
            float a = AngleOf(corners[c] - eye) - base;
            while (a > kPi) a -= 2 * kPi;
            while (a < -kPi) a += 2 * kPi;
            if (a < aLo) { aLo = a; lo = c; }
            if (a > aHi) { aHi = a; hi = c; }
        }
        Vec2f a = corners[lo], b = corners[hi];
        Vec2f aFar = eye + Normalize(a - eye) * far;
        Vec2f bFar = eye + Normalize(b - eye) * far;
        Color black{0, 0, 0, 255};
        DrawTriangleAny(V(a), V(b), V(bFar), black);
        DrawTriangleAny(V(a), V(bFar), V(aFar), black);
    }
    EndMode2D();
    EndTextureMode();

    // Render textures are upside down in OpenGL -> negative height flips it.
    DrawTextureRec(g_fogTexture.texture, Rectangle{0, 0, static_cast<float>(sw), -static_cast<float>(sh)},
                   Vector2{0, 0}, Color{255, 255, 255, 150});
    BeginMode2D(cam);
}

void DrawCorpses(const Game& game) {
    int index = 0;
    for (const Effect& fx : game.Effects()) {
        ++index;
        if (fx.type != EffectType::Corpse) continue;
        float fade = Clampf((fx.duration - fx.time) / 2.0f, 0.0f, 1.0f);  // fade out in the last 2 s
        Color c = fx.team == gm::TEAM_PLAYER ? kPlayerColor : kSoldierColor;
        // blood pool + body
        for (int i = 0; i < 6; ++i) {
            float a = 6.2831853f * Hash01(index, i);
            float d = fx.size * 0.9f * Hash01(index, i + 11);
            DrawCircleV(V(fx.pos + FromAngle(a) * d), fx.size * (0.35f + 0.3f * Hash01(index, i + 5)),
                        WithAlpha(Color{90, 15, 20, 255}, 0.45f * fade));
        }
        DrawCircleV(V(fx.pos), fx.size * 0.85f, WithAlpha(Darker(c, 0.35f), 0.8f * fade));
    }
}

void DrawEntity(const Game& game, const gm::GameMemory& mem, int slot) {
    const gm::Entity& e = mem.entities[slot];
    Color body = KindColor(e.kind);
    bool hidden = !e.visible;  // no line of sight (only drawn like this when fog is off)
    if (hidden) body = WithAlpha(Darker(body, 0.8f), 0.55f);
    float alpha = body.a / 255.0f;
    body = Mix(body, WHITE, game.HitFlash(slot) / 0.1f);  // flash white when hit

    Vector2 pos = V(e.pos);
    Vec2f dir = FromAngle(e.aimAngle);
    float gunLen = e.weapon == gm::WEAPON_SNIPER ? 24.0f : (e.weapon == gm::WEAPON_SHOTGUN ? 12.0f : 14.0f);
    float gunWidth = e.weapon == gm::WEAPON_SHOTGUN ? 11.0f : 9.0f;
    Vector2 gunStart = V(e.pos + dir * (e.radius * 0.3f));
    Vector2 gunEnd   = V(e.pos + dir * (e.radius + gunLen));

    DrawCircleV(Vector2{pos.x + 5, pos.y + 7}, e.radius, kShadow);
    DrawLineEx(gunStart, gunEnd, gunWidth, WithAlpha(Color{20, 22, 28, 255}, alpha));
    DrawLineEx(gunStart, gunEnd, gunWidth - 4.0f, WithAlpha(Color{90, 95, 110, 255}, alpha));
    if (e.kind == gm::KIND_HEAVY) {  // armor ring
        DrawCircleV(pos, e.radius + 3.0f, WithAlpha(Color{60, 60, 70, 255}, alpha));
    }
    DrawCircleV(pos, e.radius, WithAlpha(Darker(body, 0.45f), alpha));
    DrawCircleV(pos, e.radius - 3.0f, body);
    if (e.kind == gm::KIND_RUNNER) {  // speed stripes
        Vec2f side = Perp(Normalize(e.vel));
        if (LengthSq(e.vel) > 100.0f) {
            Vec2f back = e.pos - Normalize(e.vel) * (e.radius + 4.0f);
            for (int k = -1; k <= 1; ++k)
                DrawLineEx(V(back + side * (6.0f * k)), V(back + side * (6.0f * k) - Normalize(e.vel) * 12.0f), 2.0f,
                           WithAlpha(kRunnerColor, 0.5f * alpha));
        }
    }
    // small highlight
    DrawCircleV(Vector2{pos.x - e.radius * 0.3f, pos.y - e.radius * 0.35f}, e.radius * 0.3f,
                WithAlpha(WHITE, 0.18f * alpha));

    // spawn protection bubble
    if (slot == static_cast<int>(mem.localPlayerIndex) && game.SpawnProtection() > 0.0f) {
        float pulse = 0.5f + 0.5f * std::sin(mem.gameTime * 12.0f);
        DrawCircleLinesV(pos, e.radius + 7.0f, WithAlpha(SKYBLUE, 0.4f + 0.5f * pulse));
        DrawCircleV(pos, e.radius + 7.0f, WithAlpha(SKYBLUE, 0.1f));
    }
}

void DrawBullets(const Game& game) {
    for (const Bullet& b : game.Bullets()) {
        if (!b.active) continue;
        Color c = b.team == gm::TEAM_PLAYER ? kPlayerBullet : kBotBullet;
        if (b.heavy) {
            Vec2f tail = b.pos - Normalize(b.vel) * 60.0f;
            DrawLineEx(V(tail), V(b.pos), 5.0f, WithAlpha(c, 0.35f));
            DrawLineEx(V(tail), V(b.pos), 2.0f, WHITE);
            DrawCircleV(V(b.pos), 4.0f, c);
            continue;
        }
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
                DrawCircleV(V(fx.pos), fx.size * (1.0f - t) + 2.0f, WithAlpha(c, 1.0f - t));
                DrawCircleV(V(fx.pos), fx.size * 2.5f * (1.0f - t), WithAlpha(c, 0.15f * (1.0f - t)));
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
                float r = fx.size / 18.0f;
                DrawRing(V(fx.pos), (10.0f + 50.0f * t) * r, (14.0f + 52.0f * t) * r, 0, 360, 36,
                         WithAlpha(c, 0.8f * (1.0f - t)));
                for (int i = 0; i < 12; ++i) {
                    float a = 6.2831853f * i / 12.0f + Hash01(index, i);
                    DrawCircleV(V(fx.pos + FromAngle(a) * (60.0f * t * r)), 4.0f * (1.0f - t),
                                WithAlpha(c, 1.0f - t));
                }
                break;
            }
            case EffectType::Pickup: {
                DrawRing(V(fx.pos), 10.0f + 40.0f * t, 14.0f + 42.0f * t, 0, 360, 36,
                         WithAlpha(kHealthGreen, 1.0f - t));
                break;
            }
            case EffectType::Corpse:
                break;  // drawn earlier (under everything)
        }
    }
}

// ---- screen space UI -------------------------------------------------------------

void DrawEntityOverlays(const Game& game, const gm::GameMemory& mem, const Camera2D& cam, bool debug) {
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem.entities[i];
        if (!e.active || !e.alive) continue;
        bool shown = IsShown(mem, i);
        if (!shown && !debug) continue;
        Vector2 s = GetWorldToScreen2D(V(e.pos), cam);
        float r = e.radius * cam.zoom;

        if (shown && i != static_cast<int>(mem.localPlayerIndex)) {
            // health bar + name
            float frac = static_cast<float>(e.health) / static_cast<float>(e.maxHealth);
            float w = 20.0f + e.radius * 1.4f;
            Rectangle bg{s.x - w / 2, s.y - r - 16, w, 6};
            DrawRectangleRec(bg, Color{0, 0, 0, 170});
            DrawRectangleRec(Rectangle{bg.x + 1, bg.y + 1, (w - 2) * frac, 4},
                             frac > 0.5f ? kAccent : (frac > 0.25f ? ORANGE : RED));
            char label[48];
            if (e.kind == gm::KIND_SOLDIER) std::snprintf(label, sizeof(label), "%s", e.name);
            else std::snprintf(label, sizeof(label), "%s  %s", e.name, KindName(e.kind));
            int tw = MeasureText(label, 10);
            DrawText(label, static_cast<int>(s.x) - tw / 2, static_cast<int>(bg.y) - 12, 10,
                     e.visible ? kText : kTextDim);
            if (e.reloadTimer > 0.0f) {
                DrawText("reloading", static_cast<int>(s.x) - MeasureText("reloading", 10) / 2,
                         static_cast<int>(s.y + r + 6), 10, kTextDim);
            }
        }

        if (debug) {
            uint64_t addr = reinterpret_cast<uint64_t>(&mem.entities[i]);
            char buf[96];
            Color c = shown ? YELLOW : WithAlpha(YELLOW, 0.5f);
            std::snprintf(buf, sizeof(buf), "[%d] 0x%" PRIX64 "%s", i, addr, shown ? "" : " (hidden)");
            DrawText(buf, static_cast<int>(s.x + r + 6), static_cast<int>(s.y - 10), 10, c);
            std::snprintf(buf, sizeof(buf), "hp %d  (%.0f, %.0f)", e.health, e.pos.x, e.pos.y);
            DrawText(buf, static_cast<int>(s.x + r + 6), static_cast<int>(s.y + 2), 10, c);
            if (!shown) DrawCircleLinesV(s, r, WithAlpha(YELLOW, 0.5f));
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
        int size = t.value >= 100 ? 30 : 20;
        DrawTextCentered(buf, static_cast<int>(s.x), static_cast<int>(s.y) - 20, size,
                         WithAlpha(t.value >= 100 ? ORANGE : kPlayerBullet, 1.0f - t.time / 0.8f));
    }
}

void DrawCrosshair(const Game& game, const gm::GameMemory& mem, const Camera2D& cam) {
    Vector2 m{mem.mouseScreen.x, mem.mouseScreen.y};
    Color c = Color{255, 255, 255, 230};
    const gm::Entity& p = game.Player();
    const WeaponDef& w = GetWeaponDef(p.weapon);

    // Circle = where the pellets can go (shotgun), otherwise fixed size.
    float radius = 11.0f;
    if (w.spread > 0.0f && p.alive) {
        Vector2 ps = GetWorldToScreen2D(V(p.pos), cam);
        float dist = std::hypot(m.x - ps.x, m.y - ps.y);
        radius = std::max(11.0f, std::tan(w.spread * 0.5f) * dist);
    }
    float gap = radius > 11.0f ? radius - 5.0f : 6.0f;
    DrawCircleLinesV(m, radius, c);
    DrawLineEx(Vector2{m.x - gap - 12, m.y}, Vector2{m.x - gap, m.y}, 2.0f, c);
    DrawLineEx(Vector2{m.x + gap, m.y}, Vector2{m.x + gap + 12, m.y}, 2.0f, c);
    DrawLineEx(Vector2{m.x, m.y - gap - 12}, Vector2{m.x, m.y - gap}, 2.0f, c);
    DrawLineEx(Vector2{m.x, m.y + gap}, Vector2{m.x, m.y + gap + 12}, 2.0f, c);
    DrawCircleV(m, 1.5f, c);

    // Reload progress ring.
    if (p.reloadTimer > 0.0f) {
        float frac = 1.0f - p.reloadTimer / w.reloadTime;
        DrawRing(m, radius + 4.0f, radius + 7.0f, -90.0f, -90.0f + 360.0f * frac, 32, kAccent);
    } else if (p.weapon == gm::WEAPON_SNIPER && game.PlayerFireCooldown() > 0.0f) {
        float frac = 1.0f - game.PlayerFireCooldown() / w.fireDelay;
        DrawRing(m, radius + 4.0f, radius + 6.0f, -90.0f, -90.0f + 360.0f * frac, 32, WithAlpha(WHITE, 0.5f));
    }
    if (p.alive && p.ammo == 0 && p.reloadTimer <= 0.0f) {
        DrawTextCentered("RELOAD (R)", static_cast<int>(m.x), static_cast<int>(m.y + radius + 10), 10, ORANGE);
    }

    if (game.PlayerHitMarker() > 0.0f) {
        Color hc = WithAlpha(Color{255, 80, 80, 255}, game.PlayerHitMarker() / 0.15f);
        DrawLineEx(Vector2{m.x - 12, m.y - 12}, Vector2{m.x - 5, m.y - 5}, 3.0f, hc);
        DrawLineEx(Vector2{m.x + 12, m.y - 12}, Vector2{m.x + 5, m.y - 5}, 3.0f, hc);
        DrawLineEx(Vector2{m.x - 12, m.y + 12}, Vector2{m.x - 5, m.y + 5}, 3.0f, hc);
        DrawLineEx(Vector2{m.x + 12, m.y + 12}, Vector2{m.x + 5, m.y + 5}, 3.0f, hc);
    }
}

void DrawWeaponBar(const Game& game) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const gm::Entity& p = game.Player();
    const int boxW = 130, boxH = 50, gap = 8;
    const int total = gm::kWeaponCount * boxW + (gm::kWeaponCount - 1) * gap;
    int x = sw / 2 - total / 2;
    const int y = sh - boxH - 14;
    char buf[64];

    for (int w = 0; w < gm::kWeaponCount; ++w, x += boxW + gap) {
        const WeaponDef& def = GetWeaponDef(static_cast<uint32_t>(w));
        bool selected = p.weapon == static_cast<uint32_t>(w);
        DrawRectangle(x, y, boxW, boxH, WithAlpha(kPanel, selected ? 0.95f : 0.6f));
        if (selected) DrawRectangleLinesEx(Rectangle{static_cast<float>(x), static_cast<float>(y), boxW, boxH}, 2, kAccent);
        std::snprintf(buf, sizeof(buf), "%d  %s", w + 1, def.name);
        DrawText(buf, x + 10, y + 6, 20, selected ? kText : kTextDim);

        int ammo = game.PlayerAmmo(w);
        std::snprintf(buf, sizeof(buf), "%d / %d", ammo, def.magSize);
        Color ac = ammo == 0 ? RED : (ammo <= def.magSize / 4 ? ORANGE : (selected ? kPlayerBullet : kTextDim));
        DrawText(buf, x + 10, y + 29, 10, ac);

        // magazine pips
        int pips = std::min(def.magSize, 30);
        float pipW = 54.0f / pips;
        for (int i = 0; i < pips; ++i) {
            bool full = i < ammo * pips / def.magSize;
            DrawRectangle(x + 70 + static_cast<int>(i * pipW), y + 30, std::max(1, static_cast<int>(pipW) - 1), 8,
                          full ? ac : WithAlpha(kTextDim, 0.25f));
        }
        if (selected && p.reloadTimer > 0.0f) {
            float frac = 1.0f - p.reloadTimer / def.reloadTime;
            DrawRectangle(x + 2, y + boxH - 5, static_cast<int>((boxW - 4) * frac), 3, kAccent);
        }
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
    std::snprintf(buf, sizeof(buf), "Streak %d   Accuracy %.0f%%", game.KillStreak(), acc);
    DrawTextShadow(buf, 20, sh - 98, 20, kTextDim);
    std::snprintf(buf, sizeof(buf), "Kills %d   Deaths %d", p.kills, p.deaths);
    DrawTextShadow(buf, 20, sh - 74, 20, kText);

    DrawWeaponBar(game);

    // Top left: status
    std::snprintf(buf, sizeof(buf), "FPS %d   Bots %d (+/-)   Zoom %.2f", GetFPS(), game.BotCount(), mem.camZoom);
    DrawTextShadow(buf, 12, 10, 20, kTextDim);
    std::snprintf(buf, sizeof(buf), "%s (F6)   Fog %s (F7)%s", DifficultyName(mem.difficulty),
                  mem.fogOfWar ? "on" : "off", ui.muted ? "   Sound off (M)" : "");
    DrawTextShadow(buf, 12, 34, 20, kTextDim);
    int y = 58;
    if (game.godMode)      { DrawTextShadow("GOD MODE (F2)", 12, y, 20, GOLD); y += 22; }
    if (game.botsFrozen)   { DrawTextShadow("BOTS FROZEN (F3)", 12, y, 20, SKYBLUE); y += 22; }
    if (game.botsPeaceful) { DrawTextShadow("BOTS PEACEFUL (F4)", 12, y, 20, kAccent); y += 22; }
    if (mem.buttons & gm::BUTTON_RIGHT) { DrawTextShadow("RMB held", 12, y, 20, kTextDim); y += 22; }
    if (ui.helpHintTimer > 0.0f && !ui.showHelp) {
        DrawTextShadow("H = help / controls    F1 = memory debug    Tab = scores", 12, y, 20,
                       WithAlpha(kText, Clampf(ui.helpHintTimer, 0.0f, 1.0f)));
    }

    // Kill feed (top right)
    int ky = 10;
    for (const KillFeedEntry& k : game.KillFeed()) {
        int w = MeasureText(k.text.c_str(), 20);
        float alpha = Clampf(6.0f - k.time, 0.0f, 1.0f);
        DrawRectangle(sw - w - 28, ky, w + 16, 26, WithAlpha(kPanel, 0.8f * alpha));
        DrawText(k.text.c_str(), sw - w - 20, ky + 3, 20, WithAlpha(k.involvesPlayer ? GOLD : kText, alpha));
        ky += 30;
    }

    // Kill streak announcement
    const Announcement& an = game.CurrentAnnouncement();
    if (an.time < 2.0f && !an.text.empty()) {
        float pop = an.time < 0.12f ? 1.0f + (0.12f - an.time) * 4.0f : 1.0f;
        float alpha = Clampf((2.0f - an.time) / 0.5f, 0.0f, 1.0f);
        int size = static_cast<int>(44 * pop);
        DrawTextCentered(an.text.c_str(), sw / 2, 130, size, WithAlpha(GOLD, alpha));
    }

    if (game.SpawnProtection() > 0.0f && p.alive) {
        DrawTextCentered("SPAWN PROTECTION", sw / 2, sh - 110, 20, WithAlpha(SKYBLUE, 0.9f));
    }

    // Damage flash
    if (game.PlayerDamageFlash() > 0.0f) {
        float a = game.PlayerDamageFlash() / 0.25f * 0.35f;
        DrawRectangleGradientV(0, 0, sw, sh / 4, WithAlpha(RED, a), WithAlpha(RED, 0));
        DrawRectangleGradientV(0, sh - sh / 4, sw, sh / 4, WithAlpha(RED, 0), WithAlpha(RED, a));
    }
    // Low health vignette
    if (p.alive && frac < 0.3f) {
        float pulse = 0.5f + 0.5f * std::sin(mem.gameTime * 6.0f);
        float a = (0.3f - frac) / 0.3f * (0.15f + 0.1f * pulse);
        DrawRectangleGradientH(0, 0, sw / 5, sh, WithAlpha(RED, a), WithAlpha(RED, 0));
        DrawRectangleGradientH(sw - sw / 5, 0, sw / 5, sh, WithAlpha(RED, 0), WithAlpha(RED, a));
    }

    // Death screen
    if (!p.alive) {
        DrawRectangle(0, 0, sw, sh, Color{60, 0, 0, 90});
        DrawTextCentered("YOU DIED", sw / 2, sh / 2 - 50, 60, Color{255, 90, 90, 255});
        std::snprintf(buf, sizeof(buf), "respawning in %.1f", p.respawnTimer);
        DrawTextCentered(buf, sw / 2, sh / 2 + 20, 20, kText);
    }
}

void DrawMinimap(const Game& game, const gm::GameMemory& mem, const Camera2D& cam) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const float scale = 0.09f;
    const float w = mem.arenaSize.x * scale, h = mem.arenaSize.y * scale;
    const float x0 = sw - w - 16, y0 = sh - h - 16;

    DrawRectangleRec(Rectangle{x0 - 4, y0 - 4, w + 8, h + 8}, kPanel);
    DrawRectangleRec(Rectangle{x0, y0, w, h}, kFloor);
    for (uint32_t i = 0; i < mem.obstacleCount; ++i) {
        const gm::Obstacle& o = mem.obstacles[i];
        DrawRectangleRec(Rectangle{x0 + o.x * scale, y0 + o.y * scale, o.w * scale, o.h * scale}, kWallTop);
    }
    for (int i = 0; i < gm::kMaxPickups; ++i) {
        const gm::Pickup& pk = mem.pickups[i];
        if (!pk.available) continue;
        DrawRectangleV(Vector2{x0 + pk.pos.x * scale - 2, y0 + pk.pos.y * scale - 2}, Vector2{4, 4}, kHealthGreen);
    }
    // Current view rectangle
    Vector2 tl = GetScreenToWorld2D(Vector2{0, 0}, cam);
    Vector2 br = GetScreenToWorld2D(Vector2{static_cast<float>(sw), static_cast<float>(sh)}, cam);
    DrawRectangleLinesEx(
        Rectangle{x0 + tl.x * scale, y0 + tl.y * scale, (br.x - tl.x) * scale, (br.y - tl.y) * scale}, 1.0f,
        WithAlpha(WHITE, 0.4f));

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        if (!IsShown(mem, i)) continue;
        const gm::Entity& e = mem.entities[i];
        Color c = KindColor(e.kind);
        if (!e.visible) c = WithAlpha(c, 0.35f);
        DrawCircleV(Vector2{x0 + e.pos.x * scale, y0 + e.pos.y * scale},
                    i == static_cast<int>(mem.localPlayerIndex) ? 4.0f : 3.0f, c);
    }
    (void)game;
}

void DrawScoreboard(const gm::GameMemory& mem) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    int order[gm::kMaxEntities];
    int n = 0;
    for (int i = 0; i < gm::kMaxEntities; ++i)
        if (mem.entities[i].active) order[n++] = i;
    std::sort(order, order + n, [&](int a, int b) {
        const gm::Entity& ea = mem.entities[a];
        const gm::Entity& eb = mem.entities[b];
        if (ea.kills != eb.kills) return ea.kills > eb.kills;
        return ea.deaths < eb.deaths;
    });

    const int rowH = 22, w = 520;
    const int h = 70 + n * rowH;
    const int x = sw / 2 - w / 2, y = std::max(20, sh / 2 - h / 2);
    DrawRectangle(x, y, w, h, kPanel);
    DrawRectangleLines(x, y, w, h, kArenaBorder);
    DrawText("SCOREBOARD", x + 20, y + 14, 20, YELLOW);
    const int cols[] = {x + 20, x + 60, x + 220, x + 330, x + 400, x + 460};
    const char* head[] = {"#", "Name", "Type", "Kills", "Deaths", "HP"};
    for (int c = 0; c < 6; ++c) DrawText(head[c], cols[c], y + 44, 10, kTextDim);
    char buf[32];
    for (int r = 0; r < n; ++r) {
        const gm::Entity& e = mem.entities[order[r]];
        int ry = y + 60 + r * rowH;
        bool me = order[r] == static_cast<int>(mem.localPlayerIndex);
        if (me) DrawRectangle(x + 10, ry - 3, w - 20, rowH - 2, WithAlpha(kPlayerColor, 0.2f));
        Color c = e.alive ? kText : kTextDim;
        std::snprintf(buf, sizeof(buf), "%d", r + 1);
        DrawText(buf, cols[0], ry, 20, c);
        DrawText(e.name, cols[1], ry, 20, me ? kPlayerColor : c);
        DrawText(KindName(e.kind), cols[2], ry, 20, KindColor(e.kind));
        std::snprintf(buf, sizeof(buf), "%d", e.kills);
        DrawText(buf, cols[3], ry, 20, c);
        std::snprintf(buf, sizeof(buf), "%d", e.deaths);
        DrawText(buf, cols[4], ry, 20, c);
        std::snprintf(buf, sizeof(buf), "%d", e.alive ? e.health : 0);
        DrawText(buf, cols[5], ry, 20, c);
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
    DrawRectangle(x - 6, y - 6, 640, 24 + 10 * lh + rows * lh + 10, kPanel);

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
    line(GOLD,  "g_game       0x%016" PRIX64 "   RVA 0x%" PRIX64 "   layout v%u", addr, addr - base,
         mem.layoutVersion);
    line(kText, "entities     0x%016" PRIX64 "   (g_game + 0x%zX, stride 0x%zX)",
         reinterpret_cast<uint64_t>(&mem.entities[0]), offsetof(gm::GameMemory, entities), sizeof(gm::Entity));
    line(kTextDim, "frame %u   time %.2f   entityCount %u   paused %u   difficulty %u   fog %u", mem.frameCount,
         mem.gameTime, mem.entityCount, mem.paused, mem.difficulty, mem.fogOfWar);
    line(kTextDim, "camTarget (%.1f, %.1f)  camOffset (%.1f, %.1f)  zoom %.3f", mem.camTarget.x, mem.camTarget.y,
         mem.camOffset.x, mem.camOffset.y, mem.camZoom);
    line(kTextDim, "mouseScreen (%.1f, %.1f)  mouseWorld (%.1f, %.1f)  buttons 0x%X  bulletSpeed %.0f",
         mem.mouseScreen.x, mem.mouseScreen.y, mem.mouseWorld.x, mem.mouseWorld.y, mem.buttons, mem.bulletSpeed);
    line(kTextDim, "full layout: shooter_offsets.txt / console window");
    y += 4;

    const int cols[] = {x, x + 26, x + 150, x + 190, x + 222, x + 256, x + 326, x + 396, x + 456, x + 516, x + 546, x + 580};
    const char* head[] = {"idx", "address", "id", "alive", "hp", "pos.x", "pos.y", "vel.x", "vel.y", "vis", "kind", "ammo"};
    for (int c = 0; c < 12; ++c) DrawText(head[c], cols[c], y, 10, YELLOW);
    y += lh;

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem.entities[i];
        if (!e.active) continue;
        Color c = e.alive ? (e.team == gm::TEAM_PLAYER ? kPlayerColor : kText) : kTextDim;
        char cell[12][40];
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
        std::snprintf(cell[10], 40, "%u", e.kind);
        std::snprintf(cell[11], 40, "%d", e.ammo);
        for (int col = 0; col < 12; ++col) DrawText(cell[col], cols[col], y, 10, c);
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
        {"1 / 2 / 3", "rifle / shotgun / sniper"},
        {"Q", "previous weapon"},
        {"R", "reload"},
        {"Mouse wheel", "zoom"},
        {"Tab (hold)", "scoreboard"},
        {"+ / -", "more / fewer bots"},
        {"Esc / P", "pause"},
        {"M", "sound on / off"},
        {"", ""},
        {"F1", "memory debug overlay (addresses)"},
        {"F2", "god mode"},
        {"F3", "freeze bots (static targets)"},
        {"F4", "peaceful bots (no shooting)"},
        {"F5", "restart round"},
        {"F6", "difficulty easy / normal / hard"},
        {"F7", "fog of war on / off"},
        {"H", "close this help"},
    };
    const int n = sizeof(lines) / sizeof(lines[0]);
    const int w = 640, h = n * 24 + 80;
    const int x = sw / 2 - w / 2, y = std::max(10, sh / 2 - h / 2);
    DrawRectangle(x, y, w, h, kPanel);
    DrawRectangleLines(x, y, w, h, kArenaBorder);
    DrawText("CONTROLS  (game paused)", x + 30, y + 20, 20, YELLOW);
    for (int i = 0; i < n; ++i) {
        int ly = y + 60 + i * 24;
        DrawText(lines[i][0], x + 30, ly, 20, GOLD);
        DrawText(lines[i][1], x + 220, ly, 20, kText);
    }
}

}  // namespace

void DrawFrame(const Game& game, const gm::GameMemory& mem, const Camera2D& camera, const UiState& ui) {
    ClearBackground(kBackground);

    BeginMode2D(camera);
    DrawFloor(mem);
    DrawCorpses(game);
    DrawPickups(game, mem);
    DrawFog(mem, camera);
    DrawObstacles(mem);
    for (int i = gm::kMaxEntities - 1; i >= 0; --i) {  // player (slot 0) drawn last = on top
        if (IsShown(mem, i)) DrawEntity(game, mem, i);
    }
    DrawBullets(game);
    DrawEffects(game);
    EndMode2D();

    DrawEntityOverlays(game, mem, camera, ui.debugOverlay);
    DrawMinimap(game, mem, camera);
    DrawHud(game, mem, ui);
    if (ui.debugOverlay) DrawDebugPanel(mem);
    if (ui.showScoreboard) DrawScoreboard(mem);
    if (ui.showHelp) DrawHelp();
    if (ui.paused && !ui.showHelp) {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 120});
        DrawTextCentered("PAUSED", GetScreenWidth() / 2, GetScreenHeight() / 2 - 30, 60, kText);
        DrawTextCentered("Esc / P to continue", GetScreenWidth() / 2, GetScreenHeight() / 2 + 40, 20, kTextDim);
    }
    DrawCrosshair(game, mem, camera);
}

void ShutdownRenderer() {
    if (g_fogTextureLoaded) UnloadRenderTexture(g_fogTexture);
    g_fogTextureLoaded = false;
}
