#pragma once

#include "Config.h"

#include <string>
#include <string_view>
#include <vector>

namespace vicon_lsl {

enum class CommandLineAction {
    Run,
    Help,
    Error
};

struct CommandLineResult {
    CommandLineAction action = CommandLineAction::Run;
    Config config{};
    std::string message;

    bool shouldRun() const { return action == CommandLineAction::Run; }
};

CommandLineResult parseCommandLine(int argc, const char* const argv[]);
CommandLineResult parseCommandLine(const std::vector<std::string>& args);

std::string formatUsage(std::string_view program);
std::string formatStartupDiagnostics(const Config& config);

} // namespace vicon_lsl
