// Menus: main menu, pause menu and the match result screen.
// Simple immediate-mode widgets: work with the mouse and with the keyboard
// (arrow keys / W S A D, Enter).
#pragma once

#include "game.h"
#include "raylib.h"

enum class MenuAction { None, Play, Quit, Resume, Restart, MainMenu };

struct MenuState {
    MatchSettings settings;
    bool fog      = true;
    int  selected = 0;   // keyboard focus in the current menu
    bool click    = false;  // set when a button/option was used (for the click sound)
};

// Main menu (draw it on top of the map preview). Changes `menu.settings`.
MenuAction DoMainMenu(MenuState& menu);

// Pause menu overlay: Resume / Restart / Main menu / Quit.
MenuAction DoPauseMenu(MenuState& menu);

// Result screen after a match: Play again / Main menu.
MenuAction DoResultScreen(const Game& game, MenuState& menu);

// Arrow shaped mouse pointer (the system cursor is hidden).
void DrawPointer();
