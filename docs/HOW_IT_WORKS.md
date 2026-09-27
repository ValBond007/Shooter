# How it works

Background for the project write-up, from the RAM chips up to the mouse.

## 1. Memory, from the game's point of view

The game keeps its state in a global variable:

```cpp
gm::GameMemory g_game = { "ARENA_SHOOTER!!", 1, sizeof(gm::GameMemory) };
```

- Because it is a **global with an initializer**, the compiler puts it in the
  `.data` section of `shooter.exe`. When Windows loads the exe, the whole image
  (code, `.rdata`, `.data`, …) is mapped at the **module base**. So
  `&g_game - moduleBase` (the **RVA**) is the same on every run of the same build.
  ASLR only changes the module base.
- The struct uses only fixed-size types and is checked with `static_assert`, so its
  layout is known exactly: `health` of entity *i* is always at
  `g_game + 0x80 + i*0x60 + 0x10`.

Real games are harder: objects on the heap, pointer chains (`base → ptr → ptr → value`),
changing offsets after updates, and sometimes encrypted values. This game keeps
everything easy on purpose, so the project can focus on the DMA part.

## 2. Virtual vs. physical memory

`0x7FF6A2B4C140` is a **virtual** address. It only means something inside the game
process. The CPU translates it to a **physical** RAM address using the process's
**page tables** (x64: 4 levels, PML4 → PDPT → PD → PT, 4 KiB pages). The root of
those tables is the process's `CR3` / `DirectoryTableBase`.

A DMA card only sees **physical** memory. So to read `g_game` from outside you
have to:

1. Find the game process in physical memory. Windows keeps a list of processes
   (`EPROCESS` structures); each one holds the process name, PID and `DirectoryTableBase`.
2. Walk the page tables yourself to translate virtual → physical.
3. Find the module base (from the PEB's loader list / VADs).

**MemProcFS** does all of this for us. `VMMDLL_PidGetFromName`,
`VMMDLL_ProcessGetModuleBaseU` and `VMMDLL_MemReadEx` hide the page walking.

## 3. DMA

The FPGA card sits in a PCIe slot of the game PC and can issue memory read
requests over PCIe (**D**irect **M**emory **A**ccess) without the CPU or the OS being
involved. It sends the data over USB to the second PC, where LeechCore talks to the
card and MemProcFS runs on top of it.

```
aimbot.exe → vmm.dll (MemProcFS) → leechcore.dll → FTD3XX (USB3) → FPGA card → PCIe → RAM of the game PC
```

Consequences you can measure with `aimbot bench`:
- Each read is a **round trip** over USB + PCIe, so latency is much higher than a local
  `ReadProcessMemory`.
- Reading 8 bytes costs about as much as reading 4 KiB, so **few big reads beat many
  small ones**. That is why the aimbot reads the whole `GameMemory` (0xFA0 bytes) in
  one go instead of reading each field.
- Memory can change while it is being read (the game writes, we read), so a snapshot
  can mix two frames. For an aimbot that's fine; `frameCount` shows how fresh the
  data is.

## 4. Finding `g_game` without hard-coded addresses

`GameReader::ScanForMagic` in `aimbot/src/game_reader.cpp`:

1. Read the PE header at the module base: `e_lfanew` at `+0x3C`, then
   `SizeOfImage` at `NT headers + 0x50`.
2. Read the module in 64 KiB chunks and search for `"ARENA_SHOOTER!!\0"`.
3. For each hit, check `layoutVersion`, `structSize` and **`selfAddress == hit`**. The
   game writes `&g_game` into that field at startup, so a random copy of the string
   can never pass the check.

This is the same idea as a *signature scan* for a byte pattern in real tools.

## 5. What the player can't see is still in memory

With fog of war on, the game doesn't draw enemies that are behind walls. It still
simulates them, though: their position, health, weapon and ammo stay in `entities[]`
and are updated every tick. That's why `aimbot radar` shows every enemy (`e` = hidden)
while the screen shows only the visible ones.

This is how "wallhacks" and radar cheats work in real games. Anything the client has
in RAM can be read, whatever it draws. Competitive games fight this on the server
side and send enemy positions only when they could be visible.

## 6. From memory to the screen

The camera is also in memory (`camTarget`, `camOffset`, `camZoom`), so the aimbot can
compute where an enemy is drawn:

```
screen = (world - camTarget) * camZoom + camOffset
```

The crosshair is the mouse cursor (`mouseScreen`), so the aim error is simply
`targetScreen - mouseScreen`, in pixels.

## 7. Prediction

Bullets take time to arrive (rifle 1300, shotgun 1000, sniper 2600 units/s; the
current value is `bulletSpeed` in memory). A bot moving at 250 units/s,
600 units away, moves about 115 units before a rifle bullet gets there. Aiming at the
current position misses, so the aimbot aims where the bot will be:

```
t     = distance(me, target) / bulletSpeed
aimAt = target.pos + target.vel * t        (repeat 3x, because the distance changes)
```

`vel` is stored in memory. Bots change direction often, so prediction helps but
isn't perfect.

## 8. Moving the mouse (closed loop)

The KMBox acts as a real USB mouse for the game PC: it receives "move by
(dx, dy) counts" and sends those as normal mouse reports. Windows turns counts into
cursor pixels (1:1 at pointer speed 10/20 without "Enhance pointer precision").

The aimbot does **not** jump to the target in one go. Each frame it moves
`error / smooth` and then reads the new cursor position back from memory:

```
frame N  : read error = 90 px  → move 30 (smooth 3)
frame N+1: read error = 60 px  → move 20
frame N+2: read error = 40 px  → move 13 ...
```

This is a simple **proportional controller**. Because the new cursor position is read
from memory, small errors (DPI scaling, acceleration, a wrong `mouse_scale`) correct
themselves. It only moves once per new `frameCount`: the game samples the cursor once
per frame, and moving again before that would use stale data and overshoot.

## 9. Measuring the result

Aim Training mode spawns one moving target at a time for 60 s and measures:

- **time to first hit**: from the target appearing to the first bullet hitting it (reaction + aiming)
- **time to kill**: from appearing to dying (100 HP = 4 rifle hits)
- **accuracy**: hits / shots

Every match is saved to `shooter_results.csv`, so runs with and without the aimbot (or
with different settings) can be compared in a table or chart.

## 10. Code map

| File | Role |
|---|---|
| `shared/game_memory.h` | layout, shared by game and aimbot |
| `game/src/memory_export.cpp` | defines `g_game`, prints addresses |
| `game/src/game.cpp` | simulation, bot AI, bullets |
| `game/src/render.cpp` | drawing, fog of war, HUD, F1 debug overlay |
| `game/src/audio.cpp` | generated sound effects |
| `game/src/maps.cpp` | the three map layouts |
| `game/src/ui.cpp` | main menu, pause menu, result screen |
| `aimbot/src/memory/memory_dma.cpp` | MemProcFS backend |
| `aimbot/src/memory/memory_winapi.cpp` | ReadProcessMemory backend (testing) |
| `aimbot/src/game_reader.cpp` | find `g_game`, read snapshots |
| `aimbot/src/aimbot.cpp` | target selection, prediction, smoothing |
| `aimbot/src/input/mouse_kmbox_b.cpp` | KMBox B serial output |
| `aimbot/src/main.cpp` | modes: aim, dump, radar, info, bench, calibrate |
