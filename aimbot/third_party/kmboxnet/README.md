# KMBox Net vendor library goes here

Only needed if you use a **KMBox Net** (`mouse = kmbox_net`). The KMBox B / B+ /
B Pro (serial) works without it.

1. Get the official KMBox Net C++ library from the manufacturer (it comes with
   the device / on their download page): `kmboxNet.cpp`, `kmboxNet.h` and the
   files they include (e.g. `HidTable.h`).
2. Copy them into this folder.
3. Re-run CMake (or `build.bat`). You should see
   `aimbot: KMBox Net support enabled`.

The aimbot uses only `kmNet_init(ip, port, uuid)` and `kmNet_mouse_move(x, y)`.
