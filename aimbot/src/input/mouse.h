// =============================================================================
//  MouseOutput - "how do we move the mouse?"
//
//  Implementations:
//    kmbox_b   : KMBox B / B+ / B Pro over a (USB) serial port   mouse_kmbox_b.cpp
//    kmbox_net : KMBox Net over the network (vendor library)     mouse_kmbox_net.cpp
//    sendinput : Windows SendInput on the same PC (testing)      mouse_sendinput.cpp
//    x11       : XTest on Linux (development/testing)            mouse_x11.cpp
//    none      : does nothing, only prints (dry run)
//
//  All of them take RELATIVE movement in mouse counts (like a real mouse).
// =============================================================================
#pragma once

#include <memory>
#include <string>

struct Config;

class MouseOutput {
public:
    virtual ~MouseOutput() = default;
    virtual const char* Name() const = 0;
    virtual bool Connect() = 0;
    virtual bool Move(int dx, int dy) = 0;
};

std::unique_ptr<MouseOutput> CreateKmboxB(const std::string& port, int baud);
std::unique_ptr<MouseOutput> CreateKmboxNet(const std::string& ip, const std::string& port,
                                            const std::string& uuid);
std::unique_ptr<MouseOutput> CreateSendInput();
std::unique_ptr<MouseOutput> CreateX11Mouse();
std::unique_ptr<MouseOutput> CreateNoMouse(bool verbose);

// Creates the output selected in the config ("mouse = ...").
std::unique_ptr<MouseOutput> CreateMouseOutput(const Config& cfg);
