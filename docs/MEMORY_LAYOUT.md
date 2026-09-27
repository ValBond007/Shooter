# Memory layout (layout version 1)

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

## `GameMemory` (size 0xE80)

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x000 | char[16] | `magic` | `"ARENA_SHOOTER!!"` |
| 0x010 | u32 | `layoutVersion` | 1 |
| 0x014 | u32 | `structSize` | 0xE80 |
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
| 0x064 | u32 | `paused` | |
| 0x068 | vec2 | `arenaSize` | 2400 × 1600 |
| 0x070 | f32 | `bulletSpeed` | 1300 (player bullets) |
| 0x074 | u32 | `obstacleCount` | |
| 0x078 | u8[8] | reserved | |
| 0x080 | Entity[32] | `entities` | stride 0x60 |
| 0xC80 | Obstacle[32] | `obstacles` | stride 0x10 |

## `Entity` (size 0x60)

`entities[i]` is at `g_game + 0x80 + i * 0x60`. Slot 0 is the local player.

| Offset | Type | Name | Notes |
|---:|---|---|---|
| 0x00 | u32 | `id` | new id every spawn |
| 0x04 | u32 | `active` | slot used |
| 0x08 | u32 | `alive` | |
| 0x0C | u32 | `team` | 0 = player, 1 = bots |
| 0x10 | i32 | `health` | |
| 0x14 | i32 | `maxHealth` | 100 |
| 0x18 | vec2 | `pos` | world position (center) |
| 0x20 | vec2 | `vel` | units / second |
| 0x28 | f32 | `aimAngle` | radians, 0 = right, π/2 = down |
| 0x2C | f32 | `radius` | 18 |
| 0x30 | u32 | `visible` | line of sight from the local player |
| 0x34 | i32 | `kills` | |
| 0x38 | i32 | `deaths` | |
| 0x3C | f32 | `respawnTimer` | |
| 0x40 | char[16] | `name` | |
| 0x50 | u8[16] | reserved | |

## `Obstacle` (size 0x10)

| Offset | Type | Name |
|---:|---|---|
| 0x00 | f32 | `x` (top-left) |
| 0x04 | f32 | `y` |
| 0x08 | f32 | `w` |
| 0x0C | f32 | `h` |

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
