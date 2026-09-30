#include "ViconServerAddress.h"
#include "TestSupport.h"

#include <string>
#include <vector>

namespace {

std::vector<std::string> endpointTexts(const std::string& address) {
    std::vector<std::string> texts;
    for (const auto& endpoint : vicon_lsl::viconServerEndpoints(address)) {
        texts.push_back(endpoint.host + " " + endpoint.port);
    }
    return texts;
}

} // namespace

TEST_CASE("Vicon server addresses default to port 801") {
    REQUIRE(endpointTexts("localhost") == std::vector<std::string>{"localhost 801"});
    REQUIRE(endpointTexts("10.0.0.2:802") == std::vector<std::string>{"10.0.0.2 802"});
}

TEST_CASE("Vicon server addresses list several hosts as the Vicon SDK does") {
    REQUIRE(endpointTexts("capture-a:801;capture-b") ==
            (std::vector<std::string>{"capture-a 801", "capture-b 801"}));
    // The SDK skips a host whose port is not a number, and empty entries.
    REQUIRE(endpointTexts("bad:port;;good:65535;big:65536;:801") ==
            std::vector<std::string>{"good 65535"});
    REQUIRE(endpointTexts("").empty());
}
