// Settings loaded from config.ini (simple "key = value" lines, # = comment).
#pragma once

#include <cstdint>
#include <string>

struct Config {
    // ---- target process ----------------------------------------------------
    std::string process  = "shooter.exe";
    uint64_t    gameRva  = 0;               // 0 = find g_game by scanning for the magic

    // ---- memory backend ------------------------------------------------------
    std::string memory   = "dma";           // dma | winapi | linux
    std::string dmaArgs  = "-device fpga";  // arguments for VMMDLL_Initialize

    // ---- mouse output ----------------------------------------------------------
    std::string mouse     = "kmbox_b";      // kmbox_b | kmbox_net | sendinput | x11 | none
    std::string kmboxPort = "COM3";
    int         kmboxBaud = 115200;
    std::string kmnetIp   = "192.168.2.188";
    std::string kmnetPort = "8888";
    std::string kmnetUuid = "00000000";

    // ---- aimbot ------------------------------------------------------------------
    std::string aimKey      = "rmb";    // rmb | lmb | mmb | always
    float       fov         = 200.0f;   // max distance (screen pixels) crosshair -> target
    float       smooth      = 3.0f;     // 1 = snap, higher = slower/smoother
    float       maxStep     = 80.0f;    // max pixels moved per step
    float       deadzone    = 1.0f;     // don't move when closer than this (pixels)
    float       mouseScale  = 1.0f;     // mouse counts per screen pixel (see `calibrate`)
    bool        prediction  = true;     // lead moving targets (bullet travel time)
    bool        visibleOnly = true;     // ignore targets behind walls
    bool        stickyTarget = true;    // keep the same target while the key is held

    // ---- loop --------------------------------------------------------------------
    int         pollIntervalUs = 500;   // pause between memory reads (microseconds)

    bool Load(const std::string& path);
    void Print() const;
};
