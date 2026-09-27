// =============================================================================
//  SendInput - Windows, same PC. For testing the aimbot without a KMBox
//  (use together with memory = winapi).
// =============================================================================
#include "mouse.h"

#include <cstdio>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

namespace {

class SendInputMouse : public MouseOutput {
public:
    const char* Name() const override { return "sendinput (local)"; }
    bool Connect() override { return true; }
    bool Move(int dx, int dy) override {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = dx;
        in.mi.dy = dy;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;  // relative movement
        return SendInput(1, &in, sizeof(INPUT)) == 1;
    }
};

}  // namespace

std::unique_ptr<MouseOutput> CreateSendInput() { return std::make_unique<SendInputMouse>(); }

#else

std::unique_ptr<MouseOutput> CreateSendInput() { return nullptr; }

#endif
