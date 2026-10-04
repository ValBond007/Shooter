// =============================================================================
//  KMBox Net - over the network (UDP), using the vendor library.
//
//  The KMBox Net protocol is implemented by the manufacturer's library
//  (kmboxNet.cpp / kmboxNet.h, shipped with the box / on their website).
//  To enable this backend copy those files into
//      aimbot/third_party/kmboxnet/
//  and re-run CMake. It then defines AIMBOT_HAVE_KMBOXNET automatically.
//
//  IP, port and UUID are shown on the little screen of the KMBox Net.
// =============================================================================
#include "mouse.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#if defined(AIMBOT_HAVE_KMBOXNET)
#  include "kmboxNet.h"

namespace {

class KmboxNet : public MouseOutput {
public:
    KmboxNet(std::string ip, std::string port, std::string uuid)
        : ip_(std::move(ip)), port_(std::move(port)), uuid_(std::move(uuid)) {}

    const char* Name() const override { return "kmbox_net (UDP)"; }

    bool Connect() override {
        // kmNet_init takes non-const char* -> copy into writable buffers.
        std::vector<char> ip(ip_.begin(), ip_.end()), port(port_.begin(), port_.end()),
            uuid(uuid_.begin(), uuid_.end());
        ip.push_back(0);
        port.push_back(0);
        uuid.push_back(0);
        int rc = kmNet_init(ip.data(), port.data(), uuid.data());
        if (rc != 0) {
            std::fprintf(stderr, "[kmbox_net] kmNet_init(%s, %s, %s) failed: %d\n", ip_.c_str(),
                         port_.c_str(), uuid_.c_str(), rc);
            return false;
        }
        std::printf("[kmbox_net] connected to %s:%s\n", ip_.c_str(), port_.c_str());
        return true;
    }

    bool Move(int dx, int dy) override {
        dx = std::clamp(dx, -32767, 32767);
        dy = std::clamp(dy, -32767, 32767);
        return kmNet_mouse_move(static_cast<short>(dx), static_cast<short>(dy)) == 0;
    }

private:
    std::string ip_, port_, uuid_;
};

}  // namespace

std::unique_ptr<MouseOutput> CreateKmboxNet(const std::string& ip, const std::string& port,
                                            const std::string& uuid) {
    return std::make_unique<KmboxNet>(ip, port, uuid);
}

#else

std::unique_ptr<MouseOutput> CreateKmboxNet(const std::string&, const std::string&, const std::string&) {
    std::fprintf(stderr,
                 "[kmbox_net] this build has no KMBox Net support.\n"
                 "            Copy kmboxNet.cpp/.h (from the KMBox vendor) into\n"
                 "            aimbot/third_party/kmboxnet/ and rebuild.\n");
    return nullptr;
}

#endif
