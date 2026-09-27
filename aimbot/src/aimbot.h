// =============================================================================
//  Aimbot logic - pure math, no memory reading and no mouse code in here.
//
//  Input : one snapshot of the game state (gm::GameMemory)
//  Output: how many mouse counts to move right now
//
//  Steps (see Aimbot::Update):
//    1. Is the aim key held? (read from the game's memory: GameMemory::buttons)
//    2. Collect enemies that are alive (and visible, if enabled).
//    3. Predict where each enemy will be when our bullet arrives.
//    4. Convert that world position to screen pixels (camera from memory).
//    5. Pick the enemy closest to the crosshair inside the FOV circle.
//    6. Move the mouse a fraction (1/smooth) of the distance towards it.
// =============================================================================
#pragma once

#include <cstdint>

#include "config.h"
#include "game_memory.h"

struct AimResult {
    bool     keyHeld     = false;
    bool     hasTarget   = false;
    int      targetIndex = -1;       // index into GameMemory::entities
    gm::Vec2f targetScreen{0, 0};    // aim point on screen (pixels)
    gm::Vec2f error{0, 0};           // aim point - crosshair (pixels)
    int      moveX = 0;              // mouse counts to send
    int      moveY = 0;
};

class Aimbot {
public:
    explicit Aimbot(const Config& cfg) : cfg_(cfg) {}

    // Call once per NEW game frame (when GameMemory::frameCount changed).
    AimResult Update(const gm::GameMemory& g);

    // Where will `target` be when a bullet fired from `shooter` hits it?
    static gm::Vec2f PredictAimPoint(const gm::Entity& shooter, const gm::Entity& target, float bulletSpeed);

private:
    bool AimKeyHeld(const gm::GameMemory& g) const;

    const Config& cfg_;
    uint32_t lockedId_ = 0;       // Entity::id of the current target (sticky)
    float    remainderX_ = 0.0f;  // sub-pixel movement carried to the next step
    float    remainderY_ = 0.0f;
};
