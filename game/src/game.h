// Game simulation (no rendering, no raylib). All state that external tools
// might want to read is stored in the global gm::GameMemory (see
// shared/game_memory.h). Internal-only data (bullets, AI state, effects) is
// kept in this class.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "game_memory.h"
#include "vec2.h"

struct PlayerInput {
    Vec2f move{0, 0};      // desired move direction (WASD), any length
    Vec2f aimWorld{0, 0};  // world position the player aims at (mouse)
    bool  shoot = false;   // fire button held
};

struct Bullet {
    bool     active = false;
    Vec2f    pos{0, 0};
    Vec2f    prevPos{0, 0};
    Vec2f    vel{0, 0};
    float    life = 0.0f;
    int      owner = -1;  // entity slot that fired it
    uint32_t team = 0;
    int      damage = 0;
};

struct BotBrain {
    float decisionTimer = 0.0f;  // time until next movement decision
    float strafeDir     = 1.0f;  // +1 / -1: circle clockwise or counter-clockwise
    float preferredDist = 350.0f;
    float jitterAngle   = 0.0f;
    float dashTimer     = 0.0f;  // > 0 while dashing
    float dashCooldown  = 0.0f;
    float fireCooldown  = 0.0f;
    float reaction      = 0.0f;  // delay before first shot after spotting the player
    float aimError      = 0.0f;
    float baseSpeed     = 240.0f;
    float stuckTimer    = 0.0f;
    float lastSeenTimer = 99.0f;  // seconds since the player was last seen
    Vec2f lastSeenPos{0, 0};
    Vec2f wanderTarget{0, 0};
};

enum class EffectType { Spark, Blood, Death, Muzzle };

struct Effect {
    EffectType type;
    Vec2f      pos;
    float      angle;
    float      time;      // age in seconds
    float      duration;
    uint32_t   team;
};

struct FloatingText {
    Vec2f pos;
    float time;
    int   value;
};

struct KillFeedEntry {
    std::string text;
    float       time;
    bool        involvesPlayer;
};

class Game {
public:
    static constexpr float kTickRate = 120.0f;
    static constexpr float kTickDt   = 1.0f / kTickRate;

    Game(gm::GameMemory& mem, uint32_t seed);

    void Reset();                 // new round: respawn everyone, clear scores
    void SetBotCount(int count);  // 0 .. kMaxEntities-1
    int  BotCount() const { return botCount_; }

    // Advance the simulation by one fixed step (kTickDt).
    void Tick(const PlayerInput& input);

    // Recompute Entity::visible (line of sight from the local player).
    void UpdateVisibility();

    bool LineOfSight(Vec2f a, Vec2f b) const;

    // --- debug / cheat toggles (keys F2..F4) --------------------------------
    bool godMode      = false;
    bool botsFrozen   = false;
    bool botsPeaceful = false;

    // --- data for the renderer ---------------------------------------------
    const std::vector<Bullet>&        Bullets() const { return bullets_; }
    const std::vector<Effect>&        Effects() const { return effects_; }
    const std::vector<FloatingText>&  Texts() const { return texts_; }
    const std::vector<KillFeedEntry>& KillFeed() const { return killFeed_; }
    const BotBrain& Brain(int slot) const { return brains_[slot]; }
    float PlayerHitMarker() const { return hitMarker_; }
    float PlayerDamageFlash() const { return damageFlash_; }

    int shotsFired = 0;
    int shotsHit   = 0;

    gm::Entity&       Player() { return mem_.entities[mem_.localPlayerIndex]; }
    const gm::Entity& Player() const { return mem_.entities[mem_.localPlayerIndex]; }

private:
    void BuildArena();
    void SpawnEntity(int slot);
    Vec2f FindSpawnPoint(bool farFromPlayer);

    void UpdatePlayer(const PlayerInput& input, float dt);
    void UpdateBot(int slot, float dt);
    void UpdateBullets(float dt);
    void UpdateEffects(float dt);
    void UpdateRespawns(float dt);

    void FireBullet(int slot, float angle, float speed, int damage);
    void ApplyDamage(int victim, int attacker, int damage, Vec2f hitPos, float angle);

    // Moves a circle and resolves collisions with walls/arena bounds.
    // Returns the actual displacement.
    Vec2f MoveCircle(Vec2f& pos, Vec2f delta, float radius) const;
    bool  CircleHitsObstacle(Vec2f pos, float radius) const;

    float RandF(float lo, float hi);
    int   RandI(int lo, int hi);

    gm::GameMemory& mem_;
    std::mt19937    rng_;

    int      botCount_ = 8;
    uint32_t nextId_   = 1;
    float    playerFireCooldown_ = 0.0f;
    float    hitMarker_   = 0.0f;
    float    damageFlash_ = 0.0f;

    BotBrain                   brains_[gm::kMaxEntities];
    std::vector<Bullet>        bullets_;
    std::vector<Effect>        effects_;
    std::vector<FloatingText>  texts_;
    std::vector<KillFeedEntry> killFeed_;
};
