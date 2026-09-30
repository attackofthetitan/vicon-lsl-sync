#include "ViconServerAddress.h"

#include <algorithm>
#include <utility>

namespace vicon_lsl {
namespace {

constexpr const char* kDefaultViconPort = "801";

bool isPortNumber(const std::string& text) {
    if (text.empty()) return false;
    unsigned long value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return false;
        value = value * 10 + static_cast<unsigned long>(ch - '0');
        if (value > 65535) return false;
    }
    return true;
}

} // namespace

std::vector<ViconServerEndpoint> viconServerEndpoints(const std::string& address) {
    std::vector<ViconServerEndpoint> endpoints;
    std::size_t start = 0;
    while (start <= address.size()) {
        const std::size_t end = (std::min)(address.find(';', start), address.size());
        std::string host = address.substr(start, end - start);
        std::string port = kDefaultViconPort;
        const std::size_t colon = host.rfind(':');
        bool usable = true;
        if (colon != std::string::npos) {
            port = host.substr(colon + 1);
            host.erase(colon);
            usable = isPortNumber(port);
        }
        if (usable && !host.empty()) endpoints.push_back({std::move(host), std::move(port)});
        start = end + 1;
    }
    return endpoints;
}

} // namespace vicon_lsl
