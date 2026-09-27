#include "mouse.h"

#include <cstdio>

#include "../config.h"

namespace {

class NoMouse : public MouseOutput {
public:
    explicit NoMouse(bool verbose) : verbose_(verbose) {}
    const char* Name() const override { return "none (dry run)"; }
    bool Connect() override { return true; }
    bool Move(int dx, int dy) override {
        if (verbose_) std::printf("[mouse] move(%d, %d)\n", dx, dy);
        return true;
    }

private:
    bool verbose_;
};

}  // namespace

std::unique_ptr<MouseOutput> CreateNoMouse(bool verbose) { return std::make_unique<NoMouse>(verbose); }

std::unique_ptr<MouseOutput> CreateMouseOutput(const Config& cfg) {
    std::unique_ptr<MouseOutput> m;
    if (cfg.mouse == "kmbox_b") m = CreateKmboxB(cfg.kmboxPort, cfg.kmboxBaud);
    else if (cfg.mouse == "kmbox_net") m = CreateKmboxNet(cfg.kmnetIp, cfg.kmnetPort, cfg.kmnetUuid);
    else if (cfg.mouse == "sendinput") m = CreateSendInput();
    else if (cfg.mouse == "x11") m = CreateX11Mouse();
    else if (cfg.mouse == "none") m = CreateNoMouse(false);
    else {
        std::fprintf(stderr, "[mouse] unknown mouse output '%s'\n", cfg.mouse.c_str());
        return nullptr;
    }
    if (!m) std::fprintf(stderr, "[mouse] output '%s' is not available in this build/OS\n", cfg.mouse.c_str());
    return m;
}
