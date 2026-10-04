#include "config.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ParseBool(const std::string& v) {
    std::string l = Lower(v);
    return l == "1" || l == "true" || l == "yes" || l == "on";
}

}  // namespace

bool Config::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    int lineNo = 0;
    for (std::string line; std::getline(f, line);) {
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = Trim(line);
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            std::fprintf(stderr, "[config] %s:%d: expected key = value\n", path.c_str(), lineNo);
            continue;
        }
        std::string key = Lower(Trim(line.substr(0, eq)));
        std::string val = Trim(line.substr(eq + 1));

        if (key == "process") process = val;
        else if (key == "game_rva") gameRva = std::strtoull(val.c_str(), nullptr, 0);  // accepts 0x...
        else if (key == "memory") memory = Lower(val);
        else if (key == "dma_args") dmaArgs = val;
        else if (key == "mouse") mouse = Lower(val);
        else if (key == "kmbox_port") kmboxPort = val;
        else if (key == "kmbox_baud") kmboxBaud = std::atoi(val.c_str());
        else if (key == "kmnet_ip") kmnetIp = val;
        else if (key == "kmnet_port") kmnetPort = val;
        else if (key == "kmnet_uuid") kmnetUuid = val;
        else if (key == "aim_key") aimKey = Lower(val);
        else if (key == "fov") fov = std::strtof(val.c_str(), nullptr);
        else if (key == "smooth") smooth = std::max(1.0f, std::strtof(val.c_str(), nullptr));
        else if (key == "max_step") maxStep = std::strtof(val.c_str(), nullptr);
        else if (key == "deadzone") deadzone = std::strtof(val.c_str(), nullptr);
        else if (key == "mouse_scale") mouseScale = std::strtof(val.c_str(), nullptr);
        else if (key == "prediction") prediction = ParseBool(val);
        else if (key == "visible_only") visibleOnly = ParseBool(val);
        else if (key == "sticky_target") stickyTarget = ParseBool(val);
        else if (key == "poll_interval_us") pollIntervalUs = std::max(0, std::atoi(val.c_str()));
        else if (key == "move_interval_ms") moveIntervalMs = std::max(0.0f, std::strtof(val.c_str(), nullptr));
        else if (key == "move_timeout_ms") moveTimeoutMs = std::max(1.0f, std::strtof(val.c_str(), nullptr));
        else std::fprintf(stderr, "[config] %s:%d: unknown key '%s'\n", path.c_str(), lineNo, key.c_str());
    }
    return true;
}

void Config::Print() const {
    std::printf("  process      = %s\n", process.c_str());
    std::printf("  game_rva     = 0x%" PRIX64 "%s\n", gameRva, gameRva ? "" : " (auto scan)");
    std::printf("  memory       = %s%s%s\n", memory.c_str(), memory == "dma" ? "  args: " : "",
                memory == "dma" ? dmaArgs.c_str() : "");
    std::printf("  mouse        = %s\n", mouse.c_str());
    std::printf("  aim_key      = %s   fov %.0f px   smooth %.1f   max_step %.0f   mouse_scale %.3f\n",
                aimKey.c_str(), fov, smooth, maxStep, mouseScale);
    std::printf("  move_interval_ms %.1f   move_timeout_ms %.0f\n", moveIntervalMs, moveTimeoutMs);
    std::printf("  prediction   = %s   visible_only %s   sticky_target %s\n", prediction ? "on" : "off",
                visibleOnly ? "on" : "off", stickyTarget ? "on" : "off");
}
