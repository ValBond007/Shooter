#include "audio.h"

#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <random>

#include "raylib.h"

namespace {

constexpr int   kSampleRate = 44100;
constexpr float kTwoPi = 6.2831853f;

// Builds a mono 16-bit sound from a function f(t) -> sample in [-1, 1].
Sound Synthesize(float seconds, const std::function<float(float)>& f) {
    int frames = static_cast<int>(seconds * kSampleRate);
    auto* data = static_cast<short*>(std::malloc(sizeof(short) * frames));
    for (int i = 0; i < frames; ++i) {
        float t = static_cast<float>(i) / kSampleRate;
        float s = f(t);
        // short fade out at the end to avoid clicks
        float tail = std::fmin(1.0f, (seconds - t) / 0.01f);
        s = std::fmax(-1.0f, std::fmin(1.0f, s * tail));
        data[i] = static_cast<short>(s * 32000.0f);
    }
    Wave w{};
    w.frameCount = static_cast<unsigned int>(frames);
    w.sampleRate = kSampleRate;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;
    Sound snd = LoadSoundFromWave(w);
    UnloadWave(w);  // frees `data`
    return snd;
}

// Noise source with a simple one-pole low-pass filter (smaller `cutoff` = duller).
struct Noise {
    std::mt19937 rng{1234};
    std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    float state = 0.0f;
    float Next(float cutoff) {
        state += cutoff * (dist(rng) - state);
        return state;
    }
};

float Env(float t, float rate) { return std::exp(-t * rate); }

}  // namespace

struct Audio::Voices {
    Sound source[SND_COUNT];
    Sound alias[SND_COUNT][kVoices];
    int   next[SND_COUNT] = {};
};

Audio::~Audio() {
    if (!ready_) return;
    for (int s = 0; s < SND_COUNT; ++s) {
        for (int v = 0; v < kVoices; ++v) UnloadSoundAlias(voices_->alias[s][v]);
        UnloadSound(voices_->source[s]);
    }
    delete voices_;
    CloseAudioDevice();
}

bool Audio::Init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;
    voices_ = new Voices();
    Sound* src = voices_->source;

    {
        Noise n;
        src[SND_RIFLE] = Synthesize(0.14f, [&](float t) {
            return 0.7f * n.Next(0.5f) * Env(t, 30) + 0.5f * std::sin(kTwoPi * 160 * t) * Env(t, 45);
        });
    }
    {
        Noise n;
        src[SND_SHOTGUN] = Synthesize(0.40f, [&](float t) {
            return 0.9f * n.Next(0.25f) * Env(t, 11) + 0.6f * std::sin(kTwoPi * 80 * t) * Env(t, 18);
        });
    }
    {
        Noise n1, n2;
        src[SND_SNIPER] = Synthesize(0.70f, [&](float t) {
            return 0.9f * n1.Next(0.9f) * Env(t, 70) + 0.7f * std::sin(kTwoPi * 55 * t) * Env(t, 7) +
                   0.35f * n2.Next(0.08f) * Env(t, 6);
        });
    }
    src[SND_HIT] = Synthesize(0.06f, [](float t) { return 0.5f * std::sin(kTwoPi * 1500 * t) * Env(t, 60); });
    src[SND_KILL] = Synthesize(0.28f, [](float t) {
        float f = t < 0.1f ? 880.0f : 1320.0f;
        return 0.4f * std::sin(kTwoPi * f * t) * Env(t < 0.1f ? t : t - 0.1f, 14);
    });
    {
        Noise n;
        src[SND_HURT] = Synthesize(0.18f, [&](float t) {
            float sq = std::sin(kTwoPi * 110 * t) > 0 ? 1.0f : -1.0f;
            return (0.35f * sq + 0.4f * n.Next(0.3f)) * Env(t, 20);
        });
    }
    {
        Noise n;
        auto click = [&](float t, float at) { return t >= at ? n.Next(0.7f) * Env(t - at, 90) : 0.0f; };
        src[SND_RELOAD] = Synthesize(0.30f, [&](float t) { return 0.6f * (click(t, 0.0f) + click(t, 0.2f)); });
        src[SND_RELOAD_DONE] = Synthesize(0.12f, [&](float t) {
            return 0.6f * click(t, 0.0f) + 0.3f * std::sin(kTwoPi * 600 * t) * Env(t, 50);
        });
        src[SND_EMPTY] = Synthesize(0.06f, [&](float t) { return 0.5f * std::sin(kTwoPi * 2200 * t) * Env(t, 80); });
        src[SND_SWITCH] = Synthesize(0.10f, [&](float t) { return 0.5f * (click(t, 0.0f) + click(t, 0.05f)); });
        src[SND_WALL] = Synthesize(0.05f, [&](float t) { return 0.25f * n.Next(0.9f) * Env(t, 100); });
    }
    src[SND_PICKUP] = Synthesize(0.24f, [](float t) {
        float f = t < 0.08f ? 523.0f : (t < 0.16f ? 659.0f : 784.0f);
        return 0.35f * std::sin(kTwoPi * f * t);
    });
    src[SND_DEATH] = Synthesize(0.7f, [](float t) {
        float f = 400.0f - 420.0f * t;  // falling tone
        return 0.4f * std::sin(kTwoPi * f * t) * Env(t, 3);
    });
    src[SND_RESPAWN] = Synthesize(0.3f, [](float t) {
        float f = 300.0f + 700.0f * t;  // rising tone
        return 0.3f * std::sin(kTwoPi * f * t) * Env(t, 6);
    });

    {
        Noise n;
        src[SND_DASH] = Synthesize(0.22f, [&](float t) {
            float env = std::sin(3.14159f * t / 0.22f);  // swell up and down
            return 0.5f * n.Next(0.05f + 0.4f * t) * env;
        });
    }
    src[SND_WAVE] = Synthesize(0.9f, [](float t) {
        float f = t < 0.45f ? 196.0f : 262.0f;
        float saw = 2.0f * (f * t - std::floor(f * t + 0.5f));
        return 0.3f * saw * Env(t < 0.45f ? t : t - 0.45f, 4);
    });
    src[SND_MATCH_END] = Synthesize(1.0f, [](float t) {
        const float notes[4] = {523.0f, 659.0f, 784.0f, 1047.0f};
        int i = std::min(3, static_cast<int>(t / 0.15f));
        float local = t - i * 0.15f;
        return 0.35f * std::sin(kTwoPi * notes[i] * t) * Env(i == 3 ? local : local * 3.0f, 3);
    });
    src[SND_CLICK] = Synthesize(0.04f, [](float t) { return 0.4f * std::sin(kTwoPi * 900 * t) * Env(t, 90); });

    for (int s = 0; s < SND_COUNT; ++s)
        for (int v = 0; v < kVoices; ++v) voices_->alias[s][v] = LoadSoundAlias(src[s]);
    ready_ = true;
    return true;
}

void Audio::Play(SoundId id, float volume, float pitch) {
    if (!ready_ || muted || volume <= 0.01f) return;
    int& next = voices_->next[id];
    Sound& s = voices_->alias[id][next];
    next = (next + 1) % kVoices;
    SetSoundVolume(s, volume);
    SetSoundPitch(s, pitch);
    PlaySound(s);
}

void Audio::PlayEvents(const std::vector<GameEvent>& events, Vec2f listener) {
    if (!ready_ || muted) return;
    for (const GameEvent& e : events) {
        // Sounds far away from the player are quieter.
        float dist = Distance(e.pos, listener);
        float att = Clampf(1.0f - dist / 1500.0f, 0.0f, 1.0f);
        float jitter = 0.95f + 0.1f * static_cast<float>(std::rand() % 100) / 100.0f;

        auto weaponSound = [](uint32_t w) {
            return w == gm::WEAPON_SHOTGUN ? SND_SHOTGUN : (w == gm::WEAPON_SNIPER ? SND_SNIPER : SND_RIFLE);
        };
        switch (e.type) {
            case GameEventType::PlayerShot:     Play(weaponSound(e.weapon), 0.55f, jitter); break;
            case GameEventType::Shot:           Play(weaponSound(e.weapon), 0.35f * att, 0.8f * jitter); break;
            case GameEventType::PlayerHitEnemy: Play(SND_HIT, 0.5f); break;
            case GameEventType::PlayerKill:     Play(SND_KILL, 0.6f); break;
            case GameEventType::Kill:           break;
            case GameEventType::PlayerHurt:     Play(SND_HURT, 0.5f, jitter); break;
            case GameEventType::PlayerDied:     Play(SND_DEATH, 0.7f); break;
            case GameEventType::Reload:         Play(SND_RELOAD, 0.5f); break;
            case GameEventType::ReloadDone:     Play(SND_RELOAD_DONE, 0.5f); break;
            case GameEventType::Empty:          Play(SND_EMPTY, 0.5f); break;
            case GameEventType::WeaponSwitch:   Play(SND_SWITCH, 0.5f); break;
            case GameEventType::Pickup:         Play(SND_PICKUP, 0.6f); break;
            case GameEventType::WallHit:        Play(SND_WALL, 0.3f * att, jitter); break;
            case GameEventType::Respawn:        Play(SND_RESPAWN, 0.4f); break;
            case GameEventType::Hit:            break;
            case GameEventType::Dash:           Play(SND_DASH, 0.5f, jitter); break;
            case GameEventType::WaveStart:      Play(SND_WAVE, 0.6f); break;
            case GameEventType::MatchEnd:       Play(SND_MATCH_END, 0.6f); break;
        }
    }
}
