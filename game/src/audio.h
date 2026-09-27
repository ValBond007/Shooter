// Procedurally generated sound effects (no audio files needed).
#pragma once

#include <vector>

#include "game.h"

class Audio {
public:
    ~Audio();

    // Opens the audio device and synthesizes all sounds. Returns false if
    // there is no audio device (the game then just runs silently).
    bool Init();

    // Plays the sounds for this frame's game events. `listener` = the player
    // position, used to make far away sounds quieter.
    void PlayEvents(const std::vector<GameEvent>& events, Vec2f listener);

    bool muted = false;

private:
    enum SoundId {
        SND_RIFLE, SND_SHOTGUN, SND_SNIPER, SND_HIT, SND_KILL, SND_HURT, SND_RELOAD,
        SND_RELOAD_DONE, SND_EMPTY, SND_SWITCH, SND_PICKUP, SND_DEATH, SND_RESPAWN,
        SND_WALL, SND_COUNT
    };
    void Play(SoundId id, float volume, float pitch = 1.0f);

    static constexpr int kVoices = 8;  // how many copies of a sound can overlap
    struct Voices;
    Voices* voices_ = nullptr;
    bool    ready_ = false;
};
