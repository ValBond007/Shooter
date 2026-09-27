#include "aimbot.h"

#include <cmath>

namespace {

float Dist(gm::Vec2f a, gm::Vec2f b) { return std::hypot(a.x - b.x, a.y - b.y); }

}  // namespace

bool Aimbot::AimKeyHeld(const gm::GameMemory& g) const {
    if (cfg_.aimKey == "always") return true;
    if (cfg_.aimKey == "lmb") return (g.buttons & gm::BUTTON_LEFT) != 0;
    if (cfg_.aimKey == "mmb") return (g.buttons & gm::BUTTON_MIDDLE) != 0;
    return (g.buttons & gm::BUTTON_RIGHT) != 0;  // "rmb" (default)
}

gm::Vec2f Aimbot::PredictAimPoint(const gm::Entity& shooter, const gm::Entity& target, float bulletSpeed) {
    // Bullets fly in a straight line with constant speed and start at the
    // shooter's center. The target moves with (roughly) constant velocity.
    // time = distance / bulletSpeed, but the distance depends on where the
    // target will be -> iterate a few times, it converges very quickly.
    gm::Vec2f p = target.pos;
    if (bulletSpeed <= 1.0f) return p;
    for (int i = 0; i < 3; ++i) {
        float t = Dist(shooter.pos, p) / bulletSpeed;
        p = gm::Vec2f{target.pos.x + target.vel.x * t, target.pos.y + target.vel.y * t};
    }
    return p;
}

AimResult Aimbot::Update(const gm::GameMemory& g) {
    AimResult r;
    r.keyHeld = AimKeyHeld(g);

    if (g.localPlayerIndex >= static_cast<uint32_t>(gm::kMaxEntities)) return r;
    const gm::Entity& me = g.entities[g.localPlayerIndex];
    const gm::Vec2f crosshair = g.mouseScreen;

    bool mouseInWindow = crosshair.x >= 0 && crosshair.y >= 0 && crosshair.x < g.screenWidth &&
                         crosshair.y < g.screenHeight;
    if (!r.keyHeld || g.paused || !me.alive || !mouseInWindow) {
        lockedId_ = 0;
        remainderX_ = remainderY_ = 0.0f;
        return r;
    }

    // ---- choose a target -------------------------------------------------------
    int   best = -1;
    float bestDist = cfg_.fov;
    gm::Vec2f bestScreen{0, 0};

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = g.entities[i];
        if (i == static_cast<int>(g.localPlayerIndex)) continue;
        if (!e.active || !e.alive || e.team == me.team) continue;
        if (cfg_.visibleOnly && !e.visible) continue;

        gm::Vec2f world = cfg_.prediction ? PredictAimPoint(me, e, g.bulletSpeed) : e.pos;
        gm::Vec2f screen = gm::WorldToScreen(g, world);
        float d = Dist(screen, crosshair);

        // Keep the locked target as long as it is valid (and not too far out).
        if (cfg_.stickyTarget && e.id == lockedId_ && d <= cfg_.fov * 1.5f) {
            best = i;
            bestScreen = screen;
            break;
        }
        if (d < bestDist) {
            bestDist = d;
            best = i;
            bestScreen = screen;
        }
    }

    if (best < 0) {
        lockedId_ = 0;
        return r;
    }
    lockedId_ = g.entities[best].id;
    r.hasTarget = true;
    r.targetIndex = best;
    r.targetScreen = bestScreen;
    r.error = gm::Vec2f{bestScreen.x - crosshair.x, bestScreen.y - crosshair.y};

    // ---- compute the mouse movement -----------------------------------------------
    float len = std::hypot(r.error.x, r.error.y);
    if (len < cfg_.deadzone) return r;

    // Move only a part of the way each step: this makes it smooth AND stable,
    // because the game only sees our movement one frame later.
    float stepX = r.error.x / cfg_.smooth;
    float stepY = r.error.y / cfg_.smooth;
    float stepLen = std::hypot(stepX, stepY);
    if (cfg_.maxStep > 0 && stepLen > cfg_.maxStep) {
        stepX *= cfg_.maxStep / stepLen;
        stepY *= cfg_.maxStep / stepLen;
    }

    // Pixels -> mouse counts. Mice move in whole counts, so keep the
    // fractional part for the next step instead of losing it.
    float countsX = stepX * cfg_.mouseScale + remainderX_;
    float countsY = stepY * cfg_.mouseScale + remainderY_;
    r.moveX = static_cast<int>(std::lround(countsX));
    r.moveY = static_cast<int>(std::lround(countsY));
    remainderX_ = countsX - r.moveX;
    remainderY_ = countsY - r.moveY;
    return r;
}
