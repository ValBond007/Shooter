// =============================================================================
//  KMBox B / B+ / B Pro - serial (COM port over USB).
//
//  The box runs a small Python-like interpreter. We send text commands:
//      km.move(dx,dy)\r\n
//  and it moves the mouse like a real USB mouse would (the game PC only sees
//  a normal hardware mouse).
//
//  Default baud rate: 115200 (change `kmbox_baud` if you changed it on the box).
//  Find the COM port in the Windows Device Manager ("Ports (COM & LPT)").
// =============================================================================
#include "mouse.h"

#include <algorithm>
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

class KmboxB : public MouseOutput {
public:
    KmboxB(std::string port, int baud) : port_(std::move(port)), baud_(baud) {}
    ~KmboxB() override {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    }

    const char* Name() const override { return "kmbox_b (serial)"; }

    bool Connect() override {
        // "\\.\COM10" syntax works for all port numbers.
        std::string path = "\\\\.\\" + port_;
        handle_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "[kmbox_b] cannot open %s (error %lu)\n", port_.c_str(), GetLastError());
            return false;
        }

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        GetCommState(handle_, &dcb);
        dcb.BaudRate = static_cast<DWORD>(baud_);
        dcb.ByteSize = 8;
        dcb.Parity   = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        if (!SetCommState(handle_, &dcb)) {
            std::fprintf(stderr, "[kmbox_b] SetCommState failed (error %lu)\n", GetLastError());
            return false;
        }

        // Reads return immediately with whatever is buffered (we only read to
        // throw away the echo the box sends back).
        COMMTIMEOUTS t{};
        t.ReadIntervalTimeout = MAXDWORD;
        t.WriteTotalTimeoutConstant = 50;
        SetCommTimeouts(handle_, &t);
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);

        std::printf("[kmbox_b] opened %s @ %d baud\n", port_.c_str(), baud_);
        return true;
    }

    bool Move(int dx, int dy) override {
        dx = std::clamp(dx, -32767, 32767);
        dy = std::clamp(dy, -32767, 32767);
        char cmd[64];
        int len = std::snprintf(cmd, sizeof(cmd), "km.move(%d,%d)\r\n", dx, dy);
        DWORD written = 0;
        bool ok = WriteFile(handle_, cmd, static_cast<DWORD>(len), &written, nullptr) &&
                  written == static_cast<DWORD>(len);
        DrainEcho();
        return ok;
    }

private:
    void DrainEcho() {
        char buf[256];
        DWORD n = 0;
        while (ReadFile(handle_, buf, sizeof(buf), &n, nullptr) && n > 0) {}
    }

    std::string port_;
    int         baud_;
    HANDLE      handle_ = INVALID_HANDLE_VALUE;
};

}  // namespace

std::unique_ptr<MouseOutput> CreateKmboxB(const std::string& port, int baud) {
    return std::make_unique<KmboxB>(port, baud);
}

#else

std::unique_ptr<MouseOutput> CreateKmboxB(const std::string&, int) { return nullptr; }

#endif
