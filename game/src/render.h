#pragma once

#include "game.h"
#include "raylib.h"

struct UiState {
    bool debugOverlay = false;  // F1
    bool showHelp     = false;  // H
    bool paused       = false;  // Esc / P
    float helpHintTimer = 8.0f; // "press H for help" hint at startup
};

// Draws one complete frame (call between BeginDrawing / EndDrawing).
void DrawFrame(const Game& game, const gm::GameMemory& mem, const Camera2D& camera,
               const UiState& ui);
