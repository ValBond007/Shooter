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
    Vec2f move{0, 0};       // desired move direction (WASD), any length
    Vec2f aimWorld{0, 0};   // world position the player aims at (mouse)
    bool  shoot = false;    // fire button held
    bool  reload = false;   // reload key pressed this frame
    int   selectWeapon = -1;  // 0..2 = switch weapon, -1 = no change
    bool  dash = false;     // dash key pressed this frame
};

// What the player picked in the main menu.
struct MatchSettings {
    uint32_t mode       = gm::MODE_DEATHMATCH;
    int      map        = 0;
    int      bots       = 8;
    uint32_t difficulty = gm::DIFFICULTY_NORMAL;
};

// Aim training statistics (MODE_TRAINING).
struct TrainingStats {
    int   targetsSpawned = 0;
    int   kills          = 0;
    float totalTimeToKill = 0.0f;   // sum of spawn -> kill times
    float totalReaction  = 0.0f;    // sum of spawn -> first hit times
    int   reactions      = 0;
    float bestTimeToKill = 0.0f;
    // current target
    float targetAge      = 0.0f;
    bool  targetHit      = false;
    float nextSpawn      = 0.0f;
};

// Shown on the result screen when a match ends.
struct MatchResult {
    std::string              title;     // "VICTORY", "DEFEAT", ...
    bool                     good = false;
    std::vector<std::string> lines;     // stats
    std::string              csvLine;   // appended to shooter_results.csv
};

struct WeaponDef {
    const char* name;
    float fireDelay;    // seconds between shots
    float bulletSpeed;  // units / second
    int   damage;       // per pellet
    int   pellets;      // bullets per shot
    float spread;       // total spread angle (radians)
    int   magSize;
    float reloadTime;   // seconds
    float bulletLife;   // seconds (range = speed * life)
    float shake;        // screen shake strength when firing
};

const WeaponDef& GetWeaponDef(uint32_t weapon);
const char* DifficultyName(uint32_t difficulty);
const char* KindName(uint32_t kind);
const char* ModeName(uint32_t mode);
const char* ModeDescription(uint32_t mode);

struct Bullet {
    bool     active = false;
    Vec2f    pos{0, 0};
    Vec2f    prevPos{0, 0};
    Vec2f    vel{0, 0};
    float    life = 0.0f;
    int      owner = -1;  // entity slot that fired it
    uint32_t team = 0;
    int      damage = 0;
    bool     heavy = false;  // sniper round (drawn bigger)
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
    float lastSeenTimer = 99.0f;  // seconds since the target was last seen
    int   target        = -1;     // entity slot this bot is fighting
    float retargetTimer = 0.0f;
    Vec2f lastSeenPos{0, 0};
    Vec2f wanderTarget{0, 0};
};

enum class EffectType { Spark, Blood, Death, Muzzle, Corpse, Pickup };

struct Effect {
    EffectType type;
    Vec2f      pos;
    float      angle;
    float      time;      // age in seconds
    float      duration;
    uint32_t   team;
    float      size = 18.0f;
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

// Things that happened this tick, for sound and screen shake (main.cpp).
enum class GameEventType {
    Shot, PlayerShot, Hit, PlayerHitEnemy, PlayerHurt, Kill, PlayerKill, PlayerDied,
    Reload, ReloadDone, Empty, WeaponSwitch, Pickup, WallHit, Respawn, Dash, WaveStart,
    MatchEnd,
};

struct GameEvent {
    GameEventType type;
    Vec2f         pos;
    uint32_t      weapon = 0;
};

struct Announcement {
    std::string text;
    float       time = 99.0f;  // age
};

class Game {
public:
    static constexpr float kTickRate = 120.0f;
    static constexpr float kTickDt   = 1.0f / kTickRate;

    Game(gm::GameMemory& mem, uint32_t seed);

    // Starts a new match (mode, map, bots, difficulty). Reset() restarts the current one.
    void StartMatch(const MatchSettings& settings);
    void Reset() { StartMatch(settings_); }
    // Main menu: shows `map` in the background, nothing is simulated.
    void EnterMenu(int map);
    const MatchSettings& Settings() const { return settings_; }
    bool Playing() const { return mem_.match.state == gm::MATCH_PLAYING; }
    bool MatchOver() const { return mem_.match.state == gm::MATCH_ENDED; }
    const MatchResult& Result() const { return result_; }
    const TrainingStats& Training() const { return training_; }
    float WaveBreak() const { return waveBreak_; }
    float MatchTime() const { return matchTime_; }

    // Kill / score limits of the current mode (0 = none).
    int  KillLimit() const;

    void SetBotCount(int count);  // 0 .. kMaxEntities-1
    int  BotCount() const { return botCount_; }

    void SetDifficulty(uint32_t d);
    uint32_t Difficulty() const { return mem_.difficulty; }

    // Advance the simulation by one fixed step (kTickDt).
    void Tick(const PlayerInput& input);

    // Recompute Entity::visible (line of sight from the local player).
    void UpdateVisibility();

    bool LineOfSight(Vec2f a, Vec2f b) const;

    // Events since the last call (sounds, shake). Clears the list.
    std::vector<GameEvent> TakeEvents();

    // --- debug / cheat toggles (keys F2..F4) --------------------------------
    bool godMode      = false;
    bool botsFrozen   = false;
    bool botsPeaceful = false;

    // --- data for the renderer ---------------------------------------------
    const std::vector<Bullet>&        Bullets() const { return bullets_; }
    const std::vector<Effect>&        Effects() const { return effects_; }
    const std::vector<FloatingText>&  Texts() const { return texts_; }
    const std::vector<KillFeedEntry>& KillFeed() const { return killFeed_; }
    const Announcement&               CurrentAnnouncement() const { return announcement_; }
    float PlayerHitMarker() const { return hitMarker_; }
    float PlayerDamageFlash() const { return damageFlash_; }
    float HitFlash(int slot) const { return hitFlash_[slot]; }
    float SpawnProtection() const { return spawnProtection_; }
    float PlayerFireCooldown() const { return playerFireCooldown_; }
    int   PlayerAmmo(int weapon) const { return playerAmmo_[weapon]; }
    int   KillStreak() const { return killStreak_; }
    float PickupRespawn(int i) const { return pickupTimers_[i]; }
    float DashCooldown() const { return dashCooldown_; }
    static constexpr float kDashCooldown = 1.2f;

    int shotsFired = 0;
    int shotsHit   = 0;

    gm::Entity&       Player() { return mem_.entities[mem_.localPlayerIndex]; }
    const gm::Entity& Player() const { return mem_.entities[mem_.localPlayerIndex]; }

private:
    void BuildArena(int map);
    void SpawnEntity(int slot);
    Vec2f FindSpawnPoint(uint32_t team);
    Vec2f FindTrainingSpawn();
    int  FindBotTarget(int slot, bool& canSee);
    void UpdateMatch(float dt);
    void StartWave(int wave);
    void EndMatch(const std::string& title, bool good);
    void OnKill(int victim, int attacker);

    void UpdatePlayer(const PlayerInput& input, float dt);
    void UpdateBot(int slot, float dt);
    void UpdateBullets(float dt);
    void UpdateEffects(float dt);
    void UpdateRespawns(float dt);
    void UpdatePickups(float dt);

    void FireBullets(int slot, float angle, float speed, int damage, int pellets, float spread,
                     float life, bool heavy);
    void ApplyDamage(int victim, int attacker, int damage, Vec2f hitPos, float angle);
    void Announce(const std::string& text);
    void Emit(GameEventType type, Vec2f pos, uint32_t weapon = 0) { events_.push_back({type, pos, weapon}); }

    // Moves a circle and resolves collisions with walls/arena bounds.
    // Returns the actual displacement.
    Vec2f MoveCircle(Vec2f& pos, Vec2f delta, float radius) const;
    bool  CircleHitsObstacle(Vec2f pos, float radius) const;
    int   NearestPickup(Vec2f from, float maxDist) const;

    float RandF(float lo, float hi);
    int   RandI(int lo, int hi);

    gm::GameMemory& mem_;
    std::mt19937    rng_;

    int      botCount_ = 8;
    uint32_t nextId_   = 1;

    // player weapon state
    float playerFireCooldown_ = 0.0f;
    int   playerAmmo_[gm::kWeaponCount] = {};
    float spawnProtection_ = 0.0f;

    // feedback
    float hitMarker_   = 0.0f;
    float damageFlash_ = 0.0f;
    float hitFlash_[gm::kMaxEntities] = {};
    int   killStreak_  = 0;
    int   multiKill_   = 0;
    float multiKillTimer_ = 0.0f;
    Announcement announcement_;

    float pickupTimers_[gm::kMaxPickups] = {};

    // match state
    MatchSettings settings_;
    MatchResult   result_;
    TrainingStats training_;
    float         matchTime_ = 0.0f;   // seconds played
    float         waveBreak_ = 0.0f;   // survival: countdown to the next wave
    int           playerDeathsAllowed_ = 0;

    // player dash
    float dashTimer_ = 0.0f;
    float dashCooldown_ = 0.0f;
    Vec2f dashDir_{0, 0};

    BotBrain                   brains_[gm::kMaxEntities];
    std::vector<Bullet>        bullets_;
    std::vector<Effect>        effects_;
    std::vector<FloatingText>  texts_;
    std::vector<KillFeedEntry> killFeed_;
    std::vector<GameEvent>     events_;
};
