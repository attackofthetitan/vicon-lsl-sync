#pragma once

#include "Config.h"

#include <string>

struct ParseResult {
    bool ok = false;
    bool help_requested = false;
    Config config;
    std::string error;
};

ParseResult parseCommandLine(int argc, char* argv[]);
std::string usageText(const char* program);
