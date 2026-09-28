// =============================================================================
//  XTest - Linux/X11, same machine. Only for development/testing.
// =============================================================================
#include "mouse.h"

#include <cstdio>

#if defined(AIMBOT_HAVE_X11)
#  include <X11/Xlib.h>
#  include <X11/extensions/XTest.h>

namespace {

class X11Mouse : public MouseOutput {
public:
    ~X11Mouse() override {
        if (display_) XCloseDisplay(display_);
    }
    const char* Name() const override { return "x11 (XTest)"; }
    bool Connect() override {
        display_ = XOpenDisplay(nullptr);
        if (!display_) std::fprintf(stderr, "[x11] cannot open display\n");
        return display_ != nullptr;
    }
    bool Move(int dx, int dy) override {
        XTestFakeRelativeMotionEvent(display_, dx, dy, CurrentTime);
        XFlush(display_);
        return true;
    }

private:
    Display* display_ = nullptr;
};

}  // namespace

std::unique_ptr<MouseOutput> CreateX11Mouse() { return std::make_unique<X11Mouse>(); }

#else

std::unique_ptr<MouseOutput> CreateX11Mouse() { return nullptr; }

#endif
