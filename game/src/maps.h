// Arena layouts. All maps are 2400 x 1600 world units.
#pragma once

#include <vector>

#include "game_memory.h"

struct MapDef {
    const char*               name;
    std::vector<gm::Obstacle> walls;
    std::vector<gm::Vec2f>    healthPacks;
};

int           MapCount();
const MapDef& GetMap(int index);
