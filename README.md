# Arena Shooter + DMA Aimbot

A small top-down 2D arena shooter (C++ / [raylib](https://www.raylib.com/)) built
as a **target for a memory-reading project**, plus an **external aimbot** that
reads the game's RAM (via DMA with MemProcFS, or locally for testing) and moves
the mouse with a KMBox.

```
 GAME PC                                     SECOND PC (aimbot PC)
┌──────────────────────────┐   PCIe DMA    ┌──────────────────────────────┐
│ shooter.exe              │◄──────────────│ aimbot.exe                   │
│   g_game (GameMemory)    │  (FPGA card)  │  MemProcFS reads g_game      │
│                          │               │  -> picks target             │
│ mouse input ◄── KMBox ◄──┼───────────────│  -> km.move(dx, dy)          │
└──────────────────────────┘  USB / LAN    └──────────────────────────────┘
```

| Folder | What |
|---|---|
| `game/` | the game (`shooter.exe`) |
| `aimbot/` | the external reader + aimbot (`aimbot.exe`) |
| `shared/game_memory.h` | **the memory layout**, used by both |
| `docs/MEMORY_LAYOUT.md` | offset tables |
| `docs/HOW_IT_WORKS.md` | the ideas behind it (for your project write-up) |

---

## 1. Building (Windows)

You need **Visual Studio 2022** (workload "Desktop development with C++"),
**CMake** (VS ships one) and **Git** (CMake downloads raylib automatically).

**Option A:** double-click `build.bat` (or run it in a terminal). Executables end up in
`build\bin\Release\`.

**Option B:** open the folder in Visual Studio (*File → Open → Folder*), choose
`x64-Release`, then *Build → Build All*.

**Option C (command line):**
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Only need one of the two programs? Pass `-DBUILD_AIMBOT=OFF` or `-DBUILD_GAME=OFF`.

> On Linux, `cmake -S . -B build && cmake --build build` builds both (you need the
> X11/GL dev packages for raylib). A MinGW cross-compile toolchain file is in `cmake/`.

---

## 2. The game

Run `shooter.exe`. A console window opens next to the game and prints the
memory layout (addresses and offsets). The same text goes to `shooter_offsets.txt`.

The game starts in the **main menu**, where you pick the mode, map, number of bots,
difficulty and fog of war (mouse or arrow keys + Enter).

| Mode | Rules |
|---|---|
| **Deathmatch** | everyone against you, first to 25 kills or 5 minutes |
| **Team Deathmatch** | you + allied bots (green) against enemy bots (red), first team to 50 kills |
| **Survival** | waves of enemies that get bigger and tougher, enemies don't respawn, 3 lives |
| **Aim Training** | 60 s, one moving target at a time, you can't die. Measures accuracy, time to kill and time to first hit |

Maps: **Arena** (mixed cover), **Warehouse** (shelves and crates), **Corridors** (long lanes).

The game remembers your last menu choices and the mute setting in `shooter_settings.ini`.
Every finished match adds one line to **`shooter_results.csv`** (see
[measuring the aimbot](#measuring-the-aimbot)).

| Key | Action |
|---|---|
| WASD / arrows | move |
| mouse | aim (the crosshair is the Windows cursor) |
| left mouse | shoot |
| `Shift` / `Space` | dash (short burst of speed, 1.2 s cooldown) |
| `G` | throw a grenade at the cursor |
| `1` / `2` / `3` | rifle / shotgun / sniper |
| `Q` | previous weapon |
| `R` | reload |
| mouse wheel | zoom |
| `Tab` (hold) | scoreboard |
| `+` / `-` | more / fewer bots (deathmatch modes) |
| `Esc` / `P` | pause menu (resume, restart, main menu, quit) |
| `M` | sound on / off |
| `H` | help |
| **F1** | **memory debug overlay**: module base, `g_game` address, RVA, live entity table with addresses |
| F2 | god mode |
| F3 | freeze bots (static targets, good for testing) |
| F4 | peaceful bots (they don't shoot) |
| F5 | restart match |
| F6 | difficulty: easy / normal / hard |
| F7 | fog of war on / off |
| F11 | fullscreen (borderless) |

Command line:
```
shooter.exe --mode dm|tdm|survival|training --map 0|1|2 --bots 12
            --difficulty easy|normal|hard --no-fog --skip-menu
            --seed 123 --width 1600 --height 900
```
`--skip-menu` starts the match right away (handy for testing).
`--print-offsets` prints the layout and exits.

### Features

- **3 weapons**, each with its own magazine and reload time:

  | Weapon | Damage | Fire rate | Bullet speed | Magazine |
  |---|---|---|---|---|
  | Rifle | 25 | 9 / s | 1300 | 30 |
  | Shotgun | 14 per pellet, 7 pellets | 1.25 / s | 1000 (short range) | 6 |
  | Sniper | 100 | 0.9 / s | 2600 | 5 |

- **3 bot types**: *Soldier* (normal), *Runner* (fast, small, 60 HP) and *Heavy*
  (slow, big, 220 HP, shotgun). Bots strafe, change direction often, dash when
  you aim at them, back off while reloading and go for health packs when hurt.
- **Health packs** (+40 HP, back after 20 s).
- **Power-ups** (rotating diamonds, random type, back after 30 s): *speed* (+40 % for 8 s),
  *double damage* (8 s), *shield* (absorbs 50 damage), *grenade* (+1).
- **Grenades**: you start with 2 (max 3). They bounce off walls and explode after 1.5 s;
  a red circle on the ground shows the blast radius. Walls block the blast. Soldiers and
  Heavies throw grenades at you when you hide behind cover.
- **Explosive barrels**: shoot them (30 HP) or blow them up with a grenade. They chain-react
  and come back after 25 s.
- Red arcs around you show where damage comes from; the death screen tells you who killed
  you and with what.
- **Fog of war**: walls cast shadows and enemies you can't see aren't drawn.
  They are still in memory, so `aimbot radar` / `aimbot dump` show them. This is a
  nice demo of what reading memory gives you.
- **Allied bots** in Team Deathmatch: they pick their own targets and fight enemy bots
  (the `team` field in memory tells friend from foe).
- Kill streaks (*DOUBLE KILL*, *KILLING SPREE*, …), scoreboard, 3 difficulty levels,
  spawn protection, screen shake, hit flashes, corpses, generated sound effects
  (no audio files needed).

Game facts that matter for the aimbot:
- bullets are not instant, so moving targets need **prediction**. The bullet speed of
  the weapon you are holding is in memory (`bulletSpeed`), so the aimbot's prediction
  is correct for every weapon.
- bots move a lot, and Runners are fast
- walls block bullets and line of sight (`Entity::visible`)

---

## 3. Where is the data? (short version)

Everything is in **one global struct** in the game's `.data` section:

```cpp
gm::GameMemory g_game;   // shared/game_memory.h, size 0x14A0
```

```
g_game + 0x000  "ARENA_SHOOTER!!"   magic (16 bytes)
g_game + 0x018  selfAddress         = &g_game (for verification)
g_game + 0x020  frameCount
g_game + 0x030  camTarget, camOffset, camZoom      (world -> screen)
g_game + 0x050  mouseScreen, mouseWorld, buttons   (crosshair + mouse buttons)
g_game + 0x080  entities[32]   (0x60 bytes each, [0] = you)
                  +0x08 alive  +0x0C team  +0x10 health  +0x18 pos  +0x20 vel  +0x30 visible
                  +0x50 weapon  +0x54 ammo  +0x58 reloadTimer  +0x5C kind
g_game + 0xC80  obstacles[32]  (walls)
g_game + 0xE80  pickups[16]    (health packs)
g_game + 0xF80  match          (mode, state, time left, team scores, wave, lives)
g_game + 0xFA0  grenades[16]   (pos, vel, fuse, team, owner)
g_game + 0x11A0 barrels[8]     (pos, health, alive)
g_game + 0x12A0 buffs[32]      (per entity: speed / damage time, shield, grenades)
```

Two ways to find it:
1. **RVA**: `address = moduleBase("shooter.exe") + RVA`. The RVA is shown in the F1
   overlay and in `shooter_offsets.txt`. It only changes when you rebuild the game.
2. **Signature scan**: search the module for the magic string, then check that
   `selfAddress` matches. The aimbot does this by default (`game_rva = 0`), so it
   keeps working after a rebuild.

Full tables: [docs/MEMORY_LAYOUT.md](docs/MEMORY_LAYOUT.md).

---

## 4. The aimbot

`aimbot.exe` reads `config.ini` from the current folder (a copy is placed next to
the exe when building). Modes:

| Command | What it does |
|---|---|
| `aimbot info` | find the game, print module base / `g_game` / RVA, exit |
| `aimbot dump` | **live table of everything read from memory** (the "DMA reading" part) |
| `aimbot radar` | ASCII radar of the arena, built only from memory |
| `aimbot bench` | measure read latency / throughput (nice numbers for your report) |
| `aimbot calibrate` | measure mouse counts per pixel -> `mouse_scale` |
| `aimbot` or `aimbot aim` | run the aimbot |

Overrides: `--config other.ini`, `--memory winapi`, `--mouse none`, `--seconds 30`.

### How the aimbot aims

Each new game frame:
1. Checks whether you hold the aim key. The key is read from **the game's memory**
   (`buttons`), so the aimbot PC doesn't need your mouse. Default: right mouse button.
2. Takes every enemy that is alive, visible and on screen.
3. Predicts where it will be when the bullet arrives: `pos + vel * distance / bulletSpeed`.
4. Converts that to screen pixels with the camera from memory.
5. Picks the one closest to the crosshair, inside `fov` pixels.
6. Moves the mouse by `error / smooth` pixels × `mouse_scale` through the KMBox.

The code is in `aimbot/src/aimbot.cpp` (about 100 lines).

### Step 1: test on ONE PC (no hardware)

Game and aimbot on the same Windows PC:

```ini
memory = winapi
mouse  = sendinput
```
Start `shooter.exe`, then `aimbot.exe`. In the game, hold the **right mouse button**
near an enemy. `aimbot dump` also works like this.

### Step 2: DMA (the real setup)

1. Set up the DMA card as usual and check that **MemProcFS** works on its own
   (`MemProcFS.exe -device fpga` shows the game PC's processes).
2. Copy **`vmm.dll`, `leechcore.dll`, `FTD3XX.dll`** (from the MemProcFS release /
   FTDI) next to `aimbot.exe`. `info.db`, `dbghelp.dll` and `symsrv.dll` are optional.
3. In `config.ini`, set `memory = dma` and `dma_args = -device fpga`
   (add `-memmap auto` if reads fail).
4. Start the game on the game PC, then run `aimbot info` and `aimbot dump` on the
   aimbot PC. If you see the entities moving, the DMA reading works.

The aimbot loads `vmm.dll` at runtime, so you don't need the MemProcFS SDK to build it.

### Step 3: KMBox

**KMBox B / B+ / B Pro (serial):** plug the KMBox's serial USB into the aimbot PC and
its mouse output into the game PC, and plug your real mouse into the KMBox. Find the COM
port in Device Manager, then:
```ini
mouse      = kmbox_b
kmbox_port = COM5
kmbox_baud = 115200
```
It sends `km.move(dx,dy)`.

**KMBox Net:** copy the vendor's `kmboxNet.cpp/.h` into `aimbot/third_party/kmboxnet/`,
rebuild, then set `mouse = kmbox_net` with the IP, port and UUID shown on the box.

### Step 4: calibrate

Windows mouse settings on the game PC: pointer speed 10/20 (the default) and
**"Enhance pointer precision" OFF**. Then:
1. Focus the game window and put the cursor in the middle.
2. On the aimbot PC, run `aimbot calibrate`.
3. Put the printed value into `mouse_scale`.

### Tuning (`config.ini`)

| Key | Default | Meaning |
|---|---|---|
| `aim_key` | `rmb` | `rmb`, `lmb`, `mmb`, `always` |
| `fov` | 200 | max distance crosshair → target (pixels) |
| `smooth` | 3 | 1 = snap, higher = smoother. Below 2 can overshoot, because the game only sees the movement one frame later |
| `max_step` | 80 | max pixels per step |
| `prediction` | true | lead moving targets |
| `visible_only` | true | skip enemies behind walls |
| `sticky_target` | true | stay on one target while the key is held |
| `game_rva` | 0 | 0 = scan, otherwise the fixed RVA |
| `poll_interval_us` | 500 | pause between reads |

---

### Measuring the aimbot

**Aim Training** mode gives you comparable numbers for the report. Run it a few times
without the aimbot and a few times with it; each run adds a line to
`shooter_results.csv` (open it in Excel):

```
date,mode,map,difficulty,bots,duration_s,kills,deaths,shots,hits,accuracy,avg_ttk_ms,avg_first_hit_ms,wave,result
```

Example from testing (60 s, holding the fire button, same seed):

| | targets killed | accuracy | avg time to kill | avg time to first hit |
|---|---|---|---|---|
| no aimbot (cursor not moved) | 2 | 2.3 % | 27 s | 5.5 s |
| aimbot (`smooth = 2`) | 28 | 32.9 % | 1.75 s | 0.9 s |

Ideas for experiments: compare `smooth` values, `prediction` on/off, the three
weapons (each has a different bullet speed), or DMA against local reading
(`memory = winapi`).

---

## 5. Troubleshooting

| Problem | Fix |
|---|---|
| `could not load vmm.dll` | copy vmm.dll, leechcore.dll and FTD3XX.dll next to aimbot.exe |
| `VMMDLL_Initialize failed` | check the DMA card and that MemProcFS works by itself; try `dma_args = -device fpga -memmap auto` |
| `process 'shooter.exe' not found` | start the game first; the aimbot keeps retrying anyway |
| `magic not found` | wait until the game window is open; rebuild the aimbot and the game together if the layout changed |
| aim overshoots / shakes | increase `smooth`, run `calibrate`, turn off "Enhance pointer precision" |
| aim too slow | lower `smooth`, raise `max_step` |
| mouse doesn't move (KMBox B) | wrong COM port or baud rate; close other programs that use the port |
