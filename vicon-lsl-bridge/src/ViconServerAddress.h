#pragma once

#include <string>
#include <vector>

namespace vicon_lsl {

struct ViconServerEndpoint {
    std::string host;
    std::string port;
};

// Splits a Vicon server address as the Vicon SDK does: hosts separated by ';',
// each with an optional ":port" (801 if none), skipping a host whose port is
// not a number from 0 to 65535.
std::vector<ViconServerEndpoint> viconServerEndpoints(const std::string& address);

} // namespace vicon_lsl
