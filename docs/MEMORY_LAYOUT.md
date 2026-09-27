# Memory layout (layout version 4)

Source of truth: [`shared/game_memory.h`](../shared/game_memory.h). Every offset
below is checked there with `static_assert`, so these tables can't silently
go out of date.

All values are little-endian. `u32`/`i32` = 4-byte unsigned/signed integer,
`f32` = 4-byte float, `u64` = 8-byte integer, `vec2` = two `f32` (x, y).

## Finding `g_game`

```
g_game = moduleBase("shooter.exe") + RVA
```

- The **RVA** is printed at game start (console + `shooter_offsets.txt`) and in the
  F1 overlay. It is fixed for a given build of `shooter.exe`.
- Or **scan** the module (`moduleBase` … `moduleBase + SizeOfImage`) for the 16 bytes
  `41 52 45 4E 41 5F 53 48 4F 4F 54 45 52 21 21 00` (`"ARENA_SHOOTER!!\0"`)
  and accept a hit only if `*(u64*)(hit + 0x18) == hit`.

`SizeOfImage` comes from the PE header: `e_lfanew = *(i32*)(base + 0x3C)`, then
`SizeOfImage = *(u32*)(base + e_lfanew + 0x50)`.

## `GameMemory` (size 0x14A0)

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x000 | char[16] | `magic` | `"ARENA_SHOOTER!!"` |
| 0x010 | u32 | `layoutVersion` | 4 |
| 0x014 | u32 | `structSize` | 0x14A0 |
| 0x018 | u64 | `selfAddress` | address of `g_game` itself |
| 0x020 | u32 | `frameCount` | +1 per rendered frame |
| 0x024 | f32 | `gameTime` | seconds |
| 0x028 | u32 | `localPlayerIndex` | always 0 |
| 0x02C | u32 | `entityCount` | used slots (1 + bots) |
| 0x030 | vec2 | `camTarget` | world point drawn at `camOffset` |
| 0x038 | vec2 | `camOffset` | screen center |
| 0x040 | f32 | `camZoom` | 0.5 … 1.6 (mouse wheel) |
| 0x044 | f32 | `camRotation` | always 0 |
| 0x048 | i32 | `screenWidth` | window client size |
| 0x04C | i32 | `screenHeight` | |
| 0x050 | vec2 | `mouseScreen` | cursor / crosshair in window pixels |
| 0x058 | vec2 | `mouseWorld` | cursor in world units |
| 0x060 | u32 | `buttons` | bit0 left, bit1 right, bit2 middle |
| 0x064 | u32 | `paused` | 1 while paused, in a menu or on the result screen |
| 0x068 | vec2 | `arenaSize` | 2400 × 1600 |
| 0x070 | f32 | `bulletSpeed` | bullet speed of the player's current weapon (1300 / 1000 / 2600) |
| 0x074 | u32 | `obstacleCount` | |
| 0x078 | u32 | `difficulty` | 0 easy, 1 normal, 2 hard |
| 0x07C | u32 | `fogOfWar` | 1 = enemies without line of sight are not drawn |
| 0x080 | Entity[32] | `entities` | stride 0x60 |
| 0xC80 | Obstacle[32] | `obstacles` | stride 0x10 |
| 0xE80 | Pickup[16] | `pickups` | stride 0x10 |
| 0xF80 | MatchInfo | `match` | see below |
| 0xFA0 | Grenade[16] | `grenades` | stride 0x20 |
| 0x11A0 | Barrel[8] | `barrels` | stride 0x10 |
| 0x1220 | u8[0x80] | reserved | |
| 0x12A0 | EntityBuffs[32] | `buffs` | stride 0x10, same index as `entities` |

## `Entity` (size 0x60)

`entities[i]` is at `g_game + 0x80 + i * 0x60`. Slot 0 is the local player.

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | u32 | `id` | new id every spawn |
| 0x04 | u32 | `active` | slot used |
| 0x08 | u32 | `alive` | |
| 0x0C | u32 | `team` | 0 = your team (you + allies in Team Deathmatch), 1 = enemies |
| 0x10 | i32 | `health` | |
| 0x14 | i32 | `maxHealth` | player/soldier 100, runner 60, heavy 220 |
| 0x18 | vec2 | `pos` | world position (center) |
| 0x20 | vec2 | `vel` | units / second |
| 0x28 | f32 | `aimAngle` | radians, 0 = right, π/2 = down |
| 0x2C | f32 | `radius` | player/soldier 18, runner 14, heavy 25 |
| 0x30 | u32 | `visible` | line of sight from the local player |
| 0x34 | i32 | `kills` | |
| 0x38 | i32 | `deaths` | |
| 0x3C | f32 | `respawnTimer` | |
| 0x40 | char[16] | `name` | |
| 0x50 | u32 | `weapon` | 0 rifle, 1 shotgun, 2 sniper |
| 0x54 | i32 | `ammo` | rounds left in the magazine |
| 0x58 | f32 | `reloadTimer` | > 0 while reloading (seconds left) |
| 0x5C | u32 | `kind` | 0 player, 1 soldier, 2 runner, 3 heavy |

## `Obstacle` (size 0x10)

| Offset | Type | Name |
|---:|---|---|
| 0x00 | f32 | `x` (top-left) |
| 0x04 | f32 | `y` |
| 0x08 | f32 | `w` |
| 0x0C | f32 | `h` |

## `Pickup` (size 0x10)

`pickups[i]` is at `g_game + 0xE80 + i * 0x10`. Unused slots are all zero.

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | vec2 | `pos` | world position |
| 0x08 | u32 | `type` | 0 health, 1 speed, 2 double damage, 3 shield, 4 grenade |
| 0x0C | u32 | `available` | 0 while respawning |

## `MatchInfo` (size 0x20, at `g_game + 0xF80`)

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | u32 | `mode` | 0 deathmatch, 1 team deathmatch, 2 survival, 3 aim training |
| 0x04 | u32 | `state` | 0 main menu, 1 playing, 2 result screen |
| 0x08 | f32 | `timeLeft` | seconds, 0 = no time limit |
| 0x0C | u32 | `map` | 0 Arena, 1 Warehouse, 2 Corridors |
| 0x10 | i32[2] | `teamScore` | kills of team 0 / team 1 |
| 0x18 | u32 | `wave` | survival wave |
| 0x1C | i32 | `livesLeft` | survival lives |

## `Grenade` (size 0x20)

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | vec2 | `pos` | |
| 0x08 | vec2 | `vel` | slows down over time |
| 0x10 | f32 | `fuse` | seconds until it explodes (blast radius 150) |
| 0x14 | u32 | `active` | |
| 0x18 | u32 | `team` | team of the thrower |
| 0x1C | i32 | `owner` | entity index of the thrower |

## `Barrel` (size 0x10)

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | vec2 | `pos` | center, radius 20 |
| 0x08 | i32 | `health` | 30 when full |
| 0x0C | u32 | `alive` | 0 after exploding (back after 25 s) |

## `EntityBuffs` (size 0x10)

`buffs[i]` belongs to `entities[i]`.

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | f32 | `speedTime` | seconds of speed boost left |
| 0x04 | f32 | `damageTime` | seconds of double damage left |
| 0x08 | i32 | `shield` | 0..50, absorbs damage before health |
| 0x0C | i32 | `grenades` | grenades carried |

## World → screen

```
screen.x = (world.x - camTarget.x) * camZoom + camOffset.x
screen.y = (world.y - camTarget.y) * camZoom + camOffset.y
```

## Example: reading one enemy's health by hand

```
g_game        = 0x7FF6A2B4C140
entities[3]   = g_game + 0x80 + 3 * 0x60 = 0x7FF6A2B4C2E0
health        = *(i32*)(entities[3] + 0x10)   -> 0x7FF6A2B4C2F0
```
