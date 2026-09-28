#include "ui.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "maps.h"

namespace {

const Color kPanel     = {10, 12, 16, 225};
const Color kBorder    = {90, 98, 120, 255};
const Color kText      = {230, 232, 240, 255};
const Color kTextDim   = {150, 155, 170, 255};
const Color kAccent    = {120, 220, 160, 255};
const Color kFocusFill = {40, 70, 60, 255};

void TextCentered(const char* text, int cx, int y, int size, Color c) {
    int w = MeasureText(text, size);
    DrawText(text, cx - w / 2 + 2, y + 2, size, Color{0, 0, 0, 170});
    DrawText(text, cx - w / 2, y, size, c);
}

// Keyboard navigation shared by all menus. Returns true if Enter was pressed.
bool Navigate(MenuState& menu, int count) {
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) { menu.selected = (menu.selected + 1) % count; menu.click = true; }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) { menu.selected = (menu.selected + count - 1) % count; menu.click = true; }
    menu.selected = std::clamp(menu.selected, 0, count - 1);
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
}

// A button. Returns true when activated (mouse click or Enter while focused).
bool Button(MenuState& menu, int index, Rectangle r, const char* label, bool enterPressed) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (hover && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) menu.selected = index;
    bool focused = menu.selected == index;
    DrawRectangleRec(r, focused ? kFocusFill : Color{25, 28, 36, 235});
    DrawRectangleLinesEx(r, 2, focused ? kAccent : kBorder);
    TextCentered(label, static_cast<int>(r.x + r.width / 2), static_cast<int>(r.y + r.height / 2 - 12), 24,
                 focused ? kText : kTextDim);
    bool activated = (hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) || (focused && enterPressed);
    if (activated) menu.click = true;
    return activated;
}

// "Label   < value >" row. Returns -1 / +1 when the value should change.
int OptionRow(MenuState& menu, int index, Rectangle r, const char* label, const char* value) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    if (hover && (GetMouseDelta().x != 0 || GetMouseDelta().y != 0)) menu.selected = index;
    bool focused = menu.selected == index;
    DrawRectangleRec(r, focused ? kFocusFill : Color{25, 28, 36, 200});
    if (focused) DrawRectangleLinesEx(r, 2, kAccent);
    DrawText(label, static_cast<int>(r.x + 16), static_cast<int>(r.y + r.height / 2 - 10), 20, focused ? kText : kTextDim);

    // arrows + value on the right half
    Rectangle left{r.x + r.width * 0.45f, r.y, 36, r.height};
    Rectangle right{r.x + r.width - 44, r.y, 36, r.height};
    float valueCenter = (left.x + left.width + right.x) / 2;
    Color arrow = focused ? kAccent : kTextDim;
    TextCentered("<", static_cast<int>(left.x + left.width / 2), static_cast<int>(r.y + r.height / 2 - 12), 24, arrow);
    TextCentered(">", static_cast<int>(right.x + right.width / 2), static_cast<int>(r.y + r.height / 2 - 12), 24, arrow);
    TextCentered(value, static_cast<int>(valueCenter), static_cast<int>(r.y + r.height / 2 - 10), 20, kText);

    int change = 0;
    Vector2 m = GetMousePosition();
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(m, left)) change = -1;
        else if (CheckCollisionPointRec(m, right) || hover) change = +1;
    }
    if (focused) {
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) change = -1;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) change = +1;
    }
    if (change != 0) menu.click = true;
    return change;
}

int Wrap(int v, int n) { return ((v % n) + n) % n; }

}  // namespace

void DrawPointer() {
    Vector2 m = GetMousePosition();
    Vector2 a = m, b{m.x, m.y + 20}, c{m.x + 14, m.y + 14};
    DrawTriangle(a, b, c, Color{0, 0, 0, 200});
    DrawTriangleLines(a, b, c, WHITE);
    DrawTriangle(Vector2{a.x + 2, a.y + 5}, Vector2{b.x + 2, b.y - 5}, Vector2{c.x - 5, c.y - 2}, kText);
}

MenuAction DoMainMenu(MenuState& menu) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    MatchSettings& s = menu.settings;
    menu.click = false;

    DrawRectangle(0, 0, sw, sh, Color{8, 10, 14, 150});
    TextCentered("ARENA SHOOTER", sw / 2, std::max(20, sh / 2 - 300), 64, kText);
    TextCentered("a target for memory reading / DMA experiments", sw / 2, std::max(90, sh / 2 - 228), 20, kTextDim);

    const int w = 520, rowH = 44, gap = 8;
    const int x = sw / 2 - w / 2;
    int y = std::max(130, sh / 2 - 180);
    const int kItems = 7;  // 5 options + 2 buttons
    bool enter = Navigate(menu, kItems);

    char value[64];
    int d = OptionRow(menu, 0, Rectangle{(float)x, (float)y, (float)w, (float)rowH}, "Mode", ModeName(s.mode));
    if (d) s.mode = static_cast<uint32_t>(Wrap(static_cast<int>(s.mode) + d, 4));
    y += rowH + gap;

    d = OptionRow(menu, 1, Rectangle{(float)x, (float)y, (float)w, (float)rowH}, "Map", GetMap(s.map).name);
    if (d) s.map = Wrap(s.map + d, MapCount());
    y += rowH + gap;

    bool botsUsed = s.mode == gm::MODE_DEATHMATCH || s.mode == gm::MODE_TEAM_DEATHMATCH;
    if (botsUsed) std::snprintf(value, sizeof(value), "%d", s.bots);
    else std::snprintf(value, sizeof(value), "%s", s.mode == gm::MODE_SURVIVAL ? "waves" : "1 target");
    d = OptionRow(menu, 2, Rectangle{(float)x, (float)y, (float)w, (float)rowH}, "Bots", value);
    if (d && botsUsed) s.bots = std::clamp(s.bots + d, 1, gm::kMaxEntities - 1);
    y += rowH + gap;

    d = OptionRow(menu, 3, Rectangle{(float)x, (float)y, (float)w, (float)rowH}, "Difficulty", DifficultyName(s.difficulty));
    if (d) s.difficulty = static_cast<uint32_t>(Wrap(static_cast<int>(s.difficulty) + d, 3));
    y += rowH + gap;

    d = OptionRow(menu, 4, Rectangle{(float)x, (float)y, (float)w, (float)rowH}, "Fog of war", menu.fog ? "On" : "Off");
    if (d) menu.fog = !menu.fog;
    y += rowH + gap;

    TextCentered(ModeDescription(s.mode), sw / 2, y + 4, 20, kAccent);
    y += 40;

    MenuAction action = MenuAction::None;
    if (Button(menu, 5, Rectangle{(float)x, (float)y, (float)w, 56}, "PLAY", enter)) action = MenuAction::Play;
    y += 56 + gap;
    if (Button(menu, 6, Rectangle{(float)x, (float)y, (float)w, 44}, "QUIT", enter)) action = MenuAction::Quit;
    y += 60;

    TextCentered("mouse or arrow keys + Enter      results are saved to shooter_results.csv", sw / 2,
                 std::min(y, sh - 30), 10, kTextDim);
    return action;
}

MenuAction DoPauseMenu(MenuState& menu) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    menu.click = false;
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 140});
    const int w = 360, h = 56, gap = 10;
    const int x = sw / 2 - w / 2;
    int y = sh / 2 - 150;
    TextCentered("PAUSED", sw / 2, y - 70, 50, kText);

    bool enter = Navigate(menu, 4);
    MenuAction action = MenuAction::None;
    if (Button(menu, 0, Rectangle{(float)x, (float)y, (float)w, (float)h}, "RESUME", enter)) action = MenuAction::Resume;
    y += h + gap;
    if (Button(menu, 1, Rectangle{(float)x, (float)y, (float)w, (float)h}, "RESTART", enter)) action = MenuAction::Restart;
    y += h + gap;
    if (Button(menu, 2, Rectangle{(float)x, (float)y, (float)w, (float)h}, "MAIN MENU", enter)) action = MenuAction::MainMenu;
    y += h + gap;
    if (Button(menu, 3, Rectangle{(float)x, (float)y, (float)w, (float)h}, "QUIT", enter)) action = MenuAction::Quit;
    return action;
}

MenuAction DoResultScreen(const Game& game, MenuState& menu) {
    const int sw = GetScreenWidth(), sh = GetScreenHeight();
    const MatchResult& r = game.Result();
    menu.click = false;
    DrawRectangle(0, 0, sw, sh, Color{0, 0, 0, 160});

    const int w = 720;
    const int x = sw / 2 - w / 2;
    const int h = 230 + static_cast<int>(r.lines.size()) * 28;
    int y = std::max(10, sh / 2 - h / 2);
    DrawRectangle(x, y, w, h, kPanel);
    DrawRectangleLines(x, y, w, h, kBorder);

    Color titleColor = r.good ? Color{255, 210, 80, 255} : Color{255, 100, 100, 255};
    TextCentered(r.title.c_str(), sw / 2, y + 24, 60, titleColor);
    y += 110;
    for (size_t i = 0; i < r.lines.size(); ++i) {
        // "label\tvalue" -> two columns
        const std::string& l = r.lines[i];
        size_t tab = l.find('\t');
        if (tab == std::string::npos) {
            DrawText(l.c_str(), x + 50, y, 20, i == 0 ? kAccent : kText);
        } else {
            DrawText(l.substr(0, tab).c_str(), x + 50, y, 20, kTextDim);
            DrawText(l.substr(tab + 1).c_str(), x + 280, y, 20, kText);
        }
        y += 28;
    }
    y += 10;
    TextCentered("saved to shooter_results.csv", sw / 2, y, 10, kTextDim);
    y += 30;

    bool enter = Navigate(menu, 2);
    MenuAction action = MenuAction::None;
    const int bw = 250;
    if (Button(menu, 0, Rectangle{(float)(sw / 2 - bw - 10), (float)y, (float)bw, 56}, "PLAY AGAIN", enter))
        action = MenuAction::Restart;
    if (Button(menu, 1, Rectangle{(float)(sw / 2 + 10), (float)y, (float)bw, 56}, "MAIN MENU", enter))
        action = MenuAction::MainMenu;
    return action;
}
